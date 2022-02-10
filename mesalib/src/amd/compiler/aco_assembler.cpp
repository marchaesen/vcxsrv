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

#include "common/sid.h"

#include "util/memstream.h"

#include <algorithm>
#include <map>
#include <vector>

namespace aco {

struct constaddr_info {
   unsigned getpc_end;
   unsigned add_literal;
};

struct asm_context {
   Program* program;
   enum chip_class chip_class;
   std::vector<std::pair<int, SOPP_instruction*>> branches;
   std::map<unsigned, constaddr_info> constaddrs;
   const int16_t* opcode;
   // TODO: keep track of branch instructions referring blocks
   // and, when emitting the block, correct the offset in instr
   asm_context(Program* program_) : program(program_), chip_class(program->chip_class)
   {
      if (chip_class <= GFX7)
         opcode = &instr_info.opcode_gfx7[0];
      else if (chip_class <= GFX9)
         opcode = &instr_info.opcode_gfx9[0];
      else if (chip_class >= GFX10)
         opcode = &instr_info.opcode_gfx10[0];
   }

   int subvector_begin_pos = -1;
};

unsigned
get_mimg_nsa_dwords(const Instruction* instr)
{
   unsigned addr_dwords = instr->operands.size() - 3;
   for (unsigned i = 1; i < addr_dwords; i++) {
      if (instr->operands[3 + i].physReg() != instr->operands[3].physReg().advance(i * 4))
         return DIV_ROUND_UP(addr_dwords - 1, 4);
   }
   return 0;
}

void
emit_instruction(asm_context& ctx, std::vector<uint32_t>& out, Instruction* instr)
{
   /* lower remaining pseudo-instructions */
   if (instr->opcode == aco_opcode::p_constaddr_getpc) {
      ctx.constaddrs[instr->operands[0].constantValue()].getpc_end = out.size() + 1;

      instr->opcode = aco_opcode::s_getpc_b64;
      instr->operands.pop_back();
   } else if (instr->opcode == aco_opcode::p_constaddr_addlo) {
      ctx.constaddrs[instr->operands[1].constantValue()].add_literal = out.size() + 1;

      instr->opcode = aco_opcode::s_add_u32;
      instr->operands[1] = Operand::zero();
      instr->operands[1].setFixed(PhysReg(255));
   }

   uint32_t opcode = ctx.opcode[(int)instr->opcode];
   if (opcode == (uint32_t)-1) {
      char* outmem;
      size_t outsize;
      struct u_memstream mem;
      u_memstream_open(&mem, &outmem, &outsize);
      FILE* const memf = u_memstream_get(&mem);

      fprintf(memf, "Unsupported opcode: ");
      aco_print_instr(instr, memf);
      u_memstream_close(&mem);

      aco_err(ctx.program, outmem);
      free(outmem);

      abort();
   }

   switch (instr->format) {
   case Format::SOP2: {
      uint32_t encoding = (0b10 << 30);
      encoding |= opcode << 23;
      encoding |= !instr->definitions.empty() ? instr->definitions[0].physReg() << 16 : 0;
      encoding |= instr->operands.size() >= 2 ? instr->operands[1].physReg() << 8 : 0;
      encoding |= !instr->operands.empty() ? instr->operands[0].physReg() : 0;
      out.push_back(encoding);
      break;
   }
   case Format::SOPK: {
      SOPK_instruction& sopk = instr->sopk();

      if (instr->opcode == aco_opcode::s_subvector_loop_begin) {
         assert(ctx.chip_class >= GFX10);
         assert(ctx.subvector_begin_pos == -1);
         ctx.subvector_begin_pos = out.size();
      } else if (instr->opcode == aco_opcode::s_subvector_loop_end) {
         assert(ctx.chip_class >= GFX10);
         assert(ctx.subvector_begin_pos != -1);
         /* Adjust s_subvector_loop_begin instruction to the address after the end  */
         out[ctx.subvector_begin_pos] |= (out.size() - ctx.subvector_begin_pos);
         /* Adjust s_subvector_loop_end instruction to the address after the beginning  */
         sopk.imm = (uint16_t)(ctx.subvector_begin_pos - (int)out.size());
         ctx.subvector_begin_pos = -1;
      }

      uint32_t encoding = (0b1011 << 28);
      encoding |= opcode << 23;
      encoding |= !instr->definitions.empty() && !(instr->definitions[0].physReg() == scc)
                     ? instr->definitions[0].physReg() << 16
                  : !instr->operands.empty() && instr->operands[0].physReg() <= 127
                     ? instr->operands[0].physReg() << 16
                     : 0;
      encoding |= sopk.imm;
      out.push_back(encoding);
      break;
   }
   case Format::SOP1: {
      uint32_t encoding = (0b101111101 << 23);
      if (opcode >= 55 && ctx.chip_class <= GFX9) {
         assert(ctx.chip_class == GFX9 && opcode < 60);
         opcode = opcode - 4;
      }
      encoding |= !instr->definitions.empty() ? instr->definitions[0].physReg() << 16 : 0;
      encoding |= opcode << 8;
      encoding |= !instr->operands.empty() ? instr->operands[0].physReg() : 0;
      out.push_back(encoding);
      break;
   }
   case Format::SOPC: {
      uint32_t encoding = (0b101111110 << 23);
      encoding |= opcode << 16;
      encoding |= instr->operands.size() == 2 ? instr->operands[1].physReg() << 8 : 0;
      encoding |= !instr->operands.empty() ? instr->operands[0].physReg() : 0;
      out.push_back(encoding);
      break;
   }
   case Format::SOPP: {
      SOPP_instruction& sopp = instr->sopp();
      uint32_t encoding = (0b101111111 << 23);
      encoding |= opcode << 16;
      encoding |= (uint16_t)sopp.imm;
      if (sopp.block != -1) {
         sopp.pass_flags = 0;
         ctx.branches.emplace_back(out.size(), &sopp);
      }
      out.push_back(encoding);
      break;
   }
   case Format::SMEM: {
      SMEM_instruction& smem = instr->smem();
      bool soe = instr->operands.size() >= (!instr->definitions.empty() ? 3 : 4);
      bool is_load = !instr->definitions.empty();
      uint32_t encoding = 0;

      if (ctx.chip_class <= GFX7) {
         encoding = (0b11000 << 27);
         encoding |= opcode << 22;
         encoding |= instr->definitions.size() ? instr->definitions[0].physReg() << 15 : 0;
         encoding |= instr->operands.size() ? (instr->operands[0].physReg() >> 1) << 9 : 0;
         if (instr->operands.size() >= 2) {
            if (!instr->operands[1].isConstant()) {
               encoding |= instr->operands[1].physReg().reg();
            } else if (instr->operands[1].constantValue() >= 1024) {
               encoding |= 255; /* SQ_SRC_LITERAL */
            } else {
               encoding |= instr->operands[1].constantValue() >> 2;
               encoding |= 1 << 8;
            }
         }
         out.push_back(encoding);
         /* SMRD instructions can take a literal on GFX7 */
         if (instr->operands.size() >= 2 && instr->operands[1].isConstant() &&
             instr->operands[1].constantValue() >= 1024)
            out.push_back(instr->operands[1].constantValue() >> 2);
         return;
      }

      if (ctx.chip_class <= GFX9) {
         encoding = (0b110000 << 26);
         assert(!smem.dlc); /* Device-level coherent is not supported on GFX9 and lower */
         encoding |= smem.nv ? 1 << 15 : 0;
      } else {
         encoding = (0b111101 << 26);
         assert(!smem.nv); /* Non-volatile is not supported on GFX10 */
         encoding |= smem.dlc ? 1 << 14 : 0;
      }

      encoding |= opcode << 18;
      encoding |= smem.glc ? 1 << 16 : 0;

      if (ctx.chip_class <= GFX9) {
         if (instr->operands.size() >= 2)
            encoding |= instr->operands[1].isConstant() ? 1 << 17 : 0; /* IMM - immediate enable */
      }
      if (ctx.chip_class == GFX9) {
         encoding |= soe ? 1 << 14 : 0;
      }

      if (is_load || instr->operands.size() >= 3) { /* SDATA */
         encoding |= (is_load ? instr->definitions[0].physReg() : instr->operands[2].physReg())
                     << 6;
      }
      if (instr->operands.size() >= 1) { /* SBASE */
         encoding |= instr->operands[0].physReg() >> 1;
      }

      out.push_back(encoding);
      encoding = 0;

      int32_t offset = 0;
      uint32_t soffset = ctx.chip_class >= GFX10
                            ? sgpr_null /* On GFX10 this is disabled by specifying SGPR_NULL */
                            : 0; /* On GFX9, it is disabled by the SOE bit (and it's not present on
                                    GFX8 and below) */
      if (instr->operands.size() >= 2) {
         const Operand& op_off1 = instr->operands[1];
         if (ctx.chip_class <= GFX9) {
            offset = op_off1.isConstant() ? op_off1.constantValue() : op_off1.physReg();
         } else {
            /* GFX10 only supports constants in OFFSET, so put the operand in SOFFSET if it's an
             * SGPR */
            if (op_off1.isConstant()) {
               offset = op_off1.constantValue();
            } else {
               soffset = op_off1.physReg();
               assert(!soe); /* There is no place to put the other SGPR offset, if any */
            }
         }

         if (soe) {
            const Operand& op_off2 = instr->operands.back();
            assert(ctx.chip_class >= GFX9); /* GFX8 and below don't support specifying a constant
                                               and an SGPR at the same time */
            assert(!op_off2.isConstant());
            soffset = op_off2.physReg();
         }
      }
      encoding |= offset;
      encoding |= soffset << 25;

      out.push_back(encoding);
      return;
   }
   case Format::VOP2: {
      uint32_t encoding = 0;
      encoding |= opcode << 25;
      encoding |= (0xFF & instr->definitions[0].physReg()) << 17;
      encoding |= (0xFF & instr->operands[1].physReg()) << 9;
      encoding |= instr->operands[0].physReg();
      out.push_back(encoding);
      break;
   }
   case Format::VOP1: {
      uint32_t encoding = (0b0111111 << 25);
      if (!instr->definitions.empty())
         encoding |= (0xFF & instr->definitions[0].physReg()) << 17;
      encoding |= opcode << 9;
      if (!instr->operands.empty())
         encoding |= instr->operands[0].physReg();
      out.push_back(encoding);
      break;
   }
   case Format::VOPC: {
      uint32_t encoding = (0b0111110 << 25);
      encoding |= opcode << 17;
      encoding |= (0xFF & instr->operands[1].physReg()) << 9;
      encoding |= instr->operands[0].physReg();
      out.push_back(encoding);
      break;
   }
   case Format::VINTRP: {
      Interp_instruction& interp = instr->vintrp();
      uint32_t encoding = 0;

      if (instr->opcode == aco_opcode::v_interp_p1ll_f16 ||
          instr->opcode == aco_opcode::v_interp_p1lv_f16 ||
          instr->opcode == aco_opcode::v_interp_p2_legacy_f16 ||
          instr->opcode == aco_opcode::v_interp_p2_f16) {
         if (ctx.chip_class == GFX8 || ctx.chip_class == GFX9) {
            encoding = (0b110100 << 26);
         } else if (ctx.chip_class >= GFX10) {
            encoding = (0b110101 << 26);
         } else {
            unreachable("Unknown chip_class.");
         }

         encoding |= opcode << 16;
         encoding |= (0xFF & instr->definitions[0].physReg());
         out.push_back(encoding);

         encoding = 0;
         encoding |= interp.attribute;
         encoding |= interp.component << 6;
         encoding |= instr->operands[0].physReg() << 9;
         if (instr->opcode == aco_opcode::v_interp_p2_f16 ||
             instr->opcode == aco_opcode::v_interp_p2_legacy_f16 ||
             instr->opcode == aco_opcode::v_interp_p1lv_f16) {
            encoding |= instr->operands[2].physReg() << 18;
         }
         out.push_back(encoding);
      } else {
         if (ctx.chip_class == GFX8 || ctx.chip_class == GFX9) {
            encoding = (0b110101 << 26); /* Vega ISA doc says 110010 but it's wrong */
         } else {
            encoding = (0b110010 << 26);
         }

         assert(encoding);
         encoding |= (0xFF & instr->definitions[0].physReg()) << 18;
         encoding |= opcode << 16;
         encoding |= interp.attribute << 10;
         encoding |= interp.component << 8;
         if (instr->opcode == aco_opcode::v_interp_mov_f32)
            encoding |= (0x3 & instr->operands[0].constantValue());
         else
            encoding |= (0xFF & instr->operands[0].physReg());
         out.push_back(encoding);
      }
      break;
   }
   case Format::DS: {
      DS_instruction& ds = instr->ds();
      uint32_t encoding = (0b110110 << 26);
      if (ctx.chip_class == GFX8 || ctx.chip_class == GFX9) {
         encoding |= opcode << 17;
         encoding |= (ds.gds ? 1 : 0) << 16;
      } else {
         encoding |= opcode << 18;
         encoding |= (ds.gds ? 1 : 0) << 17;
      }
      encoding |= ((0xFF & ds.offset1) << 8);
      encoding |= (0xFFFF & ds.offset0);
      out.push_back(encoding);
      encoding = 0;
      unsigned reg = !instr->definitions.empty() ? instr->definitions[0].physReg() : 0;
      encoding |= (0xFF & reg) << 24;
      reg = instr->operands.size() >= 3 && !(instr->operands[2].physReg() == m0)
               ? instr->operands[2].physReg()
               : 0;
      encoding |= (0xFF & reg) << 16;
      reg = instr->operands.size() >= 2 && !(instr->operands[1].physReg() == m0)
               ? instr->operands[1].physReg()
               : 0;
      encoding |= (0xFF & reg) << 8;
      encoding |= (0xFF & instr->operands[0].physReg());
      out.push_back(encoding);
      break;
   }
   case Format::MUBUF: {
      MUBUF_instruction& mubuf = instr->mubuf();
      uint32_t encoding = (0b111000 << 26);
      encoding |= opcode << 18;
      encoding |= (mubuf.lds ? 1 : 0) << 16;
      encoding |= (mubuf.glc ? 1 : 0) << 14;
      encoding |= (mubuf.idxen ? 1 : 0) << 13;
      assert(!mubuf.addr64 || ctx.chip_class <= GFX7);
      if (ctx.chip_class == GFX6 || ctx.chip_class == GFX7)
         encoding |= (mubuf.addr64 ? 1 : 0) << 15;
      encoding |= (mubuf.offen ? 1 : 0) << 12;
      if (ctx.chip_class == GFX8 || ctx.chip_class == GFX9) {
         assert(!mubuf.dlc); /* Device-level coherent is not supported on GFX9 and lower */
         encoding |= (mubuf.slc ? 1 : 0) << 17;
      } else if (ctx.chip_class >= GFX10) {
         encoding |= (mubuf.dlc ? 1 : 0) << 15;
      }
      encoding |= 0x0FFF & mubuf.offset;
      out.push_back(encoding);
      encoding = 0;
      if (ctx.chip_class <= GFX7 || ctx.chip_class >= GFX10) {
         encoding |= (mubuf.slc ? 1 : 0) << 22;
      }
      encoding |= instr->operands[2].physReg() << 24;
      encoding |= (mubuf.tfe ? 1 : 0) << 23;
      encoding |= (instr->operands[0].physReg() >> 2) << 16;
      unsigned reg = instr->operands.size() > 3 ? instr->operands[3].physReg()
                                                : instr->definitions[0].physReg();
      encoding |= (0xFF & reg) << 8;
      encoding |= (0xFF & instr->operands[1].physReg());
      out.push_back(encoding);
      break;
   }
   case Format::MTBUF: {
      MTBUF_instruction& mtbuf = instr->mtbuf();

      uint32_t img_format = ac_get_tbuffer_format(ctx.chip_class, mtbuf.dfmt, mtbuf.nfmt);
      uint32_t encoding = (0b111010 << 26);
      assert(img_format <= 0x7F);
      assert(!mtbuf.dlc || ctx.chip_class >= GFX10);
      encoding |= (mtbuf.dlc ? 1 : 0) << 15; /* DLC bit replaces one bit of the OPCODE on GFX10 */
      encoding |= (mtbuf.glc ? 1 : 0) << 14;
      encoding |= (mtbuf.idxen ? 1 : 0) << 13;
      encoding |= (mtbuf.offen ? 1 : 0) << 12;
      encoding |= 0x0FFF & mtbuf.offset;
      encoding |= (img_format << 19); /* Handles both the GFX10 FORMAT and the old NFMT+DFMT */

      if (ctx.chip_class == GFX8 || ctx.chip_class == GFX9) {
         encoding |= opcode << 15;
      } else {
         encoding |= (opcode & 0x07) << 16; /* 3 LSBs of 4-bit OPCODE */
      }

      out.push_back(encoding);
      encoding = 0;

      encoding |= instr->operands[2].physReg() << 24;
      encoding |= (mtbuf.tfe ? 1 : 0) << 23;
      encoding |= (mtbuf.slc ? 1 : 0) << 22;
      encoding |= (instr->operands[0].physReg() >> 2) << 16;
      unsigned reg = instr->operands.size() > 3 ? instr->operands[3].physReg()
                                                : instr->definitions[0].physReg();
      encoding |= (0xFF & reg) << 8;
      encoding |= (0xFF & instr->operands[1].physReg());

      if (ctx.chip_class >= GFX10) {
         encoding |= (((opcode & 0x08) >> 3) << 21); /* MSB of 4-bit OPCODE */
      }

      out.push_back(encoding);
      break;
   }
   case Format::MIMG: {
      unsigned nsa_dwords = get_mimg_nsa_dwords(instr);
      assert(!nsa_dwords || ctx.chip_class >= GFX10);

      MIMG_instruction& mimg = instr->mimg();
      uint32_t encoding = (0b111100 << 26);
      encoding |= mimg.slc ? 1 << 25 : 0;
      encoding |= (opcode & 0x7f) << 18;
      encoding |= (opcode >> 7) & 1;
      encoding |= mimg.lwe ? 1 << 17 : 0;
      encoding |= mimg.tfe ? 1 << 16 : 0;
      encoding |= mimg.glc ? 1 << 13 : 0;
      encoding |= mimg.unrm ? 1 << 12 : 0;
      if (ctx.chip_class <= GFX9) {
         assert(!mimg.dlc); /* Device-level coherent is not supported on GFX9 and lower */
         assert(!mimg.r128);
         encoding |= mimg.a16 ? 1 << 15 : 0;
         encoding |= mimg.da ? 1 << 14 : 0;
      } else {
         encoding |= mimg.r128 ? 1 << 15
                               : 0; /* GFX10: A16 moved to 2nd word, R128 replaces it in 1st word */
         encoding |= nsa_dwords << 1;
         encoding |= mimg.dim << 3; /* GFX10: dimensionality instead of declare array */
         encoding |= mimg.dlc ? 1 << 7 : 0;
      }
      encoding |= (0xF & mimg.dmask) << 8;
      out.push_back(encoding);
      encoding = (0xFF & instr->operands[3].physReg()); /* VADDR */
      if (!instr->definitions.empty()) {
         encoding |= (0xFF & instr->definitions[0].physReg()) << 8; /* VDATA */
      } else if (!instr->operands[2].isUndefined()) {
         encoding |= (0xFF & instr->operands[2].physReg()) << 8; /* VDATA */
      }
      encoding |= (0x1F & (instr->operands[0].physReg() >> 2)) << 16; /* T# (resource) */
      if (!instr->operands[1].isUndefined())
         encoding |= (0x1F & (instr->operands[1].physReg() >> 2)) << 21; /* sampler */

      assert(!mimg.d16 || ctx.chip_class >= GFX9);
      encoding |= mimg.d16 ? 1 << 31 : 0;
      if (ctx.chip_class >= GFX10) {
         /* GFX10: A16 still exists, but is in a different place */
         encoding |= mimg.a16 ? 1 << 30 : 0;
      }

      out.push_back(encoding);

      if (nsa_dwords) {
         out.resize(out.size() + nsa_dwords);
         std::vector<uint32_t>::iterator nsa = std::prev(out.end(), nsa_dwords);
         for (unsigned i = 0; i < instr->operands.size() - 4u; i++)
            nsa[i / 4] |= (0xFF & instr->operands[4 + i].physReg().reg()) << (i % 4 * 8);
      }
      break;
   }
   case Format::FLAT:
   case Format::SCRATCH:
   case Format::GLOBAL: {
      FLAT_instruction& flat = instr->flatlike();
      uint32_t encoding = (0b110111 << 26);
      encoding |= opcode << 18;
      if (ctx.chip_class <= GFX9) {
         assert(flat.offset <= 0x1fff);
         encoding |= flat.offset & 0x1fff;
      } else if (instr->isFlat()) {
         /* GFX10 has a 12-bit immediate OFFSET field,
          * but it has a hw bug: it ignores the offset, called FlatSegmentOffsetBug
          */
         assert(flat.offset == 0);
      } else {
         assert(flat.offset <= 0xfff);
         encoding |= flat.offset & 0xfff;
      }
      if (instr->isScratch())
         encoding |= 1 << 14;
      else if (instr->isGlobal())
         encoding |= 2 << 14;
      encoding |= flat.lds ? 1 << 13 : 0;
      encoding |= flat.glc ? 1 << 16 : 0;
      encoding |= flat.slc ? 1 << 17 : 0;
      if (ctx.chip_class >= GFX10) {
         assert(!flat.nv);
         encoding |= flat.dlc ? 1 << 12 : 0;
      } else {
         assert(!flat.dlc);
      }
      out.push_back(encoding);
      encoding = (0xFF & instr->operands[0].physReg());
      if (!instr->definitions.empty())
         encoding |= (0xFF & instr->definitions[0].physReg()) << 24;
      if (instr->operands.size() >= 3)
         encoding |= (0xFF & instr->operands[2].physReg()) << 8;
      if (!instr->operands[1].isUndefined()) {
         assert(ctx.chip_class >= GFX10 || instr->operands[1].physReg() != 0x7F);
         assert(instr->format != Format::FLAT);
         encoding |= instr->operands[1].physReg() << 16;
      } else if (instr->format != Format::FLAT ||
                 ctx.chip_class >= GFX10) { /* SADDR is actually used with FLAT on GFX10 */
         if (ctx.chip_class <= GFX9)
            encoding |= 0x7F << 16;
         else
            encoding |= sgpr_null << 16;
      }
      encoding |= flat.nv ? 1 << 23 : 0;
      out.push_back(encoding);
      break;
   }
   case Format::EXP: {
      Export_instruction& exp = instr->exp();
      uint32_t encoding;
      if (ctx.chip_class == GFX8 || ctx.chip_class == GFX9) {
         encoding = (0b110001 << 26);
      } else {
         encoding = (0b111110 << 26);
      }

      encoding |= exp.valid_mask ? 0b1 << 12 : 0;
      encoding |= exp.done ? 0b1 << 11 : 0;
      encoding |= exp.compressed ? 0b1 << 10 : 0;
      encoding |= exp.dest << 4;
      encoding |= exp.enabled_mask;
      out.push_back(encoding);
      encoding = 0xFF & exp.operands[0].physReg();
      encoding |= (0xFF & exp.operands[1].physReg()) << 8;
      encoding |= (0xFF & exp.operands[2].physReg()) << 16;
      encoding |= (0xFF & exp.operands[3].physReg()) << 24;
      out.push_back(encoding);
      break;
   }
   case Format::PSEUDO:
   case Format::PSEUDO_BARRIER:
      if (instr->opcode != aco_opcode::p_unit_test)
         unreachable("Pseudo instructions should be lowered before assembly.");
      break;
   default:
      if (instr->isVOP3()) {
         VOP3_instruction& vop3 = instr->vop3();

         if (instr->isVOP2()) {
            opcode = opcode + 0x100;
         } else if (instr->isVOP1()) {
            if (ctx.chip_class == GFX8 || ctx.chip_class == GFX9)
               opcode = opcode + 0x140;
            else
               opcode = opcode + 0x180;
         } else if (instr->isVOPC()) {
            opcode = opcode + 0x0;
         } else if (instr->isVINTRP()) {
            opcode = opcode + 0x270;
         }

         uint32_t encoding;
         if (ctx.chip_class <= GFX9) {
            encoding = (0b110100 << 26);
         } else if (ctx.chip_class >= GFX10) {
            encoding = (0b110101 << 26);
         } else {
            unreachable("Unknown chip_class.");
         }

         if (ctx.chip_class <= GFX7) {
            encoding |= opcode << 17;
            encoding |= (vop3.clamp ? 1 : 0) << 11;
         } else {
            encoding |= opcode << 16;
            encoding |= (vop3.clamp ? 1 : 0) << 15;
         }
         encoding |= vop3.opsel << 11;
         for (unsigned i = 0; i < 3; i++)
            encoding |= vop3.abs[i] << (8 + i);
         if (instr->definitions.size() == 2)
            encoding |= instr->definitions[1].physReg() << 8;
         encoding |= (0xFF & instr->definitions[0].physReg());
         out.push_back(encoding);
         encoding = 0;
         if (instr->opcode == aco_opcode::v_interp_mov_f32) {
            encoding = 0x3 & instr->operands[0].constantValue();
         } else if (instr->opcode == aco_opcode::v_writelane_b32_e64) {
            encoding |= instr->operands[0].physReg() << 0;
            encoding |= instr->operands[1].physReg() << 9;
            /* Encoding src2 works fine with hardware but breaks some disassemblers. */
         } else {
            for (unsigned i = 0; i < instr->operands.size(); i++)
               encoding |= instr->operands[i].physReg() << (i * 9);
         }
         encoding |= vop3.omod << 27;
         for (unsigned i = 0; i < 3; i++)
            encoding |= vop3.neg[i] << (29 + i);
         out.push_back(encoding);

      } else if (instr->isVOP3P()) {
         VOP3P_instruction& vop3 = instr->vop3p();

         uint32_t encoding;
         if (ctx.chip_class == GFX9) {
            encoding = (0b110100111 << 23);
         } else if (ctx.chip_class >= GFX10) {
            encoding = (0b110011 << 26);
         } else {
            unreachable("Unknown chip_class.");
         }

         encoding |= opcode << 16;
         encoding |= (vop3.clamp ? 1 : 0) << 15;
         encoding |= vop3.opsel_lo << 11;
         encoding |= ((vop3.opsel_hi & 0x4) ? 1 : 0) << 14;
         for (unsigned i = 0; i < 3; i++)
            encoding |= vop3.neg_hi[i] << (8 + i);
         encoding |= (0xFF & instr->definitions[0].physReg());
         out.push_back(encoding);
         encoding = 0;
         for (unsigned i = 0; i < instr->operands.size(); i++)
            encoding |= instr->operands[i].physReg() << (i * 9);
         encoding |= (vop3.opsel_hi & 0x3) << 27;
         for (unsigned i = 0; i < 3; i++)
            encoding |= vop3.neg_lo[i] << (29 + i);
         out.push_back(encoding);

      } else if (instr->isDPP16()) {
         assert(ctx.chip_class >= GFX8);
         DPP16_instruction& dpp = instr->dpp16();

         /* first emit the instruction without the DPP operand */
         Operand dpp_op = instr->operands[0];
         instr->operands[0] = Operand(PhysReg{250}, v1);
         instr->format = (Format)((uint16_t)instr->format & ~(uint16_t)Format::DPP16);
         emit_instruction(ctx, out, instr);
         uint32_t encoding = (0xF & dpp.row_mask) << 28;
         encoding |= (0xF & dpp.bank_mask) << 24;
         encoding |= dpp.abs[1] << 23;
         encoding |= dpp.neg[1] << 22;
         encoding |= dpp.abs[0] << 21;
         encoding |= dpp.neg[0] << 20;
         if (ctx.chip_class >= GFX10)
            encoding |= 1 << 18; /* set Fetch Inactive to match GFX9 behaviour */
         encoding |= dpp.bound_ctrl << 19;
         encoding |= dpp.dpp_ctrl << 8;
         encoding |= (0xFF) & dpp_op.physReg();
         out.push_back(encoding);
         return;
      } else if (instr->isDPP8()) {
         assert(ctx.chip_class >= GFX10);
         DPP8_instruction& dpp = instr->dpp8();

         /* first emit the instruction without the DPP operand */
         Operand dpp_op = instr->operands[0];
         instr->operands[0] = Operand(PhysReg{234}, v1);
         instr->format = (Format)((uint16_t)instr->format & ~(uint16_t)Format::DPP8);
         emit_instruction(ctx, out, instr);
         uint32_t encoding = (0xFF) & dpp_op.physReg();
         for (unsigned i = 0; i < 8; ++i)
            encoding |= dpp.lane_sel[i] << (8 + i * 3);
         out.push_back(encoding);
         return;
      } else if (instr->isSDWA()) {
         SDWA_instruction& sdwa = instr->sdwa();

         /* first emit the instruction without the SDWA operand */
         Operand sdwa_op = instr->operands[0];
         instr->operands[0] = Operand(PhysReg{249}, v1);
         instr->format = (Format)((uint16_t)instr->format & ~(uint16_t)Format::SDWA);
         emit_instruction(ctx, out, instr);

         uint32_t encoding = 0;

         if (instr->isVOPC()) {
            if (instr->definitions[0].physReg() != vcc) {
               encoding |= instr->definitions[0].physReg() << 8;
               encoding |= 1 << 15;
            }
            encoding |= (sdwa.clamp ? 1 : 0) << 13;
         } else {
            encoding |= sdwa.dst_sel.to_sdwa_sel(instr->definitions[0].physReg().byte()) << 8;
            uint32_t dst_u = sdwa.dst_sel.sign_extend() ? 1 : 0;
            if (instr->definitions[0].bytes() < 4) /* dst_preserve */
               dst_u = 2;
            encoding |= dst_u << 11;
            encoding |= (sdwa.clamp ? 1 : 0) << 13;
            encoding |= sdwa.omod << 14;
         }

         encoding |= sdwa.sel[0].to_sdwa_sel(sdwa_op.physReg().byte()) << 16;
         encoding |= sdwa.sel[0].sign_extend() ? 1 << 19 : 0;
         encoding |= sdwa.abs[0] << 21;
         encoding |= sdwa.neg[0] << 20;

         if (instr->operands.size() >= 2) {
            encoding |= sdwa.sel[1].to_sdwa_sel(instr->operands[1].physReg().byte()) << 24;
            encoding |= sdwa.sel[1].sign_extend() ? 1 << 27 : 0;
            encoding |= sdwa.abs[1] << 29;
            encoding |= sdwa.neg[1] << 28;
         }

         encoding |= 0xFF & sdwa_op.physReg();
         encoding |= (sdwa_op.physReg() < 256) << 23;
         if (instr->operands.size() >= 2)
            encoding |= (instr->operands[1].physReg() < 256) << 31;
         out.push_back(encoding);
      } else {
         unreachable("unimplemented instruction format");
      }
      break;
   }

   /* append literal dword */
   for (const Operand& op : instr->operands) {
      if (op.isLiteral()) {
         out.push_back(op.constantValue());
         break;
      }
   }
}

void
emit_block(asm_context& ctx, std::vector<uint32_t>& out, Block& block)
{
   for (aco_ptr<Instruction>& instr : block.instructions) {
#if 0
      int start_idx = out.size();
      std::cerr << "Encoding:\t" << std::endl;
      aco_print_instr(&*instr, stderr);
      std::cerr << std::endl;
#endif
      emit_instruction(ctx, out, instr.get());
#if 0
      for (int i = start_idx; i < out.size(); i++)
         std::cerr << "encoding: " << "0x" << std::setfill('0') << std::setw(8) << std::hex << out[i] << std::endl;
#endif
   }
}

void
fix_exports(asm_context& ctx, std::vector<uint32_t>& out, Program* program)
{
   bool exported = false;
   for (Block& block : program->blocks) {
      if (!(block.kind & block_kind_export_end))
         continue;
      std::vector<aco_ptr<Instruction>>::reverse_iterator it = block.instructions.rbegin();
      while (it != block.instructions.rend()) {
         if ((*it)->isEXP()) {
            Export_instruction& exp = (*it)->exp();
            if (program->stage.hw == HWStage::VS || program->stage.hw == HWStage::NGG) {
               if (exp.dest >= V_008DFC_SQ_EXP_POS && exp.dest <= (V_008DFC_SQ_EXP_POS + 3)) {
                  exp.done = true;
                  exported = true;
                  break;
               }
            } else {
               exp.done = true;
               exp.valid_mask = true;
               exported = true;
               break;
            }
         } else if ((*it)->definitions.size() && (*it)->definitions[0].physReg() == exec)
            break;
         ++it;
      }
   }

   if (!exported) {
      /* Abort in order to avoid a GPU hang. */
      bool is_vertex_or_ngg =
         (program->stage.hw == HWStage::VS || program->stage.hw == HWStage::NGG);
      aco_err(program,
              "Missing export in %s shader:", is_vertex_or_ngg ? "vertex or NGG" : "fragment");
      aco_print_program(program, stderr);
      abort();
   }
}

static void
insert_code(asm_context& ctx, std::vector<uint32_t>& out, unsigned insert_before,
            unsigned insert_count, const uint32_t* insert_data)
{
   out.insert(out.begin() + insert_before, insert_data, insert_data + insert_count);

   /* Update the offset of each affected block */
   for (Block& block : ctx.program->blocks) {
      if (block.offset >= insert_before)
         block.offset += insert_count;
   }

   /* Find first branch after the inserted code */
   auto branch_it = std::find_if(ctx.branches.begin(), ctx.branches.end(),
                                 [insert_before](const auto& branch) -> bool
                                 { return (unsigned)branch.first >= insert_before; });

   /* Update the locations of branches */
   for (; branch_it != ctx.branches.end(); ++branch_it)
      branch_it->first += insert_count;

   /* Update the locations of p_constaddr instructions */
   for (auto& constaddr : ctx.constaddrs) {
      constaddr_info& info = constaddr.second;
      if (info.getpc_end >= insert_before)
         info.getpc_end += insert_count;
      if (info.add_literal >= insert_before)
         info.add_literal += insert_count;
   }
}

static void
fix_branches_gfx10(asm_context& ctx, std::vector<uint32_t>& out)
{
   /* Branches with an offset of 0x3f are buggy on GFX10,
    * we workaround by inserting NOPs if needed.
    */
   bool gfx10_3f_bug = false;

   do {
      auto buggy_branch_it = std::find_if(
         ctx.branches.begin(), ctx.branches.end(),
         [&ctx](const auto& branch) -> bool {
            return ((int)ctx.program->blocks[branch.second->block].offset - branch.first - 1) ==
                   0x3f;
         });

      gfx10_3f_bug = buggy_branch_it != ctx.branches.end();

      if (gfx10_3f_bug) {
         /* Insert an s_nop after the branch */
         constexpr uint32_t s_nop_0 = 0xbf800000u;
         insert_code(ctx, out, buggy_branch_it->first + 1, 1, &s_nop_0);
      }
   } while (gfx10_3f_bug);
}

void
emit_long_jump(asm_context& ctx, SOPP_instruction* branch, bool backwards,
               std::vector<uint32_t>& out)
{
   Builder bld(ctx.program);

   Definition def_tmp_lo(branch->definitions[0].physReg(), s1);
   Operand op_tmp_lo(branch->definitions[0].physReg(), s1);
   Definition def_tmp_hi(branch->definitions[0].physReg().advance(4), s1);
   Operand op_tmp_hi(branch->definitions[0].physReg().advance(4), s1);

   aco_ptr<Instruction> instr;

   if (branch->opcode != aco_opcode::s_branch) {
      /* for conditional branches, skip the long jump if the condition is false */
      aco_opcode inv;
      switch (branch->opcode) {
      case aco_opcode::s_cbranch_scc0: inv = aco_opcode::s_cbranch_scc1; break;
      case aco_opcode::s_cbranch_scc1: inv = aco_opcode::s_cbranch_scc0; break;
      case aco_opcode::s_cbranch_vccz: inv = aco_opcode::s_cbranch_vccnz; break;
      case aco_opcode::s_cbranch_vccnz: inv = aco_opcode::s_cbranch_vccz; break;
      case aco_opcode::s_cbranch_execz: inv = aco_opcode::s_cbranch_execnz; break;
      case aco_opcode::s_cbranch_execnz: inv = aco_opcode::s_cbranch_execz; break;
      default: unreachable("Unhandled long jump.");
      }
      instr.reset(bld.sopp(inv, -1, 7));
      emit_instruction(ctx, out, instr.get());
   }

   /* create the new PC and stash SCC in the LSB */
   instr.reset(bld.sop1(aco_opcode::s_getpc_b64, branch->definitions[0]).instr);
   emit_instruction(ctx, out, instr.get());

   instr.reset(bld.sop2(aco_opcode::s_addc_u32, def_tmp_lo, op_tmp_lo, Operand::zero()).instr);
   instr->operands[1].setFixed(PhysReg{255}); /* this operand has to be a literal */
   emit_instruction(ctx, out, instr.get());
   branch->pass_flags = out.size();

   instr.reset(bld.sop2(aco_opcode::s_addc_u32, def_tmp_hi, op_tmp_hi,
                        Operand::c32(backwards ? UINT32_MAX : 0u))
                  .instr);
   emit_instruction(ctx, out, instr.get());

   /* restore SCC and clear the LSB of the new PC */
   instr.reset(bld.sopc(aco_opcode::s_bitcmp1_b32, def_tmp_lo, op_tmp_lo, Operand::zero()).instr);
   emit_instruction(ctx, out, instr.get());
   instr.reset(bld.sop1(aco_opcode::s_bitset0_b32, def_tmp_lo, Operand::zero()).instr);
   emit_instruction(ctx, out, instr.get());

   /* create the s_setpc_b64 to jump */
   instr.reset(
      bld.sop1(aco_opcode::s_setpc_b64, Operand(branch->definitions[0].physReg(), s2)).instr);
   emit_instruction(ctx, out, instr.get());
}

void
fix_branches(asm_context& ctx, std::vector<uint32_t>& out)
{
   bool repeat = false;
   do {
      repeat = false;

      if (ctx.chip_class == GFX10)
         fix_branches_gfx10(ctx, out);

      for (std::pair<int, SOPP_instruction*>& branch : ctx.branches) {
         int offset = (int)ctx.program->blocks[branch.second->block].offset - branch.first - 1;
         if ((offset < INT16_MIN || offset > INT16_MAX) && !branch.second->pass_flags) {
            std::vector<uint32_t> long_jump;
            bool backwards =
               ctx.program->blocks[branch.second->block].offset < (unsigned)branch.first;
            emit_long_jump(ctx, branch.second, backwards, long_jump);

            out[branch.first] = long_jump[0];
            insert_code(ctx, out, branch.first + 1, long_jump.size() - 1, long_jump.data() + 1);

            repeat = true;
            break;
         }

         if (branch.second->pass_flags) {
            int after_getpc = branch.first + branch.second->pass_flags - 2;
            offset = (int)ctx.program->blocks[branch.second->block].offset - after_getpc;
            out[branch.first + branch.second->pass_flags - 1] = offset * 4;
         } else {
            out[branch.first] &= 0xffff0000u;
            out[branch.first] |= (uint16_t)offset;
         }
      }
   } while (repeat);
}

void
fix_constaddrs(asm_context& ctx, std::vector<uint32_t>& out)
{
   for (auto& constaddr : ctx.constaddrs) {
      constaddr_info& info = constaddr.second;
      out[info.add_literal] += (out.size() - info.getpc_end) * 4u;
   }
}

unsigned
emit_program(Program* program, std::vector<uint32_t>& code)
{
   asm_context ctx(program);

   if (program->stage.hw == HWStage::VS || program->stage.hw == HWStage::FS ||
       program->stage.hw == HWStage::NGG)
      fix_exports(ctx, code, program);

   for (Block& block : program->blocks) {
      block.offset = code.size();
      emit_block(ctx, code, block);
   }

   fix_branches(ctx, code);

   unsigned exec_size = code.size() * sizeof(uint32_t);

   if (program->chip_class >= GFX10) {
      /* Pad output with s_code_end so instruction prefetching doesn't cause
       * page faults */
      unsigned final_size = align(code.size() + 3 * 16, 16);
      while (code.size() < final_size)
         code.push_back(0xbf9f0000u);
   }

   fix_constaddrs(ctx, code);

   while (program->constant_data.size() % 4u)
      program->constant_data.push_back(0);
   /* Copy constant data */
   code.insert(code.end(), (uint32_t*)program->constant_data.data(),
               (uint32_t*)(program->constant_data.data() + program->constant_data.size()));

   return exec_size;
}

} // namespace aco
