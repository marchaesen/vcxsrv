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

#pragma once
#include "knobs.h"

#include "common/os.h"
#include "common/rdtsc_buckets.h"

#include <vector>

///////////////////////////////////////////////////////////////////////////////
// NOTE:  This enum MUST be kept in sync with gCoreBuckets in rdtsc_core.cpp
///////////////////////////////////////////////////////////////////////////////
enum CORE_BUCKETS
{
    APIClearRenderTarget,
    APIDraw,
    APIDrawWakeAllThreads,
    APIDrawIndexed,
    APIDispatch,
    APIStoreTiles,
    APIGetDrawContext,
    APISync,
    APIWaitForIdle,
    FEProcessDraw,
    FEProcessDrawIndexed,
    FEFetchShader,
    FEVertexShader,
    FEHullShader,
    FETessellation,
    FEDomainShader,
    FEGeometryShader,
    FEStreamout,
    FEPAAssemble,
    FEBinPoints,
    FEBinLines,
    FEBinTriangles,
    FETriangleSetup,
    FEViewportCull,
    FEGuardbandClip,
    FEClipPoints,
    FEClipLines,
    FEClipTriangles,
    FEClipRectangles,
    FECullZeroAreaAndBackface,
    FECullBetweenCenters,
    FEEarlyRastEnter,
    FEEarlyRastExit,
    FEProcessStoreTiles,
    FEProcessInvalidateTiles,
    WorkerWorkOnFifoBE,
    WorkerFoundWork,
    BELoadTiles,
    BEDispatch,
    BEClear,
    BERasterizeLine,
    BERasterizeTriangle,
    BETriangleSetup,
    BEStepSetup,
    BECullZeroArea,
    BEEmptyTriangle,
    BETrivialAccept,
    BETrivialReject,
    BERasterizePartial,
    BEPixelBackend,
    BESetup,
    BEBarycentric,
    BEEarlyDepthTest,
    BEPixelShader,
    BESingleSampleBackend,
    BEPixelRateBackend,
    BESampleRateBackend,
    BENullBackend,
    BELateDepthTest,
    BEOutputMerger,
    BEStoreTiles,
    BEEndTile,

    NumBuckets
};

void rdtscReset(BucketManager* pBucketMgr);
void rdtscInit(BucketManager* pBucketMgr, int threadId);
void rdtscStart(BucketManager* pBucketMgr, uint32_t bucketId);
void rdtscStop(BucketManager* pBucketMgr, uint32_t bucketId, uint32_t count, uint64_t drawId);
void rdtscEvent(BucketManager* pBucketMgr, uint32_t bucketId, uint32_t count1, uint32_t count2);
void rdtscEndFrame(BucketManager* pBucketMgr);

#ifdef KNOB_ENABLE_RDTSC
#define RDTSC_RESET(pBucketMgr) rdtscReset(pBucketMgr)
#define RDTSC_INIT(pBucketMgr, threadId) rdtscInit(pBucketMgr,threadId)
#define RDTSC_START(pBucketMgr, bucket) rdtscStart(pBucketMgr, bucket)
#define RDTSC_STOP(pBucketMgr, bucket, count, draw) rdtscStop(pBucketMgr, bucket, count, draw)
#define RDTSC_EVENT(pBucketMgr, bucket, count1, count2) rdtscEvent(pBucketMgr, bucket, count1, count2)
#define RDTSC_ENDFRAME(pBucketMgr) rdtscEndFrame(pBucketMgr)
#else
#define RDTSC_RESET(pBucketMgr)
#define RDTSC_INIT(pBucketMgr, threadId)
#define RDTSC_START(pBucketMgr, bucket)
#define RDTSC_STOP(pBucketMgr, bucket, count, draw)
#define RDTSC_EVENT(pBucketMgr, bucket, count1, count2)
#define RDTSC_ENDFRAME(pBucketMgr)
#endif

extern BUCKET_DESC           gCoreBuckets[];

INLINE void rdtscReset(BucketManager *pBucketMgr)
{
    pBucketMgr->mCurrentFrame = 0;
    pBucketMgr->ClearThreads();
}

INLINE void rdtscInit(BucketManager* pBucketMgr, int threadId)
{
    // register all the buckets once
    if (!pBucketMgr->mBucketsInitialized && (threadId == 0))
    {
        pBucketMgr->mBucketMap.resize(NumBuckets);
        for (uint32_t i = 0; i < NumBuckets; ++i)
        {
            pBucketMgr->mBucketMap[i] = pBucketMgr->RegisterBucket(gCoreBuckets[i]);
        }
        pBucketMgr->mBucketsInitialized = true;
    }

    std::string name = threadId == 0 ? "API" : "WORKER";
    pBucketMgr->RegisterThread(name);
}

INLINE void rdtscStart(BucketManager* pBucketMgr, uint32_t bucketId)
{
    uint32_t id = pBucketMgr->mBucketMap[bucketId];
    pBucketMgr->StartBucket(id);
}

INLINE void rdtscStop(BucketManager* pBucketMgr, uint32_t bucketId, uint32_t count, uint64_t drawId)
{
    uint32_t id = pBucketMgr->mBucketMap[bucketId];
    pBucketMgr->StopBucket(id);
}

INLINE void rdtscEvent(BucketManager* pBucketMgr, uint32_t bucketId, uint32_t count1, uint32_t count2)
{
    uint32_t id = pBucketMgr->mBucketMap[bucketId];
    pBucketMgr->AddEvent(id, count1);
}

INLINE void rdtscEndFrame(BucketManager* pBucketMgr)
{
    pBucketMgr->mCurrentFrame++;

    if (pBucketMgr->mCurrentFrame == KNOB_BUCKETS_START_FRAME &&
        KNOB_BUCKETS_START_FRAME < KNOB_BUCKETS_END_FRAME)
    {
        pBucketMgr->StartCapture();
    }

    if (pBucketMgr->mCurrentFrame == KNOB_BUCKETS_END_FRAME &&
        KNOB_BUCKETS_START_FRAME < KNOB_BUCKETS_END_FRAME)
    {
        pBucketMgr->StopCapture();
        pBucketMgr->PrintReport("rdtsc.txt");
    }
}
