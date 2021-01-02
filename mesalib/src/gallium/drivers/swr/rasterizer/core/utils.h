/****************************************************************************
 * Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * @file utils.h
 *
 * @brief Utilities used by SWR core.
 *
 ******************************************************************************/
#pragma once

#include <string.h>
#include <type_traits>
#include <algorithm>
#include <array>
#include "common/os.h"
#include "common/intrin.h"
#include "common/swr_assert.h"
#include "core/api.h"

struct simdBBox
{
    simdscalari ymin;
    simdscalari ymax;
    simdscalari xmin;
    simdscalari xmax;
};

struct simd16BBox
{
    simd16scalari ymin;
    simd16scalari ymax;
    simd16scalari xmin;
    simd16scalari xmax;
};

template <typename SIMD_T>
struct SIMDBBOX_T
{
    typename SIMD_T::Integer ymin;
    typename SIMD_T::Integer ymax;
    typename SIMD_T::Integer xmin;
    typename SIMD_T::Integer xmax;
};

// helper function to unroll loops
template <int Begin, int End, int Step = 1>
struct UnrollerL
{
    template <typename Lambda>
    INLINE static void step(Lambda& func)
    {
        func(Begin);
        UnrollerL<Begin + Step, End, Step>::step(func);
    }
};

template <int End, int Step>
struct UnrollerL<End, End, Step>
{
    template <typename Lambda>
    static void step(Lambda& func)
    {
    }
};

// helper function to unroll loops, with mask to skip specific iterations
template <int Begin, int End, int Step = 1, int Mask = 0x7f>
struct UnrollerLMask
{
    template <typename Lambda>
    INLINE static void step(Lambda& func)
    {
        if (Mask & (1 << Begin))
        {
            func(Begin);
        }
        UnrollerL<Begin + Step, End, Step>::step(func);
    }
};

template <int End, int Step, int Mask>
struct UnrollerLMask<End, End, Step, Mask>
{
    template <typename Lambda>
    static void step(Lambda& func)
    {
    }
};

// general CRC compute
INLINE
uint32_t ComputeCRC(uint32_t crc, const void* pData, uint32_t size)
{
#if defined(_WIN64) || defined(__x86_64__)
    uint32_t  sizeInQwords       = size / sizeof(uint64_t);
    uint32_t  sizeRemainderBytes = size % sizeof(uint64_t);
    uint64_t* pDataWords         = (uint64_t*)pData;
    for (uint32_t i = 0; i < sizeInQwords; ++i)
    {
        crc = (uint32_t)_mm_crc32_u64(crc, *pDataWords++);
    }
#else
    uint32_t  sizeInDwords       = size / sizeof(uint32_t);
    uint32_t  sizeRemainderBytes = size % sizeof(uint32_t);
    uint32_t* pDataWords         = (uint32_t*)pData;
    for (uint32_t i = 0; i < sizeInDwords; ++i)
    {
        crc = _mm_crc32_u32(crc, *pDataWords++);
    }
#endif

    uint8_t* pRemainderBytes = (uint8_t*)pDataWords;
    for (uint32_t i = 0; i < sizeRemainderBytes; ++i)
    {
        crc = _mm_crc32_u8(crc, *pRemainderBytes++);
    }

    return crc;
}

//////////////////////////////////////////////////////////////////////////
/// Check specified bit within a data word
//////////////////////////////////////////////////////////////////////////
template <typename T>
INLINE static bool CheckBit(T word, uint32_t bit)
{
    return 0 != (word & (T(1) << bit));
}

//////////////////////////////////////////////////////////////////////////
/// Add byte offset to any-type pointer
//////////////////////////////////////////////////////////////////////////
template <typename T>
INLINE static T* PtrAdd(T* p, intptr_t offset)
{
    intptr_t intp = reinterpret_cast<intptr_t>(p);
    return reinterpret_cast<T*>(intp + offset);
}

//////////////////////////////////////////////////////////////////////////
/// Is a power-of-2?
//////////////////////////////////////////////////////////////////////////
template <typename T>
INLINE static bool IsPow2(T value)
{
    return value == (value & (T(0) - value));
}

