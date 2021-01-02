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
 * @file blend_jit.cpp
 *
 * @brief Implementation of the blend jitter
 *
 * Notes:
 *
 ******************************************************************************/
#include "jit_pch.hpp"
#include "builder.h"
#include "jit_api.h"
#include "blend_jit.h"
#include "gen_state_llvm.h"
#include "functionpasses/passes.h"

// components with bit-widths <= the QUANTIZE_THRESHOLD will be quantized
#define QUANTIZE_THRESHOLD 2

using namespace llvm;
using namespace SwrJit;

//////////////////////////////////////////////////////////////////////////
/// Interface to Jitting a blend shader
//////////////////////////////////////////////////////////////////////////
struct BlendJit : public Builder
{
    BlendJit(JitManager* pJitMgr) : Builder(pJitMgr){};

    template <bool Color, bool Alpha>
    void GenerateBlendFactor(SWR_BLEND_FACTOR factor,
                             Value*           constColor[4],
                             Value*           src[4],
                             Value*           src1[4],
                             Value*           dst[4],
                             Value*           result[4])
    {
        Value* out[4];

        switch (factor)
        {
        case BLENDFACTOR_ONE:
            out[0] = out[1] = out[2] = out[3] = VIMMED1(1.0f);
            break;
        case BLENDFACTOR_SRC_COLOR:
            out[0] = src[0];
            out[1] = src[1];
            out[2] = src[2];
            out[3] = src[3];
            break;
        case BLENDFACTOR_SRC_ALPHA:
            out[0] = out[1] = out[2] = out[3] = src[3];
            break;
        case BLENDFACTOR_DST_ALPHA:
            out[0] = out[1] = out[2] = out[3] = dst[3];
            break;
        case BLENDFACTOR_DST_COLOR:
            out[0] = dst[0];
            out[1] = dst[1];
            out[2] = dst[2];
            out[3] = dst[3];
            break;
        case BLENDFACTOR_SRC_ALPHA_SATURATE:
            out[0] = out[1] = out[2] = VMINPS(src[3], FSUB(VIMMED1(1.0f), dst[3]));
            out[3]                   = VIMMED1(1.0f);
            break;
        case BLENDFACTOR_CONST_COLOR:
            out[0] = constColor[0];
            out[1] = constColor[1];
            out[2] = constColor[2];
            out[3] = constColor[3];
            break;
        case BLENDFACTOR_CONST_ALPHA:
            out[0] = out[1] = out[2] = out[3] = constColor[3];
            break;
        case BLENDFACTOR_SRC1_COLOR:
            out[0] = src1[0];
            out[1] = src1[1];
            out[2] = src1[2];
            out[3] = src1[3];
            break;
        case BLENDFACTOR_SRC1_ALPHA:
            out[0] = out[1] = out[2] = out[3] = src1[3];
            break;
        case BLENDFACTOR_ZERO:
            out[0] = out[1] = out[2] = out[3] = VIMMED1(0.0f);
            break;
        case BLENDFACTOR_INV_SRC_COLOR:
            out[0] = FSUB(VIMMED1(1.0f), src[0]);
            out[1] = FSUB(VIMMED1(1.0f), src[1]);
            out[2] = FSUB(VIMMED1(1.0f), src[2]);
            out[3] = FSUB(VIMMED1(1.0f), src[3]);
            break;
        case BLENDFACTOR_INV_SRC_ALPHA:
            out[0] = out[1] = out[2] = out[3] = FSUB(VIMMED1(1.0f), src[3]);
            break;
        case BLENDFACTOR_INV_DST_ALPHA:
            out[0] = out[1] = out[2] = out[3] = FSUB(VIMMED1(1.0f), dst[3]);
            break;
        case BLENDFACTOR_INV_DST_COLOR:
            out[0] = FSUB(VIMMED1(1.0f), dst[0]);
            out[1] = FSUB(VIMMED1(1.0f), dst[1]);
            out[2] = FSUB(VIMMED1(1.0f), dst[2]);
            out[3] = FSUB(VIMMED1(1.0f), dst[3]);
            break;
        case BLENDFACTOR_INV_CONST_COLOR:
            out[0] = FSUB(VIMMED1(1.0f), constColor[0]);
            out[1] = FSUB(VIMMED1(1.0f), constColor[1]);
            out[2] = FSUB(VIMMED1(1.0f), constColor[2]);
            out[3] = FSUB(VIMMED1(1.0f), constColor[3]);
            break;
        case BLENDFACTOR_INV_CONST_ALPHA:
            out[0] = out[1] = out[2] = out[3] = FSUB(VIMMED1(1.0f), constColor[3]);
            break;
        case BLENDFACTOR_INV_SRC1_COLOR:
            out[0] = FSUB(VIMMED1(1.0f), src1[0]);
            out[1] = FSUB(VIMMED1(1.0f), src1[1]);
            out[2] = FSUB(VIMMED1(1.0f), src1[2]);
            out[3] = FSUB(VIMMED1(1.0f), src1[3]);
            break;
        case BLENDFACTOR_INV_SRC1_ALPHA:
            out[0] = out[1] = out[2] = out[3] = FSUB(VIMMED1(1.0f), src1[3]);
            break;
        default:
            SWR_INVALID("Unsupported blend factor: %d", factor);
            out[0] = out[1] = out[2] = out[3] = VIMMED1(0.0f);
            break;
        }

        if (Color)
        {
            result[0] = out[0];
            result[1] = out[1];
            result[2] = out[2];
        }

        if (Alpha)
        {
            result[3] = out[3];
        }
    }

