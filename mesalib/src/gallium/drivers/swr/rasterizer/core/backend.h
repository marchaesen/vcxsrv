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
 * @file backend.h
 *
 * @brief Backend handles rasterization, pixel shading and output merger
 *        operations.
 *
 ******************************************************************************/
#pragma once

#include "common/os.h"
#include "core/context.h"
#include "core/multisample.h"
#include "depthstencil.h"
#include "rdtsc_core.h"

void ProcessComputeBE(DRAW_CONTEXT* pDC,
                      uint32_t      workerId,
                      uint32_t      threadGroupId,
                      void*&        pSpillFillBuffer,
                      void*&        pScratchSpace);
void ProcessSyncBE(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pUserData);
void ProcessClearBE(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pUserData);
void ProcessStoreTilesBE(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pData);
void ProcessDiscardInvalidateTilesBE(DRAW_CONTEXT* pDC,
                                     uint32_t      workerId,
                                     uint32_t      macroTile,
                                     void*         pData);
void ProcessShutdownBE(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pUserData);

typedef void (*PFN_CLEAR_TILES)(DRAW_CONTEXT*,
                                HANDLE                      hWorkerData,
                                SWR_RENDERTARGET_ATTACHMENT rt,
                                uint32_t,
                                uint32_t,
                                uint32_t[4],
                                const SWR_RECT& rect);

extern PFN_CLEAR_TILES  gClearTilesTable[NUM_SWR_FORMATS];
extern PFN_BACKEND_FUNC gBackendNullPs[SWR_MULTISAMPLE_TYPE_COUNT];
extern PFN_BACKEND_FUNC gBackendSingleSample[SWR_INPUT_COVERAGE_COUNT][2]     // centroid
                                            [2];                              // canEarlyZ
extern PFN_BACKEND_FUNC gBackendPixelRateTable[SWR_MULTISAMPLE_TYPE_COUNT][2] // isCenterPattern
                                              [SWR_INPUT_COVERAGE_COUNT][2]   // centroid
                                              [2]                             // forcedSampleCount
                                              [2]                             // canEarlyZ
    ;
extern PFN_BACKEND_FUNC gBackendSampleRateTable[SWR_MULTISAMPLE_TYPE_COUNT]
                                               [SWR_INPUT_COVERAGE_COUNT][2] // centroid
                                               [2];                          // canEarlyZ
