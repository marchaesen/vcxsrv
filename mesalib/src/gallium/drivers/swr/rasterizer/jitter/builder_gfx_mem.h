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
 * @file builder_gfx_mem.h
 *
 * @brief Definition of the builder to support different translation types for gfx memory access
 *
 * Notes:
 *
 ******************************************************************************/
#pragma once

#include "builder.h"

namespace SwrJit
{
    using namespace llvm;

    class BuilderGfxMem : public Builder
    {
    public:
        BuilderGfxMem(JitManager* pJitMgr);
        virtual ~BuilderGfxMem() {}

        virtual Value* GEP(Value* Ptr, Value* Idx, Type* Ty = nullptr, bool isReadOnly = true, const Twine& Name = "");
        virtual Value* GEP(Type* Ty, Value* Ptr, Value* Idx, const Twine& Name = "");
        virtual Value*
        GEP(Value* Ptr, const std::initializer_list<Value*>& indexList, Type* Ty = nullptr);
        virtual Value*
        GEP(Value* Ptr, const std::initializer_list<uint32_t>& indexList, Type* Ty = nullptr);

        virtual LoadInst* LOAD(Value*         Ptr,
                               const char*    Name,
                               Type*          Ty    = nullptr,
                               MEM_CLIENT usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);
        virtual LoadInst* LOAD(Value*         Ptr,
                               const Twine&   Name  = "",
                               Type*          Ty    = nullptr,
                               MEM_CLIENT     usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);
        virtual LoadInst* LOAD(Value*         Ptr,
                               bool           isVolatile,
                               const Twine&   Name  = "",
                               Type*          Ty    = nullptr,
                               MEM_CLIENT     usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);
        virtual LoadInst* LOAD(Value*                                 BasePtr,
                               const std::initializer_list<uint32_t>& offset,
                               const llvm::Twine&                     Name  = "",
                               Type*                                  Ty    = nullptr,
                               MEM_CLIENT                         usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);

        virtual CallInst* MASKED_LOAD(Value*         Ptr,
                                      unsigned       Align,
                                      Value*         Mask,
                                      Value*         PassThru = nullptr,
                                      const Twine&   Name     = "",
                                      Type*          Ty       = nullptr,
                                      MEM_CLIENT     usage    = MEM_CLIENT::MEM_CLIENT_INTERNAL);

        virtual StoreInst* STORE(Value *Val, Value *Ptr, bool isVolatile = false, Type* Ty = nullptr, MEM_CLIENT usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);
        
        virtual StoreInst* STORE(Value* Val, Value* BasePtr, const std::initializer_list<uint32_t>& offset, Type* Ty = nullptr, MEM_CLIENT usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);

        virtual CallInst* MASKED_STORE(Value *Val, Value *Ptr, unsigned Align, Value *Mask, Type* Ty = nullptr, MEM_CLIENT usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);

        virtual Value* GATHERPS(Value*         src,
                                Value*         pBase,
                                Value*         indices,
                                Value*         mask,
                                uint8_t        scale = 1,
                                MEM_CLIENT     usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);
        virtual Value* GATHERDD(Value*         src,
                                Value*         pBase,
                                Value*         indices,
                                Value*         mask,
                                uint8_t        scale = 1,
                                MEM_CLIENT     usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);

        virtual void SCATTERPS(Value*         pDst,
                               Value*         vSrc,
                               Value*         vOffsets,
                               Value*         vMask,
                               MEM_CLIENT     usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);

        Value* TranslateGfxAddressForRead(Value*         xpGfxAddress,
                                          Type*          PtrTy = nullptr,
                                          const Twine&   Name  = "",
                                          MEM_CLIENT     usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);
        Value* TranslateGfxAddressForWrite(Value*         xpGfxAddress,
                                           Type*          PtrTy = nullptr,
                                           const Twine&   Name  = "",
                                           MEM_CLIENT     usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);
        
    protected:
        void AssertGFXMemoryParams(Value* ptr, MEM_CLIENT usage);

        virtual void NotifyPrivateContextSet();

        virtual Value* OFFSET_TO_NEXT_COMPONENT(Value* base, Constant* offset);

        Value* TranslationHelper(Value* Ptr, Type* Ty, Value* pfnTranslateGfxAddress);
        void   TrackerHelper(Value* Ptr, Type* Ty, MEM_CLIENT usage, bool isRead);

        FunctionType* GetTranslationFunctionType() { return mpTranslationFuncTy; }
        Value*        GetTranslationFunctionForRead() { return mpfnTranslateGfxAddressForRead; }
        Value*        GetTranslationFunctionForWrite() { return mpfnTranslateGfxAddressForWrite; }
        Value*        GetParamSimDC() { return mpParamSimDC; }

        Value*        mpWorkerData;

    private:
        FunctionType* mpTranslationFuncTy;
        Value*        mpfnTranslateGfxAddressForRead;
        Value*        mpfnTranslateGfxAddressForWrite;
        Value*        mpParamSimDC;
        Value*        mpfnTrackMemAccess;
    };
} // namespace SwrJit