    void Clamp(SWR_FORMAT format, Value* src[4])
    {
        const SWR_FORMAT_INFO& info = GetFormatInfo(format);
        SWR_TYPE               type = info.type[0];

        switch (type)
        {
        default:
            break;

        case SWR_TYPE_UNORM:
            src[0] = VMINPS(VMAXPS(src[0], VIMMED1(0.0f)), VIMMED1(1.0f));
            src[1] = VMINPS(VMAXPS(src[1], VIMMED1(0.0f)), VIMMED1(1.0f));
            src[2] = VMINPS(VMAXPS(src[2], VIMMED1(0.0f)), VIMMED1(1.0f));
            src[3] = VMINPS(VMAXPS(src[3], VIMMED1(0.0f)), VIMMED1(1.0f));
            break;

        case SWR_TYPE_SNORM:
            src[0] = VMINPS(VMAXPS(src[0], VIMMED1(-1.0f)), VIMMED1(1.0f));
            src[1] = VMINPS(VMAXPS(src[1], VIMMED1(-1.0f)), VIMMED1(1.0f));
            src[2] = VMINPS(VMAXPS(src[2], VIMMED1(-1.0f)), VIMMED1(1.0f));
            src[3] = VMINPS(VMAXPS(src[3], VIMMED1(-1.0f)), VIMMED1(1.0f));
            break;

        case SWR_TYPE_UNKNOWN:
            SWR_INVALID("Unsupported format type: %d", type);
        }
    }

    void ApplyDefaults(SWR_FORMAT format, Value* src[4])
    {
        const SWR_FORMAT_INFO& info = GetFormatInfo(format);

        bool valid[] = {false, false, false, false};
        for (uint32_t c = 0; c < info.numComps; ++c)
        {
            valid[info.swizzle[c]] = true;
        }

        for (uint32_t c = 0; c < 4; ++c)
        {
            if (!valid[c])
            {
                src[c] = BITCAST(VIMMED1((int)info.defaults[c]), mSimdFP32Ty);
            }
        }
    }

    void ApplyUnusedDefaults(SWR_FORMAT format, Value* src[4])
    {
        const SWR_FORMAT_INFO& info = GetFormatInfo(format);

        for (uint32_t c = 0; c < info.numComps; ++c)
        {
            if (info.type[c] == SWR_TYPE_UNUSED)
            {
                src[info.swizzle[c]] =
                    BITCAST(VIMMED1((int)info.defaults[info.swizzle[c]]), mSimdFP32Ty);
            }
        }
    }

    void Quantize(SWR_FORMAT format, Value* src[4])
    {
        const SWR_FORMAT_INFO& info = GetFormatInfo(format);
        for (uint32_t c = 0; c < info.numComps; ++c)
        {
            if (info.bpc[c] <= QUANTIZE_THRESHOLD && info.type[c] != SWR_TYPE_UNUSED)
            {
                uint32_t swizComp = info.swizzle[c];
                float    factor   = (float)((1 << info.bpc[c]) - 1);
                switch (info.type[c])
                {
                case SWR_TYPE_UNORM:
                    src[swizComp] = FADD(FMUL(src[swizComp], VIMMED1(factor)), VIMMED1(0.5f));
                    src[swizComp] = VROUND(src[swizComp], C(_MM_FROUND_TO_ZERO));
                    src[swizComp] = FMUL(src[swizComp], VIMMED1(1.0f / factor));
                    break;
                default:
                    SWR_INVALID("Unsupported format type: %d", info.type[c]);
                }
            }
        }
    }

