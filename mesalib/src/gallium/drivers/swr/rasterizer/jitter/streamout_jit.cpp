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
 * @file streamout_jit.cpp
 *
 * @brief Implementation of the streamout jitter
 *
 * Notes:
 *
 ******************************************************************************/
#include "jit_pch.hpp"
#include "builder_gfx_mem.h"
#include "jit_api.h"
#include "streamout_jit.h"
#include "gen_state_llvm.h"
#include "functionpasses/passes.h"

using namespace llvm;
using namespace SwrJit;

//////////////////////////////////////////////////////////////////////////
/// Interface to Jitting a fetch shader
//////////////////////////////////////////////////////////////////////////
struct StreamOutJit : public BuilderGfxMem
{
    StreamOutJit(JitManager* pJitMgr) : BuilderGfxMem(pJitMgr){};

    // returns pointer to SWR_STREAMOUT_BUFFER
    Value* getSOBuffer(Value* pSoCtx, uint32_t buffer)
    {
        return LOAD(pSoCtx, {0, SWR_STREAMOUT_CONTEXT_pBuffer, buffer});
    }

    //////////////////////////////////////////////////////////////////////////
    // @brief checks if streamout buffer is oob
    // @return <i1> true/false
    Value* oob(const STREAMOUT_COMPILE_STATE& state, Value* pSoCtx, uint32_t buffer)
    {
        Value* returnMask = C(false);

        Value* pBuf = getSOBuffer(pSoCtx, buffer);

        // load enable
        // @todo bool data types should generate <i1> llvm type
        Value* enabled = TRUNC(LOAD(pBuf, {0, SWR_STREAMOUT_BUFFER_enable}), IRB()->getInt1Ty());

        // load buffer size
        Value* bufferSize = LOAD(pBuf, {0, SWR_STREAMOUT_BUFFER_bufferSize});

        // load current streamOffset
        Value* streamOffset = LOAD(pBuf, {0, SWR_STREAMOUT_BUFFER_streamOffset});

        // load buffer pitch
        Value* pitch = LOAD(pBuf, {0, SWR_STREAMOUT_BUFFER_pitch});

        // buffer is considered oob if in use in a decl but not enabled
        returnMask = OR(returnMask, NOT(enabled));

        // buffer is oob if cannot fit a prims worth of verts
        Value* newOffset = ADD(streamOffset, MUL(pitch, C(state.numVertsPerPrim)));
        returnMask       = OR(returnMask, ICMP_SGT(newOffset, bufferSize));

        return returnMask;
    }

    //////////////////////////////////////////////////////////////////////////
    // @brief converts scalar bitmask to <4 x i32> suitable for shuffle vector,
    //        packing the active mask bits
    //        ex. bitmask 0011 -> (0, 1, 0, 0)
    //            bitmask 1000 -> (3, 0, 0, 0)
    //            bitmask 1100 -> (2, 3, 0, 0)
    Value* PackMask(uint32_t bitmask)
    {
        std::vector<Constant*> indices(4, C(0));
        unsigned long          index;
        uint32_t               elem = 0;
        while (_BitScanForward(&index, bitmask))
        {
            indices[elem++] = C((int)index);
            bitmask &= ~(1 << index);
        }

        return ConstantVector::get(indices);
    }

    //////////////////////////////////////////////////////////////////////////
    // @brief convert scalar bitmask to <4xfloat> bitmask
    Value* ToMask(uint32_t bitmask)
    {
        std::vector<Constant*> indices;
        for (uint32_t i = 0; i < 4; ++i)
        {
            if (bitmask & (1 << i))
            {
                indices.push_back(C(true));
            }
            else
            {
                indices.push_back(C(false));
            }
        }
        return ConstantVector::get(indices);
    }

    //////////////////////////////////////////////////////////////////////////
    // @brief processes a single decl from the streamout stream. Reads 4 components from the input
    //        stream and writes N components to the output buffer given the componentMask or if
    //        a hole, just increments the buffer pointer
    // @param pStream - pointer to current attribute
    // @param pOutBuffers - pointers to the current location of each output buffer
    // @param decl - input decl
    void buildDecl(Value* pStream, Value* pOutBuffers[4], const STREAMOUT_DECL& decl)
    {
        uint32_t numComponents = _mm_popcnt_u32(decl.componentMask);
        uint32_t packedMask    = (1 << numComponents) - 1;
        if (!decl.hole)
        {
            // increment stream pointer to correct slot
            Value* pAttrib = GEP(pStream, C(4 * decl.attribSlot));

            // load 4 components from stream
            Type* simd4Ty    = getVectorType(IRB()->getFloatTy(), 4);
            Type* simd4PtrTy = PointerType::get(simd4Ty, 0);
            pAttrib          = BITCAST(pAttrib, simd4PtrTy);
            Value* vattrib   = LOAD(pAttrib);

            // shuffle/pack enabled components
            Value* vpackedAttrib = VSHUFFLE(vattrib, vattrib, PackMask(decl.componentMask));

            // store to output buffer
            // cast SO buffer to i8*, needed by maskstore
            Value* pOut = BITCAST(pOutBuffers[decl.bufferIndex], PointerType::get(simd4Ty, 0));

            // cast input to <4xfloat>
            Value* src = BITCAST(vpackedAttrib, simd4Ty);

            // cast mask to <4xi1>
            Value* mask = ToMask(packedMask);
            MASKED_STORE(src, pOut, 4, mask, PointerType::get(simd4Ty, 0), MEM_CLIENT::GFX_MEM_CLIENT_STREAMOUT);
        }

        // increment SO buffer
        pOutBuffers[decl.bufferIndex] = GEP(pOutBuffers[decl.bufferIndex], C(numComponents));
    }

