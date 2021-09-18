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

static void
create_vs_pushconst(nir_shader *nir)
{
   nir_variable *vs_pushconst;
   /* create compatible layout for the ntv push constant loader */
   struct glsl_struct_field *fields = rzalloc_array(nir, struct glsl_struct_field, 2);
   fields[0].type = glsl_array_type(glsl_uint_type(), 1, 0);
   fields[0].name = ralloc_asprintf(nir, "draw_mode_is_indexed");
   fields[0].offset = offsetof(struct zink_gfx_push_constant, draw_mode_is_indexed);
   fields[1].type = glsl_array_type(glsl_uint_type(), 1, 0);
   fields[1].name = ralloc_asprintf(nir, "draw_id");
   fields[1].offset = offsetof(struct zink_gfx_push_constant, draw_id);
   vs_pushconst = nir_variable_create(nir, nir_var_mem_push_const,
                                                 glsl_struct_type(fields, 2, "struct", false), "vs_pushconst");
   vs_pushconst->data.location = INT_MAX; //doesn't really matter
}

static void
create_cs_pushconst(nir_shader *nir)
{
   nir_variable *cs_pushconst;
   /* create compatible layout for the ntv push constant loader */
   struct glsl_struct_field *fields = rzalloc_size(nir, 1 * sizeof(struct glsl_struct_field));
   fields[0].type = glsl_array_type(glsl_uint_type(), 1, 0);
   fields[0].name = ralloc_asprintf(nir, "work_dim");
   fields[0].offset = 0;
   cs_pushconst = nir_variable_create(nir, nir_var_mem_push_const,
                                                 glsl_struct_type(fields, 1, "struct", false), "cs_pushconst");
   cs_pushconst->data.location = INT_MAX; //doesn't really matter
}

static bool
reads_work_dim(nir_shader *shader)
{
   return BITSET_TEST(shader->info.system_values_read, SYSTEM_VALUE_WORK_DIM);
}

static bool
lower_discard_if_instr(nir_builder *b, nir_instr *instr_, UNUSED void *cb_data)
{
   if (instr_->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *instr = nir_instr_as_intrinsic(instr_);

   if (instr->intrinsic == nir_intrinsic_discard_if) {
      b->cursor = nir_before_instr(&instr->instr);

      nir_if *if_stmt = nir_push_if(b, nir_ssa_for_src(b, instr->src[0], 1));
      nir_discard(b);
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
   return nir_shader_instructions_pass(shader,
                                       lower_discard_if_instr,
                                       nir_metadata_dominance,
                                       NULL);
}

static bool
lower_work_dim_instr(nir_builder *b, nir_instr *in, void *data)
{
   if (in->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *instr = nir_instr_as_intrinsic(in);
   if (instr->intrinsic != nir_intrinsic_load_work_dim)
      return false;

   if (instr->intrinsic == nir_intrinsic_load_work_dim) {
      b->cursor = nir_after_instr(&instr->instr);
      nir_intrinsic_instr *load = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_push_constant);
      load->src[0] = nir_src_for_ssa(nir_imm_int(b, 0));
      nir_intrinsic_set_range(load, 3 * sizeof(uint32_t));
      load->num_components = 1;
      nir_ssa_dest_init(&load->instr, &load->dest, 1, 32, "work_dim");
      nir_builder_instr_insert(b, &load->instr);

      nir_ssa_def_rewrite_uses(&instr->dest.ssa, &load->dest.ssa);
   }

   return true;
}

static bool
lower_work_dim(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_KERNEL)
      return false;

   if (!reads_work_dim(shader))
      return false;

   return nir_shader_instructions_pass(shader, lower_work_dim_instr, nir_metadata_dominance, NULL);
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
      nir_ssa_def_rewrite_uses_after(&intr->dest.ssa, new_vec,
                                     new_vec->parent_instr);
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

static bool
lower_basevertex_instr(nir_builder *b, nir_instr *in, void *data)
{
   if (in->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *instr = nir_instr_as_intrinsic(in);
   if (instr->intrinsic != nir_intrinsic_load_base_vertex)
      return false;

   b->cursor = nir_after_instr(&instr->instr);
   nir_intrinsic_instr *load = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_push_constant);
   load->src[0] = nir_src_for_ssa(nir_imm_int(b, 0));
   nir_intrinsic_set_range(load, 4);
   load->num_components = 1;
   nir_ssa_dest_init(&load->instr, &load->dest, 1, 32, "draw_mode_is_indexed");
   nir_builder_instr_insert(b, &load->instr);

   nir_ssa_def *composite = nir_build_alu(b, nir_op_bcsel,
                                          nir_build_alu(b, nir_op_ieq, &load->dest.ssa, nir_imm_int(b, 1), NULL, NULL),
                                          &instr->dest.ssa,
                                          nir_imm_int(b, 0),
                                          NULL);

   nir_ssa_def_rewrite_uses_after(&instr->dest.ssa, composite,
                                  composite->parent_instr);
   return true;
}

static bool
lower_basevertex(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_VERTEX)
      return false;

   if (!BITSET_TEST(shader->info.system_values_read, SYSTEM_VALUE_BASE_VERTEX))
      return false;

   return nir_shader_instructions_pass(shader, lower_basevertex_instr, nir_metadata_dominance, NULL);
}


static bool
lower_drawid_instr(nir_builder *b, nir_instr *in, void *data)
{
   if (in->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *instr = nir_instr_as_intrinsic(in);
   if (instr->intrinsic != nir_intrinsic_load_draw_id)
      return false;

   b->cursor = nir_before_instr(&instr->instr);
   nir_intrinsic_instr *load = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_push_constant);
   load->src[0] = nir_src_for_ssa(nir_imm_int(b, 1));
   nir_intrinsic_set_range(load, 4);
   load->num_components = 1;
   nir_ssa_dest_init(&load->instr, &load->dest, 1, 32, "draw_id");
   nir_builder_instr_insert(b, &load->instr);

   nir_ssa_def_rewrite_uses(&instr->dest.ssa, &load->dest.ssa);

   return true;
}

static bool
lower_drawid(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_VERTEX)
      return false;

   if (!BITSET_TEST(shader->info.system_values_read, SYSTEM_VALUE_DRAW_ID))
      return false;

   return nir_shader_instructions_pass(shader, lower_drawid_instr, nir_metadata_dominance, NULL);
}

