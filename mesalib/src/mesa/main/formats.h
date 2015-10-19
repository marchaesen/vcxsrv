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

/*
 * Authors:
 *   Brian Paul
 */


#ifndef FORMATS_H
#define FORMATS_H


#include <GL/gl.h>
#include <stdbool.h>
#include <stdint.h>
#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * OpenGL doesn't have GL_UNSIGNED_BYTE_4_4, so we must define our own type
 * for GL_LUMINANCE4_ALPHA4.
 */
#define MESA_UNSIGNED_BYTE_4_4 (GL_UNSIGNED_BYTE<<1)


/**
 * Max number of bytes for any non-compressed pixel format below, or for
 * intermediate pixel storage in Mesa.  This should never be less than
 * 16.  Maybe 32 someday?
 */
#define MAX_PIXEL_BYTES 16

/**
 * Specifies the layout of a pixel format.  See the MESA_FORMAT
 * documentation below.
 */
enum mesa_format_layout {
   MESA_FORMAT_LAYOUT_ARRAY,
   MESA_FORMAT_LAYOUT_PACKED,
   MESA_FORMAT_LAYOUT_S3TC,
   MESA_FORMAT_LAYOUT_RGTC,
   MESA_FORMAT_LAYOUT_LATC,
   MESA_FORMAT_LAYOUT_FXT1,
   MESA_FORMAT_LAYOUT_ETC1,
   MESA_FORMAT_LAYOUT_ETC2,
   MESA_FORMAT_LAYOUT_BPTC,
   MESA_FORMAT_LAYOUT_ASTC,
   MESA_FORMAT_LAYOUT_OTHER,
};

/**
 * An enum representing different possible swizzling values.  This is used
 * to interpret the output of _mesa_get_format_swizzle
 */
enum {
   MESA_FORMAT_SWIZZLE_X = 0,
   MESA_FORMAT_SWIZZLE_Y = 1,
   MESA_FORMAT_SWIZZLE_Z = 2,
   MESA_FORMAT_SWIZZLE_W = 3,
   MESA_FORMAT_SWIZZLE_ZERO = 4,
   MESA_FORMAT_SWIZZLE_ONE = 5,
   MESA_FORMAT_SWIZZLE_NONE = 6,
};

/**
 * An uint32_t that encodes the information necessary to represent an
 * array format
 */
typedef uint32_t mesa_array_format;

/**
 * Encoding for valid array format data types
 */
enum mesa_array_format_datatype {
   MESA_ARRAY_FORMAT_TYPE_UBYTE = 0x0,
   MESA_ARRAY_FORMAT_TYPE_USHORT = 0x1,
   MESA_ARRAY_FORMAT_TYPE_UINT = 0x2,
   MESA_ARRAY_FORMAT_TYPE_BYTE = 0x4,
   MESA_ARRAY_FORMAT_TYPE_SHORT = 0x5,
   MESA_ARRAY_FORMAT_TYPE_INT = 0x6,
   MESA_ARRAY_FORMAT_TYPE_HALF = 0xd,
   MESA_ARRAY_FORMAT_TYPE_FLOAT = 0xe,
};

/**
 * An enum useful to encode/decode information stored in a mesa_array_format
 */
enum {
   MESA_ARRAY_FORMAT_TYPE_IS_SIGNED = 0x4,
   MESA_ARRAY_FORMAT_TYPE_IS_FLOAT = 0x8,
   MESA_ARRAY_FORMAT_TYPE_NORMALIZED = 0x10,
   MESA_ARRAY_FORMAT_DATATYPE_MASK = 0xf,
   MESA_ARRAY_FORMAT_TYPE_MASK = 0x1f,
   MESA_ARRAY_FORMAT_TYPE_SIZE_MASK = 0x3,
   MESA_ARRAY_FORMAT_NUM_CHANS_MASK = 0xe0,
   MESA_ARRAY_FORMAT_SWIZZLE_X_MASK = 0x00700,
   MESA_ARRAY_FORMAT_SWIZZLE_Y_MASK = 0x03800,
   MESA_ARRAY_FORMAT_SWIZZLE_Z_MASK = 0x1c000,
   MESA_ARRAY_FORMAT_SWIZZLE_W_MASK = 0xe0000,
   MESA_ARRAY_FORMAT_BIT = 0x80000000
};

