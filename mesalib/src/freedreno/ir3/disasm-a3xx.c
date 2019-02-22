/*
 * Copyright (c) 2013 Rob Clark <robdclark@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <util/u_debug.h>

#include "instr-a3xx.h"

/* bitmask of debug flags */
enum debug_t {
	PRINT_RAW      = 0x1,    /* dump raw hexdump */
	PRINT_VERBOSE  = 0x2,
};

static enum debug_t debug;

#define printf debug_printf

static const char *levels[] = {
		"",
		"\t",
		"\t\t",
		"\t\t\t",
		"\t\t\t\t",
		"\t\t\t\t\t",
		"\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t\t\t",
		"x",
		"x",
		"x",
		"x",
		"x",
		"x",
};

static const char *component = "xyzw";

static const char *type[] = {
		[TYPE_F16] = "f16",
		[TYPE_F32] = "f32",
		[TYPE_U16] = "u16",
		[TYPE_U32] = "u32",
		[TYPE_S16] = "s16",
		[TYPE_S32] = "s32",
		[TYPE_U8]  = "u8",
		[TYPE_S8]  = "s8",
};

struct disasm_ctx {
	FILE *out;
	int level;
	unsigned gpu_id;

	/* current instruction repeat flag: */
	unsigned repeat;
};

static void print_reg(struct disasm_ctx *ctx, reg_t reg, bool full, bool r,
		bool c, bool im, bool neg, bool abs, bool addr_rel)
{
	const char type = c ? 'c' : 'r';

	// XXX I prefer - and || for neg/abs, but preserving format used
	// by libllvm-a3xx for easy diffing..

	if (abs && neg)
		fprintf(ctx->out, "(absneg)");
	else if (neg)
		fprintf(ctx->out, "(neg)");
	else if (abs)
		fprintf(ctx->out, "(abs)");

	if (r)
		fprintf(ctx->out, "(r)");

	if (im) {
		fprintf(ctx->out, "%d", reg.iim_val);
	} else if (addr_rel) {
		/* I would just use %+d but trying to make it diff'able with
		 * libllvm-a3xx...
		 */
		if (reg.iim_val < 0)
			fprintf(ctx->out, "%s%c<a0.x - %d>", full ? "" : "h", type, -reg.iim_val);
		else if (reg.iim_val > 0)
			fprintf(ctx->out, "%s%c<a0.x + %d>", full ? "" : "h", type, reg.iim_val);
		else
			fprintf(ctx->out, "%s%c<a0.x>", full ? "" : "h", type);
	} else if ((reg.num == REG_A0) && !c) {
		fprintf(ctx->out, "a0.%c", component[reg.comp]);
	} else if ((reg.num == REG_P0) && !c) {
		fprintf(ctx->out, "p0.%c", component[reg.comp]);
	} else {
		fprintf(ctx->out, "%s%c%d.%c", full ? "" : "h", type, reg.num, component[reg.comp]);
	}
}


static void print_reg_dst(struct disasm_ctx *ctx, reg_t reg, bool full, bool addr_rel)
{
	print_reg(ctx, reg, full, false, false, false, false, false, addr_rel);
}

static void print_reg_src(struct disasm_ctx *ctx, reg_t reg, bool full, bool r,
		bool c, bool im, bool neg, bool abs, bool addr_rel)
{
	print_reg(ctx, reg, full, r, c, im, neg, abs, addr_rel);
}

/* TODO switch to using reginfo struct everywhere, since more readable
 * than passing a bunch of bools to print_reg_src
 */

struct reginfo {
	reg_t reg;
	bool full;
	bool r;
	bool c;
	bool im;
	bool neg;
	bool abs;
	bool addr_rel;
};

static void print_src(struct disasm_ctx *ctx, struct reginfo *info)
{
	print_reg_src(ctx, info->reg, info->full, info->r, info->c, info->im,
			info->neg, info->abs, info->addr_rel);
}

//static void print_dst(struct disasm_ctx *ctx, struct reginfo *info)
//{
//	print_reg_dst(ctx, info->reg, info->full, info->addr_rel);
//}

static void print_instr_cat0(struct disasm_ctx *ctx, instr_t *instr)
{
	instr_cat0_t *cat0 = &instr->cat0;

	switch (cat0->opc) {
	case OPC_KILL:
		fprintf(ctx->out, " %sp0.%c", cat0->inv ? "!" : "",
				component[cat0->comp]);
		break;
	case OPC_BR:
		fprintf(ctx->out, " %sp0.%c, #%d", cat0->inv ? "!" : "",
				component[cat0->comp], cat0->a3xx.immed);
		break;
	case OPC_JUMP:
	case OPC_CALL:
		fprintf(ctx->out, " #%d", cat0->a3xx.immed);
		break;
	}

	if ((debug & PRINT_VERBOSE) && (cat0->dummy2|cat0->dummy3|cat0->dummy4))
		fprintf(ctx->out, "\t{0: %x,%x,%x}", cat0->dummy2, cat0->dummy3, cat0->dummy4);
}

