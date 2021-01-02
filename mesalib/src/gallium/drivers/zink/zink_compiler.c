/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "zink_context.h"
#include "zink_compiler.h"
#include "zink_program.h"
#include "zink_screen.h"
#include "nir_to_spirv/nir_to_spirv.h"

#include "pipe/p_state.h"

#include "nir.h"
#include "compiler/nir/nir_builder.h"

#include "nir/tgsi_to_nir.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_from_mesa.h"

#include "util/u_memory.h"

static bool
lower_discard_if_instr(nir_intrinsic_instr *instr, nir_builder *b)
{
   if (instr->intrinsic == nir_intrinsic_discard_if) {
      b->cursor = nir_before_instr(&instr->instr);

      nir_if *if_stmt = nir_push_if(b, nir_ssa_for_src(b, instr->src[0], 1));
      nir_intrinsic_instr *discard =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_discard);
      nir_builder_instr_insert(b, &discard->instr);
      nir_pop_if(b, if_stmt);
      nir_instr_remove(&instr->instr);
      return true;
   }
   /* a shader like this (shaders@glsl-fs-discard-04):

      uniform int j, k;

      void main()
      {
       for (int i = 0; i < j; i++) {
        if (i > k)
         continue;
        discard;
       }
       gl_FragColor = vec4(0.0, 1.0, 0.0, 0.0);
      }



      will generate nir like:

      loop   {
         //snip
         if   ssa_11   {
            block   block_5:
            /   preds:   block_4   /
            vec1   32   ssa_17   =   iadd   ssa_50,   ssa_31
            /   succs:   block_7   /
         }   else   {
            block   block_6:
            /   preds:   block_4   /
            intrinsic   discard   ()   () <-- not last instruction
            vec1   32   ssa_23   =   iadd   ssa_50,   ssa_31 <-- dead code loop itr increment
            /   succs:   block_7   /
         }
         //snip
      }

      which means that we can't assert like this:

      assert(instr->intrinsic != nir_intrinsic_discard ||
             nir_block_last_instr(instr->instr.block) == &instr->instr);


      and it's unnecessary anyway since post-vtn optimizing will dce the instructions following the discard
    */

   return false;
}

static bool
lower_discard_if(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder builder;
         nir_builder_init(&builder, function->impl);
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type == nir_instr_type_intrinsic)
                  progress |= lower_discard_if_instr(
                                                  nir_instr_as_intrinsic(instr),
                                                  &builder);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_dominance);
      }
   }

   return progress;
}

static bool
lower_64bit_vertex_attribs_instr(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_deref)
      return false;
   nir_deref_instr *deref = nir_instr_as_deref(instr);
   if (deref->deref_type != nir_deref_type_var)
      return false;
   nir_variable *var = nir_deref_instr_get_variable(deref);
   if (var->data.mode != nir_var_shader_in)
      return false;
   if (!glsl_type_is_64bit(var->type) || !glsl_type_is_vector(var->type) || glsl_get_vector_elements(var->type) < 3)
      return false;

   /* create second variable for the split */
   nir_variable *var2 = nir_variable_clone(var, b->shader);
   /* split new variable into second slot */
   var2->data.driver_location++;
   nir_shader_add_variable(b->shader, var2);

   unsigned total_num_components = glsl_get_vector_elements(var->type);
   /* new variable is the second half of the dvec */
   var2->type = glsl_vector_type(glsl_get_base_type(var->type), glsl_get_vector_elements(var->type) - 2);
   /* clamp original variable to a dvec2 */
   deref->type = var->type = glsl_vector_type(glsl_get_base_type(var->type), 2);

   /* create deref instr for new variable */
   b->cursor = nir_after_instr(instr);
   nir_deref_instr *deref2 = nir_build_deref_var(b, var2);

   nir_foreach_use_safe(use_src, &deref->dest.ssa) {
      nir_instr *use_instr = use_src->parent_instr;
      assert(use_instr->type == nir_instr_type_intrinsic &&
             nir_instr_as_intrinsic(use_instr)->intrinsic == nir_intrinsic_load_deref);

      /* this is a load instruction for the deref, and we need to split it into two instructions that we can
       * then zip back into a single ssa def */
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(use_instr);
      /* clamp the first load to 2 64bit components */
      intr->num_components = intr->dest.ssa.num_components = 2;
      b->cursor = nir_after_instr(use_instr);
      /* this is the second load instruction for the second half of the dvec3/4 components */
      nir_intrinsic_instr *intr2 = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_deref);
      intr2->src[0] = nir_src_for_ssa(&deref2->dest.ssa);
      intr2->num_components = total_num_components - 2;
      nir_ssa_dest_init(&intr2->instr, &intr2->dest, intr2->num_components, 64, NULL);
      nir_builder_instr_insert(b, &intr2->instr);

      nir_ssa_def *def[4];
      /* create a new dvec3/4 comprised of all the loaded components from both variables */
      def[0] = nir_vector_extract(b, &intr->dest.ssa, nir_imm_int(b, 0));
      def[1] = nir_vector_extract(b, &intr->dest.ssa, nir_imm_int(b, 1));
      def[2] = nir_vector_extract(b, &intr2->dest.ssa, nir_imm_int(b, 0));
      if (total_num_components == 4)
         def[3] = nir_vector_extract(b, &intr2->dest.ssa, nir_imm_int(b, 1));
      nir_ssa_def *new_vec = nir_vec(b, def, total_num_components);
      /* use the assembled dvec3/4 for all other uses of the load */
      nir_ssa_def_rewrite_uses_after(&intr->dest.ssa, nir_src_for_ssa(new_vec), new_vec->parent_instr);
   }

   return true;
}

