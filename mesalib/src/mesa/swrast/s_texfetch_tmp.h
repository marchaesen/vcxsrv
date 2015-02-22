/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2008  Brian Paul   All Rights Reserved.
 * Copyright (c) 2008-2009  VMware, Inc.
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


/**
 * \file texfetch_tmp.h
 * Texel fetch functions template.
 *
 * This template file is used by texfetch.c to generate texel fetch functions
 * for 1-D, 2-D and 3-D texture images.
 *
 * It should be expanded by defining \p DIM as the number texture dimensions
 * (1, 2 or 3).  According to the value of \p DIM a series of macros is defined
 * for the texel lookup in the gl_texture_image::Data.
 *
 * \author Gareth Hughes
 * \author Brian Paul
 */

#include <format_unpack.h>

#if DIM == 1

#define TEXEL_ADDR( type, image, i, j, k, size ) \
	((void) (j), (void) (k), ((type *)(image)->ImageSlices[0] + (i) * (size)))

#define FETCH(x) fetch_texel_1d_##x

#elif DIM == 2

#define TEXEL_ADDR( type, image, i, j, k, size )			\
       ((void) (k),							\
        ((type *)((GLubyte *) (image)->ImageSlices[0] + (image)->RowStride * (j)) + \
          (i) * (size)))

#define FETCH(x) fetch_texel_2d_##x

#elif DIM == 3

#define TEXEL_ADDR( type, image, i, j, k, size )			\
        ((type *)((GLubyte *) (image)->ImageSlices[k] +                      \
                  (image)->RowStride * (j)) + (i) * (size))

#define FETCH(x) fetch_texel_3d_##x

#else
#error	illegal number of texture dimensions
#endif

