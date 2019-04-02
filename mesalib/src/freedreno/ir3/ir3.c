/*
 * Copyright (c) 2012 Rob Clark <robdclark@gmail.com>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "ir3.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <errno.h>

#include "util/bitscan.h"
#include "util/ralloc.h"
#include "util/u_math.h"

#include "instr-a3xx.h"
#include "ir3_compiler.h"

/* simple allocator to carve allocations out of an up-front allocated heap,
 * so that we can free everything easily in one shot.
 */
void * ir3_alloc(struct ir3 *shader, int sz)
{
	return rzalloc_size(shader, sz); /* TODO: don't use rzalloc */
}

struct ir3 * ir3_create(struct ir3_compiler *compiler,
		gl_shader_stage type, unsigned nin, unsigned nout)
{
	struct ir3 *shader = rzalloc(compiler, struct ir3);

	shader->compiler = compiler;
	shader->type = type;
	shader->ninputs = nin;
	shader->inputs = ir3_alloc(shader, sizeof(shader->inputs[0]) * nin);

	shader->noutputs = nout;
	shader->outputs = ir3_alloc(shader, sizeof(shader->outputs[0]) * nout);

	list_inithead(&shader->block_list);
	list_inithead(&shader->array_list);

	return shader;
}

void ir3_destroy(struct ir3 *shader)
{
	ralloc_free(shader);
}

#define iassert(cond) do { \
	if (!(cond)) { \
		debug_assert(cond); \
		return -1; \
	} } while (0)

#define iassert_type(reg, full) do { \
	if ((full)) { \
		iassert(!((reg)->flags & IR3_REG_HALF)); \
	} else { \
		iassert((reg)->flags & IR3_REG_HALF); \
	} } while (0);

static uint32_t reg(struct ir3_register *reg, struct ir3_info *info,
		uint32_t repeat, uint32_t valid_flags)
{
	reg_t val = { .dummy32 = 0 };

	if (reg->flags & ~valid_flags) {
		debug_printf("INVALID FLAGS: %x vs %x\n",
				reg->flags, valid_flags);
	}

	if (!(reg->flags & IR3_REG_R))
		repeat = 0;

	if (reg->flags & IR3_REG_IMMED) {
		val.iim_val = reg->iim_val;
	} else {
		unsigned components;
		int16_t max;

		if (reg->flags & IR3_REG_RELATIV) {
			components = reg->size;
			val.idummy10 = reg->array.offset;
			max = (reg->array.offset + repeat + components - 1) >> 2;
		} else {
			components = util_last_bit(reg->wrmask);
			val.comp = reg->num & 0x3;
			val.num  = reg->num >> 2;
			max = (reg->num + repeat + components - 1) >> 2;
		}

		if (reg->flags & IR3_REG_CONST) {
			info->max_const = MAX2(info->max_const, max);
		} else if (val.num == 63) {
			/* ignore writes to dummy register r63.x */
		} else if (max < 48) {
			if (reg->flags & IR3_REG_HALF) {
				if (info->gpu_id >= 600) {
					/* starting w/ a6xx, half regs conflict with full regs: */
					info->max_reg = MAX2(info->max_reg, (max+1)/2);
				} else {
					info->max_half_reg = MAX2(info->max_half_reg, max);
				}
			} else {
				info->max_reg = MAX2(info->max_reg, max);
			}
		}
	}

	return val.dummy32;
}

static int emit_cat0(struct ir3_instruction *instr, void *ptr,
		struct ir3_info *info)
{
	instr_cat0_t *cat0 = ptr;

	if (info->gpu_id >= 500) {
		cat0->a5xx.immed = instr->cat0.immed;
	} else if (info->gpu_id >= 400) {
		cat0->a4xx.immed = instr->cat0.immed;
	} else {
		cat0->a3xx.immed = instr->cat0.immed;
	}
	cat0->repeat   = instr->repeat;
	cat0->ss       = !!(instr->flags & IR3_INSTR_SS);
	cat0->inv      = instr->cat0.inv;
	cat0->comp     = instr->cat0.comp;
	cat0->opc      = instr->opc;
	cat0->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat0->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat0->opc_cat  = 0;

	return 0;
}

static int emit_cat1(struct ir3_instruction *instr, void *ptr,
		struct ir3_info *info)
{
	struct ir3_register *dst = instr->regs[0];
	struct ir3_register *src = instr->regs[1];
	instr_cat1_t *cat1 = ptr;

	iassert(instr->regs_count == 2);
	iassert_type(dst, type_size(instr->cat1.dst_type) == 32);
	if (!(src->flags & IR3_REG_IMMED))
		iassert_type(src, type_size(instr->cat1.src_type) == 32);

	if (src->flags & IR3_REG_IMMED) {
		cat1->iim_val = src->iim_val;
		cat1->src_im  = 1;
	} else if (src->flags & IR3_REG_RELATIV) {
		cat1->off       = reg(src, info, instr->repeat,
				IR3_REG_R | IR3_REG_CONST | IR3_REG_HALF | IR3_REG_RELATIV);
		cat1->src_rel   = 1;
		cat1->src_rel_c = !!(src->flags & IR3_REG_CONST);
	} else {
		cat1->src  = reg(src, info, instr->repeat,
				IR3_REG_R | IR3_REG_CONST | IR3_REG_HALF);
		cat1->src_c     = !!(src->flags & IR3_REG_CONST);
	}

