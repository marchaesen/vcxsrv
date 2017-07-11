/*
 * Copyright © 2017 Google.
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

#ifndef RADV_DEBUG_H
#define RADV_DEBUG_H

enum {
	RADV_DEBUG_NO_FAST_CLEARS    =   0x1,
	RADV_DEBUG_NO_DCC            =   0x2,
	RADV_DEBUG_DUMP_SHADERS      =   0x4,
	RADV_DEBUG_NO_CACHE          =   0x8,
	RADV_DEBUG_DUMP_SHADER_STATS =  0x10,
	RADV_DEBUG_NO_HIZ            =  0x20,
	RADV_DEBUG_NO_COMPUTE_QUEUE  =  0x40,
	RADV_DEBUG_UNSAFE_MATH       =  0x80,
	RADV_DEBUG_ALL_BOS           = 0x100,
	RADV_DEBUG_NO_IBS            = 0x200,
};

enum {
	RADV_PERFTEST_BATCHCHAIN     =   0x1,
	RADV_PERFTEST_SISCHED        =   0x2,
};
#endif
