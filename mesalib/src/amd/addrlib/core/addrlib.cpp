/*
 * Copyright © 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

/**
***************************************************************************************************
* @file  addrlib.cpp
* @brief Contains the implementation for the AddrLib base class..
***************************************************************************************************
*/

#include "addrinterface.h"
#include "addrlib.h"
#include "addrcommon.h"

#if defined(__APPLE__)

UINT_32 div64_32(UINT_64 n, UINT_32 base)
{
    UINT_64 rem = n;
    UINT_64 b = base;
    UINT_64 res, d = 1;
    UINT_32 high = rem >> 32;

    res = 0;
    if (high >= base)
    {
        high /= base;
        res = (UINT_64) high << 32;
        rem -= (UINT_64) (high*base) << 32;
    }

    while ((INT_64)b > 0 && b < rem)
    {
        b = b+b;
        d = d+d;
    }

    do
    {
        if (rem >= b)
        {
            rem -= b;
            res += d;
        }
        b >>= 1;
        d >>= 1;
    } while (d);

    n = res;
    return rem;
}

extern "C"
UINT_32 __umoddi3(UINT_64 n, UINT_32 base)
{
    return div64_32(n, base);
}

#endif // __APPLE__

///////////////////////////////////////////////////////////////////////////////////////////////////
//                               Static Const Member
///////////////////////////////////////////////////////////////////////////////////////////////////

const AddrTileModeFlags AddrLib::m_modeFlags[ADDR_TM_COUNT] =
{// T   L  1  2  3  P  Pr B
    {1, 1, 0, 0, 0, 0, 0, 0}, // ADDR_TM_LINEAR_GENERAL
    {1, 1, 0, 0, 0, 0, 0, 0}, // ADDR_TM_LINEAR_ALIGNED
    {1, 0, 1, 0, 0, 0, 0, 0}, // ADDR_TM_1D_TILED_THIN1
    {4, 0, 1, 0, 0, 0, 0, 0}, // ADDR_TM_1D_TILED_THICK
    {1, 0, 0, 1, 0, 0, 0, 0}, // ADDR_TM_2D_TILED_THIN1
    {1, 0, 0, 1, 0, 0, 0, 0}, // ADDR_TM_2D_TILED_THIN2
    {1, 0, 0, 1, 0, 0, 0, 0}, // ADDR_TM_2D_TILED_THIN4
    {4, 0, 0, 1, 0, 0, 0, 0}, // ADDR_TM_2D_TILED_THICK
    {1, 0, 0, 1, 0, 0, 0, 1}, // ADDR_TM_2B_TILED_THIN1
    {1, 0, 0, 1, 0, 0, 0, 1}, // ADDR_TM_2B_TILED_THIN2
    {1, 0, 0, 1, 0, 0, 0, 1}, // ADDR_TM_2B_TILED_THIN4
    {4, 0, 0, 1, 0, 0, 0, 1}, // ADDR_TM_2B_TILED_THICK
    {1, 0, 0, 1, 1, 0, 0, 0}, // ADDR_TM_3D_TILED_THIN1
    {4, 0, 0, 1, 1, 0, 0, 0}, // ADDR_TM_3D_TILED_THICK
    {1, 0, 0, 1, 1, 0, 0, 1}, // ADDR_TM_3B_TILED_THIN1
    {4, 0, 0, 1, 1, 0, 0, 1}, // ADDR_TM_3B_TILED_THICK
    {8, 0, 0, 1, 0, 0, 0, 0}, // ADDR_TM_2D_TILED_XTHICK
    {8, 0, 0, 1, 1, 0, 0, 0}, // ADDR_TM_3D_TILED_XTHICK
    {1, 0, 0, 0, 0, 0, 0, 0}, // ADDR_TM_POWER_SAVE
    {1, 0, 0, 1, 0, 1, 1, 0}, // ADDR_TM_PRT_TILED_THIN1
    {1, 0, 0, 1, 0, 1, 0, 0}, // ADDR_TM_PRT_2D_TILED_THIN1
    {1, 0, 0, 1, 1, 1, 0, 0}, // ADDR_TM_PRT_3D_TILED_THIN1
    {4, 0, 0, 1, 0, 1, 1, 0}, // ADDR_TM_PRT_TILED_THICK
    {4, 0, 0, 1, 0, 1, 0, 0}, // ADDR_TM_PRT_2D_TILED_THICK
    {4, 0, 0, 1, 1, 1, 0, 0}, // ADDR_TM_PRT_3D_TILED_THICK
};

///////////////////////////////////////////////////////////////////////////////////////////////////
//                               Constructor/Destructor
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
***************************************************************************************************
*   AddrLib::AddrLib
*
*   @brief
*       Constructor for the AddrLib class
*
***************************************************************************************************
*/
AddrLib::AddrLib() :
    m_class(BASE_ADDRLIB),
    m_chipFamily(ADDR_CHIP_FAMILY_IVLD),
    m_chipRevision(0),
    m_version(ADDRLIB_VERSION),
    m_pipes(0),
    m_banks(0),
    m_pipeInterleaveBytes(0),
    m_rowSize(0),
    m_minPitchAlignPixels(1),
    m_maxSamples(8),
    m_pElemLib(NULL)
{
    m_configFlags.value = 0;
}

/**
***************************************************************************************************
*   AddrLib::AddrLib
*
*   @brief
*       Constructor for the AddrLib class with hClient as parameter
*
***************************************************************************************************
*/
AddrLib::AddrLib(const AddrClient* pClient) :
    AddrObject(pClient),
    m_class(BASE_ADDRLIB),
    m_chipFamily(ADDR_CHIP_FAMILY_IVLD),
    m_chipRevision(0),
    m_version(ADDRLIB_VERSION),
    m_pipes(0),
    m_banks(0),
    m_pipeInterleaveBytes(0),
    m_rowSize(0),
    m_minPitchAlignPixels(1),
    m_maxSamples(8),
    m_pElemLib(NULL)
{
    m_configFlags.value = 0;
}

/**
***************************************************************************************************
*   AddrLib::~AddrLib
*
*   @brief
*       Destructor for the AddrLib class
*
***************************************************************************************************
*/
AddrLib::~AddrLib()
{
    if (m_pElemLib)
    {
        delete m_pElemLib;
    }
}



