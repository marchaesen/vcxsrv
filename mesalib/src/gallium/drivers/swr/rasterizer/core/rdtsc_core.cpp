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

#include "rdtsc_core.h"
#include "common/rdtsc_buckets.h"

// must match CORE_BUCKETS enum order
BUCKET_DESC gCoreBuckets[] = {
    {"APIClearRenderTarget", "", true, 0xff0b8bea},
    {"APIDraw", "", true, 0xff000066},
    {"APIDrawWakeAllThreads", "", false, 0xffffffff},
    {"APIDrawIndexed", "", true, 0xff000066},
    {"APIDispatch", "", true, 0xff660000},
    {"APIStoreTiles", "", true, 0xff00ffff},
    {"APIGetDrawContext", "", false, 0xffffffff},
    {"APISync", "", true, 0xff6666ff},
    {"APIWaitForIdle", "", true, 0xff0000ff},
    {"FEProcessDraw", "", true, 0xff009900},
    {"FEProcessDrawIndexed", "", true, 0xff009900},
    {"FEFetchShader", "", false, 0xffffffff},
    {"FEVertexShader", "", false, 0xffffffff},
    {"FEHullShader", "", false, 0xffffffff},
    {"FETessellation", "", false, 0xffffffff},
    {"FEDomainShader", "", false, 0xffffffff},
    {"FEGeometryShader", "", false, 0xffffffff},
    {"FEStreamout", "", false, 0xffffffff},
    {"FEPAAssemble", "", false, 0xffffffff},
    {"FEBinPoints", "", false, 0xff29b854},
    {"FEBinLines", "", false, 0xff29b854},
    {"FEBinTriangles", "", false, 0xff29b854},
    {"FETriangleSetup", "", false, 0xffffffff},
    {"FEViewportCull", "", false, 0xffffffff},
    {"FEGuardbandClip", "", false, 0xffffffff},
    {"FEClipPoints", "", false, 0xffffffff},
    {"FEClipLines", "", false, 0xffffffff},
    {"FEClipTriangles", "", false, 0xffffffff},
    {"FEClipRectangles", "", false, 0xffffffff},
    {"FECullZeroAreaAndBackface", "", false, 0xffffffff},
    {"FECullBetweenCenters", "", false, 0xffffffff},
    {"FEEarlyRastEnter", "", false, 0xffffffff},
    {"FEEarlyRastExit", "", false, 0xffffffff},
    {"FEProcessStoreTiles", "", true, 0xff39c864},
    {"FEProcessInvalidateTiles", "", true, 0xffffffff},
    {"WorkerWorkOnFifoBE", "", false, 0xff40261c},
    {"WorkerFoundWork", "", false, 0xff573326},
    {"BELoadTiles", "", true, 0xffb0e2ff},
    {"BEDispatch", "", true, 0xff00a2ff},
    {"BEClear", "", true, 0xff00ccbb},
    {"BERasterizeLine", "", true, 0xffb26a4e},
    {"BERasterizeTriangle", "", true, 0xffb26a4e},
    {"BETriangleSetup", "", false, 0xffffffff},
    {"BEStepSetup", "", false, 0xffffffff},
    {"BECullZeroArea", "", false, 0xffffffff},
    {"BEEmptyTriangle", "", false, 0xffffffff},
    {"BETrivialAccept", "", false, 0xffffffff},
    {"BETrivialReject", "", false, 0xffffffff},
    {"BERasterizePartial", "", false, 0xffffffff},
    {"BEPixelBackend", "", false, 0xffffffff},
    {"BESetup", "", false, 0xffffffff},
    {"BEBarycentric", "", false, 0xffffffff},
    {"BEEarlyDepthTest", "", false, 0xffffffff},
    {"BEPixelShader", "", false, 0xffffffff},
    {"BESingleSampleBackend", "", false, 0xffffffff},
    {"BEPixelRateBackend", "", false, 0xffffffff},
    {"BESampleRateBackend", "", false, 0xffffffff},
    {"BENullBackend", "", false, 0xffffffff},
    {"BELateDepthTest", "", false, 0xffffffff},
    {"BEOutputMerger", "", false, 0xffffffff},
    {"BEStoreTiles", "", true, 0xff00cccc},
    {"BEEndTile", "", false, 0xffffffff},
};
static_assert(NumBuckets == (sizeof(gCoreBuckets) / sizeof(gCoreBuckets[0])),
              "RDTSC Bucket enum and description table size mismatched.");

