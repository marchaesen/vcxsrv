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
 */

#include "aco_builder.h"
#include "aco_ir.h"

#include "util/half_float.h"
#include "util/memstream.h"

#include <algorithm>
#include <array>
#include <vector>

namespace aco {

#ifndef NDEBUG
void
perfwarn(Program* program, bool cond, const char* msg, Instruction* instr)
{
   if (cond) {
      char* out;
      size_t outsize;
      struct u_memstream mem;
      u_memstream_open(&mem, &out, &outsize);
      FILE* const memf = u_memstream_get(&mem);

      fprintf(memf, "%s: ", msg);
      aco_print_instr(instr, memf);
      u_memstream_close(&mem);

      aco_perfwarn(program, out);
      free(out);

      if (debug_flags & DEBUG_PERFWARN)
         exit(1);
   }
}
#endif

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
       : add_instr(std::move(instr)), mul_temp_id(id), literal_idx(0), check_literal(false)
   {}
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
   label_clamp = 1 << 12,
   label_undefined = 1 << 14,
   label_vcc = 1 << 15,
   label_b2f = 1 << 16,
   label_add_sub = 1 << 17,
   label_bitwise = 1 << 18,
   label_minmax = 1 << 19,
   label_vopc = 1 << 20,
   label_uniform_bool = 1 << 21,
   label_constant_64bit = 1 << 22,
   label_uniform_bitwise = 1 << 23,
   label_scc_invert = 1 << 24,
   label_vcc_hint = 1 << 25,
   label_scc_needed = 1 << 26,
   label_b2i = 1 << 27,
   label_fcanonicalize = 1 << 28,
   label_constant_16bit = 1 << 29,
   label_usedef = 1 << 30,   /* generic label */
   label_vop3p = 1ull << 31, /* 1ull to prevent sign extension */
   label_canonicalized = 1ull << 32,
   label_extract = 1ull << 33,
   label_insert = 1ull << 34,
   label_dpp = 1ull << 35,
};

static constexpr uint64_t instr_usedef_labels =
   label_vec | label_mul | label_mad | label_add_sub | label_vop3p | label_bitwise |
   label_uniform_bitwise | label_minmax | label_vopc | label_usedef | label_extract | label_dpp;
static constexpr uint64_t instr_mod_labels =
   label_omod2 | label_omod4 | label_omod5 | label_clamp | label_insert;

static constexpr uint64_t instr_labels = instr_usedef_labels | instr_mod_labels;
static constexpr uint64_t temp_labels = label_abs | label_neg | label_temp | label_vcc | label_b2f |
                                        label_uniform_bool | label_scc_invert | label_b2i |
                                        label_fcanonicalize;
static constexpr uint32_t val_labels =
   label_constant_32bit | label_constant_64bit | label_constant_16bit | label_literal;

static_assert((instr_labels & temp_labels) == 0, "labels cannot intersect");
static_assert((instr_labels & val_labels) == 0, "labels cannot intersect");
static_assert((temp_labels & val_labels) == 0, "labels cannot intersect");

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
      /* Since all the instr_usedef_labels use instr for the same thing
       * (indicating the defining instruction), there is usually no need to
       * clear any other instr labels. */
      if (new_label & instr_usedef_labels)
         label &= ~(instr_mod_labels | temp_labels | val_labels); /* instr, temp and val alias */

      if (new_label & instr_mod_labels) {
         label &= ~instr_labels;
         label &= ~(temp_labels | val_labels); /* instr, temp and val alias */
      }

      if (new_label & temp_labels) {
         label &= ~temp_labels;
         label &= ~(instr_labels | val_labels); /* instr, temp and val alias */
      }

      uint32_t const_labels =
         label_literal | label_constant_32bit | label_constant_64bit | label_constant_16bit;
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

   bool is_vec() { return label & label_vec; }

   void set_constant(chip_class chip, uint64_t constant)
   {
      Operand op16 = Operand::c16(constant);
      Operand op32 = Operand::get_const(chip, constant, 4);
      add_label(label_literal);
      val = constant;

      /* check that no upper bits are lost in case of packed 16bit constants */
      if (chip >= GFX8 && !op16.isLiteral() && op16.constantValue64() == constant)
         add_label(label_constant_16bit);

      if (!op32.isLiteral())
         add_label(label_constant_32bit);

      if (Operand::is_constant_representable(constant, 8))
         add_label(label_constant_64bit);

      if (label & label_constant_64bit) {
         val = Operand::c64(constant).constantValue();
         if (val != constant)
            label &= ~(label_literal | label_constant_16bit | label_constant_32bit);
      }
   }

   bool is_constant(unsigned bits)
   {
      switch (bits) {
      case 8: return label & label_literal;
      case 16: return label & label_constant_16bit;
      case 32: return label & label_constant_32bit;
      case 64: return label & label_constant_64bit;
      }
      return false;
   }

   bool is_literal(unsigned bits)
   {
      bool is_lit = label & label_literal;
      switch (bits) {
      case 8: return false;
      case 16: return is_lit && ~(label & label_constant_16bit);
      case 32: return is_lit && ~(label & label_constant_32bit);
      case 64: return false;
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

   bool is_abs() { return label & label_abs; }

   void set_neg(Temp neg_temp)
   {
      add_label(label_neg);
      temp = neg_temp;
   }

   bool is_neg() { return label & label_neg; }

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

   bool is_mul() { return label & label_mul; }

   void set_temp(Temp tmp)
   {
      add_label(label_temp);
      temp = tmp;
   }

   bool is_temp() { return label & label_temp; }

   void set_mad(Instruction* mad, uint32_t mad_info_idx)
   {
      add_label(label_mad);
      mad->pass_flags = mad_info_idx;
      instr = mad;
   }

   bool is_mad() { return label & label_mad; }

   void set_omod2(Instruction* mul)
   {
      add_label(label_omod2);
      instr = mul;
   }

   bool is_omod2() { return label & label_omod2; }

   void set_omod4(Instruction* mul)
   {
      add_label(label_omod4);
      instr = mul;
   }

   bool is_omod4() { return label & label_omod4; }

   void set_omod5(Instruction* mul)
   {
      add_label(label_omod5);
      instr = mul;
   }

   bool is_omod5() { return label & label_omod5; }

   void set_clamp(Instruction* med3)
   {
      add_label(label_clamp);
      instr = med3;
   }

   bool is_clamp() { return label & label_clamp; }

   void set_undefined() { add_label(label_undefined); }

   bool is_undefined() { return label & label_undefined; }

   void set_vcc(Temp vcc_val)
   {
      add_label(label_vcc);
      temp = vcc_val;
   }

   bool is_vcc() { return label & label_vcc; }

   void set_b2f(Temp b2f_val)
   {
      add_label(label_b2f);
      temp = b2f_val;
   }

   bool is_b2f() { return label & label_b2f; }

   void set_add_sub(Instruction* add_sub_instr)
   {
      add_label(label_add_sub);
      instr = add_sub_instr;
   }

   bool is_add_sub() { return label & label_add_sub; }

   void set_bitwise(Instruction* bitwise_instr)
   {
      add_label(label_bitwise);
      instr = bitwise_instr;
   }

   bool is_bitwise() { return label & label_bitwise; }

   void set_uniform_bitwise() { add_label(label_uniform_bitwise); }

   bool is_uniform_bitwise() { return label & label_uniform_bitwise; }

   void set_minmax(Instruction* minmax_instr)
   {
      add_label(label_minmax);
      instr = minmax_instr;
   }

   bool is_minmax() { return label & label_minmax; }

   void set_vopc(Instruction* vopc_instr)
   {
      add_label(label_vopc);
      instr = vopc_instr;
   }

   bool is_vopc() { return label & label_vopc; }

   void set_scc_needed() { add_label(label_scc_needed); }

   bool is_scc_needed() { return label & label_scc_needed; }

   void set_scc_invert(Temp scc_inv)
   {
      add_label(label_scc_invert);
      temp = scc_inv;
   }

   bool is_scc_invert() { return label & label_scc_invert; }

   void set_uniform_bool(Temp uniform_bool)
   {
      add_label(label_uniform_bool);
      temp = uniform_bool;
   }

   bool is_uniform_bool() { return label & label_uniform_bool; }

   void set_vcc_hint() { add_label(label_vcc_hint); }

   bool is_vcc_hint() { return label & label_vcc_hint; }

   void set_b2i(Temp b2i_val)
   {
      add_label(label_b2i);
      temp = b2i_val;
   }

   bool is_b2i() { return label & label_b2i; }

   void set_usedef(Instruction* label_instr)
   {
      add_label(label_usedef);
      instr = label_instr;
   }

   bool is_usedef() { return label & label_usedef; }

   void set_vop3p(Instruction* vop3p_instr)
   {
      add_label(label_vop3p);
      instr = vop3p_instr;
   }

   bool is_vop3p() { return label & label_vop3p; }

   void set_fcanonicalize(Temp tmp)
   {
      add_label(label_fcanonicalize);
      temp = tmp;
   }

   bool is_fcanonicalize() { return label & label_fcanonicalize; }

   void set_canonicalized() { add_label(label_canonicalized); }

   bool is_canonicalized() { return label & label_canonicalized; }

   void set_extract(Instruction* extract)
   {
      add_label(label_extract);
      instr = extract;
   }

   bool is_extract() { return label & label_extract; }

   void set_insert(Instruction* insert)
   {
      add_label(label_insert);
      instr = insert;
   }

   bool is_insert() { return label & label_insert; }

   void set_dpp(Instruction* mov)
   {
      add_label(label_dpp);
      instr = mov;
   }

   bool is_dpp() { return label & label_dpp; }
};

struct opt_ctx {
   Program* program;
   float_mode fp_mode;
   std::vector<aco_ptr<Instruction>> instructions;
   ssa_info* info;
   std::pair<uint32_t, Temp> last_literal;
   std::vector<mad_info> mad_infos;
   std::vector<uint16_t> uses;
};

bool
can_use_VOP3(opt_ctx& ctx, const aco_ptr<Instruction>& instr)
{
   if (instr->isVOP3())
      return true;

   if (instr->isVOP3P())
      return false;

   if (instr->operands.size() && instr->operands[0].isLiteral() && ctx.program->chip_class < GFX10)
      return false;

   if (instr->isDPP() || instr->isSDWA())
      return false;

   return instr->opcode != aco_opcode::v_madmk_f32 && instr->opcode != aco_opcode::v_madak_f32 &&
          instr->opcode != aco_opcode::v_madmk_f16 && instr->opcode != aco_opcode::v_madak_f16 &&
          instr->opcode != aco_opcode::v_fmamk_f32 && instr->opcode != aco_opcode::v_fmaak_f32 &&
          instr->opcode != aco_opcode::v_fmamk_f16 && instr->opcode != aco_opcode::v_fmaak_f16 &&
          instr->opcode != aco_opcode::v_readlane_b32 &&
          instr->opcode != aco_opcode::v_writelane_b32 &&
          instr->opcode != aco_opcode::v_readfirstlane_b32;
}

bool
pseudo_propagate_temp(opt_ctx& ctx, aco_ptr<Instruction>& instr, Temp temp, unsigned index)
{
   if (instr->definitions.empty())
      return false;

   const bool vgpr =
      instr->opcode == aco_opcode::p_as_uniform ||
      std::all_of(instr->definitions.begin(), instr->definitions.end(),
                  [](const Definition& def) { return def.regClass().type() == RegType::vgpr; });

   /* don't propagate VGPRs into SGPR instructions */
   if (temp.type() == RegType::vgpr && !vgpr)
      return false;

   bool can_accept_sgpr =
      ctx.program->chip_class >= GFX9 ||
      std::none_of(instr->definitions.begin(), instr->definitions.end(),
                   [](const Definition& def) { return def.regClass().is_subdword(); });

   switch (instr->opcode) {
   case aco_opcode::p_phi:
   case aco_opcode::p_linear_phi:
   case aco_opcode::p_parallelcopy:
   case aco_opcode::p_create_vector:
      if (temp.bytes() != instr->operands[index].bytes())
         return false;
      break;
   case aco_opcode::p_extract_vector:
      if (temp.type() == RegType::sgpr && !can_accept_sgpr)
         return false;
      break;
   case aco_opcode::p_split_vector: {
      if (temp.type() == RegType::sgpr && !can_accept_sgpr)
         return false;
      /* don't increase the vector size */
      if (temp.bytes() > instr->operands[index].bytes())
         return false;
      /* We can decrease the vector size as smaller temporaries are only
       * propagated by p_as_uniform instructions.
       * If this propagation leads to invalid IR or hits the assertion below,
       * it means that some undefined bytes within a dword are begin accessed
       * and a bug in instruction_selection is likely. */
      int decrease = instr->operands[index].bytes() - temp.bytes();
      while (decrease > 0) {
         decrease -= instr->definitions.back().bytes();
         instr->definitions.pop_back();
      }
      assert(decrease == 0);
      break;
   }
   case aco_opcode::p_as_uniform:
      if (temp.regClass() == instr->definitions[0].regClass())
         instr->opcode = aco_opcode::p_parallelcopy;
      break;
   default: return false;
   }

   instr->operands[index].setTemp(temp);
   return true;
}

/* This expects the DPP modifier to be removed. */
bool
can_apply_sgprs(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->isSDWA() && ctx.program->chip_class < GFX9)
      return false;
   return instr->opcode != aco_opcode::v_readfirstlane_b32 &&
          instr->opcode != aco_opcode::v_readlane_b32 &&
          instr->opcode != aco_opcode::v_readlane_b32_e64 &&
          instr->opcode != aco_opcode::v_writelane_b32 &&
          instr->opcode != aco_opcode::v_writelane_b32_e64 &&
          instr->opcode != aco_opcode::v_permlane16_b32 &&
          instr->opcode != aco_opcode::v_permlanex16_b32;
}

void
to_VOP3(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->isVOP3())
      return;

   aco_ptr<Instruction> tmp = std::move(instr);
   Format format = asVOP3(tmp->format);
   instr.reset(create_instruction<VOP3_instruction>(tmp->opcode, format, tmp->operands.size(),
                                                    tmp->definitions.size()));
   std::copy(tmp->operands.cbegin(), tmp->operands.cend(), instr->operands.begin());
   for (unsigned i = 0; i < instr->definitions.size(); i++) {
      instr->definitions[i] = tmp->definitions[i];
      if (instr->definitions[i].isTemp()) {
         ssa_info& info = ctx.info[instr->definitions[i].tempId()];
         if (info.label & instr_usedef_labels && info.instr == tmp.get())
            info.instr = instr.get();
      }
   }
   /* we don't need to update any instr_mod_labels because they either haven't
    * been applied yet or this instruction isn't dead and so they've been ignored */
}

bool
is_operand_vgpr(Operand op)
{
   return op.isTemp() && op.getTemp().type() == RegType::vgpr;
}

void
to_SDWA(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   aco_ptr<Instruction> tmp = convert_to_SDWA(ctx.program->chip_class, instr);
   if (!tmp)
      return;

   for (unsigned i = 0; i < instr->definitions.size(); i++) {
      ssa_info& info = ctx.info[instr->definitions[i].tempId()];
      if (info.label & instr_labels && info.instr == tmp.get())
         info.instr = instr.get();
   }
}

/* only covers special cases */
bool
alu_can_accept_constant(aco_opcode opcode, unsigned operand)
{
   switch (opcode) {
   case aco_opcode::v_interp_p2_f32:
   case aco_opcode::v_mac_f32:
   case aco_opcode::v_writelane_b32:
   case aco_opcode::v_writelane_b32_e64:
   case aco_opcode::v_cndmask_b32: return operand != 2;
   case aco_opcode::s_addk_i32:
   case aco_opcode::s_mulk_i32:
   case aco_opcode::p_wqm:
   case aco_opcode::p_extract_vector:
   case aco_opcode::p_split_vector:
   case aco_opcode::v_readlane_b32:
   case aco_opcode::v_readlane_b32_e64:
   case aco_opcode::v_readfirstlane_b32:
   case aco_opcode::p_extract:
   case aco_opcode::p_insert: return operand != 0;
   default: return true;
   }
}

bool
valu_can_accept_vgpr(aco_ptr<Instruction>& instr, unsigned operand)
{
   if (instr->opcode == aco_opcode::v_readlane_b32 ||
       instr->opcode == aco_opcode::v_readlane_b32_e64 ||
       instr->opcode == aco_opcode::v_writelane_b32 ||
       instr->opcode == aco_opcode::v_writelane_b32_e64)
      return operand != 1;
   if (instr->opcode == aco_opcode::v_permlane16_b32 ||
       instr->opcode == aco_opcode::v_permlanex16_b32)
      return operand == 0;
   return true;
}

/* check constant bus and literal limitations */
bool
check_vop3_operands(opt_ctx& ctx, unsigned num_operands, Operand* operands)
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

bool
parse_base_offset(opt_ctx& ctx, Instruction* instr, unsigned op_index, Temp* base, uint32_t* offset,
                  bool prevent_overflow)
{
   Operand op = instr->operands[op_index];

   if (!op.isTemp())
      return false;
   Temp tmp = op.getTemp();
   if (!ctx.info[tmp.id()].is_add_sub())
      return false;

   Instruction* add_instr = ctx.info[tmp.id()].instr;

   switch (add_instr->opcode) {
   case aco_opcode::v_add_u32:
   case aco_opcode::v_add_co_u32:
   case aco_opcode::v_add_co_u32_e64:
   case aco_opcode::s_add_i32:
   case aco_opcode::s_add_u32: break;
   default: return false;
   }
   if (prevent_overflow && !add_instr->definitions[0].isNUW())
      return false;

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
      if (parse_base_offset(ctx, add_instr, !i, base, &offset2, prevent_overflow)) {
         *offset += offset2;
      } else {
         *base = add_instr->operands[!i].getTemp();
      }
      return true;
   }

   return false;
}

