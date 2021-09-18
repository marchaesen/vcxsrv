/*
 * Copyright (C) 2021 Alyssa Rosenzweig
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
 */

#ifndef __AGX_FORMATS_H_
#define __AGX_FORMATS_H_

#include "util/format/u_format.h"
#include "asahi/compiler/agx_compile.h"

struct agx_pixel_format_entry {
   uint16_t hw;
   bool renderable : 1;
   enum agx_format internal : 4;
};

extern const struct agx_pixel_format_entry agx_pixel_format[PIPE_FORMAT_COUNT];
extern const enum agx_format agx_vertex_format[PIPE_FORMAT_COUNT];

/* N.b. hardware=0 corresponds to R8 UNORM, which is renderable. So a zero
 * entry indicates an invalid format. */

static inline bool
agx_is_valid_pixel_format(enum pipe_format format)
{
   struct agx_pixel_format_entry entry = agx_pixel_format[format];
   return (entry.hw != 0) || entry.renderable;
}

#endif