//////////////////////////////////////////////////////////////////////////
/// Align down to specified alignment
/// Note: IsPow2(alignment) MUST be true
//////////////////////////////////////////////////////////////////////////
template <typename T1, typename T2>
INLINE static T1 AlignDownPow2(T1 value, T2 alignment)
{
    SWR_ASSERT(IsPow2(alignment));
    return value & ~T1(alignment - 1);
}

//////////////////////////////////////////////////////////////////////////
/// Align up to specified alignment
/// Note: IsPow2(alignment) MUST be true
//////////////////////////////////////////////////////////////////////////
template <typename T1, typename T2>
INLINE static T1 AlignUpPow2(T1 value, T2 alignment)
{
    return AlignDownPow2(value + T1(alignment - 1), alignment);
}

//////////////////////////////////////////////////////////////////////////
/// Align up ptr to specified alignment
/// Note: IsPow2(alignment) MUST be true
//////////////////////////////////////////////////////////////////////////
template <typename T1, typename T2>
INLINE static T1* AlignUpPow2(T1* value, T2 alignment)
{
    return reinterpret_cast<T1*>(
        AlignDownPow2(reinterpret_cast<uintptr_t>(value) + uintptr_t(alignment - 1), alignment));
}

//////////////////////////////////////////////////////////////////////////
/// Align down to specified alignment
//////////////////////////////////////////////////////////////////////////
template <typename T1, typename T2>
INLINE static T1 AlignDown(T1 value, T2 alignment)
{
    if (IsPow2(alignment))
    {
        return AlignDownPow2(value, alignment);
    }
    return value - T1(value % alignment);
}

//////////////////////////////////////////////////////////////////////////
/// Align down to specified alignment
//////////////////////////////////////////////////////////////////////////
template <typename T1, typename T2>
INLINE static T1* AlignDown(T1* value, T2 alignment)
{
    return (T1*)AlignDown(uintptr_t(value), alignment);
}

//////////////////////////////////////////////////////////////////////////
/// Align up to specified alignment
/// Note: IsPow2(alignment) MUST be true
//////////////////////////////////////////////////////////////////////////
template <typename T1, typename T2>
INLINE static T1 AlignUp(T1 value, T2 alignment)
{
    return AlignDown(value + T1(alignment - 1), alignment);
}

//////////////////////////////////////////////////////////////////////////
/// Align up to specified alignment
/// Note: IsPow2(alignment) MUST be true
//////////////////////////////////////////////////////////////////////////
template <typename T1, typename T2>
INLINE static T1* AlignUp(T1* value, T2 alignment)
{
    return AlignDown(PtrAdd(value, alignment - 1), alignment);
}

//////////////////////////////////////////////////////////////////////////
/// Helper structure used to access an array of elements that don't
/// correspond to a typical word size.
//////////////////////////////////////////////////////////////////////////
template <typename T, size_t BitsPerElementT, size_t ArrayLenT>
class BitsArray
{
private:
    static const size_t BITS_PER_WORD     = sizeof(size_t) * 8;
    static const size_t ELEMENTS_PER_WORD = BITS_PER_WORD / BitsPerElementT;
    static const size_t NUM_WORDS         = (ArrayLenT + ELEMENTS_PER_WORD - 1) / ELEMENTS_PER_WORD;
    static const size_t ELEMENT_MASK      = (size_t(1) << BitsPerElementT) - 1;

    static_assert(ELEMENTS_PER_WORD * BitsPerElementT == BITS_PER_WORD,
                  "Element size must an integral fraction of pointer size");

    size_t m_words[NUM_WORDS] = {};

public:
    T operator[](size_t elementIndex) const
    {
        size_t word = m_words[elementIndex / ELEMENTS_PER_WORD];
        word >>= ((elementIndex % ELEMENTS_PER_WORD) * BitsPerElementT);
        return T(word & ELEMENT_MASK);
    }
};

// Ranged integer argument for TemplateArgUnroller
template <typename T, T TMin, T TMax>
struct RangedArg
{
    T val;
};

template <uint32_t TMin, uint32_t TMax>
using IntArg = RangedArg<uint32_t, TMin, TMax>;

