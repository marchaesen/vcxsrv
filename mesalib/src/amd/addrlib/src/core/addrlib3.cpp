/*
************************************************************************************************************************
*
*  Copyright (C) 2007-2024 Advanced Micro Devices, Inc. All rights reserved.
*  SPDX-License-Identifier: MIT
*
***********************************************************************************************************************/


/**
************************************************************************************************************************
* @file  addrlib3.cpp
* @brief Contains the implementation for the AddrLib3 base class.
************************************************************************************************************************
*/

#include "addrinterface.h"
#include "addrlib3.h"
#include "addrcommon.h"

namespace Addr
{
namespace V3
{

////////////////////////////////////////////////////////////////////////////////////////////////////
//                               Constructor/Destructor
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
************************************************************************************************************************
*   Lib::Lib
*
*   @brief
*       Constructor for the Addr::V3::Lib class
*
************************************************************************************************************************
*/
Lib::Lib()
    :
    Addr::Lib(),
    m_pipesLog2(0),
    m_pipeInterleaveLog2(0),
    m_numEquations(0)
{
    Init();
}

/**
************************************************************************************************************************
*   Lib::Lib
*
*   @brief
*       Constructor for the AddrLib3 class with hClient as parameter
*
************************************************************************************************************************
*/
Lib::Lib(
    const Client* pClient)
    :
    Addr::Lib(pClient),
    m_pipesLog2(0),
    m_pipeInterleaveLog2(0),
    m_numEquations(0)
{
    Init();
}

/**
************************************************************************************************************************
*   Lib::Init
*
*   @brief
*       Initialization of class
*
************************************************************************************************************************
*/
void Lib::Init()
{
    memset(m_blockDimensionTable, 0, sizeof(m_blockDimensionTable));

    // There is no equation table entry for linear, so start at the "next" swizzle mode entry.
    for (UINT_32  swizzleModeIdx = ADDR3_LINEAR + 1; swizzleModeIdx < ADDR3_MAX_TYPE; swizzleModeIdx++)
    {
        for (UINT_32  msaaRateIdx = 0; msaaRateIdx < MaxNumMsaaRates; msaaRateIdx++)
        {
            for (UINT_32  log2BytesIdx = 0; log2BytesIdx < MaxElementBytesLog2; log2BytesIdx++)
            {
                SetEquationTableEntry(static_cast<Addr3SwizzleMode>(swizzleModeIdx),
                                      msaaRateIdx,
                                      log2BytesIdx,
                                      ADDR_INVALID_EQUATION_INDEX);
            }
        }
    }
}

/**
************************************************************************************************************************
*   Lib::~Lib
*
*   @brief
*       Destructor for the AddrLib2 class
*
************************************************************************************************************************
*/
Lib::~Lib()
{
}

/**
************************************************************************************************************************
*   Lib::GetLib
*
*   @brief
*       Get Addr::V3::Lib pointer
*
*   @return
*      An Addr::V2::Lib class pointer
************************************************************************************************************************
*/
Lib* Lib::GetLib(
    ADDR_HANDLE hLib)   ///< [in] handle of ADDR_HANDLE
{
    Addr::Lib* pAddrLib = Addr::Lib::GetLib(hLib);

    return static_cast<Lib*>(pAddrLib);
}

/**
************************************************************************************************************************
*   Lib::GetBlockSize
*
*   @brief
*       Returns the byte size of a block for the swizzle mode.
*
*   @return
*       Byte size of the block, zero if swizzle mode is invalid.
************************************************************************************************************************
*/
UINT_32  Lib::GetBlockSize(
    Addr3SwizzleMode  swizzleMode,
    BOOL_32           forPitch
    ) const
{
    return  (1 << GetBlockSizeLog2(swizzleMode, forPitch));
}

/**
************************************************************************************************************************
*   Lib::GetBlockSizeLog2
*
*   @brief
*       Returns the log2 of the byte size of a block for the swizzle mode.
*
*   @return
*       Byte size of the block, zero if swizzle mode is invalid.
************************************************************************************************************************
*/
UINT_32  Lib::GetBlockSizeLog2(
    Addr3SwizzleMode  swizzleMode,
    BOOL_32           forPitch
    ) const
{
    UINT_32  blockSize = 0;

    switch (swizzleMode)
    {
        case ADDR3_256B_2D:
            blockSize = 8;
            break;
        case ADDR3_4KB_2D:
        case ADDR3_4KB_3D:
            blockSize = 12;
            break;
        case ADDR3_64KB_2D:
        case ADDR3_64KB_3D:
            blockSize = 16;
            break;
        case ADDR3_256KB_2D:
        case ADDR3_256KB_3D:
            blockSize = 18;
            break;
        case ADDR3_LINEAR:
            blockSize = (forPitch ? 7 : 8);
            break;
        default:
            ADDR_ASSERT_ALWAYS();
            break;
    }

    return  blockSize;
}

/**
************************************************************************************************************************
*   Lib::ComputeSurfaceInfo
*
*   @brief
*       Interface function stub of ComputeSurfaceInfo.
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceInfo(
     const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pIn,    ///< [in] input structure
     ADDR3_COMPUTE_SURFACE_INFO_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR3_COMPUTE_SURFACE_INFO_INPUT)) ||
            (pOut->size != sizeof(ADDR3_COMPUTE_SURFACE_INFO_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    // Adjust incoming parameters.
    ADDR3_COMPUTE_SURFACE_INFO_INPUT localIn = *pIn;
    localIn.width        = Max(pIn->width, 1u);
    localIn.height       = Max(pIn->height, 1u);
    localIn.numMipLevels = Max(pIn->numMipLevels, 1u);
    localIn.numSlices    = Max(pIn->numSlices, 1u);
    localIn.numSamples   = Max(pIn->numSamples, 1u);

    UINT_32  expandX  = 1;
    UINT_32  expandY  = 1;
    ElemMode elemMode = ADDR_UNCOMPRESSED;

    if (returnCode == ADDR_OK)
    {
        // Set format to INVALID will skip this conversion
        if (localIn.format != ADDR_FMT_INVALID)
        {
            // Get compression/expansion factors and element mode which indicates compression/expansion
            localIn.bpp = GetElemLib()->GetBitsPerPixel(localIn.format,
                                                        &elemMode,
                                                        &expandX,
                                                        &expandY);

            // Special flag for 96 bit surface. 96 (or 48 if we support) bit surface's width is
            // pre-multiplied by 3 and bpp is divided by 3. So pitch alignment for linear-
            // aligned does not meet 64-pixel in real. We keep special handling in hwl since hw
            // restrictions are different.
            // Also Mip 1+ needs an element pitch of 32 bits so we do not need this workaround
            // but we use this flag to skip RestoreSurfaceInfo below
            if ((elemMode == ADDR_EXPANDED) && (expandX > 1))
            {
                ADDR_ASSERT(IsLinear(localIn.swizzleMode));
            }

            UINT_32 basePitch = 0;
            GetElemLib()->AdjustSurfaceInfo(elemMode,
                                            expandX,
                                            expandY,
                                            &localIn.bpp,
                                            &basePitch,
                                            &localIn.width,
                                            &localIn.height);

            // Overwrite these parameters if we have a valid format
        }

        if (localIn.bpp != 0)
        {
            localIn.width  = Max(localIn.width, 1u);
            localIn.height = Max(localIn.height, 1u);
        }
        else // Rule out some invalid parameters
        {
            returnCode = ADDR_INVALIDPARAMS;
        }
    }

    if (returnCode == ADDR_OK)
    {
        returnCode = ComputeSurfaceInfoSanityCheck(&localIn);
    }

    if (returnCode == ADDR_OK)
    {
        returnCode = HwlComputeSurfaceInfo(&localIn, pOut);

        if (returnCode == ADDR_OK)
        {
            pOut->bpp         = localIn.bpp;
            pOut->pixelPitch  = pOut->pitch;
            pOut->pixelHeight = pOut->height;

            if (localIn.format != ADDR_FMT_INVALID)
            {
                UINT_32 pixelBits = pOut->pixelBits;

                GetElemLib()->RestoreSurfaceInfo(elemMode,
                                                 expandX,
                                                 expandY,
                                                 &pOut->pixelBits,
                                                 &pOut->pixelPitch,
                                                 &pOut->pixelHeight);

                GetElemLib()->RestoreSurfaceInfo(elemMode,
                                                 expandX,
                                                 expandY,
                                                 &pixelBits,
                                                 &pOut->pixelMipChainPitch,
                                                 &pOut->pixelMipChainHeight);

                if ((localIn.numMipLevels > 1) && (pOut->pMipInfo != NULL))
                {
                    for (UINT_32 i = 0; i < localIn.numMipLevels; i++)
                    {
                        pOut->pMipInfo[i].pixelPitch  = pOut->pMipInfo[i].pitch;
                        pOut->pMipInfo[i].pixelHeight = pOut->pMipInfo[i].height;

                        GetElemLib()->RestoreSurfaceInfo(elemMode,
                                                         expandX,
                                                         expandY,
                                                         &pixelBits,
                                                         &pOut->pMipInfo[i].pixelPitch,
                                                         &pOut->pMipInfo[i].pixelHeight);
                    }
                }

                if (localIn.flags.qbStereo && (pOut->pStereoInfo != NULL))
                {
                    ComputeQbStereoInfo(pOut);
                }
            }

            SetEquationIndex(&localIn, pOut);
        }
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Lib::GetPossibleSwizzleModes
*
*   @brief
*       Populates pOut with a list of the possible swizzle modes for the described surface.
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::GetPossibleSwizzleModes(
    const ADDR3_GET_POSSIBLE_SWIZZLE_MODE_INPUT*  pIn,
    ADDR3_GET_POSSIBLE_SWIZZLE_MODE_OUTPUT*       pOut
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size  != sizeof(ADDR3_GET_POSSIBLE_SWIZZLE_MODE_INPUT)) ||
            (pOut->size != sizeof(ADDR3_GET_POSSIBLE_SWIZZLE_MODE_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if ((returnCode == ADDR_OK) && (HwlValidateNonSwModeParams(pIn) == FALSE))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    if (returnCode == ADDR_OK)
    {
        returnCode = HwlGetPossibleSwizzleModes(pIn, pOut);
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Lib::ComputeBlockDimensionForSurf
*
*   @brief
*       Internal function to get block width/height/depth in elements from surface input params.
*
*   @return
*       VOID
************************************************************************************************************************
*/
VOID Lib::ComputeBlockDimensionForSurf(
    const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,
    ADDR_EXTENT3D*                                 pExtent
    ) const
{
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pSurfInfo   = pIn->pSurfInfo;
    const UINT_32                           log2BlkSize = GetBlockSizeLog2(pSurfInfo->swizzleMode);

    HwlCalcBlockSize(pIn, pExtent);
}

/**
************************************************************************************************************************
*   Lib::GetMipTailDim
*
*   @brief
*       Internal function to get out max dimension of first level in mip tail
*
*   @return
*       Max Width/Height/Depth value of the first mip fitted in mip tail
************************************************************************************************************************
*/
ADDR_EXTENT3D Lib::GetMipTailDim(
    const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,
    const ADDR_EXTENT3D&                           blockDims
    ) const
{
    return HwlGetMipInTailMaxSize(pIn, blockDims);
}

/**
************************************************************************************************************************
*   Lib::ComputeSurfaceAddrFromCoord
*
*   @brief
*       Interface function stub of ComputeSurfaceAddrFromCoord.
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceAddrFromCoord(
    const ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,    ///< [in] input structure
    ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT*      pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT)) ||
            (pOut->size != sizeof(ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT localIn = *pIn;
    localIn.unAlignedDims.width  = Max(pIn->unAlignedDims.width,  1u);
    localIn.unAlignedDims.height = Max(pIn->unAlignedDims.height, 1u);
    localIn.unAlignedDims.depth  = Max(pIn->unAlignedDims.depth,  1u);
    localIn.numMipLevels         = Max(pIn->numMipLevels,         1u);
    localIn.numSamples           = Max(pIn->numSamples,           1u);

    if ((localIn.bpp < 8)                               ||
        (localIn.bpp > 128)                             ||
        ((localIn.bpp % 8) != 0)                        ||
        (localIn.sample >= localIn.numSamples)          ||
        (localIn.slice >= localIn.unAlignedDims.depth)  ||
        (localIn.mipId >= localIn.numMipLevels)         ||
        (IsTex3d(localIn.resourceType)                  &&
        (Valid3DMipSliceIdConstraint(localIn.unAlignedDims.depth, localIn.mipId, localIn.slice) == FALSE)))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    if (returnCode == ADDR_OK)
    {
        if (IsLinear(localIn.swizzleMode))
        {
            returnCode = ComputeSurfaceAddrFromCoordLinear(&localIn, pOut);
        }
        else
        {
            returnCode = ComputeSurfaceAddrFromCoordTiled(&localIn, pOut);
        }

        if (returnCode == ADDR_OK)
        {
            pOut->prtBlockIndex = static_cast<UINT_32>(pOut->addr / (64 * 1024));
        }
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Lib::CopyLinearSurface
*
*   @brief
*       Implements uncompressed linear copies between memory and images.
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::CopyLinearSurface(
    const ADDR3_COPY_MEMSURFACE_INPUT*  pIn,
    const ADDR3_COPY_MEMSURFACE_REGION* pRegions,
    UINT_32                             regionCount,
    bool                                surfaceIsDst) const
{
    ADDR3_COMPUTE_SURFACE_INFO_INPUT  localIn  = {0};
    ADDR3_COMPUTE_SURFACE_INFO_OUTPUT localOut = {0};
    ADDR3_MIP_INFO                    mipInfo[Addr3MaxMipLevels] = {{0}};
    ADDR_ASSERT(pIn->numMipLevels <= Addr3MaxMipLevels);
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (pIn->numSamples > 1)
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    localIn.size         = sizeof(localIn);
    localIn.flags        = pIn->flags;
    localIn.swizzleMode  = ADDR3_LINEAR;
    localIn.resourceType = pIn->resourceType;
    localIn.format       = pIn->format;
    localIn.bpp          = pIn->bpp;
    localIn.width        = Max(pIn->unAlignedDims.width,  1u);
    localIn.height       = Max(pIn->unAlignedDims.height, 1u);
    localIn.numSlices    = Max(pIn->unAlignedDims.depth,  1u);
    localIn.numMipLevels = Max(pIn->numMipLevels,         1u);
    localIn.numSamples   = Max(pIn->numSamples,           1u);

    if (localIn.numMipLevels <= 1)
    {
        localIn.pitchInElement = pIn->pitchInElement;
    }

    localOut.size     = sizeof(localOut);
    localOut.pMipInfo = mipInfo;

    if (returnCode == ADDR_OK)
    {
        returnCode = ComputeSurfaceInfo(&localIn, &localOut);
    }

    if (returnCode == ADDR_OK)
    {
        for (UINT_32 regionIdx = 0; regionIdx < regionCount; regionIdx++)
        {
            const ADDR3_COPY_MEMSURFACE_REGION* pCurRegion = &pRegions[regionIdx];

            void* pMipBase = VoidPtrInc(pIn->pMappedSurface,
                                        (pIn->singleSubres ? 0 : mipInfo[pCurRegion->mipId].offset));

            const size_t lineSizeBytes = (localIn.bpp >> 3) * pCurRegion->copyDims.width;
            const size_t lineImgPitchBytes = (localIn.bpp >> 3) * mipInfo[pCurRegion->mipId].pitch;

            for (UINT_32 sliceIdx = 0; sliceIdx < pCurRegion->copyDims.depth; sliceIdx++)
            {
                UINT_32 sliceCoord = sliceIdx + pCurRegion->slice;
                size_t imgOffsetInMip = (localOut.sliceSize * sliceCoord) +
                                        (lineImgPitchBytes * pCurRegion->y) +
                                        (pCurRegion->x * (pIn->bpp >> 3));
                size_t memOffset = sliceIdx * pCurRegion->memSlicePitch;

                for (UINT_32 yIdx = 0; yIdx < pCurRegion->copyDims.height; yIdx++)
                {
                    if (surfaceIsDst)
                    {
                        memcpy(VoidPtrInc(pMipBase, imgOffsetInMip),
                               VoidPtrInc(pCurRegion->pMem, memOffset),
                               lineSizeBytes);
                    }
                    else
                    {
                        memcpy(VoidPtrInc(pCurRegion->pMem, memOffset),
                               VoidPtrInc(pMipBase, imgOffsetInMip),
                               lineSizeBytes);
                    }

                    imgOffsetInMip += lineImgPitchBytes;
                    memOffset      += pCurRegion->memRowPitch;
                }
            }
        }
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Lib::CopyMemToSurface
*
*   @brief
*       Interface function stub of Addr3CopyMemToSurface.
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::CopyMemToSurface(
    const ADDR3_COPY_MEMSURFACE_INPUT*  pIn,
    const ADDR3_COPY_MEMSURFACE_REGION* pRegions,
    UINT_32                             regionCount) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if ((regionCount == 0) || (pRegions == NULL))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else if (GetFillSizeFieldsFlags() == TRUE)
    {
        if (pIn->size  != sizeof(ADDR3_COPY_MEMSURFACE_INPUT))
        {
            returnCode = ADDR_INVALIDPARAMS;
        }
        else
        {
            UINT_32 baseSlice    = pRegions[0].slice;
            UINT_32 baseMip      = pRegions[0].mipId;
            BOOL_32 singleSubres = pIn->singleSubres;
            for (UINT_32 i = 0; i < regionCount; i++)
            {
                if (pRegions[i].size != sizeof(ADDR3_COPY_MEMSURFACE_REGION))
                {
                    returnCode = ADDR_INVALIDPARAMS;
                    break;
                }
                if (singleSubres &&
                    ((pRegions[i].copyDims.depth != 1) ||
                     (pRegions[i].slice != baseSlice)  ||
                     (pRegions[i].mipId != baseMip)))
                {
                    // Copy will cover multiple/interleaved subresources, a
                    // mapped pointer to a single subres cannot be valid.
                    returnCode = ADDR_INVALIDPARAMS;
                    break;
                }
            }
        }
    }

    if (returnCode == ADDR_OK)
    {
        if (IsLinear(pIn->swizzleMode))
        {
            returnCode = CopyLinearSurface(pIn, pRegions, regionCount, true);
        }
        else
        {
            returnCode = HwlCopyMemToSurface(pIn, pRegions, regionCount);
        }
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Lib::CopySurfaceToMem
*
*   @brief
*       Interface function stub of Addr3CopySurfaceToMem.
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::CopySurfaceToMem(
    const ADDR3_COPY_MEMSURFACE_INPUT*  pIn,
    const ADDR3_COPY_MEMSURFACE_REGION* pRegions,
    UINT_32                             regionCount) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (regionCount == 0)
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else if (GetFillSizeFieldsFlags() == TRUE)
    {
        if (pIn->size  != sizeof(ADDR3_COPY_MEMSURFACE_INPUT))
        {
            returnCode = ADDR_INVALIDPARAMS;
        }
        else
        {
            UINT_32 baseSlice    = pRegions[0].slice;
            UINT_32 baseMip      = pRegions[0].mipId;
            BOOL_32 singleSubres = pIn->singleSubres;
            for (UINT_32 i = 0; i < regionCount; i++)
            {
                if (pRegions[i].size != sizeof(ADDR3_COPY_MEMSURFACE_REGION))
                {
                    returnCode = ADDR_INVALIDPARAMS;
                    break;
                }
                if (singleSubres &&
                    ((pRegions[i].copyDims.depth != 1) ||
                     (pRegions[i].slice != baseSlice)  ||
                     (pRegions[i].mipId != baseMip)))
                {
                    // Copy will cover multiple/interleaved subresources, a
                    // mapped pointer to a single subres cannot be valid.
                    returnCode = ADDR_INVALIDPARAMS;
                    break;
                }
            }
        }
    }

    if (returnCode == ADDR_OK)
    {
        if (IsLinear(pIn->swizzleMode))
        {
            returnCode = CopyLinearSurface(pIn, pRegions, regionCount, false);
        }
        else
        {
            returnCode = HwlCopySurfaceToMem(pIn, pRegions, regionCount);
        }
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Lib::ComputeSurfaceAddrFromCoord
*
*   @brief
*       Interface function stub of Addr3ComputePipeBankXor.
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputePipeBankXor(
    const ADDR3_COMPUTE_PIPEBANKXOR_INPUT* pIn,
    ADDR3_COMPUTE_PIPEBANKXOR_OUTPUT*      pOut)
{
    ADDR_E_RETURNCODE returnCode;

    if ((GetFillSizeFieldsFlags() == TRUE) &&
        ((pIn->size  != sizeof(ADDR3_COMPUTE_PIPEBANKXOR_INPUT)) ||
         (pOut->size != sizeof(ADDR3_COMPUTE_PIPEBANKXOR_OUTPUT))))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else
    {
        returnCode = HwlComputePipeBankXor(pIn, pOut);
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Lib::ComputeSurfaceAddrFromCoordLinear
*
*   @brief
*       Internal function to calculate address from coord for linear swizzle surface
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceAddrFromCoordLinear(
     const ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,    ///< [in] input structure
     ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;
    BOOL_32 valid = (pIn->numSamples <= 1);

    if (valid)
    {
        if (IsTex1d(pIn->resourceType))
        {
            valid = (pIn->y == 0);
        }
    }

    if (valid)
    {
        ADDR3_COMPUTE_SURFACE_INFO_INPUT  surfInfoIn = {0};

        surfInfoIn.size         = sizeof(surfInfoIn);
        surfInfoIn.flags        = pIn->flags;
        surfInfoIn.swizzleMode  = ADDR3_LINEAR;
        surfInfoIn.resourceType = pIn->resourceType;
        surfInfoIn.format       = ADDR_FMT_INVALID;
        surfInfoIn.bpp          = pIn->bpp;
        surfInfoIn.width        = Max(pIn->unAlignedDims.width,  1u);
        surfInfoIn.height       = Max(pIn->unAlignedDims.height, 1u);
        surfInfoIn.numSlices    = Max(pIn->unAlignedDims.depth,  1u);
        surfInfoIn.numMipLevels = Max(pIn->numMipLevels,         1u);
        surfInfoIn.numSamples   = Max(pIn->numSamples,           1u);

        if (surfInfoIn.numMipLevels <= 1)
        {
            surfInfoIn.pitchInElement = pIn->pitchInElement;
        }

        returnCode = HwlComputeSurfaceAddrFromCoordLinear(pIn, &surfInfoIn, pOut);
    }

    if (valid == FALSE)
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Lib::ComputeSurfaceAddrFromCoordTiled
*
*   @brief
*       Internal function to calculate address from coord for tiled swizzle surface
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceAddrFromCoordTiled(
     const ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,    ///< [in] input structure
     ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    return HwlComputeSurfaceAddrFromCoordTiled(pIn, pOut);
}

/**
************************************************************************************************************************
*   Lib::ComputeNonBlockCompressedView
*
*   @brief
*       Interface function stub of Addr3ComputeNonBlockCompressedView.
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeNonBlockCompressedView(
    const ADDR3_COMPUTE_NONBLOCKCOMPRESSEDVIEW_INPUT* pIn,
    ADDR3_COMPUTE_NONBLOCKCOMPRESSEDVIEW_OUTPUT*      pOut)
{
    ADDR_E_RETURNCODE returnCode;

    if ((GetFillSizeFieldsFlags() == TRUE) &&
        ((pIn->size  != sizeof(ADDR3_COMPUTE_NONBLOCKCOMPRESSEDVIEW_INPUT)) ||
         (pOut->size != sizeof(ADDR3_COMPUTE_NONBLOCKCOMPRESSEDVIEW_OUTPUT))))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else if (Is3dSwizzle(pIn->swizzleMode))
    {
        // 3D volume images using ADDR3_XX_3D is currently not supported.
        returnCode = ADDR_NOTSUPPORTED;
    }
    else
    {
        returnCode = HwlComputeNonBlockCompressedView(pIn, pOut);
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Lib::ComputeSubResourceOffsetForSwizzlePattern
*
*   @brief
*       Interface function stub of Addr3ComputeSubResourceOffsetForSwizzlePattern.
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSubResourceOffsetForSwizzlePattern(
    const ADDR3_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_INPUT* pIn,
    ADDR3_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_OUTPUT*      pOut)
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if ((GetFillSizeFieldsFlags() == TRUE) &&
        ((pIn->size  != sizeof(ADDR3_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_INPUT)) ||
         (pOut->size != sizeof(ADDR3_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_OUTPUT))))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else
    {
        HwlComputeSubResourceOffsetForSwizzlePattern(pIn, pOut);
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Lib::ComputeSlicePipeBankXor
*
*   @brief
*       Interface function stub of Addr3ComputeSlicePipeBankXor.
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSlicePipeBankXor(
    const ADDR3_COMPUTE_SLICE_PIPEBANKXOR_INPUT* pIn,
    ADDR3_COMPUTE_SLICE_PIPEBANKXOR_OUTPUT*      pOut)
{
    ADDR_E_RETURNCODE returnCode;

    if ((GetFillSizeFieldsFlags() == TRUE) &&
        ((pIn->size  != sizeof(ADDR3_COMPUTE_SLICE_PIPEBANKXOR_INPUT)) ||
         (pOut->size != sizeof(ADDR3_COMPUTE_SLICE_PIPEBANKXOR_OUTPUT))))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    if ((pIn->bpe != 0) &&
        (pIn->bpe != 8) &&
        (pIn->bpe != 16) &&
        (pIn->bpe != 32) &&
        (pIn->bpe != 64) &&
        (pIn->bpe != 128))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else
    {
        returnCode = HwlComputeSlicePipeBankXor(pIn, pOut);
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Lib::UseCustomHeight
*
*   @brief
*       Determines if the calculations for this surface should use minimal HW values or user-specified values.
*
*   @return
*       Returns TRUE if the user-specified alignment should be used
************************************************************************************************************************
*/
BOOL_32 Lib::UseCustomHeight(
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT*  pIn
    ) const
{
    return ((pIn->numMipLevels <= 1)   &&
            IsLinear(pIn->swizzleMode) &&
            (pIn->sliceAlign > 0));
}

/**
************************************************************************************************************************
*   Lib::UseCustomPitch
*
*   @brief
*       Determines if the calculations for this surface should use minimal HW values or user-specified values.
*
*   @return
*       Returns TRUE if the user-specified pitch should be used
************************************************************************************************************************
*/
BOOL_32 Lib::UseCustomPitch(
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT*  pIn
    ) const
{
    return ((pIn->numMipLevels <= 1)   &&
            IsLinear(pIn->swizzleMode) &&
            (pIn->pitchInElement > 0));
}

/**
************************************************************************************************************************
*   Lib::CanTrimLinearPadding
*
*   @brief
*       Determines if the calculations for this surface can omit extra trailing padding for linear surfaces.
*
*   @return
*       Returns TRUE if the trailing padding can be omitted.
************************************************************************************************************************
*/
BOOL_32 Lib::CanTrimLinearPadding(
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT*  pIn
    ) const
{
    return ((IsTex3d(pIn->resourceType) == FALSE) &&
            (pIn->numSlices <= 1)                 &&
            IsLinear(pIn->swizzleMode));
}

/**
************************************************************************************************************************
*   Lib::ApplyCustomizedPitchHeight
*
*   @brief
*       Helper function to override hw required row pitch/slice pitch by customrized one
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ApplyCustomizedPitchHeight(
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pIn,    ///< [in] input structure
    ADDR3_COMPUTE_SURFACE_INFO_OUTPUT*      pOut
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    const UINT_32  elementBytes = pIn->bpp >> 3;

    UINT_32  pitchAlignmentElements      = pOut->blockExtent.width;
    UINT_32  pitchSliceAlignmentElements = pOut->blockExtent.width;

    if (IsLinear(pIn->swizzleMode))
    {
        // Normal pitch of image data
        const UINT_32  pitchAlignmentBytes    = 1 << GetBlockSizeLog2(pIn->swizzleMode, TRUE);
        pitchAlignmentElements = pitchAlignmentBytes / elementBytes;

        // Pitch of image data used for slice sizing
        const UINT_32  pitchSliceAlignmentBytes    = 1 << GetBlockSizeLog2(pIn->swizzleMode, CanTrimLinearPadding(pIn));
        pitchSliceAlignmentElements = pitchSliceAlignmentBytes / elementBytes;
    }

    pOut->pitch         = PowTwoAlign(pIn->width, pitchAlignmentElements);
    pOut->pitchForSlice = PowTwoAlign(pIn->width, pitchSliceAlignmentElements);

    UINT_32 heightAlign = pOut->blockExtent.height;

    if (pIn->flags.qbStereo)
    {
        UINT_32 rightXor = 0;

        returnCode = HwlComputeStereoInfo(pIn, &heightAlign, &rightXor);

        if (returnCode == ADDR_OK)
        {
            pOut->pStereoInfo->rightSwizzle = rightXor;
        }
    }

    pOut->height = PowTwoAlign(pIn->height, heightAlign);

    // Custom pitches / alignments are only possible with single mip level / linear images; otherwise,
    // ignore those parameters.
    if ((returnCode == ADDR_OK) && UseCustomPitch(pIn))
    {
        // Their requested pitch has to meet the pitch alignment constraints applied by the HW.
        if ((pIn->pitchInElement % pitchAlignmentElements) != 0)
        {
            returnCode = ADDR_INVALIDPARAMS;
        }
        // And their pitch can't be less than the minimum
        else if (pIn->pitchInElement < pOut->pitch)
        {
            returnCode = ADDR_INVALIDPARAMS;
        }
        else
        {
            pOut->pitch = pIn->pitchInElement;
            pOut->pitchForSlice = PowTwoAlign(pIn->pitchInElement, pitchSliceAlignmentElements);
        }
    }

    if ((returnCode == ADDR_OK) && UseCustomHeight(pIn))
    {
        // Note: if a custom slice align is present, it must be an even multiple
        // of pitchForSlice, not just pitch.
        UINT_32 customizedHeight = pIn->sliceAlign / elementBytes / pOut->pitchForSlice;

        if ((pIn->numSlices > 1) && (customizedHeight * elementBytes * pOut->pitchForSlice != pIn->sliceAlign))
        {
            returnCode = ADDR_INVALIDPARAMS;
        }
        else if ((pIn->numSlices > 1) && (pOut->height != customizedHeight))
        {
            returnCode = ADDR_INVALIDPARAMS;
        }
        else if ((pIn->height * elementBytes * pOut->pitch) > pIn->sliceAlign)
        {
            // If we only have one slice/depth, then we don't need an even multiple, but the slice size must still
            // fit all the pixel data. The one provided is too small!
            returnCode = ADDR_INVALIDPARAMS;
        }
        else
        {
            // For the single-slice case, the customized height could have been rounded down below the height since
            // we allow non-multiples of pitch here, so take the max.
            pOut->height = Max(pOut->height, customizedHeight);
        }
    }

    return returnCode;
}


/**
************************************************************************************************************************
*   Lib::ComputeQbStereoInfo
*
*   @brief
*       Get quad buffer stereo information
*   @return
*       N/A
************************************************************************************************************************
*/
VOID Lib::ComputeQbStereoInfo(
    ADDR3_COMPUTE_SURFACE_INFO_OUTPUT* pOut   ///< [in,out] updated pOut+pStereoInfo
    ) const
{
    ADDR_ASSERT(pOut->bpp >= 8);
    ADDR_ASSERT((pOut->surfSize % pOut->baseAlign) == 0);

    // Save original height
    pOut->pStereoInfo->eyeHeight = pOut->height;

    // Right offset
    pOut->pStereoInfo->rightOffset = static_cast<UINT_32>(pOut->surfSize);

    // Double height
    pOut->height <<= 1;

    ADDR_ASSERT(pOut->height <= MaxSurfaceHeight);

    pOut->pixelHeight <<= 1;

    // Double size
    pOut->surfSize  <<= 1;
    pOut->sliceSize <<= 1;
}

/**
************************************************************************************************************************
*   Lib::ComputeSurfaceInfoSanityCheck
*
*   @brief
*       Internal function to do basic sanity check before compute surface info
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceInfoSanityCheck(
    const ADDR3_COMPUTE_SURFACE_INFO_INPUT*  pIn   ///< [in] input structure
    ) const
{
    ADDR3_GET_POSSIBLE_SWIZZLE_MODE_INPUT localIn = {};
    localIn.size         = sizeof(ADDR3_GET_POSSIBLE_SWIZZLE_MODE_INPUT);
    localIn.flags        = pIn->flags;
    localIn.resourceType = pIn->resourceType;
    localIn.bpp          = pIn->bpp;
    localIn.width        = pIn->width;
    localIn.height       = pIn->height;
    localIn.numSlices    = pIn->numSlices;
    localIn.numMipLevels = pIn->numMipLevels;
    localIn.numSamples   = pIn->numSamples;

    return HwlValidateNonSwModeParams(&localIn) ? ADDR_OK : ADDR_INVALIDPARAMS;
}

} // V3
} // Addr
