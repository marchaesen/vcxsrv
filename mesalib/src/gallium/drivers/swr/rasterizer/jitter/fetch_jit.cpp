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
 * @file fetch_jit.cpp
 *
 * @brief Implementation of the fetch jitter
 *
 * Notes:
 *
 ******************************************************************************/
#include "jit_pch.hpp"
#include "builder_gfx_mem.h"
#include "jit_api.h"
#include "fetch_jit.h"
#include "gen_state_llvm.h"
#include "functionpasses/passes.h"

//#define FETCH_DUMP_VERTEX 1
using namespace llvm;
using namespace SwrJit;

bool isComponentEnabled(ComponentEnable enableMask, uint8_t component);

enum ConversionType
{
    CONVERT_NONE,
    CONVERT_NORMALIZED,
    CONVERT_USCALED,
    CONVERT_SSCALED,
    CONVERT_SFIXED,
};

//////////////////////////////////////////////////////////////////////////
/// Interface to Jitting a fetch shader
//////////////////////////////////////////////////////////////////////////
struct FetchJit : public BuilderGfxMem
{
    FetchJit(JitManager* pJitMgr) : BuilderGfxMem(pJitMgr), mpFetchInfo(NULL) {}

    Function* Create(const FETCH_COMPILE_STATE& fetchState);

    Value* GetSimdValid32bitIndices(Value* vIndices, Value* pLastIndex);
    Value* GetSimdValid16bitIndices(Value* vIndices, Value* pLastIndex);
    Value* GetSimdValid8bitIndices(Value* vIndices, Value* pLastIndex);
    template <typename T>
    Value* GetSimdValidIndicesHelper(Value* pIndices, Value* pLastIndex);

    // package up Shuffle*bpcGatherd args into a tuple for convenience
    typedef std::tuple<Value*&,
                       Value*,
                       const Instruction::CastOps,
                       const ConversionType,
                       uint32_t&,
                       uint32_t&,
                       const ComponentEnable,
                       const ComponentControl (&)[4],
                       Value* (&)[4],
                       const uint32_t (&)[4]>
        Shuffle8bpcArgs;

    void Shuffle8bpcGatherd16(Shuffle8bpcArgs& args);
    void Shuffle8bpcGatherd(Shuffle8bpcArgs& args);

    typedef std::tuple<Value* (&)[2],
                       Value*,
                       const Instruction::CastOps,
                       const ConversionType,
                       uint32_t&,
                       uint32_t&,
                       const ComponentEnable,
                       const ComponentControl (&)[4],
                       Value* (&)[4]>
        Shuffle16bpcArgs;

    void Shuffle16bpcGather16(Shuffle16bpcArgs& args);
    void Shuffle16bpcGather(Shuffle16bpcArgs& args);

    void StoreVertexElements(Value*         pVtxOut,
                             const uint32_t outputElt,
                             const uint32_t numEltsToStore,
                             Value* (&vVertexElements)[4]);

    Value* GenerateCompCtrlVector(const ComponentControl ctrl);

    void JitGatherVertices(const FETCH_COMPILE_STATE& fetchState,
                           Value*                     streams,
                           Value*                     vIndices,
                           Value*                     pVtxOut);

    bool IsOddFormat(SWR_FORMAT format);
    bool IsUniformFormat(SWR_FORMAT format);
    void UnpackComponents(SWR_FORMAT format, Value* vInput, Value* result[4]);
    void CreateGatherOddFormats(
        SWR_FORMAT format, Value* pMask, Value* pBase, Value* offsets, Value* result[4]);
    void ConvertFormat(SWR_FORMAT format, Value* texels[4]);

    Value* mpFetchInfo;
};

Function* FetchJit::Create(const FETCH_COMPILE_STATE& fetchState)
{
    std::stringstream fnName("FCH_", std::ios_base::in | std::ios_base::out | std::ios_base::ate);
    fnName << ComputeCRC(0, &fetchState, sizeof(fetchState));

    Function* fetch = Function::Create(
        JM()->mFetchShaderTy, GlobalValue::ExternalLinkage, fnName.str(), JM()->mpCurrentModule);
    BasicBlock* entry = BasicBlock::Create(JM()->mContext, "entry", fetch);

    fetch->getParent()->setModuleIdentifier(fetch->getName());

    IRB()->SetInsertPoint(entry);

    auto argitr = fetch->arg_begin();

    // Fetch shader arguments
    Value* privateContext = &*argitr;
    ++argitr;
    privateContext->setName("privateContext");
    SetPrivateContext(privateContext);

    mpWorkerData = &*argitr;
    ++argitr;
    mpWorkerData->setName("pWorkerData");

    mpFetchInfo = &*argitr;
    ++argitr;
    mpFetchInfo->setName("fetchInfo");
    Value* pVtxOut = &*argitr;
    pVtxOut->setName("vtxOutput");

    uint32_t baseWidth = mVWidth;

    SWR_ASSERT(mVWidth == 8 || mVWidth == 16, "Unsupported vector width %d", mVWidth);

    // Override builder target width to force 16-wide SIMD
#if USE_SIMD16_SHADERS
    SetTargetWidth(16);
#endif

    pVtxOut = BITCAST(pVtxOut, PointerType::get(mSimdFP32Ty, 0));

    // SWR_FETCH_CONTEXT::pStreams
    Value* streams = LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_pStreams});
    streams->setName("pStreams");

    // SWR_FETCH_CONTEXT::pIndices
    Value* indices = LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_xpIndices});
    indices->setName("pIndices");

    // SWR_FETCH_CONTEXT::pLastIndex
    Value* pLastIndex = LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_xpLastIndex});
    pLastIndex->setName("pLastIndex");

    Value* vIndices;
    switch (fetchState.indexType)
    {
    case R8_UINT:
        indices = BITCAST(indices, Type::getInt8PtrTy(JM()->mContext, 0));
        if (fetchState.bDisableIndexOOBCheck)
        {
            vIndices = LOAD(
                BITCAST(indices, PointerType::get(getVectorType(mInt8Ty, mpJitMgr->mVWidth), 0)),
                {(uint32_t)0});
            vIndices = Z_EXT(vIndices, mSimdInt32Ty);
        }
        else
        {
            vIndices = GetSimdValid8bitIndices(indices, pLastIndex);
        }
        break;
    case R16_UINT:
        if (fetchState.bDisableIndexOOBCheck)
        {
            vIndices = LOAD(
                BITCAST(indices, PointerType::get(getVectorType(mInt16Ty, mpJitMgr->mVWidth), 0)),
                {(uint32_t)0});
            vIndices = Z_EXT(vIndices, mSimdInt32Ty);
        }
        else
        {
            vIndices = GetSimdValid16bitIndices(indices, pLastIndex);
        }
        break;
    case R32_UINT:
        (fetchState.bDisableIndexOOBCheck)
            ? vIndices = LOAD(indices,
                              "",
                              PointerType::get(mSimdInt32Ty, 0),
                              MEM_CLIENT::GFX_MEM_CLIENT_FETCH)
            : vIndices = GetSimdValid32bitIndices(indices, pLastIndex);
        break; // incoming type is already 32bit int
    default:
        vIndices = nullptr;
        assert(false && "Unsupported index type");
        break;
    }

    if (fetchState.bForceSequentialAccessEnable)
    {
        Value* pOffsets = mVWidth == 8 ? C({0, 1, 2, 3, 4, 5, 6, 7})
                                       : C({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});

        // VertexData buffers are accessed sequentially, the index is equal to the vertex number
        vIndices = VBROADCAST(LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_StartVertex}));
        vIndices = ADD(vIndices, pOffsets);
    }

    Value* vVertexId = vIndices;
    if (fetchState.bVertexIDOffsetEnable)
    {
        // Assuming one of baseVertex or startVertex is 0, so adding both should be functionally
        // correct
        Value* vBaseVertex  = VBROADCAST(LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_BaseVertex}));
        Value* vStartVertex = VBROADCAST(LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_StartVertex}));
        vVertexId           = ADD(vIndices, vBaseVertex);
        vVertexId           = ADD(vVertexId, vStartVertex);
    }

    // store out vertex IDs
    if (mVWidth == 16)
    {
        // store out in simd8 halves until core supports 16-wide natively
        auto vVertexIdLo = EXTRACT_16(vVertexId, 0);
        auto vVertexIdHi = EXTRACT_16(vVertexId, 1);
        STORE(vVertexIdLo, GEP(mpFetchInfo, {0, SWR_FETCH_CONTEXT_VertexID}));
        STORE(vVertexIdHi, GEP(mpFetchInfo, {0, SWR_FETCH_CONTEXT_VertexID2}));
    }
    else if (mVWidth == 8)
    {
        STORE(vVertexId, GEP(mpFetchInfo, {0, SWR_FETCH_CONTEXT_VertexID}));
    }

    // store out cut mask if enabled
    if (fetchState.bEnableCutIndex)
    {
        Value* vCutIndex = VIMMED1(fetchState.cutIndex);
        Value* cutMask   = VMASK(ICMP_EQ(vIndices, vCutIndex));

        if (mVWidth == 16)
        {
            auto cutMaskLo = EXTRACT_16(cutMask, 0);
            auto cutMaskHi = EXTRACT_16(cutMask, 1);
            STORE(cutMaskLo, GEP(mpFetchInfo, {0, SWR_FETCH_CONTEXT_CutMask}));
            STORE(cutMaskHi, GEP(mpFetchInfo, {0, SWR_FETCH_CONTEXT_CutMask2}));
        }
        else if (mVWidth == 8)
        {
            STORE(cutMask, GEP(mpFetchInfo, {0, SWR_FETCH_CONTEXT_CutMask}));
        }
    }

    // Fetch attributes from memory and output to a simdvertex struct
    JitGatherVertices(fetchState, streams, vIndices, pVtxOut);

    RET_VOID();

    JitManager::DumpToFile(fetch, "src");

#if defined(_DEBUG)
    verifyFunction(*fetch);
#endif

    ::FunctionPassManager setupPasses(JM()->mpCurrentModule);

    ///@todo We don't need the CFG passes for fetch. (e.g. BreakCriticalEdges and CFGSimplification)
    setupPasses.add(createBreakCriticalEdgesPass());
    setupPasses.add(createCFGSimplificationPass());
    setupPasses.add(createEarlyCSEPass());
    setupPasses.add(createPromoteMemoryToRegisterPass());

    setupPasses.run(*fetch);

    JitManager::DumpToFile(fetch, "se");

    ::FunctionPassManager optPasses(JM()->mpCurrentModule);

    ///@todo Haven't touched these either. Need to remove some of these and add others.
    optPasses.add(createCFGSimplificationPass());
    optPasses.add(createEarlyCSEPass());
    optPasses.add(createInstructionCombiningPass());
#if LLVM_VERSION_MAJOR <= 11
    optPasses.add(createConstantPropagationPass());
#endif
    optPasses.add(createSCCPPass());
    optPasses.add(createAggressiveDCEPass());

    optPasses.run(*fetch);

    optPasses.add(createLowerX86Pass(this));
    optPasses.run(*fetch);

    JitManager::DumpToFile(fetch, "opt");


    // Revert 16-wide override
#if USE_SIMD16_SHADERS
    SetTargetWidth(baseWidth);
#endif

    return fetch;
}

