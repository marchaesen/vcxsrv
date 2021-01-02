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
 * @file context.h
 *
 * @brief Definitions for SWR_CONTEXT and DRAW_CONTEXT
 *        The SWR_CONTEXT is our global context and contains the DC ring,
 *        thread state, etc.
 *
 *        The DRAW_CONTEXT contains all state associated with a draw operation.
 *
 ******************************************************************************/
#pragma once

#include <condition_variable>
#include <algorithm>

#include "core/api.h"
#include "core/utils.h"
#include "core/arena.h"
#include "core/fifo.hpp"
#include "core/knobs.h"
#include "common/intrin.h"
#include "common/rdtsc_buckets.h"
#include "core/threads.h"
#include "ringbuffer.h"
#include "archrast/archrast.h"

// x.8 fixed point precision values
#define FIXED_POINT_SHIFT 8
#define FIXED_POINT_SCALE 256

// x.16 fixed point precision values
#define FIXED_POINT16_SHIFT 16
#define FIXED_POINT16_SCALE 65536

struct SWR_CONTEXT;
struct DRAW_CONTEXT;

struct TRI_FLAGS
{
    uint32_t frontFacing : 1;
    uint32_t yMajor : 1;
    uint32_t coverageMask : (SIMD_TILE_X_DIM* SIMD_TILE_Y_DIM);
    uint32_t reserved : 32 - 1 - 1 - (SIMD_TILE_X_DIM * SIMD_TILE_Y_DIM);
    float    pointSize;
    uint32_t renderTargetArrayIndex;
    uint32_t viewportIndex;
};

//////////////////////////////////////////////////////////////////////////
/// SWR_TRIANGLE_DESC
/////////////////////////////////////////////////////////////////////////
struct SWR_TRIANGLE_DESC
{
    float I[3];
    float J[3];
    float Z[3];
    float OneOverW[3];
    float recipDet;

    float* pRecipW;
    float* pAttribs;
    float* pPerspAttribs;
    float* pSamplePos;
    float* pUserClipBuffer;

    uint64_t coverageMask[SWR_MAX_NUM_MULTISAMPLES];
    uint64_t innerCoverageMask; // Conservative rasterization inner coverage: marked covered if
                                // entire pixel is covered
    uint64_t anyCoveredSamples;

    TRI_FLAGS triFlags;
};

struct TRIANGLE_WORK_DESC
{
    float* pTriBuffer;
    float* pAttribs;
    float* pUserClipBuffer;
    uint32_t  numAttribs;
    TRI_FLAGS triFlags;
};

struct CLEAR_DESC
{
    SWR_RECT rect;
    uint32_t attachmentMask;
    uint32_t renderTargetArrayIndex;
    float    clearRTColor[4]; // RGBA_32F
    float    clearDepth;      // [0..1]
    uint8_t  clearStencil;
};

struct DISCARD_INVALIDATE_TILES_DESC
{
    uint32_t       attachmentMask;
    SWR_RECT       rect;
    SWR_TILE_STATE newTileState;
    bool           createNewTiles;
    bool           fullTilesOnly;
};

struct SYNC_DESC
{
    PFN_CALLBACK_FUNC pfnCallbackFunc;
    uint64_t          userData;
    uint64_t          userData2;
    uint64_t          userData3;
};

struct STORE_TILES_DESC
{
    uint32_t       attachmentMask;
    SWR_TILE_STATE postStoreTileState;
    SWR_RECT       rect;
};

struct COMPUTE_DESC
{
    uint32_t threadGroupCountX;
    uint32_t threadGroupCountY;
    uint32_t threadGroupCountZ;
    bool     enableThreadDispatch;
};

typedef void (*PFN_WORK_FUNC)(DRAW_CONTEXT* pDC,
                              uint32_t      workerId,
                              uint32_t      macroTile,
                              void*         pDesc);

enum WORK_TYPE
{
    SYNC,
    DRAW,
    CLEAR,
    DISCARDINVALIDATETILES,
    STORETILES,
    SHUTDOWN,
};

