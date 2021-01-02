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

#include "jit_pch.hpp"
#include "builder.h"

namespace SwrJit
{
    using namespace llvm;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Contructor for Builder.
    /// @param pJitMgr - JitManager which contains modules, function passes, etc.
    Builder::Builder(JitManager* pJitMgr) : mpJitMgr(pJitMgr), mpPrivateContext(nullptr)
    {
        mVWidth   = pJitMgr->mVWidth;
        mVWidth16 = 16;

        mpIRBuilder = &pJitMgr->mBuilder;

        // Built in types: scalar

        mVoidTy     = Type::getVoidTy(pJitMgr->mContext);
        mFP16Ty     = Type::getHalfTy(pJitMgr->mContext);
        mFP32Ty     = Type::getFloatTy(pJitMgr->mContext);
        mFP32PtrTy  = PointerType::get(mFP32Ty, 0);
        mDoubleTy   = Type::getDoubleTy(pJitMgr->mContext);
        mInt1Ty     = Type::getInt1Ty(pJitMgr->mContext);
        mInt8Ty     = Type::getInt8Ty(pJitMgr->mContext);
        mInt16Ty    = Type::getInt16Ty(pJitMgr->mContext);
        mInt32Ty    = Type::getInt32Ty(pJitMgr->mContext);
        mInt64Ty    = Type::getInt64Ty(pJitMgr->mContext);
        mInt8PtrTy  = PointerType::get(mInt8Ty, 0);
        mInt16PtrTy = PointerType::get(mInt16Ty, 0);
        mInt32PtrTy = PointerType::get(mInt32Ty, 0);
        mInt64PtrTy = PointerType::get(mInt64Ty, 0);
        mHandleTy   = mInt8PtrTy;

        mSimd4FP64Ty = getVectorType(mDoubleTy, 4);

        // Built in types: target simd
        SetTargetWidth(pJitMgr->mVWidth);

        // Built in types: simd16

        mSimd16Int1Ty     = getVectorType(mInt1Ty, mVWidth16);
        mSimd16Int16Ty    = getVectorType(mInt16Ty, mVWidth16);
        mSimd16Int32Ty    = getVectorType(mInt32Ty, mVWidth16);
        mSimd16Int64Ty    = getVectorType(mInt64Ty, mVWidth16);
        mSimd16FP16Ty     = getVectorType(mFP16Ty, mVWidth16);
        mSimd16FP32Ty     = getVectorType(mFP32Ty, mVWidth16);
        mSimd16VectorTy   = ArrayType::get(mSimd16FP32Ty, 4);
        mSimd16VectorTRTy = ArrayType::get(mSimd16FP32Ty, 5);

        mSimd32Int8Ty = getVectorType(mInt8Ty, 32);

        if (sizeof(uint32_t*) == 4)
        {
            mIntPtrTy       = mInt32Ty;
            mSimdIntPtrTy   = mSimdInt32Ty;
            mSimd16IntPtrTy = mSimd16Int32Ty;
        }
        else
        {
            SWR_ASSERT(sizeof(uint32_t*) == 8);

            mIntPtrTy       = mInt64Ty;
            mSimdIntPtrTy   = mSimdInt64Ty;
            mSimd16IntPtrTy = mSimd16Int64Ty;
        }
    }

    void Builder::SetTargetWidth(uint32_t width)
    {
        mVWidth = width;

        mSimdInt1Ty      = getVectorType(mInt1Ty, mVWidth);
        mSimdInt16Ty     = getVectorType(mInt16Ty, mVWidth);
        mSimdInt32Ty     = getVectorType(mInt32Ty, mVWidth);
        mSimdInt64Ty     = getVectorType(mInt64Ty, mVWidth);
        mSimdFP16Ty      = getVectorType(mFP16Ty, mVWidth);
        mSimdFP32Ty      = getVectorType(mFP32Ty, mVWidth);
        mSimdVectorTy    = ArrayType::get(mSimdFP32Ty, 4);
        mSimdVectorIntTy = ArrayType::get(mSimdInt32Ty, 4);
        mSimdVectorTRTy  = ArrayType::get(mSimdFP32Ty, 5);
        mSimdVectorTRIntTy  = ArrayType::get(mSimdInt32Ty, 5);
    }

    /// @brief Mark this alloca as temporary to avoid hoisting later on
    void Builder::SetTempAlloca(Value* inst)
    {
        AllocaInst* pAlloca = dyn_cast<AllocaInst>(inst);
        SWR_ASSERT(pAlloca, "Unexpected non-alloca instruction");
        MDNode* N = MDNode::get(JM()->mContext, MDString::get(JM()->mContext, "is_temp_alloca"));
        pAlloca->setMetadata("is_temp_alloca", N);
    }

    bool Builder::IsTempAlloca(Value* inst)
    {
        AllocaInst* pAlloca = dyn_cast<AllocaInst>(inst);
        SWR_ASSERT(pAlloca, "Unexpected non-alloca instruction");

        return (pAlloca->getMetadata("is_temp_alloca") != nullptr);
    }

    // Returns true if able to find a call instruction to mark
    bool Builder::SetNamedMetaDataOnCallInstr(Instruction* inst, StringRef mdName)
    {
        CallInst* pCallInstr = dyn_cast<CallInst>(inst);
        if (pCallInstr)
        {
            MDNode* N = MDNode::get(JM()->mContext, MDString::get(JM()->mContext, mdName));
            pCallInstr->setMetadata(mdName, N);
            return true;
        }
        else
        {
            // Follow use def chain back up
            for (Use& u : inst->operands())
            {
                Instruction* srcInst = dyn_cast<Instruction>(u.get());
                if (srcInst)
                {
                    if (SetNamedMetaDataOnCallInstr(srcInst, mdName))
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    bool Builder::HasNamedMetaDataOnCallInstr(Instruction* inst, StringRef mdName)
    {
        CallInst* pCallInstr = dyn_cast<CallInst>(inst);

        if (!pCallInstr)
        {
            return false;
        }

        return (pCallInstr->getMetadata(mdName) != nullptr);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Packetizes the type. Assumes SOA conversion.
    Type* Builder::GetVectorType(Type* pType)
    {
        if (pType->isVectorTy())
        {
            return pType;
        }

        // [N x float] should packetize to [N x <8 x float>]
        if (pType->isArrayTy())
        {
            uint32_t arraySize     = pType->getArrayNumElements();
            Type*    pArrayType    = pType->getArrayElementType();
            Type*    pVecArrayType = GetVectorType(pArrayType);
            Type*    pVecType      = ArrayType::get(pVecArrayType, arraySize);
            return pVecType;
        }

        // {float,int} should packetize to {<8 x float>, <8 x int>}
        if (pType->isAggregateType())
        {
            uint32_t              numElems = pType->getStructNumElements();
            SmallVector<Type*, 8> vecTypes;
            for (uint32_t i = 0; i < numElems; ++i)
            {
                Type* pElemType    = pType->getStructElementType(i);
                Type* pVecElemType = GetVectorType(pElemType);
                vecTypes.push_back(pVecElemType);
            }
            Type* pVecType = StructType::get(JM()->mContext, vecTypes);
            return pVecType;
        }

        // [N x float]* should packetize to [N x <8 x float>]*
        if (pType->isPointerTy() && pType->getPointerElementType()->isArrayTy())
        {
            return PointerType::get(GetVectorType(pType->getPointerElementType()),
                                    pType->getPointerAddressSpace());
        }

        // <ty> should packetize to <8 x <ty>>
        Type* vecType = getVectorType(pType, JM()->mVWidth);
        return vecType;
    }
} // namespace SwrJit