unsigned
get_operand_size(aco_ptr<Instruction>& instr, unsigned index)
{
   if (instr->isPseudo())
      return instr->operands[index].bytes() * 8u;
   else if (instr->opcode == aco_opcode::v_mad_u64_u32 ||
            instr->opcode == aco_opcode::v_mad_i64_i32)
      return index == 2 ? 64 : 32;
   else if (instr->isVALU() || instr->isSALU())
      return instr_info.operand_size[(int)instr->opcode];
   else
      return 0;
}

Operand
get_constant_op(opt_ctx& ctx, ssa_info info, uint32_t bits)
{
   if (bits == 64)
      return Operand::c32_or_c64(info.val, true);
   return Operand::get_const(ctx.program->chip_class, info.val, bits / 8u);
}

bool
fixed_to_exec(Operand op)
{
   return op.isFixed() && op.physReg() == exec;
}

SubdwordSel
parse_extract(Instruction* instr)
{
   if (instr->opcode == aco_opcode::p_extract) {
      unsigned size = instr->operands[2].constantValue() / 8;
      unsigned offset = instr->operands[1].constantValue() * size;
      bool sext = instr->operands[3].constantEquals(1);
      return SubdwordSel(size, offset, sext);
   } else if (instr->opcode == aco_opcode::p_insert && instr->operands[1].constantEquals(0)) {
      return instr->operands[2].constantEquals(8) ? SubdwordSel::ubyte : SubdwordSel::uword;
   } else {
      return SubdwordSel();
   }
}

SubdwordSel
parse_insert(Instruction* instr)
{
   if (instr->opcode == aco_opcode::p_extract && instr->operands[3].constantEquals(0) &&
       instr->operands[1].constantEquals(0)) {
      return instr->operands[2].constantEquals(8) ? SubdwordSel::ubyte : SubdwordSel::uword;
   } else if (instr->opcode == aco_opcode::p_insert) {
      unsigned size = instr->operands[2].constantValue() / 8;
      unsigned offset = instr->operands[1].constantValue() * size;
      return SubdwordSel(size, offset, false);
   } else {
      return SubdwordSel();
   }
}

bool
can_apply_extract(opt_ctx& ctx, aco_ptr<Instruction>& instr, unsigned idx, ssa_info& info)
{
   if (idx >= 2)
      return false;

   Temp tmp = info.instr->operands[0].getTemp();
   SubdwordSel sel = parse_extract(info.instr);

   if (!sel) {
      return false;
   } else if (sel.size() == 4) {
      return true;
   } else if (instr->opcode == aco_opcode::v_cvt_f32_u32 && sel.size() == 1 && !sel.sign_extend()) {
      return true;
   } else if (can_use_SDWA(ctx.program->chip_class, instr, true) &&
              (tmp.type() == RegType::vgpr || ctx.program->chip_class >= GFX9)) {
      if (instr->isSDWA() && instr->sdwa().sel[idx] != SubdwordSel::dword)
         return false;
      return true;
   } else if (instr->isVOP3() && sel.size() == 2 &&
              can_use_opsel(ctx.program->chip_class, instr->opcode, idx, sel.offset()) &&
              !(instr->vop3().opsel & (1 << idx))) {
      return true;
   } else {
      return false;
   }
}

/* Combine an p_extract (or p_insert, in some cases) instruction with instr.
 * instr(p_extract(...)) -> instr()
 */
void
apply_extract(opt_ctx& ctx, aco_ptr<Instruction>& instr, unsigned idx, ssa_info& info)
{
   Temp tmp = info.instr->operands[0].getTemp();
   SubdwordSel sel = parse_extract(info.instr);
   assert(sel);

   instr->operands[idx].set16bit(false);
   instr->operands[idx].set24bit(false);

   ctx.info[tmp.id()].label &= ~label_insert;

   if (sel.size() == 4) {
      /* full dword selection */
   } else if (instr->opcode == aco_opcode::v_cvt_f32_u32 && sel.size() == 1 && !sel.sign_extend()) {
      switch (sel.offset()) {
      case 0: instr->opcode = aco_opcode::v_cvt_f32_ubyte0; break;
      case 1: instr->opcode = aco_opcode::v_cvt_f32_ubyte1; break;
      case 2: instr->opcode = aco_opcode::v_cvt_f32_ubyte2; break;
      case 3: instr->opcode = aco_opcode::v_cvt_f32_ubyte3; break;
      }
   } else if (instr->opcode == aco_opcode::v_lshlrev_b32 && instr->operands[0].isConstant() &&
              sel.offset() == 0 &&
              ((sel.size() == 2 && instr->operands[0].constantValue() >= 16u) ||
               (sel.size() == 1 && instr->operands[0].constantValue() >= 24u))) {
      /* The undesireable upper bits are already shifted out. */
      return;
   } else if (can_use_SDWA(ctx.program->chip_class, instr, true) &&
              (tmp.type() == RegType::vgpr || ctx.program->chip_class >= GFX9)) {
      to_SDWA(ctx, instr);
      static_cast<SDWA_instruction*>(instr.get())->sel[idx] = sel;
   } else if (instr->isVOP3()) {
      if (sel.offset())
         instr->vop3().opsel |= 1 << idx;
   }

   /* label_vopc seems to be the only one worth keeping at the moment */
   for (Definition& def : instr->definitions)
      ctx.info[def.tempId()].label &= label_vopc;
}

void
check_sdwa_extract(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   for (unsigned i = 0; i < instr->operands.size(); i++) {
      Operand op = instr->operands[i];
      if (!op.isTemp())
         continue;
      ssa_info& info = ctx.info[op.tempId()];
      if (info.is_extract() && (info.instr->operands[0].getTemp().type() == RegType::vgpr ||
                                op.getTemp().type() == RegType::sgpr)) {
         if (!can_apply_extract(ctx, instr, i, info))
            info.label &= ~label_extract;
      }
   }
}

bool
does_fp_op_flush_denorms(opt_ctx& ctx, aco_opcode op)
{
   if (ctx.program->chip_class <= GFX8) {
      switch (op) {
      case aco_opcode::v_min_f32:
      case aco_opcode::v_max_f32:
      case aco_opcode::v_med3_f32:
      case aco_opcode::v_min3_f32:
      case aco_opcode::v_max3_f32:
      case aco_opcode::v_min_f16:
      case aco_opcode::v_max_f16: return false;
      default: break;
      }
   }
   return op != aco_opcode::v_cndmask_b32;
}

bool
can_eliminate_fcanonicalize(opt_ctx& ctx, aco_ptr<Instruction>& instr, Temp tmp)
{
   float_mode* fp = &ctx.fp_mode;
   if (ctx.info[tmp.id()].is_canonicalized() ||
       (tmp.bytes() == 4 ? fp->denorm32 : fp->denorm16_64) == fp_denorm_keep)
      return true;

   aco_opcode op = instr->opcode;
   return instr_info.can_use_input_modifiers[(int)op] && does_fp_op_flush_denorms(ctx, op);
}

bool
is_copy_label(opt_ctx& ctx, aco_ptr<Instruction>& instr, ssa_info& info)
{
   return info.is_temp() ||
          (info.is_fcanonicalize() && can_eliminate_fcanonicalize(ctx, instr, info.temp));
}

bool
is_op_canonicalized(opt_ctx& ctx, Operand op)
{
   float_mode* fp = &ctx.fp_mode;
   if ((op.isTemp() && ctx.info[op.tempId()].is_canonicalized()) ||
       (op.bytes() == 4 ? fp->denorm32 : fp->denorm16_64) == fp_denorm_keep)
      return true;

   if (op.isConstant() || (op.isTemp() && ctx.info[op.tempId()].is_constant_or_literal(32))) {
      uint32_t val = op.isTemp() ? ctx.info[op.tempId()].val : op.constantValue();
      if (op.bytes() == 2)
         return (val & 0x7fff) == 0 || (val & 0x7fff) > 0x3ff;
      else if (op.bytes() == 4)
         return (val & 0x7fffffff) == 0 || (val & 0x7fffffff) > 0x7fffff;
   }
   return false;
}

