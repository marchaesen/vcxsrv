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
 ****************************************************************************/

#include <stdio.h>
#include <thread>
#include <algorithm>
#include <float.h>
#include <vector>
#include <utility>
#include <fstream>
#include <string>

#if defined(__linux__) || defined(__gnu_linux__) || defined(__APPLE__)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include "common/os.h"
#include "core/api.h"
#include "context.h"
#include "frontend.h"
#include "backend.h"
#include "rasterizer.h"
#include "rdtsc_core.h"
#include "tilemgr.h"
#include "tileset.h"


// ThreadId
struct Core
{
    uint32_t              procGroup = 0;
    std::vector<uint32_t> threadIds;
};

struct NumaNode
{
    uint32_t          numaId;
    std::vector<Core> cores;
};

typedef std::vector<NumaNode> CPUNumaNodes;

void CalculateProcessorTopology(CPUNumaNodes& out_nodes, uint32_t& out_numThreadsPerProcGroup)
{
    out_nodes.clear();
    out_numThreadsPerProcGroup = 0;

#if defined(_WIN32)

    std::vector<KAFFINITY> threadMaskPerProcGroup;

    static std::mutex           m;
    std::lock_guard<std::mutex> l(m);

    DWORD bufSize = 0;

    BOOL ret = GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bufSize);
    SWR_ASSERT(ret == FALSE && GetLastError() == ERROR_INSUFFICIENT_BUFFER);

    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pBufferMem =
        (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)malloc(bufSize);
    SWR_ASSERT(pBufferMem);

    ret = GetLogicalProcessorInformationEx(RelationProcessorCore, pBufferMem, &bufSize);
    SWR_ASSERT(ret != FALSE, "Failed to get Processor Topology Information");

    uint32_t                                 count   = bufSize / pBufferMem->Size;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pBuffer = pBufferMem;

    for (uint32_t i = 0; i < count; ++i)
    {
        SWR_ASSERT(pBuffer->Relationship == RelationProcessorCore);
        for (uint32_t g = 0; g < pBuffer->Processor.GroupCount; ++g)
        {
            auto&    gmask     = pBuffer->Processor.GroupMask[g];
            uint32_t threadId  = 0;
            uint32_t procGroup = gmask.Group;

            Core* pCore = nullptr;

            while (BitScanForwardSizeT((unsigned long*)&threadId, gmask.Mask))
            {
                // clear mask
                KAFFINITY threadMask = KAFFINITY(1) << threadId;
                gmask.Mask &= ~threadMask;

                if (procGroup >= threadMaskPerProcGroup.size())
                {
                    threadMaskPerProcGroup.resize(procGroup + 1);
                }

                if (threadMaskPerProcGroup[procGroup] & threadMask)
                {
                    // Already seen this mask.  This means that we are in 32-bit mode and
                    // have seen more than 32 HW threads for this procGroup
                    // Don't use it
#if defined(_WIN64)
                    SWR_INVALID("Shouldn't get here in 64-bit mode");
#endif
                    continue;
                }

                threadMaskPerProcGroup[procGroup] |= (KAFFINITY(1) << threadId);

                // Find Numa Node
                uint32_t         numaId  = 0;
                PROCESSOR_NUMBER procNum = {};
                procNum.Group            = WORD(procGroup);
                procNum.Number           = UCHAR(threadId);

                ret = GetNumaProcessorNodeEx(&procNum, (PUSHORT)&numaId);
                SWR_ASSERT(ret);

                // Store data
                if (out_nodes.size() <= numaId)
                {
                    out_nodes.resize(numaId + 1);
                }
                auto& numaNode  = out_nodes[numaId];
                numaNode.numaId = numaId;

                if (nullptr == pCore)
                {
                    numaNode.cores.push_back(Core());
                    pCore            = &numaNode.cores.back();
                    pCore->procGroup = procGroup;
                }
                pCore->threadIds.push_back(threadId);
                if (procGroup == 0)
                {
                    out_numThreadsPerProcGroup++;
                }
            }
        }
        pBuffer = PtrAdd(pBuffer, pBuffer->Size);
    }

    free(pBufferMem);

#elif defined(__linux__) || defined(__gnu_linux__)

    // Parse /proc/cpuinfo to get full topology
    std::ifstream input("/proc/cpuinfo");
    std::string   line;
    char*         c;
    uint32_t      procId = uint32_t(-1);
    uint32_t      coreId = uint32_t(-1);
    uint32_t      physId = uint32_t(-1);

    while (std::getline(input, line))
    {
        if (line.find("processor") != std::string::npos)
        {
            auto data_start = line.find(": ") + 2;
            procId          = std::strtoul(&line.c_str()[data_start], &c, 10);
            continue;
        }
        if (line.find("core id") != std::string::npos)
        {
            auto data_start = line.find(": ") + 2;
            coreId          = std::strtoul(&line.c_str()[data_start], &c, 10);
            continue;
        }
        if (line.find("physical id") != std::string::npos)
        {
            auto data_start = line.find(": ") + 2;
            physId          = std::strtoul(&line.c_str()[data_start], &c, 10);
            continue;
        }
        if (line.length() == 0)
        {
            if (physId + 1 > out_nodes.size())
                out_nodes.resize(physId + 1);
            auto& numaNode  = out_nodes[physId];
            numaNode.numaId = physId;

            if (coreId + 1 > numaNode.cores.size())
                numaNode.cores.resize(coreId + 1);
            auto& core     = numaNode.cores[coreId];
            core.procGroup = coreId;
            core.threadIds.push_back(procId);
        }
    }

    out_numThreadsPerProcGroup = 0;
    for (auto& node : out_nodes)
    {
        for (auto& core : node.cores)
        {
            out_numThreadsPerProcGroup += core.threadIds.size();
        }
    }