static void print_instr_cat1(struct disasm_ctx *ctx, instr_t *instr)
{
	instr_cat1_t *cat1 = &instr->cat1;

	if (cat1->ul)
		fprintf(ctx->out, "(ul)");

	if (cat1->src_type == cat1->dst_type) {
		if ((cat1->src_type == TYPE_S16) && (((reg_t)cat1->dst).num == REG_A0)) {
			/* special case (nmemonic?): */
			fprintf(ctx->out, "mova");
		} else {
			fprintf(ctx->out, "mov.%s%s", type[cat1->src_type], type[cat1->dst_type]);
		}
	} else {
		fprintf(ctx->out, "cov.%s%s", type[cat1->src_type], type[cat1->dst_type]);
	}

	fprintf(ctx->out, " ");

	if (cat1->even)
		fprintf(ctx->out, "(even)");

	if (cat1->pos_inf)
		fprintf(ctx->out, "(pos_infinity)");

	print_reg_dst(ctx, (reg_t)(cat1->dst), type_size(cat1->dst_type) == 32,
			cat1->dst_rel);

	fprintf(ctx->out, ", ");

	/* ugg, have to special case this.. vs print_reg().. */
	if (cat1->src_im) {
		if (type_float(cat1->src_type))
			fprintf(ctx->out, "(%f)", cat1->fim_val);
		else if (type_uint(cat1->src_type))
			fprintf(ctx->out, "0x%08x", cat1->uim_val);
		else
			fprintf(ctx->out, "%d", cat1->iim_val);
	} else if (cat1->src_rel && !cat1->src_c) {
		/* I would just use %+d but trying to make it diff'able with
		 * libllvm-a3xx...
		 */
		char type = cat1->src_rel_c ? 'c' : 'r';
		if (cat1->off < 0)
			fprintf(ctx->out, "%c<a0.x - %d>", type, -cat1->off);
		else if (cat1->off > 0)
			fprintf(ctx->out, "%c<a0.x + %d>", type, cat1->off);
		else
			fprintf(ctx->out, "%c<a0.x>", type);
	} else {
		print_reg_src(ctx, (reg_t)(cat1->src), type_size(cat1->src_type) == 32,
				cat1->src_r, cat1->src_c, cat1->src_im, false, false, false);
	}

	if ((debug & PRINT_VERBOSE) && (cat1->must_be_0))
		fprintf(ctx->out, "\t{1: %x}", cat1->must_be_0);
}

static void print_instr_cat2(struct disasm_ctx *ctx, instr_t *instr)
{
	instr_cat2_t *cat2 = &instr->cat2;
	static const char *cond[] = {
			"lt",
			"le",
			"gt",
			"ge",
			"eq",
			"ne",
			"?6?",
	};

	switch (_OPC(2, cat2->opc)) {
	case OPC_CMPS_F:
	case OPC_CMPS_U:
	case OPC_CMPS_S:
	case OPC_CMPV_F:
	case OPC_CMPV_U:
	case OPC_CMPV_S:
		fprintf(ctx->out, ".%s", cond[cat2->cond]);
		break;
	}

	fprintf(ctx->out, " ");
	if (cat2->ei)
		fprintf(ctx->out, "(ei)");
	print_reg_dst(ctx, (reg_t)(cat2->dst), cat2->full ^ cat2->dst_half, false);
	fprintf(ctx->out, ", ");

	unsigned src1_r = cat2->repeat ? cat2->src1_r : 0;
	if (cat2->c1.src1_c) {
		print_reg_src(ctx, (reg_t)(cat2->c1.src1), cat2->full, src1_r,
				cat2->c1.src1_c, cat2->src1_im, cat2->src1_neg,
				cat2->src1_abs, false);
	} else if (cat2->rel1.src1_rel) {
		print_reg_src(ctx, (reg_t)(cat2->rel1.src1), cat2->full, src1_r,
				cat2->rel1.src1_c, cat2->src1_im, cat2->src1_neg,
				cat2->src1_abs, cat2->rel1.src1_rel);
	} else {
		print_reg_src(ctx, (reg_t)(cat2->src1), cat2->full, src1_r,
				false, cat2->src1_im, cat2->src1_neg,
				cat2->src1_abs, false);
	}

	unsigned src2_r = cat2->repeat ? cat2->src2_r : 0;
	switch (_OPC(2, cat2->opc)) {
	case OPC_ABSNEG_F:
	case OPC_ABSNEG_S:
	case OPC_CLZ_B:
	case OPC_CLZ_S:
	case OPC_SIGN_F:
	case OPC_FLOOR_F:
	case OPC_CEIL_F:
	case OPC_RNDNE_F:
	case OPC_RNDAZ_F:
	case OPC_TRUNC_F:
	case OPC_NOT_B:
	case OPC_BFREV_B:
	case OPC_SETRM:
	case OPC_CBITS_B:
		/* these only have one src reg */
		break;
	default:
		fprintf(ctx->out, ", ");
		if (cat2->c2.src2_c) {
			print_reg_src(ctx, (reg_t)(cat2->c2.src2), cat2->full, src2_r,
					cat2->c2.src2_c, cat2->src2_im, cat2->src2_neg,
					cat2->src2_abs, false);
		} else if (cat2->rel2.src2_rel) {
			print_reg_src(ctx, (reg_t)(cat2->rel2.src2), cat2->full, src2_r,
					cat2->rel2.src2_c, cat2->src2_im, cat2->src2_neg,
					cat2->src2_abs, cat2->rel2.src2_rel);
		} else {
			print_reg_src(ctx, (reg_t)(cat2->src2), cat2->full, src2_r,
					false, cat2->src2_im, cat2->src2_neg,
					cat2->src2_abs, false);
		}
		break;
	}
}

