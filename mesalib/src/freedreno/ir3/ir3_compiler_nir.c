/*
 * Copyright (C) 2015 Rob Clark <robclark@freedesktop.org>
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
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include <stdarg.h>

#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_math.h"

#include "ir3_compiler.h"
#include "ir3_shader.h"
#include "ir3_nir.h"

#include "instr-a3xx.h"
#include "ir3.h"
#include "ir3_context.h"


static struct ir3_instruction *
create_indirect_load(struct ir3_context *ctx, unsigned arrsz, int n,
		struct ir3_instruction *address, struct ir3_instruction *collect)
{
	struct ir3_block *block = ctx->block;
	struct ir3_instruction *mov;
	struct ir3_register *src;

	mov = ir3_instr_create(block, OPC_MOV);
	mov->cat1.src_type = TYPE_U32;
	mov->cat1.dst_type = TYPE_U32;
	ir3_reg_create(mov, 0, 0);
	src = ir3_reg_create(mov, 0, IR3_REG_SSA | IR3_REG_RELATIV);
	src->instr = collect;
	src->size  = arrsz;
	src->array.offset = n;

	ir3_instr_set_address(mov, address);

	return mov;
}

static struct ir3_instruction *
create_input_compmask(struct ir3_context *ctx, unsigned n, unsigned compmask)
{
	struct ir3_instruction *in;

	in = ir3_instr_create(ctx->in_block, OPC_META_INPUT);
	in->inout.block = ctx->in_block;
	ir3_reg_create(in, n, 0);

	in->regs[0]->wrmask = compmask;

	return in;
}

static struct ir3_instruction *
create_input(struct ir3_context *ctx, unsigned n)
{
	return create_input_compmask(ctx, n, 0x1);
}

static struct ir3_instruction *
create_frag_input(struct ir3_context *ctx, bool use_ldlv)
{
	struct ir3_block *block = ctx->block;
	struct ir3_instruction *instr;
	/* actual inloc is assigned and fixed up later: */
	struct ir3_instruction *inloc = create_immed(block, 0);

	if (use_ldlv) {
		instr = ir3_LDLV(block, inloc, 0, create_immed(block, 1), 0);
		instr->cat6.type = TYPE_U32;
		instr->cat6.iim_val = 1;
	} else {
		instr = ir3_BARY_F(block, inloc, 0, ctx->frag_vcoord, 0);
		instr->regs[2]->wrmask = 0x3;
	}

	return instr;
}

static struct ir3_instruction *
create_driver_param(struct ir3_context *ctx, enum ir3_driver_param dp)
{
	/* first four vec4 sysval's reserved for UBOs: */
	/* NOTE: dp is in scalar, but there can be >4 dp components: */
	unsigned n = ctx->so->constbase.driver_param;
	unsigned r = regid(n + dp / 4, dp % 4);
	return create_uniform(ctx->block, r);
}

/*
 * Adreno uses uint rather than having dedicated bool type,
 * which (potentially) requires some conversion, in particular
 * when using output of an bool instr to int input, or visa
 * versa.
 *
 *         | Adreno  |  NIR  |
 *  -------+---------+-------+-
 *   true  |    1    |  ~0   |
 *   false |    0    |   0   |
 *
 * To convert from an adreno bool (uint) to nir, use:
 *
 *    absneg.s dst, (neg)src
 *
 * To convert back in the other direction:
 *
 *    absneg.s dst, (abs)arc
 *
 * The CP step can clean up the absneg.s that cancel each other
 * out, and with a slight bit of extra cleverness (to recognize
 * the instructions which produce either a 0 or 1) can eliminate
 * the absneg.s's completely when an instruction that wants
 * 0/1 consumes the result.  For example, when a nir 'bcsel'
 * consumes the result of 'feq'.  So we should be able to get by
 * without a boolean resolve step, and without incuring any
 * extra penalty in instruction count.
 */

/* NIR bool -> native (adreno): */
static struct ir3_instruction *
ir3_b2n(struct ir3_block *block, struct ir3_instruction *instr)
{
	return ir3_ABSNEG_S(block, instr, IR3_REG_SABS);
}

/* native (adreno) -> NIR bool: */
static struct ir3_instruction *
ir3_n2b(struct ir3_block *block, struct ir3_instruction *instr)
{
	return ir3_ABSNEG_S(block, instr, IR3_REG_SNEG);
}

/*
 * alu/sfu instructions:
 */

static struct ir3_instruction *
create_cov(struct ir3_context *ctx, struct ir3_instruction *src,
		unsigned src_bitsize, nir_op op)
{
	type_t src_type, dst_type;

	switch (op) {
	case nir_op_f2f32:
	case nir_op_f2f16_rtne:
	case nir_op_f2f16_rtz:
	case nir_op_f2f16:
	case nir_op_f2i32:
	case nir_op_f2i16:
	case nir_op_f2i8:
	case nir_op_f2u32:
	case nir_op_f2u16:
	case nir_op_f2u8:
		switch (src_bitsize) {
		case 32:
			src_type = TYPE_F32;
			break;
		case 16:
			src_type = TYPE_F16;
			break;
		default:
			ir3_context_error(ctx, "invalid src bit size: %u", src_bitsize);
		}
		break;

	case nir_op_i2f32:
	case nir_op_i2f16:
	case nir_op_i2i32:
	case nir_op_i2i16:
	case nir_op_i2i8:
		switch (src_bitsize) {
		case 32:
			src_type = TYPE_S32;
			break;
		case 16:
			src_type = TYPE_S16;
			break;
		case 8:
			src_type = TYPE_S8;
			break;
		default:
			ir3_context_error(ctx, "invalid src bit size: %u", src_bitsize);
		}
		break;

	case nir_op_u2f32:
	case nir_op_u2f16:
	case nir_op_u2u32:
	case nir_op_u2u16:
	case nir_op_u2u8:
		switch (src_bitsize) {
		case 32:
			src_type = TYPE_U32;
			break;
		case 16:
			src_type = TYPE_U16;
			break;
		case 8:
			src_type = TYPE_U8;
			break;
		default:
			ir3_context_error(ctx, "invalid src bit size: %u", src_bitsize);
		}
		break;

	default:
		ir3_context_error(ctx, "invalid conversion op: %u", op);
	}

	switch (op) {
	case nir_op_f2f32:
	case nir_op_i2f32:
	case nir_op_u2f32:
		dst_type = TYPE_F32;
		break;

	case nir_op_f2f16_rtne:
	case nir_op_f2f16_rtz:
	case nir_op_f2f16:
		/* TODO how to handle rounding mode? */
	case nir_op_i2f16:
	case nir_op_u2f16:
		dst_type = TYPE_F16;
		break;

	case nir_op_f2i32:
	case nir_op_i2i32:
		dst_type = TYPE_S32;
		break;

	case nir_op_f2i16:
	case nir_op_i2i16:
		dst_type = TYPE_S16;
		break;

	case nir_op_f2i8:
	case nir_op_i2i8:
		dst_type = TYPE_S8;
		break;

	case nir_op_f2u32:
	case nir_op_u2u32:
		dst_type = TYPE_U32;
		break;

	case nir_op_f2u16:
	case nir_op_u2u16:
		dst_type = TYPE_U16;
		break;

	case nir_op_f2u8:
	case nir_op_u2u8:
		dst_type = TYPE_U8;
		break;

	default:
		ir3_context_error(ctx, "invalid conversion op: %u", op);
	}

	return ir3_COV(ctx->block, src, src_type, dst_type);
}