void
label_instruction(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->isSALU() || instr->isVALU() || instr->isPseudo()) {
      ASSERTED bool all_const = false;
      for (Operand& op : instr->operands)
         all_const =
            all_const && (!op.isTemp() || ctx.info[op.tempId()].is_constant_or_literal(32));
      perfwarn(ctx.program, all_const, "All instruction operands are constant", instr.get());

      ASSERTED bool is_copy = instr->opcode == aco_opcode::s_mov_b32 ||
                              instr->opcode == aco_opcode::s_mov_b64 ||
                              instr->opcode == aco_opcode::v_mov_b32;
      perfwarn(ctx.program, is_copy && !instr->usesModifiers(), "Use p_parallelcopy instead",
               instr.get());
   }

   for (unsigned i = 0; i < instr->operands.size(); i++) {
      if (!instr->operands[i].isTemp())
         continue;

      ssa_info info = ctx.info[instr->operands[i].tempId()];
      /* propagate undef */
      if (info.is_undefined() && is_phi(instr))
         instr->operands[i] = Operand(instr->operands[i].regClass());
      /* propagate reg->reg of same type */
      while (info.is_temp() && info.temp.regClass() == instr->operands[i].getTemp().regClass()) {
         instr->operands[i].setTemp(ctx.info[instr->operands[i].tempId()].temp);
         info = ctx.info[info.temp.id()];
      }

      /* PSEUDO: propagate temporaries */
      if (instr->isPseudo()) {
         while (info.is_temp()) {
            pseudo_propagate_temp(ctx, instr, info.temp, i);
            info = ctx.info[info.temp.id()];
         }
      }

      /* SALU / PSEUDO: propagate inline constants */
      if (instr->isSALU() || instr->isPseudo()) {
         unsigned bits = get_operand_size(instr, i);
         if ((info.is_constant(bits) || (info.is_literal(bits) && instr->isPseudo())) &&
             !instr->operands[i].isFixed() && alu_can_accept_constant(instr->opcode, i)) {
            instr->operands[i] = get_constant_op(ctx, info, bits);
            continue;
         }
      }

      /* VALU: propagate neg, abs & inline constants */
      else if (instr->isVALU()) {
         if (is_copy_label(ctx, instr, info) && info.temp.type() == RegType::vgpr &&
             valu_can_accept_vgpr(instr, i)) {
            instr->operands[i].setTemp(info.temp);
            info = ctx.info[info.temp.id()];
         }
         /* applying SGPRs to VOP1 doesn't increase code size and DCE is helped by doing it earlier */
         if (info.is_temp() && info.temp.type() == RegType::sgpr && can_apply_sgprs(ctx, instr) &&
             instr->operands.size() == 1) {
            instr->format = withoutDPP(instr->format);
            instr->operands[i].setTemp(info.temp);
            info = ctx.info[info.temp.id()];
         }

         /* for instructions other than v_cndmask_b32, the size of the instruction should match the
          * operand size */
         unsigned can_use_mod =
            instr->opcode != aco_opcode::v_cndmask_b32 || instr->operands[i].getTemp().bytes() == 4;
         can_use_mod = can_use_mod && instr_info.can_use_input_modifiers[(int)instr->opcode];

         if (instr->isSDWA())
            can_use_mod = can_use_mod && instr->sdwa().sel[i].size() == 4;
         else
            can_use_mod = can_use_mod && (instr->isDPP() || can_use_VOP3(ctx, instr));

         if (info.is_neg() && instr->opcode == aco_opcode::v_add_f32) {
            instr->opcode = i ? aco_opcode::v_sub_f32 : aco_opcode::v_subrev_f32;
            instr->operands[i].setTemp(info.temp);
         } else if (info.is_neg() && instr->opcode == aco_opcode::v_add_f16) {
            instr->opcode = i ? aco_opcode::v_sub_f16 : aco_opcode::v_subrev_f16;
            instr->operands[i].setTemp(info.temp);
         } else if (info.is_neg() && can_use_mod &&
                    can_eliminate_fcanonicalize(ctx, instr, info.temp)) {
            if (!instr->isDPP() && !instr->isSDWA())
               to_VOP3(ctx, instr);
            instr->operands[i].setTemp(info.temp);
            if (instr->isDPP() && !instr->dpp().abs[i])
               instr->dpp().neg[i] = true;
            else if (instr->isSDWA() && !instr->sdwa().abs[i])
               instr->sdwa().neg[i] = true;
            else if (instr->isVOP3() && !instr->vop3().abs[i])
               instr->vop3().neg[i] = true;
         }
         if (info.is_abs() && can_use_mod && can_eliminate_fcanonicalize(ctx, instr, info.temp)) {
            if (!instr->isDPP() && !instr->isSDWA())
               to_VOP3(ctx, instr);
            instr->operands[i] = Operand(info.temp);
            if (instr->isDPP())
               instr->dpp().abs[i] = true;
            else if (instr->isSDWA())
               instr->sdwa().abs[i] = true;
            else
               instr->vop3().abs[i] = true;
            continue;
         }

         unsigned bits = get_operand_size(instr, i);
         if (info.is_constant(bits) && alu_can_accept_constant(instr->opcode, i) &&
             (!instr->isSDWA() || ctx.program->chip_class >= GFX9)) {
            Operand op = get_constant_op(ctx, info, bits);
            perfwarn(ctx.program, instr->opcode == aco_opcode::v_cndmask_b32 && i == 2,
                     "v_cndmask_b32 with a constant selector", instr.get());
            if (i == 0 || instr->isSDWA() || instr->isVOP3P() ||
                instr->opcode == aco_opcode::v_readlane_b32 ||
                instr->opcode == aco_opcode::v_writelane_b32) {
               instr->format = withoutDPP(instr->format);
               instr->operands[i] = op;
               continue;
            } else if (!instr->isVOP3() && can_swap_operands(instr, &instr->opcode)) {
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
      else if (instr->isMUBUF()) {
         MUBUF_instruction& mubuf = instr->mubuf();
         Temp base;
         uint32_t offset;
         while (info.is_temp())
            info = ctx.info[info.temp.id()];

         /* According to AMDGPUDAGToDAGISel::SelectMUBUFScratchOffen(), vaddr
          * overflow for scratch accesses works only on GFX9+ and saddr overflow
          * never works. Since swizzling is the only thing that separates
          * scratch accesses and other accesses and swizzling changing how
          * addressing works significantly, this probably applies to swizzled
          * MUBUF accesses. */
         bool vaddr_prevent_overflow = mubuf.swizzled && ctx.program->chip_class < GFX9;
         bool saddr_prevent_overflow = mubuf.swizzled;

         if (mubuf.offen && i == 1 && info.is_constant_or_literal(32) &&
             mubuf.offset + info.val < 4096) {
            assert(!mubuf.idxen);
            instr->operands[1] = Operand(v1);
            mubuf.offset += info.val;
            mubuf.offen = false;
            continue;
         } else if (i == 2 && info.is_constant_or_literal(32) && mubuf.offset + info.val < 4096) {
            instr->operands[2] = Operand::c32(0);
            mubuf.offset += info.val;
            continue;
         } else if (mubuf.offen && i == 1 &&
                    parse_base_offset(ctx, instr.get(), i, &base, &offset,
                                      vaddr_prevent_overflow) &&
                    base.regClass() == v1 && mubuf.offset + offset < 4096) {
            assert(!mubuf.idxen);
            instr->operands[1].setTemp(base);
            mubuf.offset += offset;
            continue;
         } else if (i == 2 &&
                    parse_base_offset(ctx, instr.get(), i, &base, &offset,
                                      saddr_prevent_overflow) &&
                    base.regClass() == s1 && mubuf.offset + offset < 4096) {
            instr->operands[i].setTemp(base);
            mubuf.offset += offset;
            continue;
         }
      }

      /* DS: combine additions */
      else if (instr->isDS()) {

         DS_instruction& ds = instr->ds();
         Temp base;
         uint32_t offset;
         bool has_usable_ds_offset = ctx.program->chip_class >= GFX7;
         if (has_usable_ds_offset && i == 0 &&
             parse_base_offset(ctx, instr.get(), i, &base, &offset, false) &&
             base.regClass() == instr->operands[i].regClass() &&
             instr->opcode != aco_opcode::ds_swizzle_b32) {
            if (instr->opcode == aco_opcode::ds_write2_b32 ||
                instr->opcode == aco_opcode::ds_read2_b32 ||
                instr->opcode == aco_opcode::ds_write2_b64 ||
                instr->opcode == aco_opcode::ds_read2_b64) {
               unsigned mask = (instr->opcode == aco_opcode::ds_write2_b64 ||
                                instr->opcode == aco_opcode::ds_read2_b64)
                                  ? 0x7
                                  : 0x3;
               unsigned shifts = (instr->opcode == aco_opcode::ds_write2_b64 ||
                                  instr->opcode == aco_opcode::ds_read2_b64)
                                    ? 3
                                    : 2;

               if ((offset & mask) == 0 && ds.offset0 + (offset >> shifts) <= 255 &&
                   ds.offset1 + (offset >> shifts) <= 255) {
                  instr->operands[i].setTemp(base);
                  ds.offset0 += offset >> shifts;
                  ds.offset1 += offset >> shifts;
               }
            } else {
               if (ds.offset0 + offset <= 65535) {
                  instr->operands[i].setTemp(base);
                  ds.offset0 += offset;
               }
            }
         }
      }

      /* SMEM: propagate constants and combine additions */
      else if (instr->isSMEM()) {

         SMEM_instruction& smem = instr->smem();
         Temp base;
         uint32_t offset;
         bool prevent_overflow = smem.operands[0].size() > 2 || smem.prevent_overflow;
         if (i == 1 && info.is_constant_or_literal(32) &&
             ((ctx.program->chip_class == GFX6 && info.val <= 0x3FF) ||
              (ctx.program->chip_class == GFX7 && info.val <= 0xFFFFFFFF) ||
              (ctx.program->chip_class >= GFX8 && info.val <= 0xFFFFF))) {
            instr->operands[i] = Operand::c32(info.val);
            continue;
         } else if (i == 1 &&
                    parse_base_offset(ctx, instr.get(), i, &base, &offset, prevent_overflow) &&
                    base.regClass() == s1 && offset <= 0xFFFFF && ctx.program->chip_class >= GFX9) {
            bool soe = smem.operands.size() >= (!smem.definitions.empty() ? 3 : 4);
            if (soe && (!ctx.info[smem.operands.back().tempId()].is_constant_or_literal(32) ||
                        ctx.info[smem.operands.back().tempId()].val != 0)) {
               continue;
            }
            if (soe) {
               smem.operands[1] = Operand::c32(offset);
               smem.operands.back() = Operand(base);
            } else {
               SMEM_instruction* new_instr = create_instruction<SMEM_instruction>(
                  smem.opcode, Format::SMEM, smem.operands.size() + 1, smem.definitions.size());
               new_instr->operands[0] = smem.operands[0];
               new_instr->operands[1] = Operand::c32(offset);
               if (smem.definitions.empty())
                  new_instr->operands[2] = smem.operands[2];
               new_instr->operands.back() = Operand(base);
               if (!smem.definitions.empty())
                  new_instr->definitions[0] = smem.definitions[0];
               new_instr->sync = smem.sync;
               new_instr->glc = smem.glc;
               new_instr->dlc = smem.dlc;
               new_instr->nv = smem.nv;
               new_instr->disable_wqm = smem.disable_wqm;
               instr.reset(new_instr);
            }
            continue;
         }
      }

      else if (instr->isBranch()) {
         if (ctx.info[instr->operands[0].tempId()].is_scc_invert()) {
            /* Flip the branch instruction to get rid of the scc_invert instruction */
            instr->opcode = instr->opcode == aco_opcode::p_cbranch_z ? aco_opcode::p_cbranch_nz
                                                                     : aco_opcode::p_cbranch_z;
            instr->operands[0].setTemp(ctx.info[instr->operands[0].tempId()].temp);
         }
      }
   }

   /* if this instruction doesn't define anything, return */
   if (instr->definitions.empty()) {
      check_sdwa_extract(ctx, instr);
      return;
   }

   if (instr->isVALU() || instr->isVINTRP()) {
      if (instr_info.can_use_output_modifiers[(int)instr->opcode] || instr->isVINTRP() ||
          instr->opcode == aco_opcode::v_cndmask_b32) {
         bool canonicalized = true;
         if (!does_fp_op_flush_denorms(ctx, instr->opcode)) {
            unsigned ops = instr->opcode == aco_opcode::v_cndmask_b32 ? 2 : instr->operands.size();
            for (unsigned i = 0; canonicalized && (i < ops); i++)
               canonicalized = is_op_canonicalized(ctx, instr->operands[i]);
         }
         if (canonicalized)
            ctx.info[instr->definitions[0].tempId()].set_canonicalized();
      }

      if (instr->isVOPC()) {
         ctx.info[instr->definitions[0].tempId()].set_vopc(instr.get());
         check_sdwa_extract(ctx, instr);
         return;
      }
      if (instr->isVOP3P()) {
         ctx.info[instr->definitions[0].tempId()].set_vop3p(instr.get());
         return;
      }
   }

   switch (instr->opcode) {
   case aco_opcode::p_create_vector: {
      bool copy_prop = instr->operands.size() == 1 && instr->operands[0].isTemp() &&
                       instr->operands[0].regClass() == instr->definitions[0].regClass();
      if (copy_prop) {
         ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
         break;
      }

      /* expand vector operands */
      std::vector<Operand> ops;
      unsigned offset = 0;
      for (const Operand& op : instr->operands) {
         /* ensure that any expanded operands are properly aligned */
         bool aligned = offset % 4 == 0 || op.bytes() < 4;
         offset += op.bytes();
         if (aligned && op.isTemp() && ctx.info[op.tempId()].is_vec()) {
            Instruction* vec = ctx.info[op.tempId()].instr;
            for (const Operand& vec_op : vec->operands)
               ops.emplace_back(vec_op);
         } else {
            ops.emplace_back(op);
         }
      }

      /* combine expanded operands to new vector */
      if (ops.size() != instr->operands.size()) {
         assert(ops.size() > instr->operands.size());
         Definition def = instr->definitions[0];
         instr.reset(create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector,
                                                            Format::PSEUDO, ops.size(), 1));
         for (unsigned i = 0; i < ops.size(); i++) {
            if (ops[i].isTemp() && ctx.info[ops[i].tempId()].is_temp() &&
                ops[i].regClass() == ctx.info[ops[i].tempId()].temp.regClass())
               ops[i].setTemp(ctx.info[ops[i].tempId()].temp);
            instr->operands[i] = ops[i];
         }
         instr->definitions[0] = def;
      } else {
         for (unsigned i = 0; i < ops.size(); i++) {
            assert(instr->operands[i] == ops[i]);
         }
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
      for (unsigned i = 0; i < instr->definitions.size();
           split_offset += instr->definitions[i++].bytes()) {
         while (vec_offset < split_offset && vec_index < vec->operands.size())
            vec_offset += vec->operands[vec_index++].bytes();

         if (vec_offset != split_offset ||
             vec->operands[vec_index].bytes() != instr->definitions[i].bytes())
            continue;

         Operand vec_op = vec->operands[vec_index];
         if (vec_op.isConstant()) {
            ctx.info[instr->definitions[i].tempId()].set_constant(ctx.program->chip_class,
                                                                  vec_op.constantValue64());
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

      if (info.is_vec()) {
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
            instr->operands[0] = op;
            break;
         }
      } else if (info.is_constant_or_literal(32)) {
         /* propagate constants */
         uint32_t mask = u_bit_consecutive(0, instr->definitions[0].bytes() * 8u);
         uint32_t val = (info.val >> (dst_offset * 8u)) & mask;
         instr->operands[0] =
            Operand::get_const(ctx.program->chip_class, val, instr->definitions[0].bytes());
         ;
      } else if (index == 0 && instr->operands[0].size() == instr->definitions[0].size()) {
         ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
      }

      if (instr->operands[0].bytes() != instr->definitions[0].bytes())
         break;

      /* convert this extract into a copy instruction */
      instr->opcode = aco_opcode::p_parallelcopy;
      instr->operands.pop_back();
      FALLTHROUGH;
   }
   case aco_opcode::p_parallelcopy: /* propagate */
      if (instr->operands[0].isTemp() && ctx.info[instr->operands[0].tempId()].is_vec() &&
          instr->operands[0].regClass() != instr->definitions[0].regClass()) {
         /* We might not be able to copy-propagate if it's a SGPR->VGPR copy, so
          * duplicate the vector instead.
          */
         Instruction* vec = ctx.info[instr->operands[0].tempId()].instr;
         aco_ptr<Instruction> old_copy = std::move(instr);

         instr.reset(create_instruction<Pseudo_instruction>(
            aco_opcode::p_create_vector, Format::PSEUDO, vec->operands.size(), 1));
         instr->definitions[0] = old_copy->definitions[0];
         std::copy(vec->operands.begin(), vec->operands.end(), instr->operands.begin());
         for (unsigned i = 0; i < vec->operands.size(); i++) {
            Operand& op = instr->operands[i];
            if (op.isTemp() && ctx.info[op.tempId()].is_temp() &&
                ctx.info[op.tempId()].temp.type() == instr->definitions[0].regClass().type())
               op.setTemp(ctx.info[op.tempId()].temp);
         }
         ctx.info[instr->definitions[0].tempId()].set_vec(instr.get());
         break;
      }
      FALLTHROUGH;
   case aco_opcode::p_as_uniform:
      if (instr->definitions[0].isFixed()) {
         /* don't copy-propagate copies into fixed registers */
      } else if (instr->usesModifiers()) {
         // TODO
      } else if (instr->operands[0].isConstant()) {
         ctx.info[instr->definitions[0].tempId()].set_constant(
            ctx.program->chip_class, instr->operands[0].constantValue64());
      } else if (instr->operands[0].isTemp()) {
         ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
         if (ctx.info[instr->operands[0].tempId()].is_canonicalized())
            ctx.info[instr->definitions[0].tempId()].set_canonicalized();
      } else {
         assert(instr->operands[0].isFixed());
      }
      break;
   case aco_opcode::v_mov_b32:
      if (instr->isDPP()) {
         /* anything else doesn't make sense in SSA */
         assert(instr->dpp().row_mask == 0xf && instr->dpp().bank_mask == 0xf);
         ctx.info[instr->definitions[0].tempId()].set_dpp(instr.get());
      }
      break;
   case aco_opcode::p_is_helper:
      if (!ctx.program->needs_wqm)
         ctx.info[instr->definitions[0].tempId()].set_constant(ctx.program->chip_class, 0u);
      break;
   case aco_opcode::v_mul_f64: ctx.info[instr->definitions[0].tempId()].set_mul(instr.get()); break;
   case aco_opcode::v_mul_f16:
   case aco_opcode::v_mul_f32: { /* omod */
      ctx.info[instr->definitions[0].tempId()].set_mul(instr.get());

      /* TODO: try to move the negate/abs modifier to the consumer instead */
      bool uses_mods = instr->usesModifiers();
      bool fp16 = instr->opcode == aco_opcode::v_mul_f16;

      for (unsigned i = 0; i < 2; i++) {
         if (instr->operands[!i].isConstant() && instr->operands[i].isTemp()) {
            if (!instr->isDPP() && !instr->isSDWA() &&
                (instr->operands[!i].constantEquals(fp16 ? 0x3c00 : 0x3f800000) ||   /* 1.0 */
                 instr->operands[!i].constantEquals(fp16 ? 0xbc00 : 0xbf800000u))) { /* -1.0 */
               bool neg1 = instr->operands[!i].constantEquals(fp16 ? 0xbc00 : 0xbf800000u);

               VOP3_instruction* vop3 = instr->isVOP3() ? &instr->vop3() : NULL;
               if (vop3 && (vop3->abs[!i] || vop3->neg[!i] || vop3->clamp || vop3->omod))
                  continue;

               bool abs = vop3 && vop3->abs[i];
               bool neg = neg1 ^ (vop3 && vop3->neg[i]);

               Temp other = instr->operands[i].getTemp();
               if (abs && neg && other.type() == RegType::vgpr)
                  ctx.info[instr->definitions[0].tempId()].set_neg_abs(other);
               else if (abs && !neg && other.type() == RegType::vgpr)
                  ctx.info[instr->definitions[0].tempId()].set_abs(other);
               else if (!abs && neg && other.type() == RegType::vgpr)
                  ctx.info[instr->definitions[0].tempId()].set_neg(other);
               else if (!abs && !neg)
                  ctx.info[instr->definitions[0].tempId()].set_fcanonicalize(other);
            } else if (uses_mods) {
               continue;
            } else if (instr->operands[!i].constantValue() ==
                       (fp16 ? 0x4000 : 0x40000000)) { /* 2.0 */
               ctx.info[instr->operands[i].tempId()].set_omod2(instr.get());
            } else if (instr->operands[!i].constantValue() ==
                       (fp16 ? 0x4400 : 0x40800000)) { /* 4.0 */
               ctx.info[instr->operands[i].tempId()].set_omod4(instr.get());
            } else if (instr->operands[!i].constantValue() ==
                       (fp16 ? 0x3800 : 0x3f000000)) { /* 0.5 */
               ctx.info[instr->operands[i].tempId()].set_omod5(instr.get());
            } else if (instr->operands[!i].constantValue() == 0u &&
                       !(fp16 ? ctx.fp_mode.preserve_signed_zero_inf_nan16_64
                              : ctx.fp_mode.preserve_signed_zero_inf_nan32)) { /* 0.0 */
               ctx.info[instr->definitions[0].tempId()].set_constant(ctx.program->chip_class, 0u);
            } else {
               continue;
            }
            break;
         }
      }
      break;
   }
   case aco_opcode::v_mul_lo_u16:
   case aco_opcode::v_mul_lo_u16_e64:
   case aco_opcode::v_mul_u32_u24:
      ctx.info[instr->definitions[0].tempId()].set_usedef(instr.get());
      break;
   case aco_opcode::v_med3_f16:
   case aco_opcode::v_med3_f32: { /* clamp */
      VOP3_instruction& vop3 = instr->vop3();
      if (vop3.abs[0] || vop3.abs[1] || vop3.abs[2] || vop3.neg[0] || vop3.neg[1] || vop3.neg[2] ||
          vop3.omod != 0 || vop3.opsel != 0)
         break;

      unsigned idx = 0;
      bool found_zero = false, found_one = false;
      bool is_fp16 = instr->opcode == aco_opcode::v_med3_f16;
      for (unsigned i = 0; i < 3; i++) {
         if (instr->operands[i].constantEquals(0))
            found_zero = true;
         else if (instr->operands[i].constantEquals(is_fp16 ? 0x3c00 : 0x3f800000)) /* 1.0 */
            found_one = true;
         else
            idx = i;
      }
      if (found_zero && found_one && instr->operands[idx].isTemp())
         ctx.info[instr->operands[idx].tempId()].set_clamp(instr.get());
      break;
   }
   case aco_opcode::v_cndmask_b32:
      if (instr->operands[0].constantEquals(0) && instr->operands[1].constantEquals(0xFFFFFFFF))
         ctx.info[instr->definitions[0].tempId()].set_vcc(instr->operands[2].getTemp());
      else if (instr->operands[0].constantEquals(0) &&
               instr->operands[1].constantEquals(0x3f800000u))
         ctx.info[instr->definitions[0].tempId()].set_b2f(instr->operands[2].getTemp());
      else if (instr->operands[0].constantEquals(0) && instr->operands[1].constantEquals(1))
         ctx.info[instr->definitions[0].tempId()].set_b2i(instr->operands[2].getTemp());

      ctx.info[instr->operands[2].tempId()].set_vcc_hint();
      break;
   case aco_opcode::v_cmp_lg_u32:
      if (instr->format == Format::VOPC && /* don't optimize VOP3 / SDWA / DPP */
          instr->operands[0].constantEquals(0) && instr->operands[1].isTemp() &&
          ctx.info[instr->operands[1].tempId()].is_vcc())
         ctx.info[instr->definitions[0].tempId()].set_temp(
            ctx.info[instr->operands[1].tempId()].temp);
      break;
   case aco_opcode::p_linear_phi: {
      /* lower_bool_phis() can create phis like this */
      bool all_same_temp = instr->operands[0].isTemp();
      /* this check is needed when moving uniform loop counters out of a divergent loop */
      if (all_same_temp)
         all_same_temp = instr->definitions[0].regClass() == instr->operands[0].regClass();
      for (unsigned i = 1; all_same_temp && (i < instr->operands.size()); i++) {
         if (!instr->operands[i].isTemp() ||
             instr->operands[i].tempId() != instr->operands[0].tempId())
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
   case aco_opcode::v_subbrev_co_u32:
      ctx.info[instr->definitions[0].tempId()].set_add_sub(instr.get());
      break;
   case aco_opcode::s_not_b32:
   case aco_opcode::s_not_b64:
      if (ctx.info[instr->operands[0].tempId()].is_uniform_bool()) {
         ctx.info[instr->definitions[0].tempId()].set_uniform_bitwise();
         ctx.info[instr->definitions[1].tempId()].set_scc_invert(
            ctx.info[instr->operands[0].tempId()].temp);
      } else if (ctx.info[instr->operands[0].tempId()].is_uniform_bitwise()) {
         ctx.info[instr->definitions[0].tempId()].set_uniform_bitwise();
         ctx.info[instr->definitions[1].tempId()].set_scc_invert(
            ctx.info[instr->operands[0].tempId()].instr->definitions[1].getTemp());
      }
      ctx.info[instr->definitions[0].tempId()].set_bitwise(instr.get());
      break;
   case aco_opcode::s_and_b32:
   case aco_opcode::s_and_b64:
      if (fixed_to_exec(instr->operands[1]) && instr->operands[0].isTemp()) {
         if (ctx.info[instr->operands[0].tempId()].is_uniform_bool()) {
            /* Try to get rid of the superfluous s_cselect + s_and_b64 that comes from turning a
             * uniform bool into divergent */
            ctx.info[instr->definitions[1].tempId()].set_temp(
               ctx.info[instr->operands[0].tempId()].temp);
            ctx.info[instr->definitions[0].tempId()].set_uniform_bool(
               ctx.info[instr->operands[0].tempId()].temp);
            break;
         } else if (ctx.info[instr->operands[0].tempId()].is_uniform_bitwise()) {
            /* Try to get rid of the superfluous s_and_b64, since the uniform bitwise instruction
             * already produces the same SCC */
            ctx.info[instr->definitions[1].tempId()].set_temp(
               ctx.info[instr->operands[0].tempId()].instr->definitions[1].getTemp());
            ctx.info[instr->definitions[0].tempId()].set_uniform_bool(
               ctx.info[instr->operands[0].tempId()].instr->definitions[1].getTemp());
            break;
         } else if ((ctx.program->stage.num_sw_stages() > 1 ||
                     ctx.program->stage.hw == HWStage::NGG) &&
                    instr->pass_flags == 1) {
            /* In case of merged shaders, pass_flags=1 means that all lanes are active (exec=-1), so
             * s_and is unnecessary. */
            ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
            break;
         } else if (ctx.info[instr->operands[0].tempId()].is_vopc()) {
            Instruction* vopc_instr = ctx.info[instr->operands[0].tempId()].instr;
            /* Remove superfluous s_and when the VOPC instruction uses the same exec and thus
             * already produces the same result */
            if (vopc_instr->pass_flags == instr->pass_flags) {
               assert(instr->pass_flags > 0);
               ctx.info[instr->definitions[0].tempId()].set_temp(
                  vopc_instr->definitions[0].getTemp());
               break;
            }
         }
      }
      FALLTHROUGH;
   case aco_opcode::s_or_b32:
   case aco_opcode::s_or_b64:
   case aco_opcode::s_xor_b32:
   case aco_opcode::s_xor_b64:
      if (std::all_of(instr->operands.begin(), instr->operands.end(),
                      [&ctx](const Operand& op)
                      {
                         return op.isTemp() && (ctx.info[op.tempId()].is_uniform_bool() ||
                                                ctx.info[op.tempId()].is_uniform_bitwise());
                      })) {
         ctx.info[instr->definitions[0].tempId()].set_uniform_bitwise();
      }
      FALLTHROUGH;
   case aco_opcode::s_lshl_b32:
   case aco_opcode::v_or_b32:
   case aco_opcode::v_lshlrev_b32:
   case aco_opcode::v_bcnt_u32_b32:
   case aco_opcode::v_and_b32:
   case aco_opcode::v_xor_b32:
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
   case aco_opcode::s_cselect_b64:
   case aco_opcode::s_cselect_b32:
      if (instr->operands[0].constantEquals((unsigned)-1) && instr->operands[1].constantEquals(0)) {
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
      if (instr->operands[0].isTemp() && ctx.info[instr->operands[0].tempId()].is_scc_invert()) {
         ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
      }
      break;
   case aco_opcode::s_mul_i32:
      /* Testing every uint32_t shows that 0x3f800000*n is never a denormal.
       * This pattern is created from a uniform nir_op_b2f. */
      if (instr->operands[0].constantEquals(0x3f800000u))
         ctx.info[instr->definitions[0].tempId()].set_canonicalized();
      break;
   case aco_opcode::p_extract: {
      if (instr->definitions[0].bytes() == 4) {
         ctx.info[instr->definitions[0].tempId()].set_extract(instr.get());
         if (instr->operands[0].regClass() == v1 && parse_insert(instr.get()))
            ctx.info[instr->operands[0].tempId()].set_insert(instr.get());
      }
      break;
   }
   case aco_opcode::p_insert: {
      if (instr->operands[0].bytes() == 4) {
         if (instr->operands[0].regClass() == v1)
            ctx.info[instr->operands[0].tempId()].set_insert(instr.get());
         if (parse_extract(instr.get()))
            ctx.info[instr->definitions[0].tempId()].set_extract(instr.get());
         ctx.info[instr->definitions[0].tempId()].set_bitwise(instr.get());
      }
      break;
   }
   case aco_opcode::ds_read_u8:
   case aco_opcode::ds_read_u8_d16:
   case aco_opcode::ds_read_u16:
   case aco_opcode::ds_read_u16_d16: {
      ctx.info[instr->definitions[0].tempId()].set_usedef(instr.get());
      break;
   }
   default: break;
   }

   /* Don't remove label_extract if we can't apply the extract to
    * neg/abs instructions because we'll likely combine it into another valu. */
   if (!(ctx.info[instr->definitions[0].tempId()].label & (label_neg | label_abs)))
      check_sdwa_extract(ctx, instr);
}

unsigned
original_temp_id(opt_ctx& ctx, Temp tmp)
{
   if (ctx.info[tmp.id()].is_temp())
      return ctx.info[tmp.id()].temp.id();
   else
      return tmp.id();
}

void
decrease_uses(opt_ctx& ctx, Instruction* instr)
{
   if (!--ctx.uses[instr->definitions[0].tempId()]) {
      for (const Operand& op : instr->operands) {
         if (op.isTemp())
            ctx.uses[op.tempId()]--;
      }
   }
}

Instruction*
follow_operand(opt_ctx& ctx, Operand op, bool ignore_uses = false)
{
   if (!op.isTemp() || !(ctx.info[op.tempId()].label & instr_usedef_labels))
      return nullptr;
   if (!ignore_uses && ctx.uses[op.tempId()] > 1)
      return nullptr;

   Instruction* instr = ctx.info[op.tempId()].instr;

   if (instr->definitions.size() == 2) {
      assert(instr->definitions[0].isTemp() && instr->definitions[0].tempId() == op.tempId());
      if (instr->definitions[1].isTemp() && ctx.uses[instr->definitions[1].tempId()])
         return nullptr;
   }

   return instr;
}

/* s_or_b64(neq(a, a), neq(b, b)) -> v_cmp_u_f32(a, b)
 * s_and_b64(eq(a, a), eq(b, b)) -> v_cmp_o_f32(a, b) */
bool
combine_ordering_test(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->definitions[0].regClass() != ctx.program->lane_mask)
      return false;
   if (instr->definitions[1].isTemp() && ctx.uses[instr->definitions[1].tempId()])
      return false;

   bool is_or = instr->opcode == aco_opcode::s_or_b64 || instr->opcode == aco_opcode::s_or_b32;

   bool neg[2] = {false, false};
   bool abs[2] = {false, false};
   uint8_t opsel = 0;
   Instruction* op_instr[2];
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
         VOP3_instruction& vop3 = op_instr[i]->vop3();
         if (vop3.neg[0] != vop3.neg[1] || vop3.abs[0] != vop3.abs[1] || vop3.opsel == 1 ||
             vop3.opsel == 2)
            return false;
         neg[i] = vop3.neg[0];
         abs[i] = vop3.abs[0];
         opsel |= (vop3.opsel & 1) << i;
      } else if (op_instr[i]->isSDWA()) {
         return false;
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
   case 16: new_op = is_or ? aco_opcode::v_cmp_u_f16 : aco_opcode::v_cmp_o_f16; break;
   case 32: new_op = is_or ? aco_opcode::v_cmp_u_f32 : aco_opcode::v_cmp_o_f32; break;
   case 64: new_op = is_or ? aco_opcode::v_cmp_u_f64 : aco_opcode::v_cmp_o_f64; break;
   }
   Instruction* new_instr;
   if (neg[0] || neg[1] || abs[0] || abs[1] || opsel || num_sgprs > 1) {
      VOP3_instruction* vop3 =
         create_instruction<VOP3_instruction>(new_op, asVOP3(Format::VOPC), 2, 1);
      for (unsigned i = 0; i < 2; i++) {
         vop3->neg[i] = neg[i];
         vop3->abs[i] = abs[i];
      }
      vop3->opsel = opsel;
      new_instr = static_cast<Instruction*>(vop3);
   } else {
      new_instr = create_instruction<VOPC_instruction>(new_op, Format::VOPC, 2, 1);
      instr->definitions[0].setHint(vcc);
   }
   new_instr->operands[0] = Operand(op[0]);
   new_instr->operands[1] = Operand(op[1]);
   new_instr->definitions[0] = instr->definitions[0];

   ctx.info[instr->definitions[0].tempId()].label = 0;
   ctx.info[instr->definitions[0].tempId()].set_vopc(new_instr);

   instr.reset(new_instr);

   return true;
}

/* s_or_b64(v_cmp_u_f32(a, b), cmp(a, b)) -> get_unordered(cmp)(a, b)
 * s_and_b64(v_cmp_o_f32(a, b), cmp(a, b)) -> get_ordered(cmp)(a, b) */
bool
combine_comparison_ordering(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->definitions[0].regClass() != ctx.program->lane_mask)
      return false;
   if (instr->definitions[1].isTemp() && ctx.uses[instr->definitions[1].tempId()])
      return false;

   bool is_or = instr->opcode == aco_opcode::s_or_b64 || instr->opcode == aco_opcode::s_or_b32;
   aco_opcode expected_nan_test = is_or ? aco_opcode::v_cmp_u_f32 : aco_opcode::v_cmp_o_f32;

   Instruction* nan_test = follow_operand(ctx, instr->operands[0], true);
   Instruction* cmp = follow_operand(ctx, instr->operands[1], true);
   if (!nan_test || !cmp)
      return false;
   if (nan_test->isSDWA() || cmp->isSDWA())
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
   Instruction* new_instr;
   if (cmp->isVOP3()) {
      VOP3_instruction* new_vop3 =
         create_instruction<VOP3_instruction>(new_op, asVOP3(Format::VOPC), 2, 1);
      VOP3_instruction& cmp_vop3 = cmp->vop3();
      memcpy(new_vop3->abs, cmp_vop3.abs, sizeof(new_vop3->abs));
      memcpy(new_vop3->neg, cmp_vop3.neg, sizeof(new_vop3->neg));
      new_vop3->clamp = cmp_vop3.clamp;
      new_vop3->omod = cmp_vop3.omod;
      new_vop3->opsel = cmp_vop3.opsel;
      new_instr = new_vop3;
   } else {
      new_instr = create_instruction<VOPC_instruction>(new_op, Format::VOPC, 2, 1);
      instr->definitions[0].setHint(vcc);
   }
   new_instr->operands[0] = cmp->operands[0];
   new_instr->operands[1] = cmp->operands[1];
   new_instr->definitions[0] = instr->definitions[0];

   ctx.info[instr->definitions[0].tempId()].label = 0;
   ctx.info[instr->definitions[0].tempId()].set_vopc(new_instr);

   instr.reset(new_instr);

   return true;
}

bool
is_operand_constant(opt_ctx& ctx, Operand op, unsigned bit_size, uint64_t* value)
{
   if (op.isConstant()) {
      *value = op.constantValue64();
      return true;
   } else if (op.isTemp()) {
      unsigned id = original_temp_id(ctx, op.getTemp());
      if (!ctx.info[id].is_constant_or_literal(bit_size))
         return false;
      *value = get_constant_op(ctx, ctx.info[id], bit_size).constantValue64();
      return true;
   }
   return false;
}

bool
is_constant_nan(uint64_t value, unsigned bit_size)
{
   if (bit_size == 16)
      return ((value >> 10) & 0x1f) == 0x1f && (value & 0x3ff);
   else if (bit_size == 32)
      return ((value >> 23) & 0xff) == 0xff && (value & 0x7fffff);
   else
      return ((value >> 52) & 0x7ff) == 0x7ff && (value & 0xfffffffffffff);
}

/* s_or_b64(v_cmp_neq_f32(a, a), cmp(a, #b)) and b is not NaN -> get_unordered(cmp)(a, b)
 * s_and_b64(v_cmp_eq_f32(a, a), cmp(a, #b)) and b is not NaN -> get_ordered(cmp)(a, b) */
bool
combine_constant_comparison_ordering(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->definitions[0].regClass() != ctx.program->lane_mask)
      return false;
   if (instr->definitions[1].isTemp() && ctx.uses[instr->definitions[1].tempId()])
      return false;

   bool is_or = instr->opcode == aco_opcode::s_or_b64 || instr->opcode == aco_opcode::s_or_b32;

   Instruction* nan_test = follow_operand(ctx, instr->operands[0], true);
   Instruction* cmp = follow_operand(ctx, instr->operands[1], true);

   if (!nan_test || !cmp || nan_test->isSDWA() || cmp->isSDWA())
      return false;
   if (nan_test->isSDWA() || cmp->isSDWA())
      return false;

   aco_opcode expected_nan_test = is_or ? aco_opcode::v_cmp_neq_f32 : aco_opcode::v_cmp_eq_f32;
   if (get_f32_cmp(cmp->opcode) == expected_nan_test)
      std::swap(nan_test, cmp);
   else if (get_f32_cmp(nan_test->opcode) != expected_nan_test)
      return false;

   unsigned bit_size = get_cmp_bitsize(cmp->opcode);
   if (!is_cmp(cmp->opcode) || get_cmp_bitsize(nan_test->opcode) != bit_size)
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
      VOP3_instruction& vop3 = nan_test->vop3();
      if (vop3.neg[0] != vop3.neg[1] || vop3.abs[0] != vop3.abs[1] || vop3.opsel == 1 ||
          vop3.opsel == 2)
         return false;
   }

   int constant_operand = -1;
   for (unsigned i = 0; i < 2; i++) {
      if (cmp->operands[i].isTemp() &&
          original_temp_id(ctx, cmp->operands[i].getTemp()) == prop_nan0) {
         constant_operand = !i;
         break;
      }
   }
   if (constant_operand == -1)
      return false;

   uint64_t constant_value;
   if (!is_operand_constant(ctx, cmp->operands[constant_operand], bit_size, &constant_value))
      return false;
   if (is_constant_nan(constant_value, bit_size))
      return false;

   if (cmp->operands[0].isTemp())
      ctx.uses[cmp->operands[0].tempId()]++;
   if (cmp->operands[1].isTemp())
      ctx.uses[cmp->operands[1].tempId()]++;
   decrease_uses(ctx, nan_test);
   decrease_uses(ctx, cmp);

   aco_opcode new_op = is_or ? get_unordered(cmp->opcode) : get_ordered(cmp->opcode);
   Instruction* new_instr;
   if (cmp->isVOP3()) {
      VOP3_instruction* new_vop3 =
         create_instruction<VOP3_instruction>(new_op, asVOP3(Format::VOPC), 2, 1);
      VOP3_instruction& cmp_vop3 = cmp->vop3();
      memcpy(new_vop3->abs, cmp_vop3.abs, sizeof(new_vop3->abs));
      memcpy(new_vop3->neg, cmp_vop3.neg, sizeof(new_vop3->neg));
      new_vop3->clamp = cmp_vop3.clamp;
      new_vop3->omod = cmp_vop3.omod;
      new_vop3->opsel = cmp_vop3.opsel;
      new_instr = new_vop3;
   } else {
      new_instr = create_instruction<VOPC_instruction>(new_op, Format::VOPC, 2, 1);
      instr->definitions[0].setHint(vcc);
   }
   new_instr->operands[0] = cmp->operands[0];
   new_instr->operands[1] = cmp->operands[1];
   new_instr->definitions[0] = instr->definitions[0];

   ctx.info[instr->definitions[0].tempId()].label = 0;
   ctx.info[instr->definitions[0].tempId()].set_vopc(new_instr);

   instr.reset(new_instr);

   return true;
}

/* s_andn2(exec, cmp(a, b)) -> get_inverse(cmp)(a, b) */
bool
combine_inverse_comparison(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (!instr->operands[0].isFixed() || instr->operands[0].physReg() != exec)
      return false;
   if (ctx.uses[instr->definitions[1].tempId()])
      return false;

   Instruction* cmp = follow_operand(ctx, instr->operands[1]);
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

   /* This creates a new instruction instead of modifying the existing
    * comparison so that the comparison is done with the correct exec mask. */
   Instruction* new_instr;
   if (cmp->isVOP3()) {
      VOP3_instruction* new_vop3 =
         create_instruction<VOP3_instruction>(new_opcode, asVOP3(Format::VOPC), 2, 1);
      VOP3_instruction& cmp_vop3 = cmp->vop3();
      memcpy(new_vop3->abs, cmp_vop3.abs, sizeof(new_vop3->abs));
      memcpy(new_vop3->neg, cmp_vop3.neg, sizeof(new_vop3->neg));
      new_vop3->clamp = cmp_vop3.clamp;
      new_vop3->omod = cmp_vop3.omod;
      new_vop3->opsel = cmp_vop3.opsel;
      new_instr = new_vop3;
   } else if (cmp->isSDWA()) {
      SDWA_instruction* new_sdwa = create_instruction<SDWA_instruction>(
         new_opcode, (Format)((uint16_t)Format::SDWA | (uint16_t)Format::VOPC), 2, 1);
      SDWA_instruction& cmp_sdwa = cmp->sdwa();
      memcpy(new_sdwa->abs, cmp_sdwa.abs, sizeof(new_sdwa->abs));
      memcpy(new_sdwa->sel, cmp_sdwa.sel, sizeof(new_sdwa->sel));
      memcpy(new_sdwa->neg, cmp_sdwa.neg, sizeof(new_sdwa->neg));
      new_sdwa->dst_sel = cmp_sdwa.dst_sel;
      new_sdwa->clamp = cmp_sdwa.clamp;
      new_sdwa->omod = cmp_sdwa.omod;
      new_instr = new_sdwa;
   } else if (cmp->isDPP()) {
      DPP_instruction* new_dpp = create_instruction<DPP_instruction>(
         new_opcode, (Format)((uint16_t)Format::DPP | (uint16_t)Format::VOPC), 2, 1);
      DPP_instruction& cmp_dpp = cmp->dpp();
      memcpy(new_dpp->abs, cmp_dpp.abs, sizeof(new_dpp->abs));
      memcpy(new_dpp->neg, cmp_dpp.neg, sizeof(new_dpp->neg));
      new_dpp->dpp_ctrl = cmp_dpp.dpp_ctrl;
      new_dpp->row_mask = cmp_dpp.row_mask;
      new_dpp->bank_mask = cmp_dpp.bank_mask;
      new_dpp->bound_ctrl = cmp_dpp.bound_ctrl;
      new_instr = new_dpp;
   } else {
      new_instr = create_instruction<VOPC_instruction>(new_opcode, Format::VOPC, 2, 1);
      instr->definitions[0].setHint(vcc);
   }
   new_instr->operands[0] = cmp->operands[0];
   new_instr->operands[1] = cmp->operands[1];
   new_instr->definitions[0] = instr->definitions[0];

   ctx.info[instr->definitions[0].tempId()].label = 0;
   ctx.info[instr->definitions[0].tempId()].set_vopc(new_instr);

   instr.reset(new_instr);

   return true;
}

/* op1(op2(1, 2), 0) if swap = false
 * op1(0, op2(1, 2)) if swap = true */
bool
match_op3_for_vop3(opt_ctx& ctx, aco_opcode op1, aco_opcode op2, Instruction* op1_instr, bool swap,
                   const char* shuffle_str, Operand operands[3], bool neg[3], bool abs[3],
                   uint8_t* opsel, bool* op1_clamp, uint8_t* op1_omod, bool* inbetween_neg,
                   bool* inbetween_abs, bool* inbetween_opsel, bool* precise)
{
   /* checks */
   if (op1_instr->opcode != op1)
      return false;

   Instruction* op2_instr = follow_operand(ctx, op1_instr->operands[swap]);
   if (!op2_instr || op2_instr->opcode != op2)
      return false;
   if (fixed_to_exec(op2_instr->operands[0]) || fixed_to_exec(op2_instr->operands[1]))
      return false;

   VOP3_instruction* op1_vop3 = op1_instr->isVOP3() ? &op1_instr->vop3() : NULL;
   VOP3_instruction* op2_vop3 = op2_instr->isVOP3() ? &op2_instr->vop3() : NULL;

   if (op1_instr->isSDWA() || op2_instr->isSDWA())
      return false;
   if (op1_instr->isDPP() || op2_instr->isDPP())
      return false;

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
      *inbetween_opsel = op1_vop3 ? op1_vop3->opsel & (1 << (unsigned)swap) : false;
   else if (op1_vop3 && op1_vop3->opsel & (1 << (unsigned)swap))
      return false;

   *precise = op1_instr->definitions[0].isPrecise() || op2_instr->definitions[0].isPrecise();

   int shuffle[3];
   shuffle[shuffle_str[0] - '0'] = 0;
   shuffle[shuffle_str[1] - '0'] = 1;
   shuffle[shuffle_str[2] - '0'] = 2;

   operands[shuffle[0]] = op1_instr->operands[!swap];
   neg[shuffle[0]] = op1_vop3 ? op1_vop3->neg[!swap] : false;
   abs[shuffle[0]] = op1_vop3 ? op1_vop3->abs[!swap] : false;
   if (op1_vop3 && (op1_vop3->opsel & (1 << (unsigned)!swap)))
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

void
create_vop3_for_op3(opt_ctx& ctx, aco_opcode opcode, aco_ptr<Instruction>& instr,
                    Operand operands[3], bool neg[3], bool abs[3], uint8_t opsel, bool clamp,
                    unsigned omod)
{
   VOP3_instruction* new_instr = create_instruction<VOP3_instruction>(opcode, Format::VOP3, 3, 1);
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

bool
combine_three_valu_op(opt_ctx& ctx, aco_ptr<Instruction>& instr, aco_opcode op2, aco_opcode new_op,
                      const char* shuffle, uint8_t ops)
{
   for (unsigned swap = 0; swap < 2; swap++) {
      if (!((1 << swap) & ops))
         continue;

      Operand operands[3];
      bool neg[3], abs[3], clamp, precise;
      uint8_t opsel = 0, omod = 0;
      if (match_op3_for_vop3(ctx, instr->opcode, op2, instr.get(), swap, shuffle, operands, neg,
                             abs, &opsel, &clamp, &omod, NULL, NULL, NULL, &precise)) {
         ctx.uses[instr->operands[swap].tempId()]--;
         create_vop3_for_op3(ctx, new_op, instr, operands, neg, abs, opsel, clamp, omod);
         return true;
      }
   }
   return false;
}

/* creates v_lshl_add_u32, v_lshl_or_b32 or v_and_or_b32 */
bool
combine_add_or_then_and_lshl(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   bool is_or = instr->opcode == aco_opcode::v_or_b32;
   aco_opcode new_op_lshl = is_or ? aco_opcode::v_lshl_or_b32 : aco_opcode::v_lshl_add_u32;

   if (is_or && combine_three_valu_op(ctx, instr, aco_opcode::s_and_b32, aco_opcode::v_and_or_b32,
                                      "120", 1 | 2))
      return true;
   if (is_or && combine_three_valu_op(ctx, instr, aco_opcode::v_and_b32, aco_opcode::v_and_or_b32,
                                      "120", 1 | 2))
      return true;
   if (combine_three_valu_op(ctx, instr, aco_opcode::s_lshl_b32, new_op_lshl, "120", 1 | 2))
      return true;
   if (combine_three_valu_op(ctx, instr, aco_opcode::v_lshlrev_b32, new_op_lshl, "210", 1 | 2))
      return true;

   if (instr->isSDWA() || instr->isDPP())
      return false;

   /* v_or_b32(p_extract(a, 0, 8/16, 0), b) -> v_and_or_b32(a, 0xff/0xffff, b)
    * v_or_b32(p_insert(a, 0, 8/16), b) -> v_and_or_b32(a, 0xff/0xffff, b)
    * v_or_b32(p_insert(a, 24/16, 8/16), b) -> v_lshl_or_b32(a, 24/16, b)
    * v_add_u32(p_insert(a, 24/16, 8/16), b) -> v_lshl_add_b32(a, 24/16, b)
    */
   for (unsigned i = 0; i < 2; i++) {
      Instruction* extins = follow_operand(ctx, instr->operands[i]);
      if (!extins)
         continue;

      aco_opcode op;
      Operand operands[3];

      if (extins->opcode == aco_opcode::p_insert &&
          (extins->operands[1].constantValue() + 1) * extins->operands[2].constantValue() == 32) {
         op = new_op_lshl;
         operands[1] =
            Operand::c32(extins->operands[1].constantValue() * extins->operands[2].constantValue());
      } else if (is_or &&
                 (extins->opcode == aco_opcode::p_insert ||
                  (extins->opcode == aco_opcode::p_extract &&
                   extins->operands[3].constantEquals(0))) &&
                 extins->operands[1].constantEquals(0)) {
         op = aco_opcode::v_and_or_b32;
         operands[1] = Operand::c32(extins->operands[2].constantEquals(8) ? 0xffu : 0xffffu);
      } else {
         continue;
      }

      operands[0] = extins->operands[0];
      operands[2] = instr->operands[!i];

      if (!check_vop3_operands(ctx, 3, operands))
         continue;

      bool neg[3] = {}, abs[3] = {};
      uint8_t opsel = 0, omod = 0;
      bool clamp = false;
      if (instr->isVOP3())
         clamp = instr->vop3().clamp;

      ctx.uses[instr->operands[i].tempId()]--;
      create_vop3_for_op3(ctx, op, instr, operands, neg, abs, opsel, clamp, omod);
      return true;
   }

   return false;
}

bool
combine_minmax(opt_ctx& ctx, aco_ptr<Instruction>& instr, aco_opcode opposite, aco_opcode minmax3)
{
   /* TODO: this can handle SDWA min/max instructions by using opsel */
   if (combine_three_valu_op(ctx, instr, instr->opcode, minmax3, "012", 1 | 2))
      return true;

   /* min(-max(a, b), c) -> min3(c, -a, -b) *
    * max(-min(a, b), c) -> max3(c, -a, -b) */
   for (unsigned swap = 0; swap < 2; swap++) {
      Operand operands[3];
      bool neg[3], abs[3], clamp, precise;
      uint8_t opsel = 0, omod = 0;
      bool inbetween_neg;
      if (match_op3_for_vop3(ctx, instr->opcode, opposite, instr.get(), swap, "012", operands, neg,
                             abs, &opsel, &clamp, &omod, &inbetween_neg, NULL, NULL, &precise) &&
          inbetween_neg) {
         ctx.uses[instr->operands[swap].tempId()]--;
         neg[1] = !neg[1];
         neg[2] = !neg[2];
         create_vop3_for_op3(ctx, minmax3, instr, operands, neg, abs, opsel, clamp, omod);
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
bool
combine_salu_not_bitwise(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   /* checks */
   if (!instr->operands[0].isTemp())
      return false;
   if (instr->definitions[1].isTemp() && ctx.uses[instr->definitions[1].tempId()])
      return false;

   Instruction* op2_instr = follow_operand(ctx, instr->operands[0]);
   if (!op2_instr)
      return false;
   switch (op2_instr->opcode) {
   case aco_opcode::s_and_b32:
   case aco_opcode::s_or_b32:
   case aco_opcode::s_xor_b32:
   case aco_opcode::s_and_b64:
   case aco_opcode::s_or_b64:
   case aco_opcode::s_xor_b64: break;
   default: return false;
   }

   /* create instruction */
   std::swap(instr->definitions[0], op2_instr->definitions[0]);
   std::swap(instr->definitions[1], op2_instr->definitions[1]);
   ctx.uses[instr->operands[0].tempId()]--;
   ctx.info[op2_instr->definitions[0].tempId()].label = 0;

   switch (op2_instr->opcode) {
   case aco_opcode::s_and_b32: op2_instr->opcode = aco_opcode::s_nand_b32; break;
   case aco_opcode::s_or_b32: op2_instr->opcode = aco_opcode::s_nor_b32; break;
   case aco_opcode::s_xor_b32: op2_instr->opcode = aco_opcode::s_xnor_b32; break;
   case aco_opcode::s_and_b64: op2_instr->opcode = aco_opcode::s_nand_b64; break;
   case aco_opcode::s_or_b64: op2_instr->opcode = aco_opcode::s_nor_b64; break;
   case aco_opcode::s_xor_b64: op2_instr->opcode = aco_opcode::s_xnor_b64; break;
   default: break;
   }

   return true;
}

/* s_and_b32(a, s_not_b32(b)) -> s_andn2_b32(a, b)
 * s_or_b32(a, s_not_b32(b)) -> s_orn2_b32(a, b)
 * s_and_b64(a, s_not_b64(b)) -> s_andn2_b64(a, b)
 * s_or_b64(a, s_not_b64(b)) -> s_orn2_b64(a, b) */
bool
combine_salu_n2(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->definitions[0].isTemp() && ctx.info[instr->definitions[0].tempId()].is_uniform_bool())
      return false;

   for (unsigned i = 0; i < 2; i++) {
      Instruction* op2_instr = follow_operand(ctx, instr->operands[i]);
      if (!op2_instr || (op2_instr->opcode != aco_opcode::s_not_b32 &&
                         op2_instr->opcode != aco_opcode::s_not_b64))
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
      case aco_opcode::s_and_b32: instr->opcode = aco_opcode::s_andn2_b32; break;
      case aco_opcode::s_or_b32: instr->opcode = aco_opcode::s_orn2_b32; break;
      case aco_opcode::s_and_b64: instr->opcode = aco_opcode::s_andn2_b64; break;
      case aco_opcode::s_or_b64: instr->opcode = aco_opcode::s_orn2_b64; break;
      default: break;
      }

      return true;
   }
   return false;
}

/* s_add_{i32,u32}(a, s_lshl_b32(b, <n>)) -> s_lshl<n>_add_u32(a, b) */
bool
combine_salu_lshl_add(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->opcode == aco_opcode::s_add_i32 && ctx.uses[instr->definitions[1].tempId()])
      return false;

   for (unsigned i = 0; i < 2; i++) {
      Instruction* op2_instr = follow_operand(ctx, instr->operands[i], true);
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

      instr->opcode = std::array<aco_opcode, 4>{
         aco_opcode::s_lshl1_add_u32, aco_opcode::s_lshl2_add_u32, aco_opcode::s_lshl3_add_u32,
         aco_opcode::s_lshl4_add_u32}[shift - 1];

      return true;
   }
   return false;
}

bool
combine_add_sub_b2i(opt_ctx& ctx, aco_ptr<Instruction>& instr, aco_opcode new_op, uint8_t ops)
{
   if (instr->usesModifiers())
      return false;

   for (unsigned i = 0; i < 2; i++) {
      if (!((1 << i) & ops))
         continue;
      if (instr->operands[i].isTemp() && ctx.info[instr->operands[i].tempId()].is_b2i() &&
          ctx.uses[instr->operands[i].tempId()] == 1) {

         aco_ptr<Instruction> new_instr;
         if (instr->operands[!i].isTemp() &&
             instr->operands[!i].getTemp().type() == RegType::vgpr) {
            new_instr.reset(create_instruction<VOP2_instruction>(new_op, Format::VOP2, 3, 2));
         } else if (ctx.program->chip_class >= GFX10 ||
                    (instr->operands[!i].isConstant() && !instr->operands[!i].isLiteral())) {
            new_instr.reset(
               create_instruction<VOP3_instruction>(new_op, asVOP3(Format::VOP2), 3, 2));
         } else {
            return false;
         }
         ctx.uses[instr->operands[i].tempId()]--;
         new_instr->definitions[0] = instr->definitions[0];
         if (instr->definitions.size() == 2) {
            new_instr->definitions[1] = instr->definitions[1];
         } else {
            new_instr->definitions[1] =
               Definition(ctx.program->allocateTmp(ctx.program->lane_mask));
            /* Make sure the uses vector is large enough and the number of
             * uses properly initialized to 0.
             */
            ctx.uses.push_back(0);
         }
         new_instr->definitions[1].setHint(vcc);
         new_instr->operands[0] = Operand::zero();
         new_instr->operands[1] = instr->operands[!i];
         new_instr->operands[2] = Operand(ctx.info[instr->operands[i].tempId()].temp);
         instr = std::move(new_instr);
         ctx.info[instr->definitions[0].tempId()].set_add_sub(instr.get());
         return true;
      }
   }

   return false;
}

bool
combine_add_bcnt(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->usesModifiers())
      return false;

   for (unsigned i = 0; i < 2; i++) {
      Instruction* op_instr = follow_operand(ctx, instr->operands[i]);
      if (op_instr && op_instr->opcode == aco_opcode::v_bcnt_u32_b32 &&
          !op_instr->usesModifiers() && op_instr->operands[0].isTemp() &&
          op_instr->operands[0].getTemp().type() == RegType::vgpr &&
          op_instr->operands[1].constantEquals(0)) {
         aco_ptr<Instruction> new_instr{
            create_instruction<VOP3_instruction>(aco_opcode::v_bcnt_u32_b32, Format::VOP3, 2, 1)};
         ctx.uses[instr->operands[i].tempId()]--;
         new_instr->operands[0] = op_instr->operands[0];
         new_instr->operands[1] = instr->operands[!i];
         new_instr->definitions[0] = instr->definitions[0];
         instr = std::move(new_instr);
         ctx.info[instr->definitions[0].tempId()].label = 0;

         return true;
      }
   }

   return false;
}

bool
get_minmax_info(aco_opcode op, aco_opcode* min, aco_opcode* max, aco_opcode* min3, aco_opcode* max3,
                aco_opcode* med3, bool* some_gfx9_only)
{
   switch (op) {
#define MINMAX(type, gfx9)                                                                         \
   case aco_opcode::v_min_##type:                                                                  \
   case aco_opcode::v_max_##type:                                                                  \
   case aco_opcode::v_med3_##type:                                                                 \
      *min = aco_opcode::v_min_##type;                                                             \
      *max = aco_opcode::v_max_##type;                                                             \
      *med3 = aco_opcode::v_med3_##type;                                                           \
      *min3 = aco_opcode::v_min3_##type;                                                           \
      *max3 = aco_opcode::v_max3_##type;                                                           \
      *some_gfx9_only = gfx9;                                                                      \
      return true;
      MINMAX(f32, false)
      MINMAX(u32, false)
      MINMAX(i32, false)
      MINMAX(f16, true)
      MINMAX(u16, true)
      MINMAX(i16, true)
#undef MINMAX
   default: return false;
   }
}

/* when ub > lb:
 * v_min_{f,u,i}{16,32}(v_max_{f,u,i}{16,32}(a, lb), ub) -> v_med3_{f,u,i}{16,32}(a, lb, ub)
 * v_max_{f,u,i}{16,32}(v_min_{f,u,i}{16,32}(a, ub), lb) -> v_med3_{f,u,i}{16,32}(a, lb, ub)
 */
bool
combine_clamp(opt_ctx& ctx, aco_ptr<Instruction>& instr, aco_opcode min, aco_opcode max,
              aco_opcode med)
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

   for (unsigned swap = 0; swap < 2; swap++) {
      Operand operands[3];
      bool neg[3], abs[3], clamp, precise;
      uint8_t opsel = 0, omod = 0;
      if (match_op3_for_vop3(ctx, instr->opcode, other_op, instr.get(), swap, "012", operands, neg,
                             abs, &opsel, &clamp, &omod, NULL, NULL, NULL, &precise)) {
         /* max(min(src, upper), lower) returns upper if src is NaN, but
          * med3(src, lower, upper) returns lower.
          */
         if (precise && instr->opcode != min)
            continue;

         int const0_idx = -1, const1_idx = -1;
         uint32_t const0 = 0, const1 = 0;
         for (int i = 0; i < 3; i++) {
            uint32_t val;
            if (operands[i].isConstant()) {
               val = operands[i].constantValue();
            } else if (operands[i].isTemp() &&
                       ctx.info[operands[i].tempId()].is_constant_or_literal(32)) {
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
            if (abs[const0_idx])
               const0_f = fabsf(const0_f);
            if (abs[const1_idx])
               const1_f = fabsf(const1_f);
            if (neg[const0_idx])
               const0_f = -const0_f;
            if (neg[const1_idx])
               const1_f = -const1_f;
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
            int32_t const0_i =
               const0 & 0x80000000u ? -2147483648 + (int32_t)(const0 & 0x7fffffffu) : const0;
            int32_t const1_i =
               const1 & 0x80000000u ? -2147483648 + (int32_t)(const1 & 0x7fffffffu) : const1;
            lower_idx = const0_i < const1_i ? const0_idx : const1_idx;
            break;
         }
         case aco_opcode::v_min_i16: {
            int16_t const0_i = const0 & 0x8000u ? -32768 + (int16_t)(const0 & 0x7fffu) : const0;
            int16_t const1_i = const1 & 0x8000u ? -32768 + (int16_t)(const1 & 0x7fffu) : const1;
            lower_idx = const0_i < const1_i ? const0_idx : const1_idx;
            break;
         }
         default: break;
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

         return true;
      }
   }

   return false;
}

void
apply_sgprs(opt_ctx& ctx, aco_ptr<Instruction>& instr)
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
      if (is_copy_label(ctx, instr, info) && info.temp.type() == RegType::sgpr)
         operand_mask |= 1u << i;
      if (info.is_extract() && info.instr->operands[0].getTemp().type() == RegType::sgpr)
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

      ssa_info& info = ctx.info[sgpr_info_id];

      /* Applying two sgprs require making it VOP3, so don't do it unless it's
       * definitively beneficial.
       * TODO: this is too conservative because later the use count could be reduced to 1 */
      if (!info.is_extract() && num_sgprs && ctx.uses[sgpr_info_id] > 1 && !instr->isVOP3() &&
          !instr->isSDWA() && instr->format != Format::VOP3P)
         break;

      Temp sgpr = info.is_extract() ? info.instr->operands[0].getTemp() : info.temp;
      bool new_sgpr = sgpr.id() != sgpr_ids[0] && sgpr.id() != sgpr_ids[1];
      if (new_sgpr && num_sgprs >= max_sgprs)
         continue;

      if (sgpr_idx == 0)
         instr->format = withoutDPP(instr->format);

      if (sgpr_idx == 0 || instr->isVOP3() || instr->isSDWA() || instr->isVOP3P() ||
          info.is_extract()) {
         /* can_apply_extract() checks SGPR encoding restrictions */
         if (info.is_extract() && can_apply_extract(ctx, instr, sgpr_idx, info))
            apply_extract(ctx, instr, sgpr_idx, info);
         else if (info.is_extract())
            continue;
         instr->operands[sgpr_idx] = Operand(sgpr);
      } else if (can_swap_operands(instr, &instr->opcode)) {
         instr->operands[sgpr_idx] = instr->operands[0];
         instr->operands[0] = Operand(sgpr);
         /* swap bits using a 4-entry LUT */
         uint32_t swapped = (0x3120 >> (operand_mask & 0x3)) & 0xf;
         operand_mask = (operand_mask & ~0x3) | swapped;
      } else if (can_use_VOP3(ctx, instr) && !info.is_extract()) {
         to_VOP3(ctx, instr);
         instr->operands[sgpr_idx] = Operand(sgpr);
      } else {
         continue;
      }

      if (new_sgpr)
         sgpr_ids[num_sgprs++] = sgpr.id();
      ctx.uses[sgpr_info_id]--;
      ctx.uses[sgpr.id()]++;

      /* TODO: handle when it's a VGPR */
      if ((ctx.info[sgpr.id()].label & (label_extract | label_temp)) &&
          ctx.info[sgpr.id()].temp.type() == RegType::sgpr)
         operand_mask |= 1u << sgpr_idx;
   }
}

