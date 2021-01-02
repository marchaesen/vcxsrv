/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2018-2019 Collabora LTD
 *
 * Author: Gert Wollny <gert.wollny@collabora.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SFN_EMITALUINSTRUCTION_H
#define SFN_EMITALUINSTRUCTION_H

#include "sfn_emitinstruction.h"

#include "sfn_alu_defines.h"
#include "sfn_instruction_alu.h"
#include "sfn_instruction_tex.h"

namespace r600  {


class EmitAluInstruction : public EmitInstruction
{
public:
   EmitAluInstruction(ShaderFromNirProcessor& processor);

private:

   enum AluOp2Opts {
      op2_opt_none = 0,
      op2_opt_reverse = 1,
      op2_opt_neg_src1 = 1 << 1
   };

   bool do_emit(nir_instr* instr) override;

   void split_constants(const nir_alu_instr& instr, unsigned nsrc_comp);

   bool emit_mov(const nir_alu_instr& instr);
   bool emit_alu_op1(const nir_alu_instr& instr, EAluOp opcode, const AluOpFlags &flags = 0);
   bool emit_alu_op2(const nir_alu_instr& instr, EAluOp opcode, AluOp2Opts ops = op2_opt_none);
   bool emit_alu_op2_split_src_mods(const nir_alu_instr& instr, EAluOp opcode, AluOp2Opts ops = op2_opt_none);

   bool emit_alu_trans_op2(const nir_alu_instr& instr, EAluOp opcode);

   bool emit_alu_inot(const nir_alu_instr& instr);
   bool emit_alu_iabs(const nir_alu_instr& instr);
   bool emit_alu_ineg(const nir_alu_instr& instr);
   bool emit_alu_isign(const nir_alu_instr& instr);
   bool emit_alu_div_int(const nir_alu_instr& instr, bool use_signed, bool mod);
   bool emit_alu_op2_int(const nir_alu_instr& instr, EAluOp opcode, AluOp2Opts ops = op2_opt_none);

   bool emit_alu_op3(const nir_alu_instr& instr, EAluOp opcode, std::array<uint8_t, 3> reorder={0,1,2});
   bool emit_alu_trans_op1(const nir_alu_instr& instr, EAluOp opcode, bool absolute = false);
   bool emit_alu_trig_op1(const nir_alu_instr& instr, EAluOp opcode);

   bool emit_alu_b2f(const nir_alu_instr& instr);
   bool emit_alu_i2orf2_b1(const nir_alu_instr& instr, EAluOp op);
   bool emit_dot(const nir_alu_instr& instr, int n);
   bool emit_fsign(const nir_alu_instr& instr);
   bool emit_create_vec(const nir_alu_instr& instr, unsigned nc);
   bool emit_any_all_icomp(const nir_alu_instr& instr, EAluOp op,  unsigned nc, bool all);
   bool emit_any_iequal(const nir_alu_instr& instr, unsigned nc);

   bool emit_any_all_fcomp(const nir_alu_instr& instr, EAluOp op, unsigned nc, bool all);
   bool emit_any_all_fcomp2(const nir_alu_instr& instr, EAluOp op, bool all);

   bool emit_fdph(const nir_alu_instr &instr);
   bool emit_discard_if(const nir_intrinsic_instr *instr);

   bool emit_find_msb(const nir_alu_instr& instr, bool sgn);
   bool emit_alu_f2b32(const nir_alu_instr& instr);
   bool emit_b2i32(const nir_alu_instr& instr);
   bool emit_alu_f2i32_or_u32(const nir_alu_instr& instr, EAluOp op);
   bool emit_pack_64_2x32_split(const nir_alu_instr& instr);
   bool emit_unpack_64_2x32_split(const nir_alu_instr& instr, unsigned comp);

   bool emit_tex_fdd(const nir_alu_instr& instr, TexInstruction::Opcode op, bool fine);
   bool emit_bitfield_extract(const nir_alu_instr& instr, EAluOp opcode);
   bool emit_bitfield_insert(const nir_alu_instr& instr);
   bool emit_unpack_32_2x16_split_y(const nir_alu_instr& instr);
   bool emit_unpack_32_2x16_split_x(const nir_alu_instr& instr);
   bool emit_pack_32_2x16_split(const nir_alu_instr& instr);

private:
   void make_last(AluInstruction *ir) const;
   void split_alu_modifiers(const nir_alu_src &src, const GPRVector::Values& v,
                            GPRVector::Values& out, int ncomp);

   void preload_src(const nir_alu_instr& instr);
   unsigned num_src_comp(const nir_alu_instr& instr);

   using vreg = std::array<PValue, 4>;

   std::array<PValue, 4> m_src[4];
};

inline void EmitAluInstruction::make_last(AluInstruction *ir) const
{
   if (ir)
      ir->set_flag(alu_last_instr);
}

}

#endif // SFN_EMITALUINSTRUCTION_H