static bool
lower_dual_blend(nir_shader *shader)
{
   bool progress = false;
   nir_variable *var = nir_find_variable_with_location(shader, nir_var_shader_out, FRAG_RESULT_DATA1);
   if (var) {
      var->data.location = FRAG_RESULT_DATA0;
      var->data.index = 1;
      progress = true;
   }
   nir_shader_preserve_all_metadata(shader);
   return progress;
}

void
zink_screen_init_compiler(struct zink_screen *screen)
{
   static const struct nir_shader_compiler_options
   default_options = {
      .lower_ffma16 = true,
      .lower_ffma32 = true,
      .lower_ffma64 = true,
      .lower_scmp = true,
      .lower_fdph = true,
      .lower_flrp32 = true,
      .lower_fpow = true,
      .lower_fsat = true,
      .lower_extract_byte = true,
      .lower_extract_word = true,
      .lower_insert_byte = true,
      .lower_insert_word = true,
      .lower_mul_high = true,
      .lower_rotate = true,
      .lower_uadd_carry = true,
      .lower_pack_64_2x32_split = true,
      .lower_unpack_64_2x32_split = true,
      .lower_pack_32_2x16_split = true,
      .lower_unpack_32_2x16_split = true,
      .lower_vector_cmp = true,
      .lower_int64_options = 0,
      .lower_doubles_options = ~nir_lower_fp64_full_software,
      .lower_uniforms_to_ubo = true,
      .has_fsub = true,
      .has_isub = true,
      .lower_mul_2x32_64 = true,
      .support_16bit_alu = true, /* not quite what it sounds like */
   };

   screen->nir_options = default_options;

   if (!screen->info.feats.features.shaderInt64)
      screen->nir_options.lower_int64_options = ~0;

   if (!screen->info.feats.features.shaderFloat64) {
      screen->nir_options.lower_doubles_options = ~0;
      screen->nir_options.lower_flrp64 = true;
      screen->nir_options.lower_ffma64 = true;
   }
}

const void *
zink_get_compiler_options(struct pipe_screen *pscreen,
                          enum pipe_shader_ir ir,
                          enum pipe_shader_type shader)
{
   assert(ir == PIPE_SHADER_IR_NIR);
   return &zink_screen(pscreen)->nir_options;
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

   do {
      progress = false;
      NIR_PASS(progress, s, nir_opt_algebraic_late);
      if (progress) {
         NIR_PASS_V(s, nir_copy_prop);
         NIR_PASS_V(s, nir_opt_dce);
         NIR_PASS_V(s, nir_opt_cse);
      }
   } while (progress);
}

/* - copy the lowered fbfetch variable
 * - set the new one up as an input attachment for descriptor 0.6
 * - load it as an image
 * - overwrite the previous load
 */
static bool
lower_fbfetch_instr(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_deref)
      return false;
   nir_variable *var = nir_deref_instr_get_variable(nir_src_as_deref(intr->src[0]));
   if (var != data)
      return false;
   b->cursor = nir_after_instr(instr);
   nir_variable *fbfetch = nir_variable_clone(data, b->shader);
   /* If Dim is SubpassData, ... Image Format must be Unknown
    * - SPIRV OpTypeImage specification
    */
   fbfetch->data.image.format = 0;
   fbfetch->data.index = 0; /* fix this if more than 1 fbfetch target is supported */
   fbfetch->data.mode = nir_var_uniform;
   fbfetch->data.binding = ZINK_FBFETCH_BINDING;
   fbfetch->type = glsl_image_type(GLSL_SAMPLER_DIM_SUBPASS, false, GLSL_TYPE_FLOAT);
   nir_shader_add_variable(b->shader, fbfetch);
   nir_ssa_def *deref = &nir_build_deref_var(b, fbfetch)->dest.ssa;
   nir_ssa_def *load = nir_image_deref_load(b, 4, 32, deref, nir_imm_vec4(b, 0, 0, 0, 1), nir_ssa_undef(b, 1, 32), nir_imm_int(b, 0));
   unsigned swiz[4] = {2, 1, 0, 3};
   nir_ssa_def *swizzle = nir_swizzle(b, load, swiz, 4);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, swizzle);
   return true;
}

