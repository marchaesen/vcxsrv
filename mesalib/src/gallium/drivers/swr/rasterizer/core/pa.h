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
 *
 * @file pa.h
 *
 * @brief Definitions for primitive assembly.
 *        N primitives are assembled at a time, where N is the SIMD width.
 *        A state machine, that is specific for a given topology, drives the
 *        assembly of vertices into triangles.
 *
 ******************************************************************************/
#pragma once

#include "frontend.h"

struct PA_STATE
{
#if USE_SIMD16_FRONTEND
    enum
    {
        SIMD_WIDTH      = KNOB_SIMD16_WIDTH,
        SIMD_WIDTH_DIV2 = KNOB_SIMD16_WIDTH / 2,
        SIMD_WIDTH_LOG2 = 4
    };

    typedef simd16mask SIMDMASK;

    typedef simd16scalar SIMDSCALAR;
    typedef simd16vector SIMDVECTOR;
    typedef simd16vertex SIMDVERTEX;

    typedef simd16scalari SIMDSCALARI;

#else
    enum
    {
        SIMD_WIDTH      = KNOB_SIMD_WIDTH,
        SIMD_WIDTH_DIV2 = KNOB_SIMD_WIDTH / 2,
        SIMD_WIDTH_LOG2 = 3
    };

    typedef simdmask SIMDMASK;

    typedef simdscalar SIMDSCALAR;
    typedef simdvector SIMDVECTOR;
    typedef simdvertex SIMDVERTEX;

    typedef simdscalari SIMDSCALARI;

#endif
    DRAW_CONTEXT* pDC{nullptr};         // draw context
    uint8_t*      pStreamBase{nullptr}; // vertex stream
    uint32_t      streamSizeInVerts{0}; // total size of the input stream in verts
    uint32_t      vertexStride{0};      // stride of a vertex in simdvector units

    // The topology the binner will use. In some cases the FE changes the topology from the api
    // state.
    PRIMITIVE_TOPOLOGY binTopology{TOP_UNKNOWN};

#if ENABLE_AVX512_SIMD16
    bool useAlternateOffset{false};
#endif

    bool     viewportArrayActive{false};
    bool     rtArrayActive{false};
    uint32_t numVertsPerPrim{0};

    PA_STATE() {}
    PA_STATE(DRAW_CONTEXT* in_pDC,
             uint8_t*      in_pStreamBase,
             uint32_t      in_streamSizeInVerts,
             uint32_t      in_vertexStride,
             uint32_t      in_numVertsPerPrim) :
        pDC(in_pDC),
        pStreamBase(in_pStreamBase), streamSizeInVerts(in_streamSizeInVerts),
        vertexStride(in_vertexStride), numVertsPerPrim(in_numVertsPerPrim)
    {
    }

    virtual bool        HasWork()                                    = 0;
    virtual simdvector& GetSimdVector(uint32_t index, uint32_t slot) = 0;
#if ENABLE_AVX512_SIMD16
    virtual simd16vector& GetSimdVector_simd16(uint32_t index, uint32_t slot) = 0;
#endif
    virtual bool Assemble(uint32_t slot, simdvector verts[]) = 0;
#if ENABLE_AVX512_SIMD16
    virtual bool Assemble(uint32_t slot, simd16vector verts[]) = 0;
#endif
    virtual void        AssembleSingle(uint32_t slot, uint32_t primIndex, simd4scalar verts[]) = 0;
    virtual bool        NextPrim()                                                             = 0;
    virtual SIMDVERTEX& GetNextVsOutput()                                                      = 0;
    virtual bool        GetNextStreamOutput()                                                  = 0;
    virtual SIMDMASK&   GetNextVsIndices()                                                     = 0;
    virtual uint32_t    NumPrims()                                                             = 0;
    virtual void        Reset()                                                                = 0;
    virtual SIMDSCALARI GetPrimID(uint32_t startID)                                            = 0;
};

// The Optimized PA is a state machine that assembles triangles from vertex shader simd
// output. Here is the sequence
//    1. Execute FS/VS to generate a simd vertex (4 vertices for SSE simd and 8 for AVX simd).
//    2. Execute PA function to assemble and bin triangles.
//        a.    The PA function is a set of functions that collectively make up the
//            state machine for a given topology.
//                1.    We use a state index to track which PA function to call.
//        b. Often the PA function needs to 2 simd vertices in order to assemble the next triangle.
//                1.    We call this the current and previous simd vertex.
//                2.    The SSE simd is 4-wide which is not a multiple of 3 needed for triangles. In
//                    order to assemble the second triangle, for a triangle list, we'll need the
//                    last vertex from the previous simd and the first 2 vertices from the current
//                    simd.
//                3. At times the PA can assemble multiple triangles from the 2 simd vertices.
//
// This optimized PA is not cut aware, so only should be used by non-indexed draws or draws without
// cuts
struct PA_STATE_OPT : public PA_STATE
{
    uint32_t numPrims{0};         // Total number of primitives for draw.
    uint32_t numPrimsComplete{0}; // Total number of complete primitives.

    uint32_t numSimdPrims{0}; // Number of prims in current simd.

    uint32_t       cur{0};   // index to current VS output.
    uint32_t       prev{0};  // index to prev VS output. Not really needed in the state.
    const uint32_t first{0}; // index to first VS output. Used for tri fan and line loop.

    uint32_t counter{0};   // state counter
    bool     reset{false}; // reset state

    uint32_t    primIDIncr{0}; // how much to increment for each vector (typically vector / {1, 2})
    SIMDSCALARI primID;

