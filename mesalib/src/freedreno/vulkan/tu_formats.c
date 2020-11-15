
/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tu_private.h"

#include "adreno_common.xml.h"
#include "a6xx.xml.h"

#include "vk_format.h"
#include "vk_util.h"
#include "drm-uapi/drm_fourcc.h"

#define TU6_FMT(vkfmt, hwfmt, swapfmt, valid) \
   [VK_FORMAT_##vkfmt] = {                   \
      .fmt = FMT6_##hwfmt,                     \
      .swap = swapfmt,                       \
      .supported = valid,                    \
   }

#define TU6_VTC(vk, fmt, swap) TU6_FMT(vk, fmt, swap, FMT_VERTEX | FMT_TEXTURE | FMT_COLOR)
#define TU6_xTC(vk, fmt, swap) TU6_FMT(vk, fmt, swap, FMT_TEXTURE | FMT_COLOR)
#define TU6_Vxx(vk, fmt, swap) TU6_FMT(vk, fmt, swap, FMT_VERTEX)
#define TU6_xTx(vk, fmt, swap) TU6_FMT(vk, fmt, swap, FMT_TEXTURE)
#define TU6_xxx(vk, fmt, swap) TU6_FMT(vk, NONE, WZYX, 0)

static const struct tu_native_format tu6_format_table[] = {
   TU6_xxx(UNDEFINED,                  x,                 x),    /* 0 */

   /* 8-bit packed */
   TU6_xxx(R4G4_UNORM_PACK8,           4_4_UNORM,         WZXY), /* 1 */

   /* 16-bit packed */
   TU6_xTC(R4G4B4A4_UNORM_PACK16,      4_4_4_4_UNORM,     XYZW), /* 2 */
   TU6_xTC(B4G4R4A4_UNORM_PACK16,      4_4_4_4_UNORM,     ZYXW), /* 3 */
   TU6_xTC(R5G6B5_UNORM_PACK16,        5_6_5_UNORM,       WXYZ), /* 4 */
   TU6_xTC(B5G6R5_UNORM_PACK16,        5_6_5_UNORM,       WZYX), /* 5 */
   TU6_xTC(R5G5B5A1_UNORM_PACK16,      5_5_5_1_UNORM,     XYZW), /* 6 */
   TU6_xTC(B5G5R5A1_UNORM_PACK16,      5_5_5_1_UNORM,     ZYXW), /* 7 */
   TU6_xTC(A1R5G5B5_UNORM_PACK16,      5_5_5_1_UNORM,     WXYZ), /* 8 */

   /* 8-bit R */
   TU6_VTC(R8_UNORM,                   8_UNORM,           WZYX), /* 9 */
   TU6_VTC(R8_SNORM,                   8_SNORM,           WZYX), /* 10 */
   TU6_Vxx(R8_USCALED,                 8_UINT,            WZYX), /* 11 */
   TU6_Vxx(R8_SSCALED,                 8_SINT,            WZYX), /* 12 */
   TU6_VTC(R8_UINT,                    8_UINT,            WZYX), /* 13 */
   TU6_VTC(R8_SINT,                    8_SINT,            WZYX), /* 14 */
   TU6_xTC(R8_SRGB,                    8_UNORM,           WZYX), /* 15 */

   /* 16-bit RG */
   TU6_VTC(R8G8_UNORM,                 8_8_UNORM,         WZYX), /* 16 */
   TU6_VTC(R8G8_SNORM,                 8_8_SNORM,         WZYX), /* 17 */
   TU6_Vxx(R8G8_USCALED,               8_8_UINT,          WZYX), /* 18 */
   TU6_Vxx(R8G8_SSCALED,               8_8_SINT,          WZYX), /* 19 */
   TU6_VTC(R8G8_UINT,                  8_8_UINT,          WZYX), /* 20 */
   TU6_VTC(R8G8_SINT,                  8_8_SINT,          WZYX), /* 21 */
   TU6_xTC(R8G8_SRGB,                  8_8_UNORM,         WZYX), /* 22 */

   /* 24-bit RGB */
   TU6_Vxx(R8G8B8_UNORM,               8_8_8_UNORM,       WZYX), /* 23 */
   TU6_Vxx(R8G8B8_SNORM,               8_8_8_SNORM,       WZYX), /* 24 */
   TU6_Vxx(R8G8B8_USCALED,             8_8_8_UINT,        WZYX), /* 25 */
   TU6_Vxx(R8G8B8_SSCALED,             8_8_8_SINT,        WZYX), /* 26 */
   TU6_Vxx(R8G8B8_UINT,                8_8_8_UINT,        WZYX), /* 27 */
   TU6_Vxx(R8G8B8_SINT,                8_8_8_SINT,        WZYX), /* 28 */
   TU6_xxx(R8G8B8_SRGB,                8_8_8_UNORM,       WZYX), /* 29 */

   /* 24-bit BGR */
   TU6_xxx(B8G8R8_UNORM,               8_8_8_UNORM,       WXYZ), /* 30 */
   TU6_xxx(B8G8R8_SNORM,               8_8_8_SNORM,       WXYZ), /* 31 */
   TU6_xxx(B8G8R8_USCALED,             8_8_8_UINT,        WXYZ), /* 32 */
   TU6_xxx(B8G8R8_SSCALED,             8_8_8_SINT,        WXYZ), /* 33 */
   TU6_xxx(B8G8R8_UINT,                8_8_8_UINT,        WXYZ), /* 34 */
   TU6_xxx(B8G8R8_SINT,                8_8_8_SINT,        WXYZ), /* 35 */
   TU6_xxx(B8G8R8_SRGB,                8_8_8_UNORM,       WXYZ), /* 36 */

   /* 32-bit RGBA */
   TU6_VTC(R8G8B8A8_UNORM,             8_8_8_8_UNORM,     WZYX), /* 37 */
   TU6_VTC(R8G8B8A8_SNORM,             8_8_8_8_SNORM,     WZYX), /* 38 */
   TU6_Vxx(R8G8B8A8_USCALED,           8_8_8_8_UINT,      WZYX), /* 39 */
   TU6_Vxx(R8G8B8A8_SSCALED,           8_8_8_8_SINT,      WZYX), /* 40 */
   TU6_VTC(R8G8B8A8_UINT,              8_8_8_8_UINT,      WZYX), /* 41 */
   TU6_VTC(R8G8B8A8_SINT,              8_8_8_8_SINT,      WZYX), /* 42 */
   TU6_xTC(R8G8B8A8_SRGB,              8_8_8_8_UNORM,     WZYX), /* 43 */

   /* 32-bit BGRA */
   TU6_VTC(B8G8R8A8_UNORM,             8_8_8_8_UNORM,     WXYZ), /* 44 */
   TU6_VTC(B8G8R8A8_SNORM,             8_8_8_8_SNORM,     WXYZ), /* 45 */
   TU6_Vxx(B8G8R8A8_USCALED,           8_8_8_8_UINT,      WXYZ), /* 46 */
   TU6_Vxx(B8G8R8A8_SSCALED,           8_8_8_8_SINT,      WXYZ), /* 47 */
   TU6_VTC(B8G8R8A8_UINT,              8_8_8_8_UINT,      WXYZ), /* 48 */
   TU6_VTC(B8G8R8A8_SINT,              8_8_8_8_SINT,      WXYZ), /* 49 */
   TU6_xTC(B8G8R8A8_SRGB,              8_8_8_8_UNORM,     WXYZ), /* 50 */

   /* 32-bit packed */
   TU6_VTC(A8B8G8R8_UNORM_PACK32,      8_8_8_8_UNORM,     WZYX), /* 51 */
   TU6_VTC(A8B8G8R8_SNORM_PACK32,      8_8_8_8_SNORM,     WZYX), /* 52 */
   TU6_Vxx(A8B8G8R8_USCALED_PACK32,    8_8_8_8_UINT,      WZYX), /* 53 */
   TU6_Vxx(A8B8G8R8_SSCALED_PACK32,    8_8_8_8_SINT,      WZYX), /* 54 */
   TU6_VTC(A8B8G8R8_UINT_PACK32,       8_8_8_8_UINT,      WZYX), /* 55 */
   TU6_VTC(A8B8G8R8_SINT_PACK32,       8_8_8_8_SINT,      WZYX), /* 56 */
   TU6_xTC(A8B8G8R8_SRGB_PACK32,       8_8_8_8_UNORM,     WZYX), /* 57 */
   TU6_VTC(A2R10G10B10_UNORM_PACK32,   10_10_10_2_UNORM,  WXYZ), /* 58 */
   TU6_Vxx(A2R10G10B10_SNORM_PACK32,   10_10_10_2_SNORM,  WXYZ), /* 59 */
   TU6_Vxx(A2R10G10B10_USCALED_PACK32, 10_10_10_2_UINT,   WXYZ), /* 60 */
   TU6_Vxx(A2R10G10B10_SSCALED_PACK32, 10_10_10_2_SINT,   WXYZ), /* 61 */
   TU6_VTC(A2R10G10B10_UINT_PACK32,    10_10_10_2_UINT,   WXYZ), /* 62 */
   TU6_Vxx(A2R10G10B10_SINT_PACK32,    10_10_10_2_SINT,   WXYZ), /* 63 */
   TU6_VTC(A2B10G10R10_UNORM_PACK32,   10_10_10_2_UNORM,  WZYX), /* 64 */
   TU6_Vxx(A2B10G10R10_SNORM_PACK32,   10_10_10_2_SNORM,  WZYX), /* 65 */
   TU6_Vxx(A2B10G10R10_USCALED_PACK32, 10_10_10_2_UINT,   WZYX), /* 66 */
   TU6_Vxx(A2B10G10R10_SSCALED_PACK32, 10_10_10_2_SINT,   WZYX), /* 67 */
   TU6_VTC(A2B10G10R10_UINT_PACK32,    10_10_10_2_UINT,   WZYX), /* 68 */
   TU6_Vxx(A2B10G10R10_SINT_PACK32,    10_10_10_2_SINT,   WZYX), /* 69 */

   /* 16-bit R */
   TU6_VTC(R16_UNORM,                  16_UNORM,          WZYX), /* 70 */
   TU6_VTC(R16_SNORM,                  16_SNORM,          WZYX), /* 71 */
   TU6_Vxx(R16_USCALED,                16_UINT,           WZYX), /* 72 */
   TU6_Vxx(R16_SSCALED,                16_SINT,           WZYX), /* 73 */
   TU6_VTC(R16_UINT,                   16_UINT,           WZYX), /* 74 */
   TU6_VTC(R16_SINT,                   16_SINT,           WZYX), /* 75 */
   TU6_VTC(R16_SFLOAT,                 16_FLOAT,          WZYX), /* 76 */

   /* 32-bit RG */
   TU6_VTC(R16G16_UNORM,               16_16_UNORM,       WZYX), /* 77 */
   TU6_VTC(R16G16_SNORM,               16_16_SNORM,       WZYX), /* 78 */
   TU6_Vxx(R16G16_USCALED,             16_16_UINT,        WZYX), /* 79 */
   TU6_Vxx(R16G16_SSCALED,             16_16_SINT,        WZYX), /* 80 */
   TU6_VTC(R16G16_UINT,                16_16_UINT,        WZYX), /* 81 */
   TU6_VTC(R16G16_SINT,                16_16_SINT,        WZYX), /* 82 */
   TU6_VTC(R16G16_SFLOAT,              16_16_FLOAT,       WZYX), /* 83 */

   /* 48-bit RGB */
   TU6_Vxx(R16G16B16_UNORM,            16_16_16_UNORM,    WZYX), /* 84 */
   TU6_Vxx(R16G16B16_SNORM,            16_16_16_SNORM,    WZYX), /* 85 */
   TU6_Vxx(R16G16B16_USCALED,          16_16_16_UINT,     WZYX), /* 86 */
   TU6_Vxx(R16G16B16_SSCALED,          16_16_16_SINT,     WZYX), /* 87 */
   TU6_Vxx(R16G16B16_UINT,             16_16_16_UINT,     WZYX), /* 88 */
   TU6_Vxx(R16G16B16_SINT,             16_16_16_SINT,     WZYX), /* 89 */
   TU6_Vxx(R16G16B16_SFLOAT,           16_16_16_FLOAT,    WZYX), /* 90 */

   /* 64-bit RGBA */
   TU6_VTC(R16G16B16A16_UNORM,         16_16_16_16_UNORM, WZYX), /* 91 */
   TU6_VTC(R16G16B16A16_SNORM,         16_16_16_16_SNORM, WZYX), /* 92 */
   TU6_Vxx(R16G16B16A16_USCALED,       16_16_16_16_UINT,  WZYX), /* 93 */
   TU6_Vxx(R16G16B16A16_SSCALED,       16_16_16_16_SINT,  WZYX), /* 94 */
   TU6_VTC(R16G16B16A16_UINT,          16_16_16_16_UINT,  WZYX), /* 95 */
   TU6_VTC(R16G16B16A16_SINT,          16_16_16_16_SINT,  WZYX), /* 96 */
   TU6_VTC(R16G16B16A16_SFLOAT,        16_16_16_16_FLOAT, WZYX), /* 97 */

   /* 32-bit R */
   TU6_VTC(R32_UINT,                   32_UINT,           WZYX), /* 98 */
   TU6_VTC(R32_SINT,                   32_SINT,           WZYX), /* 99 */
   TU6_VTC(R32_SFLOAT,                 32_FLOAT,          WZYX), /* 100 */

   /* 64-bit RG */
   TU6_VTC(R32G32_UINT,                32_32_UINT,        WZYX), /* 101 */
   TU6_VTC(R32G32_SINT,                32_32_SINT,        WZYX), /* 102 */
   TU6_VTC(R32G32_SFLOAT,              32_32_FLOAT,       WZYX), /* 103 */

   /* 96-bit RGB */
   TU6_Vxx(R32G32B32_UINT,             32_32_32_UINT,     WZYX), /* 104 */
   TU6_Vxx(R32G32B32_SINT,             32_32_32_SINT,     WZYX), /* 105 */
   TU6_Vxx(R32G32B32_SFLOAT,           32_32_32_FLOAT,    WZYX), /* 106 */

   /* 128-bit RGBA */
   TU6_VTC(R32G32B32A32_UINT,          32_32_32_32_UINT,  WZYX), /* 107 */
   TU6_VTC(R32G32B32A32_SINT,          32_32_32_32_SINT,  WZYX), /* 108 */
   TU6_VTC(R32G32B32A32_SFLOAT,        32_32_32_32_FLOAT, WZYX), /* 109 */

   /* 64-bit R */
   TU6_xxx(R64_UINT,                   64_UINT,           WZYX), /* 110 */
   TU6_xxx(R64_SINT,                   64_SINT,           WZYX), /* 111 */
   TU6_xxx(R64_SFLOAT,                 64_FLOAT,          WZYX), /* 112 */

   /* 128-bit RG */
   TU6_xxx(R64G64_UINT,                64_64_UINT,        WZYX), /* 113 */
   TU6_xxx(R64G64_SINT,                64_64_SINT,        WZYX), /* 114 */
   TU6_xxx(R64G64_SFLOAT,              64_64_FLOAT,       WZYX), /* 115 */

   /* 192-bit RGB */
   TU6_xxx(R64G64B64_UINT,             64_64_64_UINT,     WZYX), /* 116 */
   TU6_xxx(R64G64B64_SINT,             64_64_64_SINT,     WZYX), /* 117 */
   TU6_xxx(R64G64B64_SFLOAT,           64_64_64_FLOAT,    WZYX), /* 118 */

   /* 256-bit RGBA */
   TU6_xxx(R64G64B64A64_UINT,          64_64_64_64_UINT,  WZYX), /* 119 */
   TU6_xxx(R64G64B64A64_SINT,          64_64_64_64_SINT,  WZYX), /* 120 */
   TU6_xxx(R64G64B64A64_SFLOAT,        64_64_64_64_FLOAT, WZYX), /* 121 */

   /* 32-bit packed float */
   TU6_VTC(B10G11R11_UFLOAT_PACK32,    11_11_10_FLOAT,    WZYX), /* 122 */
   TU6_xTx(E5B9G9R9_UFLOAT_PACK32,     9_9_9_E5_FLOAT,    WZYX), /* 123 */

   /* depth/stencil
    * X8_D24_UNORM/D24_UNORM_S8_UINT should be Z24_UNORM_S8_UINT_AS_R8G8B8A8
    * but the format doesn't work on A630 when UBWC is disabled, so use
    * 8_8_8_8_UNORM as the default and override it when UBWC is enabled
    */
   TU6_xTC(D16_UNORM,                  16_UNORM,                      WZYX), /* 124 */
   TU6_xTC(X8_D24_UNORM_PACK32,        8_8_8_8_UNORM,                 WZYX), /* 125 */
   TU6_xTC(D32_SFLOAT,                 32_FLOAT,                      WZYX), /* 126 */
   TU6_xTC(S8_UINT,                    8_UINT,                        WZYX), /* 127 */
   TU6_xxx(D16_UNORM_S8_UINT,          X8Z16_UNORM,                   WZYX), /* 128 */
   TU6_xTC(D24_UNORM_S8_UINT,          8_8_8_8_UNORM,                 WZYX), /* 129 */
   TU6_xTC(D32_SFLOAT_S8_UINT,         NONE,                          WZYX), /* 130 */

   /* compressed */
   TU6_xTx(BC1_RGB_UNORM_BLOCK,        DXT1,              WZYX), /* 131 */
   TU6_xTx(BC1_RGB_SRGB_BLOCK,         DXT1,              WZYX), /* 132 */
   TU6_xTx(BC1_RGBA_UNORM_BLOCK,       DXT1,              WZYX), /* 133 */
   TU6_xTx(BC1_RGBA_SRGB_BLOCK,        DXT1,              WZYX), /* 134 */
   TU6_xTx(BC2_UNORM_BLOCK,            DXT3,              WZYX), /* 135 */
   TU6_xTx(BC2_SRGB_BLOCK,             DXT3,              WZYX), /* 136 */
   TU6_xTx(BC3_UNORM_BLOCK,            DXT5,              WZYX), /* 137 */
   TU6_xTx(BC3_SRGB_BLOCK,             DXT5,              WZYX), /* 138 */
   TU6_xTx(BC4_UNORM_BLOCK,            RGTC1_UNORM,       WZYX), /* 139 */
   TU6_xTx(BC4_SNORM_BLOCK,            RGTC1_SNORM,       WZYX), /* 140 */
   TU6_xTx(BC5_UNORM_BLOCK,            RGTC2_UNORM,       WZYX), /* 141 */
   TU6_xTx(BC5_SNORM_BLOCK,            RGTC2_SNORM,       WZYX), /* 142 */
   TU6_xTx(BC6H_UFLOAT_BLOCK,          BPTC_UFLOAT,       WZYX), /* 143 */
   TU6_xTx(BC6H_SFLOAT_BLOCK,          BPTC_FLOAT,        WZYX), /* 144 */
   TU6_xTx(BC7_UNORM_BLOCK,            BPTC,              WZYX), /* 145 */
   TU6_xTx(BC7_SRGB_BLOCK,             BPTC,              WZYX), /* 146 */
   TU6_xTx(ETC2_R8G8B8_UNORM_BLOCK,    ETC2_RGB8,         WZYX), /* 147 */
   TU6_xTx(ETC2_R8G8B8_SRGB_BLOCK,     ETC2_RGB8,         WZYX), /* 148 */
   TU6_xTx(ETC2_R8G8B8A1_UNORM_BLOCK,  ETC2_RGB8A1,       WZYX), /* 149 */
   TU6_xTx(ETC2_R8G8B8A1_SRGB_BLOCK,   ETC2_RGB8A1,       WZYX), /* 150 */
   TU6_xTx(ETC2_R8G8B8A8_UNORM_BLOCK,  ETC2_RGBA8,        WZYX), /* 151 */
   TU6_xTx(ETC2_R8G8B8A8_SRGB_BLOCK,   ETC2_RGBA8,        WZYX), /* 152 */
   TU6_xTx(EAC_R11_UNORM_BLOCK,        ETC2_R11_UNORM,    WZYX), /* 153 */
   TU6_xTx(EAC_R11_SNORM_BLOCK,        ETC2_R11_SNORM,    WZYX), /* 154 */
   TU6_xTx(EAC_R11G11_UNORM_BLOCK,     ETC2_RG11_UNORM,   WZYX), /* 155 */
   TU6_xTx(EAC_R11G11_SNORM_BLOCK,     ETC2_RG11_SNORM,   WZYX), /* 156 */
   TU6_xTx(ASTC_4x4_UNORM_BLOCK,       ASTC_4x4,          WZYX), /* 157 */
   TU6_xTx(ASTC_4x4_SRGB_BLOCK,        ASTC_4x4,          WZYX), /* 158 */
   TU6_xTx(ASTC_5x4_UNORM_BLOCK,       ASTC_5x4,          WZYX), /* 159 */
   TU6_xTx(ASTC_5x4_SRGB_BLOCK,        ASTC_5x4,          WZYX), /* 160 */
   TU6_xTx(ASTC_5x5_UNORM_BLOCK,       ASTC_5x5,          WZYX), /* 161 */
   TU6_xTx(ASTC_5x5_SRGB_BLOCK,        ASTC_5x5,          WZYX), /* 162 */
   TU6_xTx(ASTC_6x5_UNORM_BLOCK,       ASTC_6x5,          WZYX), /* 163 */
   TU6_xTx(ASTC_6x5_SRGB_BLOCK,        ASTC_6x5,          WZYX), /* 164 */
   TU6_xTx(ASTC_6x6_UNORM_BLOCK,       ASTC_6x6,          WZYX), /* 165 */
   TU6_xTx(ASTC_6x6_SRGB_BLOCK,        ASTC_6x6,          WZYX), /* 166 */
   TU6_xTx(ASTC_8x5_UNORM_BLOCK,       ASTC_8x5,          WZYX), /* 167 */
   TU6_xTx(ASTC_8x5_SRGB_BLOCK,        ASTC_8x5,          WZYX), /* 168 */
   TU6_xTx(ASTC_8x6_UNORM_BLOCK,       ASTC_8x6,          WZYX), /* 169 */
   TU6_xTx(ASTC_8x6_SRGB_BLOCK,        ASTC_8x6,          WZYX), /* 170 */
   TU6_xTx(ASTC_8x8_UNORM_BLOCK,       ASTC_8x8,          WZYX), /* 171 */
   TU6_xTx(ASTC_8x8_SRGB_BLOCK,        ASTC_8x8,          WZYX), /* 172 */
   TU6_xTx(ASTC_10x5_UNORM_BLOCK,      ASTC_10x5,         WZYX), /* 173 */
   TU6_xTx(ASTC_10x5_SRGB_BLOCK,       ASTC_10x5,         WZYX), /* 174 */
   TU6_xTx(ASTC_10x6_UNORM_BLOCK,      ASTC_10x6,         WZYX), /* 175 */
   TU6_xTx(ASTC_10x6_SRGB_BLOCK,       ASTC_10x6,         WZYX), /* 176 */
   TU6_xTx(ASTC_10x8_UNORM_BLOCK,      ASTC_10x8,         WZYX), /* 177 */
   TU6_xTx(ASTC_10x8_SRGB_BLOCK,       ASTC_10x8,         WZYX), /* 178 */
   TU6_xTx(ASTC_10x10_UNORM_BLOCK,     ASTC_10x10,        WZYX), /* 179 */
   TU6_xTx(ASTC_10x10_SRGB_BLOCK,      ASTC_10x10,        WZYX), /* 180 */
   TU6_xTx(ASTC_12x10_UNORM_BLOCK,     ASTC_12x10,        WZYX), /* 181 */
   TU6_xTx(ASTC_12x10_SRGB_BLOCK,      ASTC_12x10,        WZYX), /* 182 */
   TU6_xTx(ASTC_12x12_UNORM_BLOCK,     ASTC_12x12,        WZYX), /* 183 */
   TU6_xTx(ASTC_12x12_SRGB_BLOCK,      ASTC_12x12,        WZYX), /* 184 */
};

