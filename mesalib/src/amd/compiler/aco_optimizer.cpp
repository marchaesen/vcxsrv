/*
 * Copyright © 2018 Valve Corporation
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
 * Authors:
 *    Daniel Schürmann (daniel.schuermann@campus.tu-berlin.de)
 *
 */

#include <algorithm>
#include <math.h>

#include "aco_ir.h"
#include "util/half_float.h"
#include "util/u_math.h"

namespace aco {

/**
 * The optimizer works in 4 phases:
 * (1) The first pass collects information for each ssa-def,
 *     propagates reg->reg operands of the same type, inline constants
 *     and neg/abs input modifiers.
 * (2) The second pass combines instructions like mad, omod, clamp and
 *     propagates sgpr's on VALU instructions.
 *     This pass depends on information collected in the first pass.
 * (3) The third pass goes backwards, and selects instructions,
 *     i.e. decides if a mad instruction is profitable and eliminates dead code.
 * (4) The fourth pass cleans up the sequence: literals get applied and dead
 *     instructions are removed from the sequence.
 */


struct mad_info {
   aco_ptr<Instruction> add_instr;
   uint32_t mul_temp_id;
   uint16_t literal_idx;
   bool check_literal;

   mad_info(aco_ptr<Instruction> instr, uint32_t id)
   : add_instr(std::move(instr)), mul_temp_id(id), check_literal(false) {}
};

enum Label {
   label_vec = 1 << 0,
   label_constant_32bit = 1 << 1,
   /* label_{abs,neg,mul,omod2,omod4,omod5,clamp} are used for both 16 and
    * 32-bit operations but this shouldn't cause any issues because we don't
    * look through any conversions */
   label_abs = 1 << 2,
   label_neg = 1 << 3,
   label_mul = 1 << 4,
   label_temp = 1 << 5,
   label_literal = 1 << 6,
   label_mad = 1 << 7,
   label_omod2 = 1 << 8,
   label_omod4 = 1 << 9,
   label_omod5 = 1 << 10,
   label_omod_success = 1 << 11,
   label_clamp = 1 << 12,
   label_clamp_success = 1 << 13,
   label_undefined = 1 << 14,
   label_vcc = 1 << 15,
   label_b2f = 1 << 16,
   label_add_sub = 1 << 17,
   label_bitwise = 1 << 18,
   label_minmax = 1 << 19,
   label_fcmp = 1 << 20,
   label_uniform_bool = 1 << 21,
   label_constant_64bit = 1 << 22,
   label_uniform_bitwise = 1 << 23,
   label_scc_invert = 1 << 24,
   label_vcc_hint = 1 << 25,
   label_scc_needed = 1 << 26,
   label_b2i = 1 << 27,
   label_constant_16bit = 1 << 29,
};

static constexpr uint64_t instr_labels = label_vec | label_mul | label_mad | label_omod_success | label_clamp_success |
                                         label_add_sub | label_bitwise | label_uniform_bitwise | label_minmax | label_fcmp;
static constexpr uint64_t temp_labels = label_abs | label_neg | label_temp | label_vcc | label_b2f | label_uniform_bool |
                                        label_omod2 | label_omod4 | label_omod5 | label_clamp | label_scc_invert | label_b2i;
static constexpr uint32_t val_labels = label_constant_32bit | label_constant_64bit | label_constant_16bit | label_literal;

struct ssa_info {
   uint64_t label;
   union {
      uint32_t val;
      Temp temp;
      Instruction* instr;
   };

   ssa_info() : label(0) {}

   void add_label(Label new_label)
   {
      /* Since all labels which use "instr" use it for the same thing
       * (indicating the defining instruction), there is no need to clear
       * any other instr labels. */
      if (new_label & instr_labels)
         label &= ~(temp_labels | val_labels); /* instr, temp and val alias */

      if (new_label & temp_labels) {
         label &= ~temp_labels;
         label &= ~(instr_labels | val_labels); /* instr, temp and val alias */
      }

      uint32_t const_labels = label_literal | label_constant_32bit | label_constant_64bit | label_constant_16bit;
      if (new_label & const_labels) {
         label &= ~val_labels | const_labels;
         label &= ~(instr_labels | temp_labels); /* instr, temp and val alias */
      } else if (new_label & val_labels) {
         label &= ~val_labels;
         label &= ~(instr_labels | temp_labels); /* instr, temp and val alias */
      }

      label |= new_label;
   }

   void set_vec(Instruction* vec)
   {
      add_label(label_vec);
      instr = vec;
   }

   bool is_vec()
   {
      return label & label_vec;
   }

   void set_constant(chip_class chip, uint64_t constant)
   {
      Operand op16((uint16_t)constant);
      Operand op32((uint32_t)constant);
      add_label(label_literal);
      val = constant;

      if (chip >= GFX8 && !op16.isLiteral())
         add_label(label_constant_16bit);

      if (!op32.isLiteral() || ((uint32_t)constant == 0x3e22f983 && chip >= GFX8))
         add_label(label_constant_32bit);

      if (constant <= 64) {
         add_label(label_constant_64bit);
      } else if (constant >= 0xFFFFFFFFFFFFFFF0) { /* [-16 .. -1] */
         add_label(label_constant_64bit);
      } else if (constant == 0x3FE0000000000000) { /* 0.5 */
         add_label(label_constant_64bit);
      } else if (constant == 0xBFE0000000000000) { /* -0.5 */
         add_label(label_constant_64bit);
      } else if (constant == 0x3FF0000000000000) { /* 1.0 */
         add_label(label_constant_64bit);
      } else if (constant == 0xBFF0000000000000) { /* -1.0 */
         add_label(label_constant_64bit);
      } else if (constant == 0x4000000000000000) { /* 2.0 */
         add_label(label_constant_64bit);
      } else if (constant == 0xC000000000000000) { /* -2.0 */
         add_label(label_constant_64bit);
      } else if (constant == 0x4010000000000000) { /* 4.0 */
         add_label(label_constant_64bit);
      } else if (constant == 0xC010000000000000) { /* -4.0 */
         add_label(label_constant_64bit);
      }

      if (label & label_constant_64bit) {
         val = Operand(constant).constantValue();
         if (val != constant)
            label &= ~(label_literal | label_constant_16bit | label_constant_32bit);
      }
   }

   bool is_constant(unsigned bits)
   {
      switch (bits) {
      case 8:
         return label & label_literal;
      case 16:
         return label & label_constant_16bit;
      case 32:
         return label & label_constant_32bit;
      case 64:
         return label & label_constant_64bit;
      }
      return false;
   }

   bool is_literal(unsigned bits)
   {
      bool is_lit = label & label_literal;
      switch (bits) {
      case 8:
         return false;
      case 16:
         return is_lit && ~(label & label_constant_16bit);
      case 32:
         return is_lit && ~(label & label_constant_32bit);
      case 64:
         return false;
      }
      return false;
   }

   bool is_constant_or_literal(unsigned bits)
   {
      if (bits == 64)
         return label & label_constant_64bit;
      else
         return label & label_literal;
   }

   void set_abs(Temp abs_temp)
   {
      add_label(label_abs);
      temp = abs_temp;
   }

   bool is_abs()
   {
      return label & label_abs;
   }

   void set_neg(Temp neg_temp)
   {
      add_label(label_neg);
      temp = neg_temp;
   }

   bool is_neg()
   {
      return label & label_neg;
   }

   void set_neg_abs(Temp neg_abs_temp)
   {
      add_label((Label)((uint32_t)label_abs | (uint32_t)label_neg));
      temp = neg_abs_temp;
   }

   void set_mul(Instruction* mul)
   {
      add_label(label_mul);
      instr = mul;
   }

   bool is_mul()
   {
      return label & label_mul;
   }

   void set_temp(Temp tmp)
   {
      add_label(label_temp);
      temp = tmp;
   }

   bool is_temp()
   {
      return label & label_temp;
   }

   void set_mad(Instruction* mad, uint32_t mad_info_idx)
   {
      add_label(label_mad);
      mad->pass_flags = mad_info_idx;
      instr = mad;
   }

   bool is_mad()
   {
      return label & label_mad;
   }

   void set_omod2(Temp def)
   {
      add_label(label_omod2);
      temp = def;
   }

   bool is_omod2()
   {
      return label & label_omod2;
   }

   void set_omod4(Temp def)
   {
      add_label(label_omod4);
      temp = def;
   }

   bool is_omod4()
   {
      return label & label_omod4;
   }

   void set_omod5(Temp def)
   {
      add_label(label_omod5);
      temp = def;
   }

   bool is_omod5()
   {
      return label & label_omod5;
   }

   void set_omod_success(Instruction* omod_instr)
   {
      add_label(label_omod_success);
      instr = omod_instr;
   }

   bool is_omod_success()
   {
      return label & label_omod_success;
   }

   void set_clamp(Temp def)
   {
      add_label(label_clamp);
      temp = def;
   }

   bool is_clamp()
   {
      return label & label_clamp;
   }

   void set_clamp_success(Instruction* clamp_instr)
   {
      add_label(label_clamp_success);
      instr = clamp_instr;
   }

   bool is_clamp_success()
   {
      return label & label_clamp_success;
   }

   void set_undefined()
   {
      add_label(label_undefined);
   }

   bool is_undefined()
   {
      return label & label_undefined;
   }

   void set_vcc(Temp vcc)
   {
      add_label(label_vcc);
      temp = vcc;
   }

   bool is_vcc()
   {
      return label & label_vcc;
   }

   void set_b2f(Temp val)
   {
      add_label(label_b2f);
      temp = val;
   }

   bool is_b2f()
   {
      return label & label_b2f;
   }

   void set_add_sub(Instruction *add_sub_instr)
   {
      add_label(label_add_sub);
      instr = add_sub_instr;
   }

   bool is_add_sub()
   {
      return label & label_add_sub;
   }

   void set_bitwise(Instruction *bitwise_instr)
   {
      add_label(label_bitwise);
      instr = bitwise_instr;
   }

   bool is_bitwise()
   {
      return label & label_bitwise;
   }

   void set_uniform_bitwise()
   {
      add_label(label_uniform_bitwise);
   }

   bool is_uniform_bitwise()
   {
      return label & label_uniform_bitwise;
   }

   void set_minmax(Instruction *minmax_instr)
   {
      add_label(label_minmax);
      instr = minmax_instr;
   }

   bool is_minmax()
   {
      return label & label_minmax;
   }

   void set_fcmp(Instruction *fcmp_instr)
   {
      add_label(label_fcmp);
      instr = fcmp_instr;
   }

   bool is_fcmp()
   {
      return label & label_fcmp;
   }

   void set_scc_needed()
   {
      add_label(label_scc_needed);
   }

   bool is_scc_needed()
   {
      return label & label_scc_needed;
   }

   void set_scc_invert(Temp scc_inv)
   {
      add_label(label_scc_invert);
      temp = scc_inv;
   }

   bool is_scc_invert()
   {
      return label & label_scc_invert;
   }

   void set_uniform_bool(Temp uniform_bool)
   {
      add_label(label_uniform_bool);
      temp = uniform_bool;
   }

   bool is_uniform_bool()
   {
      return label & label_uniform_bool;
   }

   void set_vcc_hint()
   {
      add_label(label_vcc_hint);
   }

   bool is_vcc_hint()
   {
      return label & label_vcc_hint;
   }

   void set_b2i(Temp val)
   {
      add_label(label_b2i);
      temp = val;
   }

   bool is_b2i()
   {
      return label & label_b2i;
   }

};

struct opt_ctx {
   Program* program;
   std::vector<aco_ptr<Instruction>> instructions;
   ssa_info* info;
   std::pair<uint32_t,Temp> last_literal;
   std::vector<mad_info> mad_infos;
   std::vector<uint16_t> uses;
};

struct CmpInfo {
   aco_opcode ordered;
   aco_opcode unordered;
   aco_opcode ordered_swapped;
   aco_opcode unordered_swapped;
   aco_opcode inverse;
   aco_opcode f32;
   unsigned size;
};

ALWAYS_INLINE bool get_cmp_info(aco_opcode op, CmpInfo *info);

bool can_swap_operands(aco_ptr<Instruction>& instr)
{
   if (instr->operands[0].isConstant() ||
       (instr->operands[0].isTemp() && instr->operands[0].getTemp().type() == RegType::sgpr))
      return false;

   switch (instr->opcode) {
   case aco_opcode::v_add_f16:
   case aco_opcode::v_add_f32:
   case aco_opcode::v_mul_f16:
   case aco_opcode::v_mul_f32:
   case aco_opcode::v_or_b32:
   case aco_opcode::v_and_b32:
   case aco_opcode::v_xor_b32:
   case aco_opcode::v_max_f16:
   case aco_opcode::v_max_f32:
   case aco_opcode::v_min_f16:
   case aco_opcode::v_min_f32:
   case aco_opcode::v_max_i32:
   case aco_opcode::v_min_i32:
   case aco_opcode::v_max_u32:
   case aco_opcode::v_min_u32:
   case aco_opcode::v_max_i16:
   case aco_opcode::v_min_i16:
   case aco_opcode::v_max_u16:
   case aco_opcode::v_min_u16:
   case aco_opcode::v_max_i16_e64:
   case aco_opcode::v_min_i16_e64:
   case aco_opcode::v_max_u16_e64:
   case aco_opcode::v_min_u16_e64:
      return true;
   case aco_opcode::v_sub_f16:
      instr->opcode = aco_opcode::v_subrev_f16;
      return true;
   case aco_opcode::v_sub_f32:
      instr->opcode = aco_opcode::v_subrev_f32;
      return true;
   case aco_opcode::v_sub_co_u32:
      instr->opcode = aco_opcode::v_subrev_co_u32;
      return true;
   case aco_opcode::v_sub_u16:
      instr->opcode = aco_opcode::v_subrev_u16;
      return true;
   case aco_opcode::v_sub_u32:
      instr->opcode = aco_opcode::v_subrev_u32;
      return true;
   default: {
      CmpInfo info;
      get_cmp_info(instr->opcode, &info);
      if (info.ordered == instr->opcode) {
         instr->opcode = info.ordered_swapped;
         return true;
      }
      if (info.unordered == instr->opcode) {
         instr->opcode = info.unordered_swapped;
         return true;
      }
      return false;
   }
   }
}

bool can_use_VOP3(opt_ctx& ctx, const aco_ptr<Instruction>& instr)
{
   if (instr->isVOP3())
      return true;

   if (instr->operands.size() && instr->operands[0].isLiteral() && ctx.program->chip_class < GFX10)
      return false;

   if (instr->isDPP() || instr->isSDWA())
      return false;

   return instr->opcode != aco_opcode::v_madmk_f32 &&
          instr->opcode != aco_opcode::v_madak_f32 &&
          instr->opcode != aco_opcode::v_madmk_f16 &&
          instr->opcode != aco_opcode::v_madak_f16 &&
          instr->opcode != aco_opcode::v_fmamk_f32 &&
          instr->opcode != aco_opcode::v_fmaak_f32 &&
          instr->opcode != aco_opcode::v_fmamk_f16 &&
          instr->opcode != aco_opcode::v_fmaak_f16 &&
          instr->opcode != aco_opcode::v_readlane_b32 &&
          instr->opcode != aco_opcode::v_writelane_b32 &&
          instr->opcode != aco_opcode::v_readfirstlane_b32;
}

bool can_apply_sgprs(aco_ptr<Instruction>& instr)
{
   return instr->opcode != aco_opcode::v_readfirstlane_b32 &&
          instr->opcode != aco_opcode::v_readlane_b32 &&
          instr->opcode != aco_opcode::v_readlane_b32_e64 &&
          instr->opcode != aco_opcode::v_writelane_b32 &&
          instr->opcode != aco_opcode::v_writelane_b32_e64;
}

void to_VOP3(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->isVOP3())
      return;

   aco_ptr<Instruction> tmp = std::move(instr);
   Format format = asVOP3(tmp->format);
   instr.reset(create_instruction<VOP3A_instruction>(tmp->opcode, format, tmp->operands.size(), tmp->definitions.size()));
   std::copy(tmp->operands.cbegin(), tmp->operands.cend(), instr->operands.begin());
   for (unsigned i = 0; i < instr->definitions.size(); i++) {
      instr->definitions[i] = tmp->definitions[i];
      if (instr->definitions[i].isTemp()) {
         ssa_info& info = ctx.info[instr->definitions[i].tempId()];
         if (info.label & instr_labels && info.instr == tmp.get())
            info.instr = instr.get();
      }
   }
}