#define MESA_ARRAY_FORMAT(SIZE, SIGNED, IS_FLOAT, NORM, NUM_CHANS, \
      SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_W) (                \
   (((SIZE >> 1)      ) & MESA_ARRAY_FORMAT_TYPE_SIZE_MASK) |      \
   (((SIGNED)    << 2 ) & MESA_ARRAY_FORMAT_TYPE_IS_SIGNED) |      \
   (((IS_FLOAT)  << 3 ) & MESA_ARRAY_FORMAT_TYPE_IS_FLOAT) |       \
   (((NORM)      << 4 ) & MESA_ARRAY_FORMAT_TYPE_NORMALIZED) |     \
   (((NUM_CHANS) << 5 ) & MESA_ARRAY_FORMAT_NUM_CHANS_MASK) |      \
   (((SWIZZLE_X) << 8 ) & MESA_ARRAY_FORMAT_SWIZZLE_X_MASK) |      \
   (((SWIZZLE_Y) << 11) & MESA_ARRAY_FORMAT_SWIZZLE_Y_MASK) |      \
   (((SWIZZLE_Z) << 14) & MESA_ARRAY_FORMAT_SWIZZLE_Z_MASK) |      \
   (((SWIZZLE_W) << 17) & MESA_ARRAY_FORMAT_SWIZZLE_W_MASK) |      \
   MESA_ARRAY_FORMAT_BIT)

/**
 * Various helpers to access the data encoded in a mesa_array_format
 */
static inline bool
_mesa_array_format_is_signed(mesa_array_format f)
{
   return (f & MESA_ARRAY_FORMAT_TYPE_IS_SIGNED) != 0;
}

static inline bool
_mesa_array_format_is_float(mesa_array_format f)
{
   return (f & MESA_ARRAY_FORMAT_TYPE_IS_FLOAT) != 0;
}

static inline bool
_mesa_array_format_is_normalized(mesa_array_format f)
{
   return (f & MESA_ARRAY_FORMAT_TYPE_NORMALIZED) !=0;
}

static inline enum mesa_array_format_datatype
_mesa_array_format_get_datatype(mesa_array_format f)
{
   return (enum mesa_array_format_datatype)
            (f & MESA_ARRAY_FORMAT_DATATYPE_MASK);
}

static inline int
_mesa_array_format_datatype_get_size(enum mesa_array_format_datatype type)
{
   return 1 << (type & MESA_ARRAY_FORMAT_TYPE_SIZE_MASK);
}

static inline int
_mesa_array_format_get_type_size(mesa_array_format f)
{
   return 1 << (f & MESA_ARRAY_FORMAT_TYPE_SIZE_MASK);
}

static inline int
_mesa_array_format_get_num_channels(mesa_array_format f)
{
   return (f & MESA_ARRAY_FORMAT_NUM_CHANS_MASK) >> 5;
}

static inline void
_mesa_array_format_get_swizzle(mesa_array_format f, uint8_t *swizzle)
{
   swizzle[0] = (f & MESA_ARRAY_FORMAT_SWIZZLE_X_MASK) >> 8;
   swizzle[1] = (f & MESA_ARRAY_FORMAT_SWIZZLE_Y_MASK) >> 11;
   swizzle[2] = (f & MESA_ARRAY_FORMAT_SWIZZLE_Z_MASK) >> 14;
   swizzle[3] = (f & MESA_ARRAY_FORMAT_SWIZZLE_W_MASK) >> 17;
}

static inline void
_mesa_array_format_set_swizzle(mesa_array_format *f,
                               int32_t x, int32_t y, int32_t z, int32_t w)
{
   *f &= ~(MESA_ARRAY_FORMAT_SWIZZLE_X_MASK |
           MESA_ARRAY_FORMAT_SWIZZLE_Y_MASK |
           MESA_ARRAY_FORMAT_SWIZZLE_Z_MASK |
           MESA_ARRAY_FORMAT_SWIZZLE_W_MASK);

   *f |= ((x << 8 ) & MESA_ARRAY_FORMAT_SWIZZLE_X_MASK) |
         ((y << 11) & MESA_ARRAY_FORMAT_SWIZZLE_Y_MASK) |
         ((z << 14) & MESA_ARRAY_FORMAT_SWIZZLE_Z_MASK) |
         ((w << 17) & MESA_ARRAY_FORMAT_SWIZZLE_W_MASK);
}

/**
 * A helper to know if the format stored in a uint32_t is a mesa_format
 * or a mesa_array_format
 */
static inline bool
_mesa_format_is_mesa_array_format(uint32_t f)
{
   return (f & MESA_ARRAY_FORMAT_BIT) != 0;
}

/**
 * Mesa texture/renderbuffer image formats.
 */
