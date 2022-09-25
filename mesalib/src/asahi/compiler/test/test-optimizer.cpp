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

#define CASE(instr, expected, size) INSTRUCTION_CASE(\
      { UNUSED agx_index out = agx_temp(b->shader, AGX_SIZE_ ## size); \
        instr; agx_unit_test(b, out); }, \
      { UNUSED agx_index out = agx_temp(b->shader, AGX_SIZE_ ## size); \
        expected; agx_unit_test(b, out); }, \
        agx_optimize_and_dce)

#define NEGCASE(instr, size) CASE(instr, instr, size)

#define CASE16(instr, expected) CASE(instr, expected, 16)
#define CASE32(instr, expected) CASE(instr, expected, 32)

#define NEGCASE16(instr) NEGCASE(instr, 16)
#define NEGCASE32(instr) NEGCASE(instr, 32)

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
      hy     = agx_register(1, AGX_SIZE_16);
      hz     = agx_register(2, AGX_SIZE_16);
   }

   ~Optimizer() {
      ralloc_free(mem_ctx);
   }

   void *mem_ctx;

   agx_index wx, wy, wz, hx, hy, hz;
};

TEST_F(Optimizer, FloatCopyprop)
{
   CASE32(agx_fadd_to(b, out, agx_abs(agx_fmov(b, wx)), wy),
          agx_fadd_to(b, out, agx_abs(wx), wy));

   CASE32(agx_fadd_to(b, out, agx_neg(agx_fmov(b, wx)), wy),
          agx_fadd_to(b, out, agx_neg(wx), wy));
}

TEST_F(Optimizer, FusedFABSNEG)
{
   CASE32(agx_fadd_to(b, out, agx_fmov(b, agx_abs(wx)), wy),
          agx_fadd_to(b, out, agx_abs(wx), wy));

   CASE32(agx_fmul_to(b, out, wx, agx_fmov(b, agx_neg(agx_abs(wx)))),
          agx_fmul_to(b, out, wx, agx_neg(agx_abs(wx))));
}

TEST_F(Optimizer, FusedFabsAbsorb)
{
   CASE32(agx_fadd_to(b, out, agx_abs(agx_fmov(b, agx_abs(wx))), wy),
          agx_fadd_to(b, out, agx_abs(wx), wy));
}

TEST_F(Optimizer, FusedFnegCancel)
{
   CASE32(agx_fmul_to(b, out, wx, agx_neg(agx_fmov(b, agx_neg(wx)))),
          agx_fmul_to(b, out, wx, wx));

   CASE32(agx_fmul_to(b, out, wx, agx_neg(agx_fmov(b, agx_neg(agx_abs(wx))))),
          agx_fmul_to(b, out, wx, agx_abs(wx)));
}

TEST_F(Optimizer, FmulFsatF2F16)
{
   CASE16({
         agx_index tmp = agx_temp(b->shader, AGX_SIZE_32);
         agx_fmov_to(b, tmp, agx_fmul(b, wx, wy))->saturate = true;
         agx_fmov_to(b, out, tmp);
   }, {
         agx_fmul_to(b, out, wx, wy)->saturate = true;
   });
}

TEST_F(Optimizer, Copyprop)
{
   CASE32(agx_fmul_to(b, out, wx, agx_mov(b, wy)), agx_fmul_to(b, out, wx, wy));
   CASE32(agx_fmul_to(b, out, agx_mov(b, wx), agx_mov(b, wy)), agx_fmul_to(b, out, wx, wy));
}

TEST_F(Optimizer, InlineHazards)
{
   NEGCASE32({
         agx_instr *I = agx_p_combine_to(b, out, 4);
         I->src[0] = agx_mov_imm(b, AGX_SIZE_32, 0);
         I->src[1] = wy;
         I->src[2] = wz;
         I->src[3] = wz;
   });
}

TEST_F(Optimizer, CopypropRespectsAbsNeg)
{
   CASE32(agx_fadd_to(b, out, agx_abs(agx_mov(b, wx)), wy),
          agx_fadd_to(b, out, agx_abs(wx), wy));

   CASE32(agx_fadd_to(b, out, agx_neg(agx_mov(b, wx)), wy),
          agx_fadd_to(b, out, agx_neg(wx), wy));

   CASE32(agx_fadd_to(b, out, agx_neg(agx_abs(agx_mov(b, wx))), wy),
          agx_fadd_to(b, out, agx_neg(agx_abs(wx)), wy));
}

TEST_F(Optimizer, IntCopyprop)
{
   CASE32(agx_xor_to(b, out, agx_mov(b, wx), wy),
          agx_xor_to(b, out, wx, wy));
}

TEST_F(Optimizer, IntCopypropDoesntConvert)
{
   NEGCASE32({
         agx_index cvt = agx_temp(b->shader, AGX_SIZE_32);
         agx_mov_to(b, cvt, hx);
         agx_xor_to(b, out, cvt, wy);
   });
}