static void
emit_alu(struct ir3_context *ctx, nir_alu_instr *alu)
{
	const nir_op_info *info = &nir_op_infos[alu->op];
	struct ir3_instruction **dst, *src[info->num_inputs];
	unsigned bs[info->num_inputs];     /* bit size */
	struct ir3_block *b = ctx->block;
	unsigned dst_sz, wrmask;

	if (alu->dest.dest.is_ssa) {
		dst_sz = alu->dest.dest.ssa.num_components;
		wrmask = (1 << dst_sz) - 1;
	} else {
		dst_sz = alu->dest.dest.reg.reg->num_components;
		wrmask = alu->dest.write_mask;
	}

	dst = ir3_get_dst(ctx, &alu->dest.dest, dst_sz);

	/* Vectors are special in that they have non-scalarized writemasks,
	 * and just take the first swizzle channel for each argument in
	 * order into each writemask channel.
	 */
	if ((alu->op == nir_op_vec2) ||
			(alu->op == nir_op_vec3) ||
			(alu->op == nir_op_vec4)) {

		for (int i = 0; i < info->num_inputs; i++) {
			nir_alu_src *asrc = &alu->src[i];

			compile_assert(ctx, !asrc->abs);
			compile_assert(ctx, !asrc->negate);

			src[i] = ir3_get_src(ctx, &asrc->src)[asrc->swizzle[0]];
			if (!src[i])
				src[i] = create_immed(ctx->block, 0);
			dst[i] = ir3_MOV(b, src[i], TYPE_U32);
		}

		put_dst(ctx, &alu->dest.dest);
		return;
	}

	/* We also get mov's with more than one component for mov's so
	 * handle those specially:
	 */
	if ((alu->op == nir_op_imov) || (alu->op == nir_op_fmov)) {
		type_t type = (alu->op == nir_op_imov) ? TYPE_U32 : TYPE_F32;
		nir_alu_src *asrc = &alu->src[0];
		struct ir3_instruction *const *src0 = ir3_get_src(ctx, &asrc->src);

		for (unsigned i = 0; i < dst_sz; i++) {
			if (wrmask & (1 << i)) {
				dst[i] = ir3_MOV(b, src0[asrc->swizzle[i]], type);
			} else {
				dst[i] = NULL;
			}
		}

		put_dst(ctx, &alu->dest.dest);
		return;
	}

	/* General case: We can just grab the one used channel per src. */
	for (int i = 0; i < info->num_inputs; i++) {
		unsigned chan = ffs(alu->dest.write_mask) - 1;
		nir_alu_src *asrc = &alu->src[i];

		compile_assert(ctx, !asrc->abs);
		compile_assert(ctx, !asrc->negate);

		src[i] = ir3_get_src(ctx, &asrc->src)[asrc->swizzle[chan]];
		bs[i] = nir_src_bit_size(asrc->src);

		compile_assert(ctx, src[i]);
	}

	switch (alu->op) {
	case nir_op_f2f32:
	case nir_op_f2f16_rtne:
	case nir_op_f2f16_rtz:
	case nir_op_f2f16:
	case nir_op_f2i32:
	case nir_op_f2i16:
	case nir_op_f2i8:
	case nir_op_f2u32:
	case nir_op_f2u16:
	case nir_op_f2u8:
	case nir_op_i2f32:
	case nir_op_i2f16:
	case nir_op_i2i32:
	case nir_op_i2i16:
	case nir_op_i2i8:
	case nir_op_u2f32:
	case nir_op_u2f16:
	case nir_op_u2u32:
	case nir_op_u2u16:
	case nir_op_u2u8:
		dst[0] = create_cov(ctx, src[0], bs[0], alu->op);
		break;
	case nir_op_f2b32:
		dst[0] = ir3_CMPS_F(b, src[0], 0, create_immed(b, fui(0.0)), 0);
		dst[0]->cat2.condition = IR3_COND_NE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_b2f16:
	case nir_op_b2f32:
		dst[0] = ir3_COV(b, ir3_b2n(b, src[0]), TYPE_U32, TYPE_F32);
		break;
	case nir_op_b2i8:
	case nir_op_b2i16:
	case nir_op_b2i32:
		dst[0] = ir3_b2n(b, src[0]);
		break;
	case nir_op_i2b32:
		dst[0] = ir3_CMPS_S(b, src[0], 0, create_immed(b, 0), 0);
		dst[0]->cat2.condition = IR3_COND_NE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;

	case nir_op_fneg:
		dst[0] = ir3_ABSNEG_F(b, src[0], IR3_REG_FNEG);
		break;
	case nir_op_fabs:
		dst[0] = ir3_ABSNEG_F(b, src[0], IR3_REG_FABS);
		break;
	case nir_op_fmax:
		dst[0] = ir3_MAX_F(b, src[0], 0, src[1], 0);
		break;
	case nir_op_fmin:
		dst[0] = ir3_MIN_F(b, src[0], 0, src[1], 0);
		break;
	case nir_op_fsat:
		/* if there is just a single use of the src, and it supports
		 * (sat) bit, we can just fold the (sat) flag back to the
		 * src instruction and create a mov.  This is easier for cp
		 * to eliminate.
		 *
		 * TODO probably opc_cat==4 is ok too
		 */
		if (alu->src[0].src.is_ssa &&
				(list_length(&alu->src[0].src.ssa->uses) == 1) &&
				((opc_cat(src[0]->opc) == 2) || (opc_cat(src[0]->opc) == 3))) {
			src[0]->flags |= IR3_INSTR_SAT;
			dst[0] = ir3_MOV(b, src[0], TYPE_U32);
		} else {
			/* otherwise generate a max.f that saturates.. blob does
			 * similar (generating a cat2 mov using max.f)
			 */
			dst[0] = ir3_MAX_F(b, src[0], 0, src[0], 0);
			dst[0]->flags |= IR3_INSTR_SAT;
		}
		break;
	case nir_op_fmul:
		dst[0] = ir3_MUL_F(b, src[0], 0, src[1], 0);
		break;
	case nir_op_fadd:
		dst[0] = ir3_ADD_F(b, src[0], 0, src[1], 0);
		break;
	case nir_op_fsub:
		dst[0] = ir3_ADD_F(b, src[0], 0, src[1], IR3_REG_FNEG);
		break;
	case nir_op_ffma:
		dst[0] = ir3_MAD_F32(b, src[0], 0, src[1], 0, src[2], 0);
		break;
	case nir_op_fddx:
		dst[0] = ir3_DSX(b, src[0], 0);
		dst[0]->cat5.type = TYPE_F32;
		break;
	case nir_op_fddy:
		dst[0] = ir3_DSY(b, src[0], 0);
		dst[0]->cat5.type = TYPE_F32;
		break;
		break;
	case nir_op_flt:
		dst[0] = ir3_CMPS_F(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_LT;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_fge:
		dst[0] = ir3_CMPS_F(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_GE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_feq:
		dst[0] = ir3_CMPS_F(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_EQ;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_fne:
		dst[0] = ir3_CMPS_F(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_NE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_fceil:
		dst[0] = ir3_CEIL_F(b, src[0], 0);
		break;
	case nir_op_ffloor:
		dst[0] = ir3_FLOOR_F(b, src[0], 0);
		break;
	case nir_op_ftrunc:
		dst[0] = ir3_TRUNC_F(b, src[0], 0);
		break;
	case nir_op_fround_even:
		dst[0] = ir3_RNDNE_F(b, src[0], 0);
		break;
	case nir_op_fsign:
		dst[0] = ir3_SIGN_F(b, src[0], 0);
		break;

	case nir_op_fsin:
		dst[0] = ir3_SIN(b, src[0], 0);
		break;
	case nir_op_fcos:
		dst[0] = ir3_COS(b, src[0], 0);
		break;
	case nir_op_frsq:
		dst[0] = ir3_RSQ(b, src[0], 0);
		break;
	case nir_op_frcp:
		dst[0] = ir3_RCP(b, src[0], 0);
		break;
	case nir_op_flog2:
		dst[0] = ir3_LOG2(b, src[0], 0);
		break;
	case nir_op_fexp2:
		dst[0] = ir3_EXP2(b, src[0], 0);
		break;
	case nir_op_fsqrt:
		dst[0] = ir3_SQRT(b, src[0], 0);
		break;

	case nir_op_iabs:
		dst[0] = ir3_ABSNEG_S(b, src[0], IR3_REG_SABS);
		break;
	case nir_op_iadd:
		dst[0] = ir3_ADD_U(b, src[0], 0, src[1], 0);
		break;
	case nir_op_iand:
		dst[0] = ir3_AND_B(b, src[0], 0, src[1], 0);
		break;
	case nir_op_imax:
		dst[0] = ir3_MAX_S(b, src[0], 0, src[1], 0);
		break;
	case nir_op_umax:
		dst[0] = ir3_MAX_U(b, src[0], 0, src[1], 0);
		break;
	case nir_op_imin:
		dst[0] = ir3_MIN_S(b, src[0], 0, src[1], 0);
		break;
	case nir_op_umin:
		dst[0] = ir3_MIN_U(b, src[0], 0, src[1], 0);
		break;
	case nir_op_imul:
		/*
		 * dst = (al * bl) + (ah * bl << 16) + (al * bh << 16)
		 *   mull.u tmp0, a, b           ; mul low, i.e. al * bl
		 *   madsh.m16 tmp1, a, b, tmp0  ; mul-add shift high mix, i.e. ah * bl << 16
		 *   madsh.m16 dst, b, a, tmp1   ; i.e. al * bh << 16
		 */
		dst[0] = ir3_MADSH_M16(b, src[1], 0, src[0], 0,
					ir3_MADSH_M16(b, src[0], 0, src[1], 0,
						ir3_MULL_U(b, src[0], 0, src[1], 0), 0), 0);
		break;
	case nir_op_ineg:
		dst[0] = ir3_ABSNEG_S(b, src[0], IR3_REG_SNEG);
		break;
	case nir_op_inot:
		dst[0] = ir3_NOT_B(b, src[0], 0);
		break;
	case nir_op_ior:
		dst[0] = ir3_OR_B(b, src[0], 0, src[1], 0);
		break;
	case nir_op_ishl:
		dst[0] = ir3_SHL_B(b, src[0], 0, src[1], 0);
		break;
	case nir_op_ishr:
		dst[0] = ir3_ASHR_B(b, src[0], 0, src[1], 0);
		break;
	case nir_op_isign: {
		/* maybe this would be sane to lower in nir.. */
		struct ir3_instruction *neg, *pos;

		neg = ir3_CMPS_S(b, src[0], 0, create_immed(b, 0), 0);
		neg->cat2.condition = IR3_COND_LT;

		pos = ir3_CMPS_S(b, src[0], 0, create_immed(b, 0), 0);
		pos->cat2.condition = IR3_COND_GT;

		dst[0] = ir3_SUB_U(b, pos, 0, neg, 0);

		break;
	}
	case nir_op_isub:
		dst[0] = ir3_SUB_U(b, src[0], 0, src[1], 0);
		break;
	case nir_op_ixor:
		dst[0] = ir3_XOR_B(b, src[0], 0, src[1], 0);
		break;
	case nir_op_ushr:
		dst[0] = ir3_SHR_B(b, src[0], 0, src[1], 0);
		break;
	case nir_op_ilt:
		dst[0] = ir3_CMPS_S(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_LT;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_ige:
		dst[0] = ir3_CMPS_S(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_GE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_ieq:
		dst[0] = ir3_CMPS_S(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_EQ;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_ine:
		dst[0] = ir3_CMPS_S(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_NE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_ult:
		dst[0] = ir3_CMPS_U(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_LT;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_uge:
		dst[0] = ir3_CMPS_U(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_GE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;

	case nir_op_bcsel: {
		struct ir3_instruction *cond = ir3_b2n(b, src[0]);
		compile_assert(ctx, bs[1] == bs[2]);
		/* the boolean condition is 32b even if src[1] and src[2] are
		 * half-precision, but sel.b16 wants all three src's to be the
		 * same type.
		 */
		if (bs[1] < 32)
			cond = ir3_COV(b, cond, TYPE_U32, TYPE_U16);
		dst[0] = ir3_SEL_B32(b, src[1], 0, cond, 0, src[2], 0);
		break;
	}
	case nir_op_bit_count:
		dst[0] = ir3_CBITS_B(b, src[0], 0);
		break;
	case nir_op_ifind_msb: {
		struct ir3_instruction *cmp;
		dst[0] = ir3_CLZ_S(b, src[0], 0);
		cmp = ir3_CMPS_S(b, dst[0], 0, create_immed(b, 0), 0);
		cmp->cat2.condition = IR3_COND_GE;
		dst[0] = ir3_SEL_B32(b,
				ir3_SUB_U(b, create_immed(b, 31), 0, dst[0], 0), 0,
				cmp, 0, dst[0], 0);
		break;
	}
	case nir_op_ufind_msb:
		dst[0] = ir3_CLZ_B(b, src[0], 0);
		dst[0] = ir3_SEL_B32(b,
				ir3_SUB_U(b, create_immed(b, 31), 0, dst[0], 0), 0,
				src[0], 0, dst[0], 0);
		break;
	case nir_op_find_lsb:
		dst[0] = ir3_BFREV_B(b, src[0], 0);
		dst[0] = ir3_CLZ_B(b, dst[0], 0);
		break;
	case nir_op_bitfield_reverse:
		dst[0] = ir3_BFREV_B(b, src[0], 0);
		break;

	default:
		ir3_context_error(ctx, "Unhandled ALU op: %s\n",
				nir_op_infos[alu->op].name);
		break;
	}

	put_dst(ctx, &alu->dest.dest);
}

/* handles direct/indirect UBO reads: */
static void
emit_intrinsic_load_ubo(struct ir3_context *ctx, nir_intrinsic_instr *intr,
		struct ir3_instruction **dst)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *base_lo, *base_hi, *addr, *src0, *src1;
	nir_const_value *const_offset;
	/* UBO addresses are the first driver params: */
	unsigned ubo = regid(ctx->so->constbase.ubo, 0);
	const unsigned ptrsz = ir3_pointer_size(ctx);

	int off = 0;

	/* First src is ubo index, which could either be an immed or not: */
	src0 = ir3_get_src(ctx, &intr->src[0])[0];
	if (is_same_type_mov(src0) &&
			(src0->regs[1]->flags & IR3_REG_IMMED)) {
		base_lo = create_uniform(b, ubo + (src0->regs[1]->iim_val * ptrsz));
		base_hi = create_uniform(b, ubo + (src0->regs[1]->iim_val * ptrsz) + 1);
	} else {
		base_lo = create_uniform_indirect(b, ubo, ir3_get_addr(ctx, src0, 4));
		base_hi = create_uniform_indirect(b, ubo + 1, ir3_get_addr(ctx, src0, 4));
	}

	/* note: on 32bit gpu's base_hi is ignored and DCE'd */
	addr = base_lo;

	const_offset = nir_src_as_const_value(intr->src[1]);
	if (const_offset) {
		off += const_offset->u32[0];
	} else {
		/* For load_ubo_indirect, second src is indirect offset: */
		src1 = ir3_get_src(ctx, &intr->src[1])[0];

		/* and add offset to addr: */
		addr = ir3_ADD_S(b, addr, 0, src1, 0);
	}

	/* if offset is to large to encode in the ldg, split it out: */
	if ((off + (intr->num_components * 4)) > 1024) {
		/* split out the minimal amount to improve the odds that
		 * cp can fit the immediate in the add.s instruction:
		 */
		unsigned off2 = off + (intr->num_components * 4) - 1024;
		addr = ir3_ADD_S(b, addr, 0, create_immed(b, off2), 0);
		off -= off2;
	}

	if (ptrsz == 2) {
		struct ir3_instruction *carry;

		/* handle 32b rollover, ie:
		 *   if (addr < base_lo)
		 *      base_hi++
		 */
		carry = ir3_CMPS_U(b, addr, 0, base_lo, 0);
		carry->cat2.condition = IR3_COND_LT;
		base_hi = ir3_ADD_S(b, base_hi, 0, carry, 0);

		addr = ir3_create_collect(ctx, (struct ir3_instruction*[]){ addr, base_hi }, 2);
	}

	for (int i = 0; i < intr->num_components; i++) {
		struct ir3_instruction *load =
				ir3_LDG(b, addr, 0, create_immed(b, 1), 0);
		load->cat6.type = TYPE_U32;
		load->cat6.src_offset = off + i * 4;     /* byte offset */
		dst[i] = load;
	}
}

/* src[] = { buffer_index, offset }. No const_index */
static void
emit_intrinsic_load_ssbo(struct ir3_context *ctx, nir_intrinsic_instr *intr,
		struct ir3_instruction **dst)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *ldgb, *src0, *src1, *offset;
	nir_const_value *const_offset;

	/* can this be non-const buffer_index?  how do we handle that? */
	const_offset = nir_src_as_const_value(intr->src[0]);
	compile_assert(ctx, const_offset);

	offset = ir3_get_src(ctx, &intr->src[1])[0];

	/* src0 is uvec2(offset*4, 0), src1 is offset.. nir already *= 4: */
	src0 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
		offset,
		create_immed(b, 0),
	}, 2);
	src1 = ir3_SHR_B(b, offset, 0, create_immed(b, 2), 0);

	ldgb = ir3_LDGB(b, create_immed(b, const_offset->u32[0]), 0,
			src0, 0, src1, 0);
	ldgb->regs[0]->wrmask = MASK(intr->num_components);
	ldgb->cat6.iim_val = intr->num_components;
	ldgb->cat6.d = 4;
	ldgb->cat6.type = TYPE_U32;
	ldgb->barrier_class = IR3_BARRIER_BUFFER_R;
	ldgb->barrier_conflict = IR3_BARRIER_BUFFER_W;

	ir3_split_dest(b, dst, ldgb, 0, intr->num_components);
}

/* src[] = { value, block_index, offset }. const_index[] = { write_mask } */
static void
emit_intrinsic_store_ssbo(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *stgb, *src0, *src1, *src2, *offset;
	nir_const_value *const_offset;
	/* TODO handle wrmask properly, see _store_shared().. but I think
	 * it is more a PITA than that, since blob ends up loading the
	 * masked components and writing them back out.
	 */
	unsigned wrmask = intr->const_index[0];
	unsigned ncomp = ffs(~wrmask) - 1;

	/* can this be non-const buffer_index?  how do we handle that? */
	const_offset = nir_src_as_const_value(intr->src[1]);
	compile_assert(ctx, const_offset);

	offset = ir3_get_src(ctx, &intr->src[2])[0];

	/* src0 is value, src1 is offset, src2 is uvec2(offset*4, 0)..
	 * nir already *= 4:
	 */
	src0 = ir3_create_collect(ctx, ir3_get_src(ctx, &intr->src[0]), ncomp);
	src1 = ir3_SHR_B(b, offset, 0, create_immed(b, 2), 0);
	src2 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
		offset,
		create_immed(b, 0),
	}, 2);

	stgb = ir3_STGB(b, create_immed(b, const_offset->u32[0]), 0,
			src0, 0, src1, 0, src2, 0);
	stgb->cat6.iim_val = ncomp;
	stgb->cat6.d = 4;
	stgb->cat6.type = TYPE_U32;
	stgb->barrier_class = IR3_BARRIER_BUFFER_W;
	stgb->barrier_conflict = IR3_BARRIER_BUFFER_R | IR3_BARRIER_BUFFER_W;

	array_insert(b, b->keeps, stgb);
}

/* src[] = { block_index } */
static void
emit_intrinsic_ssbo_size(struct ir3_context *ctx, nir_intrinsic_instr *intr,
		struct ir3_instruction **dst)
{
	/* SSBO size stored as a const starting at ssbo_sizes: */
	unsigned blk_idx = nir_src_as_const_value(intr->src[0])->u32[0];
	unsigned idx = regid(ctx->so->constbase.ssbo_sizes, 0) +
		ctx->so->const_layout.ssbo_size.off[blk_idx];

	debug_assert(ctx->so->const_layout.ssbo_size.mask & (1 << blk_idx));

	dst[0] = create_uniform(ctx->block, idx);
}

/*
 * SSBO atomic intrinsics
 *
 * All of the SSBO atomic memory operations read a value from memory,
 * compute a new value using one of the operations below, write the new
 * value to memory, and return the original value read.
 *
 * All operations take 3 sources except CompSwap that takes 4. These
 * sources represent:
 *
 * 0: The SSBO buffer index.
 * 1: The offset into the SSBO buffer of the variable that the atomic
 *    operation will operate on.
 * 2: The data parameter to the atomic function (i.e. the value to add
 *    in ssbo_atomic_add, etc).
 * 3: For CompSwap only: the second data parameter.
 */
static struct ir3_instruction *
emit_intrinsic_atomic_ssbo(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *atomic, *ssbo, *src0, *src1, *src2, *offset;
	nir_const_value *const_offset;
	type_t type = TYPE_U32;

	/* can this be non-const buffer_index?  how do we handle that? */
	const_offset = nir_src_as_const_value(intr->src[0]);
	compile_assert(ctx, const_offset);
	ssbo = create_immed(b, const_offset->u32[0]);

	offset = ir3_get_src(ctx, &intr->src[1])[0];

	/* src0 is data (or uvec2(data, compare))
	 * src1 is offset
	 * src2 is uvec2(offset*4, 0) (appears to be 64b byte offset)
	 *
	 * Note that nir already multiplies the offset by four
	 */
	src0 = ir3_get_src(ctx, &intr->src[2])[0];
	src1 = ir3_SHR_B(b, offset, 0, create_immed(b, 2), 0);
	src2 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
		offset,
		create_immed(b, 0),
	}, 2);

	switch (intr->intrinsic) {
	case nir_intrinsic_ssbo_atomic_add:
		atomic = ir3_ATOMIC_ADD_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_imin:
		atomic = ir3_ATOMIC_MIN_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		type = TYPE_S32;
		break;
	case nir_intrinsic_ssbo_atomic_umin:
		atomic = ir3_ATOMIC_MIN_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_imax:
		atomic = ir3_ATOMIC_MAX_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		type = TYPE_S32;
		break;
	case nir_intrinsic_ssbo_atomic_umax:
		atomic = ir3_ATOMIC_MAX_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_and:
		atomic = ir3_ATOMIC_AND_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_or:
		atomic = ir3_ATOMIC_OR_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_xor:
		atomic = ir3_ATOMIC_XOR_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_exchange:
		atomic = ir3_ATOMIC_XCHG_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_comp_swap:
		/* for cmpxchg, src0 is [ui]vec2(data, compare): */
		src0 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
			ir3_get_src(ctx, &intr->src[3])[0],
			src0,
		}, 2);
		atomic = ir3_ATOMIC_CMPXCHG_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	default:
		unreachable("boo");
	}

	atomic->cat6.iim_val = 1;
	atomic->cat6.d = 4;
	atomic->cat6.type = type;
	atomic->barrier_class = IR3_BARRIER_BUFFER_W;
	atomic->barrier_conflict = IR3_BARRIER_BUFFER_R | IR3_BARRIER_BUFFER_W;

	/* even if nothing consume the result, we can't DCE the instruction: */
	array_insert(b, b->keeps, atomic);

	return atomic;
}

