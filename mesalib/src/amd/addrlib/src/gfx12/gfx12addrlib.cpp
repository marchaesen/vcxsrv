/*
************************************************************************************************************************
*
*  Copyright (C) 2023 Advanced Micro Devices, Inc.  All rights reserved.
*  SPDX-License-Identifier: MIT
*
***********************************************************************************************************************/

/**
************************************************************************************************************************
* @file  gfx12addrlib.cpp
* @brief Contain the implementation for the Gfx12Lib class.
************************************************************************************************************************
*/

#include "gfx12addrlib.h"
#include "gfx12_gb_reg.h"

#include "amdgpu_asic_addr.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Addr
{
/**
************************************************************************************************************************
*   Gfx12HwlInit
*
*   @brief
*       Creates an Gfx12Lib object.
*
*   @return
*       Returns an Gfx12Lib object pointer.
************************************************************************************************************************
*/
Addr::Lib* Gfx12HwlInit(
    const Client* pClient)
{
    return V3::Gfx12Lib::CreateObj(pClient);
}

namespace V3
{

////////////////////////////////////////////////////////////////////////////////////////////////////
//                               Static Const Member
////////////////////////////////////////////////////////////////////////////////////////////////////
const SwizzleModeFlags Gfx12Lib::SwizzleModeTable[ADDR3_MAX_TYPE] =
{//Linear 2d   3d  256B  4KB  64KB  256KB  Reserved
    {{1,   0,   0,    0,   0,    0,     0,    0}}, // ADDR3_LINEAR
    {{0,   1,   0,    1,   0,    0,     0,    0}}, // ADDR3_256B_2D
    {{0,   1,   0,    0,   1,    0,     0,    0}}, // ADDR3_4KB_2D
    {{0,   1,   0,    0,   0,    1,     0,    0}}, // ADDR3_64KB_2D
    {{0,   1,   0,    0,   0,    0,     1,    0}}, // ADDR3_256KB_2D
    {{0,   0,   1,    0,   1,    0,     0,    0}}, // ADDR3_4KB_3D
    {{0,   0,   1,    0,   0,    1,     0,    0}}, // ADDR3_64KB_3D
    {{0,   0,   1,    0,   0,    0,     1,    0}}, // ADDR3_256KB_3D
};

/**
************************************************************************************************************************
*   Gfx12Lib::Gfx12Lib
*
*   @brief
*       Constructor
*
************************************************************************************************************************
*/
Gfx12Lib::Gfx12Lib(
    const Client* pClient)
    :
    Lib(pClient),
    m_numSwizzleBits(0)
{
    memcpy(m_swizzleModeTable, SwizzleModeTable, sizeof(SwizzleModeTable));
}

/**
************************************************************************************************************************
*   Gfx12Lib::~Gfx12Lib
*
*   @brief
*       Destructor
************************************************************************************************************************
*/
Gfx12Lib::~Gfx12Lib()
{
}

/**
************************************************************************************************************************
*   Gfx12Lib::ConvertSwizzlePatternToEquation
*
*   @brief
*       Convert swizzle pattern to equation.
*
*   @return
*       N/A
************************************************************************************************************************
*/
VOID Gfx12Lib::ConvertSwizzlePatternToEquation(
    UINT_32                elemLog2,  ///< [in] element bytes log2
    Addr3SwizzleMode       swMode,    ///< [in] swizzle mode
    const ADDR_SW_PATINFO* pPatInfo,  ///< [in] swizzle pattern info
    ADDR_EQUATION*         pEquation) ///< [out] equation converted from swizzle pattern
    const
{
    ADDR_BIT_SETTING fullSwizzlePattern[Log2Size256K];
    GetSwizzlePatternFromPatternInfo(pPatInfo, fullSwizzlePattern);

    const ADDR_BIT_SETTING* pSwizzle = fullSwizzlePattern;
    const UINT_32           blockSizeLog2 = GetBlockSizeLog2(swMode, TRUE);

    pEquation->numBits = blockSizeLog2;
    pEquation->stackedDepthSlices = FALSE;

    for (UINT_32 i = 0; i < elemLog2; i++)
    {
        pEquation->addr[i].channel = 0;
        pEquation->addr[i].valid = 1;
        pEquation->addr[i].index = i;
    }

    for (UINT_32 i = elemLog2; i < blockSizeLog2; i++)
    {
        ADDR_ASSERT(IsPow2(pSwizzle[i].value));

        if (pSwizzle[i].x != 0)
        {
            ADDR_ASSERT(IsPow2(static_cast<UINT_32>(pSwizzle[i].x)));

            pEquation->addr[i].channel = 0;
            pEquation->addr[i].valid = 1;
            pEquation->addr[i].index = Log2(pSwizzle[i].x) + elemLog2;
        }
        else if (pSwizzle[i].y != 0)
        {
            ADDR_ASSERT(IsPow2(static_cast<UINT_32>(pSwizzle[i].y)));

            pEquation->addr[i].channel = 1;
            pEquation->addr[i].valid = 1;
            pEquation->addr[i].index = Log2(pSwizzle[i].y);
        }
        else if (pSwizzle[i].z != 0)
        {
            ADDR_ASSERT(IsPow2(static_cast<UINT_32>(pSwizzle[i].z)));

            pEquation->addr[i].channel = 2;
            pEquation->addr[i].valid = 1;
            pEquation->addr[i].index = Log2(pSwizzle[i].z);
        }
        else if (pSwizzle[i].s != 0)
        {
            ADDR_ASSERT(IsPow2(static_cast<UINT_32>(pSwizzle[i].s)));

            pEquation->addr[i].channel = 3;
            pEquation->addr[i].valid = 1;
            pEquation->addr[i].index = Log2(pSwizzle[i].s);
        }
        else
        {
            ADDR_ASSERT_ALWAYS();
        }
    }
}

/**
************************************************************************************************************************
*   Gfx12Lib::InitEquationTable
*
*   @brief
*       Initialize Equation table.
*
*   @return
*       N/A
************************************************************************************************************************
*/
VOID Gfx12Lib::InitEquationTable()
{
    memset(m_equationTable, 0, sizeof(m_equationTable));

    for (UINT_32 swModeIdx = 0; swModeIdx < ADDR3_MAX_TYPE; swModeIdx++)
    {
        const Addr3SwizzleMode swMode = static_cast<Addr3SwizzleMode>(swModeIdx);

        ADDR_ASSERT(IsValidSwMode(swMode));

        if (IsLinear(swMode))
        {
            // Skip linear equation (data table is not useful for 2D/3D images-- only contains x-coordinate bits)
            continue;
        }

        const UINT_32 maxMsaa = Is2dSwizzle(swMode) ? MaxMsaaRateLog2 : 1;

        for (UINT_32 msaaIdx = 0; msaaIdx < maxMsaa; msaaIdx++)
        {
            for (UINT_32 elemLog2 = 0; elemLog2 < MaxElementBytesLog2; elemLog2++)
            {
                UINT_32                equationIndex = ADDR_INVALID_EQUATION_INDEX;
                const ADDR_SW_PATINFO* pPatInfo = GetSwizzlePatternInfo(swMode, elemLog2, 1 << msaaIdx);

                if (pPatInfo != NULL)
                {
                    ADDR_EQUATION equation = {};

                    ConvertSwizzlePatternToEquation(elemLog2, swMode, pPatInfo, &equation);

                    equationIndex = m_numEquations;
                    ADDR_ASSERT(equationIndex < NumSwizzlePatterns);

                    m_equationTable[equationIndex] = equation;
                    m_numEquations++;
                }
                SetEquationTableEntry(swMode, msaaIdx, elemLog2, equationIndex);
            }
        }
    }
}

/**
************************************************************************************************************************
*   Gfx12Lib::InitBlockDimensionTable
*
*   @brief
*       Initialize block dimension table for all swizzle modes + msaa samples + bpp bundles.
*
*   @return
*       N/A
************************************************************************************************************************
*/
VOID Gfx12Lib::InitBlockDimensionTable()
{
    memset(m_blockDimensionTable, 0, sizeof(m_blockDimensionTable));

    ADDR3_COMPUTE_SURFACE_INFO_INPUT surfaceInfo {};


    for (UINT_32 swModeIdx = 0; swModeIdx < ADDR3_MAX_TYPE; swModeIdx++)
    {
        const Addr3SwizzleMode swMode = static_cast<Addr3SwizzleMode>(swModeIdx);
        ADDR_ASSERT(IsValidSwMode(swMode));

        surfaceInfo.swizzleMode = swMode;
        const UINT_32 maxMsaa   = Is2dSwizzle(swMode) ? MaxMsaaRateLog2 : 1;

        for (UINT_32 msaaIdx = 0; msaaIdx < maxMsaa; msaaIdx++)
        {
            surfaceInfo.numSamples = (1u << msaaIdx);
            for (UINT_32 elementBytesLog2 = 0; elementBytesLog2 < MaxElementBytesLog2; elementBytesLog2++)
            {
                surfaceInfo.bpp = (1u << (elementBytesLog2 + 3));
                ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT input{ &surfaceInfo };
                ComputeBlockDimensionForSurf(&input, &m_blockDimensionTable[swModeIdx][msaaIdx][elementBytesLog2]);
            }
        }
    }
}

/**
************************************************************************************************************************
*   Gfx12Lib::GetMipOrigin
*
*   @brief
*       Internal function to calculate origins of the mip levels
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
VOID Gfx12Lib::GetMipOrigin(
     const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,        ///< [in] input structure
     const ADDR_EXTENT3D&                           mipExtentFirstInTail,
     ADDR3_COMPUTE_SURFACE_INFO_OUTPUT*             pOut        ///< [out] output structure
     ) const
{
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pSurfInfo = pIn->pSurfInfo;
    const BOOL_32        is3d             = Is3dSwizzle(pSurfInfo->swizzleMode);
    const UINT_32        bytesPerPixel    = pSurfInfo->bpp >> 3;
    const UINT_32        elementBytesLog2 = Log2(bytesPerPixel);
    const UINT_32        samplesLog2      = Log2(pSurfInfo->numSamples);

    // Calculate the width/height/depth for ADDR3_4KB_3D swizzle mode.
    // Note that only 2D swizzle mode supports MSAA, 1D and 3D all assume no MSAA, i.e., samplesLog2 == 0, so we must
    // pass a 0 instead of samplesLog2 for the following ADDR3_4KB_3D.
    // See implementation of Addrlib3::ComputeBlockDimensionForSurf().
    ADDR_EXTENT3D        pixelDimOfAddr4kb3d = GetBlockDimensionTableEntry(ADDR3_4KB_3D, 0, elementBytesLog2);
    const ADDR_EXTENT3D  tailMaxDim          = GetMipTailDim(pIn, pOut->blockExtent);
    const UINT_32        blockSizeLog2       = GetBlockSizeLog2(pSurfInfo->swizzleMode);

    UINT_32 pitch  = tailMaxDim.width;
    UINT_32 height = tailMaxDim.height;

    UINT_32 depth  = (is3d ? PowTwoAlign(mipExtentFirstInTail.depth, pixelDimOfAddr4kb3d.depth) : 1);

    const UINT_32 tailMaxDepth   = (is3d ? (depth / pixelDimOfAddr4kb3d.depth) : 1);

    // Calculate the width/height/depth for ADDR3_256B_2D swizzle mode.
    ADDR_EXTENT3D blockDimOfAddr256b2d = GetBlockDimensionTableEntry(ADDR3_256B_2D, samplesLog2, elementBytesLog2);

    for (UINT_32 i = pOut->firstMipIdInTail; i < pSurfInfo->numMipLevels; i++)
    {
        const INT_32  mipInTail = CalcMipInTail(pIn, pOut, i);
        const UINT_32 mipOffset = CalcMipOffset(pIn, mipInTail);

        pOut->pMipInfo[i].offset           = mipOffset * tailMaxDepth;
        pOut->pMipInfo[i].mipTailOffset    = mipOffset;
        pOut->pMipInfo[i].macroBlockOffset = 0;

        pOut->pMipInfo[i].pitch  = pitch;
        pOut->pMipInfo[i].height = height;
        pOut->pMipInfo[i].depth  = depth;
        if (IsLinear(pSurfInfo->swizzleMode))
        {
            pOut->pMipInfo[i].mipTailCoordX = mipOffset >> 8;
            pOut->pMipInfo[i].mipTailCoordY = 0;
            pOut->pMipInfo[i].mipTailCoordZ = 0;
        }
        else
        {
            UINT_32 mipX = ((mipOffset >> 9)  & 1)  |
                           ((mipOffset >> 10) & 2)  |
                           ((mipOffset >> 11) & 4)  |
                           ((mipOffset >> 12) & 8)  |
                           ((mipOffset >> 13) & 16) |
                           ((mipOffset >> 14) & 32);
            UINT_32 mipY = ((mipOffset >> 8)  & 1)  |
                           ((mipOffset >> 9)  & 2)  |
                           ((mipOffset >> 10) & 4)  |
                           ((mipOffset >> 11) & 8)  |
                           ((mipOffset >> 12) & 16) |
                           ((mipOffset >> 13) & 32);

            if (is3d == FALSE)
            {
                pOut->pMipInfo[i].mipTailCoordX = mipX * blockDimOfAddr256b2d.width;
                pOut->pMipInfo[i].mipTailCoordY = mipY * blockDimOfAddr256b2d.height;
                pOut->pMipInfo[i].mipTailCoordZ = 0;
            }
            else
            {
                pOut->pMipInfo[i].mipTailCoordX = mipX * pixelDimOfAddr4kb3d.width;
                pOut->pMipInfo[i].mipTailCoordY = mipY * pixelDimOfAddr4kb3d.height;
                pOut->pMipInfo[i].mipTailCoordZ = 0;
            }
        }
        if (IsLinear(pSurfInfo->swizzleMode))
        {
            pitch = Max(pitch >> 1, 1u);
        }
        else if (is3d)
        {
            pitch  = Max(pitch >> 1, blockDimOfAddr256b2d.width);
            height = Max(height >> 1, blockDimOfAddr256b2d.height);
            depth  = 1;
        }
        else
        {
            pitch  = Max(pitch >> 1, pixelDimOfAddr4kb3d.width);
            height = Max(height >> 1, pixelDimOfAddr4kb3d.height);
            depth  = PowTwoAlign(Max(depth >> 1, 1u), pixelDimOfAddr4kb3d.depth);
        }
    }
}

/**
************************************************************************************************************************
*   Gfx12Lib::GetMipOffset
*
*   @brief
*       Internal function to calculate alignment for a surface
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
VOID Gfx12Lib::GetMipOffset(
     const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,    ///< [in] input structure
     ADDR3_COMPUTE_SURFACE_INFO_OUTPUT*             pOut    ///< [out] output structure
     ) const
{
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pSurfInfo = pIn->pSurfInfo;
    const UINT_32        bytesPerPixel    = pSurfInfo->bpp >> 3;
    const UINT_32        elementBytesLog2 = Log2(bytesPerPixel);
    const UINT_32        blockSizeLog2    = GetBlockSizeLog2(pSurfInfo->swizzleMode);
    const UINT_32        blockSize        = 1 << blockSizeLog2;
    const ADDR_EXTENT3D  tailMaxDim       = GetMipTailDim(pIn, pOut->blockExtent);;
    const ADDR_EXTENT3D  mip0Dims         = GetBaseMipExtents(pSurfInfo);
    const UINT_32        maxMipsInTail    = GetMaxNumMipsInTail(pIn);

    UINT_32       firstMipInTail    = pSurfInfo->numMipLevels;
    UINT_64       mipChainSliceSize = 0;
    UINT_64       mipSize[MaxMipLevels];
    UINT_64       mipSliceSize[MaxMipLevels];

    const BOOL_32 useCustomPitch    = UseCustomPitch(pSurfInfo);
    const BOOL_32 trimLinearPadding = CanTrimLinearPadding(pSurfInfo);
    for (UINT_32 mipIdx = 0; mipIdx < pSurfInfo->numMipLevels; mipIdx++)
    {
        const ADDR_EXTENT3D  mipExtents = GetMipExtent(mip0Dims, mipIdx);

        if (Lib::SupportsMipTail(pSurfInfo->swizzleMode) &&
            IsInMipTail(tailMaxDim, mipExtents, maxMipsInTail, pSurfInfo->numMipLevels - mipIdx))
        {
            firstMipInTail     = mipIdx;
            mipChainSliceSize += blockSize / pOut->blockExtent.depth;
            break;
        }
        else
        {
            const BOOL_32 trimLinearPaddingForMip0 = ((mipIdx == 0) && trimLinearPadding);
            UINT_32       pitch                    = 0u;
            if (useCustomPitch)
            {
                pitch = pOut->pitch;
            }
            // We may use 128B pitch alignment for linear addressing mip0 image under some restriction, which is
            // followed by a post-processing to conform to base alignment requirement.
            else if (trimLinearPaddingForMip0)
            {
                pitch = PowTwoAlign(mipExtents.width, 128u / bytesPerPixel);
            }
            else
            {
                pitch = PowTwoAlign(mipExtents.width, pOut->blockExtent.width);
            }

            const UINT_32 height = UseCustomHeight(pSurfInfo)
                                        ? pOut->height
                                        : PowTwoAlign(mipExtents.height, pOut->blockExtent.height);
            const UINT_32 depth  = PowTwoAlign(mipExtents.depth, pOut->blockExtent.depth);

            // The original "blockExtent" calculation does subtraction of logs (i.e., division) to get the
            // sizes.  We aligned our pitch and height to those sizes, which means we need to multiply the various
            // factors back together to get back to the slice size.
            UINT_64 sliceSize = static_cast<UINT_64>(pitch) * height * pSurfInfo->numSamples * (pSurfInfo->bpp >> 3);

            // When using 128B for linear mip0, sometimes the calculated sliceSize above might be not 256B aligned,
            // e.g., sliceSize is odd times of 128B, it might cause next plane to be base misaligned.
            // We must check if sliceSize is 256B aligned, otherwise the pitch has to be adjusted to be 256B aligned
            // instead of 128B.
            if ((useCustomPitch == FALSE) && trimLinearPaddingForMip0 && (sliceSize % 256 != 0))
            {
                pitch     = PowTwoAlign(mipExtents.width, pOut->blockExtent.width);
                sliceSize = PowTwoAlign(sliceSize, 256ul);
            }

            mipSize[mipIdx]       = sliceSize * depth;
            mipSliceSize[mipIdx]  = sliceSize * pOut->blockExtent.depth;
            mipChainSliceSize    += sliceSize;

            if (pOut->pMipInfo != NULL)
            {
                pOut->pMipInfo[mipIdx].pitch  = pitch;
                pOut->pMipInfo[mipIdx].height = height;
                pOut->pMipInfo[mipIdx].depth  = depth;

                // The slice size of a linear image was calculated above as if the "pitch" is 256 byte aligned.
                // However, the rendering pitch is aligned to 128 bytes, and that is what needs to be reported
                // to our clients.
                if (IsLinear(pSurfInfo->swizzleMode) &&
                    // If UseCustomPitch is true, the pitch should be from pOut->pitch instead of 128B
                    (useCustomPitch == FALSE))
                {
                    pOut->pMipInfo[mipIdx].pitch = PowTwoAlign(mipExtents.width,  128u / bytesPerPixel);
                }
            }
        }
    }

    pOut->sliceSize        = mipChainSliceSize;
    pOut->surfSize         = mipChainSliceSize * pOut->numSlices;
    pOut->mipChainInTail   = (firstMipInTail == 0) ? TRUE : FALSE;
    pOut->firstMipIdInTail = firstMipInTail;

    if (pOut->pMipInfo != NULL)
    {
        if (IsLinear(pSurfInfo->swizzleMode))
        {
            // 1. Linear swizzle mode doesn't have miptails.
            // 2. The organization of linear 3D mipmap resource is same as GFX11, we should use mip slice size to
            // caculate mip offset.
            ADDR_ASSERT(firstMipInTail == pSurfInfo->numMipLevels);

            UINT_64 sliceSize = 0;

            for (INT_32 i = static_cast<INT_32>(pSurfInfo->numMipLevels) - 1; i >= 0; i--)
            {
                pOut->pMipInfo[i].offset           = sliceSize;
                pOut->pMipInfo[i].macroBlockOffset = sliceSize;
                pOut->pMipInfo[i].mipTailOffset    = 0;

                sliceSize += mipSliceSize[i];
            }
        }
        else
        {
            UINT_64 offset         = 0;
            UINT_64 macroBlkOffset = 0;

            // Even though "firstMipInTail" is zero-based while "numMipLevels" is one-based, from definition of
            // _ADDR3_COMPUTE_SURFACE_INFO_OUTPUT struct,
            // UINT_32             firstMipIdInTail;     ///< The id of first mip in tail, if there is no mip
            //                                           ///  in tail, it will be set to number of mip levels
            // See initialization:
            //              UINT_32       firstMipInTail    = pIn->numMipLevels
            // It is possible that they are equal if
            //      1. a single mip level image that's larger than the largest mip that would fit in the mip tail if
            //         the mip tail existed
            //      2. 256B_2D and linear images which don't have miptails from HWAL functionality
            //
            // We can use firstMipInTail != pIn->numMipLevels to check it has mip in tails and do mipInfo assignment.
            if (firstMipInTail != pSurfInfo->numMipLevels)
            {
                // Determine the application dimensions of the first mip level that resides in the tail.
                // This is distinct from "tailMaxDim" which is the maximum size of a mip level that will fit in the
                // tail.
                ADDR_EXTENT3D mipExtentFirstInTail = GetMipExtent(mip0Dims, firstMipInTail);

                // For a 2D image, "alignedDepth" is always "1".
                // For a 3D image, this is effectively the number of application slices associated with the first mip
                //                 in the tail (up-aligned to HW requirements).
                const UINT_32 alignedDepth = PowTwoAlign(mipExtentFirstInTail.depth, pOut->blockExtent.depth);

                // "hwSlices" is the number of HW blocks required to represent the first mip level in the tail.
                const UINT_32 hwSlices = alignedDepth / pOut->blockExtent.depth;

                // Note that for 3D images that utilize a 2D swizzle mode, there really can be multiple
                // HW slices that encompass the mip tail; i.e., hwSlices is not necessarily one.
                // For example, you could have a single mip level 8x8x32 image with a 4KB_2D swizzle mode
                // The 8x8 region fits into a 4KB block (so it's "in the tail"), but because we have a 2D
                // swizzle mode (where each slice is its own block, so blockExtent.depth == 1), hwSlices
                // will now be equivalent to the number of application slices, or 32.

                // Mip tails are stored in "reverse" order -- i.e., the mip-tail itself is stored first, so the
                // first mip level outside the tail has an offset that's the dimension of the tail itself, or one
                // swizzle block in size.
                offset         = blockSize * hwSlices;
                macroBlkOffset = blockSize;

                // And determine the per-mip information for everything inside the mip tail.
                GetMipOrigin(pIn, mipExtentFirstInTail, pOut);
            }

            // Again, because mip-levels are stored backwards (smallest first), we start determining mip-level
            // offsets from the smallest to the largest.
            // Note that firstMipInTail == 0 immediately terminates the loop, so there is no need to check for this
            // case.
            for (INT_32 i = firstMipInTail - 1; i >= 0; i--)
            {
                pOut->pMipInfo[i].offset           = offset;
                pOut->pMipInfo[i].macroBlockOffset = macroBlkOffset;
                pOut->pMipInfo[i].mipTailOffset    = 0;

                offset         += mipSize[i];
                macroBlkOffset += mipSliceSize[i];
            }
        }
    }
}

/**
************************************************************************************************************************
*   Gfx12Lib::HwlComputeSurfaceInfo
*
*   @brief
*       Internal function to calculate alignment for a surface
*
*   @return
*       VOID
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx12Lib::HwlComputeSurfaceInfo(
     const ADDR3_COMPUTE_SURFACE_INFO_INPUT*  pSurfInfo,  ///< [in] input structure
     ADDR3_COMPUTE_SURFACE_INFO_OUTPUT*       pOut        ///< [out] output structure
     ) const
{
    ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT input{ pSurfInfo };

    // Check that only 2D swizzle mode supports MSAA
    const UINT_32 samplesLog2 = Is2dSwizzle(pSurfInfo->swizzleMode) ? Log2(pSurfInfo->numSamples) : 0;

    // The block dimension width/height/depth is determined only by swizzle mode, MSAA samples and bpp
    pOut->blockExtent = GetBlockDimensionTableEntry(pSurfInfo->swizzleMode, samplesLog2, Log2(pSurfInfo->bpp >> 3));

    ADDR_E_RETURNCODE  returnCode = ApplyCustomizedPitchHeight(pSurfInfo, pOut);

    if (returnCode == ADDR_OK)
    {
        pOut->numSlices = PowTwoAlign(pSurfInfo->numSlices, pOut->blockExtent.depth);
        pOut->baseAlign = 1 << GetBlockSizeLog2(pSurfInfo->swizzleMode);

        GetMipOffset(&input, pOut);

        SanityCheckSurfSize(&input, pOut);

        // Slices must be exact multiples of the block sizes.  However:
        // - with 3D images, one block will contain multiple slices, so that needs to be taken into account.
        //
        // Note that with linear images that have only one slice, we can always guarantee pOut->sliceSize is 256B
        // alignment so there is no need to worry about it.
        ADDR_ASSERT(((pOut->sliceSize * pOut->blockExtent.depth) % GetBlockSize(pSurfInfo->swizzleMode)) == 0);
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Gfx12Lib::GetBaseMipExtents
*
*   @brief
*       Return the size of the base mip level in a nice cozy little structure.
*
************************************************************************************************************************
*/
ADDR_EXTENT3D Gfx12Lib::GetBaseMipExtents(
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pIn
    ) const
{
    return { pIn->width,
             pIn->height,
             (IsTex3d(pIn->resourceType) ? pIn->numSlices : 1) }; // slices is depth for 3d
}

/**
************************************************************************************************************************
*   Gfx12Lib::GetMaxNumMipsInTail
*
*   @brief
*       Return max number of mips in tails
*
*   @return
*       Max number of mips in tails
************************************************************************************************************************
*/
UINT_32 Gfx12Lib::GetMaxNumMipsInTail(
    const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn
    ) const
{
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pSurfInfo = pIn->pSurfInfo;
    const UINT_32  blockSizeLog2 = GetBlockSizeLog2(pSurfInfo->swizzleMode);

    UINT_32 effectiveLog2 = blockSizeLog2;
    UINT_32 mipsInTail    = 1;

    if (Is3dSwizzle(pSurfInfo->swizzleMode))
    {
        effectiveLog2 -= (blockSizeLog2 - 8) / 3;
    }

    if (effectiveLog2 > 8)
    {
        mipsInTail = (effectiveLog2 <= 11) ? (1 + (1 << (effectiveLog2 - 9))) : (effectiveLog2 - 4);
    }

    return mipsInTail;
}

/**
************************************************************************************************************************
*   Gfx12Lib::HwlCalcMipInTail
*
*   @brief
*       Internal function to calculate the "mipInTail" parameter.
*
*   @return
*       The magic "mipInTail" parameter.
************************************************************************************************************************
*/
INT_32 Gfx12Lib::CalcMipInTail(
    const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,
    const ADDR3_COMPUTE_SURFACE_INFO_OUTPUT*       pOut,
    UINT_32                                        mipLevel
    ) const
{
    const INT_32  firstMipIdInTail = static_cast<INT_32>(pOut->firstMipIdInTail);

    INT_32  mipInTail = 0;

    const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pSurfInfo = pIn->pSurfInfo;
    mipInTail = static_cast<INT_32>(mipLevel) - firstMipIdInTail;
    if ((mipInTail < 0) || (pSurfInfo->numMipLevels == 1) || (GetBlockSize(pSurfInfo->swizzleMode) <= 256))
    {
        mipInTail = MaxMipLevels;
    }

    return mipInTail;
}

/**
************************************************************************************************************************
*   Gfx12Lib::CalcMipOffset
*
*   @brief
*
*   @return
*       The magic "mipInTail" parameter.
************************************************************************************************************************
*/
UINT_32 Gfx12Lib::CalcMipOffset(
    const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,
    UINT_32                                        mipInTail
    ) const
{
    const UINT_32 maxMipsInTail = GetMaxNumMipsInTail(pIn);

    const INT_32  signedM       = static_cast<INT_32>(maxMipsInTail) - static_cast<INT_32>(1) - mipInTail;
    const UINT_32 m             = Max(0, signedM);
    const UINT_32 mipOffset     = (m > 6) ? (16 << m) : (m << 8);

    return mipOffset;
}

/**
************************************************************************************************************************
*   Gfx12Lib::HwlComputeSurfaceAddrFromCoordTiled
*
*   @brief
*       Internal function to calculate address from coord for tiled swizzle surface
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx12Lib::HwlComputeSurfaceAddrFromCoordTiled(
     const ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,    ///< [in] input structure
     ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    // 256B block cannot support 3D image.
    ADDR_ASSERT((IsTex3d(pIn->resourceType) && IsBlock256b(pIn->swizzleMode)) == FALSE);

    ADDR3_COMPUTE_SURFACE_INFO_INPUT  localIn               = {};
    ADDR3_COMPUTE_SURFACE_INFO_OUTPUT localOut              = {};
    ADDR3_MIP_INFO                    mipInfo[MaxMipLevels] = {};

    localIn.size         = sizeof(localIn);
    localIn.flags        = pIn->flags;
    localIn.swizzleMode  = pIn->swizzleMode;
    localIn.resourceType = pIn->resourceType;
    localIn.format       = ADDR_FMT_INVALID;
    localIn.bpp          = pIn->bpp;
    localIn.width        = Max(pIn->unAlignedDims.width, 1u);
    localIn.height       = Max(pIn->unAlignedDims.height, 1u);
    localIn.numSlices    = Max(pIn->unAlignedDims.depth, 1u);
    localIn.numMipLevels = Max(pIn->numMipLevels, 1u);
    localIn.numSamples   = Max(pIn->numSamples, 1u);

    localOut.size        = sizeof(localOut);
    localOut.pMipInfo    = mipInfo;
    ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT input{ &localIn };

    ADDR_E_RETURNCODE ret = ComputeSurfaceInfo(&localIn, &localOut);

    if (ret == ADDR_OK)
    {
        const UINT_32 elemLog2    = Log2(pIn->bpp >> 3);
        const UINT_32 blkSizeLog2 = GetBlockSizeLog2(pIn->swizzleMode);
        const UINT_32 eqIndex     = GetEquationTableEntry(pIn->swizzleMode, Log2(localIn.numSamples), elemLog2);

        if (eqIndex != ADDR_INVALID_EQUATION_INDEX)
        {
            ADDR3_COORD  coords = {};

            // For a 3D image, one swizzle block contains multiple application slices.
            // For any given image, each HW slice is addressed identically to any other HW slice.
            // hwSliceSizeBytes is the size of one HW slice; i.e., the number of bytes for the pattern to repeat.
            // hwSliceId is the index (0, 1, 2...) of the HW slice that an application slice resides in.
            const UINT_64 hwSliceSizeBytes = localOut.sliceSize * localOut.blockExtent.depth;
            const UINT_32 hwSliceId = pIn->slice / localOut.blockExtent.depth;

            const UINT_32 pb     = mipInfo[pIn->mipId].pitch / localOut.blockExtent.width;
            const UINT_32 yb     = pIn->y / localOut.blockExtent.height;
            const UINT_32 xb     = pIn->x / localOut.blockExtent.width;
            const UINT_64 blkIdx = yb * pb + xb;

            // Technically, the addition of "mipTailCoordX" is only necessary if we're in the mip-tail.
            // The "mipTailCoordXYZ" values should be zero if we're not in the mip-tail.
            const BOOL_32 inTail = ((mipInfo[pIn->mipId].mipTailOffset != 0) && (blkSizeLog2 != Log2Size256));

            ADDR_ASSERT((inTail == TRUE) ||
                        // If we're not in the tail, then all of these must be zero.
                        ((mipInfo[pIn->mipId].mipTailCoordX == 0) &&
                         (mipInfo[pIn->mipId].mipTailCoordY == 0) &&
                         (mipInfo[pIn->mipId].mipTailCoordZ == 0)));

            coords.x = pIn->x     + mipInfo[pIn->mipId].mipTailCoordX;
            coords.y = pIn->y     + mipInfo[pIn->mipId].mipTailCoordY;
            coords.z = pIn->slice + mipInfo[pIn->mipId].mipTailCoordZ;

            // Note that in this path, blkIdx does not account for the HW slice ID, so we need to
            // add it in here.
            pOut->addr = hwSliceSizeBytes * hwSliceId;

            const UINT_32 blkOffset  = ComputeOffsetFromEquation(&m_equationTable[eqIndex],
                                                                 coords.x << elemLog2,
                                                                 coords.y,
                                                                 coords.z,
                                                                 pIn->sample);

            pOut->addr += mipInfo[pIn->mipId].macroBlockOffset +
                          (blkIdx << blkSizeLog2)              +
                          blkOffset;

            ADDR_ASSERT(pOut->addr < localOut.surfSize);
        }
        else
        {
            ret = ADDR_INVALIDPARAMS;
        }
    }

    return ret;
}

/**
************************************************************************************************************************
*   Gfx12Lib::HwlComputePipeBankXor
*
*   @brief
*       Generate a PipeBankXor value to be ORed into bits above numSwizzleBits of address
*
*   @return
*       PipeBankXor value
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx12Lib::HwlComputePipeBankXor(
    const ADDR3_COMPUTE_PIPEBANKXOR_INPUT* pIn,     ///< [in] input structure
    ADDR3_COMPUTE_PIPEBANKXOR_OUTPUT*      pOut     ///< [out] output structure
    ) const
{
    if ((m_numSwizzleBits != 0)               && // does this configuration support swizzling
        //         base address XOR in GFX12 will be applied to all blk_size = 4KB, 64KB, or 256KB swizzle modes,
        //         Note that Linear and 256B are excluded.
        (IsLinear(pIn->swizzleMode) == FALSE) &&
        (IsBlock256b(pIn->swizzleMode) == FALSE))
    {
        pOut->pipeBankXor = pIn->surfIndex % (1 << m_numSwizzleBits);
    }
    else
    {
        pOut->pipeBankXor = 0;
    }

    return ADDR_OK;
}

/**
************************************************************************************************************************
*   Gfx12Lib::ComputeOffsetFromEquation
*
*   @brief
*       Compute offset from equation
*
*   @return
*       Offset
************************************************************************************************************************
*/
UINT_32 Gfx12Lib::ComputeOffsetFromEquation(
    const ADDR_EQUATION* pEq,   ///< Equation
    UINT_32              x,     ///< x coord in bytes
    UINT_32              y,     ///< y coord in pixel
    UINT_32              z,     ///< z coord in slice
    UINT_32              s      ///< MSAA sample index
    ) const
{
    UINT_32 offset = 0;

    for (UINT_32 i = 0; i < pEq->numBits; i++)
    {
        UINT_32 v = 0;

        if (pEq->addr[i].valid)
        {
            if (pEq->addr[i].channel == 0)
            {
                v ^= (x >> pEq->addr[i].index) & 1;
            }
            else if (pEq->addr[i].channel == 1)
            {
                v ^= (y >> pEq->addr[i].index) & 1;
            }
            else if (pEq->addr[i].channel == 2)
            {
                v ^= (z >> pEq->addr[i].index) & 1;
            }
            else if (pEq->addr[i].channel == 3)
            {
                v ^= (s >> pEq->addr[i].index) & 1;
            }
            else
            {
                ADDR_ASSERT_ALWAYS();
            }
        }

        offset |= (v << i);
    }

    return offset;
}

/**
************************************************************************************************************************
*   Gfx12Lib::GetSwizzlePatternInfo
*
*   @brief
*       Get swizzle pattern
*
*   @return
*       Swizzle pattern information
************************************************************************************************************************
*/
const ADDR_SW_PATINFO* Gfx12Lib::GetSwizzlePatternInfo(
    Addr3SwizzleMode swizzleMode,       ///< Swizzle mode
    UINT_32          elemLog2,          ///< Element size in bytes log2
    UINT_32          numFrag            ///< Number of fragment
    ) const
{
    const ADDR_SW_PATINFO* patInfo = NULL;

    if (Is2dSwizzle(swizzleMode) == FALSE)
    {
        ADDR_ASSERT(numFrag == 1);
    }

    switch (swizzleMode)
    {
    case ADDR3_256KB_2D:
        switch (numFrag)
        {
        case 1:
            patInfo = GFX12_SW_256KB_2D_1xAA_PATINFO;
            break;
        case 2:
            patInfo = GFX12_SW_256KB_2D_2xAA_PATINFO;
            break;
        case 4:
            patInfo = GFX12_SW_256KB_2D_4xAA_PATINFO;
            break;
        case 8:
            patInfo = GFX12_SW_256KB_2D_8xAA_PATINFO;
            break;
        default:
            ADDR_ASSERT_ALWAYS();
        }
        break;
    case ADDR3_256KB_3D:
        patInfo = GFX12_SW_256KB_3D_PATINFO;
        break;
    case ADDR3_64KB_2D:
        switch (numFrag)
        {
        case 1:
            patInfo = GFX12_SW_64KB_2D_1xAA_PATINFO;
            break;
        case 2:
            patInfo = GFX12_SW_64KB_2D_2xAA_PATINFO;
            break;
        case 4:
            patInfo = GFX12_SW_64KB_2D_4xAA_PATINFO;
            break;
        case 8:
            patInfo = GFX12_SW_64KB_2D_8xAA_PATINFO;
            break;
        default:
            ADDR_ASSERT_ALWAYS();
        }
        break;
    case ADDR3_64KB_3D:
        patInfo = GFX12_SW_64KB_3D_PATINFO;
        break;
    case ADDR3_4KB_2D:
        switch (numFrag)
        {
        case 1:
            patInfo = GFX12_SW_4KB_2D_1xAA_PATINFO;
            break;
        case 2:
            patInfo = GFX12_SW_4KB_2D_2xAA_PATINFO;
            break;
        case 4:
            patInfo = GFX12_SW_4KB_2D_4xAA_PATINFO;
            break;
        case 8:
            patInfo = GFX12_SW_4KB_2D_8xAA_PATINFO;
            break;
        default:
            ADDR_ASSERT_ALWAYS();
        }
        break;
    case ADDR3_4KB_3D:
        patInfo = GFX12_SW_4KB_3D_PATINFO;
        break;
    case ADDR3_256B_2D:
        switch (numFrag)
        {
        case 1:
            patInfo = GFX12_SW_256B_2D_1xAA_PATINFO;
            break;
        case 2:
            patInfo = GFX12_SW_256B_2D_2xAA_PATINFO;
            break;
        case 4:
            patInfo = GFX12_SW_256B_2D_4xAA_PATINFO;
            break;
        case 8:
            patInfo = GFX12_SW_256B_2D_8xAA_PATINFO;
            break;
        default:
            break;
        }
        break;
    default:
        ADDR_ASSERT_ALWAYS();
        break;
    }

    return (patInfo != NULL) ? &patInfo[elemLog2] : NULL;
}
/**
************************************************************************************************************************
*   Gfx12Lib::HwlInitGlobalParams
*
*   @brief
*       Initializes global parameters
*
*   @return
*       TRUE if all settings are valid
*
************************************************************************************************************************
*/
BOOL_32 Gfx12Lib::HwlInitGlobalParams(
    const ADDR_CREATE_INPUT* pCreateIn) ///< [in] create input
{
    BOOL_32              valid = TRUE;
    GB_ADDR_CONFIG_GFX12 gbAddrConfig;

    gbAddrConfig.u32All = pCreateIn->regValue.gbAddrConfig;

    switch (gbAddrConfig.bits.NUM_PIPES)
    {
        case ADDR_CONFIG_1_PIPE:
            m_pipesLog2 = 0;
            break;
        case ADDR_CONFIG_2_PIPE:
            m_pipesLog2 = 1;
            break;
        case ADDR_CONFIG_4_PIPE:
            m_pipesLog2 = 2;
            break;
        case ADDR_CONFIG_8_PIPE:
            m_pipesLog2 = 3;
            break;
        case ADDR_CONFIG_16_PIPE:
            m_pipesLog2 = 4;
            break;
        case ADDR_CONFIG_32_PIPE:
            m_pipesLog2 = 5;
            break;
        case ADDR_CONFIG_64_PIPE:
            m_pipesLog2 = 6;
            break;
        default:
            ADDR_ASSERT_ALWAYS();
            valid = FALSE;
            break;
    }

    switch (gbAddrConfig.bits.PIPE_INTERLEAVE_SIZE)
    {
        case ADDR_CONFIG_PIPE_INTERLEAVE_256B:
            m_pipeInterleaveLog2 = 8;
            break;
        case ADDR_CONFIG_PIPE_INTERLEAVE_512B:
            m_pipeInterleaveLog2 = 9;
            break;
        case ADDR_CONFIG_PIPE_INTERLEAVE_1KB:
            m_pipeInterleaveLog2 = 10;
            break;
        case ADDR_CONFIG_PIPE_INTERLEAVE_2KB:
            m_pipeInterleaveLog2 = 11;
            break;
        default:
            ADDR_ASSERT_ALWAYS();
            valid = FALSE;
            break;
    }

    m_numSwizzleBits = ((m_pipesLog2 >= 3) ? m_pipesLog2 - 2 : 0);

    if (valid)
    {
        InitEquationTable();
        InitBlockDimensionTable();
    }

    return valid;
}

/**
************************************************************************************************************************
*   Gfx12Lib::HwlComputeNonBlockCompressedView
*
*   @brief
*       Compute non-block-compressed view for a given mipmap level/slice.
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx12Lib::HwlComputeNonBlockCompressedView(
    const ADDR3_COMPUTE_NONBLOCKCOMPRESSEDVIEW_INPUT* pIn,    ///< [in] input structure
    ADDR3_COMPUTE_NONBLOCKCOMPRESSEDVIEW_OUTPUT*      pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (((pIn->format < ADDR_FMT_ASTC_4x4) || (pIn->format > ADDR_FMT_ETC2_128BPP)) &&
        ((pIn->format < ADDR_FMT_BC1) || (pIn->format > ADDR_FMT_BC7)))
    {
        // Only support BC1~BC7, ASTC, or ETC2 for now...
        returnCode = ADDR_NOTSUPPORTED;
    }
    else
    {
        UINT_32 bcWidth, bcHeight;
        const UINT_32 bpp = GetElemLib()->GetBitsPerPixel(pIn->format, NULL, &bcWidth, &bcHeight);

        ADDR3_COMPUTE_SURFACE_INFO_INPUT infoIn = {};
        infoIn.size         = sizeof(infoIn);
        infoIn.flags        = pIn->flags;
        infoIn.swizzleMode  = pIn->swizzleMode;
        infoIn.resourceType = pIn->resourceType;
        infoIn.format       = pIn->format;
        infoIn.bpp          = bpp;
        infoIn.width        = RoundUpQuotient(pIn->unAlignedDims.width, bcWidth);
        infoIn.height       = RoundUpQuotient(pIn->unAlignedDims.height, bcHeight);
        infoIn.numSlices    = pIn->unAlignedDims.depth;
        infoIn.numMipLevels = pIn->numMipLevels;
        infoIn.numSamples   = 1;

        ADDR3_MIP_INFO mipInfo[MaxMipLevels] = {};

        ADDR3_COMPUTE_SURFACE_INFO_OUTPUT infoOut = {};
        infoOut.size     = sizeof(infoOut);
        infoOut.pMipInfo = mipInfo;

        returnCode = HwlComputeSurfaceInfo(&infoIn, &infoOut);

        if (returnCode == ADDR_OK)
        {
            ADDR3_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_INPUT subOffIn = {};
            subOffIn.size             = sizeof(subOffIn);
            subOffIn.swizzleMode      = infoIn.swizzleMode;
            subOffIn.resourceType     = infoIn.resourceType;
            subOffIn.pipeBankXor      = pIn->pipeBankXor;
            subOffIn.slice            = pIn->slice;
            subOffIn.sliceSize        = infoOut.sliceSize;
            subOffIn.macroBlockOffset = mipInfo[pIn->mipId].macroBlockOffset;
            subOffIn.mipTailOffset    = mipInfo[pIn->mipId].mipTailOffset;

            ADDR3_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_OUTPUT subOffOut = {};
            subOffOut.size = sizeof(subOffOut);

            // For any mipmap level, move nonBc view base address by offset
            HwlComputeSubResourceOffsetForSwizzlePattern(&subOffIn, &subOffOut);
            pOut->offset = subOffOut.offset;

            ADDR3_COMPUTE_SLICE_PIPEBANKXOR_INPUT slicePbXorIn = {};
            slicePbXorIn.size            = sizeof(slicePbXorIn);
            slicePbXorIn.swizzleMode     = infoIn.swizzleMode;
            slicePbXorIn.resourceType    = infoIn.resourceType;
            slicePbXorIn.bpe             = infoIn.bpp;
            slicePbXorIn.basePipeBankXor = pIn->pipeBankXor;
            slicePbXorIn.slice           = pIn->slice;
            slicePbXorIn.numSamples      = 1;

            ADDR3_COMPUTE_SLICE_PIPEBANKXOR_OUTPUT slicePbXorOut = {};
            slicePbXorOut.size = sizeof(slicePbXorOut);

            // For any mipmap level, nonBc view should use computed pbXor
            HwlComputeSlicePipeBankXor(&slicePbXorIn, &slicePbXorOut);
            pOut->pipeBankXor = slicePbXorOut.pipeBankXor;

            const BOOL_32 tiled            = (pIn->swizzleMode != ADDR3_LINEAR);
            const BOOL_32 inTail           = tiled && (pIn->mipId >= infoOut.firstMipIdInTail);
            const UINT_32 requestMipWidth  =
                    RoundUpQuotient(Max(pIn->unAlignedDims.width  >> pIn->mipId, 1u), bcWidth);
            const UINT_32 requestMipHeight =
                    RoundUpQuotient(Max(pIn->unAlignedDims.height >> pIn->mipId, 1u), bcHeight);

            if (inTail)
            {
                // For mipmap level that is in mip tail block, hack a lot of things...
                // Basically all mipmap levels in tail block will be viewed as a small mipmap chain that all levels
                // are fit in tail block:

                // - mipId = relative mip id (which is counted from first mip ID in tail in original mip chain)
                pOut->mipId = pIn->mipId - infoOut.firstMipIdInTail;

                // - at least 2 mipmap levels (since only 1 mipmap level will not be viewed as mipmap!)
                pOut->numMipLevels = Max(infoIn.numMipLevels - infoOut.firstMipIdInTail, 2u);

                // - (mip0) width = requestMipWidth << mipId, the value can't exceed mip tail dimension threshold
                pOut->unAlignedDims.width  = Min(requestMipWidth << pOut->mipId, infoOut.blockExtent.width / 2);

                // - (mip0) height = requestMipHeight << mipId, the value can't exceed mip tail dimension threshold
                pOut->unAlignedDims.height = Min(requestMipHeight << pOut->mipId, infoOut.blockExtent.height);
            }
            // This check should cover at least mipId == 0
            else if ((requestMipWidth << pIn->mipId) == infoIn.width)
            {
                // For mipmap level [N] that is not in mip tail block and downgraded without losing element:
                // - only one mipmap level and mipId = 0
                pOut->mipId        = 0;
                pOut->numMipLevels = 1;

                // (mip0) width = requestMipWidth
                pOut->unAlignedDims.width  = requestMipWidth;

                // (mip0) height = requestMipHeight
                pOut->unAlignedDims.height = requestMipHeight;
            }
            else
            {
                // For mipmap level [N] that is not in mip tail block and downgraded with element losing,
                // We have to make it a multiple mipmap view (2 levels view here), add one extra element if needed,
                // because single mip view may have different pitch value than original (multiple) mip view...
                // A simple case would be:
                // - 64KB block swizzle mode, 8 Bytes-Per-Element. Block dim = [0x80, 0x40]
                // - 2 mipmap levels with API mip0 width = 0x401/mip1 width = 0x200 and non-BC view
                //   mip0 width = 0x101/mip1 width = 0x80
                // By multiple mip view, the pitch for mip level 1 would be 0x100 bytes, due to rounding up logic in
                // GetMipSize(), and by single mip level view the pitch will only be 0x80 bytes.

                // - 2 levels and mipId = 1
                pOut->mipId        = 1;
                pOut->numMipLevels = 2;

                const UINT_32 upperMipWidth  =
                    RoundUpQuotient(Max(pIn->unAlignedDims.width  >> (pIn->mipId - 1), 1u), bcWidth);
                const UINT_32 upperMipHeight =
                    RoundUpQuotient(Max(pIn->unAlignedDims.height >> (pIn->mipId - 1), 1u), bcHeight);

                const BOOL_32 needToAvoidInTail = tiled                                              &&
                                                  (requestMipWidth <= infoOut.blockExtent.width / 2) &&
                                                  (requestMipHeight <= infoOut.blockExtent.height);

                const UINT_32 hwMipWidth  =
                    PowTwoAlign(ShiftCeil(infoIn.width, pIn->mipId), infoOut.blockExtent.width);
                const UINT_32 hwMipHeight =
                    PowTwoAlign(ShiftCeil(infoIn.height, pIn->mipId), infoOut.blockExtent.height);

                const BOOL_32 needExtraWidth =
                    ((upperMipWidth < requestMipWidth * 2) ||
                     ((upperMipWidth == requestMipWidth * 2) &&
                      ((needToAvoidInTail == TRUE) ||
                       (hwMipWidth > PowTwoAlign(requestMipWidth, infoOut.blockExtent.width)))));

                const BOOL_32 needExtraHeight =
                    ((upperMipHeight < requestMipHeight * 2) ||
                     ((upperMipHeight == requestMipHeight * 2) &&
                      ((needToAvoidInTail == TRUE) ||
                       (hwMipHeight > PowTwoAlign(requestMipHeight, infoOut.blockExtent.height)))));

                // (mip0) width = requestLastMipLevelWidth
                pOut->unAlignedDims.width  = upperMipWidth + (needExtraWidth ? 1: 0);

                // (mip0) height = requestLastMipLevelHeight
                pOut->unAlignedDims.height = upperMipHeight + (needExtraHeight ? 1: 0);
            }

            // Assert the downgrading from this mip[0] width would still generate correct mip[N] width
            ADDR_ASSERT(ShiftRight(pOut->unAlignedDims.width, pOut->mipId)  == requestMipWidth);
            // Assert the downgrading from this mip[0] height would still generate correct mip[N] height
            ADDR_ASSERT(ShiftRight(pOut->unAlignedDims.height, pOut->mipId) == requestMipHeight);
        }
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Gfx12Lib::HwlComputeSubResourceOffsetForSwizzlePattern
*
*   @brief
*       Compute sub resource offset to support swizzle pattern
*
*   @return
*       VOID
************************************************************************************************************************
*/
VOID Gfx12Lib::HwlComputeSubResourceOffsetForSwizzlePattern(
    const ADDR3_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_INPUT* pIn,    ///< [in] input structure
    ADDR3_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_OUTPUT*      pOut    ///< [out] output structure
    ) const
{
    pOut->offset = pIn->slice * pIn->sliceSize + pIn->macroBlockOffset;
}

/**
************************************************************************************************************************
*   Gfx12Lib::HwlComputeSlicePipeBankXor
*
*   @brief
*       Generate slice PipeBankXor value based on base PipeBankXor value and slice id
*
*   @return
*       PipeBankXor value
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx12Lib::HwlComputeSlicePipeBankXor(
    const ADDR3_COMPUTE_SLICE_PIPEBANKXOR_INPUT* pIn,   ///< [in] input structure
    ADDR3_COMPUTE_SLICE_PIPEBANKXOR_OUTPUT*      pOut   ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    // PipeBankXor is only applied to 4KB, 64KB and 256KB on GFX12.
    if ((IsLinear(pIn->swizzleMode) == FALSE) && (IsBlock256b(pIn->swizzleMode) == FALSE))
    {
        if (pIn->bpe == 0)
        {
            // Require a valid bytes-per-element value passed from client...
            returnCode = ADDR_INVALIDPARAMS;
        }
        else
        {
            const ADDR_SW_PATINFO* pPatInfo = GetSwizzlePatternInfo(pIn->swizzleMode,
                                                                    Log2(pIn->bpe >> 3),
                                                                    1);

            if (pPatInfo != NULL)
            {
                const UINT_32 elemLog2    = Log2(pIn->bpe >> 3);
                const UINT_32 eqIndex     = GetEquationTableEntry(pIn->swizzleMode, Log2(pIn->numSamples), elemLog2);

                const UINT_32 pipeBankXorOffset = ComputeOffsetFromEquation(&m_equationTable[eqIndex],
                                                                            0,
                                                                            0,
                                                                            pIn->slice,
                                                                            0);

                const UINT_32 pipeBankXor = pipeBankXorOffset >> m_pipeInterleaveLog2;

                // Should have no bit set under pipe interleave
                ADDR_ASSERT((pipeBankXor << m_pipeInterleaveLog2) == pipeBankXorOffset);

                pOut->pipeBankXor = pIn->basePipeBankXor ^ pipeBankXor;
            }
            else
            {
                // Should never come here...
                ADDR_NOT_IMPLEMENTED();

                returnCode = ADDR_NOTSUPPORTED;
            }
        }
    }
    else
    {
        pOut->pipeBankXor = 0;
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Gfx12Lib::HwlConvertChipFamily
*
*   @brief
*       Convert familyID defined in atiid.h to ChipFamily and set m_chipFamily/m_chipRevision
*   @return
*       ChipFamily
************************************************************************************************************************
*/
ChipFamily Gfx12Lib::HwlConvertChipFamily(
    UINT_32 chipFamily,        ///< [in] chip family defined in atiih.h
    UINT_32 chipRevision)      ///< [in] chip revision defined in "asic_family"_id.h
{
    return ADDR_CHIP_FAMILY_NAVI;
}

/**
************************************************************************************************************************
*   Gfx12Lib::SanityCheckSurfSize
*
*   @brief
*       Calculate the surface size via the exact hardware algorithm to see if it matches.
*
*   @return
************************************************************************************************************************
*/
void Gfx12Lib::SanityCheckSurfSize(
    const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,
    const ADDR3_COMPUTE_SURFACE_INFO_OUTPUT*       pOut
    ) const
{
#if DEBUG
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pSurfInfo = pIn->pSurfInfo;
    // Verify that the requested image size is valid for the below algorithm.  The below code includes
    // implicit assumptions about the surface dimensions being less than "MaxImageDim"; otherwise, it can't
    // calculate "firstMipInTail" accurately and the below assertion will trip incorrectly.
    //
    // Surfaces destined for use only on the SDMA engine can exceed the gfx-engine-imposed limitations of
    // the "maximum" image dimensions.
    if ((pSurfInfo->width <= MaxImageDim)         &&
        (pSurfInfo->height <= MaxImageDim)        &&
        (pSurfInfo->numMipLevels <= MaxMipLevels) &&
        (UseCustomPitch(pSurfInfo) == FALSE)      &&
        (UseCustomHeight(pSurfInfo) == FALSE)     &&
        // HiZS surfaces have a reduced image size (i.e,. each pixel represents an 8x8 region of the parent
        // image, at least for single samples) but they still have the same number of mip levels as the
        // parent image.  This disconnect produces false assertions below as the image size doesn't apparently
        // support the specified number of mip levels.
        ((pSurfInfo->flags.hiZHiS == 0) || (pSurfInfo->numMipLevels == 1)))
    {
        UINT_32  lastMipSize = 1;
        UINT_64  dataChainSize = 0;

        const ADDR_EXTENT3D  mip0Dims      = GetBaseMipExtents(pSurfInfo);
        const UINT_32        blockSizeLog2 = GetBlockSizeLog2(pSurfInfo->swizzleMode);
        const ADDR_EXTENT3D  tailMaxDim    = GetMipTailDim(pIn, pOut->blockExtent);
        const UINT_32        maxMipsInTail = GetMaxNumMipsInTail(pIn);

        UINT_32  firstMipInTail = 0;
        for (INT_32 mipIdx = MaxMipLevels - 1; mipIdx >= 0; mipIdx--)
        {
            const ADDR_EXTENT3D  mipExtents = GetMipExtent(mip0Dims, mipIdx);

            if (IsInMipTail(tailMaxDim, mipExtents, maxMipsInTail, pSurfInfo->numMipLevels - mipIdx))
            {
                firstMipInTail = mipIdx;
            }
        }

        for (INT_32 mipIdx = firstMipInTail - 1; mipIdx >= -1; mipIdx--)
        {
            if (mipIdx < (static_cast<INT_32>(pSurfInfo->numMipLevels) - 1))
            {
                dataChainSize += lastMipSize;
            }

            if (mipIdx >= 0)
            {
                const ADDR_EXTENT3D  mipExtents     = GetMipExtent(mip0Dims, mipIdx);
                const UINT_32        mipBlockWidth  = ShiftCeil(mipExtents.width, Log2(pOut->blockExtent.width));
                const UINT_32        mipBlockHeight = ShiftCeil(mipExtents.height, Log2(pOut->blockExtent.height));

                lastMipSize = 4 * lastMipSize
                    - ((mipBlockWidth & 1) ? mipBlockHeight : 0)
                    - ((mipBlockHeight & 1) ? mipBlockWidth : 0)
                    - ((mipBlockWidth & mipBlockHeight & 1) ? 1 : 0);
            }
        }

        if (CanTrimLinearPadding(pSurfInfo))
        {
            ADDR_ASSERT((pOut->sliceSize * pOut->blockExtent.depth) <= (dataChainSize << blockSizeLog2));
        }
        else
        {
            ADDR_ASSERT((pOut->sliceSize * pOut->blockExtent.depth) == (dataChainSize << blockSizeLog2));
        }
    }
#endif
}


/**
************************************************************************************************************************
*   Gfx12Lib::HwlCalcBlockSize
*
*   @brief
*       Determines the extent, in pixels of a swizzle block.
*
*   @return
************************************************************************************************************************
*/
VOID Gfx12Lib::HwlCalcBlockSize(
    const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,
    ADDR_EXTENT3D*                                 pExtent
    ) const
{
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pSurfInfo = pIn->pSurfInfo;
    const UINT_32                           log2BlkSize = GetBlockSizeLog2(pSurfInfo->swizzleMode);
    const UINT_32 eleBytes     = pSurfInfo->bpp >> 3;
    const UINT_32 log2EleBytes = Log2(eleBytes);

    if (IsLinear(pSurfInfo->swizzleMode))
    {
        // 1D swizzle mode doesn't support MSAA, so there is no need to consider log2(samples)
        pExtent->width  = 1 << (log2BlkSize - log2EleBytes);
        pExtent->height = 1;
        pExtent->depth  = 1;
    }
    else if (Is3dSwizzle(pSurfInfo->swizzleMode))
    {
        // 3D swizlze mode doesn't support MSAA, so there is no need to consider log2(samples)
        const UINT_32 base             = (log2BlkSize / 3) - (log2EleBytes / 3);
        const UINT_32 log2BlkSizeMod3  = log2BlkSize % 3;
        const UINT_32 log2EleBytesMod3 = log2EleBytes % 3;

        UINT_32  x = base;
        UINT_32  y = base;
        UINT_32  z = base;

        if (log2BlkSizeMod3 > 0)
        {
            x++;
        }

        if (log2BlkSizeMod3 > 1)
        {
            z++;
        }

        if (log2EleBytesMod3 > 0)
        {
            x--;
        }

        if (log2EleBytesMod3 > 1)
        {
            z--;
        }

        pExtent->width  = 1u << x;
        pExtent->height = 1u << y;
        pExtent->depth  = 1u << z;
    }
    else
    {
        // Only 2D swizzle mode supports MSAA...
        // Since for gfx12 MSAA is unconditionally supported by all 2D swizzle modes, we don't need to restrict samples
        // to be 1 for ADDR3_256B_2D and ADDR3_4KB_2D as gfx10/11 did.
        const UINT_32 log2Samples = Log2(pSurfInfo->numSamples);
        const UINT_32 log2Width   = (log2BlkSize  >> 1)  -
                                    (log2EleBytes >> 1)  -
                                    (log2Samples  >> 1)  -
                                    (log2EleBytes & log2Samples & 1);
        const UINT_32 log2Height  = (log2BlkSize  >> 1)  -
                                    (log2EleBytes >> 1)  -
                                    (log2Samples  >> 1)  -
                                    ((log2EleBytes | log2Samples) & 1);

        // Return the extent in actual units, not log2
        pExtent->width  = 1u << log2Width;
        pExtent->height = 1u << log2Height;
        pExtent->depth  = 1;
    }
}

/**
************************************************************************************************************************
*   Gfx12Lib::HwlGetMipInTailMaxSize
*
*   @brief
*       Determines the max size of a mip level that fits in the mip-tail.
*
*   @return
************************************************************************************************************************
*/
ADDR_EXTENT3D Gfx12Lib::HwlGetMipInTailMaxSize(
    const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,
    const ADDR_EXTENT3D&                           blockDims) const
{
    ADDR_EXTENT3D mipTailDim = {};
    const Addr3SwizzleMode swizzleMode = pIn->pSurfInfo->swizzleMode;
    const UINT_32          log2BlkSize = GetBlockSizeLog2(swizzleMode);

    mipTailDim = blockDims;

    if (Is3dSwizzle(swizzleMode))
    {
        const UINT_32 dim = log2BlkSize % 3;

        if (dim == 0)
        {
            mipTailDim.height >>= 1;
        }
        else if (dim == 1)
        {
            mipTailDim.width >>= 1;
        }
        else
        {
            mipTailDim.depth >>= 1;
        }
    }
    else
    {
        if ((log2BlkSize % 2) == 0)
        {
            mipTailDim.width >>= 1;
        }
        else
        {
            mipTailDim.height >>= 1;
        }
    }
    return mipTailDim;
}

} // V3
} // Addr
