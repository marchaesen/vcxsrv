/*
 * Copyright 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "agx_builder.h"
#include "agx_compiler.h"
#include "agx_test.h"

#include <gtest/gtest.h>

static void
agx_optimize_and_dce(agx_context *ctx)
{
   agx_optimizer_backward(ctx);
   agx_optimizer_forward(ctx);
   agx_dce(ctx, true);
}

#define CASE(instr, expected, size, returns)                                   \
   INSTRUCTION_CASE(                                                           \
      {                                                                        \
         UNUSED agx_index out = agx_temp(b->shader, AGX_SIZE_##size);          \
         instr;                                                                \
         if (returns)                                                          \
            agx_unit_test(b, out);                                             \
      },                                                                       \
      {                                                                        \
         UNUSED agx_index out = agx_temp(b->shader, AGX_SIZE_##size);          \
         expected;                                                             \
         if (returns)                                                          \
            agx_unit_test(b, out);                                             \
      },                                                                       \
      agx_optimize_and_dce)

#define NEGCASE(instr, size) CASE(instr, instr, size, true)

#define CASE16(instr, expected) CASE(instr, expected, 16, true)
#define CASE32(instr, expected) CASE(instr, expected, 32, true)
#define CASE64(instr, expected) CASE(instr, expected, 64, true)

#define CASE_NO_RETURN(instr, expected)                                        \
   CASE(instr, expected, 32 /* irrelevant */, false)

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
   Optimizer()
   {
      mem_ctx = ralloc_context(NULL);

      dx = agx_register(0, AGX_SIZE_64);
      dz = agx_register(4, AGX_SIZE_64);

      wx = agx_register(0, AGX_SIZE_32);
      wy = agx_register(2, AGX_SIZE_32);
      wz = agx_register(4, AGX_SIZE_32);

      hx = agx_register(0, AGX_SIZE_16);
      hy = agx_register(1, AGX_SIZE_16);
      hz = agx_register(2, AGX_SIZE_16);
   }

   ~Optimizer()
   {
      ralloc_free(mem_ctx);
   }

   void *mem_ctx;

   agx_index dx, dz, wx, wy, wz, hx, hy, hz;
};

TEST_F(Optimizer, FloatCopyprop)
{
   CASE32(agx_fadd_to(b, out, agx_abs(agx_fmov(b, wx)), wy),
          agx_fadd_to(b, out, agx_abs(wx), wy));

   CASE32(agx_fadd_to(b, out, agx_neg(agx_fmov(b, wx)), wy),
          agx_fadd_to(b, out, agx_neg(wx), wy));
}

