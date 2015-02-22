/**************************************************************************
 *
 * Copyright 2011 Red Hat Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 **************************************************************************/

#ifndef U_FORMAT_FAKE_H_
#define U_FORMAT_FAKE_H_

#define __format_fake(format) \
void \
util_format_##format##_fetch_rgba_8unorm(uint8_t *dst, const uint8_t *src, unsigned i, unsigned j); \
\
void \
util_format_##format##_unpack_rgba_8unorm(uint8_t *dst_row, unsigned dst_stride, const uint8_t *src_row, unsigned src_stride, unsigned width, unsigned height); \
\
void \
util_format_##format##_pack_rgba_8unorm(uint8_t *dst_row, unsigned dst_stride, const uint8_t *src_row, unsigned src_stride, unsigned width, unsigned height); \
\
void \
util_format_##format##_unpack_rgba_float(float *dst_row, unsigned dst_stride, const uint8_t *src_row, unsigned src_stride, unsigned width, unsigned height); \
\
void \
util_format_##format##_pack_rgba_float(uint8_t *dst_row, unsigned dst_stride, const float *src_row, unsigned src_stride, unsigned width, unsigned height); \
\
void \
util_format_##format##_fetch_rgba_float(float *dst, const uint8_t *src, unsigned i, unsigned j);

__format_fake(bptc_rgba_unorm)
__format_fake(bptc_srgba)
__format_fake(bptc_rgb_float)
__format_fake(bptc_rgb_ufloat)

__format_fake(etc2_rgb8)
__format_fake(etc2_srgb8)
__format_fake(etc2_rgb8a1)
__format_fake(etc2_srgb8a1)
__format_fake(etc2_rgba8)
__format_fake(etc2_srgba8)
__format_fake(etc2_r11_unorm)
__format_fake(etc2_r11_snorm)
__format_fake(etc2_rg11_unorm)
__format_fake(etc2_rg11_snorm)

#endif