    template <bool Color, bool Alpha>
    void BlendFunc(SWR_BLEND_OP blendOp,
                   Value*       src[4],
                   Value*       srcFactor[4],
                   Value*       dst[4],
                   Value*       dstFactor[4],
                   Value*       result[4])
    {
        Value* out[4];
        Value* srcBlend[4];
        Value* dstBlend[4];
        for (uint32_t i = 0; i < 4; ++i)
        {
            srcBlend[i] = FMUL(src[i], srcFactor[i]);
            dstBlend[i] = FMUL(dst[i], dstFactor[i]);
        }

        switch (blendOp)
        {
        case BLENDOP_ADD:
            out[0] = FADD(srcBlend[0], dstBlend[0]);
            out[1] = FADD(srcBlend[1], dstBlend[1]);
            out[2] = FADD(srcBlend[2], dstBlend[2]);
            out[3] = FADD(srcBlend[3], dstBlend[3]);
            break;

        case BLENDOP_SUBTRACT:
            out[0] = FSUB(srcBlend[0], dstBlend[0]);
            out[1] = FSUB(srcBlend[1], dstBlend[1]);
            out[2] = FSUB(srcBlend[2], dstBlend[2]);
            out[3] = FSUB(srcBlend[3], dstBlend[3]);
            break;

        case BLENDOP_REVSUBTRACT:
            out[0] = FSUB(dstBlend[0], srcBlend[0]);
            out[1] = FSUB(dstBlend[1], srcBlend[1]);
            out[2] = FSUB(dstBlend[2], srcBlend[2]);
            out[3] = FSUB(dstBlend[3], srcBlend[3]);
            break;

        case BLENDOP_MIN:
            out[0] = VMINPS(src[0], dst[0]);
            out[1] = VMINPS(src[1], dst[1]);
            out[2] = VMINPS(src[2], dst[2]);
            out[3] = VMINPS(src[3], dst[3]);
            break;

        case BLENDOP_MAX:
            out[0] = VMAXPS(src[0], dst[0]);
            out[1] = VMAXPS(src[1], dst[1]);
            out[2] = VMAXPS(src[2], dst[2]);
            out[3] = VMAXPS(src[3], dst[3]);
            break;

        default:
            SWR_INVALID("Unsupported blend operation: %d", blendOp);
            out[0] = out[1] = out[2] = out[3] = VIMMED1(0.0f);
            break;
        }

        if (Color)
        {
            result[0] = out[0];
            result[1] = out[1];
            result[2] = out[2];
        }

        if (Alpha)
        {
            result[3] = out[3];
        }
    }

