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

#include "aco_builder.h"

#include "util/debug.h"

#include "c11/threads.h"

namespace aco {

uint64_t debug_flags = 0;

static const struct debug_control aco_debug_options[] = {{"validateir", DEBUG_VALIDATE_IR},
                                                         {"validatera", DEBUG_VALIDATE_RA},
                                                         {"perfwarn", DEBUG_PERFWARN},
                                                         {"force-waitcnt", DEBUG_FORCE_WAITCNT},
                                                         {"novn", DEBUG_NO_VN},
                                                         {"noopt", DEBUG_NO_OPT},
                                                         {"nosched", DEBUG_NO_SCHED},
                                                         {"perfinfo", DEBUG_PERF_INFO},
                                                         {"liveinfo", DEBUG_LIVE_INFO},
                                                         {NULL, 0}};

static once_flag init_once_flag = ONCE_FLAG_INIT;

static void
init_once()
{
   debug_flags = parse_debug_string(getenv("ACO_DEBUG"), aco_debug_options);

#ifndef NDEBUG
   /* enable some flags by default on debug builds */
   debug_flags |= aco::DEBUG_VALIDATE_IR;
#endif
}

void
init()
{
   call_once(&init_once_flag, init_once);
}

void
init_program(Program* program, Stage stage, const struct radv_shader_info* info,
             enum chip_class chip_class, enum radeon_family family, bool wgp_mode,
             ac_shader_config* config)
{
   program->stage = stage;
   program->config = config;
   program->info = info;
   program->chip_class = chip_class;
   if (family == CHIP_UNKNOWN) {
      switch (chip_class) {
      case GFX6: program->family = CHIP_TAHITI; break;
      case GFX7: program->family = CHIP_BONAIRE; break;
      case GFX8: program->family = CHIP_POLARIS10; break;
      case GFX9: program->family = CHIP_VEGA10; break;
      case GFX10: program->family = CHIP_NAVI10; break;
      default: program->family = CHIP_UNKNOWN; break;
      }
   } else {
      program->family = family;
   }
   program->wave_size = info->wave_size;
   program->lane_mask = program->wave_size == 32 ? s1 : s2;

   program->dev.lds_encoding_granule = chip_class >= GFX7 ? 512 : 256;
   program->dev.lds_alloc_granule =
      chip_class >= GFX10_3 ? 1024 : program->dev.lds_encoding_granule;
   program->dev.lds_limit = chip_class >= GFX7 ? 65536 : 32768;
   /* apparently gfx702 also has 16-bank LDS but I can't find a family for that */
   program->dev.has_16bank_lds = family == CHIP_KABINI || family == CHIP_STONEY;

   program->dev.vgpr_limit = 256;
   program->dev.physical_vgprs = 256;
   program->dev.vgpr_alloc_granule = 4;

   if (chip_class >= GFX10) {
      program->dev.physical_sgprs = 5120; /* doesn't matter as long as it's at least 128 * 40 */
      program->dev.physical_vgprs = program->wave_size == 32 ? 1024 : 512;
      program->dev.sgpr_alloc_granule = 128;
      program->dev.sgpr_limit =
         108; /* includes VCC, which can be treated as s[106-107] on GFX10+ */
      if (chip_class >= GFX10_3)
         program->dev.vgpr_alloc_granule = program->wave_size == 32 ? 16 : 8;
      else
         program->dev.vgpr_alloc_granule = program->wave_size == 32 ? 8 : 4;
   } else if (program->chip_class >= GFX8) {
      program->dev.physical_sgprs = 800;
      program->dev.sgpr_alloc_granule = 16;
      program->dev.sgpr_limit = 102;
      if (family == CHIP_TONGA || family == CHIP_ICELAND)
         program->dev.sgpr_alloc_granule = 96; /* workaround hardware bug */
   } else {
      program->dev.physical_sgprs = 512;
      program->dev.sgpr_alloc_granule = 8;
      program->dev.sgpr_limit = 104;
   }

   program->dev.max_wave64_per_simd = 10;
   if (program->chip_class >= GFX10_3)
      program->dev.max_wave64_per_simd = 16;
   else if (program->chip_class == GFX10)
      program->dev.max_wave64_per_simd = 20;
   else if (program->family >= CHIP_POLARIS10 && program->family <= CHIP_VEGAM)
      program->dev.max_wave64_per_simd = 8;

   program->dev.simd_per_cu = program->chip_class >= GFX10 ? 2 : 4;

   switch (program->family) {
   /* GFX8 APUs */
   case CHIP_CARRIZO:
   case CHIP_STONEY:
   /* GFX9 APUS */
   case CHIP_RAVEN:
   case CHIP_RAVEN2:
   case CHIP_RENOIR: program->dev.xnack_enabled = true; break;
   default: break;
   }

   program->dev.sram_ecc_enabled = program->family == CHIP_ARCTURUS;
   /* apparently gfx702 also has fast v_fma_f32 but I can't find a family for that */
   program->dev.has_fast_fma32 = program->chip_class >= GFX9;
   if (program->family == CHIP_TAHITI || program->family == CHIP_CARRIZO ||
       program->family == CHIP_HAWAII)
      program->dev.has_fast_fma32 = true;

   program->wgp_mode = wgp_mode;

   program->progress = CompilationProgress::after_isel;

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

memory_sync_info
get_sync_info(const Instruction* instr)
{
   switch (instr->format) {
   case Format::SMEM: return instr->smem().sync;
   case Format::MUBUF: return instr->mubuf().sync;
   case Format::MIMG: return instr->mimg().sync;
   case Format::MTBUF: return instr->mtbuf().sync;
   case Format::FLAT:
   case Format::GLOBAL:
   case Format::SCRATCH: return instr->flatlike().sync;
   case Format::DS: return instr->ds().sync;
   default: return memory_sync_info();
   }
}

bool
can_use_SDWA(chip_class chip, const aco_ptr<Instruction>& instr, bool pre_ra)
{
   if (!instr->isVALU())
      return false;

   if (chip < GFX8 || instr->isDPP())
      return false;

   if (instr->isSDWA())
      return true;

   if (instr->isVOP3()) {
      VOP3_instruction& vop3 = instr->vop3();
      if (instr->format == Format::VOP3)
         return false;
      if (vop3.clamp && instr->isVOPC() && chip != GFX8)
         return false;
      if (vop3.omod && chip < GFX9)
         return false;

      // TODO: return true if we know we will use vcc
      if (!pre_ra && instr->definitions.size() >= 2)
         return false;

      for (unsigned i = 1; i < instr->operands.size(); i++) {
         if (instr->operands[i].isLiteral())
            return false;
         if (chip < GFX9 && !instr->operands[i].isOfType(RegType::vgpr))
            return false;
      }
   }

   if (!instr->definitions.empty() && instr->definitions[0].bytes() > 4 && !instr->isVOPC())
      return false;

   if (!instr->operands.empty()) {
      if (instr->operands[0].isLiteral())
         return false;
      if (chip < GFX9 && !instr->operands[0].isOfType(RegType::vgpr))
         return false;
      if (instr->operands[0].bytes() > 4)
         return false;
      if (instr->operands.size() > 1 && instr->operands[1].bytes() > 4)
         return false;
   }

   bool is_mac = instr->opcode == aco_opcode::v_mac_f32 || instr->opcode == aco_opcode::v_mac_f16 ||
                 instr->opcode == aco_opcode::v_fmac_f32 || instr->opcode == aco_opcode::v_fmac_f16;

   if (chip != GFX8 && is_mac)
      return false;

   // TODO: return true if we know we will use vcc
   if (!pre_ra && instr->isVOPC() && chip == GFX8)
      return false;
   if (!pre_ra && instr->operands.size() >= 3 && !is_mac)
      return false;

   return instr->opcode != aco_opcode::v_madmk_f32 && instr->opcode != aco_opcode::v_madak_f32 &&
          instr->opcode != aco_opcode::v_madmk_f16 && instr->opcode != aco_opcode::v_madak_f16 &&
          instr->opcode != aco_opcode::v_readfirstlane_b32 &&
          instr->opcode != aco_opcode::v_clrexcp && instr->opcode != aco_opcode::v_swap_b32;
}

/* updates "instr" and returns the old instruction (or NULL if no update was needed) */
aco_ptr<Instruction>
convert_to_SDWA(chip_class chip, aco_ptr<Instruction>& instr)
{
   if (instr->isSDWA())
      return NULL;

   aco_ptr<Instruction> tmp = std::move(instr);
   Format format =
      (Format)(((uint16_t)tmp->format & ~(uint16_t)Format::VOP3) | (uint16_t)Format::SDWA);
   instr.reset(create_instruction<SDWA_instruction>(tmp->opcode, format, tmp->operands.size(),
                                                    tmp->definitions.size()));
   std::copy(tmp->operands.cbegin(), tmp->operands.cend(), instr->operands.begin());
   std::copy(tmp->definitions.cbegin(), tmp->definitions.cend(), instr->definitions.begin());

   SDWA_instruction& sdwa = instr->sdwa();

   if (tmp->isVOP3()) {
      VOP3_instruction& vop3 = tmp->vop3();
      memcpy(sdwa.neg, vop3.neg, sizeof(sdwa.neg));
      memcpy(sdwa.abs, vop3.abs, sizeof(sdwa.abs));
      sdwa.omod = vop3.omod;
      sdwa.clamp = vop3.clamp;
   }

   for (unsigned i = 0; i < instr->operands.size(); i++) {
      /* SDWA only uses operands 0 and 1. */
      if (i >= 2)
         break;

      sdwa.sel[i] = SubdwordSel(instr->operands[i].bytes(), 0, false);
   }

   sdwa.dst_sel = SubdwordSel(instr->definitions[0].bytes(), 0, false);

   if (instr->definitions[0].getTemp().type() == RegType::sgpr && chip == GFX8)
      instr->definitions[0].setFixed(vcc);
   if (instr->definitions.size() >= 2)
      instr->definitions[1].setFixed(vcc);
   if (instr->operands.size() >= 3)
      instr->operands[2].setFixed(vcc);

   return tmp;
}

bool
can_use_DPP(const aco_ptr<Instruction>& instr, bool pre_ra)
{
   assert(instr->isVALU() && !instr->operands.empty());

   if (instr->isDPP())
      return true;

   if (instr->operands.size() && instr->operands[0].isLiteral())
      return false;

   if (instr->isSDWA())
      return false;

   if (!pre_ra && (instr->isVOPC() || instr->definitions.size() > 1) &&
       instr->definitions.back().physReg() != vcc)
      return false;

   if (!pre_ra && instr->operands.size() >= 3 && instr->operands[2].physReg() != vcc)
      return false;

   if (instr->isVOP3()) {
      const VOP3_instruction* vop3 = &instr->vop3();
      if (vop3->clamp || vop3->omod || vop3->opsel)
         return false;
      if (instr->format == Format::VOP3)
         return false;
      if (instr->operands.size() > 1 && !instr->operands[1].isOfType(RegType::vgpr))
         return false;
   }

   /* there are more cases but those all take 64-bit inputs */
   return instr->opcode != aco_opcode::v_madmk_f32 && instr->opcode != aco_opcode::v_madak_f32 &&
          instr->opcode != aco_opcode::v_madmk_f16 && instr->opcode != aco_opcode::v_madak_f16 &&
          instr->opcode != aco_opcode::v_readfirstlane_b32 &&
          instr->opcode != aco_opcode::v_cvt_f64_i32 &&
          instr->opcode != aco_opcode::v_cvt_f64_f32 && instr->opcode != aco_opcode::v_cvt_f64_u32;
}

aco_ptr<Instruction>
convert_to_DPP(aco_ptr<Instruction>& instr)
{
   if (instr->isDPP())
      return NULL;

   aco_ptr<Instruction> tmp = std::move(instr);
   Format format =
      (Format)(((uint32_t)tmp->format & ~(uint32_t)Format::VOP3) | (uint32_t)Format::DPP);
   instr.reset(create_instruction<DPP_instruction>(tmp->opcode, format, tmp->operands.size(),
                                                   tmp->definitions.size()));
   std::copy(tmp->operands.cbegin(), tmp->operands.cend(), instr->operands.begin());
   for (unsigned i = 0; i < instr->definitions.size(); i++)
      instr->definitions[i] = tmp->definitions[i];

   DPP_instruction* dpp = &instr->dpp();
   dpp->dpp_ctrl = dpp_quad_perm(0, 1, 2, 3);
   dpp->row_mask = 0xf;
   dpp->bank_mask = 0xf;

   if (tmp->isVOP3()) {
      const VOP3_instruction* vop3 = &tmp->vop3();
      memcpy(dpp->neg, vop3->neg, sizeof(dpp->neg));
      memcpy(dpp->abs, vop3->abs, sizeof(dpp->abs));
   }

   if (instr->isVOPC() || instr->definitions.size() > 1)
      instr->definitions.back().setFixed(vcc);

   if (instr->operands.size() >= 3)
      instr->operands[2].setFixed(vcc);

   return tmp;
}

bool
can_use_opsel(chip_class chip, aco_opcode op, int idx, bool high)
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
   case aco_opcode::v_mul_lo_u16_e64: return true;
   case aco_opcode::v_pack_b32_f16:
   case aco_opcode::v_cvt_pknorm_i16_f16:
   case aco_opcode::v_cvt_pknorm_u16_f16: return idx != -1;
   case aco_opcode::v_mad_u32_u16:
   case aco_opcode::v_mad_i32_i16: return idx >= 0 && idx < 2;
   default: return false;
   }
}

