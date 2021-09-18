/*
 * Copyright © 2015 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "st_nir.h"

#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_context.h"

#include "program/program.h"
#include "program/prog_statevars.h"
#include "program/prog_parameter.h"
#include "program/ir_to_mesa.h"
#include "main/context.h"
#include "main/mtypes.h"
#include "main/errors.h"
#include "main/glspirv.h"
#include "main/shaderapi.h"
#include "main/uniforms.h"

#include "main/shaderobj.h"
#include "st_context.h"
#include "st_program.h"
#include "st_shader_cache.h"

#include "compiler/nir/nir.h"
#include "compiler/glsl_types.h"
#include "compiler/glsl/glsl_to_nir.h"
#include "compiler/glsl/gl_nir.h"
#include "compiler/glsl/gl_nir_linker.h"
#include "compiler/glsl/ir.h"
#include "compiler/glsl/ir_optimization.h"
#include "compiler/glsl/linker_util.h"
#include "compiler/glsl/string_to_uint_map.h"

static int
type_size(const struct glsl_type *type)
{
   return type->count_attribute_slots(false);
}

/* Depending on PIPE_CAP_TGSI_TEXCOORD (st->needs_texcoord_semantic) we
 * may need to fix up varying slots so the glsl->nir path is aligned
 * with the anything->tgsi->nir path.
 */
static void
st_nir_fixup_varying_slots(struct st_context *st, nir_shader *shader,
                           nir_variable_mode mode)
{
   if (st->needs_texcoord_semantic)
      return;

   /* This is called from finalize, but we don't want to do this adjustment twice. */
   assert(!st->allow_st_finalize_nir_twice);

   nir_foreach_variable_with_modes(var, shader, mode) {
      if (var->data.location >= VARYING_SLOT_VAR0) {
         var->data.location += 9;
      } else if (var->data.location == VARYING_SLOT_PNTC) {
         var->data.location = VARYING_SLOT_VAR8;
      } else if ((var->data.location >= VARYING_SLOT_TEX0) &&
               (var->data.location <= VARYING_SLOT_TEX7)) {
         var->data.location += VARYING_SLOT_VAR0 - VARYING_SLOT_TEX0;
      }
   }
}

static void
st_shader_gather_info(nir_shader *nir, struct gl_program *prog)
{
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   /* Copy the info we just generated back into the gl_program */
   const char *prog_name = prog->info.name;
   const char *prog_label = prog->info.label;
   prog->info = nir->info;
   prog->info.name = prog_name;
   prog->info.label = prog_label;
}

/* input location assignment for VS inputs must be handled specially, so
 * that it is aligned w/ st's vbo state.
 * (This isn't the case with, for ex, FS inputs, which only need to agree
 * on varying-slot w/ the VS outputs)
 */
void
st_nir_assign_vs_in_locations(struct nir_shader *nir)
{
   if (nir->info.stage != MESA_SHADER_VERTEX || nir->info.io_lowered)
      return;

   nir->num_inputs = util_bitcount64(nir->info.inputs_read);

   bool removed_inputs = false;

   nir_foreach_shader_in_variable_safe(var, nir) {
      /* NIR already assigns dual-slot inputs to two locations so all we have
       * to do is compact everything down.
       */
      if (nir->info.inputs_read & BITFIELD64_BIT(var->data.location)) {
         var->data.driver_location =
            util_bitcount64(nir->info.inputs_read &
                              BITFIELD64_MASK(var->data.location));
      } else {
         /* Convert unused input variables to shader_temp (with no
          * initialization), to avoid confusing drivers looking through the
          * inputs array and expecting to find inputs with a driver_location
          * set.
          */
         var->data.mode = nir_var_shader_temp;
         removed_inputs = true;
      }
   }

   /* Re-lower global vars, to deal with any dead VS inputs. */
   if (removed_inputs)
      NIR_PASS_V(nir, nir_lower_global_vars_to_local);
}

