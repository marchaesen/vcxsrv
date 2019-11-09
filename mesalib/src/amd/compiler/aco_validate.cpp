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

#include <array>
#include <map>

namespace aco {

#ifndef NDEBUG
void perfwarn(bool cond, const char *msg, Instruction *instr)
{
   if (cond) {
      fprintf(stderr, "ACO performance warning: %s\n", msg);
      if (instr) {
         fprintf(stderr, "instruction: ");
         aco_print_instr(instr, stderr);
         fprintf(stderr, "\n");
      }

      if (debug_flags & DEBUG_PERFWARN)
         exit(1);
   }
}
#endif

void validate(Program* program, FILE * output)
{
   if (!(debug_flags & DEBUG_VALIDATE))
      return;

   bool is_valid = true;
   auto check = [&output, &is_valid](bool check, const char * msg, aco::Instruction * instr) -> void {
      if (!check) {
         fprintf(output, "%s: ", msg);
         aco_print_instr(instr, output);
         fprintf(output, "\n");
         is_valid = false;
      }
   };

   for (Block& block : program->blocks) {
      for (aco_ptr<Instruction>& instr : block.instructions) {

         /* check base format */
         Format base_format = instr->format;
         base_format = (Format)((uint32_t)base_format & ~(uint32_t)Format::SDWA);
         base_format = (Format)((uint32_t)base_format & ~(uint32_t)Format::DPP);
         if ((uint32_t)base_format & (uint32_t)Format::VOP1)
            base_format = Format::VOP1;
         else if ((uint32_t)base_format & (uint32_t)Format::VOP2)
            base_format = Format::VOP2;
         else if ((uint32_t)base_format & (uint32_t)Format::VOPC)
            base_format = Format::VOPC;
         else if ((uint32_t)base_format & (uint32_t)Format::VINTRP)
            base_format = Format::VINTRP;
         check(base_format == instr_info.format[(int)instr->opcode], "Wrong base format for instruction", instr.get());

         /* check VOP3 modifiers */
         if (((uint32_t)instr->format & (uint32_t)Format::VOP3) && instr->format != Format::VOP3) {
            check(base_format == Format::VOP2 ||
                  base_format == Format::VOP1 ||
                  base_format == Format::VOPC ||
                  base_format == Format::VINTRP,
                  "Format cannot have VOP3A/VOP3B applied", instr.get());
         }

         /* check for undefs */
         for (unsigned i = 0; i < instr->operands.size(); i++) {
            if (instr->operands[i].isUndefined()) {
               bool flat = instr->format == Format::FLAT || instr->format == Format::SCRATCH || instr->format == Format::GLOBAL;
               bool can_be_undef = is_phi(instr) || instr->format == Format::EXP ||
                                   instr->format == Format::PSEUDO_REDUCTION ||
                                   (flat && i == 1) || (instr->format == Format::MIMG && i == 2) ||
                                   ((instr->format == Format::MUBUF || instr->format == Format::MTBUF) && i == 0);
               check(can_be_undef, "Undefs can only be used in certain operands", instr.get());
            }
         }

         /* check num literals */
         if (instr->isSALU() || instr->isVALU()) {
            unsigned num_literals = 0;
            for (unsigned i = 0; i < instr->operands.size(); i++)
            {
               if (instr->operands[i].isLiteral() && instr->isVOP3() && program->chip_class >= GFX10) {
                  num_literals++;
               } else if (instr->operands[i].isLiteral()) {
                  check(instr->format == Format::SOP1 ||
                        instr->format == Format::SOP2 ||
                        instr->format == Format::SOPC ||
                        instr->format == Format::VOP1 ||
                        instr->format == Format::VOP2 ||
                        instr->format == Format::VOPC,
                        "Literal applied on wrong instruction format", instr.get());

                  num_literals++;
                  check(!instr->isVALU() || i == 0 || i == 2, "Wrong source position for Literal argument", instr.get());
               }
            }
            check(num_literals <= 1, "Only 1 Literal allowed", instr.get());

            /* check num sgprs for VALU */
            if (instr->isVALU()) {
               check(instr->definitions[0].getTemp().type() == RegType::vgpr ||
                     (int) instr->format & (int) Format::VOPC ||
                     instr->opcode == aco_opcode::v_readfirstlane_b32 ||
                     instr->opcode == aco_opcode::v_readlane_b32,
                     "Wrong Definition type for VALU instruction", instr.get());
               unsigned num_sgpr = 0;
               unsigned sgpr_idx = instr->operands.size();
               for (unsigned i = 0; i < instr->operands.size(); i++)
               {
                  if (instr->operands[i].isTemp() && instr->operands[i].regClass().type() == RegType::sgpr) {
                     check(i != 1 || (int) instr->format & (int) Format::VOP3A, "Wrong source position for SGPR argument", instr.get());

                     if (sgpr_idx == instr->operands.size() || instr->operands[sgpr_idx].tempId() != instr->operands[i].tempId())
                        num_sgpr++;
                     sgpr_idx = i;
                  }

                  if (instr->operands[i].isConstant() && !instr->operands[i].isLiteral())
                     check(i == 0 || (int) instr->format & (int) Format::VOP3A, "Wrong source position for constant argument", instr.get());
               }
               check(num_sgpr + num_literals <= 1, "Only 1 Literal OR 1 SGPR allowed", instr.get());
            }

            if (instr->format == Format::SOP1 || instr->format == Format::SOP2) {
               check(instr->definitions[0].getTemp().type() == RegType::sgpr, "Wrong Definition type for SALU instruction", instr.get());
               for (const Operand& op : instr->operands) {
                 check(op.isConstant() || op.regClass().type() <= RegType::sgpr,
                       "Wrong Operand type for SALU instruction", instr.get());
            }
         }
         }

         switch (instr->format) {
         case Format::PSEUDO: {
            if (instr->opcode == aco_opcode::p_create_vector) {
               unsigned size = 0;
               for (const Operand& op : instr->operands) {
                  size += op.size();
               }
               check(size == instr->definitions[0].size(), "Definition size does not match operand sizes", instr.get());
               if (instr->definitions[0].getTemp().type() == RegType::sgpr) {
                  for (const Operand& op : instr->operands) {
                     check(op.isConstant() || op.regClass().type() == RegType::sgpr,
                           "Wrong Operand type for scalar vector", instr.get());
                  }
               }
            } else if (instr->opcode == aco_opcode::p_extract_vector) {
               check((instr->operands[0].isTemp()) && instr->operands[1].isConstant(), "Wrong Operand types", instr.get());
               check(instr->operands[1].constantValue() < instr->operands[0].size(), "Index out of range", instr.get());
               check(instr->definitions[0].getTemp().type() == RegType::vgpr || instr->operands[0].regClass().type() == RegType::sgpr,
                     "Cannot extract SGPR value from VGPR vector", instr.get());
            } else if (instr->opcode == aco_opcode::p_parallelcopy) {
               check(instr->definitions.size() == instr->operands.size(), "Number of Operands does not match number of Definitions", instr.get());
               for (unsigned i = 0; i < instr->operands.size(); i++) {
                  if (instr->operands[i].isTemp())
                     check((instr->definitions[i].getTemp().type() == instr->operands[i].regClass().type()) ||
                           (instr->definitions[i].getTemp().type() == RegType::vgpr && instr->operands[i].regClass().type() == RegType::sgpr),
                           "Operand and Definition types do not match", instr.get());
               }
            } else if (instr->opcode == aco_opcode::p_phi) {
               check(instr->operands.size() == block.logical_preds.size(), "Number of Operands does not match number of predecessors", instr.get());
               check(instr->definitions[0].getTemp().type() == RegType::vgpr || instr->definitions[0].getTemp().regClass() == s2, "Logical Phi Definition must be vgpr or divergent boolean", instr.get());
            } else if (instr->opcode == aco_opcode::p_linear_phi) {
               for (const Operand& op : instr->operands)
                  check(!op.isTemp() || op.getTemp().is_linear(), "Wrong Operand type", instr.get());
               check(instr->operands.size() == block.linear_preds.size(), "Number of Operands does not match number of predecessors", instr.get());
            }
            break;
         }
         case Format::SMEM: {
            if (instr->operands.size() >= 1)
               check(instr->operands[0].isTemp() && instr->operands[0].regClass().type() == RegType::sgpr, "SMEM operands must be sgpr", instr.get());
            if (instr->operands.size() >= 2)
               check(instr->operands[1].isConstant() || (instr->operands[1].isTemp() && instr->operands[1].regClass().type() == RegType::sgpr),
                     "SMEM offset must be constant or sgpr", instr.get());
            if (!instr->definitions.empty())
               check(instr->definitions[0].getTemp().type() == RegType::sgpr, "SMEM result must be sgpr", instr.get());
            break;
         }
         case Format::MTBUF:
         case Format::MUBUF:
         case Format::MIMG: {
            check(instr->operands.size() > 1, "VMEM instructions must have at least one operand", instr.get());
            check(instr->operands[0].hasRegClass() && instr->operands[0].regClass().type() == RegType::vgpr,
                  "VADDR must be in vgpr for VMEM instructions", instr.get());
            check(instr->operands[1].isTemp() && instr->operands[1].regClass().type() == RegType::sgpr, "VMEM resource constant must be sgpr", instr.get());
            check(instr->operands.size() < 4 || (instr->operands[3].isTemp() && instr->operands[3].regClass().type() == RegType::vgpr), "VMEM write data must be vgpr", instr.get());
            break;
         }
         case Format::DS: {
            for (const Operand& op : instr->operands) {
               check((op.isTemp() && op.regClass().type() == RegType::vgpr) || op.physReg() == m0,
                     "Only VGPRs are valid DS instruction operands", instr.get());
            }
            if (!instr->definitions.empty())
               check(instr->definitions[0].getTemp().type() == RegType::vgpr, "DS instruction must return VGPR", instr.get());
            break;
         }
         case Format::EXP: {
            for (unsigned i = 0; i < 4; i++)
               check(instr->operands[i].hasRegClass() && instr->operands[i].regClass().type() == RegType::vgpr,
                     "Only VGPRs are valid Export arguments", instr.get());
            break;
         }
         case Format::FLAT:
            check(instr->operands[1].isUndefined(), "Flat instructions don't support SADDR", instr.get());
            /* fallthrough */
         case Format::GLOBAL:
         case Format::SCRATCH: {
            check(instr->operands[0].isTemp() && instr->operands[0].regClass().type() == RegType::vgpr, "FLAT/GLOBAL/SCRATCH address must be vgpr", instr.get());
            check(instr->operands[1].hasRegClass() && instr->operands[1].regClass().type() == RegType::sgpr,
                  "FLAT/GLOBAL/SCRATCH sgpr address must be undefined or sgpr", instr.get());
            if (!instr->definitions.empty())
               check(instr->definitions[0].getTemp().type() == RegType::vgpr, "FLAT/GLOBAL/SCRATCH result must be vgpr", instr.get());
            else
               check(instr->operands[2].regClass().type() == RegType::vgpr, "FLAT/GLOBAL/SCRATCH data must be vgpr", instr.get());
            break;
         }
         default:
            break;
         }
      }
   }
   assert(is_valid);
}

/* RA validation */
namespace {

struct Location {
   Location() : block(NULL), instr(NULL) {}