    typedef bool (*PFN_PA_FUNC)(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
#if ENABLE_AVX512_SIMD16
    typedef bool (*PFN_PA_FUNC_SIMD16)(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
#endif
    typedef void (*PFN_PA_SINGLE_FUNC)(PA_STATE_OPT& pa,
                                       uint32_t      slot,
                                       uint32_t      primIndex,
                                       simd4scalar   verts[]);

    PFN_PA_FUNC pfnPaFunc{nullptr}; // PA state machine function for assembling 4 triangles.
#if ENABLE_AVX512_SIMD16
    PFN_PA_FUNC_SIMD16 pfnPaFunc_simd16{nullptr};
#endif
    PFN_PA_SINGLE_FUNC pfnPaSingleFunc{
        nullptr}; // PA state machine function for assembling single triangle.
    PFN_PA_FUNC pfnPaFuncReset{nullptr}; // initial state to set on reset
#if ENABLE_AVX512_SIMD16
    PFN_PA_FUNC_SIMD16 pfnPaFuncReset_simd16{nullptr};
#endif

    // state used to advance the PA when Next is called
    PFN_PA_FUNC pfnPaNextFunc{nullptr};
#if ENABLE_AVX512_SIMD16
    PFN_PA_FUNC_SIMD16 pfnPaNextFunc_simd16{nullptr};
#endif
    uint32_t nextNumSimdPrims{0};
    uint32_t nextNumPrimsIncrement{0};
    bool     nextReset{false};
    bool     isStreaming{false};

    SIMDMASK junkIndices{0}; // temporary index store for unused virtual function

    PA_STATE_OPT() {}
    PA_STATE_OPT(DRAW_CONTEXT*      pDC,
                 uint32_t           numPrims,
                 uint8_t*           pStream,
                 uint32_t           streamSizeInVerts,
                 uint32_t           vertexStride,
                 bool               in_isStreaming,
                 uint32_t           numVertsPerPrim,
                 PRIMITIVE_TOPOLOGY topo = TOP_UNKNOWN);

    bool HasWork() { return (this->numPrimsComplete < this->numPrims) ? true : false; }

    simdvector& GetSimdVector(uint32_t index, uint32_t slot)
    {
        SWR_ASSERT(slot < vertexStride);
        uint32_t    offset     = index * vertexStride + slot;
        simdvector& vertexSlot = ((simdvector*)pStreamBase)[offset];
        return vertexSlot;
    }

#if ENABLE_AVX512_SIMD16
    simd16vector& GetSimdVector_simd16(uint32_t index, uint32_t slot)
    {
        SWR_ASSERT(slot < vertexStride);
        uint32_t      offset     = index * vertexStride + slot;
        simd16vector& vertexSlot = ((simd16vector*)pStreamBase)[offset];
        return vertexSlot;
    }

#endif
    // Assembles 4 triangles. Each simdvector is a single vertex from 4
    // triangles (xxxx yyyy zzzz wwww) and there are 3 verts per triangle.
    bool Assemble(uint32_t slot, simdvector verts[]) { return this->pfnPaFunc(*this, slot, verts); }

#if ENABLE_AVX512_SIMD16
    bool Assemble(uint32_t slot, simd16vector verts[])
    {
        return this->pfnPaFunc_simd16(*this, slot, verts);
    }

#endif
    // Assembles 1 primitive. Each simdscalar is a vertex (xyzw).
    void AssembleSingle(uint32_t slot, uint32_t primIndex, simd4scalar verts[])
    {
        return this->pfnPaSingleFunc(*this, slot, primIndex, verts);
    }

    bool NextPrim()
    {
        this->pfnPaFunc = this->pfnPaNextFunc;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = this->pfnPaNextFunc_simd16;
#endif
        this->numSimdPrims = this->nextNumSimdPrims;
        this->numPrimsComplete += this->nextNumPrimsIncrement;
        this->reset = this->nextReset;

        if (this->isStreaming)
        {
            this->reset = false;
        }

        bool morePrims = false;

        if (this->numSimdPrims > 0)
        {
            morePrims = true;
            this->numSimdPrims--;
        }
        else
        {
            this->counter = (this->reset) ? 0 : (this->counter + 1);
            this->reset   = false;
        }

        if (!HasWork())
        {
            morePrims = false; // no more to do
        }

        return morePrims;
    }

    SIMDVERTEX& GetNextVsOutput()
    {
        const uint32_t numSimdVerts = streamSizeInVerts / SIMD_WIDTH;

        // increment cur and prev indices
        if (counter < numSimdVerts)
        {
            // prev undefined for first state
            prev = cur;
            cur  = counter;
        }
        else
        {
            // swap/recycle last two simd verts for prev and cur, leave other simd verts intact in
            // the buffer
            uint32_t temp = prev;

            prev = cur;
            cur  = temp;
        }

        SWR_ASSERT(cur < numSimdVerts);
        SIMDVECTOR* pVertex = &((SIMDVECTOR*)pStreamBase)[cur * vertexStride];

        return *(SIMDVERTEX*)pVertex;
    }

    SIMDMASK& GetNextVsIndices()
    {
        // unused in optimized PA, pass tmp buffer back
        return junkIndices;
    }

    bool GetNextStreamOutput()
    {
        this->prev = this->cur;
        this->cur  = this->counter;

        return HasWork();
    }

    uint32_t NumPrims()
    {
        return (this->numPrimsComplete + this->nextNumPrimsIncrement > this->numPrims)
                   ? (SIMD_WIDTH -
                      (this->numPrimsComplete + this->nextNumPrimsIncrement - this->numPrims))
                   : SIMD_WIDTH;
    }

    void SetNextState(PA_STATE_OPT::PFN_PA_FUNC        pfnPaNextFunc,
                      PA_STATE_OPT::PFN_PA_SINGLE_FUNC pfnPaNextSingleFunc,
                      uint32_t                         numSimdPrims      = 0,
                      uint32_t                         numPrimsIncrement = 0,
                      bool                             reset             = false)
    {
        this->pfnPaNextFunc         = pfnPaNextFunc;
        this->nextNumSimdPrims      = numSimdPrims;
        this->nextNumPrimsIncrement = numPrimsIncrement;
        this->nextReset             = reset;

        this->pfnPaSingleFunc = pfnPaNextSingleFunc;
    }

#if ENABLE_AVX512_SIMD16
    void SetNextState_simd16(PA_STATE_OPT::PFN_PA_FUNC_SIMD16 pfnPaNextFunc_simd16,
                             PA_STATE_OPT::PFN_PA_FUNC        pfnPaNextFunc,
                             PA_STATE_OPT::PFN_PA_SINGLE_FUNC pfnPaNextSingleFunc,
                             uint32_t                         numSimdPrims      = 0,
                             uint32_t                         numPrimsIncrement = 0,
                             bool                             reset             = false)
    {
        this->pfnPaNextFunc_simd16  = pfnPaNextFunc_simd16;
        this->pfnPaNextFunc         = pfnPaNextFunc;
        this->nextNumSimdPrims      = numSimdPrims;
        this->nextNumPrimsIncrement = numPrimsIncrement;
        this->nextReset             = reset;

        this->pfnPaSingleFunc = pfnPaNextSingleFunc;
    }

#endif
    void Reset()
    {
#if ENABLE_AVX512_SIMD16
        useAlternateOffset = false;

#endif
        this->pfnPaFunc = this->pfnPaFuncReset;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = this->pfnPaFuncReset_simd16;
#endif
        this->numPrimsComplete = 0;
        this->numSimdPrims     = 0;
        this->cur              = 0;
        this->prev             = 0;
        this->counter          = 0;
        this->reset            = false;
    }

    SIMDSCALARI GetPrimID(uint32_t startID)
    {
#if USE_SIMD16_FRONTEND
        return _simd16_add_epi32(
            this->primID,
            _simd16_set1_epi32(startID + this->primIDIncr * (this->numPrimsComplete / SIMD_WIDTH)));
#else
        return _simd_add_epi32(
            this->primID,
            _simd_set1_epi32(startID + this->primIDIncr * (this->numPrimsComplete / SIMD_WIDTH)));
#endif
    }
};

// helper C wrappers to avoid having to rewrite all the PA topology state functions
INLINE void SetNextPaState(PA_STATE_OPT&                    pa,
                           PA_STATE_OPT::PFN_PA_FUNC        pfnPaNextFunc,
                           PA_STATE_OPT::PFN_PA_SINGLE_FUNC pfnPaNextSingleFunc,
                           uint32_t                         numSimdPrims      = 0,
                           uint32_t                         numPrimsIncrement = 0,
                           bool                             reset             = false)
{
    return pa.SetNextState(
        pfnPaNextFunc, pfnPaNextSingleFunc, numSimdPrims, numPrimsIncrement, reset);
}

#if ENABLE_AVX512_SIMD16
INLINE void SetNextPaState_simd16(PA_STATE_OPT&                    pa,
                                  PA_STATE_OPT::PFN_PA_FUNC_SIMD16 pfnPaNextFunc_simd16,
                                  PA_STATE_OPT::PFN_PA_FUNC        pfnPaNextFunc,
                                  PA_STATE_OPT::PFN_PA_SINGLE_FUNC pfnPaNextSingleFunc,
                                  uint32_t                         numSimdPrims      = 0,
                                  uint32_t                         numPrimsIncrement = 0,
                                  bool                             reset             = false)
{
    return pa.SetNextState_simd16(pfnPaNextFunc_simd16,
                                  pfnPaNextFunc,
                                  pfnPaNextSingleFunc,
                                  numSimdPrims,
                                  numPrimsIncrement,
                                  reset);
}

#endif
INLINE simdvector& PaGetSimdVector(PA_STATE& pa, uint32_t index, uint32_t slot)
{
    return pa.GetSimdVector(index, slot);
}

#if ENABLE_AVX512_SIMD16
INLINE simd16vector& PaGetSimdVector_simd16(PA_STATE& pa, uint32_t index, uint32_t slot)
{
    return pa.GetSimdVector_simd16(index, slot);
}

#endif
// Cut-aware primitive assembler.
struct PA_STATE_CUT : public PA_STATE
{
    SIMDMASK* pCutIndices{nullptr};  // cut indices buffer, 1 bit per vertex
    uint32_t  numVerts{0};           // number of vertices available in buffer store
    uint32_t  numAttribs{0};         // number of attributes
    int32_t   numRemainingVerts{0};  // number of verts remaining to be assembled
    uint32_t  numVertsToAssemble{0}; // total number of verts to assemble for the draw
#if ENABLE_AVX512_SIMD16
    OSALIGNSIMD16(uint32_t)
    indices[MAX_NUM_VERTS_PER_PRIM][SIMD_WIDTH]; // current index buffer for gather
#else
    OSALIGNSIMD(uint32_t)
    indices[MAX_NUM_VERTS_PER_PRIM][SIMD_WIDTH]; // current index buffer for gather
#endif
    SIMDSCALARI vOffsets[MAX_NUM_VERTS_PER_PRIM]; // byte offsets for currently assembling simd
    uint32_t    numPrimsAssembled{0};             // number of primitives that are fully assembled
    uint32_t    headVertex{0};      // current unused vertex slot in vertex buffer store
    uint32_t    tailVertex{0};      // beginning vertex currently assembling
    uint32_t    curVertex{0};       // current unprocessed vertex
    uint32_t    startPrimId{0};     // starting prim id
    SIMDSCALARI vPrimId;            // vector of prim ID
    bool        needOffsets{false}; // need to compute gather offsets for current SIMD
    uint32_t    vertsPerPrim{0};
    bool        processCutVerts{
        false}; // vertex indices with cuts should be processed as normal, otherwise they
                // are ignored.  Fetch shader sends invalid verts on cuts that should be ignored
                // while the GS sends valid verts for every index

    simdvector junkVector; // junk simdvector for unimplemented API
#if ENABLE_AVX512_SIMD16
    simd16vector junkVector_simd16; // junk simd16vector for unimplemented API
#endif

    // Topology state tracking
    uint32_t vert[MAX_NUM_VERTS_PER_PRIM];
    uint32_t curIndex{0};
    bool     reverseWinding{false}; // indicates reverse winding for strips
    int32_t  adjExtraVert{0};       // extra vert uses for tristrip w/ adj

    typedef void (PA_STATE_CUT::*PFN_PA_FUNC)(uint32_t vert, bool finish);
    PFN_PA_FUNC pfnPa{nullptr}; // per-topology function that processes a single vert

    PA_STATE_CUT() {}
    PA_STATE_CUT(DRAW_CONTEXT*      pDC,
                 uint8_t*           in_pStream,
                 uint32_t           in_streamSizeInVerts,
                 uint32_t           in_vertexStride,
                 SIMDMASK*          in_pIndices,
                 uint32_t           in_numVerts,
                 uint32_t           in_numAttribs,
                 PRIMITIVE_TOPOLOGY topo,
                 bool               in_processCutVerts,
                 uint32_t           in_numVertsPerPrim) :
        PA_STATE(pDC, in_pStream, in_streamSizeInVerts, in_vertexStride, in_numVertsPerPrim)
    {
        numVerts        = in_streamSizeInVerts;
        numAttribs      = in_numAttribs;
        binTopology     = topo;
        needOffsets     = false;
        processCutVerts = in_processCutVerts;

        numVertsToAssemble = numRemainingVerts = in_numVerts;
        numPrimsAssembled                      = 0;
        headVertex = tailVertex = curVertex = 0;

        curIndex    = 0;
        pCutIndices = in_pIndices;
        memset(indices, 0, sizeof(indices));
#if USE_SIMD16_FRONTEND
        vPrimId = _simd16_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
#else
        vPrimId = _simd_set_epi32(7, 6, 5, 4, 3, 2, 1, 0);
#endif
        reverseWinding = false;
        adjExtraVert   = -1;

        bool gsEnabled = pDC->pState->state.gsState.gsEnable;
        vertsPerPrim   = NumVertsPerPrim(topo, gsEnabled);

        switch (topo)
        {
        case TOP_TRIANGLE_LIST:
            pfnPa = &PA_STATE_CUT::ProcessVertTriList;
            break;
        case TOP_TRI_LIST_ADJ:
            pfnPa = gsEnabled ? &PA_STATE_CUT::ProcessVertTriListAdj
                              : &PA_STATE_CUT::ProcessVertTriListAdjNoGs;
            break;
        case TOP_TRIANGLE_STRIP:
            pfnPa = &PA_STATE_CUT::ProcessVertTriStrip;
            break;
        case TOP_TRI_STRIP_ADJ:
            if (gsEnabled)
            {
                pfnPa = &PA_STATE_CUT::ProcessVertTriStripAdj<true>;
            }
            else
            {
                pfnPa = &PA_STATE_CUT::ProcessVertTriStripAdj<false>;
            }
            break;

        case TOP_POINT_LIST:
            pfnPa = &PA_STATE_CUT::ProcessVertPointList;
            break;
        case TOP_LINE_LIST:
            pfnPa = &PA_STATE_CUT::ProcessVertLineList;
            break;
        case TOP_LINE_LIST_ADJ:
            pfnPa = gsEnabled ? &PA_STATE_CUT::ProcessVertLineListAdj
                              : &PA_STATE_CUT::ProcessVertLineListAdjNoGs;
            break;
        case TOP_LINE_STRIP:
            pfnPa = &PA_STATE_CUT::ProcessVertLineStrip;
            break;
        case TOP_LISTSTRIP_ADJ:
            pfnPa = gsEnabled ? &PA_STATE_CUT::ProcessVertLineStripAdj
                              : &PA_STATE_CUT::ProcessVertLineStripAdjNoGs;
            break;
        case TOP_RECT_LIST:
            pfnPa = &PA_STATE_CUT::ProcessVertRectList;
            break;
        default:
            assert(0 && "Unimplemented topology");
        }
    }

    SIMDVERTEX& GetNextVsOutput()
    {
        uint32_t vertexIndex = this->headVertex / SIMD_WIDTH;
        this->headVertex     = (this->headVertex + SIMD_WIDTH) % this->numVerts;
        this->needOffsets    = true;
        SIMDVECTOR* pVertex  = &((SIMDVECTOR*)pStreamBase)[vertexIndex * vertexStride];

        return *(SIMDVERTEX*)pVertex;
    }

    SIMDMASK& GetNextVsIndices()
    {
        uint32_t  vertexIndex  = this->headVertex / SIMD_WIDTH;
        SIMDMASK* pCurCutIndex = this->pCutIndices + vertexIndex;
        return *pCurCutIndex;
    }

    simdvector& GetSimdVector(uint32_t index, uint32_t slot)
    {
        // unused
        SWR_ASSERT(0 && "Not implemented");
        return junkVector;
    }

#if ENABLE_AVX512_SIMD16
    simd16vector& GetSimdVector_simd16(uint32_t index, uint32_t slot)
    {
        // unused
        SWR_ASSERT(0 && "Not implemented");
        return junkVector_simd16;
    }

#endif
    bool GetNextStreamOutput()
    {
        this->headVertex += SIMD_WIDTH;
        this->needOffsets = true;
        return HasWork();
    }

    SIMDSCALARI GetPrimID(uint32_t startID)
    {
#if USE_SIMD16_FRONTEND
        return _simd16_add_epi32(_simd16_set1_epi32(startID), this->vPrimId);
#else
        return _simd_add_epi32(_simd_set1_epi32(startID), this->vPrimId);
#endif
    }

    void Reset()
    {
#if ENABLE_AVX512_SIMD16
        useAlternateOffset = false;

#endif
        this->numRemainingVerts = this->numVertsToAssemble;
        this->numPrimsAssembled = 0;
        this->curIndex          = 0;
        this->curVertex         = 0;
        this->tailVertex        = 0;
        this->headVertex        = 0;
        this->reverseWinding    = false;
        this->adjExtraVert      = -1;
#if USE_SIMD16_FRONTEND
        this->vPrimId = _simd16_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
#else
        this->vPrimId = _simd_set_epi32(7, 6, 5, 4, 3, 2, 1, 0);
#endif
    }

    bool HasWork() { return this->numRemainingVerts > 0 || this->adjExtraVert != -1; }

    bool IsVertexStoreFull()
    {
        return ((this->headVertex + SIMD_WIDTH) % this->numVerts) == this->tailVertex;
    }

    void RestartTopology()
    {
        this->curIndex       = 0;
        this->reverseWinding = false;
        this->adjExtraVert   = -1;
    }

    bool IsCutIndex(uint32_t vertex)
    {
        uint32_t vertexIndex  = vertex / SIMD_WIDTH;
        uint32_t vertexOffset = vertex & (SIMD_WIDTH - 1);
        return CheckBit(this->pCutIndices[vertexIndex], vertexOffset);
    }

    // iterates across the unprocessed verts until we hit the end or we
    // have assembled SIMD prims
    void ProcessVerts()
    {
        while (this->numPrimsAssembled != SIMD_WIDTH && this->numRemainingVerts > 0 &&
               this->curVertex != this->headVertex)
        {
            // if cut index, restart topology
            if (IsCutIndex(this->curVertex))
            {
                if (this->processCutVerts)
                {
                    (this->*pfnPa)(this->curVertex, false);
                }
                // finish off tri strip w/ adj before restarting topo
                if (this->adjExtraVert != -1)
                {
                    (this->*pfnPa)(this->curVertex, true);
                }
                RestartTopology();
            }
            else
            {
                (this->*pfnPa)(this->curVertex, false);
            }

            this->curVertex++;
            if (this->curVertex >= this->numVerts)
            {
                this->curVertex = 0;
            }
            this->numRemainingVerts--;
        }

        // special case last primitive for tri strip w/ adj
        if (this->numPrimsAssembled != SIMD_WIDTH && this->numRemainingVerts == 0 &&
            this->adjExtraVert != -1)
        {
            (this->*pfnPa)(this->curVertex, true);
        }
    }

    void Advance()
    {
        // done with current batch
        // advance tail to the current unsubmitted vertex
        this->tailVertex        = this->curVertex;
        this->numPrimsAssembled = 0;
#if USE_SIMD16_FRONTEND
        this->vPrimId = _simd16_add_epi32(vPrimId, _simd16_set1_epi32(SIMD_WIDTH));
#else
        this->vPrimId = _simd_add_epi32(vPrimId, _simd_set1_epi32(SIMD_WIDTH));
#endif
    }

    bool NextPrim()
    {
        // if we've assembled enough prims, we can advance to the next set of verts
        if (this->numPrimsAssembled == SIMD_WIDTH || this->numRemainingVerts <= 0)
        {
            Advance();
        }
        return false;
    }

    void ComputeOffsets()
    {
        for (uint32_t v = 0; v < this->vertsPerPrim; ++v)
        {
            uint32_t    vertexStrideBytes = vertexStride * sizeof(SIMDVECTOR);
            SIMDSCALARI vIndices          = *(SIMDSCALARI*)&this->indices[v][0];

            // step to simdvertex batch
            const uint32_t simdShift = SIMD_WIDTH_LOG2;
#if USE_SIMD16_FRONTEND
            SIMDSCALARI vVertexBatch = _simd16_srai_epi32(vIndices, simdShift);
            this->vOffsets[v] =
                _simd16_mullo_epi32(vVertexBatch, _simd16_set1_epi32(vertexStrideBytes));
#else
            SIMDSCALARI vVertexBatch = _simd_srai_epi32(vIndices, simdShift);
            this->vOffsets[v] =
                _simd_mullo_epi32(vVertexBatch, _simd_set1_epi32(vertexStrideBytes));
#endif

            // step to index
            const uint32_t simdMask = SIMD_WIDTH - 1;
#if USE_SIMD16_FRONTEND
            SIMDSCALARI vVertexIndex = _simd16_and_si(vIndices, _simd16_set1_epi32(simdMask));
            this->vOffsets[v]        = _simd16_add_epi32(
                this->vOffsets[v],
                _simd16_mullo_epi32(vVertexIndex, _simd16_set1_epi32(sizeof(float))));
#else
            SIMDSCALARI vVertexIndex = _simd_and_si(vIndices, _simd_set1_epi32(simdMask));
            this->vOffsets[v] =
                _simd_add_epi32(this->vOffsets[v],
                                _simd_mullo_epi32(vVertexIndex, _simd_set1_epi32(sizeof(float))));
#endif
        }
    }

    bool Assemble(uint32_t slot, simdvector* verts)
    {
        // process any outstanding verts
        ProcessVerts();

        // return false if we don't have enough prims assembled
        if (this->numPrimsAssembled != SIMD_WIDTH && this->numRemainingVerts > 0)
        {
            return false;
        }

        // cache off gather offsets given the current SIMD set of indices the first time we get an
        // assemble
        if (this->needOffsets)
        {
            ComputeOffsets();
            this->needOffsets = false;
        }

        for (uint32_t v = 0; v < this->vertsPerPrim; ++v)
        {
            SIMDSCALARI offsets = this->vOffsets[v];

            // step to attribute
#if USE_SIMD16_FRONTEND
            offsets = _simd16_add_epi32(offsets, _simd16_set1_epi32(slot * sizeof(SIMDVECTOR)));
#else
            offsets = _simd_add_epi32(offsets, _simd_set1_epi32(slot * sizeof(SIMDVECTOR)));
#endif

            float* pBase = (float*)this->pStreamBase;
            for (uint32_t c = 0; c < 4; ++c)
            {
#if USE_SIMD16_FRONTEND
                simd16scalar temp = _simd16_i32gather_ps(pBase, offsets, 1);

                // Assigning to a temporary first to avoid an MSVC 2017 compiler bug
                simdscalar t =
                    useAlternateOffset ? _simd16_extract_ps(temp, 1) : _simd16_extract_ps(temp, 0);
                verts[v].v[c] = t;
#else
                verts[v].v[c] = _simd_i32gather_ps(pBase, offsets, 1);
#endif

                // move base to next component
                pBase += SIMD_WIDTH;
            }
        }

        // compute the implied 4th vertex, v3
        if (this->binTopology == TOP_RECT_LIST)
        {
            for (uint32_t c = 0; c < 4; ++c)
            {
                // v1, v3 = v1 + v2 - v0, v2
                // v1 stored in verts[0], v0 stored in verts[1], v2 stored in verts[2]
                simd16scalar temp = _simd16_add_ps(verts[0].v[c], verts[2].v[c]);
                temp              = _simd16_sub_ps(temp, verts[1].v[c]);
                temp = _simd16_blend_ps(verts[1].v[c], temp, 0xAAAA); // 1010 1010 1010 1010
                verts[1].v[c] = _simd16_extract_ps(temp, 0);
            }
        }

        return true;
    }

#if ENABLE_AVX512_SIMD16
    bool Assemble(uint32_t slot, simd16vector verts[])
    {
       // process any outstanding verts
        ProcessVerts();

        // return false if we don't have enough prims assembled
        if (this->numPrimsAssembled != SIMD_WIDTH && this->numRemainingVerts > 0)
        {
            return false;
        }

        // cache off gather offsets given the current SIMD set of indices the first time we get an
        // assemble
        if (this->needOffsets)
        {
            ComputeOffsets();
            this->needOffsets = false;
        }

        for (uint32_t v = 0; v < this->vertsPerPrim; ++v)
        {
            SIMDSCALARI offsets = this->vOffsets[v];

            // step to attribute
#if USE_SIMD16_FRONTEND
            offsets = _simd16_add_epi32(offsets, _simd16_set1_epi32(slot * sizeof(SIMDVECTOR)));
#else
            offsets = _simd_add_epi32(offsets, _simd_set1_epi32(slot * sizeof(simdvector)));
#endif

            float* pBase = (float*)this->pStreamBase;
            for (uint32_t c = 0; c < 4; ++c)
            {
#if USE_SIMD16_FRONTEND
                verts[v].v[c] = _simd16_i32gather_ps(pBase, offsets, 1);
#else
                verts[v].v[c] = _simd16_insert_ps(
                    _simd16_setzero_ps(), _simd_i32gather_ps(pBase, offsets, 1), 0);
#endif

                // move base to next component
                pBase += SIMD_WIDTH;
            }
        }

        // compute the implied 4th vertex, v3
        if (this->binTopology == TOP_RECT_LIST)
        {
            for (uint32_t c = 0; c < 4; ++c)
            {
                // v1, v3 = v1 + v2 - v0, v2
                // v1 stored in verts[0], v0 stored in verts[1], v2 stored in verts[2]
                simd16scalar temp = _simd16_add_ps(verts[0].v[c], verts[2].v[c]);
                temp              = _simd16_sub_ps(temp, verts[1].v[c]);
                verts[1].v[c] =
                    _simd16_blend_ps(verts[1].v[c], temp, 0xAAAA); // 1010 1010 1010 1010
            }
        }

        return true;
    }

#endif
    void AssembleSingle(uint32_t slot, uint32_t triIndex, simd4scalar tri[3])
    {
       // move to slot
        for (uint32_t v = 0; v < this->vertsPerPrim; ++v)
        {
            uint32_t* pOffset = (uint32_t*)&this->vOffsets[v];
#if USE_SIMD16_FRONTEND
            uint32_t offset =
                useAlternateOffset ? pOffset[triIndex + SIMD_WIDTH_DIV2] : pOffset[triIndex];
#else
            uint32_t offset = pOffset[triIndex];
#endif
            offset += sizeof(SIMDVECTOR) * slot;
            float* pVert = (float*)&tri[v];
            for (uint32_t c = 0; c < 4; ++c)
            {
                float* pComponent = (float*)(this->pStreamBase + offset);
                pVert[c]          = *pComponent;
                offset += SIMD_WIDTH * sizeof(float);
            }
        }

        // compute the implied 4th vertex, v3
        if ((this->binTopology == TOP_RECT_LIST) && (triIndex % 2 == 1))
        {
            // v1, v3 = v1 + v2 - v0, v2
            // v1 stored in tri[0], v0 stored in tri[1], v2 stored in tri[2]
            float* pVert0 = (float*)&tri[1];
            float* pVert1 = (float*)&tri[0];
            float* pVert2 = (float*)&tri[2];
            float* pVert3 = (float*)&tri[1];
            for (uint32_t c = 0; c < 4; ++c)
            {
                pVert3[c] = pVert1[c] + pVert2[c] - pVert0[c];
            }
        }
    }

    uint32_t NumPrims() { return this->numPrimsAssembled; }

    // Per-topology functions
    void ProcessVertTriStrip(uint32_t index, bool finish)
    {
        this->vert[this->curIndex] = index;
        this->curIndex++;
        if (this->curIndex == 3)
        {
            // assembled enough verts for prim, add to gather indices
            this->indices[0][this->numPrimsAssembled] = this->vert[0];
            if (reverseWinding)
            {
                this->indices[1][this->numPrimsAssembled] = this->vert[2];
                this->indices[2][this->numPrimsAssembled] = this->vert[1];
            }
            else
            {
                this->indices[1][this->numPrimsAssembled] = this->vert[1];
                this->indices[2][this->numPrimsAssembled] = this->vert[2];
            }

            // increment numPrimsAssembled
            this->numPrimsAssembled++;

            // set up next prim state
            this->vert[0]  = this->vert[1];
            this->vert[1]  = this->vert[2];
            this->curIndex = 2;
            this->reverseWinding ^= 1;
        }
    }

    template <bool gsEnabled>
    void AssembleTriStripAdj()
    {
        if (!gsEnabled)
        {
            this->vert[1] = this->vert[2];
            this->vert[2] = this->vert[4];

            this->indices[0][this->numPrimsAssembled] = this->vert[0];
            this->indices[1][this->numPrimsAssembled] = this->vert[1];
            this->indices[2][this->numPrimsAssembled] = this->vert[2];

            this->vert[4] = this->vert[2];
            this->vert[2] = this->vert[1];
        }
        else
        {
            this->indices[0][this->numPrimsAssembled] = this->vert[0];
            this->indices[1][this->numPrimsAssembled] = this->vert[1];
            this->indices[2][this->numPrimsAssembled] = this->vert[2];
            this->indices[3][this->numPrimsAssembled] = this->vert[3];
            this->indices[4][this->numPrimsAssembled] = this->vert[4];
            this->indices[5][this->numPrimsAssembled] = this->vert[5];
        }
        this->numPrimsAssembled++;
    }

    template <bool gsEnabled>
    void ProcessVertTriStripAdj(uint32_t index, bool finish)
    {
        // handle last primitive of tristrip
        if (finish && this->adjExtraVert != -1)
        {
            this->vert[3] = this->adjExtraVert;
            AssembleTriStripAdj<gsEnabled>();
            this->adjExtraVert = -1;
            return;
        }

        switch (this->curIndex)
        {
        case 0:
        case 1:
        case 2:
        case 4:
            this->vert[this->curIndex] = index;
            this->curIndex++;
            break;
        case 3:
            this->vert[5] = index;
            this->curIndex++;
            break;
        case 5:
            if (this->adjExtraVert == -1)
            {
                this->adjExtraVert = index;
            }
            else
            {
                this->vert[3] = index;
                if (!gsEnabled)
                {
                    AssembleTriStripAdj<gsEnabled>();

                    uint32_t nextTri[6];
                    if (this->reverseWinding)
                    {
                        nextTri[0] = this->vert[4];
                        nextTri[1] = this->vert[0];
                        nextTri[2] = this->vert[2];
                        nextTri[4] = this->vert[3];
                        nextTri[5] = this->adjExtraVert;
                    }
                    else
                    {
                        nextTri[0] = this->vert[2];
                        nextTri[1] = this->adjExtraVert;
                        nextTri[2] = this->vert[3];
                        nextTri[4] = this->vert[4];
                        nextTri[5] = this->vert[0];
                    }
                    for (uint32_t i = 0; i < 6; ++i)
                    {
                        this->vert[i] = nextTri[i];
                    }

                    this->adjExtraVert = -1;
                    this->reverseWinding ^= 1;
                }
                else
                {
                    this->curIndex++;
                }
            }
            break;
        case 6:
            SWR_ASSERT(this->adjExtraVert != -1, "Algorithm failure!");
            AssembleTriStripAdj<gsEnabled>();

            uint32_t nextTri[6];
            if (this->reverseWinding)
            {
                nextTri[0] = this->vert[4];
                nextTri[1] = this->vert[0];
                nextTri[2] = this->vert[2];
                nextTri[4] = this->vert[3];
                nextTri[5] = this->adjExtraVert;
            }
            else
            {
                nextTri[0] = this->vert[2];
                nextTri[1] = this->adjExtraVert;
                nextTri[2] = this->vert[3];
                nextTri[4] = this->vert[4];
                nextTri[5] = this->vert[0];
            }
            for (uint32_t i = 0; i < 6; ++i)
            {
                this->vert[i] = nextTri[i];
            }
            this->reverseWinding ^= 1;
            this->adjExtraVert = index;
            this->curIndex--;
            break;
        }
    }

    void ProcessVertTriList(uint32_t index, bool finish)
    {
        this->vert[this->curIndex] = index;
        this->curIndex++;
        if (this->curIndex == 3)
        {
            // assembled enough verts for prim, add to gather indices
            this->indices[0][this->numPrimsAssembled] = this->vert[0];
            this->indices[1][this->numPrimsAssembled] = this->vert[1];
            this->indices[2][this->numPrimsAssembled] = this->vert[2];

            // increment numPrimsAssembled
            this->numPrimsAssembled++;

            // set up next prim state
            this->curIndex = 0;
        }
    }

    void ProcessVertTriListAdj(uint32_t index, bool finish)
    {
        this->vert[this->curIndex] = index;
        this->curIndex++;
        if (this->curIndex == 6)
        {
            // assembled enough verts for prim, add to gather indices
            this->indices[0][this->numPrimsAssembled] = this->vert[0];
            this->indices[1][this->numPrimsAssembled] = this->vert[1];
            this->indices[2][this->numPrimsAssembled] = this->vert[2];
            this->indices[3][this->numPrimsAssembled] = this->vert[3];
            this->indices[4][this->numPrimsAssembled] = this->vert[4];
            this->indices[5][this->numPrimsAssembled] = this->vert[5];

            // increment numPrimsAssembled
            this->numPrimsAssembled++;

            // set up next prim state
            this->curIndex = 0;
        }
    }

    void ProcessVertTriListAdjNoGs(uint32_t index, bool finish)
    {
        this->vert[this->curIndex] = index;
        this->curIndex++;
        if (this->curIndex == 6)
        {
            // assembled enough verts for prim, add to gather indices
            this->indices[0][this->numPrimsAssembled] = this->vert[0];
            this->indices[1][this->numPrimsAssembled] = this->vert[2];
            this->indices[2][this->numPrimsAssembled] = this->vert[4];

            // increment numPrimsAssembled
            this->numPrimsAssembled++;

            // set up next prim state
            this->curIndex = 0;
        }
    }

    void ProcessVertLineList(uint32_t index, bool finish)
    {
        this->vert[this->curIndex] = index;
        this->curIndex++;
        if (this->curIndex == 2)
        {
            this->indices[0][this->numPrimsAssembled] = this->vert[0];
            this->indices[1][this->numPrimsAssembled] = this->vert[1];

            this->numPrimsAssembled++;
            this->curIndex = 0;
        }
    }

    void ProcessVertLineStrip(uint32_t index, bool finish)
    {
        this->vert[this->curIndex] = index;
        this->curIndex++;
        if (this->curIndex == 2)
        {
            // assembled enough verts for prim, add to gather indices
            this->indices[0][this->numPrimsAssembled] = this->vert[0];
            this->indices[1][this->numPrimsAssembled] = this->vert[1];

            // increment numPrimsAssembled
            this->numPrimsAssembled++;

            // set up next prim state
            this->vert[0]  = this->vert[1];
            this->curIndex = 1;
        }
    }

    void ProcessVertLineStripAdj(uint32_t index, bool finish)
    {
        this->vert[this->curIndex] = index;
        this->curIndex++;
        if (this->curIndex == 4)
        {
            // assembled enough verts for prim, add to gather indices
            this->indices[0][this->numPrimsAssembled] = this->vert[0];
            this->indices[1][this->numPrimsAssembled] = this->vert[1];
            this->indices[2][this->numPrimsAssembled] = this->vert[2];
            this->indices[3][this->numPrimsAssembled] = this->vert[3];

            // increment numPrimsAssembled
            this->numPrimsAssembled++;

            // set up next prim state
            this->vert[0]  = this->vert[1];
            this->vert[1]  = this->vert[2];
            this->vert[2]  = this->vert[3];
            this->curIndex = 3;
        }
    }

    void ProcessVertLineStripAdjNoGs(uint32_t index, bool finish)
    {
        this->vert[this->curIndex] = index;
        this->curIndex++;
        if (this->curIndex == 4)
        {
            // assembled enough verts for prim, add to gather indices
            this->indices[0][this->numPrimsAssembled] = this->vert[1];
            this->indices[1][this->numPrimsAssembled] = this->vert[2];

            // increment numPrimsAssembled
            this->numPrimsAssembled++;

            // set up next prim state
            this->vert[0]  = this->vert[1];
            this->vert[1]  = this->vert[2];
            this->vert[2]  = this->vert[3];
            this->curIndex = 3;
        }
    }

    void ProcessVertLineListAdj(uint32_t index, bool finish)
    {
        this->vert[this->curIndex] = index;
        this->curIndex++;
        if (this->curIndex == 4)
        {
            this->indices[0][this->numPrimsAssembled] = this->vert[0];
            this->indices[1][this->numPrimsAssembled] = this->vert[1];
            this->indices[2][this->numPrimsAssembled] = this->vert[2];
            this->indices[3][this->numPrimsAssembled] = this->vert[3];

            this->numPrimsAssembled++;
            this->curIndex = 0;
        }
    }

    void ProcessVertLineListAdjNoGs(uint32_t index, bool finish)
    {
        this->vert[this->curIndex] = index;
        this->curIndex++;
        if (this->curIndex == 4)
        {
            this->indices[0][this->numPrimsAssembled] = this->vert[1];
            this->indices[1][this->numPrimsAssembled] = this->vert[2];

            this->numPrimsAssembled++;
            this->curIndex = 0;
        }
    }

    void ProcessVertPointList(uint32_t index, bool finish)
    {
        this->vert[this->curIndex] = index;
        this->curIndex++;
        if (this->curIndex == 1)
        {
            this->indices[0][this->numPrimsAssembled] = this->vert[0];
            this->numPrimsAssembled++;
            this->curIndex = 0;
        }
    }

    void ProcessVertRectList(uint32_t index, bool finish)
    {
        this->vert[this->curIndex] = index;
        this->curIndex++;
        if (this->curIndex == 3)
        {
            // assembled enough verts for prim, add to gather indices
            this->indices[0][this->numPrimsAssembled] = this->vert[0];
            this->indices[1][this->numPrimsAssembled] = this->vert[1];
            this->indices[2][this->numPrimsAssembled] = this->vert[2];

            // second triangle in the rectangle
            // v1, v3 = v1 + v2 - v0, v2
            this->indices[0][this->numPrimsAssembled + 1] = this->vert[1];
            this->indices[1][this->numPrimsAssembled + 1] = this->vert[0];
            this->indices[2][this->numPrimsAssembled + 1] = this->vert[2];

            // increment numPrimsAssembled
            this->numPrimsAssembled += 2;

            // set up next prim state
            this->curIndex = 0;
        }
    }
};

// Primitive Assembly for data output from the DomainShader.
struct PA_TESS : PA_STATE
{
    PA_TESS(DRAW_CONTEXT*     in_pDC,
            const SIMDSCALAR* in_pVertData,
            uint32_t          in_attributeStrideInVectors,
            uint32_t          in_vertexStride,
            uint32_t          in_numAttributes,
            uint32_t* (&in_ppIndices)[3],
            uint32_t           in_numPrims,
            PRIMITIVE_TOPOLOGY in_binTopology,
            uint32_t           numVertsPerPrim,
            bool               SOA = true) :

        PA_STATE(in_pDC, nullptr, 0, in_vertexStride, numVertsPerPrim),
        m_pVertexData(in_pVertData), m_attributeStrideInVectors(in_attributeStrideInVectors),
        m_numAttributes(in_numAttributes), m_numPrims(in_numPrims), m_SOA(SOA)
    {
#if USE_SIMD16_FRONTEND
        m_vPrimId = _simd16_setzero_si();
#else
        m_vPrimId = _simd_setzero_si();
#endif
        binTopology    = in_binTopology;
        m_ppIndices[0] = in_ppIndices[0];
        m_ppIndices[1] = in_ppIndices[1];
        m_ppIndices[2] = in_ppIndices[2];

        switch (binTopology)
        {
        case TOP_POINT_LIST:
            m_numVertsPerPrim = 1;
            break;

        case TOP_LINE_LIST:
            m_numVertsPerPrim = 2;
            break;

        case TOP_TRIANGLE_LIST:
            m_numVertsPerPrim = 3;
            break;

        default:
            SWR_INVALID("Invalid binTopology (%d) for %s", binTopology, __FUNCTION__);
            break;
        }
    }

    bool HasWork() { return m_numPrims != 0; }

    simdvector& GetSimdVector(uint32_t index, uint32_t slot)
    {
        SWR_INVALID("%s NOT IMPLEMENTED", __FUNCTION__);
        return junkVector;
    }

#if ENABLE_AVX512_SIMD16
    simd16vector& GetSimdVector_simd16(uint32_t index, uint32_t slot)
    {
        SWR_INVALID("%s NOT IMPLEMENTED", __FUNCTION__);
        return junkVector_simd16;
    }

#endif
    static SIMDSCALARI GenPrimMask(uint32_t numPrims)
    {
        SWR_ASSERT(numPrims <= SIMD_WIDTH);
#if USE_SIMD16_FRONTEND
        static const OSALIGNLINE(int32_t) maskGen[SIMD_WIDTH * 2] = {
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};

        return _simd16_loadu_si((const SIMDSCALARI*)&maskGen[SIMD_WIDTH - numPrims]);
#else
        static const OSALIGNLINE(int32_t)
            maskGen[SIMD_WIDTH * 2] = {-1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0};

        return _simd_loadu_si((const SIMDSCALARI*)&maskGen[SIMD_WIDTH - numPrims]);
#endif
    }

    bool Assemble(uint32_t slot, simdvector verts[])
    {
        SWR_ASSERT(slot < m_numAttributes);

        uint32_t numPrimsToAssemble = PA_TESS::NumPrims();
        if (0 == numPrimsToAssemble)
        {
            return false;
        }

        SIMDSCALARI mask = GenPrimMask(numPrimsToAssemble);

        const float* pBaseAttrib;
        if (m_SOA)
        {
            pBaseAttrib = (const float*)&m_pVertexData[slot * m_attributeStrideInVectors * 4];
        }
        else
        {
            const float* pVertData = (const float*)m_pVertexData;
            pBaseAttrib            = pVertData + slot * 4;
        }

        for (uint32_t i = 0; i < m_numVertsPerPrim; ++i)
        {
#if USE_SIMD16_FRONTEND
            SIMDSCALARI indices = _simd16_load_si((const SIMDSCALARI*)m_ppIndices[i]);
#else
            SIMDSCALARI indices = _simd_load_si((const SIMDSCALARI*)m_ppIndices[i]);
#endif

            const float* pBase = pBaseAttrib;
            for (uint32_t c = 0; c < 4; ++c)
            {
#if USE_SIMD16_FRONTEND
                simd16scalar temp =
                    _simd16_mask_i32gather_ps(_simd16_setzero_ps(),
                                              pBase,
                                              indices,
                                              _simd16_castsi_ps(mask),
                                              4 /* gcc doesn't like sizeof(float) */);

                verts[i].v[c] =
                    useAlternateOffset ? _simd16_extract_ps(temp, 1) : _simd16_extract_ps(temp, 0);
#else
                verts[i].v[c] = _simd_mask_i32gather_ps(_simd_setzero_ps(),
                                                        pBase,
                                                        indices,
                                                        _simd_castsi_ps(mask),
                                                        4); // gcc doesn't like sizeof(float)
#endif
                if (m_SOA)
                {
                    pBase += m_attributeStrideInVectors * SIMD_WIDTH;
                }
                else
                {
                    pBase += sizeof(float);
                }
            }
        }

        return true;
    }

#if ENABLE_AVX512_SIMD16
    bool Assemble(uint32_t slot, simd16vector verts[])
    {
        SWR_ASSERT(slot < m_numAttributes);

        uint32_t numPrimsToAssemble = PA_TESS::NumPrims();
        if (0 == numPrimsToAssemble)
        {
            return false;
        }

        SIMDSCALARI mask = GenPrimMask(numPrimsToAssemble);

        const float* pBaseAttrib;
        if (m_SOA)
        {
            pBaseAttrib = (const float*)&m_pVertexData[slot * m_attributeStrideInVectors * 4];
        }
        else
        {
            const float* pVertData = (const float*)m_pVertexData;
            pBaseAttrib            = pVertData + slot * 4;
        }

        for (uint32_t i = 0; i < m_numVertsPerPrim; ++i)
        {
#if USE_SIMD16_FRONTEND
            SIMDSCALARI indices = _simd16_load_si((const SIMDSCALARI*)m_ppIndices[i]);
            if (!m_SOA)
            {
                indices = _simd16_mullo_epi32(indices, _simd16_set1_epi32(vertexStride / 4));
            }
#else
            SIMDSCALARI indices = _simd_load_si((const SIMDSCALARI*)m_ppIndices[i]);
#endif

            const float* pBase = pBaseAttrib;
            for (uint32_t c = 0; c < 4; ++c)
            {
#if USE_SIMD16_FRONTEND
                verts[i].v[c] = _simd16_mask_i32gather_ps(_simd16_setzero_ps(),
                                                          pBase,
                                                          indices,
                                                          _simd16_castsi_ps(mask),
                                                          4 /* gcc doesn't like sizeof(float) */);
#else
                simdscalar temp = _simd_mask_i32gather_ps(_simd_setzero_ps(),
                                                          pBase,
                                                          indices,
                                                          _simd_castsi_ps(mask),
                                                          4 /* gcc doesn't like sizeof(float) */);
                verts[i].v[c]   = _simd16_insert_ps(_simd16_setzero_ps(), temp, 0);
#endif
                if (m_SOA)
                {
                    pBase += m_attributeStrideInVectors * SIMD_WIDTH;
                }
                else
                {
                    pBase++;
                }
            }
        }

        return true;
    }

#endif
    void AssembleSingle(uint32_t slot, uint32_t primIndex, simd4scalar verts[])
    {
        SWR_ASSERT(slot < m_numAttributes);


        SWR_ASSERT(primIndex < PA_TESS::NumPrims());

        const float* pVertDataBase;
        if (m_SOA)
        {
            pVertDataBase = (const float*)&m_pVertexData[slot * m_attributeStrideInVectors * 4];
        }
        else
        {
            const float* pVertData = (const float*)m_pVertexData;
            pVertDataBase          = pVertData + slot * 4;
        };
        for (uint32_t i = 0; i < m_numVertsPerPrim; ++i)
        {
#if USE_SIMD16_FRONTEND
            uint32_t index = useAlternateOffset ? m_ppIndices[i][primIndex + SIMD_WIDTH_DIV2]
                                                : m_ppIndices[i][primIndex];
            if (!m_SOA)
            {
                index *= (vertexStride / 4);
            }
#else
            uint32_t index = m_ppIndices[i][primIndex];
#endif
            const float* pVertData = pVertDataBase;
            float*       pVert     = (float*)&verts[i];

            for (uint32_t c = 0; c < 4; ++c)
            {
                pVert[c] = pVertData[index];
                if (m_SOA)
                {
                    pVertData += m_attributeStrideInVectors * SIMD_WIDTH;
                }
                else
                {
                    pVertData++;
                }
            }

        }
    }

    bool NextPrim()
    {
        uint32_t numPrims = PA_TESS::NumPrims();
        m_numPrims -= numPrims;
        m_ppIndices[0] += numPrims;
        m_ppIndices[1] += numPrims;
        m_ppIndices[2] += numPrims;

        return HasWork();
    }

    SIMDVERTEX& GetNextVsOutput()
    {
        SWR_NOT_IMPL;
        return junkVertex;
    }

    bool GetNextStreamOutput()
    {
        SWR_NOT_IMPL;
        return false;
    }

    SIMDMASK& GetNextVsIndices()
    {
        SWR_NOT_IMPL;
        return junkIndices;
    }

    uint32_t NumPrims() { return std::min<uint32_t>(m_numPrims, SIMD_WIDTH); }

    void Reset() { SWR_NOT_IMPL; }

    SIMDSCALARI GetPrimID(uint32_t startID)
    {
#if USE_SIMD16_FRONTEND
        return _simd16_add_epi32(_simd16_set1_epi32(startID), m_vPrimId);
#else
        return _simd_add_epi32(_simd_set1_epi32(startID), m_vPrimId);
#endif
    }

private:
    const SIMDSCALAR* m_pVertexData              = nullptr;
    uint32_t          m_attributeStrideInVectors = 0;
    uint32_t          m_numAttributes            = 0;
    uint32_t          m_numPrims                 = 0;
    uint32_t*         m_ppIndices[3];

    uint32_t m_numVertsPerPrim = 0;

    SIMDSCALARI m_vPrimId;

    simdvector junkVector; // junk simdvector for unimplemented API
#if ENABLE_AVX512_SIMD16
    simd16vector junkVector_simd16; // junk simd16vector for unimplemented API
#endif
    SIMDVERTEX junkVertex;  // junk SIMDVERTEX for unimplemented API
    SIMDMASK   junkIndices; // temporary index store for unused virtual function

    bool m_SOA;
};

// Primitive Assembler factory class, responsible for creating and initializing the correct
// assembler based on state.
template <typename IsIndexedT, typename IsCutIndexEnabledT>
struct PA_FACTORY
{
    PA_FACTORY(DRAW_CONTEXT*         pDC,
               PRIMITIVE_TOPOLOGY    in_topo,
               uint32_t              numVerts,
               PA_STATE::SIMDVERTEX* pVertexStore,
               uint32_t              vertexStoreSize,
               uint32_t              vertexStride,
               uint32_t              numVertsPerPrim) :
        topo(in_topo)
    {
#if KNOB_ENABLE_CUT_AWARE_PA == TRUE
        const API_STATE& state = GetApiState(pDC);
        if ((IsIndexedT::value && IsCutIndexEnabledT::value &&
             (topo == TOP_TRIANGLE_STRIP || topo == TOP_POINT_LIST || topo == TOP_LINE_LIST ||
              topo == TOP_LINE_STRIP || topo == TOP_TRIANGLE_LIST)) ||

            // non-indexed draws with adjacency topologies must use cut-aware PA until we add
            // support for them in the optimized PA
            (topo == TOP_LINE_LIST_ADJ || topo == TOP_LISTSTRIP_ADJ || topo == TOP_TRI_LIST_ADJ ||
             topo == TOP_TRI_STRIP_ADJ))
        {
            memset(&indexStore, 0, sizeof(indexStore));
            uint32_t numAttribs = state.feNumAttributes;

            new (&this->paCut) PA_STATE_CUT(pDC,
                                            reinterpret_cast<uint8_t*>(pVertexStore),
                                            vertexStoreSize * PA_STATE::SIMD_WIDTH,
                                            vertexStride,
                                            &this->indexStore[0],
                                            numVerts,
                                            numAttribs,
                                            state.topology,
                                            false,
                                            numVertsPerPrim);
            cutPA = true;
        }
        else
#endif
        {
            uint32_t numPrims = GetNumPrims(in_topo, numVerts);
            new (&this->paOpt) PA_STATE_OPT(pDC,
                                            numPrims,
                                            reinterpret_cast<uint8_t*>(pVertexStore),
                                            vertexStoreSize * PA_STATE::SIMD_WIDTH,
                                            vertexStride,
                                            false,
                                            numVertsPerPrim);
            cutPA = false;
        }
    }

    PA_STATE& GetPA()
    {
#if KNOB_ENABLE_CUT_AWARE_PA == TRUE
        if (cutPA)
        {
            return this->paCut;
        }
        else
#endif
        {
            return this->paOpt;
        }
    }

    PA_STATE_OPT paOpt;
    PA_STATE_CUT paCut;

    bool cutPA{false};

    PRIMITIVE_TOPOLOGY topo{TOP_UNKNOWN};

    PA_STATE::SIMDMASK indexStore[MAX_NUM_VERTS_PER_PRIM];
};
