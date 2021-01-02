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
 * @file api.cpp
 *
 * @brief API implementation
 *
 ******************************************************************************/

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <new>

#include "core/api.h"
#include "core/backend.h"
#include "core/context.h"
#include "core/depthstencil.h"
#include "core/frontend.h"
#include "core/rasterizer.h"
#include "core/rdtsc_core.h"
#include "core/threads.h"
#include "core/tilemgr.h"
#include "core/clip.h"
#include "core/utils.h"
#include "core/tileset.h"

#include "common/os.h"

static const SWR_RECT g_MaxScissorRect = {0, 0, KNOB_MAX_SCISSOR_X, KNOB_MAX_SCISSOR_Y};

void SetupDefaultState(SWR_CONTEXT* pContext);

static INLINE SWR_CONTEXT* GetContext(HANDLE hContext)
{
    return (SWR_CONTEXT*)hContext;
}

void WakeAllThreads(SWR_CONTEXT* pContext)
{
    pContext->FifosNotEmpty.notify_all();
}

//////////////////////////////////////////////////////////////////////////
/// @brief Create SWR Context.
/// @param pCreateInfo - pointer to creation info.
HANDLE SwrCreateContext(SWR_CREATECONTEXT_INFO* pCreateInfo)
{
    void* pContextMem = AlignedMalloc(sizeof(SWR_CONTEXT), KNOB_SIMD_WIDTH * 4);
    memset(pContextMem, 0, sizeof(SWR_CONTEXT));
    SWR_CONTEXT* pContext = new (pContextMem) SWR_CONTEXT();

    pContext->privateStateSize = pCreateInfo->privateStateSize;

    // initialize callback functions
    pContext->pfnLoadTile                = pCreateInfo->pfnLoadTile;
    pContext->pfnStoreTile               = pCreateInfo->pfnStoreTile;
    pContext->pfnTranslateGfxptrForRead  = pCreateInfo->pfnTranslateGfxptrForRead;
    pContext->pfnTranslateGfxptrForWrite = pCreateInfo->pfnTranslateGfxptrForWrite;
    pContext->pfnMakeGfxPtr              = pCreateInfo->pfnMakeGfxPtr;
    pContext->pfnCreateMemoryContext     = pCreateInfo->pfnCreateMemoryContext;
    pContext->pfnDestroyMemoryContext    = pCreateInfo->pfnDestroyMemoryContext;
    pContext->pfnUpdateSoWriteOffset     = pCreateInfo->pfnUpdateSoWriteOffset;
    pContext->pfnUpdateStats             = pCreateInfo->pfnUpdateStats;
    pContext->pfnUpdateStatsFE           = pCreateInfo->pfnUpdateStatsFE;
    pContext->pfnUpdateStreamOut         = pCreateInfo->pfnUpdateStreamOut;


    pContext->hExternalMemory = pCreateInfo->hExternalMemory;

    pContext->MAX_DRAWS_IN_FLIGHT = KNOB_MAX_DRAWS_IN_FLIGHT;
    if (pCreateInfo->MAX_DRAWS_IN_FLIGHT != 0)
    {
        pContext->MAX_DRAWS_IN_FLIGHT = pCreateInfo->MAX_DRAWS_IN_FLIGHT;
    }

    pContext->dcRing.Init(pContext->MAX_DRAWS_IN_FLIGHT);
    pContext->dsRing.Init(pContext->MAX_DRAWS_IN_FLIGHT);

    pContext->pMacroTileManagerArray =
        (MacroTileMgr*)AlignedMalloc(sizeof(MacroTileMgr) * pContext->MAX_DRAWS_IN_FLIGHT, 64);
    pContext->pDispatchQueueArray =
        (DispatchQueue*)AlignedMalloc(sizeof(DispatchQueue) * pContext->MAX_DRAWS_IN_FLIGHT, 64);

    for (uint32_t dc = 0; dc < pContext->MAX_DRAWS_IN_FLIGHT; ++dc)
    {
        pContext->dcRing[dc].pArena = new CachingArena(pContext->cachingArenaAllocator);
        new (&pContext->pMacroTileManagerArray[dc]) MacroTileMgr(*pContext->dcRing[dc].pArena);
        new (&pContext->pDispatchQueueArray[dc]) DispatchQueue();

        pContext->dsRing[dc].pArena = new CachingArena(pContext->cachingArenaAllocator);
    }

    if (pCreateInfo->pThreadInfo)
    {
        pContext->threadInfo = *pCreateInfo->pThreadInfo;
    }
    else
    {
        pContext->threadInfo.MAX_WORKER_THREADS      = KNOB_MAX_WORKER_THREADS;
        pContext->threadInfo.BASE_NUMA_NODE          = KNOB_BASE_NUMA_NODE;
        pContext->threadInfo.BASE_CORE               = KNOB_BASE_CORE;
        pContext->threadInfo.BASE_THREAD             = KNOB_BASE_THREAD;
        pContext->threadInfo.MAX_NUMA_NODES          = KNOB_MAX_NUMA_NODES;
        pContext->threadInfo.MAX_CORES_PER_NUMA_NODE = KNOB_MAX_CORES_PER_NUMA_NODE;
        pContext->threadInfo.MAX_THREADS_PER_CORE    = KNOB_MAX_THREADS_PER_CORE;
        pContext->threadInfo.SINGLE_THREADED         = KNOB_SINGLE_THREADED;
    }

    if (pCreateInfo->pApiThreadInfo)
    {
        pContext->apiThreadInfo = *pCreateInfo->pApiThreadInfo;
    }
    else
    {
        pContext->apiThreadInfo.bindAPIThread0        = true;
        pContext->apiThreadInfo.numAPIReservedThreads = 1;
        pContext->apiThreadInfo.numAPIThreadsPerCore  = 1;
    }

    if (pCreateInfo->pWorkerPrivateState)
    {
        pContext->workerPrivateState = *pCreateInfo->pWorkerPrivateState;
    }

    memset((void*)&pContext->WaitLock, 0, sizeof(pContext->WaitLock));
    memset((void*)&pContext->FifosNotEmpty, 0, sizeof(pContext->FifosNotEmpty));
    new (&pContext->WaitLock) std::mutex();
    new (&pContext->FifosNotEmpty) std::condition_variable();

    CreateThreadPool(pContext, &pContext->threadPool);

    if (pContext->apiThreadInfo.bindAPIThread0)
    {
        BindApiThread(pContext, 0);
    }

    if (pContext->threadInfo.SINGLE_THREADED)
    {
        pContext->pSingleThreadLockedTiles = new TileSet();
    }

    pContext->ppScratch = new uint8_t*[pContext->NumWorkerThreads];
    pContext->pStats =
        (SWR_STATS*)AlignedMalloc(sizeof(SWR_STATS) * pContext->NumWorkerThreads, 64);

#if defined(KNOB_ENABLE_AR)
    // Setup ArchRast thread contexts which includes +1 for API thread.
    pContext->pArContext = new HANDLE[pContext->NumWorkerThreads + 1];
    pContext->pArContext[pContext->NumWorkerThreads] =
        ArchRast::CreateThreadContext(ArchRast::AR_THREAD::API);
#endif

#if defined(KNOB_ENABLE_RDTSC)
    pContext->pBucketMgr = new BucketManager(pCreateInfo->contextName);
    RDTSC_RESET(pContext->pBucketMgr);
    RDTSC_INIT(pContext->pBucketMgr, 0);
#endif

    // Allocate scratch space for workers.
    ///@note We could lazily allocate this but its rather small amount of memory.
    for (uint32_t i = 0; i < pContext->NumWorkerThreads; ++i)
    {
#if defined(_WIN32)
        uint32_t numaNode =
            pContext->threadPool.pThreadData ? pContext->threadPool.pThreadData[i].numaId : 0;
        pContext->ppScratch[i] = (uint8_t*)VirtualAllocExNuma(GetCurrentProcess(),
                                                              nullptr,
                                                              KNOB_WORKER_SCRATCH_SPACE_SIZE,
                                                              MEM_RESERVE | MEM_COMMIT,
                                                              PAGE_READWRITE,
                                                              numaNode);
#else
        pContext->ppScratch[i] =
            (uint8_t*)AlignedMalloc(KNOB_WORKER_SCRATCH_SPACE_SIZE, KNOB_SIMD_WIDTH * 4);
#endif

#if defined(KNOB_ENABLE_AR)
        // Initialize worker thread context for ArchRast.
        pContext->pArContext[i] = ArchRast::CreateThreadContext(ArchRast::AR_THREAD::WORKER);

        SWR_WORKER_DATA* pWorkerData = (SWR_WORKER_DATA*)pContext->threadPool.pThreadData[i].pWorkerPrivateData;
        pWorkerData->hArContext = pContext->pArContext[i];
#endif


    }

#if defined(KNOB_ENABLE_AR)
    // cache the API thread event manager, for use with sim layer
    pCreateInfo->hArEventManager = pContext->pArContext[pContext->NumWorkerThreads];
#endif

    // State setup AFTER context is fully initialized
    SetupDefaultState(pContext);

    // initialize hot tile manager
    pContext->pHotTileMgr = new HotTileMgr();

    // pass pointer to bucket manager back to caller
#ifdef KNOB_ENABLE_RDTSC
    pCreateInfo->pBucketMgr = pContext->pBucketMgr;
#endif

    pCreateInfo->contextSaveSize = sizeof(API_STATE);

    StartThreadPool(pContext, &pContext->threadPool);

    return (HANDLE)pContext;
}

