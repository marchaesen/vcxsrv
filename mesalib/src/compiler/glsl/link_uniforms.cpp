/*
 * Copyright © 2011 Intel Corporation
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
#include "glsl_symbol_table.h"
#include "program/hash_table.h"
#include "program.h"
#include "util/hash_table.h"

/**
 * \file link_uniforms.cpp
 * Assign locations for GLSL uniforms.
 *
 * \author Ian Romanick <ian.d.romanick@intel.com>
 */

/**
 * Used by linker to indicate uniforms that have no location set.
 */
#define UNMAPPED_UNIFORM_LOC ~0u

/**
 * Count the backing storage requirements for a type
 */
static unsigned
values_for_type(const glsl_type *type)
{
   if (type->is_sampler()) {
      return 1;
   } else if (type->is_array() && type->fields.array->is_sampler()) {
      return type->array_size();
   } else {
      return type->component_slots();
   }
}

void
program_resource_visitor::process(const glsl_type *type, const char *name)
{
   assert(type->without_array()->is_record()
          || type->without_array()->is_interface());

   unsigned record_array_count = 1;
   char *name_copy = ralloc_strdup(NULL, name);
   enum glsl_interface_packing packing = type->get_interface_packing();

   recursion(type, &name_copy, strlen(name), false, NULL, packing, false,
             record_array_count, NULL);
   ralloc_free(name_copy);
}

void
program_resource_visitor::process(ir_variable *var)
{
   unsigned record_array_count = 1;
   const bool row_major =
      var->data.matrix_layout == GLSL_MATRIX_LAYOUT_ROW_MAJOR;

   const enum glsl_interface_packing packing = var->get_interface_type() ?
      var->get_interface_type_packing() :
      var->type->get_interface_packing();

   const glsl_type *t =
      var->data.from_named_ifc_block ? var->get_interface_type() : var->type;
   const glsl_type *t_without_array = t->without_array();

   /* false is always passed for the row_major parameter to the other
    * processing functions because no information is available to do
    * otherwise.  See the warning in linker.h.
    */
   if (t_without_array->is_record() ||
              (t->is_array() && t->fields.array->is_array())) {
      char *name = ralloc_strdup(NULL, var->name);
      recursion(var->type, &name, strlen(name), row_major, NULL, packing,
                false, record_array_count, NULL);
      ralloc_free(name);
   } else if (t_without_array->is_interface()) {
      char *name = ralloc_strdup(NULL, t_without_array->name);
      const glsl_struct_field *ifc_member = var->data.from_named_ifc_block ?
         &t_without_array->
            fields.structure[t_without_array->field_index(var->name)] : NULL;

      recursion(t, &name, strlen(name), row_major, NULL, packing,
                false, record_array_count, ifc_member);
      ralloc_free(name);
   } else {
      this->set_record_array_count(record_array_count);
      this->visit_field(t, var->name, row_major, NULL, packing, false);
   }
}

void
program_resource_visitor::recursion(const glsl_type *t, char **name,
                                    size_t name_length, bool row_major,
                                    const glsl_type *record_type,
                                    const enum glsl_interface_packing packing,
                                    bool last_field,
                                    unsigned record_array_count,
                                    const glsl_struct_field *named_ifc_member)
{
   /* Records need to have each field processed individually.
    *
    * Arrays of records need to have each array element processed
    * individually, then each field of the resulting array elements processed
    * individually.
    */
   if (t->is_interface() && named_ifc_member) {
      ralloc_asprintf_rewrite_tail(name, &name_length, ".%s",
                                   named_ifc_member->name);
      recursion(named_ifc_member->type, name, name_length, row_major, NULL,
                packing, false, record_array_count, NULL);
   } else if (t->is_record() || t->is_interface()) {
      if (record_type == NULL && t->is_record())
         record_type = t;

      if (t->is_record())
         this->enter_record(t, *name, row_major, packing);

      for (unsigned i = 0; i < t->length; i++) {
         const char *field = t->fields.structure[i].name;
         size_t new_length = name_length;

         if (t->fields.structure[i].type->is_record())
            this->visit_field(&t->fields.structure[i]);

         if (t->is_interface() && t->fields.structure[i].offset != -1)
            this->set_buffer_offset(t->fields.structure[i].offset);

         /* Append '.field' to the current variable name. */
         if (name_length == 0) {
            ralloc_asprintf_rewrite_tail(name, &new_length, "%s", field);
         } else {
            ralloc_asprintf_rewrite_tail(name, &new_length, ".%s", field);
         }

         /* The layout of structures at the top level of the block is set
          * during parsing.  For matrices contained in multiple levels of
          * structures in the block, the inner structures have no layout.
          * These cases must potentially inherit the layout from the outer
          * levels.
          */
         bool field_row_major = row_major;
         const enum glsl_matrix_layout matrix_layout =
            glsl_matrix_layout(t->fields.structure[i].matrix_layout);
         if (matrix_layout == GLSL_MATRIX_LAYOUT_ROW_MAJOR) {
            field_row_major = true;
         } else if (matrix_layout == GLSL_MATRIX_LAYOUT_COLUMN_MAJOR) {
            field_row_major = false;
         }

         recursion(t->fields.structure[i].type, name, new_length,
                   field_row_major,
                   record_type,
                   packing,
                   (i + 1) == t->length, record_array_count, NULL);

         /* Only the first leaf-field of the record gets called with the
          * record type pointer.
          */
         record_type = NULL;
      }

      if (t->is_record()) {
         (*name)[name_length] = '\0';
         this->leave_record(t, *name, row_major, packing);
      }
   } else if (t->without_array()->is_record() ||
              t->without_array()->is_interface() ||
              (t->is_array() && t->fields.array->is_array())) {
      if (record_type == NULL && t->fields.array->is_record())
         record_type = t->fields.array;

      unsigned length = t->length;
      /* Shader storage block unsized arrays: add subscript [0] to variable
       * names */
      if (t->is_unsized_array())
         length = 1;

      record_array_count *= length;

      for (unsigned i = 0; i < length; i++) {
         size_t new_length = name_length;

         /* Append the subscript to the current variable name */
         ralloc_asprintf_rewrite_tail(name, &new_length, "[%u]", i);

         recursion(t->fields.array, name, new_length, row_major,
                   record_type,
                   packing,
                   (i + 1) == t->length, record_array_count,
                   named_ifc_member);

         /* Only the first leaf-field of the record gets called with the
          * record type pointer.
          */
         record_type = NULL;
      }
   } else {
      this->set_record_array_count(record_array_count);
      this->visit_field(t, *name, row_major, record_type, packing, last_field);
   }
}