OSALIGNSIMD(struct) BE_WORK
{
    WORK_TYPE     type;
    PFN_WORK_FUNC pfnWork;
    union
    {
        SYNC_DESC                     sync;
        TRIANGLE_WORK_DESC            tri;
        CLEAR_DESC                    clear;
        DISCARD_INVALIDATE_TILES_DESC discardInvalidateTiles;
        STORE_TILES_DESC              storeTiles;
    } desc;
};

struct DRAW_WORK
{
    DRAW_CONTEXT* pDC;
    union
    {
        uint32_t numIndices; // DrawIndexed: Number of indices for draw.
        uint32_t numVerts;   // Draw: Number of verts (triangles, lines, etc)
    };
    union
    {
        gfxptr_t xpIB;        // DrawIndexed: App supplied int32 indices
        uint32_t startVertex; // Draw: Starting vertex in VB to render from.
    };
    int32_t  baseVertex;
    uint32_t numInstances;  // Number of instances
    uint32_t startInstance; // Instance offset
    uint32_t startPrimID;   // starting primitiveID for this draw batch
    uint32_t
               startVertexID; // starting VertexID for this draw batch (only needed for non-indexed draws)
    SWR_FORMAT type;          // index buffer type
};

typedef void (*PFN_FE_WORK_FUNC)(SWR_CONTEXT*  pContext,
                                 DRAW_CONTEXT* pDC,
                                 uint32_t      workerId,
                                 void*         pDesc);
struct FE_WORK
{
    WORK_TYPE        type;
    PFN_FE_WORK_FUNC pfnWork;
    union
    {
        SYNC_DESC                     sync;
        DRAW_WORK                     draw;
        CLEAR_DESC                    clear;
        DISCARD_INVALIDATE_TILES_DESC discardInvalidateTiles;
        STORE_TILES_DESC              storeTiles;
    } desc;
};

struct GUARDBANDS
{
    float left[KNOB_NUM_VIEWPORTS_SCISSORS];
    float right[KNOB_NUM_VIEWPORTS_SCISSORS];
    float top[KNOB_NUM_VIEWPORTS_SCISSORS];
    float bottom[KNOB_NUM_VIEWPORTS_SCISSORS];
};

struct PA_STATE;

// function signature for pipeline stages that execute after primitive assembly
typedef void (*PFN_PROCESS_PRIMS)(DRAW_CONTEXT*      pDC,
                                  PA_STATE&          pa,
                                  uint32_t           workerId,
                                  simdvector         prims[],
                                  uint32_t           primMask,
                                  simdscalari const& primID,
                                  simdscalari const& viewportIdx,
                                  simdscalari const& rtIdx);

// function signature for pipeline stages that execute after primitive assembly
typedef void(SIMDCALL* PFN_PROCESS_PRIMS_SIMD16)(DRAW_CONTEXT*        pDC,
                                                 PA_STATE&            pa,
                                                 uint32_t             workerId,
                                                 simd16vector         prims[],
                                                 uint32_t             primMask,
                                                 simd16scalari const& primID,
                                                 simd16scalari const& viewportIdx,
                                                 simd16scalari const& rtIdx);

