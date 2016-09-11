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
* @file  addrcommon.h
* @brief Contains the helper function and constants
***************************************************************************************************
*/

#ifndef __ADDR_COMMON_H__
#define __ADDR_COMMON_H__

#include "addrinterface.h"


// ADDR_LNX_KERNEL_BUILD is for internal build
// Moved from addrinterface.h so __KERNEL__ is not needed any more
#if ADDR_LNX_KERNEL_BUILD // || (defined(__GNUC__) && defined(__KERNEL__))
    #include "lnx_common_defs.h" // ported from cmmqs
#elif !defined(__APPLE__)
    #include <stdlib.h>
    #include <string.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
// Common constants
///////////////////////////////////////////////////////////////////////////////////////////////////
static const UINT_32 MicroTileWidth      = 8;       ///< Micro tile width, for 1D and 2D tiling
static const UINT_32 MicroTileHeight     = 8;       ///< Micro tile height, for 1D and 2D tiling
static const UINT_32 ThickTileThickness  = 4;       ///< Micro tile thickness, for THICK modes
static const UINT_32 XThickTileThickness = 8;       ///< Extra thick tiling thickness
static const UINT_32 PowerSaveTileBytes  = 64;      ///< Nuber of bytes per tile for power save 64
static const UINT_32 CmaskCacheBits      = 1024;    ///< Number of bits for CMASK cache
static const UINT_32 CmaskElemBits       = 4;       ///< Number of bits for CMASK element
static const UINT_32 HtileCacheBits      = 16384;   ///< Number of bits for HTILE cache 512*32

static const UINT_32 MicroTilePixels     = MicroTileWidth * MicroTileHeight;

static const INT_32 TileIndexInvalid        = TILEINDEX_INVALID;
static const INT_32 TileIndexLinearGeneral  = TILEINDEX_LINEAR_GENERAL;
static const INT_32 TileIndexNoMacroIndex   = -3;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Common macros
///////////////////////////////////////////////////////////////////////////////////////////////////
#define BITS_PER_BYTE 8
#define BITS_TO_BYTES(x) ( ((x) + (BITS_PER_BYTE-1)) / BITS_PER_BYTE )
#define BYTES_TO_BITS(x) ( (x) * BITS_PER_BYTE )

/// Helper macros to select a single bit from an int (undefined later in section)
#define _BIT(v,b)      (((v) >> (b) ) & 1)

/**
***************************************************************************************************
* @brief Enums to identify AddrLib type
***************************************************************************************************
*/
enum AddrLibClass
{
    BASE_ADDRLIB = 0x0,
    R600_ADDRLIB = 0x6,
    R800_ADDRLIB = 0x8,
    SI_ADDRLIB   = 0xa,
    CI_ADDRLIB   = 0xb,
};

/**
***************************************************************************************************
* AddrChipFamily
*
*   @brief
*       Neutral enums that specifies chip family.
*
***************************************************************************************************
*/
enum AddrChipFamily
{
    ADDR_CHIP_FAMILY_IVLD,    ///< Invalid family
    ADDR_CHIP_FAMILY_R6XX,
    ADDR_CHIP_FAMILY_R7XX,
    ADDR_CHIP_FAMILY_R8XX,
    ADDR_CHIP_FAMILY_NI,
    ADDR_CHIP_FAMILY_SI,
    ADDR_CHIP_FAMILY_CI,
    ADDR_CHIP_FAMILY_VI,
};

/**
***************************************************************************************************
* ADDR_CONFIG_FLAGS
*
*   @brief
*       This structure is used to set addr configuration flags.
***************************************************************************************************
*/
union ADDR_CONFIG_FLAGS
{
    struct
    {
        /// Clients do not need to set these flags except forceLinearAligned.
        /// There flags are set up by AddrLib inside thru AddrInitGlobalParamsFromRegister
        UINT_32 optimalBankSwap        : 1;    ///< New bank tiling for RV770 only
        UINT_32 noCubeMipSlicesPad     : 1;    ///< Disables faces padding for cubemap mipmaps
        UINT_32 fillSizeFields         : 1;    ///< If clients fill size fields in all input and
                                               ///  output structure
        UINT_32 ignoreTileInfo         : 1;    ///< Don't use tile info structure
        UINT_32 useTileIndex           : 1;    ///< Make tileIndex field in input valid
        UINT_32 useCombinedSwizzle     : 1;    ///< Use combined swizzle
        UINT_32 checkLast2DLevel       : 1;    ///< Check the last 2D mip sub level
        UINT_32 useHtileSliceAlign     : 1;    ///< Do htile single slice alignment
        UINT_32 degradeBaseLevel       : 1;    ///< Degrade to 1D modes automatically for base level
        UINT_32 allowLargeThickTile    : 1;    ///< Allow 64*thickness*bytesPerPixel > rowSize
        UINT_32 reserved               : 22;   ///< Reserved bits for future use
    };

