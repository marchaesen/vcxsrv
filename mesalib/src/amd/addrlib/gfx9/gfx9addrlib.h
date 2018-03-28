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
* @file  gfx9addrlib.h
* @brief Contgfx9ns the Gfx9Lib class definition.
************************************************************************************************************************
*/

#ifndef __GFX9_ADDR_LIB_H__
#define __GFX9_ADDR_LIB_H__

#include "addrlib2.h"
#include "coord.h"

namespace Addr
{
namespace V2
{

/**
************************************************************************************************************************
* @brief GFX9 specific settings structure.
************************************************************************************************************************
*/
struct Gfx9ChipSettings
{
    struct
    {
        // Asic/Generation name
        UINT_32 isArcticIsland      : 1;
        UINT_32 isVega10            : 1;
        UINT_32 isRaven             : 1;
        UINT_32 isVega12            : 1;

        // Display engine IP version name
        UINT_32 isDce12             : 1;
        UINT_32 isDcn1              : 1;

        // Misc configuration bits
        UINT_32 metaBaseAlignFix    : 1;
        UINT_32 depthPipeXorDisable : 1;
        UINT_32 htileAlignFix       : 1;
        UINT_32 applyAliasFix       : 1;
        UINT_32 htileCacheRbConflict: 1;
        UINT_32 reserved2           : 27;
    };
};

/**
************************************************************************************************************************
* @brief GFX9 data surface type.
************************************************************************************************************************
*/
enum Gfx9DataType
{
    Gfx9DataColor,
    Gfx9DataDepthStencil,
    Gfx9DataFmask
};

/**
************************************************************************************************************************
* @brief GFX9 meta equation parameters
************************************************************************************************************************
*/
struct MetaEqParams
{
    UINT_32          maxMip;
    UINT_32          elementBytesLog2;
    UINT_32          numSamplesLog2;
    ADDR2_META_FLAGS metaFlag;
    Gfx9DataType     dataSurfaceType;
    AddrSwizzleMode  swizzleMode;
    AddrResourceType resourceType;
    UINT_32          metaBlkWidthLog2;
    UINT_32          metaBlkHeightLog2;
    UINT_32          metaBlkDepthLog2;
    UINT_32          compBlkWidthLog2;
    UINT_32          compBlkHeightLog2;
    UINT_32          compBlkDepthLog2;
};

/**
************************************************************************************************************************
* @brief This class is the GFX9 specific address library
*        function set.
************************************************************************************************************************
*/
class Gfx9Lib : public Lib
{
public:
    /// Creates Gfx9Lib object
    static Addr::Lib* CreateObj(const Client* pClient)
    {
        VOID* pMem = Object::ClientAlloc(sizeof(Gfx9Lib), pClient);
        return (pMem != NULL) ? new (pMem) Gfx9Lib(pClient) : NULL;
    }

protected:
    Gfx9Lib(const Client* pClient);
    virtual ~Gfx9Lib();

    virtual BOOL_32 HwlIsStandardSwizzle(
        AddrResourceType resourceType,
        AddrSwizzleMode  swizzleMode) const
    {
        return m_swizzleModeTable[swizzleMode].isStd ||
               (IsTex3d(resourceType) && m_swizzleModeTable[swizzleMode].isDisp);
    }

    virtual BOOL_32 HwlIsDisplaySwizzle(
        AddrResourceType resourceType,
        AddrSwizzleMode  swizzleMode) const
    {
        return IsTex2d(resourceType) && m_swizzleModeTable[swizzleMode].isDisp;
    }

    virtual BOOL_32 HwlIsThin(
        AddrResourceType resourceType,
        AddrSwizzleMode  swizzleMode) const
    {
        return ((IsTex2d(resourceType)  == TRUE) ||
                ((IsTex3d(resourceType) == TRUE)                  &&
                 (m_swizzleModeTable[swizzleMode].isZ   == FALSE) &&
                 (m_swizzleModeTable[swizzleMode].isStd == FALSE)));
    }

