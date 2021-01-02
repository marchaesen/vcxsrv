/****************************************************************************
* Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
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
* @file tilingtraits.h
* 
* @brief Tiling traits.
* 
******************************************************************************/
#pragma once

#include "core/state.h"
#include "common/intrin.h"

template<SWR_TILE_MODE mode, int>
struct TilingTraits
{
    static const SWR_TILE_MODE TileMode{ mode };
    static UINT GetCu() { SWR_NOT_IMPL; return 0; }
    static UINT GetCv() { SWR_NOT_IMPL; return 0; }
    static UINT GetCr() { SWR_NOT_IMPL; return 0; }
    static UINT GetTileIDShift() { SWR_NOT_IMPL; return 0; }

    /// @todo correct pdep shifts for all rastertile dims.  Unused for now
    static UINT GetPdepX() { SWR_NOT_IMPL; return 0x37; }
    static UINT GetPdepY() { SWR_NOT_IMPL; return 0xC8; }
};

template<int X> struct TilingTraits <SWR_TILE_NONE, X>
{
    static const SWR_TILE_MODE TileMode{ SWR_TILE_NONE };
    static UINT GetCu() { return 0; }
    static UINT GetCv() { return 0; }
    static UINT GetCr() { return 0; }
    static UINT GetTileIDShift() { return 0; }
    static UINT GetPdepX() { return 0x00; }
    static UINT GetPdepY() { return 0x00; }
};

template<> struct TilingTraits <SWR_TILE_SWRZ, 8>
{
    static const SWR_TILE_MODE TileMode{ SWR_TILE_SWRZ };
    static UINT GetCu() { return KNOB_TILE_X_DIM_SHIFT; }
    static UINT GetCv() { return KNOB_TILE_Y_DIM_SHIFT; }
    static UINT GetCr() { return 0; }
    static UINT GetTileIDShift() { return KNOB_TILE_X_DIM_SHIFT + KNOB_TILE_Y_DIM_SHIFT; }

    /// @todo correct pdep shifts for all rastertile dims.  Unused for now
    static UINT GetPdepX() { SWR_NOT_IMPL; return 0x00; }
    static UINT GetPdepY() { SWR_NOT_IMPL; return 0x00; }
};

template<> struct TilingTraits <SWR_TILE_SWRZ, 32>
{
    static const SWR_TILE_MODE TileMode{ SWR_TILE_SWRZ };
    static UINT GetCu() { return KNOB_TILE_X_DIM_SHIFT + 2; }
    static UINT GetCv() { return KNOB_TILE_Y_DIM_SHIFT; }
    static UINT GetCr() { return 0; }
    static UINT GetTileIDShift() { return KNOB_TILE_X_DIM_SHIFT + KNOB_TILE_Y_DIM_SHIFT + 2; }

    static UINT GetPdepX() { return 0x37; }
    static UINT GetPdepY() { return 0xC8; }
};

template<> struct TilingTraits <SWR_TILE_SWRZ, 128>
{
    static const SWR_TILE_MODE TileMode{ SWR_TILE_SWRZ };
    static UINT GetCu() { return KNOB_TILE_X_DIM_SHIFT + 4; }
    static UINT GetCv() { return KNOB_TILE_Y_DIM_SHIFT; }
    static UINT GetCr() { return 0; }
    static UINT GetTileIDShift() { return KNOB_TILE_X_DIM_SHIFT + KNOB_TILE_Y_DIM_SHIFT + 4; }

    /// @todo correct pdep shifts for all rastertile dims.  Unused for now
    static UINT GetPdepX() { SWR_NOT_IMPL; return 0x37; }
    static UINT GetPdepY() { SWR_NOT_IMPL; return 0xC8; }
};

// y-major tiling layout unaffected by element size
template<int X> struct TilingTraits <SWR_TILE_MODE_YMAJOR, X>
{
    static const SWR_TILE_MODE TileMode{ SWR_TILE_MODE_YMAJOR };
    static UINT GetCu() { return 7; }
    static UINT GetCv() { return 5; }
    static UINT GetCr() { return 0; }
    static UINT GetTileIDShift() { return 12; }

    static UINT GetPdepX() { return 0xe0f; }
    static UINT GetPdepY() { return 0x1f0; }
};

// x-major tiling layout unaffected by element size
template<int X> struct TilingTraits <SWR_TILE_MODE_XMAJOR, X>
{
    static const SWR_TILE_MODE TileMode{ SWR_TILE_MODE_XMAJOR };
    static UINT GetCu() { return 9; }
    static UINT GetCv() { return 3; }
    static UINT GetCr() { return 0; }
    static UINT GetTileIDShift() { return 12; }

    static UINT GetPdepX() { return 0x1ff; }
    static UINT GetPdepY() { return 0xe00; }
};

