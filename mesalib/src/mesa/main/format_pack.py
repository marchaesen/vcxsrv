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

#include "format_pack.h"
#include "format_utils.h"
#include "macros.h"
#include "util/format_rgb9e5.h"
#include "util/format_r11g11b10f.h"
#include "util/format_srgb.h"

#define UNPACK(SRC, OFFSET, BITS) (((SRC) >> (OFFSET)) & MAX_UINT(BITS))
#define PACK(SRC, OFFSET, BITS) (((SRC) & MAX_UINT(BITS)) << (OFFSET))

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

/* ubyte packing functions */

%for f in rgb_formats:
   %if f.name in ('MESA_FORMAT_R9G9B9E5_FLOAT', 'MESA_FORMAT_R11G11B10_FLOAT'):
      <% continue %>
   %elif f.is_compressed():
      <% continue %>
   %endif

static inline void
pack_ubyte_${f.short_name()}(const GLubyte src[4], void *dst)
{
   %for (i, c) in enumerate(f.channels):
      <% i = f.swizzle.inverse()[i] %>
      %if c.type == 'x':
         <% continue %>
      %endif

      ${c.datatype()} ${c.name} =
      %if not f.is_normalized() and f.is_int():
          %if c.type == parser.SIGNED:
              _mesa_unsigned_to_signed(src[${i}], ${c.size});
          %else:
              _mesa_unsigned_to_unsigned(src[${i}], ${c.size});
          %endif
      %elif c.type == parser.UNSIGNED:
         %if f.colorspace == 'srgb' and c.name in 'rgb':
            <% assert c.size == 8 %>
            util_format_linear_to_srgb_8unorm(src[${i}]);
         %else:
            _mesa_unorm_to_unorm(src[${i}], 8, ${c.size});
         %endif
      %elif c.type == parser.SIGNED:
         _mesa_unorm_to_snorm(src[${i}], 8, ${c.size});
      %elif c.type == parser.FLOAT:
         %if c.size == 32:
            _mesa_unorm_to_float(src[${i}], 8);
         %elif c.size == 16:
            _mesa_unorm_to_half(src[${i}], 8);
         %else:
            <% assert False %>
         %endif
      %else:
         <% assert False %>
      %endif
   %endfor

   %if f.layout == parser.ARRAY:
      ${f.datatype()} *d = (${f.datatype()} *)dst;
      %for (i, c) in enumerate(f.channels):
         %if c.type == 'x':
            <% continue %>
         %endif
         d[${i}] = ${c.name};
      %endfor
   %elif f.layout == parser.PACKED:
      ${f.datatype()} d = 0;
      %for (i, c) in enumerate(f.channels):
         %if c.type == 'x':
            <% continue %>
         %endif
         d |= PACK(${c.name}, ${c.shift}, ${c.size});
      %endfor
      (*(${f.datatype()} *)dst) = d;
   %else:
      <% assert False %>
   %endif
}
%endfor

static inline void
pack_ubyte_r9g9b9e5_float(const GLubyte src[4], void *dst)
{
   GLuint *d = (GLuint *) dst;
   GLfloat rgb[3];
   rgb[0] = _mesa_unorm_to_float(src[RCOMP], 8);
   rgb[1] = _mesa_unorm_to_float(src[GCOMP], 8);
   rgb[2] = _mesa_unorm_to_float(src[BCOMP], 8);
   *d = float3_to_rgb9e5(rgb);
}

static inline void
pack_ubyte_r11g11b10_float(const GLubyte src[4], void *dst)
{
   GLuint *d = (GLuint *) dst;
   GLfloat rgb[3];
   rgb[0] = _mesa_unorm_to_float(src[RCOMP], 8);
   rgb[1] = _mesa_unorm_to_float(src[GCOMP], 8);
   rgb[2] = _mesa_unorm_to_float(src[BCOMP], 8);
   *d = float3_to_r11g11b10f(rgb);
}

/* uint packing functions */

%for f in rgb_formats:
   %if not f.is_int():
      <% continue %>
   %elif f.is_normalized():
      <% continue %>
   %elif f.is_compressed():
      <% continue %>
   %endif

