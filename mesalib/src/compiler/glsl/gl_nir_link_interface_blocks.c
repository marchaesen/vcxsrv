/*
 * Copyright © 2013 Intel Corporation
 * Copyright © 2024 Valve Corporation
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

/**
 * Linker support for GLSL's interface blocks.
 */

#include "gl_nir_linker.h"
#include "linker_util.h"
#include "nir.h"
#include "main/macros.h"
#include "main/shader_types.h"
#include "util/hash_table.h"
#include "util/u_string.h"

/**
 * Change var->interface_type on a variable that previously had a
 * different, but compatible, interface_type.  This is used during linking
 * to set the size of arrays in interface blocks.
 */
static void
change_interface_type(nir_variable *var, const struct glsl_type *type)
{
   if (var->max_ifc_array_access != NULL) {
      /* max_ifc_array_access has already been allocated, so make sure the
       * new interface has the same number of fields as the old one.
       */
      assert(var->interface_type->length == type->length);
   }
   var->interface_type = type;
}

/**
 * If the type pointed to by \c type represents an unsized array, replace
 * it with a sized array whose size is determined by max_array_access.
 */
static void
fixup_type(const struct glsl_type **type, unsigned max_array_access,
           bool from_ssbo_unsized_array, bool *implicit_sized)
{
   if (!from_ssbo_unsized_array && glsl_type_is_unsized_array(*type)) {
      *type = glsl_array_type((*type)->fields.array,
                              max_array_access + 1, (*type)->explicit_stride);
      *implicit_sized = true;
      assert(*type != NULL);
   }
}

static void
fixup_unnamed_interface_type(const void *key, void *data,
                             UNUSED void *closure)
{
   const struct glsl_type *ifc_type = (const struct glsl_type *) key;
   nir_variable **interface_vars = (nir_variable **) data;
   unsigned num_fields = ifc_type->length;
   glsl_struct_field *fields = malloc(sizeof(glsl_struct_field) * num_fields);
   memcpy(fields, ifc_type->fields.structure,
          num_fields * sizeof(*fields));
   bool interface_type_changed = false;
   for (unsigned i = 0; i < num_fields; i++) {
      if (interface_vars[i] != NULL &&
          fields[i].type != interface_vars[i]->type) {
         fields[i].type = interface_vars[i]->type;
         interface_type_changed = true;
      }
   }
   if (!interface_type_changed) {
      free(fields);
      return;
   }
   enum glsl_interface_packing packing =
      (enum glsl_interface_packing) ifc_type->interface_packing;
   bool row_major = (bool) ifc_type->interface_row_major;
   const struct glsl_type *new_ifc_type =
      glsl_interface_type(fields, num_fields, packing,
                          row_major, glsl_get_type_name(ifc_type));
   free(fields);
   for (unsigned i = 0; i < num_fields; i++) {
      if (interface_vars[i] != NULL)
         change_interface_type(interface_vars[i], new_ifc_type);
   }
}

/**
 * Create a new interface type based on the given type, with unsized arrays
 * replaced by sized arrays whose size is determined by
 * max_ifc_array_access.
 */
static const glsl_type *
resize_interface_members(const struct glsl_type *type,
                         const int *max_ifc_array_access,
                         bool is_ssbo)
{
   unsigned num_fields = type->length;
   glsl_struct_field *fields = malloc(sizeof(glsl_struct_field) * num_fields); //new glsl_struct_field[num_fields];
   memcpy(fields, type->fields.structure,
          num_fields * sizeof(*fields));
   for (unsigned i = 0; i < num_fields; i++) {
      bool implicit_sized_array = fields[i].implicit_sized_array;
      /* If SSBO last member is unsized array, we don't replace it by a sized
       * array.
       */
      if (is_ssbo && i == (num_fields - 1))
         fixup_type(&fields[i].type, max_ifc_array_access[i],
                    true, &implicit_sized_array);
      else
         fixup_type(&fields[i].type, max_ifc_array_access[i],
                    false, &implicit_sized_array);
      fields[i].implicit_sized_array = implicit_sized_array;
   }
   enum glsl_interface_packing packing =
      (enum glsl_interface_packing) type->interface_packing;
   bool row_major = (bool) type->interface_row_major;
   const struct glsl_type *new_ifc_type =
      glsl_interface_type(fields, num_fields,
                          packing, row_major, glsl_get_type_name(type));
   free(fields);
   return new_ifc_type;
}