void CopyState(DRAW_STATE& dst, const DRAW_STATE& src)
{
    memcpy((void*)&dst.state, (void*)&src.state, sizeof(API_STATE));
}

template <bool IsDraw>
void QueueWork(SWR_CONTEXT* pContext)
{
    DRAW_CONTEXT* pDC     = pContext->pCurDrawContext;
    uint32_t      dcIndex = pDC->drawId % pContext->MAX_DRAWS_IN_FLIGHT;

    if (IsDraw)
    {
        pDC->pTileMgr = &pContext->pMacroTileManagerArray[dcIndex];
        pDC->pTileMgr->initialize();
    }

    // Each worker thread looks at a DC for both FE and BE work at different times and so we
    // multiply threadDone by 2.  When the threadDone counter has reached 0 then all workers
    // have moved past this DC. (i.e. Each worker has checked this DC for both FE and BE work and
    // then moved on if all work is done.)
    pContext->pCurDrawContext->threadsDone = pContext->NumFEThreads + pContext->NumBEThreads;

    if (IsDraw)
    {
        InterlockedIncrement(&pContext->drawsOutstandingFE);
    }

    _ReadWriteBarrier();
    {
        std::unique_lock<std::mutex> lock(pContext->WaitLock);
        pContext->dcRing.Enqueue();
    }

    if (pContext->threadInfo.SINGLE_THREADED)
    {
        uint32_t mxcsr = SetOptimalVectorCSR();

        if (IsDraw)
        {
            uint32_t curDraw[2] = {pContext->pCurDrawContext->drawId,
                                   pContext->pCurDrawContext->drawId};
            WorkOnFifoFE(pContext, 0, curDraw[0]);
            WorkOnFifoBE(pContext, 0, curDraw[1], *pContext->pSingleThreadLockedTiles, 0, 0);
        }
        else
        {
            uint32_t curDispatch = pContext->pCurDrawContext->drawId;
            WorkOnCompute(pContext, 0, curDispatch);
        }

        // Dequeue the work here, if not already done, since we're single threaded (i.e. no
        // workers).
        while (CompleteDrawContext(pContext, pContext->pCurDrawContext) > 0)
        {
        }

        // restore csr
        RestoreVectorCSR(mxcsr);
    }
    else
    {
        RDTSC_BEGIN(pContext->pBucketMgr, APIDrawWakeAllThreads, pDC->drawId);
        WakeAllThreads(pContext);
        RDTSC_END(pContext->pBucketMgr, APIDrawWakeAllThreads, 1);
    }

    // Set current draw context to NULL so that next state call forces a new draw context to be
    // created and populated.
    pContext->pPrevDrawContext = pContext->pCurDrawContext;
    pContext->pCurDrawContext  = nullptr;
}

INLINE void QueueDraw(SWR_CONTEXT* pContext)
{
    QueueWork<true>(pContext);
}

INLINE void QueueDispatch(SWR_CONTEXT* pContext)
{
    QueueWork<false>(pContext);
}

DRAW_CONTEXT* GetDrawContext(SWR_CONTEXT* pContext, bool isSplitDraw = false)
{
    RDTSC_BEGIN(pContext->pBucketMgr, APIGetDrawContext, 0);
    // If current draw context is null then need to obtain a new draw context to use from ring.
    if (pContext->pCurDrawContext == nullptr)
    {
        // Need to wait for a free entry.
        while (pContext->dcRing.IsFull())
        {
            _mm_pause();
        }

        uint64_t curDraw = pContext->dcRing.GetHead();
        uint32_t dcIndex = curDraw % pContext->MAX_DRAWS_IN_FLIGHT;

        if ((pContext->frameCount - pContext->lastFrameChecked) > 2 ||
            (curDraw - pContext->lastDrawChecked) > 0x10000)
        {
            // Take this opportunity to clean-up old arena allocations
            pContext->cachingArenaAllocator.FreeOldBlocks();

            pContext->lastFrameChecked = pContext->frameCount;
            pContext->lastDrawChecked  = curDraw;
        }

        DRAW_CONTEXT* pCurDrawContext = &pContext->dcRing[dcIndex];
        pContext->pCurDrawContext     = pCurDrawContext;

        // Assign next available entry in DS ring to this DC.
        uint32_t dsIndex        = pContext->curStateId % pContext->MAX_DRAWS_IN_FLIGHT;
        pCurDrawContext->pState = &pContext->dsRing[dsIndex];

        // Copy previous state to current state.
        if (pContext->pPrevDrawContext)
        {
            DRAW_CONTEXT* pPrevDrawContext = pContext->pPrevDrawContext;

            // If we're splitting our draw then we can just use the same state from the previous
            // draw. In this case, we won't increment the DS ring index so the next non-split
            // draw can receive the state.
            if (isSplitDraw == false)
            {
                CopyState(*pCurDrawContext->pState, *pPrevDrawContext->pState);

                // Should have been cleaned up previously
                SWR_ASSERT(pCurDrawContext->pState->pArena->IsEmpty() == true);

                pCurDrawContext->pState->pPrivateState = nullptr;

                pContext->curStateId++; // Progress state ring index forward.
            }
            else
            {
                // If its a split draw then just copy the state pointer over
                // since its the same draw.
                pCurDrawContext->pState = pPrevDrawContext->pState;
                SWR_ASSERT(pPrevDrawContext->cleanupState == false);
            }
        }
        else
        {
            SWR_ASSERT(pCurDrawContext->pState->pArena->IsEmpty() == true);
            pContext->curStateId++; // Progress state ring index forward.
        }

        SWR_ASSERT(pCurDrawContext->pArena->IsEmpty() == true);

        // Reset dependency
        pCurDrawContext->dependent   = false;
        pCurDrawContext->dependentFE = false;

        pCurDrawContext->pContext  = pContext;
        pCurDrawContext->isCompute = false; // Dispatch has to set this to true.

        pCurDrawContext->doneFE                         = false;
        pCurDrawContext->FeLock                         = 0;
        pCurDrawContext->threadsDone                    = 0;
        pCurDrawContext->retireCallback.pfnCallbackFunc = nullptr;

        pCurDrawContext->dynState.Reset(pContext->NumWorkerThreads);

        // Assign unique drawId for this DC
        pCurDrawContext->drawId = pContext->dcRing.GetHead();

        pCurDrawContext->cleanupState = true;
    }
    else
    {
        SWR_ASSERT(isSplitDraw == false, "Split draw should only be used when obtaining a new DC");
    }

    RDTSC_END(pContext->pBucketMgr, APIGetDrawContext, 0);
    return pContext->pCurDrawContext;
}

API_STATE* GetDrawState(SWR_CONTEXT* pContext)
{
    DRAW_CONTEXT* pDC = GetDrawContext(pContext);
    SWR_ASSERT(pDC->pState != nullptr);

    return &pDC->pState->state;
}

void SwrDestroyContext(HANDLE hContext)
{
    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);

    pDC->FeWork.type    = SHUTDOWN;
    pDC->FeWork.pfnWork = ProcessShutdown;

    // enqueue
    QueueDraw(pContext);

    DestroyThreadPool(pContext, &pContext->threadPool);

    // free the fifos
    for (uint32_t i = 0; i < pContext->MAX_DRAWS_IN_FLIGHT; ++i)
    {
        AlignedFree(pContext->dcRing[i].dynState.pStats);
        delete pContext->dcRing[i].pArena;
        delete pContext->dsRing[i].pArena;
        pContext->pMacroTileManagerArray[i].~MacroTileMgr();
        pContext->pDispatchQueueArray[i].~DispatchQueue();
    }

    AlignedFree(pContext->pDispatchQueueArray);
    AlignedFree(pContext->pMacroTileManagerArray);

    // Free scratch space.
    for (uint32_t i = 0; i < pContext->NumWorkerThreads; ++i)
    {
#if defined(_WIN32)
        VirtualFree(pContext->ppScratch[i], 0, MEM_RELEASE);
#else
        AlignedFree(pContext->ppScratch[i]);
#endif

#if defined(KNOB_ENABLE_AR)
        ArchRast::DestroyThreadContext(pContext->pArContext[i]);
#endif
    }

#if defined(KNOB_ENABLE_RDTSC)
    delete pContext->pBucketMgr;
#endif

    delete[] pContext->ppScratch;
    AlignedFree(pContext->pStats);

    delete pContext->pHotTileMgr;
    delete pContext->pSingleThreadLockedTiles;

    pContext->~SWR_CONTEXT();
    AlignedFree(GetContext(hContext));
}