/* src[] = { offset }. const_index[] = { base } */
static void
emit_intrinsic_load_shared(struct ir3_context *ctx, nir_intrinsic_instr *intr,
		struct ir3_instruction **dst)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *ldl, *offset;
	unsigned base;

	offset = ir3_get_src(ctx, &intr->src[0])[0];
	base   = nir_intrinsic_base(intr);

	ldl = ir3_LDL(b, offset, 0, create_immed(b, intr->num_components), 0);
	ldl->cat6.src_offset = base;
	ldl->cat6.type = utype_dst(intr->dest);
	ldl->regs[0]->wrmask = MASK(intr->num_components);

	ldl->barrier_class = IR3_BARRIER_SHARED_R;
	ldl->barrier_conflict = IR3_BARRIER_SHARED_W;

	ir3_split_dest(b, dst, ldl, 0, intr->num_components);
}

/* src[] = { value, offset }. const_index[] = { base, write_mask } */
static void
emit_intrinsic_store_shared(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *stl, *offset;
	struct ir3_instruction * const *value;
	unsigned base, wrmask;

	value  = ir3_get_src(ctx, &intr->src[0]);
	offset = ir3_get_src(ctx, &intr->src[1])[0];

	base   = nir_intrinsic_base(intr);
	wrmask = nir_intrinsic_write_mask(intr);

	/* Combine groups of consecutive enabled channels in one write
	 * message. We use ffs to find the first enabled channel and then ffs on
	 * the bit-inverse, down-shifted writemask to determine the length of
	 * the block of enabled bits.
	 *
	 * (trick stolen from i965's fs_visitor::nir_emit_cs_intrinsic())
	 */
	while (wrmask) {
		unsigned first_component = ffs(wrmask) - 1;
		unsigned length = ffs(~(wrmask >> first_component)) - 1;

		stl = ir3_STL(b, offset, 0,
			ir3_create_collect(ctx, &value[first_component], length), 0,
			create_immed(b, length), 0);
		stl->cat6.dst_offset = first_component + base;
		stl->cat6.type = utype_src(intr->src[0]);
		stl->barrier_class = IR3_BARRIER_SHARED_W;
		stl->barrier_conflict = IR3_BARRIER_SHARED_R | IR3_BARRIER_SHARED_W;

		array_insert(b, b->keeps, stl);

		/* Clear the bits in the writemask that we just wrote, then try
		 * again to see if more channels are left.
		 */
		wrmask &= (15 << (first_component + length));
	}
}

/*
 * CS shared variable atomic intrinsics
 *
 * All of the shared variable atomic memory operations read a value from
 * memory, compute a new value using one of the operations below, write the
 * new value to memory, and return the original value read.
 *
 * All operations take 2 sources except CompSwap that takes 3. These
 * sources represent:
 *
 * 0: The offset into the shared variable storage region that the atomic
 *    operation will operate on.
 * 1: The data parameter to the atomic function (i.e. the value to add
 *    in shared_atomic_add, etc).
 * 2: For CompSwap only: the second data parameter.
 */
static struct ir3_instruction *
emit_intrinsic_atomic_shared(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *atomic, *src0, *src1;
	type_t type = TYPE_U32;

	src0 = ir3_get_src(ctx, &intr->src[0])[0];   /* offset */
	src1 = ir3_get_src(ctx, &intr->src[1])[0];   /* value */

	switch (intr->intrinsic) {
	case nir_intrinsic_shared_atomic_add:
		atomic = ir3_ATOMIC_ADD(b, src0, 0, src1, 0);
		break;
	case nir_intrinsic_shared_atomic_imin:
		atomic = ir3_ATOMIC_MIN(b, src0, 0, src1, 0);
		type = TYPE_S32;
		break;
	case nir_intrinsic_shared_atomic_umin:
		atomic = ir3_ATOMIC_MIN(b, src0, 0, src1, 0);
		break;
	case nir_intrinsic_shared_atomic_imax:
		atomic = ir3_ATOMIC_MAX(b, src0, 0, src1, 0);
		type = TYPE_S32;
		break;
	case nir_intrinsic_shared_atomic_umax:
		atomic = ir3_ATOMIC_MAX(b, src0, 0, src1, 0);
		break;
	case nir_intrinsic_shared_atomic_and:
		atomic = ir3_ATOMIC_AND(b, src0, 0, src1, 0);
		break;
	case nir_intrinsic_shared_atomic_or:
		atomic = ir3_ATOMIC_OR(b, src0, 0, src1, 0);
		break;
	case nir_intrinsic_shared_atomic_xor:
		atomic = ir3_ATOMIC_XOR(b, src0, 0, src1, 0);
		break;
	case nir_intrinsic_shared_atomic_exchange:
		atomic = ir3_ATOMIC_XCHG(b, src0, 0, src1, 0);
		break;
	case nir_intrinsic_shared_atomic_comp_swap:
		/* for cmpxchg, src1 is [ui]vec2(data, compare): */
		src1 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
			ir3_get_src(ctx, &intr->src[2])[0],
			src1,
		}, 2);
		atomic = ir3_ATOMIC_CMPXCHG(b, src0, 0, src1, 0);
		break;
	default:
		unreachable("boo");
	}

	atomic->cat6.iim_val = 1;
	atomic->cat6.d = 1;
	atomic->cat6.type = type;
	atomic->barrier_class = IR3_BARRIER_SHARED_W;
	atomic->barrier_conflict = IR3_BARRIER_SHARED_R | IR3_BARRIER_SHARED_W;

	/* even if nothing consume the result, we can't DCE the instruction: */
	array_insert(b, b->keeps, atomic);

	return atomic;
}

/* Images get mapped into SSBO/image state (for store/atomic) and texture
 * state block (for load).  To simplify things, invert the image id and
 * map it from end of state block, ie. image 0 becomes num-1, image 1
 * becomes num-2, etc.  This potentially avoids needing to re-emit texture
 * state when switching shaders.
 *
 * TODO is max # of samplers and SSBOs the same.  This shouldn't be hard-
 * coded.  Also, since all the gl shader stages (ie. everything but CS)
 * share the same SSBO/image state block, this might require some more
 * logic if we supported images in anything other than FS..
 */
static unsigned
get_image_slot(struct ir3_context *ctx, nir_deref_instr *deref)
{
	unsigned int loc = 0;
	unsigned inner_size = 1;

	while (deref->deref_type != nir_deref_type_var) {
		assert(deref->deref_type == nir_deref_type_array);
		nir_const_value *const_index = nir_src_as_const_value(deref->arr.index);
		assert(const_index);

		/* Go to the next instruction */
		deref = nir_deref_instr_parent(deref);

		assert(glsl_type_is_array(deref->type));
		const unsigned array_len = glsl_get_length(deref->type);
		loc += MIN2(const_index->u32[0], array_len - 1) * inner_size;

		/* Update the inner size */
		inner_size *= array_len;
	}

	loc += deref->var->data.driver_location;

	/* TODO figure out real limit per generation, and don't hardcode: */
	const unsigned max_samplers = 16;
	return max_samplers - loc - 1;
}

/* see tex_info() for equiv logic for texture instructions.. it would be
 * nice if this could be better unified..
 */
static unsigned
get_image_coords(const nir_variable *var, unsigned *flagsp)
{
	const struct glsl_type *type = glsl_without_array(var->type);
	unsigned coords, flags = 0;

	switch (glsl_get_sampler_dim(type)) {
	case GLSL_SAMPLER_DIM_1D:
	case GLSL_SAMPLER_DIM_BUF:
		coords = 1;
		break;
	case GLSL_SAMPLER_DIM_2D:
	case GLSL_SAMPLER_DIM_RECT:
	case GLSL_SAMPLER_DIM_EXTERNAL:
	case GLSL_SAMPLER_DIM_MS:
		coords = 2;
		break;
	case GLSL_SAMPLER_DIM_3D:
	case GLSL_SAMPLER_DIM_CUBE:
		flags |= IR3_INSTR_3D;
		coords = 3;
		break;
	default:
		unreachable("bad sampler dim");
		return 0;
	}

	if (glsl_sampler_type_is_array(type)) {
		/* note: unlike tex_info(), adjust # of coords to include array idx: */
		coords++;
		flags |= IR3_INSTR_A;
	}

	if (flagsp)
		*flagsp = flags;

	return coords;
}

static type_t
get_image_type(const nir_variable *var)
{
	switch (glsl_get_sampler_result_type(glsl_without_array(var->type))) {
	case GLSL_TYPE_UINT:
		return TYPE_U32;
	case GLSL_TYPE_INT:
		return TYPE_S32;
	case GLSL_TYPE_FLOAT:
		return TYPE_F32;
	default:
		unreachable("bad sampler type.");
		return 0;
	}
}

static struct ir3_instruction *
get_image_offset(struct ir3_context *ctx, const nir_variable *var,
		struct ir3_instruction * const *coords, bool byteoff)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *offset;
	unsigned ncoords = get_image_coords(var, NULL);

	/* to calculate the byte offset (yes, uggg) we need (up to) three
	 * const values to know the bytes per pixel, and y and z stride:
	 */
	unsigned cb = regid(ctx->so->constbase.image_dims, 0) +
		ctx->so->const_layout.image_dims.off[var->data.driver_location];

	debug_assert(ctx->so->const_layout.image_dims.mask &
			(1 << var->data.driver_location));

	/* offset = coords.x * bytes_per_pixel: */
	offset = ir3_MUL_S(b, coords[0], 0, create_uniform(b, cb + 0), 0);
	if (ncoords > 1) {
		/* offset += coords.y * y_pitch: */
		offset = ir3_MAD_S24(b, create_uniform(b, cb + 1), 0,
				coords[1], 0, offset, 0);
	}
	if (ncoords > 2) {
		/* offset += coords.z * z_pitch: */
		offset = ir3_MAD_S24(b, create_uniform(b, cb + 2), 0,
				coords[2], 0, offset, 0);
	}

	if (!byteoff) {
		/* Some cases, like atomics, seem to use dword offset instead
		 * of byte offsets.. blob just puts an extra shr.b in there
		 * in those cases:
		 */
		offset = ir3_SHR_B(b, offset, 0, create_immed(b, 2), 0);
	}

	return ir3_create_collect(ctx, (struct ir3_instruction*[]){
		offset,
		create_immed(b, 0),
	}, 2);
}