template<int X> struct TilingTraits <SWR_TILE_MODE_WMAJOR, X>
{
    static const SWR_TILE_MODE TileMode{ SWR_TILE_MODE_WMAJOR };
    static UINT GetCu() { return 6; }
    static UINT GetCv() { return 6; }
    static UINT GetCr() { return 0; }
    static UINT GetTileIDShift() { return 12; }

    static UINT GetPdepX() { return 0xe15; }
    static UINT GetPdepY() { return 0x1ea; }
};

//////////////////////////////////////////////////////////////////////////
/// @brief Computes the tileID for 2D tiled surfaces
/// @param pitch - surface pitch in bytes
/// @param tileX - x offset in tiles
/// @param tileY - y offset in tiles
template<typename TTraits>
INLINE UINT ComputeTileOffset2D(UINT pitch, UINT tileX, UINT tileY)
{
    UINT tileID = tileY * (pitch >> TTraits::GetCu()) + tileX;
    return tileID << TTraits::GetTileIDShift();
}

//////////////////////////////////////////////////////////////////////////
/// @brief Computes the tileID for 3D tiled surfaces
/// @param qpitch - surface qpitch in rows
/// @param pitch - surface pitch in bytes
/// @param tileX - x offset in tiles
/// @param tileY - y offset in tiles
/// @param tileZ - y offset in tiles
template<typename TTraits>
INLINE UINT ComputeTileOffset3D(UINT qpitch, UINT pitch, UINT tileX, UINT tileY, UINT tileZ)
{
    UINT tileID = (tileZ * (qpitch >> TTraits::GetCv()) + tileY) * (pitch >> TTraits::GetCu()) + tileX;
    return tileID << TTraits::GetTileIDShift();
}

//////////////////////////////////////////////////////////////////////////
/// @brief Computes the byte offset for 2D tiled surfaces
/// @param pitch - surface pitch in bytes
/// @param x - x offset in bytes
/// @param y - y offset in rows
template<typename TTraits>
INLINE UINT ComputeOffset2D(UINT pitch, UINT x, UINT y)
{
    UINT tileID = ComputeTileOffset2D<TTraits>(pitch, x >> TTraits::GetCu(), y >> TTraits::GetCv());
    UINT xSwizzle = pdep_u32(x, TTraits::GetPdepX());
    UINT ySwizzle = pdep_u32(y, TTraits::GetPdepY());
    return (tileID | xSwizzle | ySwizzle);
}

#if KNOB_ARCH <= KNOB_ARCH_AVX
//////////////////////////////////////////////////////////////////////////
/// @brief Computes the byte offset for 2D tiled surfaces. Specialization
///        for tile-y surfaces that uses bit twiddling instead of pdep emulation.
/// @param pitch - surface pitch in bytes
/// @param x - x offset in bytes
/// @param y - y offset in rows
template<>
INLINE UINT ComputeOffset2D<TilingTraits<SWR_TILE_MODE_YMAJOR, 32> >(UINT pitch, UINT x, UINT y)
{
    typedef TilingTraits<SWR_TILE_MODE_YMAJOR, 32> TTraits;

    UINT tileID = ComputeTileOffset2D<TTraits>(pitch, x >> TTraits::GetCu(), y >> TTraits::GetCv());
    UINT xSwizzle = ((x << 5) & 0xe00) | (x & 0xf);
    UINT ySwizzle = (y << 4) & 0x1f0;
    return (tileID | xSwizzle | ySwizzle);
}
#endif

//////////////////////////////////////////////////////////////////////////
/// @brief Computes the byte offset for 3D tiled surfaces
/// @param qpitch - depth pitch in rows
/// @param pitch - surface pitch in bytes
/// @param x - x offset in bytes
/// @param y - y offset in rows
/// @param z - y offset in slices
template<typename TTraits>
INLINE UINT ComputeOffset3D(UINT qpitch, UINT pitch, UINT x, UINT y, UINT z)
{
    UINT tileID = ComputeTileOffset3D<TTraits>(qpitch, pitch, x >> TTraits::GetCu(), y >> TTraits::GetCv(), z >> TTraits::GetCr());
    UINT xSwizzle = pdep_u32(x, TTraits::GetPdepX());
    UINT ySwizzle = pdep_u32(y, TTraits::GetPdepY());
    return (tileID | xSwizzle | ySwizzle);
}
