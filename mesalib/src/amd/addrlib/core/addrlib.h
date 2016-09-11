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
* @file  addrlib.h
* @brief Contains the AddrLib base class definition.
***************************************************************************************************
*/

#ifndef __ADDR_LIB_H__
#define __ADDR_LIB_H__


#include "addrinterface.h"
#include "addrobject.h"
#include "addrelemlib.h"

#if BRAHMA_BUILD
#include "amdgpu_id.h"
#else
#include "atiid.h"
#endif

#ifndef CIASICIDGFXENGINE_R600
#define CIASICIDGFXENGINE_R600 0x00000006
#endif

#ifndef CIASICIDGFXENGINE_R800
#define CIASICIDGFXENGINE_R800 0x00000008
#endif

#ifndef CIASICIDGFXENGINE_SOUTHERNISLAND
#define CIASICIDGFXENGINE_SOUTHERNISLAND 0x0000000A
#endif

#ifndef CIASICIDGFXENGINE_SEAISLAND
#define CIASICIDGFXENGINE_SEAISLAND 0x0000000B
#endif
/**
***************************************************************************************************
* @brief Neutral enums that define pipeinterleave
***************************************************************************************************
*/
enum AddrPipeInterleave
{
    ADDR_PIPEINTERLEAVE_256B = 256,
    ADDR_PIPEINTERLEAVE_512B = 512,
};

/**
***************************************************************************************************
* @brief Neutral enums that define DRAM row size
***************************************************************************************************
*/
enum AddrRowSize
{
    ADDR_ROWSIZE_1KB = 1024,
    ADDR_ROWSIZE_2KB = 2048,
    ADDR_ROWSIZE_4KB = 4096,
    ADDR_ROWSIZE_8KB = 8192,
};

/**
***************************************************************************************************
* @brief Neutral enums that define bank interleave
***************************************************************************************************
*/
enum AddrBankInterleave
{
    ADDR_BANKINTERLEAVE_1 = 1,
    ADDR_BANKINTERLEAVE_2 = 2,
    ADDR_BANKINTERLEAVE_4 = 4,
    ADDR_BANKINTERLEAVE_8 = 8,
};

/**
***************************************************************************************************
* @brief Neutral enums that define MGPU chip tile size
***************************************************************************************************
*/
enum AddrChipTileSize
{
    ADDR_CHIPTILESIZE_16 = 16,
    ADDR_CHIPTILESIZE_32 = 32,
    ADDR_CHIPTILESIZE_64 = 64,
    ADDR_CHIPTILESIZE_128 = 128,
};

/**
***************************************************************************************************
* @brief Neutral enums that define shader engine tile size
***************************************************************************************************
*/
enum AddrEngTileSize
{
    ADDR_SE_TILESIZE_16 = 16,
    ADDR_SE_TILESIZE_32 = 32,
};

/**
***************************************************************************************************
* @brief Neutral enums that define bank swap size
***************************************************************************************************
*/
enum AddrBankSwapSize
{
    ADDR_BANKSWAP_128B = 128,
    ADDR_BANKSWAP_256B = 256,
    ADDR_BANKSWAP_512B = 512,
    ADDR_BANKSWAP_1KB = 1024,
};

/**
***************************************************************************************************
* @brief Neutral enums that define bank swap size
***************************************************************************************************
*/
enum AddrSampleSplitSize
{
    ADDR_SAMPLESPLIT_1KB = 1024,
    ADDR_SAMPLESPLIT_2KB = 2048,
    ADDR_SAMPLESPLIT_4KB = 4096,
    ADDR_SAMPLESPLIT_8KB = 8192,
};

/**
***************************************************************************************************
* @brief Flags for AddrTileMode
***************************************************************************************************
*/
struct AddrTileModeFlags
{
    UINT_32 thickness       : 4;
    UINT_32 isLinear        : 1;
    UINT_32 isMicro         : 1;
    UINT_32 isMacro         : 1;
    UINT_32 isMacro3d       : 1;
    UINT_32 isPrt           : 1;
    UINT_32 isPrtNoRotation : 1;
    UINT_32 isBankSwapped   : 1;
};

