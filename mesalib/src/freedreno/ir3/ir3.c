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
#include "ir3_shader.h"

/* simple allocator to carve allocations out of an up-front allocated heap,
 * so that we can free everything easily in one shot.
 */
void * ir3_alloc(struct ir3 *shader, int sz)
{
	return rzalloc_size(shader, sz); /* TODO: don't use rzalloc */
}

struct ir3 * ir3_create(struct ir3_compiler *compiler,
		struct ir3_shader_variant *v)
{
	struct ir3 *shader = rzalloc(v, struct ir3);

	shader->compiler = compiler;
	shader->type = v->type;

	list_inithead(&shader->block_list);
	list_inithead(&shader->array_list);

	return shader;
}

void ir3_destroy(struct ir3 *shader)
{
	ralloc_free(shader);
}

static void
collect_reg_info(struct ir3_instruction *instr, struct ir3_register *reg,
		struct ir3_info *info)
{
	struct ir3_shader_variant *v = info->data;
	unsigned repeat = instr->repeat;

	if (reg->flags & IR3_REG_IMMED) {
		/* nothing to do */
		return;
	}

	if (!(reg->flags & IR3_REG_R)) {
		repeat = 0;
	}

	unsigned components;
	int16_t max;

	if (reg->flags & IR3_REG_RELATIV) {
		components = reg->size;
		max = (reg->array.offset + repeat + components - 1);
	} else {
		components = util_last_bit(reg->wrmask);
		max = (reg->num + repeat + components - 1);
	}

	if (reg->flags & IR3_REG_CONST) {
		info->max_const = MAX2(info->max_const, max >> 2);
	} else if (max < regid(48, 0)) {
		if (reg->flags & IR3_REG_HALF) {
			if (v->mergedregs) {
				/* starting w/ a6xx, half regs conflict with full regs: */
				info->max_reg = MAX2(info->max_reg, max >> 3);
			} else {
				info->max_half_reg = MAX2(info->max_half_reg, max >> 2);
			}
		} else {
			info->max_reg = MAX2(info->max_reg, max >> 2);
		}
	}
}

static bool
should_double_threadsize(struct ir3_shader_variant *v,
						 unsigned regs_count)
{
	const struct ir3_compiler *compiler = v->shader->compiler;

	/* We can't support more than compiler->branchstack_size diverging threads
	 * in a wave. Thus, doubling the threadsize is only possible if we don't
	 * exceed the branchstack size limit.
	 */
	if (MIN2(v->branchstack, compiler->threadsize_base * 2) >
			compiler->branchstack_size) {
		return false;
	}

	switch (v->type) {
	case MESA_SHADER_COMPUTE: {
		unsigned threads_per_wg = v->local_size[0] * v->local_size[1] * v->local_size[2];

		/* For a5xx, if the workgroup size is greater than the maximum number
		 * of threads per core with 32 threads per wave (512) then we have to
		 * use the doubled threadsize because otherwise the workgroup wouldn't
		 * fit. For smaller workgroup sizes, we follow the blob and use the
		 * smaller threadsize.
		 */
		if (compiler->gpu_id < 600) {
			return v->local_size_variable || threads_per_wg >
				compiler->threadsize_base * compiler->max_waves;
		}

		/* On a6xx, we prefer the larger threadsize unless the workgroup is
		 * small enough that it would be useless. Note that because
		 * threadsize_base is bumped to 64, we don't have to worry about the
		 * workgroup fitting, unlike the a5xx case.
		 */
		if (!v->local_size_variable) {
			if (threads_per_wg <= compiler->threadsize_base)
				return false;
		}
	}
	FALLTHROUGH;
	case MESA_SHADER_FRAGMENT: {
		/* Check that doubling the threadsize wouldn't exceed the regfile size */
		return regs_count * 2 <= compiler->reg_size_vec4;
	}

	default:
		/* On a6xx+, it's impossible to use a doubled wavesize in the geometry
		 * stages - the bit doesn't exist. The blob never used it for the VS
		 * on earlier gen's anyway.
		 */
		return false;
	}
}

/* Get the maximum number of waves that could be used even if this shader
 * didn't use any registers.
 */
