/*
************************************************************************************************************************
*
*  Copyright (C) 2007-2024 Advanced Micro Devices, Inc. All rights reserved.
*  SPDX-License-Identifier: MIT
*
***********************************************************************************************************************/

/**
****************************************************************************************************
* @file  addrcommon.h
* @brief Contains the helper function and constants.
****************************************************************************************************
*/

#ifndef __ADDR_COMMON_H__
#define __ADDR_COMMON_H__

#include "addrinterface.h"
#include <stdint.h>


#if !defined(__APPLE__) || defined(HAVE_TSERVER)
    #include <stdlib.h>
    #include <string.h>
#endif

#if defined(__GNUC__)
    #include <signal.h>
#endif

#if defined(_WIN32)
#include <intrin.h>
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
// Platform specific debug break defines
////////////////////////////////////////////////////////////////////////////////////////////////////
#if DEBUG
    #if defined(__GNUC__)
        #define ADDR_DBG_BREAK()    { raise(SIGTRAP); }
    #elif defined(__APPLE__)
        #define ADDR_DBG_BREAK()    { IOPanic("");}
    #else
        #define ADDR_DBG_BREAK()    { __debugbreak(); }
    #endif
#else
    #define ADDR_DBG_BREAK()
#endif
////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug print macro
////////////////////////////////////////////////////////////////////////////////////////////////////
#if DEBUG

// Forward decl.
namespace Addr {

/// @brief Debug print helper
/// This function sends messages to thread-local callbacks for printing. If no callback is present
/// it is sent to stderr.
///
VOID DebugPrint( const CHAR* pDebugString, ...);

/// This function sets thread-local callbacks (or NULL) for printing. It should be called when
/// entering addrlib and is implicitly called by GetLib().
VOID ApplyDebugPrinters(ADDR_DEBUGPRINT pfnDebugPrint, ADDR_CLIENT_HANDLE pClientHandle);
}

/// @brief Printf-like macro for printing messages
#define ADDR_PRNT(msg, ...) Addr::DebugPrint(msg, ##__VA_ARGS__)

/// @brief Resets thread-local debug state
/// @ingroup util
///
/// This macro resets any thread-local state on where to print a message.
/// It should be called before returning from addrlib.
#define ADDR_RESET_DEBUG_PRINTERS() Addr::ApplyDebugPrinters(NULL, NULL)

