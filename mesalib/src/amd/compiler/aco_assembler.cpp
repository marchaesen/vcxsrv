#include <map>

#include "aco_ir.h"
#include "common/sid.h"

namespace aco {

struct asm_context {
   Program *program;
   enum chip_class chip_class;
   std::map<int, SOPP_instruction*> branches;
   std::vector<unsigned> constaddrs;
   const int16_t* opcode;
   // TODO: keep track of branch instructions referring blocks
   // and, when emitting the block, correct the offset in instr
   asm_context(Program* program) : program(program), chip_class(program->chip_class) {
      if (chip_class <= GFX9)
         opcode = &instr_info.opcode_gfx9[0];
   }
};

void emit_instruction(asm_context& ctx, std::vector<uint32_t>& out, Instruction* instr)
{
   uint32_t instr_offset = out.size() * 4u;

   /* lower remaining pseudo-instructions */
   if (instr->opcode == aco_opcode::p_constaddr) {
      unsigned dest = instr->definitions[0].physReg();
      unsigned offset = instr->operands[0].constantValue();

      /* s_getpc_b64 dest[0:1] */
      uint32_t encoding = (0b101111101 << 23);
      uint32_t opcode = ctx.opcode[(int)aco_opcode::s_getpc_b64];
      if (opcode >= 55 && ctx.chip_class <= GFX9) {
         assert(ctx.chip_class == GFX9 && opcode < 60);
         opcode = opcode - 4;
      }
      encoding |= dest << 16;
      encoding |= opcode << 8;
      out.push_back(encoding);

      /* s_add_u32 dest[0], dest[0], ... */
      encoding = (0b10 << 30);
      encoding |= ctx.opcode[(int)aco_opcode::s_add_u32] << 23;
      encoding |= dest << 16;
      encoding |= dest;
      encoding |= 255 << 8;
      out.push_back(encoding);
      ctx.constaddrs.push_back(out.size());
      out.push_back(-(instr_offset + 4) + offset);

      /* s_addc_u32 dest[1], dest[1], 0 */
      encoding = (0b10 << 30);
      encoding |= ctx.opcode[(int)aco_opcode::s_addc_u32] << 23;
      encoding |= (dest + 1) << 16;
      encoding |= dest + 1;
      encoding |= 128 << 8;
      out.push_back(encoding);
      return;
   }

   uint32_t opcode = ctx.opcode[(int)instr->opcode];
   if (opcode == (uint32_t)-1) {
      fprintf(stderr, "Unsupported opcode: ");
      aco_print_instr(instr, stderr);
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
      uint32_t encoding = (0b1011 << 28);
      encoding |= opcode << 23;
      encoding |=
         !instr->definitions.empty() && !(instr->definitions[0].physReg() == scc) ?
         instr->definitions[0].physReg() << 16 :
         !instr->operands.empty() && !(instr->operands[0].physReg() == scc) ?
         instr->operands[0].physReg() << 16 : 0;
      encoding |= static_cast<SOPK_instruction*>(instr)->imm;
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
      SOPP_instruction* sopp = static_cast<SOPP_instruction*>(instr);
      uint32_t encoding = (0b101111111 << 23);
      encoding |= opcode << 16;
      encoding |= (uint16_t) sopp->imm;
      if (sopp->block != -1)
         ctx.branches.insert({out.size(), sopp});
      out.push_back(encoding);
      break;
   }
   case Format::SMEM: {
      SMEM_instruction* smem = static_cast<SMEM_instruction*>(instr);
      uint32_t encoding = (0b110000 << 26);
      encoding |= opcode << 18;
      if (instr->operands.size() >= 2)
         encoding |= instr->operands[1].isConstant() ? 1 << 17 : 0;
      bool soe = instr->operands.size() >= (!instr->definitions.empty() ? 3 : 4);
      assert(!soe || ctx.chip_class >= GFX9);
      encoding |= soe ? 1 << 14 : 0;
      encoding |= smem->glc ? 1 << 16 : 0;
      if (!instr->definitions.empty() || instr->operands.size() >= 3)
         encoding |= (!instr->definitions.empty() ? instr->definitions[0].physReg() : instr->operands[2].physReg().reg) << 6;
      if (instr->operands.size() >= 1)
         encoding |= instr->operands[0].physReg() >> 1;
      out.push_back(encoding);
      encoding = 0;
      if (instr->operands.size() >= 2)
         encoding |= instr->operands[1].isConstant() ? instr->operands[1].constantValue() : instr->operands[1].physReg().reg;
      encoding |= soe ? instr->operands.back().physReg() << 25 : 0;
      out.push_back(encoding);
      return;
   }
   case Format::VOP2: {
      uint32_t encoding = 0;
      encoding |= opcode << 25;
      encoding |= (0xFF & instr->definitions[0].physReg().reg) << 17;
      encoding |= (0xFF & instr->operands[1].physReg().reg) << 9;
      encoding |= instr->operands[0].physReg().reg;
      out.push_back(encoding);
      break;
   }
   case Format::VOP1: {
      uint32_t encoding = (0b0111111 << 25);
      encoding |= (0xFF & instr->definitions[0].physReg().reg) << 17;
      encoding |= opcode << 9;
      encoding |= instr->operands[0].physReg().reg;
      out.push_back(encoding);
      break;
   }
   case Format::VOPC: {
      uint32_t encoding = (0b0111110 << 25);
      encoding |= opcode << 17;
      encoding |= (0xFF & instr->operands[1].physReg().reg) << 9;
      encoding |= instr->operands[0].physReg().reg;
      out.push_back(encoding);
      break;
   }
   case Format::VINTRP: {
      Interp_instruction* interp = static_cast<Interp_instruction*>(instr);
      uint32_t encoding = (0b110101 << 26);
      encoding |= (0xFF & instr->definitions[0].physReg().reg) << 18;
      encoding |= opcode << 16;
      encoding |= interp->attribute << 10;
      encoding |= interp->component << 8;
      if (instr->opcode == aco_opcode::v_interp_mov_f32)
         encoding |= (0x3 & instr->operands[0].constantValue());
      else
         encoding |= (0xFF & instr->operands[0].physReg().reg);
      out.push_back(encoding);
      break;
   }
   case Format::DS: {
      DS_instruction* ds = static_cast<DS_instruction*>(instr);
      uint32_t encoding = (0b110110 << 26);
      encoding |= opcode << 17;
      encoding |= (ds->gds ? 1 : 0) << 16;
      encoding |= ((0xFF & ds->offset1) << 8);
      encoding |= (0xFFFF & ds->offset0);
      out.push_back(encoding);
      encoding = 0;
      unsigned reg = !instr->definitions.empty() ? instr->definitions[0].physReg() : 0;
      encoding |= (0xFF & reg) << 24;
      reg = instr->operands.size() >= 3 && !(instr->operands[2].physReg() == m0)  ? instr->operands[2].physReg() : 0;
      encoding |= (0xFF & reg) << 16;
      reg = instr->operands.size() >= 2 && !(instr->operands[1].physReg() == m0) ? instr->operands[1].physReg() : 0;
      encoding |= (0xFF & reg) << 8;
      encoding |= (0xFF & instr->operands[0].physReg().reg);
      out.push_back(encoding);
      break;
   }
   case Format::MUBUF: {
      MUBUF_instruction* mubuf = static_cast<MUBUF_instruction*>(instr);
      uint32_t encoding = (0b111000 << 26);
      encoding |= opcode << 18;
      encoding |= (mubuf->slc ? 1 : 0) << 17;
      encoding |= (mubuf->lds ? 1 : 0) << 16;
      encoding |= (mubuf->glc ? 1 : 0) << 14;
      encoding |= (mubuf->idxen ? 1 : 0) << 13;
      encoding |= (mubuf->offen ? 1 : 0) << 12;
      encoding |= 0x0FFF & mubuf->offset;
      out.push_back(encoding);
      encoding = 0;
      encoding |= instr->operands[2].physReg() << 24;
      encoding |= (mubuf->tfe ? 1 : 0) << 23;
      encoding |= (instr->operands[1].physReg() >> 2) << 16;
      unsigned reg = instr->operands.size() > 3 ? instr->operands[3].physReg() : instr->definitions[0].physReg().reg;
      encoding |= (0xFF & reg) << 8;
      encoding |= (0xFF & instr->operands[0].physReg().reg);
      out.push_back(encoding);
      break;
   }
   case Format::MTBUF: {
      MTBUF_instruction* mtbuf = static_cast<MTBUF_instruction*>(instr);
      uint32_t encoding = (0b111010 << 26);
      encoding |= opcode << 15;
      encoding |= (mtbuf->glc ? 1 : 0) << 14;
      encoding |= (mtbuf->idxen ? 1 : 0) << 13;
      encoding |= (mtbuf->offen ? 1 : 0) << 12;
      encoding |= 0x0FFF & mtbuf->offset;
      encoding |= (0xF & mtbuf->dfmt) << 19;
      encoding |= (0x7 & mtbuf->nfmt) << 23;
      out.push_back(encoding);
      encoding = 0;
      encoding |= instr->operands[2].physReg().reg << 24;
      encoding |= (mtbuf->tfe ? 1 : 0) << 23;
      encoding |= (mtbuf->slc ? 1 : 0) << 22;
      encoding |= (instr->operands[1].physReg().reg >> 2) << 16;
      unsigned reg = instr->operands.size() > 3 ? instr->operands[3].physReg().reg : instr->definitions[0].physReg().reg;
      encoding |= (0xFF & reg) << 8;
      encoding |= (0xFF & instr->operands[0].physReg().reg);
      out.push_back(encoding);
      break;
   }
   case Format::MIMG: {
      MIMG_instruction* mimg = static_cast<MIMG_instruction*>(instr);
      uint32_t encoding = (0b111100 << 26);
      encoding |= mimg->slc ? 1 << 25 : 0;
      encoding |= opcode << 18;
      encoding |= mimg->lwe ? 1 << 17 : 0;
      encoding |= mimg->tfe ? 1 << 16 : 0;
      encoding |= mimg->r128 ? 1 << 15 : 0;
      encoding |= mimg->da ? 1 << 14 : 0;
      encoding |= mimg->glc ? 1 << 13 : 0;
      encoding |= mimg->unrm ? 1 << 12 : 0;
      encoding |= (0xF & mimg->dmask) << 8;
      out.push_back(encoding);
      encoding = (0xFF & instr->operands[0].physReg().reg); /* VADDR */
      if (!instr->definitions.empty()) {
         encoding |= (0xFF & instr->definitions[0].physReg().reg) << 8; /* VDATA */
      } else if (instr->operands.size() == 4) {
         encoding |= (0xFF & instr->operands[3].physReg().reg) << 8; /* VDATA */
      }
      encoding |= (0x1F & (instr->operands[1].physReg() >> 2)) << 16; /* T# (resource) */
      if (instr->operands.size() > 2)
         encoding |= (0x1F & (instr->operands[2].physReg() >> 2)) << 21; /* sampler */
      // TODO VEGA: D16
      out.push_back(encoding);
      break;
   }
   case Format::FLAT:
   case Format::SCRATCH:
   case Format::GLOBAL: {
      FLAT_instruction *flat = static_cast<FLAT_instruction*>(instr);
      uint32_t encoding = (0b110111 << 26);
      encoding |= opcode << 18;
      encoding |= flat->offset & 0x1fff;
      if (instr->format == Format::SCRATCH)
         encoding |= 1 << 14;
      else if (instr->format == Format::GLOBAL)
         encoding |= 2 << 14;
      encoding |= flat->lds ? 1 << 13 : 0;
      encoding |= flat->glc ? 1 << 13 : 0;
      encoding |= flat->slc ? 1 << 13 : 0;
      out.push_back(encoding);
      encoding = (0xFF & instr->operands[0].physReg().reg);
      if (!instr->definitions.empty())
         encoding |= (0xFF & instr->definitions[0].physReg().reg) << 24;
      else
         encoding |= (0xFF & instr->operands[2].physReg().reg) << 8;
      if (!instr->operands[1].isUndefined()) {
         assert(instr->operands[1].physReg() != 0x7f);
         assert(instr->format != Format::FLAT);
         encoding |= instr->operands[1].physReg() << 16;
      } else if (instr->format != Format::FLAT) {
         encoding |= 0x7F << 16;
      }
      encoding |= flat->nv ? 1 << 23 : 0;
      out.push_back(encoding);
      break;
   }
   case Format::EXP: {
      Export_instruction* exp = static_cast<Export_instruction*>(instr);
      uint32_t encoding = (0b110001 << 26);
      encoding |= exp->valid_mask ? 0b1 << 12 : 0;
      encoding |= exp->done ? 0b1 << 11 : 0;
      encoding |= exp->compressed ? 0b1 << 10 : 0;
      encoding |= exp->dest << 4;
      encoding |= exp->enabled_mask;
      out.push_back(encoding);
      encoding = 0xFF & exp->operands[0].physReg().reg;
      encoding |= (0xFF & exp->operands[1].physReg().reg) << 8;
      encoding |= (0xFF & exp->operands[2].physReg().reg) << 16;
      encoding |= (0xFF & exp->operands[3].physReg().reg) << 24;
      out.push_back(encoding);
      break;
   }
   case Format::PSEUDO:
   case Format::PSEUDO_BARRIER:
      unreachable("Pseudo instructions should be lowered before assembly.");
   default:
      if ((uint16_t) instr->format & (uint16_t) Format::VOP3A) {
         VOP3A_instruction* vop3 = static_cast<VOP3A_instruction*>(instr);

         if ((uint16_t) instr->format & (uint16_t) Format::VOP2)
            opcode = opcode + 0x100;
         else if ((uint16_t) instr->format & (uint16_t) Format::VOP1)
            opcode = opcode + 0x140;
         else if ((uint16_t) instr->format & (uint16_t) Format::VOPC)
            opcode = opcode + 0x0;
         else if ((uint16_t) instr->format & (uint16_t) Format::VINTRP)
            opcode = opcode + 0x270;

         // TODO: op_sel
         uint32_t encoding = (0b110100 << 26);
         encoding |= opcode << 16;
         encoding |= (vop3->clamp ? 1 : 0) << 15;
         for (unsigned i = 0; i < 3; i++)
            encoding |= vop3->abs[i] << (8+i);
         if (instr->definitions.size() == 2)
            encoding |= instr->definitions[1].physReg() << 8;
         encoding |= (0xFF & instr->definitions[0].physReg().reg);
         out.push_back(encoding);
         encoding = 0;
         if (instr->opcode == aco_opcode::v_interp_mov_f32) {
            encoding = 0x3 & instr->operands[0].constantValue();
         } else {
            for (unsigned i = 0; i < instr->operands.size(); i++)
               encoding |= instr->operands[i].physReg() << (i * 9);
         }
         encoding |= vop3->omod << 27;
         for (unsigned i = 0; i < 3; i++)
            encoding |= vop3->neg[i] << (29+i);
         out.push_back(encoding);
         return;

      } else if (instr->isDPP()){
         /* first emit the instruction without the DPP operand */
         Operand dpp_op = instr->operands[0];
         instr->operands[0] = Operand(PhysReg{250}, v1);
         instr->format = (Format) ((uint32_t) instr->format & ~(1 << 14));
         emit_instruction(ctx, out, instr);
         DPP_instruction* dpp = static_cast<DPP_instruction*>(instr);
         uint32_t encoding = (0xF & dpp->row_mask) << 28;
         encoding |= (0xF & dpp->bank_mask) << 24;
         encoding |= dpp->abs[1] << 23;
         encoding |= dpp->neg[1] << 22;
         encoding |= dpp->abs[0] << 21;
         encoding |= dpp->neg[0] << 20;
         encoding |= dpp->bound_ctrl << 19;
         encoding |= dpp->dpp_ctrl << 8;
         encoding |= (0xFF) & dpp_op.physReg().reg;
         out.push_back(encoding);
         return;
      } else {
         unreachable("unimplemented instruction format");
      }
   }

