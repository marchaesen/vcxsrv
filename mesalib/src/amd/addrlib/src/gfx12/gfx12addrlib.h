/*
************************************************************************************************************************
*
*  Copyright (C) 2022-2024 Advanced Micro Devices, Inc. All rights reserved.
*  SPDX-License-Identifier: MIT
*
***********************************************************************************************************************/

/**
************************************************************************************************************************
* @file  gfx12addrlib.h
* @brief Contains the Gfx12Lib class definition.
************************************************************************************************************************
*/

#ifndef __GFX12_ADDR_LIB_H__
#define __GFX12_ADDR_LIB_H__

#include "addrlib3.h"
#include "coord.h"
#include "gfx12SwizzlePattern.h"


namespace Addr
{
namespace V3
{

/**
************************************************************************************************************************
* @brief This class is the GFX12 specific address library
*        function set.
************************************************************************************************************************
*/
class Gfx12Lib : public Lib
{
public:
    /// Creates Gfx12Lib object
    static Addr::Lib* CreateObj(const Client* pClient)
    {
        VOID* pMem = Object::ClientAlloc(sizeof(Gfx12Lib), pClient);
        return (pMem != NULL) ? new (pMem) Gfx12Lib(pClient) : NULL;
    }

protected:
    Gfx12Lib(const Client* pClient);
    virtual ~Gfx12Lib();

    // Meta surfaces such as Hi-S/Z are essentially images on GFX12, so just return the max
    // image alignment.
    virtual UINT_32 HwlComputeMaxMetaBaseAlignments() const override { return 256 * 1024; }

    UINT_32 GetMaxNumMipsInTail(
        const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn) const;

    BOOL_32 IsInMipTail(
        const ADDR_EXTENT3D&  mipTailDim,       ///< The output of GetMipTailDim() function which is dimensions of the
                                                ///  largest mip level in the tail (again, only 4kb/64kb/256kb block).
        const ADDR_EXTENT3D&  mipDims,          ///< The dimensions of the mip level being queried now.
        INT_32                maxNumMipsInTail, ///< The output of GetMaxNumMipsInTail() function which is the maximal
                                                ///  number of the mip levels that could fit in the tail of larger
                                                ///  block.
        INT_32                numMipsToTheEnd   ///< This is (numMipLevels - mipIdx) and it may be negative when called
                                                ///  in SanityCheckSurfSize() since mipIdx has to be in [0, 16].
        ) const
    {
        BOOL_32 inTail = ((mipDims.width   <= mipTailDim.width)  &&
                          (mipDims.height  <= mipTailDim.height) &&
                          (numMipsToTheEnd <= maxNumMipsInTail));

        return inTail;
    }

    virtual ADDR_E_RETURNCODE HwlComputeSurfaceAddrFromCoordLinear(
        const ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,
        const ADDR3_COMPUTE_SURFACE_INFO_INPUT*          pSurfInfoIn,
        ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT*      pOut) const override;

    virtual ADDR_E_RETURNCODE HwlComputeSurfaceAddrFromCoordTiled(
        const ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,
        ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT*      pOut) const override;

    virtual ADDR_E_RETURNCODE HwlComputeNonBlockCompressedView(
        const ADDR3_COMPUTE_NONBLOCKCOMPRESSEDVIEW_INPUT* pIn,
        ADDR3_COMPUTE_NONBLOCKCOMPRESSEDVIEW_OUTPUT*      pOut) const override;

    virtual VOID HwlComputeSubResourceOffsetForSwizzlePattern(
        const ADDR3_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_INPUT* pIn,
        ADDR3_COMPUTE_SUBRESOURCE_OFFSET_FORSWIZZLEPATTERN_OUTPUT*      pOut) const override;

    virtual ADDR_E_RETURNCODE HwlComputeSlicePipeBankXor(
        const ADDR3_COMPUTE_SLICE_PIPEBANKXOR_INPUT* pIn,
        ADDR3_COMPUTE_SLICE_PIPEBANKXOR_OUTPUT*      pOut) const override;

    virtual UINT_32 HwlGetEquationIndex(
        const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pIn) const override;

    virtual UINT_32 HwlGetEquationTableInfo(const ADDR_EQUATION** ppEquationTable) const override
    {
        *ppEquationTable = m_equationTable;

        return m_numEquations;
    }

    BOOL_32 HwlValidateNonSwModeParams(const ADDR3_GET_POSSIBLE_SWIZZLE_MODE_INPUT* pIn) const override;

    virtual ADDR_E_RETURNCODE HwlGetPossibleSwizzleModes(
        const ADDR3_GET_POSSIBLE_SWIZZLE_MODE_INPUT*   pIn,
        ADDR3_GET_POSSIBLE_SWIZZLE_MODE_OUTPUT*        pOut) const override;

    virtual ChipFamily HwlConvertChipFamily(UINT_32 uChipFamily, UINT_32 uChipRevision) override;

protected:
    virtual VOID HwlCalcBlockSize(
        const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,
        ADDR_EXTENT3D*                                 pExtent) const override final;

    ADDR_EXTENT3D HwlGetMicroBlockSize(
        const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn) const;

    virtual ADDR_EXTENT3D HwlGetMipInTailMaxSize(
        const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,
        const ADDR_EXTENT3D&                           blockDims) const override final;

private:
    static const SwizzleModeFlags SwizzleModeTable[ADDR3_MAX_TYPE];

    // Number of unique swizzle patterns (one entry per swizzle mode + MSAA + bpp configuration)
    static const UINT_32 NumSwizzlePatterns  = 19 * MaxElementBytesLog2;

    // Equation table
    ADDR_EQUATION        m_equationTable[NumSwizzlePatterns];

