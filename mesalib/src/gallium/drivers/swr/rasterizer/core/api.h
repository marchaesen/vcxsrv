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
 * @file api.h
 *
 * @brief API definitions
 *
 ******************************************************************************/

#ifndef __SWR_API_H__
#define __SWR_API_H__

#include "common/os.h"

#include <assert.h>
#include <algorithm>

#include "common/intrin.h"
#include "common/formats.h"
#include "core/state.h"

typedef void(SWR_API* PFN_CALLBACK_FUNC)(uint64_t data, uint64_t data2, uint64_t data3);

//////////////////////////////////////////////////////////////////////////
/// @brief Rectangle structure
struct SWR_RECT
{
    int32_t xmin; ///< inclusive
    int32_t ymin; ///< inclusive
    int32_t xmax; ///< exclusive
    int32_t ymax; ///< exclusive

    bool operator==(const SWR_RECT& rhs)
    {
        return (this->ymin == rhs.ymin && this->ymax == rhs.ymax && this->xmin == rhs.xmin &&
                this->xmax == rhs.xmax);
    }

    bool operator!=(const SWR_RECT& rhs) { return !(*this == rhs); }

    SWR_RECT& Intersect(const SWR_RECT& other)
    {
        this->xmin = std::max(this->xmin, other.xmin);
        this->ymin = std::max(this->ymin, other.ymin);
        this->xmax = std::min(this->xmax, other.xmax);
        this->ymax = std::min(this->ymax, other.ymax);

        if (xmax - xmin < 0 || ymax - ymin < 0)
        {
            // Zero area
            ymin = ymax = xmin = xmax = 0;
        }

        return *this;
    }
    SWR_RECT& operator&=(const SWR_RECT& other) { return Intersect(other); }

    SWR_RECT& Union(const SWR_RECT& other)
    {
        this->xmin = std::min(this->xmin, other.xmin);
        this->ymin = std::min(this->ymin, other.ymin);
        this->xmax = std::max(this->xmax, other.xmax);
        this->ymax = std::max(this->ymax, other.ymax);

        return *this;
    }

    SWR_RECT& operator|=(const SWR_RECT& other) { return Union(other); }

    void Translate(int32_t x, int32_t y)
    {
        xmin += x;
        ymin += y;
        xmax += x;
        ymax += y;
    }
};

//////////////////////////////////////////////////////////////////////////
/// @brief Function signature for load hot tiles
/// @param hDC - handle to DRAW_CONTEXT
/// @param dstFormat - format of the hot tile
/// @param renderTargetIndex - render target to store, can be color, depth or stencil
/// @param x - destination x coordinate
/// @param y - destination y coordinate
/// @param pDstHotTile - pointer to the hot tile surface
typedef void(SWR_API* PFN_LOAD_TILE)(HANDLE                      hDC,
                                     HANDLE                      hWorkerPrivateData,
                                     SWR_FORMAT                  dstFormat,
                                     SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
                                     uint32_t                    x,
                                     uint32_t                    y,
                                     uint32_t                    renderTargetArrayIndex,
                                     uint8_t*                    pDstHotTile);

//////////////////////////////////////////////////////////////////////////
/// @brief Function signature for store hot tiles
/// @param hDC - handle to DRAW_CONTEXT
/// @param srcFormat - format of the hot tile
/// @param renderTargetIndex - render target to store, can be color, depth or stencil
/// @param x - destination x coordinate
/// @param y - destination y coordinate
/// @param pSrcHotTile - pointer to the hot tile surface
typedef void(SWR_API* PFN_STORE_TILE)(HANDLE                      hDC,
                                      HANDLE                      hWorkerPrivateData,
                                      SWR_FORMAT                  srcFormat,
                                      SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
                                      uint32_t                    x,
                                      uint32_t                    y,
                                      uint32_t                    renderTargetArrayIndex,
                                      uint8_t*                    pSrcHotTile);