template <typename T>
bool
apply_omod_clamp_helper(opt_ctx& ctx, T* instr, ssa_info& def_info)
{
   if (!def_info.is_clamp() && (instr->clamp || instr->omod))
      return false;

   if (def_info.is_omod2())
      instr->omod = 1;
   else if (def_info.is_omod4())
      instr->omod = 2;
   else if (def_info.is_omod5())
      instr->omod = 3;
   else if (def_info.is_clamp())
      instr->clamp = true;

   return true;
}

/* apply omod / clamp modifiers if the def is used only once and the instruction can have modifiers */
bool
apply_omod_clamp(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->definitions.empty() || ctx.uses[instr->definitions[0].tempId()] != 1 ||
       !instr_info.can_use_output_modifiers[(int)instr->opcode])
      return false;

   bool can_vop3 = can_use_VOP3(ctx, instr);
   if (!instr->isSDWA() && !can_vop3)
      return false;

   /* omod flushes -0 to +0 and has no effect if denormals are enabled */
   bool can_use_omod = (can_vop3 || ctx.program->chip_class >= GFX9); /* SDWA omod is GFX9+ */
   if (instr->definitions[0].bytes() == 4)
      can_use_omod =
         can_use_omod && ctx.fp_mode.denorm32 == 0 && !ctx.fp_mode.preserve_signed_zero_inf_nan32;
   else
      can_use_omod = can_use_omod && ctx.fp_mode.denorm16_64 == 0 &&
                     !ctx.fp_mode.preserve_signed_zero_inf_nan16_64;

   ssa_info& def_info = ctx.info[instr->definitions[0].tempId()];

   uint64_t omod_labels = label_omod2 | label_omod4 | label_omod5;
   if (!def_info.is_clamp() && !(can_use_omod && (def_info.label & omod_labels)))
      return false;
   /* if the omod/clamp instruction is dead, then the single user of this
    * instruction is a different instruction */
   if (!ctx.uses[def_info.instr->definitions[0].tempId()])
      return false;

   /* MADs/FMAs are created later, so we don't have to update the original add */
   assert(!ctx.info[instr->definitions[0].tempId()].is_mad());

   if (instr->isSDWA()) {
      if (!apply_omod_clamp_helper(ctx, &instr->sdwa(), def_info))
         return false;
   } else {
      to_VOP3(ctx, instr);
      if (!apply_omod_clamp_helper(ctx, &instr->vop3(), def_info))
         return false;
   }

   instr->definitions[0].swapTemp(def_info.instr->definitions[0]);
   ctx.info[instr->definitions[0].tempId()].label &= label_clamp | label_insert;
   ctx.uses[def_info.instr->definitions[0].tempId()]--;

   return true;
}

