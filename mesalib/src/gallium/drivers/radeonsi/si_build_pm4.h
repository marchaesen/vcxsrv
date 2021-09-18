/*
 * Copyright 2013 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * This file contains helpers for writing commands to commands streams.
 */

#ifndef SI_BUILD_PM4_H
#define SI_BUILD_PM4_H

#include "si_pipe.h"
#include "sid.h"

#if 0
#include "ac_shadowed_regs.h"
#define SI_CHECK_SHADOWED_REGS(reg_offset, count) ac_check_shadowed_regs(GFX10, CHIP_NAVI14, reg_offset, count)
#else
#define SI_CHECK_SHADOWED_REGS(reg_offset, count)
#endif

#define radeon_begin(cs) struct radeon_cmdbuf *__cs = (cs); \
                         unsigned __cs_num = __cs->current.cdw; \
                         UNUSED unsigned __cs_num_initial = __cs_num; \
                         uint32_t *__cs_buf = __cs->current.buf

#define radeon_begin_again(cs) do { \
   assert(__cs == NULL); \
   __cs = (cs); \
   __cs_num = __cs->current.cdw; \
   __cs_num_initial = __cs_num; \
   __cs_buf = __cs->current.buf; \
} while (0)

#define radeon_end() do { \
   __cs->current.cdw = __cs_num; \
   assert(__cs->current.cdw <= __cs->current.max_dw); \
   __cs = NULL; \
} while (0)

#define radeon_emit(cs, value)  __cs_buf[__cs_num++] = (value)
#define radeon_packets_added()  (__cs_num != __cs_num_initial)

#define radeon_end_update_context_roll(sctx) do { \
   radeon_end(); \
   if (radeon_packets_added()) \
      (sctx)->context_roll = true; \
} while (0)

#define radeon_emit_array(cs, values, num) do { \
   unsigned __n = (num); \
   memcpy(__cs_buf + __cs_num, (values), __n * 4); \
   __cs_num += __n; \
} while (0)

#define radeon_set_config_reg_seq(cs, reg, num) do { \
   SI_CHECK_SHADOWED_REGS(reg, num); \
   assert((reg) < SI_CONTEXT_REG_OFFSET); \
   radeon_emit(cs, PKT3(PKT3_SET_CONFIG_REG, num, 0)); \
   radeon_emit(cs, ((reg) - SI_CONFIG_REG_OFFSET) >> 2); \
} while (0)

#define radeon_set_config_reg(cs, reg, value) do { \
   radeon_set_config_reg_seq(cs, reg, 1); \
   radeon_emit(cs, value); \
} while (0)

#define radeon_set_context_reg_seq(cs, reg, num) do { \
   SI_CHECK_SHADOWED_REGS(reg, num); \
   assert((reg) >= SI_CONTEXT_REG_OFFSET); \
   radeon_emit(cs, PKT3(PKT3_SET_CONTEXT_REG, num, 0)); \
   radeon_emit(cs, ((reg) - SI_CONTEXT_REG_OFFSET) >> 2); \
} while (0)

#define radeon_set_context_reg(cs, reg, value) do { \
   radeon_set_context_reg_seq(cs, reg, 1); \
   radeon_emit(cs, value); \
} while (0)

#define radeon_set_context_reg_seq_array(cs, reg, num, values) do { \
   radeon_set_context_reg_seq(cs, reg, num); \
   radeon_emit_array(cs, values, num); \
} while (0)

#define radeon_set_context_reg_idx(cs, reg, idx, value) do { \
   SI_CHECK_SHADOWED_REGS(reg, 1); \
   assert((reg) >= SI_CONTEXT_REG_OFFSET); \
   radeon_emit(cs, PKT3(PKT3_SET_CONTEXT_REG, 1, 0)); \
   radeon_emit(cs, ((reg) - SI_CONTEXT_REG_OFFSET) >> 2 | ((idx) << 28)); \
   radeon_emit(cs, value); \
} while (0)

#define radeon_set_sh_reg_seq(cs, reg, num) do { \
   SI_CHECK_SHADOWED_REGS(reg, num); \
   assert((reg) >= SI_SH_REG_OFFSET && (reg) < SI_SH_REG_END); \
   radeon_emit(cs, PKT3(PKT3_SET_SH_REG, num, 0)); \
   radeon_emit(cs, ((reg) - SI_SH_REG_OFFSET) >> 2); \
} while (0)

