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
* @file  addrinterface.h
* @brief Contains the addrlib interfaces declaration and parameter defines
***************************************************************************************************
*/
#ifndef __ADDR_INTERFACE_H__
#define __ADDR_INTERFACE_H__

#include "addrtypes.h"

#if defined(__cplusplus)
extern "C"
{
#endif

#define ADDRLIB_VERSION_MAJOR 5
#define ADDRLIB_VERSION_MINOR 25
#define ADDRLIB_VERSION ((ADDRLIB_VERSION_MAJOR << 16) | ADDRLIB_VERSION_MINOR)

/// Virtually all interface functions need ADDR_HANDLE as first parameter
typedef VOID*   ADDR_HANDLE;

/// Client handle used in callbacks
typedef VOID*   ADDR_CLIENT_HANDLE;

/**
* /////////////////////////////////////////////////////////////////////////////////////////////////
* //                                  Callback functions
* /////////////////////////////////////////////////////////////////////////////////////////////////
*    typedef VOID* (ADDR_API* ADDR_ALLOCSYSMEM)(
*         const ADDR_ALLOCSYSMEM_INPUT* pInput);
*    typedef ADDR_E_RETURNCODE (ADDR_API* ADDR_FREESYSMEM)(
*         VOID* pVirtAddr);
*    typedef ADDR_E_RETURNCODE (ADDR_API* ADDR_DEBUGPRINT)(
*         const ADDR_DEBUGPRINT_INPUT* pInput);
*
* /////////////////////////////////////////////////////////////////////////////////////////////////
* //                               Create/Destroy/Config functions
* /////////////////////////////////////////////////////////////////////////////////////////////////
*     AddrCreate()
*     AddrDestroy()
*
* /////////////////////////////////////////////////////////////////////////////////////////////////
* //                                  Surface functions
* /////////////////////////////////////////////////////////////////////////////////////////////////
*     AddrComputeSurfaceInfo()
*     AddrComputeSurfaceAddrFromCoord()
*     AddrComputeSurfaceCoordFromAddr()
*
* /////////////////////////////////////////////////////////////////////////////////////////////////
* //                                   HTile functions
* /////////////////////////////////////////////////////////////////////////////////////////////////
*     AddrComputeHtileInfo()
*     AddrComputeHtileAddrFromCoord()
*     AddrComputeHtileCoordFromAddr()
*
* /////////////////////////////////////////////////////////////////////////////////////////////////
* //                                   C-mask functions
* /////////////////////////////////////////////////////////////////////////////////////////////////
*     AddrComputeCmaskInfo()
*     AddrComputeCmaskAddrFromCoord()
*     AddrComputeCmaskCoordFromAddr()
*
* /////////////////////////////////////////////////////////////////////////////////////////////////
* //                                   F-mask functions
* /////////////////////////////////////////////////////////////////////////////////////////////////
*     AddrComputeFmaskInfo()
*     AddrComputeFmaskAddrFromCoord()
*     AddrComputeFmaskCoordFromAddr()
*
* /////////////////////////////////////////////////////////////////////////////////////////////////
* //                               Element/Utility functions
* /////////////////////////////////////////////////////////////////////////////////////////////////
*     ElemFlt32ToDepthPixel()
*     ElemFlt32ToColorPixel()
*     AddrExtractBankPipeSwizzle()
*     AddrCombineBankPipeSwizzle()
*     AddrComputeSliceSwizzle()
*     AddrConvertTileInfoToHW()
*     AddrConvertTileIndex()
*     AddrConvertTileIndex1()
*     AddrGetTileIndex()
*     AddrComputeBaseSwizzle()
*     AddrUseTileIndex()
*     AddrUseCombinedSwizzle()
*
* /////////////////////////////////////////////////////////////////////////////////////////////////
* //                                    Dump functions
* /////////////////////////////////////////////////////////////////////////////////////////////////
*     AddrDumpSurfaceInfo()
*     AddrDumpFmaskInfo()
*     AddrDumpCmaskInfo()
*     AddrDumpHtileInfo()
*
**/

///////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Callback functions
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
***************************************************************************************************
* @brief Alloc system memory flags.
* @note These flags are reserved for future use and if flags are added will minimize the impact
*       of the client.
***************************************************************************************************
*/
typedef union _ADDR_ALLOCSYSMEM_FLAGS
{
    struct
    {
        UINT_32 reserved    : 32;  ///< Reserved for future use.
    } fields;
    UINT_32 value;

} ADDR_ALLOCSYSMEM_FLAGS;

/**
***************************************************************************************************
* @brief Alloc system memory input structure
***************************************************************************************************
*/
typedef struct _ADDR_ALLOCSYSMEM_INPUT
{
    UINT_32                 size;           ///< Size of this structure in bytes

    ADDR_ALLOCSYSMEM_FLAGS  flags;          ///< System memory flags.
    UINT_32                 sizeInBytes;    ///< System memory allocation size in bytes.
    ADDR_CLIENT_HANDLE      hClient;        ///< Client handle
} ADDR_ALLOCSYSMEM_INPUT;

/**
***************************************************************************************************
* ADDR_ALLOCSYSMEM
*   @brief
*       Allocate system memory callback function. Returns valid pointer on success.
***************************************************************************************************
*/
typedef VOID* (ADDR_API* ADDR_ALLOCSYSMEM)(
    const ADDR_ALLOCSYSMEM_INPUT* pInput);

/**
***************************************************************************************************
* @brief Free system memory input structure
***************************************************************************************************
*/
typedef struct _ADDR_FREESYSMEM_INPUT
{
    UINT_32                 size;           ///< Size of this structure in bytes

    VOID*                   pVirtAddr;      ///< Virtual address
    ADDR_CLIENT_HANDLE      hClient;        ///< Client handle
} ADDR_FREESYSMEM_INPUT;

/**
***************************************************************************************************
* ADDR_FREESYSMEM
*   @brief
*       Free system memory callback function.
*       Returns ADDR_OK on success.
***************************************************************************************************
*/
typedef ADDR_E_RETURNCODE (ADDR_API* ADDR_FREESYSMEM)(
    const ADDR_FREESYSMEM_INPUT* pInput);

/**
***************************************************************************************************
* @brief Print debug message input structure
***************************************************************************************************
*/
typedef struct _ADDR_DEBUGPRINT_INPUT
{
    UINT_32             size;           ///< Size of this structure in bytes

    CHAR*               pDebugString;   ///< Debug print string
    va_list             ap;             ///< Variable argument list
    ADDR_CLIENT_HANDLE  hClient;        ///< Client handle
} ADDR_DEBUGPRINT_INPUT;

/**
***************************************************************************************************
* ADDR_DEBUGPRINT
*   @brief
*       Print debug message callback function.
*       Returns ADDR_OK on success.
***************************************************************************************************
*/
typedef ADDR_E_RETURNCODE (ADDR_API* ADDR_DEBUGPRINT)(
    const ADDR_DEBUGPRINT_INPUT* pInput);

/**
***************************************************************************************************
* ADDR_CALLBACKS
*
*   @brief
*       Address Library needs client to provide system memory alloc/free routines.
***************************************************************************************************
*/
typedef struct _ADDR_CALLBACKS
{
    ADDR_ALLOCSYSMEM allocSysMem;   ///< Routine to allocate system memory
    ADDR_FREESYSMEM  freeSysMem;    ///< Routine to free system memory
    ADDR_DEBUGPRINT  debugPrint;    ///< Routine to print debug message
} ADDR_CALLBACKS;

///////////////////////////////////////////////////////////////////////////////////////////////////
//                               Create/Destroy functions
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
***************************************************************************************************
* ADDR_CREATE_FLAGS
*
*   @brief
*       This structure is used to pass some setup in creation of AddrLib
*   @note
***************************************************************************************************
*/
typedef union _ADDR_CREATE_FLAGS
{
    struct
    {
        UINT_32 noCubeMipSlicesPad     : 1;    ///< Turn cubemap faces padding off
        UINT_32 fillSizeFields         : 1;    ///< If clients fill size fields in all input and
                                               ///  output structure
        UINT_32 useTileIndex           : 1;    ///< Make tileIndex field in input valid
        UINT_32 useCombinedSwizzle     : 1;    ///< Use combined tile swizzle
        UINT_32 checkLast2DLevel       : 1;    ///< Check the last 2D mip sub level
        UINT_32 useHtileSliceAlign     : 1;    ///< Do htile single slice alignment
        UINT_32 degradeBaseLevel       : 1;    ///< Degrade to 1D modes automatically for base level
        UINT_32 allowLargeThickTile    : 1;    ///< Allow 64*thickness*bytesPerPixel > rowSize
        UINT_32 reserved               : 24;   ///< Reserved bits for future use
    };

    UINT_32 value;
} ADDR_CREATE_FLAGS;

/**
***************************************************************************************************
*   ADDR_REGISTER_VALUE
*
*   @brief
*       Data from registers to setup AddrLib global data, used in AddrCreate
***************************************************************************************************
*/
typedef struct _ADDR_REGISTER_VALUE
{
    UINT_32  gbAddrConfig;       ///< For R8xx, use GB_ADDR_CONFIG register value.
                                 ///  For R6xx/R7xx, use GB_TILING_CONFIG.
                                 ///  But they can be treated as the same.
                                 ///  if this value is 0, use chip to set default value
    UINT_32  backendDisables;    ///< 1 bit per backend, starting with LSB. 1=disabled,0=enabled.
                                 ///  Register value of CC_RB_BACKEND_DISABLE.BACKEND_DISABLE

                                 ///  R800 registers-----------------------------------------------
    UINT_32  noOfBanks;          ///< Number of h/w ram banks - For r800: MC_ARB_RAMCFG.NOOFBANK
                                 ///  No enums for this value in h/w header files
                                 ///  0: 4
                                 ///  1: 8
                                 ///  2: 16
    UINT_32  noOfRanks;          ///  MC_ARB_RAMCFG.NOOFRANK
                                 ///  0: 1
                                 ///  1: 2
                                 ///  SI (R1000) registers-----------------------------------------
    const UINT_32* pTileConfig;  ///< Global tile setting tables
    UINT_32  noOfEntries;        ///< Number of entries in pTileConfig

                                 ///< CI registers-------------------------------------------------
    const UINT_32* pMacroTileConfig;    ///< Global macro tile mode table
    UINT_32  noOfMacroEntries;   ///< Number of entries in pMacroTileConfig

} ADDR_REGISTER_VALUE;

/**
***************************************************************************************************
* ADDR_CREATE_INPUT
*
*   @brief
*       Parameters use to create an AddrLib Object. Caller must provide all fields.
*
***************************************************************************************************
*/
typedef struct _ADDR_CREATE_INPUT
{
    UINT_32             size;                ///< Size of this structure in bytes

    UINT_32             chipEngine;          ///< Chip Engine
    UINT_32             chipFamily;          ///< Chip Family
    UINT_32             chipRevision;        ///< Chip Revision
    ADDR_CALLBACKS      callbacks;           ///< Callbacks for sysmem alloc/free/print
    ADDR_CREATE_FLAGS   createFlags;         ///< Flags to setup AddrLib
    ADDR_REGISTER_VALUE regValue;            ///< Data from registers to setup AddrLib global data
    ADDR_CLIENT_HANDLE  hClient;             ///< Client handle
    UINT_32             minPitchAlignPixels; ///< Minimum pitch alignment in pixels
} ADDR_CREATE_INPUT;

/**
***************************************************************************************************
* ADDR_CREATEINFO_OUTPUT
*
*   @brief
*       Return AddrLib handle to client driver
*
***************************************************************************************************
*/
typedef struct _ADDR_CREATE_OUTPUT
{
    UINT_32     size;    ///< Size of this structure in bytes

    ADDR_HANDLE hLib;    ///< Address lib handle
} ADDR_CREATE_OUTPUT;

/**
***************************************************************************************************
*   AddrCreate
*
*   @brief
*       Create AddrLib object, must be called before any interface calls
*
*   @return
*       ADDR_OK if successful
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrCreate(
    const ADDR_CREATE_INPUT*    pAddrCreateIn,
    ADDR_CREATE_OUTPUT*         pAddrCreateOut);



/**
***************************************************************************************************
*   AddrDestroy
*
*   @brief
*       Destroy AddrLib object, must be called to free internally allocated resources.
*
*   @return
*      ADDR_OK if successful
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrDestroy(
    ADDR_HANDLE hLib);



///////////////////////////////////////////////////////////////////////////////////////////////////
//                                    Surface functions
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
***************************************************************************************************
* @brief
*       Bank/tiling parameters. On function input, these can be set as desired or
*       left 0 for AddrLib to calculate/default. On function output, these are the actual
*       parameters used.
* @note
*       Valid bankWidth/bankHeight value:
*       1,2,4,8. They are factors instead of pixels or bytes.
*
*       The bank number remains constant across each row of the
*       macro tile as each pipe is selected, so the number of
*       tiles in the x direction with the same bank number will
*       be bank_width * num_pipes.
***************************************************************************************************
*/
typedef struct _ADDR_TILEINFO
{
    ///  Any of these parameters can be set to 0 to use the HW default.
    UINT_32     banks;              ///< Number of banks, numerical value
    UINT_32     bankWidth;          ///< Number of tiles in the X direction in the same bank
    UINT_32     bankHeight;         ///< Number of tiles in the Y direction in the same bank
    UINT_32     macroAspectRatio;   ///< Macro tile aspect ratio. 1-1:1, 2-4:1, 4-16:1, 8-64:1
    UINT_32     tileSplitBytes;     ///< Tile split size, in bytes
    AddrPipeCfg pipeConfig;         ///< Pipe Config = HW enum + 1
} ADDR_TILEINFO;

// Create a define to avoid client change. The removal of R800 is because we plan to implement SI
// within 800 HWL - An AddrPipeCfg is added in above data structure
typedef ADDR_TILEINFO ADDR_R800_TILEINFO;

/**
***************************************************************************************************
* @brief
*       Information needed by quad buffer stereo support
***************************************************************************************************
*/
typedef struct _ADDR_QBSTEREOINFO
{
    UINT_32         eyeHeight;          ///< Height (in pixel rows) to right eye
    UINT_32         rightOffset;        ///< Offset (in bytes) to right eye
    UINT_32         rightSwizzle;       ///< TileSwizzle for right eyes
} ADDR_QBSTEREOINFO;

/**
***************************************************************************************************
*   ADDR_SURFACE_FLAGS
*
*   @brief
*       Surface flags
***************************************************************************************************
*/
typedef union _ADDR_SURFACE_FLAGS
{
    struct
    {
        UINT_32 color         : 1; ///< Flag indicates this is a color buffer
        UINT_32 depth         : 1; ///< Flag indicates this is a depth/stencil buffer
        UINT_32 stencil       : 1; ///< Flag indicates this is a stencil buffer
        UINT_32 texture       : 1; ///< Flag indicates this is a texture
        UINT_32 cube          : 1; ///< Flag indicates this is a cubemap

        UINT_32 volume        : 1; ///< Flag indicates this is a volume texture
        UINT_32 fmask         : 1; ///< Flag indicates this is an fmask
        UINT_32 cubeAsArray   : 1; ///< Flag indicates if treat cubemap as arrays
        UINT_32 compressZ     : 1; ///< Flag indicates z buffer is compressed
        UINT_32 overlay       : 1; ///< Flag indicates this is an overlay surface
        UINT_32 noStencil     : 1; ///< Flag indicates this depth has no separate stencil
        UINT_32 display       : 1; ///< Flag indicates this should match display controller req.
        UINT_32 opt4Space     : 1; ///< Flag indicates this surface should be optimized for space
                                   ///  i.e. save some memory but may lose performance
        UINT_32 prt           : 1; ///< Flag for partially resident texture
        UINT_32 qbStereo      : 1; ///< Quad buffer stereo surface
        UINT_32 pow2Pad       : 1; ///< SI: Pad to pow2, must set for mipmap (include level0)
        UINT_32 interleaved   : 1; ///< Special flag for interleaved YUV surface padding
        UINT_32 degrade4Space : 1; ///< Degrade base level's tile mode to save memory
        UINT_32 tcCompatible  : 1; ///< Flag indicates surface needs to be shader readable
        UINT_32 dispTileType  : 1; ///< NI: force display Tiling for 128 bit shared resoruce
        UINT_32 dccCompatible : 1; ///< VI: whether to support dcc fast clear
        UINT_32 czDispCompatible: 1; ///< SI+: CZ family (Carrizo) has a HW bug needs special alignment.
                                     ///<      This flag indicates we need to follow the alignment with
                                     ///<      CZ families or other ASICs under PX configuration + CZ.
        UINT_32 reserved      :10; ///< Reserved bits
    };

    UINT_32 value;
} ADDR_SURFACE_FLAGS;

/**
***************************************************************************************************
*   ADDR_COMPUTE_SURFACE_INFO_INPUT
*
*   @brief
*       Input structure for AddrComputeSurfaceInfo
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_SURFACE_INFO_INPUT
{
    UINT_32             size;               ///< Size of this structure in bytes

    AddrTileMode        tileMode;           ///< Tile mode
    AddrFormat          format;             ///< If format is set to valid one, bpp/width/height
                                            ///  might be overwritten
    UINT_32             bpp;                ///< Bits per pixel
    UINT_32             numSamples;         ///< Number of samples
    UINT_32             width;              ///< Width, in pixels
    UINT_32             height;             ///< Height, in pixels
    UINT_32             numSlices;          ///< Number surface slice/depth,
                                            ///  Note:
                                            ///  For cubemap, driver clients usually set numSlices
                                            ///  to 1 in per-face calc.
                                            ///  For 7xx and above, we need pad faces as slices.
                                            ///  In this case, clients should set numSlices to 6 and
                                            ///  this is also can be turned off by createFlags when
                                            ///  calling AddrCreate
    UINT_32             slice;              ///< Slice index
    UINT_32             mipLevel;           ///< Current mipmap level.
                                            ///  Padding/tiling have different rules for level0 and
                                            ///  sublevels
    ADDR_SURFACE_FLAGS  flags;              ///< Surface type flags
    UINT_32             numFrags;           ///< Number of fragments, leave it zero or the same as
                                            ///  number of samples for normal AA; Set it to the
                                            ///  number of fragments for EQAA
    /// r800 and later HWL parameters
    // Needed by 2D tiling, for linear and 1D tiling, just keep them 0's
    ADDR_TILEINFO*      pTileInfo;          ///< 2D tile parameters. Set to 0 to default/calculate
    AddrTileType        tileType;           ///< Micro tiling type, not needed when tileIndex != -1
    INT_32              tileIndex;          ///< Tile index, MUST be -1 if you don't want to use it
                                            ///  while the global useTileIndex is set to 1
    UINT_32             basePitch;          ///< Base level pitch in pixels, 0 means ignored, is a
                                            ///  must for mip levels from SI+.
                                            ///  Don't use pitch in blocks for compressed formats!
} ADDR_COMPUTE_SURFACE_INFO_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_SURFACE_INFO_OUTPUT
*
*   @brief
*       Output structure for AddrComputeSurfInfo
*   @note
        Element: AddrLib unit for computing. e.g. BCn: 4x4 blocks; R32B32B32: 32bit with 3x pitch
        Pixel: Original pixel
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_SURFACE_INFO_OUTPUT
{
    UINT_32         size;           ///< Size of this structure in bytes

    UINT_32         pitch;          ///< Pitch in elements (in blocks for compressed formats)
    UINT_32         height;         ///< Height in elements (in blocks for compressed formats)
    UINT_32         depth;          ///< Number of slice/depth
    UINT_64         surfSize;       ///< Surface size in bytes
    AddrTileMode    tileMode;       ///< Actual tile mode. May differ from that in input
    UINT_32         baseAlign;      ///< Base address alignment
    UINT_32         pitchAlign;     ///< Pitch alignment, in elements
    UINT_32         heightAlign;    ///< Height alignment, in elements
    UINT_32         depthAlign;     ///< Depth alignment, aligned to thickness, for 3d texture
    UINT_32         bpp;            ///< Bits per elements (e.g. blocks for BCn, 1/3 for 96bit)
    UINT_32         pixelPitch;     ///< Pitch in original pixels
    UINT_32         pixelHeight;    ///< Height in original pixels
    UINT_32         pixelBits;      ///< Original bits per pixel, passed from input
    UINT_64         sliceSize;      ///< Size of slice specified by input's slice
                                    ///  The result is controlled by surface flags & createFlags
                                    ///  By default this value equals to surfSize for volume
    UINT_32         pitchTileMax;   ///< PITCH_TILE_MAX value for h/w register
    UINT_32         heightTileMax;  ///< HEIGHT_TILE_MAX value for h/w register
    UINT_32         sliceTileMax;   ///< SLICE_TILE_MAX value for h/w register

    UINT_32         numSamples;     ///< Pass the effective numSamples processed in this call

    /// r800 and later HWL parameters
    ADDR_TILEINFO*  pTileInfo;      ///< Tile parameters used. Filled in if 0 on input
    AddrTileType    tileType;       ///< Micro tiling type, only valid when tileIndex != -1
    INT_32          tileIndex;      ///< Tile index, MAY be "downgraded"

    INT_32          macroModeIndex; ///< Index in macro tile mode table if there is one (CI)
    /// Special information to work around SI mipmap swizzle bug UBTS #317508
    BOOL_32         last2DLevel;    ///< TRUE if this is the last 2D(3D) tiled
                                    ///< Only meaningful when create flag checkLast2DLevel is set
    /// Stereo info
    ADDR_QBSTEREOINFO*  pStereoInfo;///< Stereo information, needed when .qbStereo flag is TRUE
} ADDR_COMPUTE_SURFACE_INFO_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeSurfaceInfo
*
*   @brief
*       Compute surface width/height/depth/alignments and suitable tiling mode
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeSurfaceInfo(
    ADDR_HANDLE                             hLib,
    const ADDR_COMPUTE_SURFACE_INFO_INPUT*  pIn,
    ADDR_COMPUTE_SURFACE_INFO_OUTPUT*       pOut);



/**
***************************************************************************************************
*   ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT
*
*   @brief
*       Input structure for AddrComputeSurfaceAddrFromCoord
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT
{
    UINT_32         size;               ///< Size of this structure in bytes

    UINT_32         x;                  ///< X coordinate
    UINT_32         y;                  ///< Y coordinate
    UINT_32         slice;              ///< Slice index
    UINT_32         sample;             ///< Sample index, use fragment index for EQAA

    UINT_32         bpp;                ///< Bits per pixel
    UINT_32         pitch;              ///< Surface pitch, in pixels
    UINT_32         height;             ///< Surface height, in pixels
    UINT_32         numSlices;          ///< Surface depth
    UINT_32         numSamples;         ///< Number of samples

    AddrTileMode    tileMode;           ///< Tile mode
    BOOL_32         isDepth;            ///< TRUE if the surface uses depth sample ordering within
                                        ///  micro tile. Textures can also choose depth sample order
    UINT_32         tileBase;           ///< Base offset (in bits) inside micro tile which handles
                                        ///  the case that components are stored separately
    UINT_32         compBits;           ///< The component bits actually needed(for planar surface)

    UINT_32         numFrags;           ///< Number of fragments, leave it zero or the same as
                                        ///  number of samples for normal AA; Set it to the
                                        ///  number of fragments for EQAA
    /// r800 and later HWL parameters
    // Used for 1D tiling above
    AddrTileType    tileType;           ///< See defintion of AddrTileType
    struct
    {
        UINT_32     ignoreSE : 1;       ///< TRUE if shader engines are ignored. This is texture
                                        ///  only flag. Only non-RT texture can set this to TRUE
        UINT_32     reserved :31;       ///< Reserved for future use.
    };
    // 2D tiling needs following structure
    ADDR_TILEINFO*  pTileInfo;          ///< 2D tile parameters. Client must provide all data
    INT_32          tileIndex;          ///< Tile index, MUST be -1 if you don't want to use it
                                        ///  while the global useTileIndex is set to 1
    union
    {
        struct
        {
            UINT_32  bankSwizzle;       ///< Bank swizzle
            UINT_32  pipeSwizzle;       ///< Pipe swizzle
        };
        UINT_32     tileSwizzle;        ///< Combined swizzle, if useCombinedSwizzle is TRUE
    };

#if ADDR_AM_BUILD // These two fields are not valid in SW blt since no HTILE access
    UINT_32         addr5Swizzle;       ///< ADDR5_SWIZZLE_MASK of DB_DEPTH_INFO
    BOOL_32         is32ByteTile;       ///< Caller must have access to HTILE buffer and know if
                                        ///  this tile is compressed to 32B
#endif
} ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT
*
*   @brief
*       Output structure for AddrComputeSurfaceAddrFromCoord
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT
{
    UINT_32 size;           ///< Size of this structure in bytes

    UINT_64 addr;           ///< Byte address
    UINT_32 bitPosition;    ///< Bit position within surfaceAddr, 0-7.
                            ///  For surface bpp < 8, e.g. FMT_1.
    UINT_32 prtBlockIndex;  ///< Index of a PRT tile (64K block)
} ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeSurfaceAddrFromCoord
*
*   @brief
*       Compute surface address from a given coordinate.
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeSurfaceAddrFromCoord(
    ADDR_HANDLE                                     hLib,
    const ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT* pIn,
    ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT*      pOut);



/**
***************************************************************************************************
*   ADDR_COMPUTE_SURFACE_COORDFROMADDR_INPUT
*
*   @brief
*       Input structure for AddrComputeSurfaceCoordFromAddr
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_SURFACE_COORDFROMADDR_INPUT
{
    UINT_32         size;               ///< Size of this structure in bytes

    UINT_64         addr;               ///< Address in bytes
    UINT_32         bitPosition;        ///< Bit position in addr. 0-7. for surface bpp < 8,
                                        ///  e.g. FMT_1;
    UINT_32         bpp;                ///< Bits per pixel
    UINT_32         pitch;              ///< Pitch, in pixels
    UINT_32         height;             ///< Height in pixels
    UINT_32         numSlices;          ///< Surface depth
    UINT_32         numSamples;         ///< Number of samples

    AddrTileMode    tileMode;           ///< Tile mode
    BOOL_32         isDepth;            ///< Surface uses depth sample ordering within micro tile.
                                        ///  Note: Textures can choose depth sample order as well.
    UINT_32         tileBase;           ///< Base offset (in bits) inside micro tile which handles
                                        ///  the case that components are stored separately
    UINT_32         compBits;           ///< The component bits actually needed(for planar surface)

    UINT_32         numFrags;           ///< Number of fragments, leave it zero or the same as
                                        ///  number of samples for normal AA; Set it to the
                                        ///  number of fragments for EQAA
    /// r800 and later HWL parameters
    // Used for 1D tiling above
    AddrTileType    tileType;           ///< See defintion of AddrTileType
    struct
    {
        UINT_32     ignoreSE : 1;       ///< TRUE if shader engines are ignored. This is texture
                                        ///  only flag. Only non-RT texture can set this to TRUE
        UINT_32     reserved :31;       ///< Reserved for future use.
    };
    // 2D tiling needs following structure
    ADDR_TILEINFO*  pTileInfo;          ///< 2D tile parameters. Client must provide all data
    INT_32          tileIndex;          ///< Tile index, MUST be -1 if you don't want to use it
                                        ///  while the global useTileIndex is set to 1
    union
    {
        struct
        {
            UINT_32  bankSwizzle;       ///< Bank swizzle
            UINT_32  pipeSwizzle;       ///< Pipe swizzle
        };
        UINT_32     tileSwizzle;        ///< Combined swizzle, if useCombinedSwizzle is TRUE
    };
} ADDR_COMPUTE_SURFACE_COORDFROMADDR_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_SURFACE_COORDFROMADDR_OUTPUT
*
*   @brief
*       Output structure for AddrComputeSurfaceCoordFromAddr
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_SURFACE_COORDFROMADDR_OUTPUT
{
    UINT_32 size;   ///< Size of this structure in bytes

    UINT_32 x;      ///< X coordinate
    UINT_32 y;      ///< Y coordinate
    UINT_32 slice;  ///< Index of slices
    UINT_32 sample; ///< Index of samples, means fragment index for EQAA
} ADDR_COMPUTE_SURFACE_COORDFROMADDR_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeSurfaceCoordFromAddr
*
*   @brief
*       Compute coordinate from a given surface address
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeSurfaceCoordFromAddr(
    ADDR_HANDLE                                     hLib,
    const ADDR_COMPUTE_SURFACE_COORDFROMADDR_INPUT* pIn,
    ADDR_COMPUTE_SURFACE_COORDFROMADDR_OUTPUT*      pOut);

///////////////////////////////////////////////////////////////////////////////////////////////////
//                                   HTile functions
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
***************************************************************************************************
*   ADDR_HTILE_FLAGS
*
*   @brief
*       HTILE flags
***************************************************************************************************
*/
typedef union _ADDR_HTILE_FLAGS
{
    struct
    {
        UINT_32 tcCompatible  : 1; ///< Flag indicates surface needs to be shader readable
        UINT_32 reserved      :31; ///< Reserved bits
    };

    UINT_32 value;
} ADDR_HTILE_FLAGS;

/**
***************************************************************************************************
*   ADDR_COMPUTE_HTILE_INFO_INPUT
*
*   @brief
*       Input structure of AddrComputeHtileInfo
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_HTILE_INFO_INPUT
{
    UINT_32            size;            ///< Size of this structure in bytes

    ADDR_HTILE_FLAGS   flags;           ///< HTILE flags
    UINT_32            pitch;           ///< Surface pitch, in pixels
    UINT_32            height;          ///< Surface height, in pixels
    UINT_32            numSlices;       ///< Number of slices
    BOOL_32            isLinear;        ///< Linear or tiled HTILE layout
    AddrHtileBlockSize blockWidth;      ///< 4 or 8. EG above only support 8
    AddrHtileBlockSize blockHeight;     ///< 4 or 8. EG above only support 8
    ADDR_TILEINFO*     pTileInfo;       ///< Tile info

    INT_32             tileIndex;       ///< Tile index, MUST be -1 if you don't want to use it
                                        ///  while the global useTileIndex is set to 1
    INT_32             macroModeIndex;  ///< Index in macro tile mode table if there is one (CI)
                                        ///< README: When tileIndex is not -1, this must be valid
} ADDR_COMPUTE_HTILE_INFO_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_HTILE_INFO_OUTPUT
*
*   @brief
*       Output structure of AddrComputeHtileInfo
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_HTILE_INFO_OUTPUT
{
    UINT_32 size;           ///< Size of this structure in bytes

    UINT_32 pitch;          ///< Pitch in pixels of depth buffer represented in this
                            ///  HTile buffer. This might be larger than original depth
                            ///  buffer pitch when called with an unaligned pitch.
    UINT_32 height;         ///< Height in pixels, as above
    UINT_64 htileBytes;     ///< Size of HTILE buffer, in bytes
    UINT_32 baseAlign;      ///< Base alignment
    UINT_32 bpp;            ///< Bits per pixel for HTILE is how many bits for an 8x8 block!
    UINT_32 macroWidth;     ///< Macro width in pixels, actually squared cache shape
    UINT_32 macroHeight;    ///< Macro height in pixels
    UINT_64 sliceSize;      ///< Slice size, in bytes.
} ADDR_COMPUTE_HTILE_INFO_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeHtileInfo
*
*   @brief
*       Compute Htile pitch, height, base alignment and size in bytes
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeHtileInfo(
    ADDR_HANDLE                             hLib,
    const ADDR_COMPUTE_HTILE_INFO_INPUT*    pIn,
    ADDR_COMPUTE_HTILE_INFO_OUTPUT*         pOut);



/**
***************************************************************************************************
*   ADDR_COMPUTE_HTILE_ADDRFROMCOORD_INPUT
*
*   @brief
*       Input structure for AddrComputeHtileAddrFromCoord
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_HTILE_ADDRFROMCOORD_INPUT
{
    UINT_32            size;            ///< Size of this structure in bytes

    UINT_32            pitch;           ///< Pitch, in pixels
    UINT_32            height;          ///< Height in pixels
    UINT_32            x;               ///< X coordinate
    UINT_32            y;               ///< Y coordinate
    UINT_32            slice;           ///< Index of slice
    UINT_32            numSlices;       ///< Number of slices
    BOOL_32            isLinear;        ///< Linear or tiled HTILE layout
    AddrHtileBlockSize blockWidth;      ///< 4 or 8. 1 means 8, 0 means 4. EG above only support 8
    AddrHtileBlockSize blockHeight;     ///< 4 or 8. 1 means 8, 0 means 4. EG above only support 8
    ADDR_TILEINFO*     pTileInfo;       ///< Tile info

    INT_32             tileIndex;       ///< Tile index, MUST be -1 if you don't want to use it
                                        ///  while the global useTileIndex is set to 1
    INT_32             macroModeIndex;  ///< Index in macro tile mode table if there is one (CI)
                                        ///< README: When tileIndex is not -1, this must be valid
} ADDR_COMPUTE_HTILE_ADDRFROMCOORD_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_HTILE_ADDRFROMCOORD_OUTPUT
*
*   @brief
*       Output structure for AddrComputeHtileAddrFromCoord
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_HTILE_ADDRFROMCOORD_OUTPUT
{
    UINT_32 size;           ///< Size of this structure in bytes

    UINT_64 addr;           ///< Address in bytes
    UINT_32 bitPosition;    ///< Bit position, 0 or 4. CMASK and HTILE shares some lib method.
                            ///  So we keep bitPosition for HTILE as well
} ADDR_COMPUTE_HTILE_ADDRFROMCOORD_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeHtileAddrFromCoord
*
*   @brief
*       Compute Htile address according to coordinates (of depth buffer)
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeHtileAddrFromCoord(
    ADDR_HANDLE                                     hLib,
    const ADDR_COMPUTE_HTILE_ADDRFROMCOORD_INPUT*   pIn,
    ADDR_COMPUTE_HTILE_ADDRFROMCOORD_OUTPUT*        pOut);



/**
***************************************************************************************************
*   ADDR_COMPUTE_HTILE_COORDFROMADDR_INPUT
*
*   @brief
*       Input structure for AddrComputeHtileCoordFromAddr
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_HTILE_COORDFROMADDR_INPUT
{
    UINT_32            size;            ///< Size of this structure in bytes

    UINT_64            addr;            ///< Address
    UINT_32            bitPosition;     ///< Bit position 0 or 4. CMASK and HTILE share some methods
                                        ///  so we keep bitPosition for HTILE as well
    UINT_32            pitch;           ///< Pitch, in pixels
    UINT_32            height;          ///< Height, in pixels
    UINT_32            numSlices;       ///< Number of slices
    BOOL_32            isLinear;        ///< Linear or tiled HTILE layout
    AddrHtileBlockSize blockWidth;      ///< 4 or 8. 1 means 8, 0 means 4. R8xx/R9xx only support 8
    AddrHtileBlockSize blockHeight;     ///< 4 or 8. 1 means 8, 0 means 4. R8xx/R9xx only support 8
    ADDR_TILEINFO*     pTileInfo;       ///< Tile info

    INT_32             tileIndex;       ///< Tile index, MUST be -1 if you don't want to use it
                                        ///  while the global useTileIndex is set to 1
    INT_32             macroModeIndex;  ///< Index in macro tile mode table if there is one (CI)
                                        ///< README: When tileIndex is not -1, this must be valid
} ADDR_COMPUTE_HTILE_COORDFROMADDR_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_HTILE_COORDFROMADDR_OUTPUT
*
*   @brief
*       Output structure for AddrComputeHtileCoordFromAddr
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_HTILE_COORDFROMADDR_OUTPUT
{
    UINT_32 size;   ///< Size of this structure in bytes

    UINT_32 x;      ///< X coordinate
    UINT_32 y;      ///< Y coordinate
    UINT_32 slice;  ///< Slice index
} ADDR_COMPUTE_HTILE_COORDFROMADDR_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeHtileCoordFromAddr
*
*   @brief
*       Compute coordinates within depth buffer (1st pixel of a micro tile) according to
*       Htile address
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeHtileCoordFromAddr(
    ADDR_HANDLE                                     hLib,
    const ADDR_COMPUTE_HTILE_COORDFROMADDR_INPUT*   pIn,
    ADDR_COMPUTE_HTILE_COORDFROMADDR_OUTPUT*        pOut);



///////////////////////////////////////////////////////////////////////////////////////////////////
//                                     C-mask functions
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
***************************************************************************************************
*   ADDR_CMASK_FLAGS
*
*   @brief
*       CMASK flags
***************************************************************************************************
*/
typedef union _ADDR_CMASK_FLAGS
{
    struct
    {
        UINT_32 tcCompatible  : 1; ///< Flag indicates surface needs to be shader readable
        UINT_32 reserved      :31; ///< Reserved bits
    };

    UINT_32 value;
} ADDR_CMASK_FLAGS;

/**
***************************************************************************************************
*   ADDR_COMPUTE_CMASK_INFO_INPUT
*
*   @brief
*       Input structure of AddrComputeCmaskInfo
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_CMASKINFO_INPUT
{
    UINT_32             size;            ///< Size of this structure in bytes

    ADDR_CMASK_FLAGS    flags;           ///< CMASK flags
    UINT_32             pitch;           ///< Pitch, in pixels, of color buffer
    UINT_32             height;          ///< Height, in pixels, of color buffer
    UINT_32             numSlices;       ///< Number of slices, of color buffer
    BOOL_32             isLinear;        ///< Linear or tiled layout, Only SI can be linear
    ADDR_TILEINFO*      pTileInfo;       ///< Tile info

    INT_32              tileIndex;       ///< Tile index, MUST be -1 if you don't want to use it
                                         ///  while the global useTileIndex is set to 1
    INT_32              macroModeIndex;  ///< Index in macro tile mode table if there is one (CI)
                                         ///< README: When tileIndex is not -1, this must be valid
} ADDR_COMPUTE_CMASK_INFO_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_CMASK_INFO_OUTPUT
*
*   @brief
*       Output structure of AddrComputeCmaskInfo
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_CMASK_INFO_OUTPUT
{
    UINT_32 size;           ///< Size of this structure in bytes

    UINT_32 pitch;          ///< Pitch in pixels of color buffer which
                            ///  this Cmask matches. The size might be larger than
                            ///  original color buffer pitch when called with
                            ///  an unaligned pitch.
    UINT_32 height;         ///< Height in pixels, as above
    UINT_64 cmaskBytes;     ///< Size in bytes of CMask buffer
    UINT_32 baseAlign;      ///< Base alignment
    UINT_32 blockMax;       ///< Cmask block size. Need this to set CB_COLORn_MASK register
    UINT_32 macroWidth;     ///< Macro width in pixels, actually squared cache shape
    UINT_32 macroHeight;    ///< Macro height in pixels
    UINT_64 sliceSize;      ///< Slice size, in bytes.
} ADDR_COMPUTE_CMASK_INFO_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeCmaskInfo
*
*   @brief
*       Compute Cmask pitch, height, base alignment and size in bytes from color buffer
*       info
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeCmaskInfo(
    ADDR_HANDLE                             hLib,
    const ADDR_COMPUTE_CMASK_INFO_INPUT*    pIn,
    ADDR_COMPUTE_CMASK_INFO_OUTPUT*         pOut);



/**
***************************************************************************************************
*   ADDR_COMPUTE_CMASK_ADDRFROMCOORD_INPUT
*
*   @brief
*       Input structure for AddrComputeCmaskAddrFromCoord
*
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_CMASK_ADDRFROMCOORD_INPUT
{
    UINT_32          size;           ///< Size of this structure in bytes
    UINT_32          x;              ///< X coordinate
    UINT_32          y;              ///< Y coordinate
    UINT_64          fmaskAddr;      ///< Fmask addr for tc compatible Cmask
    UINT_32          slice;          ///< Slice index
    UINT_32          pitch;          ///< Pitch in pixels, of color buffer
    UINT_32          height;         ///< Height in pixels, of color buffer
    UINT_32          numSlices;      ///< Number of slices
    UINT_32          bpp;
    BOOL_32          isLinear;       ///< Linear or tiled layout, Only SI can be linear
    ADDR_CMASK_FLAGS flags;          ///< CMASK flags
    ADDR_TILEINFO*   pTileInfo;      ///< Tile info

    INT_32           tileIndex;      ///< Tile index, MUST be -1 if you don't want to use it
                                     ///< while the global useTileIndex is set to 1
    INT_32           macroModeIndex; ///< Index in macro tile mode table if there is one (CI)
                                     ///< README: When tileIndex is not -1, this must be valid
} ADDR_COMPUTE_CMASK_ADDRFROMCOORD_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT
*
*   @brief
*       Output structure for AddrComputeCmaskAddrFromCoord
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT
{
    UINT_32 size;           ///< Size of this structure in bytes

    UINT_64 addr;           ///< CMASK address in bytes
    UINT_32 bitPosition;    ///< Bit position within addr, 0-7. CMASK is 4 bpp,
                            ///  so the address may be located in bit 0 (0) or 4 (4)
} ADDR_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeCmaskAddrFromCoord
*
*   @brief
*       Compute Cmask address according to coordinates (of MSAA color buffer)
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeCmaskAddrFromCoord(
    ADDR_HANDLE                                     hLib,
    const ADDR_COMPUTE_CMASK_ADDRFROMCOORD_INPUT*   pIn,
    ADDR_COMPUTE_CMASK_ADDRFROMCOORD_OUTPUT*        pOut);



/**
***************************************************************************************************
*   ADDR_COMPUTE_CMASK_COORDFROMADDR_INPUT
*
*   @brief
*       Input structure for AddrComputeCmaskCoordFromAddr
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_CMASK_COORDFROMADDR_INPUT
{
    UINT_32        size;            ///< Size of this structure in bytes

    UINT_64        addr;            ///< CMASK address in bytes
    UINT_32        bitPosition;     ///< Bit position within addr, 0-7. CMASK is 4 bpp,
                                    ///  so the address may be located in bit 0 (0) or 4 (4)
    UINT_32        pitch;           ///< Pitch, in pixels
    UINT_32        height;          ///< Height in pixels
    UINT_32        numSlices;       ///< Number of slices
    BOOL_32        isLinear;        ///< Linear or tiled layout, Only SI can be linear
    ADDR_TILEINFO* pTileInfo;       ///< Tile info

    INT_32         tileIndex;       ///< Tile index, MUST be -1 if you don't want to use it
                                    ///  while the global useTileIndex is set to 1
    INT_32         macroModeIndex;  ///< Index in macro tile mode table if there is one (CI)
                                    ///< README: When tileIndex is not -1, this must be valid
} ADDR_COMPUTE_CMASK_COORDFROMADDR_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_CMASK_COORDFROMADDR_OUTPUT
*
*   @brief
*       Output structure for AddrComputeCmaskCoordFromAddr
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_CMASK_COORDFROMADDR_OUTPUT
{
    UINT_32 size;   ///< Size of this structure in bytes

    UINT_32 x;      ///< X coordinate
    UINT_32 y;      ///< Y coordinate
    UINT_32 slice;  ///< Slice index
} ADDR_COMPUTE_CMASK_COORDFROMADDR_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeCmaskCoordFromAddr
*
*   @brief
*       Compute coordinates within color buffer (1st pixel of a micro tile) according to
*       Cmask address
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeCmaskCoordFromAddr(
    ADDR_HANDLE                                     hLib,
    const ADDR_COMPUTE_CMASK_COORDFROMADDR_INPUT*   pIn,
    ADDR_COMPUTE_CMASK_COORDFROMADDR_OUTPUT*        pOut);



///////////////////////////////////////////////////////////////////////////////////////////////////
//                                     F-mask functions
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
***************************************************************************************************
*   ADDR_COMPUTE_FMASK_INFO_INPUT
*
*   @brief
*       Input structure for AddrComputeFmaskInfo
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_FMASK_INFO_INPUT
{
    UINT_32         size;               ///< Size of this structure in bytes

    AddrTileMode    tileMode;           ///< Tile mode
    UINT_32         pitch;              ///< Surface pitch, in pixels
    UINT_32         height;             ///< Surface height, in pixels
    UINT_32         numSlices;          ///< Number of slice/depth
    UINT_32         numSamples;         ///< Number of samples
    UINT_32         numFrags;           ///< Number of fragments, leave it zero or the same as
                                        ///  number of samples for normal AA; Set it to the
                                        ///  number of fragments for EQAA
    /// r800 and later HWL parameters
    struct
    {
        UINT_32 resolved:   1;          ///< TRUE if the surface is for resolved fmask, only used
                                        ///  by H/W clients. S/W should always set it to FALSE.
        UINT_32 reserved:  31;          ///< Reserved for future use.
    };
    ADDR_TILEINFO*  pTileInfo;          ///< 2D tiling parameters. Clients must give valid data
    INT_32          tileIndex;          ///< Tile index, MUST be -1 if you don't want to use it
                                        ///  while the global useTileIndex is set to 1
} ADDR_COMPUTE_FMASK_INFO_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_FMASK_INFO_OUTPUT
*
*   @brief
*       Output structure for AddrComputeFmaskInfo
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_FMASK_INFO_OUTPUT
{
    UINT_32         size;           ///< Size of this structure in bytes

    UINT_32         pitch;          ///< Pitch of fmask in pixels
    UINT_32         height;         ///< Height of fmask in pixels
    UINT_32         numSlices;      ///< Slices of fmask
    UINT_64         fmaskBytes;     ///< Size of fmask in bytes
    UINT_32         baseAlign;      ///< Base address alignment
    UINT_32         pitchAlign;     ///< Pitch alignment
    UINT_32         heightAlign;    ///< Height alignment
    UINT_32         bpp;            ///< Bits per pixel of FMASK is: number of bit planes
    UINT_32         numSamples;     ///< Number of samples, used for dump, export this since input
                                    ///  may be changed in 9xx and above
    /// r800 and later HWL parameters
    ADDR_TILEINFO*  pTileInfo;      ///< Tile parameters used. Fmask can have different
                                    ///  bank_height from color buffer
    INT_32          tileIndex;      ///< Tile index, MUST be -1 if you don't want to use it
                                    ///  while the global useTileIndex is set to 1
    INT_32          macroModeIndex; ///< Index in macro tile mode table if there is one (CI)
    UINT_64         sliceSize;      ///< Size of slice in bytes
} ADDR_COMPUTE_FMASK_INFO_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeFmaskInfo
*
*   @brief
*       Compute Fmask pitch/height/depth/alignments and size in bytes
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeFmaskInfo(
    ADDR_HANDLE                             hLib,
    const ADDR_COMPUTE_FMASK_INFO_INPUT*    pIn,
    ADDR_COMPUTE_FMASK_INFO_OUTPUT*         pOut);



/**
***************************************************************************************************
*   ADDR_COMPUTE_FMASK_ADDRFROMCOORD_INPUT
*
*   @brief
*       Input structure for AddrComputeFmaskAddrFromCoord
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_FMASK_ADDRFROMCOORD_INPUT
{
    UINT_32         size;               ///< Size of this structure in bytes

    UINT_32         x;                  ///< X coordinate
    UINT_32         y;                  ///< Y coordinate
    UINT_32         slice;              ///< Slice index
    UINT_32         plane;              ///< Plane number
    UINT_32         sample;             ///< Sample index (fragment index for EQAA)

    UINT_32         pitch;              ///< Surface pitch, in pixels
    UINT_32         height;             ///< Surface height, in pixels
    UINT_32         numSamples;         ///< Number of samples
    UINT_32         numFrags;           ///< Number of fragments, leave it zero or the same as
                                        ///  number of samples for normal AA; Set it to the
                                        ///  number of fragments for EQAA

    AddrTileMode    tileMode;           ///< Tile mode
    union
    {
        struct
        {
            UINT_32  bankSwizzle;       ///< Bank swizzle
            UINT_32  pipeSwizzle;       ///< Pipe swizzle
        };
        UINT_32     tileSwizzle;        ///< Combined swizzle, if useCombinedSwizzle is TRUE
    };

    /// r800 and later HWL parameters
    struct
    {
        UINT_32 resolved:   1;          ///< TRUE if this is a resolved fmask, used by H/W clients
        UINT_32 ignoreSE:   1;          ///< TRUE if shader engines are ignored.
        UINT_32 reserved:  30;          ///< Reserved for future use.
    };
    ADDR_TILEINFO*  pTileInfo;          ///< 2D tiling parameters. Client must provide all data

} ADDR_COMPUTE_FMASK_ADDRFROMCOORD_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_FMASK_ADDRFROMCOORD_OUTPUT
*
*   @brief
*       Output structure for AddrComputeFmaskAddrFromCoord
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_FMASK_ADDRFROMCOORD_OUTPUT
{
    UINT_32 size;           ///< Size of this structure in bytes

    UINT_64 addr;           ///< Fmask address
    UINT_32 bitPosition;    ///< Bit position within fmaskAddr, 0-7.
} ADDR_COMPUTE_FMASK_ADDRFROMCOORD_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeFmaskAddrFromCoord
*
*   @brief
*       Compute Fmask address according to coordinates (x,y,slice,sample,plane)
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeFmaskAddrFromCoord(
    ADDR_HANDLE                                     hLib,
    const ADDR_COMPUTE_FMASK_ADDRFROMCOORD_INPUT*   pIn,
    ADDR_COMPUTE_FMASK_ADDRFROMCOORD_OUTPUT*        pOut);



/**
***************************************************************************************************
*   ADDR_COMPUTE_FMASK_COORDFROMADDR_INPUT
*
*   @brief
*       Input structure for AddrComputeFmaskCoordFromAddr
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_FMASK_COORDFROMADDR_INPUT
{
    UINT_32         size;               ///< Size of this structure in bytes

    UINT_64         addr;               ///< Address
    UINT_32         bitPosition;        ///< Bit position within addr, 0-7.

    UINT_32         pitch;              ///< Pitch, in pixels
    UINT_32         height;             ///< Height in pixels
    UINT_32         numSamples;         ///< Number of samples
    UINT_32         numFrags;           ///< Number of fragments
    AddrTileMode    tileMode;           ///< Tile mode
    union
    {
        struct
        {
            UINT_32  bankSwizzle;       ///< Bank swizzle
            UINT_32  pipeSwizzle;       ///< Pipe swizzle
        };
        UINT_32     tileSwizzle;        ///< Combined swizzle, if useCombinedSwizzle is TRUE
    };

    /// r800 and later HWL parameters
    struct
    {
        UINT_32 resolved:   1;          ///< TRUE if this is a resolved fmask, used by HW components
        UINT_32 ignoreSE:   1;          ///< TRUE if shader engines are ignored.
        UINT_32 reserved:  30;          ///< Reserved for future use.
    };
    ADDR_TILEINFO*  pTileInfo;          ///< 2D tile parameters. Client must provide all data

} ADDR_COMPUTE_FMASK_COORDFROMADDR_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_FMASK_COORDFROMADDR_OUTPUT
*
*   @brief
*       Output structure for AddrComputeFmaskCoordFromAddr
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_FMASK_COORDFROMADDR_OUTPUT
{
    UINT_32 size;       ///< Size of this structure in bytes

    UINT_32 x;          ///< X coordinate
    UINT_32 y;          ///< Y coordinate
    UINT_32 slice;      ///< Slice index
    UINT_32 plane;      ///< Plane number
    UINT_32 sample;     ///< Sample index (fragment index for EQAA)
} ADDR_COMPUTE_FMASK_COORDFROMADDR_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeFmaskCoordFromAddr
*
*   @brief
*       Compute FMASK coordinate from an given address
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeFmaskCoordFromAddr(
    ADDR_HANDLE                                     hLib,
    const ADDR_COMPUTE_FMASK_COORDFROMADDR_INPUT*   pIn,
    ADDR_COMPUTE_FMASK_COORDFROMADDR_OUTPUT*        pOut);



///////////////////////////////////////////////////////////////////////////////////////////////////
//                          Element/utility functions
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
***************************************************************************************************
*   AddrGetVersion
*
*   @brief
*       Get AddrLib version number
***************************************************************************************************
*/
UINT_32 ADDR_API AddrGetVersion(ADDR_HANDLE hLib);

/**
***************************************************************************************************
*   AddrUseTileIndex
*
*   @brief
*       Return TRUE if tileIndex is enabled in this address library
***************************************************************************************************
*/
BOOL_32 ADDR_API AddrUseTileIndex(ADDR_HANDLE hLib);

/**
***************************************************************************************************
*   AddrUseCombinedSwizzle
*
*   @brief
*       Return TRUE if combined swizzle is enabled in this address library
***************************************************************************************************
*/
BOOL_32 ADDR_API AddrUseCombinedSwizzle(ADDR_HANDLE hLib);

/**
***************************************************************************************************
*   ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT
*
*   @brief
*       Input structure of AddrExtractBankPipeSwizzle
***************************************************************************************************
*/
typedef struct _ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT
{
    UINT_32         size;           ///< Size of this structure in bytes

    UINT_32         base256b;       ///< Base256b value

    /// r800 and later HWL parameters
    ADDR_TILEINFO*  pTileInfo;      ///< 2D tile parameters. Client must provide all data

    INT_32          tileIndex;      ///< Tile index, MUST be -1 if you don't want to use it
                                    ///  while the global useTileIndex is set to 1
    INT_32          macroModeIndex; ///< Index in macro tile mode table if there is one (CI)
                                    ///< README: When tileIndex is not -1, this must be valid
} ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT;

/**
***************************************************************************************************
*   ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT
*
*   @brief
*       Output structure of AddrExtractBankPipeSwizzle
***************************************************************************************************
*/
typedef struct _ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT
{
    UINT_32 size;           ///< Size of this structure in bytes

    UINT_32 bankSwizzle;    ///< Bank swizzle
    UINT_32 pipeSwizzle;    ///< Pipe swizzle
} ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT;

/**
***************************************************************************************************
*   AddrExtractBankPipeSwizzle
*
*   @brief
*       Extract Bank and Pipe swizzle from base256b
*   @return
*       ADDR_OK if no error
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrExtractBankPipeSwizzle(
    ADDR_HANDLE                                 hLib,
    const ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT*  pIn,
    ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT*       pOut);


/**
***************************************************************************************************
*   ADDR_COMBINE_BANKPIPE_SWIZZLE_INPUT
*
*   @brief
*       Input structure of AddrCombineBankPipeSwizzle
***************************************************************************************************
*/
typedef struct _ADDR_COMBINE_BANKPIPE_SWIZZLE_INPUT
{
    UINT_32         size;           ///< Size of this structure in bytes

    UINT_32         bankSwizzle;    ///< Bank swizzle
    UINT_32         pipeSwizzle;    ///< Pipe swizzle
    UINT_64         baseAddr;       ///< Base address (leave it zero for driver clients)

    /// r800 and later HWL parameters
    ADDR_TILEINFO*  pTileInfo;      ///< 2D tile parameters. Client must provide all data

    INT_32          tileIndex;      ///< Tile index, MUST be -1 if you don't want to use it
                                    ///  while the global useTileIndex is set to 1
    INT_32          macroModeIndex; ///< Index in macro tile mode table if there is one (CI)
                                    ///< README: When tileIndex is not -1, this must be valid
} ADDR_COMBINE_BANKPIPE_SWIZZLE_INPUT;

/**
***************************************************************************************************
*   ADDR_COMBINE_BANKPIPE_SWIZZLE_OUTPUT
*
*   @brief
*       Output structure of AddrCombineBankPipeSwizzle
***************************************************************************************************
*/
typedef struct _ADDR_COMBINE_BANKPIPE_SWIZZLE_OUTPUT
{
    UINT_32 size;           ///< Size of this structure in bytes

    UINT_32 tileSwizzle;    ///< Combined swizzle
} ADDR_COMBINE_BANKPIPE_SWIZZLE_OUTPUT;

/**
***************************************************************************************************
*   AddrCombineBankPipeSwizzle
*
*   @brief
*       Combine Bank and Pipe swizzle
*   @return
*       ADDR_OK if no error
*   @note
*       baseAddr here is full MCAddress instead of base256b
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrCombineBankPipeSwizzle(
    ADDR_HANDLE                                 hLib,
    const ADDR_COMBINE_BANKPIPE_SWIZZLE_INPUT*  pIn,
    ADDR_COMBINE_BANKPIPE_SWIZZLE_OUTPUT*       pOut);



/**
***************************************************************************************************
*   ADDR_COMPUTE_SLICESWIZZLE_INPUT
*
*   @brief
*       Input structure of AddrComputeSliceSwizzle
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_SLICESWIZZLE_INPUT
{
    UINT_32         size;               ///< Size of this structure in bytes

    AddrTileMode    tileMode;           ///< Tile Mode
    UINT_32         baseSwizzle;        ///< Base tile swizzle
    UINT_32         slice;              ///< Slice index
    UINT_64         baseAddr;           ///< Base address, driver should leave it 0 in most cases

    /// r800 and later HWL parameters
    ADDR_TILEINFO*  pTileInfo;          ///< 2D tile parameters. Actually banks needed here!

    INT_32          tileIndex;          ///< Tile index, MUST be -1 if you don't want to use it
                                        ///  while the global useTileIndex is set to 1
    INT_32          macroModeIndex;     ///< Index in macro tile mode table if there is one (CI)
                                        ///< README: When tileIndex is not -1, this must be valid
} ADDR_COMPUTE_SLICESWIZZLE_INPUT;



/**
***************************************************************************************************
*   ADDR_COMPUTE_SLICESWIZZLE_OUTPUT
*
*   @brief
*       Output structure of AddrComputeSliceSwizzle
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_SLICESWIZZLE_OUTPUT
{
    UINT_32  size;           ///< Size of this structure in bytes

    UINT_32  tileSwizzle;    ///< Recalculated tileSwizzle value
} ADDR_COMPUTE_SLICESWIZZLE_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeSliceSwizzle
*
*   @brief
*       Extract Bank and Pipe swizzle from base256b
*   @return
*       ADDR_OK if no error
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeSliceSwizzle(
    ADDR_HANDLE                             hLib,
    const ADDR_COMPUTE_SLICESWIZZLE_INPUT*  pIn,
    ADDR_COMPUTE_SLICESWIZZLE_OUTPUT*       pOut);


/**
***************************************************************************************************
*   AddrSwizzleGenOption
*
*   @brief
*       Which swizzle generating options: legacy or linear
***************************************************************************************************
*/
typedef enum _AddrSwizzleGenOption
{
    ADDR_SWIZZLE_GEN_DEFAULT    = 0,    ///< As is in client driver implemention for swizzle
    ADDR_SWIZZLE_GEN_LINEAR     = 1,    ///< Using a linear increment of swizzle
} AddrSwizzleGenOption;

/**
***************************************************************************************************
*   AddrSwizzleOption
*
*   @brief
*       Controls how swizzle is generated
***************************************************************************************************
*/
typedef union _ADDR_SWIZZLE_OPTION
{
    struct
    {
        UINT_32 genOption       : 1;    ///< The way swizzle is generated, see AddrSwizzleGenOption
        UINT_32 reduceBankBit   : 1;    ///< TRUE if we need reduce swizzle bits
        UINT_32 reserved        :30;    ///< Reserved bits
    };

    UINT_32 value;

} ADDR_SWIZZLE_OPTION;

/**
***************************************************************************************************
*   ADDR_COMPUTE_BASE_SWIZZLE_INPUT
*
*   @brief
*       Input structure of AddrComputeBaseSwizzle
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_BASE_SWIZZLE_INPUT
{
    UINT_32             size;           ///< Size of this structure in bytes

    ADDR_SWIZZLE_OPTION option;         ///< Swizzle option
    UINT_32             surfIndex;      ///< Index of this surface type
    AddrTileMode        tileMode;       ///< Tile Mode

    /// r800 and later HWL parameters
    ADDR_TILEINFO*      pTileInfo;      ///< 2D tile parameters. Actually banks needed here!

    INT_32              tileIndex;      ///< Tile index, MUST be -1 if you don't want to use it
                                        ///  while the global useTileIndex is set to 1
    INT_32              macroModeIndex; ///< Index in macro tile mode table if there is one (CI)
                                        ///< README: When tileIndex is not -1, this must be valid
} ADDR_COMPUTE_BASE_SWIZZLE_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT
*
*   @brief
*       Output structure of AddrComputeBaseSwizzle
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT
{
    UINT_32 size;           ///< Size of this structure in bytes

    UINT_32 tileSwizzle;    ///< Combined swizzle
} ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeBaseSwizzle
*
*   @brief
*       Return a Combined Bank and Pipe swizzle base on surface based on surface type/index
*   @return
*       ADDR_OK if no error
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeBaseSwizzle(
    ADDR_HANDLE                             hLib,
    const ADDR_COMPUTE_BASE_SWIZZLE_INPUT*  pIn,
    ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT*       pOut);



/**
***************************************************************************************************
*   ELEM_GETEXPORTNORM_INPUT
*
*   @brief
*       Input structure for ElemGetExportNorm
*
***************************************************************************************************
*/
typedef struct _ELEM_GETEXPORTNORM_INPUT
{
    UINT_32             size;       ///< Size of this structure in bytes

    AddrColorFormat     format;     ///< Color buffer format; Client should use ColorFormat
    AddrSurfaceNumber   num;        ///< Surface number type; Client should use NumberType
    AddrSurfaceSwap     swap;       ///< Surface swap byte swap; Client should use SurfaceSwap
    UINT_32             numSamples; ///< Number of samples
} ELEM_GETEXPORTNORM_INPUT;

/**
***************************************************************************************************
*  ElemGetExportNorm
*
*   @brief
*       Helper function to check one format can be EXPORT_NUM, which is a register
*       CB_COLOR_INFO.SURFACE_FORMAT. FP16 can be reported as EXPORT_NORM for rv770 in r600
*       family
*   @note
*       The implementation is only for r600.
*       00 - EXPORT_FULL: PS exports are 4 pixels with 4 components with 32-bits-per-component. (two
*       clocks per export)
*       01 - EXPORT_NORM: PS exports are 4 pixels with 4 components with 16-bits-per-component. (one
*       clock per export)
*
***************************************************************************************************
*/
BOOL_32 ADDR_API ElemGetExportNorm(
    ADDR_HANDLE                     hLib,
    const ELEM_GETEXPORTNORM_INPUT* pIn);



/**
***************************************************************************************************
*   ELEM_FLT32TODEPTHPIXEL_INPUT
*
*   @brief
*       Input structure for addrFlt32ToDepthPixel
*
***************************************************************************************************
*/
typedef struct _ELEM_FLT32TODEPTHPIXEL_INPUT
{
    UINT_32         size;           ///< Size of this structure in bytes

    AddrDepthFormat format;         ///< Depth buffer format
    ADDR_FLT_32     comps[2];       ///< Component values (Z/stencil)
} ELEM_FLT32TODEPTHPIXEL_INPUT;

/**
***************************************************************************************************
*   ELEM_FLT32TODEPTHPIXEL_INPUT
*
*   @brief
*       Output structure for ElemFlt32ToDepthPixel
*
***************************************************************************************************
*/
typedef struct _ELEM_FLT32TODEPTHPIXEL_OUTPUT
{
    UINT_32 size;           ///< Size of this structure in bytes

    UINT_8* pPixel;         ///< Real depth value. Same data type as depth buffer.
                            ///  Client must provide enough storage for this type.
    UINT_32 depthBase;      ///< Tile base in bits for depth bits
    UINT_32 stencilBase;    ///< Tile base in bits for stencil bits
    UINT_32 depthBits;      ///< Bits for depth
    UINT_32 stencilBits;    ///< Bits for stencil
} ELEM_FLT32TODEPTHPIXEL_OUTPUT;

/**
***************************************************************************************************
*   ElemFlt32ToDepthPixel
*
*   @brief
*       Convert a FLT_32 value to a depth/stencil pixel value
*
*   @return
*       Return code
*
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API ElemFlt32ToDepthPixel(
    ADDR_HANDLE                         hLib,
    const ELEM_FLT32TODEPTHPIXEL_INPUT* pIn,
    ELEM_FLT32TODEPTHPIXEL_OUTPUT*      pOut);



/**
***************************************************************************************************
*   ELEM_FLT32TOCOLORPIXEL_INPUT
*
*   @brief
*       Input structure for addrFlt32ToColorPixel
*
***************************************************************************************************
*/
typedef struct _ELEM_FLT32TOCOLORPIXEL_INPUT
{
    UINT_32            size;           ///< Size of this structure in bytes

    AddrColorFormat    format;         ///< Color buffer format
    AddrSurfaceNumber  surfNum;        ///< Surface number
    AddrSurfaceSwap    surfSwap;       ///< Surface swap
    ADDR_FLT_32        comps[4];       ///< Component values (r/g/b/a)
} ELEM_FLT32TOCOLORPIXEL_INPUT;

/**
***************************************************************************************************
*   ELEM_FLT32TOCOLORPIXEL_INPUT
*
*   @brief
*       Output structure for ElemFlt32ToColorPixel
*
***************************************************************************************************
*/
typedef struct _ELEM_FLT32TOCOLORPIXEL_OUTPUT
{
    UINT_32 size;       ///< Size of this structure in bytes

    UINT_8* pPixel;     ///< Real color value. Same data type as color buffer.
                        ///  Client must provide enough storage for this type.
} ELEM_FLT32TOCOLORPIXEL_OUTPUT;

/**
***************************************************************************************************
*   ElemFlt32ToColorPixel
*
*   @brief
*       Convert a FLT_32 value to a red/green/blue/alpha pixel value
*
*   @return
*       Return code
*
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API ElemFlt32ToColorPixel(
    ADDR_HANDLE                         hLib,
    const ELEM_FLT32TOCOLORPIXEL_INPUT* pIn,
    ELEM_FLT32TOCOLORPIXEL_OUTPUT*      pOut);


/**
***************************************************************************************************
*   ADDR_CONVERT_TILEINFOTOHW_INPUT
*
*   @brief
*       Input structure for AddrConvertTileInfoToHW
*   @note
*       When reverse is TRUE, indices are igonred
***************************************************************************************************
*/
typedef struct _ADDR_CONVERT_TILEINFOTOHW_INPUT
{
    UINT_32         size;               ///< Size of this structure in bytes
    BOOL_32         reverse;            ///< Convert control flag.
                                        ///  FALSE: convert from real value to HW value;
                                        ///  TRUE: convert from HW value to real value.

    /// r800 and later HWL parameters
    ADDR_TILEINFO*  pTileInfo;          ///< Tile parameters with real value

    INT_32          tileIndex;          ///< Tile index, MUST be -1 if you don't want to use it
                                        ///  while the global useTileIndex is set to 1
    INT_32          macroModeIndex;     ///< Index in macro tile mode table if there is one (CI)
                                        ///< README: When tileIndex is not -1, this must be valid
} ADDR_CONVERT_TILEINFOTOHW_INPUT;

/**
***************************************************************************************************
*   ADDR_CONVERT_TILEINFOTOHW_OUTPUT
*
*   @brief
*       Output structure for AddrConvertTileInfoToHW
***************************************************************************************************
*/
typedef struct _ADDR_CONVERT_TILEINFOTOHW_OUTPUT
{
    UINT_32             size;               ///< Size of this structure in bytes

    /// r800 and later HWL parameters
    ADDR_TILEINFO*      pTileInfo;          ///< Tile parameters with hardware register value

} ADDR_CONVERT_TILEINFOTOHW_OUTPUT;

/**
***************************************************************************************************
*   AddrConvertTileInfoToHW
*
*   @brief
*       Convert tile info from real value to hardware register value
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrConvertTileInfoToHW(
    ADDR_HANDLE                             hLib,
    const ADDR_CONVERT_TILEINFOTOHW_INPUT*  pIn,
    ADDR_CONVERT_TILEINFOTOHW_OUTPUT*       pOut);



/**
***************************************************************************************************
*   ADDR_CONVERT_TILEINDEX_INPUT
*
*   @brief
*       Input structure for AddrConvertTileIndex
***************************************************************************************************
*/
typedef struct _ADDR_CONVERT_TILEINDEX_INPUT
{
    UINT_32         size;               ///< Size of this structure in bytes

    INT_32          tileIndex;          ///< Tile index
    INT_32          macroModeIndex;     ///< Index in macro tile mode table if there is one (CI)
    BOOL_32         tileInfoHw;         ///< Set to TRUE if client wants HW enum, otherwise actual
} ADDR_CONVERT_TILEINDEX_INPUT;

/**
***************************************************************************************************
*   ADDR_CONVERT_TILEINDEX_OUTPUT
*
*   @brief
*       Output structure for AddrConvertTileIndex
***************************************************************************************************
*/
typedef struct _ADDR_CONVERT_TILEINDEX_OUTPUT
{
    UINT_32             size;           ///< Size of this structure in bytes

    AddrTileMode        tileMode;       ///< Tile mode
    AddrTileType        tileType;       ///< Tile type
    ADDR_TILEINFO*      pTileInfo;      ///< Tile info

} ADDR_CONVERT_TILEINDEX_OUTPUT;

/**
***************************************************************************************************
*   AddrConvertTileIndex
*
*   @brief
*       Convert tile index to tile mode/type/info
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrConvertTileIndex(
    ADDR_HANDLE                         hLib,
    const ADDR_CONVERT_TILEINDEX_INPUT* pIn,
    ADDR_CONVERT_TILEINDEX_OUTPUT*      pOut);



/**
***************************************************************************************************
*   ADDR_CONVERT_TILEINDEX1_INPUT
*
*   @brief
*       Input structure for AddrConvertTileIndex1 (without macro mode index)
***************************************************************************************************
*/
typedef struct _ADDR_CONVERT_TILEINDEX1_INPUT
{
    UINT_32         size;               ///< Size of this structure in bytes

    INT_32          tileIndex;          ///< Tile index
    UINT_32         bpp;                ///< Bits per pixel
    UINT_32         numSamples;         ///< Number of samples
    BOOL_32         tileInfoHw;         ///< Set to TRUE if client wants HW enum, otherwise actual
} ADDR_CONVERT_TILEINDEX1_INPUT;

/**
***************************************************************************************************
*   AddrConvertTileIndex1
*
*   @brief
*       Convert tile index to tile mode/type/info
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrConvertTileIndex1(
    ADDR_HANDLE                             hLib,
    const ADDR_CONVERT_TILEINDEX1_INPUT*    pIn,
    ADDR_CONVERT_TILEINDEX_OUTPUT*          pOut);



/**
***************************************************************************************************
*   ADDR_GET_TILEINDEX_INPUT
*
*   @brief
*       Input structure for AddrGetTileIndex
***************************************************************************************************
*/
typedef struct _ADDR_GET_TILEINDEX_INPUT
{
    UINT_32         size;           ///< Size of this structure in bytes

    AddrTileMode    tileMode;       ///< Tile mode
    AddrTileType    tileType;       ///< Tile-type: disp/non-disp/...
    ADDR_TILEINFO*  pTileInfo;      ///< Pointer to tile-info structure, can be NULL for linear/1D
} ADDR_GET_TILEINDEX_INPUT;

/**
***************************************************************************************************
*   ADDR_GET_TILEINDEX_OUTPUT
*
*   @brief
*       Output structure for AddrGetTileIndex
***************************************************************************************************
*/
typedef struct _ADDR_GET_TILEINDEX_OUTPUT
{
    UINT_32         size;           ///< Size of this structure in bytes

    INT_32          index;          ///< index in table
} ADDR_GET_TILEINDEX_OUTPUT;

/**
***************************************************************************************************
*   AddrGetTileIndex
*
*   @brief
*       Get the tiling mode index in table
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrGetTileIndex(
    ADDR_HANDLE                     hLib,
    const ADDR_GET_TILEINDEX_INPUT* pIn,
    ADDR_GET_TILEINDEX_OUTPUT*      pOut);




/**
***************************************************************************************************
*   ADDR_PRT_INFO_INPUT
*
*   @brief
*       Input structure for AddrComputePrtInfo
***************************************************************************************************
*/
typedef struct _ADDR_PRT_INFO_INPUT
{
    AddrFormat          format;        ///< Surface format
    UINT_32             baseMipWidth;  ///< Base mipmap width
    UINT_32             baseMipHeight; ///< Base mipmap height
    UINT_32             baseMipDepth;  ///< Base mipmap depth
    UINT_32             numFrags;      ///< Number of fragments,
} ADDR_PRT_INFO_INPUT;

/**
***************************************************************************************************
*   ADDR_PRT_INFO_OUTPUT
*
*   @brief
*       Input structure for AddrComputePrtInfo
***************************************************************************************************
*/
typedef struct _ADDR_PRT_INFO_OUTPUT
{
    UINT_32             prtTileWidth;
    UINT_32             prtTileHeight;
} ADDR_PRT_INFO_OUTPUT;

/**
***************************************************************************************************
*   AddrComputePrtInfo
*
*   @brief
*       Compute prt surface related information
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputePrtInfo(
    ADDR_HANDLE                 hLib,
    const ADDR_PRT_INFO_INPUT*  pIn,
    ADDR_PRT_INFO_OUTPUT*       pOut);

///////////////////////////////////////////////////////////////////////////////////////////////////
//                                     DCC key functions
///////////////////////////////////////////////////////////////////////////////////////////////////

/**
***************************************************************************************************
*   _ADDR_COMPUTE_DCCINFO_INPUT
*
*   @brief
*       Input structure of AddrComputeDccInfo
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_DCCINFO_INPUT
{
    UINT_32             size;            ///< Size of this structure in bytes
    UINT_32             bpp;             ///< BitPP of color surface
    UINT_32             numSamples;      ///< Sample number of color surface
    UINT_64             colorSurfSize;   ///< Size of color surface to which dcc key is bound
    AddrTileMode        tileMode;        ///< Tile mode of color surface
    ADDR_TILEINFO       tileInfo;        ///< Tile info of color surface
    UINT_32             tileSwizzle;     ///< Tile swizzle
    INT_32              tileIndex;       ///< Tile index of color surface,
                                         ///< MUST be -1 if you don't want to use it
                                         ///< while the global useTileIndex is set to 1
    INT_32              macroModeIndex;  ///< Index in macro tile mode table if there is one (CI)
                                         ///< README: When tileIndex is not -1, this must be valid
} ADDR_COMPUTE_DCCINFO_INPUT;

/**
***************************************************************************************************
*   ADDR_COMPUTE_DCCINFO_OUTPUT
*
*   @brief
*       Output structure of AddrComputeDccInfo
***************************************************************************************************
*/
typedef struct _ADDR_COMPUTE_DCCINFO_OUTPUT
{
    UINT_32 size;                 ///< Size of this structure in bytes
    UINT_64 dccRamBaseAlign;      ///< Base alignment of dcc key
    UINT_64 dccRamSize;           ///< Size of dcc key
    UINT_64 dccFastClearSize;     ///< Size of dcc key portion that can be fast cleared
    BOOL_32 subLvlCompressible;   ///< whether sub resource is compressiable
} ADDR_COMPUTE_DCCINFO_OUTPUT;

/**
***************************************************************************************************
*   AddrComputeDccInfo
*
*   @brief
*       Compute DCC key size, base alignment
*       info
***************************************************************************************************
*/
ADDR_E_RETURNCODE ADDR_API AddrComputeDccInfo(
    ADDR_HANDLE                             hLib,
    const ADDR_COMPUTE_DCCINFO_INPUT*       pIn,
    ADDR_COMPUTE_DCCINFO_OUTPUT*            pOut);

#if defined(__cplusplus)
}
#endif

#endif // __ADDR_INTERFACE_H__


