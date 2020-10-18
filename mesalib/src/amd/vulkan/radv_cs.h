/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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
 */

#ifndef RADV_CS_H
#define RADV_CS_H

#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "radv_private.h"
#include "sid.h"

static inline unsigned radeon_check_space(struct radeon_winsys *ws,
                                      struct radeon_cmdbuf *cs,
                                      unsigned needed)
{
        if (cs->max_dw - cs->cdw < needed)
                ws->cs_grow(cs, needed);
        return cs->cdw + needed;
}

static inline void radeon_set_config_reg_seq(struct radeon_cmdbuf *cs, unsigned reg, unsigned num)
{
        assert(reg >= SI_CONFIG_REG_OFFSET && reg < SI_CONFIG_REG_END);
        assert(cs->cdw + 2 + num <= cs->max_dw);
        assert(num);
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
        assert(reg >= SI_CONTEXT_REG_OFFSET && reg < SI_CONTEXT_REG_END);
        assert(cs->cdw + 2 + num <= cs->max_dw);
        assert(num);
        radeon_emit(cs, PKT3(PKT3_SET_CONTEXT_REG, num, 0));
        radeon_emit(cs, (reg - SI_CONTEXT_REG_OFFSET) >> 2);
}

static inline void radeon_set_context_reg(struct radeon_cmdbuf *cs, unsigned reg, unsigned value)
{
        radeon_set_context_reg_seq(cs, reg, 1);
        radeon_emit(cs, value);
}


static inline void radeon_set_context_reg_idx(struct radeon_cmdbuf *cs,
					      unsigned reg, unsigned idx,
					      unsigned value)
{
	assert(reg >= SI_CONTEXT_REG_OFFSET && reg < SI_CONTEXT_REG_END);
	assert(cs->cdw + 3 <= cs->max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_CONTEXT_REG, 1, 0));
	radeon_emit(cs, (reg - SI_CONTEXT_REG_OFFSET) >> 2 | (idx << 28));
	radeon_emit(cs, value);
}

static inline void radeon_set_context_reg_rmw(struct radeon_cmdbuf *cs,
					      unsigned reg, unsigned value,
					      unsigned mask)
{
	assert(reg >= SI_CONTEXT_REG_OFFSET && reg < SI_CONTEXT_REG_END);
	assert(cs->cdw + 4 <= cs->max_dw);
	radeon_emit(cs, PKT3(PKT3_CONTEXT_REG_RMW, 2, 0));
	radeon_emit(cs, (reg - SI_CONTEXT_REG_OFFSET) >> 2);
	radeon_emit(cs, mask);
	radeon_emit(cs, value);
}

static inline void radeon_set_sh_reg_seq(struct radeon_cmdbuf *cs, unsigned reg, unsigned num)
{
	assert(reg >= SI_SH_REG_OFFSET && reg < SI_SH_REG_END);
	assert(cs->cdw + 2 + num <= cs->max_dw);
	assert(num);
	radeon_emit(cs, PKT3(PKT3_SET_SH_REG, num, 0));
	radeon_emit(cs, (reg - SI_SH_REG_OFFSET) >> 2);
}

static inline void radeon_set_sh_reg(struct radeon_cmdbuf *cs, unsigned reg, unsigned value)
{
	radeon_set_sh_reg_seq(cs, reg, 1);
	radeon_emit(cs, value);
}

static inline void radeon_set_sh_reg_idx(const struct radv_physical_device *pdevice,
					 struct radeon_cmdbuf *cs,
					 unsigned reg, unsigned idx,
					 unsigned value)
{
	assert(reg >= SI_SH_REG_OFFSET && reg < SI_SH_REG_END);
	assert(cs->cdw + 3 <= cs->max_dw);
	assert(idx);

	unsigned opcode = PKT3_SET_SH_REG_INDEX;
	if (pdevice->rad_info.chip_class < GFX10)
		opcode = PKT3_SET_SH_REG;

	radeon_emit(cs, PKT3(opcode, 1, 0));
	radeon_emit(cs, (reg - SI_SH_REG_OFFSET) >> 2 | (idx << 28));
	radeon_emit(cs, value);
}

static inline void radeon_set_uconfig_reg_seq(struct radeon_cmdbuf *cs, unsigned reg, unsigned num)
{
	assert(reg >= CIK_UCONFIG_REG_OFFSET && reg < CIK_UCONFIG_REG_END);
	assert(cs->cdw + 2 + num <= cs->max_dw);
	assert(num);
	radeon_emit(cs, PKT3(PKT3_SET_UCONFIG_REG, num, 0));
	radeon_emit(cs, (reg - CIK_UCONFIG_REG_OFFSET) >> 2);
}

static inline void radeon_set_uconfig_reg_seq_perfctr(struct radeon_cmdbuf *cs,
						      unsigned reg, unsigned num)
{
	assert(reg >= CIK_UCONFIG_REG_OFFSET && reg < CIK_UCONFIG_REG_END);
	assert(cs->cdw + 2 + num <= cs->max_dw);
	assert(num);
	radeon_emit(cs, PKT3(PKT3_SET_UCONFIG_REG, num, 1));
	radeon_emit(cs, (reg - CIK_UCONFIG_REG_OFFSET) >> 2);
}

static inline void radeon_set_uconfig_reg(struct radeon_cmdbuf *cs, unsigned reg, unsigned value)
{
	radeon_set_uconfig_reg_seq(cs, reg, 1);
	radeon_emit(cs, value);
}

static inline void radeon_set_uconfig_reg_idx(const struct radv_physical_device *pdevice,
					      struct radeon_cmdbuf *cs,
					      unsigned reg, unsigned idx,
					      unsigned value)
{
	assert(reg >= CIK_UCONFIG_REG_OFFSET && reg < CIK_UCONFIG_REG_END);
	assert(cs->cdw + 3 <= cs->max_dw);
	assert(idx);

	unsigned opcode = PKT3_SET_UCONFIG_REG_INDEX;
	if (pdevice->rad_info.chip_class < GFX9 ||
	    (pdevice->rad_info.chip_class == GFX9 && pdevice->rad_info.me_fw_version < 26))
		opcode = PKT3_SET_UCONFIG_REG;

	radeon_emit(cs, PKT3(opcode, 1, 0));
	radeon_emit(cs, (reg - CIK_UCONFIG_REG_OFFSET) >> 2 | (idx << 28));
	radeon_emit(cs, value);
}

static inline void radeon_set_privileged_config_reg(struct radeon_cmdbuf *cs,
						    unsigned reg,
						    unsigned value)
{
	assert(reg < CIK_UCONFIG_REG_OFFSET);
	assert(cs->cdw + 6 <= cs->max_dw);

	radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
	radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_IMM) |
			COPY_DATA_DST_SEL(COPY_DATA_PERF));
	radeon_emit(cs, value);
	radeon_emit(cs, 0); /* unused */
	radeon_emit(cs, reg >> 2);
	radeon_emit(cs, 0); /* unused */
}

#endif /* RADV_CS_H */
