/*
 * Copyright 2023 Pavel Ondračka <pavel.ondracka@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "r300_nir.h"

#include "compiler/nir/nir_builder.h"
#include "r300_screen.h"

bool
r300_is_only_used_as_float(const nir_alu_instr *instr)
{
   nir_foreach_use (src, &instr->def) {
      if (nir_src_is_if(src))
         return false;

      nir_instr *user_instr = nir_src_parent_instr(src);
      if (user_instr->type == nir_instr_type_alu) {
         nir_alu_instr *alu = nir_instr_as_alu(user_instr);
         switch (alu->op) {
         case nir_op_mov:
         case nir_op_vec2:
         case nir_op_vec3:
         case nir_op_vec4:
         case nir_op_bcsel:
         case nir_op_b32csel:
            if (!r300_is_only_used_as_float(alu))
               return false;
            break;
         default:
            break;
         }

         const nir_op_info *info = &nir_op_infos[alu->op];
         nir_alu_src *alu_src = exec_node_data(nir_alu_src, src, src);
         int src_idx = alu_src - &alu->src[0];
         if ((info->input_types[src_idx] & nir_type_int) ||
             (info->input_types[src_idx] & nir_type_bool))
            return false;
      }
   }
   return true;
}

static unsigned char
r300_should_vectorize_instr(const nir_instr *instr, const void *data)
{
   bool *too_many_ubos = (bool *)data;

   if (instr->type != nir_instr_type_alu)
      return 0;

   /* Vectorization can make the constant layout worse and increase
    * the constant register usage. The worst scenario is vectorization
    * of lowered indirect register access, where we access i-th element
    * and later we access i-1 or i+1 (most notably glamor and gsk shaders).
    * In this case we already added constants 1..n where n is the array
    * size, however we can reuse them unless the lowered ladder gets
    * vectorized later.
    *
    * Thus prevent vectorization of the specific patterns from lowered
    * indirect access.
    *
    * This is quite a heavy hammer, we could in theory estimate how many
    * slots will the current ubos and constants need and only disable
    * vectorization when we are close to the limit. However, this would
    * likely need a global shader analysis each time r300_should_vectorize_inst
    * is called, which we want to avoid.
    *
    * So for now just don't vectorize anything that loads constants.
    */
   if (*too_many_ubos) {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      unsigned num_srcs = nir_op_infos[alu->op].num_inputs;
      for (unsigned i = 0; i < num_srcs; i++) {
         if (nir_src_is_const(alu->src[i].src)) {
            return 0;
         }
      }
   }

   return 4;
}

/* R300 and R400 have just 32 vec4 constant register slots in fs.
 * Therefore, while its possible we will be able to compact some of
 * the constants later, we need to be extra careful with adding
 * new constants anyway.
 */
static bool
have_too_many_ubos(nir_shader *s, bool is_r500)
{
   if (s->info.stage != MESA_SHADER_FRAGMENT)
      return false;

   if (is_r500)
      return false;

   nir_foreach_variable_with_modes (var, s, nir_var_mem_ubo) {
      int ubo = var->data.driver_location;
      assert(ubo == 0);

      unsigned size = glsl_get_explicit_size(var->interface_type, false);
      if (DIV_ROUND_UP(size, 16) > 32)
         return true;
   }

   return false;
}

static bool
set_speculate(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *_)
{
   if (intr->intrinsic == nir_intrinsic_load_ubo_vec4) {
      nir_intrinsic_set_access(intr, nir_intrinsic_access(intr) | ACCESS_CAN_SPECULATE);
      return true;
   }
   return false;
}

static bool
remove_clip_vertex(nir_builder *b, nir_instr *instr, UNUSED void *_)
{
   if (instr->type != nir_instr_type_deref)
      return false;
   nir_deref_instr *deref = nir_instr_as_deref(instr);
   if (deref->deref_type == nir_deref_type_var &&
       deref->var->data.mode == nir_var_shader_out &&
       deref->var->data.location == VARYING_SLOT_CLIP_VERTEX) {
       nir_foreach_use_safe(src, &deref->def) {
          nir_instr_remove(nir_src_parent_instr(src));
       }
       nir_instr_remove(instr);
       return true;
   }
   return false;
}

