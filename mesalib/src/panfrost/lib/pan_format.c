/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include <stdio.h>
#include "midgard_pack.h"
#include "pan_texture.h"
#include "panfrost-quirks.h"

/* Convenience */

#define BFMT2(pipe, internal, writeback) \
        [PIPE_FORMAT_##pipe] = { \
                MALI_COLOR_BUFFER_INTERNAL_FORMAT_## internal, \
                MALI_MFBD_COLOR_FORMAT_## writeback \
        }

#define BFMT(pipe, internal_and_writeback) \
        BFMT2(pipe, internal_and_writeback, internal_and_writeback)

static const struct pan_blendable_format panfrost_blendable_formats[PIPE_FORMAT_COUNT] = {
        BFMT2(L8_UNORM, R8G8B8A8, R8),
        BFMT2(L8A8_UNORM, R8G8B8A8, R8G8),
        BFMT2(I8_UNORM, R8G8B8A8, R8),
        BFMT2(A8_UNORM, R8G8B8A8, R8),
        BFMT2(R8_UNORM, R8G8B8A8, R8),
        BFMT2(R8G8_UNORM, R8G8B8A8, R8G8),
        BFMT2(R8G8B8_UNORM, R8G8B8A8, R8G8B8),

        BFMT(B8G8R8A8_UNORM, R8G8B8A8),
        BFMT(B8G8R8X8_UNORM, R8G8B8A8),
        BFMT(A8R8G8B8_UNORM, R8G8B8A8),
        BFMT(X8R8G8B8_UNORM, R8G8B8A8),
        BFMT(A8B8G8R8_UNORM, R8G8B8A8),
        BFMT(X8B8G8R8_UNORM, R8G8B8A8),
        BFMT(R8G8B8X8_UNORM, R8G8B8A8),
        BFMT(R8G8B8A8_UNORM, R8G8B8A8),

        BFMT2(B5G6R5_UNORM, R5G6B5A0, R5G6B5),

        BFMT(A4B4G4R4_UNORM, R4G4B4A4),
        BFMT(B4G4R4A4_UNORM, R4G4B4A4),
        BFMT(R4G4B4A4_UNORM, R4G4B4A4),

        BFMT(R10G10B10A2_UNORM, R10G10B10A2),
        BFMT(B10G10R10A2_UNORM, R10G10B10A2),
        BFMT(R10G10B10X2_UNORM, R10G10B10A2),
        BFMT(B10G10R10X2_UNORM, R10G10B10A2),

        BFMT(B5G5R5A1_UNORM, R5G5B5A1),
        BFMT(R5G5B5A1_UNORM, R5G5B5A1),
        BFMT(B5G5R5X1_UNORM, R5G5B5A1),
};

/* Accessor that is generic over linear/sRGB */

struct pan_blendable_format
panfrost_blend_format(enum pipe_format format)
{
        return panfrost_blendable_formats[util_format_linear(format)];
}

/* Convenience */

#define _V PIPE_BIND_VERTEX_BUFFER
#define _T PIPE_BIND_SAMPLER_VIEW
#define _R PIPE_BIND_RENDER_TARGET
#define _Z PIPE_BIND_DEPTH_STENCIL

#define FLAGS_V___ (_V)
#define FLAGS__T__ (_T)
#define FLAGS_VTR_ (_V | _T | _R)
#define FLAGS_VT__ (_V | _T)
#define FLAGS__T_Z (_T | _Z)

#define V6_0000 PAN_V6_SWIZZLE(0, 0, 0, 0)
#define V6_000R PAN_V6_SWIZZLE(0, 0, 0, R)
#define V6_0R00 PAN_V6_SWIZZLE(0, R, 0, 0)
#define V6_0A00 PAN_V6_SWIZZLE(0, A, 0, 0)
#define V6_A001 PAN_V6_SWIZZLE(A, 0, 0, 1)
#define V6_ABG1 PAN_V6_SWIZZLE(A, B, G, 1)
#define V6_ABGR PAN_V6_SWIZZLE(A, B, G, R)
#define V6_BGR1 PAN_V6_SWIZZLE(B, G, R, 1)
#define V6_BGRA PAN_V6_SWIZZLE(B, G, R, A)
#define V6_GBA1 PAN_V6_SWIZZLE(G, B, A, 1)
#define V6_GBAR PAN_V6_SWIZZLE(G, B, A, R)
#define V6_R000 PAN_V6_SWIZZLE(R, 0, 0, 0)
#define V6_R001 PAN_V6_SWIZZLE(R, 0, 0, 1)
#define V6_RG01 PAN_V6_SWIZZLE(R, G, 0, 1)
#define V6_RGB1 PAN_V6_SWIZZLE(R, G, B, 1)
#define V6_RGBA PAN_V6_SWIZZLE(R, G, B, A)
#define V6_RRR1 PAN_V6_SWIZZLE(R, R, R, 1)
#define V6_RRRG PAN_V6_SWIZZLE(R, R, R, G)
#define V6_RRRR PAN_V6_SWIZZLE(R, R, R, R)

#define SRGB_L (0)
#define SRGB_S (1)

