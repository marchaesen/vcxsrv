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
 *    Bas Nieuwenhuizen (bas@basnieuwenhuizen.nl)
 *
 */

#include <algorithm>
#include <array>
#include <map>
#include <unordered_map>

#include "aco_ir.h"
#include "sid.h"
#include "util/u_math.h"

namespace aco {
namespace {

struct ra_ctx;

unsigned get_subdword_operand_stride(chip_class chip, const aco_ptr<Instruction>& instr, unsigned idx, RegClass rc);
void add_subdword_operand(ra_ctx& ctx, aco_ptr<Instruction>& instr, unsigned idx, unsigned byte, RegClass rc);
std::pair<unsigned, unsigned> get_subdword_definition_info(Program *program, const aco_ptr<Instruction>& instr, RegClass rc);
void add_subdword_definition(Program *program, aco_ptr<Instruction>& instr, unsigned idx, PhysReg reg, bool is_partial);

struct assignment {
   PhysReg reg;
   RegClass rc;
   uint8_t assigned = 0;
   assignment() = default;
   assignment(PhysReg reg_, RegClass rc_) : reg(reg_), rc(rc_), assigned(-1) {}
};

struct phi_info {
   Instruction* phi;
   unsigned block_idx;
   std::set<Instruction*> uses;
};

struct ra_ctx {
   std::bitset<512> war_hint;
   Program* program;
   std::vector<assignment> assignments;
   std::vector<std::unordered_map<unsigned, Temp>> renames;
   std::vector<std::vector<Instruction*>> incomplete_phis;
   std::vector<bool> filled;
   std::vector<bool> sealed;
   std::unordered_map<unsigned, Temp> orig_names;
   std::unordered_map<unsigned, phi_info> phi_map;
   std::unordered_map<unsigned, unsigned> affinities;
   std::unordered_map<unsigned, Instruction*> vectors;
   std::unordered_map<unsigned, Instruction*> split_vectors;
   aco_ptr<Instruction> pseudo_dummy;
   unsigned max_used_sgpr = 0;
   unsigned max_used_vgpr = 0;
   std::bitset<64> defs_done; /* see MAX_ARGS in aco_instruction_selection_setup.cpp */

   ra_test_policy policy;

   ra_ctx(Program* program_, ra_test_policy policy_)
      : program(program_),
        assignments(program->peekAllocationId()),
        renames(program->blocks.size()),
        incomplete_phis(program->blocks.size()),
        filled(program->blocks.size()),
        sealed(program->blocks.size()),
        policy(policy_)
   {
      pseudo_dummy.reset(create_instruction<Instruction>(aco_opcode::p_parallelcopy, Format::PSEUDO, 0, 0));
   }
};

struct DefInfo {
   uint16_t lb;
   uint16_t ub;
   uint8_t size;
   uint8_t stride;
   RegClass rc;

   DefInfo(ra_ctx& ctx, aco_ptr<Instruction>& instr, RegClass rc_, int operand) : rc(rc_) {
      size = rc.size();
      stride = 1;

      if (rc.type() == RegType::vgpr) {
         lb = 256;
         ub = 256 + ctx.program->max_reg_demand.vgpr;
      } else {
         lb = 0;
         ub = ctx.program->max_reg_demand.sgpr;
         if (size == 2)
            stride = 2;
         else if (size >= 4)
            stride = 4;
      }

      if (rc.is_subdword() && operand >= 0) {
         /* stride in bytes */
         stride = get_subdword_operand_stride(ctx.program->chip_class, instr, operand, rc);
      } else if (rc.is_subdword()) {
         std::pair<unsigned, unsigned> info = get_subdword_definition_info(ctx.program, instr, rc);
         stride = info.first;
         if (info.second > rc.bytes()) {
            rc = RegClass::get(rc.type(), info.second);
            size = rc.size();
            /* we might still be able to put the definition in the high half,
             * but that's only useful for affinities and this information isn't
             * used for them */
            stride = align(stride, info.second);
            if (!rc.is_subdword())
               stride = DIV_ROUND_UP(stride, 4);
         }
         assert(stride > 0);
      }
   }
};

class RegisterFile {
public:
   RegisterFile() {regs.fill(0);}

   std::array<uint32_t, 512> regs;
   std::map<uint32_t, std::array<uint32_t, 4>> subdword_regs;

   const uint32_t& operator [] (unsigned index) const {
      return regs[index];
   }

   uint32_t& operator [] (unsigned index) {
      return regs[index];
   }

   unsigned count_zero(PhysReg start, unsigned size) {
      unsigned res = 0;
      for (unsigned i = 0; i < size; i++)
         res += !regs[start + i];
      return res;
   }

   /* Returns true if any of the bytes in the given range are allocated or blocked */
   bool test(PhysReg start, unsigned num_bytes) {
      for (PhysReg i = start; i.reg_b < start.reg_b + num_bytes; i = PhysReg(i + 1)) {
         if (regs[i] & 0x0FFFFFFF)
            return true;
         if (regs[i] == 0xF0000000) {
            assert(subdword_regs.find(i) != subdword_regs.end());
            for (unsigned j = i.byte(); i * 4 + j < start.reg_b + num_bytes && j < 4; j++) {
               if (subdword_regs[i][j])
                  return true;
            }
         }
      }
      return false;
   }

   void block(PhysReg start, RegClass rc) {
      if (rc.is_subdword())
         fill_subdword(start, rc.bytes(), 0xFFFFFFFF);
      else
         fill(start, rc.size(), 0xFFFFFFFF);
   }

   bool is_blocked(PhysReg start) {
      if (regs[start] == 0xFFFFFFFF)
         return true;
      if (regs[start] == 0xF0000000) {
         for (unsigned i = start.byte(); i < 4; i++)
            if (subdword_regs[start][i] == 0xFFFFFFFF)
               return true;
      }
      return false;
   }

   bool is_empty_or_blocked(PhysReg start) {
      /* Empty is 0, blocked is 0xFFFFFFFF, so to check both we compare the
       * incremented value to 1 */
      if (regs[start] == 0xF0000000) {
         return subdword_regs[start][start.byte()] + 1 <= 1;
      }
      return regs[start] + 1 <= 1;
   }

   void clear(PhysReg start, RegClass rc) {
      if (rc.is_subdword())
         fill_subdword(start, rc.bytes(), 0);
      else
         fill(start, rc.size(), 0);
   }

   void fill(Operand op) {
      if (op.regClass().is_subdword())
         fill_subdword(op.physReg(), op.bytes(), op.tempId());
      else
         fill(op.physReg(), op.size(), op.tempId());
   }

   void clear(Operand op) {
      clear(op.physReg(), op.regClass());
   }

   void fill(Definition def) {
      if (def.regClass().is_subdword())
         fill_subdword(def.physReg(), def.bytes(), def.tempId());
      else
         fill(def.physReg(), def.size(), def.tempId());
   }

   void clear(Definition def) {
      clear(def.physReg(), def.regClass());
   }

   unsigned get_id(PhysReg reg) {
      return regs[reg] == 0xF0000000 ? subdword_regs[reg][reg.byte()] : regs[reg];
   }

private:
   void fill(PhysReg start, unsigned size, uint32_t val) {
      for (unsigned i = 0; i < size; i++)
         regs[start + i] = val;
   }