static int
st_nir_lookup_parameter_index(struct gl_program *prog, nir_variable *var)
{
   struct gl_program_parameter_list *params = prog->Parameters;

   /* Lookup the first parameter that the uniform storage that match the
    * variable location.
    */
   for (unsigned i = 0; i < params->NumParameters; i++) {
      int index = params->Parameters[i].MainUniformStorageIndex;
      if (index == var->data.location)
         return i;
   }

   /* TODO: Handle this fallback for SPIR-V.  We need this for GLSL e.g. in
    * dEQP-GLES2.functional.uniform_api.random.3
    */

   /* is there a better way to do this?  If we have something like:
    *
    *    struct S {
    *           float f;
    *           vec4 v;
    *    };
    *    uniform S color;
    *
    * Then what we get in prog->Parameters looks like:
    *
    *    0: Name=color.f, Type=6, DataType=1406, Size=1
    *    1: Name=color.v, Type=6, DataType=8b52, Size=4
    *
    * So the name doesn't match up and _mesa_lookup_parameter_index()
    * fails.  In this case just find the first matching "color.*"..
    *
    * Note for arrays you could end up w/ color[n].f, for example.
    *
    * glsl_to_tgsi works slightly differently in this regard.  It is
    * emitting something more low level, so it just translates the
    * params list 1:1 to CONST[] regs.  Going from GLSL IR to TGSI,
    * it just calculates the additional offset of struct field members
    * in glsl_to_tgsi_visitor::visit(ir_dereference_record *ir) or
    * glsl_to_tgsi_visitor::visit(ir_dereference_array *ir).  It never
    * needs to work backwards to get base var loc from the param-list
    * which already has them separated out.
    */
   if (!prog->sh.data->spirv) {
      int namelen = strlen(var->name);
      for (unsigned i = 0; i < params->NumParameters; i++) {
         struct gl_program_parameter *p = &params->Parameters[i];
         if ((strncmp(p->Name, var->name, namelen) == 0) &&
             ((p->Name[namelen] == '.') || (p->Name[namelen] == '['))) {
            return i;
         }
      }
   }

   return -1;
}

static void
st_nir_assign_uniform_locations(struct gl_context *ctx,
                                struct gl_program *prog,
                                nir_shader *nir)
{
   int shaderidx = 0;
   int imageidx = 0;

   nir_foreach_uniform_variable(uniform, nir) {
      int loc;

      const struct glsl_type *type = glsl_without_array(uniform->type);
      if (!uniform->data.bindless && (type->is_sampler() || type->is_image())) {
         if (type->is_sampler()) {
            loc = shaderidx;
            shaderidx += type_size(uniform->type);
         } else {
            loc = imageidx;
            imageidx += type_size(uniform->type);
         }
      } else if (uniform->state_slots) {
         const gl_state_index16 *const stateTokens = uniform->state_slots[0].tokens;
         /* This state reference has already been setup by ir_to_mesa, but we'll
          * get the same index back here.
          */

         unsigned comps;
         if (glsl_type_is_struct_or_ifc(type)) {
            comps = 4;
         } else {
            comps = glsl_get_vector_elements(type);
         }

         if (ctx->Const.PackedDriverUniformStorage) {
            loc = _mesa_add_sized_state_reference(prog->Parameters,
                                                  stateTokens, comps, false);
            loc = prog->Parameters->Parameters[loc].ValueOffset;
         } else {
            loc = _mesa_add_state_reference(prog->Parameters, stateTokens);
         }
      } else {
         loc = st_nir_lookup_parameter_index(prog, uniform);

         /* We need to check that loc is not -1 here before accessing the
          * array. It can be negative for example when we have a struct that
          * only contains opaque types.
          */
         if (loc >= 0 && ctx->Const.PackedDriverUniformStorage) {
            loc = prog->Parameters->Parameters[loc].ValueOffset;
         }
      }

      uniform->data.driver_location = loc;
   }
}

void
st_nir_opts(nir_shader *nir)
{
   bool progress;

   do {
      progress = false;

      NIR_PASS_V(nir, nir_lower_vars_to_ssa);
      
      /* Linking deals with unused inputs/outputs, but here we can remove
       * things local to the shader in the hopes that we can cleanup other
       * things. This pass will also remove variables with only stores, so we
       * might be able to make progress after it.
       */
      NIR_PASS(progress, nir, nir_remove_dead_variables,
               nir_var_function_temp | nir_var_shader_temp |
               nir_var_mem_shared,
               NULL);

      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_dead_write_vars);

      if (nir->options->lower_to_scalar) {
         NIR_PASS_V(nir, nir_lower_alu_to_scalar,
                    nir->options->lower_to_scalar_filter, NULL);
         NIR_PASS_V(nir, nir_lower_phis_to_scalar, false);
      }

      NIR_PASS_V(nir, nir_lower_alu);
      NIR_PASS_V(nir, nir_lower_pack);
      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);
      if (nir_opt_trivial_continues(nir)) {
         progress = true;
         NIR_PASS(progress, nir, nir_copy_prop);
         NIR_PASS(progress, nir, nir_opt_dce);
      }
      NIR_PASS(progress, nir, nir_opt_if, false);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_peephole_select, 8, true, true);

      NIR_PASS(progress, nir, nir_opt_phi_precision);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      if (!nir->info.flrp_lowered) {
         unsigned lower_flrp =
            (nir->options->lower_flrp16 ? 16 : 0) |
            (nir->options->lower_flrp32 ? 32 : 0) |
            (nir->options->lower_flrp64 ? 64 : 0);

         if (lower_flrp) {
            bool lower_flrp_progress = false;

            NIR_PASS(lower_flrp_progress, nir, nir_lower_flrp,
                     lower_flrp,
                     false /* always_precise */);
            if (lower_flrp_progress) {
               NIR_PASS(progress, nir,
                        nir_opt_constant_folding);
               progress = true;
            }
         }

         /* Nothing should rematerialize any flrps, so we only need to do this
          * lowering once.
          */
         nir->info.flrp_lowered = true;
      }

      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_opt_conditional_discard);
      if (nir->options->max_unroll_iterations) {
         NIR_PASS(progress, nir, nir_opt_loop_unroll);
      }
   } while (progress);
}

