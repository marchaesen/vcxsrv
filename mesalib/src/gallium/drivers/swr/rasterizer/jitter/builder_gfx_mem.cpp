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
 * @file builder_gfx_mem.cpp
 *
 * @brief Definition of the gfx mem builder
 *
 * Notes:
 *
 ******************************************************************************/
#include "jit_pch.hpp"
#include "builder.h"
#include "common/rdtsc_buckets.h"
#include "builder_gfx_mem.h"

namespace SwrJit
{
    using namespace llvm;

    BuilderGfxMem::BuilderGfxMem(JitManager* pJitMgr) : Builder(pJitMgr)
    {
        mpTranslationFuncTy             = nullptr;
        mpfnTranslateGfxAddressForRead  = nullptr;
        mpfnTranslateGfxAddressForWrite = nullptr;
        mpfnTrackMemAccess              = nullptr;
        mpParamSimDC                    = nullptr;
        mpWorkerData                    = nullptr;

    }

    void BuilderGfxMem::NotifyPrivateContextSet()
    {
    }

    void BuilderGfxMem::AssertGFXMemoryParams(Value* ptr, MEM_CLIENT usage)
    {
        SWR_ASSERT(!(ptr->getType() == mInt64Ty && usage == MEM_CLIENT::MEM_CLIENT_INTERNAL),
                   "Internal memory should not be gfxptr_t.");
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Generate a masked gather operation in LLVM IR.  If not
    /// supported on the underlying platform, emulate it with loads
    /// @param vSrc - SIMD wide value that will be loaded if mask is invalid
    /// @param pBase - Int8* base VB address pointer value
    /// @param vIndices - SIMD wide value of VB byte offsets
    /// @param vMask - SIMD wide mask that controls whether to access memory or the src values
    /// @param scale - value to scale indices by
    Value* BuilderGfxMem::GATHERPS(Value*         vSrc,
                                   Value*         pBase,
                                   Value*         vIndices,
                                   Value*         vMask,
                                   uint8_t        scale,
                                   MEM_CLIENT     usage)
    {
       // address may be coming in as 64bit int now so get the pointer
        if (pBase->getType() == mInt64Ty)
        {
            pBase = INT_TO_PTR(pBase, PointerType::get(mInt8Ty, 0));
        }

        Value* vGather = Builder::GATHERPS(vSrc, pBase, vIndices, vMask, scale);
        return vGather;
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Generate a masked gather operation in LLVM IR.  If not
    /// supported on the underlying platform, emulate it with loads
    /// @param vSrc - SIMD wide value that will be loaded if mask is invalid
    /// @param pBase - Int8* base VB address pointer value
    /// @param vIndices - SIMD wide value of VB byte offsets
    /// @param vMask - SIMD wide mask that controls whether to access memory or the src values
    /// @param scale - value to scale indices by
    Value* BuilderGfxMem::GATHERDD(Value*         vSrc,
                                   Value*         pBase,
                                   Value*         vIndices,
                                   Value*         vMask,
                                   uint8_t        scale,
                                   MEM_CLIENT     usage)
    {

        // address may be coming in as 64bit int now so get the pointer
        if (pBase->getType() == mInt64Ty)
        {
            pBase = INT_TO_PTR(pBase, PointerType::get(mInt8Ty, 0));
        }

        Value* vGather = Builder::GATHERDD(vSrc, pBase, vIndices, vMask, scale);
        return vGather;
    }

    void BuilderGfxMem::SCATTERPS(
        Value* pDst, Value* vSrc, Value* vOffsets, Value* vMask, MEM_CLIENT usage)
    {

        // address may be coming in as 64bit int now so get the pointer
        if (pDst->getType() == mInt64Ty)
        {
            pDst = INT_TO_PTR(pDst, PointerType::get(mInt8Ty, 0));
        }

        Builder::SCATTERPS(pDst, BITCAST(vSrc, mSimdFP32Ty), vOffsets, vMask, usage);
    }

    Value* BuilderGfxMem::OFFSET_TO_NEXT_COMPONENT(Value* base, Constant* offset)
    {
        return ADD(base, offset);
    }

    Value* BuilderGfxMem::GEP(Value* Ptr, Value* Idx, Type* Ty, bool isReadOnly, const Twine& Name)
    {
        bool xlate = (Ptr->getType() == mInt64Ty);
        if (xlate)
        {
            Ptr = INT_TO_PTR(Ptr, Ty);
            Ptr = Builder::GEP(Ptr, Idx, nullptr, isReadOnly, Name);
            Ptr = PTR_TO_INT(Ptr, mInt64Ty);
            if (isReadOnly)
            {
                Ptr = TranslationHelper(Ptr, Ty, mpfnTranslateGfxAddressForRead);
            }
            else
            {
                Ptr = TranslationHelper(Ptr, Ty, mpfnTranslateGfxAddressForWrite);
            }
        }
        else
        {
            Ptr = Builder::GEP(Ptr, Idx, nullptr, isReadOnly, Name);
        }
        return Ptr;
    }

    Value* BuilderGfxMem::GEP(Type* Ty, Value* Ptr, Value* Idx, const Twine& Name)
    {
        bool xlate = (Ptr->getType() == mInt64Ty);
        if (xlate)
        {
            Ptr = INT_TO_PTR(Ptr, Ty);
            Ptr = Builder::GEP(Ty, Ptr, Idx, Name);
            Ptr = PTR_TO_INT(Ptr, mInt64Ty);
            Ptr = TranslationHelper(Ptr, Ty, mpfnTranslateGfxAddressForRead);
        }
        else
        {
            Ptr = Builder::GEP(Ty, Ptr, Idx, Name);
        }
        return Ptr;
    }

    Value* BuilderGfxMem::GEP(Value* Ptr, const std::initializer_list<Value*>& indexList, Type* Ty)
    {
        bool xlate = (Ptr->getType() == mInt64Ty);
        if (xlate)
        {
            Ptr = INT_TO_PTR(Ptr, Ty);
            Ptr = Builder::GEP(Ptr, indexList);
            Ptr = PTR_TO_INT(Ptr, mInt64Ty);
            Ptr = TranslationHelper(Ptr, Ty, mpfnTranslateGfxAddressForRead);
        }
        else
        {
            Ptr = Builder::GEP(Ptr, indexList);
        }
        return Ptr;
    }

    Value*
    BuilderGfxMem::GEP(Value* Ptr, const std::initializer_list<uint32_t>& indexList, Type* Ty)
    {
        bool xlate = (Ptr->getType() == mInt64Ty);
        if (xlate)
        {
            Ptr = INT_TO_PTR(Ptr, Ty);
            Ptr = Builder::GEP(Ptr, indexList);
            Ptr = PTR_TO_INT(Ptr, mInt64Ty);
            Ptr = TranslationHelper(Ptr, Ty, mpfnTranslateGfxAddressForRead);
        }
        else
        {
            Ptr = Builder::GEP(Ptr, indexList);
        }
        return Ptr;
    }

    Value* BuilderGfxMem::TranslationHelper(Value* Ptr, Type* Ty, Value* pfnTranslateGfxAddress)
    {
        SWR_ASSERT(!(Ptr->getType() == mInt64Ty && Ty == nullptr),
                   "Access of GFX pointers must have non-null type specified.");

        // address may be coming in as 64bit int now so get the pointer
        if (Ptr->getType() == mInt64Ty)
        {
            Ptr = INT_TO_PTR(Ptr, Ty);
        }

        return Ptr;
    }

    void BuilderGfxMem::TrackerHelper(Value* Ptr, Type* Ty, MEM_CLIENT usage, bool isRead)
    {
#if defined(KNOB_ENABLE_AR)
        if (!KNOB_AR_ENABLE_MEMORY_EVENTS)
        {
            return;
        }

        Value* tmpPtr;
        // convert actual pointers to int64.
        uint32_t size = 0;

        if (Ptr->getType() == mInt64Ty)
        {
            DataLayout dataLayout(JM()->mpCurrentModule);
            size = (uint32_t)dataLayout.getTypeAllocSize(Ty);

            tmpPtr = Ptr;
        }
        else
        {
            DataLayout dataLayout(JM()->mpCurrentModule);
            size = (uint32_t)dataLayout.getTypeAllocSize(Ptr->getType());

            tmpPtr = PTR_TO_INT(Ptr, mInt64Ty);
        }

        // There are some shader compile setups where there's no translation functions set up.
        // This would be a situation where the accesses are to internal rasterizer memory and won't
        // be logged.
        // TODO:  we may wish to revisit this for URB reads/writes, though.
        if (mpfnTrackMemAccess)
        {
            SWR_ASSERT(mpWorkerData != nullptr);
            CALL(mpfnTrackMemAccess,
                 {mpParamSimDC,
                  mpWorkerData,
                  tmpPtr,
                  C((uint32_t)size),
                  C((uint8_t)isRead),
                  C((uint32_t)usage)});
        }
#endif

        return;
    }

    LoadInst* BuilderGfxMem::LOAD(Value* Ptr, const char* Name, Type* Ty, MEM_CLIENT usage)
    {
        AssertGFXMemoryParams(Ptr, usage);
        TrackerHelper(Ptr, Ty, usage, true);

        Ptr = TranslationHelper(Ptr, Ty, mpfnTranslateGfxAddressForRead);
        return Builder::LOAD(Ptr, Name);
    }

    LoadInst* BuilderGfxMem::LOAD(Value* Ptr, const Twine& Name, Type* Ty, MEM_CLIENT usage)
    {
        AssertGFXMemoryParams(Ptr, usage);
        TrackerHelper(Ptr, Ty, usage, true);

        Ptr = TranslationHelper(Ptr, Ty, mpfnTranslateGfxAddressForRead);
        return Builder::LOAD(Ptr, Name);
    }

    LoadInst* BuilderGfxMem::LOAD(
        Value* Ptr, bool isVolatile, const Twine& Name, Type* Ty, MEM_CLIENT usage)
    {
        AssertGFXMemoryParams(Ptr, usage);
        TrackerHelper(Ptr, Ty, usage, true);

        Ptr = TranslationHelper(Ptr, Ty, mpfnTranslateGfxAddressForRead);
        return Builder::LOAD(Ptr, isVolatile, Name);
    }

    LoadInst* BuilderGfxMem::LOAD(Value*                                 BasePtr,
                                  const std::initializer_list<uint32_t>& offset,
                                  const llvm::Twine&                     name,
                                  Type*                                  Ty,
                                  MEM_CLIENT                             usage)
    {
        AssertGFXMemoryParams(BasePtr, usage);

        bool bNeedTranslation = false;
        if (BasePtr->getType() == mInt64Ty)
        {
            SWR_ASSERT(Ty);
            BasePtr          = INT_TO_PTR(BasePtr, Ty, name);
            bNeedTranslation = true;
        }
        std::vector<Value*> valIndices;
        for (auto i : offset)
        {
            valIndices.push_back(C(i));
        }
        BasePtr = Builder::GEPA(BasePtr, valIndices, name);
        if (bNeedTranslation)
        {
            BasePtr = PTR_TO_INT(BasePtr, mInt64Ty, name);
        }

        return LOAD(BasePtr, name, Ty, usage);
    }

    CallInst* BuilderGfxMem::MASKED_LOAD(Value*         Ptr,
                                         unsigned       Align,
                                         Value*         Mask,
                                         Value*         PassThru,
                                         const Twine&   Name,
                                         Type*          Ty,
                                         MEM_CLIENT     usage)
    {
        AssertGFXMemoryParams(Ptr, usage);
        TrackerHelper(Ptr, Ty, usage, true);

        Ptr = TranslationHelper(Ptr, Ty, mpfnTranslateGfxAddressForRead);
        return Builder::MASKED_LOAD(Ptr, Align, Mask, PassThru, Name, Ty, usage);
    }

    StoreInst*
    BuilderGfxMem::STORE(Value* Val, Value* Ptr, bool isVolatile, Type* Ty, MEM_CLIENT usage)
    {
        AssertGFXMemoryParams(Ptr, usage);
        TrackerHelper(Ptr, Ty, usage, false);

        Ptr = TranslationHelper(Ptr, Ty, mpfnTranslateGfxAddressForRead);
        return Builder::STORE(Val, Ptr, isVolatile, Ty, usage);
    }

    StoreInst* BuilderGfxMem::STORE(Value*                                 Val,
                                    Value*                                 BasePtr,
                                    const std::initializer_list<uint32_t>& offset,
                                    Type*                                  Ty,
                                    MEM_CLIENT                             usage)
    {
        AssertGFXMemoryParams(BasePtr, usage);
        TrackerHelper(BasePtr, Ty, usage, false);

        BasePtr = TranslationHelper(BasePtr, Ty, mpfnTranslateGfxAddressForRead);
        return Builder::STORE(Val, BasePtr, offset, Ty, usage);
    }

    CallInst* BuilderGfxMem::MASKED_STORE(
        Value* Val, Value* Ptr, unsigned Align, Value* Mask, Type* Ty, MEM_CLIENT usage)
    {
        AssertGFXMemoryParams(Ptr, usage);

        TrackerHelper(Ptr, Ty, usage, false);

        Ptr = TranslationHelper(Ptr, Ty, mpfnTranslateGfxAddressForRead);
        return Builder::MASKED_STORE(Val, Ptr, Align, Mask, Ty, usage);
    }

    Value* BuilderGfxMem::TranslateGfxAddressForRead(Value*       xpGfxAddress,
                                                     Type*        PtrTy,
                                                     const Twine& Name,
                                                     MEM_CLIENT /* usage */)
    {
        if (PtrTy == nullptr)
        {
            PtrTy = mInt8PtrTy;
        }
        return INT_TO_PTR(xpGfxAddress, PtrTy, Name);
    }

    Value* BuilderGfxMem::TranslateGfxAddressForWrite(Value*       xpGfxAddress,
                                                      Type*        PtrTy,
                                                      const Twine& Name,
                                                      MEM_CLIENT /* usage */)
    {
        if (PtrTy == nullptr)
        {
            PtrTy = mInt8PtrTy;
        }
        return INT_TO_PTR(xpGfxAddress, PtrTy, Name);
    }

} // namespace SwrJit