static void print_instr_cat3(struct disasm_ctx *ctx, instr_t *instr)
{
	instr_cat3_t *cat3 = &instr->cat3;
	bool full = instr_cat3_full(cat3);

	fprintf(ctx->out, " ");
	print_reg_dst(ctx, (reg_t)(cat3->dst), full ^ cat3->dst_half, false);
	fprintf(ctx->out, ", ");
	unsigned src1_r = cat3->repeat ? cat3->src1_r : 0;
	if (cat3->c1.src1_c) {
		print_reg_src(ctx, (reg_t)(cat3->c1.src1), full,
				src1_r, cat3->c1.src1_c, false, cat3->src1_neg,
				false, false);
	} else if (cat3->rel1.src1_rel) {
		print_reg_src(ctx, (reg_t)(cat3->rel1.src1), full,
				src1_r, cat3->rel1.src1_c, false, cat3->src1_neg,
				false, cat3->rel1.src1_rel);
	} else {
		print_reg_src(ctx, (reg_t)(cat3->src1), full,
				src1_r, false, false, cat3->src1_neg,
				false, false);
	}
	fprintf(ctx->out, ", ");
	unsigned src2_r = cat3->repeat ? cat3->src2_r : 0;
	print_reg_src(ctx, (reg_t)cat3->src2, full,
			src2_r, cat3->src2_c, false, cat3->src2_neg,
			false, false);
	fprintf(ctx->out, ", ");
	if (cat3->c2.src3_c) {
		print_reg_src(ctx, (reg_t)(cat3->c2.src3), full,
				cat3->src3_r, cat3->c2.src3_c, false, cat3->src3_neg,
				false, false);
	} else if (cat3->rel2.src3_rel) {
		print_reg_src(ctx, (reg_t)(cat3->rel2.src3), full,
				cat3->src3_r, cat3->rel2.src3_c, false, cat3->src3_neg,
				false, cat3->rel2.src3_rel);
	} else {
		print_reg_src(ctx, (reg_t)(cat3->src3), full,
				cat3->src3_r, false, false, cat3->src3_neg,
				false, false);
	}
}

static void print_instr_cat4(struct disasm_ctx *ctx, instr_t *instr)
{
	instr_cat4_t *cat4 = &instr->cat4;

	fprintf(ctx->out, " ");
	print_reg_dst(ctx, (reg_t)(cat4->dst), cat4->full ^ cat4->dst_half, false);
	fprintf(ctx->out, ", ");

	if (cat4->c.src_c) {
		print_reg_src(ctx, (reg_t)(cat4->c.src), cat4->full,
				cat4->src_r, cat4->c.src_c, cat4->src_im,
				cat4->src_neg, cat4->src_abs, false);
	} else if (cat4->rel.src_rel) {
		print_reg_src(ctx, (reg_t)(cat4->rel.src), cat4->full,
				cat4->src_r, cat4->rel.src_c, cat4->src_im,
				cat4->src_neg, cat4->src_abs, cat4->rel.src_rel);
	} else {
		print_reg_src(ctx, (reg_t)(cat4->src), cat4->full,
				cat4->src_r, false, cat4->src_im,
				cat4->src_neg, cat4->src_abs, false);
	}

	if ((debug & PRINT_VERBOSE) && (cat4->dummy1|cat4->dummy2))
		fprintf(ctx->out, "\t{4: %x,%x}", cat4->dummy1, cat4->dummy2);
}