static inline void
pack_uint_${f.short_name()}(const GLuint src[4], void *dst)
{
   %for (i, c) in enumerate(f.channels):
      <% i = f.swizzle.inverse()[i] %>
      %if c.type == 'x':
         <% continue %>
      %endif

      ${c.datatype()} ${c.name} =
      %if c.type == parser.SIGNED:
         _mesa_signed_to_signed(src[${i}], ${c.size});
      %elif c.type == parser.UNSIGNED:
         _mesa_unsigned_to_unsigned(src[${i}], ${c.size});
      %else:
         assert(!"Invalid type: only integer types are allowed");
      %endif
   %endfor

   %if f.layout == parser.ARRAY:
      ${f.datatype()} *d = (${f.datatype()} *)dst;
      %for (i, c) in enumerate(f.channels):
         %if c.type == 'x':
            <% continue %>
         %endif
         d[${i}] = ${c.name};
      %endfor
   %elif f.layout == parser.PACKED:
      ${f.datatype()} d = 0;
      %for (i, c) in enumerate(f.channels):
         %if c.type == 'x':
            <% continue %>
         %endif
         d |= PACK(${c.name}, ${c.shift}, ${c.size});
      %endfor
      (*(${f.datatype()} *)dst) = d;
   %else:
      <% assert False %>
   %endif
}
%endfor

/* float packing functions */

%for f in rgb_formats:
   %if f.name in ('MESA_FORMAT_R9G9B9E5_FLOAT', 'MESA_FORMAT_R11G11B10_FLOAT'):
      <% continue %>
   %elif f.is_int() and not f.is_normalized():
      <% continue %>
   %elif f.is_compressed():
      <% continue %>
   %endif

static inline void
pack_float_${f.short_name()}(const GLfloat src[4], void *dst)
{
   %for (i, c) in enumerate(f.channels):
      <% i = f.swizzle.inverse()[i] %>
      %if c.type == 'x':
         <% continue %>
      %endif

      ${c.datatype()} ${c.name} =
      %if c.type == parser.UNSIGNED:
         %if f.colorspace == 'srgb' and c.name in 'rgb':
            <% assert c.size == 8 %>
            util_format_linear_float_to_srgb_8unorm(src[${i}]);
         %else:
            _mesa_float_to_unorm(src[${i}], ${c.size});
         %endif
      %elif c.type == parser.SIGNED:
         _mesa_float_to_snorm(src[${i}], ${c.size});
      %elif c.type == parser.FLOAT:
         %if c.size == 32:
            src[${i}];
         %elif c.size == 16:
            _mesa_float_to_half(src[${i}]);
         %else:
            <% assert False %>
         %endif
      %else:
         <% assert False %>
      %endif
   %endfor

   %if f.layout == parser.ARRAY:
      ${f.datatype()} *d = (${f.datatype()} *)dst;
      %for (i, c) in enumerate(f.channels):
         %if c.type == 'x':
            <% continue %>
         %endif
         d[${i}] = ${c.name};
      %endfor
   %elif f.layout == parser.PACKED:
      ${f.datatype()} d = 0;
      %for (i, c) in enumerate(f.channels):
         %if c.type == 'x':
            <% continue %>
         %endif
         d |= PACK(${c.name}, ${c.shift}, ${c.size});
      %endfor
      (*(${f.datatype()} *)dst) = d;
   %else:
      <% assert False %>
   %endif
}
%endfor

static inline void
pack_float_r9g9b9e5_float(const GLfloat src[4], void *dst)
{
   GLuint *d = (GLuint *) dst;
   *d = float3_to_rgb9e5(src);
}

static inline void
pack_float_r11g11b10_float(const GLfloat src[4], void *dst)
{
   GLuint *d = (GLuint *) dst;
   *d = float3_to_r11g11b10f(src);
}

/**
 * Return a function that can pack a GLubyte rgba[4] color.
 */
gl_pack_ubyte_rgba_func
_mesa_get_pack_ubyte_rgba_function(mesa_format format)
{
   switch (format) {
%for f in rgb_formats:
   %if f.is_compressed():
      <% continue %>
   %endif

   case ${f.name}:
      return pack_ubyte_${f.short_name()};
%endfor
   default:
      return NULL;
   }
}

