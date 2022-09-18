/*
 * Copyright (C) 2021 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "agx_test.h"

#include <gtest/gtest.h>

static void
agx_optimize_and_dce(agx_context *ctx)
{
   agx_optimizer(ctx);
   agx_dce(ctx);
}

#define CASE(instr, expected) INSTRUCTION_CASE(instr, expected, agx_optimize_and_dce)
#define NEGCASE(instr) CASE(instr, instr)

static inline agx_index
agx_fmov(agx_builder *b, agx_index s0)
{
    agx_index tmp = agx_temp(b->shader, s0.size);
    agx_fmov_to(b, tmp, s0);
    return tmp;
}

class Optimizer : public testing::Test {
protected:
   Optimizer() {
      mem_ctx = ralloc_context(NULL);

      wx     = agx_register(0, AGX_SIZE_32);
      wy     = agx_register(2, AGX_SIZE_32);
      wz     = agx_register(4, AGX_SIZE_32);

      hx     = agx_register(0, AGX_SIZE_16);
   }

   ~Optimizer() {
      ralloc_free(mem_ctx);
   }

   void *mem_ctx;

   agx_index wx, wy, wz, hx;
};

TEST_F(Optimizer, FloatCopyprop)
{
   CASE(agx_fadd_to(b, wz, agx_abs(agx_fmov(b, wx)), wy),
        agx_fadd_to(b, wz, agx_abs(wx), wy));

   CASE(agx_fadd_to(b, wz, agx_neg(agx_fmov(b, wx)), wy),
        agx_fadd_to(b, wz, agx_neg(wx), wy));
}

TEST_F(Optimizer, FusedFABSNEG)
{
   CASE(agx_fadd_to(b, wz, agx_fmov(b, agx_abs(wx)), wy),
        agx_fadd_to(b, wz, agx_abs(wx), wy));

   CASE(agx_fmul_to(b, wz, wx, agx_fmov(b, agx_neg(agx_abs(wx)))),
        agx_fmul_to(b, wz, wx, agx_neg(agx_abs(wx))));
}

TEST_F(Optimizer, FusedFabsAbsorb)
{
   CASE(agx_fadd_to(b, wz, agx_abs(agx_fmov(b, agx_abs(wx))), wy),
        agx_fadd_to(b, wz, agx_abs(wx), wy));
}

TEST_F(Optimizer, FusedFnegCancel)
{
   CASE(agx_fmul_to(b, wz, wx, agx_neg(agx_fmov(b, agx_neg(wx)))),
        agx_fmul_to(b, wz, wx, wx));

   CASE(agx_fmul_to(b, wz, wx, agx_neg(agx_fmov(b, agx_neg(agx_abs(wx))))),
        agx_fmul_to(b, wz, wx, agx_abs(wx)));
}

TEST_F(Optimizer, Copyprop)
{
   CASE(agx_fmul_to(b, wz, wx, agx_mov(b, wy)), agx_fmul_to(b, wz, wx, wy));
   CASE(agx_fmul_to(b, wz, agx_mov(b, wx), agx_mov(b, wy)), agx_fmul_to(b, wz, wx, wy));
}

TEST_F(Optimizer, InlineHazards)
{
   NEGCASE({
         agx_instr *I = agx_p_combine_to(b, wx, 4);
         I->src[0] = agx_mov_imm(b, AGX_SIZE_32, 0);
         I->src[1] = wy;
         I->src[2] = wz;
         I->src[3] = wz;
   });
}

TEST_F(Optimizer, CopypropRespectsAbsNeg)
{
   CASE(agx_fadd_to(b, wz, agx_abs(agx_mov(b, wx)), wy),
        agx_fadd_to(b, wz, agx_abs(wx), wy));

   CASE(agx_fadd_to(b, wz, agx_neg(agx_mov(b, wx)), wy),
        agx_fadd_to(b, wz, agx_neg(wx), wy));

   CASE(agx_fadd_to(b, wz, agx_neg(agx_abs(agx_mov(b, wx))), wy),
        agx_fadd_to(b, wz, agx_neg(agx_abs(wx)), wy));
}

TEST_F(Optimizer, IntCopyprop)
{
   CASE(agx_xor_to(b, wz, agx_mov(b, wx), wy),
        agx_xor_to(b, wz, wx, wy));
}

TEST_F(Optimizer, IntCopypropDoesntConvert)
{
   NEGCASE({
         agx_index cvt = agx_temp(b->shader, AGX_SIZE_32);
         agx_mov_to(b, cvt, hx);
         agx_xor_to(b, wz, cvt, wy);
   });
}