static void print_instr_cat5(struct disasm_ctx *ctx, instr_t *instr)
{
	static const struct {
		bool src1, src2, samp, tex;
	} info[0x1f] = {
			[opc_op(OPC_ISAM)]     = { true,  false, true,  true,  },
			[opc_op(OPC_ISAML)]    = { true,  true,  true,  true,  },
			[opc_op(OPC_ISAMM)]    = { true,  false, true,  true,  },
			[opc_op(OPC_SAM)]      = { true,  false, true,  true,  },
			[opc_op(OPC_SAMB)]     = { true,  true,  true,  true,  },
			[opc_op(OPC_SAML)]     = { true,  true,  true,  true,  },
			[opc_op(OPC_SAMGQ)]    = { true,  false, true,  true,  },
			[opc_op(OPC_GETLOD)]   = { true,  false, true,  true,  },
			[opc_op(OPC_CONV)]     = { true,  true,  true,  true,  },
			[opc_op(OPC_CONVM)]    = { true,  true,  true,  true,  },
			[opc_op(OPC_GETSIZE)]  = { true,  false, false, true,  },
			[opc_op(OPC_GETBUF)]   = { false, false, false, true,  },
			[opc_op(OPC_GETPOS)]   = { true,  false, false, true,  },
			[opc_op(OPC_GETINFO)]  = { false, false, false, true,  },
			[opc_op(OPC_DSX)]      = { true,  false, false, false, },
			[opc_op(OPC_DSY)]      = { true,  false, false, false, },
			[opc_op(OPC_GATHER4R)] = { true,  false, true,  true,  },
			[opc_op(OPC_GATHER4G)] = { true,  false, true,  true,  },
			[opc_op(OPC_GATHER4B)] = { true,  false, true,  true,  },
			[opc_op(OPC_GATHER4A)] = { true,  false, true,  true,  },
			[opc_op(OPC_SAMGP0)]   = { true,  false, true,  true,  },
			[opc_op(OPC_SAMGP1)]   = { true,  false, true,  true,  },
			[opc_op(OPC_SAMGP2)]   = { true,  false, true,  true,  },
			[opc_op(OPC_SAMGP3)]   = { true,  false, true,  true,  },
			[opc_op(OPC_DSXPP_1)]  = { true,  false, false, false, },
			[opc_op(OPC_DSYPP_1)]  = { true,  false, false, false, },
			[opc_op(OPC_RGETPOS)]  = { false, false, false, false, },
			[opc_op(OPC_RGETINFO)] = { false, false, false, false, },
	};
	instr_cat5_t *cat5 = &instr->cat5;
	int i;

	if (cat5->is_3d)   fprintf(ctx->out, ".3d");
	if (cat5->is_a)    fprintf(ctx->out, ".a");
	if (cat5->is_o)    fprintf(ctx->out, ".o");
	if (cat5->is_p)    fprintf(ctx->out, ".p");
	if (cat5->is_s)    fprintf(ctx->out, ".s");
	if (cat5->is_s2en) fprintf(ctx->out, ".s2en");

	fprintf(ctx->out, " ");

	switch (_OPC(5, cat5->opc)) {
	case OPC_DSXPP_1:
	case OPC_DSYPP_1:
		break;
	default:
		fprintf(ctx->out, "(%s)", type[cat5->type]);
		break;
	}

	fprintf(ctx->out, "(");
	for (i = 0; i < 4; i++)
		if (cat5->wrmask & (1 << i))
			fprintf(ctx->out, "%c", "xyzw"[i]);
	fprintf(ctx->out, ")");

	print_reg_dst(ctx, (reg_t)(cat5->dst), type_size(cat5->type) == 32, false);

	if (info[cat5->opc].src1) {
		fprintf(ctx->out, ", ");
		print_reg_src(ctx, (reg_t)(cat5->src1), cat5->full, false, false, false,
				false, false, false);
	}

	if (cat5->is_s2en) {
		fprintf(ctx->out, ", ");
		print_reg_src(ctx, (reg_t)(cat5->s2en.src2), cat5->full, false, false, false,
				false, false, false);
		fprintf(ctx->out, ", ");
		print_reg_src(ctx, (reg_t)(cat5->s2en.src3), false, false, false, false,
				false, false, false);
	} else {
		if (cat5->is_o || info[cat5->opc].src2) {
			fprintf(ctx->out, ", ");
			print_reg_src(ctx, (reg_t)(cat5->norm.src2), cat5->full,
					false, false, false, false, false, false);
		}
		if (info[cat5->opc].samp)
			fprintf(ctx->out, ", s#%d", cat5->norm.samp);
		if (info[cat5->opc].tex)
			fprintf(ctx->out, ", t#%d", cat5->norm.tex);
	}

	if (debug & PRINT_VERBOSE) {
		if (cat5->is_s2en) {
			if ((debug & PRINT_VERBOSE) && (cat5->s2en.dummy1|cat5->s2en.dummy2|cat5->dummy2))
				fprintf(ctx->out, "\t{5: %x,%x,%x}", cat5->s2en.dummy1, cat5->s2en.dummy2, cat5->dummy2);
		} else {
			if ((debug & PRINT_VERBOSE) && (cat5->norm.dummy1|cat5->dummy2))
				fprintf(ctx->out, "\t{5: %x,%x}", cat5->norm.dummy1, cat5->dummy2);
		}
	}
}