/* "64-bit three- and four-component vectors consume two consecutive locations."
 *  - 14.1.4. Location Assignment
 *
 * this pass splits dvec3 and dvec4 vertex inputs into a dvec2 and a double/dvec2 which
 * are assigned to consecutive locations, loaded separately, and then assembled back into a
 * composite value that's used in place of the original loaded ssa src
 */
static bool
lower_64bit_vertex_attribs(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_VERTEX)
      return false;

   return nir_shader_instructions_pass(shader, lower_64bit_vertex_attribs_instr, nir_metadata_dominance, NULL);
}

static const struct nir_shader_compiler_options nir_options = {
   .lower_ffma16 = true,
   .lower_ffma32 = true,
   .lower_ffma64 = true,
   .lower_fdph = true,
   .lower_flrp32 = true,
   .lower_fpow = true,
   .lower_fsat = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_mul_high = true,
   .lower_rotate = true,
   .lower_uadd_carry = true,
   .lower_pack_64_2x32_split = true,
   .lower_unpack_64_2x32_split = true,
   .use_scoped_barrier = true,
   .lower_int64_options = ~0,
   .lower_doubles_options = ~nir_lower_fp64_full_software,
};

static const struct nir_shader_compiler_options softfp_nir_options = {
   .lower_ffma16 = true,
   .lower_ffma32 = true,
   .lower_ffma64 = true,
   .lower_fdph = true,
   .lower_flrp32 = true,
   .lower_fpow = true,
   .lower_fsat = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_mul_high = true,
   .lower_rotate = true,
   .lower_uadd_carry = true,
   .lower_pack_64_2x32_split = true,
   .lower_unpack_64_2x32_split = true,
   .use_scoped_barrier = true,
   .lower_int64_options = ~0,
   .lower_doubles_options = ~0,
};

const void *
zink_get_compiler_options(struct pipe_screen *screen,
                          enum pipe_shader_ir ir,
                          enum pipe_shader_type shader)
{
   assert(ir == PIPE_SHADER_IR_NIR);
   struct zink_screen *zscreen = zink_screen(screen);
   /* do we actually want this? fails a lot and not just from bugs I've added */
   if (!zscreen->info.feats.features.shaderFloat64)
      return &softfp_nir_options;
   return &nir_options;
}

struct nir_shader *
zink_tgsi_to_nir(struct pipe_screen *screen, const struct tgsi_token *tokens)
{
   if (zink_debug & ZINK_DEBUG_TGSI) {
      fprintf(stderr, "TGSI shader:\n---8<---\n");
      tgsi_dump_to_file(tokens, 0, stderr);
      fprintf(stderr, "---8<---\n\n");
   }

   return tgsi_to_nir(tokens, screen, false);
}

