
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

#include "registers/adreno_common.xml.h"
#include "registers/a6xx.xml.h"

#include "util/format_r11g11b10f.h"
#include "util/format_srgb.h"
#include "util/u_half.h"
#include "vk_format.h"
#include "vk_util.h"

/**
 * Declare a format table.  A format table is an array of tu_native_format.
 * It can map a consecutive range of VkFormat to the corresponding
 * tu_native_format.
 *
 * TU_FORMAT_TABLE_FIRST and TU_FORMAT_TABLE_LAST must already be defined and
 * have the values of the first and last VkFormat of the array respectively.
 */
#define TU_FORMAT_TABLE(var)                                                 \
   static const VkFormat var##_first = TU_FORMAT_TABLE_FIRST;                \
   static const VkFormat var##_last = TU_FORMAT_TABLE_LAST;                  \
   static const struct tu_native_format var[TU_FORMAT_TABLE_LAST - TU_FORMAT_TABLE_FIRST + 1]
#undef TU_FORMAT_TABLE_FIRST
#undef TU_FORMAT_TABLE_LAST

#define VFMT6_x -1
#define TFMT6_x -1
#define RB6_x -1

#define TU6_FMT(vkfmt, vtxfmt, texfmt, rbfmt, swapfmt, valid)                \
   [VK_FORMAT_##vkfmt - TU_FORMAT_TABLE_FIRST] = {                           \
      .vtx = VFMT6_##vtxfmt,                                                 \
      .tex = TFMT6_##texfmt,                                                 \
      .rb = RB6_##rbfmt,                                                     \
      .swap = swapfmt,                                                       \
      .present = valid,                                                      \
   }

/**
 * fmt/alias/swap are derived from VkFormat mechanically (and might not even
 * exist).  It is the macro of choice that decides whether a VkFormat is
 * supported and how.
 */
#define TU6_VTC(vk, fmt, alias, swap) TU6_FMT(vk, fmt, fmt, alias, swap, true)
#define TU6_xTC(vk, fmt, alias, swap) TU6_FMT(vk, x, fmt, alias, swap, true)
#define TU6_VTx(vk, fmt, alias, swap) TU6_FMT(vk, fmt, fmt, x, swap, true)
#define TU6_Vxx(vk, fmt, alias, swap) TU6_FMT(vk, fmt, x, x, swap, true)
#define TU6_xTx(vk, fmt, alias, swap) TU6_FMT(vk, x, fmt, x, swap, true)
#define TU6_xxx(vk, fmt, alias, swap) TU6_FMT(vk, x, x, x, WZYX, false)

