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
 */

#include "agx_pack.h"
#include "agx_formats.h"

#define T true
#define F false
#define AGX_FORMAT__ 0

#define AGX_FMT(pipe, channels, type, is_renderable, internal_fmt) \
   [PIPE_FORMAT_ ## pipe] = { \
      .hw = (AGX_CHANNELS_ ## channels) | ((AGX_TEXTURE_TYPE_ ## type) << 7), \
      .renderable = is_renderable, \
      .internal = AGX_FORMAT_ ## internal_fmt,\
   }

const struct agx_pixel_format_entry agx_pixel_format[PIPE_FORMAT_COUNT] = {
   AGX_FMT(R8_UNORM,                R8,            UNORM,  T, U8NORM),
   AGX_FMT(R8G8_UNORM,              R8G8,          UNORM,  T, U8NORM),
   AGX_FMT(R8G8B8A8_UNORM,          R8G8B8A8,      UNORM,  T, U8NORM),
   AGX_FMT(A8R8G8B8_UNORM,          R8G8B8A8,      UNORM,  T, U8NORM),
   AGX_FMT(A8B8G8R8_UNORM,          R8G8B8A8,      UNORM,  T, U8NORM),
   AGX_FMT(B8G8R8A8_UNORM,          R8G8B8A8,      UNORM,  T, U8NORM),

   AGX_FMT(R16_UNORM,               R16,           UNORM,  T, U16NORM),
   AGX_FMT(R16G16_UNORM,            R16G16,        UNORM,  T, U16NORM),
   AGX_FMT(R16G16B16A16_UNORM,      R16G16B16A16,  UNORM,  T, U16NORM),

   AGX_FMT(R8_SRGB,                 R8,            UNORM,  T, SRGBA8),
   AGX_FMT(R8G8_SRGB,               R8G8,          UNORM,  T, SRGBA8),
   AGX_FMT(R8G8B8A8_SRGB,           R8G8B8A8,      UNORM,  T, SRGBA8),
   AGX_FMT(A8R8G8B8_SRGB,           R8G8B8A8,      UNORM,  T, SRGBA8),
   AGX_FMT(A8B8G8R8_SRGB,           R8G8B8A8,      UNORM,  T, SRGBA8),
   AGX_FMT(B8G8R8A8_SRGB,           R8G8B8A8,      UNORM,  T, SRGBA8),

   AGX_FMT(R8_SNORM,                R8,            SNORM,  T, S8NORM),
   AGX_FMT(R8G8_SNORM,              R8G8,          SNORM,  T, S8NORM),
   AGX_FMT(R8G8B8A8_SNORM,          R8G8B8A8,      SNORM,  T, S8NORM),
   AGX_FMT(A8R8G8B8_SNORM,          R8G8B8A8,      SNORM,  T, S8NORM),
   AGX_FMT(A8B8G8R8_SNORM,          R8G8B8A8,      SNORM,  T, S8NORM),
   AGX_FMT(B8G8R8A8_SNORM,          R8G8B8A8,      SNORM,  T, S8NORM),

   AGX_FMT(R16_FLOAT,               R16,           FLOAT,  T, F16),
   AGX_FMT(R16G16_FLOAT,            R16G16,        FLOAT,  T, F16),
   AGX_FMT(R16G16B16A16_FLOAT,      R16G16B16A16,  FLOAT,  T, F16),

   AGX_FMT(R32_FLOAT,               R32,           FLOAT,  T, I32),
   AGX_FMT(R32G32_FLOAT,            R32G32,        FLOAT,  T, I32),
   AGX_FMT(R32G32B32A32_FLOAT,      R32G32B32A32,  FLOAT,  T, I32),

   AGX_FMT(R8_UINT,                 R8,            UINT,   T, I8),
   AGX_FMT(R8G8_UINT,               R8G8,          UINT,   T, I8),
   AGX_FMT(R8G8B8A8_UINT,           R8G8B8A8,      UINT,   T, I8),

   AGX_FMT(R16_UINT,                R16,           UINT,   T, I16),
   AGX_FMT(R16G16_UINT,             R16G16,        UINT,   T, I16),
   AGX_FMT(R16G16B16A16_UINT,       R16G16B16A16,  UINT,   T, I16),

   AGX_FMT(R32_UINT,                R32,           UINT,   T, I32),
   AGX_FMT(R32G32_UINT,             R32G32,        UINT,   T, I32),
   AGX_FMT(R32G32B32A32_UINT,       R32G32B32A32,  UINT,   T, I32),

   AGX_FMT(R8_SINT,                 R8,            SINT,   T, I8),
   AGX_FMT(R8G8_SINT,               R8G8,          SINT,   T, I8),
   AGX_FMT(R8G8B8A8_SINT,           R8G8B8A8,      SINT,   T, I8),

   AGX_FMT(R16_SINT,                R16,           SINT,   T, I16),
   AGX_FMT(R16G16_SINT,             R16G16,        SINT,   T, I16),
   AGX_FMT(R16G16B16A16_SINT,       R16G16B16A16,  SINT,   T, I16),

   AGX_FMT(R32_SINT,                R32,           SINT,   T, I32),
   AGX_FMT(R32G32_SINT,             R32G32,        SINT,   T, I32),
   AGX_FMT(R32G32B32A32_SINT,       R32G32B32A32,  SINT,   T, I32),

   AGX_FMT(Z16_UNORM,               R16,           UNORM,  F, _),
   AGX_FMT(Z32_FLOAT,               R32,           FLOAT,  F, _),
   AGX_FMT(Z32_FLOAT_S8X24_UINT,    R32,           FLOAT,  F, _),

   /* These must be lowered by u_transfer_helper to Z32F */
   AGX_FMT(Z24X8_UNORM,             R32,           FLOAT,  F, _),
   AGX_FMT(Z24_UNORM_S8_UINT,       R32,           FLOAT,  F, _),

   AGX_FMT(R10G10B10A2_UNORM,       R10G10B10A2,   UNORM,  T, RGB10A2),
   AGX_FMT(B10G10R10A2_UNORM,       R10G10B10A2,   UNORM,  T, RGB10A2),

   AGX_FMT(R10G10B10A2_UINT,        R10G10B10A2,   UINT,   T, _),
   AGX_FMT(B10G10R10A2_UINT,        R10G10B10A2,   UINT,   T, _),

   AGX_FMT(R10G10B10A2_SINT,        R10G10B10A2,   SINT,   T, _),
   AGX_FMT(B10G10R10A2_SINT,        R10G10B10A2,   SINT,   T, _),

   AGX_FMT(R11G11B10_FLOAT,         R11G11B10,     FLOAT,  T, RG11B10F),
   AGX_FMT(R9G9B9E5_FLOAT,          R9G9B9E5,      FLOAT,  F, RGB9E5),

   AGX_FMT(ETC1_RGB8,               ETC2_RGB8,     UNORM,  F,_),
   AGX_FMT(ETC2_RGB8,               ETC2_RGB8,     UNORM,  F,_),
   AGX_FMT(ETC2_SRGB8,              ETC2_RGB8,     UNORM,  F,_),
   AGX_FMT(ETC2_RGB8A1,             ETC2_RGB8A1,   UNORM,  F,_),
   AGX_FMT(ETC2_SRGB8A1,            ETC2_RGB8A1,   UNORM,  F,_),
   AGX_FMT(ETC2_RGBA8,              ETC2_RGBA8,    UNORM,  F,_),
   AGX_FMT(ETC2_SRGBA8,             ETC2_RGBA8,    UNORM,  F,_),
   AGX_FMT(ETC2_R11_UNORM,          EAC_R11,       UNORM,  F,_),
   AGX_FMT(ETC2_R11_SNORM,          EAC_R11,       SNORM,  F,_),
   AGX_FMT(ETC2_RG11_UNORM,         EAC_RG11,      UNORM,  F,_),
   AGX_FMT(ETC2_RG11_SNORM,         EAC_RG11,      SNORM,  F,_),

   AGX_FMT(ASTC_4x4,                ASTC_4X4,      UNORM,  F, _),
   AGX_FMT(ASTC_5x4,                ASTC_5X4,      UNORM,  F, _),
   AGX_FMT(ASTC_5x5,                ASTC_5X5,      UNORM,  F, _),
   AGX_FMT(ASTC_6x5,                ASTC_6X5,      UNORM,  F, _),
   AGX_FMT(ASTC_6x6,                ASTC_6X6,      UNORM,  F, _),
   AGX_FMT(ASTC_8x5,                ASTC_8X5,      UNORM,  F, _),
   AGX_FMT(ASTC_8x6,                ASTC_8X6,      UNORM,  F, _),
   AGX_FMT(ASTC_8x8,                ASTC_8X8,      UNORM,  F, _),
   AGX_FMT(ASTC_10x5,               ASTC_10X5,     UNORM,  F, _),
   AGX_FMT(ASTC_10x6,               ASTC_10X6,     UNORM,  F, _),
   AGX_FMT(ASTC_10x8,               ASTC_10X8,     UNORM,  F, _),
   AGX_FMT(ASTC_10x10,              ASTC_10X10,    UNORM,  F, _),
   AGX_FMT(ASTC_12x10,              ASTC_12X10,    UNORM,  F, _),
   AGX_FMT(ASTC_12x12,              ASTC_12X12,    UNORM,  F, _),

   AGX_FMT(ASTC_4x4_SRGB,           ASTC_4X4,      UNORM,  F, _),
   AGX_FMT(ASTC_5x4_SRGB,           ASTC_5X4,      UNORM,  F, _),
   AGX_FMT(ASTC_5x5_SRGB,           ASTC_5X5,      UNORM,  F, _),
   AGX_FMT(ASTC_6x5_SRGB,           ASTC_6X5,      UNORM,  F, _),
   AGX_FMT(ASTC_6x6_SRGB,           ASTC_6X6,      UNORM,  F, _),
   AGX_FMT(ASTC_8x5_SRGB,           ASTC_8X5,      UNORM,  F, _),
   AGX_FMT(ASTC_8x6_SRGB,           ASTC_8X6,      UNORM,  F, _),
   AGX_FMT(ASTC_8x8_SRGB,           ASTC_8X8,      UNORM,  F, _),
   AGX_FMT(ASTC_10x5_SRGB,          ASTC_10X5,     UNORM,  F, _),
   AGX_FMT(ASTC_10x6_SRGB,          ASTC_10X6,     UNORM,  F, _),
   AGX_FMT(ASTC_10x8_SRGB,          ASTC_10X8,     UNORM,  F, _),
   AGX_FMT(ASTC_10x10_SRGB,         ASTC_10X10,    UNORM,  F, _),
   AGX_FMT(ASTC_12x10_SRGB,         ASTC_12X10,    UNORM,  F, _),
   AGX_FMT(ASTC_12x12_SRGB,         ASTC_12X12,    UNORM,  F, _),
};

const enum agx_format
agx_vertex_format[PIPE_FORMAT_COUNT] = {
   [PIPE_FORMAT_R32_FLOAT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32_SINT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32_UINT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32_FLOAT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32_SINT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32_UINT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32B32_FLOAT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32B32_UINT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32B32_SINT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32B32A32_FLOAT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32B32A32_UINT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32B32A32_SINT] = AGX_FORMAT_I32,

   [PIPE_FORMAT_R8_UNORM] = AGX_FORMAT_U8NORM,
   [PIPE_FORMAT_R8G8_UNORM] = AGX_FORMAT_U8NORM,
   [PIPE_FORMAT_R8G8B8_UNORM] = AGX_FORMAT_U8NORM,
   [PIPE_FORMAT_R8G8B8A8_UNORM] = AGX_FORMAT_U8NORM,

   [PIPE_FORMAT_R8_SNORM] = AGX_FORMAT_S8NORM,
   [PIPE_FORMAT_R8G8_SNORM] = AGX_FORMAT_S8NORM,
   [PIPE_FORMAT_R8G8B8_SNORM] = AGX_FORMAT_S8NORM,
   [PIPE_FORMAT_R8G8B8A8_SNORM] = AGX_FORMAT_S8NORM,

   [PIPE_FORMAT_R16_UNORM] = AGX_FORMAT_U16NORM,
   [PIPE_FORMAT_R16G16_UNORM] = AGX_FORMAT_U16NORM,
   [PIPE_FORMAT_R16G16B16_UNORM] = AGX_FORMAT_U16NORM,
   [PIPE_FORMAT_R16G16B16A16_UNORM] = AGX_FORMAT_U16NORM,

   [PIPE_FORMAT_R16_SNORM] = AGX_FORMAT_S16NORM,
   [PIPE_FORMAT_R16G16_SNORM] = AGX_FORMAT_S16NORM,
   [PIPE_FORMAT_R16G16B16_SNORM] = AGX_FORMAT_S16NORM,
   [PIPE_FORMAT_R16G16B16A16_SNORM] = AGX_FORMAT_S16NORM,

   [PIPE_FORMAT_R8_UINT] = AGX_FORMAT_I8,
   [PIPE_FORMAT_R8G8_UINT] = AGX_FORMAT_I8,
   [PIPE_FORMAT_R8G8B8_UINT] = AGX_FORMAT_I8,
   [PIPE_FORMAT_R8G8B8A8_UINT] = AGX_FORMAT_I8,

   [PIPE_FORMAT_R8_SINT] = AGX_FORMAT_I8,
   [PIPE_FORMAT_R8G8_SINT] = AGX_FORMAT_I8,
   [PIPE_FORMAT_R8G8B8_SINT] = AGX_FORMAT_I8,
   [PIPE_FORMAT_R8G8B8A8_SINT] = AGX_FORMAT_I8,

   [PIPE_FORMAT_R16_UINT] = AGX_FORMAT_I16,
   [PIPE_FORMAT_R16G16_UINT] = AGX_FORMAT_I16,
   [PIPE_FORMAT_R16G16B16_UINT] = AGX_FORMAT_I16,
   [PIPE_FORMAT_R16G16B16A16_UINT] = AGX_FORMAT_I16,

   [PIPE_FORMAT_R16_SINT] = AGX_FORMAT_I16,
   [PIPE_FORMAT_R16G16_SINT] = AGX_FORMAT_I16,
   [PIPE_FORMAT_R16G16B16_SINT] = AGX_FORMAT_I16,
   [PIPE_FORMAT_R16G16B16A16_SINT] = AGX_FORMAT_I16,

   [PIPE_FORMAT_R32_UINT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32_UINT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32B32_UINT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32B32A32_UINT] = AGX_FORMAT_I32,

   [PIPE_FORMAT_R32_SINT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32_SINT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32B32_SINT] = AGX_FORMAT_I32,
   [PIPE_FORMAT_R32G32B32A32_SINT] = AGX_FORMAT_I32,
};