/**
 * Return a function that can pack a GLfloat rgba[4] color.
 */
gl_pack_float_rgba_func
_mesa_get_pack_float_rgba_function(mesa_format format)
{
   switch (format) {
%for f in rgb_formats:
   %if f.is_compressed():
      <% continue %>
   %elif f.is_int() and not f.is_normalized():
      <% continue %>
   %endif

   case ${f.name}:
      return pack_float_${f.short_name()};
%endfor
   default:
      return NULL;
   }
}

/**
 * Pack a row of GLubyte rgba[4] values to the destination.
 */
void
_mesa_pack_ubyte_rgba_row(mesa_format format, GLuint n,
                          const GLubyte src[][4], void *dst)
{
   GLuint i;
   GLubyte *d = dst;

   switch (format) {
%for f in rgb_formats:
   %if f.is_compressed():
      <% continue %>
   %endif

   case ${f.name}:
      for (i = 0; i < n; ++i) {
         pack_ubyte_${f.short_name()}(src[i], d);
         d += ${f.block_size() / 8};
      }
      break;
%endfor
   default:
      assert(!"Invalid format");
   }
}

/**
 * Pack a row of GLuint rgba[4] values to the destination.
 */
void
_mesa_pack_uint_rgba_row(mesa_format format, GLuint n,
                          const GLuint src[][4], void *dst)
{
   GLuint i;
   GLubyte *d = dst;

   switch (format) {
%for f in rgb_formats:
   %if not f.is_int():
      <% continue %>
   %elif f.is_normalized():
      <% continue %>
   %elif f.is_compressed():
      <% continue %>
   %endif

   case ${f.name}:
      for (i = 0; i < n; ++i) {
         pack_uint_${f.short_name()}(src[i], d);
         d += ${f.block_size() / 8};
      }
      break;
%endfor
   default:
      assert(!"Invalid format");
   }
}

/**
 * Pack a row of GLfloat rgba[4] values to the destination.
 */
void
_mesa_pack_float_rgba_row(mesa_format format, GLuint n,
                          const GLfloat src[][4], void *dst)
{
   GLuint i;
   GLubyte *d = dst;

   switch (format) {
%for f in rgb_formats:
   %if f.is_compressed():
      <% continue %>
   %elif f.is_int() and not f.is_normalized():
      <% continue %>
   %endif

   case ${f.name}:
      for (i = 0; i < n; ++i) {
         pack_float_${f.short_name()}(src[i], d);
         d += ${f.block_size() / 8};
      }
      break;
%endfor
   default:
      assert(!"Invalid format");
   }
}

/**
 * Pack a 2D image of ubyte RGBA pixels in the given format.
 * \param srcRowStride  source image row stride in bytes
 * \param dstRowStride  destination image row stride in bytes
 */
void
_mesa_pack_ubyte_rgba_rect(mesa_format format, GLuint width, GLuint height,
                           const GLubyte *src, GLint srcRowStride,
                           void *dst, GLint dstRowStride)
{
   GLubyte *dstUB = dst;
   GLuint i;

   if (srcRowStride == width * 4 * sizeof(GLubyte) &&
       dstRowStride == _mesa_format_row_stride(format, width)) {
      /* do whole image at once */
      _mesa_pack_ubyte_rgba_row(format, width * height,
                                (const GLubyte (*)[4]) src, dst);
   }
   else {
      /* row by row */
      for (i = 0; i < height; i++) {
         _mesa_pack_ubyte_rgba_row(format, width,
                                   (const GLubyte (*)[4]) src, dstUB);
         src += srcRowStride;
         dstUB += dstRowStride;
      }
   }
}


/** Helper struct for MESA_FORMAT_Z32_FLOAT_S8X24_UINT */
struct z32f_x24s8
{
   float z;
   uint32_t x24s8;
};


/**
 ** Pack float Z pixels
 **/

static void
pack_float_S8_UINT_Z24_UNORM(const GLfloat *src, void *dst)
{
   /* don't disturb the stencil values */
   GLuint *d = ((GLuint *) dst);
   const GLdouble scale = (GLdouble) 0xffffff;
   GLuint s = *d & 0xff;
   GLuint z = (GLuint) (*src * scale);
   assert(z <= 0xffffff);
   *d = (z << 8) | s;
}