#define TU_FORMAT_TABLE_FIRST VK_FORMAT_UNDEFINED
#define TU_FORMAT_TABLE_LAST VK_FORMAT_ASTC_12x12_SRGB_BLOCK
TU_FORMAT_TABLE(tu6_format_table0) = {
   TU6_xxx(UNDEFINED,                  x,                 x,                  x),    /* 0 */

   /* 8-bit packed */
   TU6_xxx(R4G4_UNORM_PACK8,           4_4_UNORM,         R4G4_UNORM,         WZXY), /* 1 */

   /* 16-bit packed */
   TU6_xTC(R4G4B4A4_UNORM_PACK16,      4_4_4_4_UNORM,     R4G4B4A4_UNORM,     XYZW), /* 2 */
   TU6_xTC(B4G4R4A4_UNORM_PACK16,      4_4_4_4_UNORM,     R4G4B4A4_UNORM,     ZYXW), /* 3 */
   TU6_xTC(R5G6B5_UNORM_PACK16,        5_6_5_UNORM,       R5G6B5_UNORM,       WXYZ), /* 4 */
   TU6_xTC(B5G6R5_UNORM_PACK16,        5_6_5_UNORM,       R5G6B5_UNORM,       WXYZ), /* 5 */
   TU6_xxx(R5G5B5A1_UNORM_PACK16,      1_5_5_5_UNORM,     A1R5G5B5_UNORM,     XYZW), /* 6 */
   TU6_xxx(B5G5R5A1_UNORM_PACK16,      1_5_5_5_UNORM,     A1R5G5B5_UNORM,     XYZW), /* 7 */
   TU6_xTC(A1R5G5B5_UNORM_PACK16,      5_5_5_1_UNORM,     R5G5B5A1_UNORM,     WXYZ), /* 8 */

   /* 8-bit R */
   TU6_VTC(R8_UNORM,                   8_UNORM,           R8_UNORM,           WZYX), /* 9 */
   TU6_VTC(R8_SNORM,                   8_SNORM,           R8_SNORM,           WZYX), /* 10 */
   TU6_Vxx(R8_USCALED,                 8_UINT,            R8_UINT,            WZYX), /* 11 */
   TU6_Vxx(R8_SSCALED,                 8_SINT,            R8_SINT,            WZYX), /* 12 */
   TU6_VTC(R8_UINT,                    8_UINT,            R8_UINT,            WZYX), /* 13 */
   TU6_VTC(R8_SINT,                    8_SINT,            R8_SINT,            WZYX), /* 14 */
   TU6_xTC(R8_SRGB,                    8_UNORM,           R8_UNORM,           WZYX), /* 15 */

   /* 16-bit RG */
   TU6_VTC(R8G8_UNORM,                 8_8_UNORM,         R8G8_UNORM,         WZYX), /* 16 */
   TU6_VTC(R8G8_SNORM,                 8_8_SNORM,         R8G8_SNORM,         WZYX), /* 17 */
   TU6_Vxx(R8G8_USCALED,               8_8_UINT,          R8G8_UINT,          WZYX), /* 18 */
   TU6_Vxx(R8G8_SSCALED,               8_8_SINT,          R8G8_SINT,          WZYX), /* 19 */
   TU6_VTC(R8G8_UINT,                  8_8_UINT,          R8G8_UINT,          WZYX), /* 20 */
   TU6_VTC(R8G8_SINT,                  8_8_SINT,          R8G8_SINT,          WZYX), /* 21 */
   TU6_xTC(R8G8_SRGB,                  8_8_UNORM,         R8G8_UNORM,         WZYX), /* 22 */

   /* 24-bit RGB */
   TU6_Vxx(R8G8B8_UNORM,               8_8_8_UNORM,       R8G8B8_UNORM,       WZYX), /* 23 */
   TU6_Vxx(R8G8B8_SNORM,               8_8_8_SNORM,       R8G8B8_SNORM,       WZYX), /* 24 */
   TU6_Vxx(R8G8B8_USCALED,             8_8_8_UINT,        R8G8B8_UINT,        WZYX), /* 25 */
   TU6_Vxx(R8G8B8_SSCALED,             8_8_8_SINT,        R8G8B8_SINT,        WZYX), /* 26 */
   TU6_Vxx(R8G8B8_UINT,                8_8_8_UINT,        R8G8B8_UINT,        WZYX), /* 27 */
   TU6_Vxx(R8G8B8_SINT,                8_8_8_SINT,        R8G8B8_SINT,        WZYX), /* 28 */
   TU6_xxx(R8G8B8_SRGB,                8_8_8_UNORM,       R8G8B8_UNORM,       WZYX), /* 29 */

   /* 24-bit BGR */
   TU6_Vxx(B8G8R8_UNORM,               8_8_8_UNORM,       R8G8B8_UNORM,       WXYZ), /* 30 */
   TU6_Vxx(B8G8R8_SNORM,               8_8_8_SNORM,       R8G8B8_SNORM,       WXYZ), /* 31 */
   TU6_Vxx(B8G8R8_USCALED,             8_8_8_UINT,        R8G8B8_UINT,        WXYZ), /* 32 */
   TU6_Vxx(B8G8R8_SSCALED,             8_8_8_SINT,        R8G8B8_SINT,        WXYZ), /* 33 */
   TU6_Vxx(B8G8R8_UINT,                8_8_8_UINT,        R8G8B8_UINT,        WXYZ), /* 34 */
   TU6_Vxx(B8G8R8_SINT,                8_8_8_SINT,        R8G8B8_SINT,        WXYZ), /* 35 */
   TU6_xxx(B8G8R8_SRGB,                8_8_8_UNORM,       R8G8B8_UNORM,       WXYZ), /* 36 */

   /* 32-bit RGBA */
   TU6_VTC(R8G8B8A8_UNORM,             8_8_8_8_UNORM,     R8G8B8A8_UNORM,     WZYX), /* 37 */
   TU6_VTC(R8G8B8A8_SNORM,             8_8_8_8_SNORM,     R8G8B8A8_SNORM,     WZYX), /* 38 */
   TU6_Vxx(R8G8B8A8_USCALED,           8_8_8_8_UINT,      R8G8B8A8_UINT,      WZYX), /* 39 */
   TU6_Vxx(R8G8B8A8_SSCALED,           8_8_8_8_SINT,      R8G8B8A8_SINT,      WZYX), /* 40 */
   TU6_VTC(R8G8B8A8_UINT,              8_8_8_8_UINT,      R8G8B8A8_UINT,      WZYX), /* 41 */
   TU6_VTC(R8G8B8A8_SINT,              8_8_8_8_SINT,      R8G8B8A8_SINT,      WZYX), /* 42 */
   TU6_xTC(R8G8B8A8_SRGB,              8_8_8_8_UNORM,     R8G8B8A8_UNORM,     WZYX), /* 43 */

   /* 32-bit BGRA */
   TU6_VTC(B8G8R8A8_UNORM,             8_8_8_8_UNORM,     R8G8B8A8_UNORM,     WXYZ), /* 44 */
   TU6_VTC(B8G8R8A8_SNORM,             8_8_8_8_SNORM,     R8G8B8A8_SNORM,     WXYZ), /* 45 */
   TU6_Vxx(B8G8R8A8_USCALED,           8_8_8_8_UINT,      R8G8B8A8_UINT,      WXYZ), /* 46 */
   TU6_Vxx(B8G8R8A8_SSCALED,           8_8_8_8_SINT,      R8G8B8A8_SINT,      WXYZ), /* 47 */
   TU6_VTC(B8G8R8A8_UINT,              8_8_8_8_UINT,      R8G8B8A8_UINT,      WXYZ), /* 48 */
   TU6_VTC(B8G8R8A8_SINT,              8_8_8_8_SINT,      R8G8B8A8_SINT,      WXYZ), /* 49 */
   TU6_xTC(B8G8R8A8_SRGB,              8_8_8_8_UNORM,     R8G8B8A8_UNORM,     WXYZ), /* 50 */

   /* 32-bit packed */
   TU6_VTC(A8B8G8R8_UNORM_PACK32,      8_8_8_8_UNORM,     R8G8B8A8_UNORM,     WZYX), /* 51 */
   TU6_VTC(A8B8G8R8_SNORM_PACK32,      8_8_8_8_SNORM,     R8G8B8A8_SNORM,     WZYX), /* 52 */
   TU6_Vxx(A8B8G8R8_USCALED_PACK32,    8_8_8_8_UINT,      R8G8B8A8_UINT,      WZYX), /* 53 */
   TU6_Vxx(A8B8G8R8_SSCALED_PACK32,    8_8_8_8_SINT,      R8G8B8A8_SINT,      WZYX), /* 54 */
   TU6_VTC(A8B8G8R8_UINT_PACK32,       8_8_8_8_UINT,      R8G8B8A8_UINT,      WZYX), /* 55 */
   TU6_VTC(A8B8G8R8_SINT_PACK32,       8_8_8_8_SINT,      R8G8B8A8_SINT,      WZYX), /* 56 */
   TU6_xTC(A8B8G8R8_SRGB_PACK32,       8_8_8_8_UNORM,     R8G8B8A8_UNORM,     WZYX), /* 57 */
   TU6_VTC(A2R10G10B10_UNORM_PACK32,   10_10_10_2_UNORM,  R10G10B10A2_UNORM,  WXYZ), /* 58 */
   TU6_Vxx(A2R10G10B10_SNORM_PACK32,   10_10_10_2_SNORM,  R10G10B10A2_SNORM,  WXYZ), /* 59 */
   TU6_Vxx(A2R10G10B10_USCALED_PACK32, 10_10_10_2_UINT,   R10G10B10A2_UINT,   WXYZ), /* 60 */
   TU6_Vxx(A2R10G10B10_SSCALED_PACK32, 10_10_10_2_SINT,   R10G10B10A2_SINT,   WXYZ), /* 61 */
   TU6_VTC(A2R10G10B10_UINT_PACK32,    10_10_10_2_UINT,   R10G10B10A2_UINT,   WXYZ), /* 62 */
   TU6_Vxx(A2R10G10B10_SINT_PACK32,    10_10_10_2_SINT,   R10G10B10A2_SINT,   WXYZ), /* 63 */
   TU6_VTC(A2B10G10R10_UNORM_PACK32,   10_10_10_2_UNORM,  R10G10B10A2_UNORM,  WZYX), /* 64 */
   TU6_Vxx(A2B10G10R10_SNORM_PACK32,   10_10_10_2_SNORM,  R10G10B10A2_SNORM,  WZYX), /* 65 */
   TU6_Vxx(A2B10G10R10_USCALED_PACK32, 10_10_10_2_UINT,   R10G10B10A2_UINT,   WZYX), /* 66 */
   TU6_Vxx(A2B10G10R10_SSCALED_PACK32, 10_10_10_2_SINT,   R10G10B10A2_SINT,   WZYX), /* 67 */
   TU6_VTC(A2B10G10R10_UINT_PACK32,    10_10_10_2_UINT,   R10G10B10A2_UINT,   WZYX), /* 68 */
   TU6_Vxx(A2B10G10R10_SINT_PACK32,    10_10_10_2_SINT,   R10G10B10A2_SINT,   WZYX), /* 69 */

   /* 16-bit R */
   TU6_VTC(R16_UNORM,                  16_UNORM,          R16_UNORM,          WZYX), /* 70 */
   TU6_VTC(R16_SNORM,                  16_SNORM,          R16_SNORM,          WZYX), /* 71 */
   TU6_Vxx(R16_USCALED,                16_UINT,           R16_UINT,           WZYX), /* 72 */
   TU6_Vxx(R16_SSCALED,                16_SINT,           R16_SINT,           WZYX), /* 73 */
   TU6_VTC(R16_UINT,                   16_UINT,           R16_UINT,           WZYX), /* 74 */
   TU6_VTC(R16_SINT,                   16_SINT,           R16_SINT,           WZYX), /* 75 */
   TU6_VTC(R16_SFLOAT,                 16_FLOAT,          R16_FLOAT,          WZYX), /* 76 */

   /* 32-bit RG */
   TU6_VTC(R16G16_UNORM,               16_16_UNORM,       R16G16_UNORM,       WZYX), /* 77 */
   TU6_VTC(R16G16_SNORM,               16_16_SNORM,       R16G16_SNORM,       WZYX), /* 78 */
   TU6_VTx(R16G16_USCALED,             16_16_UINT,        R16G16_UINT,        WZYX), /* 79 */
   TU6_VTx(R16G16_SSCALED,             16_16_SINT,        R16G16_SINT,        WZYX), /* 80 */
   TU6_VTC(R16G16_UINT,                16_16_UINT,        R16G16_UINT,        WZYX), /* 81 */
   TU6_VTC(R16G16_SINT,                16_16_SINT,        R16G16_SINT,        WZYX), /* 82 */
   TU6_VTC(R16G16_SFLOAT,              16_16_FLOAT,       R16G16_FLOAT,       WZYX), /* 83 */

   /* 48-bit RGB */
   TU6_Vxx(R16G16B16_UNORM,            16_16_16_UNORM,    R16G16B16_UNORM,    WZYX), /* 84 */
   TU6_Vxx(R16G16B16_SNORM,            16_16_16_SNORM,    R16G16B16_SNORM,    WZYX), /* 85 */
   TU6_Vxx(R16G16B16_USCALED,          16_16_16_UINT,     R16G16B16_UINT,     WZYX), /* 86 */
   TU6_Vxx(R16G16B16_SSCALED,          16_16_16_SINT,     R16G16B16_SINT,     WZYX), /* 87 */
   TU6_Vxx(R16G16B16_UINT,             16_16_16_UINT,     R16G16B16_UINT,     WZYX), /* 88 */
   TU6_Vxx(R16G16B16_SINT,             16_16_16_SINT,     R16G16B16_SINT,     WZYX), /* 89 */
   TU6_Vxx(R16G16B16_SFLOAT,           16_16_16_FLOAT,    R16G16B16_FLOAT,    WZYX), /* 90 */

   /* 64-bit RGBA */
   TU6_VTC(R16G16B16A16_UNORM,         16_16_16_16_UNORM, R16G16B16A16_UNORM, WZYX), /* 91 */
   TU6_VTC(R16G16B16A16_SNORM,         16_16_16_16_SNORM, R16G16B16A16_SNORM, WZYX), /* 92 */
   TU6_VTx(R16G16B16A16_USCALED,       16_16_16_16_UINT,  R16G16B16A16_UINT,  WZYX), /* 93 */
   TU6_VTx(R16G16B16A16_SSCALED,       16_16_16_16_SINT,  R16G16B16A16_SINT,  WZYX), /* 94 */
   TU6_VTC(R16G16B16A16_UINT,          16_16_16_16_UINT,  R16G16B16A16_UINT,  WZYX), /* 95 */
   TU6_VTC(R16G16B16A16_SINT,          16_16_16_16_SINT,  R16G16B16A16_SINT,  WZYX), /* 96 */
   TU6_VTC(R16G16B16A16_SFLOAT,        16_16_16_16_FLOAT, R16G16B16A16_FLOAT, WZYX), /* 97 */

   /* 32-bit R */
   TU6_VTC(R32_UINT,                   32_UINT,           R32_UINT,           WZYX), /* 98 */
   TU6_VTC(R32_SINT,                   32_SINT,           R32_SINT,           WZYX), /* 99 */
   TU6_VTC(R32_SFLOAT,                 32_FLOAT,          R32_FLOAT,          WZYX), /* 100 */

   /* 64-bit RG */
   TU6_VTC(R32G32_UINT,                32_32_UINT,        R32G32_UINT,        WZYX), /* 101 */
   TU6_VTC(R32G32_SINT,                32_32_SINT,        R32G32_SINT,        WZYX), /* 102 */
   TU6_VTC(R32G32_SFLOAT,              32_32_FLOAT,       R32G32_FLOAT,       WZYX), /* 103 */

   /* 96-bit RGB */
   TU6_VTx(R32G32B32_UINT,             32_32_32_UINT,     R32G32B32_UINT,     WZYX), /* 104 */
   TU6_VTx(R32G32B32_SINT,             32_32_32_SINT,     R32G32B32_SINT,     WZYX), /* 105 */
   TU6_VTx(R32G32B32_SFLOAT,           32_32_32_FLOAT,    R32G32B32_FLOAT,    WZYX), /* 106 */

   /* 128-bit RGBA */
   TU6_VTC(R32G32B32A32_UINT,          32_32_32_32_UINT,  R32G32B32A32_UINT,  WZYX), /* 107 */
   TU6_VTC(R32G32B32A32_SINT,          32_32_32_32_SINT,  R32G32B32A32_SINT,  WZYX), /* 108 */
   TU6_VTC(R32G32B32A32_SFLOAT,        32_32_32_32_FLOAT, R32G32B32A32_FLOAT, WZYX), /* 109 */

   /* 64-bit R */
   TU6_xxx(R64_UINT,                   64_UINT,           R64_UINT,           WZYX), /* 110 */
   TU6_xxx(R64_SINT,                   64_SINT,           R64_SINT,           WZYX), /* 111 */
   TU6_xxx(R64_SFLOAT,                 64_FLOAT,          R64_FLOAT,          WZYX), /* 112 */

   /* 128-bit RG */
   TU6_xxx(R64G64_UINT,                64_64_UINT,        R64G64_UINT,        WZYX), /* 113 */
   TU6_xxx(R64G64_SINT,                64_64_SINT,        R64G64_SINT,        WZYX), /* 114 */
   TU6_xxx(R64G64_SFLOAT,              64_64_FLOAT,       R64G64_FLOAT,       WZYX), /* 115 */

   /* 192-bit RGB */
   TU6_xxx(R64G64B64_UINT,             64_64_64_UINT,     R64G64B64_UINT,     WZYX), /* 116 */
   TU6_xxx(R64G64B64_SINT,             64_64_64_SINT,     R64G64B64_SINT,     WZYX), /* 117 */
   TU6_xxx(R64G64B64_SFLOAT,           64_64_64_FLOAT,    R64G64B64_FLOAT,    WZYX), /* 118 */

   /* 256-bit RGBA */
   TU6_xxx(R64G64B64A64_UINT,          64_64_64_64_UINT,  R64G64B64A64_UINT,  WZYX), /* 119 */
   TU6_xxx(R64G64B64A64_SINT,          64_64_64_64_SINT,  R64G64B64A64_SINT,  WZYX), /* 120 */
   TU6_xxx(R64G64B64A64_SFLOAT,        64_64_64_64_FLOAT, R64G64B64A64_FLOAT, WZYX), /* 121 */

   /* 32-bit packed float */
   TU6_VTC(B10G11R11_UFLOAT_PACK32,    11_11_10_FLOAT,    R11G11B10_FLOAT,    WZYX), /* 122 */
   TU6_xTx(E5B9G9R9_UFLOAT_PACK32,     9_9_9_E5_FLOAT,    R9G9B9E5_FLOAT,     WZYX), /* 123 */

   /* depth/stencil */
   TU6_xTC(D16_UNORM,                  16_UNORM,          R16_UNORM,          WZYX), /* 124 */
   TU6_xTC(X8_D24_UNORM_PACK32,        X8Z24_UNORM,       X8Z24_UNORM,        WZYX), /* 125 */
   TU6_xTC(D32_SFLOAT,                 32_FLOAT,          R32_FLOAT,          WZYX), /* 126 */
   TU6_xTC(S8_UINT,                    8_UINT,            R8_UNORM,           WZYX), /* 127 */
   TU6_xxx(D16_UNORM_S8_UINT,          X8Z16_UNORM,       X8Z16_UNORM,        WZYX), /* 128 */
   TU6_xTC(D24_UNORM_S8_UINT,          X8Z24_UNORM,       X8Z24_UNORM,        WZYX), /* 129 */
   TU6_xTC(D32_SFLOAT_S8_UINT,         32_FLOAT,          R32_FLOAT,          WZYX), /* 130 */

   /* compressed */
   TU6_xTx(BC1_RGB_UNORM_BLOCK,        DXT1,              DXT1,               WZYX), /* 131 */
   TU6_xTx(BC1_RGB_SRGB_BLOCK,         DXT1,              DXT1,               WZYX), /* 132 */
   TU6_xTx(BC1_RGBA_UNORM_BLOCK,       DXT1,              DXT1,               WZYX), /* 133 */
   TU6_xTx(BC1_RGBA_SRGB_BLOCK,        DXT1,              DXT1,               WZYX), /* 134 */
   TU6_xTx(BC2_UNORM_BLOCK,            DXT3,              DXT3,               WZYX), /* 135 */
   TU6_xTx(BC2_SRGB_BLOCK,             DXT3,              DXT3,               WZYX), /* 136 */
   TU6_xTx(BC3_UNORM_BLOCK,            DXT5,              DXT5,               WZYX), /* 137 */
   TU6_xTx(BC3_SRGB_BLOCK,             DXT5,              DXT5,               WZYX), /* 138 */
   TU6_xTx(BC4_UNORM_BLOCK,            RGTC1_UNORM,       RGTC1_UNORM,        WZYX), /* 139 */
   TU6_xTx(BC4_SNORM_BLOCK,            RGTC1_SNORM,       RGTC1_SNORM,        WZYX), /* 140 */
   TU6_xTx(BC5_UNORM_BLOCK,            RGTC2_UNORM,       RGTC2_UNORM,        WZYX), /* 141 */
   TU6_xTx(BC5_SNORM_BLOCK,            RGTC2_SNORM,       RGTC2_SNORM,        WZYX), /* 142 */
   TU6_xTx(BC6H_UFLOAT_BLOCK,          BPTC_UFLOAT,       BPTC_UFLOAT,        WZYX), /* 143 */
   TU6_xTx(BC6H_SFLOAT_BLOCK,          BPTC_FLOAT,        BPTC_FLOAT,         WZYX), /* 144 */
   TU6_xTx(BC7_UNORM_BLOCK,            BPTC,              BPTC,               WZYX), /* 145 */
   TU6_xTx(BC7_SRGB_BLOCK,             BPTC,              BPTC,               WZYX), /* 146 */
   TU6_xTx(ETC2_R8G8B8_UNORM_BLOCK,    ETC2_RGB8,         ETC2_RGB8,          WZYX), /* 147 */
   TU6_xTx(ETC2_R8G8B8_SRGB_BLOCK,     ETC2_RGB8,         ETC2_RGB8,          WZYX), /* 148 */
   TU6_xTx(ETC2_R8G8B8A1_UNORM_BLOCK,  ETC2_RGB8A1,       ETC2_RGB8A1,        WZYX), /* 149 */
   TU6_xTx(ETC2_R8G8B8A1_SRGB_BLOCK,   ETC2_RGB8A1,       ETC2_RGB8A1,        WZYX), /* 150 */
   TU6_xTx(ETC2_R8G8B8A8_UNORM_BLOCK,  ETC2_RGBA8,        ETC2_RGBA8,         WZYX), /* 151 */
   TU6_xTx(ETC2_R8G8B8A8_SRGB_BLOCK,   ETC2_RGBA8,        ETC2_RGBA8,         WZYX), /* 152 */
   TU6_xTx(EAC_R11_UNORM_BLOCK,        ETC2_R11_UNORM,    ETC2_R11_UNORM,     WZYX), /* 153 */
   TU6_xTx(EAC_R11_SNORM_BLOCK,        ETC2_R11_SNORM,    ETC2_R11_SNORM,     WZYX), /* 154 */
   TU6_xTx(EAC_R11G11_UNORM_BLOCK,     ETC2_RG11_UNORM,   ETC2_RG11_UNORM,    WZYX), /* 155 */
   TU6_xTx(EAC_R11G11_SNORM_BLOCK,     ETC2_RG11_SNORM,   ETC2_RG11_SNORM,    WZYX), /* 156 */
   TU6_xTx(ASTC_4x4_UNORM_BLOCK,       ASTC_4x4,          ASTC_4x4,           WZYX), /* 157 */
   TU6_xTx(ASTC_4x4_SRGB_BLOCK,        ASTC_4x4,          ASTC_4x4,           WZYX), /* 158 */
   TU6_xTx(ASTC_5x4_UNORM_BLOCK,       ASTC_5x4,          ASTC_5x4,           WZYX), /* 159 */
   TU6_xTx(ASTC_5x4_SRGB_BLOCK,        ASTC_5x4,          ASTC_5x4,           WZYX), /* 160 */
   TU6_xTx(ASTC_5x5_UNORM_BLOCK,       ASTC_5x5,          ASTC_5x5,           WZYX), /* 161 */
   TU6_xTx(ASTC_5x5_SRGB_BLOCK,        ASTC_5x5,          ASTC_5x5,           WZYX), /* 162 */
   TU6_xTx(ASTC_6x5_UNORM_BLOCK,       ASTC_6x5,          ASTC_6x5,           WZYX), /* 163 */
   TU6_xTx(ASTC_6x5_SRGB_BLOCK,        ASTC_6x5,          ASTC_6x5,           WZYX), /* 164 */
   TU6_xTx(ASTC_6x6_UNORM_BLOCK,       ASTC_6x6,          ASTC_6x6,           WZYX), /* 165 */
   TU6_xTx(ASTC_6x6_SRGB_BLOCK,        ASTC_6x6,          ASTC_6x6,           WZYX), /* 166 */
   TU6_xTx(ASTC_8x5_UNORM_BLOCK,       ASTC_8x5,          ASTC_8x5,           WZYX), /* 167 */
   TU6_xTx(ASTC_8x5_SRGB_BLOCK,        ASTC_8x5,          ASTC_8x5,           WZYX), /* 168 */
   TU6_xTx(ASTC_8x6_UNORM_BLOCK,       ASTC_8x6,          ASTC_8x6,           WZYX), /* 169 */
   TU6_xTx(ASTC_8x6_SRGB_BLOCK,        ASTC_8x6,          ASTC_8x6,           WZYX), /* 170 */
   TU6_xTx(ASTC_8x8_UNORM_BLOCK,       ASTC_8x8,          ASTC_8x8,           WZYX), /* 171 */
   TU6_xTx(ASTC_8x8_SRGB_BLOCK,        ASTC_8x8,          ASTC_8x8,           WZYX), /* 172 */
   TU6_xTx(ASTC_10x5_UNORM_BLOCK,      ASTC_10x5,         ASTC_10x5,          WZYX), /* 173 */
   TU6_xTx(ASTC_10x5_SRGB_BLOCK,       ASTC_10x5,         ASTC_10x5,          WZYX), /* 174 */
   TU6_xTx(ASTC_10x6_UNORM_BLOCK,      ASTC_10x6,         ASTC_10x6,          WZYX), /* 175 */
   TU6_xTx(ASTC_10x6_SRGB_BLOCK,       ASTC_10x6,         ASTC_10x6,          WZYX), /* 176 */
   TU6_xTx(ASTC_10x8_UNORM_BLOCK,      ASTC_10x8,         ASTC_10x8,          WZYX), /* 177 */
   TU6_xTx(ASTC_10x8_SRGB_BLOCK,       ASTC_10x8,         ASTC_10x8,          WZYX), /* 178 */
   TU6_xTx(ASTC_10x10_UNORM_BLOCK,     ASTC_10x10,        ASTC_10x10,         WZYX), /* 179 */
   TU6_xTx(ASTC_10x10_SRGB_BLOCK,      ASTC_10x10,        ASTC_10x10,         WZYX), /* 180 */
   TU6_xTx(ASTC_12x10_UNORM_BLOCK,     ASTC_12x10,        ASTC_12x10,         WZYX), /* 181 */
   TU6_xTx(ASTC_12x10_SRGB_BLOCK,      ASTC_12x10,        ASTC_12x10,         WZYX), /* 182 */
   TU6_xTx(ASTC_12x12_UNORM_BLOCK,     ASTC_12x12,        ASTC_12x12,         WZYX), /* 183 */
   TU6_xTx(ASTC_12x12_SRGB_BLOCK,      ASTC_12x12,        ASTC_12x12,         WZYX), /* 184 */
};
#undef TU_FORMAT_TABLE_FIRST
#undef TU_FORMAT_TABLE_LAST