   /* append literal dword */
   for (const Operand& op : instr->operands) {
      if (op.isLiteral()) {
         out.push_back(op.constantValue());
         break;
      }
   }
}

void emit_block(asm_context& ctx, std::vector<uint32_t>& out, Block& block)
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

void fix_exports(asm_context& ctx, std::vector<uint32_t>& out, Program* program)
{
   for (int idx = program->blocks.size() - 1; idx >= 0; idx--) {
      Block& block = program->blocks[idx];
      std::vector<aco_ptr<Instruction>>::reverse_iterator it = block.instructions.rbegin();
      bool endBlock = false;
      bool exported = false;
      while ( it != block.instructions.rend())
      {
         if ((*it)->format == Format::EXP && endBlock) {
            Export_instruction* exp = static_cast<Export_instruction*>((*it).get());
            if (program->stage & hw_vs) {
               if (exp->dest >= V_008DFC_SQ_EXP_POS && exp->dest <= (V_008DFC_SQ_EXP_POS + 3)) {
                  exp->done = true;
                  exported = true;
                  break;
               }
            } else {
               exp->done = true;
               exp->valid_mask = true;
               exported = true;
               break;
            }
         } else if ((*it)->definitions.size() && (*it)->definitions[0].physReg() == exec)
            break;
         else if ((*it)->opcode == aco_opcode::s_endpgm) {
            if (endBlock)
               break;
            endBlock = true;
         }
         ++it;
      }
      if (!endBlock || exported)
         continue;
      /* we didn't find an Export instruction and have to insert a null export */
      aco_ptr<Export_instruction> exp{create_instruction<Export_instruction>(aco_opcode::exp, Format::EXP, 4, 0)};
      for (unsigned i = 0; i < 4; i++)
         exp->operands[i] = Operand(v1);
      exp->enabled_mask = 0;
      exp->compressed = false;
      exp->done = true;
      exp->valid_mask = program->stage & hw_fs;
      if (program->stage & hw_fs)
         exp->dest = 9; /* NULL */
      else
         exp->dest = V_008DFC_SQ_EXP_POS;
      /* insert the null export 1 instruction before endpgm */
      block.instructions.insert(block.instructions.end() - 1, std::move(exp));
   }
}