	cat1->dst      = reg(dst, info, instr->repeat,
			IR3_REG_RELATIV | IR3_REG_EVEN |
			IR3_REG_R | IR3_REG_POS_INF | IR3_REG_HALF);
	cat1->repeat   = instr->repeat;
	cat1->src_r    = !!(src->flags & IR3_REG_R);
	cat1->ss       = !!(instr->flags & IR3_INSTR_SS);
	cat1->ul       = !!(instr->flags & IR3_INSTR_UL);
	cat1->dst_type = instr->cat1.dst_type;
	cat1->dst_rel  = !!(dst->flags & IR3_REG_RELATIV);
	cat1->src_type = instr->cat1.src_type;
	cat1->even     = !!(dst->flags & IR3_REG_EVEN);
	cat1->pos_inf  = !!(dst->flags & IR3_REG_POS_INF);
	cat1->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat1->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat1->opc_cat  = 1;

	return 0;
}

static int emit_cat2(struct ir3_instruction *instr, void *ptr,
		struct ir3_info *info)
{
	struct ir3_register *dst = instr->regs[0];
	struct ir3_register *src1 = instr->regs[1];
	struct ir3_register *src2 = instr->regs[2];
	instr_cat2_t *cat2 = ptr;
	unsigned absneg = ir3_cat2_absneg(instr->opc);

	iassert((instr->regs_count == 2) || (instr->regs_count == 3));

	if (instr->nop) {
		iassert(!instr->repeat);
		iassert(instr->nop <= 3);

		cat2->src1_r = instr->nop & 0x1;
		cat2->src2_r = (instr->nop >> 1) & 0x1;
	} else {
		cat2->src1_r = !!(src1->flags & IR3_REG_R);
		if (src2)
			cat2->src2_r = !!(src2->flags & IR3_REG_R);
	}

	if (src1->flags & IR3_REG_RELATIV) {
		iassert(src1->array.offset < (1 << 10));
		cat2->rel1.src1      = reg(src1, info, instr->repeat,
				IR3_REG_RELATIV | IR3_REG_CONST | IR3_REG_R |
				IR3_REG_HALF | absneg);
		cat2->rel1.src1_c    = !!(src1->flags & IR3_REG_CONST);
		cat2->rel1.src1_rel  = 1;
	} else if (src1->flags & IR3_REG_CONST) {
		iassert(src1->num < (1 << 12));
		cat2->c1.src1   = reg(src1, info, instr->repeat,
				IR3_REG_CONST | IR3_REG_R | IR3_REG_HALF);
		cat2->c1.src1_c = 1;
	} else {
		iassert(src1->num < (1 << 11));
		cat2->src1 = reg(src1, info, instr->repeat,
				IR3_REG_IMMED | IR3_REG_R | IR3_REG_HALF |
				absneg);
	}
	cat2->src1_im  = !!(src1->flags & IR3_REG_IMMED);
	cat2->src1_neg = !!(src1->flags & (IR3_REG_FNEG | IR3_REG_SNEG | IR3_REG_BNOT));
	cat2->src1_abs = !!(src1->flags & (IR3_REG_FABS | IR3_REG_SABS));

	if (src2) {
		iassert((src2->flags & IR3_REG_IMMED) ||
				!((src1->flags ^ src2->flags) & IR3_REG_HALF));

		if (src2->flags & IR3_REG_RELATIV) {
			iassert(src2->array.offset < (1 << 10));
			cat2->rel2.src2      = reg(src2, info, instr->repeat,
					IR3_REG_RELATIV | IR3_REG_CONST | IR3_REG_R |
					IR3_REG_HALF | absneg);
			cat2->rel2.src2_c    = !!(src2->flags & IR3_REG_CONST);
			cat2->rel2.src2_rel  = 1;
		} else if (src2->flags & IR3_REG_CONST) {
			iassert(src2->num < (1 << 12));
			cat2->c2.src2   = reg(src2, info, instr->repeat,
					IR3_REG_CONST | IR3_REG_R | IR3_REG_HALF);
			cat2->c2.src2_c = 1;
		} else {
			iassert(src2->num < (1 << 11));
			cat2->src2 = reg(src2, info, instr->repeat,
					IR3_REG_IMMED | IR3_REG_R | IR3_REG_HALF |
					absneg);
		}

		cat2->src2_im  = !!(src2->flags & IR3_REG_IMMED);
		cat2->src2_neg = !!(src2->flags & (IR3_REG_FNEG | IR3_REG_SNEG | IR3_REG_BNOT));
		cat2->src2_abs = !!(src2->flags & (IR3_REG_FABS | IR3_REG_SABS));
	}

