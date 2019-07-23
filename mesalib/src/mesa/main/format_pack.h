/*
 * Mesa 3-D graphics library
 *
 * Copyright (c) 2011 VMware, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef FORMAT_PACK_H
#define FORMAT_PACK_H


#include "formats.h"


/** Pack a uint8_t rgba[4] color to dest address */
typedef void (*mesa_pack_ubyte_rgba_func)(const uint8_t src[4], void *dst);

/** Pack a float rgba[4] color to dest address */
typedef void (*mesa_pack_float_rgba_func)(const float src[4], void *dst);

/** Pack a float Z value to dest address */
typedef void (*mesa_pack_float_z_func)(const float *src, void *dst);

/** Pack a uint32_t Z value to dest address */
typedef void (*mesa_pack_uint_z_func)(const uint32_t *src, void *dst);

/** Pack a uint8_t stencil value to dest address */
typedef void (*mesa_pack_ubyte_stencil_func)(const uint8_t *src, void *dst);




extern mesa_pack_ubyte_rgba_func
_mesa_get_pack_ubyte_rgba_function(mesa_format format);


extern mesa_pack_float_rgba_func
_mesa_get_pack_float_rgba_function(mesa_format format);


extern mesa_pack_float_z_func
_mesa_get_pack_float_z_func(mesa_format format);


extern mesa_pack_uint_z_func
_mesa_get_pack_uint_z_func(mesa_format format);


extern mesa_pack_ubyte_stencil_func
_mesa_get_pack_ubyte_stencil_func(mesa_format format);


extern void
_mesa_pack_float_rgba_row(mesa_format format, uint32_t n,
                          const float src[][4], void *dst);

extern void
_mesa_pack_ubyte_rgba_row(mesa_format format, uint32_t n,
                          const uint8_t src[][4], void *dst);

extern void
_mesa_pack_uint_rgba_row(mesa_format format, uint32_t n,
                         const uint32_t src[][4], void *dst);

extern void
_mesa_pack_ubyte_rgba_rect(mesa_format format, uint32_t width, uint32_t height,
                           const uint8_t *src, int32_t srcRowStride,
                           void *dst, int32_t dstRowStride);

extern void
_mesa_pack_float_z_row(mesa_format format, uint32_t n,
                       const float *src, void *dst);

extern void
_mesa_pack_uint_z_row(mesa_format format, uint32_t n,
                      const uint32_t *src, void *dst);

extern void
_mesa_pack_ubyte_stencil_row(mesa_format format, uint32_t n,
                             const uint8_t *src, void *dst);

extern void
_mesa_pack_uint_24_8_depth_stencil_row(mesa_format format, uint32_t n,
                                       const uint32_t *src, void *dst);

#endif