/* only covers special cases */
bool alu_can_accept_constant(aco_opcode opcode, unsigned operand)
{
   switch (opcode) {
   case aco_opcode::v_interp_p2_f32:
   case aco_opcode::v_mac_f32:
   case aco_opcode::v_writelane_b32:
   case aco_opcode::v_writelane_b32_e64:
   case aco_opcode::v_cndmask_b32:
      return operand != 2;
   case aco_opcode::s_addk_i32:
   case aco_opcode::s_mulk_i32:
   case aco_opcode::p_wqm:
   case aco_opcode::p_extract_vector:
   case aco_opcode::p_split_vector:
   case aco_opcode::v_readlane_b32:
   case aco_opcode::v_readlane_b32_e64:
   case aco_opcode::v_readfirstlane_b32:
      return operand != 0;
   default:
      return true;
   }
}

bool valu_can_accept_vgpr(aco_ptr<Instruction>& instr, unsigned operand)
{
   if (instr->opcode == aco_opcode::v_readlane_b32 || instr->opcode == aco_opcode::v_readlane_b32_e64 ||
       instr->opcode == aco_opcode::v_writelane_b32 || instr->opcode == aco_opcode::v_writelane_b32_e64)
      return operand != 1;
   return true;
}

/* check constant bus and literal limitations */
bool check_vop3_operands(opt_ctx& ctx, unsigned num_operands, Operand *operands)
{
   int limit = ctx.program->chip_class >= GFX10 ? 2 : 1;
   Operand literal32(s1);
   Operand literal64(s2);
   unsigned num_sgprs = 0;
   unsigned sgpr[] = {0, 0};

   for (unsigned i = 0; i < num_operands; i++) {
      Operand op = operands[i];

      if (op.hasRegClass() && op.regClass().type() == RegType::sgpr) {
         /* two reads of the same SGPR count as 1 to the limit */
         if (op.tempId() != sgpr[0] && op.tempId() != sgpr[1]) {
            if (num_sgprs < 2)
               sgpr[num_sgprs++] = op.tempId();
            limit--;
            if (limit < 0)
               return false;
         }
      } else if (op.isLiteral()) {
         if (ctx.program->chip_class < GFX10)
            return false;

         if (!literal32.isUndefined() && literal32.constantValue() != op.constantValue())
            return false;
         if (!literal64.isUndefined() && literal64.constantValue() != op.constantValue())
            return false;

         /* Any number of 32-bit literals counts as only 1 to the limit. Same
          * (but separately) for 64-bit literals. */
         if (op.size() == 1 && literal32.isUndefined()) {
            limit--;
            literal32 = op;
         } else if (op.size() == 2 && literal64.isUndefined()) {
            limit--;
            literal64 = op;
         }

         if (limit < 0)
            return false;
      }
   }

   return true;
}

bool parse_base_offset(opt_ctx &ctx, Instruction* instr, unsigned op_index, Temp *base, uint32_t *offset)
{
   Operand op = instr->operands[op_index];

   if (!op.isTemp())
      return false;
   Temp tmp = op.getTemp();
   if (!ctx.info[tmp.id()].is_add_sub())
      return false;

   Instruction *add_instr = ctx.info[tmp.id()].instr;

   switch (add_instr->opcode) {
   case aco_opcode::v_add_u32:
   case aco_opcode::v_add_co_u32:
   case aco_opcode::v_add_co_u32_e64:
   case aco_opcode::s_add_i32:
   case aco_opcode::s_add_u32:
      break;
   default:
      return false;
   }

   if (add_instr->usesModifiers())
      return false;

   for (unsigned i = 0; i < 2; i++) {
      if (add_instr->operands[i].isConstant()) {
         *offset = add_instr->operands[i].constantValue();
      } else if (add_instr->operands[i].isTemp() &&
                 ctx.info[add_instr->operands[i].tempId()].is_constant_or_literal(32)) {
         *offset = ctx.info[add_instr->operands[i].tempId()].val;
      } else {
         continue;
      }
      if (!add_instr->operands[!i].isTemp())
         continue;

      uint32_t offset2 = 0;
      if (parse_base_offset(ctx, add_instr, !i, base, &offset2)) {
         *offset += offset2;
      } else {
         *base = add_instr->operands[!i].getTemp();
      }
      return true;
   }

   return false;
}

unsigned get_operand_size(aco_ptr<Instruction>& instr, unsigned index)
{
   if (instr->format == Format::PSEUDO)
      return instr->operands[index].bytes() * 8u;
   else if (instr->opcode == aco_opcode::v_mad_u64_u32 || instr->opcode == aco_opcode::v_mad_i64_i32)
      return index == 2 ? 64 : 32;
   else if (instr->isVALU() || instr->isSALU())
      return instr_info.operand_size[(int)instr->opcode];
   else
      return 0;
}

Operand get_constant_op(opt_ctx &ctx, ssa_info info, uint32_t bits)
{
   if (bits == 8)
      return Operand((uint8_t)info.val);
   if (bits == 16)
      return Operand((uint16_t)info.val);
   // TODO: this functions shouldn't be needed if we store Operand instead of value.
   Operand op(info.val, bits == 64);
   if (info.is_literal(32) && info.val == 0x3e22f983 && ctx.program->chip_class >= GFX8)
      op.setFixed(PhysReg{248}); /* 1/2 PI can be an inline constant on GFX8+ */
   return op;
}

bool fixed_to_exec(Operand op)
{
   return op.isFixed() && op.physReg() == exec;
}

