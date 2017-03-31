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
* @file  gfx9addrlib.cpp
* @brief Contgfx9ns the implementation for the Gfx9Lib class.
****************************************************************************************************
*/

#include "gfx9addrlib.h"

#include "gfx9_gb_reg.h"
#include "gfx9_enum.h"

#if BRAHMA_BUILD
#include "amdgpu_id.h"
#else
#include "ai_id.h"
#include "rv_id.h"
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Addr
{

/**
****************************************************************************************************
*   Gfx9HwlInit
*
*   @brief
*       Creates an Gfx9Lib object.
*
*   @return
*       Returns an Gfx9Lib object pointer.
****************************************************************************************************
*/
Addr::Lib* Gfx9HwlInit(const Client* pClient)
{
    return V2::Gfx9Lib::CreateObj(pClient);
}

namespace V2
{

/**
****************************************************************************************************
*   Gfx9Lib::Gfx9Lib
*
*   @brief
*       Constructor
*
****************************************************************************************************
*/
Gfx9Lib::Gfx9Lib(const Client* pClient)
    :
    Lib(pClient),
    m_numEquations(0)
{
    m_class = AI_ADDRLIB;
    memset(&m_settings, 0, sizeof(m_settings));
}

/**
****************************************************************************************************
*   Gfx9Lib::~Gfx9Lib
*
*   @brief
*       Destructor
****************************************************************************************************
*/
Gfx9Lib::~Gfx9Lib()
{
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlComputeHtileInfo
*
*   @brief
*       Interface function stub of AddrComputeHtilenfo
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeHtileInfo(
    const ADDR2_COMPUTE_HTILE_INFO_INPUT*    pIn,    ///< [in] input structure
    ADDR2_COMPUTE_HTILE_INFO_OUTPUT*         pOut    ///< [out] output structure
    ) const
{
    UINT_32 numPipeTotal = GetPipeNumForMetaAddressing(pIn->hTileFlags.pipeAligned,
                                                       pIn->swizzleMode);

    UINT_32 numRbTotal = pIn->hTileFlags.rbAligned ? m_se * m_rbPerSe : 1;

    UINT_32 numCompressBlkPerMetaBlk, numCompressBlkPerMetaBlkLog2;

    if ((numPipeTotal == 1) && (numRbTotal == 1))
    {
        numCompressBlkPerMetaBlkLog2 = 10;
    }
    else
    {
        numCompressBlkPerMetaBlkLog2 = m_seLog2 + m_rbPerSeLog2 + 10;
    }

    numCompressBlkPerMetaBlk = 1 << numCompressBlkPerMetaBlkLog2;

    Dim3d metaBlkDim = {8, 8, 1};
    UINT_32 totalAmpBits = numCompressBlkPerMetaBlkLog2;
    UINT_32 widthAmp = (pIn->numMipLevels > 1) ? (totalAmpBits >> 1) : RoundHalf(totalAmpBits);
    UINT_32 heightAmp = totalAmpBits - widthAmp;
    metaBlkDim.w <<= widthAmp;
    metaBlkDim.h <<= heightAmp;

#if DEBUG
    Dim3d metaBlkDimDbg = {8, 8, 1};
    for (UINT_32 index = 0; index < numCompressBlkPerMetaBlkLog2; index++)
    {
        if ((metaBlkDimDbg.h < metaBlkDimDbg.w) ||
            ((pIn->numMipLevels > 1) && (metaBlkDimDbg.h == metaBlkDimDbg.w)))
        {
            metaBlkDimDbg.h <<= 1;
        }
        else
        {
            metaBlkDimDbg.w <<= 1;
        }
    }
    ADDR_ASSERT((metaBlkDimDbg.w == metaBlkDim.w) && (metaBlkDimDbg.h == metaBlkDim.h));
#endif

    UINT_32 numMetaBlkX;
    UINT_32 numMetaBlkY;
    UINT_32 numMetaBlkZ;

    GetMetaMipInfo(pIn->numMipLevels, &metaBlkDim, FALSE, pOut->pMipInfo,
                   pIn->unalignedWidth, pIn->unalignedHeight, pIn->numSlices,
                   &numMetaBlkX, &numMetaBlkY, &numMetaBlkZ);

    UINT_32 sizeAlign = numPipeTotal * numRbTotal * m_pipeInterleaveBytes;

    pOut->pitch      = numMetaBlkX * metaBlkDim.w;
    pOut->height     = numMetaBlkY * metaBlkDim.h;
    pOut->sliceSize  = numMetaBlkX * numMetaBlkY * numCompressBlkPerMetaBlk * 4;

    pOut->metaBlkWidth = metaBlkDim.w;
    pOut->metaBlkHeight = metaBlkDim.h;
    pOut->metaBlkNumPerSlice = numMetaBlkX * numMetaBlkY;

    if ((IsXor(pIn->swizzleMode) == FALSE) && (numPipeTotal > 2))
    {
        UINT_32 additionalAlign = numPipeTotal * numCompressBlkPerMetaBlk * 2;

        if (additionalAlign > sizeAlign)
        {
            sizeAlign = additionalAlign;
        }
    }

    pOut->htileBytes = PowTwoAlign(pOut->sliceSize * numMetaBlkZ, sizeAlign);
    pOut->baseAlign  = Max(numCompressBlkPerMetaBlk * 4, sizeAlign);

    if (m_settings.metaBaseAlignFix)
    {
        pOut->baseAlign = Max(pOut->baseAlign, HwlComputeSurfaceBaseAlign(pIn->swizzleMode));
    }

    return ADDR_OK;
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlComputeCmaskInfo
*
*   @brief
*       Interface function stub of AddrComputeCmaskInfo
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeCmaskInfo(
    const ADDR2_COMPUTE_CMASK_INFO_INPUT*    pIn,    ///< [in] input structure
    ADDR2_COMPUTE_CMASK_INFO_OUTPUT*         pOut    ///< [out] output structure
    ) const
{
    ADDR_ASSERT(pIn->resourceType == ADDR_RSRC_TEX_2D);

    UINT_32 numPipeTotal = GetPipeNumForMetaAddressing(pIn->cMaskFlags.pipeAligned,
                                                       pIn->swizzleMode);

    UINT_32 numRbTotal = pIn->cMaskFlags.rbAligned ? m_se * m_rbPerSe : 1;

    UINT_32 numCompressBlkPerMetaBlkLog2, numCompressBlkPerMetaBlk;

    if ((numPipeTotal == 1) && (numRbTotal == 1))
    {
        numCompressBlkPerMetaBlkLog2 = 13;
    }
    else
    {
        numCompressBlkPerMetaBlkLog2 = m_seLog2 + m_rbPerSeLog2 + 10;

        numCompressBlkPerMetaBlkLog2 = Max(numCompressBlkPerMetaBlkLog2, 13u);
    }

    numCompressBlkPerMetaBlk = 1 << numCompressBlkPerMetaBlkLog2;

    Dim2d metaBlkDim = {8, 8};
    UINT_32 totalAmpBits = numCompressBlkPerMetaBlkLog2;
    UINT_32 heightAmp = totalAmpBits >> 1;
    UINT_32 widthAmp = totalAmpBits - heightAmp;
    metaBlkDim.w <<= widthAmp;
    metaBlkDim.h <<= heightAmp;

#if DEBUG
    Dim2d metaBlkDimDbg = {8, 8};
    for (UINT_32 index = 0; index < numCompressBlkPerMetaBlkLog2; index++)
    {
        if (metaBlkDimDbg.h < metaBlkDimDbg.w)
        {
            metaBlkDimDbg.h <<= 1;
        }
        else
        {
            metaBlkDimDbg.w <<= 1;
        }
    }
    ADDR_ASSERT((metaBlkDimDbg.w == metaBlkDim.w) && (metaBlkDimDbg.h == metaBlkDim.h));
#endif

    UINT_32 numMetaBlkX = (pIn->unalignedWidth  + metaBlkDim.w - 1) / metaBlkDim.w;
    UINT_32 numMetaBlkY = (pIn->unalignedHeight + metaBlkDim.h - 1) / metaBlkDim.h;
    UINT_32 numMetaBlkZ = Max(pIn->numSlices, 1u);

    UINT_32 sizeAlign = numPipeTotal * numRbTotal * m_pipeInterleaveBytes;

    pOut->pitch      = numMetaBlkX * metaBlkDim.w;
    pOut->height     = numMetaBlkY * metaBlkDim.h;
    pOut->sliceSize  = (numMetaBlkX * numMetaBlkY * numCompressBlkPerMetaBlk) >> 1;
    pOut->cmaskBytes = PowTwoAlign(pOut->sliceSize * numMetaBlkZ, sizeAlign);
    pOut->baseAlign  = Max(numCompressBlkPerMetaBlk >> 1, sizeAlign);

    if (m_settings.metaBaseAlignFix)
    {
        pOut->baseAlign = Max(pOut->baseAlign, HwlComputeSurfaceBaseAlign(pIn->swizzleMode));
    }

    pOut->metaBlkWidth = metaBlkDim.w;
    pOut->metaBlkHeight = metaBlkDim.h;

    pOut->metaBlkNumPerSlice = numMetaBlkX * numMetaBlkY;

    return ADDR_OK;
}

/**
****************************************************************************************************
*   Gfx9Lib::GetMetaMipInfo
*
*   @brief
*       Get meta mip info
*
*   @return
*       N/A
****************************************************************************************************
*/
VOID Gfx9Lib::GetMetaMipInfo(
    UINT_32 numMipLevels,           ///< [in]  number of mip levels
    Dim3d* pMetaBlkDim,             ///< [in]  meta block dimension
    BOOL_32 dataThick,              ///< [in]  data surface is thick
    ADDR2_META_MIP_INFO* pInfo,     ///< [out] meta mip info
    UINT_32 mip0Width,              ///< [in]  mip0 width
    UINT_32 mip0Height,             ///< [in]  mip0 height
    UINT_32 mip0Depth,              ///< [in]  mip0 depth
    UINT_32* pNumMetaBlkX,          ///< [out] number of metablock X in mipchain
    UINT_32* pNumMetaBlkY,          ///< [out] number of metablock Y in mipchain
    UINT_32* pNumMetaBlkZ)          ///< [out] number of metablock Z in mipchain
    const
{
    UINT_32 numMetaBlkX = (mip0Width  + pMetaBlkDim->w - 1) / pMetaBlkDim->w;
    UINT_32 numMetaBlkY = (mip0Height + pMetaBlkDim->h - 1) / pMetaBlkDim->h;
    UINT_32 numMetaBlkZ = (mip0Depth  + pMetaBlkDim->d - 1) / pMetaBlkDim->d;
    UINT_32 tailWidth   = pMetaBlkDim->w;
    UINT_32 tailHeight  = pMetaBlkDim->h >> 1;
    UINT_32 tailDepth   = pMetaBlkDim->d;
    BOOL_32 inTail      = FALSE;
    AddrMajorMode major = ADDR_MAJOR_MAX_TYPE;

    if (numMipLevels > 1)
    {
        if (dataThick && (numMetaBlkZ > numMetaBlkX) && (numMetaBlkZ > numMetaBlkY))
        {
            // Z major
            major = ADDR_MAJOR_Z;
        }
        else if (numMetaBlkX >= numMetaBlkY)
        {
            // X major
            major = ADDR_MAJOR_X;
        }
        else
        {
            // Y major
            major = ADDR_MAJOR_Y;
        }

        inTail = ((mip0Width <= tailWidth) &&
                  (mip0Height <= tailHeight) &&
                  ((dataThick == FALSE) || (mip0Depth <= tailDepth)));

        if (inTail == FALSE)
        {
            UINT_32 orderLimit;
            UINT_32 *pMipDim;
            UINT_32 *pOrderDim;

            if (major == ADDR_MAJOR_Z)
            {
                // Z major
                pMipDim = &numMetaBlkY;
                pOrderDim = &numMetaBlkZ;
                orderLimit = 4;
            }
            else if (major == ADDR_MAJOR_X)
            {
                // X major
                pMipDim = &numMetaBlkY;
                pOrderDim = &numMetaBlkX;
                orderLimit = 4;
            }
            else
            {
                // Y major
                pMipDim = &numMetaBlkX;
                pOrderDim = &numMetaBlkY;
                orderLimit = 2;
            }

            if ((*pMipDim < 3) && (*pOrderDim > orderLimit) && (numMipLevels > 3))
            {
                *pMipDim += 2;
            }
            else
            {
                *pMipDim += ((*pMipDim / 2) + (*pMipDim & 1));
            }
        }
    }

    if (pInfo != NULL)
    {
        UINT_32 mipWidth  = mip0Width;
        UINT_32 mipHeight = mip0Height;
        UINT_32 mipDepth  = mip0Depth;
        Dim3d   mipCoord  = {0};

        for (UINT_32 mip = 0; mip < numMipLevels; mip++)
        {
            if (inTail)
            {
                GetMetaMiptailInfo(&pInfo[mip], mipCoord, numMipLevels - mip,
                                   pMetaBlkDim);
                break;
            }
            else
            {
                mipWidth  = PowTwoAlign(mipWidth, pMetaBlkDim->w);
                mipHeight = PowTwoAlign(mipHeight, pMetaBlkDim->h);
                mipDepth  = PowTwoAlign(mipDepth, pMetaBlkDim->d);

                pInfo[mip].inMiptail = FALSE;
                pInfo[mip].startX = mipCoord.w;
                pInfo[mip].startY = mipCoord.h;
                pInfo[mip].startZ = mipCoord.d;
                pInfo[mip].width  = mipWidth;
                pInfo[mip].height = mipHeight;
                pInfo[mip].depth  = dataThick ? mipDepth : 1;

                if ((mip >= 3) || (mip & 1))
                {
                    switch (major)
                    {
                        case ADDR_MAJOR_X:
                            mipCoord.w += mipWidth;
                            break;
                        case ADDR_MAJOR_Y:
                            mipCoord.h += mipHeight;
                            break;
                        case ADDR_MAJOR_Z:
                            mipCoord.d += mipDepth;
                            break;
                        default:
                            break;
                    }
                }
                else
                {
                    switch (major)
                    {
                        case ADDR_MAJOR_X:
                            mipCoord.h += mipHeight;
                            break;
                        case ADDR_MAJOR_Y:
                            mipCoord.w += mipWidth;
                            break;
                        case ADDR_MAJOR_Z:
                            mipCoord.h += mipHeight;
                            break;
                        default:
                            break;
                    }
                }

                mipWidth  = Max(mipWidth >> 1, 1u);
                mipHeight = Max(mipHeight >> 1, 1u);
                mipDepth = Max(mipDepth >> 1, 1u);

                inTail = ((mipWidth <= tailWidth) &&
                          (mipHeight <= tailHeight) &&
                          ((dataThick == FALSE) || (mipDepth <= tailDepth)));
            }
        }
    }

    *pNumMetaBlkX = numMetaBlkX;
    *pNumMetaBlkY = numMetaBlkY;
    *pNumMetaBlkZ = numMetaBlkZ;
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlComputeDccInfo
*
*   @brief
*       Interface function to compute DCC key info
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeDccInfo(
    const ADDR2_COMPUTE_DCCINFO_INPUT*    pIn,    ///< [in] input structure
    ADDR2_COMPUTE_DCCINFO_OUTPUT*         pOut    ///< [out] output structure
    ) const
{
    BOOL_32 dataLinear = IsLinear(pIn->swizzleMode);
    BOOL_32 metaLinear = pIn->dccKeyFlags.linear;
    BOOL_32 pipeAligned = pIn->dccKeyFlags.pipeAligned;

    if (dataLinear)
    {
        metaLinear = TRUE;
    }
    else if (metaLinear == TRUE)
    {
        pipeAligned = FALSE;
    }

    UINT_32 numPipeTotal = GetPipeNumForMetaAddressing(pipeAligned, pIn->swizzleMode);

    if (metaLinear)
    {
        // Linear metadata supporting was removed for GFX9! No one can use this feature on GFX9.
        ADDR_ASSERT_ALWAYS();

        pOut->dccRamBaseAlign = numPipeTotal * m_pipeInterleaveBytes;
        pOut->dccRamSize = PowTwoAlign((pIn->dataSurfaceSize / 256), pOut->dccRamBaseAlign);
    }
    else
    {
        BOOL_32 dataThick = IsThick(pIn->resourceType, pIn->swizzleMode);

        UINT_32 minMetaBlkSize = dataThick ? 65536 : 4096;

        UINT_32 numFrags = (pIn->numFrags == 0) ? 1 : pIn->numFrags;
        UINT_32 numSlices = (pIn->numSlices == 0) ? 1 : pIn->numSlices;

        minMetaBlkSize /= numFrags;

        UINT_32 numCompressBlkPerMetaBlk = minMetaBlkSize;

        UINT_32 numRbTotal = pIn->dccKeyFlags.rbAligned ? m_se * m_rbPerSe : 1;

        if ((numPipeTotal > 1) || (numRbTotal > 1))
        {
            numCompressBlkPerMetaBlk =
                Max(numCompressBlkPerMetaBlk, m_se * m_rbPerSe * (dataThick ? 262144 : 1024));

            if (numCompressBlkPerMetaBlk > 65536 * pIn->bpp)
            {
                numCompressBlkPerMetaBlk = 65536 * pIn->bpp;
            }
        }

        Dim3d compressBlkDim = GetDccCompressBlk(pIn->resourceType, pIn->swizzleMode, pIn->bpp);
        Dim3d metaBlkDim = compressBlkDim;

        for (UINT_32 index = 1; index < numCompressBlkPerMetaBlk; index <<= 1)
        {
            if ((metaBlkDim.h < metaBlkDim.w) ||
                ((pIn->numMipLevels > 1) && (metaBlkDim.h == metaBlkDim.w)))
            {
                if ((dataThick == FALSE) || (metaBlkDim.h <= metaBlkDim.d))
                {
                    metaBlkDim.h <<= 1;
                }
                else
                {
                    metaBlkDim.d <<= 1;
                }
            }
            else
            {
                if ((dataThick == FALSE) || (metaBlkDim.w <= metaBlkDim.d))
                {
                    metaBlkDim.w <<= 1;
                }
                else
                {
                    metaBlkDim.d <<= 1;
                }
            }
        }

        UINT_32 numMetaBlkX;
        UINT_32 numMetaBlkY;
        UINT_32 numMetaBlkZ;

        GetMetaMipInfo(pIn->numMipLevels, &metaBlkDim, dataThick, pOut->pMipInfo,
                       pIn->unalignedWidth, pIn->unalignedHeight, numSlices,
                       &numMetaBlkX, &numMetaBlkY, &numMetaBlkZ);

        UINT_32 sizeAlign = numPipeTotal * numRbTotal * m_pipeInterleaveBytes;

        if (numFrags > m_maxCompFrag)
        {
            sizeAlign *= (numFrags / m_maxCompFrag);
        }

        pOut->dccRamSize = numMetaBlkX * numMetaBlkY * numMetaBlkZ *
                           numCompressBlkPerMetaBlk * numFrags;
        pOut->dccRamSize = PowTwoAlign(pOut->dccRamSize, sizeAlign);
        pOut->dccRamBaseAlign = Max(numCompressBlkPerMetaBlk, sizeAlign);

        if (m_settings.metaBaseAlignFix)
        {
            pOut->dccRamBaseAlign = Max(pOut->dccRamBaseAlign, HwlComputeSurfaceBaseAlign(pIn->swizzleMode));
        }

        pOut->pitch = numMetaBlkX * metaBlkDim.w;
        pOut->height = numMetaBlkY * metaBlkDim.h;
        pOut->depth = numMetaBlkZ * metaBlkDim.d;

        pOut->compressBlkWidth = compressBlkDim.w;
        pOut->compressBlkHeight = compressBlkDim.h;
        pOut->compressBlkDepth = compressBlkDim.d;

        pOut->metaBlkWidth = metaBlkDim.w;
        pOut->metaBlkHeight = metaBlkDim.h;
        pOut->metaBlkDepth = metaBlkDim.d;

        pOut->metaBlkNumPerSlice = numMetaBlkX * numMetaBlkY;
        pOut->fastClearSizePerSlice =
            pOut->metaBlkNumPerSlice * numCompressBlkPerMetaBlk * Min(numFrags, m_maxCompFrag);
    }

    return ADDR_OK;
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlGetMaxAlignments
*
*   @brief
*       Gets maximum alignments
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlGetMaxAlignments(
    ADDR_GET_MAX_ALINGMENTS_OUTPUT* pOut    ///< [out] output structure
    ) const
{
    pOut->baseAlign = HwlComputeSurfaceBaseAlign(ADDR_SW_64KB);

    return ADDR_OK;
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlComputeCmaskAddrFromCoord
*
*   @brief
*       Interface function stub of AddrComputeCmaskAddrFromCoord
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeCmaskAddrFromCoord(
    const ADDR2_COMPUTE_CMASK_ADDRFROMCOORD_INPUT*   pIn,    ///< [in] input structure
    ADDR2_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT*        pOut    ///< [out] output structure
    ) const
{
    ADDR2_COMPUTE_CMASK_INFO_INPUT input;
    ADDR2_COMPUTE_CMASK_INFO_OUTPUT output;

    memset(&input, 0, sizeof(ADDR2_COMPUTE_CMASK_INFO_INPUT));
    input.size = sizeof(ADDR2_COMPUTE_CMASK_INFO_INPUT);
    input.cMaskFlags = pIn->cMaskFlags;
    input.colorFlags = pIn->colorFlags;
    input.unalignedWidth = Max(pIn->unalignedWidth, 1u);
    input.unalignedHeight = Max(pIn->unalignedHeight, 1u);
    input.numSlices = Max(pIn->numSlices, 1u);
    input.swizzleMode = pIn->swizzleMode;
    input.resourceType = pIn->resourceType;

    memset(&output, 0, sizeof(ADDR2_COMPUTE_CMASK_INFO_OUTPUT));
    output.size = sizeof(ADDR2_COMPUTE_CMASK_INFO_OUTPUT);

    ADDR_E_RETURNCODE returnCode = ComputeCmaskInfo(&input, &output);

    if (returnCode == ADDR_OK)
    {
        UINT_32 fmaskBpp = GetFmaskBpp(pIn->numSamples, pIn->numFrags);

        UINT_32 fmaskElementBytesLog2 = Log2(fmaskBpp >> 3);

        UINT_32 metaBlkWidthLog2 = Log2(output.metaBlkWidth);
        UINT_32 metaBlkHeightLog2 = Log2(output.metaBlkHeight);

        CoordEq metaEq;

        GetMetaEquation(&metaEq, 0, fmaskElementBytesLog2, 0, pIn->cMaskFlags,
                        Gfx9DataFmask, pIn->swizzleMode, pIn->resourceType,
                        metaBlkWidthLog2, metaBlkHeightLog2, 0, 3, 3, 0);

        UINT_32 xb = pIn->x / output.metaBlkWidth;
        UINT_32 yb = pIn->y / output.metaBlkHeight;
        UINT_32 zb = pIn->slice;

        UINT_32 pitchInBlock = output.pitch / output.metaBlkWidth;
        UINT_32 sliceSizeInBlock = (output.height / output.metaBlkHeight) * pitchInBlock;
        UINT_32 blockIndex = zb * sliceSizeInBlock + yb * pitchInBlock + xb;

        UINT_64 address = metaEq.solve(pIn->x, pIn->y, pIn->slice, 0, blockIndex);

        pOut->addr = address >> 1;
        pOut->bitPosition = static_cast<UINT_32>((address & 1) << 2);


        UINT_32 numPipeBits = GetPipeLog2ForMetaAddressing(pIn->cMaskFlags.pipeAligned,
                                                           pIn->swizzleMode);

        UINT_64 pipeXor = static_cast<UINT_64>(pIn->pipeXor & ((1 << numPipeBits) - 1));

        pOut->addr ^= (pipeXor << m_pipeInterleaveLog2);
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlComputeHtileAddrFromCoord
*
*   @brief
*       Interface function stub of AddrComputeHtileAddrFromCoord
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeHtileAddrFromCoord(
    const ADDR2_COMPUTE_HTILE_ADDRFROMCOORD_INPUT*   pIn,    ///< [in] input structure
    ADDR2_COMPUTE_HTILE_ADDRFROMCOORD_OUTPUT*        pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (pIn->numMipLevels > 1)
    {
        returnCode = ADDR_NOTIMPLEMENTED;
    }
    else
    {
        ADDR2_COMPUTE_HTILE_INFO_INPUT input;
        ADDR2_COMPUTE_HTILE_INFO_OUTPUT output;

        memset(&input, 0, sizeof(ADDR2_COMPUTE_HTILE_INFO_INPUT));
        input.size = sizeof(ADDR2_COMPUTE_HTILE_INFO_INPUT);
        input.hTileFlags = pIn->hTileFlags;
        input.depthFlags = pIn->depthflags;
        input.swizzleMode = pIn->swizzleMode;
        input.unalignedWidth = Max(pIn->unalignedWidth, 1u);
        input.unalignedHeight = Max(pIn->unalignedHeight, 1u);
        input.numSlices = Max(pIn->numSlices, 1u);
        input.numMipLevels = Max(pIn->numMipLevels, 1u);

        memset(&output, 0, sizeof(ADDR2_COMPUTE_HTILE_INFO_OUTPUT));
        output.size = sizeof(ADDR2_COMPUTE_HTILE_INFO_OUTPUT);

        returnCode = ComputeHtileInfo(&input, &output);

        if (returnCode == ADDR_OK)
        {
            UINT_32 elementBytesLog2 = Log2(pIn->bpp >> 3);

            UINT_32 metaBlkWidthLog2 = Log2(output.metaBlkWidth);
            UINT_32 metaBlkHeightLog2 = Log2(output.metaBlkHeight);

            UINT_32 numSamplesLog2 = Log2(pIn->numSamples);

            CoordEq metaEq;

            GetMetaEquation(&metaEq, 0, elementBytesLog2, numSamplesLog2, pIn->hTileFlags,
                            Gfx9DataDepthStencil, pIn->swizzleMode, ADDR_RSRC_TEX_2D,
                            metaBlkWidthLog2, metaBlkHeightLog2, 0, 3, 3, 0);

            UINT_32 xb = pIn->x / output.metaBlkWidth;
            UINT_32 yb = pIn->y / output.metaBlkHeight;
            UINT_32 zb = pIn->slice;

            UINT_32 pitchInBlock = output.pitch / output.metaBlkWidth;
            UINT_32 sliceSizeInBlock = (output.height / output.metaBlkHeight) * pitchInBlock;
            UINT_32 blockIndex = zb * sliceSizeInBlock + yb * pitchInBlock + xb;

            UINT_64 address = metaEq.solve(pIn->x, pIn->y, pIn->slice, 0, blockIndex);

            pOut->addr = address >> 1;

            UINT_32 numPipeBits = GetPipeLog2ForMetaAddressing(pIn->hTileFlags.pipeAligned,
                                                               pIn->swizzleMode);

            UINT_64 pipeXor = static_cast<UINT_64>(pIn->pipeXor & ((1 << numPipeBits) - 1));

            pOut->addr ^= (pipeXor << m_pipeInterleaveLog2);
        }
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlComputeHtileCoordFromAddr
*
*   @brief
*       Interface function stub of AddrComputeHtileCoordFromAddr
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeHtileCoordFromAddr(
    const ADDR2_COMPUTE_HTILE_COORDFROMADDR_INPUT*   pIn,    ///< [in] input structure
    ADDR2_COMPUTE_HTILE_COORDFROMADDR_OUTPUT*        pOut    ///< [out] output structure
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (pIn->numMipLevels > 1)
    {
        returnCode = ADDR_NOTIMPLEMENTED;
    }
    else
    {
        ADDR2_COMPUTE_HTILE_INFO_INPUT input;
        ADDR2_COMPUTE_HTILE_INFO_OUTPUT output;

        memset(&input, 0, sizeof(ADDR2_COMPUTE_HTILE_INFO_INPUT));
        input.size = sizeof(ADDR2_COMPUTE_HTILE_INFO_INPUT);
        input.hTileFlags = pIn->hTileFlags;
        input.swizzleMode = pIn->swizzleMode;
        input.unalignedWidth = Max(pIn->unalignedWidth, 1u);
        input.unalignedHeight = Max(pIn->unalignedHeight, 1u);
        input.numSlices = Max(pIn->numSlices, 1u);
        input.numMipLevels = Max(pIn->numMipLevels, 1u);

        memset(&output, 0, sizeof(ADDR2_COMPUTE_HTILE_INFO_OUTPUT));
        output.size = sizeof(ADDR2_COMPUTE_HTILE_INFO_OUTPUT);

        returnCode = ComputeHtileInfo(&input, &output);

        if (returnCode == ADDR_OK)
        {
            UINT_32 elementBytesLog2 = Log2(pIn->bpp >> 3);

            UINT_32 metaBlkWidthLog2 = Log2(output.metaBlkWidth);
            UINT_32 metaBlkHeightLog2 = Log2(output.metaBlkHeight);

            UINT_32 numSamplesLog2 = Log2(pIn->numSamples);

            CoordEq metaEq;

            GetMetaEquation(&metaEq, 0, elementBytesLog2, numSamplesLog2, pIn->hTileFlags,
                            Gfx9DataDepthStencil, pIn->swizzleMode, ADDR_RSRC_TEX_2D,
                            metaBlkWidthLog2, metaBlkHeightLog2, 0, 3, 3, 0);

            UINT_32 numPipeBits = GetPipeLog2ForMetaAddressing(pIn->hTileFlags.pipeAligned,
                                                               pIn->swizzleMode);

            UINT_64 pipeXor = static_cast<UINT_64>(pIn->pipeXor & ((1 << numPipeBits) - 1));

            UINT_64 nibbleAddress = (pIn->addr ^ (pipeXor << m_pipeInterleaveLog2)) << 1;

            UINT_32 pitchInBlock = output.pitch / output.metaBlkWidth;
            UINT_32 sliceSizeInBlock = (output.height / output.metaBlkHeight) * pitchInBlock;

            UINT_32 x, y, z, s, m;

            metaEq.solveAddr(nibbleAddress, sliceSizeInBlock, x, y, z, s, m);

            pOut->slice = m / sliceSizeInBlock;
            pOut->y = ((m % sliceSizeInBlock) / pitchInBlock) * output.metaBlkHeight + y;
            pOut->x = (m % pitchInBlock) * output.metaBlkWidth + x;
        }
    }

    return returnCode;
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlInitGlobalParams
*
*   @brief
*       Initializes global parameters
*
*   @return
*       TRUE if all settings are valid
*
****************************************************************************************************
*/
BOOL_32 Gfx9Lib::HwlInitGlobalParams(
    const ADDR_CREATE_INPUT* pCreateIn) ///< [in] create input
{
    BOOL_32 valid = TRUE;

    if (m_settings.isArcticIsland)
    {
        GB_ADDR_CONFIG gbAddrConfig;

        gbAddrConfig.u32All = pCreateIn->regValue.gbAddrConfig;

        // These values are copied from CModel code
        switch (gbAddrConfig.bits.NUM_PIPES)
        {
            case ADDR_CONFIG_1_PIPE:
                m_pipes = 1;
                m_pipesLog2 = 0;
                break;
            case ADDR_CONFIG_2_PIPE:
                m_pipes = 2;
                m_pipesLog2 = 1;
                break;
            case ADDR_CONFIG_4_PIPE:
                m_pipes = 4;
                m_pipesLog2 = 2;
                break;
            case ADDR_CONFIG_8_PIPE:
                m_pipes = 8;
                m_pipesLog2 = 3;
                break;
            case ADDR_CONFIG_16_PIPE:
                m_pipes = 16;
                m_pipesLog2 = 4;
                break;
            case ADDR_CONFIG_32_PIPE:
                m_pipes = 32;
                m_pipesLog2 = 5;
                break;
            default:
                break;
        }

        switch (gbAddrConfig.bits.PIPE_INTERLEAVE_SIZE)
        {
            case ADDR_CONFIG_PIPE_INTERLEAVE_256B:
                m_pipeInterleaveBytes = ADDR_PIPEINTERLEAVE_256B;
                m_pipeInterleaveLog2 = 8;
                break;
            case ADDR_CONFIG_PIPE_INTERLEAVE_512B:
                m_pipeInterleaveBytes = ADDR_PIPEINTERLEAVE_512B;
                m_pipeInterleaveLog2 = 9;
                break;
            case ADDR_CONFIG_PIPE_INTERLEAVE_1KB:
                m_pipeInterleaveBytes = ADDR_PIPEINTERLEAVE_1KB;
                m_pipeInterleaveLog2 = 10;
                break;
            case ADDR_CONFIG_PIPE_INTERLEAVE_2KB:
                m_pipeInterleaveBytes = ADDR_PIPEINTERLEAVE_2KB;
                m_pipeInterleaveLog2 = 11;
                break;
            default:
                break;
        }

        switch (gbAddrConfig.bits.NUM_BANKS)
        {
            case ADDR_CONFIG_1_BANK:
                m_banks = 1;
                m_banksLog2 = 0;
                break;
            case ADDR_CONFIG_2_BANK:
                m_banks = 2;
                m_banksLog2 = 1;
                break;
            case ADDR_CONFIG_4_BANK:
                m_banks = 4;
                m_banksLog2 = 2;
                break;
            case ADDR_CONFIG_8_BANK:
                m_banks = 8;
                m_banksLog2 = 3;
                break;
            case ADDR_CONFIG_16_BANK:
                m_banks = 16;
                m_banksLog2 = 4;
                break;
            default:
                break;
        }

        switch (gbAddrConfig.bits.NUM_SHADER_ENGINES)
        {
            case ADDR_CONFIG_1_SHADER_ENGINE:
                m_se = 1;
                m_seLog2 = 0;
                break;
            case ADDR_CONFIG_2_SHADER_ENGINE:
                m_se = 2;
                m_seLog2 = 1;
                break;
            case ADDR_CONFIG_4_SHADER_ENGINE:
                m_se = 4;
                m_seLog2 = 2;
                break;
            case ADDR_CONFIG_8_SHADER_ENGINE:
                m_se = 8;
                m_seLog2 = 3;
                break;
            default:
                break;
        }

        switch (gbAddrConfig.bits.NUM_RB_PER_SE)
        {
            case ADDR_CONFIG_1_RB_PER_SHADER_ENGINE:
                m_rbPerSe = 1;
                m_rbPerSeLog2 = 0;
                break;
            case ADDR_CONFIG_2_RB_PER_SHADER_ENGINE:
                m_rbPerSe = 2;
                m_rbPerSeLog2 = 1;
                break;
            case ADDR_CONFIG_4_RB_PER_SHADER_ENGINE:
                m_rbPerSe = 4;
                m_rbPerSeLog2 = 2;
                break;
            default:
                break;
        }

        switch (gbAddrConfig.bits.MAX_COMPRESSED_FRAGS)
        {
            case ADDR_CONFIG_1_MAX_COMPRESSED_FRAGMENTS:
                m_maxCompFrag = 1;
                m_maxCompFragLog2 = 0;
                break;
            case ADDR_CONFIG_2_MAX_COMPRESSED_FRAGMENTS:
                m_maxCompFrag = 2;
                m_maxCompFragLog2 = 1;
                break;
            case ADDR_CONFIG_4_MAX_COMPRESSED_FRAGMENTS:
                m_maxCompFrag = 4;
                m_maxCompFragLog2 = 2;
                break;
            case ADDR_CONFIG_8_MAX_COMPRESSED_FRAGMENTS:
                m_maxCompFrag = 8;
                m_maxCompFragLog2 = 3;
                break;
            default:
                break;
        }

        m_blockVarSizeLog2 = pCreateIn->regValue.blockVarSizeLog2;
        ADDR_ASSERT((m_blockVarSizeLog2 == 0) ||
                    ((m_blockVarSizeLog2 >= 17u) && (m_blockVarSizeLog2 <= 20u)));
        m_blockVarSizeLog2 = Min(Max(17u, m_blockVarSizeLog2), 20u);
    }
    else
    {
        valid = FALSE;
        ADDR_NOT_IMPLEMENTED();
    }

    if (valid)
    {
        InitEquationTable();
    }

    return valid;
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlConvertChipFamily
*
*   @brief
*       Convert familyID defined in atiid.h to ChipFamily and set m_chipFamily/m_chipRevision
*   @return
*       ChipFamily
****************************************************************************************************
*/
ChipFamily Gfx9Lib::HwlConvertChipFamily(
    UINT_32 uChipFamily,        ///< [in] chip family defined in atiih.h
    UINT_32 uChipRevision)      ///< [in] chip revision defined in "asic_family"_id.h
{
    ChipFamily family = ADDR_CHIP_FAMILY_AI;

    switch (uChipFamily)
    {
        case FAMILY_AI:
            m_settings.isArcticIsland = 1;
            m_settings.isVega10    = ASICREV_IS_VEGA10_P(uChipRevision);

            if (m_settings.isVega10)
            {
                m_settings.isDce12  = 1;
            }

            // Bug ID DEGGIGX90-1056
            m_settings.metaBaseAlignFix = 1;
            break;

        default:
            ADDR_ASSERT(!"This should be a Fusion");
            break;
    }

    return family;
}

/**
****************************************************************************************************
*   Gfx9Lib::InitRbEquation
*
*   @brief
*       Init RB equation
*   @return
*       N/A
****************************************************************************************************
*/
VOID Gfx9Lib::GetRbEquation(
    CoordEq* pRbEq,             ///< [out] rb equation
    UINT_32  numRbPerSeLog2,    ///< [in] number of rb per shader engine
    UINT_32  numSeLog2)         ///< [in] number of shader engine
{
    // RB's are distributed on 16x16, except when we have 1 rb per se, in which case its 32x32
    UINT_32 rbRegion = (numRbPerSeLog2 == 0) ? 5 : 4;
    Coordinate cx('x', rbRegion);
    Coordinate cy('y', rbRegion);

    UINT_32 start = 0;
    UINT_32 numRbTotalLog2 = numRbPerSeLog2 + numSeLog2;

    // Clear the rb equation
    pRbEq->resize(0);
    pRbEq->resize(numRbTotalLog2);

    if ((numSeLog2 > 0) && (numRbPerSeLog2 == 1))
    {
        // Special case when more than 1 SE, and 2 RB per SE
        (*pRbEq)[0].add(cx);
        (*pRbEq)[0].add(cy);
        cx++;
        cy++;
        (*pRbEq)[0].add(cy);
        start++;
    }

    UINT_32 numBits = 2 * (numRbTotalLog2 - start);

    for (UINT_32 i = 0; i < numBits; i++)
    {
        UINT_32 idx =
            start + (((start + i) >= numRbTotalLog2) ? (2 * (numRbTotalLog2 - start) - i - 1) : i);

        if ((i % 2) == 1)
        {
            (*pRbEq)[idx].add(cx);
            cx++;
        }
        else
        {
            (*pRbEq)[idx].add(cy);
            cy++;
        }
    }
}

/**
****************************************************************************************************
*   Gfx9Lib::GetDataEquation
*
*   @brief
*       Get data equation for fmask and Z
*   @return
*       N/A
****************************************************************************************************
*/
VOID Gfx9Lib::GetDataEquation(
    CoordEq* pDataEq,               ///< [out] data surface equation
    Gfx9DataType dataSurfaceType,   ///< [in] data surface type
    AddrSwizzleMode swizzleMode,    ///< [in] data surface swizzle mode
    AddrResourceType resourceType,  ///< [in] data surface resource type
    UINT_32 elementBytesLog2,       ///< [in] data surface element bytes
    UINT_32 numSamplesLog2)         ///< [in] data surface sample count
    const
{
    Coordinate cx('x', 0);
    Coordinate cy('y', 0);
    Coordinate cz('z', 0);
    Coordinate cs('s', 0);

    // Clear the equation
    pDataEq->resize(0);
    pDataEq->resize(27);

    if (dataSurfaceType == Gfx9DataColor)
    {
        if (IsLinear(swizzleMode))
        {
            Coordinate cm('m', 0);

            pDataEq->resize(49);

            for (UINT_32 i = 0; i < 49; i++)
            {
                (*pDataEq)[i].add(cm);
                cm++;
            }
        }
        else if (IsThick(resourceType, swizzleMode))
        {
            // Color 3d_S and 3d_Z modes, 3d_D is same as color 2d
            UINT_32 i;
            if (IsStandardSwizzle(resourceType, swizzleMode))
            {
                // Standard 3d swizzle
                // Fill in bottom x bits
                for (i = elementBytesLog2; i < 4; i++)
                {
                    (*pDataEq)[i].add(cx);
                    cx++;
                }
                // Fill in 2 bits of y and then z
                for (i = 4; i < 6; i++)
                {
                    (*pDataEq)[i].add(cy);
                    cy++;
                }
                for (i = 6; i < 8; i++)
                {
                    (*pDataEq)[i].add(cz);
                    cz++;
                }
                if (elementBytesLog2 < 2)
                {
                    // fill in z & y bit
                    (*pDataEq)[8].add(cz);
                    (*pDataEq)[9].add(cy);
                    cz++;
                    cy++;
                }
                else if (elementBytesLog2 == 2)
                {
                    // fill in y and x bit
                    (*pDataEq)[8].add(cy);
                    (*pDataEq)[9].add(cx);
                    cy++;
                    cx++;
                }
                else
                {
                    // fill in 2 x bits
                    (*pDataEq)[8].add(cx);
                    cx++;
                    (*pDataEq)[9].add(cx);
                    cx++;
                }
            }
            else
            {
                // Z 3d swizzle
                UINT_32 m2dEnd = (elementBytesLog2 ==0) ? 3 : ((elementBytesLog2 < 4) ? 4 : 5);
                UINT_32 numZs = (elementBytesLog2 == 0 || elementBytesLog2 == 4) ?
                                2 : ((elementBytesLog2 == 1) ? 3 : 1);
                pDataEq->mort2d(cx, cy, elementBytesLog2, m2dEnd);
                for (i = m2dEnd + 1; i <= m2dEnd + numZs; i++)
                {
                    (*pDataEq)[i].add(cz);
                    cz++;
                }
                if ((elementBytesLog2 == 0) || (elementBytesLog2 == 3))
                {
                    // add an x and z
                    (*pDataEq)[6].add(cx);
                    (*pDataEq)[7].add(cz);
                    cx++;
                    cz++;
                }
                else if (elementBytesLog2 == 2)
                {
                    // add a y and z
                    (*pDataEq)[6].add(cy);
                    (*pDataEq)[7].add(cz);
                    cy++;
                    cz++;
                }
                // add y and x
                (*pDataEq)[8].add(cy);
                (*pDataEq)[9].add(cx);
                cy++;
                cx++;
            }
            // Fill in bit 10 and up
            pDataEq->mort3d( cz, cy, cx, 10 );
        }
        else if (IsThin(resourceType, swizzleMode))
        {
            UINT_32 blockSizeLog2 = GetBlockSizeLog2(swizzleMode);
            // Color 2D
            UINT_32 microYBits = (8 - elementBytesLog2) / 2;
            UINT_32 tileSplitStart = blockSizeLog2 - numSamplesLog2;
            UINT_32 i;
            // Fill in bottom x bits
            for (i = elementBytesLog2; i < 4; i++)
            {
                (*pDataEq)[i].add(cx);
                cx++;
            }
            // Fill in bottom y bits
            for (i = 4; i < 4 + microYBits; i++)
            {
                (*pDataEq)[i].add(cy);
                cy++;
            }
            // Fill in last of the micro_x bits
            for (i = 4 + microYBits; i < 8; i++)
            {
                (*pDataEq)[i].add(cx);
                cx++;
            }
            // Fill in x/y bits below sample split
            pDataEq->mort2d(cy, cx, 8, tileSplitStart - 1);
            // Fill in sample bits
            for (i = 0; i < numSamplesLog2; i++)
            {
                cs.set('s', i);
                (*pDataEq)[tileSplitStart + i].add(cs);
            }
            // Fill in x/y bits above sample split
            if ((numSamplesLog2 & 1) ^ (blockSizeLog2 & 1))
            {
                pDataEq->mort2d(cx, cy, blockSizeLog2);
            }
            else
            {
                pDataEq->mort2d(cy, cx, blockSizeLog2);
            }
        }
        else
        {
            ADDR_ASSERT_ALWAYS();
        }
    }
    else
    {
        // Fmask or depth
        UINT_32 sampleStart = elementBytesLog2;
        UINT_32 pixelStart = elementBytesLog2 + numSamplesLog2;
        UINT_32 ymajStart = 6 + numSamplesLog2;

        for (UINT_32 s = 0; s < numSamplesLog2; s++)
        {
            cs.set('s', s);
            (*pDataEq)[sampleStart + s].add(cs);
        }

        // Put in the x-major order pixel bits
        pDataEq->mort2d(cx, cy, pixelStart, ymajStart - 1);
        // Put in the y-major order pixel bits
        pDataEq->mort2d(cy, cx, ymajStart);
    }
}

/**
****************************************************************************************************
*   Gfx9Lib::GetPipeEquation
*
*   @brief
*       Get pipe equation
*   @return
*       N/A
****************************************************************************************************
*/
VOID Gfx9Lib::GetPipeEquation(
    CoordEq*         pPipeEq,            ///< [out] pipe equation
    CoordEq*         pDataEq,            ///< [in] data equation
    UINT_32          pipeInterleaveLog2, ///< [in] pipe interleave
    UINT_32          numPipeLog2,        ///< [in] number of pipes
    UINT_32          numSamplesLog2,     ///< [in] data surface sample count
    Gfx9DataType     dataSurfaceType,    ///< [in] data surface type
    AddrSwizzleMode  swizzleMode,        ///< [in] data surface swizzle mode
    AddrResourceType resourceType        ///< [in] data surface resource type
    ) const
{
    UINT_32 blockSizeLog2 = GetBlockSizeLog2(swizzleMode);
    CoordEq dataEq;

    pDataEq->copy(dataEq);

    if (dataSurfaceType == Gfx9DataColor)
    {
        INT_32 shift = static_cast<INT_32>(numSamplesLog2);
        dataEq.shift(-shift, blockSizeLog2 - numSamplesLog2);
    }

    dataEq.copy(*pPipeEq, pipeInterleaveLog2, numPipeLog2);

    // This section should only apply to z/stencil, maybe fmask
    // If the pipe bit is below the comp block size,
    // then keep moving up the address until we find a bit that is above
    UINT_32 pipeStart = 0;

    if (dataSurfaceType != Gfx9DataColor)
    {
        Coordinate tileMin('x', 3);

        while (dataEq[pipeInterleaveLog2 + pipeStart][0] < tileMin)
        {
            pipeStart++;
        }

        // if pipe is 0, then the first pipe bit is above the comp block size,
        // so we don't need to do anything
        // Note, this if condition is not necessary, since if we execute the loop when pipe==0,
        // we will get the same pipe equation
        if (pipeStart != 0)
        {
            for (UINT_32 i = 0; i < numPipeLog2; i++)
            {
                // Copy the jth bit above pipe interleave to the current pipe equation bit
                dataEq[pipeInterleaveLog2 + pipeStart + i].copyto((*pPipeEq)[i]);
            }
        }
    }

    if (IsPrt(swizzleMode))
    {
        // Clear out bits above the block size if prt's are enabled
        dataEq.resize(blockSizeLog2);
        dataEq.resize(48);
    }

    if (IsXor(swizzleMode))
    {
        CoordEq xorMask;

        if (IsThick(resourceType, swizzleMode))
        {
            CoordEq xorMask2;

            dataEq.copy(xorMask2, pipeInterleaveLog2 + numPipeLog2, 2 * numPipeLog2);

            xorMask.resize(numPipeLog2);

            for (UINT_32 pipeIdx = 0; pipeIdx < numPipeLog2; pipeIdx++)
            {
                xorMask[pipeIdx].add(xorMask2[2 * pipeIdx]);
                xorMask[pipeIdx].add(xorMask2[2 * pipeIdx + 1]);
            }
        }
        else
        {
            // Xor in the bits above the pipe+gpu bits
            dataEq.copy(xorMask, pipeInterleaveLog2 + pipeStart + numPipeLog2, numPipeLog2);

            if ((numSamplesLog2 == 0) && (IsPrt(swizzleMode) == FALSE))
            {
                Coordinate co;
                CoordEq xorMask2;
                // if 1xaa and not prt, then xor in the z bits
                xorMask2.resize(0);
                xorMask2.resize(numPipeLog2);
                for (UINT_32 pipeIdx = 0; pipeIdx < numPipeLog2; pipeIdx++)
                {
                    co.set('z', numPipeLog2 - 1 - pipeIdx);
                    xorMask2[pipeIdx].add(co);
                }

                pPipeEq->xorin(xorMask2);
            }
        }

        xorMask.reverse();
        pPipeEq->xorin(xorMask);
    }
}

/**
****************************************************************************************************
*   Gfx9Lib::GetMetaEquation
*
*   @brief
*       Get meta equation for cmask/htile/DCC
*   @return
*       N/A
****************************************************************************************************
*/
VOID Gfx9Lib::GetMetaEquation(
    CoordEq* pMetaEq,               ///< [out] meta equation
    UINT_32 maxMip,                 ///< [in] max mip Id
    UINT_32 elementBytesLog2,       ///< [in] data surface element bytes
    UINT_32 numSamplesLog2,         ///< [in] data surface sample count
    ADDR2_META_FLAGS metaFlag,      ///< [in] meta falg
    Gfx9DataType dataSurfaceType,   ///< [in] data surface type
    AddrSwizzleMode swizzleMode,    ///< [in] data surface swizzle mode
    AddrResourceType resourceType,  ///< [in] data surface resource type
    UINT_32 metaBlkWidthLog2,       ///< [in] meta block width
    UINT_32 metaBlkHeightLog2,      ///< [in] meta block height
    UINT_32 metaBlkDepthLog2,       ///< [in] meta block depth
    UINT_32 compBlkWidthLog2,       ///< [in] compress block width
    UINT_32 compBlkHeightLog2,      ///< [in] compress block height
    UINT_32 compBlkDepthLog2)       ///< [in] compress block depth
    const
{
    UINT_32 numPipeTotalLog2 = GetPipeLog2ForMetaAddressing(metaFlag.pipeAligned, swizzleMode);
    UINT_32 pipeInterleaveLog2 = m_pipeInterleaveLog2;
    //UINT_32 blockSizeLog2 = GetBlockSizeLog2(swizzleMode);

    // Get the correct data address and rb equation
    CoordEq dataEq;
    GetDataEquation(&dataEq, dataSurfaceType, swizzleMode, resourceType,
                    elementBytesLog2, numSamplesLog2);

    // Get pipe and rb equations
    CoordEq pipeEquation;
    GetPipeEquation(&pipeEquation, &dataEq, pipeInterleaveLog2, numPipeTotalLog2,
                    numSamplesLog2, dataSurfaceType, swizzleMode, resourceType);
    numPipeTotalLog2 = pipeEquation.getsize();

    if (metaFlag.linear)
    {
        // Linear metadata supporting was removed for GFX9! No one can use this feature.
        ADDR_ASSERT_ALWAYS();

        ADDR_ASSERT(dataSurfaceType == Gfx9DataColor);

        dataEq.copy(*pMetaEq);

        if (IsLinear(swizzleMode))
        {
            if (metaFlag.pipeAligned)
            {
                // Remove the pipe bits
                INT_32 shift = static_cast<INT_32>(numPipeTotalLog2);
                pMetaEq->shift(-shift, pipeInterleaveLog2);
            }
            // Divide by comp block size, which for linear (which is always color) is 256 B
            pMetaEq->shift(-8);

            if (metaFlag.pipeAligned)
            {
                // Put pipe bits back in
                pMetaEq->shift(numPipeTotalLog2, pipeInterleaveLog2);

                for (UINT_32 i = 0; i < numPipeTotalLog2; i++)
                {
                    pipeEquation[i].copyto((*pMetaEq)[pipeInterleaveLog2 + i]);
                }
            }
        }

        pMetaEq->shift(1);
    }
    else
    {
        UINT_32 maxCompFragLog2 = static_cast<INT_32>(m_maxCompFragLog2);
        UINT_32 compFragLog2 =
            ((dataSurfaceType == Gfx9DataColor) && (numSamplesLog2 > maxCompFragLog2)) ?
            maxCompFragLog2 : numSamplesLog2;

        UINT_32 uncompFragLog2 = numSamplesLog2 - compFragLog2;

        // Make sure the metaaddr is cleared
        pMetaEq->resize(0);
        pMetaEq->resize(27);

        if (IsThick(resourceType, swizzleMode))
        {
            Coordinate cx('x', 0);
            Coordinate cy('y', 0);
            Coordinate cz('z', 0);

            if (maxMip > 0)
            {
                pMetaEq->mort3d(cy, cx, cz);
            }
            else
            {
                pMetaEq->mort3d(cx, cy, cz);
            }
        }
        else
        {
            Coordinate cx('x', 0);
            Coordinate cy('y', 0);
            Coordinate cs;

            if (maxMip > 0)
            {
                pMetaEq->mort2d(cy, cx, compFragLog2);
            }
            else
            {
                pMetaEq->mort2d(cx, cy, compFragLog2);
            }

            //------------------------------------------------------------------------------------------------------------------------
            // Put the compressible fragments at the lsb
            // the uncompressible frags will be at the msb of the micro address
            //------------------------------------------------------------------------------------------------------------------------
            for (UINT_32 s = 0; s < compFragLog2; s++)
            {
                cs.set('s', s);
                (*pMetaEq)[s].add(cs);
            }
        }

        // Keep a copy of the pipe equations
        CoordEq origPipeEquation;
        pipeEquation.copy(origPipeEquation);

        Coordinate co;
        // filter out everything under the compressed block size
        co.set('x', compBlkWidthLog2);
        pMetaEq->Filter('<', co, 0, 'x');
        co.set('y', compBlkHeightLog2);
        pMetaEq->Filter('<', co, 0, 'y');
        co.set('z', compBlkDepthLog2);
        pMetaEq->Filter('<', co, 0, 'z');

        // For non-color, filter out sample bits
        if (dataSurfaceType != Gfx9DataColor)
        {
            co.set('x', 0);
            pMetaEq->Filter('<', co, 0, 's');
        }

        // filter out everything above the metablock size
        co.set('x', metaBlkWidthLog2 - 1);
        pMetaEq->Filter('>', co, 0, 'x');
        co.set('y', metaBlkHeightLog2 - 1);
        pMetaEq->Filter('>', co, 0, 'y');
        co.set('z', metaBlkDepthLog2 - 1);
        pMetaEq->Filter('>', co, 0, 'z');

        // filter out everything above the metablock size for the channel bits
        co.set('x', metaBlkWidthLog2 - 1);
        pipeEquation.Filter('>', co, 0, 'x');
        co.set('y', metaBlkHeightLog2 - 1);
        pipeEquation.Filter('>', co, 0, 'y');
        co.set('z', metaBlkDepthLog2 - 1);
        pipeEquation.Filter('>', co, 0, 'z');

        // Make sure we still have the same number of channel bits
        if (pipeEquation.getsize() != numPipeTotalLog2)
        {
            ADDR_ASSERT_ALWAYS();
        }

        // Loop through all channel and rb bits,
        // and make sure these components exist in the metadata address
        for (UINT_32 i = 0; i < numPipeTotalLog2; i++)
        {
            for (UINT_32 j = pipeEquation[i].getsize(); j > 0; j--)
            {
                if (pMetaEq->Exists(pipeEquation[i][j - 1]) == FALSE)
                {
                    ADDR_ASSERT_ALWAYS();
                }
            }
        }

        UINT_32 numSeLog2 = metaFlag.rbAligned ? m_seLog2 : 0;
        UINT_32 numRbPeSeLog2 = metaFlag.rbAligned ? m_rbPerSeLog2 : 0;
        CoordEq origRbEquation;

        GetRbEquation(&origRbEquation, numRbPeSeLog2, numSeLog2);

        CoordEq rbEquation = origRbEquation;

        UINT_32 numRbTotalLog2 = numRbPeSeLog2 + numSeLog2;

        for (UINT_32 i = 0; i < numRbTotalLog2; i++)
        {
            for (UINT_32 j = rbEquation[i].getsize(); j > 0; j--)
            {
                if (pMetaEq->Exists(rbEquation[i][j - 1]) == FALSE)
                {
                    ADDR_ASSERT_ALWAYS();
                }
            }
        }

        // Loop through each rb id bit; if it is equal to any of the filtered channel bits, clear it
        for (UINT_32 i = 0; i < numRbTotalLog2; i++)
        {
            for (UINT_32 j = 0; j < numPipeTotalLog2; j++)
            {
                if (rbEquation[i] == pipeEquation[j])
                {
                    rbEquation[i].Clear();
                }
            }
        }

        // Loop through each bit of the channel, get the smallest coordinate,
        // and remove it from the metaaddr, and rb_equation
        for (UINT_32 i = 0; i < numPipeTotalLog2; i++)
        {
            pipeEquation[i].getsmallest(co);

            UINT_32 old_size = pMetaEq->getsize();
            pMetaEq->Filter('=', co);
            UINT_32 new_size = pMetaEq->getsize();
            if (new_size != old_size-1)
            {
                ADDR_ASSERT_ALWAYS();
            }
            pipeEquation.remove(co);
            for (UINT_32 j = 0; j < numRbTotalLog2; j++)
            {
                if (rbEquation[j].remove(co))
                {
                    // if we actually removed something from this bit, then add the remaining
                    // channel bits, as these can be removed for this bit
                    for (UINT_32 k = 0; k < pipeEquation[i].getsize(); k++)
                    {
                        if (pipeEquation[i][k] != co)
                        {
                            rbEquation[j].add(pipeEquation[i][k]);
                        }
                    }
                }
            }
        }

        // Loop through the rb bits and see what remain;
        // filter out the smallest coordinate if it remains
        UINT_32 rbBitsLeft = 0;
        for (UINT_32 i = 0; i < numRbTotalLog2; i++)
        {
            if (rbEquation[i].getsize() > 0)
            {
                rbBitsLeft++;
                rbEquation[i].getsmallest(co);
                UINT_32 old_size = pMetaEq->getsize();
                pMetaEq->Filter('=', co);
                UINT_32 new_size = pMetaEq->getsize();
                if (new_size != old_size - 1)
                {
                    // assert warning
                }
                for (UINT_32 j = i + 1; j < numRbTotalLog2; j++)
                {
                    if (rbEquation[j].remove(co))
                    {
                        // if we actually removed something from this bit, then add the remaining
                        // rb bits, as these can be removed for this bit
                        for (UINT_32 k = 0; k < rbEquation[i].getsize(); k++)
                        {
                            if (rbEquation[i][k] != co)
                            {
                                rbEquation[j].add(rbEquation[i][k]);
                            }
                        }
                    }
                }
            }
        }

        // capture the size of the metaaddr
        UINT_32 metaSize = pMetaEq->getsize();
        // resize to 49 bits...make this a nibble address
        pMetaEq->resize(49);
        // Concatenate the macro address above the current address
        for (UINT_32 i = metaSize, j = 0; i < 49; i++, j++)
        {
            co.set('m', j);
            (*pMetaEq)[i].add(co);
        }

        // Multiply by meta element size (in nibbles)
        if (dataSurfaceType == Gfx9DataColor)
        {
            pMetaEq->shift(1);
        }
        else if (dataSurfaceType == Gfx9DataDepthStencil)
        {
            pMetaEq->shift(3);
        }

        //------------------------------------------------------------------------------------------
        // Note the pipeInterleaveLog2+1 is because address is a nibble address
        // Shift up from pipe interleave number of channel
        // and rb bits left, and uncompressed fragments
        //------------------------------------------------------------------------------------------

        pMetaEq->shift(numPipeTotalLog2 + rbBitsLeft + uncompFragLog2, pipeInterleaveLog2 + 1);

        // Put in the channel bits
        for (UINT_32 i = 0; i < numPipeTotalLog2; i++)
        {
            origPipeEquation[i].copyto((*pMetaEq)[pipeInterleaveLog2+1 + i]);
        }

        // Put in remaining rb bits
        for (UINT_32 i = 0, j = 0; j < rbBitsLeft; i = (i + 1) % numRbTotalLog2)
        {
            if (rbEquation[i].getsize() > 0)
            {
                origRbEquation[i].copyto((*pMetaEq)[pipeInterleaveLog2 + 1 + numPipeTotalLog2 + j]);
                // Mark any rb bit we add in to the rb mask
                j++;
            }
        }

        //------------------------------------------------------------------------------------------
        // Put in the uncompressed fragment bits
        //------------------------------------------------------------------------------------------
        for (UINT_32 i = 0; i < uncompFragLog2; i++)
        {
            co.set('s', compFragLog2 + i);
            (*pMetaEq)[pipeInterleaveLog2 + 1 + numPipeTotalLog2 + rbBitsLeft + i].add(co);
        }
    }
}

/**
****************************************************************************************************
*   Gfx9Lib::IsEquationSupported
*
*   @brief
*       Check if equation is supported for given swizzle mode and resource type.
*
*   @return
*       TRUE if supported
****************************************************************************************************
*/
BOOL_32 Gfx9Lib::IsEquationSupported(
    AddrResourceType rsrcType,
    AddrSwizzleMode  swMode,
    UINT_32          elementBytesLog2) const
{
    BOOL_32 supported = (elementBytesLog2 < MaxElementBytesLog2) &&
                        (IsLinear(swMode) == FALSE) &&
                        ((IsTex2d(rsrcType) == TRUE) ||
                         ((IsTex3d(rsrcType) == TRUE) &&
                          (IsRotateSwizzle(swMode) == FALSE) &&
                          (IsBlock256b(swMode) == FALSE)));

    return supported;
}

/**
****************************************************************************************************
*   Gfx9Lib::InitEquationTable
*
*   @brief
*       Initialize Equation table.
*
*   @return
*       N/A
****************************************************************************************************
*/
VOID Gfx9Lib::InitEquationTable()
{
    memset(m_equationTable, 0, sizeof(m_equationTable));

    // Loop all possible resource type (2D/3D)
    for (UINT_32 rsrcTypeIdx = 0; rsrcTypeIdx < MaxRsrcType; rsrcTypeIdx++)
    {
        AddrResourceType rsrcType = static_cast<AddrResourceType>(rsrcTypeIdx + ADDR_RSRC_TEX_2D);

        // Loop all possible swizzle mode
        for (UINT_32 swModeIdx = 0; swModeIdx < MaxSwMode; swModeIdx++)
        {
            AddrSwizzleMode swMode = static_cast<AddrSwizzleMode>(swModeIdx);

            // Loop all possible bpp
            for (UINT_32 bppIdx = 0; bppIdx < MaxElementBytesLog2; bppIdx++)
            {
                UINT_32 equationIndex = ADDR_INVALID_EQUATION_INDEX;

                // Check if the input is supported
                if (IsEquationSupported(rsrcType, swMode, bppIdx))
                {
                    ADDR_EQUATION equation;
                    ADDR_E_RETURNCODE retCode;

                    memset(&equation, 0, sizeof(ADDR_EQUATION));

                    // Generate the equation
                    if (IsBlock256b(swMode) && IsTex2d(rsrcType))
                    {
                        retCode = ComputeBlock256Equation(rsrcType, swMode, bppIdx, &equation);
                    }
                    else if (IsThin(rsrcType, swMode))
                    {
                        retCode = ComputeThinEquation(rsrcType, swMode, bppIdx, &equation);
                    }
                    else
                    {
                        retCode = ComputeThickEquation(rsrcType, swMode, bppIdx, &equation);
                    }

                    // Only fill the equation into the table if the return code is ADDR_OK,
                    // otherwise if the return code is not ADDR_OK, it indicates this is not
                    // a valid input, we do nothing but just fill invalid equation index
                    // into the lookup table.
                    if (retCode == ADDR_OK)
                    {
                        equationIndex = m_numEquations;
                        ADDR_ASSERT(equationIndex < EquationTableSize);

                        m_equationTable[equationIndex] = equation;

                        m_numEquations++;
                    }
                }

                // Fill the index into the lookup table, if the combination is not supported
                // fill the invalid equation index
                m_equationLookupTable[rsrcTypeIdx][swModeIdx][bppIdx] = equationIndex;
            }
        }
    }
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlGetEquationIndex
*
*   @brief
*       Interface function stub of GetEquationIndex
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
UINT_32 Gfx9Lib::HwlGetEquationIndex(
    const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,
    ADDR2_COMPUTE_SURFACE_INFO_OUTPUT*      pOut
    ) const
{
    AddrResourceType rsrcType = pIn->resourceType;
    AddrSwizzleMode swMode = pIn->swizzleMode;
    UINT_32 elementBytesLog2 = Log2(pIn->bpp >> 3);
    UINT_32 numMipLevels = pIn->numMipLevels;
    ADDR2_MIP_INFO* pMipInfo = pOut->pMipInfo;

    UINT_32 index = ADDR_INVALID_EQUATION_INDEX;

    BOOL_32 eqSupported = (pOut->firstMipInTail == FALSE) &&
                          IsEquationSupported(rsrcType, swMode, elementBytesLog2);

    UINT_32 rsrcTypeIdx = static_cast<UINT_32>(rsrcType) - 1;
    UINT_32 swModeIdx = static_cast<UINT_32>(swMode);

    if (eqSupported)
    {
        index = m_equationLookupTable[rsrcTypeIdx][swModeIdx][elementBytesLog2];

        if (pMipInfo != NULL)
        {
            pMipInfo->equationIndex = index;
            pMipInfo->mipOffsetXBytes = 0;
            pMipInfo->mipOffsetYPixel = 0;
            pMipInfo->mipOffsetZPixel = 0;
            pMipInfo->postSwizzleOffset = 0;

            /*static const UINT_32 Prt_Xor_Gap =
                static_cast<UINT_32>(ADDR_SW_64KB_Z_T) - static_cast<UINT_32>(ADDR_SW_64KB_Z);*/

            for (UINT_32 i = 1; i < numMipLevels; i++)
            {
                Dim3d mipStartPos = {0};
                UINT_32 mipTailOffset = 0;

                mipStartPos = GetMipStartPos(rsrcType,
                                             swMode,
                                             pOut->pitch,
                                             pOut->height,
                                             pOut->numSlices,
                                             pOut->blockWidth,
                                             pOut->blockHeight,
                                             pOut->blockSlices,
                                             i,
                                             &mipTailOffset);

                UINT_32 mipSwModeIdx = swModeIdx;

                pMipInfo[i].equationIndex =
                    m_equationLookupTable[rsrcTypeIdx][mipSwModeIdx][elementBytesLog2];
                pMipInfo[i].mipOffsetXBytes = mipStartPos.w * pOut->blockWidth * (pOut->bpp >> 3);
                pMipInfo[i].mipOffsetYPixel = mipStartPos.h * pOut->blockHeight;
                pMipInfo[i].mipOffsetZPixel = mipStartPos.d * pOut->blockSlices;
                pMipInfo[i].postSwizzleOffset = mipTailOffset;
            }
        }
    }
    else if (pMipInfo != NULL)
    {
        for (UINT_32 i = 0; i < numMipLevels; i++)
        {
            pMipInfo[i].equationIndex = ADDR_INVALID_EQUATION_INDEX;
            pMipInfo[i].mipOffsetXBytes = 0;
            pMipInfo[i].mipOffsetYPixel = 0;
            pMipInfo[i].mipOffsetZPixel = 0;
            pMipInfo[i].postSwizzleOffset = 0;
        }
    }

    return index;
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlComputeBlock256Equation
*
*   @brief
*       Interface function stub of ComputeBlock256Equation
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeBlock256Equation(
    AddrResourceType rsrcType,
    AddrSwizzleMode swMode,
    UINT_32 elementBytesLog2,
    ADDR_EQUATION* pEquation) const
{
    ADDR_E_RETURNCODE ret = ADDR_OK;

    pEquation->numBits = 8;

    UINT_32 i = 0;
    for (; i < elementBytesLog2; i++)
    {
        InitChannel(1, 0 , i, &pEquation->addr[i]);
    }

    ADDR_CHANNEL_SETTING* pixelBit = &pEquation->addr[elementBytesLog2];

    const UINT_32 MaxBitsUsed = 4;
    ADDR_CHANNEL_SETTING x[MaxBitsUsed] = {};
    ADDR_CHANNEL_SETTING y[MaxBitsUsed] = {};

    for (i = 0; i < MaxBitsUsed; i++)
    {
        InitChannel(1, 0, elementBytesLog2 + i, &x[i]);
        InitChannel(1, 1, i, &y[i]);
    }

    if (IsStandardSwizzle(rsrcType, swMode))
    {
        switch (elementBytesLog2)
        {
            case 0:
                pixelBit[0] = x[0];
                pixelBit[1] = x[1];
                pixelBit[2] = x[2];
                pixelBit[3] = x[3];
                pixelBit[4] = y[0];
                pixelBit[5] = y[1];
                pixelBit[6] = y[2];
                pixelBit[7] = y[3];
                break;
            case 1:
                pixelBit[0] = x[0];
                pixelBit[1] = x[1];
                pixelBit[2] = x[2];
                pixelBit[3] = y[0];
                pixelBit[4] = y[1];
                pixelBit[5] = y[2];
                pixelBit[6] = x[3];
                break;
            case 2:
                pixelBit[0] = x[0];
                pixelBit[1] = x[1];
                pixelBit[2] = y[0];
                pixelBit[3] = y[1];
                pixelBit[4] = y[2];
                pixelBit[5] = x[2];
                break;
            case 3:
                pixelBit[0] = x[0];
                pixelBit[1] = y[0];
                pixelBit[2] = y[1];
                pixelBit[3] = x[1];
                pixelBit[4] = x[2];
                break;
            case 4:
                pixelBit[0] = y[0];
                pixelBit[1] = y[1];
                pixelBit[2] = x[0];
                pixelBit[3] = x[1];
                break;
            default:
                ADDR_ASSERT_ALWAYS();
                ret = ADDR_INVALIDPARAMS;
                break;
        }
    }
    else if (IsDisplaySwizzle(rsrcType, swMode))
    {
        switch (elementBytesLog2)
        {
            case 0:
                pixelBit[0] = x[0];
                pixelBit[1] = x[1];
                pixelBit[2] = x[2];
                pixelBit[3] = y[1];
                pixelBit[4] = y[0];
                pixelBit[5] = y[2];
                pixelBit[6] = x[3];
                pixelBit[7] = y[3];
                break;
            case 1:
                pixelBit[0] = x[0];
                pixelBit[1] = x[1];
                pixelBit[2] = x[2];
                pixelBit[3] = y[0];
                pixelBit[4] = y[1];
                pixelBit[5] = y[2];
                pixelBit[6] = x[3];
                break;
            case 2:
                pixelBit[0] = x[0];
                pixelBit[1] = x[1];
                pixelBit[2] = y[0];
                pixelBit[3] = x[2];
                pixelBit[4] = y[1];
                pixelBit[5] = y[2];
                break;
            case 3:
                pixelBit[0] = x[0];
                pixelBit[1] = y[0];
                pixelBit[2] = x[1];
                pixelBit[3] = x[2];
                pixelBit[4] = y[1];
                break;
            case 4:
                pixelBit[0] = x[0];
                pixelBit[1] = y[0];
                pixelBit[2] = x[1];
                pixelBit[3] = y[1];
                break;
            default:
                ADDR_ASSERT_ALWAYS();
                ret = ADDR_INVALIDPARAMS;
                break;
        }
    }
    else if (IsRotateSwizzle(swMode))
    {
        switch (elementBytesLog2)
        {
            case 0:
                pixelBit[0] = y[0];
                pixelBit[1] = y[1];
                pixelBit[2] = y[2];
                pixelBit[3] = x[1];
                pixelBit[4] = x[0];
                pixelBit[5] = x[2];
                pixelBit[6] = x[3];
                pixelBit[7] = y[3];
                break;
            case 1:
                pixelBit[0] = y[0];
                pixelBit[1] = y[1];
                pixelBit[2] = y[2];
                pixelBit[3] = x[0];
                pixelBit[4] = x[1];
                pixelBit[5] = x[2];
                pixelBit[6] = x[3];
                break;
            case 2:
                pixelBit[0] = y[0];
                pixelBit[1] = y[1];
                pixelBit[2] = x[0];
                pixelBit[3] = y[2];
                pixelBit[4] = x[1];
                pixelBit[5] = x[2];
                break;
            case 3:
                pixelBit[0] = y[0];
                pixelBit[1] = x[0];
                pixelBit[2] = y[1];
                pixelBit[3] = x[1];
                pixelBit[4] = x[2];
                break;
            default:
                ADDR_ASSERT_ALWAYS();
            case 4:
                ret = ADDR_INVALIDPARAMS;
                break;
        }
    }
    else
    {
        ADDR_ASSERT_ALWAYS();
        ret = ADDR_INVALIDPARAMS;
    }

    // Post validation
    if (ret == ADDR_OK)
    {
        Dim2d microBlockDim = Block256b[elementBytesLog2];
        ADDR_ASSERT((2u << GetMaxValidChannelIndex(pEquation->addr, 8, 0)) ==
                    (microBlockDim.w * (1 << elementBytesLog2)));
        ADDR_ASSERT((2u << GetMaxValidChannelIndex(pEquation->addr, 8, 1)) == microBlockDim.h);
    }

    return ret;
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlComputeThinEquation
*
*   @brief
*       Interface function stub of ComputeThinEquation
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeThinEquation(
    AddrResourceType rsrcType,
    AddrSwizzleMode swMode,
    UINT_32 elementBytesLog2,
    ADDR_EQUATION* pEquation) const
{
    ADDR_E_RETURNCODE ret = ADDR_OK;

    UINT_32 blockSizeLog2 = GetBlockSizeLog2(swMode);

    UINT_32 maxXorBits = blockSizeLog2;
    if (IsNonPrtXor(swMode))
    {
        // For non-prt-xor, maybe need to initialize some more bits for xor
        // The highest xor bit used in equation will be max the following 3 items:
        // 1. m_pipeInterleaveLog2 + 2 * pipeXorBits
        // 2. m_pipeInterleaveLog2 + pipeXorBits + 2 * bankXorBits
        // 3. blockSizeLog2

        maxXorBits = Max(maxXorBits, m_pipeInterleaveLog2 + 2 * GetPipeXorBits(blockSizeLog2));
        maxXorBits = Max(maxXorBits, m_pipeInterleaveLog2 +
                                     GetPipeXorBits(blockSizeLog2) +
                                     2 * GetBankXorBits(blockSizeLog2));
    }

    const UINT_32 MaxBitsUsed = 14;
    ADDR_ASSERT((2 * MaxBitsUsed) >= maxXorBits);
    ADDR_CHANNEL_SETTING x[MaxBitsUsed] = {};
    ADDR_CHANNEL_SETTING y[MaxBitsUsed] = {};

    const UINT_32 ExtraXorBits = 16;
    ADDR_ASSERT(ExtraXorBits >= maxXorBits - blockSizeLog2);
    ADDR_CHANNEL_SETTING xorExtra[ExtraXorBits] = {};

    for (UINT_32 i = 0; i < MaxBitsUsed; i++)
    {
        InitChannel(1, 0, elementBytesLog2 + i, &x[i]);
        InitChannel(1, 1, i, &y[i]);
    }

    ADDR_CHANNEL_SETTING* pixelBit = pEquation->addr;

    for (UINT_32 i = 0; i < elementBytesLog2; i++)
    {
        InitChannel(1, 0 , i, &pixelBit[i]);
    }

    UINT_32 xIdx = 0;
    UINT_32 yIdx = 0;
    UINT_32 lowBits = 0;

    if (IsZOrderSwizzle(swMode))
    {
        if (elementBytesLog2 <= 3)
        {
            for (UINT_32 i = elementBytesLog2; i < 6; i++)
            {
                pixelBit[i] = (((i - elementBytesLog2) & 1) == 0) ? x[xIdx++] : y[yIdx++];
            }

            lowBits = 6;
        }
        else
        {
            ret = ADDR_INVALIDPARAMS;
        }
    }
    else
    {
        ret = HwlComputeBlock256Equation(rsrcType, swMode, elementBytesLog2, pEquation);
        if (ret == ADDR_OK)
        {
            Dim2d microBlockDim = Block256b[elementBytesLog2];
            xIdx = Log2(microBlockDim.w);
            yIdx = Log2(microBlockDim.h);
            lowBits = 8;
        }
    }

    if (ret == ADDR_OK)
    {
        for (UINT_32 i = lowBits; i < blockSizeLog2; i++)
        {
            pixelBit[i] = ((i & 1) == 0) ? y[yIdx++] : x[xIdx++];
        }

        for (UINT_32 i = blockSizeLog2; i < maxXorBits; i++)
        {
            xorExtra[i - blockSizeLog2] = ((i & 1) == 0) ? y[yIdx++] : x[xIdx++];
        }
    }

    if ((ret == ADDR_OK) && IsXor(swMode))
    {
        // Fill XOR bits
        UINT_32 pipeStart = m_pipeInterleaveLog2;
        UINT_32 pipeXorBits = GetPipeXorBits(blockSizeLog2);
        for (UINT_32 i = 0; i < pipeXorBits; i++)
        {
            UINT_32 xor1BitPos = pipeStart + 2 * pipeXorBits - 1 - i;
            ADDR_CHANNEL_SETTING* pXor1Src =
                (xor1BitPos < blockSizeLog2) ?
                &pEquation->addr[xor1BitPos] : &xorExtra[xor1BitPos - blockSizeLog2];

            InitChannel(&pEquation->xor1[pipeStart + i], pXor1Src);
        }

        UINT_32 bankStart = pipeStart + pipeXorBits;
        UINT_32 bankXorBits = GetBankXorBits(blockSizeLog2);
        for (UINT_32 i = 0; i < bankXorBits; i++)
        {
            UINT_32 xor1BitPos = bankStart + 2 * bankXorBits - 1 - i;
            ADDR_CHANNEL_SETTING* pXor1Src =
                (xor1BitPos < blockSizeLog2) ?
                &pEquation->addr[xor1BitPos] : &xorExtra[xor1BitPos - blockSizeLog2];

            InitChannel(&pEquation->xor1[pipeStart + i], pXor1Src);
        }

        pEquation->numBits = blockSizeLog2;
    }

    if ((ret == ADDR_OK) && IsTex3d(rsrcType))
    {
        pEquation->stackedDepthSlices = TRUE;
    }

    return ret;
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlComputeThickEquation
*
*   @brief
*       Interface function stub of ComputeThickEquation
*
*   @return
*       ADDR_E_RETURNCODE
****************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeThickEquation(
    AddrResourceType rsrcType,
    AddrSwizzleMode swMode,
    UINT_32 elementBytesLog2,
    ADDR_EQUATION* pEquation) const
{
    ADDR_E_RETURNCODE ret = ADDR_OK;

    ADDR_ASSERT(IsTex3d(rsrcType));

    UINT_32 blockSizeLog2 = GetBlockSizeLog2(swMode);

    UINT_32 maxXorBits = blockSizeLog2;
    if (IsNonPrtXor(swMode))
    {
        // For non-prt-xor, maybe need to initialize some more bits for xor
        // The highest xor bit used in equation will be max the following 3:
        // 1. m_pipeInterleaveLog2 + 3 * pipeXorBits
        // 2. m_pipeInterleaveLog2 + pipeXorBits + 3 * bankXorBits
        // 3. blockSizeLog2

        maxXorBits = Max(maxXorBits, m_pipeInterleaveLog2 + 3 * GetPipeXorBits(blockSizeLog2));
        maxXorBits = Max(maxXorBits, m_pipeInterleaveLog2 +
                                     GetPipeXorBits(blockSizeLog2) +
                                     3 * GetBankXorBits(blockSizeLog2));
    }

    for (UINT_32 i = 0; i < elementBytesLog2; i++)
    {
        InitChannel(1, 0 , i, &pEquation->addr[i]);
    }

    ADDR_CHANNEL_SETTING* pixelBit = &pEquation->addr[elementBytesLog2];

    const UINT_32 MaxBitsUsed = 12;
    ADDR_ASSERT((3 * MaxBitsUsed) >= maxXorBits);
    ADDR_CHANNEL_SETTING x[MaxBitsUsed] = {};
    ADDR_CHANNEL_SETTING y[MaxBitsUsed] = {};
    ADDR_CHANNEL_SETTING z[MaxBitsUsed] = {};

    const UINT_32 ExtraXorBits = 24;
    ADDR_ASSERT(ExtraXorBits >= maxXorBits - blockSizeLog2);
    ADDR_CHANNEL_SETTING xorExtra[ExtraXorBits] = {};

    for (UINT_32 i = 0; i < MaxBitsUsed; i++)
    {
        InitChannel(1, 0, elementBytesLog2 + i, &x[i]);
        InitChannel(1, 1, i, &y[i]);
        InitChannel(1, 2, i, &z[i]);
    }

    if (IsZOrderSwizzle(swMode))
    {
        switch (elementBytesLog2)
        {
            case 0:
                pixelBit[0]  = x[0];
                pixelBit[1]  = y[0];
                pixelBit[2]  = x[1];
                pixelBit[3]  = y[1];
                pixelBit[4]  = z[0];
                pixelBit[5]  = z[1];
                pixelBit[6]  = x[2];
                pixelBit[7]  = z[2];
                pixelBit[8]  = y[2];
                pixelBit[9]  = x[3];
                break;
            case 1:
                pixelBit[0]  = x[0];
                pixelBit[1]  = y[0];
                pixelBit[2]  = x[1];
                pixelBit[3]  = y[1];
                pixelBit[4]  = z[0];
                pixelBit[5]  = z[1];
                pixelBit[6]  = z[2];
                pixelBit[7]  = y[2];
                pixelBit[8]  = x[2];
                break;
            case 2:
                pixelBit[0]  = x[0];
                pixelBit[1]  = y[0];
                pixelBit[2]  = x[1];
                pixelBit[3]  = z[0];
                pixelBit[4]  = y[1];
                pixelBit[5]  = z[1];
                pixelBit[6]  = y[2];
                pixelBit[7]  = x[2];
                break;
            case 3:
                pixelBit[0]  = x[0];
                pixelBit[1]  = y[0];
                pixelBit[2]  = z[0];
                pixelBit[3]  = x[1];
                pixelBit[4]  = z[1];
                pixelBit[5]  = y[1];
                pixelBit[6]  = x[2];
                break;
            case 4:
                pixelBit[0]  = x[0];
                pixelBit[1]  = y[0];
                pixelBit[2]  = z[0];
                pixelBit[3]  = z[1];
                pixelBit[4]  = y[1];
                pixelBit[5]  = x[1];
                break;
            default:
                ADDR_ASSERT_ALWAYS();
                ret = ADDR_INVALIDPARAMS;
                break;
        }
    }
    else if (IsStandardSwizzle(rsrcType, swMode))
    {
        switch (elementBytesLog2)
        {
            case 0:
                pixelBit[0]  = x[0];
                pixelBit[1]  = x[1];
                pixelBit[2]  = x[2];
                pixelBit[3]  = x[3];
                pixelBit[4]  = y[0];
                pixelBit[5]  = y[1];
                pixelBit[6]  = z[0];
                pixelBit[7]  = z[1];
                pixelBit[8]  = z[2];
                pixelBit[9]  = y[2];
                break;
            case 1:
                pixelBit[0]  = x[0];
                pixelBit[1]  = x[1];
                pixelBit[2]  = x[2];
                pixelBit[3]  = y[0];
                pixelBit[4]  = y[1];
                pixelBit[5]  = z[0];
                pixelBit[6]  = z[1];
                pixelBit[7]  = z[2];
                pixelBit[8]  = y[2];
                break;
            case 2:
                pixelBit[0]  = x[0];
                pixelBit[1]  = x[1];
                pixelBit[2]  = y[0];
                pixelBit[3]  = y[1];
                pixelBit[4]  = z[0];
                pixelBit[5]  = z[1];
                pixelBit[6]  = y[2];
                pixelBit[7]  = x[2];
                break;
            case 3:
                pixelBit[0]  = x[0];
                pixelBit[1]  = y[0];
                pixelBit[2]  = y[1];
                pixelBit[3]  = z[0];
                pixelBit[4]  = z[1];
                pixelBit[5]  = x[1];
                pixelBit[6]  = x[2];
                break;
            case 4:
                pixelBit[0]  = y[0];
                pixelBit[1]  = y[1];
                pixelBit[2]  = z[0];
                pixelBit[3]  = z[1];
                pixelBit[4]  = x[0];
                pixelBit[5]  = x[1];
                break;
            default:
                ADDR_ASSERT_ALWAYS();
                ret = ADDR_INVALIDPARAMS;
                break;
        }
    }
    else
    {
        ADDR_ASSERT_ALWAYS();
        ret = ADDR_INVALIDPARAMS;
    }

    if (ret == ADDR_OK)
    {
        Dim3d microBlockDim = Block1kb[elementBytesLog2];
        UINT_32 xIdx = Log2(microBlockDim.w);
        UINT_32 yIdx = Log2(microBlockDim.h);
        UINT_32 zIdx = Log2(microBlockDim.d);

        pixelBit = pEquation->addr;

        static const UINT_32 lowBits = 10;
        ADDR_ASSERT(pEquation->addr[lowBits - 1].valid == 1);
        ADDR_ASSERT(pEquation->addr[lowBits].valid == 0);

        for (UINT_32 i = lowBits; i < blockSizeLog2; i++)
        {
            if (((i - lowBits) % 3) == 0)
            {
                pixelBit[i] = x[xIdx++];
            }
            else if (((i - lowBits) % 3) == 1)
            {
                pixelBit[i] = z[zIdx++];
            }
            else
            {
                pixelBit[i] = y[yIdx++];
            }
        }

        for (UINT_32 i = blockSizeLog2; i < maxXorBits; i++)
        {
            if (((i - lowBits) % 3) == 0)
            {
                xorExtra[i - blockSizeLog2] = x[xIdx++];
            }
            else if (((i - lowBits) % 3) == 1)
            {
                xorExtra[i - blockSizeLog2] = z[zIdx++];
            }
            else
            {
                xorExtra[i - blockSizeLog2] = y[yIdx++];
            }
        }
    }

    if ((ret == ADDR_OK) && IsXor(swMode))
    {
        // Fill XOR bits
        UINT_32 pipeStart = m_pipeInterleaveLog2;
        UINT_32 pipeXorBits = GetPipeXorBits(blockSizeLog2);
        for (UINT_32 i = 0; i < pipeXorBits; i++)
        {
            UINT_32 xor1BitPos = pipeStart + (3 * pipeXorBits) - 1 - (2 * i);
            ADDR_CHANNEL_SETTING* pXor1Src =
                (xor1BitPos < blockSizeLog2) ?
                &pEquation->addr[xor1BitPos] : &xorExtra[xor1BitPos - blockSizeLog2];

            InitChannel(&pEquation->xor1[pipeStart + i], pXor1Src);

            UINT_32 xor2BitPos = pipeStart + (3 * pipeXorBits) - 2 - (2 * i);
            ADDR_CHANNEL_SETTING* pXor2Src =
                (xor2BitPos < blockSizeLog2) ?
                &pEquation->addr[xor2BitPos] : &xorExtra[xor2BitPos - blockSizeLog2];

            InitChannel(&pEquation->xor2[pipeStart + i], pXor2Src);
        }

        UINT_32 bankStart = pipeStart + pipeXorBits;
        UINT_32 bankXorBits = GetBankXorBits(blockSizeLog2);
        for (UINT_32 i = 0; i < bankXorBits; i++)
        {
            UINT_32 xor1BitPos = bankStart + (3 * bankXorBits) - 1 - (2 * i);
            ADDR_CHANNEL_SETTING* pXor1Src =
                (xor1BitPos < blockSizeLog2) ?
                &pEquation->addr[xor1BitPos] : &xorExtra[xor1BitPos - blockSizeLog2];

            InitChannel(&pEquation->xor1[bankStart + i], pXor1Src);

            UINT_32 xor2BitPos = bankStart + (3 * bankXorBits) - 2 - (2 * i);
            ADDR_CHANNEL_SETTING* pXor2Src =
                (xor2BitPos < blockSizeLog2) ?
                &pEquation->addr[xor2BitPos] : &xorExtra[xor2BitPos - blockSizeLog2];

            InitChannel(&pEquation->xor2[bankStart + i], pXor2Src);
        }

        pEquation->numBits = blockSizeLog2;
    }

    return ret;
}

/**
****************************************************************************************************
*   Gfx9Lib::HwlIsValidDisplaySwizzleMode
*
*   @brief
*       Check if a swizzle mode is supported by display engine
*
*   @return
*       TRUE is swizzle mode is supported by display engine
****************************************************************************************************
*/
BOOL_32 Gfx9Lib::HwlIsValidDisplaySwizzleMode(const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn) const
{
    BOOL_32 support = FALSE;

    //const AddrResourceType resourceType = pIn->resourceType;
    const AddrSwizzleMode swizzleMode = pIn->swizzleMode;

    if (m_settings.isDce12)
    {
        switch (swizzleMode)
        {
            case ADDR_SW_256B_D:
            case ADDR_SW_256B_R:
                support = (pIn->bpp == 32);
                break;

            case ADDR_SW_LINEAR:
            case ADDR_SW_4KB_D:
            case ADDR_SW_4KB_R:
            case ADDR_SW_64KB_D:
            case ADDR_SW_64KB_R:
            case ADDR_SW_VAR_D:
            case ADDR_SW_VAR_R:
            case ADDR_SW_4KB_D_X:
            case ADDR_SW_4KB_R_X:
            case ADDR_SW_64KB_D_X:
            case ADDR_SW_64KB_R_X:
            case ADDR_SW_VAR_D_X:
            case ADDR_SW_VAR_R_X:
                support = (pIn->bpp <= 64);
                break;

            default:
                break;
        }
    }
    else
    {
        ADDR_NOT_IMPLEMENTED();
    }

    return support;
}

} // V2
} // Addr