/* src[] = { deref, coord, sample_index }. const_index[] = {} */
static void
emit_intrinsic_load_image(struct ir3_context *ctx, nir_intrinsic_instr *intr,
		struct ir3_instruction **dst)
{
	struct ir3_block *b = ctx->block;
	const nir_variable *var = nir_intrinsic_get_var(intr, 0);
	struct ir3_instruction *sam;
	struct ir3_instruction * const *src0 = ir3_get_src(ctx, &intr->src[1]);
	struct ir3_instruction *coords[4];
	unsigned flags, ncoords = get_image_coords(var, &flags);
	unsigned tex_idx = get_image_slot(ctx, nir_src_as_deref(intr->src[0]));
	type_t type = get_image_type(var);

	/* hmm, this seems a bit odd, but it is what blob does and (at least
	 * a5xx) just faults on bogus addresses otherwise:
	 */
	if (flags & IR3_INSTR_3D) {
		flags &= ~IR3_INSTR_3D;
		flags |= IR3_INSTR_A;
	}

	for (unsigned i = 0; i < ncoords; i++)
		coords[i] = src0[i];

	if (ncoords == 1)
		coords[ncoords++] = create_immed(b, 0);

	sam = ir3_SAM(b, OPC_ISAM, type, 0b1111, flags,
			tex_idx, tex_idx, ir3_create_collect(ctx, coords, ncoords), NULL);

	sam->barrier_class = IR3_BARRIER_IMAGE_R;
	sam->barrier_conflict = IR3_BARRIER_IMAGE_W;

	ir3_split_dest(b, dst, sam, 0, 4);
}

/* src[] = { deref, coord, sample_index, value }. const_index[] = {} */
static void
emit_intrinsic_store_image(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	const nir_variable *var = nir_intrinsic_get_var(intr, 0);
	struct ir3_instruction *stib, *offset;
	struct ir3_instruction * const *value = ir3_get_src(ctx, &intr->src[3]);
	struct ir3_instruction * const *coords = ir3_get_src(ctx, &intr->src[1]);
	unsigned ncoords = get_image_coords(var, NULL);
	unsigned tex_idx = get_image_slot(ctx, nir_src_as_deref(intr->src[0]));

	/* src0 is value
	 * src1 is coords
	 * src2 is 64b byte offset
	 */

	offset = get_image_offset(ctx, var, coords, true);

	/* NOTE: stib seems to take byte offset, but stgb.typed can be used
	 * too and takes a dword offset.. not quite sure yet why blob uses
	 * one over the other in various cases.
	 */

	stib = ir3_STIB(b, create_immed(b, tex_idx), 0,
			ir3_create_collect(ctx, value, 4), 0,
			ir3_create_collect(ctx, coords, ncoords), 0,
			offset, 0);
	stib->cat6.iim_val = 4;
	stib->cat6.d = ncoords;
	stib->cat6.type = get_image_type(var);
	stib->cat6.typed = true;
	stib->barrier_class = IR3_BARRIER_IMAGE_W;
	stib->barrier_conflict = IR3_BARRIER_IMAGE_R | IR3_BARRIER_IMAGE_W;

	array_insert(b, b->keeps, stib);
}

static void
emit_intrinsic_image_size(struct ir3_context *ctx, nir_intrinsic_instr *intr,
		struct ir3_instruction **dst)
{
	struct ir3_block *b = ctx->block;
	const nir_variable *var = nir_intrinsic_get_var(intr, 0);
	unsigned tex_idx = get_image_slot(ctx, nir_src_as_deref(intr->src[0]));
	struct ir3_instruction *sam, *lod;
	unsigned flags, ncoords = get_image_coords(var, &flags);

	lod = create_immed(b, 0);
	sam = ir3_SAM(b, OPC_GETSIZE, TYPE_U32, 0b1111, flags,
			tex_idx, tex_idx, lod, NULL);

	/* Array size actually ends up in .w rather than .z. This doesn't
	 * matter for miplevel 0, but for higher mips the value in z is
	 * minified whereas w stays. Also, the value in TEX_CONST_3_DEPTH is
	 * returned, which means that we have to add 1 to it for arrays for
	 * a3xx.
	 *
	 * Note use a temporary dst and then copy, since the size of the dst
	 * array that is passed in is based on nir's understanding of the
	 * result size, not the hardware's
	 */
	struct ir3_instruction *tmp[4];

	ir3_split_dest(b, tmp, sam, 0, 4);

	/* get_size instruction returns size in bytes instead of texels
	 * for imageBuffer, so we need to divide it by the pixel size
	 * of the image format.
	 *
	 * TODO: This is at least true on a5xx. Check other gens.
	 */
	enum glsl_sampler_dim dim =
		glsl_get_sampler_dim(glsl_without_array(var->type));
	if (dim == GLSL_SAMPLER_DIM_BUF) {
		/* Since all the possible values the divisor can take are
		 * power-of-two (4, 8, or 16), the division is implemented
		 * as a shift-right.
		 * During shader setup, the log2 of the image format's
		 * bytes-per-pixel should have been emitted in 2nd slot of
		 * image_dims. See ir3_shader::emit_image_dims().
		 */
		unsigned cb = regid(ctx->so->constbase.image_dims, 0) +
			ctx->so->const_layout.image_dims.off[var->data.driver_location];
		struct ir3_instruction *aux = create_uniform(b, cb + 1);

		tmp[0] = ir3_SHR_B(b, tmp[0], 0, aux, 0);
	}

	for (unsigned i = 0; i < ncoords; i++)
		dst[i] = tmp[i];

	if (flags & IR3_INSTR_A) {
		if (ctx->compiler->levels_add_one) {
			dst[ncoords-1] = ir3_ADD_U(b, tmp[3], 0, create_immed(b, 1), 0);
		} else {
			dst[ncoords-1] = ir3_MOV(b, tmp[3], TYPE_U32);
		}
	}
}

/* src[] = { deref, coord, sample_index, value, compare }. const_index[] = {} */
static struct ir3_instruction *
emit_intrinsic_atomic_image(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	const nir_variable *var = nir_intrinsic_get_var(intr, 0);
	struct ir3_instruction *atomic, *image, *src0, *src1, *src2;
	struct ir3_instruction * const *coords = ir3_get_src(ctx, &intr->src[1]);
	unsigned ncoords = get_image_coords(var, NULL);

	image = create_immed(b, get_image_slot(ctx, nir_src_as_deref(intr->src[0])));

	/* src0 is value (or uvec2(value, compare))
	 * src1 is coords
	 * src2 is 64b byte offset
	 */
	src0 = ir3_get_src(ctx, &intr->src[3])[0];
	src1 = ir3_create_collect(ctx, coords, ncoords);
	src2 = get_image_offset(ctx, var, coords, false);

	switch (intr->intrinsic) {
	case nir_intrinsic_image_deref_atomic_add:
		atomic = ir3_ATOMIC_ADD_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_deref_atomic_min:
		atomic = ir3_ATOMIC_MIN_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_deref_atomic_max:
		atomic = ir3_ATOMIC_MAX_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_deref_atomic_and:
		atomic = ir3_ATOMIC_AND_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_deref_atomic_or:
		atomic = ir3_ATOMIC_OR_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_deref_atomic_xor:
		atomic = ir3_ATOMIC_XOR_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_deref_atomic_exchange:
		atomic = ir3_ATOMIC_XCHG_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_deref_atomic_comp_swap:
		/* for cmpxchg, src0 is [ui]vec2(data, compare): */
		src0 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
			ir3_get_src(ctx, &intr->src[4])[0],
			src0,
		}, 2);
		atomic = ir3_ATOMIC_CMPXCHG_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	default:
		unreachable("boo");
	}

	atomic->cat6.iim_val = 1;
	atomic->cat6.d = ncoords;
	atomic->cat6.type = get_image_type(var);
	atomic->cat6.typed = true;
	atomic->barrier_class = IR3_BARRIER_IMAGE_W;
	atomic->barrier_conflict = IR3_BARRIER_IMAGE_R | IR3_BARRIER_IMAGE_W;

	/* even if nothing consume the result, we can't DCE the instruction: */
	array_insert(b, b->keeps, atomic);

	return atomic;
}

static void
emit_intrinsic_barrier(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *barrier;

	switch (intr->intrinsic) {
	case nir_intrinsic_barrier:
		barrier = ir3_BAR(b);
		barrier->cat7.g = true;
		barrier->cat7.l = true;
		barrier->flags = IR3_INSTR_SS | IR3_INSTR_SY;
		barrier->barrier_class = IR3_BARRIER_EVERYTHING;
		break;
	case nir_intrinsic_memory_barrier:
		barrier = ir3_FENCE(b);
		barrier->cat7.g = true;
		barrier->cat7.r = true;
		barrier->cat7.w = true;
		barrier->barrier_class = IR3_BARRIER_IMAGE_W |
				IR3_BARRIER_BUFFER_W;
		barrier->barrier_conflict =
				IR3_BARRIER_IMAGE_R | IR3_BARRIER_IMAGE_W |
				IR3_BARRIER_BUFFER_R | IR3_BARRIER_BUFFER_W;
		break;
	case nir_intrinsic_memory_barrier_atomic_counter:
	case nir_intrinsic_memory_barrier_buffer:
		barrier = ir3_FENCE(b);
		barrier->cat7.g = true;
		barrier->cat7.r = true;
		barrier->cat7.w = true;
		barrier->barrier_class = IR3_BARRIER_BUFFER_W;
		barrier->barrier_conflict = IR3_BARRIER_BUFFER_R |
				IR3_BARRIER_BUFFER_W;
		break;
	case nir_intrinsic_memory_barrier_image:
		// TODO double check if this should have .g set
		barrier = ir3_FENCE(b);
		barrier->cat7.g = true;
		barrier->cat7.r = true;
		barrier->cat7.w = true;
		barrier->barrier_class = IR3_BARRIER_IMAGE_W;
		barrier->barrier_conflict = IR3_BARRIER_IMAGE_R |
				IR3_BARRIER_IMAGE_W;
		break;
	case nir_intrinsic_memory_barrier_shared:
		barrier = ir3_FENCE(b);
		barrier->cat7.g = true;
		barrier->cat7.l = true;
		barrier->cat7.r = true;
		barrier->cat7.w = true;
		barrier->barrier_class = IR3_BARRIER_SHARED_W;
		barrier->barrier_conflict = IR3_BARRIER_SHARED_R |
				IR3_BARRIER_SHARED_W;
		break;
	case nir_intrinsic_group_memory_barrier:
		barrier = ir3_FENCE(b);
		barrier->cat7.g = true;
		barrier->cat7.l = true;
		barrier->cat7.r = true;
		barrier->cat7.w = true;
		barrier->barrier_class = IR3_BARRIER_SHARED_W |
				IR3_BARRIER_IMAGE_W |
				IR3_BARRIER_BUFFER_W;
		barrier->barrier_conflict =
				IR3_BARRIER_SHARED_R | IR3_BARRIER_SHARED_W |
				IR3_BARRIER_IMAGE_R | IR3_BARRIER_IMAGE_W |
				IR3_BARRIER_BUFFER_R | IR3_BARRIER_BUFFER_W;
		break;
	default:
		unreachable("boo");
	}

	/* make sure barrier doesn't get DCE'd */
	array_insert(b, b->keeps, barrier);
}

static void add_sysval_input_compmask(struct ir3_context *ctx,
		gl_system_value slot, unsigned compmask,
		struct ir3_instruction *instr)
{
	struct ir3_shader_variant *so = ctx->so;
	unsigned r = regid(so->inputs_count, 0);
	unsigned n = so->inputs_count++;

	so->inputs[n].sysval = true;
	so->inputs[n].slot = slot;
	so->inputs[n].compmask = compmask;
	so->inputs[n].regid = r;
	so->inputs[n].interpolate = INTERP_MODE_FLAT;
	so->total_in++;

	ctx->ir->ninputs = MAX2(ctx->ir->ninputs, r + 1);
	ctx->ir->inputs[r] = instr;
}

static void add_sysval_input(struct ir3_context *ctx, gl_system_value slot,
		struct ir3_instruction *instr)
{
	add_sysval_input_compmask(ctx, slot, 0x1, instr);
}