// returns true for odd formats that require special state.gather handling
bool FetchJit::IsOddFormat(SWR_FORMAT format)
{
    const SWR_FORMAT_INFO& info = GetFormatInfo(format);
    if (info.bpc[0] != 8 && info.bpc[0] != 16 && info.bpc[0] != 32 && info.bpc[0] != 64)
    {
        return true;
    }
    return false;
}

// format is uniform if all components are the same size and type
bool FetchJit::IsUniformFormat(SWR_FORMAT format)
{
    const SWR_FORMAT_INFO& info  = GetFormatInfo(format);
    uint32_t               bpc0  = info.bpc[0];
    uint32_t               type0 = info.type[0];

    for (uint32_t c = 1; c < info.numComps; ++c)
    {
        if (bpc0 != info.bpc[c] || type0 != info.type[c])
        {
            return false;
        }
    }
    return true;
}

// unpacks components based on format
// foreach component in the pixel
//   mask off everything but this component
//   shift component to LSB
void FetchJit::UnpackComponents(SWR_FORMAT format, Value* vInput, Value* result[4])
{
    const SWR_FORMAT_INFO& info = GetFormatInfo(format);

    uint32_t bitOffset = 0;
    for (uint32_t c = 0; c < info.numComps; ++c)
    {
        uint32_t swizzledIndex = info.swizzle[c];
        uint32_t compBits      = info.bpc[c];
        uint32_t bitmask       = ((1 << compBits) - 1) << bitOffset;
        Value*   comp          = AND(vInput, bitmask);
        comp                   = LSHR(comp, bitOffset);

        result[swizzledIndex] = comp;
        bitOffset += compBits;
    }
}

// gather for odd component size formats
// gather SIMD full pixels per lane then shift/mask to move each component to their
// own vector
void FetchJit::CreateGatherOddFormats(
    SWR_FORMAT format, Value* pMask, Value* xpBase, Value* pOffsets, Value* pResult[4])
{
    const SWR_FORMAT_INFO& info = GetFormatInfo(format);

    // only works if pixel size is <= 32bits
    SWR_ASSERT(info.bpp <= 32);

    Value* pGather;
    if (info.bpp == 32)
    {
        pGather =
            GATHERDD(VIMMED1(0), xpBase, pOffsets, pMask, 1, MEM_CLIENT::GFX_MEM_CLIENT_FETCH);
    }
    else
    {
        // Can't use 32-bit gather for items less than 32-bits, could cause page faults.
        Value* pMem = ALLOCA(mSimdInt32Ty);
        STORE(VIMMED1(0u), pMem);

        Value* pDstMem = POINTER_CAST(pMem, mInt32PtrTy);

        for (uint32_t lane = 0; lane < mVWidth; ++lane)
        {
            // Get index
            Value* index = VEXTRACT(pOffsets, C(lane));
            Value* mask  = VEXTRACT(pMask, C(lane));

            // use branch around load based on mask
            // Needed to avoid page-faults on unmasked lanes
            BasicBlock* pCurrentBB = IRB()->GetInsertBlock();
            BasicBlock* pMaskedLoadBlock =
                BasicBlock::Create(JM()->mContext, "MaskedLaneLoad", pCurrentBB->getParent());
            BasicBlock* pEndLoadBB =
                BasicBlock::Create(JM()->mContext, "AfterMaskedLoad", pCurrentBB->getParent());

            COND_BR(mask, pMaskedLoadBlock, pEndLoadBB);

            JM()->mBuilder.SetInsertPoint(pMaskedLoadBlock);

            switch (info.bpp)
            {
            case 8:
            {
                Value* pDst  = BITCAST(GEP(pDstMem, C(lane)), PointerType::get(mInt8Ty, 0));
                Value* xpSrc = ADD(xpBase, Z_EXT(index, xpBase->getType()));
                STORE(LOAD(xpSrc, "", mInt8PtrTy, MEM_CLIENT::GFX_MEM_CLIENT_FETCH), pDst);
                break;
            }

            case 16:
            {
                Value* pDst  = BITCAST(GEP(pDstMem, C(lane)), PointerType::get(mInt16Ty, 0));
                Value* xpSrc = ADD(xpBase, Z_EXT(index, xpBase->getType()));
                STORE(LOAD(xpSrc, "", mInt16PtrTy, MEM_CLIENT::GFX_MEM_CLIENT_FETCH), pDst);
                break;
            }
            break;

            case 24:
            {
                // First 16-bits of data
                Value* pDst  = BITCAST(GEP(pDstMem, C(lane)), PointerType::get(mInt16Ty, 0));
                Value* xpSrc = ADD(xpBase, Z_EXT(index, xpBase->getType()));
                STORE(LOAD(xpSrc, "", mInt16PtrTy, MEM_CLIENT::GFX_MEM_CLIENT_FETCH), pDst);

                // Last 8-bits of data
                pDst  = BITCAST(GEP(pDst, C(1)), PointerType::get(mInt8Ty, 0));
                xpSrc = ADD(xpSrc, C((int64_t)2));
                STORE(LOAD(xpSrc, "", mInt8PtrTy, MEM_CLIENT::GFX_MEM_CLIENT_FETCH), pDst);
                break;
            }

            default:
                SWR_INVALID("Shouldn't have BPP = %d now", info.bpp);
                break;
            }

            BR(pEndLoadBB);
            JM()->mBuilder.SetInsertPoint(pEndLoadBB);
        }

        pGather = LOAD(pMem);
    }

    for (uint32_t comp = 0; comp < 4; ++comp)
    {
        pResult[comp] = VIMMED1((int)info.defaults[comp]);
    }

    UnpackComponents(format, pGather, pResult);

    // cast to fp32
    pResult[0] = BITCAST(pResult[0], mSimdFP32Ty);
    pResult[1] = BITCAST(pResult[1], mSimdFP32Ty);
    pResult[2] = BITCAST(pResult[2], mSimdFP32Ty);
    pResult[3] = BITCAST(pResult[3], mSimdFP32Ty);
}

