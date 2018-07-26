/*
 * Copyright © 2018 Red Hat Inc.
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
 */

#ifndef NIR_BUILTIN_BUILDER_H
#define NIR_BUILTIN_BUILDER_H

#include "nir/nir_builder.h"

/*
 * Functions are sorted alphabetically with removed type and "fast" prefix.
 * Definitions for functions in the C file come first.
 */

nir_ssa_def* nir_cross(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y);
nir_ssa_def* nir_fast_length(nir_builder *b, nir_ssa_def *vec);
nir_ssa_def* nir_smoothstep(nir_builder *b, nir_ssa_def *edge0,
                            nir_ssa_def *edge1, nir_ssa_def *x);

static inline nir_ssa_def *
nir_fclamp(nir_builder *b,
           nir_ssa_def *x, nir_ssa_def *min_val, nir_ssa_def *max_val)
{
   return nir_fmin(b, nir_fmax(b, x, min_val), max_val);
}

static inline nir_ssa_def *
nir_iclamp(nir_builder *b,
           nir_ssa_def *x, nir_ssa_def *min_val, nir_ssa_def *max_val)
{
   return nir_imin(b, nir_imax(b, x, min_val), max_val);
}

static inline nir_ssa_def *
nir_uclamp(nir_builder *b,
           nir_ssa_def *x, nir_ssa_def *min_val, nir_ssa_def *max_val)
{
   return nir_umin(b, nir_umax(b, x, min_val), max_val);
}

static inline nir_ssa_def *
nir_degrees(nir_builder *b, nir_ssa_def *val)
{
   return nir_fmul(b, val, nir_imm_float(b, 57.2957795131));
}

static inline nir_ssa_def *
nir_fast_distance(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   return nir_fast_length(b, nir_fsub(b, x, y));
}

static inline nir_ssa_def*
nir_fast_normalize(nir_builder *b, nir_ssa_def *vec)
{
   return nir_fdiv(b, vec, nir_fast_length(b, vec));
}

static inline nir_ssa_def *
nir_radians(nir_builder *b, nir_ssa_def *val)
{
   return nir_fmul(b, val, nir_imm_float(b, 0.01745329251));
}

#endif /* NIR_BUILTIN_BUILDER_H */