    void LogicOpFunc(SWR_LOGIC_OP logicOp, Value* src[4], Value* dst[4], Value* result[4])
    {
        // Op: (s == PS output, d = RT contents)
        switch (logicOp)
        {
        case LOGICOP_CLEAR:
            result[0] = VIMMED1(0);
            result[1] = VIMMED1(0);
            result[2] = VIMMED1(0);
            result[3] = VIMMED1(0);
            break;

        case LOGICOP_NOR:
            // ~(s | d)
            result[0] = XOR(OR(src[0], dst[0]), VIMMED1(0xFFFFFFFF));
            result[1] = XOR(OR(src[1], dst[1]), VIMMED1(0xFFFFFFFF));
            result[2] = XOR(OR(src[2], dst[2]), VIMMED1(0xFFFFFFFF));
            result[3] = XOR(OR(src[3], dst[3]), VIMMED1(0xFFFFFFFF));
            break;

        case LOGICOP_AND_INVERTED:
            // ~s & d
            // todo: use avx andnot instr when I can find the intrinsic to call
            result[0] = AND(XOR(src[0], VIMMED1(0xFFFFFFFF)), dst[0]);
            result[1] = AND(XOR(src[1], VIMMED1(0xFFFFFFFF)), dst[1]);
            result[2] = AND(XOR(src[2], VIMMED1(0xFFFFFFFF)), dst[2]);
            result[3] = AND(XOR(src[3], VIMMED1(0xFFFFFFFF)), dst[3]);
            break;

        case LOGICOP_COPY_INVERTED:
            // ~s
            result[0] = XOR(src[0], VIMMED1(0xFFFFFFFF));
            result[1] = XOR(src[1], VIMMED1(0xFFFFFFFF));
            result[2] = XOR(src[2], VIMMED1(0xFFFFFFFF));
            result[3] = XOR(src[3], VIMMED1(0xFFFFFFFF));
            break;

        case LOGICOP_AND_REVERSE:
            // s & ~d
            // todo: use avx andnot instr when I can find the intrinsic to call
            result[0] = AND(XOR(dst[0], VIMMED1(0xFFFFFFFF)), src[0]);
            result[1] = AND(XOR(dst[1], VIMMED1(0xFFFFFFFF)), src[1]);
            result[2] = AND(XOR(dst[2], VIMMED1(0xFFFFFFFF)), src[2]);
            result[3] = AND(XOR(dst[3], VIMMED1(0xFFFFFFFF)), src[3]);
            break;

        case LOGICOP_INVERT:
            // ~d
            result[0] = XOR(dst[0], VIMMED1(0xFFFFFFFF));
            result[1] = XOR(dst[1], VIMMED1(0xFFFFFFFF));
            result[2] = XOR(dst[2], VIMMED1(0xFFFFFFFF));
            result[3] = XOR(dst[3], VIMMED1(0xFFFFFFFF));
            break;

        case LOGICOP_XOR:
            // s ^ d
            result[0] = XOR(src[0], dst[0]);
            result[1] = XOR(src[1], dst[1]);
            result[2] = XOR(src[2], dst[2]);
            result[3] = XOR(src[3], dst[3]);
            break;

        case LOGICOP_NAND:
            // ~(s & d)
            result[0] = XOR(AND(src[0], dst[0]), VIMMED1(0xFFFFFFFF));
            result[1] = XOR(AND(src[1], dst[1]), VIMMED1(0xFFFFFFFF));
            result[2] = XOR(AND(src[2], dst[2]), VIMMED1(0xFFFFFFFF));
            result[3] = XOR(AND(src[3], dst[3]), VIMMED1(0xFFFFFFFF));
            break;

        case LOGICOP_AND:
            // s & d
            result[0] = AND(src[0], dst[0]);
            result[1] = AND(src[1], dst[1]);
            result[2] = AND(src[2], dst[2]);
            result[3] = AND(src[3], dst[3]);
            break;

        case LOGICOP_EQUIV:
            // ~(s ^ d)
            result[0] = XOR(XOR(src[0], dst[0]), VIMMED1(0xFFFFFFFF));
            result[1] = XOR(XOR(src[1], dst[1]), VIMMED1(0xFFFFFFFF));
            result[2] = XOR(XOR(src[2], dst[2]), VIMMED1(0xFFFFFFFF));
            result[3] = XOR(XOR(src[3], dst[3]), VIMMED1(0xFFFFFFFF));
            break;

        case LOGICOP_NOOP:
            result[0] = dst[0];
            result[1] = dst[1];
            result[2] = dst[2];
            result[3] = dst[3];
            break;

        case LOGICOP_OR_INVERTED:
            // ~s | d
            result[0] = OR(XOR(src[0], VIMMED1(0xFFFFFFFF)), dst[0]);
            result[1] = OR(XOR(src[1], VIMMED1(0xFFFFFFFF)), dst[1]);
            result[2] = OR(XOR(src[2], VIMMED1(0xFFFFFFFF)), dst[2]);
            result[3] = OR(XOR(src[3], VIMMED1(0xFFFFFFFF)), dst[3]);
            break;

        case LOGICOP_COPY:
            result[0] = src[0];
            result[1] = src[1];
            result[2] = src[2];
            result[3] = src[3];
            break;

        case LOGICOP_OR_REVERSE:
            // s | ~d
            result[0] = OR(XOR(dst[0], VIMMED1(0xFFFFFFFF)), src[0]);
            result[1] = OR(XOR(dst[1], VIMMED1(0xFFFFFFFF)), src[1]);
            result[2] = OR(XOR(dst[2], VIMMED1(0xFFFFFFFF)), src[2]);
            result[3] = OR(XOR(dst[3], VIMMED1(0xFFFFFFFF)), src[3]);
            break;

        case LOGICOP_OR:
            // s | d
            result[0] = OR(src[0], dst[0]);
            result[1] = OR(src[1], dst[1]);
            result[2] = OR(src[2], dst[2]);
            result[3] = OR(src[3], dst[3]);
            break;

        case LOGICOP_SET:
            result[0] = VIMMED1(0xFFFFFFFF);
            result[1] = VIMMED1(0xFFFFFFFF);
            result[2] = VIMMED1(0xFFFFFFFF);
            result[3] = VIMMED1(0xFFFFFFFF);
            break;

        default:
            SWR_INVALID("Unsupported logic operation: %d", logicOp);
            result[0] = result[1] = result[2] = result[3] = VIMMED1(0.0f);
            break;
        }
    }

