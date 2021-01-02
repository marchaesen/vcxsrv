/****************************************************************************
 * Copyright (C) 2016 Intel Corporation.   All Rights Reserved.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * @file formats.h
 *
 * @brief auto-generated file
 *
 * DO NOT EDIT
 *
 ******************************************************************************/

#pragma once

#include "common/os.h"

//////////////////////////////////////////////////////////////////////////
/// SWR_TYPE - Format component type
//////////////////////////////////////////////////////////////////////////
enum SWR_TYPE
{
    SWR_TYPE_UNKNOWN,
    SWR_TYPE_UNUSED,
    SWR_TYPE_UNORM,
    SWR_TYPE_SNORM,
    SWR_TYPE_UINT,
    SWR_TYPE_SINT,
    SWR_TYPE_FLOAT,
    SWR_TYPE_SSCALED,
    SWR_TYPE_USCALED,
    SWR_TYPE_SFIXED,
};

//////////////////////////////////////////////////////////////////////////
/// SWR_FORMAT
//////////////////////////////////////////////////////////////////////////
enum SWR_FORMAT
{
    R32G32B32A32_FLOAT       = 0x0,
    R32G32B32A32_SINT        = 0x1,
    R32G32B32A32_UINT        = 0x2,
    R64G64_FLOAT             = 0x5,
    R32G32B32X32_FLOAT       = 0x6,
    R32G32B32A32_SSCALED     = 0x7,
    R32G32B32A32_USCALED     = 0x8,
    R32G32B32A32_SFIXED      = 0x20,
    R32G32B32_FLOAT          = 0x40,
    R32G32B32_SINT           = 0x41,
    R32G32B32_UINT           = 0x42,
    R32G32B32_SSCALED        = 0x45,
    R32G32B32_USCALED        = 0x46,
    R32G32B32_SFIXED         = 0x50,
    R16G16B16A16_UNORM       = 0x80,
    R16G16B16A16_SNORM       = 0x81,
    R16G16B16A16_SINT        = 0x82,
    R16G16B16A16_UINT        = 0x83,
    R16G16B16A16_FLOAT       = 0x84,
    R32G32_FLOAT             = 0x85,
    R32G32_SINT              = 0x86,
    R32G32_UINT              = 0x87,
    R32_FLOAT_X8X24_TYPELESS = 0x88,
    X32_TYPELESS_G8X24_UINT  = 0x89,
    L32A32_FLOAT             = 0x8A,
    R64_FLOAT                = 0x8D,
    R16G16B16X16_UNORM       = 0x8E,
    R16G16B16X16_FLOAT       = 0x8F,
    L32X32_FLOAT             = 0x91,
    I32X32_FLOAT             = 0x92,
    R16G16B16A16_SSCALED     = 0x93,
    R16G16B16A16_USCALED     = 0x94,
    R32G32_SSCALED           = 0x95,
    R32G32_USCALED           = 0x96,
    R32G32_SFIXED            = 0xA0,
    B8G8R8A8_UNORM           = 0xC0,
    B8G8R8A8_UNORM_SRGB      = 0xC1,
    R10G10B10A2_UNORM        = 0xC2,
    R10G10B10A2_UNORM_SRGB   = 0xC3,
    R10G10B10A2_UINT         = 0xC4,
    R8G8B8A8_UNORM           = 0xC7,
    R8G8B8A8_UNORM_SRGB      = 0xC8,
    R8G8B8A8_SNORM           = 0xC9,
    R8G8B8A8_SINT            = 0xCA,
    R8G8B8A8_UINT            = 0xCB,
    R16G16_UNORM             = 0xCC,
    R16G16_SNORM             = 0xCD,
    R16G16_SINT              = 0xCE,
    R16G16_UINT              = 0xCF,
    R16G16_FLOAT             = 0xD0,
    B10G10R10A2_UNORM        = 0xD1,
    B10G10R10A2_UNORM_SRGB   = 0xD2,
    R11G11B10_FLOAT          = 0xD3,
    R10G10B10_FLOAT_A2_UNORM = 0xD5,
    R32_SINT                 = 0xD6,
    R32_UINT                 = 0xD7,
    R32_FLOAT                = 0xD8,
    R24_UNORM_X8_TYPELESS    = 0xD9,
    X24_TYPELESS_G8_UINT     = 0xDA,
    L32_UNORM                = 0xDD,
    L16A16_UNORM             = 0xDF,
    I24X8_UNORM              = 0xE0,
    L24X8_UNORM              = 0xE1,
    I32_FLOAT                = 0xE3,
    L32_FLOAT                = 0xE4,
    A32_FLOAT                = 0xE5,
    B8G8R8X8_UNORM           = 0xE9,
    B8G8R8X8_UNORM_SRGB      = 0xEA,
    R8G8B8X8_UNORM           = 0xEB,
    R8G8B8X8_UNORM_SRGB      = 0xEC,
    R9G9B9E5_SHAREDEXP       = 0xED,
    B10G10R10X2_UNORM        = 0xEE,
    L16A16_FLOAT             = 0xF0,
    R10G10B10X2_USCALED      = 0xF3,
    R8G8B8A8_SSCALED         = 0xF4,
    R8G8B8A8_USCALED         = 0xF5,
    R16G16_SSCALED           = 0xF6,
    R16G16_USCALED           = 0xF7,
    R32_SSCALED              = 0xF8,
    R32_USCALED              = 0xF9,
    B5G6R5_UNORM             = 0x100,
    B5G6R5_UNORM_SRGB        = 0x101,
    B5G5R5A1_UNORM           = 0x102,
    B5G5R5A1_UNORM_SRGB      = 0x103,
    B4G4R4A4_UNORM           = 0x104,
    B4G4R4A4_UNORM_SRGB      = 0x105,
    R8G8_UNORM               = 0x106,
    R8G8_SNORM               = 0x107,
    R8G8_SINT                = 0x108,
    R8G8_UINT                = 0x109,
    R16_UNORM                = 0x10A,
    R16_SNORM                = 0x10B,
    R16_SINT                 = 0x10C,
    R16_UINT                 = 0x10D,
    R16_FLOAT                = 0x10E,
    I16_UNORM                = 0x111,
    L16_UNORM                = 0x112,
    A16_UNORM                = 0x113,
    L8A8_UNORM               = 0x114,
    I16_FLOAT                = 0x115,
    L16_FLOAT                = 0x116,
    A16_FLOAT                = 0x117,
    L8A8_UNORM_SRGB          = 0x118,
    B5G5R5X1_UNORM           = 0x11A,
    B5G5R5X1_UNORM_SRGB      = 0x11B,
    R8G8_SSCALED             = 0x11C,
    R8G8_USCALED             = 0x11D,
    R16_SSCALED              = 0x11E,
    R16_USCALED              = 0x11F,
    A1B5G5R5_UNORM           = 0x124,
    A4B4G4R4_UNORM           = 0x125,
    L8A8_UINT                = 0x126,
    L8A8_SINT                = 0x127,
    R8_UNORM                 = 0x140,
    R8_SNORM                 = 0x141,
    R8_SINT                  = 0x142,
    R8_UINT                  = 0x143,
    A8_UNORM                 = 0x144,
    I8_UNORM                 = 0x145,
    L8_UNORM                 = 0x146,
    R8_SSCALED               = 0x149,
    R8_USCALED               = 0x14A,
    L8_UNORM_SRGB            = 0x14C,
    L8_UINT                  = 0x152,
    L8_SINT                  = 0x153,
    I8_UINT                  = 0x154,
    I8_SINT                  = 0x155,
    DXT1_RGB_SRGB            = 0x180,
    YCRCB_SWAPUVY            = 0x183,
    BC1_UNORM                = 0x186,
    BC2_UNORM                = 0x187,
    BC3_UNORM                = 0x188,
    BC4_UNORM                = 0x189,
    BC5_UNORM                = 0x18A,
    BC1_UNORM_SRGB           = 0x18B,
    BC2_UNORM_SRGB           = 0x18C,
    BC3_UNORM_SRGB           = 0x18D,
    YCRCB_SWAPUV             = 0x18F,
    DXT1_RGB                 = 0x191,
    R8G8B8_UNORM             = 0x193,
    R8G8B8_SNORM             = 0x194,
    R8G8B8_SSCALED           = 0x195,
    R8G8B8_USCALED           = 0x196,
    R64G64B64A64_FLOAT       = 0x197,
    R64G64B64_FLOAT          = 0x198,
    BC4_SNORM                = 0x199,
    BC5_SNORM                = 0x19A,
    R16G16B16_FLOAT          = 0x19B,
    R16G16B16_UNORM          = 0x19C,
    R16G16B16_SNORM          = 0x19D,
    R16G16B16_SSCALED        = 0x19E,
    R16G16B16_USCALED        = 0x19F,
    BC6H_SF16                = 0x1A1,
    BC7_UNORM                = 0x1A2,
    BC7_UNORM_SRGB           = 0x1A3,
    BC6H_UF16                = 0x1A4,
    R8G8B8_UNORM_SRGB        = 0x1A8,
    R16G16B16_UINT           = 0x1B0,
    R16G16B16_SINT           = 0x1B1,
    R32_SFIXED               = 0x1B2,
    R10G10B10A2_SNORM        = 0x1B3,
    R10G10B10A2_USCALED      = 0x1B4,
    R10G10B10A2_SSCALED      = 0x1B5,
    R10G10B10A2_SINT         = 0x1B6,
    B10G10R10A2_SNORM        = 0x1B7,
    B10G10R10A2_USCALED      = 0x1B8,
    B10G10R10A2_SSCALED      = 0x1B9,
    B10G10R10A2_UINT         = 0x1BA,
    B10G10R10A2_SINT         = 0x1BB,
    R8G8B8_UINT              = 0x1C8,
    R8G8B8_SINT              = 0x1C9,
    RAW                      = 0x1FF,
    NUM_SWR_FORMATS          = 0x200,
};

