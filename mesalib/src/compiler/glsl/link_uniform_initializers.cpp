/*
 * Copyright © 2012 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "main/core.h"
#include "ir.h"
#include "linker.h"
#include "ir_uniform.h"
#include "util/string_to_uint_map.h"

/* These functions are put in a "private" namespace instead of being marked
 * static so that the unit tests can access them.  See
 * http://code.google.com/p/googletest/wiki/AdvancedGuide#Testing_Private_Code
 */
namespace linker {

gl_uniform_storage *
get_storage(struct gl_shader_program *prog, const char *name)
{
   unsigned id;
   if (prog->UniformHash->get(id, name))
      return &prog->UniformStorage[id];

   assert(!"No uniform storage found!");
   return NULL;
}

void
copy_constant_to_storage(union gl_constant_value *storage,
                         const ir_constant *val,
                         const enum glsl_base_type base_type,
                         const unsigned int elements,
                         unsigned int boolean_true)
{
   for (unsigned int i = 0; i < elements; i++) {
      switch (base_type) {
      case GLSL_TYPE_UINT:
         storage[i].u = val->value.u[i];
         break;
      case GLSL_TYPE_INT:
      case GLSL_TYPE_SAMPLER:
         storage[i].i = val->value.i[i];
         break;
      case GLSL_TYPE_FLOAT:
         storage[i].f = val->value.f[i];
         break;
      case GLSL_TYPE_DOUBLE:
         /* XXX need to check on big-endian */
         memcpy(&storage[i * 2].u, &val->value.d[i], sizeof(double));
         break;
      case GLSL_TYPE_BOOL:
         storage[i].b = val->value.b[i] ? boolean_true : 0;
         break;
      case GLSL_TYPE_ARRAY:
      case GLSL_TYPE_STRUCT:
      case GLSL_TYPE_IMAGE:
      case GLSL_TYPE_ATOMIC_UINT:
      case GLSL_TYPE_INTERFACE:
      case GLSL_TYPE_VOID:
      case GLSL_TYPE_SUBROUTINE:
      case GLSL_TYPE_FUNCTION:
      case GLSL_TYPE_ERROR:
         /* All other types should have already been filtered by other
          * paths in the caller.
          */
         assert(!"Should not get here.");
         break;
      }
   }
}

/**
 * Initialize an opaque uniform from the value of an explicit binding
 * qualifier specified in the shader.  Atomic counters are different because
 * they have no storage and should be handled elsewhere.
 */
void
set_opaque_binding(void *mem_ctx, gl_shader_program *prog,
                   const glsl_type *type, const char *name, int *binding)
{

   if (type->is_array() && type->fields.array->is_array()) {
      const glsl_type *const element_type = type->fields.array;

      for (unsigned int i = 0; i < type->length; i++) {
         const char *element_name = ralloc_asprintf(mem_ctx, "%s[%d]", name, i);

         set_opaque_binding(mem_ctx, prog, element_type,
                            element_name, binding);
      }
   } else {
      struct gl_uniform_storage *const storage = get_storage(prog, name);

      if (!storage)
         return;

      const unsigned elements = MAX2(storage->array_elements, 1);

      /* Section 4.4.4 (Opaque-Uniform Layout Qualifiers) of the GLSL 4.20 spec
       * says:
       *
       *     "If the binding identifier is used with an array, the first element
       *     of the array takes the specified unit and each subsequent element
       *     takes the next consecutive unit."
       */
      for (unsigned int i = 0; i < elements; i++) {
         storage->storage[i].i = (*binding)++;
      }

      for (int sh = 0; sh < MESA_SHADER_STAGES; sh++) {
        gl_linked_shader *shader = prog->_LinkedShaders[sh];

         if (shader) {
            if (storage->type->base_type == GLSL_TYPE_SAMPLER &&
                storage->opaque[sh].active) {
               for (unsigned i = 0; i < elements; i++) {
                  const unsigned index = storage->opaque[sh].index + i;
                  shader->SamplerUnits[index] = storage->storage[i].i;
               }

            } else if (storage->type->base_type == GLSL_TYPE_IMAGE &&
                    storage->opaque[sh].active) {
               for (unsigned i = 0; i < elements; i++) {
                  const unsigned index = storage->opaque[sh].index + i;
                  if (index >= ARRAY_SIZE(shader->ImageUnits))
                     break;
                  shader->ImageUnits[index] = storage->storage[i].i;
               }
            }
         }
      }
   }
}

void
set_block_binding(gl_shader_program *prog, const char *block_name,
                  unsigned mode, int binding)
{
   unsigned num_blocks = mode == ir_var_uniform ? prog->NumUniformBlocks :
      prog->NumShaderStorageBlocks;
   struct gl_uniform_block *blks = mode == ir_var_uniform ?
      prog->UniformBlocks : prog->ShaderStorageBlocks;

   for (unsigned i = 0; i < num_blocks; i++) {
      if (!strcmp(blks[i].Name, block_name)) {
         blks[i].Binding = binding;
         return;
      }
   }

   unreachable("Failed to initialize block binding");
}

void
set_uniform_initializer(void *mem_ctx, gl_shader_program *prog,
                        const char *name, const glsl_type *type,
                        ir_constant *val, unsigned int boolean_true)
{
   const glsl_type *t_without_array = type->without_array();
   if (type->is_record()) {
      ir_constant *field_constant;

      field_constant = (ir_constant *)val->components.get_head();

      for (unsigned int i = 0; i < type->length; i++) {
         const glsl_type *field_type = type->fields.structure[i].type;
         const char *field_name = ralloc_asprintf(mem_ctx, "%s.%s", name,
                                            type->fields.structure[i].name);
         set_uniform_initializer(mem_ctx, prog, field_name,
                                 field_type, field_constant, boolean_true);
         field_constant = (ir_constant *)field_constant->next;
      }
      return;
   } else if (t_without_array->is_record() ||
              (type->is_array() && type->fields.array->is_array())) {
      const glsl_type *const element_type = type->fields.array;

      for (unsigned int i = 0; i < type->length; i++) {
         const char *element_name = ralloc_asprintf(mem_ctx, "%s[%d]", name, i);

         set_uniform_initializer(mem_ctx, prog, element_name,
                                 element_type, val->array_elements[i],
                                 boolean_true);
      }
      return;
   }

   struct gl_uniform_storage *const storage = get_storage(prog, name);

   if (!storage)
      return;

   if (val->type->is_array()) {
      const enum glsl_base_type base_type =
         val->array_elements[0]->type->base_type;
      const unsigned int elements = val->array_elements[0]->type->components();
      unsigned int idx = 0;
      unsigned dmul = glsl_base_type_is_64bit(base_type) ? 2 : 1;

      assert(val->type->length >= storage->array_elements);
      for (unsigned int i = 0; i < storage->array_elements; i++) {
         copy_constant_to_storage(& storage->storage[idx],
                                  val->array_elements[i],
                                  base_type,
                                  elements,
                                  boolean_true);

         idx += elements * dmul;
      }
   } else {
      copy_constant_to_storage(storage->storage,
                               val,
                               val->type->base_type,
                               val->type->components(),
                               boolean_true);

      if (storage->type->is_sampler()) {
         for (int sh = 0; sh < MESA_SHADER_STAGES; sh++) {
            gl_linked_shader *shader = prog->_LinkedShaders[sh];

            if (shader && storage->opaque[sh].active) {
               unsigned index = storage->opaque[sh].index;

               shader->SamplerUnits[index] = storage->storage[0].i;
            }
         }
      }
   }
}
}