	cat2->dst      = reg(dst, info, instr->repeat,
			IR3_REG_R | IR3_REG_EI | IR3_REG_HALF);
	cat2->repeat   = instr->repeat;
	cat2->sat      = !!(instr->flags & IR3_INSTR_SAT);
	cat2->ss       = !!(instr->flags & IR3_INSTR_SS);
	cat2->ul       = !!(instr->flags & IR3_INSTR_UL);
	cat2->dst_half = !!((src1->flags ^ dst->flags) & IR3_REG_HALF);
	cat2->ei       = !!(dst->flags & IR3_REG_EI);
	cat2->cond     = instr->cat2.condition;
	cat2->full     = ! (src1->flags & IR3_REG_HALF);
	cat2->opc      = instr->opc;
	cat2->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat2->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat2->opc_cat  = 2;

	return 0;
}

static int emit_cat3(struct ir3_instruction *instr, void *ptr,
		struct ir3_info *info)
{
	struct ir3_register *dst = instr->regs[0];
	struct ir3_register *src1 = instr->regs[1];
	struct ir3_register *src2 = instr->regs[2];
	struct ir3_register *src3 = instr->regs[3];
	unsigned absneg = ir3_cat3_absneg(instr->opc);
	instr_cat3_t *cat3 = ptr;
	uint32_t src_flags = 0;

	switch (instr->opc) {
	case OPC_MAD_F16:
	case OPC_MAD_U16:
	case OPC_MAD_S16:
	case OPC_SEL_B16:
	case OPC_SEL_S16:
	case OPC_SEL_F16:
	case OPC_SAD_S16:
	case OPC_SAD_S32:  // really??
		src_flags |= IR3_REG_HALF;
		break;
	default:
		break;
	}

	iassert(instr->regs_count == 4);
	iassert(!((src1->flags ^ src_flags) & IR3_REG_HALF));
	iassert(!((src2->flags ^ src_flags) & IR3_REG_HALF));
	iassert(!((src3->flags ^ src_flags) & IR3_REG_HALF));

	if (instr->nop) {
		iassert(!instr->repeat);
		iassert(instr->nop <= 3);

		cat3->src1_r = instr->nop & 0x1;
		cat3->src2_r = (instr->nop >> 1) & 0x1;
	} else {
		cat3->src1_r = !!(src1->flags & IR3_REG_R);
		cat3->src2_r = !!(src2->flags & IR3_REG_R);
	}

	if (src1->flags & IR3_REG_RELATIV) {
		iassert(src1->array.offset < (1 << 10));
		cat3->rel1.src1      = reg(src1, info, instr->repeat,
				IR3_REG_RELATIV | IR3_REG_CONST | IR3_REG_R |
				IR3_REG_HALF | absneg);
		cat3->rel1.src1_c    = !!(src1->flags & IR3_REG_CONST);
		cat3->rel1.src1_rel  = 1;
	} else if (src1->flags & IR3_REG_CONST) {
		iassert(src1->num < (1 << 12));
		cat3->c1.src1   = reg(src1, info, instr->repeat,
				IR3_REG_CONST | IR3_REG_R | IR3_REG_HALF);
		cat3->c1.src1_c = 1;
	} else {
		iassert(src1->num < (1 << 11));
		cat3->src1 = reg(src1, info, instr->repeat,
				IR3_REG_R | IR3_REG_HALF | absneg);
	}

	cat3->src1_neg = !!(src1->flags & (IR3_REG_FNEG | IR3_REG_SNEG | IR3_REG_BNOT));

	cat3->src2     = reg(src2, info, instr->repeat,
			IR3_REG_CONST | IR3_REG_R | IR3_REG_HALF | absneg);
	cat3->src2_c   = !!(src2->flags & IR3_REG_CONST);
	cat3->src2_neg = !!(src2->flags & (IR3_REG_FNEG | IR3_REG_SNEG | IR3_REG_BNOT));

	if (src3->flags & IR3_REG_RELATIV) {
		iassert(src3->array.offset < (1 << 10));
		cat3->rel2.src3      = reg(src3, info, instr->repeat,
				IR3_REG_RELATIV | IR3_REG_CONST | IR3_REG_R |
				IR3_REG_HALF | absneg);
		cat3->rel2.src3_c    = !!(src3->flags & IR3_REG_CONST);
		cat3->rel2.src3_rel  = 1;
	} else if (src3->flags & IR3_REG_CONST) {
		iassert(src3->num < (1 << 12));
		cat3->c2.src3   = reg(src3, info, instr->repeat,
				IR3_REG_CONST | IR3_REG_R | IR3_REG_HALF);
		cat3->c2.src3_c = 1;
	} else {
		iassert(src3->num < (1 << 11));
		cat3->src3 = reg(src3, info, instr->repeat,
				IR3_REG_R | IR3_REG_HALF | absneg);
	}

	cat3->src3_neg = !!(src3->flags & (IR3_REG_FNEG | IR3_REG_SNEG | IR3_REG_BNOT));
	cat3->src3_r   = !!(src3->flags & IR3_REG_R);

	cat3->dst      = reg(dst, info, instr->repeat, IR3_REG_R | IR3_REG_HALF);
	cat3->repeat   = instr->repeat;
	cat3->sat      = !!(instr->flags & IR3_INSTR_SAT);
	cat3->ss       = !!(instr->flags & IR3_INSTR_SS);
	cat3->ul       = !!(instr->flags & IR3_INSTR_UL);
	cat3->dst_half = !!((src_flags ^ dst->flags) & IR3_REG_HALF);
	cat3->opc      = instr->opc;
	cat3->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat3->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat3->opc_cat  = 3;

