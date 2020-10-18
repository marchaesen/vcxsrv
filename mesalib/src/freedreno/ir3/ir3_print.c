/*
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
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
#include <stdio.h>

#include "ir3.h"

#define PTRID(x) ((unsigned long)(x))

/* ansi escape sequences: */
#define RESET	"\x1b[0m"
#define RED		"\x1b[0;31m"
#define GREEN	"\x1b[0;32m"
#define BLUE	"\x1b[0;34m"
#define MAGENTA	"\x1b[0;35m"

/* syntax coloring, mostly to make it easier to see different sorts of
 * srcs (immediate, constant, ssa, array, ...)
 */
#define SYN_REG(x)		RED x RESET
#define SYN_IMMED(x)	GREEN x RESET
#define SYN_CONST(x)	GREEN x RESET
#define SYN_SSA(x)		BLUE x RESET
#define SYN_ARRAY(x)	MAGENTA x RESET

static const char *
type_name(type_t type)
{
	static const char *type_names[] = {
			[TYPE_F16] = "f16",
			[TYPE_F32] = "f32",
			[TYPE_U16] = "u16",
			[TYPE_U32] = "u32",
			[TYPE_S16] = "s16",
			[TYPE_S32] = "s32",
			[TYPE_U8]  = "u8",
			[TYPE_S8]  = "s8",
	};
	return type_names[type];
}

static void print_instr_name(struct ir3_instruction *instr, bool flags)
{
	if (!instr)
		return;
#ifdef DEBUG
	printf("%04u:", instr->serialno);
#endif
	printf("%04u:", instr->name);
	printf("%04u:", instr->ip);
	if (instr->flags & IR3_INSTR_UNUSED) {
		printf("XXX: ");
	} else {
		printf("%03u: ", instr->use_count);
	}

	if (flags) {
		printf("\t");
		if (instr->flags & IR3_INSTR_SY)
			printf("(sy)");
		if (instr->flags & IR3_INSTR_SS)
			printf("(ss)");
		if (instr->flags & IR3_INSTR_JP)
			printf("(jp)");
		if (instr->repeat)
			printf("(rpt%d)", instr->repeat);
		if (instr->nop)
			printf("(nop%d)", instr->nop);
		if (instr->flags & IR3_INSTR_UL)
			printf("(ul)");
	} else {
		printf(" ");
	}

	if (is_meta(instr)) {
		switch (instr->opc) {
		case OPC_META_INPUT:  printf("_meta:in");   break;
		case OPC_META_SPLIT:        printf("_meta:split");        break;
		case OPC_META_COLLECT:      printf("_meta:collect");      break;
		case OPC_META_TEX_PREFETCH: printf("_meta:tex_prefetch"); break;

		/* shouldn't hit here.. just for debugging: */
		default: printf("_meta:%d", instr->opc);    break;
		}
	} else if (instr->opc == OPC_MOV) {
		if (instr->cat1.src_type == instr->cat1.dst_type)
			printf("mov");
		else
			printf("cov");
		printf(".%s%s", type_name(instr->cat1.src_type),
				type_name(instr->cat1.dst_type));
	} else {
		printf("%s", disasm_a3xx_instr_name(instr->opc));
		if (instr->flags & IR3_INSTR_3D)
			printf(".3d");
		if (instr->flags & IR3_INSTR_A)
			printf(".a");
		if (instr->flags & IR3_INSTR_O)
			printf(".o");
		if (instr->flags & IR3_INSTR_P)
			printf(".p");
		if (instr->flags & IR3_INSTR_S)
			printf(".s");
		if (instr->flags & IR3_INSTR_A1EN)
			printf(".a1en");
		if (instr->opc == OPC_LDC)
			printf(".offset%d", instr->cat6.d);
		if (instr->flags & IR3_INSTR_B) {
			printf(".base%d",
				   is_tex(instr) ? instr->cat5.tex_base : instr->cat6.base);
		}
		if (instr->flags & IR3_INSTR_S2EN)
			printf(".s2en");

		static const char *cond[0x7] = {
				"lt",
				"le",
				"gt",
				"ge",
				"eq",
				"ne",
		};

		switch (instr->opc) {
		case OPC_CMPS_F:
		case OPC_CMPS_U:
		case OPC_CMPS_S:
		case OPC_CMPV_F:
		case OPC_CMPV_U:
		case OPC_CMPV_S:
			printf(".%s", cond[instr->cat2.condition & 0x7]);
			break;
		default:
			break;
		}
	}
}