/* Combine an p_insert (or p_extract, in some cases) instruction with instr.
 * p_insert(instr(...)) -> instr_insert().
 */
bool
apply_insert(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->definitions.empty() || ctx.uses[instr->definitions[0].tempId()] != 1)
      return false;

   ssa_info& def_info = ctx.info[instr->definitions[0].tempId()];
   if (!def_info.is_insert())
      return false;
   /* if the insert instruction is dead, then the single user of this
    * instruction is a different instruction */
   if (!ctx.uses[def_info.instr->definitions[0].tempId()])
      return false;

   /* MADs/FMAs are created later, so we don't have to update the original add */
   assert(!ctx.info[instr->definitions[0].tempId()].is_mad());

   SubdwordSel sel = parse_insert(def_info.instr);
   assert(sel);

   if (instr->isVOP3() && sel.size() == 2 && !sel.sign_extend() &&
       can_use_opsel(ctx.program->chip_class, instr->opcode, 3, sel.offset())) {
      if (instr->vop3().opsel & (1 << 3))
         return false;
      if (sel.offset())
         instr->vop3().opsel |= 1 << 3;
   } else {
      if (!can_use_SDWA(ctx.program->chip_class, instr, true))
         return false;

      to_SDWA(ctx, instr);
      if (instr->sdwa().dst_sel.size() != 4)
         return false;
      static_cast<SDWA_instruction*>(instr.get())->dst_sel = sel;
   }

   instr->definitions[0].swapTemp(def_info.instr->definitions[0]);
   ctx.info[instr->definitions[0].tempId()].label = 0;
   ctx.uses[def_info.instr->definitions[0].tempId()]--;

   return true;
}