//////////////////////////////////////////////////////////////////////////
/// @brief Function signature for clearing from the hot tiles clear value
/// @param hPrivateContext - handle to private data
/// @param renderTargetIndex - render target to store, can be color, depth or stencil
/// @param x - destination x coordinate
/// @param y - destination y coordinate
/// @param renderTargetArrayIndex - render target array offset from arrayIndex
/// @param pClearColor - pointer to the hot tile's clear value
typedef void(SWR_API* PFN_CLEAR_TILE)(HANDLE                      hPrivateContext,
                                      HANDLE                      hWorkerPrivateData,
                                      SWR_RENDERTARGET_ATTACHMENT rtIndex,
                                      uint32_t                    x,
                                      uint32_t                    y,
                                      uint32_t                    renderTargetArrayIndex,
                                      const float*                pClearColor);

typedef void*(SWR_API* PFN_TRANSLATE_GFXPTR_FOR_READ)(HANDLE   hPrivateContext,
                                                      gfxptr_t xpAddr,
                                                      bool*    pbNullTileAccessed,
                                                      HANDLE   hPrivateWorkerData);

typedef void*(SWR_API* PFN_TRANSLATE_GFXPTR_FOR_WRITE)(HANDLE   hPrivateContext,
                                                       gfxptr_t xpAddr,
                                                       bool*    pbNullTileAccessed,
                                                       HANDLE   hPrivateWorkerData);

typedef gfxptr_t(SWR_API* PFN_MAKE_GFXPTR)(HANDLE hPrivateContext, void* sysAddr);

typedef HANDLE(SWR_API* PFN_CREATE_MEMORY_CONTEXT)(HANDLE hExternalMemory);

typedef void(SWR_API* PFN_DESTROY_MEMORY_CONTEXT)(HANDLE hExternalMemory, HANDLE hMemoryContext);

//////////////////////////////////////////////////////////////////////////
/// @brief Callback to allow driver to update their copy of streamout write offset.
///        This is call is made for any draw operation that has streamout enabled
///        and has updated the write offset.
/// @param hPrivateContext - handle to private data
/// @param soBufferSlot - buffer slot for write offset
/// @param soWriteOffset - update value for so write offset.
typedef void(SWR_API* PFN_UPDATE_SO_WRITE_OFFSET)(HANDLE   hPrivateContext,
                                                  uint32_t soBufferSlot,
                                                  uint32_t soWriteOffset);

//////////////////////////////////////////////////////////////////////////
/// @brief Callback to allow driver to update their copy of stats.
/// @param hPrivateContext - handle to private data
/// @param pStats - pointer to draw stats
typedef void(SWR_API* PFN_UPDATE_STATS)(HANDLE hPrivateContext, const SWR_STATS* pStats);

//////////////////////////////////////////////////////////////////////////
/// @brief Callback to allow driver to update their copy of FE stats.
/// @note Its optimal to have a separate callback for FE stats since
///       there is only one DC per FE thread. This means we do not have
///       to sum up the stats across all of the workers.
/// @param hPrivateContext - handle to private data
/// @param pStats - pointer to draw stats
typedef void(SWR_API* PFN_UPDATE_STATS_FE)(HANDLE hPrivateContext, const SWR_STATS_FE* pStats);

//////////////////////////////////////////////////////////////////////////
/// @brief Callback to allow driver to update StreamOut status
/// @param hPrivateContext - handle to private data
/// @param numPrims - number of primitives written to StreamOut buffer
typedef void(SWR_API* PFN_UPDATE_STREAMOUT)(HANDLE hPrivateContext, uint64_t numPrims);

//////////////////////////////////////////////////////////////////////////
/// BucketManager
/// Forward Declaration (see rdtsc_buckets.h for full definition)
/////////////////////////////////////////////////////////////////////////
class BucketManager;

//////////////////////////////////////////////////////////////////////////
/// SWR_THREADING_INFO
/////////////////////////////////////////////////////////////////////////
struct SWR_THREADING_INFO
{
    uint32_t BASE_NUMA_NODE;
    uint32_t BASE_CORE;
    uint32_t BASE_THREAD;
    uint32_t MAX_WORKER_THREADS;
    uint32_t MAX_NUMA_NODES;
    uint32_t MAX_CORES_PER_NUMA_NODE;
    uint32_t MAX_THREADS_PER_CORE;
    bool     SINGLE_THREADED;
};

