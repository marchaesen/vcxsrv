/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef NIR_TO_DXIL_H
#define NIR_TO_DXIL_H

#include <stdbool.h>

#include "nir.h"

#ifdef __cplusplus
extern "C" {
#endif

struct blob;

enum dxil_sysvalue_type {
   DXIL_NO_SYSVALUE = 0,
   DXIL_SYSVALUE,
   DXIL_GENERATED_SYSVALUE
};

enum dxil_sysvalue_type
nir_var_to_dxil_sysvalue_type(nir_variable *var, uint64_t other_stage_mask);

struct nir_to_dxil_options {
   bool interpolate_at_vertex;
   bool lower_int16;
   bool disable_math_refactoring;
   unsigned ubo_binding_offset;
   unsigned provoking_vertex;
   unsigned num_kernel_globals;
   bool vulkan_environment;
};

bool
nir_to_dxil(struct nir_shader *s, const struct nir_to_dxil_options *opts,
            struct blob *blob);

const nir_shader_compiler_options*
dxil_get_nir_compiler_options(void);

#ifdef __cplusplus
}
#endif

#endif