    //////////////////////////////////////////////////////////////////////////
    // @brief builds a single vertex worth of data for the given stream
    // @param streamState - state for this stream
    // @param pCurVertex - pointer to src stream vertex data
    // @param pOutBuffer - pointers to up to 4 SO buffers
    void buildVertex(const STREAMOUT_STREAM& streamState, Value* pCurVertex, Value* pOutBuffer[4])
    {
        for (uint32_t d = 0; d < streamState.numDecls; ++d)
        {
            const STREAMOUT_DECL& decl = streamState.decl[d];
            buildDecl(pCurVertex, pOutBuffer, decl);
        }
    }

    void buildStream(const STREAMOUT_COMPILE_STATE& state,
                     const STREAMOUT_STREAM&        streamState,
                     Value*                         pSoCtx,
                     BasicBlock*                    returnBB,
                     Function*                      soFunc)
    {
        // get list of active SO buffers
        std::unordered_set<uint32_t> activeSOBuffers;
        for (uint32_t d = 0; d < streamState.numDecls; ++d)
        {
            const STREAMOUT_DECL& decl = streamState.decl[d];
            activeSOBuffers.insert(decl.bufferIndex);
        }

        // always increment numPrimStorageNeeded
        Value* numPrimStorageNeeded = LOAD(pSoCtx, {0, SWR_STREAMOUT_CONTEXT_numPrimStorageNeeded});
        numPrimStorageNeeded        = ADD(numPrimStorageNeeded, C(1));
        STORE(numPrimStorageNeeded, pSoCtx, {0, SWR_STREAMOUT_CONTEXT_numPrimStorageNeeded});

        // check OOB on active SO buffers.  If any buffer is out of bound, don't write
        // the primitive to any buffer
        Value* oobMask = C(false);
        for (uint32_t buffer : activeSOBuffers)
        {
            oobMask = OR(oobMask, oob(state, pSoCtx, buffer));
        }

        BasicBlock* validBB = BasicBlock::Create(JM()->mContext, "valid", soFunc);

        // early out if OOB
        COND_BR(oobMask, returnBB, validBB);

        IRB()->SetInsertPoint(validBB);

        Value* numPrimsWritten = LOAD(pSoCtx, {0, SWR_STREAMOUT_CONTEXT_numPrimsWritten});
        numPrimsWritten        = ADD(numPrimsWritten, C(1));
        STORE(numPrimsWritten, pSoCtx, {0, SWR_STREAMOUT_CONTEXT_numPrimsWritten});

        // compute start pointer for each output buffer
        Value* pOutBuffer[4];
        Value* pOutBufferStartVertex[4];
        Value* outBufferPitch[4];
        for (uint32_t b : activeSOBuffers)
        {
            Value* pBuf              = getSOBuffer(pSoCtx, b);
            Value* pData             = LOAD(pBuf, {0, SWR_STREAMOUT_BUFFER_pBuffer});
            Value* streamOffset      = LOAD(pBuf, {0, SWR_STREAMOUT_BUFFER_streamOffset});
            pOutBuffer[b] = GEP(pData, streamOffset, PointerType::get(IRB()->getInt32Ty(), 0));
            pOutBufferStartVertex[b] = pOutBuffer[b];

            outBufferPitch[b] = LOAD(pBuf, {0, SWR_STREAMOUT_BUFFER_pitch});
        }

        // loop over the vertices of the prim
        Value* pStreamData = LOAD(pSoCtx, {0, SWR_STREAMOUT_CONTEXT_pPrimData});
        for (uint32_t v = 0; v < state.numVertsPerPrim; ++v)
        {
            buildVertex(streamState, pStreamData, pOutBuffer);

            // increment stream and output buffer pointers
            // stream verts are always 32*4 dwords apart
            pStreamData = GEP(pStreamData, C(SWR_VTX_NUM_SLOTS * 4));

            // output buffers offset using pitch in buffer state
            for (uint32_t b : activeSOBuffers)
            {
                pOutBufferStartVertex[b] = GEP(pOutBufferStartVertex[b], outBufferPitch[b]);
                pOutBuffer[b]            = pOutBufferStartVertex[b];
            }
        }

        // update each active buffer's streamOffset
        for (uint32_t b : activeSOBuffers)
        {
            Value* pBuf         = getSOBuffer(pSoCtx, b);
            Value* streamOffset = LOAD(pBuf, {0, SWR_STREAMOUT_BUFFER_streamOffset});
            streamOffset = ADD(streamOffset, MUL(C(state.numVertsPerPrim), outBufferPitch[b]));
            STORE(streamOffset, pBuf, {0, SWR_STREAMOUT_BUFFER_streamOffset});
        }
    }