void label_instruction(opt_ctx &ctx, Block& block, aco_ptr<Instruction>& instr)
{
   if (instr->isSALU() || instr->isVALU() || instr->format == Format::PSEUDO) {
      ASSERTED bool all_const = false;
      for (Operand& op : instr->operands)
         all_const = all_const && (!op.isTemp() || ctx.info[op.tempId()].is_constant_or_literal(32));
      perfwarn(all_const, "All instruction operands are constant", instr.get());
   }

   for (unsigned i = 0; i < instr->operands.size(); i++)
   {
      if (!instr->operands[i].isTemp())
         continue;

      ssa_info info = ctx.info[instr->operands[i].tempId()];
      /* propagate undef */
      if (info.is_undefined() && is_phi(instr))
         instr->operands[i] = Operand(instr->operands[i].regClass());
      /* propagate reg->reg of same type */
      if (info.is_temp() && info.temp.regClass() == instr->operands[i].getTemp().regClass()) {
         instr->operands[i].setTemp(ctx.info[instr->operands[i].tempId()].temp);
         info = ctx.info[info.temp.id()];
      }

      /* SALU / PSEUDO: propagate inline constants */
      if (instr->isSALU() || instr->format == Format::PSEUDO) {
         bool is_subdword = false;
         // TODO: optimize SGPR propagation for subdword pseudo instructions on gfx9+
         if (instr->format == Format::PSEUDO) {
            is_subdword = std::any_of(instr->definitions.begin(), instr->definitions.end(),
                                      [] (const Definition& def) { return def.regClass().is_subdword();});
            is_subdword = is_subdword || std::any_of(instr->operands.begin(), instr->operands.end(),
                                                     [] (const Operand& op) { return op.hasRegClass() && op.regClass().is_subdword();});
            if (is_subdword && ctx.program->chip_class < GFX9)
               continue;
         }

         if (info.is_temp() && info.temp.type() == RegType::sgpr) {
            instr->operands[i].setTemp(info.temp);
            info = ctx.info[info.temp.id()];
         } else if (info.is_temp() && info.temp.type() == RegType::vgpr) {
            /* propagate vgpr if it can take it */
            switch (instr->opcode) {
            case aco_opcode::p_create_vector:
            case aco_opcode::p_split_vector:
            case aco_opcode::p_extract_vector:
            case aco_opcode::p_phi: {
               const bool all_vgpr = std::none_of(instr->definitions.begin(), instr->definitions.end(),
                                                  [] (const Definition& def) { return def.getTemp().type() != RegType::vgpr;});
               if (all_vgpr) {
                  instr->operands[i] = Operand(info.temp);
                  info = ctx.info[info.temp.id()];
               }
               break;
            }
            default:
               break;
            }
         }
         unsigned bits = get_operand_size(instr, i);
         if ((info.is_constant(bits) || (!is_subdword && info.is_literal(bits) && instr->format == Format::PSEUDO)) &&
             !instr->operands[i].isFixed() && alu_can_accept_constant(instr->opcode, i)) {
            instr->operands[i] = get_constant_op(ctx, info, bits);
            continue;
         }
      }

      /* VALU: propagate neg, abs & inline constants */
      else if (instr->isVALU()) {
         if (info.is_temp() && info.temp.type() == RegType::vgpr && valu_can_accept_vgpr(instr, i)) {
            instr->operands[i].setTemp(info.temp);
            info = ctx.info[info.temp.id()];
         }

         /* for instructions other than v_cndmask_b32, the size of the instruction should match the operand size */
         unsigned can_use_mod = instr->opcode != aco_opcode::v_cndmask_b32 || instr->operands[i].getTemp().bytes() == 4;
         can_use_mod = can_use_mod && instr_info.can_use_input_modifiers[(int)instr->opcode];

         if (info.is_abs() && (can_use_VOP3(ctx, instr) || instr->isDPP()) && can_use_mod) {
            if (!instr->isDPP())
               to_VOP3(ctx, instr);
            instr->operands[i] = Operand(info.temp);
            if (instr->isDPP())
               static_cast<DPP_instruction*>(instr.get())->abs[i] = true;
            else
               static_cast<VOP3A_instruction*>(instr.get())->abs[i] = true;
         }
         if (info.is_neg() && instr->opcode == aco_opcode::v_add_f32) {
            instr->opcode = i ? aco_opcode::v_sub_f32 : aco_opcode::v_subrev_f32;
            instr->operands[i].setTemp(info.temp);
            continue;
         } else if (info.is_neg() && instr->opcode == aco_opcode::v_add_f16) {
            instr->opcode = i ? aco_opcode::v_sub_f16 : aco_opcode::v_subrev_f16;
            instr->operands[i].setTemp(info.temp);
            continue;
         } else if (info.is_neg() && (can_use_VOP3(ctx, instr) || instr->isDPP()) && can_use_mod) {
            if (!instr->isDPP())
               to_VOP3(ctx, instr);
            instr->operands[i].setTemp(info.temp);
            if (instr->isDPP())
               static_cast<DPP_instruction*>(instr.get())->neg[i] = true;
            else
               static_cast<VOP3A_instruction*>(instr.get())->neg[i] = true;
            continue;
         }
         unsigned bits = get_operand_size(instr, i);
         if (info.is_constant(bits) && alu_can_accept_constant(instr->opcode, i)) {
            Operand op = get_constant_op(ctx, info, bits);
            perfwarn(instr->opcode == aco_opcode::v_cndmask_b32 && i == 2, "v_cndmask_b32 with a constant selector", instr.get());
            if (i == 0 || instr->opcode == aco_opcode::v_readlane_b32 || instr->opcode == aco_opcode::v_writelane_b32) {
               instr->operands[i] = op;
               continue;
            } else if (!instr->isVOP3() && can_swap_operands(instr)) {
               instr->operands[i] = instr->operands[0];
               instr->operands[0] = op;
               continue;
            } else if (can_use_VOP3(ctx, instr)) {
               to_VOP3(ctx, instr);
               instr->operands[i] = op;
               continue;
            }
         }
      }

      /* MUBUF: propagate constants and combine additions */
      else if (instr->format == Format::MUBUF) {
         MUBUF_instruction *mubuf = static_cast<MUBUF_instruction *>(instr.get());
         Temp base;
         uint32_t offset;
         while (info.is_temp())
            info = ctx.info[info.temp.id()];

         if (mubuf->offen && i == 1 && info.is_constant_or_literal(32) && mubuf->offset + info.val < 4096) {
            assert(!mubuf->idxen);
            instr->operands[1] = Operand(v1);
            mubuf->offset += info.val;
            mubuf->offen = false;
            continue;
         } else if (i == 2 && info.is_constant_or_literal(32) && mubuf->offset + info.val < 4096) {
            instr->operands[2] = Operand((uint32_t) 0);
            mubuf->offset += info.val;
            continue;
         } else if (mubuf->offen && i == 1 && parse_base_offset(ctx, instr.get(), i, &base, &offset) && base.regClass() == v1 && mubuf->offset + offset < 4096) {
            assert(!mubuf->idxen);
            instr->operands[1].setTemp(base);
            mubuf->offset += offset;
            continue;
         } else if (i == 2 && parse_base_offset(ctx, instr.get(), i, &base, &offset) && base.regClass() == s1 && mubuf->offset + offset < 4096) {
            instr->operands[i].setTemp(base);
            mubuf->offset += offset;
            continue;
         }
      }

      /* DS: combine additions */
      else if (instr->format == Format::DS) {

         DS_instruction *ds = static_cast<DS_instruction *>(instr.get());
         Temp base;
         uint32_t offset;
         bool has_usable_ds_offset = ctx.program->chip_class >= GFX7;
         if (has_usable_ds_offset &&
             i == 0 && parse_base_offset(ctx, instr.get(), i, &base, &offset) &&
             base.regClass() == instr->operands[i].regClass() &&
             instr->opcode != aco_opcode::ds_swizzle_b32) {
            if (instr->opcode == aco_opcode::ds_write2_b32 || instr->opcode == aco_opcode::ds_read2_b32 ||
                instr->opcode == aco_opcode::ds_write2_b64 || instr->opcode == aco_opcode::ds_read2_b64) {
               unsigned mask = (instr->opcode == aco_opcode::ds_write2_b64 || instr->opcode == aco_opcode::ds_read2_b64) ? 0x7 : 0x3;
               unsigned shifts = (instr->opcode == aco_opcode::ds_write2_b64 || instr->opcode == aco_opcode::ds_read2_b64) ? 3 : 2;

               if ((offset & mask) == 0 &&
                   ds->offset0 + (offset >> shifts) <= 255 &&
                   ds->offset1 + (offset >> shifts) <= 255) {
                  instr->operands[i].setTemp(base);
                  ds->offset0 += offset >> shifts;
                  ds->offset1 += offset >> shifts;
               }
            } else {
               if (ds->offset0 + offset <= 65535) {
                  instr->operands[i].setTemp(base);
                  ds->offset0 += offset;
               }
            }
         }
      }

      /* SMEM: propagate constants and combine additions */
      else if (instr->format == Format::SMEM) {

         SMEM_instruction *smem = static_cast<SMEM_instruction *>(instr.get());
         Temp base;
         uint32_t offset;
         if (i == 1 && info.is_constant_or_literal(32) &&
             ((ctx.program->chip_class == GFX6 && info.val <= 0x3FF) ||
              (ctx.program->chip_class == GFX7 && info.val <= 0xFFFFFFFF) ||
              (ctx.program->chip_class >= GFX8 && info.val <= 0xFFFFF))) {
            instr->operands[i] = Operand(info.val);
            continue;
         } else if (i == 1 && parse_base_offset(ctx, instr.get(), i, &base, &offset) && base.regClass() == s1 && offset <= 0xFFFFF && ctx.program->chip_class >= GFX9) {
            bool soe = smem->operands.size() >= (!smem->definitions.empty() ? 3 : 4);
            if (soe &&
                (!ctx.info[smem->operands.back().tempId()].is_constant_or_literal(32) ||
                 ctx.info[smem->operands.back().tempId()].val != 0)) {
               continue;
            }
            if (soe) {
               smem->operands[1] = Operand(offset);
               smem->operands.back() = Operand(base);
            } else {
               SMEM_instruction *new_instr = create_instruction<SMEM_instruction>(smem->opcode, Format::SMEM, smem->operands.size() + 1, smem->definitions.size());
               new_instr->operands[0] = smem->operands[0];
               new_instr->operands[1] = Operand(offset);
               if (smem->definitions.empty())
                  new_instr->operands[2] = smem->operands[2];
               new_instr->operands.back() = Operand(base);
               if (!smem->definitions.empty())
                  new_instr->definitions[0] = smem->definitions[0];
               new_instr->can_reorder = smem->can_reorder;
               new_instr->barrier = smem->barrier;
               new_instr->glc = smem->glc;
               new_instr->dlc = smem->dlc;
               new_instr->nv = smem->nv;
               new_instr->disable_wqm = smem->disable_wqm;
               instr.reset(new_instr);
               smem = static_cast<SMEM_instruction *>(instr.get());
            }
            continue;
         }
      }

      else if (instr->format == Format::PSEUDO_BRANCH) {
         if (ctx.info[instr->operands[0].tempId()].is_scc_invert()) {
            /* Flip the branch instruction to get rid of the scc_invert instruction */
            instr->opcode = instr->opcode == aco_opcode::p_cbranch_z ? aco_opcode::p_cbranch_nz : aco_opcode::p_cbranch_z;
            instr->operands[0].setTemp(ctx.info[instr->operands[0].tempId()].temp);
         }
      }
   }

   /* if this instruction doesn't define anything, return */
   if (instr->definitions.empty())
      return;

   switch (instr->opcode) {
   case aco_opcode::p_create_vector: {
      bool copy_prop = instr->operands.size() == 1 && instr->operands[0].isTemp() &&
                       instr->operands[0].regClass() == instr->definitions[0].regClass();
      if (copy_prop) {
         ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
         break;
      }

      unsigned num_ops = instr->operands.size();
      for (const Operand& op : instr->operands) {
         if (op.isTemp() && ctx.info[op.tempId()].is_vec())
            num_ops += ctx.info[op.tempId()].instr->operands.size() - 1;
      }
      if (num_ops != instr->operands.size()) {
         aco_ptr<Instruction> old_vec = std::move(instr);
         instr.reset(create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, num_ops, 1));
         instr->definitions[0] = old_vec->definitions[0];
         unsigned k = 0;
         for (Operand& old_op : old_vec->operands) {
            if (old_op.isTemp() && ctx.info[old_op.tempId()].is_vec()) {
               for (unsigned j = 0; j < ctx.info[old_op.tempId()].instr->operands.size(); j++) {
                  Operand op = ctx.info[old_op.tempId()].instr->operands[j];
                  if (op.isTemp() && ctx.info[op.tempId()].is_temp() &&
                      ctx.info[op.tempId()].temp.type() == instr->definitions[0].regClass().type())
                     op.setTemp(ctx.info[op.tempId()].temp);
                  instr->operands[k++] = op;
               }
            } else {
               instr->operands[k++] = old_op;
            }
         }
         assert(k == num_ops);
      }

      ctx.info[instr->definitions[0].tempId()].set_vec(instr.get());
      break;
   }
   case aco_opcode::p_split_vector: {
      ssa_info& info = ctx.info[instr->operands[0].tempId()];

      if (info.is_constant_or_literal(32)) {
         uint32_t val = info.val;
         for (Definition def : instr->definitions) {
            uint32_t mask = u_bit_consecutive(0, def.bytes() * 8u);
            ctx.info[def.tempId()].set_constant(ctx.program->chip_class, val & mask);
            val >>= def.bytes() * 8u;
         }
         break;
      } else if (!info.is_vec()) {
         break;
      }

      Instruction* vec = ctx.info[instr->operands[0].tempId()].instr;
      unsigned split_offset = 0;
      unsigned vec_offset = 0;
      unsigned vec_index = 0;
      for (unsigned i = 0; i < instr->definitions.size(); split_offset += instr->definitions[i++].bytes()) {
         while (vec_offset < split_offset && vec_index < vec->operands.size())
            vec_offset += vec->operands[vec_index++].bytes();

         if (vec_offset != split_offset || vec->operands[vec_index].bytes() != instr->definitions[i].bytes())
            continue;

         Operand vec_op = vec->operands[vec_index];
         if (vec_op.isConstant()) {
            ctx.info[instr->definitions[i].tempId()].set_constant(ctx.program->chip_class, vec_op.constantValue64());
         } else if (vec_op.isUndefined()) {
            ctx.info[instr->definitions[i].tempId()].set_undefined();
         } else {
            assert(vec_op.isTemp());
            ctx.info[instr->definitions[i].tempId()].set_temp(vec_op.getTemp());
         }
      }
      break;
   }
   case aco_opcode::p_extract_vector: { /* mov */
      ssa_info& info = ctx.info[instr->operands[0].tempId()];
      const unsigned index = instr->operands[1].constantValue();
      const unsigned dst_offset = index * instr->definitions[0].bytes();

      if (info.is_constant_or_literal(32)) {
         uint32_t mask = u_bit_consecutive(0, instr->definitions[0].bytes() * 8u);
         ctx.info[instr->definitions[0].tempId()].set_constant(ctx.program->chip_class, (info.val >> (dst_offset * 8u)) & mask);
         break;
      } else if (!info.is_vec()) {
         break;
      }

      /* check if we index directly into a vector element */
      Instruction* vec = info.instr;
      unsigned offset = 0;

      for (const Operand& op : vec->operands) {
         if (offset < dst_offset) {
            offset += op.bytes();
            continue;
         } else if (offset != dst_offset || op.bytes() != instr->definitions[0].bytes()) {
            break;
         }

         /* convert this extract into a copy instruction */
         instr->opcode = aco_opcode::p_parallelcopy;
         instr->operands.pop_back();
         instr->operands[0] = op;

         if (op.isConstant()) {
            ctx.info[instr->definitions[0].tempId()].set_constant(ctx.program->chip_class, op.constantValue64());
         } else if (op.isUndefined()) {
            ctx.info[instr->definitions[0].tempId()].set_undefined();
         } else {
            assert(op.isTemp());
            ctx.info[instr->definitions[0].tempId()].set_temp(op.getTemp());
         }
         break;
      }
      break;
   }
   case aco_opcode::s_mov_b32: /* propagate */
   case aco_opcode::s_mov_b64:
   case aco_opcode::v_mov_b32:
   case aco_opcode::p_as_uniform:
      if (instr->definitions[0].isFixed()) {
         /* don't copy-propagate copies into fixed registers */
      } else if (instr->usesModifiers()) {
         // TODO
      } else if (instr->operands[0].isConstant()) {
         ctx.info[instr->definitions[0].tempId()].set_constant(ctx.program->chip_class, instr->operands[0].constantValue64());
      } else if (instr->operands[0].isTemp()) {
         ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
      } else {
         assert(instr->operands[0].isFixed());
      }
      break;
   case aco_opcode::p_is_helper:
      if (!ctx.program->needs_wqm)
         ctx.info[instr->definitions[0].tempId()].set_constant(ctx.program->chip_class, 0u);
      break;
   case aco_opcode::s_movk_i32: {
      uint32_t v = static_cast<SOPK_instruction*>(instr.get())->imm;
      v = v & 0x8000 ? (v | 0xffff0000) : v;
      ctx.info[instr->definitions[0].tempId()].set_constant(ctx.program->chip_class, v);
      break;
   }
   case aco_opcode::v_bfrev_b32:
   case aco_opcode::s_brev_b32: {
      if (instr->operands[0].isConstant()) {
         uint32_t v = util_bitreverse(instr->operands[0].constantValue());
         ctx.info[instr->definitions[0].tempId()].set_constant(ctx.program->chip_class, v);
      }
      break;
   }
   case aco_opcode::s_bfm_b32: {
      if (instr->operands[0].isConstant() && instr->operands[1].isConstant()) {
         unsigned size = instr->operands[0].constantValue() & 0x1f;
         unsigned start = instr->operands[1].constantValue() & 0x1f;
         uint32_t v = ((1u << size) - 1u) << start;
         ctx.info[instr->definitions[0].tempId()].set_constant(ctx.program->chip_class, v);
      }
      break;
   }
   case aco_opcode::v_mul_f16:
   case aco_opcode::v_mul_f32: { /* omod */
      /* TODO: try to move the negate/abs modifier to the consumer instead */
      if (instr->usesModifiers())
         break;

      bool fp16 = instr->opcode == aco_opcode::v_mul_f16;

      for (unsigned i = 0; i < 2; i++) {
         if (instr->operands[!i].isConstant() && instr->operands[i].isTemp()) {
            if (instr->operands[!i].constantValue() == (fp16 ? 0x4000 : 0x40000000)) { /* 2.0 */
               ctx.info[instr->operands[i].tempId()].set_omod2(instr->definitions[0].getTemp());
            } else if (instr->operands[!i].constantValue() == (fp16 ? 0x4400 : 0x40800000)) { /* 4.0 */
               ctx.info[instr->operands[i].tempId()].set_omod4(instr->definitions[0].getTemp());
            } else if (instr->operands[!i].constantValue() == (fp16 ? 0xb800 : 0x3f000000)) { /* 0.5 */
               ctx.info[instr->operands[i].tempId()].set_omod5(instr->definitions[0].getTemp());
            } else if (instr->operands[!i].constantValue() == (fp16 ? 0x3c00 : 0x3f800000) &&
                       !(fp16 ? block.fp_mode.must_flush_denorms16_64 : block.fp_mode.must_flush_denorms32)) { /* 1.0 */
               ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[i].getTemp());
            } else {
               continue;
            }
            break;
         }
      }
      break;
   }
   case aco_opcode::v_and_b32: { /* abs */
      if (!instr->usesModifiers() && instr->operands[1].isTemp() &&
          instr->operands[1].getTemp().type() == RegType::vgpr &&
          ((instr->definitions[0].bytes() == 4 && instr->operands[0].constantEquals(0x7FFFFFFFu)) ||
           (instr->definitions[0].bytes() == 2 && instr->operands[0].constantEquals(0x7FFFu))))
         ctx.info[instr->definitions[0].tempId()].set_abs(instr->operands[1].getTemp());
      else
         ctx.info[instr->definitions[0].tempId()].set_bitwise(instr.get());
      break;
   }
   case aco_opcode::v_xor_b32: { /* neg */
      if (!instr->usesModifiers() && instr->operands[1].isTemp() &&
          ((instr->definitions[0].bytes() == 4 && instr->operands[0].constantEquals(0x80000000u)) ||
           (instr->definitions[0].bytes() == 2 && instr->operands[0].constantEquals(0x8000u)))) {
         if (ctx.info[instr->operands[1].tempId()].is_neg()) {
            ctx.info[instr->definitions[0].tempId()].set_temp(ctx.info[instr->operands[1].tempId()].temp);
         } else if (instr->operands[1].getTemp().type() == RegType::vgpr) {
            if (ctx.info[instr->operands[1].tempId()].is_abs()) { /* neg(abs(x)) */
               instr->operands[1].setTemp(ctx.info[instr->operands[1].tempId()].temp);
               instr->opcode = aco_opcode::v_or_b32;
               ctx.info[instr->definitions[0].tempId()].set_neg_abs(instr->operands[1].getTemp());
            } else {
               ctx.info[instr->definitions[0].tempId()].set_neg(instr->operands[1].getTemp());
            }
         }
      } else {
         ctx.info[instr->definitions[0].tempId()].set_bitwise(instr.get());
      }
      break;
   }
   case aco_opcode::v_med3_f16:
   case aco_opcode::v_med3_f32: { /* clamp */
      VOP3A_instruction* vop3 = static_cast<VOP3A_instruction*>(instr.get());
      if (vop3->abs[0] || vop3->abs[1] || vop3->abs[2] ||
          vop3->neg[0] || vop3->neg[1] || vop3->neg[2] ||
          vop3->omod != 0 || vop3->opsel != 0)
         break;

      unsigned idx = 0;
      bool found_zero = false, found_one = false;
      bool is_fp16 = instr->opcode == aco_opcode::v_med3_f16;
      for (unsigned i = 0; i < 3; i++)
      {
         if (instr->operands[i].constantEquals(0))
            found_zero = true;
         else if (instr->operands[i].constantEquals(is_fp16 ? 0x3c00 : 0x3f800000)) /* 1.0 */
            found_one = true;
         else
            idx = i;
      }
      if (found_zero && found_one && instr->operands[idx].isTemp()) {
         ctx.info[instr->operands[idx].tempId()].set_clamp(instr->definitions[0].getTemp());
      }
      break;
   }
   case aco_opcode::v_cndmask_b32:
      if (instr->operands[0].constantEquals(0) &&
          instr->operands[1].constantEquals(0xFFFFFFFF))
         ctx.info[instr->definitions[0].tempId()].set_vcc(instr->operands[2].getTemp());
      else if (instr->operands[0].constantEquals(0) &&
               instr->operands[1].constantEquals(0x3f800000u))
         ctx.info[instr->definitions[0].tempId()].set_b2f(instr->operands[2].getTemp());
      else if (instr->operands[0].constantEquals(0) &&
               instr->operands[1].constantEquals(1))
         ctx.info[instr->definitions[0].tempId()].set_b2i(instr->operands[2].getTemp());

      ctx.info[instr->operands[2].tempId()].set_vcc_hint();
      break;
   case aco_opcode::v_cmp_lg_u32:
      if (instr->format == Format::VOPC && /* don't optimize VOP3 / SDWA / DPP */
          instr->operands[0].constantEquals(0) &&
          instr->operands[1].isTemp() && ctx.info[instr->operands[1].tempId()].is_vcc())
         ctx.info[instr->definitions[0].tempId()].set_temp(ctx.info[instr->operands[1].tempId()].temp);
      break;
   case aco_opcode::p_phi:
   case aco_opcode::p_linear_phi: {
      /* lower_bool_phis() can create phis like this */
      bool all_same_temp = instr->operands[0].isTemp();
      /* this check is needed when moving uniform loop counters out of a divergent loop */
      if (all_same_temp)
         all_same_temp = instr->definitions[0].regClass() == instr->operands[0].regClass();
      for (unsigned i = 1; all_same_temp && (i < instr->operands.size()); i++) {
         if (!instr->operands[i].isTemp() || instr->operands[i].tempId() != instr->operands[0].tempId())
            all_same_temp = false;
      }
      if (all_same_temp) {
         ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
      } else {
         bool all_undef = instr->operands[0].isUndefined();
         for (unsigned i = 1; all_undef && (i < instr->operands.size()); i++) {
            if (!instr->operands[i].isUndefined())
               all_undef = false;
         }
         if (all_undef)
            ctx.info[instr->definitions[0].tempId()].set_undefined();
      }
      break;
   }
   case aco_opcode::v_add_u32:
   case aco_opcode::v_add_co_u32:
   case aco_opcode::v_add_co_u32_e64:
   case aco_opcode::s_add_i32:
   case aco_opcode::s_add_u32:
      ctx.info[instr->definitions[0].tempId()].set_add_sub(instr.get());
      break;
   case aco_opcode::s_not_b32:
   case aco_opcode::s_not_b64:
      if (ctx.info[instr->operands[0].tempId()].is_uniform_bool()) {
         ctx.info[instr->definitions[0].tempId()].set_uniform_bitwise();
         ctx.info[instr->definitions[1].tempId()].set_scc_invert(ctx.info[instr->operands[0].tempId()].temp);
      } else if (ctx.info[instr->operands[0].tempId()].is_uniform_bitwise()) {
         ctx.info[instr->definitions[0].tempId()].set_uniform_bitwise();
         ctx.info[instr->definitions[1].tempId()].set_scc_invert(ctx.info[instr->operands[0].tempId()].instr->definitions[1].getTemp());
      }
      ctx.info[instr->definitions[0].tempId()].set_bitwise(instr.get());
      break;
   case aco_opcode::s_and_b32:
   case aco_opcode::s_and_b64:
      if (fixed_to_exec(instr->operands[1]) && instr->operands[0].isTemp()) {
         if (ctx.info[instr->operands[0].tempId()].is_uniform_bool()) {
            /* Try to get rid of the superfluous s_cselect + s_and_b64 that comes from turning a uniform bool into divergent */
            ctx.info[instr->definitions[1].tempId()].set_temp(ctx.info[instr->operands[0].tempId()].temp);
            ctx.info[instr->definitions[0].tempId()].set_uniform_bool(ctx.info[instr->operands[0].tempId()].temp);
            break;
         } else if (ctx.info[instr->operands[0].tempId()].is_uniform_bitwise()) {
            /* Try to get rid of the superfluous s_and_b64, since the uniform bitwise instruction already produces the same SCC */
            ctx.info[instr->definitions[1].tempId()].set_temp(ctx.info[instr->operands[0].tempId()].instr->definitions[1].getTemp());
            ctx.info[instr->definitions[0].tempId()].set_uniform_bool(ctx.info[instr->operands[0].tempId()].instr->definitions[1].getTemp());
            break;
         }
      }
      /* fallthrough */
   case aco_opcode::s_or_b32:
   case aco_opcode::s_or_b64:
   case aco_opcode::s_xor_b32:
   case aco_opcode::s_xor_b64:
      if (std::all_of(instr->operands.begin(), instr->operands.end(), [&ctx](const Operand& op) {
                         return op.isTemp() && (ctx.info[op.tempId()].is_uniform_bool() || ctx.info[op.tempId()].is_uniform_bitwise());
                      })) {
         ctx.info[instr->definitions[0].tempId()].set_uniform_bitwise();
      }
      /* fallthrough */
   case aco_opcode::s_lshl_b32:
   case aco_opcode::v_or_b32:
   case aco_opcode::v_lshlrev_b32:
      ctx.info[instr->definitions[0].tempId()].set_bitwise(instr.get());
      break;
   case aco_opcode::v_min_f32:
   case aco_opcode::v_min_f16:
   case aco_opcode::v_min_u32:
   case aco_opcode::v_min_i32:
   case aco_opcode::v_min_u16:
   case aco_opcode::v_min_i16:
   case aco_opcode::v_max_f32:
   case aco_opcode::v_max_f16:
   case aco_opcode::v_max_u32:
   case aco_opcode::v_max_i32:
   case aco_opcode::v_max_u16:
   case aco_opcode::v_max_i16:
      ctx.info[instr->definitions[0].tempId()].set_minmax(instr.get());
      break;
   #define CMP(cmp) \
   case aco_opcode::v_cmp_##cmp##_f16:\
   case aco_opcode::v_cmp_##cmp##_f32:\
   case aco_opcode::v_cmp_##cmp##_f64:\
   case aco_opcode::v_cmp_n##cmp##_f16:\
   case aco_opcode::v_cmp_n##cmp##_f32:\
   case aco_opcode::v_cmp_n##cmp##_f64:
   CMP(lt)
   CMP(eq)
   CMP(le)
   CMP(gt)
   CMP(lg)
   CMP(ge)
   case aco_opcode::v_cmp_o_f16:
   case aco_opcode::v_cmp_u_f16:
   case aco_opcode::v_cmp_o_f32:
   case aco_opcode::v_cmp_u_f32:
   case aco_opcode::v_cmp_o_f64:
   case aco_opcode::v_cmp_u_f64:
   #undef CMP
      ctx.info[instr->definitions[0].tempId()].set_fcmp(instr.get());
      break;
   case aco_opcode::s_cselect_b64:
   case aco_opcode::s_cselect_b32:
      if (instr->operands[0].constantEquals((unsigned) -1) &&
          instr->operands[1].constantEquals(0)) {
         /* Found a cselect that operates on a uniform bool that comes from eg. s_cmp */
         ctx.info[instr->definitions[0].tempId()].set_uniform_bool(instr->operands[2].getTemp());
      }
      if (instr->operands[2].isTemp() && ctx.info[instr->operands[2].tempId()].is_scc_invert()) {
         /* Flip the operands to get rid of the scc_invert instruction */
         std::swap(instr->operands[0], instr->operands[1]);
         instr->operands[2].setTemp(ctx.info[instr->operands[2].tempId()].temp);
      }
      break;
   case aco_opcode::p_wqm:
      if (instr->operands[0].isTemp() &&
          ctx.info[instr->operands[0].tempId()].is_scc_invert()) {
         ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
      }
      break;
   default:
      break;
   }
}