/**
 * Determine whether the given interface type contains unsized arrays (if
 * it doesn't, array_sizing_visitor doesn't need to process it).
 */
static bool
interface_contains_unsized_arrays(const glsl_type *type)
{
   for (unsigned i = 0; i < type->length; i++) {
      const struct glsl_type *elem_type = type->fields.structure[i].type;
      if (glsl_type_is_unsized_array(elem_type))
         return true;
   }

   return false;
}

static const glsl_type *
update_interface_members_array(const glsl_type *type,
                               const glsl_type *new_interface_type)
{
   const struct glsl_type *element_type = type->fields.array;
   if (glsl_type_is_array(element_type)) {
      const glsl_type *new_array_type =
         update_interface_members_array(element_type, new_interface_type);
      return glsl_array_type(new_array_type, type->length,
                             type->explicit_stride);
   } else {
      return glsl_array_type(new_interface_type, type->length,
                             type->explicit_stride);
   }
}

static void
size_variable_array(void *mem_ctx, nir_variable *var,
                    struct hash_table *unnamed_interfaces)
{
   const struct glsl_type *type_without_array;
   const struct glsl_type *ifc_type = var->interface_type;
   bool implicit_sized_array = var->data.implicit_sized_array;

   fixup_type(&var->type, var->data.max_array_access,
              var->data.from_ssbo_unsized_array,
              &implicit_sized_array);
   var->data.implicit_sized_array = implicit_sized_array;
   type_without_array = glsl_without_array(var->type);
   if (glsl_type_is_interface(var->type)) {
      if (interface_contains_unsized_arrays(var->type)) {
         const struct glsl_type *new_type =
            resize_interface_members(var->type,
                                     var->max_ifc_array_access,
                                     var->data.mode == nir_var_mem_ssbo);
         var->type = new_type;
         change_interface_type(var, new_type);
      }
   } else if (glsl_type_is_interface(type_without_array)) {
      if (interface_contains_unsized_arrays(type_without_array)) {
         const struct glsl_type *new_type =
            resize_interface_members(type_without_array,
                                     var->max_ifc_array_access,
                                     var->data.mode == nir_var_mem_ssbo);
         change_interface_type(var, new_type);
         var->type = update_interface_members_array(var->type, new_type);
      }
   } else if (ifc_type) {
      /* Store a pointer to the variable in the unnamed_interfaces
       * hashtable.
       */
      struct hash_entry *entry =
            _mesa_hash_table_search(unnamed_interfaces, ifc_type);

      nir_variable **interface_vars =
         entry ? (nir_variable **) entry->data : NULL;

      if (interface_vars == NULL) {
         interface_vars = rzalloc_array(mem_ctx, nir_variable *,
                                        ifc_type->length);
         _mesa_hash_table_insert(unnamed_interfaces, ifc_type,
                                 interface_vars);
      }
      unsigned index = glsl_get_field_index(ifc_type, var->name);
      assert(index < ifc_type->length);
      assert(interface_vars[index] == NULL);
      interface_vars[index] = var;
   }
}

