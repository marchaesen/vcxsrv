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
************************************************************************************************************************
* @file  gfx9addrlib.cpp
* @brief Contgfx9ns the implementation for the Gfx9Lib class.
************************************************************************************************************************
*/

#include "gfx9addrlib.h"

#include "gfx9_gb_reg.h"

#include "amdgpu_asic_addr.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Addr
{

/**
************************************************************************************************************************
*   Gfx9HwlInit
*
*   @brief
*       Creates an Gfx9Lib object.
*
*   @return
*       Returns an Gfx9Lib object pointer.
************************************************************************************************************************
*/
Addr::Lib* Gfx9HwlInit(const Client* pClient)
{
    return V2::Gfx9Lib::CreateObj(pClient);
}

namespace V2
{

////////////////////////////////////////////////////////////////////////////////////////////////////
//                               Static Const Member
////////////////////////////////////////////////////////////////////////////////////////////////////

const SwizzleModeFlags Gfx9Lib::SwizzleModeTable[ADDR_SW_MAX_TYPE] =
{//Linear 256B  4KB  64KB   Var    Z    Std   Disp  Rot   XOR    T     RtOpt
    {1,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0}, // ADDR_SW_LINEAR
    {0,    1,    0,    0,    0,    0,    1,    0,    0,    0,    0,    0}, // ADDR_SW_256B_S
    {0,    1,    0,    0,    0,    0,    0,    1,    0,    0,    0,    0}, // ADDR_SW_256B_D
    {0,    1,    0,    0,    0,    0,    0,    0,    1,    0,    0,    0}, // ADDR_SW_256B_R

    {0,    0,    1,    0,    0,    1,    0,    0,    0,    0,    0,    0}, // ADDR_SW_4KB_Z
    {0,    0,    1,    0,    0,    0,    1,    0,    0,    0,    0,    0}, // ADDR_SW_4KB_S
    {0,    0,    1,    0,    0,    0,    0,    1,    0,    0,    0,    0}, // ADDR_SW_4KB_D
    {0,    0,    1,    0,    0,    0,    0,    0,    1,    0,    0,    0}, // ADDR_SW_4KB_R

    {0,    0,    0,    1,    0,    1,    0,    0,    0,    0,    0,    0}, // ADDR_SW_64KB_Z
    {0,    0,    0,    1,    0,    0,    1,    0,    0,    0,    0,    0}, // ADDR_SW_64KB_S
    {0,    0,    0,    1,    0,    0,    0,    1,    0,    0,    0,    0}, // ADDR_SW_64KB_D
    {0,    0,    0,    1,    0,    0,    0,    0,    1,    0,    0,    0}, // ADDR_SW_64KB_R

    {0,    0,    0,    0,    1,    1,    0,    0,    0,    0,    0,    0}, // ADDR_SW_VAR_Z
    {0,    0,    0,    0,    1,    0,    1,    0,    0,    0,    0,    0}, // ADDR_SW_VAR_S
    {0,    0,    0,    0,    1,    0,    0,    1,    0,    0,    0,    0}, // ADDR_SW_VAR_D
    {0,    0,    0,    0,    1,    0,    0,    0,    1,    0,    0,    0}, // ADDR_SW_VAR_R

    {0,    0,    0,    1,    0,    1,    0,    0,    0,    1,    1,    0}, // ADDR_SW_64KB_Z_T
    {0,    0,    0,    1,    0,    0,    1,    0,    0,    1,    1,    0}, // ADDR_SW_64KB_S_T
    {0,    0,    0,    1,    0,    0,    0,    1,    0,    1,    1,    0}, // ADDR_SW_64KB_D_T
    {0,    0,    0,    1,    0,    0,    0,    0,    1,    1,    1,    0}, // ADDR_SW_64KB_R_T

    {0,    0,    1,    0,    0,    1,    0,    0,    0,    1,    0,    0}, // ADDR_SW_4KB_Z_x
    {0,    0,    1,    0,    0,    0,    1,    0,    0,    1,    0,    0}, // ADDR_SW_4KB_S_x
    {0,    0,    1,    0,    0,    0,    0,    1,    0,    1,    0,    0}, // ADDR_SW_4KB_D_x
    {0,    0,    1,    0,    0,    0,    0,    0,    1,    1,    0,    0}, // ADDR_SW_4KB_R_x

    {0,    0,    0,    1,    0,    1,    0,    0,    0,    1,    0,    0}, // ADDR_SW_64KB_Z_X
    {0,    0,    0,    1,    0,    0,    1,    0,    0,    1,    0,    0}, // ADDR_SW_64KB_S_X
    {0,    0,    0,    1,    0,    0,    0,    1,    0,    1,    0,    0}, // ADDR_SW_64KB_D_X
    {0,    0,    0,    1,    0,    0,    0,    0,    1,    1,    0,    0}, // ADDR_SW_64KB_R_X

    {0,    0,    0,    0,    1,    1,    0,    0,    0,    1,    0,    0}, // ADDR_SW_VAR_Z_X
    {0,    0,    0,    0,    1,    0,    1,    0,    0,    1,    0,    0}, // ADDR_SW_VAR_S_X
    {0,    0,    0,    0,    1,    0,    0,    1,    0,    1,    0,    0}, // ADDR_SW_VAR_D_X
    {0,    0,    0,    0,    1,    0,    0,    0,    1,    1,    0,    0}, // ADDR_SW_VAR_R_X
    {1,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0}, // ADDR_SW_LINEAR_GENERAL
};

const UINT_32 Gfx9Lib::MipTailOffset256B[] = {2048, 1024, 512, 256, 128, 64, 32, 16,
                                              8, 6, 5, 4, 3, 2, 1, 0};

const Dim3d   Gfx9Lib::Block256_3dS[]  = {{16, 4, 4}, {8, 4, 4}, {4, 4, 4}, {2, 4, 4}, {1, 4, 4}};

const Dim3d   Gfx9Lib::Block256_3dZ[]  = {{8, 4, 8}, {4, 4, 8}, {4, 4, 4}, {4, 2, 4}, {2, 2, 4}};

/**
************************************************************************************************************************
*   Gfx9Lib::Gfx9Lib
*
*   @brief
*       Constructor
*
************************************************************************************************************************
*/
Gfx9Lib::Gfx9Lib(const Client* pClient)
    :
    Lib(pClient),
    m_numEquations(0)
{
    m_class = AI_ADDRLIB;
    memset(&m_settings, 0, sizeof(m_settings));
    memcpy(m_swizzleModeTable, SwizzleModeTable, sizeof(SwizzleModeTable));
}

/**
************************************************************************************************************************
*   Gfx9Lib::~Gfx9Lib
*
*   @brief
*       Destructor
************************************************************************************************************************
*/
Gfx9Lib::~Gfx9Lib()
{
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeHtileInfo
*
*   @brief
*       Interface function stub of AddrComputeHtilenfo
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
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
        if (m_settings.applyAliasFix)
        {
            numCompressBlkPerMetaBlkLog2 = m_seLog2 + m_rbPerSeLog2 + Max(10u, m_pipeInterleaveLog2);
        }
        else
        {
            numCompressBlkPerMetaBlkLog2 = m_seLog2 + m_rbPerSeLog2 + 10;
        }
    }

    numCompressBlkPerMetaBlk = 1 << numCompressBlkPerMetaBlkLog2;

    Dim3d   metaBlkDim   = {8, 8, 1};
    UINT_32 totalAmpBits = numCompressBlkPerMetaBlkLog2;
    UINT_32 widthAmp     = (pIn->numMipLevels > 1) ? (totalAmpBits >> 1) : RoundHalf(totalAmpBits);
    UINT_32 heightAmp    = totalAmpBits - widthAmp;
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

    const UINT_32 metaBlkSize = numCompressBlkPerMetaBlk << 2;
    UINT_32       align       = numPipeTotal * numRbTotal * m_pipeInterleaveBytes;

    if ((IsXor(pIn->swizzleMode) == FALSE) && (numPipeTotal > 2))
    {
        align *= (numPipeTotal >> 1);
    }

    align = Max(align, metaBlkSize);

    if (m_settings.metaBaseAlignFix)
    {
        align = Max(align, GetBlockSize(pIn->swizzleMode));
    }

    if (m_settings.htileAlignFix)
    {
        const INT_32 metaBlkSizeLog2        = numCompressBlkPerMetaBlkLog2 + 2;
        const INT_32 htileCachelineSizeLog2 = 11;
        const INT_32 maxNumOfRbMaskBits     = 1 + Log2(numPipeTotal) + Log2(numRbTotal);

        INT_32 rbMaskPadding = Max(0, htileCachelineSizeLog2 - (metaBlkSizeLog2 - maxNumOfRbMaskBits));

        align <<= rbMaskPadding;
    }

    pOut->pitch      = numMetaBlkX * metaBlkDim.w;
    pOut->height     = numMetaBlkY * metaBlkDim.h;
    pOut->sliceSize  = numMetaBlkX * numMetaBlkY * metaBlkSize;

    pOut->metaBlkWidth       = metaBlkDim.w;
    pOut->metaBlkHeight      = metaBlkDim.h;
    pOut->metaBlkNumPerSlice = numMetaBlkX * numMetaBlkY;

    pOut->baseAlign  = align;
    pOut->htileBytes = PowTwoAlign(pOut->sliceSize * numMetaBlkZ, align);

    return ADDR_OK;
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeCmaskInfo
*
*   @brief
*       Interface function stub of AddrComputeCmaskInfo
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeCmaskInfo(
    const ADDR2_COMPUTE_CMASK_INFO_INPUT*    pIn,    ///< [in] input structure
    ADDR2_COMPUTE_CMASK_INFO_OUTPUT*         pOut    ///< [out] output structure
    ) const
{
// TODO: Clarify with AddrLib team
//     ADDR_ASSERT(pIn->resourceType == ADDR_RSRC_TEX_2D);

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
        if (m_settings.applyAliasFix)
        {
            numCompressBlkPerMetaBlkLog2 = m_seLog2 + m_rbPerSeLog2 + Max(10u, m_pipeInterleaveLog2);
        }
        else
        {
            numCompressBlkPerMetaBlkLog2 = m_seLog2 + m_rbPerSeLog2 + 10;
        }

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

    if (m_settings.metaBaseAlignFix)
    {
        sizeAlign = Max(sizeAlign, GetBlockSize(pIn->swizzleMode));
    }

    pOut->pitch      = numMetaBlkX * metaBlkDim.w;
    pOut->height     = numMetaBlkY * metaBlkDim.h;
    pOut->sliceSize  = (numMetaBlkX * numMetaBlkY * numCompressBlkPerMetaBlk) >> 1;
    pOut->cmaskBytes = PowTwoAlign(pOut->sliceSize * numMetaBlkZ, sizeAlign);
    pOut->baseAlign  = Max(numCompressBlkPerMetaBlk >> 1, sizeAlign);

    pOut->metaBlkWidth = metaBlkDim.w;
    pOut->metaBlkHeight = metaBlkDim.h;

    pOut->metaBlkNumPerSlice = numMetaBlkX * numMetaBlkY;

    return ADDR_OK;
}

/**
************************************************************************************************************************
*   Gfx9Lib::GetMetaMipInfo
*
*   @brief
*       Get meta mip info
*
*   @return
*       N/A
************************************************************************************************************************
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
************************************************************************************************************************
*   Gfx9Lib::HwlComputeDccInfo
*
*   @brief
*       Interface function to compute DCC key info
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
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

        UINT_32 numFrags = Max(pIn->numFrags, 1u);
        UINT_32 numSlices = Max(pIn->numSlices, 1u);

        minMetaBlkSize /= numFrags;

        UINT_32 numCompressBlkPerMetaBlk = minMetaBlkSize;

        UINT_32 numRbTotal = pIn->dccKeyFlags.rbAligned ? m_se * m_rbPerSe : 1;

        if ((numPipeTotal > 1) || (numRbTotal > 1))
        {
            const UINT_32 thinBlkSize = 1 << (m_settings.applyAliasFix ? Max(10u, m_pipeInterleaveLog2) : 10);

            numCompressBlkPerMetaBlk =
                Max(numCompressBlkPerMetaBlk, m_se * m_rbPerSe * (dataThick ? 262144 : thinBlkSize));

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

        if (m_settings.metaBaseAlignFix)
        {
            sizeAlign = Max(sizeAlign, GetBlockSize(pIn->swizzleMode));
        }

        pOut->dccRamSize = numMetaBlkX * numMetaBlkY * numMetaBlkZ *
                           numCompressBlkPerMetaBlk * numFrags;
        pOut->dccRamSize = PowTwoAlign(pOut->dccRamSize, sizeAlign);
        pOut->dccRamBaseAlign = Max(numCompressBlkPerMetaBlk, sizeAlign);

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
************************************************************************************************************************
*   Gfx9Lib::HwlComputeMaxBaseAlignments
*
*   @brief
*       Gets maximum alignments
*   @return
*       maximum alignments
************************************************************************************************************************
*/
UINT_32 Gfx9Lib::HwlComputeMaxBaseAlignments() const
{
    return ComputeSurfaceBaseAlignTiled(ADDR_SW_64KB);
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeMaxMetaBaseAlignments
*
*   @brief
*       Gets maximum alignments for metadata
*   @return
*       maximum alignments for metadata
************************************************************************************************************************
*/
UINT_32 Gfx9Lib::HwlComputeMaxMetaBaseAlignments() const
{
    // Max base alignment for Htile
    const UINT_32 maxNumPipeTotal = GetPipeNumForMetaAddressing(TRUE, ADDR_SW_64KB_Z);
    const UINT_32 maxNumRbTotal   = m_se * m_rbPerSe;

    // If applyAliasFix was set, the extra bits should be MAX(10u, m_pipeInterleaveLog2),
    // but we never saw any ASIC whose m_pipeInterleaveLog2 != 8, so just put an assertion and simply the logic.
    ADDR_ASSERT((m_settings.applyAliasFix == FALSE) || (m_pipeInterleaveLog2 <= 10u));
    const UINT_32 maxNumCompressBlkPerMetaBlk = 1u << (m_seLog2 + m_rbPerSeLog2 + 10u);

    UINT_32 maxBaseAlignHtile = maxNumPipeTotal * maxNumRbTotal * m_pipeInterleaveBytes;

    if (maxNumPipeTotal > 2)
    {
        maxBaseAlignHtile *= (maxNumPipeTotal >> 1);
    }

    maxBaseAlignHtile = Max(maxNumCompressBlkPerMetaBlk << 2, maxBaseAlignHtile);

    if (m_settings.metaBaseAlignFix)
    {
        maxBaseAlignHtile = Max(maxBaseAlignHtile, GetBlockSize(ADDR_SW_64KB));
    }

    if (m_settings.htileAlignFix)
    {
        maxBaseAlignHtile *= maxNumPipeTotal;
    }

    // Max base alignment for Cmask will not be larger than that for Htile, no need to calculate

    // Max base alignment for 2D Dcc will not be larger than that for 3D, no need to calculate
    UINT_32 maxBaseAlignDcc3D = 65536;

    if ((maxNumPipeTotal > 1) || (maxNumRbTotal > 1))
    {
        maxBaseAlignDcc3D = Min(m_se * m_rbPerSe * 262144, 65536 * 128u);
    }

    // Max base alignment for Msaa Dcc
    UINT_32 maxBaseAlignDccMsaa = maxNumPipeTotal * maxNumRbTotal * m_pipeInterleaveBytes * (8 / m_maxCompFrag);

    if (m_settings.metaBaseAlignFix)
    {
        maxBaseAlignDccMsaa = Max(maxBaseAlignDccMsaa, GetBlockSize(ADDR_SW_64KB));
    }

    return Max(maxBaseAlignHtile, Max(maxBaseAlignDccMsaa, maxBaseAlignDcc3D));
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeCmaskAddrFromCoord
*
*   @brief
*       Interface function stub of AddrComputeCmaskAddrFromCoord
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeCmaskAddrFromCoord(
    const ADDR2_COMPUTE_CMASK_ADDRFROMCOORD_INPUT*   pIn,    ///< [in] input structure
    ADDR2_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT*        pOut)   ///< [out] output structure
{
    ADDR2_COMPUTE_CMASK_INFO_INPUT input = {0};
    input.size            = sizeof(input);
    input.cMaskFlags      = pIn->cMaskFlags;
    input.colorFlags      = pIn->colorFlags;
    input.unalignedWidth  = Max(pIn->unalignedWidth, 1u);
    input.unalignedHeight = Max(pIn->unalignedHeight, 1u);
    input.numSlices       = Max(pIn->numSlices, 1u);
    input.swizzleMode     = pIn->swizzleMode;
    input.resourceType    = pIn->resourceType;

    ADDR2_COMPUTE_CMASK_INFO_OUTPUT output = {0};
    output.size = sizeof(output);

    ADDR_E_RETURNCODE returnCode = ComputeCmaskInfo(&input, &output);

    if (returnCode == ADDR_OK)
    {
        UINT_32 fmaskBpp              = GetFmaskBpp(pIn->numSamples, pIn->numFrags);
        UINT_32 fmaskElementBytesLog2 = Log2(fmaskBpp >> 3);
        UINT_32 metaBlkWidthLog2      = Log2(output.metaBlkWidth);
        UINT_32 metaBlkHeightLog2     = Log2(output.metaBlkHeight);

        MetaEqParams metaEqParams = {0, fmaskElementBytesLog2, 0, pIn->cMaskFlags,
                                     Gfx9DataFmask, pIn->swizzleMode, pIn->resourceType,
                                     metaBlkWidthLog2, metaBlkHeightLog2, 0, 3, 3, 0};

        const CoordEq* pMetaEq = GetMetaEquation(metaEqParams);

        UINT_32 xb = pIn->x / output.metaBlkWidth;
        UINT_32 yb = pIn->y / output.metaBlkHeight;
        UINT_32 zb = pIn->slice;

        UINT_32 pitchInBlock     = output.pitch / output.metaBlkWidth;
        UINT_32 sliceSizeInBlock = (output.height / output.metaBlkHeight) * pitchInBlock;
        UINT_32 blockIndex       = zb * sliceSizeInBlock + yb * pitchInBlock + xb;

        UINT_64 address = pMetaEq->solve(pIn->x, pIn->y, pIn->slice, 0, blockIndex);

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
************************************************************************************************************************
*   Gfx9Lib::HwlComputeHtileAddrFromCoord
*
*   @brief
*       Interface function stub of AddrComputeHtileAddrFromCoord
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeHtileAddrFromCoord(
    const ADDR2_COMPUTE_HTILE_ADDRFROMCOORD_INPUT*   pIn,    ///< [in] input structure
    ADDR2_COMPUTE_HTILE_ADDRFROMCOORD_OUTPUT*        pOut)   ///< [out] output structure
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (pIn->numMipLevels > 1)
    {
        returnCode = ADDR_NOTIMPLEMENTED;
    }
    else
    {
        ADDR2_COMPUTE_HTILE_INFO_INPUT input = {0};
        input.size            = sizeof(input);
        input.hTileFlags      = pIn->hTileFlags;
        input.depthFlags      = pIn->depthflags;
        input.swizzleMode     = pIn->swizzleMode;
        input.unalignedWidth  = Max(pIn->unalignedWidth, 1u);
        input.unalignedHeight = Max(pIn->unalignedHeight, 1u);
        input.numSlices       = Max(pIn->numSlices, 1u);
        input.numMipLevels    = Max(pIn->numMipLevels, 1u);

        ADDR2_COMPUTE_HTILE_INFO_OUTPUT output = {0};
        output.size = sizeof(output);

        returnCode = ComputeHtileInfo(&input, &output);

        if (returnCode == ADDR_OK)
        {
            UINT_32 elementBytesLog2  = Log2(pIn->bpp >> 3);
            UINT_32 metaBlkWidthLog2  = Log2(output.metaBlkWidth);
            UINT_32 metaBlkHeightLog2 = Log2(output.metaBlkHeight);
            UINT_32 numSamplesLog2    = Log2(pIn->numSamples);

            MetaEqParams metaEqParams = {0, elementBytesLog2, numSamplesLog2, pIn->hTileFlags,
                                         Gfx9DataDepthStencil, pIn->swizzleMode, ADDR_RSRC_TEX_2D,
                                         metaBlkWidthLog2, metaBlkHeightLog2, 0, 3, 3, 0};

            const CoordEq* pMetaEq = GetMetaEquation(metaEqParams);

            UINT_32 xb = pIn->x / output.metaBlkWidth;
            UINT_32 yb = pIn->y / output.metaBlkHeight;
            UINT_32 zb = pIn->slice;

            UINT_32 pitchInBlock     = output.pitch / output.metaBlkWidth;
            UINT_32 sliceSizeInBlock = (output.height / output.metaBlkHeight) * pitchInBlock;
            UINT_32 blockIndex       = zb * sliceSizeInBlock + yb * pitchInBlock + xb;

            UINT_64 address = pMetaEq->solve(pIn->x, pIn->y, pIn->slice, 0, blockIndex);

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
************************************************************************************************************************
*   Gfx9Lib::HwlComputeHtileCoordFromAddr
*
*   @brief
*       Interface function stub of AddrComputeHtileCoordFromAddr
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeHtileCoordFromAddr(
    const ADDR2_COMPUTE_HTILE_COORDFROMADDR_INPUT*   pIn,    ///< [in] input structure
    ADDR2_COMPUTE_HTILE_COORDFROMADDR_OUTPUT*        pOut)   ///< [out] output structure
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if (pIn->numMipLevels > 1)
    {
        returnCode = ADDR_NOTIMPLEMENTED;
    }
    else
    {
        ADDR2_COMPUTE_HTILE_INFO_INPUT input = {0};
        input.size            = sizeof(input);
        input.hTileFlags      = pIn->hTileFlags;
        input.swizzleMode     = pIn->swizzleMode;
        input.unalignedWidth  = Max(pIn->unalignedWidth, 1u);
        input.unalignedHeight = Max(pIn->unalignedHeight, 1u);
        input.numSlices       = Max(pIn->numSlices, 1u);
        input.numMipLevels    = Max(pIn->numMipLevels, 1u);

        ADDR2_COMPUTE_HTILE_INFO_OUTPUT output = {0};
        output.size = sizeof(output);

        returnCode = ComputeHtileInfo(&input, &output);

        if (returnCode == ADDR_OK)
        {
            UINT_32 elementBytesLog2  = Log2(pIn->bpp >> 3);
            UINT_32 metaBlkWidthLog2  = Log2(output.metaBlkWidth);
            UINT_32 metaBlkHeightLog2 = Log2(output.metaBlkHeight);
            UINT_32 numSamplesLog2    = Log2(pIn->numSamples);

            MetaEqParams metaEqParams = {0, elementBytesLog2, numSamplesLog2, pIn->hTileFlags,
                                         Gfx9DataDepthStencil, pIn->swizzleMode, ADDR_RSRC_TEX_2D,
                                         metaBlkWidthLog2, metaBlkHeightLog2, 0, 3, 3, 0};

            const CoordEq* pMetaEq = GetMetaEquation(metaEqParams);

            UINT_32 numPipeBits = GetPipeLog2ForMetaAddressing(pIn->hTileFlags.pipeAligned,
                                                               pIn->swizzleMode);

            UINT_64 pipeXor = static_cast<UINT_64>(pIn->pipeXor & ((1 << numPipeBits) - 1));

            UINT_64 nibbleAddress = (pIn->addr ^ (pipeXor << m_pipeInterleaveLog2)) << 1;

            UINT_32 pitchInBlock     = output.pitch / output.metaBlkWidth;
            UINT_32 sliceSizeInBlock = (output.height / output.metaBlkHeight) * pitchInBlock;

            UINT_32 x, y, z, s, m;
            pMetaEq->solveAddr(nibbleAddress, sliceSizeInBlock, x, y, z, s, m);

            pOut->slice = m / sliceSizeInBlock;
            pOut->y     = ((m % sliceSizeInBlock) / pitchInBlock) * output.metaBlkHeight + y;
            pOut->x     = (m % pitchInBlock) * output.metaBlkWidth + x;
        }
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeDccAddrFromCoord
*
*   @brief
*       Interface function stub of AddrComputeDccAddrFromCoord
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeDccAddrFromCoord(
    const ADDR2_COMPUTE_DCC_ADDRFROMCOORD_INPUT*  pIn,
    ADDR2_COMPUTE_DCC_ADDRFROMCOORD_OUTPUT* pOut)
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    if ((pIn->numMipLevels > 1) || (pIn->mipId > 1) || pIn->dccKeyFlags.linear)
    {
        returnCode = ADDR_NOTIMPLEMENTED;
    }
    else
    {
        ADDR2_COMPUTE_DCCINFO_INPUT input = {0};
        input.size            = sizeof(input);
        input.dccKeyFlags     = pIn->dccKeyFlags;
        input.colorFlags      = pIn->colorFlags;
        input.swizzleMode     = pIn->swizzleMode;
        input.resourceType    = pIn->resourceType;
        input.bpp             = pIn->bpp;
        input.unalignedWidth  = Max(pIn->unalignedWidth, 1u);
        input.unalignedHeight = Max(pIn->unalignedHeight, 1u);
        input.numSlices       = Max(pIn->numSlices, 1u);
        input.numFrags        = Max(pIn->numFrags, 1u);
        input.numMipLevels    = Max(pIn->numMipLevels, 1u);

        ADDR2_COMPUTE_DCCINFO_OUTPUT output = {0};
        output.size = sizeof(output);

        returnCode = ComputeDccInfo(&input, &output);

        if (returnCode == ADDR_OK)
        {
            UINT_32 elementBytesLog2  = Log2(pIn->bpp >> 3);
            UINT_32 numSamplesLog2    = Log2(pIn->numFrags);
            UINT_32 metaBlkWidthLog2  = Log2(output.metaBlkWidth);
            UINT_32 metaBlkHeightLog2 = Log2(output.metaBlkHeight);
            UINT_32 metaBlkDepthLog2  = Log2(output.metaBlkDepth);
            UINT_32 compBlkWidthLog2  = Log2(output.compressBlkWidth);
            UINT_32 compBlkHeightLog2 = Log2(output.compressBlkHeight);
            UINT_32 compBlkDepthLog2  = Log2(output.compressBlkDepth);

            MetaEqParams metaEqParams = {pIn->mipId, elementBytesLog2, numSamplesLog2, pIn->dccKeyFlags,
                                         Gfx9DataColor, pIn->swizzleMode, pIn->resourceType,
                                         metaBlkWidthLog2, metaBlkHeightLog2, metaBlkDepthLog2,
                                         compBlkWidthLog2, compBlkHeightLog2, compBlkDepthLog2};

            const CoordEq* pMetaEq = GetMetaEquation(metaEqParams);

            UINT_32 xb = pIn->x / output.metaBlkWidth;
            UINT_32 yb = pIn->y / output.metaBlkHeight;
            UINT_32 zb = pIn->slice / output.metaBlkDepth;

            UINT_32 pitchInBlock     = output.pitch / output.metaBlkWidth;
            UINT_32 sliceSizeInBlock = (output.height / output.metaBlkHeight) * pitchInBlock;
            UINT_32 blockIndex       = zb * sliceSizeInBlock + yb * pitchInBlock + xb;

            UINT_64 address = pMetaEq->solve(pIn->x, pIn->y, pIn->slice, pIn->sample, blockIndex);

            pOut->addr = address >> 1;

            UINT_32 numPipeBits = GetPipeLog2ForMetaAddressing(pIn->dccKeyFlags.pipeAligned,
                                                               pIn->swizzleMode);

            UINT_64 pipeXor = static_cast<UINT_64>(pIn->pipeXor & ((1 << numPipeBits) - 1));

            pOut->addr ^= (pipeXor << m_pipeInterleaveLog2);
        }
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlInitGlobalParams
*
*   @brief
*       Initializes global parameters
*
*   @return
*       TRUE if all settings are valid
*
************************************************************************************************************************
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
                ADDR_ASSERT_ALWAYS();
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
                ADDR_ASSERT_ALWAYS();
                break;
        }

        // Addr::V2::Lib::ComputePipeBankXor()/ComputeSlicePipeBankXor() requires pipe interleave to be exactly 8 bits,
        // and any larger value requires a post-process (left shift) on the output pipeBankXor bits.
        ADDR_ASSERT(m_pipeInterleaveBytes == ADDR_PIPEINTERLEAVE_256B);

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
                ADDR_ASSERT_ALWAYS();
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
                ADDR_ASSERT_ALWAYS();
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
                ADDR_ASSERT_ALWAYS();
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
                ADDR_ASSERT_ALWAYS();
                break;
        }

        m_blockVarSizeLog2 = pCreateIn->regValue.blockVarSizeLog2;
        ADDR_ASSERT((m_blockVarSizeLog2 == 0) ||
                    ((m_blockVarSizeLog2 >= 17u) && (m_blockVarSizeLog2 <= 20u)));
        m_blockVarSizeLog2 = Min(Max(17u, m_blockVarSizeLog2), 20u);

        if ((m_rbPerSeLog2 == 1) &&
            (((m_pipesLog2 == 1) && ((m_seLog2 == 2) || (m_seLog2 == 3))) ||
             ((m_pipesLog2 == 2) && ((m_seLog2 == 1) || (m_seLog2 == 2)))))
        {
            ADDR_ASSERT(m_settings.isVega10 == FALSE);
            ADDR_ASSERT(m_settings.isRaven == FALSE);

            if (m_settings.isVega12)
            {
                m_settings.htileCacheRbConflict = 1;
            }
        }
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
************************************************************************************************************************
*   Gfx9Lib::HwlConvertChipFamily
*
*   @brief
*       Convert familyID defined in atiid.h to ChipFamily and set m_chipFamily/m_chipRevision
*   @return
*       ChipFamily
************************************************************************************************************************
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
            m_settings.isVega12    = ASICREV_IS_VEGA12_P(uChipRevision);

            m_settings.isDce12 = 1;

            if (m_settings.isVega10 == 0)
            {
                m_settings.htileAlignFix = 1;
                m_settings.applyAliasFix = 1;
            }

            m_settings.metaBaseAlignFix = 1;

            m_settings.depthPipeXorDisable = 1;
            break;
        case FAMILY_RV:
            m_settings.isArcticIsland = 1;
            m_settings.isRaven        = ASICREV_IS_RAVEN(uChipRevision);

            if (m_settings.isRaven)
            {
                m_settings.isDcn1   = 1;
            }

            m_settings.metaBaseAlignFix = 1;

            if (ASICREV_IS_RAVEN(uChipRevision))
            {
                m_settings.depthPipeXorDisable = 1;
            }
            break;

        default:
            ADDR_ASSERT(!"This should be a Fusion");
            break;
    }

    return family;
}

/**
************************************************************************************************************************
*   Gfx9Lib::InitRbEquation
*
*   @brief
*       Init RB equation
*   @return
*       N/A
************************************************************************************************************************
*/
VOID Gfx9Lib::GetRbEquation(
    CoordEq* pRbEq,             ///< [out] rb equation
    UINT_32  numRbPerSeLog2,    ///< [in] number of rb per shader engine
    UINT_32  numSeLog2)         ///< [in] number of shader engine
    const
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

        if (m_settings.applyAliasFix == false)
        {
            (*pRbEq)[0].add(cy);
        }

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
************************************************************************************************************************
*   Gfx9Lib::GetDataEquation
*
*   @brief
*       Get data equation for fmask and Z
*   @return
*       N/A
************************************************************************************************************************
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
************************************************************************************************************************
*   Gfx9Lib::GetPipeEquation
*
*   @brief
*       Get pipe equation
*   @return
*       N/A
************************************************************************************************************************
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
************************************************************************************************************************
*   Gfx9Lib::GetMetaEquation
*
*   @brief
*       Get meta equation for cmask/htile/DCC
*   @return
*       Pointer to a calculated meta equation
************************************************************************************************************************
*/
const CoordEq* Gfx9Lib::GetMetaEquation(
    const MetaEqParams& metaEqParams)
{
    UINT_32 cachedMetaEqIndex;

    for (cachedMetaEqIndex = 0; cachedMetaEqIndex < MaxCachedMetaEq; cachedMetaEqIndex++)
    {
        if (memcmp(&metaEqParams,
                   &m_cachedMetaEqKey[cachedMetaEqIndex],
                   static_cast<UINT_32>(sizeof(metaEqParams))) == 0)
        {
            break;
        }
    }

    CoordEq* pMetaEq = NULL;

    if (cachedMetaEqIndex < MaxCachedMetaEq)
    {
        pMetaEq = &m_cachedMetaEq[cachedMetaEqIndex];
    }
    else
    {
        m_cachedMetaEqKey[m_metaEqOverrideIndex] = metaEqParams;

        pMetaEq = &m_cachedMetaEq[m_metaEqOverrideIndex++];

        m_metaEqOverrideIndex %= MaxCachedMetaEq;

        GenMetaEquation(pMetaEq,
                        metaEqParams.maxMip,
                        metaEqParams.elementBytesLog2,
                        metaEqParams.numSamplesLog2,
                        metaEqParams.metaFlag,
                        metaEqParams.dataSurfaceType,
                        metaEqParams.swizzleMode,
                        metaEqParams.resourceType,
                        metaEqParams.metaBlkWidthLog2,
                        metaEqParams.metaBlkHeightLog2,
                        metaEqParams.metaBlkDepthLog2,
                        metaEqParams.compBlkWidthLog2,
                        metaEqParams.compBlkHeightLog2,
                        metaEqParams.compBlkDepthLog2);
    }

    return pMetaEq;
}

/**
************************************************************************************************************************
*   Gfx9Lib::GenMetaEquation
*
*   @brief
*       Get meta equation for cmask/htile/DCC
*   @return
*       N/A
************************************************************************************************************************
*/
VOID Gfx9Lib::GenMetaEquation(
    CoordEq*         pMetaEq,               ///< [out] meta equation
    UINT_32          maxMip,                ///< [in] max mip Id
    UINT_32          elementBytesLog2,      ///< [in] data surface element bytes
    UINT_32          numSamplesLog2,        ///< [in] data surface sample count
    ADDR2_META_FLAGS metaFlag,              ///< [in] meta falg
    Gfx9DataType     dataSurfaceType,       ///< [in] data surface type
    AddrSwizzleMode  swizzleMode,           ///< [in] data surface swizzle mode
    AddrResourceType resourceType,          ///< [in] data surface resource type
    UINT_32          metaBlkWidthLog2,      ///< [in] meta block width
    UINT_32          metaBlkHeightLog2,     ///< [in] meta block height
    UINT_32          metaBlkDepthLog2,      ///< [in] meta block depth
    UINT_32          compBlkWidthLog2,      ///< [in] compress block width
    UINT_32          compBlkHeightLog2,     ///< [in] compress block height
    UINT_32          compBlkDepthLog2)      ///< [in] compress block depth
    const
{
    UINT_32 numPipeTotalLog2   = GetPipeLog2ForMetaAddressing(metaFlag.pipeAligned, swizzleMode);
    UINT_32 pipeInterleaveLog2 = m_pipeInterleaveLog2;

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

        const UINT_32 numSeLog2     = metaFlag.rbAligned ? m_seLog2      : 0;
        const UINT_32 numRbPeSeLog2 = metaFlag.rbAligned ? m_rbPerSeLog2 : 0;
        const UINT_32 numRbTotalLog2 = numRbPeSeLog2 + numSeLog2;
        CoordEq       origRbEquation;

        GetRbEquation(&origRbEquation, numRbPeSeLog2, numSeLog2);

        CoordEq rbEquation = origRbEquation;

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

        if (m_settings.applyAliasFix)
        {
            co.set('z', -1);
        }

        // Loop through each rb id bit; if it is equal to any of the filtered channel bits, clear it
        for (UINT_32 i = 0; i < numRbTotalLog2; i++)
        {
            for (UINT_32 j = 0; j < numPipeTotalLog2; j++)
            {
                BOOL_32 isRbEquationInPipeEquation = FALSE;

                if (m_settings.applyAliasFix)
                {
                    CoordTerm filteredPipeEq;
                    filteredPipeEq = pipeEquation[j];

                    filteredPipeEq.Filter('>', co, 0, 'z');

                    isRbEquationInPipeEquation = (rbEquation[i] == filteredPipeEq);
                }
                else
                {
                    isRbEquationInPipeEquation = (rbEquation[i] == pipeEquation[j]);
                }

                if (isRbEquationInPipeEquation)
                {
                    rbEquation[i].Clear();
                }
            }
        }

         bool rbAppendedWithPipeBits[1 << (MaxSeLog2 + MaxRbPerSeLog2)] = {};

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
                            rbAppendedWithPipeBits[j] = true;
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
            BOOL_32 isRbEqAppended = FALSE;

            if (m_settings.applyAliasFix)
            {
                isRbEqAppended = (rbEquation[i].getsize() > (rbAppendedWithPipeBits[i] ? 1 : 0));
            }
            else
            {
                isRbEqAppended = (rbEquation[i].getsize() > 0);
            }

            if (isRbEqAppended)
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
                                rbAppendedWithPipeBits[j] |= rbAppendedWithPipeBits[i];
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
            BOOL_32 isRbEqAppended = FALSE;

            if (m_settings.applyAliasFix)
            {
                isRbEqAppended = (rbEquation[i].getsize() > (rbAppendedWithPipeBits[i] ? 1 : 0));
            }
            else
            {
                isRbEqAppended = (rbEquation[i].getsize() > 0);
            }

            if (isRbEqAppended)
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
************************************************************************************************************************
*   Gfx9Lib::IsEquationSupported
*
*   @brief
*       Check if equation is supported for given swizzle mode and resource type.
*
*   @return
*       TRUE if supported
************************************************************************************************************************
*/
BOOL_32 Gfx9Lib::IsEquationSupported(
    AddrResourceType rsrcType,
    AddrSwizzleMode  swMode,
    UINT_32          elementBytesLog2) const
{
    BOOL_32 supported = (elementBytesLog2 < MaxElementBytesLog2) &&
                        (IsLinear(swMode) == FALSE) &&
                        (((IsTex2d(rsrcType) == TRUE) &&
                          ((elementBytesLog2 < 4) ||
                           ((IsRotateSwizzle(swMode) == FALSE) &&
                            (IsZOrderSwizzle(swMode) == FALSE)))) ||
                         ((IsTex3d(rsrcType) == TRUE) &&
                          (IsRotateSwizzle(swMode) == FALSE) &&
                          (IsBlock256b(swMode) == FALSE)));

    return supported;
}

/**
************************************************************************************************************************
*   Gfx9Lib::InitEquationTable
*
*   @brief
*       Initialize Equation table.
*
*   @return
*       N/A
************************************************************************************************************************
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
                    else
                    {
                        ADDR_ASSERT_ALWAYS();
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
************************************************************************************************************************
*   Gfx9Lib::HwlGetEquationIndex
*
*   @brief
*       Interface function stub of GetEquationIndex
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
UINT_32 Gfx9Lib::HwlGetEquationIndex(
    const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,
    ADDR2_COMPUTE_SURFACE_INFO_OUTPUT*      pOut
    ) const
{
    AddrResourceType rsrcType         = pIn->resourceType;
    AddrSwizzleMode  swMode           = pIn->swizzleMode;
    UINT_32          elementBytesLog2 = Log2(pIn->bpp >> 3);
    UINT_32          index            = ADDR_INVALID_EQUATION_INDEX;

    if (IsEquationSupported(rsrcType, swMode, elementBytesLog2))
    {
        UINT_32 rsrcTypeIdx = static_cast<UINT_32>(rsrcType) - 1;
        UINT_32 swModeIdx   = static_cast<UINT_32>(swMode);

        index = m_equationLookupTable[rsrcTypeIdx][swModeIdx][elementBytesLog2];
    }

    if (pOut->pMipInfo != NULL)
    {
        for (UINT_32 i = 0; i < pIn->numMipLevels; i++)
        {
            pOut->pMipInfo[i].equationIndex = index;
        }
    }

    return index;
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeBlock256Equation
*
*   @brief
*       Interface function stub of ComputeBlock256Equation
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeBlock256Equation(
    AddrResourceType rsrcType,
    AddrSwizzleMode  swMode,
    UINT_32          elementBytesLog2,
    ADDR_EQUATION*   pEquation) const
{
    ADDR_E_RETURNCODE ret = ADDR_OK;

    pEquation->numBits = 8;

    UINT_32 i = 0;
    for (; i < elementBytesLog2; i++)
    {
        InitChannel(1, 0 , i, &pEquation->addr[i]);
    }

    ADDR_CHANNEL_SETTING* pixelBit = &pEquation->addr[elementBytesLog2];

    const UINT_32 maxBitsUsed = 4;
    ADDR_CHANNEL_SETTING x[maxBitsUsed] = {};
    ADDR_CHANNEL_SETTING y[maxBitsUsed] = {};

    for (i = 0; i < maxBitsUsed; i++)
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
        Dim2d microBlockDim = Block256_2d[elementBytesLog2];
        ADDR_ASSERT((2u << GetMaxValidChannelIndex(pEquation->addr, 8, 0)) ==
                    (microBlockDim.w * (1 << elementBytesLog2)));
        ADDR_ASSERT((2u << GetMaxValidChannelIndex(pEquation->addr, 8, 1)) == microBlockDim.h);
    }

    return ret;
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeThinEquation
*
*   @brief
*       Interface function stub of ComputeThinEquation
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeThinEquation(
    AddrResourceType rsrcType,
    AddrSwizzleMode  swMode,
    UINT_32          elementBytesLog2,
    ADDR_EQUATION*   pEquation) const
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

    const UINT_32 maxBitsUsed = 14;
    ADDR_ASSERT((2 * maxBitsUsed) >= maxXorBits);
    ADDR_CHANNEL_SETTING x[maxBitsUsed] = {};
    ADDR_CHANNEL_SETTING y[maxBitsUsed] = {};

    const UINT_32 extraXorBits = 16;
    ADDR_ASSERT(extraXorBits >= maxXorBits - blockSizeLog2);
    ADDR_CHANNEL_SETTING xorExtra[extraXorBits] = {};

    for (UINT_32 i = 0; i < maxBitsUsed; i++)
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
            Dim2d microBlockDim = Block256_2d[elementBytesLog2];
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

        if (IsXor(swMode))
        {
            // Fill XOR bits
            UINT_32 pipeStart = m_pipeInterleaveLog2;
            UINT_32 pipeXorBits = GetPipeXorBits(blockSizeLog2);

            UINT_32 bankStart = pipeStart + pipeXorBits;
            UINT_32 bankXorBits = GetBankXorBits(blockSizeLog2);

            for (UINT_32 i = 0; i < pipeXorBits; i++)
            {
                UINT_32               xor1BitPos = pipeStart + 2 * pipeXorBits - 1 - i;
                ADDR_CHANNEL_SETTING* pXor1Src   = (xor1BitPos < blockSizeLog2) ?
                                                   &pEquation->addr[xor1BitPos] : &xorExtra[xor1BitPos - blockSizeLog2];

                InitChannel(&pEquation->xor1[pipeStart + i], pXor1Src);
            }

            for (UINT_32 i = 0; i < bankXorBits; i++)
            {
                UINT_32               xor1BitPos = bankStart + 2 * bankXorBits - 1 - i;
                ADDR_CHANNEL_SETTING* pXor1Src   = (xor1BitPos < blockSizeLog2) ?
                                                   &pEquation->addr[xor1BitPos] : &xorExtra[xor1BitPos - blockSizeLog2];

                InitChannel(&pEquation->xor1[bankStart + i], pXor1Src);
            }

            if (IsPrt(swMode) == FALSE)
            {
                for (UINT_32 i = 0; i < pipeXorBits; i++)
                {
                    InitChannel(1, 2, pipeXorBits - i - 1, &pEquation->xor2[pipeStart + i]);
                }

                for (UINT_32 i = 0; i < bankXorBits; i++)
                {
                    InitChannel(1, 2, bankXorBits - i - 1 + pipeXorBits, &pEquation->xor2[bankStart + i]);
                }
            }
        }

        pEquation->numBits = blockSizeLog2;
    }

    return ret;
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeThickEquation
*
*   @brief
*       Interface function stub of ComputeThickEquation
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeThickEquation(
    AddrResourceType rsrcType,
    AddrSwizzleMode  swMode,
    UINT_32          elementBytesLog2,
    ADDR_EQUATION*   pEquation) const
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

    const UINT_32 maxBitsUsed = 12;
    ADDR_ASSERT((3 * maxBitsUsed) >= maxXorBits);
    ADDR_CHANNEL_SETTING x[maxBitsUsed] = {};
    ADDR_CHANNEL_SETTING y[maxBitsUsed] = {};
    ADDR_CHANNEL_SETTING z[maxBitsUsed] = {};

    const UINT_32 extraXorBits = 24;
    ADDR_ASSERT(extraXorBits >= maxXorBits - blockSizeLog2);
    ADDR_CHANNEL_SETTING xorExtra[extraXorBits] = {};

    for (UINT_32 i = 0; i < maxBitsUsed; i++)
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
        Dim3d microBlockDim = Block1K_3d[elementBytesLog2];
        UINT_32 xIdx = Log2(microBlockDim.w);
        UINT_32 yIdx = Log2(microBlockDim.h);
        UINT_32 zIdx = Log2(microBlockDim.d);

        pixelBit = pEquation->addr;

        const UINT_32 lowBits = 10;
        ADDR_ASSERT(pEquation->addr[lowBits - 1].valid == 1);
        ADDR_ASSERT(pEquation->addr[lowBits].valid == 0);

        for (UINT_32 i = lowBits; i < blockSizeLog2; i++)
        {
            if ((i % 3) == 0)
            {
                pixelBit[i] = x[xIdx++];
            }
            else if ((i % 3) == 1)
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
            if ((i % 3) == 0)
            {
                xorExtra[i - blockSizeLog2] = x[xIdx++];
            }
            else if ((i % 3) == 1)
            {
                xorExtra[i - blockSizeLog2] = z[zIdx++];
            }
            else
            {
                xorExtra[i - blockSizeLog2] = y[yIdx++];
            }
        }

        if (IsXor(swMode))
        {
            // Fill XOR bits
            UINT_32 pipeStart = m_pipeInterleaveLog2;
            UINT_32 pipeXorBits = GetPipeXorBits(blockSizeLog2);
            for (UINT_32 i = 0; i < pipeXorBits; i++)
            {
                UINT_32               xor1BitPos = pipeStart + (3 * pipeXorBits) - 1 - (2 * i);
                ADDR_CHANNEL_SETTING* pXor1Src   = (xor1BitPos < blockSizeLog2) ?
                                                   &pEquation->addr[xor1BitPos] : &xorExtra[xor1BitPos - blockSizeLog2];

                InitChannel(&pEquation->xor1[pipeStart + i], pXor1Src);

                UINT_32               xor2BitPos = pipeStart + (3 * pipeXorBits) - 2 - (2 * i);
                ADDR_CHANNEL_SETTING* pXor2Src   = (xor2BitPos < blockSizeLog2) ?
                                                   &pEquation->addr[xor2BitPos] : &xorExtra[xor2BitPos - blockSizeLog2];

                InitChannel(&pEquation->xor2[pipeStart + i], pXor2Src);
            }

            UINT_32 bankStart = pipeStart + pipeXorBits;
            UINT_32 bankXorBits = GetBankXorBits(blockSizeLog2);
            for (UINT_32 i = 0; i < bankXorBits; i++)
            {
                UINT_32               xor1BitPos = bankStart + (3 * bankXorBits) - 1 - (2 * i);
                ADDR_CHANNEL_SETTING* pXor1Src   = (xor1BitPos < blockSizeLog2) ?
                                                   &pEquation->addr[xor1BitPos] : &xorExtra[xor1BitPos - blockSizeLog2];

                InitChannel(&pEquation->xor1[bankStart + i], pXor1Src);

                UINT_32               xor2BitPos = bankStart + (3 * bankXorBits) - 2 - (2 * i);
                ADDR_CHANNEL_SETTING* pXor2Src   = (xor2BitPos < blockSizeLog2) ?
                                                   &pEquation->addr[xor2BitPos] : &xorExtra[xor2BitPos - blockSizeLog2];

                InitChannel(&pEquation->xor2[bankStart + i], pXor2Src);
            }
        }

        pEquation->numBits = blockSizeLog2;
    }

    return ret;
}

/**
************************************************************************************************************************
*   Gfx9Lib::IsValidDisplaySwizzleMode
*
*   @brief
*       Check if a swizzle mode is supported by display engine
*
*   @return
*       TRUE is swizzle mode is supported by display engine
************************************************************************************************************************
*/
BOOL_32 Gfx9Lib::IsValidDisplaySwizzleMode(
    const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn) const
{
    BOOL_32 support = FALSE;

    const AddrResourceType resourceType = pIn->resourceType;
    (void)resourceType;
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
    else if (m_settings.isDcn1)
    {
        switch (swizzleMode)
        {
            case ADDR_SW_4KB_D:
            case ADDR_SW_64KB_D:
            case ADDR_SW_VAR_D:
            case ADDR_SW_64KB_D_T:
            case ADDR_SW_4KB_D_X:
            case ADDR_SW_64KB_D_X:
            case ADDR_SW_VAR_D_X:
                support = (pIn->bpp == 64);
                break;

            case ADDR_SW_LINEAR:
            case ADDR_SW_4KB_S:
            case ADDR_SW_64KB_S:
            case ADDR_SW_VAR_S:
            case ADDR_SW_64KB_S_T:
            case ADDR_SW_4KB_S_X:
            case ADDR_SW_64KB_S_X:
            case ADDR_SW_VAR_S_X:
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

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputePipeBankXor
*
*   @brief
*       Generate a PipeBankXor value to be ORed into bits above pipeInterleaveBits of address
*
*   @return
*       PipeBankXor value
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputePipeBankXor(
    const ADDR2_COMPUTE_PIPEBANKXOR_INPUT* pIn,
    ADDR2_COMPUTE_PIPEBANKXOR_OUTPUT*      pOut) const
{
    UINT_32 macroBlockBits = GetBlockSizeLog2(pIn->swizzleMode);
    UINT_32 pipeBits       = GetPipeXorBits(macroBlockBits);
    UINT_32 bankBits       = GetBankXorBits(macroBlockBits);

    UINT_32 pipeXor = 0;
    UINT_32 bankXor = 0;

    const UINT_32 bankMask = (1 << bankBits) - 1;
    const UINT_32 index    = pIn->surfIndex & bankMask;

    const UINT_32 bpp      = pIn->flags.fmask ?
                             GetFmaskBpp(pIn->numSamples, pIn->numFrags) : GetElemLib()->GetBitsPerPixel(pIn->format);
    if (bankBits == 4)
    {
        static const UINT_32 BankXorSmallBpp[] = {0, 7, 4, 3, 8, 15, 12, 11, 1, 6, 5, 2, 9, 14, 13, 10};
        static const UINT_32 BankXorLargeBpp[] = {0, 7, 8, 15, 4, 3, 12, 11, 1, 6, 9, 14, 5, 2, 13, 10};

        bankXor = (bpp <= 32) ? BankXorSmallBpp[index] : BankXorLargeBpp[index];
    }
    else if (bankBits > 0)
    {
        UINT_32 bankIncrease = (1 << (bankBits - 1)) - 1;
        bankIncrease = (bankIncrease == 0) ? 1 : bankIncrease;
        bankXor = (index * bankIncrease) & bankMask;
    }

    pOut->pipeBankXor = (bankXor << pipeBits) | pipeXor;

    return ADDR_OK;
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeSlicePipeBankXor
*
*   @brief
*       Generate slice PipeBankXor value based on base PipeBankXor value and slice id
*
*   @return
*       PipeBankXor value
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeSlicePipeBankXor(
    const ADDR2_COMPUTE_SLICE_PIPEBANKXOR_INPUT* pIn,
    ADDR2_COMPUTE_SLICE_PIPEBANKXOR_OUTPUT*      pOut) const
{
    UINT_32 macroBlockBits = GetBlockSizeLog2(pIn->swizzleMode);
    UINT_32 pipeBits       = GetPipeXorBits(macroBlockBits);
    UINT_32 bankBits       = GetBankXorBits(macroBlockBits);

    UINT_32 pipeXor        = ReverseBitVector(pIn->slice, pipeBits);
    UINT_32 bankXor        = ReverseBitVector(pIn->slice >> pipeBits, bankBits);

    pOut->pipeBankXor = pIn->basePipeBankXor ^ (pipeXor | (bankXor << pipeBits));

    return ADDR_OK;
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeSubResourceOffsetForSwizzlePattern
*
*   @brief
*       Compute sub resource offset to support swizzle pattern
*
*   @return
*       Offset
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeSubResourceOffsetForSwizzlePattern(
    const ADDR2_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_INPUT* pIn,
    ADDR2_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_OUTPUT*      pOut) const
{
    ADDR_ASSERT(IsThin(pIn->resourceType, pIn->swizzleMode));

    UINT_32 macroBlockBits = GetBlockSizeLog2(pIn->swizzleMode);
    UINT_32 pipeBits       = GetPipeXorBits(macroBlockBits);
    UINT_32 bankBits       = GetBankXorBits(macroBlockBits);
    UINT_32 pipeXor        = ReverseBitVector(pIn->slice, pipeBits);
    UINT_32 bankXor        = ReverseBitVector(pIn->slice >> pipeBits, bankBits);
    UINT_32 pipeBankXor    = ((pipeXor | (bankXor << pipeBits)) ^ (pIn->pipeBankXor)) << m_pipeInterleaveLog2;

    pOut->offset = pIn->slice * pIn->sliceSize +
                   pIn->macroBlockOffset +
                   (pIn->mipTailOffset ^ pipeBankXor) -
                   static_cast<UINT_64>(pipeBankXor);
    return ADDR_OK;
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeSurfaceInfoSanityCheck
*
*   @brief
*       Compute surface info sanity check
*
*   @return
*       Offset
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeSurfaceInfoSanityCheck(
    const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn) const
{
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

    AddrResourceType rsrcType    = pIn->resourceType;
    BOOL_32          tex3d       = IsTex3d(rsrcType);
    AddrSwizzleMode  swizzle     = pIn->swizzleMode;
    BOOL_32          linear      = IsLinear(swizzle);
    BOOL_32          blk256B     = IsBlock256b(swizzle);
    BOOL_32          blkVar      = IsBlockVariable(swizzle);
    BOOL_32          isNonPrtXor = IsNonPrtXor(swizzle);
    BOOL_32          prt         = flags.prt;
    BOOL_32          stereo      = flags.qbStereo;

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
                invalid = msaa || zbuffer || display || (linear == FALSE) || stereo;
                break;
            case ADDR_RSRC_TEX_2D:
                invalid = (msaa && mipmap) || (stereo && msaa) || (stereo && mipmap);
                break;
            case ADDR_RSRC_TEX_3D:
                invalid = msaa || zbuffer || display || stereo;
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
            invalid = ((ADDR_RSRC_TEX_1D != rsrcType) && prt) ||
                      zbuffer || msaa || (pIn->bpp == 0) || ((pIn->bpp % 8) != 0);
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
                    invalid = zbuffer || (pIn->bpp > 64) || tex3d;
                }
                else
                {
                    ADDR_ASSERT(!"invalid swizzle mode");
                    invalid = TRUE;
                }
            }
        }
    }

    ADDR_ASSERT(invalid == FALSE);

    return invalid ? ADDR_INVALIDPARAMS : ADDR_OK;
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlGetPreferredSurfaceSetting
*
*   @brief
*       Internal function to get suggested surface information for cliet to use
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlGetPreferredSurfaceSetting(
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

    enum AddrSwSet
    {
        AddrSwSetZ = 1 << ADDR_SW_Z,
        AddrSwSetS = 1 << ADDR_SW_S,
        AddrSwSetD = 1 << ADDR_SW_D,
        AddrSwSetR = 1 << ADDR_SW_R,

        AddrSwSetAll = AddrSwSetZ | AddrSwSetS | AddrSwSetD | AddrSwSetR,
    };

    ADDR_E_RETURNCODE returnCode = ADDR_OK;
    ElemLib*          pElemLib   = GetElemLib();

    // Set format to INVALID will skip this conversion
    UINT_32 expandX = 1;
    UINT_32 expandY = 1;
    UINT_32 bpp     = pIn->bpp;
    UINT_32 width   = pIn->width;
    UINT_32 height  = pIn->height;

    if (pIn->format != ADDR_FMT_INVALID)
    {
        // Don't care for this case
        ElemMode elemMode = ADDR_UNCOMPRESSED;

        // Get compression/expansion factors and element mode which indicates compression/expansion
        bpp = pElemLib->GetBitsPerPixel(pIn->format,
                                        &elemMode,
                                        &expandX,
                                        &expandY);

        UINT_32 basePitch = 0;
        GetElemLib()->AdjustSurfaceInfo(elemMode,
                                        expandX,
                                        expandY,
                                        &bpp,
                                        &basePitch,
                                        &width,
                                        &height);
    }

    UINT_32 numSamples   = Max(pIn->numSamples, 1u);
    UINT_32 numFrags     = (pIn->numFrags == 0) ? numSamples : pIn->numFrags;
    UINT_32 slice        = Max(pIn->numSlices, 1u);
    UINT_32 numMipLevels = Max(pIn->numMipLevels, 1u);
    UINT_32 minSizeAlign = NextPow2(pIn->minSizeAlign);

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

    if (bpp < 8)
    {
        ADDR_ASSERT_ALWAYS();

        returnCode = ADDR_INVALIDPARAMS;
    }
    else if (IsTex1d(pOut->resourceType))
    {
        pOut->swizzleMode         = ADDR_SW_LINEAR;
        pOut->validBlockSet.value = AddrBlockSetLinear;
        pOut->canXor              = FALSE;
    }
    else
    {
        ADDR2_BLOCK_SET blockSet;
        blockSet.value = 0;

        ADDR2_SWTYPE_SET addrPreferredSwSet, addrValidSwSet, clientPreferredSwSet;
        addrPreferredSwSet.value = AddrSwSetS;
        addrValidSwSet           = addrPreferredSwSet;
        clientPreferredSwSet     = pIn->preferredSwSet;

        if (clientPreferredSwSet.value == 0)
        {
            clientPreferredSwSet.value = AddrSwSetAll;
        }

        // prt Xor and non-xor will have less height align requirement for stereo surface
        BOOL_32 prtXor          = (pIn->flags.prt || pIn->flags.qbStereo) && (pIn->noXor == FALSE);
        BOOL_32 displayResource = FALSE;

        pOut->canXor = (pIn->flags.prt == FALSE) && (pIn->noXor == FALSE);

        // Filter out improper swType and blockSet by HW restriction
        if (pIn->flags.fmask || pIn->flags.depth || pIn->flags.stencil)
        {
            ADDR_ASSERT(IsTex2d(pOut->resourceType));
            blockSet.value           = AddrBlockSetMacro;
            addrPreferredSwSet.value = AddrSwSetZ;
            addrValidSwSet.value     = AddrSwSetZ;

            if (pIn->flags.noMetadata == FALSE)
            {
                if (pIn->flags.depth &&
                    pIn->flags.texture &&
                    (((bpp == 16) && (numFrags >= 4)) || ((bpp == 32) && (numFrags >= 2))))
                {
                    // When _X/_T swizzle mode was used for MSAA depth texture, TC will get zplane
                    // equation from wrong address within memory range a tile covered and use the
                    // garbage data for compressed Z reading which finally leads to corruption.
                    pOut->canXor = FALSE;
                    prtXor       = FALSE;
                }

                if (m_settings.htileCacheRbConflict &&
                    (pIn->flags.depth || pIn->flags.stencil) &&
                    (slice > 1) &&
                    (pIn->flags.metaRbUnaligned == FALSE) &&
                    (pIn->flags.metaPipeUnaligned == FALSE))
                {
                    // Z_X 2D array with Rb/Pipe aligned HTile won't have metadata cache coherency
                    pOut->canXor = FALSE;
                }
            }
        }
        else if (ElemLib::IsBlockCompressed(pIn->format))
        {
            // block compressed formats (BCx, ASTC, ETC2) must be either S or D modes.
            // Not sure under what circumstances "_D" would be appropriate as these formats
            // are not displayable.
            blockSet.value = AddrBlockSetMacro;

            // This isn't to be used as texture and caller doesn't allow macro tiled.
            if ((pIn->flags.texture == FALSE) &&
                (pIn->forbiddenBlock.macro4KB && pIn->forbiddenBlock.macro64KB))
            {
                blockSet.value |= AddrBlockSetLinear;
            }

            addrPreferredSwSet.value = AddrSwSetD;
            addrValidSwSet.value     = AddrSwSetS | AddrSwSetD;
        }
        else if (ElemLib::IsMacroPixelPacked(pIn->format))
        {
            // macro pixel packed formats (BG_RG, GB_GR) does not support the Z modes.
            // Its notclear under what circumstances the D or R modes would be appropriate
            // since these formats are not displayable.
            blockSet.value  = AddrBlockSetLinear | AddrBlockSetMacro;

            addrPreferredSwSet.value = AddrSwSetS;
            addrValidSwSet.value     = AddrSwSetS | AddrSwSetD | AddrSwSetR;
        }
        else if (IsTex3d(pOut->resourceType))
        {
            blockSet.value = AddrBlockSetLinear | AddrBlockSetMacro;

            if (pIn->flags.prt)
            {
                // PRT cannot use SW_D which gives an unexpected block dimension
                addrPreferredSwSet.value = AddrSwSetZ;
                addrValidSwSet.value     = AddrSwSetZ | AddrSwSetS;
            }
            else if ((numMipLevels > 1) && (slice >= width) && (slice >= height))
            {
                // When depth (Z) is the maximum dimension then must use one of the SW_*_S
                // or SW_*_Z modes if mipmapping is desired on a 3D surface
                addrPreferredSwSet.value = AddrSwSetZ;
                addrValidSwSet.value     = AddrSwSetZ | AddrSwSetS;
            }
            else if (pIn->flags.color)
            {
                addrPreferredSwSet.value = AddrSwSetD;
                addrValidSwSet.value     = AddrSwSetZ | AddrSwSetS | AddrSwSetD;
            }
            else
            {
                addrPreferredSwSet.value = AddrSwSetZ;
                addrValidSwSet.value     = AddrSwSetZ | AddrSwSetD;
                if (bpp != 128)
                {
                    addrValidSwSet.value |= AddrSwSetS;
                }
            }
        }
        else
        {
            addrPreferredSwSet.value = ((pIn->flags.display == TRUE) ||
                                        (pIn->flags.overlay == TRUE) ||
                                        (pIn->bpp           == 128)) ? AddrSwSetD : AddrSwSetS;

            addrValidSwSet.value     = AddrSwSetS | AddrSwSetD | AddrSwSetR;

            if (numMipLevels > 1)
            {
                ADDR_ASSERT(numFrags == 1);
                blockSet.value = AddrBlockSetLinear | AddrBlockSetMacro;
            }
            else if ((numFrags > 1) || (numSamples > 1))
            {
                ADDR_ASSERT(IsTex2d(pOut->resourceType));
                blockSet.value = AddrBlockSetMacro;
            }
            else
            {
                ADDR_ASSERT(IsTex2d(pOut->resourceType));
                blockSet.value = AddrBlockSetLinear | AddrBlockSetMicro | AddrBlockSetMacro;

                displayResource = pIn->flags.rotated || pIn->flags.display;

                if (displayResource)
                {
                    addrPreferredSwSet.value = pIn->flags.rotated ? AddrSwSetR : AddrSwSetD;

                    if (pIn->bpp > 64)
                    {
                        blockSet.value = 0;
                    }
                    else if (m_settings.isDce12)
                    {
                        if (pIn->bpp != 32)
                        {
                            blockSet.micro = FALSE;
                        }

                        // DCE12 does not support display surface to be _T swizzle mode
                        prtXor = FALSE;

                        addrValidSwSet.value = AddrSwSetD | AddrSwSetR;
                    }
                    else if (m_settings.isDcn1)
                    {
                        // _R is not supported by Dcn1
                        if (pIn->bpp == 64)
                        {
                            addrPreferredSwSet.value = AddrSwSetD;
                            addrValidSwSet.value     = AddrSwSetS | AddrSwSetD;
                        }
                        else
                        {
                            addrPreferredSwSet.value = AddrSwSetS;
                            addrValidSwSet.value     = AddrSwSetS;
                        }

                        blockSet.micro = FALSE;
                    }
                    else
                    {
                        ADDR_NOT_IMPLEMENTED();
                        returnCode = ADDR_NOTSUPPORTED;
                    }
                }
            }
        }

        ADDR_ASSERT((addrValidSwSet.value & addrPreferredSwSet.value) == addrPreferredSwSet.value);

        pOut->clientPreferredSwSet = clientPreferredSwSet;

        // Clamp client preferred set to valid set
        clientPreferredSwSet.value &= addrValidSwSet.value;

        pOut->validSwTypeSet = addrValidSwSet;

        if (clientPreferredSwSet.value == 0)
        {
            // Client asks for an invalid swizzle type...
            ADDR_ASSERT_ALWAYS();
            returnCode = ADDR_INVALIDPARAMS;
        }
        else
        {
            if (IsPow2(clientPreferredSwSet.value))
            {
                // Only one swizzle type left, use it directly
                addrPreferredSwSet.value = clientPreferredSwSet.value;
            }
            else if ((clientPreferredSwSet.value & addrPreferredSwSet.value) == 0)
            {
                // Client wants 2 or more a valid swizzle type but none of them is addrlib preferred
                if (clientPreferredSwSet.sw_D)
                {
                    addrPreferredSwSet.value = AddrSwSetD;
                }
                else if (clientPreferredSwSet.sw_Z)
                {
                    addrPreferredSwSet.value = AddrSwSetZ;
                }
                else if (clientPreferredSwSet.sw_R)
                {
                    addrPreferredSwSet.value = AddrSwSetR;
                }
                else
                {
                    ADDR_ASSERT(clientPreferredSwSet.sw_S);
                    addrPreferredSwSet.value = AddrSwSetS;
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
                blockSet.value &= AddrBlockSetMacro64KB;
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

            Dim3d blkAlign[AddrBlockMaxTiledType]  = {{0}, {0}, {0}};
            Dim3d paddedDim[AddrBlockMaxTiledType] = {{0}, {0}, {0}};
            UINT_64 padSize[AddrBlockMaxTiledType] = {0};

            if (blockSet.micro)
            {
                returnCode = ComputeBlockDimensionForSurf(&blkAlign[AddrBlockMicro].w,
                                                          &blkAlign[AddrBlockMicro].h,
                                                          &blkAlign[AddrBlockMicro].d,
                                                          bpp,
                                                          numFrags,
                                                          pOut->resourceType,
                                                          ADDR_SW_256B);

                if (returnCode == ADDR_OK)
                {
                    if (displayResource)
                    {
                        blkAlign[AddrBlockMicro].w = PowTwoAlign(blkAlign[AddrBlockMicro].w, 32);
                    }
                    else if ((blkAlign[AddrBlockMicro].w >= width) && (blkAlign[AddrBlockMicro].h >= height) &&
                             (minSizeAlign <= GetBlockSize(ADDR_SW_256B)))
                    {
                        // If one 256B block can contain the surface, don't bother bigger block type
                        blockSet.macro4KB = FALSE;
                        blockSet.macro64KB = FALSE;
                        blockSet.var = FALSE;
                    }

                    padSize[AddrBlockMicro] = ComputePadSize(&blkAlign[AddrBlockMicro], width, height,
                                                             slice, &paddedDim[AddrBlockMicro]);
                }
            }

            if ((returnCode == ADDR_OK) && blockSet.macro4KB)
            {
                returnCode = ComputeBlockDimensionForSurf(&blkAlign[AddrBlock4KB].w,
                                                          &blkAlign[AddrBlock4KB].h,
                                                          &blkAlign[AddrBlock4KB].d,
                                                          bpp,
                                                          numFrags,
                                                          pOut->resourceType,
                                                          ADDR_SW_4KB);

                if (returnCode == ADDR_OK)
                {
                    if (displayResource)
                    {
                        blkAlign[AddrBlock4KB].w = PowTwoAlign(blkAlign[AddrBlock4KB].w, 32);
                    }

                    padSize[AddrBlock4KB] = ComputePadSize(&blkAlign[AddrBlock4KB], width, height,
                                                           slice, &paddedDim[AddrBlock4KB]);

                    ADDR_ASSERT(padSize[AddrBlock4KB] >= padSize[AddrBlockMicro]);
                }
            }

            if ((returnCode == ADDR_OK) && blockSet.macro64KB)
            {
                returnCode = ComputeBlockDimensionForSurf(&blkAlign[AddrBlock64KB].w,
                                                          &blkAlign[AddrBlock64KB].h,
                                                          &blkAlign[AddrBlock64KB].d,
                                                          bpp,
                                                          numFrags,
                                                          pOut->resourceType,
                                                          ADDR_SW_64KB);

                if (returnCode == ADDR_OK)
                {
                    if (displayResource)
                    {
                        blkAlign[AddrBlock64KB].w = PowTwoAlign(blkAlign[AddrBlock64KB].w, 32);
                    }

                    padSize[AddrBlock64KB] = ComputePadSize(&blkAlign[AddrBlock64KB], width, height,
                                                            slice, &paddedDim[AddrBlock64KB]);

                    ADDR_ASSERT(padSize[AddrBlock64KB] >= padSize[AddrBlock4KB]);
                    ADDR_ASSERT(padSize[AddrBlock64KB] >= padSize[AddrBlockMicro]);
                }
            }

            if (returnCode == ADDR_OK)
            {
                UINT_64 minSizeAlignInElement = Max(minSizeAlign / (bpp >> 3), 1u);

                for (UINT_32 i = AddrBlockMicro; i < AddrBlockMaxTiledType; i++)
                {
                    padSize[i] = PowTwoAlign(padSize[i], minSizeAlignInElement);
                }

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
                    UINT_64 threshold = blockSet.micro ? padSize[AddrBlockMicro] :
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
                else
                {
                    if (blockSet.macro64KB &&
                        (padSize[AddrBlock64KB] >= static_cast<UINT_64>(width) * height * slice * 2) &&
                        ((blockSet.value & ~AddrBlockSetMacro64KB) != 0))
                    {
                        // If 64KB block waste more than half memory on padding, filter it out from
                        // candidate list when it is not the only choice left
                        blockSet.macro64KB = FALSE;
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
                    pOut->canXor = pOut->canXor &&
                                   (blockSet.macro4KB || blockSet.macro64KB || blockSet.var);

                    if (blockSet.macro64KB || blockSet.macro4KB)
                    {
                        if (addrPreferredSwSet.value == AddrSwSetZ)
                        {
                            pOut->swizzleMode = blockSet.macro64KB ? ADDR_SW_64KB_Z : ADDR_SW_4KB_Z;
                        }
                        else if (addrPreferredSwSet.value == AddrSwSetS)
                        {
                            pOut->swizzleMode = blockSet.macro64KB ? ADDR_SW_64KB_S : ADDR_SW_4KB_S;
                        }
                        else if (addrPreferredSwSet.value == AddrSwSetD)
                        {
                            pOut->swizzleMode = blockSet.macro64KB ? ADDR_SW_64KB_D : ADDR_SW_4KB_D;
                        }
                        else
                        {
                            ADDR_ASSERT(addrPreferredSwSet.value == AddrSwSetR);
                            pOut->swizzleMode = blockSet.macro64KB ? ADDR_SW_64KB_R : ADDR_SW_4KB_R;
                        }

                        if (prtXor && blockSet.macro64KB)
                        {
                            // Client wants PRTXOR, give back _T swizzle mode if 64KB is available
                            const UINT_32 prtGap = ADDR_SW_64KB_Z_T - ADDR_SW_64KB_Z;
                            pOut->swizzleMode = static_cast<AddrSwizzleMode>(pOut->swizzleMode + prtGap);
                        }
                        else if (pOut->canXor)
                        {
                            // Client wants XOR and this is allowed, return XOR version swizzle mode
                            const UINT_32 xorGap = ADDR_SW_4KB_Z_X - ADDR_SW_4KB_Z;
                            pOut->swizzleMode = static_cast<AddrSwizzleMode>(pOut->swizzleMode + xorGap);
                        }
                    }
                    else if (blockSet.micro)
                    {
                        if (addrPreferredSwSet.value == AddrSwSetS)
                        {
                            pOut->swizzleMode = ADDR_SW_256B_S;
                        }
                        else if (addrPreferredSwSet.value == AddrSwSetD)
                        {
                            pOut->swizzleMode = ADDR_SW_256B_D;
                        }
                        else
                        {
                            ADDR_ASSERT(addrPreferredSwSet.value == AddrSwSetR);
                            pOut->swizzleMode = ADDR_SW_256B_R;
                        }
                    }
                    else if (blockSet.linear)
                    {
                        // Fall into this branch doesn't mean linear is suitable, only no other choices!
                        pOut->swizzleMode = ADDR_SW_LINEAR;
                    }
                    else
                    {
                        ADDR_ASSERT(blockSet.var);

                        // Designer consider VAR swizzle mode is usless for most cases
                        ADDR_UNHANDLED_CASE();

                        returnCode = ADDR_NOTSUPPORTED;
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

                        HwlComputeSurfaceInfoSanityCheck(&localIn);

                    }
#endif
                }
            }
        }
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Gfx9Lib::ComputeStereoInfo
*
*   @brief
*       Compute height alignment and right eye pipeBankXor for stereo surface
*
*   @return
*       Error code
*
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::ComputeStereoInfo(
    const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,
    ADDR2_COMPUTE_SURFACE_INFO_OUTPUT*      pOut,
    UINT_32*                                pHeightAlign
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    UINT_32 eqIndex = HwlGetEquationIndex(pIn, pOut);

    if (eqIndex < m_numEquations)
    {
        if (IsXor(pIn->swizzleMode))
        {
            const UINT_32        blkSizeLog2       = GetBlockSizeLog2(pIn->swizzleMode);
            const UINT_32        numPipeBits       = GetPipeXorBits(blkSizeLog2);
            const UINT_32        numBankBits       = GetBankXorBits(blkSizeLog2);
            const UINT_32        bppLog2           = Log2(pIn->bpp >> 3);
            const UINT_32        maxYCoordBlock256 = Log2(Block256_2d[bppLog2].h) - 1;
            const ADDR_EQUATION *pEqToCheck        = &m_equationTable[eqIndex];

            ADDR_ASSERT(maxYCoordBlock256 ==
                        GetMaxValidChannelIndex(&pEqToCheck->addr[0], GetBlockSizeLog2(ADDR_SW_256B), 1));

            const UINT_32 maxYCoordInBaseEquation =
                (blkSizeLog2 - GetBlockSizeLog2(ADDR_SW_256B)) / 2 + maxYCoordBlock256;

            ADDR_ASSERT(maxYCoordInBaseEquation ==
                        GetMaxValidChannelIndex(&pEqToCheck->addr[0], blkSizeLog2, 1));

            const UINT_32 maxYCoordInPipeXor = (numPipeBits == 0) ? 0 : maxYCoordBlock256 + numPipeBits;

            ADDR_ASSERT(maxYCoordInPipeXor ==
                        GetMaxValidChannelIndex(&pEqToCheck->xor1[m_pipeInterleaveLog2], numPipeBits, 1));

            const UINT_32 maxYCoordInBankXor = (numBankBits == 0) ?
                                               0 : maxYCoordBlock256 + (numPipeBits + 1) / 2 + numBankBits;

            ADDR_ASSERT(maxYCoordInBankXor ==
                        GetMaxValidChannelIndex(&pEqToCheck->xor1[m_pipeInterleaveLog2 + numPipeBits], numBankBits, 1));

            const UINT_32 maxYCoordInPipeBankXor = Max(maxYCoordInPipeXor, maxYCoordInBankXor);

            if (maxYCoordInPipeBankXor > maxYCoordInBaseEquation)
            {
                *pHeightAlign = 1u << maxYCoordInPipeBankXor;

                if (pOut->pStereoInfo != NULL)
                {
                    pOut->pStereoInfo->rightSwizzle = 0;

                    if ((PowTwoAlign(pIn->height, *pHeightAlign) % (*pHeightAlign * 2)) != 0)
                    {
                        if (maxYCoordInPipeXor == maxYCoordInPipeBankXor)
                        {
                            pOut->pStereoInfo->rightSwizzle |= (1u << 1);
                        }

                        if (maxYCoordInBankXor == maxYCoordInPipeBankXor)
                        {
                            pOut->pStereoInfo->rightSwizzle |=
                                1u << ((numPipeBits % 2) ? numPipeBits : numPipeBits + 1);
                        }

                        ADDR_ASSERT(pOut->pStereoInfo->rightSwizzle ==
                                    GetCoordActiveMask(&pEqToCheck->xor1[m_pipeInterleaveLog2],
                                                       numPipeBits + numBankBits, 1, maxYCoordInPipeBankXor));
                    }
                }
            }
        }
    }
    else
    {
        ADDR_ASSERT_ALWAYS();
        returnCode = ADDR_ERROR;
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeSurfaceInfoTiled
*
*   @brief
*       Internal function to calculate alignment for tiled surface
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeSurfaceInfoTiled(
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
        UINT_32 pitchAlignInElement = pOut->blockWidth;

        if ((IsTex2d(pIn->resourceType) == TRUE) &&
            (pIn->flags.display || pIn->flags.rotated) &&
            (pIn->numMipLevels <= 1) &&
            (pIn->numSamples <= 1) &&
            (pIn->numFrags <= 1))
        {
            // Display engine needs pitch align to be at least 32 pixels.
            pitchAlignInElement = PowTwoAlign(pitchAlignInElement, 32);
        }

        pOut->pitch = PowTwoAlign(pIn->width, pitchAlignInElement);

        if ((pIn->numMipLevels <= 1) && (pIn->pitchInElement > 0))
        {
            if ((pIn->pitchInElement % pitchAlignInElement) != 0)
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

        UINT_32 heightAlign = 0;

        if (pIn->flags.qbStereo)
        {
            returnCode = ComputeStereoInfo(pIn, pOut, &heightAlign);
        }

        if (returnCode == ADDR_OK)
        {
            pOut->height = PowTwoAlign(pIn->height, pOut->blockHeight);

            if (heightAlign > 1)
            {
                pOut->height = PowTwoAlign(pOut->height, heightAlign);
            }

            pOut->numSlices = PowTwoAlign(pIn->numSlices, pOut->blockSlices);

            pOut->epitchIsHeight   = FALSE;
            pOut->mipChainInTail   = FALSE;
            pOut->firstMipIdInTail = pIn->numMipLevels;

            pOut->mipChainPitch    = pOut->pitch;
            pOut->mipChainHeight   = pOut->height;
            pOut->mipChainSlice    = pOut->numSlices;

            if (pIn->numMipLevels > 1)
            {
                pOut->firstMipIdInTail = GetMipChainInfo(pIn->resourceType,
                                                         pIn->swizzleMode,
                                                         pIn->bpp,
                                                         pIn->width,
                                                         pIn->height,
                                                         pIn->numSlices,
                                                         pOut->blockWidth,
                                                         pOut->blockHeight,
                                                         pOut->blockSlices,
                                                         pIn->numMipLevels,
                                                         pOut->pMipInfo);

                const UINT_32 endingMipId = Min(pOut->firstMipIdInTail, pIn->numMipLevels - 1);

                if (endingMipId == 0)
                {
                    const Dim3d tailMaxDim = GetMipTailDim(pIn->resourceType,
                                                           pIn->swizzleMode,
                                                           pOut->blockWidth,
                                                           pOut->blockHeight,
                                                           pOut->blockSlices);

                    pOut->epitchIsHeight = TRUE;
                    pOut->pitch          = tailMaxDim.w;
                    pOut->height         = tailMaxDim.h;
                    pOut->numSlices      = IsThick(pIn->resourceType, pIn->swizzleMode) ?
                                           tailMaxDim.d : pIn->numSlices;
                    pOut->mipChainInTail = TRUE;
                }
                else
                {
                    UINT_32 mip0WidthInBlk  = pOut->pitch  / pOut->blockWidth;
                    UINT_32 mip0HeightInBlk = pOut->height / pOut->blockHeight;

                    AddrMajorMode majorMode = GetMajorMode(pIn->resourceType,
                                                           pIn->swizzleMode,
                                                           mip0WidthInBlk,
                                                           mip0HeightInBlk,
                                                           pOut->numSlices / pOut->blockSlices);
                    if (majorMode == ADDR_MAJOR_Y)
                    {
                        UINT_32 mip1WidthInBlk = RoundHalf(mip0WidthInBlk);

                        if ((mip1WidthInBlk == 1) && (endingMipId > 2))
                        {
                            mip1WidthInBlk++;
                        }

                        pOut->mipChainPitch += (mip1WidthInBlk * pOut->blockWidth);

                        pOut->epitchIsHeight = FALSE;
                    }
                    else
                    {
                        UINT_32 mip1HeightInBlk = RoundHalf(mip0HeightInBlk);

                        if ((mip1HeightInBlk == 1) && (endingMipId > 2))
                        {
                            mip1HeightInBlk++;
                        }

                        pOut->mipChainHeight += (mip1HeightInBlk * pOut->blockHeight);

                        pOut->epitchIsHeight = TRUE;
                    }
                }

                if (pOut->pMipInfo != NULL)
                {
                    UINT_32 elementBytesLog2 = Log2(pIn->bpp >> 3);

                    for (UINT_32 i = 0; i < pIn->numMipLevels; i++)
                    {
                        Dim3d   mipStartPos          = {0};
                        UINT_32 mipTailOffsetInBytes = 0;

                        mipStartPos = GetMipStartPos(pIn->resourceType,
                                                     pIn->swizzleMode,
                                                     pOut->pitch,
                                                     pOut->height,
                                                     pOut->numSlices,
                                                     pOut->blockWidth,
                                                     pOut->blockHeight,
                                                     pOut->blockSlices,
                                                     i,
                                                     elementBytesLog2,
                                                     &mipTailOffsetInBytes);

                        UINT_32 pitchInBlock     =
                            pOut->mipChainPitch / pOut->blockWidth;
                        UINT_32 sliceInBlock     =
                            (pOut->mipChainHeight / pOut->blockHeight) * pitchInBlock;
                        UINT_64 blockIndex       =
                            mipStartPos.d * sliceInBlock + mipStartPos.h * pitchInBlock + mipStartPos.w;
                        UINT_64 macroBlockOffset =
                            blockIndex << GetBlockSizeLog2(pIn->swizzleMode);

                        pOut->pMipInfo[i].macroBlockOffset = macroBlockOffset;
                        pOut->pMipInfo[i].mipTailOffset    = mipTailOffsetInBytes;
                    }
                }
            }
            else if (pOut->pMipInfo != NULL)
            {
                pOut->pMipInfo[0].pitch  = pOut->pitch;
                pOut->pMipInfo[0].height = pOut->height;
                pOut->pMipInfo[0].depth  = IsTex3d(pIn->resourceType)? pOut->numSlices : 1;
                pOut->pMipInfo[0].offset = 0;
            }

            pOut->sliceSize = static_cast<UINT_64>(pOut->mipChainPitch) * pOut->mipChainHeight *
                              (pIn->bpp >> 3) * pIn->numFrags;
            pOut->surfSize  = pOut->sliceSize * pOut->mipChainSlice;
            pOut->baseAlign = ComputeSurfaceBaseAlignTiled(pIn->swizzleMode);

            if (pIn->flags.prt)
            {
                pOut->baseAlign = Max(pOut->baseAlign, PrtAlignment);
            }
        }
    }

    return returnCode;
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeSurfaceInfoLinear
*
*   @brief
*       Internal function to calculate alignment for linear surface
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeSurfaceInfoLinear(
     const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,    ///< [in] input structure
     ADDR2_COMPUTE_SURFACE_INFO_OUTPUT*      pOut    ///< [out] output structure
     ) const
{
    ADDR_E_RETURNCODE returnCode   = ADDR_OK;
    UINT_32           pitch        = 0;
    UINT_32           actualHeight = 0;
    UINT_32           elementBytes = pIn->bpp >> 3;
    const UINT_32     alignment    = pIn->flags.prt ? PrtAlignment : 256;

    if (IsTex1d(pIn->resourceType))
    {
        if (pIn->height > 1)
        {
            returnCode = ADDR_INVALIDPARAMS;
        }
        else
        {
            const UINT_32 pitchAlignInElement = alignment / elementBytes;

            pitch        = PowTwoAlign(pIn->width, pitchAlignInElement);
            actualHeight = pIn->numMipLevels;

            if (pIn->flags.prt == FALSE)
            {
                returnCode = ApplyCustomizedPitchHeight(pIn, elementBytes, pitchAlignInElement,
                                                        &pitch, &actualHeight);
            }

            if (returnCode == ADDR_OK)
            {
                if (pOut->pMipInfo != NULL)
                {
                    for (UINT_32 i = 0; i < pIn->numMipLevels; i++)
                    {
                        pOut->pMipInfo[i].offset = pitch * elementBytes * i;
                        pOut->pMipInfo[i].pitch  = pitch;
                        pOut->pMipInfo[i].height = 1;
                        pOut->pMipInfo[i].depth  = 1;
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
        pOut->pitch          = pitch;
        pOut->height         = pIn->height;
        pOut->numSlices      = pIn->numSlices;
        pOut->mipChainPitch  = pitch;
        pOut->mipChainHeight = actualHeight;
        pOut->mipChainSlice  = pOut->numSlices;
        pOut->epitchIsHeight = (pIn->numMipLevels > 1) ? TRUE : FALSE;
        pOut->sliceSize      = static_cast<UINT_64>(pOut->pitch) * actualHeight * elementBytes;
        pOut->surfSize       = pOut->sliceSize * pOut->numSlices;
        pOut->baseAlign      = (pIn->swizzleMode == ADDR_SW_LINEAR_GENERAL) ? (pIn->bpp / 8) : alignment;
        pOut->blockWidth     = (pIn->swizzleMode == ADDR_SW_LINEAR_GENERAL) ? 1 : (256 / elementBytes);
        pOut->blockHeight    = 1;
        pOut->blockSlices    = 1;
    }

    // Post calculation validate
    ADDR_ASSERT(pOut->sliceSize > 0);

    return returnCode;
}

/**
************************************************************************************************************************
*   Gfx9Lib::GetMipChainInfo
*
*   @brief
*       Internal function to get out information about mip chain
*
*   @return
*       Smaller value between Id of first mip fitted in mip tail and max Id of mip being created
************************************************************************************************************************
*/
UINT_32 Gfx9Lib::GetMipChainInfo(
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

    UINT_32 mipPitch         = mip0Width;
    UINT_32 mipHeight        = mip0Height;
    UINT_32 mipDepth         = IsTex3d(resourceType) ? mip0Depth : 1;
    UINT_32 offset           = 0;
    UINT_32 firstMipIdInTail = numMipLevel;
    BOOL_32 inTail           = FALSE;
    BOOL_32 finalDim         = FALSE;
    BOOL_32 is3dThick        = IsThick(resourceType, swizzleMode);
    BOOL_32 is3dThin         = IsTex3d(resourceType) && (is3dThick == FALSE);

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
                        mipPitch  = Block256_3dZ[index].w;
                        mipHeight = Block256_3dZ[index].h;
                        mipDepth  = Block256_3dZ[index].d;
                    }
                    else
                    {
                        mipPitch  = Block256_2d[index].w;
                        mipHeight = Block256_2d[index].h;
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
                firstMipIdInTail = mipId;
                mipPitch         = tailMaxDim.w;
                mipHeight        = tailMaxDim.h;

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

        if (pMipInfo != NULL)
        {
            pMipInfo[mipId].pitch  = mipPitch;
            pMipInfo[mipId].height = mipHeight;
            pMipInfo[mipId].depth  = mipDepth;
            pMipInfo[mipId].offset = offset;
        }

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

    return firstMipIdInTail;
}

/**
************************************************************************************************************************
*   Gfx9Lib::GetMetaMiptailInfo
*
*   @brief
*       Get mip tail coordinate information.
*
*   @return
*       N/A
************************************************************************************************************************
*/
VOID Gfx9Lib::GetMetaMiptailInfo(
    ADDR2_META_MIP_INFO*    pInfo,          ///< [out] output structure to store per mip coord
    Dim3d                   mipCoord,       ///< [in] mip tail base coord
    UINT_32                 numMipInTail,   ///< [in] number of mips in tail
    Dim3d*                  pMetaBlkDim     ///< [in] meta block width/height/depth
    ) const
{
    BOOL_32 isThick   = (pMetaBlkDim->d > 1);
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
************************************************************************************************************************
*   Gfx9Lib::GetMipStartPos
*
*   @brief
*       Internal function to get out information about mip logical start position
*
*   @return
*       logical start position in macro block width/heith/depth of one mip level within one slice
************************************************************************************************************************
*/
Dim3d Gfx9Lib::GetMipStartPos(
    AddrResourceType  resourceType,
    AddrSwizzleMode   swizzleMode,
    UINT_32           width,
    UINT_32           height,
    UINT_32           depth,
    UINT_32           blockWidth,
    UINT_32           blockHeight,
    UINT_32           blockDepth,
    UINT_32           mipId,
    UINT_32           log2ElementBytes,
    UINT_32*          pMipTailBytesOffset) const
{
    Dim3d       mipStartPos = {0};
    const Dim3d tailMaxDim  = GetMipTailDim(resourceType, swizzleMode, blockWidth, blockHeight, blockDepth);

    // Report mip in tail if Mip0 is already in mip tail
    BOOL_32 inMipTail      = IsInMipTail(resourceType, swizzleMode, tailMaxDim, width, height, depth);
    UINT_32 log2blkSize    = GetBlockSizeLog2(swizzleMode);
    UINT_32 mipIndexInTail = mipId;

    if (inMipTail == FALSE)
    {
        // Mip 0 dimension, unit in block
        UINT_32 mipWidthInBlk   = width  / blockWidth;
        UINT_32 mipHeightInBlk  = height / blockHeight;
        UINT_32 mipDepthInBlk   = depth  / blockDepth;
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

            mipWidthInBlk  = RoundHalf(mipWidthInBlk);
            mipHeightInBlk = RoundHalf(mipHeightInBlk);
            mipDepthInBlk  = RoundHalf(mipDepthInBlk);
        }

        if (mipId >= endingMip)
        {
            inMipTail      = TRUE;
            mipIndexInTail = mipId - endingMip;
        }
    }

    if (inMipTail)
    {
        UINT_32 index = mipIndexInTail + MaxMacroBits - log2blkSize;
        ADDR_ASSERT(index < sizeof(MipTailOffset256B) / sizeof(UINT_32));
        *pMipTailBytesOffset = MipTailOffset256B[index] << 8;
    }

    return mipStartPos;
}

/**
************************************************************************************************************************
*   Gfx9Lib::HwlComputeSurfaceAddrFromCoordTiled
*
*   @brief
*       Internal function to calculate address from coord for tiled swizzle surface
*
*   @return
*       ADDR_E_RETURNCODE
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::HwlComputeSurfaceAddrFromCoordTiled(
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
    localIn.numSamples   = Max(pIn->numSamples, 1u);
    localIn.numFrags     = Max(pIn->numFrags, 1u);
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
        UINT_32 log2ElementBytes   = Log2(pIn->bpp >> 3);
        Dim3d   mipStartPos        = {0};
        UINT_32 mipTailBytesOffset = 0;

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
                                         log2ElementBytes,
                                         &mipTailBytesOffset);
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
                ADDR_ASSERT(log2ElementBytes < MaxNumOfBpp);
                Dim2d microBlockDim = Block256_2d[log2ElementBytes];
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

            ADDR_ASSERT((blockOffset | mipTailBytesOffset) == (blockOffset + mipTailBytesOffset));
            ADDR_ASSERT((mipTailBytesOffset == 0u) || (blockOffset < (1u << log2blkSize)));

            blockOffset |= mipTailBytesOffset;

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
            UINT_64 macroBlockIndex =
                (pIn->slice + mipStartPos.d) * sliceSizeInMacroBlock +
                ((pIn->y / localOut.blockHeight) + mipStartPos.h) * pitchInMacroBlock +
                ((pIn->x / localOut.blockWidth) + mipStartPos.w);

            pOut->addr = blockOffset | (macroBlockIndex << log2blkSize);
        }
        else
        {
            UINT_32 log2blkSize = GetBlockSizeLog2(pIn->swizzleMode);

            Dim3d microBlockDim = Block1K_3d[log2ElementBytes];

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

            ADDR_ASSERT((blockOffset | mipTailBytesOffset) == (blockOffset + mipTailBytesOffset));
            ADDR_ASSERT((mipTailBytesOffset == 0u) || (blockOffset < (1u << log2blkSize)));
            blockOffset |= mipTailBytesOffset;

            returnCode = ApplyCustomerPipeBankXor(pIn->swizzleMode, pIn->pipeBankXor,
                                                  bankBits, pipeBits, &blockOffset);

            blockOffset %= (1 << log2blkSize);

            UINT_32 xb = pIn->x / localOut.blockWidth  + mipStartPos.w;
            UINT_32 yb = pIn->y / localOut.blockHeight + mipStartPos.h;
            UINT_32 zb = pIn->slice / localOut.blockSlices + + mipStartPos.d;

            UINT_32 pitchInBlock = localOut.mipChainPitch / localOut.blockWidth;
            UINT_32 sliceSizeInBlock =
                (localOut.mipChainHeight / localOut.blockHeight) * pitchInBlock;
            UINT_64 blockIndex = zb * sliceSizeInBlock + yb * pitchInBlock + xb;

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
************************************************************************************************************************
*   Gfx9Lib::ComputeSurfaceInfoLinear
*
*   @brief
*       Internal function to calculate padding for linear swizzle 2D/3D surface
*
*   @return
*       N/A
************************************************************************************************************************
*/
ADDR_E_RETURNCODE Gfx9Lib::ComputeSurfaceLinearPadding(
    const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,                    ///< [in] input srtucture
    UINT_32*                                pMipmap0PaddedWidth,    ///< [out] padded width in element
    UINT_32*                                pSlice0PaddedHeight,    ///< [out] padded height for HW
    ADDR2_MIP_INFO*                         pMipInfo                ///< [out] per mip information
    ) const
{
    ADDR_E_RETURNCODE returnCode = ADDR_OK;

    UINT_32 elementBytes        = pIn->bpp >> 3;
    UINT_32 pitchAlignInElement = 0;

    if (pIn->swizzleMode == ADDR_SW_LINEAR_GENERAL)
    {
        ADDR_ASSERT(pIn->numMipLevels <= 1);
        ADDR_ASSERT(pIn->numSlices <= 1);
        pitchAlignInElement = 1;
    }
    else
    {
        pitchAlignInElement = (256 / elementBytes);
    }

    UINT_32 mipChainWidth      = PowTwoAlign(pIn->width, pitchAlignInElement);
    UINT_32 slice0PaddedHeight = pIn->height;

    returnCode = ApplyCustomizedPitchHeight(pIn, elementBytes, pitchAlignInElement,
                                            &mipChainWidth, &slice0PaddedHeight);

    if (returnCode == ADDR_OK)
    {
        UINT_32 mipChainHeight = 0;
        UINT_32 mipHeight      = pIn->height;

        for (UINT_32 i = 0; i < pIn->numMipLevels; i++)
        {
            if (pMipInfo != NULL)
            {
                pMipInfo[i].offset = mipChainWidth * mipChainHeight * elementBytes;
                pMipInfo[i].pitch  = mipChainWidth;
                pMipInfo[i].height = mipHeight;
                pMipInfo[i].depth  = 1;
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

} // V2
} // Addr
