/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_ir.h"

namespace aco {

namespace {

Temp
dword_temp(Temp tmp)
{
   if (!tmp.regClass().is_subdword())
      return tmp;

   RegClass rc = RegClass(tmp.type(), tmp.size());
   if (tmp.regClass().is_linear())
      rc = rc.as_linear();
   return Temp(tmp.id(), rc);
}

Definition
dword_def(Program* program, Definition def)
{
   def.setTemp(dword_temp(def.getTemp()));

   if (def.isTemp())
      program->temp_rc[def.tempId()] = def.regClass();

   return def;
}

Operand
dword_op(Operand op, bool convert_const)
{
   if (op.isTemp() || op.isUndefined())
      op.setTemp(dword_temp(op.getTemp()));
   else if (convert_const && op.isConstant() && op.bytes() < 4)
      op = Operand::c32(op.constantValue());
   return op;
}

struct op_info {
   Operand op;
   unsigned offset; /* byte offset into op. */
   unsigned bytes;  /* how many bytes to use after offset. */
};

void
emit_pack(Builder& bld, Definition def, std::vector<op_info> operands)
{
   assert(def.regClass().type() == RegType::vgpr);

   /* split definition into dwords. */
   if (def.size() > 1) {
      aco_ptr<Instruction> vec{
         create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, def.size(), 1)};
      vec->definitions[0] = def;

      unsigned op_idx = 0;
      for (unsigned i = 0; i < def.size(); i++) {
         std::vector<op_info> sub_operands;
         Definition sub_def = bld.def(v1);
         vec->operands[i] = Operand(sub_def.getTemp());
         unsigned sub_bytes = 0;
         while (sub_bytes < 4) {
            unsigned new_bytes = MIN2(operands[op_idx].bytes, 4 - sub_bytes);
            sub_bytes += new_bytes;

            sub_operands.push_back({operands[op_idx].op, operands[op_idx].offset, new_bytes});

            if (new_bytes == operands[op_idx].bytes) {
               op_idx++;
               if (op_idx >= operands.size())
                  break;
            } else {
               operands[op_idx].offset += new_bytes;
               operands[op_idx].bytes -= new_bytes;
            }
         }

         emit_pack(bld, sub_def, std::move(sub_operands));
      }

      bld.insert(std::move(vec));
      return;
   }

   /* split operands into dwords. */
   for (unsigned i = 0; i < operands.size(); i++) {
      Operand op = operands[i].op;
      unsigned offset = operands[i].offset;
      unsigned bytes = operands[i].bytes;

      if (op.isUndefined() || op.isConstant()) {
         if (op.isConstant())
            operands[i].op = Operand::c32(op.constantValue64() >> (offset * 8));
         else
            operands[i].op = Operand(v1);
         operands[i].offset = 0;
         continue;
      }

      if (op.size() == 1)
         continue;

      assert(!op.isFixed());

      RegClass rc = op.isOfType(RegType::vgpr) ? v1 : s1;

      aco_ptr<Instruction> split{
         create_instruction(aco_opcode::p_split_vector, Format::PSEUDO, 1, op.size())};
      split->operands[0] = op;
      for (unsigned j = 0; j < op.size(); j++)
         split->definitions[j] = bld.def(rc);

      unsigned dword_off = offset / 4;
      unsigned new_bytes = MIN2(4 - (offset % 4), bytes);
      operands[i].op = Operand(split->definitions[dword_off++].getTemp());
      operands[i].offset = offset % 4;
      operands[i].bytes = new_bytes;
      if (new_bytes != bytes) {
         i++;
         operands.insert(
            std::next(operands.begin(), i),
            {Operand(split->definitions[dword_off++].getTemp()), 0, bytes - new_bytes});
      }

      bld.insert(std::move(split));
   }

   /* remove undef operands */
   for (unsigned i = 0; i < operands.size(); i++) {
      Operand op = operands[i].op;
      unsigned bytes = operands[i].bytes;
      if (!op.isUndefined())
         continue;

      if (i != operands.size() - 1) {
         unsigned offset = operands[i + 1].offset;
         operands[i + 1].offset -= MIN2(offset, bytes);
         bytes -= MIN2(offset, bytes);
      }

      if (i != 0) {
         unsigned rem = 4 - (operands[i - 1].bytes + operands[i - 1].offset);
         operands[i - 1].bytes += MIN2(rem, bytes);
         bytes -= MIN2(rem, bytes);
      }

      if (bytes == 0) {
         operands.erase(std::next(operands.begin(), i));
         i--;
      } else {
         operands[i].op = Operand::c32(0);
         operands[i].bytes = bytes;
      }
   }

   /* combine constant operands */
   for (unsigned i = 1; i < operands.size(); i++) {
      if (!operands[i].op.isConstant())
         continue;
      assert(operands[i].offset == 0);

      if (!operands[i - 1].op.isConstant())
         continue;

      unsigned bytes = operands[i - 1].bytes;
      uint32_t prev = operands[i - 1].op.constantValue() & BITFIELD_MASK(bytes * 8);
      uint32_t current = operands[i].op.constantValue() << (bytes * 8);

      operands[i - 1].op = Operand::c32(prev | current);
      operands[i - 1].bytes += operands[i].bytes;
      operands.erase(std::next(operands.begin(), i));
      i--;
   }