TEST_F(Optimizer, FloatConversion)
{
   CASE32(
      {
         agx_index cvt = agx_temp(b->shader, AGX_SIZE_32);
         agx_fmov_to(b, cvt, hx);
         agx_fadd_to(b, out, cvt, wy);
      },
      { agx_fadd_to(b, out, hx, wy); });

   CASE16(
      {
         agx_index sum = agx_temp(b->shader, AGX_SIZE_32);
         agx_fadd_to(b, sum, wx, wy);
         agx_fmov_to(b, out, sum);
      },
      { agx_fadd_to(b, out, wx, wy); });
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

TEST_F(Optimizer, FusedNot)
{
   CASE32(agx_not_to(b, out, agx_and(b, wx, wx)), agx_nand_to(b, out, wx, wx));

   CASE32(agx_not_to(b, out, agx_or(b, wx, wx)), agx_nor_to(b, out, wx, wx));

   CASE32(agx_not_to(b, out, agx_xor(b, wx, wx)), agx_xnor_to(b, out, wx, wx));

   CASE32(agx_xor_to(b, out, agx_not(b, wx), agx_not(b, wx)),
          agx_xor_to(b, out, wx, wx));

   CASE32(agx_xor_to(b, out, agx_not(b, wx), wx), agx_xnor_to(b, out, wx, wx));

   CASE32(agx_xor_to(b, out, wx, agx_not(b, wx)), agx_xnor_to(b, out, wx, wx));

   CASE32(agx_nand_to(b, out, agx_not(b, wx), agx_not(b, wx)),
          agx_or_to(b, out, wx, wx));

   CASE32(agx_andn1_to(b, out, agx_not(b, wx), wx), agx_and_to(b, out, wx, wx));

   CASE32(agx_andn1_to(b, out, wx, agx_not(b, wx)), agx_nor_to(b, out, wx, wx));

   CASE32(agx_andn2_to(b, out, agx_not(b, wx), wx), agx_nor_to(b, out, wx, wx));

   CASE32(agx_andn2_to(b, out, wx, agx_not(b, wx)), agx_and_to(b, out, wx, wx));

   CASE32(agx_xor_to(b, out, agx_not(b, wx), agx_uniform(8, AGX_SIZE_32)),
          agx_xnor_to(b, out, wx, agx_uniform(8, AGX_SIZE_32)));

   CASE32(agx_or_to(b, out, agx_immediate(123), agx_not(b, wx)),
          agx_orn2_to(b, out, agx_immediate(123), wx));

   CASE32(agx_xor_to(b, out, wx, agx_not(b, wy)), agx_xnor_to(b, out, wx, wy));

   CASE32(agx_xor_to(b, out, wy, agx_not(b, wx)), agx_xnor_to(b, out, wy, wx));

   CASE32(agx_and_to(b, out, agx_not(b, wx), wy), agx_andn1_to(b, out, wx, wy));

   CASE32(agx_or_to(b, out, wx, agx_not(b, wy)), agx_orn2_to(b, out, wx, wy));
}

TEST_F(Optimizer, FmulFsatF2F16)
{
   CASE16(
      {
         agx_index tmp = agx_temp(b->shader, AGX_SIZE_32);
         agx_fmov_to(b, tmp, agx_fmul(b, wx, wy))->saturate = true;
         agx_fmov_to(b, out, tmp);
      },
      { agx_fmul_to(b, out, wx, wy)->saturate = true; });
}

TEST_F(Optimizer, FsatWithPhi)
{
   /*
    * Construct the loop:
    *
    * A:
    *   ...
    *
    * B:
    *    phi ..., u
    *    u = wx * phi
    *    out = fsat u
    *    --> B
    *
    * This example shows that phi sources are read at the end of the
    * predecessor, not at the start of the successor. If phis are not handled
    * properly, the fsat would be fused incorrectly.
    *
    * This reproduces an issue hit in a Control shader. Astonishingly, it is not
    * hit anywhere in CTS.
    */
   NEGCASE32({
      agx_block *A = agx_start_block(b->shader);
      agx_block *B = agx_test_block(b->shader);

      agx_block_add_successor(A, B);
      agx_block_add_successor(B, B);

      b->cursor = agx_after_block(B);
      agx_index u = agx_temp(b->shader, AGX_SIZE_32);

      agx_instr *phi = agx_phi_to(b, agx_temp(b->shader, AGX_SIZE_32), 2);
      phi->src[0] = wx;
      phi->src[1] = u;

      agx_fmul_to(b, u, wx, phi->dest[0]);
      agx_fmov_to(b, out, u)->saturate = true;
   });
}

TEST_F(Optimizer, Copyprop)
{
   CASE32(agx_fmul_to(b, out, wx, agx_mov(b, wy)), agx_fmul_to(b, out, wx, wy));
   CASE32(agx_fmul_to(b, out, agx_mov(b, wx), agx_mov(b, wy)),
          agx_fmul_to(b, out, wx, wy));
}

TEST_F(Optimizer, SourceZeroExtend)
{
   CASE32(
      {
         agx_index t = agx_temp(b->shader, AGX_SIZE_32);
         agx_mov_to(b, t, hy);
         agx_ffs_to(b, out, t);
      },
      agx_ffs_to(b, out, hy));
}

TEST_F(Optimizer, AddSourceZeroExtend)
{
   CASE32(
      {
         agx_index t = agx_temp(b->shader, AGX_SIZE_32);
         agx_mov_to(b, t, hy);
         agx_iadd_to(b, out, wx, t, 1);
      },
      agx_iadd_to(b, out, wx, agx_abs(hy), 1));
}

TEST_F(Optimizer, AddSourceSignExtend)
{
   CASE32(
      {
         agx_index t = agx_temp(b->shader, AGX_SIZE_32);
         agx_signext_to(b, t, hy);
         agx_iadd_to(b, out, wx, t, 1);
      },
      agx_iadd_to(b, out, wx, hy, 1));
}

TEST_F(Optimizer, SubInlineImmediate)
{
   CASE16(agx_iadd_to(b, out, hx, agx_mov_imm(b, 16, -2), 0),
          agx_iadd_to(b, out, hx, agx_neg(agx_immediate(2)), 0));

   CASE32(agx_iadd_to(b, out, wx, agx_mov_imm(b, 32, -1), 0),
          agx_iadd_to(b, out, wx, agx_neg(agx_immediate(1)), 0));

   CASE64(agx_iadd_to(b, out, dx, agx_mov_imm(b, 64, -17), 0),
          agx_iadd_to(b, out, dx, agx_neg(agx_immediate(17)), 0));

   CASE16(agx_imad_to(b, out, hx, hy, agx_mov_imm(b, 16, -2), 0),
          agx_imad_to(b, out, hx, hy, agx_neg(agx_immediate(2)), 0));

   CASE32(agx_imad_to(b, out, wx, wy, agx_mov_imm(b, 32, -1), 0),
          agx_imad_to(b, out, wx, wy, agx_neg(agx_immediate(1)), 0));

   CASE64(agx_imad_to(b, out, dx, dz, agx_mov_imm(b, 64, -17), 0),
          agx_imad_to(b, out, dx, dz, agx_neg(agx_immediate(17)), 0));
}

TEST_F(Optimizer, InlineHazards)
{
   NEGCASE32({
      agx_index zero = agx_mov_imm(b, AGX_SIZE_32, 0);
      agx_instr *I = agx_collect_to(b, out, 4);

      I->src[0] = zero;
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
   CASE32(agx_xor_to(b, out, agx_mov(b, wx), wy), agx_xor_to(b, out, wx, wy));
}

TEST_F(Optimizer, CopypropSplitMovedUniform64)
{
   CASE32(
      {
         /* emit_load_preamble puts in the move, so we do too */
         agx_index mov = agx_mov(b, agx_uniform(40, AGX_SIZE_64));
         agx_instr *spl = agx_split(b, 2, mov);
         spl->dest[0] = agx_temp(b->shader, AGX_SIZE_32);
         spl->dest[1] = agx_temp(b->shader, AGX_SIZE_32);
         agx_xor_to(b, out, spl->dest[0], spl->dest[1]);
      },
      {
         agx_xor_to(b, out, agx_uniform(40, AGX_SIZE_32),
                    agx_uniform(42, AGX_SIZE_32));
      });
}

TEST_F(Optimizer, IntCopypropDoesntConvert)
{
   NEGCASE32({
      agx_index cvt = agx_temp(b->shader, AGX_SIZE_32);
      agx_mov_to(b, cvt, hx);
      agx_fmul_to(b, out, cvt, wy);
   });
}

TEST_F(Optimizer, SkipPreloads)
{
   NEGCASE32({
      agx_index preload = agx_preload(b, agx_register(0, AGX_SIZE_32));
      agx_xor_to(b, out, preload, wy);
   });
}

TEST_F(Optimizer, NoConversionsOn16BitALU)
{
   NEGCASE16({
      agx_index cvt = agx_temp(b->shader, AGX_SIZE_16);
      agx_fmov_to(b, cvt, wx);
      agx_fadd_to(b, out, cvt, hy);
   });

   NEGCASE32(agx_fmov_to(b, out, agx_fadd(b, hx, hy)));
}

TEST_F(Optimizer, BallotCondition)
{
   CASE32(agx_ballot_to(b, out, agx_icmp(b, wx, wy, AGX_ICOND_UEQ, true)),
          agx_icmp_ballot_to(b, out, wx, wy, AGX_ICOND_UEQ, true));

   CASE32(agx_ballot_to(b, out, agx_fcmp(b, wx, wy, AGX_FCOND_GE, false)),
          agx_fcmp_ballot_to(b, out, wx, wy, AGX_FCOND_GE, false));

   CASE32(agx_quad_ballot_to(b, out, agx_icmp(b, wx, wy, AGX_ICOND_UEQ, true)),
          agx_icmp_quad_ballot_to(b, out, wx, wy, AGX_ICOND_UEQ, true));

   CASE32(agx_quad_ballot_to(b, out, agx_fcmp(b, wx, wy, AGX_FCOND_GT, false)),
          agx_fcmp_quad_ballot_to(b, out, wx, wy, AGX_FCOND_GT, false));
}

TEST_F(Optimizer, BallotMultipleUses)
{
   CASE32(
      {
         agx_index cmp = agx_fcmp(b, wx, wy, AGX_FCOND_GT, false);
         agx_index ballot = agx_quad_ballot(b, cmp);
         agx_fadd_to(b, out, cmp, ballot);
      },
      {
         agx_index cmp = agx_fcmp(b, wx, wy, AGX_FCOND_GT, false);
         agx_index ballot =
            agx_fcmp_quad_ballot(b, wx, wy, AGX_FCOND_GT, false);
         agx_fadd_to(b, out, cmp, ballot);
      });
}

/*
 * We had a bug where the ballot optimization didn't check the agx_index's type
 * so would fuse constants with overlapping values. An unrelated common code
 * change surfaced this in CTS case:
 *
 *    dEQP-VK.subgroups.vote.frag_helper.subgroupallequal_bool_fragment
 *
 * We passed Vulkan CTS without hitting it though, hence the targeted test.
 */
TEST_F(Optimizer, BallotConstant)
{
   CASE32(
      {
         agx_index cmp = agx_fcmp(b, wx, wy, AGX_FCOND_GT, false);
         agx_index ballot = agx_quad_ballot(b, agx_immediate(cmp.value));
         agx_index ballot2 = agx_quad_ballot(b, cmp);
         agx_fadd_to(b, out, ballot, agx_fadd(b, ballot2, cmp));
      },
      {
         agx_index cmp = agx_fcmp(b, wx, wy, AGX_FCOND_GT, false);
         agx_index ballot = agx_quad_ballot(b, agx_immediate(cmp.value));
         agx_index ballot2 =
            agx_fcmp_quad_ballot(b, wx, wy, AGX_FCOND_GT, false);
         agx_fadd_to(b, out, ballot, agx_fadd(b, ballot2, cmp));
      });
}

TEST_F(Optimizer, IfCondition)
{
   CASE_NO_RETURN(agx_if_icmp(b, agx_icmp(b, wx, wy, AGX_ICOND_UEQ, true),
                              agx_zero(), 1, AGX_ICOND_UEQ, true, NULL),
                  agx_if_icmp(b, wx, wy, 1, AGX_ICOND_UEQ, true, NULL));

   CASE_NO_RETURN(agx_if_icmp(b, agx_fcmp(b, wx, wy, AGX_FCOND_EQ, true),
                              agx_zero(), 1, AGX_ICOND_UEQ, true, NULL),
                  agx_if_fcmp(b, wx, wy, 1, AGX_FCOND_EQ, true, NULL));

   CASE_NO_RETURN(agx_if_icmp(b, agx_fcmp(b, hx, hy, AGX_FCOND_LT, false),
                              agx_zero(), 1, AGX_ICOND_UEQ, true, NULL),
                  agx_if_fcmp(b, hx, hy, 1, AGX_FCOND_LT, false, NULL));
}

TEST_F(Optimizer, SelectCondition)
{
   CASE32(agx_icmpsel_to(b, out, agx_icmp(b, wx, wy, AGX_ICOND_UEQ, false),
                         agx_zero(), wz, wx, AGX_ICOND_UEQ),
          agx_icmpsel_to(b, out, wx, wy, wx, wz, AGX_ICOND_UEQ));

   CASE32(agx_icmpsel_to(b, out, agx_icmp(b, wx, wy, AGX_ICOND_UEQ, true),
                         agx_zero(), wz, wx, AGX_ICOND_UEQ),
          agx_icmpsel_to(b, out, wx, wy, wz, wx, AGX_ICOND_UEQ));

   CASE32(agx_icmpsel_to(b, out, agx_fcmp(b, wx, wy, AGX_FCOND_EQ, false),
                         agx_zero(), wz, wx, AGX_ICOND_UEQ),
          agx_fcmpsel_to(b, out, wx, wy, wx, wz, AGX_FCOND_EQ));

   CASE32(agx_icmpsel_to(b, out, agx_fcmp(b, wx, wy, AGX_FCOND_LT, true),
                         agx_zero(), wz, wx, AGX_ICOND_UEQ),
          agx_fcmpsel_to(b, out, wx, wy, wz, wx, AGX_FCOND_LT));
}

TEST_F(Optimizer, IfInverted)
{
   CASE_NO_RETURN(
      agx_if_icmp(b, agx_xor(b, hx, agx_immediate(1)), agx_zero(), 1,
                  AGX_ICOND_UEQ, true, NULL),
      agx_if_icmp(b, hx, agx_zero(), 1, AGX_ICOND_UEQ, false, NULL));

   CASE_NO_RETURN(agx_if_icmp(b, agx_xor(b, hx, agx_immediate(1)), agx_zero(),
                              1, AGX_ICOND_UEQ, false, NULL),
                  agx_if_icmp(b, hx, agx_zero(), 1, AGX_ICOND_UEQ, true, NULL));
}

TEST_F(Optimizer, IfInvertedCondition)
{
   CASE_NO_RETURN(
      agx_if_icmp(
         b,
         agx_xor(b, agx_icmp(b, wx, wy, AGX_ICOND_UEQ, true), agx_immediate(1)),
         agx_zero(), 1, AGX_ICOND_UEQ, true, NULL),
      agx_if_icmp(b, wx, wy, 1, AGX_ICOND_UEQ, false, NULL));

   CASE_NO_RETURN(
      agx_if_icmp(
         b,
         agx_xor(b, agx_fcmp(b, wx, wy, AGX_FCOND_EQ, true), agx_immediate(1)),
         agx_zero(), 1, AGX_ICOND_UEQ, true, NULL),
      agx_if_fcmp(b, wx, wy, 1, AGX_FCOND_EQ, false, NULL));

   CASE_NO_RETURN(
      agx_if_icmp(
         b,
         agx_xor(b, agx_fcmp(b, hx, hy, AGX_FCOND_LT, false), agx_immediate(1)),
         agx_zero(), 1, AGX_ICOND_UEQ, true, NULL),
      agx_if_fcmp(b, hx, hy, 1, AGX_FCOND_LT, true, NULL));
}
