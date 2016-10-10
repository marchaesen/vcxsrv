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
#include "r600d_common.h"

static inline unsigned radeon_check_space(struct radeon_winsys *ws,
                                      struct radeon_winsys_cs *cs,
                                      unsigned needed)
{
        if (cs->max_dw - cs->cdw < needed)
                ws->cs_grow(cs, needed);
        return cs->cdw + needed;
}

static inline void radeon_set_config_reg_seq(struct radeon_winsys_cs *cs, unsigned reg, unsigned num)
{
        assert(reg < R600_CONTEXT_REG_OFFSET);
        assert(cs->cdw + 2 + num <= cs->max_dw);
        radeon_emit(cs, PKT3(PKT3_SET_CONFIG_REG, num, 0));
        radeon_emit(cs, (reg - R600_CONFIG_REG_OFFSET) >> 2);
}

static inline void radeon_set_config_reg(struct radeon_winsys_cs *cs, unsigned reg, unsigned value)
{
        radeon_set_config_reg_seq(cs, reg, 1);
        radeon_emit(cs, value);
}

static inline void radeon_set_context_reg_seq(struct radeon_winsys_cs *cs, unsigned reg, unsigned num)
{
        assert(reg >= R600_CONTEXT_REG_OFFSET);
        assert(cs->cdw + 2 + num <= cs->max_dw);
        radeon_emit(cs, PKT3(PKT3_SET_CONTEXT_REG, num, 0));
        radeon_emit(cs, (reg - R600_CONTEXT_REG_OFFSET) >> 2);
}

static inline void radeon_set_context_reg(struct radeon_winsys_cs *cs, unsigned reg, unsigned value)
{
        radeon_set_context_reg_seq(cs, reg, 1);
        radeon_emit(cs, value);
}


static inline void radeon_set_context_reg_idx(struct radeon_winsys_cs *cs,
					      unsigned reg, unsigned idx,
					      unsigned value)
{
	assert(reg >= R600_CONTEXT_REG_OFFSET);
	assert(cs->cdw + 3 <= cs->max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_CONTEXT_REG, 1, 0));
	radeon_emit(cs, (reg - R600_CONTEXT_REG_OFFSET) >> 2 | (idx << 28));
	radeon_emit(cs, value);
}

static inline void radeon_set_sh_reg_seq(struct radeon_winsys_cs *cs, unsigned reg, unsigned num)
{
	assert(reg >= SI_SH_REG_OFFSET && reg < SI_SH_REG_END);
	assert(cs->cdw + 2 + num <= cs->max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_SH_REG, num, 0));
	radeon_emit(cs, (reg - SI_SH_REG_OFFSET) >> 2);
}

static inline void radeon_set_sh_reg(struct radeon_winsys_cs *cs, unsigned reg, unsigned value)
{
	radeon_set_sh_reg_seq(cs, reg, 1);
	radeon_emit(cs, value);
}

static inline void radeon_set_uconfig_reg_seq(struct radeon_winsys_cs *cs, unsigned reg, unsigned num)
{
	assert(reg >= CIK_UCONFIG_REG_OFFSET && reg < CIK_UCONFIG_REG_END);
	assert(cs->cdw + 2 + num <= cs->max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_UCONFIG_REG, num, 0));
	radeon_emit(cs, (reg - CIK_UCONFIG_REG_OFFSET) >> 2);
}

static inline void radeon_set_uconfig_reg(struct radeon_winsys_cs *cs, unsigned reg, unsigned value)
{
	radeon_set_uconfig_reg_seq(cs, reg, 1);
	radeon_emit(cs, value);
}

static inline void radeon_set_uconfig_reg_idx(struct radeon_winsys_cs *cs,
					      unsigned reg, unsigned idx,
					      unsigned value)
{
	assert(reg >= CIK_UCONFIG_REG_OFFSET && reg < CIK_UCONFIG_REG_END);
	assert(cs->cdw + 3 <= cs->max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_UCONFIG_REG, 1, 0));
	radeon_emit(cs, (reg - CIK_UCONFIG_REG_OFFSET) >> 2 | (idx << 28));
	radeon_emit(cs, value);
}

#endif /* RADV_CS_H */