#elif defined(__APPLE__)

    auto numProcessors  = 0;
    auto numCores       = 0;
    auto numPhysicalIds = 0;

    int    value;
    size_t size = sizeof(value);

    int result = sysctlbyname("hw.packages", &value, &size, NULL, 0);
    SWR_ASSERT(result == 0);
    numPhysicalIds = value;

    result = sysctlbyname("hw.logicalcpu", &value, &size, NULL, 0);
    SWR_ASSERT(result == 0);
    numProcessors = value;

    result = sysctlbyname("hw.physicalcpu", &value, &size, NULL, 0);
    SWR_ASSERT(result == 0);
    numCores = value;

    out_nodes.resize(numPhysicalIds);

    for (auto physId = 0; physId < numPhysicalIds; ++physId)
    {
        auto& numaNode = out_nodes[physId];
        auto  procId   = 0;

        numaNode.cores.resize(numCores);

        while (procId < numProcessors)
        {
            for (auto coreId = 0; coreId < numaNode.cores.size(); ++coreId, ++procId)
            {
                auto& core = numaNode.cores[coreId];

                core.procGroup = coreId;
                core.threadIds.push_back(procId);
            }
        }
    }

    out_numThreadsPerProcGroup = 0;

    for (auto& node : out_nodes)
    {
        for (auto& core : node.cores)
        {
            out_numThreadsPerProcGroup += core.threadIds.size();
        }
    }

#else

#error Unsupported platform

#endif

    // Prune empty cores and numa nodes
    for (auto node_it = out_nodes.begin(); node_it != out_nodes.end();)
    {
        // Erase empty cores (first)
        for (auto core_it = node_it->cores.begin(); core_it != node_it->cores.end();)
        {
            if (core_it->threadIds.size() == 0)
            {
                core_it = node_it->cores.erase(core_it);
            }
            else
            {
                ++core_it;
            }
        }

        // Erase empty numa nodes (second)
        if (node_it->cores.size() == 0)
        {
            node_it = out_nodes.erase(node_it);
        }
        else
        {
            ++node_it;
        }
    }
}

void bindThread(SWR_CONTEXT* pContext,
                uint32_t     threadId,
                uint32_t     procGroupId   = 0,
                bool         bindProcGroup = false)
{
    // Only bind threads when MAX_WORKER_THREADS isn't set.
    if (pContext->threadInfo.SINGLE_THREADED ||
        (pContext->threadInfo.MAX_WORKER_THREADS && bindProcGroup == false))
    {
        return;
    }

#if defined(_WIN32)

    GROUP_AFFINITY affinity = {};
    affinity.Group          = procGroupId;

#if !defined(_WIN64)
    if (threadId >= 32)
    {
        // Hopefully we don't get here.  Logic in CreateThreadPool should prevent this.
        SWR_INVALID("Shouldn't get here");

        // In a 32-bit process on Windows it is impossible to bind
        // to logical processors 32-63 within a processor group.
        // In this case set the mask to 0 and let the system assign
        // the processor.  Hopefully it will make smart choices.
        affinity.Mask = 0;
    }
    else
#endif
    {
        // If MAX_WORKER_THREADS is set, only bind to the proc group,
        // Not the individual HW thread.
        if (!bindProcGroup && !pContext->threadInfo.MAX_WORKER_THREADS)
        {
            affinity.Mask = KAFFINITY(1) << threadId;
        }
        else
        {
            affinity.Mask = KAFFINITY(0);
        }
    }

    if (!SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr))
    {
        SWR_INVALID("Failed to set Thread Affinity");
    }

#elif defined(__linux__) || defined(__gnu_linux__)

    cpu_set_t cpuset;
    pthread_t thread = pthread_self();
    CPU_ZERO(&cpuset);
    CPU_SET(threadId, &cpuset);

    int err = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (err != 0)
    {
        fprintf(stderr, "pthread_setaffinity_np failure for tid %u: %s\n", threadId, strerror(err));
    }

#endif
}

INLINE
uint32_t GetEnqueuedDraw(SWR_CONTEXT* pContext)
{
    return pContext->dcRing.GetHead();
}

INLINE
DRAW_CONTEXT* GetDC(SWR_CONTEXT* pContext, uint32_t drawId)
{
    return &pContext->dcRing[(drawId - 1) % pContext->MAX_DRAWS_IN_FLIGHT];
}

INLINE
bool IDComparesLess(uint32_t a, uint32_t b)
{
    // Use signed delta to ensure that wrap-around to 0 is correctly handled.
    int32_t delta = int32_t(a - b);
    return (delta < 0);
}

// returns true if dependency not met
INLINE
bool CheckDependency(SWR_CONTEXT* pContext, DRAW_CONTEXT* pDC, uint32_t lastRetiredDraw)
{
    return pDC->dependent && IDComparesLess(lastRetiredDraw, pDC->drawId - 1);
}