    void
    AlphaTest(const BLEND_COMPILE_STATE& state, Value* pBlendState, Value* ppAlpha, Value* ppMask)
    {
        // load uint32_t reference
        Value* pRef = VBROADCAST(LOAD(pBlendState, {0, SWR_BLEND_STATE_alphaTestReference}));

        // load alpha
        Value* pAlpha = LOAD(ppAlpha, {0, 0});

        Value* pTest = nullptr;
        if (state.alphaTestFormat == ALPHA_TEST_UNORM8)
        {
            // convert float alpha to unorm8
            Value* pAlphaU8 = FMUL(pAlpha, VIMMED1(256.0f));
            pAlphaU8        = FP_TO_UI(pAlphaU8, mSimdInt32Ty);

            // compare
            switch (state.alphaTestFunction)
            {
            case ZFUNC_ALWAYS:
                pTest = VIMMED1(true);
                break;
            case ZFUNC_NEVER:
                pTest = VIMMED1(false);
                break;
            case ZFUNC_LT:
                pTest = ICMP_ULT(pAlphaU8, pRef);
                break;
            case ZFUNC_EQ:
                pTest = ICMP_EQ(pAlphaU8, pRef);
                break;
            case ZFUNC_LE:
                pTest = ICMP_ULE(pAlphaU8, pRef);
                break;
            case ZFUNC_GT:
                pTest = ICMP_UGT(pAlphaU8, pRef);
                break;
            case ZFUNC_NE:
                pTest = ICMP_NE(pAlphaU8, pRef);
                break;
            case ZFUNC_GE:
                pTest = ICMP_UGE(pAlphaU8, pRef);
                break;
            default:
                SWR_INVALID("Invalid alpha test function");
                break;
            }
        }
        else
        {
            // cast ref to float
            pRef = BITCAST(pRef, mSimdFP32Ty);

            // compare
            switch (state.alphaTestFunction)
            {
            case ZFUNC_ALWAYS:
                pTest = VIMMED1(true);
                break;
            case ZFUNC_NEVER:
                pTest = VIMMED1(false);
                break;
            case ZFUNC_LT:
                pTest = FCMP_OLT(pAlpha, pRef);
                break;
            case ZFUNC_EQ:
                pTest = FCMP_OEQ(pAlpha, pRef);
                break;
            case ZFUNC_LE:
                pTest = FCMP_OLE(pAlpha, pRef);
                break;
            case ZFUNC_GT:
                pTest = FCMP_OGT(pAlpha, pRef);
                break;
            case ZFUNC_NE:
                pTest = FCMP_ONE(pAlpha, pRef);
                break;
            case ZFUNC_GE:
                pTest = FCMP_OGE(pAlpha, pRef);
                break;
            default:
                SWR_INVALID("Invalid alpha test function");
                break;
            }
        }

        // load current mask
        Value* pMask = LOAD(ppMask);

        // convert to int1 mask
        pMask = MASK(pMask);

        // and with alpha test result
        pMask = AND(pMask, pTest);

        // convert back to vector mask
        pMask = VMASK(pMask);

        // store new mask
        STORE(pMask, ppMask);
    }