static void print_instr_cat6_a3xx(struct disasm_ctx *ctx, instr_t *instr)
{
	instr_cat6_t *cat6 = &instr->cat6;
	char sd = 0, ss = 0;  /* dst/src address space */
	bool nodst = false;
	struct reginfo dst, src1, src2;
	int src1off = 0, dstoff = 0;

	memset(&dst, 0, sizeof(dst));
	memset(&src1, 0, sizeof(src1));
	memset(&src2, 0, sizeof(src2));

	switch (_OPC(6, cat6->opc)) {
	case OPC_RESINFO:
	case OPC_RESFMT:
		dst.full  = type_size(cat6->type) == 32;
		src1.full = type_size(cat6->type) == 32;
		src2.full = type_size(cat6->type) == 32;
		break;
	case OPC_L2G:
	case OPC_G2L:
		dst.full = true;
		src1.full = true;
		src2.full = true;
		break;
	case OPC_STG:
	case OPC_STL:
	case OPC_STP:
	case OPC_STLW:
	case OPC_STIB:
		dst.full  = true;
		src1.full = type_size(cat6->type) == 32;
		src2.full = type_size(cat6->type) == 32;
		break;
	default:
		dst.full  = type_size(cat6->type) == 32;
		src1.full = true;
		src2.full = true;
		break;
	}

	switch (_OPC(6, cat6->opc)) {
	case OPC_PREFETCH:
		break;
	case OPC_RESINFO:
		fprintf(ctx->out, ".%dd", cat6->ldgb.d + 1);
		break;
	case OPC_LDGB:
		fprintf(ctx->out, ".%s", cat6->ldgb.typed ? "typed" : "untyped");
		fprintf(ctx->out, ".%dd", cat6->ldgb.d + 1);
		fprintf(ctx->out, ".%s", type[cat6->type]);
		fprintf(ctx->out, ".%d", cat6->ldgb.type_size + 1);
		break;
	case OPC_STGB:
	case OPC_STIB:
		fprintf(ctx->out, ".%s", cat6->stgb.typed ? "typed" : "untyped");
		fprintf(ctx->out, ".%dd", cat6->stgb.d + 1);
		fprintf(ctx->out, ".%s", type[cat6->type]);
		fprintf(ctx->out, ".%d", cat6->stgb.type_size + 1);
		break;
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
		ss = cat6->g ? 'g' : 'l';
		fprintf(ctx->out, ".%s", cat6->ldgb.typed ? "typed" : "untyped");
		fprintf(ctx->out, ".%dd", cat6->ldgb.d + 1);
		fprintf(ctx->out, ".%s", type[cat6->type]);
		fprintf(ctx->out, ".%d", cat6->ldgb.type_size + 1);
		fprintf(ctx->out, ".%c", ss);
		break;
	default:
		dst.im = cat6->g && !cat6->dst_off;
		fprintf(ctx->out, ".%s", type[cat6->type]);
		break;
	}
	fprintf(ctx->out, " ");

	switch (_OPC(6, cat6->opc)) {
	case OPC_STG:
		sd = 'g';
		break;
	case OPC_STP:
		sd = 'p';
		break;
	case OPC_STL:
	case OPC_STLW:
		sd = 'l';
		break;

	case OPC_LDG:
	case OPC_LDC:
		ss = 'g';
		break;
	case OPC_LDP:
		ss = 'p';
		break;
	case OPC_LDL:
	case OPC_LDLW:
	case OPC_LDLV:
		ss = 'l';
		break;

	case OPC_L2G:
		ss = 'l';
		sd = 'g';
		break;

	case OPC_G2L:
		ss = 'g';
		sd = 'l';
		break;

	case OPC_PREFETCH:
		ss = 'g';
		nodst = true;
		break;
	}

	if ((_OPC(6, cat6->opc) == OPC_STGB) || (_OPC(6, cat6->opc) == OPC_STIB)) {
		struct reginfo src3;

		memset(&src3, 0, sizeof(src3));

		src1.reg = (reg_t)(cat6->stgb.src1);
		src2.reg = (reg_t)(cat6->stgb.src2);
		src2.im  = cat6->stgb.src2_im;
		src3.reg = (reg_t)(cat6->stgb.src3);
		src3.im  = cat6->stgb.src3_im;
		src3.full = true;

		fprintf(ctx->out, "g[%u], ", cat6->stgb.dst_ssbo);
		print_src(ctx, &src1);
		fprintf(ctx->out, ", ");
		print_src(ctx, &src2);
		fprintf(ctx->out, ", ");
		print_src(ctx, &src3);

		if (debug & PRINT_VERBOSE)
			fprintf(ctx->out, " (pad0=%x, pad3=%x)", cat6->stgb.pad0, cat6->stgb.pad3);

		return;
	}

	if (is_atomic(_OPC(6, cat6->opc))) {

		src1.reg = (reg_t)(cat6->ldgb.src1);
		src1.im  = cat6->ldgb.src1_im;
		src2.reg = (reg_t)(cat6->ldgb.src2);
		src2.im  = cat6->ldgb.src2_im;
		dst.reg  = (reg_t)(cat6->ldgb.dst);

		print_src(ctx, &dst);
		fprintf(ctx->out, ", ");
		if (ss == 'g') {
			struct reginfo src3;
			memset(&src3, 0, sizeof(src3));

			src3.reg = (reg_t)(cat6->ldgb.src3);
			src3.full = true;

			/* For images, the ".typed" variant is used and src2 is
			 * the ivecN coordinates, ie ivec2 for 2d.
			 *
			 * For SSBOs, the ".untyped" variant is used and src2 is
			 * a simple dword offset..  src3 appears to be
			 * uvec2(offset * 4, 0).  Not sure the point of that.
			 */

			fprintf(ctx->out, "g[%u], ", cat6->ldgb.src_ssbo);
			print_src(ctx, &src1);  /* value */
			fprintf(ctx->out, ", ");
			print_src(ctx, &src2);  /* offset/coords */
			fprintf(ctx->out, ", ");
			print_src(ctx, &src3);  /* 64b byte offset.. */

			if (debug & PRINT_VERBOSE) {
				fprintf(ctx->out, " (pad0=%x, pad3=%x, mustbe0=%x)", cat6->ldgb.pad0,
						cat6->ldgb.pad3, cat6->ldgb.mustbe0);
			}
		} else { /* ss == 'l' */
			fprintf(ctx->out, "l[");
			print_src(ctx, &src1);  /* simple byte offset */
			fprintf(ctx->out, "], ");
			print_src(ctx, &src2);  /* value */

			if (debug & PRINT_VERBOSE) {
				fprintf(ctx->out, " (src3=%x, pad0=%x, pad3=%x, mustbe0=%x)",
						cat6->ldgb.src3, cat6->ldgb.pad0,
						cat6->ldgb.pad3, cat6->ldgb.mustbe0);
			}
		}

		return;
	} else if (_OPC(6, cat6->opc) == OPC_RESINFO) {
		dst.reg  = (reg_t)(cat6->ldgb.dst);

		print_src(ctx, &dst);
		fprintf(ctx->out, ", ");
		fprintf(ctx->out, "g[%u]", cat6->ldgb.src_ssbo);

		return;
	} else if (_OPC(6, cat6->opc) == OPC_LDGB) {

		src1.reg = (reg_t)(cat6->ldgb.src1);
		src1.im  = cat6->ldgb.src1_im;
		src2.reg = (reg_t)(cat6->ldgb.src2);
		src2.im  = cat6->ldgb.src2_im;
		dst.reg  = (reg_t)(cat6->ldgb.dst);

		print_src(ctx, &dst);
		fprintf(ctx->out, ", ");
		fprintf(ctx->out, "g[%u], ", cat6->ldgb.src_ssbo);
		print_src(ctx, &src1);
		fprintf(ctx->out, ", ");
		print_src(ctx, &src2);

		if (debug & PRINT_VERBOSE)
			fprintf(ctx->out, " (pad0=%x, pad3=%x, mustbe0=%x)", cat6->ldgb.pad0, cat6->ldgb.pad3, cat6->ldgb.mustbe0);

		return;
	}
	if (cat6->dst_off) {
		dst.reg = (reg_t)(cat6->c.dst);
		dstoff  = cat6->c.off;
	} else {
		dst.reg = (reg_t)(cat6->d.dst);
	}

	if (cat6->src_off) {
		src1.reg = (reg_t)(cat6->a.src1);
		src1.im  = cat6->a.src1_im;
		src2.reg = (reg_t)(cat6->a.src2);
		src2.im  = cat6->a.src2_im;
		src1off  = cat6->a.off;
	} else {
		src1.reg = (reg_t)(cat6->b.src1);
		src1.im  = cat6->b.src1_im;
		src2.reg = (reg_t)(cat6->b.src2);
		src2.im  = cat6->b.src2_im;
	}

	if (!nodst) {
		if (sd)
			fprintf(ctx->out, "%c[", sd);
		/* note: dst might actually be a src (ie. address to store to) */
		print_src(ctx, &dst);
		if (dstoff)
			fprintf(ctx->out, "%+d", dstoff);
		if (sd)
			fprintf(ctx->out, "]");
		fprintf(ctx->out, ", ");
	}

	if (ss)
		fprintf(ctx->out, "%c[", ss);

	/* can have a larger than normal immed, so hack: */
	if (src1.im) {
		fprintf(ctx->out, "%u", src1.reg.dummy13);
	} else {
		print_src(ctx, &src1);
	}

	if (src1off)
		fprintf(ctx->out, "%+d", src1off);
	if (ss)
		fprintf(ctx->out, "]");

	switch (_OPC(6, cat6->opc)) {
	case OPC_RESINFO:
	case OPC_RESFMT:
		break;
	default:
		fprintf(ctx->out, ", ");
		print_src(ctx, &src2);
		break;
	}
}