   void fill_subdword(PhysReg start, unsigned num_bytes, uint32_t val) {
      fill(start, DIV_ROUND_UP(num_bytes, 4), 0xF0000000);
      for (PhysReg i = start; i.reg_b < start.reg_b + num_bytes; i = PhysReg(i + 1)) {
         /* emplace or get */
         std::array<uint32_t, 4>& sub = subdword_regs.emplace(i, std::array<uint32_t, 4>{0, 0, 0, 0}).first->second;
         for (unsigned j = i.byte(); i * 4 + j < start.reg_b + num_bytes && j < 4; j++)
            sub[j] = val;

         if (sub == std::array<uint32_t, 4>{0, 0, 0, 0}) {
            subdword_regs.erase(i);
            regs[i] = 0;
         }
      }
   }
};


/* helper function for debugging */
#if 0
void print_regs(ra_ctx& ctx, bool vgprs, RegisterFile& reg_file)
{
   unsigned max = vgprs ? ctx.program->max_reg_demand.vgpr : ctx.program->max_reg_demand.sgpr;
   unsigned lb = vgprs ? 256 : 0;
   unsigned ub = lb + max;
   char reg_char = vgprs ? 'v' : 's';

   /* print markers */
   printf("       ");
   for (unsigned i = lb; i < ub; i += 3) {
      printf("%.2u ", i - lb);
   }
   printf("\n");

   /* print usage */
   printf("%cgprs: ", reg_char);
   unsigned free_regs = 0;
   unsigned prev = 0;
   bool char_select = false;
   for (unsigned i = lb; i < ub; i++) {
      if (reg_file[i] == 0xFFFF) {
         printf("~");
      } else if (reg_file[i]) {
         if (reg_file[i] != prev) {
            prev = reg_file[i];
            char_select = !char_select;
         }
         printf(char_select ? "#" : "@");
      } else {
         free_regs++;
         printf(".");
      }
   }
   printf("\n");

   printf("%u/%u used, %u/%u free\n", max - free_regs, max, free_regs, max);

   /* print assignments */
   prev = 0;
   unsigned size = 0;
   for (unsigned i = lb; i < ub; i++) {
      if (reg_file[i] != prev) {
         if (prev && size > 1)
            printf("-%d]\n", i - 1 - lb);
         else if (prev)
            printf("]\n");
         prev = reg_file[i];
         if (prev && prev != 0xFFFF) {
            if (ctx.orig_names.count(reg_file[i]) && ctx.orig_names[reg_file[i]].id() != reg_file[i])
               printf("%%%u (was %%%d) = %c[%d", reg_file[i], ctx.orig_names[reg_file[i]].id(), reg_char, i - lb);
            else
               printf("%%%u = %c[%d", reg_file[i], reg_char, i - lb);
         }
         size = 1;
      } else {
         size++;
      }
   }
   if (prev && size > 1)
      printf("-%d]\n", ub - lb - 1);
   else if (prev)
      printf("]\n");
}
#endif


unsigned get_subdword_operand_stride(chip_class chip, const aco_ptr<Instruction>& instr, unsigned idx, RegClass rc)
{
   /* v_readfirstlane_b32 cannot use SDWA */
   if (instr->opcode == aco_opcode::p_as_uniform)
      return 4;
   if (instr->format == Format::PSEUDO && chip >= GFX8)
      return rc.bytes() % 2 == 0 ? 2 : 1;

   if (instr->opcode == aco_opcode::v_cvt_f32_ubyte0) {
      return 1;
   } else if (can_use_SDWA(chip, instr)) {
      return rc.bytes() % 2 == 0 ? 2 : 1;
   } else if (rc.bytes() == 2 && can_use_opsel(chip, instr->opcode, idx, 1)) {
      return 2;
   }

   switch (instr->opcode) {
   case aco_opcode::ds_write_b8:
   case aco_opcode::ds_write_b16:
      return chip >= GFX8 ? 2 : 4;
   case aco_opcode::buffer_store_byte:
   case aco_opcode::buffer_store_short:
   case aco_opcode::flat_store_byte:
   case aco_opcode::flat_store_short:
   case aco_opcode::scratch_store_byte:
   case aco_opcode::scratch_store_short:
   case aco_opcode::global_store_byte:
   case aco_opcode::global_store_short:
      return chip >= GFX9 ? 2 : 4;
   default:
      break;
   }

   return 4;
}

void update_phi_map(ra_ctx& ctx, Instruction *old, Instruction *instr)
{
   for (Operand& op : instr->operands) {
      if (!op.isTemp())
         continue;
      std::unordered_map<unsigned, phi_info>::iterator phi = ctx.phi_map.find(op.tempId());
      if (phi != ctx.phi_map.end()) {
         phi->second.uses.erase(old);
         phi->second.uses.emplace(instr);
      }
   }
}

void add_subdword_operand(ra_ctx& ctx, aco_ptr<Instruction>& instr, unsigned idx, unsigned byte, RegClass rc)
{
   chip_class chip = ctx.program->chip_class;
   if (instr->format == Format::PSEUDO || byte == 0)
      return;

   assert(rc.bytes() <= 2);

   if (!instr->usesModifiers() && instr->opcode == aco_opcode::v_cvt_f32_ubyte0) {
      switch (byte) {
      case 0:
         instr->opcode = aco_opcode::v_cvt_f32_ubyte0;
         break;
      case 1:
         instr->opcode = aco_opcode::v_cvt_f32_ubyte1;
         break;
      case 2:
         instr->opcode = aco_opcode::v_cvt_f32_ubyte2;
         break;
      case 3:
         instr->opcode = aco_opcode::v_cvt_f32_ubyte3;
         break;
      }
      return;
   } else if (can_use_SDWA(chip, instr)) {
      aco_ptr<Instruction> tmp = convert_to_SDWA(chip, instr);
      if (tmp)
         update_phi_map(ctx, tmp.get(), instr.get());
      return;
   } else if (rc.bytes() == 2 && can_use_opsel(chip, instr->opcode, idx, byte / 2)) {
      VOP3A_instruction *vop3 = static_cast<VOP3A_instruction *>(instr.get());
      vop3->opsel |= (byte / 2) << idx;
      return;
   }

   if (chip >= GFX8 && instr->opcode == aco_opcode::ds_write_b8 && byte == 2) {
      instr->opcode = aco_opcode::ds_write_b8_d16_hi;
      return;
   }
   if (chip >= GFX8 && instr->opcode == aco_opcode::ds_write_b16 && byte == 2) {
      instr->opcode = aco_opcode::ds_write_b16_d16_hi;
      return;
   }

   if (chip >= GFX9 && byte == 2) {
      if (instr->opcode == aco_opcode::buffer_store_byte)
         instr->opcode = aco_opcode::buffer_store_byte_d16_hi;
      else if (instr->opcode == aco_opcode::buffer_store_short)
         instr->opcode = aco_opcode::buffer_store_short_d16_hi;
      else if (instr->opcode == aco_opcode::flat_store_byte)
         instr->opcode = aco_opcode::flat_store_byte_d16_hi;
      else if (instr->opcode == aco_opcode::flat_store_short)
         instr->opcode = aco_opcode::flat_store_short_d16_hi;
      else if (instr->opcode == aco_opcode::scratch_store_byte)
         instr->opcode = aco_opcode::scratch_store_byte_d16_hi;
      else if (instr->opcode == aco_opcode::scratch_store_short)
         instr->opcode = aco_opcode::scratch_store_short_d16_hi;
      else if (instr->opcode == aco_opcode::global_store_byte)
         instr->opcode = aco_opcode::global_store_byte_d16_hi;
      else if (instr->opcode == aco_opcode::global_store_short)
         instr->opcode = aco_opcode::global_store_short_d16_hi;
      else
         unreachable("Something went wrong: Impossible register assignment.");
   }
}

/* minimum_stride, bytes_written */
std::pair<unsigned, unsigned> get_subdword_definition_info(Program *program, const aco_ptr<Instruction>& instr, RegClass rc)
{
   chip_class chip = program->chip_class;

   if (instr->format == Format::PSEUDO && chip >= GFX8)
      return std::make_pair(rc.bytes() % 2 == 0 ? 2 : 1, rc.bytes());
   else if (instr->format == Format::PSEUDO)
      return std::make_pair(4, rc.size() * 4u);

   unsigned bytes_written = chip >= GFX10 ? rc.bytes() : 4u;
   switch (instr->opcode) {
   case aco_opcode::v_mad_f16:
   case aco_opcode::v_mad_u16:
   case aco_opcode::v_mad_i16:
   case aco_opcode::v_fma_f16:
   case aco_opcode::v_div_fixup_f16:
   case aco_opcode::v_interp_p2_f16:
      bytes_written = chip >= GFX9 ? rc.bytes() : 4u;
      break;
   default:
      break;
   }
   bytes_written = bytes_written > 4 ? align(bytes_written, 4) : bytes_written;
   bytes_written = MAX2(bytes_written, instr_info.definition_size[(int)instr->opcode] / 8u);

   if (can_use_SDWA(chip, instr)) {
      return std::make_pair(rc.bytes(), rc.bytes());
   } else if (rc.bytes() == 2 && can_use_opsel(chip, instr->opcode, -1, 1)) {
      return std::make_pair(2u, bytes_written);
   }

   switch (instr->opcode) {
   case aco_opcode::buffer_load_ubyte_d16:
   case aco_opcode::buffer_load_short_d16:
   case aco_opcode::flat_load_ubyte_d16:
   case aco_opcode::flat_load_short_d16:
   case aco_opcode::scratch_load_ubyte_d16:
   case aco_opcode::scratch_load_short_d16:
   case aco_opcode::global_load_ubyte_d16:
   case aco_opcode::global_load_short_d16:
   case aco_opcode::ds_read_u8_d16:
   case aco_opcode::ds_read_u16_d16:
      if (chip >= GFX9 && !program->sram_ecc_enabled)
         return std::make_pair(2u, 2u);
      else
         return std::make_pair(2u, 4u);
   default:
      break;
   }

   return std::make_pair(4u, bytes_written);
}

void add_subdword_definition(Program *program, aco_ptr<Instruction>& instr, unsigned idx, PhysReg reg, bool is_partial)
{
   RegClass rc = instr->definitions[idx].regClass();
   chip_class chip = program->chip_class;

   instr->definitions[idx].setFixed(reg);

   if (instr->format == Format::PSEUDO) {
      return;
   } else if (can_use_SDWA(chip, instr)) {
      if (reg.byte() || (is_partial && chip < GFX10))
         convert_to_SDWA(chip, instr);
      return;
   } else if (reg.byte() && rc.bytes() == 2 && can_use_opsel(chip, instr->opcode, -1, reg.byte() / 2)) {
      VOP3A_instruction *vop3 = static_cast<VOP3A_instruction *>(instr.get());
      if (reg.byte() == 2)
         vop3->opsel |= (1 << 3); /* dst in high half */
      return;
   }

   if (reg.byte() == 2) {
      if (instr->opcode == aco_opcode::buffer_load_ubyte_d16)
         instr->opcode = aco_opcode::buffer_load_ubyte_d16_hi;
      else if (instr->opcode == aco_opcode::buffer_load_short_d16)
         instr->opcode = aco_opcode::buffer_load_short_d16_hi;
      else if (instr->opcode == aco_opcode::flat_load_ubyte_d16)
         instr->opcode = aco_opcode::flat_load_ubyte_d16_hi;
      else if (instr->opcode == aco_opcode::flat_load_short_d16)
         instr->opcode = aco_opcode::flat_load_short_d16_hi;
      else if (instr->opcode == aco_opcode::scratch_load_ubyte_d16)
         instr->opcode = aco_opcode::scratch_load_ubyte_d16_hi;
      else if (instr->opcode == aco_opcode::scratch_load_short_d16)
         instr->opcode = aco_opcode::scratch_load_short_d16_hi;
      else if (instr->opcode == aco_opcode::global_load_ubyte_d16)
         instr->opcode = aco_opcode::global_load_ubyte_d16_hi;
      else if (instr->opcode == aco_opcode::global_load_short_d16)
         instr->opcode = aco_opcode::global_load_short_d16_hi;
      else if (instr->opcode == aco_opcode::ds_read_u8_d16)
         instr->opcode = aco_opcode::ds_read_u8_d16_hi;
      else if (instr->opcode == aco_opcode::ds_read_u16_d16)
         instr->opcode = aco_opcode::ds_read_u16_d16_hi;
      else
         unreachable("Something went wrong: Impossible register assignment.");
   }
}

void adjust_max_used_regs(ra_ctx& ctx, RegClass rc, unsigned reg)
{
   unsigned max_addressible_sgpr = ctx.program->sgpr_limit;
   unsigned size = rc.size();
   if (rc.type() == RegType::vgpr) {
      assert(reg >= 256);
      unsigned hi = reg - 256 + size - 1;
      ctx.max_used_vgpr = std::max(ctx.max_used_vgpr, hi);
   } else if (reg + rc.size() <= max_addressible_sgpr) {
      unsigned hi = reg + size - 1;
      ctx.max_used_sgpr = std::max(ctx.max_used_sgpr, std::min(hi, max_addressible_sgpr));
   }
}


void update_renames(ra_ctx& ctx, RegisterFile& reg_file,
                    std::vector<std::pair<Operand, Definition>>& parallelcopies,
                    aco_ptr<Instruction>& instr, bool rename_not_killed_ops)
{
   /* clear operands */
   for (std::pair<Operand, Definition>& copy : parallelcopies) {
      /* the definitions with id are not from this function and already handled */
      if (copy.second.isTemp())
         continue;
      reg_file.clear(copy.first);
   }

   /* allocate id's and rename operands: this is done transparently here */
   for (std::pair<Operand, Definition>& copy : parallelcopies) {
      /* the definitions with id are not from this function and already handled */
      if (copy.second.isTemp())
         continue;

      /* check if we we moved another parallelcopy definition */
      for (std::pair<Operand, Definition>& other : parallelcopies) {
         if (!other.second.isTemp())
            continue;
         if (copy.first.getTemp() == other.second.getTemp()) {
            copy.first.setTemp(other.first.getTemp());
            copy.first.setFixed(other.first.physReg());
         }
      }
      // FIXME: if a definition got moved, change the target location and remove the parallelcopy
      copy.second.setTemp(ctx.program->allocateTmp(copy.second.regClass()));
      ctx.assignments.emplace_back(copy.second.physReg(), copy.second.regClass());
      assert(ctx.assignments.size() == ctx.program->peekAllocationId());

      /* check if we moved an operand */
      bool first = true;
      bool fill = true;
      for (unsigned i = 0; i < instr->operands.size(); i++) {
         Operand& op = instr->operands[i];
         if (!op.isTemp())
            continue;
         if (op.tempId() == copy.first.tempId()) {
            bool omit_renaming = !rename_not_killed_ops && !op.isKillBeforeDef();
            for (std::pair<Operand, Definition>& pc : parallelcopies) {
               PhysReg def_reg = pc.second.physReg();
               omit_renaming &= def_reg > copy.first.physReg() ?
                                (copy.first.physReg() + copy.first.size() <= def_reg.reg()) :
                                (def_reg + pc.second.size() <= copy.first.physReg().reg());
            }
            if (omit_renaming) {
               if (first)
                  op.setFirstKill(true);
               else
                  op.setKill(true);
               first = false;
               continue;
            }
            op.setTemp(copy.second.getTemp());
            op.setFixed(copy.second.physReg());

            fill = !op.isKillBeforeDef();
         }
      }

      if (fill)
         reg_file.fill(copy.second);
   }
}

std::pair<PhysReg, bool> get_reg_simple(ra_ctx& ctx,
                                        RegisterFile& reg_file,
                                        DefInfo info)
{
   uint32_t lb = info.lb;
   uint32_t ub = info.ub;
   uint32_t size = info.size;
   uint32_t stride = info.rc.is_subdword() ? DIV_ROUND_UP(info.stride, 4) : info.stride;
   RegClass rc = info.rc;

   if (stride == 1) {
      info.rc = RegClass(rc.type(), size);
      for (unsigned new_stride = 8; new_stride > 1; new_stride /= 2) {
         if (size % new_stride)
            continue;
         info.stride = new_stride;
         std::pair<PhysReg, bool> res = get_reg_simple(ctx, reg_file, info);
         if (res.second)
            return res;
      }

      /* best fit algorithm: find the smallest gap to fit in the variable */
      unsigned best_pos = 0xFFFF;
      unsigned gap_size = 0xFFFF;
      unsigned last_pos = 0xFFFF;

      for (unsigned current_reg = lb; current_reg < ub; current_reg++) {

         if (reg_file[current_reg] == 0 && !ctx.war_hint[current_reg]) {
            if (last_pos == 0xFFFF)
               last_pos = current_reg;

            /* stop searching after max_used_gpr */
            if (current_reg == ctx.max_used_sgpr + 1 || current_reg == 256 + ctx.max_used_vgpr + 1)
               break;
            else
               continue;
         }

         if (last_pos == 0xFFFF)
            continue;

         /* early return on exact matches */
         if (last_pos + size == current_reg) {
            adjust_max_used_regs(ctx, rc, last_pos);
            return {PhysReg{last_pos}, true};
         }

         /* check if it fits and the gap size is smaller */
         if (last_pos + size < current_reg && current_reg - last_pos < gap_size) {
            best_pos = last_pos;
            gap_size = current_reg - last_pos;
         }
         last_pos = 0xFFFF;
      }

      /* final check */
      if (last_pos + size <= ub && ub - last_pos < gap_size) {
         best_pos = last_pos;
         gap_size = ub - last_pos;
      }

      if (best_pos == 0xFFFF)
         return {{}, false};

      /* find best position within gap by leaving a good stride for other variables*/
      unsigned buffer = gap_size - size;
      if (buffer > 1) {
         if (((best_pos + size) % 8 != 0 && (best_pos + buffer) % 8 == 0) ||
             ((best_pos + size) % 4 != 0 && (best_pos + buffer) % 4 == 0) ||
             ((best_pos + size) % 2 != 0 && (best_pos + buffer) % 2 == 0))
            best_pos = best_pos + buffer;
      }

      adjust_max_used_regs(ctx, rc, best_pos);
      return {PhysReg{best_pos}, true};
   }

   bool found = false;
   unsigned reg_lo = lb;
   unsigned reg_hi = lb + size - 1;
   while (!found && reg_lo + size <= ub) {
      if (reg_file[reg_lo] != 0) {
         reg_lo += stride;
         continue;
      }
      reg_hi = reg_lo + size - 1;
      found = true;
      for (unsigned reg = reg_lo + 1; found && reg <= reg_hi; reg++) {
         if (reg_file[reg] != 0 || ctx.war_hint[reg])
            found = false;
      }
      if (found) {
         adjust_max_used_regs(ctx, rc, reg_lo);
         return {PhysReg{reg_lo}, true};
      }

      reg_lo += stride;
   }

   /* do this late because using the upper bytes of a register can require
    * larger instruction encodings or copies
    * TODO: don't do this in situations where it doesn't benefit */
   if (rc.is_subdword()) {
      for (std::pair<uint32_t, std::array<uint32_t, 4>> entry : reg_file.subdword_regs) {
         assert(reg_file[entry.first] == 0xF0000000);
         if (lb > entry.first || entry.first >= ub)
            continue;

         for (unsigned i = 0; i < 4; i+= info.stride) {
            if (entry.second[i] != 0)
               continue;

            bool reg_found = true;
            for (unsigned j = 1; reg_found && i + j < 4 && j < rc.bytes(); j++)
               reg_found &= entry.second[i + j] == 0;

            /* check neighboring reg if needed */
            reg_found &= ((int)i <= 4 - (int)rc.bytes() || reg_file[entry.first + 1] == 0);
            if (reg_found) {
               PhysReg res{entry.first};
               res.reg_b += i;
               adjust_max_used_regs(ctx, rc, entry.first);
               return {res, true};
            }
         }
      }
   }

   return {{}, false};
}

/* collect variables from a register area and clear reg_file */
std::set<std::pair<unsigned, unsigned>> collect_vars(ra_ctx& ctx, RegisterFile& reg_file,
                                                     PhysReg reg, unsigned size)
{
   std::set<std::pair<unsigned, unsigned>> vars;
   for (unsigned j = reg; j < reg + size; j++) {
      if (reg_file.is_blocked(PhysReg{j}))
         continue;
      if (reg_file[j] == 0xF0000000) {
         for (unsigned k = 0; k < 4; k++) {
            unsigned id = reg_file.subdword_regs[j][k];
            if (id) {
               assignment& var = ctx.assignments[id];
               vars.emplace(var.rc.bytes(), id);
               reg_file.clear(var.reg, var.rc);
               if (!reg_file[j])
                  break;
            }
         }
      } else if (reg_file[j] != 0) {
         unsigned id = reg_file[j];
         assignment& var = ctx.assignments[id];
         vars.emplace(var.rc.bytes(), id);
         reg_file.clear(var.reg, var.rc);
      }
   }
   return vars;
}

bool get_regs_for_copies(ra_ctx& ctx,
                         RegisterFile& reg_file,
                         std::vector<std::pair<Operand, Definition>>& parallelcopies,
                         const std::set<std::pair<unsigned, unsigned>> &vars,
                         uint32_t lb, uint32_t ub,
                         aco_ptr<Instruction>& instr,
                         uint32_t def_reg_lo,
                         uint32_t def_reg_hi)
{

   /* variables are sorted from small sized to large */
   /* NOTE: variables are also sorted by ID. this only affects a very small number of shaders slightly though. */
   for (std::set<std::pair<unsigned, unsigned>>::const_reverse_iterator it = vars.rbegin(); it != vars.rend(); ++it) {
      unsigned id = it->second;
      assignment& var = ctx.assignments[id];
      DefInfo info = DefInfo(ctx, ctx.pseudo_dummy, var.rc, -1);
      uint32_t size = info.size;

      /* check if this is a dead operand, then we can re-use the space from the definition
       * also use the correct stride for sub-dword operands */
      bool is_dead_operand = false;
      for (unsigned i = 0; !is_phi(instr) && i < instr->operands.size(); i++) {
         if (instr->operands[i].isTemp() && instr->operands[i].tempId() == id) {
            if (instr->operands[i].isKillBeforeDef())
               is_dead_operand = true;
            info = DefInfo(ctx, instr, var.rc, i);
            break;
         }
      }

      std::pair<PhysReg, bool> res;
      if (is_dead_operand) {
         if (instr->opcode == aco_opcode::p_create_vector) {
            PhysReg reg(def_reg_lo);
            for (unsigned i = 0; i < instr->operands.size(); i++) {
               if (instr->operands[i].isTemp() && instr->operands[i].tempId() == id) {
                  res = {reg, (!var.rc.is_subdword() || (reg.byte() % info.stride == 0)) && !reg_file.test(reg, var.rc.bytes())};
                  break;
               }
               reg.reg_b += instr->operands[i].bytes();
            }
            if (!res.second)
               res = {var.reg, !reg_file.test(var.reg, var.rc.bytes())};
         } else {
            info.lb = def_reg_lo;
            info.ub = def_reg_hi + 1;
            res = get_reg_simple(ctx, reg_file, info);
         }
      } else {
         info.lb = lb;
         info.ub = MIN2(def_reg_lo, ub);
         res = get_reg_simple(ctx, reg_file, info);
         if (!res.second && def_reg_hi < ub) {
            info.lb = (def_reg_hi + info.stride) & ~(info.stride - 1);
            info.ub = ub;
            res = get_reg_simple(ctx, reg_file, info);
         }
      }

      if (res.second) {
         /* mark the area as blocked */
         reg_file.block(res.first, var.rc);

         /* create parallelcopy pair (without definition id) */
         Temp tmp = Temp(id, var.rc);
         Operand pc_op = Operand(tmp);
         pc_op.setFixed(var.reg);
         Definition pc_def = Definition(res.first, pc_op.regClass());
         parallelcopies.emplace_back(pc_op, pc_def);
         continue;
      }

      unsigned best_pos = lb;
      unsigned num_moves = 0xFF;
      unsigned num_vars = 0;

      /* we use a sliding window to find potential positions */
      unsigned reg_lo = lb;
      unsigned reg_hi = lb + size - 1;
      unsigned stride = var.rc.is_subdword() ? 1 : info.stride;
      for (reg_lo = lb, reg_hi = lb + size - 1; reg_hi < ub; reg_lo += stride, reg_hi += stride) {
         if (!is_dead_operand && ((reg_lo >= def_reg_lo && reg_lo <= def_reg_hi) ||
                                  (reg_hi >= def_reg_lo && reg_hi <= def_reg_hi)))
            continue;

         /* second, check that we have at most k=num_moves elements in the window
          * and no element is larger than the currently processed one */
         unsigned k = 0;
         unsigned n = 0;
         unsigned last_var = 0;
         bool found = true;
         for (unsigned j = reg_lo; found && j <= reg_hi; j++) {
            if (reg_file[j] == 0 || reg_file[j] == last_var)
               continue;

            if (reg_file.is_blocked(PhysReg{j}) || k > num_moves) {
               found = false;
               break;
            }
            if (reg_file[j] == 0xF0000000) {
               k += 1;
               n++;
               continue;
            }
            /* we cannot split live ranges of linear vgprs */
            if (ctx.assignments[reg_file[j]].rc & (1 << 6)) {
               found = false;
               break;
            }
            bool is_kill = false;
            for (const Operand& op : instr->operands) {
               if (op.isTemp() && op.isKillBeforeDef() && op.tempId() == reg_file[j]) {
                  is_kill = true;
                  break;
               }
            }
            if (!is_kill && ctx.assignments[reg_file[j]].rc.size() >= size) {
               found = false;
               break;
            }

            k += ctx.assignments[reg_file[j]].rc.size();
            last_var = reg_file[j];
            n++;
            if (k > num_moves || (k == num_moves && n <= num_vars)) {
               found = false;
               break;
            }
         }

         if (found) {
            best_pos = reg_lo;
            num_moves = k;
            num_vars = n;
         }
      }

      /* FIXME: we messed up and couldn't find space for the variables to be copied */
      if (num_moves == 0xFF)
         return false;

      reg_lo = best_pos;
      reg_hi = best_pos + size - 1;

      /* collect variables and block reg file */
      std::set<std::pair<unsigned, unsigned>> new_vars = collect_vars(ctx, reg_file, PhysReg{reg_lo}, size);

      /* mark the area as blocked */
      reg_file.block(PhysReg{reg_lo}, var.rc);

      if (!get_regs_for_copies(ctx, reg_file, parallelcopies, new_vars, lb, ub, instr, def_reg_lo, def_reg_hi))
         return false;

      adjust_max_used_regs(ctx, var.rc, reg_lo);

      /* create parallelcopy pair (without definition id) */
      Temp tmp = Temp(id, var.rc);
      Operand pc_op = Operand(tmp);
      pc_op.setFixed(var.reg);
      Definition pc_def = Definition(PhysReg{reg_lo}, pc_op.regClass());
      parallelcopies.emplace_back(pc_op, pc_def);
   }

   return true;
}


std::pair<PhysReg, bool> get_reg_impl(ra_ctx& ctx,
                                      RegisterFile& reg_file,
                                      std::vector<std::pair<Operand, Definition>>& parallelcopies,
                                      DefInfo info,
                                      aco_ptr<Instruction>& instr)
{
   uint32_t lb = info.lb;
   uint32_t ub = info.ub;
   uint32_t size = info.size;
   uint32_t stride = info.stride;
   RegClass rc = info.rc;

   /* check how many free regs we have */
   unsigned regs_free = reg_file.count_zero(PhysReg{lb}, ub-lb);

   /* mark and count killed operands */
   unsigned killed_ops = 0;
   std::bitset<256> is_killed_operand; /* per-register */
   for (unsigned j = 0; !is_phi(instr) && j < instr->operands.size(); j++) {
      Operand& op = instr->operands[j];
      if (op.isTemp() &&
          op.isFirstKillBeforeDef() &&
          op.physReg() >= lb &&
          op.physReg() < ub &&
          !reg_file.test(PhysReg{op.physReg().reg()}, align(op.bytes() + op.physReg().byte(), 4))) {
         assert(op.isFixed());

         for (unsigned i = 0; i < op.size(); ++i) {
            is_killed_operand[(op.physReg() & 0xff) + i] = true;
         }

         killed_ops += op.getTemp().size();
      }
   }

   assert(regs_free >= size);
   /* we might have to move dead operands to dst in order to make space */
   unsigned op_moves = 0;

   if (size > (regs_free - killed_ops))
      op_moves = size - (regs_free - killed_ops);

   /* find the best position to place the definition */
   unsigned best_pos = lb;
   unsigned num_moves = 0xFF;
   unsigned num_vars = 0;

   /* we use a sliding window to check potential positions */
   unsigned reg_lo = lb;
   unsigned reg_hi = lb + size - 1;
   for (reg_lo = lb, reg_hi = lb + size - 1; reg_hi < ub; reg_lo += stride, reg_hi += stride) {
      /* first check if the register window starts in the middle of an
       * allocated variable: this is what we have to fix to allow for
       * num_moves > size */
      if (reg_lo > lb && !reg_file.is_empty_or_blocked(PhysReg(reg_lo)) &&
          reg_file.get_id(PhysReg(reg_lo)) == reg_file.get_id(PhysReg(reg_lo).advance(-1)))
         continue;
      if (reg_hi < ub - 1 && !reg_file.is_empty_or_blocked(PhysReg(reg_hi).advance(3)) &&
          reg_file.get_id(PhysReg(reg_hi).advance(3)) == reg_file.get_id(PhysReg(reg_hi).advance(4)))
         continue;

      /* second, check that we have at most k=num_moves elements in the window
       * and no element is larger than the currently processed one */
      unsigned k = op_moves;
      unsigned n = 0;
      unsigned remaining_op_moves = op_moves;
      unsigned last_var = 0;
      bool found = true;
      bool aligned = rc == RegClass::v4 && reg_lo % 4 == 0;
      for (unsigned j = reg_lo; found && j <= reg_hi; j++) {
         /* dead operands effectively reduce the number of estimated moves */
         if (is_killed_operand[j & 0xFF]) {
            if (remaining_op_moves) {
               k--;
               remaining_op_moves--;
            }
            continue;
         }

         if (reg_file[j] == 0 || reg_file[j] == last_var)
            continue;

         if (reg_file[j] == 0xF0000000) {
            k += 1;
            n++;
            continue;
         }

         if (ctx.assignments[reg_file[j]].rc.size() >= size) {
            found = false;
            break;
         }

         /* we cannot split live ranges of linear vgprs */
         if (ctx.assignments[reg_file[j]].rc & (1 << 6)) {
            found = false;
            break;
         }

         k += ctx.assignments[reg_file[j]].rc.size();
         n++;
         last_var = reg_file[j];
      }

      if (!found || k > num_moves)
         continue;
      if (k == num_moves && n < num_vars)
         continue;
      if (!aligned && k == num_moves && n == num_vars)
         continue;

      if (found) {
         best_pos = reg_lo;
         num_moves = k;
         num_vars = n;
      }
   }

   if (num_moves == 0xFF)
      return {{}, false};

   /* now, we figured the placement for our definition */
   RegisterFile tmp_file(reg_file);
   std::set<std::pair<unsigned, unsigned>> vars = collect_vars(ctx, tmp_file, PhysReg{best_pos}, size);

   if (instr->opcode == aco_opcode::p_create_vector) {
      /* move killed operands which aren't yet at the correct position (GFX9+)
       * or which are in the definition space */
      PhysReg reg = PhysReg{best_pos};
      for (Operand& op : instr->operands) {
         if (op.isTemp() && op.isFirstKillBeforeDef() &&
             op.getTemp().type() == rc.type()) {
            if (op.physReg() != reg &&
                (ctx.program->chip_class >= GFX9 ||
                 (op.physReg().advance(op.bytes()) > PhysReg{best_pos} &&
                  op.physReg() < PhysReg{best_pos + size}))) {
               vars.emplace(op.bytes(), op.tempId());
               tmp_file.clear(op);
            } else {
               tmp_file.fill(op);
            }
         }
         reg.reg_b += op.bytes();
      }
   } else if (!is_phi(instr)) {
      /* re-enable killed operands */
      for (Operand& op : instr->operands) {
         if (op.isTemp() && op.isFirstKillBeforeDef())
            tmp_file.fill(op);
      }
   }

   std::vector<std::pair<Operand, Definition>> pc;
   if (!get_regs_for_copies(ctx, tmp_file, pc, vars, lb, ub, instr, best_pos, best_pos + size - 1))
      return {{}, false};

   parallelcopies.insert(parallelcopies.end(), pc.begin(), pc.end());

   adjust_max_used_regs(ctx, rc, best_pos);
   return {PhysReg{best_pos}, true};
}

bool get_reg_specified(ra_ctx& ctx,
                       RegisterFile& reg_file,
                       RegClass rc,
                       std::vector<std::pair<Operand, Definition>>& parallelcopies,
                       aco_ptr<Instruction>& instr,
                       PhysReg reg)
{
   std::pair<unsigned, unsigned> sdw_def_info;
   if (rc.is_subdword())
      sdw_def_info = get_subdword_definition_info(ctx.program, instr, rc);

   if (rc.is_subdword() && reg.byte() % sdw_def_info.first)
      return false;
   if (!rc.is_subdword() && reg.byte())
      return false;

   uint32_t size = rc.size();
   uint32_t stride = 1;
   uint32_t lb, ub;

   if (rc.type() == RegType::vgpr) {
      lb = 256;
      ub = 256 + ctx.program->max_reg_demand.vgpr;
   } else {
      if (size == 2)
         stride = 2;
      else if (size >= 4)
         stride = 4;
      if (reg % stride != 0)
         return false;
      lb = 0;
      ub = ctx.program->max_reg_demand.sgpr;
   }

   uint32_t reg_lo = reg.reg();
   uint32_t reg_hi = reg + (size - 1);

   if (reg_lo < lb || reg_hi >= ub || reg_lo > reg_hi)
      return false;

   if (rc.is_subdword()) {
      PhysReg test_reg;
      test_reg.reg_b = reg.reg_b & ~(sdw_def_info.second - 1);
      if (reg_file.test(test_reg, sdw_def_info.second))
         return false;
   } else {
      if (reg_file.test(reg, rc.bytes()))
         return false;
   }

   adjust_max_used_regs(ctx, rc, reg_lo);
   return true;
}

void increase_register_file(ra_ctx& ctx, RegType type) {
   uint16_t max_addressible_sgpr = ctx.program->sgpr_limit;
   uint16_t max_addressible_vgpr = ctx.program->vgpr_limit;
   if (type == RegType::vgpr && ctx.program->max_reg_demand.vgpr < max_addressible_vgpr) {
      update_vgpr_sgpr_demand(ctx.program, RegisterDemand(ctx.program->max_reg_demand.vgpr + 1, ctx.program->max_reg_demand.sgpr));
   } else if (type == RegType::sgpr && ctx.program->max_reg_demand.sgpr < max_addressible_sgpr) {
      update_vgpr_sgpr_demand(ctx.program,  RegisterDemand(ctx.program->max_reg_demand.vgpr, ctx.program->max_reg_demand.sgpr + 1));
   } else {
      //FIXME: if nothing helps, shift-rotate the registers to make space
      aco_err(ctx.program, "Failed to allocate registers during shader compilation.");
      abort();
   }
}

PhysReg get_reg(ra_ctx& ctx,
                RegisterFile& reg_file,
                Temp temp,
                std::vector<std::pair<Operand, Definition>>& parallelcopies,
                aco_ptr<Instruction>& instr,
                int operand_index=-1)
{
   auto split_vec = ctx.split_vectors.find(temp.id());
   if (split_vec != ctx.split_vectors.end()) {
      unsigned offset = 0;
      for (Definition def : split_vec->second->definitions) {
         auto affinity_it = ctx.affinities.find(def.tempId());
         if (affinity_it != ctx.affinities.end() && ctx.assignments[affinity_it->second].assigned) {
            PhysReg reg = ctx.assignments[affinity_it->second].reg;
            reg.reg_b -= offset;
            if (get_reg_specified(ctx, reg_file, temp.regClass(), parallelcopies, instr, reg))
               return reg;
         }
         offset += def.bytes();
      }
   }

   if (ctx.affinities.find(temp.id()) != ctx.affinities.end() &&
       ctx.assignments[ctx.affinities[temp.id()]].assigned) {
      PhysReg reg = ctx.assignments[ctx.affinities[temp.id()]].reg;
      if (get_reg_specified(ctx, reg_file, temp.regClass(), parallelcopies, instr, reg))
         return reg;
   }

   if (ctx.vectors.find(temp.id()) != ctx.vectors.end()) {
      Instruction* vec = ctx.vectors[temp.id()];
      unsigned byte_offset = 0;
      for (const Operand& op : vec->operands) {
         if (op.isTemp() && op.tempId() == temp.id())
            break;
         else
            byte_offset += op.bytes();
      }
      unsigned k = 0;
      for (const Operand& op : vec->operands) {
         if (op.isTemp() &&
             op.tempId() != temp.id() &&
             op.getTemp().type() == temp.type() &&
             ctx.assignments[op.tempId()].assigned) {
            PhysReg reg = ctx.assignments[op.tempId()].reg;
            reg.reg_b += (byte_offset - k);
            if (get_reg_specified(ctx, reg_file, temp.regClass(), parallelcopies, instr, reg))
               return reg;
         }
         k += op.bytes();
      }

      DefInfo info(ctx, ctx.pseudo_dummy, vec->definitions[0].regClass(), -1);
      std::pair<PhysReg, bool> res = get_reg_simple(ctx, reg_file, info);
      PhysReg reg = res.first;
      if (res.second) {
         reg.reg_b += byte_offset;
         /* make sure to only use byte offset if the instruction supports it */
         if (get_reg_specified(ctx, reg_file, temp.regClass(), parallelcopies, instr, reg))
            return reg;
      }
   }

   DefInfo info(ctx, instr, temp.regClass(), operand_index);

   std::pair<PhysReg, bool> res;

   if (!ctx.policy.skip_optimistic_path) {
      /* try to find space without live-range splits */
      res = get_reg_simple(ctx, reg_file, info);

      if (res.second)
         return res.first;
   }

   /* try to find space with live-range splits */
   res = get_reg_impl(ctx, reg_file, parallelcopies, info, instr);

   if (res.second)
      return res.first;

   /* try using more registers */

   /* We should only fail here because keeping under the limit would require
    * too many moves. */
   assert(reg_file.count_zero(PhysReg{info.lb}, info.ub-info.lb) >= info.size);

   //FIXME: if nothing helps, shift-rotate the registers to make space

   increase_register_file(ctx, info.rc.type());
   return get_reg(ctx, reg_file, temp, parallelcopies, instr, operand_index);
}

PhysReg get_reg_create_vector(ra_ctx& ctx,
                              RegisterFile& reg_file,
                              Temp temp,
                              std::vector<std::pair<Operand, Definition>>& parallelcopies,
                              aco_ptr<Instruction>& instr)
{
   RegClass rc = temp.regClass();
   /* create_vector instructions have different costs w.r.t. register coalescing */
   uint32_t size = rc.size();
   uint32_t bytes = rc.bytes();
   uint32_t stride = 1;
   uint32_t lb, ub;
   if (rc.type() == RegType::vgpr) {
      lb = 256;
      ub = 256 + ctx.program->max_reg_demand.vgpr;
   } else {
      lb = 0;
      ub = ctx.program->max_reg_demand.sgpr;
      if (size == 2)
         stride = 2;
      else if (size >= 4)
         stride = 4;
   }

   //TODO: improve p_create_vector for sub-dword vectors

   unsigned best_pos = -1;
   unsigned num_moves = 0xFF;
   bool best_war_hint = true;

   /* test for each operand which definition placement causes the least shuffle instructions */
   for (unsigned i = 0, offset = 0; i < instr->operands.size(); offset += instr->operands[i].bytes(), i++) {
      // TODO: think about, if we can alias live operands on the same register
      if (!instr->operands[i].isTemp() || !instr->operands[i].isKillBeforeDef() || instr->operands[i].getTemp().type() != rc.type())
         continue;

      if (offset > instr->operands[i].physReg().reg_b)
         continue;

      unsigned reg_lo = instr->operands[i].physReg().reg_b - offset;
      if (reg_lo % 4)
         continue;
      reg_lo /= 4;
      unsigned reg_hi = reg_lo + size - 1;
      unsigned k = 0;

      /* no need to check multiple times */
      if (reg_lo == best_pos)
         continue;

      /* check borders */
      // TODO: this can be improved */
      if (reg_lo < lb || reg_hi >= ub || reg_lo % stride != 0)
         continue;
      if (reg_lo > lb && reg_file[reg_lo] != 0 && reg_file.get_id(PhysReg(reg_lo)) == reg_file.get_id(PhysReg(reg_lo).advance(-1)))
         continue;
      if (reg_hi < ub - 1 && reg_file[reg_hi] != 0 && reg_file.get_id(PhysReg(reg_hi).advance(3)) == reg_file.get_id(PhysReg(reg_hi).advance(4)))
         continue;

      /* count variables to be moved and check war_hint */
      bool war_hint = false;
      bool linear_vgpr = false;
      for (unsigned j = reg_lo; j <= reg_hi && !linear_vgpr; j++) {
         if (reg_file[j] != 0) {
            if (reg_file[j] == 0xF0000000) {
               PhysReg reg;
               reg.reg_b = j * 4;
               unsigned bytes_left = bytes - (j - reg_lo) * 4;
               for (unsigned byte_idx = 0; byte_idx < MIN2(bytes_left, 4); byte_idx++, reg.reg_b++)
                  k += reg_file.test(reg, 1);
            } else {
               k += 4;
               /* we cannot split live ranges of linear vgprs */
               if (ctx.assignments[reg_file[j]].rc & (1 << 6))
                  linear_vgpr = true;
            }
         }
         war_hint |= ctx.war_hint[j];
      }
      if (linear_vgpr || (war_hint && !best_war_hint))
         continue;

      /* count operands in wrong positions */
      for (unsigned j = 0, offset2 = 0; j < instr->operands.size(); offset2 += instr->operands[j].bytes(), j++) {
         if (j == i ||
             !instr->operands[j].isTemp() ||
             instr->operands[j].getTemp().type() != rc.type())
            continue;
         if (instr->operands[j].physReg().reg_b != reg_lo * 4 + offset2)
            k += instr->operands[j].bytes();
      }
      bool aligned = rc == RegClass::v4 && reg_lo % 4 == 0;
      if (k > num_moves || (!aligned && k == num_moves))
         continue;

      best_pos = reg_lo;
      num_moves = k;
      best_war_hint = war_hint;
   }

   if (num_moves >= bytes)
      return get_reg(ctx, reg_file, temp, parallelcopies, instr);

   /* re-enable killed operands which are in the wrong position */
   RegisterFile tmp_file(reg_file);
   for (unsigned i = 0, offset = 0; i < instr->operands.size(); offset += instr->operands[i].bytes(), i++) {
      if (instr->operands[i].isTemp() &&
          instr->operands[i].isFirstKillBeforeDef() &&
          instr->operands[i].physReg().reg_b != best_pos * 4 + offset)
         tmp_file.fill(instr->operands[i]);
   }

   /* collect variables to be moved */
   std::set<std::pair<unsigned, unsigned>> vars = collect_vars(ctx, tmp_file, PhysReg{best_pos}, size);

   for (unsigned i = 0, offset = 0; i < instr->operands.size(); offset += instr->operands[i].bytes(), i++) {
      if (!instr->operands[i].isTemp() || !instr->operands[i].isFirstKillBeforeDef() ||
          instr->operands[i].getTemp().type() != rc.type())
         continue;
      bool correct_pos = instr->operands[i].physReg().reg_b == best_pos * 4 + offset;
      /* GFX9+: move killed operands which aren't yet at the correct position
       * Moving all killed operands generally leads to more register swaps.
       * This is only done on GFX9+ because of the cheap v_swap instruction.
       */
      if (ctx.program->chip_class >= GFX9 && !correct_pos) {
         vars.emplace(instr->operands[i].bytes(), instr->operands[i].tempId());
         tmp_file.clear(instr->operands[i]);
      /* fill operands which are in the correct position to avoid overwriting */
      } else if (correct_pos) {
         tmp_file.fill(instr->operands[i]);
      }
   }
   bool success = false;
   std::vector<std::pair<Operand, Definition>> pc;
   success = get_regs_for_copies(ctx, tmp_file, pc, vars, lb, ub, instr, best_pos, best_pos + size - 1);

   if (!success) {
      increase_register_file(ctx, temp.type());
      return get_reg_create_vector(ctx, reg_file, temp, parallelcopies, instr);
   }

   parallelcopies.insert(parallelcopies.end(), pc.begin(), pc.end());
   adjust_max_used_regs(ctx, rc, best_pos);

   return PhysReg{best_pos};
}

void handle_pseudo(ra_ctx& ctx,
                   const RegisterFile& reg_file,
                   Instruction* instr)
{
   if (instr->format != Format::PSEUDO)
      return;

   /* all instructions which use handle_operands() need this information */
   switch (instr->opcode) {
   case aco_opcode::p_extract_vector:
   case aco_opcode::p_create_vector:
   case aco_opcode::p_split_vector:
   case aco_opcode::p_parallelcopy:
   case aco_opcode::p_wqm:
      break;
   default:
      return;
   }

   /* if all definitions are vgpr, no need to care for SCC */
   bool writes_sgpr = false;
   for (Definition& def : instr->definitions) {
      if (def.getTemp().type() == RegType::sgpr) {
         writes_sgpr = true;
         break;
      }
   }
   /* if all operands are constant, no need to care either */
   bool reads_sgpr = false;
   bool reads_subdword = false;
   for (Operand& op : instr->operands) {
      if (op.isTemp() && op.getTemp().type() == RegType::sgpr) {
         reads_sgpr = true;
         break;
      }
      if (op.isTemp() && op.regClass().is_subdword())
         reads_subdword = true;
   }
   bool needs_scratch_reg = (writes_sgpr && reads_sgpr) ||
                            (ctx.program->chip_class <= GFX7 && reads_subdword);
   if (!needs_scratch_reg)
      return;

   Pseudo_instruction *pi = (Pseudo_instruction *)instr;
   if (reg_file[scc.reg()]) {
      pi->tmp_in_scc = true;

      int reg = ctx.max_used_sgpr;
      for (; reg >= 0 && reg_file[reg]; reg--)
         ;
      if (reg < 0) {
         reg = ctx.max_used_sgpr + 1;
         for (; reg < ctx.program->max_reg_demand.sgpr && reg_file[reg]; reg++)
            ;
         if (reg == ctx.program->max_reg_demand.sgpr) {
            assert(reads_subdword && reg_file[m0] == 0);
            reg = m0;
         }
      }

      adjust_max_used_regs(ctx, s1, reg);
      pi->scratch_sgpr = PhysReg{(unsigned)reg};
   } else {
      pi->tmp_in_scc = false;
   }
}

bool operand_can_use_reg(chip_class chip, aco_ptr<Instruction>& instr, unsigned idx, PhysReg reg, RegClass rc)
{
   if (instr->operands[idx].isFixed())
      return instr->operands[idx].physReg() == reg;

   bool is_writelane = instr->opcode == aco_opcode::v_writelane_b32 ||
                       instr->opcode == aco_opcode::v_writelane_b32_e64;
   if (chip <= GFX9 && is_writelane && idx <= 1) {
      /* v_writelane_b32 can take two sgprs but only if one is m0. */
      bool is_other_sgpr = instr->operands[!idx].isTemp() &&
                           (!instr->operands[!idx].isFixed() ||
                            instr->operands[!idx].physReg() != m0);
      if (is_other_sgpr && instr->operands[!idx].tempId() != instr->operands[idx].tempId()) {
         instr->operands[idx].setFixed(m0);
         return reg == m0;
      }
   }

   if (reg.byte()) {
      unsigned stride = get_subdword_operand_stride(chip, instr, idx, rc);
      if (reg.byte() % stride)
         return false;
   }

   switch (instr->format) {
   case Format::SMEM:
      return reg != scc &&
             reg != exec &&
             (reg != m0 || idx == 1 || idx == 3) && /* offset can be m0 */
             (reg != vcc || (instr->definitions.empty() && idx == 2)); /* sdata can be vcc */
   default:
      // TODO: there are more instructions with restrictions on registers
      return true;
   }
}

void get_reg_for_operand(ra_ctx& ctx, RegisterFile& register_file,
                         std::vector<std::pair<Operand, Definition>>& parallelcopy,
                         aco_ptr<Instruction>& instr, Operand& operand, unsigned operand_index)
{
   /* check if the operand is fixed */
   PhysReg dst;
   bool blocking_var = false;
   if (operand.isFixed()) {
      assert(operand.physReg() != ctx.assignments[operand.tempId()].reg);

      /* check if target reg is blocked, and move away the blocking var */
      if (register_file[operand.physReg().reg()]) {
         assert(register_file[operand.physReg()] != 0xF0000000);
         uint32_t blocking_id = register_file[operand.physReg().reg()];
         RegClass rc = ctx.assignments[blocking_id].rc;
         Operand pc_op = Operand(Temp{blocking_id, rc});
         pc_op.setFixed(operand.physReg());

         /* find free reg */
         PhysReg reg = get_reg(ctx, register_file, pc_op.getTemp(), parallelcopy, ctx.pseudo_dummy);
         update_renames(ctx, register_file, parallelcopy, ctx.pseudo_dummy, true);
         Definition pc_def = Definition(PhysReg{reg}, pc_op.regClass());
         parallelcopy.emplace_back(pc_op, pc_def);
         blocking_var = true;
      }
      dst = operand.physReg();

   } else {
      dst = get_reg(ctx, register_file, operand.getTemp(), parallelcopy, instr, operand_index);
      update_renames(ctx, register_file, parallelcopy, instr, instr->opcode != aco_opcode::p_create_vector);
   }

   Operand pc_op = operand;
   pc_op.setFixed(ctx.assignments[operand.tempId()].reg);
   Definition pc_def = Definition(dst, pc_op.regClass());
   parallelcopy.emplace_back(pc_op, pc_def);
   update_renames(ctx, register_file, parallelcopy, instr, true);

   if (operand.isKillBeforeDef())
      register_file.fill(parallelcopy.back().second);
   /* fill in case the blocking var is a killed operand (update_renames() will not fill it) */
   if (blocking_var)
      register_file.fill(parallelcopy[parallelcopy.size() - 2].second);
}

Temp read_variable(ra_ctx& ctx, Temp val, unsigned block_idx)
{
   std::unordered_map<unsigned, Temp>::iterator it = ctx.renames[block_idx].find(val.id());
   if (it == ctx.renames[block_idx].end())
      return val;
   else
      return it->second;
}

Temp handle_live_in(ra_ctx& ctx, Temp val, Block* block)
{
   std::vector<unsigned>& preds = val.is_linear() ? block->linear_preds : block->logical_preds;
   if (preds.size() == 0 || val.regClass() == val.regClass().as_linear())
      return val;

   assert(preds.size() > 0);

   Temp new_val;
   if (!ctx.sealed[block->index]) {
      /* consider rename from already processed predecessor */
      Temp tmp = read_variable(ctx, val, preds[0]);

      /* if the block is not sealed yet, we create an incomplete phi (which might later get removed again) */
      new_val = ctx.program->allocateTmp(val.regClass());
      ctx.assignments.emplace_back();
      aco_opcode opcode = val.is_linear() ? aco_opcode::p_linear_phi : aco_opcode::p_phi;
      aco_ptr<Instruction> phi{create_instruction<Pseudo_instruction>(opcode, Format::PSEUDO, preds.size(), 1)};
      phi->definitions[0] = Definition(new_val);
      for (unsigned i = 0; i < preds.size(); i++)
         phi->operands[i] = Operand(val);
      if (tmp.regClass() == new_val.regClass())
         ctx.affinities[new_val.id()] = tmp.id();

      ctx.phi_map.emplace(new_val.id(), phi_info{phi.get(), block->index});
      ctx.incomplete_phis[block->index].emplace_back(phi.get());
      block->instructions.insert(block->instructions.begin(), std::move(phi));

   } else if (preds.size() == 1) {
      /* if the block has only one predecessor, just look there for the name */
      new_val = read_variable(ctx, val, preds[0]);
   } else {
      /* there are multiple predecessors and the block is sealed */
      Temp *const ops = (Temp *)alloca(preds.size() * sizeof(Temp));

      /* get the rename from each predecessor and check if they are the same */
      bool needs_phi = false;
      for (unsigned i = 0; i < preds.size(); i++) {
         ops[i] = read_variable(ctx, val, preds[i]);
         if (i == 0)
            new_val = ops[i];
         else
            needs_phi |= !(new_val == ops[i]);
      }

      if (needs_phi) {
         /* the variable has been renamed differently in the predecessors: we need to insert a phi */
         aco_opcode opcode = val.is_linear() ? aco_opcode::p_linear_phi : aco_opcode::p_phi;
         aco_ptr<Instruction> phi{create_instruction<Pseudo_instruction>(opcode, Format::PSEUDO, preds.size(), 1)};
         new_val = ctx.program->allocateTmp(val.regClass());
         phi->definitions[0] = Definition(new_val);
         for (unsigned i = 0; i < preds.size(); i++) {
            phi->operands[i] = Operand(ops[i]);
            phi->operands[i].setFixed(ctx.assignments[ops[i].id()].reg);
            if (ops[i].regClass() == new_val.regClass())
               ctx.affinities[new_val.id()] = ops[i].id();
            /* make sure the operand gets it's original name in case
             * it comes from an incomplete phi */
            std::unordered_map<unsigned, phi_info>::iterator it = ctx.phi_map.find(ops[i].id());
            if (it != ctx.phi_map.end())
               it->second.uses.emplace(phi.get());
         }
         ctx.assignments.emplace_back();
         assert(ctx.assignments.size() == ctx.program->peekAllocationId());
         ctx.phi_map.emplace(new_val.id(), phi_info{phi.get(), block->index});
         block->instructions.insert(block->instructions.begin(), std::move(phi));
      }
   }

   if (new_val != val) {
      ctx.renames[block->index][val.id()] = new_val;
      ctx.orig_names[new_val.id()] = val;
   }
   return new_val;
}

void try_remove_trivial_phi(ra_ctx& ctx, Temp temp)
{
   std::unordered_map<unsigned, phi_info>::iterator info = ctx.phi_map.find(temp.id());

   if (info == ctx.phi_map.end() || !ctx.sealed[info->second.block_idx])
      return;

   assert(info->second.block_idx != 0);
   Instruction* phi = info->second.phi;
   Temp same = Temp();
   Definition def = phi->definitions[0];

   /* a phi node is trivial if all operands are the same as the definition of the phi */
   for (const Operand& op : phi->operands) {
      const Temp t = op.getTemp();
      if (t == same || t == def.getTemp()) {
         assert(t == same || op.physReg() == def.physReg());
         continue;
      }
      if (same != Temp())
         return;

      same = t;
   }
   assert(same != Temp() || same == def.getTemp());

   /* reroute all uses to same and remove phi */
   std::vector<Temp> phi_users;
   std::unordered_map<unsigned, phi_info>::iterator same_phi_info = ctx.phi_map.find(same.id());
   for (Instruction* instr : info->second.uses) {
      assert(phi != instr);
      /* recursively try to remove trivial phis */
      if (is_phi(instr)) {
         /* ignore if the phi was already flagged trivial */
         if (instr->definitions.empty())
            continue;

         if (instr->definitions[0].getTemp() != temp)
            phi_users.emplace_back(instr->definitions[0].getTemp());
      }
      for (Operand& op : instr->operands) {
         if (op.isTemp() && op.tempId() == def.tempId()) {
            op.setTemp(same);
            if (same_phi_info != ctx.phi_map.end())
               same_phi_info->second.uses.emplace(instr);
         }
      }
   }

   auto it = ctx.orig_names.find(same.id());
   unsigned orig_var = it != ctx.orig_names.end() ? it->second.id() : same.id();
   for (unsigned i = 0; i < ctx.program->blocks.size(); i++) {
      auto rename_it = ctx.renames[i].find(orig_var);
      if (rename_it != ctx.renames[i].end() && rename_it->second == def.getTemp())
         ctx.renames[i][orig_var] = same;
   }

   phi->definitions.clear(); /* this indicates that the phi can be removed */
   ctx.phi_map.erase(info);
   for (Temp t : phi_users)
      try_remove_trivial_phi(ctx, t);

   return;
}

} /* end namespace */


void register_allocation(Program *program, std::vector<IDSet>& live_out_per_block, ra_test_policy policy)
{
   ra_ctx ctx(program, policy);
   std::vector<std::vector<Temp>> phi_ressources;
   std::unordered_map<unsigned, unsigned> temp_to_phi_ressources;

   for (auto block_rit = program->blocks.rbegin(); block_rit != program->blocks.rend(); block_rit++) {
      Block& block = *block_rit;

      /* first, compute the death points of all live vars within the block */
      IDSet& live = live_out_per_block[block.index];

      std::vector<aco_ptr<Instruction>>::reverse_iterator rit;
      for (rit = block.instructions.rbegin(); rit != block.instructions.rend(); ++rit) {
         aco_ptr<Instruction>& instr = *rit;
         if (is_phi(instr)) {
            if (instr->definitions[0].isKill() || instr->definitions[0].isFixed()) {
               live.erase(instr->definitions[0].tempId());
               continue;
            }
            /* collect information about affinity-related temporaries */
            std::vector<Temp> affinity_related;
            /* affinity_related[0] is the last seen affinity-related temp */
            affinity_related.emplace_back(instr->definitions[0].getTemp());
            affinity_related.emplace_back(instr->definitions[0].getTemp());
            for (const Operand& op : instr->operands) {
               if (op.isTemp() && op.regClass() == instr->definitions[0].regClass()) {
                  affinity_related.emplace_back(op.getTemp());
                  temp_to_phi_ressources[op.tempId()] = phi_ressources.size();
               }
            }
            phi_ressources.emplace_back(std::move(affinity_related));
         } else {
            /* add vector affinities */
            if (instr->opcode == aco_opcode::p_create_vector) {
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.isFirstKill() && op.getTemp().type() == instr->definitions[0].getTemp().type())
                     ctx.vectors[op.tempId()] = instr.get();
               }
            }

            if (instr->opcode == aco_opcode::p_split_vector && instr->operands[0].isFirstKillBeforeDef())
               ctx.split_vectors[instr->operands[0].tempId()] = instr.get();

            /* add operands to live variables */
            for (const Operand& op : instr->operands) {
               if (op.isTemp())
                  live.insert(op.tempId());
            }
         }

         /* erase definitions from live */
         for (unsigned i = 0; i < instr->definitions.size(); i++) {
            const Definition& def = instr->definitions[i];
            if (!def.isTemp())
               continue;
            live.erase(def.tempId());
            /* mark last-seen phi operand */
            std::unordered_map<unsigned, unsigned>::iterator it = temp_to_phi_ressources.find(def.tempId());
            if (it != temp_to_phi_ressources.end() && def.regClass() == phi_ressources[it->second][0].regClass()) {
               phi_ressources[it->second][0] = def.getTemp();
               /* try to coalesce phi affinities with parallelcopies */
               Operand op = Operand();
               if (!def.isFixed() && instr->opcode == aco_opcode::p_parallelcopy)
                  op = instr->operands[i];
               else if ((instr->opcode == aco_opcode::v_mad_f32 ||
                        (instr->opcode == aco_opcode::v_fma_f32 && program->chip_class >= GFX10) ||
                        instr->opcode == aco_opcode::v_mad_f16 ||
                        instr->opcode == aco_opcode::v_mad_legacy_f16 ||
                        (instr->opcode == aco_opcode::v_fma_f16 && program->chip_class >= GFX10)) && !instr->usesModifiers())
                  op = instr->operands[2];

               if (op.isTemp() && op.isFirstKillBeforeDef() && def.regClass() == op.regClass()) {
                  phi_ressources[it->second].emplace_back(op.getTemp());
                  temp_to_phi_ressources[op.tempId()] = it->second;
               }
            }
         }
      }
   }
   /* create affinities */
   for (std::vector<Temp>& vec : phi_ressources) {
      assert(vec.size() > 1);
      for (unsigned i = 1; i < vec.size(); i++)
         if (vec[i].id() != vec[0].id())
            ctx.affinities[vec[i].id()] = vec[0].id();
   }

   /* state of register file after phis */
   std::vector<std::bitset<128>> sgpr_live_in(program->blocks.size());

   for (Block& block : program->blocks) {
      IDSet& live = live_out_per_block[block.index];
      /* initialize register file */
      assert(block.index != 0 || live.empty());
      RegisterFile register_file;
      ctx.war_hint.reset();

      for (unsigned t : live) {
         Temp renamed = handle_live_in(ctx, Temp(t, program->temp_rc[t]), &block);
         assignment& var = ctx.assignments[renamed.id()];
         /* due to live-range splits, the live-in might be a phi, now */
         if (var.assigned)
            register_file.fill(Definition(renamed.id(), var.reg, var.rc));
      }

      std::vector<aco_ptr<Instruction>> instructions;
      std::vector<aco_ptr<Instruction>>::iterator instr_it;

      /* this is a slight adjustment from the paper as we already have phi nodes:
       * We consider them incomplete phis and only handle the definition. */

      /* handle fixed phi definitions */
      for (instr_it = block.instructions.begin(); instr_it != block.instructions.end(); ++instr_it) {
         aco_ptr<Instruction>& phi = *instr_it;
         if (!is_phi(phi))
            break;
         Definition& definition = phi->definitions[0];
         if (!definition.isFixed())
            continue;

         /* check if a dead exec mask phi is needed */
         if (definition.isKill()) {
            for (Operand& op : phi->operands) {
               assert(op.isTemp());
               if (!ctx.assignments[op.tempId()].assigned ||
                   ctx.assignments[op.tempId()].reg != exec) {
                   definition.setKill(false);
                   break;
               }
            }
         }

         if (definition.isKill())
            continue;

         assert(definition.physReg() == exec);
         assert(!register_file.test(definition.physReg(), definition.bytes()));
         register_file.fill(definition);
         ctx.assignments[definition.tempId()] = {definition.physReg(), definition.regClass()};
      }

      /* look up the affinities */
      for (instr_it = block.instructions.begin(); instr_it != block.instructions.end(); ++instr_it) {
         aco_ptr<Instruction>& phi = *instr_it;
         if (!is_phi(phi))
            break;
         Definition& definition = phi->definitions[0];
         if (definition.isKill() || definition.isFixed())
             continue;

         if (ctx.affinities.find(definition.tempId()) != ctx.affinities.end() &&
             ctx.assignments[ctx.affinities[definition.tempId()]].assigned) {
            assert(ctx.assignments[ctx.affinities[definition.tempId()]].rc == definition.regClass());
            PhysReg reg = ctx.assignments[ctx.affinities[definition.tempId()]].reg;
            bool try_use_special_reg = reg == scc || reg == exec;
            if (try_use_special_reg) {
               for (const Operand& op : phi->operands) {
                  if (!(op.isTemp() && ctx.assignments[op.tempId()].assigned &&
                        ctx.assignments[op.tempId()].reg == reg)) {
                     try_use_special_reg = false;
                     break;
                  }
               }
               if (!try_use_special_reg)
                  continue;
            }
            /* only assign if register is still free */
            if (!register_file.test(reg, definition.bytes())) {
               definition.setFixed(reg);
               register_file.fill(definition);
               ctx.assignments[definition.tempId()] = {definition.physReg(), definition.regClass()};
            }
         }
      }

      /* find registers for phis without affinity or where the register was blocked */
      for (instr_it = block.instructions.begin();instr_it != block.instructions.end(); ++instr_it) {
         aco_ptr<Instruction>& phi = *instr_it;
         if (!is_phi(phi))
            break;

         Definition& definition = phi->definitions[0];
         if (definition.isKill())
            continue;

         if (!definition.isFixed()) {
            std::vector<std::pair<Operand, Definition>> parallelcopy;
            /* try to find a register that is used by at least one operand */
            for (const Operand& op : phi->operands) {
               if (!(op.isTemp() && ctx.assignments[op.tempId()].assigned))
                  continue;
               PhysReg reg = ctx.assignments[op.tempId()].reg;
               /* we tried this already on the previous loop */
               if (reg == scc || reg == exec)
                  continue;
               if (get_reg_specified(ctx, register_file, definition.regClass(), parallelcopy, phi, reg)) {
                  definition.setFixed(reg);
                  break;
               }
            }
            if (!definition.isFixed()) {
               definition.setFixed(get_reg(ctx, register_file, definition.getTemp(), parallelcopy, phi));
               update_renames(ctx, register_file, parallelcopy, phi, true);
            }

            /* process parallelcopy */
            for (std::pair<Operand, Definition> pc : parallelcopy) {
               /* see if it's a copy from a different phi */
               //TODO: prefer moving some previous phis over live-ins
               //TODO: somehow prevent phis fixed before the RA from being updated (shouldn't be a problem in practice since they can only be fixed to exec)
               Instruction *prev_phi = NULL;
               std::vector<aco_ptr<Instruction>>::iterator phi_it;
               for (phi_it = instructions.begin(); phi_it != instructions.end(); ++phi_it) {
                  if ((*phi_it)->definitions[0].tempId() == pc.first.tempId())
                     prev_phi = phi_it->get();
               }
               phi_it = instr_it;
               while (!prev_phi && is_phi(*++phi_it)) {
                  if ((*phi_it)->definitions[0].tempId() == pc.first.tempId())
                     prev_phi = phi_it->get();
               }
               if (prev_phi) {
                  /* if so, just update that phi's register */
                  register_file.clear(prev_phi->definitions[0]);
                  prev_phi->definitions[0].setFixed(pc.second.physReg());
                  ctx.assignments[prev_phi->definitions[0].tempId()] = {pc.second.physReg(), pc.second.regClass()};
                  register_file.fill(prev_phi->definitions[0]);
                  continue;
               }

               /* rename */
               std::unordered_map<unsigned, Temp>::iterator orig_it = ctx.orig_names.find(pc.first.tempId());
               Temp orig = pc.first.getTemp();
               if (orig_it != ctx.orig_names.end())
                  orig = orig_it->second;
               else
                  ctx.orig_names[pc.second.tempId()] = orig;
               ctx.renames[block.index][orig.id()] = pc.second.getTemp();

               /* otherwise, this is a live-in and we need to create a new phi
                * to move it in this block's predecessors */
               aco_opcode opcode = pc.first.getTemp().is_linear() ? aco_opcode::p_linear_phi : aco_opcode::p_phi;
               std::vector<unsigned>& preds = pc.first.getTemp().is_linear() ? block.linear_preds : block.logical_preds;
               aco_ptr<Instruction> new_phi{create_instruction<Pseudo_instruction>(opcode, Format::PSEUDO, preds.size(), 1)};
               new_phi->definitions[0] = pc.second;
               for (unsigned i = 0; i < preds.size(); i++)
                  new_phi->operands[i] = Operand(pc.first);
               instructions.emplace_back(std::move(new_phi));
            }

            register_file.fill(definition);
            ctx.assignments[definition.tempId()] = {definition.physReg(), definition.regClass()};
         }
         live.insert(definition.tempId());

         /* update phi affinities */
         for (const Operand& op : phi->operands) {
            if (op.isTemp() && op.regClass() == phi->definitions[0].regClass())
               ctx.affinities[op.tempId()] = definition.tempId();
         }

         instructions.emplace_back(std::move(*instr_it));
      }

      /* fill in sgpr_live_in */
      for (unsigned i = 0; i <= ctx.max_used_sgpr; i++)
         sgpr_live_in[block.index][i] = register_file[i];
      sgpr_live_in[block.index][127] = register_file[scc.reg()];

      /* Handle all other instructions of the block */
      for (; instr_it != block.instructions.end(); ++instr_it) {
         aco_ptr<Instruction>& instr = *instr_it;

         /* parallelcopies from p_phi are inserted here which means
          * live ranges of killed operands end here as well */
         if (instr->opcode == aco_opcode::p_logical_end) {
            /* no need to process this instruction any further */
            if (block.logical_succs.size() != 1) {
               instructions.emplace_back(std::move(instr));
               continue;
            }

            Block& succ = program->blocks[block.logical_succs[0]];
            unsigned idx = 0;
            for (; idx < succ.logical_preds.size(); idx++) {
               if (succ.logical_preds[idx] == block.index)
                  break;
            }
            for (aco_ptr<Instruction>& phi : succ.instructions) {
               if (phi->opcode == aco_opcode::p_phi) {
                  if (phi->operands[idx].isTemp() &&
                      phi->operands[idx].getTemp().type() == RegType::sgpr &&
                      phi->operands[idx].isFirstKillBeforeDef()) {
                     Definition phi_op(read_variable(ctx, phi->operands[idx].getTemp(), block.index));
                     phi_op.setFixed(ctx.assignments[phi_op.tempId()].reg);
                     register_file.clear(phi_op);
                  }
               } else if (phi->opcode != aco_opcode::p_linear_phi) {
                  break;
               }
            }
            instructions.emplace_back(std::move(instr));
            continue;
         }

         std::vector<std::pair<Operand, Definition>> parallelcopy;

         assert(!is_phi(instr));

         /* handle operands */
         for (unsigned i = 0; i < instr->operands.size(); ++i) {
            auto& operand = instr->operands[i];
            if (!operand.isTemp())
               continue;

            /* rename operands */
            operand.setTemp(read_variable(ctx, operand.getTemp(), block.index));
            assert(ctx.assignments[operand.tempId()].assigned);

            PhysReg reg = ctx.assignments[operand.tempId()].reg;
            if (operand_can_use_reg(program->chip_class, instr, i, reg, operand.regClass()))
               operand.setFixed(reg);
            else
               get_reg_for_operand(ctx, register_file, parallelcopy, instr, operand, i);

            if (instr->format == Format::EXP ||
                (instr->isVMEM() && i == 3 && ctx.program->chip_class == GFX6) ||
                (instr->format == Format::DS && static_cast<DS_instruction*>(instr.get())->gds)) {
               for (unsigned j = 0; j < operand.size(); j++)
                  ctx.war_hint.set(operand.physReg().reg() + j);
            }

            std::unordered_map<unsigned, phi_info>::iterator phi = ctx.phi_map.find(operand.getTemp().id());
            if (phi != ctx.phi_map.end())
               phi->second.uses.emplace(instr.get());
         }

         /* remove dead vars from register file */
         for (const Operand& op : instr->operands) {
            if (op.isTemp() && op.isFirstKillBeforeDef())
               register_file.clear(op);
         }

         /* try to optimize v_mad_f32 -> v_mac_f32 */
         if ((instr->opcode == aco_opcode::v_mad_f32 ||
              (instr->opcode == aco_opcode::v_fma_f32 && program->chip_class >= GFX10) ||
              instr->opcode == aco_opcode::v_mad_f16 ||
              instr->opcode == aco_opcode::v_mad_legacy_f16 ||
              (instr->opcode == aco_opcode::v_fma_f16 && program->chip_class >= GFX10)) &&
             instr->operands[2].isTemp() &&
             instr->operands[2].isKillBeforeDef() &&
             instr->operands[2].getTemp().type() == RegType::vgpr &&
             instr->operands[1].isTemp() &&
             instr->operands[1].getTemp().type() == RegType::vgpr &&
             !instr->usesModifiers() &&
             instr->operands[0].physReg().byte() == 0 &&
             instr->operands[1].physReg().byte() == 0 &&
             instr->operands[2].physReg().byte() == 0) {
            unsigned def_id = instr->definitions[0].tempId();
            auto it = ctx.affinities.find(def_id);
            if (it == ctx.affinities.end() || !ctx.assignments[it->second].assigned ||
                instr->operands[2].physReg() == ctx.assignments[it->second].reg ||
                register_file.test(ctx.assignments[it->second].reg, instr->operands[2].bytes())) {
               instr->format = Format::VOP2;
               switch (instr->opcode) {
               case aco_opcode::v_mad_f32:
                  instr->opcode = aco_opcode::v_mac_f32;
                  break;
               case aco_opcode::v_fma_f32:
                  instr->opcode = aco_opcode::v_fmac_f32;
                  break;
               case aco_opcode::v_mad_f16:
               case aco_opcode::v_mad_legacy_f16:
                  instr->opcode = aco_opcode::v_mac_f16;
                  break;
               case aco_opcode::v_fma_f16:
                  instr->opcode = aco_opcode::v_fmac_f16;
                  break;
               default:
                  break;
               }
            }
         }

         /* handle definitions which must have the same register as an operand */
         if (instr->opcode == aco_opcode::v_interp_p2_f32 ||
             instr->opcode == aco_opcode::v_mac_f32 ||
             instr->opcode == aco_opcode::v_fmac_f32 ||
             instr->opcode == aco_opcode::v_mac_f16 ||
             instr->opcode == aco_opcode::v_fmac_f16 ||
             instr->opcode == aco_opcode::v_writelane_b32 ||
             instr->opcode == aco_opcode::v_writelane_b32_e64) {
            instr->definitions[0].setFixed(instr->operands[2].physReg());
         } else if (instr->opcode == aco_opcode::s_addk_i32 ||
                    instr->opcode == aco_opcode::s_mulk_i32) {
            instr->definitions[0].setFixed(instr->operands[0].physReg());
         } else if (instr->format == Format::MUBUF &&
                    instr->definitions.size() == 1 &&
                    instr->operands.size() == 4) {
            instr->definitions[0].setFixed(instr->operands[3].physReg());
         } else if (instr->format == Format::MIMG &&
                    instr->definitions.size() == 1 &&
                    instr->operands[1].regClass().type() == RegType::vgpr) {
            instr->definitions[0].setFixed(instr->operands[1].physReg());
         }

         ctx.defs_done.reset();

         /* handle fixed definitions first */
         for (unsigned i = 0; i < instr->definitions.size(); ++i) {
            auto& definition = instr->definitions[i];
            if (!definition.isFixed())
               continue;

            adjust_max_used_regs(ctx, definition.regClass(), definition.physReg());
            /* check if the target register is blocked */
            if (register_file.test(definition.physReg(), definition.bytes())) {
               /* create parallelcopy pair to move blocking vars */
               std::set<std::pair<unsigned, unsigned>> vars = collect_vars(ctx, register_file, definition.physReg(), definition.size());

               RegisterFile tmp_file(register_file);
               /* re-enable the killed operands, so that we don't move the blocking vars there */
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.isFirstKillBeforeDef())
                     tmp_file.fill(op);
               }

               ASSERTED bool success = false;
               DefInfo info(ctx, instr, definition.regClass(), -1);
               success = get_regs_for_copies(ctx, tmp_file, parallelcopy,
                                             vars, info.lb, info.ub, instr,
                                             definition.physReg(),
                                             definition.physReg() + definition.size() - 1);
               assert(success);

               update_renames(ctx, register_file, parallelcopy, instr, false);
            }
            ctx.defs_done.set(i);

            if (!definition.isTemp())
               continue;

            /* set live if it has a kill point */
            if (!definition.isKill())
               live.insert(definition.tempId());

            ctx.assignments[definition.tempId()] = {definition.physReg(), definition.regClass()};
            register_file.fill(definition);
         }

         /* handle all other definitions */
         for (unsigned i = 0; i < instr->definitions.size(); ++i) {
            Definition *definition = &instr->definitions[i];

            if (definition->isFixed() || !definition->isTemp())
               continue;

            /* find free reg */
            if (definition->hasHint() && register_file[definition->physReg().reg()] == 0)
               definition->setFixed(definition->physReg());
            else if (instr->opcode == aco_opcode::p_split_vector) {
               PhysReg reg = instr->operands[0].physReg();
               for (unsigned j = 0; j < i; j++)
                  reg.reg_b += instr->definitions[j].bytes();
               if (get_reg_specified(ctx, register_file, definition->regClass(), parallelcopy, instr, reg))
                  definition->setFixed(reg);
            } else if (instr->opcode == aco_opcode::p_wqm || instr->opcode == aco_opcode::p_parallelcopy) {
               PhysReg reg = instr->operands[i].physReg();
               if (instr->operands[i].isTemp() &&
                   instr->operands[i].getTemp().type() == definition->getTemp().type() &&
                   !register_file.test(reg, definition->bytes()))
                  definition->setFixed(reg);
            } else if (instr->opcode == aco_opcode::p_extract_vector) {
               PhysReg reg = instr->operands[0].physReg();
               reg.reg_b += definition->bytes() * instr->operands[1].constantValue();
               if (get_reg_specified(ctx, register_file, definition->regClass(), parallelcopy, instr, reg))
                  definition->setFixed(reg);
            } else if (instr->opcode == aco_opcode::p_create_vector) {
               PhysReg reg = get_reg_create_vector(ctx, register_file, definition->getTemp(),
                                                   parallelcopy, instr);
               update_renames(ctx, register_file, parallelcopy, instr, false);
               definition->setFixed(reg);
            }

            if (!definition->isFixed()) {
               Temp tmp = definition->getTemp();
               if (definition->regClass().is_subdword() && definition->bytes() < 4) {
                  PhysReg reg = get_reg(ctx, register_file, tmp, parallelcopy, instr);
                  bool partial = !(tmp.bytes() <= 4 && reg.byte() == 0 && !register_file.test(reg, 4));
                  add_subdword_definition(program, instr, i, reg, partial);
                  definition = &instr->definitions[i]; /* add_subdword_definition can invalidate the reference */
               } else {
                  definition->setFixed(get_reg(ctx, register_file, tmp, parallelcopy, instr));
               }
               update_renames(ctx, register_file, parallelcopy, instr, instr->opcode != aco_opcode::p_create_vector);
            }

            assert(definition->isFixed() && ((definition->getTemp().type() == RegType::vgpr && definition->physReg() >= 256) ||
                                             (definition->getTemp().type() != RegType::vgpr && definition->physReg() < 256)));
            ctx.defs_done.set(i);

            /* set live if it has a kill point */
            if (!definition->isKill())
               live.insert(definition->tempId());

            ctx.assignments[definition->tempId()] = {definition->physReg(), definition->regClass()};
            register_file.fill(*definition);
         }

         handle_pseudo(ctx, register_file, instr.get());

         /* kill definitions and late-kill operands and ensure that sub-dword operands can actually be read */
         for (const Definition& def : instr->definitions) {
             if (def.isTemp() && def.isKill())
                register_file.clear(def);
         }
         for (unsigned i = 0; i < instr->operands.size(); i++) {
            const Operand& op = instr->operands[i];
            if (op.isTemp() && op.isFirstKill() && op.isLateKill())
               register_file.clear(op);
            if (op.isTemp() && op.physReg().byte() != 0)
               add_subdword_operand(ctx, instr, i, op.physReg().byte(), op.regClass());
         }

         /* emit parallelcopy */
         if (!parallelcopy.empty()) {
            aco_ptr<Pseudo_instruction> pc;
            pc.reset(create_instruction<Pseudo_instruction>(aco_opcode::p_parallelcopy, Format::PSEUDO, parallelcopy.size(), parallelcopy.size()));
            bool temp_in_scc = register_file[scc.reg()];
            bool sgpr_operands_alias_defs = false;
            uint64_t sgpr_operands[4] = {0, 0, 0, 0};
            for (unsigned i = 0; i < parallelcopy.size(); i++) {
               if (temp_in_scc && parallelcopy[i].first.isTemp() && parallelcopy[i].first.getTemp().type() == RegType::sgpr) {
                  if (!sgpr_operands_alias_defs) {
                     unsigned reg = parallelcopy[i].first.physReg().reg();
                     unsigned size = parallelcopy[i].first.getTemp().size();
                     sgpr_operands[reg / 64u] |= u_bit_consecutive64(reg % 64u, size);

                     reg = parallelcopy[i].second.physReg().reg();
                     size = parallelcopy[i].second.getTemp().size();
                     if (sgpr_operands[reg / 64u] & u_bit_consecutive64(reg % 64u, size))
                        sgpr_operands_alias_defs = true;
                  }
               }

               pc->operands[i] = parallelcopy[i].first;
               pc->definitions[i] = parallelcopy[i].second;
               assert(pc->operands[i].size() == pc->definitions[i].size());

               /* it might happen that the operand is already renamed. we have to restore the original name. */
               std::unordered_map<unsigned, Temp>::iterator it = ctx.orig_names.find(pc->operands[i].tempId());
               Temp orig = it != ctx.orig_names.end() ? it->second : pc->operands[i].getTemp();
               ctx.orig_names[pc->definitions[i].tempId()] = orig;
               ctx.renames[block.index][orig.id()] = pc->definitions[i].getTemp();

               std::unordered_map<unsigned, phi_info>::iterator phi = ctx.phi_map.find(pc->operands[i].tempId());
               if (phi != ctx.phi_map.end())
                  phi->second.uses.emplace(pc.get());
            }

            if (temp_in_scc && sgpr_operands_alias_defs) {
               /* disable definitions and re-enable operands */
               RegisterFile tmp_file(register_file);
               for (const Definition& def : instr->definitions) {
                  if (def.isTemp() && !def.isKill())
                     tmp_file.clear(def);
               }
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.isFirstKill())
                     tmp_file.block(op.physReg(), op.regClass());
               }

               handle_pseudo(ctx, tmp_file, pc.get());
            } else {
               pc->tmp_in_scc = false;
            }

            instructions.emplace_back(std::move(pc));
         }

         /* some instructions need VOP3 encoding if operand/definition is not assigned to VCC */
         bool instr_needs_vop3 = !instr->isVOP3() &&
                                 ((instr->format == Format::VOPC && !(instr->definitions[0].physReg() == vcc)) ||
                                  (instr->opcode == aco_opcode::v_cndmask_b32 && !(instr->operands[2].physReg() == vcc)) ||
                                  ((instr->opcode == aco_opcode::v_add_co_u32 ||
                                    instr->opcode == aco_opcode::v_addc_co_u32 ||
                                    instr->opcode == aco_opcode::v_sub_co_u32 ||
                                    instr->opcode == aco_opcode::v_subb_co_u32 ||
                                    instr->opcode == aco_opcode::v_subrev_co_u32 ||
                                    instr->opcode == aco_opcode::v_subbrev_co_u32) &&
                                   !(instr->definitions[1].physReg() == vcc)) ||
                                  ((instr->opcode == aco_opcode::v_addc_co_u32 ||
                                    instr->opcode == aco_opcode::v_subb_co_u32 ||
                                    instr->opcode == aco_opcode::v_subbrev_co_u32) &&
                                   !(instr->operands[2].physReg() == vcc)));
         if (instr_needs_vop3) {

            /* if the first operand is a literal, we have to move it to a reg */
            if (instr->operands.size() && instr->operands[0].isLiteral() && program->chip_class < GFX10) {
               bool can_sgpr = true;
               /* check, if we have to move to vgpr */
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.getTemp().type() == RegType::sgpr) {
                     can_sgpr = false;
                     break;
                  }
               }
               /* disable definitions and re-enable operands */
               RegisterFile tmp_file(register_file);
               for (const Definition& def : instr->definitions)
                  tmp_file.clear(def);
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.isFirstKill())
                     tmp_file.block(op.physReg(), op.regClass());
               }
               Temp tmp = program->allocateTmp(can_sgpr ? s1 : v1);
               ctx.assignments.emplace_back();
               PhysReg reg = get_reg(ctx, tmp_file, tmp, parallelcopy, instr);
               update_renames(ctx, register_file, parallelcopy, instr, true);

               aco_ptr<Instruction> mov;
               if (can_sgpr)
                  mov.reset(create_instruction<SOP1_instruction>(aco_opcode::s_mov_b32, Format::SOP1, 1, 1));
               else
                  mov.reset(create_instruction<VOP1_instruction>(aco_opcode::v_mov_b32, Format::VOP1, 1, 1));
               mov->operands[0] = instr->operands[0];
               mov->definitions[0] = Definition(tmp);
               mov->definitions[0].setFixed(reg);

               instr->operands[0] = Operand(tmp);
               instr->operands[0].setFixed(reg);
               instr->operands[0].setFirstKill(true);

               instructions.emplace_back(std::move(mov));
            }

            /* change the instruction to VOP3 to enable an arbitrary register pair as dst */
            aco_ptr<Instruction> tmp = std::move(instr);
            Format format = asVOP3(tmp->format);
            instr.reset(create_instruction<VOP3A_instruction>(tmp->opcode, format, tmp->operands.size(), tmp->definitions.size()));
            std::copy(tmp->operands.begin(), tmp->operands.end(), instr->operands.begin());
            std::copy(tmp->definitions.begin(), tmp->definitions.end(), instr->definitions.begin());
            update_phi_map(ctx, tmp.get(), instr.get());
         }

         instructions.emplace_back(std::move(*instr_it));

      } /* end for Instr */

      block.instructions = std::move(instructions);

      ctx.filled[block.index] = true;
      for (unsigned succ_idx : block.linear_succs) {
         Block& succ = program->blocks[succ_idx];
         /* seal block if all predecessors are filled */
         bool all_filled = true;
         for (unsigned pred_idx : succ.linear_preds) {
            if (!ctx.filled[pred_idx]) {
               all_filled = false;
               break;
            }
         }
         if (all_filled) {
            ctx.sealed[succ_idx] = true;

            /* finish incomplete phis and check if they became trivial */
            for (Instruction* phi : ctx.incomplete_phis[succ_idx]) {
               std::vector<unsigned> preds = phi->definitions[0].getTemp().is_linear() ? succ.linear_preds : succ.logical_preds;
               for (unsigned i = 0; i < phi->operands.size(); i++) {
                  phi->operands[i].setTemp(read_variable(ctx, phi->operands[i].getTemp(), preds[i]));
                  phi->operands[i].setFixed(ctx.assignments[phi->operands[i].tempId()].reg);
               }
               try_remove_trivial_phi(ctx, phi->definitions[0].getTemp());
            }
            /* complete the original phi nodes, but no need to check triviality */
            for (aco_ptr<Instruction>& instr : succ.instructions) {
               if (!is_phi(instr))
                  break;
               std::vector<unsigned> preds = instr->opcode == aco_opcode::p_phi ? succ.logical_preds : succ.linear_preds;

               for (unsigned i = 0; i < instr->operands.size(); i++) {
                  auto& operand = instr->operands[i];
                  if (!operand.isTemp())
                     continue;
                  operand.setTemp(read_variable(ctx, operand.getTemp(), preds[i]));
                  operand.setFixed(ctx.assignments[operand.tempId()].reg);
                  std::unordered_map<unsigned, phi_info>::iterator phi = ctx.phi_map.find(operand.getTemp().id());
                  if (phi != ctx.phi_map.end())
                     phi->second.uses.emplace(instr.get());
               }
            }
         }
      }
   } /* end for BB */

   /* remove trivial phis */
   for (Block& block : program->blocks) {
      auto end = std::find_if(block.instructions.begin(), block.instructions.end(),
                              [](aco_ptr<Instruction>& instr) { return !is_phi(instr);});
      auto middle = std::remove_if(block.instructions.begin(), end,
                                   [](const aco_ptr<Instruction>& instr) { return instr->definitions.empty();});
      block.instructions.erase(middle, end);
   }

   /* find scc spill registers which may be needed for parallelcopies created by phis */
   for (Block& block : program->blocks) {
      if (block.linear_preds.size() <= 1)
         continue;

      std::bitset<128> regs = sgpr_live_in[block.index];
      if (!regs[127])
         continue;

      /* choose a register */
      int16_t reg = 0;
      for (; reg < ctx.program->max_reg_demand.sgpr && regs[reg]; reg++)
         ;
      assert(reg < ctx.program->max_reg_demand.sgpr);
      adjust_max_used_regs(ctx, s1, reg);

      /* update predecessors */
      for (unsigned& pred_index : block.linear_preds) {
         Block& pred = program->blocks[pred_index];
         pred.scc_live_out = true;
         pred.scratch_sgpr = PhysReg{(uint16_t)reg};
      }
   }

   /* num_gpr = rnd_up(max_used_gpr + 1) */
   program->config->num_vgprs = align(ctx.max_used_vgpr + 1, 4);
   if (program->family == CHIP_TONGA || program->family == CHIP_ICELAND) /* workaround hardware bug */
      program->config->num_sgprs = get_sgpr_alloc(program, program->sgpr_limit);
   else
      program->config->num_sgprs = align(ctx.max_used_sgpr + 1 + get_extra_sgprs(program), 8);
}

}