static void
pack_float_Z24_UNORM_S8_UINT(const GLfloat *src, void *dst)
{
   /* don't disturb the stencil values */
   GLuint *d = ((GLuint *) dst);
   const GLdouble scale = (GLdouble) 0xffffff;
   GLuint s = *d & 0xff000000;
   GLuint z = (GLuint) (*src * scale);
   assert(z <= 0xffffff);
   *d = s | z;
}

static void
pack_float_Z_UNORM16(const GLfloat *src, void *dst)
{
   GLushort *d = ((GLushort *) dst);
   const GLfloat scale = (GLfloat) 0xffff;
   *d = (GLushort) (*src * scale);
}

static void
pack_float_Z_UNORM32(const GLfloat *src, void *dst)
{
   GLuint *d = ((GLuint *) dst);
   const GLdouble scale = (GLdouble) 0xffffffff;
   *d = (GLuint) (*src * scale);
}

static void
pack_float_Z_FLOAT32(const GLfloat *src, void *dst)
{
   GLfloat *d = (GLfloat *) dst;
   *d = *src;
}

gl_pack_float_z_func
_mesa_get_pack_float_z_func(mesa_format format)
{
   switch (format) {
   case MESA_FORMAT_S8_UINT_Z24_UNORM:
   case MESA_FORMAT_X8_UINT_Z24_UNORM:
      return pack_float_S8_UINT_Z24_UNORM;
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
   case MESA_FORMAT_Z24_UNORM_X8_UINT:
      return pack_float_Z24_UNORM_S8_UINT;
   case MESA_FORMAT_Z_UNORM16:
      return pack_float_Z_UNORM16;
   case MESA_FORMAT_Z_UNORM32:
      return pack_float_Z_UNORM32;
   case MESA_FORMAT_Z_FLOAT32:
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      return pack_float_Z_FLOAT32;
   default:
      _mesa_problem(NULL,
                    "unexpected format in _mesa_get_pack_float_z_func()");
      return NULL;
   }
}



/**
 ** Pack uint Z pixels.  The incoming src value is always in
 ** the range [0, 2^32-1].
 **/

static void
pack_uint_S8_UINT_Z24_UNORM(const GLuint *src, void *dst)
{
   /* don't disturb the stencil values */
   GLuint *d = ((GLuint *) dst);
   GLuint s = *d & 0xff;
   GLuint z = *src & 0xffffff00;
   *d = z | s;
}

static void
pack_uint_Z24_UNORM_S8_UINT(const GLuint *src, void *dst)
{
   /* don't disturb the stencil values */
   GLuint *d = ((GLuint *) dst);
   GLuint s = *d & 0xff000000;
   GLuint z = *src >> 8;
   *d = s | z;
}

static void
pack_uint_Z_UNORM16(const GLuint *src, void *dst)
{
   GLushort *d = ((GLushort *) dst);
   *d = *src >> 16;
}

static void
pack_uint_Z_UNORM32(const GLuint *src, void *dst)
{
   GLuint *d = ((GLuint *) dst);
   *d = *src;
}

static void
pack_uint_Z_FLOAT32(const GLuint *src, void *dst)
{
   GLuint *d = ((GLuint *) dst);
   const GLdouble scale = 1.0 / (GLdouble) 0xffffffff;
   *d = (GLuint) (*src * scale);
   assert(*d >= 0.0f);
   assert(*d <= 1.0f);
}

static void
pack_uint_Z_FLOAT32_X24S8(const GLuint *src, void *dst)
{
   GLfloat *d = ((GLfloat *) dst);
   const GLdouble scale = 1.0 / (GLdouble) 0xffffffff;
   *d = (GLfloat) (*src * scale);
   assert(*d >= 0.0f);
   assert(*d <= 1.0f);
}