//////////////////////////////////////////////////////////////////////////
/// SWR_API_THREADING_INFO
/// Data used to reserve HW threads for API use
/// API Threads are reserved from numa nodes / cores used for
/// SWR Worker threads.  Specifying reserved threads here can reduce
/// the total number of SWR worker threads.
/////////////////////////////////////////////////////////////////////////
struct SWR_API_THREADING_INFO
{
    uint32_t numAPIReservedThreads; // Default is 1 if SWR_API_THREADING_INFO is not sent
    uint32_t bindAPIThread0;        // Default is true if numAPIReservedThreads is > 0,
                                    // binds thread used in SwrCreateContext to API Reserved
                                    // thread 0
    uint32_t numAPIThreadsPerCore; // 0 - means use all threads per core, else clamp to this number.
                                   // Independent of KNOB_MAX_THREADS_PER_CORE.
};

//////////////////////////////////////////////////////////////////////////
/// SWR_CONTEXT
/// Forward Declaration (see context.h for full definition)
/////////////////////////////////////////////////////////////////////////
class SWR_CONTEXT;

//////////////////////////////////////////////////////////////////////////
/// SWR_WORKER_PRIVATE_STATE
/// Data used to allocate per-worker thread private data.  A pointer
/// to this data will be passed in to each shader function.
/// The first field of this private data must be SWR_WORKER_DATA
/// perWorkerPrivateStateSize must be >= sizeof SWR_WORKER_DATA 
/////////////////////////////////////////////////////////////////////////
struct SWR_WORKER_PRIVATE_STATE
{
    typedef void(SWR_API* PFN_WORKER_DATA)(SWR_CONTEXT* pContext, HANDLE hWorkerPrivateData, uint32_t iWorkerNum);

    size_t          perWorkerPrivateStateSize; ///< Amount of data to allocate per-worker
    PFN_WORKER_DATA pfnInitWorkerData;         ///< Init function for worker data.  If null
                                               ///< worker data will be initialized to 0.
    PFN_WORKER_DATA pfnFinishWorkerData;       ///< Finish / destroy function for worker data.
                                               ///< Can be null.
};

//////////////////////////////////////////////////////////////////////////
/// SWR_CREATECONTEXT_INFO
/////////////////////////////////////////////////////////////////////////
struct SWR_CREATECONTEXT_INFO
{
    // External functions (e.g. sampler) need per draw context state.
    // Use SwrGetPrivateContextState() to access private state.
    size_t privateStateSize;

    // Optional per-worker state, can be NULL for no worker-private data
    SWR_WORKER_PRIVATE_STATE* pWorkerPrivateState;

    // Callback functions
    PFN_LOAD_TILE                  pfnLoadTile;
    PFN_STORE_TILE                 pfnStoreTile;
    PFN_TRANSLATE_GFXPTR_FOR_READ  pfnTranslateGfxptrForRead;
    PFN_TRANSLATE_GFXPTR_FOR_WRITE pfnTranslateGfxptrForWrite;
    PFN_MAKE_GFXPTR                pfnMakeGfxPtr;
    PFN_CREATE_MEMORY_CONTEXT      pfnCreateMemoryContext;
    PFN_DESTROY_MEMORY_CONTEXT     pfnDestroyMemoryContext;
    PFN_UPDATE_SO_WRITE_OFFSET     pfnUpdateSoWriteOffset;
    PFN_UPDATE_STATS               pfnUpdateStats;
    PFN_UPDATE_STATS_FE            pfnUpdateStatsFE;
    PFN_UPDATE_STREAMOUT           pfnUpdateStreamOut;


    // Pointer to rdtsc buckets mgr returned to the caller.
    // Only populated when KNOB_ENABLE_RDTSC is set
    BucketManager* pBucketMgr;

    // Output: size required memory passed to for SwrSaveState / SwrRestoreState
    size_t contextSaveSize;

    // ArchRast event manager.
    HANDLE hArEventManager;

    // handle to external memory for worker data to create memory contexts
    HANDLE hExternalMemory;

    // Input (optional): Threading info that overrides any set KNOB values.
    SWR_THREADING_INFO* pThreadInfo;

    // Input (optional): Info for reserving API threads
    SWR_API_THREADING_INFO* pApiThreadInfo;

    // Input: if set to non-zero value, overrides KNOB value for maximum
    // number of draws in flight
    uint32_t MAX_DRAWS_IN_FLIGHT;

    std::string contextName;
};