    Function* Create(const BLEND_COMPILE_STATE& state)
    {
        std::stringstream fnName("BLND_",
                                 std::ios_base::in | std::ios_base::out | std::ios_base::ate);
        fnName << ComputeCRC(0, &state, sizeof(state));

        // blend function signature
        // typedef void(*PFN_BLEND_JIT_FUNC)(const SWR_BLEND_CONTEXT*);

        std::vector<Type*> args{
            PointerType::get(Gen_SWR_BLEND_CONTEXT(JM()), 0) // SWR_BLEND_CONTEXT*
        };

        // std::vector<Type*> args{
        //    PointerType::get(Gen_SWR_BLEND_CONTEXT(JM()), 0), // SWR_BLEND_CONTEXT*
        //};

        FunctionType* fTy       = FunctionType::get(IRB()->getVoidTy(), args, false);
        Function*     blendFunc = Function::Create(
            fTy, GlobalValue::ExternalLinkage, fnName.str(), JM()->mpCurrentModule);
        blendFunc->getParent()->setModuleIdentifier(blendFunc->getName());

        BasicBlock* entry = BasicBlock::Create(JM()->mContext, "entry", blendFunc);

        IRB()->SetInsertPoint(entry);

        // arguments
        auto   argitr        = blendFunc->arg_begin();
        Value* pBlendContext = &*argitr++;
        pBlendContext->setName("pBlendContext");
        Value* pBlendState = LOAD(pBlendContext, {0, SWR_BLEND_CONTEXT_pBlendState});
        pBlendState->setName("pBlendState");
        Value* pSrc = LOAD(pBlendContext, {0, SWR_BLEND_CONTEXT_src});
        pSrc->setName("src");
        Value* pSrc1 = LOAD(pBlendContext, {0, SWR_BLEND_CONTEXT_src1});
        pSrc1->setName("src1");
        Value* pSrc0Alpha = LOAD(pBlendContext, {0, SWR_BLEND_CONTEXT_src0alpha});
        pSrc0Alpha->setName("src0alpha");
        Value* sampleNum = LOAD(pBlendContext, {0, SWR_BLEND_CONTEXT_sampleNum});
        sampleNum->setName("sampleNum");
        Value* pDst = LOAD(pBlendContext, {0, SWR_BLEND_CONTEXT_pDst});
        pDst->setName("pDst");
        Value* pResult = LOAD(pBlendContext, {0, SWR_BLEND_CONTEXT_result});
        pResult->setName("result");
        Value* ppoMask = LOAD(pBlendContext, {0, SWR_BLEND_CONTEXT_oMask});
        ppoMask->setName("ppoMask");
        Value* ppMask = LOAD(pBlendContext, {0, SWR_BLEND_CONTEXT_pMask});
        ppMask->setName("pMask");

        static_assert(KNOB_COLOR_HOT_TILE_FORMAT == R32G32B32A32_FLOAT,
                      "Unsupported hot tile format");
        Value* dst[4];
        Value* constantColor[4];
        Value* src[4];
        Value* src1[4];
        Value* result[4];
        for (uint32_t i = 0; i < 4; ++i)
        {
            // load hot tile
            dst[i] = LOAD(pDst, {0, i});

            // load constant color
            constantColor[i] = VBROADCAST(LOAD(pBlendState, {0, SWR_BLEND_STATE_constantColor, i}));

            // load src
            src[i] = LOAD(pSrc, {0, i});

            // load src1
            src1[i] = LOAD(pSrc1, {0, i});
        }
        Value* currentSampleMask = VIMMED1(-1);
        if (state.desc.alphaToCoverageEnable)
        {
            Value*   pClampedSrc = FCLAMP(src[3], 0.0f, 1.0f);
            uint32_t bits        = (1 << state.desc.numSamples) - 1;
            currentSampleMask    = FMUL(pClampedSrc, VBROADCAST(C((float)bits)));
            currentSampleMask    = FP_TO_SI(FADD(currentSampleMask, VIMMED1(0.5f)), mSimdInt32Ty);
        }

        // alpha test
        if (state.desc.alphaTestEnable)
        {
            // Gather for archrast stats
            STORE(C(1), pBlendContext, {0, SWR_BLEND_CONTEXT_isAlphaTested});
            AlphaTest(state, pBlendState, pSrc0Alpha, ppMask);
        }
        else
        {
            // Gather for archrast stats
            STORE(C(0), pBlendContext, {0, SWR_BLEND_CONTEXT_isAlphaTested});
        }

        // color blend
        if (state.blendState.blendEnable)
        {
            // Gather for archrast stats
            STORE(C(1), pBlendContext, {0, SWR_BLEND_CONTEXT_isAlphaBlended});

            // clamp sources
            Clamp(state.format, src);
            Clamp(state.format, src1);
            Clamp(state.format, dst);
            Clamp(state.format, constantColor);

            // apply defaults to hottile contents to take into account missing components
            ApplyDefaults(state.format, dst);

            // Force defaults for unused 'X' components
            ApplyUnusedDefaults(state.format, dst);

            // Quantize low precision components
            Quantize(state.format, dst);

            // special case clamping for R11G11B10_float which has no sign bit
            if (state.format == R11G11B10_FLOAT)
            {
                dst[0] = VMAXPS(dst[0], VIMMED1(0.0f));
                dst[1] = VMAXPS(dst[1], VIMMED1(0.0f));
                dst[2] = VMAXPS(dst[2], VIMMED1(0.0f));
                dst[3] = VMAXPS(dst[3], VIMMED1(0.0f));
            }

            Value* srcFactor[4];
            Value* dstFactor[4];
            if (state.desc.independentAlphaBlendEnable)
            {
                GenerateBlendFactor<true, false>(
                    state.blendState.sourceBlendFactor, constantColor, src, src1, dst, srcFactor);
                GenerateBlendFactor<false, true>(state.blendState.sourceAlphaBlendFactor,
                                                 constantColor,
                                                 src,
                                                 src1,
                                                 dst,
                                                 srcFactor);

                GenerateBlendFactor<true, false>(
                    state.blendState.destBlendFactor, constantColor, src, src1, dst, dstFactor);
                GenerateBlendFactor<false, true>(state.blendState.destAlphaBlendFactor,
                                                 constantColor,
                                                 src,
                                                 src1,
                                                 dst,
                                                 dstFactor);

                BlendFunc<true, false>(
                    state.blendState.colorBlendFunc, src, srcFactor, dst, dstFactor, result);
                BlendFunc<false, true>(
                    state.blendState.alphaBlendFunc, src, srcFactor, dst, dstFactor, result);
            }
            else
            {
                GenerateBlendFactor<true, true>(
                    state.blendState.sourceBlendFactor, constantColor, src, src1, dst, srcFactor);
                GenerateBlendFactor<true, true>(
                    state.blendState.destBlendFactor, constantColor, src, src1, dst, dstFactor);

                BlendFunc<true, true>(
                    state.blendState.colorBlendFunc, src, srcFactor, dst, dstFactor, result);
            }

            // store results out
            for (uint32_t i = 0; i < 4; ++i)
            {
                STORE(result[i], pResult, {0, i});
            }
        }
        else
        {
            // Gather for archrast stats
            STORE(C(0), pBlendContext, {0, SWR_BLEND_CONTEXT_isAlphaBlended});
        }

        if (state.blendState.logicOpEnable)
        {
            const SWR_FORMAT_INFO& info = GetFormatInfo(state.format);
            Value*                 vMask[4];
            float                  scale[4];

            if (!state.blendState.blendEnable)
            {
                Clamp(state.format, src);
                Clamp(state.format, dst);
            }

            for (uint32_t i = 0; i < 4; i++)
            {
                if (info.type[i] == SWR_TYPE_UNUSED)
                {
                    continue;
                }

                if (info.bpc[i] >= 32)
                {
                    vMask[i] = VIMMED1(0xFFFFFFFF);
                    scale[i] = 0xFFFFFFFF;
                }
                else
                {
                    vMask[i] = VIMMED1((1 << info.bpc[i]) - 1);
                    if (info.type[i] == SWR_TYPE_SNORM)
                        scale[i] = (1 << (info.bpc[i] - 1)) - 1;
                    else
                        scale[i] = (1 << info.bpc[i]) - 1;
                }

                switch (info.type[i])
                {
                default:
                    SWR_INVALID("Unsupported type for logic op: %d", info.type[i]);
                    break;

                case SWR_TYPE_UNKNOWN:
                case SWR_TYPE_UNUSED:
                    // fallthrough

                case SWR_TYPE_UINT:
                case SWR_TYPE_SINT:
                    src[i] = BITCAST(src[i], mSimdInt32Ty);
                    dst[i] = BITCAST(dst[i], mSimdInt32Ty);
                    break;
                case SWR_TYPE_SNORM:
                    src[i] = FP_TO_SI(FMUL(src[i], VIMMED1(scale[i])), mSimdInt32Ty);
                    dst[i] = FP_TO_SI(FMUL(dst[i], VIMMED1(scale[i])), mSimdInt32Ty);
                    break;
                case SWR_TYPE_UNORM:
                    src[i] = FP_TO_UI(FMUL(src[i], VIMMED1(scale[i])), mSimdInt32Ty);
                    dst[i] = FP_TO_UI(FMUL(dst[i], VIMMED1(scale[i])), mSimdInt32Ty);
                    break;
                }
            }

            LogicOpFunc(state.blendState.logicOpFunc, src, dst, result);

            // store results out
            for (uint32_t i = 0; i < 4; ++i)
            {
                if (info.type[i] == SWR_TYPE_UNUSED)
                {
                    continue;
                }

                // clear upper bits from PS output not in RT format after doing logic op
                result[i] = AND(result[i], vMask[i]);

                switch (info.type[i])
                {
                default:
                    SWR_INVALID("Unsupported type for logic op: %d", info.type[i]);
                    break;

                case SWR_TYPE_UNKNOWN:
                case SWR_TYPE_UNUSED:
                    // fallthrough

                case SWR_TYPE_UINT:
                case SWR_TYPE_SINT:
                    result[i] = BITCAST(result[i], mSimdFP32Ty);
                    break;
                case SWR_TYPE_SNORM:
                    result[i] = SHL(result[i], C(32 - info.bpc[i]));
                    result[i] = ASHR(result[i], C(32 - info.bpc[i]));
                    result[i] = FMUL(SI_TO_FP(result[i], mSimdFP32Ty), VIMMED1(1.0f / scale[i]));
                    break;
                case SWR_TYPE_UNORM:
                    result[i] = FMUL(UI_TO_FP(result[i], mSimdFP32Ty), VIMMED1(1.0f / scale[i]));
                    break;
                }

                STORE(result[i], pResult, {0, i});
            }
        }

        if (state.desc.oMaskEnable)
        {
            assert(!(state.desc.alphaToCoverageEnable));
            // load current mask
            Value* oMask      = LOAD(ppoMask);
            currentSampleMask = AND(oMask, currentSampleMask);
        }

        if (state.desc.sampleMaskEnable)
        {
            Value* sampleMask = LOAD(pBlendState, {0, SWR_BLEND_STATE_sampleMask});
            currentSampleMask = AND(VBROADCAST(sampleMask), currentSampleMask);
        }

        if (state.desc.sampleMaskEnable || state.desc.alphaToCoverageEnable ||
            state.desc.oMaskEnable)
        {
            // load coverage mask and mask off any lanes with no samples
            Value* pMask        = LOAD(ppMask);
            Value* sampleMasked = SHL(C(1), sampleNum);
            currentSampleMask   = AND(currentSampleMask, VBROADCAST(sampleMasked));
            currentSampleMask = S_EXT(ICMP_UGT(currentSampleMask, VBROADCAST(C(0))), mSimdInt32Ty);
            Value* outputMask = AND(pMask, currentSampleMask);
            // store new mask
            STORE(outputMask, GEP(ppMask, C(0)));
        }

        RET_VOID();

        JitManager::DumpToFile(blendFunc, "");

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

        passes.run(*blendFunc);

        JitManager::DumpToFile(blendFunc, "optimized");

        return blendFunc;
    }
};

