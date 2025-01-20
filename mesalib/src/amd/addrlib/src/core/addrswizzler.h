/*
************************************************************************************************************************
*
*  Copyright (C) 2024 Advanced Micro Devices, Inc.  All rights reserved.
*
***********************************************************************************************************************/
/**
****************************************************************************************************
* @file  addrswizzler.cpp
* @brief Contains code for efficient CPU swizzling.
****************************************************************************************************
*/
#ifndef __ADDR_SWIZZLER_H__
#define __ADDR_SWIZZLER_H__

#include "addrlib.h"
#include "addrcommon.h"

namespace Addr
{

// Forward decl 
class LutAddresser;

typedef void (*UnalignedCopyMemImgFunc)(
    void*               pImgBlockSliceStart,  // Block corresponding to beginning of slice
    void*               pBuf,                 // Pointer to data starting from the copy origin.
    size_t              bufStrideY,           // Stride of each row in pBuf
    UINT_32             imageBlocksY,         // Width of the image slice, in blocks.
    ADDR_COORD2D        origin,               // Absolute origin, in elements
    ADDR_EXTENT2D       extent,               // Size to copy, in elements
    UINT_32             sliceXor,             // Includes pipeBankXor and z XOR
    const LutAddresser& addresser);

// This class calculates and holds up to four lookup tables (x/y/z/s) which can be used to cheaply calculate the
// position of a pixel within a block at the cost of some precomputation and memory usage.
//
// This works for all equations and does something like this:
//    offset = blockAddr ^ XLut[x & xMask] ^ YLut[Y & ymask]...
class LutAddresser
{
public:
    constexpr static UINT_32 MaxLutSize = 2100; // Sized to fit the largest non-VAR LUT size

    LutAddresser();

    void Init(const ADDR_BIT_SETTING* pEq, UINT_32 eqSize, ADDR_EXTENT3D blockSize, UINT_8 blkBits);

    // Does a full calculation to get the offset within a block. Takes an *absolute* coordinate,
    // not the coordinate within the block.
    UINT_32  GetBlockOffset(
        UINT_32 x,
        UINT_32 y,
        UINT_32 z,
        UINT_32 s = 0,
        UINT_32 pipeBankXor = 0)
    {
        return GetAddressX(x) ^ GetAddressY(y) ^ GetAddressZ(z) ^ GetAddressS(s) ^ pipeBankXor;
    }

    // Get the block size
    UINT_32  GetBlockBits() const { return m_blockBits; }
    UINT_32  GetBlockXBits() const { return Log2(m_blockSize.width); }
    UINT_32  GetBlockYBits() const { return Log2(m_blockSize.height); }
    UINT_32  GetBlockZBits() const { return Log2(m_blockSize.depth); }

    // "Fast single channel" functions to get the part that each channel contributes to be XORd together.
    UINT_32  GetAddressX(UINT_32  x) const { return m_pXLut[x & m_xLutMask];}
    UINT_32  GetAddressY(UINT_32  y) const { return m_pYLut[y & m_yLutMask];}
    UINT_32  GetAddressZ(UINT_32  z) const { return m_pZLut[z & m_zLutMask];}
    UINT_32  GetAddressS(UINT_32  s) const { return m_pSLut[s & m_sLutMask];}

    // Get a function that can copy a single 2D slice of an image with this swizzle.
    UnalignedCopyMemImgFunc GetCopyMemImgFunc() const;
    UnalignedCopyMemImgFunc GetCopyImgMemFunc() const;
private:
    // Calculate general properties of the swizzle equations
    void InitSwizzleProps();
    // Fills a LUT for each channel.
    void InitLuts();
    // Evaluate coordinate without LUTs
    UINT_32 EvalEquation(UINT_32 x, UINT_32 y, UINT_32 z, UINT_32 s);

    // Pointers within m_lutData corresponding to where each LUT starts
    // m_lutData[0] always has a value of 0 and thus can be considered an empty 1-entry LUT for "don't care" channels
    UINT_32* m_pXLut;
    UINT_32* m_pYLut;
    UINT_32* m_pZLut;
    UINT_32* m_pSLut;

    // Size of each LUT, minus 1 to form a mask. A mask of 0 is valid for an empty LUT.
    UINT_32 m_xLutMask;
    UINT_32 m_yLutMask;
    UINT_32 m_zLutMask;
    UINT_32 m_sLutMask;

    // Number of bits in the block (aka Log2(blkSize))
    UINT_32  m_blockBits;

    // The block size
    ADDR_EXTENT3D m_blockSize;

    // Number of 'x' bits at the bottom of the equation. Must be a pow2 and at least 1.
    // This will be used as a simple optimization to batch together operations on adjacent x pixels.
    UINT_32  m_maxExpandX;

    // BPE for this equation.
    UINT_32  m_bpeLog2;

    // The full equation
    ADDR_BIT_SETTING m_bit[ADDR_MAX_EQUATION_BIT];

    // Backing store for the LUT tables.
    UINT_32 m_lutData[MaxLutSize];
};

}

#endif // __ADDR_SWIZZLER_H__