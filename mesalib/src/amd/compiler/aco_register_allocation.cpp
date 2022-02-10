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

#include "aco_ir.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace aco {
namespace {

struct ra_ctx;

unsigned get_subdword_operand_stride(chip_class chip, const aco_ptr<Instruction>& instr,
                                     unsigned idx, RegClass rc);
void add_subdword_operand(ra_ctx& ctx, aco_ptr<Instruction>& instr, unsigned idx, unsigned byte,
                          RegClass rc);
std::pair<unsigned, unsigned>
get_subdword_definition_info(Program* program, const aco_ptr<Instruction>& instr, RegClass rc);
void add_subdword_definition(Program* program, aco_ptr<Instruction>& instr, PhysReg reg);

struct assignment {
   PhysReg reg;
   RegClass rc;
   bool assigned = false;
   uint32_t affinity = 0;
   assignment() = default;
   assignment(PhysReg reg_, RegClass rc_) : reg(reg_), rc(rc_), assigned(-1) {}
   void set(const Definition& def)
   {
      assigned = true;
      reg = def.physReg();
      rc = def.regClass();
   }
};

struct ra_ctx {

   Program* program;
   Block* block = NULL;
   std::vector<assignment> assignments;
   std::vector<std::unordered_map<unsigned, Temp>> renames;
   std::vector<uint32_t> loop_header;
   std::unordered_map<unsigned, Temp> orig_names;
   std::unordered_map<unsigned, Instruction*> vectors;
   std::unordered_map<unsigned, Instruction*> split_vectors;
   aco_ptr<Instruction> pseudo_dummy;
   uint16_t max_used_sgpr = 0;
   uint16_t max_used_vgpr = 0;
   uint16_t sgpr_limit;
   uint16_t vgpr_limit;
   std::bitset<512> war_hint;
   std::bitset<64> defs_done; /* see MAX_ARGS in aco_instruction_selection_setup.cpp */

   ra_test_policy policy;

   ra_ctx(Program* program_, ra_test_policy policy_)
       : program(program_), assignments(program->peekAllocationId()),
         renames(program->blocks.size()), policy(policy_)
   {
      pseudo_dummy.reset(
         create_instruction<Instruction>(aco_opcode::p_parallelcopy, Format::PSEUDO, 0, 0));
      sgpr_limit = get_addr_sgpr_from_waves(program, program->min_waves);
      vgpr_limit = get_addr_sgpr_from_waves(program, program->min_waves);
   }
};

/* Iterator type for making PhysRegInterval compatible with range-based for */
struct PhysRegIterator {
   using difference_type = int;
   using value_type = unsigned;
   using reference = const unsigned&;
   using pointer = const unsigned*;
   using iterator_category = std::bidirectional_iterator_tag;

   PhysReg reg;

   PhysReg operator*() const { return reg; }

   PhysRegIterator& operator++()
   {
      reg.reg_b += 4;
      return *this;
   }

   PhysRegIterator& operator--()
   {
      reg.reg_b -= 4;
      return *this;
   }

   bool operator==(PhysRegIterator oth) const { return reg == oth.reg; }

   bool operator!=(PhysRegIterator oth) const { return reg != oth.reg; }

   bool operator<(PhysRegIterator oth) const { return reg < oth.reg; }
};

/* Half-open register interval used in "sliding window"-style for-loops */
struct PhysRegInterval {
   PhysReg lo_;
   unsigned size;

   /* Inclusive lower bound */
   PhysReg lo() const { return lo_; }

   /* Exclusive upper bound */
   PhysReg hi() const { return PhysReg{lo() + size}; }

   PhysRegInterval& operator+=(uint32_t stride)
   {
      lo_ = PhysReg{lo_.reg() + stride};
      return *this;
   }

   bool operator!=(const PhysRegInterval& oth) const { return lo_ != oth.lo_ || size != oth.size; }

   /* Construct a half-open interval, excluding the end register */
   static PhysRegInterval from_until(PhysReg first, PhysReg end) { return {first, end - first}; }

   bool contains(PhysReg reg) const { return lo() <= reg && reg < hi(); }

   bool contains(const PhysRegInterval& needle) const
   {
      return needle.lo() >= lo() && needle.hi() <= hi();
   }

   PhysRegIterator begin() const { return {lo_}; }

   PhysRegIterator end() const { return {PhysReg{lo_ + size}}; }
};

bool
intersects(const PhysRegInterval& a, const PhysRegInterval& b)
{
   return a.hi() > b.lo() && b.hi() > a.lo();
}

/* Gets the stride for full (non-subdword) registers */
uint32_t
get_stride(RegClass rc)
{
   if (rc.type() == RegType::vgpr) {
      return 1;
   } else {
      uint32_t size = rc.size();
      if (size == 2) {
         return 2;
      } else if (size >= 4) {
         return 4;
      } else {
         return 1;
      }
   }
}

PhysRegInterval
get_reg_bounds(Program* program, RegType type)
{
   if (type == RegType::vgpr) {
      return {PhysReg{256}, (unsigned)program->max_reg_demand.vgpr};
   } else {
      return {PhysReg{0}, (unsigned)program->max_reg_demand.sgpr};
   }
}

struct DefInfo {
   PhysRegInterval bounds;
   uint8_t size;
   uint8_t stride;
   RegClass rc;

   DefInfo(ra_ctx& ctx, aco_ptr<Instruction>& instr, RegClass rc_, int operand) : rc(rc_)
   {
      size = rc.size();
      stride = get_stride(rc);

      bounds = get_reg_bounds(ctx.program, rc.type());

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
      } else if (instr->isMIMG() && instr->mimg().d16 && ctx.program->chip_class <= GFX9) {
         /* Workaround GFX9 hardware bug for D16 image instructions: FeatureImageGather4D16Bug
          *
          * The register use is not calculated correctly, and the hardware assumes a
          * full dword per component. Don't use the last registers of the register file.
          * Otherwise, the instruction will be skipped.
          *
          * https://reviews.llvm.org/D81172
          */
         bool imageGather4D16Bug = operand == -1 && rc == v2 && instr->mimg().dmask != 0xF;
         assert(ctx.program->chip_class == GFX9 && "Image D16 on GFX8 not supported.");

         if (imageGather4D16Bug)
            bounds.size -= rc.bytes() / 4;
      }
   }
};

class RegisterFile {
public:
   RegisterFile() { regs.fill(0); }

   std::array<uint32_t, 512> regs;
   std::map<uint32_t, std::array<uint32_t, 4>> subdword_regs;

   const uint32_t& operator[](PhysReg index) const { return regs[index]; }

   uint32_t& operator[](PhysReg index) { return regs[index]; }

   unsigned count_zero(PhysRegInterval reg_interval)
   {
      unsigned res = 0;
      for (PhysReg reg : reg_interval)
         res += !regs[reg];
      return res;
   }