/**
***************************************************************************************************
* @brief This class contains asic independent address lib functionalities
***************************************************************************************************
*/
class AddrLib : public AddrObject
{
public:
    virtual ~AddrLib();

    static ADDR_E_RETURNCODE Create(
        const ADDR_CREATE_INPUT* pCreateInfo, ADDR_CREATE_OUTPUT* pCreateOut);

    /// Pair of Create
    VOID Destroy()
    {
        delete this;
    }

    static AddrLib* GetAddrLib(
        ADDR_HANDLE hLib);

    /// Returns AddrLib version (from compiled binary instead include file)
    UINT_32 GetVersion()
    {
        return m_version;
    }

    /// Returns asic chip family name defined by AddrLib
    AddrChipFamily GetAddrChipFamily()
    {
        return m_chipFamily;
    }

    /// Returns tileIndex support
    BOOL_32 UseTileIndex(INT_32 index) const
    {
        return m_configFlags.useTileIndex && (index != TileIndexInvalid);
    }

    /// Returns combined swizzle support
    BOOL_32 UseCombinedSwizzle() const
    {
        return m_configFlags.useCombinedSwizzle;
    }

    //
    // Interface stubs
    //
    ADDR_E_RETURNCODE ComputeSurfaceInfo(
        const ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn,
        ADDR_COMPUTE_SURFACE_INFO_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputeSurfaceAddrFromCoord(
        const ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,
        ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputeSurfaceCoordFromAddr(
        const ADDR_COMPUTE_SURFACE_COORDFROMADDR_INPUT*  pIn,
        ADDR_COMPUTE_SURFACE_COORDFROMADDR_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputeSliceTileSwizzle(
        const ADDR_COMPUTE_SLICESWIZZLE_INPUT*  pIn,
        ADDR_COMPUTE_SLICESWIZZLE_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ExtractBankPipeSwizzle(
        const ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT* pIn,
        ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE CombineBankPipeSwizzle(
        const ADDR_COMBINE_BANKPIPE_SWIZZLE_INPUT*  pIn,
        ADDR_COMBINE_BANKPIPE_SWIZZLE_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputeBaseSwizzle(
        const ADDR_COMPUTE_BASE_SWIZZLE_INPUT*  pIn,
        ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputeFmaskInfo(
        const ADDR_COMPUTE_FMASK_INFO_INPUT*  pIn,
        ADDR_COMPUTE_FMASK_INFO_OUTPUT* pOut);

    ADDR_E_RETURNCODE ComputeFmaskAddrFromCoord(
        const ADDR_COMPUTE_FMASK_ADDRFROMCOORD_INPUT*  pIn,
        ADDR_COMPUTE_FMASK_ADDRFROMCOORD_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputeFmaskCoordFromAddr(
        const ADDR_COMPUTE_FMASK_COORDFROMADDR_INPUT*  pIn,
        ADDR_COMPUTE_FMASK_COORDFROMADDR_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ConvertTileInfoToHW(
        const ADDR_CONVERT_TILEINFOTOHW_INPUT* pIn,
        ADDR_CONVERT_TILEINFOTOHW_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ConvertTileIndex(
        const ADDR_CONVERT_TILEINDEX_INPUT* pIn,
        ADDR_CONVERT_TILEINDEX_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ConvertTileIndex1(
        const ADDR_CONVERT_TILEINDEX1_INPUT* pIn,
        ADDR_CONVERT_TILEINDEX_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE GetTileIndex(
        const ADDR_GET_TILEINDEX_INPUT* pIn,
        ADDR_GET_TILEINDEX_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputeHtileInfo(
        const ADDR_COMPUTE_HTILE_INFO_INPUT* pIn,
        ADDR_COMPUTE_HTILE_INFO_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputeCmaskInfo(
        const ADDR_COMPUTE_CMASK_INFO_INPUT* pIn,
        ADDR_COMPUTE_CMASK_INFO_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputeDccInfo(
        const ADDR_COMPUTE_DCCINFO_INPUT* pIn,
        ADDR_COMPUTE_DCCINFO_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputeHtileAddrFromCoord(
        const ADDR_COMPUTE_HTILE_ADDRFROMCOORD_INPUT*  pIn,
        ADDR_COMPUTE_HTILE_ADDRFROMCOORD_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputeCmaskAddrFromCoord(
        const ADDR_COMPUTE_CMASK_ADDRFROMCOORD_INPUT*  pIn,
        ADDR_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputeHtileCoordFromAddr(
        const ADDR_COMPUTE_HTILE_COORDFROMADDR_INPUT*  pIn,
        ADDR_COMPUTE_HTILE_COORDFROMADDR_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputeCmaskCoordFromAddr(
        const ADDR_COMPUTE_CMASK_COORDFROMADDR_INPUT*  pIn,
        ADDR_COMPUTE_CMASK_COORDFROMADDR_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE ComputePrtInfo(
        const ADDR_PRT_INFO_INPUT*  pIn,
        ADDR_PRT_INFO_OUTPUT*       pOut) const;

    ADDR_E_RETURNCODE Flt32ToDepthPixel(
        const ELEM_FLT32TODEPTHPIXEL_INPUT* pIn,
        ELEM_FLT32TODEPTHPIXEL_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE Flt32ToColorPixel(
        const ELEM_FLT32TOCOLORPIXEL_INPUT* pIn,
        ELEM_FLT32TOCOLORPIXEL_OUTPUT* pOut) const;

    BOOL_32 GetExportNorm(
        const ELEM_GETEXPORTNORM_INPUT* pIn) const;

protected:
    AddrLib();  // Constructor is protected
    AddrLib(const AddrClient* pClient);

    /// Pure Virtual function for Hwl computing surface info
    virtual ADDR_E_RETURNCODE HwlComputeSurfaceInfo(
        const ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn,
        ADDR_COMPUTE_SURFACE_INFO_OUTPUT* pOut) const = 0;

    /// Pure Virtual function for Hwl computing surface address from coord
    virtual ADDR_E_RETURNCODE HwlComputeSurfaceAddrFromCoord(
        const ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,
        ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT* pOut) const = 0;

    /// Pure Virtual function for Hwl computing surface coord from address
    virtual ADDR_E_RETURNCODE HwlComputeSurfaceCoordFromAddr(
        const ADDR_COMPUTE_SURFACE_COORDFROMADDR_INPUT* pIn,
        ADDR_COMPUTE_SURFACE_COORDFROMADDR_OUTPUT* pOut) const = 0;

    /// Pure Virtual function for Hwl computing surface tile swizzle
    virtual ADDR_E_RETURNCODE HwlComputeSliceTileSwizzle(
        const ADDR_COMPUTE_SLICESWIZZLE_INPUT* pIn,
        ADDR_COMPUTE_SLICESWIZZLE_OUTPUT* pOut) const = 0;

    /// Pure Virtual function for Hwl extracting bank/pipe swizzle from base256b
    virtual ADDR_E_RETURNCODE HwlExtractBankPipeSwizzle(
        const ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT* pIn,
        ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT* pOut) const = 0;

    /// Pure Virtual function for Hwl combining bank/pipe swizzle
    virtual ADDR_E_RETURNCODE HwlCombineBankPipeSwizzle(
        UINT_32 bankSwizzle, UINT_32 pipeSwizzle, ADDR_TILEINFO*  pTileInfo,
        UINT_64 baseAddr, UINT_32* pTileSwizzle) const = 0;

    /// Pure Virtual function for Hwl computing base swizzle
    virtual ADDR_E_RETURNCODE HwlComputeBaseSwizzle(
        const ADDR_COMPUTE_BASE_SWIZZLE_INPUT* pIn,
        ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT* pOut) const = 0;

    /// Pure Virtual function for Hwl computing HTILE base align
    virtual UINT_32 HwlComputeHtileBaseAlign(
        BOOL_32 isTcCompatible, BOOL_32 isLinear, ADDR_TILEINFO* pTileInfo) const = 0;

    /// Pure Virtual function for Hwl computing HTILE bpp
    virtual UINT_32 HwlComputeHtileBpp(
        BOOL_32 isWidth8, BOOL_32 isHeight8) const = 0;

    /// Pure Virtual function for Hwl computing HTILE bytes
    virtual UINT_64 HwlComputeHtileBytes(
        UINT_32 pitch, UINT_32 height, UINT_32 bpp,
        BOOL_32 isLinear, UINT_32 numSlices, UINT_64* pSliceBytes, UINT_32 baseAlign) const = 0;

    /// Pure Virtual function for Hwl computing FMASK info
    virtual ADDR_E_RETURNCODE HwlComputeFmaskInfo(
        const ADDR_COMPUTE_FMASK_INFO_INPUT* pIn,
        ADDR_COMPUTE_FMASK_INFO_OUTPUT* pOut) = 0;

    /// Pure Virtual function for Hwl FMASK address from coord
    virtual ADDR_E_RETURNCODE HwlComputeFmaskAddrFromCoord(
        const ADDR_COMPUTE_FMASK_ADDRFROMCOORD_INPUT* pIn,
        ADDR_COMPUTE_FMASK_ADDRFROMCOORD_OUTPUT* pOut) const = 0;

    /// Pure Virtual function for Hwl FMASK coord from address
    virtual ADDR_E_RETURNCODE HwlComputeFmaskCoordFromAddr(
        const ADDR_COMPUTE_FMASK_COORDFROMADDR_INPUT* pIn,
        ADDR_COMPUTE_FMASK_COORDFROMADDR_OUTPUT* pOut) const = 0;

    /// Pure Virtual function for Hwl convert tile info from real value to HW value
    virtual ADDR_E_RETURNCODE HwlConvertTileInfoToHW(
        const ADDR_CONVERT_TILEINFOTOHW_INPUT* pIn,
        ADDR_CONVERT_TILEINFOTOHW_OUTPUT* pOut) const = 0;

    /// Pure Virtual function for Hwl compute mipmap info
    virtual BOOL_32 HwlComputeMipLevel(
        ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn) const = 0;

    /// Pure Virtual function for Hwl compute max cmask blockMax value
    virtual BOOL_32 HwlGetMaxCmaskBlockMax() const = 0;

    /// Pure Virtual function for Hwl compute fmask bits
    virtual UINT_32 HwlComputeFmaskBits(
        const ADDR_COMPUTE_FMASK_INFO_INPUT* pIn,
        UINT_32* pNumSamples) const = 0;

    /// Virtual function to get index (not pure then no need to implement this in all hwls
    virtual ADDR_E_RETURNCODE HwlGetTileIndex(
        const ADDR_GET_TILEINDEX_INPUT* pIn,
        ADDR_GET_TILEINDEX_OUTPUT*      pOut) const
    {
        return ADDR_NOTSUPPORTED;
    }

    /// Virtual function for Hwl to compute Dcc info
    virtual ADDR_E_RETURNCODE HwlComputeDccInfo(
        const ADDR_COMPUTE_DCCINFO_INPUT* pIn,
        ADDR_COMPUTE_DCCINFO_OUTPUT* pOut) const
    {
        return ADDR_NOTSUPPORTED;
    }

    /// Virtual function to get cmask address for tc compatible cmask
    virtual ADDR_E_RETURNCODE HwlComputeCmaskAddrFromCoord(
        const ADDR_COMPUTE_CMASK_ADDRFROMCOORD_INPUT* pIn,
        ADDR_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT* pOut) const
    {
        return ADDR_NOTSUPPORTED;
    }
    // Compute attributes

    // HTILE
    UINT_32    ComputeHtileInfo(
        ADDR_HTILE_FLAGS flags,
        UINT_32 pitchIn, UINT_32 heightIn, UINT_32 numSlices,
        BOOL_32 isLinear, BOOL_32 isWidth8, BOOL_32 isHeight8,
        ADDR_TILEINFO*  pTileInfo,
        UINT_32* pPitchOut, UINT_32* pHeightOut, UINT_64* pHtileBytes,
        UINT_32* pMacroWidth = NULL, UINT_32* pMacroHeight = NULL,
        UINT_64* pSliceSize = NULL, UINT_32* pBaseAlign = NULL) const;

    // CMASK
    ADDR_E_RETURNCODE ComputeCmaskInfo(
        ADDR_CMASK_FLAGS flags,
        UINT_32 pitchIn, UINT_32 heightIn, UINT_32 numSlices, BOOL_32 isLinear,
        ADDR_TILEINFO* pTileInfo, UINT_32* pPitchOut, UINT_32* pHeightOut, UINT_64* pCmaskBytes,
        UINT_32* pMacroWidth, UINT_32* pMacroHeight, UINT_64* pSliceSize = NULL,
        UINT_32* pBaseAlign = NULL, UINT_32* pBlockMax = NULL) const;

    virtual VOID HwlComputeTileDataWidthAndHeightLinear(
        UINT_32* pMacroWidth, UINT_32* pMacroHeight,
        UINT_32 bpp, ADDR_TILEINFO* pTileInfo) const;

    // CMASK & HTILE addressing
    virtual UINT_64 HwlComputeXmaskAddrFromCoord(
        UINT_32 pitch, UINT_32 height, UINT_32 x, UINT_32 y, UINT_32 slice,
        UINT_32 numSlices, UINT_32 factor, BOOL_32 isLinear, BOOL_32 isWidth8,
        BOOL_32 isHeight8, ADDR_TILEINFO* pTileInfo,
        UINT_32* bitPosition) const;

    virtual VOID HwlComputeXmaskCoordFromAddr(
        UINT_64 addr, UINT_32 bitPosition, UINT_32 pitch, UINT_32 height, UINT_32 numSlices,
        UINT_32 factor, BOOL_32 isLinear, BOOL_32 isWidth8, BOOL_32 isHeight8,
        ADDR_TILEINFO* pTileInfo, UINT_32* pX, UINT_32* pY, UINT_32* pSlice) const;

    // Surface mipmap
    VOID    ComputeMipLevel(
        ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn) const;

    /// Pure Virtual function for Hwl checking degrade for base level
    virtual BOOL_32 HwlDegradeBaseLevel(
        const ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn) const = 0;

    virtual BOOL_32 HwlOverrideTileMode(
        const ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn,
        AddrTileMode* pTileMode,
        AddrTileType* pTileType) const
    {
        // not supported in hwl layer, FALSE for not-overrided
        return FALSE;
    }

    AddrTileMode DegradeLargeThickTile(AddrTileMode tileMode, UINT_32 bpp) const;

    VOID PadDimensions(
        AddrTileMode tileMode, UINT_32 bpp, ADDR_SURFACE_FLAGS flags,
        UINT_32 numSamples, ADDR_TILEINFO* pTileInfo, UINT_32 padDims, UINT_32 mipLevel,
        UINT_32* pPitch, UINT_32 pitchAlign, UINT_32* pHeight, UINT_32 heightAlign,
        UINT_32* pSlices, UINT_32 sliceAlign) const;

    virtual VOID HwlPadDimensions(
        AddrTileMode tileMode, UINT_32 bpp, ADDR_SURFACE_FLAGS flags,
        UINT_32 numSamples, ADDR_TILEINFO* pTileInfo, UINT_32 padDims, UINT_32 mipLevel,
        UINT_32* pPitch, UINT_32 pitchAlign, UINT_32* pHeight, UINT_32 heightAlign,
        UINT_32* pSlices, UINT_32 sliceAlign) const
    {
    }

    //
    // Addressing shared for linear/1D tiling
    //
    UINT_64 ComputeSurfaceAddrFromCoordLinear(
        UINT_32 x, UINT_32 y, UINT_32 slice, UINT_32 sample,
        UINT_32 bpp, UINT_32 pitch, UINT_32 height, UINT_32 numSlices,
        UINT_32* pBitPosition) const;

    VOID    ComputeSurfaceCoordFromAddrLinear(
        UINT_64 addr, UINT_32 bitPosition, UINT_32 bpp,
        UINT_32 pitch, UINT_32 height, UINT_32 numSlices,
        UINT_32* pX, UINT_32* pY, UINT_32* pSlice, UINT_32* pSample) const;

    VOID    ComputeSurfaceCoordFromAddrMicroTiled(
        UINT_64 addr, UINT_32 bitPosition,
        UINT_32 bpp, UINT_32 pitch, UINT_32 height, UINT_32 numSamples,
        AddrTileMode tileMode, UINT_32 tileBase, UINT_32 compBits,
        UINT_32* pX, UINT_32* pY, UINT_32* pSlice, UINT_32* pSample,
        AddrTileType microTileType, BOOL_32 isDepthSampleOrder) const;

    UINT_32 ComputePixelIndexWithinMicroTile(
        UINT_32 x, UINT_32 y, UINT_32 z,
        UINT_32 bpp, AddrTileMode tileMode, AddrTileType microTileType) const;

    /// Pure Virtual function for Hwl computing coord from offset inside micro tile
    virtual VOID HwlComputePixelCoordFromOffset(
        UINT_32 offset, UINT_32 bpp, UINT_32 numSamples,
        AddrTileMode tileMode, UINT_32 tileBase, UINT_32 compBits,
        UINT_32* pX, UINT_32* pY, UINT_32* pSlice, UINT_32* pSample,
        AddrTileType microTileType, BOOL_32 isDepthSampleOrder) const = 0;

    //
    // Addressing shared by all
    //
    virtual UINT_32 HwlGetPipes(
        const ADDR_TILEINFO* pTileInfo) const;

    UINT_32 ComputePipeFromAddr(
        UINT_64 addr, UINT_32 numPipes) const;

    /// Pure Virtual function for Hwl computing pipe from coord
    virtual UINT_32 ComputePipeFromCoord(
        UINT_32 x, UINT_32 y, UINT_32 slice, AddrTileMode tileMode,
        UINT_32 pipeSwizzle, BOOL_32 flags, ADDR_TILEINFO* pTileInfo) const = 0;

    /// Pure Virtual function for Hwl computing coord Y for 8 pipe cmask/htile
    virtual UINT_32 HwlComputeXmaskCoordYFrom8Pipe(
        UINT_32 pipe, UINT_32 x) const = 0;

    //
    // Initialization
    //
    /// Pure Virtual function for Hwl computing internal global parameters from h/w registers
    virtual BOOL_32 HwlInitGlobalParams(
        const ADDR_CREATE_INPUT* pCreateIn) = 0;

    /// Pure Virtual function for Hwl converting chip family
    virtual AddrChipFamily HwlConvertChipFamily(UINT_32 uChipFamily, UINT_32 uChipRevision) = 0;

    //
    // Misc helper
    //
    static const AddrTileModeFlags m_modeFlags[ADDR_TM_COUNT];

    static UINT_32 ComputeSurfaceThickness(
        AddrTileMode tileMode);

    // Checking tile mode
    static BOOL_32 IsMacroTiled(AddrTileMode tileMode);
    static BOOL_32 IsMacro3dTiled(AddrTileMode tileMode);
    static BOOL_32 IsLinear(AddrTileMode tileMode);
    static BOOL_32 IsMicroTiled(AddrTileMode tileMode);
    static BOOL_32 IsPrtTileMode(AddrTileMode tileMode);
    static BOOL_32 IsPrtNoRotationTileMode(AddrTileMode tileMode);

    static UINT_32 Bits2Number(UINT_32 bitNum,...);

    static UINT_32 GetNumFragments(UINT_32 numSamples, UINT_32 numFrags)
    {
        return numFrags != 0 ? numFrags : Max(1u, numSamples);
    }

    /// Returns pointer of AddrElemLib
    AddrElemLib* GetElemLib() const
    {
        return m_pElemLib;
    }

    /// Return TRUE if tile info is needed
    BOOL_32 UseTileInfo() const
    {
        return !m_configFlags.ignoreTileInfo;
    }

    /// Returns fillSizeFields flag
    UINT_32 GetFillSizeFieldsFlags() const
    {
        return m_configFlags.fillSizeFields;
    }

    /// Adjusts pitch alignment for flipping surface
    VOID    AdjustPitchAlignment(
        ADDR_SURFACE_FLAGS flags, UINT_32* pPitchAlign) const;

    /// Overwrite tile config according to tile index
    virtual ADDR_E_RETURNCODE HwlSetupTileCfg(
        INT_32 index, INT_32 macroModeIndex,
        ADDR_TILEINFO* pInfo, AddrTileMode* mode = NULL, AddrTileType* type = NULL) const;

    /// Overwrite macro tile config according to tile index
    virtual INT_32 HwlComputeMacroModeIndex(
        INT_32 index, ADDR_SURFACE_FLAGS flags, UINT_32 bpp, UINT_32 numSamples,
        ADDR_TILEINFO* pTileInfo, AddrTileMode *pTileMode = NULL, AddrTileType *pTileType = NULL
        ) const
    {
        return TileIndexNoMacroIndex;
    }

    /// Pre-handler of 3x pitch (96 bit) adjustment
    virtual UINT_32 HwlPreHandleBaseLvl3xPitch(
        const ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn, UINT_32 expPitch) const;
    /// Post-handler of 3x pitch adjustment
    virtual UINT_32 HwlPostHandleBaseLvl3xPitch(
        const ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn, UINT_32 expPitch) const;
    /// Check miplevel after surface adjustment
    ADDR_E_RETURNCODE PostComputeMipLevel(
        ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn,
        ADDR_COMPUTE_SURFACE_INFO_OUTPUT* pOut) const;

    /// Quad buffer stereo support, has its implementation in ind. layer
    virtual BOOL_32 ComputeQbStereoInfo(
        ADDR_COMPUTE_SURFACE_INFO_OUTPUT* pOut) const;

    /// Pure virutual function to compute stereo bank swizzle for right eye
    virtual UINT_32 HwlComputeQbStereoRightSwizzle(
        ADDR_COMPUTE_SURFACE_INFO_OUTPUT* pOut) const = 0;

private:
    // Disallow the copy constructor
    AddrLib(const AddrLib& a);

    // Disallow the assignment operator
    AddrLib& operator=(const AddrLib& a);

    VOID SetAddrChipFamily(UINT_32 uChipFamily, UINT_32 uChipRevision);

    UINT_32 ComputeCmaskBaseAlign(
        ADDR_CMASK_FLAGS flags, ADDR_TILEINFO*  pTileInfo) const;

    UINT_64 ComputeCmaskBytes(
        UINT_32 pitch, UINT_32 height, UINT_32 numSlices) const;

    //
    // CMASK/HTILE shared methods
    //
    VOID    ComputeTileDataWidthAndHeight(
        UINT_32 bpp, UINT_32 cacheBits, ADDR_TILEINFO* pTileInfo,
        UINT_32* pMacroWidth, UINT_32* pMacroHeight) const;

    UINT_32 ComputeXmaskCoordYFromPipe(
        UINT_32 pipe, UINT_32 x) const;

    VOID SetMinPitchAlignPixels(UINT_32 minPitchAlignPixels);

    BOOL_32 DegradeBaseLevel(
        const ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn, AddrTileMode* pTileMode) const;

protected:
    AddrLibClass        m_class;        ///< Store class type (HWL type)

    AddrChipFamily      m_chipFamily;   ///< Chip family translated from the one in atiid.h

    UINT_32             m_chipRevision; ///< Revision id from xxx_id.h

    UINT_32             m_version;      ///< Current version

    //
    // Global parameters
    //
    ADDR_CONFIG_FLAGS   m_configFlags;  ///< Global configuration flags. Note this is setup by
                                        ///  AddrLib instead of Client except forceLinearAligned

    UINT_32             m_pipes;        ///< Number of pipes
    UINT_32             m_banks;        ///< Number of banks
                                        ///  For r800 this is MC_ARB_RAMCFG.NOOFBANK
                                        ///  Keep it here to do default parameter calculation

    UINT_32             m_pipeInterleaveBytes;
                                        ///< Specifies the size of contiguous address space
                                        ///  within each tiling pipe when making linear
                                        ///  accesses. (Formerly Group Size)

    UINT_32             m_rowSize;      ///< DRAM row size, in bytes

    UINT_32             m_minPitchAlignPixels; ///< Minimum pitch alignment in pixels
    UINT_32             m_maxSamples;   ///< Max numSamples
private:
    AddrElemLib*        m_pElemLib;     ///< Element Lib pointer
};

AddrLib* AddrSIHwlInit  (const AddrClient* pClient);
AddrLib* AddrCIHwlInit  (const AddrClient* pClient);

#endif