void
link_set_uniform_initializers(struct gl_shader_program *prog,
                              unsigned int boolean_true)
{
   void *mem_ctx = NULL;

   for (unsigned int i = 0; i < MESA_SHADER_STAGES; i++) {
      struct gl_linked_shader *shader = prog->_LinkedShaders[i];

      if (shader == NULL)
         continue;

      foreach_in_list(ir_instruction, node, shader->ir) {
         ir_variable *const var = node->as_variable();

         if (!var || (var->data.mode != ir_var_uniform &&
             var->data.mode != ir_var_shader_storage))
            continue;

         if (!mem_ctx)
            mem_ctx = ralloc_context(NULL);

         if (var->data.explicit_binding) {
            const glsl_type *const type = var->type;

            if (type->without_array()->is_sampler() ||
                type->without_array()->is_image()) {
               int binding = var->data.binding;
               linker::set_opaque_binding(mem_ctx, prog, var->type,
                                          var->name, &binding);
            } else if (var->is_in_buffer_block()) {
               const glsl_type *const iface_type = var->get_interface_type();

               /* If the variable is an array and it is an interface instance,
                * we need to set the binding for each array element.  Just
                * checking that the variable is an array is not sufficient.
                * The variable could be an array element of a uniform block
                * that lacks an instance name.  For example:
                *
                *     uniform U {
                *         float f[4];
                *     };
                *
                * In this case "f" would pass is_in_buffer_block (above) and
                * type->is_array(), but it will fail is_interface_instance().
                */
               if (var->is_interface_instance() && var->type->is_array()) {
                  for (unsigned i = 0; i < var->type->length; i++) {
                     const char *name =
                        ralloc_asprintf(mem_ctx, "%s[%u]", iface_type->name, i);

                     /* Section 4.4.3 (Uniform Block Layout Qualifiers) of the
                      * GLSL 4.20 spec says:
                      *
                      *     "If the binding identifier is used with a uniform
                      *     block instanced as an array then the first element
                      *     of the array takes the specified block binding and
                      *     each subsequent element takes the next consecutive
                      *     uniform block binding point."
                      */
                     linker::set_block_binding(prog, name, var->data.mode,
                                               var->data.binding + i);
                  }
               } else {
                  linker::set_block_binding(prog, iface_type->name,
                                            var->data.mode,
                                            var->data.binding);
               }
            } else if (type->contains_atomic()) {
               /* we don't actually need to do anything. */
            } else {
               assert(!"Explicit binding not on a sampler, UBO or atomic.");
            }
         } else if (var->constant_initializer) {
            linker::set_uniform_initializer(mem_ctx, prog, var->name,
                                            var->type, var->constant_initializer,
                                            boolean_true);
         }
      }
   }

   ralloc_free(mem_ctx);
}