   /* Returns true if any of the bytes in the given range are allocated or blocked */
   bool test(PhysReg start, unsigned num_bytes)
   {
      for (PhysReg i = start; i.reg_b < start.reg_b + num_bytes; i = PhysReg(i + 1)) {
         assert(i <= 511);
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

   void block(PhysReg start, RegClass rc)
   {
      if (rc.is_subdword())
         fill_subdword(start, rc.bytes(), 0xFFFFFFFF);
      else
         fill(start, rc.size(), 0xFFFFFFFF);
   }

   bool is_blocked(PhysReg start)
   {
      if (regs[start] == 0xFFFFFFFF)
         return true;
      if (regs[start] == 0xF0000000) {
         for (unsigned i = start.byte(); i < 4; i++)
            if (subdword_regs[start][i] == 0xFFFFFFFF)
               return true;
      }
      return false;
   }

   bool is_empty_or_blocked(PhysReg start)
   {
      /* Empty is 0, blocked is 0xFFFFFFFF, so to check both we compare the
       * incremented value to 1 */
      if (regs[start] == 0xF0000000) {
         return subdword_regs[start][start.byte()] + 1 <= 1;
      }
      return regs[start] + 1 <= 1;
   }

   void clear(PhysReg start, RegClass rc)
   {
      if (rc.is_subdword())
         fill_subdword(start, rc.bytes(), 0);
      else
         fill(start, rc.size(), 0);
   }

   void fill(Operand op)
   {
      if (op.regClass().is_subdword())
         fill_subdword(op.physReg(), op.bytes(), op.tempId());
      else
         fill(op.physReg(), op.size(), op.tempId());
   }

   void clear(Operand op) { clear(op.physReg(), op.regClass()); }

   void fill(Definition def)
   {
      if (def.regClass().is_subdword())
         fill_subdword(def.physReg(), def.bytes(), def.tempId());
      else
         fill(def.physReg(), def.size(), def.tempId());
   }

   void clear(Definition def) { clear(def.physReg(), def.regClass()); }

   unsigned get_id(PhysReg reg)
   {
      return regs[reg] == 0xF0000000 ? subdword_regs[reg][reg.byte()] : regs[reg];
   }

private:
   void fill(PhysReg start, unsigned size, uint32_t val)
   {
      for (unsigned i = 0; i < size; i++)
         regs[start + i] = val;
   }

   void fill_subdword(PhysReg start, unsigned num_bytes, uint32_t val)
   {
      fill(start, DIV_ROUND_UP(num_bytes, 4), 0xF0000000);
      for (PhysReg i = start; i.reg_b < start.reg_b + num_bytes; i = PhysReg(i + 1)) {
         /* emplace or get */
         std::array<uint32_t, 4>& sub =
            subdword_regs.emplace(i, std::array<uint32_t, 4>{0, 0, 0, 0}).first->second;
         for (unsigned j = i.byte(); i * 4 + j < start.reg_b + num_bytes && j < 4; j++)
            sub[j] = val;

         if (sub == std::array<uint32_t, 4>{0, 0, 0, 0}) {
            subdword_regs.erase(i);
            regs[i] = 0;
         }
      }
   }
};

std::set<std::pair<unsigned, unsigned>> find_vars(ra_ctx& ctx, RegisterFile& reg_file,
                                                  const PhysRegInterval reg_interval);

/* helper function for debugging */
UNUSED void
print_reg(const RegisterFile& reg_file, PhysReg reg, bool has_adjacent_variable)
{
   if (reg_file[reg] == 0xFFFFFFFF) {
      printf("☐");
   } else if (reg_file[reg]) {
      const bool show_subdword_alloc = (reg_file[reg] == 0xF0000000);
      if (show_subdword_alloc) {
         const char* block_chars[] = {
            // clang-format off
            "?", "▘", "▝", "▀",
            "▖", "▌", "▞", "▛",
            "▗", "▚", "▐", "▜",
            "▄", "▙", "▟", "▉"
            // clang-format on
         };
         unsigned index = 0;
         for (int i = 0; i < 4; ++i) {
            if (reg_file.subdword_regs.at(reg)[i]) {
               index |= 1 << i;
            }
         }
         printf("%s", block_chars[index]);
      } else {
         /* Indicate filled register slot */
         if (!has_adjacent_variable) {
            printf("█");
         } else {
            /* Use a slightly shorter box to leave a small gap between adjacent variables */
            printf("▉");
         }
      }
   } else {
      printf("·");
   }
}

/* helper function for debugging */
UNUSED void
print_regs(ra_ctx& ctx, bool vgprs, RegisterFile& reg_file)
{
   PhysRegInterval regs = get_reg_bounds(ctx.program, vgprs ? RegType::vgpr : RegType::sgpr);
   char reg_char = vgprs ? 'v' : 's';
   const int max_regs_per_line = 64;

   /* print markers */
   printf("       ");
   for (int i = 0; i < std::min<int>(max_regs_per_line, ROUND_DOWN_TO(regs.size, 4)); i += 4) {
      printf("%-3.2u ", i);
   }
   printf("\n");

   /* print usage */
   auto line_begin_it = regs.begin();
   while (line_begin_it != regs.end()) {
      const int regs_in_line =
         std::min<int>(max_regs_per_line, std::distance(line_begin_it, regs.end()));

      if (line_begin_it == regs.begin()) {
         printf("%cgprs: ", reg_char);
      } else {
         printf("  %+4d ", std::distance(regs.begin(), line_begin_it));
      }
      const auto line_end_it = std::next(line_begin_it, regs_in_line);

      for (auto reg_it = line_begin_it; reg_it != line_end_it; ++reg_it) {
         bool has_adjacent_variable =
            (std::next(reg_it) != line_end_it &&
             reg_file[*reg_it] != reg_file[*std::next(reg_it)] && reg_file[*std::next(reg_it)]);
         print_reg(reg_file, *reg_it, has_adjacent_variable);
      }

      line_begin_it = line_end_it;
      printf("\n");
   }

   const unsigned free_regs =
      std::count_if(regs.begin(), regs.end(), [&](auto reg) { return !reg_file[reg]; });
   printf("%u/%u used, %u/%u free\n", regs.size - free_regs, regs.size, free_regs, regs.size);

   /* print assignments ordered by registers */
   std::map<PhysReg, std::pair<unsigned, unsigned>>
      regs_to_vars; /* maps to byte size and temp id */
   for (const auto& size_id : find_vars(ctx, reg_file, regs)) {
      auto reg = ctx.assignments[size_id.second].reg;
      ASSERTED auto inserted = regs_to_vars.emplace(reg, size_id);
      assert(inserted.second);
   }

   for (const auto& reg_and_var : regs_to_vars) {
      const auto& first_reg = reg_and_var.first;
      const auto& size_id = reg_and_var.second;

      printf("%%%u ", size_id.second);
      if (ctx.orig_names.count(size_id.second) &&
          ctx.orig_names[size_id.second].id() != size_id.second) {
         printf("(was %%%d) ", ctx.orig_names[size_id.second].id());
      }
      printf("= %c[%d", reg_char, first_reg.reg() - regs.lo());
      PhysReg last_reg = first_reg.advance(size_id.first - 1);
      if (first_reg.reg() != last_reg.reg()) {
         assert(first_reg.byte() == 0 && last_reg.byte() == 3);
         printf("-%d", last_reg.reg() - regs.lo());
      }
      printf("]");
      if (first_reg.byte() != 0 || last_reg.byte() != 3) {
         printf("[%d:%d]", first_reg.byte() * 8, (last_reg.byte() + 1) * 8);
      }
      printf("\n");
   }
}

unsigned
get_subdword_operand_stride(chip_class chip, const aco_ptr<Instruction>& instr, unsigned idx,
                            RegClass rc)
{
   if (instr->isPseudo()) {
      /* v_readfirstlane_b32 cannot use SDWA */
      if (instr->opcode == aco_opcode::p_as_uniform)
         return 4;
      else if (chip >= GFX8)
         return rc.bytes() % 2 == 0 ? 2 : 1;
      else
         return 4;
   }

   assert(rc.bytes() <= 2);
   if (instr->isVALU()) {
      if (can_use_SDWA(chip, instr, false))
         return rc.bytes();
      if (can_use_opsel(chip, instr->opcode, idx, true))
         return 2;
      if (instr->format == Format::VOP3P)
         return 2;
   }

   switch (instr->opcode) {
   case aco_opcode::v_cvt_f32_ubyte0: return 1;
   case aco_opcode::ds_write_b8:
   case aco_opcode::ds_write_b16: return chip >= GFX9 ? 2 : 4;
   case aco_opcode::buffer_store_byte:
   case aco_opcode::buffer_store_short:
   case aco_opcode::buffer_store_format_d16_x:
   case aco_opcode::flat_store_byte:
   case aco_opcode::flat_store_short:
   case aco_opcode::scratch_store_byte:
   case aco_opcode::scratch_store_short:
   case aco_opcode::global_store_byte:
   case aco_opcode::global_store_short: return chip >= GFX9 ? 2 : 4;
   default: return 4;
   }
}

void
add_subdword_operand(ra_ctx& ctx, aco_ptr<Instruction>& instr, unsigned idx, unsigned byte,
                     RegClass rc)
{
   chip_class chip = ctx.program->chip_class;
   if (instr->isPseudo() || byte == 0)
      return;

   assert(rc.bytes() <= 2);
   if (instr->isVALU()) {
      /* check if we can use opsel */
      if (instr->format == Format::VOP3) {
         assert(byte == 2);
         instr->vop3().opsel |= 1 << idx;
         return;
      }
      if (instr->isVOP3P()) {
         assert(byte == 2 && !(instr->vop3p().opsel_lo & (1 << idx)));
         instr->vop3p().opsel_lo |= 1 << idx;
         instr->vop3p().opsel_hi |= 1 << idx;
         return;
      }
      if (instr->opcode == aco_opcode::v_cvt_f32_ubyte0) {
         switch (byte) {
         case 0: instr->opcode = aco_opcode::v_cvt_f32_ubyte0; break;
         case 1: instr->opcode = aco_opcode::v_cvt_f32_ubyte1; break;
         case 2: instr->opcode = aco_opcode::v_cvt_f32_ubyte2; break;
         case 3: instr->opcode = aco_opcode::v_cvt_f32_ubyte3; break;
         }
         return;
      }

      /* use SDWA */
      assert(can_use_SDWA(chip, instr, false));
      convert_to_SDWA(chip, instr);
      return;
   }

   assert(byte == 2);
   if (instr->opcode == aco_opcode::ds_write_b8)
      instr->opcode = aco_opcode::ds_write_b8_d16_hi;
   else if (instr->opcode == aco_opcode::ds_write_b16)
      instr->opcode = aco_opcode::ds_write_b16_d16_hi;
   else if (instr->opcode == aco_opcode::buffer_store_byte)
      instr->opcode = aco_opcode::buffer_store_byte_d16_hi;
   else if (instr->opcode == aco_opcode::buffer_store_short)
      instr->opcode = aco_opcode::buffer_store_short_d16_hi;
   else if (instr->opcode == aco_opcode::buffer_store_format_d16_x)
      instr->opcode = aco_opcode::buffer_store_format_d16_hi_x;
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
   return;
}

/* minimum_stride, bytes_written */
std::pair<unsigned, unsigned>
get_subdword_definition_info(Program* program, const aco_ptr<Instruction>& instr, RegClass rc)
{
   chip_class chip = program->chip_class;

   if (instr->isPseudo()) {
      if (chip >= GFX8)
         return std::make_pair(rc.bytes() % 2 == 0 ? 2 : 1, rc.bytes());
      else
         return std::make_pair(4, rc.size() * 4u);
   }

   if (instr->isVALU() || instr->isVINTRP()) {
      assert(rc.bytes() <= 2);

      if (can_use_SDWA(chip, instr, false))
         return std::make_pair(rc.bytes(), rc.bytes());

      unsigned bytes_written = 4u;
      if (instr_is_16bit(chip, instr->opcode))
         bytes_written = 2u;

      unsigned stride = 4u;
      if (instr->opcode == aco_opcode::v_fma_mixlo_f16 ||
          can_use_opsel(chip, instr->opcode, -1, true))
         stride = 2u;

      return std::make_pair(stride, bytes_written);
   }

   switch (instr->opcode) {
   /* D16 loads with _hi version */
   case aco_opcode::ds_read_u8_d16:
   case aco_opcode::ds_read_i8_d16:
   case aco_opcode::ds_read_u16_d16:
   case aco_opcode::flat_load_ubyte_d16:
   case aco_opcode::flat_load_sbyte_d16:
   case aco_opcode::flat_load_short_d16:
   case aco_opcode::global_load_ubyte_d16:
   case aco_opcode::global_load_sbyte_d16:
   case aco_opcode::global_load_short_d16:
   case aco_opcode::scratch_load_ubyte_d16:
   case aco_opcode::scratch_load_sbyte_d16:
   case aco_opcode::scratch_load_short_d16:
   case aco_opcode::buffer_load_ubyte_d16:
   case aco_opcode::buffer_load_sbyte_d16:
   case aco_opcode::buffer_load_short_d16:
   case aco_opcode::buffer_load_format_d16_x: {
      assert(chip >= GFX9);
      if (!program->dev.sram_ecc_enabled)
         return std::make_pair(2u, 2u);
      else
         return std::make_pair(2u, 4u);
   }
   /* 3-component D16 loads */
   case aco_opcode::buffer_load_format_d16_xyz:
   case aco_opcode::tbuffer_load_format_d16_xyz: {
      assert(chip >= GFX9);
      if (!program->dev.sram_ecc_enabled)
         return std::make_pair(4u, 6u);
      break;
   }

   default: break;
   }

   if (instr->isMIMG() && instr->mimg().d16 && !program->dev.sram_ecc_enabled) {
      assert(chip >= GFX9);
      return std::make_pair(4u, rc.bytes());
   }

   return std::make_pair(4, rc.size() * 4u);
}

void
add_subdword_definition(Program* program, aco_ptr<Instruction>& instr, PhysReg reg)
{
   if (instr->isPseudo())
      return;

   if (instr->isVALU()) {
      chip_class chip = program->chip_class;
      assert(instr->definitions[0].bytes() <= 2);

      if (reg.byte() == 0 && instr_is_16bit(chip, instr->opcode))
         return;

      /* check if we can use opsel */
      if (instr->format == Format::VOP3) {
         assert(reg.byte() == 2);
         assert(can_use_opsel(chip, instr->opcode, -1, true));
         instr->vop3().opsel |= (1 << 3); /* dst in high half */
         return;
      }

      if (instr->opcode == aco_opcode::v_fma_mixlo_f16) {
         instr->opcode = aco_opcode::v_fma_mixhi_f16;
         return;
      }

      /* use SDWA */
      assert(can_use_SDWA(chip, instr, false));
      convert_to_SDWA(chip, instr);
      return;
   }

   if (reg.byte() == 0)
      return;
   else if (instr->opcode == aco_opcode::buffer_load_ubyte_d16)
      instr->opcode = aco_opcode::buffer_load_ubyte_d16_hi;
   else if (instr->opcode == aco_opcode::buffer_load_sbyte_d16)
      instr->opcode = aco_opcode::buffer_load_sbyte_d16_hi;
   else if (instr->opcode == aco_opcode::buffer_load_short_d16)
      instr->opcode = aco_opcode::buffer_load_short_d16_hi;
   else if (instr->opcode == aco_opcode::buffer_load_format_d16_x)
      instr->opcode = aco_opcode::buffer_load_format_d16_hi_x;
   else if (instr->opcode == aco_opcode::flat_load_ubyte_d16)
      instr->opcode = aco_opcode::flat_load_ubyte_d16_hi;
   else if (instr->opcode == aco_opcode::flat_load_sbyte_d16)
      instr->opcode = aco_opcode::flat_load_sbyte_d16_hi;
   else if (instr->opcode == aco_opcode::flat_load_short_d16)
      instr->opcode = aco_opcode::flat_load_short_d16_hi;
   else if (instr->opcode == aco_opcode::scratch_load_ubyte_d16)
      instr->opcode = aco_opcode::scratch_load_ubyte_d16_hi;
   else if (instr->opcode == aco_opcode::scratch_load_sbyte_d16)
      instr->opcode = aco_opcode::scratch_load_sbyte_d16_hi;
   else if (instr->opcode == aco_opcode::scratch_load_short_d16)
      instr->opcode = aco_opcode::scratch_load_short_d16_hi;
   else if (instr->opcode == aco_opcode::global_load_ubyte_d16)
      instr->opcode = aco_opcode::global_load_ubyte_d16_hi;
   else if (instr->opcode == aco_opcode::global_load_sbyte_d16)
      instr->opcode = aco_opcode::global_load_sbyte_d16_hi;
   else if (instr->opcode == aco_opcode::global_load_short_d16)
      instr->opcode = aco_opcode::global_load_short_d16_hi;
   else if (instr->opcode == aco_opcode::ds_read_u8_d16)
      instr->opcode = aco_opcode::ds_read_u8_d16_hi;
   else if (instr->opcode == aco_opcode::ds_read_i8_d16)
      instr->opcode = aco_opcode::ds_read_i8_d16_hi;
   else if (instr->opcode == aco_opcode::ds_read_u16_d16)
      instr->opcode = aco_opcode::ds_read_u16_d16_hi;
   else
      unreachable("Something went wrong: Impossible register assignment.");
}

void
adjust_max_used_regs(ra_ctx& ctx, RegClass rc, unsigned reg)
{
   uint16_t max_addressible_sgpr = ctx.sgpr_limit;
   unsigned size = rc.size();
   if (rc.type() == RegType::vgpr) {
      assert(reg >= 256);
      uint16_t hi = reg - 256 + size - 1;
      ctx.max_used_vgpr = std::max(ctx.max_used_vgpr, hi);
   } else if (reg + rc.size() <= max_addressible_sgpr) {
      uint16_t hi = reg + size - 1;
      ctx.max_used_sgpr = std::max(ctx.max_used_sgpr, std::min(hi, max_addressible_sgpr));
   }
}

enum UpdateRenames {
   rename_not_killed_ops = 0x1,
   fill_killed_ops = 0x2,
};
MESA_DEFINE_CPP_ENUM_BITFIELD_OPERATORS(UpdateRenames);

void
update_renames(ra_ctx& ctx, RegisterFile& reg_file,
               std::vector<std::pair<Operand, Definition>>& parallelcopies,
               aco_ptr<Instruction>& instr, UpdateRenames flags)
{
   /* clear operands */
   for (std::pair<Operand, Definition>& copy : parallelcopies) {
      /* the definitions with id are not from this function and already handled */
      if (copy.second.isTemp())
         continue;
      reg_file.clear(copy.first);
   }

   /* allocate id's and rename operands: this is done transparently here */
   auto it = parallelcopies.begin();
   while (it != parallelcopies.end()) {
      if (it->second.isTemp()) {
         ++it;
         continue;
      }

      /* check if we moved a definition: change the register and remove copy */
      bool is_def = false;
      for (Definition& def : instr->definitions) {
         if (def.isTemp() && def.getTemp() == it->first.getTemp()) {
            // FIXME: ensure that the definition can use this reg
            def.setFixed(it->second.physReg());
            reg_file.fill(def);
            ctx.assignments[def.tempId()].reg = def.physReg();
            it = parallelcopies.erase(it);
            is_def = true;
            break;
         }
      }
      if (is_def)
         continue;

      /* check if we moved another parallelcopy definition */
      for (std::pair<Operand, Definition>& other : parallelcopies) {
         if (!other.second.isTemp())
            continue;
         if (it->first.getTemp() == other.second.getTemp()) {
            other.second.setFixed(it->second.physReg());
            ctx.assignments[other.second.tempId()].reg = other.second.physReg();
            it = parallelcopies.erase(it);
            is_def = true;
            /* check if we moved an operand, again */
            bool fill = true;
            for (Operand& op : instr->operands) {
               if (op.isTemp() && op.tempId() == other.second.tempId()) {
                  // FIXME: ensure that the operand can use this reg
                  op.setFixed(other.second.physReg());
                  fill = (flags & fill_killed_ops) || !op.isKillBeforeDef();
               }
            }
            if (fill)
               reg_file.fill(other.second);
            break;
         }
      }
      if (is_def)
         continue;

      std::pair<Operand, Definition>& copy = *it;
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
            bool omit_renaming = !(flags & rename_not_killed_ops) && !op.isKillBeforeDef();
            for (std::pair<Operand, Definition>& pc : parallelcopies) {
               PhysReg def_reg = pc.second.physReg();
               omit_renaming &= def_reg > copy.first.physReg()
                                   ? (copy.first.physReg() + copy.first.size() <= def_reg.reg())
                                   : (def_reg + pc.second.size() <= copy.first.physReg().reg());
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

            fill = (flags & fill_killed_ops) || !op.isKillBeforeDef();
         }
      }

      if (fill)
         reg_file.fill(copy.second);

      ++it;
   }
}

std::pair<PhysReg, bool>
get_reg_simple(ra_ctx& ctx, RegisterFile& reg_file, DefInfo info)
{
   const PhysRegInterval& bounds = info.bounds;
   uint32_t size = info.size;
   uint32_t stride = info.rc.is_subdword() ? DIV_ROUND_UP(info.stride, 4) : info.stride;
   RegClass rc = info.rc;

   DefInfo new_info = info;
   new_info.rc = RegClass(rc.type(), size);
   for (unsigned new_stride = 16; new_stride > stride; new_stride /= 2) {
      if (size % new_stride)
         continue;
      new_info.stride = new_stride;
      std::pair<PhysReg, bool> res = get_reg_simple(ctx, reg_file, new_info);
      if (res.second)
         return res;
   }

   auto is_free = [&](PhysReg reg_index)
   { return reg_file[reg_index] == 0 && !ctx.war_hint[reg_index]; };

   if (stride == 1) {
      /* best fit algorithm: find the smallest gap to fit in the variable */
      PhysRegInterval best_gap{PhysReg{0}, UINT_MAX};
      const unsigned max_gpr =
         (rc.type() == RegType::vgpr) ? (256 + ctx.max_used_vgpr) : ctx.max_used_sgpr;

      PhysRegIterator reg_it = bounds.begin();
      const PhysRegIterator end_it =
         std::min(bounds.end(), std::max(PhysRegIterator{PhysReg{max_gpr + 1}}, reg_it));
      while (reg_it != bounds.end()) {
         /* Find the next chunk of available register slots */
         reg_it = std::find_if(reg_it, end_it, is_free);
         auto next_nonfree_it = std::find_if_not(reg_it, end_it, is_free);
         if (reg_it == bounds.end()) {
            break;
         }

         if (next_nonfree_it == end_it) {
            /* All registers past max_used_gpr are free */
            next_nonfree_it = bounds.end();
         }

         PhysRegInterval gap = PhysRegInterval::from_until(*reg_it, *next_nonfree_it);

         /* early return on exact matches */
         if (size == gap.size) {
            adjust_max_used_regs(ctx, rc, gap.lo());
            return {gap.lo(), true};
         }

         /* check if it fits and the gap size is smaller */
         if (size < gap.size && gap.size < best_gap.size) {
            best_gap = gap;
         }

         /* Move past the processed chunk */
         reg_it = next_nonfree_it;
      }

      if (best_gap.size == UINT_MAX)
         return {{}, false};

      /* find best position within gap by leaving a good stride for other variables*/
      unsigned buffer = best_gap.size - size;
      if (buffer > 1) {
         if (((best_gap.lo() + size) % 8 != 0 && (best_gap.lo() + buffer) % 8 == 0) ||
             ((best_gap.lo() + size) % 4 != 0 && (best_gap.lo() + buffer) % 4 == 0) ||
             ((best_gap.lo() + size) % 2 != 0 && (best_gap.lo() + buffer) % 2 == 0))
            best_gap = {PhysReg{best_gap.lo() + buffer}, best_gap.size - buffer};
      }

      adjust_max_used_regs(ctx, rc, best_gap.lo());
      return {best_gap.lo(), true};
   }

   for (PhysRegInterval reg_win = {bounds.lo(), size}; reg_win.hi() <= bounds.hi();
        reg_win += stride) {
      if (reg_file[reg_win.lo()] != 0) {
         continue;
      }

      bool is_valid = std::all_of(std::next(reg_win.begin()), reg_win.end(), is_free);
      if (is_valid) {
         adjust_max_used_regs(ctx, rc, reg_win.lo());
         return {reg_win.lo(), true};
      }
   }

   /* do this late because using the upper bytes of a register can require
    * larger instruction encodings or copies
    * TODO: don't do this in situations where it doesn't benefit */
   if (rc.is_subdword()) {
      for (std::pair<const uint32_t, std::array<uint32_t, 4>>& entry : reg_file.subdword_regs) {
         assert(reg_file[PhysReg{entry.first}] == 0xF0000000);
         if (!bounds.contains({PhysReg{entry.first}, rc.size()}))
            continue;

         for (unsigned i = 0; i < 4; i += info.stride) {
            /* check if there's a block of free bytes large enough to hold the register */
            bool reg_found =
               std::all_of(&entry.second[i], &entry.second[std::min(4u, i + rc.bytes())],
                           [](unsigned v) { return v == 0; });

            /* check if also the neighboring reg is free if needed */
            if (reg_found && i + rc.bytes() > 4)
               reg_found = (reg_file[PhysReg{entry.first + 1}] == 0);

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
std::set<std::pair<unsigned, unsigned>>
find_vars(ra_ctx& ctx, RegisterFile& reg_file, const PhysRegInterval reg_interval)
{
   std::set<std::pair<unsigned, unsigned>> vars;
   for (PhysReg j : reg_interval) {
      if (reg_file.is_blocked(j))
         continue;
      if (reg_file[j] == 0xF0000000) {
         for (unsigned k = 0; k < 4; k++) {
            unsigned id = reg_file.subdword_regs[j][k];
            if (id) {
               assignment& var = ctx.assignments[id];
               vars.emplace(var.rc.bytes(), id);
            }
         }
      } else if (reg_file[j] != 0) {
         unsigned id = reg_file[j];
         assignment& var = ctx.assignments[id];
         vars.emplace(var.rc.bytes(), id);
      }
   }
   return vars;
}

/* collect variables from a register area and clear reg_file */
std::set<std::pair<unsigned, unsigned>>
collect_vars(ra_ctx& ctx, RegisterFile& reg_file, const PhysRegInterval reg_interval)
{
   std::set<std::pair<unsigned, unsigned>> vars = find_vars(ctx, reg_file, reg_interval);
   for (std::pair<unsigned, unsigned> size_id : vars) {
      assignment& var = ctx.assignments[size_id.second];
      reg_file.clear(var.reg, var.rc);
   }
   return vars;
}

bool
get_regs_for_copies(ra_ctx& ctx, RegisterFile& reg_file,
                    std::vector<std::pair<Operand, Definition>>& parallelcopies,
                    const std::set<std::pair<unsigned, unsigned>>& vars,
                    const PhysRegInterval bounds, aco_ptr<Instruction>& instr,
                    const PhysRegInterval def_reg)
{
   /* variables are sorted from small sized to large */
   /* NOTE: variables are also sorted by ID. this only affects a very small number of shaders
    * slightly though. */
   for (std::set<std::pair<unsigned, unsigned>>::const_reverse_iterator it = vars.rbegin();
        it != vars.rend(); ++it) {
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
            PhysReg reg(def_reg.lo());
            for (unsigned i = 0; i < instr->operands.size(); i++) {
               if (instr->operands[i].isTemp() && instr->operands[i].tempId() == id) {
                  res = {reg, (!var.rc.is_subdword() || (reg.byte() % info.stride == 0)) &&
                                 !reg_file.test(reg, var.rc.bytes())};
                  break;
               }
               reg.reg_b += instr->operands[i].bytes();
            }
            if (!res.second)
               res = {var.reg, !reg_file.test(var.reg, var.rc.bytes())};
         } else {
            info.bounds = def_reg;
            res = get_reg_simple(ctx, reg_file, info);
         }
      } else {
         /* Try to find space within the bounds but outside of the definition */
         info.bounds = PhysRegInterval::from_until(bounds.lo(), MIN2(def_reg.lo(), bounds.hi()));
         res = get_reg_simple(ctx, reg_file, info);
         if (!res.second && def_reg.hi() <= bounds.hi()) {
            unsigned lo = (def_reg.hi() + info.stride - 1) & ~(info.stride - 1);
            info.bounds = PhysRegInterval::from_until(PhysReg{lo}, bounds.hi());
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

      PhysReg best_pos = bounds.lo();
      unsigned num_moves = 0xFF;
      unsigned num_vars = 0;

      /* we use a sliding window to find potential positions */
      unsigned stride = var.rc.is_subdword() ? 1 : info.stride;
      for (PhysRegInterval reg_win{bounds.lo(), size}; reg_win.hi() <= bounds.hi();
           reg_win += stride) {
         if (!is_dead_operand && intersects(reg_win, def_reg))
            continue;

         /* second, check that we have at most k=num_moves elements in the window
          * and no element is larger than the currently processed one */
         unsigned k = 0;
         unsigned n = 0;
         unsigned last_var = 0;
         bool found = true;
         for (PhysReg j : reg_win) {
            if (reg_file[j] == 0 || reg_file[j] == last_var)
               continue;

            if (reg_file.is_blocked(j) || k > num_moves) {
               found = false;
               break;
            }
            if (reg_file[j] == 0xF0000000) {
               k += 1;
               n++;
               continue;
            }
            /* we cannot split live ranges of linear vgprs inside control flow */
            if (!(ctx.block->kind & block_kind_top_level) &&
                ctx.assignments[reg_file[j]].rc.is_linear_vgpr()) {
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
            best_pos = reg_win.lo();
            num_moves = k;
            num_vars = n;
         }
      }

      /* FIXME: we messed up and couldn't find space for the variables to be copied */
      if (num_moves == 0xFF)
         return false;

      PhysRegInterval reg_win{best_pos, size};

      /* collect variables and block reg file */
      std::set<std::pair<unsigned, unsigned>> new_vars = collect_vars(ctx, reg_file, reg_win);

      /* mark the area as blocked */
      reg_file.block(reg_win.lo(), var.rc);
      adjust_max_used_regs(ctx, var.rc, reg_win.lo());

      if (!get_regs_for_copies(ctx, reg_file, parallelcopies, new_vars, bounds, instr, def_reg))
         return false;

      /* create parallelcopy pair (without definition id) */
      Temp tmp = Temp(id, var.rc);
      Operand pc_op = Operand(tmp);
      pc_op.setFixed(var.reg);
      Definition pc_def = Definition(reg_win.lo(), pc_op.regClass());
      parallelcopies.emplace_back(pc_op, pc_def);
   }

   return true;
}

std::pair<PhysReg, bool>
get_reg_impl(ra_ctx& ctx, RegisterFile& reg_file,
             std::vector<std::pair<Operand, Definition>>& parallelcopies, const DefInfo& info,
             aco_ptr<Instruction>& instr)
{
   const PhysRegInterval& bounds = info.bounds;
   uint32_t size = info.size;
   uint32_t stride = info.stride;
   RegClass rc = info.rc;

   /* check how many free regs we have */
   unsigned regs_free = reg_file.count_zero(bounds);

   /* mark and count killed operands */
   unsigned killed_ops = 0;
   std::bitset<256> is_killed_operand; /* per-register */
   for (unsigned j = 0; !is_phi(instr) && j < instr->operands.size(); j++) {
      Operand& op = instr->operands[j];
      if (op.isTemp() && op.isFirstKillBeforeDef() && bounds.contains(op.physReg()) &&
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
   PhysRegInterval best_win = {bounds.lo(), size};
   unsigned num_moves = 0xFF;
   unsigned num_vars = 0;

   /* we use a sliding window to check potential positions */
   for (PhysRegInterval reg_win = {bounds.lo(), size}; reg_win.hi() <= bounds.hi();
        reg_win += stride) {
      /* first check if the register window starts in the middle of an
       * allocated variable: this is what we have to fix to allow for
       * num_moves > size */
      if (reg_win.lo() > bounds.lo() && !reg_file.is_empty_or_blocked(reg_win.lo()) &&
          reg_file.get_id(reg_win.lo()) == reg_file.get_id(reg_win.lo().advance(-1)))
         continue;
      if (reg_win.hi() < bounds.hi() && !reg_file.is_empty_or_blocked(reg_win.hi().advance(-1)) &&
          reg_file.get_id(reg_win.hi().advance(-1)) == reg_file.get_id(reg_win.hi()))
         continue;

      /* second, check that we have at most k=num_moves elements in the window
       * and no element is larger than the currently processed one */
      unsigned k = op_moves;
      unsigned n = 0;
      unsigned remaining_op_moves = op_moves;
      unsigned last_var = 0;
      bool found = true;
      bool aligned = rc == RegClass::v4 && reg_win.lo() % 4 == 0;
      for (const PhysReg j : reg_win) {
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

         /* we cannot split live ranges of linear vgprs inside control flow */
         // TODO: ensure that live range splits inside control flow are never necessary
         if (!(ctx.block->kind & block_kind_top_level) &&
             ctx.assignments[reg_file[j]].rc.is_linear_vgpr()) {
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
         best_win = reg_win;
         num_moves = k;
         num_vars = n;
      }
   }

   if (num_moves == 0xFF)
      return {{}, false};

   /* now, we figured the placement for our definition */
   RegisterFile tmp_file(reg_file);
   std::set<std::pair<unsigned, unsigned>> vars = collect_vars(ctx, tmp_file, best_win);

   if (instr->opcode == aco_opcode::p_create_vector) {
      /* move killed operands which aren't yet at the correct position (GFX9+)
       * or which are in the definition space */
      PhysReg reg = best_win.lo();
      for (Operand& op : instr->operands) {
         if (op.isTemp() && op.isFirstKillBeforeDef() && op.getTemp().type() == rc.type()) {
            if (op.physReg() != reg && (ctx.program->chip_class >= GFX9 ||
                                        (op.physReg().advance(op.bytes()) > best_win.lo() &&
                                         op.physReg() < best_win.hi()))) {
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
   if (!get_regs_for_copies(ctx, tmp_file, pc, vars, bounds, instr, best_win))
      return {{}, false};

   parallelcopies.insert(parallelcopies.end(), pc.begin(), pc.end());

   adjust_max_used_regs(ctx, rc, best_win.lo());
   return {best_win.lo(), true};
}

bool
get_reg_specified(ra_ctx& ctx, RegisterFile& reg_file, RegClass rc, aco_ptr<Instruction>& instr,
                  PhysReg reg)
{
   /* catch out-of-range registers */
   if (reg >= PhysReg{512})
      return false;

   std::pair<unsigned, unsigned> sdw_def_info;
   if (rc.is_subdword())
      sdw_def_info = get_subdword_definition_info(ctx.program, instr, rc);

   if (rc.is_subdword() && reg.byte() % sdw_def_info.first)
      return false;
   if (!rc.is_subdword() && reg.byte())
      return false;

   if (rc.type() == RegType::sgpr && reg % get_stride(rc) != 0)
      return false;

   PhysRegInterval reg_win = {reg, rc.size()};
   PhysRegInterval bounds = get_reg_bounds(ctx.program, rc.type());
   PhysRegInterval vcc_win = {vcc, 2};
   /* VCC is outside the bounds */
   bool is_vcc = rc.type() == RegType::sgpr && vcc_win.contains(reg_win);
   bool is_m0 = rc == s1 && reg == m0;
   if (!bounds.contains(reg_win) && !is_vcc && !is_m0)
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

   adjust_max_used_regs(ctx, rc, reg_win.lo());
   return true;
}

bool
increase_register_file(ra_ctx& ctx, RegType type)
{
   if (type == RegType::vgpr && ctx.program->max_reg_demand.vgpr < ctx.vgpr_limit) {
      update_vgpr_sgpr_demand(ctx.program, RegisterDemand(ctx.program->max_reg_demand.vgpr + 1,
                                                          ctx.program->max_reg_demand.sgpr));
   } else if (type == RegType::sgpr && ctx.program->max_reg_demand.sgpr < ctx.sgpr_limit) {
      update_vgpr_sgpr_demand(ctx.program, RegisterDemand(ctx.program->max_reg_demand.vgpr,
                                                          ctx.program->max_reg_demand.sgpr + 1));
   } else {
      return false;
   }
   return true;
}

struct IDAndRegClass {
   IDAndRegClass(unsigned id_, RegClass rc_) : id(id_), rc(rc_) {}

   unsigned id;
   RegClass rc;
};

struct IDAndInfo {
   IDAndInfo(unsigned id_, DefInfo info_) : id(id_), info(info_) {}

   unsigned id;
   DefInfo info;
};

/* Reallocates vars by sorting them and placing each variable after the previous
 * one. If one of the variables has 0xffffffff as an ID, the register assigned
 * for that variable will be returned.
 */
PhysReg
compact_relocate_vars(ra_ctx& ctx, const std::vector<IDAndRegClass>& vars,
                      std::vector<std::pair<Operand, Definition>>& parallelcopies, PhysReg start)
{
   /* This function assumes RegisterDemand/live_var_analysis rounds up sub-dword
    * temporary sizes to dwords.
    */
   std::vector<IDAndInfo> sorted;
   for (IDAndRegClass var : vars) {
      DefInfo info(ctx, ctx.pseudo_dummy, var.rc, -1);
      sorted.emplace_back(var.id, info);
   }

   std::sort(
      sorted.begin(), sorted.end(),
      [&ctx](const IDAndInfo& a, const IDAndInfo& b)
      {
         unsigned a_stride = a.info.stride * (a.info.rc.is_subdword() ? 1 : 4);
         unsigned b_stride = b.info.stride * (b.info.rc.is_subdword() ? 1 : 4);
         if (a_stride > b_stride)
            return true;
         if (a_stride < b_stride)
            return false;
         if (a.id == 0xffffffff || b.id == 0xffffffff)
            return a.id ==
                   0xffffffff; /* place 0xffffffff before others if possible, not for any reason */
         return ctx.assignments[a.id].reg < ctx.assignments[b.id].reg;
      });

   PhysReg next_reg = start;
   PhysReg space_reg;
   for (IDAndInfo& var : sorted) {
      unsigned stride = var.info.rc.is_subdword() ? var.info.stride : var.info.stride * 4;
      next_reg.reg_b = align(next_reg.reg_b, MAX2(stride, 4));

      /* 0xffffffff is a special variable ID used reserve a space for killed
       * operands and definitions.
       */
      if (var.id != 0xffffffff) {
         if (next_reg != ctx.assignments[var.id].reg) {
            RegClass rc = ctx.assignments[var.id].rc;
            Temp tmp(var.id, rc);

            Operand pc_op(tmp);
            pc_op.setFixed(ctx.assignments[var.id].reg);
            Definition pc_def(next_reg, rc);
            parallelcopies.emplace_back(pc_op, pc_def);
         }
      } else {
         space_reg = next_reg;
      }

      adjust_max_used_regs(ctx, var.info.rc, next_reg);

      next_reg = next_reg.advance(var.info.rc.size() * 4);
   }

   return space_reg;
}

bool
is_mimg_vaddr_intact(ra_ctx& ctx, RegisterFile& reg_file, Instruction* instr)
{
   PhysReg first{512};
   for (unsigned i = 0; i < instr->operands.size() - 3u; i++) {
      Operand op = instr->operands[i + 3];

      if (ctx.assignments[op.tempId()].assigned) {
         PhysReg reg = ctx.assignments[op.tempId()].reg;

         if (first.reg() == 512) {
            PhysRegInterval bounds = get_reg_bounds(ctx.program, RegType::vgpr);
            first = reg.advance(i * -4);
            PhysRegInterval vec = PhysRegInterval{first, instr->operands.size() - 3u};
            if (!bounds.contains(vec)) /* not enough space for other operands */
               return false;
         } else {
            if (reg != first.advance(i * 4)) /* not at the best position */
               return false;
         }
      } else {
         /* If there's an unexpected temporary, this operand is unlikely to be
          * placed in the best position.
          */
         if (first.reg() != 512 && reg_file.test(first.advance(i * 4), 4))
            return false;
      }
   }

   return true;
}

std::pair<PhysReg, bool>
get_reg_vector(ra_ctx& ctx, RegisterFile& reg_file, Temp temp, aco_ptr<Instruction>& instr)
{
   Instruction* vec = ctx.vectors[temp.id()];
   unsigned first_operand = vec->format == Format::MIMG ? 3 : 0;
   unsigned our_offset = 0;
   for (unsigned i = first_operand; i < vec->operands.size(); i++) {
      Operand& op = vec->operands[i];
      if (op.isTemp() && op.tempId() == temp.id())
         break;
      else
         our_offset += op.bytes();
   }

   if (vec->format != Format::MIMG || is_mimg_vaddr_intact(ctx, reg_file, vec)) {
      unsigned their_offset = 0;
      /* check for every operand of the vector
       * - whether the operand is assigned and
       * - we can use the register relative to that operand
       */
      for (unsigned i = first_operand; i < vec->operands.size(); i++) {
         Operand& op = vec->operands[i];
         if (op.isTemp() && op.tempId() != temp.id() && op.getTemp().type() == temp.type() &&
             ctx.assignments[op.tempId()].assigned) {
            PhysReg reg = ctx.assignments[op.tempId()].reg;
            reg.reg_b += (our_offset - their_offset);
            if (get_reg_specified(ctx, reg_file, temp.regClass(), instr, reg))
               return {reg, true};

            /* return if MIMG vaddr components don't remain vector-aligned */
            if (vec->format == Format::MIMG)
               return {{}, false};
         }
         their_offset += op.bytes();
      }

      /* We didn't find a register relative to other vector operands.
       * Try to find new space which fits the whole vector.
       */
      RegClass vec_rc = RegClass::get(temp.type(), their_offset);
      DefInfo info(ctx, ctx.pseudo_dummy, vec_rc, -1);
      std::pair<PhysReg, bool> res = get_reg_simple(ctx, reg_file, info);
      PhysReg reg = res.first;
      if (res.second) {
         reg.reg_b += our_offset;
         /* make sure to only use byte offset if the instruction supports it */
         if (get_reg_specified(ctx, reg_file, temp.regClass(), instr, reg))
            return {reg, true};
      }
   }
   return {{}, false};
}

PhysReg
get_reg(ra_ctx& ctx, RegisterFile& reg_file, Temp temp,
        std::vector<std::pair<Operand, Definition>>& parallelcopies, aco_ptr<Instruction>& instr,
        int operand_index = -1)
{
   auto split_vec = ctx.split_vectors.find(temp.id());
   if (split_vec != ctx.split_vectors.end()) {
      unsigned offset = 0;
      for (Definition def : split_vec->second->definitions) {
         if (ctx.assignments[def.tempId()].affinity) {
            assignment& affinity = ctx.assignments[ctx.assignments[def.tempId()].affinity];
            if (affinity.assigned) {
               PhysReg reg = affinity.reg;
               reg.reg_b -= offset;
               if (get_reg_specified(ctx, reg_file, temp.regClass(), instr, reg))
                  return reg;
            }
         }
         offset += def.bytes();
      }
   }

   if (ctx.assignments[temp.id()].affinity) {
      assignment& affinity = ctx.assignments[ctx.assignments[temp.id()].affinity];
      if (affinity.assigned) {
         if (get_reg_specified(ctx, reg_file, temp.regClass(), instr, affinity.reg))
            return affinity.reg;
      }
   }

   std::pair<PhysReg, bool> res;

   if (ctx.vectors.find(temp.id()) != ctx.vectors.end()) {
      res = get_reg_vector(ctx, reg_file, temp, instr);
      if (res.second)
         return res.first;
   }

   DefInfo info(ctx, instr, temp.regClass(), operand_index);

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
   assert(reg_file.count_zero(info.bounds) >= info.size);

   if (!increase_register_file(ctx, info.rc.type())) {
      /* fallback algorithm: reallocate all variables at once */
      unsigned def_size = info.rc.size();
      for (Definition def : instr->definitions) {
         if (ctx.assignments[def.tempId()].assigned && def.regClass().type() == info.rc.type())
            def_size += def.regClass().size();
      }

      unsigned killed_op_size = 0;
      for (Operand op : instr->operands) {
         if (op.isTemp() && op.isKillBeforeDef() && op.regClass().type() == info.rc.type())
            killed_op_size += op.regClass().size();
      }

      const PhysRegInterval regs = get_reg_bounds(ctx.program, info.rc.type());

      /* reallocate passthrough variables and non-killed operands */
      std::vector<IDAndRegClass> vars;
      for (const std::pair<unsigned, unsigned>& var : find_vars(ctx, reg_file, regs))
         vars.emplace_back(var.second, ctx.assignments[var.second].rc);
      vars.emplace_back(0xffffffff, RegClass(info.rc.type(), MAX2(def_size, killed_op_size)));

      PhysReg space = compact_relocate_vars(ctx, vars, parallelcopies, regs.lo());

      /* reallocate killed operands */
      std::vector<IDAndRegClass> killed_op_vars;
      for (Operand op : instr->operands) {
         if (op.isKillBeforeDef() && op.regClass().type() == info.rc.type())
            killed_op_vars.emplace_back(op.tempId(), op.regClass());
      }
      compact_relocate_vars(ctx, killed_op_vars, parallelcopies, space);

      /* reallocate definitions */
      std::vector<IDAndRegClass> def_vars;
      for (Definition def : instr->definitions) {
         if (ctx.assignments[def.tempId()].assigned && def.regClass().type() == info.rc.type())
            def_vars.emplace_back(def.tempId(), def.regClass());
      }
      def_vars.emplace_back(0xffffffff, info.rc);
      return compact_relocate_vars(ctx, def_vars, parallelcopies, space);
   }

   return get_reg(ctx, reg_file, temp, parallelcopies, instr, operand_index);
}

PhysReg
get_reg_create_vector(ra_ctx& ctx, RegisterFile& reg_file, Temp temp,
                      std::vector<std::pair<Operand, Definition>>& parallelcopies,
                      aco_ptr<Instruction>& instr)
{
   RegClass rc = temp.regClass();
   /* create_vector instructions have different costs w.r.t. register coalescing */
   uint32_t size = rc.size();
   uint32_t bytes = rc.bytes();
   uint32_t stride = get_stride(rc);
   PhysRegInterval bounds = get_reg_bounds(ctx.program, rc.type());

   // TODO: improve p_create_vector for sub-dword vectors

   PhysReg best_pos{0xFFF};
   unsigned num_moves = 0xFF;
   bool best_avoid = true;

   /* test for each operand which definition placement causes the least shuffle instructions */
   for (unsigned i = 0, offset = 0; i < instr->operands.size();
        offset += instr->operands[i].bytes(), i++) {
      // TODO: think about, if we can alias live operands on the same register
      if (!instr->operands[i].isTemp() || !instr->operands[i].isKillBeforeDef() ||
          instr->operands[i].getTemp().type() != rc.type())
         continue;

      if (offset > instr->operands[i].physReg().reg_b)
         continue;

      unsigned reg_lower = instr->operands[i].physReg().reg_b - offset;
      if (reg_lower % 4)
         continue;
      PhysRegInterval reg_win = {PhysReg{reg_lower / 4}, size};
      unsigned k = 0;

      /* no need to check multiple times */
      if (reg_win.lo() == best_pos)
         continue;

      /* check borders */
      // TODO: this can be improved */
      if (!bounds.contains(reg_win) || reg_win.lo() % stride != 0)
         continue;
      if (reg_win.lo() > bounds.lo() && reg_file[reg_win.lo()] != 0 &&
          reg_file.get_id(reg_win.lo()) == reg_file.get_id(reg_win.lo().advance(-1)))
         continue;
      if (reg_win.hi() < bounds.hi() && reg_file[reg_win.hi().advance(-4)] != 0 &&
          reg_file.get_id(reg_win.hi().advance(-1)) == reg_file.get_id(reg_win.hi()))
         continue;

      /* count variables to be moved and check "avoid" */
      bool avoid = false;
      bool linear_vgpr = false;
      for (PhysReg j : reg_win) {
         if (reg_file[j] != 0) {
            if (reg_file[j] == 0xF0000000) {
               PhysReg reg;
               reg.reg_b = j * 4;
               unsigned bytes_left = bytes - ((unsigned)j - reg_win.lo()) * 4;
               for (unsigned byte_idx = 0; byte_idx < MIN2(bytes_left, 4); byte_idx++, reg.reg_b++)
                  k += reg_file.test(reg, 1);
            } else {
               k += 4;
               linear_vgpr |= ctx.assignments[reg_file[j]].rc.is_linear_vgpr();
            }
         }
         avoid |= ctx.war_hint[j];
      }

      if (linear_vgpr) {
         /* we cannot split live ranges of linear vgprs inside control flow */
         if (ctx.block->kind & block_kind_top_level)
            avoid = true;
         else
            continue;
      }

      if (avoid && !best_avoid)
         continue;

      /* count operands in wrong positions */
      for (unsigned j = 0, offset2 = 0; j < instr->operands.size();
           offset2 += instr->operands[j].bytes(), j++) {
         if (j == i || !instr->operands[j].isTemp() ||
             instr->operands[j].getTemp().type() != rc.type())
            continue;
         if (instr->operands[j].physReg().reg_b != reg_win.lo() * 4 + offset2)
            k += instr->operands[j].bytes();
      }
      bool aligned = rc == RegClass::v4 && reg_win.lo() % 4 == 0;
      if (k > num_moves || (!aligned && k == num_moves))
         continue;

      best_pos = reg_win.lo();
      num_moves = k;
      best_avoid = avoid;
   }

   if (num_moves >= bytes)
      return get_reg(ctx, reg_file, temp, parallelcopies, instr);

   /* re-enable killed operands which are in the wrong position */
   RegisterFile tmp_file(reg_file);
   for (unsigned i = 0, offset = 0; i < instr->operands.size();
        offset += instr->operands[i].bytes(), i++) {
      if (instr->operands[i].isTemp() && instr->operands[i].isFirstKillBeforeDef() &&
          instr->operands[i].physReg().reg_b != best_pos.reg_b + offset)
         tmp_file.fill(instr->operands[i]);
   }

   /* collect variables to be moved */
   std::set<std::pair<unsigned, unsigned>> vars =
      collect_vars(ctx, tmp_file, PhysRegInterval{best_pos, size});

   for (unsigned i = 0, offset = 0; i < instr->operands.size();
        offset += instr->operands[i].bytes(), i++) {
      if (!instr->operands[i].isTemp() || !instr->operands[i].isFirstKillBeforeDef() ||
          instr->operands[i].getTemp().type() != rc.type())
         continue;
      bool correct_pos = instr->operands[i].physReg().reg_b == best_pos.reg_b + offset;
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
   success =
      get_regs_for_copies(ctx, tmp_file, pc, vars, bounds, instr, PhysRegInterval{best_pos, size});

   if (!success) {
      if (!increase_register_file(ctx, temp.type())) {
         /* use the fallback algorithm in get_reg() */
         return get_reg(ctx, reg_file, temp, parallelcopies, instr);
      }
      return get_reg_create_vector(ctx, reg_file, temp, parallelcopies, instr);
   }

   parallelcopies.insert(parallelcopies.end(), pc.begin(), pc.end());
   adjust_max_used_regs(ctx, rc, best_pos);

   return best_pos;
}

void
handle_pseudo(ra_ctx& ctx, const RegisterFile& reg_file, Instruction* instr)
{
   if (instr->format != Format::PSEUDO)
      return;

   /* all instructions which use handle_operands() need this information */
   switch (instr->opcode) {
   case aco_opcode::p_extract_vector:
   case aco_opcode::p_create_vector:
   case aco_opcode::p_split_vector:
   case aco_opcode::p_parallelcopy:
   case aco_opcode::p_wqm: break;
   default: return;
   }

   bool writes_linear = false;
   /* if all definitions are logical vgpr, no need to care for SCC */
   for (Definition& def : instr->definitions) {
      if (def.getTemp().regClass().is_linear())
         writes_linear = true;
   }
   /* if all operands are constant, no need to care either */
   bool reads_linear = false;
   bool reads_subdword = false;
   for (Operand& op : instr->operands) {
      if (op.isTemp() && op.getTemp().regClass().is_linear())
         reads_linear = true;
      if (op.isTemp() && op.regClass().is_subdword())
         reads_subdword = true;
   }
   bool needs_scratch_reg = (writes_linear && reads_linear && reg_file[scc]) ||
                            (ctx.program->chip_class <= GFX7 && reads_subdword);
   if (!needs_scratch_reg)
      return;

   instr->pseudo().tmp_in_scc = reg_file[scc];

   int reg = ctx.max_used_sgpr;
   for (; reg >= 0 && reg_file[PhysReg{(unsigned)reg}]; reg--)
      ;
   if (reg < 0) {
      reg = ctx.max_used_sgpr + 1;
      for (; reg < ctx.program->max_reg_demand.sgpr && reg_file[PhysReg{(unsigned)reg}]; reg++)
         ;
      if (reg == ctx.program->max_reg_demand.sgpr) {
         assert(reads_subdword && reg_file[m0] == 0);
         reg = m0;
      }
   }

   adjust_max_used_regs(ctx, s1, reg);
   instr->pseudo().scratch_sgpr = PhysReg{(unsigned)reg};
}

bool
operand_can_use_reg(chip_class chip, aco_ptr<Instruction>& instr, unsigned idx, PhysReg reg,
                    RegClass rc)
{
   if (instr->operands[idx].isFixed())
      return instr->operands[idx].physReg() == reg;

   bool is_writelane = instr->opcode == aco_opcode::v_writelane_b32 ||
                       instr->opcode == aco_opcode::v_writelane_b32_e64;
   if (chip <= GFX9 && is_writelane && idx <= 1) {
      /* v_writelane_b32 can take two sgprs but only if one is m0. */
      bool is_other_sgpr =
         instr->operands[!idx].isTemp() &&
         (!instr->operands[!idx].isFixed() || instr->operands[!idx].physReg() != m0);
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
      return reg != scc && reg != exec &&
             (reg != m0 || idx == 1 || idx == 3) && /* offset can be m0 */
             (reg != vcc || (instr->definitions.empty() && idx == 2) ||
              chip >= GFX10); /* sdata can be vcc */
   default:
      // TODO: there are more instructions with restrictions on registers
      return true;
   }
}

void
get_reg_for_operand(ra_ctx& ctx, RegisterFile& register_file,
                    std::vector<std::pair<Operand, Definition>>& parallelcopy,
                    aco_ptr<Instruction>& instr, Operand& operand, unsigned operand_index)
{
   /* check if the operand is fixed */
   PhysReg src = ctx.assignments[operand.tempId()].reg;
   PhysReg dst;
   if (operand.isFixed()) {
      assert(operand.physReg() != src);

      /* check if target reg is blocked, and move away the blocking var */
      if (register_file.test(operand.physReg(), operand.bytes())) {
         PhysRegInterval target{operand.physReg(), operand.size()};

         RegisterFile tmp_file(register_file);

         std::set<std::pair<unsigned, unsigned>> blocking_vars =
            collect_vars(ctx, tmp_file, target);

         tmp_file.clear(src, operand.regClass()); // TODO: try to avoid moving block vars to src
         tmp_file.block(operand.physReg(), operand.regClass());

         DefInfo info(ctx, instr, operand.regClass(), -1);
         get_regs_for_copies(ctx, tmp_file, parallelcopy, blocking_vars, info.bounds, instr,
                             PhysRegInterval());
      }
      dst = operand.physReg();

   } else {
      /* clear the operand in case it's only a stride mismatch */
      register_file.clear(src, operand.regClass());
      dst = get_reg(ctx, register_file, operand.getTemp(), parallelcopy, instr, operand_index);
   }

   Operand pc_op = operand;
   pc_op.setFixed(src);
   Definition pc_def = Definition(dst, pc_op.regClass());
   parallelcopy.emplace_back(pc_op, pc_def);
   update_renames(ctx, register_file, parallelcopy, instr, rename_not_killed_ops | fill_killed_ops);
}

void
get_regs_for_phis(ra_ctx& ctx, Block& block, RegisterFile& register_file,
                  std::vector<aco_ptr<Instruction>>& instructions, IDSet& live_in)
{
   /* assign phis with all-matching registers to that register */
   for (aco_ptr<Instruction>& phi : block.instructions) {
      if (!is_phi(phi))
         break;
      Definition& definition = phi->definitions[0];
      if (definition.isKill() || definition.isFixed())
         continue;

      if (!phi->operands[0].isTemp())
         continue;

      PhysReg reg = phi->operands[0].physReg();
      auto OpsSame = [=](const Operand& op) -> bool
      { return op.isTemp() && (!op.isFixed() || op.physReg() == reg); };
      bool all_same = std::all_of(phi->operands.cbegin() + 1, phi->operands.cend(), OpsSame);
      if (!all_same)
         continue;

      if (!get_reg_specified(ctx, register_file, definition.regClass(), phi, reg))
         continue;

      definition.setFixed(reg);
      register_file.fill(definition);
      ctx.assignments[definition.tempId()].set(definition);
   }

   /* try to find a register that is used by at least one operand */
   for (aco_ptr<Instruction>& phi : block.instructions) {
      if (!is_phi(phi))
         break;
      Definition& definition = phi->definitions[0];
      if (definition.isKill() || definition.isFixed())
         continue;

      /* use affinity if available */
      if (ctx.assignments[definition.tempId()].affinity &&
          ctx.assignments[ctx.assignments[definition.tempId()].affinity].assigned) {
         assignment& affinity = ctx.assignments[ctx.assignments[definition.tempId()].affinity];
         assert(affinity.rc == definition.regClass());
         if (get_reg_specified(ctx, register_file, definition.regClass(), phi, affinity.reg)) {
            definition.setFixed(affinity.reg);
            register_file.fill(definition);
            ctx.assignments[definition.tempId()].set(definition);
            continue;
         }
      }

      /* by going backwards, we aim to avoid copies in else-blocks */
      for (int i = phi->operands.size() - 1; i >= 0; i--) {
         const Operand& op = phi->operands[i];
         if (!op.isTemp() || !op.isFixed())
            continue;

         PhysReg reg = op.physReg();
         if (get_reg_specified(ctx, register_file, definition.regClass(), phi, reg)) {
            definition.setFixed(reg);
            register_file.fill(definition);
            ctx.assignments[definition.tempId()].set(definition);
            break;
         }
      }
   }

   /* find registers for phis where the register was blocked or no operand was assigned */
   for (aco_ptr<Instruction>& phi : block.instructions) {
      if (!is_phi(phi))
         break;

      Definition& definition = phi->definitions[0];
      if (definition.isKill())
         continue;

      if (definition.isFixed()) {
         instructions.emplace_back(std::move(phi));
         continue;
      }

      std::vector<std::pair<Operand, Definition>> parallelcopy;
      definition.setFixed(get_reg(ctx, register_file, definition.getTemp(), parallelcopy, phi));
      update_renames(ctx, register_file, parallelcopy, phi, rename_not_killed_ops);

      /* process parallelcopy */
      for (std::pair<Operand, Definition> pc : parallelcopy) {
         /* see if it's a copy from a different phi */
         // TODO: prefer moving some previous phis over live-ins
         // TODO: somehow prevent phis fixed before the RA from being updated (shouldn't be a
         // problem in practice since they can only be fixed to exec)
         Instruction* prev_phi = NULL;
         std::vector<aco_ptr<Instruction>>::iterator phi_it;
         for (phi_it = instructions.begin(); phi_it != instructions.end(); ++phi_it) {
            if ((*phi_it)->definitions[0].tempId() == pc.first.tempId())
               prev_phi = phi_it->get();
         }
         if (prev_phi) {
            /* if so, just update that phi's register */
            prev_phi->definitions[0].setFixed(pc.second.physReg());
            ctx.assignments[prev_phi->definitions[0].tempId()].set(pc.second);
            continue;
         }

         /* rename */
         std::unordered_map<unsigned, Temp>::iterator orig_it =
            ctx.orig_names.find(pc.first.tempId());
         Temp orig = pc.first.getTemp();
         if (orig_it != ctx.orig_names.end())
            orig = orig_it->second;
         else
            ctx.orig_names[pc.second.tempId()] = orig;
         ctx.renames[block.index][orig.id()] = pc.second.getTemp();

         /* otherwise, this is a live-in and we need to create a new phi
          * to move it in this block's predecessors */
         aco_opcode opcode =
            pc.first.getTemp().is_linear() ? aco_opcode::p_linear_phi : aco_opcode::p_phi;
         std::vector<unsigned>& preds =
            pc.first.getTemp().is_linear() ? block.linear_preds : block.logical_preds;
         aco_ptr<Instruction> new_phi{
            create_instruction<Pseudo_instruction>(opcode, Format::PSEUDO, preds.size(), 1)};
         new_phi->definitions[0] = pc.second;
         for (unsigned i = 0; i < preds.size(); i++)
            new_phi->operands[i] = Operand(pc.first);
         instructions.emplace_back(std::move(new_phi));

         /* Remove from live_out_per_block (now used for live-in), because handle_loop_phis()
          * would re-create this phi later if this is a loop header.
          */
         live_in.erase(orig.id());
      }

      register_file.fill(definition);
      ctx.assignments[definition.tempId()].set(definition);
      instructions.emplace_back(std::move(phi));
   }
}

Temp
read_variable(ra_ctx& ctx, Temp val, unsigned block_idx)
{
   std::unordered_map<unsigned, Temp>::iterator it = ctx.renames[block_idx].find(val.id());
   if (it == ctx.renames[block_idx].end())
      return val;
   else
      return it->second;
}

Temp
handle_live_in(ra_ctx& ctx, Temp val, Block* block)
{
   std::vector<unsigned>& preds = val.is_linear() ? block->linear_preds : block->logical_preds;
   if (preds.size() == 0)
      return val;

   if (preds.size() == 1) {
      /* if the block has only one predecessor, just look there for the name */
      return read_variable(ctx, val, preds[0]);
   }

   /* there are multiple predecessors and the block is sealed */
   Temp* const ops = (Temp*)alloca(preds.size() * sizeof(Temp));

   /* get the rename from each predecessor and check if they are the same */
   Temp new_val;
   bool needs_phi = false;
   for (unsigned i = 0; i < preds.size(); i++) {
      ops[i] = read_variable(ctx, val, preds[i]);
      if (i == 0)
         new_val = ops[i];
      else
         needs_phi |= !(new_val == ops[i]);
   }

   if (needs_phi) {
      assert(!val.regClass().is_linear_vgpr());

      /* the variable has been renamed differently in the predecessors: we need to insert a phi */
      aco_opcode opcode = val.is_linear() ? aco_opcode::p_linear_phi : aco_opcode::p_phi;
      aco_ptr<Instruction> phi{
         create_instruction<Pseudo_instruction>(opcode, Format::PSEUDO, preds.size(), 1)};
      new_val = ctx.program->allocateTmp(val.regClass());
      phi->definitions[0] = Definition(new_val);
      ctx.assignments.emplace_back();
      assert(ctx.assignments.size() == ctx.program->peekAllocationId());
      for (unsigned i = 0; i < preds.size(); i++) {
         /* update the operands so that it uses the new affinity */
         phi->operands[i] = Operand(ops[i]);
         assert(ctx.assignments[ops[i].id()].assigned);
         assert(ops[i].regClass() == new_val.regClass());
         phi->operands[i].setFixed(ctx.assignments[ops[i].id()].reg);
      }
      block->instructions.insert(block->instructions.begin(), std::move(phi));
   }

   return new_val;
}

void
handle_loop_phis(ra_ctx& ctx, const IDSet& live_in, uint32_t loop_header_idx,
                 uint32_t loop_exit_idx)
{
   Block& loop_header = ctx.program->blocks[loop_header_idx];
   std::unordered_map<unsigned, Temp> renames;

   /* create phis for variables renamed during the loop */
   for (unsigned t : live_in) {
      Temp val = Temp(t, ctx.program->temp_rc[t]);
      Temp prev = read_variable(ctx, val, loop_header_idx - 1);
      Temp renamed = handle_live_in(ctx, val, &loop_header);
      if (renamed == prev)
         continue;

      /* insert additional renames at block end, but don't overwrite */
      renames[prev.id()] = renamed;
      ctx.orig_names[renamed.id()] = val;
      for (unsigned idx = loop_header_idx; idx < loop_exit_idx; idx++) {
         auto it = ctx.renames[idx].emplace(val.id(), renamed);
         /* if insertion is unsuccessful, update if necessary */
         if (!it.second && it.first->second == prev)
            it.first->second = renamed;
      }

      /* update loop-carried values of the phi created by handle_live_in() */
      for (unsigned i = 1; i < loop_header.instructions[0]->operands.size(); i++) {
         Operand& op = loop_header.instructions[0]->operands[i];
         if (op.getTemp() == prev)
            op.setTemp(renamed);
      }

      /* use the assignment from the loop preheader and fix def reg */
      assignment& var = ctx.assignments[prev.id()];
      ctx.assignments[renamed.id()] = var;
      loop_header.instructions[0]->definitions[0].setFixed(var.reg);
   }

   /* rename loop carried phi operands */
   for (unsigned i = renames.size(); i < loop_header.instructions.size(); i++) {
      aco_ptr<Instruction>& phi = loop_header.instructions[i];
      if (!is_phi(phi))
         break;
      const std::vector<unsigned>& preds =
         phi->opcode == aco_opcode::p_phi ? loop_header.logical_preds : loop_header.linear_preds;
      for (unsigned j = 1; j < phi->operands.size(); j++) {
         Operand& op = phi->operands[j];
         if (!op.isTemp())
            continue;

         /* Find the original name, since this operand might not use the original name if the phi
          * was created after init_reg_file().
          */
         std::unordered_map<unsigned, Temp>::iterator it = ctx.orig_names.find(op.tempId());
         Temp orig = it != ctx.orig_names.end() ? it->second : op.getTemp();

         op.setTemp(read_variable(ctx, orig, preds[j]));
         op.setFixed(ctx.assignments[op.tempId()].reg);
      }
   }

   /* return early if no new phi was created */
   if (renames.empty())
      return;

   /* propagate new renames through loop */
   for (unsigned idx = loop_header_idx; idx < loop_exit_idx; idx++) {
      Block& current = ctx.program->blocks[idx];
      /* rename all uses in this block */
      for (aco_ptr<Instruction>& instr : current.instructions) {
         /* phis are renamed after RA */
         if (idx == loop_header_idx && is_phi(instr))
            continue;

         for (Operand& op : instr->operands) {
            if (!op.isTemp())
               continue;

            auto rename = renames.find(op.tempId());
            if (rename != renames.end()) {
               assert(rename->second.id());
               op.setTemp(rename->second);
            }
         }
      }
   }
}

/**
 * This function serves the purpose to correctly initialize the register file
 * at the beginning of a block (before any existing phis).
 * In order to do so, all live-in variables are entered into the RegisterFile.
 * Reg-to-reg moves (renames) from previous blocks are taken into account and
 * the SSA is repaired by inserting corresponding phi-nodes.
 */
RegisterFile
init_reg_file(ra_ctx& ctx, const std::vector<IDSet>& live_out_per_block, Block& block)
{
   if (block.kind & block_kind_loop_exit) {
      uint32_t header = ctx.loop_header.back();
      ctx.loop_header.pop_back();
      handle_loop_phis(ctx, live_out_per_block[header], header, block.index);
   }

   RegisterFile register_file;
   const IDSet& live_in = live_out_per_block[block.index];
   assert(block.index != 0 || live_in.empty());

   if (block.kind & block_kind_loop_header) {
      ctx.loop_header.emplace_back(block.index);
      /* already rename phis incoming value */
      for (aco_ptr<Instruction>& instr : block.instructions) {
         if (!is_phi(instr))
            break;
         Operand& operand = instr->operands[0];
         if (operand.isTemp()) {
            operand.setTemp(read_variable(ctx, operand.getTemp(), block.index - 1));
            operand.setFixed(ctx.assignments[operand.tempId()].reg);
         }
      }
      for (unsigned t : live_in) {
         Temp val = Temp(t, ctx.program->temp_rc[t]);
         Temp renamed = read_variable(ctx, val, block.index - 1);
         if (renamed != val)
            ctx.renames[block.index][val.id()] = renamed;
         assignment& var = ctx.assignments[renamed.id()];
         assert(var.assigned);
         register_file.fill(Definition(renamed.id(), var.reg, var.rc));
      }
   } else {
      /* rename phi operands */
      for (aco_ptr<Instruction>& instr : block.instructions) {
         if (!is_phi(instr))
            break;
         const std::vector<unsigned>& preds =
            instr->opcode == aco_opcode::p_phi ? block.logical_preds : block.linear_preds;

         for (unsigned i = 0; i < instr->operands.size(); i++) {
            Operand& operand = instr->operands[i];
            if (!operand.isTemp())
               continue;
            operand.setTemp(read_variable(ctx, operand.getTemp(), preds[i]));
            operand.setFixed(ctx.assignments[operand.tempId()].reg);
         }
      }
      for (unsigned t : live_in) {
         Temp val = Temp(t, ctx.program->temp_rc[t]);
         Temp renamed = handle_live_in(ctx, val, &block);
         assignment& var = ctx.assignments[renamed.id()];
         /* due to live-range splits, the live-in might be a phi, now */
         if (var.assigned) {
            register_file.fill(Definition(renamed.id(), var.reg, var.rc));
         }
         if (renamed != val) {
            ctx.renames[block.index].emplace(t, renamed);
            ctx.orig_names[renamed.id()] = val;
         }
      }
   }

   return register_file;
}

void
get_affinities(ra_ctx& ctx, std::vector<IDSet>& live_out_per_block)
{
   std::vector<std::vector<Temp>> phi_ressources;
   std::unordered_map<unsigned, unsigned> temp_to_phi_ressources;

   for (auto block_rit = ctx.program->blocks.rbegin(); block_rit != ctx.program->blocks.rend();
        block_rit++) {
      Block& block = *block_rit;

      /* first, compute the death points of all live vars within the block */
      IDSet& live = live_out_per_block[block.index];

      std::vector<aco_ptr<Instruction>>::reverse_iterator rit;
      for (rit = block.instructions.rbegin(); rit != block.instructions.rend(); ++rit) {
         aco_ptr<Instruction>& instr = *rit;
         if (is_phi(instr))
            break;

         /* add vector affinities */
         if (instr->opcode == aco_opcode::p_create_vector) {
            for (const Operand& op : instr->operands) {
               if (op.isTemp() && op.isFirstKill() &&
                   op.getTemp().type() == instr->definitions[0].getTemp().type())
                  ctx.vectors[op.tempId()] = instr.get();
            }
         } else if (instr->format == Format::MIMG && instr->operands.size() > 4) {
            for (unsigned i = 3; i < instr->operands.size(); i++)
               ctx.vectors[instr->operands[i].tempId()] = instr.get();
         }

         if (instr->opcode == aco_opcode::p_split_vector &&
             instr->operands[0].isFirstKillBeforeDef())
            ctx.split_vectors[instr->operands[0].tempId()] = instr.get();

         /* add operands to live variables */
         for (const Operand& op : instr->operands) {
            if (op.isTemp())
               live.insert(op.tempId());
         }

         /* erase definitions from live */
         for (unsigned i = 0; i < instr->definitions.size(); i++) {
            const Definition& def = instr->definitions[i];
            if (!def.isTemp())
               continue;
            live.erase(def.tempId());
            /* mark last-seen phi operand */
            std::unordered_map<unsigned, unsigned>::iterator it =
               temp_to_phi_ressources.find(def.tempId());
            if (it != temp_to_phi_ressources.end() &&
                def.regClass() == phi_ressources[it->second][0].regClass()) {
               phi_ressources[it->second][0] = def.getTemp();
               /* try to coalesce phi affinities with parallelcopies */
               Operand op = Operand();
               switch (instr->opcode) {
               case aco_opcode::p_parallelcopy: op = instr->operands[i]; break;

               case aco_opcode::v_interp_p2_f32:
               case aco_opcode::v_writelane_b32:
               case aco_opcode::v_writelane_b32_e64: op = instr->operands[2]; break;

               case aco_opcode::v_fma_f32:
               case aco_opcode::v_fma_f16:
               case aco_opcode::v_pk_fma_f16:
                  if (ctx.program->chip_class < GFX10)
                     continue;
                  FALLTHROUGH;
               case aco_opcode::v_mad_f32:
               case aco_opcode::v_mad_f16:
                  if (instr->usesModifiers())
                     continue;
                  op = instr->operands[2];
                  break;

               case aco_opcode::v_mad_legacy_f32:
               case aco_opcode::v_fma_legacy_f32:
                  if (instr->usesModifiers() || !ctx.program->dev.has_mac_legacy32)
                     continue;
                  op = instr->operands[2];
                  break;

               default: continue;
               }

               if (op.isTemp() && op.isFirstKillBeforeDef() && def.regClass() == op.regClass()) {
                  phi_ressources[it->second].emplace_back(op.getTemp());
                  temp_to_phi_ressources[op.tempId()] = it->second;
               }
            }
         }
      }

      /* collect phi affinities */
      for (; rit != block.instructions.rend(); ++rit) {
         aco_ptr<Instruction>& instr = *rit;
         assert(is_phi(instr));

         live.erase(instr->definitions[0].tempId());
         if (instr->definitions[0].isKill() || instr->definitions[0].isFixed())
            continue;

         assert(instr->definitions[0].isTemp());
         std::unordered_map<unsigned, unsigned>::iterator it =
            temp_to_phi_ressources.find(instr->definitions[0].tempId());
         unsigned index = phi_ressources.size();
         std::vector<Temp>* affinity_related;
         if (it != temp_to_phi_ressources.end()) {
            index = it->second;
            phi_ressources[index][0] = instr->definitions[0].getTemp();
            affinity_related = &phi_ressources[index];
         } else {
            phi_ressources.emplace_back(std::vector<Temp>{instr->definitions[0].getTemp()});
            affinity_related = &phi_ressources.back();
         }

         for (const Operand& op : instr->operands) {
            if (op.isTemp() && op.isKill() && op.regClass() == instr->definitions[0].regClass()) {
               affinity_related->emplace_back(op.getTemp());
               if (block.kind & block_kind_loop_header)
                  continue;
               temp_to_phi_ressources[op.tempId()] = index;
            }
         }
      }

      /* visit the loop header phis first in order to create nested affinities */
      if (block.kind & block_kind_loop_exit) {
         /* find loop header */
         auto header_rit = block_rit;
         while ((header_rit + 1)->loop_nest_depth > block.loop_nest_depth)
            header_rit++;

         for (aco_ptr<Instruction>& phi : header_rit->instructions) {
            if (!is_phi(phi))
               break;
            if (phi->definitions[0].isKill() || phi->definitions[0].isFixed())
               continue;

            /* create an (empty) merge-set for the phi-related variables */
            auto it = temp_to_phi_ressources.find(phi->definitions[0].tempId());
            unsigned index = phi_ressources.size();
            if (it == temp_to_phi_ressources.end()) {
               temp_to_phi_ressources[phi->definitions[0].tempId()] = index;
               phi_ressources.emplace_back(std::vector<Temp>{phi->definitions[0].getTemp()});
            } else {
               index = it->second;
            }
            for (unsigned i = 1; i < phi->operands.size(); i++) {
               const Operand& op = phi->operands[i];
               if (op.isTemp() && op.isKill() && op.regClass() == phi->definitions[0].regClass()) {
                  temp_to_phi_ressources[op.tempId()] = index;
               }
            }
         }
      }
   }
   /* create affinities */
   for (std::vector<Temp>& vec : phi_ressources) {
      for (unsigned i = 1; i < vec.size(); i++)
         if (vec[i].id() != vec[0].id())
            ctx.assignments[vec[i].id()].affinity = vec[0].id();
   }
}

} /* end namespace */

void
register_allocation(Program* program, std::vector<IDSet>& live_out_per_block, ra_test_policy policy)
{
   ra_ctx ctx(program, policy);
   get_affinities(ctx, live_out_per_block);

   /* state of register file after phis */
   std::vector<std::bitset<128>> sgpr_live_in(program->blocks.size());

   for (Block& block : program->blocks) {
      ctx.block = &block;

      /* initialize register file */
      RegisterFile register_file = init_reg_file(ctx, live_out_per_block, block);
      ctx.war_hint.reset();

      std::vector<aco_ptr<Instruction>> instructions;

      /* this is a slight adjustment from the paper as we already have phi nodes:
       * We consider them incomplete phis and only handle the definition. */
      get_regs_for_phis(ctx, block, register_file, instructions, live_out_per_block[block.index]);

      /* fill in sgpr_live_in */
      for (unsigned i = 0; i <= ctx.max_used_sgpr; i++)
         sgpr_live_in[block.index][i] = register_file[PhysReg{i}];
      sgpr_live_in[block.index][127] = register_file[scc];

      /* Handle all other instructions of the block */
      auto NonPhi = [](aco_ptr<Instruction>& instr) -> bool { return instr && !is_phi(instr); };
      std::vector<aco_ptr<Instruction>>::iterator instr_it =
         std::find_if(block.instructions.begin(), block.instructions.end(), NonPhi);
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
                     Definition phi_op(
                        read_variable(ctx, phi->operands[idx].getTemp(), block.index));
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

         bool temp_in_scc = register_file[scc];

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

            if (instr->isEXP() || (instr->isVMEM() && i == 3 && ctx.program->chip_class == GFX6) ||
                (instr->isDS() && instr->ds().gds)) {
               for (unsigned j = 0; j < operand.size(); j++)
                  ctx.war_hint.set(operand.physReg().reg() + j);
            }
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
              (instr->opcode == aco_opcode::v_fma_f16 && program->chip_class >= GFX10) ||
              (instr->opcode == aco_opcode::v_pk_fma_f16 && program->chip_class >= GFX10) ||
              (instr->opcode == aco_opcode::v_mad_legacy_f32 && program->dev.has_mac_legacy32) ||
              (instr->opcode == aco_opcode::v_fma_legacy_f32 && program->dev.has_mac_legacy32) ||
              (instr->opcode == aco_opcode::v_dot4_i32_i8 && program->family != CHIP_VEGA20)) &&
             instr->operands[2].isTemp() && instr->operands[2].isKillBeforeDef() &&
             instr->operands[2].getTemp().type() == RegType::vgpr &&
             ((instr->operands[0].isTemp() &&
               instr->operands[0].getTemp().type() == RegType::vgpr) ||
              (instr->operands[1].isTemp() &&
               instr->operands[1].getTemp().type() == RegType::vgpr)) &&
             !instr->usesModifiers() && instr->operands[0].physReg().byte() == 0 &&
             instr->operands[1].physReg().byte() == 0 && instr->operands[2].physReg().byte() == 0) {
            if (!instr->operands[1].isTemp() ||
                instr->operands[1].getTemp().type() != RegType::vgpr)
               std::swap(instr->operands[0], instr->operands[1]);

            unsigned def_id = instr->definitions[0].tempId();
            bool use_vop2 = true;
            if (ctx.assignments[def_id].affinity) {
               assignment& affinity = ctx.assignments[ctx.assignments[def_id].affinity];
               if (affinity.assigned && affinity.reg != instr->operands[2].physReg() &&
                   !register_file.test(affinity.reg, instr->operands[2].bytes()))
                  use_vop2 = false;
            }
            if (use_vop2) {
               instr->format = Format::VOP2;
               switch (instr->opcode) {
               case aco_opcode::v_mad_f32: instr->opcode = aco_opcode::v_mac_f32; break;
               case aco_opcode::v_fma_f32: instr->opcode = aco_opcode::v_fmac_f32; break;
               case aco_opcode::v_mad_f16:
               case aco_opcode::v_mad_legacy_f16: instr->opcode = aco_opcode::v_mac_f16; break;
               case aco_opcode::v_fma_f16: instr->opcode = aco_opcode::v_fmac_f16; break;
               case aco_opcode::v_pk_fma_f16: instr->opcode = aco_opcode::v_pk_fmac_f16; break;
               case aco_opcode::v_dot4_i32_i8: instr->opcode = aco_opcode::v_dot4c_i32_i8; break;
               case aco_opcode::v_mad_legacy_f32:
                  instr->opcode = aco_opcode::v_mac_legacy_f32;
                  break;
               case aco_opcode::v_fma_legacy_f32:
                  instr->opcode = aco_opcode::v_fmac_legacy_f32;
                  break;
               default: break;
               }
            }
         }

         /* handle definitions which must have the same register as an operand */
         if (instr->opcode == aco_opcode::v_interp_p2_f32 ||
             instr->opcode == aco_opcode::v_mac_f32 || instr->opcode == aco_opcode::v_fmac_f32 ||
             instr->opcode == aco_opcode::v_mac_f16 || instr->opcode == aco_opcode::v_fmac_f16 ||
             instr->opcode == aco_opcode::v_mac_legacy_f32 ||
             instr->opcode == aco_opcode::v_fmac_legacy_f32 ||
             instr->opcode == aco_opcode::v_pk_fmac_f16 ||
             instr->opcode == aco_opcode::v_writelane_b32 ||
             instr->opcode == aco_opcode::v_writelane_b32_e64 ||
             instr->opcode == aco_opcode::v_dot4c_i32_i8) {
            instr->definitions[0].setFixed(instr->operands[2].physReg());
         } else if (instr->opcode == aco_opcode::s_addk_i32 ||
                    instr->opcode == aco_opcode::s_mulk_i32) {
            instr->definitions[0].setFixed(instr->operands[0].physReg());
         } else if (instr->isMUBUF() && instr->definitions.size() == 1 &&
                    instr->operands.size() == 4) {
            instr->definitions[0].setFixed(instr->operands[3].physReg());
         } else if (instr->isMIMG() && instr->definitions.size() == 1 &&
                    !instr->operands[2].isUndefined()) {
            instr->definitions[0].setFixed(instr->operands[2].physReg());
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
               const PhysRegInterval def_regs{definition.physReg(), definition.size()};

               /* create parallelcopy pair to move blocking vars */
               std::set<std::pair<unsigned, unsigned>> vars =
                  collect_vars(ctx, register_file, def_regs);

               RegisterFile tmp_file(register_file);
               /* re-enable the killed operands, so that we don't move the blocking vars there */
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.isFirstKillBeforeDef())
                     tmp_file.fill(op);
               }

               ASSERTED bool success = false;
               DefInfo info(ctx, instr, definition.regClass(), -1);
               success = get_regs_for_copies(ctx, tmp_file, parallelcopy, vars, info.bounds, instr,
                                             def_regs);
               assert(success);

               update_renames(ctx, register_file, parallelcopy, instr, (UpdateRenames)0);
            }
            ctx.defs_done.set(i);

            if (!definition.isTemp())
               continue;

            ctx.assignments[definition.tempId()].set(definition);
            register_file.fill(definition);
         }

         /* handle all other definitions */
         for (unsigned i = 0; i < instr->definitions.size(); ++i) {
            Definition* definition = &instr->definitions[i];

            if (definition->isFixed() || !definition->isTemp())
               continue;

            /* find free reg */
            if (definition->hasHint() &&
                get_reg_specified(ctx, register_file, definition->regClass(), instr,
                                  definition->physReg())) {
               definition->setFixed(definition->physReg());
            } else if (instr->opcode == aco_opcode::p_split_vector) {
               PhysReg reg = instr->operands[0].physReg();
               for (unsigned j = 0; j < i; j++)
                  reg.reg_b += instr->definitions[j].bytes();
               if (get_reg_specified(ctx, register_file, definition->regClass(), instr, reg))
                  definition->setFixed(reg);
            } else if (instr->opcode == aco_opcode::p_wqm ||
                       instr->opcode == aco_opcode::p_parallelcopy) {
               PhysReg reg = instr->operands[i].physReg();
               if (instr->operands[i].isTemp() &&
                   instr->operands[i].getTemp().type() == definition->getTemp().type() &&
                   !register_file.test(reg, definition->bytes()))
                  definition->setFixed(reg);
            } else if (instr->opcode == aco_opcode::p_extract_vector) {
               PhysReg reg = instr->operands[0].physReg();
               reg.reg_b += definition->bytes() * instr->operands[1].constantValue();
               if (get_reg_specified(ctx, register_file, definition->regClass(), instr, reg))
                  definition->setFixed(reg);
            } else if (instr->opcode == aco_opcode::p_create_vector) {
               PhysReg reg = get_reg_create_vector(ctx, register_file, definition->getTemp(),
                                                   parallelcopy, instr);
               update_renames(ctx, register_file, parallelcopy, instr, (UpdateRenames)0);
               definition->setFixed(reg);
            }

            if (!definition->isFixed()) {
               Temp tmp = definition->getTemp();
               if (definition->regClass().is_subdword() && definition->bytes() < 4) {
                  PhysReg reg = get_reg(ctx, register_file, tmp, parallelcopy, instr);
                  definition->setFixed(reg);
                  if (reg.byte() || register_file.test(reg, 4)) {
                     add_subdword_definition(program, instr, reg);
                     definition = &instr->definitions[i]; /* add_subdword_definition can invalidate
                                                             the reference */
                  }
               } else {
                  definition->setFixed(get_reg(ctx, register_file, tmp, parallelcopy, instr));
               }
               update_renames(ctx, register_file, parallelcopy, instr,
                              instr->opcode != aco_opcode::p_create_vector ? rename_not_killed_ops
                                                                           : (UpdateRenames)0);
            }

            assert(
               definition->isFixed() &&
               ((definition->getTemp().type() == RegType::vgpr && definition->physReg() >= 256) ||
                (definition->getTemp().type() != RegType::vgpr && definition->physReg() < 256)));
            ctx.defs_done.set(i);
            ctx.assignments[definition->tempId()].set(*definition);
            register_file.fill(*definition);
         }

         handle_pseudo(ctx, register_file, instr.get());

         /* kill definitions and late-kill operands and ensure that sub-dword operands can actually
          * be read */
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
            pc.reset(create_instruction<Pseudo_instruction>(aco_opcode::p_parallelcopy,
                                                            Format::PSEUDO, parallelcopy.size(),
                                                            parallelcopy.size()));
            bool linear_vgpr = false;
            bool sgpr_operands_alias_defs = false;
            uint64_t sgpr_operands[4] = {0, 0, 0, 0};
            for (unsigned i = 0; i < parallelcopy.size(); i++) {
               linear_vgpr |= parallelcopy[i].first.regClass().is_linear_vgpr();

               if (temp_in_scc && parallelcopy[i].first.isTemp() &&
                   parallelcopy[i].first.getTemp().type() == RegType::sgpr) {
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

               /* it might happen that the operand is already renamed. we have to restore the
                * original name. */
               std::unordered_map<unsigned, Temp>::iterator it =
                  ctx.orig_names.find(pc->operands[i].tempId());
               Temp orig = it != ctx.orig_names.end() ? it->second : pc->operands[i].getTemp();
               ctx.orig_names[pc->definitions[i].tempId()] = orig;
               ctx.renames[block.index][orig.id()] = pc->definitions[i].getTemp();
            }

            if (temp_in_scc && (sgpr_operands_alias_defs || linear_vgpr)) {
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
         bool instr_needs_vop3 =
            !instr->isVOP3() &&
            ((instr->format == Format::VOPC && !(instr->definitions[0].physReg() == vcc)) ||
             (instr->opcode == aco_opcode::v_cndmask_b32 &&
              !(instr->operands[2].physReg() == vcc)) ||
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
            if (instr->operands.size() && instr->operands[0].isLiteral() &&
                program->chip_class < GFX10) {
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
               update_renames(ctx, register_file, parallelcopy, instr, rename_not_killed_ops);

               aco_ptr<Instruction> mov;
               if (can_sgpr)
                  mov.reset(create_instruction<SOP1_instruction>(aco_opcode::s_mov_b32,
                                                                 Format::SOP1, 1, 1));
               else
                  mov.reset(create_instruction<VOP1_instruction>(aco_opcode::v_mov_b32,
                                                                 Format::VOP1, 1, 1));
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
            instr.reset(create_instruction<VOP3_instruction>(
               tmp->opcode, format, tmp->operands.size(), tmp->definitions.size()));
            std::copy(tmp->operands.begin(), tmp->operands.end(), instr->operands.begin());
            std::copy(tmp->definitions.begin(), tmp->definitions.end(), instr->definitions.begin());
         }

         instructions.emplace_back(std::move(*instr_it));

      } /* end for Instr */

      block.instructions = std::move(instructions);
   } /* end for BB */

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
   program->config->num_vgprs = get_vgpr_alloc(program, ctx.max_used_vgpr + 1);
   program->config->num_sgprs = get_sgpr_alloc(program, ctx.max_used_sgpr + 1);

   program->progress = CompilationProgress::after_ra;
}

} // namespace aco