//////////////////////////////////////////////////////////////////////////
/// @brief Create SWR Context.
/// @param pCreateInfo - pointer to creation info.
SWR_FUNC(HANDLE, SwrCreateContext, SWR_CREATECONTEXT_INFO* pCreateInfo);

//////////////////////////////////////////////////////////////////////////
/// @brief Destroys SWR Context.
/// @param hContext - Handle passed back from SwrCreateContext
SWR_FUNC(void, SwrDestroyContext, HANDLE hContext);

//////////////////////////////////////////////////////////////////////////
/// @brief Bind current thread to an API reserved HW thread
/// @param hContext - Handle passed back from SwrCreateContext
/// @param apiThreadId - index of reserved HW thread to bind to.
SWR_FUNC(void, SwrBindApiThread, HANDLE hContext, uint32_t apiThreadId);

//////////////////////////////////////////////////////////////////////////
/// @brief Saves API state associated with hContext
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pOutputStateBlock - Memory block to receive API state data
/// @param memSize - Size of memory pointed to by pOutputStateBlock
SWR_FUNC(void, SwrSaveState, HANDLE hContext, void* pOutputStateBlock, size_t memSize);

//////////////////////////////////////////////////////////////////////////
/// @brief Restores API state to hContext previously saved with SwrSaveState
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pStateBlock - Memory block to read API state data from
/// @param memSize - Size of memory pointed to by pStateBlock
SWR_FUNC(void, SwrRestoreState, HANDLE hContext, const void* pStateBlock, size_t memSize);

//////////////////////////////////////////////////////////////////////////
/// @brief Sync cmd. Executes the callback func when all rendering up to this sync
///        has been completed
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pfnFunc - pointer to callback function,
/// @param userData - user data to pass back
SWR_FUNC(void,
         SwrSync,
         HANDLE            hContext,
         PFN_CALLBACK_FUNC pfnFunc,
         uint64_t          userData,
         uint64_t          userData2,
         uint64_t          userData3);

//////////////////////////////////////////////////////////////////////////
/// @brief Stall cmd. Stalls the backend until all previous work has been completed.
///        Frontend work can continue to make progress
/// @param hContext - Handle passed back from SwrCreateContext
SWR_FUNC(void, SwrStallBE, HANDLE hContext);

//////////////////////////////////////////////////////////////////////////
/// @brief Blocks until all rendering has been completed.
/// @param hContext - Handle passed back from SwrCreateContext
SWR_FUNC(void, SwrWaitForIdle, HANDLE hContext);

//////////////////////////////////////////////////////////////////////////
/// @brief Blocks until all FE rendering has been completed.
/// @param hContext - Handle passed back from SwrCreateContext
SWR_FUNC(void, SwrWaitForIdleFE, HANDLE hContext);

//////////////////////////////////////////////////////////////////////////
/// @brief Set vertex buffer state.
/// @param hContext - Handle passed back from SwrCreateContext
/// @param numBuffers - Number of vertex buffer state descriptors.
/// @param pVertexBuffers - Array of vertex buffer state descriptors.
SWR_FUNC(void,
         SwrSetVertexBuffers,
         HANDLE                         hContext,
         uint32_t                       numBuffers,
         const SWR_VERTEX_BUFFER_STATE* pVertexBuffers);

//////////////////////////////////////////////////////////////////////////
/// @brief Set index buffer
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pIndexBuffer - Index buffer.
SWR_FUNC(void, SwrSetIndexBuffer, HANDLE hContext, const SWR_INDEX_BUFFER_STATE* pIndexBuffer);

//////////////////////////////////////////////////////////////////////////
/// @brief Set fetch shader pointer.
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pfnFetchFunc - Pointer to shader.
SWR_FUNC(void, SwrSetFetchFunc, HANDLE hContext, PFN_FETCH_FUNC pfnFetchFunc);

//////////////////////////////////////////////////////////////////////////
/// @brief Set streamout shader pointer.
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pfnSoFunc - Pointer to shader.
/// @param streamIndex - specifies stream
SWR_FUNC(void, SwrSetSoFunc, HANDLE hContext, PFN_SO_FUNC pfnSoFunc, uint32_t streamIndex);

//////////////////////////////////////////////////////////////////////////
/// @brief Set streamout state
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pSoState - Pointer to streamout state.
SWR_FUNC(void, SwrSetSoState, HANDLE hContext, SWR_STREAMOUT_STATE* pSoState);

