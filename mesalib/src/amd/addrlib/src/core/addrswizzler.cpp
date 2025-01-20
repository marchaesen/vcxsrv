
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

#include "addrswizzler.h"

namespace Addr
{

/**
****************************************************************************************************
*   LutAddresser::LutAddresser
*
*   @brief
*       Constructor for the LutAddresser class.
****************************************************************************************************
*/
LutAddresser::LutAddresser()
    :
    m_pXLut(&m_lutData[0]),
    m_pYLut(&m_lutData[0]),
    m_pZLut(&m_lutData[0]),
    m_pSLut(&m_lutData[0]),
    m_xLutMask(0),
    m_yLutMask(0),
    m_zLutMask(0),
    m_sLutMask(0),
    m_blockBits(0),
    m_blockSize(),
    m_bpeLog2(0),
    m_bit(),
    m_lutData()
{
}

/**
****************************************************************************************************
*   LutAddresser::Init
*
*   @brief
*       Calculates general properties about the swizzle
****************************************************************************************************
*/
void LutAddresser::Init(
    const ADDR_BIT_SETTING* pEq,
    UINT_32                 eqSize,
    ADDR_EXTENT3D           blockSize,
    UINT_8                  blockBits)
{
    ADDR_ASSERT(eqSize <= ADDR_MAX_EQUATION_BIT);
    memcpy(&m_bit[0], pEq, sizeof(ADDR_BIT_SETTING) * eqSize);
    m_blockSize = blockSize;
    m_blockBits = blockBits;

    InitSwizzleProps();
    InitLuts();
}

/**
****************************************************************************************************
*   LutAddresser::InitSwizzleProps
*
*   @brief
*       Calculates general properties about the swizzle
****************************************************************************************************
*/
void LutAddresser::InitSwizzleProps()
{
    // Calculate BPE from the swizzle. This can be derived from the number of invalid low bits.
    m_bpeLog2 = 0;
    for (UINT_32 i = 0; i < MaxElementBytesLog2; i++)
    {
        if (m_bit[i].value != 0)
        {
            break;
        }
        m_bpeLog2++;
    }

    // Generate a mask/size for each channel's LUT. This may be larger than the block size.
    // If a given 'source' bit (eg. 'x0') is used for any part of the equation, fill that in the mask.
    for (UINT_32 i = 0; i < ADDR_MAX_EQUATION_BIT; i++)
    {
        m_xLutMask |= m_bit[i].x;
        m_yLutMask |= m_bit[i].y;
        m_zLutMask |= m_bit[i].z;
        m_sLutMask |= m_bit[i].s;
    }

    // An expandX of 1 is a no-op
    m_maxExpandX = 1;
    if (m_sLutMask == 0)
    {
        // Calculate expandX from the swizzle. This can be derived from the number of consecutive,
        // increasing low x bits
        for (UINT_32 i = 0; i < 3; i++)
        {
            const auto& curBit = m_bit[m_bpeLog2 + i];
            ADDR_ASSERT(curBit.value != 0);
            if ((IsPow2(curBit.value) == false) || // More than one bit contributes
                (curBit.x == 0)                 || // Bit is from Y/Z/S channel
                (curBit.x != m_maxExpandX))        // X bits are out of order
            {
                break;
            }
            m_maxExpandX *= 2;
        }
    }
}

/**
****************************************************************************************************
*   LutAddresser::InitLuts
*
*   @brief
*       Creates lookup tables for each channel.
****************************************************************************************************
*/
void LutAddresser::InitLuts()
{
    UINT_32 curOffset = 0;
    m_pXLut = &m_lutData[0];
    for (UINT_32 x = 0; x < (m_xLutMask + 1); x++)
    {
        m_pXLut[x] = EvalEquation(x, 0, 0, 0);
    }
    curOffset += m_xLutMask + 1;
    ADDR_ASSERT(curOffset <= MaxLutSize);

    if (m_yLutMask != 0)
    {
        m_pYLut = &m_lutData[curOffset];
        for (UINT_32 y = 0; y < (m_yLutMask + 1); y++)
        {
            m_pYLut[y] = EvalEquation(0, y, 0, 0);
        }
        curOffset += m_yLutMask + 1;
        ADDR_ASSERT(curOffset <= MaxLutSize);
    }
    else
    {
        m_pYLut = &m_lutData[0];
        ADDR_ASSERT(m_pYLut[0] == 0);
    }
    
    if (m_zLutMask != 0)
    {
        m_pZLut = &m_lutData[curOffset];
        for (UINT_32 z = 0; z < (m_zLutMask + 1); z++)
        {
            m_pZLut[z] = EvalEquation(0, 0, z, 0);
        }
        curOffset += m_zLutMask + 1;
        ADDR_ASSERT(curOffset <= MaxLutSize);
    }
    else
    {
        m_pZLut = &m_lutData[0];
        ADDR_ASSERT(m_pZLut[0] == 0);
    }

    if (m_sLutMask != 0)
    {
        m_pSLut = &m_lutData[curOffset];
        for (UINT_32 s = 0; s < (m_sLutMask + 1); s++)
        {
            m_pSLut[s] = EvalEquation(0, 0, 0, s);
        }
        curOffset += m_sLutMask + 1;
        ADDR_ASSERT(curOffset <= MaxLutSize);
    }
    else
    {
        m_pSLut = &m_lutData[0];
        ADDR_ASSERT(m_pSLut[0] == 0);
    }
}

/**
****************************************************************************************************
*   LutAddresser::EvalEquation
*
*   @brief
*       Evaluates the equation at a given coordinate manually.
****************************************************************************************************
*/
UINT_32 LutAddresser::EvalEquation(
    UINT_32 x,
    UINT_32 y,
    UINT_32 z,
    UINT_32 s)
{
    UINT_32 out = 0;

    for (UINT_32 i = 0; i < ADDR_MAX_EQUATION_BIT; i++)
    {
        if (m_bit[i].value == 0)
        {
            if (out != 0)
            {
                // Invalid bits at the top of the equation
                break;
            }
            else
            {
                continue;
            }
        }

        if (x != 0)
        {
            UINT_32 xSrcs = m_bit[i].x;
            while (xSrcs != 0)
            {
                UINT_32 xIdx = BitScanForward(xSrcs);
                out ^= (((x >> xIdx) & 1) << i);
                xSrcs = UnsetLeastBit(xSrcs);
            }
        }

        if (y != 0)
        {
            UINT_32 ySrcs = m_bit[i].y;
            while (ySrcs != 0)
            {
                UINT_32 yIdx = BitScanForward(ySrcs);
                out ^= (((y >> yIdx) & 1) << i);
                ySrcs = UnsetLeastBit(ySrcs);
            }
        }

        if (z != 0)
        {
            UINT_32 zSrcs = m_bit[i].z;
            while (zSrcs != 0)
            {
                UINT_32 zIdx = BitScanForward(zSrcs);
                out ^= (((z >> zIdx) & 1) << i);
                zSrcs = UnsetLeastBit(zSrcs);
            }
        }

        if (s != 0)
        {
            UINT_32 sSrcs = m_bit[i].s;
            while (sSrcs != 0)
            {
                UINT_32 sIdx = BitScanForward(sSrcs);
                out ^= (((s >> sIdx) & 1) << i);
                sSrcs = UnsetLeastBit(sSrcs);
            }
        }
    }

    return out;
}


/**
****************************************************************************************************
*   Copy2DSliceUnaligned
*
*   @brief
*       Copies an arbitrary 2D pixel region to or from a surface.
****************************************************************************************************
*/
template <int BPELog2, int ExpandX, bool ImgIsDest>
void Copy2DSliceUnaligned(
    void*               pImgBlockSliceStart, // Block corresponding to beginning of slice
    void*               pBuf,                // Pointer to data starting from the copy origin.
    size_t              bufStrideY,          // Stride of each row in pBuf
    UINT_32             imageBlocksY,        // Width of the image slice, in blocks.
    ADDR_COORD2D        origin,              // Absolute origin, in elements
    ADDR_EXTENT2D       extent,              // Size to copy, in elements
    UINT_32             sliceXor,            // Includes pipeBankXor and z XOR
    const LutAddresser& addresser)
{
    UINT_32  xStart = origin.x;
    UINT_32  xEnd   = origin.x + extent.width;

    constexpr UINT_32  PixBytes = (1 << BPELog2);

    // Apply a negative offset now so later code can do eg. pBuf[x] instead of pBuf[x - origin.x]
    pBuf = VoidPtrDec(pBuf, xStart * PixBytes);

    // Do things one row at a time for unaligned regions.
    for (UINT_32 y = origin.y; y < (origin.y + extent.height); y++)
    {
        UINT_32 yBlk = (y >> addresser.GetBlockYBits()) * imageBlocksY;
        UINT_32 rowXor = sliceXor ^ addresser.GetAddressY(y);

        UINT_32 x = xStart;

        // Most swizzles pack 2-4 pixels horizontally. Take advantage of this even in non-microblock-aligned
        // regions to commonly do 2-4x less work. This is still way less good than copying by whole microblocks though.
        if (ExpandX > 1)
        {
            // Unaligned left edge
            for (; x < Min(xEnd, PowTwoAlign(xStart, ExpandX)); x++)
            {
                UINT_32 blk = (yBlk + (x >> addresser.GetBlockXBits()));
                void* pImgBlock = VoidPtrInc(pImgBlockSliceStart, blk << addresser.GetBlockBits());
                void* pPix = VoidPtrInc(pImgBlock, rowXor ^ addresser.GetAddressX(x));
                if (ImgIsDest)
                {
                    memcpy(pPix, VoidPtrInc(pBuf, x * PixBytes), PixBytes);
                }
                else
                {
                    memcpy(VoidPtrInc(pBuf, x * PixBytes), pPix, PixBytes);
                }
            }
            // Aligned middle
            for (; x < PowTwoAlignDown(xEnd, ExpandX); x += ExpandX)
            {
                UINT_32 blk = (yBlk + (x >> addresser.GetBlockXBits()));
                void* pImgBlock = VoidPtrInc(pImgBlockSliceStart, blk << addresser.GetBlockBits());
                void* pPix = VoidPtrInc(pImgBlock, rowXor ^ addresser.GetAddressX(x));
                if (ImgIsDest)
                {
                    memcpy(pPix, VoidPtrInc(pBuf, x * PixBytes), PixBytes * ExpandX);
                }
                else
                {
                    memcpy(VoidPtrInc(pBuf, x * PixBytes), pPix, PixBytes * ExpandX);
                }
            }
        }
        // Unaligned end (or the whole thing when ExpandX == 1)
        for (; x < xEnd; x++)
        {
            // Get the index of the block within the slice
            UINT_32 blk = (yBlk + (x >> addresser.GetBlockXBits()));
            // Apply that index to get the base address of the current block. 
            void* pImgBlock = VoidPtrInc(pImgBlockSliceStart, blk << addresser.GetBlockBits());
            // Grab the x-xor and XOR it all together, adding to get the final address
            void* pPix = VoidPtrInc(pImgBlock, rowXor ^ addresser.GetAddressX(x));
            if (ImgIsDest)
            {
                memcpy(pPix, VoidPtrInc(pBuf, x * PixBytes), PixBytes);
            }
            else
            {
                memcpy(VoidPtrInc(pBuf, x * PixBytes), pPix, PixBytes);
            }
        }

        pBuf = VoidPtrInc(pBuf, bufStrideY);
    }
}

/**
****************************************************************************************************
*   LutAddresser::GetCopyMemImgFunc
*
*   @brief
*       Determines and returns which copy function to use for copying to images
****************************************************************************************************
*/
UnalignedCopyMemImgFunc LutAddresser::GetCopyMemImgFunc() const
{
    // While these are all the same function, the codegen gets really bad if the size of each pixel
    // is not known at compile time. Hence, templates.
    const UnalignedCopyMemImgFunc Funcs[MaxElementBytesLog2][3] =
    {
        // ExpandX =  1, 2, 4
        { Copy2DSliceUnaligned<0, 1, true>, Copy2DSliceUnaligned<0, 2, true>, Copy2DSliceUnaligned<0, 4, true> }, // 1BPE
        { Copy2DSliceUnaligned<1, 1, true>, Copy2DSliceUnaligned<1, 2, true>, Copy2DSliceUnaligned<1, 4, true> }, // 2BPE
        { Copy2DSliceUnaligned<2, 1, true>, Copy2DSliceUnaligned<2, 2, true>, Copy2DSliceUnaligned<2, 4, true> }, // 4BPE
        { Copy2DSliceUnaligned<3, 1, true>, Copy2DSliceUnaligned<3, 2, true>, Copy2DSliceUnaligned<3, 4, true> }, // 8BPE
        { Copy2DSliceUnaligned<4, 1, true>, Copy2DSliceUnaligned<4, 2, true>, Copy2DSliceUnaligned<4, 4, true> }, // 16BPE
    };

    UnalignedCopyMemImgFunc pfnRet = nullptr;
    ADDR_ASSERT(m_bpeLog2 < MaxElementBytesLog2);
    if (m_maxExpandX >= 4)
    {
        pfnRet = Funcs[m_bpeLog2][2];
    }
    else if (m_maxExpandX >= 2)
    {
        pfnRet = Funcs[m_bpeLog2][1];
    }
    else
    {
        pfnRet = Funcs[m_bpeLog2][0];
    }
    return pfnRet;
}

/**
****************************************************************************************************
*   LutAddresser::GetCopyImgMemFunc
*
*   @brief
*       Determines and returns which copy function to use for copying from images
****************************************************************************************************
*/
UnalignedCopyMemImgFunc LutAddresser::GetCopyImgMemFunc() const
{
    // While these are all the same function, the codegen gets really bad if the size of each pixel
    // is not known at compile time. Hence, templates.
    const UnalignedCopyMemImgFunc Funcs[MaxElementBytesLog2][3] =
    {
        // ExpandX =  1, 2, 4
        { Copy2DSliceUnaligned<0, 1, false>, Copy2DSliceUnaligned<0, 2, false>, Copy2DSliceUnaligned<0, 4, false> }, // 1BPE
        { Copy2DSliceUnaligned<1, 1, false>, Copy2DSliceUnaligned<1, 2, false>, Copy2DSliceUnaligned<1, 4, false> }, // 2BPE
        { Copy2DSliceUnaligned<2, 1, false>, Copy2DSliceUnaligned<2, 2, false>, Copy2DSliceUnaligned<2, 4, false> }, // 4BPE
        { Copy2DSliceUnaligned<3, 1, false>, Copy2DSliceUnaligned<3, 2, false>, Copy2DSliceUnaligned<3, 4, false> }, // 8BPE
        { Copy2DSliceUnaligned<4, 1, false>, Copy2DSliceUnaligned<4, 2, false>, Copy2DSliceUnaligned<4, 4, false> }, // 16BPE
    };

    UnalignedCopyMemImgFunc pfnRet = nullptr;
    ADDR_ASSERT(m_bpeLog2 < MaxElementBytesLog2);
    if (m_maxExpandX >= 4)
    {
        pfnRet = Funcs[m_bpeLog2][2];
    }
    else if (m_maxExpandX >= 2)
    {
        pfnRet = Funcs[m_bpeLog2][1];
    }
    else
    {
        pfnRet = Funcs[m_bpeLog2][0];
    }
    return pfnRet;
}

}
