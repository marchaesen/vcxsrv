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
 * @file builder_misc.h
 *
 * @brief miscellaneous builder functions
 *
 * Notes:
 *
 ******************************************************************************/
#pragma once

public:
enum class MEM_CLIENT
{
    MEM_CLIENT_INTERNAL,
    GFX_MEM_CLIENT_FETCH,
    GFX_MEM_CLIENT_SAMPLER,
    GFX_MEM_CLIENT_SHADER,
    GFX_MEM_CLIENT_STREAMOUT,
    GFX_MEM_CLIENT_URB
};

protected:
virtual Value* OFFSET_TO_NEXT_COMPONENT(Value* base, Constant* offset);
void           AssertMemoryUsageParams(Value* ptr, MEM_CLIENT usage);

public:
virtual Value* GEP(Value* Ptr, Value* Idx, Type* Ty = nullptr, bool isReadOnly = true, const Twine& Name = "");
virtual Value* GEP(Type* Ty, Value* Ptr, Value* Idx, const Twine& Name = "");
virtual Value* GEP(Value* ptr, const std::initializer_list<Value*>& indexList, Type* Ty = nullptr);
virtual Value*
GEP(Value* ptr, const std::initializer_list<uint32_t>& indexList, Type* Ty = nullptr);

Value* GEPA(Value* Ptr, ArrayRef<Value*> IdxList, const Twine& Name = "");
Value* GEPA(Type* Ty, Value* Ptr, ArrayRef<Value*> IdxList, const Twine& Name = "");

Value* IN_BOUNDS_GEP(Value* ptr, const std::initializer_list<Value*>& indexList);
Value* IN_BOUNDS_GEP(Value* ptr, const std::initializer_list<uint32_t>& indexList);

virtual LoadInst*
                  LOAD(Value* Ptr, const char* Name, Type* Ty = nullptr, MEM_CLIENT usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);
virtual LoadInst* LOAD(Value*         Ptr,
                       const Twine&   Name  = "",
                       Type*          Ty    = nullptr,
                       MEM_CLIENT usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);
virtual LoadInst*
                  LOAD(Type* Ty, Value* Ptr, const Twine& Name = "", MEM_CLIENT usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);
virtual LoadInst* LOAD(Value*         Ptr,
                       bool           isVolatile,
                       const Twine&   Name  = "",
                       Type*          Ty    = nullptr,
                       MEM_CLIENT     usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);
virtual LoadInst* LOAD(Value*                                 BasePtr,
                       const std::initializer_list<uint32_t>& offset,
                       const llvm::Twine&                     Name  = "",
                       Type*                                  Ty    = nullptr,
                       MEM_CLIENT                             usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);

virtual CallInst* MASKED_LOAD(Value*         Ptr,
                              unsigned       Align,
                              Value*         Mask,
                              Value*         PassThru = nullptr,
                              const Twine&   Name     = "",
                              Type*          Ty       = nullptr,
                              MEM_CLIENT usage    = MEM_CLIENT::MEM_CLIENT_INTERNAL)
{
    return IRB()->CreateMaskedLoad(Ptr, AlignType(Align), Mask, PassThru, Name);
}

virtual StoreInst* STORE(Value *Val, Value *Ptr, bool isVolatile = false, Type* Ty = nullptr, MEM_CLIENT usage = MEM_CLIENT::MEM_CLIENT_INTERNAL)
{
    return IRB()->CreateStore(Val, Ptr, isVolatile);
}

virtual StoreInst* STORE(Value* Val, Value* BasePtr, const std::initializer_list<uint32_t>& offset, Type* Ty = nullptr, MEM_CLIENT usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);

virtual CallInst* MASKED_STORE(Value *Val, Value *Ptr, unsigned Align, Value *Mask, Type* Ty = nullptr, MEM_CLIENT usage = MEM_CLIENT::MEM_CLIENT_INTERNAL)
{
    return IRB()->CreateMaskedStore(Val, Ptr, AlignType(Align), Mask);
}

LoadInst*  LOADV(Value* BasePtr, const std::initializer_list<Value*>& offset, const llvm::Twine& name = "");
StoreInst* STOREV(Value* Val, Value* BasePtr, const std::initializer_list<Value*>& offset);

Value* MEM_ADD(Value*                                 i32Incr,
               Value*                                 basePtr,
               const std::initializer_list<uint32_t>& indices,
               const llvm::Twine&                     name = "");

void Gather4(const SWR_FORMAT format,
             Value*           pSrcBase,
             Value*           byteOffsets,
             Value*           mask,
             Value*           vGatherComponents[],
             bool             bPackedOutput,
             MEM_CLIENT       usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);

virtual Value* GATHERPS(Value*         src,
                        Value*         pBase,
                        Value*         indices,
                        Value*         mask,
                        uint8_t        scale = 1,
                        MEM_CLIENT     usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);

void GATHER4PS(const SWR_FORMAT_INFO& info,
               Value*                 pSrcBase,
               Value*                 byteOffsets,
               Value*                 mask,
               Value*                 vGatherComponents[],
               bool                   bPackedOutput,
               MEM_CLIENT             usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);

virtual Value* GATHERDD(Value*         src,
                        Value*         pBase,
                        Value*         indices,
                        Value*         mask,
                        uint8_t        scale = 1,
                        MEM_CLIENT     usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);

void GATHER4DD(const SWR_FORMAT_INFO& info,
               Value*                 pSrcBase,
               Value*                 byteOffsets,
               Value*                 mask,
               Value*                 vGatherComponents[],
               bool                   bPackedOutput,
               MEM_CLIENT             usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);

Value* GATHERPD(Value* src, Value* pBase, Value* indices, Value* mask, uint8_t scale = 1);

Value* GATHER_PTR(Value* pVecSrcPtr, Value* pVecMask, Value* pVecPassthru);
void SCATTER_PTR(Value* pVecDstPtr, Value* pVecSrc, Value* pVecMask);

virtual void SCATTERPS(Value*         pDst,
                       Value*         vSrc,
                       Value*         vOffsets,
                       Value*         vMask,
                       MEM_CLIENT     usage = MEM_CLIENT::MEM_CLIENT_INTERNAL);

void Shuffle8bpcGather4(const SWR_FORMAT_INFO& info,
                        Value*                 vGatherInput,
                        Value*                 vGatherOutput[],
                        bool                   bPackedOutput);
void Shuffle16bpcGather4(const SWR_FORMAT_INFO& info,
                         Value*                 vGatherInput[],
                         Value*                 vGatherOutput[],
                         bool                   bPackedOutput);

// Static stack allocations for scatter operations
Value* pScatterStackSrc{nullptr};
Value* pScatterStackOffsets{nullptr};
