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
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

/*
 * This lowering pass converts references to input/output variables with
 * loads/stores to actual input/output intrinsics.
 *
 * NOTE: This pass really only works for scalar backends at the moment due
 * to the way it packes the input/output data.
 */

#include "nir.h"

struct lower_io_state {
   void *mem_ctx;
};

static unsigned
type_size(const struct glsl_type *type)
{
   unsigned int size, i;

   switch (glsl_get_base_type(type)) {
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_INT:
   case GLSL_TYPE_FLOAT:
   case GLSL_TYPE_BOOL:
      return glsl_get_components(type);
   case GLSL_TYPE_ARRAY:
      return type_size(glsl_get_array_element(type)) * glsl_get_length(type);
   case GLSL_TYPE_STRUCT:
      size = 0;
      for (i = 0; i < glsl_get_length(type); i++) {
         size += type_size(glsl_get_struct_field(type, i));
      }
      return size;
   case GLSL_TYPE_SAMPLER:
      return 0;
   case GLSL_TYPE_ATOMIC_UINT:
      return 0;
   case GLSL_TYPE_INTERFACE:
      return 0;
   case GLSL_TYPE_IMAGE:
      return 0;
   case GLSL_TYPE_VOID:
   case GLSL_TYPE_ERROR:
   case GLSL_TYPE_DOUBLE:
      unreachable("not reached");
   }

   return 0;
}

void
nir_assign_var_locations_scalar(struct exec_list *var_list, unsigned *size)
{
   unsigned location = 0;

   foreach_list_typed(nir_variable, var, node, var_list) {
      /*
       * UBO's have their own address spaces, so don't count them towards the
       * number of global uniforms
       */
      if (var->data.mode == nir_var_uniform && var->interface_type != NULL)
         continue;

      var->data.driver_location = location;
      location += type_size(var->type);
   }

   *size = location;
}

static bool
deref_has_indirect(nir_deref_var *deref)
{
   for (nir_deref *tail = deref->deref.child; tail; tail = tail->child) {
      if (tail->deref_type == nir_deref_type_array) {
         nir_deref_array *arr = nir_deref_as_array(tail);
         if (arr->deref_array_type == nir_deref_array_type_indirect)
            return true;
      }
   }

   return false;
}

static bool
mark_indirect_uses_block(nir_block *block, void *void_state)
{
   struct set *indirect_set = void_state;

   nir_foreach_instr(block, instr) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      for (unsigned i = 0;
           i < nir_intrinsic_infos[intrin->intrinsic].num_variables; i++) {
         if (deref_has_indirect(intrin->variables[i]))
            _mesa_set_add(indirect_set, intrin->variables[i]->var);
      }
   }

   return true;
}

/* Identical to nir_assign_var_locations_packed except that it assigns
 * locations to the variables that are used 100% directly first and then
 * assigns locations to variables that are used indirectly.
 */
void
nir_assign_var_locations_scalar_direct_first(nir_shader *shader,
                                             struct exec_list *var_list,
                                             unsigned *direct_size,
                                             unsigned *size)
{
   struct set *indirect_set = _mesa_set_create(NULL, _mesa_hash_pointer,
                                               _mesa_key_pointer_equal);

   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         nir_foreach_block(overload->impl, mark_indirect_uses_block,
                           indirect_set);
   }

   unsigned location = 0;

   foreach_list_typed(nir_variable, var, node, var_list) {
      if (var->data.mode == nir_var_uniform && var->interface_type != NULL)
         continue;

      if (_mesa_set_search(indirect_set, var))
         continue;

      var->data.driver_location = location;
      location += type_size(var->type);
   }

   *direct_size = location;

   foreach_list_typed(nir_variable, var, node, var_list) {
      if (var->data.mode == nir_var_uniform && var->interface_type != NULL)
         continue;

      if (!_mesa_set_search(indirect_set, var))
         continue;

      var->data.driver_location = location;
      location += type_size(var->type);
   }

   *size = location;

   _mesa_set_destroy(indirect_set, NULL);
}

static unsigned
get_io_offset(nir_deref_var *deref, nir_instr *instr, nir_src *indirect,
              struct lower_io_state *state)
{
   bool found_indirect = false;
   unsigned base_offset = 0;

