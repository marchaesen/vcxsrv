/*
 * Copyright © 2019 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

#ifndef GFX10_FORMAT_TABLE_H
#define GFX10_FORMAT_TABLE_H

#include "pipe/p_format.h"

#include <stdbool.h>

struct gfx10_format {
   unsigned img_format : 9;

   /* Various formats are only supported with workarounds for vertex fetch,
    * and some 32_32_32 formats are supported natively, but only for buffers
    * (possibly with some image support, actually, but no filtering). */
   bool buffers_only : 1;
};

extern const struct gfx10_format gfx10_format_table[PIPE_FORMAT_COUNT];

#endif /* GFX10_FORMAT_TABLE_H */
