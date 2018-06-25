/*
 * Copyright © 2015 Intel Corporation
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
#include "nir_builder.h"
#include "nir_control_flow.h"
#include "nir_vla.h"

static bool inline_function_impl(nir_function_impl *impl, struct set *inlined);

static bool
inline_functions_block(nir_block *block, nir_builder *b,
                       struct set *inlined)
{
   bool progress = false;
   /* This is tricky.  We're iterating over instructions in a block but, as
    * we go, the block and its instruction list are being split into
    * pieces.  However, this *should* be safe since foreach_safe always
    * stashes the next thing in the iteration.  That next thing will
    * properly get moved to the next block when it gets split, and we
    * continue iterating there.
    */
   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_call)
         continue;

      progress = true;

      nir_call_instr *call = nir_instr_as_call(instr);
      assert(call->callee->impl);

      inline_function_impl(call->callee->impl, inlined);

      nir_function_impl *callee_copy =
         nir_function_impl_clone(call->callee->impl);
      callee_copy->function = call->callee;

      exec_list_append(&b->impl->locals, &callee_copy->locals);
      exec_list_append(&b->impl->registers, &callee_copy->registers);

      b->cursor = nir_before_instr(&call->instr);

      /* Rewrite all of the uses of the callee's parameters to use the call
       * instructions sources.  In order to ensure that the "load" happens
       * here and not later (for register sources), we make sure to convert it
       * to an SSA value first.
       */
      const unsigned num_params = call->num_params;
      NIR_VLA(nir_ssa_def *, params, num_params);
      for (unsigned i = 0; i < num_params; i++) {
         params[i] = nir_ssa_for_src(b, call->params[i],
                                     call->callee->params[i].num_components);
      }

      nir_foreach_block(block, callee_copy) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *load = nir_instr_as_intrinsic(instr);
            if (load->intrinsic != nir_intrinsic_load_param)
               continue;

            unsigned param_idx = nir_intrinsic_param_idx(load);
            assert(param_idx < num_params);
            assert(load->dest.is_ssa);
            nir_ssa_def_rewrite_uses(&load->dest.ssa,
                                     nir_src_for_ssa(params[param_idx]));

            /* Remove any left-over load_param intrinsics because they're soon
             * to be in another function and therefore no longer valid.
             */
            nir_instr_remove(&load->instr);
         }
      }

      /* Pluck the body out of the function and place it here */
      nir_cf_list body;
      nir_cf_list_extract(&body, &callee_copy->body);
      nir_cf_reinsert(&body, b->cursor);

      nir_instr_remove(&call->instr);
   }

   return progress;
}

static bool
inline_function_impl(nir_function_impl *impl, struct set *inlined)
{
   if (_mesa_set_search(inlined, impl))
      return false; /* Already inlined */

   nir_builder b;
   nir_builder_init(&b, impl);

   bool progress = false;
   nir_foreach_block_safe(block, impl) {
      progress |= inline_functions_block(block, &b, inlined);
   }

   if (progress) {
      /* SSA and register indices are completely messed up now */
      nir_index_ssa_defs(impl);
      nir_index_local_regs(impl);

      nir_metadata_preserve(impl, nir_metadata_none);
   }

   _mesa_set_add(inlined, impl);

   return progress;
}

bool
nir_inline_functions(nir_shader *shader)
{
   struct set *inlined = _mesa_set_create(NULL, _mesa_hash_pointer,
                                          _mesa_key_pointer_equal);
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress = inline_function_impl(function->impl, inlined) || progress;
   }

   _mesa_set_destroy(inlined, NULL);

   return progress;
}