/// @brief Macro for reporting informational messages
/// @ingroup util
///
/// This macro optionally prints an informational message to stdout.
/// The first parameter is a condition -- if it is true, nothing is done.
/// The second parameter is a message that may have printf-like args.
/// Any remaining parameters are used to format the message.
///
#define ADDR_INFO(cond, msg, ...)         \
do { if (!(cond)) { Addr::DebugPrint(msg, ##__VA_ARGS__); } } while (0)


/// @brief Macro for reporting error warning messages
/// @ingroup util
///
/// This macro optionally prints an error warning message to stdout,
/// followed by the file name and line number where the macro was called.
/// The first parameter is a condition -- if it is true, nothing is done.
/// The second parameter is a message that may have printf-like args.
/// Any remaining parameters are used to format the message.
///
#define ADDR_WARN(cond, msg, ...)         \
do { if (!(cond))                         \
  { Addr::DebugPrint(msg, ##__VA_ARGS__); \
    Addr::DebugPrint("  WARNING in file %s, line %d\n", __FILE__, __LINE__); \
} } while (0)


/// @brief Macro for reporting fatal error conditions
/// @ingroup util
///
/// This macro optionally stops execution of the current routine
/// after printing an error warning message to stdout,
/// followed by the file name and line number where the macro was called.
/// The first parameter is a condition -- if it is true, nothing is done.
/// The second parameter is a message that may have printf-like args.
/// Any remaining parameters are used to format the message.
///
#define ADDR_EXIT(cond, msg, ...)                           \
do { if (!(cond))                                           \
  { Addr::DebugPrint(msg, ##__VA_ARGS__); ADDR_DBG_BREAK(); \
} } while (0)

#else // DEBUG

#define ADDR_RESET_DEBUG_PRINTERS()

#define ADDR_PRNT(msg, ...)

#define ADDR_DBG_BREAK()

#define ADDR_INFO(cond, msg, ...)

#define ADDR_WARN(cond, msg, ...)

#define ADDR_EXIT(cond, msg, ...)

#endif // DEBUG
////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug assertions used in AddrLib
////////////////////////////////////////////////////////////////////////////////////////////////////
#if defined(_WIN32) && (_MSC_VER >= 1400)
    #define ADDR_ANALYSIS_ASSUME(expr) __analysis_assume(expr)
#else
    #define ADDR_ANALYSIS_ASSUME(expr) do { (void)(expr); } while (0)
#endif

#if DEBUG
    #define ADDR_BREAK_WITH_MSG(msg)                                      \
        do {                                                              \
            Addr::DebugPrint(msg " in file %s:%d\n", __FILE__, __LINE__); \
            ADDR_DBG_BREAK();                                             \
        } while (0)

    #define ADDR_ASSERT(__e)                                    \
    do {                                                        \
        ADDR_ANALYSIS_ASSUME(__e);                              \
        if ( !((__e) ? TRUE : FALSE)) {                         \
            ADDR_BREAK_WITH_MSG("Assertion '" #__e "' failed"); \
        }                                                       \
    } while (0)

    #if ADDR_SILENCE_ASSERT_ALWAYS
        #define ADDR_ASSERT_ALWAYS()
    #else
        #define ADDR_ASSERT_ALWAYS() ADDR_BREAK_WITH_MSG("Unconditional assert failed")
    #endif

    #define ADDR_UNHANDLED_CASE() ADDR_BREAK_WITH_MSG("Unhandled case")
    #define ADDR_NOT_IMPLEMENTED() ADDR_BREAK_WITH_MSG("Not implemented");
#else //DEBUG
    #if defined( _WIN32 )
        #define ADDR_ASSERT(__e) ADDR_ANALYSIS_ASSUME(__e)
    #else
        #define ADDR_ASSERT(__e)
    #endif
    #define ADDR_ASSERT_ALWAYS()
    #define ADDR_UNHANDLED_CASE()
    #define ADDR_NOT_IMPLEMENTED()
#endif //DEBUG
////////////////////////////////////////////////////////////////////////////////////////////////////


#if 1
#define ADDR_C_ASSERT(__e) static_assert(__e, "")
#else
#define ADDR_C_ASSERT(__e) typedef char __ADDR_C_ASSERT__[(__e) ? 1 : -1]
#endif

namespace Addr
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// Common constants
////////////////////////////////////////////////////////////////////////////////////////////////////
static const UINT_32 MaxElementBytesLog2 = 5; ///< Max number of bpp (8bpp/16bpp/32bpp/64bpp/128bpp)


namespace V1
{
////////////////////////////////////////////////////////////////////////////////////////////////////
// Common constants
////////////////////////////////////////////////////////////////////////////////////////////////////
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

} // V1

namespace V2
{
////////////////////////////////////////////////////////////////////////////////////////////////////
// Common constants
////////////////////////////////////////////////////////////////////////////////////////////////////
static const UINT_32 MaxSurfaceHeight = 16384;
} // V2

namespace V3
{
////////////////////////////////////////////////////////////////////////////////////////////////////
// Common constants
////////////////////////////////////////////////////////////////////////////////////////////////////
static const UINT_32 MaxSurfaceHeight = 65536;
} // V3

////////////////////////////////////////////////////////////////////////////////////////////////////
// Common macros
////////////////////////////////////////////////////////////////////////////////////////////////////
#define BITS_PER_BYTE 8
#define BITS_TO_BYTES(x) ( ((x) + (BITS_PER_BYTE-1)) / BITS_PER_BYTE )
#define BYTES_TO_BITS(x) ( (x) * BITS_PER_BYTE )

/// Helper macros to select a single bit from an int (undefined later in section)
#define _BIT(v,b)      (((v) >> (b) ) & 1)

/**
****************************************************************************************************
* ChipFamily
*
*   @brief
*       Neutral enums that specifies chip family.
*
****************************************************************************************************
*/
enum ChipFamily
{
    ADDR_CHIP_FAMILY_IVLD,    ///< Invalid family
    ADDR_CHIP_FAMILY_R6XX,
    ADDR_CHIP_FAMILY_R7XX,
    ADDR_CHIP_FAMILY_R8XX,
    ADDR_CHIP_FAMILY_NI,
    ADDR_CHIP_FAMILY_SI,
    ADDR_CHIP_FAMILY_CI,
    ADDR_CHIP_FAMILY_VI,
    ADDR_CHIP_FAMILY_AI,
    ADDR_CHIP_FAMILY_NAVI,
    ADDR_CHIP_FAMILY_UNKNOWN,
};

/**
****************************************************************************************************
* ConfigFlags
*
*   @brief
*       This structure is used to set configuration flags.
****************************************************************************************************
*/
union ConfigFlags
{
    struct
    {
        /// These flags are set up internally thru AddrLib::Create() based on ADDR_CREATE_FLAGS
        UINT_32 optimalBankSwap        : 1;    ///< New bank tiling for RV770 only
        UINT_32 noCubeMipSlicesPad     : 1;    ///< Disables faces padding for cubemap mipmaps
        UINT_32 fillSizeFields         : 1;    ///< If clients fill size fields in all input and
                                               ///  output structure
        UINT_32 ignoreTileInfo         : 1;    ///< Don't use tile info structure
        UINT_32 useTileIndex           : 1;    ///< Make tileIndex field in input valid
        UINT_32 useCombinedSwizzle     : 1;    ///< Use combined swizzle
        UINT_32 checkLast2DLevel       : 1;    ///< Check the last 2D mip sub level
        UINT_32 useHtileSliceAlign     : 1;    ///< Do htile single slice alignment
        UINT_32 allowLargeThickTile    : 1;    ///< Allow 64*thickness*bytesPerPixel > rowSize
        UINT_32 disableLinearOpt       : 1;    ///< Disallow tile modes to be optimized to linear
        UINT_32 use32bppFor422Fmt      : 1;    ///< View 422 formats as 32 bits per pixel element
        UINT_32 forceDccAndTcCompat    : 1;    ///< Force enable DCC and TC compatibility
        UINT_32 nonPower2MemConfig     : 1;    ///< Video memory bit width is not power of 2
        UINT_32 enableAltTiling        : 1;    ///< Enable alt tile mode
        UINT_32 reserved               : 18;   ///< Reserved bits for future use
    };

    UINT_32 value;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// Misc helper functions
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
****************************************************************************************************
*   AddrXorReduce
*
*   @brief
*       Xor the right-side numberOfBits bits of x.
****************************************************************************************************
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
****************************************************************************************************
*   Unset least bit
*
*   @brief
*       Returns a copy of the value with the least-significant '1' bit unset
****************************************************************************************************
*/
static inline UINT_32 UnsetLeastBit(
    UINT_32 val)
{
    return val & (val - 1);
}

/**
****************************************************************************************************
*   BitScanForward
*
*   @brief
*       Returns the index-position of the least-significant '1' bit. Must not be 0.
****************************************************************************************************
*/
static inline UINT_32 BitScanForward(
    UINT_32 mask) ///< [in] Bitmask to scan
{
    ADDR_ASSERT(mask > 0);
    unsigned long out = 0;
#if (defined(_WIN64) && defined(_M_X64)) || (defined(_WIN32) && defined(_M_IX64))
    out = ::_tzcnt_u32(mask);
#elif (defined(_WIN32) || defined(_WIN64))
    ::_BitScanForward(&out, mask);
#elif defined(__GNUC__)
    out = __builtin_ctz(mask);
#else
    while ((mask & 1) == 0)
    {
        mask >>= 1;
        out++;
    }
#endif
    return out;
}

/**
****************************************************************************************************
*   BitScanReverse
*
*   @brief
*       Returns the reverse-position of the most-significant '1' bit. Must not be 0.
****************************************************************************************************
*/
static inline UINT_32 BitScanReverse(
    UINT_32 mask) ///< [in] Bitmask to scan
{
    ADDR_ASSERT(mask > 0);
    unsigned long out = 0;
#if (defined(_WIN32) || defined(_WIN64))
    ::_BitScanReverse(&out, mask);
    out ^= 31;
#elif defined(__GNUC__)
    out = __builtin_clz(mask);
#else
    out = 32;
    while (mask != 0)
    {
        mask >>= 1;
        out++;
    }
    out = sizeof(mask) * 8 - out;
#endif
    return out;
}

/**
****************************************************************************************************
*   IsPow2
*
*   @brief
*       Check if the size (UINT_32) is pow 2
****************************************************************************************************
*/
static inline UINT_32 IsPow2(
    UINT_32 dim)        ///< [in] dimension of miplevel
{
    ADDR_ASSERT(dim > 0);
    return !(dim & (dim - 1));
}

/**
****************************************************************************************************
*   IsPow2
*
*   @brief
*       Check if the size (UINT_64) is pow 2
****************************************************************************************************
*/
static inline UINT_64 IsPow2(
    UINT_64 dim)        ///< [in] dimension of miplevel
{
    ADDR_ASSERT(dim > 0);
    return !(dim & (dim - 1));
}

/**
****************************************************************************************************
*   PowTwoAlign
*
*   @brief
*       Align UINT_32 "x" up to "align" alignment, "align" should be power of 2
****************************************************************************************************
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
****************************************************************************************************
*   PowTwoAlign
*
*   @brief
*       Align UINT_64 "x" up to "align" alignment, "align" should be power of 2
****************************************************************************************************
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
****************************************************************************************************
*   PowTwoAlignDown
*
*   @brief
*       Align UINT_32 "x" down to "align" alignment, "align" should be power of 2
****************************************************************************************************
*/
static inline UINT_32 PowTwoAlignDown(
    UINT_32 x,
    UINT_32 align)
{
    //
    // Assert that x is a power of two.
    //
    ADDR_ASSERT(IsPow2(align));
    return (x & ~(align - 1));
}

/**
****************************************************************************************************
*   PowTwoAlignDown
*
*   @brief
*       Align UINT_64 "x" down to "align" alignment, "align" should be power of 2
****************************************************************************************************
*/
static inline UINT_64 PowTwoAlignDown(
    UINT_64 x,
    UINT_64 align)
{
    //
    // Assert that x is a power of two.
    //
    ADDR_ASSERT(IsPow2(align));
    return (x & ~(align - 1));
}

/**
****************************************************************************************************
*   Min
*
*   @brief
*       Get the min value between two unsigned values
****************************************************************************************************
*/
static inline UINT_32 Min(
    UINT_32 value1,
    UINT_32 value2)
{
    return ((value1 < (value2)) ? (value1) : value2);
}

/**
****************************************************************************************************
*   Min
*
*   @brief
*       Get the min value between two signed values
****************************************************************************************************
*/
static inline INT_32 Min(
    INT_32 value1,
    INT_32 value2)
{
    return ((value1 < (value2)) ? (value1) : value2);
}

/**
****************************************************************************************************
*   Max
*
*   @brief
*       Get the max value between two unsigned values
****************************************************************************************************
*/
static inline UINT_32 Max(
    UINT_32 value1,
    UINT_32 value2)
{
    return ((value1 > (value2)) ? (value1) : value2);
}

/**
****************************************************************************************************
*   Max
*
*   @brief
*       Get the max value between two signed values
****************************************************************************************************
*/
static inline INT_32 Max(
    INT_32 value1,
    INT_32 value2)
{
    return ((value1 > (value2)) ? (value1) : value2);
}

/**
****************************************************************************************************
*   RoundUpQuotient
*
*   @brief
*       Divides two numbers, rounding up any remainder.
****************************************************************************************************
*/
static inline UINT_32 RoundUpQuotient(
    UINT_32 numerator,
    UINT_32 denominator)
{
    ADDR_ASSERT(denominator > 0);
    return ((numerator + (denominator - 1)) / denominator);
}

/**
****************************************************************************************************
*   RoundUpQuotient
*
*   @brief
*       Divides two numbers, rounding up any remainder.
****************************************************************************************************
*/
static inline UINT_64 RoundUpQuotient(
    UINT_64 numerator,
    UINT_64 denominator)
{
    ADDR_ASSERT(denominator > 0);
    return ((numerator + (denominator - 1)) / denominator);
}

/**
****************************************************************************************************
*   NextPow2
*
*   @brief
*       Compute the mipmap's next level dim size
****************************************************************************************************
*/
static inline UINT_32 NextPow2(
    UINT_32 dim)        ///< [in] dimension of miplevel
{
    UINT_32 newDim = 1;

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
****************************************************************************************************
*   Log2
*
*   @brief
*       Compute log of base 2 no matter the target is power of 2 or not. Returns 0 if 0.
****************************************************************************************************
*/
static inline UINT_32 Log2(
    UINT_32 x)      ///< [in] the value should calculate log based 2
{
    return (x != 0) ? (31 ^ BitScanReverse(x)) : 0;
}

/**
****************************************************************************************************
*   QLog2
*
*   @brief
*       Compute log of base 2 quickly (<= 16)
****************************************************************************************************
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
****************************************************************************************************
*   SafeAssign
*
*   @brief
*       NULL pointer safe assignment
****************************************************************************************************
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
****************************************************************************************************
*   SafeAssign
*
*   @brief
*       NULL pointer safe assignment for 64bit values
****************************************************************************************************
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
****************************************************************************************************
*   RoundHalf
*
*   @brief
*       return (x + 1) / 2
****************************************************************************************************
*/
static inline UINT_32 RoundHalf(
    UINT_32     x)     ///< [in] input value
{
    ADDR_ASSERT(x != 0);

#if 1
    return (x >> 1) + (x & 1);
#else
    return (x + 1) >> 1;
#endif
}

/**
****************************************************************************************************
*   SumGeo
*
*   @brief
*       Calculate sum of a geometric progression whose ratio is 1/2
****************************************************************************************************
*/
static inline UINT_32 SumGeo(
    UINT_32     base,   ///< [in] First term in the geometric progression
    UINT_32     num)    ///< [in] Number of terms to be added into sum
{
    ADDR_ASSERT(base > 0);

    UINT_32 sum = 0;
    UINT_32 i = 0;
    for (; (i < num) && (base > 1); i++)
    {
        sum += base;
        base = RoundHalf(base);
    }
    sum += num - i;

    return sum;
}

/**
****************************************************************************************************
*   GetBit
*
*   @brief
*       Extract bit N value (0 or 1) of a UINT32 value.
****************************************************************************************************
*/
static inline UINT_32 GetBit(
    UINT_32     u32,   ///< [in] UINT32 value
    UINT_32     pos)   ///< [in] bit position from LSB, valid range is [0..31]
{
    ADDR_ASSERT(pos <= 31);

    return (u32 >> pos) & 0x1;
}

/**
****************************************************************************************************
*   GetBits
*
*   @brief
*       Copy 'bitsNum' bits from src start from srcStartPos into destination from dstStartPos
*       srcStartPos: 0~31 for UINT_32
*       bitsNum    : 1~32 for UINT_32
*       srcStartPos: 0~31 for UINT_32
*                                                                 src start position
*                                                                          |
*       src : b[31] b[30] b[29] ... ... ... ... ... ... ... ... b[end]..b[beg] ... b[1] b[0]
*                                   || Bits num || copy length  || Bits num ||
*       dst : b[31] b[30] b[29] ... b[end]..b[beg] ... ... ... ... ... ... ... ... b[1] b[0]
*                                              |
*                                     dst start position
****************************************************************************************************
*/
static inline UINT_32 GetBits(
    UINT_32 src,
    UINT_32 srcStartPos,
    UINT_32 bitsNum,
    UINT_32 dstStartPos)
{
    ADDR_ASSERT((srcStartPos < 32) && (dstStartPos < 32) && (bitsNum > 0));
    ADDR_ASSERT((bitsNum + dstStartPos <= 32) && (bitsNum + srcStartPos <= 32));

    return ((src >> srcStartPos) << (32 - bitsNum)) >> (32 - bitsNum - dstStartPos);
}

/**
****************************************************************************************************
*   MortonGen2d
*
*   @brief
*       Generate 2D Morton interleave code with num lowest bits in each channel
****************************************************************************************************
*/
static inline UINT_32 MortonGen2d(
    UINT_32     x,     ///< [in] First channel
    UINT_32     y,     ///< [in] Second channel
    UINT_32     num)   ///< [in] Number of bits extracted from each channel
{
    UINT_32 mort = 0;

    for (UINT_32 i = 0; i < num; i++)
    {
        mort |= (GetBit(y, i) << (2 * i));
        mort |= (GetBit(x, i) << (2 * i + 1));
    }

    return mort;
}

/**
****************************************************************************************************
*   MortonGen3d
*
*   @brief
*       Generate 3D Morton interleave code with num lowest bits in each channel
****************************************************************************************************
*/
static inline UINT_32 MortonGen3d(
    UINT_32     x,     ///< [in] First channel
    UINT_32     y,     ///< [in] Second channel
    UINT_32     z,     ///< [in] Third channel
    UINT_32     num)   ///< [in] Number of bits extracted from each channel
{
    UINT_32 mort = 0;

    for (UINT_32 i = 0; i < num; i++)
    {
        mort |= (GetBit(z, i) << (3 * i));
        mort |= (GetBit(y, i) << (3 * i + 1));
        mort |= (GetBit(x, i) << (3 * i + 2));
    }

    return mort;
}

/**
****************************************************************************************************
*   ReverseBitVector
*
*   @brief
*       Return reversed lowest num bits of v: v[0]v[1]...v[num-2]v[num-1]
****************************************************************************************************
*/
static inline UINT_32 ReverseBitVector(
    UINT_32     v,     ///< [in] Reverse operation base value
    UINT_32     num)   ///< [in] Number of bits used in reverse operation
{
    UINT_32 reverse = 0;

    for (UINT_32 i = 0; i < num; i++)
    {
        reverse |= (GetBit(v, num - 1 - i) << i);
    }

    return reverse;
}

/**
****************************************************************************************************
*   FoldXor2d
*
*   @brief
*       Xor bit vector v[num-1]v[num-2]...v[1]v[0] with v[num]v[num+1]...v[2*num-2]v[2*num-1]
****************************************************************************************************
*/
static inline UINT_32 FoldXor2d(
    UINT_32     v,     ///< [in] Xor operation base value
    UINT_32     num)   ///< [in] Number of bits used in fold xor operation
{
    return (v & ((1 << num) - 1)) ^ ReverseBitVector(v >> num, num);
}

/**
****************************************************************************************************
*   DeMort
*
*   @brief
*       Return v[0] | v[2] | v[4] | v[6]... | v[2*num - 2]
****************************************************************************************************
*/
static inline UINT_32 DeMort(
    UINT_32     v,     ///< [in] DeMort operation base value
    UINT_32     num)   ///< [in] Number of bits used in fold DeMort operation
{
    UINT_32 d = 0;

    for (UINT_32 i = 0; i < num; i++)
    {
        d |= ((v & (1 << (i << 1))) >> i);
    }

    return d;
}

/**
****************************************************************************************************
*   FoldXor3d
*
*   @brief
*       v[0]...v[num-1] ^ v[3*num-1]v[3*num-3]...v[num+2]v[num] ^ v[3*num-2]...v[num+1]v[num-1]
****************************************************************************************************
*/
static inline UINT_32 FoldXor3d(
    UINT_32     v,     ///< [in] Xor operation base value
    UINT_32     num)   ///< [in] Number of bits used in fold xor operation
{
    UINT_32 t = v & ((1 << num) - 1);
    t ^= ReverseBitVector(DeMort(v >> num, num), num);
    t ^= ReverseBitVector(DeMort(v >> (num + 1), num), num);

    return t;
}

/**
****************************************************************************************************
*   InitChannel
*
*   @brief
*       Set channel initialization value via a return value
****************************************************************************************************
*/
static inline ADDR_CHANNEL_SETTING InitChannel(
    UINT_32     valid,     ///< [in] valid setting
    UINT_32     channel,   ///< [in] channel setting
    UINT_32     index)     ///< [in] index setting
{
    ADDR_CHANNEL_SETTING t;
    t.valid = valid;
    t.channel = channel;
    t.index = index;

    return t;
}

/**
****************************************************************************************************
*   InitChannel
*
*   @brief
*       Set channel initialization value via channel pointer
****************************************************************************************************
*/
static inline VOID InitChannel(
    UINT_32     valid,              ///< [in] valid setting
    UINT_32     channel,            ///< [in] channel setting
    UINT_32     index,              ///< [in] index setting
    ADDR_CHANNEL_SETTING *pChanSet) ///< [out] channel setting to be initialized
{
    pChanSet->valid = valid;
    pChanSet->channel = channel;
    pChanSet->index = index;
}


/**
****************************************************************************************************
*   InitChannel
*
*   @brief
*       Set channel initialization value via another channel
****************************************************************************************************
*/
static inline VOID InitChannel(
    ADDR_CHANNEL_SETTING *pChanDst, ///< [in] channel setting to be copied from
    ADDR_CHANNEL_SETTING *pChanSrc) ///< [out] channel setting to be initialized
{
    pChanDst->valid = pChanSrc->valid;
    pChanDst->channel = pChanSrc->channel;
    pChanDst->index = pChanSrc->index;
}

/**
****************************************************************************************************
*   GetMaxValidChannelIndex
*
*   @brief
*       Get max valid index for a specific channel
****************************************************************************************************
*/
static inline UINT_32 GetMaxValidChannelIndex(
    const ADDR_CHANNEL_SETTING *pChanSet,   ///< [in] channel setting to be initialized
    UINT_32                     searchCount,///< [in] number of channel setting to be searched
    UINT_32                     channel)    ///< [in] channel to be searched
{
    UINT_32 index = 0;

    for (UINT_32 i = 0; i < searchCount; i++)
    {
        if (pChanSet[i].valid && (pChanSet[i].channel == channel))
        {
            index = Max(index, static_cast<UINT_32>(pChanSet[i].index));
        }
    }

    return index;
}

/**
****************************************************************************************************
*   GetCoordActiveMask
*
*   @brief
*       Get bit mask which indicates which positions in the equation match the target coord
****************************************************************************************************
*/
static inline UINT_32 GetCoordActiveMask(
    const ADDR_CHANNEL_SETTING *pChanSet,   ///< [in] channel setting to be initialized
    UINT_32                     searchCount,///< [in] number of channel setting to be searched
    UINT_32                     channel,    ///< [in] channel to be searched
    UINT_32                     index)      ///< [in] index to be searched
{
    UINT_32 mask = 0;

    for (UINT_32 i = 0; i < searchCount; i++)
    {
        if ((pChanSet[i].valid   == TRUE)    &&
            (pChanSet[i].channel == channel) &&
            (pChanSet[i].index   == index))
        {
            mask |= (1 << i);
        }
    }

    return mask;
}

/**
****************************************************************************************************
*   FillEqBitComponents
*
*   @brief
*       Fill the 'numBitComponents' field based on the equation.
****************************************************************************************************
*/
static inline void FillEqBitComponents(
    ADDR_EQUATION *pEquation) // [in/out] Equation to calculate bit components for
{
    pEquation->numBitComponents = 1; // We always have at least the address
    for (UINT_32 xorN = 1; xorN < ADDR_MAX_EQUATION_COMP; xorN++)
    {
        for (UINT_32 bit = 0; bit < ADDR_MAX_EQUATION_BIT; bit++)
        {
            if (pEquation->comps[xorN][bit].valid)
            {
                pEquation->numBitComponents = xorN + 1;
                break;
            }
        }

        if (pEquation->numBitComponents != (xorN + 1))
        {
            // Skip following components if this one wasn't valid
            break;
        }
    }
}

/**
****************************************************************************************************
*   ShiftCeil
*
*   @brief
*       Apply right-shift with ceiling
****************************************************************************************************
*/
static inline UINT_32 ShiftCeil(
    UINT_32 a,  ///< [in] value to be right-shifted
    UINT_32 b)  ///< [in] number of bits to shift
{
    return (a >> b) + (((a & ((1 << b) - 1)) != 0) ? 1 : 0);
}

/**
****************************************************************************************************
*   ShiftRight
*
*   @brief
*       Return right-shift value and minimum is 1
****************************************************************************************************
*/
static inline UINT_32 ShiftRight(
    UINT_32 a,  ///< [in] value to be right-shifted
    UINT_32 b)  ///< [in] number of bits to shift
{
    return Max(a >> b, 1u);
}

/**
****************************************************************************************************
*   VoidPtrDec
*
*   @brief
*       Subtracts a value to the given pointer directly.
****************************************************************************************************
*/
static inline void* VoidPtrDec(
    void*  pIn,
    size_t offset)
{
    return (void*)(((char*)(pIn)) - offset);
}

static inline const void* VoidPtrDec(
    const void* pIn,
    size_t      offset)
{
    return (const void*)(((const char*)(pIn)) - offset);
}

/**
****************************************************************************************************
*   VoidPtrInc
*
*   @brief
*       Adds a value to the given pointer directly.
****************************************************************************************************
*/
static inline void* VoidPtrInc(
    void*  pIn,
    size_t offset)
{
    return (void*)(((char*)(pIn)) + offset);
}

static inline const void* VoidPtrInc(
    const void* pIn,
    size_t      offset)
{
    return (const void*)(((const char*)(pIn)) + offset);
}

/**
****************************************************************************************************
*   VoidPtrXor
*
*   @brief
*       Xors a value to the given pointer directly.
****************************************************************************************************
*/
static inline void* VoidPtrXor(
    void*  pIn,
    size_t offset)
{
    return (void*)(((uintptr_t)(pIn)) ^ offset);
}

static inline const void* VoidPtrXor(
    const void* pIn,
    size_t      offset)
{
    return (const void*)(((uintptr_t)(pIn)) ^ offset);
}

} // Addr

#endif // __ADDR_COMMON_H__

