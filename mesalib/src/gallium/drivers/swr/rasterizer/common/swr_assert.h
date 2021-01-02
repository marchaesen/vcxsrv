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
 ****************************************************************************/

#ifndef __SWR_ASSERT_H__
#define __SWR_ASSERT_H__

#if !defined(__SWR_OS_H__)
#error swr_assert.h should not be included directly, please include "common/os.h" instead.
#endif

//=============================================================================
//
// MACROS defined in this file:
//
// - SWR_ASSUME(expression, ...):   Tell compiler that the expression is true.
//                                  Helps with static code analysis as well.
//                                  DO NOT USE if code after this dynamically
//                                  checks for errors and handles them.  The
//                                  compiler may optimize out the error check.
//
// - SWR_ASSERT(expression, ...):   Inform the user is expression is false.
//                                  This check is only conditionally made,
//                                  usually only in debug mode.
//
// - SWR_REL_ASSERT(expression, ...): Unconditionally enabled version of SWR_ASSERT
//
// - SWR_ASSUME_ASSERT(expression, ...): Conditionally enabled SWR_ASSERT.  Uses
//                                       SWR_ASSUME if SWR_ASSERT is disabled.
//                                       DO NOT USE in combination with actual
//                                       error checking (see SWR_ASSUME)
//
// - SWR_REL_ASSUME_ASSERT(expression, ...): Same as SWR_REL_ASSERT.
//
//=============================================================================

// Stupid preprocessor tricks to avoid -Wall / -W4 warnings
#if defined(_MSC_VER)
#define _SWR_WARN_DISABLE __pragma(warning(push)) __pragma(warning(disable : 4127))
#define _SWR_WARN_RESTORE __pragma(warning(pop))
#else // ! MSVC compiler
#define _SWR_WARN_DISABLE
#define _SWR_WARN_RESTORE
#endif

#define _SWR_MACRO_START \
    do                   \
    {
#define _SWR_MACRO_END \
    _SWR_WARN_DISABLE  \
    }                  \
    while (0)          \
    _SWR_WARN_RESTORE

#if defined(_MSC_VER)
#define SWR_ASSUME(e, ...)        \
    _SWR_MACRO_START __assume(e); \
    _SWR_MACRO_END
#elif defined(__clang__)
#define SWR_ASSUME(e, ...)                \
    _SWR_MACRO_START __builtin_assume(e); \
    _SWR_MACRO_END
#elif defined(__GNUC__)
#define SWR_ASSUME(e, ...)                                       \
    _SWR_MACRO_START((e) ? ((void)0) : __builtin_unreachable()); \
    _SWR_MACRO_END
#else
#define SWR_ASSUME(e, ...)      \
    _SWR_MACRO_START ASSUME(e); \
    _SWR_MACRO_END
#endif

#if !defined(SWR_ENABLE_ASSERTS)

#if !defined(NDEBUG)
#define SWR_ENABLE_ASSERTS 1
#else
#define SWR_ENABLE_ASSERTS 0
#endif // _DEBUG

#endif // SWR_ENABLE_ASSERTS

#if !defined(SWR_ENABLE_REL_ASSERTS)
#define SWR_ENABLE_REL_ASSERTS 1
#endif

#if SWR_ENABLE_ASSERTS || SWR_ENABLE_REL_ASSERTS
#include "assert.h"

#if !defined(__cplusplus)

#pragma message("C++ is required for SWR Asserts, falling back to assert.h")

#if SWR_ENABLE_ASSERTS
#define SWR_ASSERT(e, ...) assert(e)
#endif

#if SWR_ENABLE_REL_ASSERTS
#define SWR_REL_ASSERT(e, ...) assert(e)
#endif

#else

bool SwrAssert(bool        chkDebugger,
               bool&       enabled,
               const char* pExpression,
               const char* pFileName,
               uint32_t    lineNum,
               const char* function,
               const char* pFmtString = nullptr,
               ...);

void SwrTrace(
    const char* pFileName, uint32_t lineNum, const char* function, const char* pFmtString, ...);