static void
shared_type_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size = glsl_type_is_boolean(type)
      ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length,
   *align = comp_size * (length == 3 ? 4 : length);
}

/* First third of converting glsl_to_nir.. this leaves things in a pre-
 * nir_lower_io state, so that shader variants can more easily insert/
 * replace variables, etc.
 */
static void
st_nir_preprocess(struct st_context *st, struct gl_program *prog,
                  struct gl_shader_program *shader_program,
                  gl_shader_stage stage)
{
   struct pipe_screen *screen = st->screen;
   const nir_shader_compiler_options *options =
      st->ctx->Const.ShaderCompilerOptions[prog->info.stage].NirOptions;
   assert(options);
   nir_shader *nir = prog->nir;

   /* Set the next shader stage hint for VS and TES. */
   if (!nir->info.separate_shader &&
       (nir->info.stage == MESA_SHADER_VERTEX ||
        nir->info.stage == MESA_SHADER_TESS_EVAL)) {

      unsigned prev_stages = (1 << (prog->info.stage + 1)) - 1;
      unsigned stages_mask =
         ~prev_stages & shader_program->data->linked_stages;

      nir->info.next_stage = stages_mask ?
         (gl_shader_stage) u_bit_scan(&stages_mask) : MESA_SHADER_FRAGMENT;
   } else {
      nir->info.next_stage = MESA_SHADER_FRAGMENT;
   }

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   if (!st->ctx->SoftFP64 && ((nir->info.bit_sizes_int | nir->info.bit_sizes_float) & 64) &&
       (options->lower_doubles_options & nir_lower_fp64_full_software) != 0) {
      st->ctx->SoftFP64 = glsl_float64_funcs_to_nir(st->ctx, options);
   }

   /* ES has strict SSO validation rules for shader IO matching so we can't
    * remove dead IO until the resource list has been built. Here we skip
    * removing them until later. This will potentially make the IO lowering
    * calls below do a little extra work but should otherwise have no impact.
    */
   if (!_mesa_is_gles(st->ctx) || !nir->info.separate_shader) {
      nir_variable_mode mask = nir_var_shader_in | nir_var_shader_out;
      nir_remove_dead_variables(nir, mask, NULL);
   }

   if (options->lower_all_io_to_temps ||
       nir->info.stage == MESA_SHADER_VERTEX ||
       nir->info.stage == MESA_SHADER_GEOMETRY) {
      NIR_PASS_V(nir, nir_lower_io_to_temporaries,
                 nir_shader_get_entrypoint(nir),
                 true, true);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT ||
              !screen->get_param(screen, PIPE_CAP_TGSI_CAN_READ_OUTPUTS)) {
      NIR_PASS_V(nir, nir_lower_io_to_temporaries,
                 nir_shader_get_entrypoint(nir),
                 true, false);
   }

   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);

   if (options->lower_to_scalar) {
     NIR_PASS_V(nir, nir_lower_alu_to_scalar,
                options->lower_to_scalar_filter, NULL);
   }

   /* before buffers and vars_to_ssa */
   NIR_PASS_V(nir, gl_nir_lower_images, true);

   /* TODO: Change GLSL to not lower shared memory. */
   if (prog->nir->info.stage == MESA_SHADER_COMPUTE &&
       shader_program->data->spirv) {
      NIR_PASS_V(prog->nir, nir_lower_vars_to_explicit_types,
                 nir_var_mem_shared, shared_type_info);
      NIR_PASS_V(prog->nir, nir_lower_explicit_io,
                 nir_var_mem_shared, nir_address_format_32bit_offset);
   }

   /* Do a round of constant folding to clean up address calculations */
   NIR_PASS_V(nir, nir_opt_constant_folding);
}

static bool
dest_is_64bit(nir_dest *dest, void *state)
{
   bool *lower = (bool *)state;
   if (dest && (nir_dest_bit_size(*dest) == 64)) {
      *lower = true;
      return false;
   }
   return true;
}

static bool
src_is_64bit(nir_src *src, void *state)
{
   bool *lower = (bool *)state;
   if (src && (nir_src_bit_size(*src) == 64)) {
      *lower = true;
      return false;
   }
   return true;
}

static bool
filter_64_bit_instr(const nir_instr *const_instr, UNUSED const void *data)
{
   bool lower = false;
   /* lower_alu_to_scalar required nir_instr to be const, but nir_foreach_*
    * doesn't have const variants, so do the ugly const_cast here. */
   nir_instr *instr = const_cast<nir_instr *>(const_instr);

   nir_foreach_dest(instr, dest_is_64bit, &lower);
   if (lower)
      return true;
   nir_foreach_src(instr, src_is_64bit, &lower);
   return lower;
}

