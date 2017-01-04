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

static bool inline_function_impl(nir_function_impl *impl, struct set *inlined);

static void
convert_deref_to_param_deref(nir_instr *instr, nir_deref_var **deref,
                             nir_call_instr *call)
{
   /* This isn't a parameter, just return the deref */
   if ((*deref)->var->data.mode != nir_var_param)
      return;

   int param_idx = (*deref)->var->data.location;

   nir_deref_var *call_deref;
   if (param_idx >= 0) {
      assert(param_idx < call->callee->num_params);
      call_deref = call->params[param_idx];
   } else {
      call_deref = call->return_deref;
   }
   assert(call_deref);

   /* Now we make a new deref by concatenating the deref in the call's
    * parameter with the deref we were given.
    */
   nir_deref_var *new_deref = nir_deref_var_clone(call_deref, instr);
   nir_deref *new_tail = nir_deref_tail(&new_deref->deref);
   new_tail->child = (*deref)->deref.child;
   ralloc_steal(new_tail, new_tail->child);
   *deref = new_deref;
}

static void
rewrite_param_derefs(nir_instr *instr, nir_call_instr *call)
{
   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      for (unsigned i = 0;
           i < nir_intrinsic_infos[intrin->intrinsic].num_variables; i++) {
         convert_deref_to_param_deref(instr, &intrin->variables[i], call);
      }
      break;
   }

   case nir_instr_type_tex: {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      if (tex->texture)
         convert_deref_to_param_deref(&tex->instr, &tex->texture, call);
      if (tex->sampler)
         convert_deref_to_param_deref(&tex->instr, &tex->sampler, call);
      break;
   }

   default:
      break; /* Nothing else has derefs */
   }
}

static void
lower_param_to_local(nir_variable *param, nir_function_impl *impl, bool write)
{
   if (param->data.mode != nir_var_param)
      return;

   nir_parameter_type param_type;
   if (param->data.location >= 0) {
      assert(param->data.location < impl->num_params);
      param_type = impl->function->params[param->data.location].param_type;
   } else {
      /* Return variable */
      param_type = nir_parameter_out;
   }

   if ((write && param_type == nir_parameter_in) ||
       (!write && param_type == nir_parameter_out)) {
      /* In this case, we need a shadow copy.  Turn it into a local */
      param->data.mode = nir_var_local;
      exec_list_push_tail(&impl->locals, &param->node);
   }
}

static bool
lower_params_to_locals_block(nir_block *block, nir_function_impl *impl)
{
   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      switch (intrin->intrinsic) {
      case nir_intrinsic_store_var:
         lower_param_to_local(intrin->variables[0]->var, impl, true);
         break;

      case nir_intrinsic_copy_var:
         lower_param_to_local(intrin->variables[0]->var, impl, true);
         lower_param_to_local(intrin->variables[1]->var, impl, false);
         break;

      case nir_intrinsic_load_var:
         /* All other intrinsics which access variables (image_load_store)
          * do so in a read-only fasion.
          */
         for (unsigned i = 0;
              i < nir_intrinsic_infos[intrin->intrinsic].num_variables; i++) {
            lower_param_to_local(intrin->variables[i]->var, impl, false);
         }
         break;

      default:
         continue;
      }
   }

   return true;
}

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

      /* Add copies of all in parameters */
      assert(call->num_params == callee_copy->num_params);

      exec_list_append(&b->impl->locals, &callee_copy->locals);
      exec_list_append(&b->impl->registers, &callee_copy->registers);

      b->cursor = nir_before_instr(&call->instr);

      /* We now need to tie the two functions together using the
       * parameters.  There are two ways we do this: One is to turn the
       * parameter into a local variable and do a shadow-copy.  The other
       * is to treat the parameter as a "proxy" and rewrite derefs to use
       * the actual variable that comes from the call instruction.  We
       * implement both schemes.  The first is needed in the case where we
       * have an in parameter that we write or similar.  The second case is
       * needed for handling things such as images and uniforms properly.
       */

      /* Figure out when we need to lower to a shadow local */
      nir_foreach_block(block, callee_copy) {
         lower_params_to_locals_block(block, callee_copy);
      }

      for (unsigned i = 0; i < callee_copy->num_params; i++) {
         nir_variable *param = callee_copy->params[i];

         if (param->data.mode == nir_var_local &&
             call->callee->params[i].param_type != nir_parameter_out) {
            nir_copy_deref_var(b, nir_deref_var_create(b->shader, param),
                                  call->params[i]);
         }
      }

      nir_foreach_block(block, callee_copy) {
         nir_foreach_instr(instr, block)
            rewrite_param_derefs(instr, call);
      }

      /* Pluck the body out of the function and place it here */
      nir_cf_list body;
      nir_cf_list_extract(&body, &callee_copy->body);
      nir_cf_reinsert(&body, b->cursor);

      b->cursor = nir_before_instr(&call->instr);

      /* Add copies of all out parameters and the return */
      assert(call->num_params == callee_copy->num_params);
      for (unsigned i = 0; i < callee_copy->num_params; i++) {
         nir_variable *param = callee_copy->params[i];

         if (param->data.mode == nir_var_local &&
             call->callee->params[i].param_type != nir_parameter_in) {
            nir_copy_deref_var(b, call->params[i],
                                  nir_deref_var_create(b->shader, param));
         }
      }
      if (!glsl_type_is_void(call->callee->return_type) &&
          callee_copy->return_var->data.mode == nir_var_local) {
         nir_copy_deref_var(b, call->return_deref,
                               nir_deref_var_create(b->shader,
                                                    callee_copy->return_var));
      }

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