bool
instr_is_16bit(chip_class chip, aco_opcode op)
{
   /* partial register writes are GFX9+, only */
   if (chip < GFX9)
      return false;

   switch (op) {
   /* VOP3 */
   case aco_opcode::v_mad_f16:
   case aco_opcode::v_mad_u16:
   case aco_opcode::v_mad_i16:
   case aco_opcode::v_fma_f16:
   case aco_opcode::v_div_fixup_f16:
   case aco_opcode::v_interp_p2_f16:
   case aco_opcode::v_fma_mixlo_f16:
   /* VOP2 */
   case aco_opcode::v_mac_f16:
   case aco_opcode::v_madak_f16:
   case aco_opcode::v_madmk_f16: return chip >= GFX9;
   case aco_opcode::v_add_f16:
   case aco_opcode::v_sub_f16:
   case aco_opcode::v_subrev_f16:
   case aco_opcode::v_mul_f16:
   case aco_opcode::v_max_f16:
   case aco_opcode::v_min_f16:
   case aco_opcode::v_ldexp_f16:
   case aco_opcode::v_fmac_f16:
   case aco_opcode::v_fmamk_f16:
   case aco_opcode::v_fmaak_f16:
   /* VOP1 */
   case aco_opcode::v_cvt_f16_f32:
   case aco_opcode::v_cvt_f16_u16:
   case aco_opcode::v_cvt_f16_i16:
   case aco_opcode::v_rcp_f16:
   case aco_opcode::v_sqrt_f16:
   case aco_opcode::v_rsq_f16:
   case aco_opcode::v_log_f16:
   case aco_opcode::v_exp_f16:
   case aco_opcode::v_frexp_mant_f16:
   case aco_opcode::v_frexp_exp_i16_f16:
   case aco_opcode::v_floor_f16:
   case aco_opcode::v_ceil_f16:
   case aco_opcode::v_trunc_f16:
   case aco_opcode::v_rndne_f16:
   case aco_opcode::v_fract_f16:
   case aco_opcode::v_sin_f16:
   case aco_opcode::v_cos_f16: return chip >= GFX10;
   // TODO: confirm whether these write 16 or 32 bit on GFX10+
   // case aco_opcode::v_cvt_u16_f16:
   // case aco_opcode::v_cvt_i16_f16:
   // case aco_opcode::p_cvt_f16_f32_rtne:
   // case aco_opcode::v_cvt_norm_i16_f16:
   // case aco_opcode::v_cvt_norm_u16_f16:
   /* on GFX10, all opsel instructions preserve the high bits */
   default: return chip >= GFX10 && can_use_opsel(chip, op, -1, false);
   }
}