const struct tu_native_format *
tu6_get_native_format(VkFormat format)
{
   const struct tu_native_format *fmt = NULL;

   if (format >= tu6_format_table0_first && format <= tu6_format_table0_last)
      fmt = &tu6_format_table0[format - tu6_format_table0_first];

   return (fmt && fmt->present) ? fmt : NULL;
}

enum a6xx_2d_ifmt
tu6_rb_fmt_to_ifmt(enum a6xx_color_fmt fmt)
{
   switch (fmt) {
   case RB6_A8_UNORM:
   case RB6_R8_UNORM:
   case RB6_R8_SNORM:
   case RB6_R8G8_UNORM:
   case RB6_R8G8_SNORM:
   case RB6_R8G8B8A8_UNORM:
   case RB6_R8G8B8_UNORM:
   case RB6_R8G8B8A8_SNORM:
      return R2D_UNORM8;

   case RB6_R32_UINT:
   case RB6_R32_SINT:
   case RB6_R32G32_UINT:
   case RB6_R32G32_SINT:
   case RB6_R32G32B32A32_UINT:
   case RB6_R32G32B32A32_SINT:
      return R2D_INT32;

   case RB6_R16_UINT:
   case RB6_R16_SINT:
   case RB6_R16G16_UINT:
   case RB6_R16G16_SINT:
   case RB6_R16G16B16A16_UINT:
   case RB6_R16G16B16A16_SINT:
      return R2D_INT16;

   case RB6_R8_UINT:
   case RB6_R8_SINT:
   case RB6_R8G8_UINT:
   case RB6_R8G8_SINT:
   case RB6_R8G8B8A8_UINT:
   case RB6_R8G8B8A8_SINT:
      return R2D_INT8;

   case RB6_R16_UNORM:
   case RB6_R16_SNORM:
   case RB6_R16G16_UNORM:
   case RB6_R16G16_SNORM:
   case RB6_R16G16B16A16_UNORM:
   case RB6_R16G16B16A16_SNORM:
   case RB6_R32_FLOAT:
   case RB6_R32G32_FLOAT:
   case RB6_R32G32B32A32_FLOAT:
      return R2D_FLOAT32;

   case RB6_R16_FLOAT:
   case RB6_R16G16_FLOAT:
   case RB6_R16G16B16A16_FLOAT:
      return R2D_FLOAT16;

   case RB6_R4G4B4A4_UNORM:
   case RB6_R5G5B5A1_UNORM:
   case RB6_R5G6B5_UNORM:
   case RB6_R10G10B10A2_UNORM:
   case RB6_R10G10B10A2_UINT:
   case RB6_R11G11B10_FLOAT:
   case RB6_X8Z24_UNORM:
      // ???
      return 0;
   default:
      unreachable("bad format");
      return 0;
   }
}