/* Second third of converting glsl_to_nir. This creates uniforms, gathers
 * info on varyings, etc after NIR link time opts have been applied.
 */
static char *
st_glsl_to_nir_post_opts(struct st_context *st, struct gl_program *prog,
                         struct gl_shader_program *shader_program)
{
   nir_shader *nir = prog->nir;
   struct pipe_screen *screen = st->screen;

   /* Make a pass over the IR to add state references for any built-in
    * uniforms that are used.  This has to be done now (during linking).
    * Code generation doesn't happen until the first time this shader is
    * used for rendering.  Waiting until then to generate the parameters is
    * too late.  At that point, the values for the built-in uniforms won't
    * get sent to the shader.
    */
   nir_foreach_uniform_variable(var, nir) {
      const nir_state_slot *const slots = var->state_slots;
      if (slots != NULL) {
         const struct glsl_type *type = glsl_without_array(var->type);
         for (unsigned int i = 0; i < var->num_state_slots; i++) {
            unsigned comps;
            if (glsl_type_is_struct_or_ifc(type)) {
               comps = _mesa_program_state_value_size(slots[i].tokens);
            } else {
               comps = glsl_get_vector_elements(type);
            }

            if (st->ctx->Const.PackedDriverUniformStorage) {
               _mesa_add_sized_state_reference(prog->Parameters,
                                               slots[i].tokens,
                                               comps, false);
            } else {
               _mesa_add_state_reference(prog->Parameters,
                                         slots[i].tokens);
            }
         }
      }
   }

   /* Avoid reallocation of the program parameter list, because the uniform
    * storage is only associated with the original parameter list.
    * This should be enough for Bitmap and DrawPixels constants.
    */
   _mesa_ensure_and_associate_uniform_storage(st->ctx, shader_program, prog, 16);

   st_set_prog_affected_state_flags(prog);

   /* None of the builtins being lowered here can be produced by SPIR-V.  See
    * _mesa_builtin_uniform_desc. Also drivers that support packed uniform
    * storage don't need to lower builtins.
    */
   if (!shader_program->data->spirv &&
       !st->ctx->Const.PackedDriverUniformStorage) {
      /* at this point, array uniforms have been split into separate
       * nir_variable structs where possible. this codepath can't handle dynamic
       * array indexing, however, so all indirect uniform derefs
       * must be eliminated beforehand to avoid trying to lower one of those builtins
       */
      NIR_PASS_V(nir, nir_lower_indirect_builtin_uniform_derefs);
      NIR_PASS_V(nir, st_nir_lower_builtin);
   }

   if (!screen->get_param(screen, PIPE_CAP_NIR_ATOMICS_AS_DEREF))
      NIR_PASS_V(nir, gl_nir_lower_atomics, shader_program, true);

   NIR_PASS_V(nir, nir_opt_intrinsics);
   NIR_PASS_V(nir, nir_opt_fragdepth);

   /* Lower 64-bit ops. */
   if (nir->options->lower_int64_options ||
       nir->options->lower_doubles_options) {
      bool lowered_64bit_ops = false;
      bool revectorize = false;

      /* nir_lower_doubles is not prepared for vector ops, so if the backend doesn't
       * request lower_alu_to_scalar until now, lower all 64 bit ops, and try to
       * vectorize them afterwards again */
      if (!nir->options->lower_to_scalar) {
         NIR_PASS(revectorize, nir, nir_lower_alu_to_scalar, filter_64_bit_instr, nullptr);
         NIR_PASS(revectorize, nir, nir_lower_phis_to_scalar, false);
      }

      if (nir->options->lower_doubles_options) {
         NIR_PASS(lowered_64bit_ops, nir, nir_lower_doubles,
                  st->ctx->SoftFP64, nir->options->lower_doubles_options);
      }
      if (nir->options->lower_int64_options)
         NIR_PASS(lowered_64bit_ops, nir, nir_lower_int64);

      if (revectorize)
         NIR_PASS_V(nir, nir_opt_vectorize, nullptr, nullptr);

      if (revectorize || lowered_64bit_ops)
         st_nir_opts(nir);
   }

   nir_variable_mode mask =
      nir_var_shader_in | nir_var_shader_out | nir_var_function_temp;
   nir_remove_dead_variables(nir, mask, NULL);

   if (!st->has_hw_atomics && !screen->get_param(screen, PIPE_CAP_NIR_ATOMICS_AS_DEREF))
      NIR_PASS_V(nir, nir_lower_atomics_to_ssbo);

   st_finalize_nir_before_variants(nir);

   char *msg = NULL;
   if (st->allow_st_finalize_nir_twice)
      msg = st_finalize_nir(st, prog, shader_program, nir, true, true);

   if (st->ctx->_Shader->Flags & GLSL_DUMP) {
      _mesa_log("\n");
      _mesa_log("NIR IR for linked %s program %d:\n",
             _mesa_shader_stage_to_string(prog->info.stage),
             shader_program->Name);
      nir_print_shader(nir, _mesa_get_log_file());
      _mesa_log("\n\n");
   }