gl_pack_uint_z_func
_mesa_get_pack_uint_z_func(mesa_format format)
{
   switch (format) {
   case MESA_FORMAT_S8_UINT_Z24_UNORM:
   case MESA_FORMAT_X8_UINT_Z24_UNORM:
      return pack_uint_S8_UINT_Z24_UNORM;
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
   case MESA_FORMAT_Z24_UNORM_X8_UINT:
      return pack_uint_Z24_UNORM_S8_UINT;
   case MESA_FORMAT_Z_UNORM16:
      return pack_uint_Z_UNORM16;
   case MESA_FORMAT_Z_UNORM32:
      return pack_uint_Z_UNORM32;
   case MESA_FORMAT_Z_FLOAT32:
      return pack_uint_Z_FLOAT32;
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      return pack_uint_Z_FLOAT32_X24S8;
   default:
      _mesa_problem(NULL, "unexpected format in _mesa_get_pack_uint_z_func()");
      return NULL;
   }
}


/**
 ** Pack ubyte stencil pixels
 **/

static void
pack_ubyte_stencil_Z24_S8(const GLubyte *src, void *dst)
{
   /* don't disturb the Z values */
   GLuint *d = ((GLuint *) dst);
   GLuint s = *src;
   GLuint z = *d & 0xffffff00;
   *d = z | s;
}

static void
pack_ubyte_stencil_S8_Z24(const GLubyte *src, void *dst)
{
   /* don't disturb the Z values */
   GLuint *d = ((GLuint *) dst);
   GLuint s = *src << 24;
   GLuint z = *d & 0xffffff;
   *d = s | z;
}

static void
pack_ubyte_stencil_S8(const GLubyte *src, void *dst)
{
   GLubyte *d = (GLubyte *) dst;
   *d = *src;
}

static void
pack_ubyte_stencil_Z32_FLOAT_X24S8(const GLubyte *src, void *dst)
{
   GLfloat *d = ((GLfloat *) dst);
   d[1] = *src;
}


gl_pack_ubyte_stencil_func
_mesa_get_pack_ubyte_stencil_func(mesa_format format)
{
   switch (format) {
   case MESA_FORMAT_S8_UINT_Z24_UNORM:
      return pack_ubyte_stencil_Z24_S8;
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
      return pack_ubyte_stencil_S8_Z24;
   case MESA_FORMAT_S_UINT8:
      return pack_ubyte_stencil_S8;
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      return pack_ubyte_stencil_Z32_FLOAT_X24S8;
   default:
      _mesa_problem(NULL,
                    "unexpected format in _mesa_pack_ubyte_stencil_func()");
      return NULL;
   }
}



void
_mesa_pack_float_z_row(mesa_format format, GLuint n,
                       const GLfloat *src, void *dst)
{
   switch (format) {
   case MESA_FORMAT_S8_UINT_Z24_UNORM:
   case MESA_FORMAT_X8_UINT_Z24_UNORM:
      {
         /* don't disturb the stencil values */
         GLuint *d = ((GLuint *) dst);
         const GLdouble scale = (GLdouble) 0xffffff;
         GLuint i;
         for (i = 0; i < n; i++) {
            GLuint s = d[i] & 0xff;
            GLuint z = (GLuint) (src[i] * scale);
            assert(z <= 0xffffff);
            d[i] = (z << 8) | s;
         }
      }
      break;
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
   case MESA_FORMAT_Z24_UNORM_X8_UINT:
      {
         /* don't disturb the stencil values */
         GLuint *d = ((GLuint *) dst);
         const GLdouble scale = (GLdouble) 0xffffff;
         GLuint i;
         for (i = 0; i < n; i++) {
            GLuint s = d[i] & 0xff000000;
            GLuint z = (GLuint) (src[i] * scale);
            assert(z <= 0xffffff);
            d[i] = s | z;
         }
      }
      break;
   case MESA_FORMAT_Z_UNORM16:
      {
         GLushort *d = ((GLushort *) dst);
         const GLfloat scale = (GLfloat) 0xffff;
         GLuint i;
         for (i = 0; i < n; i++) {
            d[i] = (GLushort) (src[i] * scale);
         }
      }
      break;
   case MESA_FORMAT_Z_UNORM32:
      {
         GLuint *d = ((GLuint *) dst);
         const GLdouble scale = (GLdouble) 0xffffffff;
         GLuint i;
         for (i = 0; i < n; i++) {
            d[i] = (GLuint) (src[i] * scale);
         }
      }
      break;
   case MESA_FORMAT_Z_FLOAT32:
      memcpy(dst, src, n * sizeof(GLfloat));
      break;
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      {
         struct z32f_x24s8 *d = (struct z32f_x24s8 *) dst;
         GLuint i;
         for (i = 0; i < n; i++) {
            d[i].z = src[i];
         }
      }
      break;
   default:
      _mesa_problem(NULL, "unexpected format in _mesa_pack_float_z_row()");
   }
}


