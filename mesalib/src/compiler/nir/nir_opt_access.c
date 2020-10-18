/*
 * Copyright © 2019 Valve Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "nir.h"

/* This pass optimizes GL access qualifiers. So far it does two things:
 *
 * - Infer readonly when it's missing.
 * - Infer ACCESS_CAN_REORDER when the following are true:
 *   - Either there are no writes, or ACCESS_NON_WRITEABLE and ACCESS_RESTRICT
 *     are both set. In either case there are no writes to the underlying
 *     memory.
 *   - If ACCESS_COHERENT is set, then there must be no memory barriers
 *     involving the access. Coherent accesses may return different results
 *     before and after barriers.
 *   - ACCESS_VOLATILE is not set.
 *
 * If these conditions are true, then image and buffer reads may be treated as
 * if they were uniform buffer reads, i.e. they may be arbitrarily moved,
 * combined, rematerialized etc.
 */

struct access_state {
   struct set *vars_written;
   bool images_written;
   bool buffers_written;
   bool image_barriers;
   bool buffer_barriers;
};

static void
gather_intrinsic(struct access_state *state, nir_intrinsic_instr *instr)
{
   nir_variable *var;
   switch (instr->intrinsic) {
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic_add:
   case nir_intrinsic_image_deref_atomic_imin:
   case nir_intrinsic_image_deref_atomic_umin:
   case nir_intrinsic_image_deref_atomic_imax:
   case nir_intrinsic_image_deref_atomic_umax:
   case nir_intrinsic_image_deref_atomic_and:
   case nir_intrinsic_image_deref_atomic_or:
   case nir_intrinsic_image_deref_atomic_xor:
   case nir_intrinsic_image_deref_atomic_exchange:
   case nir_intrinsic_image_deref_atomic_comp_swap:
   case nir_intrinsic_image_deref_atomic_fadd:
      var = nir_intrinsic_get_var(instr, 0);

      /* In OpenGL, buffer images use normal buffer objects, whereas other
       * image types use textures which cannot alias with buffer objects.
       * Therefore we have to group buffer samplers together with SSBO's.
       */
      if (glsl_get_sampler_dim(glsl_without_array(var->type)) ==
          GLSL_SAMPLER_DIM_BUF)
         state->buffers_written = true;
      else
         state->images_written = true;

      if (var->data.mode == nir_var_uniform)
         _mesa_set_add(state->vars_written, var);
      break;

   case nir_intrinsic_bindless_image_store:
   case nir_intrinsic_bindless_image_atomic_add:
   case nir_intrinsic_bindless_image_atomic_imin:
   case nir_intrinsic_bindless_image_atomic_umin:
   case nir_intrinsic_bindless_image_atomic_imax:
   case nir_intrinsic_bindless_image_atomic_umax:
   case nir_intrinsic_bindless_image_atomic_and:
   case nir_intrinsic_bindless_image_atomic_or:
   case nir_intrinsic_bindless_image_atomic_xor:
   case nir_intrinsic_bindless_image_atomic_exchange:
   case nir_intrinsic_bindless_image_atomic_comp_swap:
   case nir_intrinsic_bindless_image_atomic_fadd:
      if (nir_intrinsic_image_dim(instr) == GLSL_SAMPLER_DIM_BUF)
         state->buffers_written = true;
      else
         state->images_written = true;
      break;

   case nir_intrinsic_store_deref:
   case nir_intrinsic_deref_atomic_add:
   case nir_intrinsic_deref_atomic_imin:
   case nir_intrinsic_deref_atomic_umin:
   case nir_intrinsic_deref_atomic_imax:
   case nir_intrinsic_deref_atomic_umax:
   case nir_intrinsic_deref_atomic_and:
   case nir_intrinsic_deref_atomic_or:
   case nir_intrinsic_deref_atomic_xor:
   case nir_intrinsic_deref_atomic_exchange:
   case nir_intrinsic_deref_atomic_comp_swap:
   case nir_intrinsic_deref_atomic_fadd:
   case nir_intrinsic_deref_atomic_fmin:
   case nir_intrinsic_deref_atomic_fmax:
   case nir_intrinsic_deref_atomic_fcomp_swap:
      var = nir_intrinsic_get_var(instr, 0);
      if (var->data.mode != nir_var_mem_ssbo)
         break;

      _mesa_set_add(state->vars_written, var);
      state->buffers_written = true;
      break;

   case nir_intrinsic_memory_barrier:
      state->buffer_barriers = true;
      state->image_barriers = true;
      break;

   case nir_intrinsic_memory_barrier_buffer:
      state->buffer_barriers = true;
      break;

   case nir_intrinsic_memory_barrier_image:
      state->image_barriers = true;
      break;

   case nir_intrinsic_scoped_barrier:
      /* TODO: Could be more granular if we had nir_var_mem_image. */
      if (nir_intrinsic_memory_modes(instr) & (nir_var_mem_ubo |
                                               nir_var_mem_ssbo |
                                               nir_var_uniform |
                                               nir_var_mem_global)) {
         state->buffer_barriers = true;
         state->image_barriers = true;
      }
      break;

   default:
      break;
   }
}