   return msg;
}

static void
st_nir_vectorize_io(nir_shader *producer, nir_shader *consumer)
{
   NIR_PASS_V(producer, nir_lower_io_to_vector, nir_var_shader_out);
   NIR_PASS_V(producer, nir_opt_combine_stores, nir_var_shader_out);
   NIR_PASS_V(consumer, nir_lower_io_to_vector, nir_var_shader_in);

   if ((producer)->info.stage != MESA_SHADER_TESS_CTRL) {
      /* Calling lower_io_to_vector creates output variable writes with
       * write-masks.  We only support these for TCS outputs, so for other
       * stages, we need to call nir_lower_io_to_temporaries to get rid of
       * them.  This, in turn, creates temporary variables and extra
       * copy_deref intrinsics that we need to clean up.
       */
      NIR_PASS_V(producer, nir_lower_io_to_temporaries,
                 nir_shader_get_entrypoint(producer), true, false);
      NIR_PASS_V(producer, nir_lower_global_vars_to_local);
      NIR_PASS_V(producer, nir_split_var_copies);
      NIR_PASS_V(producer, nir_lower_var_copies);
   }

   /* Undef scalar store_deref intrinsics are not ignored by nir_lower_io,
    * so they must be removed before that. These passes remove them.
    */
   NIR_PASS_V(producer, nir_lower_vars_to_ssa);
   NIR_PASS_V(producer, nir_opt_undef);
   NIR_PASS_V(producer, nir_opt_dce);
}

static void
st_nir_link_shaders(nir_shader *producer, nir_shader *consumer)
{
   if (producer->options->lower_to_scalar) {
      NIR_PASS_V(producer, nir_lower_io_to_scalar_early, nir_var_shader_out);
      NIR_PASS_V(consumer, nir_lower_io_to_scalar_early, nir_var_shader_in);
   }

   nir_lower_io_arrays_to_elements(producer, consumer);

   st_nir_opts(producer);
   st_nir_opts(consumer);

   if (nir_link_opt_varyings(producer, consumer))
      st_nir_opts(consumer);

   NIR_PASS_V(producer, nir_remove_dead_variables, nir_var_shader_out, NULL);
   NIR_PASS_V(consumer, nir_remove_dead_variables, nir_var_shader_in, NULL);

   if (nir_remove_unused_varyings(producer, consumer)) {
      NIR_PASS_V(producer, nir_lower_global_vars_to_local);
      NIR_PASS_V(consumer, nir_lower_global_vars_to_local);

      st_nir_opts(producer);
      st_nir_opts(consumer);

      /* Optimizations can cause varyings to become unused.
       * nir_compact_varyings() depends on all dead varyings being removed so
       * we need to call nir_remove_dead_variables() again here.
       */
      NIR_PASS_V(producer, nir_remove_dead_variables, nir_var_shader_out,
                 NULL);
      NIR_PASS_V(consumer, nir_remove_dead_variables, nir_var_shader_in,
                 NULL);
   }

   nir_link_varying_precision(producer, consumer);
}

static void
st_lower_patch_vertices_in(struct gl_shader_program *shader_prog)
{
   struct gl_linked_shader *linked_tcs =
      shader_prog->_LinkedShaders[MESA_SHADER_TESS_CTRL];
   struct gl_linked_shader *linked_tes =
      shader_prog->_LinkedShaders[MESA_SHADER_TESS_EVAL];

   /* If we have a TCS and TES linked together, lower TES patch vertices. */
   if (linked_tcs && linked_tes) {
      nir_shader *tcs_nir = linked_tcs->Program->nir;
      nir_shader *tes_nir = linked_tes->Program->nir;

      /* The TES input vertex count is the TCS output vertex count,
       * lower TES gl_PatchVerticesIn to a constant.
       */
      uint32_t tes_patch_verts = tcs_nir->info.tess.tcs_vertices_out;
      NIR_PASS_V(tes_nir, nir_lower_patch_vertices, tes_patch_verts, NULL);
   }
}