#undef TU6_FMT
#define TU6_FMT(vkfmt, hwfmt, swapfmt, valid)   \
   case VK_FORMAT_##vkfmt:                      \
      fmt = (struct tu_native_format) {         \
         .fmt = FMT6_##hwfmt,                   \
         .swap = swapfmt,                       \
         .supported = valid,                    \
      }; break;

static struct tu_native_format
tu6_get_native_format(VkFormat format)
{
   struct tu_native_format fmt = {};

   if (format < ARRAY_SIZE(tu6_format_table)) {
      fmt = tu6_format_table[format];
   } else {
      switch (format) {
      TU6_xTx(G8B8G8R8_422_UNORM,         R8G8R8B8_422_UNORM,        WZYX)
      TU6_xTx(B8G8R8G8_422_UNORM,         G8R8B8R8_422_UNORM,        WZYX)
      TU6_xTx(G8_B8_R8_3PLANE_420_UNORM,  R8_G8_B8_3PLANE_420_UNORM, WZYX)
      TU6_xTx(G8_B8R8_2PLANE_420_UNORM,   R8_G8B8_2PLANE_420_UNORM,  WZYX)
      TU6_xTC(A4R4G4B4_UNORM_PACK16_EXT,  4_4_4_4_UNORM,             WXYZ)
      TU6_xTC(A4B4G4R4_UNORM_PACK16_EXT,  4_4_4_4_UNORM,             WZYX)
      default:
         break;
      }
   }

   if (fmt.supported && vk_format_to_pipe_format(format) == PIPE_FORMAT_NONE) {
      tu_finishme("vk_format %d missing matching pipe format.\n", format);
      fmt.supported = false;
   }

   return fmt;
}