void
program_resource_visitor::visit_field(const glsl_type *type, const char *name,
                                      bool row_major,
                                      const glsl_type *,
                                      const enum glsl_interface_packing,
                                      bool /* last_field */)
{
   visit_field(type, name, row_major);
}

void
program_resource_visitor::visit_field(const glsl_struct_field *field)
{
   (void) field;
   /* empty */
}

void
program_resource_visitor::enter_record(const glsl_type *, const char *, bool,
                                       const enum glsl_interface_packing)
{
}

void
program_resource_visitor::leave_record(const glsl_type *, const char *, bool,
                                       const enum glsl_interface_packing)
{
}

void
program_resource_visitor::set_buffer_offset(unsigned)
{
}

void
program_resource_visitor::set_record_array_count(unsigned)
{
}

namespace {

/**
 * Class to help calculate the storage requirements for a set of uniforms
 *
 * As uniforms are added to the active set the number of active uniforms and
 * the storage requirements for those uniforms are accumulated.  The active
 * uniforms are added to the hash table supplied to the constructor.
 *
 * If the same uniform is added multiple times (i.e., once for each shader
 * target), it will only be accounted once.
 */
class count_uniform_size : public program_resource_visitor {
public:
   count_uniform_size(struct string_to_uint_map *map,
                      struct string_to_uint_map *hidden_map)
      : num_active_uniforms(0), num_hidden_uniforms(0), num_values(0),
        num_shader_samplers(0), num_shader_images(0),
        num_shader_uniform_components(0), num_shader_subroutines(0),
        is_buffer_block(false), is_shader_storage(false), map(map),
        hidden_map(hidden_map)
   {
      /* empty */
   }

   void start_shader()
   {
      this->num_shader_samplers = 0;
      this->num_shader_images = 0;
      this->num_shader_uniform_components = 0;
      this->num_shader_subroutines = 0;
   }

   void process(ir_variable *var)
   {
      this->current_var = var;
      this->is_buffer_block = var->is_in_buffer_block();
      this->is_shader_storage = var->is_in_shader_storage_block();
      if (var->is_interface_instance())
         program_resource_visitor::process(var->get_interface_type(),
                                           var->get_interface_type()->name);
      else
         program_resource_visitor::process(var);
   }

   /**
    * Total number of active uniforms counted
    */
   unsigned num_active_uniforms;

   unsigned num_hidden_uniforms;

   /**
    * Number of data values required to back the storage for the active uniforms
    */
   unsigned num_values;

   /**
    * Number of samplers used
    */
   unsigned num_shader_samplers;

   /**
    * Number of images used
    */
   unsigned num_shader_images;

   /**
    * Number of uniforms used in the current shader
    */
   unsigned num_shader_uniform_components;

   /**
    * Number of subroutine uniforms used
    */
   unsigned num_shader_subroutines;

   bool is_buffer_block;
   bool is_shader_storage;

