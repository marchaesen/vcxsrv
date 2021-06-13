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

#include "util/memstream.h"

namespace aco {

static void aco_log(Program *program, enum radv_compiler_debug_level level,
                    const char *prefix, const char *file, unsigned line,
                    const char *fmt, va_list args)
{
   char *msg;

   msg = ralloc_strdup(NULL, prefix);

   ralloc_asprintf_append(&msg, "    In file %s:%u\n", file, line);
   ralloc_asprintf_append(&msg, "    ");
   ralloc_vasprintf_append(&msg, fmt, args);

   if (program->debug.func)
      program->debug.func(program->debug.private_data, level, msg);

   fprintf(stderr, "%s\n", msg);

   ralloc_free(msg);
}

void _aco_perfwarn(Program *program, const char *file, unsigned line,
                   const char *fmt, ...)
{
   va_list args;

   va_start(args, fmt);
   aco_log(program, RADV_COMPILER_DEBUG_LEVEL_PERFWARN,
           "ACO PERFWARN:\n", file, line, fmt, args);
   va_end(args);
}

void _aco_err(Program *program, const char *file, unsigned line,
              const char *fmt, ...)
{
   va_list args;

   va_start(args, fmt);
   aco_log(program, RADV_COMPILER_DEBUG_LEVEL_ERROR,
           "ACO ERROR:\n", file, line, fmt, args);
   va_end(args);
}

bool validate_ir(Program* program)
{
   bool is_valid = true;
   auto check = [&program, &is_valid](bool success, const char * msg, aco::Instruction * instr) -> void {
      if (!success) {
         char *out;
         size_t outsize;
         struct u_memstream mem;
         u_memstream_open(&mem, &out, &outsize);
         FILE *const memf = u_memstream_get(&mem);

         fprintf(memf, "%s: ", msg);
         aco_print_instr(instr, memf);
         u_memstream_close(&mem);

         aco_err(program, "%s", out);
         free(out);

         is_valid = false;
      }
   };

   auto check_block = [&program, &is_valid](bool success, const char * msg, aco::Block * block) -> void {
      if (!success) {
         aco_err(program, "%s: BB%u", msg, block->index);
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
         else if ((uint32_t)base_format & (uint32_t)Format::VINTRP) {
            if (instr->opcode == aco_opcode::v_interp_p1ll_f16 ||
                instr->opcode == aco_opcode::v_interp_p1lv_f16 ||
                instr->opcode == aco_opcode::v_interp_p2_legacy_f16 ||
                instr->opcode == aco_opcode::v_interp_p2_f16) {
               /* v_interp_*_fp16 are considered VINTRP by the compiler but
                * they are emitted as VOP3.
                */
               base_format = Format::VOP3;
            } else {
               base_format = Format::VINTRP;
            }
         }
         check(base_format == instr_info.format[(int)instr->opcode], "Wrong base format for instruction", instr.get());

         /* check VOP3 modifiers */
         if (instr->isVOP3() && instr->format != Format::VOP3) {
            check(base_format == Format::VOP2 ||
                  base_format == Format::VOP1 ||
                  base_format == Format::VOPC ||
                  base_format == Format::VINTRP,
                  "Format cannot have VOP3/VOP3B applied", instr.get());
         }

         /* check SDWA */
         if (instr->isSDWA()) {
            check(base_format == Format::VOP2 ||
                  base_format == Format::VOP1 ||
                  base_format == Format::VOPC,
                  "Format cannot have SDWA applied", instr.get());

            check(program->chip_class >= GFX8, "SDWA is GFX8+ only", instr.get());

            SDWA_instruction& sdwa = instr->sdwa();
            check(sdwa.omod == 0 || program->chip_class >= GFX9, "SDWA omod only supported on GFX9+", instr.get());
            if (base_format == Format::VOPC) {
               check(sdwa.clamp == false || program->chip_class == GFX8, "SDWA VOPC clamp only supported on GFX8", instr.get());
               check((instr->definitions[0].isFixed() && instr->definitions[0].physReg() == vcc) ||
                     program->chip_class >= GFX9,
                     "SDWA+VOPC definition must be fixed to vcc on GFX8", instr.get());
            }

            if (instr->operands.size() >= 3) {
               check(instr->operands[2].isFixed() && instr->operands[2].physReg() == vcc,
                     "3rd operand must be fixed to vcc with SDWA", instr.get());
            }
            if (instr->definitions.size() >= 2) {
               check(instr->definitions[1].isFixed() && instr->definitions[1].physReg() == vcc,
                     "2nd definition must be fixed to vcc with SDWA", instr.get());
            }

            check(instr->opcode != aco_opcode::v_madmk_f32 &&
                  instr->opcode != aco_opcode::v_madak_f32 &&
                  instr->opcode != aco_opcode::v_madmk_f16 &&
                  instr->opcode != aco_opcode::v_madak_f16 &&
                  instr->opcode != aco_opcode::v_readfirstlane_b32 &&
                  instr->opcode != aco_opcode::v_clrexcp &&
                  instr->opcode != aco_opcode::v_swap_b32,
                  "SDWA can't be used with this opcode", instr.get());
            if (program->chip_class != GFX8) {
               check(instr->opcode != aco_opcode::v_mac_f32 &&
                     instr->opcode != aco_opcode::v_mac_f16 &&
                     instr->opcode != aco_opcode::v_fmac_f32 &&
                     instr->opcode != aco_opcode::v_fmac_f16,
                     "SDWA can't be used with this opcode", instr.get());
            }

            if (instr->definitions[0].regClass().is_subdword())
               check((sdwa.dst_sel & sdwa_asuint) == (sdwa_isra | instr->definitions[0].bytes()), "Unexpected SDWA sel for sub-dword definition", instr.get());
         }

         /* check opsel */
         if (instr->isVOP3()) {
            VOP3_instruction& vop3 = instr->vop3();
            check(vop3.opsel == 0 || program->chip_class >= GFX9, "Opsel is only supported on GFX9+", instr.get());

            for (unsigned i = 0; i < 3; i++) {
               if (i >= instr->operands.size() ||
                   (instr->operands[i].hasRegClass() && instr->operands[i].regClass().is_subdword() && !instr->operands[i].isFixed()))
                  check((vop3.opsel & (1 << i)) == 0, "Unexpected opsel for operand", instr.get());
            }
            if (instr->definitions[0].regClass().is_subdword() && !instr->definitions[0].isFixed())
               check((vop3.opsel & (1 << 3)) == 0, "Unexpected opsel for sub-dword definition", instr.get());
         }

         /* check for undefs */
         for (unsigned i = 0; i < instr->operands.size(); i++) {
            if (instr->operands[i].isUndefined()) {
               bool flat = instr->isFlatLike();
               bool can_be_undef = is_phi(instr) || instr->isEXP() ||
                                   instr->isReduction() ||
                                   instr->opcode == aco_opcode::p_create_vector ||
                                   (flat && i == 1) || (instr->isMIMG() && (i == 1 || i == 2)) ||
                                   ((instr->isMUBUF() || instr->isMTBUF()) && i == 1);
               check(can_be_undef, "Undefs can only be used in certain operands", instr.get());
            } else {
               check(instr->operands[i].isFixed() || instr->operands[i].isTemp() || instr->operands[i].isConstant(), "Uninitialized Operand", instr.get());
            }
         }

         /* check subdword definitions */
         for (unsigned i = 0; i < instr->definitions.size(); i++) {
            if (instr->definitions[i].regClass().is_subdword())
               check(instr->isPseudo() || instr->definitions[i].bytes() <= 4, "Only Pseudo instructions can write subdword registers larger than 4 bytes", instr.get());
         }

         if (instr->isSALU() || instr->isVALU()) {
            /* check literals */
            Operand literal(s1);
            for (unsigned i = 0; i < instr->operands.size(); i++)
            {
               Operand op = instr->operands[i];
               if (!op.isLiteral())
                  continue;

               check(instr->isSOP1() || instr->isSOP2() || instr->isSOPC() ||
                     instr->isVOP1() || instr->isVOP2() || instr->isVOPC() ||
                     (instr->isVOP3() && program->chip_class >= GFX10) ||
                     (instr->isVOP3P() && program->chip_class >= GFX10),
                     "Literal applied on wrong instruction format", instr.get());

               check(literal.isUndefined() || (literal.size() == op.size() && literal.constantValue() == op.constantValue()), "Only 1 Literal allowed", instr.get());
               literal = op;
               check(instr->isSALU() || instr->isVOP3() || instr->isVOP3P() || i == 0 || i == 2, "Wrong source position for Literal argument", instr.get());
            }

            /* check num sgprs for VALU */
            if (instr->isVALU()) {
               bool is_shift64 = instr->opcode == aco_opcode::v_lshlrev_b64 ||
                                 instr->opcode == aco_opcode::v_lshrrev_b64 ||
                                 instr->opcode == aco_opcode::v_ashrrev_i64;
               unsigned const_bus_limit = 1;
               if (program->chip_class >= GFX10 && !is_shift64)
                  const_bus_limit = 2;

               uint32_t scalar_mask = instr->isVOP3() || instr->isVOP3P() ? 0x7 : 0x5;
               if (instr->isSDWA())
                  scalar_mask = program->chip_class >= GFX9 ? 0x7 : 0x4;

               if (instr->isVOPC() ||
                   instr->opcode == aco_opcode::v_readfirstlane_b32 ||
                   instr->opcode == aco_opcode::v_readlane_b32 ||
                   instr->opcode == aco_opcode::v_readlane_b32_e64) {
                  check(instr->definitions[0].getTemp().type() == RegType::sgpr,
                        "Wrong Definition type for VALU instruction", instr.get());
               } else {
                  check(instr->definitions[0].getTemp().type() == RegType::vgpr,
                        "Wrong Definition type for VALU instruction", instr.get());
               }

               unsigned num_sgprs = 0;
               unsigned sgpr[] = {0, 0};
               for (unsigned i = 0; i < instr->operands.size(); i++)
               {
                  Operand op = instr->operands[i];
                  if (instr->opcode == aco_opcode::v_readfirstlane_b32 ||
                      instr->opcode == aco_opcode::v_readlane_b32 ||
                      instr->opcode == aco_opcode::v_readlane_b32_e64) {
                     check(i != 1 ||
                           (op.isTemp() && op.regClass().type() == RegType::sgpr) ||
                           op.isConstant(),
                           "Must be a SGPR or a constant", instr.get());
                     check(i == 1 ||
                           (op.isTemp() && op.regClass().type() == RegType::vgpr && op.bytes() <= 4),
                           "Wrong Operand type for VALU instruction", instr.get());
                     continue;
                  }

                  if (instr->opcode == aco_opcode::v_writelane_b32 ||
                      instr->opcode == aco_opcode::v_writelane_b32_e64) {
                     check(i != 2 ||
                           (op.isTemp() && op.regClass().type() == RegType::vgpr && op.bytes() <= 4),
                           "Wrong Operand type for VALU instruction", instr.get());
                     check(i == 2 ||
                           (op.isTemp() && op.regClass().type() == RegType::sgpr) ||
                           op.isConstant(),
                           "Must be a SGPR or a constant", instr.get());
                     continue;
                  }
                  if (op.isTemp() && instr->operands[i].regClass().type() == RegType::sgpr) {
                     check(scalar_mask & (1 << i), "Wrong source position for SGPR argument", instr.get());

                     if (op.tempId() != sgpr[0] && op.tempId() != sgpr[1]) {
                        if (num_sgprs < 2)
                           sgpr[num_sgprs++] = op.tempId();
                     }
                  }

                  if (op.isConstant() && !op.isLiteral())
                     check(scalar_mask & (1 << i), "Wrong source position for constant argument", instr.get());
               }
               check(num_sgprs + (literal.isUndefined() ? 0 : 1) <= const_bus_limit, "Too many SGPRs/literals", instr.get());
            }

            if (instr->isSOP1() || instr->isSOP2()) {
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
                  check(op.bytes() < 4 || size % 4 == 0, "Operand is not aligned", instr.get());
                  size += op.bytes();
               }
               check(size == instr->definitions[0].bytes(), "Definition size does not match operand sizes", instr.get());
               if (instr->definitions[0].getTemp().type() == RegType::sgpr) {
                  for (const Operand& op : instr->operands) {
                     check(op.isConstant() || op.regClass().type() == RegType::sgpr,
                           "Wrong Operand type for scalar vector", instr.get());
                  }
               }
            } else if (instr->opcode == aco_opcode::p_extract_vector) {
               check((instr->operands[0].isTemp()) && instr->operands[1].isConstant(), "Wrong Operand types", instr.get());
               check((instr->operands[1].constantValue() + 1) * instr->definitions[0].bytes() <= instr->operands[0].bytes(), "Index out of range", instr.get());
               check(instr->definitions[0].getTemp().type() == RegType::vgpr || instr->operands[0].regClass().type() == RegType::sgpr,
                     "Cannot extract SGPR value from VGPR vector", instr.get());
               check(program->chip_class >= GFX9 || !instr->definitions[0].regClass().is_subdword() ||
                     instr->operands[0].regClass().type() == RegType::vgpr, "Cannot extract subdword from SGPR before GFX9+", instr.get());
            } else if (instr->opcode == aco_opcode::p_split_vector) {
               check(instr->operands[0].isTemp(), "Operand must be a temporary", instr.get());
               unsigned size = 0;
               for (const Definition& def : instr->definitions) {
                  size += def.bytes();
               }
               check(size == instr->operands[0].bytes(), "Operand size does not match definition sizes", instr.get());
               if (instr->operands[0].getTemp().type() == RegType::vgpr) {
                  for (const Definition& def : instr->definitions)
                     check(def.regClass().type() == RegType::vgpr, "Wrong Definition type for VGPR split_vector", instr.get());
               } else {
                  for (const Definition& def : instr->definitions)
                     check(program->chip_class >= GFX9 || !def.regClass().is_subdword(), "Cannot split SGPR into subdword VGPRs before GFX9+", instr.get());
               }
            } else if (instr->opcode == aco_opcode::p_parallelcopy) {
               check(instr->definitions.size() == instr->operands.size(), "Number of Operands does not match number of Definitions", instr.get());
               for (unsigned i = 0; i < instr->operands.size(); i++) {
                  check(instr->definitions[i].bytes() == instr->operands[i].bytes(), "Operand and Definition size must match", instr.get());
                  if (instr->operands[i].isTemp())
                     check((instr->definitions[i].getTemp().type() == instr->operands[i].regClass().type()) ||
                           (instr->definitions[i].getTemp().type() == RegType::vgpr && instr->operands[i].regClass().type() == RegType::sgpr),
                           "Operand and Definition types do not match", instr.get());
               }
            } else if (instr->opcode == aco_opcode::p_phi) {
               check(instr->operands.size() == block.logical_preds.size(), "Number of Operands does not match number of predecessors", instr.get());
               check(instr->definitions[0].getTemp().type() == RegType::vgpr, "Logical Phi Definition must be vgpr", instr.get());
            } else if (instr->opcode == aco_opcode::p_linear_phi) {
               for (const Operand& op : instr->operands)
                  check(!op.isTemp() || op.getTemp().is_linear(), "Wrong Operand type", instr.get());
               check(instr->operands.size() == block.linear_preds.size(), "Number of Operands does not match number of predecessors", instr.get());
            }
            break;
         }
         case Format::PSEUDO_REDUCTION: {
            for (const Operand &op : instr->operands)
               check(op.regClass().type() == RegType::vgpr, "All operands of PSEUDO_REDUCTION instructions must be in VGPRs.", instr.get());

            if (instr->opcode == aco_opcode::p_reduce && instr->reduction().cluster_size == program->wave_size)
               check(instr->definitions[0].regClass().type() == RegType::sgpr, "The result of unclustered reductions must go into an SGPR.", instr.get());
            else
               check(instr->definitions[0].regClass().type() == RegType::vgpr, "The result of scans and clustered reductions must go into a VGPR.", instr.get());

            break;
         }
         case Format::SMEM: {
            if (instr->operands.size() >= 1)
               check((instr->operands[0].isFixed() && !instr->operands[0].isConstant()) ||
                     (instr->operands[0].isTemp() && instr->operands[0].regClass().type() == RegType::sgpr), "SMEM operands must be sgpr", instr.get());
            if (instr->operands.size() >= 2)
               check(instr->operands[1].isConstant() || (instr->operands[1].isTemp() && instr->operands[1].regClass().type() == RegType::sgpr),
                     "SMEM offset must be constant or sgpr", instr.get());
            if (!instr->definitions.empty())
               check(instr->definitions[0].getTemp().type() == RegType::sgpr, "SMEM result must be sgpr", instr.get());
            break;
         }
         case Format::MTBUF:
         case Format::MUBUF: {
            check(instr->operands.size() > 1, "VMEM instructions must have at least one operand", instr.get());
            check(instr->operands[1].hasRegClass() && instr->operands[1].regClass().type() == RegType::vgpr,
                  "VADDR must be in vgpr for VMEM instructions", instr.get());
            check(instr->operands[0].isTemp() && instr->operands[0].regClass().type() == RegType::sgpr, "VMEM resource constant must be sgpr", instr.get());
            check(instr->operands.size() < 4 || (instr->operands[3].isTemp() && instr->operands[3].regClass().type() == RegType::vgpr), "VMEM write data must be vgpr", instr.get());
            break;
         }
         case Format::MIMG: {
            check(instr->operands.size() >= 4, "MIMG instructions must have at least 4 operands", instr.get());
            check(instr->operands[0].hasRegClass() && (instr->operands[0].regClass() == s4 || instr->operands[0].regClass() == s8),
                  "MIMG operands[0] (resource constant) must be in 4 or 8 SGPRs", instr.get());
            if (instr->operands[1].hasRegClass())
               check(instr->operands[1].regClass() == s4, "MIMG operands[1] (sampler constant) must be 4 SGPRs", instr.get());
            if (!instr->operands[2].isUndefined()) {
               bool is_cmpswap = instr->opcode == aco_opcode::image_atomic_cmpswap ||
                                 instr->opcode == aco_opcode::image_atomic_fcmpswap;
               check(instr->definitions.empty() || (instr->definitions[0].regClass() == instr->operands[2].regClass() || is_cmpswap),
                     "MIMG operands[2] (VDATA) must be the same as definitions[0] for atomics and TFE/LWE loads", instr.get());
            }
            check(instr->operands.size() == 4 || program->chip_class >= GFX10, "NSA is only supported on GFX10+", instr.get());
            for (unsigned i = 3; i < instr->operands.size(); i++) {
               if (instr->operands.size() == 4) {
                  check(instr->operands[i].hasRegClass() && instr->operands[i].regClass().type() == RegType::vgpr,
                        "MIMG operands[3] (VADDR) must be VGPR", instr.get());
               } else {
                  check(instr->operands[i].regClass() == v1, "MIMG VADDR must be v1 if NSA is used", instr.get());
               }
            }
            check(instr->definitions.empty() || (instr->definitions[0].isTemp() && instr->definitions[0].regClass().type() == RegType::vgpr),
                  "MIMG definitions[0] (VDATA) must be VGPR", instr.get());
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
            FALLTHROUGH;
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

   /* validate CFG */
   for (unsigned i = 0; i < program->blocks.size(); i++) {
      Block& block = program->blocks[i];
      check_block(block.index == i, "block.index must match actual index", &block);

      /* predecessors/successors should be sorted */
      for (unsigned j = 0; j + 1 < block.linear_preds.size(); j++)
         check_block(block.linear_preds[j] < block.linear_preds[j + 1], "linear predecessors must be sorted", &block);
      for (unsigned j = 0; j + 1 < block.logical_preds.size(); j++)
         check_block(block.logical_preds[j] < block.logical_preds[j + 1], "logical predecessors must be sorted", &block);
      for (unsigned j = 0; j + 1 < block.linear_succs.size(); j++)
         check_block(block.linear_succs[j] < block.linear_succs[j + 1], "linear successors must be sorted", &block);
      for (unsigned j = 0; j + 1 < block.logical_succs.size(); j++)
         check_block(block.logical_succs[j] < block.logical_succs[j + 1], "logical successors must be sorted", &block);

      /* critical edges are not allowed */
      if (block.linear_preds.size() > 1) {
         for (unsigned pred : block.linear_preds)
            check_block(program->blocks[pred].linear_succs.size() == 1, "linear critical edges are not allowed", &program->blocks[pred]);
         for (unsigned pred : block.logical_preds)
            check_block(program->blocks[pred].logical_succs.size() == 1, "logical critical edges are not allowed", &program->blocks[pred]);
      }
   }

   return is_valid;
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

bool ra_fail(Program *program, Location loc, Location loc2, const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   char msg[1024];
   vsprintf(msg, fmt, args);
   va_end(args);

   char *out;
   size_t outsize;
   struct u_memstream mem;
   u_memstream_open(&mem, &out, &outsize);
   FILE *const memf = u_memstream_get(&mem);

   fprintf(memf, "RA error found at instruction in BB%d:\n", loc.block->index);
   if (loc.instr) {
      aco_print_instr(loc.instr, memf);
      fprintf(memf, "\n%s", msg);
   } else {
      fprintf(memf, "%s", msg);
   }
   if (loc2.block) {
      fprintf(memf, " in BB%d:\n", loc2.block->index);
      aco_print_instr(loc2.instr, memf);
   }
   fprintf(memf, "\n\n");
   u_memstream_close(&mem);

   aco_err(program, "%s", out);
   free(out);

   return true;
}

bool validate_subdword_operand(chip_class chip, const aco_ptr<Instruction>& instr, unsigned index)
{
   Operand op = instr->operands[index];
   unsigned byte = op.physReg().byte();

   if (instr->opcode == aco_opcode::p_as_uniform)
      return byte == 0;
   if (instr->isPseudo() && chip >= GFX8)
      return true;
   if (instr->isSDWA() && (instr->sdwa().sel[index] & sdwa_asuint) == (sdwa_isra | op.bytes()))
      return true;
   if (byte == 2 && can_use_opsel(chip, instr->opcode, index, 1))
      return true;

   switch (instr->opcode) {
   case aco_opcode::v_cvt_f32_ubyte1:
      if (byte == 1)
         return true;
      break;
   case aco_opcode::v_cvt_f32_ubyte2:
      if (byte == 2)
         return true;
      break;
   case aco_opcode::v_cvt_f32_ubyte3:
      if (byte == 3)
         return true;
      break;
   case aco_opcode::ds_write_b8_d16_hi:
   case aco_opcode::ds_write_b16_d16_hi:
      if (byte == 2 && index == 1)
         return true;
      break;
   case aco_opcode::buffer_store_byte_d16_hi:
   case aco_opcode::buffer_store_short_d16_hi:
      if (byte == 2 && index == 3)
         return true;
      break;
   case aco_opcode::flat_store_byte_d16_hi:
   case aco_opcode::flat_store_short_d16_hi:
   case aco_opcode::scratch_store_byte_d16_hi:
   case aco_opcode::scratch_store_short_d16_hi:
   case aco_opcode::global_store_byte_d16_hi:
   case aco_opcode::global_store_short_d16_hi:
      if (byte == 2 && index == 2)
         return true;
   default:
      break;
   }

   return byte == 0;
}

bool validate_subdword_definition(chip_class chip, const aco_ptr<Instruction>& instr)
{
   Definition def = instr->definitions[0];
   unsigned byte = def.physReg().byte();

   if (instr->isPseudo() && chip >= GFX8)
      return true;
   if (instr->isSDWA() && instr->sdwa().dst_sel == (sdwa_isra | def.bytes()))
      return true;
   if (byte == 2 && can_use_opsel(chip, instr->opcode, -1, 1))
      return true;

   switch (instr->opcode) {
   case aco_opcode::buffer_load_ubyte_d16_hi:
   case aco_opcode::buffer_load_short_d16_hi:
   case aco_opcode::flat_load_ubyte_d16_hi:
   case aco_opcode::flat_load_short_d16_hi:
   case aco_opcode::scratch_load_ubyte_d16_hi:
   case aco_opcode::scratch_load_short_d16_hi:
   case aco_opcode::global_load_ubyte_d16_hi:
   case aco_opcode::global_load_short_d16_hi:
   case aco_opcode::ds_read_u8_d16_hi:
   case aco_opcode::ds_read_u16_d16_hi:
      return byte == 2;
   default:
      break;
   }

   return byte == 0;
}

unsigned get_subdword_bytes_written(Program *program, const aco_ptr<Instruction>& instr, unsigned index)
{
   chip_class chip = program->chip_class;
   Definition def = instr->definitions[index];

   if (instr->isPseudo())
      return chip >= GFX8 ? def.bytes() : def.size() * 4u;
   if (instr->isSDWA() && instr->sdwa().dst_sel == (sdwa_isra | def.bytes()))
      return def.bytes();

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
   case aco_opcode::buffer_load_ubyte_d16_hi:
   case aco_opcode::buffer_load_short_d16_hi:
   case aco_opcode::flat_load_ubyte_d16_hi:
   case aco_opcode::flat_load_short_d16_hi:
   case aco_opcode::scratch_load_ubyte_d16_hi:
   case aco_opcode::scratch_load_short_d16_hi:
   case aco_opcode::global_load_ubyte_d16_hi:
   case aco_opcode::global_load_short_d16_hi:
   case aco_opcode::ds_read_u8_d16_hi:
   case aco_opcode::ds_read_u16_d16_hi:
      return program->dev.sram_ecc_enabled ? 4 : 2;
   case aco_opcode::v_mad_f16:
   case aco_opcode::v_mad_u16:
   case aco_opcode::v_mad_i16:
   case aco_opcode::v_fma_f16:
   case aco_opcode::v_div_fixup_f16:
   case aco_opcode::v_interp_p2_f16:
      if (chip >= GFX9)
         return 2;
   default:
      break;
   }

   return MAX2(chip >= GFX10 ? def.bytes() : 4, instr_info.definition_size[(int)instr->opcode] / 8u);
}

} /* end namespace */

bool validate_ra(Program *program) {
   if (!(debug_flags & DEBUG_VALIDATE_RA))
      return false;

   bool err = false;
   aco::live live_vars = aco::live_var_analysis(program);
   std::vector<std::vector<Temp>> phi_sgpr_ops(program->blocks.size());
   uint16_t sgpr_limit = get_addr_sgpr_from_waves(program, program->num_waves);

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
               err |= ra_fail(program, loc, Location(), "Operand %d is not assigned a register", i);
            if (assignments.count(op.tempId()) && assignments[op.tempId()].reg != op.physReg())
               err |= ra_fail(program, loc, assignments.at(op.tempId()).firstloc, "Operand %d has an inconsistent register assignment with instruction", i);
            if ((op.getTemp().type() == RegType::vgpr && op.physReg().reg_b + op.bytes() > (256 + program->config->num_vgprs) * 4) ||
                (op.getTemp().type() == RegType::sgpr && op.physReg() + op.size() > program->config->num_sgprs && op.physReg() < sgpr_limit))
               err |= ra_fail(program, loc, assignments.at(op.tempId()).firstloc, "Operand %d has an out-of-bounds register assignment", i);
            if (op.physReg() == vcc && !program->needs_vcc)
               err |= ra_fail(program, loc, Location(), "Operand %d fixed to vcc but needs_vcc=false", i);
            if (op.regClass().is_subdword() && !validate_subdword_operand(program->chip_class, instr, i))
               err |= ra_fail(program, loc, Location(), "Operand %d not aligned correctly", i);
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
               err |= ra_fail(program, loc, Location(), "Definition %d is not assigned a register", i);
            if (assignments[def.tempId()].defloc.block)
               err |= ra_fail(program, loc, assignments.at(def.tempId()).defloc, "Temporary %%%d also defined by instruction", def.tempId());
            if ((def.getTemp().type() == RegType::vgpr && def.physReg().reg_b + def.bytes() > (256 + program->config->num_vgprs) * 4) ||
                (def.getTemp().type() == RegType::sgpr && def.physReg() + def.size() > program->config->num_sgprs && def.physReg() < sgpr_limit))
               err |= ra_fail(program, loc, assignments.at(def.tempId()).firstloc, "Definition %d has an out-of-bounds register assignment", i);
            if (def.physReg() == vcc && !program->needs_vcc)
               err |= ra_fail(program, loc, Location(), "Definition %d fixed to vcc but needs_vcc=false", i);
            if (def.regClass().is_subdword() && !validate_subdword_definition(program->chip_class, instr))
               err |= ra_fail(program, loc, Location(), "Definition %d not aligned correctly", i);
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

      std::array<unsigned, 2048> regs; /* register file in bytes */
      regs.fill(0);

      std::set<Temp> live;
      for (unsigned id : live_vars.live_out[block.index])
         live.insert(Temp(id, program->temp_rc[id]));
      /* remove killed p_phi sgpr operands */
      for (Temp tmp : phi_sgpr_ops[block.index])
         live.erase(tmp);

      /* check live out */
      for (Temp tmp : live) {
         PhysReg reg = assignments.at(tmp.id()).reg;
         for (unsigned i = 0; i < tmp.bytes(); i++) {
            if (regs[reg.reg_b + i]) {
               err |= ra_fail(program, loc, Location(), "Assignment of element %d of %%%d already taken by %%%d in live-out", i, tmp.id(), regs[reg.reg_b + i]);
            }
            regs[reg.reg_b + i] = tmp.id();
         }
      }
      regs.fill(0);

      for (auto it = block.instructions.rbegin(); it != block.instructions.rend(); ++it) {
         aco_ptr<Instruction>& instr = *it;

         /* check killed p_phi sgpr operands */
         if (instr->opcode == aco_opcode::p_logical_end) {
            for (Temp tmp : phi_sgpr_ops[block.index]) {
               PhysReg reg = assignments.at(tmp.id()).reg;
               for (unsigned i = 0; i < tmp.bytes(); i++) {
                  if (regs[reg.reg_b + i])
                     err |= ra_fail(program, loc, Location(), "Assignment of element %d of %%%d already taken by %%%d in live-out", i, tmp.id(), regs[reg.reg_b + i]);
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
         for (unsigned i = 0; i < tmp.bytes(); i++)
            regs[reg.reg_b + i] = tmp.id();
      }

      for (aco_ptr<Instruction>& instr : block.instructions) {
         loc.instr = instr.get();

         /* remove killed p_phi operands from regs */
         if (instr->opcode == aco_opcode::p_logical_end) {
            for (Temp tmp : phi_sgpr_ops[block.index]) {
               PhysReg reg = assignments.at(tmp.id()).reg;
               for (unsigned i = 0; i < tmp.bytes(); i++)
                  regs[reg.reg_b + i] = 0;
            }
         }

         if (instr->opcode != aco_opcode::p_phi && instr->opcode != aco_opcode::p_linear_phi) {
            for (const Operand& op : instr->operands) {
               if (!op.isTemp())
                  continue;
               if (op.isFirstKillBeforeDef()) {
                  for (unsigned j = 0; j < op.getTemp().bytes(); j++)
                     regs[op.physReg().reg_b + j] = 0;
               }
            }
         }

         for (unsigned i = 0; i < instr->definitions.size(); i++) {
            Definition& def = instr->definitions[i];
            if (!def.isTemp())
               continue;
            Temp tmp = def.getTemp();
            PhysReg reg = assignments.at(tmp.id()).reg;
            for (unsigned j = 0; j < tmp.bytes(); j++) {
               if (regs[reg.reg_b + j])
                  err |= ra_fail(program, loc, assignments.at(regs[reg.reg_b + j]).defloc, "Assignment of element %d of %%%d already taken by %%%d from instruction", i, tmp.id(), regs[reg.reg_b + j]);
               regs[reg.reg_b + j] = tmp.id();
            }
            if (def.regClass().is_subdword() && def.bytes() < 4) {
               unsigned written = get_subdword_bytes_written(program, instr, i);
               /* If written=4, the instruction still might write the upper half. In that case, it's the lower half that isn't preserved */
               for (unsigned j = reg.byte() & ~(written - 1); j < written; j++) {
                  unsigned written_reg = reg.reg() * 4u + j;
                  if (regs[written_reg] && regs[written_reg] != def.tempId())
                     err |= ra_fail(program, loc, assignments.at(regs[written_reg]).defloc, "Assignment of element %d of %%%d overwrites the full register taken by %%%d from instruction", i, tmp.id(), regs[written_reg]);
               }
            }
         }

         for (const Definition& def : instr->definitions) {
            if (!def.isTemp())
               continue;
            if (def.isKill()) {
               for (unsigned j = 0; j < def.getTemp().bytes(); j++)
                  regs[def.physReg().reg_b + j] = 0;
            }
         }

         if (instr->opcode != aco_opcode::p_phi && instr->opcode != aco_opcode::p_linear_phi) {
            for (const Operand& op : instr->operands) {
               if (!op.isTemp())
                  continue;
               if (op.isLateKill() && op.isFirstKill()) {
                  for (unsigned j = 0; j < op.getTemp().bytes(); j++)
                     regs[op.physReg().reg_b + j] = 0;
               }
            }
         }
      }
   }

   return err;
}
}