uint32_t
get_reduction_identity(ReduceOp op, unsigned idx)
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
   case umax64: return 0;
   case imul8:
   case imul16:
   case imul32:
   case imul64: return idx ? 0 : 1;
   case fmul16: return 0x3c00u;                /* 1.0 */
   case fmul32: return 0x3f800000u;            /* 1.0 */
   case fmul64: return idx ? 0x3ff00000u : 0u; /* 1.0 */
   case imin8: return INT8_MAX;
   case imin16: return INT16_MAX;
   case imin32: return INT32_MAX;
   case imin64: return idx ? 0x7fffffffu : 0xffffffffu;
   case imax8: return INT8_MIN;
   case imax16: return INT16_MIN;
   case imax32: return INT32_MIN;
   case imax64: return idx ? 0x80000000u : 0;
   case umin8:
   case umin16:
   case iand8:
   case iand16: return 0xffffffffu;
   case umin32:
   case umin64:
   case iand32:
   case iand64: return 0xffffffffu;
   case fmin16: return 0x7c00u;                /* infinity */
   case fmin32: return 0x7f800000u;            /* infinity */
   case fmin64: return idx ? 0x7ff00000u : 0u; /* infinity */
   case fmax16: return 0xfc00u;                /* negative infinity */
   case fmax32: return 0xff800000u;            /* negative infinity */
   case fmax64: return idx ? 0xfff00000u : 0u; /* negative infinity */
   default: unreachable("Invalid reduction operation"); break;
   }
   return 0;
}

