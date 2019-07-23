/*
 * Copyright © 2019 Google LLC
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

#ifndef _M_VECTOR_ASM_H_
#define _M_VECTOR_ASM_H_

/* This file is a set of defines usable by the old FF TNL assembly code for
 * referencing GLvector4f and GLmatrix structs.
 */

#define VEC_DIRTY_0        0x1
#define VEC_DIRTY_1        0x2
#define VEC_DIRTY_2        0x4
#define VEC_DIRTY_3        0x8

#define VEC_SIZE_1   VEC_DIRTY_0
#define VEC_SIZE_2   (VEC_DIRTY_0|VEC_DIRTY_1)
#define VEC_SIZE_3   (VEC_DIRTY_0|VEC_DIRTY_1|VEC_DIRTY_2)
#define VEC_SIZE_4   (VEC_DIRTY_0|VEC_DIRTY_1|VEC_DIRTY_2|VEC_DIRTY_3)

/* If you add a new field, please add it to the STATIC_ASSERTs in
 * _mesa_vector4f_init().
 */
#define V4F_DATA   0
#define V4F_START  (V4F_DATA + MATH_ASM_PTR_SIZE)
#define V4F_COUNT  (V4F_START + MATH_ASM_PTR_SIZE)
#define V4F_STRIDE (V4F_COUNT + 4)
#define V4F_SIZE   (V4F_STRIDE + 4)
#define V4F_FLAGS  (V4F_SIZE + 4)

/* If you add a new field, please add it to the STATIC_ASSERTs in
 * _math_matrix_set_identity().
 */
#define MATRIX_M   0
#define MATRIX_INV (MATRIX_M + MATH_ASM_PTR_SIZE)

#endif /* _M_VECTOR_ASM_H */