struct tu_native_format
tu6_format_vtx(VkFormat format)
{
   struct tu_native_format fmt = tu6_get_native_format(format);
   assert(fmt.supported & FMT_VERTEX);
   return fmt;
}

struct tu_native_format
tu6_format_color(VkFormat format, enum a6xx_tile_mode tile_mode)
{
   struct tu_native_format fmt = tu6_get_native_format(format);
   assert(fmt.supported & FMT_COLOR);

   if (fmt.fmt == FMT6_10_10_10_2_UNORM)
      fmt.fmt = FMT6_10_10_10_2_UNORM_DEST;

   if (tile_mode)
      fmt.swap = WZYX;

   return fmt;
}

struct tu_native_format
tu6_format_texture(VkFormat format, enum a6xx_tile_mode tile_mode)
{
   struct tu_native_format fmt = tu6_get_native_format(format);
   assert(fmt.supported & FMT_TEXTURE);

   if (!tile_mode) {
      /* different from format table when used as linear src */
      if (format == VK_FORMAT_R5G5B5A1_UNORM_PACK16)
         fmt.fmt = FMT6_1_5_5_5_UNORM, fmt.swap = WXYZ;
      if (format == VK_FORMAT_B5G5R5A1_UNORM_PACK16)
         fmt.fmt = FMT6_1_5_5_5_UNORM, fmt.swap = WZYX;
   } else {
      fmt.swap = WZYX;
   }

   return fmt;
}

