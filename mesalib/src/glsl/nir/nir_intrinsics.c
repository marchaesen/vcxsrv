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

#include "nir.h"

#define OPCODE(name) nir_intrinsic_##name

#define INTRINSIC(_name, _num_srcs, _src_components, _has_dest, \
                  _dest_components, _num_variables, _num_indices, _flags) \
{ \
   .name = #_name, \
   .num_srcs = _num_srcs, \
   .src_components = _src_components, \
   .has_dest = _has_dest, \
   .dest_components = _dest_components, \
   .num_variables = _num_variables, \
   .num_indices = _num_indices, \
   .flags = _flags \
},

#define LAST_INTRINSIC(name)

const nir_intrinsic_info nir_intrinsic_infos[nir_num_intrinsics] = {
#include "nir_intrinsics.h"
};