//////////////////////////////////////////////////////////////////////////
/// @brief JITs from fetch shader IR
/// @param hJitMgr - JitManager handle
/// @param func   - LLVM function IR
/// @return PFN_FETCH_FUNC - pointer to fetch code
PFN_BLEND_JIT_FUNC JitBlendFunc(HANDLE hJitMgr, const HANDLE hFunc)
{
    const llvm::Function* func    = (const llvm::Function*)hFunc;
    JitManager*           pJitMgr = reinterpret_cast<JitManager*>(hJitMgr);
    PFN_BLEND_JIT_FUNC    pfnBlend;
    pfnBlend = (PFN_BLEND_JIT_FUNC)(pJitMgr->mpExec->getFunctionAddress(func->getName().str()));
    // MCJIT finalizes modules the first time you JIT code from them. After finalized, you cannot
    // add new IR to the module
    pJitMgr->mIsModuleFinalized = true;

    return pfnBlend;
}

//////////////////////////////////////////////////////////////////////////
/// @brief JIT compiles blend shader
/// @param hJitMgr - JitManager handle
/// @param state   - blend state to build function from
extern "C" PFN_BLEND_JIT_FUNC JITCALL JitCompileBlend(HANDLE                     hJitMgr,
                                                      const BLEND_COMPILE_STATE& state)
{
    JitManager* pJitMgr = reinterpret_cast<JitManager*>(hJitMgr);

    pJitMgr->SetupNewModule();

    BlendJit theJit(pJitMgr);
    HANDLE   hFunc = theJit.Create(state);

    return JitBlendFunc(hJitMgr, hFunc);
}
