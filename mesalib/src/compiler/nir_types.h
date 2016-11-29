/*
 * Copyright © 2014 Connor Abbott
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

#pragma once

#include <stdio.h>
#include <stdbool.h>

/* C wrapper around compiler/glsl_types.h */

#include "glsl_types.h"

#ifdef __cplusplus
extern "C" {
#else
struct glsl_type;
#endif

const char *glsl_get_type_name(const struct glsl_type *type);

const struct glsl_type *glsl_get_struct_field(const struct glsl_type *type,
                                              unsigned index);

const struct glsl_type *glsl_get_array_element(const struct glsl_type *type);
const struct glsl_type *glsl_without_array(const struct glsl_type *type);

const struct glsl_type *glsl_get_column_type(const struct glsl_type *type);

const struct glsl_type *
glsl_get_function_return_type(const struct glsl_type *type);

const struct glsl_function_param *
glsl_get_function_param(const struct glsl_type *type, unsigned index);

enum glsl_base_type glsl_get_base_type(const struct glsl_type *type);

unsigned glsl_get_vector_elements(const struct glsl_type *type);

unsigned glsl_get_components(const struct glsl_type *type);

unsigned glsl_get_matrix_columns(const struct glsl_type *type);

unsigned glsl_get_length(const struct glsl_type *type);

unsigned glsl_get_aoa_size(const struct glsl_type *type);

unsigned glsl_count_attribute_slots(const struct glsl_type *type,
                                    bool is_vertex_input);

const char *glsl_get_struct_elem_name(const struct glsl_type *type,
                                      unsigned index);

enum glsl_sampler_dim glsl_get_sampler_dim(const struct glsl_type *type);
enum glsl_base_type glsl_get_sampler_result_type(const struct glsl_type *type);

unsigned glsl_get_record_location_offset(const struct glsl_type *type,
                                         unsigned length);

static inline unsigned
glsl_get_bit_size(const struct glsl_type *type)
{
   switch (glsl_get_base_type(type)) {
   case GLSL_TYPE_INT:
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_BOOL:
   case GLSL_TYPE_FLOAT: /* TODO handle mediump */
   case GLSL_TYPE_SUBROUTINE:
      return 32;

   case GLSL_TYPE_DOUBLE:
      return 64;

   default:
      unreachable("unknown base type");
   }

   return 0;
}

bool glsl_type_is_void(const struct glsl_type *type);
bool glsl_type_is_error(const struct glsl_type *type);
bool glsl_type_is_vector(const struct glsl_type *type);
bool glsl_type_is_scalar(const struct glsl_type *type);
bool glsl_type_is_vector_or_scalar(const struct glsl_type *type);
bool glsl_type_is_matrix(const struct glsl_type *type);
bool glsl_type_is_array(const struct glsl_type *type);
bool glsl_type_is_array_of_arrays(const struct glsl_type *type);
bool glsl_type_is_struct(const struct glsl_type *type);
bool glsl_type_is_sampler(const struct glsl_type *type);
bool glsl_type_is_image(const struct glsl_type *type);
bool glsl_type_is_dual_slot(const struct glsl_type *type);
bool glsl_type_is_numeric(const struct glsl_type *type);
bool glsl_type_is_boolean(const struct glsl_type *type);
bool glsl_sampler_type_is_shadow(const struct glsl_type *type);
bool glsl_sampler_type_is_array(const struct glsl_type *type);

const struct glsl_type *glsl_void_type(void);
const struct glsl_type *glsl_float_type(void);
const struct glsl_type *glsl_double_type(void);
const struct glsl_type *glsl_vec_type(unsigned n);
const struct glsl_type *glsl_dvec_type(unsigned n);
const struct glsl_type *glsl_vec4_type(void);
const struct glsl_type *glsl_int_type(void);
const struct glsl_type *glsl_uint_type(void);
const struct glsl_type *glsl_bool_type(void);

const struct glsl_type *glsl_scalar_type(enum glsl_base_type base_type);
const struct glsl_type *glsl_vector_type(enum glsl_base_type base_type,
                                         unsigned components);
const struct glsl_type *glsl_matrix_type(enum glsl_base_type base_type,
                                         unsigned rows, unsigned columns);
const struct glsl_type *glsl_array_type(const struct glsl_type *base,
                                        unsigned elements);
const struct glsl_type *glsl_struct_type(const struct glsl_struct_field *fields,
                                         unsigned num_fields, const char *name);
const struct glsl_type *glsl_sampler_type(enum glsl_sampler_dim dim,
                                          bool is_shadow, bool is_array,
                                          enum glsl_base_type base_type);
const struct glsl_type *glsl_bare_sampler_type();
const struct glsl_type *glsl_image_type(enum glsl_sampler_dim dim,
                                        bool is_array,
                                        enum glsl_base_type base_type);
const struct glsl_type * glsl_function_type(const struct glsl_type *return_type,
                                            const struct glsl_function_param *params,
                                            unsigned num_params);

const struct glsl_type *glsl_transposed_type(const struct glsl_type *type);

#ifdef __cplusplus
}
#endif