static void print_instr_cat6_a6xx(struct disasm_ctx *ctx, instr_t *instr)
{
	instr_cat6_a6xx_t *cat6 = &instr->cat6_a6xx;
	struct reginfo src1, src2;
	bool has_dest = _OPC(6, cat6->opc) == OPC_LDIB;
	char ss = 0;

	memset(&src1, 0, sizeof(src1));
	memset(&src2, 0, sizeof(src2));

	fprintf(ctx->out, ".%s", cat6->typed ? "typed" : "untyped");
	fprintf(ctx->out, ".%dd", cat6->d + 1);
	fprintf(ctx->out, ".%s", type[cat6->type]);
	fprintf(ctx->out, ".%u ", cat6->type_size + 1);

	if (has_dest) {
		src2.reg = (reg_t)(cat6->src2);
		src2.full = true; // XXX
		print_src(ctx, &src2);

		fprintf(ctx->out, ", ");
	}

	/* NOTE: blob seems to use old encoding for ldl/stl (local memory) */
	ss = 'g';

	fprintf(ctx->out, "%c[%u", ss, cat6->ssbo);
	fprintf(ctx->out, "] + ");
	src1.reg = (reg_t)(cat6->src1);
	src1.full = true; // XXX
	print_src(ctx, &src1);

	if (!has_dest) {
		fprintf(ctx->out, ", ");

		src2.reg = (reg_t)(cat6->src2);
		src2.full = true; // XXX
		print_src(ctx, &src2);
	}

	if (debug & PRINT_VERBOSE) {
		fprintf(ctx->out, " (pad1=%x, pad2=%x, pad3=%x, pad4=%x)", cat6->pad1,
				cat6->pad2, cat6->pad3, cat6->pad4);
	}
}

static void print_instr_cat6(struct disasm_ctx *ctx, instr_t *instr)
{
	// TODO not sure if this is the best way to figure
	// out if new vs old encoding, but it kinda seems
	// to work:
	if ((ctx->gpu_id >= 600) && (instr->cat6.opc == 0)) {
		print_instr_cat6_a6xx(ctx, instr);
		if (debug & PRINT_VERBOSE)
			fprintf(ctx->out, " NEW");
	} else {
		print_instr_cat6_a3xx(ctx, instr);
		if (debug & PRINT_VERBOSE)
			fprintf(ctx->out, " LEGACY");
	}
}
static void print_instr_cat7(struct disasm_ctx *ctx, instr_t *instr)
{
	instr_cat7_t *cat7 = &instr->cat7;

	if (cat7->g)
		fprintf(ctx->out, ".g");
	if (cat7->l)
		fprintf(ctx->out, ".l");

	if (_OPC(7, cat7->opc) == OPC_FENCE) {
		if (cat7->r)
			fprintf(ctx->out, ".r");
		if (cat7->w)
			fprintf(ctx->out, ".w");
	}
}

/* size of largest OPC field of all the instruction categories: */
#define NOPC_BITS 6

