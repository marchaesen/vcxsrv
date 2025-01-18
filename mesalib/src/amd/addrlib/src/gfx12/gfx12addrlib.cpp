/*
************************************************************************************************************************
*
*  Copyright (C) 2022-2024 Advanced Micro Devices, Inc. All rights reserved.
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
#include "addrswizzler.h"

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

        // Skip linear equation (data table is not useful for 2D/3D images-- only contains x-coordinate bits)
        if (IsValidSwMode(swMode) && (IsLinear(swMode) == false))
        {
            const UINT_32 maxMsaa = Is2dSwizzle(swMode) ? MaxNumMsaaRates : 1;

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
                } // loop through bpp sizes
            } // loop through MSAA rates
        } // End check for valid non-linear modes
    } // loop through swizzle modes
}

/**
************************************************************************************************************************
*   Gfx12Lib::HwlGetEquationIndex
*
*   @brief
*       Return equationIndex by surface info input
*
*   @return
*       equationIndex
************************************************************************************************************************
*/
UINT_32 Gfx12Lib::HwlGetEquationIndex(
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pIn    ///< [in] input structure
    ) const
{
    UINT_32 equationIdx = ADDR_INVALID_EQUATION_INDEX;

    if ((pIn->resourceType == ADDR_RSRC_TEX_2D) ||
        (pIn->resourceType == ADDR_RSRC_TEX_3D))
    {
        equationIdx = GetEquationTableEntry(pIn->swizzleMode, Log2(pIn->numSamples), Log2(pIn->bpp >> 3));
    }

    return equationIdx;
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

        if (IsValidSwMode(swMode))
        {
            surfaceInfo.swizzleMode = swMode;
            const UINT_32 maxMsaa   = Is2dSwizzle(swMode) ? MaxNumMsaaRates : 1;

            for (UINT_32 msaaIdx = 0; msaaIdx < maxMsaa; msaaIdx++)
            {
                surfaceInfo.numSamples = (1u << msaaIdx);
                for (UINT_32 elementBytesLog2 = 0; elementBytesLog2 < MaxElementBytesLog2; elementBytesLog2++)
                {
                    surfaceInfo.bpp = (1u << (elementBytesLog2 + 3));
                    ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT input{ &surfaceInfo };
                    ComputeBlockDimensionForSurf(&input, &m_blockDimensionTable[swModeIdx][msaaIdx][elementBytesLog2]);
                } // end loop through bpp sizes
            } // end loop through MSAA rates
        } // end check for valid swizzle modes
    } // end loop through swizzle modes
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
    const BOOL_32        is3d             = (pSurfInfo->resourceType == ADDR_RSRC_TEX_3D);
    const UINT_32        bytesPerPixel    = pSurfInfo->bpp >> 3;
    const UINT_32        elementBytesLog2 = Log2(bytesPerPixel);
    const UINT_32        samplesLog2      = Log2(pSurfInfo->numSamples);

    // Calculate the width/height/depth for the given microblock, because the mip offset calculation
    // is in units of microblocks but we want it in elements.
    ADDR_EXTENT3D        microBlockExtent = HwlGetMicroBlockSize(pIn);
    const ADDR_EXTENT3D  tailMaxDim       = GetMipTailDim(pIn, pOut->blockExtent);
    const UINT_32        blockSizeLog2    = GetBlockSizeLog2(pSurfInfo->swizzleMode);

    UINT_32 pitch  = tailMaxDim.width;
    UINT_32 height = tailMaxDim.height;
    UINT_32 depth  = (is3d ? PowTwoAlign(mipExtentFirstInTail.depth, microBlockExtent.depth) : 1);

    const UINT_32 tailMaxDepth   = (is3d ? (depth / microBlockExtent.depth) : 1);

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

            pOut->pMipInfo[i].mipTailCoordX = mipX * microBlockExtent.width;
            pOut->pMipInfo[i].mipTailCoordY = mipY * microBlockExtent.height;
            pOut->pMipInfo[i].mipTailCoordZ = 0;
        }
        if (IsLinear(pSurfInfo->swizzleMode))
        {
            pitch = Max(pitch >> 1, 1u);
        }
        else
        {
            pOut->pMipInfo[i].pitch  = PowTwoAlign(pitch,  microBlockExtent.width);
            pOut->pMipInfo[i].height = PowTwoAlign(height, microBlockExtent.height);
            pOut->pMipInfo[i].depth  = PowTwoAlign(depth,  microBlockExtent.depth);
            pitch  = Max(pitch >> 1,  1u);
            height = Max(height >> 1, 1u);
            depth  = Max(depth >> 1,  1u);
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
    const bool           isLinear         = IsLinear(pSurfInfo->swizzleMode);

    UINT_32 firstMipInTail    = pSurfInfo->numMipLevels;
    UINT_64 mipChainSliceSize = 0;
    UINT_64 mipChainSliceSizeDense  = 0;
    UINT_64 mipSize[MaxMipLevels];
    UINT_64 mipSliceSize[MaxMipLevels];

    const BOOL_32 useCustomPitch    = UseCustomPitch(pSurfInfo);
    for (UINT_32 mipIdx = 0; mipIdx < pSurfInfo->numMipLevels; mipIdx++)
    {
        const ADDR_EXTENT3D  mipExtents = GetMipExtent(mip0Dims, mipIdx);

        if (Lib::SupportsMipTail(pSurfInfo->swizzleMode) &&
            (pSurfInfo->numMipLevels > 1)                &&
            IsInMipTail(tailMaxDim, mipExtents, maxMipsInTail, pSurfInfo->numMipLevels - mipIdx))
        {
            firstMipInTail          = mipIdx;
            mipChainSliceSize      += blockSize / pOut->blockExtent.depth;
            mipChainSliceSizeDense += blockSize / pOut->blockExtent.depth;
            break;
        }
        else
        {
            UINT_32 pitchImgData   = 0u;
            UINT_32 pitchSliceSize = 0u;
            if (isLinear)
            {
                // The slice size of a linear image is calculated as if the "pitch" is 256 byte aligned.
                // However, the rendering pitch is aligned to 128 bytes, and that is what needs to be reported
                // to our clients in the normal 'pitch' field.
                // Note this is NOT the same as the total size of the image being aligned to 256 bytes!
                pitchImgData   = (useCustomPitch ? pOut->pitch : PowTwoAlign(mipExtents.width, 128u / bytesPerPixel));
                pitchSliceSize = PowTwoAlign(pitchImgData, blockSize / bytesPerPixel);
            }
            else
            {
                pitchImgData   = PowTwoAlign(mipExtents.width, pOut->blockExtent.width);
                pitchSliceSize = pitchImgData;
            }

            UINT_32 height = UseCustomHeight(pSurfInfo)
                                        ? pOut->height
                                        : PowTwoAlign(mipExtents.height, pOut->blockExtent.height);
            const UINT_32 depth  = PowTwoAlign(mipExtents.depth, pOut->blockExtent.depth);

            if (isLinear && pSurfInfo->flags.denseSliceExact && ((pitchImgData % blockSize) != 0))
            {
                // If we want size to exactly equal (data)pitch * height, make sure that value is 256B aligned.
                // Essentially, if the pitch is less aligned, ensure the height is padded so total alignment is 256B.
                ADDR_ASSERT((blockSize % 128) == 0);
                height = PowTwoAlign(height, blockSize / 128u);
            }

            // The original "blockExtent" calculation does subtraction of logs (i.e., division) to get the
            // sizes.  We aligned our pitch and height to those sizes, which means we need to multiply the various
            // factors back together to get back to the slice size.
            UINT_64 sizeExceptPitch = static_cast<UINT_64>(height) * pSurfInfo->numSamples * (pSurfInfo->bpp >> 3);
            UINT_64 sliceSize       = static_cast<UINT_64>(pitchSliceSize) * sizeExceptPitch;
            UINT_64 sliceDataSize   = PowTwoAlign(static_cast<UINT_64>(pitchImgData) * sizeExceptPitch,
                                                  static_cast<UINT_64>(blockSize));

            UINT_64 hwSliceSize     = sliceSize * pOut->blockExtent.depth;
            ADDR_ASSERT(PowTwoAlign(hwSliceSize, static_cast<UINT_64>(blockSize)) == hwSliceSize);

            if ((mipIdx == 0) && CanTrimLinearPadding(pSurfInfo))
            {
                // When this is the last linear subresource of the whole image (as laid out in memory), then we don't
                // need to worry about the real slice size and can reduce it to the end of the image data (or some
                // inflated value to meet a custom depth pitch)
                pitchSliceSize = pitchImgData;
                if (UseCustomHeight(pSurfInfo))
                {
                    sliceSize = pSurfInfo->sliceAlign;
                }
                else
                {
                    sliceSize = sliceDataSize;
                }
                // CanTrimLinearPadding is always false for 3D swizzles, so block depth is always 1.
                hwSliceSize = sliceSize;
            }

            mipSize[mipIdx]         = sliceSize * depth;
            mipSliceSize[mipIdx]    = hwSliceSize;
            mipChainSliceSize      += sliceSize;
            mipChainSliceSizeDense += (mipIdx == 0) ? sliceDataSize : sliceSize;

            if (pOut->pMipInfo != NULL)
            {
                pOut->pMipInfo[mipIdx].pitch         = pitchImgData;
                pOut->pMipInfo[mipIdx].pitchForSlice = pitchSliceSize;
                pOut->pMipInfo[mipIdx].height        = height;
                pOut->pMipInfo[mipIdx].depth         = depth;
            }
        }
    }

    pOut->sliceSize            = mipChainSliceSize;
    pOut->sliceSizeDensePacked = mipChainSliceSizeDense;
    pOut->surfSize             = mipChainSliceSize * pOut->numSlices;
    pOut->mipChainInTail       = (firstMipInTail == 0) ? TRUE : FALSE;
    pOut->firstMipIdInTail     = firstMipInTail;

    if (pOut->pMipInfo != NULL)
    {
        if (isLinear)
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
        // - with linear images that have only one slice, we may trim and use the pitch alignment for size.
        ADDR_ASSERT(((pOut->sliceSize * pOut->blockExtent.depth) %
                     GetBlockSize(pSurfInfo->swizzleMode, CanTrimLinearPadding(pSurfInfo))) == 0);
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
*   Gfx12Lib::HwlComputeSurfaceAddrFromCoordLinear
*
*   @brief
*       Internal function to calculate address from coord for linear swizzle surface
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx12Lib::HwlComputeSurfaceAddrFromCoordLinear(
    const ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,         ///< [in] input structure
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT*          pSurfInfoIn, ///< [in] input structure
    ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT*      pOut         ///< [out] output structure
    ) const
{
    ADDR3_MIP_INFO mipInfo[MaxMipLevels];
    ADDR_ASSERT(pIn->numMipLevels <= MaxMipLevels);

    ADDR3_COMPUTE_SURFACE_INFO_OUTPUT surfInfoOut = {0};
    surfInfoOut.size     = sizeof(surfInfoOut);
    surfInfoOut.pMipInfo = mipInfo;

    ADDR_E_RETURNCODE returnCode = ComputeSurfaceInfo(pSurfInfoIn, &surfInfoOut);

    if (returnCode == ADDR_OK)
    {
        pOut->addr        = (surfInfoOut.sliceSize * pIn->slice) +
                            mipInfo[pIn->mipId].offset +
                            (pIn->y * mipInfo[pIn->mipId].pitch + pIn->x) * (pIn->bpp >> 3);

        pOut->bitPosition = 0;
    }

    return returnCode;
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

        // Addr3 equation table excludes linear swizzle mode, and fortunately HwlComputeSurfaceAddrFromCoordTiled() is
        // only called for non-linear swizzle mode.
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
*   Gfx12Lib::HwlCopyMemToSurface
*
*   @brief
*       Copy multiple regions from memory to a non-linear surface. 
*
*   @return
*       Error or success.
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx12Lib::HwlCopyMemToSurface(
    const ADDR3_COPY_MEMSURFACE_INPUT*  pIn,
    const ADDR3_COPY_MEMSURFACE_REGION* pRegions,
    UINT_32                             regionCount
    ) const
{
    // Copy memory to tiled surface. We will use the 'swizzler' object to dispatch to a version of the copy routine
    // optimized for a particular micro-swizzle mode if available.
    ADDR3_COMPUTE_SURFACE_INFO_INPUT  localIn  = {0};
    ADDR3_COMPUTE_SURFACE_INFO_OUTPUT localOut = {0};
    ADDR3_MIP_INFO                    mipInfo[MaxMipLevels] = {{0}};
    ADDR_ASSERT(pIn->numMipLevels <= MaxMipLevels);
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (pIn->numSamples > 1)
    {
        // TODO: MSAA
        returnCode = ADDR_NOTIMPLEMENTED;
    }

    localIn.size         = sizeof(localIn);
    localIn.flags        = pIn->flags;
    localIn.swizzleMode  = pIn->swizzleMode;
    localIn.resourceType = pIn->resourceType;
    localIn.format       = pIn->format;
    localIn.bpp          = pIn->bpp;
    localIn.width        = Max(pIn->unAlignedDims.width,  1u);
    localIn.height       = Max(pIn->unAlignedDims.height, 1u);
    localIn.numSlices    = Max(pIn->unAlignedDims.depth,  1u);
    localIn.numMipLevels = Max(pIn->numMipLevels,         1u);
    localIn.numSamples   = Max(pIn->numSamples,           1u);

    localOut.size     = sizeof(localOut);
    localOut.pMipInfo = mipInfo;

    if (returnCode == ADDR_OK)
    {
        returnCode = ComputeSurfaceInfo(&localIn, &localOut);
    }

    LutAddresser addresser = LutAddresser();
    UnalignedCopyMemImgFunc pfnCopyUnaligned = nullptr; 
    if (returnCode == ADDR_OK)
    {
        const UINT_32 blkSizeLog2 = GetBlockSizeLog2(pIn->swizzleMode);
        const ADDR_SW_PATINFO* pPatInfo = GetSwizzlePatternInfo(pIn->swizzleMode,
                                                                Log2(pIn->bpp >> 3),
                                                                pIn->numSamples);

        ADDR_BIT_SETTING fullSwizzlePattern[Log2Size256K] = {};
        GetSwizzlePatternFromPatternInfo(pPatInfo, fullSwizzlePattern);
        addresser.Init(fullSwizzlePattern, Log2Size256K, localOut.blockExtent, blkSizeLog2);
        pfnCopyUnaligned = addresser.GetCopyMemImgFunc();
        if (pfnCopyUnaligned == nullptr)
        {
            ADDR_ASSERT_ALWAYS(); // What format is this?
            returnCode = ADDR_INVALIDPARAMS;
        }
    }

    if (returnCode == ADDR_OK)
    {
        for (UINT_32  regionIdx = 0; regionIdx < regionCount; regionIdx++)
        {
            const ADDR3_COPY_MEMSURFACE_REGION* pCurRegion = &pRegions[regionIdx];
            const ADDR3_MIP_INFO* pMipInfo = &mipInfo[pCurRegion->mipId];
            UINT_64 mipOffset = pIn->singleSubres ? 0 : pMipInfo->macroBlockOffset;
            UINT_32 yBlks = pMipInfo->pitch / localOut.blockExtent.width;

            UINT_32 xStart = pCurRegion->x + pMipInfo->mipTailCoordX;
            UINT_32 yStart = pCurRegion->y + pMipInfo->mipTailCoordY;
            UINT_32 sliceStart = pCurRegion->slice + pMipInfo->mipTailCoordZ;

            for (UINT_32 slice = sliceStart; slice < (sliceStart + pCurRegion->copyDims.depth); slice++)
            {
                // The copy functions take the base address of the hardware slice, not the logical slice. Those are
                // not the same thing in 3D swizzles. Logical slices within 3D swizzles are handled by sliceXor
                // for unaligned copies.
                UINT_32 sliceBlkStart = PowTwoAlignDown(slice, localOut.blockExtent.depth);
                UINT_32 sliceXor = pIn->pbXor ^ addresser.GetAddressZ(slice);

                UINT_64 memOffset = ((slice - pCurRegion->slice) * pCurRegion->memSlicePitch);
                UINT_64 imgOffset = mipOffset + (sliceBlkStart * localOut.sliceSize);

                ADDR_COORD2D sliceOrigin = { xStart, yStart };
                ADDR_EXTENT2D sliceExtent = { pCurRegion->copyDims.width, pCurRegion->copyDims.height };

                pfnCopyUnaligned(VoidPtrInc(pIn->pMappedSurface, imgOffset),
                                 VoidPtrInc(pCurRegion->pMem, memOffset),
                                 pCurRegion->memRowPitch,
                                 yBlks,
                                 sliceOrigin,
                                 sliceExtent,
                                 sliceXor,
                                 addresser);
            }
        }
    }
    return returnCode;
}

/**
************************************************************************************************************************
*   Gfx12Lib::HwlCopySurfaceToMem
*
*   @brief
*       Copy multiple regions from a non-linear surface to memory. 
*
*   @return
*       Error or success.
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx12Lib::HwlCopySurfaceToMem(
    const ADDR3_COPY_MEMSURFACE_INPUT*  pIn,
    const ADDR3_COPY_MEMSURFACE_REGION* pRegions,
    UINT_32                             regionCount
    ) const
{
    // Copy memory to tiled surface. We will use the 'swizzler' object to dispatch to a version of the copy routine
    // optimized for a particular micro-swizzle mode if available.
    ADDR3_COMPUTE_SURFACE_INFO_INPUT  localIn  = {0};
    ADDR3_COMPUTE_SURFACE_INFO_OUTPUT localOut = {0};
    ADDR3_MIP_INFO                    mipInfo[MaxMipLevels] = {{0}};
    ADDR_ASSERT(pIn->numMipLevels <= MaxMipLevels);
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (pIn->numSamples > 1)
    {
        // TODO: MSAA
        returnCode = ADDR_NOTIMPLEMENTED;
    }

    localIn.size         = sizeof(localIn);
    localIn.flags        = pIn->flags;
    localIn.swizzleMode  = pIn->swizzleMode;
    localIn.resourceType = pIn->resourceType;
    localIn.format       = pIn->format;
    localIn.bpp          = pIn->bpp;
    localIn.width        = Max(pIn->unAlignedDims.width,  1u);
    localIn.height       = Max(pIn->unAlignedDims.height, 1u);
    localIn.numSlices    = Max(pIn->unAlignedDims.depth,  1u);
    localIn.numMipLevels = Max(pIn->numMipLevels,         1u);
    localIn.numSamples   = Max(pIn->numSamples,           1u);

    localOut.size     = sizeof(localOut);
    localOut.pMipInfo = mipInfo;

    if (returnCode == ADDR_OK)
    {
        returnCode = ComputeSurfaceInfo(&localIn, &localOut);
    }

    LutAddresser addresser = LutAddresser();
    UnalignedCopyMemImgFunc pfnCopyUnaligned = nullptr; 
    if (returnCode == ADDR_OK)
    {
        const UINT_32 blkSizeLog2 = GetBlockSizeLog2(pIn->swizzleMode);
        const ADDR_SW_PATINFO* pPatInfo = GetSwizzlePatternInfo(pIn->swizzleMode,
                                                                Log2(pIn->bpp >> 3),
                                                                pIn->numSamples);

        ADDR_BIT_SETTING fullSwizzlePattern[Log2Size256K] = {};
        GetSwizzlePatternFromPatternInfo(pPatInfo, fullSwizzlePattern);
        addresser.Init(fullSwizzlePattern, Log2Size256K, localOut.blockExtent, blkSizeLog2);
        pfnCopyUnaligned = addresser.GetCopyImgMemFunc();
        if (pfnCopyUnaligned == nullptr)
        {
            ADDR_ASSERT_ALWAYS(); // What format is this?
            returnCode = ADDR_INVALIDPARAMS;
        }
    }

    if (returnCode == ADDR_OK)
    {
        for (UINT_32  regionIdx = 0; regionIdx < regionCount; regionIdx++)
        {
            const ADDR3_COPY_MEMSURFACE_REGION* pCurRegion = &pRegions[regionIdx];
            const ADDR3_MIP_INFO* pMipInfo = &mipInfo[pCurRegion->mipId];
            UINT_64 mipOffset = pIn->singleSubres ? 0 : pMipInfo->macroBlockOffset;
            UINT_32 yBlks = pMipInfo->pitch / localOut.blockExtent.width;

            UINT_32 xStart = pCurRegion->x + pMipInfo->mipTailCoordX;
            UINT_32 yStart = pCurRegion->y + pMipInfo->mipTailCoordY;
            UINT_32 sliceStart = pCurRegion->slice + pMipInfo->mipTailCoordZ;

            for (UINT_32 slice = sliceStart; slice < (sliceStart + pCurRegion->copyDims.depth); slice++)
            {
                // The copy functions take the base address of the hardware slice, not the logical slice. Those are
                // not the same thing in 3D swizzles. Logical slices within 3D swizzles are handled by sliceXor
                // for unaligned copies.
                UINT_32 sliceBlkStart = PowTwoAlignDown(slice, localOut.blockExtent.depth);
                UINT_32 sliceXor = pIn->pbXor ^ addresser.GetAddressZ(slice);

                UINT_64 memOffset = ((slice - pCurRegion->slice) * pCurRegion->memSlicePitch);
                UINT_64 imgOffset = mipOffset + (sliceBlkStart * localOut.sliceSize);

                ADDR_COORD2D sliceOrigin = { xStart, yStart };
                ADDR_EXTENT2D sliceExtent = { pCurRegion->copyDims.width, pCurRegion->copyDims.height };

                pfnCopyUnaligned(VoidPtrInc(pIn->pMappedSurface, imgOffset),
                                 VoidPtrInc(pCurRegion->pMem, memOffset),
                                 pCurRegion->memRowPitch,
                                 yBlks,
                                 sliceOrigin,
                                 sliceExtent,
                                 sliceXor,
                                 addresser);
            }
        }
    }
    return returnCode;
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

                // Addr3 equation table excludes linear swizzle mode, and fortunately when calling
                // HwlComputeSlicePipeBankXor the swizzle mode is non-linear, so we don't need to worry about negative
                // table index.
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
*   Gfx12Lib::HwlGetMicroBlockSize
*
*   @brief
*       Determines the dimensions of a 256B microblock
*
*   @return
************************************************************************************************************************
*/
ADDR_EXTENT3D Gfx12Lib::HwlGetMicroBlockSize(
    const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn
    ) const
{
    ADDR_EXTENT3D out = {};
    INT_32 widthLog2  = 0;
    INT_32 heightLog2 = 0;
    INT_32 depthLog2  = 0;
    Addr3SwizzleMode swMode    = pIn->pSurfInfo->swizzleMode;
    UINT_32          bppLog2   = Log2(pIn->pSurfInfo->bpp >> 3);
    UINT_32          blockBits = 8 - bppLog2;
    if (IsLinear(swMode))
    {
        widthLog2 = blockBits;
    }
    else if (Is2dSwizzle(swMode))
    {
        widthLog2  = (blockBits >> 1) + (blockBits & 1);
        heightLog2 = (blockBits >> 1);
    }
    else
    {
        ADDR_ASSERT(Is3dSwizzle(swMode));
        depthLog2  = (blockBits / 3) + (((blockBits % 3) > 0) ? 1 : 0);
        widthLog2  = (blockBits / 3) + (((blockBits % 3) > 1) ? 1 : 0);
        heightLog2 = (blockBits / 3);
    }
    out.width  = 1 << widthLog2;
    out.height = 1 << heightLog2;
    out.depth  = 1 << depthLog2;
    return out;
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


/**
************************************************************************************************************************
*   Lib::GetPossibleSwizzleModes
*
*   @brief
*       GFX12 specific implementation of Addr3GetPossibleSwizzleModes
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx12Lib::HwlGetPossibleSwizzleModes(
     const ADDR3_GET_POSSIBLE_SWIZZLE_MODE_INPUT* pIn,    ///< [in] input structure
     ADDR3_GET_POSSIBLE_SWIZZLE_MODE_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    const ADDR3_SURFACE_FLAGS flags = pIn->flags;

    if (pIn->bpp == 96)
    {
        pOut->validModes.swLinear = 1;
    }
    // Depth/Stencil images can't be linear and must be 2D swizzle modes.
    // These three are related to DB block that supports only SW_64KB_2D and SW_256KB_2D for DSV.
    else if (flags.depth || flags.stencil)
    {
        pOut->validModes.sw2d64kB  = 1;
        pOut->validModes.sw2d256kB = 1;
    }
    // The organization of elements in the hierarchical surface is the same as any other surface, and it can support
    // any 2D swizzle mode (SW_256_2D, SW_4KB_2D, SW_64KB_2D, or SW_256KB_2D).  The swizzle mode can be selected
    // orthogonally to the underlying z or stencil surface.
    else if (pIn->flags.hiZHiS)
    {
        pOut->validModes.sw2d256B  = 1;
        pOut->validModes.sw2d4kB   = 1;
        pOut->validModes.sw2d64kB  = 1;
        pOut->validModes.sw2d256kB = 1;
    }
    // MSAA can't be linear and must be 2D swizzle modes.
    else if (pIn->numSamples > 1)
    {
        pOut->validModes.sw2d256B  = 1;
        pOut->validModes.sw2d4kB   = 1;
        pOut->validModes.sw2d64kB  = 1;
        pOut->validModes.sw2d256kB = 1;
    }
    // Some APIs (like Vulkan) require that PRT should always use 64KB blocks
    else if (flags.standardPrt)
    {
        if (IsTex3d(pIn->resourceType) && (flags.view3dAs2dArray == 0))
        {
            pOut->validModes.sw3d64kB = 1;
        }
        else
        {
            pOut->validModes.sw2d64kB = 1;
        }
    }
    else if (// Block-compressed images need to be either using 2D or linear swizzle modes.
             flags.blockCompressed                 ||
             // Only 3D w/ view3dAs2dArray == 0 will use 1D/2D block swizzle modes
             (IsTex3d(pIn->resourceType) == FALSE) || flags.view3dAs2dArray ||
             //      NV12 and P010 support
             //      SW_LINEAR, SW_256B_2D, SW_4KB_2D, SW_64KB_2D, SW_256KB_2D
             // There could be more multimedia formats that require more hw specific tiling modes...
             flags.nv12                            || flags.p010)
    {
        // Linear is not allowed for VRS images.
        if (flags.isVrsImage == 0)
        {
            pOut->validModes.swLinear = 1;
        }

        // 3D resources can't use SW_256B_2D
        if (IsTex3d(pIn->resourceType) == FALSE)
        {
            pOut->validModes.sw2d256B = 1;
        }
        pOut->validModes.sw2d4kB   = 1;
        pOut->validModes.sw2d64kB  = 1;
        pOut->validModes.sw2d256kB = 1;
    }
    else if (IsTex3d(pIn->resourceType))
    {
        // An eventual determination would be based on pal setting of height_watermark and depth_watermark.
        // However, we just adopt the simpler logic currently.
        // For 3D images w/ view3dAs2dArray = 0, SW_3D is preferred.
        // For 3D images w/ view3dAs2dArray = 1, it should go to 2D path above.
        // Enable linear since client may force linear tiling for 3D texture that does not set view3dAs2dArray.
        pOut->validModes.swLinear  = 1;
        pOut->validModes.sw3d4kB   = 1;
        pOut->validModes.sw3d64kB  = 1;
        pOut->validModes.sw3d256kB = 1;
    }

    // If client specifies a max alignment, remove swizzles that require alignment beyond it.
    if (pIn->maxAlign != 0)
    {
        if (pIn->maxAlign < Size256K)
        {
            pOut->validModes.value &= ~Blk256KBSwModeMask;
        }

        if (pIn->maxAlign < Size64K)
        {
            pOut->validModes.value &= ~Blk64KBSwModeMask;
        }

        if (pIn->maxAlign < Size4K)
        {
            pOut->validModes.value &= ~Blk4KBSwModeMask;
        }

        if (pIn->maxAlign < Size256)
        {
            pOut->validModes.value &= ~Blk256BSwModeMask;
        }
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Gfx12Lib::HwlComputeStereoInfo
*
*   @brief
*       Compute height alignment and right eye pipeBankXor for stereo surface
*
*   @return
*       Error code
*
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx12Lib::HwlComputeStereoInfo(
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pIn,        ///< Compute surface info
    UINT_32*                                pAlignY,    ///< Stereo requested additional alignment in Y
    UINT_32*                                pRightXor   ///< Right eye xor
    ) const
{
    ADDR_E_RETURNCODE ret = ADDR_OK;

    *pRightXor = 0;

    const UINT_32 elemLog2    = Log2(pIn->bpp >> 3);
    const UINT_32 samplesLog2 = Log2(pIn->numSamples);
    const UINT_32 eqIndex     = GetEquationTableEntry(pIn->swizzleMode, samplesLog2, elemLog2);

    if (eqIndex != ADDR_INVALID_EQUATION_INDEX)
    {
        const UINT_32 blkSizeLog2 = GetBlockSizeLog2(pIn->swizzleMode);

        UINT_32 yMax     = 0;
        UINT_32 yPosMask = 0;

        // First get "max y bit"
        for (UINT_32 i = m_pipeInterleaveLog2; i < blkSizeLog2; i++)
        {
            ADDR_ASSERT(m_equationTable[eqIndex].addr[i].valid == 1);

            if ((m_equationTable[eqIndex].addr[i].channel == 1) &&
                (m_equationTable[eqIndex].addr[i].index > yMax))
            {
                yMax = m_equationTable[eqIndex].addr[i].index;
            }
        }

        // Then loop again for populating a position mask of "max Y bit"
        for (UINT_32 i = m_pipeInterleaveLog2; i < blkSizeLog2; i++)
        {
            if ((m_equationTable[eqIndex].addr[i].channel == 1) &&
                (m_equationTable[eqIndex].addr[i].index == yMax))
            {
                yPosMask |= 1u << i;
            }
        }

        const UINT_32 additionalAlign = 1 << yMax;

        if (additionalAlign >= *pAlignY)
        {
            *pAlignY = additionalAlign;

            const UINT_32 alignedHeight = PowTwoAlign(pIn->height, additionalAlign);

            if ((alignedHeight >> yMax) & 1)
            {
                *pRightXor = yPosMask >> m_pipeInterleaveLog2;
            }
        }
    }
    else
    {
        ret = ADDR_INVALIDPARAMS;
    }

    return ret;
}

/**
************************************************************************************************************************
*   Gfx12Lib::HwlValidateNonSwModeParams
*
*   @brief
*       Validate compute surface info params except swizzle mode
*
*   @return
*       TRUE if parameters are valid, FALSE otherwise
************************************************************************************************************************
*/
BOOL_32 Gfx12Lib::HwlValidateNonSwModeParams(
    const ADDR3_GET_POSSIBLE_SWIZZLE_MODE_INPUT* pIn
    ) const
{
    const ADDR3_SURFACE_FLAGS flags     = pIn->flags;
    const AddrResourceType    rsrcType  = pIn->resourceType;
    const BOOL_32             isVrs     = flags.isVrsImage;
    const BOOL_32             isStereo  = flags.qbStereo;
    const BOOL_32             isDisplay = flags.display;
    const BOOL_32             isMipmap  = (pIn->numMipLevels > 1);
    const BOOL_32             isMsaa    = (pIn->numSamples > 1);
    const UINT_32             bpp       = pIn->bpp;

    BOOL_32                   valid     = TRUE;
    if ((bpp == 0) || (bpp > 128) || (pIn->width == 0) || (pIn->numSamples > 8))
    {
        ADDR_ASSERT_ALWAYS();
        valid = FALSE;
    }

    // Resource type check
    if (IsTex1d(rsrcType))
    {
        if (isMsaa || isStereo || isVrs || isDisplay)
        {
            ADDR_ASSERT_ALWAYS();
            valid = FALSE;
        }
    }
    else if (IsTex2d(rsrcType))
    {
        if ((isMsaa && isMipmap) || (isStereo && isMsaa) || (isStereo && isMipmap) ||
            // VRS surface needs to be 8BPP format
            (isVrs && (bpp != 8)))
        {
            ADDR_ASSERT_ALWAYS();
            valid = FALSE;
        }
    }
    else if (IsTex3d(rsrcType))
    {
        if (isMsaa || isStereo || isVrs || isDisplay)
        {
            ADDR_ASSERT_ALWAYS();
            valid = FALSE;
        }
    }
    else
    {
        // An invalid resource type that is not 1D, 2D or 3D.
        ADDR_ASSERT_ALWAYS();
        valid = FALSE;
    }

    return valid;
}

} // V3
} // Addr