#define radeon_set_sh_reg(cs, reg, value) do { \
   radeon_set_sh_reg_seq(cs, reg, 1); \
   radeon_emit(cs, value); \
} while (0)

#define radeon_set_uconfig_reg_seq(cs, reg, num, perfctr) do { \
   SI_CHECK_SHADOWED_REGS(reg, num); \
   assert((reg) >= CIK_UCONFIG_REG_OFFSET && (reg) < CIK_UCONFIG_REG_END); \
   radeon_emit(cs, PKT3(PKT3_SET_UCONFIG_REG, num, perfctr)); \
   radeon_emit(cs, ((reg) - CIK_UCONFIG_REG_OFFSET) >> 2); \
} while (0)

#define radeon_set_uconfig_reg(cs, reg, value) do { \
   radeon_set_uconfig_reg_seq(cs, reg, 1, false); \
   radeon_emit(cs, value); \
} while (0)

#define radeon_set_uconfig_reg_perfctr(cs, reg, value) do { \
   radeon_set_uconfig_reg_seq(cs, reg, 1, true); \
   radeon_emit(cs, value); \
} while (0)

#define radeon_set_uconfig_reg_idx(cs, screen, chip_class, reg, idx, value) do { \
   SI_CHECK_SHADOWED_REGS(reg, 1); \
   assert((reg) >= CIK_UCONFIG_REG_OFFSET && (reg) < CIK_UCONFIG_REG_END); \
   assert((idx) != 0); \
   unsigned __opcode = PKT3_SET_UCONFIG_REG_INDEX; \
   if ((chip_class) < GFX9 || \
       ((chip_class) == GFX9 && (screen)->info.me_fw_version < 26)) \
      __opcode = PKT3_SET_UCONFIG_REG; \
   radeon_emit(cs, PKT3(__opcode, 1, 0)); \
   radeon_emit(cs, ((reg) - CIK_UCONFIG_REG_OFFSET) >> 2 | ((idx) << 28)); \
   radeon_emit(cs, value); \
} while (0)

#define radeon_set_context_reg_rmw(cs, reg, value, mask) do { \
   SI_CHECK_SHADOWED_REGS(reg, 1); \
   assert((reg) >= SI_CONTEXT_REG_OFFSET); \
   radeon_emit(cs, PKT3(PKT3_CONTEXT_REG_RMW, 2, 0)); \
   radeon_emit(cs, ((reg) - SI_CONTEXT_REG_OFFSET) >> 2); \
   radeon_emit(cs, mask); \
   radeon_emit(cs, value); \
} while (0)

/* Emit PKT3_CONTEXT_REG_RMW if the register value is different. */
#define radeon_opt_set_context_reg_rmw(sctx, offset, reg, val, mask) do { \
   unsigned __value = (val); \
   assert((__value & ~mask) == 0); \
   __value &= mask; \
   if (((sctx->tracked_regs.reg_saved >> (reg)) & 0x1) != 0x1 || \
       sctx->tracked_regs.reg_value[reg] != __value) { \
      radeon_set_context_reg_rmw(cs, offset, __value, mask); \
      sctx->tracked_regs.reg_saved |= 0x1ull << (reg); \
      sctx->tracked_regs.reg_value[reg] = __value; \
   } \
} while (0)

/* Emit PKT3_SET_CONTEXT_REG if the register value is different. */
#define radeon_opt_set_context_reg(sctx, offset, reg, val) do { \
   unsigned __value = val; \
   if (((sctx->tracked_regs.reg_saved >> (reg)) & 0x1) != 0x1 || \
       sctx->tracked_regs.reg_value[reg] != __value) { \
      radeon_set_context_reg(cs, offset, __value); \
      sctx->tracked_regs.reg_saved |= 0x1ull << (reg); \
      sctx->tracked_regs.reg_value[reg] = __value; \
   } \
} while (0)

/**
 * Set 2 consecutive registers if any registers value is different.
 * @param offset        starting register offset
 * @param val1          is written to first register
 * @param val2          is written to second register
 */