void SwrBindApiThread(HANDLE hContext, uint32_t apiThreadId)
{
    SWR_CONTEXT* pContext = GetContext(hContext);
    BindApiThread(pContext, apiThreadId);
}

void SWR_API SwrSaveState(HANDLE hContext, void* pOutputStateBlock, size_t memSize)
{
    SWR_CONTEXT* pContext = GetContext(hContext);
    auto         pSrc     = GetDrawState(pContext);
    assert(pOutputStateBlock && memSize >= sizeof(*pSrc));

    memcpy(pOutputStateBlock, pSrc, sizeof(*pSrc));
}

void SWR_API SwrRestoreState(HANDLE hContext, const void* pStateBlock, size_t memSize)
{
    SWR_CONTEXT* pContext = GetContext(hContext);
    auto         pDst     = GetDrawState(pContext);
    assert(pStateBlock && memSize >= sizeof(*pDst));

    memcpy((void*)pDst, (void*)pStateBlock, sizeof(*pDst));
}

void SetupDefaultState(SWR_CONTEXT* pContext)
{
    API_STATE* pState = GetDrawState(pContext);

    pState->rastState.cullMode     = SWR_CULLMODE_NONE;
    pState->rastState.frontWinding = SWR_FRONTWINDING_CCW;

    pState->depthBoundsState.depthBoundsTestEnable   = false;
    pState->depthBoundsState.depthBoundsTestMinValue = 0.0f;
    pState->depthBoundsState.depthBoundsTestMaxValue = 1.0f;
}

void SWR_API SwrSync(HANDLE            hContext,
                     PFN_CALLBACK_FUNC pfnFunc,
                     uint64_t          userData,
                     uint64_t          userData2,
                     uint64_t          userData3)
{
    SWR_ASSERT(pfnFunc != nullptr);

    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);

    RDTSC_BEGIN(pContext->pBucketMgr, APISync, 0);

    pDC->FeWork.type    = SYNC;
    pDC->FeWork.pfnWork = ProcessSync;

    // Setup callback function
    pDC->retireCallback.pfnCallbackFunc = pfnFunc;
    pDC->retireCallback.userData        = userData;
    pDC->retireCallback.userData2       = userData2;
    pDC->retireCallback.userData3       = userData3;

    AR_API_EVENT(SwrSyncEvent(pDC->drawId));

    // enqueue
    QueueDraw(pContext);

    RDTSC_END(pContext->pBucketMgr, APISync, 1);
}

void SwrStallBE(HANDLE hContext)
{
    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);

    pDC->dependent = true;
}

void SwrWaitForIdle(HANDLE hContext)
{
    SWR_CONTEXT* pContext = GetContext(hContext);

    RDTSC_BEGIN(pContext->pBucketMgr, APIWaitForIdle, 0);

    while (!pContext->dcRing.IsEmpty())
    {
        _mm_pause();
    }

    RDTSC_END(pContext->pBucketMgr, APIWaitForIdle, 1);
}

void SwrWaitForIdleFE(HANDLE hContext)
{
    SWR_CONTEXT* pContext = GetContext(hContext);

    RDTSC_BEGIN(pContext->pBucketMgr, APIWaitForIdle, 0);

    while (pContext->drawsOutstandingFE > 0)
    {
        _mm_pause();
    }

    RDTSC_END(pContext->pBucketMgr, APIWaitForIdle, 1);
}

void SwrSetVertexBuffers(HANDLE                         hContext,
                         uint32_t                       numBuffers,
                         const SWR_VERTEX_BUFFER_STATE* pVertexBuffers)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));

    for (uint32_t i = 0; i < numBuffers; ++i)
    {
        const SWR_VERTEX_BUFFER_STATE* pVB = &pVertexBuffers[i];
        pState->vertexBuffers[pVB->index]  = *pVB;
    }
}

void SwrSetIndexBuffer(HANDLE hContext, const SWR_INDEX_BUFFER_STATE* pIndexBuffer)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));

    pState->indexBuffer = *pIndexBuffer;
}

void SwrSetFetchFunc(HANDLE hContext, PFN_FETCH_FUNC pfnFetchFunc)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));

    pState->pfnFetchFunc = pfnFetchFunc;
}

void SwrSetSoFunc(HANDLE hContext, PFN_SO_FUNC pfnSoFunc, uint32_t streamIndex)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));

    SWR_ASSERT(streamIndex < MAX_SO_STREAMS);

    pState->pfnSoFunc[streamIndex] = pfnSoFunc;
}

void SwrSetSoState(HANDLE hContext, SWR_STREAMOUT_STATE* pSoState)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));

    pState->soState = *pSoState;
}

void SwrSetSoBuffers(HANDLE hContext, SWR_STREAMOUT_BUFFER* pSoBuffer, uint32_t slot)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));

    SWR_ASSERT((slot < MAX_SO_STREAMS), "There are only 4 SO buffer slots [0, 3]\nSlot requested: %d", slot);

    // remember buffer status in case of future resume StreamOut
    if ((pState->soBuffer[slot].pBuffer != 0) && (pSoBuffer->pBuffer == 0))
	pState->soPausedBuffer[slot] = pState->soBuffer[slot];

    // resume
    if (pState->soPausedBuffer[slot].pBuffer == pSoBuffer->pBuffer)
	pState->soBuffer[slot] = pState->soPausedBuffer[slot];
    else
        pState->soBuffer[slot] = *pSoBuffer;
}

void SwrSetVertexFunc(HANDLE hContext, PFN_VERTEX_FUNC pfnVertexFunc)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));

    pState->pfnVertexFunc = pfnVertexFunc;
}

void SwrSetFrontendState(HANDLE hContext, SWR_FRONTEND_STATE* pFEState)
{
    API_STATE* pState     = GetDrawState(GetContext(hContext));
    pState->frontendState = *pFEState;
}

void SwrSetGsState(HANDLE hContext, SWR_GS_STATE* pGSState)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));
    pState->gsState   = *pGSState;
}

void SwrSetGsFunc(HANDLE hContext, PFN_GS_FUNC pfnGsFunc)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));
    pState->pfnGsFunc = pfnGsFunc;
}

void SwrSetCsFunc(HANDLE      hContext,
                  PFN_CS_FUNC pfnCsFunc,
                  uint32_t    totalThreadsInGroup,
                  uint32_t    totalSpillFillSize,
                  uint32_t    scratchSpaceSizePerWarp,
                  uint32_t    numWarps)
{
    API_STATE* pState               = GetDrawState(GetContext(hContext));
    pState->pfnCsFunc               = pfnCsFunc;
    pState->totalThreadsInGroup     = totalThreadsInGroup;
    pState->totalSpillFillSize      = totalSpillFillSize;
    pState->scratchSpaceSizePerWarp = scratchSpaceSizePerWarp;
    pState->scratchSpaceNumWarps    = numWarps;
}

void SwrSetTsState(HANDLE hContext, SWR_TS_STATE* pState)
{
    API_STATE* pApiState = GetDrawState(GetContext(hContext));
    pApiState->tsState   = *pState;
}

void SwrSetHsFunc(HANDLE hContext, PFN_HS_FUNC pfnFunc)
{
    API_STATE* pApiState = GetDrawState(GetContext(hContext));
    pApiState->pfnHsFunc = pfnFunc;
}

void SwrSetDsFunc(HANDLE hContext, PFN_DS_FUNC pfnFunc)
{
    API_STATE* pApiState = GetDrawState(GetContext(hContext));
    pApiState->pfnDsFunc = pfnFunc;
}

void SwrSetDepthStencilState(HANDLE hContext, SWR_DEPTH_STENCIL_STATE* pDSState)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));

    pState->depthStencilState = *pDSState;
}

void SwrSetBackendState(HANDLE hContext, SWR_BACKEND_STATE* pBEState)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));

    pState->backendState = *pBEState;
}

void SwrSetDepthBoundsState(HANDLE hContext, SWR_DEPTH_BOUNDS_STATE* pDBState)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));

    pState->depthBoundsState = *pDBState;
}

void SwrSetPixelShaderState(HANDLE hContext, SWR_PS_STATE* pPSState)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));
    pState->psState   = *pPSState;
}

void SwrSetBlendState(HANDLE hContext, SWR_BLEND_STATE* pBlendState)
{
    API_STATE* pState = GetDrawState(GetContext(hContext));
    memcpy(&pState->blendState, pBlendState, sizeof(SWR_BLEND_STATE));
}

void SwrSetBlendFunc(HANDLE hContext, uint32_t renderTarget, PFN_BLEND_JIT_FUNC pfnBlendFunc)
{
    SWR_ASSERT(renderTarget < SWR_NUM_RENDERTARGETS);
    API_STATE* pState                  = GetDrawState(GetContext(hContext));
    pState->pfnBlendFunc[renderTarget] = pfnBlendFunc;
}