	return 0;
}

static int emit_cat4(struct ir3_instruction *instr, void *ptr,
		struct ir3_info *info)
{
	struct ir3_register *dst = instr->regs[0];
	struct ir3_register *src = instr->regs[1];
	instr_cat4_t *cat4 = ptr;

	iassert(instr->regs_count == 2);

	if (src->flags & IR3_REG_RELATIV) {
		iassert(src->array.offset < (1 << 10));
		cat4->rel.src      = reg(src, info, instr->repeat,
				IR3_REG_RELATIV | IR3_REG_CONST | IR3_REG_FNEG |
				IR3_REG_FABS | IR3_REG_R | IR3_REG_HALF);
		cat4->rel.src_c    = !!(src->flags & IR3_REG_CONST);
		cat4->rel.src_rel  = 1;
	} else if (src->flags & IR3_REG_CONST) {
		iassert(src->num < (1 << 12));
		cat4->c.src   = reg(src, info, instr->repeat,
				IR3_REG_CONST | IR3_REG_FNEG | IR3_REG_FABS |
				IR3_REG_R | IR3_REG_HALF);
		cat4->c.src_c = 1;
	} else {
		iassert(src->num < (1 << 11));
		cat4->src = reg(src, info, instr->repeat,
				IR3_REG_IMMED | IR3_REG_FNEG | IR3_REG_FABS |
				IR3_REG_R | IR3_REG_HALF);
	}

	cat4->src_im   = !!(src->flags & IR3_REG_IMMED);
	cat4->src_neg  = !!(src->flags & IR3_REG_FNEG);
	cat4->src_abs  = !!(src->flags & IR3_REG_FABS);
	cat4->src_r    = !!(src->flags & IR3_REG_R);

	cat4->dst      = reg(dst, info, instr->repeat, IR3_REG_R | IR3_REG_HALF);
	cat4->repeat   = instr->repeat;
	cat4->sat      = !!(instr->flags & IR3_INSTR_SAT);
	cat4->ss       = !!(instr->flags & IR3_INSTR_SS);
	cat4->ul       = !!(instr->flags & IR3_INSTR_UL);
	cat4->dst_half = !!((src->flags ^ dst->flags) & IR3_REG_HALF);
	cat4->full     = ! (src->flags & IR3_REG_HALF);
	cat4->opc      = instr->opc;
	cat4->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat4->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat4->opc_cat  = 4;

	return 0;
}

static int emit_cat5(struct ir3_instruction *instr, void *ptr,
		struct ir3_info *info)
{
	struct ir3_register *dst = instr->regs[0];
	/* To simplify things when there could be zero, one, or two args other
	 * than tex/sampler idx, we use the first src reg in the ir to hold
	 * samp_tex hvec2:
	 */
	struct ir3_register *src1;
	struct ir3_register *src2;
	instr_cat5_t *cat5 = ptr;

	iassert((instr->regs_count == 2) ||
			(instr->regs_count == 3) || (instr->regs_count == 4));

	switch (instr->opc) {
	case OPC_DSX:
	case OPC_DSXPP_1:
	case OPC_DSY:
	case OPC_DSYPP_1:
		iassert((instr->flags & IR3_INSTR_S2EN) == 0);
		src1 = instr->regs[1];
		src2 = instr->regs_count > 2 ? instr->regs[2] : NULL;
		break;
	default:
		src1 = instr->regs[2];
		src2 = instr->regs_count > 3 ? instr->regs[3] : NULL;
		break;
	}

	iassert_type(dst, type_size(instr->cat5.type) == 32)

	assume(src1 || !src2);

	if (src1) {
		cat5->full = ! (src1->flags & IR3_REG_HALF);
		cat5->src1 = reg(src1, info, instr->repeat, IR3_REG_HALF);
	}

	if (instr->flags & IR3_INSTR_S2EN) {
		struct ir3_register *samp_tex = instr->regs[1];
		if (src2) {
			iassert(!((src1->flags ^ src2->flags) & IR3_REG_HALF));
			cat5->s2en.src2 = reg(src2, info, instr->repeat, IR3_REG_HALF);
		}
		iassert(samp_tex->flags & IR3_REG_HALF);
		cat5->s2en.src3 = reg(samp_tex, info, instr->repeat, IR3_REG_HALF);
		iassert(!(instr->cat5.samp | instr->cat5.tex));
	} else {
		if (src2) {
			iassert(!((src1->flags ^ src2->flags) & IR3_REG_HALF));
			cat5->norm.src2 = reg(src2, info, instr->repeat, IR3_REG_HALF);
		}
		cat5->norm.samp = instr->cat5.samp;
		cat5->norm.tex  = instr->cat5.tex;
	}