///////////////////////////////////////////////////////////////////////////////////////////////////
//                               Initialization/Helper
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
***************************************************************************************************
*   AddrLib::Create
*
*   @brief
*       Creates and initializes AddrLib object.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::Create(
    const ADDR_CREATE_INPUT* pCreateIn,     ///< [in] pointer to ADDR_CREATE_INPUT
    ADDR_CREATE_OUTPUT*      pCreateOut)    ///< [out] pointer to ADDR_CREATE_OUTPUT
{
    AddrLib* pLib = NULL;
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (pCreateIn->createFlags.fillSizeFields == TRUE)
    {
        if ((pCreateIn->size != sizeof(ADDR_CREATE_INPUT)) ||
            (pCreateOut->size != sizeof(ADDR_CREATE_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if ((returnCode == ADDR_OK)                    &&
        (pCreateIn->callbacks.allocSysMem != NULL) &&
        (pCreateIn->callbacks.freeSysMem != NULL))
    {
        AddrClient client = {
            pCreateIn->hClient,
            pCreateIn->callbacks
        };

        switch (pCreateIn->chipEngine)
        {
            case CIASICIDGFXENGINE_SOUTHERNISLAND:
                switch (pCreateIn->chipFamily)
                {
                    case FAMILY_SI:
                        pLib = AddrSIHwlInit(&client);
                        break;
                    case FAMILY_VI:
                    case FAMILY_CZ: // VI based fusion(carrizo)
                    case FAMILY_CI:
                    case FAMILY_KV: // CI based fusion
                        pLib = AddrCIHwlInit(&client);
                        break;
                    default:
                        ADDR_ASSERT_ALWAYS();
                        break;
                }
                break;
            default:
                ADDR_ASSERT_ALWAYS();
                break;
        }
    }

    if ((pLib != NULL))
    {
        BOOL_32 initValid;

        // Pass createFlags to configFlags first since these flags may be overwritten
        pLib->m_configFlags.noCubeMipSlicesPad  = pCreateIn->createFlags.noCubeMipSlicesPad;
        pLib->m_configFlags.fillSizeFields      = pCreateIn->createFlags.fillSizeFields;
        pLib->m_configFlags.useTileIndex        = pCreateIn->createFlags.useTileIndex;
        pLib->m_configFlags.useCombinedSwizzle  = pCreateIn->createFlags.useCombinedSwizzle;
        pLib->m_configFlags.checkLast2DLevel    = pCreateIn->createFlags.checkLast2DLevel;
        pLib->m_configFlags.useHtileSliceAlign  = pCreateIn->createFlags.useHtileSliceAlign;
        pLib->m_configFlags.degradeBaseLevel    = pCreateIn->createFlags.degradeBaseLevel;
        pLib->m_configFlags.allowLargeThickTile = pCreateIn->createFlags.allowLargeThickTile;

        pLib->SetAddrChipFamily(pCreateIn->chipFamily, pCreateIn->chipRevision);

        pLib->SetMinPitchAlignPixels(pCreateIn->minPitchAlignPixels);

        // Global parameters initialized and remaining configFlags bits are set as well
        initValid = pLib->HwlInitGlobalParams(pCreateIn);

        if (initValid)
        {
            pLib->m_pElemLib = AddrElemLib::Create(pLib);
        }
        else
        {
            pLib->m_pElemLib = NULL; // Don't go on allocating element lib
            returnCode = ADDR_INVALIDGBREGVALUES;
        }

        if (pLib->m_pElemLib == NULL)
        {
            delete pLib;
            pLib = NULL;
            ADDR_ASSERT_ALWAYS();
        }
        else
        {
            pLib->m_pElemLib->SetConfigFlags(pLib->m_configFlags);
        }
    }

    pCreateOut->hLib = pLib;

    if ((pLib == NULL) &&
        (returnCode == ADDR_OK))
    {
        // Unknown failures, we return the general error code
        returnCode = ADDR_ERROR;
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::SetAddrChipFamily
*
*   @brief
*       Convert familyID defined in atiid.h to AddrChipFamily and set m_chipFamily/m_chipRevision
*   @return
*      N/A
***************************************************************************************************
*/
VOID AddrLib::SetAddrChipFamily(
    UINT_32 uChipFamily,        ///< [in] chip family defined in atiih.h
    UINT_32 uChipRevision)      ///< [in] chip revision defined in "asic_family"_id.h
{
    AddrChipFamily family = ADDR_CHIP_FAMILY_IVLD;

    family = HwlConvertChipFamily(uChipFamily, uChipRevision);

    ADDR_ASSERT(family != ADDR_CHIP_FAMILY_IVLD);

    m_chipFamily    = family;
    m_chipRevision  = uChipRevision;
}

/**
***************************************************************************************************
*   AddrLib::SetMinPitchAlignPixels
*
*   @brief
*       Set m_minPitchAlignPixels with input param
*
*   @return
*      N/A
***************************************************************************************************
*/
VOID AddrLib::SetMinPitchAlignPixels(
    UINT_32 minPitchAlignPixels)    ///< [in] minmum pitch alignment in pixels
{
    m_minPitchAlignPixels = (minPitchAlignPixels == 0)? 1 : minPitchAlignPixels;
}

/**
***************************************************************************************************
*   AddrLib::GetAddrLib
*
*   @brief
*       Get AddrLib pointer
*
*   @return
*      An AddrLib class pointer
***************************************************************************************************
*/
AddrLib * AddrLib::GetAddrLib(
    ADDR_HANDLE hLib)   ///< [in] handle of ADDR_HANDLE
{
    return static_cast<AddrLib *>(hLib);
}



///////////////////////////////////////////////////////////////////////////////////////////////////
//                               Surface Methods
///////////////////////////////////////////////////////////////////////////////////////////////////


/**
***************************************************************************************************
*   AddrLib::ComputeSurfaceInfo
*
*   @brief
*       Interface function stub of AddrComputeSurfaceInfo.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeSurfaceInfo(
     const ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn,    ///< [in] input structure
     ADDR_COMPUTE_SURFACE_INFO_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_SURFACE_INFO_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_SURFACE_INFO_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    // We suggest client do sanity check but a check here is also good
    if (pIn->bpp > 128)
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    // Thick modes don't support multisample
    if (ComputeSurfaceThickness(pIn->tileMode) > 1 && pIn->numSamples > 1)
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    if (returnCode == ADDR_OK)
    {
        // Get a local copy of input structure and only reference pIn for unadjusted values
        ADDR_COMPUTE_SURFACE_INFO_INPUT localIn = *pIn;
        ADDR_TILEINFO tileInfoNull = {0};

        if (UseTileInfo())
        {
            // If the original input has a valid ADDR_TILEINFO pointer then copy its contents.
            // Otherwise the default 0's in tileInfoNull are used.
            if (pIn->pTileInfo)
            {
                tileInfoNull = *pIn->pTileInfo;
            }
            localIn.pTileInfo  = &tileInfoNull;
        }

        localIn.numSamples = pIn->numSamples == 0 ? 1 : pIn->numSamples;

        // Do mipmap check first
        // If format is BCn, pre-pad dimension to power-of-two according to HWL
        ComputeMipLevel(&localIn);

        if (m_configFlags.checkLast2DLevel)
        {
            // Save this level's original height in pixels
            pOut->height = pIn->height;
        }

        UINT_32 expandX = 1;
        UINT_32 expandY = 1;
        AddrElemMode elemMode;

        // Save outputs that may not go through HWL
        pOut->pixelBits = localIn.bpp;
        pOut->numSamples = localIn.numSamples;
        pOut->last2DLevel = FALSE;

#if !ALT_TEST
        if (localIn.numSamples > 1)
        {
            ADDR_ASSERT(localIn.mipLevel == 0);
        }
#endif

        if (localIn.format != ADDR_FMT_INVALID) // Set format to INVALID will skip this conversion
        {
            // Get compression/expansion factors and element mode
            // (which indicates compression/expansion
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

            if ((elemMode == ADDR_EXPANDED) &&
                (expandX > 1))
            {
                ADDR_ASSERT(localIn.tileMode == ADDR_TM_LINEAR_ALIGNED || localIn.height == 1);
            }

            GetElemLib()->AdjustSurfaceInfo(elemMode,
                                            expandX,
                                            expandY,
                                            &localIn.bpp,
                                            &localIn.basePitch,
                                            &localIn.width,
                                            &localIn.height);

            // Overwrite these parameters if we have a valid format
        }
        else if (localIn.bpp != 0)
        {
            localIn.width  = (localIn.width != 0) ? localIn.width : 1;
            localIn.height = (localIn.height != 0) ? localIn.height : 1;
        }
        else // Rule out some invalid parameters
        {
            ADDR_ASSERT_ALWAYS();

            returnCode = ADDR_INVALIDPARAMS;
        }

        // Check mipmap after surface expansion
        if (returnCode == ADDR_OK)
        {
            returnCode = PostComputeMipLevel(&localIn, pOut);
        }

        if (returnCode == ADDR_OK)
        {
            if (UseTileIndex(localIn.tileIndex))
            {
                // Make sure pTileInfo is not NULL
                ADDR_ASSERT(localIn.pTileInfo);

                UINT_32 numSamples = GetNumFragments(localIn.numSamples, localIn.numFrags);

                INT_32 macroModeIndex = TileIndexNoMacroIndex;

                if (localIn.tileIndex != TileIndexLinearGeneral)
                {
                    // Try finding a macroModeIndex
                    macroModeIndex = HwlComputeMacroModeIndex(localIn.tileIndex,
                                                              localIn.flags,
                                                              localIn.bpp,
                                                              numSamples,
                                                              localIn.pTileInfo,
                                                              &localIn.tileMode,
                                                              &localIn.tileType);
                }

                // If macroModeIndex is not needed, then call HwlSetupTileCfg to get tile info
                if (macroModeIndex == TileIndexNoMacroIndex)
                {
                    returnCode = HwlSetupTileCfg(localIn.tileIndex, macroModeIndex,
                                                 localIn.pTileInfo,
                                                 &localIn.tileMode, &localIn.tileType);
                }
                // If macroModeIndex is invalid, then assert this is not macro tiled
                else if (macroModeIndex == TileIndexInvalid)
                {
                    ADDR_ASSERT(!IsMacroTiled(localIn.tileMode));
                }
            }
        }

        if (returnCode == ADDR_OK)
        {
            AddrTileMode tileMode = localIn.tileMode;
            AddrTileType tileType = localIn.tileType;

            // HWL layer may override tile mode if necessary
            if (HwlOverrideTileMode(&localIn, &tileMode, &tileType))
            {
                localIn.tileMode = tileMode;
                localIn.tileType = tileType;
            }
            // Degrade base level if applicable
            if (DegradeBaseLevel(&localIn, &tileMode))
            {
                localIn.tileMode = tileMode;
            }
        }

        // Call main function to compute surface info
        if (returnCode == ADDR_OK)
        {
            returnCode = HwlComputeSurfaceInfo(&localIn, pOut);
        }

        if (returnCode == ADDR_OK)
        {
            // Since bpp might be changed we just pass it through
            pOut->bpp  = localIn.bpp;

            // Also original width/height/bpp
            pOut->pixelPitch    = pOut->pitch;
            pOut->pixelHeight   = pOut->height;

#if DEBUG
            if (localIn.flags.display)
            {
                ADDR_ASSERT((pOut->pitchAlign % 32) == 0);
            }
#endif //DEBUG

            if (localIn.format != ADDR_FMT_INVALID)
            {
                //
                // 96 bits surface of level 1+ requires element pitch of 32 bits instead
                // In hwl function we skip multiplication of 3 then we should skip division of 3
                // We keep pitch that represents 32 bit element instead of 96 bits since we
                // will get an odd number if divided by 3.
                //
                if (!((expandX == 3) && (localIn.mipLevel > 0)))
                {

                    GetElemLib()->RestoreSurfaceInfo(elemMode,
                                                     expandX,
                                                     expandY,
                                                     &localIn.bpp,
                                                     &pOut->pixelPitch,
                                                     &pOut->pixelHeight);
                }
            }

            if (localIn.flags.qbStereo)
            {
                if (pOut->pStereoInfo)
                {
                    ComputeQbStereoInfo(pOut);
                }
            }

            if (localIn.flags.volume) // For volume sliceSize equals to all z-slices
            {
                pOut->sliceSize = pOut->surfSize;
            }
            else // For array: sliceSize is likely to have slice-padding (the last one)
            {
                pOut->sliceSize = pOut->surfSize / pOut->depth;

                // array or cubemap
                if (pIn->numSlices > 1)
                {
                    // If this is the last slice then add the padding size to this slice
                    if (pIn->slice == (pIn->numSlices - 1))
                    {
                        pOut->sliceSize += pOut->sliceSize * (pOut->depth - pIn->numSlices);
                    }
                    else if (m_configFlags.checkLast2DLevel)
                    {
                        // Reset last2DLevel flag if this is not the last array slice
                        pOut->last2DLevel = FALSE;
                    }
                }
            }

            pOut->pitchTileMax = pOut->pitch / 8 - 1;
            pOut->heightTileMax = pOut->height / 8 - 1;
            pOut->sliceTileMax = pOut->pitch * pOut->height / 64 - 1;
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeSurfaceInfo
*
*   @brief
*       Interface function stub of AddrComputeSurfaceInfo.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeSurfaceAddrFromCoord(
    const ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,    ///< [in] input structure
    ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT*      pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;
            // Use temp tile info for calcalation
            input.pTileInfo = &tileInfoNull;

            const ADDR_SURFACE_FLAGS flags = {{0}};
            UINT_32 numSamples = GetNumFragments(pIn->numSamples, pIn->numFrags);

            // Try finding a macroModeIndex
            INT_32 macroModeIndex = HwlComputeMacroModeIndex(input.tileIndex,
                                                             flags,
                                                             input.bpp,
                                                             numSamples,
                                                             input.pTileInfo,
                                                             &input.tileMode,
                                                             &input.tileType);

            // If macroModeIndex is not needed, then call HwlSetupTileCfg to get tile info
            if (macroModeIndex == TileIndexNoMacroIndex)
            {
                returnCode = HwlSetupTileCfg(input.tileIndex, macroModeIndex,
                                             input.pTileInfo, &input.tileMode, &input.tileType);
            }
            // If macroModeIndex is invalid, then assert this is not macro tiled
            else if (macroModeIndex == TileIndexInvalid)
            {
                ADDR_ASSERT(!IsMacroTiled(input.tileMode));
            }

            // Change the input structure
            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            returnCode = HwlComputeSurfaceAddrFromCoord(pIn, pOut);

            if (returnCode == ADDR_OK)
            {
                pOut->prtBlockIndex = static_cast<UINT_32>(pOut->addr / (64 * 1024));
            }
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeSurfaceCoordFromAddr
*
*   @brief
*       Interface function stub of ComputeSurfaceCoordFromAddr.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeSurfaceCoordFromAddr(
    const ADDR_COMPUTE_SURFACE_COORDFROMADDR_INPUT* pIn,    ///< [in] input structure
    ADDR_COMPUTE_SURFACE_COORDFROMADDR_OUTPUT*      pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_SURFACE_COORDFROMADDR_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_SURFACE_COORDFROMADDR_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_COMPUTE_SURFACE_COORDFROMADDR_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;
            // Use temp tile info for calcalation
            input.pTileInfo = &tileInfoNull;

            const ADDR_SURFACE_FLAGS flags = {{0}};
            UINT_32 numSamples = GetNumFragments(pIn->numSamples, pIn->numFrags);

            // Try finding a macroModeIndex
            INT_32 macroModeIndex = HwlComputeMacroModeIndex(input.tileIndex,
                                                             flags,
                                                             input.bpp,
                                                             numSamples,
                                                             input.pTileInfo,
                                                             &input.tileMode,
                                                             &input.tileType);

            // If macroModeIndex is not needed, then call HwlSetupTileCfg to get tile info
            if (macroModeIndex == TileIndexNoMacroIndex)
            {
                returnCode = HwlSetupTileCfg(input.tileIndex, macroModeIndex,
                                             input.pTileInfo, &input.tileMode, &input.tileType);
            }
            // If macroModeIndex is invalid, then assert this is not macro tiled
            else if (macroModeIndex == TileIndexInvalid)
            {
                ADDR_ASSERT(!IsMacroTiled(input.tileMode));
            }

            // Change the input structure
            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            returnCode = HwlComputeSurfaceCoordFromAddr(pIn, pOut);
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeSliceTileSwizzle
*
*   @brief
*       Interface function stub of ComputeSliceTileSwizzle.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeSliceTileSwizzle(
    const ADDR_COMPUTE_SLICESWIZZLE_INPUT*  pIn,    ///< [in] input structure
    ADDR_COMPUTE_SLICESWIZZLE_OUTPUT*       pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_SLICESWIZZLE_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_SLICESWIZZLE_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_COMPUTE_SLICESWIZZLE_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;
            // Use temp tile info for calcalation
            input.pTileInfo = &tileInfoNull;

            returnCode = HwlSetupTileCfg(input.tileIndex, input.macroModeIndex,
                                         input.pTileInfo, &input.tileMode);
            // Change the input structure
            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            returnCode = HwlComputeSliceTileSwizzle(pIn, pOut);
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ExtractBankPipeSwizzle
*
*   @brief
*       Interface function stub of AddrExtractBankPipeSwizzle.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ExtractBankPipeSwizzle(
    const ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT*  pIn,    ///< [in] input structure
    ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT*       pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT)) ||
            (pOut->size != sizeof(ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;
            // Use temp tile info for calcalation
            input.pTileInfo = &tileInfoNull;

            returnCode = HwlSetupTileCfg(input.tileIndex, input.macroModeIndex, input.pTileInfo);
            // Change the input structure
            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            returnCode = HwlExtractBankPipeSwizzle(pIn, pOut);
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::CombineBankPipeSwizzle
*
*   @brief
*       Interface function stub of AddrCombineBankPipeSwizzle.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::CombineBankPipeSwizzle(
    const ADDR_COMBINE_BANKPIPE_SWIZZLE_INPUT*  pIn,    ///< [in] input structure
    ADDR_COMBINE_BANKPIPE_SWIZZLE_OUTPUT*       pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_FMASK_INFO_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_FMASK_INFO_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_COMBINE_BANKPIPE_SWIZZLE_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;
            // Use temp tile info for calcalation
            input.pTileInfo = &tileInfoNull;

            returnCode = HwlSetupTileCfg(input.tileIndex, input.macroModeIndex, input.pTileInfo);
            // Change the input structure
            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            returnCode = HwlCombineBankPipeSwizzle(pIn->bankSwizzle,
                                                   pIn->pipeSwizzle,
                                                   pIn->pTileInfo,
                                                   pIn->baseAddr,
                                                   &pOut->tileSwizzle);
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeBaseSwizzle
*
*   @brief
*       Interface function stub of AddrCompueBaseSwizzle.
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeBaseSwizzle(
    const ADDR_COMPUTE_BASE_SWIZZLE_INPUT*  pIn,
    ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT* pOut) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_BASE_SWIZZLE_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_COMPUTE_BASE_SWIZZLE_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;
            // Use temp tile info for calcalation
            input.pTileInfo = &tileInfoNull;

            returnCode = HwlSetupTileCfg(input.tileIndex, input.macroModeIndex, input.pTileInfo);
            // Change the input structure
            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            if (IsMacroTiled(pIn->tileMode))
            {
                returnCode = HwlComputeBaseSwizzle(pIn, pOut);
            }
            else
            {
                pOut->tileSwizzle = 0;
            }
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeFmaskInfo
*
*   @brief
*       Interface function stub of ComputeFmaskInfo.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeFmaskInfo(
    const ADDR_COMPUTE_FMASK_INFO_INPUT*    pIn,    ///< [in] input structure
    ADDR_COMPUTE_FMASK_INFO_OUTPUT*         pOut    ///< [out] output structure
    )
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_FMASK_INFO_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_FMASK_INFO_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    // No thick MSAA
    if (ComputeSurfaceThickness(pIn->tileMode) > 1)
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_COMPUTE_FMASK_INFO_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;

            if (pOut->pTileInfo)
            {
                // Use temp tile info for calcalation
                input.pTileInfo = pOut->pTileInfo;
            }
            else
            {
                input.pTileInfo = &tileInfoNull;
            }

            ADDR_SURFACE_FLAGS flags = {{0}};
            flags.fmask = 1;

            // Try finding a macroModeIndex
            INT_32 macroModeIndex = HwlComputeMacroModeIndex(pIn->tileIndex,
                                                             flags,
                                                             HwlComputeFmaskBits(pIn, NULL),
                                                             pIn->numSamples,
                                                             input.pTileInfo,
                                                             &input.tileMode);

            // If macroModeIndex is not needed, then call HwlSetupTileCfg to get tile info
            if (macroModeIndex == TileIndexNoMacroIndex)
            {
                returnCode = HwlSetupTileCfg(input.tileIndex, macroModeIndex,
                                             input.pTileInfo, &input.tileMode);
            }

            ADDR_ASSERT(macroModeIndex != TileIndexInvalid);

            // Change the input structure
            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            if (pIn->numSamples > 1)
            {
                returnCode = HwlComputeFmaskInfo(pIn, pOut);
            }
            else
            {
                memset(pOut, 0, sizeof(ADDR_COMPUTE_FMASK_INFO_OUTPUT));

                returnCode = ADDR_INVALIDPARAMS;
            }
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeFmaskAddrFromCoord
*
*   @brief
*       Interface function stub of ComputeFmaskAddrFromCoord.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeFmaskAddrFromCoord(
    const ADDR_COMPUTE_FMASK_ADDRFROMCOORD_INPUT*   pIn,    ///< [in] input structure
    ADDR_COMPUTE_FMASK_ADDRFROMCOORD_OUTPUT*        pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_FMASK_ADDRFROMCOORD_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_FMASK_ADDRFROMCOORD_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_ASSERT(pIn->numSamples > 1);

        if (pIn->numSamples > 1)
        {
            returnCode = HwlComputeFmaskAddrFromCoord(pIn, pOut);
        }
        else
        {
            returnCode = ADDR_INVALIDPARAMS;
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeFmaskCoordFromAddr
*
*   @brief
*       Interface function stub of ComputeFmaskAddrFromCoord.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeFmaskCoordFromAddr(
    const ADDR_COMPUTE_FMASK_COORDFROMADDR_INPUT*  pIn,     ///< [in] input structure
    ADDR_COMPUTE_FMASK_COORDFROMADDR_OUTPUT* pOut           ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_FMASK_COORDFROMADDR_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_FMASK_COORDFROMADDR_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_ASSERT(pIn->numSamples > 1);

        if (pIn->numSamples > 1)
        {
            returnCode = HwlComputeFmaskCoordFromAddr(pIn, pOut);
        }
        else
        {
            returnCode = ADDR_INVALIDPARAMS;
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ConvertTileInfoToHW
*
*   @brief
*       Convert tile info from real value to HW register value in HW layer
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ConvertTileInfoToHW(
    const ADDR_CONVERT_TILEINFOTOHW_INPUT* pIn, ///< [in] input structure
    ADDR_CONVERT_TILEINFOTOHW_OUTPUT* pOut      ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_CONVERT_TILEINFOTOHW_INPUT)) ||
            (pOut->size != sizeof(ADDR_CONVERT_TILEINFOTOHW_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_CONVERT_TILEINFOTOHW_INPUT input;
        // if pIn->reverse is TRUE, indices are ignored
        if (pIn->reverse == FALSE && UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;
            input.pTileInfo = &tileInfoNull;

            returnCode = HwlSetupTileCfg(input.tileIndex, input.macroModeIndex, input.pTileInfo);

            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            returnCode = HwlConvertTileInfoToHW(pIn, pOut);
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ConvertTileIndex
*
*   @brief
*       Convert tile index to tile mode/type/info
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ConvertTileIndex(
    const ADDR_CONVERT_TILEINDEX_INPUT* pIn, ///< [in] input structure
    ADDR_CONVERT_TILEINDEX_OUTPUT* pOut      ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_CONVERT_TILEINDEX_INPUT)) ||
            (pOut->size != sizeof(ADDR_CONVERT_TILEINDEX_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {

        returnCode = HwlSetupTileCfg(pIn->tileIndex, pIn->macroModeIndex,
                                     pOut->pTileInfo, &pOut->tileMode, &pOut->tileType);

        if (returnCode == ADDR_OK && pIn->tileInfoHw)
        {
            ADDR_CONVERT_TILEINFOTOHW_INPUT hwInput = {0};
            ADDR_CONVERT_TILEINFOTOHW_OUTPUT hwOutput = {0};

            hwInput.pTileInfo = pOut->pTileInfo;
            hwInput.tileIndex = -1;
            hwOutput.pTileInfo = pOut->pTileInfo;

            returnCode = HwlConvertTileInfoToHW(&hwInput, &hwOutput);
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ConvertTileIndex1
*
*   @brief
*       Convert tile index to tile mode/type/info
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ConvertTileIndex1(
    const ADDR_CONVERT_TILEINDEX1_INPUT* pIn,   ///< [in] input structure
    ADDR_CONVERT_TILEINDEX_OUTPUT* pOut         ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_CONVERT_TILEINDEX1_INPUT)) ||
            (pOut->size != sizeof(ADDR_CONVERT_TILEINDEX_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_SURFACE_FLAGS flags = {{0}};

        HwlComputeMacroModeIndex(pIn->tileIndex, flags, pIn->bpp, pIn->numSamples,
                                 pOut->pTileInfo, &pOut->tileMode, &pOut->tileType);

        if (pIn->tileInfoHw)
        {
            ADDR_CONVERT_TILEINFOTOHW_INPUT hwInput = {0};
            ADDR_CONVERT_TILEINFOTOHW_OUTPUT hwOutput = {0};

            hwInput.pTileInfo = pOut->pTileInfo;
            hwInput.tileIndex = -1;
            hwOutput.pTileInfo = pOut->pTileInfo;

            returnCode = HwlConvertTileInfoToHW(&hwInput, &hwOutput);
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::GetTileIndex
*
*   @brief
*       Get tile index from tile mode/type/info
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::GetTileIndex(
    const ADDR_GET_TILEINDEX_INPUT* pIn, ///< [in] input structure
    ADDR_GET_TILEINDEX_OUTPUT* pOut      ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_GET_TILEINDEX_INPUT)) ||
            (pOut->size != sizeof(ADDR_GET_TILEINDEX_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        returnCode = HwlGetTileIndex(pIn, pOut);
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeSurfaceThickness
*
*   @brief
*       Compute surface thickness
*
*   @return
*       Surface thickness
***************************************************************************************************
*/
UINT_32 AddrLib::ComputeSurfaceThickness(
    AddrTileMode tileMode)    ///< [in] tile mode
{
    return m_modeFlags[tileMode].thickness;
}



///////////////////////////////////////////////////////////////////////////////////////////////////
//                               CMASK/HTILE
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
***************************************************************************************************
*   AddrLib::ComputeHtileInfo
*
*   @brief
*       Interface function stub of AddrComputeHtilenfo
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeHtileInfo(
    const ADDR_COMPUTE_HTILE_INFO_INPUT*    pIn,    ///< [in] input structure
    ADDR_COMPUTE_HTILE_INFO_OUTPUT*         pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    BOOL_32 isWidth8  = (pIn->blockWidth == 8) ? TRUE : FALSE;
    BOOL_32 isHeight8 = (pIn->blockHeight == 8) ? TRUE : FALSE;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_HTILE_INFO_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_HTILE_INFO_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_COMPUTE_HTILE_INFO_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;
            // Use temp tile info for calcalation
            input.pTileInfo = &tileInfoNull;

            returnCode = HwlSetupTileCfg(input.tileIndex, input.macroModeIndex, input.pTileInfo);

            // Change the input structure
            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            pOut->bpp = ComputeHtileInfo(pIn->flags,
                                         pIn->pitch,
                                         pIn->height,
                                         pIn->numSlices,
                                         pIn->isLinear,
                                         isWidth8,
                                         isHeight8,
                                         pIn->pTileInfo,
                                         &pOut->pitch,
                                         &pOut->height,
                                         &pOut->htileBytes,
                                         &pOut->macroWidth,
                                         &pOut->macroHeight,
                                         &pOut->sliceSize,
                                         &pOut->baseAlign);
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeCmaskInfo
*
*   @brief
*       Interface function stub of AddrComputeCmaskInfo
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeCmaskInfo(
    const ADDR_COMPUTE_CMASK_INFO_INPUT*    pIn,    ///< [in] input structure
    ADDR_COMPUTE_CMASK_INFO_OUTPUT*         pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_CMASK_INFO_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_CMASK_INFO_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_COMPUTE_CMASK_INFO_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;
            // Use temp tile info for calcalation
            input.pTileInfo = &tileInfoNull;

            returnCode = HwlSetupTileCfg(input.tileIndex, input.macroModeIndex, input.pTileInfo);

            // Change the input structure
            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            returnCode = ComputeCmaskInfo(pIn->flags,
                                          pIn->pitch,
                                          pIn->height,
                                          pIn->numSlices,
                                          pIn->isLinear,
                                          pIn->pTileInfo,
                                          &pOut->pitch,
                                          &pOut->height,
                                          &pOut->cmaskBytes,
                                          &pOut->macroWidth,
                                          &pOut->macroHeight,
                                          &pOut->sliceSize,
                                          &pOut->baseAlign,
                                          &pOut->blockMax);
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeDccInfo
*
*   @brief
*       Interface function to compute DCC key info
*
*   @return
*       return code of HwlComputeDccInfo
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeDccInfo(
    const ADDR_COMPUTE_DCCINFO_INPUT*    pIn,    ///< [in] input structure
    ADDR_COMPUTE_DCCINFO_OUTPUT*         pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE ret = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_DCCINFO_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_DCCINFO_OUTPUT)))
        {
            ret = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (ret == ADDR_OK)
    {
        ADDR_COMPUTE_DCCINFO_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;

            ret = HwlSetupTileCfg(input.tileIndex, input.macroModeIndex,
                                  &input.tileInfo, &input.tileMode);

            pIn = &input;
        }

        if (ADDR_OK == ret)
        {
            ret = HwlComputeDccInfo(pIn, pOut);
        }
    }

    return ret;
}

/**
***************************************************************************************************
*   AddrLib::ComputeHtileAddrFromCoord
*
*   @brief
*       Interface function stub of AddrComputeHtileAddrFromCoord
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeHtileAddrFromCoord(
    const ADDR_COMPUTE_HTILE_ADDRFROMCOORD_INPUT*   pIn,    ///< [in] input structure
    ADDR_COMPUTE_HTILE_ADDRFROMCOORD_OUTPUT*        pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    BOOL_32 isWidth8  = (pIn->blockWidth == 8) ? TRUE : FALSE;
    BOOL_32 isHeight8 = (pIn->blockHeight == 8) ? TRUE : FALSE;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_HTILE_ADDRFROMCOORD_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_HTILE_ADDRFROMCOORD_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_COMPUTE_HTILE_ADDRFROMCOORD_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;
            // Use temp tile info for calcalation
            input.pTileInfo = &tileInfoNull;

            returnCode = HwlSetupTileCfg(input.tileIndex, input.macroModeIndex, input.pTileInfo);

            // Change the input structure
            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            pOut->addr = HwlComputeXmaskAddrFromCoord(pIn->pitch,
                                                      pIn->height,
                                                      pIn->x,
                                                      pIn->y,
                                                      pIn->slice,
                                                      pIn->numSlices,
                                                      1,
                                                      pIn->isLinear,
                                                      isWidth8,
                                                      isHeight8,
                                                      pIn->pTileInfo,
                                                      &pOut->bitPosition);
        }
    }

    return returnCode;

}

/**
***************************************************************************************************
*   AddrLib::ComputeHtileCoordFromAddr
*
*   @brief
*       Interface function stub of AddrComputeHtileCoordFromAddr
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeHtileCoordFromAddr(
    const ADDR_COMPUTE_HTILE_COORDFROMADDR_INPUT*   pIn,    ///< [in] input structure
    ADDR_COMPUTE_HTILE_COORDFROMADDR_OUTPUT*        pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    BOOL_32 isWidth8  = (pIn->blockWidth == 8) ? TRUE : FALSE;
    BOOL_32 isHeight8 = (pIn->blockHeight == 8) ? TRUE : FALSE;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_HTILE_COORDFROMADDR_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_HTILE_COORDFROMADDR_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_COMPUTE_HTILE_COORDFROMADDR_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;
            // Use temp tile info for calcalation
            input.pTileInfo = &tileInfoNull;

            returnCode = HwlSetupTileCfg(input.tileIndex, input.macroModeIndex, input.pTileInfo);

            // Change the input structure
            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            HwlComputeXmaskCoordFromAddr(pIn->addr,
                                         pIn->bitPosition,
                                         pIn->pitch,
                                         pIn->height,
                                         pIn->numSlices,
                                         1,
                                         pIn->isLinear,
                                         isWidth8,
                                         isHeight8,
                                         pIn->pTileInfo,
                                         &pOut->x,
                                         &pOut->y,
                                         &pOut->slice);
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeCmaskAddrFromCoord
*
*   @brief
*       Interface function stub of AddrComputeCmaskAddrFromCoord
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeCmaskAddrFromCoord(
    const ADDR_COMPUTE_CMASK_ADDRFROMCOORD_INPUT*   pIn,    ///< [in] input structure
    ADDR_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT*        pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_CMASK_ADDRFROMCOORD_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_COMPUTE_CMASK_ADDRFROMCOORD_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;
            // Use temp tile info for calcalation
            input.pTileInfo = &tileInfoNull;

            returnCode = HwlSetupTileCfg(input.tileIndex, input.macroModeIndex, input.pTileInfo);

            // Change the input structure
            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            if (pIn->flags.tcCompatible == TRUE)
            {
                returnCode = HwlComputeCmaskAddrFromCoord(pIn, pOut);
            }
            else
            {
                pOut->addr = HwlComputeXmaskAddrFromCoord(pIn->pitch,
                                                          pIn->height,
                                                          pIn->x,
                                                          pIn->y,
                                                          pIn->slice,
                                                          pIn->numSlices,
                                                          2,
                                                          pIn->isLinear,
                                                          FALSE, //this is cmask, isWidth8 is not needed
                                                          FALSE, //this is cmask, isHeight8 is not needed
                                                          pIn->pTileInfo,
                                                          &pOut->bitPosition);
            }

        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeCmaskCoordFromAddr
*
*   @brief
*       Interface function stub of AddrComputeCmaskCoordFromAddr
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeCmaskCoordFromAddr(
    const ADDR_COMPUTE_CMASK_COORDFROMADDR_INPUT*   pIn,    ///< [in] input structure
    ADDR_COMPUTE_CMASK_COORDFROMADDR_OUTPUT*        pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR_COMPUTE_CMASK_COORDFROMADDR_INPUT)) ||
            (pOut->size != sizeof(ADDR_COMPUTE_CMASK_COORDFROMADDR_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        ADDR_TILEINFO tileInfoNull;
        ADDR_COMPUTE_CMASK_COORDFROMADDR_INPUT input;

        if (UseTileIndex(pIn->tileIndex))
        {
            input = *pIn;
            // Use temp tile info for calcalation
            input.pTileInfo = &tileInfoNull;

            returnCode = HwlSetupTileCfg(input.tileIndex, input.macroModeIndex, input.pTileInfo);

            // Change the input structure
            pIn = &input;
        }

        if (returnCode == ADDR_OK)
        {
            HwlComputeXmaskCoordFromAddr(pIn->addr,
                                         pIn->bitPosition,
                                         pIn->pitch,
                                         pIn->height,
                                         pIn->numSlices,
                                         2,
                                         pIn->isLinear,
                                         FALSE,
                                         FALSE,
                                         pIn->pTileInfo,
                                         &pOut->x,
                                         &pOut->y,
                                         &pOut->slice);
        }
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeTileDataWidthAndHeight
*
*   @brief
*       Compute the squared cache shape for per-tile data (CMASK and HTILE)
*
*   @return
*       N/A
*
*   @note
*       MacroWidth and macroHeight are measured in pixels
***************************************************************************************************
*/
VOID AddrLib::ComputeTileDataWidthAndHeight(
    UINT_32         bpp,             ///< [in] bits per pixel
    UINT_32         cacheBits,       ///< [in] bits of cache
    ADDR_TILEINFO*  pTileInfo,       ///< [in] Tile info
    UINT_32*        pMacroWidth,     ///< [out] macro tile width
    UINT_32*        pMacroHeight     ///< [out] macro tile height
    ) const
{
    UINT_32 height = 1;
    UINT_32 width  = cacheBits / bpp;
    UINT_32 pipes  = HwlGetPipes(pTileInfo);

    // Double height until the macro-tile is close to square
    // Height can only be doubled if width is even

    while ((width > height * 2 * pipes) && !(width & 1))
    {
        width  /= 2;
        height *= 2;
    }

    *pMacroWidth  = 8 * width;
    *pMacroHeight = 8 * height * pipes;

    // Note: The above iterative comptuation is equivalent to the following
    //
    //int log2_height = ((log2(cacheBits)-log2(bpp)-log2(pipes))/2);
    //int macroHeight = pow2( 3+log2(pipes)+log2_height );
}

/**
***************************************************************************************************
*   AddrLib::HwlComputeTileDataWidthAndHeightLinear
*
*   @brief
*       Compute the squared cache shape for per-tile data (CMASK and HTILE) for linear layout
*
*   @return
*       N/A
*
*   @note
*       MacroWidth and macroHeight are measured in pixels
***************************************************************************************************
*/
VOID AddrLib::HwlComputeTileDataWidthAndHeightLinear(
    UINT_32*        pMacroWidth,     ///< [out] macro tile width
    UINT_32*        pMacroHeight,    ///< [out] macro tile height
    UINT_32         bpp,             ///< [in] bits per pixel
    ADDR_TILEINFO*  pTileInfo        ///< [in] tile info
    ) const
{
    ADDR_ASSERT(bpp != 4);              // Cmask does not support linear layout prior to SI
    *pMacroWidth  = 8 * 512 / bpp;      // Align width to 512-bit memory accesses
    *pMacroHeight = 8 * m_pipes;        // Align height to number of pipes
}

/**
***************************************************************************************************
*   AddrLib::ComputeHtileInfo
*
*   @brief
*       Compute htile pitch,width, bytes per 2D slice
*
*   @return
*       Htile bpp i.e. How many bits for an 8x8 tile
*       Also returns by output parameters:
*       *Htile pitch, height, total size in bytes, macro-tile dimensions and slice size*
***************************************************************************************************
*/
UINT_32 AddrLib::ComputeHtileInfo(
    ADDR_HTILE_FLAGS flags,             ///< [in] htile flags
    UINT_32          pitchIn,           ///< [in] pitch input
    UINT_32          heightIn,          ///< [in] height input
    UINT_32          numSlices,         ///< [in] number of slices
    BOOL_32          isLinear,          ///< [in] if it is linear mode
    BOOL_32          isWidth8,          ///< [in] if htile block width is 8
    BOOL_32          isHeight8,         ///< [in] if htile block height is 8
    ADDR_TILEINFO*   pTileInfo,         ///< [in] Tile info
    UINT_32*         pPitchOut,         ///< [out] pitch output
    UINT_32*         pHeightOut,        ///< [out] height output
    UINT_64*         pHtileBytes,       ///< [out] bytes per 2D slice
    UINT_32*         pMacroWidth,       ///< [out] macro-tile width in pixels
    UINT_32*         pMacroHeight,      ///< [out] macro-tile width in pixels
    UINT_64*         pSliceSize,        ///< [out] slice size in bytes
    UINT_32*         pBaseAlign         ///< [out] base alignment
    ) const
{

    UINT_32 macroWidth;
    UINT_32 macroHeight;
    UINT_32 baseAlign;
    UINT_64 surfBytes;
    UINT_64 sliceBytes;

    numSlices = Max(1u, numSlices);

    const UINT_32 bpp = HwlComputeHtileBpp(isWidth8, isHeight8);
    const UINT_32 cacheBits = HtileCacheBits;

    if (isLinear)
    {
        HwlComputeTileDataWidthAndHeightLinear(&macroWidth,
                                               &macroHeight,
                                               bpp,
                                               pTileInfo);
    }
    else
    {
        ComputeTileDataWidthAndHeight(bpp,
                                      cacheBits,
                                      pTileInfo,
                                      &macroWidth,
                                      &macroHeight);
    }

    *pPitchOut = PowTwoAlign(pitchIn,  macroWidth);
    *pHeightOut = PowTwoAlign(heightIn,  macroHeight);

    baseAlign = HwlComputeHtileBaseAlign(flags.tcCompatible, isLinear, pTileInfo);

    surfBytes = HwlComputeHtileBytes(*pPitchOut,
                                     *pHeightOut,
                                     bpp,
                                     isLinear,
                                     numSlices,
                                     &sliceBytes,
                                     baseAlign);

    *pHtileBytes = surfBytes;

    //
    // Use SafeAssign since they are optional
    //
    SafeAssign(pMacroWidth, macroWidth);

    SafeAssign(pMacroHeight, macroHeight);

    SafeAssign(pSliceSize,  sliceBytes);

    SafeAssign(pBaseAlign, baseAlign);

    return bpp;
}

/**
***************************************************************************************************
*   AddrLib::ComputeCmaskBaseAlign
*
*   @brief
*       Compute cmask base alignment
*
*   @return
*       Cmask base alignment
***************************************************************************************************
*/
UINT_32 AddrLib::ComputeCmaskBaseAlign(
    ADDR_CMASK_FLAGS flags,           ///< [in] Cmask flags
    ADDR_TILEINFO*   pTileInfo        ///< [in] Tile info
    ) const
{
    UINT_32 baseAlign = m_pipeInterleaveBytes * HwlGetPipes(pTileInfo);

    if (flags.tcCompatible)
    {
        ADDR_ASSERT(pTileInfo != NULL);
        if (pTileInfo)
        {
            baseAlign *= pTileInfo->banks;
        }
    }

    return baseAlign;
}

/**
***************************************************************************************************
*   AddrLib::ComputeCmaskBytes
*
*   @brief
*       Compute cmask size in bytes
*
*   @return
*       Cmask size in bytes
***************************************************************************************************
*/
UINT_64 AddrLib::ComputeCmaskBytes(
    UINT_32 pitch,        ///< [in] pitch
    UINT_32 height,       ///< [in] height
    UINT_32 numSlices     ///< [in] number of slices
    ) const
{
    return BITS_TO_BYTES(static_cast<UINT_64>(pitch) * height * numSlices * CmaskElemBits) /
        MicroTilePixels;
}

/**
***************************************************************************************************
*   AddrLib::ComputeCmaskInfo
*
*   @brief
*       Compute cmask pitch,width, bytes per 2D slice
*
*   @return
*       BlockMax. Also by output parameters: Cmask pitch,height, total size in bytes,
*       macro-tile dimensions
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputeCmaskInfo(
    ADDR_CMASK_FLAGS flags,            ///< [in] cmask flags
    UINT_32          pitchIn,           ///< [in] pitch input
    UINT_32          heightIn,          ///< [in] height input
    UINT_32          numSlices,         ///< [in] number of slices
    BOOL_32          isLinear,          ///< [in] is linear mode
    ADDR_TILEINFO*   pTileInfo,         ///< [in] Tile info
    UINT_32*         pPitchOut,         ///< [out] pitch output
    UINT_32*         pHeightOut,        ///< [out] height output
    UINT_64*         pCmaskBytes,       ///< [out] bytes per 2D slice
    UINT_32*         pMacroWidth,       ///< [out] macro-tile width in pixels
    UINT_32*         pMacroHeight,      ///< [out] macro-tile width in pixels
    UINT_64*         pSliceSize,        ///< [out] slice size in bytes
    UINT_32*         pBaseAlign,        ///< [out] base alignment
    UINT_32*         pBlockMax          ///< [out] block max == slice / 128 / 128 - 1
    ) const
{
    UINT_32 macroWidth;
    UINT_32 macroHeight;
    UINT_32 baseAlign;
    UINT_64 surfBytes;
    UINT_64 sliceBytes;

    numSlices = Max(1u, numSlices);

    const UINT_32 bpp = CmaskElemBits;
    const UINT_32 cacheBits = CmaskCacheBits;

    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (isLinear)
    {
        HwlComputeTileDataWidthAndHeightLinear(&macroWidth,
                                               &macroHeight,
                                               bpp,
                                               pTileInfo);
    }
    else
    {
        ComputeTileDataWidthAndHeight(bpp,
                                      cacheBits,
                                      pTileInfo,
                                      &macroWidth,
                                      &macroHeight);
    }

    *pPitchOut = (pitchIn + macroWidth - 1) & ~(macroWidth - 1);
    *pHeightOut = (heightIn + macroHeight - 1) & ~(macroHeight - 1);


    sliceBytes = ComputeCmaskBytes(*pPitchOut,
                                   *pHeightOut,
                                   1);

    baseAlign = ComputeCmaskBaseAlign(flags, pTileInfo);

    while (sliceBytes % baseAlign)
    {
        *pHeightOut += macroHeight;

        sliceBytes = ComputeCmaskBytes(*pPitchOut,
                                       *pHeightOut,
                                       1);
    }

    surfBytes = sliceBytes * numSlices;

    *pCmaskBytes = surfBytes;

    //
    // Use SafeAssign since they are optional
    //
    SafeAssign(pMacroWidth, macroWidth);

    SafeAssign(pMacroHeight, macroHeight);

    SafeAssign(pBaseAlign, baseAlign);

    SafeAssign(pSliceSize, sliceBytes);

    UINT_32 slice = (*pPitchOut) * (*pHeightOut);
    UINT_32 blockMax = slice / 128 / 128 - 1;

#if DEBUG
    if (slice % (64*256) != 0)
    {
        ADDR_ASSERT_ALWAYS();
    }
#endif //DEBUG

    UINT_32 maxBlockMax = HwlGetMaxCmaskBlockMax();

    if (blockMax > maxBlockMax)
    {
        blockMax = maxBlockMax;
        returnCode = ADDR_INVALIDPARAMS;
    }

    SafeAssign(pBlockMax, blockMax);

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::ComputeXmaskCoordYFromPipe
*
*   @brief
*       Compute the Y coord from pipe number for cmask/htile
*
*   @return
*       Y coordinate
*
***************************************************************************************************
*/
UINT_32 AddrLib::ComputeXmaskCoordYFromPipe(
    UINT_32         pipe,       ///< [in] pipe number
    UINT_32         x           ///< [in] x coordinate
    ) const
{
    UINT_32 pipeBit0;
    UINT_32 pipeBit1;
    UINT_32 xBit0;
    UINT_32 xBit1;
    UINT_32 yBit0;
    UINT_32 yBit1;

    UINT_32 y = 0;

    UINT_32 numPipes = m_pipes; // SI has its implementation
    //
    // Convert pipe + x to y coordinate.
    //
    switch (numPipes)
    {
        case 1:
            //
            // 1 pipe
            //
            // p0 = 0
            //
            y = 0;
            break;
        case 2:
            //
            // 2 pipes
            //
            // p0 = x0 ^ y0
            //
            // y0 = p0 ^ x0
            //
            pipeBit0 = pipe & 0x1;

            xBit0 = x & 0x1;

            yBit0 = pipeBit0 ^ xBit0;

            y = yBit0;
            break;
        case 4:
            //
            // 4 pipes
            //
            // p0 = x1 ^ y0
            // p1 = x0 ^ y1
            //
            // y0 = p0 ^ x1
            // y1 = p1 ^ x0
            //
            pipeBit0 =  pipe & 0x1;
            pipeBit1 = (pipe & 0x2) >> 1;

            xBit0 =  x & 0x1;
            xBit1 = (x & 0x2) >> 1;

            yBit0 = pipeBit0 ^ xBit1;
            yBit1 = pipeBit1 ^ xBit0;

            y = (yBit0 |
                 (yBit1 << 1));
            break;
        case 8:
            //
            // 8 pipes
            //
            // r600 and r800 have different method
            //
            y = HwlComputeXmaskCoordYFrom8Pipe(pipe, x);
            break;
        default:
            break;
    }
    return y;
}

/**
***************************************************************************************************
*   AddrLib::HwlComputeXmaskCoordFromAddr
*
*   @brief
*       Compute the coord from an address of a cmask/htile
*
*   @return
*       N/A
*
*   @note
*       This method is reused by htile, so rename to Xmask
***************************************************************************************************
*/
VOID AddrLib::HwlComputeXmaskCoordFromAddr(
    UINT_64         addr,           ///< [in] address
    UINT_32         bitPosition,    ///< [in] bitPosition in a byte
    UINT_32         pitch,          ///< [in] pitch
    UINT_32         height,         ///< [in] height
    UINT_32         numSlices,      ///< [in] number of slices
    UINT_32         factor,         ///< [in] factor that indicates cmask or htile
    BOOL_32         isLinear,       ///< [in] linear or tiled HTILE layout
    BOOL_32         isWidth8,       ///< [in] TRUE if width is 8, FALSE means 4. It's register value
    BOOL_32         isHeight8,      ///< [in] TRUE if width is 8, FALSE means 4. It's register value
    ADDR_TILEINFO*  pTileInfo,      ///< [in] Tile info
    UINT_32*        pX,             ///< [out] x coord
    UINT_32*        pY,             ///< [out] y coord
    UINT_32*        pSlice          ///< [out] slice index
    ) const
{
    UINT_32 pipe;
    UINT_32 numPipes;
    UINT_32 numPipeBits;
    UINT_32 macroTilePitch;
    UINT_32 macroTileHeight;

    UINT_64 bitAddr;

    UINT_32 microTileCoordY;

    UINT_32 elemBits;

    UINT_32 pitchAligned = pitch;
    UINT_32 heightAligned = height;
    UINT_64 totalBytes;

    UINT_64 elemOffset;

    UINT_64 macroIndex;
    UINT_32 microIndex;

    UINT_64 macroNumber;
    UINT_32 microNumber;

    UINT_32 macroX;
    UINT_32 macroY;
    UINT_32 macroZ;

    UINT_32 microX;
    UINT_32 microY;

    UINT_32 tilesPerMacro;
    UINT_32 macrosPerPitch;
    UINT_32 macrosPerSlice;

    //
    // Extract pipe.
    //
    numPipes = HwlGetPipes(pTileInfo);
    pipe = ComputePipeFromAddr(addr, numPipes);

    //
    // Compute the number of group and pipe bits.
    //
    numPipeBits  = Log2(numPipes);

    UINT_32 groupBits = 8 * m_pipeInterleaveBytes;
    UINT_32 pipes = numPipes;


    //
    // Compute the micro tile size, in bits. And macro tile pitch and height.
    //
    if (factor == 2) //CMASK
    {
        ADDR_CMASK_FLAGS flags = {{0}};

        elemBits = CmaskElemBits;

        ComputeCmaskInfo(flags,
                         pitch,
                         height,
                         numSlices,
                         isLinear,
                         pTileInfo,
                         &pitchAligned,
                         &heightAligned,
                         &totalBytes,
                         &macroTilePitch,
                         &macroTileHeight);
    }
    else  //HTILE
    {
        ADDR_HTILE_FLAGS flags = {{0}};

        if (factor != 1)
        {
            factor = 1;
        }

        elemBits = HwlComputeHtileBpp(isWidth8, isHeight8);

        ComputeHtileInfo(flags,
                         pitch,
                         height,
                         numSlices,
                         isLinear,
                         isWidth8,
                         isHeight8,
                         pTileInfo,
                         &pitchAligned,
                         &heightAligned,
                         &totalBytes,
                         &macroTilePitch,
                         &macroTileHeight);
    }

    // Should use aligned dims
    //
    pitch = pitchAligned;
    height = heightAligned;


    //
    // Convert byte address to bit address.
    //
    bitAddr = BYTES_TO_BITS(addr) + bitPosition;


    //
    // Remove pipe bits from address.
    //

    bitAddr = (bitAddr % groupBits) + ((bitAddr/groupBits/pipes)*groupBits);


    elemOffset = bitAddr / elemBits;

    tilesPerMacro = (macroTilePitch/factor) * macroTileHeight / MicroTilePixels >> numPipeBits;

    macrosPerPitch = pitch / (macroTilePitch/factor);
    macrosPerSlice = macrosPerPitch * height / macroTileHeight;

    macroIndex = elemOffset / factor / tilesPerMacro;
    microIndex = static_cast<UINT_32>(elemOffset % (tilesPerMacro * factor));

    macroNumber = macroIndex * factor + microIndex % factor;
    microNumber = microIndex / factor;

    macroX = static_cast<UINT_32>((macroNumber % macrosPerPitch));
    macroY = static_cast<UINT_32>((macroNumber % macrosPerSlice) / macrosPerPitch);
    macroZ = static_cast<UINT_32>((macroNumber / macrosPerSlice));


    microX = microNumber % (macroTilePitch / factor / MicroTileWidth);
    microY = (microNumber / (macroTilePitch / factor / MicroTileHeight));

    *pX = macroX * (macroTilePitch/factor) + microX * MicroTileWidth;
    *pY = macroY * macroTileHeight + (microY * MicroTileHeight << numPipeBits);
    *pSlice = macroZ;

    microTileCoordY = ComputeXmaskCoordYFromPipe(pipe,
                                                 *pX/MicroTileWidth);


    //
    // Assemble final coordinates.
    //
    *pY += microTileCoordY * MicroTileHeight;

}

/**
***************************************************************************************************
*   AddrLib::HwlComputeXmaskAddrFromCoord
*
*   @brief
*       Compute the address from an address of cmask (prior to si)
*
*   @return
*       Address in bytes
*
***************************************************************************************************
*/
UINT_64 AddrLib::HwlComputeXmaskAddrFromCoord(
    UINT_32        pitch,          ///< [in] pitch
    UINT_32        height,         ///< [in] height
    UINT_32        x,              ///< [in] x coord
    UINT_32        y,              ///< [in] y coord
    UINT_32        slice,          ///< [in] slice/depth index
    UINT_32        numSlices,      ///< [in] number of slices
    UINT_32        factor,         ///< [in] factor that indicates cmask(2) or htile(1)
    BOOL_32        isLinear,       ///< [in] linear or tiled HTILE layout
    BOOL_32        isWidth8,       ///< [in] TRUE if width is 8, FALSE means 4. It's register value
    BOOL_32        isHeight8,      ///< [in] TRUE if width is 8, FALSE means 4. It's register value
    ADDR_TILEINFO* pTileInfo,      ///< [in] Tile info
    UINT_32*       pBitPosition    ///< [out] bit position inside a byte
    ) const
{
    UINT_64 addr;
    UINT_32 numGroupBits;
    UINT_32 numPipeBits;
    UINT_32 newPitch = 0;
    UINT_32 newHeight = 0;
    UINT_64 sliceBytes = 0;
    UINT_64 totalBytes = 0;
    UINT_64 sliceOffset;
    UINT_32 pipe;
    UINT_32 macroTileWidth;
    UINT_32 macroTileHeight;
    UINT_32 macroTilesPerRow;
    UINT_32 macroTileBytes;
    UINT_32 macroTileIndexX;
    UINT_32 macroTileIndexY;
    UINT_64 macroTileOffset;
    UINT_32 pixelBytesPerRow;
    UINT_32 pixelOffsetX;
    UINT_32 pixelOffsetY;
    UINT_32 pixelOffset;
    UINT_64 totalOffset;
    UINT_64 offsetLo;
    UINT_64 offsetHi;
    UINT_64 groupMask;


    UINT_32 elemBits = 0;

    UINT_32 numPipes = m_pipes; // This function is accessed prior to si only

    if (factor == 2) //CMASK
    {
        elemBits = CmaskElemBits;

        // For asics before SI, cmask is always tiled
        isLinear = FALSE;
    }
    else //HTILE
    {
        if (factor != 1) // Fix compile warning
        {
            factor = 1;
        }

        elemBits = HwlComputeHtileBpp(isWidth8, isHeight8);
    }

    //
    // Compute the number of group bits and pipe bits.
    //
    numGroupBits = Log2(m_pipeInterleaveBytes);
    numPipeBits  = Log2(numPipes);

    //
    // Compute macro tile dimensions.
    //
    if (factor == 2) // CMASK
    {
        ADDR_CMASK_FLAGS flags = {{0}};

        ComputeCmaskInfo(flags,
                         pitch,
                         height,
                         numSlices,
                         isLinear,
                         pTileInfo,
                         &newPitch,
                         &newHeight,
                         &totalBytes,
                         &macroTileWidth,
                         &macroTileHeight);

        sliceBytes = totalBytes / numSlices;
    }
    else // HTILE
    {
        ADDR_HTILE_FLAGS flags = {{0}};

        ComputeHtileInfo(flags,
                         pitch,
                         height,
                         numSlices,
                         isLinear,
                         isWidth8,
                         isHeight8,
                         pTileInfo,
                         &newPitch,
                         &newHeight,
                         &totalBytes,
                         &macroTileWidth,
                         &macroTileHeight,
                         &sliceBytes);
    }

    sliceOffset = slice * sliceBytes;

    //
    // Get the pipe.  Note that neither slice rotation nor pipe swizzling apply for CMASK.
    //
    pipe = ComputePipeFromCoord(x,
                                y,
                                0,
                                ADDR_TM_2D_TILED_THIN1,
                                0,
                                FALSE,
                                pTileInfo);

    //
    // Compute the number of macro tiles per row.
    //
    macroTilesPerRow = newPitch / macroTileWidth;

    //
    // Compute the number of bytes per macro tile.
    //
    macroTileBytes = BITS_TO_BYTES((macroTileWidth * macroTileHeight * elemBits) / MicroTilePixels);

    //
    // Compute the offset to the macro tile containing the specified coordinate.
    //
    macroTileIndexX = x / macroTileWidth;
    macroTileIndexY = y / macroTileHeight;
    macroTileOffset = ((macroTileIndexY * macroTilesPerRow) + macroTileIndexX) * macroTileBytes;

    //
    // Compute the pixel offset within the macro tile.
    //
    pixelBytesPerRow = BITS_TO_BYTES(macroTileWidth * elemBits) / MicroTileWidth;

    //
    // The nibbles are interleaved (see below), so the part of the offset relative to the x
    // coordinate repeats halfway across the row. (Not for HTILE)
    //
    if (factor == 2)
    {
        pixelOffsetX = (x % (macroTileWidth / 2)) / MicroTileWidth;
    }
    else
    {
        pixelOffsetX = (x % (macroTileWidth)) / MicroTileWidth * BITS_TO_BYTES(elemBits);
    }

    //
    // Compute the y offset within the macro tile.
    //
    pixelOffsetY = (((y % macroTileHeight) / MicroTileHeight) / numPipes) * pixelBytesPerRow;

    pixelOffset = pixelOffsetX + pixelOffsetY;

    //
    // Combine the slice offset and macro tile offset with the pixel offset, accounting for the
    // pipe bits in the middle of the address.
    //
    totalOffset = ((sliceOffset + macroTileOffset) >> numPipeBits) + pixelOffset;

    //
    // Split the offset to put some bits below the pipe bits and some above.
    //
    groupMask = (1 << numGroupBits) - 1;
    offsetLo  = totalOffset &  groupMask;
    offsetHi  = (totalOffset & ~groupMask) << numPipeBits;

    //
    // Assemble the address from its components.
    //
    addr  = offsetLo;
    addr |= offsetHi;
    // This is to remove warning with /analyze option
    UINT_32 pipeBits = pipe << numGroupBits;
    addr |= pipeBits;

    //
    // Compute the bit position.  The lower nibble is used when the x coordinate within the macro
    // tile is less than half of the macro tile width, and the upper nibble is used when the x
    // coordinate within the macro tile is greater than or equal to half the macro tile width.
    //
    *pBitPosition = ((x % macroTileWidth) < (macroTileWidth / factor)) ? 0 : 4;

    return addr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//                               Surface Addressing Shared
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
***************************************************************************************************
*   AddrLib::ComputeSurfaceAddrFromCoordLinear
*
*   @brief
*       Compute address from coord for linear surface
*
*   @return
*       Address in bytes
*
***************************************************************************************************
*/
UINT_64 AddrLib::ComputeSurfaceAddrFromCoordLinear(
    UINT_32  x,              ///< [in] x coord
    UINT_32  y,              ///< [in] y coord
    UINT_32  slice,          ///< [in] slice/depth index
    UINT_32  sample,         ///< [in] sample index
    UINT_32  bpp,            ///< [in] bits per pixel
    UINT_32  pitch,          ///< [in] pitch
    UINT_32  height,         ///< [in] height
    UINT_32  numSlices,      ///< [in] number of slices
    UINT_32* pBitPosition    ///< [out] bit position inside a byte
    ) const
{
    const UINT_64 sliceSize = static_cast<UINT_64>(pitch) * height;

    UINT_64 sliceOffset = (slice + sample * numSlices)* sliceSize;
    UINT_64 rowOffset   = static_cast<UINT_64>(y) * pitch;
    UINT_64 pixOffset   = x;

    UINT_64 addr = (sliceOffset + rowOffset + pixOffset) * bpp;

    *pBitPosition = static_cast<UINT_32>(addr % 8);
    addr /= 8;

    return addr;
}

/**
***************************************************************************************************
*   AddrLib::ComputeSurfaceCoordFromAddrLinear
*
*   @brief
*       Compute the coord from an address of a linear surface
*
*   @return
*       N/A
***************************************************************************************************
*/
VOID AddrLib::ComputeSurfaceCoordFromAddrLinear(
    UINT_64  addr,           ///< [in] address
    UINT_32  bitPosition,    ///< [in] bitPosition in a byte
    UINT_32  bpp,            ///< [in] bits per pixel
    UINT_32  pitch,          ///< [in] pitch
    UINT_32  height,         ///< [in] height
    UINT_32  numSlices,      ///< [in] number of slices
    UINT_32* pX,             ///< [out] x coord
    UINT_32* pY,             ///< [out] y coord
    UINT_32* pSlice,         ///< [out] slice/depth index
    UINT_32* pSample         ///< [out] sample index
    ) const
{
    const UINT_64 sliceSize = static_cast<UINT_64>(pitch) * height;
    const UINT_64 linearOffset = (BYTES_TO_BITS(addr) + bitPosition) / bpp;

    *pX = static_cast<UINT_32>((linearOffset % sliceSize) % pitch);
    *pY = static_cast<UINT_32>((linearOffset % sliceSize) / pitch % height);
    *pSlice  = static_cast<UINT_32>((linearOffset / sliceSize) % numSlices);
    *pSample = static_cast<UINT_32>((linearOffset / sliceSize) / numSlices);
}

/**
***************************************************************************************************
*   AddrLib::ComputeSurfaceCoordFromAddrMicroTiled
*
*   @brief
*       Compute the coord from an address of a micro tiled surface
*
*   @return
*       N/A
***************************************************************************************************
*/
VOID AddrLib::ComputeSurfaceCoordFromAddrMicroTiled(
    UINT_64         addr,               ///< [in] address
    UINT_32         bitPosition,        ///< [in] bitPosition in a byte
    UINT_32         bpp,                ///< [in] bits per pixel
    UINT_32         pitch,              ///< [in] pitch
    UINT_32         height,             ///< [in] height
    UINT_32         numSamples,         ///< [in] number of samples
    AddrTileMode    tileMode,           ///< [in] tile mode
    UINT_32         tileBase,           ///< [in] base offset within a tile
    UINT_32         compBits,           ///< [in] component bits actually needed(for planar surface)
    UINT_32*        pX,                 ///< [out] x coord
    UINT_32*        pY,                 ///< [out] y coord
    UINT_32*        pSlice,             ///< [out] slice/depth index
    UINT_32*        pSample,            ///< [out] sample index,
    AddrTileType    microTileType,      ///< [in] micro tiling order
    BOOL_32         isDepthSampleOrder  ///< [in] TRUE if in depth sample order
    ) const
{
    UINT_64 bitAddr;
    UINT_32 microTileThickness;
    UINT_32 microTileBits;
    UINT_64 sliceBits;
    UINT_64 rowBits;
    UINT_32 sliceIndex;
    UINT_32 microTileCoordX;
    UINT_32 microTileCoordY;
    UINT_32 pixelOffset;
    UINT_32 pixelCoordX = 0;
    UINT_32 pixelCoordY = 0;
    UINT_32 pixelCoordZ = 0;
    UINT_32 pixelCoordS = 0;

    //
    // Convert byte address to bit address.
    //
    bitAddr = BYTES_TO_BITS(addr) + bitPosition;

    //
    // Compute the micro tile size, in bits.
    //
    switch (tileMode)
    {
        case ADDR_TM_1D_TILED_THICK:
            microTileThickness = ThickTileThickness;
            break;
        default:
            microTileThickness = 1;
            break;
    }

    microTileBits = MicroTilePixels * microTileThickness * bpp * numSamples;

    //
    // Compute number of bits per slice and number of bits per row of micro tiles.
    //
    sliceBits = static_cast<UINT_64>(pitch) * height * microTileThickness * bpp * numSamples;

    rowBits   = (pitch / MicroTileWidth) * microTileBits;

    //
    // Extract the slice index.
    //
    sliceIndex = static_cast<UINT_32>(bitAddr / sliceBits);
    bitAddr -= sliceIndex * sliceBits;

    //
    // Extract the y coordinate of the micro tile.
    //
    microTileCoordY = static_cast<UINT_32>(bitAddr / rowBits) * MicroTileHeight;
    bitAddr -= (microTileCoordY / MicroTileHeight) * rowBits;

    //
    // Extract the x coordinate of the micro tile.
    //
    microTileCoordX = static_cast<UINT_32>(bitAddr / microTileBits) * MicroTileWidth;

    //
    // Compute the pixel offset within the micro tile.
    //
    pixelOffset = static_cast<UINT_32>(bitAddr % microTileBits);

    //
    // Extract pixel coordinates from the offset.
    //
    HwlComputePixelCoordFromOffset(pixelOffset,
                                   bpp,
                                   numSamples,
                                   tileMode,
                                   tileBase,
                                   compBits,
                                   &pixelCoordX,
                                   &pixelCoordY,
                                   &pixelCoordZ,
                                   &pixelCoordS,
                                   microTileType,
                                   isDepthSampleOrder);

    //
    // Assemble final coordinates.
    //
    *pX     = microTileCoordX + pixelCoordX;
    *pY     = microTileCoordY + pixelCoordY;
    *pSlice = (sliceIndex * microTileThickness) + pixelCoordZ;
    *pSample = pixelCoordS;

    if (microTileThickness > 1)
    {
        *pSample = 0;
    }
}

/**
***************************************************************************************************
*   AddrLib::ComputePipeFromAddr
*
*   @brief
*       Compute the pipe number from an address
*
*   @return
*       Pipe number
*
***************************************************************************************************
*/
UINT_32 AddrLib::ComputePipeFromAddr(
    UINT_64 addr,        ///< [in] address
    UINT_32 numPipes     ///< [in] number of banks
    ) const
{
    UINT_32 pipe;

    UINT_32 groupBytes = m_pipeInterleaveBytes; //just different terms

    // R600
    // The LSBs of the address are arranged as follows:
    //   bank | pipe | group
    //
    // To get the pipe number, shift off the group bits and mask the pipe bits.
    //

    // R800
    // The LSBs of the address are arranged as follows:
    //   bank | bankInterleave | pipe | pipeInterleave
    //
    // To get the pipe number, shift off the pipe interleave bits and mask the pipe bits.
    //

    pipe = static_cast<UINT_32>(addr >> Log2(groupBytes)) & (numPipes - 1);

    return pipe;
}

/**
***************************************************************************************************
*   AddrLib::ComputePixelIndexWithinMicroTile
*
*   @brief
*       Compute the pixel index inside a micro tile of surface
*
*   @return
*       Pixel index
*
***************************************************************************************************
*/
UINT_32 AddrLib::ComputePixelIndexWithinMicroTile(
    UINT_32         x,              ///< [in] x coord
    UINT_32         y,              ///< [in] y coord
    UINT_32         z,              ///< [in] slice/depth index
    UINT_32         bpp,            ///< [in] bits per pixel
    AddrTileMode    tileMode,       ///< [in] tile mode
    AddrTileType    microTileType   ///< [in] pixel order in display/non-display mode
    ) const
{
    UINT_32 pixelBit0 = 0;
    UINT_32 pixelBit1 = 0;
    UINT_32 pixelBit2 = 0;
    UINT_32 pixelBit3 = 0;
    UINT_32 pixelBit4 = 0;
    UINT_32 pixelBit5 = 0;
    UINT_32 pixelBit6 = 0;
    UINT_32 pixelBit7 = 0;
    UINT_32 pixelBit8 = 0;
    UINT_32 pixelNumber;

    UINT_32 x0 = _BIT(x, 0);
    UINT_32 x1 = _BIT(x, 1);
    UINT_32 x2 = _BIT(x, 2);
    UINT_32 y0 = _BIT(y, 0);
    UINT_32 y1 = _BIT(y, 1);
    UINT_32 y2 = _BIT(y, 2);
    UINT_32 z0 = _BIT(z, 0);
    UINT_32 z1 = _BIT(z, 1);
    UINT_32 z2 = _BIT(z, 2);

    UINT_32 thickness = ComputeSurfaceThickness(tileMode);

    // Compute the pixel number within the micro tile.

    if (microTileType != ADDR_THICK)
    {
        if (microTileType == ADDR_DISPLAYABLE)
        {
            switch (bpp)
            {
                case 8:
                    pixelBit0 = x0;
                    pixelBit1 = x1;
                    pixelBit2 = x2;
                    pixelBit3 = y1;
                    pixelBit4 = y0;
                    pixelBit5 = y2;
                    break;
                case 16:
                    pixelBit0 = x0;
                    pixelBit1 = x1;
                    pixelBit2 = x2;
                    pixelBit3 = y0;
                    pixelBit4 = y1;
                    pixelBit5 = y2;
                    break;
                case 32:
                    pixelBit0 = x0;
                    pixelBit1 = x1;
                    pixelBit2 = y0;
                    pixelBit3 = x2;
                    pixelBit4 = y1;
                    pixelBit5 = y2;
                    break;
                case 64:
                    pixelBit0 = x0;
                    pixelBit1 = y0;
                    pixelBit2 = x1;
                    pixelBit3 = x2;
                    pixelBit4 = y1;
                    pixelBit5 = y2;
                    break;
                case 128:
                    pixelBit0 = y0;
                    pixelBit1 = x0;
                    pixelBit2 = x1;
                    pixelBit3 = x2;
                    pixelBit4 = y1;
                    pixelBit5 = y2;
                    break;
                default:
                    ADDR_ASSERT_ALWAYS();
                    break;
            }
        }
        else if (microTileType == ADDR_NON_DISPLAYABLE || microTileType == ADDR_DEPTH_SAMPLE_ORDER)
        {
            pixelBit0 = x0;
            pixelBit1 = y0;
            pixelBit2 = x1;
            pixelBit3 = y1;
            pixelBit4 = x2;
            pixelBit5 = y2;
        }
        else if (microTileType == ADDR_ROTATED)
        {
            ADDR_ASSERT(thickness == 1);

            switch (bpp)
            {
                case 8:
                    pixelBit0 = y0;
                    pixelBit1 = y1;
                    pixelBit2 = y2;
                    pixelBit3 = x1;
                    pixelBit4 = x0;
                    pixelBit5 = x2;
                    break;
                case 16:
                    pixelBit0 = y0;
                    pixelBit1 = y1;
                    pixelBit2 = y2;
                    pixelBit3 = x0;
                    pixelBit4 = x1;
                    pixelBit5 = x2;
                    break;
                case 32:
                    pixelBit0 = y0;
                    pixelBit1 = y1;
                    pixelBit2 = x0;
                    pixelBit3 = y2;
                    pixelBit4 = x1;
                    pixelBit5 = x2;
                    break;
                case 64:
                    pixelBit0 = y0;
                    pixelBit1 = x0;
                    pixelBit2 = y1;
                    pixelBit3 = x1;
                    pixelBit4 = x2;
                    pixelBit5 = y2;
                    break;
                default:
                    ADDR_ASSERT_ALWAYS();
                    break;
            }
        }

        if (thickness > 1)
        {
            pixelBit6 = z0;
            pixelBit7 = z1;
        }
    }
    else // ADDR_THICK
    {
        ADDR_ASSERT(thickness > 1);

        switch (bpp)
        {
            case 8:
            case 16:
                pixelBit0 = x0;
                pixelBit1 = y0;
                pixelBit2 = x1;
                pixelBit3 = y1;
                pixelBit4 = z0;
                pixelBit5 = z1;
                break;
            case 32:
                pixelBit0 = x0;
                pixelBit1 = y0;
                pixelBit2 = x1;
                pixelBit3 = z0;
                pixelBit4 = y1;
                pixelBit5 = z1;
                break;
            case 64:
            case 128:
                pixelBit0 = y0;
                pixelBit1 = x0;
                pixelBit2 = z0;
                pixelBit3 = x1;
                pixelBit4 = y1;
                pixelBit5 = z1;
                break;
            default:
                ADDR_ASSERT_ALWAYS();
                break;
        }

        pixelBit6 = x2;
        pixelBit7 = y2;
    }

    if (thickness == 8)
    {
        pixelBit8 = z2;
    }

    pixelNumber = ((pixelBit0     ) |
                   (pixelBit1 << 1) |
                   (pixelBit2 << 2) |
                   (pixelBit3 << 3) |
                   (pixelBit4 << 4) |
                   (pixelBit5 << 5) |
                   (pixelBit6 << 6) |
                   (pixelBit7 << 7) |
                   (pixelBit8 << 8));

    return pixelNumber;
}

/**
***************************************************************************************************
*   AddrLib::AdjustPitchAlignment
*
*   @brief
*       Adjusts pitch alignment for flipping surface
*
*   @return
*       N/A
*
***************************************************************************************************
*/
VOID AddrLib::AdjustPitchAlignment(
    ADDR_SURFACE_FLAGS  flags,      ///< [in] Surface flags
    UINT_32*            pPitchAlign ///< [out] Pointer to pitch alignment
    ) const
{
    // Display engine hardwires lower 5 bit of GRPH_PITCH to ZERO which means 32 pixel alignment
    // Maybe it will be fixed in future but let's make it general for now.
    if (flags.display || flags.overlay)
    {
        *pPitchAlign = PowTwoAlign(*pPitchAlign, 32);

        if(flags.display)
        {
            *pPitchAlign = Max(m_minPitchAlignPixels, *pPitchAlign);
        }
    }
}

/**
***************************************************************************************************
*   AddrLib::PadDimensions
*
*   @brief
*       Helper function to pad dimensions
*
*   @return
*       N/A
*
***************************************************************************************************
*/
VOID AddrLib::PadDimensions(
    AddrTileMode        tileMode,    ///< [in] tile mode
    UINT_32             bpp,         ///< [in] bits per pixel
    ADDR_SURFACE_FLAGS  flags,       ///< [in] surface flags
    UINT_32             numSamples,  ///< [in] number of samples
    ADDR_TILEINFO*      pTileInfo,   ///< [in/out] bank structure.
    UINT_32             padDims,     ///< [in] Dimensions to pad valid value 1,2,3
    UINT_32             mipLevel,    ///< [in] MipLevel
    UINT_32*            pPitch,      ///< [in/out] pitch in pixels
    UINT_32             pitchAlign,  ///< [in] pitch alignment
    UINT_32*            pHeight,     ///< [in/out] height in pixels
    UINT_32             heightAlign, ///< [in] height alignment
    UINT_32*            pSlices,     ///< [in/out] number of slices
    UINT_32             sliceAlign   ///< [in] number of slice alignment
    ) const
{
    UINT_32 thickness = ComputeSurfaceThickness(tileMode);

    ADDR_ASSERT(padDims <= 3);

    //
    // Override padding for mip levels
    //
    if (mipLevel > 0)
    {
        if (flags.cube)
        {
            // for cubemap, we only pad when client call with 6 faces as an identity
            if (*pSlices > 1)
            {
                padDims = 3; // we should pad cubemap sub levels when we treat it as 3d texture
            }
            else
            {
                padDims = 2;
            }
        }
    }

    // Any possibilities that padDims is 0?
    if (padDims == 0)
    {
        padDims = 3;
    }

    if (IsPow2(pitchAlign))
    {
        *pPitch = PowTwoAlign((*pPitch), pitchAlign);
    }
    else // add this code to pass unit test, r600 linear mode is not align bpp to pow2 for linear
    {
        *pPitch += pitchAlign - 1;
        *pPitch /= pitchAlign;
        *pPitch *= pitchAlign;
    }

    if (padDims > 1)
    {
        *pHeight = PowTwoAlign((*pHeight), heightAlign);
    }

    if (padDims > 2 || thickness > 1)
    {
        // for cubemap single face, we do not pad slices.
        // if we pad it, the slice number should be set to 6 and current mip level > 1
        if (flags.cube && (!m_configFlags.noCubeMipSlicesPad || flags.cubeAsArray))
        {
            *pSlices = NextPow2(*pSlices);
        }

        // normal 3D texture or arrays or cubemap has a thick mode? (Just pass unit test)
        if (thickness > 1)
        {
            *pSlices = PowTwoAlign((*pSlices), sliceAlign);
        }

    }

    HwlPadDimensions(tileMode,
                     bpp,
                     flags,
                     numSamples,
                     pTileInfo,
                     padDims,
                     mipLevel,
                     pPitch,
                     pitchAlign,
                     pHeight,
                     heightAlign,
                     pSlices,
                     sliceAlign);
}


/**
***************************************************************************************************
*   AddrLib::HwlPreHandleBaseLvl3xPitch
*
*   @brief
*       Pre-handler of 3x pitch (96 bit) adjustment
*
*   @return
*       Expected pitch
***************************************************************************************************
*/
UINT_32 AddrLib::HwlPreHandleBaseLvl3xPitch(
    const ADDR_COMPUTE_SURFACE_INFO_INPUT*  pIn,        ///< [in] input
    UINT_32                                 expPitch    ///< [in] pitch
    ) const
{
    ADDR_ASSERT(pIn->width == expPitch);
    //
    // If pitch is pre-multiplied by 3, we retrieve original one here to get correct miplevel size
    //
    if (AddrElemLib::IsExpand3x(pIn->format) &&
        pIn->mipLevel == 0 &&
        pIn->tileMode == ADDR_TM_LINEAR_ALIGNED)
    {
        expPitch /= 3;
        expPitch = NextPow2(expPitch);
    }

    return expPitch;
}

/**
***************************************************************************************************
*   AddrLib::HwlPostHandleBaseLvl3xPitch
*
*   @brief
*       Post-handler of 3x pitch adjustment
*
*   @return
*       Expected pitch
***************************************************************************************************
*/
UINT_32 AddrLib::HwlPostHandleBaseLvl3xPitch(
    const ADDR_COMPUTE_SURFACE_INFO_INPUT*  pIn,        ///< [in] input
    UINT_32                                 expPitch    ///< [in] pitch
    ) const
{
    //
    // 96 bits surface of sub levels require element pitch of 32 bits instead
    // So we just return pitch in 32 bit pixels without timing 3
    //
    if (AddrElemLib::IsExpand3x(pIn->format) &&
        pIn->mipLevel == 0 &&
        pIn->tileMode == ADDR_TM_LINEAR_ALIGNED)
    {
        expPitch *= 3;
    }

    return expPitch;
}


/**
***************************************************************************************************
*   AddrLib::IsMacroTiled
*
*   @brief
*       Check if the tile mode is macro tiled
*
*   @return
*       TRUE if it is macro tiled (2D/2B/3D/3B)
***************************************************************************************************
*/
BOOL_32 AddrLib::IsMacroTiled(
    AddrTileMode tileMode)  ///< [in] tile mode
{
   return m_modeFlags[tileMode].isMacro;
}

/**
***************************************************************************************************
*   AddrLib::IsMacro3dTiled
*
*   @brief
*       Check if the tile mode is 3D macro tiled
*
*   @return
*       TRUE if it is 3D macro tiled
***************************************************************************************************
*/
BOOL_32 AddrLib::IsMacro3dTiled(
    AddrTileMode tileMode)  ///< [in] tile mode
{
    return m_modeFlags[tileMode].isMacro3d;
}

/**
***************************************************************************************************
*   AddrLib::IsMicroTiled
*
*   @brief
*       Check if the tile mode is micro tiled
*
*   @return
*       TRUE if micro tiled
***************************************************************************************************
*/
BOOL_32 AddrLib::IsMicroTiled(
    AddrTileMode tileMode)  ///< [in] tile mode
{
    return m_modeFlags[tileMode].isMicro;
}

/**
***************************************************************************************************
*   AddrLib::IsLinear
*
*   @brief
*       Check if the tile mode is linear
*
*   @return
*       TRUE if linear
***************************************************************************************************
*/
BOOL_32 AddrLib::IsLinear(
    AddrTileMode tileMode)  ///< [in] tile mode
{
    return m_modeFlags[tileMode].isLinear;
}

/**
***************************************************************************************************
*   AddrLib::IsPrtNoRotationTileMode
*
*   @brief
*       Return TRUE if it is prt tile without rotation
*   @note
*       This function just used by CI
***************************************************************************************************
*/
BOOL_32 AddrLib::IsPrtNoRotationTileMode(
    AddrTileMode tileMode)
{
    return m_modeFlags[tileMode].isPrtNoRotation;
}

/**
***************************************************************************************************
*   AddrLib::IsPrtTileMode
*
*   @brief
*       Return TRUE if it is prt tile
*   @note
*       This function just used by CI
***************************************************************************************************
*/
BOOL_32 AddrLib::IsPrtTileMode(
    AddrTileMode tileMode)
{
    return m_modeFlags[tileMode].isPrt;
}

/**
***************************************************************************************************
*   AddrLib::Bits2Number
*
*   @brief
*       Cat a array of binary bit to a number
*
*   @return
*       The number combined with the array of bits
***************************************************************************************************
*/
UINT_32 AddrLib::Bits2Number(
    UINT_32 bitNum,     ///< [in] how many bits
    ...)                ///< [in] varaible bits value starting from MSB
{
    UINT_32 number = 0;
    UINT_32 i;
    va_list bits_ptr;

    va_start(bits_ptr, bitNum);

    for(i = 0; i < bitNum; i++)
    {
        number |= va_arg(bits_ptr, UINT_32);
        number <<= 1;
    }

    number>>=1;

    va_end(bits_ptr);

    return number;
}

/**
***************************************************************************************************
*   AddrLib::ComputeMipLevel
*
*   @brief
*       Compute mipmap level width/height/slices
*   @return
*      N/A
***************************************************************************************************
*/
VOID AddrLib::ComputeMipLevel(
    ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn ///< [in/out] Input structure
    ) const
{
    if (AddrElemLib::IsBlockCompressed(pIn->format))
    {
        if (pIn->mipLevel == 0)
        {
            // DXTn's level 0 must be multiple of 4
            // But there are exceptions:
            // 1. Internal surface creation in hostblt/vsblt/etc...
            // 2. Runtime doesn't reject ATI1/ATI2 whose width/height are not multiple of 4
            pIn->width = PowTwoAlign(pIn->width, 4);
            pIn->height = PowTwoAlign(pIn->height, 4);
        }
    }

    HwlComputeMipLevel(pIn);
}

/**
***************************************************************************************************
*   AddrLib::DegradeBaseLevel
*
*   @brief
*       Check if base level's tile mode can be degraded
*   @return
*       TRUE if degraded, also returns degraded tile mode (unchanged if not degraded)
***************************************************************************************************
*/
BOOL_32 AddrLib::DegradeBaseLevel(
    const ADDR_COMPUTE_SURFACE_INFO_INPUT*  pIn,        ///< [in] Input structure for surface info
    AddrTileMode*                           pTileMode   ///< [out] Degraded tile mode
    ) const
{
    BOOL_32 degraded = FALSE;
    AddrTileMode tileMode = pIn->tileMode;
    UINT_32 thickness = ComputeSurfaceThickness(tileMode);

    if (m_configFlags.degradeBaseLevel) // This is a global setting
    {
        if (pIn->flags.degrade4Space        && // Degradation per surface
            pIn->mipLevel == 0              &&
            pIn->numSamples == 1            &&
            IsMacroTiled(tileMode))
        {
            if (HwlDegradeBaseLevel(pIn))
            {
                *pTileMode = thickness == 1 ? ADDR_TM_1D_TILED_THIN1 : ADDR_TM_1D_TILED_THICK;
                degraded = TRUE;
            }
            else if (thickness > 1)
            {
                // As in the following HwlComputeSurfaceInfo, thick modes may be degraded to
                // thinner modes, we should re-evaluate whether the corresponding thinner modes
                // need to be degraded. If so, we choose 1D thick mode instead.
                tileMode = DegradeLargeThickTile(pIn->tileMode, pIn->bpp);
                if (tileMode != pIn->tileMode)
                {
                    ADDR_COMPUTE_SURFACE_INFO_INPUT input = *pIn;
                    input.tileMode = tileMode;
                    if (HwlDegradeBaseLevel(&input))
                    {
                        *pTileMode = ADDR_TM_1D_TILED_THICK;
                        degraded = TRUE;
                    }
                }
            }
        }
    }

    return degraded;
}

/**
***************************************************************************************************
*   AddrLib::DegradeLargeThickTile
*
*   @brief
*       Check if the thickness needs to be reduced if a tile is too large
*   @return
*       The degraded tile mode (unchanged if not degraded)
***************************************************************************************************
*/
AddrTileMode AddrLib::DegradeLargeThickTile(
    AddrTileMode tileMode,
    UINT_32 bpp) const
{
    // Override tilemode
    // When tile_width (8) * tile_height (8) * thickness * element_bytes is > row_size,
    // it is better to just use THIN mode in this case
    UINT_32 thickness = ComputeSurfaceThickness(tileMode);

    if (thickness > 1 && m_configFlags.allowLargeThickTile == 0)
    {
        UINT_32 tileSize = MicroTilePixels * thickness * (bpp >> 3);

        if (tileSize > m_rowSize)
        {
            switch (tileMode)
            {
                case ADDR_TM_2D_TILED_XTHICK:
                    if ((tileSize >> 1) <= m_rowSize)
                    {
                        tileMode = ADDR_TM_2D_TILED_THICK;
                        break;
                    }
                    // else fall through
                case ADDR_TM_2D_TILED_THICK:
                    tileMode    = ADDR_TM_2D_TILED_THIN1;
                    break;

                case ADDR_TM_3D_TILED_XTHICK:
                    if ((tileSize >> 1) <= m_rowSize)
                    {
                        tileMode = ADDR_TM_3D_TILED_THICK;
                        break;
                    }
                    // else fall through
                case ADDR_TM_3D_TILED_THICK:
                    tileMode    = ADDR_TM_3D_TILED_THIN1;
                    break;

                case ADDR_TM_PRT_TILED_THICK:
                    tileMode    = ADDR_TM_PRT_TILED_THIN1;
                    break;

                case ADDR_TM_PRT_2D_TILED_THICK:
                    tileMode    = ADDR_TM_PRT_2D_TILED_THIN1;
                    break;

                case ADDR_TM_PRT_3D_TILED_THICK:
                    tileMode    = ADDR_TM_PRT_3D_TILED_THIN1;
                    break;

                default:
                    break;
            }
        }
    }

    return tileMode;
}

/**
***************************************************************************************************
*   AddrLib::PostComputeMipLevel
*   @brief
*       Compute MipLevel info (including level 0) after surface adjustment
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::PostComputeMipLevel(
    ADDR_COMPUTE_SURFACE_INFO_INPUT*    pIn,   ///< [in/out] Input structure
    ADDR_COMPUTE_SURFACE_INFO_OUTPUT*   pOut   ///< [out] Output structure
    ) const
{
    // Mipmap including level 0 must be pow2 padded since either SI hw expects so or it is
    // required by CFX  for Hw Compatibility between NI and SI. Otherwise it is only needed for
    // mipLevel > 0. Any h/w has different requirement should implement its own virtual function

    if (pIn->flags.pow2Pad)
    {
        pIn->width      = NextPow2(pIn->width);
        pIn->height     = NextPow2(pIn->height);
        pIn->numSlices  = NextPow2(pIn->numSlices);
    }
    else if (pIn->mipLevel > 0)
    {
        pIn->width      = NextPow2(pIn->width);
        pIn->height     = NextPow2(pIn->height);

        if (!pIn->flags.cube)
        {
            pIn->numSlices = NextPow2(pIn->numSlices);
        }

        // for cubemap, we keep its value at first
    }

    return ADDR_OK;
}

/**
***************************************************************************************************
*   AddrLib::HwlSetupTileCfg
*
*   @brief
*       Map tile index to tile setting.
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::HwlSetupTileCfg(
    INT_32          index,            ///< [in] Tile index
    INT_32          macroModeIndex,   ///< [in] Index in macro tile mode table(CI)
    ADDR_TILEINFO*  pInfo,            ///< [out] Tile Info
    AddrTileMode*   pMode,            ///< [out] Tile mode
    AddrTileType*   pType             ///< [out] Tile type
    ) const
{
    return ADDR_NOTSUPPORTED;
}

/**
***************************************************************************************************
*   AddrLib::HwlGetPipes
*
*   @brief
*       Get number pipes
*   @return
*       num pipes
***************************************************************************************************
*/
UINT_32 AddrLib::HwlGetPipes(
    const ADDR_TILEINFO* pTileInfo    ///< [in] Tile info
    ) const
{
    //pTileInfo can be NULL when asic is 6xx and 8xx.
    return m_pipes;
}

/**
***************************************************************************************************
*   AddrLib::ComputeQbStereoInfo
*
*   @brief
*       Get quad buffer stereo information
*   @return
*       TRUE if no error
***************************************************************************************************
*/
BOOL_32 AddrLib::ComputeQbStereoInfo(
    ADDR_COMPUTE_SURFACE_INFO_OUTPUT*       pOut    ///< [in/out] updated pOut+pStereoInfo
    ) const
{
    BOOL_32 success = FALSE;

    if (pOut->pStereoInfo)
    {
        ADDR_ASSERT(pOut->bpp >= 8);
        ADDR_ASSERT((pOut->surfSize % pOut->baseAlign) == 0);

        // Save original height
        pOut->pStereoInfo->eyeHeight = pOut->height;

        // Right offset
        pOut->pStereoInfo->rightOffset = static_cast<UINT_32>(pOut->surfSize);

        pOut->pStereoInfo->rightSwizzle = HwlComputeQbStereoRightSwizzle(pOut);
        // Double height
        pOut->height <<= 1;
        pOut->pixelHeight <<= 1;

        // Double size
        pOut->surfSize <<= 1;

        // Right start address meets the base align since it is guaranteed by AddrLib

        // 1D surface on SI may break this rule, but we can force it to meet by checking .qbStereo.
        success = TRUE;
    }

    return success;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//                               Element lib
///////////////////////////////////////////////////////////////////////////////////////////////////


/**
***************************************************************************************************
*   AddrLib::Flt32ToColorPixel
*
*   @brief
*       Convert a FLT_32 value to a depth/stencil pixel value
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::Flt32ToDepthPixel(
    const ELEM_FLT32TODEPTHPIXEL_INPUT* pIn,
    ELEM_FLT32TODEPTHPIXEL_OUTPUT* pOut) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ELEM_FLT32TODEPTHPIXEL_INPUT)) ||
            (pOut->size != sizeof(ELEM_FLT32TODEPTHPIXEL_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        GetElemLib()->Flt32ToDepthPixel(pIn->format,
                                        pIn->comps,
                                        pOut->pPixel);
        UINT_32 depthBase = 0;
        UINT_32 stencilBase = 0;
        UINT_32 depthBits = 0;
        UINT_32 stencilBits = 0;

        switch (pIn->format)
        {
            case ADDR_DEPTH_16:
                depthBits = 16;
                break;
            case ADDR_DEPTH_X8_24:
            case ADDR_DEPTH_8_24:
            case ADDR_DEPTH_X8_24_FLOAT:
            case ADDR_DEPTH_8_24_FLOAT:
                depthBase = 8;
                depthBits = 24;
                stencilBits = 8;
                break;
            case ADDR_DEPTH_32_FLOAT:
                depthBits = 32;
                break;
            case ADDR_DEPTH_X24_8_32_FLOAT:
                depthBase = 8;
                depthBits = 32;
                stencilBits = 8;
                break;
            default:
                break;
        }

        // Overwrite base since R800 has no "tileBase"
        if (GetElemLib()->IsDepthStencilTilePlanar() == FALSE)
        {
            depthBase = 0;
            stencilBase = 0;
        }

        depthBase *= 64;
        stencilBase *= 64;

        pOut->stencilBase = stencilBase;
        pOut->depthBase = depthBase;
        pOut->depthBits = depthBits;
        pOut->stencilBits = stencilBits;
    }

    return returnCode;
}

/**
***************************************************************************************************
*   AddrLib::Flt32ToColorPixel
*
*   @brief
*       Convert a FLT_32 value to a red/green/blue/alpha pixel value
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::Flt32ToColorPixel(
    const ELEM_FLT32TOCOLORPIXEL_INPUT* pIn,
    ELEM_FLT32TOCOLORPIXEL_OUTPUT* pOut) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ELEM_FLT32TOCOLORPIXEL_INPUT)) ||
            (pOut->size != sizeof(ELEM_FLT32TOCOLORPIXEL_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        GetElemLib()->Flt32ToColorPixel(pIn->format,
                                        pIn->surfNum,
                                        pIn->surfSwap,
                                        pIn->comps,
                                        pOut->pPixel);
    }

    return returnCode;
}


/**
***************************************************************************************************
*   AddrLib::GetExportNorm
*
*   @brief
*       Check one format can be EXPORT_NUM
*   @return
*       TRUE if EXPORT_NORM can be used
***************************************************************************************************
*/
BOOL_32 AddrLib::GetExportNorm(
    const ELEM_GETEXPORTNORM_INPUT* pIn) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    BOOL_32 enabled = FALSE;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if (pIn->size != sizeof(ELEM_GETEXPORTNORM_INPUT))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if (returnCode == ADDR_OK)
    {
        enabled = GetElemLib()->PixGetExportNorm(pIn->format,
                                                 pIn->num,
                                                 pIn->swap);
    }

    return enabled;
}

/**
***************************************************************************************************
*   AddrLib::ComputePrtInfo
*
*   @brief
*       Compute prt surface related info
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE AddrLib::ComputePrtInfo(
    const ADDR_PRT_INFO_INPUT*  pIn,
    ADDR_PRT_INFO_OUTPUT*       pOut) const
{
    ADDR_ASSERT(pOut != NULL);

    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    UINT_32     expandX = 1;
    UINT_32     expandY = 1;
    AddrElemMode elemMode;

    UINT_32     bpp = GetElemLib()->GetBitsPerPixel(pIn->format,
                                                &elemMode,
                                                &expandX,
                                                &expandY);

    if (bpp <8 || bpp == 24 || bpp == 48 || bpp == 96 )
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    UINT_32     numFrags = pIn->numFrags;
    ADDR_ASSERT(numFrags <= 8);

    UINT_32     tileWidth = 0;
    UINT_32     tileHeight = 0;
    if (returnCode == ADDR_OK)
    {
        // 3D texture without depth or 2d texture
        if (pIn->baseMipDepth > 1 || pIn->baseMipHeight > 1)
        {
            if (bpp == 8)
            {
                tileWidth = 256;
                tileHeight = 256;
            }
            else if (bpp == 16)
            {
                tileWidth = 256;
                tileHeight = 128;
            }
            else if (bpp == 32)
            {
                tileWidth = 128;
                tileHeight = 128;
            }
            else if (bpp == 64)
            {
                // assume it is BC1/4
                tileWidth = 512;
                tileHeight = 256;

                if (elemMode == ADDR_UNCOMPRESSED)
                {
                    tileWidth = 128;
                    tileHeight = 64;
                }
            }
            else if (bpp == 128)
            {
                // assume it is BC2/3/5/6H/7
                tileWidth = 256;
                tileHeight = 256;

                if (elemMode == ADDR_UNCOMPRESSED)
                {
                    tileWidth = 64;
                    tileHeight = 64;
                }
            }

            if (numFrags == 2)
            {
                tileWidth = tileWidth / 2;
            }
            else if (numFrags == 4)
            {
                tileWidth = tileWidth / 2;
                tileHeight = tileHeight / 2;
            }
            else if (numFrags == 8)
            {
                tileWidth = tileWidth / 4;
                tileHeight = tileHeight / 2;
            }
        }
        else    // 1d
        {
            tileHeight = 1;
            if (bpp == 8)
            {
                tileWidth = 65536;
            }
            else if (bpp == 16)
            {
                tileWidth = 32768;
            }
            else if (bpp == 32)
            {
                tileWidth = 16384;
            }
            else if (bpp == 64)
            {
                tileWidth = 8192;
            }
            else if (bpp == 128)
            {
                tileWidth = 4096;
            }
        }
    }

    pOut->prtTileWidth = tileWidth;
    pOut->prtTileHeight = tileHeight;

    return returnCode;
}