// update guardband multipliers for the viewport
void updateGuardbands(API_STATE* pState)
{
    uint32_t numGbs = pState->backendState.readViewportArrayIndex ? KNOB_NUM_VIEWPORTS_SCISSORS : 1;

    for (uint32_t i = 0; i < numGbs; ++i)
    {
        // guardband center is viewport center
        pState->gbState.left[i]   = KNOB_GUARDBAND_WIDTH / pState->vp[i].width;
        pState->gbState.right[i]  = KNOB_GUARDBAND_WIDTH / pState->vp[i].width;
        pState->gbState.top[i]    = KNOB_GUARDBAND_HEIGHT / pState->vp[i].height;
        pState->gbState.bottom[i] = KNOB_GUARDBAND_HEIGHT / pState->vp[i].height;
    }
}

void SwrSetRastState(HANDLE hContext, const SWR_RASTSTATE* pRastState)
{
    SWR_CONTEXT* pContext = GetContext(hContext);
    API_STATE*   pState   = GetDrawState(pContext);

    memcpy((void*)&pState->rastState, (void*)pRastState, sizeof(SWR_RASTSTATE));
}

void SwrSetViewports(HANDLE                       hContext,
                     uint32_t                     numViewports,
                     const SWR_VIEWPORT*          pViewports,
                     const SWR_VIEWPORT_MATRICES* pMatrices)
{
    SWR_ASSERT(numViewports <= KNOB_NUM_VIEWPORTS_SCISSORS, "Invalid number of viewports.");

    SWR_CONTEXT* pContext = GetContext(hContext);
    API_STATE*   pState   = GetDrawState(pContext);

    memcpy(&pState->vp[0], pViewports, sizeof(SWR_VIEWPORT) * numViewports);
    // @todo Faster to copy portions of the SOA or just copy all of it?
    memcpy(&pState->vpMatrices, pMatrices, sizeof(SWR_VIEWPORT_MATRICES));
}

void SwrSetScissorRects(HANDLE hContext, uint32_t numScissors, const SWR_RECT* pScissors)
{
    SWR_ASSERT(numScissors <= KNOB_NUM_VIEWPORTS_SCISSORS, "Invalid number of scissor rects.");

    API_STATE* pState = GetDrawState(GetContext(hContext));
    memcpy(&pState->scissorRects[0], pScissors, numScissors * sizeof(pScissors[0]));
};

void SetupMacroTileScissors(DRAW_CONTEXT* pDC)
{
    API_STATE* pState = &pDC->pState->state;
    uint32_t numScissors =
        pState->backendState.readViewportArrayIndex ? KNOB_NUM_VIEWPORTS_SCISSORS : 1;
    pState->scissorsTileAligned = true;

    for (uint32_t index = 0; index < numScissors; ++index)
    {
        SWR_RECT& scissorInFixedPoint = pState->scissorsInFixedPoint[index];

        // Set up scissor dimensions based on scissor or viewport
        if (pState->rastState.scissorEnable)
        {
            scissorInFixedPoint = pState->scissorRects[index];
        }
        else
        {
            // the vp width and height must be added to origin un-rounded then the result round to
            // -inf. The cast to int works for rounding assuming all [left, right, top, bottom] are
            // positive.
            scissorInFixedPoint.xmin = (int32_t)pState->vp[index].x;
            scissorInFixedPoint.xmax = (int32_t)(pState->vp[index].x + pState->vp[index].width);
            scissorInFixedPoint.ymin = (int32_t)pState->vp[index].y;
            scissorInFixedPoint.ymax = (int32_t)(pState->vp[index].y + pState->vp[index].height);
        }

        // Clamp to max rect
        scissorInFixedPoint &= g_MaxScissorRect;

        // Test for tile alignment
        bool tileAligned;
        tileAligned = (scissorInFixedPoint.xmin % KNOB_TILE_X_DIM) == 0;
        tileAligned &= (scissorInFixedPoint.ymin % KNOB_TILE_Y_DIM) == 0;
        tileAligned &= (scissorInFixedPoint.xmax % KNOB_TILE_X_DIM) == 0;
        tileAligned &= (scissorInFixedPoint.ymax % KNOB_TILE_Y_DIM) == 0;

        pState->scissorsTileAligned &= tileAligned;

        // Scale to fixed point
        scissorInFixedPoint.xmin *= FIXED_POINT_SCALE;
        scissorInFixedPoint.xmax *= FIXED_POINT_SCALE;
        scissorInFixedPoint.ymin *= FIXED_POINT_SCALE;
        scissorInFixedPoint.ymax *= FIXED_POINT_SCALE;

        // Make scissor inclusive
        scissorInFixedPoint.xmax -= 1;
        scissorInFixedPoint.ymax -= 1;
    }
}


// templated backend function tables