	cat5->dst      = reg(dst, info, instr->repeat, IR3_REG_R | IR3_REG_HALF);
	cat5->wrmask   = dst->wrmask;
	cat5->type     = instr->cat5.type;
	cat5->is_3d    = !!(instr->flags & IR3_INSTR_3D);
	cat5->is_a     = !!(instr->flags & IR3_INSTR_A);
	cat5->is_s     = !!(instr->flags & IR3_INSTR_S);
	cat5->is_s2en  = !!(instr->flags & IR3_INSTR_S2EN);
	cat5->is_o     = !!(instr->flags & IR3_INSTR_O);
	cat5->is_p     = !!(instr->flags & IR3_INSTR_P);
	cat5->opc      = instr->opc;
	cat5->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat5->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat5->opc_cat  = 5;

	return 0;
}

static int emit_cat6_a6xx(struct ir3_instruction *instr, void *ptr,
		struct ir3_info *info)
{
	struct ir3_register *src1, *src2;
	instr_cat6_a6xx_t *cat6 = ptr;
	bool has_dest = (instr->opc == OPC_LDIB);

	/* first reg should be SSBO binding point: */
	iassert(instr->regs[1]->flags & IR3_REG_IMMED);

	src1 = instr->regs[2];

	if (has_dest) {
		/* the src2 field in the instruction is actually the destination
		 * register for load instructions:
		 */
		src2 = instr->regs[0];
	} else {
		src2 = instr->regs[3];
	}

	cat6->type      = instr->cat6.type;
	cat6->d         = instr->cat6.d - 1;
	cat6->typed     = instr->cat6.typed;
	cat6->type_size = instr->cat6.iim_val - 1;
	cat6->opc       = instr->opc;
	cat6->jmp_tgt   = !!(instr->flags & IR3_INSTR_JP);
	cat6->sync      = !!(instr->flags & IR3_INSTR_SY);
	cat6->opc_cat   = 6;

	cat6->src1 = reg(src1, info, instr->repeat, 0);
	cat6->src2 = reg(src2, info, instr->repeat, 0);
	cat6->ssbo = instr->regs[1]->iim_val;

	switch (instr->opc) {
	case OPC_ATOMIC_ADD:
	case OPC_ATOMIC_SUB:
	case OPC_ATOMIC_XCHG:
	case OPC_ATOMIC_INC:
	case OPC_ATOMIC_DEC:
	case OPC_ATOMIC_CMPXCHG:
	case OPC_ATOMIC_MIN:
	case OPC_ATOMIC_MAX:
	case OPC_ATOMIC_AND:
	case OPC_ATOMIC_OR:
	case OPC_ATOMIC_XOR:
		cat6->pad1 = 0x1;
		cat6->pad2 = 0xc;
		cat6->pad3 = 0x0;
		cat6->pad4 = 0x3;
		break;
	case OPC_STIB:
		cat6->pad1 = 0x0;
		cat6->pad2 = 0xc;
		cat6->pad3 = 0x0;
		cat6->pad4 = 0x2;
		break;
	case OPC_LDIB:
		cat6->pad1 = 0x1;
		cat6->pad2 = 0xc;
		cat6->pad3 = 0x0;
		cat6->pad4 = 0x2;
		break;
	case OPC_LDC:
		cat6->pad1 = 0x0;
		cat6->pad2 = 0x8;
		cat6->pad3 = 0x0;
		cat6->pad4 = 0x2;
		break;
	default:
		iassert(0);
	}

	return 0;
}

static int emit_cat6(struct ir3_instruction *instr, void *ptr,
		struct ir3_info *info)
{
	struct ir3_register *dst, *src1, *src2;
	instr_cat6_t *cat6 = ptr;

	/* In a6xx we start using a new instruction encoding for some of
	 * these instructions:
	 */
	if (info->gpu_id >= 600) {
		switch (instr->opc) {
		case OPC_ATOMIC_ADD:
		case OPC_ATOMIC_SUB:
		case OPC_ATOMIC_XCHG:
		case OPC_ATOMIC_INC:
		case OPC_ATOMIC_DEC:
		case OPC_ATOMIC_CMPXCHG:
		case OPC_ATOMIC_MIN:
		case OPC_ATOMIC_MAX:
		case OPC_ATOMIC_AND:
		case OPC_ATOMIC_OR:
		case OPC_ATOMIC_XOR:
			/* The shared variants of these still use the old encoding: */
			if (!(instr->flags & IR3_INSTR_G))
				break;
			/* fallthrough */
		case OPC_STIB:
		case OPC_LDIB:
		case OPC_LDC:
			return emit_cat6_a6xx(instr, ptr, info);
		default:
			break;
		}
	}

	bool type_full = type_size(instr->cat6.type) == 32;

	cat6->type     = instr->cat6.type;
	cat6->opc      = instr->opc;
	cat6->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat6->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat6->g        = !!(instr->flags & IR3_INSTR_G);
	cat6->opc_cat  = 6;