static bool
lower_fbfetch(nir_shader *shader, nir_variable **fbfetch)
{
   nir_foreach_shader_out_variable(var, shader) {
      if (var->data.fb_fetch_output) {
         *fbfetch = var;
         break;
      }
   }
   assert(*fbfetch);
   if (!*fbfetch)
      return false;
   return nir_shader_instructions_pass(shader, lower_fbfetch_instr, nir_metadata_dominance, *fbfetch);
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

static void
update_so_info(struct zink_shader *zs, const struct pipe_stream_output_info *so_info,
               uint64_t outputs_written, bool have_psiz)
{
   uint8_t reverse_map[64] = {0};
   unsigned slot = 0;
   /* semi-copied from iris */
   while (outputs_written) {
      int bit = u_bit_scan64(&outputs_written);
      /* PSIZ from nir_lower_point_size_mov breaks stream output, so always skip it */
      if (bit == VARYING_SLOT_PSIZ && !have_psiz)
         continue;
      reverse_map[slot++] = bit;
   }

   nir_foreach_shader_out_variable(var, zs->nir)
      var->data.explicit_xfb_buffer = 0;

   bool inlined[64] = {0};
   for (unsigned i = 0; i < so_info->num_outputs; i++) {
      const struct pipe_stream_output *output = &so_info->output[i];
      unsigned slot = reverse_map[output->register_index];
      /* always set stride to be used during draw */
      zs->streamout.so_info.stride[output->output_buffer] = so_info->stride[output->output_buffer];
      if ((zs->nir->info.stage != MESA_SHADER_GEOMETRY || util_bitcount(zs->nir->info.gs.active_stream_mask) == 1) &&
          !output->start_component) {
         nir_variable *var = NULL;
         while (!var)
            var = nir_find_variable_with_location(zs->nir, nir_var_shader_out, slot--);
         slot++;
         if (inlined[slot])
            continue;
         assert(var && var->data.location == slot);
         /* if this is the entire variable, try to blast it out during the initial declaration */
         if (glsl_get_components(var->type) == output->num_components) {
            var->data.explicit_xfb_buffer = 1;
            var->data.xfb.buffer = output->output_buffer;
            var->data.xfb.stride = so_info->stride[output->output_buffer] * 4;
            var->data.offset = output->dst_offset * 4;
            var->data.stream = output->stream;
            inlined[slot] = true;
            continue;
         }
      }
      zs->streamout.so_info.output[zs->streamout.so_info.num_outputs] = *output;
      /* Map Gallium's condensed "slots" back to real VARYING_SLOT_* enums */
      zs->streamout.so_info_slots[zs->streamout.so_info.num_outputs++] = reverse_map[output->register_index];
   }
   zs->streamout.have_xfb = !!zs->streamout.so_info.num_outputs;
}

struct decompose_state {
  nir_variable **split;
  bool needs_w;
};

static bool
lower_attrib(nir_builder *b, nir_instr *instr, void *data)
{
   struct decompose_state *state = data;
   nir_variable **split = state->split;
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_deref)
      return false;
   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   if (var != split[0])
      return false;
   unsigned num_components = glsl_get_vector_elements(split[0]->type);
   b->cursor = nir_after_instr(instr);
   nir_ssa_def *loads[4];
   for (unsigned i = 0; i < (state->needs_w ? num_components - 1 : num_components); i++)
      loads[i] = nir_load_deref(b, nir_build_deref_var(b, split[i+1]));
   if (state->needs_w) {
      /* oob load w comopnent to get correct value for int/float */
      loads[3] = nir_channel(b, loads[0], 3);
      loads[0] = nir_channel(b, loads[0], 0);
   }
   nir_ssa_def *new_load = nir_vec(b, loads, num_components);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, new_load);
   nir_instr_remove_v(instr);
   return true;
}

static bool
decompose_attribs(nir_shader *nir, uint32_t decomposed_attrs, uint32_t decomposed_attrs_without_w)
{
   uint32_t bits = 0;
   nir_foreach_variable_with_modes(var, nir, nir_var_shader_in)
      bits |= BITFIELD_BIT(var->data.driver_location);
   bits = ~bits;
   u_foreach_bit(location, decomposed_attrs | decomposed_attrs_without_w) {
      nir_variable *split[5];
      struct decompose_state state;
      state.split = split;
      nir_variable *var = nir_find_variable_with_driver_location(nir, nir_var_shader_in, location);
      assert(var);
      split[0] = var;
      bits |= BITFIELD_BIT(var->data.driver_location);
      const struct glsl_type *new_type = glsl_type_is_scalar(var->type) ? var->type : glsl_get_array_element(var->type);
      unsigned num_components = glsl_get_vector_elements(var->type);
      state.needs_w = (decomposed_attrs_without_w & BITFIELD_BIT(location)) != 0 && num_components == 4;
      for (unsigned i = 0; i < (state.needs_w ? num_components - 1 : num_components); i++) {
         split[i+1] = nir_variable_clone(var, nir);
         split[i+1]->name = ralloc_asprintf(nir, "%s_split%u", var->name, i);
         if (decomposed_attrs_without_w & BITFIELD_BIT(location))
            split[i+1]->type = !i && num_components == 4 ? var->type : new_type;
         else
            split[i+1]->type = new_type;
         split[i+1]->data.driver_location = ffs(bits) - 1;
         bits &= ~BITFIELD_BIT(split[i+1]->data.driver_location);
         nir_shader_add_variable(nir, split[i+1]);
      }
      var->data.mode = nir_var_shader_temp;
      nir_shader_instructions_pass(nir, lower_attrib, nir_metadata_dominance, &state);
   }
   nir_fixup_deref_modes(nir);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_shader_temp, NULL);
   optimize_nir(nir);
   return true;
}