void SetupPipeline(DRAW_CONTEXT* pDC)
{
    DRAW_STATE*          pState       = pDC->pState;
    const SWR_RASTSTATE& rastState    = pState->state.rastState;
    const SWR_PS_STATE&  psState      = pState->state.psState;
    BACKEND_FUNCS&       backendFuncs = pState->backendFuncs;

    // setup backend
    if (psState.pfnPixelShader == nullptr)
    {
        backendFuncs.pfnBackend = gBackendNullPs[pState->state.rastState.sampleCount];
    }
    else
    {
        const uint32_t forcedSampleCount = (rastState.forcedSampleCount) ? 1 : 0;
        const bool     bMultisampleEnable =
            ((rastState.sampleCount > SWR_MULTISAMPLE_1X) || forcedSampleCount) ? 1 : 0;
        const uint32_t centroid =
            ((psState.barycentricsMask & SWR_BARYCENTRIC_CENTROID_MASK) > 0) ? 1 : 0;
        const uint32_t canEarlyZ =
            (psState.forceEarlyZ || (!psState.writesODepth && !psState.usesUAV)) ? 1 : 0;
        SWR_BARYCENTRICS_MASK barycentricsMask = (SWR_BARYCENTRICS_MASK)psState.barycentricsMask;

        // select backend function
        switch (psState.shadingRate)
        {
        case SWR_SHADING_RATE_PIXEL:
            if (bMultisampleEnable)
            {
                // always need to generate I & J per sample for Z interpolation
                barycentricsMask =
                    (SWR_BARYCENTRICS_MASK)(barycentricsMask | SWR_BARYCENTRIC_PER_SAMPLE_MASK);
                backendFuncs.pfnBackend =
                    gBackendPixelRateTable[rastState.sampleCount][rastState.bIsCenterPattern]
                                          [psState.inputCoverage][centroid][forcedSampleCount]
                                          [canEarlyZ]
                    ;
            }
            else
            {
                // always need to generate I & J per pixel for Z interpolation
                barycentricsMask =
                    (SWR_BARYCENTRICS_MASK)(barycentricsMask | SWR_BARYCENTRIC_PER_PIXEL_MASK);
                backendFuncs.pfnBackend =
                    gBackendSingleSample[psState.inputCoverage][centroid][canEarlyZ];
            }
            break;
        case SWR_SHADING_RATE_SAMPLE:
            SWR_ASSERT(rastState.bIsCenterPattern != true);
            // always need to generate I & J per sample for Z interpolation
            barycentricsMask =
                (SWR_BARYCENTRICS_MASK)(barycentricsMask | SWR_BARYCENTRIC_PER_SAMPLE_MASK);
            backendFuncs.pfnBackend =
                gBackendSampleRateTable[rastState.sampleCount][psState.inputCoverage][centroid]
                                       [canEarlyZ];
            break;
        default:
            SWR_ASSERT(0 && "Invalid shading rate");
            break;
        }
    }

    SWR_ASSERT(backendFuncs.pfnBackend);

    PFN_PROCESS_PRIMS pfnBinner;
#if USE_SIMD16_FRONTEND
    PFN_PROCESS_PRIMS_SIMD16 pfnBinner_simd16;
#endif
    switch (pState->state.topology)
    {
    case TOP_POINT_LIST:
        pState->pfnProcessPrims = ClipPoints;
        pfnBinner               = BinPoints;
#if USE_SIMD16_FRONTEND
        pState->pfnProcessPrims_simd16 = ClipPoints_simd16;
        pfnBinner_simd16               = BinPoints_simd16;
#endif
        break;
    case TOP_LINE_LIST:
    case TOP_LINE_STRIP:
    case TOP_LINE_LOOP:
    case TOP_LINE_LIST_ADJ:
    case TOP_LISTSTRIP_ADJ:
        pState->pfnProcessPrims = ClipLines;
        pfnBinner               = BinLines;
#if USE_SIMD16_FRONTEND
        pState->pfnProcessPrims_simd16 = ClipLines_simd16;
        pfnBinner_simd16               = BinLines_simd16;
#endif
        break;
    default:
        pState->pfnProcessPrims = ClipTriangles;
        pfnBinner               = GetBinTrianglesFunc((rastState.conservativeRast > 0));
#if USE_SIMD16_FRONTEND
        pState->pfnProcessPrims_simd16 = ClipTriangles_simd16;
        pfnBinner_simd16 = GetBinTrianglesFunc_simd16((rastState.conservativeRast > 0));
#endif
        break;
    };


    // Disable clipper if viewport transform is disabled or if clipper is disabled
    if (pState->state.frontendState.vpTransformDisable || !pState->state.rastState.clipEnable)
    {
        pState->pfnProcessPrims = pfnBinner;
#if USE_SIMD16_FRONTEND
        pState->pfnProcessPrims_simd16 = pfnBinner_simd16;
#endif
    }

    // Disable rasterizer and backend if no pixel, no depth/stencil, and no attributes
    if ((pState->state.psState.pfnPixelShader == nullptr) &&
        (pState->state.depthStencilState.depthTestEnable == FALSE) &&
        (pState->state.depthStencilState.depthWriteEnable == FALSE) &&
        (pState->state.depthStencilState.stencilTestEnable == FALSE) &&
        (pState->state.depthStencilState.stencilWriteEnable == FALSE) &&
        (pState->state.backendState.numAttributes == 0))
    {
        pState->pfnProcessPrims = nullptr;
#if USE_SIMD16_FRONTEND
        pState->pfnProcessPrims_simd16 = nullptr;
#endif
    }

    if (pState->state.soState.rasterizerDisable == true)
    {
        pState->pfnProcessPrims = nullptr;
#if USE_SIMD16_FRONTEND
        pState->pfnProcessPrims_simd16 = nullptr;
#endif
    }


    // set up the frontend attribute count
    pState->state.feNumAttributes         = 0;
    const SWR_BACKEND_STATE& backendState = pState->state.backendState;
    if (backendState.swizzleEnable)
    {
        // attribute swizzling is enabled, iterate over the map and record the max attribute used
        for (uint32_t i = 0; i < backendState.numAttributes; ++i)
        {
            pState->state.feNumAttributes =
                std::max(pState->state.feNumAttributes,
                         (uint32_t)backendState.swizzleMap[i].sourceAttrib + 1);
        }
    }
    else
    {
        pState->state.feNumAttributes = pState->state.backendState.numAttributes;
    }

    if (pState->state.soState.soEnable)
    {
        uint64_t streamMasks = 0;
        for (uint32_t i = 0; i < 4; ++i)
        {
            streamMasks |= pState->state.soState.streamMasks[i];
        }

        unsigned long maxAttrib;
        if (_BitScanReverse64(&maxAttrib, streamMasks))
        {
            pState->state.feNumAttributes =
                std::max(pState->state.feNumAttributes, (uint32_t)(maxAttrib + 1));
        }
    }

    // complicated logic to test for cases where we don't need backing hottile memory for a draw
    // have to check for the special case where depth/stencil test is enabled but depthwrite is
    // disabled.
    pState->state.depthHottileEnable =
        ((!(pState->state.depthStencilState.depthTestEnable &&
            !pState->state.depthStencilState.depthWriteEnable &&
            !pState->state.depthBoundsState.depthBoundsTestEnable &&
            pState->state.depthStencilState.depthTestFunc == ZFUNC_ALWAYS)) &&
         (pState->state.depthStencilState.depthTestEnable ||
          pState->state.depthStencilState.depthWriteEnable ||
          pState->state.depthBoundsState.depthBoundsTestEnable))
            ? true
            : false;

    pState->state.stencilHottileEnable =
        (((!(pState->state.depthStencilState.stencilTestEnable &&
             !pState->state.depthStencilState.stencilWriteEnable &&
             pState->state.depthStencilState.stencilTestFunc == ZFUNC_ALWAYS)) ||
          // for stencil we have to check the double sided state as well
          (!(pState->state.depthStencilState.doubleSidedStencilTestEnable &&
             !pState->state.depthStencilState.stencilWriteEnable &&
             pState->state.depthStencilState.backfaceStencilTestFunc == ZFUNC_ALWAYS))) &&
         (pState->state.depthStencilState.stencilTestEnable ||
          pState->state.depthStencilState.stencilWriteEnable))
            ? true
            : false;

    uint32_t hotTileEnable = pState->state.psState.renderTargetMask;

    // Disable hottile for surfaces with no writes
    if (psState.pfnPixelShader != nullptr)
    {
        unsigned long rt;
        uint32_t rtMask = pState->state.psState.renderTargetMask;
        while (_BitScanForward(&rt, rtMask))
        {
            rtMask &= ~(1 << rt);

            if (pState->state.blendState.renderTarget[rt].writeDisableAlpha &&
                pState->state.blendState.renderTarget[rt].writeDisableRed &&
                pState->state.blendState.renderTarget[rt].writeDisableGreen &&
                pState->state.blendState.renderTarget[rt].writeDisableBlue)
            {
                hotTileEnable &= ~(1 << rt);
            }
        }
    }

    pState->state.colorHottileEnable = hotTileEnable;

    // Setup depth quantization function
    if (pState->state.depthHottileEnable)
    {
        switch (pState->state.rastState.depthFormat)
        {
        case R32_FLOAT_X8X24_TYPELESS:
            pState->state.pfnQuantizeDepth = QuantizeDepth<R32_FLOAT_X8X24_TYPELESS>;
            break;
        case R32_FLOAT:
            pState->state.pfnQuantizeDepth = QuantizeDepth<R32_FLOAT>;
            break;
        case R24_UNORM_X8_TYPELESS:
            pState->state.pfnQuantizeDepth = QuantizeDepth<R24_UNORM_X8_TYPELESS>;
            break;
        case R16_UNORM:
            pState->state.pfnQuantizeDepth = QuantizeDepth<R16_UNORM>;
            break;
        default:
            SWR_INVALID("Unsupported depth format for depth quantization.");
            pState->state.pfnQuantizeDepth = QuantizeDepth<R32_FLOAT>;
        }
    }
    else
    {
        // set up pass-through quantize if depth isn't enabled
        pState->state.pfnQuantizeDepth = QuantizeDepth<R32_FLOAT>;
    }

    // Generate guardbands
    updateGuardbands(&pState->state);
}

//////////////////////////////////////////////////////////////////////////
/// @brief InitDraw
/// @param pDC - Draw context to initialize for this draw.
void InitDraw(DRAW_CONTEXT* pDC, bool isSplitDraw)
{
    // We don't need to re-setup the scissors/pipeline state again for split draw.
    if (isSplitDraw == false)
    {
        SetupMacroTileScissors(pDC);
        SetupPipeline(pDC);
    }

}

//////////////////////////////////////////////////////////////////////////
/// @brief We can split the draw for certain topologies for better performance.
/// @param totalVerts - Total vertices for draw
/// @param topology - Topology used for draw
uint32_t MaxVertsPerDraw(DRAW_CONTEXT* pDC, uint32_t totalVerts, PRIMITIVE_TOPOLOGY topology)
{
    API_STATE& state = pDC->pState->state;

    // We can not split draws that have streamout enabled because there is no practical way
    // to support multiple threads generating SO data for a single set of buffers.
    if (state.soState.soEnable)
    {
        return totalVerts;
    }

    // The Primitive Assembly code can only handle 1 RECT at a time. Specified with only 3 verts.
    if (topology == TOP_RECT_LIST)
    {
        return 3;
    }

    // Is split drawing disabled?
    if (KNOB_DISABLE_SPLIT_DRAW)
    {
        return totalVerts;
    }

    uint32_t vertsPerDraw = totalVerts;

    switch (topology)
    {
    case TOP_POINT_LIST:
    case TOP_TRIANGLE_LIST:
        vertsPerDraw = KNOB_MAX_PRIMS_PER_DRAW;
        break;

    case TOP_PATCHLIST_1:
    case TOP_PATCHLIST_2:
    case TOP_PATCHLIST_3:
    case TOP_PATCHLIST_4:
    case TOP_PATCHLIST_5:
    case TOP_PATCHLIST_6:
    case TOP_PATCHLIST_7:
    case TOP_PATCHLIST_8:
    case TOP_PATCHLIST_9:
    case TOP_PATCHLIST_10:
    case TOP_PATCHLIST_11:
    case TOP_PATCHLIST_12:
    case TOP_PATCHLIST_13:
    case TOP_PATCHLIST_14:
    case TOP_PATCHLIST_15:
    case TOP_PATCHLIST_16:
    case TOP_PATCHLIST_17:
    case TOP_PATCHLIST_18:
    case TOP_PATCHLIST_19:
    case TOP_PATCHLIST_20:
    case TOP_PATCHLIST_21:
    case TOP_PATCHLIST_22:
    case TOP_PATCHLIST_23:
    case TOP_PATCHLIST_24:
    case TOP_PATCHLIST_25:
    case TOP_PATCHLIST_26:
    case TOP_PATCHLIST_27:
    case TOP_PATCHLIST_28:
    case TOP_PATCHLIST_29:
    case TOP_PATCHLIST_30:
    case TOP_PATCHLIST_31:
    case TOP_PATCHLIST_32:
        if (pDC->pState->state.tsState.tsEnable)
        {
            uint32_t vertsPerPrim = topology - TOP_PATCHLIST_BASE;
            vertsPerDraw          = vertsPerPrim * KNOB_MAX_TESS_PRIMS_PER_DRAW;
        }
        break;
    default:
        // We are not splitting up draws for other topologies.
        break;
    }

    return vertsPerDraw;
}