OSALIGNLINE(struct) API_STATE
{
    // Vertex Buffers
    SWR_VERTEX_BUFFER_STATE vertexBuffers[KNOB_NUM_STREAMS];

    // GS - Geometry Shader State
    SWR_GS_STATE gsState;
    PFN_GS_FUNC  pfnGsFunc;

    // FS - Fetch Shader State
    PFN_FETCH_FUNC pfnFetchFunc;

    // VS - Vertex Shader State
    PFN_VERTEX_FUNC pfnVertexFunc;

    // Index Buffer
    SWR_INDEX_BUFFER_STATE indexBuffer;

    // CS - Compute Shader
    PFN_CS_FUNC pfnCsFunc;
    uint32_t    totalThreadsInGroup;
    uint32_t    totalSpillFillSize;
    uint32_t    scratchSpaceSizePerWarp;
    uint32_t    scratchSpaceNumWarps;

    // FE - Frontend State
    SWR_FRONTEND_STATE frontendState;

    // SOS - Streamout Shader State
    PFN_SO_FUNC pfnSoFunc[MAX_SO_STREAMS];

    // Streamout state
    SWR_STREAMOUT_STATE          soState;
    mutable SWR_STREAMOUT_BUFFER soBuffer[MAX_SO_STREAMS];
    mutable SWR_STREAMOUT_BUFFER soPausedBuffer[MAX_SO_STREAMS];

    // Tessellation State
    PFN_HS_FUNC  pfnHsFunc;
    PFN_DS_FUNC  pfnDsFunc;
    SWR_TS_STATE tsState;

    // Number of attributes used by the frontend (vs, so, gs)
    uint32_t feNumAttributes;

    // RS - Rasterizer State
    SWR_RASTSTATE rastState;
    // floating point multisample offsets
    float samplePos[SWR_MAX_NUM_MULTISAMPLES * 2];

    GUARDBANDS gbState;

    SWR_VIEWPORT          vp[KNOB_NUM_VIEWPORTS_SCISSORS];
    SWR_VIEWPORT_MATRICES vpMatrices;

    SWR_RECT scissorRects[KNOB_NUM_VIEWPORTS_SCISSORS];
    SWR_RECT scissorsInFixedPoint[KNOB_NUM_VIEWPORTS_SCISSORS];
    bool     scissorsTileAligned;

    bool               forceFront;
    PRIMITIVE_TOPOLOGY topology;


    // Backend state
    OSALIGNLINE(SWR_BACKEND_STATE) backendState;

    SWR_DEPTH_BOUNDS_STATE depthBoundsState;

    // PS - Pixel shader state
    SWR_PS_STATE psState;

    SWR_DEPTH_STENCIL_STATE depthStencilState;

    // OM - Output Merger State
    SWR_BLEND_STATE    blendState;
    PFN_BLEND_JIT_FUNC pfnBlendFunc[SWR_NUM_RENDERTARGETS];

    struct
    {
        uint32_t enableStatsFE : 1;        // Enable frontend pipeline stats
        uint32_t enableStatsBE : 1;        // Enable backend pipeline stats
        uint32_t colorHottileEnable : 8;   // Bitmask of enabled color hottiles
        uint32_t depthHottileEnable : 1;   // Enable depth buffer hottile
        uint32_t stencilHottileEnable : 1; // Enable stencil buffer hottile
    };

    PFN_QUANTIZE_DEPTH pfnQuantizeDepth;
};

class MacroTileMgr;
class DispatchQueue;
class HOTTILE;

struct RenderOutputBuffers
{
    uint8_t* pColor[SWR_NUM_RENDERTARGETS];
    uint8_t* pDepth;
    uint8_t* pStencil;

    HOTTILE* pColorHotTile[SWR_NUM_RENDERTARGETS];
    HOTTILE* pDepthHotTile;
    HOTTILE* pStencilHotTile;
};

// Plane equation A/B/C coeffs used to evaluate I/J barycentric coords
struct BarycentricCoeffs
{
    simdscalar vIa;
    simdscalar vIb;
    simdscalar vIc;

    simdscalar vJa;
    simdscalar vJb;
    simdscalar vJc;

    simdscalar vZa;
    simdscalar vZb;
    simdscalar vZc;

    simdscalar vRecipDet;

    simdscalar vAOneOverW;
    simdscalar vBOneOverW;
    simdscalar vCOneOverW;
};

// pipeline function pointer types
typedef void (*PFN_BACKEND_FUNC)(
    DRAW_CONTEXT*, uint32_t, uint32_t, uint32_t, SWR_TRIANGLE_DESC&, RenderOutputBuffers&);
typedef void (*PFN_OUTPUT_MERGER)(SWR_PS_CONTEXT&,
                                  uint8_t* (&)[SWR_NUM_RENDERTARGETS],
                                  uint32_t,
                                  const SWR_BLEND_STATE*,
                                  const PFN_BLEND_JIT_FUNC (&)[SWR_NUM_RENDERTARGETS],
                                  simdscalar&,
                                  simdscalar const&);
