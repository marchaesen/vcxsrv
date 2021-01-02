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
 * @file lower_x86.cpp
 *
 * @brief llvm pass to lower meta code to x86
 *
 * Notes:
 *
 ******************************************************************************/

#include "jit_pch.hpp"
#include "passes.h"
#include "JitManager.h"

#include "common/simdlib.hpp"

#include <unordered_map>

extern "C" void ScatterPS_256(uint8_t*, SIMD256::Integer, SIMD256::Float, uint8_t, uint32_t);

namespace llvm
{
    // forward declare the initializer
    void initializeLowerX86Pass(PassRegistry&);
} // namespace llvm

namespace SwrJit
{
    using namespace llvm;

    enum TargetArch
    {
        AVX    = 0,
        AVX2   = 1,
        AVX512 = 2
    };

    enum TargetWidth
    {
        W256       = 0,
        W512       = 1,
        NUM_WIDTHS = 2
    };

    struct LowerX86;

    typedef std::function<Instruction*(LowerX86*, TargetArch, TargetWidth, CallInst*)> EmuFunc;

    struct X86Intrinsic
    {
        IntrinsicID intrin[NUM_WIDTHS];
        EmuFunc       emuFunc;
    };

    // Map of intrinsics that haven't been moved to the new mechanism yet. If used, these get the
    // previous behavior of mapping directly to avx/avx2 intrinsics.
    using intrinsicMap_t = std::map<std::string, IntrinsicID>;
    static intrinsicMap_t& getIntrinsicMap() {
        static std::map<std::string, IntrinsicID> intrinsicMap = {
            {"meta.intrinsic.BEXTR_32", Intrinsic::x86_bmi_bextr_32},
            {"meta.intrinsic.VPSHUFB", Intrinsic::x86_avx2_pshuf_b},
            {"meta.intrinsic.VCVTPS2PH", Intrinsic::x86_vcvtps2ph_256},
            {"meta.intrinsic.VPTESTC", Intrinsic::x86_avx_ptestc_256},
            {"meta.intrinsic.VPTESTZ", Intrinsic::x86_avx_ptestz_256},
            {"meta.intrinsic.VPHADDD", Intrinsic::x86_avx2_phadd_d},
            {"meta.intrinsic.PDEP32", Intrinsic::x86_bmi_pdep_32},
            {"meta.intrinsic.RDTSC", Intrinsic::x86_rdtsc}
        };
        return intrinsicMap;
    }