/**
 * The incoming Z values are always in the range [0, 0xffffffff].
 */
void
_mesa_pack_uint_z_row(mesa_format format, GLuint n,
                      const GLuint *src, void *dst)
{
   switch (format) {
   case MESA_FORMAT_S8_UINT_Z24_UNORM:
   case MESA_FORMAT_X8_UINT_Z24_UNORM:
      {
         /* don't disturb the stencil values */
         GLuint *d = ((GLuint *) dst);
         GLuint i;
         for (i = 0; i < n; i++) {
            GLuint s = d[i] & 0xff;
            GLuint z = src[i] & 0xffffff00;
            d[i] = z | s;
         }
      }
      break;
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
   case MESA_FORMAT_Z24_UNORM_X8_UINT:
      {
         /* don't disturb the stencil values */
         GLuint *d = ((GLuint *) dst);
         GLuint i;
         for (i = 0; i < n; i++) {
            GLuint s = d[i] & 0xff000000;
            GLuint z = src[i] >> 8;
            d[i] = s | z;
         }
      }
      break;
   case MESA_FORMAT_Z_UNORM16:
      {
         GLushort *d = ((GLushort *) dst);
         GLuint i;
         for (i = 0; i < n; i++) {
            d[i] = src[i] >> 16;
         }
      }
      break;
   case MESA_FORMAT_Z_UNORM32:
      memcpy(dst, src, n * sizeof(GLfloat));
      break;
   case MESA_FORMAT_Z_FLOAT32:
      {
         GLuint *d = ((GLuint *) dst);
         const GLdouble scale = 1.0 / (GLdouble) 0xffffffff;
         GLuint i;
         for (i = 0; i < n; i++) {
            d[i] = (GLuint) (src[i] * scale);
            assert(d[i] >= 0.0f);
            assert(d[i] <= 1.0f);
         }
      }
      break;
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      {
         struct z32f_x24s8 *d = (struct z32f_x24s8 *) dst;
         const GLdouble scale = 1.0 / (GLdouble) 0xffffffff;
         GLuint i;
         for (i = 0; i < n; i++) {
            d[i].z = (GLfloat) (src[i] * scale);
            assert(d[i].z >= 0.0f);
            assert(d[i].z <= 1.0f);
         }
      }
      break;
   default:
      _mesa_problem(NULL, "unexpected format in _mesa_pack_uint_z_row()");
   }
}


void
_mesa_pack_ubyte_stencil_row(mesa_format format, GLuint n,
                             const GLubyte *src, void *dst)
{
   switch (format) {
   case MESA_FORMAT_S8_UINT_Z24_UNORM:
      {
         /* don't disturb the Z values */
         GLuint *d = ((GLuint *) dst);
         GLuint i;
         for (i = 0; i < n; i++) {
            GLuint s = src[i];
            GLuint z = d[i] & 0xffffff00;
            d[i] = z | s;
         }
      }
      break;
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
      {
         /* don't disturb the Z values */
         GLuint *d = ((GLuint *) dst);
         GLuint i;
         for (i = 0; i < n; i++) {
            GLuint s = src[i] << 24;
            GLuint z = d[i] & 0xffffff;
            d[i] = s | z;
         }
      }
      break;
   case MESA_FORMAT_S_UINT8:
      memcpy(dst, src, n * sizeof(GLubyte));
      break;
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      {
         struct z32f_x24s8 *d = (struct z32f_x24s8 *) dst;
         GLuint i;
         for (i = 0; i < n; i++) {
            d[i].x24s8 = src[i];
         }
      }
      break;
   default:
      _mesa_problem(NULL, "unexpected format in _mesa_pack_ubyte_stencil_row()");
   }
}


/**
 * Incoming Z/stencil values are always in uint_24_8 format.
 */