void
gl_nir_linker_size_arrays(nir_shader *shader)
{
   void *mem_ctx = ralloc_context(NULL);

   /**
    * Hash table from const glsl_type * to an array of nir_variable *'s
    * pointing to the nir_variables constituting each unnamed interface block.
    */
   struct hash_table *unnamed_interfaces =
      _mesa_pointer_hash_table_create(NULL);

   nir_foreach_variable_in_shader(var, shader) {
      size_variable_array(mem_ctx, var, unnamed_interfaces);
   }

   nir_foreach_function_impl(impl, shader) {
      nir_foreach_variable_in_list(var, &impl->locals) {
         size_variable_array(mem_ctx, var, unnamed_interfaces);
      }
   }

   /**
    * For each unnamed interface block that was discovered while running the
    * visitor, adjust the interface type to reflect the newly assigned array
    * sizes, and fix up the nir_variable nodes to point to the new interface
    * type.
    */
   hash_table_call_foreach(unnamed_interfaces,
                           fixup_unnamed_interface_type, NULL);

   _mesa_hash_table_destroy(unnamed_interfaces, NULL);
   ralloc_free(mem_ctx);
}

/**
 * Return true if interface members mismatch and its not allowed by GLSL.
 */
static bool
interstage_member_mismatch(struct gl_shader_program *prog,
                           const struct glsl_type *c,
                           const struct glsl_type *p)
{
   if (c->length != p->length)
      return true;

   for (unsigned i = 0; i < c->length; i++) {
      if (c->fields.structure[i].type != p->fields.structure[i].type)
         return true;
      if (strcmp(c->fields.structure[i].name,
                 p->fields.structure[i].name) != 0)
         return true;
      if (c->fields.structure[i].location !=
          p->fields.structure[i].location)
         return true;
      if (c->fields.structure[i].component !=
          p->fields.structure[i].component)
         return true;
      if (c->fields.structure[i].patch !=
          p->fields.structure[i].patch)
         return true;

      /* From Section 4.5 (Interpolation Qualifiers) of the GLSL 4.40 spec:
       *
       *    "It is a link-time error if, within the same stage, the
       *    interpolation qualifiers of variables of the same name do not
       *    match."
       */
      if (prog->IsES || prog->GLSL_Version < 440)
         if (c->fields.structure[i].interpolation !=
             p->fields.structure[i].interpolation)
            return true;

      /* From Section 4.3.4 (Input Variables) of the GLSL ES 3.0 spec:
       *
       *    "The output of the vertex shader and the input of the fragment
       *    shader form an interface.  For this interface, vertex shader
       *    output variables and fragment shader input variables of the same
       *    name must match in type and qualification (other than precision
       *    and out matching to in).
       *
       * The table in Section 9.2.1 Linked Shaders of the GLSL ES 3.1 spec
       * says that centroid no longer needs to match for varyings.
       *
       * The table in Section 9.2.1 Linked Shaders of the GLSL ES 3.2 spec
       * says that sample need not match for varyings.
       */
      if (!prog->IsES || prog->GLSL_Version < 310)
         if (c->fields.structure[i].centroid !=
             p->fields.structure[i].centroid)
            return true;
      if (!prog->IsES)
         if (c->fields.structure[i].sample !=
             p->fields.structure[i].sample)
            return true;
   }

   return false;
}

static bool
is_interface_instance(nir_variable *var)
{
 return glsl_without_array(var->type) == var->interface_type;
}

/**
 * Check if two interfaces match, according to intrastage interface matching
 * rules.  If they do, and the first interface uses an unsized array, it will
 * be updated to reflect the array size declared in the second interface.
 */
