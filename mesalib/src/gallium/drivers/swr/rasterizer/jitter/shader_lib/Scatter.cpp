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
 * @file Scatter.cpp
 *
 * @brief Shader support library implementation for scatter emulation
 *
 * Notes:
 *
 ******************************************************************************/
#include <stdarg.h>
#include "common/os.h"
#include "common/simdlib.hpp"

extern "C" void ScatterPS_256(uint8_t* pBase, SIMD256::Integer vIndices, SIMD256::Float vSrc, uint8_t mask, uint32_t scale)
{
    OSALIGN(float, 32) src[8];
    OSALIGN(uint32_t, 32) indices[8];

    SIMD256::store_ps(src, vSrc);
    SIMD256::store_si((SIMD256::Integer*)indices, vIndices);

    unsigned long index;
    while (_BitScanForward(&index, mask))
    {
        mask &= ~(1 << index);

        *(float*)(pBase + indices[index] * scale) = src[index];
    }
}