//////////////////////////////////////////////////////////////////////////
/// @brief Set streamout buffer state
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pSoBuffer - Pointer to streamout buffer.
/// @param slot - Slot to bind SO buffer to.
SWR_FUNC(void, SwrSetSoBuffers, HANDLE hContext, SWR_STREAMOUT_BUFFER* pSoBuffer, uint32_t slot);

//////////////////////////////////////////////////////////////////////////
/// @brief Set vertex shader pointer.
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pfnVertexFunc - Pointer to shader.
SWR_FUNC(void, SwrSetVertexFunc, HANDLE hContext, PFN_VERTEX_FUNC pfnVertexFunc);

//////////////////////////////////////////////////////////////////////////
/// @brief Set frontend state.
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pState - Pointer to state
SWR_FUNC(void, SwrSetFrontendState, HANDLE hContext, SWR_FRONTEND_STATE* pState);

//////////////////////////////////////////////////////////////////////////
/// @brief Set geometry shader state.
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pState - Pointer to state
SWR_FUNC(void, SwrSetGsState, HANDLE hContext, SWR_GS_STATE* pState);

//////////////////////////////////////////////////////////////////////////
/// @brief Set geometry shader
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pState - Pointer to geometry shader function
SWR_FUNC(void, SwrSetGsFunc, HANDLE hContext, PFN_GS_FUNC pfnGsFunc);

//////////////////////////////////////////////////////////////////////////
/// @brief Set compute shader
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pfnCsFunc - Pointer to compute shader function
/// @param totalThreadsInGroup - product of thread group dimensions.
/// @param totalSpillFillSize - size in bytes needed for spill/fill.
/// @param scratchSpaceSizePerInstance - size of the scratch space needed per simd instance
/// @param numInstances - number of simd instances that are run per execution of the shader
SWR_FUNC(void,
         SwrSetCsFunc,
         HANDLE      hContext,
         PFN_CS_FUNC pfnCsFunc,
         uint32_t    totalThreadsInGroup,
         uint32_t    totalSpillFillSize,
         uint32_t    scratchSpaceSizePerInstance,
         uint32_t    numInstances);

//////////////////////////////////////////////////////////////////////////
/// @brief Set tessellation state.
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pState - Pointer to state
SWR_FUNC(void, SwrSetTsState, HANDLE hContext, SWR_TS_STATE* pState);

//////////////////////////////////////////////////////////////////////////
/// @brief Set hull shader
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pfnFunc - Pointer to shader function
SWR_FUNC(void, SwrSetHsFunc, HANDLE hContext, PFN_HS_FUNC pfnFunc);

//////////////////////////////////////////////////////////////////////////
/// @brief Set domain shader
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pfnFunc - Pointer to shader function
SWR_FUNC(void, SwrSetDsFunc, HANDLE hContext, PFN_DS_FUNC pfnFunc);

//////////////////////////////////////////////////////////////////////////
/// @brief Set depth stencil state
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pState - Pointer to state.
SWR_FUNC(void, SwrSetDepthStencilState, HANDLE hContext, SWR_DEPTH_STENCIL_STATE* pState);

//////////////////////////////////////////////////////////////////////////
/// @brief Set backend state
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pState - Pointer to state.
SWR_FUNC(void, SwrSetBackendState, HANDLE hContext, SWR_BACKEND_STATE* pState);

//////////////////////////////////////////////////////////////////////////
/// @brief Set depth bounds state
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pState - Pointer to state.
SWR_FUNC(void, SwrSetDepthBoundsState, HANDLE hContext, SWR_DEPTH_BOUNDS_STATE* pState);

//////////////////////////////////////////////////////////////////////////
/// @brief Set pixel shader state
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pState - Pointer to state.
SWR_FUNC(void, SwrSetPixelShaderState, HANDLE hContext, SWR_PS_STATE* pState);

//////////////////////////////////////////////////////////////////////////
/// @brief Set blend state
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pState - Pointer to state.
SWR_FUNC(void, SwrSetBlendState, HANDLE hContext, SWR_BLEND_STATE* pState);