static uint32_t
tu_pack_mask(int bits)
{
   assert(bits <= 32);
   return (1ull << bits) - 1;
}

static uint32_t
tu_pack_float32_for_unorm(float val, int bits)
{
   const uint32_t max = tu_pack_mask(bits);
   if (val < 0.0f)
      return 0;
   else if (val > 1.0f)
      return max;
   else
      return _mesa_lroundevenf(val * (float) max);
}

static uint32_t
tu_pack_float32_for_snorm(float val, int bits)
{
   const int32_t max = tu_pack_mask(bits - 1);
   int32_t tmp;
   if (val < -1.0f)
      tmp = -max;
   else if (val > 1.0f)
      tmp = max;
   else
      tmp = _mesa_lroundevenf(val * (float) max);

   return tmp & tu_pack_mask(bits);
}

static uint32_t
tu_pack_float32_for_uscaled(float val, int bits)
{
   const uint32_t max = tu_pack_mask(bits);
   if (val < 0.0f)
      return 0;
   else if (val > (float) max)
      return max;
   else
      return (uint32_t) val;
}

static uint32_t
tu_pack_float32_for_sscaled(float val, int bits)
{
   const int32_t max = tu_pack_mask(bits - 1);
   const int32_t min = -max - 1;
   int32_t tmp;
   if (val < (float) min)
      tmp = min;
   else if (val > (float) max)
      tmp = max;
   else
      tmp = (int32_t) val;

   return tmp & tu_pack_mask(bits);
}