typedef enum
{
   MESA_FORMAT_NONE = 0,

   /**
    * \name Basic hardware formats
    *
    * The mesa format name specification is as follows:
    *
    *  There shall be 3 naming format base types: those for component array
    *  formats (type A); those for compressed formats (type C); and those for
    *  packed component formats (type P). With type A formats, color component
    *  order does not change with endianess. Each format name shall begin with
    *  MESA_FORMAT_, followed by a component label (from the Component Label
    *  list below) for each component in the order that the component(s) occur
    *  in the format, except for non-linear color formats where the first
    *  letter shall be 'S'. For type P formats, each component label is
    *  followed by the number of bits that represent it in the fundamental
    *  data type used by the format.
    *
    *  Following the listing of the component labels shall be an underscore; a
    *  compression type followed by an underscore for Type C formats only; a
    *  storage type from the list below; and a bit with for type A formats,
    *  which is the bit width for each array element.
    *
    *
    *  ----------    Format Base Type A: Array ----------
    *  MESA_FORMAT_[component list]_[storage type][array element bit width]
    *
    *  examples:
    *  MESA_FORMAT_A_SNORM8     - uchar[i] = A
    *  MESA_FORMAT_RGBA_16 - ushort[i * 4 + 0] = R, ushort[i * 4 + 1] = G,
    *                             ushort[i * 4 + 2] = B, ushort[i * 4 + 3] = A
    *  MESA_FORMAT_Z_UNORM32    - float[i] = Z
    *
    *
    *
    *  ----------    Format Base Type C: Compressed ----------
    *  MESA_FORMAT_[component list*][_*][compression type][storage type*]
    *  * where required
    *
    *  examples:
    *  MESA_FORMAT_RGB_ETC1
    *  MESA_FORMAT_RGBA_ETC2
    *  MESA_FORMAT_LATC1_UNORM
    *  MESA_FORMAT_RGBA_FXT1
    *
    *
    *
    *  ----------    Format Base Type P: Packed  ----------
    *  MESA_FORMAT_[[component list,bit width][storage type*][_]][_][storage type**]
    *   * when type differs between component
    *   ** when type applies to all components
    *
    *  examples:                   msb <------ TEXEL BITS -----------> lsb
    *  MESA_FORMAT_A8B8G8R8_UNORM, RRRR RRRR GGGG GGGG BBBB BBBB AAAA AAAA
    *  MESA_FORMAT_R5G6B5_UNORM                        BBBB BGGG GGGR RRRR
    *  MESA_FORMAT_B4G4R4X4_UNORM                      XXXX RRRR GGGG BBBB
    *  MESA_FORMAT_Z32_FLOAT_S8X24_UINT
    *  MESA_FORMAT_R10G10B10A2_UINT
    *  MESA_FORMAT_R9G9B9E5_FLOAT
    *
    *
    *
    *  ----------    Component Labels: ----------
    *  A - Alpha
    *  B - Blue
    *  DU - Delta U
    *  DV - Delta V
    *  E - Shared Exponent
    *  G - Green
    *  I - Intensity
    *  L - Luminance
    *  R - Red
    *  S - Stencil (when not followed by RGB or RGBA)
    *  U - Chrominance
    *  V - Chrominance
    *  Y - Luma
    *  X - Packing bits
    *  Z - Depth
    *
    *
    *
    *  ----------    Type C Compression Types: ----------
    *  DXT1 - Color component labels shall be given
    *  DXT3 - Color component labels shall be given
    *  DXT5 - Color component labels shall be given
    *  ETC1 - No other information required
    *  ETC2 - No other information required
    *  FXT1 - Color component labels shall be given
    *  FXT3 - Color component labels shall be given
    *  LATC1 - Fundamental data type shall be given
    *  LATC2 - Fundamental data type shall be given
    *  RGTC1 - Color component labels and data type shall be given
    *  RGTC2 - Color component labels and data type shall be given
    *
    *
    *
    *  ----------    Storage Types: ----------
    *  FLOAT
    *  SINT
    *  UINT
    *  SNORM
    *  UNORM
    *  SRGB - RGB components, or L are UNORMs in sRGB color space.
    *         Alpha, if present is linear.
    *
    */

   /* Packed unorm formats */    /* msb <------ TEXEL BITS -----------> lsb */
                                 /* ---- ---- ---- ---- ---- ---- ---- ---- */
   MESA_FORMAT_A8B8G8R8_UNORM,   /* RRRR RRRR GGGG GGGG BBBB BBBB AAAA AAAA */
   MESA_FORMAT_X8B8G8R8_UNORM,   /* RRRR RRRR GGGG GGGG BBBB BBBB xxxx xxxx */
   MESA_FORMAT_R8G8B8A8_UNORM,   /* AAAA AAAA BBBB BBBB GGGG GGGG RRRR RRRR */
   MESA_FORMAT_R8G8B8X8_UNORM,   /* xxxx xxxx BBBB BBBB GGGG GGGG RRRR RRRR */
   MESA_FORMAT_B8G8R8A8_UNORM,   /* AAAA AAAA RRRR RRRR GGGG GGGG BBBB BBBB */
   MESA_FORMAT_B8G8R8X8_UNORM,   /* xxxx xxxx RRRR RRRR GGGG GGGG BBBB BBBB */
   MESA_FORMAT_A8R8G8B8_UNORM,   /* BBBB BBBB GGGG GGGG RRRR RRRR AAAA AAAA */
   MESA_FORMAT_X8R8G8B8_UNORM,   /* BBBB BBBB GGGG GGGG RRRR RRRR xxxx xxxx */
   MESA_FORMAT_L16A16_UNORM,     /* AAAA AAAA AAAA AAAA LLLL LLLL LLLL LLLL */
   MESA_FORMAT_A16L16_UNORM,     /* LLLL LLLL LLLL LLLL AAAA AAAA AAAA AAAA */
   MESA_FORMAT_B5G6R5_UNORM,                         /* RRRR RGGG GGGB BBBB */
   MESA_FORMAT_R5G6B5_UNORM,                         /* BBBB BGGG GGGR RRRR */
   MESA_FORMAT_B4G4R4A4_UNORM,                       /* AAAA RRRR GGGG BBBB */
   MESA_FORMAT_B4G4R4X4_UNORM,                       /* xxxx RRRR GGGG BBBB */
   MESA_FORMAT_A4R4G4B4_UNORM,                       /* BBBB GGGG RRRR AAAA */
   MESA_FORMAT_A1B5G5R5_UNORM,                       /* RRRR RGGG GGBB BBBA */
   MESA_FORMAT_B5G5R5A1_UNORM,                       /* ARRR RRGG GGGB BBBB */
   MESA_FORMAT_B5G5R5X1_UNORM,                       /* xRRR RRGG GGGB BBBB */
   MESA_FORMAT_A1R5G5B5_UNORM,                       /* BBBB BGGG GGRR RRRA */
   MESA_FORMAT_L8A8_UNORM,                           /* AAAA AAAA LLLL LLLL */
   MESA_FORMAT_A8L8_UNORM,                           /* LLLL LLLL AAAA AAAA */
   MESA_FORMAT_R8G8_UNORM,                           /* GGGG GGGG RRRR RRRR */
   MESA_FORMAT_G8R8_UNORM,                           /* RRRR RRRR GGGG GGGG */
   MESA_FORMAT_L4A4_UNORM,                                     /* AAAA LLLL */
   MESA_FORMAT_B2G3R3_UNORM,                                   /* RRRG GGBB */

   MESA_FORMAT_R16G16_UNORM,     /* GGGG GGGG GGGG GGGG RRRR RRRR RRRR RRRR */
   MESA_FORMAT_G16R16_UNORM,     /* RRRR RRRR RRRR RRRR GGGG GGGG GGGG GGGG */
   MESA_FORMAT_B10G10R10A2_UNORM,/* AARR RRRR RRRR GGGG GGGG GGBB BBBB BBBB */
   MESA_FORMAT_B10G10R10X2_UNORM,/* xxRR RRRR RRRR GGGG GGGG GGBB BBBB BBBB */
   MESA_FORMAT_R10G10B10A2_UNORM,/* AABB BBBB BBBB GGGG GGGG GGRR RRRR RRRR */
   MESA_FORMAT_R10G10B10X2_UNORM,/* xxBB BBBB BBBB GGGG GGGG GGRR RRRR RRRR */

   MESA_FORMAT_S8_UINT_Z24_UNORM,/* ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ SSSS SSSS */
   MESA_FORMAT_X8_UINT_Z24_UNORM,/* ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ xxxx xxxx */
   MESA_FORMAT_Z24_UNORM_S8_UINT,/* SSSS SSSS ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ */
   MESA_FORMAT_Z24_UNORM_X8_UINT,/* xxxx xxxx ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ ZZZZ */

   /* Other formats */
   MESA_FORMAT_R3G3B2_UNORM,                                   /* BBGG GRRR */
   MESA_FORMAT_A4B4G4R4_UNORM,                       /* RRRR GGGG BBBB AAAA */
   MESA_FORMAT_R4G4B4A4_UNORM,                       /* AAAA BBBB GGGG RRRR */
   MESA_FORMAT_R5G5B5A1_UNORM,                       /* ABBB BBGG GGGR RRRR */
   MESA_FORMAT_A2B10G10R10_UNORM,/* RRRR RRRR RRGG GGGG GGGG BBBB BBBB BBAA */
   MESA_FORMAT_A2R10G10B10_UNORM,/* BBBB BBBB BBGG GGGG GGGG RRRR RRRR RRAA */

   MESA_FORMAT_YCBCR,            /*                     YYYY YYYY UorV UorV */
   MESA_FORMAT_YCBCR_REV,        /*                     UorV UorV YYYY YYYY */

   /* Array unorm formats */
   MESA_FORMAT_A_UNORM8,      /* ubyte[i] = A */
   MESA_FORMAT_A_UNORM16,     /* ushort[i] = A */
   MESA_FORMAT_L_UNORM8,      /* ubyte[i] = L */
   MESA_FORMAT_L_UNORM16,     /* ushort[i] = L */
   MESA_FORMAT_I_UNORM8,      /* ubyte[i] = I */
   MESA_FORMAT_I_UNORM16,     /* ushort[i] = I */
   MESA_FORMAT_R_UNORM8,      /* ubyte[i] = R */
   MESA_FORMAT_R_UNORM16,     /* ushort[i] = R */
   MESA_FORMAT_BGR_UNORM8,    /* ubyte[i*3] = B, [i*3+1] = G, [i*3+2] = R */
   MESA_FORMAT_RGB_UNORM8,    /* ubyte[i*3] = R, [i*3+1] = G, [i*3+2] = B */
   MESA_FORMAT_RGBA_UNORM16,  /* ushort[i] = R, [1] = G, [2] = B, [3] = A */
   MESA_FORMAT_RGBX_UNORM16,  

   MESA_FORMAT_Z_UNORM16,     /* ushort[i] = Z */
   MESA_FORMAT_Z_UNORM32,     /* uint[i] = Z */
   MESA_FORMAT_S_UINT8,       /* ubyte[i] = S */

   /* Packed signed/normalized formats */
                                 /* msb <------ TEXEL BITS -----------> lsb */
                                 /* ---- ---- ---- ---- ---- ---- ---- ---- */
   MESA_FORMAT_A8B8G8R8_SNORM,   /* RRRR RRRR GGGG GGGG BBBB BBBB AAAA AAAA */
   MESA_FORMAT_X8B8G8R8_SNORM,   /* RRRR RRRR GGGG GGGG BBBB BBBB xxxx xxxx */
   MESA_FORMAT_R8G8B8A8_SNORM,   /* AAAA AAAA BBBB BBBB GGGG GGGG RRRR RRRR */
   MESA_FORMAT_R8G8B8X8_SNORM,   /* xxxx xxxx BBBB BBBB GGGG GGGG RRRR RRRR */
   MESA_FORMAT_R16G16_SNORM,     /* GGGG GGGG GGGG GGGG RRRR RRRR RRRR RRRR */
   MESA_FORMAT_G16R16_SNORM,     /* RRRR RRRR RRRR RRRR GGGG GGGG GGGG GGGG */
   MESA_FORMAT_R8G8_SNORM,       /*                     GGGG GGGG RRRR RRRR */
   MESA_FORMAT_G8R8_SNORM,       /*                     RRRR RRRR GGGG GGGG */
   MESA_FORMAT_L8A8_SNORM,       /*                     AAAA AAAA LLLL LLLL */
   MESA_FORMAT_A8L8_SNORM,       /*                     LLLL LLLL AAAA AAAA */

   /* Array signed/normalized formats */
   MESA_FORMAT_A_SNORM8,      /* byte[i] = A */
   MESA_FORMAT_A_SNORM16,     /* short[i] = A */
   MESA_FORMAT_L_SNORM8,      /* byte[i] = L */
   MESA_FORMAT_L_SNORM16,     /* short[i] = L */
   MESA_FORMAT_I_SNORM8,      /* byte[i] = I */
   MESA_FORMAT_I_SNORM16,     /* short[i] = I */
   MESA_FORMAT_R_SNORM8,      /* byte[i] = R */
   MESA_FORMAT_R_SNORM16,     /* short[i] = R */
   MESA_FORMAT_LA_SNORM16,    /* short[i * 2] = L, [i * 2 + 1] = A */
   MESA_FORMAT_RGB_SNORM16,   /* short[i*3] = R, [i*3+1] = G, [i*3+2] = B */
   MESA_FORMAT_RGBA_SNORM16,  /* ... */
   MESA_FORMAT_RGBX_SNORM16,  /* ... */

   /* Packed sRGB formats */
   MESA_FORMAT_A8B8G8R8_SRGB,    /* RRRR RRRR GGGG GGGG BBBB BBBB AAAA AAAA */
   MESA_FORMAT_B8G8R8A8_SRGB,    /* AAAA AAAA RRRR RRRR GGGG GGGG BBBB BBBB */
   MESA_FORMAT_A8R8G8B8_SRGB,    /* BBBB BBBB GGGG GGGG RRRR RRRR AAAA AAAA */
   MESA_FORMAT_B8G8R8X8_SRGB,    /* xxxx xxxx RRRR RRRR GGGG GGGG BBBB BBBB */
   MESA_FORMAT_X8R8G8B8_SRGB,    /* BBBB BBBB GGGG GGGG RRRR RRRR xxxx xxxx */
   MESA_FORMAT_R8G8B8A8_SRGB,    /* AAAA AAAA BBBB BBBB GGGG GGGG RRRR RRRR */
   MESA_FORMAT_R8G8B8X8_SRGB,    /* xxxx xxxx BBBB BBBB GGGG GGGG RRRR RRRR */
   MESA_FORMAT_X8B8G8R8_SRGB,    /* RRRR RRRR GGGG GGGG BBBB BBBB xxxx xxxx */
   MESA_FORMAT_L8A8_SRGB,                            /* AAAA AAAA LLLL LLLL */
   MESA_FORMAT_A8L8_SRGB,                            /* LLLL LLLL AAAA AAAA */

   /* Array sRGB formats */
   MESA_FORMAT_L_SRGB8,       /* ubyte[i] = L */
   MESA_FORMAT_BGR_SRGB8,     /* ubyte[i*3] = B, [i*3+1] = G, [i*3+2] = R */

   /* Packed float formats */
   MESA_FORMAT_R9G9B9E5_FLOAT,
   MESA_FORMAT_R11G11B10_FLOAT,   /* BBBB BBBB BBGG GGGG GGGG GRRR RRRR RRRR */
   MESA_FORMAT_Z32_FLOAT_S8X24_UINT, /* (float, x24s8) */

   /* Array float formats */
   MESA_FORMAT_A_FLOAT16,
   MESA_FORMAT_A_FLOAT32,
   MESA_FORMAT_L_FLOAT16,
   MESA_FORMAT_L_FLOAT32,
   MESA_FORMAT_LA_FLOAT16,
   MESA_FORMAT_LA_FLOAT32,
   MESA_FORMAT_I_FLOAT16,
   MESA_FORMAT_I_FLOAT32,
   MESA_FORMAT_R_FLOAT16,
   MESA_FORMAT_R_FLOAT32,
   MESA_FORMAT_RG_FLOAT16,
   MESA_FORMAT_RG_FLOAT32,
   MESA_FORMAT_RGB_FLOAT16,
   MESA_FORMAT_RGB_FLOAT32,
   MESA_FORMAT_RGBA_FLOAT16,
   MESA_FORMAT_RGBA_FLOAT32,  /* float[0] = R, [1] = G, [2] = B, [3] = A */
   MESA_FORMAT_RGBX_FLOAT16,
   MESA_FORMAT_RGBX_FLOAT32,
   MESA_FORMAT_Z_FLOAT32,

   /* Packed signed/unsigned non-normalized integer formats */

   MESA_FORMAT_A8B8G8R8_UINT,    /* RRRR RRRR GGGG GGGG BBBB BBBB AAAA AAAA */
   MESA_FORMAT_A8R8G8B8_UINT,    /* BBBB BBBB GGGG GGGG RRRR RRRR AAAA AAAA */
   MESA_FORMAT_R8G8B8A8_UINT,    /* AAAA AAAA BBBB BBBB GGGG GGGG RRRR RRRR */
   MESA_FORMAT_B8G8R8A8_UINT,    /* AAAA AAAA RRRR RRRR GGGG GGGG BBBB BBBB */
   MESA_FORMAT_B10G10R10A2_UINT, /* AARR RRRR RRRR GGGG GGGG GGBB BBBB BBBB */
   MESA_FORMAT_R10G10B10A2_UINT, /* AABB BBBB BBBB GGGG GGGG GGRR RRRR RRRR */
   MESA_FORMAT_A2B10G10R10_UINT, /* RRRR RRRR RRGG GGGG GGGG BBBB BBBB BBAA */
   MESA_FORMAT_A2R10G10B10_UINT, /* BBBB BBBB BBGG GGGG GGGG RRRR RRRR RRAA */
   MESA_FORMAT_B5G6R5_UINT,                          /* RRRR RGGG GGGB BBBB */
   MESA_FORMAT_R5G6B5_UINT,                          /* BBBB BGGG GGGR RRRR */
   MESA_FORMAT_B2G3R3_UINT,                                    /* RRRG GGBB */
   MESA_FORMAT_R3G3B2_UINT,                                    /* BBGG GRRR */
   MESA_FORMAT_A4B4G4R4_UINT,                        /* RRRR GGGG BBBB AAAA */
   MESA_FORMAT_R4G4B4A4_UINT,                        /* AAAA BBBB GGGG RRRR */
   MESA_FORMAT_B4G4R4A4_UINT,                        /* AAAA RRRR GGGG BBBB */
   MESA_FORMAT_A4R4G4B4_UINT,                        /* BBBB GGGG RRRR AAAA */
   MESA_FORMAT_A1B5G5R5_UINT,                        /* RRRR RGGG GGBB BBBA */
   MESA_FORMAT_B5G5R5A1_UINT,                        /* ARRR RRGG GGGB BBBB */
   MESA_FORMAT_A1R5G5B5_UINT,                        /* BBBB BGGG GGRR RRRA */
   MESA_FORMAT_R5G5B5A1_UINT,                        /* ABBB BBGG GGGR RRRR */

   /* Array signed/unsigned non-normalized integer formats */
   MESA_FORMAT_A_UINT8,
   MESA_FORMAT_A_UINT16,
   MESA_FORMAT_A_UINT32,
   MESA_FORMAT_A_SINT8,
   MESA_FORMAT_A_SINT16,
   MESA_FORMAT_A_SINT32,

   MESA_FORMAT_I_UINT8,
   MESA_FORMAT_I_UINT16,
   MESA_FORMAT_I_UINT32,
   MESA_FORMAT_I_SINT8,
   MESA_FORMAT_I_SINT16,
   MESA_FORMAT_I_SINT32,

   MESA_FORMAT_L_UINT8,
   MESA_FORMAT_L_UINT16,
   MESA_FORMAT_L_UINT32,
   MESA_FORMAT_L_SINT8,
   MESA_FORMAT_L_SINT16,
   MESA_FORMAT_L_SINT32,

   MESA_FORMAT_LA_UINT8,
   MESA_FORMAT_LA_UINT16,
   MESA_FORMAT_LA_UINT32,
   MESA_FORMAT_LA_SINT8,
   MESA_FORMAT_LA_SINT16,
   MESA_FORMAT_LA_SINT32,

   MESA_FORMAT_R_UINT8,
   MESA_FORMAT_R_UINT16,
   MESA_FORMAT_R_UINT32,
   MESA_FORMAT_R_SINT8,
   MESA_FORMAT_R_SINT16,
   MESA_FORMAT_R_SINT32,

   MESA_FORMAT_RG_UINT8,
   MESA_FORMAT_RG_UINT16,
   MESA_FORMAT_RG_UINT32,
   MESA_FORMAT_RG_SINT8,
   MESA_FORMAT_RG_SINT16,
   MESA_FORMAT_RG_SINT32,

   MESA_FORMAT_RGB_UINT8,
   MESA_FORMAT_RGB_UINT16,
   MESA_FORMAT_RGB_UINT32,
   MESA_FORMAT_RGB_SINT8,
   MESA_FORMAT_RGB_SINT16,
   MESA_FORMAT_RGB_SINT32,

   MESA_FORMAT_RGBA_UINT8,
   MESA_FORMAT_RGBA_UINT16,
   MESA_FORMAT_RGBA_UINT32,
   MESA_FORMAT_RGBA_SINT8,
   MESA_FORMAT_RGBA_SINT16,
   MESA_FORMAT_RGBA_SINT32,

   MESA_FORMAT_RGBX_UINT8,
   MESA_FORMAT_RGBX_UINT16,
   MESA_FORMAT_RGBX_UINT32,
   MESA_FORMAT_RGBX_SINT8,
   MESA_FORMAT_RGBX_SINT16,
   MESA_FORMAT_RGBX_SINT32,

   /* DXT compressed formats */
   MESA_FORMAT_RGB_DXT1,
   MESA_FORMAT_RGBA_DXT1,
   MESA_FORMAT_RGBA_DXT3,
   MESA_FORMAT_RGBA_DXT5,

   /* DXT sRGB compressed formats */
   MESA_FORMAT_SRGB_DXT1,
   MESA_FORMAT_SRGBA_DXT1,
   MESA_FORMAT_SRGBA_DXT3,
   MESA_FORMAT_SRGBA_DXT5,

   /* FXT1 compressed formats */
   MESA_FORMAT_RGB_FXT1,
   MESA_FORMAT_RGBA_FXT1,

   /* RGTC compressed formats */
   MESA_FORMAT_R_RGTC1_UNORM,
   MESA_FORMAT_R_RGTC1_SNORM,
   MESA_FORMAT_RG_RGTC2_UNORM,
   MESA_FORMAT_RG_RGTC2_SNORM,

   /* LATC1/2 compressed formats */
   MESA_FORMAT_L_LATC1_UNORM,
   MESA_FORMAT_L_LATC1_SNORM,
   MESA_FORMAT_LA_LATC2_UNORM,
   MESA_FORMAT_LA_LATC2_SNORM,

   /* ETC1/2 compressed formats */
   MESA_FORMAT_ETC1_RGB8,
   MESA_FORMAT_ETC2_RGB8,
   MESA_FORMAT_ETC2_SRGB8,
   MESA_FORMAT_ETC2_RGBA8_EAC,
   MESA_FORMAT_ETC2_SRGB8_ALPHA8_EAC,
   MESA_FORMAT_ETC2_R11_EAC,
   MESA_FORMAT_ETC2_RG11_EAC,
   MESA_FORMAT_ETC2_SIGNED_R11_EAC,
   MESA_FORMAT_ETC2_SIGNED_RG11_EAC,
   MESA_FORMAT_ETC2_RGB8_PUNCHTHROUGH_ALPHA1,
   MESA_FORMAT_ETC2_SRGB8_PUNCHTHROUGH_ALPHA1,

   /* BPTC compressed formats */
   MESA_FORMAT_BPTC_RGBA_UNORM,
   MESA_FORMAT_BPTC_SRGB_ALPHA_UNORM,
   MESA_FORMAT_BPTC_RGB_SIGNED_FLOAT,
   MESA_FORMAT_BPTC_RGB_UNSIGNED_FLOAT,

   /* ASTC compressed formats */
   MESA_FORMAT_RGBA_ASTC_4x4,
   MESA_FORMAT_RGBA_ASTC_5x4,
   MESA_FORMAT_RGBA_ASTC_5x5,
   MESA_FORMAT_RGBA_ASTC_6x5,
   MESA_FORMAT_RGBA_ASTC_6x6,
   MESA_FORMAT_RGBA_ASTC_8x5,
   MESA_FORMAT_RGBA_ASTC_8x6,
   MESA_FORMAT_RGBA_ASTC_8x8,
   MESA_FORMAT_RGBA_ASTC_10x5,
   MESA_FORMAT_RGBA_ASTC_10x6,
   MESA_FORMAT_RGBA_ASTC_10x8,
   MESA_FORMAT_RGBA_ASTC_10x10,
   MESA_FORMAT_RGBA_ASTC_12x10,
   MESA_FORMAT_RGBA_ASTC_12x12,

   MESA_FORMAT_SRGB8_ALPHA8_ASTC_4x4,
   MESA_FORMAT_SRGB8_ALPHA8_ASTC_5x4,
   MESA_FORMAT_SRGB8_ALPHA8_ASTC_5x5,
   MESA_FORMAT_SRGB8_ALPHA8_ASTC_6x5,
   MESA_FORMAT_SRGB8_ALPHA8_ASTC_6x6,
   MESA_FORMAT_SRGB8_ALPHA8_ASTC_8x5,
   MESA_FORMAT_SRGB8_ALPHA8_ASTC_8x6,
   MESA_FORMAT_SRGB8_ALPHA8_ASTC_8x8,
   MESA_FORMAT_SRGB8_ALPHA8_ASTC_10x5,
   MESA_FORMAT_SRGB8_ALPHA8_ASTC_10x6,
   MESA_FORMAT_SRGB8_ALPHA8_ASTC_10x8,
   MESA_FORMAT_SRGB8_ALPHA8_ASTC_10x10,
   MESA_FORMAT_SRGB8_ALPHA8_ASTC_12x10,
   MESA_FORMAT_SRGB8_ALPHA8_ASTC_12x12,
   MESA_FORMAT_COUNT
} mesa_format;