//////////////////////////////////////////////////////////////////////////
/// @brief Set blend function
/// @param hContext - Handle passed back from SwrCreateContext
/// @param renderTarget - render target index
/// @param pfnBlendFunc - function pointer
SWR_FUNC(
    void, SwrSetBlendFunc, HANDLE hContext, uint32_t renderTarget, PFN_BLEND_JIT_FUNC pfnBlendFunc);

//////////////////////////////////////////////////////////////////////////
/// @brief SwrDraw
/// @param hContext - Handle passed back from SwrCreateContext
/// @param topology - Specifies topology for draw.
/// @param startVertex - Specifies start vertex in vertex buffer for draw.
/// @param primCount - Number of vertices.
SWR_FUNC(void,
         SwrDraw,
         HANDLE             hContext,
         PRIMITIVE_TOPOLOGY topology,
         uint32_t           startVertex,
         uint32_t           primCount);

//////////////////////////////////////////////////////////////////////////
/// @brief SwrDrawInstanced
/// @param hContext - Handle passed back from SwrCreateContext
/// @param topology - Specifies topology for draw.
/// @param numVertsPerInstance - How many vertices to read sequentially from vertex data.
/// @param numInstances - How many instances to render.
/// @param startVertex - Specifies start vertex for draw. (vertex data)
/// @param startInstance - Which instance to start sequentially fetching from in each buffer
/// (instanced data)
SWR_FUNC(void,
         SwrDrawInstanced,
         HANDLE             hContext,
         PRIMITIVE_TOPOLOGY topology,
         uint32_t           numVertsPerInstance,
         uint32_t           numInstances,
         uint32_t           startVertex,
         uint32_t           startInstance);

//////////////////////////////////////////////////////////////////////////
/// @brief DrawIndexed
/// @param hContext - Handle passed back from SwrCreateContext
/// @param topology - Specifies topology for draw.
/// @param numIndices - Number of indices to read sequentially from index buffer.
/// @param indexOffset - Starting index into index buffer.
/// @param baseVertex - Vertex in vertex buffer to consider as index "0". Note value is signed.
SWR_FUNC(void,
         SwrDrawIndexed,
         HANDLE             hContext,
         PRIMITIVE_TOPOLOGY topology,
         uint32_t           numIndices,
         uint32_t           indexOffset,
         int32_t            baseVertex);

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
SWR_FUNC(void,
         SwrDrawIndexedInstanced,
         HANDLE             hContext,
         PRIMITIVE_TOPOLOGY topology,
         uint32_t           numIndices,
         uint32_t           numInstances,
         uint32_t           indexOffset,
         int32_t            baseVertex,
         uint32_t           startInstance);

//////////////////////////////////////////////////////////////////////////
/// @brief SwrInvalidateTiles
/// @param hContext - Handle passed back from SwrCreateContext
/// @param attachmentMask - The mask specifies which surfaces attached to the hottiles to
/// invalidate.
/// @param invalidateRect - The pixel-coordinate rectangle to invalidate.  This will be expanded to
///                         be hottile size-aligned.
SWR_FUNC(void,
         SwrInvalidateTiles,
         HANDLE          hContext,
         uint32_t        attachmentMask,
         const SWR_RECT& invalidateRect);

//////////////////////////////////////////////////////////////////////////
/// @brief SwrDiscardRect
/// @param hContext - Handle passed back from SwrCreateContext
/// @param attachmentMask - The mask specifies which surfaces attached to the hottiles to discard.
/// @param rect - The pixel-coordinate rectangle to discard.  Only fully-covered hottiles will be
///               discarded.
SWR_FUNC(void, SwrDiscardRect, HANDLE hContext, uint32_t attachmentMask, const SWR_RECT& rect);

//////////////////////////////////////////////////////////////////////////
/// @brief SwrDispatch
/// @param hContext - Handle passed back from SwrCreateContext
/// @param threadGroupCountX - Number of thread groups dispatched in X direction
/// @param threadGroupCountY - Number of thread groups dispatched in Y direction
/// @param threadGroupCountZ - Number of thread groups dispatched in Z direction
SWR_FUNC(void,
         SwrDispatch,
         HANDLE   hContext,
         uint32_t threadGroupCountX,
         uint32_t threadGroupCountY,
         uint32_t threadGroupCountZ);