static void
assign_producer_var_io(gl_shader_stage stage, nir_variable *var, unsigned *reserved, unsigned char *slot_map)
{
   unsigned slot = var->data.location;
   switch (var->data.location) {
   case VARYING_SLOT_POS:
   case VARYING_SLOT_PNTC:
   case VARYING_SLOT_PSIZ:
   case VARYING_SLOT_LAYER:
   case VARYING_SLOT_PRIMITIVE_ID:
   case VARYING_SLOT_CLIP_DIST0:
   case VARYING_SLOT_CULL_DIST0:
   case VARYING_SLOT_VIEWPORT:
   case VARYING_SLOT_FACE:
   case VARYING_SLOT_TESS_LEVEL_OUTER:
   case VARYING_SLOT_TESS_LEVEL_INNER:
      /* use a sentinel value to avoid counting later */
      var->data.driver_location = UINT_MAX;
      break;

   default:
      if (var->data.patch) {
         assert(var->data.location >= VARYING_SLOT_PATCH0);
         slot = var->data.location - VARYING_SLOT_PATCH0;
      } else if (var->data.location >= VARYING_SLOT_VAR0 &&
                 var->data.mode == nir_var_shader_in &&
                  stage == MESA_SHADER_TESS_EVAL) {
         slot = var->data.location - VARYING_SLOT_VAR0;
      } else {
         if (slot_map[var->data.location] == 0xff) {
            assert(*reserved < MAX_VARYING);
            slot_map[var->data.location] = *reserved;
            *reserved += glsl_count_vec4_slots(var->type, false, false);
         }
         slot = slot_map[var->data.location];
         assert(slot < MAX_VARYING);
      }
      var->data.driver_location = slot;
   }
}

ALWAYS_INLINE static bool
is_texcoord(gl_shader_stage stage, const nir_variable *var)
{
   if (stage != MESA_SHADER_FRAGMENT)
      return false;
   return var->data.location >= VARYING_SLOT_TEX0 && 
          var->data.location <= VARYING_SLOT_TEX7;
}

static bool
assign_consumer_var_io(gl_shader_stage stage, nir_variable *var, unsigned *reserved, unsigned char *slot_map)
{
   switch (var->data.location) {
   case VARYING_SLOT_POS:
   case VARYING_SLOT_PNTC:
   case VARYING_SLOT_PSIZ:
   case VARYING_SLOT_LAYER:
   case VARYING_SLOT_PRIMITIVE_ID:
   case VARYING_SLOT_CLIP_DIST0:
   case VARYING_SLOT_CULL_DIST0:
   case VARYING_SLOT_VIEWPORT:
   case VARYING_SLOT_FACE:
   case VARYING_SLOT_TESS_LEVEL_OUTER:
   case VARYING_SLOT_TESS_LEVEL_INNER:
      /* use a sentinel value to avoid counting later */
      var->data.driver_location = UINT_MAX;
      break;
   default:
      if (var->data.patch) {
         assert(var->data.location >= VARYING_SLOT_PATCH0);
         var->data.driver_location = var->data.location - VARYING_SLOT_PATCH0;
      } else if (var->data.location >= VARYING_SLOT_VAR0 &&
          stage == MESA_SHADER_TESS_CTRL &&
          var->data.mode == nir_var_shader_out)
         var->data.driver_location = var->data.location - VARYING_SLOT_VAR0;
      else {
         if (slot_map[var->data.location] == (unsigned char)-1) {
            if (!is_texcoord(stage, var))
               /* dead io */
               return false;
            /* texcoords can't be eliminated in fs due to GL_COORD_REPLACE */
            slot_map[var->data.location] = (*reserved)++;
         }
         var->data.driver_location = slot_map[var->data.location];
      }
   }
   return true;
}


static bool
rewrite_and_discard_read(nir_builder *b, nir_instr *instr, void *data)
{
   nir_variable *var = data;
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_deref)
      return false;
   nir_variable *deref_var = nir_intrinsic_get_var(intr, 0);
   if (deref_var != var)
      return false;
   nir_ssa_def *undef = nir_ssa_undef(b, nir_dest_num_components(intr->dest), nir_dest_bit_size(intr->dest));
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, undef);
   return true;
}

void
zink_compiler_assign_io(nir_shader *producer, nir_shader *consumer)
{
   unsigned reserved = 0;
   unsigned char slot_map[VARYING_SLOT_MAX];
   memset(slot_map, -1, sizeof(slot_map));
   bool do_fixup = false;
   nir_shader *nir = producer->info.stage == MESA_SHADER_TESS_CTRL ? producer : consumer;
   if (producer->info.stage == MESA_SHADER_TESS_CTRL) {
      /* never assign from tcs -> tes, always invert */
      nir_foreach_variable_with_modes(var, consumer, nir_var_shader_in)
         assign_producer_var_io(consumer->info.stage, var, &reserved, slot_map);
      nir_foreach_variable_with_modes_safe(var, producer, nir_var_shader_out) {
         if (!assign_consumer_var_io(producer->info.stage, var, &reserved, slot_map))
            /* this is an output, nothing more needs to be done for it to be dropped */
            do_fixup = true;
      }
   } else {
      nir_foreach_variable_with_modes(var, producer, nir_var_shader_out)
         assign_producer_var_io(producer->info.stage, var, &reserved, slot_map);
      nir_foreach_variable_with_modes_safe(var, consumer, nir_var_shader_in) {
         if (!assign_consumer_var_io(consumer->info.stage, var, &reserved, slot_map)) {
            do_fixup = true;
            /* input needs to be rewritten as an undef to ensure the entire deref chain is deleted */
            nir_shader_instructions_pass(consumer, rewrite_and_discard_read, nir_metadata_dominance, var);
         }
      }
   }
   if (!do_fixup)
      return;
   nir_fixup_deref_modes(nir);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_shader_temp, NULL);
   optimize_nir(nir);
}