static void
emit_intrinsic(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	const nir_intrinsic_info *info = &nir_intrinsic_infos[intr->intrinsic];
	struct ir3_instruction **dst;
	struct ir3_instruction * const *src;
	struct ir3_block *b = ctx->block;
	nir_const_value *const_offset;
	int idx, comp;

	if (info->has_dest) {
		unsigned n = nir_intrinsic_dest_components(intr);
		dst = ir3_get_dst(ctx, &intr->dest, n);
	} else {
		dst = NULL;
	}

	switch (intr->intrinsic) {
	case nir_intrinsic_load_uniform:
		idx = nir_intrinsic_base(intr);
		const_offset = nir_src_as_const_value(intr->src[0]);
		if (const_offset) {
			idx += const_offset->u32[0];
			for (int i = 0; i < intr->num_components; i++) {
				unsigned n = idx * 4 + i;
				dst[i] = create_uniform(b, n);
			}
		} else {
			src = ir3_get_src(ctx, &intr->src[0]);
			for (int i = 0; i < intr->num_components; i++) {
				int n = idx * 4 + i;
				dst[i] = create_uniform_indirect(b, n,
						ir3_get_addr(ctx, src[0], 4));
			}
			/* NOTE: if relative addressing is used, we set
			 * constlen in the compiler (to worst-case value)
			 * since we don't know in the assembler what the max
			 * addr reg value can be:
			 */
			ctx->so->constlen = ctx->s->num_uniforms;
		}
		break;
	case nir_intrinsic_load_ubo:
		emit_intrinsic_load_ubo(ctx, intr, dst);
		break;
	case nir_intrinsic_load_input:
		idx = nir_intrinsic_base(intr);
		comp = nir_intrinsic_component(intr);
		const_offset = nir_src_as_const_value(intr->src[0]);
		if (const_offset) {
			idx += const_offset->u32[0];
			for (int i = 0; i < intr->num_components; i++) {
				unsigned n = idx * 4 + i + comp;
				dst[i] = ctx->ir->inputs[n];
			}
		} else {
			src = ir3_get_src(ctx, &intr->src[0]);
			struct ir3_instruction *collect =
					ir3_create_collect(ctx, ctx->ir->inputs, ctx->ir->ninputs);
			struct ir3_instruction *addr = ir3_get_addr(ctx, src[0], 4);
			for (int i = 0; i < intr->num_components; i++) {
				unsigned n = idx * 4 + i + comp;
				dst[i] = create_indirect_load(ctx, ctx->ir->ninputs,
						n, addr, collect);
			}
		}
		break;
	case nir_intrinsic_load_ssbo:
		emit_intrinsic_load_ssbo(ctx, intr, dst);
		break;
	case nir_intrinsic_store_ssbo:
		emit_intrinsic_store_ssbo(ctx, intr);
		break;
	case nir_intrinsic_get_buffer_size:
		emit_intrinsic_ssbo_size(ctx, intr, dst);
		break;
	case nir_intrinsic_ssbo_atomic_add:
	case nir_intrinsic_ssbo_atomic_imin:
	case nir_intrinsic_ssbo_atomic_umin:
	case nir_intrinsic_ssbo_atomic_imax:
	case nir_intrinsic_ssbo_atomic_umax:
	case nir_intrinsic_ssbo_atomic_and:
	case nir_intrinsic_ssbo_atomic_or:
	case nir_intrinsic_ssbo_atomic_xor:
	case nir_intrinsic_ssbo_atomic_exchange:
	case nir_intrinsic_ssbo_atomic_comp_swap:
		dst[0] = emit_intrinsic_atomic_ssbo(ctx, intr);
		break;
	case nir_intrinsic_load_shared:
		emit_intrinsic_load_shared(ctx, intr, dst);
		break;
	case nir_intrinsic_store_shared:
		emit_intrinsic_store_shared(ctx, intr);
		break;
	case nir_intrinsic_shared_atomic_add:
	case nir_intrinsic_shared_atomic_imin:
	case nir_intrinsic_shared_atomic_umin:
	case nir_intrinsic_shared_atomic_imax:
	case nir_intrinsic_shared_atomic_umax:
	case nir_intrinsic_shared_atomic_and:
	case nir_intrinsic_shared_atomic_or:
	case nir_intrinsic_shared_atomic_xor:
	case nir_intrinsic_shared_atomic_exchange:
	case nir_intrinsic_shared_atomic_comp_swap:
		dst[0] = emit_intrinsic_atomic_shared(ctx, intr);
		break;
	case nir_intrinsic_image_deref_load:
		emit_intrinsic_load_image(ctx, intr, dst);
		break;
	case nir_intrinsic_image_deref_store:
		emit_intrinsic_store_image(ctx, intr);
		break;
	case nir_intrinsic_image_deref_size:
		emit_intrinsic_image_size(ctx, intr, dst);
		break;
	case nir_intrinsic_image_deref_atomic_add:
	case nir_intrinsic_image_deref_atomic_min:
	case nir_intrinsic_image_deref_atomic_max:
	case nir_intrinsic_image_deref_atomic_and:
	case nir_intrinsic_image_deref_atomic_or:
	case nir_intrinsic_image_deref_atomic_xor:
	case nir_intrinsic_image_deref_atomic_exchange:
	case nir_intrinsic_image_deref_atomic_comp_swap:
		dst[0] = emit_intrinsic_atomic_image(ctx, intr);
		break;
	case nir_intrinsic_barrier:
	case nir_intrinsic_memory_barrier:
	case nir_intrinsic_group_memory_barrier:
	case nir_intrinsic_memory_barrier_atomic_counter:
	case nir_intrinsic_memory_barrier_buffer:
	case nir_intrinsic_memory_barrier_image:
	case nir_intrinsic_memory_barrier_shared:
		emit_intrinsic_barrier(ctx, intr);
		/* note that blk ptr no longer valid, make that obvious: */
		b = NULL;
		break;
	case nir_intrinsic_store_output:
		idx = nir_intrinsic_base(intr);
		comp = nir_intrinsic_component(intr);
		const_offset = nir_src_as_const_value(intr->src[1]);
		compile_assert(ctx, const_offset != NULL);
		idx += const_offset->u32[0];

		src = ir3_get_src(ctx, &intr->src[0]);
		for (int i = 0; i < intr->num_components; i++) {
			unsigned n = idx * 4 + i + comp;
			ctx->ir->outputs[n] = src[i];
		}
		break;
	case nir_intrinsic_load_base_vertex:
	case nir_intrinsic_load_first_vertex:
		if (!ctx->basevertex) {
			ctx->basevertex = create_driver_param(ctx, IR3_DP_VTXID_BASE);
			add_sysval_input(ctx, SYSTEM_VALUE_FIRST_VERTEX, ctx->basevertex);
		}
		dst[0] = ctx->basevertex;
		break;
	case nir_intrinsic_load_vertex_id_zero_base:
	case nir_intrinsic_load_vertex_id:
		if (!ctx->vertex_id) {
			gl_system_value sv = (intr->intrinsic == nir_intrinsic_load_vertex_id) ?
				SYSTEM_VALUE_VERTEX_ID : SYSTEM_VALUE_VERTEX_ID_ZERO_BASE;
			ctx->vertex_id = create_input(ctx, 0);
			add_sysval_input(ctx, sv, ctx->vertex_id);
		}
		dst[0] = ctx->vertex_id;
		break;
	case nir_intrinsic_load_instance_id:
		if (!ctx->instance_id) {
			ctx->instance_id = create_input(ctx, 0);
			add_sysval_input(ctx, SYSTEM_VALUE_INSTANCE_ID,
					ctx->instance_id);
		}
		dst[0] = ctx->instance_id;
		break;
	case nir_intrinsic_load_sample_id:
	case nir_intrinsic_load_sample_id_no_per_sample:
		if (!ctx->samp_id) {
			ctx->samp_id = create_input(ctx, 0);
			ctx->samp_id->regs[0]->flags |= IR3_REG_HALF;
			add_sysval_input(ctx, SYSTEM_VALUE_SAMPLE_ID,
					ctx->samp_id);
		}
		dst[0] = ir3_COV(b, ctx->samp_id, TYPE_U16, TYPE_U32);
		break;
	case nir_intrinsic_load_sample_mask_in:
		if (!ctx->samp_mask_in) {
			ctx->samp_mask_in = create_input(ctx, 0);
			add_sysval_input(ctx, SYSTEM_VALUE_SAMPLE_MASK_IN,
					ctx->samp_mask_in);
		}
		dst[0] = ctx->samp_mask_in;
		break;
	case nir_intrinsic_load_user_clip_plane:
		idx = nir_intrinsic_ucp_id(intr);
		for (int i = 0; i < intr->num_components; i++) {
			unsigned n = idx * 4 + i;
			dst[i] = create_driver_param(ctx, IR3_DP_UCP0_X + n);
		}
		break;
	case nir_intrinsic_load_front_face:
		if (!ctx->frag_face) {
			ctx->so->frag_face = true;
			ctx->frag_face = create_input(ctx, 0);
			add_sysval_input(ctx, SYSTEM_VALUE_FRONT_FACE, ctx->frag_face);
			ctx->frag_face->regs[0]->flags |= IR3_REG_HALF;
		}
		/* for fragface, we get -1 for back and 0 for front. However this is
		 * the inverse of what nir expects (where ~0 is true).
		 */
		dst[0] = ir3_COV(b, ctx->frag_face, TYPE_S16, TYPE_S32);
		dst[0] = ir3_NOT_B(b, dst[0], 0);
		break;
	case nir_intrinsic_load_local_invocation_id:
		if (!ctx->local_invocation_id) {
			ctx->local_invocation_id = create_input_compmask(ctx, 0, 0x7);
			add_sysval_input_compmask(ctx, SYSTEM_VALUE_LOCAL_INVOCATION_ID,
					0x7, ctx->local_invocation_id);
		}
		ir3_split_dest(b, dst, ctx->local_invocation_id, 0, 3);
		break;
	case nir_intrinsic_load_work_group_id:
		if (!ctx->work_group_id) {
			ctx->work_group_id = create_input_compmask(ctx, 0, 0x7);
			add_sysval_input_compmask(ctx, SYSTEM_VALUE_WORK_GROUP_ID,
					0x7, ctx->work_group_id);
			ctx->work_group_id->regs[0]->flags |= IR3_REG_HIGH;
		}
		ir3_split_dest(b, dst, ctx->work_group_id, 0, 3);
		break;
	case nir_intrinsic_load_num_work_groups:
		for (int i = 0; i < intr->num_components; i++) {
			dst[i] = create_driver_param(ctx, IR3_DP_NUM_WORK_GROUPS_X + i);
		}
		break;
	case nir_intrinsic_load_local_group_size:
		for (int i = 0; i < intr->num_components; i++) {
			dst[i] = create_driver_param(ctx, IR3_DP_LOCAL_GROUP_SIZE_X + i);
		}
		break;
	case nir_intrinsic_discard_if:
	case nir_intrinsic_discard: {
		struct ir3_instruction *cond, *kill;

		if (intr->intrinsic == nir_intrinsic_discard_if) {
			/* conditional discard: */
			src = ir3_get_src(ctx, &intr->src[0]);
			cond = ir3_b2n(b, src[0]);
		} else {
			/* unconditional discard: */
			cond = create_immed(b, 1);
		}

		/* NOTE: only cmps.*.* can write p0.x: */
		cond = ir3_CMPS_S(b, cond, 0, create_immed(b, 0), 0);
		cond->cat2.condition = IR3_COND_NE;

		/* condition always goes in predicate register: */
		cond->regs[0]->num = regid(REG_P0, 0);

		kill = ir3_KILL(b, cond, 0);
		array_insert(ctx->ir, ctx->ir->predicates, kill);

		array_insert(b, b->keeps, kill);
		ctx->so->has_kill = true;

		break;
	}
	default:
		ir3_context_error(ctx, "Unhandled intrinsic type: %s\n",
				nir_intrinsic_infos[intr->intrinsic].name);
		break;
	}

	if (info->has_dest)
		put_dst(ctx, &intr->dest);
}

static void
emit_load_const(struct ir3_context *ctx, nir_load_const_instr *instr)
{
	struct ir3_instruction **dst = ir3_get_dst_ssa(ctx, &instr->def,
			instr->def.num_components);
	type_t type = (instr->def.bit_size < 32) ? TYPE_U16 : TYPE_U32;

	for (int i = 0; i < instr->def.num_components; i++)
		dst[i] = create_immed_typed(ctx->block, instr->value.u32[i], type);
}

static void
emit_undef(struct ir3_context *ctx, nir_ssa_undef_instr *undef)
{
	struct ir3_instruction **dst = ir3_get_dst_ssa(ctx, &undef->def,
			undef->def.num_components);
	type_t type = (undef->def.bit_size < 32) ? TYPE_U16 : TYPE_U32;

	/* backend doesn't want undefined instructions, so just plug
	 * in 0.0..
	 */
	for (int i = 0; i < undef->def.num_components; i++)
		dst[i] = create_immed_typed(ctx->block, fui(0.0), type);
}

/*
 * texture fetch/sample instructions:
 */

static void
tex_info(nir_tex_instr *tex, unsigned *flagsp, unsigned *coordsp)
{
	unsigned coords, flags = 0;

	/* note: would use tex->coord_components.. except txs.. also,
	 * since array index goes after shadow ref, we don't want to
	 * count it:
	 */
	switch (tex->sampler_dim) {
	case GLSL_SAMPLER_DIM_1D:
	case GLSL_SAMPLER_DIM_BUF:
		coords = 1;
		break;
	case GLSL_SAMPLER_DIM_2D:
	case GLSL_SAMPLER_DIM_RECT:
	case GLSL_SAMPLER_DIM_EXTERNAL:
	case GLSL_SAMPLER_DIM_MS:
		coords = 2;
		break;
	case GLSL_SAMPLER_DIM_3D:
	case GLSL_SAMPLER_DIM_CUBE:
		coords = 3;
		flags |= IR3_INSTR_3D;
		break;
	default:
		unreachable("bad sampler_dim");
	}

	if (tex->is_shadow && tex->op != nir_texop_lod)
		flags |= IR3_INSTR_S;

	if (tex->is_array && tex->op != nir_texop_lod)
		flags |= IR3_INSTR_A;

	*flagsp = flags;
	*coordsp = coords;
}