/// @note this enum needs to be kept in sync with HOTTILE_STATE!
enum SWR_TILE_STATE
{
    SWR_TILE_INVALID = 0, // tile is in uninitialized state and should be loaded with surface contents
                          // before rendering
    SWR_TILE_DIRTY    = 2, // tile contains newer data than surface it represents
    SWR_TILE_RESOLVED = 3, // is in sync with surface it represents
};

/// @todo Add a good description for what attachments are and when and why you would use the
/// different SWR_TILE_STATEs.
SWR_FUNC(void,
         SwrStoreTiles,
         HANDLE          hContext,
         uint32_t        attachmentMask,
         SWR_TILE_STATE  postStoreTileState,
         const SWR_RECT& storeRect);

//////////////////////////////////////////////////////////////////////////
/// @brief SwrClearRenderTarget - Clear attached render targets / depth / stencil
/// @param hContext - Handle passed back from SwrCreateContext
/// @param attachmentMask - combination of SWR_ATTACHMENT_*_BIT attachments to clear
/// @param renderTargetArrayIndex - the RT array index to clear
/// @param clearColor - color use for clearing render targets
/// @param z - depth value use for clearing depth buffer
/// @param stencil - stencil value used for clearing stencil buffer
/// @param clearRect - The pixel-coordinate rectangle to clear in all cleared buffers
SWR_FUNC(void,
         SwrClearRenderTarget,
         HANDLE          hContext,
         uint32_t        attachmentMask,
         uint32_t        renderTargetArrayIndex,
         const float     clearColor[4],
         float           z,
         uint8_t         stencil,
         const SWR_RECT& clearRect);

//////////////////////////////////////////////////////////////////////////
/// @brief SwrSetRastState
/// @param hContext - Handle passed back from SwrCreateContext
/// @param pRastState - New SWR_RASTSTATE used for SwrDraw* commands
SWR_FUNC(void, SwrSetRastState, HANDLE hContext, const SWR_RASTSTATE* pRastState);

//////////////////////////////////////////////////////////////////////////
/// @brief SwrSetViewports
/// @param hContext - Handle passed back from SwrCreateContext
/// @param numViewports - number of viewports passed in
/// @param pViewports - Specifies extents of viewport.
/// @param pMatrices - If not specified then SWR computes a default one.
SWR_FUNC(void,
         SwrSetViewports,
         HANDLE                       hContext,
         uint32_t                     numViewports,
         const SWR_VIEWPORT*          pViewports,
         const SWR_VIEWPORT_MATRICES* pMatrices);

//////////////////////////////////////////////////////////////////////////
/// @brief SwrSetScissorRects
/// @param hContext - Handle passed back from SwrCreateContext
/// @param numScissors - number of scissors passed in
/// @param pScissors - array of scissors
SWR_FUNC(
    void, SwrSetScissorRects, HANDLE hContext, uint32_t numScissors, const SWR_RECT* pScissors);

//////////////////////////////////////////////////////////////////////////
/// @brief Returns a pointer to the private context state for the current
///        draw operation. This is used for external componets such as the
///        sampler.
///
/// @note  Client needs to resend private state prior to each draw call.
///        Also, SWR is responsible for the private state memory.
/// @param hContext - Handle passed back from SwrCreateContext
SWR_FUNC(void*, SwrGetPrivateContextState, HANDLE hContext);

//////////////////////////////////////////////////////////////////////////
/// @brief Clients can use this to allocate memory for draw/dispatch
///        operations. The memory will automatically be freed once operation
///        has completed. Client can use this to allocate binding tables,
///        etc. needed for shader execution.
/// @param hContext - Handle passed back from SwrCreateContext
/// @param size - Size of allocation
/// @param align - Alignment needed for allocation.
SWR_FUNC(void*, SwrAllocDrawContextMemory, HANDLE hContext, uint32_t size, uint32_t align);

//////////////////////////////////////////////////////////////////////////
/// @brief Enables stats counting
/// @param hContext - Handle passed back from SwrCreateContext
/// @param enable - If true then counts are incremented.
SWR_FUNC(void, SwrEnableStatsFE, HANDLE hContext, bool enable);

//////////////////////////////////////////////////////////////////////////
/// @brief Enables stats counting
/// @param hContext - Handle passed back from SwrCreateContext
/// @param enable - If true then counts are incremented.
SWR_FUNC(void, SwrEnableStatsBE, HANDLE hContext, bool enable);

