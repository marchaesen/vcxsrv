/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_UTIL_H_
#define FD6_UTIL_H_

#include "fdl/fd6_format_table.h"
#include "freedreno_resource.h"
#include "freedreno_util.h"

#include "a6xx.xml.h"

enum a6xx_tex_swiz fd6_pipe2swiz(unsigned swiz);

void fd6_tex_swiz(enum pipe_format format, enum a6xx_tile_mode tile_mode, unsigned char *swiz,
                  unsigned swizzle_r, unsigned swizzle_g, unsigned swizzle_b,
                  unsigned swizzle_a);

uint32_t fd6_tex_const_0(struct pipe_resource *prsc, unsigned level,
                         enum pipe_format format, unsigned swizzle_r,
                         unsigned swizzle_g, unsigned swizzle_b,
                         unsigned swizzle_a);

#endif /* FD6_UTIL_H_ */
