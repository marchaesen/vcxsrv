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
* @file  ciaddrlib.h
* @brief Contains the CIAddrLib class definition.
***************************************************************************************************
*/

#ifndef __CI_ADDR_LIB_H__
#define __CI_ADDR_LIB_H__

#include "addrlib.h"
#include "siaddrlib.h"

/**
***************************************************************************************************
* @brief CI specific settings structure.
***************************************************************************************************
*/
struct CIChipSettings
{
    struct
    {
        UINT_32 isSeaIsland : 1;
        UINT_32 isBonaire   : 1;
        UINT_32 isKaveri    : 1;
        UINT_32 isSpectre   : 1;
        UINT_32 isSpooky    : 1;
        UINT_32 isKalindi   : 1;
        // Hawaii is GFXIP 7.2, similar with CI (Bonaire)
        UINT_32 isHawaii    : 1;

        // VI
        UINT_32 isVolcanicIslands : 1;
        UINT_32 isIceland         : 1;
        UINT_32 isTonga           : 1;
        UINT_32 isFiji            : 1;
        UINT_32 isPolaris10       : 1;
        UINT_32 isPolaris11       : 1;
        UINT_32 isPolaris12       : 1;
        // VI fusion (Carrizo)
        UINT_32 isCarrizo         : 1;
    };
};

/**
***************************************************************************************************
* @brief This class is the CI specific address library
*        function set.
***************************************************************************************************
*/
class CIAddrLib : public SIAddrLib
{
public:
    /// Creates CIAddrLib object
    static AddrLib* CreateObj(const AddrClient* pClient)
    {
        return new(pClient) CIAddrLib(pClient);
    }

private:
    CIAddrLib(const AddrClient* pClient);
    virtual ~CIAddrLib();

protected:

    // Hwl interface - defined in AddrLib
    virtual ADDR_E_RETURNCODE HwlComputeSurfaceInfo(
        const ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn,
        ADDR_COMPUTE_SURFACE_INFO_OUTPUT* pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeFmaskInfo(
        const ADDR_COMPUTE_FMASK_INFO_INPUT* pIn,
        ADDR_COMPUTE_FMASK_INFO_OUTPUT* pOut);

    virtual AddrChipFamily HwlConvertChipFamily(
        UINT_32 uChipFamily, UINT_32 uChipRevision);

    virtual BOOL_32 HwlInitGlobalParams(
        const ADDR_CREATE_INPUT* pCreateIn);

    virtual ADDR_E_RETURNCODE HwlSetupTileCfg(
        INT_32 index, INT_32 macroModeIndex, ADDR_TILEINFO* pInfo,
        AddrTileMode* pMode = 0, AddrTileType* pType = 0) const;

    virtual VOID HwlComputeTileDataWidthAndHeightLinear(
        UINT_32* pMacroWidth, UINT_32* pMacroHeight,
        UINT_32 bpp, ADDR_TILEINFO* pTileInfo) const;

    virtual INT_32 HwlComputeMacroModeIndex(
        INT_32 tileIndex, ADDR_SURFACE_FLAGS flags, UINT_32 bpp, UINT_32 numSamples,
        ADDR_TILEINFO* pTileInfo, AddrTileMode* pTileMode = NULL, AddrTileType* pTileType = NULL
        ) const;

    // Sub-hwl interface - defined in EgBasedAddrLib
    virtual VOID HwlSetupTileInfo(
        AddrTileMode tileMode, ADDR_SURFACE_FLAGS flags,
        UINT_32 bpp, UINT_32 pitch, UINT_32 height, UINT_32 numSamples,
        ADDR_TILEINFO* inputTileInfo, ADDR_TILEINFO* outputTileInfo,
        AddrTileType inTileType, ADDR_COMPUTE_SURFACE_INFO_OUTPUT* pOut) const;

    virtual INT_32 HwlPostCheckTileIndex(
        const ADDR_TILEINFO* pInfo, AddrTileMode mode, AddrTileType type,
        INT curIndex = TileIndexInvalid) const;

    virtual VOID   HwlFmaskPreThunkSurfInfo(
        const ADDR_COMPUTE_FMASK_INFO_INPUT* pFmaskIn,
        const ADDR_COMPUTE_FMASK_INFO_OUTPUT* pFmaskOut,
        ADDR_COMPUTE_SURFACE_INFO_INPUT* pSurfIn,
        ADDR_COMPUTE_SURFACE_INFO_OUTPUT* pSurfOut) const;

    virtual VOID   HwlFmaskPostThunkSurfInfo(
        const ADDR_COMPUTE_SURFACE_INFO_OUTPUT* pSurfOut,
        ADDR_COMPUTE_FMASK_INFO_OUTPUT* pFmaskOut) const;

    virtual AddrTileMode HwlDegradeThickTileMode(
        AddrTileMode baseTileMode, UINT_32 numSlices, UINT_32* pBytesPerTile) const;

    virtual BOOL_32 HwlOverrideTileMode(
        const ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn,
        AddrTileMode* pTileMode,
        AddrTileType* pTileType) const;

    virtual BOOL_32 HwlStereoCheckRightOffsetPadding() const;

    virtual ADDR_E_RETURNCODE HwlComputeDccInfo(
        const ADDR_COMPUTE_DCCINFO_INPUT* pIn,
        ADDR_COMPUTE_DCCINFO_OUTPUT* pOut) const;

    virtual ADDR_E_RETURNCODE HwlComputeCmaskAddrFromCoord(
        const ADDR_COMPUTE_CMASK_ADDRFROMCOORD_INPUT* pIn,
        ADDR_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT* pOut) const;

protected:
    virtual VOID HwlPadDimensions(
        AddrTileMode tileMode, UINT_32 bpp, ADDR_SURFACE_FLAGS flags,
        UINT_32 numSamples, ADDR_TILEINFO* pTileInfo, UINT_32 padDims, UINT_32 mipLevel,
        UINT_32* pPitch, UINT_32 pitchAlign, UINT_32* pHeight, UINT_32 heightAlign,
        UINT_32* pSlices, UINT_32 sliceAlign) const;

private:
    VOID ReadGbTileMode(
        UINT_32 regValue, ADDR_TILECONFIG* pCfg) const;

    VOID ReadGbMacroTileCfg(
        UINT_32 regValue, ADDR_TILEINFO* pCfg) const;

    UINT_32 GetPrtSwitchP4Threshold() const;

    BOOL_32 InitTileSettingTable(
        const UINT_32 *pSetting, UINT_32 noOfEntries);

    BOOL_32 InitMacroTileCfgTable(
        const UINT_32 *pSetting, UINT_32 noOfEntries);

    UINT_64 HwlComputeMetadataNibbleAddress(
        UINT_64 uncompressedDataByteAddress,
        UINT_64 dataBaseByteAddress,
        UINT_64 metadataBaseByteAddress,
        UINT_32 metadataBitSize,
        UINT_32 elementBitSize,
        UINT_32 blockByteSize,
        UINT_32 pipeInterleaveBytes,
        UINT_32 numOfPipes,
        UINT_32 numOfBanks,
        UINT_32 numOfSamplesPerSplit) const;

    static const UINT_32    MacroTileTableSize = 16;
    ADDR_TILEINFO           m_macroTileTable[MacroTileTableSize];
    UINT_32                 m_noOfMacroEntries;
    BOOL_32                 m_allowNonDispThickModes;

    CIChipSettings          m_settings;
};

#endif