static unsigned
get_reg_independent_max_waves(struct ir3_shader_variant *v, bool double_threadsize)
{
	const struct ir3_compiler *compiler = v->shader->compiler;
	unsigned max_waves = compiler->max_waves;

	/* If this is a compute shader, compute the limit based on shared size */
	if (v->type == MESA_SHADER_COMPUTE) {
		/* Shared is allocated in chunks of 1k */
		unsigned shared_per_wg = ALIGN_POT(v->shared_size, 1024);
		if (shared_per_wg > 0 && !v->local_size_variable) {
			unsigned wgs_per_core = compiler->local_mem_size / shared_per_wg;
			unsigned threads_per_wg = v->local_size[0] * v->local_size[1] * v->local_size[2];
			unsigned waves_per_wg =
				DIV_ROUND_UP(threads_per_wg,
					compiler->threadsize_base *
					(double_threadsize ? 2 : 1) * compiler->wave_granularity);
			max_waves =
				MIN2(max_waves, waves_per_wg * wgs_per_core * compiler->wave_granularity);
		}
	}

	/* Compute the limit based on branchstack */
	if (v->branchstack > 0) {
		unsigned branchstack_max_waves =
			compiler->branchstack_size / v->branchstack *
			compiler->wave_granularity;
		max_waves = MIN2(max_waves, branchstack_max_waves);
	}

	return max_waves;
}

/* Get the maximum number of waves that could be launched limited by reg size.
 */
static unsigned
get_reg_dependent_max_waves(const struct ir3_compiler *compiler,
							unsigned reg_count, bool double_threadsize)
{
	return reg_count ?
		(compiler->reg_size_vec4 / (reg_count * (double_threadsize ? 2 : 1)) *
		 compiler->wave_granularity) :
		compiler->max_waves;
}

void
ir3_collect_info(struct ir3_shader_variant *v)
{
	struct ir3_info *info = &v->info;
	struct ir3 *shader = v->ir;
	const struct ir3_compiler *compiler = v->shader->compiler;

	memset(info, 0, sizeof(*info));
	info->data          = v;
	info->max_reg       = -1;
	info->max_half_reg  = -1;
	info->max_const     = -1;
	info->multi_dword_ldp_stp = false;

	uint32_t instr_count = 0;
	foreach_block (block, &shader->block_list) {
		foreach_instr (instr, &block->instr_list) {
			instr_count++;
		}
	}

	v->instrlen = DIV_ROUND_UP(instr_count, compiler->instr_align);

	/* Pad out with NOPs to instrlen, including at least 4 so that cffdump
	 * doesn't try to decode the following data as instructions (such as the
	 * next stage's shader in turnip)
	 */
	info->size = MAX2(v->instrlen * compiler->instr_align, instr_count + 4) * 8;
	info->sizedwords = info->size / 4;

	foreach_block (block, &shader->block_list) {
		int sfu_delay = 0;

		foreach_instr (instr, &block->instr_list) {

			foreach_src (reg, instr) {
				collect_reg_info(instr, reg, info);
			}

			if (writes_gpr(instr)) {
				collect_reg_info(instr, instr->regs[0], info);
			}

			if ((instr->opc == OPC_STP || instr->opc == OPC_LDP)) {
				struct ir3_register *base = (instr->opc == OPC_STP) ?
						instr->regs[3] : instr->regs[2];
				if (base->iim_val * type_size(instr->cat6.type) > 32) {
					info->multi_dword_ldp_stp = true;
				}
			}

			if ((instr->opc == OPC_BARY_F) && (instr->regs[0]->flags & IR3_REG_EI))
				info->last_baryf = info->instrs_count;

			unsigned instrs_count = 1 + instr->repeat + instr->nop;
			unsigned nops_count = instr->nop;

			if (instr->opc == OPC_NOP) {
				nops_count = 1 + instr->repeat;
				info->instrs_per_cat[0] += nops_count;
			} else {
				info->instrs_per_cat[opc_cat(instr->opc)] += 1 + instr->repeat;
				info->instrs_per_cat[0] += nops_count;
			}

			if (instr->opc == OPC_MOV) {
				if (instr->cat1.src_type == instr->cat1.dst_type) {
					info->mov_count += 1 + instr->repeat;
				} else {
					info->cov_count += 1 + instr->repeat;
				}
			}

			info->instrs_count += instrs_count;
			info->nops_count += nops_count;

			if (instr->flags & IR3_INSTR_SS) {
				info->ss++;
				info->sstall += sfu_delay;
				sfu_delay = 0;
			}

			if (instr->flags & IR3_INSTR_SY)
				info->sy++;

			if (is_sfu(instr)) {
				sfu_delay = 10;
			} else {
				int n = MIN2(sfu_delay, 1 + instr->repeat + instr->nop);
				sfu_delay -= n;
			}
		}
	}

	/* TODO: for a5xx and below, is there a separate regfile for
	 * half-registers?
	 */
	unsigned regs_count =
		info->max_reg + 1 + (compiler->gpu_id >= 600 ? ((info->max_half_reg + 2) / 2) : 0);

	info->double_threadsize = should_double_threadsize(v, regs_count);
	unsigned reg_independent_max_waves =
		get_reg_independent_max_waves(v, info->double_threadsize);
	unsigned reg_dependent_max_waves =
		get_reg_dependent_max_waves(compiler, regs_count, info->double_threadsize);
	info->max_waves = MIN2(reg_independent_max_waves, reg_dependent_max_waves);
	assert(info->max_waves <= v->shader->compiler->max_waves);
}