extern const char *
_mesa_get_format_name(mesa_format format);

extern GLint
_mesa_get_format_bytes(mesa_format format);

extern GLint
_mesa_get_format_bits(mesa_format format, GLenum pname);

extern GLuint
_mesa_get_format_max_bits(mesa_format format);

extern enum mesa_format_layout
_mesa_get_format_layout(mesa_format format);

extern GLenum
_mesa_get_format_datatype(mesa_format format);

extern GLenum
_mesa_get_format_base_format(uint32_t format);

extern void
_mesa_get_format_block_size(mesa_format format, GLuint *bw, GLuint *bh);

extern mesa_array_format
_mesa_array_format_flip_channels(mesa_array_format format);

extern void
_mesa_get_format_swizzle(mesa_format format, uint8_t swizzle_out[4]);

extern uint32_t
_mesa_format_to_array_format(mesa_format format);

extern mesa_format
_mesa_format_from_array_format(uint32_t array_format);

extern GLboolean
_mesa_is_format_compressed(mesa_format format);

extern GLboolean
_mesa_is_format_packed_depth_stencil(mesa_format format);

extern GLboolean
_mesa_is_format_integer_color(mesa_format format);

extern GLboolean
_mesa_is_format_unsigned(mesa_format format);

extern GLboolean
_mesa_is_format_signed(mesa_format format);

