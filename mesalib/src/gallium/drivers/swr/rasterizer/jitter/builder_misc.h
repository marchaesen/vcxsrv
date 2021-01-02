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

Constant* C(bool i);
Constant* C(char i);
Constant* C(uint8_t i);
Constant* C(int i);
Constant* C(int64_t i);
Constant* C(uint64_t i);
Constant* C(uint16_t i);
Constant* C(uint32_t i);
Constant* C(float i);

template <typename Ty>
Constant* C(const std::initializer_list<Ty>& constList)
{
    std::vector<Constant*> vConsts;
    for (auto i : constList)
    {
        vConsts.push_back(C((Ty)i));
    }
    return ConstantVector::get(vConsts);
}

template <typename Ty>
Constant* C(const std::vector<Ty>& constList)
{
    std::vector<Constant*> vConsts;
    for (auto i : constList)
    {
        vConsts.push_back(C((Ty)i));
    }
    return ConstantVector::get(vConsts);
}

template <typename Ty>
Constant* CA(LLVMContext& ctx, ArrayRef<Ty> constList)
{
    return ConstantDataArray::get(ctx, constList);
}

template <typename Ty>
Constant* CInc(uint32_t base, uint32_t count)
{
    std::vector<Constant*> vConsts;

    for (uint32_t i = 0; i < count; i++)
    {
        vConsts.push_back(C((Ty)base));
        base++;
    }
    return ConstantVector::get(vConsts);
}

Constant* PRED(bool pred);

Value* VIMMED1(uint64_t i);
Value* VIMMED1_16(uint64_t i);

Value* VIMMED1(int i);
Value* VIMMED1_16(int i);

Value* VIMMED1(uint32_t i);
Value* VIMMED1_16(uint32_t i);

Value* VIMMED1(float i);
Value* VIMMED1_16(float i);

Value* VIMMED1(bool i);
Value* VIMMED1_16(bool i);

Value* VUNDEF(Type* t);

Value* VUNDEF_F();
Value* VUNDEF_F_16();

Value* VUNDEF_I();
Value* VUNDEF_I_16();

Value* VUNDEF(Type* ty, uint32_t size);

Value* VUNDEF_IPTR();

Value* VBROADCAST(Value* src, const llvm::Twine& name = "");
Value* VBROADCAST_16(Value* src);

Value* VRCP(Value* va, const llvm::Twine& name = "");
Value* VPLANEPS(Value* vA, Value* vB, Value* vC, Value*& vX, Value*& vY);

uint32_t IMMED(Value* i);
int32_t  S_IMMED(Value* i);

CallInst* CALL(Value* Callee, const std::initializer_list<Value*>& args, const llvm::Twine& name = "");
CallInst* CALL(Value* Callee)
{
#if LLVM_VERSION_MAJOR >= 11
    // Not a great idea - we loose type info (Function) calling CALL
    // and then we recast it here. Good for now, but needs to be
    // more clean - optimally just always CALL a Function
    return CALLA(FunctionCallee(cast<Function>(Callee)));
#else
    return CALLA(Callee);
#endif
}
CallInst* CALL(Value* Callee, Value* arg);
CallInst* CALL2(Value* Callee, Value* arg1, Value* arg2);
CallInst* CALL3(Value* Callee, Value* arg1, Value* arg2, Value* arg3);

Value* MASK(Value* vmask);
Value* MASK_16(Value* vmask);

Value* VMASK(Value* mask);
Value* VMASK_16(Value* mask);

Value* VMOVMSK(Value* mask);

//////////////////////////////////////////////////////////////////////////
/// @brief Float / Fixed-point conversions
//////////////////////////////////////////////////////////////////////////
// Signed
Value* VCVT_F32_FIXED_SI(Value*             vFloat,
                         uint32_t           numIntBits,
                         uint32_t           numFracBits,
                         const llvm::Twine& name = "");
Value* VCVT_FIXED_SI_F32(Value*             vFixed,
                         uint32_t           numIntBits,
                         uint32_t           numFracBits,
                         const llvm::Twine& name = "");
// Unsigned
Value* VCVT_F32_FIXED_UI(Value*             vFloat,
                         uint32_t           numIntBits,
                         uint32_t           numFracBits,
                         const llvm::Twine& name = "");
Value* VCVT_FIXED_UI_F32(Value*             vFixed,
                         uint32_t           numIntBits,
                         uint32_t           numFracBits,
                         const llvm::Twine& name = "");

//////////////////////////////////////////////////////////////////////////
/// @brief functions that build IR to call x86 intrinsics directly, or
/// emulate them with other instructions if not available on the host
//////////////////////////////////////////////////////////////////////////

Value* EXTRACT_16(Value* x, uint32_t imm);
Value* JOIN_16(Value* a, Value* b);

Value* PSHUFB(Value* a, Value* b);
Value* PMOVSXBD(Value* a);
Value* PMOVSXWD(Value* a);
Value* CVTPH2PS(Value* a, const llvm::Twine& name = "");
Value* CVTPS2PH(Value* a, Value* rounding);
Value* PMAXSD(Value* a, Value* b);
Value* PMINSD(Value* a, Value* b);
Value* PMAXUD(Value* a, Value* b);
Value* PMINUD(Value* a, Value* b);
Value* VABSPS(Value* a);
Value* FMADDPS(Value* a, Value* b, Value* c);

Value* ICLAMP(Value* src, Value* low, Value* high, const llvm::Twine& name = "");
Value* FCLAMP(Value* src, Value* low, Value* high);
Value* FCLAMP(Value* src, float low, float high);

CallInst* PRINT(const std::string& printStr);
CallInst* PRINT(const std::string& printStr, const std::initializer_list<Value*>& printArgs);

Value* VPOPCNT(Value* a);

Value* INT3()
{
    return DEBUGTRAP();
}


Value* VEXTRACTI128(Value* a, Constant* imm8);
Value* VINSERTI128(Value* a, Value* b, Constant* imm8);

// rdtsc buckets macros
void RDTSC_START(Value* pBucketMgr, Value* pId);
void RDTSC_STOP(Value* pBucketMgr, Value* pId);

Value* CreateEntryAlloca(Function* pFunc, Type* pType);
Value* CreateEntryAlloca(Function* pFunc, Type* pType, Value* pArraySize);

uint32_t GetTypeSize(Type* pType);
