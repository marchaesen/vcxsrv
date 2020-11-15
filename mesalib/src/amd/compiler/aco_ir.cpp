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
#include "vulkan/radv_shader.h"
#include "c11/threads.h"
#include "util/debug.h"

namespace aco {

uint64_t debug_flags = 0;

static const struct debug_control aco_debug_options[] = {
   {"validateir", DEBUG_VALIDATE_IR},
   {"validatera", DEBUG_VALIDATE_RA},
   {"perfwarn", DEBUG_PERFWARN},
   {"force-waitcnt", DEBUG_FORCE_WAITCNT},
   {"novn", DEBUG_NO_VN},
   {"noopt", DEBUG_NO_OPT},
   {"nosched", DEBUG_NO_SCHED},
   {NULL, 0}
};

static once_flag init_once_flag = ONCE_FLAG_INIT;

static void init_once()
{
   debug_flags = parse_debug_string(getenv("ACO_DEBUG"), aco_debug_options);

   #ifndef NDEBUG
   /* enable some flags by default on debug builds */
   debug_flags |= aco::DEBUG_VALIDATE_IR;
   #endif
}

void init()
{
   call_once(&init_once_flag, init_once);
}

void init_program(Program *program, Stage stage, struct radv_shader_info *info,
                  enum chip_class chip_class, enum radeon_family family,
                  ac_shader_config *config)
{
   program->stage = stage;
   program->config = config;
   program->info = info;
   program->chip_class = chip_class;
   if (family == CHIP_UNKNOWN) {
      switch (chip_class) {
      case GFX6:
         program->family = CHIP_TAHITI;
         break;
      case GFX7:
         program->family = CHIP_BONAIRE;
         break;
      case GFX8:
         program->family = CHIP_POLARIS10;
         break;
      case GFX9:
         program->family = CHIP_VEGA10;
         break;
      case GFX10:
         program->family = CHIP_NAVI10;
         break;
      default:
         program->family = CHIP_UNKNOWN;
         break;
      }
   } else {
      program->family = family;
   }
   program->wave_size = info->wave_size;
   program->lane_mask = program->wave_size == 32 ? s1 : s2;

   program->lds_alloc_granule = chip_class >= GFX7 ? 512 : 256;
   program->lds_limit = chip_class >= GFX7 ? 65536 : 32768;
   /* apparently gfx702 also has 16-bank LDS but I can't find a family for that */
   program->has_16bank_lds = family == CHIP_KABINI || family == CHIP_STONEY;

   program->vgpr_limit = 256;
   program->vgpr_alloc_granule = 3;

   if (chip_class >= GFX10) {
      program->physical_sgprs = 2560; /* doesn't matter as long as it's at least 128 * 20 */
      program->sgpr_alloc_granule = 127;
      program->sgpr_limit = 106;
      if (chip_class >= GFX10_3)
         program->vgpr_alloc_granule = program->wave_size == 32 ? 15 : 7;
      else
         program->vgpr_alloc_granule = program->wave_size == 32 ? 7 : 3;
   } else if (program->chip_class >= GFX8) {
      program->physical_sgprs = 800;
      program->sgpr_alloc_granule = 15;
      if (family == CHIP_TONGA || family == CHIP_ICELAND)
         program->sgpr_limit = 94; /* workaround hardware bug */
      else
         program->sgpr_limit = 102;
   } else {
      program->physical_sgprs = 512;
      program->sgpr_alloc_granule = 7;
      program->sgpr_limit = 104;
   }

   program->next_fp_mode.preserve_signed_zero_inf_nan32 = false;
   program->next_fp_mode.preserve_signed_zero_inf_nan16_64 = false;
   program->next_fp_mode.must_flush_denorms32 = false;
   program->next_fp_mode.must_flush_denorms16_64 = false;
   program->next_fp_mode.care_about_round32 = false;
   program->next_fp_mode.care_about_round16_64 = false;
   program->next_fp_mode.denorm16_64 = fp_denorm_keep;
   program->next_fp_mode.denorm32 = 0;
   program->next_fp_mode.round16_64 = fp_round_ne;
   program->next_fp_mode.round32 = fp_round_ne;
}

memory_sync_info get_sync_info(const Instruction* instr)
{
   switch (instr->format) {
   case Format::SMEM:
      return static_cast<const SMEM_instruction*>(instr)->sync;
   case Format::MUBUF:
      return static_cast<const MUBUF_instruction*>(instr)->sync;
   case Format::MIMG:
      return static_cast<const MIMG_instruction*>(instr)->sync;
   case Format::MTBUF:
      return static_cast<const MTBUF_instruction*>(instr)->sync;
   case Format::FLAT:
   case Format::GLOBAL:
   case Format::SCRATCH:
      return static_cast<const FLAT_instruction*>(instr)->sync;
   case Format::DS:
      return static_cast<const DS_instruction*>(instr)->sync;
   default:
      return memory_sync_info();
   }
}

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
      /* SDWA only uses operands 0 and 1. */
      if (i >= 2)
         break;

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
   case aco_opcode::v_lshlrev_b16_e64:
   case aco_opcode::v_lshrrev_b16_e64:
   case aco_opcode::v_ashrrev_i16_e64:
   case aco_opcode::v_mul_lo_u16_e64:
      return true;
   case aco_opcode::v_pack_b32_f16:
   case aco_opcode::v_cvt_pknorm_i16_f16:
   case aco_opcode::v_cvt_pknorm_u16_f16:
      return idx != -1;
   case aco_opcode::v_mad_u32_u16:
   case aco_opcode::v_mad_i32_i16:
      return idx >= 0 && idx < 2;
   default:
      return false;
   }
}

uint32_t get_reduction_identity(ReduceOp op, unsigned idx)
{
   switch (op) {
   case iadd8:
   case iadd16:
   case iadd32:
   case iadd64:
   case fadd16:
   case fadd32:
   case fadd64:
   case ior8:
   case ior16:
   case ior32:
   case ior64:
   case ixor8:
   case ixor16:
   case ixor32:
   case ixor64:
   case umax8:
   case umax16:
   case umax32:
   case umax64:
      return 0;
   case imul8:
   case imul16:
   case imul32:
   case imul64:
      return idx ? 0 : 1;
   case fmul16:
      return 0x3c00u; /* 1.0 */
   case fmul32:
      return 0x3f800000u; /* 1.0 */
   case fmul64:
      return idx ? 0x3ff00000u : 0u; /* 1.0 */
   case imin8:
      return INT8_MAX;
   case imin16:
      return INT16_MAX;
   case imin32:
      return INT32_MAX;
   case imin64:
      return idx ? 0x7fffffffu : 0xffffffffu;
   case imax8:
      return INT8_MIN;
   case imax16:
      return INT16_MIN;
   case imax32:
      return INT32_MIN;
   case imax64:
      return idx ? 0x80000000u : 0;
   case umin8:
   case umin16:
   case iand8:
   case iand16:
      return 0xffffffffu;
   case umin32:
   case umin64:
   case iand32:
   case iand64:
      return 0xffffffffu;
   case fmin16:
      return 0x7c00u; /* infinity */
   case fmin32:
      return 0x7f800000u; /* infinity */
   case fmin64:
      return idx ? 0x7ff00000u : 0u; /* infinity */
   case fmax16:
      return 0xfc00u; /* negative infinity */
   case fmax32:
      return 0xff800000u; /* negative infinity */
   case fmax64:
      return idx ? 0xfff00000u : 0u; /* negative infinity */
   default:
      unreachable("Invalid reduction operation");
      break;
   }
   return 0;
}

}