/* Remove superfluous extract after ds_read like so:
 * p_extract(ds_read_uN(), 0, N, 0) -> ds_read_uN()
 */
bool
apply_ds_extract(opt_ctx& ctx, aco_ptr<Instruction>& extract)
{
   /* Check if p_extract has a usedef operand and is the only user. */
   if (!ctx.info[extract->operands[0].tempId()].is_usedef() ||
       ctx.uses[extract->operands[0].tempId()] > 1)
      return false;

   /* Check if the usedef is a DS instruction. */
   Instruction* ds = ctx.info[extract->operands[0].tempId()].instr;
   if (ds->format != Format::DS)
      return false;

   unsigned extract_idx = extract->operands[1].constantValue();
   unsigned bits_extracted = extract->operands[2].constantValue();
   unsigned sign_ext = extract->operands[3].constantValue();
   unsigned dst_bitsize = extract->definitions[0].bytes() * 8u;

   /* TODO: These are doable, but probably don't occour too often. */
   if (extract_idx || sign_ext || dst_bitsize != 32)
      return false;

   unsigned bits_loaded = 0;
   if (ds->opcode == aco_opcode::ds_read_u8 || ds->opcode == aco_opcode::ds_read_u8_d16)
      bits_loaded = 8;
   else if (ds->opcode == aco_opcode::ds_read_u16 || ds->opcode == aco_opcode::ds_read_u16_d16)
      bits_loaded = 16;
   else
      return false;

   /* Shrink the DS load if the extracted bit size is smaller. */
   bits_loaded = MIN2(bits_loaded, bits_extracted);

   /* Change the DS opcode so it writes the full register. */
   if (bits_loaded == 8)
      ds->opcode = aco_opcode::ds_read_u8;
   else if (bits_loaded == 16)
      ds->opcode = aco_opcode::ds_read_u16;
   else
      unreachable("Forgot to add DS opcode above.");

   /* The DS now produces the exact same thing as the extract, remove the extract. */
   std::swap(ds->definitions[0], extract->definitions[0]);
   ctx.uses[extract->definitions[0].tempId()] = 0;
   ctx.info[ds->definitions[0].tempId()].label = 0;
   return true;
}

/* v_and(a, v_subbrev_co(0, 0, vcc)) -> v_cndmask(0, a, vcc) */
bool
combine_and_subbrev(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->usesModifiers())
      return false;

   for (unsigned i = 0; i < 2; i++) {
      Instruction* op_instr = follow_operand(ctx, instr->operands[i], true);
      if (op_instr && op_instr->opcode == aco_opcode::v_subbrev_co_u32 &&
          op_instr->operands[0].constantEquals(0) && op_instr->operands[1].constantEquals(0) &&
          !op_instr->usesModifiers()) {

         aco_ptr<Instruction> new_instr;
         if (instr->operands[!i].isTemp() &&
             instr->operands[!i].getTemp().type() == RegType::vgpr) {
            new_instr.reset(
               create_instruction<VOP2_instruction>(aco_opcode::v_cndmask_b32, Format::VOP2, 3, 1));
         } else if (ctx.program->chip_class >= GFX10 ||
                    (instr->operands[!i].isConstant() && !instr->operands[!i].isLiteral())) {
            new_instr.reset(create_instruction<VOP3_instruction>(aco_opcode::v_cndmask_b32,
                                                                 asVOP3(Format::VOP2), 3, 1));
         } else {
            return false;
         }

         ctx.uses[instr->operands[i].tempId()]--;
         if (ctx.uses[instr->operands[i].tempId()])
            ctx.uses[op_instr->operands[2].tempId()]++;

         new_instr->operands[0] = Operand::zero();
         new_instr->operands[1] = instr->operands[!i];
         new_instr->operands[2] = Operand(op_instr->operands[2]);
         new_instr->definitions[0] = instr->definitions[0];
         instr = std::move(new_instr);
         ctx.info[instr->definitions[0].tempId()].label = 0;
         return true;
      }
   }

   return false;
}

/* v_add_co(c, s_lshl(a, b)) -> v_mad_u32_u24(a, 1<<b, c)
 * v_add_co(c, v_lshlrev(a, b)) -> v_mad_u32_u24(b, 1<<a, c)
 * v_sub(c, s_lshl(a, b)) -> v_mad_i32_i24(a, -(1<<b), c)
 * v_sub(c, v_lshlrev(a, b)) -> v_mad_i32_i24(b, -(1<<a), c)
 */
bool
combine_add_lshl(opt_ctx& ctx, aco_ptr<Instruction>& instr, bool is_sub)
{
   if (instr->usesModifiers())
      return false;

   /* Substractions: start at operand 1 to avoid mixup such as
    * turning v_sub(v_lshlrev(a, b), c) into v_mad_i32_i24(b, -(1<<a), c)
    */
   unsigned start_op_idx = is_sub ? 1 : 0;

   /* Don't allow 24-bit operands on subtraction because
    * v_mad_i32_i24 applies a sign extension.
    */
   bool allow_24bit = !is_sub;

   for (unsigned i = start_op_idx; i < 2; i++) {
      Instruction* op_instr = follow_operand(ctx, instr->operands[i]);
      if (!op_instr)
         continue;

      if (op_instr->opcode != aco_opcode::s_lshl_b32 &&
          op_instr->opcode != aco_opcode::v_lshlrev_b32)
         continue;

      int shift_op_idx = op_instr->opcode == aco_opcode::s_lshl_b32 ? 1 : 0;

      if (op_instr->operands[shift_op_idx].isConstant() &&
          ((allow_24bit && op_instr->operands[!shift_op_idx].is24bit()) ||
           op_instr->operands[!shift_op_idx].is16bit())) {
         uint32_t multiplier = 1 << (op_instr->operands[shift_op_idx].constantValue() % 32u);
         if (is_sub)
            multiplier = -multiplier;
         if (is_sub ? (multiplier < 0xff800000) : (multiplier > 0xffffff))
            continue;

         Operand ops[3] = {
            op_instr->operands[!shift_op_idx],
            Operand::c32(multiplier),
            instr->operands[!i],
         };
         if (!check_vop3_operands(ctx, 3, ops))
            return false;

         ctx.uses[instr->operands[i].tempId()]--;

         aco_opcode mad_op = is_sub ? aco_opcode::v_mad_i32_i24 : aco_opcode::v_mad_u32_u24;
         aco_ptr<VOP3_instruction> new_instr{
            create_instruction<VOP3_instruction>(mad_op, Format::VOP3, 3, 1)};
         for (unsigned op_idx = 0; op_idx < 3; ++op_idx)
            new_instr->operands[op_idx] = ops[op_idx];
         new_instr->definitions[0] = instr->definitions[0];
         instr = std::move(new_instr);
         ctx.info[instr->definitions[0].tempId()].label = 0;
         return true;
      }
   }

   return false;
}

void
propagate_swizzles(VOP3P_instruction* instr, uint8_t opsel_lo, uint8_t opsel_hi)
{
   /* propagate swizzles which apply to a result down to the instruction's operands:
    * result = a.xy + b.xx -> result.yx = a.yx + b.xx */
   assert((opsel_lo & 1) == opsel_lo);
   assert((opsel_hi & 1) == opsel_hi);
   uint8_t tmp_lo = instr->opsel_lo;
   uint8_t tmp_hi = instr->opsel_hi;
   bool neg_lo[3] = {instr->neg_lo[0], instr->neg_lo[1], instr->neg_lo[2]};
   bool neg_hi[3] = {instr->neg_hi[0], instr->neg_hi[1], instr->neg_hi[2]};
   if (opsel_lo == 1) {
      instr->opsel_lo = tmp_hi;
      for (unsigned i = 0; i < 3; i++)
         instr->neg_lo[i] = neg_hi[i];
   }
   if (opsel_hi == 0) {
      instr->opsel_hi = tmp_lo;
      for (unsigned i = 0; i < 3; i++)
         instr->neg_hi[i] = neg_lo[i];
   }
}