static bool
intrastage_match(nir_variable *a,
                 nir_variable *b,
                 struct gl_shader_program *prog,
                 nir_shader *a_shader,
                 bool match_precision)
{
   /* From section 4.7 "Precision and Precision Qualifiers" in GLSL 4.50:
    *
    *    "For the purposes of determining if an output from one shader
    *    stage matches an input of the next stage, the precision qualifier
    *    need not match."
    */
   bool interface_type_match =
      (prog->IsES ? a->interface_type == b->interface_type :
       glsl_type_compare_no_precision(a->interface_type, b->interface_type));

   /* Types must match. */
   if (!interface_type_match) {
      /* Exception: if both the interface blocks are implicitly declared,
       * don't force their types to match.  They might mismatch due to the two
       * shaders using different GLSL versions, and that's ok.
       */
      if ((a->data.how_declared != nir_var_declared_implicitly ||
           b->data.how_declared != nir_var_declared_implicitly) &&
          (!prog->IsES ||
           interstage_member_mismatch(prog, a->interface_type,
                                      b->interface_type)))
         return false;
   }

   /* Presence/absence of interface names must match. */
   if (is_interface_instance(a) != is_interface_instance(b))
      return false;

   /* For uniforms, instance names need not match.  For shader ins/outs,
    * it's not clear from the spec whether they need to match, but
    * Mesa's implementation relies on them matching.
    */
   if (is_interface_instance(a) && b->data.mode != nir_var_mem_ubo &&
       b->data.mode != nir_var_mem_ssbo &&
       strcmp(a->name, b->name) != 0) {
      return false;
   }

   bool type_match = (match_precision ?
                      a->type == b->type :
                      glsl_type_compare_no_precision(a->type, b->type));

   /* If a block is an array then it must match across the shader.
    * Unsized arrays are also processed and matched agaist sized arrays.
    */
   if (!type_match && (glsl_type_is_array(b->type) || glsl_type_is_array(a->type)) &&
       (is_interface_instance(b) || is_interface_instance(a)) &&
       !gl_nir_validate_intrastage_arrays(prog, b, a, a_shader,
                                          match_precision))
      return false;

   return true;
}

/**
 * Check if two interfaces match, according to interstage (in/out) interface
 * matching rules.
 *
 * If \c extra_array_level is true, the consumer interface is required to be
 * an array and the producer interface is required to be a non-array.
 * This is used for tessellation control and geometry shader consumers.
 */
static bool
interstage_match(struct gl_shader_program *prog, nir_variable *producer,
                 nir_variable *consumer, bool extra_array_level)
{
   /* Types must match. */
   if (consumer->interface_type != producer->interface_type) {
      /* Exception: if both the interface blocks are implicitly declared,
       * don't force their types to match.  They might mismatch due to the two
       * shaders using different GLSL versions, and that's ok.
       *
       * Also we store some member information such as interpolation in
       * glsl_type that doesn't always have to match across shader stages.
       * Therefore we make a pass over the members glsl_struct_field to make
       * sure we don't reject shaders where fields don't need to match.
       */
      if ((consumer->data.how_declared != nir_var_declared_implicitly ||
           producer->data.how_declared != nir_var_declared_implicitly) &&
          interstage_member_mismatch(prog, consumer->interface_type,
                                     producer->interface_type))
         return false;
   }

   /* Ignore outermost array if geom shader */
   const glsl_type *consumer_instance_type;
   if (extra_array_level) {
      consumer_instance_type = glsl_get_array_element(consumer->type);
   } else {
      consumer_instance_type = consumer->type;
   }

   /* If a block is an array then it must match across shaders.
    * Since unsized arrays have been ruled out, we can check this by just
    * making sure the types are equal.
    */
   if ((is_interface_instance(consumer) &&
        glsl_type_is_array(consumer_instance_type)) ||
       (is_interface_instance(producer) &&
        glsl_type_is_array(producer->type))) {
      if (consumer_instance_type != producer->type)
         return false;
   }

   return true;
}

struct ifc_var {
   nir_shader *shader;
   nir_variable *var;
};

/**
 * Lookup the interface definition. Return NULL if none is found.
 */
static struct ifc_var *
ifc_lookup(struct hash_table *ht, nir_variable *var)
{
   if (var->data.explicit_location &&
       var->data.location >= VARYING_SLOT_VAR0) {
      char location_str[11];
      snprintf(location_str, 11, "%d", var->data.location);

      const struct hash_entry *entry =
         _mesa_hash_table_search(ht, location_str);
      return entry ? (struct ifc_var *) entry->data : NULL;
   } else {
      const struct hash_entry *entry =
         _mesa_hash_table_search(ht,
            glsl_get_type_name(glsl_without_array(var->interface_type)));
      return entry ? (struct ifc_var *) entry->data : NULL;
   }
}