static void
tu_physical_device_get_format_properties(
   struct tu_physical_device *physical_device,
   VkFormat format,
   VkFormatProperties *out_properties)
{
   VkFormatFeatureFlags linear = 0, optimal = 0, buffer = 0;
   const struct util_format_description *desc = vk_format_description(format);
   const struct tu_native_format native_fmt = tu6_get_native_format(format);
   if (!desc || !native_fmt.supported) {
      goto end;
   }

   buffer |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
   if (native_fmt.supported & FMT_VERTEX)
      buffer |= VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;

   if (native_fmt.supported & FMT_TEXTURE) {
      optimal |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                 VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT |
                 VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT |
                 VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;

      buffer |= VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;

      /* no blit src bit for YUYV/NV12/I420 formats */
      if (desc->layout != UTIL_FORMAT_LAYOUT_SUBSAMPLED &&
          desc->layout != UTIL_FORMAT_LAYOUT_PLANAR2 &&
          desc->layout != UTIL_FORMAT_LAYOUT_PLANAR3)
         optimal |= VK_FORMAT_FEATURE_BLIT_SRC_BIT;

      if (desc->layout != UTIL_FORMAT_LAYOUT_SUBSAMPLED)
         optimal |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT;

      if (physical_device->supported_extensions.EXT_filter_cubic)
         optimal |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT;
   }

   if (native_fmt.supported & FMT_COLOR) {
      assert(native_fmt.supported & FMT_TEXTURE);
      optimal |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                 VK_FORMAT_FEATURE_BLIT_DST_BIT;

      /* IBO's don't have a swap field at all, so swapped formats can't be
       * supported, even with linear images.
       *
       * TODO: See if setting the swap field from the tex descriptor works,
       * after we enable shaderStorageImageReadWithoutFormat and there are
       * tests for these formats.
       */
      if (native_fmt.swap == WZYX) {
         optimal |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
         buffer |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
      }

      /* TODO: The blob also exposes these for R16G16_UINT/R16G16_SINT, but we
       * don't have any tests for those.
       */
      if (format == VK_FORMAT_R32_UINT || format == VK_FORMAT_R32_SINT) {
         optimal |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;
         buffer |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
      }

      if (vk_format_is_float(format) ||
          vk_format_is_unorm(format) ||
          vk_format_is_snorm(format) ||
          vk_format_is_srgb(format)) {
         optimal |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
      }
   }

   /* For the most part, we can do anything with a linear image that we could
    * do with a tiled image. However, we can't support sysmem rendering with a
    * linear depth texture, because we don't know if there's a bit to control
    * the tiling of the depth buffer in BYPASS mode, and the blob also
    * disables linear depth rendering, so there's no way to discover it. We
    * also can't force GMEM mode, because there are other situations where we
    * have to use sysmem rendering. So follow the blob here, and only enable
    * DEPTH_STENCIL_ATTACHMENT_BIT for the optimal features.
    */
   linear = optimal;
   if (tu6_pipe2depth(format) != (enum a6xx_depth_format)~0)
      optimal |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

   /* no tiling for special UBWC formats
    * TODO: NV12 can be UBWC but has a special UBWC format for accessing the Y plane aspect
    * for 3plane, tiling/UBWC might be supported, but the blob doesn't use tiling
    */
   if (format == VK_FORMAT_G8B8G8R8_422_UNORM ||
       format == VK_FORMAT_B8G8R8G8_422_UNORM ||
       format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM ||
       format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM) {
      optimal = 0;
   }

   /* D32_SFLOAT_S8_UINT is tiled as two images, so no linear format
    * blob enables some linear features, but its not useful, so don't bother.
    */
   if (format == VK_FORMAT_D32_SFLOAT_S8_UINT)
      linear = 0;

end:
   out_properties->linearTilingFeatures = linear;
   out_properties->optimalTilingFeatures = optimal;
   out_properties->bufferFeatures = buffer;
}

