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
 * @file builder_misc.cpp
 *
 * @brief Implementation for miscellaneous builder functions
 *
 * Notes:
 *
 ******************************************************************************/
#include "jit_pch.hpp"
#include "builder.h"
#include "common/rdtsc_buckets.h"

#include <cstdarg>

extern "C" void CallPrint(const char* fmt, ...);

namespace SwrJit
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Convert an IEEE 754 32-bit single precision float to an
    ///        16 bit float with 5 exponent bits and a variable
    ///        number of mantissa bits.
    /// @param val - 32-bit float
    /// @todo Maybe move this outside of this file into a header?
    static uint16_t ConvertFloat32ToFloat16(float val)
    {
        uint32_t sign, exp, mant;
        uint32_t roundBits;

        // Extract the sign, exponent, and mantissa
        uint32_t uf = *(uint32_t*)&val;
        sign        = (uf & 0x80000000) >> 31;
        exp         = (uf & 0x7F800000) >> 23;
        mant        = uf & 0x007FFFFF;

        // Check for out of range
        if (std::isnan(val))
        {
            exp  = 0x1F;
            mant = 0x200;
            sign = 1; // set the sign bit for NANs
        }
        else if (std::isinf(val))
        {
            exp  = 0x1f;
            mant = 0x0;
        }
        else if (exp > (0x70 + 0x1E)) // Too big to represent -> max representable value
        {
            exp  = 0x1E;
            mant = 0x3FF;
        }
        else if ((exp <= 0x70) && (exp >= 0x66)) // It's a denorm
        {
            mant |= 0x00800000;
            for (; exp <= 0x70; mant >>= 1, exp++)
                ;
            exp  = 0;
            mant = mant >> 13;
        }
        else if (exp < 0x66) // Too small to represent -> Zero
        {
            exp  = 0;
            mant = 0;
        }
        else
        {
            // Saves bits that will be shifted off for rounding
            roundBits = mant & 0x1FFFu;
            // convert exponent and mantissa to 16 bit format
            exp  = exp - 0x70;
            mant = mant >> 13;

            // Essentially RTZ, but round up if off by only 1 lsb
            if (roundBits == 0x1FFFu)
            {
                mant++;
                // check for overflow
                if ((mant & 0xC00u) != 0)
                    exp++;
                // make sure only the needed bits are used
                mant &= 0x3FF;
            }
        }

        uint32_t tmpVal = (sign << 15) | (exp << 10) | mant;
        return (uint16_t)tmpVal;
    }

    Constant* Builder::C(bool i) { return ConstantInt::get(IRB()->getInt1Ty(), (i ? 1 : 0)); }

    Constant* Builder::C(char i) { return ConstantInt::get(IRB()->getInt8Ty(), i); }

    Constant* Builder::C(uint8_t i) { return ConstantInt::get(IRB()->getInt8Ty(), i); }

    Constant* Builder::C(int i) { return ConstantInt::get(IRB()->getInt32Ty(), i); }

    Constant* Builder::C(int64_t i) { return ConstantInt::get(IRB()->getInt64Ty(), i); }

    Constant* Builder::C(uint16_t i) { return ConstantInt::get(mInt16Ty, i); }

    Constant* Builder::C(uint32_t i) { return ConstantInt::get(IRB()->getInt32Ty(), i); }

    Constant* Builder::C(uint64_t i) { return ConstantInt::get(IRB()->getInt64Ty(), i); }

    Constant* Builder::C(float i) { return ConstantFP::get(IRB()->getFloatTy(), i); }

    Constant* Builder::PRED(bool pred)
    {
        return ConstantInt::get(IRB()->getInt1Ty(), (pred ? 1 : 0));
    }

    Value* Builder::VIMMED1(uint64_t i)
    {
#if LLVM_VERSION_MAJOR <= 10
        return ConstantVector::getSplat(mVWidth, cast<ConstantInt>(C(i)));
#elif LLVM_VERSION_MAJOR == 11
        return ConstantVector::getSplat(ElementCount(mVWidth, false), cast<ConstantInt>(C(i)));
#else
        return ConstantVector::getSplat(ElementCount::get(mVWidth, false), cast<ConstantInt>(C(i)));
#endif
    }

    Value* Builder::VIMMED1_16(uint64_t i)
    {
#if LLVM_VERSION_MAJOR <= 10
        return ConstantVector::getSplat(mVWidth16, cast<ConstantInt>(C(i)));
#elif LLVM_VERSION_MAJOR == 11
        return ConstantVector::getSplat(ElementCount(mVWidth16, false), cast<ConstantInt>(C(i)));
#else
        return ConstantVector::getSplat(ElementCount::get(mVWidth16, false), cast<ConstantInt>(C(i)));
#endif
    }

    Value* Builder::VIMMED1(int i)
    {
#if LLVM_VERSION_MAJOR <= 10
        return ConstantVector::getSplat(mVWidth, cast<ConstantInt>(C(i)));
#elif LLVM_VERSION_MAJOR == 11
        return ConstantVector::getSplat(ElementCount(mVWidth, false), cast<ConstantInt>(C(i)));
#else
        return ConstantVector::getSplat(ElementCount::get(mVWidth, false), cast<ConstantInt>(C(i)));
#endif
    }

    Value* Builder::VIMMED1_16(int i)
    {
#if LLVM_VERSION_MAJOR <= 10
        return ConstantVector::getSplat(mVWidth16, cast<ConstantInt>(C(i)));
#elif LLVM_VERSION_MAJOR == 11
        return ConstantVector::getSplat(ElementCount(mVWidth16, false), cast<ConstantInt>(C(i)));
#else
        return ConstantVector::getSplat(ElementCount::get(mVWidth16, false), cast<ConstantInt>(C(i)));
#endif
    }

    Value* Builder::VIMMED1(uint32_t i)
    {
#if LLVM_VERSION_MAJOR <= 10
        return ConstantVector::getSplat(mVWidth, cast<ConstantInt>(C(i)));
#elif LLVM_VERSION_MAJOR == 11
        return ConstantVector::getSplat(ElementCount(mVWidth, false), cast<ConstantInt>(C(i)));
#else
        return ConstantVector::getSplat(ElementCount::get(mVWidth, false), cast<ConstantInt>(C(i)));
#endif
    }

    Value* Builder::VIMMED1_16(uint32_t i)
    {
#if LLVM_VERSION_MAJOR <= 10
        return ConstantVector::getSplat(mVWidth16, cast<ConstantInt>(C(i)));
#elif LLVM_VERSION_MAJOR == 11
        return ConstantVector::getSplat(ElementCount(mVWidth16, false), cast<ConstantInt>(C(i)));
#else
        return ConstantVector::getSplat(ElementCount::get(mVWidth16, false), cast<ConstantInt>(C(i)));
#endif
    }

    Value* Builder::VIMMED1(float i)
    {
#if LLVM_VERSION_MAJOR <= 10
        return ConstantVector::getSplat(mVWidth, cast<ConstantFP>(C(i)));
#elif LLVM_VERSION_MAJOR == 11
        return ConstantVector::getSplat(ElementCount(mVWidth, false), cast<ConstantFP>(C(i)));
#else
        return ConstantVector::getSplat(ElementCount::get(mVWidth, false), cast<ConstantFP>(C(i)));
#endif
    }

    Value* Builder::VIMMED1_16(float i)
    {
#if LLVM_VERSION_MAJOR <= 10
        return ConstantVector::getSplat(mVWidth16, cast<ConstantFP>(C(i)));
#elif LLVM_VERSION_MAJOR == 11
        return ConstantVector::getSplat(ElementCount(mVWidth16, false), cast<ConstantFP>(C(i)));
#else
        return ConstantVector::getSplat(ElementCount::get(mVWidth16, false), cast<ConstantFP>(C(i)));
#endif
    }

    Value* Builder::VIMMED1(bool i)
    {
#if LLVM_VERSION_MAJOR <= 10
        return ConstantVector::getSplat(mVWidth, cast<ConstantInt>(C(i)));
#elif LLVM_VERSION_MAJOR == 11
        return ConstantVector::getSplat(ElementCount(mVWidth, false), cast<ConstantInt>(C(i)));
#else
        return ConstantVector::getSplat(ElementCount::get(mVWidth, false), cast<ConstantInt>(C(i)));
#endif
    }

    Value* Builder::VIMMED1_16(bool i)
    {
#if LLVM_VERSION_MAJOR <= 10
        return ConstantVector::getSplat(mVWidth16, cast<ConstantInt>(C(i)));
#elif LLVM_VERSION_MAJOR == 11
        return ConstantVector::getSplat(ElementCount(mVWidth16, false), cast<ConstantInt>(C(i)));
#else
        return ConstantVector::getSplat(ElementCount::get(mVWidth16, false), cast<ConstantInt>(C(i)));
#endif
    }

    Value* Builder::VUNDEF_IPTR() { return UndefValue::get(getVectorType(mInt32PtrTy, mVWidth)); }

    Value* Builder::VUNDEF(Type* t) { return UndefValue::get(getVectorType(t, mVWidth)); }

    Value* Builder::VUNDEF_I() { return UndefValue::get(getVectorType(mInt32Ty, mVWidth)); }

    Value* Builder::VUNDEF_I_16() { return UndefValue::get(getVectorType(mInt32Ty, mVWidth16)); }

    Value* Builder::VUNDEF_F() { return UndefValue::get(getVectorType(mFP32Ty, mVWidth)); }

    Value* Builder::VUNDEF_F_16() { return UndefValue::get(getVectorType(mFP32Ty, mVWidth16)); }

    Value* Builder::VUNDEF(Type* ty, uint32_t size)
    {
        return UndefValue::get(getVectorType(ty, size));
    }

    Value* Builder::VBROADCAST(Value* src, const llvm::Twine& name)
    {
        // check if src is already a vector
        if (src->getType()->isVectorTy())
        {
            return src;
        }

        return VECTOR_SPLAT(mVWidth, src, name);
    }

    Value* Builder::VBROADCAST_16(Value* src)
    {
        // check if src is already a vector
        if (src->getType()->isVectorTy())
        {
            return src;
        }

        return VECTOR_SPLAT(mVWidth16, src);
    }

    uint32_t Builder::IMMED(Value* v)
    {
        SWR_ASSERT(isa<ConstantInt>(v));
        ConstantInt* pValConst = cast<ConstantInt>(v);
        return pValConst->getZExtValue();
    }

    int32_t Builder::S_IMMED(Value* v)
    {
        SWR_ASSERT(isa<ConstantInt>(v));
        ConstantInt* pValConst = cast<ConstantInt>(v);
        return pValConst->getSExtValue();
    }

    CallInst* Builder::CALL(Value*                               Callee,
                            const std::initializer_list<Value*>& argsList,
                            const llvm::Twine&                   name)
    {
        std::vector<Value*> args;
        for (auto arg : argsList)
            args.push_back(arg);
#if LLVM_VERSION_MAJOR >= 11
        // see comment to CALLA(Callee) function in the header
        return CALLA(FunctionCallee(cast<Function>(Callee)), args, name);
#else
        return CALLA(Callee, args, name);
#endif
    }

    CallInst* Builder::CALL(Value* Callee, Value* arg)
    {
        std::vector<Value*> args;
        args.push_back(arg);
#if LLVM_VERSION_MAJOR >= 11
        // see comment to CALLA(Callee) function in the header
        return CALLA(FunctionCallee(cast<Function>(Callee)), args);
#else
        return CALLA(Callee, args);
#endif
    }

    CallInst* Builder::CALL2(Value* Callee, Value* arg1, Value* arg2)
    {
        std::vector<Value*> args;
        args.push_back(arg1);
        args.push_back(arg2);
#if LLVM_VERSION_MAJOR >= 11
        // see comment to CALLA(Callee) function in the header
        return CALLA(FunctionCallee(cast<Function>(Callee)), args);
#else
        return CALLA(Callee, args);
#endif
    }

    CallInst* Builder::CALL3(Value* Callee, Value* arg1, Value* arg2, Value* arg3)
    {
        std::vector<Value*> args;
        args.push_back(arg1);
        args.push_back(arg2);
        args.push_back(arg3);
#if LLVM_VERSION_MAJOR >= 11
        // see comment to CALLA(Callee) function in the header
        return CALLA(FunctionCallee(cast<Function>(Callee)), args);
#else
        return CALLA(Callee, args);
#endif
    }

    Value* Builder::VRCP(Value* va, const llvm::Twine& name)
    {
        return FDIV(VIMMED1(1.0f), va, name); // 1 / a
    }

    Value* Builder::VPLANEPS(Value* vA, Value* vB, Value* vC, Value*& vX, Value*& vY)
    {
        Value* vOut = FMADDPS(vA, vX, vC);
        vOut        = FMADDPS(vB, vY, vOut);
        return vOut;
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief insert a JIT call to CallPrint
    /// - outputs formatted string to both stdout and VS output window
    /// - DEBUG builds only
    /// Usage example:
    ///   PRINT("index %d = 0x%p\n",{C(lane), pIndex});
    ///   where C(lane) creates a constant value to print, and pIndex is the Value*
    ///   result from a GEP, printing out the pointer to memory
    /// @param printStr - constant string to print, which includes format specifiers
    /// @param printArgs - initializer list of Value*'s to print to std out
    CallInst* Builder::PRINT(const std::string&                   printStr,
                             const std::initializer_list<Value*>& printArgs)
    {
        // push the arguments to CallPrint into a vector
        std::vector<Value*> printCallArgs;
        // save room for the format string.  we still need to modify it for vectors
        printCallArgs.resize(1);

        // search through the format string for special processing
        size_t      pos = 0;
        std::string tempStr(printStr);
        pos    = tempStr.find('%', pos);
        auto v = printArgs.begin();

        while ((pos != std::string::npos) && (v != printArgs.end()))
        {
            Value* pArg  = *v;
            Type*  pType = pArg->getType();

            if (pType->isVectorTy())
            {
                Type* pContainedType = pType->getContainedType(0);
#if LLVM_VERSION_MAJOR >= 12
                FixedVectorType* pVectorType = cast<FixedVectorType>(pType);
#elif LLVM_VERSION_MAJOR >= 11
                VectorType* pVectorType = cast<VectorType>(pType);
#endif
                if (toupper(tempStr[pos + 1]) == 'X')
                {
                    tempStr[pos]     = '0';
                    tempStr[pos + 1] = 'x';
                    tempStr.insert(pos + 2, "%08X ");
                    pos += 7;

                    printCallArgs.push_back(VEXTRACT(pArg, C(0)));

                    std::string vectorFormatStr;
#if LLVM_VERSION_MAJOR >= 11
                    for (uint32_t i = 1; i < pVectorType->getNumElements(); ++i)
#else
                    for (uint32_t i = 1; i < pType->getVectorNumElements(); ++i)
#endif
                    {
                        vectorFormatStr += "0x%08X ";
                        printCallArgs.push_back(VEXTRACT(pArg, C(i)));
                    }

                    tempStr.insert(pos, vectorFormatStr);
                    pos += vectorFormatStr.size();
                }
                else if ((tempStr[pos + 1] == 'f') && (pContainedType->isFloatTy()))
                {
                    uint32_t i = 0;
#if LLVM_VERSION_MAJOR >= 11
                    for (; i < pVectorType->getNumElements() - 1; i++)
#else
                    for (; i < pType->getVectorNumElements() - 1; i++)
#endif
                    {
                        tempStr.insert(pos, std::string("%f "));
                        pos += 3;
                        printCallArgs.push_back(
                            FP_EXT(VEXTRACT(pArg, C(i)), Type::getDoubleTy(JM()->mContext)));
                    }
                    printCallArgs.push_back(
                        FP_EXT(VEXTRACT(pArg, C(i)), Type::getDoubleTy(JM()->mContext)));
                }
                else if ((tempStr[pos + 1] == 'd') && (pContainedType->isIntegerTy()))
                {
                    uint32_t i = 0;
#if LLVM_VERSION_MAJOR >= 11
                    for (; i < pVectorType->getNumElements() - 1; i++)
#else
                    for (; i < pType->getVectorNumElements() - 1; i++)
#endif
                    {
                        tempStr.insert(pos, std::string("%d "));
                        pos += 3;
                        printCallArgs.push_back(
                            S_EXT(VEXTRACT(pArg, C(i)), Type::getInt32Ty(JM()->mContext)));
                    }
                    printCallArgs.push_back(
                        S_EXT(VEXTRACT(pArg, C(i)), Type::getInt32Ty(JM()->mContext)));
                }
                else if ((tempStr[pos + 1] == 'u') && (pContainedType->isIntegerTy()))
                {
                    uint32_t i = 0;
#if LLVM_VERSION_MAJOR >= 11
                    for (; i < pVectorType->getNumElements() - 1; i++)
#else
                    for (; i < pType->getVectorNumElements() - 1; i++)
#endif
                    {
                        tempStr.insert(pos, std::string("%d "));
                        pos += 3;
                        printCallArgs.push_back(
                            Z_EXT(VEXTRACT(pArg, C(i)), Type::getInt32Ty(JM()->mContext)));
                    }
                    printCallArgs.push_back(
                        Z_EXT(VEXTRACT(pArg, C(i)), Type::getInt32Ty(JM()->mContext)));
                }
            }
            else
            {
                if (toupper(tempStr[pos + 1]) == 'X')
                {
                    tempStr[pos] = '0';
                    tempStr.insert(pos + 1, "x%08");
                    printCallArgs.push_back(pArg);
                    pos += 3;
                }
                // for %f we need to cast float Values to doubles so that they print out correctly
                else if ((tempStr[pos + 1] == 'f') && (pType->isFloatTy()))
                {
                    printCallArgs.push_back(FP_EXT(pArg, Type::getDoubleTy(JM()->mContext)));
                    pos++;
                }
                else
                {
                    printCallArgs.push_back(pArg);
                }
            }

            // advance to the next argument
            v++;
            pos = tempStr.find('%', ++pos);
        }

        // create global variable constant string
        Constant*       constString = ConstantDataArray::getString(JM()->mContext, tempStr, true);
        GlobalVariable* gvPtr       = new GlobalVariable(
            constString->getType(), true, GlobalValue::InternalLinkage, constString, "printStr");
        JM()->mpCurrentModule->getGlobalList().push_back(gvPtr);

        // get a pointer to the first character in the constant string array
        std::vector<Constant*> geplist{C(0), C(0)};
        Constant* strGEP = ConstantExpr::getGetElementPtr(nullptr, gvPtr, geplist, false);

        // insert the pointer to the format string in the argument vector
        printCallArgs[0] = strGEP;

        // get pointer to CallPrint function and insert decl into the module if needed
        std::vector<Type*> args;
        args.push_back(PointerType::get(mInt8Ty, 0));
        FunctionType* callPrintTy = FunctionType::get(Type::getVoidTy(JM()->mContext), args, true);
        Function*     callPrintFn =
#if LLVM_VERSION_MAJOR >= 9
            cast<Function>(JM()->mpCurrentModule->getOrInsertFunction("CallPrint", callPrintTy).getCallee());
#else
            cast<Function>(JM()->mpCurrentModule->getOrInsertFunction("CallPrint", callPrintTy));
#endif

        // if we haven't yet added the symbol to the symbol table
        if ((sys::DynamicLibrary::SearchForAddressOfSymbol("CallPrint")) == nullptr)
        {
            sys::DynamicLibrary::AddSymbol("CallPrint", (void*)&CallPrint);
        }

        // insert a call to CallPrint
        return CALLA(callPrintFn, printCallArgs);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Wrapper around PRINT with initializer list.
    CallInst* Builder::PRINT(const std::string& printStr) { return PRINT(printStr, {}); }

    Value* Builder::EXTRACT_16(Value* x, uint32_t imm)
    {
        if (imm == 0)
        {
            return VSHUFFLE(x, UndefValue::get(x->getType()), {0, 1, 2, 3, 4, 5, 6, 7});
        }
        else
        {
            return VSHUFFLE(x, UndefValue::get(x->getType()), {8, 9, 10, 11, 12, 13, 14, 15});
        }
    }

    Value* Builder::JOIN_16(Value* a, Value* b)
    {
        return VSHUFFLE(a, b, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief convert x86 <N x float> mask to llvm <N x i1> mask
    Value* Builder::MASK(Value* vmask)
    {
        Value* src = BITCAST(vmask, mSimdInt32Ty);
        return ICMP_SLT(src, VIMMED1(0));
    }

    Value* Builder::MASK_16(Value* vmask)
    {
        Value* src = BITCAST(vmask, mSimd16Int32Ty);
        return ICMP_SLT(src, VIMMED1_16(0));
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief convert llvm <N x i1> mask to x86 <N x i32> mask
    Value* Builder::VMASK(Value* mask) { return S_EXT(mask, mSimdInt32Ty); }

    Value* Builder::VMASK_16(Value* mask) { return S_EXT(mask, mSimd16Int32Ty); }

    /// @brief Convert <Nxi1> llvm mask to integer
    Value* Builder::VMOVMSK(Value* mask)
    {
#if LLVM_VERSION_MAJOR >= 11
#if LLVM_VERSION_MAJOR >= 12
        FixedVectorType* pVectorType = cast<FixedVectorType>(mask->getType());
#else
        VectorType* pVectorType = cast<VectorType>(mask->getType());
#endif
        SWR_ASSERT(pVectorType->getElementType() == mInt1Ty);
        uint32_t numLanes = pVectorType->getNumElements();
#else
        SWR_ASSERT(mask->getType()->getVectorElementType() == mInt1Ty);
        uint32_t numLanes = mask->getType()->getVectorNumElements();
#endif
        Value*   i32Result;
        if (numLanes == 8)
        {
            i32Result = BITCAST(mask, mInt8Ty);
        }
        else if (numLanes == 16)
        {
            i32Result = BITCAST(mask, mInt16Ty);
        }
        else
        {
            SWR_ASSERT("Unsupported vector width");
            i32Result = BITCAST(mask, mInt8Ty);
        }
        return Z_EXT(i32Result, mInt32Ty);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Generate a VPSHUFB operation in LLVM IR.  If not
    /// supported on the underlying platform, emulate it
    /// @param a - 256bit SIMD(32x8bit) of 8bit integer values
    /// @param b - 256bit SIMD(32x8bit) of 8bit integer mask values
    /// Byte masks in lower 128 lane of b selects 8 bit values from lower
    /// 128bits of a, and vice versa for the upper lanes.  If the mask
    /// value is negative, '0' is inserted.
    Value* Builder::PSHUFB(Value* a, Value* b)
    {
        Value* res;
        // use avx2 pshufb instruction if available
        if (JM()->mArch.AVX2())
        {
            res = VPSHUFB(a, b);
        }
        else
        {
            Constant* cB = dyn_cast<Constant>(b);
            assert(cB != nullptr);
            // number of 8 bit elements in b
#if LLVM_VERSION_MAJOR >= 12
            uint32_t numElms = cast<FixedVectorType>(cB->getType())->getNumElements();
#else
            uint32_t numElms = cast<VectorType>(cB->getType())->getNumElements();
#endif
            // output vector
            Value* vShuf = UndefValue::get(getVectorType(mInt8Ty, numElms));

            // insert an 8 bit value from the high and low lanes of a per loop iteration
            numElms /= 2;
            for (uint32_t i = 0; i < numElms; i++)
            {
                ConstantInt* cLow128b  = cast<ConstantInt>(cB->getAggregateElement(i));
                ConstantInt* cHigh128b = cast<ConstantInt>(cB->getAggregateElement(i + numElms));

                // extract values from constant mask
                char valLow128bLane  = (char)(cLow128b->getSExtValue());
                char valHigh128bLane = (char)(cHigh128b->getSExtValue());

                Value* insertValLow128b;
                Value* insertValHigh128b;

                // if the mask value is negative, insert a '0' in the respective output position
                // otherwise, lookup the value at mask position (bits 3..0 of the respective mask
                // byte) in a and insert in output vector
                insertValLow128b =
                    (valLow128bLane < 0) ? C((char)0) : VEXTRACT(a, C((valLow128bLane & 0xF)));
                insertValHigh128b = (valHigh128bLane < 0)
                                        ? C((char)0)
                                        : VEXTRACT(a, C((valHigh128bLane & 0xF) + numElms));

                vShuf = VINSERT(vShuf, insertValLow128b, i);
                vShuf = VINSERT(vShuf, insertValHigh128b, (i + numElms));
            }
            res = vShuf;
        }
        return res;
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Generate a VPSHUFB operation (sign extend 8 8bit values to 32
    /// bits)in LLVM IR.  If not supported on the underlying platform, emulate it
    /// @param a - 128bit SIMD lane(16x8bit) of 8bit integer values.  Only
    /// lower 8 values are used.
    Value* Builder::PMOVSXBD(Value* a)
    {
        // VPMOVSXBD output type
        Type* v8x32Ty = getVectorType(mInt32Ty, 8);
        // Extract 8 values from 128bit lane and sign extend
        return S_EXT(VSHUFFLE(a, a, C<int>({0, 1, 2, 3, 4, 5, 6, 7})), v8x32Ty);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Generate a VPSHUFB operation (sign extend 8 16bit values to 32
    /// bits)in LLVM IR.  If not supported on the underlying platform, emulate it
    /// @param a - 128bit SIMD lane(8x16bit) of 16bit integer values.
    Value* Builder::PMOVSXWD(Value* a)
    {
        // VPMOVSXWD output type
        Type* v8x32Ty = getVectorType(mInt32Ty, 8);
        // Extract 8 values from 128bit lane and sign extend
        return S_EXT(VSHUFFLE(a, a, C<int>({0, 1, 2, 3, 4, 5, 6, 7})), v8x32Ty);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Generate a VCVTPH2PS operation (float16->float32 conversion)
    /// in LLVM IR.  If not supported on the underlying platform, emulate it
    /// @param a - 128bit SIMD lane(8x16bit) of float16 in int16 format.
    Value* Builder::CVTPH2PS(Value* a, const llvm::Twine& name)
    {
        // Bitcast Nxint16 to Nxhalf
#if LLVM_VERSION_MAJOR >= 12
        uint32_t numElems = cast<FixedVectorType>(a->getType())->getNumElements();
#elif LLVM_VERSION_MAJOR >= 11
        uint32_t numElems = cast<VectorType>(a->getType())->getNumElements();
#else
        uint32_t numElems = a->getType()->getVectorNumElements();
#endif
        Value*   input    = BITCAST(a, getVectorType(mFP16Ty, numElems));

        return FP_EXT(input, getVectorType(mFP32Ty, numElems), name);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Generate a VCVTPS2PH operation (float32->float16 conversion)
    /// in LLVM IR.  If not supported on the underlying platform, emulate it
    /// @param a - 128bit SIMD lane(8x16bit) of float16 in int16 format.
    Value* Builder::CVTPS2PH(Value* a, Value* rounding)
    {
        if (JM()->mArch.F16C())
        {
            return VCVTPS2PH(a, rounding);
        }
        else
        {
            // call scalar C function for now
            FunctionType* pFuncTy   = FunctionType::get(mInt16Ty, mFP32Ty);
            Function*     pCvtPs2Ph = cast<Function>(
#if LLVM_VERSION_MAJOR >= 9
                JM()->mpCurrentModule->getOrInsertFunction("ConvertFloat32ToFloat16", pFuncTy).getCallee());
#else
                JM()->mpCurrentModule->getOrInsertFunction("ConvertFloat32ToFloat16", pFuncTy));
#endif

            if (sys::DynamicLibrary::SearchForAddressOfSymbol("ConvertFloat32ToFloat16") == nullptr)
            {
                sys::DynamicLibrary::AddSymbol("ConvertFloat32ToFloat16",
                                               (void*)&ConvertFloat32ToFloat16);
            }

            Value* pResult = UndefValue::get(mSimdInt16Ty);
            for (uint32_t i = 0; i < mVWidth; ++i)
            {
                Value* pSrc  = VEXTRACT(a, C(i));
                Value* pConv = CALL(pCvtPs2Ph, std::initializer_list<Value*>{pSrc});
                pResult      = VINSERT(pResult, pConv, C(i));
            }

            return pResult;
        }
    }

    Value* Builder::PMAXSD(Value* a, Value* b)
    {
        Value* cmp = ICMP_SGT(a, b);
        return SELECT(cmp, a, b);
    }

    Value* Builder::PMINSD(Value* a, Value* b)
    {
        Value* cmp = ICMP_SLT(a, b);
        return SELECT(cmp, a, b);
    }

    Value* Builder::PMAXUD(Value* a, Value* b)
    {
        Value* cmp = ICMP_UGT(a, b);
        return SELECT(cmp, a, b);
    }

    Value* Builder::PMINUD(Value* a, Value* b)
    {
        Value* cmp = ICMP_ULT(a, b);
        return SELECT(cmp, a, b);
    }

    // Helper function to create alloca in entry block of function
    Value* Builder::CreateEntryAlloca(Function* pFunc, Type* pType)
    {
        auto saveIP = IRB()->saveIP();
        IRB()->SetInsertPoint(&pFunc->getEntryBlock(), pFunc->getEntryBlock().begin());
        Value* pAlloca = ALLOCA(pType);
        if (saveIP.isSet())
            IRB()->restoreIP(saveIP);
        return pAlloca;
    }

    Value* Builder::CreateEntryAlloca(Function* pFunc, Type* pType, Value* pArraySize)
    {
        auto saveIP = IRB()->saveIP();
        IRB()->SetInsertPoint(&pFunc->getEntryBlock(), pFunc->getEntryBlock().begin());
        Value* pAlloca = ALLOCA(pType, pArraySize);
        if (saveIP.isSet())
            IRB()->restoreIP(saveIP);
        return pAlloca;
    }

    Value* Builder::VABSPS(Value* a)
    {
        Value* asInt  = BITCAST(a, mSimdInt32Ty);
        Value* result = BITCAST(AND(asInt, VIMMED1(0x7fffffff)), mSimdFP32Ty);
        return result;
    }

    Value* Builder::ICLAMP(Value* src, Value* low, Value* high, const llvm::Twine& name)
    {
        Value* lowCmp = ICMP_SLT(src, low);
        Value* ret    = SELECT(lowCmp, low, src);

        Value* highCmp = ICMP_SGT(ret, high);
        ret            = SELECT(highCmp, high, ret, name);

        return ret;
    }

    Value* Builder::FCLAMP(Value* src, Value* low, Value* high)
    {
        Value* lowCmp = FCMP_OLT(src, low);
        Value* ret    = SELECT(lowCmp, low, src);

        Value* highCmp = FCMP_OGT(ret, high);
        ret            = SELECT(highCmp, high, ret);

        return ret;
    }

    Value* Builder::FCLAMP(Value* src, float low, float high)
    {
        Value* result = VMAXPS(src, VIMMED1(low));
        result        = VMINPS(result, VIMMED1(high));

        return result;
    }

    Value* Builder::FMADDPS(Value* a, Value* b, Value* c)
    {
        Value* vOut;
        // This maps to LLVM fmuladd intrinsic
        vOut = VFMADDPS(a, b, c);
        return vOut;
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief pop count on vector mask (e.g. <8 x i1>)
    Value* Builder::VPOPCNT(Value* a) { return POPCNT(VMOVMSK(a)); }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Float / Fixed-point conversions
    //////////////////////////////////////////////////////////////////////////
    Value* Builder::VCVT_F32_FIXED_SI(Value*             vFloat,
                                      uint32_t           numIntBits,
                                      uint32_t           numFracBits,
                                      const llvm::Twine& name)
    {
        SWR_ASSERT((numIntBits + numFracBits) <= 32, "Can only handle 32-bit fixed-point values");
        Value* fixed = nullptr;

#if 0   // This doesn't work for negative numbers!!
        {
            fixed = FP_TO_SI(VROUND(FMUL(vFloat, VIMMED1(float(1 << numFracBits))),
                                    C(_MM_FROUND_TO_NEAREST_INT)),
                             mSimdInt32Ty);
        }
        else
#endif
        {
            // Do round to nearest int on fractional bits first
            // Not entirely perfect for negative numbers, but close enough
            vFloat = VROUND(FMUL(vFloat, VIMMED1(float(1 << numFracBits))),
                            C(_MM_FROUND_TO_NEAREST_INT));
            vFloat = FMUL(vFloat, VIMMED1(1.0f / float(1 << numFracBits)));

            // TODO: Handle INF, NAN, overflow / underflow, etc.

            Value* vSgn      = FCMP_OLT(vFloat, VIMMED1(0.0f));
            Value* vFloatInt = BITCAST(vFloat, mSimdInt32Ty);
            Value* vFixed    = AND(vFloatInt, VIMMED1((1 << 23) - 1));
            vFixed           = OR(vFixed, VIMMED1(1 << 23));
            vFixed           = SELECT(vSgn, NEG(vFixed), vFixed);

            Value* vExp = LSHR(SHL(vFloatInt, VIMMED1(1)), VIMMED1(24));
            vExp        = SUB(vExp, VIMMED1(127));

            Value* vExtraBits = SUB(VIMMED1(23 - numFracBits), vExp);

            fixed = ASHR(vFixed, vExtraBits, name);
        }

        return fixed;
    }

    Value* Builder::VCVT_FIXED_SI_F32(Value*             vFixed,
                                      uint32_t           numIntBits,
                                      uint32_t           numFracBits,
                                      const llvm::Twine& name)
    {
        SWR_ASSERT((numIntBits + numFracBits) <= 32, "Can only handle 32-bit fixed-point values");
        uint32_t extraBits = 32 - numIntBits - numFracBits;
        if (numIntBits && extraBits)
        {
            // Sign extend
            Value* shftAmt = VIMMED1(extraBits);
            vFixed         = ASHR(SHL(vFixed, shftAmt), shftAmt);
        }

        Value* fVal  = VIMMED1(0.0f);
        Value* fFrac = VIMMED1(0.0f);
        if (numIntBits)
        {
            fVal = SI_TO_FP(ASHR(vFixed, VIMMED1(numFracBits)), mSimdFP32Ty, name);
        }

        if (numFracBits)
        {
            fFrac = UI_TO_FP(AND(vFixed, VIMMED1((1 << numFracBits) - 1)), mSimdFP32Ty);
            fFrac = FDIV(fFrac, VIMMED1(float(1 << numFracBits)), name);
        }

        return FADD(fVal, fFrac, name);
    }

    Value* Builder::VCVT_F32_FIXED_UI(Value*             vFloat,
                                      uint32_t           numIntBits,
                                      uint32_t           numFracBits,
                                      const llvm::Twine& name)
    {
        SWR_ASSERT((numIntBits + numFracBits) <= 32, "Can only handle 32-bit fixed-point values");
        Value* fixed = nullptr;
#if 1   // KNOB_SIM_FAST_MATH?  Below works correctly from a precision
        // standpoint...
        {
            fixed = FP_TO_UI(VROUND(FMUL(vFloat, VIMMED1(float(1 << numFracBits))),
                                    C(_MM_FROUND_TO_NEAREST_INT)),
                             mSimdInt32Ty);
        }
#else
        {
            // Do round to nearest int on fractional bits first
            vFloat = VROUND(FMUL(vFloat, VIMMED1(float(1 << numFracBits))),
                            C(_MM_FROUND_TO_NEAREST_INT));
            vFloat = FMUL(vFloat, VIMMED1(1.0f / float(1 << numFracBits)));

            // TODO: Handle INF, NAN, overflow / underflow, etc.

            Value* vSgn      = FCMP_OLT(vFloat, VIMMED1(0.0f));
            Value* vFloatInt = BITCAST(vFloat, mSimdInt32Ty);
            Value* vFixed    = AND(vFloatInt, VIMMED1((1 << 23) - 1));
            vFixed           = OR(vFixed, VIMMED1(1 << 23));

            Value* vExp = LSHR(SHL(vFloatInt, VIMMED1(1)), VIMMED1(24));
            vExp        = SUB(vExp, VIMMED1(127));

            Value* vExtraBits = SUB(VIMMED1(23 - numFracBits), vExp);

            fixed = LSHR(vFixed, vExtraBits, name);
        }
#endif
        return fixed;
    }

    Value* Builder::VCVT_FIXED_UI_F32(Value*             vFixed,
                                      uint32_t           numIntBits,
                                      uint32_t           numFracBits,
                                      const llvm::Twine& name)
    {
        SWR_ASSERT((numIntBits + numFracBits) <= 32, "Can only handle 32-bit fixed-point values");
        uint32_t extraBits = 32 - numIntBits - numFracBits;
        if (numIntBits && extraBits)
        {
            // Sign extend
            Value* shftAmt = VIMMED1(extraBits);
            vFixed         = ASHR(SHL(vFixed, shftAmt), shftAmt);
        }

        Value* fVal  = VIMMED1(0.0f);
        Value* fFrac = VIMMED1(0.0f);
        if (numIntBits)
        {
            fVal = UI_TO_FP(LSHR(vFixed, VIMMED1(numFracBits)), mSimdFP32Ty, name);
        }

        if (numFracBits)
        {
            fFrac = UI_TO_FP(AND(vFixed, VIMMED1((1 << numFracBits) - 1)), mSimdFP32Ty);
            fFrac = FDIV(fFrac, VIMMED1(float(1 << numFracBits)), name);
        }

        return FADD(fVal, fFrac, name);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief C functions called by LLVM IR
    //////////////////////////////////////////////////////////////////////////

    Value* Builder::VEXTRACTI128(Value* a, Constant* imm8)
    {
        bool                      flag = !imm8->isZeroValue();
        SmallVector<Constant*, 8> idx;
        for (unsigned i = 0; i < mVWidth / 2; i++)
        {
            idx.push_back(C(flag ? i + mVWidth / 2 : i));
        }
        return VSHUFFLE(a, VUNDEF_I(), ConstantVector::get(idx));
    }

    Value* Builder::VINSERTI128(Value* a, Value* b, Constant* imm8)
    {
        bool                      flag = !imm8->isZeroValue();
        SmallVector<Constant*, 8> idx;
        for (unsigned i = 0; i < mVWidth; i++)
        {
            idx.push_back(C(i));
        }
        Value* inter = VSHUFFLE(b, VUNDEF_I(), ConstantVector::get(idx));

        SmallVector<Constant*, 8> idx2;
        for (unsigned i = 0; i < mVWidth / 2; i++)
        {
            idx2.push_back(C(flag ? i : i + mVWidth));
        }
        for (unsigned i = mVWidth / 2; i < mVWidth; i++)
        {
            idx2.push_back(C(flag ? i + mVWidth / 2 : i));
        }
        return VSHUFFLE(a, inter, ConstantVector::get(idx2));
    }

    // rdtsc buckets macros
    void Builder::RDTSC_START(Value* pBucketMgr, Value* pId)
    {
        // @todo due to an issue with thread local storage propagation in llvm, we can only safely
        // call into buckets framework when single threaded
        if (KNOB_SINGLE_THREADED)
        {
            std::vector<Type*> args{
                PointerType::get(mInt32Ty, 0), // pBucketMgr
                mInt32Ty                       // id
            };

            FunctionType* pFuncTy = FunctionType::get(Type::getVoidTy(JM()->mContext), args, false);
            Function*     pFunc   = cast<Function>(
#if LLVM_VERSION_MAJOR >= 9
                JM()->mpCurrentModule->getOrInsertFunction("BucketManager_StartBucket", pFuncTy).getCallee());
#else
                JM()->mpCurrentModule->getOrInsertFunction("BucketManager_StartBucket", pFuncTy));
#endif
            if (sys::DynamicLibrary::SearchForAddressOfSymbol("BucketManager_StartBucket") ==
                nullptr)
            {
                sys::DynamicLibrary::AddSymbol("BucketManager_StartBucket",
                                               (void*)&BucketManager_StartBucket);
            }

            CALL(pFunc, {pBucketMgr, pId});
        }
    }

    void Builder::RDTSC_STOP(Value* pBucketMgr, Value* pId)
    {
        // @todo due to an issue with thread local storage propagation in llvm, we can only safely
        // call into buckets framework when single threaded
        if (KNOB_SINGLE_THREADED)
        {
            std::vector<Type*> args{
                PointerType::get(mInt32Ty, 0), // pBucketMgr
                mInt32Ty                       // id
            };

            FunctionType* pFuncTy = FunctionType::get(Type::getVoidTy(JM()->mContext), args, false);
            Function*     pFunc   = cast<Function>(
#if LLVM_VERSION_MAJOR >= 9
                JM()->mpCurrentModule->getOrInsertFunction("BucketManager_StopBucket", pFuncTy).getCallee());
#else
                JM()->mpCurrentModule->getOrInsertFunction("BucketManager_StopBucket", pFuncTy));
#endif
            if (sys::DynamicLibrary::SearchForAddressOfSymbol("BucketManager_StopBucket") ==
                nullptr)
            {
                sys::DynamicLibrary::AddSymbol("BucketManager_StopBucket",
                                               (void*)&BucketManager_StopBucket);
            }

            CALL(pFunc, {pBucketMgr, pId});
        }
    }

    uint32_t Builder::GetTypeSize(Type* pType)
    {
        if (pType->isStructTy())
        {
            uint32_t numElems = pType->getStructNumElements();
            Type*    pElemTy  = pType->getStructElementType(0);
            return numElems * GetTypeSize(pElemTy);
        }

        if (pType->isArrayTy())
        {
            uint32_t numElems = pType->getArrayNumElements();
            Type*    pElemTy  = pType->getArrayElementType();
            return numElems * GetTypeSize(pElemTy);
        }

        if (pType->isIntegerTy())
        {
            uint32_t bitSize = pType->getIntegerBitWidth();
            return bitSize / 8;
        }

        if (pType->isFloatTy())
        {
            return 4;
        }

        if (pType->isHalfTy())
        {
            return 2;
        }

        if (pType->isDoubleTy())
        {
            return 8;
        }

        SWR_ASSERT(false, "Unimplemented type.");
        return 0;
    }
} // namespace SwrJit