   if (operands.size() == 1) {
      Operand op = operands[0].op;
      unsigned offset = operands[0].offset;
      if (offset != 0) {
         if (op.isOfType(RegType::vgpr))
            bld.vop2(aco_opcode::v_lshrrev_b32, def, Operand::c32(offset * 8), op);
         else
            bld.vop2_e64(aco_opcode::v_lshrrev_b32, def, Operand::c32(offset * 8), op);
      } else {
         bld.copy(def, op);
      }
      return;
   }

   Operand curr = operands[0].op;
   unsigned shift = (4 - (operands[0].bytes + operands[0].offset)) * 8;
   if (shift != 0) {
      if (curr.isConstant())
         curr = Operand::c32(curr.constantValue() << shift);
      else if (curr.isOfType(RegType::vgpr))
         curr = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(shift), curr);
      else
         curr = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), curr,
                         Operand::c32(shift));
   }

   if (curr.isLiteral())
      curr = bld.copy(bld.def(s1), curr);

   unsigned packed_bytes = operands[0].bytes;
   for (unsigned i = 1; i < operands.size(); i++) {
      Operand op = operands[i].op;
      unsigned offset = operands[i].offset;

      if (offset) {
         if (op.isOfType(RegType::vgpr))
            op = bld.vop2(aco_opcode::v_lshrrev_b32, bld.def(v1), Operand::c32(offset * 8), op);
         else
            op = bld.sop2(aco_opcode::s_lshr_b32, bld.def(s1), bld.def(s1, scc), op,
                          Operand::c32(offset * 8));
      }

      if (curr.isOfType(RegType::sgpr) && (op.isOfType(RegType::sgpr) || op.isLiteral()))
         op = bld.copy(bld.def(v1), op);
      else if (op.isLiteral())
         op = bld.copy(bld.def(s1), op);

      Definition next = i + 1 == operands.size() ? def : bld.def(v1);
      unsigned bytes = i + 1 == operands.size() ? 4 - packed_bytes : operands[i].bytes;
      curr = bld.vop3(aco_opcode::v_alignbyte_b32, next, op, curr, Operand::c32(bytes));
      packed_bytes += bytes;
   }
}

void
emit_split_vector(Builder& bld, aco_ptr<Instruction>& instr)
{
   bool needs_lowering = false;
   for (Definition& def : instr->definitions)
      needs_lowering |= def.regClass().is_subdword();

   if (!needs_lowering) {
      bld.insert(std::move(instr));
      return;
   }

   std::vector<op_info> operands = {{dword_op(instr->operands[0], true), 0, 0}};
   for (Definition& def : instr->definitions) {
      operands[0].bytes = def.bytes();
      emit_pack(bld, dword_def(bld.program, def), operands);
      operands[0].offset += def.bytes();
   }
}

void
emit_create_vector(Builder& bld, aco_ptr<Instruction>& instr)
{
   instr->definitions[0] = dword_def(bld.program, instr->definitions[0]);
   bool needs_lowering = false;
   for (Operand& op : instr->operands)
      needs_lowering |= (op.hasRegClass() && op.regClass().is_subdword()) || op.bytes() < 4;

   if (!needs_lowering) {
      bld.insert(std::move(instr));
      return;
   }

   std::vector<op_info> operands;
   operands.reserve(instr->operands.size());
   for (const Operand& op : instr->operands)
      operands.push_back({dword_op(op, true), 0, op.bytes()});

   emit_pack(bld, instr->definitions[0], std::move(operands));
}

void
process_block(Program* program, Block* block)
{
   std::vector<aco_ptr<Instruction>> instructions;
   instructions.reserve(block->instructions.size());

   Builder bld(program, &instructions);
   for (unsigned idx = 0; idx < block->instructions.size(); idx++) {
      aco_ptr<Instruction> instr = std::move(block->instructions[idx]);

      if (instr->opcode == aco_opcode::p_split_vector) {
         emit_split_vector(bld, instr);
      } else if (instr->opcode == aco_opcode::p_create_vector) {
         emit_create_vector(bld, instr);
      } else if (instr->opcode == aco_opcode::p_extract_vector &&
                 instr->definitions[0].regClass().is_subdword()) {
         const Definition& def = instr->definitions[0];
         unsigned offset = def.bytes() * instr->operands[1].constantValue();
         std::vector<op_info> operands = {
            {dword_op(instr->operands[0], true), offset, def.bytes()}};
         emit_pack(bld, dword_def(program, def), std::move(operands));
      } else {
         for (Definition& def : instr->definitions)
            def = dword_def(program, def);

         for (Operand& op : instr->operands)
            op = dword_op(op, instr->isPseudo());

         bld.insert(std::move(instr));
      }
   }

   block->instructions = std::move(instructions);
}

} /* end namespace */

void
lower_subdword(Program* program)
{
   for (Block& block : program->blocks)
      process_block(program, &block);
}

} /* end namespace aco */