void FetchJit::ConvertFormat(SWR_FORMAT format, Value* texels[4])
{
    const SWR_FORMAT_INFO& info = GetFormatInfo(format);

    for (uint32_t c = 0; c < info.numComps; ++c)
    {
        uint32_t compIndex = info.swizzle[c];

        // skip any conversion on UNUSED components
        if (info.type[c] == SWR_TYPE_UNUSED)
        {
            continue;
        }

        if (info.isNormalized[c])
        {
            if (info.type[c] == SWR_TYPE_SNORM)
            {
                /// @todo The most-negative value maps to -1.0f. e.g. the 5-bit value 10000 maps to
                /// -1.0f.

                /// result = c * (1.0f / (2^(n-1) - 1);
                uint32_t n        = info.bpc[c];
                uint32_t pow2     = 1 << (n - 1);
                float    scale    = 1.0f / (float)(pow2 - 1);
                Value*   vScale   = VIMMED1(scale);
                texels[compIndex] = BITCAST(texels[compIndex], mSimdInt32Ty);
                texels[compIndex] = SI_TO_FP(texels[compIndex], mSimdFP32Ty);
                texels[compIndex] = FMUL(texels[compIndex], vScale);
            }
            else
            {
                SWR_ASSERT(info.type[c] == SWR_TYPE_UNORM);

                /// result = c * (1.0f / (2^n - 1))
                uint32_t n    = info.bpc[c];
                uint32_t pow2 = 1 << n;
                // special case 24bit unorm format, which requires a full divide to meet ULP
                // requirement
                if (n == 24)
                {
                    float  scale      = (float)(pow2 - 1);
                    Value* vScale     = VIMMED1(scale);
                    texels[compIndex] = BITCAST(texels[compIndex], mSimdInt32Ty);
                    texels[compIndex] = SI_TO_FP(texels[compIndex], mSimdFP32Ty);
                    texels[compIndex] = FDIV(texels[compIndex], vScale);
                }
                else
                {
                    float  scale      = 1.0f / (float)(pow2 - 1);
                    Value* vScale     = VIMMED1(scale);
                    texels[compIndex] = BITCAST(texels[compIndex], mSimdInt32Ty);
                    texels[compIndex] = UI_TO_FP(texels[compIndex], mSimdFP32Ty);
                    texels[compIndex] = FMUL(texels[compIndex], vScale);
                }
            }
            continue;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Loads attributes from memory using AVX2 GATHER(s)
/// @param fetchState - info about attributes to be fetched from memory
/// @param streams - value pointer to the current vertex stream
/// @param vIndices - vector value of indices to gather
/// @param pVtxOut - value pointer to output simdvertex struct
void FetchJit::JitGatherVertices(const FETCH_COMPILE_STATE& fetchState,
                                 Value*                     streams,
                                 Value*                     vIndices,
                                 Value*                     pVtxOut)
{
    uint32_t currentVertexElement = 0;
    uint32_t outputElt            = 0;
    Value*   vVertexElements[4];

    Value* startVertex   = LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_StartVertex});
    Value* startInstance = LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_StartInstance});
    Value* curInstance   = LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_CurInstance});
    Value* vBaseVertex   = VBROADCAST(LOAD(mpFetchInfo, {0, SWR_FETCH_CONTEXT_BaseVertex}));
    curInstance->setName("curInstance");

    for (uint32_t nInputElt = 0; nInputElt < fetchState.numAttribs; nInputElt += 1)
    {
        const INPUT_ELEMENT_DESC& ied = fetchState.layout[nInputElt];

        // skip element if all components are disabled
        if (ied.ComponentPacking == ComponentEnable::NONE)
        {
            continue;
        }

        const SWR_FORMAT_INFO& info = GetFormatInfo((SWR_FORMAT)ied.Format);
        SWR_ASSERT((info.bpp != 0), "Unsupported format in JitGatherVertices.");
        uint32_t bpc =
            info.bpp /
            info.numComps; ///@todo Code below assumes all components are same size. Need to fix.

        Value* stream = LOAD(streams, {ied.StreamIndex, SWR_VERTEX_BUFFER_STATE_xpData});

        Value* stride  = LOAD(streams, {ied.StreamIndex, SWR_VERTEX_BUFFER_STATE_pitch});
        Value* vStride = VBROADCAST(stride);

        // max vertex index that is fully in bounds
        Value* maxVertex = GEP(streams, {C(ied.StreamIndex), C(SWR_VERTEX_BUFFER_STATE_maxVertex)});
        maxVertex        = LOAD(maxVertex);

        Value* minVertex = NULL;
        if (fetchState.bPartialVertexBuffer)
        {
            // min vertex index for low bounds OOB checking
            minVertex = GEP(streams, {C(ied.StreamIndex), C(SWR_VERTEX_BUFFER_STATE_minVertex)});
            minVertex = LOAD(minVertex);
        }

        if (fetchState.bInstanceIDOffsetEnable)
        {
            // the InstanceID (curInstance) value is offset by StartInstanceLocation
            curInstance = ADD(curInstance, startInstance);
        }

        Value* vCurIndices;
        Value* startOffset;
        Value* vInstanceStride = VIMMED1(0);

        if (ied.InstanceEnable)
        {
            Value* stepRate = C(ied.InstanceAdvancementState);

            // prevent a div by 0 for 0 step rate
            Value* isNonZeroStep = ICMP_UGT(stepRate, C(0));
            stepRate             = SELECT(isNonZeroStep, stepRate, C(1));

            // calc the current offset into instanced data buffer
            Value* calcInstance = UDIV(curInstance, stepRate);

            // if step rate is 0, every instance gets instance 0
            calcInstance = SELECT(isNonZeroStep, calcInstance, C(0));

            vCurIndices = VBROADCAST(calcInstance);
            startOffset = startInstance;
        }
        else if (ied.InstanceStrideEnable)
        {
            // grab the instance advancement state, determines stride in bytes from one instance to
            // the next
            Value* stepRate = C(ied.InstanceAdvancementState);
            vInstanceStride = VBROADCAST(MUL(curInstance, stepRate));

            // offset indices by baseVertex
            vCurIndices = ADD(vIndices, vBaseVertex);

            startOffset = startVertex;
            SWR_ASSERT((0), "TODO: Fill out more once driver sends this down.");
        }
        else
        {
            // offset indices by baseVertex
            vCurIndices = ADD(vIndices, vBaseVertex);
            startOffset = startVertex;
        }

        // All of the OOB calculations are in vertices, not VB offsets, to prevent having to
        // do 64bit address offset calculations.

        // calculate byte offset to the start of the VB
        Value* baseOffset = MUL(Z_EXT(startOffset, mInt64Ty), Z_EXT(stride, mInt64Ty));

        // VGATHER* takes an *i8 src pointer so that's what stream is
        Value* pStreamBaseGFX = ADD(stream, baseOffset);

        // if we have a start offset, subtract from max vertex. Used for OOB check
        maxVertex     = SUB(Z_EXT(maxVertex, mInt64Ty), Z_EXT(startOffset, mInt64Ty));
        Value* maxNeg = ICMP_SLT(maxVertex, C((int64_t)0));
        // if we have a negative value, we're already OOB. clamp at 0.
        maxVertex = SELECT(maxNeg, C(0), TRUNC(maxVertex, mInt32Ty));

        if (fetchState.bPartialVertexBuffer)
        {
            // similary for min vertex
            minVertex     = SUB(Z_EXT(minVertex, mInt64Ty), Z_EXT(startOffset, mInt64Ty));
            Value* minNeg = ICMP_SLT(minVertex, C((int64_t)0));
            minVertex     = SELECT(minNeg, C(0), TRUNC(minVertex, mInt32Ty));
        }

        // Load the in bounds size of a partially valid vertex
        Value* partialInboundsSize =
            GEP(streams, {C(ied.StreamIndex), C(SWR_VERTEX_BUFFER_STATE_partialInboundsSize)});
        partialInboundsSize       = LOAD(partialInboundsSize);
        Value* vPartialVertexSize = VBROADCAST(partialInboundsSize);
        Value* vBpp               = VBROADCAST(C(info.Bpp));
        Value* vAlignmentOffsets  = VBROADCAST(C(ied.AlignedByteOffset));

        // is the element is <= the partially valid size
        Value* vElementInBoundsMask = ICMP_SLE(vBpp, SUB(vPartialVertexSize, vAlignmentOffsets));

        // override cur indices with 0 if pitch is 0
        Value* pZeroPitchMask = ICMP_EQ(vStride, VIMMED1(0));
        vCurIndices           = SELECT(pZeroPitchMask, VIMMED1(0), vCurIndices);

        // are vertices partially OOB?
        Value* vMaxVertex      = VBROADCAST(maxVertex);
        Value* vPartialOOBMask = ICMP_EQ(vCurIndices, vMaxVertex);

        // are vertices fully in bounds?
        Value* vMaxGatherMask = ICMP_ULT(vCurIndices, vMaxVertex);

        Value* vGatherMask;
        if (fetchState.bPartialVertexBuffer)
        {
            // are vertices below minVertex limit?
            Value* vMinVertex     = VBROADCAST(minVertex);
            Value* vMinGatherMask = ICMP_UGE(vCurIndices, vMinVertex);

            // only fetch lanes that pass both tests
            vGatherMask = AND(vMaxGatherMask, vMinGatherMask);
        }
        else
        {
            vGatherMask = vMaxGatherMask;
        }

        // blend in any partially OOB indices that have valid elements
        vGatherMask = SELECT(vPartialOOBMask, vElementInBoundsMask, vGatherMask);

        // calculate the actual offsets into the VB
        Value* vOffsets = MUL(vCurIndices, vStride);
        vOffsets        = ADD(vOffsets, vAlignmentOffsets);

        // if instance stride enable is:
        //  true  - add product of the instanceID and advancement state to the offset into the VB
        //  false - value of vInstanceStride has been initialized to zero
        vOffsets = ADD(vOffsets, vInstanceStride);

        // Packing and component control
        ComponentEnable        compMask = (ComponentEnable)ied.ComponentPacking;
        const ComponentControl compCtrl[4]{(ComponentControl)ied.ComponentControl0,
                                           (ComponentControl)ied.ComponentControl1,
                                           (ComponentControl)ied.ComponentControl2,
                                           (ComponentControl)ied.ComponentControl3};

        // Special gather/conversion for formats without equal component sizes
        if (IsOddFormat((SWR_FORMAT)ied.Format))
        {
            Value* pResults[4];
            CreateGatherOddFormats(
                (SWR_FORMAT)ied.Format, vGatherMask, pStreamBaseGFX, vOffsets, pResults);
            ConvertFormat((SWR_FORMAT)ied.Format, pResults);

            for (uint32_t c = 0; c < 4; c += 1)
            {
                if (isComponentEnabled(compMask, c))
                {
                    vVertexElements[currentVertexElement++] = pResults[c];
                    if (currentVertexElement > 3)
                    {
                        StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                        // reset to the next vVertexElement to output
                        currentVertexElement = 0;
                    }
                }
            }
        }
        else if (info.type[0] == SWR_TYPE_FLOAT)
        {
            ///@todo: support 64 bit vb accesses
            Value* gatherSrc = VIMMED1(0.0f);

            SWR_ASSERT(IsUniformFormat((SWR_FORMAT)ied.Format),
                       "Unsupported format for standard gather fetch.");

            // Gather components from memory to store in a simdvertex structure
            switch (bpc)
            {
            case 16:
            {
                Value* vGatherResult[2];

                // if we have at least one component out of x or y to fetch
                if (isComponentEnabled(compMask, 0) || isComponentEnabled(compMask, 1))
                {
                    vGatherResult[0] = GATHERPS(gatherSrc, pStreamBaseGFX, vOffsets, vGatherMask, 1, MEM_CLIENT::GFX_MEM_CLIENT_FETCH);
                    // e.g. result of first 8x32bit integer gather for 16bit components
                    // 256i - 0    1    2    3    4    5    6    7
                    //        xyxy xyxy xyxy xyxy xyxy xyxy xyxy xyxy
                    //
                }

                // if we have at least one component out of z or w to fetch
                if (isComponentEnabled(compMask, 2) || isComponentEnabled(compMask, 3))
                {
                    // offset base to the next components(zw) in the vertex to gather
                    pStreamBaseGFX = ADD(pStreamBaseGFX, C((int64_t)4));

                    vGatherResult[1] = GATHERPS(gatherSrc, pStreamBaseGFX, vOffsets, vGatherMask, 1, MEM_CLIENT::GFX_MEM_CLIENT_FETCH);
                    // e.g. result of second 8x32bit integer gather for 16bit components
                    // 256i - 0    1    2    3    4    5    6    7
                    //        zwzw zwzw zwzw zwzw zwzw zwzw zwzw zwzw
                    //
                }

                // if we have at least one component to shuffle into place
                if (compMask)
                {
                    Shuffle16bpcArgs args = std::forward_as_tuple(vGatherResult,
                                                                  pVtxOut,
                                                                  Instruction::CastOps::FPExt,
                                                                  CONVERT_NONE,
                                                                  currentVertexElement,
                                                                  outputElt,
                                                                  compMask,
                                                                  compCtrl,
                                                                  vVertexElements);

                    // Shuffle gathered components into place in simdvertex struct
                    mVWidth == 16 ? Shuffle16bpcGather16(args)
                                  : Shuffle16bpcGather(args); // outputs to vVertexElements ref
                }
            }
            break;
            case 32:
            {
                for (uint32_t i = 0; i < 4; i += 1)
                {
                    if (isComponentEnabled(compMask, i))
                    {
                        // if we need to gather the component
                        if (compCtrl[i] == StoreSrc)
                        {
                            // Gather a SIMD of vertices
                            // APIs allow a 4GB range for offsets
                            // However, GATHERPS uses signed 32-bit offsets, so +/- 2GB range :(
                            // Add 2GB to the base pointer and 2GB to the offsets.  This makes
                            // "negative" (large) offsets into positive offsets and small offsets
                            // into negative offsets.
                            Value* vNewOffsets = ADD(vOffsets, VIMMED1(0x80000000));
                            vVertexElements[currentVertexElement++] =
                                GATHERPS(gatherSrc,
                                         ADD(pStreamBaseGFX, C((uintptr_t)0x80000000U)),
                                         vNewOffsets,
                                         vGatherMask,
                                         1,
                                         MEM_CLIENT::GFX_MEM_CLIENT_FETCH);
                        }
                        else
                        {
                            vVertexElements[currentVertexElement++] =
                                GenerateCompCtrlVector(compCtrl[i]);
                        }

                        if (currentVertexElement > 3)
                        {
                            StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                            // reset to the next vVertexElement to output
                            currentVertexElement = 0;
                        }
                    }

                    // offset base to the next component in the vertex to gather
                    pStreamBaseGFX = ADD(pStreamBaseGFX, C((int64_t)4));
                }
            }
            break;
            case 64:
            {
                for (uint32_t i = 0; i < 4; i += 1)
                {
                    if (isComponentEnabled(compMask, i))
                    {
                        // if we need to gather the component
                        if (compCtrl[i] == StoreSrc)
                        {
                            Value* vShufLo;
                            Value* vShufHi;
                            Value* vShufAll;

                            if (mVWidth == 8)
                            {
                                vShufLo  = C({0, 1, 2, 3});
                                vShufHi  = C({4, 5, 6, 7});
                                vShufAll = C({0, 1, 2, 3, 4, 5, 6, 7});
                            }
                            else
                            {
                                SWR_ASSERT(mVWidth == 16);
                                vShufLo = C({0, 1, 2, 3, 4, 5, 6, 7});
                                vShufHi = C({8, 9, 10, 11, 12, 13, 14, 15});
                                vShufAll =
                                    C({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});
                            }

                            Value* vMaskLo = VSHUFFLE(vGatherMask, vGatherMask, vShufLo);
                            Value* vMaskHi = VSHUFFLE(vGatherMask, vGatherMask, vShufHi);

                            Value* vOffsetsLo = VSHUFFLE(vOffsets, vOffsets, vShufLo);
                            Value* vOffsetsHi = VSHUFFLE(vOffsets, vOffsets, vShufHi);

                            Value* vZeroDouble = VECTOR_SPLAT(
                                mVWidth / 2, ConstantFP::get(IRB()->getDoubleTy(), 0.0f));

                            Value* pGatherLo =
                                GATHERPD(vZeroDouble, pStreamBaseGFX, vOffsetsLo, vMaskLo);
                            Value* pGatherHi =
                                GATHERPD(vZeroDouble, pStreamBaseGFX, vOffsetsHi, vMaskHi);

                            Value* pGather = VSHUFFLE(pGatherLo, pGatherHi, vShufAll);
                            pGather        = FP_TRUNC(pGather, mSimdFP32Ty);

                            vVertexElements[currentVertexElement++] = pGather;
                        }
                        else
                        {
                            vVertexElements[currentVertexElement++] =
                                GenerateCompCtrlVector(compCtrl[i]);
                        }

                        if (currentVertexElement > 3)
                        {
                            StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                            // reset to the next vVertexElement to output
                            currentVertexElement = 0;
                        }
                    }

                    // offset base to the next component  in the vertex to gather
                    pStreamBaseGFX = ADD(pStreamBaseGFX, C((int64_t)8));
                }
            }
            break;
            default:
                SWR_INVALID("Tried to fetch invalid FP format");
                break;
            }
        }
        else
        {
            Instruction::CastOps extendCastType = Instruction::CastOps::CastOpsEnd;
            ConversionType       conversionType = CONVERT_NONE;

            SWR_ASSERT(IsUniformFormat((SWR_FORMAT)ied.Format),
                       "Unsupported format for standard gather fetch.");

            switch (info.type[0])
            {
            case SWR_TYPE_UNORM:
                conversionType = CONVERT_NORMALIZED;
            case SWR_TYPE_UINT:
                extendCastType = Instruction::CastOps::ZExt;
                break;
            case SWR_TYPE_SNORM:
                conversionType = CONVERT_NORMALIZED;
            case SWR_TYPE_SINT:
                extendCastType = Instruction::CastOps::SExt;
                break;
            case SWR_TYPE_USCALED:
                conversionType = CONVERT_USCALED;
                extendCastType = Instruction::CastOps::UIToFP;
                break;
            case SWR_TYPE_SSCALED:
                conversionType = CONVERT_SSCALED;
                extendCastType = Instruction::CastOps::SIToFP;
                break;
            case SWR_TYPE_SFIXED:
                conversionType = CONVERT_SFIXED;
                extendCastType = Instruction::CastOps::SExt;
                break;
            default:
                break;
            }

            // value substituted when component of gather is masked
            Value* gatherSrc = VIMMED1(0);

            // Gather components from memory to store in a simdvertex structure
            switch (bpc)
            {
            case 8:
            {
                // if we have at least one component to fetch
                if (compMask)
                {
                    Value* vGatherResult = GATHERDD(gatherSrc,
                                                    pStreamBaseGFX,
                                                    vOffsets,
                                                    vGatherMask,
                                                    1,
                                                    MEM_CLIENT::GFX_MEM_CLIENT_FETCH);
                    // e.g. result of an 8x32bit integer gather for 8bit components
                    // 256i - 0    1    2    3    4    5    6    7
                    //        xyzw xyzw xyzw xyzw xyzw xyzw xyzw xyzw

                    Shuffle8bpcArgs args = std::forward_as_tuple(vGatherResult,
                                                                 pVtxOut,
                                                                 extendCastType,
                                                                 conversionType,
                                                                 currentVertexElement,
                                                                 outputElt,
                                                                 compMask,
                                                                 compCtrl,
                                                                 vVertexElements,
                                                                 info.swizzle);

                    // Shuffle gathered components into place in simdvertex struct
                    mVWidth == 16 ? Shuffle8bpcGatherd16(args)
                                  : Shuffle8bpcGatherd(args); // outputs to vVertexElements ref
                }
            }
            break;
            case 16:
            {
                Value* vGatherResult[2];

                // if we have at least one component out of x or y to fetch
                if (isComponentEnabled(compMask, 0) || isComponentEnabled(compMask, 1))
                {
                    vGatherResult[0] = GATHERDD(gatherSrc,
                                                pStreamBaseGFX,
                                                vOffsets,
                                                vGatherMask,
                                                1,
                                                MEM_CLIENT::GFX_MEM_CLIENT_FETCH);
                    // e.g. result of first 8x32bit integer gather for 16bit components
                    // 256i - 0    1    2    3    4    5    6    7
                    //        xyxy xyxy xyxy xyxy xyxy xyxy xyxy xyxy
                    //
                }

                // if we have at least one component out of z or w to fetch
                if (isComponentEnabled(compMask, 2) || isComponentEnabled(compMask, 3))
                {
                    // offset base to the next components(zw) in the vertex to gather
                    pStreamBaseGFX = ADD(pStreamBaseGFX, C((int64_t)4));

                    vGatherResult[1] = GATHERDD(gatherSrc,
                                                pStreamBaseGFX,
                                                vOffsets,
                                                vGatherMask,
                                                1,
                                                MEM_CLIENT::GFX_MEM_CLIENT_FETCH);
                    // e.g. result of second 8x32bit integer gather for 16bit components
                    // 256i - 0    1    2    3    4    5    6    7
                    //        zwzw zwzw zwzw zwzw zwzw zwzw zwzw zwzw
                    //
                }

                // if we have at least one component to shuffle into place
                if (compMask)
                {
                    Shuffle16bpcArgs args = std::forward_as_tuple(vGatherResult,
                                                                  pVtxOut,
                                                                  extendCastType,
                                                                  conversionType,
                                                                  currentVertexElement,
                                                                  outputElt,
                                                                  compMask,
                                                                  compCtrl,
                                                                  vVertexElements);

                    // Shuffle gathered components into place in simdvertex struct
                    mVWidth == 16 ? Shuffle16bpcGather16(args)
                                  : Shuffle16bpcGather(args); // outputs to vVertexElements ref
                }
            }
            break;
            case 32:
            {
                // Gathered components into place in simdvertex struct
                for (uint32_t i = 0; i < 4; i++)
                {
                    if (isComponentEnabled(compMask, i))
                    {
                        // if we need to gather the component
                        if (compCtrl[i] == StoreSrc)
                        {
                            Value* pGather = GATHERDD(gatherSrc,
                                                      pStreamBaseGFX,
                                                      vOffsets,
                                                      vGatherMask,
                                                      1,
                                                      MEM_CLIENT::GFX_MEM_CLIENT_FETCH);

                            if (conversionType == CONVERT_USCALED)
                            {
                                pGather = UI_TO_FP(pGather, mSimdFP32Ty);
                            }
                            else if (conversionType == CONVERT_SSCALED)
                            {
                                pGather = SI_TO_FP(pGather, mSimdFP32Ty);
                            }
                            else if (conversionType == CONVERT_SFIXED)
                            {
                                pGather = FMUL(SI_TO_FP(pGather, mSimdFP32Ty),
                                               VBROADCAST(C(1 / 65536.0f)));
                            }

                            vVertexElements[currentVertexElement++] = pGather;

                            // e.g. result of a single 8x32bit integer gather for 32bit components
                            // 256i - 0    1    2    3    4    5    6    7
                            //        xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx
                        }
                        else
                        {
                            vVertexElements[currentVertexElement++] =
                                GenerateCompCtrlVector(compCtrl[i]);
                        }

                        if (currentVertexElement > 3)
                        {
                            StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);

                            // reset to the next vVertexElement to output
                            currentVertexElement = 0;
                        }
                    }

                    // offset base to the next component  in the vertex to gather
                    pStreamBaseGFX = ADD(pStreamBaseGFX, C((int64_t)4));
                }
            }
            break;
            }
        }
    }

    // if we have a partially filled vVertexElement struct, output it
    if (currentVertexElement > 0)
    {
        StoreVertexElements(pVtxOut, outputElt++, currentVertexElement, vVertexElements);
    }
}