static void
emit_tex(struct ir3_context *ctx, nir_tex_instr *tex)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction **dst, *sam, *src0[12], *src1[4];
	struct ir3_instruction * const *coord, * const *off, * const *ddx, * const *ddy;
	struct ir3_instruction *lod, *compare, *proj, *sample_index;
	bool has_bias = false, has_lod = false, has_proj = false, has_off = false;
	unsigned i, coords, flags;
	unsigned nsrc0 = 0, nsrc1 = 0;
	type_t type;
	opc_t opc = 0;

	coord = off = ddx = ddy = NULL;
	lod = proj = compare = sample_index = NULL;

	/* TODO: might just be one component for gathers? */
	dst = ir3_get_dst(ctx, &tex->dest, 4);

	for (unsigned i = 0; i < tex->num_srcs; i++) {
		switch (tex->src[i].src_type) {
		case nir_tex_src_coord:
			coord = ir3_get_src(ctx, &tex->src[i].src);
			break;
		case nir_tex_src_bias:
			lod = ir3_get_src(ctx, &tex->src[i].src)[0];
			has_bias = true;
			break;
		case nir_tex_src_lod:
			lod = ir3_get_src(ctx, &tex->src[i].src)[0];
			has_lod = true;
			break;
		case nir_tex_src_comparator: /* shadow comparator */
			compare = ir3_get_src(ctx, &tex->src[i].src)[0];
			break;
		case nir_tex_src_projector:
			proj = ir3_get_src(ctx, &tex->src[i].src)[0];
			has_proj = true;
			break;
		case nir_tex_src_offset:
			off = ir3_get_src(ctx, &tex->src[i].src);
			has_off = true;
			break;
		case nir_tex_src_ddx:
			ddx = ir3_get_src(ctx, &tex->src[i].src);
			break;
		case nir_tex_src_ddy:
			ddy = ir3_get_src(ctx, &tex->src[i].src);
			break;
		case nir_tex_src_ms_index:
			sample_index = ir3_get_src(ctx, &tex->src[i].src)[0];
			break;
		default:
			ir3_context_error(ctx, "Unhandled NIR tex src type: %d\n",
					tex->src[i].src_type);
			return;
		}
	}

	switch (tex->op) {
	case nir_texop_tex:      opc = has_lod ? OPC_SAML : OPC_SAM; break;
	case nir_texop_txb:      opc = OPC_SAMB;     break;
	case nir_texop_txl:      opc = OPC_SAML;     break;
	case nir_texop_txd:      opc = OPC_SAMGQ;    break;
	case nir_texop_txf:      opc = OPC_ISAML;    break;
	case nir_texop_lod:      opc = OPC_GETLOD;   break;
	case nir_texop_tg4:
		/* NOTE: a4xx might need to emulate gather w/ txf (this is
		 * what blob does, seems gather  is broken?), and a3xx did
		 * not support it (but probably could also emulate).
		 */
		switch (tex->component) {
		case 0:              opc = OPC_GATHER4R; break;
		case 1:              opc = OPC_GATHER4G; break;
		case 2:              opc = OPC_GATHER4B; break;
		case 3:              opc = OPC_GATHER4A; break;
		}
		break;
	case nir_texop_txf_ms:   opc = OPC_ISAMM;    break;
	case nir_texop_txs:
	case nir_texop_query_levels:
	case nir_texop_texture_samples:
	case nir_texop_samples_identical:
	case nir_texop_txf_ms_mcs:
		ir3_context_error(ctx, "Unhandled NIR tex type: %d\n", tex->op);
		return;
	}

	tex_info(tex, &flags, &coords);

	/*
	 * lay out the first argument in the proper order:
	 *  - actual coordinates first
	 *  - shadow reference
	 *  - array index
	 *  - projection w
	 *  - starting at offset 4, dpdx.xy, dpdy.xy
	 *
	 * bias/lod go into the second arg
	 */

	/* insert tex coords: */
	for (i = 0; i < coords; i++)
		src0[i] = coord[i];

	nsrc0 = i;

	/* NOTE a3xx (and possibly a4xx?) might be different, using isaml
	 * with scaled x coord according to requested sample:
	 */
	if (tex->op == nir_texop_txf_ms) {
		if (ctx->compiler->txf_ms_with_isaml) {
			/* the samples are laid out in x dimension as
			 *     0 1 2 3
			 * x_ms = (x << ms) + sample_index;
			 */
			struct ir3_instruction *ms;
			ms = create_immed(b, (ctx->samples >> (2 * tex->texture_index)) & 3);

			src0[0] = ir3_SHL_B(b, src0[0], 0, ms, 0);
			src0[0] = ir3_ADD_U(b, src0[0], 0, sample_index, 0);

			opc = OPC_ISAML;
		} else {
			src0[nsrc0++] = sample_index;
		}
	}

	/* scale up integer coords for TXF based on the LOD */
	if (ctx->compiler->unminify_coords && (opc == OPC_ISAML)) {
		assert(has_lod);
		for (i = 0; i < coords; i++)
			src0[i] = ir3_SHL_B(b, src0[i], 0, lod, 0);
	}

	if (coords == 1) {
		/* hw doesn't do 1d, so we treat it as 2d with
		 * height of 1, and patch up the y coord.
		 * TODO: y coord should be (int)0 in some cases..
		 */
		src0[nsrc0++] = create_immed(b, fui(0.5));
	}

	if (tex->is_shadow && tex->op != nir_texop_lod)
		src0[nsrc0++] = compare;

	if (tex->is_array && tex->op != nir_texop_lod) {
		struct ir3_instruction *idx = coord[coords];

		/* the array coord for cube arrays needs 0.5 added to it */
		if (ctx->compiler->array_index_add_half && (opc != OPC_ISAML))
			idx = ir3_ADD_F(b, idx, 0, create_immed(b, fui(0.5)), 0);

		src0[nsrc0++] = idx;
	}

	if (has_proj) {
		src0[nsrc0++] = proj;
		flags |= IR3_INSTR_P;
	}

	/* pad to 4, then ddx/ddy: */
	if (tex->op == nir_texop_txd) {
		while (nsrc0 < 4)
			src0[nsrc0++] = create_immed(b, fui(0.0));
		for (i = 0; i < coords; i++)
			src0[nsrc0++] = ddx[i];
		if (coords < 2)
			src0[nsrc0++] = create_immed(b, fui(0.0));
		for (i = 0; i < coords; i++)
			src0[nsrc0++] = ddy[i];
		if (coords < 2)
			src0[nsrc0++] = create_immed(b, fui(0.0));
	}

	/*
	 * second argument (if applicable):
	 *  - offsets
	 *  - lod
	 *  - bias
	 */
	if (has_off | has_lod | has_bias) {
		if (has_off) {
			unsigned off_coords = coords;
			if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE)
				off_coords--;
			for (i = 0; i < off_coords; i++)
				src1[nsrc1++] = off[i];
			if (off_coords < 2)
				src1[nsrc1++] = create_immed(b, fui(0.0));
			flags |= IR3_INSTR_O;
		}

		if (has_lod | has_bias)
			src1[nsrc1++] = lod;
	}

	switch (tex->dest_type) {
	case nir_type_invalid:
	case nir_type_float:
		type = TYPE_F32;
		break;
	case nir_type_int:
		type = TYPE_S32;
		break;
	case nir_type_uint:
	case nir_type_bool:
		type = TYPE_U32;
		break;
	default:
		unreachable("bad dest_type");
	}

	if (opc == OPC_GETLOD)
		type = TYPE_U32;

	unsigned tex_idx = tex->texture_index;

	ctx->max_texture_index = MAX2(ctx->max_texture_index, tex_idx);

	struct ir3_instruction *col0 = ir3_create_collect(ctx, src0, nsrc0);
	struct ir3_instruction *col1 = ir3_create_collect(ctx, src1, nsrc1);

	sam = ir3_SAM(b, opc, type, 0b1111, flags,
			tex_idx, tex_idx, col0, col1);

	if ((ctx->astc_srgb & (1 << tex_idx)) && !nir_tex_instr_is_query(tex)) {
		/* only need first 3 components: */
		sam->regs[0]->wrmask = 0x7;
		ir3_split_dest(b, dst, sam, 0, 3);

		/* we need to sample the alpha separately with a non-ASTC
		 * texture state:
		 */
		sam = ir3_SAM(b, opc, type, 0b1000, flags,
				tex_idx, tex_idx, col0, col1);

		array_insert(ctx->ir, ctx->ir->astc_srgb, sam);

		/* fixup .w component: */
		ir3_split_dest(b, &dst[3], sam, 3, 1);
	} else {
		/* normal (non-workaround) case: */
		ir3_split_dest(b, dst, sam, 0, 4);
	}

	/* GETLOD returns results in 4.8 fixed point */
	if (opc == OPC_GETLOD) {
		struct ir3_instruction *factor = create_immed(b, fui(1.0 / 256));

		compile_assert(ctx, tex->dest_type == nir_type_float);
		for (i = 0; i < 2; i++) {
			dst[i] = ir3_MUL_F(b, ir3_COV(b, dst[i], TYPE_U32, TYPE_F32), 0,
							   factor, 0);
		}
	}

	put_dst(ctx, &tex->dest);
}

static void
emit_tex_query_levels(struct ir3_context *ctx, nir_tex_instr *tex)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction **dst, *sam;

	dst = ir3_get_dst(ctx, &tex->dest, 1);

	sam = ir3_SAM(b, OPC_GETINFO, TYPE_U32, 0b0100, 0,
			tex->texture_index, tex->texture_index, NULL, NULL);

	/* even though there is only one component, since it ends
	 * up in .z rather than .x, we need a split_dest()
	 */
	ir3_split_dest(b, dst, sam, 0, 3);

	/* The # of levels comes from getinfo.z. We need to add 1 to it, since
	 * the value in TEX_CONST_0 is zero-based.
	 */
	if (ctx->compiler->levels_add_one)
		dst[0] = ir3_ADD_U(b, dst[0], 0, create_immed(b, 1), 0);

	put_dst(ctx, &tex->dest);
}

static void
emit_tex_txs(struct ir3_context *ctx, nir_tex_instr *tex)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction **dst, *sam;
	struct ir3_instruction *lod;
	unsigned flags, coords;

	tex_info(tex, &flags, &coords);

	/* Actually we want the number of dimensions, not coordinates. This
	 * distinction only matters for cubes.
	 */
	if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE)
		coords = 2;

	dst = ir3_get_dst(ctx, &tex->dest, 4);

	compile_assert(ctx, tex->num_srcs == 1);
	compile_assert(ctx, tex->src[0].src_type == nir_tex_src_lod);

	lod = ir3_get_src(ctx, &tex->src[0].src)[0];

	sam = ir3_SAM(b, OPC_GETSIZE, TYPE_U32, 0b1111, flags,
			tex->texture_index, tex->texture_index, lod, NULL);

	ir3_split_dest(b, dst, sam, 0, 4);

	/* Array size actually ends up in .w rather than .z. This doesn't
	 * matter for miplevel 0, but for higher mips the value in z is
	 * minified whereas w stays. Also, the value in TEX_CONST_3_DEPTH is
	 * returned, which means that we have to add 1 to it for arrays.
	 */
	if (tex->is_array) {
		if (ctx->compiler->levels_add_one) {
			dst[coords] = ir3_ADD_U(b, dst[3], 0, create_immed(b, 1), 0);
		} else {
			dst[coords] = ir3_MOV(b, dst[3], TYPE_U32);
		}
	}

	put_dst(ctx, &tex->dest);
}

static void
emit_jump(struct ir3_context *ctx, nir_jump_instr *jump)
{
	switch (jump->type) {
	case nir_jump_break:
	case nir_jump_continue:
	case nir_jump_return:
		/* I *think* we can simply just ignore this, and use the
		 * successor block link to figure out where we need to
		 * jump to for break/continue
		 */
		break;
	default:
		ir3_context_error(ctx, "Unhandled NIR jump type: %d\n", jump->type);
		break;
	}
}

static void
emit_instr(struct ir3_context *ctx, nir_instr *instr)
{
	switch (instr->type) {
	case nir_instr_type_alu:
		emit_alu(ctx, nir_instr_as_alu(instr));
		break;
	case nir_instr_type_deref:
		/* ignored, handled as part of the intrinsic they are src to */
		break;
	case nir_instr_type_intrinsic:
		emit_intrinsic(ctx, nir_instr_as_intrinsic(instr));
		break;
	case nir_instr_type_load_const:
		emit_load_const(ctx, nir_instr_as_load_const(instr));
		break;
	case nir_instr_type_ssa_undef:
		emit_undef(ctx, nir_instr_as_ssa_undef(instr));
		break;
	case nir_instr_type_tex: {
		nir_tex_instr *tex = nir_instr_as_tex(instr);
		/* couple tex instructions get special-cased:
		 */
		switch (tex->op) {
		case nir_texop_txs:
			emit_tex_txs(ctx, tex);
			break;
		case nir_texop_query_levels:
			emit_tex_query_levels(ctx, tex);
			break;
		default:
			emit_tex(ctx, tex);
			break;
		}
		break;
	}
	case nir_instr_type_jump:
		emit_jump(ctx, nir_instr_as_jump(instr));
		break;
	case nir_instr_type_phi:
		/* we have converted phi webs to regs in NIR by now */
		ir3_context_error(ctx, "Unexpected NIR instruction type: %d\n", instr->type);
		break;
	case nir_instr_type_call:
	case nir_instr_type_parallel_copy:
		ir3_context_error(ctx, "Unhandled NIR instruction type: %d\n", instr->type);
		break;
	}
}

static struct ir3_block *
get_block(struct ir3_context *ctx, const nir_block *nblock)
{
	struct ir3_block *block;
	struct hash_entry *hentry;
	unsigned i;

	hentry = _mesa_hash_table_search(ctx->block_ht, nblock);
	if (hentry)
		return hentry->data;

	block = ir3_block_create(ctx->ir);
	block->nblock = nblock;
	_mesa_hash_table_insert(ctx->block_ht, nblock, block);

	block->predecessors_count = nblock->predecessors->entries;
	block->predecessors = ralloc_array_size(block,
		sizeof(block->predecessors[0]), block->predecessors_count);
	i = 0;
	set_foreach(nblock->predecessors, sentry) {
		block->predecessors[i++] = get_block(ctx, sentry->key);
	}

	return block;
}

