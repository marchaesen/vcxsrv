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

static inline void radeon_set_config_reg_seq(struct radeon_cmdbuf *cs, unsigned reg, unsigned num)
{
   SI_CHECK_SHADOWED_REGS(reg, num);
   assert(reg < SI_CONTEXT_REG_OFFSET);
   assert(cs->current.cdw + 2 + num <= cs->current.max_dw);
   radeon_emit(cs, PKT3(PKT3_SET_CONFIG_REG, num, 0));
   radeon_emit(cs, (reg - SI_CONFIG_REG_OFFSET) >> 2);
}

static inline void radeon_set_config_reg(struct radeon_cmdbuf *cs, unsigned reg, unsigned value)
{
   radeon_set_config_reg_seq(cs, reg, 1);
   radeon_emit(cs, value);
}

static inline void radeon_set_context_reg_seq(struct radeon_cmdbuf *cs, unsigned reg, unsigned num)
{
   SI_CHECK_SHADOWED_REGS(reg, num);
   assert(reg >= SI_CONTEXT_REG_OFFSET);
   assert(cs->current.cdw + 2 + num <= cs->current.max_dw);
   radeon_emit(cs, PKT3(PKT3_SET_CONTEXT_REG, num, 0));
   radeon_emit(cs, (reg - SI_CONTEXT_REG_OFFSET) >> 2);
}

static inline void radeon_set_context_reg(struct radeon_cmdbuf *cs, unsigned reg, unsigned value)
{
   radeon_set_context_reg_seq(cs, reg, 1);
   radeon_emit(cs, value);
}

static inline void radeon_set_context_reg_seq_array(struct radeon_cmdbuf *cs, unsigned reg,
                                                    unsigned num, const uint32_t *values)
{
   radeon_set_context_reg_seq(cs, reg, num);
   radeon_emit_array(cs, values, num);
}

static inline void radeon_set_context_reg_idx(struct radeon_cmdbuf *cs, unsigned reg, unsigned idx,
                                              unsigned value)
{
   SI_CHECK_SHADOWED_REGS(reg, 1);
   assert(reg >= SI_CONTEXT_REG_OFFSET);
   assert(cs->current.cdw + 3 <= cs->current.max_dw);
   radeon_emit(cs, PKT3(PKT3_SET_CONTEXT_REG, 1, 0));
   radeon_emit(cs, (reg - SI_CONTEXT_REG_OFFSET) >> 2 | (idx << 28));
   radeon_emit(cs, value);
}

static inline void radeon_set_sh_reg_seq(struct radeon_cmdbuf *cs, unsigned reg, unsigned num)
{
   SI_CHECK_SHADOWED_REGS(reg, num);
   assert(reg >= SI_SH_REG_OFFSET && reg < SI_SH_REG_END);
   assert(cs->current.cdw + 2 + num <= cs->current.max_dw);
   radeon_emit(cs, PKT3(PKT3_SET_SH_REG, num, 0));
   radeon_emit(cs, (reg - SI_SH_REG_OFFSET) >> 2);
}

static inline void radeon_set_sh_reg(struct radeon_cmdbuf *cs, unsigned reg, unsigned value)
{
   radeon_set_sh_reg_seq(cs, reg, 1);
   radeon_emit(cs, value);
}

static inline void radeon_set_uconfig_reg_seq(struct radeon_cmdbuf *cs, unsigned reg, unsigned num)
{
   SI_CHECK_SHADOWED_REGS(reg, num);
   assert(reg >= CIK_UCONFIG_REG_OFFSET && reg < CIK_UCONFIG_REG_END);
   assert(cs->current.cdw + 2 + num <= cs->current.max_dw);
   radeon_emit(cs, PKT3(PKT3_SET_UCONFIG_REG, num, 0));
   radeon_emit(cs, (reg - CIK_UCONFIG_REG_OFFSET) >> 2);
}

static inline void radeon_set_uconfig_reg(struct radeon_cmdbuf *cs, unsigned reg, unsigned value)
{
   radeon_set_uconfig_reg_seq(cs, reg, 1);
   radeon_emit(cs, value);
}

static inline void radeon_set_uconfig_reg_idx(struct radeon_cmdbuf *cs, struct si_screen *screen,
                                              unsigned reg, unsigned idx, unsigned value)
{
   SI_CHECK_SHADOWED_REGS(reg, 1);
   assert(reg >= CIK_UCONFIG_REG_OFFSET && reg < CIK_UCONFIG_REG_END);
   assert(cs->current.cdw + 3 <= cs->current.max_dw);
   assert(idx != 0);
   unsigned opcode = PKT3_SET_UCONFIG_REG_INDEX;
   if (screen->info.chip_class < GFX9 ||
       (screen->info.chip_class == GFX9 && screen->info.me_fw_version < 26))
      opcode = PKT3_SET_UCONFIG_REG;
   radeon_emit(cs, PKT3(opcode, 1, 0));
   radeon_emit(cs, (reg - CIK_UCONFIG_REG_OFFSET) >> 2 | (idx << 28));
   radeon_emit(cs, value);
}

static inline void radeon_set_context_reg_rmw(struct radeon_cmdbuf *cs, unsigned reg,
                                              unsigned value, unsigned mask)
{
   SI_CHECK_SHADOWED_REGS(reg, 1);
   assert(reg >= SI_CONTEXT_REG_OFFSET);
   assert(cs->current.cdw + 4 <= cs->current.max_dw);
   radeon_emit(cs, PKT3(PKT3_CONTEXT_REG_RMW, 2, 0));
   radeon_emit(cs, (reg - SI_CONTEXT_REG_OFFSET) >> 2);
   radeon_emit(cs, mask);
   radeon_emit(cs, value);
}

