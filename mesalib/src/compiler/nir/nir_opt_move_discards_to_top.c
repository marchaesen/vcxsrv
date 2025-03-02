/*
 * Copyright © 2018 Intel Corporation
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

#include "util/u_dynarray.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_control_flow.h"

#define MAX_DISCARDS               254
#define MOVE_INSTR_FLAG(i)         ((i) + 1)
#define STOP_PROCESSING_INSTR_FLAG 255

struct move_discard_state {
   struct util_dynarray worklist;
   unsigned discard_id;
};

/** Check recursively if the source can be moved to the top of the shader.
 *  Sets instr->pass_flags to MOVE_INSTR_FLAG and adds the instr
 *  to the given worklist
 */
static bool
add_src_to_worklist(nir_src *src, void *state_)
{
   struct move_discard_state *state = state_;
   nir_instr *instr = src->ssa->parent_instr;
   if (instr->pass_flags)
      return true;

   /* Phi instructions can't be moved at all.  Also, if we're dependent on
    * a phi then we are dependent on some other bit of control flow and
    * it's hard to figure out the proper condition.
    */
   if (instr->type == nir_instr_type_phi)
      return false;

   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      /* Increasing the set of active invocations is safe for these intrinsics, which is
       * all that moving it to the top does. This is because the read from inactive
       * invocations is undefined.
       */
      case nir_intrinsic_quad_swizzle_amd:
         /* If FI=0, then these intrinsics return 0 for inactive invocations. */
         if (!nir_intrinsic_fetch_inactive(intrin))
            return false;
         FALLTHROUGH;
      case nir_intrinsic_ddx:
      case nir_intrinsic_ddy:
      case nir_intrinsic_ddx_fine:
      case nir_intrinsic_ddy_fine:
      case nir_intrinsic_ddx_coarse:
      case nir_intrinsic_ddy_coarse:
      case nir_intrinsic_quad_broadcast:
      case nir_intrinsic_quad_swap_horizontal:
      case nir_intrinsic_quad_swap_vertical:
      case nir_intrinsic_quad_swap_diagonal:
         break;
      default:
         if (!nir_intrinsic_can_reorder(intrin))
            return false;
         break;
      }
   }

   /* Set pass_flags and remember the instruction to add it's own sources and for potential
    * cleanup.
    */
   instr->pass_flags = MOVE_INSTR_FLAG(state->discard_id);
   util_dynarray_append(&state->worklist, nir_instr *, instr);

   return true;
}

/** Try to mark a discard or demote instruction for moving
 *
 * This function does two things.  One is that it searches through the
 * dependency chain to see if this discard is an instruction that we can move
 * up to the top.  Second, if the discard is one we can move, it tags the
 * discard and its dependencies (using pass_flags = 1).
 * Demote are handled the same way, except that they can still be moved up
 * when implicit derivatives are used.
 */
static void
try_move_discard(nir_intrinsic_instr *discard, unsigned *next_discard_id)
{
   /* We require the discard to be in the top level of control flow.  We
    * could, in theory, move discards that are inside ifs or loops but that
    * would be a lot more work.
    */
   if (discard->instr.block->cf_node.parent->type != nir_cf_node_function)
      return;

   if (*next_discard_id == MAX_DISCARDS)
      return;

   discard->instr.pass_flags = MOVE_INSTR_FLAG(*next_discard_id);

   /* Build the set of all instructions discard depends on to be able to
    * clear the flags in case the discard cannot be moved.
    */
   nir_instr *work_[64];
   struct move_discard_state state;
   state.discard_id = *next_discard_id;
   util_dynarray_init_from_stack(&state.worklist, work_, sizeof(work_));
   util_dynarray_append(&state.worklist, nir_instr *, &discard->instr);

   unsigned next = 0;
   bool can_move_discard = true;
   while (next < util_dynarray_num_elements(&state.worklist, nir_instr *) && can_move_discard) {
      nir_instr *instr = *util_dynarray_element(&state.worklist, nir_instr *, next);
      next++;
      /* Instead of removing instructions from the worklist, we keep them so that the
       * flags can be cleared if we fail.
       */
      can_move_discard = nir_foreach_src(instr, add_src_to_worklist, &state.worklist);
   }

   if (!can_move_discard) {
      /* Moving the discard is impossible: clear the flags */
      util_dynarray_foreach(&state.worklist, nir_instr *, instr)
         (*instr)->pass_flags = 0;
   } else {
      (*next_discard_id)++;
   }

   util_dynarray_fini(&state.worklist);
}

enum intrinsic_discard_info {
   can_move_after_demote = 1 << 0,
   can_move_after_terminate = 1 << 1,
};