typedef void* (*PFN_TRANSLATEGFXADDRESS_FUNC)(void* pdc, gfxptr_t va, bool* out_pbNullTileAccessed, void* pWorkerData);

template <typename T>
void GetSimdValidIndicesGfx(gfxptr_t                     indices,
                            gfxptr_t                     lastIndex,
                            uint32_t                     vWidth,
                            PFN_TRANSLATEGFXADDRESS_FUNC pfnTranslate,
                            void*                        pdc,
                            uint32_t*                    outIndices,
                            void*                        pWorkerData)
{
    SWR_ASSERT(outIndices != nullptr);

    gfxptr_t indexPtr = indices;
    for (int64_t lane = 0; lane < vWidth; lane++)
    {
        uint32_t index = 0;

        if (indexPtr < lastIndex)
        {
            // translate indexPtr and load from it
            T* addr = (T*)pfnTranslate(pdc, indexPtr, nullptr, pWorkerData);
            SWR_ASSERT(addr != nullptr);
            index = *addr;
        }

        // index to 32 bits and insert into the correct simd lane
        outIndices[lane] = index;

        indexPtr += sizeof(T);
    }
}

void GetSimdValid8bitIndicesGfx(gfxptr_t                     indices,
                                gfxptr_t                     lastIndex,
                                uint32_t                     vWidth,
                                PFN_TRANSLATEGFXADDRESS_FUNC pfnTranslate,
                                void*                        pdc,
                                uint32_t*                    outIndices,
                                void*                        pWorkerData)
{
    GetSimdValidIndicesGfx<uint8_t>(indices, lastIndex, vWidth, pfnTranslate, pdc, outIndices, pWorkerData);
}

void GetSimdValid16bitIndicesGfx(gfxptr_t                     indices,
                                 gfxptr_t                     lastIndex,
                                 uint32_t                     vWidth,
                                 PFN_TRANSLATEGFXADDRESS_FUNC pfnTranslate,
                                 void*                        pdc,
                                 uint32_t*                    outIndices,
                                 void*                        pWorkerData)
{
    GetSimdValidIndicesGfx<uint16_t>(indices, lastIndex, vWidth, pfnTranslate, pdc, outIndices, pWorkerData);
}