bool
needs_exec_mask(const Instruction* instr)
{
   if (instr->isVALU()) {
      return instr->opcode != aco_opcode::v_readlane_b32 &&
             instr->opcode != aco_opcode::v_readlane_b32_e64 &&
             instr->opcode != aco_opcode::v_writelane_b32 &&
             instr->opcode != aco_opcode::v_writelane_b32_e64;
   }

   if (instr->isVMEM() || instr->isFlatLike())
      return true;

   if (instr->isSALU() || instr->isBranch() || instr->isSMEM() || instr->isBarrier())
      return instr->reads_exec();

   if (instr->isPseudo()) {
      switch (instr->opcode) {
      case aco_opcode::p_create_vector:
      case aco_opcode::p_extract_vector:
      case aco_opcode::p_split_vector:
      case aco_opcode::p_phi:
      case aco_opcode::p_parallelcopy:
         for (Definition def : instr->definitions) {
            if (def.getTemp().type() == RegType::vgpr)
               return true;
         }
         return instr->reads_exec();
      case aco_opcode::p_spill:
      case aco_opcode::p_reload:
      case aco_opcode::p_logical_start:
      case aco_opcode::p_logical_end:
      case aco_opcode::p_startpgm: return instr->reads_exec();
      default: break;
      }
   }

   return true;
}