#define FETCH_Z(x, type, size)                       \
   static void \
   FETCH(x) (const struct swrast_texture_image *texImage, \
             GLint i, GLint j, GLint k, GLfloat *texel) \
   { \
            const type *src = TEXEL_ADDR(type, texImage, i, j, k, size); \
            _mesa_unpack_float_z_row(MESA_FORMAT_##x, 1, src, texel); \
   }

#define FETCH_RGBA(x, type, size)                    \
   static void \
   FETCH(x) (const struct swrast_texture_image *texImage, \
             GLint i, GLint j, GLint k, GLfloat *texel) \
   { \
            const type *src = TEXEL_ADDR(type, texImage, i, j, k, size); \
            _mesa_unpack_rgba_row(MESA_FORMAT_##x, 1, src, (GLvoid *)texel); \
   }

FETCH_Z(Z_UNORM32, GLuint, 1)
FETCH_Z(Z_UNORM16, GLushort, 1)
FETCH_Z(S8_UINT_Z24_UNORM, GLuint, 1) /* only return Z, not stencil data */
FETCH_Z(Z24_UNORM_S8_UINT, GLuint, 1) /* only return Z, not stencil data */
FETCH_Z(Z32_FLOAT_S8X24_UINT, GLfloat, 2)

FETCH_RGBA(RGBA_FLOAT32, GLfloat, 4)
FETCH_RGBA(RGBA_FLOAT16, GLhalfARB, 4)
FETCH_RGBA(RGB_FLOAT32, GLfloat, 3)
FETCH_RGBA(RGB_FLOAT16, GLhalfARB, 3)
FETCH_RGBA(A_FLOAT32, GLfloat, 1)
FETCH_RGBA(A_FLOAT16, GLhalfARB, 1)
FETCH_RGBA(L_FLOAT32, GLfloat, 1)
FETCH_RGBA(L_FLOAT16, GLhalfARB, 1)
FETCH_RGBA(LA_FLOAT32, GLfloat, 2)
FETCH_RGBA(LA_FLOAT16, GLhalfARB, 2)
FETCH_RGBA(I_FLOAT32, GLfloat, 1)
FETCH_RGBA(I_FLOAT16, GLhalfARB, 1)
FETCH_RGBA(R_FLOAT32, GLfloat, 1)
FETCH_RGBA(R_FLOAT16, GLhalfARB, 1)
FETCH_RGBA(RG_FLOAT32, GLfloat, 2)
FETCH_RGBA(RG_FLOAT16, GLhalfARB, 2)
FETCH_RGBA(A8B8G8R8_UNORM, GLuint, 1)
FETCH_RGBA(R8G8B8A8_UNORM, GLuint, 1)
FETCH_RGBA(B8G8R8A8_UNORM, GLuint, 1)
FETCH_RGBA(A8R8G8B8_UNORM, GLuint, 1)
FETCH_RGBA(X8B8G8R8_UNORM, GLuint, 1)
FETCH_RGBA(R8G8B8X8_UNORM, GLuint, 1)
FETCH_RGBA(B8G8R8X8_UNORM, GLuint, 1)
FETCH_RGBA(X8R8G8B8_UNORM, GLuint, 1)
FETCH_RGBA(BGR_UNORM8, GLubyte, 3)
FETCH_RGBA(RGB_UNORM8, GLubyte, 3)
FETCH_RGBA(B5G6R5_UNORM, GLushort, 1)
FETCH_RGBA(R5G6B5_UNORM, GLushort, 1)
FETCH_RGBA(B4G4R4A4_UNORM, GLushort, 1)
FETCH_RGBA(A4R4G4B4_UNORM, GLushort, 1)
FETCH_RGBA(A1B5G5R5_UNORM, GLushort, 1)
FETCH_RGBA(B5G5R5A1_UNORM, GLushort, 1)
FETCH_RGBA(A1R5G5B5_UNORM, GLushort, 1)
FETCH_RGBA(B10G10R10A2_UNORM, GLuint, 1)
FETCH_RGBA(R10G10B10A2_UNORM, GLuint, 1)
FETCH_RGBA(R8G8_UNORM, GLushort, 1)
FETCH_RGBA(G8R8_UNORM, GLushort, 1)
FETCH_RGBA(L4A4_UNORM, GLubyte, 1)
FETCH_RGBA(L8A8_UNORM, GLushort, 1)
FETCH_RGBA(R_UNORM8, GLubyte, 1)
FETCH_RGBA(R_UNORM16, GLushort, 1)
FETCH_RGBA(A8L8_UNORM, GLushort, 1)
FETCH_RGBA(R16G16_UNORM, GLuint, 1)
FETCH_RGBA(G16R16_UNORM, GLuint, 1)
FETCH_RGBA(L16A16_UNORM, GLuint, 1)
FETCH_RGBA(A16L16_UNORM, GLuint, 1)
FETCH_RGBA(B2G3R3_UNORM, GLubyte, 1)
FETCH_RGBA(A_UNORM8, GLubyte, 1)
FETCH_RGBA(A_UNORM16, GLushort, 1)
FETCH_RGBA(L_UNORM8, GLubyte, 1)
FETCH_RGBA(L_UNORM16, GLushort, 1)
FETCH_RGBA(I_UNORM8, GLubyte, 1)
FETCH_RGBA(I_UNORM16, GLushort, 1)
FETCH_RGBA(BGR_SRGB8, GLubyte, 3)
FETCH_RGBA(A8B8G8R8_SRGB, GLuint, 1)
FETCH_RGBA(B8G8R8A8_SRGB, GLuint, 1)
FETCH_RGBA(A8R8G8B8_SRGB, GLuint, 1)
FETCH_RGBA(R8G8B8A8_SRGB, GLuint, 1)
FETCH_RGBA(R8G8B8X8_SRGB, GLuint, 1)
FETCH_RGBA(X8B8G8R8_SRGB, GLuint, 1)
FETCH_RGBA(L_SRGB8, GLubyte, 1)
FETCH_RGBA(L8A8_SRGB, GLushort, 1)
FETCH_RGBA(A8L8_SRGB, GLushort, 2)
FETCH_RGBA(RGBA_SINT8, GLbyte, 4)
FETCH_RGBA(RGBA_SINT16, GLshort, 4)
FETCH_RGBA(RGBA_SINT32, GLint, 4)
FETCH_RGBA(RGBA_UINT8, GLubyte, 4)
FETCH_RGBA(RGBA_UINT16, GLushort, 4)
FETCH_RGBA(RGBA_UINT32, GLuint, 4)
FETCH_RGBA(R_SNORM8, GLbyte, 1)
FETCH_RGBA(A_SNORM8, GLbyte, 1)
FETCH_RGBA(L_SNORM8, GLbyte, 1)
FETCH_RGBA(I_SNORM8, GLbyte, 1)
FETCH_RGBA(R8G8_SNORM, GLshort, 1)
FETCH_RGBA(L8A8_SNORM, GLshort, 1)
FETCH_RGBA(A8L8_SNORM, GLshort, 1)
FETCH_RGBA(X8B8G8R8_SNORM, GLint, 1)
FETCH_RGBA(A8B8G8R8_SNORM, GLint, 1)
FETCH_RGBA(R8G8B8A8_SNORM, GLint, 1)
FETCH_RGBA(R_SNORM16, GLshort, 1)
FETCH_RGBA(A_SNORM16, GLshort, 1)
FETCH_RGBA(L_SNORM16, GLshort, 1)
FETCH_RGBA(I_SNORM16, GLshort, 1)
FETCH_RGBA(R16G16_SNORM, GLshort, 2)
FETCH_RGBA(LA_SNORM16, GLshort, 2)
FETCH_RGBA(RGB_SNORM16, GLshort, 3)
FETCH_RGBA(RGBA_SNORM16, GLshort, 4)
FETCH_RGBA(RGBA_UNORM16, GLushort, 4)
FETCH_RGBA(RGBX_UNORM16, GLushort, 4)
FETCH_RGBA(RGBX_FLOAT16, GLhalfARB, 4)
FETCH_RGBA(RGBX_FLOAT32, GLfloat, 4)
FETCH_RGBA(YCBCR, GLushort, 1) /* Fetch texel from 1D, 2D or 3D ycbcr texture, returning RGBA. */
FETCH_RGBA(YCBCR_REV, GLushort, 1) /* Fetch texel from 1D, 2D or 3D ycbcr texture, returning RGBA. */
FETCH_RGBA(R9G9B9E5_FLOAT, GLuint, 1)
FETCH_RGBA(R11G11B10_FLOAT, GLuint, 1)

#undef TEXEL_ADDR
#undef DIM
#undef FETCH
#undef FETCH_Z
#undef FETCH_RGBA
