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
****************************************************************************************************
* @file  addrlib.h
* @brief Contains the Addr::Lib base class definition.
****************************************************************************************************
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

#ifndef CIASICIDGFXENGINE_ARCTICISLAND
#define CIASICIDGFXENGINE_ARCTICISLAND 0x0000000D
#endif

namespace Addr
{

/**
****************************************************************************************************
* @brief Neutral enums that define pipeinterleave
****************************************************************************************************
*/
enum PipeInterleave
{
    ADDR_PIPEINTERLEAVE_256B = 256,
    ADDR_PIPEINTERLEAVE_512B = 512,
    ADDR_PIPEINTERLEAVE_1KB  = 1024,
    ADDR_PIPEINTERLEAVE_2KB  = 2048,
};

/**
****************************************************************************************************
* @brief Neutral enums that define DRAM row size
****************************************************************************************************
*/
enum RowSize
{
    ADDR_ROWSIZE_1KB = 1024,
    ADDR_ROWSIZE_2KB = 2048,
    ADDR_ROWSIZE_4KB = 4096,
    ADDR_ROWSIZE_8KB = 8192,
};

/**
****************************************************************************************************
* @brief Neutral enums that define bank interleave
****************************************************************************************************
*/
enum BankInterleave
{
    ADDR_BANKINTERLEAVE_1 = 1,
    ADDR_BANKINTERLEAVE_2 = 2,
    ADDR_BANKINTERLEAVE_4 = 4,
    ADDR_BANKINTERLEAVE_8 = 8,
};

/**
****************************************************************************************************
* @brief Neutral enums that define shader engine tile size
****************************************************************************************************
*/
enum ShaderEngineTileSize
{
    ADDR_SE_TILESIZE_16 = 16,
    ADDR_SE_TILESIZE_32 = 32,
};

/**
****************************************************************************************************
* @brief Neutral enums that define bank swap size
****************************************************************************************************
*/
enum BankSwapSize
{
    ADDR_BANKSWAP_128B = 128,
    ADDR_BANKSWAP_256B = 256,
    ADDR_BANKSWAP_512B = 512,
    ADDR_BANKSWAP_1KB = 1024,
};

/**
****************************************************************************************************
* @brief This class contains asic independent address lib functionalities
****************************************************************************************************
*/
class Lib : public Object
{
public:
    virtual ~Lib();

    static ADDR_E_RETURNCODE Create(
        const ADDR_CREATE_INPUT* pCreateInfo, ADDR_CREATE_OUTPUT* pCreateOut);

    /// Pair of Create
    VOID Destroy()
    {
        delete this;
    }

    static Lib* GetLib(ADDR_HANDLE hLib);

    /// Returns AddrLib version (from compiled binary instead include file)
    UINT_32 GetVersion()
    {
        return m_version;
    }

    /// Returns asic chip family name defined by AddrLib
    ChipFamily GetChipFamily()
    {
        return m_chipFamily;
    }

    ADDR_E_RETURNCODE Flt32ToDepthPixel(
        const ELEM_FLT32TODEPTHPIXEL_INPUT* pIn,
        ELEM_FLT32TODEPTHPIXEL_OUTPUT* pOut) const;

    ADDR_E_RETURNCODE Flt32ToColorPixel(
        const ELEM_FLT32TOCOLORPIXEL_INPUT* pIn,
        ELEM_FLT32TOCOLORPIXEL_OUTPUT* pOut) const;

    BOOL_32 GetExportNorm(const ELEM_GETEXPORTNORM_INPUT* pIn) const;

    ADDR_E_RETURNCODE GetMaxAlignments(ADDR_GET_MAX_ALIGNMENTS_OUTPUT* pOut) const;

protected:
    Lib();  // Constructor is protected
    Lib(const Client* pClient);

    /// Pure virtual function to get max alignments
    virtual ADDR_E_RETURNCODE HwlGetMaxAlignments(ADDR_GET_MAX_ALIGNMENTS_OUTPUT* pOut) const = 0;

    //
    // Initialization
    //
    /// Pure Virtual function for Hwl computing internal global parameters from h/w registers
    virtual BOOL_32 HwlInitGlobalParams(const ADDR_CREATE_INPUT* pCreateIn) = 0;

    /// Pure Virtual function for Hwl converting chip family
    virtual ChipFamily HwlConvertChipFamily(UINT_32 uChipFamily, UINT_32 uChipRevision) = 0;

    /// Get equation table pointer and number of equations
    virtual UINT_32 HwlGetEquationTableInfo(const ADDR_EQUATION** ppEquationTable) const
    {
        *ppEquationTable = NULL;

        return 0;
    }

    //
    // Misc helper
    //
    static UINT_32 Bits2Number(UINT_32 bitNum, ...);

    static UINT_32 GetNumFragments(UINT_32 numSamples, UINT_32 numFrags)
    {
        return (numFrags != 0) ? numFrags : Max(1u, numSamples);
    }

    /// Returns pointer of ElemLib
    ElemLib* GetElemLib() const
    {
        return m_pElemLib;
    }

    /// Returns fillSizeFields flag
    UINT_32 GetFillSizeFieldsFlags() const
    {
        return m_configFlags.fillSizeFields;
    }

private:
    // Disallow the copy constructor
    Lib(const Lib& a);

    // Disallow the assignment operator
    Lib& operator=(const Lib& a);

    VOID SetChipFamily(UINT_32 uChipFamily, UINT_32 uChipRevision);

    VOID SetMinPitchAlignPixels(UINT_32 minPitchAlignPixels);

protected:
    LibClass    m_class;        ///< Store class type (HWL type)

    ChipFamily  m_chipFamily;   ///< Chip family translated from the one in atiid.h

    UINT_32     m_chipRevision; ///< Revision id from xxx_id.h

    UINT_32     m_version;      ///< Current version

    //
    // Global parameters
    //
    ConfigFlags m_configFlags;          ///< Global configuration flags. Note this is setup by
                                        ///  AddrLib instead of Client except forceLinearAligned

    UINT_32     m_pipes;                ///< Number of pipes
    UINT_32     m_banks;                ///< Number of banks
                                        ///  For r800 this is MC_ARB_RAMCFG.NOOFBANK
                                        ///  Keep it here to do default parameter calculation

    UINT_32     m_pipeInterleaveBytes;
                                        ///< Specifies the size of contiguous address space
                                        ///  within each tiling pipe when making linear
                                        ///  accesses. (Formerly Group Size)

    UINT_32     m_rowSize;              ///< DRAM row size, in bytes

    UINT_32     m_minPitchAlignPixels;  ///< Minimum pitch alignment in pixels
    UINT_32     m_maxSamples;           ///< Max numSamples
private:
    ElemLib*    m_pElemLib;             ///< Element Lib pointer
};

Lib* SiHwlInit   (const Client* pClient);
Lib* CiHwlInit   (const Client* pClient);
Lib* Gfx9HwlInit (const Client* pClient);

} // Addr

#endif