extern GLboolean
_mesa_is_format_integer(mesa_format format);

extern bool
_mesa_is_format_etc2(mesa_format format);

GLenum
_mesa_is_format_color_format(mesa_format format);

extern GLenum
_mesa_get_format_color_encoding(mesa_format format);

extern GLuint
_mesa_format_image_size(mesa_format format, GLsizei width,
                        GLsizei height, GLsizei depth);

extern uint64_t
_mesa_format_image_size64(mesa_format format, GLsizei width,
                          GLsizei height, GLsizei depth);

extern GLint
_mesa_format_row_stride(mesa_format format, GLsizei width);

extern void
_mesa_uncompressed_format_to_type_and_comps(mesa_format format,
                               GLenum *datatype, GLuint *comps);

extern void
_mesa_test_formats(void);

extern mesa_format
_mesa_get_srgb_format_linear(mesa_format format);

extern mesa_format
_mesa_get_uncompressed_format(mesa_format format);

extern GLuint
_mesa_format_num_components(mesa_format format);

extern bool
_mesa_format_has_color_component(mesa_format format, int component);

GLboolean
_mesa_format_matches_format_and_type(mesa_format mesa_format,
				     GLenum format, GLenum type,
				     GLboolean swapBytes, GLenum *error);

#ifdef __cplusplus
}
#endif

#endif /* FORMATS_H */