extern "C" {

void
st_nir_lower_wpos_ytransform(struct nir_shader *nir,
                             struct gl_program *prog,
                             struct pipe_screen *pscreen)
{
   if (nir->info.stage != MESA_SHADER_FRAGMENT)
      return;

   static const gl_state_index16 wposTransformState[STATE_LENGTH] = {
      STATE_FB_WPOS_Y_TRANSFORM
   };
   nir_lower_wpos_ytransform_options wpos_options = { { 0 } };

   memcpy(wpos_options.state_tokens, wposTransformState,
          sizeof(wpos_options.state_tokens));
   wpos_options.fs_coord_origin_upper_left =
      pscreen->get_param(pscreen,
                         PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT);
   wpos_options.fs_coord_origin_lower_left =
      pscreen->get_param(pscreen,
                         PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT);
   wpos_options.fs_coord_pixel_center_integer =
      pscreen->get_param(pscreen,
                         PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER);
   wpos_options.fs_coord_pixel_center_half_integer =
      pscreen->get_param(pscreen,
                         PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER);

   if (nir_lower_wpos_ytransform(nir, &wpos_options)) {
      nir_validate_shader(nir, "after nir_lower_wpos_ytransform");
      _mesa_add_state_reference(prog->Parameters, wposTransformState);
   }

   static const gl_state_index16 pntcTransformState[STATE_LENGTH] = {
      STATE_FB_PNTC_Y_TRANSFORM
   };

   if (nir_lower_pntc_ytransform(nir, &pntcTransformState)) {
      _mesa_add_state_reference(prog->Parameters, pntcTransformState);
   }
}

bool
st_link_nir(struct gl_context *ctx,
            struct gl_shader_program *shader_program)
{
   struct st_context *st = st_context(ctx);
   struct gl_linked_shader *linked_shader[MESA_SHADER_STAGES];
   unsigned num_shaders = 0;

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (shader_program->_LinkedShaders[i])
         linked_shader[num_shaders++] = shader_program->_LinkedShaders[i];
   }

   for (unsigned i = 0; i < num_shaders; i++) {
      struct gl_linked_shader *shader = linked_shader[i];
      const nir_shader_compiler_options *options =
         st->ctx->Const.ShaderCompilerOptions[shader->Stage].NirOptions;
      struct gl_program *prog = shader->Program;
      struct st_program *stp = (struct st_program *)prog;

      _mesa_copy_linked_program_data(shader_program, shader);

      assert(!prog->nir);
      stp->shader_program = shader_program;
      stp->state.type = PIPE_SHADER_IR_NIR;

      /* Parameters will be filled during NIR linking. */
      prog->Parameters = _mesa_new_parameter_list();

      if (shader_program->data->spirv) {
         prog->nir = _mesa_spirv_to_nir(ctx, shader_program, shader->Stage, options);
      } else {
         validate_ir_tree(shader->ir);

         if (ctx->_Shader->Flags & GLSL_DUMP) {
            _mesa_log("\n");
            _mesa_log("GLSL IR for linked %s program %d:\n",
                      _mesa_shader_stage_to_string(shader->Stage),
                      shader_program->Name);
            _mesa_print_ir(_mesa_get_log_file(), shader->ir, NULL);
            _mesa_log("\n\n");
         }

         prog->nir = glsl_to_nir(st->ctx, shader_program, shader->Stage, options);
         st_nir_preprocess(st, prog, shader_program, shader->Stage);
      }

      if (options->lower_to_scalar) {
         NIR_PASS_V(shader->Program->nir, nir_lower_load_const_to_scalar);
      }
   }

   st_lower_patch_vertices_in(shader_program);

   /* For SPIR-V, we have to perform the NIR linking before applying
    * st_nir_preprocess.
    */
   if (shader_program->data->spirv) {
      static const gl_nir_linker_options opts = {
         true /*fill_parameters */
      };
      if (!gl_nir_link_spirv(ctx, shader_program, &opts))
         return GL_FALSE;

      nir_build_program_resource_list(ctx, shader_program, true);

      for (unsigned i = 0; i < num_shaders; i++) {
         struct gl_linked_shader *shader = linked_shader[i];
         struct gl_program *prog = shader->Program;

         prog->ExternalSamplersUsed = gl_external_samplers(prog);
         _mesa_update_shader_textures_used(shader_program, prog);
         st_nir_preprocess(st, prog, shader_program, shader->Stage);
      }
   }

   /* Linking the stages in the opposite order (from fragment to vertex)
    * ensures that inter-shader outputs written to in an earlier stage
    * are eliminated if they are (transitively) not used in a later
    * stage.
    */
   for (int i = num_shaders - 2; i >= 0; i--) {
      st_nir_link_shaders(linked_shader[i]->Program->nir,
                          linked_shader[i + 1]->Program->nir);
   }
   /* Linking shaders also optimizes them. Separate shaders, compute shaders
    * and shaders with a fixed-func VS or FS that don't need linking are
    * optimized here.
    */
   if (num_shaders == 1)
      st_nir_opts(linked_shader[0]->Program->nir);

   if (!shader_program->data->spirv) {
      if (!gl_nir_link_glsl(ctx, shader_program))
         return GL_FALSE;

      for (unsigned i = 0; i < num_shaders; i++) {
         struct gl_program *prog = linked_shader[i]->Program;
         prog->ExternalSamplersUsed = gl_external_samplers(prog);
         _mesa_update_shader_textures_used(shader_program, prog);
      }

      nir_build_program_resource_list(ctx, shader_program, false);
   }

   for (unsigned i = 0; i < num_shaders; i++) {
      struct gl_linked_shader *shader = linked_shader[i];
      nir_shader *nir = shader->Program->nir;

      /* don't infer ACCESS_NON_READABLE so that Program->sh.ImageAccess is
       * correct: https://gitlab.freedesktop.org/mesa/mesa/-/issues/3278
       */
      nir_opt_access_options opt_access_options;
      opt_access_options.is_vulkan = false;
      opt_access_options.infer_non_readable = false;
      NIR_PASS_V(nir, nir_opt_access, &opt_access_options);

      /* This needs to run after the initial pass of nir_lower_vars_to_ssa, so
       * that the buffer indices are constants in nir where they where
       * constants in GLSL. */
      NIR_PASS_V(nir, gl_nir_lower_buffers, shader_program);

      /* Remap the locations to slots so those requiring two slots will occupy
       * two locations. For instance, if we have in the IR code a dvec3 attr0 in
       * location 0 and vec4 attr1 in location 1, in NIR attr0 will use
       * locations/slots 0 and 1, and attr1 will use location/slot 2
       */
      if (nir->info.stage == MESA_SHADER_VERTEX && !shader_program->data->spirv)
         nir_remap_dual_slot_attributes(nir, &shader->Program->DualSlotInputs);

      NIR_PASS_V(nir, st_nir_lower_wpos_ytransform, shader->Program,
                 st->screen);

      NIR_PASS_V(nir, nir_lower_system_values);
      NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

      NIR_PASS_V(nir, nir_lower_clip_cull_distance_arrays);

      st_shader_gather_info(nir, shader->Program);
      if (shader->Stage == MESA_SHADER_VERTEX) {
         /* NIR expands dual-slot inputs out to two locations.  We need to
          * compact things back down GL-style single-slot inputs to avoid
          * confusing the state tracker.
          */
         shader->Program->info.inputs_read =
            nir_get_single_slot_attribs_mask(nir->info.inputs_read,
                                             shader->Program->DualSlotInputs);
      }

      if (i >= 1) {
         struct gl_program *prev_shader = linked_shader[i - 1]->Program;

         /* We can't use nir_compact_varyings with transform feedback, since
          * the pipe_stream_output->output_register field is based on the
          * pre-compacted driver_locations.
          */
         if (!(prev_shader->sh.LinkedTransformFeedback &&
               prev_shader->sh.LinkedTransformFeedback->NumVarying > 0))
            nir_compact_varyings(prev_shader->nir,
                                 nir, ctx->API != API_OPENGL_COMPAT);

         if (ctx->Const.ShaderCompilerOptions[shader->Stage].NirOptions->vectorize_io)
            st_nir_vectorize_io(prev_shader->nir, nir);
      }
   }

   struct shader_info *prev_info = NULL;

   for (unsigned i = 0; i < num_shaders; i++) {
      struct gl_linked_shader *shader = linked_shader[i];
      struct shader_info *info = &shader->Program->nir->info;

      char *msg = st_glsl_to_nir_post_opts(st, shader->Program, shader_program);
      if (msg) {
         linker_error(shader_program, msg);
         break;
      }

      if (prev_info &&
          ctx->Const.ShaderCompilerOptions[shader->Stage].NirOptions->unify_interfaces) {
         prev_info->outputs_written |= info->inputs_read &
            ~(VARYING_BIT_TESS_LEVEL_INNER | VARYING_BIT_TESS_LEVEL_OUTER);
         info->inputs_read |= prev_info->outputs_written &
            ~(VARYING_BIT_TESS_LEVEL_INNER | VARYING_BIT_TESS_LEVEL_OUTER);

         prev_info->patch_outputs_written |= info->patch_inputs_read;
         info->patch_inputs_read |= prev_info->patch_outputs_written;
      }
      prev_info = info;
   }

   for (unsigned i = 0; i < num_shaders; i++) {
      struct gl_linked_shader *shader = linked_shader[i];
      struct gl_program *prog = shader->Program;
      struct st_program *stp = st_program(prog);

      /* Make sure that prog->info is in sync with nir->info, but st/mesa
       * expects some of the values to be from before lowering.
       */
      shader_info old_info = prog->info;
      prog->info = prog->nir->info;
      prog->info.name = old_info.name;
      prog->info.label = old_info.label;
      prog->info.num_ssbos = old_info.num_ssbos;
      prog->info.num_ubos = old_info.num_ubos;
      prog->info.num_abos = old_info.num_abos;
      if (prog->info.stage == MESA_SHADER_VERTEX)
         prog->info.inputs_read = old_info.inputs_read;

      /* Initialize st_vertex_program members. */
      if (shader->Stage == MESA_SHADER_VERTEX)
         st_prepare_vertex_program(stp, NULL);

      /* Get pipe_stream_output_info. */
      if (shader->Stage == MESA_SHADER_VERTEX ||
          shader->Stage == MESA_SHADER_TESS_EVAL ||
          shader->Stage == MESA_SHADER_GEOMETRY)
         st_translate_stream_output_info(prog);

      st_store_ir_in_disk_cache(st, prog, true);

      st_release_variants(st, stp);
      st_finalize_program(st, prog);

      /* The GLSL IR won't be needed anymore. */
      ralloc_free(shader->ir);
      shader->ir = NULL;
   }

   return true;
}