//////////////////////////////////////////////////////////////////////////
/// @brief DrawInstanced
/// @param hContext - Handle passed back from SwrCreateContext
/// @param topology - Specifies topology for draw.
/// @param numVerts - How many vertices to read sequentially from vertex data (per instance).
/// @param startVertex - Specifies start vertex for draw. (vertex data)
/// @param numInstances - How many instances to render.
/// @param startInstance - Which instance to start sequentially fetching from in each buffer
/// (instanced data)
void DrawInstanced(HANDLE             hContext,
                   PRIMITIVE_TOPOLOGY topology,
                   uint32_t           numVertices,
                   uint32_t           startVertex,
                   uint32_t           numInstances  = 1,
                   uint32_t           startInstance = 0)
{
    if (KNOB_TOSS_DRAW)
    {
        return;
    }

    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);

    RDTSC_BEGIN(pContext->pBucketMgr, APIDraw, pDC->drawId);

    uint32_t maxVertsPerDraw = MaxVertsPerDraw(pDC, numVertices, topology);
    uint32_t primsPerDraw    = GetNumPrims(topology, maxVertsPerDraw);
    uint32_t remainingVerts  = numVertices;

    API_STATE* pState  = &pDC->pState->state;
    pState->topology   = topology;
    pState->forceFront = false;

    // disable culling for points/lines
    uint32_t oldCullMode = pState->rastState.cullMode;
    if (topology == TOP_POINT_LIST)
    {
        pState->rastState.cullMode = SWR_CULLMODE_NONE;
        pState->forceFront         = true;
    }
    else if (topology == TOP_RECT_LIST)
    {
        pState->rastState.cullMode = SWR_CULLMODE_NONE;
    }

    int draw = 0;
    while (remainingVerts)
    {
        uint32_t numVertsForDraw =
            (remainingVerts < maxVertsPerDraw) ? remainingVerts : maxVertsPerDraw;

        bool          isSplitDraw = (draw > 0) ? !KNOB_DISABLE_SPLIT_DRAW : false;
        DRAW_CONTEXT* pDC         = GetDrawContext(pContext, isSplitDraw);
        InitDraw(pDC, isSplitDraw);

        pDC->FeWork.type                    = DRAW;
        pDC->FeWork.pfnWork                 = GetProcessDrawFunc(false, // IsIndexed
                                                 false, // bEnableCutIndex
                                                 pState->tsState.tsEnable,
                                                 pState->gsState.gsEnable,
                                                 pState->soState.soEnable,
                                                 pDC->pState->pfnProcessPrims != nullptr);
        pDC->FeWork.desc.draw.numVerts      = numVertsForDraw;
        pDC->FeWork.desc.draw.startVertex   = startVertex;
        pDC->FeWork.desc.draw.numInstances  = numInstances;
        pDC->FeWork.desc.draw.startInstance = startInstance;
        pDC->FeWork.desc.draw.startPrimID   = draw * primsPerDraw;
        pDC->FeWork.desc.draw.startVertexID = draw * maxVertsPerDraw;

        pDC->cleanupState = (remainingVerts == numVertsForDraw);

        // enqueue DC
        QueueDraw(pContext);

        AR_API_EVENT(DrawInstancedEvent(pDC->drawId,
                                        topology,
                                        numVertsForDraw,
                                        startVertex,
                                        numInstances,
                                        startInstance,
                                        pState->tsState.tsEnable,
                                        pState->gsState.gsEnable,
                                        pState->soState.soEnable,
                                        pState->gsState.outputTopology,
                                        draw));

        remainingVerts -= numVertsForDraw;
        draw++;
    }

    // restore culling state
    pDC                                   = GetDrawContext(pContext);
    pDC->pState->state.rastState.cullMode = oldCullMode;

    RDTSC_END(pContext->pBucketMgr, APIDraw, numVertices * numInstances);
}

//////////////////////////////////////////////////////////////////////////
/// @brief SwrDraw
/// @param hContext - Handle passed back from SwrCreateContext
/// @param topology - Specifies topology for draw.
/// @param startVertex - Specifies start vertex in vertex buffer for draw.
/// @param primCount - Number of vertices.
void SwrDraw(HANDLE             hContext,
             PRIMITIVE_TOPOLOGY topology,
             uint32_t           startVertex,
             uint32_t           numVertices)
{
    DrawInstanced(hContext, topology, numVertices, startVertex);
}

//////////////////////////////////////////////////////////////////////////
/// @brief SwrDrawInstanced
/// @param hContext - Handle passed back from SwrCreateContext
/// @param topology - Specifies topology for draw.
/// @param numVertsPerInstance - How many vertices to read sequentially from vertex data.
/// @param numInstances - How many instances to render.
/// @param startVertex - Specifies start vertex for draw. (vertex data)
/// @param startInstance - Which instance to start sequentially fetching from in each buffer
/// (instanced data)
void SwrDrawInstanced(HANDLE             hContext,
                      PRIMITIVE_TOPOLOGY topology,
                      uint32_t           numVertsPerInstance,
                      uint32_t           numInstances,
                      uint32_t           startVertex,
                      uint32_t           startInstance)
{
    DrawInstanced(
        hContext, topology, numVertsPerInstance, startVertex, numInstances, startInstance);
}

//////////////////////////////////////////////////////////////////////////
/// @brief DrawIndexedInstanced
/// @param hContext - Handle passed back from SwrCreateContext
/// @param topology - Specifies topology for draw.
/// @param numIndices - Number of indices to read sequentially from index buffer.
/// @param indexOffset - Starting index into index buffer.
/// @param baseVertex - Vertex in vertex buffer to consider as index "0". Note value is signed.
/// @param numInstances - Number of instances to render.
/// @param startInstance - Which instance to start sequentially fetching from in each buffer
/// (instanced data)
void DrawIndexedInstance(HANDLE             hContext,
                         PRIMITIVE_TOPOLOGY topology,
                         uint32_t           numIndices,
                         uint32_t           indexOffset,
                         int32_t            baseVertex,
                         uint32_t           numInstances  = 1,
                         uint32_t           startInstance = 0)
{
    if (KNOB_TOSS_DRAW)
    {
        return;
    }

    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);
    API_STATE*    pState   = &pDC->pState->state;

    RDTSC_BEGIN(pContext->pBucketMgr, APIDrawIndexed, pDC->drawId);

    uint32_t maxIndicesPerDraw = MaxVertsPerDraw(pDC, numIndices, topology);
    uint32_t primsPerDraw      = GetNumPrims(topology, maxIndicesPerDraw);
    uint32_t remainingIndices  = numIndices;

    uint32_t indexSize = 0;
    switch (pState->indexBuffer.format)
    {
    case R32_UINT:
        indexSize = sizeof(uint32_t);
        break;
    case R16_UINT:
        indexSize = sizeof(uint16_t);
        break;
    case R8_UINT:
        indexSize = sizeof(uint8_t);
        break;
    default:
        SWR_INVALID("Invalid index buffer format: %d", pState->indexBuffer.format);
    }

    int      draw = 0;
    gfxptr_t xpIB = pState->indexBuffer.xpIndices;
    xpIB += (uint64_t)indexOffset * (uint64_t)indexSize;

    pState->topology   = topology;
    pState->forceFront = false;

    // disable culling for points/lines
    uint32_t oldCullMode = pState->rastState.cullMode;
    if (topology == TOP_POINT_LIST)
    {
        pState->rastState.cullMode = SWR_CULLMODE_NONE;
        pState->forceFront         = true;
    }
    else if (topology == TOP_RECT_LIST)
    {
        pState->rastState.cullMode = SWR_CULLMODE_NONE;
    }

    while (remainingIndices)
    {
        uint32_t numIndicesForDraw =
            (remainingIndices < maxIndicesPerDraw) ? remainingIndices : maxIndicesPerDraw;

        // When breaking up draw, we need to obtain new draw context for each iteration.
        bool isSplitDraw = (draw > 0) ? !KNOB_DISABLE_SPLIT_DRAW : false;

        pDC = GetDrawContext(pContext, isSplitDraw);
        InitDraw(pDC, isSplitDraw);

        pDC->FeWork.type                 = DRAW;
        pDC->FeWork.pfnWork              = GetProcessDrawFunc(true, // IsIndexed
                                                 pState->frontendState.bEnableCutIndex,
                                                 pState->tsState.tsEnable,
                                                 pState->gsState.gsEnable,
                                                 pState->soState.soEnable,
                                                 pDC->pState->pfnProcessPrims != nullptr);
        pDC->FeWork.desc.draw.pDC        = pDC;
        pDC->FeWork.desc.draw.numIndices = numIndicesForDraw;
        pDC->FeWork.desc.draw.xpIB       = xpIB;
        pDC->FeWork.desc.draw.type       = pDC->pState->state.indexBuffer.format;

        pDC->FeWork.desc.draw.numInstances  = numInstances;
        pDC->FeWork.desc.draw.startInstance = startInstance;
        pDC->FeWork.desc.draw.baseVertex    = baseVertex;
        pDC->FeWork.desc.draw.startPrimID   = draw * primsPerDraw;

        pDC->cleanupState = (remainingIndices == numIndicesForDraw);

        // enqueue DC
        QueueDraw(pContext);

        AR_API_EVENT(DrawIndexedInstancedEvent(pDC->drawId,
                                               topology,
                                               numIndicesForDraw,
                                               indexOffset,
                                               baseVertex,
                                               numInstances,
                                               startInstance,
                                               pState->tsState.tsEnable,
                                               pState->gsState.gsEnable,
                                               pState->soState.soEnable,
                                               pState->gsState.outputTopology,
                                               draw));

        xpIB += maxIndicesPerDraw * indexSize;
        remainingIndices -= numIndicesForDraw;
        draw++;
    }

    // Restore culling state
    pDC                                   = GetDrawContext(pContext);
    pDC->pState->state.rastState.cullMode = oldCullMode;

    RDTSC_END(pContext->pBucketMgr, APIDrawIndexed, numIndices * numInstances);
}

