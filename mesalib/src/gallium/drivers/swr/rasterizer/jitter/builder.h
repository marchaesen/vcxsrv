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
 * @file builder.h
 *
 * @brief Includes all the builder related functionality
 *
 * Notes:
 *
 ******************************************************************************/
#pragma once

#include "JitManager.h"
#include "common/formats.h"

namespace SwrJit
{
    ///@todo Move this to better place
    enum SHADER_STATS_COUNTER_TYPE
    {
        STATS_INST_EXECUTED           = 0,
        STATS_SAMPLE_EXECUTED         = 1,
        STATS_SAMPLE_L_EXECUTED       = 2,
        STATS_SAMPLE_B_EXECUTED       = 3,
        STATS_SAMPLE_C_EXECUTED       = 4,
        STATS_SAMPLE_C_LZ_EXECUTED    = 5,
        STATS_SAMPLE_C_D_EXECUTED     = 6,
        STATS_LOD_EXECUTED            = 7,
        STATS_GATHER4_EXECUTED        = 8,
        STATS_GATHER4_C_EXECUTED      = 9,
        STATS_GATHER4_C_PO_EXECUTED   = 10,
        STATS_GATHER4_C_PO_C_EXECUTED = 11,
        STATS_LOAD_RAW_UAV            = 12,
        STATS_LOAD_RAW_RESOURCE       = 13,
        STATS_STORE_RAW_UAV           = 14,
        STATS_STORE_TGSM              = 15,
        STATS_DISCARD                 = 16,
        STATS_BARRIER                 = 17,

        // ------------------
        STATS_TOTAL_COUNTERS
    };

    using namespace llvm;
    struct Builder
    {
        Builder(JitManager* pJitMgr);
        virtual ~Builder() {}

        IRBuilder<>* IRB() { return mpIRBuilder; };
        JitManager*  JM() { return mpJitMgr; }

        JitManager*  mpJitMgr;
        IRBuilder<>* mpIRBuilder;

        uint32_t mVWidth;   // vector width target simd
        uint32_t mVWidth16; // vector width simd16

        // Built in types: scalar

        Type* mVoidTy;
        Type* mHandleTy;
        Type* mInt1Ty;
        Type* mInt8Ty;
        Type* mInt16Ty;
        Type* mInt32Ty;
        Type* mInt64Ty;
        Type* mIntPtrTy;
        Type* mFP16Ty;
        Type* mFP32Ty;
        Type* mFP32PtrTy;
        Type* mDoubleTy;
        Type* mInt8PtrTy;
        Type* mInt16PtrTy;
        Type* mInt32PtrTy;
        Type* mInt64PtrTy;

        Type* mSimd4FP64Ty;

        // Built in types: target SIMD

        Type* mSimdFP16Ty;
        Type* mSimdFP32Ty;
        Type* mSimdInt1Ty;
        Type* mSimdInt16Ty;
        Type* mSimdInt32Ty;
        Type* mSimdInt64Ty;
        Type* mSimdIntPtrTy;
        Type* mSimdVectorTy;
        Type* mSimdVectorTRTy;
        Type* mSimdVectorIntTy;
        Type* mSimdVectorTRIntTy;

        // Built in types: simd16

        Type* mSimd16FP16Ty;
        Type* mSimd16FP32Ty;
        Type* mSimd16Int1Ty;
        Type* mSimd16Int16Ty;
        Type* mSimd16Int32Ty;
        Type* mSimd16Int64Ty;
        Type* mSimd16IntPtrTy;
        Type* mSimd16VectorTy;
        Type* mSimd16VectorTRTy;

        Type* mSimd32Int8Ty;

        void  SetTargetWidth(uint32_t width);
        void  SetTempAlloca(Value* inst);
        bool  IsTempAlloca(Value* inst);
        bool  SetNamedMetaDataOnCallInstr(Instruction* inst, StringRef mdName);
        bool  HasNamedMetaDataOnCallInstr(Instruction* inst, StringRef mdName);
        Type* GetVectorType(Type* pType);
        void  SetMetadata(StringRef s, uint32_t val)
        {
            llvm::NamedMDNode* metaData = mpJitMgr->mpCurrentModule->getOrInsertNamedMetadata(s);
            Constant*          cval     = mpIRBuilder->getInt32(val);
            llvm::MDNode*      mdNode   = llvm::MDNode::get(mpJitMgr->mpCurrentModule->getContext(),
                                                     llvm::ConstantAsMetadata::get(cval));
            if (metaData->getNumOperands())
            {
                metaData->setOperand(0, mdNode);
            }
            else
            {
                metaData->addOperand(mdNode);
            }
        }
        uint32_t GetMetadata(StringRef s)
        {
            NamedMDNode* metaData = mpJitMgr->mpCurrentModule->getNamedMetadata(s);
            if (metaData)
            {
                MDNode*   mdNode = metaData->getOperand(0);
                Metadata* val    = mdNode->getOperand(0);
                return mdconst::dyn_extract<ConstantInt>(val)->getZExtValue();
            }
            else
            {
                return 0;
            }
        }

#include "gen_builder.hpp"
#include "gen_builder_meta.hpp"
#include "gen_builder_intrin.hpp"
#include "builder_misc.h"
#include "builder_math.h"
#include "builder_mem.h"

        void SetPrivateContext(Value* pPrivateContext)
        {
            mpPrivateContext = pPrivateContext;
            NotifyPrivateContextSet();
        }
        virtual void  NotifyPrivateContextSet() {}
        inline Value* GetPrivateContext() { return mpPrivateContext; }

    private:
        Value* mpPrivateContext;
    };
} // namespace SwrJit