   struct string_to_uint_map *map;

private:
   virtual void visit_field(const glsl_type *type, const char *name,
                            bool row_major)
   {
      assert(!type->without_array()->is_record());
      assert(!type->without_array()->is_interface());
      assert(!(type->is_array() && type->fields.array->is_array()));

      (void) row_major;

      /* Count the number of samplers regardless of whether the uniform is
       * already in the hash table.  The hash table prevents adding the same
       * uniform for multiple shader targets, but in this case we want to
       * count it for each shader target.
       */
      const unsigned values = values_for_type(type);
      if (type->contains_subroutine()) {
         this->num_shader_subroutines += values;
      } else if (type->contains_sampler()) {
         this->num_shader_samplers += values;
      } else if (type->contains_image()) {
         this->num_shader_images += values;

         /* As drivers are likely to represent image uniforms as
          * scalar indices, count them against the limit of uniform
          * components in the default block.  The spec allows image
          * uniforms to use up no more than one scalar slot.
          */
         if(!is_shader_storage)
            this->num_shader_uniform_components += values;
      } else {
         /* Accumulate the total number of uniform slots used by this shader.
          * Note that samplers do not count against this limit because they
          * don't use any storage on current hardware.
          */
         if (!is_buffer_block)
            this->num_shader_uniform_components += values;
      }

      /* If the uniform is already in the map, there's nothing more to do.
       */
      unsigned id;
      if (this->map->get(id, name))
         return;

      if (this->current_var->data.how_declared == ir_var_hidden) {
         this->hidden_map->put(this->num_hidden_uniforms, name);
         this->num_hidden_uniforms++;
      } else {
         this->map->put(this->num_active_uniforms-this->num_hidden_uniforms,
                        name);
      }

      /* Each leaf uniform occupies one entry in the list of active
       * uniforms.
       */
      this->num_active_uniforms++;

      if(!is_gl_identifier(name) && !is_shader_storage && !is_buffer_block)
         this->num_values += values;
   }

   struct string_to_uint_map *hidden_map;

   /**
    * Current variable being processed.
    */
   ir_variable *current_var;
};

} /* anonymous namespace */

/**
 * Class to help parcel out pieces of backing storage to uniforms
 *
 * Each uniform processed has some range of the \c gl_constant_value
 * structures associated with it.  The association is done by finding
 * the uniform in the \c string_to_uint_map and using the value from
 * the map to connect that slot in the \c gl_uniform_storage table
 * with the next available slot in the \c gl_constant_value array.
 *
 * \warning
 * This class assumes that every uniform that will be processed is
 * already in the \c string_to_uint_map.  In addition, it assumes that
 * the \c gl_uniform_storage and \c gl_constant_value arrays are "big
 * enough."
 */
class parcel_out_uniform_storage : public program_resource_visitor {
public:
   parcel_out_uniform_storage(struct gl_shader_program *prog,
                              struct string_to_uint_map *map,
                              struct gl_uniform_storage *uniforms,
                              union gl_constant_value *values)
      : prog(prog), map(map), uniforms(uniforms), values(values)
   {
   }

   void start_shader(gl_shader_stage shader_type)
   {
      assert(shader_type < MESA_SHADER_STAGES);
      this->shader_type = shader_type;

      this->shader_samplers_used = 0;
      this->shader_shadow_samplers = 0;
      this->next_sampler = 0;
      this->next_image = 0;
      this->next_subroutine = 0;
      this->record_array_count = 1;
      memset(this->targets, 0, sizeof(this->targets));
   }

   void set_and_process(ir_variable *var)
   {
      current_var = var;
      field_counter = 0;
      this->record_next_sampler = new string_to_uint_map;

      buffer_block_index = -1;
      if (var->is_in_buffer_block()) {
         struct gl_uniform_block *blks = var->is_in_shader_storage_block() ?
            prog->ShaderStorageBlocks : prog->UniformBlocks;
         unsigned num_blks = var->is_in_shader_storage_block() ?
            prog->NumShaderStorageBlocks : prog->NumUniformBlocks;

         if (var->is_interface_instance() && var->type->is_array()) {
            unsigned l = strlen(var->get_interface_type()->name);

            for (unsigned i = 0; i < num_blks; i++) {
               if (strncmp(var->get_interface_type()->name, blks[i].Name, l)
                   == 0 && blks[i].Name[l] == '[') {
                  buffer_block_index = i;
                  break;
               }
            }
         } else {
            for (unsigned i = 0; i < num_blks; i++) {
               if (strcmp(var->get_interface_type()->name, blks[i].Name) ==
                   0) {
                  buffer_block_index = i;
                  break;
               }
            }
         }
         assert(buffer_block_index != -1);

         /* Uniform blocks that were specified with an instance name must be
          * handled a little bit differently.  The name of the variable is the
          * name used to reference the uniform block instead of being the name
          * of a variable within the block.  Therefore, searching for the name
          * within the block will fail.
          */
         if (var->is_interface_instance()) {
            ubo_byte_offset = 0;
            process(var->get_interface_type(),
                    var->get_interface_type()->name);
         } else {
            const struct gl_uniform_block *const block =
               &blks[buffer_block_index];

            assert(var->data.location != -1);

            const struct gl_uniform_buffer_variable *const ubo_var =
               &block->Uniforms[var->data.location];

            ubo_byte_offset = ubo_var->Offset;
            process(var);
         }
      } else {
         /* Store any explicit location and reset data location so we can
          * reuse this variable for storing the uniform slot number.
          */
         this->explicit_location = current_var->data.location;
         current_var->data.location = -1;

         process(var);
      }
      delete this->record_next_sampler;
   }