/**
 * Add a new interface definition.
 */
static void
ifc_store(void *mem_ctx, struct hash_table *ht, nir_variable *var,
          nir_shader *shader)
{
   struct ifc_var *ifc_var = ralloc(mem_ctx, struct ifc_var);
   ifc_var->var = var;
   ifc_var->shader = shader;

   if (var->data.explicit_location &&
       var->data.location >= VARYING_SLOT_VAR0) {
      /* If explicit location is given then lookup the variable by location.
       * We turn the location into a string and use this as the hash key
       * rather than the name. Note: We allocate enough space for a 32-bit
       * unsigned location value which is overkill but future proof.
       */
      char location_str[11];
      snprintf(location_str, 11, "%d", var->data.location);
      _mesa_hash_table_insert(ht, ralloc_strdup(mem_ctx, location_str), ifc_var);
   } else {
      _mesa_hash_table_insert(ht,
         glsl_get_type_name(glsl_without_array(var->interface_type)), ifc_var);
   }
}

static const glsl_type *
get_interface(const struct gl_linked_shader *shader, char *name,
              nir_variable_mode mode)
{
   nir_foreach_variable_with_modes(var, shader->Program->nir, mode) {
      if (var->type == var->interface_type) {
         const char *ifc_name = glsl_get_type_name(var->interface_type);
         if (strcmp(name, ifc_name) == 0)
            return var->interface_type;
      }
   }

   return NULL;
}

void
gl_nir_validate_intrastage_interface_blocks(struct gl_shader_program *prog,
                                            const struct gl_shader **shader_list,
                                            unsigned num_shaders)
{
   void *mem_ctx = ralloc_context(NULL);

   struct hash_table *in_interfaces =
      _mesa_hash_table_create(mem_ctx, _mesa_hash_string,
                              _mesa_key_string_equal);
   struct hash_table *out_interfaces =
      _mesa_hash_table_create(mem_ctx, _mesa_hash_string,
                              _mesa_key_string_equal);
   struct hash_table *uniform_interfaces =
      _mesa_hash_table_create(mem_ctx, _mesa_hash_string,
                              _mesa_key_string_equal);
   struct hash_table *buffer_interfaces =
      _mesa_hash_table_create(mem_ctx, _mesa_hash_string,
                              _mesa_key_string_equal);

   for (unsigned int i = 0; i < num_shaders; i++) {
      if (shader_list[i] == NULL)
         continue;

      nir_foreach_variable_in_shader(var, shader_list[i]->nir) {
         if (!var->interface_type)
            continue;

         struct hash_table *definitions;
         switch (var->data.mode) {
         case nir_var_shader_in:
            definitions = in_interfaces;
            break;
         case nir_var_shader_out:
            definitions = out_interfaces;
            break;
         case nir_var_mem_ubo:
            definitions = uniform_interfaces;
            break;
         case nir_var_mem_ssbo:
            definitions = buffer_interfaces;
            break;
         default:
            /* Only in, out, and uniform interfaces are legal, so we should
             * never get here.
             */
            assert(!"illegal interface type");
            continue;
         }

         struct ifc_var *ifc_var = ifc_lookup(definitions, var);
         if (ifc_var == NULL) {
            /* This is the first time we've seen the interface, so save
             * it into the appropriate data structure.
             */
            ifc_store(mem_ctx, definitions, var,
                      shader_list[i]->nir);
         } else {
            nir_variable *prev_def = ifc_var->var;
            if (!intrastage_match(prev_def, var, prog, ifc_var->shader,
                                  true /* match_precision */)) {
               linker_error(prog, "definitions of interface block `%s' do not"
                            " match\n", glsl_get_type_name(var->interface_type));
               goto fail;
            }
         }
      }
   }

 fail:
   ralloc_free(mem_ctx);
}