static enum intrinsic_discard_info
can_move_intrinsic_after_discard(nir_intrinsic_instr *intrin)
{
   if (nir_intrinsic_can_reorder(intrin))
      return can_move_after_demote | can_move_after_terminate;

   switch (intrin->intrinsic) {
   case nir_intrinsic_is_helper_invocation:
   case nir_intrinsic_load_helper_invocation:
      return can_move_after_terminate;
   case nir_intrinsic_load_param:
   case nir_intrinsic_load_deref:
   case nir_intrinsic_decl_reg:
   case nir_intrinsic_load_reg:
   case nir_intrinsic_load_reg_indirect:
   case nir_intrinsic_as_uniform:
   case nir_intrinsic_inverse_ballot:
   case nir_intrinsic_write_invocation_amd:
   case nir_intrinsic_mbcnt_amd:
   case nir_intrinsic_atomic_counter_read:
   case nir_intrinsic_atomic_counter_read_deref:
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_load:
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_image_deref_sparse_load:
   case nir_intrinsic_image_sparse_load:
   case nir_intrinsic_bindless_image_sparse_load:
   case nir_intrinsic_image_deref_samples_identical:
   case nir_intrinsic_image_samples_identical:
   case nir_intrinsic_bindless_image_samples_identical:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_output:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_2x32:
   case nir_intrinsic_load_scratch:
   case nir_intrinsic_load_stack:
   case nir_intrinsic_load_buffer_amd:
   case nir_intrinsic_load_typed_buffer_amd:
   case nir_intrinsic_load_global_amd:
   case nir_intrinsic_load_shared2_amd:
      return can_move_after_demote | can_move_after_terminate;
   case nir_intrinsic_store_deref:
      if (!nir_deref_mode_is_in_set(nir_src_as_deref(intrin->src[0]),
                                    nir_var_shader_temp | nir_var_function_temp)) {
         break;
      }
      FALLTHROUGH;
   case nir_intrinsic_store_reg:
   case nir_intrinsic_store_reg_indirect:
   case nir_intrinsic_store_scratch:
      return can_move_after_demote | can_move_after_terminate;
   default:
      break;
   }

   if (nir_intrinsic_has_semantic(intrin, NIR_INTRINSIC_QUADGROUP))
      return can_move_after_demote;

   return 0;
}

static bool
opt_move_discards_to_top_impl(nir_function_impl *impl)
{
   bool progress = false;
   bool consider_terminates = true;
   unsigned next_discard_id = 0;

   /* Walk through the instructions and look for a discard that we can move
    * to the top of the program.  If we hit any operation along the way that
    * we cannot safely move a discard above, break out of the loop and stop
    * trying to move any more discards.
    */
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         instr->pass_flags = 0;

         switch (instr->type) {
         case nir_instr_type_alu:
         case nir_instr_type_deref:
         case nir_instr_type_load_const:
         case nir_instr_type_undef:
         case nir_instr_type_phi:
            /* These are all safe */
            continue;

         case nir_instr_type_call:
            instr->pass_flags = STOP_PROCESSING_INSTR_FLAG;
            /* We don't know what the function will do */
            goto break_all;

         case nir_instr_type_tex: {
            nir_tex_instr *tex = nir_instr_as_tex(instr);
            if (nir_tex_instr_has_implicit_derivative(tex))
               consider_terminates = false;
            continue;
         }

         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            switch (intrin->intrinsic) {
            case nir_intrinsic_terminate_if:
               if (!consider_terminates) {
                  /* assume that a shader either uses terminate or demote, but not both */
                  instr->pass_flags = STOP_PROCESSING_INSTR_FLAG;
                  goto break_all;
               }
               FALLTHROUGH;
            case nir_intrinsic_demote_if:
               try_move_discard(intrin, &next_discard_id);
               break;
            default: {
               enum intrinsic_discard_info info = can_move_intrinsic_after_discard(intrin);
               if (!(info & can_move_after_demote)) {
                  instr->pass_flags = STOP_PROCESSING_INSTR_FLAG;
                  goto break_all;
               } else if (!(info & can_move_after_terminate)) {
                  consider_terminates = false;
               }
               break;
            }
            }
            continue;
         }

         case nir_instr_type_jump: {
            nir_jump_instr *jump = nir_instr_as_jump(instr);
            /* A return would cause the discard to not get executed */
            if (jump->type == nir_jump_return) {
               instr->pass_flags = STOP_PROCESSING_INSTR_FLAG;
               goto break_all;
            }
            continue;
         }

         case nir_instr_type_parallel_copy:
            unreachable("Unhanded instruction type");
         }
      }
   }
break_all:

   if (next_discard_id == 0)
      return false;

   /* Walk the list of instructions and move the discard/demote and
    * everything it depends on to the top.  We walk the instruction list
    * here because it ensures that everything stays in its original order.
    * This provides stability for the algorithm and ensures that we don't
    * accidentally get dependencies out-of-order.
    */
   BITSET_DECLARE(cursors_valid, MAX_DISCARDS) = { 1u };
   nir_cursor cursors_[32];
   struct util_dynarray cursors;
   util_dynarray_init_from_stack(&cursors, cursors_, sizeof(cursors_));
   if (!util_dynarray_resize(&cursors, nir_cursor, next_discard_id))
      return false;

   *util_dynarray_element(&cursors, nir_cursor, 0) = nir_before_impl(impl);

   nir_foreach_block(block, impl) {
      bool stop = false;
      nir_foreach_instr_safe(instr, block) {
         if (instr->pass_flags == 0)
            continue;

         if (instr->pass_flags == STOP_PROCESSING_INSTR_FLAG) {
            stop = true;
            break;
         }

         unsigned index = instr->pass_flags - 1;
         nir_cursor *cursor = util_dynarray_element(&cursors, nir_cursor, index);
         if (!BITSET_TEST(cursors_valid, index)) {
            unsigned prev_idx = BITSET_LAST_BIT_BEFORE(cursors_valid, index) - 1;
            *cursor = *util_dynarray_element(&cursors, nir_cursor, prev_idx);
            BITSET_SET(cursors_valid, index);
         }

         progress |= nir_instr_move(*cursor, instr);
         *cursor = nir_after_instr(instr);
      }
      if (stop)
         break;
   }

   util_dynarray_fini(&cursors);
   return progress;
}

/* This optimization only operates on terminate_if/demote_if so
 * nir_opt_peephole_select and nir_lower_discard_or_demote
 * should have been called before.
 */
bool
nir_opt_move_discards_to_top(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   bool progress = false;

   if (!shader->info.fs.uses_discard)
      return false;

   nir_foreach_function_impl(impl, shader) {
      if (opt_move_discards_to_top_impl(impl)) {
         progress = nir_progress(true, impl, nir_metadata_control_flow);
      }
   }

   return progress;
}