static void
optimize_nir(struct nir_shader *s)
{
   bool progress;
   do {
      progress = false;
      NIR_PASS_V(s, nir_lower_vars_to_ssa);
      NIR_PASS(progress, s, nir_copy_prop);
      NIR_PASS(progress, s, nir_opt_remove_phis);
      NIR_PASS(progress, s, nir_opt_dce);
      NIR_PASS(progress, s, nir_opt_dead_cf);
      NIR_PASS(progress, s, nir_opt_cse);
      NIR_PASS(progress, s, nir_opt_peephole_select, 8, true, true);
      NIR_PASS(progress, s, nir_opt_algebraic);
      NIR_PASS(progress, s, nir_opt_constant_folding);
      NIR_PASS(progress, s, nir_opt_undef);
      NIR_PASS(progress, s, zink_nir_lower_b2b);
   } while (progress);
}

/* check for a genuine gl_PointSize output vs one from nir_lower_point_size_mov */
static bool
check_psiz(struct nir_shader *s)
{
   nir_foreach_shader_out_variable(var, s) {
      if (var->data.location == VARYING_SLOT_PSIZ) {
         /* genuine PSIZ outputs will have this set */
         return !!var->data.explicit_location;
      }
   }
   return false;
}

/* semi-copied from iris */
static void
update_so_info(struct zink_shader *sh,
               uint64_t outputs_written, bool have_psiz)
{
   uint8_t reverse_map[64] = {};
   unsigned slot = 0;
   while (outputs_written) {
      int bit = u_bit_scan64(&outputs_written);
      /* PSIZ from nir_lower_point_size_mov breaks stream output, so always skip it */
      if (bit == VARYING_SLOT_PSIZ && !have_psiz)
         continue;
      reverse_map[slot++] = bit;
   }

   for (unsigned i = 0; i < sh->streamout.so_info.num_outputs; i++) {
      struct pipe_stream_output *output = &sh->streamout.so_info.output[i];
      /* Map Gallium's condensed "slots" back to real VARYING_SLOT_* enums */
      sh->streamout.so_info_slots[i] = reverse_map[output->register_index];
   }
}

VkShaderModule
zink_shader_compile(struct zink_screen *screen, struct zink_shader *zs, struct zink_shader_key *key,
                    unsigned char *shader_slot_map, unsigned char *shader_slots_reserved)
{
   VkShaderModule mod = VK_NULL_HANDLE;
   void *streamout = NULL;
   nir_shader *nir = zs->nir;
   /* TODO: use a separate mem ctx here for ralloc */
   if (zs->has_geometry_shader) {
      if (zs->nir->info.stage == MESA_SHADER_GEOMETRY) {
         streamout = &zs->streamout;
         NIR_PASS_V(nir, nir_lower_clip_halfz);
      }
   } else if (zs->has_tess_shader) {
      if (zs->nir->info.stage == MESA_SHADER_TESS_EVAL) {
         streamout = &zs->streamout;
         NIR_PASS_V(nir, nir_lower_clip_halfz);
      }
   } else {
      streamout = &zs->streamout;
      NIR_PASS_V(nir, nir_lower_clip_halfz);
   }
   if (!zs->streamout.so_info_slots)
       streamout = NULL;
   if (zs->nir->info.stage == MESA_SHADER_FRAGMENT) {
      nir = nir_shader_clone(NULL, nir);
      if (!zink_fs_key(key)->samples &&
          nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK)) {
         /* VK will always use gl_SampleMask[] values even if sample count is 0,
          * so we need to skip this write here to mimic GL's behavior of ignoring it
          */
         nir_foreach_shader_out_variable(var, nir) {
            if (var->data.location == FRAG_RESULT_SAMPLE_MASK)
               var->data.mode = nir_var_shader_temp;
         }
         nir_fixup_deref_modes(nir);
         NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_shader_temp, NULL);
         optimize_nir(nir);
      }
   }
   struct spirv_shader *spirv = nir_to_spirv(nir, streamout, shader_slot_map, shader_slots_reserved);
   assert(spirv);

   if (zink_debug & ZINK_DEBUG_SPIRV) {
      char buf[256];
      static int i;
      snprintf(buf, sizeof(buf), "dump%02d.spv", i++);
      FILE *fp = fopen(buf, "wb");
      if (fp) {
         fwrite(spirv->words, sizeof(uint32_t), spirv->num_words, fp);
         fclose(fp);
         fprintf(stderr, "wrote '%s'...\n", buf);
      }
   }

   VkShaderModuleCreateInfo smci = {};
   smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   smci.codeSize = spirv->num_words * sizeof(uint32_t);
   smci.pCode = spirv->words;

   if (vkCreateShaderModule(screen->dev, &smci, NULL, &mod) != VK_SUCCESS)
      mod = VK_NULL_HANDLE;

   if (zs->nir->info.stage == MESA_SHADER_FRAGMENT)
      ralloc_free(nir);

   /* TODO: determine if there's any reason to cache spirv output? */
   ralloc_free(spirv);
   return mod;
}