static const struct opc_info {
	uint16_t cat;
	uint16_t opc;
	const char *name;
	void (*print)(struct disasm_ctx *ctx, instr_t *instr);
} opcs[1 << (3+NOPC_BITS)] = {
#define OPC(cat, opc, name) [(opc)] = { (cat), (opc), #name, print_instr_cat##cat }
	/* category 0: */
	OPC(0, OPC_NOP,          nop),
	OPC(0, OPC_BR,           br),
	OPC(0, OPC_JUMP,         jump),
	OPC(0, OPC_CALL,         call),
	OPC(0, OPC_RET,          ret),
	OPC(0, OPC_KILL,         kill),
	OPC(0, OPC_END,          end),
	OPC(0, OPC_EMIT,         emit),
	OPC(0, OPC_CUT,          cut),
	OPC(0, OPC_CHMASK,       chmask),
	OPC(0, OPC_CHSH,         chsh),
	OPC(0, OPC_FLOW_REV,     flow_rev),

	/* category 1: */
	OPC(1, OPC_MOV, ),

	/* category 2: */
	OPC(2, OPC_ADD_F,        add.f),
	OPC(2, OPC_MIN_F,        min.f),
	OPC(2, OPC_MAX_F,        max.f),
	OPC(2, OPC_MUL_F,        mul.f),
	OPC(2, OPC_SIGN_F,       sign.f),
	OPC(2, OPC_CMPS_F,       cmps.f),
	OPC(2, OPC_ABSNEG_F,     absneg.f),
	OPC(2, OPC_CMPV_F,       cmpv.f),
	OPC(2, OPC_FLOOR_F,      floor.f),
	OPC(2, OPC_CEIL_F,       ceil.f),
	OPC(2, OPC_RNDNE_F,      rndne.f),
	OPC(2, OPC_RNDAZ_F,      rndaz.f),
	OPC(2, OPC_TRUNC_F,      trunc.f),
	OPC(2, OPC_ADD_U,        add.u),
	OPC(2, OPC_ADD_S,        add.s),
	OPC(2, OPC_SUB_U,        sub.u),
	OPC(2, OPC_SUB_S,        sub.s),
	OPC(2, OPC_CMPS_U,       cmps.u),
	OPC(2, OPC_CMPS_S,       cmps.s),
	OPC(2, OPC_MIN_U,        min.u),
	OPC(2, OPC_MIN_S,        min.s),
	OPC(2, OPC_MAX_U,        max.u),
	OPC(2, OPC_MAX_S,        max.s),
	OPC(2, OPC_ABSNEG_S,     absneg.s),
	OPC(2, OPC_AND_B,        and.b),
	OPC(2, OPC_OR_B,         or.b),
	OPC(2, OPC_NOT_B,        not.b),
	OPC(2, OPC_XOR_B,        xor.b),
	OPC(2, OPC_CMPV_U,       cmpv.u),
	OPC(2, OPC_CMPV_S,       cmpv.s),
	OPC(2, OPC_MUL_U,        mul.u),
	OPC(2, OPC_MUL_S,        mul.s),
	OPC(2, OPC_MULL_U,       mull.u),
	OPC(2, OPC_BFREV_B,      bfrev.b),
	OPC(2, OPC_CLZ_S,        clz.s),
	OPC(2, OPC_CLZ_B,        clz.b),
	OPC(2, OPC_SHL_B,        shl.b),
	OPC(2, OPC_SHR_B,        shr.b),
	OPC(2, OPC_ASHR_B,       ashr.b),
	OPC(2, OPC_BARY_F,       bary.f),
	OPC(2, OPC_MGEN_B,       mgen.b),
	OPC(2, OPC_GETBIT_B,     getbit.b),
	OPC(2, OPC_SETRM,        setrm),
	OPC(2, OPC_CBITS_B,      cbits.b),
	OPC(2, OPC_SHB,          shb),
	OPC(2, OPC_MSAD,         msad),

	/* category 3: */
	OPC(3, OPC_MAD_U16,      mad.u16),
	OPC(3, OPC_MADSH_U16,    madsh.u16),
	OPC(3, OPC_MAD_S16,      mad.s16),
	OPC(3, OPC_MADSH_M16,    madsh.m16),
	OPC(3, OPC_MAD_U24,      mad.u24),
	OPC(3, OPC_MAD_S24,      mad.s24),
	OPC(3, OPC_MAD_F16,      mad.f16),
	OPC(3, OPC_MAD_F32,      mad.f32),
	OPC(3, OPC_SEL_B16,      sel.b16),
	OPC(3, OPC_SEL_B32,      sel.b32),
	OPC(3, OPC_SEL_S16,      sel.s16),
	OPC(3, OPC_SEL_S32,      sel.s32),
	OPC(3, OPC_SEL_F16,      sel.f16),
	OPC(3, OPC_SEL_F32,      sel.f32),
	OPC(3, OPC_SAD_S16,      sad.s16),
	OPC(3, OPC_SAD_S32,      sad.s32),

	/* category 4: */
	OPC(4, OPC_RCP,          rcp),
	OPC(4, OPC_RSQ,          rsq),
	OPC(4, OPC_LOG2,         log2),
	OPC(4, OPC_EXP2,         exp2),
	OPC(4, OPC_SIN,          sin),
	OPC(4, OPC_COS,          cos),
	OPC(4, OPC_SQRT,         sqrt),

	/* category 5: */
	OPC(5, OPC_ISAM,         isam),
	OPC(5, OPC_ISAML,        isaml),
	OPC(5, OPC_ISAMM,        isamm),
	OPC(5, OPC_SAM,          sam),
	OPC(5, OPC_SAMB,         samb),
	OPC(5, OPC_SAML,         saml),
	OPC(5, OPC_SAMGQ,        samgq),
	OPC(5, OPC_GETLOD,       getlod),
	OPC(5, OPC_CONV,         conv),
	OPC(5, OPC_CONVM,        convm),
	OPC(5, OPC_GETSIZE,      getsize),
	OPC(5, OPC_GETBUF,       getbuf),
	OPC(5, OPC_GETPOS,       getpos),
	OPC(5, OPC_GETINFO,      getinfo),
	OPC(5, OPC_DSX,          dsx),
	OPC(5, OPC_DSY,          dsy),
	OPC(5, OPC_GATHER4R,     gather4r),
	OPC(5, OPC_GATHER4G,     gather4g),
	OPC(5, OPC_GATHER4B,     gather4b),
	OPC(5, OPC_GATHER4A,     gather4a),
	OPC(5, OPC_SAMGP0,       samgp0),
	OPC(5, OPC_SAMGP1,       samgp1),
	OPC(5, OPC_SAMGP2,       samgp2),
	OPC(5, OPC_SAMGP3,       samgp3),
	OPC(5, OPC_DSXPP_1,      dsxpp.1),
	OPC(5, OPC_DSYPP_1,      dsypp.1),
	OPC(5, OPC_RGETPOS,      rgetpos),
	OPC(5, OPC_RGETINFO,     rgetinfo),


	/* category 6: */
	OPC(6, OPC_LDG,          ldg),
	OPC(6, OPC_LDL,          ldl),
	OPC(6, OPC_LDP,          ldp),
	OPC(6, OPC_STG,          stg),
	OPC(6, OPC_STL,          stl),
	OPC(6, OPC_STP,          stp),
	OPC(6, OPC_LDIB,         ldib),
	OPC(6, OPC_G2L,          g2l),
	OPC(6, OPC_L2G,          l2g),
	OPC(6, OPC_PREFETCH,     prefetch),
	OPC(6, OPC_LDLW,         ldlw),
	OPC(6, OPC_STLW,         stlw),
	OPC(6, OPC_RESFMT,       resfmt),
	OPC(6, OPC_RESINFO,      resinfo),
	OPC(6, OPC_ATOMIC_ADD,     atomic.add),
	OPC(6, OPC_ATOMIC_SUB,     atomic.sub),
	OPC(6, OPC_ATOMIC_XCHG,    atomic.xchg),
	OPC(6, OPC_ATOMIC_INC,     atomic.inc),
	OPC(6, OPC_ATOMIC_DEC,     atomic.dec),
	OPC(6, OPC_ATOMIC_CMPXCHG, atomic.cmpxchg),
	OPC(6, OPC_ATOMIC_MIN,     atomic.min),
	OPC(6, OPC_ATOMIC_MAX,     atomic.max),
	OPC(6, OPC_ATOMIC_AND,     atomic.and),
	OPC(6, OPC_ATOMIC_OR,      atomic.or),
	OPC(6, OPC_ATOMIC_XOR,     atomic.xor),
	OPC(6, OPC_LDGB,         ldgb),
	OPC(6, OPC_STGB,         stgb),
	OPC(6, OPC_STIB,         stib),
	OPC(6, OPC_LDC,          ldc),
	OPC(6, OPC_LDLV,         ldlv),

	OPC(7, OPC_BAR,          bar),
	OPC(7, OPC_FENCE,        fence),

#undef OPC
};