typedef void (*PFN_CALC_PIXEL_BARYCENTRICS)(const BarycentricCoeffs&, SWR_PS_CONTEXT&);
typedef void (*PFN_CALC_SAMPLE_BARYCENTRICS)(const BarycentricCoeffs&, SWR_PS_CONTEXT&);
typedef void (*PFN_CALC_CENTROID_BARYCENTRICS)(const BarycentricCoeffs&,
                                               SWR_PS_CONTEXT&,
                                               const uint64_t* const,
                                               const uint32_t,
                                               simdscalar const&,
                                               simdscalar const&);

struct BACKEND_FUNCS
{
    PFN_BACKEND_FUNC pfnBackend;
};

// Draw State
struct DRAW_STATE
{
    API_STATE state;

    void* pPrivateState; // Its required the driver sets this up for each draw.

    // pipeline function pointers, filled in by API thread when setting up the draw
    BACKEND_FUNCS     backendFuncs;
    PFN_PROCESS_PRIMS pfnProcessPrims;
#if USE_SIMD16_FRONTEND
    PFN_PROCESS_PRIMS_SIMD16 pfnProcessPrims_simd16;
#endif

    CachingArena* pArena; // This should only be used by API thread.
};

struct DRAW_DYNAMIC_STATE
{
    void Reset(uint32_t numThreads)
    {
        SWR_STATS* pSavePtr = pStats;
        memset(this, 0, sizeof(*this));
        pStats = pSavePtr;
        memset(pStats, 0, sizeof(SWR_STATS) * numThreads);
    }
    ///@todo Currently assumes only a single FE can do stream output for a draw.
    uint32_t SoWriteOffset[4];
    bool     SoWriteOffsetDirty[4];

    SWR_STATS_FE statsFE; // Only one FE thread per DC.
    SWR_STATS*   pStats;
    uint64_t     soPrims; // number of primitives written to StreamOut buffer
};

// Draw Context
//    The api thread sets up a draw context that exists for the life of the draw.
//    This draw context maintains all of the state needed for the draw operation.
struct DRAW_CONTEXT
{
    SWR_CONTEXT* pContext;
    union
    {
        MacroTileMgr*  pTileMgr;
        DispatchQueue* pDispatch; // Queue for thread groups. (isCompute)
    };
    DRAW_STATE*   pState; // Read-only state. Core should not update this outside of API thread.
    CachingArena* pArena;

    uint32_t drawId;
    bool     dependentFE;  // Frontend work is dependent on all previous FE
    bool     dependent;    // Backend work is dependent on all previous BE
    bool     isCompute;    // Is this DC a compute context?
    bool     cleanupState; // True if this is the last draw using an entry in the state ring.

    FE_WORK FeWork;

    SYNC_DESC retireCallback; // Call this func when this DC is retired.

    DRAW_DYNAMIC_STATE dynState;

    volatile OSALIGNLINE(bool) doneFE; // Is FE work done for this draw?
    volatile OSALIGNLINE(uint32_t) FeLock;
    volatile OSALIGNLINE(uint32_t) threadsDone;
};

static_assert((sizeof(DRAW_CONTEXT) & 63) == 0, "Invalid size for DRAW_CONTEXT");

INLINE const API_STATE& GetApiState(const DRAW_CONTEXT* pDC)
{
    SWR_ASSERT(pDC != nullptr);
    SWR_ASSERT(pDC->pState != nullptr);

    return pDC->pState->state;
}

INLINE void* GetPrivateState(const DRAW_CONTEXT* pDC)
{
    SWR_ASSERT(pDC != nullptr);
    SWR_ASSERT(pDC->pState != nullptr);

    return pDC->pState->pPrivateState;
}

class HotTileMgr;

struct SWR_CONTEXT
{
    // Draw Context Ring
    //  Each draw needs its own state in order to support multiple draws in flight across multiple
    //  threads. We maintain N draw contexts configured as a ring. The size of the ring limits the
    //  maximum number of draws that can be in flight at any given time.
    //
    //  Description:
    //  1. State - When an application first sets state we'll request a new draw context to use.
    //     a. If there are no available draw contexts then we'll have to wait until one becomes
    //     free. b. If one is available then set pCurDrawContext to point to it and mark it in use.
    //     c. All state calls set state on pCurDrawContext.
    //  2. Draw - Creates submits a work item that is associated with current draw context.
    //     a. Set pPrevDrawContext = pCurDrawContext
    //     b. Set pCurDrawContext to NULL.
    //  3. State - When an applications sets state after draw
    //     a. Same as step 1.
    //     b. State is copied from prev draw context to current.
    RingBuffer<DRAW_CONTEXT> dcRing;