static uint32_t
tu_pack_uint32_for_uint(uint32_t val, int bits)
{
   return val & tu_pack_mask(bits);
}

static uint32_t
tu_pack_int32_for_sint(int32_t val, int bits)
{
   return val & tu_pack_mask(bits);
}

static uint32_t
tu_pack_float32_for_sfloat(float val, int bits)
{
   assert(bits == 16 || bits == 32);
   return bits == 16 ? util_float_to_half(val) : fui(val);
}

union tu_clear_component_value {
   float float32;
   int32_t int32;
   uint32_t uint32;
};

static uint32_t
tu_pack_clear_component_value(union tu_clear_component_value val,
                              const struct vk_format_channel_description *ch)
{
   uint32_t packed;

   switch (ch->type) {
   case VK_FORMAT_TYPE_UNSIGNED:
      /* normalized, scaled, or pure integer */
      assert(ch->normalized + ch->scaled + ch->pure_integer == 1);
      if (ch->normalized)
         packed = tu_pack_float32_for_unorm(val.float32, ch->size);
      else if (ch->scaled)
         packed = tu_pack_float32_for_uscaled(val.float32, ch->size);
      else
         packed = tu_pack_uint32_for_uint(val.uint32, ch->size);
      break;
   case VK_FORMAT_TYPE_SIGNED:
      /* normalized, scaled, or pure integer */
      assert(ch->normalized + ch->scaled + ch->pure_integer == 1);
      if (ch->normalized)
         packed = tu_pack_float32_for_snorm(val.float32, ch->size);
      else if (ch->scaled)
         packed = tu_pack_float32_for_sscaled(val.float32, ch->size);
      else
         packed = tu_pack_int32_for_sint(val.int32, ch->size);
      break;
   case VK_FORMAT_TYPE_FLOAT:
      packed = tu_pack_float32_for_sfloat(val.float32, ch->size);
      break;
   default:
      unreachable("unexpected channel type");
      packed = 0;
      break;
   }

   assert((packed & tu_pack_mask(ch->size)) == packed);
   return packed;
}

