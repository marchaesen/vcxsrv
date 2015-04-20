#!/usr/bin/env python

from mako.template import Template
from sys import argv

string = """/*
 * Mesa 3-D graphics library
 *
 * Copyright (c) 2011 VMware, Inc.
 * Copyright (c) 2014 Intel Corporation.
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
 * Color, depth, stencil packing functions.
 * Used to pack basic color, depth and stencil formats to specific
 * hardware formats.
 *
 * There are both per-pixel and per-row packing functions:
 * - The former will be used by swrast to write values to the color, depth,
 *   stencil buffers when drawing points, lines and masked spans.
 * - The later will be used for image-oriented functions like glDrawPixels,
 *   glAccum, and glTexImage.
 */

#include <stdint.h>

#include "format_unpack.h"
#include "format_utils.h"
#include "macros.h"
#include "../../gallium/auxiliary/util/u_format_rgb9e5.h"
#include "../../gallium/auxiliary/util/u_format_r11g11b10f.h"
#include "util/format_srgb.h"

#define UNPACK(SRC, OFFSET, BITS) (((SRC) >> (OFFSET)) & MAX_UINT(BITS))

<%
import format_parser as parser

formats = parser.parse(argv[1])

rgb_formats = []
for f in formats:
   if f.name == 'MESA_FORMAT_NONE':
      continue
   if f.colorspace not in ('rgb', 'srgb'):
      continue

   rgb_formats.append(f)
%>

/* float unpacking functions */

%for f in rgb_formats:
   %if f.name in ('MESA_FORMAT_R9G9B9E5_FLOAT', 'MESA_FORMAT_R11G11B10_FLOAT'):
      <% continue %>
   %elif f.is_int() and not f.is_normalized():
      <% continue %>
   %elif f.is_compressed():
      <% continue %>
   %endif

static inline void
unpack_float_${f.short_name()}(const void *void_src, GLfloat dst[4])
{
   ${f.datatype()} *src = (${f.datatype()} *)void_src;
   %if f.layout == parser.PACKED:
      %for c in f.channels:
         %if c.type != 'x':
            ${c.datatype()} ${c.name} = UNPACK(*src, ${c.shift}, ${c.size});
         %endif
      %endfor
   %elif f.layout == parser.ARRAY:
      %for (i, c) in enumerate(f.channels):
         %if c.type != 'x':
            ${c.datatype()} ${c.name} = src[${i}];
         %endif
      %endfor
   %else:
      <% assert False %>
   %endif

   %for i in range(4):
      <% s = f.swizzle[i] %>
      %if 0 <= s and s <= parser.Swizzle.SWIZZLE_W:
         <% c = f.channels[s] %>
         %if c.type == parser.UNSIGNED:
            %if f.colorspace == 'srgb' and c.name in 'rgb':
               <% assert c.size == 8 %>
               dst[${i}] = util_format_srgb_8unorm_to_linear_float(${c.name});
            %else:
               dst[${i}] = _mesa_unorm_to_float(${c.name}, ${c.size});
            %endif
         %elif c.type == parser.SIGNED:
            dst[${i}] = _mesa_snorm_to_float(${c.name}, ${c.size});
         %elif c.type == parser.FLOAT:
            %if c.size == 32:
               dst[${i}] = ${c.name};
            %elif c.size == 16:
               dst[${i}] = _mesa_half_to_float(${c.name});
            %else:
               <% assert False %>
            %endif
         %else:
            <% assert False %>
         %endif
      %elif s == parser.Swizzle.SWIZZLE_ZERO:
         dst[${i}] = 0.0f;
      %elif s == parser.Swizzle.SWIZZLE_ONE:
         dst[${i}] = 1.0f;
      %else:
         <% assert False %>
      %endif
   %endfor
}
%endfor

static void
unpack_float_r9g9b9e5_float(const void *src, GLfloat dst[4])
{
   rgb9e5_to_float3(*(const GLuint *)src, dst);
   dst[3] = 1.0f;
}

static void
unpack_float_r11g11b10_float(const void *src, GLfloat dst[4])
{
   r11g11b10f_to_float3(*(const GLuint *)src, dst);
   dst[3] = 1.0f;
}

static void
unpack_float_ycbcr(const void *src, GLfloat dst[][4], GLuint n)
{
   GLuint i;
   for (i = 0; i < n; i++) {
      const GLushort *src0 = ((const GLushort *) src) + i * 2; /* even */
      const GLushort *src1 = src0 + 1;         /* odd */
      const GLubyte y0 = (*src0 >> 8) & 0xff;  /* luminance */
      const GLubyte cb = *src0 & 0xff;         /* chroma U */
      const GLubyte y1 = (*src1 >> 8) & 0xff;  /* luminance */
      const GLubyte cr = *src1 & 0xff;         /* chroma V */
      const GLubyte y = (i & 1) ? y1 : y0;     /* choose even/odd luminance */
      GLfloat r = 1.164F * (y - 16) + 1.596F * (cr - 128);
      GLfloat g = 1.164F * (y - 16) - 0.813F * (cr - 128) - 0.391F * (cb - 128);
      GLfloat b = 1.164F * (y - 16) + 2.018F * (cb - 128);
      r *= (1.0F / 255.0F);
      g *= (1.0F / 255.0F);
      b *= (1.0F / 255.0F);
      dst[i][0] = CLAMP(r, 0.0F, 1.0F);
      dst[i][1] = CLAMP(g, 0.0F, 1.0F);
      dst[i][2] = CLAMP(b, 0.0F, 1.0F);
      dst[i][3] = 1.0F;
   }
}

static void
unpack_float_ycbcr_rev(const void *src, GLfloat dst[][4], GLuint n)
{
   GLuint i;
   for (i = 0; i < n; i++) {
      const GLushort *src0 = ((const GLushort *) src) + i * 2; /* even */
      const GLushort *src1 = src0 + 1;         /* odd */
      const GLubyte y0 = *src0 & 0xff;         /* luminance */
      const GLubyte cr = (*src0 >> 8) & 0xff;  /* chroma V */
      const GLubyte y1 = *src1 & 0xff;         /* luminance */
      const GLubyte cb = (*src1 >> 8) & 0xff;  /* chroma U */
      const GLubyte y = (i & 1) ? y1 : y0;     /* choose even/odd luminance */
      GLfloat r = 1.164F * (y - 16) + 1.596F * (cr - 128);
      GLfloat g = 1.164F * (y - 16) - 0.813F * (cr - 128) - 0.391F * (cb - 128);
      GLfloat b = 1.164F * (y - 16) + 2.018F * (cb - 128);
      r *= (1.0F / 255.0F);
      g *= (1.0F / 255.0F);
      b *= (1.0F / 255.0F);
      dst[i][0] = CLAMP(r, 0.0F, 1.0F);
      dst[i][1] = CLAMP(g, 0.0F, 1.0F);
      dst[i][2] = CLAMP(b, 0.0F, 1.0F);
      dst[i][3] = 1.0F;
   }
}

/* ubyte packing functions */

%for f in rgb_formats:
   %if not f.is_normalized():
      <% continue %>
   %endif

static inline void
unpack_ubyte_${f.short_name()}(const void *void_src, GLubyte dst[4])
{
   ${f.datatype()} *src = (${f.datatype()} *)void_src;
   %if f.layout == parser.PACKED:
      %for c in f.channels:
         %if c.type != 'x':
            ${c.datatype()} ${c.name} = UNPACK(*src, ${c.shift}, ${c.size});
         %endif
      %endfor
   %elif f.layout == parser.ARRAY:
      %for (i, c) in enumerate(f.channels):
         %if c.type != 'x':
            ${c.datatype()} ${c.name} = src[${i}];
         %endif
      %endfor
   %else:
      <% assert False %>
   %endif

   %for i in range(4):
      <% s = f.swizzle[i] %>
      %if 0 <= s and s <= parser.Swizzle.SWIZZLE_W:
         <% c = f.channels[s] %>
         %if c.type == parser.UNSIGNED:
            %if f.colorspace == 'srgb' and c.name in 'rgb':
               <% assert c.size == 8 %>
               dst[${i}] = util_format_srgb_to_linear_8unorm(${c.name});
            %else:
               dst[${i}] = _mesa_unorm_to_unorm(${c.name}, ${c.size}, 8);
            %endif
         %elif c.type == parser.SIGNED:
            dst[${i}] = _mesa_snorm_to_unorm(${c.name}, ${c.size}, 8);
         %elif c.type == parser.FLOAT:
            %if c.size == 32:
               dst[${i}] = _mesa_float_to_unorm(${c.name}, 8);
            %elif c.size == 16:
               dst[${i}] = _mesa_half_to_unorm(${c.name}, 8);
            %else:
               <% assert False %>
            %endif
         %else:
            <% assert False %>
         %endif
      %elif s == parser.Swizzle.SWIZZLE_ZERO:
         dst[${i}] = 0;
      %elif s == parser.Swizzle.SWIZZLE_ONE:
         dst[${i}] = 255;
      %else:
         <% assert False %>
      %endif
   %endfor
}
%endfor

/* integer packing functions */

%for f in rgb_formats:
   %if not f.is_int():
      <% continue %>
   %elif f.is_normalized():
      <% continue %>
   %endif

static inline void
unpack_int_${f.short_name()}(const void *void_src, GLuint dst[4])
{
   ${f.datatype()} *src = (${f.datatype()} *)void_src;
   %if f.layout == parser.PACKED:
      %for c in f.channels:
         %if c.type != 'x':
            ${c.datatype()} ${c.name} = UNPACK(*src, ${c.shift}, ${c.size});
         %endif
      %endfor
   %elif f.layout == parser.ARRAY:
      %for (i, c) in enumerate(f.channels):
         %if c.type != 'x':
            ${c.datatype()} ${c.name} = src[${i}];
         %endif
      %endfor
   %else:
      <% assert False %>
   %endif

   %for i in range(4):
      <% s = f.swizzle[i] %>
      %if 0 <= s and s <= parser.Swizzle.SWIZZLE_W:
         dst[${i}] = ${f.channels[s].name};
      %elif s == parser.Swizzle.SWIZZLE_ZERO:
         dst[${i}] = 0;
      %elif s == parser.Swizzle.SWIZZLE_ONE:
         dst[${i}] = 1;
      %else:
         <% assert False %>
      %endif
   %endfor
}
%endfor


void
_mesa_unpack_rgba_row(mesa_format format, GLuint n,
                      const void *src, GLfloat dst[][4])
{
   GLubyte *s = (GLubyte *)src;
   GLuint i;

   switch (format) {
%for f in rgb_formats:
   %if f.is_compressed():
      <% continue %>
   %elif f.is_int() and not f.is_normalized():
      <% continue %>
   %endif
   case ${f.name}:
      for (i = 0; i < n; ++i) {
         unpack_float_${f.short_name()}(s, dst[i]);
         s += ${f.block_size() / 8};
      }
      break;
%endfor
   case MESA_FORMAT_YCBCR:
      unpack_float_ycbcr(src, dst, n);
      break;
   case MESA_FORMAT_YCBCR_REV:
      unpack_float_ycbcr_rev(src, dst, n);
      break;
   default:
      _mesa_problem(NULL, "%s: bad format %s", __func__,
                    _mesa_get_format_name(format));
      return;
   }
}

void
_mesa_unpack_ubyte_rgba_row(mesa_format format, GLuint n,
                            const void *src, GLubyte dst[][4])
{
   GLubyte *s = (GLubyte *)src;
   GLuint i;

   switch (format) {
%for f in rgb_formats:
   %if not f.is_normalized():
      <% continue %>
   %endif

   case ${f.name}:
      for (i = 0; i < n; ++i) {
         unpack_ubyte_${f.short_name()}(s, dst[i]);
         s += ${f.block_size() / 8};
      }
      break;
%endfor
   default:
      /* get float values, convert to ubyte */
      {
         GLfloat *tmp = malloc(n * 4 * sizeof(GLfloat));
         if (tmp) {
            GLuint i;
            _mesa_unpack_rgba_row(format, n, src, (GLfloat (*)[4]) tmp);
            for (i = 0; i < n; i++) {
               dst[i][0] = _mesa_float_to_unorm(tmp[i*4+0], 8);
               dst[i][1] = _mesa_float_to_unorm(tmp[i*4+1], 8);
               dst[i][2] = _mesa_float_to_unorm(tmp[i*4+2], 8);
               dst[i][3] = _mesa_float_to_unorm(tmp[i*4+3], 8);
            }
            free(tmp);
         }
      }
      break;
   }
}

void
_mesa_unpack_uint_rgba_row(mesa_format format, GLuint n,
                           const void *src, GLuint dst[][4])
{
   GLubyte *s = (GLubyte *)src;
   GLuint i;

   switch (format) {
%for f in rgb_formats:
   %if not f.is_int():
      <% continue %>
   %elif f.is_normalized():
      <% continue %>
   %endif

   case ${f.name}:
      for (i = 0; i < n; ++i) {
         unpack_int_${f.short_name()}(s, dst[i]);
         s += ${f.block_size() / 8};
      }
      break;
%endfor
   default:
      _mesa_problem(NULL, "%s: bad format %s", __func__,
                    _mesa_get_format_name(format));
      return;
   }
}

/**
 * Unpack a 2D rect of pixels returning float RGBA colors.
 * \param format  the source image format
 * \param src  start address of the source image
 * \param srcRowStride  source image row stride in bytes
 * \param dst  start address of the dest image
 * \param dstRowStride  dest image row stride in bytes
 * \param x  source image start X pos
 * \param y  source image start Y pos
 * \param width  width of rect region to convert
 * \param height  height of rect region to convert
 */
void
_mesa_unpack_rgba_block(mesa_format format,
                        const void *src, GLint srcRowStride,
                        GLfloat dst[][4], GLint dstRowStride,
                        GLuint x, GLuint y, GLuint width, GLuint height)
{
   const GLuint srcPixStride = _mesa_get_format_bytes(format);
   const GLuint dstPixStride = 4 * sizeof(GLfloat);
   const GLubyte *srcRow;
   GLubyte *dstRow;
   GLuint i;

   /* XXX needs to be fixed for compressed formats */

   srcRow = ((const GLubyte *) src) + srcRowStride * y + srcPixStride * x;
   dstRow = ((GLubyte *) dst) + dstRowStride * y + dstPixStride * x;

   for (i = 0; i < height; i++) {
      _mesa_unpack_rgba_row(format, width, srcRow, (GLfloat (*)[4]) dstRow);

      dstRow += dstRowStride;
      srcRow += srcRowStride;
   }
}

/** Helper struct for MESA_FORMAT_Z32_FLOAT_S8X24_UINT */
struct z32f_x24s8
{
   float z;
   uint32_t x24s8;
};

typedef void (*unpack_float_z_func)(GLuint n, const void *src, GLfloat *dst);

static void
unpack_float_z_X8_UINT_Z24_UNORM(GLuint n, const void *src, GLfloat *dst)
{
   /* only return Z, not stencil data */
   const GLuint *s = ((const GLuint *) src);
   const GLdouble scale = 1.0 / (GLdouble) 0xffffff;
   GLuint i;
   for (i = 0; i < n; i++) {
      dst[i] = (GLfloat) ((s[i] >> 8) * scale);
      assert(dst[i] >= 0.0F);
      assert(dst[i] <= 1.0F);
   }
}

static void
unpack_float_z_Z24_UNORM_X8_UINT(GLuint n, const void *src, GLfloat *dst)
{
   /* only return Z, not stencil data */
   const GLuint *s = ((const GLuint *) src);
   const GLdouble scale = 1.0 / (GLdouble) 0xffffff;
   GLuint i;
   for (i = 0; i < n; i++) {
      dst[i] = (GLfloat) ((s[i] & 0x00ffffff) * scale);
      assert(dst[i] >= 0.0F);
      assert(dst[i] <= 1.0F);
   }
}

static void
unpack_float_Z_UNORM16(GLuint n, const void *src, GLfloat *dst)
{
   const GLushort *s = ((const GLushort *) src);
   GLuint i;
   for (i = 0; i < n; i++) {
      dst[i] = s[i] * (1.0F / 65535.0F);
   }
}

static void
unpack_float_Z_UNORM32(GLuint n, const void *src, GLfloat *dst)
{
   const GLuint *s = ((const GLuint *) src);
   GLuint i;
   for (i = 0; i < n; i++) {
      dst[i] = s[i] * (1.0F / 0xffffffff);
   }
}

static void
unpack_float_Z_FLOAT32(GLuint n, const void *src, GLfloat *dst)
{
   memcpy(dst, src, n * sizeof(float));
}

static void
unpack_float_z_Z32X24S8(GLuint n, const void *src, GLfloat *dst)
{
   const struct z32f_x24s8 *s = (const struct z32f_x24s8 *) src;
   GLuint i;
   for (i = 0; i < n; i++) {
      dst[i] = s[i].z;
   }
}



/**
 * Unpack Z values.
 * The returned values will always be in the range [0.0, 1.0].
 */
void
_mesa_unpack_float_z_row(mesa_format format, GLuint n,
                         const void *src, GLfloat *dst)
{
   unpack_float_z_func unpack;

   switch (format) {
   case MESA_FORMAT_S8_UINT_Z24_UNORM:
   case MESA_FORMAT_X8_UINT_Z24_UNORM:
      unpack = unpack_float_z_X8_UINT_Z24_UNORM;
      break;
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
   case MESA_FORMAT_Z24_UNORM_X8_UINT:
      unpack = unpack_float_z_Z24_UNORM_X8_UINT;
      break;
   case MESA_FORMAT_Z_UNORM16:
      unpack = unpack_float_Z_UNORM16;
      break;
   case MESA_FORMAT_Z_UNORM32:
      unpack = unpack_float_Z_UNORM32;
      break;
   case MESA_FORMAT_Z_FLOAT32:
      unpack = unpack_float_Z_FLOAT32;
      break;
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      unpack = unpack_float_z_Z32X24S8;
      break;
   default:
      _mesa_problem(NULL, "bad format %s in _mesa_unpack_float_z_row",
                    _mesa_get_format_name(format));
      return;
   }

   unpack(n, src, dst);
}



typedef void (*unpack_uint_z_func)(const void *src, GLuint *dst, GLuint n);

static void
unpack_uint_z_X8_UINT_Z24_UNORM(const void *src, GLuint *dst, GLuint n)
{
   /* only return Z, not stencil data */
   const GLuint *s = ((const GLuint *) src);
   GLuint i;
   for (i = 0; i < n; i++) {
      dst[i] = (s[i] & 0xffffff00) | (s[i] >> 24);
   }
}

static void
unpack_uint_z_Z24_UNORM_X8_UINT(const void *src, GLuint *dst, GLuint n)
{
   /* only return Z, not stencil data */
   const GLuint *s = ((const GLuint *) src);
   GLuint i;
   for (i = 0; i < n; i++) {
      dst[i] = (s[i] << 8) | ((s[i] >> 16) & 0xff);
   }
}

static void
unpack_uint_Z_UNORM16(const void *src, GLuint *dst, GLuint n)
{
   const GLushort *s = ((const GLushort *)src);
   GLuint i;
   for (i = 0; i < n; i++) {
      dst[i] = (s[i] << 16) | s[i];
   }
}

static void
unpack_uint_Z_UNORM32(const void *src, GLuint *dst, GLuint n)
{
   memcpy(dst, src, n * sizeof(GLuint));
}

static void
unpack_uint_Z_FLOAT32(const void *src, GLuint *dst, GLuint n)
{
   const float *s = (const float *)src;
   GLuint i;
   for (i = 0; i < n; i++) {
      dst[i] = FLOAT_TO_UINT(CLAMP(s[i], 0.0F, 1.0F));
   }
}

static void
unpack_uint_Z_FLOAT32_X24S8(const void *src, GLuint *dst, GLuint n)
{
   const struct z32f_x24s8 *s = (const struct z32f_x24s8 *) src;
   GLuint i;

   for (i = 0; i < n; i++) {
      dst[i] = FLOAT_TO_UINT(CLAMP(s[i].z, 0.0F, 1.0F));
   }
}


/**
 * Unpack Z values.
 * The returned values will always be in the range [0, 0xffffffff].
 */
void
_mesa_unpack_uint_z_row(mesa_format format, GLuint n,
                        const void *src, GLuint *dst)
{
   unpack_uint_z_func unpack;
   const GLubyte *srcPtr = (GLubyte *) src;

   switch (format) {
   case MESA_FORMAT_S8_UINT_Z24_UNORM:
   case MESA_FORMAT_X8_UINT_Z24_UNORM:
      unpack = unpack_uint_z_X8_UINT_Z24_UNORM;
      break;
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
   case MESA_FORMAT_Z24_UNORM_X8_UINT:
      unpack = unpack_uint_z_Z24_UNORM_X8_UINT;
      break;
   case MESA_FORMAT_Z_UNORM16:
      unpack = unpack_uint_Z_UNORM16;
      break;
   case MESA_FORMAT_Z_UNORM32:
      unpack = unpack_uint_Z_UNORM32;
      break;
   case MESA_FORMAT_Z_FLOAT32:
      unpack = unpack_uint_Z_FLOAT32;
      break;
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      unpack = unpack_uint_Z_FLOAT32_X24S8;
      break;
   default:
      _mesa_problem(NULL, "bad format %s in _mesa_unpack_uint_z_row",
                    _mesa_get_format_name(format));
      return;
   }

   unpack(srcPtr, dst, n);
}


static void
unpack_ubyte_s_S_UINT8(const void *src, GLubyte *dst, GLuint n)
{
   memcpy(dst, src, n);
}

static void
unpack_ubyte_s_S8_UINT_Z24_UNORM(const void *src, GLubyte *dst, GLuint n)
{
   GLuint i;
   const GLuint *src32 = src;

   for (i = 0; i < n; i++)
      dst[i] = src32[i] & 0xff;
}

static void
unpack_ubyte_s_Z24_UNORM_S8_UINT(const void *src, GLubyte *dst, GLuint n)
{
   GLuint i;
   const GLuint *src32 = src;

   for (i = 0; i < n; i++)
      dst[i] = src32[i] >> 24;
}

static void
unpack_ubyte_s_Z32_FLOAT_S8X24_UINT(const void *src, GLubyte *dst, GLuint n)
{
   GLuint i;
   const struct z32f_x24s8 *s = (const struct z32f_x24s8 *) src;

   for (i = 0; i < n; i++)
      dst[i] = s[i].x24s8 & 0xff;
}

void
_mesa_unpack_ubyte_stencil_row(mesa_format format, GLuint n,
			       const void *src, GLubyte *dst)
{
   switch (format) {
   case MESA_FORMAT_S_UINT8:
      unpack_ubyte_s_S_UINT8(src, dst, n);
      break;
   case MESA_FORMAT_S8_UINT_Z24_UNORM:
      unpack_ubyte_s_S8_UINT_Z24_UNORM(src, dst, n);
      break;
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
      unpack_ubyte_s_Z24_UNORM_S8_UINT(src, dst, n);
      break;
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      unpack_ubyte_s_Z32_FLOAT_S8X24_UINT(src, dst, n);
      break;
   default:
      _mesa_problem(NULL, "bad format %s in _mesa_unpack_ubyte_s_row",
                    _mesa_get_format_name(format));
      return;
   }
}

static void
unpack_uint_24_8_depth_stencil_Z24_UNORM_S8_UINT(const GLuint *src, GLuint *dst, GLuint n)
{
   GLuint i;

   for (i = 0; i < n; i++) {
      GLuint val = src[i];
      dst[i] = val >> 24 | val << 8;
   }
}

static void
unpack_uint_24_8_depth_stencil_Z32_S8X24(const GLuint *src,
                                         GLuint *dst, GLuint n)
{
   GLuint i;

   for (i = 0; i < n; i++) {
      /* 8 bytes per pixel (float + uint32) */
      GLfloat zf = ((GLfloat *) src)[i * 2 + 0];
      GLuint z24 = (GLuint) (zf * (GLfloat) 0xffffff);
      GLuint s = src[i * 2 + 1] & 0xff;
      dst[i] = (z24 << 8) | s;
   }
}

static void
unpack_uint_24_8_depth_stencil_S8_UINT_Z24_UNORM(const GLuint *src, GLuint *dst, GLuint n)
{
   memcpy(dst, src, n * 4);
}

/**
 * Unpack depth/stencil returning as GL_UNSIGNED_INT_24_8.
 * \param format  the source data format
 */
void
_mesa_unpack_uint_24_8_depth_stencil_row(mesa_format format, GLuint n,
					 const void *src, GLuint *dst)
{
   switch (format) {
   case MESA_FORMAT_S8_UINT_Z24_UNORM:
      unpack_uint_24_8_depth_stencil_S8_UINT_Z24_UNORM(src, dst, n);
      break;
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
      unpack_uint_24_8_depth_stencil_Z24_UNORM_S8_UINT(src, dst, n);
      break;
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      unpack_uint_24_8_depth_stencil_Z32_S8X24(src, dst, n);
      break;
   default:
      _mesa_problem(NULL,
                    "bad format %s in _mesa_unpack_uint_24_8_depth_stencil_row",
                    _mesa_get_format_name(format));
      return;
   }
}

static void
unpack_float_32_uint_24_8_Z24_UNORM_S8_UINT(const GLuint *src,
                                            GLuint *dst, GLuint n)
{
   GLuint i;
   struct z32f_x24s8 *d = (struct z32f_x24s8 *) dst;
   const GLdouble scale = 1.0 / (GLdouble) 0xffffff;

   for (i = 0; i < n; i++) {
      const GLuint z24 = src[i] & 0xffffff;
      d[i].z = z24 * scale;
      d[i].x24s8 = src[i] >> 24;
      assert(d[i].z >= 0.0f);
      assert(d[i].z <= 1.0f);
   }
}

static void
unpack_float_32_uint_24_8_Z32_FLOAT_S8X24_UINT(const GLuint *src,
                                               GLuint *dst, GLuint n)
{
   memcpy(dst, src, n * sizeof(struct z32f_x24s8));
}

static void
unpack_float_32_uint_24_8_S8_UINT_Z24_UNORM(const GLuint *src,
                                            GLuint *dst, GLuint n)
{
   GLuint i;
   struct z32f_x24s8 *d = (struct z32f_x24s8 *) dst;
   const GLdouble scale = 1.0 / (GLdouble) 0xffffff;

   for (i = 0; i < n; i++) {
      const GLuint z24 = src[i] >> 8;
      d[i].z = z24 * scale;
      d[i].x24s8 = src[i] & 0xff;
      assert(d[i].z >= 0.0f);
      assert(d[i].z <= 1.0f);
   }
}

/**
 * Unpack depth/stencil returning as GL_FLOAT_32_UNSIGNED_INT_24_8_REV.
 * \param format  the source data format
 *
 * In GL_FLOAT_32_UNSIGNED_INT_24_8_REV lower 4 bytes contain float
 * component and higher 4 bytes contain packed 24-bit and 8-bit
 * components.
 *
 *    31 30 29 28 ... 4 3 2 1 0    31 30 29 ... 9 8 7 6 5 ... 2 1 0
 *    +-------------------------+  +--------------------------------+
 *    |    Float Component      |  | Unused         | 8 bit stencil |
 *    +-------------------------+  +--------------------------------+
 *          lower 4 bytes                  higher 4 bytes
 */
void
_mesa_unpack_float_32_uint_24_8_depth_stencil_row(mesa_format format, GLuint n,
			                          const void *src, GLuint *dst)
{
   switch (format) {
   case MESA_FORMAT_S8_UINT_Z24_UNORM:
      unpack_float_32_uint_24_8_S8_UINT_Z24_UNORM(src, dst, n);
      break;
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
      unpack_float_32_uint_24_8_Z24_UNORM_S8_UINT(src, dst, n);
      break;
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      unpack_float_32_uint_24_8_Z32_FLOAT_S8X24_UINT(src, dst, n);
      break;
   default:
      _mesa_problem(NULL,
                    "bad format %s in _mesa_unpack_uint_24_8_depth_stencil_row",
                    _mesa_get_format_name(format));
      return;
   }
}

/**
 * Unpack depth/stencil
 * \param format  the source data format
 * \param type the destination data type
 */
void
_mesa_unpack_depth_stencil_row(mesa_format format, GLuint n,
	                       const void *src, GLenum type,
                               GLuint *dst)
{
   assert(type == GL_UNSIGNED_INT_24_8 ||
          type == GL_FLOAT_32_UNSIGNED_INT_24_8_REV);

   switch (type) {
   case GL_UNSIGNED_INT_24_8:
      _mesa_unpack_uint_24_8_depth_stencil_row(format, n, src, dst);
      break;
   case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
      _mesa_unpack_float_32_uint_24_8_depth_stencil_row(format, n, src, dst);
      break;
   default:
      _mesa_problem(NULL,
                    "bad type 0x%x in _mesa_unpack_depth_stencil_row",
                    type);
      return;
   }
}
"""

template = Template(string);

print template.render(argv = argv[0:])