	switch (instr->opc) {
	case OPC_RESINFO:
	case OPC_RESFMT:
		iassert_type(instr->regs[0], type_full); /* dst */
		iassert_type(instr->regs[1], type_full); /* src1 */
		break;
	case OPC_L2G:
	case OPC_G2L:
		iassert_type(instr->regs[0], true);      /* dst */
		iassert_type(instr->regs[1], true);      /* src1 */
		break;
	case OPC_STG:
	case OPC_STL:
	case OPC_STP:
	case OPC_STLW:
	case OPC_STIB:
		/* no dst, so regs[0] is dummy */
		iassert_type(instr->regs[1], true);      /* dst */
		iassert_type(instr->regs[2], type_full); /* src1 */
		iassert_type(instr->regs[3], true);      /* src2 */
		break;
	default:
		iassert_type(instr->regs[0], type_full); /* dst */
		iassert_type(instr->regs[1], true);      /* src1 */
		if (instr->regs_count > 2)
			iassert_type(instr->regs[2], true);  /* src1 */
		break;
	}

	/* the "dst" for a store instruction is (from the perspective
	 * of data flow in the shader, ie. register use/def, etc) in
	 * fact a register that is read by the instruction, rather
	 * than written:
	 */
	if (is_store(instr)) {
		iassert(instr->regs_count >= 3);

		dst  = instr->regs[1];
		src1 = instr->regs[2];
		src2 = (instr->regs_count >= 4) ? instr->regs[3] : NULL;
	} else {
		iassert(instr->regs_count >= 2);

		dst  = instr->regs[0];
		src1 = instr->regs[1];
		src2 = (instr->regs_count >= 3) ? instr->regs[2] : NULL;
	}

	/* TODO we need a more comprehensive list about which instructions
	 * can be encoded which way.  Or possibly use IR3_INSTR_0 flag to
	 * indicate to use the src_off encoding even if offset is zero
	 * (but then what to do about dst_off?)
	 */
	if (is_atomic(instr->opc)) {
		instr_cat6ldgb_t *ldgb = ptr;

		/* maybe these two bits both determine the instruction encoding? */
		cat6->src_off = false;

		ldgb->d = instr->cat6.d - 1;
		ldgb->typed = instr->cat6.typed;
		ldgb->type_size = instr->cat6.iim_val - 1;

		ldgb->dst = reg(dst, info, instr->repeat, IR3_REG_R | IR3_REG_HALF);

		if (ldgb->g) {
			struct ir3_register *src3 = instr->regs[3];
			struct ir3_register *src4 = instr->regs[4];

			/* first src is src_ssbo: */
			iassert(src1->flags & IR3_REG_IMMED);
			ldgb->src_ssbo = src1->uim_val;

			ldgb->src1 = reg(src2, info, instr->repeat, IR3_REG_IMMED);
			ldgb->src1_im = !!(src2->flags & IR3_REG_IMMED);
			ldgb->src2 = reg(src3, info, instr->repeat, IR3_REG_IMMED);
			ldgb->src2_im = !!(src3->flags & IR3_REG_IMMED);

			ldgb->src3 = reg(src4, info, instr->repeat, 0);
			ldgb->pad0 = 0x1;
			ldgb->pad3 = 0x1;
		} else {
			ldgb->src1 = reg(src1, info, instr->repeat, IR3_REG_IMMED);
			ldgb->src1_im = !!(src1->flags & IR3_REG_IMMED);
			ldgb->src2 = reg(src2, info, instr->repeat, IR3_REG_IMMED);
			ldgb->src2_im = !!(src2->flags & IR3_REG_IMMED);
			ldgb->pad0 = 0x1;
			ldgb->pad3 = 0x0;
		}

		return 0;
	} else if (instr->opc == OPC_LDGB) {
		struct ir3_register *src3 = instr->regs[3];
		instr_cat6ldgb_t *ldgb = ptr;

		/* maybe these two bits both determine the instruction encoding? */
		cat6->src_off = false;

		ldgb->d = instr->cat6.d - 1;
		ldgb->typed = instr->cat6.typed;
		ldgb->type_size = instr->cat6.iim_val - 1;

		ldgb->dst = reg(dst, info, instr->repeat, IR3_REG_R | IR3_REG_HALF);

		/* first src is src_ssbo: */
		iassert(src1->flags & IR3_REG_IMMED);
		ldgb->src_ssbo = src1->uim_val;

		/* then next two are src1/src2: */
		ldgb->src1 = reg(src2, info, instr->repeat, IR3_REG_IMMED);
		ldgb->src1_im = !!(src2->flags & IR3_REG_IMMED);
		ldgb->src2 = reg(src3, info, instr->repeat, IR3_REG_IMMED);
		ldgb->src2_im = !!(src3->flags & IR3_REG_IMMED);

		ldgb->pad0 = 0x0;
		ldgb->pad3 = 0x1;

		return 0;
	} else if (instr->opc == OPC_RESINFO) {
		instr_cat6ldgb_t *ldgb = ptr;

		ldgb->d = instr->cat6.d - 1;

		ldgb->dst = reg(dst, info, instr->repeat, IR3_REG_R | IR3_REG_HALF);

		/* first src is src_ssbo: */
		iassert(src1->flags & IR3_REG_IMMED);
		ldgb->src_ssbo = src1->uim_val;

		return 0;
	} else if ((instr->opc == OPC_STGB) || (instr->opc == OPC_STIB)) {
		struct ir3_register *src3 = instr->regs[4];
		instr_cat6stgb_t *stgb = ptr;

		/* maybe these two bits both determine the instruction encoding? */
		cat6->src_off = true;
		stgb->pad3 = 0x2;

		stgb->d = instr->cat6.d - 1;
		stgb->typed = instr->cat6.typed;
		stgb->type_size = instr->cat6.iim_val - 1;

		/* first src is dst_ssbo: */
		iassert(dst->flags & IR3_REG_IMMED);
		stgb->dst_ssbo = dst->uim_val;

		/* then src1/src2/src3: */
		stgb->src1 = reg(src1, info, instr->repeat, 0);
		stgb->src2 = reg(src2, info, instr->repeat, IR3_REG_IMMED);
		stgb->src2_im = !!(src2->flags & IR3_REG_IMMED);
		stgb->src3 = reg(src3, info, instr->repeat, IR3_REG_IMMED);
		stgb->src3_im = !!(src3->flags & IR3_REG_IMMED);

		return 0;
	} else if (instr->cat6.src_offset || (instr->opc == OPC_LDG) ||
			(instr->opc == OPC_LDL)) {
		instr_cat6a_t *cat6a = ptr;

		cat6->src_off = true;

		cat6a->src1 = reg(src1, info, instr->repeat, IR3_REG_IMMED);
		cat6a->src1_im = !!(src1->flags & IR3_REG_IMMED);
		if (src2) {
			cat6a->src2 = reg(src2, info, instr->repeat, IR3_REG_IMMED);
			cat6a->src2_im = !!(src2->flags & IR3_REG_IMMED);
		}
		cat6a->off = instr->cat6.src_offset;
	} else {
		instr_cat6b_t *cat6b = ptr;

		cat6->src_off = false;

		cat6b->src1 = reg(src1, info, instr->repeat, IR3_REG_IMMED | IR3_REG_HALF);
		cat6b->src1_im = !!(src1->flags & IR3_REG_IMMED);
		if (src2) {
			cat6b->src2 = reg(src2, info, instr->repeat, IR3_REG_IMMED);
			cat6b->src2_im = !!(src2->flags & IR3_REG_IMMED);
		}
	}