static const struct vk_format_channel_description *
tu_get_format_channel_description(const struct vk_format_description *desc,
                                  int comp)
{
   switch (desc->swizzle[comp]) {
   case VK_SWIZZLE_X:
      return &desc->channel[0];
   case VK_SWIZZLE_Y:
      return &desc->channel[1];
   case VK_SWIZZLE_Z:
      return &desc->channel[2];
   case VK_SWIZZLE_W:
      return &desc->channel[3];
   default:
      return NULL;
   }
}

static union tu_clear_component_value
tu_get_clear_component_value(const VkClearValue *val, int comp, bool color)
{
   union tu_clear_component_value tmp;
   if (color) {
      assert(comp < 4);
      tmp.uint32 = val->color.uint32[comp];
   } else {
      assert(comp < 2);
      if (comp == 0)
         tmp.float32 = val->depthStencil.depth;
      else
         tmp.uint32 = val->depthStencil.stencil;
   }

   return tmp;
}

/**
 * Pack a VkClearValue into a 128-bit buffer.  \a format is respected except
 * for the component order.  The components are always packed in WZYX order
 * (i.e., msb is white and lsb is red).
 *
 * Return the number of uint32_t's used.
 */
int
tu_pack_clear_value(const VkClearValue *val, VkFormat format, uint32_t buf[4])
{
   const struct vk_format_description *desc = vk_format_description(format);
   assert(desc && desc->layout == VK_FORMAT_LAYOUT_PLAIN);

   /* S8_UINT is special and has no depth */
   const int max_components =
      format == VK_FORMAT_S8_UINT ? 2 : desc->nr_channels;

   int buf_offset = 0;
   int bit_shift = 0;
   for (int comp = 0; comp < max_components; comp++) {
      const struct vk_format_channel_description *ch =
         tu_get_format_channel_description(desc, comp);
      if (!ch) {
         assert(format == VK_FORMAT_S8_UINT && comp == 0);
         continue;
      }

      union tu_clear_component_value v = tu_get_clear_component_value(
         val, comp, desc->colorspace != VK_FORMAT_COLORSPACE_ZS);

      /* move to the next uint32_t when there is not enough space */
      assert(ch->size <= 32);
      if (bit_shift + ch->size > 32) {
         buf_offset++;
         bit_shift = 0;
      }

      if (bit_shift == 0)
         buf[buf_offset] = 0;

      buf[buf_offset] |= tu_pack_clear_component_value(v, ch) << bit_shift;
      bit_shift += ch->size;
   }

   return buf_offset + 1;
}

