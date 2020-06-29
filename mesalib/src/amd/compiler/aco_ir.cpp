/*
 * Copyright © 2020 Valve Corporation
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

namespace aco {

bool can_use_SDWA(chip_class chip, const aco_ptr<Instruction>& instr)
{
   if (!instr->isVALU())
      return false;

   if (chip < GFX8 || instr->isDPP())
      return false;

   if (instr->isSDWA())
      return true;

   if (instr->isVOP3()) {
      VOP3A_instruction *vop3 = static_cast<VOP3A_instruction*>(instr.get());
      if (instr->format == Format::VOP3)
         return false;
      if (vop3->clamp && instr->format == asVOP3(Format::VOPC) && chip != GFX8)
         return false;
      if (vop3->omod && chip < GFX9)
         return false;

      //TODO: return true if we know we will use vcc
      if (instr->definitions.size() >= 2)
         return false;

      for (unsigned i = 1; i < instr->operands.size(); i++) {
         if (instr->operands[i].isLiteral())
            return false;
         if (chip < GFX9 && !instr->operands[i].isOfType(RegType::vgpr))
            return false;
      }
   }

   if (!instr->operands.empty()) {
      if (instr->operands[0].isLiteral())
         return false;
      if (chip < GFX9 && !instr->operands[0].isOfType(RegType::vgpr))
         return false;
   }

   bool is_mac = instr->opcode == aco_opcode::v_mac_f32 ||
                 instr->opcode == aco_opcode::v_mac_f16 ||
                 instr->opcode == aco_opcode::v_fmac_f32 ||
                 instr->opcode == aco_opcode::v_fmac_f16;

   if (chip != GFX8 && is_mac)
      return false;

   //TODO: return true if we know we will use vcc
   if ((unsigned)instr->format & (unsigned)Format::VOPC)
      return false;
   if (instr->operands.size() >= 3 && !is_mac)
      return false;

   return instr->opcode != aco_opcode::v_madmk_f32 &&
          instr->opcode != aco_opcode::v_madak_f32 &&
          instr->opcode != aco_opcode::v_madmk_f16 &&
          instr->opcode != aco_opcode::v_madak_f16 &&
          instr->opcode != aco_opcode::v_readfirstlane_b32 &&
          instr->opcode != aco_opcode::v_clrexcp &&
          instr->opcode != aco_opcode::v_swap_b32;
}

/* updates "instr" and returns the old instruction (or NULL if no update was needed) */
aco_ptr<Instruction> convert_to_SDWA(chip_class chip, aco_ptr<Instruction>& instr)
{
   if (instr->isSDWA())
      return NULL;

   aco_ptr<Instruction> tmp = std::move(instr);
   Format format = (Format)(((uint16_t)tmp->format & ~(uint16_t)Format::VOP3) | (uint16_t)Format::SDWA);
   instr.reset(create_instruction<SDWA_instruction>(tmp->opcode, format, tmp->operands.size(), tmp->definitions.size()));
   std::copy(tmp->operands.cbegin(), tmp->operands.cend(), instr->operands.begin());
   std::copy(tmp->definitions.cbegin(), tmp->definitions.cend(), instr->definitions.begin());

   SDWA_instruction *sdwa = static_cast<SDWA_instruction*>(instr.get());

   if (tmp->isVOP3()) {
      VOP3A_instruction *vop3 = static_cast<VOP3A_instruction*>(tmp.get());
      memcpy(sdwa->neg, vop3->neg, sizeof(sdwa->neg));
      memcpy(sdwa->abs, vop3->abs, sizeof(sdwa->abs));
      sdwa->omod = vop3->omod;
      sdwa->clamp = vop3->clamp;
   }

   for (unsigned i = 0; i < instr->operands.size(); i++) {
      switch (instr->operands[i].bytes()) {
      case 1:
         sdwa->sel[i] = sdwa_ubyte;
         break;
      case 2:
         sdwa->sel[i] = sdwa_uword;
         break;
      case 4:
         sdwa->sel[i] = sdwa_udword;
         break;
      }
   }
   switch (instr->definitions[0].bytes()) {
   case 1:
      sdwa->dst_sel = sdwa_ubyte;
      sdwa->dst_preserve = true;
      break;
   case 2:
      sdwa->dst_sel = sdwa_uword;
      sdwa->dst_preserve = true;
      break;
   case 4:
      sdwa->dst_sel = sdwa_udword;
      break;
   }

   if (instr->definitions[0].getTemp().type() == RegType::sgpr && chip == GFX8)
      instr->definitions[0].setFixed(vcc);
   if (instr->definitions.size() >= 2)
      instr->definitions[1].setFixed(vcc);
   if (instr->operands.size() >= 3)
      instr->operands[2].setFixed(vcc);

   return tmp;
}

bool can_use_opsel(chip_class chip, aco_opcode op, int idx, bool high)
{
   /* opsel is only GFX9+ */
   if ((high || idx == -1) && chip < GFX9)
      return false;

   switch (op) {
   case aco_opcode::v_div_fixup_f16:
   case aco_opcode::v_fma_f16:
   case aco_opcode::v_mad_f16:
   case aco_opcode::v_mad_u16:
   case aco_opcode::v_mad_i16:
   case aco_opcode::v_med3_f16:
   case aco_opcode::v_med3_i16:
   case aco_opcode::v_med3_u16:
   case aco_opcode::v_min3_f16:
   case aco_opcode::v_min3_i16:
   case aco_opcode::v_min3_u16:
   case aco_opcode::v_max3_f16:
   case aco_opcode::v_max3_i16:
   case aco_opcode::v_max3_u16:
   case aco_opcode::v_max_u16_e64:
   case aco_opcode::v_max_i16_e64:
   case aco_opcode::v_min_u16_e64:
   case aco_opcode::v_min_i16_e64:
   case aco_opcode::v_add_i16:
   case aco_opcode::v_sub_i16:
   case aco_opcode::v_add_u16_e64:
   case aco_opcode::v_sub_u16_e64:
   case aco_opcode::v_cvt_pknorm_i16_f16:
   case aco_opcode::v_cvt_pknorm_u16_f16:
   case aco_opcode::v_lshlrev_b16_e64:
   case aco_opcode::v_lshrrev_b16_e64:
   case aco_opcode::v_ashrrev_i16_e64:
   case aco_opcode::v_mul_lo_u16_e64:
      return true;
   case aco_opcode::v_pack_b32_f16:
      return idx != -1;
   case aco_opcode::v_mad_u32_u16:
   case aco_opcode::v_mad_i32_i16:
      return idx >= 0 && idx < 2;
   default:
      return false;
   }
}

}
