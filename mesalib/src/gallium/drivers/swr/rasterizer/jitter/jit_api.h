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
 * @file jit_api.h
 *
 * @brief Platform independent JIT interface
 *
 * Notes:
 *
 ******************************************************************************/
#pragma once
#include "common/os.h"
#include "core/utils.h"

#include "fetch_jit.h"
#include "streamout_jit.h"
#include "blend_jit.h"

#include <stdlib.h>

#if defined(_WIN32)
#define EXCEPTION_PRINT_STACK(ret) ret
#endif // _WIN32

#if defined(_WIN32)
#define JITCALL __stdcall
#else
#define JITCALL
#endif


struct ShaderInfo;

//////////////////////////////////////////////////////////////////////////
/// Jit Compile Info Input
//////////////////////////////////////////////////////////////////////////
struct JIT_COMPILE_INPUT
{
    SWR_SHADER_TYPE type;
    uint32_t        crc;

    const void* pIR; ///< Pointer to LLVM IR text.
    size_t      irLength;

    bool enableJitSampler;

};


extern "C" {

//////////////////////////////////////////////////////////////////////////
/// @brief Create JIT context.
HANDLE JITCALL JitCreateContext(uint32_t targetSimdWidth, const char* arch, const char* core);

//////////////////////////////////////////////////////////////////////////
/// @brief Destroy JIT context.
void JITCALL JitDestroyContext(HANDLE hJitContext);

//////////////////////////////////////////////////////////////////////////
/// @brief JIT compile shader.
/// @param hJitContext - Jit Context
/// @param input  - Input containing LLVM IR and other information
/// @param output - Output containing information about JIT shader
ShaderInfo* JITCALL JitCompileShader(HANDLE hJitContext, const JIT_COMPILE_INPUT& input);

ShaderInfo* JITCALL JitGetShader(HANDLE hJitContext, const char* name);

//////////////////////////////////////////////////////////////////////////
/// @brief JIT destroy shader.
/// @param hJitContext - Jit Context
/// @param pShaderInfo  - pointer to shader object.
void JITCALL JitDestroyShader(HANDLE hJitContext, ShaderInfo*& pShaderInfo);

//////////////////////////////////////////////////////////////////////////
/// @brief JIT compiles fetch shader
/// @param hJitContext - Jit Context
/// @param state   - Fetch state to build function from
PFN_FETCH_FUNC JITCALL JitCompileFetch(HANDLE hJitContext, const FETCH_COMPILE_STATE& state);

//////////////////////////////////////////////////////////////////////////
/// @brief JIT compiles streamout shader
/// @param hJitContext - Jit Context
/// @param state   - SO state to build function from
PFN_SO_FUNC JITCALL JitCompileStreamout(HANDLE hJitContext, const STREAMOUT_COMPILE_STATE& state);

//////////////////////////////////////////////////////////////////////////
/// @brief JIT compiles blend shader
/// @param hJitContext - Jit Context
/// @param state   - blend state to build function from
PFN_BLEND_JIT_FUNC JITCALL JitCompileBlend(HANDLE hJitContext, const BLEND_COMPILE_STATE& state);

}

