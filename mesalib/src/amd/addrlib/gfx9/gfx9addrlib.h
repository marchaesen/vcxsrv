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
* @file  gfx9addrlib.h
* @brief Contgfx9ns the Gfx9Lib class definition.
****************************************************************************************************
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
****************************************************************************************************
* @brief GFX9 specific settings structure.
****************************************************************************************************
*/
struct Gfx9ChipSettings
{
    struct
    {
        // Asic/Generation name
        UINT_32 isArcticIsland      : 1;
        UINT_32 isVega10            : 1;
        UINT_32 reserved0           : 30;

        // Display engine IP version name
        UINT_32 isDce12             : 1;
        UINT_32 reserved1           : 31;

        // Misc configuration bits
        UINT_32 metaBaseAlignFix    : 1;
        UINT_32 reserved2           : 31;
    };
};

/**
****************************************************************************************************
* @brief GFX9 data surface type.
****************************************************************************************************
*/
enum Gfx9DataType
{
    Gfx9DataColor,
    Gfx9DataDepthStencil,
    Gfx9DataFmask
};

/**
****************************************************************************************************
* @brief This class is the GFX9 specific address library
*        function set.
****************************************************************************************************
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

    virtual ADDR_E_RETURNCODE HwlComputeHtileInfo(
        const ADDR2_COMPUTE_HTILE_INFO_INPUT* pIn,
        ADDR2_COMPUTE_HTILE_INFO_OUTPUT* pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeCmaskInfo(
        const ADDR2_COMPUTE_CMASK_INFO_INPUT* pIn,
        ADDR2_COMPUTE_CMASK_INFO_OUTPUT* pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeDccInfo(
        const ADDR2_COMPUTE_DCCINFO_INPUT* pIn,
        ADDR2_COMPUTE_DCCINFO_OUTPUT* pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeCmaskAddrFromCoord(
        const ADDR2_COMPUTE_CMASK_ADDRFROMCOORD_INPUT*  pIn,
        ADDR2_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT* pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeHtileAddrFromCoord(
        const ADDR2_COMPUTE_HTILE_ADDRFROMCOORD_INPUT*  pIn,
        ADDR2_COMPUTE_HTILE_ADDRFROMCOORD_OUTPUT* pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeHtileCoordFromAddr(
        const ADDR2_COMPUTE_HTILE_COORDFROMADDR_INPUT*  pIn,
        ADDR2_COMPUTE_HTILE_COORDFROMADDR_OUTPUT* pOut) const;

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

    virtual UINT_32 HwlComputeSurfaceBaseAlign(AddrSwizzleMode swizzleMode) const
    {
        UINT_32 baseAlign;

        if (IsXor(swizzleMode))
        {
            if (m_settings.isVega10)
            {
                baseAlign = GetBlockSize(swizzleMode);
            }
            else
            {
                UINT_32 blockSizeLog2 = GetBlockSizeLog2(swizzleMode);
                UINT_32 pipeBits = GetPipeXorBits(blockSizeLog2);
                UINT_32 bankBits = GetBankXorBits(blockSizeLog2);
                baseAlign = 1 << (Min(blockSizeLog2, m_pipeInterleaveLog2 + pipeBits+ bankBits));
            }
        }
        else
        {
            baseAlign = 256;
        }

        return baseAlign;
    }

    virtual BOOL_32 HwlIsValidDisplaySwizzleMode(const ADDR2_COMPUTE_SURFACE_INFO_INPUT* pIn) const;

    virtual BOOL_32 HwlIsDce12() const { return m_settings.isDce12; }

    // Initialize equation table
    VOID InitEquationTable();

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

private:
    virtual ADDR_E_RETURNCODE HwlGetMaxAlignments(
        ADDR_GET_MAX_ALINGMENTS_OUTPUT* pOut) const;

    virtual BOOL_32 HwlInitGlobalParams(
        const ADDR_CREATE_INPUT* pCreateIn);

    static VOID GetRbEquation(CoordEq* pRbEq, UINT_32 rbPerSeLog2, UINT_32 seLog2);

    VOID GetDataEquation(CoordEq* pDataEq, Gfx9DataType dataSurfaceType,
                         AddrSwizzleMode swizzleMode, AddrResourceType resourceType,
                         UINT_32 elementBytesLog2, UINT_32 numSamplesLog2) const;

    VOID GetPipeEquation(CoordEq* pPipeEq, CoordEq* pDataEq,
                         UINT_32 pipeInterleaveLog2, UINT_32 numPipesLog2,
                         UINT_32 numSamplesLog2, Gfx9DataType dataSurfaceType,
                         AddrSwizzleMode swizzleMode, AddrResourceType resourceType) const;

    VOID GetMetaEquation(CoordEq* pMetaEq, UINT_32 maxMip,
                         UINT_32 elementBytesLog2, UINT_32 numSamplesLog2,
                         ADDR2_META_FLAGS metaFlag, Gfx9DataType dataSurfaceType,
                         AddrSwizzleMode swizzleMode, AddrResourceType resourceType,
                         UINT_32 metaBlkWidthLog2, UINT_32 metaBlkHeightLog2,
                         UINT_32 metaBlkDepthLog2, UINT_32 compBlkWidthLog2,
                         UINT_32 compBlkHeightLog2, UINT_32 compBlkDepthLog2) const;

    virtual ChipFamily HwlConvertChipFamily(UINT_32 uChipFamily, UINT_32 uChipRevision);

    VOID GetMetaMipInfo(UINT_32 numMipLevels, Dim3d* pMetaBlkDim,
                        BOOL_32 dataThick, ADDR2_META_MIP_INFO* pInfo,
                        UINT_32 mip0Width, UINT_32 mip0Height, UINT_32 mip0Depth,
                        UINT_32* pNumMetaBlkX, UINT_32* pNumMetaBlkY, UINT_32* pNumMetaBlkZ) const;

    Gfx9ChipSettings m_settings;
};

} // V2
} // Addr

#endif

