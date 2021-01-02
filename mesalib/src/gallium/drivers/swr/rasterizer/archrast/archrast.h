/****************************************************************************
 * Copyright (C) 2016 Intel Corporation.   All Rights Reserved.
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
 * @file archrast.h
 *
 * @brief Definitions for archrast.
 *
 ******************************************************************************/
#pragma once

#include "common/os.h"
#include "gen_ar_event.hpp"
#include "eventmanager.h"

namespace ArchRast
{
    enum class AR_THREAD
    {
        API    = 0,
        WORKER = 1
    };

    HANDLE CreateThreadContext(AR_THREAD type);
    void   DestroyThreadContext(HANDLE hThreadContext);

    // Dispatch event for this thread.
    void Dispatch(HANDLE hThreadContext, const Event& event);

    void FlushDraw(HANDLE hThreadContext, uint32_t drawId);
}; // namespace ArchRast
