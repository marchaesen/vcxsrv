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
   uint32_t literal_idx;
   bool needs_vop3;
   bool check_literal;

   mad_info(aco_ptr<Instruction> instr, uint32_t id, bool vop3)
   : add_instr(std::move(instr)), mul_temp_id(id), needs_vop3(vop3), check_literal(false) {}
};

enum Label {
   label_vec = 1 << 0,
   label_constant = 1 << 1,
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
};

static constexpr uint32_t instr_labels = label_vec | label_mul | label_mad | label_omod_success | label_clamp_success | label_add_sub | label_bitwise | label_minmax | label_fcmp;
static constexpr uint32_t temp_labels = label_abs | label_neg | label_temp | label_vcc | label_b2f | label_uniform_bool;
static constexpr uint32_t val_labels = label_constant | label_literal | label_mad;

struct ssa_info {
   uint32_t val;
   union {
      Temp temp;
      Instruction* instr;
   };
   uint32_t label;

   void add_label(Label new_label)
   {
      /* Since all labels which use "instr" use it for the same thing
       * (indicating the defining instruction), there is no need to clear
       * any other instr labels. */
      if (new_label & instr_labels)
         label &= ~temp_labels; /* instr and temp alias */

      if (new_label & temp_labels) {
         label &= ~temp_labels;
         label &= ~instr_labels; /* instr and temp alias */
      }

      if (new_label & val_labels)
         label &= ~val_labels;

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

   void set_constant(uint32_t constant)
   {
      add_label(label_constant);
      val = constant;
   }

   bool is_constant()
   {
      return label & label_constant;
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

   void set_literal(uint32_t lit)
   {
      add_label(label_literal);
      val = lit;
   }

   bool is_literal()
   {
      return label & label_literal;
   }

   void set_mad(Instruction* mad, uint32_t mad_info_idx)
   {
      add_label(label_mad);
      val = mad_info_idx;
      instr = mad;
   }

   bool is_mad()
   {
      return label & label_mad;
   }

   void set_omod2()
   {
      add_label(label_omod2);
   }

   bool is_omod2()
   {
      return label & label_omod2;
   }

   void set_omod4()
   {
      add_label(label_omod4);
   }

   bool is_omod4()
   {
      return label & label_omod4;
   }

   void set_omod5()
   {
      add_label(label_omod5);
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

   void set_clamp()
   {
      add_label(label_clamp);
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

   bool is_constant_or_literal()
   {
      return is_constant() || is_literal();
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

   void set_uniform_bool(Temp uniform_bool)
   {
      add_label(label_uniform_bool);
      temp = uniform_bool;
   }

   bool is_uniform_bool()
   {
      return label & label_uniform_bool;
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

bool can_swap_operands(aco_ptr<Instruction>& instr)
{
   if (instr->operands[0].isConstant() ||
       (instr->operands[0].isTemp() && instr->operands[0].getTemp().type() == RegType::sgpr))
      return false;

   switch (instr->opcode) {
   case aco_opcode::v_add_f32:
   case aco_opcode::v_mul_f32:
   case aco_opcode::v_or_b32:
   case aco_opcode::v_and_b32:
   case aco_opcode::v_xor_b32:
   case aco_opcode::v_max_f32:
   case aco_opcode::v_min_f32:
   case aco_opcode::v_cmp_eq_f32:
   case aco_opcode::v_cmp_lg_f32:
      return true;
   case aco_opcode::v_sub_f32:
      instr->opcode = aco_opcode::v_subrev_f32;
      return true;
   case aco_opcode::v_cmp_lt_f32:
      instr->opcode = aco_opcode::v_cmp_gt_f32;
      return true;
   case aco_opcode::v_cmp_ge_f32:
      instr->opcode = aco_opcode::v_cmp_le_f32;
      return true;
   case aco_opcode::v_cmp_lt_i32:
      instr->opcode = aco_opcode::v_cmp_gt_i32;
      return true;
   default:
      return false;
   }
}

bool can_use_VOP3(aco_ptr<Instruction>& instr)
{
   if (instr->operands.size() && instr->operands[0].isLiteral())
      return false;

   if (instr->isDPP() || instr->isSDWA())
      return false;

   return instr->opcode != aco_opcode::v_madmk_f32 &&
          instr->opcode != aco_opcode::v_madak_f32 &&
          instr->opcode != aco_opcode::v_madmk_f16 &&
          instr->opcode != aco_opcode::v_madak_f16 &&
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

   assert(!instr->operands[0].isLiteral());
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
bool can_accept_constant(aco_ptr<Instruction>& instr, unsigned operand)
{
   switch (instr->opcode) {
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
      if ((instr->format == Format::MUBUF ||
           instr->format == Format::MIMG) &&
          instr->definitions.size() == 1 &&
          instr->operands.size() == 4) {
         return operand != 3;
      }
      return true;
   }
}

bool valu_can_accept_literal(opt_ctx& ctx, aco_ptr<Instruction>& instr, unsigned operand)
{
   /* instructions like v_cndmask_b32 can't take a literal because they always
    * read SGPRs */
   if (instr->operands.size() >= 3 &&
       instr->operands[2].isTemp() && instr->operands[2].regClass().type() == RegType::sgpr)
      return false;

   // TODO: VOP3 can take a literal on GFX10
   return !instr->isSDWA() && !instr->isDPP() && !instr->isVOP3() &&
          operand == 0 && can_accept_constant(instr, operand);
}

bool valu_can_accept_vgpr(aco_ptr<Instruction>& instr, unsigned operand)
{
   if (instr->opcode == aco_opcode::v_readlane_b32 || instr->opcode == aco_opcode::v_readlane_b32_e64 ||
       instr->opcode == aco_opcode::v_writelane_b32 || instr->opcode == aco_opcode::v_writelane_b32_e64)
      return operand != 1;
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
                 ctx.info[add_instr->operands[i].tempId()].is_constant_or_literal()) {
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

Operand get_constant_op(opt_ctx &ctx, uint32_t val)
{
   // TODO: this functions shouldn't be needed if we store Operand instead of value.
   Operand op(val);
   if (val == 0x3e22f983 && ctx.program->chip_class >= GFX8)
      op.setFixed(PhysReg{248}); /* 1/2 PI can be an inline constant on GFX8+ */
   return op;
}

void label_instruction(opt_ctx &ctx, Block& block, aco_ptr<Instruction>& instr)
{
   if (instr->isSALU() || instr->isVALU() || instr->format == Format::PSEUDO) {
      ASSERTED bool all_const = false;
      for (Operand& op : instr->operands)
         all_const = all_const && (!op.isTemp() || ctx.info[op.tempId()].is_constant_or_literal());
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
         if ((info.is_constant() || (info.is_literal() && instr->format == Format::PSEUDO)) && !instr->operands[i].isFixed() && can_accept_constant(instr, i)) {
            instr->operands[i] = get_constant_op(ctx, info.val);
            continue;
         }
      }

      /* VALU: propagate neg, abs & inline constants */
      else if (instr->isVALU()) {
         if (info.is_temp() && info.temp.type() == RegType::vgpr && valu_can_accept_vgpr(instr, i)) {
            instr->operands[i].setTemp(info.temp);
            info = ctx.info[info.temp.id()];
         }
         if (info.is_abs() && (can_use_VOP3(instr) || instr->isDPP()) && instr_info.can_use_input_modifiers[(int)instr->opcode]) {
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
         } else if (info.is_neg() && (can_use_VOP3(instr) || instr->isDPP()) && instr_info.can_use_input_modifiers[(int)instr->opcode]) {
            if (!instr->isDPP())
               to_VOP3(ctx, instr);
            instr->operands[i].setTemp(info.temp);
            if (instr->isDPP())
               static_cast<DPP_instruction*>(instr.get())->neg[i] = true;
            else
               static_cast<VOP3A_instruction*>(instr.get())->neg[i] = true;
            continue;
         }
         if (info.is_constant() && can_accept_constant(instr, i)) {
            perfwarn(instr->opcode == aco_opcode::v_cndmask_b32 && i == 2, "v_cndmask_b32 with a constant selector", instr.get());
            if (i == 0 || instr->opcode == aco_opcode::v_readlane_b32 || instr->opcode == aco_opcode::v_writelane_b32) {
               instr->operands[i] = get_constant_op(ctx, info.val);
               continue;
            } else if (!instr->isVOP3() && can_swap_operands(instr)) {
               instr->operands[i] = instr->operands[0];
               instr->operands[0] = get_constant_op(ctx, info.val);
               continue;
            } else if (can_use_VOP3(instr)) {
               to_VOP3(ctx, instr);
               instr->operands[i] = get_constant_op(ctx, info.val);
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

         if (mubuf->offen && i == 0 && info.is_constant_or_literal() && mubuf->offset + info.val < 4096) {
            assert(!mubuf->idxen);
            instr->operands[i] = Operand(v1);
            mubuf->offset += info.val;
            mubuf->offen = false;
            continue;
         } else if (i == 2 && info.is_constant_or_literal() && mubuf->offset + info.val < 4096) {
            instr->operands[2] = Operand((uint32_t) 0);
            mubuf->offset += info.val;
            continue;
         } else if (mubuf->offen && i == 0 && parse_base_offset(ctx, instr.get(), i, &base, &offset) && base.regClass() == v1 && mubuf->offset + offset < 4096) {
            assert(!mubuf->idxen);
            instr->operands[i].setTemp(base);
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
         if (i == 0 && parse_base_offset(ctx, instr.get(), i, &base, &offset) && base.regClass() == instr->operands[i].regClass()) {
            if (instr->opcode == aco_opcode::ds_write2_b32 || instr->opcode == aco_opcode::ds_read2_b32 ||
                instr->opcode == aco_opcode::ds_write2_b64 || instr->opcode == aco_opcode::ds_read2_b64) {
               if (offset % 4 == 0 &&
                   ds->offset0 + (offset >> 2) <= 255 &&
                   ds->offset1 + (offset >> 2) <= 255) {
                  instr->operands[i].setTemp(base);
                  ds->offset0 += offset >> 2;
                  ds->offset1 += offset >> 2;
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
         if (i == 1 && info.is_constant_or_literal() &&
             (ctx.program->chip_class < GFX8 || info.val <= 0xFFFFF)) {
            instr->operands[i] = Operand(info.val);
            continue;
         } else if (i == 1 && parse_base_offset(ctx, instr.get(), i, &base, &offset) && base.regClass() == s1 && offset <= 0xFFFFF && ctx.program->chip_class >= GFX9) {
            bool soe = smem->operands.size() >= (!smem->definitions.empty() ? 3 : 4);
            if (soe &&
                (!ctx.info[smem->operands.back().tempId()].is_constant_or_literal() ||
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
               instr.reset(new_instr);
               smem = static_cast<SMEM_instruction *>(instr.get());
            }
            continue;
         }
      }
   }

   /* if this instruction doesn't define anything, return */
   if (instr->definitions.empty())
      return;

   switch (instr->opcode) {
   case aco_opcode::p_create_vector: {
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
      if (instr->operands.size() == 1 && instr->operands[0].isTemp())
         ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
      else if (instr->definitions[0].getTemp().size() == instr->operands.size())
         ctx.info[instr->definitions[0].tempId()].set_vec(instr.get());
      break;
   }
   case aco_opcode::p_split_vector: {
      if (!ctx.info[instr->operands[0].tempId()].is_vec())
         break;
      Instruction* vec = ctx.info[instr->operands[0].tempId()].instr;
      assert(instr->definitions.size() == vec->operands.size());
      for (unsigned i = 0; i < instr->definitions.size(); i++) {
         Operand vec_op = vec->operands[i];
         if (vec_op.isConstant()) {
            if (vec_op.isLiteral())
               ctx.info[instr->definitions[i].tempId()].set_literal(vec_op.constantValue());
            else if (vec_op.size() == 1)
               ctx.info[instr->definitions[i].tempId()].set_constant(vec_op.constantValue());
         } else {
            assert(vec_op.isTemp());
            ctx.info[instr->definitions[i].tempId()].set_temp(vec_op.getTemp());
         }
      }
      break;
   }
   case aco_opcode::p_extract_vector: { /* mov */
      if (!ctx.info[instr->operands[0].tempId()].is_vec())
         break;
      Instruction* vec = ctx.info[instr->operands[0].tempId()].instr;
      if (vec->definitions[0].getTemp().size() == vec->operands.size() && /* TODO: what about 64bit or other combinations? */
          vec->operands[0].size() == instr->definitions[0].size()) {

         /* convert this extract into a mov instruction */
         Operand vec_op = vec->operands[instr->operands[1].constantValue()];
         bool is_vgpr = instr->definitions[0].getTemp().type() == RegType::vgpr;
         aco_opcode opcode = is_vgpr ? aco_opcode::v_mov_b32 : aco_opcode::s_mov_b32;
         Format format = is_vgpr ? Format::VOP1 : Format::SOP1;
         instr->opcode = opcode;
         instr->format = format;
         instr->operands = {instr->operands.begin(), 1 };
         instr->operands[0] = vec_op;

         if (vec_op.isConstant()) {
            if (vec_op.isLiteral())
               ctx.info[instr->definitions[0].tempId()].set_literal(vec_op.constantValue());
            else if (vec_op.size() == 1)
               ctx.info[instr->definitions[0].tempId()].set_constant(vec_op.constantValue());
         } else {
            assert(vec_op.isTemp());
            ctx.info[instr->definitions[0].tempId()].set_temp(vec_op.getTemp());
         }
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
         if (instr->operands[0].isLiteral())
            ctx.info[instr->definitions[0].tempId()].set_literal(instr->operands[0].constantValue());
         else if (instr->operands[0].size() == 1)
            ctx.info[instr->definitions[0].tempId()].set_constant(instr->operands[0].constantValue());
      } else if (instr->operands[0].isTemp()) {
         ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
      } else {
         assert(instr->operands[0].isFixed());
      }
      break;
   case aco_opcode::p_is_helper:
      if (!ctx.program->needs_wqm)
         ctx.info[instr->definitions[0].tempId()].set_constant(0u);
      break;
   case aco_opcode::s_movk_i32: {
      uint32_t v = static_cast<SOPK_instruction*>(instr.get())->imm;
      v = v & 0x8000 ? (v | 0xffff0000) : v;
      if (v <= 64 || v >= 0xfffffff0)
         ctx.info[instr->definitions[0].tempId()].set_constant(v);
      else
         ctx.info[instr->definitions[0].tempId()].set_literal(v);
      break;
   }
   case aco_opcode::v_bfrev_b32:
   case aco_opcode::s_brev_b32: {
      if (instr->operands[0].isConstant()) {
         uint32_t v = util_bitreverse(instr->operands[0].constantValue());
         if (v <= 64 || v >= 0xfffffff0)
            ctx.info[instr->definitions[0].tempId()].set_constant(v);
         else
            ctx.info[instr->definitions[0].tempId()].set_literal(v);
      }
      break;
   }
   case aco_opcode::s_bfm_b32: {
      if (instr->operands[0].isConstant() && instr->operands[1].isConstant()) {
         unsigned size = instr->operands[0].constantValue() & 0x1f;
         unsigned start = instr->operands[1].constantValue() & 0x1f;
         uint32_t v = ((1u << size) - 1u) << start;
         if (v <= 64 || v >= 0xfffffff0)
            ctx.info[instr->definitions[0].tempId()].set_constant(v);
         else
            ctx.info[instr->definitions[0].tempId()].set_literal(v);
      }
   }
   case aco_opcode::v_mul_f32: { /* omod */
      /* TODO: try to move the negate/abs modifier to the consumer instead */
      if (instr->usesModifiers())
         break;

      for (unsigned i = 0; i < 2; i++) {
         if (instr->operands[!i].isConstant() && instr->operands[i].isTemp()) {
            if (instr->operands[!i].constantValue() == 0x40000000) { /* 2.0 */
               ctx.info[instr->operands[i].tempId()].set_omod2();
            } else if (instr->operands[!i].constantValue() == 0x40800000) { /* 4.0 */
               ctx.info[instr->operands[i].tempId()].set_omod4();
            } else if (instr->operands[!i].constantValue() == 0x3f000000) { /* 0.5 */
               ctx.info[instr->operands[i].tempId()].set_omod5();
            } else if (instr->operands[!i].constantValue() == 0x3f800000 &&
                       !block.fp_mode.must_flush_denorms32) { /* 1.0 */
               ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[i].getTemp());
            } else {
               continue;
            }
            break;
         }
      }
      break;
   }
   case aco_opcode::v_and_b32: /* abs */
      if (instr->operands[0].constantEquals(0x7FFFFFFF) && instr->operands[1].isTemp())
         ctx.info[instr->definitions[0].tempId()].set_abs(instr->operands[1].getTemp());
      else
         ctx.info[instr->definitions[0].tempId()].set_bitwise(instr.get());
      break;
   case aco_opcode::v_xor_b32: { /* neg */
      if (instr->operands[0].constantEquals(0x80000000u) && instr->operands[1].isTemp()) {
         if (ctx.info[instr->operands[1].tempId()].is_neg()) {
            ctx.info[instr->definitions[0].tempId()].set_temp(ctx.info[instr->operands[1].tempId()].temp);
         } else {
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
   case aco_opcode::v_med3_f32: { /* clamp */
      VOP3A_instruction* vop3 = static_cast<VOP3A_instruction*>(instr.get());
      if (vop3->abs[0] || vop3->neg[0] || vop3->opsel[0] ||
          vop3->abs[1] || vop3->neg[1] || vop3->opsel[1] ||
          vop3->abs[2] || vop3->neg[2] || vop3->opsel[2] ||
          vop3->omod != 0)
         break;

      unsigned idx = 0;
      bool found_zero = false, found_one = false;
      for (unsigned i = 0; i < 3; i++)
      {
         if (instr->operands[i].constantEquals(0))
            found_zero = true;
         else if (instr->operands[i].constantEquals(0x3f800000)) /* 1.0 */
            found_one = true;
         else
            idx = i;
      }
      if (found_zero && found_one && instr->operands[idx].isTemp()) {
         ctx.info[instr->operands[idx].tempId()].set_clamp();
      }
      break;
   }
   case aco_opcode::v_cndmask_b32:
      if (instr->operands[0].constantEquals(0) &&
          instr->operands[1].constantEquals(0xFFFFFFFF) &&
          instr->operands[2].isTemp())
         ctx.info[instr->definitions[0].tempId()].set_vcc(instr->operands[2].getTemp());
      else if (instr->operands[0].constantEquals(0) &&
               instr->operands[1].constantEquals(0x3f800000u) &&
               instr->operands[2].isTemp())
         ctx.info[instr->definitions[0].tempId()].set_b2f(instr->operands[2].getTemp());
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
   case aco_opcode::s_add_i32:
   case aco_opcode::s_add_u32:
      ctx.info[instr->definitions[0].tempId()].set_add_sub(instr.get());
      break;
   case aco_opcode::s_and_b32:
   case aco_opcode::s_and_b64:
      if (instr->operands[1].isFixed() && instr->operands[1].physReg() == exec &&
          instr->operands[0].isTemp() && ctx.info[instr->operands[0].tempId()].is_uniform_bool()) {
         ctx.info[instr->definitions[1].tempId()].set_temp(ctx.info[instr->operands[0].tempId()].temp);
      }
      /* fallthrough */
   case aco_opcode::s_not_b32:
   case aco_opcode::s_not_b64:
   case aco_opcode::s_or_b32:
   case aco_opcode::s_or_b64:
   case aco_opcode::s_xor_b32:
   case aco_opcode::s_xor_b64:
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
   case aco_opcode::v_cmp_lt_f32:
   case aco_opcode::v_cmp_eq_f32:
   case aco_opcode::v_cmp_le_f32:
   case aco_opcode::v_cmp_gt_f32:
   case aco_opcode::v_cmp_lg_f32:
   case aco_opcode::v_cmp_ge_f32:
   case aco_opcode::v_cmp_o_f32:
   case aco_opcode::v_cmp_u_f32:
   case aco_opcode::v_cmp_nge_f32:
   case aco_opcode::v_cmp_nlg_f32:
   case aco_opcode::v_cmp_ngt_f32:
   case aco_opcode::v_cmp_nle_f32:
   case aco_opcode::v_cmp_neq_f32:
   case aco_opcode::v_cmp_nlt_f32:
      ctx.info[instr->definitions[0].tempId()].set_fcmp(instr.get());
      break;
   case aco_opcode::s_cselect_b64:
   case aco_opcode::s_cselect_b32:
      if (instr->operands[0].constantEquals((unsigned) -1) &&
          instr->operands[1].constantEquals(0)) {
         /* Found a cselect that operates on a uniform bool that comes from eg. s_cmp */
         ctx.info[instr->definitions[0].tempId()].set_uniform_bool(instr->operands[2].getTemp());
      }
      break;
   default:
      break;
   }
}

ALWAYS_INLINE bool get_cmp_info(aco_opcode op, aco_opcode *ordered, aco_opcode *unordered, aco_opcode *inverse)
{
   *ordered = *unordered = op;
   switch (op) {
   #define CMP(ord, unord) \
   case aco_opcode::v_cmp_##ord##_f32:\
   case aco_opcode::v_cmp_n##unord##_f32:\
      *ordered = aco_opcode::v_cmp_##ord##_f32;\
      *unordered = aco_opcode::v_cmp_n##unord##_f32;\
      *inverse = op == aco_opcode::v_cmp_n##unord##_f32 ? aco_opcode::v_cmp_##unord##_f32 : aco_opcode::v_cmp_n##ord##_f32;\
      return true;
   CMP(lt, /*n*/ge)
   CMP(eq, /*n*/lg)
   CMP(le, /*n*/gt)
   CMP(gt, /*n*/le)
   CMP(lg, /*n*/eq)
   CMP(ge, /*n*/lt)
   #undef CMP
   default:
      return false;
   }
}

aco_opcode get_ordered(aco_opcode op)
{
   aco_opcode ordered, unordered, inverse;
   return get_cmp_info(op, &ordered, &unordered, &inverse) ? ordered : aco_opcode::last_opcode;
}

aco_opcode get_unordered(aco_opcode op)
{
   aco_opcode ordered, unordered, inverse;
   return get_cmp_info(op, &ordered, &unordered, &inverse) ? unordered : aco_opcode::last_opcode;
}

aco_opcode get_inverse(aco_opcode op)
{
   aco_opcode ordered, unordered, inverse;
   return get_cmp_info(op, &ordered, &unordered, &inverse) ? inverse : aco_opcode::last_opcode;
}

bool is_cmp(aco_opcode op)
{
   aco_opcode ordered, unordered, inverse;
   return get_cmp_info(op, &ordered, &unordered, &inverse);
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
   bool opsel[2] = {false, false};
   Instruction *op_instr[2];
   Temp op[2];

   for (unsigned i = 0; i < 2; i++) {
      op_instr[i] = follow_operand(ctx, instr->operands[i], true);
      if (!op_instr[i])
         return false;

      aco_opcode expected_cmp = is_or ? aco_opcode::v_cmp_neq_f32 : aco_opcode::v_cmp_eq_f32;

      if (op_instr[i]->opcode != expected_cmp)
         return false;
      if (!op_instr[i]->operands[0].isTemp() || !op_instr[i]->operands[1].isTemp())
         return false;

      if (op_instr[i]->isVOP3()) {
         VOP3A_instruction *vop3 = static_cast<VOP3A_instruction*>(op_instr[i]);
         if (vop3->neg[0] != vop3->neg[1] || vop3->abs[0] != vop3->abs[1] || vop3->opsel[0] != vop3->opsel[1])
            return false;
         neg[i] = vop3->neg[0];
         abs[i] = vop3->abs[0];
         opsel[i] = vop3->opsel[0];
      }

      Temp op0 = op_instr[i]->operands[0].getTemp();
      Temp op1 = op_instr[i]->operands[1].getTemp();
      if (original_temp_id(ctx, op0) != original_temp_id(ctx, op1))
         return false;
      /* shouldn't happen yet, but best to be safe */
      if (op1.type() != RegType::vgpr)
         return false;

      op[i] = op1;
   }

   ctx.uses[op[0].id()]++;
   ctx.uses[op[1].id()]++;
   decrease_uses(ctx, op_instr[0]);
   decrease_uses(ctx, op_instr[1]);

   aco_opcode new_op = is_or ? aco_opcode::v_cmp_u_f32 : aco_opcode::v_cmp_o_f32;
   Instruction *new_instr;
   if (neg[0] || neg[1] || abs[0] || abs[1] || opsel[0] || opsel[1]) {
      VOP3A_instruction *vop3 = create_instruction<VOP3A_instruction>(new_op, asVOP3(Format::VOPC), 2, 1);
      for (unsigned i = 0; i < 2; i++) {
         vop3->neg[i] = neg[i];
         vop3->abs[i] = abs[i];
         vop3->opsel[i] = opsel[i];
      }
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

   if (cmp->opcode == expected_nan_test)
      std::swap(nan_test, cmp);
   else if (nan_test->opcode != expected_nan_test)
      return false;

   if (!is_cmp(cmp->opcode))
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
      memcpy(new_vop3->opsel, cmp_vop3->opsel, sizeof(new_vop3->opsel));
      memcpy(new_vop3->neg, cmp_vop3->neg, sizeof(new_vop3->neg));
      new_vop3->clamp = cmp_vop3->clamp;
      new_vop3->omod = cmp_vop3->omod;
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
   if (cmp->opcode == expected_nan_test)
      std::swap(nan_test, cmp);
   else if (nan_test->opcode != expected_nan_test)
      return false;

   if (!is_cmp(cmp->opcode))
      return false;

   if (!nan_test->operands[0].isTemp() || !nan_test->operands[1].isTemp())
      return false;
   if (!cmp->operands[0].isTemp() && !cmp->operands[1].isTemp())
      return false;

   unsigned prop_nan0 = original_temp_id(ctx, nan_test->operands[0].getTemp());
   unsigned prop_nan1 = original_temp_id(ctx, nan_test->operands[1].getTemp());
   if (prop_nan0 != prop_nan1)
      return false;

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
      unsigned id = cmp->operands[constant_operand].tempId();
      if (!ctx.info[id].is_constant() && !ctx.info[id].is_literal())
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
      memcpy(new_vop3->opsel, cmp_vop3->opsel, sizeof(new_vop3->opsel));
      memcpy(new_vop3->neg, cmp_vop3->neg, sizeof(new_vop3->neg));
      new_vop3->clamp = cmp_vop3->clamp;
      new_vop3->omod = cmp_vop3->omod;
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
   if (new_opcode == aco_opcode::last_opcode)
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
      memcpy(new_vop3->opsel, cmp_vop3->opsel, sizeof(new_vop3->opsel));
      memcpy(new_vop3->neg, cmp_vop3->neg, sizeof(new_vop3->neg));
      new_vop3->clamp = cmp_vop3->clamp;
      new_vop3->omod = cmp_vop3->omod;
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
                        Operand operands[3], bool neg[3], bool abs[3], bool opsel[3],
                        bool *op1_clamp, unsigned *op1_omod,
                        bool *inbetween_neg, bool *inbetween_abs, bool *inbetween_opsel)
{
   /* checks */
   if (op1_instr->opcode != op1)
      return false;

   Instruction *op2_instr = follow_operand(ctx, op1_instr->operands[swap]);
   if (!op2_instr || op2_instr->opcode != op2)
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
      *inbetween_opsel = op1_vop3 ? op1_vop3->opsel[swap] : false;
   else if (op1_vop3 && op1_vop3->opsel[swap])
      return false;

   int shuffle[3];
   shuffle[shuffle_str[0] - '0'] = 0;
   shuffle[shuffle_str[1] - '0'] = 1;
   shuffle[shuffle_str[2] - '0'] = 2;

   operands[shuffle[0]] = op1_instr->operands[!swap];
   neg[shuffle[0]] = op1_vop3 ? op1_vop3->neg[!swap] : false;
   abs[shuffle[0]] = op1_vop3 ? op1_vop3->abs[!swap] : false;
   opsel[shuffle[0]] = op1_vop3 ? op1_vop3->opsel[!swap] : false;

   for (unsigned i = 0; i < 2; i++) {
      operands[shuffle[i + 1]] = op2_instr->operands[i];
      neg[shuffle[i + 1]] = op2_vop3 ? op2_vop3->neg[i] : false;
      abs[shuffle[i + 1]] = op2_vop3 ? op2_vop3->abs[i] : false;
      opsel[shuffle[i + 1]] = op2_vop3 ? op2_vop3->opsel[i] : false;
   }

   /* check operands */
   unsigned sgpr_id = 0;
   for (unsigned i = 0; i < 3; i++) {
      Operand op = operands[i];
      if (op.isLiteral()) {
         return false;
      } else if (op.isTemp() && op.getTemp().type() == RegType::sgpr) {
         if (sgpr_id && sgpr_id != op.tempId())
            return false;
         sgpr_id = op.tempId();
      }
   }

   return true;
}

void create_vop3_for_op3(opt_ctx& ctx, aco_opcode opcode, aco_ptr<Instruction>& instr,
                         Operand operands[3], bool neg[3], bool abs[3], bool opsel[3],
                         bool clamp, unsigned omod)
{
   VOP3A_instruction *new_instr = create_instruction<VOP3A_instruction>(opcode, Format::VOP3A, 3, 1);
   memcpy(new_instr->abs, abs, sizeof(bool[3]));
   memcpy(new_instr->opsel, opsel, sizeof(bool[3]));
   memcpy(new_instr->neg, neg, sizeof(bool[3]));
   new_instr->clamp = clamp;
   new_instr->omod = omod;
   new_instr->operands[0] = operands[0];
   new_instr->operands[1] = operands[1];
   new_instr->operands[2] = operands[2];
   new_instr->definitions[0] = instr->definitions[0];
   ctx.info[instr->definitions[0].tempId()].label = 0;

   instr.reset(new_instr);
}

bool combine_three_valu_op(opt_ctx& ctx, aco_ptr<Instruction>& instr, aco_opcode op2, aco_opcode new_op, const char *shuffle, uint8_t ops)
{
   uint32_t omod_clamp = ctx.info[instr->definitions[0].tempId()].label &
                         (label_omod_success | label_clamp_success);

   for (unsigned swap = 0; swap < 2; swap++) {
      if (!((1 << swap) & ops))
         continue;

      Operand operands[3];
      bool neg[3], abs[3], opsel[3], clamp;
      unsigned omod;
      if (match_op3_for_vop3(ctx, instr->opcode, op2,
                             instr.get(), swap, shuffle,
                             operands, neg, abs, opsel,
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
   if (instr->definitions[1].isTemp() && ctx.uses[instr->definitions[1].tempId()])
      return false;

   for (unsigned i = 0; i < 2; i++) {
      Instruction *op2_instr = follow_operand(ctx, instr->operands[i]);
      if (!op2_instr || (op2_instr->opcode != aco_opcode::s_not_b32 && op2_instr->opcode != aco_opcode::s_not_b64))
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
   if (instr->definitions[1].isTemp() && ctx.uses[instr->definitions[1].tempId()])
      return false;

   for (unsigned i = 0; i < 2; i++) {
      Instruction *op2_instr = follow_operand(ctx, instr->operands[i]);
      if (!op2_instr || op2_instr->opcode != aco_opcode::s_lshl_b32 || !op2_instr->operands[1].isConstant())
         continue;

      uint32_t shift = op2_instr->operands[1].constantValue();
      if (shift < 1 || shift > 4)
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
   aco_opcode other_op;
   if (instr->opcode == min)
      other_op = max;
   else if (instr->opcode == max)
      other_op = min;
   else
      return false;

   uint32_t omod_clamp = ctx.info[instr->definitions[0].tempId()].label &
                         (label_omod_success | label_clamp_success);

   for (unsigned swap = 0; swap < 2; swap++) {
      Operand operands[3];
      bool neg[3], abs[3], opsel[3], clamp, inbetween_neg, inbetween_abs;
      unsigned omod;
      if (match_op3_for_vop3(ctx, instr->opcode, other_op, instr.get(), swap,
                             "012", operands, neg, abs, opsel,
                             &clamp, &omod, &inbetween_neg, &inbetween_abs, NULL)) {
         int const0_idx = -1, const1_idx = -1;
         uint32_t const0 = 0, const1 = 0;
         for (int i = 0; i < 3; i++) {
            uint32_t val;
            if (operands[i].isConstant()) {
               val = operands[i].constantValue();
            } else if (operands[i].isTemp() && ctx.uses[operands[i].tempId()] == 1 &&
                       ctx.info[operands[i].tempId()].is_constant_or_literal()) {
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

         if (opsel[const0_idx])
            const0 >>= 16;
         if (opsel[const1_idx])
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

         neg[1] ^= inbetween_neg;
         neg[2] ^= inbetween_neg;
         abs[1] |= inbetween_abs;
         abs[2] |= inbetween_abs;

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
   /* apply sgprs */
   uint32_t sgpr_idx = 0;
   uint32_t sgpr_info_id = 0;
   bool has_sgpr = false;
   uint32_t sgpr_ssa_id = 0;
   /* find 'best' possible sgpr */
   for (unsigned i = 0; i < instr->operands.size(); i++)
   {
      if (instr->operands[i].isLiteral()) {
         has_sgpr = true;
         break;
      }
      if (!instr->operands[i].isTemp())
         continue;
      if (instr->operands[i].getTemp().type() == RegType::sgpr) {
         has_sgpr = true;
         sgpr_ssa_id = instr->operands[i].tempId();
         continue;
      }
      ssa_info& info = ctx.info[instr->operands[i].tempId()];
      if (info.is_temp() && info.temp.type() == RegType::sgpr) {
         uint16_t uses = ctx.uses[instr->operands[i].tempId()];
         if (sgpr_info_id == 0 || uses < ctx.uses[sgpr_info_id]) {
            sgpr_idx = i;
            sgpr_info_id = instr->operands[i].tempId();
         }
      }
   }
   if (!has_sgpr && sgpr_info_id != 0) {
      ssa_info& info = ctx.info[sgpr_info_id];
      if (sgpr_idx == 0 || instr->isVOP3()) {
         instr->operands[sgpr_idx] = Operand(info.temp);
         ctx.uses[sgpr_info_id]--;
         ctx.uses[info.temp.id()]++;
      } else if (can_swap_operands(instr)) {
         instr->operands[sgpr_idx] = instr->operands[0];
         instr->operands[0] = Operand(info.temp);
         ctx.uses[sgpr_info_id]--;
         ctx.uses[info.temp.id()]++;
      } else if (can_use_VOP3(instr)) {
         to_VOP3(ctx, instr);
         instr->operands[sgpr_idx] = Operand(info.temp);
         ctx.uses[sgpr_info_id]--;
         ctx.uses[info.temp.id()]++;
      }

   /* we can have two sgprs on one instruction if it is the same sgpr! */
   } else if (sgpr_info_id != 0 &&
              sgpr_ssa_id == sgpr_info_id &&
              ctx.uses[sgpr_info_id] == 1 &&
              can_use_VOP3(instr)) {
      to_VOP3(ctx, instr);
      instr->operands[sgpr_idx] = Operand(ctx.info[sgpr_info_id].temp);
      ctx.uses[sgpr_info_id]--;
      ctx.uses[ctx.info[sgpr_info_id].temp.id()]++;
   }
}

bool apply_omod_clamp(opt_ctx &ctx, Block& block, aco_ptr<Instruction>& instr)
{
   /* check if we could apply omod on predecessor */
   if (instr->opcode == aco_opcode::v_mul_f32) {
      if (instr->operands[1].isTemp() && ctx.info[instr->operands[1].tempId()].is_omod_success()) {

         /* omod was successfully applied */
         /* if the omod instruction is v_mad, we also have to change the original add */
         if (ctx.info[instr->operands[1].tempId()].is_mad()) {
            Instruction* add_instr = ctx.mad_infos[ctx.info[instr->operands[1].tempId()].val].add_instr.get();
            if (ctx.info[instr->definitions[0].tempId()].is_clamp())
               static_cast<VOP3A_instruction*>(add_instr)->clamp = true;
            add_instr->definitions[0] = instr->definitions[0];
         }

         Instruction* omod_instr = ctx.info[instr->operands[1].tempId()].instr;
         /* check if we have an additional clamp modifier */
         if (ctx.info[instr->definitions[0].tempId()].is_clamp() && ctx.uses[instr->definitions[0].tempId()] == 1) {
            static_cast<VOP3A_instruction*>(omod_instr)->clamp = true;
            ctx.info[instr->definitions[0].tempId()].set_clamp_success(omod_instr);
         }
         /* change definition ssa-id of modified instruction */
         omod_instr->definitions[0] = instr->definitions[0];

         /* change the definition of instr to something unused, e.g. the original omod def */
         instr->definitions[0] = Definition(instr->operands[1].getTemp());
         ctx.uses[instr->definitions[0].tempId()] = 0;
         return true;
      }
      if (!ctx.info[instr->definitions[0].tempId()].label) {
         /* in all other cases, label this instruction as option for multiply-add */
         ctx.info[instr->definitions[0].tempId()].set_mul(instr.get());
      }
   }

   /* check if we could apply clamp on predecessor */
   if (instr->opcode == aco_opcode::v_med3_f32) {
      unsigned idx = 0;
      bool found_zero = false, found_one = false;
      for (unsigned i = 0; i < 3; i++)
      {
         if (instr->operands[i].constantEquals(0))
            found_zero = true;
         else if (instr->operands[i].constantEquals(0x3f800000)) /* 1.0 */
            found_one = true;
         else
            idx = i;
      }
      if (found_zero && found_one && instr->operands[idx].isTemp() &&
          ctx.info[instr->operands[idx].tempId()].is_clamp_success()) {
         /* clamp was successfully applied */
         /* if the clamp instruction is v_mad, we also have to change the original add */
         if (ctx.info[instr->operands[idx].tempId()].is_mad()) {
            Instruction* add_instr = ctx.mad_infos[ctx.info[instr->operands[idx].tempId()].val].add_instr.get();
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
   bool can_use_omod = block.fp_mode.denorm32 == 0;

   /* apply omod / clamp modifiers if the def is used only once and the instruction can have modifiers */
   if (!instr->definitions.empty() && ctx.uses[instr->definitions[0].tempId()] == 1 &&
       can_use_VOP3(instr) && instr_info.can_use_output_modifiers[(int)instr->opcode]) {
      if (can_use_omod && ctx.info[instr->definitions[0].tempId()].is_omod2()) {
         to_VOP3(ctx, instr);
         static_cast<VOP3A_instruction*>(instr.get())->omod = 1;
         ctx.info[instr->definitions[0].tempId()].set_omod_success(instr.get());
      } else if (can_use_omod && ctx.info[instr->definitions[0].tempId()].is_omod4()) {
         to_VOP3(ctx, instr);
         static_cast<VOP3A_instruction*>(instr.get())->omod = 2;
         ctx.info[instr->definitions[0].tempId()].set_omod_success(instr.get());
      } else if (can_use_omod && ctx.info[instr->definitions[0].tempId()].is_omod5()) {
         to_VOP3(ctx, instr);
         static_cast<VOP3A_instruction*>(instr.get())->omod = 3;
         ctx.info[instr->definitions[0].tempId()].set_omod_success(instr.get());
      } else if (ctx.info[instr->definitions[0].tempId()].is_clamp()) {
         to_VOP3(ctx, instr);
         static_cast<VOP3A_instruction*>(instr.get())->clamp = true;
         ctx.info[instr->definitions[0].tempId()].set_clamp_success(instr.get());
      }
   }

   return false;
}

// TODO: we could possibly move the whole label_instruction pass to combine_instruction:
// this would mean that we'd have to fix the instruction uses while value propagation

void combine_instruction(opt_ctx &ctx, Block& block, aco_ptr<Instruction>& instr)
{
   if (instr->definitions.empty() || !ctx.uses[instr->definitions[0].tempId()])
      return;

   if (instr->isVALU()) {
      if (can_apply_sgprs(instr))
         apply_sgprs(ctx, instr);
      if (apply_omod_clamp(ctx, block, instr))
         return;
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
      instr.reset(create_instruction<VOP3A_instruction>(aco_opcode::v_mul_f32, asVOP3(Format::VOP2), 2, 1));
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
   else if ((instr->opcode == aco_opcode::v_add_f32 ||
             instr->opcode == aco_opcode::v_sub_f32 ||
             instr->opcode == aco_opcode::v_subrev_f32) &&
            block.fp_mode.denorm32 == 0 && !block.fp_mode.preserve_signed_zero_inf_nan32) {
      //TODO: we could use fma instead when denormals are enabled if the NIR isn't marked as precise

      uint32_t uses_src0 = UINT32_MAX;
      uint32_t uses_src1 = UINT32_MAX;
      Instruction* mul_instr = nullptr;
      unsigned add_op_idx;
      /* check if any of the operands is a multiplication */
      if (instr->operands[0].isTemp() && ctx.info[instr->operands[0].tempId()].is_mul())
         uses_src0 = ctx.uses[instr->operands[0].tempId()];
      if (instr->operands[1].isTemp() && ctx.info[instr->operands[1].tempId()].is_mul())
         uses_src1 = ctx.uses[instr->operands[1].tempId()];

      /* find the 'best' mul instruction to combine with the add */
      if (uses_src0 < uses_src1) {
         mul_instr = ctx.info[instr->operands[0].tempId()].instr;
         add_op_idx = 1;
      } else if (uses_src1 < uses_src0) {
         mul_instr = ctx.info[instr->operands[1].tempId()].instr;
         add_op_idx = 0;
      } else if (uses_src0 != UINT32_MAX) {
         /* tiebreaker: quite random what to pick */
         if (ctx.info[instr->operands[0].tempId()].instr->operands[0].isLiteral()) {
            mul_instr = ctx.info[instr->operands[1].tempId()].instr;
            add_op_idx = 0;
         } else {
            mul_instr = ctx.info[instr->operands[0].tempId()].instr;
            add_op_idx = 1;
         }
      }
      if (mul_instr) {
         Operand op[3] = {Operand(v1), Operand(v1), Operand(v1)};
         bool neg[3] = {false, false, false};
         bool abs[3] = {false, false, false};
         unsigned omod = 0;
         bool clamp = false;
         bool need_vop3 = false;
         int num_sgpr = 0;
         op[0] = mul_instr->operands[0];
         op[1] = mul_instr->operands[1];
         op[2] = instr->operands[add_op_idx];
         for (unsigned i = 0; i < 3; i++)
         {
            if (op[i].isLiteral())
               return;
            if (op[i].isTemp() && op[i].getTemp().type() == RegType::sgpr)
               num_sgpr++;
            if (!(i == 0 || (op[i].isTemp() && op[i].getTemp().type() == RegType::vgpr)))
               need_vop3 = true;
         }
         // TODO: would be better to check this before selecting a mul instr?
         if (num_sgpr > 1)
            return;

         if (mul_instr->isVOP3()) {
            VOP3A_instruction* vop3 = static_cast<VOP3A_instruction*> (mul_instr);
            neg[0] = vop3->neg[0];
            neg[1] = vop3->neg[1];
            abs[0] = vop3->abs[0];
            abs[1] = vop3->abs[1];
            need_vop3 = true;
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
            need_vop3 = true;
         }
         if (instr->opcode == aco_opcode::v_sub_f32) {
            neg[1 + add_op_idx] = neg[1 + add_op_idx] ^ true;
            need_vop3 = true;
         } else if (instr->opcode == aco_opcode::v_subrev_f32) {
            neg[2 - add_op_idx] = neg[2 - add_op_idx] ^ true;
            need_vop3 = true;
         }

         aco_ptr<VOP3A_instruction> mad{create_instruction<VOP3A_instruction>(aco_opcode::v_mad_f32, Format::VOP3A, 3, 1)};
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
         ctx.mad_infos.emplace_back(std::move(instr), mul_instr->definitions[0].tempId(), need_vop3);
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
      if (combine_three_valu_op(ctx, instr, aco_opcode::v_or_b32, aco_opcode::v_or3_b32, "012", 1 | 2)) ;
      else if (combine_three_valu_op(ctx, instr, aco_opcode::v_and_b32, aco_opcode::v_and_or_b32, "120", 1 | 2)) ;
      else combine_three_valu_op(ctx, instr, aco_opcode::v_lshlrev_b32, aco_opcode::v_lshl_or_b32, "210", 1 | 2);
   } else if (instr->opcode == aco_opcode::v_add_u32 && ctx.program->chip_class >= GFX9) {
      if (combine_three_valu_op(ctx, instr, aco_opcode::v_xor_b32, aco_opcode::v_xad_u32, "120", 1 | 2)) ;
      else if (combine_three_valu_op(ctx, instr, aco_opcode::v_add_u32, aco_opcode::v_add3_u32, "012", 1 | 2)) ;
      else combine_three_valu_op(ctx, instr, aco_opcode::v_lshlrev_b32, aco_opcode::v_lshl_add_u32, "210", 1 | 2);
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
         if (combine_three_valu_op(ctx, instr, instr->opcode, instr->opcode == min ? min3 : max3, "012", 1 | 2));
         else combine_clamp(ctx, instr, min, max, med3);
      }
   }
}


void select_instruction(opt_ctx &ctx, aco_ptr<Instruction>& instr)
{
   const uint32_t threshold = 4;

   /* Dead Code Elimination:
    * We remove instructions if they define temporaries which all are unused */
   const bool is_used = instr->definitions.empty() ||
                        std::any_of(instr->definitions.begin(), instr->definitions.end(),
                                    [&ctx](const Definition& def) { return ctx.uses[def.tempId()]; });
   if (!is_used) {
      instr.reset();
      return;
   }

   /* convert split_vector into extract_vector if only one definition is ever used */
   if (instr->opcode == aco_opcode::p_split_vector) {
      unsigned num_used = 0;
      unsigned idx = 0;
      for (unsigned i = 0; i < instr->definitions.size(); i++) {
         if (ctx.uses[instr->definitions[i].tempId()]) {
            num_used++;
            idx = i;
         }
      }
      if (num_used == 1) {
         aco_ptr<Pseudo_instruction> extract{create_instruction<Pseudo_instruction>(aco_opcode::p_extract_vector, Format::PSEUDO, 2, 1)};
         extract->operands[0] = instr->operands[0];
         extract->operands[1] = Operand((uint32_t) idx);
         extract->definitions[0] = instr->definitions[idx];
         instr.reset(extract.release());
      }
   }

   /* re-check mad instructions */
   if (instr->opcode == aco_opcode::v_mad_f32 && ctx.info[instr->definitions[0].tempId()].is_mad()) {
      mad_info* info = &ctx.mad_infos[ctx.info[instr->definitions[0].tempId()].val];
      /* first, check profitability */
      if (ctx.uses[info->mul_temp_id]) {
         ctx.uses[info->mul_temp_id]++;
         instr.swap(info->add_instr);

      /* second, check possible literals */
      } else if (!info->needs_vop3) {
         uint32_t literal_idx = 0;
         uint32_t literal_uses = UINT32_MAX;
         for (unsigned i = 0; i < instr->operands.size(); i++)
         {
            if (!instr->operands[i].isTemp())
               continue;
            /* if one of the operands is sgpr, we cannot add a literal somewhere else */
            if (instr->operands[i].getTemp().type() == RegType::sgpr) {
               if (ctx.info[instr->operands[i].tempId()].is_literal()) {
                  literal_uses = ctx.uses[instr->operands[i].tempId()];
                  literal_idx = i;
               } else {
                  literal_uses = UINT32_MAX;
               }
               break;
            }
            else if (ctx.info[instr->operands[i].tempId()].is_literal() &&
                ctx.uses[instr->operands[i].tempId()] < literal_uses) {
               literal_uses = ctx.uses[instr->operands[i].tempId()];
               literal_idx = i;
            }
         }
         if (literal_uses < threshold) {
            ctx.uses[instr->operands[literal_idx].tempId()]--;
            info->check_literal = true;
            info->literal_idx = literal_idx;
         }
      }
      return;
   }

   /* check for literals */
   /* we do not apply the literals yet as we don't know if it is profitable */
   if (instr->isSALU()) {
      uint32_t literal_idx = 0;
      uint32_t literal_uses = UINT32_MAX;
      bool has_literal = false;
      for (unsigned i = 0; i < instr->operands.size(); i++)
      {
         if (instr->operands[i].isLiteral()) {
            has_literal = true;
            break;
         }
         if (!instr->operands[i].isTemp())
            continue;
         if (ctx.info[instr->operands[i].tempId()].is_literal() &&
             ctx.uses[instr->operands[i].tempId()] < literal_uses) {
            literal_uses = ctx.uses[instr->operands[i].tempId()];
            literal_idx = i;
         }
      }
      if (!has_literal && literal_uses < threshold) {
         ctx.uses[instr->operands[literal_idx].tempId()]--;
         if (ctx.uses[instr->operands[literal_idx].tempId()] == 0)
            instr->operands[literal_idx] = Operand(ctx.info[instr->operands[literal_idx].tempId()].val);
      }
   } else if (instr->isVALU() && valu_can_accept_literal(ctx, instr, 0) &&
       instr->operands[0].isTemp() &&
       ctx.info[instr->operands[0].tempId()].is_literal() &&
       ctx.uses[instr->operands[0].tempId()] < threshold) {
      ctx.uses[instr->operands[0].tempId()]--;
      if (ctx.uses[instr->operands[0].tempId()] == 0)
         instr->operands[0] = Operand(ctx.info[instr->operands[0].tempId()].val);
   }

}


void apply_literals(opt_ctx &ctx, aco_ptr<Instruction>& instr)
{
   /* Cleanup Dead Instructions */
   if (!instr)
      return;

   /* apply literals on SALU */
   if (instr->isSALU()) {
      for (Operand& op : instr->operands) {
         if (!op.isTemp())
            continue;
         if (op.isLiteral())
            break;
         if (ctx.info[op.tempId()].is_literal() &&
             ctx.uses[op.tempId()] == 0)
            op = Operand(ctx.info[op.tempId()].val);
      }
   }

   /* apply literals on VALU */
   else if (instr->isVALU() && !instr->isVOP3() &&
       instr->operands[0].isTemp() &&
       ctx.info[instr->operands[0].tempId()].is_literal() &&
       ctx.uses[instr->operands[0].tempId()] == 0) {
      instr->operands[0] = Operand(ctx.info[instr->operands[0].tempId()].val);
   }

   /* apply literals on MAD */
   else if (instr->opcode == aco_opcode::v_mad_f32 && ctx.info[instr->definitions[0].tempId()].is_mad()) {
      mad_info* info = &ctx.mad_infos[ctx.info[instr->definitions[0].tempId()].val];
      aco_ptr<Instruction> new_mad;
      if (info->check_literal && ctx.uses[instr->operands[info->literal_idx].tempId()] == 0) {
         if (info->literal_idx == 2) { /* add literal -> madak */
            new_mad.reset(create_instruction<VOP2_instruction>(aco_opcode::v_madak_f32, Format::VOP2, 3, 1));
            new_mad->operands[0] = instr->operands[0];
            new_mad->operands[1] = instr->operands[1];
         } else { /* mul literal -> madmk */
            new_mad.reset(create_instruction<VOP2_instruction>(aco_opcode::v_madmk_f32, Format::VOP2, 3, 1));
            new_mad->operands[0] = instr->operands[1 - info->literal_idx];
            new_mad->operands[1] = instr->operands[2];
         }
         new_mad->operands[2] = Operand(ctx.info[instr->operands[info->literal_idx].tempId()].val);
         new_mad->definitions[0] = instr->definitions[0];
         instr.swap(new_mad);
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

   ctx.uses = std::move(dead_code_analysis(program));

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