struct zink_shader *
zink_shader_create(struct zink_screen *screen, struct nir_shader *nir,
                   const struct pipe_stream_output_info *so_info)
{
   struct zink_shader *ret = CALLOC_STRUCT(zink_shader);
   bool have_psiz = false;

   ret->shader_id = p_atomic_inc_return(&screen->shader_id);
   ret->programs = _mesa_pointer_set_create(NULL);

   /* only do uniforms -> ubo if we have uniforms, otherwise we're just
    * screwing with the bindings for no reason
    */
   if (nir->num_uniforms)
      NIR_PASS_V(nir, nir_lower_uniforms_to_ubo, 16);
   if (nir->info.stage < MESA_SHADER_FRAGMENT)
      have_psiz = check_psiz(nir);
   if (nir->info.stage == MESA_SHADER_GEOMETRY)
      NIR_PASS_V(nir, nir_lower_gs_intrinsics, nir_lower_gs_intrinsics_per_stream);
   NIR_PASS_V(nir, nir_lower_regs_to_ssa);
   optimize_nir(nir);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
   NIR_PASS_V(nir, lower_discard_if);
   NIR_PASS_V(nir, nir_lower_fragcolor);
   NIR_PASS_V(nir, lower_64bit_vertex_attribs);
   NIR_PASS_V(nir, nir_convert_from_ssa, true);

   if (zink_debug & ZINK_DEBUG_NIR) {
      fprintf(stderr, "NIR shader:\n---8<---\n");
      nir_print_shader(nir, stderr);
      fprintf(stderr, "---8<---\n");
   }

   ret->num_bindings = 0;
   uint32_t cur_ubo = 0;
   /* UBO buffers are zero-indexed, but buffer 0 is always the one created by nir_lower_uniforms_to_ubo,
    * which means there is no buffer 0 if there are no uniforms
    */
   int ubo_index = !nir->num_uniforms;
   /* need to set up var->data.binding for UBOs, which means we need to start at
    * the "first" UBO, which is at the end of the list
    */
   foreach_list_typed_reverse(nir_variable, var, node, &nir->variables) {
      if (_nir_shader_variable_has_mode(var, nir_var_uniform |
                                        nir_var_mem_ubo |
                                        nir_var_mem_ssbo)) {
         if (var->data.mode == nir_var_mem_ubo) {
            /* ignore variables being accessed if they aren't the base of the UBO */
            if (var->data.location)
               continue;
            var->data.binding = cur_ubo++;

            int binding = zink_binding(nir->info.stage,
                                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                       var->data.binding);
            ret->bindings[ret->num_bindings].index = ubo_index++;
            ret->bindings[ret->num_bindings].binding = binding;
            ret->bindings[ret->num_bindings].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            ret->num_bindings++;
         } else {
            assert(var->data.mode == nir_var_uniform);
            if (glsl_type_is_sampler(var->type)) {
               VkDescriptorType vktype = zink_sampler_type(var->type);
               int binding = zink_binding(nir->info.stage,
                                          vktype,
                                          var->data.binding);
               ret->bindings[ret->num_bindings].index = var->data.binding;
               ret->bindings[ret->num_bindings].binding = binding;
               ret->bindings[ret->num_bindings].type = vktype;
               ret->num_bindings++;
            } else if (glsl_type_is_array(var->type)) {
               /* need to unroll possible arrays of arrays before checking type
                * in order to handle ARB_arrays_of_arrays extension
                */
               const struct glsl_type *type = glsl_without_array(var->type);
               if (!glsl_type_is_sampler(type))
                  continue;
               VkDescriptorType vktype = zink_sampler_type(type);

               unsigned size = glsl_get_aoa_size(var->type);
               for (int i = 0; i < size; ++i) {
                  int binding = zink_binding(nir->info.stage,
                                             vktype,
                                             var->data.binding + i);
                  ret->bindings[ret->num_bindings].index = var->data.binding + i;
                  ret->bindings[ret->num_bindings].binding = binding;
                  ret->bindings[ret->num_bindings].type = vktype;
                  ret->num_bindings++;
               }
            }
         }
      }
   }

   ret->nir = nir;
   if (so_info) {
      memcpy(&ret->streamout.so_info, so_info, sizeof(struct pipe_stream_output_info));
      ret->streamout.so_info_slots = malloc(so_info->num_outputs * sizeof(unsigned int));
      assert(ret->streamout.so_info_slots);
      update_so_info(ret, nir->info.outputs_written, have_psiz);
   }

   return ret;
}