   int buffer_block_index;
   int ubo_byte_offset;
   gl_shader_stage shader_type;

private:
   void handle_samplers(const glsl_type *base_type,
                        struct gl_uniform_storage *uniform, const char *name)
   {
      if (base_type->is_sampler()) {
         uniform->opaque[shader_type].active = true;

         /* Handle multiple samplers inside struct arrays */
         if (this->record_array_count > 1) {
            unsigned inner_array_size = MAX2(1, uniform->array_elements);
            char *name_copy = ralloc_strdup(NULL, name);

            /* Remove all array subscripts from the sampler name */
            char *str_start;
            const char *str_end;
            while((str_start = strchr(name_copy, '[')) &&
                  (str_end = strchr(name_copy, ']'))) {
               memmove(str_start, str_end + 1, 1 + strlen(str_end));
            }

            unsigned index = 0;
            if (this->record_next_sampler->get(index, name_copy)) {
               /* In this case, we've already seen this uniform so we just use
                * the next sampler index recorded the last time we visited.
                */
               uniform->opaque[shader_type].index = index;
               index = inner_array_size + uniform->opaque[shader_type].index;
               this->record_next_sampler->put(index, name_copy);

               ralloc_free(name_copy);
               /* Return as everything else has already been initialised in a
                * previous pass.
                */
               return;
            } else {
               /* We've never seen this uniform before so we need to allocate
                * enough indices to store it.
                *
                * Nested struct arrays behave like arrays of arrays so we need
                * to increase the index by the total number of elements of the
                * sampler in case there is more than one sampler inside the
                * structs. This allows the offset to be easily calculated for
                * indirect indexing.
                */
               uniform->opaque[shader_type].index = this->next_sampler;
               this->next_sampler +=
                  inner_array_size * this->record_array_count;

               /* Store the next index for future passes over the struct array
                */
               index = uniform->opaque[shader_type].index + inner_array_size;
               this->record_next_sampler->put(index, name_copy);
               ralloc_free(name_copy);
            }
         } else {
            /* Increment the sampler by 1 for non-arrays and by the number of
             * array elements for arrays.
             */
            uniform->opaque[shader_type].index = this->next_sampler;
            this->next_sampler += MAX2(1, uniform->array_elements);
         }

         const gl_texture_index target = base_type->sampler_index();
         const unsigned shadow = base_type->sampler_shadow;
         for (unsigned i = uniform->opaque[shader_type].index;
              i < MIN2(this->next_sampler, MAX_SAMPLERS);
              i++) {
            this->targets[i] = target;
            this->shader_samplers_used |= 1U << i;
            this->shader_shadow_samplers |= shadow << i;
         }
      }
   }

   void handle_images(const glsl_type *base_type,
                      struct gl_uniform_storage *uniform)
   {
      if (base_type->is_image()) {
         uniform->opaque[shader_type].index = this->next_image;
         uniform->opaque[shader_type].active = true;

         /* Set image access qualifiers */
         const GLenum access =
            (current_var->data.image_read_only ? GL_READ_ONLY :
             current_var->data.image_write_only ? GL_WRITE_ONLY :
                GL_READ_WRITE);

         const unsigned first = this->next_image;

         /* Increment the image index by 1 for non-arrays and by the
          * number of array elements for arrays.
          */
         this->next_image += MAX2(1, uniform->array_elements);

         for (unsigned i = first; i < MIN2(next_image, MAX_IMAGE_UNIFORMS); i++)
            prog->_LinkedShaders[shader_type]->ImageAccess[i] = access;
      }
   }

   void handle_subroutines(const glsl_type *base_type,
                           struct gl_uniform_storage *uniform)
   {
      if (base_type->is_subroutine()) {
         uniform->opaque[shader_type].index = this->next_subroutine;
         uniform->opaque[shader_type].active = true;

         /* Increment the subroutine index by 1 for non-arrays and by the
          * number of array elements for arrays.
          */
         this->next_subroutine += MAX2(1, uniform->array_elements);

      }
   }

   virtual void set_buffer_offset(unsigned offset)
   {
      this->ubo_byte_offset = offset;
   }

   virtual void set_record_array_count(unsigned record_array_count)
   {
      this->record_array_count = record_array_count;
   }

   virtual void visit_field(const glsl_type *type, const char *name,
                            bool row_major)
   {
      (void) type;
      (void) name;
      (void) row_major;
      assert(!"Should not get here.");
   }

   virtual void enter_record(const glsl_type *type, const char *,
                             bool row_major, const enum glsl_interface_packing packing) {
      assert(type->is_record());
      if (this->buffer_block_index == -1)
         return;
      if (packing == GLSL_INTERFACE_PACKING_STD430)
         this->ubo_byte_offset = glsl_align(
            this->ubo_byte_offset, type->std430_base_alignment(row_major));
      else
         this->ubo_byte_offset = glsl_align(
            this->ubo_byte_offset, type->std140_base_alignment(row_major));
   }