//////////////////////////////////////////////////////////////////////////
/// @brief DrawIndexed
/// @param hContext - Handle passed back from SwrCreateContext
/// @param topology - Specifies topology for draw.
/// @param numIndices - Number of indices to read sequentially from index buffer.
/// @param indexOffset - Starting index into index buffer.
/// @param baseVertex - Vertex in vertex buffer to consider as index "0". Note value is signed.
void SwrDrawIndexed(HANDLE             hContext,
                    PRIMITIVE_TOPOLOGY topology,
                    uint32_t           numIndices,
                    uint32_t           indexOffset,
                    int32_t            baseVertex)
{
    DrawIndexedInstance(hContext, topology, numIndices, indexOffset, baseVertex);
}

//////////////////////////////////////////////////////////////////////////
/// @brief SwrDrawIndexedInstanced
/// @param hContext - Handle passed back from SwrCreateContext
/// @param topology - Specifies topology for draw.
/// @param numIndices - Number of indices to read sequentially from index buffer.
/// @param numInstances - Number of instances to render.
/// @param indexOffset - Starting index into index buffer.
/// @param baseVertex - Vertex in vertex buffer to consider as index "0". Note value is signed.
/// @param startInstance - Which instance to start sequentially fetching from in each buffer
/// (instanced data)
void SwrDrawIndexedInstanced(HANDLE             hContext,
                             PRIMITIVE_TOPOLOGY topology,
                             uint32_t           numIndices,
                             uint32_t           numInstances,
                             uint32_t           indexOffset,
                             int32_t            baseVertex,
                             uint32_t           startInstance)
{
    DrawIndexedInstance(
        hContext, topology, numIndices, indexOffset, baseVertex, numInstances, startInstance);
}

//////////////////////////////////////////////////////////////////////////
/// @brief SwrInvalidateTiles
/// @param hContext - Handle passed back from SwrCreateContext
/// @param attachmentMask - The mask specifies which surfaces attached to the hottiles to
/// invalidate.
/// @param invalidateRect - The pixel-coordinate rectangle to invalidate.  This will be expanded to
///                         be hottile size-aligned.
void SWR_API SwrInvalidateTiles(HANDLE          hContext,
                                uint32_t        attachmentMask,
                                const SWR_RECT& invalidateRect)
{
    if (KNOB_TOSS_DRAW)
    {
        return;
    }

    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);

    pDC->FeWork.type                                       = DISCARDINVALIDATETILES;
    pDC->FeWork.pfnWork                                    = ProcessDiscardInvalidateTiles;
    pDC->FeWork.desc.discardInvalidateTiles.attachmentMask = attachmentMask;
    pDC->FeWork.desc.discardInvalidateTiles.rect           = invalidateRect;
    pDC->FeWork.desc.discardInvalidateTiles.rect &= g_MaxScissorRect;
    pDC->FeWork.desc.discardInvalidateTiles.newTileState   = SWR_TILE_INVALID;
    pDC->FeWork.desc.discardInvalidateTiles.createNewTiles = false;
    pDC->FeWork.desc.discardInvalidateTiles.fullTilesOnly  = false;

    // enqueue
    QueueDraw(pContext);

    AR_API_EVENT(SwrInvalidateTilesEvent(pDC->drawId));
}

//////////////////////////////////////////////////////////////////////////
/// @brief SwrDiscardRect
/// @param hContext - Handle passed back from SwrCreateContext
/// @param attachmentMask - The mask specifies which surfaces attached to the hottiles to discard.
/// @param rect - The pixel-coordinate rectangle to discard.  Only fully-covered hottiles will be
///               discarded.
void SWR_API SwrDiscardRect(HANDLE hContext, uint32_t attachmentMask, const SWR_RECT& rect)
{
    if (KNOB_TOSS_DRAW)
    {
        return;
    }

    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);

    // Queue a load to the hottile
    pDC->FeWork.type                                       = DISCARDINVALIDATETILES;
    pDC->FeWork.pfnWork                                    = ProcessDiscardInvalidateTiles;
    pDC->FeWork.desc.discardInvalidateTiles.attachmentMask = attachmentMask;
    pDC->FeWork.desc.discardInvalidateTiles.rect           = rect;
    pDC->FeWork.desc.discardInvalidateTiles.rect &= g_MaxScissorRect;
    pDC->FeWork.desc.discardInvalidateTiles.newTileState   = SWR_TILE_RESOLVED;
    pDC->FeWork.desc.discardInvalidateTiles.createNewTiles = true;
    pDC->FeWork.desc.discardInvalidateTiles.fullTilesOnly  = true;

    // enqueue
    QueueDraw(pContext);

    AR_API_EVENT(SwrDiscardRectEvent(pDC->drawId));
}

//////////////////////////////////////////////////////////////////////////
/// @brief SwrDispatch
/// @param hContext - Handle passed back from SwrCreateContext
/// @param threadGroupCountX - Number of thread groups dispatched in X direction
/// @param threadGroupCountY - Number of thread groups dispatched in Y direction
/// @param threadGroupCountZ - Number of thread groups dispatched in Z direction
void SwrDispatch(HANDLE   hContext,
                 uint32_t threadGroupCountX,
                 uint32_t threadGroupCountY,
                 uint32_t threadGroupCountZ

)
{
    if (KNOB_TOSS_DRAW)
    {
        return;
    }

    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);

    RDTSC_BEGIN(pContext->pBucketMgr, APIDispatch, pDC->drawId);
    AR_API_EVENT(
        DispatchEvent(pDC->drawId, threadGroupCountX, threadGroupCountY, threadGroupCountZ));
    pDC->isCompute = true; // This is a compute context.

    COMPUTE_DESC* pTaskData = (COMPUTE_DESC*)pDC->pArena->AllocAligned(sizeof(COMPUTE_DESC), 64);

    pTaskData->threadGroupCountX = threadGroupCountX;
    pTaskData->threadGroupCountY = threadGroupCountY;
    pTaskData->threadGroupCountZ = threadGroupCountZ;

    pTaskData->enableThreadDispatch = false;

    uint32_t totalThreadGroups = threadGroupCountX * threadGroupCountY * threadGroupCountZ;
    uint32_t dcIndex           = pDC->drawId % pContext->MAX_DRAWS_IN_FLIGHT;
    pDC->pDispatch             = &pContext->pDispatchQueueArray[dcIndex];
    pDC->pDispatch->initialize(totalThreadGroups, pTaskData, &ProcessComputeBE);

    QueueDispatch(pContext);
    RDTSC_END(pContext->pBucketMgr,
              APIDispatch,
              threadGroupCountX * threadGroupCountY * threadGroupCountZ);
}

// Deswizzles, converts and stores current contents of the hot tiles to surface
// described by pState
void SWR_API SwrStoreTiles(HANDLE          hContext,
                           uint32_t        attachmentMask,
                           SWR_TILE_STATE  postStoreTileState,
                           const SWR_RECT& storeRect)
{
    if (KNOB_TOSS_DRAW)
    {
        return;
    }

    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);

    RDTSC_BEGIN(pContext->pBucketMgr, APIStoreTiles, pDC->drawId);

    pDC->FeWork.type                               = STORETILES;
    pDC->FeWork.pfnWork                            = ProcessStoreTiles;
    pDC->FeWork.desc.storeTiles.attachmentMask     = attachmentMask;
    pDC->FeWork.desc.storeTiles.postStoreTileState = postStoreTileState;
    pDC->FeWork.desc.storeTiles.rect               = storeRect;
    pDC->FeWork.desc.storeTiles.rect &= g_MaxScissorRect;

    // enqueue
    QueueDraw(pContext);

    AR_API_EVENT(SwrStoreTilesEvent(pDC->drawId));

    RDTSC_END(pContext->pBucketMgr, APIStoreTiles, 1);
}