    /**
    ************************************************************************************************************************
    * @brief Bitmasks for swizzle mode determination on GFX12
    ************************************************************************************************************************
    */
    static const UINT_32 Blk256KBSwModeMask = (1u << ADDR3_256KB_2D)  |
                                              (1u << ADDR3_256KB_3D);
    static const UINT_32 Blk64KBSwModeMask  = (1u << ADDR3_64KB_2D)   |
                                              (1u << ADDR3_64KB_3D);
    static const UINT_32 Blk4KBSwModeMask   = (1u << ADDR3_4KB_2D)    |
                                              (1u << ADDR3_4KB_3D);
    static const UINT_32 Blk256BSwModeMask  = (1u << ADDR3_256B_2D);

    static const UINT_32 MaxImageDim  = 32768; // Max image size is 32k
    static const UINT_32 MaxMipLevels = 16;

    virtual ADDR_E_RETURNCODE HwlComputePipeBankXor(
        const ADDR3_COMPUTE_PIPEBANKXOR_INPUT* pIn,
        ADDR3_COMPUTE_PIPEBANKXOR_OUTPUT*      pOut) const override;

    virtual BOOL_32 HwlInitGlobalParams(const ADDR_CREATE_INPUT* pCreateIn) override;

    virtual ADDR_E_RETURNCODE HwlComputeStereoInfo(
        const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pIn,
        UINT_32*                                pAlignY,
        UINT_32*                                pRightXor) const override;

    void SanityCheckSurfSize(
        const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT*   pIn,
        const ADDR3_COMPUTE_SURFACE_INFO_OUTPUT*         pOut) const;

    virtual ADDR_E_RETURNCODE HwlCopyMemToSurface(
        const ADDR3_COPY_MEMSURFACE_INPUT*  pIn,
        const ADDR3_COPY_MEMSURFACE_REGION* pRegions,
        UINT_32                             regionCount) const override;

    virtual ADDR_E_RETURNCODE HwlCopySurfaceToMem(
        const ADDR3_COPY_MEMSURFACE_INPUT*  pIn,
        const ADDR3_COPY_MEMSURFACE_REGION* pRegions,
        UINT_32                             regionCount) const override;

    UINT_32           m_numSwizzleBits;

    // Initialize equation table
    VOID InitEquationTable();

    // Initialize block dimension table
    VOID InitBlockDimensionTable();

    VOID GetSwizzlePatternFromPatternInfo(
        const ADDR_SW_PATINFO* pPatInfo,
        ADDR_BIT_SETTING       (&pSwizzle)[Log2Size256K]) const
    {
        memcpy(pSwizzle,
               GFX12_SW_PATTERN_NIBBLE1[pPatInfo->nibble1Idx],
               sizeof(GFX12_SW_PATTERN_NIBBLE1[pPatInfo->nibble1Idx]));

        memcpy(&pSwizzle[8],
               GFX12_SW_PATTERN_NIBBLE2[pPatInfo->nibble2Idx],
               sizeof(GFX12_SW_PATTERN_NIBBLE2[pPatInfo->nibble2Idx]));

        memcpy(&pSwizzle[12],
               GFX12_SW_PATTERN_NIBBLE3[pPatInfo->nibble3Idx],
               sizeof(GFX12_SW_PATTERN_NIBBLE3[pPatInfo->nibble3Idx]));

        memcpy(&pSwizzle[16],
               GFX12_SW_PATTERN_NIBBLE4[pPatInfo->nibble4Idx],
               sizeof(GFX12_SW_PATTERN_NIBBLE4[pPatInfo->nibble4Idx]));
    }

    VOID ConvertSwizzlePatternToEquation(
        UINT_32                elemLog2,
        Addr3SwizzleMode       swMode,
        const ADDR_SW_PATINFO* pPatInfo,
        ADDR_EQUATION*         pEquation) const;

    ADDR_EXTENT3D GetBaseMipExtents(
        const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pIn) const;

    INT_32 CalcMipInTail(
        const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT*  pIn,
        const ADDR3_COMPUTE_SURFACE_INFO_OUTPUT*        pOut,
        UINT_32                                         mipLevel) const;

    UINT_32 CalcMipOffset(
        const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,
        UINT_32                                        mipInTail) const;

    virtual ADDR_E_RETURNCODE HwlComputeSurfaceInfo(
         const ADDR3_COMPUTE_SURFACE_INFO_INPUT* pIn,
         ADDR3_COMPUTE_SURFACE_INFO_OUTPUT*      pOut) const override;

    static ADDR_EXTENT3D GetMipExtent(
        const ADDR_EXTENT3D&  mip0,
        UINT_32               mipId)
    {
        return {
            ShiftCeil(Max(mip0.width, 1u),  mipId),
            ShiftCeil(Max(mip0.height, 1u), mipId),
            ShiftCeil(Max(mip0.depth, 1u),  mipId)
        };
    }

    UINT_32 ComputeOffsetFromEquation(
        const ADDR_EQUATION* pEq,
        UINT_32              x,
        UINT_32              y,
        UINT_32              z,
        UINT_32              s) const;

    const ADDR_SW_PATINFO* GetSwizzlePatternInfo(
        Addr3SwizzleMode swizzleMode,
        UINT_32          log2Elem,
        UINT_32          numFrag) const;

    VOID GetMipOffset(
         const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,
         ADDR3_COMPUTE_SURFACE_INFO_OUTPUT*             pOut) const;

    VOID GetMipOrigin(
         const ADDR3_COMPUTE_SURFACE_INFO_PARAMS_INPUT* pIn,
         const ADDR_EXTENT3D&                           mipExtentFirstInTail,
         ADDR3_COMPUTE_SURFACE_INFO_OUTPUT*             pOut) const;

};

} // V3
} // Addr

#endif