    UINT_32 value;
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// Platform specific debug break defines
///////////////////////////////////////////////////////////////////////////////////////////////////
#if DEBUG
    #if defined(__GNUC__)
        #define ADDR_DBG_BREAK()
    #elif defined(__APPLE__)
        #define ADDR_DBG_BREAK()    { IOPanic("");}
    #else
        #define ADDR_DBG_BREAK()    { __debugbreak(); }
    #endif
#else
    #define ADDR_DBG_BREAK()
#endif
///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////
// Debug assertions used in AddrLib
///////////////////////////////////////////////////////////////////////////////////////////////////
#if DEBUG
#define ADDR_ASSERT(__e) if ( !((__e) ? TRUE : FALSE)) { ADDR_DBG_BREAK(); }
#define ADDR_ASSERT_ALWAYS() ADDR_DBG_BREAK()
#define ADDR_UNHANDLED_CASE() ADDR_ASSERT(!"Unhandled case")
#define ADDR_NOT_IMPLEMENTED() ADDR_ASSERT(!"Not implemented");
#else //DEBUG
#define ADDR_ASSERT(__e)
#define ADDR_ASSERT_ALWAYS()
#define ADDR_UNHANDLED_CASE()
#define ADDR_NOT_IMPLEMENTED()
#endif //DEBUG
///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////
// Debug print macro from legacy address library
///////////////////////////////////////////////////////////////////////////////////////////////////
#if DEBUG

#define ADDR_PRNT(a)    AddrObject::DebugPrint a

/// @brief Macro for reporting informational messages
/// @ingroup util
///
/// This macro optionally prints an informational message to stdout.
/// The first parameter is a condition -- if it is true, nothing is done.
/// The second pararmeter MUST be a parenthesis-enclosed list of arguments,
/// starting with a string. This is passed to printf() or an equivalent
/// in order to format the informational message. For example,
/// ADDR_INFO(0, ("test %d",3) ); prints out "test 3".
///
#define ADDR_INFO(cond, a)         \
{ if (!(cond)) { ADDR_PRNT(a); } }


/// @brief Macro for reporting error warning messages
/// @ingroup util
///
/// This macro optionally prints an error warning message to stdout,
/// followed by the file name and line number where the macro was called.
/// The first parameter is a condition -- if it is true, nothing is done.
/// The second pararmeter MUST be a parenthesis-enclosed list of arguments,
/// starting with a string. This is passed to printf() or an equivalent
/// in order to format the informational message. For example,
/// ADDR_WARN(0, ("test %d",3) ); prints out "test 3" followed by
/// a second line with the file name and line number.
///
#define ADDR_WARN(cond, a)         \
{ if (!(cond))                     \
  { ADDR_PRNT(a);                  \
    ADDR_PRNT(("  WARNING in file %s, line %d\n", __FILE__, __LINE__)); \
} }


/// @brief Macro for reporting fatal error conditions
/// @ingroup util
///
/// This macro optionally stops execution of the current routine
/// after printing an error warning message to stdout,
/// followed by the file name and line number where the macro was called.
/// The first parameter is a condition -- if it is true, nothing is done.
/// The second pararmeter MUST be a parenthesis-enclosed list of arguments,
/// starting with a string. This is passed to printf() or an equivalent
/// in order to format the informational message. For example,
/// ADDR_EXIT(0, ("test %d",3) ); prints out "test 3" followed by
/// a second line with the file name and line number, then stops execution.
///
#define ADDR_EXIT(cond, a)         \
{ if (!(cond))                     \
  { ADDR_PRNT(a); ADDR_DBG_BREAK();\
} }

#else // DEBUG

#define ADDRDPF 1 ? (void)0 : (void)

#define ADDR_PRNT(a)

#define ADDR_DBG_BREAK()

#define ADDR_INFO(cond, a)

#define ADDR_WARN(cond, a)

#define ADDR_EXIT(cond, a)

#endif // DEBUG
///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////
// Misc helper functions
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
***************************************************************************************************
*   AddrXorReduce
*
*   @brief
*       Xor the right-side numberOfBits bits of x.
***************************************************************************************************
*/
static inline UINT_32 XorReduce(
    UINT_32 x,
    UINT_32 numberOfBits)
{
    UINT_32 i;
    UINT_32 result = x & 1;

    for (i=1; i<numberOfBits; i++)
    {
        result ^= ((x>>i) & 1);
    }

    return result;
}

/**
***************************************************************************************************
*   IsPow2
*
*   @brief
*       Check if the size (UINT_32) is pow 2
***************************************************************************************************
*/
static inline UINT_32 IsPow2(
    UINT_32 dim)        ///< [in] dimension of miplevel
{
    ADDR_ASSERT(dim > 0);
    return !(dim & (dim - 1));
}

/**
***************************************************************************************************
*   IsPow2
*
*   @brief
*       Check if the size (UINT_64) is pow 2
***************************************************************************************************
*/
static inline UINT_64 IsPow2(
    UINT_64 dim)        ///< [in] dimension of miplevel
{
    ADDR_ASSERT(dim > 0);
    return !(dim & (dim - 1));
}

/**
***************************************************************************************************
*   ByteAlign
*
*   @brief
*       Align UINT_32 "x" to "align" alignment, "align" should be power of 2
***************************************************************************************************
*/
static inline UINT_32 PowTwoAlign(
    UINT_32 x,
    UINT_32 align)
{
    //
    // Assert that x is a power of two.
    //
    ADDR_ASSERT(IsPow2(align));
    return (x + (align - 1)) & (~(align - 1));
}

/**
***************************************************************************************************
*   ByteAlign
*
*   @brief
*       Align UINT_64 "x" to "align" alignment, "align" should be power of 2
***************************************************************************************************
*/
static inline UINT_64 PowTwoAlign(
    UINT_64 x,
    UINT_64 align)
{
    //
    // Assert that x is a power of two.
    //
    ADDR_ASSERT(IsPow2(align));
    return (x + (align - 1)) & (~(align - 1));
}

/**
***************************************************************************************************
*   Min
*
*   @brief
*       Get the min value between two unsigned values
***************************************************************************************************
*/
static inline UINT_32 Min(
    UINT_32 value1,
    UINT_32 value2)
{
    return ((value1 < (value2)) ? (value1) : value2);
}

/**
***************************************************************************************************
*   Min
*
*   @brief
*       Get the min value between two signed values
***************************************************************************************************
*/
static inline INT_32 Min(
    INT_32 value1,
    INT_32 value2)
{
    return ((value1 < (value2)) ? (value1) : value2);
}

/**
***************************************************************************************************
*   Max
*
*   @brief
*       Get the max value between two unsigned values
***************************************************************************************************
*/
static inline UINT_32 Max(
    UINT_32 value1,
    UINT_32 value2)
{
    return ((value1 > (value2)) ? (value1) : value2);
}

/**
***************************************************************************************************
*   Max
*
*   @brief
*       Get the max value between two signed values
***************************************************************************************************
*/
static inline INT_32 Max(
    INT_32 value1,
    INT_32 value2)
{
    return ((value1 > (value2)) ? (value1) : value2);
}

/**
***************************************************************************************************
*   NextPow2
*
*   @brief
*       Compute the mipmap's next level dim size
***************************************************************************************************
*/
static inline UINT_32 NextPow2(
    UINT_32 dim)        ///< [in] dimension of miplevel
{
    UINT_32 newDim;

    newDim = 1;

    if (dim > 0x7fffffff)
    {
        ADDR_ASSERT_ALWAYS();
        newDim = 0x80000000;
    }
    else
    {
        while (newDim < dim)
        {
            newDim <<= 1;
        }
    }

    return newDim;
}

/**
***************************************************************************************************
*   Log2
*
*   @brief
*       Compute log of base 2
***************************************************************************************************
*/
static inline UINT_32 Log2(
    UINT_32 x)      ///< [in] the value should calculate log based 2
{
    UINT_32 y;

    //
    // Assert that x is a power of two.
    //
    ADDR_ASSERT(IsPow2(x));

    y = 0;
    while (x > 1)
    {
        x >>= 1;
        y++;
    }

    return y;
}

/**
***************************************************************************************************
*   QLog2
*
*   @brief
*       Compute log of base 2 quickly (<= 16)
***************************************************************************************************
*/
static inline UINT_32 QLog2(
    UINT_32 x)      ///< [in] the value should calculate log based 2
{
    ADDR_ASSERT(x <= 16);

    UINT_32 y = 0;

    switch (x)
    {
        case 1:
            y = 0;
            break;
        case 2:
            y = 1;
            break;
        case 4:
            y = 2;
            break;
        case 8:
            y = 3;
            break;
        case 16:
            y = 4;
            break;
        default:
            ADDR_ASSERT_ALWAYS();
    }

    return y;
}

/**
***************************************************************************************************
*   SafeAssign
*
*   @brief
*       NULL pointer safe assignment
***************************************************************************************************
*/
static inline VOID SafeAssign(
    UINT_32*    pLVal,  ///< [in] Pointer to left val
    UINT_32     rVal)   ///< [in] Right value
{
    if (pLVal)
    {
        *pLVal = rVal;
    }
}

/**
***************************************************************************************************
*   SafeAssign
*
*   @brief
*       NULL pointer safe assignment for 64bit values
***************************************************************************************************
*/
static inline VOID SafeAssign(
    UINT_64*    pLVal,  ///< [in] Pointer to left val
    UINT_64     rVal)   ///< [in] Right value
{
    if (pLVal)
    {
        *pLVal = rVal;
    }
}

/**
***************************************************************************************************
*   SafeAssign
*
*   @brief
*       NULL pointer safe assignment for AddrTileMode
***************************************************************************************************
*/
static inline VOID SafeAssign(
    AddrTileMode*    pLVal, ///< [in] Pointer to left val
    AddrTileMode     rVal)  ///< [in] Right value
{
    if (pLVal)
    {
        *pLVal = rVal;
    }
}

#endif // __ADDR_COMMON_H__

