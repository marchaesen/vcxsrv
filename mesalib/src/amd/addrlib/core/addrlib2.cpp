/*
 * Copyright © 2017 Advanced Micro Devices, Inc.
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
****************************************************************************************************
* @file  addrlib2.cpp
* @brief Contains the implementation for the AddrLib2 base class.
****************************************************************************************************
*/

#include "addrinterface.h"
#include "addrlib2.h"
#include "addrcommon.h"

namespace Addr
{
namespace V2
{

////////////////////////////////////////////////////////////////////////////////////////////////////
//                               Static Const Member
////////////////////////////////////////////////////////////////////////////////////////////////////

const SwizzleModeFlags Lib::SwizzleModeTable[ADDR_SW_MAX_TYPE] =
{//Linear 256B  4KB  64KB   Var    Z    Std   Disp  Rot   XOR    T
    {1,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0},//ADDR_SW_LINEAR
    {0,    1,    0,    0,    0,    0,    1,    0,    0,    0,    0},//ADDR_SW_256B_S
    {0,    1,    0,    0,    0,    0,    0,    1,    0,    0,    0},//ADDR_SW_256B_D
    {0,    1,    0,    0,    0,    0,    0,    0,    1,    0,    0},//ADDR_SW_256B_R

    {0,    0,    1,    0,    0,    1,    0,    0,    0,    0,    0},//ADDR_SW_4KB_Z
    {0,    0,    1,    0,    0,    0,    1,    0,    0,    0,    0},//ADDR_SW_4KB_S
    {0,    0,    1,    0,    0,    0,    0,    1,    0,    0,    0},//ADDR_SW_4KB_D
    {0,    0,    1,    0,    0,    0,    0,    0,    1,    0,    0},//ADDR_SW_4KB_R

    {0,    0,    0,    1,    0,    1,    0,    0,    0,    0,    0},//ADDR_SW_64KB_Z
    {0,    0,    0,    1,    0,    0,    1,    0,    0,    0,    0},//ADDR_SW_64KB_S
    {0,    0,    0,    1,    0,    0,    0,    1,    0,    0,    0},//ADDR_SW_64KB_D
    {0,    0,    0,    1,    0,    0,    0,    0,    1,    0,    0},//ADDR_SW_64KB_R

    {0,    0,    0,    0,    1,    1,    0,    0,    0,    0,    0},//ADDR_SW_VAR_Z
    {0,    0,    0,    0,    1,    0,    1,    0,    0,    0,    0},//ADDR_SW_VAR_S
    {0,    0,    0,    0,    1,    0,    0,    1,    0,    0,    0},//ADDR_SW_VAR_D
    {0,    0,    0,    0,    1,    0,    0,    0,    1,    0,    0},//ADDR_SW_VAR_R

    {0,    0,    0,    1,    0,    1,    0,    0,    0,    1,    1},//ADDR_SW_64KB_Z_T
    {0,    0,    0,    1,    0,    0,    1,    0,    0,    1,    1},//ADDR_SW_64KB_S_T
    {0,    0,    0,    1,    0,    0,    0,    1,    0,    1,    1},//ADDR_SW_64KB_D_T
    {0,    0,    0,    1,    0,    0,    0,    0,    1,    1,    1},//ADDR_SW_64KB_R_T

    {0,    0,    1,    0,    0,    1,    0,    0,    0,    1,    0},//ADDR_SW_4KB_Z_x
    {0,    0,    1,    0,    0,    0,    1,    0,    0,    1,    0},//ADDR_SW_4KB_S_x
    {0,    0,    1,    0,    0,    0,    0,    1,    0,    1,    0},//ADDR_SW_4KB_D_x
    {0,    0,    1,    0,    0,    0,    0,    0,    1,    1,    0},//ADDR_SW_4KB_R_x

    {0,    0,    0,    1,    0,    1,    0,    0,    0,    1,    0},//ADDR_SW_64KB_Z_X
    {0,    0,    0,    1,    0,    0,    1,    0,    0,    1,    0},//ADDR_SW_64KB_S_X
    {0,    0,    0,    1,    0,    0,    0,    1,    0,    1,    0},//ADDR_SW_64KB_D_X
    {0,    0,    0,    1,    0,    0,    0,    0,    1,    1,    0},//ADDR_SW_64KB_R_X

    {0,    0,    0,    0,    1,    1,    0,    0,    0,    1,    0},//ADDR_SW_VAR_Z_X
    {0,    0,    0,    0,    1,    0,    1,    0,    0,    1,    0},//ADDR_SW_VAR_S_X
    {0,    0,    0,    0,    1,    0,    0,    1,    0,    1,    0},//ADDR_SW_VAR_D_X
    {0,    0,    0,    0,    1,    0,    0,    0,    1,    1,    0},//ADDR_SW_VAR_R_X
    {1,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0},//ADDR_SW_LINEAR_GENERAL
};

const Dim2d Lib::Block256b[] = {{16, 16}, {16, 8}, {8, 8}, {8, 4}, {4, 4}};

const Dim3d Lib::Block1kb[] = {{16, 8, 8}, {8, 8, 8}, {8, 8, 4}, {8, 4, 4}, {4, 4, 4}};

const Dim2d Lib::CompressBlock2d[] = {{16, 16}, {16, 8}, {8, 8}, {8, 4}, {4, 4}};

const Dim3d Lib::CompressBlock3dS[] = {{16, 4, 4}, {8, 4, 4}, {4, 4, 4}, {2, 4, 4}, {1, 4, 4}};

const Dim3d Lib::CompressBlock3dZ[] = {{8, 4, 8}, {4, 4, 8}, {4, 4, 4}, {4, 2, 4}, {2, 2, 4}};

const UINT_32 Lib::MaxMacroBits = 20;

const UINT_32 Lib::MipTailOffset[] = {2048, 1024, 512, 256, 128, 64, 32, 16,
                                      8, 6, 5, 4, 3, 2, 1, 0};

////////////////////////////////////////////////////////////////////////////////////////////////////
//                               Constructor/Destructor
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
****************************************************************************************************
*   Lib::Lib
*
*   @brief
*       Constructor for the Addr::V2::Lib class
*
****************************************************************************************************
*/
Lib::Lib()
    :
    Addr::Lib()
{
}

/**
****************************************************************************************************
*   Lib::Lib
*
*   @brief
*       Constructor for the AddrLib2 class with hClient as parameter
*
****************************************************************************************************
*/
Lib::Lib(const Client* pClient)
    :
    Addr::Lib(pClient)
{
}

/**
****************************************************************************************************
*   Lib::~Lib
*
*   @brief
*       Destructor for the AddrLib2 class
*
****************************************************************************************************
*/
Lib::~Lib()
{
}

/**
****************************************************************************************************
*   Lib::GetLib
*
*   @brief
*       Get Addr::V2::Lib pointer
*
*   @return
*      An Addr::V2::Lib class pointer
****************************************************************************************************
*/
Lib* Lib::GetLib(
    ADDR_HANDLE hLib)   ///< [in] handle of ADDR_HANDLE
{
    Addr::Lib* pAddrLib = Addr::Lib::GetLib(hLib);
    if ((pAddrLib != NULL) &&
        (pAddrLib->GetChipFamily() <= ADDR_CHIP_FAMILY_VI))
    {
        // only valid and GFX9+ AISC can use AddrLib2 function.
        ADDR_ASSERT_ALWAYS();
        hLib = NULL;
    }
    return static_cast<Lib*>(hLib);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//                               Surface Methods
////////////////////////////////////////////////////////////////////////////////////////////////////


/**
****************************************************************************************************
*   Lib::ComputeSurfaceInfo
*
*   @brief
*       Interface function stub of AddrComputeSurfaceInfo.
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceInfo(
     const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,    ///< [in] input structure
     ADDR2_COMPUTE_SURFACE_INFO_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR2_COMPUTE_SURFACE_INFO_INPUT)) ||
            (pOut->size != sizeof(ADDR2_COMPUTE_SURFACE_INFO_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    // Adjust coming parameters.
    ADDR2_COMPUTE_SURFACE_INFO_INPUT localIn = *pIn;
    localIn.width = Max(pIn->width, 1u);
    localIn.height = Max(pIn->height, 1u);
    localIn.numMipLevels = Max(pIn->numMipLevels, 1u);
    localIn.numSlices = Max(pIn->numSlices, 1u);
    localIn.numSamples = Max(pIn->numSamples, 1u);
    localIn.numFrags = (localIn.numFrags == 0) ? localIn.numSamples : pIn->numFrags;

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
                ADDR_ASSERT((localIn.swizzleMode == ADDR_SW_LINEAR) || (localIn.height == 1));
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
            ADDR_ASSERT_ALWAYS();

            returnCode = ADDR_INVALIDPARAMS;
        }
    }

    if (returnCode == ADDR_OK)
    {
        returnCode = ComputeSurfaceInfoSanityCheck(&localIn);
    }

    if (returnCode == ADDR_OK)
    {
        VerifyMipLevelInfo(pIn);

        if (IsLinear(pIn->swizzleMode))
        {
            // linear mode
            returnCode = ComputeSurfaceInfoLinear(&localIn, pOut);
        }
        else
        {
            // tiled mode
            returnCode = ComputeSurfaceInfoTiled(&localIn, pOut);
        }

        if (returnCode == ADDR_OK)
        {
            pOut->bpp = localIn.bpp;
            pOut->pixelPitch = pOut->pitch;
            pOut->pixelHeight = pOut->height;
            pOut->pixelMipChainPitch = pOut->mipChainPitch;
            pOut->pixelMipChainHeight = pOut->mipChainHeight;
            pOut->pixelBits = localIn.bpp;

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
            }

            if (localIn.flags.needEquation && (Log2(localIn.numFrags) == 0))
            {
                pOut->equationIndex = GetEquationIndex(&localIn, pOut);
            }
        }
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeSurfaceInfo
*
*   @brief
*       Interface function stub of AddrComputeSurfaceInfo.
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceAddrFromCoord(
    const ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,    ///< [in] input structure
    ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT*      pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT)) ||
            (pOut->size != sizeof(ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT localIn = *pIn;
    localIn.unalignedWidth = Max(pIn->unalignedWidth, 1u);
    localIn.unalignedHeight = Max(pIn->unalignedHeight, 1u);
    localIn.numMipLevels = Max(pIn->numMipLevels, 1u);
    localIn.numSlices = Max(pIn->numSlices, 1u);
    localIn.numSamples = Max(pIn->numSamples, 1u);
    localIn.numFrags = Max(pIn->numFrags, 1u);

    if ((localIn.bpp < 8)        ||
        (localIn.bpp > 128)      ||
        ((localIn.bpp % 8) != 0) ||
        (localIn.sample >= localIn.numSamples)  ||
        (localIn.slice >= localIn.numSlices)    ||
        (localIn.mipId >= localIn.numMipLevels) ||
        (IsTex3d(localIn.resourceType) &&
         (Valid3DMipSliceIdConstraint(localIn.numSlices, localIn.mipId, localIn.slice) == FALSE)))
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
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeSurfaceCoordFromAddr
*
*   @brief
*       Interface function stub of ComputeSurfaceCoordFromAddr.
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceCoordFromAddr(
    const ADDR2_COMPUTE_SURFACE_COORDFROMADDR_INPUT* pIn,    ///< [in] input structure
    ADDR2_COMPUTE_SURFACE_COORDFROMADDR_OUTPUT*      pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (GetFillSizeFieldsFlags() == TRUE)
    {
        if ((pIn->size != sizeof(ADDR2_COMPUTE_SURFACE_COORDFROMADDR_INPUT)) ||
            (pOut->size != sizeof(ADDR2_COMPUTE_SURFACE_COORDFROMADDR_OUTPUT)))
        {
            returnCode = ADDR_PARAMSIZEMISMATCH;
        }
    }

    if ((pIn->bpp < 8)        ||
        (pIn->bpp > 128)      ||
        ((pIn->bpp % 8) != 0) ||
        (pIn->bitPosition >= 8))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    if (returnCode == ADDR_OK)
    {
        if (IsLinear(pIn->swizzleMode))
        {
            returnCode = ComputeSurfaceCoordFromAddrLinear(pIn, pOut);
        }
        else
        {
            returnCode = ComputeSurfaceCoordFromAddrTiled(pIn, pOut);
        }
    }

    return returnCode;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//                               CMASK/HTILE
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
****************************************************************************************************
*   Lib::ComputeHtileInfo
*
*   @brief
*       Interface function stub of AddrComputeHtilenfo
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeHtileInfo(
    const ADDR2_COMPUTE_HTILE_INFO_INPUT*    pIn,    ///< [in] input structure
    ADDR2_COMPUTE_HTILE_INFO_OUTPUT*         pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode;

    if ((GetFillSizeFieldsFlags() == TRUE) &&
        ((pIn->size != sizeof(ADDR2_COMPUTE_HTILE_INFO_INPUT)) ||
         (pOut->size != sizeof(ADDR2_COMPUTE_HTILE_INFO_OUTPUT))))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else
    {
        returnCode = HwlComputeHtileInfo(pIn, pOut);
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeHtileAddrFromCoord
*
*   @brief
*       Interface function stub of AddrComputeHtileAddrFromCoord
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeHtileAddrFromCoord(
    const ADDR2_COMPUTE_HTILE_ADDRFROMCOORD_INPUT*   pIn,    ///< [in] input structure
    ADDR2_COMPUTE_HTILE_ADDRFROMCOORD_OUTPUT*        pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode;

    if ((GetFillSizeFieldsFlags() == TRUE) &&
        ((pIn->size != sizeof(ADDR2_COMPUTE_HTILE_ADDRFROMCOORD_INPUT)) ||
         (pOut->size != sizeof(ADDR2_COMPUTE_HTILE_ADDRFROMCOORD_OUTPUT))))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else
    {
        returnCode = HwlComputeHtileAddrFromCoord(pIn, pOut);
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeHtileCoordFromAddr
*
*   @brief
*       Interface function stub of AddrComputeHtileCoordFromAddr
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeHtileCoordFromAddr(
    const ADDR2_COMPUTE_HTILE_COORDFROMADDR_INPUT*   pIn,    ///< [in] input structure
    ADDR2_COMPUTE_HTILE_COORDFROMADDR_OUTPUT*        pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode;

    if ((GetFillSizeFieldsFlags() == TRUE) &&
        ((pIn->size != sizeof(ADDR2_COMPUTE_HTILE_COORDFROMADDR_INPUT)) ||
         (pOut->size != sizeof(ADDR2_COMPUTE_HTILE_COORDFROMADDR_OUTPUT))))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else
    {
        returnCode = HwlComputeHtileCoordFromAddr(pIn, pOut);
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeCmaskInfo
*
*   @brief
*       Interface function stub of AddrComputeCmaskInfo
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeCmaskInfo(
    const ADDR2_COMPUTE_CMASK_INFO_INPUT*    pIn,    ///< [in] input structure
    ADDR2_COMPUTE_CMASK_INFO_OUTPUT*         pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode;

    if ((GetFillSizeFieldsFlags() == TRUE) &&
        ((pIn->size != sizeof(ADDR2_COMPUTE_CMASK_INFO_INPUT)) ||
         (pOut->size != sizeof(ADDR2_COMPUTE_CMASK_INFO_OUTPUT))))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else if (pIn->cMaskFlags.linear)
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else
    {
        returnCode = HwlComputeCmaskInfo(pIn, pOut);
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeCmaskAddrFromCoord
*
*   @brief
*       Interface function stub of AddrComputeCmaskAddrFromCoord
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeCmaskAddrFromCoord(
    const ADDR2_COMPUTE_CMASK_ADDRFROMCOORD_INPUT*   pIn,    ///< [in] input structure
    ADDR2_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT*        pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode;

    if ((GetFillSizeFieldsFlags() == TRUE) &&
        ((pIn->size != sizeof(ADDR2_COMPUTE_CMASK_ADDRFROMCOORD_INPUT)) ||
         (pOut->size != sizeof(ADDR2_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT))))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else
    {
        returnCode = HwlComputeCmaskAddrFromCoord(pIn, pOut);
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeCmaskCoordFromAddr
*
*   @brief
*       Interface function stub of AddrComputeCmaskCoordFromAddr
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeCmaskCoordFromAddr(
    const ADDR2_COMPUTE_CMASK_COORDFROMADDR_INPUT*   pIn,    ///< [in] input structure
    ADDR2_COMPUTE_CMASK_COORDFROMADDR_OUTPUT*        pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_NOTIMPLEMENTED;

    ADDR_NOT_IMPLEMENTED();

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeFmaskInfo
*
*   @brief
*       Interface function stub of ComputeFmaskInfo.
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeFmaskInfo(
    const ADDR2_COMPUTE_FMASK_INFO_INPUT*    pIn,    ///< [in] input structure
    ADDR2_COMPUTE_FMASK_INFO_OUTPUT*         pOut    ///< [out] output structure
    )
{
    ADDR_E_RETURNCODE returnCode;

    BOOL_32 valid = (IsZOrderSwizzle(pIn->swizzleMode) == TRUE) &&
                    ((pIn->numSamples > 0) || (pIn->numFrags > 0));

    if (GetFillSizeFieldsFlags())
    {
        if ((pIn->size != sizeof(ADDR2_COMPUTE_FMASK_INFO_INPUT)) ||
            (pOut->size != sizeof(ADDR2_COMPUTE_FMASK_INFO_OUTPUT)))
        {
            valid = FALSE;
        }
    }

    if (valid == FALSE)
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else
    {
        ADDR2_COMPUTE_SURFACE_INFO_INPUT  localIn = {0};
        ADDR2_COMPUTE_SURFACE_INFO_OUTPUT localOut = {0};

        localIn.size = sizeof(ADDR2_COMPUTE_SURFACE_INFO_INPUT);
        localOut.size = sizeof(ADDR2_COMPUTE_SURFACE_INFO_OUTPUT);

        localIn.swizzleMode  = pIn->swizzleMode;
        localIn.numSlices    = Max(pIn->numSlices, 1u);
        localIn.width        = Max(pIn->unalignedWidth, 1u);
        localIn.height       = Max(pIn->unalignedHeight, 1u);
        localIn.bpp          = GetFmaskBpp(pIn->numSamples, pIn->numFrags);
        localIn.flags.fmask  = 1;
        localIn.numFrags     = 1;
        localIn.numSamples   = 1;
        localIn.resourceType = ADDR_RSRC_TEX_2D;

        if (localIn.bpp == 8)
        {
            localIn.format = ADDR_FMT_8;
        }
        else if (localIn.bpp == 16)
        {
            localIn.format = ADDR_FMT_16;
        }
        else if (localIn.bpp == 32)
        {
            localIn.format = ADDR_FMT_32;
        }
        else
        {
            localIn.format = ADDR_FMT_32_32;
        }

        returnCode = ComputeSurfaceInfo(&localIn, &localOut);

        if (returnCode == ADDR_OK)
        {
            pOut->pitch      = localOut.pitch;
            pOut->height     = localOut.height;
            pOut->baseAlign  = localOut.baseAlign;
            pOut->numSlices  = localOut.numSlices;
            pOut->fmaskBytes = static_cast<UINT_32>(localOut.surfSize);
            pOut->sliceSize  = localOut.sliceSize;
            pOut->bpp        = localIn.bpp;
            pOut->numSamples = 1;
        }
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeFmaskAddrFromCoord
*
*   @brief
*       Interface function stub of ComputeFmaskAddrFromCoord.
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeFmaskAddrFromCoord(
    const ADDR2_COMPUTE_FMASK_ADDRFROMCOORD_INPUT*   pIn,    ///< [in] input structure
    ADDR2_COMPUTE_FMASK_ADDRFROMCOORD_OUTPUT*        pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_NOTIMPLEMENTED;

    ADDR_NOT_IMPLEMENTED();

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeFmaskCoordFromAddr
*
*   @brief
*       Interface function stub of ComputeFmaskAddrFromCoord.
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeFmaskCoordFromAddr(
    const ADDR2_COMPUTE_FMASK_COORDFROMADDR_INPUT*  pIn,     ///< [in] input structure
    ADDR2_COMPUTE_FMASK_COORDFROMADDR_OUTPUT*       pOut     ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_NOTIMPLEMENTED;

    ADDR_NOT_IMPLEMENTED();

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::GetMetaMiptailInfo
*
*   @brief
*       Get mip tail coordinate information.
*
*   @return
*       N/A
****************************************************************************************************
*/
VOID Lib::GetMetaMiptailInfo(
    ADDR2_META_MIP_INFO*    pInfo,          ///< [out] output structure to store per mip coord
    Dim3d                   mipCoord,       ///< [in] mip tail base coord
    UINT_32                 numMipInTail,   ///< [in] number of mips in tail
    Dim3d*                  pMetaBlkDim     ///< [in] meta block width/height/depth
    ) const
{
    BOOL_32 isThick = (pMetaBlkDim->d > 1);
    UINT_32 mipWidth  = pMetaBlkDim->w;
    UINT_32 mipHeight = pMetaBlkDim->h >> 1;
    UINT_32 mipDepth  = pMetaBlkDim->d;
    UINT_32 minInc;

    if (isThick)
    {
        minInc = (pMetaBlkDim->h >= 512) ? 128 : ((pMetaBlkDim->h == 256) ? 64 : 32);
    }
    else if (pMetaBlkDim->h >= 1024)
    {
        minInc = 256;
    }
    else if (pMetaBlkDim->h == 512)
    {
        minInc = 128;
    }
    else
    {
        minInc = 64;
    }

    UINT_32 blk32MipId = 0xFFFFFFFF;

    for (UINT_32 mip = 0; mip < numMipInTail; mip++)
    {
        pInfo[mip].inMiptail = TRUE;
        pInfo[mip].startX = mipCoord.w;
        pInfo[mip].startY = mipCoord.h;
        pInfo[mip].startZ = mipCoord.d;
        pInfo[mip].width = mipWidth;
        pInfo[mip].height = mipHeight;
        pInfo[mip].depth = mipDepth;

        if (mipWidth <= 32)
        {
            if (blk32MipId == 0xFFFFFFFF)
            {
                blk32MipId = mip;
            }

            mipCoord.w = pInfo[blk32MipId].startX;
            mipCoord.h = pInfo[blk32MipId].startY;
            mipCoord.d = pInfo[blk32MipId].startZ;

            switch (mip - blk32MipId)
            {
                case 0:
                    mipCoord.w += 32;       // 16x16
                    break;
                case 1:
                    mipCoord.h += 32;       // 8x8
                    break;
                case 2:
                    mipCoord.h += 32;       // 4x4
                    mipCoord.w += 16;
                    break;
                case 3:
                    mipCoord.h += 32;       // 2x2
                    mipCoord.w += 32;
                    break;
                case 4:
                    mipCoord.h += 32;       // 1x1
                    mipCoord.w += 48;
                    break;
                // The following are for BC/ASTC formats
                case 5:
                    mipCoord.h += 48;       // 1/2 x 1/2
                    break;
                case 6:
                    mipCoord.h += 48;       // 1/4 x 1/4
                    mipCoord.w += 16;
                    break;
                case 7:
                    mipCoord.h += 48;       // 1/8 x 1/8
                    mipCoord.w += 32;
                    break;
                case 8:
                    mipCoord.h += 48;       // 1/16 x 1/16
                    mipCoord.w += 48;
                    break;
                default:
                    ADDR_ASSERT_ALWAYS();
                    break;
            }

            mipWidth = ((mip - blk32MipId) == 0) ? 16 : 8;
            mipHeight = mipWidth;

            if (isThick)
            {
                mipDepth = mipWidth;
            }
        }
        else
        {
            if (mipWidth <= minInc)
            {
                // if we're below the minimal increment...
                if (isThick)
                {
                    // For 3d, just go in z direction
                    mipCoord.d += mipDepth;
                }
                else
                {
                    // For 2d, first go across, then down
                    if ((mipWidth * 2) == minInc)
                    {
                        // if we're 2 mips below, that's when we go back in x, and down in y
                        mipCoord.w -= minInc;
                        mipCoord.h += minInc;
                    }
                    else
                    {
                        // otherwise, just go across in x
                        mipCoord.w += minInc;
                    }
                }
            }
            else
            {
                // On even mip, go down, otherwise, go across
                if (mip & 1)
                {
                    mipCoord.w += mipWidth;
                }
                else
                {
                    mipCoord.h += mipHeight;
                }
            }
            // Divide the width by 2
            mipWidth >>= 1;
            // After the first mip in tail, the mip is always a square
            mipHeight = mipWidth;
            // ...or for 3d, a cube
            if (isThick)
            {
                mipDepth = mipWidth;
            }
        }
    }
}

/**
****************************************************************************************************
*   Lib::ComputeDccInfo
*
*   @brief
*       Interface function to compute DCC key info
*
*   @return
*       return code of HwlComputeDccInfo
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeDccInfo(
    const ADDR2_COMPUTE_DCCINFO_INPUT*    pIn,    ///< [in] input structure
    ADDR2_COMPUTE_DCCINFO_OUTPUT*         pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode;

    if ((GetFillSizeFieldsFlags() == TRUE) &&
        ((pIn->size != sizeof(ADDR2_COMPUTE_DCCINFO_INPUT)) ||
         (pOut->size != sizeof(ADDR2_COMPUTE_DCCINFO_OUTPUT))))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else
    {
        returnCode = HwlComputeDccInfo(pIn, pOut);
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputePipeBankXor
*
*   @brief
*       Interface function stub of Addr2ComputePipeBankXor.
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputePipeBankXor(
    const ADDR2_COMPUTE_PIPEBANKXOR_INPUT* pIn,
    ADDR2_COMPUTE_PIPEBANKXOR_OUTPUT* pOut)
{
    ADDR_E_RETURNCODE returnCode;

    if ((GetFillSizeFieldsFlags() == TRUE) &&
        ((pIn->size != sizeof(ADDR2_COMPUTE_PIPEBANKXOR_INPUT)) ||
         (pOut->size != sizeof(ADDR2_COMPUTE_PIPEBANKXOR_OUTPUT))))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }
    else
    {
        UINT_32 macroBlockBits = GetBlockSizeLog2(pIn->swizzleMode);
        UINT_32 pipeBits = GetPipeXorBits(macroBlockBits);
        UINT_32 bankBits = GetBankXorBits(macroBlockBits);
        UINT_32 pipeXor = 0;
        UINT_32 bankXor = 0;

        if (bankBits > 0)
        {
            UINT_32 bankMask = (1 << bankBits) - 1;
            UINT_32 bankIncrease = (1 << (bankBits - 1)) - 1;
            bankIncrease = (bankIncrease == 0) ? 1 : bankIncrease;
            bankXor = ((pIn->surfIndex & bankMask) * bankIncrease) & bankMask;
        }

        if (pipeBits > 0)
        {
            UINT_32 pipeMask = (1 << pipeBits) - 1;
            UINT_32 pipeIncrease = ((1 << (pipeBits - 1)) + 1) & pipeMask;
            pipeIncrease = (pipeIncrease == 0) ? 1 : pipeIncrease;
            pipeXor = ((pIn->surfIndex & pipeMask) * pipeIncrease) & pipeMask;
        }

        // Todo - pOut->pipeBankXor = pOut->pipeBankXor << (PipeInterleaveLog2 - 8)
        pOut->pipeBankXor = (bankXor << pipeBits) | pipeXor;

        returnCode = ADDR_OK;
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ExtractPipeBankXor
*
*   @brief
*       Internal function to extract bank and pipe xor bits from combined xor bits.
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ExtractPipeBankXor(
    UINT_32  pipeBankXor,
    UINT_32  bankBits,
    UINT_32  pipeBits,
    UINT_32* pBankX,
    UINT_32* pPipeX)
{
    ADDR_E_RETURNCODE returnCode;

    if (pipeBankXor < (1u << (pipeBits + bankBits)))
    {
        *pPipeX = pipeBankXor % (1 << pipeBits);
        *pBankX = pipeBankXor >> pipeBits;
        returnCode = ADDR_OK;
    }
    else
    {
        ADDR_ASSERT_ALWAYS();
        returnCode = ADDR_INVALIDPARAMS;
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeSurfaceInfoSanityCheck
*
*   @brief
*       Internal function to do basic sanity check before compute surface info
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceInfoSanityCheck(
    const ADDR2_COMPUTE_SURFACE_INFO_INPUT*  pIn   ///< [in] input structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    BOOL_32 invalid = FALSE;

    if ((pIn->bpp > 128) || (pIn->width == 0) || (pIn->numFrags > 8) || (pIn->numSamples > 16))
    {
        invalid = TRUE;
    }
    else if ((pIn->swizzleMode >= ADDR_SW_MAX_TYPE)    ||
             (pIn->resourceType >= ADDR_RSRC_MAX_TYPE))
    {
        invalid = TRUE;
    }

    BOOL_32 mipmap = (pIn->numMipLevels > 1);
    BOOL_32 msaa   = (pIn->numFrags > 1);

    ADDR2_SURFACE_FLAGS flags = pIn->flags;
    BOOL_32 zbuffer = (flags.depth || flags.stencil);
    BOOL_32 color   = flags.color;
    BOOL_32 display = flags.display || flags.rotated;

    AddrResourceType rsrcType = pIn->resourceType;
    BOOL_32 tex3d = IsTex3d(rsrcType);

    AddrSwizzleMode swizzle = pIn->swizzleMode;
    BOOL_32 linear  = IsLinear(swizzle);
    BOOL_32 blk256B = IsBlock256b(swizzle);
    BOOL_32 blkVar = IsBlockVariable(swizzle);
    BOOL_32 isNonPrtXor = IsNonPrtXor(swizzle);
    BOOL_32 prt = pIn->flags.prt;

    if (invalid == FALSE)
    {
        if ((pIn->numFrags > 1) &&
            (GetBlockSize(swizzle) < (m_pipeInterleaveBytes * pIn->numFrags)))
        {
            // MSAA surface must have blk_bytes/pipe_interleave >= num_samples
            invalid = TRUE;
        }
    }

    if (invalid == FALSE)
    {
        switch (rsrcType)
        {
            case ADDR_RSRC_TEX_1D:
                invalid = msaa || zbuffer || display || (linear == FALSE);
                break;
            case ADDR_RSRC_TEX_2D:
                invalid = msaa && mipmap;
                break;
            case ADDR_RSRC_TEX_3D:
                invalid = msaa || zbuffer || display;
                break;
            default:
                invalid = TRUE;
                break;
        }
    }

    if (invalid == FALSE)
    {
        if (display)
        {
            invalid = (IsValidDisplaySwizzleMode(pIn) == FALSE);
        }
    }

    if (invalid == FALSE)
    {
        if (linear)
        {
            invalid = prt || zbuffer || msaa || (pIn->bpp == 0) || ((pIn->bpp % 8) != 0);
        }
        else
        {
            if (blk256B || blkVar || isNonPrtXor)
            {
                invalid = prt;
                if (blk256B)
                {
                    invalid = invalid || zbuffer || tex3d || mipmap || msaa;
                }
            }

            if (invalid == FALSE)
            {
                if (IsZOrderSwizzle(swizzle))
                {
                    invalid = color && msaa;
                }
                else if (IsStandardSwizzle(rsrcType, swizzle))
                {
                    invalid = zbuffer;
                }
                else if (IsDisplaySwizzle(rsrcType, swizzle))
                {
                    invalid = zbuffer;
                }
                else if (IsRotateSwizzle(swizzle))
                {
                    invalid = zbuffer || (pIn->bpp > 64);
                }
                else
                {
                    ADDR_ASSERT(!"invalid swizzle mode");
                    invalid = TRUE;
                }
            }
        }
    }

    if (invalid)
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ApplyCustomizedPitchHeight
*
*   @brief
*       Helper function to override hw required row pitch/slice pitch by customrized one
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ApplyCustomizedPitchHeight(
    const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,    ///< [in] input structure
    UINT_32  elementBytes,                          ///< [in] element bytes per element
    UINT_32  widthAlignInElement,                   ///< [in] pitch alignment in element
    UINT_32* pPitch,                                ///< [in/out] pitch
    UINT_32* pHeight                                ///< [in/out] height
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (pIn->numMipLevels <= 1)
    {
        if (pIn->pitchInElement > 0)
        {
            if ((pIn->pitchInElement % widthAlignInElement) != 0)
            {
                returnCode = ADDR_INVALIDPARAMS;
            }
            else if (pIn->pitchInElement < (*pPitch))
            {
                returnCode = ADDR_INVALIDPARAMS;
            }
            else
            {
                *pPitch = pIn->pitchInElement;
            }
        }

        if (returnCode == ADDR_OK)
        {
            if (pIn->sliceAlign > 0)
            {
                UINT_32 customizedHeight = pIn->sliceAlign / elementBytes / (*pPitch);

                if (customizedHeight * elementBytes * (*pPitch) != pIn->sliceAlign)
                {
                    returnCode = ADDR_INVALIDPARAMS;
                }
                else if ((pIn->numSlices > 1) && ((*pHeight) != customizedHeight))
                {
                    returnCode = ADDR_INVALIDPARAMS;
                }
                else
                {
                    *pHeight = customizedHeight;
                }
            }
        }
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeSurfaceInfoLinear
*
*   @brief
*       Internal function to calculate alignment for linear swizzle surface
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceInfoLinear(
     const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,    ///< [in] input structure
     ADDR2_COMPUTE_SURFACE_INFO_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    UINT_32 pitch = 0;
    UINT_32 actualHeight = 0;
    UINT_32 elementBytes = pIn->bpp >> 3;

    if (IsTex1d(pIn->resourceType))
    {
        if (pIn->height > 1)
        {
            returnCode = ADDR_INVALIDPARAMS;
        }
        else
        {
            const UINT_32 widthAlignInElement = 256 / elementBytes;
            pitch = PowTwoAlign(pIn->width, widthAlignInElement);
            actualHeight = pIn->numMipLevels;
            returnCode = ApplyCustomizedPitchHeight(pIn, elementBytes, widthAlignInElement,
                                                    &pitch, &actualHeight);

            if (returnCode == ADDR_OK)
            {
                if (pOut->pMipInfo != NULL)
                {
                    for (UINT_32 i = 0; i < pIn->numMipLevels; i++)
                    {
                        pOut->pMipInfo[i].offset = pitch * elementBytes * i;
                        pOut->pMipInfo[i].pitch = pitch;
                        pOut->pMipInfo[i].height = 1;
                        pOut->pMipInfo[i].depth = 1;
                    }
                }
            }
        }
    }
    else
    {
        returnCode = ComputeSurfaceLinearPadding(pIn, &pitch, &actualHeight, pOut->pMipInfo);
    }

    if ((pitch == 0) || (actualHeight == 0))
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    if (returnCode == ADDR_OK)
    {
        pOut->pitch = pitch;
        pOut->height = pIn->height;
        pOut->numSlices = pIn->numSlices;
        pOut->mipChainPitch = pitch;
        pOut->mipChainHeight = actualHeight;
        pOut->mipChainSlice = pOut->numSlices;
        pOut->epitchIsHeight = (pIn->numMipLevels > 1) ? TRUE : FALSE;
        pOut->sliceSize = pOut->pitch * actualHeight * elementBytes;
        pOut->surfSize = pOut->sliceSize * pOut->numSlices;
        pOut->baseAlign = (pIn->swizzleMode == ADDR_SW_LINEAR_GENERAL) ? (pIn->bpp / 8) : 256;
        pOut->blockWidth = (pIn->swizzleMode == ADDR_SW_LINEAR_GENERAL) ? 1 : (256 * 8 / pIn->bpp);
        pOut->blockHeight = 1;
        pOut->blockSlices = 1;
    }

    // Post calculation validate
    ADDR_ASSERT((pOut->sliceSize > 0));

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeSurfaceInfoTiled
*
*   @brief
*       Internal function to calculate alignment for tiled swizzle surface
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceInfoTiled(
     const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,    ///< [in] input structure
     ADDR2_COMPUTE_SURFACE_INFO_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    ADDR_E_RETURNCODE returnCode = ComputeBlockDimensionForSurf(&pOut->blockWidth,
                                                                &pOut->blockHeight,
                                                                &pOut->blockSlices,
                                                                pIn->bpp,
                                                                pIn->numFrags,
                                                                pIn->resourceType,
                                                                pIn->swizzleMode);

    if (returnCode == ADDR_OK)
    {
        const UINT_32 widthAlignInElement = pOut->blockWidth;

        pOut->pitch = PowTwoAlign(pIn->width, widthAlignInElement);

        if ((pIn->numMipLevels <= 1) && (pIn->pitchInElement > 0))
        {
            if ((pIn->pitchInElement % widthAlignInElement) != 0)
            {
                returnCode = ADDR_INVALIDPARAMS;
            }
            else if (pIn->pitchInElement < pOut->pitch)
            {
                returnCode = ADDR_INVALIDPARAMS;
            }
            else
            {
                pOut->pitch = pIn->pitchInElement;
            }
        }

        if (returnCode == ADDR_OK)
        {
            pOut->height = PowTwoAlign(pIn->height, pOut->blockHeight);
            pOut->numSlices = PowTwoAlign(pIn->numSlices, pOut->blockSlices);

            pOut->epitchIsHeight = FALSE;
            pOut->firstMipInTail = FALSE;

            pOut->mipChainPitch  = pOut->pitch;
            pOut->mipChainHeight = pOut->height;
            pOut->mipChainSlice  = pOut->numSlices;

            if (pIn->numMipLevels > 1)
            {
                UINT_32 numMipLevel;
                ADDR2_MIP_INFO *pMipInfo;
                ADDR2_MIP_INFO mipInfo[4];

                if (pOut->pMipInfo != NULL)
                {
                    pMipInfo = pOut->pMipInfo;
                    numMipLevel = pIn->numMipLevels;
                }
                else
                {
                    pMipInfo = mipInfo;
                    numMipLevel = Min(pIn->numMipLevels, 4u);
                }

                UINT_32 endingMip = GetMipChainInfo(pIn->resourceType,
                                                    pIn->swizzleMode,
                                                    pIn->bpp,
                                                    pIn->width,
                                                    pIn->height,
                                                    pIn->numSlices,
                                                    pOut->blockWidth,
                                                    pOut->blockHeight,
                                                    pOut->blockSlices,
                                                    numMipLevel,
                                                    pMipInfo);

                if (endingMip == 0)
                {
                    pOut->epitchIsHeight = TRUE;
                    pOut->pitch          = pMipInfo[0].pitch;
                    pOut->height         = pMipInfo[0].height;
                    pOut->numSlices      = pMipInfo[0].depth;
                    pOut->firstMipInTail = TRUE;
                }
                else
                {
                    UINT_32 mip0WidthInBlk = pOut->pitch / pOut->blockWidth;
                    UINT_32 mip0HeightInBlk = pOut->height / pOut->blockHeight;

                    AddrMajorMode majorMode = GetMajorMode(pIn->resourceType,
                                                           pIn->swizzleMode,
                                                           mip0WidthInBlk,
                                                           mip0HeightInBlk,
                                                           pOut->numSlices / pOut->blockSlices);
                    if (majorMode == ADDR_MAJOR_Y)
                    {
                        UINT_32 mip1WidthInBlk = RoundHalf(mip0WidthInBlk);

                        if ((mip1WidthInBlk == 1) && (endingMip > 2))
                        {
                            mip1WidthInBlk++;
                        }

                        pOut->mipChainPitch += (mip1WidthInBlk * pOut->blockWidth);

                        pOut->epitchIsHeight = FALSE;
                    }
                    else
                    {
                        UINT_32 mip1HeightInBlk = RoundHalf(mip0HeightInBlk);

                        if ((mip1HeightInBlk == 1) && (endingMip > 2))
                        {
                            mip1HeightInBlk++;
                        }

                        pOut->mipChainHeight += (mip1HeightInBlk * pOut->blockHeight);

                        pOut->epitchIsHeight = TRUE;
                    }
                }
            }
            else if (pOut->pMipInfo != NULL)
            {
                pOut->pMipInfo[0].pitch = pOut->pitch;
                pOut->pMipInfo[0].height = pOut->height;
                pOut->pMipInfo[0].depth = IsTex3d(pIn->resourceType)? pOut->numSlices : 1;
                pOut->pMipInfo[0].offset = 0;
            }

            pOut->sliceSize = pOut->mipChainPitch *pOut->mipChainHeight *
                              (pIn->bpp >> 3) * pIn->numFrags;
            pOut->surfSize = pOut->sliceSize * pOut->mipChainSlice;
            pOut->baseAlign = ComputeSurfaceBaseAlign(pIn->swizzleMode);
        }
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeSurfaceAddrFromCoordLinear
*
*   @brief
*       Internal function to calculate address from coord for linear swizzle surface
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceAddrFromCoordLinear(
     const ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,    ///< [in] input structure
     ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;
    BOOL_32 valid = (pIn->numSamples <= 1) && (pIn->numFrags <= 1) && (pIn->pipeBankXor == 0);

    if (valid)
    {
        if (IsTex1d(pIn->resourceType))
        {
            valid = (pIn->y == 0);
        }
    }

    if (valid)
    {
        ADDR2_COMPUTE_SURFACE_INFO_INPUT localIn = {0};
        ADDR2_COMPUTE_SURFACE_INFO_OUTPUT localOut = {0};
        localIn.bpp = pIn->bpp;
        localIn.width = Max(pIn->unalignedWidth, 1u);
        localIn.height = Max(pIn->unalignedHeight, 1u);
        localIn.numSlices = Max(pIn->numSlices, 1u);
        localIn.numMipLevels = Max(pIn->numMipLevels, 1u);
        localIn.resourceType = pIn->resourceType;
        if (localIn.numMipLevels <= 1)
        {
            localIn.pitchInElement = pIn->pitchInElement;
        }
        returnCode = ComputeSurfaceInfoLinear(&localIn, &localOut);

        if (returnCode == ADDR_OK)
        {
            UINT_32 elementBytes = pIn->bpp >> 3;
            UINT_64 sliceOffsetInSurf = static_cast<UINT_64>(pIn->slice) * localOut.sliceSize;
            UINT_64 mipOffsetInSlice = 0;
            UINT_64 offsetInMip = 0;

            if (IsTex1d(pIn->resourceType))
            {
                offsetInMip = static_cast<UINT_64>(pIn->x) * elementBytes;
                mipOffsetInSlice = static_cast<UINT_64>(pIn->mipId) * localOut.pitch * elementBytes;
            }
            else
            {
                UINT_64 mipStartHeight = SumGeo(localIn.height, pIn->mipId);
                mipOffsetInSlice = static_cast<UINT_64>(mipStartHeight) * localOut.pitch * elementBytes;
                offsetInMip = (pIn->y * localOut.pitch + pIn->x) * elementBytes;
            }

            pOut->addr = sliceOffsetInSurf + mipOffsetInSlice + offsetInMip;
            pOut->bitPosition = 0;
        }
        else
        {
            valid = FALSE;
        }
    }

    if (valid == FALSE)
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeSurfaceAddrFromCoordTiled
*
*   @brief
*       Internal function to calculate address from coord for tiled swizzle surface
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceAddrFromCoordTiled(
     const ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,    ///< [in] input structure
     ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    ADDR2_COMPUTE_SURFACE_INFO_INPUT localIn = {0};
    localIn.swizzleMode  = pIn->swizzleMode;
    localIn.flags        = pIn->flags;
    localIn.resourceType = pIn->resourceType;
    localIn.bpp          = pIn->bpp;
    localIn.width        = Max(pIn->unalignedWidth, 1u);
    localIn.height       = Max(pIn->unalignedHeight, 1u);
    localIn.numSlices    = Max(pIn->numSlices, 1u);
    localIn.numMipLevels = Max(pIn->numMipLevels, 1u);
    if (localIn.numMipLevels <= 1)
    {
        localIn.pitchInElement = pIn->pitchInElement;
    }

    ADDR2_COMPUTE_SURFACE_INFO_OUTPUT localOut = {0};
    ADDR_E_RETURNCODE returnCode = ComputeSurfaceInfoTiled(&localIn, &localOut);

    BOOL_32 valid = (returnCode == ADDR_OK) &&
                    (IsThin(pIn->resourceType, pIn->swizzleMode) ||
                     IsThick(pIn->resourceType, pIn->swizzleMode)) &&
                    ((pIn->pipeBankXor == 0) || (IsXor(pIn->swizzleMode)));

    if (valid)
    {
        Dim3d mipStartPos = {0};
        UINT_32 mipTailOffset = 0;

        if (pIn->numMipLevels > 1)
        {
            // Mip-map chain cannot be MSAA surface
            ADDR_ASSERT((pIn->numSamples <= 1) && (pIn->numFrags<= 1));

            mipStartPos = GetMipStartPos(pIn->resourceType,
                                         pIn->swizzleMode,
                                         localOut.pitch,
                                         localOut.height,
                                         localOut.numSlices,
                                         localOut.blockWidth,
                                         localOut.blockHeight,
                                         localOut.blockSlices,
                                         pIn->mipId,
                                         &mipTailOffset);
        }

        UINT_32 interleaveOffset = 0;
        UINT_32 pipeBits = 0;
        UINT_32 pipeXor = 0;
        UINT_32 bankBits = 0;
        UINT_32 bankXor = 0;

        if (IsThin(pIn->resourceType, pIn->swizzleMode))
        {
            UINT_32 blockOffset = 0;
            UINT_32 log2blkSize = GetBlockSizeLog2(pIn->swizzleMode);
            UINT_32 log2ElementBytes = Log2(pIn->bpp >> 3);

            if (IsZOrderSwizzle(pIn->swizzleMode))
            {
                // Morton generation
                if ((log2ElementBytes == 0) || (log2ElementBytes == 2))
                {
                    UINT_32 totalLowBits = 6 - log2ElementBytes;
                    UINT_32 mortBits = totalLowBits / 2;
                    UINT_32 lowBitsValue = MortonGen2d(pIn->y, pIn->x, mortBits);
                    // Are 9 bits enough?
                    UINT_32 highBitsValue =
                        MortonGen2d(pIn->x >> mortBits, pIn->y >> mortBits, 9) << totalLowBits;
                    blockOffset = lowBitsValue | highBitsValue;
                    ADDR_ASSERT(blockOffset == lowBitsValue + highBitsValue);
                }
                else
                {
                    blockOffset = MortonGen2d(pIn->y, pIn->x, 13);
                }

                // Fill LSBs with sample bits
                if (pIn->numSamples > 1)
                {
                    blockOffset *= pIn->numSamples;
                    blockOffset |= pIn->sample;
                }

                // Shift according to BytesPP
                blockOffset <<= log2ElementBytes;
            }
            else
            {
                // Micro block offset
                UINT_32 microBlockOffset = ComputeSurface2DMicroBlockOffset(pIn);
                blockOffset = microBlockOffset;

                // Micro block dimension
                ADDR_ASSERT(log2ElementBytes < sizeof(Block256b) / sizeof(Block256b[0]));
                Dim2d microBlockDim = Block256b[log2ElementBytes];
                // Morton generation, does 12 bit enough?
                blockOffset |=
                    MortonGen2d((pIn->x / microBlockDim.w), (pIn->y / microBlockDim.h), 12) << 8;

                // Sample bits start location
                UINT_32 sampleStart = log2blkSize - Log2(pIn->numSamples);
                // Join sample bits information to the highest Macro block bits
                if (IsNonPrtXor(pIn->swizzleMode))
                {
                    // Non-prt-Xor : xor highest Macro block bits with sample bits
                    blockOffset = blockOffset ^ (pIn->sample << sampleStart);
                }
                else
                {
                    // Non-Xor or prt-Xor: replace highest Macro block bits with sample bits
                    // after this op, the blockOffset only contains log2 Macro block size bits
                    blockOffset %= (1 << sampleStart);
                    blockOffset |= (pIn->sample << sampleStart);
                    ADDR_ASSERT((blockOffset >> log2blkSize) == 0);
                }
            }

            if (IsXor(pIn->swizzleMode))
            {
                // Mask off bits above Macro block bits to keep page synonyms working for prt
                if (IsPrt(pIn->swizzleMode))
                {
                    blockOffset &= ((1 << log2blkSize) - 1);
                }

                // Preserve offset inside pipe interleave
                interleaveOffset = blockOffset & ((1 << m_pipeInterleaveLog2) - 1);
                blockOffset >>= m_pipeInterleaveLog2;

                // Pipe/Se xor bits
                pipeBits = GetPipeXorBits(log2blkSize);
                // Pipe xor
                pipeXor = FoldXor2d(blockOffset, pipeBits);
                blockOffset >>= pipeBits;

                // Bank xor bits
                bankBits = GetBankXorBits(log2blkSize);
                // Bank Xor
                bankXor = FoldXor2d(blockOffset, bankBits);
                blockOffset >>= bankBits;

                // Put all the part back together
                blockOffset <<= bankBits;
                blockOffset |= bankXor;
                blockOffset <<= pipeBits;
                blockOffset |= pipeXor;
                blockOffset <<= m_pipeInterleaveLog2;
                blockOffset |= interleaveOffset;
            }

            ADDR_ASSERT((blockOffset | mipTailOffset) == (blockOffset + mipTailOffset));
            blockOffset |= mipTailOffset;

            if (IsNonPrtXor(pIn->swizzleMode) && (pIn->numSamples <= 1))
            {
                // Apply slice xor if not MSAA/PRT
                blockOffset ^= (ReverseBitVector(pIn->slice, pipeBits) << m_pipeInterleaveLog2);
                blockOffset ^= (ReverseBitVector(pIn->slice >> pipeBits, bankBits) <<
                                (m_pipeInterleaveLog2 + pipeBits));
            }

            returnCode = ApplyCustomerPipeBankXor(pIn->swizzleMode, pIn->pipeBankXor,
                                                  bankBits, pipeBits, &blockOffset);

            blockOffset %= (1 << log2blkSize);

            UINT_32 pitchInMacroBlock = localOut.mipChainPitch / localOut.blockWidth;
            UINT_32 paddedHeightInMacroBlock = localOut.mipChainHeight / localOut.blockHeight;
            UINT_32 sliceSizeInMacroBlock = pitchInMacroBlock * paddedHeightInMacroBlock;
            UINT_32 macroBlockIndex =
                (pIn->slice + mipStartPos.d) * sliceSizeInMacroBlock +
                ((pIn->y / localOut.blockHeight) + mipStartPos.h) * pitchInMacroBlock +
                ((pIn->x / localOut.blockWidth) + mipStartPos.w);

            UINT_64 macroBlockOffset = (static_cast<UINT_64>(macroBlockIndex) <<
                                       GetBlockSizeLog2(pIn->swizzleMode));

            pOut->addr = blockOffset | macroBlockOffset;
        }
        else
        {
            UINT_32 log2blkSize = GetBlockSizeLog2(pIn->swizzleMode);
            UINT_32 log2ElementBytes = Log2(pIn->bpp >> 3);

            Dim3d microBlockDim = Block1kb[log2ElementBytes];

            UINT_32 blockOffset = MortonGen3d((pIn->x / microBlockDim.w),
                                              (pIn->y / microBlockDim.h),
                                              (pIn->slice / microBlockDim.d),
                                              8);

            blockOffset <<= 10;
            blockOffset |= ComputeSurface3DMicroBlockOffset(pIn);

            if (IsXor(pIn->swizzleMode))
            {
                // Mask off bits above Macro block bits to keep page synonyms working for prt
                if (IsPrt(pIn->swizzleMode))
                {
                    blockOffset &= ((1 << log2blkSize) - 1);
                }

                // Preserve offset inside pipe interleave
                interleaveOffset = blockOffset & ((1 << m_pipeInterleaveLog2) - 1);
                blockOffset >>= m_pipeInterleaveLog2;

                // Pipe/Se xor bits
                pipeBits = GetPipeXorBits(log2blkSize);
                // Pipe xor
                pipeXor = FoldXor3d(blockOffset, pipeBits);
                blockOffset >>= pipeBits;

                // Bank xor bits
                bankBits = GetBankXorBits(log2blkSize);
                // Bank Xor
                bankXor = FoldXor3d(blockOffset, bankBits);
                blockOffset >>= bankBits;

                // Put all the part back together
                blockOffset <<= bankBits;
                blockOffset |= bankXor;
                blockOffset <<= pipeBits;
                blockOffset |= pipeXor;
                blockOffset <<= m_pipeInterleaveLog2;
                blockOffset |= interleaveOffset;
            }

            ADDR_ASSERT((blockOffset | mipTailOffset) == (blockOffset + mipTailOffset));
            blockOffset |= mipTailOffset;

            returnCode = ApplyCustomerPipeBankXor(pIn->swizzleMode, pIn->pipeBankXor,
                                                  bankBits, pipeBits, &blockOffset);

            blockOffset %= (1 << log2blkSize);

            UINT_32 xb = (pIn->x + mipStartPos.w) / localOut.blockWidth;
            UINT_32 yb = (pIn->y + mipStartPos.h) / localOut.blockHeight;
            UINT_32 zb = (pIn->slice + mipStartPos.d) / localOut.blockSlices;

            UINT_32 pitchInBlock = localOut.mipChainPitch / localOut.blockWidth;
            UINT_32 sliceSizeInBlock =
                (localOut.mipChainHeight / localOut.blockHeight) * pitchInBlock;
            UINT_32 blockIndex = zb * sliceSizeInBlock + yb * pitchInBlock + xb;

            pOut->addr = blockOffset | (blockIndex << log2blkSize);
        }
    }
    else
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeSurfaceCoordFromAddrLinear
*
*   @brief
*       Internal function to calculate coord from address for linear swizzle surface
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceCoordFromAddrLinear(
     const ADDR2_COMPUTE_SURFACE_COORDFROMADDR_INPUT* pIn,    ///< [in] input structure
     ADDR2_COMPUTE_SURFACE_COORDFROMADDR_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    BOOL_32 valid = (pIn->numSamples <= 1) && (pIn->numFrags <= 1);

    if (valid)
    {
        if (IsTex1d(pIn->resourceType))
        {
            valid = (pIn->unalignedHeight == 1);
        }
    }

    if (valid)
    {
        ADDR2_COMPUTE_SURFACE_INFO_INPUT localIn = {0};
        ADDR2_COMPUTE_SURFACE_INFO_OUTPUT localOut = {0};
        localIn.bpp = pIn->bpp;
        localIn.width = Max(pIn->unalignedWidth, 1u);
        localIn.height = Max(pIn->unalignedHeight, 1u);
        localIn.numSlices = Max(pIn->numSlices, 1u);
        localIn.numMipLevels = Max(pIn->numMipLevels, 1u);
        localIn.resourceType = pIn->resourceType;
        if (localIn.numMipLevels <= 1)
        {
            localIn.pitchInElement = pIn->pitchInElement;
        }
        returnCode = ComputeSurfaceInfoLinear(&localIn, &localOut);

        if (returnCode == ADDR_OK)
        {
            pOut->slice = static_cast<UINT_32>(pIn->addr / localOut.sliceSize);
            pOut->sample = 0;

            UINT_32 offsetInSlice = static_cast<UINT_32>(pIn->addr % localOut.sliceSize);
            UINT_32 elementBytes = pIn->bpp >> 3;
            UINT_32 mipOffsetInSlice = 0;
            UINT_32 mipSize = 0;
            UINT_32 mipId = 0;
            for (; mipId < pIn->numMipLevels ; mipId++)
            {
                if (IsTex1d(pIn->resourceType))
                {
                    mipSize = localOut.pitch * elementBytes;
                }
                else
                {
                    UINT_32 currentMipHeight = (PowTwoAlign(localIn.height, (1 << mipId))) >> mipId;
                    mipSize = currentMipHeight * localOut.pitch * elementBytes;
                }

                if (mipSize == 0)
                {
                    valid = FALSE;
                    break;
                }
                else if ((mipSize + mipOffsetInSlice) > offsetInSlice)
                {
                    break;
                }
                else
                {
                    mipOffsetInSlice += mipSize;
                    if ((mipId == (pIn->numMipLevels - 1)) ||
                        (mipOffsetInSlice >= localOut.sliceSize))
                    {
                        valid = FALSE;
                    }
                }
            }

            if (valid)
            {
                pOut->mipId = mipId;

                UINT_32 elemOffsetInMip = (offsetInSlice - mipOffsetInSlice) / elementBytes;
                if (IsTex1d(pIn->resourceType))
                {
                    if (elemOffsetInMip < localOut.pitch)
                    {
                        pOut->x = elemOffsetInMip;
                        pOut->y = 0;
                    }
                    else
                    {
                        valid = FALSE;
                    }
                }
                else
                {
                    pOut->y = elemOffsetInMip / localOut.pitch;
                    pOut->x = elemOffsetInMip % localOut.pitch;
                }

                if ((pOut->slice >= pIn->numSlices)    ||
                    (pOut->mipId >= pIn->numMipLevels) ||
                    (pOut->x >= Max((pIn->unalignedWidth >> pOut->mipId), 1u))  ||
                    (pOut->y >= Max((pIn->unalignedHeight >> pOut->mipId), 1u)) ||
                    (IsTex3d(pIn->resourceType) &&
                     (FALSE == Valid3DMipSliceIdConstraint(pIn->numSlices,
                                                           pOut->mipId,
                                                           pOut->slice))))
                {
                    valid = FALSE;
                }
            }
        }
        else
        {
            valid = FALSE;
        }
    }

    if (valid == FALSE)
    {
        returnCode = ADDR_INVALIDPARAMS;
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeSurfaceCoordFromAddrTiled
*
*   @brief
*       Internal function to calculate coord from address for tiled swizzle surface
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceCoordFromAddrTiled(
     const ADDR2_COMPUTE_SURFACE_COORDFROMADDR_INPUT* pIn,    ///< [in] input structure
     ADDR2_COMPUTE_SURFACE_COORDFROMADDR_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_NOTIMPLEMENTED;

    ADDR_NOT_IMPLEMENTED();

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeSurfaceInfoLinear
*
*   @brief
*       Internal function to calculate padding for linear swizzle 2D/3D surface
*
*   @return
*       N/A
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeSurfaceLinearPadding(
    const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,    ///< [in] input srtucture
    UINT_32* pMipmap0PaddedWidth,                   ///< [out] padded width in element
    UINT_32* pSlice0PaddedHeight,                   ///< [out] padded height for HW
    ADDR2_MIP_INFO* pMipInfo                        ///< [out] per mip information
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    UINT_32 elementBytes = pIn->bpp >> 3;
    UINT_32 widthAlignInElement = 0;

    if (pIn->swizzleMode == ADDR_SW_LINEAR_GENERAL)
    {
        ADDR_ASSERT(pIn->numMipLevels <= 1);
        ADDR_ASSERT(pIn->numSlices <= 1);
        widthAlignInElement = 1;
    }
    else
    {
        widthAlignInElement = (256 / elementBytes);
    }

    UINT_32 mipChainWidth = PowTwoAlign(pIn->width, widthAlignInElement);
    UINT_32 slice0PaddedHeight = pIn->height;

    returnCode = ApplyCustomizedPitchHeight(pIn, elementBytes, widthAlignInElement,
                                            &mipChainWidth, &slice0PaddedHeight);

    if (returnCode == ADDR_OK)
    {
        UINT_32 mipChainHeight = 0;
        UINT_32 mipHeight = pIn->height;

        for (UINT_32 i = 0; i < pIn->numMipLevels; i++)
        {
            if (pMipInfo != NULL)
            {
                pMipInfo[i].offset = mipChainWidth * mipChainHeight * elementBytes;
                pMipInfo[i].pitch = mipChainWidth;
                pMipInfo[i].height = mipHeight;
                pMipInfo[i].depth = 1;
            }

            mipChainHeight += mipHeight;
            mipHeight = RoundHalf(mipHeight);
            mipHeight = Max(mipHeight, 1u);
        }

        *pMipmap0PaddedWidth = mipChainWidth;
        *pSlice0PaddedHeight = (pIn->numMipLevels > 1) ? mipChainHeight : slice0PaddedHeight;
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeBlockDimensionForSurf
*
*   @brief
*       Internal function to get block width/height/depth in element from surface input params.
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeBlockDimensionForSurf(
    Dim3d*            pDim,
    UINT_32           bpp,
    UINT_32           numSamples,
    AddrResourceType  resourceType,
    AddrSwizzleMode   swizzleMode) const
{
    return ComputeBlockDimensionForSurf(&pDim->w, &pDim->h, &pDim->d, bpp,
                                        numSamples, resourceType, swizzleMode);
}

/**
****************************************************************************************************
*   Lib::ComputeBlockDimensionForSurf
*
*   @brief
*       Internal function to get block width/height/depth in element from surface input params.
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeBlockDimensionForSurf(
    UINT_32*         pWidth,
    UINT_32*         pHeight,
    UINT_32*         pDepth,
    UINT_32          bpp,
    UINT_32          numSamples,
    AddrResourceType resourceType,
    AddrSwizzleMode  swizzleMode) const
{
    ADDR_E_RETURNCODE returnCode = ComputeBlockDimension(pWidth,
                                                         pHeight,
                                                         pDepth,
                                                         bpp,
                                                         resourceType,
                                                         swizzleMode);

    if ((returnCode == ADDR_OK) && (numSamples > 1) && IsThin(resourceType, swizzleMode))
    {
        UINT_32 log2blkSize = GetBlockSizeLog2(swizzleMode);
        UINT_32 sample = numSamples;
        UINT_32 log2sample = Log2(sample);

        *pWidth  >>= (log2sample / 2);
        *pHeight >>= (log2sample / 2);

        if ((log2blkSize % 2) == 0)
        {
            *pWidth >>= (sample % 2);
        }
        else
        {
            *pHeight >>= (sample % 2);
        }
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeBlockDimension
*
*   @brief
*       Internal function to get block width/height/depth in element without considering MSAA case
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeBlockDimension(
    UINT_32*          pWidth,
    UINT_32*          pHeight,
    UINT_32*          pDepth,
    UINT_32           bpp,
    AddrResourceType  resourceType,
    AddrSwizzleMode   swizzleMode) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    UINT_32 eleBytes = bpp >> 3;
    UINT_32 microBlockSizeTableIndex = Log2(eleBytes);
    UINT_32 log2blkSize = GetBlockSizeLog2(swizzleMode);

    if (IsThin(resourceType, swizzleMode))
    {
        if (pDepth != NULL)
        {
            *pDepth = 1;
        }

        UINT_32 log2blkSizeIn256B = log2blkSize - 8;
        UINT_32 widthAmp  = log2blkSizeIn256B / 2;
        UINT_32 heightAmp = log2blkSizeIn256B - widthAmp;

        ADDR_ASSERT(microBlockSizeTableIndex < sizeof(Block256b) / sizeof(Block256b[0]));

        *pWidth  = (Block256b[microBlockSizeTableIndex].w << widthAmp);
        *pHeight = (Block256b[microBlockSizeTableIndex].h << heightAmp);
    }
    else if (IsThick(resourceType, swizzleMode))
    {
        UINT_32 log2blkSizeIn1KB = log2blkSize - 10;
        UINT_32 averageAmp = log2blkSizeIn1KB / 3;
        UINT_32 restAmp = log2blkSizeIn1KB % 3;

        ADDR_ASSERT(microBlockSizeTableIndex < sizeof(Block1kb) / sizeof(Block1kb[0]));

        *pWidth  = Block1kb[microBlockSizeTableIndex].w << averageAmp;
        *pHeight = Block1kb[microBlockSizeTableIndex].h << (averageAmp + (restAmp / 2));
        *pDepth  = Block1kb[microBlockSizeTableIndex].d << (averageAmp + ((restAmp != 0) ? 1 : 0));
    }
    else
    {
        ADDR_ASSERT_ALWAYS();
        returnCode = ADDR_INVALIDPARAMS;
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::GetMipChainInfo
*
*   @brief
*       Internal function to get out information about mip chain
*
*   @return
*       Smaller value between Id of first mip fitted in mip tail and max Id of mip being created
****************************************************************************************************
*/
UINT_32 Lib::GetMipChainInfo(
    AddrResourceType  resourceType,
    AddrSwizzleMode   swizzleMode,
    UINT_32           bpp,
    UINT_32           mip0Width,
    UINT_32           mip0Height,
    UINT_32           mip0Depth,
    UINT_32           blockWidth,
    UINT_32           blockHeight,
    UINT_32           blockDepth,
    UINT_32           numMipLevel,
    ADDR2_MIP_INFO*   pMipInfo) const
{
    const Dim3d tailMaxDim =
        GetMipTailDim(resourceType, swizzleMode, blockWidth, blockHeight, blockDepth);

    UINT_32 mipPitch  = mip0Width;
    UINT_32 mipHeight = mip0Height;
    UINT_32 mipDepth  = IsTex3d(resourceType) ? mip0Depth : 1;
    UINT_32 offset    = 0;
    UINT_32 endingMip = numMipLevel - 1;
    BOOL_32 inTail    = FALSE;
    BOOL_32 finalDim  = FALSE;

    BOOL_32 is3dThick = IsThick(resourceType, swizzleMode);
    BOOL_32 is3dThin  = IsTex3d(resourceType) && SwizzleModeTable[swizzleMode].isDisp;

    for (UINT_32 mipId = 0; mipId < numMipLevel; mipId++)
    {
        if (inTail)
        {
            if (finalDim == FALSE)
            {
                UINT_32 mipSize;

                if (is3dThick)
                {
                    mipSize = mipPitch * mipHeight * mipDepth * (bpp >> 3);
                }
                else
                {
                    mipSize = mipPitch * mipHeight * (bpp >> 3);
                }

                if (mipSize <= 256)
                {
                    UINT_32 index = Log2(bpp >> 3);

                    if (is3dThick)
                    {
                        mipPitch  = CompressBlock3dZ[index].w;
                        mipHeight = CompressBlock3dZ[index].h;
                        mipDepth  = CompressBlock3dZ[index].d;
                    }
                    else
                    {
                        mipPitch  = CompressBlock2d[index].w;
                        mipHeight = CompressBlock2d[index].h;
                    }

                    finalDim = TRUE;
                }
            }
        }
        else
        {
            inTail = IsInMipTail(resourceType, swizzleMode, tailMaxDim,
                                 mipPitch, mipHeight, mipDepth);

            if (inTail)
            {
                endingMip = mipId;

                mipPitch  = tailMaxDim.w;
                mipHeight = tailMaxDim.h;

                if (is3dThick)
                {
                    mipDepth = tailMaxDim.d;
                }
            }
            else
            {
                mipPitch  = PowTwoAlign(mipPitch,  blockWidth);
                mipHeight = PowTwoAlign(mipHeight, blockHeight);

                if (is3dThick)
                {
                    mipDepth = PowTwoAlign(mipDepth,  blockDepth);
                }
            }
        }

        pMipInfo[mipId].pitch  = mipPitch;
        pMipInfo[mipId].height = mipHeight;
        pMipInfo[mipId].depth  = mipDepth;
        pMipInfo[mipId].offset = offset;
        offset += (mipPitch * mipHeight * mipDepth * (bpp >> 3));

        if (finalDim)
        {
            if (is3dThin)
            {
                mipDepth = Max(mipDepth >> 1, 1u);
            }
        }
        else
        {
            mipPitch  = Max(mipPitch >> 1, 1u);
            mipHeight = Max(mipHeight >> 1, 1u);

            if (is3dThick || is3dThin)
            {
                mipDepth = Max(mipDepth >> 1, 1u);
            }
        }
    }

    return endingMip;
}

/**
****************************************************************************************************
*   Lib::GetMipStartPos
*
*   @brief
*       Internal function to get out information about mip logical start position
*
*   @return
*       logical start position in macro block width/heith/depth of one mip level within one slice
****************************************************************************************************
*/
Dim3d Lib::GetMipStartPos(
    AddrResourceType  resourceType,
    AddrSwizzleMode   swizzleMode,
    UINT_32           width,
    UINT_32           height,
    UINT_32           depth,
    UINT_32           blockWidth,
    UINT_32           blockHeight,
    UINT_32           blockDepth,
    UINT_32           mipId,
    UINT_32*          pMipTailOffset) const
{
    Dim3d mipStartPos = {0};

    const Dim3d tailMaxDim =
        GetMipTailDim(resourceType, swizzleMode, blockWidth, blockHeight, blockDepth);

    // Report mip in tail if Mip0 is already in mip tail
    BOOL_32 inMipTail = IsInMipTail(resourceType, swizzleMode, tailMaxDim, width, height, depth);

    UINT_32 log2blkSize = GetBlockSizeLog2(swizzleMode);

    if (inMipTail == FALSE)
    {
        // Mip 0 dimension, unit in block
        UINT_32 mipWidthInBlk  = width / blockWidth;
        UINT_32 mipHeightInBlk = height / blockHeight;
        UINT_32 mipDepthInBlk  = depth / blockDepth;
        AddrMajorMode majorMode = GetMajorMode(resourceType,
                                               swizzleMode,
                                               mipWidthInBlk,
                                               mipHeightInBlk,
                                               mipDepthInBlk);

        UINT_32 endingMip = mipId + 1;

        for (UINT_32 i = 1; i <= mipId; i++)
        {
            if ((i == 1) || (i == 3))
            {
                if (majorMode == ADDR_MAJOR_Y)
                {
                    mipStartPos.w += mipWidthInBlk;
                }
                else
                {
                    mipStartPos.h += mipHeightInBlk;
                }
            }
            else
            {
                if (majorMode == ADDR_MAJOR_X)
                {
                   mipStartPos.w += mipWidthInBlk;
                }
                else if (majorMode == ADDR_MAJOR_Y)
                {
                   mipStartPos.h += mipHeightInBlk;
                }
                else
                {
                   mipStartPos.d += mipDepthInBlk;
                }
            }

            BOOL_32 inTail = FALSE;

            if (IsThick(resourceType, swizzleMode))
            {
                UINT_32 dim = log2blkSize % 3;

                if (dim == 0)
                {
                    inTail =
                        (mipWidthInBlk <= 2) && (mipHeightInBlk == 1) && (mipDepthInBlk <= 2);
                }
                else if (dim == 1)
                {
                    inTail =
                        (mipWidthInBlk == 1) && (mipHeightInBlk <= 2) && (mipDepthInBlk <= 2);
                }
                else
                {
                    inTail =
                        (mipWidthInBlk <= 2) && (mipHeightInBlk <= 2) && (mipDepthInBlk == 1);
                }
            }
            else
            {
                if (log2blkSize & 1)
                {
                    inTail = (mipWidthInBlk <= 2) && (mipHeightInBlk == 1);
                }
                else
                {
                    inTail = (mipWidthInBlk == 1) && (mipHeightInBlk <= 2);
                }
            }

            if (inTail)
            {
                endingMip = i;
                break;
            }

            mipWidthInBlk = RoundHalf(mipWidthInBlk);
            mipHeightInBlk = RoundHalf(mipHeightInBlk);
            mipDepthInBlk = RoundHalf(mipDepthInBlk);
        }

        if (mipId >= endingMip)
        {
            inMipTail = TRUE;
            UINT_32 index = mipId - endingMip + MaxMacroBits - log2blkSize;
            ADDR_ASSERT(index < sizeof(MipTailOffset) / sizeof(UINT_32));
            *pMipTailOffset = MipTailOffset[index] << 8;
        }
    }
    else
    {
        UINT_32 index = mipId + MaxMacroBits - log2blkSize;
        ADDR_ASSERT(index < sizeof(MipTailOffset) / sizeof(UINT_32));
        *pMipTailOffset = MipTailOffset[index] << 8;
    }

    return mipStartPos;
}

/**
****************************************************************************************************
*   Lib::GetMipTailDim
*
*   @brief
*       Internal function to get out max dimension of first level in mip tail
*
*   @return
*       Max Width/Height/Depth value of the first mip fitted in mip tail
****************************************************************************************************
*/
Dim3d Lib::GetMipTailDim(
    AddrResourceType  resourceType,
    AddrSwizzleMode   swizzleMode,
    UINT_32           blockWidth,
    UINT_32           blockHeight,
    UINT_32           blockDepth) const
{
    Dim3d out = {blockWidth, blockHeight, blockDepth};
    UINT_32 log2blkSize = GetBlockSizeLog2(swizzleMode);

    if (IsThick(resourceType, swizzleMode))
    {
        UINT_32 dim = log2blkSize % 3;

        if (dim == 0)
        {
            out.h >>= 1;
        }
        else if (dim == 1)
        {
            out.w >>= 1;
        }
        else
        {
            out.d >>= 1;
        }
    }
    else
    {
        if (log2blkSize & 1)
        {
            out.h >>= 1;
        }
        else
        {
            out.w >>= 1;
        }
    }

    return out;
}

/**
****************************************************************************************************
*   Lib::ComputeSurface2DMicroBlockOffset
*
*   @brief
*       Internal function to calculate micro block (256B) offset from coord for 2D resource
*
*   @return
*       micro block (256B) offset for 2D resource
****************************************************************************************************
*/
UINT_32 Lib::ComputeSurface2DMicroBlockOffset(
    const _ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn) const
{
    ADDR_ASSERT(IsThin(pIn->resourceType, pIn->swizzleMode));

    UINT_32 log2ElementBytes = Log2(pIn->bpp >> 3);
    UINT_32 microBlockOffset = 0;
    if (IsStandardSwizzle(pIn->resourceType, pIn->swizzleMode))
    {
        UINT_32 xBits = pIn->x << log2ElementBytes;
        microBlockOffset = (xBits & 0xf) | ((pIn->y & 0x3) << 4);
        if (log2ElementBytes < 3)
        {
            microBlockOffset |= (pIn->y & 0x4) << 4;
            if (log2ElementBytes == 0)
            {
                microBlockOffset |= (pIn->y & 0x8) << 4;
            }
            else
            {
                microBlockOffset |= (xBits & 0x10) << 3;
            }
        }
        else
        {
            microBlockOffset |= (xBits & 0x30) << 2;
        }
    }
    else if (IsDisplaySwizzle(pIn->resourceType, pIn->swizzleMode))
    {
        if (log2ElementBytes == 4)
        {
            microBlockOffset = (GetBit(pIn->x, 0) << 4) |
                               (GetBit(pIn->y, 0) << 5) |
                               (GetBit(pIn->x, 1) << 6) |
                               (GetBit(pIn->y, 1) << 7);
        }
        else
        {
            microBlockOffset = GetBits(pIn->x, 0, 3, log2ElementBytes)     |
                               GetBits(pIn->y, 1, 2, 3 + log2ElementBytes) |
                               GetBits(pIn->x, 3, 1, 5 + log2ElementBytes) |
                               GetBits(pIn->y, 3, 1, 6 + log2ElementBytes);
            microBlockOffset = GetBits(microBlockOffset, 0, 4, 0) |
                               (GetBit(pIn->y, 0) << 4) |
                               GetBits(microBlockOffset, 4, 3, 5);
        }
    }
    else if (IsRotateSwizzle(pIn->swizzleMode))
    {
        microBlockOffset = GetBits(pIn->y, 0, 3, log2ElementBytes) |
                           GetBits(pIn->x, 1, 2, 3 + log2ElementBytes) |
                           GetBits(pIn->x, 3, 1, 5 + log2ElementBytes) |
                           GetBits(pIn->y, 3, 1, 6 + log2ElementBytes);
        microBlockOffset = GetBits(microBlockOffset, 0, 4, 0) |
                           (GetBit(pIn->x, 0) << 4) |
                           GetBits(microBlockOffset, 4, 3, 5);
        if (log2ElementBytes == 3)
        {
           microBlockOffset = GetBits(microBlockOffset, 0, 6, 0) |
                              GetBits(pIn->x, 1, 2, 6);
        }
    }

    return microBlockOffset;
}

/**
****************************************************************************************************
*   Lib::ComputeSurface3DMicroBlockOffset
*
*   @brief
*       Internal function to calculate micro block (1KB) offset from coord for 3D resource
*
*   @return
*       micro block (1KB) offset for 3D resource
****************************************************************************************************
*/
UINT_32 Lib::ComputeSurface3DMicroBlockOffset(
    const _ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn) const
{
    ADDR_ASSERT(IsThick(pIn->resourceType, pIn->swizzleMode));

    UINT_32 log2ElementBytes = Log2(pIn->bpp >> 3);
    UINT_32 microBlockOffset = 0;
    if (IsStandardSwizzle(pIn->resourceType, pIn->swizzleMode))
    {
        if (log2ElementBytes == 0)
        {
            microBlockOffset = ((pIn->slice & 4) >> 2) | ((pIn->y & 4) >> 1);
        }
        else if (log2ElementBytes == 1)
        {
            microBlockOffset = ((pIn->slice & 4) >> 2) | ((pIn->y & 4) >> 1);
        }
        else if (log2ElementBytes == 2)
        {
            microBlockOffset = ((pIn->y & 4) >> 2) | ((pIn->x & 4) >> 1);
        }
        else if (log2ElementBytes == 3)
        {
            microBlockOffset = (pIn->x & 6) >> 1;
        }
        else
        {
            microBlockOffset = pIn->x & 3;
        }

        microBlockOffset <<= 8;

        UINT_32 xBits = pIn->x << log2ElementBytes;
        microBlockOffset |= (xBits & 0xf) | ((pIn->y & 0x3) << 4) | ((pIn->slice & 0x3) << 6);
    }
    else if (IsZOrderSwizzle(pIn->swizzleMode))
    {
        UINT_32 xh, yh, zh;

        if (log2ElementBytes == 0)
        {
            microBlockOffset =
                (pIn->x & 1) | ((pIn->y & 1) << 1) | ((pIn->x & 2) << 1) | ((pIn->y & 2) << 2);
            microBlockOffset = microBlockOffset | ((pIn->slice & 3) << 4) | ((pIn->x & 4) << 4);

            xh = pIn->x >> 3;
            yh = pIn->y >> 2;
            zh = pIn->slice >> 2;
        }
        else if (log2ElementBytes == 1)
        {
            microBlockOffset =
                (pIn->x & 1) | ((pIn->y & 1) << 1) | ((pIn->x & 2) << 1) | ((pIn->y & 2) << 2);
            microBlockOffset = (microBlockOffset << 1) | ((pIn->slice & 3) << 5);

            xh = pIn->x >> 2;
            yh = pIn->y >> 2;
            zh = pIn->slice >> 2;
        }
        else if (log2ElementBytes == 2)
        {
            microBlockOffset =
                (pIn->x & 1) | ((pIn->y & 1) << 1) | ((pIn->x & 2) << 1) | ((pIn->slice & 1) << 3);
            microBlockOffset = (microBlockOffset << 2) | ((pIn->y & 2) << 5);

            xh = pIn->x >> 2;
            yh = pIn->y >> 2;
            zh = pIn->slice >> 1;
        }
        else if (log2ElementBytes == 3)
        {
            microBlockOffset =
                (pIn->x & 1) | ((pIn->y & 1) << 1) | ((pIn->slice & 1) << 2) | ((pIn->x & 2) << 2);
            microBlockOffset <<= 3;

            xh = pIn->x >> 2;
            yh = pIn->y >> 1;
            zh = pIn->slice >> 1;
        }
        else
        {
            microBlockOffset =
                (((pIn->x & 1) | ((pIn->y & 1) << 1) | ((pIn->slice & 1) << 2)) << 4);

            xh = pIn->x >> 1;
            yh = pIn->y >> 1;
            zh = pIn->slice >> 1;
        }

        microBlockOffset |= ((MortonGen3d(xh, yh, zh, 1) << 7) & 0x380);
    }

    return microBlockOffset;
}

/**
****************************************************************************************************
*   Lib::GetPipeXorBits
*
*   @brief
*       Internal function to get bits number for pipe/se xor operation
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
UINT_32 Lib::GetPipeXorBits(
    UINT_32 macroBlockBits) const
{
    ADDR_ASSERT(macroBlockBits >= m_pipeInterleaveLog2);

    // Total available xor bits
    UINT_32 xorBits = macroBlockBits - m_pipeInterleaveLog2;

    // Pipe/Se xor bits
    UINT_32 pipeBits = Min(xorBits, m_pipesLog2 + m_seLog2);

    return pipeBits;
}

/**
****************************************************************************************************
*   Lib::GetBankXorBits
*
*   @brief
*       Internal function to get bits number for pipe/se xor operation
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
UINT_32 Lib::GetBankXorBits(
    UINT_32 macroBlockBits) const
{
    UINT_32 pipeBits = GetPipeXorBits(macroBlockBits);

    // Bank xor bits
    UINT_32 bankBits = Min(macroBlockBits - pipeBits - m_pipeInterleaveLog2, m_banksLog2);

    return bankBits;
}

/**
****************************************************************************************************
*   Lib::Addr2GetPreferredSurfaceSetting
*
*   @brief
*       Internal function to get suggested surface information for cliet to use
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::Addr2GetPreferredSurfaceSetting(
    const ADDR2_GET_PREFERRED_SURF_SETTING_INPUT* pIn,
    ADDR2_GET_PREFERRED_SURF_SETTING_OUTPUT*      pOut) const
{
    // Macro define resource block type
    enum AddrBlockType
    {
        AddrBlockMicro     = 0, // Resource uses 256B block
        AddrBlock4KB       = 1, // Resource uses 4KB block
        AddrBlock64KB      = 2, // Resource uses 64KB block
        AddrBlockVar       = 3, // Resource uses var blcok
        AddrBlockLinear    = 4, // Resource uses linear swizzle mode

        AddrBlockMaxTiledType = AddrBlock64KB + 1,
    };

    enum AddrBlockSet
    {
        AddrBlockSetMicro     = 1 << AddrBlockMicro,
        AddrBlockSetMacro4KB  = 1 << AddrBlock4KB,
        AddrBlockSetMacro64KB = 1 << AddrBlock64KB,
        AddrBlockSetVar       = 1 << AddrBlockVar,
        AddrBlockSetLinear    = 1 << AddrBlockLinear,

        AddrBlockSetMacro = AddrBlockSetMacro4KB | AddrBlockSetMacro64KB,
    };

    ADDR_E_RETURNCODE returnCode = ADDR_OK;
    ElemLib*          pElemLib   = GetElemLib();

    // Set format to INVALID will skip this conversion
    UINT_32 expandX = 1;
    UINT_32 expandY = 1;
    UINT_32 bpp = pIn->bpp;
    if (pIn->format != ADDR_FMT_INVALID)
    {
        // Don't care for this case
        ElemMode elemMode = ADDR_UNCOMPRESSED;

        // Get compression/expansion factors and element mode which indicates compression/expansion
        bpp = pElemLib->GetBitsPerPixel(pIn->format,
                                        &elemMode,
                                        &expandX,
                                        &expandY);
    }

    UINT_32 numSamples = Max(pIn->numSamples, 1u);
    UINT_32 numFrags = (pIn->numFrags == 0) ? numSamples : pIn->numFrags;
    UINT_32 width = Max(pIn->width / expandX, 1u);
    UINT_32 height = Max(pIn->height / expandY, 1u);
    UINT_32 slice = Max(pIn->numSlices, 1u);
    UINT_32 numMipLevels = Max(pIn->numMipLevels, 1u);

    if (pIn->flags.fmask)
    {
        bpp        = GetFmaskBpp(numSamples, numFrags);
        numFrags   = 1;
        numSamples = 1;
        pOut->resourceType = ADDR_RSRC_TEX_2D;
    }
    else
    {
        // The output may get changed for volume(3D) texture resource in future
        pOut->resourceType = pIn->resourceType;
    }

    if (IsTex1d(pOut->resourceType))
    {
        pOut->swizzleMode = ADDR_SW_LINEAR;
        pOut->validBlockSet.value = AddrBlockSetLinear;
        pOut->canXor = FALSE;
    }
    else
    {
        ADDR2_BLOCK_SET blockSet;
        AddrSwType swType;

        blockSet.value = 0;

        BOOL_32 tryPrtXor = pIn->flags.prt;

        // Filter out improper swType and blockSet by HW restriction
        if (pIn->flags.fmask || pIn->flags.depth || pIn->flags.stencil)
        {
            ADDR_ASSERT(IsTex2d(pOut->resourceType));
            blockSet.value = AddrBlockSetMacro;
            swType = ADDR_SW_Z;
        }
        else if (pElemLib->IsBlockCompressed(pIn->format))
        {
            // block compressed formats (BCx, ASTC, ETC2) must be either S or D modes.  Not sure
            // under what circumstances "_D" would be appropriate as these formats are not
            // displayable.
            blockSet.value = AddrBlockSetMacro;
            swType = ADDR_SW_S;
        }
        else if (IsTex3d(pOut->resourceType))
        {
            blockSet.value = AddrBlockSetLinear | AddrBlockSetMacro;
            swType = (slice >= 8) ? ADDR_SW_Z : ADDR_SW_S;
        }
        else if (numMipLevels > 1)
        {
            ADDR_ASSERT(numFrags == 1);
            blockSet.value = AddrBlockSetLinear | AddrBlockSetMacro;
            swType = pIn->flags.display ? ADDR_SW_D : ADDR_SW_S;
        }
        else if ((numFrags > 1) || (numSamples > 1))
        {
            ADDR_ASSERT(IsTex2d(pOut->resourceType));
            blockSet.value = AddrBlockSetMacro;
            swType = pIn->flags.display ? ADDR_SW_D : ADDR_SW_S;
        }
        else
        {
            ADDR_ASSERT(IsTex2d(pOut->resourceType));
            blockSet.value = AddrBlockSetLinear | AddrBlockSetMicro | AddrBlockSetMacro;
            if (pIn->flags.rotated || pIn->flags.display)
            {
                swType = pIn->flags.rotated ? ADDR_SW_R : ADDR_SW_D;

                if (IsDce12())
                {
                    if (pIn->bpp != 32)
                    {
                        blockSet.micro = FALSE;
                    }

                    // DCE12 does not support display surface to be _T swizzle mode
                    tryPrtXor = FALSE;
                }
                else
                {
                    ADDR_NOT_IMPLEMENTED();
                }
            }
            else if (pIn->flags.overlay)
            {
                swType = ADDR_SW_D;
            }
            else
            {
                swType = ADDR_SW_S;
            }
        }

        if ((numFrags > 1) &&
            (GetBlockSize(ADDR_SW_4KB) < (m_pipeInterleaveBytes * numFrags)))
        {
            // MSAA surface must have blk_bytes/pipe_interleave >= num_samples
            blockSet.macro4KB = FALSE;
        }

        if (pIn->flags.prt)
        {
            blockSet.value &= AddrBlock64KB;
        }

        // Apply customized forbidden setting
        blockSet.value &= ~pIn->forbiddenBlock.value;

        if (pIn->maxAlign > 0)
        {
            if (pIn->maxAlign < GetBlockSize(ADDR_SW_64KB))
            {
                blockSet.macro64KB = FALSE;
            }

            if (pIn->maxAlign < GetBlockSize(ADDR_SW_4KB))
            {
                blockSet.macro4KB = FALSE;
            }

            if (pIn->maxAlign < GetBlockSize(ADDR_SW_256B))
            {
                blockSet.micro = FALSE;
            }
        }

        Dim3d blkDim[AddrBlockMaxTiledType] = {{0}, {0}, {0}};
        Dim3d padDim[AddrBlockMaxTiledType] = {{0}, {0}, {0}};
        UINT_64 padSize[AddrBlockMaxTiledType] = {0};

        if (blockSet.micro)
        {
            returnCode = ComputeBlockDimensionForSurf(&blkDim[AddrBlockMicro],
                                                      bpp,
                                                      numFrags,
                                                      pOut->resourceType,
                                                      ADDR_SW_256B);

            if (returnCode == ADDR_OK)
            {
                if ((blkDim[AddrBlockMicro].w >= width) && (blkDim[AddrBlockMicro].h >= height))
                {
                    // If one 256B block can contain the surface, don't bother bigger block type
                    blockSet.macro4KB = FALSE;
                    blockSet.macro64KB = FALSE;
                    blockSet.var = FALSE;
                }

                padSize[AddrBlockMicro] = ComputePadSize(&blkDim[AddrBlockMicro], width, height,
                                                         slice, &padDim[AddrBlockMicro]);
            }
        }

        if ((returnCode == ADDR_OK) && (blockSet.macro4KB))
        {
            returnCode = ComputeBlockDimensionForSurf(&blkDim[AddrBlock4KB],
                                                      bpp,
                                                      numFrags,
                                                      pOut->resourceType,
                                                      ADDR_SW_4KB);

            if (returnCode == ADDR_OK)
            {
                padSize[AddrBlock4KB] = ComputePadSize(&blkDim[AddrBlock4KB], width, height,
                                                       slice, &padDim[AddrBlock4KB]);

                ADDR_ASSERT(padSize[AddrBlock4KB] >= padSize[AddrBlockMicro]);
            }
        }

        if ((returnCode == ADDR_OK) && (blockSet.macro64KB))
        {
            returnCode = ComputeBlockDimensionForSurf(&blkDim[AddrBlock64KB],
                                                      bpp,
                                                      numFrags,
                                                      pOut->resourceType,
                                                      ADDR_SW_64KB);

            if (returnCode == ADDR_OK)
            {
                padSize[AddrBlock64KB] = ComputePadSize(&blkDim[AddrBlock64KB], width, height,
                                                        slice, &padDim[AddrBlock64KB]);

                ADDR_ASSERT(padSize[AddrBlock64KB] >= padSize[AddrBlock4KB]);
                ADDR_ASSERT(padSize[AddrBlock64KB] >= padSize[AddrBlockMicro]);

                if ((padSize[AddrBlock64KB] >= static_cast<UINT_64>(width) * height * slice * 2) &&
                    ((blockSet.value & ~AddrBlockSetMacro64KB) != 0))
                {
                    // If 64KB block waste more than half memory on padding, filter it out from
                    // candidate list when it is not the only choice left
                    blockSet.macro64KB = FALSE;
                }
            }
        }

        if (returnCode == ADDR_OK)
        {
            // Use minimum block type which meets all conditions above if flag minimizeAlign was set
            if (pIn->flags.minimizeAlign)
            {
                // If padded size of 64KB block is larger than padded size of 256B block or 4KB
                // block, filter out 64KB block from candidate list
                if (blockSet.macro64KB &&
                    ((blockSet.micro && (padSize[AddrBlockMicro] < padSize[AddrBlock64KB])) ||
                     (blockSet.macro4KB && (padSize[AddrBlock4KB] < padSize[AddrBlock64KB]))))
                {
                    blockSet.macro64KB = FALSE;
                }

                // If padded size of 4KB block is larger than padded size of 256B block,
                // filter out 4KB block from candidate list
                if (blockSet.macro4KB &&
                    blockSet.micro &&
                    (padSize[AddrBlockMicro] < padSize[AddrBlock4KB]))
                {
                    blockSet.macro4KB = FALSE;
                }
            }
            // Filter out 64KB/4KB block if a smaller block type has 2/3 or less memory footprint
            else if (pIn->flags.opt4space)
            {
                UINT_64 threshold =
                    blockSet.micro ?
                    padSize[AddrBlockMicro] :
                    (blockSet.macro4KB ? padSize[AddrBlock4KB] : padSize[AddrBlock64KB]);

                threshold += threshold >> 1;

                if (blockSet.macro64KB && (padSize[AddrBlock64KB] > threshold))
                {
                    blockSet.macro64KB = FALSE;
                }

                if (blockSet.macro4KB && (padSize[AddrBlock4KB] > threshold))
                {
                    blockSet.macro4KB = FALSE;
                }
            }

            if (blockSet.value == 0)
            {
                // Bad things happen, client will not get any useful information from AddrLib.
                // Maybe we should fill in some output earlier instead of outputing nothing?
                ADDR_ASSERT_ALWAYS();
                returnCode = ADDR_INVALIDPARAMS;
            }
            else
            {
                pOut->validBlockSet = blockSet;
                pOut->canXor = (pIn->flags.prt == FALSE) &&
                               (blockSet.macro4KB || blockSet.macro64KB || blockSet.var);

                if (blockSet.macro64KB || blockSet.macro4KB)
                {
                    if (swType == ADDR_SW_Z)
                    {
                        pOut->swizzleMode = blockSet.macro64KB ? ADDR_SW_64KB_Z : ADDR_SW_4KB_Z;
                    }
                    else if (swType == ADDR_SW_S)
                    {
                        pOut->swizzleMode = blockSet.macro64KB ? ADDR_SW_64KB_S : ADDR_SW_4KB_S;
                    }
                    else if (swType == ADDR_SW_D)
                    {
                        pOut->swizzleMode = blockSet.macro64KB ? ADDR_SW_64KB_D : ADDR_SW_4KB_D;
                    }
                    else
                    {
                        ADDR_ASSERT(swType == ADDR_SW_R);
                        pOut->swizzleMode = blockSet.macro64KB ? ADDR_SW_64KB_R : ADDR_SW_4KB_R;
                    }

                    if (pIn->noXor == FALSE)
                    {
                        if (tryPrtXor && blockSet.macro64KB)
                        {
                            // Client wants PRTXOR, give back _T swizzle mode if 64KB is available
                            static const UINT_32 PrtGap = ADDR_SW_64KB_Z_T - ADDR_SW_64KB_Z;
                            pOut->swizzleMode =
                                static_cast<AddrSwizzleMode>(pOut->swizzleMode + PrtGap);
                        }
                        else if (pOut->canXor)
                        {
                            // Client wants XOR and this is allowed, return XOR version swizzle mode
                            static const UINT_32 XorGap = ADDR_SW_4KB_Z_X - ADDR_SW_4KB_Z;
                            pOut->swizzleMode =
                                static_cast<AddrSwizzleMode>(pOut->swizzleMode + XorGap);
                        }
                    }
                }
                else if (blockSet.var)
                {
                    // Designer consider this swizzle is usless for most cases
                    ADDR_UNHANDLED_CASE();
                }
                else if (blockSet.micro)
                {
                    if (swType == ADDR_SW_S)
                    {
                        pOut->swizzleMode = ADDR_SW_256B_S;
                    }
                    else if (swType == ADDR_SW_D)
                    {
                        pOut->swizzleMode = ADDR_SW_256B_D;
                    }
                    else
                    {
                        ADDR_ASSERT(swType == ADDR_SW_R);
                        pOut->swizzleMode = ADDR_SW_256B_R;
                    }
                }
                else
                {
                    ADDR_ASSERT(blockSet.linear);
                    // Fall into this branch doesn't mean linear is suitable, only no other choices!
                    pOut->swizzleMode = ADDR_SW_LINEAR;
                }

#if DEBUG
                // Post sanity check, at least AddrLib should accept the output generated by its own
                if (pOut->swizzleMode != ADDR_SW_LINEAR)
                {
                    ADDR2_COMPUTE_SURFACE_INFO_INPUT localIn = {0};
                    localIn.flags = pIn->flags;
                    localIn.swizzleMode = pOut->swizzleMode;
                    localIn.resourceType = pOut->resourceType;
                    localIn.format = pIn->format;
                    localIn.bpp = bpp;
                    localIn.width = width;
                    localIn.height = height;
                    localIn.numSlices = slice;
                    localIn.numMipLevels = numMipLevels;
                    localIn.numSamples = numSamples;
                    localIn.numFrags = numFrags;

                    ADDR_E_RETURNCODE coherentCheck = ComputeSurfaceInfoSanityCheck(&localIn);
                    ADDR_ASSERT(coherentCheck == ADDR_OK);

                    // TODO : check all valid block type available in validBlockSet?
                }
#endif
            }
        }
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Lib::ComputeBlock256Equation
*
*   @brief
*       Compute equation for block 256B
*
*   @return
*       If equation computed successfully
*
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeBlock256Equation(
    AddrResourceType rsrcType,
    AddrSwizzleMode swMode,
    UINT_32 elementBytesLog2,
    ADDR_EQUATION* pEquation) const
{
    ADDR_E_RETURNCODE ret;

    if (IsBlock256b(swMode))
    {
        ret = HwlComputeBlock256Equation(rsrcType, swMode, elementBytesLog2, pEquation);
    }
    else
    {
        ADDR_ASSERT_ALWAYS();
        ret = ADDR_INVALIDPARAMS;
    }

    return ret;
}

/**
****************************************************************************************************
*   Lib::ComputeThinEquation
*
*   @brief
*       Compute equation for 2D/3D resource which use THIN mode
*
*   @return
*       If equation computed successfully
*
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeThinEquation(
    AddrResourceType rsrcType,
    AddrSwizzleMode swMode,
    UINT_32 elementBytesLog2,
    ADDR_EQUATION* pEquation) const
{
    ADDR_E_RETURNCODE ret;

    if (IsThin(rsrcType, swMode))
    {
        ret = HwlComputeThinEquation(rsrcType, swMode, elementBytesLog2, pEquation);
    }
    else
    {
        ADDR_ASSERT_ALWAYS();
        ret = ADDR_INVALIDPARAMS;
    }

    return ret;
}

/**
****************************************************************************************************
*   Lib::ComputeThickEquation
*
*   @brief
*       Compute equation for 3D resource which use THICK mode
*
*   @return
*       If equation computed successfully
*
****************************************************************************************************
*/
ADDR_E_RETURNCODE Lib::ComputeThickEquation(
    AddrResourceType rsrcType,
    AddrSwizzleMode swMode,
    UINT_32 elementBytesLog2,
    ADDR_EQUATION* pEquation) const
{
    ADDR_E_RETURNCODE ret;

    if (IsThick(rsrcType, swMode))
    {
        ret = HwlComputeThickEquation(rsrcType, swMode, elementBytesLog2, pEquation);
    }
    else
    {
        ADDR_ASSERT_ALWAYS();
        ret = ADDR_INVALIDPARAMS;
    }

    return ret;
}

} // V2
} // Addr