template <typename T>
Value* FetchJit::GetSimdValidIndicesHelper(Value* pIndices, Value* pLastIndex)
{
    SWR_ASSERT(pIndices->getType() == mInt64Ty && pLastIndex->getType() == mInt64Ty,
               "Function expects gfxptr_t for both input parameters.");

    Type* Ty = nullptr;

    static_assert(sizeof(T) == sizeof(uint16_t) || sizeof(T) == sizeof(uint8_t),
                  "Unsupported type for use with GetSimdValidIndicesHelper<T>");
    constexpr bool bSize = (sizeof(T) == sizeof(uint16_t));
    if (bSize)
    {
        Ty = mInt16PtrTy;
    }
    else if (sizeof(T) == sizeof(uint8_t))
    {
        Ty = mInt8PtrTy;
    }
    else
    {
        SWR_ASSERT(false, "This should never happen as per static_assert above.");
    }

    Value* vIndices = VUNDEF_I();

    {
        // store 0 index on stack to be used to conditionally load from if index address is OOB
        Value* pZeroIndex = ALLOCA(Ty->getPointerElementType());
        STORE(C((T)0), pZeroIndex);

        // Load a SIMD of index pointers
        for (int64_t lane = 0; lane < mVWidth; lane++)
        {
            // Calculate the address of the requested index
            Value* pIndex = GEP(pIndices, C(lane), Ty);

            pLastIndex = INT_TO_PTR(pLastIndex, Ty);

            // check if the address is less than the max index,
            Value* mask = ICMP_ULT(pIndex, pLastIndex);

            // if valid, load the index. if not, load 0 from the stack
            Value* pValid = SELECT(mask, pIndex, pZeroIndex);
            Value* index  = LOAD(pValid, "valid index", Ty, MEM_CLIENT::GFX_MEM_CLIENT_FETCH);

            // zero extended index to 32 bits and insert into the correct simd lane
            index    = Z_EXT(index, mInt32Ty);
            vIndices = VINSERT(vIndices, index, lane);
        }
    }

    return vIndices;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Loads a simd of valid indices. OOB indices are set to 0
/// *Note* have to do 8bit index checking in scalar until we have AVX-512
/// support
/// @param pIndices - pointer to 8 bit indices
/// @param pLastIndex - pointer to last valid index
Value* FetchJit::GetSimdValid8bitIndices(Value* pIndices, Value* pLastIndex)
{
    return GetSimdValidIndicesHelper<uint8_t>(pIndices, pLastIndex);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Loads a simd of valid indices. OOB indices are set to 0
/// *Note* have to do 16bit index checking in scalar until we have AVX-512
/// support
/// @param pIndices - pointer to 16 bit indices
/// @param pLastIndex - pointer to last valid index
Value* FetchJit::GetSimdValid16bitIndices(Value* pIndices, Value* pLastIndex)
{
    return GetSimdValidIndicesHelper<uint16_t>(pIndices, pLastIndex);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Loads a simd of valid indices. OOB indices are set to 0
/// @param pIndices - pointer to 32 bit indices
/// @param pLastIndex - pointer to last valid index
Value* FetchJit::GetSimdValid32bitIndices(Value* pIndices, Value* pLastIndex)
{
    DataLayout dL(JM()->mpCurrentModule);
    Value*     iLastIndex = pLastIndex;
    Value*     iIndices   = pIndices;

    // get the number of indices left in the buffer (endPtr - curPtr) / sizeof(index)
    Value* numIndicesLeft = SUB(iLastIndex, iIndices);
    numIndicesLeft        = TRUNC(numIndicesLeft, mInt32Ty);
    numIndicesLeft        = SDIV(numIndicesLeft, C(4));

    // create a vector of index counts from the base index ptr passed into the fetch
    Constant* vIndexOffsets;
    if (mVWidth == 8)
    {
        vIndexOffsets = C({0, 1, 2, 3, 4, 5, 6, 7});
    }
    else
    {
        vIndexOffsets = C({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});
    }

    // compare index count to the max valid index
    // e.g vMaxIndex      4 4 4 4 4 4 4 4 : 4 indices left to load
    //     vIndexOffsets  0 1 2 3 4 5 6 7
    //     ------------------------------
    //     vIndexMask    -1-1-1-1 0 0 0 0 : offsets < max pass
    //     vLoadedIndices 0 1 2 3 0 0 0 0 : offsets >= max masked to 0
    Value* vMaxIndex  = VBROADCAST(numIndicesLeft);
    Value* vIndexMask = ICMP_SGT(vMaxIndex, vIndexOffsets);

    // Load the indices; OOB loads 0
    return MASKED_LOAD(pIndices,
                       4,
                       vIndexMask,
                       VIMMED1(0),
                       "vIndices",
                       PointerType::get(mSimdInt32Ty, 0),
                       MEM_CLIENT::GFX_MEM_CLIENT_FETCH);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Takes a SIMD of gathered 8bpc verts, zero or sign extends,
/// denormalizes if needed, converts to F32 if needed, and positions in
//  the proper SIMD rows to be output to the simdvertex structure
/// @param args: (tuple of args, listed below)
///   @param vGatherResult - 8 gathered 8bpc vertices
///   @param pVtxOut - base pointer to output simdvertex struct
///   @param extendType - sign extend or zero extend
///   @param bNormalized - do we need to denormalize?
///   @param currentVertexElement - reference to the current vVertexElement
///   @param outputElt - reference to the current offset from simdvertex we're o
///   @param compMask - component packing mask
///   @param compCtrl - component control val
///   @param vVertexElements[4] - vertex components to output
///   @param swizzle[4] - component swizzle location
void FetchJit::Shuffle8bpcGatherd16(Shuffle8bpcArgs& args)
{
    // Unpack tuple args
    Value*&                    vGatherResult        = std::get<0>(args);
    Value*                     pVtxOut              = std::get<1>(args);
    const Instruction::CastOps extendType           = std::get<2>(args);
    const ConversionType       conversionType       = std::get<3>(args);
    uint32_t&                  currentVertexElement = std::get<4>(args);
    uint32_t&                  outputElt            = std::get<5>(args);
    const ComponentEnable      compMask             = std::get<6>(args);
    const ComponentControl(&compCtrl)[4]            = std::get<7>(args);
    Value*(&vVertexElements)[4]                     = std::get<8>(args);
    const uint32_t(&swizzle)[4]                     = std::get<9>(args);

    // cast types
    Type* vGatherTy = getVectorType(mInt32Ty, 8);
    Type* v32x8Ty   = getVectorType(mInt8Ty, 32);

    // have to do extra work for sign extending
    if ((extendType == Instruction::CastOps::SExt) || (extendType == Instruction::CastOps::SIToFP))
    {
        Type* v16x8Ty = getVectorType(mInt8Ty, 16); // 8x16bit ints in a 128bit lane
        Type* v128Ty  = getVectorType(IntegerType::getIntNTy(JM()->mContext, 128), 2);

        // shuffle mask, including any swizzling
        const char x          = (char)swizzle[0];
        const char y          = (char)swizzle[1];
        const char z          = (char)swizzle[2];
        const char w          = (char)swizzle[3];
        Value*     vConstMask = C<char>(
            {char(x),     char(x + 4),  char(x + 8), char(x + 12), char(y),     char(y + 4),
             char(y + 8), char(y + 12), char(z),     char(z + 4),  char(z + 8), char(z + 12),
             char(w),     char(w + 4),  char(w + 8), char(w + 12), char(x),     char(x + 4),
             char(x + 8), char(x + 12), char(y),     char(y + 4),  char(y + 8), char(y + 12),
             char(z),     char(z + 4),  char(z + 8), char(z + 12), char(w),     char(w + 4),
             char(w + 8), char(w + 12)});

        // SIMD16 PSHUFB isn't part of AVX-512F, so split into SIMD8 for the sake of KNL, for now..

        Value* vGatherResult_lo = EXTRACT_16(vGatherResult, 0);
        Value* vGatherResult_hi = EXTRACT_16(vGatherResult, 1);

        Value* vShufResult_lo =
            BITCAST(PSHUFB(BITCAST(vGatherResult_lo, v32x8Ty), vConstMask), vGatherTy);
        Value* vShufResult_hi =
            BITCAST(PSHUFB(BITCAST(vGatherResult_hi, v32x8Ty), vConstMask), vGatherTy);

        // after pshufb: group components together in each 128bit lane
        // 256i - 0    1    2    3    4    5    6    7
        //        xxxx yyyy zzzz wwww xxxx yyyy zzzz wwww

        Value* vi128XY_lo = nullptr;
        Value* vi128XY_hi = nullptr;
        if (isComponentEnabled(compMask, 0) || isComponentEnabled(compMask, 1))
        {
            vi128XY_lo = BITCAST(
                VSHUFFLE(vShufResult_lo, vShufResult_lo, C<int32_t>({0, 4, 0, 0, 1, 5, 0, 0})),
                v128Ty);
            vi128XY_hi = BITCAST(
                VSHUFFLE(vShufResult_hi, vShufResult_hi, C<int32_t>({0, 4, 0, 0, 1, 5, 0, 0})),
                v128Ty);

            // after PERMD: move and pack xy and zw components in low 64 bits of each 128bit lane
            // 256i - 0    1    2    3    4    5    6    7
            //        xxxx xxxx dcdc dcdc yyyy yyyy dcdc dcdc (dc - don't care)
        }

        // do the same for zw components
        Value* vi128ZW_lo = nullptr;
        Value* vi128ZW_hi = nullptr;
        if (isComponentEnabled(compMask, 2) || isComponentEnabled(compMask, 3))
        {
            vi128ZW_lo = BITCAST(
                VSHUFFLE(vShufResult_lo, vShufResult_lo, C<int32_t>({2, 6, 0, 0, 3, 7, 0, 0})),
                v128Ty);
            vi128ZW_hi = BITCAST(
                VSHUFFLE(vShufResult_hi, vShufResult_hi, C<int32_t>({2, 6, 0, 0, 3, 7, 0, 0})),
                v128Ty);
        }

        // init denormalize variables if needed
        Instruction::CastOps fpCast;
        Value*               conversionFactor;

        switch (conversionType)
        {
        case CONVERT_NORMALIZED:
            fpCast           = Instruction::CastOps::SIToFP;
            conversionFactor = VIMMED1((float)(1.0 / 127.0));
            break;
        case CONVERT_SSCALED:
            fpCast           = Instruction::CastOps::SIToFP;
            conversionFactor = VIMMED1((float)(1.0));
            break;
        case CONVERT_USCALED:
            assert(false && "Type should not be sign extended!");
            conversionFactor = nullptr;
            break;
        default:
            assert(conversionType == CONVERT_NONE);
            conversionFactor = nullptr;
            break;
        }

        // sign extend all enabled components. If we have a fill vVertexElements, output to current
        // simdvertex
        for (uint32_t i = 0; i < 4; i++)
        {
            if (isComponentEnabled(compMask, i))
            {
                if (compCtrl[i] == ComponentControl::StoreSrc)
                {
                    // if x or z, extract 128bits from lane 0, else for y or w, extract from lane 1
                    uint32_t lane = ((i == 0) || (i == 2)) ? 0 : 1;
                    // if x or y, use vi128XY permute result, else use vi128ZW
                    Value* selectedPermute_lo = (i < 2) ? vi128XY_lo : vi128ZW_lo;
                    Value* selectedPermute_hi = (i < 2) ? vi128XY_hi : vi128ZW_hi;

                    // sign extend
                    Value* temp_lo =
                        PMOVSXBD(BITCAST(VEXTRACT(selectedPermute_lo, C(lane)), v16x8Ty));
                    Value* temp_hi =
                        PMOVSXBD(BITCAST(VEXTRACT(selectedPermute_hi, C(lane)), v16x8Ty));

                    Value* temp = JOIN_16(temp_lo, temp_hi);

                    // denormalize if needed
                    if (conversionType != CONVERT_NONE)
                    {
                        temp = FMUL(CAST(fpCast, temp, mSimdFP32Ty), conversionFactor);
                    }

                    vVertexElements[currentVertexElement] = temp;

                    currentVertexElement += 1;
                }
                else
                {
                    vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
                }

                if (currentVertexElement > 3)
                {
                    StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                    // reset to the next vVertexElement to output
                    currentVertexElement = 0;
                }
            }
        }
    }
    // else zero extend
    else if ((extendType == Instruction::CastOps::ZExt) ||
             (extendType == Instruction::CastOps::UIToFP))
    {
        // init denormalize variables if needed
        Instruction::CastOps fpCast;
        Value*               conversionFactor;

        switch (conversionType)
        {
        case CONVERT_NORMALIZED:
            fpCast           = Instruction::CastOps::UIToFP;
            conversionFactor = VIMMED1((float)(1.0 / 255.0));
            break;
        case CONVERT_USCALED:
            fpCast           = Instruction::CastOps::UIToFP;
            conversionFactor = VIMMED1((float)(1.0));
            break;
        case CONVERT_SSCALED:
            assert(false && "Type should not be zero extended!");
            conversionFactor = nullptr;
            break;
        default:
            assert(conversionType == CONVERT_NONE);
            conversionFactor = nullptr;
            break;
        }

        // shuffle enabled components into lower byte of each 32bit lane, 0 extending to 32 bits
        for (uint32_t i = 0; i < 4; i++)
        {
            if (isComponentEnabled(compMask, i))
            {
                if (compCtrl[i] == ComponentControl::StoreSrc)
                {
                    // pshufb masks for each component
                    Value* vConstMask;
                    switch (swizzle[i])
                    {
                    case 0:
                        // x shuffle mask
                        vConstMask =
                            C<char>({0, -1, -1, -1, 4, -1, -1, -1, 8, -1, -1, -1, 12, -1, -1, -1,
                                     0, -1, -1, -1, 4, -1, -1, -1, 8, -1, -1, -1, 12, -1, -1, -1});
                        break;
                    case 1:
                        // y shuffle mask
                        vConstMask =
                            C<char>({1, -1, -1, -1, 5, -1, -1, -1, 9, -1, -1, -1, 13, -1, -1, -1,
                                     1, -1, -1, -1, 5, -1, -1, -1, 9, -1, -1, -1, 13, -1, -1, -1});
                        break;
                    case 2:
                        // z shuffle mask
                        vConstMask =
                            C<char>({2, -1, -1, -1, 6, -1, -1, -1, 10, -1, -1, -1, 14, -1, -1, -1,
                                     2, -1, -1, -1, 6, -1, -1, -1, 10, -1, -1, -1, 14, -1, -1, -1});
                        break;
                    case 3:
                        // w shuffle mask
                        vConstMask =
                            C<char>({3, -1, -1, -1, 7, -1, -1, -1, 11, -1, -1, -1, 15, -1, -1, -1,
                                     3, -1, -1, -1, 7, -1, -1, -1, 11, -1, -1, -1, 15, -1, -1, -1});
                        break;
                    default:
                        assert(false && "Invalid component");
                        vConstMask = nullptr;
                        break;
                    }

                    Value* vGatherResult_lo = EXTRACT_16(vGatherResult, 0);
                    Value* vGatherResult_hi = EXTRACT_16(vGatherResult, 1);

                    Value* temp_lo =
                        BITCAST(PSHUFB(BITCAST(vGatherResult_lo, v32x8Ty), vConstMask), vGatherTy);
                    Value* temp_hi =
                        BITCAST(PSHUFB(BITCAST(vGatherResult_hi, v32x8Ty), vConstMask), vGatherTy);

                    // after pshufb for x channel
                    // 256i - 0    1    2    3    4    5    6    7
                    //        x000 x000 x000 x000 x000 x000 x000 x000

                    Value* temp = JOIN_16(temp_lo, temp_hi);

                    // denormalize if needed
                    if (conversionType != CONVERT_NONE)
                    {
                        temp = FMUL(CAST(fpCast, temp, mSimdFP32Ty), conversionFactor);
                    }

                    vVertexElements[currentVertexElement] = temp;

                    currentVertexElement += 1;
                }
                else
                {
                    vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
                }

                if (currentVertexElement > 3)
                {
                    StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                    // reset to the next vVertexElement to output
                    currentVertexElement = 0;
                }
            }
        }
    }
    else
    {
        SWR_INVALID("Unsupported conversion type");
    }
}

void FetchJit::Shuffle8bpcGatherd(Shuffle8bpcArgs& args)
{
    // Unpack tuple args
    Value*&                    vGatherResult        = std::get<0>(args);
    Value*                     pVtxOut              = std::get<1>(args);
    const Instruction::CastOps extendType           = std::get<2>(args);
    const ConversionType       conversionType       = std::get<3>(args);
    uint32_t&                  currentVertexElement = std::get<4>(args);
    uint32_t&                  outputElt            = std::get<5>(args);
    const ComponentEnable      compMask             = std::get<6>(args);
    const ComponentControl(&compCtrl)[4]            = std::get<7>(args);
    Value*(&vVertexElements)[4]                     = std::get<8>(args);
    const uint32_t(&swizzle)[4]                     = std::get<9>(args);

    // cast types
    Type* v32x8Ty = getVectorType(mInt8Ty, mVWidth * 4); // vwidth is units of 32 bits

    for (uint32_t i = 0; i < 4; i++)
    {
        if (!isComponentEnabled(compMask, i))
            continue;

        if (compCtrl[i] == ComponentControl::StoreSrc)
        {
#if LLVM_VERSION_MAJOR >= 11
            using MaskType = int32_t;
#else
            using MaskType = uint32_t;
#endif
            std::vector<MaskType> vShuffleMasks[4] = {
                {0, 4, 8, 12, 16, 20, 24, 28},  // x
                {1, 5, 9, 13, 17, 21, 25, 29},  // y
                {2, 6, 10, 14, 18, 22, 26, 30}, // z
                {3, 7, 11, 15, 19, 23, 27, 31}, // w
            };

            Value* val = VSHUFFLE(BITCAST(vGatherResult, v32x8Ty),
                                  UndefValue::get(v32x8Ty),
                                  vShuffleMasks[swizzle[i]]);

            if ((extendType == Instruction::CastOps::SExt) ||
                (extendType == Instruction::CastOps::SIToFP))
            {
                switch (conversionType)
                {
                case CONVERT_NORMALIZED:
                    val = FMUL(SI_TO_FP(val, mSimdFP32Ty), VIMMED1((float)(1.0 / 127.0)));
                    break;
                case CONVERT_SSCALED:
                    val = SI_TO_FP(val, mSimdFP32Ty);
                    break;
                case CONVERT_USCALED:
                    SWR_INVALID("Type should not be sign extended!");
                    break;
                default:
                    SWR_ASSERT(conversionType == CONVERT_NONE);
                    val = S_EXT(val, mSimdInt32Ty);
                    break;
                }
            }
            else if ((extendType == Instruction::CastOps::ZExt) ||
                     (extendType == Instruction::CastOps::UIToFP))
            {
                switch (conversionType)
                {
                case CONVERT_NORMALIZED:
                    val = FMUL(UI_TO_FP(val, mSimdFP32Ty), VIMMED1((float)(1.0 / 255.0)));
                    break;
                case CONVERT_SSCALED:
                    SWR_INVALID("Type should not be zero extended!");
                    break;
                case CONVERT_USCALED:
                    val = UI_TO_FP(val, mSimdFP32Ty);
                    break;
                default:
                    SWR_ASSERT(conversionType == CONVERT_NONE);
                    val = Z_EXT(val, mSimdInt32Ty);
                    break;
                }
            }
            else
            {
                SWR_INVALID("Unsupported conversion type");
            }

            vVertexElements[currentVertexElement++] = val;
        }
        else
        {
            vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
        }

        if (currentVertexElement > 3)
        {
            StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
            // reset to the next vVertexElement to output
            currentVertexElement = 0;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Takes a SIMD of gathered 16bpc verts, zero or sign extends,
/// denormalizes if needed, converts to F32 if needed, and positions in
//  the proper SIMD rows to be output to the simdvertex structure
/// @param args: (tuple of args, listed below)
///   @param vGatherResult[2] - array of gathered 16bpc vertices, 4 per index
///   @param pVtxOut - base pointer to output simdvertex struct
///   @param extendType - sign extend or zero extend
///   @param bNormalized - do we need to denormalize?
///   @param currentVertexElement - reference to the current vVertexElement
///   @param outputElt - reference to the current offset from simdvertex we're o
///   @param compMask - component packing mask
///   @param compCtrl - component control val
///   @param vVertexElements[4] - vertex components to output
void FetchJit::Shuffle16bpcGather16(Shuffle16bpcArgs& args)
{
    // Unpack tuple args
    Value*(&vGatherResult)[2]                       = std::get<0>(args);
    Value*                     pVtxOut              = std::get<1>(args);
    const Instruction::CastOps extendType           = std::get<2>(args);
    const ConversionType       conversionType       = std::get<3>(args);
    uint32_t&                  currentVertexElement = std::get<4>(args);
    uint32_t&                  outputElt            = std::get<5>(args);
    const ComponentEnable      compMask             = std::get<6>(args);
    const ComponentControl(&compCtrl)[4]            = std::get<7>(args);
    Value*(&vVertexElements)[4]                     = std::get<8>(args);

    // cast types
    Type* vGatherTy = getVectorType(mInt32Ty, 8);
    Type* v32x8Ty   = getVectorType(mInt8Ty, 32);

    // have to do extra work for sign extending
    if ((extendType == Instruction::CastOps::SExt) ||
        (extendType == Instruction::CastOps::SIToFP) || (extendType == Instruction::CastOps::FPExt))
    {
        // is this PP float?
        bool bFP = (extendType == Instruction::CastOps::FPExt) ? true : false;

        Type* v8x16Ty   = getVectorType(mInt16Ty, 8); // 8x16bit in a 128bit lane
        Type* v128bitTy = getVectorType(IntegerType::getIntNTy(JM()->mContext, 128), 2);

        // shuffle mask
        Value* vConstMask = C<uint8_t>({0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15,
                                        0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15});
        Value* vi128XY_lo = nullptr;
        Value* vi128XY_hi = nullptr;
        if (isComponentEnabled(compMask, 0) || isComponentEnabled(compMask, 1))
        {
            // SIMD16 PSHUFB isn't part of AVX-512F, so split into SIMD8 for the sake of KNL, for
            // now..

            Value* vGatherResult_lo = BITCAST(EXTRACT_16(vGatherResult[0], 0), v32x8Ty);
            Value* vGatherResult_hi = BITCAST(EXTRACT_16(vGatherResult[0], 1), v32x8Ty);

            Value* vShufResult_lo = BITCAST(PSHUFB(vGatherResult_lo, vConstMask), vGatherTy);
            Value* vShufResult_hi = BITCAST(PSHUFB(vGatherResult_hi, vConstMask), vGatherTy);

            // after pshufb: group components together in each 128bit lane
            // 256i - 0    1    2    3    4    5    6    7
            //        xxxx xxxx yyyy yyyy xxxx xxxx yyyy yyyy

            vi128XY_lo = BITCAST(
                VSHUFFLE(vShufResult_lo, vShufResult_lo, C<int32_t>({0, 1, 4, 5, 2, 3, 6, 7})),
                v128bitTy);
            vi128XY_hi = BITCAST(
                VSHUFFLE(vShufResult_hi, vShufResult_hi, C<int32_t>({0, 1, 4, 5, 2, 3, 6, 7})),
                v128bitTy);

            // after PERMD: move and pack xy components into each 128bit lane
            // 256i - 0    1    2    3    4    5    6    7
            //        xxxx xxxx xxxx xxxx yyyy yyyy yyyy yyyy
        }

        // do the same for zw components
        Value* vi128ZW_lo = nullptr;
        Value* vi128ZW_hi = nullptr;
        if (isComponentEnabled(compMask, 2) || isComponentEnabled(compMask, 3))
        {
            Value* vGatherResult_lo = BITCAST(EXTRACT_16(vGatherResult[1], 0), v32x8Ty);
            Value* vGatherResult_hi = BITCAST(EXTRACT_16(vGatherResult[1], 1), v32x8Ty);

            Value* vShufResult_lo = BITCAST(PSHUFB(vGatherResult_lo, vConstMask), vGatherTy);
            Value* vShufResult_hi = BITCAST(PSHUFB(vGatherResult_hi, vConstMask), vGatherTy);

            vi128ZW_lo = BITCAST(
                VSHUFFLE(vShufResult_lo, vShufResult_lo, C<int32_t>({0, 1, 4, 5, 2, 3, 6, 7})),
                v128bitTy);
            vi128ZW_hi = BITCAST(
                VSHUFFLE(vShufResult_hi, vShufResult_hi, C<int32_t>({0, 1, 4, 5, 2, 3, 6, 7})),
                v128bitTy);
        }

        // init denormalize variables if needed
        Instruction::CastOps IntToFpCast;
        Value*               conversionFactor;

        switch (conversionType)
        {
        case CONVERT_NORMALIZED:
            IntToFpCast      = Instruction::CastOps::SIToFP;
            conversionFactor = VIMMED1((float)(1.0 / 32767.0));
            break;
        case CONVERT_SSCALED:
            IntToFpCast      = Instruction::CastOps::SIToFP;
            conversionFactor = VIMMED1((float)(1.0));
            break;
        case CONVERT_USCALED:
            assert(false && "Type should not be sign extended!");
            conversionFactor = nullptr;
            break;
        default:
            assert(conversionType == CONVERT_NONE);
            conversionFactor = nullptr;
            break;
        }

        // sign extend all enabled components. If we have a fill vVertexElements, output to current
        // simdvertex
        for (uint32_t i = 0; i < 4; i++)
        {
            if (isComponentEnabled(compMask, i))
            {
                if (compCtrl[i] == ComponentControl::StoreSrc)
                {
                    // if x or z, extract 128bits from lane 0, else for y or w, extract from lane 1
                    uint32_t lane = ((i == 0) || (i == 2)) ? 0 : 1;
                    // if x or y, use vi128XY permute result, else use vi128ZW
                    Value* selectedPermute_lo = (i < 2) ? vi128XY_lo : vi128ZW_lo;
                    Value* selectedPermute_hi = (i < 2) ? vi128XY_hi : vi128ZW_hi;

                    if (bFP)
                    {
                        // extract 128 bit lanes to sign extend each component
                        Value* temp_lo =
                            CVTPH2PS(BITCAST(VEXTRACT(selectedPermute_lo, C(lane)), v8x16Ty));
                        Value* temp_hi =
                            CVTPH2PS(BITCAST(VEXTRACT(selectedPermute_hi, C(lane)), v8x16Ty));

                        vVertexElements[currentVertexElement] = JOIN_16(temp_lo, temp_hi);
                    }
                    else
                    {
                        // extract 128 bit lanes to sign extend each component
                        Value* temp_lo =
                            PMOVSXWD(BITCAST(VEXTRACT(selectedPermute_lo, C(lane)), v8x16Ty));
                        Value* temp_hi =
                            PMOVSXWD(BITCAST(VEXTRACT(selectedPermute_hi, C(lane)), v8x16Ty));

                        Value* temp = JOIN_16(temp_lo, temp_hi);

                        // denormalize if needed
                        if (conversionType != CONVERT_NONE)
                        {
                            temp = FMUL(CAST(IntToFpCast, temp, mSimdFP32Ty), conversionFactor);
                        }

                        vVertexElements[currentVertexElement] = temp;
                    }

                    currentVertexElement += 1;
                }
                else
                {
                    vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
                }

                if (currentVertexElement > 3)
                {
                    StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                    // reset to the next vVertexElement to output
                    currentVertexElement = 0;
                }
            }
        }
    }
    // else zero extend
    else if ((extendType == Instruction::CastOps::ZExt) ||
             (extendType == Instruction::CastOps::UIToFP))
    {
        // pshufb masks for each component
        Value* vConstMask[2];

        if (isComponentEnabled(compMask, 0) || isComponentEnabled(compMask, 2))
        {
            // x/z shuffle mask
            vConstMask[0] = C<char>({
                0, 1, -1, -1, 4, 5, -1, -1, 8, 9, -1, -1, 12, 13, -1, -1,
                0, 1, -1, -1, 4, 5, -1, -1, 8, 9, -1, -1, 12, 13, -1, -1,
            });
        }

        if (isComponentEnabled(compMask, 1) || isComponentEnabled(compMask, 3))
        {
            // y/w shuffle mask
            vConstMask[1] = C<char>({2, 3, -1, -1, 6, 7, -1, -1, 10, 11, -1, -1, 14, 15, -1, -1,
                                     2, 3, -1, -1, 6, 7, -1, -1, 10, 11, -1, -1, 14, 15, -1, -1});
        }

        // init denormalize variables if needed
        Instruction::CastOps fpCast;
        Value*               conversionFactor;

        switch (conversionType)
        {
        case CONVERT_NORMALIZED:
            fpCast           = Instruction::CastOps::UIToFP;
            conversionFactor = VIMMED1((float)(1.0 / 65535.0));
            break;
        case CONVERT_USCALED:
            fpCast           = Instruction::CastOps::UIToFP;
            conversionFactor = VIMMED1((float)(1.0f));
            break;
        case CONVERT_SSCALED:
            SWR_INVALID("Type should not be zero extended!");
            conversionFactor = nullptr;
            break;
        default:
            SWR_ASSERT(conversionType == CONVERT_NONE);
            conversionFactor = nullptr;
            break;
        }

        // shuffle enabled components into lower word of each 32bit lane, 0 extending to 32 bits
        for (uint32_t i = 0; i < 4; i++)
        {
            if (isComponentEnabled(compMask, i))
            {
                if (compCtrl[i] == ComponentControl::StoreSrc)
                {
                    // select correct constMask for x/z or y/w pshufb
                    uint32_t selectedMask = ((i == 0) || (i == 2)) ? 0 : 1;
                    // if x or y, use vi128XY permute result, else use vi128ZW
                    uint32_t selectedGather = (i < 2) ? 0 : 1;

                    // SIMD16 PSHUFB isn't part of AVX-512F, so split into SIMD8 for the sake of KNL,
                    // for now..

                    Value* vGatherResult_lo = EXTRACT_16(vGatherResult[selectedGather], 0);
                    Value* vGatherResult_hi = EXTRACT_16(vGatherResult[selectedGather], 1);

                    Value* temp_lo = BITCAST(
                        PSHUFB(BITCAST(vGatherResult_lo, v32x8Ty), vConstMask[selectedMask]),
                        vGatherTy);
                    Value* temp_hi = BITCAST(
                        PSHUFB(BITCAST(vGatherResult_hi, v32x8Ty), vConstMask[selectedMask]),
                        vGatherTy);

                    // after pshufb mask for x channel; z uses the same shuffle from the second
                    // gather 256i - 0    1    2    3    4    5    6    7
                    //        xx00 xx00 xx00 xx00 xx00 xx00 xx00 xx00

                    Value* temp = JOIN_16(temp_lo, temp_hi);

                    // denormalize if needed
                    if (conversionType != CONVERT_NONE)
                    {
                        temp = FMUL(CAST(fpCast, temp, mSimdFP32Ty), conversionFactor);
                    }

                    vVertexElements[currentVertexElement] = temp;

                    currentVertexElement += 1;
                }
                else
                {
                    vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
                }

                if (currentVertexElement > 3)
                {
                    StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                    // reset to the next vVertexElement to output
                    currentVertexElement = 0;
                }
            }
        }
    }
    else
    {
        SWR_INVALID("Unsupported conversion type");
    }
}

void FetchJit::Shuffle16bpcGather(Shuffle16bpcArgs& args)
{
    // Unpack tuple args
    Value*(&vGatherResult)[2]                       = std::get<0>(args);
    Value*                     pVtxOut              = std::get<1>(args);
    const Instruction::CastOps extendType           = std::get<2>(args);
    const ConversionType       conversionType       = std::get<3>(args);
    uint32_t&                  currentVertexElement = std::get<4>(args);
    uint32_t&                  outputElt            = std::get<5>(args);
    const ComponentEnable      compMask             = std::get<6>(args);
    const ComponentControl(&compCtrl)[4]            = std::get<7>(args);
    Value*(&vVertexElements)[4]                     = std::get<8>(args);

    // cast types
    Type* vGatherTy = getVectorType(IntegerType::getInt32Ty(JM()->mContext), mVWidth);
    Type* v32x8Ty   = getVectorType(mInt8Ty, mVWidth * 4); // vwidth is units of 32 bits

    // have to do extra work for sign extending
    if ((extendType == Instruction::CastOps::SExt) ||
        (extendType == Instruction::CastOps::SIToFP) || (extendType == Instruction::CastOps::FPExt))
    {
        // is this PP float?
        bool bFP = (extendType == Instruction::CastOps::FPExt) ? true : false;

        Type* v8x16Ty   = getVectorType(mInt16Ty, 8); // 8x16bit in a 128bit lane
        Type* v128bitTy = getVectorType(IntegerType::getIntNTy(JM()->mContext, 128),
                                          mVWidth / 4); // vwidth is units of 32 bits

        // shuffle mask
        Value* vConstMask = C<char>({0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15,
                                     0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15});
        Value* vi128XY    = nullptr;
        if (isComponentEnabled(compMask, 0) || isComponentEnabled(compMask, 1))
        {
            Value* vShufResult =
                BITCAST(PSHUFB(BITCAST(vGatherResult[0], v32x8Ty), vConstMask), vGatherTy);
            // after pshufb: group components together in each 128bit lane
            // 256i - 0    1    2    3    4    5    6    7
            //        xxxx xxxx yyyy yyyy xxxx xxxx yyyy yyyy

            vi128XY = BITCAST(VPERMD(vShufResult, C<int32_t>({0, 1, 4, 5, 2, 3, 6, 7})), v128bitTy);
            // after PERMD: move and pack xy components into each 128bit lane
            // 256i - 0    1    2    3    4    5    6    7
            //        xxxx xxxx xxxx xxxx yyyy yyyy yyyy yyyy
        }

        // do the same for zw components
        Value* vi128ZW = nullptr;
        if (isComponentEnabled(compMask, 2) || isComponentEnabled(compMask, 3))
        {
            Value* vShufResult =
                BITCAST(PSHUFB(BITCAST(vGatherResult[1], v32x8Ty), vConstMask), vGatherTy);
            vi128ZW = BITCAST(VPERMD(vShufResult, C<int32_t>({0, 1, 4, 5, 2, 3, 6, 7})), v128bitTy);
        }

        // init denormalize variables if needed
        Instruction::CastOps IntToFpCast;
        Value*               conversionFactor;

        switch (conversionType)
        {
        case CONVERT_NORMALIZED:
            IntToFpCast      = Instruction::CastOps::SIToFP;
            conversionFactor = VIMMED1((float)(1.0 / 32767.0));
            break;
        case CONVERT_SSCALED:
            IntToFpCast      = Instruction::CastOps::SIToFP;
            conversionFactor = VIMMED1((float)(1.0));
            break;
        case CONVERT_USCALED:
            SWR_INVALID("Type should not be sign extended!");
            conversionFactor = nullptr;
            break;
        default:
            SWR_ASSERT(conversionType == CONVERT_NONE);
            conversionFactor = nullptr;
            break;
        }

        // sign extend all enabled components. If we have a fill vVertexElements, output to current
        // simdvertex
        for (uint32_t i = 0; i < 4; i++)
        {
            if (isComponentEnabled(compMask, i))
            {
                if (compCtrl[i] == ComponentControl::StoreSrc)
                {
                    // if x or z, extract 128bits from lane 0, else for y or w, extract from lane 1
                    uint32_t lane = ((i == 0) || (i == 2)) ? 0 : 1;
                    // if x or y, use vi128XY permute result, else use vi128ZW
                    Value* selectedPermute = (i < 2) ? vi128XY : vi128ZW;

                    if (bFP)
                    {
                        // extract 128 bit lanes to sign extend each component
                        vVertexElements[currentVertexElement] =
                            CVTPH2PS(BITCAST(VEXTRACT(selectedPermute, C(lane)), v8x16Ty));
                    }
                    else
                    {
                        // extract 128 bit lanes to sign extend each component
                        vVertexElements[currentVertexElement] =
                            PMOVSXWD(BITCAST(VEXTRACT(selectedPermute, C(lane)), v8x16Ty));

                        // denormalize if needed
                        if (conversionType != CONVERT_NONE)
                        {
                            vVertexElements[currentVertexElement] =
                                FMUL(CAST(IntToFpCast,
                                          vVertexElements[currentVertexElement],
                                          mSimdFP32Ty),
                                     conversionFactor);
                        }
                    }
                    currentVertexElement++;
                }
                else
                {
                    vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
                }

                if (currentVertexElement > 3)
                {
                    StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                    // reset to the next vVertexElement to output
                    currentVertexElement = 0;
                }
            }
        }
    }
    // else zero extend
    else if ((extendType == Instruction::CastOps::ZExt) ||
             (extendType == Instruction::CastOps::UIToFP))
    {
        // pshufb masks for each component
        Value* vConstMask[2];
        if (isComponentEnabled(compMask, 0) || isComponentEnabled(compMask, 2))
        {
            // x/z shuffle mask
            vConstMask[0] = C<char>({
                0, 1, -1, -1, 4, 5, -1, -1, 8, 9, -1, -1, 12, 13, -1, -1,
                0, 1, -1, -1, 4, 5, -1, -1, 8, 9, -1, -1, 12, 13, -1, -1,
            });
        }

        if (isComponentEnabled(compMask, 1) || isComponentEnabled(compMask, 3))
        {
            // y/w shuffle mask
            vConstMask[1] = C<char>({2, 3, -1, -1, 6, 7, -1, -1, 10, 11, -1, -1, 14, 15, -1, -1,
                                     2, 3, -1, -1, 6, 7, -1, -1, 10, 11, -1, -1, 14, 15, -1, -1});
        }

        // init denormalize variables if needed
        Instruction::CastOps fpCast;
        Value*               conversionFactor;

        switch (conversionType)
        {
        case CONVERT_NORMALIZED:
            fpCast           = Instruction::CastOps::UIToFP;
            conversionFactor = VIMMED1((float)(1.0 / 65535.0));
            break;
        case CONVERT_USCALED:
            fpCast           = Instruction::CastOps::UIToFP;
            conversionFactor = VIMMED1((float)(1.0f));
            break;
        case CONVERT_SSCALED:
            SWR_INVALID("Type should not be zero extended!");
            conversionFactor = nullptr;
            break;
        default:
            SWR_ASSERT(conversionType == CONVERT_NONE);
            conversionFactor = nullptr;
            break;
        }

        // shuffle enabled components into lower word of each 32bit lane, 0 extending to 32 bits
        for (uint32_t i = 0; i < 4; i++)
        {
            if (isComponentEnabled(compMask, i))
            {
                if (compCtrl[i] == ComponentControl::StoreSrc)
                {
                    // select correct constMask for x/z or y/w pshufb
                    uint32_t selectedMask = ((i == 0) || (i == 2)) ? 0 : 1;
                    // if x or y, use vi128XY permute result, else use vi128ZW
                    uint32_t selectedGather = (i < 2) ? 0 : 1;

                    vVertexElements[currentVertexElement] =
                        BITCAST(PSHUFB(BITCAST(vGatherResult[selectedGather], v32x8Ty),
                                       vConstMask[selectedMask]),
                                vGatherTy);
                    // after pshufb mask for x channel; z uses the same shuffle from the second
                    // gather 256i - 0    1    2    3    4    5    6    7
                    //        xx00 xx00 xx00 xx00 xx00 xx00 xx00 xx00

                    // denormalize if needed
                    if (conversionType != CONVERT_NONE)
                    {
                        vVertexElements[currentVertexElement] =
                            FMUL(CAST(fpCast, vVertexElements[currentVertexElement], mSimdFP32Ty),
                                 conversionFactor);
                    }
                    currentVertexElement++;
                }
                else
                {
                    vVertexElements[currentVertexElement++] = GenerateCompCtrlVector(compCtrl[i]);
                }

                if (currentVertexElement > 3)
                {
                    StoreVertexElements(pVtxOut, outputElt++, 4, vVertexElements);
                    // reset to the next vVertexElement to output
                    currentVertexElement = 0;
                }
            }
        }
    }
    else
    {
        SWR_INVALID("Unsupported conversion type");
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Output a simdvertex worth of elements to the current outputElt
/// @param pVtxOut - base address of VIN output struct
/// @param outputElt - simdvertex offset in VIN to write to
/// @param numEltsToStore - number of simdvertex rows to write out
/// @param vVertexElements - LLVM Value*[] simdvertex to write out
void FetchJit::StoreVertexElements(Value*         pVtxOut,
                                   const uint32_t outputElt,
                                   const uint32_t numEltsToStore,
                                   Value* (&vVertexElements)[4])
{
    SWR_ASSERT(numEltsToStore <= 4, "Invalid element count.");

    for (uint32_t c = 0; c < numEltsToStore; ++c)
    {
        // STORE expects FP32 x vWidth type, just bitcast if needed
        if (!vVertexElements[c]->getType()->getScalarType()->isFloatTy())
        {
#if FETCH_DUMP_VERTEX
            PRINT("vVertexElements[%d]: 0x%x\n", {C(c), vVertexElements[c]});
#endif
            vVertexElements[c] = BITCAST(vVertexElements[c], mSimdFP32Ty);
        }
#if FETCH_DUMP_VERTEX
        else
        {
            PRINT("vVertexElements[%d]: %f\n", {C(c), vVertexElements[c]});
        }
#endif
        // outputElt * 4 = offsetting by the size of a simdvertex
        // + c offsets to a 32bit x vWidth row within the current vertex
        Value* dest = GEP(pVtxOut, C(outputElt * 4 + c), nullptr, "destGEP");
        STORE(vVertexElements[c], dest);
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Generates a constant vector of values based on the
/// ComponentControl value
/// @param ctrl - ComponentControl value
Value* FetchJit::GenerateCompCtrlVector(const ComponentControl ctrl)
{
    switch (ctrl)
    {
    case NoStore:
        return VUNDEF_I();
    case Store0:
        return VIMMED1(0);
    case Store1Fp:
        return VIMMED1(1.0f);
    case Store1Int:
        return VIMMED1(1);
    case StoreVertexId:
    {
        if (mVWidth == 16)
        {
            Type*  pSimd8FPTy = getVectorType(mFP32Ty, 8);
            Value* pIdLo =
                BITCAST(LOAD(GEP(mpFetchInfo, {0, SWR_FETCH_CONTEXT_VertexID})), pSimd8FPTy);
            Value* pIdHi =
                BITCAST(LOAD(GEP(mpFetchInfo, {0, SWR_FETCH_CONTEXT_VertexID2})), pSimd8FPTy);
            return JOIN_16(pIdLo, pIdHi);
        }
        else
        {
            return BITCAST(LOAD(GEP(mpFetchInfo, {0, SWR_FETCH_CONTEXT_VertexID})), mSimdFP32Ty);
        }
    }
    case StoreInstanceId:
    {
        Value* pId = BITCAST(LOAD(GEP(mpFetchInfo, {0, SWR_FETCH_CONTEXT_CurInstance})), mFP32Ty);
        return VBROADCAST(pId);
    }


    case StoreSrc:
    default:
        SWR_INVALID("Invalid component control");
        return VUNDEF_I();
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Returns the enable mask for the specified component.
/// @param enableMask - enable bits
/// @param component - component to check if enabled.
bool isComponentEnabled(ComponentEnable enableMask, uint8_t component)
{
    switch (component)
    {
        // X
    case 0:
        return (enableMask & ComponentEnable::X);
        // Y
    case 1:
        return (enableMask & ComponentEnable::Y);
        // Z
    case 2:
        return (enableMask & ComponentEnable::Z);
        // W
    case 3:
        return (enableMask & ComponentEnable::W);

    default:
        return false;
    }
}

// Don't want two threads compiling the same fetch shader simultaneously
// Has problems in the JIT cache implementation
// This is only a problem for fetch right now.
static std::mutex gFetchCodegenMutex;

//////////////////////////////////////////////////////////////////////////
/// @brief JITs from fetch shader IR
/// @param hJitMgr - JitManager handle
/// @param func   - LLVM function IR
/// @return PFN_FETCH_FUNC - pointer to fetch code
PFN_FETCH_FUNC JitFetchFunc(HANDLE hJitMgr, const HANDLE hFunc)
{
    const llvm::Function* func    = (const llvm::Function*)hFunc;
    JitManager*           pJitMgr = reinterpret_cast<JitManager*>(hJitMgr);
    PFN_FETCH_FUNC        pfnFetch;

    gFetchCodegenMutex.lock();
    pfnFetch = (PFN_FETCH_FUNC)(pJitMgr->mpExec->getFunctionAddress(func->getName().str()));
    // MCJIT finalizes modules the first time you JIT code from them. After finalized, you cannot
    // add new IR to the module
    pJitMgr->mIsModuleFinalized = true;

#if defined(KNOB_SWRC_TRACING)
    char        fName[1024];
    const char* funcName = func->getName().data();
    sprintf(fName, "%s.bin", funcName);
    FILE* fd = fopen(fName, "wb");
    fwrite((void*)pfnFetch, 1, 2048, fd);
    fclose(fd);
#endif

    pJitMgr->DumpAsm(const_cast<llvm::Function*>(func), "final");
    gFetchCodegenMutex.unlock();


    return pfnFetch;
}

//////////////////////////////////////////////////////////////////////////
/// @brief JIT compiles fetch shader
/// @param hJitMgr - JitManager handle
/// @param state   - fetch state to build function from
extern "C" PFN_FETCH_FUNC JITCALL JitCompileFetch(HANDLE hJitMgr, const FETCH_COMPILE_STATE& state)
{
    JitManager* pJitMgr = reinterpret_cast<JitManager*>(hJitMgr);

    pJitMgr->SetupNewModule();

    FetchJit theJit(pJitMgr);
    HANDLE   hFunc = theJit.Create(state);

    return JitFetchFunc(hJitMgr, hFunc);
}