static bool
is_builtin_gl_in_block(nir_variable *var, int consumer_stage)
{
   return !strcmp(var->name, "gl_in") &&
          (consumer_stage == MESA_SHADER_TESS_CTRL ||
           consumer_stage == MESA_SHADER_TESS_EVAL ||
           consumer_stage == MESA_SHADER_GEOMETRY);
}

void
gl_nir_validate_interstage_inout_blocks(struct gl_shader_program *prog,
                                        const struct gl_linked_shader *producer,
                                        const struct gl_linked_shader *consumer)
{
   void *mem_ctx = ralloc_context(NULL);
   struct hash_table *ht = _mesa_hash_table_create(mem_ctx, _mesa_hash_string,
                                                   _mesa_key_string_equal);

   /* VS -> GS, VS -> TCS, VS -> TES, TES -> GS */
   const bool extra_array_level = (producer->Stage == MESA_SHADER_VERTEX &&
                                   consumer->Stage != MESA_SHADER_FRAGMENT) ||
                                  consumer->Stage == MESA_SHADER_GEOMETRY;

   /* Check that block re-declarations of gl_PerVertex are compatible
    * across shaders: From OpenGL Shading Language 4.5, section
    * "7.1 Built-In Language Variables", page 130 of the PDF:
    *
    *    "If multiple shaders using members of a built-in block belonging
    *     to the same interface are linked together in the same program,
    *     they must all redeclare the built-in block in the same way, as
    *     described in section 4.3.9 “Interface Blocks” for interface-block
    *     matching, or a link-time error will result."
    *
    * This is done explicitly outside of iterating the member variable
    * declarations because it is possible that the variables are not used and
    * so they would have been optimised out.
    */
   const glsl_type *consumer_iface =
      get_interface(consumer, "gl_PerVertex", nir_var_shader_in);

   const glsl_type *producer_iface =
      get_interface(producer, "gl_PerVertex", nir_var_shader_out);

   if (producer_iface && consumer_iface &&
       interstage_member_mismatch(prog, consumer_iface, producer_iface)) {
      linker_error(prog, "Incompatible or missing gl_PerVertex re-declaration "
                   "in consecutive shaders");
      ralloc_free(mem_ctx);
      return;
   }

   /* Desktop OpenGL requires redeclaration of the built-in interfaces for
    * SSO programs. Passes above implement following rules:
    *
    * From Section 7.4 (Program Pipeline Objects) of the OpenGL 4.6 Core
    * spec:
    *
    *    "To use any built-in input or output in the gl_PerVertex and
    *     gl_PerFragment blocks in separable program objects, shader code
    *     must redeclare those blocks prior to use.  A separable program
    *     will fail to link if:
    *
    *     it contains multiple shaders of a single type with different
    *     redeclarations of these built-in input and output blocks; or
    *
    *     any shader uses a built-in block member not found in the
    *     redeclaration of that block."
    *
    * ARB_separate_shader_objects issues section (issue #28) states that
    * redeclaration is not required for GLSL shaders using #version 140 or
    * earlier (since interface blocks are not possible with older versions).
    *
    * From Section 7.4.1 (Shader Interface Matching) of the OpenGL ES 3.1
    * spec:
    *
    *    "Built-in inputs or outputs do not affect interface matching."
    *
    * GL_OES_shader_io_blocks adds following:
    *
    *    "When using any built-in input or output in the gl_PerVertex block
    *     in separable program objects, shader code may redeclare that block
    *     prior to use. If the shader does not redeclare the block, the
    *     intrinsically declared definition of that block will be used."
    */

   /* Add output interfaces from the producer to the symbol table. */
   nir_foreach_shader_out_variable(var, producer->Program->nir) {
      if (!var->interface_type)
         continue;

      /* Built-in interface redeclaration check. */
      if (prog->SeparateShader && !prog->IsES && prog->GLSL_Version >= 150 &&
          var->data.how_declared == nir_var_declared_implicitly &&
          var->data.used && !producer_iface) {
         linker_error(prog, "missing output builtin block %s redeclaration "
                      "in separable shader program",
                      glsl_get_type_name(var->interface_type));
         ralloc_free(mem_ctx);
         return;
      }

      ifc_store(mem_ctx, ht, var, producer->Program->nir);
   }

   /* Verify that the consumer's input interfaces match. */
   nir_foreach_shader_in_variable(var, consumer->Program->nir) {
      if (!var->interface_type)
         continue;

      struct ifc_var *ifc_var = ifc_lookup(ht, var);
      nir_variable *producer_def = ifc_var ? ifc_var->var : NULL;

      /* Built-in interface redeclaration check. */
      if (prog->SeparateShader && !prog->IsES && prog->GLSL_Version >= 150 &&
          var->data.how_declared == nir_var_declared_implicitly &&
          var->data.used && !producer_iface) {
         linker_error(prog, "missing input builtin block %s redeclaration "
                      "in separable shader program",
                      glsl_get_type_name(var->interface_type));
         ralloc_free(mem_ctx);
         return;
      }

      /* The producer doesn't generate this input: fail to link. Skip built-in
       * 'gl_in[]' since that may not be present if the producer does not
       * write to any of the pre-defined outputs (e.g. if the vertex shader
       * does not write to gl_Position, etc), which is allowed and results in
       * undefined behavior.
       *
       * From Section 4.3.4 (Inputs) of the GLSL 1.50 spec:
       *
       *    "Only the input variables that are actually read need to be written
       *     by the previous stage; it is allowed to have superfluous
       *     declarations of input variables."
       */
      if (producer_def == NULL &&
          !is_builtin_gl_in_block(var, consumer->Stage) && var->data.used) {
         linker_error(prog, "Input block `%s' is not an output of "
                      "the previous stage\n", glsl_get_type_name(var->interface_type));
         ralloc_free(mem_ctx);
         return;
      }

      if (producer_def &&
          !interstage_match(prog, producer_def, var, extra_array_level)) {
         linker_error(prog, "definitions of interface block `%s' do not "
                      "match\n", glsl_get_type_name(var->interface_type));
         ralloc_free(mem_ctx);
         return;
      }
   }

   ralloc_free(mem_ctx);
}