VkShaderModule
zink_shader_compile(struct zink_screen *screen, struct zink_shader *zs, nir_shader *base_nir, const struct zink_shader_key *key)
{
   VkShaderModule mod = VK_NULL_HANDLE;
   void *streamout = NULL;
   nir_shader *nir = nir_shader_clone(NULL, base_nir);

   if (key) {
      if (key->inline_uniforms) {
         NIR_PASS_V(nir, nir_inline_uniforms,
                    nir->info.num_inlinable_uniforms,
                    key->base.inlined_uniform_values,
                    nir->info.inlinable_uniform_dw_offsets);

         optimize_nir(nir);

         /* This must be done again. */
         NIR_PASS_V(nir, nir_io_add_const_offset_to_base, nir_var_shader_in |
                                                          nir_var_shader_out);
      }

      /* TODO: use a separate mem ctx here for ralloc */
      switch (zs->nir->info.stage) {
      case MESA_SHADER_VERTEX: {
         uint32_t decomposed_attrs = 0, decomposed_attrs_without_w = 0;
         const struct zink_vs_key *vs_key = zink_vs_key(key);
         switch (vs_key->size) {
         case 4:
            decomposed_attrs = vs_key->u32.decomposed_attrs;
            decomposed_attrs_without_w = vs_key->u32.decomposed_attrs_without_w;
            break;
         case 2:
            decomposed_attrs = vs_key->u16.decomposed_attrs;
            decomposed_attrs_without_w = vs_key->u16.decomposed_attrs_without_w;
            break;
         case 1:
            decomposed_attrs = vs_key->u8.decomposed_attrs;
            decomposed_attrs_without_w = vs_key->u8.decomposed_attrs_without_w;
            break;
         default: break;
         }
         if (decomposed_attrs || decomposed_attrs_without_w)
            NIR_PASS_V(nir, decompose_attribs, decomposed_attrs, decomposed_attrs_without_w);
         FALLTHROUGH;
      }
      case MESA_SHADER_TESS_EVAL:
      case MESA_SHADER_GEOMETRY:
         if (zink_vs_key_base(key)->last_vertex_stage) {
            if (zs->streamout.have_xfb)
               streamout = &zs->streamout;

            if (!zink_vs_key_base(key)->clip_halfz) {
               NIR_PASS_V(nir, nir_lower_clip_halfz);
            }
            if (zink_vs_key_base(key)->push_drawid) {
               NIR_PASS_V(nir, lower_drawid);
            }
         }
         break;
      case MESA_SHADER_FRAGMENT:
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
         if (zink_fs_key(key)->force_dual_color_blend && nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DATA1)) {
            NIR_PASS_V(nir, lower_dual_blend);
         }
         if (zink_fs_key(key)->coord_replace_bits) {
            NIR_PASS_V(nir, nir_lower_texcoord_replace, zink_fs_key(key)->coord_replace_bits,
                     false, zink_fs_key(key)->coord_replace_yinvert);
         }
         if (nir->info.fs.uses_fbfetch_output) {
            nir_variable *fbfetch = NULL;
            NIR_PASS_V(nir, lower_fbfetch, &fbfetch);
            /* old variable must be deleted to avoid spirv errors */
            fbfetch->data.mode = nir_var_shader_temp;
            nir_fixup_deref_modes(nir);
            NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_shader_temp, NULL);
            optimize_nir(nir);
         }
         break;
      default: break;
      }
   }
   NIR_PASS_V(nir, nir_convert_from_ssa, true);

   struct spirv_shader *spirv = nir_to_spirv(nir, streamout, screen->spirv_version);
   if (!spirv)
      goto done;

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

   VkShaderModuleCreateInfo smci = {0};
   smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   smci.codeSize = spirv->num_words * sizeof(uint32_t);
   smci.pCode = spirv->words;

   if (VKSCR(CreateShaderModule)(screen->dev, &smci, NULL, &mod) != VK_SUCCESS)
      mod = VK_NULL_HANDLE;

done:
   ralloc_free(nir);

   /* TODO: determine if there's any reason to cache spirv output? */
   ralloc_free(spirv);
   return mod;
}

static bool
lower_baseinstance_instr(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_instance_id)
      return false;
   b->cursor = nir_after_instr(instr);
   nir_ssa_def *def = nir_isub(b, &intr->dest.ssa, nir_load_base_instance(b));
   nir_ssa_def_rewrite_uses_after(&intr->dest.ssa, def, def->parent_instr);
   return true;
}

static bool
lower_baseinstance(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_VERTEX)
      return false;
   return nir_shader_instructions_pass(shader, lower_baseinstance_instr, nir_metadata_dominance, NULL);
}

bool nir_lower_dynamic_bo_access(nir_shader *shader);

/* gl_nir_lower_buffers makes variables unusable for all UBO/SSBO access
 * so instead we delete all those broken variables and just make new ones
 */
