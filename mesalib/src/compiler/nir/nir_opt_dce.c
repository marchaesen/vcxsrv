/*
 * Copyright © 2014 Intel Corporation
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
 *
 * Authors:
 *    Connor Abbott (cwabbott0@gmail.com)
 *
 */

#include "nir.h"

/* SSA-based mark-and-sweep dead code elimination */

typedef struct {
   struct exec_node node;
   nir_instr *instr;
} worklist_elem;

static void
worklist_push(struct exec_list *worklist, nir_instr *instr)
{
   worklist_elem *elem = ralloc(worklist, worklist_elem);
   elem->instr = instr;
   instr->pass_flags = 1;
   exec_list_push_tail(worklist, &elem->node);
}

static nir_instr *
worklist_pop(struct exec_list *worklist)
{
   struct exec_node *node = exec_list_pop_head(worklist);
   worklist_elem *elem = exec_node_data(worklist_elem, node, node);
   return elem->instr;
}

static bool
mark_live_cb(nir_src *src, void *_state)
{
   struct exec_list *worklist = (struct exec_list *) _state;

   if (src->is_ssa && !src->ssa->parent_instr->pass_flags) {
      worklist_push(worklist, src->ssa->parent_instr);
   }

   return true;
}

static void
init_instr(nir_instr *instr, struct exec_list *worklist)
{
   nir_alu_instr *alu_instr;
   nir_intrinsic_instr *intrin_instr;
   nir_tex_instr *tex_instr;

   /* We use the pass_flags to store the live/dead information.  In DCE, we
    * just treat it as a zero/non-zero boolean for whether or not the
    * instruction is live.
    */
   instr->pass_flags = 0;

   switch (instr->type) {
   case nir_instr_type_call:
   case nir_instr_type_jump:
      worklist_push(worklist, instr);
      break;

   case nir_instr_type_alu:
      alu_instr = nir_instr_as_alu(instr);
      if (!alu_instr->dest.dest.is_ssa)
         worklist_push(worklist, instr);
      break;

   case nir_instr_type_intrinsic:
      intrin_instr = nir_instr_as_intrinsic(instr);
      if (nir_intrinsic_infos[intrin_instr->intrinsic].flags &
          NIR_INTRINSIC_CAN_ELIMINATE) {
         if (nir_intrinsic_infos[intrin_instr->intrinsic].has_dest &&
             !intrin_instr->dest.is_ssa) {
            worklist_push(worklist, instr);
         }
      } else {
         worklist_push(worklist, instr);
      }
      break;

   case nir_instr_type_tex:
      tex_instr = nir_instr_as_tex(instr);
      if (!tex_instr->dest.is_ssa)
         worklist_push(worklist, instr);
      break;

   default:
      break;
   }
}

static bool
init_block(nir_block *block, struct exec_list *worklist)
{
   nir_foreach_instr(instr, block)
      init_instr(instr, worklist);

   nir_if *following_if = nir_block_get_following_if(block);
   if (following_if) {
      if (following_if->condition.is_ssa &&
          !following_if->condition.ssa->parent_instr->pass_flags)
         worklist_push(worklist, following_if->condition.ssa->parent_instr);
   }

   return true;
}

static bool
nir_opt_dce_impl(nir_function_impl *impl)
{
   struct exec_list *worklist = ralloc(NULL, struct exec_list);
   exec_list_make_empty(worklist);

   nir_foreach_block(block, impl) {
      init_block(block, worklist);
   }

   while (!exec_list_is_empty(worklist)) {
      nir_instr *instr = worklist_pop(worklist);
      nir_foreach_src(instr, mark_live_cb, worklist);
   }

   ralloc_free(worklist);

   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (!instr->pass_flags) {
            nir_instr_remove(instr);
            progress = true;
         }
      }
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);

   return progress;
}

bool
nir_opt_dce(nir_shader *shader)
{
   bool progress = false;
   nir_foreach_function(function, shader) {
      if (function->impl && nir_opt_dce_impl(function->impl))
         progress = true;
   }

   return progress;
}