struct CmpInfo {
   aco_opcode ordered;
   aco_opcode unordered;
   aco_opcode ordered_swapped;
   aco_opcode unordered_swapped;
   aco_opcode inverse;
   aco_opcode f32;
   unsigned size;
};

ALWAYS_INLINE bool
get_cmp_info(aco_opcode op, CmpInfo* info)
{
   info->ordered = aco_opcode::num_opcodes;
   info->unordered = aco_opcode::num_opcodes;
   info->ordered_swapped = aco_opcode::num_opcodes;
   info->unordered_swapped = aco_opcode::num_opcodes;
   switch (op) {
      // clang-format off
#define CMP2(ord, unord, ord_swap, unord_swap, sz)                                                 \
   case aco_opcode::v_cmp_##ord##_f##sz:                                                           \
   case aco_opcode::v_cmp_n##unord##_f##sz:                                                        \
      info->ordered = aco_opcode::v_cmp_##ord##_f##sz;                                             \
      info->unordered = aco_opcode::v_cmp_n##unord##_f##sz;                                        \
      info->ordered_swapped = aco_opcode::v_cmp_##ord_swap##_f##sz;                                \
      info->unordered_swapped = aco_opcode::v_cmp_n##unord_swap##_f##sz;                           \
      info->inverse = op == aco_opcode::v_cmp_n##unord##_f##sz ? aco_opcode::v_cmp_##unord##_f##sz \
                                                               : aco_opcode::v_cmp_n##ord##_f##sz; \
      info->f32 = op == aco_opcode::v_cmp_##ord##_f##sz ? aco_opcode::v_cmp_##ord##_f32            \
                                                        : aco_opcode::v_cmp_n##unord##_f32;        \
      info->size = sz;                                                                             \
      return true;
#define CMP(ord, unord, ord_swap, unord_swap)                                                      \
   CMP2(ord, unord, ord_swap, unord_swap, 16)                                                      \
   CMP2(ord, unord, ord_swap, unord_swap, 32)                                                      \
   CMP2(ord, unord, ord_swap, unord_swap, 64)
      CMP(lt, /*n*/ge, gt, /*n*/le)
      CMP(eq, /*n*/lg, eq, /*n*/lg)
      CMP(le, /*n*/gt, ge, /*n*/lt)
      CMP(gt, /*n*/le, lt, /*n*/le)
      CMP(lg, /*n*/eq, lg, /*n*/eq)
      CMP(ge, /*n*/lt, le, /*n*/gt)
#undef CMP
#undef CMP2
#define ORD_TEST(sz)                                                                               \
   case aco_opcode::v_cmp_u_f##sz:                                                                 \
      info->f32 = aco_opcode::v_cmp_u_f32;                                                         \
      info->inverse = aco_opcode::v_cmp_o_f##sz;                                                   \
      info->size = sz;                                                                             \
      return true;                                                                                 \
   case aco_opcode::v_cmp_o_f##sz:                                                                 \
      info->f32 = aco_opcode::v_cmp_o_f32;                                                         \
      info->inverse = aco_opcode::v_cmp_u_f##sz;                                                   \
      info->size = sz;                                                                             \
      return true;
      ORD_TEST(16)
      ORD_TEST(32)
      ORD_TEST(64)
#undef ORD_TEST
      // clang-format on
   default: return false;
   }
}

