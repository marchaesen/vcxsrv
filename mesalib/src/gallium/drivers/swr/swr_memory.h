/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 ***************************************************************************/

#pragma once
#include "rasterizer/core/context.h"
INLINE void
swr_LoadHotTile(HANDLE hDC,
                HANDLE hWorkerPrivateData,
                SWR_FORMAT dstFormat,
                SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
                UINT x, UINT y,
                uint32_t renderTargetArrayIndex, uint8_t* pDstHotTile)
{
   DRAW_CONTEXT *pDC = (DRAW_CONTEXT*)hDC;
   swr_draw_context *pSDC = (swr_draw_context*)GetPrivateState(pDC);
   SWR_SURFACE_STATE *pSrcSurface = &pSDC->renderTargets[renderTargetIndex];

   pSDC->pTileAPI->pfnSwrLoadHotTile(hWorkerPrivateData, pSrcSurface, pDC->pContext->pBucketMgr, dstFormat, renderTargetIndex, x, y, renderTargetArrayIndex, pDstHotTile);
}

INLINE void
swr_StoreHotTile(HANDLE hDC,
                 HANDLE hWorkerPrivateData,
                 SWR_FORMAT srcFormat,
                 SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
                 UINT x, UINT y,
                 uint32_t renderTargetArrayIndex, uint8_t* pSrcHotTile)
{
   DRAW_CONTEXT *pDC = (DRAW_CONTEXT*)hDC;
   swr_draw_context *pSDC = (swr_draw_context*)GetPrivateState(pDC);
   SWR_SURFACE_STATE *pDstSurface = &pSDC->renderTargets[renderTargetIndex];

   pSDC->pTileAPI->pfnSwrStoreHotTileToSurface(hWorkerPrivateData, pDstSurface, pDC->pContext->pBucketMgr, srcFormat, renderTargetIndex, x, y, renderTargetArrayIndex, pSrcHotTile);
}

INLINE gfxptr_t
swr_MakeGfxPtr(HANDLE hPrivateContext, void* sysAddr)
{
    // Fulfill an unused internal interface
    return (gfxptr_t)sysAddr;
}