#define PAN_V6(pipe, mali, swizzle, srgb, flags) \
        [PIPE_FORMAT_ ## pipe] = { \
            .hw = ( V6_ ## swizzle ) | \
                (( MALI_ ## mali ) << 12) | \
                ((( SRGB_ ## srgb)) << 20), \
            .bind = FLAGS_ ## flags \
        }

#define PAN_V7(pipe, mali, swizzle, srgb, flags) \
        [PIPE_FORMAT_ ## pipe] = { \
            .hw = ( MALI_RGB_COMPONENT_ORDER_ ## swizzle ) | \
                (( MALI_ ## mali ) << 12) | \
                ((( SRGB_ ## srgb)) << 20), \
            .bind = FLAGS_ ## flags \
        }

const struct panfrost_format panfrost_pipe_format_v6[PIPE_FORMAT_COUNT] = {
        PAN_V6(NONE,                    CONSTANT,        0000, L, VTR_),
        PAN_V6(ETC1_RGB8,               ETC2_RGB8,       RGB1, L, _T__),
        PAN_V6(ETC2_RGB8,               ETC2_RGB8,       RGB1, L, _T__),
        PAN_V6(ETC2_SRGB8,              ETC2_RGB8,       RGB1, S, _T__),
        PAN_V6(ETC2_R11_UNORM,          ETC2_R11_UNORM,  R001, L, _T__),
        PAN_V6(ETC2_RGBA8,              ETC2_RGBA8,      RGBA, L, _T__),
        PAN_V6(ETC2_SRGBA8,             ETC2_RGBA8,      RGBA, S, _T__),
        PAN_V6(ETC2_RG11_UNORM,         ETC2_RG11_UNORM, RG01, L, _T__),
        PAN_V6(ETC2_R11_SNORM,          ETC2_R11_SNORM,  R001, L, _T__),
        PAN_V6(ETC2_RG11_SNORM,         ETC2_RG11_SNORM, RG01, L, _T__),
        PAN_V6(ETC2_RGB8A1,             ETC2_RGB8A1,     RGBA, L, _T__),
        PAN_V6(ETC2_SRGB8A1,            ETC2_RGB8A1,     RGBA, S, _T__),
        PAN_V6(DXT1_RGB,                BC1_UNORM,       RGB1, L, _T__),
        PAN_V6(DXT1_RGBA,               BC1_UNORM,       RGBA, L, _T__),
        PAN_V6(DXT1_SRGB,               BC1_UNORM,       RGB1, S, _T__),
        PAN_V6(DXT1_SRGBA,              BC1_UNORM,       RGBA, S, _T__),
        PAN_V6(DXT3_RGBA,               BC2_UNORM,       RGBA, L, _T__),
        PAN_V6(DXT3_SRGBA,              BC2_UNORM,       RGBA, S, _T__),
        PAN_V6(DXT5_RGBA,               BC3_UNORM,       RGBA, L, _T__),
        PAN_V6(DXT5_SRGBA,              BC3_UNORM,       RGBA, S, _T__),
        PAN_V6(RGTC1_UNORM,             BC4_UNORM,       R001, L, _T__),
        PAN_V6(RGTC1_SNORM,             BC4_SNORM,       R001, L, _T__),
        PAN_V6(RGTC2_UNORM,             BC5_UNORM,       RG01, L, _T__),
        PAN_V6(RGTC2_SNORM,             BC5_SNORM,       RG01, L, _T__),
        PAN_V6(BPTC_RGB_FLOAT,          BC6H_SF16,       RGB1, L, _T__),
        PAN_V6(BPTC_RGB_UFLOAT,         BC6H_UF16,       RGB1, L, _T__),
        PAN_V6(BPTC_RGBA_UNORM,         BC7_UNORM,       RGBA, L, _T__),
        PAN_V6(BPTC_SRGBA,              BC7_UNORM,       RGBA, S, _T__),
        PAN_V6(ASTC_4x4,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_5x4,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_5x5,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_6x5,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_6x6,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_8x5,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_8x6,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_8x8,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_10x5,               ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_10x6,               ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_10x8,               ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_10x10,              ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_12x10,              ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_12x12,              ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V6(ASTC_4x4_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(ASTC_5x4_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(ASTC_5x5_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(ASTC_6x5_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(ASTC_6x6_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(ASTC_8x5_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(ASTC_8x6_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(ASTC_8x8_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(ASTC_10x5_SRGB,          ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(ASTC_10x6_SRGB,          ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(ASTC_10x8_SRGB,          ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(ASTC_10x10_SRGB,         ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(ASTC_12x10_SRGB,         ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(ASTC_12x12_SRGB,         ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V6(R5G6B5_UNORM,            RGB565,          RGB1, L, VTR_),
        PAN_V6(B5G6R5_UNORM,            RGB565,          BGR1, L, VTR_),
        PAN_V6(B5G5R5X1_UNORM,          RGB5_A1_UNORM,   BGR1, L, VT__),
        PAN_V6(R5G5B5A1_UNORM,          RGB5_A1_UNORM,   RGBA, L, VTR_),
        PAN_V6(R10G10B10X2_UNORM,       RGB10_A2_UNORM,  RGB1, L, VTR_),
        PAN_V6(B10G10R10X2_UNORM,       RGB10_A2_UNORM,  BGR1, L, VTR_),
        PAN_V6(R10G10B10A2_UNORM,       RGB10_A2_UNORM,  RGBA, L, VTR_),
        PAN_V6(B10G10R10A2_UNORM,       RGB10_A2_UNORM,  BGRA, L, VTR_),
        PAN_V6(R10G10B10X2_SNORM,       RGB10_A2_SNORM,  RGB1, L, VT__),
        PAN_V6(R10G10B10A2_SNORM,       RGB10_A2_SNORM,  RGBA, L, VT__),
        PAN_V6(B10G10R10A2_SNORM,       RGB10_A2_SNORM,  BGRA, L, VT__),
        PAN_V6(R10G10B10A2_UINT,        RGB10_A2UI,      RGBA, L, VTR_),
        PAN_V6(B10G10R10A2_UINT,        RGB10_A2UI,      BGRA, L, VTR_),
        PAN_V6(R10G10B10A2_USCALED,     RGB10_A2UI,      RGBA, L, VTR_),
        PAN_V6(B10G10R10A2_USCALED,     RGB10_A2UI,      BGRA, L, VTR_),
        PAN_V6(R10G10B10A2_SINT,        RGB10_A2I,       RGBA, L, VTR_),
        PAN_V6(B10G10R10A2_SINT,        RGB10_A2I,       BGRA, L, VTR_),
        PAN_V6(R10G10B10A2_SSCALED,     RGB10_A2I,       RGBA, L, VTR_),
        PAN_V6(B10G10R10A2_SSCALED,     RGB10_A2I,       BGRA, L, VTR_),
        PAN_V6(R8_SSCALED,              R8I,             R001, L, V___),
        PAN_V6(R8G8_SSCALED,            RG8I,            RG01, L, V___),
        PAN_V6(R8G8B8_SSCALED,          RGB8I,           RGB1, L, V___),
        PAN_V6(B8G8R8_SSCALED,          RGB8I,           BGR1, L, V___),
        PAN_V6(R8G8B8A8_SSCALED,        RGBA8I,          RGBA, L, V___),
        PAN_V6(B8G8R8A8_SSCALED,        RGBA8I,          BGRA, L, V___),
        PAN_V6(A8B8G8R8_SSCALED,        RGBA8I,          ABGR, L, V___),
        PAN_V6(R8_USCALED,              R8UI,            R001, L, V___),
        PAN_V6(R8G8_USCALED,            RG8UI,           RG01, L, V___),
        PAN_V6(R8G8B8_USCALED,          RGB8UI,          RGB1, L, V___),
        PAN_V6(B8G8R8_USCALED,          RGB8UI,          BGR1, L, V___),
        PAN_V6(R8G8B8A8_USCALED,        RGBA8UI,         RGBA, L, V___),
        PAN_V6(B8G8R8A8_USCALED,        RGBA8UI,         BGRA, L, V___),
        PAN_V6(A8B8G8R8_USCALED,        RGBA8UI,         ABGR, L, V___),
        PAN_V6(R16_USCALED,             R16UI,           R001, L, V___),
        PAN_V6(R16G16_USCALED,          RG16UI,          RG01, L, V___),
        PAN_V6(R16G16B16_USCALED,       RGB16UI,         RGB1, L, V___),
        PAN_V6(R16G16B16A16_USCALED,    RGBA16UI,        RGBA, L, V___),
        PAN_V6(R16_SSCALED,             R16I,            R001, L, V___),
        PAN_V6(R16G16_SSCALED,          RG16I,           RG01, L, V___),
        PAN_V6(R16G16B16_SSCALED,       RGB16I,          RGB1, L, V___),
        PAN_V6(R16G16B16A16_SSCALED,    RGBA16I,         RGBA, L, V___),
        PAN_V6(R32_USCALED,             R32UI,           R001, L, V___),
        PAN_V6(R32G32_USCALED,          RG32UI,          RG01, L, V___),
        PAN_V6(R32G32B32_USCALED,       RGB32UI,         RGB1, L, V___),
        PAN_V6(R32G32B32A32_USCALED,    RGBA32UI,        RGBA, L, V___),
        PAN_V6(R32_SSCALED,             R32I,            R001, L, V___),
        PAN_V6(R32G32_SSCALED,          RG32I,           RG01, L, V___),
        PAN_V6(R32G32B32_SSCALED,       RGB32I,          RGB1, L, V___),
        PAN_V6(R32G32B32A32_SSCALED,    RGBA32I,         RGBA, L, V___),
        PAN_V6(R3G3B2_UNORM,            RGB332_UNORM,    RGB1, L, VT__),
        PAN_V6(Z16_UNORM,               R16_UNORM,       R000, L, _T_Z),
        PAN_V6(Z24_UNORM_S8_UINT,       Z24X8_UNORM,     R000, L, _T_Z),
        PAN_V6(Z24X8_UNORM,             Z24X8_UNORM,     R000, L, _T_Z),
        PAN_V6(Z32_FLOAT,               R32F,            R000, L, _T_Z),
        PAN_V6(Z32_FLOAT_S8X24_UINT,    R32F,            R000, L, _T_Z),
        PAN_V6(X32_S8X24_UINT,          R32UI,           0R00, L, _T__),
        PAN_V6(X24S8_UINT,              RGBA8UI,         0A00, L, _T_Z),
        PAN_V6(S8_UINT,                 R8UI,            0R00, L, _T__),
        PAN_V6(R32_FIXED,               R32_FIXED,       R001, L, V___),
        PAN_V6(R32G32_FIXED,            RG32_FIXED,      RG01, L, V___),
        PAN_V6(R32G32B32_FIXED,         RGB32_FIXED,     RGB1, L, V___),
        PAN_V6(R32G32B32A32_FIXED,      RGBA32_FIXED,    RGBA, L, V___),
        PAN_V6(R11G11B10_FLOAT,         R11F_G11F_B10F,  RGB1, L, VTR_),
        PAN_V6(R9G9B9E5_FLOAT,          R9F_G9F_B9F_E5F, RGB1, L, VT__),
        PAN_V6(R8_SNORM,                R8_SNORM,        R001, L, VT__),
        PAN_V6(R16_SNORM,               R16_SNORM,       R001, L, VT__),
        PAN_V6(R32_SNORM,               R32_SNORM,       R001, L, VT__),
        PAN_V6(R8G8_SNORM,              RG8_SNORM,       RG01, L, VT__),
        PAN_V6(R16G16_SNORM,            RG16_SNORM,      RG01, L, VT__),
        PAN_V6(R32G32_SNORM,            RG32_SNORM,      RG01, L, VT__),
        PAN_V6(R8G8B8_SNORM,            RGB8_SNORM,      RGB1, L, VT__),
        PAN_V6(R16G16B16_SNORM,         RGB16_SNORM,     RGB1, L, VT__),
        PAN_V6(R32G32B32_SNORM,         RGB32_SNORM,     RGB1, L, VT__),
        PAN_V6(R8G8B8A8_SNORM,          RGBA8_SNORM,     RGBA, L, VT__),
        PAN_V6(R16G16B16A16_SNORM,      RGBA16_SNORM,    RGBA, L, VT__),
        PAN_V6(R32G32B32A32_SNORM,      RGBA32_SNORM,    RGBA, L, VT__),
        PAN_V6(A8_SINT,                 R8I,             000R, L, VTR_),
        PAN_V6(I8_SINT,                 R8I,             RRRR, L, VTR_),
        PAN_V6(L8_SINT,                 R8I,             RRR1, L, VTR_),
        PAN_V6(A8_UINT,                 R8UI,            000R, L, VTR_),
        PAN_V6(I8_UINT,                 R8UI,            RRRR, L, VTR_),
        PAN_V6(L8_UINT,                 R8UI,            RRR1, L, VTR_),
        PAN_V6(A16_SINT,                R16I,            000R, L, VTR_),
        PAN_V6(I16_SINT,                R16I,            RRRR, L, VTR_),
        PAN_V6(L16_SINT,                R16I,            RRR1, L, VTR_),
        PAN_V6(A16_UINT,                R16UI,           000R, L, VTR_),
        PAN_V6(I16_UINT,                R16UI,           RRRR, L, VTR_),
        PAN_V6(L16_UINT,                R16UI,           RRR1, L, VTR_),
        PAN_V6(A32_SINT,                R32I,            000R, L, VTR_),
        PAN_V6(I32_SINT,                R32I,            RRRR, L, VTR_),
        PAN_V6(L32_SINT,                R32I,            RRR1, L, VTR_),
        PAN_V6(A32_UINT,                R32UI,           000R, L, VTR_),
        PAN_V6(I32_UINT,                R32UI,           RRRR, L, VTR_),
        PAN_V6(L32_UINT,                R32UI,           RRR1, L, VTR_),
        PAN_V6(B8G8R8_UINT,             RGB8UI,          BGR1, L, VTR_),
        PAN_V6(B8G8R8A8_UINT,           RGBA8UI,         BGRA, L, VTR_),
        PAN_V6(B8G8R8_SINT,             RGB8I,           BGR1, L, VTR_),
        PAN_V6(B8G8R8A8_SINT,           RGBA8I,          BGRA, L, VTR_),
        PAN_V6(A8R8G8B8_UINT,           RGBA8UI,         GBAR, L, VTR_),
        PAN_V6(A8B8G8R8_UINT,           RGBA8UI,         ABGR, L, VTR_),
        PAN_V6(R8_UINT,                 R8UI,            R001, L, VTR_),
        PAN_V6(R16_UINT,                R16UI,           R001, L, VTR_),
        PAN_V6(R32_UINT,                R32UI,           R001, L, VTR_),
        PAN_V6(R8G8_UINT,               RG8UI,           RG01, L, VTR_),
        PAN_V6(R16G16_UINT,             RG16UI,          RG01, L, VTR_),
        PAN_V6(R32G32_UINT,             RG32UI,          RG01, L, VTR_),
        PAN_V6(R8G8B8_UINT,             RGB8UI,          RGB1, L, VTR_),
        PAN_V6(R16G16B16_UINT,          RGB16UI,         RGB1, L, VTR_),
        PAN_V6(R32G32B32_UINT,          RGB32UI,         RGB1, L, VTR_),
        PAN_V6(R8G8B8A8_UINT,           RGBA8UI,         RGBA, L, VTR_),
        PAN_V6(R16G16B16A16_UINT,       RGBA16UI,        RGBA, L, VTR_),
        PAN_V6(R32G32B32A32_UINT,       RGBA32UI,        RGBA, L, VTR_),
        PAN_V6(R32_FLOAT,               R32F,            R001, L, VTR_),
        PAN_V6(R32G32_FLOAT,            RG32F,           RG01, L, VTR_),
        PAN_V6(R32G32B32_FLOAT,         RGB32F,          RGB1, L, VTR_),
        PAN_V6(R32G32B32A32_FLOAT,      RGBA32F,         RGBA, L, VTR_),
        PAN_V6(R8_UNORM,                R8_UNORM,        R001, L, VTR_),
        PAN_V6(R16_UNORM,               R16_UNORM,       R001, L, VTR_),
        PAN_V6(R32_UNORM,               R32_UNORM,       R001, L, VTR_),
        PAN_V6(R8G8_UNORM,              RG8_UNORM,       RG01, L, VTR_),
        PAN_V6(R16G16_UNORM,            RG16_UNORM,      RG01, L, VTR_),
        PAN_V6(R32G32_UNORM,            RG32_UNORM,      RG01, L, VTR_),
        PAN_V6(R8G8B8_UNORM,            RGB8_UNORM,      RGB1, L, VTR_),
        PAN_V6(R16G16B16_UNORM,         RGB16_UNORM,     RGB1, L, VTR_),
        PAN_V6(R32G32B32_UNORM,         RGB32_UNORM,     RGB1, L, VTR_),
        PAN_V6(R4G4B4A4_UNORM,          RGBA4_UNORM,     RGBA, L, VTR_),
        PAN_V6(R16G16B16A16_UNORM,      RGBA16_UNORM,    RGBA, L, VTR_),
        PAN_V6(R32G32B32A32_UNORM,      RGBA32_UNORM,    RGBA, L, VTR_),
        PAN_V6(B8G8R8A8_UNORM,          RGBA8_UNORM,     BGRA, L, VTR_),
        PAN_V6(B8G8R8X8_UNORM,          RGBA8_UNORM,     BGR1, L, VTR_),
        PAN_V6(A8R8G8B8_UNORM,          RGBA8_UNORM,     GBAR, L, VTR_),
        PAN_V6(X8R8G8B8_UNORM,          RGBA8_UNORM,     GBA1, L, VTR_),
        PAN_V6(A8B8G8R8_UNORM,          RGBA8_UNORM,     ABGR, L, VTR_),
        PAN_V6(X8B8G8R8_UNORM,          RGBA8_UNORM,     ABG1, L, VTR_),
        PAN_V6(R8G8B8X8_UNORM,          RGBA8_UNORM,     RGB1, L, VTR_),
        PAN_V6(R8G8B8A8_UNORM,          RGBA8_UNORM,     RGBA, L, VTR_),
        PAN_V6(R8G8B8X8_SNORM,          RGBA8_SNORM,     RGB1, L, VT__),
        PAN_V6(R8G8B8X8_SRGB,           RGBA8_UNORM,     RGB1, S, VTR_),
        PAN_V6(R8G8B8X8_UINT,           RGBA8UI,         RGB1, L, VTR_),
        PAN_V6(R8G8B8X8_SINT,           RGBA8I,          RGB1, L, VTR_),
        PAN_V6(L8_UNORM,                R8_UNORM,        RRR1, L, VTR_),
        PAN_V6(A8_UNORM,                R8_UNORM,        000R, L, VTR_),
        PAN_V6(I8_UNORM,                R8_UNORM,        RRRR, L, VTR_),
        PAN_V6(L8A8_UNORM,              RG8_UNORM,       RRRG, L, VTR_),
        PAN_V6(L16_UNORM,               R16_UNORM,       RRR1, L, VTR_),
        PAN_V6(A16_UNORM,               R16_UNORM,       000R, L, VTR_),
        PAN_V6(I16_UNORM,               R16_UNORM,       RRRR, L, VTR_),
        PAN_V6(L8_SNORM,                R8_SNORM,        RRR1, L, VT__),
        PAN_V6(A8_SNORM,                R8_SNORM,        000R, L, VT__),
        PAN_V6(I8_SNORM,                R8_SNORM,        RRRR, L, VT__),
        PAN_V6(L16_SNORM,               R16_SNORM,       RRR1, L, VT__),
        PAN_V6(A16_SNORM,               R16_SNORM,       000R, L, VT__),
        PAN_V6(I16_SNORM,               R16_SNORM,       RRRR, L, VT__),
        PAN_V6(L16_FLOAT,               R16F,            RRR1, L, VTR_),
        PAN_V6(A16_FLOAT,               R16F,            000R, L, VTR_),
        PAN_V6(I16_FLOAT,               RG16F,           RRRR, L, VTR_),
        PAN_V6(L8_SRGB,                 R8_UNORM,        RRR1, S, VTR_),
        PAN_V6(R8_SRGB,                 R8_UNORM,        R001, S, VTR_),
        PAN_V6(L8A8_SRGB,               RG8_UNORM,       RRRG, S, VTR_),
        PAN_V6(R8G8_SRGB,               RG8_UNORM,       RG01, S, VTR_),
        PAN_V6(R8G8B8_SRGB,             RGB8_UNORM,      RGB1, S, VTR_),
        PAN_V6(B8G8R8_SRGB,             RGB8_UNORM,      BGR1, S, VTR_),
        PAN_V6(R8G8B8A8_SRGB,           RGBA8_UNORM,     RGBA, S, VTR_),
        PAN_V6(A8B8G8R8_SRGB,           RGBA8_UNORM,     ABGR, S, VTR_),
        PAN_V6(X8B8G8R8_SRGB,           RGBA8_UNORM,     ABG1, S, VTR_),
        PAN_V6(B8G8R8A8_SRGB,           RGBA8_UNORM,     BGRA, S, VTR_),
        PAN_V6(B8G8R8X8_SRGB,           RGBA8_UNORM,     BGR1, S, VTR_),
        PAN_V6(A8R8G8B8_SRGB,           RGBA8_UNORM,     GBAR, S, VTR_),
        PAN_V6(X8R8G8B8_SRGB,           RGBA8_UNORM,     GBA1, S, VTR_),
        PAN_V6(R8_SINT,                 R8I,             R001, L, VTR_),
        PAN_V6(R16_SINT,                R16I,            R001, L, VTR_),
        PAN_V6(R32_SINT,                R32I,            R001, L, VTR_),
        PAN_V6(R16_FLOAT,               R16F,            R001, L, VTR_),
        PAN_V6(R8G8_SINT,               RG8I,            RG01, L, VTR_),
        PAN_V6(R16G16_SINT,             RG16I,           RG01, L, VTR_),
        PAN_V6(R32G32_SINT,             RG32I,           RG01, L, VTR_),
        PAN_V6(R16G16_FLOAT,            RG16F,           RG01, L, VTR_),
        PAN_V6(R8G8B8_SINT,             RGB8I,           RGB1, L, VTR_),
        PAN_V6(R16G16B16_SINT,          RGB16I,          RGB1, L, VTR_),
        PAN_V6(R32G32B32_SINT,          RGB32I,          RGB1, L, VTR_),
        PAN_V6(R16G16B16_FLOAT,         RGB16F,          RGB1, L, VTR_),
        PAN_V6(R8G8B8A8_SINT,           RGBA8I,          RGBA, L, VTR_),
        PAN_V6(R16G16B16A16_SINT,       RGBA16I,         RGBA, L, VTR_),
        PAN_V6(R32G32B32A32_SINT,       RGBA32I,         RGBA, L, VTR_),
        PAN_V6(R16G16B16A16_FLOAT,      RGBA16F,         RGBA, L, VTR_),
        PAN_V6(R16G16B16X16_UNORM,      RGBA16_UNORM,    RGB1, L, VTR_),
        PAN_V6(R16G16B16X16_SNORM,      RGBA16_SNORM,    RGB1, L, VT__),
        PAN_V6(R16G16B16X16_FLOAT,      RGBA16F,         RGB1, L, VTR_),
        PAN_V6(R16G16B16X16_UINT,       RGBA16UI,        RGB1, L, VTR_),
        PAN_V6(R16G16B16X16_SINT,       RGBA16I,         RGB1, L, VTR_),
        PAN_V6(R32G32B32X32_FLOAT,      RGBA32F,         RGB1, L, VTR_),
        PAN_V6(R32G32B32X32_UINT,       RGBA32UI,        RGB1, L, VTR_),
        PAN_V6(R32G32B32X32_SINT,       RGBA32I,         RGB1, L, VTR_),
};

const struct panfrost_format panfrost_pipe_format_v7[PIPE_FORMAT_COUNT] = {
        PAN_V7(NONE,                    CONSTANT,        0000, L, VTR_),
        PAN_V7(ETC1_RGB8,               ETC2_RGB8,       RGB1, L, _T__),
        PAN_V7(ETC2_RGB8,               ETC2_RGB8,       RGB1, L, _T__),
        PAN_V7(ETC2_SRGB8,              ETC2_RGB8,       RGB1, S, _T__),
        PAN_V7(ETC2_R11_UNORM,          ETC2_R11_UNORM,  RGB1, L, _T__),
        PAN_V7(ETC2_RGBA8,              ETC2_RGBA8,      RGBA, L, _T__),
        PAN_V7(ETC2_SRGBA8,             ETC2_RGBA8,      RGBA, S, _T__),
        PAN_V7(ETC2_RG11_UNORM,         ETC2_RG11_UNORM, RGB1, L, _T__),
        PAN_V7(ETC2_R11_SNORM,          ETC2_R11_SNORM,  RGB1, L, _T__),
        PAN_V7(ETC2_RG11_SNORM,         ETC2_RG11_SNORM, RGB1, L, _T__),
        PAN_V7(ETC2_RGB8A1,             ETC2_RGB8A1,     RGBA, L, _T__),
        PAN_V7(ETC2_SRGB8A1,            ETC2_RGB8A1,     RGBA, S, _T__),
        PAN_V7(DXT1_RGB,                BC1_UNORM,       RGB1, L, _T__),
        PAN_V7(DXT1_RGBA,               BC1_UNORM,       RGBA, L, _T__),
        PAN_V7(DXT1_SRGB,               BC1_UNORM,       RGB1, S, _T__),
        PAN_V7(DXT1_SRGBA,              BC1_UNORM,       RGBA, S, _T__),
        PAN_V7(DXT3_RGBA,               BC2_UNORM,       RGBA, L, _T__),
        PAN_V7(DXT3_SRGBA,              BC2_UNORM,       RGBA, S, _T__),
        PAN_V7(DXT5_RGBA,               BC3_UNORM,       RGBA, L, _T__),
        PAN_V7(DXT5_SRGBA,              BC3_UNORM,       RGBA, S, _T__),
        PAN_V7(RGTC1_UNORM,             BC4_UNORM,       RGB1, L, _T__),
        PAN_V7(RGTC1_SNORM,             BC4_SNORM,       RGB1, L, _T__),
        PAN_V7(RGTC2_UNORM,             BC5_UNORM,       RGB1, L, _T__),
        PAN_V7(RGTC2_SNORM,             BC5_SNORM,       RGB1, L, _T__),
        PAN_V7(BPTC_RGB_FLOAT,          BC6H_SF16,       RGB1, L, _T__),
        PAN_V7(BPTC_RGB_UFLOAT,         BC6H_UF16,       RGB1, L, _T__),
        PAN_V7(BPTC_RGBA_UNORM,         BC7_UNORM,       RGBA, L, _T__),
        PAN_V7(BPTC_SRGBA,              BC7_UNORM,       RGBA, S, _T__),
        PAN_V7(ASTC_4x4,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_5x4,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_5x5,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_6x5,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_6x6,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_8x5,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_8x6,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_8x8,                ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_10x5,               ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_10x6,               ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_10x8,               ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_10x10,              ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_12x10,              ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_12x12,              ASTC_2D_HDR,     RGBA, L, _T__),
        PAN_V7(ASTC_4x4_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(ASTC_5x4_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(ASTC_5x5_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(ASTC_6x5_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(ASTC_6x6_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(ASTC_8x5_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(ASTC_8x6_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(ASTC_8x8_SRGB,           ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(ASTC_10x5_SRGB,          ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(ASTC_10x6_SRGB,          ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(ASTC_10x8_SRGB,          ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(ASTC_10x10_SRGB,         ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(ASTC_12x10_SRGB,         ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(ASTC_12x12_SRGB,         ASTC_2D_LDR,     RGBA, S, _T__),
        PAN_V7(R5G6B5_UNORM,            RGB565,          RGB1, L, VTR_),
        PAN_V7(B5G6R5_UNORM,            RGB565,          BGR1, L, VTR_),
        PAN_V7(B5G5R5X1_UNORM,          RGB5_A1_UNORM,   BGR1, L, VT__),
        PAN_V7(R5G5B5A1_UNORM,          RGB5_A1_UNORM,   RGBA, L, VTR_),
        PAN_V7(R10G10B10X2_UNORM,       RGB10_A2_UNORM,  RGB1, L, VTR_),
        PAN_V7(B10G10R10X2_UNORM,       RGB10_A2_UNORM,  BGR1, L, VTR_),
        PAN_V7(R10G10B10A2_UNORM,       RGB10_A2_UNORM,  RGBA, L, VTR_),
        PAN_V7(B10G10R10A2_UNORM,       RGB10_A2_UNORM,  BGRA, L, VTR_),
        PAN_V7(R10G10B10X2_SNORM,       RGB10_A2_SNORM,  RGB1, L, VT__),
        PAN_V7(R10G10B10A2_SNORM,       RGB10_A2_SNORM,  RGBA, L, VT__),
        PAN_V7(B10G10R10A2_SNORM,       RGB10_A2_SNORM,  BGRA, L, VT__),
        PAN_V7(R10G10B10A2_UINT,        RGB10_A2UI,      RGBA, L, VTR_),
        PAN_V7(B10G10R10A2_UINT,        RGB10_A2UI,      BGRA, L, VTR_),
        PAN_V7(R10G10B10A2_USCALED,     RGB10_A2UI,      RGBA, L, VTR_),
        PAN_V7(B10G10R10A2_USCALED,     RGB10_A2UI,      BGRA, L, VTR_),
        PAN_V7(R10G10B10A2_SINT,        RGB10_A2I,       RGBA, L, VTR_),
        PAN_V7(B10G10R10A2_SINT,        RGB10_A2I,       BGRA, L, VTR_),
        PAN_V7(R10G10B10A2_SSCALED,     RGB10_A2I,       RGBA, L, VTR_),
        PAN_V7(B10G10R10A2_SSCALED,     RGB10_A2I,       BGRA, L, VTR_),
        PAN_V7(R8_SSCALED,              R8I,             RGB1, L, V___),
        PAN_V7(R8G8_SSCALED,            RG8I,            RGB1, L, V___),
        PAN_V7(R8G8B8_SSCALED,          RGB8I,           RGB1, L, V___),
        PAN_V7(B8G8R8_SSCALED,          RGB8I,           BGR1, L, V___),
        PAN_V7(R8G8B8A8_SSCALED,        RGBA8I,          RGBA, L, V___),
        PAN_V7(B8G8R8A8_SSCALED,        RGBA8I,          BGRA, L, V___),
        PAN_V7(A8B8G8R8_SSCALED,        RGBA8I,          ABGR, L, V___),
        PAN_V7(R8_USCALED,              R8UI,            RGB1, L, V___),
        PAN_V7(R8G8_USCALED,            RG8UI,           RGB1, L, V___),
        PAN_V7(R8G8B8_USCALED,          RGB8UI,          RGB1, L, V___),
        PAN_V7(B8G8R8_USCALED,          RGB8UI,          BGR1, L, V___),
        PAN_V7(R8G8B8A8_USCALED,        RGBA8UI,         RGBA, L, V___),
        PAN_V7(B8G8R8A8_USCALED,        RGBA8UI,         BGRA, L, V___),
        PAN_V7(A8B8G8R8_USCALED,        RGBA8UI,         ABGR, L, V___),
        PAN_V7(R16_USCALED,             R16UI,           RGB1, L, V___),
        PAN_V7(R16G16_USCALED,          RG16UI,          RGB1, L, V___),
        PAN_V7(R16G16B16_USCALED,       RGB16UI,         RGB1, L, V___),
        PAN_V7(R16G16B16A16_USCALED,    RGBA16UI,        RGBA, L, V___),
        PAN_V7(R16_SSCALED,             R16I,            RGB1, L, V___),
        PAN_V7(R16G16_SSCALED,          RG16I,           RGB1, L, V___),
        PAN_V7(R16G16B16_SSCALED,       RGB16I,          RGB1, L, V___),
        PAN_V7(R16G16B16A16_SSCALED,    RGBA16I,         RGBA, L, V___),
        PAN_V7(R32_USCALED,             R32UI,           RGB1, L, V___),
        PAN_V7(R32G32_USCALED,          RG32UI,          RGB1, L, V___),
        PAN_V7(R32G32B32_USCALED,       RGB32UI,         RGB1, L, V___),
        PAN_V7(R32G32B32A32_USCALED,    RGBA32UI,        RGBA, L, V___),
        PAN_V7(R32_SSCALED,             R32I,            RGB1, L, V___),
        PAN_V7(R32G32_SSCALED,          RG32I,           RGB1, L, V___),
        PAN_V7(R32G32B32_SSCALED,       RGB32I,          RGB1, L, V___),
        PAN_V7(R32G32B32A32_SSCALED,    RGBA32I,         RGBA, L, V___),
        PAN_V7(R3G3B2_UNORM,            RGB332_UNORM,    RGB1, L, VT__),
        PAN_V7(Z16_UNORM,               RGB332_UNORM /* XXX: Deduplicate enum */,    RGBA, L, _T_Z),
        PAN_V7(Z24_UNORM_S8_UINT,       Z24X8_UNORM,     RGBA, L, _T_Z),
        PAN_V7(Z24X8_UNORM,             Z24X8_UNORM,     RGBA, L, _T_Z),
        PAN_V7(Z32_FLOAT,               R32F,            RGBA, L, _T_Z),
        PAN_V7(Z32_FLOAT_S8X24_UINT,    R32F,            RGBA, L, _T_Z),
        PAN_V7(X32_S8X24_UINT,          S8X24,           RGBA, L, _T__),
        PAN_V7(X24S8_UINT,              TILEBUFFER_NATIVE /* XXX: Deduplicate enum */, RGBA, L, _T_Z),
        PAN_V7(S8_UINT,                 S8,              RGBA, L, _T__),
        PAN_V7(R32_FIXED,               R32_FIXED,       RGB1, L, V___),
        PAN_V7(R32G32_FIXED,            RG32_FIXED,      RGB1, L, V___),
        PAN_V7(R32G32B32_FIXED,         RGB32_FIXED,     RGB1, L, V___),
        PAN_V7(R32G32B32A32_FIXED,      RGBA32_FIXED,    RGBA, L, V___),
        PAN_V7(R11G11B10_FLOAT,         R11F_G11F_B10F,  RGB1, L, VTR_),
        PAN_V7(R9G9B9E5_FLOAT,          R9F_G9F_B9F_E5F, RGB1, L, VT__),
        PAN_V7(R8_SNORM,                R8_SNORM,        RGB1, L, VT__),
        PAN_V7(R16_SNORM,               R16_SNORM,       RGB1, L, VT__),
        PAN_V7(R32_SNORM,               R32_SNORM,       RGB1, L, VT__),
        PAN_V7(R8G8_SNORM,              RG8_SNORM,       RGB1, L, VT__),
        PAN_V7(R16G16_SNORM,            RG16_SNORM,      RGB1, L, VT__),
        PAN_V7(R32G32_SNORM,            RG32_SNORM,      RGB1, L, VT__),
        PAN_V7(R8G8B8_SNORM,            RGB8_SNORM,      RGB1, L, VT__),
        PAN_V7(R16G16B16_SNORM,         RGB16_SNORM,     RGB1, L, VT__),
        PAN_V7(R32G32B32_SNORM,         RGB32_SNORM,     RGB1, L, VT__),
        PAN_V7(R8G8B8A8_SNORM,          RGBA8_SNORM,     RGBA, L, VT__),
        PAN_V7(R16G16B16A16_SNORM,      RGBA16_SNORM,    RGBA, L, VT__),
        PAN_V7(R32G32B32A32_SNORM,      RGBA32_SNORM,    RGBA, L, VT__),
        /* A8_SINT dropped on v7 */
        PAN_V7(I8_SINT,                 R8I,             RRRR, L, VTR_),
        PAN_V7(L8_SINT,                 R8I,             RRR1, L, VTR_),
        /* A8_UINT dropped on v7 */
        PAN_V7(I8_UINT,                 R8UI,            RRRR, L, VTR_),
        PAN_V7(L8_UINT,                 R8UI,            RRR1, L, VTR_),
        /* A16_SINT dropped on v7 */
        PAN_V7(I16_SINT,                R16I,            RRRR, L, VTR_),
        PAN_V7(L16_SINT,                R16I,            RRR1, L, VTR_),
        /* A16_UINT dropped on v7 */
        PAN_V7(I16_UINT,                R16UI,           RRRR, L, VTR_),
        PAN_V7(L16_UINT,                R16UI,           RRR1, L, VTR_),
        /* A32_SINT dropped on v7 */
        PAN_V7(I32_SINT,                R32I,            RRRR, L, VTR_),
        PAN_V7(L32_SINT,                R32I,            RRR1, L, VTR_),
        /* A32_UINT dropped on v7 */
        PAN_V7(I32_UINT,                R32UI,           RRRR, L, VTR_),
        PAN_V7(L32_UINT,                R32UI,           RRR1, L, VTR_),
        PAN_V7(B8G8R8_UINT,             RGB8UI,          BGR1, L, VTR_),
        PAN_V7(B8G8R8A8_UINT,           RGBA8UI,         BGRA, L, VTR_),
        PAN_V7(B8G8R8_SINT,             RGB8I,           BGR1, L, VTR_),
        PAN_V7(B8G8R8A8_SINT,           RGBA8I,          BGRA, L, VTR_),
        PAN_V7(A8R8G8B8_UINT,           RGBA8UI,         ARGB, L, VTR_),
        PAN_V7(A8B8G8R8_UINT,           RGBA8UI,         ABGR, L, VTR_),
        PAN_V7(R8_UINT,                 R8UI,            RGB1, L, VTR_),
        PAN_V7(R16_UINT,                R16UI,           RGB1, L, VTR_),
        PAN_V7(R32_UINT,                R32UI,           RGB1, L, VTR_),
        PAN_V7(R8G8_UINT,               RG8UI,           RGB1, L, VTR_),
        PAN_V7(R16G16_UINT,             RG16UI,          RGB1, L, VTR_),
        PAN_V7(R32G32_UINT,             RG32UI,          RGB1, L, VTR_),
        PAN_V7(R8G8B8_UINT,             RGB8UI,          RGB1, L, VTR_),
        PAN_V7(R16G16B16_UINT,          RGB16UI,         RGB1, L, VTR_),
        PAN_V7(R32G32B32_UINT,          RGB32UI,         RGB1, L, VTR_),
        PAN_V7(R8G8B8A8_UINT,           RGBA8UI,         RGBA, L, VTR_),
        PAN_V7(R16G16B16A16_UINT,       RGBA16UI,        RGBA, L, VTR_),
        PAN_V7(R32G32B32A32_UINT,       RGBA32UI,        RGBA, L, VTR_),
        PAN_V7(R32_FLOAT,               R32F,            RGB1, L, VTR_),
        PAN_V7(R32G32_FLOAT,            RG32F,           RGB1, L, VTR_),
        PAN_V7(R32G32B32_FLOAT,         RGB32F,          RGB1, L, VTR_),
        PAN_V7(R32G32B32A32_FLOAT,      RGBA32F,         RGBA, L, VTR_),
        PAN_V7(R8_UNORM,                R8_UNORM,        RGB1, L, VTR_),
        PAN_V7(R16_UNORM,               R16_UNORM,       RGB1, L, VTR_),
        PAN_V7(R32_UNORM,               R32_UNORM,       RGB1, L, VTR_),
        PAN_V7(R8G8_UNORM,              RG8_UNORM,       RGB1, L, VTR_),
        PAN_V7(R16G16_UNORM,            RG16_UNORM,      RGB1, L, VTR_),
        PAN_V7(R32G32_UNORM,            RG32_UNORM,      RGB1, L, VTR_),
        PAN_V7(R8G8B8_UNORM,            RGB8_UNORM,      RGB1, L, VTR_),
        PAN_V7(R16G16B16_UNORM,         RGB16_UNORM,     RGB1, L, VTR_),
        PAN_V7(R32G32B32_UNORM,         RGB32_UNORM,     RGB1, L, VTR_),
        PAN_V7(R4G4B4A4_UNORM,          RGBA4_UNORM,     RGBA, L, VTR_),
        PAN_V7(R16G16B16A16_UNORM,      RGBA16_UNORM,    RGBA, L, VTR_),
        PAN_V7(R32G32B32A32_UNORM,      RGBA32_UNORM,    RGBA, L, VTR_),
        PAN_V7(B8G8R8A8_UNORM,          RGBA8_UNORM,     BGRA, L, VTR_),
        PAN_V7(B8G8R8X8_UNORM,          RGBA8_UNORM,     BGR1, L, VTR_),
        PAN_V7(A8R8G8B8_UNORM,          RGBA8_UNORM,     ARGB, L, VTR_),
        PAN_V7(X8R8G8B8_UNORM,          RGBA8_UNORM,     1RGB, L, VTR_),
        PAN_V7(A8B8G8R8_UNORM,          RGBA8_UNORM,     ABGR, L, VTR_),
        PAN_V7(X8B8G8R8_UNORM,          RGBA8_UNORM,     1BGR, L, VTR_),
        PAN_V7(R8G8B8X8_UNORM,          RGBA8_UNORM,     RGB1, L, VTR_),
        PAN_V7(R8G8B8A8_UNORM,          RGBA8_UNORM,     RGBA, L, VTR_),
        PAN_V7(R8G8B8X8_SNORM,          RGBA8_SNORM,     RGB1, L, VT__),
        PAN_V7(R8G8B8X8_SRGB,           RGBA8_UNORM,     RGB1, S, VTR_),
        PAN_V7(R8G8B8X8_UINT,           RGBA8UI,         RGB1, L, VTR_),
        PAN_V7(R8G8B8X8_SINT,           RGBA8I,          RGB1, L, VTR_),
        PAN_V7(L8_UNORM,                R8_UNORM,        RRR1, L, VTR_),
        PAN_V7(A8_UNORM,                A8_UNORM,        000A, L, VTR_),
        PAN_V7(I8_UNORM,                R8_UNORM,        RRRR, L, VTR_),
        PAN_V7(L8A8_UNORM,              R8A8_UNORM,      RRRA, L, VTR_),
        PAN_V7(L16_UNORM,               R16_UNORM,       RRR1, L, VTR_),
        /* A16_UNORM dropped on v7 */
        PAN_V7(I16_UNORM,               R16_UNORM,       RRRR, L, VTR_),
        PAN_V7(L8_SNORM,                R8_SNORM,        RRR1, L, VT__),
        /* A8_SNORM dropped on v7 */
        PAN_V7(I8_SNORM,                R8_SNORM,        RRRR, L, VT__),
        PAN_V7(L16_SNORM,               R16_SNORM,       RRR1, L, VT__),
        /* A16_SNORM dropped on v7 */
        PAN_V7(I16_SNORM,               R16_SNORM,       RRRR, L, VT__),
        PAN_V7(L16_FLOAT,               R16F,            RRR1, L, VTR_),
        /* A16_FLOAT dropped on v7 */
        PAN_V7(I16_FLOAT,               RG16F,           RRRR, L, VTR_),
        PAN_V7(L8_SRGB,                 R8_UNORM,        RRR1, S, VTR_),
        PAN_V7(R8_SRGB,                 R8_UNORM,        RGB1, S, VTR_),
        PAN_V7(L8A8_SRGB,               R8A8_UNORM,      RRRA, S, VTR_),
        PAN_V7(R8G8_SRGB,               RG8_UNORM,       RGB1, S, VTR_),
        PAN_V7(R8G8B8_SRGB,             RGB8_UNORM,      RGB1, S, VTR_),
        PAN_V7(B8G8R8_SRGB,             RGB8_UNORM,      BGR1, S, VTR_),
        PAN_V7(R8G8B8A8_SRGB,           RGBA8_UNORM,     RGBA, S, VTR_),
        PAN_V7(A8B8G8R8_SRGB,           RGBA8_UNORM,     ABGR, S, VTR_),
        PAN_V7(X8B8G8R8_SRGB,           RGBA8_UNORM,     1BGR, S, VTR_),
        PAN_V7(B8G8R8A8_SRGB,           RGBA8_UNORM,     BGRA, S, VTR_),
        PAN_V7(B8G8R8X8_SRGB,           RGBA8_UNORM,     BGR1, S, VTR_),
        PAN_V7(A8R8G8B8_SRGB,           RGBA8_UNORM,     ARGB, S, VTR_),
        PAN_V7(X8R8G8B8_SRGB,           RGBA8_UNORM,     1RGB, S, VTR_),
        PAN_V7(R8_SINT,                 R8I,             RGB1, L, VTR_),
        PAN_V7(R16_SINT,                R16I,            RGB1, L, VTR_),
        PAN_V7(R32_SINT,                R32I,            RGB1, L, VTR_),
        PAN_V7(R16_FLOAT,               R16F,            RGB1, L, VTR_),
        PAN_V7(R8G8_SINT,               RG8I,            RGB1, L, VTR_),
        PAN_V7(R16G16_SINT,             RG16I,           RGB1, L, VTR_),
        PAN_V7(R32G32_SINT,             RG32I,           RGB1, L, VTR_),
        PAN_V7(R16G16_FLOAT,            RG16F,           RGB1, L, VTR_),
        PAN_V7(R8G8B8_SINT,             RGB8I,           RGB1, L, VTR_),
        PAN_V7(R16G16B16_SINT,          RGB16I,          RGB1, L, VTR_),
        PAN_V7(R32G32B32_SINT,          RGB32I,          RGB1, L, VTR_),
        PAN_V7(R16G16B16_FLOAT,         RGB16F,          RGB1, L, VTR_),
        PAN_V7(R8G8B8A8_SINT,           RGBA8I,          RGBA, L, VTR_),
        PAN_V7(R16G16B16A16_SINT,       RGBA16I,         RGBA, L, VTR_),
        PAN_V7(R32G32B32A32_SINT,       RGBA32I,         RGBA, L, VTR_),
        PAN_V7(R16G16B16A16_FLOAT,      RGBA16F,         RGBA, L, VTR_),
        PAN_V7(R16G16B16X16_UNORM,      RGBA16_UNORM,    RGB1, L, VTR_),
        PAN_V7(R16G16B16X16_SNORM,      RGBA16_SNORM,    RGB1, L, VT__),
        PAN_V7(R16G16B16X16_FLOAT,      RGBA16F,         RGB1, L, VTR_),
        PAN_V7(R16G16B16X16_UINT,       RGBA16UI,        RGB1, L, VTR_),
        PAN_V7(R16G16B16X16_SINT,       RGBA16I,         RGB1, L, VTR_),
        PAN_V7(R32G32B32X32_FLOAT,      RGBA32F,         RGB1, L, VTR_),
        PAN_V7(R32G32B32X32_UINT,       RGBA32UI,        RGB1, L, VTR_),
        PAN_V7(R32G32B32X32_SINT,       RGBA32I,         RGB1, L, VTR_),
};

/* Translate a PIPE swizzle quad to a 12-bit Mali swizzle code. PIPE
 * swizzles line up with Mali swizzles for the XYZW01, but PIPE swizzles have
 * an additional "NONE" field that we have to mask out to zero. Additionally,
 * PIPE swizzles are sparse but Mali swizzles are packed */

unsigned
panfrost_translate_swizzle_4(const unsigned char swizzle[4])
{
        unsigned out = 0;

        for (unsigned i = 0; i < 4; ++i) {
                unsigned translated = (swizzle[i] > PIPE_SWIZZLE_1) ? PIPE_SWIZZLE_0 : swizzle[i];
                out |= (translated << (3*i));
        }

        return out;
}

void
panfrost_invert_swizzle(const unsigned char *in, unsigned char *out)
{
        /* First, default to all zeroes to prevent uninitialized junk */

        for (unsigned c = 0; c < 4; ++c)
                out[c] = PIPE_SWIZZLE_0;

        /* Now "do" what the swizzle says */

        for (unsigned c = 0; c < 4; ++c) {
                unsigned char i = in[c];

                /* Who cares? */
                assert(PIPE_SWIZZLE_X == 0);
                if (i > PIPE_SWIZZLE_W)
                        continue;

                /* Invert */
                unsigned idx = i - PIPE_SWIZZLE_X;
                out[idx] = PIPE_SWIZZLE_X + c;
        }
}

unsigned
panfrost_format_to_bifrost_blend(const struct panfrost_device *dev,
                                 const struct util_format_description *desc, bool dither)
{
        struct pan_blendable_format fmt = panfrost_blend_format(desc->format);

        /* Formats requiring blend shaders are stored raw in the tilebuffer */
        if (!fmt.internal)
                return dev->formats[desc->format].hw;

        unsigned extra = 0;

        if (dev->quirks & HAS_SWIZZLES)
                extra |= panfrost_get_default_swizzle(4);

        if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB)
                extra |= 1 << 20;

        /* Else, pick the pixel format matching the tilebuffer format */
        switch (fmt.internal) {
#define TB_FORMAT(in, out) \
        case MALI_COLOR_BUFFER_INTERNAL_FORMAT_ ## in: \
                return (MALI_ ## out << 12) | extra

#define TB_FORMAT_DITHER(in, out) \
        case MALI_COLOR_BUFFER_INTERNAL_FORMAT_ ## in: \
                return ((dither ? MALI_ ## out ## _AU : MALI_ ## out ## _PU) << 12) | extra

        TB_FORMAT(R8G8B8A8, RGBA8_TB);
        TB_FORMAT(R10G10B10A2, RGB10_A2_TB);
        TB_FORMAT_DITHER(R8G8B8A2, RGB8_A2);
        TB_FORMAT_DITHER(R4G4B4A4, RGBA4);
        TB_FORMAT_DITHER(R5G6B5A0, R5G6B5);
        TB_FORMAT_DITHER(R5G5B5A1, RGB5_A1);

#undef TB_FORMAT_DITHER
#undef TB_FORMAT

        default:
                unreachable("invalid internal blendable");
        }
}

enum mali_z_internal_format
panfrost_get_z_internal_format(enum pipe_format fmt)
{
         switch (fmt) {
         case PIPE_FORMAT_Z16_UNORM:
         case PIPE_FORMAT_Z16_UNORM_S8_UINT:
                return MALI_Z_INTERNAL_FORMAT_D16;
         case PIPE_FORMAT_Z24_UNORM_S8_UINT:
         case PIPE_FORMAT_Z24X8_UNORM:
                return MALI_Z_INTERNAL_FORMAT_D24;
         case PIPE_FORMAT_Z32_FLOAT:
         case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
                return MALI_Z_INTERNAL_FORMAT_D32;
         default:
                unreachable("Unsupported depth/stencil format.");
         }
}