void
tu_GetPhysicalDeviceFormatProperties2(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkFormatProperties2 *pFormatProperties)
{
   TU_FROM_HANDLE(tu_physical_device, physical_device, physicalDevice);

   tu_physical_device_get_format_properties(
      physical_device, format, &pFormatProperties->formatProperties);

   VkDrmFormatModifierPropertiesListEXT *list =
      vk_find_struct(pFormatProperties->pNext, DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT);
   if (list) {
      VK_OUTARRAY_MAKE(out, list->pDrmFormatModifierProperties,
                       &list->drmFormatModifierCount);

      if (pFormatProperties->formatProperties.linearTilingFeatures) {
         vk_outarray_append(&out, mod_props) {
            mod_props->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
            mod_props->drmFormatModifierPlaneCount = 1;
         }
      }

      /* note: ubwc_possible() argument values to be ignored except for format */
      if (pFormatProperties->formatProperties.optimalTilingFeatures &&
          ubwc_possible(format, VK_IMAGE_TYPE_2D, 0, false)) {
         vk_outarray_append(&out, mod_props) {
            mod_props->drmFormatModifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
            mod_props->drmFormatModifierPlaneCount = 1;
         }
      }
   }
}

static VkResult
tu_get_image_format_properties(
   struct tu_physical_device *physical_device,
   const VkPhysicalDeviceImageFormatInfo2 *info,
   VkImageFormatProperties *pImageFormatProperties,
   VkFormatFeatureFlags *p_feature_flags)
{
   VkFormatProperties format_props;
   VkFormatFeatureFlags format_feature_flags;
   VkExtent3D maxExtent;
   uint32_t maxMipLevels;
   uint32_t maxArraySize;
   VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;

   tu_physical_device_get_format_properties(physical_device, info->format,
                                            &format_props);

   switch (info->tiling) {
   case VK_IMAGE_TILING_LINEAR:
      format_feature_flags = format_props.linearTilingFeatures;
      break;

   case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT: {
      const VkPhysicalDeviceImageDrmFormatModifierInfoEXT *drm_info =
         vk_find_struct_const(info->pNext, PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT);

      switch (drm_info->drmFormatModifier) {
      case DRM_FORMAT_MOD_QCOM_COMPRESSED:
         /* falling back to linear/non-UBWC isn't possible with explicit modifier */

         /* formats which don't support tiling */
         if (!format_props.optimalTilingFeatures)
            return VK_ERROR_FORMAT_NOT_SUPPORTED;

         /* for mutable formats, its very unlikely to be possible to use UBWC */
         if (info->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT)
            return VK_ERROR_FORMAT_NOT_SUPPORTED;

         if (!ubwc_possible(info->format, info->type, info->usage, physical_device->limited_z24s8))
            return VK_ERROR_FORMAT_NOT_SUPPORTED;

         format_feature_flags = format_props.optimalTilingFeatures;
         break;
      case DRM_FORMAT_MOD_LINEAR:
         format_feature_flags = format_props.linearTilingFeatures;
         break;
      default:
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
      }
   } break;
   case VK_IMAGE_TILING_OPTIMAL:
      format_feature_flags = format_props.optimalTilingFeatures;
      break;
   default:
      unreachable("bad VkPhysicalDeviceImageFormatInfo2");
   }

   if (format_feature_flags == 0)
      goto unsupported;

   if (info->type != VK_IMAGE_TYPE_2D &&
       vk_format_is_depth_or_stencil(info->format))
      goto unsupported;

   switch (info->type) {
   default:
      unreachable("bad vkimage type\n");
   case VK_IMAGE_TYPE_1D:
      maxExtent.width = 16384;
      maxExtent.height = 1;
      maxExtent.depth = 1;
      maxMipLevels = 15; /* log2(maxWidth) + 1 */
      maxArraySize = 2048;
      break;
   case VK_IMAGE_TYPE_2D:
      maxExtent.width = 16384;
      maxExtent.height = 16384;
      maxExtent.depth = 1;
      maxMipLevels = 15; /* log2(maxWidth) + 1 */
      maxArraySize = 2048;
      break;
   case VK_IMAGE_TYPE_3D:
      maxExtent.width = 2048;
      maxExtent.height = 2048;
      maxExtent.depth = 2048;
      maxMipLevels = 12; /* log2(maxWidth) + 1 */
      maxArraySize = 1;
      break;
   }

   if (info->tiling == VK_IMAGE_TILING_OPTIMAL &&
       info->type == VK_IMAGE_TYPE_2D &&
       (format_feature_flags &
        (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
         VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       !(info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       !(info->usage & VK_IMAGE_USAGE_STORAGE_BIT)) {
      sampleCounts |= VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT;
      /* note: most operations support 8 samples (GMEM render/resolve do at least)
       * but some do not (which ones?), just disable 8 samples completely,
       * (no 8x msaa matches the blob driver behavior)
       */
   }

   if (info->usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_STORAGE_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      if (!(format_feature_flags &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
         goto unsupported;
      }
   }

   *pImageFormatProperties = (VkImageFormatProperties) {
      .maxExtent = maxExtent,
      .maxMipLevels = maxMipLevels,
      .maxArrayLayers = maxArraySize,
      .sampleCounts = sampleCounts,

      /* FINISHME: Accurately calculate
       * VkImageFormatProperties::maxResourceSize.
       */
      .maxResourceSize = UINT32_MAX,
   };

   if (p_feature_flags)
      *p_feature_flags = format_feature_flags;

   return VK_SUCCESS;
unsupported:
   *pImageFormatProperties = (VkImageFormatProperties) {
      .maxExtent = { 0, 0, 0 },
      .maxMipLevels = 0,
      .maxArrayLayers = 0,
      .sampleCounts = 0,
      .maxResourceSize = 0,
   };

   return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

static VkResult
tu_get_external_image_format_properties(
   const struct tu_physical_device *physical_device,
   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
   VkExternalMemoryHandleTypeFlagBits handleType,
   VkExternalMemoryProperties *external_properties)
{
   VkExternalMemoryFeatureFlagBits flags = 0;
   VkExternalMemoryHandleTypeFlags export_flags = 0;
   VkExternalMemoryHandleTypeFlags compat_flags = 0;

   /* From the Vulkan 1.1.98 spec:
    *
    *    If handleType is not compatible with the format, type, tiling,
    *    usage, and flags specified in VkPhysicalDeviceImageFormatInfo2,
    *    then vkGetPhysicalDeviceImageFormatProperties2 returns
    *    VK_ERROR_FORMAT_NOT_SUPPORTED.
    */

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      switch (pImageFormatInfo->type) {
      case VK_IMAGE_TYPE_2D:
         flags = VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT |
                 VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                 VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
         compat_flags = export_flags =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
         break;
      default:
         return vk_errorf(physical_device->instance, VK_ERROR_FORMAT_NOT_SUPPORTED,
                          "VkExternalMemoryTypeFlagBits(0x%x) unsupported for VkImageType(%d)",
                          handleType, pImageFormatInfo->type);
      }
      break;
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT:
      flags = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
      break;
   default:
      return vk_errorf(physical_device->instance, VK_ERROR_FORMAT_NOT_SUPPORTED,
                       "VkExternalMemoryTypeFlagBits(0x%x) unsupported",
                       handleType);
   }

   *external_properties = (VkExternalMemoryProperties) {
      .externalMemoryFeatures = flags,
      .exportFromImportedHandleTypes = export_flags,
      .compatibleHandleTypes = compat_flags,
   };

   return VK_SUCCESS;
}

VkResult
tu_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceImageFormatInfo2 *base_info,
   VkImageFormatProperties2 *base_props)
{
   TU_FROM_HANDLE(tu_physical_device, physical_device, physicalDevice);
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   const VkPhysicalDeviceImageViewImageFormatInfoEXT *image_view_info = NULL;
   VkExternalImageFormatProperties *external_props = NULL;
   VkFilterCubicImageViewImageFormatPropertiesEXT *cubic_props = NULL;
   VkFormatFeatureFlags format_feature_flags;
   VkSamplerYcbcrConversionImageFormatProperties *ycbcr_props = NULL;
   VkResult result;

   result = tu_get_image_format_properties(physical_device,
      base_info, &base_props->imageFormatProperties, &format_feature_flags);
   if (result != VK_SUCCESS)
      return result;

   /* Extract input structs */
   vk_foreach_struct_const(s, base_info->pNext)
   {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const void *) s;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_IMAGE_FORMAT_INFO_EXT:
         image_view_info = (const void *) s;
         break;
      default:
         break;
      }
   }

   /* Extract output structs */
   vk_foreach_struct(s, base_props->pNext)
   {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         external_props = (void *) s;
         break;
      case VK_STRUCTURE_TYPE_FILTER_CUBIC_IMAGE_VIEW_IMAGE_FORMAT_PROPERTIES_EXT:
         cubic_props = (void *) s;
         break;
      case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES:
         ycbcr_props = (void *) s;
         break;
      default:
         break;
      }
   }

   /* From the Vulkan 1.0.42 spec:
    *
    *    If handleType is 0, vkGetPhysicalDeviceImageFormatProperties2 will
    *    behave as if VkPhysicalDeviceExternalImageFormatInfo was not
    *    present and VkExternalImageFormatProperties will be ignored.
    */
   if (external_info && external_info->handleType != 0) {
      result = tu_get_external_image_format_properties(
         physical_device, base_info, external_info->handleType,
         &external_props->externalMemoryProperties);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (cubic_props) {
      /* note: blob only allows cubic filtering for 2D and 2D array views
       * its likely we can enable it for 1D and CUBE, needs testing however
       */
      if ((image_view_info->imageViewType == VK_IMAGE_VIEW_TYPE_2D ||
           image_view_info->imageViewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY) &&
          (format_feature_flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT)) {
         cubic_props->filterCubic = true;
         cubic_props->filterCubicMinmax = true;
      } else {
         cubic_props->filterCubic = false;
         cubic_props->filterCubicMinmax = false;
      }
   }

   if (ycbcr_props)
      ycbcr_props->combinedImageSamplerDescriptorCount = 1;

   return VK_SUCCESS;

fail:
   if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
      /* From the Vulkan 1.0.42 spec:
       *
       *    If the combination of parameters to
       *    vkGetPhysicalDeviceImageFormatProperties2 is not supported by
       *    the implementation for use in vkCreateImage, then all members of
       *    imageFormatProperties will be filled with zero.
       */
      base_props->imageFormatProperties = (VkImageFormatProperties) {};
   }

   return result;
}

void
tu_GetPhysicalDeviceSparseImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
   uint32_t *pPropertyCount,
   VkSparseImageFormatProperties2 *pProperties)
{
   /* Sparse images are not yet supported. */
   *pPropertyCount = 0;
}

void
tu_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   VkExternalMemoryFeatureFlagBits flags = 0;
   VkExternalMemoryHandleTypeFlags export_flags = 0;
   VkExternalMemoryHandleTypeFlags compat_flags = 0;
   switch (pExternalBufferInfo->handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      flags = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
              VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = export_flags =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      break;
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT:
      flags = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
      break;
   default:
      break;
   }
   pExternalBufferProperties->externalMemoryProperties =
      (VkExternalMemoryProperties) {
         .externalMemoryFeatures = flags,
         .exportFromImportedHandleTypes = export_flags,
         .compatibleHandleTypes = compat_flags,
      };
}