void
gl_nir_validate_interstage_uniform_blocks(struct gl_shader_program *prog,
                                          struct gl_linked_shader **stages)
{
   void *mem_ctx = ralloc_context(NULL);

   /* Hash table mapping interface block name to a nir_variable */
   struct hash_table *ht = _mesa_hash_table_create(mem_ctx, _mesa_hash_string,
                                                   _mesa_key_string_equal);

   for (int i = 0; i < MESA_SHADER_STAGES; i++) {
      if (stages[i] == NULL)
         continue;

      const struct gl_linked_shader *stage = stages[i];
      nir_foreach_variable_in_shader(var, stage->Program->nir) {
         if (!var->interface_type ||
             (var->data.mode != nir_var_mem_ubo &&
              var->data.mode != nir_var_mem_ssbo))
            continue;

         struct ifc_var *ifc_var = ifc_lookup(ht, var);
         if (ifc_var == NULL) {
            ifc_store(mem_ctx, ht, var, stage->Program->nir);
         } else {
            /* Interstage uniform matching rules are the same as intrastage
             * uniform matchin rules (for uniforms, it is as though all
             * shaders are in the same shader stage).
             */
            nir_variable *old_def = ifc_var->var;
            if (!intrastage_match(old_def, var, prog, ifc_var->shader, false)) {
               linker_error(prog, "definitions of uniform block `%s' do not "
                            "match\n", glsl_get_type_name(var->interface_type));
               ralloc_free(mem_ctx);
               return;
            }
         }
      }
   }

   ralloc_free(mem_ctx);
}