    DRAW_CONTEXT* pCurDrawContext;  // This points to DC entry in ring for an unsubmitted draw.
    DRAW_CONTEXT* pPrevDrawContext; // This points to DC entry for the previous context submitted
                                    // that we can copy state from.

    MacroTileMgr*  pMacroTileManagerArray;
    DispatchQueue* pDispatchQueueArray;

    // Draw State Ring
    //  When draw are very large (lots of primitives) then the API thread will break these up.
    //  These split draws all have identical state. So instead of storing the state directly
    //  in the Draw Context (DC) we instead store it in a Draw State (DS). This allows multiple DCs
    //  to reference a single entry in the DS ring.
    RingBuffer<DRAW_STATE> dsRing;

    uint32_t curStateId; // Current index to the next available entry in the DS ring.

    uint32_t NumWorkerThreads;
    uint32_t NumFEThreads;
    uint32_t NumBEThreads;

    THREAD_POOL              threadPool; // Thread pool associated with this context
    SWR_THREADING_INFO       threadInfo;
    SWR_API_THREADING_INFO   apiThreadInfo;
    SWR_WORKER_PRIVATE_STATE workerPrivateState;

    uint32_t MAX_DRAWS_IN_FLIGHT;

    std::condition_variable FifosNotEmpty;
    std::mutex              WaitLock;

    uint32_t privateStateSize;

    HotTileMgr* pHotTileMgr;

    // Callback functions, passed in at create context time
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


    // Global Stats
    SWR_STATS* pStats;

    // Scratch space for workers.
    uint8_t** ppScratch;

    volatile OSALIGNLINE(uint32_t) drawsOutstandingFE;

    OSALIGNLINE(CachingAllocator) cachingArenaAllocator;
    uint32_t frameCount;

    uint32_t lastFrameChecked;
    uint64_t lastDrawChecked;
    TileSet* pSingleThreadLockedTiles;

    // ArchRast thread contexts.
    HANDLE* pArContext;

    // handle to external memory for worker data to create memory contexts
    HANDLE hExternalMemory;

    BucketManager *pBucketMgr;
};

#define UPDATE_STAT_BE(name, count)                   \
    if (GetApiState(pDC).enableStatsBE)               \
    {                                                 \
        pDC->dynState.pStats[workerId].name += count; \
    }
#define UPDATE_STAT_FE(name, count)          \
    if (GetApiState(pDC).enableStatsFE)      \
    {                                        \
        pDC->dynState.statsFE.name += count; \
    }

// ArchRast instrumentation framework
#define AR_WORKER_CTX pDC->pContext->pArContext[workerId]
#define AR_API_CTX pDC->pContext->pArContext[pContext->NumWorkerThreads]

#ifdef KNOB_ENABLE_RDTSC
#define RDTSC_BEGIN(pBucketMgr, type, drawid) RDTSC_START(pBucketMgr, type)
#define RDTSC_END(pBucketMgr, type, count) RDTSC_STOP(pBucketMgr, type, count, 0)
#else
#define RDTSC_BEGIN(pBucketMgr, type, drawid)
#define RDTSC_END(pBucketMgr, type, count)
#endif

#ifdef KNOB_ENABLE_AR
#define _AR_EVENT(ctx, event) ArchRast::Dispatch(ctx, ArchRast::event)
#define _AR_FLUSH(ctx, id) ArchRast::FlushDraw(ctx, id)
#else
#define _AR_EVENT(ctx, event)
#define _AR_FLUSH(ctx, id)
#endif

// Use these macros for api thread.
#define AR_API_EVENT(event) _AR_EVENT(AR_API_CTX, event)

// Use these macros for worker threads.
#define AR_EVENT(event) _AR_EVENT(AR_WORKER_CTX, event)
#define AR_FLUSH(id) _AR_FLUSH(AR_WORKER_CTX, id)