ALWAYS_INLINE bool get_cmp_info(aco_opcode op, CmpInfo *info)
{
   info->ordered = aco_opcode::num_opcodes;
   info->unordered = aco_opcode::num_opcodes;
   info->ordered_swapped = aco_opcode::num_opcodes;
   info->unordered_swapped = aco_opcode::num_opcodes;
   switch (op) {
   #define CMP2(ord, unord, ord_swap, unord_swap, sz) \
   case aco_opcode::v_cmp_##ord##_f##sz:\
   case aco_opcode::v_cmp_n##unord##_f##sz:\
      info->ordered = aco_opcode::v_cmp_##ord##_f##sz;\
      info->unordered = aco_opcode::v_cmp_n##unord##_f##sz;\
      info->ordered_swapped = aco_opcode::v_cmp_##ord_swap##_f##sz;\
      info->unordered_swapped = aco_opcode::v_cmp_n##unord_swap##_f##sz;\
      info->inverse = op == aco_opcode::v_cmp_n##unord##_f##sz ? aco_opcode::v_cmp_##unord##_f##sz : aco_opcode::v_cmp_n##ord##_f##sz;\
      info->f32 = op == aco_opcode::v_cmp_##ord##_f##sz ? aco_opcode::v_cmp_##ord##_f32 : aco_opcode::v_cmp_n##unord##_f32;\
      info->size = sz;\
      return true;
   #define CMP(ord, unord, ord_swap, unord_swap) \
   CMP2(ord, unord, ord_swap, unord_swap, 16)\
   CMP2(ord, unord, ord_swap, unord_swap, 32)\
   CMP2(ord, unord, ord_swap, unord_swap, 64)
   CMP(lt, /*n*/ge, gt, /*n*/le)
   CMP(eq, /*n*/lg, eq, /*n*/lg)
   CMP(le, /*n*/gt, ge, /*n*/lt)
   CMP(gt, /*n*/le, lt, /*n*/le)
   CMP(lg, /*n*/eq, lg, /*n*/eq)
   CMP(ge, /*n*/lt, le, /*n*/gt)
   #undef CMP
   #undef CMP2
   #define ORD_TEST(sz) \
   case aco_opcode::v_cmp_u_f##sz:\
      info->f32 = aco_opcode::v_cmp_u_f32;\
      info->inverse = aco_opcode::v_cmp_o_f##sz;\
      info->size = sz;\
      return true;\
   case aco_opcode::v_cmp_o_f##sz:\
      info->f32 = aco_opcode::v_cmp_o_f32;\
      info->inverse = aco_opcode::v_cmp_u_f##sz;\
      info->size = sz;\
      return true;
   ORD_TEST(16)
   ORD_TEST(32)
   ORD_TEST(64)
   #undef ORD_TEST
   default:
      return false;
   }
}

aco_opcode get_ordered(aco_opcode op)
{
   CmpInfo info;
   return get_cmp_info(op, &info) ? info.ordered : aco_opcode::num_opcodes;
}

aco_opcode get_unordered(aco_opcode op)
{
   CmpInfo info;
   return get_cmp_info(op, &info) ? info.unordered : aco_opcode::num_opcodes;
}

aco_opcode get_inverse(aco_opcode op)
{
   CmpInfo info;
   return get_cmp_info(op, &info) ? info.inverse : aco_opcode::num_opcodes;
}

aco_opcode get_f32_cmp(aco_opcode op)
{
   CmpInfo info;
   return get_cmp_info(op, &info) ? info.f32 : aco_opcode::num_opcodes;
}

unsigned get_cmp_bitsize(aco_opcode op)
{
   CmpInfo info;
   return get_cmp_info(op, &info) ? info.size : 0;
}

bool is_cmp(aco_opcode op)
{
   CmpInfo info;
   return get_cmp_info(op, &info) && info.ordered != aco_opcode::num_opcodes;
}

unsigned original_temp_id(opt_ctx &ctx, Temp tmp)
{
   if (ctx.info[tmp.id()].is_temp())
      return ctx.info[tmp.id()].temp.id();
   else
      return tmp.id();
}

void decrease_uses(opt_ctx &ctx, Instruction* instr)
{
   if (!--ctx.uses[instr->definitions[0].tempId()]) {
      for (const Operand& op : instr->operands) {
         if (op.isTemp())
            ctx.uses[op.tempId()]--;
      }
   }
}

Instruction *follow_operand(opt_ctx &ctx, Operand op, bool ignore_uses=false)
{
   if (!op.isTemp() || !(ctx.info[op.tempId()].label & instr_labels))
      return nullptr;
   if (!ignore_uses && ctx.uses[op.tempId()] > 1)
      return nullptr;

   Instruction *instr = ctx.info[op.tempId()].instr;

   if (instr->definitions.size() == 2) {
      assert(instr->definitions[0].isTemp() && instr->definitions[0].tempId() == op.tempId());
      if (instr->definitions[1].isTemp() && ctx.uses[instr->definitions[1].tempId()])
         return nullptr;
   }

   return instr;
}

/* s_or_b64(neq(a, a), neq(b, b)) -> v_cmp_u_f32(a, b)
 * s_and_b64(eq(a, a), eq(b, b)) -> v_cmp_o_f32(a, b) */
bool combine_ordering_test(opt_ctx &ctx, aco_ptr<Instruction>& instr)
{
   if (instr->definitions[0].regClass() != ctx.program->lane_mask)
      return false;
   if (instr->definitions[1].isTemp() && ctx.uses[instr->definitions[1].tempId()])
      return false;

   bool is_or = instr->opcode == aco_opcode::s_or_b64 || instr->opcode == aco_opcode::s_or_b32;

   bool neg[2] = {false, false};
   bool abs[2] = {false, false};
   uint8_t opsel = 0;
   Instruction *op_instr[2];
   Temp op[2];

   unsigned bitsize = 0;
   for (unsigned i = 0; i < 2; i++) {
      op_instr[i] = follow_operand(ctx, instr->operands[i], true);
      if (!op_instr[i])
         return false;

      aco_opcode expected_cmp = is_or ? aco_opcode::v_cmp_neq_f32 : aco_opcode::v_cmp_eq_f32;
      unsigned op_bitsize = get_cmp_bitsize(op_instr[i]->opcode);

      if (get_f32_cmp(op_instr[i]->opcode) != expected_cmp)
         return false;
      if (bitsize && op_bitsize != bitsize)
         return false;
      if (!op_instr[i]->operands[0].isTemp() || !op_instr[i]->operands[1].isTemp())
         return false;

      if (op_instr[i]->isVOP3()) {
         VOP3A_instruction *vop3 = static_cast<VOP3A_instruction*>(op_instr[i]);
         if (vop3->neg[0] != vop3->neg[1] || vop3->abs[0] != vop3->abs[1] || vop3->opsel == 1 || vop3->opsel == 2)
            return false;
         neg[i] = vop3->neg[0];
         abs[i] = vop3->abs[0];
         opsel |= (vop3->opsel & 1) << i;
      }

      Temp op0 = op_instr[i]->operands[0].getTemp();
      Temp op1 = op_instr[i]->operands[1].getTemp();
      if (original_temp_id(ctx, op0) != original_temp_id(ctx, op1))
         return false;

      op[i] = op1;
      bitsize = op_bitsize;
   }

   if (op[1].type() == RegType::sgpr)
      std::swap(op[0], op[1]);
   unsigned num_sgprs = (op[0].type() == RegType::sgpr) + (op[1].type() == RegType::sgpr);
   if (num_sgprs > (ctx.program->chip_class >= GFX10 ? 2 : 1))
      return false;

   ctx.uses[op[0].id()]++;
   ctx.uses[op[1].id()]++;
   decrease_uses(ctx, op_instr[0]);
   decrease_uses(ctx, op_instr[1]);

   aco_opcode new_op = aco_opcode::num_opcodes;
   switch (bitsize) {
   case 16:
      new_op = is_or ? aco_opcode::v_cmp_u_f16 : aco_opcode::v_cmp_o_f16;
      break;
   case 32:
      new_op = is_or ? aco_opcode::v_cmp_u_f32 : aco_opcode::v_cmp_o_f32;
      break;
   case 64:
      new_op = is_or ? aco_opcode::v_cmp_u_f64 : aco_opcode::v_cmp_o_f64;
      break;
   }
   Instruction *new_instr;
   if (neg[0] || neg[1] || abs[0] || abs[1] || opsel || num_sgprs > 1) {
      VOP3A_instruction *vop3 = create_instruction<VOP3A_instruction>(new_op, asVOP3(Format::VOPC), 2, 1);
      for (unsigned i = 0; i < 2; i++) {
         vop3->neg[i] = neg[i];
         vop3->abs[i] = abs[i];
      }
      vop3->opsel = opsel;
      new_instr = static_cast<Instruction *>(vop3);
   } else {
      new_instr = create_instruction<VOPC_instruction>(new_op, Format::VOPC, 2, 1);
   }
   new_instr->operands[0] = Operand(op[0]);
   new_instr->operands[1] = Operand(op[1]);
   new_instr->definitions[0] = instr->definitions[0];

   ctx.info[instr->definitions[0].tempId()].label = 0;
   ctx.info[instr->definitions[0].tempId()].set_fcmp(new_instr);

   instr.reset(new_instr);

   return true;
}

/* s_or_b64(v_cmp_u_f32(a, b), cmp(a, b)) -> get_unordered(cmp)(a, b)
 * s_and_b64(v_cmp_o_f32(a, b), cmp(a, b)) -> get_ordered(cmp)(a, b) */
bool combine_comparison_ordering(opt_ctx &ctx, aco_ptr<Instruction>& instr)
{
   if (instr->definitions[0].regClass() != ctx.program->lane_mask)
      return false;
   if (instr->definitions[1].isTemp() && ctx.uses[instr->definitions[1].tempId()])
      return false;

   bool is_or = instr->opcode == aco_opcode::s_or_b64 || instr->opcode == aco_opcode::s_or_b32;
   aco_opcode expected_nan_test = is_or ? aco_opcode::v_cmp_u_f32 : aco_opcode::v_cmp_o_f32;

   Instruction *nan_test = follow_operand(ctx, instr->operands[0], true);
   Instruction *cmp = follow_operand(ctx, instr->operands[1], true);
   if (!nan_test || !cmp)
      return false;

   if (get_f32_cmp(cmp->opcode) == expected_nan_test)
      std::swap(nan_test, cmp);
   else if (get_f32_cmp(nan_test->opcode) != expected_nan_test)
      return false;

   if (!is_cmp(cmp->opcode) || get_cmp_bitsize(cmp->opcode) != get_cmp_bitsize(nan_test->opcode))
      return false;

   if (!nan_test->operands[0].isTemp() || !nan_test->operands[1].isTemp())
      return false;
   if (!cmp->operands[0].isTemp() || !cmp->operands[1].isTemp())
      return false;

   unsigned prop_cmp0 = original_temp_id(ctx, cmp->operands[0].getTemp());
   unsigned prop_cmp1 = original_temp_id(ctx, cmp->operands[1].getTemp());
   unsigned prop_nan0 = original_temp_id(ctx, nan_test->operands[0].getTemp());
   unsigned prop_nan1 = original_temp_id(ctx, nan_test->operands[1].getTemp());
   if (prop_cmp0 != prop_nan0 && prop_cmp0 != prop_nan1)
      return false;
   if (prop_cmp1 != prop_nan0 && prop_cmp1 != prop_nan1)
      return false;

   ctx.uses[cmp->operands[0].tempId()]++;
   ctx.uses[cmp->operands[1].tempId()]++;
   decrease_uses(ctx, nan_test);
   decrease_uses(ctx, cmp);

   aco_opcode new_op = is_or ? get_unordered(cmp->opcode) : get_ordered(cmp->opcode);
   Instruction *new_instr;
   if (cmp->isVOP3()) {
      VOP3A_instruction *new_vop3 = create_instruction<VOP3A_instruction>(new_op, asVOP3(Format::VOPC), 2, 1);
      VOP3A_instruction *cmp_vop3 = static_cast<VOP3A_instruction*>(cmp);
      memcpy(new_vop3->abs, cmp_vop3->abs, sizeof(new_vop3->abs));
      memcpy(new_vop3->neg, cmp_vop3->neg, sizeof(new_vop3->neg));
      new_vop3->clamp = cmp_vop3->clamp;
      new_vop3->omod = cmp_vop3->omod;
      new_vop3->opsel = cmp_vop3->opsel;
      new_instr = new_vop3;
   } else {
      new_instr = create_instruction<VOPC_instruction>(new_op, Format::VOPC, 2, 1);
   }
   new_instr->operands[0] = cmp->operands[0];
   new_instr->operands[1] = cmp->operands[1];
   new_instr->definitions[0] = instr->definitions[0];

   ctx.info[instr->definitions[0].tempId()].label = 0;
   ctx.info[instr->definitions[0].tempId()].set_fcmp(new_instr);

   instr.reset(new_instr);

   return true;
}

/* s_or_b64(v_cmp_neq_f32(a, a), cmp(a, #b)) and b is not NaN -> get_unordered(cmp)(a, b)
 * s_and_b64(v_cmp_eq_f32(a, a), cmp(a, #b)) and b is not NaN -> get_ordered(cmp)(a, b) */