//////////////////////////////////////////////////////////////////////////
/// @brief Mark end of frame - used for performance profiling
/// @param hContext - Handle passed back from SwrCreateContext
SWR_FUNC(void, SwrEndFrame, HANDLE hContext);

//////////////////////////////////////////////////////////////////////////
/// @brief Initialize swr backend and memory internal tables
SWR_FUNC(void, SwrInit);


struct SWR_INTERFACE
{
    PFNSwrCreateContext          pfnSwrCreateContext;
    PFNSwrDestroyContext         pfnSwrDestroyContext;
    PFNSwrBindApiThread          pfnSwrBindApiThread;
    PFNSwrSaveState              pfnSwrSaveState;
    PFNSwrRestoreState           pfnSwrRestoreState;
    PFNSwrSync                   pfnSwrSync;
    PFNSwrStallBE                pfnSwrStallBE;
    PFNSwrWaitForIdle            pfnSwrWaitForIdle;
    PFNSwrWaitForIdleFE          pfnSwrWaitForIdleFE;
    PFNSwrSetVertexBuffers       pfnSwrSetVertexBuffers;
    PFNSwrSetIndexBuffer         pfnSwrSetIndexBuffer;
    PFNSwrSetFetchFunc           pfnSwrSetFetchFunc;
    PFNSwrSetSoFunc              pfnSwrSetSoFunc;
    PFNSwrSetSoState             pfnSwrSetSoState;
    PFNSwrSetSoBuffers           pfnSwrSetSoBuffers;
    PFNSwrSetVertexFunc          pfnSwrSetVertexFunc;
    PFNSwrSetFrontendState       pfnSwrSetFrontendState;
    PFNSwrSetGsState             pfnSwrSetGsState;
    PFNSwrSetGsFunc              pfnSwrSetGsFunc;
    PFNSwrSetCsFunc              pfnSwrSetCsFunc;
    PFNSwrSetTsState             pfnSwrSetTsState;
    PFNSwrSetHsFunc              pfnSwrSetHsFunc;
    PFNSwrSetDsFunc              pfnSwrSetDsFunc;
    PFNSwrSetDepthStencilState   pfnSwrSetDepthStencilState;
    PFNSwrSetBackendState        pfnSwrSetBackendState;
    PFNSwrSetDepthBoundsState    pfnSwrSetDepthBoundsState;
    PFNSwrSetPixelShaderState    pfnSwrSetPixelShaderState;
    PFNSwrSetBlendState          pfnSwrSetBlendState;
    PFNSwrSetBlendFunc           pfnSwrSetBlendFunc;
    PFNSwrDraw                   pfnSwrDraw;
    PFNSwrDrawInstanced          pfnSwrDrawInstanced;
    PFNSwrDrawIndexed            pfnSwrDrawIndexed;
    PFNSwrDrawIndexedInstanced   pfnSwrDrawIndexedInstanced;
    PFNSwrInvalidateTiles        pfnSwrInvalidateTiles;
    PFNSwrDiscardRect            pfnSwrDiscardRect;
    PFNSwrDispatch               pfnSwrDispatch;
    PFNSwrStoreTiles             pfnSwrStoreTiles;
    PFNSwrClearRenderTarget      pfnSwrClearRenderTarget;
    PFNSwrSetRastState           pfnSwrSetRastState;
    PFNSwrSetViewports           pfnSwrSetViewports;
    PFNSwrSetScissorRects        pfnSwrSetScissorRects;
    PFNSwrGetPrivateContextState pfnSwrGetPrivateContextState;
    PFNSwrAllocDrawContextMemory pfnSwrAllocDrawContextMemory;
    PFNSwrEnableStatsFE          pfnSwrEnableStatsFE;
    PFNSwrEnableStatsBE          pfnSwrEnableStatsBE;
    PFNSwrEndFrame               pfnSwrEndFrame;
    PFNSwrInit                   pfnSwrInit;
};

extern "C" {
typedef void(SWR_API* PFNSwrGetInterface)(SWR_INTERFACE& out_funcs);
SWR_VISIBLE void SWR_API SwrGetInterface(SWR_INTERFACE& out_funcs);
}

#endif
