/****************************************************************************
 * Copyright (C) 2014-2018 Intel Corporation.   All Rights Reserved.
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
 * @file threads.h
 *
 * @brief Definitions for SWR threading model.
 *
 ******************************************************************************/
#pragma once

#include "knobs.h"

#include <unordered_set>
#include <thread>
typedef std::thread* THREAD_PTR;

struct SWR_CONTEXT;
struct DRAW_CONTEXT;
struct SWR_WORKER_PRIVATE_STATE;

struct THREAD_DATA
{
    void*        pWorkerPrivateData; // Pointer to per-worker private data
    uint32_t     procGroupId;        // Will always be 0 for non-Windows OS
    uint32_t     threadId;           // within the procGroup for Windows
    uint32_t     numaId;             // NUMA node id
    uint32_t     coreId;             // Core id
    uint32_t     htId;               // Hyperthread id
    uint32_t     workerId;           // index of worker in total thread data
    void*        clipperData;        // pointer to hang clipper-private data on
    SWR_CONTEXT* pContext;
    bool         forceBindProcGroup; // Only useful when MAX_WORKER_THREADS is set.
};

struct THREAD_POOL
{
    THREAD_PTR*  pThreads;
    uint32_t     numThreads;
    uint32_t     numaMask;
    THREAD_DATA* pThreadData;
    void*        pWorkerPrivateDataArray; // All memory for worker private data
    uint32_t     numReservedThreads;      // Number of threads reserved for API use
    THREAD_DATA* pApiThreadData;
};

struct TileSet;

void CreateThreadPool(SWR_CONTEXT* pContext, THREAD_POOL* pPool);
void StartThreadPool(SWR_CONTEXT* pContext, THREAD_POOL* pPool);
void DestroyThreadPool(SWR_CONTEXT* pContext, THREAD_POOL* pPool);

// Expose FE and BE worker functions to the API thread if single threaded
void    WorkOnFifoFE(SWR_CONTEXT* pContext, uint32_t workerId, uint32_t& curDrawFE);
bool    WorkOnFifoBE(SWR_CONTEXT* pContext,
                     uint32_t     workerId,
                     uint32_t&    curDrawBE,
                     TileSet&     usedTiles,
                     uint32_t     numaNode,
                     uint32_t     numaMask);
void    WorkOnCompute(SWR_CONTEXT* pContext, uint32_t workerId, uint32_t& curDrawBE);
int32_t CompleteDrawContext(SWR_CONTEXT* pContext, DRAW_CONTEXT* pDC);

void BindApiThread(SWR_CONTEXT* pContext, uint32_t apiThreadId);
