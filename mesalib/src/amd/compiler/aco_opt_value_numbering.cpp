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

#include <map>
#include <unordered_map>
#include "aco_ir.h"

/*
 * Implements the algorithm for dominator-tree value numbering
 * from "Value Numbering" by Briggs, Cooper, and Simpson.
 */

namespace aco {
namespace {

struct InstrHash {
   std::size_t operator()(Instruction* instr) const
   {
      uint64_t hash = (uint64_t) instr->opcode + (uint64_t) instr->format;
      for (unsigned i = 0; i < instr->operands.size(); i++) {
         Operand op = instr->operands[i];
         uint64_t val = op.isTemp() ? op.tempId() : op.isFixed() ? op.physReg() : op.constantValue();
         hash |= val << (i+1) * 8;
      }
      if (instr->isVOP3()) {
         VOP3A_instruction* vop3 = static_cast<VOP3A_instruction*>(instr);
         for (unsigned i = 0; i < 3; i++) {
            hash ^= vop3->abs[i] << (i*3 + 0);
            hash ^= vop3->neg[i] << (i*3 + 2);
         }
         hash ^= vop3->opsel * 13;
         hash ^= (vop3->clamp << 28) * 13;
         hash += vop3->omod << 19;
      }
      switch (instr->format) {
      case Format::SMEM:
         break;
      case Format::VINTRP: {
         Interp_instruction* interp = static_cast<Interp_instruction*>(instr);
         hash ^= interp->attribute << 13;
         hash ^= interp->component << 27;
         break;
      }
      case Format::DS:
         break;
      default:
         break;
      }

      return hash;
   }
};

struct InstrPred {
   bool operator()(Instruction* a, Instruction* b) const
   {
      if (a->format != b->format)
         return false;
      if (a->opcode != b->opcode)
         return false;
      if (a->operands.size() != b->operands.size() || a->definitions.size() != b->definitions.size())
         return false; /* possible with pseudo-instructions */
      for (unsigned i = 0; i < a->operands.size(); i++) {
         if (a->operands[i].isConstant()) {
            if (!b->operands[i].isConstant())
               return false;
            if (a->operands[i].constantValue() != b->operands[i].constantValue())
               return false;
         }
         else if (a->operands[i].isTemp()) {
            if (!b->operands[i].isTemp())
               return false;
            if (a->operands[i].tempId() != b->operands[i].tempId())
               return false;
         }
         else if (a->operands[i].isUndefined() ^ b->operands[i].isUndefined())
            return false;
         if (a->operands[i].isFixed()) {
            if (!b->operands[i].isFixed())
               return false;
            if (a->operands[i].physReg() != b->operands[i].physReg())
               return false;
            if (a->operands[i].physReg() == exec && a->pass_flags != b->pass_flags)
               return false;
         }
      }
      for (unsigned i = 0; i < a->definitions.size(); i++) {
         if (a->definitions[i].isTemp()) {
            if (!b->definitions[i].isTemp())
               return false;
            if (a->definitions[i].regClass() != b->definitions[i].regClass())
               return false;
         }
         if (a->definitions[i].isFixed()) {
            if (!b->definitions[i].isFixed())
               return false;
            if (a->definitions[i].physReg() != b->definitions[i].physReg())
               return false;
            if (a->definitions[i].physReg() == exec)
               return false;
         }
      }

      if (a->opcode == aco_opcode::v_readfirstlane_b32)
         return a->pass_flags == b->pass_flags;

      /* The results of VOPC depend on the exec mask if used for subgroup operations. */
      if ((uint32_t) a->format & (uint32_t) Format::VOPC && a->pass_flags != b->pass_flags)
         return false;

      if (a->isVOP3()) {
         VOP3A_instruction* a3 = static_cast<VOP3A_instruction*>(a);
         VOP3A_instruction* b3 = static_cast<VOP3A_instruction*>(b);
         for (unsigned i = 0; i < 3; i++) {
            if (a3->abs[i] != b3->abs[i] ||
                a3->neg[i] != b3->neg[i])
               return false;
         }
         return a3->clamp == b3->clamp &&
                a3->omod == b3->omod &&
                a3->opsel == b3->opsel;
      }
      if (a->isDPP()) {
         DPP_instruction* aDPP = static_cast<DPP_instruction*>(a);
         DPP_instruction* bDPP = static_cast<DPP_instruction*>(b);
         return aDPP->pass_flags == bDPP->pass_flags &&
                aDPP->dpp_ctrl == bDPP->dpp_ctrl &&
                aDPP->bank_mask == bDPP->bank_mask &&
                aDPP->row_mask == bDPP->row_mask &&
                aDPP->bound_ctrl == bDPP->bound_ctrl &&
                aDPP->abs[0] == bDPP->abs[0] &&
                aDPP->abs[1] == bDPP->abs[1] &&
                aDPP->neg[0] == bDPP->neg[0] &&
                aDPP->neg[1] == bDPP->neg[1];
      }

      switch (a->format) {
         case Format::SOPK: {
            SOPK_instruction* aK = static_cast<SOPK_instruction*>(a);
            SOPK_instruction* bK = static_cast<SOPK_instruction*>(b);
            return aK->imm == bK->imm;
         }
         case Format::SMEM: {
            SMEM_instruction* aS = static_cast<SMEM_instruction*>(a);
            SMEM_instruction* bS = static_cast<SMEM_instruction*>(b);
            return aS->can_reorder && bS->can_reorder &&
                   aS->glc == bS->glc && aS->nv == bS->nv;
         }
         case Format::VINTRP: {
            Interp_instruction* aI = static_cast<Interp_instruction*>(a);
            Interp_instruction* bI = static_cast<Interp_instruction*>(b);
            if (aI->attribute != bI->attribute)
               return false;
            if (aI->component != bI->component)
               return false;
            return true;
         }
         case Format::PSEUDO_REDUCTION: {
            Pseudo_reduction_instruction *aR = static_cast<Pseudo_reduction_instruction*>(a);
            Pseudo_reduction_instruction *bR = static_cast<Pseudo_reduction_instruction*>(b);
            return aR->pass_flags == bR->pass_flags &&
                   aR->reduce_op == bR->reduce_op &&
                   aR->cluster_size == bR->cluster_size;
         }
         case Format::MTBUF: {
            /* this is fine since they are only used for vertex input fetches */
            MTBUF_instruction* aM = static_cast<MTBUF_instruction *>(a);
            MTBUF_instruction* bM = static_cast<MTBUF_instruction *>(b);
            return aM->can_reorder && bM->can_reorder &&
                   aM->barrier == bM->barrier &&
                   aM->dfmt == bM->dfmt &&
                   aM->nfmt == bM->nfmt &&
                   aM->offset == bM->offset &&
                   aM->offen == bM->offen &&
                   aM->idxen == bM->idxen &&
                   aM->glc == bM->glc &&
                   aM->slc == bM->slc &&
                   aM->tfe == bM->tfe &&
                   aM->disable_wqm == bM->disable_wqm;
         }
         /* we want to optimize these in NIR and don't hassle with load-store dependencies */
         case Format::MUBUF:
         case Format::FLAT:
         case Format::GLOBAL:
         case Format::SCRATCH:
         case Format::EXP:
         case Format::SOPP:
         case Format::PSEUDO_BRANCH:
         case Format::PSEUDO_BARRIER:
            return false;
         case Format::DS: {
            if (a->opcode != aco_opcode::ds_bpermute_b32 &&
                a->opcode != aco_opcode::ds_permute_b32 &&
                a->opcode != aco_opcode::ds_swizzle_b32)
               return false;
            DS_instruction* aD = static_cast<DS_instruction *>(a);
            DS_instruction* bD = static_cast<DS_instruction *>(b);
            return aD->pass_flags == bD->pass_flags &&
                   aD->gds == bD->gds &&
                   aD->offset0 == bD->offset0 &&
                   aD->offset1 == bD->offset1;
         }
         case Format::MIMG: {
            MIMG_instruction* aM = static_cast<MIMG_instruction*>(a);
            MIMG_instruction* bM = static_cast<MIMG_instruction*>(b);
            return aM->can_reorder && bM->can_reorder &&
                   aM->barrier == bM->barrier &&
                   aM->dmask == bM->dmask &&
                   aM->unrm == bM->unrm &&
                   aM->glc == bM->glc &&
                   aM->slc == bM->slc &&
                   aM->tfe == bM->tfe &&
                   aM->da == bM->da &&
                   aM->lwe == bM->lwe &&
                   aM->r128 == bM->r128 &&
                   aM->a16 == bM->a16 &&
                   aM->d16 == bM->d16 &&
                   aM->disable_wqm == bM->disable_wqm;
         }
         default:
            return true;
      }
   }
};

using expr_set = std::unordered_map<Instruction*, uint32_t, InstrHash, InstrPred>;

struct vn_ctx {
   Program* program;
   expr_set expr_values;
   std::map<uint32_t, Temp> renames;