void fix_branches(asm_context& ctx, std::vector<uint32_t>& out)
{
   for (std::pair<int, SOPP_instruction*> branch : ctx.branches)
   {
      int offset = (int)ctx.program->blocks[branch.second->block].offset - branch.first - 1;
      out[branch.first] |= (uint16_t) offset;
   }
}

void fix_constaddrs(asm_context& ctx, std::vector<uint32_t>& out)
{
   for (unsigned addr : ctx.constaddrs)
      out[addr] += out.size() * 4u;
}

unsigned emit_program(Program* program,
                      std::vector<uint32_t>& code)
{
   asm_context ctx(program);

   if (program->stage & (hw_vs | hw_fs))
      fix_exports(ctx, code, program);

   for (Block& block : program->blocks) {
      block.offset = code.size();
      emit_block(ctx, code, block);
   }

   fix_branches(ctx, code);
   fix_constaddrs(ctx, code);

   unsigned constant_data_offset = code.size() * sizeof(uint32_t);
   while (program->constant_data.size() % 4u)
      program->constant_data.push_back(0);
   /* Copy constant data */
   code.insert(code.end(), (uint32_t*)program->constant_data.data(),
               (uint32_t*)(program->constant_data.data() + program->constant_data.size()));

   return constant_data_offset;
}

}