#define radeon_opt_set_context_reg2(sctx, offset, reg, val1, val2) do { \
   unsigned __value1 = (val1), __value2 = (val2); \
   if (((sctx->tracked_regs.reg_saved >> (reg)) & 0x3) != 0x3 || \
       sctx->tracked_regs.reg_value[reg] != __value1 || \
       sctx->tracked_regs.reg_value[(reg) + 1] != __value2) { \
      radeon_set_context_reg_seq(cs, offset, 2); \
      radeon_emit(cs, __value1); \
      radeon_emit(cs, __value2); \
      sctx->tracked_regs.reg_value[reg] = __value1; \
      sctx->tracked_regs.reg_value[(reg) + 1] = __value2; \
      sctx->tracked_regs.reg_saved |= 0x3ull << (reg); \
   } \
} while (0)

/**
 * Set 3 consecutive registers if any registers value is different.
 */
#define radeon_opt_set_context_reg3(sctx, offset, reg, val1, val2, val3) do { \
   unsigned __value1 = (val1), __value2 = (val2), __value3 = (val3); \
   if (((sctx->tracked_regs.reg_saved >> (reg)) & 0x7) != 0x7 || \
       sctx->tracked_regs.reg_value[reg] != __value1 || \
       sctx->tracked_regs.reg_value[(reg) + 1] != __value2 || \
       sctx->tracked_regs.reg_value[(reg) + 2] != __value3) { \
      radeon_set_context_reg_seq(cs, offset, 3); \
      radeon_emit(cs, __value1); \
      radeon_emit(cs, __value2); \
      radeon_emit(cs, __value3); \
      sctx->tracked_regs.reg_value[reg] = __value1; \
      sctx->tracked_regs.reg_value[(reg) + 1] = __value2; \
      sctx->tracked_regs.reg_value[(reg) + 2] = __value3; \
      sctx->tracked_regs.reg_saved |= 0x7ull << (reg); \
   } \
} while (0)

/**
 * Set 4 consecutive registers if any registers value is different.
 */
#define radeon_opt_set_context_reg4(sctx, offset, reg, val1, val2, val3, val4) do { \
   unsigned __value1 = (val1), __value2 = (val2), __value3 = (val3), __value4 = (val4); \
   if (((sctx->tracked_regs.reg_saved >> (reg)) & 0xf) != 0xf || \
       sctx->tracked_regs.reg_value[reg] != __value1 || \
       sctx->tracked_regs.reg_value[(reg) + 1] != __value2 || \
       sctx->tracked_regs.reg_value[(reg) + 2] != __value3 || \
       sctx->tracked_regs.reg_value[(reg) + 3] != __value4) { \
      radeon_set_context_reg_seq(cs, offset, 4); \
      radeon_emit(cs, __value1); \
      radeon_emit(cs, __value2); \
      radeon_emit(cs, __value3); \
      radeon_emit(cs, __value4); \
      sctx->tracked_regs.reg_value[reg] = __value1; \
      sctx->tracked_regs.reg_value[(reg) + 1] = __value2; \
      sctx->tracked_regs.reg_value[(reg) + 2] = __value3; \
      sctx->tracked_regs.reg_value[(reg) + 3] = __value4; \
      sctx->tracked_regs.reg_saved |= 0xfull << (reg); \
   } \
} while (0)

/**
 * Set consecutive registers if any registers value is different.
 */
#define radeon_opt_set_context_regn(sctx, offset, value, saved_val, num) do { \
   if (memcmp(value, saved_val, sizeof(uint32_t) * (num))) { \
      radeon_set_context_reg_seq(&(sctx)->gfx_cs, offset, num); \
      radeon_emit_array(cs, value, num); \
      memcpy(saved_val, value, sizeof(uint32_t) * (num)); \
   } \
} while (0)

#define radeon_opt_set_sh_reg(sctx, offset, reg, val) do { \
   unsigned __value = val; \
   if (((sctx->tracked_regs.reg_saved >> (reg)) & 0x1) != 0x1 || \
       sctx->tracked_regs.reg_value[reg] != __value) { \
      radeon_set_sh_reg(cs, offset, __value); \
      sctx->tracked_regs.reg_saved |= BITFIELD64_BIT(reg); \
      sctx->tracked_regs.reg_value[reg] = __value; \
   } \
} while (0)