void
combine_vop3p(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   VOP3P_instruction* vop3p = &instr->vop3p();

   /* apply clamp */
   if (instr->opcode == aco_opcode::v_pk_mul_f16 && instr->operands[1].constantEquals(0x3C00) &&
       vop3p->clamp && instr->operands[0].isTemp() && ctx.uses[instr->operands[0].tempId()] == 1) {

      ssa_info& info = ctx.info[instr->operands[0].tempId()];
      if (info.is_vop3p() && instr_info.can_use_output_modifiers[(int)info.instr->opcode]) {
         VOP3P_instruction* candidate = &ctx.info[instr->operands[0].tempId()].instr->vop3p();
         candidate->clamp = true;
         propagate_swizzles(candidate, vop3p->opsel_lo, vop3p->opsel_hi);
         instr->definitions[0].swapTemp(candidate->definitions[0]);
         ctx.info[candidate->definitions[0].tempId()].instr = candidate;
         ctx.uses[instr->definitions[0].tempId()]--;
         return;
      }
   }

   /* check for fneg modifiers */
   if (instr_info.can_use_input_modifiers[(int)instr->opcode]) {
      /* at this point, we only have 2-operand instructions */
      assert(instr->operands.size() == 2);
      for (unsigned i = 0; i < 2; i++) {
         Operand& op = instr->operands[i];
         if (!op.isTemp())
            continue;

         ssa_info& info = ctx.info[op.tempId()];
         if (info.is_vop3p() && info.instr->opcode == aco_opcode::v_pk_mul_f16 &&
             info.instr->operands[1].constantEquals(0xBC00)) {
            Operand ops[2] = {instr->operands[!i], info.instr->operands[0]};
            if (!check_vop3_operands(ctx, 2, ops))
               continue;

            VOP3P_instruction* fneg = &info.instr->vop3p();
            if (fneg->clamp)
               continue;
            instr->operands[i] = fneg->operands[0];

            /* opsel_lo/hi is either 0 or 1:
             * if 0 - pick selection from fneg->lo
             * if 1 - pick selection from fneg->hi
             */
            bool opsel_lo = vop3p->opsel_lo & (1 << i);
            bool opsel_hi = vop3p->opsel_hi & (1 << i);
            vop3p->neg_lo[i] ^= true ^ (opsel_lo ? fneg->neg_hi[0] : fneg->neg_lo[0]);
            vop3p->neg_hi[i] ^= true ^ (opsel_hi ? fneg->neg_hi[0] : fneg->neg_lo[0]);
            vop3p->opsel_lo ^= ((opsel_lo ? ~fneg->opsel_hi : fneg->opsel_lo) & 1) << i;
            vop3p->opsel_hi ^= ((opsel_hi ? ~fneg->opsel_hi : fneg->opsel_lo) & 1) << i;

            if (--ctx.uses[fneg->definitions[0].tempId()])
               ctx.uses[fneg->operands[0].tempId()]++;
         }
      }
   }

   if (instr->opcode == aco_opcode::v_pk_add_f16 || instr->opcode == aco_opcode::v_pk_add_u16) {
      bool fadd = instr->opcode == aco_opcode::v_pk_add_f16;
      if (fadd && instr->definitions[0].isPrecise())
         return;

      Instruction* mul_instr = nullptr;
      unsigned add_op_idx = 0;
      uint8_t opsel_lo = 0, opsel_hi = 0;
      uint32_t uses = UINT32_MAX;

      /* find the 'best' mul instruction to combine with the add */
      for (unsigned i = 0; i < 2; i++) {
         if (!instr->operands[i].isTemp() || !ctx.info[instr->operands[i].tempId()].is_vop3p())
            continue;
         ssa_info& info = ctx.info[instr->operands[i].tempId()];
         if (fadd) {
            if (info.instr->opcode != aco_opcode::v_pk_mul_f16 ||
                info.instr->definitions[0].isPrecise())
               continue;
         } else {
            if (info.instr->opcode != aco_opcode::v_pk_mul_lo_u16)
               continue;
         }

         Operand op[3] = {info.instr->operands[0], info.instr->operands[1], instr->operands[1 - i]};
         if (ctx.uses[instr->operands[i].tempId()] >= uses || !check_vop3_operands(ctx, 3, op))
            continue;

         /* no clamp allowed between mul and add */
         if (info.instr->vop3p().clamp)
            continue;

         mul_instr = info.instr;
         add_op_idx = 1 - i;
         opsel_lo = (vop3p->opsel_lo >> i) & 1;
         opsel_hi = (vop3p->opsel_hi >> i) & 1;
         uses = ctx.uses[instr->operands[i].tempId()];
      }

      if (!mul_instr)
         return;

      /* convert to mad */
      Operand op[3] = {mul_instr->operands[0], mul_instr->operands[1], instr->operands[add_op_idx]};
      ctx.uses[mul_instr->definitions[0].tempId()]--;
      if (ctx.uses[mul_instr->definitions[0].tempId()]) {
         if (op[0].isTemp())
            ctx.uses[op[0].tempId()]++;
         if (op[1].isTemp())
            ctx.uses[op[1].tempId()]++;
      }

      /* turn packed mul+add into v_pk_fma_f16 */
      assert(mul_instr->isVOP3P());
      aco_opcode mad = fadd ? aco_opcode::v_pk_fma_f16 : aco_opcode::v_pk_mad_u16;
      aco_ptr<VOP3P_instruction> fma{
         create_instruction<VOP3P_instruction>(mad, Format::VOP3P, 3, 1)};
      VOP3P_instruction* mul = &mul_instr->vop3p();
      for (unsigned i = 0; i < 2; i++) {
         fma->operands[i] = op[i];
         fma->neg_lo[i] = mul->neg_lo[i];
         fma->neg_hi[i] = mul->neg_hi[i];
      }
      fma->operands[2] = op[2];
      fma->clamp = vop3p->clamp;
      fma->opsel_lo = mul->opsel_lo;
      fma->opsel_hi = mul->opsel_hi;
      propagate_swizzles(fma.get(), opsel_lo, opsel_hi);
      fma->opsel_lo |= (vop3p->opsel_lo << (2 - add_op_idx)) & 0x4;
      fma->opsel_hi |= (vop3p->opsel_hi << (2 - add_op_idx)) & 0x4;
      fma->neg_lo[2] = vop3p->neg_lo[add_op_idx];
      fma->neg_hi[2] = vop3p->neg_hi[add_op_idx];
      fma->neg_lo[1] = fma->neg_lo[1] ^ vop3p->neg_lo[1 - add_op_idx];
      fma->neg_hi[1] = fma->neg_hi[1] ^ vop3p->neg_hi[1 - add_op_idx];
      fma->definitions[0] = instr->definitions[0];
      instr = std::move(fma);
      ctx.info[instr->definitions[0].tempId()].set_vop3p(instr.get());
      return;
   }
}

// TODO: we could possibly move the whole label_instruction pass to combine_instruction:
// this would mean that we'd have to fix the instruction uses while value propagation

void
combine_instruction(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->definitions.empty() || is_dead(ctx.uses, instr.get()))
      return;

   if (instr->isVALU()) {
      /* Apply SDWA. Do this after label_instruction() so it can remove
       * label_extract if not all instructions can take SDWA. */
      for (unsigned i = 0; i < instr->operands.size(); i++) {
         Operand& op = instr->operands[i];
         if (!op.isTemp())
            continue;
         ssa_info& info = ctx.info[op.tempId()];
         if (!info.is_extract())
            continue;
         /* if there are that many uses, there are likely better combinations */
         // TODO: delay applying extract to a point where we know better
         if (ctx.uses[op.tempId()] > 4) {
            info.label &= ~label_extract;
            continue;
         }
         if (info.is_extract() &&
             (info.instr->operands[0].getTemp().type() == RegType::vgpr ||
              instr->operands[i].getTemp().type() == RegType::sgpr) &&
             can_apply_extract(ctx, instr, i, info)) {
            apply_extract(ctx, instr, i, info);
            ctx.uses[instr->operands[i].tempId()]--;
            instr->operands[i].setTemp(info.instr->operands[0].getTemp());
         }
      }

      if (can_apply_sgprs(ctx, instr))
         apply_sgprs(ctx, instr);
      while (apply_omod_clamp(ctx, instr))
         ;
      apply_insert(ctx, instr);
   }

   if (instr->isVOP3P())
      return combine_vop3p(ctx, instr);

   if (ctx.info[instr->definitions[0].tempId()].is_vcc_hint()) {
      instr->definitions[0].setHint(vcc);
   }

   if (instr->isSDWA() || instr->isDPP())
      return;

   if (instr->opcode == aco_opcode::p_extract)
      apply_ds_extract(ctx, instr);

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
   if (ctx.info[instr->definitions[0].tempId()].is_neg() &&
       ctx.uses[instr->operands[1].tempId()] == 1) {
      Temp val = ctx.info[instr->definitions[0].tempId()].temp;

      if (!ctx.info[val.id()].is_mul())
         return;

      Instruction* mul_instr = ctx.info[val.id()].instr;

      if (mul_instr->operands[0].isLiteral())
         return;
      if (mul_instr->isVOP3() && mul_instr->vop3().clamp)
         return;
      if (mul_instr->isSDWA() || mul_instr->isDPP())
         return;

      /* convert to mul(neg(a), b) */
      ctx.uses[mul_instr->definitions[0].tempId()]--;
      Definition def = instr->definitions[0];
      /* neg(abs(mul(a, b))) -> mul(neg(abs(a)), abs(b)) */
      bool is_abs = ctx.info[instr->definitions[0].tempId()].is_abs();
      instr.reset(
         create_instruction<VOP3_instruction>(mul_instr->opcode, asVOP3(Format::VOP2), 2, 1));
      instr->operands[0] = mul_instr->operands[0];
      instr->operands[1] = mul_instr->operands[1];
      instr->definitions[0] = def;
      VOP3_instruction& new_mul = instr->vop3();
      if (mul_instr->isVOP3()) {
         VOP3_instruction& mul = mul_instr->vop3();
         new_mul.neg[0] = mul.neg[0] && !is_abs;
         new_mul.neg[1] = mul.neg[1] && !is_abs;
         new_mul.abs[0] = mul.abs[0] || is_abs;
         new_mul.abs[1] = mul.abs[1] || is_abs;
         new_mul.omod = mul.omod;
      }
      new_mul.neg[0] ^= true;
      new_mul.clamp = false;

      ctx.info[instr->definitions[0].tempId()].set_mul(instr.get());
      return;
   }

   /* combine mul+add -> mad */
   bool mad32 = instr->opcode == aco_opcode::v_add_f32 || instr->opcode == aco_opcode::v_sub_f32 ||
                instr->opcode == aco_opcode::v_subrev_f32;
   bool mad16 = instr->opcode == aco_opcode::v_add_f16 || instr->opcode == aco_opcode::v_sub_f16 ||
                instr->opcode == aco_opcode::v_subrev_f16;
   bool mad64 = instr->opcode == aco_opcode::v_add_f64;
   if (mad16 || mad32 || mad64) {
      bool need_fma =
         mad32 ? (ctx.fp_mode.denorm32 != 0 || ctx.program->chip_class >= GFX10_3)
               : (ctx.fp_mode.denorm16_64 != 0 || ctx.program->chip_class >= GFX10 || mad64);
      if (need_fma && instr->definitions[0].isPrecise())
         return;
      if (need_fma && mad32 && !ctx.program->dev.has_fast_fma32)
         return;

      Instruction* mul_instr = nullptr;
      unsigned add_op_idx = 0;
      uint32_t uses = UINT32_MAX;
      /* find the 'best' mul instruction to combine with the add */
      for (unsigned i = 0; i < 2; i++) {
         if (!instr->operands[i].isTemp() || !ctx.info[instr->operands[i].tempId()].is_mul())
            continue;
         /* check precision requirements */
         ssa_info& info = ctx.info[instr->operands[i].tempId()];
         if (need_fma && info.instr->definitions[0].isPrecise())
            continue;

         /* no clamp/omod allowed between mul and add */
         if (info.instr->isVOP3() && (info.instr->vop3().clamp || info.instr->vop3().omod))
            continue;

         Operand op[3] = {info.instr->operands[0], info.instr->operands[1], instr->operands[1 - i]};
         if (info.instr->isSDWA() || info.instr->isDPP() || !check_vop3_operands(ctx, 3, op) ||
             ctx.uses[instr->operands[i].tempId()] >= uses)
            continue;

         mul_instr = info.instr;
         add_op_idx = 1 - i;
         uses = ctx.uses[instr->operands[i].tempId()];
      }

      if (mul_instr) {
         /* turn mul+add into v_mad/v_fma */
         Operand op[3] = {mul_instr->operands[0], mul_instr->operands[1],
                          instr->operands[add_op_idx]};
         ctx.uses[mul_instr->definitions[0].tempId()]--;
         if (ctx.uses[mul_instr->definitions[0].tempId()]) {
            if (op[0].isTemp())
               ctx.uses[op[0].tempId()]++;
            if (op[1].isTemp())
               ctx.uses[op[1].tempId()]++;
         }

         bool neg[3] = {false, false, false};
         bool abs[3] = {false, false, false};
         unsigned omod = 0;
         bool clamp = false;

         if (mul_instr->isVOP3()) {
            VOP3_instruction& vop3 = mul_instr->vop3();
            neg[0] = vop3.neg[0];
            neg[1] = vop3.neg[1];
            abs[0] = vop3.abs[0];
            abs[1] = vop3.abs[1];
         }

         if (instr->isVOP3()) {
            VOP3_instruction& vop3 = instr->vop3();
            neg[2] = vop3.neg[add_op_idx];
            abs[2] = vop3.abs[add_op_idx];
            omod = vop3.omod;
            clamp = vop3.clamp;
            /* abs of the multiplication result */
            if (vop3.abs[1 - add_op_idx]) {
               neg[0] = false;
               neg[1] = false;
               abs[0] = true;
               abs[1] = true;
            }
            /* neg of the multiplication result */
            neg[1] = neg[1] ^ vop3.neg[1 - add_op_idx];
         }
         if (instr->opcode == aco_opcode::v_sub_f32 || instr->opcode == aco_opcode::v_sub_f16)
            neg[1 + add_op_idx] = neg[1 + add_op_idx] ^ true;
         else if (instr->opcode == aco_opcode::v_subrev_f32 ||
                  instr->opcode == aco_opcode::v_subrev_f16)
            neg[2 - add_op_idx] = neg[2 - add_op_idx] ^ true;

         aco_opcode mad_op = need_fma ? aco_opcode::v_fma_f32 : aco_opcode::v_mad_f32;
         if (mad16)
            mad_op = need_fma ? (ctx.program->chip_class == GFX8 ? aco_opcode::v_fma_legacy_f16
                                                                 : aco_opcode::v_fma_f16)
                              : (ctx.program->chip_class == GFX8 ? aco_opcode::v_mad_legacy_f16
                                                                 : aco_opcode::v_mad_f16);
         if (mad64)
            mad_op = aco_opcode::v_fma_f64;

         aco_ptr<VOP3_instruction> mad{
            create_instruction<VOP3_instruction>(mad_op, Format::VOP3, 3, 1)};
         for (unsigned i = 0; i < 3; i++) {
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
         instr = std::move(mad);
         return;
      }
   }
   /* v_mul_f32(v_cndmask_b32(0, 1.0, cond), a) -> v_cndmask_b32(0, a, cond) */
   else if (instr->opcode == aco_opcode::v_mul_f32 && !instr->isVOP3()) {
      for (unsigned i = 0; i < 2; i++) {
         if (instr->operands[i].isTemp() && ctx.info[instr->operands[i].tempId()].is_b2f() &&
             ctx.uses[instr->operands[i].tempId()] == 1 && instr->operands[!i].isTemp() &&
             instr->operands[!i].getTemp().type() == RegType::vgpr) {
            ctx.uses[instr->operands[i].tempId()]--;
            ctx.uses[ctx.info[instr->operands[i].tempId()].temp.id()]++;

            aco_ptr<VOP2_instruction> new_instr{
               create_instruction<VOP2_instruction>(aco_opcode::v_cndmask_b32, Format::VOP2, 3, 1)};
            new_instr->operands[0] = Operand::zero();
            new_instr->operands[1] = instr->operands[!i];
            new_instr->operands[2] = Operand(ctx.info[instr->operands[i].tempId()].temp);
            new_instr->definitions[0] = instr->definitions[0];
            instr = std::move(new_instr);
            ctx.info[instr->definitions[0].tempId()].label = 0;
            return;
         }
      }
   } else if (instr->opcode == aco_opcode::v_or_b32 && ctx.program->chip_class >= GFX9) {
      if (combine_three_valu_op(ctx, instr, aco_opcode::s_or_b32, aco_opcode::v_or3_b32, "012",
                                1 | 2)) {
      } else if (combine_three_valu_op(ctx, instr, aco_opcode::v_or_b32, aco_opcode::v_or3_b32,
                                       "012", 1 | 2)) {
      } else if (combine_add_or_then_and_lshl(ctx, instr)) {
      }
   } else if (instr->opcode == aco_opcode::v_xor_b32 && ctx.program->chip_class >= GFX10) {
      if (combine_three_valu_op(ctx, instr, aco_opcode::v_xor_b32, aco_opcode::v_xor3_b32, "012",
                                1 | 2)) {
      } else if (combine_three_valu_op(ctx, instr, aco_opcode::s_xor_b32, aco_opcode::v_xor3_b32,
                                       "012", 1 | 2)) {
      }
   } else if (instr->opcode == aco_opcode::v_add_u16) {
      combine_three_valu_op(
         ctx, instr, aco_opcode::v_mul_lo_u16,
         ctx.program->chip_class == GFX8 ? aco_opcode::v_mad_legacy_u16 : aco_opcode::v_mad_u16,
         "120", 1 | 2);
   } else if (instr->opcode == aco_opcode::v_add_u16_e64) {
      combine_three_valu_op(ctx, instr, aco_opcode::v_mul_lo_u16_e64, aco_opcode::v_mad_u16, "120",
                            1 | 2);
   } else if (instr->opcode == aco_opcode::v_add_u32) {
      if (combine_add_sub_b2i(ctx, instr, aco_opcode::v_addc_co_u32, 1 | 2)) {
      } else if (combine_add_bcnt(ctx, instr)) {
      } else if (combine_three_valu_op(ctx, instr, aco_opcode::v_mul_u32_u24,
                                       aco_opcode::v_mad_u32_u24, "120", 1 | 2)) {
      } else if (ctx.program->chip_class >= GFX9 && !instr->usesModifiers()) {
         if (combine_three_valu_op(ctx, instr, aco_opcode::s_xor_b32, aco_opcode::v_xad_u32, "120",
                                   1 | 2)) {
         } else if (combine_three_valu_op(ctx, instr, aco_opcode::v_xor_b32, aco_opcode::v_xad_u32,
                                          "120", 1 | 2)) {
         } else if (combine_three_valu_op(ctx, instr, aco_opcode::s_add_i32, aco_opcode::v_add3_u32,
                                          "012", 1 | 2)) {
         } else if (combine_three_valu_op(ctx, instr, aco_opcode::s_add_u32, aco_opcode::v_add3_u32,
                                          "012", 1 | 2)) {
         } else if (combine_three_valu_op(ctx, instr, aco_opcode::v_add_u32, aco_opcode::v_add3_u32,
                                          "012", 1 | 2)) {
         } else if (combine_add_or_then_and_lshl(ctx, instr)) {
         }
      }
   } else if (instr->opcode == aco_opcode::v_add_co_u32 ||
              instr->opcode == aco_opcode::v_add_co_u32_e64) {
      bool carry_out = ctx.uses[instr->definitions[1].tempId()] > 0;
      if (combine_add_sub_b2i(ctx, instr, aco_opcode::v_addc_co_u32, 1 | 2)) {
      } else if (!carry_out && combine_add_bcnt(ctx, instr)) {
      } else if (!carry_out && combine_three_valu_op(ctx, instr, aco_opcode::v_mul_u32_u24,
                                                     aco_opcode::v_mad_u32_u24, "120", 1 | 2)) {
      } else if (!carry_out && combine_add_lshl(ctx, instr, false)) {
      }
   } else if (instr->opcode == aco_opcode::v_sub_u32 || instr->opcode == aco_opcode::v_sub_co_u32 ||
              instr->opcode == aco_opcode::v_sub_co_u32_e64) {
      bool carry_out =
         instr->opcode != aco_opcode::v_sub_u32 && ctx.uses[instr->definitions[1].tempId()] > 0;
      if (combine_add_sub_b2i(ctx, instr, aco_opcode::v_subbrev_co_u32, 2)) {
      } else if (!carry_out && combine_add_lshl(ctx, instr, true)) {
      }
   } else if (instr->opcode == aco_opcode::v_subrev_u32 ||
              instr->opcode == aco_opcode::v_subrev_co_u32 ||
              instr->opcode == aco_opcode::v_subrev_co_u32_e64) {
      combine_add_sub_b2i(ctx, instr, aco_opcode::v_subbrev_co_u32, 1);
   } else if (instr->opcode == aco_opcode::v_lshlrev_b32 && ctx.program->chip_class >= GFX9) {
      combine_three_valu_op(ctx, instr, aco_opcode::v_add_u32, aco_opcode::v_add_lshl_u32, "120",
                            2);
   } else if ((instr->opcode == aco_opcode::s_add_u32 || instr->opcode == aco_opcode::s_add_i32) &&
              ctx.program->chip_class >= GFX9) {
      combine_salu_lshl_add(ctx, instr);
   } else if (instr->opcode == aco_opcode::s_not_b32 || instr->opcode == aco_opcode::s_not_b64) {
      combine_salu_not_bitwise(ctx, instr);
   } else if (instr->opcode == aco_opcode::s_and_b32 || instr->opcode == aco_opcode::s_or_b32 ||
              instr->opcode == aco_opcode::s_and_b64 || instr->opcode == aco_opcode::s_or_b64) {
      if (combine_ordering_test(ctx, instr)) {
      } else if (combine_comparison_ordering(ctx, instr)) {
      } else if (combine_constant_comparison_ordering(ctx, instr)) {
      } else if (combine_salu_n2(ctx, instr)) {
      }
   } else if (instr->opcode == aco_opcode::v_and_b32) {
      combine_and_subbrev(ctx, instr);
   } else {
      aco_opcode min, max, min3, max3, med3;
      bool some_gfx9_only;
      if (get_minmax_info(instr->opcode, &min, &max, &min3, &max3, &med3, &some_gfx9_only) &&
          (!some_gfx9_only || ctx.program->chip_class >= GFX9)) {
         if (combine_minmax(ctx, instr, instr->opcode == min ? max : min,
                            instr->opcode == min ? min3 : max3)) {
         } else {
            combine_clamp(ctx, instr, min, max, med3);
         }
      }
   }

   /* do this after combine_salu_n2() */
   if (instr->opcode == aco_opcode::s_andn2_b32 || instr->opcode == aco_opcode::s_andn2_b64)
      combine_inverse_comparison(ctx, instr);
}