/* Emit PKT3_CONTEXT_REG_RMW if the register value is different. */
static inline void radeon_opt_set_context_reg_rmw(struct si_context *sctx, unsigned offset,
                                                  enum si_tracked_reg reg, unsigned value,
                                                  unsigned mask)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   assert((value & ~mask) == 0);
   value &= mask;

   if (((sctx->tracked_regs.reg_saved >> reg) & 0x1) != 0x1 ||
       sctx->tracked_regs.reg_value[reg] != value) {
      radeon_set_context_reg_rmw(cs, offset, value, mask);

      sctx->tracked_regs.reg_saved |= 0x1ull << reg;
      sctx->tracked_regs.reg_value[reg] = value;
   }
}

/* Emit PKT3_SET_CONTEXT_REG if the register value is different. */
static inline void radeon_opt_set_context_reg(struct si_context *sctx, unsigned offset,
                                              enum si_tracked_reg reg, unsigned value)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   if (((sctx->tracked_regs.reg_saved >> reg) & 0x1) != 0x1 ||
       sctx->tracked_regs.reg_value[reg] != value) {
      radeon_set_context_reg(cs, offset, value);

      sctx->tracked_regs.reg_saved |= 0x1ull << reg;
      sctx->tracked_regs.reg_value[reg] = value;
   }
}

/**
 * Set 2 consecutive registers if any registers value is different.
 * @param offset        starting register offset
 * @param value1        is written to first register
 * @param value2        is written to second register
 */
static inline void radeon_opt_set_context_reg2(struct si_context *sctx, unsigned offset,
                                               enum si_tracked_reg reg, unsigned value1,
                                               unsigned value2)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   if (((sctx->tracked_regs.reg_saved >> reg) & 0x3) != 0x3 ||
       sctx->tracked_regs.reg_value[reg] != value1 ||
       sctx->tracked_regs.reg_value[reg + 1] != value2) {
      radeon_set_context_reg_seq(cs, offset, 2);
      radeon_emit(cs, value1);
      radeon_emit(cs, value2);

      sctx->tracked_regs.reg_value[reg] = value1;
      sctx->tracked_regs.reg_value[reg + 1] = value2;
      sctx->tracked_regs.reg_saved |= 0x3ull << reg;
   }
}

/**
 * Set 3 consecutive registers if any registers value is different.
 */
static inline void radeon_opt_set_context_reg3(struct si_context *sctx, unsigned offset,
                                               enum si_tracked_reg reg, unsigned value1,
                                               unsigned value2, unsigned value3)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   if (((sctx->tracked_regs.reg_saved >> reg) & 0x7) != 0x7 ||
       sctx->tracked_regs.reg_value[reg] != value1 ||
       sctx->tracked_regs.reg_value[reg + 1] != value2 ||
       sctx->tracked_regs.reg_value[reg + 2] != value3) {
      radeon_set_context_reg_seq(cs, offset, 3);
      radeon_emit(cs, value1);
      radeon_emit(cs, value2);
      radeon_emit(cs, value3);

      sctx->tracked_regs.reg_value[reg] = value1;
      sctx->tracked_regs.reg_value[reg + 1] = value2;
      sctx->tracked_regs.reg_value[reg + 2] = value3;
      sctx->tracked_regs.reg_saved |= 0x7ull << reg;
   }
}

/**
 * Set 4 consecutive registers if any registers value is different.
 */
static inline void radeon_opt_set_context_reg4(struct si_context *sctx, unsigned offset,
                                               enum si_tracked_reg reg, unsigned value1,
                                               unsigned value2, unsigned value3, unsigned value4)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   if (((sctx->tracked_regs.reg_saved >> reg) & 0xf) != 0xf ||
       sctx->tracked_regs.reg_value[reg] != value1 ||
       sctx->tracked_regs.reg_value[reg + 1] != value2 ||
       sctx->tracked_regs.reg_value[reg + 2] != value3 ||
       sctx->tracked_regs.reg_value[reg + 3] != value4) {
      radeon_set_context_reg_seq(cs, offset, 4);
      radeon_emit(cs, value1);
      radeon_emit(cs, value2);
      radeon_emit(cs, value3);
      radeon_emit(cs, value4);

      sctx->tracked_regs.reg_value[reg] = value1;
      sctx->tracked_regs.reg_value[reg + 1] = value2;
      sctx->tracked_regs.reg_value[reg + 2] = value3;
      sctx->tracked_regs.reg_value[reg + 3] = value4;
      sctx->tracked_regs.reg_saved |= 0xfull << reg;
   }
}

/**
 * Set consecutive registers if any registers value is different.
 */
static inline void radeon_opt_set_context_regn(struct si_context *sctx, unsigned offset,
                                               unsigned *value, unsigned *saved_val, unsigned num)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   for (unsigned i = 0; i < num; i++) {
      if (saved_val[i] != value[i]) {
         radeon_set_context_reg_seq(cs, offset, num);
         for (unsigned j = 0; j < num; j++)
            radeon_emit(cs, value[j]);

         memcpy(saved_val, value, sizeof(uint32_t) * num);
         break;
      }
   }
}

#endif