static bool
unbreak_bos(nir_shader *shader)
{
   uint32_t ssbo_used = 0;
   uint32_t ubo_used = 0;
   uint64_t max_ssbo_size = 0;
   uint64_t max_ubo_size = 0;
   bool ssbo_sizes[PIPE_MAX_SHADER_BUFFERS] = {false};

   if (!shader->info.num_ssbos && !shader->info.num_ubos && !shader->num_uniforms)
      return false;
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         switch (intrin->intrinsic) {
         case nir_intrinsic_store_ssbo:
            ssbo_used |= BITFIELD_BIT(nir_src_as_uint(intrin->src[1]));
            break;

         case nir_intrinsic_get_ssbo_size: {
            uint32_t slot = nir_src_as_uint(intrin->src[0]);
            ssbo_used |= BITFIELD_BIT(slot);
            ssbo_sizes[slot] = true;
            break;
         }
         case nir_intrinsic_ssbo_atomic_add:
         case nir_intrinsic_ssbo_atomic_imin:
         case nir_intrinsic_ssbo_atomic_umin:
         case nir_intrinsic_ssbo_atomic_imax:
         case nir_intrinsic_ssbo_atomic_umax:
         case nir_intrinsic_ssbo_atomic_and:
         case nir_intrinsic_ssbo_atomic_or:
         case nir_intrinsic_ssbo_atomic_xor:
         case nir_intrinsic_ssbo_atomic_exchange:
         case nir_intrinsic_ssbo_atomic_comp_swap:
         case nir_intrinsic_ssbo_atomic_fmin:
         case nir_intrinsic_ssbo_atomic_fmax:
         case nir_intrinsic_ssbo_atomic_fcomp_swap:
         case nir_intrinsic_load_ssbo:
            ssbo_used |= BITFIELD_BIT(nir_src_as_uint(intrin->src[0]));
            break;
         case nir_intrinsic_load_ubo:
         case nir_intrinsic_load_ubo_vec4:
            ubo_used |= BITFIELD_BIT(nir_src_as_uint(intrin->src[0]));
            break;
         default:
            break;
         }
      }
   }

   nir_foreach_variable_with_modes(var, shader, nir_var_mem_ssbo | nir_var_mem_ubo) {
      const struct glsl_type *type = glsl_without_array(var->type);
      if (type_is_counter(type))
         continue;
      unsigned size = glsl_count_attribute_slots(glsl_type_is_array(var->type) ? var->type : type, false);
      if (var->data.mode == nir_var_mem_ubo)
         max_ubo_size = MAX2(max_ubo_size, size);
      else
         max_ssbo_size = MAX2(max_ssbo_size, size);
      var->data.mode = nir_var_shader_temp;
   }
   nir_fixup_deref_modes(shader);
   NIR_PASS_V(shader, nir_remove_dead_variables, nir_var_shader_temp, NULL);
   optimize_nir(shader);

   if (!ssbo_used && !ubo_used)
      return false;

   struct glsl_struct_field *fields = rzalloc_array(shader, struct glsl_struct_field, 2);
   fields[0].name = ralloc_strdup(shader, "base");
   fields[1].name = ralloc_strdup(shader, "unsized");
   if (ubo_used) {
      const struct glsl_type *ubo_type = glsl_array_type(glsl_uint_type(), max_ubo_size * 4, 4);
      fields[0].type = ubo_type;
      u_foreach_bit(slot, ubo_used) {
         char buf[64];
         snprintf(buf, sizeof(buf), "ubo_slot_%u", slot);
         nir_variable *var = nir_variable_create(shader, nir_var_mem_ubo, glsl_struct_type(fields, 1, "struct", false), buf);
         var->interface_type = var->type;
         var->data.driver_location = slot;
      }
   }
   if (ssbo_used) {
      const struct glsl_type *ssbo_type = glsl_array_type(glsl_uint_type(), max_ssbo_size * 4, 4);
      const struct glsl_type *unsized = glsl_array_type(glsl_uint_type(), 0, 4);
      fields[0].type = ssbo_type;
      u_foreach_bit(slot, ssbo_used) {
         char buf[64];
         snprintf(buf, sizeof(buf), "ssbo_slot_%u", slot);
         if (ssbo_sizes[slot])
            fields[1].type = unsized;
         else
            fields[1].type = NULL;
         nir_variable *var = nir_variable_create(shader, nir_var_mem_ssbo,
                                                 glsl_struct_type(fields, 1 + !!ssbo_sizes[slot], "struct", false), buf);
         var->interface_type = var->type;
         var->data.driver_location = slot;
      }
   }
   return true;
}

static uint32_t
zink_binding(gl_shader_stage stage, VkDescriptorType type, int index)
{
   if (stage == MESA_SHADER_NONE) {
      unreachable("not supported");
   } else {
      switch (type) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         assert(index < PIPE_MAX_CONSTANT_BUFFERS);
         return (stage * PIPE_MAX_CONSTANT_BUFFERS) + index;

      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         assert(index < PIPE_MAX_SAMPLERS);
         return (stage * PIPE_MAX_SAMPLERS) + index;

      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         assert(index < PIPE_MAX_SHADER_BUFFERS);
         return (stage * PIPE_MAX_SHADER_BUFFERS) + index;

      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         assert(index < PIPE_MAX_SHADER_IMAGES);
         return (stage * PIPE_MAX_SHADER_IMAGES) + index;

      default:
         unreachable("unexpected type");
      }
   }
}