   virtual void leave_record(const glsl_type *type, const char *,
                             bool row_major, const enum glsl_interface_packing packing) {
      assert(type->is_record());
      if (this->buffer_block_index == -1)
         return;
      if (packing == GLSL_INTERFACE_PACKING_STD430)
         this->ubo_byte_offset = glsl_align(
            this->ubo_byte_offset, type->std430_base_alignment(row_major));
      else
         this->ubo_byte_offset = glsl_align(
            this->ubo_byte_offset, type->std140_base_alignment(row_major));
   }

   virtual void visit_field(const glsl_type *type, const char *name,
                            bool row_major, const glsl_type * /* record_type */,
                            const enum glsl_interface_packing packing,
                            bool /* last_field */)
   {
      assert(!type->without_array()->is_record());
      assert(!type->without_array()->is_interface());
      assert(!(type->is_array() && type->fields.array->is_array()));

      unsigned id;
      bool found = this->map->get(id, name);
      assert(found);

      if (!found)
         return;

      const glsl_type *base_type;
      if (type->is_array()) {
         this->uniforms[id].array_elements = type->length;
         base_type = type->fields.array;
      } else {
         this->uniforms[id].array_elements = 0;
         base_type = type;
      }

      /* Initialise opaque data */
      this->uniforms[id].opaque[shader_type].index = ~0;
      this->uniforms[id].opaque[shader_type].active = false;

      /* This assigns uniform indices to sampler and image uniforms. */
      handle_samplers(base_type, &this->uniforms[id], name);
      handle_images(base_type, &this->uniforms[id]);
      handle_subroutines(base_type, &this->uniforms[id]);

      /* For array of arrays or struct arrays the base location may have
       * already been set so don't set it again.
       */
      if (buffer_block_index == -1 && current_var->data.location == -1) {
         current_var->data.location = id;
      }

      /* If there is already storage associated with this uniform or if the
       * uniform is set as builtin, it means that it was set while processing
       * an earlier shader stage.  For example, we may be processing the
       * uniform in the fragment shader, but the uniform was already processed
       * in the vertex shader.
       */
      if (this->uniforms[id].storage != NULL || this->uniforms[id].builtin) {
         return;
      }

      /* Assign explicit locations. */
      if (current_var->data.explicit_location) {
         /* Set sequential locations for struct fields. */
         if (current_var->type->without_array()->is_record() ||
             current_var->type->is_array_of_arrays()) {
            const unsigned entries = MAX2(1, this->uniforms[id].array_elements);
            this->uniforms[id].remap_location =
               this->explicit_location + field_counter;
            field_counter += entries;
         } else {
            this->uniforms[id].remap_location = this->explicit_location;
         }
      } else {
         /* Initialize to to indicate that no location is set */
         this->uniforms[id].remap_location = UNMAPPED_UNIFORM_LOC;
      }

      this->uniforms[id].name = ralloc_strdup(this->uniforms, name);
      this->uniforms[id].type = base_type;
      this->uniforms[id].num_driver_storage = 0;
      this->uniforms[id].driver_storage = NULL;
      this->uniforms[id].atomic_buffer_index = -1;
      this->uniforms[id].hidden =
         current_var->data.how_declared == ir_var_hidden;
      this->uniforms[id].builtin = is_gl_identifier(name);

      this->uniforms[id].is_shader_storage =
         current_var->is_in_shader_storage_block();

      /* Do not assign storage if the uniform is a builtin or buffer object */
      if (!this->uniforms[id].builtin &&
          !this->uniforms[id].is_shader_storage &&
          this->buffer_block_index == -1)
         this->uniforms[id].storage = this->values;

      if (this->buffer_block_index != -1) {
         this->uniforms[id].block_index = this->buffer_block_index;

         unsigned alignment = type->std140_base_alignment(row_major);
         if (packing == GLSL_INTERFACE_PACKING_STD430)
            alignment = type->std430_base_alignment(row_major);
         this->ubo_byte_offset = glsl_align(this->ubo_byte_offset, alignment);
         this->uniforms[id].offset = this->ubo_byte_offset;
         if (packing == GLSL_INTERFACE_PACKING_STD430)
            this->ubo_byte_offset += type->std430_size(row_major);
         else
            this->ubo_byte_offset += type->std140_size(row_major);

         if (type->is_array()) {
            if (packing == GLSL_INTERFACE_PACKING_STD430)
               this->uniforms[id].array_stride =
                  type->without_array()->std430_array_stride(row_major);
            else
               this->uniforms[id].array_stride =
                  glsl_align(type->without_array()->std140_size(row_major),
                             16);
         } else {
            this->uniforms[id].array_stride = 0;
         }

         if (type->without_array()->is_matrix()) {
            const glsl_type *matrix = type->without_array();
            const unsigned N = matrix->base_type == GLSL_TYPE_DOUBLE ? 8 : 4;
            const unsigned items =
               row_major ? matrix->matrix_columns : matrix->vector_elements;

            assert(items <= 4);
            if (packing == GLSL_INTERFACE_PACKING_STD430)
               this->uniforms[id].matrix_stride = items < 3 ? items * N :
                                                    glsl_align(items * N, 16);
            else
               this->uniforms[id].matrix_stride = glsl_align(items * N, 16);
            this->uniforms[id].row_major = row_major;
         } else {
            this->uniforms[id].matrix_stride = 0;
            this->uniforms[id].row_major = false;
         }
      } else {
         this->uniforms[id].block_index = -1;
         this->uniforms[id].offset = -1;
         this->uniforms[id].array_stride = -1;
         this->uniforms[id].matrix_stride = -1;
         this->uniforms[id].row_major = false;
      }

      if (!this->uniforms[id].builtin &&
          !this->uniforms[id].is_shader_storage &&
          this->buffer_block_index == -1)
         this->values += values_for_type(type);
   }