bool combine_constant_comparison_ordering(opt_ctx &ctx, aco_ptr<Instruction>& instr)
{
   if (instr->definitions[0].regClass() != ctx.program->lane_mask)
      return false;
   if (instr->definitions[1].isTemp() && ctx.uses[instr->definitions[1].tempId()])
      return false;

   bool is_or = instr->opcode == aco_opcode::s_or_b64 || instr->opcode == aco_opcode::s_or_b32;

   Instruction *nan_test = follow_operand(ctx, instr->operands[0], true);
   Instruction *cmp = follow_operand(ctx, instr->operands[1], true);

   if (!nan_test || !cmp)
      return false;

   aco_opcode expected_nan_test = is_or ? aco_opcode::v_cmp_neq_f32 : aco_opcode::v_cmp_eq_f32;
   if (get_f32_cmp(cmp->opcode) == expected_nan_test)
      std::swap(nan_test, cmp);
   else if (get_f32_cmp(nan_test->opcode) != expected_nan_test)
      return false;

   if (!is_cmp(cmp->opcode) || get_cmp_bitsize(cmp->opcode) != get_cmp_bitsize(nan_test->opcode))
      return false;

   if (!nan_test->operands[0].isTemp() || !nan_test->operands[1].isTemp())
      return false;
   if (!cmp->operands[0].isTemp() && !cmp->operands[1].isTemp())
      return false;

   unsigned prop_nan0 = original_temp_id(ctx, nan_test->operands[0].getTemp());
   unsigned prop_nan1 = original_temp_id(ctx, nan_test->operands[1].getTemp());
   if (prop_nan0 != prop_nan1)
      return false;

   if (nan_test->isVOP3()) {
      VOP3A_instruction *vop3 = static_cast<VOP3A_instruction*>(nan_test);
      if (vop3->neg[0] != vop3->neg[1] || vop3->abs[0] != vop3->abs[1] || vop3->opsel == 1 || vop3->opsel == 2)
         return false;
   }

   int constant_operand = -1;
   for (unsigned i = 0; i < 2; i++) {
      if (cmp->operands[i].isTemp() && original_temp_id(ctx, cmp->operands[i].getTemp()) == prop_nan0) {
         constant_operand = !i;
         break;
      }
   }
   if (constant_operand == -1)
      return false;

   uint32_t constant;
   if (cmp->operands[constant_operand].isConstant()) {
      constant = cmp->operands[constant_operand].constantValue();
   } else if (cmp->operands[constant_operand].isTemp()) {
      Temp tmp = cmp->operands[constant_operand].getTemp();
      unsigned id = original_temp_id(ctx, tmp);
      if (!ctx.info[id].is_constant_or_literal(32))
         return false;
      constant = ctx.info[id].val;
   } else {
      return false;
   }

   float constantf;
   memcpy(&constantf, &constant, 4);
   if (isnan(constantf))
      return false;

   if (cmp->operands[0].isTemp())
      ctx.uses[cmp->operands[0].tempId()]++;
   if (cmp->operands[1].isTemp())
      ctx.uses[cmp->operands[1].tempId()]++;
   decrease_uses(ctx, nan_test);
   decrease_uses(ctx, cmp);

   aco_opcode new_op = is_or ? get_unordered(cmp->opcode) : get_ordered(cmp->opcode);
   Instruction *new_instr;
   if (cmp->isVOP3()) {
      VOP3A_instruction *new_vop3 = create_instruction<VOP3A_instruction>(new_op, asVOP3(Format::VOPC), 2, 1);
      VOP3A_instruction *cmp_vop3 = static_cast<VOP3A_instruction*>(cmp);
      memcpy(new_vop3->abs, cmp_vop3->abs, sizeof(new_vop3->abs));
      memcpy(new_vop3->neg, cmp_vop3->neg, sizeof(new_vop3->neg));
      new_vop3->clamp = cmp_vop3->clamp;
      new_vop3->omod = cmp_vop3->omod;
      new_vop3->opsel = cmp_vop3->opsel;
      new_instr = new_vop3;
   } else {
      new_instr = create_instruction<VOPC_instruction>(new_op, Format::VOPC, 2, 1);
   }
   new_instr->operands[0] = cmp->operands[0];
   new_instr->operands[1] = cmp->operands[1];
   new_instr->definitions[0] = instr->definitions[0];

   ctx.info[instr->definitions[0].tempId()].label = 0;
   ctx.info[instr->definitions[0].tempId()].set_fcmp(new_instr);

   instr.reset(new_instr);

   return true;
}

/* s_not_b64(cmp(a, b) -> get_inverse(cmp)(a, b) */
bool combine_inverse_comparison(opt_ctx &ctx, aco_ptr<Instruction>& instr)
{
   if (instr->opcode != aco_opcode::s_not_b64)
      return false;
   if (instr->definitions[1].isTemp() && ctx.uses[instr->definitions[1].tempId()])
      return false;
   if (!instr->operands[0].isTemp())
      return false;

   Instruction *cmp = follow_operand(ctx, instr->operands[0]);
   if (!cmp)
      return false;

   aco_opcode new_opcode = get_inverse(cmp->opcode);
   if (new_opcode == aco_opcode::num_opcodes)
      return false;

   if (cmp->operands[0].isTemp())
      ctx.uses[cmp->operands[0].tempId()]++;
   if (cmp->operands[1].isTemp())
      ctx.uses[cmp->operands[1].tempId()]++;
   decrease_uses(ctx, cmp);

   Instruction *new_instr;
   if (cmp->isVOP3()) {
      VOP3A_instruction *new_vop3 = create_instruction<VOP3A_instruction>(new_opcode, asVOP3(Format::VOPC), 2, 1);
      VOP3A_instruction *cmp_vop3 = static_cast<VOP3A_instruction*>(cmp);
      memcpy(new_vop3->abs, cmp_vop3->abs, sizeof(new_vop3->abs));
      memcpy(new_vop3->neg, cmp_vop3->neg, sizeof(new_vop3->neg));
      new_vop3->clamp = cmp_vop3->clamp;
      new_vop3->omod = cmp_vop3->omod;
      new_vop3->opsel = cmp_vop3->opsel;
      new_instr = new_vop3;
   } else {
      new_instr = create_instruction<VOPC_instruction>(new_opcode, Format::VOPC, 2, 1);
   }
   new_instr->operands[0] = cmp->operands[0];
   new_instr->operands[1] = cmp->operands[1];
   new_instr->definitions[0] = instr->definitions[0];

   ctx.info[instr->definitions[0].tempId()].label = 0;
   ctx.info[instr->definitions[0].tempId()].set_fcmp(new_instr);

   instr.reset(new_instr);

   return true;
}

/* op1(op2(1, 2), 0) if swap = false
 * op1(0, op2(1, 2)) if swap = true */
bool match_op3_for_vop3(opt_ctx &ctx, aco_opcode op1, aco_opcode op2,
                        Instruction* op1_instr, bool swap, const char *shuffle_str,
                        Operand operands[3], bool neg[3], bool abs[3], uint8_t *opsel,
                        bool *op1_clamp, uint8_t *op1_omod,
                        bool *inbetween_neg, bool *inbetween_abs, bool *inbetween_opsel)
{
   /* checks */
   if (op1_instr->opcode != op1)
      return false;

   Instruction *op2_instr = follow_operand(ctx, op1_instr->operands[swap]);
   if (!op2_instr || op2_instr->opcode != op2)
      return false;
   if (fixed_to_exec(op2_instr->operands[0]) || fixed_to_exec(op2_instr->operands[1]))
      return false;

   VOP3A_instruction *op1_vop3 = op1_instr->isVOP3() ? static_cast<VOP3A_instruction *>(op1_instr) : NULL;
   VOP3A_instruction *op2_vop3 = op2_instr->isVOP3() ? static_cast<VOP3A_instruction *>(op2_instr) : NULL;

   /* don't support inbetween clamp/omod */
   if (op2_vop3 && (op2_vop3->clamp || op2_vop3->omod))
      return false;

   /* get operands and modifiers and check inbetween modifiers */
   *op1_clamp = op1_vop3 ? op1_vop3->clamp : false;
   *op1_omod = op1_vop3 ? op1_vop3->omod : 0u;

   if (inbetween_neg)
      *inbetween_neg = op1_vop3 ? op1_vop3->neg[swap] : false;
   else if (op1_vop3 && op1_vop3->neg[swap])
      return false;

   if (inbetween_abs)
      *inbetween_abs = op1_vop3 ? op1_vop3->abs[swap] : false;
   else if (op1_vop3 && op1_vop3->abs[swap])
      return false;

   if (inbetween_opsel)
      *inbetween_opsel = op1_vop3 ? op1_vop3->opsel & (1 << swap) : false;
   else if (op1_vop3 && op1_vop3->opsel & (1 << swap))
      return false;

   int shuffle[3];
   shuffle[shuffle_str[0] - '0'] = 0;
   shuffle[shuffle_str[1] - '0'] = 1;
   shuffle[shuffle_str[2] - '0'] = 2;

   operands[shuffle[0]] = op1_instr->operands[!swap];
   neg[shuffle[0]] = op1_vop3 ? op1_vop3->neg[!swap] : false;
   abs[shuffle[0]] = op1_vop3 ? op1_vop3->abs[!swap] : false;
   if (op1_vop3 && op1_vop3->opsel & (1 << !swap))
      *opsel |= 1 << shuffle[0];

   for (unsigned i = 0; i < 2; i++) {
      operands[shuffle[i + 1]] = op2_instr->operands[i];
      neg[shuffle[i + 1]] = op2_vop3 ? op2_vop3->neg[i] : false;
      abs[shuffle[i + 1]] = op2_vop3 ? op2_vop3->abs[i] : false;
      if (op2_vop3 && op2_vop3->opsel & (1 << i))
         *opsel |= 1 << shuffle[i + 1];
   }

   /* check operands */
   if (!check_vop3_operands(ctx, 3, operands))
      return false;

   return true;
}

void create_vop3_for_op3(opt_ctx& ctx, aco_opcode opcode, aco_ptr<Instruction>& instr,
                         Operand operands[3], bool neg[3], bool abs[3], uint8_t opsel,
                         bool clamp, unsigned omod)
{
   VOP3A_instruction *new_instr = create_instruction<VOP3A_instruction>(opcode, Format::VOP3A, 3, 1);
   memcpy(new_instr->abs, abs, sizeof(bool[3]));
   memcpy(new_instr->neg, neg, sizeof(bool[3]));
   new_instr->clamp = clamp;
   new_instr->omod = omod;
   new_instr->opsel = opsel;
   new_instr->operands[0] = operands[0];
   new_instr->operands[1] = operands[1];
   new_instr->operands[2] = operands[2];
   new_instr->definitions[0] = instr->definitions[0];
   ctx.info[instr->definitions[0].tempId()].label = 0;

   instr.reset(new_instr);
}

bool combine_three_valu_op(opt_ctx& ctx, aco_ptr<Instruction>& instr, aco_opcode op2, aco_opcode new_op, const char *shuffle, uint8_t ops)
{
   uint64_t omod_clamp = ctx.info[instr->definitions[0].tempId()].label &
                         (label_omod_success | label_clamp_success);

   for (unsigned swap = 0; swap < 2; swap++) {
      if (!((1 << swap) & ops))
         continue;

      Operand operands[3];
      bool neg[3], abs[3], clamp;
      uint8_t opsel = 0, omod = 0;
      if (match_op3_for_vop3(ctx, instr->opcode, op2,
                             instr.get(), swap, shuffle,
                             operands, neg, abs, &opsel,
                             &clamp, &omod, NULL, NULL, NULL)) {
         ctx.uses[instr->operands[swap].tempId()]--;
         create_vop3_for_op3(ctx, new_op, instr, operands, neg, abs, opsel, clamp, omod);
         if (omod_clamp & label_omod_success)
            ctx.info[instr->definitions[0].tempId()].set_omod_success(instr.get());
         if (omod_clamp & label_clamp_success)
            ctx.info[instr->definitions[0].tempId()].set_clamp_success(instr.get());
         return true;
      }
   }
   return false;
}

bool combine_minmax(opt_ctx& ctx, aco_ptr<Instruction>& instr, aco_opcode opposite, aco_opcode minmax3)
{
   if (combine_three_valu_op(ctx, instr, instr->opcode, minmax3, "012", 1 | 2))
      return true;

   uint64_t omod_clamp = ctx.info[instr->definitions[0].tempId()].label &
                         (label_omod_success | label_clamp_success);

   /* min(-max(a, b), c) -> min3(-a, -b, c) *
    * max(-min(a, b), c) -> max3(-a, -b, c) */
   for (unsigned swap = 0; swap < 2; swap++) {
      Operand operands[3];
      bool neg[3], abs[3], clamp;
      uint8_t opsel = 0, omod = 0;
      bool inbetween_neg;
      if (match_op3_for_vop3(ctx, instr->opcode, opposite,
                             instr.get(), swap, "012",
                             operands, neg, abs, &opsel,
                             &clamp, &omod, &inbetween_neg, NULL, NULL) &&
          inbetween_neg) {
         ctx.uses[instr->operands[swap].tempId()]--;
         neg[1] = true;
         neg[2] = true;
         create_vop3_for_op3(ctx, minmax3, instr, operands, neg, abs, opsel, clamp, omod);
         if (omod_clamp & label_omod_success)
            ctx.info[instr->definitions[0].tempId()].set_omod_success(instr.get());
         if (omod_clamp & label_clamp_success)
            ctx.info[instr->definitions[0].tempId()].set_clamp_success(instr.get());
         return true;
      }
   }
   return false;
}

/* s_not_b32(s_and_b32(a, b)) -> s_nand_b32(a, b)
 * s_not_b32(s_or_b32(a, b)) -> s_nor_b32(a, b)
 * s_not_b32(s_xor_b32(a, b)) -> s_xnor_b32(a, b)
 * s_not_b64(s_and_b64(a, b)) -> s_nand_b64(a, b)
 * s_not_b64(s_or_b64(a, b)) -> s_nor_b64(a, b)
 * s_not_b64(s_xor_b64(a, b)) -> s_xnor_b64(a, b) */
bool combine_salu_not_bitwise(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   /* checks */
   if (!instr->operands[0].isTemp())
      return false;
   if (instr->definitions[1].isTemp() && ctx.uses[instr->definitions[1].tempId()])
      return false;

   Instruction *op2_instr = follow_operand(ctx, instr->operands[0]);
   if (!op2_instr)
      return false;
   switch (op2_instr->opcode) {
   case aco_opcode::s_and_b32:
   case aco_opcode::s_or_b32:
   case aco_opcode::s_xor_b32:
   case aco_opcode::s_and_b64:
   case aco_opcode::s_or_b64:
   case aco_opcode::s_xor_b64:
      break;
   default:
      return false;
   }

   /* create instruction */
   std::swap(instr->definitions[0], op2_instr->definitions[0]);
   std::swap(instr->definitions[1], op2_instr->definitions[1]);
   ctx.uses[instr->operands[0].tempId()]--;
   ctx.info[op2_instr->definitions[0].tempId()].label = 0;

   switch (op2_instr->opcode) {
   case aco_opcode::s_and_b32:
      op2_instr->opcode = aco_opcode::s_nand_b32;
      break;
   case aco_opcode::s_or_b32:
      op2_instr->opcode = aco_opcode::s_nor_b32;
      break;
   case aco_opcode::s_xor_b32:
      op2_instr->opcode = aco_opcode::s_xnor_b32;
      break;
   case aco_opcode::s_and_b64:
      op2_instr->opcode = aco_opcode::s_nand_b64;
      break;
   case aco_opcode::s_or_b64:
      op2_instr->opcode = aco_opcode::s_nor_b64;
      break;
   case aco_opcode::s_xor_b64:
      op2_instr->opcode = aco_opcode::s_xnor_b64;
      break;
   default:
      break;
   }

   return true;
}

/* s_and_b32(a, s_not_b32(b)) -> s_andn2_b32(a, b)
 * s_or_b32(a, s_not_b32(b)) -> s_orn2_b32(a, b)
 * s_and_b64(a, s_not_b64(b)) -> s_andn2_b64(a, b)
 * s_or_b64(a, s_not_b64(b)) -> s_orn2_b64(a, b) */
bool combine_salu_n2(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->definitions[0].isTemp() && ctx.info[instr->definitions[0].tempId()].is_uniform_bool())
      return false;

   for (unsigned i = 0; i < 2; i++) {
      Instruction *op2_instr = follow_operand(ctx, instr->operands[i]);
      if (!op2_instr || (op2_instr->opcode != aco_opcode::s_not_b32 && op2_instr->opcode != aco_opcode::s_not_b64))
         continue;
      if (ctx.uses[op2_instr->definitions[1].tempId()] || fixed_to_exec(op2_instr->operands[0]))
         continue;

      if (instr->operands[!i].isLiteral() && op2_instr->operands[0].isLiteral() &&
          instr->operands[!i].constantValue() != op2_instr->operands[0].constantValue())
         continue;

      ctx.uses[instr->operands[i].tempId()]--;
      instr->operands[0] = instr->operands[!i];
      instr->operands[1] = op2_instr->operands[0];
      ctx.info[instr->definitions[0].tempId()].label = 0;

      switch (instr->opcode) {
      case aco_opcode::s_and_b32:
         instr->opcode = aco_opcode::s_andn2_b32;
         break;
      case aco_opcode::s_or_b32:
         instr->opcode = aco_opcode::s_orn2_b32;
         break;
      case aco_opcode::s_and_b64:
         instr->opcode = aco_opcode::s_andn2_b64;
         break;
      case aco_opcode::s_or_b64:
         instr->opcode = aco_opcode::s_orn2_b64;
         break;
      default:
         break;
      }

      return true;
   }
   return false;
}