#define radeon_opt_set_uconfig_reg(sctx, offset, reg, val) do { \
   unsigned __value = val; \
   if (((sctx->tracked_regs.reg_saved >> (reg)) & 0x1) != 0x1 || \
       sctx->tracked_regs.reg_value[reg] != __value) { \
      radeon_set_uconfig_reg(cs, offset, __value); \
      sctx->tracked_regs.reg_saved |= 0x1ull << (reg); \
      sctx->tracked_regs.reg_value[reg] = __value; \
   } \
} while (0)

#define radeon_set_privileged_config_reg(cs, reg, value) do { \
   assert((reg) < CIK_UCONFIG_REG_OFFSET); \
   radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0)); \
   radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_IMM) | \
               COPY_DATA_DST_SEL(COPY_DATA_PERF)); \
   radeon_emit(cs, value); \
   radeon_emit(cs, 0); /* unused */ \
   radeon_emit(cs, (reg) >> 2); \
   radeon_emit(cs, 0); /* unused */ \
} while (0)

#define radeon_emit_32bit_pointer(sscreen, cs, va) do { \
   radeon_emit(cs, va); \
   assert((va) == 0 || ((va) >> 32) == sscreen->info.address32_hi); \
} while (0)

#define radeon_emit_one_32bit_pointer(sctx, desc, sh_base) do { \
   unsigned sh_offset = (sh_base) + (desc)->shader_userdata_offset; \
   radeon_set_sh_reg_seq(cs, sh_offset, 1); \
   radeon_emit_32bit_pointer(sctx->screen, cs, (desc)->gpu_address); \
} while (0)

/* This should be evaluated at compile time if all parameters are constants. */
static ALWAYS_INLINE unsigned
si_get_user_data_base(enum chip_class chip_class, enum si_has_tess has_tess,
                      enum si_has_gs has_gs, enum si_has_ngg ngg,
                      enum pipe_shader_type shader)
{
   switch (shader) {
   case PIPE_SHADER_VERTEX:
      /* VS can be bound as VS, ES, or LS. */
      if (has_tess) {
         if (chip_class >= GFX10) {
            return R_00B430_SPI_SHADER_USER_DATA_HS_0;
         } else if (chip_class == GFX9) {
            return R_00B430_SPI_SHADER_USER_DATA_LS_0;
         } else {
            return R_00B530_SPI_SHADER_USER_DATA_LS_0;
         }
      } else if (chip_class >= GFX10) {
         if (ngg || has_gs) {
            return R_00B230_SPI_SHADER_USER_DATA_GS_0;
         } else {
            return R_00B130_SPI_SHADER_USER_DATA_VS_0;
         }
      } else if (has_gs) {
         return R_00B330_SPI_SHADER_USER_DATA_ES_0;
      } else {
         return R_00B130_SPI_SHADER_USER_DATA_VS_0;
      }

   case PIPE_SHADER_TESS_CTRL:
      if (chip_class == GFX9) {
         return R_00B430_SPI_SHADER_USER_DATA_LS_0;
      } else {
         return R_00B430_SPI_SHADER_USER_DATA_HS_0;
      }

   case PIPE_SHADER_TESS_EVAL:
      /* TES can be bound as ES, VS, or not bound. */
      if (has_tess) {
         if (chip_class >= GFX10) {
            if (ngg || has_gs) {
               return R_00B230_SPI_SHADER_USER_DATA_GS_0;
            } else {
               return R_00B130_SPI_SHADER_USER_DATA_VS_0;
            }
         } else if (has_gs) {
            return R_00B330_SPI_SHADER_USER_DATA_ES_0;
         } else {
            return R_00B130_SPI_SHADER_USER_DATA_VS_0;
         }
      } else {
         return 0;
      }

   case PIPE_SHADER_GEOMETRY:
      if (chip_class == GFX9) {
         return R_00B330_SPI_SHADER_USER_DATA_ES_0;
      } else {
         return R_00B230_SPI_SHADER_USER_DATA_GS_0;
      }

   default:
      assert(0);
      return 0;
   }
}

#endif