void
st_nir_assign_varying_locations(struct st_context *st, nir_shader *nir)
{
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      nir_assign_io_var_locations(nir, nir_var_shader_out,
                                  &nir->num_outputs,
                                  nir->info.stage);
      st_nir_fixup_varying_slots(st, nir, nir_var_shader_out);
   } else if (nir->info.stage == MESA_SHADER_GEOMETRY ||
              nir->info.stage == MESA_SHADER_TESS_CTRL ||
              nir->info.stage == MESA_SHADER_TESS_EVAL) {
      nir_assign_io_var_locations(nir, nir_var_shader_in,
                                  &nir->num_inputs,
                                  nir->info.stage);
      st_nir_fixup_varying_slots(st, nir, nir_var_shader_in);

      nir_assign_io_var_locations(nir, nir_var_shader_out,
                                  &nir->num_outputs,
                                  nir->info.stage);
      st_nir_fixup_varying_slots(st, nir, nir_var_shader_out);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      nir_assign_io_var_locations(nir, nir_var_shader_in,
                                  &nir->num_inputs,
                                  nir->info.stage);
      st_nir_fixup_varying_slots(st, nir, nir_var_shader_in);
      nir_assign_io_var_locations(nir, nir_var_shader_out,
                                  &nir->num_outputs,
                                  nir->info.stage);
   } else if (nir->info.stage == MESA_SHADER_COMPUTE) {
       /* TODO? */
   } else {
      unreachable("invalid shader type");
   }
}