static struct ir3_register * reg_create(struct ir3 *shader,
		int num, int flags)
{
	struct ir3_register *reg =
			ir3_alloc(shader, sizeof(struct ir3_register));
	reg->wrmask = 1;
	reg->flags = flags;
	reg->num = num;
	return reg;
}

static void insert_instr(struct ir3_block *block,
		struct ir3_instruction *instr)
{
	struct ir3 *shader = block->shader;

	instr->serialno = ++shader->instr_count;

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


void ir3_block_add_predecessor(struct ir3_block *block, struct ir3_block *pred)
{
	array_insert(block, block->predecessors, pred);
}

void ir3_block_remove_predecessor(struct ir3_block *block, struct ir3_block *pred)
{
	for (unsigned i = 0; i < block->predecessors_count; i++) {
		if (block->predecessors[i] == pred) {
			if (i < block->predecessors_count - 1) {
				block->predecessors[i] =
					block->predecessors[block->predecessors_count - 1];
			}

			block->predecessors_count--;
			return;
		}
	}
}

unsigned ir3_block_get_pred_index(struct ir3_block *block, struct ir3_block *pred)
{
	for (unsigned i = 0; i < block->predecessors_count; i++) {
		if (block->predecessors[i] == pred) {
			return i;
		}
	}

	unreachable("ir3_block_get_pred_index() invalid predecessor");
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

struct ir3_instruction * ir3_instr_create(struct ir3_block *block,
		opc_t opc, int nreg)
{
	struct ir3_instruction *instr = instr_create(block, nreg);
	instr->block = block;
	instr->opc = opc;
	insert_instr(block, instr);
	return instr;
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
	for (unsigned i = 0; i < instr->deps_count; i++) {
		if (instr->deps[i] == dep)
			return;
	}

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

		debug_assert(!instr->address);
		debug_assert(instr->block == addr->block);

		instr->address = addr;
		debug_assert(reg_num(addr->regs[0]) == REG_A0);
		unsigned comp = reg_comp(addr->regs[0]);
		if (comp == 0) {
			array_insert(ir, ir->a0_users, instr);
		} else {
			debug_assert(comp == 1);
			array_insert(ir, ir->a1_users, instr);
		}
	}
}

void
ir3_block_clear_mark(struct ir3_block *block)
{
	foreach_instr (instr, &block->instr_list)
		instr->flags &= ~IR3_INSTR_MARK;
}

void
ir3_clear_mark(struct ir3 *ir)
{
	foreach_block (block, &ir->block_list) {
		ir3_block_clear_mark(block);
	}
}

unsigned
ir3_count_instructions(struct ir3 *ir)
{
	unsigned cnt = 1;
	foreach_block (block, &ir->block_list) {
		block->start_ip = cnt;
		foreach_instr (instr, &block->instr_list) {
			instr->ip = cnt++;
		}
		block->end_ip = cnt;
	}
	return cnt;
}

/* When counting instructions for RA, we insert extra fake instructions at the
 * beginning of each block, where values become live, and at the end where
 * values die. This prevents problems where values live-in at the beginning or
 * live-out at the end of a block from being treated as if they were
 * live-in/live-out at the first/last instruction, which would be incorrect.
 * In ir3_legalize these ip's are assumed to be actual ip's of the final
 * program, so it would be incorrect to use this everywhere.
 */

unsigned
ir3_count_instructions_ra(struct ir3 *ir)
{
	unsigned cnt = 1;
	foreach_block (block, &ir->block_list) {
		block->start_ip = cnt++;
		foreach_instr (instr, &block->instr_list) {
			instr->ip = cnt++;
		}
		block->end_ip = cnt++;
	}
	return cnt;
}

struct ir3_array *
ir3_lookup_array(struct ir3 *ir, unsigned id)
{
	foreach_array (arr, &ir->array_list)
		if (arr->id == id)
			return arr;
	return NULL;
}

void
ir3_find_ssa_uses(struct ir3 *ir, void *mem_ctx, bool falsedeps)
{
	/* We could do this in a single pass if we can assume instructions
	 * are always sorted.  Which currently might not always be true.
	 * (In particular after ir3_group pass, but maybe other places.)
	 */
	foreach_block (block, &ir->block_list)
		foreach_instr (instr, &block->instr_list)
			instr->uses = NULL;

	foreach_block (block, &ir->block_list) {
		foreach_instr (instr, &block->instr_list) {
			foreach_ssa_src_n (src, n, instr) {
				if (__is_false_dep(instr, n) && !falsedeps)
					continue;
				if (!src->uses)
					src->uses = _mesa_pointer_set_create(mem_ctx);
				_mesa_set_add(src->uses, instr);
			}
		}
	}
}

/**
 * Set the destination type of an instruction, for example if a
 * conversion is folded in, handling the special cases where the
 * instruction's dest type or opcode needs to be fixed up.
 */
void
ir3_set_dst_type(struct ir3_instruction *instr, bool half)
{
	if (half) {
		instr->regs[0]->flags |= IR3_REG_HALF;
	} else {
		instr->regs[0]->flags &= ~IR3_REG_HALF;
	}

	switch (opc_cat(instr->opc)) {
	case 1: /* move instructions */
		if (half) {
			instr->cat1.dst_type = half_type(instr->cat1.dst_type);
		} else {
			instr->cat1.dst_type = full_type(instr->cat1.dst_type);
		}
		break;
	case 4:
		if (half) {
			instr->opc = cat4_half_opc(instr->opc);
		} else {
			instr->opc = cat4_full_opc(instr->opc);
		}
		break;
	case 5:
		if (half) {
			instr->cat5.type = half_type(instr->cat5.type);
		} else {
			instr->cat5.type = full_type(instr->cat5.type);
		}
		break;
	}
}

/**
 * One-time fixup for instruction src-types.  Other than cov's that
 * are folded, an instruction's src type does not change.
 */
void
ir3_fixup_src_type(struct ir3_instruction *instr)
{
	switch (opc_cat(instr->opc)) {
	case 1: /* move instructions */
		if (instr->regs[1]->flags & IR3_REG_HALF) {
			instr->cat1.src_type = half_type(instr->cat1.src_type);
		} else {
			instr->cat1.src_type = full_type(instr->cat1.src_type);
		}
		break;
	case 3:
		if (instr->regs[1]->flags & IR3_REG_HALF) {
			instr->opc = cat3_half_opc(instr->opc);
		} else {
			instr->opc = cat3_full_opc(instr->opc);
		}
		break;
	}
}

static unsigned
cp_flags(unsigned flags)
{
	/* only considering these flags (at least for now): */
	flags &= (IR3_REG_CONST | IR3_REG_IMMED |
			IR3_REG_FNEG | IR3_REG_FABS |
			IR3_REG_SNEG | IR3_REG_SABS |
			IR3_REG_BNOT | IR3_REG_RELATIV);
	return flags;
}

bool
ir3_valid_flags(struct ir3_instruction *instr, unsigned n,
		unsigned flags)
{
	struct ir3_compiler *compiler = instr->block->shader->compiler;
	unsigned valid_flags;

	if ((flags & IR3_REG_SHARED) &&
			opc_cat(instr->opc) > 3)
		return false;

	flags = cp_flags(flags);

	/* If destination is indirect, then source cannot be.. at least
	 * I don't think so..
	 */
	if ((instr->regs[0]->flags & IR3_REG_RELATIV) &&
			(flags & IR3_REG_RELATIV))
		return false;

	if (flags & IR3_REG_RELATIV) {
		/* TODO need to test on earlier gens.. pretty sure the earlier
		 * problem was just that we didn't check that the src was from
		 * same block (since we can't propagate address register values
		 * across blocks currently)
		 */
		if (compiler->gpu_id < 600)
			return false;

		/* NOTE in the special try_swap_mad_two_srcs() case we can be
		 * called on a src that has already had an indirect load folded
		 * in, in which case ssa() returns NULL
		 */
		if (instr->regs[n+1]->flags & IR3_REG_SSA) {
			struct ir3_instruction *src = ssa(instr->regs[n+1]);
			if (src->address->block != instr->block)
				return false;
		}
	}

	switch (opc_cat(instr->opc)) {
	case 0: /* end, chmask */
		return flags == 0;
	case 1:
		valid_flags = IR3_REG_IMMED | IR3_REG_CONST | IR3_REG_RELATIV;
		if (flags & ~valid_flags)
			return false;
		break;
	case 2:
		valid_flags = ir3_cat2_absneg(instr->opc) |
				IR3_REG_CONST | IR3_REG_RELATIV;

		if (ir3_cat2_int(instr->opc))
			valid_flags |= IR3_REG_IMMED;

		if (flags & ~valid_flags)
			return false;

		if (flags & (IR3_REG_CONST | IR3_REG_IMMED)) {
			unsigned m = (n ^ 1) + 1;
			/* cannot deal w/ const in both srcs:
			 * (note that some cat2 actually only have a single src)
			 */
			if (m < instr->regs_count) {
				struct ir3_register *reg = instr->regs[m];
				if ((flags & IR3_REG_CONST) && (reg->flags & IR3_REG_CONST))
					return false;
				if ((flags & IR3_REG_IMMED) && (reg->flags & IR3_REG_IMMED))
					return false;
			}
		}
		break;
	case 3:
		valid_flags = ir3_cat3_absneg(instr->opc) |
				IR3_REG_CONST | IR3_REG_RELATIV;

		if (flags & ~valid_flags)
			return false;

		if (flags & (IR3_REG_CONST | IR3_REG_RELATIV)) {
			/* cannot deal w/ const/relativ in 2nd src: */
			if (n == 1)
				return false;
		}

		break;
	case 4:
		/* seems like blob compiler avoids const as src.. */
		/* TODO double check if this is still the case on a4xx */
		if (flags & (IR3_REG_CONST | IR3_REG_IMMED))
			return false;
		if (flags & (IR3_REG_SABS | IR3_REG_SNEG))
			return false;
		break;
	case 5:
		/* no flags allowed */
		if (flags)
			return false;
		break;
	case 6:
		valid_flags = IR3_REG_IMMED;
		if (flags & ~valid_flags)
			return false;

		if (flags & IR3_REG_IMMED) {
			/* doesn't seem like we can have immediate src for store
			 * instructions:
			 *
			 * TODO this restriction could also apply to load instructions,
			 * but for load instructions this arg is the address (and not
			 * really sure any good way to test a hard-coded immed addr src)
			 */
			if (is_store(instr) && (n == 1))
				return false;

			if ((instr->opc == OPC_LDL) && (n == 0))
				return false;

			if ((instr->opc == OPC_STL) && (n != 2))
				return false;

			if ((instr->opc == OPC_LDP) && (n == 0))
				return false;

			if ((instr->opc == OPC_STP) && (n != 2))
				return false;

			if (instr->opc == OPC_STLW && n == 0)
				return false;

			if (instr->opc == OPC_LDLW && n == 0)
				return false;

			/* disallow immediates in anything but the SSBO slot argument for
			 * cat6 instructions:
			 */
			if (is_atomic(instr->opc) && (n != 0))
				return false;

			if (is_atomic(instr->opc) && !(instr->flags & IR3_INSTR_G))
				return false;

			if (instr->opc == OPC_STG && (instr->flags & IR3_INSTR_G) && (n != 2))
				return false;

			/* as with atomics, these cat6 instrs can only have an immediate
			 * for SSBO/IBO slot argument
			 */
			switch (instr->opc) {
			case OPC_LDIB:
			case OPC_STIB:
			case OPC_LDC:
			case OPC_RESINFO:
				if (n != 0)
					return false;
				break;
			default:
				break;
			}
		}

		break;
	}

	return true;
}
