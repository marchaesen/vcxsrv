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

#include "nir_types.h"
#include "ir.h"

void
glsl_print_type(const glsl_type *type, FILE *fp)
{
   if (type->base_type == GLSL_TYPE_ARRAY) {
      glsl_print_type(type->fields.array, fp);
      fprintf(fp, "[%u]", type->length);
   } else if ((type->base_type == GLSL_TYPE_STRUCT)
              && !is_gl_identifier(type->name)) {
      fprintf(fp, "%s@%p", type->name, (void *) type);
   } else {
      fprintf(fp, "%s", type->name);
   }
}

void
glsl_print_struct(const glsl_type *type, FILE *fp)
{
   assert(type->base_type == GLSL_TYPE_STRUCT);

   fprintf(fp, "struct {\n");
   for (unsigned i = 0; i < type->length; i++) {
      fprintf(fp, "\t");
      glsl_print_type(type->fields.structure[i].type, fp);
      fprintf(fp, " %s;\n", type->fields.structure[i].name);
   }
   fprintf(fp, "}\n");
}

const glsl_type *
glsl_get_array_element(const glsl_type* type)
{
   if (type->is_matrix())
      return type->column_type();
   return type->fields.array;
}

const glsl_type *
glsl_get_struct_field(const glsl_type *type, unsigned index)
{
   return type->fields.structure[index].type;
}

const struct glsl_type *
glsl_get_column_type(const struct glsl_type *type)
{
   return type->column_type();
}

enum glsl_base_type
glsl_get_base_type(const struct glsl_type *type)
{
   return type->base_type;
}

unsigned
glsl_get_vector_elements(const struct glsl_type *type)
{
   return type->vector_elements;
}

unsigned
glsl_get_components(const struct glsl_type *type)
{
   return type->components();
}

unsigned
glsl_get_matrix_columns(const struct glsl_type *type)
{
   return type->matrix_columns;
}

unsigned
glsl_get_length(const struct glsl_type *type)
{
   return type->length;
}

const char *
glsl_get_struct_elem_name(const struct glsl_type *type, unsigned index)
{
   return type->fields.structure[index].name;
}

bool
glsl_type_is_void(const glsl_type *type)
{
   return type->is_void();
}

bool
glsl_type_is_vector(const struct glsl_type *type)
{
   return type->is_vector();
}

bool
glsl_type_is_scalar(const struct glsl_type *type)
{
   return type->is_scalar();
}

bool
glsl_type_is_matrix(const struct glsl_type *type)
{
   return type->is_matrix();
}

const glsl_type *
glsl_void_type(void)
{
   return glsl_type::void_type;
}

const glsl_type *
glsl_float_type(void)
{
   return glsl_type::float_type;
}

const glsl_type *
glsl_vec4_type(void)
{
   return glsl_type::vec4_type;
}

const glsl_type *
glsl_array_type(const glsl_type *base, unsigned elements)
{
   return glsl_type::get_array_instance(base, elements);
}