   /* The exec id should be the same on the same level of control flow depth.
    * Together with the check for dominator relations, it is safe to assume
    * that the same exec_id also means the same execution mask.
    * Discards increment the exec_id, so that it won't return to the previous value.
    */
   uint32_t exec_id = 1;

   vn_ctx(Program* program) : program(program) {}
};


/* dominates() returns true if the parent block dominates the child block and
 * if the parent block is part of the same loop or has a smaller loop nest depth.
 */
bool dominates(vn_ctx& ctx, uint32_t parent, uint32_t child)
{
   unsigned parent_loop_nest_depth = ctx.program->blocks[parent].loop_nest_depth;
   while (parent < child && parent_loop_nest_depth <= ctx.program->blocks[child].loop_nest_depth)
      child = ctx.program->blocks[child].logical_idom;

   return parent == child;
}

void process_block(vn_ctx& ctx, Block& block)
{
   std::vector<aco_ptr<Instruction>> new_instructions;
   new_instructions.reserve(block.instructions.size());

   for (aco_ptr<Instruction>& instr : block.instructions) {
      /* first, rename operands */
      for (Operand& op : instr->operands) {
         if (!op.isTemp())
            continue;
         auto it = ctx.renames.find(op.tempId());
         if (it != ctx.renames.end())
            op.setTemp(it->second);
      }

      if (instr->opcode == aco_opcode::p_discard_if ||
          instr->opcode == aco_opcode::p_demote_to_helper)
         ctx.exec_id++;

      if (instr->definitions.empty() || instr->opcode == aco_opcode::p_phi || instr->opcode == aco_opcode::p_linear_phi) {
         new_instructions.emplace_back(std::move(instr));
         continue;
      }

      /* simple copy-propagation through renaming */
      if ((instr->opcode == aco_opcode::s_mov_b32 || instr->opcode == aco_opcode::s_mov_b64 || instr->opcode == aco_opcode::v_mov_b32) &&
          !instr->definitions[0].isFixed() && instr->operands[0].isTemp() && instr->operands[0].regClass() == instr->definitions[0].regClass() &&
          !instr->isDPP() && !((int)instr->format & (int)Format::SDWA)) {
         ctx.renames[instr->definitions[0].tempId()] = instr->operands[0].getTemp();
      }

      instr->pass_flags = ctx.exec_id;
      std::pair<expr_set::iterator, bool> res = ctx.expr_values.emplace(instr.get(), block.index);

      /* if there was already an expression with the same value number */
      if (!res.second) {
         Instruction* orig_instr = res.first->first;
         assert(instr->definitions.size() == orig_instr->definitions.size());
         /* check if the original instruction dominates the current one */
         if (dominates(ctx, res.first->second, block.index) &&
             ctx.program->blocks[res.first->second].fp_mode.canReplace(block.fp_mode)) {
            for (unsigned i = 0; i < instr->definitions.size(); i++) {
               assert(instr->definitions[i].regClass() == orig_instr->definitions[i].regClass());
               assert(instr->definitions[i].isTemp());
               ctx.renames[instr->definitions[i].tempId()] = orig_instr->definitions[i].getTemp();
            }
         } else {
            ctx.expr_values.erase(res.first);
            ctx.expr_values.emplace(instr.get(), block.index);
            new_instructions.emplace_back(std::move(instr));
         }
      } else {
         new_instructions.emplace_back(std::move(instr));
      }
   }

   block.instructions = std::move(new_instructions);
}

void rename_phi_operands(Block& block, std::map<uint32_t, Temp>& renames)
{
   for (aco_ptr<Instruction>& phi : block.instructions) {
      if (phi->opcode != aco_opcode::p_phi && phi->opcode != aco_opcode::p_linear_phi)
         break;

      for (Operand& op : phi->operands) {
         if (!op.isTemp())
            continue;
         auto it = renames.find(op.tempId());
         if (it != renames.end())
            op.setTemp(it->second);
      }
   }
}
} /* end namespace */


void value_numbering(Program* program)
{
   vn_ctx ctx(program);
   std::vector<unsigned> loop_headers;

   for (Block& block : program->blocks) {
      assert(ctx.exec_id > 0);
      /* decrement exec_id when leaving nested control flow */
      if (block.kind & block_kind_loop_header)
         loop_headers.push_back(block.index);
      if (block.kind & block_kind_merge) {
         ctx.exec_id--;
      } else if (block.kind & block_kind_loop_exit) {
         ctx.exec_id -= program->blocks[loop_headers.back()].linear_preds.size();
         ctx.exec_id -= block.linear_preds.size();
         loop_headers.pop_back();
      }

      if (block.logical_idom != -1)
         process_block(ctx, block);
      else
         rename_phi_operands(block, ctx.renames);

      /* increment exec_id when entering nested control flow */
      if (block.kind & block_kind_branch ||
          block.kind & block_kind_loop_preheader ||
          block.kind & block_kind_break ||
          block.kind & block_kind_continue ||
          block.kind & block_kind_discard)
         ctx.exec_id++;
      else if (block.kind & block_kind_continue_or_break)
         ctx.exec_id += 2;
   }

   /* rename loop header phi operands */
   for (Block& block : program->blocks) {
      if (block.kind & block_kind_loop_header)
         rename_phi_operands(block, ctx.renames);
   }
}

}