void
_mesa_pack_uint_24_8_depth_stencil_row(mesa_format format, GLuint n,
                                       const GLuint *src, void *dst)
{
   switch (format) {
   case MESA_FORMAT_S8_UINT_Z24_UNORM:
      memcpy(dst, src, n * sizeof(GLuint));
      break;
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
      {
         GLuint *d = ((GLuint *) dst);
         GLuint i;
         for (i = 0; i < n; i++) {
            GLuint s = src[i] << 24;
            GLuint z = src[i] >> 8;
            d[i] = s | z;
         }
      }
      break;
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      {
         const GLdouble scale = 1.0 / (GLdouble) 0xffffff;
         struct z32f_x24s8 *d = (struct z32f_x24s8 *) dst;
         GLuint i;
         for (i = 0; i < n; i++) {
            GLfloat z = (GLfloat) ((src[i] >> 8) * scale);
            d[i].z = z;
            d[i].x24s8 = src[i];
         }
      }
      break;
   default:
      _mesa_problem(NULL, "bad format %s in _mesa_pack_ubyte_s_row",
                    _mesa_get_format_name(format));
      return;
   }
}



/**
 * Convert a boolean color mask to a packed color where each channel of
 * the packed value at dst will be 0 or ~0 depending on the colorMask.
 */
void
_mesa_pack_colormask(mesa_format format, const GLubyte colorMask[4], void *dst)
{
   GLfloat maskColor[4];

   switch (_mesa_get_format_datatype(format)) {
   case GL_UNSIGNED_NORMALIZED:
      /* simple: 1.0 will convert to ~0 in the right bit positions */
      maskColor[0] = colorMask[0] ? 1.0f : 0.0f;
      maskColor[1] = colorMask[1] ? 1.0f : 0.0f;
      maskColor[2] = colorMask[2] ? 1.0f : 0.0f;
      maskColor[3] = colorMask[3] ? 1.0f : 0.0f;
      _mesa_pack_float_rgba_row(format, 1,
                                (const GLfloat (*)[4]) maskColor, dst);
      break;
   case GL_SIGNED_NORMALIZED:
   case GL_FLOAT:
      /* These formats are harder because it's hard to know the floating
       * point values that will convert to ~0 for each color channel's bits.
       * This solution just generates a non-zero value for each color channel
       * then fixes up the non-zero values to be ~0.
       * Note: we'll need to add special case code if we ever have to deal
       * with formats with unequal color channel sizes, like R11_G11_B10.
       * We issue a warning below for channel sizes other than 8,16,32.
       */
      {
         GLuint bits = _mesa_get_format_max_bits(format); /* bits per chan */
         GLuint bytes = _mesa_get_format_bytes(format);
         GLuint i;

         /* this should put non-zero values into the channels of dst */
         maskColor[0] = colorMask[0] ? -1.0f : 0.0f;
         maskColor[1] = colorMask[1] ? -1.0f : 0.0f;
         maskColor[2] = colorMask[2] ? -1.0f : 0.0f;
         maskColor[3] = colorMask[3] ? -1.0f : 0.0f;
         _mesa_pack_float_rgba_row(format, 1,
                                   (const GLfloat (*)[4]) maskColor, dst);

         /* fix-up the dst channels by converting non-zero values to ~0 */
         if (bits == 8) {
            GLubyte *d = (GLubyte *) dst;
            for (i = 0; i < bytes; i++) {
               d[i] = d[i] ? 0xff : 0x0;
            }
         }
         else if (bits == 16) {
            GLushort *d = (GLushort *) dst;
            for (i = 0; i < bytes / 2; i++) {
               d[i] = d[i] ? 0xffff : 0x0;
            }
         }
         else if (bits == 32) {
            GLuint *d = (GLuint *) dst;
            for (i = 0; i < bytes / 4; i++) {
               d[i] = d[i] ? 0xffffffffU : 0x0;
            }
         }
         else {
            _mesa_problem(NULL, "unexpected size in _mesa_pack_colormask()");
            return;
         }
      }
      break;
   default:
      _mesa_problem(NULL, "unexpected format data type in gen_color_mask()");
      return;
   }
}
"""

template = Template(string);

print template.render(argv = argv[0:])