// Recursive template used to auto-nest conditionals.  Converts dynamic boolean function
// arguments to static template arguments.
template <typename TermT, typename... ArgsB>
struct TemplateArgUnroller
{
    //-----------------------------------------
    // Boolean value
    //-----------------------------------------

    // Last Arg Terminator
    static typename TermT::FuncType GetFunc(bool bArg)
    {
        if (bArg)
        {
            return TermT::template GetFunc<ArgsB..., std::true_type>();
        }

        return TermT::template GetFunc<ArgsB..., std::false_type>();
    }

    // Recursively parse args
    template <typename... TArgsT>
    static typename TermT::FuncType GetFunc(bool bArg, TArgsT... remainingArgs)
    {
        if (bArg)
        {
            return TemplateArgUnroller<TermT, ArgsB..., std::true_type>::GetFunc(remainingArgs...);
        }

        return TemplateArgUnroller<TermT, ArgsB..., std::false_type>::GetFunc(remainingArgs...);
    }

    //-----------------------------------------
    // Ranged value (within specified range)
    //-----------------------------------------

    // Last Arg Terminator
    template <typename T, T TMin, T TMax>
    static typename TermT::FuncType GetFunc(RangedArg<T, TMin, TMax> iArg)
    {
        if (iArg.val == TMax)
        {
            return TermT::template GetFunc<ArgsB..., std::integral_constant<T, TMax>>();
        }
        if (TMax > TMin)
        {
            return TemplateArgUnroller<TermT, ArgsB...>::GetFunc(
                RangedArg<T, TMin, (T)(int(TMax) - 1)>{iArg.val});
        }
        SWR_ASSUME(false);
        return nullptr;
    }
    template <typename T, T TVal>
    static typename TermT::FuncType GetFunc(RangedArg<T, TVal, TVal> iArg)
    {
        SWR_ASSERT(iArg.val == TVal);
        return TermT::template GetFunc<ArgsB..., std::integral_constant<T, TVal>>();
    }

    // Recursively parse args
    template <typename T, T TMin, T TMax, typename... TArgsT>
    static typename TermT::FuncType GetFunc(RangedArg<T, TMin, TMax> iArg, TArgsT... remainingArgs)
    {
        if (iArg.val == TMax)
        {
            return TemplateArgUnroller<TermT, ArgsB..., std::integral_constant<T, TMax>>::GetFunc(
                remainingArgs...);
        }
        if (TMax > TMin)
        {
            return TemplateArgUnroller<TermT, ArgsB...>::GetFunc(
                RangedArg<T, TMin, (T)(int(TMax) - 1)>{iArg.val}, remainingArgs...);
        }
        SWR_ASSUME(false);
        return nullptr;
    }
    template <typename T, T TVal, typename... TArgsT>
    static typename TermT::FuncType GetFunc(RangedArg<T, TVal, TVal> iArg, TArgsT... remainingArgs)
    {
        SWR_ASSERT(iArg.val == TVal);
        return TemplateArgUnroller<TermT, ArgsB..., std::integral_constant<T, TVal>>::GetFunc(
            remainingArgs...);
    }
};

//////////////////////////////////////////////////////////////////////////
/// Helpers used to get / set environment variable
//////////////////////////////////////////////////////////////////////////
static INLINE std::string GetEnv(const std::string& variableName)
{
    std::string output;
#if defined(_WIN32)
    uint32_t valueSize = GetEnvironmentVariableA(variableName.c_str(), nullptr, 0);
    if (!valueSize)
        return output;
    output.resize(valueSize - 1); // valueSize includes null, output.resize() does not
    GetEnvironmentVariableA(variableName.c_str(), &output[0], valueSize);
#else
    char* env = getenv(variableName.c_str());
    output    = env ? env : "";
#endif

    return output;
}

static INLINE void SetEnv(const std::string& variableName, const std::string& value)
{
#if defined(_WIN32)
    SetEnvironmentVariableA(variableName.c_str(), value.c_str());
#else
    setenv(variableName.c_str(), value.c_str(), true);
#endif
}

