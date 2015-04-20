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

/* C wrapper around glsl_types.h */

#include "../glsl_types.h"

#ifdef __cplusplus
extern "C" {
#else
struct glsl_type;
#endif

#include <stdio.h>

void glsl_print_type(const struct glsl_type *type, FILE *fp);
void glsl_print_struct(const struct glsl_type *type, FILE *fp);

const struct glsl_type *glsl_get_struct_field(const struct glsl_type *type,
                                              unsigned index);

const struct glsl_type *glsl_get_array_element(const struct glsl_type *type);

const struct glsl_type *glsl_get_column_type(const struct glsl_type *type);

enum glsl_base_type glsl_get_base_type(const struct glsl_type *type);

unsigned glsl_get_vector_elements(const struct glsl_type *type);

unsigned glsl_get_components(const struct glsl_type *type);

unsigned glsl_get_matrix_columns(const struct glsl_type *type);

unsigned glsl_get_length(const struct glsl_type *type);

const char *glsl_get_struct_elem_name(const struct glsl_type *type,
                                      unsigned index);


bool glsl_type_is_void(const struct glsl_type *type);
bool glsl_type_is_vector(const struct glsl_type *type);
bool glsl_type_is_scalar(const struct glsl_type *type);
bool glsl_type_is_matrix(const struct glsl_type *type);

const struct glsl_type *glsl_void_type(void);
const struct glsl_type *glsl_float_type(void);
const struct glsl_type *glsl_vec4_type(void);
const struct glsl_type *glsl_array_type(const struct glsl_type *base,
                                        unsigned elements);

#ifdef __cplusplus
}
#endif
