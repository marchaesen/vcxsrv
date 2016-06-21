/*
 * Copyright © 2016 Red Hat
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

#include "nir.h"
#include "nir_builder.h"

/* Lower complex (struct/array/mat) input and output vars to primitive types
 * (vec4) for linking.  All indirect input/output access should already be
 * lowered (ie. nir_lower_io_to_temporaries).
 */

struct lower_io_types_state {
   nir_shader *shader;
   struct exec_list new_ins;
   struct exec_list new_outs;
};

static nir_variable *
get_new_var(struct lower_io_types_state *state, nir_variable *var,
            const struct glsl_type *deref_type, unsigned off)
{
   struct exec_list *list;

   if (var->data.mode == nir_var_shader_in) {
      list = &state->new_ins;
   } else {
      assert(var->data.mode == nir_var_shader_out);
      list = &state->new_outs;
   }

   nir_foreach_variable(nvar, list) {
      if (nvar->data.location == (var->data.location + off))
         return nvar;
   }

   /* doesn't already exist, so we need to create a new one: */
   /* TODO figure out if scalar vs vec, and if float/int/uint/(double?)
    * do we need to fixup interpolation mode for int vs float components
    * of a struct, etc..
    */
   const struct glsl_type *ntype =
      glsl_vector_type(glsl_get_base_type(deref_type),
                       glsl_get_vector_elements(deref_type));
   nir_variable *nvar = nir_variable_create(state->shader, var->data.mode,
                                            ntype, NULL);

   nvar->name = ralloc_asprintf(nvar, "%s@%u", var->name, off);
   nvar->data = var->data;
   nvar->data.location += off;

   /* nir_variable_create is too clever for it's own good: */
   exec_node_remove(&nvar->node);
   exec_node_self_link(&nvar->node);      /* no delinit() :-( */

   exec_list_push_tail(list, &nvar->node);

   /* remove existing var from input/output list: */
   exec_node_remove(&var->node);
   exec_node_self_link(&var->node);

   return nvar;
}

static unsigned
get_deref_offset(struct lower_io_types_state *state, nir_deref *tail, bool vs_in)
{
   unsigned offset = 0;

   while (tail->child != NULL) {
      const struct glsl_type *parent_type = tail->type;
      tail = tail->child;

      if (tail->deref_type == nir_deref_type_array) {
         nir_deref_array *deref_array = nir_deref_as_array(tail);

         /* indirect inputs/outputs should already be lowered! */
         assert(deref_array->deref_array_type == nir_deref_array_type_direct);

         unsigned size = glsl_count_attribute_slots(tail->type, vs_in);

         offset += size * deref_array->base_offset;
      } else if (tail->deref_type == nir_deref_type_struct) {
         nir_deref_struct *deref_struct = nir_deref_as_struct(tail);

         for (unsigned i = 0; i < deref_struct->index; i++) {
            const struct glsl_type *ft = glsl_get_struct_field(parent_type, i);
            offset += glsl_count_attribute_slots(ft, vs_in);
         }
      }
   }

   return offset;
}

static bool
lower_io_types_block(struct lower_io_types_state *state, nir_block *block)
{
   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

      if ((intr->intrinsic != nir_intrinsic_load_var) &&
          (intr->intrinsic != nir_intrinsic_store_var))
         continue;

      nir_variable *var = intr->variables[0]->var;

      if ((var->data.mode != nir_var_shader_in) &&
          (var->data.mode != nir_var_shader_out))
         continue;

      bool vs_in = (state->shader->stage == MESA_SHADER_VERTEX) &&
                   (var->data.mode == nir_var_shader_in);
      if (glsl_count_attribute_slots(var->type, vs_in) == 1)
         continue;

      unsigned off = get_deref_offset(state, &intr->variables[0]->deref, vs_in);
      const struct glsl_type *deref_type =
         nir_deref_tail(&intr->variables[0]->deref)->type;
      nir_variable *nvar = get_new_var(state, var, deref_type, off);

      /* and then re-write the load/store_var deref: */
      intr->variables[0] = nir_deref_var_create(intr, nvar);
   }

   return true;
}

static void
lower_io_types_impl(nir_function_impl *impl, struct lower_io_types_state *state)
{
   nir_foreach_block(block, impl) {
      lower_io_types_block(state, block);
   }

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}


void
nir_lower_io_types(nir_shader *shader)
{
   struct lower_io_types_state state;

   state.shader = shader;
   exec_list_make_empty(&state.new_ins);
   exec_list_make_empty(&state.new_outs);

   nir_foreach_function(function, shader) {
      if (function->impl)
         lower_io_types_impl(function->impl, &state);
   }

   /* move new in/out vars to shader's lists: */
   exec_list_append(&shader->inputs, &state.new_ins);
   exec_list_append(&shader->outputs, &state.new_outs);
}