   nir_deref *tail = &deref->deref;
   while (tail->child != NULL) {
      const struct glsl_type *parent_type = tail->type;
      tail = tail->child;

      if (tail->deref_type == nir_deref_type_array) {
         nir_deref_array *deref_array = nir_deref_as_array(tail);
         unsigned size = type_size(tail->type);

         base_offset += size * deref_array->base_offset;

         if (deref_array->deref_array_type == nir_deref_array_type_indirect) {
            nir_load_const_instr *load_const =
               nir_load_const_instr_create(state->mem_ctx, 1);
            load_const->value.u[0] = size;
            nir_instr_insert_before(instr, &load_const->instr);

            nir_alu_instr *mul = nir_alu_instr_create(state->mem_ctx,
                                                      nir_op_imul);
            mul->src[0].src.is_ssa = true;
            mul->src[0].src.ssa = &load_const->def;
            nir_src_copy(&mul->src[1].src, &deref_array->indirect,
                         state->mem_ctx);
            mul->dest.write_mask = 1;
            nir_ssa_dest_init(&mul->instr, &mul->dest.dest, 1, NULL);
            nir_instr_insert_before(instr, &mul->instr);

            if (found_indirect) {
               nir_alu_instr *add = nir_alu_instr_create(state->mem_ctx,
                                                         nir_op_iadd);
               add->src[0].src = *indirect;
               add->src[1].src.is_ssa = true;
               add->src[1].src.ssa = &mul->dest.dest.ssa;
               add->dest.write_mask = 1;
               nir_ssa_dest_init(&add->instr, &add->dest.dest, 1, NULL);
               nir_instr_insert_before(instr, &add->instr);

               indirect->is_ssa = true;
               indirect->ssa = &add->dest.dest.ssa;
            } else {
               indirect->is_ssa = true;
               indirect->ssa = &mul->dest.dest.ssa;
               found_indirect = true;
            }
         }
      } else if (tail->deref_type == nir_deref_type_struct) {
         nir_deref_struct *deref_struct = nir_deref_as_struct(tail);

         for (unsigned i = 0; i < deref_struct->index; i++)
            base_offset += type_size(glsl_get_struct_field(parent_type, i));
      }
   }

   return base_offset;
}

static bool
nir_lower_io_block(nir_block *block, void *void_state)
{
   struct lower_io_state *state = void_state;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_var: {
         nir_variable_mode mode = intrin->variables[0]->var->data.mode;
         if (mode != nir_var_shader_in && mode != nir_var_uniform)
            continue;

         bool has_indirect = deref_has_indirect(intrin->variables[0]);

         /* Figure out the opcode */
         nir_intrinsic_op load_op;
         switch (mode) {
         case nir_var_shader_in:
            load_op = has_indirect ? nir_intrinsic_load_input_indirect :
                                     nir_intrinsic_load_input;
            break;
         case nir_var_uniform:
            load_op = has_indirect ? nir_intrinsic_load_uniform_indirect :
                                     nir_intrinsic_load_uniform;
            break;
         default:
            unreachable("Unknown variable mode");
         }

         nir_intrinsic_instr *load = nir_intrinsic_instr_create(state->mem_ctx,
                                                                load_op);
         load->num_components = intrin->num_components;

         nir_src indirect;
         unsigned offset = get_io_offset(intrin->variables[0],
                                         &intrin->instr, &indirect, state);
         offset += intrin->variables[0]->var->data.driver_location;

         load->const_index[0] = offset;
         load->const_index[1] = 1;

         if (has_indirect)
            load->src[0] = indirect;

         if (intrin->dest.is_ssa) {
            nir_ssa_dest_init(&load->instr, &load->dest,
                              intrin->num_components, NULL);
            nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                                     nir_src_for_ssa(&load->dest.ssa),
                                     state->mem_ctx);
         } else {
            nir_dest_copy(&load->dest, &intrin->dest, state->mem_ctx);
         }

         nir_instr_insert_before(&intrin->instr, &load->instr);
         nir_instr_remove(&intrin->instr);
         break;
      }

      case nir_intrinsic_store_var: {
         if (intrin->variables[0]->var->data.mode != nir_var_shader_out)
            continue;

         bool has_indirect = deref_has_indirect(intrin->variables[0]);

         nir_intrinsic_op store_op;
         if (has_indirect) {
            store_op = nir_intrinsic_store_output_indirect;
         } else {
            store_op = nir_intrinsic_store_output;
         }

         nir_intrinsic_instr *store = nir_intrinsic_instr_create(state->mem_ctx,
                                                                 store_op);
         store->num_components = intrin->num_components;

         nir_src indirect;
         unsigned offset = get_io_offset(intrin->variables[0],
                                         &intrin->instr, &indirect, state);
         offset += intrin->variables[0]->var->data.driver_location;

         store->const_index[0] = offset;
         store->const_index[1] = 1;

         nir_src_copy(&store->src[0], &intrin->src[0], state->mem_ctx);

         if (has_indirect)
            store->src[1] = indirect;

         nir_instr_insert_before(&intrin->instr, &store->instr);
         nir_instr_remove(&intrin->instr);
         break;
      }

      default:
         break;
      }
   }

   return true;
}

static void
nir_lower_io_impl(nir_function_impl *impl)
{
   struct lower_io_state state;

   state.mem_ctx = ralloc_parent(impl);

   nir_foreach_block(impl, nir_lower_io_block, &state);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

void
nir_lower_io(nir_shader *shader)
{
   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         nir_lower_io_impl(overload->impl);
   }
}