   /**
    * Current program being processed.
    */
   struct gl_shader_program *prog;

   struct string_to_uint_map *map;

   struct gl_uniform_storage *uniforms;
   unsigned next_sampler;
   unsigned next_image;
   unsigned next_subroutine;

   /**
    * Field counter is used to take care that uniform structures
    * with explicit locations get sequential locations.
    */
   unsigned field_counter;

   /**
    * Current variable being processed.
    */
   ir_variable *current_var;

   /* Used to store the explicit location from current_var so that we can
    * reuse the location field for storing the uniform slot id.
    */
   int explicit_location;

   /* Stores total struct array elements including nested structs */
   unsigned record_array_count;

   /* Map for temporarily storing next sampler index when handling samplers in
    * struct arrays.
    */
   struct string_to_uint_map *record_next_sampler;

public:
   union gl_constant_value *values;

   gl_texture_index targets[MAX_SAMPLERS];

   /**
    * Mask of samplers used by the current shader stage.
    */
   unsigned shader_samplers_used;

   /**
    * Mask of samplers used by the current shader stage for shadows.
    */
   unsigned shader_shadow_samplers;
};

/**
 * Walks the IR and update the references to uniform blocks in the
 * ir_variables to point at linked shader's list (previously, they
 * would point at the uniform block list in one of the pre-linked
 * shaders).
 */
static void
link_update_uniform_buffer_variables(struct gl_linked_shader *shader)
{
   foreach_in_list(ir_instruction, node, shader->ir) {
      ir_variable *const var = node->as_variable();

      if ((var == NULL) || !var->is_in_buffer_block())
         continue;

      assert(var->data.mode == ir_var_uniform ||
             var->data.mode == ir_var_shader_storage);

      if (var->is_interface_instance()) {
         var->data.location = 0;
         continue;
      }

      bool found = false;
      char sentinel = '\0';

      if (var->type->is_record()) {
         sentinel = '.';
      } else if (var->type->is_array() && (var->type->fields.array->is_array()
                 || var->type->without_array()->is_record())) {
         sentinel = '[';
      }

      unsigned num_blocks = var->data.mode == ir_var_uniform ?
         shader->NumUniformBlocks : shader->NumShaderStorageBlocks;
      struct gl_uniform_block **blks = var->data.mode == ir_var_uniform ?
         shader->UniformBlocks : shader->ShaderStorageBlocks;

      const unsigned l = strlen(var->name);
      for (unsigned i = 0; i < num_blocks; i++) {
         for (unsigned j = 0; j < blks[i]->NumUniforms; j++) {
            if (sentinel) {
               const char *begin = blks[i]->Uniforms[j].Name;
               const char *end = strchr(begin, sentinel);

               if (end == NULL)
                  continue;

               if ((ptrdiff_t) l != (end - begin))
                  continue;

               if (strncmp(var->name, begin, l) == 0) {
                  found = true;
                  var->data.location = j;
                  break;
               }
            } else if (!strcmp(var->name, blks[i]->Uniforms[j].Name)) {
               found = true;
               var->data.location = j;
               break;
            }
         }
         if (found)
            break;
      }
      assert(found);
   }
}

/**
 * Combine the hidden uniform hash map with the uniform hash map so that the
 * hidden uniforms will be given indicies at the end of the uniform storage
 * array.
 */
static void
assign_hidden_uniform_slot_id(const char *name, unsigned hidden_id,
                              void *closure)
{
   count_uniform_size *uniform_size = (count_uniform_size *) closure;
   unsigned hidden_uniform_start = uniform_size->num_active_uniforms -
      uniform_size->num_hidden_uniforms;

   uniform_size->map->put(hidden_uniform_start + hidden_id, name);
}

/**
 * Search through the list of empty blocks to find one that fits the current
 * uniform.
 */
static int
find_empty_block(struct gl_shader_program *prog,
                 struct gl_uniform_storage *uniform)
{
   const unsigned entries = MAX2(1, uniform->array_elements);

   foreach_list_typed(struct empty_uniform_block, block, link,
                      &prog->EmptyUniformLocations) {
      /* Found a block with enough slots to fit the uniform */
      if (block->slots == entries) {
         unsigned start = block->start;
         exec_node_remove(&block->link);
         ralloc_free(block);

         return start;
      /* Found a block with more slots than needed. It can still be used. */
      } else if (block->slots > entries) {
         unsigned start = block->start;
         block->start += entries;
         block->slots -= entries;

         return start;
      }
   }