//////////////////////////////////////////////////////////////////////////
/// SWR_FORMAT_INFO - Format information
//////////////////////////////////////////////////////////////////////////
struct SWR_FORMAT_INFO
{
    const char* name;
    SWR_TYPE    type[4];
    uint32_t    defaults[4];
    uint32_t    swizzle[4]; ///< swizzle per component
    uint32_t    bpc[4];     ///< bits per component
    uint32_t    bpp;        ///< bits per pixel
    uint32_t    Bpp;        ///< bytes per pixel
    uint32_t    numComps;   ///< number of components
    bool        isSRGB;
    bool        isBC;
    bool        isSubsampled;
    bool        isLuminance;
    bool        isNormalized[4];
    float       toFloat[4];
    uint32_t    bcWidth;
    uint32_t    bcHeight;
};

extern const SWR_FORMAT_INFO gFormatInfo[NUM_SWR_FORMATS];

//////////////////////////////////////////////////////////////////////////
/// @brief Retrieves format info struct for given format.
/// @param format - SWR format
INLINE const SWR_FORMAT_INFO& GetFormatInfo(SWR_FORMAT format)
{
    SWR_ASSERT(format < NUM_SWR_FORMATS, "Invalid Surface Format: %d", format);
    SWR_ASSERT(gFormatInfo[format].name != nullptr, "Invalid Surface Format: %d", format);
    return gFormatInfo[format];
}

// lookup table for unorm8 srgb -> float conversion
extern const uint32_t srgb8Table[256];