struct zink_shader *
zink_shader_create(struct zink_screen *screen, struct nir_shader *nir,
                   const struct pipe_stream_output_info *so_info)
{
   struct zink_shader *ret = CALLOC_STRUCT(zink_shader);
   bool have_psiz = false;

   ret->hash = _mesa_hash_pointer(ret);

   ret->programs = _mesa_pointer_set_create(NULL);
   simple_mtx_init(&ret->lock, mtx_plain);

   nir_variable_mode indirect_derefs_modes = nir_var_function_temp;
   if (nir->info.stage == MESA_SHADER_TESS_CTRL ||
       nir->info.stage == MESA_SHADER_TESS_EVAL)
      indirect_derefs_modes |= nir_var_shader_in | nir_var_shader_out;

   NIR_PASS_V(nir, nir_lower_indirect_derefs, indirect_derefs_modes,
              UINT32_MAX);

   if (nir->info.stage == MESA_SHADER_VERTEX)
      create_vs_pushconst(nir);
   else if (nir->info.stage == MESA_SHADER_TESS_CTRL ||
            nir->info.stage == MESA_SHADER_TESS_EVAL)
      NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);
   else if (nir->info.stage == MESA_SHADER_KERNEL)
      create_cs_pushconst(nir);

   if (nir->info.stage < MESA_SHADER_FRAGMENT)
      have_psiz = check_psiz(nir);
   NIR_PASS_V(nir, lower_basevertex);
   NIR_PASS_V(nir, lower_work_dim);
   NIR_PASS_V(nir, nir_lower_regs_to_ssa);
   NIR_PASS_V(nir, lower_baseinstance);

   {
      nir_lower_subgroups_options subgroup_options = {0};
      subgroup_options.lower_to_scalar = true;
      subgroup_options.subgroup_size = screen->info.props11.subgroupSize;
      subgroup_options.ballot_bit_size = 32;
      subgroup_options.ballot_components = 4;
      subgroup_options.lower_subgroup_masks = true;
      NIR_PASS_V(nir, nir_lower_subgroups, &subgroup_options);
   }

   optimize_nir(nir);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
   NIR_PASS_V(nir, lower_discard_if);
   NIR_PASS_V(nir, nir_lower_fragcolor,
         nir->info.fs.color_is_dual_source ? 1 : 8);
   NIR_PASS_V(nir, lower_64bit_vertex_attribs);
   NIR_PASS_V(nir, unbreak_bos);

   if (zink_debug & ZINK_DEBUG_NIR) {
      fprintf(stderr, "NIR shader:\n---8<---\n");
      nir_print_shader(nir, stderr);
      fprintf(stderr, "---8<---\n");
   }

   foreach_list_typed_reverse(nir_variable, var, node, &nir->variables) {
      if (_nir_shader_variable_has_mode(var, nir_var_uniform |
                                        nir_var_mem_ubo |
                                        nir_var_mem_ssbo)) {
         enum zink_descriptor_type ztype;
         const struct glsl_type *type = glsl_without_array(var->type);
         if (var->data.mode == nir_var_mem_ubo) {
            ztype = ZINK_DESCRIPTOR_TYPE_UBO;
            /* buffer 0 is a push descriptor */
            var->data.descriptor_set = !!var->data.driver_location;
            var->data.binding = !var->data.driver_location ? nir->info.stage :
                                zink_binding(nir->info.stage,
                                             VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                             var->data.driver_location);
            assert(var->data.driver_location || var->data.binding < 10);
            VkDescriptorType vktype = !var->data.driver_location ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            int binding = var->data.binding;

            ret->bindings[ztype][ret->num_bindings[ztype]].index = var->data.driver_location;
            ret->bindings[ztype][ret->num_bindings[ztype]].binding = binding;
            ret->bindings[ztype][ret->num_bindings[ztype]].type = vktype;
            ret->bindings[ztype][ret->num_bindings[ztype]].size = 1;
            ret->ubos_used |= (1 << ret->bindings[ztype][ret->num_bindings[ztype]].index);
            ret->num_bindings[ztype]++;
         } else if (var->data.mode == nir_var_mem_ssbo) {
            ztype = ZINK_DESCRIPTOR_TYPE_SSBO;
            var->data.descriptor_set = ztype + 1;
            var->data.binding = zink_binding(nir->info.stage,
                                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                             var->data.driver_location);
            ret->bindings[ztype][ret->num_bindings[ztype]].index = var->data.driver_location;
            ret->ssbos_used |= (1 << ret->bindings[ztype][ret->num_bindings[ztype]].index);
            ret->bindings[ztype][ret->num_bindings[ztype]].binding = var->data.binding;
            ret->bindings[ztype][ret->num_bindings[ztype]].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            ret->bindings[ztype][ret->num_bindings[ztype]].size = 1;
            ret->num_bindings[ztype]++;
         } else {
            assert(var->data.mode == nir_var_uniform);
            if (glsl_type_is_sampler(type) || glsl_type_is_image(type)) {
               VkDescriptorType vktype = glsl_type_is_image(type) ? zink_image_type(type) : zink_sampler_type(type);
               if (vktype == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER)
                  ret->num_texel_buffers++;
               ztype = zink_desc_type_from_vktype(vktype);
               var->data.driver_location = var->data.binding;
               var->data.descriptor_set = ztype + 1;
               var->data.binding = zink_binding(nir->info.stage, vktype, var->data.driver_location);
               ret->bindings[ztype][ret->num_bindings[ztype]].index = var->data.driver_location;
               ret->bindings[ztype][ret->num_bindings[ztype]].binding = var->data.binding;
               ret->bindings[ztype][ret->num_bindings[ztype]].type = vktype;
               if (glsl_type_is_array(var->type))
                  ret->bindings[ztype][ret->num_bindings[ztype]].size = glsl_get_aoa_size(var->type);
               else
                  ret->bindings[ztype][ret->num_bindings[ztype]].size = 1;
               ret->num_bindings[ztype]++;
            }
         }
      }
   }

   ret->nir = nir;
   if (so_info && nir->info.outputs_written && nir->info.has_transform_feedback_varyings)
      update_so_info(ret, so_info, nir->info.outputs_written, have_psiz);

   return ret;
}