   return -1;
}

void
link_assign_uniform_locations(struct gl_shader_program *prog,
                              unsigned int boolean_true,
                              unsigned int num_explicit_uniform_locs,
                              unsigned int max_uniform_locs)
{
   ralloc_free(prog->UniformStorage);
   prog->UniformStorage = NULL;
   prog->NumUniformStorage = 0;

   if (prog->UniformHash != NULL) {
      prog->UniformHash->clear();
   } else {
      prog->UniformHash = new string_to_uint_map;
   }

   /* First pass: Count the uniform resources used by the user-defined
    * uniforms.  While this happens, each active uniform will have an index
    * assigned to it.
    *
    * Note: this is *NOT* the index that is returned to the application by
    * glGetUniformLocation.
    */
   struct string_to_uint_map *hiddenUniforms = new string_to_uint_map;
   count_uniform_size uniform_size(prog->UniformHash, hiddenUniforms);
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      struct gl_linked_shader *sh = prog->_LinkedShaders[i];

      if (sh == NULL)
         continue;

      /* Uniforms that lack an initializer in the shader code have an initial
       * value of zero.  This includes sampler uniforms.
       *
       * Page 24 (page 30 of the PDF) of the GLSL 1.20 spec says:
       *
       *     "The link time initial value is either the value of the variable's
       *     initializer, if present, or 0 if no initializer is present. Sampler
       *     types cannot have initializers."
       */
      memset(sh->SamplerUnits, 0, sizeof(sh->SamplerUnits));
      memset(sh->ImageUnits, 0, sizeof(sh->ImageUnits));

      link_update_uniform_buffer_variables(sh);

      /* Reset various per-shader target counts.
       */
      uniform_size.start_shader();

      foreach_in_list(ir_instruction, node, sh->ir) {
         ir_variable *const var = node->as_variable();

         if ((var == NULL) || (var->data.mode != ir_var_uniform &&
                               var->data.mode != ir_var_shader_storage))
            continue;

         uniform_size.process(var);
      }

      sh->num_samplers = uniform_size.num_shader_samplers;
      sh->NumImages = uniform_size.num_shader_images;
      sh->num_uniform_components = uniform_size.num_shader_uniform_components;
      sh->num_combined_uniform_components = sh->num_uniform_components;

      for (unsigned i = 0; i < sh->NumUniformBlocks; i++) {
         sh->num_combined_uniform_components +=
            sh->UniformBlocks[i]->UniformBufferSize / 4;
      }
   }

   const unsigned num_uniforms = uniform_size.num_active_uniforms;
   const unsigned num_data_slots = uniform_size.num_values;
   const unsigned hidden_uniforms = uniform_size.num_hidden_uniforms;

   /* assign hidden uniforms a slot id */
   hiddenUniforms->iterate(assign_hidden_uniform_slot_id, &uniform_size);
   delete hiddenUniforms;

   /* On the outside chance that there were no uniforms, bail out.
    */
   if (num_uniforms == 0)
      return;

   struct gl_uniform_storage *uniforms =
      rzalloc_array(prog, struct gl_uniform_storage, num_uniforms);
   union gl_constant_value *data =
      rzalloc_array(uniforms, union gl_constant_value, num_data_slots);
#ifndef NDEBUG
   union gl_constant_value *data_end = &data[num_data_slots];