/* s_add_{i32,u32}(a, s_lshl_b32(b, <n>)) -> s_lshl<n>_add_u32(a, b) */
bool combine_salu_lshl_add(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->opcode == aco_opcode::s_add_i32 && ctx.uses[instr->definitions[1].tempId()])
      return false;

   for (unsigned i = 0; i < 2; i++) {
      Instruction *op2_instr = follow_operand(ctx, instr->operands[i]);
      if (!op2_instr || op2_instr->opcode != aco_opcode::s_lshl_b32 ||
          ctx.uses[op2_instr->definitions[1].tempId()])
         continue;
      if (!op2_instr->operands[1].isConstant() || fixed_to_exec(op2_instr->operands[0]))
         continue;

      uint32_t shift = op2_instr->operands[1].constantValue();
      if (shift < 1 || shift > 4)
         continue;

      if (instr->operands[!i].isLiteral() && op2_instr->operands[0].isLiteral() &&
          instr->operands[!i].constantValue() != op2_instr->operands[0].constantValue())
         continue;

      ctx.uses[instr->operands[i].tempId()]--;
      instr->operands[1] = instr->operands[!i];
      instr->operands[0] = op2_instr->operands[0];
      ctx.info[instr->definitions[0].tempId()].label = 0;

      instr->opcode = ((aco_opcode[]){aco_opcode::s_lshl1_add_u32,
                                      aco_opcode::s_lshl2_add_u32,
                                      aco_opcode::s_lshl3_add_u32,
                                      aco_opcode::s_lshl4_add_u32})[shift - 1];

      return true;
   }
   return false;
}

bool combine_add_sub_b2i(opt_ctx& ctx, aco_ptr<Instruction>& instr, aco_opcode new_op, uint8_t ops)
{
   if (instr->usesModifiers())
      return false;

   for (unsigned i = 0; i < 2; i++) {
      if (!((1 << i) & ops))
         continue;
      if (instr->operands[i].isTemp() &&
          ctx.info[instr->operands[i].tempId()].is_b2i() &&
          ctx.uses[instr->operands[i].tempId()] == 1) {

         aco_ptr<Instruction> new_instr;
         if (instr->operands[!i].isTemp() && instr->operands[!i].getTemp().type() == RegType::vgpr) {
            new_instr.reset(create_instruction<VOP2_instruction>(new_op, Format::VOP2, 3, 2));
         } else if (ctx.program->chip_class >= GFX10 ||
                    (instr->operands[!i].isConstant() && !instr->operands[!i].isLiteral())) {
            new_instr.reset(create_instruction<VOP3A_instruction>(new_op, asVOP3(Format::VOP2), 3, 2));
         } else {
            return false;
         }
         ctx.uses[instr->operands[i].tempId()]--;
         new_instr->definitions[0] = instr->definitions[0];
         new_instr->definitions[1] = instr->definitions.size() == 2 ? instr->definitions[1] :
                                                                      Definition(ctx.program->allocateId(), ctx.program->lane_mask);
         new_instr->definitions[1].setHint(vcc);
         new_instr->operands[0] = Operand(0u);
         new_instr->operands[1] = instr->operands[!i];
         new_instr->operands[2] = Operand(ctx.info[instr->operands[i].tempId()].temp);
         instr = std::move(new_instr);
         ctx.info[instr->definitions[0].tempId()].label = 0;
         return true;
      }
   }

   return false;
}

bool get_minmax_info(aco_opcode op, aco_opcode *min, aco_opcode *max, aco_opcode *min3, aco_opcode *max3, aco_opcode *med3, bool *some_gfx9_only)
{
   switch (op) {
   #define MINMAX(type, gfx9) \
   case aco_opcode::v_min_##type:\
   case aco_opcode::v_max_##type:\
   case aco_opcode::v_med3_##type:\
      *min = aco_opcode::v_min_##type;\
      *max = aco_opcode::v_max_##type;\
      *med3 = aco_opcode::v_med3_##type;\
      *min3 = aco_opcode::v_min3_##type;\
      *max3 = aco_opcode::v_max3_##type;\
      *some_gfx9_only = gfx9;\
      return true;
   MINMAX(f32, false)
   MINMAX(u32, false)
   MINMAX(i32, false)
   MINMAX(f16, true)
   MINMAX(u16, true)
   MINMAX(i16, true)
   #undef MINMAX
   default:
      return false;
   }
}

/* v_min_{f,u,i}{16,32}(v_max_{f,u,i}{16,32}(a, lb), ub) -> v_med3_{f,u,i}{16,32}(a, lb, ub) when ub > lb
 * v_max_{f,u,i}{16,32}(v_min_{f,u,i}{16,32}(a, ub), lb) -> v_med3_{f,u,i}{16,32}(a, lb, ub) when ub > lb */
bool combine_clamp(opt_ctx& ctx, aco_ptr<Instruction>& instr,
                   aco_opcode min, aco_opcode max, aco_opcode med)
{
   /* TODO: GLSL's clamp(x, minVal, maxVal) and SPIR-V's
    * FClamp(x, minVal, maxVal)/NClamp(x, minVal, maxVal) are undefined if
    * minVal > maxVal, which means we can always select it to a v_med3_f32 */
   aco_opcode other_op;
   if (instr->opcode == min)
      other_op = max;
   else if (instr->opcode == max)
      other_op = min;
   else
      return false;

   uint64_t omod_clamp = ctx.info[instr->definitions[0].tempId()].label &
                         (label_omod_success | label_clamp_success);

   for (unsigned swap = 0; swap < 2; swap++) {
      Operand operands[3];
      bool neg[3], abs[3], clamp;
      uint8_t opsel = 0, omod = 0;
      if (match_op3_for_vop3(ctx, instr->opcode, other_op, instr.get(), swap,
                             "012", operands, neg, abs, &opsel,
                             &clamp, &omod, NULL, NULL, NULL)) {
         int const0_idx = -1, const1_idx = -1;
         uint32_t const0 = 0, const1 = 0;
         for (int i = 0; i < 3; i++) {
            uint32_t val;
            if (operands[i].isConstant()) {
               val = operands[i].constantValue();
            } else if (operands[i].isTemp() && ctx.info[operands[i].tempId()].is_constant_or_literal(32)) {
               val = ctx.info[operands[i].tempId()].val;
            } else {
               continue;
            }
            if (const0_idx >= 0) {
               const1_idx = i;
               const1 = val;
            } else {
               const0_idx = i;
               const0 = val;
            }
         }
         if (const0_idx < 0 || const1_idx < 0)
            continue;

         if (opsel & (1 << const0_idx))
            const0 >>= 16;
         if (opsel & (1 << const1_idx))
            const1 >>= 16;

         int lower_idx = const0_idx;
         switch (min) {
         case aco_opcode::v_min_f32:
         case aco_opcode::v_min_f16: {
            float const0_f, const1_f;
            if (min == aco_opcode::v_min_f32) {
               memcpy(&const0_f, &const0, 4);
               memcpy(&const1_f, &const1, 4);
            } else {
               const0_f = _mesa_half_to_float(const0);
               const1_f = _mesa_half_to_float(const1);
            }
            if (abs[const0_idx]) const0_f = fabsf(const0_f);
            if (abs[const1_idx]) const1_f = fabsf(const1_f);
            if (neg[const0_idx]) const0_f = -const0_f;
            if (neg[const1_idx]) const1_f = -const1_f;
            lower_idx = const0_f < const1_f ? const0_idx : const1_idx;
            break;
         }
         case aco_opcode::v_min_u32: {
            lower_idx = const0 < const1 ? const0_idx : const1_idx;
            break;
         }
         case aco_opcode::v_min_u16: {
            lower_idx = (uint16_t)const0 < (uint16_t)const1 ? const0_idx : const1_idx;
            break;
         }
         case aco_opcode::v_min_i32: {
            int32_t const0_i = const0 & 0x80000000u ? -2147483648 + (int32_t)(const0 & 0x7fffffffu) : const0;
            int32_t const1_i = const1 & 0x80000000u ? -2147483648 + (int32_t)(const1 & 0x7fffffffu) : const1;
            lower_idx = const0_i < const1_i ? const0_idx : const1_idx;
            break;
         }
         case aco_opcode::v_min_i16: {
            int16_t const0_i = const0 & 0x8000u ? -32768 + (int16_t)(const0 & 0x7fffu) : const0;
            int16_t const1_i = const1 & 0x8000u ? -32768 + (int16_t)(const1 & 0x7fffu) : const1;
            lower_idx = const0_i < const1_i ? const0_idx : const1_idx;
            break;
         }
         default:
            break;
         }
         int upper_idx = lower_idx == const0_idx ? const1_idx : const0_idx;

         if (instr->opcode == min) {
            if (upper_idx != 0 || lower_idx == 0)
               return false;
         } else {
            if (upper_idx == 0 || lower_idx != 0)
               return false;
         }

         ctx.uses[instr->operands[swap].tempId()]--;
         create_vop3_for_op3(ctx, med, instr, operands, neg, abs, opsel, clamp, omod);
         if (omod_clamp & label_omod_success)
            ctx.info[instr->definitions[0].tempId()].set_omod_success(instr.get());
         if (omod_clamp & label_clamp_success)
            ctx.info[instr->definitions[0].tempId()].set_clamp_success(instr.get());

         return true;
      }
   }

   return false;
}


void apply_sgprs(opt_ctx &ctx, aco_ptr<Instruction>& instr)
{
   bool is_shift64 = instr->opcode == aco_opcode::v_lshlrev_b64 ||
                     instr->opcode == aco_opcode::v_lshrrev_b64 ||
                     instr->opcode == aco_opcode::v_ashrrev_i64;

   /* find candidates and create the set of sgprs already read */
   unsigned sgpr_ids[2] = {0, 0};
   uint32_t operand_mask = 0;
   bool has_literal = false;
   for (unsigned i = 0; i < instr->operands.size(); i++) {
      if (instr->operands[i].isLiteral())
         has_literal = true;
      if (!instr->operands[i].isTemp())
         continue;
      if (instr->operands[i].getTemp().type() == RegType::sgpr) {
         if (instr->operands[i].tempId() != sgpr_ids[0])
            sgpr_ids[!!sgpr_ids[0]] = instr->operands[i].tempId();
      }
      ssa_info& info = ctx.info[instr->operands[i].tempId()];
      if (info.is_temp() && info.temp.type() == RegType::sgpr)
         operand_mask |= 1u << i;
   }
   unsigned max_sgprs = 1;
   if (ctx.program->chip_class >= GFX10 && !is_shift64)
      max_sgprs = 2;
   if (has_literal)
      max_sgprs--;

   unsigned num_sgprs = !!sgpr_ids[0] + !!sgpr_ids[1];

   /* keep on applying sgprs until there is nothing left to be done */
   while (operand_mask) {
      uint32_t sgpr_idx = 0;
      uint32_t sgpr_info_id = 0;
      uint32_t mask = operand_mask;
      /* choose a sgpr */
      while (mask) {
         unsigned i = u_bit_scan(&mask);
         uint16_t uses = ctx.uses[instr->operands[i].tempId()];
         if (sgpr_info_id == 0 || uses < ctx.uses[sgpr_info_id]) {
            sgpr_idx = i;
            sgpr_info_id = instr->operands[i].tempId();
         }
      }
      operand_mask &= ~(1u << sgpr_idx);

      /* Applying two sgprs require making it VOP3, so don't do it unless it's
       * definitively beneficial.
       * TODO: this is too conservative because later the use count could be reduced to 1 */
      if (num_sgprs && ctx.uses[sgpr_info_id] > 1 && !instr->isVOP3())
         break;

      Temp sgpr = ctx.info[sgpr_info_id].temp;
      bool new_sgpr = sgpr.id() != sgpr_ids[0] && sgpr.id() != sgpr_ids[1];
      if (new_sgpr && num_sgprs >= max_sgprs)
         continue;

      if (sgpr_idx == 0 || instr->isVOP3()) {
         instr->operands[sgpr_idx] = Operand(sgpr);
      } else if (can_swap_operands(instr)) {
         instr->operands[sgpr_idx] = instr->operands[0];
         instr->operands[0] = Operand(sgpr);
         /* swap bits using a 4-entry LUT */
         uint32_t swapped = (0x3120 >> (operand_mask & 0x3)) & 0xf;
         operand_mask = (operand_mask & ~0x3) | swapped;
      } else if (can_use_VOP3(ctx, instr)) {
         to_VOP3(ctx, instr);
         instr->operands[sgpr_idx] = Operand(sgpr);
      } else {
         continue;
      }

      if (new_sgpr)
         sgpr_ids[num_sgprs++] = sgpr.id();
      ctx.uses[sgpr_info_id]--;
      ctx.uses[sgpr.id()]++;
   }
}

bool apply_omod_clamp(opt_ctx &ctx, Block& block, aco_ptr<Instruction>& instr)
{
   /* check if we could apply omod on predecessor */
   if (instr->opcode == aco_opcode::v_mul_f32 || instr->opcode == aco_opcode::v_mul_f16) {
      bool op0 = instr->operands[0].isTemp() && ctx.info[instr->operands[0].tempId()].is_omod_success();
      bool op1 = instr->operands[1].isTemp() && ctx.info[instr->operands[1].tempId()].is_omod_success();
      if (op0 || op1) {
         unsigned idx = op0 ? 0 : 1;
         /* omod was successfully applied */
         /* if the omod instruction is v_mad, we also have to change the original add */
         if (ctx.info[instr->operands[idx].tempId()].is_mad()) {
            Instruction* add_instr = ctx.mad_infos[ctx.info[instr->operands[idx].tempId()].instr->pass_flags].add_instr.get();
            if (ctx.info[instr->definitions[0].tempId()].is_clamp())
               static_cast<VOP3A_instruction*>(add_instr)->clamp = true;
            add_instr->definitions[0] = instr->definitions[0];
         }

         Instruction* omod_instr = ctx.info[instr->operands[idx].tempId()].instr;
         /* check if we have an additional clamp modifier */
         if (ctx.info[instr->definitions[0].tempId()].is_clamp() && ctx.uses[instr->definitions[0].tempId()] == 1 &&
             ctx.uses[ctx.info[instr->definitions[0].tempId()].temp.id()]) {
            static_cast<VOP3A_instruction*>(omod_instr)->clamp = true;
            ctx.info[instr->definitions[0].tempId()].set_clamp_success(omod_instr);
         }
         /* change definition ssa-id of modified instruction */
         omod_instr->definitions[0] = instr->definitions[0];

         /* change the definition of instr to something unused, e.g. the original omod def */
         instr->definitions[0] = Definition(instr->operands[idx].getTemp());
         ctx.uses[instr->definitions[0].tempId()] = 0;
         return true;
      }
      if (!ctx.info[instr->definitions[0].tempId()].label) {
         /* in all other cases, label this instruction as option for multiply-add */
         ctx.info[instr->definitions[0].tempId()].set_mul(instr.get());
      }
   }

   /* check if we could apply clamp on predecessor */
   if (instr->opcode == aco_opcode::v_med3_f32 || instr->opcode == aco_opcode::v_med3_f16) {
      bool is_fp16 = instr->opcode == aco_opcode::v_med3_f16;
      unsigned idx = 0;
      bool found_zero = false, found_one = false;
      for (unsigned i = 0; i < 3; i++)
      {
         if (instr->operands[i].constantEquals(0))
            found_zero = true;
         else if (instr->operands[i].constantEquals(is_fp16 ? 0x3c00 : 0x3f800000)) /* 1.0 */
            found_one = true;
         else
            idx = i;
      }
      if (found_zero && found_one && instr->operands[idx].isTemp() &&
          ctx.info[instr->operands[idx].tempId()].is_clamp_success()) {
         /* clamp was successfully applied */
         /* if the clamp instruction is v_mad, we also have to change the original add */
         if (ctx.info[instr->operands[idx].tempId()].is_mad()) {
            Instruction* add_instr = ctx.mad_infos[ctx.info[instr->operands[idx].tempId()].instr->pass_flags].add_instr.get();
            add_instr->definitions[0] = instr->definitions[0];
         }
         Instruction* clamp_instr = ctx.info[instr->operands[idx].tempId()].instr;
         /* change definition ssa-id of modified instruction */
         clamp_instr->definitions[0] = instr->definitions[0];

         /* change the definition of instr to something unused, e.g. the original omod def */
         instr->definitions[0] = Definition(instr->operands[idx].getTemp());
         ctx.uses[instr->definitions[0].tempId()] = 0;
         return true;
      }
   }

   /* omod has no effect if denormals are enabled */
   /* apply omod / clamp modifiers if the def is used only once and the instruction can have modifiers */
   if (!instr->definitions.empty() && ctx.uses[instr->definitions[0].tempId()] == 1 &&
       can_use_VOP3(ctx, instr) && instr_info.can_use_output_modifiers[(int)instr->opcode]) {
      bool can_use_omod = (instr->definitions[0].bytes() == 4 ? block.fp_mode.denorm32 : block.fp_mode.denorm16_64) == 0;
      ssa_info& def_info = ctx.info[instr->definitions[0].tempId()];
      if (can_use_omod && def_info.is_omod2() && ctx.uses[def_info.temp.id()]) {
         to_VOP3(ctx, instr);
         static_cast<VOP3A_instruction*>(instr.get())->omod = 1;
         def_info.set_omod_success(instr.get());
      } else if (can_use_omod && def_info.is_omod4() && ctx.uses[def_info.temp.id()]) {
         to_VOP3(ctx, instr);
         static_cast<VOP3A_instruction*>(instr.get())->omod = 2;
         def_info.set_omod_success(instr.get());
      } else if (can_use_omod && def_info.is_omod5() && ctx.uses[def_info.temp.id()]) {
         to_VOP3(ctx, instr);
         static_cast<VOP3A_instruction*>(instr.get())->omod = 3;
         def_info.set_omod_success(instr.get());
      } else if (def_info.is_clamp() && ctx.uses[def_info.temp.id()]) {
         to_VOP3(ctx, instr);
         static_cast<VOP3A_instruction*>(instr.get())->clamp = true;
         def_info.set_clamp_success(instr.get());
      }
   }

   return false;
}