    virtual BOOL_32 HwlIsThick(
        AddrResourceType resourceType,
        AddrSwizzleMode  swizzleMode) const
    {
        return (IsTex3d(resourceType) &&
                (m_swizzleModeTable[swizzleMode].isZ || m_swizzleModeTable[swizzleMode].isStd));
    }

    virtual ADDR_E_RETURNCODE HwlComputeHtileInfo(
        const ADDR2_COMPUTE_HTILE_INFO_INPUT* pIn,
        ADDR2_COMPUTE_HTILE_INFO_OUTPUT*      pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeCmaskInfo(
        const ADDR2_COMPUTE_CMASK_INFO_INPUT* pIn,
        ADDR2_COMPUTE_CMASK_INFO_OUTPUT*      pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeDccInfo(
        const ADDR2_COMPUTE_DCCINFO_INPUT* pIn,
        ADDR2_COMPUTE_DCCINFO_OUTPUT*      pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeCmaskAddrFromCoord(
        const ADDR2_COMPUTE_CMASK_ADDRFROMCOORD_INPUT* pIn,
        ADDR2_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT*      pOut);

    virtual ADDR_E_RETURNCODE HwlComputeHtileAddrFromCoord(
        const ADDR2_COMPUTE_HTILE_ADDRFROMCOORD_INPUT* pIn,
        ADDR2_COMPUTE_HTILE_ADDRFROMCOORD_OUTPUT*      pOut);

    virtual ADDR_E_RETURNCODE HwlComputeHtileCoordFromAddr(
        const ADDR2_COMPUTE_HTILE_COORDFROMADDR_INPUT* pIn,
        ADDR2_COMPUTE_HTILE_COORDFROMADDR_OUTPUT*      pOut);

    virtual ADDR_E_RETURNCODE HwlComputeDccAddrFromCoord(
        const ADDR2_COMPUTE_DCC_ADDRFROMCOORD_INPUT* pIn,
        ADDR2_COMPUTE_DCC_ADDRFROMCOORD_OUTPUT*      pOut);

    virtual UINT_32 HwlGetEquationIndex(
        const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,
        ADDR2_COMPUTE_SURFACE_INFO_OUTPUT*      pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeBlock256Equation(
        AddrResourceType rsrcType,
        AddrSwizzleMode swMode,
        UINT_32 elementBytesLog2,
        ADDR_EQUATION* pEquation) const;

    virtual ADDR_E_RETURNCODE HwlComputeThinEquation(
        AddrResourceType rsrcType,
        AddrSwizzleMode swMode,
        UINT_32 elementBytesLog2,
        ADDR_EQUATION* pEquation) const;

    virtual ADDR_E_RETURNCODE HwlComputeThickEquation(
        AddrResourceType rsrcType,
        AddrSwizzleMode swMode,
        UINT_32 elementBytesLog2,
        ADDR_EQUATION* pEquation) const;

    // Get equation table pointer and number of equations
    virtual UINT_32 HwlGetEquationTableInfo(const ADDR_EQUATION** ppEquationTable) const
    {
        *ppEquationTable = m_equationTable;

        return m_numEquations;
    }

    virtual BOOL_32 IsEquationSupported(
        AddrResourceType rsrcType,
        AddrSwizzleMode swMode,
        UINT_32 elementBytesLog2) const;

    UINT_32 ComputeSurfaceBaseAlignTiled(AddrSwizzleMode swizzleMode) const
    {
        UINT_32 baseAlign;

        if (IsXor(swizzleMode))
        {
            baseAlign = GetBlockSize(swizzleMode);
        }
        else
        {
            baseAlign = 256;
        }

        return baseAlign;
    }

    virtual ADDR_E_RETURNCODE HwlComputePipeBankXor(
        const ADDR2_COMPUTE_PIPEBANKXOR_INPUT* pIn,
        ADDR2_COMPUTE_PIPEBANKXOR_OUTPUT*      pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeSlicePipeBankXor(
        const ADDR2_COMPUTE_SLICE_PIPEBANKXOR_INPUT* pIn,
        ADDR2_COMPUTE_SLICE_PIPEBANKXOR_OUTPUT*      pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeSubResourceOffsetForSwizzlePattern(
        const ADDR2_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_INPUT* pIn,
        ADDR2_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_OUTPUT*      pOut) const;

    virtual ADDR_E_RETURNCODE HwlGetPreferredSurfaceSetting(
        const ADDR2_GET_PREFERRED_SURF_SETTING_INPUT* pIn,
        ADDR2_GET_PREFERRED_SURF_SETTING_OUTPUT*      pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeSurfaceInfoSanityCheck(
        const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn) const;

    virtual ADDR_E_RETURNCODE HwlComputeSurfaceInfoTiled(
         const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,
         ADDR2_COMPUTE_SURFACE_INFO_OUTPUT*      pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeSurfaceInfoLinear(
         const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,
         ADDR2_COMPUTE_SURFACE_INFO_OUTPUT*      pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeSurfaceAddrFromCoordTiled(
        const ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,
        ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT*      pOut) const;

    // Initialize equation table
    VOID InitEquationTable();

    ADDR_E_RETURNCODE ComputeStereoInfo(
        const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,
        ADDR2_COMPUTE_SURFACE_INFO_OUTPUT*      pOut,
        UINT_32*                                pHeightAlign) const;

    UINT_32 GetMipChainInfo(
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
        ADDR2_MIP_INFO*   pMipInfo) const;

    VOID GetMetaMiptailInfo(
        ADDR2_META_MIP_INFO*    pInfo,
        Dim3d                   mipCoord,
        UINT_32                 numMipInTail,
        Dim3d*                  pMetaBlkDim) const;

    Dim3d GetMipStartPos(
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
        UINT_32*          pMipTailBytesOffset) const;

    AddrMajorMode GetMajorMode(
        AddrResourceType resourceType,
        AddrSwizzleMode  swizzleMode,
        UINT_32          mip0WidthInBlk,
        UINT_32          mip0HeightInBlk,
        UINT_32          mip0DepthInBlk) const
    {
        BOOL_32 yMajor = (mip0WidthInBlk < mip0HeightInBlk);
        BOOL_32 xMajor = (yMajor == FALSE);

        if (IsThick(resourceType, swizzleMode))
        {
            yMajor = yMajor && (mip0HeightInBlk >= mip0DepthInBlk);
            xMajor = xMajor && (mip0WidthInBlk >= mip0DepthInBlk);
        }

        AddrMajorMode majorMode;
        if (xMajor)
        {
            majorMode = ADDR_MAJOR_X;
        }
        else if (yMajor)
        {
            majorMode = ADDR_MAJOR_Y;
        }
        else
        {
            majorMode = ADDR_MAJOR_Z;
        }

        return majorMode;
    }

    Dim3d GetDccCompressBlk(
        AddrResourceType resourceType,
        AddrSwizzleMode  swizzleMode,
        UINT_32          bpp) const
    {
        UINT_32 index = Log2(bpp >> 3);
        Dim3d   compressBlkDim;

        if (IsThin(resourceType, swizzleMode))
        {
            compressBlkDim.w = Block256_2d[index].w;
            compressBlkDim.h = Block256_2d[index].h;
            compressBlkDim.d = 1;
        }
        else if (IsStandardSwizzle(resourceType, swizzleMode))
        {
            compressBlkDim = Block256_3dS[index];
        }
        else
        {
            compressBlkDim = Block256_3dZ[index];
        }

        return compressBlkDim;
    }


    static const UINT_32          MaxSeLog2      = 3;
    static const UINT_32          MaxRbPerSeLog2 = 2;

    static const Dim3d            Block256_3dS[MaxNumOfBpp];
    static const Dim3d            Block256_3dZ[MaxNumOfBpp];

    static const UINT_32          MipTailOffset256B[];

    static const SwizzleModeFlags SwizzleModeTable[ADDR_SW_MAX_TYPE];

    // Max number of swizzle mode supported for equation
    static const UINT_32    MaxSwMode = 32;
    // Max number of resource type (2D/3D) supported for equation
    static const UINT_32    MaxRsrcType = 2;
    // Max number of bpp (8bpp/16bpp/32bpp/64bpp/128bpp)
    static const UINT_32    MaxElementBytesLog2  = 5;
    // Almost all swizzle mode + resource type support equation
    static const UINT_32    EquationTableSize = MaxElementBytesLog2 * MaxSwMode * MaxRsrcType;
    // Equation table
    ADDR_EQUATION           m_equationTable[EquationTableSize];

    // Number of equation entries in the table
    UINT_32                 m_numEquations;
    // Equation lookup table according to bpp and tile index
    UINT_32                 m_equationLookupTable[MaxRsrcType][MaxSwMode][MaxElementBytesLog2];

    static const UINT_32    MaxCachedMetaEq = 2;

private:
    virtual UINT_32 HwlComputeMaxBaseAlignments() const;

    virtual UINT_32 HwlComputeMaxMetaBaseAlignments() const;

    virtual BOOL_32 HwlInitGlobalParams(const ADDR_CREATE_INPUT* pCreateIn);

    VOID GetRbEquation(CoordEq* pRbEq, UINT_32 rbPerSeLog2, UINT_32 seLog2) const;

    VOID GetDataEquation(CoordEq* pDataEq, Gfx9DataType dataSurfaceType,
                         AddrSwizzleMode swizzleMode, AddrResourceType resourceType,
                         UINT_32 elementBytesLog2, UINT_32 numSamplesLog2) const;

    VOID GetPipeEquation(CoordEq* pPipeEq, CoordEq* pDataEq,
                         UINT_32 pipeInterleaveLog2, UINT_32 numPipesLog2,
                         UINT_32 numSamplesLog2, Gfx9DataType dataSurfaceType,
                         AddrSwizzleMode swizzleMode, AddrResourceType resourceType) const;

    VOID GenMetaEquation(CoordEq* pMetaEq, UINT_32 maxMip,
                         UINT_32 elementBytesLog2, UINT_32 numSamplesLog2,
                         ADDR2_META_FLAGS metaFlag, Gfx9DataType dataSurfaceType,
                         AddrSwizzleMode swizzleMode, AddrResourceType resourceType,
                         UINT_32 metaBlkWidthLog2, UINT_32 metaBlkHeightLog2,
                         UINT_32 metaBlkDepthLog2, UINT_32 compBlkWidthLog2,
                         UINT_32 compBlkHeightLog2, UINT_32 compBlkDepthLog2) const;

    const CoordEq* GetMetaEquation(const MetaEqParams& metaEqParams);

    virtual ChipFamily HwlConvertChipFamily(UINT_32 uChipFamily, UINT_32 uChipRevision);

    VOID GetMetaMipInfo(UINT_32 numMipLevels, Dim3d* pMetaBlkDim,
                        BOOL_32 dataThick, ADDR2_META_MIP_INFO* pInfo,
                        UINT_32 mip0Width, UINT_32 mip0Height, UINT_32 mip0Depth,
                        UINT_32* pNumMetaBlkX, UINT_32* pNumMetaBlkY, UINT_32* pNumMetaBlkZ) const;

    BOOL_32 IsValidDisplaySwizzleMode(const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn) const;

    ADDR_E_RETURNCODE ComputeSurfaceLinearPadding(
        const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn,
        UINT_32*                                pMipmap0PaddedWidth,
        UINT_32*                                pSlice0PaddedHeight,
        ADDR2_MIP_INFO*                         pMipInfo = NULL) const;

    Gfx9ChipSettings m_settings;

    CoordEq      m_cachedMetaEq[MaxCachedMetaEq];
    MetaEqParams m_cachedMetaEqKey[MaxCachedMetaEq];
    UINT_32      m_metaEqOverrideIndex;
};

} // V2
} // Addr

#endif