	if (instr->cat6.dst_offset || (instr->opc == OPC_STG) ||
			(instr->opc == OPC_STL)) {
		instr_cat6c_t *cat6c = ptr;
		cat6->dst_off = true;
		cat6c->dst = reg(dst, info, instr->repeat, IR3_REG_R | IR3_REG_HALF);
		cat6c->off = instr->cat6.dst_offset;
	} else {
		instr_cat6d_t *cat6d = ptr;
		cat6->dst_off = false;
		cat6d->dst = reg(dst, info, instr->repeat, IR3_REG_R | IR3_REG_HALF);
	}

	return 0;
}

static int emit_cat7(struct ir3_instruction *instr, void *ptr,
		struct ir3_info *info)
{
	instr_cat7_t *cat7 = ptr;

	cat7->ss      = !!(instr->flags & IR3_INSTR_SS);
	cat7->w       = instr->cat7.w;
	cat7->r       = instr->cat7.r;
	cat7->l       = instr->cat7.l;
	cat7->g       = instr->cat7.g;
	cat7->opc     = instr->opc;
	cat7->jmp_tgt = !!(instr->flags & IR3_INSTR_JP);
	cat7->sync    = !!(instr->flags & IR3_INSTR_SY);
	cat7->opc_cat = 7;

	return 0;
}

static int (*emit[])(struct ir3_instruction *instr, void *ptr,
		struct ir3_info *info) = {
	emit_cat0, emit_cat1, emit_cat2, emit_cat3, emit_cat4, emit_cat5, emit_cat6,
	emit_cat7,
};

void * ir3_assemble(struct ir3 *shader, struct ir3_info *info,
		uint32_t gpu_id)
{
	uint32_t *ptr, *dwords;

	info->gpu_id        = gpu_id;
	info->max_reg       = -1;
	info->max_half_reg  = -1;
	info->max_const     = -1;
	info->instrs_count  = 0;
	info->sizedwords    = 0;
	info->ss = info->sy = 0;

	list_for_each_entry (struct ir3_block, block, &shader->block_list, node) {
		list_for_each_entry (struct ir3_instruction, instr, &block->instr_list, node) {
			info->sizedwords += 2;
		}
	}

	/* need an integer number of instruction "groups" (sets of 16
	 * instructions on a4xx or sets of 4 instructions on a3xx),
	 * so pad out w/ NOPs if needed: (NOTE each instruction is 64bits)
	 */
	if (gpu_id >= 400) {
		info->sizedwords = align(info->sizedwords, 16 * 2);
	} else {
		info->sizedwords = align(info->sizedwords, 4 * 2);
	}

	ptr = dwords = calloc(4, info->sizedwords);

	list_for_each_entry (struct ir3_block, block, &shader->block_list, node) {
		list_for_each_entry (struct ir3_instruction, instr, &block->instr_list, node) {
			int ret = emit[opc_cat(instr->opc)](instr, dwords, info);
			if (ret)
				goto fail;
			info->instrs_count += 1 + instr->repeat + instr->nop;
			dwords += 2;

			if (instr->flags & IR3_INSTR_SS)
				info->ss++;

			if (instr->flags & IR3_INSTR_SY)
				info->sy++;
		}
	}

	return ptr;

fail:
	free(ptr);
	return NULL;
}