aco_opcode
get_ordered(aco_opcode op)
{
   CmpInfo info;
   return get_cmp_info(op, &info) ? info.ordered : aco_opcode::num_opcodes;
}

aco_opcode
get_unordered(aco_opcode op)
{
   CmpInfo info;
   return get_cmp_info(op, &info) ? info.unordered : aco_opcode::num_opcodes;
}

aco_opcode
get_inverse(aco_opcode op)
{
   CmpInfo info;
   return get_cmp_info(op, &info) ? info.inverse : aco_opcode::num_opcodes;
}

aco_opcode
get_f32_cmp(aco_opcode op)
{
   CmpInfo info;
   return get_cmp_info(op, &info) ? info.f32 : aco_opcode::num_opcodes;
}

unsigned
get_cmp_bitsize(aco_opcode op)
{
   CmpInfo info;
   return get_cmp_info(op, &info) ? info.size : 0;
}

bool
is_cmp(aco_opcode op)
{
   CmpInfo info;
   return get_cmp_info(op, &info) && info.ordered != aco_opcode::num_opcodes;
}

bool
can_swap_operands(aco_ptr<Instruction>& instr, aco_opcode* new_op)
{
   if (instr->isDPP())
      return false;

   if (instr->operands[0].isConstant() ||
       (instr->operands[0].isTemp() && instr->operands[0].getTemp().type() == RegType::sgpr))
      return false;

   switch (instr->opcode) {
   case aco_opcode::v_add_u32:
   case aco_opcode::v_add_co_u32:
   case aco_opcode::v_add_co_u32_e64:
   case aco_opcode::v_add_i32:
   case aco_opcode::v_add_f16:
   case aco_opcode::v_add_f32:
   case aco_opcode::v_mul_f16:
   case aco_opcode::v_mul_f32:
   case aco_opcode::v_or_b32:
   case aco_opcode::v_and_b32:
   case aco_opcode::v_xor_b32:
   case aco_opcode::v_max_f16:
   case aco_opcode::v_max_f32:
   case aco_opcode::v_min_f16:
   case aco_opcode::v_min_f32:
   case aco_opcode::v_max_i32:
   case aco_opcode::v_min_i32:
   case aco_opcode::v_max_u32:
   case aco_opcode::v_min_u32:
   case aco_opcode::v_max_i16:
   case aco_opcode::v_min_i16:
   case aco_opcode::v_max_u16:
   case aco_opcode::v_min_u16:
   case aco_opcode::v_max_i16_e64:
   case aco_opcode::v_min_i16_e64:
   case aco_opcode::v_max_u16_e64:
   case aco_opcode::v_min_u16_e64: *new_op = instr->opcode; return true;
   case aco_opcode::v_sub_f16: *new_op = aco_opcode::v_subrev_f16; return true;
   case aco_opcode::v_sub_f32: *new_op = aco_opcode::v_subrev_f32; return true;
   case aco_opcode::v_sub_co_u32: *new_op = aco_opcode::v_subrev_co_u32; return true;
   case aco_opcode::v_sub_u16: *new_op = aco_opcode::v_subrev_u16; return true;
   case aco_opcode::v_sub_u32: *new_op = aco_opcode::v_subrev_u32; return true;
   default: {
      CmpInfo info;
      get_cmp_info(instr->opcode, &info);
      if (info.ordered == instr->opcode) {
         *new_op = info.ordered_swapped;
         return true;
      }
      if (info.unordered == instr->opcode) {
         *new_op = info.unordered_swapped;
         return true;
      }
      return false;
   }
   }
}