    Function* Create(const STREAMOUT_COMPILE_STATE& state)
    {
        std::stringstream fnName("SO_",
                                 std::ios_base::in | std::ios_base::out | std::ios_base::ate);
        fnName << ComputeCRC(0, &state, sizeof(state));

        std::vector<Type*> args{
            mInt8PtrTy,
            mInt8PtrTy,
            PointerType::get(Gen_SWR_STREAMOUT_CONTEXT(JM()), 0), // SWR_STREAMOUT_CONTEXT*
        };

        FunctionType* fTy    = FunctionType::get(IRB()->getVoidTy(), args, false);
        Function*     soFunc = Function::Create(
            fTy, GlobalValue::ExternalLinkage, fnName.str(), JM()->mpCurrentModule);

        soFunc->getParent()->setModuleIdentifier(soFunc->getName());

        // create return basic block
        BasicBlock* entry    = BasicBlock::Create(JM()->mContext, "entry", soFunc);
        BasicBlock* returnBB = BasicBlock::Create(JM()->mContext, "return", soFunc);

        IRB()->SetInsertPoint(entry);

        // arguments
        auto   argitr = soFunc->arg_begin();

        Value* privateContext = &*argitr++;
        privateContext->setName("privateContext");
        SetPrivateContext(privateContext);

        mpWorkerData = &*argitr;
        ++argitr;
        mpWorkerData->setName("pWorkerData");

        Value* pSoCtx = &*argitr++;
        pSoCtx->setName("pSoCtx");

        const STREAMOUT_STREAM& streamState = state.stream;
        buildStream(state, streamState, pSoCtx, returnBB, soFunc);

        BR(returnBB);

        IRB()->SetInsertPoint(returnBB);
        RET_VOID();

        JitManager::DumpToFile(soFunc, "SoFunc");

        ::FunctionPassManager passes(JM()->mpCurrentModule);

        passes.add(createBreakCriticalEdgesPass());
        passes.add(createCFGSimplificationPass());
        passes.add(createEarlyCSEPass());
        passes.add(createPromoteMemoryToRegisterPass());
        passes.add(createCFGSimplificationPass());
        passes.add(createEarlyCSEPass());
        passes.add(createInstructionCombiningPass());
#if LLVM_VERSION_MAJOR <= 11
        passes.add(createConstantPropagationPass());
#endif
        passes.add(createSCCPPass());
        passes.add(createAggressiveDCEPass());

        passes.add(createLowerX86Pass(this));

        passes.run(*soFunc);

        JitManager::DumpToFile(soFunc, "SoFunc_optimized");


        return soFunc;
    }
};

//////////////////////////////////////////////////////////////////////////
/// @brief JITs from streamout shader IR
/// @param hJitMgr - JitManager handle
/// @param func   - LLVM function IR
/// @return PFN_SO_FUNC - pointer to SOS function
PFN_SO_FUNC JitStreamoutFunc(HANDLE hJitMgr, const HANDLE hFunc)
{
    llvm::Function* func    = (llvm::Function*)hFunc;
    JitManager*     pJitMgr = reinterpret_cast<JitManager*>(hJitMgr);
    PFN_SO_FUNC     pfnStreamOut;
    pfnStreamOut = (PFN_SO_FUNC)(pJitMgr->mpExec->getFunctionAddress(func->getName().str()));
    // MCJIT finalizes modules the first time you JIT code from them. After finalized, you cannot
    // add new IR to the module
    pJitMgr->mIsModuleFinalized = true;

    pJitMgr->DumpAsm(func, "SoFunc_optimized");


    return pfnStreamOut;
}

//////////////////////////////////////////////////////////////////////////
/// @brief JIT compiles streamout shader
/// @param hJitMgr - JitManager handle
/// @param state   - SO state to build function from
extern "C" PFN_SO_FUNC JITCALL JitCompileStreamout(HANDLE                         hJitMgr,
                                                   const STREAMOUT_COMPILE_STATE& state)
{
    JitManager* pJitMgr = reinterpret_cast<JitManager*>(hJitMgr);

    STREAMOUT_COMPILE_STATE soState = state;
    if (soState.offsetAttribs)
    {
        for (uint32_t i = 0; i < soState.stream.numDecls; ++i)
        {
            soState.stream.decl[i].attribSlot -= soState.offsetAttribs;
        }
    }

    pJitMgr->SetupNewModule();

    StreamOutJit theJit(pJitMgr);
    HANDLE       hFunc = theJit.Create(soState);

    return JitStreamoutFunc(hJitMgr, hFunc);
}