static void
r300_optimize_nir(struct nir_shader *s, struct pipe_screen *screen)
{
   bool is_r500 = r300_screen(screen)->caps.is_r500;

   bool progress;
   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      if (is_r500) {
         NIR_PASS_V(s, r300_transform_fs_trig_input);
      }
   } else {
      if (r300_screen(screen)->caps.has_tcl) {
         if (r300_screen(screen)->caps.is_r500) {
            /* Only nine should set both NTT shader name and
             * use_legacy_math_rules and D3D9 already mandates
             * the proper range for the trigonometric inputs.
             */
            if (!s->info.use_legacy_math_rules || !(s->info.name && !strcmp("TTN", s->info.name))) {
               NIR_PASS_V(s, r300_transform_vs_trig_input);
            }
         } else {
            if (r300_screen(screen)->caps.is_r400) {
               NIR_PASS_V(s, r300_transform_vs_trig_input);
            }
         }

         /* There is no HW support for gl_ClipVertex, so we just remove it early. */
         if (nir_shader_instructions_pass(s, remove_clip_vertex,
                                          nir_metadata_control_flow, NULL)) {
            unsigned clip_vertex_location = 0;
            nir_foreach_variable_with_modes(var, s, nir_var_shader_out) {
               if (var->data.location == VARYING_SLOT_CLIP_VERTEX) {
                  clip_vertex_location = var->data.driver_location;
               }
            }
            nir_foreach_variable_with_modes(var, s, nir_var_shader_out) {
               if (var->data.driver_location > clip_vertex_location) {
                  var->data.driver_location--;
               }
            }
            NIR_PASS_V(s, nir_remove_dead_variables, nir_var_shader_out, NULL);
            fprintf(stderr, "r300: no HW support for clip vertex, expect misrendering.\n");
            fprintf(stderr, "r300: software emulation can be enabled with RADEON_DEBUG=notcl.\n");
         }
      }
   }

   do {
      progress = false;

      NIR_PASS_V(s, nir_lower_vars_to_ssa);

      NIR_PASS(progress, s, nir_copy_prop);
      NIR_PASS(progress, s, r300_nir_lower_flrp);
      NIR_PASS(progress, s, nir_opt_algebraic);
      if (s->info.stage == MESA_SHADER_VERTEX) {
         if (!is_r500)
            NIR_PASS(progress, s, r300_nir_lower_bool_to_float);
         NIR_PASS(progress, s, r300_nir_fuse_fround_d3d9);
      }
      NIR_PASS(progress, s, nir_opt_constant_folding);
      NIR_PASS(progress, s, nir_opt_remove_phis);
      NIR_PASS(progress, s, nir_opt_conditional_discard);
      NIR_PASS(progress, s, nir_opt_dce);
      NIR_PASS(progress, s, nir_opt_dead_cf);
      NIR_PASS(progress, s, nir_opt_cse);
      NIR_PASS(progress, s, nir_opt_find_array_copies);
      NIR_PASS(progress, s, nir_opt_copy_prop_vars);
      NIR_PASS(progress, s, nir_opt_dead_write_vars);

      NIR_PASS(progress, s, nir_opt_if, nir_opt_if_optimize_phi_true_false);
      if (is_r500)
         nir_shader_intrinsics_pass(s, set_speculate, nir_metadata_control_flow, NULL);
      NIR_PASS(progress, s, nir_opt_peephole_select, is_r500 ? 8 : ~0, true, true);
      if (s->info.stage == MESA_SHADER_FRAGMENT) {
         NIR_PASS(progress, s, r300_nir_lower_bool_to_float_fs);
      }
      NIR_PASS(progress, s, nir_opt_algebraic);
      NIR_PASS(progress, s, nir_opt_constant_folding);
      NIR_PASS(progress, s, nir_opt_shrink_stores, true);
      NIR_PASS(progress, s, nir_opt_shrink_vectors, false);
      NIR_PASS(progress, s, nir_opt_loop);

      bool too_many_ubos = have_too_many_ubos(s, is_r500);
      NIR_PASS(progress, s, nir_opt_vectorize, r300_should_vectorize_instr, &too_many_ubos);
      NIR_PASS(progress, s, nir_opt_undef);
      if (!progress)
         NIR_PASS(progress, s, nir_lower_undef_to_zero);
      NIR_PASS(progress, s, nir_opt_loop_unroll);

      /* Try to fold addressing math into ubo_vec4's base to avoid load_consts
       * and ALU ops for it.
       */
      nir_opt_offsets_options offset_options = {
         .ubo_vec4_max = 255,

         /* No const offset in TGSI for shared accesses. */
         .shared_max = 0,

         /* unused intrinsics */
         .uniform_max = 0,
         .buffer_max = 0,
      };

      NIR_PASS(progress, s, nir_opt_offsets, &offset_options);
   } while (progress);

   NIR_PASS_V(s, nir_lower_var_copies);
   NIR_PASS(progress, s, nir_remove_dead_variables, nir_var_function_temp, NULL);
}

static char *
r300_check_control_flow(nir_shader *s)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(s);
   nir_block *first = nir_start_block(impl);
   nir_cf_node *next = nir_cf_node_next(&first->cf_node);

   if (next) {
      switch (next->type) {
      case nir_cf_node_if:
         return "If/then statements not supported by R300/R400 shaders, should have been "
                "flattened by peephole_select.";
      case nir_cf_node_loop:
         return "Looping not supported R300/R400 shaders, all loops must be statically "
                "unrollable.";
      default:
         return "Unknown control flow type";
      }
   }

   return NULL;
}

char *
r300_finalize_nir(struct pipe_screen *pscreen, void *nir)
{
   nir_shader *s = nir;

   r300_optimize_nir(s, pscreen);

   /* st_program.c's parameter list optimization requires that future nir
    * variants don't reallocate the uniform storage, so we have to remove
    * uniforms that occupy storage.  But we don't want to remove samplers,
    * because they're needed for YUV variant lowering.
    */
   nir_remove_dead_derefs(s);
   nir_foreach_uniform_variable_safe (var, s) {
      if (var->data.mode == nir_var_uniform &&
          (glsl_type_get_image_count(var->type) || glsl_type_get_sampler_count(var->type)))
         continue;

      exec_node_remove(&var->node);
   }
   nir_validate_shader(s, "after uniform var removal");

   nir_sweep(s);

   if (!r300_screen(pscreen)->caps.is_r500 &&
       (r300_screen(pscreen)->caps.has_tcl || s->info.stage == MESA_SHADER_FRAGMENT)) {
      char *msg = r300_check_control_flow(s);
      if (msg)
         return strdup(msg);
   }

   return NULL;
}