//////////////////////////////////////////////////////////////////////////
/// @brief SwrClearRenderTarget - Clear attached render targets / depth / stencil
/// @param hContext - Handle passed back from SwrCreateContext
/// @param attachmentMask - combination of SWR_ATTACHMENT_*_BIT attachments to clear
/// @param renderTargetArrayIndex - the RT array index to clear
/// @param clearColor - color use for clearing render targets
/// @param z - depth value use for clearing depth buffer
/// @param stencil - stencil value used for clearing stencil buffer
/// @param clearRect - The pixel-coordinate rectangle to clear in all cleared buffers
void SWR_API SwrClearRenderTarget(HANDLE          hContext,
                                  uint32_t        attachmentMask,
                                  uint32_t        renderTargetArrayIndex,
                                  const float     clearColor[4],
                                  float           z,
                                  uint8_t         stencil,
                                  const SWR_RECT& clearRect)
{
    if (KNOB_TOSS_DRAW)
    {
        return;
    }

    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);

    RDTSC_BEGIN(pContext->pBucketMgr, APIClearRenderTarget, pDC->drawId);

    pDC->FeWork.type            = CLEAR;
    pDC->FeWork.pfnWork         = ProcessClear;
    pDC->FeWork.desc.clear.rect = clearRect;
    pDC->FeWork.desc.clear.rect &= g_MaxScissorRect;
    pDC->FeWork.desc.clear.attachmentMask         = attachmentMask;
    pDC->FeWork.desc.clear.renderTargetArrayIndex = renderTargetArrayIndex;
    pDC->FeWork.desc.clear.clearDepth             = z;
    pDC->FeWork.desc.clear.clearRTColor[0]        = clearColor[0];
    pDC->FeWork.desc.clear.clearRTColor[1]        = clearColor[1];
    pDC->FeWork.desc.clear.clearRTColor[2]        = clearColor[2];
    pDC->FeWork.desc.clear.clearRTColor[3]        = clearColor[3];
    pDC->FeWork.desc.clear.clearStencil           = stencil;

    // enqueue draw
    QueueDraw(pContext);

    RDTSC_END(pContext->pBucketMgr, APIClearRenderTarget, 1);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Returns a pointer to the private context state for the current
///        draw operation. This is used for external componets such as the
///        sampler.
///        SWR is responsible for the allocation of the private context state.
/// @param hContext - Handle passed back from SwrCreateContext
VOID* SwrGetPrivateContextState(HANDLE hContext)
{
    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);
    DRAW_STATE*   pState   = pDC->pState;

    if (pState->pPrivateState == nullptr)
    {
        pState->pPrivateState = pState->pArena->AllocAligned(pContext->privateStateSize,
                                                             KNOB_SIMD_WIDTH * sizeof(float));
    }

    return pState->pPrivateState;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Clients can use this to allocate memory for draw/dispatch
///        operations. The memory will automatically be freed once operation
///        has completed. Client can use this to allocate binding tables,
///        etc. needed for shader execution.
/// @param hContext - Handle passed back from SwrCreateContext
/// @param size - Size of allocation
/// @param align - Alignment needed for allocation.
VOID* SwrAllocDrawContextMemory(HANDLE hContext, uint32_t size, uint32_t align)
{
    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);

    return pDC->pState->pArena->AllocAligned(size, align);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Enables stats counting
/// @param hContext - Handle passed back from SwrCreateContext
/// @param enable - If true then counts are incremented.
void SwrEnableStatsFE(HANDLE hContext, bool enable)
{
    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);

    pDC->pState->state.enableStatsFE = enable;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Enables stats counting
/// @param hContext - Handle passed back from SwrCreateContext
/// @param enable - If true then counts are incremented.
void SwrEnableStatsBE(HANDLE hContext, bool enable)
{
    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);

    pDC->pState->state.enableStatsBE = enable;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Mark end of frame - used for performance profiling
/// @param hContext - Handle passed back from SwrCreateContext
void SWR_API SwrEndFrame(HANDLE hContext)
{
    SWR_CONTEXT*  pContext = GetContext(hContext);
    DRAW_CONTEXT* pDC      = GetDrawContext(pContext);
    (void)pDC; // var used

    RDTSC_ENDFRAME(pContext->pBucketMgr);
    AR_API_EVENT(FrameEndEvent(pContext->frameCount, pDC->drawId));

    pContext->frameCount++;
}

void InitSimLoadTilesTable();
void InitSimStoreTilesTable();
void InitSimClearTilesTable();

void InitClearTilesTable();
void InitBackendFuncTables();

//////////////////////////////////////////////////////////////////////////
/// @brief Initialize swr backend and memory internal tables
void SwrInit()
{
    InitClearTilesTable();
    InitBackendFuncTables();
    InitRasterizerFunctions();
}

void SwrGetInterface(SWR_INTERFACE& out_funcs)
{
    out_funcs.pfnSwrCreateContext          = SwrCreateContext;
    out_funcs.pfnSwrDestroyContext         = SwrDestroyContext;
    out_funcs.pfnSwrBindApiThread          = SwrBindApiThread;
    out_funcs.pfnSwrSaveState              = SwrSaveState;
    out_funcs.pfnSwrRestoreState           = SwrRestoreState;
    out_funcs.pfnSwrSync                   = SwrSync;
    out_funcs.pfnSwrStallBE                = SwrStallBE;
    out_funcs.pfnSwrWaitForIdle            = SwrWaitForIdle;
    out_funcs.pfnSwrWaitForIdleFE          = SwrWaitForIdleFE;
    out_funcs.pfnSwrSetVertexBuffers       = SwrSetVertexBuffers;
    out_funcs.pfnSwrSetIndexBuffer         = SwrSetIndexBuffer;
    out_funcs.pfnSwrSetFetchFunc           = SwrSetFetchFunc;
    out_funcs.pfnSwrSetSoFunc              = SwrSetSoFunc;
    out_funcs.pfnSwrSetSoState             = SwrSetSoState;
    out_funcs.pfnSwrSetSoBuffers           = SwrSetSoBuffers;
    out_funcs.pfnSwrSetVertexFunc          = SwrSetVertexFunc;
    out_funcs.pfnSwrSetFrontendState       = SwrSetFrontendState;
    out_funcs.pfnSwrSetGsState             = SwrSetGsState;
    out_funcs.pfnSwrSetGsFunc              = SwrSetGsFunc;
    out_funcs.pfnSwrSetCsFunc              = SwrSetCsFunc;
    out_funcs.pfnSwrSetTsState             = SwrSetTsState;
    out_funcs.pfnSwrSetHsFunc              = SwrSetHsFunc;
    out_funcs.pfnSwrSetDsFunc              = SwrSetDsFunc;
    out_funcs.pfnSwrSetDepthStencilState   = SwrSetDepthStencilState;
    out_funcs.pfnSwrSetBackendState        = SwrSetBackendState;
    out_funcs.pfnSwrSetDepthBoundsState    = SwrSetDepthBoundsState;
    out_funcs.pfnSwrSetPixelShaderState    = SwrSetPixelShaderState;
    out_funcs.pfnSwrSetBlendState          = SwrSetBlendState;
    out_funcs.pfnSwrSetBlendFunc           = SwrSetBlendFunc;
    out_funcs.pfnSwrDraw                   = SwrDraw;
    out_funcs.pfnSwrDrawInstanced          = SwrDrawInstanced;
    out_funcs.pfnSwrDrawIndexed            = SwrDrawIndexed;
    out_funcs.pfnSwrDrawIndexedInstanced   = SwrDrawIndexedInstanced;
    out_funcs.pfnSwrInvalidateTiles        = SwrInvalidateTiles;
    out_funcs.pfnSwrDiscardRect            = SwrDiscardRect;
    out_funcs.pfnSwrDispatch               = SwrDispatch;
    out_funcs.pfnSwrStoreTiles             = SwrStoreTiles;
    out_funcs.pfnSwrClearRenderTarget      = SwrClearRenderTarget;
    out_funcs.pfnSwrSetRastState           = SwrSetRastState;
    out_funcs.pfnSwrSetViewports           = SwrSetViewports;
    out_funcs.pfnSwrSetScissorRects        = SwrSetScissorRects;
    out_funcs.pfnSwrGetPrivateContextState = SwrGetPrivateContextState;
    out_funcs.pfnSwrAllocDrawContextMemory = SwrAllocDrawContextMemory;
    out_funcs.pfnSwrEnableStatsFE          = SwrEnableStatsFE;
    out_funcs.pfnSwrEnableStatsBE          = SwrEnableStatsBE;
    out_funcs.pfnSwrEndFrame               = SwrEndFrame;
    out_funcs.pfnSwrInit                   = SwrInit;
}