static void print_reg_name(struct ir3_register *reg)
{
	if ((reg->flags & (IR3_REG_FABS | IR3_REG_SABS)) &&
			(reg->flags & (IR3_REG_FNEG | IR3_REG_SNEG | IR3_REG_BNOT)))
		printf("(absneg)");
	else if (reg->flags & (IR3_REG_FNEG | IR3_REG_SNEG | IR3_REG_BNOT))
		printf("(neg)");
	else if (reg->flags & (IR3_REG_FABS | IR3_REG_SABS))
		printf("(abs)");

	if (reg->flags & IR3_REG_R)
		printf("(r)");

	if (reg->flags & IR3_REG_HIGH)
		printf("H");
	if (reg->flags & IR3_REG_HALF)
		printf("h");

	if (reg->flags & IR3_REG_IMMED) {
		printf(SYN_IMMED("imm[%f,%d,0x%x]"), reg->fim_val, reg->iim_val, reg->iim_val);
	} else if (reg->flags & IR3_REG_ARRAY) {
		printf(SYN_ARRAY("arr[id=%u, offset=%d, size=%u"), reg->array.id,
				reg->array.offset, reg->size);
		/* for ARRAY we could have null src, for example first write
		 * instruction..
		 */
		if (reg->instr) {
			printf(SYN_ARRAY(", "));
			printf(SYN_SSA("_["));
			print_instr_name(reg->instr, false);
			printf(SYN_SSA("]"));
		}
		printf(SYN_ARRAY("]"));
	} else if (reg->flags & IR3_REG_SSA) {
		printf(SYN_SSA("_["));
		print_instr_name(reg->instr, false);
		printf(SYN_SSA("]"));
	} else if (reg->flags & IR3_REG_RELATIV) {
		if (reg->flags & IR3_REG_CONST)
			printf(SYN_CONST("c<a0.x + %d>"), reg->array.offset);
		else
			printf(SYN_REG("r<a0.x + %d>")" (%u)", reg->array.offset, reg->size);
	} else {
		if (reg->flags & IR3_REG_CONST)
			printf(SYN_CONST("c%u.%c"), reg_num(reg), "xyzw"[reg_comp(reg)]);
		else
			printf(SYN_REG("r%u.%c"), reg_num(reg), "xyzw"[reg_comp(reg)]);
	}

	if (reg->wrmask > 0x1)
		printf(" (wrmask=0x%x)", reg->wrmask);
}

static void
tab(int lvl)
{
	for (int i = 0; i < lvl; i++)
		printf("\t");
}

static void
print_instr(struct ir3_instruction *instr, int lvl)
{
	unsigned i;

	tab(lvl);

	print_instr_name(instr, true);

	if (is_tex(instr)) {
		printf(" (%s)(", type_name(instr->cat5.type));
		for (i = 0; i < 4; i++)
			if (instr->regs[0]->wrmask & (1 << i))
				printf("%c", "xyzw"[i]);
		printf(")");
	} else if (instr->regs_count > 0) {
		printf(" ");
	}

	for (i = 0; i < instr->regs_count; i++) {
		struct ir3_register *reg = instr->regs[i];

		printf(i ? ", " : "");
		print_reg_name(reg);
	}

	if (is_tex(instr) && !(instr->flags & IR3_INSTR_S2EN)) {
		if (!!(instr->flags & IR3_INSTR_B)) {
			if (!!(instr->flags & IR3_INSTR_A1EN)) {
				printf(", s#%d", instr->cat5.samp);
			} else {
				printf(", s#%d, t#%d", instr->cat5.samp & 0xf,
					   instr->cat5.samp >> 4);
			}
		} else {
			printf(", s#%d, t#%d", instr->cat5.samp, instr->cat5.tex);
		}
	}

	if (instr->address) {
		printf(", address=_");
		printf("[");
		print_instr_name(instr->address, false);
		printf("]");
	}

	if (instr->cp.left) {
		printf(", left=_");
		printf("[");
		print_instr_name(instr->cp.left, false);
		printf("]");
	}

	if (instr->cp.right) {
		printf(", right=_");
		printf("[");
		print_instr_name(instr->cp.right, false);
		printf("]");
	}

	if (instr->opc == OPC_META_SPLIT) {
		printf(", off=%d", instr->split.off);
	} else if (instr->opc == OPC_META_TEX_PREFETCH) {
		printf(", tex=%d, samp=%d, input_offset=%d", instr->prefetch.tex,
				instr->prefetch.samp, instr->prefetch.input_offset);
	}

	if (is_flow(instr) && instr->cat0.target) {
		/* the predicate register src is implied: */
		if (instr->opc == OPC_B) {
			printf("r %sp0.x", instr->cat0.inv ? "!" : "");
		}
		printf(", target=block%u", block_id(instr->cat0.target));
	}

	if (instr->deps_count) {
		printf(", false-deps:");
		for (unsigned i = 0; i < instr->deps_count; i++) {
			if (i > 0)
				printf(", ");
			printf("_[");
			print_instr_name(instr->deps[i], false);
			printf("]");
		}
	}

	printf("\n");
}

void ir3_print_instr(struct ir3_instruction *instr)
{
	print_instr(instr, 0);
}

static void
print_block(struct ir3_block *block, int lvl)
{
	tab(lvl); printf("block%u {\n", block_id(block));

	/* computerator (ir3 assembler) doesn't really use blocks for flow
	 * control, so block->predecessors will be null.
	 */
	if (block->predecessors && block->predecessors->entries > 0) {
		unsigned i = 0;
		tab(lvl+1);
		printf("pred: ");
		set_foreach (block->predecessors, entry) {
			struct ir3_block *pred = (struct ir3_block *)entry->key;
			if (i++)
				printf(", ");
			printf("block%u", block_id(pred));
		}
		printf("\n");
	}

	foreach_instr (instr, &block->instr_list) {
		print_instr(instr, lvl+1);
	}

	tab(lvl+1); printf("/* keeps:\n");
	for (unsigned i = 0; i < block->keeps_count; i++) {
		print_instr(block->keeps[i], lvl+2);
	}
	tab(lvl+1); printf(" */\n");

	if (block->successors[1]) {
		/* leading into if/else: */
		tab(lvl+1);
		printf("/* succs: if _[");
		print_instr_name(block->condition, false);
		printf("] block%u; else block%u; */\n",
				block_id(block->successors[0]),
				block_id(block->successors[1]));
	} else if (block->successors[0]) {
		tab(lvl+1);
		printf("/* succs: block%u; */\n",
				block_id(block->successors[0]));
	}
	tab(lvl); printf("}\n");
}

void
ir3_print(struct ir3 *ir)
{
	foreach_block (block, &ir->block_list)
		print_block(block, 0);

	foreach_output_n (out, i, ir) {
		printf("out%d: ", i);
		print_instr(out, 0);
	}
}