static void
tu_physical_device_get_format_properties(
   struct tu_physical_device *physical_device,
   VkFormat format,
   VkFormatProperties *out_properties)
{
   VkFormatFeatureFlags linear = 0, tiled = 0, buffer = 0;
   const struct vk_format_description *desc = vk_format_description(format);
   const struct tu_native_format *native_fmt = tu6_get_native_format(format);
   if (!desc || !native_fmt) {
      out_properties->linearTilingFeatures = linear;
      out_properties->optimalTilingFeatures = tiled;
      out_properties->bufferFeatures = buffer;
      return;
   }

   linear |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
   tiled |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
   buffer |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;

   if (native_fmt->tex >= 0) {
      linear |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
      tiled |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
      buffer |= VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;
   }

   if (native_fmt->rb >= 0) {
      linear |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
      tiled |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
   }

   if (native_fmt->vtx >= 0) {
      buffer |= VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
   }

   out_properties->linearTilingFeatures = linear;
   out_properties->optimalTilingFeatures = tiled;
   out_properties->bufferFeatures = buffer;
}

void
tu_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice,
                                     VkFormat format,
                                     VkFormatProperties *pFormatProperties)
{
   TU_FROM_HANDLE(tu_physical_device, physical_device, physicalDevice);

   tu_physical_device_get_format_properties(physical_device, format,
                                            pFormatProperties);
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
}

