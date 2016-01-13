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
 * Implements a pass that lowers output variables to a temporary plus an
 * output variable with a single copy at each exit point of the shader.
 * This way the output variable is only ever written.
 *
 * Because valid NIR requires that output variables are never read, this
 * pass is more of a helper for NIR producers and must be run before the
 * shader is ever validated.
 */

#include "nir.h"

struct lower_outputs_state {
   nir_shader *shader;
   struct exec_list old_outputs;
};

static void
emit_output_copies(nir_cursor cursor, struct lower_outputs_state *state)
{
   assert(exec_list_length(&state->shader->outputs) ==
          exec_list_length(&state->old_outputs));

   foreach_two_lists(out_node, &state->shader->outputs,
                     temp_node, &state->old_outputs) {
      nir_variable *output = exec_node_data(nir_variable, out_node, node);
      nir_variable *temp = exec_node_data(nir_variable, temp_node, node);

      nir_intrinsic_instr *copy =
         nir_intrinsic_instr_create(state->shader, nir_intrinsic_copy_var);
      copy->variables[0] = nir_deref_var_create(copy, output);
      copy->variables[1] = nir_deref_var_create(copy, temp);

      nir_instr_insert(cursor, &copy->instr);
   }
}

static bool
emit_output_copies_block(nir_block *block, void *state)
{
   nir_foreach_instr(block, instr) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      if (intrin->intrinsic == nir_intrinsic_emit_vertex)
         emit_output_copies(nir_before_instr(&intrin->instr), state);
   }

   return true;
}

void
nir_lower_outputs_to_temporaries(nir_shader *shader)
{
   struct lower_outputs_state state;

   if (shader->stage == MESA_SHADER_TESS_CTRL)
      return;

   state.shader = shader;
   exec_list_move_nodes_to(&shader->outputs, &state.old_outputs);

   /* Walk over all of the outputs turn each output into a temporary and
    * make a new variable for the actual output.
    */
   nir_foreach_variable(var, &state.old_outputs) {
      nir_variable *output = ralloc(shader, nir_variable);
      memcpy(output, var, sizeof *output);

      /* The orignal is now the temporary */
      nir_variable *temp = var;

      /* Reparent the name to the new variable */
      ralloc_steal(output, output->name);

      /* Give the output a new name with @out-temp appended */
      temp->name = ralloc_asprintf(var, "%s@out-temp", output->name);
      temp->data.mode = nir_var_global;
      temp->constant_initializer = NULL;

      exec_list_push_tail(&shader->outputs, &output->node);
   }

   nir_foreach_function(shader, function) {
      if (function->impl == NULL)
         continue;

      if (shader->stage == MESA_SHADER_GEOMETRY) {
         /* For geometry shaders, we have to emit the output copies right
          * before each EmitVertex call.
          */
         nir_foreach_block(function->impl, emit_output_copies_block, &state);
      } else if (strcmp(function->name, "main") == 0) {
         /* For all other shader types, we need to do the copies right before
          * the jumps to the end block.
          */
         struct set_entry *block_entry;
         set_foreach(function->impl->end_block->predecessors, block_entry) {
            struct nir_block *block = (void *)block_entry->key;
            emit_output_copies(nir_after_block_before_jump(block), &state);
         }
      }

      nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                            nir_metadata_dominance);
   }

   exec_list_append(&shader->globals, &state.old_outputs);
}