// TODO: we could possibly move the whole label_instruction pass to combine_instruction:
// this would mean that we'd have to fix the instruction uses while value propagation

void combine_instruction(opt_ctx &ctx, Block& block, aco_ptr<Instruction>& instr)
{
   if (instr->definitions.empty() || is_dead(ctx.uses, instr.get()))
      return;

   if (instr->isVALU()) {
      if (can_apply_sgprs(instr))
         apply_sgprs(ctx, instr);
      if (apply_omod_clamp(ctx, block, instr))
         return;
   }

   if (ctx.info[instr->definitions[0].tempId()].is_vcc_hint()) {
      instr->definitions[0].setHint(vcc);
   }

   /* TODO: There are still some peephole optimizations that could be done:
    * - abs(a - b) -> s_absdiff_i32
    * - various patterns for s_bitcmp{0,1}_b32 and s_bitset{0,1}_b32
    * - patterns for v_alignbit_b32 and v_alignbyte_b32
    * These aren't probably too interesting though.
    * There are also patterns for v_cmp_class_f{16,32,64}. This is difficult but
    * probably more useful than the previously mentioned optimizations.
    * The various comparison optimizations also currently only work with 32-bit
    * floats. */

   /* neg(mul(a, b)) -> mul(neg(a), b) */
   if (ctx.info[instr->definitions[0].tempId()].is_neg() && ctx.uses[instr->operands[1].tempId()] == 1) {
      Temp val = ctx.info[instr->definitions[0].tempId()].temp;

      if (!ctx.info[val.id()].is_mul())
         return;

      Instruction* mul_instr = ctx.info[val.id()].instr;

      if (mul_instr->operands[0].isLiteral())
         return;
      if (mul_instr->isVOP3() && static_cast<VOP3A_instruction*>(mul_instr)->clamp)
         return;

      /* convert to mul(neg(a), b) */
      ctx.uses[mul_instr->definitions[0].tempId()]--;
      Definition def = instr->definitions[0];
      /* neg(abs(mul(a, b))) -> mul(neg(abs(a)), abs(b)) */
      bool is_abs = ctx.info[instr->definitions[0].tempId()].is_abs();
      instr.reset(create_instruction<VOP3A_instruction>(mul_instr->opcode, asVOP3(Format::VOP2), 2, 1));
      instr->operands[0] = mul_instr->operands[0];
      instr->operands[1] = mul_instr->operands[1];
      instr->definitions[0] = def;
      VOP3A_instruction* new_mul = static_cast<VOP3A_instruction*>(instr.get());
      if (mul_instr->isVOP3()) {
         VOP3A_instruction* mul = static_cast<VOP3A_instruction*>(mul_instr);
         new_mul->neg[0] = mul->neg[0] && !is_abs;
         new_mul->neg[1] = mul->neg[1] && !is_abs;
         new_mul->abs[0] = mul->abs[0] || is_abs;
         new_mul->abs[1] = mul->abs[1] || is_abs;
         new_mul->omod = mul->omod;
      }
      new_mul->neg[0] ^= true;
      new_mul->clamp = false;

      ctx.info[instr->definitions[0].tempId()].set_mul(instr.get());
      return;
   }

   /* combine mul+add -> mad */
   bool mad32 = instr->opcode == aco_opcode::v_add_f32 ||
                instr->opcode == aco_opcode::v_sub_f32 ||
                instr->opcode == aco_opcode::v_subrev_f32;
   bool mad16 = instr->opcode == aco_opcode::v_add_f16 ||
                instr->opcode == aco_opcode::v_sub_f16 ||
                instr->opcode == aco_opcode::v_subrev_f16;
   if (mad16 || mad32) {
      bool need_fma = mad32 ? block.fp_mode.denorm32 != 0 :
                              (block.fp_mode.denorm16_64 != 0 || ctx.program->chip_class >= GFX10);
      if (need_fma && instr->definitions[0].isPrecise())
         return;
      if (need_fma && mad32 && !ctx.program->has_fast_fma32)
         return;

      uint32_t uses_src0 = UINT32_MAX;
      uint32_t uses_src1 = UINT32_MAX;
      Instruction* mul_instr = nullptr;
      unsigned add_op_idx;
      /* check if any of the operands is a multiplication */
      ssa_info *op0_info = instr->operands[0].isTemp() ? &ctx.info[instr->operands[0].tempId()] : NULL;
      ssa_info *op1_info = instr->operands[1].isTemp() ? &ctx.info[instr->operands[1].tempId()] : NULL;
      if (op0_info && op0_info->is_mul() && (!need_fma || !op0_info->instr->definitions[0].isPrecise()))
         uses_src0 = ctx.uses[instr->operands[0].tempId()];
      if (op1_info && op1_info->is_mul() && (!need_fma || !op1_info->instr->definitions[0].isPrecise()))
         uses_src1 = ctx.uses[instr->operands[1].tempId()];

      /* find the 'best' mul instruction to combine with the add */
      if (uses_src0 < uses_src1) {
         mul_instr = op0_info->instr;
         add_op_idx = 1;
      } else if (uses_src1 < uses_src0) {
         mul_instr = op1_info->instr;
         add_op_idx = 0;
      } else if (uses_src0 != UINT32_MAX) {
         /* tiebreaker: quite random what to pick */
         if (op0_info->instr->operands[0].isLiteral()) {
            mul_instr = op1_info->instr;
            add_op_idx = 0;
         } else {
            mul_instr = op0_info->instr;
            add_op_idx = 1;
         }
      }
      if (mul_instr) {
         Operand op[3] = {Operand(v1), Operand(v1), Operand(v1)};
         bool neg[3] = {false, false, false};
         bool abs[3] = {false, false, false};
         unsigned omod = 0;
         bool clamp = false;
         op[0] = mul_instr->operands[0];
         op[1] = mul_instr->operands[1];
         op[2] = instr->operands[add_op_idx];
         // TODO: would be better to check this before selecting a mul instr?
         if (!check_vop3_operands(ctx, 3, op))
            return;

         if (mul_instr->isVOP3()) {
            VOP3A_instruction* vop3 = static_cast<VOP3A_instruction*> (mul_instr);
            neg[0] = vop3->neg[0];
            neg[1] = vop3->neg[1];
            abs[0] = vop3->abs[0];
            abs[1] = vop3->abs[1];
            /* we cannot use these modifiers between mul and add */
            if (vop3->clamp || vop3->omod)
               return;
         }

         /* convert to mad */
         ctx.uses[mul_instr->definitions[0].tempId()]--;
         if (ctx.uses[mul_instr->definitions[0].tempId()]) {
            if (op[0].isTemp())
               ctx.uses[op[0].tempId()]++;
            if (op[1].isTemp())
               ctx.uses[op[1].tempId()]++;
         }

         if (instr->isVOP3()) {
            VOP3A_instruction* vop3 = static_cast<VOP3A_instruction*> (instr.get());
            neg[2] = vop3->neg[add_op_idx];
            abs[2] = vop3->abs[add_op_idx];
            omod = vop3->omod;
            clamp = vop3->clamp;
            /* abs of the multiplication result */
            if (vop3->abs[1 - add_op_idx]) {
               neg[0] = false;
               neg[1] = false;
               abs[0] = true;
               abs[1] = true;
            }
            /* neg of the multiplication result */
            neg[1] = neg[1] ^ vop3->neg[1 - add_op_idx];
         }
         if (instr->opcode == aco_opcode::v_sub_f32 || instr->opcode == aco_opcode::v_sub_f16)
            neg[1 + add_op_idx] = neg[1 + add_op_idx] ^ true;
         else if (instr->opcode == aco_opcode::v_subrev_f32 || instr->opcode == aco_opcode::v_subrev_f16)
            neg[2 - add_op_idx] = neg[2 - add_op_idx] ^ true;

         aco_opcode mad_op = need_fma ? aco_opcode::v_fma_f32 : aco_opcode::v_mad_f32;
         if (mad16)
            mad_op = need_fma ? (ctx.program->chip_class == GFX8 ? aco_opcode::v_fma_legacy_f16 : aco_opcode::v_fma_f16) :
                                (ctx.program->chip_class == GFX8 ? aco_opcode::v_mad_legacy_f16 : aco_opcode::v_mad_f16);

         aco_ptr<VOP3A_instruction> mad{create_instruction<VOP3A_instruction>(mad_op, Format::VOP3A, 3, 1)};
         for (unsigned i = 0; i < 3; i++)
         {
            mad->operands[i] = op[i];
            mad->neg[i] = neg[i];
            mad->abs[i] = abs[i];
         }
         mad->omod = omod;
         mad->clamp = clamp;
         mad->definitions[0] = instr->definitions[0];

         /* mark this ssa_def to be re-checked for profitability and literals */
         ctx.mad_infos.emplace_back(std::move(instr), mul_instr->definitions[0].tempId());
         ctx.info[mad->definitions[0].tempId()].set_mad(mad.get(), ctx.mad_infos.size() - 1);
         instr.reset(mad.release());
         return;
      }
   }
   /* v_mul_f32(v_cndmask_b32(0, 1.0, cond), a) -> v_cndmask_b32(0, a, cond) */
   else if (instr->opcode == aco_opcode::v_mul_f32 && !instr->isVOP3()) {
      for (unsigned i = 0; i < 2; i++) {
         if (instr->operands[i].isTemp() && ctx.info[instr->operands[i].tempId()].is_b2f() &&
             ctx.uses[instr->operands[i].tempId()] == 1 &&
             instr->operands[!i].isTemp() && instr->operands[!i].getTemp().type() == RegType::vgpr) {
            ctx.uses[instr->operands[i].tempId()]--;
            ctx.uses[ctx.info[instr->operands[i].tempId()].temp.id()]++;

            aco_ptr<VOP2_instruction> new_instr{create_instruction<VOP2_instruction>(aco_opcode::v_cndmask_b32, Format::VOP2, 3, 1)};
            new_instr->operands[0] = Operand(0u);
            new_instr->operands[1] = instr->operands[!i];
            new_instr->operands[2] = Operand(ctx.info[instr->operands[i].tempId()].temp);
            new_instr->definitions[0] = instr->definitions[0];
            instr.reset(new_instr.release());
            ctx.info[instr->definitions[0].tempId()].label = 0;
            return;
         }
      }
   } else if (instr->opcode == aco_opcode::v_or_b32 && ctx.program->chip_class >= GFX9) {
      if (combine_three_valu_op(ctx, instr, aco_opcode::s_or_b32, aco_opcode::v_or3_b32, "012", 1 | 2)) ;
      else if (combine_three_valu_op(ctx, instr, aco_opcode::v_or_b32, aco_opcode::v_or3_b32, "012", 1 | 2)) ;
      else if (combine_three_valu_op(ctx, instr, aco_opcode::s_and_b32, aco_opcode::v_and_or_b32, "120", 1 | 2)) ;
      else if (combine_three_valu_op(ctx, instr, aco_opcode::v_and_b32, aco_opcode::v_and_or_b32, "120", 1 | 2)) ;
      else if (combine_three_valu_op(ctx, instr, aco_opcode::s_lshl_b32, aco_opcode::v_lshl_or_b32, "120", 1 | 2)) ;
      else combine_three_valu_op(ctx, instr, aco_opcode::v_lshlrev_b32, aco_opcode::v_lshl_or_b32, "210", 1 | 2);
   } else if (instr->opcode == aco_opcode::v_xor_b32 && ctx.program->chip_class >= GFX10) {
      if (combine_three_valu_op(ctx, instr, aco_opcode::v_xor_b32, aco_opcode::v_xor3_b32, "012", 1 | 2)) ;
      else combine_three_valu_op(ctx, instr, aco_opcode::s_xor_b32, aco_opcode::v_xor3_b32, "012", 1 | 2);
   } else if (instr->opcode == aco_opcode::v_add_u32) {
      if (combine_add_sub_b2i(ctx, instr, aco_opcode::v_addc_co_u32, 1 | 2)) ;
      else if (ctx.program->chip_class >= GFX9) {
         if (combine_three_valu_op(ctx, instr, aco_opcode::s_xor_b32, aco_opcode::v_xad_u32, "120", 1 | 2)) ;
         else if (combine_three_valu_op(ctx, instr, aco_opcode::v_xor_b32, aco_opcode::v_xad_u32, "120", 1 | 2)) ;
         else if (combine_three_valu_op(ctx, instr, aco_opcode::s_add_i32, aco_opcode::v_add3_u32, "012", 1 | 2)) ;
         else if (combine_three_valu_op(ctx, instr, aco_opcode::s_add_u32, aco_opcode::v_add3_u32, "012", 1 | 2)) ;
         else if (combine_three_valu_op(ctx, instr, aco_opcode::v_add_u32, aco_opcode::v_add3_u32, "012", 1 | 2)) ;
         else if (combine_three_valu_op(ctx, instr, aco_opcode::s_lshl_b32, aco_opcode::v_lshl_add_u32, "120", 1 | 2)) ;
         else combine_three_valu_op(ctx, instr, aco_opcode::v_lshlrev_b32, aco_opcode::v_lshl_add_u32, "210", 1 | 2);
      }
   } else if (instr->opcode == aco_opcode::v_add_co_u32 ||
              instr->opcode == aco_opcode::v_add_co_u32_e64) {
      combine_add_sub_b2i(ctx, instr, aco_opcode::v_addc_co_u32, 1 | 2);
   } else if (instr->opcode == aco_opcode::v_sub_u32 ||
              instr->opcode == aco_opcode::v_sub_co_u32 ||
              instr->opcode == aco_opcode::v_sub_co_u32_e64) {
      combine_add_sub_b2i(ctx, instr, aco_opcode::v_subbrev_co_u32, 2);
   } else if (instr->opcode == aco_opcode::v_subrev_u32 ||
              instr->opcode == aco_opcode::v_subrev_co_u32 ||
              instr->opcode == aco_opcode::v_subrev_co_u32_e64) {
      combine_add_sub_b2i(ctx, instr, aco_opcode::v_subbrev_co_u32, 1);
   } else if (instr->opcode == aco_opcode::v_lshlrev_b32 && ctx.program->chip_class >= GFX9) {
      combine_three_valu_op(ctx, instr, aco_opcode::v_add_u32, aco_opcode::v_add_lshl_u32, "120", 2);
   } else if ((instr->opcode == aco_opcode::s_add_u32 || instr->opcode == aco_opcode::s_add_i32) && ctx.program->chip_class >= GFX9) {
      combine_salu_lshl_add(ctx, instr);
   } else if (instr->opcode == aco_opcode::s_not_b32) {
      combine_salu_not_bitwise(ctx, instr);
   } else if (instr->opcode == aco_opcode::s_not_b64) {
      if (combine_inverse_comparison(ctx, instr)) ;
      else combine_salu_not_bitwise(ctx, instr);
   } else if (instr->opcode == aco_opcode::s_and_b32 || instr->opcode == aco_opcode::s_or_b32 ||
              instr->opcode == aco_opcode::s_and_b64 || instr->opcode == aco_opcode::s_or_b64) {
      if (combine_ordering_test(ctx, instr)) ;
      else if (combine_comparison_ordering(ctx, instr)) ;
      else if (combine_constant_comparison_ordering(ctx, instr)) ;
      else combine_salu_n2(ctx, instr);
   } else {
      aco_opcode min, max, min3, max3, med3;
      bool some_gfx9_only;
      if (get_minmax_info(instr->opcode, &min, &max, &min3, &max3, &med3, &some_gfx9_only) &&
          (!some_gfx9_only || ctx.program->chip_class >= GFX9)) {
         if (combine_minmax(ctx, instr, instr->opcode == min ? max : min, instr->opcode == min ? min3 : max3)) ;
         else combine_clamp(ctx, instr, min, max, med3);
      }
   }
}