#define _SWR_ASSERT(chkDebugger, e, ...)                                                                            \
    _SWR_MACRO_START                                                                                                \
    bool expFailed = !(e);                                                                                          \
    if (expFailed)                                                                                                  \
    {                                                                                                               \
        static bool swrAssertEnabled = true;                                                                        \
        expFailed                    = SwrAssert(                                                                   \
            chkDebugger, swrAssertEnabled, #e, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
        if (expFailed)                                                                                              \
        {                                                                                                           \
            DEBUGBREAK;                                                                                             \
        }                                                                                                           \
    }                                                                                                               \
    _SWR_MACRO_END

#define _SWR_INVALID(chkDebugger, ...)                                                                     \
    _SWR_MACRO_START                                                                                       \
    static bool swrAssertEnabled = true;                                                                   \
    bool        expFailed        = SwrAssert(                                                              \
        chkDebugger, swrAssertEnabled, "", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    if (expFailed)                                                                                         \
    {                                                                                                      \
        DEBUGBREAK;                                                                                        \
    }                                                                                                      \
    _SWR_MACRO_END

#define _SWR_TRACE(_fmtstr, ...) SwrTrace(__FILE__, __LINE__, __FUNCTION__, _fmtstr, ##__VA_ARGS__);

#if SWR_ENABLE_ASSERTS
#define SWR_ASSERT(e, ...) _SWR_ASSERT(true, e, ##__VA_ARGS__)
#define SWR_ASSUME_ASSERT(e, ...) SWR_ASSERT(e, ##__VA_ARGS__)
#define SWR_TRACE(_fmtstr, ...) _SWR_TRACE(_fmtstr, ##__VA_ARGS__)
#endif // SWR_ENABLE_ASSERTS

#if SWR_ENABLE_REL_ASSERTS
#define SWR_REL_ASSERT(e, ...) _SWR_ASSERT(false, e, ##__VA_ARGS__)
#define SWR_REL_ASSUME_ASSERT(e, ...) SWR_REL_ASSERT(e, ##__VA_ARGS__)
#define SWR_REL_TRACE(_fmtstr, ...) _SWR_TRACE(_fmtstr, ##__VA_ARGS__)

// SWR_INVALID is always enabled
// Funky handling to allow 0 arguments with g++/gcc
// This is needed because you can't "swallow commas" with ##_VA_ARGS__ unless
// there is a first argument to the macro.  So having a macro that can optionally
// accept 0 arguments is tricky.
#define _SWR_INVALID_0() _SWR_INVALID(false)
#define _SWR_INVALID_1(...) _SWR_INVALID(false, ##__VA_ARGS__)
#define _SWR_INVALID_VARGS_(_10, _9, _8, _7, _6, _5, _4, _3, _2, _1, N, ...) N
#define _SWR_INVALID_VARGS(...) _SWR_INVALID_VARGS_(__VA_ARGS__, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1)
#define _SWR_INVALID_VARGS_0() 1, 2, 3, 4, 5, 6, 7, 9, 9, 10
#define _SWR_INVALID_CONCAT_(a, b) a##b
#define _SWR_INVALID_CONCAT(a, b) _SWR_INVALID_CONCAT_(a, b)
#define SWR_INVALID(...)                                                                       \
    _SWR_INVALID_CONCAT(_SWR_INVALID_, _SWR_INVALID_VARGS(_SWR_INVALID_VARGS_0 __VA_ARGS__())) \
    (__VA_ARGS__)

#define SWR_STATIC_ASSERT(expression, ...) \
    static_assert((expression), "Failed:\n    " #expression "\n    " __VA_ARGS__);

#endif // SWR_ENABLE_REL_ASSERTS

#endif // C++

#endif // SWR_ENABLE_ASSERTS || SWR_ENABLE_REL_ASSERTS

// Needed to allow passing bitfield members to sizeof() in disabled asserts
template <typename T>
static bool SwrSizeofWorkaround(T)
{
    return false;
}

#if !SWR_ENABLE_ASSERTS
#define SWR_ASSERT(e, ...)                                 \
    _SWR_MACRO_START(void) sizeof(SwrSizeofWorkaround(e)); \
    _SWR_MACRO_END
#define SWR_ASSUME_ASSERT(e, ...) SWR_ASSUME(e, ##__VA_ARGS__)
#define SWR_TRACE(_fmtstr, ...) \
    _SWR_MACRO_START(void)(0);  \
    _SWR_MACRO_END
#endif

#if !SWR_ENABLE_REL_ASSERTS
#define SWR_REL_ASSERT(e, ...)                             \
    _SWR_MACRO_START(void) sizeof(SwrSizeofWorkaround(e)); \
    _SWR_MACRO_END
#define SWR_INVALID(...)       \
    _SWR_MACRO_START(void)(0); \
    _SWR_MACRO_END
#define SWR_REL_ASSUME_ASSERT(e, ...) SWR_ASSUME(e, ##__VA_ARGS__)
#define SWR_REL_TRACE(_fmtstr, ...) \
    _SWR_MACRO_START(void)(0);      \
    _SWR_MACRO_END
#define SWR_STATIC_ASSERT(e, ...)                           \
    _SWR_MACRO_START(void)  sizeof(SwrSizeofWorkaround(e)); \
    _SWR_MACRO_END
#endif

#if defined(_MSC_VER)
#define SWR_FUNCTION_DECL __FUNCSIG__
#elif (defined(__GNUC__) || defined(__clang__))
#define SWR_FUNCTION_DECL __PRETTY_FUNCTION__
#else
#define SWR_FUNCTION_DECL __FUNCTION__
#endif

#define SWR_NOT_IMPL SWR_INVALID("%s not implemented", SWR_FUNCTION_DECL)

#endif //__SWR_ASSERT_H__