#endif

   parcel_out_uniform_storage parcel(prog, prog->UniformHash, uniforms, data);

   unsigned total_entries = num_explicit_uniform_locs;
   unsigned empty_locs = prog->NumUniformRemapTable - num_explicit_uniform_locs;

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (prog->_LinkedShaders[i] == NULL)
         continue;

      parcel.start_shader((gl_shader_stage)i);

      foreach_in_list(ir_instruction, node, prog->_LinkedShaders[i]->ir) {
         ir_variable *const var = node->as_variable();

         if ((var == NULL) || (var->data.mode != ir_var_uniform &&
                               var->data.mode != ir_var_shader_storage))
            continue;

         parcel.set_and_process(var);
      }

      prog->_LinkedShaders[i]->active_samplers = parcel.shader_samplers_used;
      prog->_LinkedShaders[i]->shadow_samplers = parcel.shader_shadow_samplers;

      STATIC_ASSERT(sizeof(prog->_LinkedShaders[i]->SamplerTargets) ==
                    sizeof(parcel.targets));
      memcpy(prog->_LinkedShaders[i]->SamplerTargets, parcel.targets,
             sizeof(prog->_LinkedShaders[i]->SamplerTargets));
   }

   /* Reserve all the explicit locations of the active uniforms. */
   for (unsigned i = 0; i < num_uniforms; i++) {
      if (uniforms[i].type->is_subroutine() ||
          uniforms[i].is_shader_storage)
         continue;

      if (uniforms[i].remap_location != UNMAPPED_UNIFORM_LOC) {
         /* How many new entries for this uniform? */
         const unsigned entries = MAX2(1, uniforms[i].array_elements);

         /* Set remap table entries point to correct gl_uniform_storage. */
         for (unsigned j = 0; j < entries; j++) {
            unsigned element_loc = uniforms[i].remap_location + j;
            assert(prog->UniformRemapTable[element_loc] ==
                   INACTIVE_UNIFORM_EXPLICIT_LOCATION);
            prog->UniformRemapTable[element_loc] = &uniforms[i];
         }
      }
   }

   /* Reserve locations for rest of the uniforms. */
   for (unsigned i = 0; i < num_uniforms; i++) {

      if (uniforms[i].type->is_subroutine() ||
          uniforms[i].is_shader_storage)
         continue;

      /* Built-in uniforms should not get any location. */
      if (uniforms[i].builtin)
         continue;

      /* Explicit ones have been set already. */
      if (uniforms[i].remap_location != UNMAPPED_UNIFORM_LOC)
         continue;

      /* how many new entries for this uniform? */
      const unsigned entries = MAX2(1, uniforms[i].array_elements);

      /* Find UniformRemapTable for empty blocks where we can fit this uniform. */
      int chosen_location = -1;

      if (empty_locs)
         chosen_location = find_empty_block(prog, &uniforms[i]);

      /* Add new entries to the total amount of entries. */
      total_entries += entries;

      if (chosen_location != -1) {
         empty_locs -= entries;
      } else {
         chosen_location = prog->NumUniformRemapTable;

         /* resize remap table to fit new entries */
         prog->UniformRemapTable =
            reralloc(prog,
                     prog->UniformRemapTable,
                     gl_uniform_storage *,
                     prog->NumUniformRemapTable + entries);
         prog->NumUniformRemapTable += entries;
      }

      /* set pointers for this uniform */
      for (unsigned j = 0; j < entries; j++)
         prog->UniformRemapTable[chosen_location + j] = &uniforms[i];

      /* set the base location in remap table for the uniform */
      uniforms[i].remap_location = chosen_location;
   }

   /* Verify that total amount of entries for explicit and implicit locations
    * is less than MAX_UNIFORM_LOCATIONS.
    */

   if (total_entries > max_uniform_locs) {
      linker_error(prog, "count of uniform locations > MAX_UNIFORM_LOCATIONS"
                   "(%u > %u)", total_entries, max_uniform_locs);
   }

   /* Reserve all the explicit locations of the active subroutine uniforms. */
   for (unsigned i = 0; i < num_uniforms; i++) {
      if (!uniforms[i].type->is_subroutine())
         continue;

      if (uniforms[i].remap_location == UNMAPPED_UNIFORM_LOC)
         continue;

      for (unsigned j = 0; j < MESA_SHADER_STAGES; j++) {
         struct gl_linked_shader *sh = prog->_LinkedShaders[j];
         if (!sh)
            continue;

         if (!uniforms[i].opaque[j].active)
            continue;

         /* How many new entries for this uniform? */
         const unsigned entries = MAX2(1, uniforms[i].array_elements);

         /* Set remap table entries point to correct gl_uniform_storage. */
         for (unsigned k = 0; k < entries; k++) {
            unsigned element_loc = uniforms[i].remap_location + k;
            assert(sh->SubroutineUniformRemapTable[element_loc] ==
                   INACTIVE_UNIFORM_EXPLICIT_LOCATION);
            sh->SubroutineUniformRemapTable[element_loc] = &uniforms[i];
         }
      }
   }

   /* reserve subroutine locations */
   for (unsigned i = 0; i < num_uniforms; i++) {

      if (!uniforms[i].type->is_subroutine())
         continue;
      const unsigned entries = MAX2(1, uniforms[i].array_elements);

      if (uniforms[i].remap_location != UNMAPPED_UNIFORM_LOC)
         continue;
      for (unsigned j = 0; j < MESA_SHADER_STAGES; j++) {
         struct gl_linked_shader *sh = prog->_LinkedShaders[j];
         if (!sh)
            continue;

         if (!uniforms[i].opaque[j].active)
            continue;

         sh->SubroutineUniformRemapTable =
            reralloc(sh,
                     sh->SubroutineUniformRemapTable,
                     gl_uniform_storage *,
                     sh->NumSubroutineUniformRemapTable + entries);

         for (unsigned k = 0; k < entries; k++)
            sh->SubroutineUniformRemapTable[sh->NumSubroutineUniformRemapTable + k] = &uniforms[i];
         uniforms[i].remap_location = sh->NumSubroutineUniformRemapTable;
         sh->NumSubroutineUniformRemapTable += entries;
      }
   }

#ifndef NDEBUG
   for (unsigned i = 0; i < num_uniforms; i++) {
      assert(uniforms[i].storage != NULL || uniforms[i].builtin ||
             uniforms[i].is_shader_storage ||
             uniforms[i].block_index != -1);
   }

   assert(parcel.values == data_end);
#endif

   prog->NumUniformStorage = num_uniforms;
   prog->NumHiddenUniforms = hidden_uniforms;
   prog->UniformStorage = uniforms;

   link_set_uniform_initializers(prog, boolean_true);

   return;
}