    // Forward decls
    Instruction* NO_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst);
    Instruction*
    VPERM_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst);
    Instruction*
    VGATHER_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst);
    Instruction*
    VSCATTER_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst);
    Instruction*
    VROUND_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst);
    Instruction*
    VHSUB_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst);
    Instruction*
    VCONVERT_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst);

    Instruction* DOUBLE_EMU(LowerX86*     pThis,
                            TargetArch    arch,
                            TargetWidth   width,
                            CallInst*     pCallInst,
                            Intrinsic::ID intrin);

    static Intrinsic::ID DOUBLE = (Intrinsic::ID)-1;

    using intrinsicMapAdvanced_t = std::vector<std::map<std::string, X86Intrinsic>>;

    static intrinsicMapAdvanced_t&  getIntrinsicMapAdvanced()
    {
        // clang-format off
        static intrinsicMapAdvanced_t intrinsicMapAdvanced = {
            //                               256 wide                               512 wide
            {
                // AVX
                {"meta.intrinsic.VRCPPS",    {{Intrinsic::x86_avx_rcp_ps_256,       DOUBLE},                    NO_EMU}},
                {"meta.intrinsic.VPERMPS",   {{Intrinsic::not_intrinsic,            Intrinsic::not_intrinsic},  VPERM_EMU}},
                {"meta.intrinsic.VPERMD",    {{Intrinsic::not_intrinsic,            Intrinsic::not_intrinsic},  VPERM_EMU}},
                {"meta.intrinsic.VGATHERPD", {{Intrinsic::not_intrinsic,            Intrinsic::not_intrinsic},  VGATHER_EMU}},
                {"meta.intrinsic.VGATHERPS", {{Intrinsic::not_intrinsic,            Intrinsic::not_intrinsic},  VGATHER_EMU}},
                {"meta.intrinsic.VGATHERDD", {{Intrinsic::not_intrinsic,            Intrinsic::not_intrinsic},  VGATHER_EMU}},
                {"meta.intrinsic.VSCATTERPS", {{Intrinsic::not_intrinsic,           Intrinsic::not_intrinsic}, VSCATTER_EMU}},
                {"meta.intrinsic.VCVTPD2PS", {{Intrinsic::x86_avx_cvt_pd2_ps_256,   Intrinsic::not_intrinsic},  NO_EMU}},
                {"meta.intrinsic.VROUND",    {{Intrinsic::x86_avx_round_ps_256,     DOUBLE},                    NO_EMU}},
                {"meta.intrinsic.VHSUBPS",   {{Intrinsic::x86_avx_hsub_ps_256,      DOUBLE},                    NO_EMU}},
            },
            {
                // AVX2
                {"meta.intrinsic.VRCPPS",       {{Intrinsic::x86_avx_rcp_ps_256,    DOUBLE},                    NO_EMU}},
                {"meta.intrinsic.VPERMPS",      {{Intrinsic::x86_avx2_permps,       Intrinsic::not_intrinsic},  VPERM_EMU}},
                {"meta.intrinsic.VPERMD",       {{Intrinsic::x86_avx2_permd,        Intrinsic::not_intrinsic},  VPERM_EMU}},
                {"meta.intrinsic.VGATHERPD",    {{Intrinsic::not_intrinsic,         Intrinsic::not_intrinsic},  VGATHER_EMU}},
                {"meta.intrinsic.VGATHERPS",    {{Intrinsic::not_intrinsic,         Intrinsic::not_intrinsic},  VGATHER_EMU}},
                {"meta.intrinsic.VGATHERDD",    {{Intrinsic::not_intrinsic,         Intrinsic::not_intrinsic},  VGATHER_EMU}},
                {"meta.intrinsic.VSCATTERPS", {{Intrinsic::not_intrinsic,           Intrinsic::not_intrinsic}, VSCATTER_EMU}},
                {"meta.intrinsic.VCVTPD2PS",    {{Intrinsic::x86_avx_cvt_pd2_ps_256, DOUBLE},                   NO_EMU}},
                {"meta.intrinsic.VROUND",       {{Intrinsic::x86_avx_round_ps_256,  DOUBLE},                    NO_EMU}},
                {"meta.intrinsic.VHSUBPS",      {{Intrinsic::x86_avx_hsub_ps_256,   DOUBLE},                    NO_EMU}},
            },
            {
                // AVX512
                {"meta.intrinsic.VRCPPS", {{Intrinsic::x86_avx512_rcp14_ps_256,     Intrinsic::x86_avx512_rcp14_ps_512}, NO_EMU}},
    #if LLVM_VERSION_MAJOR < 7
                {"meta.intrinsic.VPERMPS", {{Intrinsic::x86_avx512_mask_permvar_sf_256, Intrinsic::x86_avx512_mask_permvar_sf_512}, NO_EMU}},
                {"meta.intrinsic.VPERMD", {{Intrinsic::x86_avx512_mask_permvar_si_256, Intrinsic::x86_avx512_mask_permvar_si_512}, NO_EMU}},
    #else
                {"meta.intrinsic.VPERMPS", {{Intrinsic::not_intrinsic,              Intrinsic::not_intrinsic}, VPERM_EMU}},
                {"meta.intrinsic.VPERMD", {{Intrinsic::not_intrinsic,               Intrinsic::not_intrinsic}, VPERM_EMU}},
    #endif
                {"meta.intrinsic.VGATHERPD", {{Intrinsic::not_intrinsic,            Intrinsic::not_intrinsic}, VGATHER_EMU}},
                {"meta.intrinsic.VGATHERPS", {{Intrinsic::not_intrinsic,            Intrinsic::not_intrinsic}, VGATHER_EMU}},
                {"meta.intrinsic.VGATHERDD", {{Intrinsic::not_intrinsic,            Intrinsic::not_intrinsic}, VGATHER_EMU}},
                {"meta.intrinsic.VSCATTERPS", {{Intrinsic::not_intrinsic,           Intrinsic::not_intrinsic}, VSCATTER_EMU}},
    #if LLVM_VERSION_MAJOR < 7
                {"meta.intrinsic.VCVTPD2PS", {{Intrinsic::x86_avx512_mask_cvtpd2ps_256, Intrinsic::x86_avx512_mask_cvtpd2ps_512}, NO_EMU}},
    #else
                {"meta.intrinsic.VCVTPD2PS", {{Intrinsic::not_intrinsic,            Intrinsic::not_intrinsic}, VCONVERT_EMU}},
    #endif
                {"meta.intrinsic.VROUND", {{Intrinsic::not_intrinsic,               Intrinsic::not_intrinsic}, VROUND_EMU}},
                {"meta.intrinsic.VHSUBPS", {{Intrinsic::not_intrinsic,              Intrinsic::not_intrinsic}, VHSUB_EMU}}
            }};
        // clang-format on
        return intrinsicMapAdvanced;
    }

    static uint32_t getBitWidth(VectorType *pVTy)
    {
#if LLVM_VERSION_MAJOR >= 12
        return cast<FixedVectorType>(pVTy)->getNumElements() * pVTy->getElementType()->getPrimitiveSizeInBits();
#elif LLVM_VERSION_MAJOR >= 11
        return pVTy->getNumElements() * pVTy->getElementType()->getPrimitiveSizeInBits();
#else
        return pVTy->getBitWidth();
#endif
    }

    struct LowerX86 : public FunctionPass
    {
        LowerX86(Builder* b = nullptr) : FunctionPass(ID), B(b)
        {
            initializeLowerX86Pass(*PassRegistry::getPassRegistry());

            // Determine target arch
            if (JM()->mArch.AVX512F())
            {
                mTarget = AVX512;
            }
            else if (JM()->mArch.AVX2())
            {
                mTarget = AVX2;
            }
            else if (JM()->mArch.AVX())
            {
                mTarget = AVX;
            }
            else
            {
                SWR_ASSERT(false, "Unsupported AVX architecture.");
                mTarget = AVX;
            }

            // Setup scatter function for 256 wide
            uint32_t curWidth = B->mVWidth;
            B->SetTargetWidth(8);
            std::vector<Type*> args = {
                B->mInt8PtrTy,   // pBase
                B->mSimdInt32Ty, // vIndices
                B->mSimdFP32Ty,  // vSrc
                B->mInt8Ty,      // mask
                B->mInt32Ty      // scale
            };

            FunctionType* pfnScatterTy = FunctionType::get(B->mVoidTy, args, false);
            mPfnScatter256             = cast<Function>(
#if LLVM_VERSION_MAJOR >= 9
                B->JM()->mpCurrentModule->getOrInsertFunction("ScatterPS_256", pfnScatterTy).getCallee());
#else
                B->JM()->mpCurrentModule->getOrInsertFunction("ScatterPS_256", pfnScatterTy));
#endif
            if (sys::DynamicLibrary::SearchForAddressOfSymbol("ScatterPS_256") == nullptr)
            {
                sys::DynamicLibrary::AddSymbol("ScatterPS_256", (void*)&ScatterPS_256);
            }

            B->SetTargetWidth(curWidth);
        }

        // Try to decipher the vector type of the instruction. This does not work properly
        // across all intrinsics, and will have to be rethought. Probably need something
        // similar to llvm's getDeclaration() utility to map a set of inputs to a specific typed
        // intrinsic.
        void GetRequestedWidthAndType(CallInst*       pCallInst,
                                      const StringRef intrinName,
                                      TargetWidth*    pWidth,
                                      Type**          pTy)
        {
            assert(pCallInst);
            Type* pVecTy = pCallInst->getType();

            // Check for intrinsic specific types
            // VCVTPD2PS type comes from src, not dst
            if (intrinName.equals("meta.intrinsic.VCVTPD2PS"))
            {
                Value* pOp = pCallInst->getOperand(0);
                assert(pOp);
                pVecTy = pOp->getType();
            }

            if (!pVecTy->isVectorTy())
            {
                for (auto& op : pCallInst->arg_operands())
                {
                    if (op.get()->getType()->isVectorTy())
                    {
                        pVecTy = op.get()->getType();
                        break;
                    }
                }
            }
            SWR_ASSERT(pVecTy->isVectorTy(), "Couldn't determine vector size");

            uint32_t width = getBitWidth(cast<VectorType>(pVecTy));
            switch (width)
            {
            case 256:
                *pWidth = W256;
                break;
            case 512:
                *pWidth = W512;
                break;
            default:
                SWR_ASSERT(false, "Unhandled vector width %d", width);
                *pWidth = W256;
            }

            *pTy = pVecTy->getScalarType();
        }

        Value* GetZeroVec(TargetWidth width, Type* pTy)
        {
            uint32_t numElem = 0;
            switch (width)
            {
            case W256:
                numElem = 8;
                break;
            case W512:
                numElem = 16;
                break;
            default:
                SWR_ASSERT(false, "Unhandled vector width type %d\n", width);
            }

            return ConstantVector::getNullValue(getVectorType(pTy, numElem));
        }

        Value* GetMask(TargetWidth width)
        {
            Value* mask;
            switch (width)
            {
            case W256:
                mask = B->C((uint8_t)-1);
                break;
            case W512:
                mask = B->C((uint16_t)-1);
                break;
            default:
                SWR_ASSERT(false, "Unhandled vector width type %d\n", width);
            }
            return mask;
        }

        // Convert <N x i1> mask to <N x i32> x86 mask
        Value* VectorMask(Value* vi1Mask)
        {
#if LLVM_VERSION_MAJOR >= 12
            uint32_t numElem = cast<FixedVectorType>(vi1Mask->getType())->getNumElements();
#elif LLVM_VERSION_MAJOR >= 11
            uint32_t numElem = cast<VectorType>(vi1Mask->getType())->getNumElements();
#else
            uint32_t numElem = vi1Mask->getType()->getVectorNumElements();
#endif
            return B->S_EXT(vi1Mask, getVectorType(B->mInt32Ty, numElem));
        }

        Instruction* ProcessIntrinsicAdvanced(CallInst* pCallInst)
        {
            Function*   pFunc = pCallInst->getCalledFunction();
            assert(pFunc);

            auto&       intrinsic = getIntrinsicMapAdvanced()[mTarget][pFunc->getName().str()];
            TargetWidth vecWidth;
            Type*       pElemTy;
            GetRequestedWidthAndType(pCallInst, pFunc->getName(), &vecWidth, &pElemTy);

            // Check if there is a native intrinsic for this instruction
            IntrinsicID id = intrinsic.intrin[vecWidth];
            if (id == DOUBLE)
            {
                // Double pump the next smaller SIMD intrinsic
                SWR_ASSERT(vecWidth != 0, "Cannot double pump smallest SIMD width.");
                Intrinsic::ID id2 = intrinsic.intrin[vecWidth - 1];
                SWR_ASSERT(id2 != Intrinsic::not_intrinsic,
                           "Cannot find intrinsic to double pump.");
                return DOUBLE_EMU(this, mTarget, vecWidth, pCallInst, id2);
            }
            else if (id != Intrinsic::not_intrinsic)
            {
                Function* pIntrin = Intrinsic::getDeclaration(B->JM()->mpCurrentModule, id);
                SmallVector<Value*, 8> args;
                for (auto& arg : pCallInst->arg_operands())
                {
                    args.push_back(arg.get());
                }

                // If AVX512, all instructions add a src operand and mask. We'll pass in 0 src and
                // full mask for now Assuming the intrinsics are consistent and place the src
                // operand and mask last in the argument list.
                if (mTarget == AVX512)
                {
                    if (pFunc->getName().equals("meta.intrinsic.VCVTPD2PS"))
                    {
                        args.push_back(GetZeroVec(W256, pCallInst->getType()->getScalarType()));
                        args.push_back(GetMask(W256));
                        // for AVX512 VCVTPD2PS, we also have to add rounding mode
                        args.push_back(B->C(_MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
                    }
                    else
                    {
                        args.push_back(GetZeroVec(vecWidth, pElemTy));
                        args.push_back(GetMask(vecWidth));
                    }
                }

                return B->CALLA(pIntrin, args);
            }
            else
            {
                // No native intrinsic, call emulation function
                return intrinsic.emuFunc(this, mTarget, vecWidth, pCallInst);
            }

            SWR_ASSERT(false);
            return nullptr;
        }

        Instruction* ProcessIntrinsic(CallInst* pCallInst)
        {
            Function* pFunc = pCallInst->getCalledFunction();
            assert(pFunc);

            // Forward to the advanced support if found
            if (getIntrinsicMapAdvanced()[mTarget].find(pFunc->getName().str()) != getIntrinsicMapAdvanced()[mTarget].end())
            {
                return ProcessIntrinsicAdvanced(pCallInst);
            }

            SWR_ASSERT(getIntrinsicMap().find(pFunc->getName().str()) != getIntrinsicMap().end(),
                       "Unimplemented intrinsic %s.",
                       pFunc->getName().str().c_str());

            Intrinsic::ID x86Intrinsic = getIntrinsicMap()[pFunc->getName().str()];
            Function*     pX86IntrinFunc =
                Intrinsic::getDeclaration(B->JM()->mpCurrentModule, x86Intrinsic);

            SmallVector<Value*, 8> args;
            for (auto& arg : pCallInst->arg_operands())
            {
                args.push_back(arg.get());
            }
            return B->CALLA(pX86IntrinFunc, args);
        }

        //////////////////////////////////////////////////////////////////////////
        /// @brief LLVM function pass run method.
        /// @param f- The function we're working on with this pass.
        virtual bool runOnFunction(Function& F)
        {
            std::vector<Instruction*> toRemove;
            std::vector<BasicBlock*>  bbs;

            // Make temp copy of the basic blocks and instructions, as the intrinsic
            // replacement code might invalidate the iterators
            for (auto& b : F.getBasicBlockList())
            {
                bbs.push_back(&b);
            }

            for (auto* BB : bbs)
            {
                std::vector<Instruction*> insts;
                for (auto& i : BB->getInstList())
                {
                    insts.push_back(&i);
                }

                for (auto* I : insts)
                {
                    if (CallInst* pCallInst = dyn_cast<CallInst>(I))
                    {
                        Function* pFunc = pCallInst->getCalledFunction();
                        if (pFunc)
                        {
                            if (pFunc->getName().startswith("meta.intrinsic"))
                            {
                                B->IRB()->SetInsertPoint(I);
                                Instruction* pReplace = ProcessIntrinsic(pCallInst);
                                toRemove.push_back(pCallInst);
                                if (pReplace)
                                {
                                    pCallInst->replaceAllUsesWith(pReplace);
                                }
                            }
                        }
                    }
                }
            }

            for (auto* pInst : toRemove)
            {
                pInst->eraseFromParent();
            }

            JitManager::DumpToFile(&F, "lowerx86");

            return true;
        }

        virtual void getAnalysisUsage(AnalysisUsage& AU) const {}

        JitManager* JM() { return B->JM(); }
        Builder*    B;
        TargetArch  mTarget;
        Function*   mPfnScatter256;

        static char ID; ///< Needed by LLVM to generate ID for FunctionPass.
    };

    char LowerX86::ID = 0; // LLVM uses address of ID as the actual ID.

    FunctionPass* createLowerX86Pass(Builder* b) { return new LowerX86(b); }

    Instruction* NO_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst)
    {
        SWR_ASSERT(false, "Unimplemented intrinsic emulation.");
        return nullptr;
    }

    Instruction* VPERM_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst)
    {
        // Only need vperm emulation for AVX
        SWR_ASSERT(arch == AVX);

        Builder* B         = pThis->B;
        auto     v32A      = pCallInst->getArgOperand(0);
        auto     vi32Index = pCallInst->getArgOperand(1);

        Value* v32Result;
        if (isa<Constant>(vi32Index))
        {
            // Can use llvm shuffle vector directly with constant shuffle indices
            v32Result = B->VSHUFFLE(v32A, v32A, vi32Index);
        }
        else
        {
            v32Result = UndefValue::get(v32A->getType());
#if LLVM_VERSION_MAJOR >= 12
            uint32_t numElem = cast<FixedVectorType>(v32A->getType())->getNumElements();
#elif LLVM_VERSION_MAJOR >= 11
            uint32_t numElem = cast<VectorType>(v32A->getType())->getNumElements();
#else
            uint32_t numElem = v32A->getType()->getVectorNumElements();
#endif
            for (uint32_t l = 0; l < numElem; ++l)
            {
                auto i32Index = B->VEXTRACT(vi32Index, B->C(l));
                auto val      = B->VEXTRACT(v32A, i32Index);
                v32Result     = B->VINSERT(v32Result, val, B->C(l));
            }
        }
        return cast<Instruction>(v32Result);
    }

    Instruction*
    VGATHER_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst)
    {
        Builder* B           = pThis->B;
        auto     vSrc        = pCallInst->getArgOperand(0);
        auto     pBase       = pCallInst->getArgOperand(1);
        auto     vi32Indices = pCallInst->getArgOperand(2);
        auto     vi1Mask     = pCallInst->getArgOperand(3);
        auto     i8Scale     = pCallInst->getArgOperand(4);

        pBase              = B->POINTER_CAST(pBase, PointerType::get(B->mInt8Ty, 0));
#if LLVM_VERSION_MAJOR >= 11
#if LLVM_VERSION_MAJOR >= 12
        FixedVectorType* pVectorType = cast<FixedVectorType>(vSrc->getType());
#else
        VectorType* pVectorType = cast<VectorType>(vSrc->getType());
#endif
        uint32_t    numElem     = pVectorType->getNumElements();
        auto        srcTy       = pVectorType->getElementType();
#else
        uint32_t numElem   = vSrc->getType()->getVectorNumElements();
        auto     srcTy     = vSrc->getType()->getVectorElementType();
#endif
        auto     i32Scale  = B->Z_EXT(i8Scale, B->mInt32Ty);

        Value*   v32Gather = nullptr;
        if (arch == AVX)
        {
            // Full emulation for AVX
            // Store source on stack to provide a valid address to load from inactive lanes
            auto pStack = B->STACKSAVE();
            auto pTmp   = B->ALLOCA(vSrc->getType());
            B->STORE(vSrc, pTmp);

            v32Gather        = UndefValue::get(vSrc->getType());
#if LLVM_VERSION_MAJOR <= 10
            auto vi32Scale   = ConstantVector::getSplat(numElem, cast<ConstantInt>(i32Scale));
#elif LLVM_VERSION_MAJOR == 11
            auto vi32Scale   = ConstantVector::getSplat(ElementCount(numElem, false), cast<ConstantInt>(i32Scale));
#else
            auto vi32Scale   = ConstantVector::getSplat(ElementCount::get(numElem, false), cast<ConstantInt>(i32Scale));
#endif
            auto vi32Offsets = B->MUL(vi32Indices, vi32Scale);

            for (uint32_t i = 0; i < numElem; ++i)
            {
                auto i32Offset          = B->VEXTRACT(vi32Offsets, B->C(i));
                auto pLoadAddress       = B->GEP(pBase, i32Offset);
                pLoadAddress            = B->BITCAST(pLoadAddress, PointerType::get(srcTy, 0));
                auto pMaskedLoadAddress = B->GEP(pTmp, {0, i});
                auto i1Mask             = B->VEXTRACT(vi1Mask, B->C(i));
                auto pValidAddress      = B->SELECT(i1Mask, pLoadAddress, pMaskedLoadAddress);
                auto val                = B->LOAD(pValidAddress);
                v32Gather               = B->VINSERT(v32Gather, val, B->C(i));
            }

            B->STACKRESTORE(pStack);
        }
        else if (arch == AVX2 || (arch == AVX512 && width == W256))
        {
            Function* pX86IntrinFunc = nullptr;
            if (srcTy == B->mFP32Ty)
            {
                pX86IntrinFunc = Intrinsic::getDeclaration(B->JM()->mpCurrentModule,
                                                           Intrinsic::x86_avx2_gather_d_ps_256);
            }
            else if (srcTy == B->mInt32Ty)
            {
                pX86IntrinFunc = Intrinsic::getDeclaration(B->JM()->mpCurrentModule,
                                                           Intrinsic::x86_avx2_gather_d_d_256);
            }
            else if (srcTy == B->mDoubleTy)
            {
                pX86IntrinFunc = Intrinsic::getDeclaration(B->JM()->mpCurrentModule,
                                                           Intrinsic::x86_avx2_gather_d_q_256);
            }
            else
            {
                SWR_ASSERT(false, "Unsupported vector element type for gather.");
            }

            if (width == W256)
            {
                auto v32Mask = B->BITCAST(pThis->VectorMask(vi1Mask), vSrc->getType());
                v32Gather = B->CALL(pX86IntrinFunc, {vSrc, pBase, vi32Indices, v32Mask, i8Scale});
            }
            else if (width == W512)
            {
                // Double pump 4-wide for 64bit elements
#if LLVM_VERSION_MAJOR >= 12
                if (cast<FixedVectorType>(vSrc->getType())->getElementType() == B->mDoubleTy)
#elif LLVM_VERSION_MAJOR >= 11
                if (cast<VectorType>(vSrc->getType())->getElementType() == B->mDoubleTy)
#else
                if (vSrc->getType()->getVectorElementType() == B->mDoubleTy)
#endif
                {
                    auto v64Mask = pThis->VectorMask(vi1Mask);
#if LLVM_VERSION_MAJOR >= 12
                    uint32_t numElem = cast<FixedVectorType>(v64Mask->getType())->getNumElements();
#elif LLVM_VERSION_MAJOR >= 11
                    uint32_t numElem = cast<VectorType>(v64Mask->getType())->getNumElements();
#else
                    uint32_t numElem = v64Mask->getType()->getVectorNumElements();
#endif
                    v64Mask = B->S_EXT(v64Mask, getVectorType(B->mInt64Ty, numElem));
                    v64Mask = B->BITCAST(v64Mask, vSrc->getType());

                    Value* src0 = B->VSHUFFLE(vSrc, vSrc, B->C({0, 1, 2, 3}));
                    Value* src1 = B->VSHUFFLE(vSrc, vSrc, B->C({4, 5, 6, 7}));

                    Value* indices0 = B->VSHUFFLE(vi32Indices, vi32Indices, B->C({0, 1, 2, 3}));
                    Value* indices1 = B->VSHUFFLE(vi32Indices, vi32Indices, B->C({4, 5, 6, 7}));

                    Value* mask0 = B->VSHUFFLE(v64Mask, v64Mask, B->C({0, 1, 2, 3}));
                    Value* mask1 = B->VSHUFFLE(v64Mask, v64Mask, B->C({4, 5, 6, 7}));

#if LLVM_VERSION_MAJOR >= 12
                    uint32_t numElemSrc0  = cast<FixedVectorType>(src0->getType())->getNumElements();
                    uint32_t numElemMask0 = cast<FixedVectorType>(mask0->getType())->getNumElements();
                    uint32_t numElemSrc1  = cast<FixedVectorType>(src1->getType())->getNumElements();
                    uint32_t numElemMask1 = cast<FixedVectorType>(mask1->getType())->getNumElements();
#elif LLVM_VERSION_MAJOR >= 11
                    uint32_t numElemSrc0  = cast<VectorType>(src0->getType())->getNumElements();
                    uint32_t numElemMask0 = cast<VectorType>(mask0->getType())->getNumElements();
                    uint32_t numElemSrc1  = cast<VectorType>(src1->getType())->getNumElements();
                    uint32_t numElemMask1 = cast<VectorType>(mask1->getType())->getNumElements();
#else
                    uint32_t numElemSrc0  = src0->getType()->getVectorNumElements();
                    uint32_t numElemMask0 = mask0->getType()->getVectorNumElements();
                    uint32_t numElemSrc1  = src1->getType()->getVectorNumElements();
                    uint32_t numElemMask1 = mask1->getType()->getVectorNumElements();
#endif
                    src0 = B->BITCAST(src0, getVectorType(B->mInt64Ty, numElemSrc0));
                    mask0 = B->BITCAST(mask0, getVectorType(B->mInt64Ty, numElemMask0));
                    Value* gather0 =
                        B->CALL(pX86IntrinFunc, {src0, pBase, indices0, mask0, i8Scale});
                    src1 = B->BITCAST(src1, getVectorType(B->mInt64Ty, numElemSrc1));
                    mask1 = B->BITCAST(mask1, getVectorType(B->mInt64Ty, numElemMask1));
                    Value* gather1 =
                        B->CALL(pX86IntrinFunc, {src1, pBase, indices1, mask1, i8Scale});
                    v32Gather = B->VSHUFFLE(gather0, gather1, B->C({0, 1, 2, 3, 4, 5, 6, 7}));
                    v32Gather = B->BITCAST(v32Gather, vSrc->getType());
                }
                else
                {
                    // Double pump 8-wide for 32bit elements
                    auto v32Mask = pThis->VectorMask(vi1Mask);
                    v32Mask      = B->BITCAST(v32Mask, vSrc->getType());
                    Value* src0  = B->EXTRACT_16(vSrc, 0);
                    Value* src1  = B->EXTRACT_16(vSrc, 1);

                    Value* indices0 = B->EXTRACT_16(vi32Indices, 0);
                    Value* indices1 = B->EXTRACT_16(vi32Indices, 1);

                    Value* mask0 = B->EXTRACT_16(v32Mask, 0);
                    Value* mask1 = B->EXTRACT_16(v32Mask, 1);

                    Value* gather0 =
                        B->CALL(pX86IntrinFunc, {src0, pBase, indices0, mask0, i8Scale});
                    Value* gather1 =
                        B->CALL(pX86IntrinFunc, {src1, pBase, indices1, mask1, i8Scale});

                    v32Gather = B->JOIN_16(gather0, gather1);
                }
            }
        }
        else if (arch == AVX512)
        {
            Value*    iMask = nullptr;
            Function* pX86IntrinFunc = nullptr;
            if (srcTy == B->mFP32Ty)
            {
                pX86IntrinFunc = Intrinsic::getDeclaration(B->JM()->mpCurrentModule,
                                                           Intrinsic::x86_avx512_gather_dps_512);
                iMask          = B->BITCAST(vi1Mask, B->mInt16Ty);
            }
            else if (srcTy == B->mInt32Ty)
            {
                pX86IntrinFunc = Intrinsic::getDeclaration(B->JM()->mpCurrentModule,
                                                           Intrinsic::x86_avx512_gather_dpi_512);
                iMask          = B->BITCAST(vi1Mask, B->mInt16Ty);
            }
            else if (srcTy == B->mDoubleTy)
            {
                pX86IntrinFunc = Intrinsic::getDeclaration(B->JM()->mpCurrentModule,
                                                           Intrinsic::x86_avx512_gather_dpd_512);
                iMask          = B->BITCAST(vi1Mask, B->mInt8Ty);
            }
            else
            {
                SWR_ASSERT(false, "Unsupported vector element type for gather.");
            }

            auto i32Scale = B->Z_EXT(i8Scale, B->mInt32Ty);
            v32Gather     = B->CALL(pX86IntrinFunc, {vSrc, pBase, vi32Indices, iMask, i32Scale});
        }

        return cast<Instruction>(v32Gather);
    }
    Instruction*
    VSCATTER_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst)
    {
        Builder* B           = pThis->B;
        auto     pBase       = pCallInst->getArgOperand(0);
        auto     vi1Mask     = pCallInst->getArgOperand(1);
        auto     vi32Indices = pCallInst->getArgOperand(2);
        auto     v32Src      = pCallInst->getArgOperand(3);
        auto     i32Scale    = pCallInst->getArgOperand(4);

        if (arch != AVX512)
        {
            // Call into C function to do the scatter. This has significantly better compile perf
            // compared to jitting scatter loops for every scatter
            if (width == W256)
            {
                auto mask = B->BITCAST(vi1Mask, B->mInt8Ty);
                B->CALL(pThis->mPfnScatter256, {pBase, vi32Indices, v32Src, mask, i32Scale});
            }
            else
            {
                // Need to break up 512 wide scatter to two 256 wide
                auto maskLo = B->VSHUFFLE(vi1Mask, vi1Mask, B->C({0, 1, 2, 3, 4, 5, 6, 7}));
                auto indicesLo =
                    B->VSHUFFLE(vi32Indices, vi32Indices, B->C({0, 1, 2, 3, 4, 5, 6, 7}));
                auto srcLo = B->VSHUFFLE(v32Src, v32Src, B->C({0, 1, 2, 3, 4, 5, 6, 7}));

                auto mask = B->BITCAST(maskLo, B->mInt8Ty);
                B->CALL(pThis->mPfnScatter256, {pBase, indicesLo, srcLo, mask, i32Scale});

                auto maskHi = B->VSHUFFLE(vi1Mask, vi1Mask, B->C({8, 9, 10, 11, 12, 13, 14, 15}));
                auto indicesHi =
                    B->VSHUFFLE(vi32Indices, vi32Indices, B->C({8, 9, 10, 11, 12, 13, 14, 15}));
                auto srcHi = B->VSHUFFLE(v32Src, v32Src, B->C({8, 9, 10, 11, 12, 13, 14, 15}));

                mask = B->BITCAST(maskHi, B->mInt8Ty);
                B->CALL(pThis->mPfnScatter256, {pBase, indicesHi, srcHi, mask, i32Scale});
            }
            return nullptr;
        }

        Value*    iMask;
        Function* pX86IntrinFunc;
        if (width == W256)
        {
            // No direct intrinsic supported in llvm to scatter 8 elem with 32bit indices, but we
            // can use the scatter of 8 elements with 64bit indices
            pX86IntrinFunc = Intrinsic::getDeclaration(B->JM()->mpCurrentModule,
                                                       Intrinsic::x86_avx512_scatter_qps_512);

            auto vi32IndicesExt = B->Z_EXT(vi32Indices, B->mSimdInt64Ty);
            iMask               = B->BITCAST(vi1Mask, B->mInt8Ty);
            B->CALL(pX86IntrinFunc, {pBase, iMask, vi32IndicesExt, v32Src, i32Scale});
        }
        else if (width == W512)
        {
            pX86IntrinFunc = Intrinsic::getDeclaration(B->JM()->mpCurrentModule,
                                                       Intrinsic::x86_avx512_scatter_dps_512);
            iMask          = B->BITCAST(vi1Mask, B->mInt16Ty);
            B->CALL(pX86IntrinFunc, {pBase, iMask, vi32Indices, v32Src, i32Scale});
        }
        return nullptr;
    }

    // No support for vroundps in avx512 (it is available in kncni), so emulate with avx
    // instructions
    Instruction*
    VROUND_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst)
    {
        SWR_ASSERT(arch == AVX512);

        auto B       = pThis->B;
        auto vf32Src = pCallInst->getOperand(0);
        assert(vf32Src);
        auto i8Round = pCallInst->getOperand(1);
        assert(i8Round);
        auto pfnFunc =
            Intrinsic::getDeclaration(B->JM()->mpCurrentModule, Intrinsic::x86_avx_round_ps_256);

        if (width == W256)
        {
            return cast<Instruction>(B->CALL2(pfnFunc, vf32Src, i8Round));
        }
        else if (width == W512)
        {
            auto v8f32SrcLo = B->EXTRACT_16(vf32Src, 0);
            auto v8f32SrcHi = B->EXTRACT_16(vf32Src, 1);

            auto v8f32ResLo = B->CALL2(pfnFunc, v8f32SrcLo, i8Round);
            auto v8f32ResHi = B->CALL2(pfnFunc, v8f32SrcHi, i8Round);

            return cast<Instruction>(B->JOIN_16(v8f32ResLo, v8f32ResHi));
        }
        else
        {
            SWR_ASSERT(false, "Unimplemented vector width.");
        }

        return nullptr;
    }

    Instruction*
    VCONVERT_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst)
    {
        SWR_ASSERT(arch == AVX512);

        auto B       = pThis->B;
        auto vf32Src = pCallInst->getOperand(0);

        if (width == W256)
        {
            auto vf32SrcRound = Intrinsic::getDeclaration(B->JM()->mpCurrentModule,
                                                          Intrinsic::x86_avx_round_ps_256);
            return cast<Instruction>(B->FP_TRUNC(vf32SrcRound, B->mFP32Ty));
        }
        else if (width == W512)
        {
            // 512 can use intrinsic
            auto pfnFunc = Intrinsic::getDeclaration(B->JM()->mpCurrentModule,
                                                     Intrinsic::x86_avx512_mask_cvtpd2ps_512);
            return cast<Instruction>(B->CALL(pfnFunc, vf32Src));
        }
        else
        {
            SWR_ASSERT(false, "Unimplemented vector width.");
        }

        return nullptr;
    }

    // No support for hsub in AVX512
    Instruction* VHSUB_EMU(LowerX86* pThis, TargetArch arch, TargetWidth width, CallInst* pCallInst)
    {
        SWR_ASSERT(arch == AVX512);

        auto B    = pThis->B;
        auto src0 = pCallInst->getOperand(0);
        auto src1 = pCallInst->getOperand(1);

        // 256b hsub can just use avx intrinsic
        if (width == W256)
        {
            auto pX86IntrinFunc =
                Intrinsic::getDeclaration(B->JM()->mpCurrentModule, Intrinsic::x86_avx_hsub_ps_256);
            return cast<Instruction>(B->CALL2(pX86IntrinFunc, src0, src1));
        }
        else if (width == W512)
        {
            // 512b hsub can be accomplished with shuf/sub combo
            auto minuend    = B->VSHUFFLE(src0, src1, B->C({0, 2, 8, 10, 4, 6, 12, 14}));
            auto subtrahend = B->VSHUFFLE(src0, src1, B->C({1, 3, 9, 11, 5, 7, 13, 15}));
            return cast<Instruction>(B->SUB(minuend, subtrahend));
        }
        else
        {
            SWR_ASSERT(false, "Unimplemented vector width.");
            return nullptr;
        }
    }

    // Double pump input using Intrin template arg. This blindly extracts lower and upper 256 from
    // each vector argument and calls the 256 wide intrinsic, then merges the results to 512 wide
    Instruction* DOUBLE_EMU(LowerX86*     pThis,
                            TargetArch    arch,
                            TargetWidth   width,
                            CallInst*     pCallInst,
                            Intrinsic::ID intrin)
    {
        auto B = pThis->B;
        SWR_ASSERT(width == W512);
        Value*    result[2];
        Function* pX86IntrinFunc = Intrinsic::getDeclaration(B->JM()->mpCurrentModule, intrin);
        for (uint32_t i = 0; i < 2; ++i)
        {
            SmallVector<Value*, 8> args;
            for (auto& arg : pCallInst->arg_operands())
            {
                auto argType = arg.get()->getType();
                if (argType->isVectorTy())
                {
#if LLVM_VERSION_MAJOR >= 12
                    uint32_t vecWidth  = cast<FixedVectorType>(argType)->getNumElements();
                    auto     elemTy    = cast<FixedVectorType>(argType)->getElementType();
#elif LLVM_VERSION_MAJOR >= 11
                    uint32_t vecWidth  = cast<VectorType>(argType)->getNumElements();
                    auto     elemTy    = cast<VectorType>(argType)->getElementType();
#else
                    uint32_t vecWidth  = argType->getVectorNumElements();
                    auto     elemTy    = argType->getVectorElementType();
#endif
                    Value*   lanes     = B->CInc<int>(i * vecWidth / 2, vecWidth / 2);
                    Value*   argToPush = B->VSHUFFLE(arg.get(), B->VUNDEF(elemTy, vecWidth), lanes);
                    args.push_back(argToPush);
                }
                else
                {
                    args.push_back(arg.get());
                }
            }
            result[i] = B->CALLA(pX86IntrinFunc, args);
        }
        uint32_t vecWidth;
        if (result[0]->getType()->isVectorTy())
        {
            assert(result[1]->getType()->isVectorTy());
#if LLVM_VERSION_MAJOR >= 12
            vecWidth = cast<FixedVectorType>(result[0]->getType())->getNumElements() +
                       cast<FixedVectorType>(result[1]->getType())->getNumElements();
#elif LLVM_VERSION_MAJOR >= 11
            vecWidth = cast<VectorType>(result[0]->getType())->getNumElements() +
                       cast<VectorType>(result[1]->getType())->getNumElements();
#else
            vecWidth = result[0]->getType()->getVectorNumElements() +
                       result[1]->getType()->getVectorNumElements();
#endif
        }
        else
        {
            vecWidth = 2;
        }
        Value* lanes = B->CInc<int>(0, vecWidth);
        return cast<Instruction>(B->VSHUFFLE(result[0], result[1], lanes));
    }

} // namespace SwrJit

using namespace SwrJit;

INITIALIZE_PASS_BEGIN(LowerX86, "LowerX86", "LowerX86", false, false)
INITIALIZE_PASS_END(LowerX86, "LowerX86", "LowerX86", false, false)
