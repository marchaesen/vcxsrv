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

/*
 * Implements a pass that lowers output and/or input variables to a
 * temporary plus an output variable with a single copy at each exit
 * point of the shader and/or an input variable with a single copy
 * at the entrance point of the shader.  This way the output variable
 * is only ever written once and/or input is only read once, and there
 * are no indirect outut/input accesses.
 */

#include "nir.h"

struct lower_io_state {
   nir_shader *shader;
   nir_function_impl *entrypoint;
   struct exec_list old_outputs;
   struct exec_list old_inputs;
};

static void
emit_copies(nir_cursor cursor, nir_shader *shader, struct exec_list *new_vars,
          struct exec_list *old_vars)
{
   assert(exec_list_length(new_vars) == exec_list_length(old_vars));

   foreach_two_lists(new_node, new_vars, old_node, old_vars) {
      nir_variable *newv = exec_node_data(nir_variable, new_node, node);
      nir_variable *temp = exec_node_data(nir_variable, old_node, node);

      /* No need to copy the contents of a non-fb_fetch_output output variable
       * to the temporary allocated for it, since its initial value is
       * undefined.
       */
      if (temp->data.mode == nir_var_shader_out &&
          !temp->data.fb_fetch_output)
         continue;

      /* Can't copy the contents of the temporary back to a read-only
       * interface variable.  The value of the temporary won't have been
       * modified by the shader anyway.
       */
      if (newv->data.read_only)
         continue;

      nir_intrinsic_instr *copy =
         nir_intrinsic_instr_create(shader, nir_intrinsic_copy_var);
      copy->variables[0] = nir_deref_var_create(copy, newv);
      copy->variables[1] = nir_deref_var_create(copy, temp);

      nir_instr_insert(cursor, &copy->instr);
   }
}

static void
emit_output_copies_impl(struct lower_io_state *state, nir_function_impl *impl)
{
   if (state->shader->stage == MESA_SHADER_GEOMETRY) {
      /* For geometry shaders, we have to emit the output copies right
       * before each EmitVertex call.
       */
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic == nir_intrinsic_emit_vertex) {
               nir_cursor cursor = nir_before_instr(&intrin->instr);
               emit_copies(cursor, state->shader, &state->shader->outputs,
                           &state->old_outputs);
            }
         }
      }
   } else if (impl == state->entrypoint) {
      nir_cursor cursor = nir_before_block(nir_start_block(impl));
      emit_copies(cursor, state->shader, &state->old_outputs,
                  &state->shader->outputs);

      /* For all other shader types, we need to do the copies right before
       * the jumps to the end block.
       */
      struct set_entry *block_entry;
      set_foreach(impl->end_block->predecessors, block_entry) {
         struct nir_block *block = (void *)block_entry->key;
         nir_cursor cursor = nir_after_block_before_jump(block);
         emit_copies(cursor, state->shader, &state->shader->outputs,
                     &state->old_outputs);
      }
   }
}

static void
emit_input_copies_impl(struct lower_io_state *state, nir_function_impl *impl)
{
   if (impl == state->entrypoint) {
      nir_cursor cursor = nir_before_block(nir_start_block(impl));
      emit_copies(cursor, state->shader, &state->old_inputs,
                  &state->shader->inputs);
   }
}

static nir_variable *
create_shadow_temp(struct lower_io_state *state, nir_variable *var)
{
   nir_variable *nvar = ralloc(state->shader, nir_variable);
   memcpy(nvar, var, sizeof *nvar);

   /* The original is now the temporary */
   nir_variable *temp = var;

   /* Reparent the name to the new variable */
   ralloc_steal(nvar, nvar->name);

   assert(nvar->constant_initializer == NULL);

   /* Give the original a new name with @<mode>-temp appended */
   const char *mode = (temp->data.mode == nir_var_shader_in) ? "in" : "out";
   temp->name = ralloc_asprintf(var, "%s@%s-temp", mode, nvar->name);
   temp->data.mode = nir_var_global;
   temp->data.read_only = false;
   temp->data.fb_fetch_output = false;

   return nvar;
}

void
nir_lower_io_to_temporaries(nir_shader *shader, nir_function_impl *entrypoint,
                            bool outputs, bool inputs)
{
   struct lower_io_state state;

   if (shader->stage == MESA_SHADER_TESS_CTRL)
      return;

   state.shader = shader;
   state.entrypoint = entrypoint;

   if (inputs)
      exec_list_move_nodes_to(&shader->inputs, &state.old_inputs);
   else
      exec_list_make_empty(&state.old_inputs);

   if (outputs)
      exec_list_move_nodes_to(&shader->outputs, &state.old_outputs);
   else
      exec_list_make_empty(&state.old_outputs);

   /* Walk over all of the outputs turn each output into a temporary and
    * make a new variable for the actual output.
    */
   nir_foreach_variable(var, &state.old_outputs) {
      nir_variable *output = create_shadow_temp(&state, var);
      exec_list_push_tail(&shader->outputs, &output->node);
   }

   /* and same for inputs: */
   nir_foreach_variable(var, &state.old_inputs) {
      nir_variable *input = create_shadow_temp(&state, var);
      exec_list_push_tail(&shader->inputs, &input->node);
   }

   nir_foreach_function(function, shader) {
      if (function->impl == NULL)
         continue;

      if (inputs)
         emit_input_copies_impl(&state, function->impl);

      if (outputs)
         emit_output_copies_impl(&state, function->impl);

      nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                            nir_metadata_dominance);
   }

   exec_list_append(&shader->globals, &state.old_inputs);
   exec_list_append(&shader->globals, &state.old_outputs);
}