char *
zink_shader_finalize(struct pipe_screen *pscreen, void *nirptr)
{
   struct zink_screen *screen = zink_screen(pscreen);
   nir_shader *nir = nirptr;

   if (!screen->info.feats.features.shaderImageGatherExtended) {
      nir_lower_tex_options tex_opts = {0};
      tex_opts.lower_tg4_offsets = true;
      NIR_PASS_V(nir, nir_lower_tex, &tex_opts);
   }
   NIR_PASS_V(nir, nir_lower_uniforms_to_ubo, true, false);
   if (nir->info.stage == MESA_SHADER_GEOMETRY)
      NIR_PASS_V(nir, nir_lower_gs_intrinsics, nir_lower_gs_intrinsics_per_stream);
   optimize_nir(nir);
   if (nir->info.num_ubos || nir->info.num_ssbos)
      NIR_PASS_V(nir, nir_lower_dynamic_bo_access);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   if (screen->driconf.inline_uniforms)
      nir_find_inlinable_uniforms(nir);

   return NULL;
}

void
zink_shader_free(struct zink_context *ctx, struct zink_shader *shader)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   set_foreach(shader->programs, entry) {
      if (shader->nir->info.stage == MESA_SHADER_COMPUTE) {
         struct zink_compute_program *comp = (void*)entry->key;
         if (!comp->base.removed) {
            _mesa_hash_table_remove_key(&ctx->compute_program_cache, comp->shader);
            comp->base.removed = true;
         }
         comp->shader = NULL;
         zink_compute_program_reference(screen, &comp, NULL);
      } else {
         struct zink_gfx_program *prog = (void*)entry->key;
         enum pipe_shader_type pstage = pipe_shader_type_from_mesa(shader->nir->info.stage);
         assert(pstage < ZINK_SHADER_COUNT);
         if (!prog->base.removed && (shader->nir->info.stage != MESA_SHADER_TESS_CTRL || !shader->is_generated)) {
            _mesa_hash_table_remove_key(&ctx->program_cache[prog->stages_present >> 2], prog->shaders);
            prog->base.removed = true;
         }
         prog->shaders[pstage] = NULL;
         if (shader->nir->info.stage == MESA_SHADER_TESS_EVAL && shader->generated)
            /* automatically destroy generated tcs shaders when tes is destroyed */
            zink_shader_free(ctx, shader->generated);
         zink_gfx_program_reference(screen, &prog, NULL);
      }
   }
   _mesa_set_destroy(shader->programs, NULL);
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
zink_shader_tcs_create(struct zink_screen *screen, struct zink_shader *vs, unsigned vertices_per_patch)
{
   struct zink_shader *ret = CALLOC_STRUCT(zink_shader);
   ret->hash = _mesa_hash_pointer(ret);
   ret->programs = _mesa_pointer_set_create(NULL);
   simple_mtx_init(&ret->lock, mtx_plain);

   nir_shader *nir = nir_shader_create(NULL, MESA_SHADER_TESS_CTRL, &screen->nir_options, NULL);
   nir_function *fn = nir_function_create(nir, "main");
   fn->is_entrypoint = true;
   nir_function_impl *impl = nir_function_impl_create(fn);

   nir_builder b;
   nir_builder_init(&b, impl);
   b.cursor = nir_before_block(nir_start_block(impl));

   nir_ssa_def *invocation_id = nir_load_invocation_id(&b);

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
         nir_if *start_block = nir_push_if(&b, nir_ieq(&b, invocation_id, nir_imm_int(&b, i)));
         nir_deref_instr *in_array_var = nir_build_deref_array(&b, nir_build_deref_var(&b, in), invocation_id);
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
   struct glsl_struct_field *fields = rzalloc_array(nir, struct glsl_struct_field, 3);
   /* just use a single blob for padding here because it's easier */
   fields[0].type = glsl_array_type(glsl_uint_type(), offsetof(struct zink_gfx_push_constant, default_inner_level) / 4, 0);
   fields[0].name = ralloc_asprintf(nir, "padding");
   fields[0].offset = 0;
   fields[1].type = glsl_array_type(glsl_uint_type(), 2, 0);
   fields[1].name = ralloc_asprintf(nir, "gl_TessLevelInner");
   fields[1].offset = offsetof(struct zink_gfx_push_constant, default_inner_level);
   fields[2].type = glsl_array_type(glsl_uint_type(), 4, 0);
   fields[2].name = ralloc_asprintf(nir, "gl_TessLevelOuter");
   fields[2].offset = offsetof(struct zink_gfx_push_constant, default_outer_level);
   nir_variable *pushconst = nir_variable_create(nir, nir_var_mem_push_const,
                                                 glsl_struct_type(fields, 3, "struct", false), "pushconst");
   pushconst->data.location = VARYING_SLOT_VAR0;

   nir_ssa_def *load_inner = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 1), .base = 1, .range = 8);
   nir_ssa_def *load_outer = nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 2), .base = 2, .range = 16);

   for (unsigned i = 0; i < 2; i++) {
      nir_deref_instr *store_idx = nir_build_deref_array_imm(&b, nir_build_deref_var(&b, gl_TessLevelInner), i);
      nir_store_deref(&b, store_idx, nir_channel(&b, load_inner, i), 0xff);
   }
   for (unsigned i = 0; i < 4; i++) {
      nir_deref_instr *store_idx = nir_build_deref_array_imm(&b, nir_build_deref_var(&b, gl_TessLevelOuter), i);
      nir_store_deref(&b, store_idx, nir_channel(&b, load_outer, i), 0xff);
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
