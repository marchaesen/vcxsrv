/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2019 Collabora LTD
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

#ifndef sfn_r600_instruction_alu_h
#define sfn_r600_instruction_alu_h

#include "sfn_instruction_base.h"
#include "sfn_alu_defines.h"

namespace r600 {

enum AluModifiers {
   alu_src0_neg,
   alu_src0_abs,
   alu_src0_rel,
   alu_src1_neg,
   alu_src1_abs,
   alu_src1_rel,
   alu_src2_neg,
   alu_src2_rel,
   alu_dst_clamp,
   alu_dst_rel,
   alu_last_instr,
   alu_update_exec,
   alu_update_pred,
   alu_write,
   alu_op3
};

enum AluDstModifiers {
   omod_off = 0,
   omod_mul2 = 1,
   omod_mul4 = 2,
   omod_divl2 = 3
};

enum AluPredSel {
   pred_off = 0,
   pred_zero = 2,
   pred_one = 3
};

enum AluBankSwizzle {
   alu_vec_012 = 0,
   sq_alu_scl_201 = 0,
   alu_vec_021 = 1,
   sq_alu_scl_122 = 1,
   alu_vec_120 = 2,
   sq_alu_scl_212 = 2,
   alu_vec_102 = 3,
   sq_alu_scl_221 = 3,
   alu_vec_201 = 4,
   alu_vec_210 = 5,
   alu_vec_unknown = 6
};

class AluInstruction : public Instruction {
public:

   static const AluModifiers src_abs_flags[2];
   static const AluModifiers src_neg_flags[3];
   static const AluModifiers src_rel_flags[3];

   AluInstruction(EAluOp opcode);
   AluInstruction(EAluOp opcode, PValue dest,
                  std::vector<PValue> src0,
                  const std::set<AluModifiers>& m_flags);

   AluInstruction(EAluOp opcode, PValue dest, PValue src0,
                  const std::set<AluModifiers>& m_flags);

   AluInstruction(EAluOp opcode, PValue dest,
                  PValue src0, PValue src1,
                  const std::set<AluModifiers>& m_flags);

   AluInstruction(EAluOp opcode, PValue dest, PValue src0, PValue src1,
                  PValue src2,
                  const std::set<AluModifiers>& m_flags);


   void set_flag(AluModifiers flag);
   unsigned n_sources() const;

   PValue dest() {return m_dest;}
   EAluOp opcode() const {return m_opcode;}
   const Value *dest() const {return m_dest.get();}
   Value& src(unsigned i) const {assert(i < m_src.size() && m_src[i]); return *m_src[i];}
   PValue *psrc(unsigned i) {assert(i < m_src.size()); return &m_src[i];}
   bool is_last() const {return m_flags.test(alu_last_instr);}
   bool write() const {return m_flags.test(alu_write);}
   bool flag(AluModifiers f) const {return m_flags.test(f);}
   void set_bank_swizzle(AluBankSwizzle swz);
   int bank_swizzle() const {return m_bank_swizzle;}
   ECFAluOpCode cf_type() const {return m_cf_type;}
   void set_cf_type(ECFAluOpCode cf_type){ m_cf_type = cf_type; }

   void replace_values(const ValueSet& candidates, PValue new_value) override;

private:

   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;
   PValue remap_one_registers(PValue reg, std::vector<rename_reg_pair>& map,
                              ValueMap &values);


   EAluOp m_opcode;
   PValue m_dest;
   std::vector<PValue> m_src;
   AluOpFlags m_flags;
   AluDstModifiers m_omod;
   AluPredSel m_pred_sel;
   AluBankSwizzle m_bank_swizzle;
   ECFAluOpCode m_cf_type;
};

}

#endif