bool CheckDependencyFE(SWR_CONTEXT* pContext, DRAW_CONTEXT* pDC, uint32_t lastRetiredDraw)
{
    return pDC->dependentFE && IDComparesLess(lastRetiredDraw, pDC->drawId - 1);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Update client stats.
INLINE void UpdateClientStats(SWR_CONTEXT* pContext, uint32_t workerId, DRAW_CONTEXT* pDC)
{
    if ((pContext->pfnUpdateStats == nullptr) || (GetApiState(pDC).enableStatsBE == false))
    {
        return;
    }

    DRAW_DYNAMIC_STATE& dynState = pDC->dynState;
    OSALIGNLINE(SWR_STATS) stats{0};

    // Sum up stats across all workers before sending to client.
    for (uint32_t i = 0; i < pContext->NumWorkerThreads; ++i)
    {
        stats.DepthPassCount += dynState.pStats[i].DepthPassCount;
        stats.PsInvocations += dynState.pStats[i].PsInvocations;
        stats.CsInvocations += dynState.pStats[i].CsInvocations;

    }


    pContext->pfnUpdateStats(GetPrivateState(pDC), &stats);
}

INLINE void ExecuteCallbacks(SWR_CONTEXT* pContext, uint32_t workerId, DRAW_CONTEXT* pDC)
{
    UpdateClientStats(pContext, workerId, pDC);

    if (pDC->retireCallback.pfnCallbackFunc)
    {
        pDC->retireCallback.pfnCallbackFunc(pDC->retireCallback.userData,
                                            pDC->retireCallback.userData2,
                                            pDC->retireCallback.userData3);

        // Callbacks to external code *could* change floating point control state
        // Reset our optimal flags
        SetOptimalVectorCSR();
    }
}

// inlined-only version
INLINE int32_t CompleteDrawContextInl(SWR_CONTEXT* pContext, uint32_t workerId, DRAW_CONTEXT* pDC)
{
    int32_t result = static_cast<int32_t>(InterlockedDecrement(&pDC->threadsDone));
    SWR_ASSERT(result >= 0);

    AR_FLUSH(pDC->drawId);

    if (result == 0)
    {
        ExecuteCallbacks(pContext, workerId, pDC);


        // Cleanup memory allocations
        pDC->pArena->Reset(true);
        if (!pDC->isCompute)
        {
            pDC->pTileMgr->initialize();
        }
        if (pDC->cleanupState)
        {
            pDC->pState->pArena->Reset(true);
        }

        _ReadWriteBarrier();

        pContext->dcRing.Dequeue(); // Remove from tail
    }

    return result;
}

// available to other translation modules
int32_t CompleteDrawContext(SWR_CONTEXT* pContext, DRAW_CONTEXT* pDC)
{
    return CompleteDrawContextInl(pContext, 0, pDC);
}

INLINE bool FindFirstIncompleteDraw(SWR_CONTEXT* pContext,
                                    uint32_t     workerId,
                                    uint32_t&    curDrawBE,
                                    uint32_t&    drawEnqueued)
{
    // increment our current draw id to the first incomplete draw
    drawEnqueued = GetEnqueuedDraw(pContext);
    while (IDComparesLess(curDrawBE, drawEnqueued))
    {
        DRAW_CONTEXT* pDC = &pContext->dcRing[curDrawBE % pContext->MAX_DRAWS_IN_FLIGHT];

        // If its not compute and FE is not done then break out of loop.
        if (!pDC->doneFE && !pDC->isCompute)
            break;

        bool isWorkComplete =
            pDC->isCompute ? pDC->pDispatch->isWorkComplete() : pDC->pTileMgr->isWorkComplete();

        if (isWorkComplete)
        {
            curDrawBE++;
            CompleteDrawContextInl(pContext, workerId, pDC);
        }
        else
        {
            break;
        }
    }

    // If there are no more incomplete draws then return false.
    return IDComparesLess(curDrawBE, drawEnqueued);
}

//////////////////////////////////////////////////////////////////////////
/// @brief If there is any BE work then go work on it.
/// @param pContext - pointer to SWR context.
/// @param workerId - The unique worker ID that is assigned to this thread.
/// @param curDrawBE - This tracks the draw contexts that this thread has processed. Each worker
/// thread
///                    has its own curDrawBE counter and this ensures that each worker processes all
///                    the draws in order.
/// @param lockedTiles - This is the set of tiles locked by other threads. Each thread maintains its
///                      own set and each time it fails to lock a macrotile, because its already
///                      locked, then it will add that tile to the lockedTiles set. As a worker
///                      begins to work on future draws the lockedTiles ensure that it doesn't work
///                      on tiles that may still have work pending in a previous draw. Additionally,
///                      the lockedTiles is heuristic that can steer a worker back to the same
///                      macrotile that it had been working on in a previous draw.
/// @returns        true if worker thread should shutdown
bool WorkOnFifoBE(SWR_CONTEXT* pContext,
                  uint32_t     workerId,
                  uint32_t&    curDrawBE,
                  TileSet&     lockedTiles,
                  uint32_t     numaNode,
                  uint32_t     numaMask)
{
    bool bShutdown = false;

    // Find the first incomplete draw that has pending work. If no such draw is found then
    // return. FindFirstIncompleteDraw is responsible for incrementing the curDrawBE.
    uint32_t drawEnqueued = 0;
    if (FindFirstIncompleteDraw(pContext, workerId, curDrawBE, drawEnqueued) == false)
    {
        return false;
    }

    uint32_t lastRetiredDraw =
        pContext->dcRing[curDrawBE % pContext->MAX_DRAWS_IN_FLIGHT].drawId - 1;

    // Reset our history for locked tiles. We'll have to re-learn which tiles are locked.
    lockedTiles.clear();

    // Try to work on each draw in order of the available draws in flight.
    //   1. If we're on curDrawBE, we can work on any macrotile that is available.
    //   2. If we're trying to work on draws after curDrawBE, we are restricted to
    //      working on those macrotiles that are known to be complete in the prior draw to
    //      maintain order. The locked tiles provides the history to ensures this.
    for (uint32_t i = curDrawBE; IDComparesLess(i, drawEnqueued); ++i)
    {
        DRAW_CONTEXT* pDC = &pContext->dcRing[i % pContext->MAX_DRAWS_IN_FLIGHT];

        if (pDC->isCompute)
            return false; // We don't look at compute work.

        // First wait for FE to be finished with this draw. This keeps threading model simple
        // but if there are lots of bubbles between draws then serializing FE and BE may
        // need to be revisited.
        if (!pDC->doneFE)
            return false;

        // If this draw is dependent on a previous draw then we need to bail.
        if (CheckDependency(pContext, pDC, lastRetiredDraw))
        {
            return false;
        }

        // Grab the list of all dirty macrotiles. A tile is dirty if it has work queued to it.
        auto& macroTiles = pDC->pTileMgr->getDirtyTiles();

        for (auto tile : macroTiles)
        {
            uint32_t tileID = tile->mId;

            // Only work on tiles for this numa node
            uint32_t x, y;
            pDC->pTileMgr->getTileIndices(tileID, x, y);
            if (((x ^ y) & numaMask) != numaNode)
            {
                _mm_pause();
                continue;
            }

            if (!tile->getNumQueued())
            {
                _mm_pause();
                continue;
            }

            // can only work on this draw if it's not in use by other threads
            if (lockedTiles.get(tileID))
            {
                _mm_pause();
                continue;
            }

            if (tile->tryLock())
            {
                BE_WORK* pWork;

                RDTSC_BEGIN(pContext->pBucketMgr, WorkerFoundWork, pDC->drawId);

                uint32_t numWorkItems = tile->getNumQueued();
                SWR_ASSERT(numWorkItems);

                pWork = tile->peek();
                SWR_ASSERT(pWork);
                if (pWork->type == DRAW)
                {
                    pContext->pHotTileMgr->InitializeHotTiles(pContext, pDC, workerId, tileID);
                }
                else if (pWork->type == SHUTDOWN)
                {
                    bShutdown = true;
                }

                while ((pWork = tile->peek()) != nullptr)
                {
                    pWork->pfnWork(pDC, workerId, tileID, &pWork->desc);
                    tile->dequeue();
                }
                RDTSC_END(pContext->pBucketMgr, WorkerFoundWork, numWorkItems);

                _ReadWriteBarrier();

                pDC->pTileMgr->markTileComplete(tileID);

                // Optimization: If the draw is complete and we're the last one to have worked on it
                // then we can reset the locked list as we know that all previous draws before the
                // next are guaranteed to be complete.
                if ((curDrawBE == i) && (bShutdown || pDC->pTileMgr->isWorkComplete()))
                {
                    // We can increment the current BE and safely move to next draw since we know
                    // this draw is complete.
                    curDrawBE++;
                    CompleteDrawContextInl(pContext, workerId, pDC);

                    lastRetiredDraw++;

                    lockedTiles.clear();
                    break;
                }

                if (bShutdown)
                {
                    break;
                }
            }
            else
            {
                // This tile is already locked. So let's add it to our locked tiles set. This way we
                // don't try locking this one again.
                lockedTiles.set(tileID);
                _mm_pause();
            }
        }
    }

    return bShutdown;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Called when FE work is complete for this DC.
INLINE void CompleteDrawFE(SWR_CONTEXT* pContext, uint32_t workerId, DRAW_CONTEXT* pDC)
{
    if (pContext->pfnUpdateStatsFE && GetApiState(pDC).enableStatsFE)
    {
        SWR_STATS_FE& stats = pDC->dynState.statsFE;

        AR_EVENT(FrontendStatsEvent(pDC->drawId,
                                    stats.IaVertices,
                                    stats.IaPrimitives,
                                    stats.VsInvocations,
                                    stats.HsInvocations,
                                    stats.DsInvocations,
                                    stats.GsInvocations,
                                    stats.GsPrimitives,
                                    stats.CInvocations,
                                    stats.CPrimitives,
                                    stats.SoPrimStorageNeeded[0],
                                    stats.SoPrimStorageNeeded[1],
                                    stats.SoPrimStorageNeeded[2],
                                    stats.SoPrimStorageNeeded[3],
                                    stats.SoNumPrimsWritten[0],
                                    stats.SoNumPrimsWritten[1],
                                    stats.SoNumPrimsWritten[2],
                                    stats.SoNumPrimsWritten[3]));
        AR_EVENT(FrontendDrawEndEvent(pDC->drawId));

        pContext->pfnUpdateStatsFE(GetPrivateState(pDC), &stats);
    }

    if (pContext->pfnUpdateSoWriteOffset)
    {
        for (uint32_t i = 0; i < MAX_SO_BUFFERS; ++i)
        {
            if ((pDC->dynState.SoWriteOffsetDirty[i]) &&
                (pDC->pState->state.soBuffer[i].soWriteEnable))
            {
                pContext->pfnUpdateSoWriteOffset(
                    GetPrivateState(pDC), i, pDC->dynState.SoWriteOffset[i]);
            }
        }
    }

    if (pContext->pfnUpdateStreamOut)
        pContext->pfnUpdateStreamOut(GetPrivateState(pDC),  pDC->dynState.soPrims);

    // Ensure all streaming writes are globally visible before marking this FE done
    _mm_mfence();
    pDC->doneFE = true;

    InterlockedDecrement(&pContext->drawsOutstandingFE);
}

void WorkOnFifoFE(SWR_CONTEXT* pContext, uint32_t workerId, uint32_t& curDrawFE)
{
    // Try to grab the next DC from the ring
    uint32_t drawEnqueued = GetEnqueuedDraw(pContext);
    while (IDComparesLess(curDrawFE, drawEnqueued))
    {
        uint32_t      dcSlot = curDrawFE % pContext->MAX_DRAWS_IN_FLIGHT;
        DRAW_CONTEXT* pDC    = &pContext->dcRing[dcSlot];
        if (pDC->isCompute || pDC->doneFE)
        {
            CompleteDrawContextInl(pContext, workerId, pDC);
            curDrawFE++;
        }
        else
        {
            break;
        }
    }

    uint32_t lastRetiredFE = curDrawFE - 1;
    uint32_t curDraw       = curDrawFE;
    while (IDComparesLess(curDraw, drawEnqueued))
    {
        uint32_t      dcSlot = curDraw % pContext->MAX_DRAWS_IN_FLIGHT;
        DRAW_CONTEXT* pDC    = &pContext->dcRing[dcSlot];

        if (!pDC->FeLock && !pDC->isCompute)
        {
            if (CheckDependencyFE(pContext, pDC, lastRetiredFE))
            {
                return;
            }

            uint32_t initial = InterlockedCompareExchange((volatile uint32_t*)&pDC->FeLock, 1, 0);
            if (initial == 0)
            {
                // successfully grabbed the DC, now run the FE
                pDC->FeWork.pfnWork(pContext, pDC, workerId, &pDC->FeWork.desc);

                CompleteDrawFE(pContext, workerId, pDC);
            }
            else
            {
                _mm_pause();
            }
        }
        else
        {
            _mm_pause();
        }

        curDraw++;
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief If there is any compute work then go work on it.
/// @param pContext - pointer to SWR context.
/// @param workerId - The unique worker ID that is assigned to this thread.
/// @param curDrawBE - This tracks the draw contexts that this thread has processed. Each worker
/// thread
///                    has its own curDrawBE counter and this ensures that each worker processes all
///                    the draws in order.
void WorkOnCompute(SWR_CONTEXT* pContext, uint32_t workerId, uint32_t& curDrawBE)
{
    uint32_t drawEnqueued = 0;
    if (FindFirstIncompleteDraw(pContext, workerId, curDrawBE, drawEnqueued) == false)
    {
        return;
    }

    uint32_t lastRetiredDraw =
        pContext->dcRing[curDrawBE % pContext->MAX_DRAWS_IN_FLIGHT].drawId - 1;

    for (uint64_t i = curDrawBE; IDComparesLess(i, drawEnqueued); ++i)
    {
        DRAW_CONTEXT* pDC = &pContext->dcRing[i % pContext->MAX_DRAWS_IN_FLIGHT];
        if (pDC->isCompute == false)
            return;

        // check dependencies
        if (CheckDependency(pContext, pDC, lastRetiredDraw))
        {
            return;
        }

        SWR_ASSERT(pDC->pDispatch != nullptr);
        DispatchQueue& queue = *pDC->pDispatch;

        // Is there any work remaining?
        if (queue.getNumQueued() > 0)
        {
            void*    pSpillFillBuffer = nullptr;
            void*    pScratchSpace    = nullptr;
            uint32_t threadGroupId    = 0;
            while (queue.getWork(threadGroupId))
            {
                queue.dispatch(pDC, workerId, threadGroupId, pSpillFillBuffer, pScratchSpace);
                queue.finishedWork();
            }

            // Ensure all streaming writes are globally visible before moving onto the next draw
            _mm_mfence();
        }
    }
}

void BindApiThread(SWR_CONTEXT* pContext, uint32_t apiThreadId)
{
    if (nullptr == pContext)
    {
        return;
    }

    if (apiThreadId >= pContext->threadPool.numReservedThreads)
    {
        if (pContext->threadPool.numReservedThreads)
        {
            const THREAD_DATA& threadData = pContext->threadPool.pApiThreadData[0];
            // Just bind to the process group used for API thread 0
            bindThread(pContext, 0, threadData.procGroupId, true);
        }
        return;
    }

    const THREAD_DATA& threadData = pContext->threadPool.pApiThreadData[apiThreadId];

    bindThread(
        pContext, threadData.threadId, threadData.procGroupId, threadData.forceBindProcGroup);
}

template <bool IsFEThread, bool IsBEThread>
DWORD workerThreadMain(LPVOID pData)
{
    THREAD_DATA* pThreadData = (THREAD_DATA*)pData;
    SWR_CONTEXT* pContext    = pThreadData->pContext;
    uint32_t     threadId    = pThreadData->threadId;
    uint32_t     workerId    = pThreadData->workerId;

    bindThread(pContext, threadId, pThreadData->procGroupId, pThreadData->forceBindProcGroup);

    {
        char threadName[64];
        sprintf_s(threadName,
#if defined(_WIN32)
                  "SWRWorker_%02d_NUMA%d_Core%02d_T%d",
#else
                  // linux pthread name limited to 16 chars (including \0)
                  "w%03d-n%d-c%03d-t%d",
#endif
                  workerId,
                  pThreadData->numaId,
                  pThreadData->coreId,
                  pThreadData->htId);
        SetCurrentThreadName(threadName);
    }

    RDTSC_INIT(pContext->pBucketMgr, threadId);

    // Only need offset numa index from base for correct masking
    uint32_t numaNode = pThreadData->numaId - pContext->threadInfo.BASE_NUMA_NODE;
    uint32_t numaMask = pContext->threadPool.numaMask;

    SetOptimalVectorCSR();

    // Track tiles locked by other threads. If we try to lock a macrotile and find its already
    // locked then we'll add it to this list so that we don't try and lock it again.
    TileSet lockedTiles;

    // each worker has the ability to work on any of the queued draws as long as certain
    // conditions are met. the data associated
    // with a draw is guaranteed to be active as long as a worker hasn't signaled that he
    // has moved on to the next draw when he determines there is no more work to do. The api
    // thread will not increment the head of the dc ring until all workers have moved past the
    // current head.
    // the logic to determine what to work on is:
    // 1- try to work on the FE any draw that is queued. For now there are no dependencies
    //    on the FE work, so any worker can grab any FE and process in parallel.  Eventually
    //    we'll need dependency tracking to force serialization on FEs.  The worker will try
    //    to pick an FE by atomically incrementing a counter in the swr context.  he'll keep
    //    trying until he reaches the tail.
    // 2- BE work must be done in strict order. we accomplish this today by pulling work off
    //    the oldest draw (ie the head) of the dcRing. the worker can determine if there is
    //    any work left by comparing the total # of binned work items and the total # of completed
    //    work items. If they are equal, then there is no more work to do for this draw, and
    //    the worker can safely increment its oldestDraw counter and move on to the next draw.
    std::unique_lock<std::mutex> lock(pContext->WaitLock, std::defer_lock);

    auto threadHasWork = [&](uint32_t curDraw) { return curDraw != pContext->dcRing.GetHead(); };

    uint32_t curDrawBE = 0;
    uint32_t curDrawFE = 0;

    bool bShutdown = false;

    while (true)
    {
        if (bShutdown && !threadHasWork(curDrawBE))
        {
            break;
        }

        uint32_t loop = 0;
        while (loop++ < KNOB_WORKER_SPIN_LOOP_COUNT && !threadHasWork(curDrawBE))
        {
            _mm_pause();
        }

        if (!threadHasWork(curDrawBE))
        {
            lock.lock();

            // check for thread idle condition again under lock
            if (threadHasWork(curDrawBE))
            {
                lock.unlock();
                continue;
            }

            pContext->FifosNotEmpty.wait(lock);
            lock.unlock();
        }

        if (IsBEThread)
        {
            RDTSC_BEGIN(pContext->pBucketMgr, WorkerWorkOnFifoBE, 0);
            bShutdown |=
                WorkOnFifoBE(pContext, workerId, curDrawBE, lockedTiles, numaNode, numaMask);
            RDTSC_END(pContext->pBucketMgr, WorkerWorkOnFifoBE, 0);

            WorkOnCompute(pContext, workerId, curDrawBE);
        }

        if (IsFEThread)
        {
            WorkOnFifoFE(pContext, workerId, curDrawFE);

            if (!IsBEThread)
            {
                curDrawBE = curDrawFE;
            }
        }
    }

    return 0;
}
template <>
DWORD workerThreadMain<false, false>(LPVOID) = delete;

template <bool IsFEThread, bool IsBEThread>
DWORD workerThreadInit(LPVOID pData)
{
#if defined(_MSC_VER)
    __try
#endif // _WIN32
    {
        return workerThreadMain<IsFEThread, IsBEThread>(pData);
    }

#if defined(_MSC_VER)
    __except (EXCEPTION_CONTINUE_SEARCH)
    {
    }

#endif // _WIN32

    return 1;
}
template <>
DWORD workerThreadInit<false, false>(LPVOID pData) = delete;

static void InitPerThreadStats(SWR_CONTEXT* pContext, uint32_t numThreads)
{
    // Initialize DRAW_CONTEXT's per-thread stats
    for (uint32_t dc = 0; dc < pContext->MAX_DRAWS_IN_FLIGHT; ++dc)
    {
        pContext->dcRing[dc].dynState.pStats =
            (SWR_STATS*)AlignedMalloc(sizeof(SWR_STATS) * numThreads, 64);
        memset(pContext->dcRing[dc].dynState.pStats, 0, sizeof(SWR_STATS) * numThreads);
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Creates thread pool info but doesn't launch threads.
/// @param pContext - pointer to context
/// @param pPool - pointer to thread pool object.
void CreateThreadPool(SWR_CONTEXT* pContext, THREAD_POOL* pPool)
{
    CPUNumaNodes nodes;
    uint32_t     numThreadsPerProcGroup = 0;
    CalculateProcessorTopology(nodes, numThreadsPerProcGroup);
    assert(numThreadsPerProcGroup > 0);

    // Assumption, for asymmetric topologies, multi-threaded cores will appear
    // in the list before single-threaded cores.  This appears to be true for
    // Windows when the total HW threads is limited to 64.
    uint32_t numHWNodes        = (uint32_t)nodes.size();
    uint32_t numHWCoresPerNode = (uint32_t)nodes[0].cores.size();
    uint32_t numHWHyperThreads = (uint32_t)nodes[0].cores[0].threadIds.size();

#if defined(_WIN32) && !defined(_WIN64)
    if (!pContext->threadInfo.MAX_WORKER_THREADS)
    {
        // Limit 32-bit windows to bindable HW threads only
        if ((numHWCoresPerNode * numHWHyperThreads) > 32)
        {
            numHWCoresPerNode = 32 / numHWHyperThreads;
        }
    }
#endif

    // Calculate num HW threads.  Due to asymmetric topologies, this is not
    // a trivial multiplication.
    uint32_t numHWThreads = 0;
    for (auto const& node : nodes)
    {
        for (auto const& core : node.cores)
        {
            numHWThreads += (uint32_t)core.threadIds.size();
        }
    }

    uint32_t numNodes        = numHWNodes;
    uint32_t numCoresPerNode = numHWCoresPerNode;
    uint32_t numHyperThreads = numHWHyperThreads;

    // Calc used threads per-core
    if (numHyperThreads > pContext->threadInfo.BASE_THREAD)
    {
        numHyperThreads -= pContext->threadInfo.BASE_THREAD;
    }
    else
    {
        SWR_ASSERT(false,
                   "Cannot use BASE_THREAD value: %d, maxThreads: %d, reverting BASE_THREAD to 0",
                   pContext->threadInfo.BASE_THREAD,
                   numHyperThreads);
        pContext->threadInfo.BASE_THREAD = 0;
    }

    if (pContext->threadInfo.MAX_THREADS_PER_CORE)
    {
        numHyperThreads = std::min(numHyperThreads, pContext->threadInfo.MAX_THREADS_PER_CORE);
    }

    // Prune any cores that don't support the number of threads
    if (numHyperThreads > 1)
    {
        for (auto& node : nodes)
        {
            uint32_t numUsableCores = 0;
            for (auto& core : node.cores)
            {
                numUsableCores += (core.threadIds.size() >= numHyperThreads);
            }
            numCoresPerNode = std::min(numCoresPerNode, numUsableCores);
        }
    }

    // Calc used cores per NUMA node
    if (numCoresPerNode > pContext->threadInfo.BASE_CORE)
    {
        numCoresPerNode -= pContext->threadInfo.BASE_CORE;
    }
    else
    {
        SWR_ASSERT(false,
                   "Cannot use BASE_CORE value: %d, maxCores: %d, reverting BASE_CORE to 0",
                   pContext->threadInfo.BASE_CORE,
                   numCoresPerNode);
        pContext->threadInfo.BASE_CORE = 0;
    }

    if (pContext->threadInfo.MAX_CORES_PER_NUMA_NODE)
    {
        numCoresPerNode = std::min(numCoresPerNode, pContext->threadInfo.MAX_CORES_PER_NUMA_NODE);
    }

    // Calc used NUMA nodes
    if (numNodes > pContext->threadInfo.BASE_NUMA_NODE)
    {
        numNodes -= pContext->threadInfo.BASE_NUMA_NODE;
    }
    else
    {
        SWR_ASSERT(
            false,
            "Cannot use BASE_NUMA_NODE value: %d, maxNodes: %d, reverting BASE_NUMA_NODE to 0",
            pContext->threadInfo.BASE_NUMA_NODE,
            numNodes);
        pContext->threadInfo.BASE_NUMA_NODE = 0;
    }

    if (pContext->threadInfo.MAX_NUMA_NODES)
    {
        numNodes = std::min(numNodes, pContext->threadInfo.MAX_NUMA_NODES);
    }

    // Calculate numThreads - at this point everything should be symmetric
    uint32_t numThreads = numNodes * numCoresPerNode * numHyperThreads;
    SWR_REL_ASSERT(numThreads <= numHWThreads);

    uint32_t& numAPIReservedThreads = pContext->apiThreadInfo.numAPIReservedThreads;
    uint32_t& numAPIThreadsPerCore  = pContext->apiThreadInfo.numAPIThreadsPerCore;
    uint32_t  numRemovedThreads     = 0;

    if (pContext->threadInfo.SINGLE_THREADED)
    {
        numAPIReservedThreads      = 0;
        numThreads                 = 1;
        pContext->NumWorkerThreads = 1;
        pContext->NumFEThreads     = 1;
        pContext->NumBEThreads     = 1;
        pPool->numThreads          = 0;
    }
    else if (pContext->threadInfo.MAX_WORKER_THREADS)
    {
        numThreads = std::min(pContext->threadInfo.MAX_WORKER_THREADS, numHWThreads);
        pContext->threadInfo.BASE_NUMA_NODE = 0;
        pContext->threadInfo.BASE_CORE      = 0;
        pContext->threadInfo.BASE_THREAD    = 0;
        numAPIReservedThreads               = 0;
    }
    else
    {
        if (numAPIReservedThreads >= numThreads)
        {
            numAPIReservedThreads = 0;
        }
        else if (numAPIReservedThreads)
        {
            numAPIThreadsPerCore = std::min(numAPIThreadsPerCore, numHWHyperThreads);

            if (0 == numAPIThreadsPerCore)
            {
                numAPIThreadsPerCore = numHWHyperThreads;
            }

            numRemovedThreads = numAPIReservedThreads;
            if (numAPIThreadsPerCore == 2 && numHyperThreads == 1)
            {
                // Adjust removed threads to make logic below work
                numRemovedThreads =
                    std::max(1U, (numRemovedThreads + numAPIThreadsPerCore - 1) / 2);
            }

            numThreads -= numRemovedThreads;
        }
    }

    InitPerThreadStats(pContext, numThreads);

    if (pContext->threadInfo.SINGLE_THREADED)
    {
        numAPIReservedThreads = 0;
        numThreads            = 1;
    }

    if (numAPIReservedThreads)
    {
        pPool->pApiThreadData = new (std::nothrow) THREAD_DATA[numAPIReservedThreads];
        SWR_ASSERT(pPool->pApiThreadData);
        if (!pPool->pApiThreadData)
        {
            numAPIReservedThreads = 0;
        }
        else
        {
            memset(pPool->pApiThreadData, 0, sizeof(THREAD_DATA) * numAPIReservedThreads);
        }
    }
    pPool->numReservedThreads = numAPIReservedThreads;

    pPool->numThreads          = numThreads;
    pContext->NumWorkerThreads = pPool->numThreads;

    pPool->pThreadData = new (std::nothrow) THREAD_DATA[pPool->numThreads];
    assert(pPool->pThreadData);
    memset(pPool->pThreadData, 0, sizeof(THREAD_DATA) * pPool->numThreads);
    pPool->numaMask = 0;

    // Allocate worker private data
    pPool->pWorkerPrivateDataArray = nullptr;
    if (pContext->workerPrivateState.perWorkerPrivateStateSize == 0)
    {
        pContext->workerPrivateState.perWorkerPrivateStateSize = sizeof(SWR_WORKER_DATA);
        pContext->workerPrivateState.pfnInitWorkerData = nullptr;
        pContext->workerPrivateState.pfnFinishWorkerData = nullptr;
    }

    // initialize contents of SWR_WORKER_DATA
    size_t perWorkerSize =
        AlignUpPow2(pContext->workerPrivateState.perWorkerPrivateStateSize, 64);
    size_t totalSize = perWorkerSize * pPool->numThreads;
    if (totalSize)
    {
        pPool->pWorkerPrivateDataArray = AlignedMalloc(totalSize, 64);
        SWR_ASSERT(pPool->pWorkerPrivateDataArray);

        void* pWorkerData = pPool->pWorkerPrivateDataArray;
        for (uint32_t i = 0; i < pPool->numThreads; ++i)
        {
            pPool->pThreadData[i].pWorkerPrivateData = pWorkerData;
            if (pContext->workerPrivateState.pfnInitWorkerData)
            {
                pContext->workerPrivateState.pfnInitWorkerData(pContext, pWorkerData, i);
            }
            pWorkerData = PtrAdd(pWorkerData, perWorkerSize);
        }
    }

    if (pContext->threadInfo.SINGLE_THREADED)
    {
        return;
    }

    pPool->pThreads = new (std::nothrow) THREAD_PTR[pPool->numThreads];
    assert(pPool->pThreads);

    if (pContext->threadInfo.MAX_WORKER_THREADS)
    {
        bool     bForceBindProcGroup = (numThreads > numThreadsPerProcGroup);
        uint32_t numProcGroups = (numThreads + numThreadsPerProcGroup - 1) / numThreadsPerProcGroup;
        // When MAX_WORKER_THREADS is set we don't bother to bind to specific HW threads
        // But Windows will still require binding to specific process groups
        for (uint32_t workerId = 0; workerId < numThreads; ++workerId)
        {
            pPool->pThreadData[workerId].workerId           = workerId;
            pPool->pThreadData[workerId].procGroupId        = workerId % numProcGroups;
            pPool->pThreadData[workerId].threadId           = 0;
            pPool->pThreadData[workerId].numaId             = 0;
            pPool->pThreadData[workerId].coreId             = 0;
            pPool->pThreadData[workerId].htId               = 0;
            pPool->pThreadData[workerId].pContext           = pContext;
            pPool->pThreadData[workerId].forceBindProcGroup = bForceBindProcGroup;

            pContext->NumBEThreads++;
            pContext->NumFEThreads++;
        }
    }
    else
    {
        // numa distribution assumes workers on all nodes
        bool useNuma = true;
        if (numCoresPerNode * numHyperThreads == 1)
        {
            useNuma = false;
        }

        if (useNuma)
        {
            pPool->numaMask = numNodes - 1; // Only works for 2**n numa nodes (1, 2, 4, etc.)
        }
        else
        {
            pPool->numaMask = 0;
        }

        uint32_t workerId           = 0;
        uint32_t numReservedThreads = numAPIReservedThreads;
        for (uint32_t n = 0; n < numNodes; ++n)
        {
            if ((n + pContext->threadInfo.BASE_NUMA_NODE) >= nodes.size())
            {
                break;
            }
            auto&    node     = nodes[n + pContext->threadInfo.BASE_NUMA_NODE];
            uint32_t numCores = numCoresPerNode;
            for (uint32_t c = 0; c < numCores; ++c)
            {
                if ((c + pContext->threadInfo.BASE_CORE) >= node.cores.size())
                {
                    break;
                }

                auto& core = node.cores[c + pContext->threadInfo.BASE_CORE];
                for (uint32_t t = 0; t < numHyperThreads; ++t)
                {
                    if ((t + pContext->threadInfo.BASE_THREAD) >= core.threadIds.size())
                    {
                        break;
                    }

                    if (numRemovedThreads)
                    {
                        --numRemovedThreads;
                        assert(numReservedThreads);
                        --numReservedThreads;
                        pPool->pApiThreadData[numReservedThreads].workerId    = 0xFFFFFFFFU;
                        pPool->pApiThreadData[numReservedThreads].procGroupId = core.procGroup;
                        pPool->pApiThreadData[numReservedThreads].threadId    = core.threadIds[t];
                        pPool->pApiThreadData[numReservedThreads].numaId =
                            useNuma ? (n + pContext->threadInfo.BASE_NUMA_NODE) : 0;
                        pPool->pApiThreadData[numReservedThreads].coreId =
                            c + pContext->threadInfo.BASE_CORE;
                        pPool->pApiThreadData[numReservedThreads].htId =
                            t + pContext->threadInfo.BASE_THREAD;
                        pPool->pApiThreadData[numReservedThreads].pContext           = pContext;
                        pPool->pApiThreadData[numReservedThreads].forceBindProcGroup = false;

                        if (numAPIThreadsPerCore > numHyperThreads && numReservedThreads)
                        {
                            --numReservedThreads;
                            pPool->pApiThreadData[numReservedThreads].workerId    = 0xFFFFFFFFU;
                            pPool->pApiThreadData[numReservedThreads].procGroupId = core.procGroup;
                            pPool->pApiThreadData[numReservedThreads].threadId =
                                core.threadIds[t + 1];
                            pPool->pApiThreadData[numReservedThreads].numaId =
                                useNuma ? (n + pContext->threadInfo.BASE_NUMA_NODE) : 0;
                            pPool->pApiThreadData[numReservedThreads].coreId =
                                c + pContext->threadInfo.BASE_CORE;
                            pPool->pApiThreadData[numReservedThreads].htId =
                                t + pContext->threadInfo.BASE_THREAD;
                            pPool->pApiThreadData[numReservedThreads].pContext           = pContext;
                            pPool->pApiThreadData[numReservedThreads].forceBindProcGroup = false;
                        }

                        continue;
                    }

                    SWR_ASSERT(workerId < numThreads);

                    pPool->pThreadData[workerId].workerId    = workerId;
                    pPool->pThreadData[workerId].procGroupId = core.procGroup;
                    pPool->pThreadData[workerId].threadId =
                        core.threadIds[t + pContext->threadInfo.BASE_THREAD];
                    pPool->pThreadData[workerId].numaId =
                        useNuma ? (n + pContext->threadInfo.BASE_NUMA_NODE) : 0;
                    pPool->pThreadData[workerId].coreId   = c + pContext->threadInfo.BASE_CORE;
                    pPool->pThreadData[workerId].htId     = t + pContext->threadInfo.BASE_THREAD;
                    pPool->pThreadData[workerId].pContext = pContext;
                    pPool->pThreadData[workerId].forceBindProcGroup = false;

                    pContext->NumBEThreads++;
                    pContext->NumFEThreads++;

                    ++workerId;
                }
            }
        }
        SWR_ASSERT(workerId == pContext->NumWorkerThreads);
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Launches worker threads in thread pool.
/// @param pContext - pointer to context
/// @param pPool - pointer to thread pool object.
void StartThreadPool(SWR_CONTEXT* pContext, THREAD_POOL* pPool)
{
    if (pContext->threadInfo.SINGLE_THREADED)
    {
        return;
    }

    for (uint32_t workerId = 0; workerId < pContext->NumWorkerThreads; ++workerId)
    {
        pPool->pThreads[workerId] =
            new std::thread(workerThreadInit<true, true>, &pPool->pThreadData[workerId]);
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Destroys thread pool.
/// @param pContext - pointer to context
/// @param pPool - pointer to thread pool object.
void DestroyThreadPool(SWR_CONTEXT* pContext, THREAD_POOL* pPool)
{
    // Wait for all threads to finish
    SwrWaitForIdle(pContext);

    // Wait for threads to finish and destroy them
    for (uint32_t t = 0; t < pPool->numThreads; ++t)
    {
        if (!pContext->threadInfo.SINGLE_THREADED)
        {
            // Detach from thread.  Cannot join() due to possibility (in Windows) of code
            // in some DLLMain(THREAD_DETACH case) blocking the thread until after this returns.
            pPool->pThreads[t]->detach();
            delete (pPool->pThreads[t]);
        }

        if (pContext->workerPrivateState.pfnFinishWorkerData)
        {
            pContext->workerPrivateState.pfnFinishWorkerData(
                pContext, pPool->pThreadData[t].pWorkerPrivateData, t);
        }
    }

    delete[] pPool->pThreads;

    // Clean up data used by threads
    delete[] pPool->pThreadData;
    delete[] pPool->pApiThreadData;

    AlignedFree(pPool->pWorkerPrivateDataArray);
}