static bool
process_variable(struct access_state *state, nir_variable *var)
{
   if (var->data.mode != nir_var_mem_ssbo &&
       !(var->data.mode == nir_var_uniform &&
         glsl_type_is_image(var->type)))
      return false;

   /* Ignore variables we've already marked */
   if (var->data.access & ACCESS_CAN_REORDER)
      return false;

   if (!(var->data.access & ACCESS_NON_WRITEABLE) &&
       !_mesa_set_search(state->vars_written, var)) {
      var->data.access |= ACCESS_NON_WRITEABLE;
      return true;
   }

   return false;
}

static bool
can_reorder(struct access_state *state, enum gl_access_qualifier access,
            bool is_buffer, bool is_ssbo)
{
   bool is_any_written = is_buffer ? state->buffers_written :
      state->images_written;

   /* Can we guarantee that the underlying memory is never written? */
   if (!is_any_written ||
       ((access & ACCESS_NON_WRITEABLE) &&
        (access & ACCESS_RESTRICT))) {
      /* Note: memoryBarrierBuffer() is only guaranteed to flush buffer
       * variables and not imageBuffer's, so we only consider the GL-level
       * type here.
       */
      bool is_any_barrier = is_ssbo ?
         state->buffer_barriers : state->image_barriers;

      return (!is_any_barrier || !(access & ACCESS_COHERENT)) &&
          !(access & ACCESS_VOLATILE);
   }

   return false;
}

static bool
process_intrinsic(struct access_state *state, nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_bindless_image_load:
      if (nir_intrinsic_access(instr) & ACCESS_CAN_REORDER)
         return false;

      /* We have less information about bindless intrinsics, since we can't
       * always trace uses back to the variable. Don't try and infer if it's
       * read-only, unless there are no image writes at all.
       */
      bool progress = false;
      bool is_buffer =
         nir_intrinsic_image_dim(instr) == GLSL_SAMPLER_DIM_BUF;

      bool is_any_written =
         is_buffer ? state->buffers_written : state->images_written;

      if (!(nir_intrinsic_access(instr) & ACCESS_NON_WRITEABLE) &&
          !is_any_written) {
         progress = true;
         nir_intrinsic_set_access(instr,
                                  nir_intrinsic_access(instr) |
                                  ACCESS_NON_WRITEABLE);
      }

      if (can_reorder(state, nir_intrinsic_access(instr), is_buffer, false)) {
         progress = true;
         nir_intrinsic_set_access(instr,
                                  nir_intrinsic_access(instr) |
                                  ACCESS_CAN_REORDER);
      }

      return progress;

   case nir_intrinsic_load_deref:
   case nir_intrinsic_image_deref_load: {
      nir_variable *var = nir_intrinsic_get_var(instr, 0);

      if (instr->intrinsic == nir_intrinsic_load_deref &&
          var->data.mode != nir_var_mem_ssbo)
         return false;

      if (nir_intrinsic_access(instr) & ACCESS_CAN_REORDER)
         return false;

      bool progress = false;

      /* Check if we were able to mark the whole variable non-writeable */
      if (!(nir_intrinsic_access(instr) & ACCESS_NON_WRITEABLE) &&
          var->data.access & ACCESS_NON_WRITEABLE) {
         progress = true;
         nir_intrinsic_set_access(instr,
                                  nir_intrinsic_access(instr) |
                                  ACCESS_NON_WRITEABLE);
      }

      bool is_ssbo = var->data.mode == nir_var_mem_ssbo;

      bool is_buffer = is_ssbo ||
         glsl_get_sampler_dim(glsl_without_array(var->type)) == GLSL_SAMPLER_DIM_BUF;

      if (can_reorder(state, nir_intrinsic_access(instr), is_buffer, is_ssbo)) {
         progress = true;
         nir_intrinsic_set_access(instr,
                                  nir_intrinsic_access(instr) |
                                  ACCESS_CAN_REORDER);
      }

      return progress;
   }

   default:
      return false;
   }
}

static bool
opt_access_impl(struct access_state *state,
                nir_function_impl *impl)
{
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_intrinsic)
            progress |= process_intrinsic(state,
                                          nir_instr_as_intrinsic(instr));
      }
   }

   if (progress) {
      nir_metadata_preserve(impl,
                            nir_metadata_block_index |
                            nir_metadata_dominance |
                            nir_metadata_live_ssa_defs |
                            nir_metadata_loop_analysis);
   }


   return progress;
}

bool
nir_opt_access(nir_shader *shader)
{
   struct access_state state = {
      .vars_written = _mesa_pointer_set_create(NULL),
   };

   bool var_progress = false;
   bool progress = false;

   nir_foreach_function(func, shader) {
      if (func->impl) {
         nir_foreach_block(block, func->impl) {
            nir_foreach_instr(instr, block) {
               if (instr->type == nir_instr_type_intrinsic)
                  gather_intrinsic(&state, nir_instr_as_intrinsic(instr));
            }
         }
      }
   }

   nir_foreach_variable_with_modes(var, shader, nir_var_uniform |
                                                nir_var_mem_ubo |
                                                nir_var_mem_ssbo)
      var_progress |= process_variable(&state, var);

   nir_foreach_function(func, shader) {
      if (func->impl) {
         progress |= opt_access_impl(&state, func->impl);

         /* If we make a change to the uniforms, update all the impls. */
         if (var_progress) {
            nir_metadata_preserve(func->impl,
                                  nir_metadata_block_index |
                                  nir_metadata_dominance |
                                  nir_metadata_live_ssa_defs |
                                  nir_metadata_loop_analysis);
         }
      }
   }

   progress |= var_progress;

   _mesa_set_destroy(state.vars_written, NULL);
   return progress;
}