bool
to_uniform_bool_instr(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   /* Check every operand to make sure they are suitable. */
   for (Operand& op : instr->operands) {
      if (!op.isTemp())
         return false;
      if (!ctx.info[op.tempId()].is_uniform_bool() && !ctx.info[op.tempId()].is_uniform_bitwise())
         return false;
   }

   switch (instr->opcode) {
   case aco_opcode::s_and_b32:
   case aco_opcode::s_and_b64: instr->opcode = aco_opcode::s_and_b32; break;
   case aco_opcode::s_or_b32:
   case aco_opcode::s_or_b64: instr->opcode = aco_opcode::s_or_b32; break;
   case aco_opcode::s_xor_b32:
   case aco_opcode::s_xor_b64: instr->opcode = aco_opcode::s_absdiff_i32; break;
   default:
      /* Don't transform other instructions. They are very unlikely to appear here. */
      return false;
   }

   for (Operand& op : instr->operands) {
      ctx.uses[op.tempId()]--;

      if (ctx.info[op.tempId()].is_uniform_bool()) {
         /* Just use the uniform boolean temp. */
         op.setTemp(ctx.info[op.tempId()].temp);
      } else if (ctx.info[op.tempId()].is_uniform_bitwise()) {
         /* Use the SCC definition of the predecessor instruction.
          * This allows the predecessor to get picked up by the same optimization (if it has no
          * divergent users), and it also makes sure that the current instruction will keep working
          * even if the predecessor won't be transformed.
          */
         Instruction* pred_instr = ctx.info[op.tempId()].instr;
         assert(pred_instr->definitions.size() >= 2);
         assert(pred_instr->definitions[1].isFixed() &&
                pred_instr->definitions[1].physReg() == scc);
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

void
select_instruction(opt_ctx& ctx, aco_ptr<Instruction>& instr)
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
      for (unsigned i = 0, offset = 0; i < instr->definitions.size();
           offset += instr->definitions[i++].bytes()) {
         if (ctx.uses[instr->definitions[i].tempId()]) {
            num_used++;
            idx = i;
            split_offset = offset;
         }
      }
      bool done = false;
      if (num_used == 1 && ctx.info[instr->operands[0].tempId()].is_vec() &&
          ctx.uses[instr->operands[0].tempId()] == 1) {
         Instruction* vec = ctx.info[instr->operands[0].tempId()].instr;

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

            aco_ptr<Pseudo_instruction> extract{create_instruction<Pseudo_instruction>(
               aco_opcode::p_create_vector, Format::PSEUDO, 1, 1)};
            extract->operands[0] = op;
            extract->definitions[0] = instr->definitions[idx];
            instr = std::move(extract);

            done = true;
         }
      }

      if (!done && num_used == 1 &&
          instr->operands[0].bytes() % instr->definitions[idx].bytes() == 0 &&
          split_offset % instr->definitions[idx].bytes() == 0) {
         aco_ptr<Pseudo_instruction> extract{create_instruction<Pseudo_instruction>(
            aco_opcode::p_extract_vector, Format::PSEUDO, 2, 1)};
         extract->operands[0] = instr->operands[0];
         extract->operands[1] =
            Operand::c32((uint32_t)split_offset / instr->definitions[idx].bytes());
         extract->definitions[0] = instr->definitions[idx];
         instr = std::move(extract);
      }
   }

   mad_info* mad_info = NULL;
   if (!instr->definitions.empty() && ctx.info[instr->definitions[0].tempId()].is_mad()) {
      mad_info = &ctx.mad_infos[ctx.info[instr->definitions[0].tempId()].instr->pass_flags];
      /* re-check mad instructions */
      if (ctx.uses[mad_info->mul_temp_id] && mad_info->add_instr) {
         ctx.uses[mad_info->mul_temp_id]++;
         if (instr->operands[0].isTemp())
            ctx.uses[instr->operands[0].tempId()]--;
         if (instr->operands[1].isTemp())
            ctx.uses[instr->operands[1].tempId()]--;
         instr.swap(mad_info->add_instr);
         mad_info = NULL;
      }
      /* check literals */
      else if (!instr->usesModifiers() && instr->opcode != aco_opcode::v_fma_f64) {
         /* FMA can only take literals on GFX10+ */
         if ((instr->opcode == aco_opcode::v_fma_f32 || instr->opcode == aco_opcode::v_fma_f16) &&
             ctx.program->chip_class < GFX10)
            return;
         /* There are no v_fmaak_legacy_f16/v_fmamk_legacy_f16 and on chips where VOP3 can take
          * literals (GFX10+), these instructions don't exist.
          */
         if (instr->opcode == aco_opcode::v_fma_legacy_f16)
            return;

         bool sgpr_used = false;
         uint32_t literal_idx = 0;
         uint32_t literal_uses = UINT32_MAX;
         for (unsigned i = 0; i < instr->operands.size(); i++) {
            if (instr->operands[i].isConstant() && i > 0) {
               literal_uses = UINT32_MAX;
               break;
            }
            if (!instr->operands[i].isTemp())
               continue;
            unsigned bits = get_operand_size(instr, i);
            /* if one of the operands is sgpr, we cannot add a literal somewhere else on pre-GFX10
             * or operands other than the 1st */
            if (instr->operands[i].getTemp().type() == RegType::sgpr &&
                (i > 0 || ctx.program->chip_class < GFX10)) {
               if (!sgpr_used && ctx.info[instr->operands[i].tempId()].is_literal(bits)) {
                  literal_uses = ctx.uses[instr->operands[i].tempId()];
                  literal_idx = i;
               } else {
                  literal_uses = UINT32_MAX;
               }
               sgpr_used = true;
               /* don't break because we still need to check constants */
            } else if (!sgpr_used && ctx.info[instr->operands[i].tempId()].is_literal(bits) &&
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

   /* Mark SCC needed, so the uniform boolean transformation won't swap the definitions
    * when it isn't beneficial */
   if (instr->isBranch() && instr->operands.size() && instr->operands[0].isTemp() &&
       instr->operands[0].isFixed() && instr->operands[0].physReg() == scc) {
      ctx.info[instr->operands[0].tempId()].set_scc_needed();
      return;
   } else if ((instr->opcode == aco_opcode::s_cselect_b64 ||
               instr->opcode == aco_opcode::s_cselect_b32) &&
              instr->operands[2].isTemp()) {
      ctx.info[instr->operands[2].tempId()].set_scc_needed();
   } else if (instr->opcode == aco_opcode::p_wqm && instr->operands[0].isTemp() &&
              ctx.info[instr->definitions[0].tempId()].is_scc_needed()) {
      /* Propagate label so it is correctly detected by the uniform bool transform */
      ctx.info[instr->operands[0].tempId()].set_scc_needed();

      /* Fix definition to SCC, this will prevent RA from adding superfluous moves */
      instr->definitions[0].setFixed(scc);
   }

   /* check for literals */
   if (!instr->isSALU() && !instr->isVALU())
      return;

   /* Transform uniform bitwise boolean operations to 32-bit when there are no divergent uses. */
   if (instr->definitions.size() && ctx.uses[instr->definitions[0].tempId()] == 0 &&
       ctx.info[instr->definitions[0].tempId()].is_uniform_bitwise()) {
      bool transform_done = to_uniform_bool_instr(ctx, instr);

      if (transform_done && !ctx.info[instr->definitions[1].tempId()].is_scc_needed()) {
         /* Swap the two definition IDs in order to avoid overusing the SCC.
          * This reduces extra moves generated by RA. */
         uint32_t def0_id = instr->definitions[0].getTemp().id();
         uint32_t def1_id = instr->definitions[1].getTemp().id();
         instr->definitions[0].setTemp(Temp(def1_id, s1));
         instr->definitions[1].setTemp(Temp(def0_id, s1));
      }

      return;
   }

   /* Combine DPP copies into VALU. This should be done after creating MAD/FMA. */
   if (instr->isVALU()) {
      for (unsigned i = 0; i < instr->operands.size(); i++) {
         if (!instr->operands[i].isTemp())
            continue;
         ssa_info info = ctx.info[instr->operands[i].tempId()];

         aco_opcode swapped_op;
         if (info.is_dpp() && info.instr->pass_flags == instr->pass_flags &&
             (i == 0 || can_swap_operands(instr, &swapped_op)) && can_use_DPP(instr, true) &&
             !instr->isDPP()) {
            convert_to_DPP(instr);
            DPP_instruction* dpp = static_cast<DPP_instruction*>(instr.get());
            if (i) {
               instr->opcode = swapped_op;
               std::swap(instr->operands[0], instr->operands[1]);
               std::swap(dpp->neg[0], dpp->neg[1]);
               std::swap(dpp->abs[0], dpp->abs[1]);
            }
            if (--ctx.uses[info.instr->definitions[0].tempId()])
               ctx.uses[info.instr->operands[0].tempId()]++;
            instr->operands[0].setTemp(info.instr->operands[0].getTemp());
            dpp->dpp_ctrl = info.instr->dpp().dpp_ctrl;
            dpp->bound_ctrl = info.instr->dpp().bound_ctrl;
            dpp->neg[0] ^= info.instr->dpp().neg[0] && !dpp->abs[0];
            dpp->abs[0] |= info.instr->dpp().abs[0];
            break;
         }
      }
   }

   if (instr->isSDWA() || (instr->isVOP3() && ctx.program->chip_class < GFX10) ||
       (instr->isVOP3P() && ctx.program->chip_class < GFX10))
      return; /* some encodings can't ever take literals */

   /* we do not apply the literals yet as we don't know if it is profitable */
   Operand current_literal(s1);

   unsigned literal_id = 0;
   unsigned literal_uses = UINT32_MAX;
   Operand literal(s1);
   unsigned num_operands = 1;
   if (instr->isSALU() ||
       (ctx.program->chip_class >= GFX10 && (can_use_VOP3(ctx, instr) || instr->isVOP3P())))
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
         literal = Operand::c32(ctx.info[op.tempId()].val);
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

void
apply_literals(opt_ctx& ctx, aco_ptr<Instruction>& instr)
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

         aco_opcode new_op =
            info->literal_idx == 2 ? aco_opcode::v_madak_f32 : aco_opcode::v_madmk_f32;
         if (instr->opcode == aco_opcode::v_fma_f32)
            new_op = info->literal_idx == 2 ? aco_opcode::v_fmaak_f32 : aco_opcode::v_fmamk_f32;
         else if (instr->opcode == aco_opcode::v_mad_f16 ||
                  instr->opcode == aco_opcode::v_mad_legacy_f16)
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
         new_mad->operands[2] =
            Operand::c32(ctx.info[instr->operands[info->literal_idx].tempId()].val);
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
            Operand literal = Operand::c32(ctx.info[op.tempId()].val);
            instr->format = withoutDPP(instr->format);
            if (instr->isVALU() && i > 0 && instr->format != Format::VOP3P)
               to_VOP3(ctx, instr);
            instr->operands[i] = literal;
         }
      }
   }

   ctx.instructions.emplace_back(std::move(instr));
}

void
optimize(Program* program)
{
   opt_ctx ctx;
   ctx.program = program;
   std::vector<ssa_info> info(program->peekAllocationId());
   ctx.info = info.data();

   /* 1. Bottom-Up DAG pass (forward) to label all ssa-defs */
   for (Block& block : program->blocks) {
      ctx.fp_mode = block.fp_mode;
      for (aco_ptr<Instruction>& instr : block.instructions)
         label_instruction(ctx, instr);
   }

   ctx.uses = dead_code_analysis(program);

   /* 2. Combine v_mad, omod, clamp and propagate sgpr on VALU instructions */
   for (Block& block : program->blocks) {
      ctx.fp_mode = block.fp_mode;
      for (aco_ptr<Instruction>& instr : block.instructions)
         combine_instruction(ctx, instr);
   }

   /* 3. Top-Down DAG pass (backward) to select instructions (includes DCE) */
   for (auto block_rit = program->blocks.rbegin(); block_rit != program->blocks.rend();
        ++block_rit) {
      Block* block = &(*block_rit);
      ctx.fp_mode = block->fp_mode;
      for (auto instr_rit = block->instructions.rbegin(); instr_rit != block->instructions.rend();
           ++instr_rit)
         select_instruction(ctx, *instr_rit);
   }

   /* 4. Add literals to instructions */
   for (Block& block : program->blocks) {
      ctx.instructions.clear();
      ctx.fp_mode = block.fp_mode;
      for (aco_ptr<Instruction>& instr : block.instructions)
         apply_literals(ctx, instr);
      block.instructions.swap(ctx.instructions);
   }
}

} // namespace aco