void
st_nir_lower_samplers(struct pipe_screen *screen, nir_shader *nir,
                      struct gl_shader_program *shader_program,
                      struct gl_program *prog)
{
   if (screen->get_param(screen, PIPE_CAP_NIR_SAMPLERS_AS_DEREF))
      NIR_PASS_V(nir, gl_nir_lower_samplers_as_deref, shader_program);
   else
      NIR_PASS_V(nir, gl_nir_lower_samplers, shader_program);

   if (prog) {
      BITSET_COPY(prog->info.textures_used, nir->info.textures_used);
      BITSET_COPY(prog->info.textures_used_by_txf, nir->info.textures_used_by_txf);
      prog->info.images_used = nir->info.images_used;
   }
}

static int
st_packed_uniforms_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_dword_slots(type, bindless);
}

static int
st_unpacked_uniforms_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_vec4_slots(type, false, bindless);
}

void
st_nir_lower_uniforms(struct st_context *st, nir_shader *nir)
{
   if (st->ctx->Const.PackedDriverUniformStorage) {
      NIR_PASS_V(nir, nir_lower_io, nir_var_uniform,
                 st_packed_uniforms_type_size,
                 (nir_lower_io_options)0);
   } else {
      NIR_PASS_V(nir, nir_lower_io, nir_var_uniform,
                 st_unpacked_uniforms_type_size,
                 (nir_lower_io_options)0);
   }

   if (nir->options->lower_uniforms_to_ubo)
      NIR_PASS_V(nir, nir_lower_uniforms_to_ubo,
                 st->ctx->Const.PackedDriverUniformStorage,
                 !st->ctx->Const.NativeIntegers);
}

/* Last third of preparing nir from glsl, which happens after shader
 * variant lowering.
 */
char *
st_finalize_nir(struct st_context *st, struct gl_program *prog,
                struct gl_shader_program *shader_program,
                nir_shader *nir, bool finalize_by_driver,
                bool is_before_variants)
{
   struct pipe_screen *screen = st->screen;

   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);

   if (st->lower_rect_tex) {
      struct nir_lower_tex_options opts = { 0 };

      opts.lower_rect = true;

      NIR_PASS_V(nir, nir_lower_tex, &opts);
   }

   st_nir_assign_varying_locations(st, nir);
   st_nir_assign_uniform_locations(st->ctx, prog, nir);

   /* Set num_uniforms in number of attribute slots (vec4s) */
   nir->num_uniforms = DIV_ROUND_UP(prog->Parameters->NumParameterValues, 4);

   st_nir_lower_uniforms(st, nir);

   if (is_before_variants && nir->options->lower_uniforms_to_ubo) {
      /* This must be done after uniforms are lowered to UBO and all
       * nir_var_uniform variables are removed from NIR to prevent conflicts
       * between state parameter merging and shader variant generation.
       */
      _mesa_optimize_state_parameters(&st->ctx->Const, prog->Parameters);
   }

   st_nir_lower_samplers(screen, nir, shader_program, prog);
   if (!screen->get_param(screen, PIPE_CAP_NIR_IMAGES_AS_DEREF))
      NIR_PASS_V(nir, gl_nir_lower_images, false);

   char *msg = NULL;
   if (finalize_by_driver && screen->finalize_nir)
      msg = screen->finalize_nir(screen, nir);

   return msg;
}

} /* extern "C" */