   Block *block;
   Instruction *instr; //NULL if it's the block's live-in
};

struct Assignment {
   Location defloc;
   Location firstloc;
   PhysReg reg;
};

bool ra_fail(FILE *output, Location loc, Location loc2, const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   char msg[1024];
   vsprintf(msg, fmt, args);
   va_end(args);

   fprintf(stderr, "RA error found at instruction in BB%d:\n", loc.block->index);
   if (loc.instr) {
      aco_print_instr(loc.instr, stderr);
      fprintf(stderr, "\n%s", msg);
   } else {
      fprintf(stderr, "%s", msg);
   }
   if (loc2.block) {
      fprintf(stderr, " in BB%d:\n", loc2.block->index);
      aco_print_instr(loc2.instr, stderr);
   }
   fprintf(stderr, "\n\n");

   return true;
}

} /* end namespace */

bool validate_ra(Program *program, const struct radv_nir_compiler_options *options, FILE *output) {
   if (!(debug_flags & DEBUG_VALIDATE_RA))
      return false;

   bool err = false;
   aco::live live_vars = aco::live_var_analysis(program, options);
   std::vector<std::vector<Temp>> phi_sgpr_ops(program->blocks.size());

   std::map<unsigned, Assignment> assignments;
   for (Block& block : program->blocks) {
      Location loc;
      loc.block = &block;
      for (aco_ptr<Instruction>& instr : block.instructions) {
         if (instr->opcode == aco_opcode::p_phi) {
            for (unsigned i = 0; i < instr->operands.size(); i++) {
               if (instr->operands[i].isTemp() &&
                   instr->operands[i].getTemp().type() == RegType::sgpr &&
                   instr->operands[i].isFirstKill())
                  phi_sgpr_ops[block.logical_preds[i]].emplace_back(instr->operands[i].getTemp());
            }
         }

         loc.instr = instr.get();
         for (unsigned i = 0; i < instr->operands.size(); i++) {
            Operand& op = instr->operands[i];
            if (!op.isTemp())
               continue;
            if (!op.isFixed())
               err |= ra_fail(output, loc, Location(), "Operand %d is not assigned a register", i);
            if (assignments.count(op.tempId()) && assignments[op.tempId()].reg != op.physReg())
               err |= ra_fail(output, loc, assignments.at(op.tempId()).firstloc, "Operand %d has an inconsistent register assignment with instruction", i);
            if ((op.getTemp().type() == RegType::vgpr && op.physReg() + op.size() > 256 + program->config->num_vgprs) ||
                (op.getTemp().type() == RegType::sgpr && op.physReg() + op.size() > program->config->num_sgprs && op.physReg() < program->sgpr_limit))
               err |= ra_fail(output, loc, assignments.at(op.tempId()).firstloc, "Operand %d has an out-of-bounds register assignment", i);
            if (!assignments[op.tempId()].firstloc.block)
               assignments[op.tempId()].firstloc = loc;
            if (!assignments[op.tempId()].defloc.block)
               assignments[op.tempId()].reg = op.physReg();
         }

         for (unsigned i = 0; i < instr->definitions.size(); i++) {
            Definition& def = instr->definitions[i];
            if (!def.isTemp())
               continue;
            if (!def.isFixed())
               err |= ra_fail(output, loc, Location(), "Definition %d is not assigned a register", i);
            if (assignments[def.tempId()].defloc.block)
               err |= ra_fail(output, loc, assignments.at(def.tempId()).defloc, "Temporary %%%d also defined by instruction", def.tempId());
            if ((def.getTemp().type() == RegType::vgpr && def.physReg() + def.size() > 256 + program->config->num_vgprs) ||
                (def.getTemp().type() == RegType::sgpr && def.physReg() + def.size() > program->config->num_sgprs && def.physReg() < program->sgpr_limit))
               err |= ra_fail(output, loc, assignments.at(def.tempId()).firstloc, "Definition %d has an out-of-bounds register assignment", i);
            if (!assignments[def.tempId()].firstloc.block)
               assignments[def.tempId()].firstloc = loc;
            assignments[def.tempId()].defloc = loc;
            assignments[def.tempId()].reg = def.physReg();
         }
      }
   }

   for (Block& block : program->blocks) {
      Location loc;
      loc.block = &block;

      std::array<unsigned, 512> regs;
      regs.fill(0);

      std::set<Temp> live;
      live.insert(live_vars.live_out[block.index].begin(), live_vars.live_out[block.index].end());
      /* remove killed p_phi sgpr operands */
      for (Temp tmp : phi_sgpr_ops[block.index])
         live.erase(tmp);

      /* check live out */
      for (Temp tmp : live) {
         PhysReg reg = assignments.at(tmp.id()).reg;
         for (unsigned i = 0; i < tmp.size(); i++) {
            if (regs[reg + i]) {
               err |= ra_fail(output, loc, Location(), "Assignment of element %d of %%%d already taken by %%%d in live-out", i, tmp.id(), regs[reg + i]);
            }
            regs[reg + i] = tmp.id();
         }
      }
      regs.fill(0);

      for (auto it = block.instructions.rbegin(); it != block.instructions.rend(); ++it) {
         aco_ptr<Instruction>& instr = *it;

         /* check killed p_phi sgpr operands */
         if (instr->opcode == aco_opcode::p_logical_end) {
            for (Temp tmp : phi_sgpr_ops[block.index]) {
               PhysReg reg = assignments.at(tmp.id()).reg;
               for (unsigned i = 0; i < tmp.size(); i++) {
                  if (regs[reg + i])
                     err |= ra_fail(output, loc, Location(), "Assignment of element %d of %%%d already taken by %%%d in live-out", i, tmp.id(), regs[reg + i]);
               }
               live.emplace(tmp);
            }
         }

         for (const Definition& def : instr->definitions) {
            if (!def.isTemp())
               continue;
            live.erase(def.getTemp());
         }

         /* don't count phi operands as live-in, since they are actually
          * killed when they are copied at the predecessor */
         if (instr->opcode != aco_opcode::p_phi && instr->opcode != aco_opcode::p_linear_phi) {
            for (const Operand& op : instr->operands) {
               if (!op.isTemp())
                  continue;
               live.insert(op.getTemp());
            }
         }
      }

      for (Temp tmp : live) {
         PhysReg reg = assignments.at(tmp.id()).reg;
         for (unsigned i = 0; i < tmp.size(); i++)
            regs[reg + i] = tmp.id();
      }

      for (aco_ptr<Instruction>& instr : block.instructions) {
         loc.instr = instr.get();

         /* remove killed p_phi operands from regs */
         if (instr->opcode == aco_opcode::p_logical_end) {
            for (Temp tmp : phi_sgpr_ops[block.index]) {
               PhysReg reg = assignments.at(tmp.id()).reg;
               regs[reg] = 0;
            }
         }

         if (instr->opcode != aco_opcode::p_phi && instr->opcode != aco_opcode::p_linear_phi) {
            for (const Operand& op : instr->operands) {
               if (!op.isTemp())
                  continue;
               if (op.isFirstKill()) {
                  for (unsigned j = 0; j < op.getTemp().size(); j++)
                     regs[op.physReg() + j] = 0;
               }
            }
         }

         for (unsigned i = 0; i < instr->definitions.size(); i++) {
            Definition& def = instr->definitions[i];
            if (!def.isTemp())
               continue;
            Temp tmp = def.getTemp();
            PhysReg reg = assignments.at(tmp.id()).reg;
            for (unsigned j = 0; j < tmp.size(); j++) {
               if (regs[reg + j])
                  err |= ra_fail(output, loc, assignments.at(regs[reg + i]).defloc, "Assignment of element %d of %%%d already taken by %%%d from instruction", i, tmp.id(), regs[reg + j]);
               regs[reg + j] = tmp.id();
            }
         }

         for (const Definition& def : instr->definitions) {
            if (!def.isTemp())
               continue;
            if (def.isKill()) {
               for (unsigned j = 0; j < def.getTemp().size(); j++)
                  regs[def.physReg() + j] = 0;
            }
         }
      }
   }

   return err;
}
}