#define GETINFO(instr) (&(opcs[((instr)->opc_cat << NOPC_BITS) | instr_opc(instr, ctx->gpu_id)]))

// XXX hack.. probably should move this table somewhere common:
#include "ir3.h"
const char *ir3_instr_name(struct ir3_instruction *instr)
{
	if (opc_cat(instr->opc) == -1) return "??meta??";
	return opcs[instr->opc].name;
}

static bool print_instr(struct disasm_ctx *ctx, uint32_t *dwords, int n)
{
	instr_t *instr = (instr_t *)dwords;
	uint32_t opc = instr_opc(instr, ctx->gpu_id);
	const char *name;

	if (debug & PRINT_VERBOSE)
		fprintf(ctx->out, "%s%04d[%08xx_%08xx] ", levels[ctx->level], n, dwords[1], dwords[0]);

	/* NOTE: order flags are printed is a bit fugly.. but for now I
	 * try to match the order in llvm-a3xx disassembler for easy
	 * diff'ing..
	 */

	ctx->repeat = instr_repeat(instr);

	if (instr->sync)
		fprintf(ctx->out, "(sy)");
	if (instr->ss && ((instr->opc_cat <= 4) || (instr->opc_cat == 7)))
		fprintf(ctx->out, "(ss)");
	if (instr->jmp_tgt)
		fprintf(ctx->out, "(jp)");
	if (instr_sat(instr))
		fprintf(ctx->out, "(sat)");
	if (ctx->repeat) {
		fprintf(ctx->out, "(rpt%d)", ctx->repeat);
	} else if ((instr->opc_cat == 2) && (instr->cat2.src1_r || instr->cat2.src2_r)) {
		unsigned nop = (instr->cat2.src2_r * 2) + instr->cat2.src1_r;
		fprintf(ctx->out, "(nop%d)", nop);
	} else if ((instr->opc_cat == 3) && (instr->cat3.src1_r || instr->cat3.src2_r)) {
		unsigned nop = (instr->cat3.src2_r * 2) + instr->cat3.src1_r;
		fprintf(ctx->out, "(nop%d)", nop);
	}
	if (instr->ul && ((2 <= instr->opc_cat) && (instr->opc_cat <= 4)))
		fprintf(ctx->out, "(ul)");

	name = GETINFO(instr)->name;

	if (name) {
		fprintf(ctx->out, "%s", name);
		GETINFO(instr)->print(ctx, instr);
	} else {
		fprintf(ctx->out, "unknown(%d,%d)", instr->opc_cat, opc);
	}

	fprintf(ctx->out, "\n");

	return (instr->opc_cat == 0) && (opc == OPC_END);
}

int disasm_a3xx(uint32_t *dwords, int sizedwords, int level, FILE *out, unsigned gpu_id)
{
	struct disasm_ctx ctx;
	int i;

	assert((sizedwords % 2) == 0);

	memset(&ctx, 0, sizeof(ctx));
	ctx.out = out;
	ctx.level = level;
	ctx.gpu_id = gpu_id;

	for (i = 0; i < sizedwords; i += 2)
		print_instr(&ctx, &dwords[i], i/2);

	return 0;
}