void
zink_shader_free(struct zink_context *ctx, struct zink_shader *shader)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   set_foreach(shader->programs, entry) {
      struct zink_gfx_program *prog = (void*)entry->key;
      _mesa_hash_table_remove_key(ctx->program_cache, prog->shaders);
      prog->shaders[pipe_shader_type_from_mesa(shader->nir->info.stage)] = NULL;
      if (shader->nir->info.stage == MESA_SHADER_TESS_EVAL && shader->generated)
            /* automatically destroy generated tcs shaders when tes is destroyed */
            zink_shader_free(ctx, shader->generated);
      zink_gfx_program_reference(screen, &prog, NULL);
   }
   _mesa_set_destroy(shader->programs, NULL);
   free(shader->streamout.so_info_slots);
   ralloc_free(shader->nir);
   FREE(shader);
}


/* creating a passthrough tcs shader that's roughly:

#version 150
#extension GL_ARB_tessellation_shader : require

in vec4 some_var[gl_MaxPatchVertices];
out vec4 some_var_out;

layout(push_constant) uniform tcsPushConstants {
    layout(offset = 0) float TessLevelInner[2];
    layout(offset = 8) float TessLevelOuter[4];
} u_tcsPushConstants;
layout(vertices = $vertices_per_patch) out;
void main()
{
  gl_TessLevelInner = u_tcsPushConstants.TessLevelInner;
  gl_TessLevelOuter = u_tcsPushConstants.TessLevelOuter;
  some_var_out = some_var[gl_InvocationID];
}

*/
struct zink_shader *
zink_shader_tcs_create(struct zink_context *ctx, struct zink_shader *vs)
{
   unsigned vertices_per_patch = ctx->gfx_pipeline_state.vertices_per_patch;
   struct zink_shader *ret = CALLOC_STRUCT(zink_shader);
   ret->shader_id = 0; //special value for internal shaders
   ret->programs = _mesa_pointer_set_create(NULL);

   nir_shader *nir = nir_shader_create(NULL, MESA_SHADER_TESS_CTRL, &nir_options, NULL);
   nir_function *fn = nir_function_create(nir, "main");
   fn->is_entrypoint = true;
   nir_function_impl *impl = nir_function_impl_create(fn);

   nir_builder b;
   nir_builder_init(&b, impl);
   b.cursor = nir_before_block(nir_start_block(impl));

   nir_intrinsic_instr *invocation_id = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_invocation_id);
   nir_ssa_dest_init(&invocation_id->instr, &invocation_id->dest, 1, 32, "gl_InvocationID");
   nir_builder_instr_insert(&b, &invocation_id->instr);

   nir_foreach_shader_out_variable(var, vs->nir) {
      const struct glsl_type *type = var->type;
      const struct glsl_type *in_type = var->type;
      const struct glsl_type *out_type = var->type;
      char buf[1024];
      snprintf(buf, sizeof(buf), "%s_out", var->name);
      in_type = glsl_array_type(type, 32 /* MAX_PATCH_VERTICES */, 0);
      out_type = glsl_array_type(type, vertices_per_patch, 0);

      nir_variable *in = nir_variable_create(nir, nir_var_shader_in, in_type, var->name);
      nir_variable *out = nir_variable_create(nir, nir_var_shader_out, out_type, buf);
      out->data.location = in->data.location = var->data.location;
      out->data.location_frac = in->data.location_frac = var->data.location_frac;

      /* gl_in[] receives values from equivalent built-in output
         variables written by the vertex shader (section 2.14.7).  Each array
         element of gl_in[] is a structure holding values for a specific vertex of
         the input patch.  The length of gl_in[] is equal to the
         implementation-dependent maximum patch size (gl_MaxPatchVertices).
         - ARB_tessellation_shader
       */
      for (unsigned i = 0; i < vertices_per_patch; i++) {
         /* we need to load the invocation-specific value of the vertex output and then store it to the per-patch output */
         nir_if *start_block = nir_push_if(&b, nir_ieq(&b, &invocation_id->dest.ssa, nir_imm_int(&b, i)));
         nir_deref_instr *in_array_var = nir_build_deref_array(&b, nir_build_deref_var(&b, in), &invocation_id->dest.ssa);
         nir_ssa_def *load = nir_load_deref(&b, in_array_var);
         nir_deref_instr *out_array_var = nir_build_deref_array_imm(&b, nir_build_deref_var(&b, out), i);
         nir_store_deref(&b, out_array_var, load, 0xff);
         nir_pop_if(&b, start_block);
      }
   }
   nir_variable *gl_TessLevelInner = nir_variable_create(nir, nir_var_shader_out, glsl_array_type(glsl_float_type(), 2, 0), "gl_TessLevelInner");
   gl_TessLevelInner->data.location = VARYING_SLOT_TESS_LEVEL_INNER;
   gl_TessLevelInner->data.patch = 1;
   nir_variable *gl_TessLevelOuter = nir_variable_create(nir, nir_var_shader_out, glsl_array_type(glsl_float_type(), 4, 0), "gl_TessLevelOuter");
   gl_TessLevelOuter->data.location = VARYING_SLOT_TESS_LEVEL_OUTER;
   gl_TessLevelOuter->data.patch = 1;

   /* hacks so we can size these right for now */
   struct glsl_struct_field *fields = ralloc_size(nir, 2 * sizeof(struct glsl_struct_field));
   fields[0].type = glsl_array_type(glsl_uint_type(), 2, 0);
   fields[0].name = ralloc_asprintf(nir, "gl_TessLevelInner");
   fields[0].offset = 0;
   fields[1].type = glsl_array_type(glsl_uint_type(), 4, 0);
   fields[1].name = ralloc_asprintf(nir, "gl_TessLevelOuter");
   fields[1].offset = 8;
   nir_variable *pushconst = nir_variable_create(nir, nir_var_mem_push_const,
                                                 glsl_struct_type(fields, 2, "struct", false), "pushconst");
   pushconst->data.location = VARYING_SLOT_VAR0;

   nir_intrinsic_instr *load_inner = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
   load_inner->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
   nir_intrinsic_set_base(load_inner, 0);
   nir_intrinsic_set_range(load_inner, 8);
   load_inner->num_components = 2;
   nir_ssa_dest_init(&load_inner->instr, &load_inner->dest, 2, 32, "TessLevelInner");
   nir_builder_instr_insert(&b, &load_inner->instr);

   nir_intrinsic_instr *load_outer = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
   load_outer->src[0] = nir_src_for_ssa(nir_imm_int(&b, 8));
   nir_intrinsic_set_base(load_outer, 8);
   nir_intrinsic_set_range(load_outer, 16);
   load_outer->num_components = 4;
   nir_ssa_dest_init(&load_outer->instr, &load_outer->dest, 4, 32, "TessLevelOuter");
   nir_builder_instr_insert(&b, &load_outer->instr);

   for (unsigned i = 0; i < 2; i++) {
      nir_deref_instr *store_idx = nir_build_deref_array_imm(&b, nir_build_deref_var(&b, gl_TessLevelInner), i);
      nir_store_deref(&b, store_idx, nir_channel(&b, &load_inner->dest.ssa, i), 0xff);
   }
   for (unsigned i = 0; i < 4; i++) {
      nir_deref_instr *store_idx = nir_build_deref_array_imm(&b, nir_build_deref_var(&b, gl_TessLevelOuter), i);
      nir_store_deref(&b, store_idx, nir_channel(&b, &load_outer->dest.ssa, i), 0xff);
   }

   nir->info.tess.tcs_vertices_out = vertices_per_patch;
   nir_validate_shader(nir, "created");

   NIR_PASS_V(nir, nir_lower_regs_to_ssa);
   optimize_nir(nir);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
   NIR_PASS_V(nir, lower_discard_if);
   NIR_PASS_V(nir, nir_convert_from_ssa, true);

   ret->nir = nir;
   ret->is_generated = true;
   return ret;
}