wait_imm::wait_imm() : vm(unset_counter), exp(unset_counter), lgkm(unset_counter), vs(unset_counter)
{}
wait_imm::wait_imm(uint16_t vm_, uint16_t exp_, uint16_t lgkm_, uint16_t vs_)
    : vm(vm_), exp(exp_), lgkm(lgkm_), vs(vs_)
{}

wait_imm::wait_imm(enum chip_class chip, uint16_t packed) : vs(unset_counter)
{
   vm = packed & 0xf;
   if (chip >= GFX9)
      vm |= (packed >> 10) & 0x30;

   exp = (packed >> 4) & 0x7;

   lgkm = (packed >> 8) & 0xf;
   if (chip >= GFX10)
      lgkm |= (packed >> 8) & 0x30;
}

uint16_t
wait_imm::pack(enum chip_class chip) const
{
   uint16_t imm = 0;
   assert(exp == unset_counter || exp <= 0x7);
   switch (chip) {
   case GFX10:
   case GFX10_3:
      assert(lgkm == unset_counter || lgkm <= 0x3f);
      assert(vm == unset_counter || vm <= 0x3f);
      imm = ((vm & 0x30) << 10) | ((lgkm & 0x3f) << 8) | ((exp & 0x7) << 4) | (vm & 0xf);
      break;
   case GFX9:
      assert(lgkm == unset_counter || lgkm <= 0xf);
      assert(vm == unset_counter || vm <= 0x3f);
      imm = ((vm & 0x30) << 10) | ((lgkm & 0xf) << 8) | ((exp & 0x7) << 4) | (vm & 0xf);
      break;
   default:
      assert(lgkm == unset_counter || lgkm <= 0xf);
      assert(vm == unset_counter || vm <= 0xf);
      imm = ((lgkm & 0xf) << 8) | ((exp & 0x7) << 4) | (vm & 0xf);
      break;
   }
   if (chip < GFX9 && vm == wait_imm::unset_counter)
      imm |= 0xc000; /* should have no effect on pre-GFX9 and now we won't have to worry about the
                        architecture when interpreting the immediate */
   if (chip < GFX10 && lgkm == wait_imm::unset_counter)
      imm |= 0x3000; /* should have no effect on pre-GFX10 and now we won't have to worry about the
                        architecture when interpreting the immediate */
   return imm;
}

bool
wait_imm::combine(const wait_imm& other)
{
   bool changed = other.vm < vm || other.exp < exp || other.lgkm < lgkm || other.vs < vs;
   vm = std::min(vm, other.vm);
   exp = std::min(exp, other.exp);
   lgkm = std::min(lgkm, other.lgkm);
   vs = std::min(vs, other.vs);
   return changed;
}

bool
wait_imm::empty() const
{
   return vm == unset_counter && exp == unset_counter && lgkm == unset_counter &&
          vs == unset_counter;
}

bool
should_form_clause(const Instruction* a, const Instruction* b)
{
   /* Vertex attribute loads from the same binding likely load from similar addresses */
   unsigned a_vtx_binding =
      a->isMUBUF() ? a->mubuf().vtx_binding : (a->isMTBUF() ? a->mtbuf().vtx_binding : 0);
   unsigned b_vtx_binding =
      b->isMUBUF() ? b->mubuf().vtx_binding : (b->isMTBUF() ? b->mtbuf().vtx_binding : 0);
   if (a_vtx_binding && a_vtx_binding == b_vtx_binding)
      return true;

   if (a->format != b->format)
      return false;

   /* Assume loads which don't use descriptors might load from similar addresses. */
   if (a->isFlatLike())
      return true;
   if (a->isSMEM() && a->operands[0].bytes() == 8 && b->operands[0].bytes() == 8)
      return true;

   /* If they load from the same descriptor, assume they might load from similar
    * addresses.
    */
   if (a->isVMEM() || a->isSMEM())
      return a->operands[0].tempId() == b->operands[0].tempId();

   return false;
}

} // namespace aco