static struct ir3_register * reg_create(struct ir3 *shader,
		int num, int flags)
{
	struct ir3_register *reg =
			ir3_alloc(shader, sizeof(struct ir3_register));
	reg->wrmask = 1;
	reg->flags = flags;
	reg->num = num;
	if (shader->compiler->gpu_id >= 600)
		reg->merged = true;
	return reg;
}

static void insert_instr(struct ir3_block *block,
		struct ir3_instruction *instr)
{
	struct ir3 *shader = block->shader;
#ifdef DEBUG
	instr->serialno = ++shader->instr_count;
#endif
	list_addtail(&instr->node, &block->instr_list);

	if (is_input(instr))
		array_insert(shader, shader->baryfs, instr);
}

struct ir3_block * ir3_block_create(struct ir3 *shader)
{
	struct ir3_block *block = ir3_alloc(shader, sizeof(*block));
#ifdef DEBUG
	block->serialno = ++shader->block_count;
#endif
	block->shader = shader;
	list_inithead(&block->node);
	list_inithead(&block->instr_list);
	return block;
}

static struct ir3_instruction *instr_create(struct ir3_block *block, int nreg)
{
	struct ir3_instruction *instr;
	unsigned sz = sizeof(*instr) + (nreg * sizeof(instr->regs[0]));
	char *ptr = ir3_alloc(block->shader, sz);

	instr = (struct ir3_instruction *)ptr;
	ptr  += sizeof(*instr);
	instr->regs = (struct ir3_register **)ptr;

#ifdef DEBUG
	instr->regs_max = nreg;
#endif

	return instr;
}

struct ir3_instruction * ir3_instr_create2(struct ir3_block *block,
		opc_t opc, int nreg)
{
	struct ir3_instruction *instr = instr_create(block, nreg);
	instr->block = block;
	instr->opc = opc;
	insert_instr(block, instr);
	return instr;
}

struct ir3_instruction * ir3_instr_create(struct ir3_block *block, opc_t opc)
{
	/* NOTE: we could be slightly more clever, at least for non-meta,
	 * and choose # of regs based on category.
	 */
	return ir3_instr_create2(block, opc, 4);
}

struct ir3_instruction * ir3_instr_clone(struct ir3_instruction *instr)
{
	struct ir3_instruction *new_instr = instr_create(instr->block,
			instr->regs_count);
	struct ir3_register **regs;
	unsigned i;

	regs = new_instr->regs;
	*new_instr = *instr;
	new_instr->regs = regs;

	insert_instr(instr->block, new_instr);

	/* clone registers: */
	new_instr->regs_count = 0;
	for (i = 0; i < instr->regs_count; i++) {
		struct ir3_register *reg = instr->regs[i];
		struct ir3_register *new_reg =
				ir3_reg_create(new_instr, reg->num, reg->flags);
		*new_reg = *reg;
	}

	return new_instr;
}

/* Add a false dependency to instruction, to ensure it is scheduled first: */
void ir3_instr_add_dep(struct ir3_instruction *instr, struct ir3_instruction *dep)
{
	array_insert(instr, instr->deps, dep);
}

struct ir3_register * ir3_reg_create(struct ir3_instruction *instr,
		int num, int flags)
{
	struct ir3 *shader = instr->block->shader;
	struct ir3_register *reg = reg_create(shader, num, flags);
#ifdef DEBUG
	debug_assert(instr->regs_count < instr->regs_max);
#endif
	instr->regs[instr->regs_count++] = reg;
	return reg;
}

struct ir3_register * ir3_reg_clone(struct ir3 *shader,
		struct ir3_register *reg)
{
	struct ir3_register *new_reg = reg_create(shader, 0, 0);
	*new_reg = *reg;
	return new_reg;
}

void
ir3_instr_set_address(struct ir3_instruction *instr,
		struct ir3_instruction *addr)
{
	if (instr->address != addr) {
		struct ir3 *ir = instr->block->shader;
		instr->address = addr;
		array_insert(ir, ir->indirects, instr);
	}
}

void
ir3_block_clear_mark(struct ir3_block *block)
{
	list_for_each_entry (struct ir3_instruction, instr, &block->instr_list, node)
		instr->flags &= ~IR3_INSTR_MARK;
}

void
ir3_clear_mark(struct ir3 *ir)
{
	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		ir3_block_clear_mark(block);
	}
}

/* note: this will destroy instr->depth, don't do it until after sched! */
unsigned
ir3_count_instructions(struct ir3 *ir)
{
	unsigned cnt = 0;
	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		list_for_each_entry (struct ir3_instruction, instr, &block->instr_list, node) {
			instr->ip = cnt++;
		}
		block->start_ip = list_first_entry(&block->instr_list, struct ir3_instruction, node)->ip;
		block->end_ip = list_last_entry(&block->instr_list, struct ir3_instruction, node)->ip;
	}
	return cnt;
}

struct ir3_array *
ir3_lookup_array(struct ir3 *ir, unsigned id)
{
	list_for_each_entry (struct ir3_array, arr, &ir->array_list, node)
		if (arr->id == id)
			return arr;
	return NULL;
}