static VkResult
tu_get_image_format_properties(
   struct tu_physical_device *physical_device,
   const VkPhysicalDeviceImageFormatInfo2 *info,
   VkImageFormatProperties *pImageFormatProperties)

{
   VkFormatProperties format_props;
   VkFormatFeatureFlags format_feature_flags;
   VkExtent3D maxExtent;
   uint32_t maxMipLevels;
   uint32_t maxArraySize;
   VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;

   tu_physical_device_get_format_properties(physical_device, info->format,
                                            &format_props);
   if (info->tiling == VK_IMAGE_TILING_LINEAR) {
      format_feature_flags = format_props.linearTilingFeatures;
   } else if (info->tiling == VK_IMAGE_TILING_OPTIMAL) {
      format_feature_flags = format_props.optimalTilingFeatures;
   } else {
      unreachable("bad VkImageTiling");
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
      sampleCounts |= VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT |
                      VK_SAMPLE_COUNT_8_BIT;
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

VkResult
tu_GetPhysicalDeviceImageFormatProperties(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkImageType type,
   VkImageTiling tiling,
   VkImageUsageFlags usage,
   VkImageCreateFlags createFlags,
   VkImageFormatProperties *pImageFormatProperties)
{
   TU_FROM_HANDLE(tu_physical_device, physical_device, physicalDevice);

   const VkPhysicalDeviceImageFormatInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = NULL,
      .format = format,
      .type = type,
      .tiling = tiling,
      .usage = usage,
      .flags = createFlags,
   };

   return tu_get_image_format_properties(physical_device, &info,
                                         pImageFormatProperties);
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
   VkExternalImageFormatProperties *external_props = NULL;
   VkResult result;

   result = tu_get_image_format_properties(
      physical_device, base_info, &base_props->imageFormatProperties);
   if (result != VK_SUCCESS)
      return result;

   /* Extract input structs */
   vk_foreach_struct_const(s, base_info->pNext)
   {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const void *) s;
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
      base_props->imageFormatProperties = (VkImageFormatProperties) { 0 };
   }

   return result;
}

void
tu_GetPhysicalDeviceSparseImageFormatProperties(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkImageType type,
   uint32_t samples,
   VkImageUsageFlags usage,
   VkImageTiling tiling,
   uint32_t *pNumProperties,
   VkSparseImageFormatProperties *pProperties)
{
   /* Sparse images are not yet supported. */
   *pNumProperties = 0;
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