bool to_uniform_bool_instr(opt_ctx &ctx, aco_ptr<Instruction> &instr)
{
   switch (instr->opcode) {
      case aco_opcode::s_and_b32:
      case aco_opcode::s_and_b64:
         instr->opcode = aco_opcode::s_and_b32;
         break;
      case aco_opcode::s_or_b32:
      case aco_opcode::s_or_b64:
         instr->opcode = aco_opcode::s_or_b32;
         break;
      case aco_opcode::s_xor_b32:
      case aco_opcode::s_xor_b64:
         instr->opcode = aco_opcode::s_absdiff_i32;
         break;
      default:
         /* Don't transform other instructions. They are very unlikely to appear here. */
         return false;
   }

   for (Operand &op : instr->operands) {
      ctx.uses[op.tempId()]--;

      if (ctx.info[op.tempId()].is_uniform_bool()) {
         /* Just use the uniform boolean temp. */
         op.setTemp(ctx.info[op.tempId()].temp);
      } else if (ctx.info[op.tempId()].is_uniform_bitwise()) {
         /* Use the SCC definition of the predecessor instruction.
          * This allows the predecessor to get picked up by the same optimization (if it has no divergent users),
          * and it also makes sure that the current instruction will keep working even if the predecessor won't be transformed.
          */
         Instruction *pred_instr = ctx.info[op.tempId()].instr;
         assert(pred_instr->definitions.size() >= 2);
         assert(pred_instr->definitions[1].isFixed() && pred_instr->definitions[1].physReg() == scc);
         op.setTemp(pred_instr->definitions[1].getTemp());
      } else {
         unreachable("Invalid operand on uniform bitwise instruction.");
      }

      ctx.uses[op.tempId()]++;
   }

   instr->definitions[0].setTemp(Temp(instr->definitions[0].tempId(), s1));
   assert(instr->operands[0].regClass() == s1);
   assert(instr->operands[1].regClass() == s1);
   return true;
}

void select_instruction(opt_ctx &ctx, aco_ptr<Instruction>& instr)
{
   const uint32_t threshold = 4;

   if (is_dead(ctx.uses, instr.get())) {
      instr.reset();
      return;
   }

   /* convert split_vector into a copy or extract_vector if only one definition is ever used */
   if (instr->opcode == aco_opcode::p_split_vector) {
      unsigned num_used = 0;
      unsigned idx = 0;
      unsigned split_offset = 0;
      for (unsigned i = 0, offset = 0; i < instr->definitions.size(); offset += instr->definitions[i++].bytes()) {
         if (ctx.uses[instr->definitions[i].tempId()]) {
            num_used++;
            idx = i;
            split_offset = offset;
         }
      }
      bool done = false;
      if (num_used == 1 && ctx.info[instr->operands[0].tempId()].is_vec() &&
          ctx.uses[instr->operands[0].tempId()] == 1) {
         Instruction *vec = ctx.info[instr->operands[0].tempId()].instr;

         unsigned off = 0;
         Operand op;
         for (Operand& vec_op : vec->operands) {
            if (off == split_offset) {
               op = vec_op;
               break;
            }
            off += vec_op.bytes();
         }
         if (off != instr->operands[0].bytes() && op.bytes() == instr->definitions[idx].bytes()) {
            ctx.uses[instr->operands[0].tempId()]--;
            for (Operand& vec_op : vec->operands) {
               if (vec_op.isTemp())
                  ctx.uses[vec_op.tempId()]--;
            }
            if (op.isTemp())
               ctx.uses[op.tempId()]++;

            aco_ptr<Pseudo_instruction> extract{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, 1, 1)};
            extract->operands[0] = op;
            extract->definitions[0] = instr->definitions[idx];
            instr.reset(extract.release());

            done = true;
         }
      }

      if (!done && num_used == 1 &&
          instr->operands[0].bytes() % instr->definitions[idx].bytes() == 0 &&
          split_offset % instr->definitions[idx].bytes() == 0) {
         aco_ptr<Pseudo_instruction> extract{create_instruction<Pseudo_instruction>(aco_opcode::p_extract_vector, Format::PSEUDO, 2, 1)};
         extract->operands[0] = instr->operands[0];
         extract->operands[1] = Operand((uint32_t) split_offset / instr->definitions[idx].bytes());
         extract->definitions[0] = instr->definitions[idx];
         instr.reset(extract.release());
      }
   }

   mad_info* mad_info = NULL;
   if (!instr->definitions.empty() && ctx.info[instr->definitions[0].tempId()].is_mad()) {
      mad_info = &ctx.mad_infos[ctx.info[instr->definitions[0].tempId()].instr->pass_flags];
      /* re-check mad instructions */
      if (ctx.uses[mad_info->mul_temp_id]) {
         ctx.uses[mad_info->mul_temp_id]++;
         if (instr->operands[0].isTemp())
            ctx.uses[instr->operands[0].tempId()]--;
         if (instr->operands[1].isTemp())
            ctx.uses[instr->operands[1].tempId()]--;
         instr.swap(mad_info->add_instr);
         mad_info = NULL;
      }
      /* check literals */
      else if (!instr->usesModifiers()) {
         /* FMA can only take literals on GFX10+ */
         if ((instr->opcode == aco_opcode::v_fma_f32 || instr->opcode == aco_opcode::v_fma_f16) &&
             ctx.program->chip_class < GFX10)
            return;

         bool sgpr_used = false;
         uint32_t literal_idx = 0;
         uint32_t literal_uses = UINT32_MAX;
         for (unsigned i = 0; i < instr->operands.size(); i++)
         {
            if (instr->operands[i].isConstant() && i > 0) {
               literal_uses = UINT32_MAX;
               break;
            }
            if (!instr->operands[i].isTemp())
               continue;
            unsigned bits = get_operand_size(instr, i);
            /* if one of the operands is sgpr, we cannot add a literal somewhere else on pre-GFX10 or operands other than the 1st */
            if (instr->operands[i].getTemp().type() == RegType::sgpr && (i > 0 || ctx.program->chip_class < GFX10)) {
               if (!sgpr_used && ctx.info[instr->operands[i].tempId()].is_literal(bits)) {
                  literal_uses = ctx.uses[instr->operands[i].tempId()];
                  literal_idx = i;
               } else {
                  literal_uses = UINT32_MAX;
               }
               sgpr_used = true;
               /* don't break because we still need to check constants */
            } else if (!sgpr_used &&
                       ctx.info[instr->operands[i].tempId()].is_literal(bits) &&
                       ctx.uses[instr->operands[i].tempId()] < literal_uses) {
               literal_uses = ctx.uses[instr->operands[i].tempId()];
               literal_idx = i;
            }
         }

         /* Limit the number of literals to apply to not increase the code
          * size too much, but always apply literals for v_mad->v_madak
          * because both instructions are 64-bit and this doesn't increase
          * code size.
          * TODO: try to apply the literals earlier to lower the number of
          * uses below threshold
          */
         if (literal_uses < threshold || literal_idx == 2) {
            ctx.uses[instr->operands[literal_idx].tempId()]--;
            mad_info->check_literal = true;
            mad_info->literal_idx = literal_idx;
            return;
         }
      }
   }

   /* Mark SCC needed, so the uniform boolean transformation won't swap the definitions when it isn't beneficial */
   if (instr->format == Format::PSEUDO_BRANCH &&
       instr->operands.size() &&
       instr->operands[0].isTemp()) {
      ctx.info[instr->operands[0].tempId()].set_scc_needed();
      return;
   } else if ((instr->opcode == aco_opcode::s_cselect_b64 ||
               instr->opcode == aco_opcode::s_cselect_b32) &&
              instr->operands[2].isTemp()) {
      ctx.info[instr->operands[2].tempId()].set_scc_needed();
   }

   /* check for literals */
   if (!instr->isSALU() && !instr->isVALU())
      return;

   /* Transform uniform bitwise boolean operations to 32-bit when there are no divergent uses. */
   if (instr->definitions.size() &&
       ctx.uses[instr->definitions[0].tempId()] == 0 &&
       ctx.info[instr->definitions[0].tempId()].is_uniform_bitwise()) {
      bool transform_done = to_uniform_bool_instr(ctx, instr);

      if (transform_done && !ctx.info[instr->definitions[1].tempId()].is_scc_needed()) {
         /* Swap the two definition IDs in order to avoid overusing the SCC. This reduces extra moves generated by RA. */
         uint32_t def0_id = instr->definitions[0].getTemp().id();
         uint32_t def1_id = instr->definitions[1].getTemp().id();
         instr->definitions[0].setTemp(Temp(def1_id, s1));
         instr->definitions[1].setTemp(Temp(def0_id, s1));
      }

      return;
   }

   if (instr->isSDWA() || instr->isDPP() || (instr->isVOP3() && ctx.program->chip_class < GFX10))
      return; /* some encodings can't ever take literals */

   /* we do not apply the literals yet as we don't know if it is profitable */
   Operand current_literal(s1);

   unsigned literal_id = 0;
   unsigned literal_uses = UINT32_MAX;
   Operand literal(s1);
   unsigned num_operands = 1;
   if (instr->isSALU() || (ctx.program->chip_class >= GFX10 && can_use_VOP3(ctx, instr)))
      num_operands = instr->operands.size();
   /* catch VOP2 with a 3rd SGPR operand (e.g. v_cndmask_b32, v_addc_co_u32) */
   else if (instr->isVALU() && instr->operands.size() >= 3)
      return;

   unsigned sgpr_ids[2] = {0, 0};
   bool is_literal_sgpr = false;
   uint32_t mask = 0;

   /* choose a literal to apply */
   for (unsigned i = 0; i < num_operands; i++) {
      Operand op = instr->operands[i];
      unsigned bits = get_operand_size(instr, i);

      if (instr->isVALU() && op.isTemp() && op.getTemp().type() == RegType::sgpr &&
          op.tempId() != sgpr_ids[0])
         sgpr_ids[!!sgpr_ids[0]] = op.tempId();

      if (op.isLiteral()) {
         current_literal = op;
         continue;
      } else if (!op.isTemp() || !ctx.info[op.tempId()].is_literal(bits)) {
         continue;
      }

      if (!alu_can_accept_constant(instr->opcode, i))
         continue;

      if (ctx.uses[op.tempId()] < literal_uses) {
         is_literal_sgpr = op.getTemp().type() == RegType::sgpr;
         mask = 0;
         literal = Operand(ctx.info[op.tempId()].val);
         literal_uses = ctx.uses[op.tempId()];
         literal_id = op.tempId();
      }

      mask |= (op.tempId() == literal_id) << i;
   }


   /* don't go over the constant bus limit */
   bool is_shift64 = instr->opcode == aco_opcode::v_lshlrev_b64 ||
                     instr->opcode == aco_opcode::v_lshrrev_b64 ||
                     instr->opcode == aco_opcode::v_ashrrev_i64;
   unsigned const_bus_limit = instr->isVALU() ? 1 : UINT32_MAX;
   if (ctx.program->chip_class >= GFX10 && !is_shift64)
      const_bus_limit = 2;

   unsigned num_sgprs = !!sgpr_ids[0] + !!sgpr_ids[1];
   if (num_sgprs == const_bus_limit && !is_literal_sgpr)
      return;

   if (literal_id && literal_uses < threshold &&
       (current_literal.isUndefined() ||
        (current_literal.size() == literal.size() &&
         current_literal.constantValue() == literal.constantValue()))) {
      /* mark the literal to be applied */
      while (mask) {
         unsigned i = u_bit_scan(&mask);
         if (instr->operands[i].isTemp() && instr->operands[i].tempId() == literal_id)
            ctx.uses[instr->operands[i].tempId()]--;
      }
   }
}


void apply_literals(opt_ctx &ctx, aco_ptr<Instruction>& instr)
{
   /* Cleanup Dead Instructions */
   if (!instr)
      return;

   /* apply literals on MAD */
   if (!instr->definitions.empty() && ctx.info[instr->definitions[0].tempId()].is_mad()) {
      mad_info* info = &ctx.mad_infos[ctx.info[instr->definitions[0].tempId()].instr->pass_flags];
      if (info->check_literal &&
          (ctx.uses[instr->operands[info->literal_idx].tempId()] == 0 || info->literal_idx == 2)) {
         aco_ptr<Instruction> new_mad;

         aco_opcode new_op = info->literal_idx == 2 ? aco_opcode::v_madak_f32 : aco_opcode::v_madmk_f32;
         if (instr->opcode == aco_opcode::v_fma_f32)
            new_op = info->literal_idx == 2 ? aco_opcode::v_fmaak_f32 : aco_opcode::v_fmamk_f32;
         else if (instr->opcode == aco_opcode::v_mad_f16 || instr->opcode == aco_opcode::v_mad_legacy_f16)
            new_op = info->literal_idx == 2 ? aco_opcode::v_madak_f16 : aco_opcode::v_madmk_f16;
         else if (instr->opcode == aco_opcode::v_fma_f16)
            new_op = info->literal_idx == 2 ? aco_opcode::v_fmaak_f16 : aco_opcode::v_fmamk_f16;

         new_mad.reset(create_instruction<VOP2_instruction>(new_op, Format::VOP2, 3, 1));
         if (info->literal_idx == 2) { /* add literal -> madak */
            new_mad->operands[0] = instr->operands[0];
            new_mad->operands[1] = instr->operands[1];
         } else { /* mul literal -> madmk */
            new_mad->operands[0] = instr->operands[1 - info->literal_idx];
            new_mad->operands[1] = instr->operands[2];
         }
         new_mad->operands[2] = Operand(ctx.info[instr->operands[info->literal_idx].tempId()].val);
         new_mad->definitions[0] = instr->definitions[0];
         ctx.instructions.emplace_back(std::move(new_mad));
         return;
      }
   }

   /* apply literals on other SALU/VALU */
   if (instr->isSALU() || instr->isVALU()) {
      for (unsigned i = 0; i < instr->operands.size(); i++) {
         Operand op = instr->operands[i];
         unsigned bits = get_operand_size(instr, i);
         if (op.isTemp() && ctx.info[op.tempId()].is_literal(bits) && ctx.uses[op.tempId()] == 0) {
            Operand literal(ctx.info[op.tempId()].val);
            if (instr->isVALU() && i > 0)
               to_VOP3(ctx, instr);
            instr->operands[i] = literal;
         }
      }
   }

   ctx.instructions.emplace_back(std::move(instr));
}


void optimize(Program* program)
{
   opt_ctx ctx;
   ctx.program = program;
   std::vector<ssa_info> info(program->peekAllocationId());
   ctx.info = info.data();

   /* 1. Bottom-Up DAG pass (forward) to label all ssa-defs */
   for (Block& block : program->blocks) {
      for (aco_ptr<Instruction>& instr : block.instructions)
         label_instruction(ctx, block, instr);
   }

   ctx.uses = dead_code_analysis(program);

   /* 2. Combine v_mad, omod, clamp and propagate sgpr on VALU instructions */
   for (Block& block : program->blocks) {
      for (aco_ptr<Instruction>& instr : block.instructions)
         combine_instruction(ctx, block, instr);
   }

   /* 3. Top-Down DAG pass (backward) to select instructions (includes DCE) */
   for (std::vector<Block>::reverse_iterator it = program->blocks.rbegin(); it != program->blocks.rend(); ++it) {
      Block* block = &(*it);
      for (std::vector<aco_ptr<Instruction>>::reverse_iterator it = block->instructions.rbegin(); it != block->instructions.rend(); ++it)
         select_instruction(ctx, *it);
   }

   /* 4. Add literals to instructions */
   for (Block& block : program->blocks) {
      ctx.instructions.clear();
      for (aco_ptr<Instruction>& instr : block.instructions)
         apply_literals(ctx, instr);
      block.instructions.swap(ctx.instructions);
   }

}

}
