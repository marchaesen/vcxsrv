/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SI_COMPUTE_H
#define SI_COMPUTE_H

#include "si_shader.h"
#include "util/u_inlines.h"

struct si_compute {
   struct si_shader_selector sel;
   struct si_shader shader;

   unsigned ir_type;
   unsigned private_size;
   unsigned input_size;

   int max_global_buffers;
   struct pipe_resource **global_buffers;
};

void si_destroy_compute(struct si_compute *program);

static inline void si_compute_reference(struct si_compute **dst, struct si_compute *src)
{
   if (pipe_reference(&(*dst)->sel.base.reference, &src->sel.base.reference))
      si_destroy_compute(*dst);

   *dst = src;
}

#endif /* SI_COMPUTE_H */