static void
emit_block(struct ir3_context *ctx, nir_block *nblock)
{
	struct ir3_block *block = get_block(ctx, nblock);

	for (int i = 0; i < ARRAY_SIZE(block->successors); i++) {
		if (nblock->successors[i]) {
			block->successors[i] =
				get_block(ctx, nblock->successors[i]);
		}
	}

	ctx->block = block;
	list_addtail(&block->node, &ctx->ir->block_list);

	/* re-emit addr register in each block if needed: */
	for (int i = 0; i < ARRAY_SIZE(ctx->addr_ht); i++) {
		_mesa_hash_table_destroy(ctx->addr_ht[i], NULL);
		ctx->addr_ht[i] = NULL;
	}

	nir_foreach_instr(instr, nblock) {
		ctx->cur_instr = instr;
		emit_instr(ctx, instr);
		ctx->cur_instr = NULL;
		if (ctx->error)
			return;
	}
}

static void emit_cf_list(struct ir3_context *ctx, struct exec_list *list);

static void
emit_if(struct ir3_context *ctx, nir_if *nif)
{
	struct ir3_instruction *condition = ir3_get_src(ctx, &nif->condition)[0];

	ctx->block->condition =
		ir3_get_predicate(ctx, ir3_b2n(condition->block, condition));

	emit_cf_list(ctx, &nif->then_list);
	emit_cf_list(ctx, &nif->else_list);
}

static void
emit_loop(struct ir3_context *ctx, nir_loop *nloop)
{
	emit_cf_list(ctx, &nloop->body);
}

static void
stack_push(struct ir3_context *ctx)
{
	ctx->stack++;
	ctx->max_stack = MAX2(ctx->max_stack, ctx->stack);
}

static void
stack_pop(struct ir3_context *ctx)
{
	compile_assert(ctx, ctx->stack > 0);
	ctx->stack--;
}

static void
emit_cf_list(struct ir3_context *ctx, struct exec_list *list)
{
	foreach_list_typed(nir_cf_node, node, node, list) {
		switch (node->type) {
		case nir_cf_node_block:
			emit_block(ctx, nir_cf_node_as_block(node));
			break;
		case nir_cf_node_if:
			stack_push(ctx);
			emit_if(ctx, nir_cf_node_as_if(node));
			stack_pop(ctx);
			break;
		case nir_cf_node_loop:
			stack_push(ctx);
			emit_loop(ctx, nir_cf_node_as_loop(node));
			stack_pop(ctx);
			break;
		case nir_cf_node_function:
			ir3_context_error(ctx, "TODO\n");
			break;
		}
	}
}

/* emit stream-out code.  At this point, the current block is the original
 * (nir) end block, and nir ensures that all flow control paths terminate
 * into the end block.  We re-purpose the original end block to generate
 * the 'if (vtxcnt < maxvtxcnt)' condition, then append the conditional
 * block holding stream-out write instructions, followed by the new end
 * block:
 *
 *   blockOrigEnd {
 *      p0.x = (vtxcnt < maxvtxcnt)
 *      // succs: blockStreamOut, blockNewEnd
 *   }
 *   blockStreamOut {
 *      ... stream-out instructions ...
 *      // succs: blockNewEnd
 *   }
 *   blockNewEnd {
 *   }
 */
static void
emit_stream_out(struct ir3_context *ctx)
{
	struct ir3_shader_variant *v = ctx->so;
	struct ir3 *ir = ctx->ir;
	struct ir3_stream_output_info *strmout =
			&ctx->so->shader->stream_output;
	struct ir3_block *orig_end_block, *stream_out_block, *new_end_block;
	struct ir3_instruction *vtxcnt, *maxvtxcnt, *cond;
	struct ir3_instruction *bases[IR3_MAX_SO_BUFFERS];

	/* create vtxcnt input in input block at top of shader,
	 * so that it is seen as live over the entire duration
	 * of the shader:
	 */
	vtxcnt = create_input(ctx, 0);
	add_sysval_input(ctx, SYSTEM_VALUE_VERTEX_CNT, vtxcnt);

	maxvtxcnt = create_driver_param(ctx, IR3_DP_VTXCNT_MAX);

	/* at this point, we are at the original 'end' block,
	 * re-purpose this block to stream-out condition, then
	 * append stream-out block and new-end block
	 */
	orig_end_block = ctx->block;

// TODO these blocks need to update predecessors..
// maybe w/ store_global intrinsic, we could do this
// stuff in nir->nir pass

	stream_out_block = ir3_block_create(ir);
	list_addtail(&stream_out_block->node, &ir->block_list);

	new_end_block = ir3_block_create(ir);
	list_addtail(&new_end_block->node, &ir->block_list);

	orig_end_block->successors[0] = stream_out_block;
	orig_end_block->successors[1] = new_end_block;
	stream_out_block->successors[0] = new_end_block;

	/* setup 'if (vtxcnt < maxvtxcnt)' condition: */
	cond = ir3_CMPS_S(ctx->block, vtxcnt, 0, maxvtxcnt, 0);
	cond->regs[0]->num = regid(REG_P0, 0);
	cond->cat2.condition = IR3_COND_LT;

	/* condition goes on previous block to the conditional,
	 * since it is used to pick which of the two successor
	 * paths to take:
	 */
	orig_end_block->condition = cond;

	/* switch to stream_out_block to generate the stream-out
	 * instructions:
	 */
	ctx->block = stream_out_block;

	/* Calculate base addresses based on vtxcnt.  Instructions
	 * generated for bases not used in following loop will be
	 * stripped out in the backend.
	 */
	for (unsigned i = 0; i < IR3_MAX_SO_BUFFERS; i++) {
		unsigned stride = strmout->stride[i];
		struct ir3_instruction *base, *off;

		base = create_uniform(ctx->block, regid(v->constbase.tfbo, i));

		/* 24-bit should be enough: */
		off = ir3_MUL_U(ctx->block, vtxcnt, 0,
				create_immed(ctx->block, stride * 4), 0);

		bases[i] = ir3_ADD_S(ctx->block, off, 0, base, 0);
	}

	/* Generate the per-output store instructions: */
	for (unsigned i = 0; i < strmout->num_outputs; i++) {
		for (unsigned j = 0; j < strmout->output[i].num_components; j++) {
			unsigned c = j + strmout->output[i].start_component;
			struct ir3_instruction *base, *out, *stg;

			base = bases[strmout->output[i].output_buffer];
			out = ctx->ir->outputs[regid(strmout->output[i].register_index, c)];

			stg = ir3_STG(ctx->block, base, 0, out, 0,
					create_immed(ctx->block, 1), 0);
			stg->cat6.type = TYPE_U32;
			stg->cat6.dst_offset = (strmout->output[i].dst_offset + j) * 4;

			array_insert(ctx->block, ctx->block->keeps, stg);
		}
	}

	/* and finally switch to the new_end_block: */
	ctx->block = new_end_block;
}

static void
emit_function(struct ir3_context *ctx, nir_function_impl *impl)
{
	nir_metadata_require(impl, nir_metadata_block_index);

	compile_assert(ctx, ctx->stack == 0);

	emit_cf_list(ctx, &impl->body);
	emit_block(ctx, impl->end_block);

	compile_assert(ctx, ctx->stack == 0);

	/* at this point, we should have a single empty block,
	 * into which we emit the 'end' instruction.
	 */
	compile_assert(ctx, list_empty(&ctx->block->instr_list));

	/* If stream-out (aka transform-feedback) enabled, emit the
	 * stream-out instructions, followed by a new empty block (into
	 * which the 'end' instruction lands).
	 *
	 * NOTE: it is done in this order, rather than inserting before
	 * we emit end_block, because NIR guarantees that all blocks
	 * flow into end_block, and that end_block has no successors.
	 * So by re-purposing end_block as the first block of stream-
	 * out, we guarantee that all exit paths flow into the stream-
	 * out instructions.
	 */
	if ((ctx->compiler->gpu_id < 500) &&
			(ctx->so->shader->stream_output.num_outputs > 0) &&
			!ctx->so->binning_pass) {
		debug_assert(ctx->so->type == MESA_SHADER_VERTEX);
		emit_stream_out(ctx);
	}

	ir3_END(ctx->block);
}

static struct ir3_instruction *
create_frag_coord(struct ir3_context *ctx, unsigned comp)
{
	struct ir3_block *block = ctx->block;
	struct ir3_instruction *instr;

	if (!ctx->frag_coord) {
		ctx->frag_coord = create_input_compmask(ctx, 0, 0xf);
		/* defer add_sysval_input() until after all inputs created */
	}

	ir3_split_dest(block, &instr, ctx->frag_coord, comp, 1);

	switch (comp) {
	case 0: /* .x */
	case 1: /* .y */
		/* for frag_coord, we get unsigned values.. we need
		 * to subtract (integer) 8 and divide by 16 (right-
		 * shift by 4) then convert to float:
		 *
		 *    sub.s tmp, src, 8
		 *    shr.b tmp, tmp, 4
		 *    mov.u32f32 dst, tmp
		 *
		 */
		instr = ir3_SUB_S(block, instr, 0,
				create_immed(block, 8), 0);
		instr = ir3_SHR_B(block, instr, 0,
				create_immed(block, 4), 0);
		instr = ir3_COV(block, instr, TYPE_U32, TYPE_F32);

		return instr;
	case 2: /* .z */
	case 3: /* .w */
	default:
		/* seems that we can use these as-is: */
		return instr;
	}
}

static void
setup_input(struct ir3_context *ctx, nir_variable *in)
{
	struct ir3_shader_variant *so = ctx->so;
	unsigned ncomp = glsl_get_components(in->type);
	unsigned n = in->data.driver_location;
	unsigned slot = in->data.location;

	/* let's pretend things other than vec4 don't exist: */
	ncomp = MAX2(ncomp, 4);

	/* skip unread inputs, we could end up with (for example), unsplit
	 * matrix/etc inputs in the case they are not read, so just silently
	 * skip these.
	 */
	if (ncomp > 4)
		return;

	compile_assert(ctx, ncomp == 4);

	so->inputs[n].slot = slot;
	so->inputs[n].compmask = (1 << ncomp) - 1;
	so->inputs_count = MAX2(so->inputs_count, n + 1);
	so->inputs[n].interpolate = in->data.interpolation;

	if (ctx->so->type == MESA_SHADER_FRAGMENT) {
		for (int i = 0; i < ncomp; i++) {
			struct ir3_instruction *instr = NULL;
			unsigned idx = (n * 4) + i;

			if (slot == VARYING_SLOT_POS) {
				so->inputs[n].bary = false;
				so->frag_coord = true;
				instr = create_frag_coord(ctx, i);
			} else if (slot == VARYING_SLOT_PNTC) {
				/* see for example st_nir_fixup_varying_slots().. this is
				 * maybe a bit mesa/st specific.  But we need things to line
				 * up for this in fdN_program:
				 *    unsigned texmask = 1 << (slot - VARYING_SLOT_VAR0);
				 *    if (emit->sprite_coord_enable & texmask) {
				 *       ...
				 *    }
				 */
				so->inputs[n].slot = VARYING_SLOT_VAR8;
				so->inputs[n].bary = true;
				instr = create_frag_input(ctx, false);
			} else {
				bool use_ldlv = false;

				/* detect the special case for front/back colors where
				 * we need to do flat vs smooth shading depending on
				 * rast state:
				 */
				if (in->data.interpolation == INTERP_MODE_NONE) {
					switch (slot) {
					case VARYING_SLOT_COL0:
					case VARYING_SLOT_COL1:
					case VARYING_SLOT_BFC0:
					case VARYING_SLOT_BFC1:
						so->inputs[n].rasterflat = true;
						break;
					default:
						break;
					}
				}

				if (ctx->compiler->flat_bypass) {
					if ((so->inputs[n].interpolate == INTERP_MODE_FLAT) ||
							(so->inputs[n].rasterflat && ctx->so->key.rasterflat))
						use_ldlv = true;
				}

				so->inputs[n].bary = true;

				instr = create_frag_input(ctx, use_ldlv);
			}

			compile_assert(ctx, idx < ctx->ir->ninputs);

			ctx->ir->inputs[idx] = instr;
		}
	} else if (ctx->so->type == MESA_SHADER_VERTEX) {
		for (int i = 0; i < ncomp; i++) {
			unsigned idx = (n * 4) + i;
			compile_assert(ctx, idx < ctx->ir->ninputs);
			ctx->ir->inputs[idx] = create_input(ctx, idx);
		}
	} else {
		ir3_context_error(ctx, "unknown shader type: %d\n", ctx->so->type);
	}

	if (so->inputs[n].bary || (ctx->so->type == MESA_SHADER_VERTEX)) {
		so->total_in += ncomp;
	}
}

static void
setup_output(struct ir3_context *ctx, nir_variable *out)
{
	struct ir3_shader_variant *so = ctx->so;
	unsigned ncomp = glsl_get_components(out->type);
	unsigned n = out->data.driver_location;
	unsigned slot = out->data.location;
	unsigned comp = 0;

	/* let's pretend things other than vec4 don't exist: */
	ncomp = MAX2(ncomp, 4);
	compile_assert(ctx, ncomp == 4);

	if (ctx->so->type == MESA_SHADER_FRAGMENT) {
		switch (slot) {
		case FRAG_RESULT_DEPTH:
			comp = 2;  /* tgsi will write to .z component */
			so->writes_pos = true;
			break;
		case FRAG_RESULT_COLOR:
			so->color0_mrt = 1;
			break;
		default:
			if (slot >= FRAG_RESULT_DATA0)
				break;
			ir3_context_error(ctx, "unknown FS output name: %s\n",
					gl_frag_result_name(slot));
		}
	} else if (ctx->so->type == MESA_SHADER_VERTEX) {
		switch (slot) {
		case VARYING_SLOT_POS:
			so->writes_pos = true;
			break;
		case VARYING_SLOT_PSIZ:
			so->writes_psize = true;
			break;
		case VARYING_SLOT_COL0:
		case VARYING_SLOT_COL1:
		case VARYING_SLOT_BFC0:
		case VARYING_SLOT_BFC1:
		case VARYING_SLOT_FOGC:
		case VARYING_SLOT_CLIP_DIST0:
		case VARYING_SLOT_CLIP_DIST1:
		case VARYING_SLOT_CLIP_VERTEX:
			break;
		default:
			if (slot >= VARYING_SLOT_VAR0)
				break;
			if ((VARYING_SLOT_TEX0 <= slot) && (slot <= VARYING_SLOT_TEX7))
				break;
			ir3_context_error(ctx, "unknown VS output name: %s\n",
					gl_varying_slot_name(slot));
		}
	} else {
		ir3_context_error(ctx, "unknown shader type: %d\n", ctx->so->type);
	}

	compile_assert(ctx, n < ARRAY_SIZE(so->outputs));

	so->outputs[n].slot = slot;
	so->outputs[n].regid = regid(n, comp);
	so->outputs_count = MAX2(so->outputs_count, n + 1);

	for (int i = 0; i < ncomp; i++) {
		unsigned idx = (n * 4) + i;
		compile_assert(ctx, idx < ctx->ir->noutputs);
		ctx->ir->outputs[idx] = create_immed(ctx->block, fui(0.0));
	}
}

static int
max_drvloc(struct exec_list *vars)
{
	int drvloc = -1;
	nir_foreach_variable(var, vars) {
		drvloc = MAX2(drvloc, (int)var->data.driver_location);
	}
	return drvloc;
}

static const unsigned max_sysvals[] = {
	[MESA_SHADER_FRAGMENT] = 24,  // TODO
	[MESA_SHADER_VERTEX]  = 16,
	[MESA_SHADER_COMPUTE] = 16, // TODO how many do we actually need?
};

static void
emit_instructions(struct ir3_context *ctx)
{
	unsigned ninputs, noutputs;
	nir_function_impl *fxn = nir_shader_get_entrypoint(ctx->s);

	ninputs  = (max_drvloc(&ctx->s->inputs) + 1) * 4;
	noutputs = (max_drvloc(&ctx->s->outputs) + 1) * 4;

	/* we need to leave room for sysvals:
	 */
	ninputs += max_sysvals[ctx->so->type];

	ctx->ir = ir3_create(ctx->compiler, ninputs, noutputs);

	/* Create inputs in first block: */
	ctx->block = get_block(ctx, nir_start_block(fxn));
	ctx->in_block = ctx->block;
	list_addtail(&ctx->block->node, &ctx->ir->block_list);

	ninputs -= max_sysvals[ctx->so->type];

	/* for fragment shader, the vcoord input register is used as the
	 * base for bary.f varying fetch instrs:
	 */
	struct ir3_instruction *vcoord = NULL;
	if (ctx->so->type == MESA_SHADER_FRAGMENT) {
		struct ir3_instruction *xy[2];

		vcoord = create_input_compmask(ctx, 0, 0x3);
		ir3_split_dest(ctx->block, xy, vcoord, 0, 2);

		ctx->frag_vcoord = ir3_create_collect(ctx, xy, 2);
	}

	/* Setup inputs: */
	nir_foreach_variable(var, &ctx->s->inputs) {
		setup_input(ctx, var);
	}

	/* Defer add_sysval_input() stuff until after setup_inputs(),
	 * because sysvals need to be appended after varyings:
	 */
	if (vcoord) {
		add_sysval_input_compmask(ctx, SYSTEM_VALUE_VARYING_COORD,
				0x3, vcoord);
	}

	if (ctx->frag_coord) {
		add_sysval_input_compmask(ctx, SYSTEM_VALUE_FRAG_COORD,
				0xf, ctx->frag_coord);
	}

	/* Setup outputs: */
	nir_foreach_variable(var, &ctx->s->outputs) {
		setup_output(ctx, var);
	}

	/* Setup registers (which should only be arrays): */
	nir_foreach_register(reg, &ctx->s->registers) {
		ir3_declare_array(ctx, reg);
	}

	/* NOTE: need to do something more clever when we support >1 fxn */
	nir_foreach_register(reg, &fxn->registers) {
		ir3_declare_array(ctx, reg);
	}
	/* And emit the body: */
	ctx->impl = fxn;
	emit_function(ctx, fxn);
}

/* from NIR perspective, we actually have varying inputs.  But the varying
 * inputs, from an IR standpoint, are just bary.f/ldlv instructions.  The
 * only actual inputs are the sysvals.
 */
static void
fixup_frag_inputs(struct ir3_context *ctx)
{
	struct ir3_shader_variant *so = ctx->so;
	struct ir3 *ir = ctx->ir;
	unsigned i = 0;

	/* sysvals should appear at the end of the inputs, drop everything else: */
	while ((i < so->inputs_count) && !so->inputs[i].sysval)
		i++;

	/* at IR level, inputs are always blocks of 4 scalars: */
	i *= 4;

	ir->inputs = &ir->inputs[i];
	ir->ninputs -= i;
}

/* Fixup tex sampler state for astc/srgb workaround instructions.  We
 * need to assign the tex state indexes for these after we know the
 * max tex index.
 */
static void
fixup_astc_srgb(struct ir3_context *ctx)
{
	struct ir3_shader_variant *so = ctx->so;
	/* indexed by original tex idx, value is newly assigned alpha sampler
	 * state tex idx.  Zero is invalid since there is at least one sampler
	 * if we get here.
	 */
	unsigned alt_tex_state[16] = {0};
	unsigned tex_idx = ctx->max_texture_index + 1;
	unsigned idx = 0;

	so->astc_srgb.base = tex_idx;

	for (unsigned i = 0; i < ctx->ir->astc_srgb_count; i++) {
		struct ir3_instruction *sam = ctx->ir->astc_srgb[i];

		compile_assert(ctx, sam->cat5.tex < ARRAY_SIZE(alt_tex_state));

		if (alt_tex_state[sam->cat5.tex] == 0) {
			/* assign new alternate/alpha tex state slot: */
			alt_tex_state[sam->cat5.tex] = tex_idx++;
			so->astc_srgb.orig_idx[idx++] = sam->cat5.tex;
			so->astc_srgb.count++;
		}

		sam->cat5.tex = alt_tex_state[sam->cat5.tex];
	}
}

static void
fixup_binning_pass(struct ir3_context *ctx)
{
	struct ir3_shader_variant *so = ctx->so;
	struct ir3 *ir = ctx->ir;
	unsigned i, j;

	for (i = 0, j = 0; i < so->outputs_count; i++) {
		unsigned slot = so->outputs[i].slot;

		/* throw away everything but first position/psize */
		if ((slot == VARYING_SLOT_POS) || (slot == VARYING_SLOT_PSIZ)) {
			if (i != j) {
				so->outputs[j] = so->outputs[i];
				ir->outputs[(j*4)+0] = ir->outputs[(i*4)+0];
				ir->outputs[(j*4)+1] = ir->outputs[(i*4)+1];
				ir->outputs[(j*4)+2] = ir->outputs[(i*4)+2];
				ir->outputs[(j*4)+3] = ir->outputs[(i*4)+3];
			}
			j++;
		}
	}
	so->outputs_count = j;
	ir->noutputs = j * 4;
}

int
ir3_compile_shader_nir(struct ir3_compiler *compiler,
		struct ir3_shader_variant *so)
{
	struct ir3_context *ctx;
	struct ir3 *ir;
	struct ir3_instruction **inputs;
	unsigned i, actual_in, inloc;
	int ret = 0, max_bary;

	assert(!so->ir);

	ctx = ir3_context_init(compiler, so);
	if (!ctx) {
		DBG("INIT failed!");
		ret = -1;
		goto out;
	}

	emit_instructions(ctx);

	if (ctx->error) {
		DBG("EMIT failed!");
		ret = -1;
		goto out;
	}

	ir = so->ir = ctx->ir;

	/* keep track of the inputs from TGSI perspective.. */
	inputs = ir->inputs;

	/* but fixup actual inputs for frag shader: */
	if (so->type == MESA_SHADER_FRAGMENT)
		fixup_frag_inputs(ctx);

	/* at this point, for binning pass, throw away unneeded outputs: */
	if (so->binning_pass && (ctx->compiler->gpu_id < 600))
		fixup_binning_pass(ctx);

	/* if we want half-precision outputs, mark the output registers
	 * as half:
	 */
	if (so->key.half_precision) {
		for (i = 0; i < ir->noutputs; i++) {
			struct ir3_instruction *out = ir->outputs[i];

			if (!out)
				continue;

			/* if frag shader writes z, that needs to be full precision: */
			if (so->outputs[i/4].slot == FRAG_RESULT_DEPTH)
				continue;

			out->regs[0]->flags |= IR3_REG_HALF;
			/* output could be a fanout (ie. texture fetch output)
			 * in which case we need to propagate the half-reg flag
			 * up to the definer so that RA sees it:
			 */
			if (out->opc == OPC_META_FO) {
				out = out->regs[1]->instr;
				out->regs[0]->flags |= IR3_REG_HALF;
			}

			if (out->opc == OPC_MOV) {
				out->cat1.dst_type = half_type(out->cat1.dst_type);
			}
		}
	}

	if (ir3_shader_debug & IR3_DBG_OPTMSGS) {
		printf("BEFORE CP:\n");
		ir3_print(ir);
	}

	ir3_cp(ir, so);

	/* at this point, for binning pass, throw away unneeded outputs:
	 * Note that for a6xx and later, we do this after ir3_cp to ensure
	 * that the uniform/constant layout for BS and VS matches, so that
	 * we can re-use same VS_CONST state group.
	 */
	if (so->binning_pass && (ctx->compiler->gpu_id >= 600))
		fixup_binning_pass(ctx);

	/* Insert mov if there's same instruction for each output.
	 * eg. dEQP-GLES31.functional.shaders.opaque_type_indexing.sampler.const_expression.vertex.sampler2dshadow
	 */
	for (int i = ir->noutputs - 1; i >= 0; i--) {
		if (!ir->outputs[i])
			continue;
		for (unsigned j = 0; j < i; j++) {
			if (ir->outputs[i] == ir->outputs[j]) {
				ir->outputs[i] =
					ir3_MOV(ir->outputs[i]->block, ir->outputs[i], TYPE_F32);
			}
		}
	}

	if (ir3_shader_debug & IR3_DBG_OPTMSGS) {
		printf("BEFORE GROUPING:\n");
		ir3_print(ir);
	}

	ir3_sched_add_deps(ir);

	/* Group left/right neighbors, inserting mov's where needed to
	 * solve conflicts:
	 */
	ir3_group(ir);

	if (ir3_shader_debug & IR3_DBG_OPTMSGS) {
		printf("AFTER GROUPING:\n");
		ir3_print(ir);
	}

	ir3_depth(ir);

	if (ir3_shader_debug & IR3_DBG_OPTMSGS) {
		printf("AFTER DEPTH:\n");
		ir3_print(ir);
	}

	ret = ir3_sched(ir);
	if (ret) {
		DBG("SCHED failed!");
		goto out;
	}

	if (ir3_shader_debug & IR3_DBG_OPTMSGS) {
		printf("AFTER SCHED:\n");
		ir3_print(ir);
	}

	ret = ir3_ra(ir, so->type, so->frag_coord, so->frag_face);
	if (ret) {
		DBG("RA failed!");
		goto out;
	}

	if (ir3_shader_debug & IR3_DBG_OPTMSGS) {
		printf("AFTER RA:\n");
		ir3_print(ir);
	}

	/* fixup input/outputs: */
	for (i = 0; i < so->outputs_count; i++) {
		so->outputs[i].regid = ir->outputs[i*4]->regs[0]->num;
	}

	/* Note that some or all channels of an input may be unused: */
	actual_in = 0;
	inloc = 0;
	for (i = 0; i < so->inputs_count; i++) {
		unsigned j, reg = regid(63,0), compmask = 0, maxcomp = 0;
		so->inputs[i].ncomp = 0;
		so->inputs[i].inloc = inloc;
		for (j = 0; j < 4; j++) {
			struct ir3_instruction *in = inputs[(i*4) + j];
			if (in && !(in->flags & IR3_INSTR_UNUSED)) {
				compmask |= (1 << j);
				reg = in->regs[0]->num - j;
				actual_in++;
				so->inputs[i].ncomp++;
				if ((so->type == MESA_SHADER_FRAGMENT) && so->inputs[i].bary) {
					/* assign inloc: */
					assert(in->regs[1]->flags & IR3_REG_IMMED);
					in->regs[1]->iim_val = inloc + j;
					maxcomp = j + 1;
				}
			}
		}
		if ((so->type == MESA_SHADER_FRAGMENT) && compmask && so->inputs[i].bary) {
			so->varying_in++;
			so->inputs[i].compmask = (1 << maxcomp) - 1;
			inloc += maxcomp;
		} else if (!so->inputs[i].sysval) {
			so->inputs[i].compmask = compmask;
		}
		so->inputs[i].regid = reg;
	}

	if (ctx->astc_srgb)
		fixup_astc_srgb(ctx);

	/* We need to do legalize after (for frag shader's) the "bary.f"
	 * offsets (inloc) have been assigned.
	 */
	ir3_legalize(ir, &so->num_samp, &so->has_ssbo, &max_bary);

	if (ir3_shader_debug & IR3_DBG_OPTMSGS) {
		printf("AFTER LEGALIZE:\n");
		ir3_print(ir);
	}

	so->branchstack = ctx->max_stack;

	/* Note that actual_in counts inputs that are not bary.f'd for FS: */
	if (so->type == MESA_SHADER_VERTEX)
		so->total_in = actual_in;
	else
		so->total_in = max_bary + 1;

out:
	if (ret) {
		if (so->ir)
			ir3_destroy(so->ir);
		so->ir = NULL;
	}
	ir3_context_free(ctx);

	return ret;
}
