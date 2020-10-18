/*
 * Copyright (c) 2017 Rob Clark <robdclark@gmail.com>
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

#include <err.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>

#include "afuc.h"
#include "rnn.h"
#include "rnndec.h"
#include "parser.h"
#include "asm.h"

int gpuver;


static struct rnndeccontext *ctx;
static struct rnndb *db;
static struct rnndomain *control_regs;
struct rnndomain *dom[2];


/* bit lame to hard-code max but fw sizes are small */
static struct asm_instruction instructions[0x2000];
static unsigned num_instructions;

static struct asm_label labels[0x512];
static unsigned num_labels;

struct asm_instruction *next_instr(int tok)
{
	struct asm_instruction *ai = &instructions[num_instructions++];
	assert(num_instructions < ARRAY_SIZE(instructions));
	ai->tok = tok;
	return ai;
}

void decl_label(const char *str)
{
	struct asm_label *label = &labels[num_labels++];

	assert(num_labels < ARRAY_SIZE(labels));

	label->offset = num_instructions;
	label->label = str;
}

static int resolve_label(const char *str)
{
	int i;

	for (i = 0; i < num_labels; i++) {
		struct asm_label *label = &labels[i];

		if (!strcmp(str, label->label)) {
			return label->offset;
		}
	}

	fprintf(stderr, "Undeclared label: %s\n", str);
	exit(2);
}

static afuc_opc tok2alu(int tok)
{
	switch (tok) {
	case T_OP_ADD:   return OPC_ADD;
	case T_OP_ADDHI: return OPC_ADDHI;
	case T_OP_SUB:   return OPC_SUB;
	case T_OP_SUBHI: return OPC_SUBHI;
	case T_OP_AND:   return OPC_AND;
	case T_OP_OR:    return OPC_OR;
	case T_OP_XOR:   return OPC_XOR;
	case T_OP_NOT:   return OPC_NOT;
	case T_OP_SHL:   return OPC_SHL;
	case T_OP_USHR:  return OPC_USHR;
	case T_OP_ISHR:  return OPC_ISHR;
	case T_OP_ROT:   return OPC_ROT;
	case T_OP_MUL8:  return OPC_MUL8;
	case T_OP_MIN:   return OPC_MIN;
	case T_OP_MAX:   return OPC_MAX;
	case T_OP_CMP:   return OPC_CMP;
	case T_OP_MSB:   return OPC_MSB;
	default:
		assert(0);
		return -1;
	}
}

static void emit_instructions(int outfd)
{
	int i;

	/* there is an extra 0x00000000 which kernel strips off.. we could
	 * perhaps use it for versioning.
	 */
	i = 0;
	write(outfd, &i, 4);

	for (i = 0; i < num_instructions; i++) {
		struct asm_instruction *ai = &instructions[i];
		afuc_instr instr = {0};
		afuc_opc opc;

		/* special case, 2nd dword is patched up w/ # of instructions
		 * (ie. offset of jmptbl)
		 */
		if (i == 1) {
			assert(ai->is_literal);
			ai->literal &= ~0xffff;
			ai->literal |= num_instructions;
		}

		if (ai->is_literal) {
			write(outfd, &ai->literal, 4);
			continue;
		}

		switch (ai->tok) {
		case T_OP_NOP:
			opc = OPC_NOP;
			if (gpuver >= 6)
				instr.pad = 0x1000000;
			break;
		case T_OP_ADD:
		case T_OP_ADDHI:
		case T_OP_SUB:
		case T_OP_SUBHI:
		case T_OP_AND:
		case T_OP_OR:
		case T_OP_XOR:
		case T_OP_NOT:
		case T_OP_SHL:
		case T_OP_USHR:
		case T_OP_ISHR:
		case T_OP_ROT:
		case T_OP_MUL8:
		case T_OP_MIN:
		case T_OP_MAX:
		case T_OP_CMP:
		case T_OP_MSB:
			if (ai->has_immed) {
				/* MSB overlaps with STORE */
				assert(ai->tok != T_OP_MSB);
				if (ai->xmov) {
					fprintf(stderr, "ALU instruction cannot have immediate and xmov\n");
					exit(1);
				}
				opc = tok2alu(ai->tok);
				instr.alui.dst = ai->dst;
				instr.alui.src = ai->src1;
				instr.alui.uimm = ai->immed;
			} else {
				opc = OPC_ALU;
				instr.alu.dst  = ai->dst;
				instr.alu.src1 = ai->src1;
				instr.alu.src2 = ai->src2;
				instr.alu.xmov = ai->xmov;
				instr.alu.alu = tok2alu(ai->tok);
			}
			break;
		case T_OP_MOV:
			/* move can either be encoded as movi (ie. move w/ immed) or
			 * an alu instruction
			 */
			if ((ai->has_immed || ai->label) && ai->xmov) {
				fprintf(stderr, "ALU instruction cannot have immediate and xmov\n");
				exit(1);
			}
			if (ai->has_immed) {
				opc = OPC_MOVI;
				instr.movi.dst = ai->dst;
				instr.movi.uimm = ai->immed;
				instr.movi.shift = ai->shift;
			} else if (ai->label) {
				/* mov w/ a label is just an alias for an immediate, this
				 * is useful to load the address of a constant table into
				 * a register:
				 */
				opc = OPC_MOVI;
				instr.movi.dst = ai->dst;
				instr.movi.uimm = resolve_label(ai->label);
				instr.movi.shift = ai->shift;
			} else {
				/* encode as: or $dst, $00, $src */
				opc = OPC_ALU;
				instr.alu.dst  = ai->dst;
				instr.alu.src1 = 0x00;      /* $00 reads-back 0 */
				instr.alu.src2 = ai->src1;
				instr.alu.xmov = ai->xmov;
				instr.alu.alu = OPC_OR;
			}
			break;
		case T_OP_CWRITE:
		case T_OP_CREAD:
		case T_OP_STORE:
		case T_OP_LOAD:
			if (gpuver >= 6) {
				if (ai->tok == T_OP_CWRITE) {
					opc = OPC_CWRITE6;
				} else if (ai->tok == T_OP_CREAD) {
					opc = OPC_CREAD6;
				} else if (ai->tok == T_OP_STORE) {
					opc = OPC_STORE6;
				} else if (ai->tok == T_OP_LOAD) {
					opc = OPC_LOAD6;
				}
			} else {
				if (ai->tok == T_OP_CWRITE) {
					opc = OPC_CWRITE5;
				} else if (ai->tok == T_OP_CREAD) {
					opc = OPC_CREAD5;
				} else if (ai->tok == T_OP_STORE ||
					   ai->tok == T_OP_LOAD) {
					fprintf(stderr, "load and store do not exist on a5xx\n");
					exit(1);
				}
			}
			instr.control.src1 = ai->src1;
			instr.control.src2 = ai->src2;
			instr.control.flags = ai->bit;
			instr.control.uimm = ai->immed;
			break;
		case T_OP_BRNE:
		case T_OP_BREQ:
			if (ai->has_immed) {
				opc = (ai->tok == T_OP_BRNE) ? OPC_BRNEI : OPC_BREQI;
				instr.br.bit_or_imm = ai->immed;
			} else {
				opc = (ai->tok == T_OP_BRNE) ? OPC_BRNEB : OPC_BREQB;
				instr.br.bit_or_imm = ai->bit;
			}
			instr.br.src = ai->src1;
			instr.br.ioff = resolve_label(ai->label) - i;
			break;
		case T_OP_RET:
			opc = OPC_RET;
			break;
		case T_OP_IRET:
			opc = OPC_RET;
			instr.ret.interrupt = 1;
			break;
		case T_OP_CALL:
			opc = OPC_CALL;
			instr.call.uoff = resolve_label(ai->label);
			break;
		case T_OP_PREEMPTLEAVE:
			opc = OPC_PREEMPTLEAVE6;
			instr.call.uoff = resolve_label(ai->label);
			break;
		case T_OP_SETSECURE:
			opc = OPC_SETSECURE;
			if (resolve_label(ai->label) != i + 3) {
				fprintf(stderr, "jump label %s is incorrect for setsecure\n", ai->label);
				exit(1);
			}
			if (ai->src1 != 0x2) {
				fprintf(stderr, "source for setsecure must be $02\n");
				exit(1);
			}
			break;
		case T_OP_JUMP:
			/* encode jump as: brne $00, b0, #label */
			opc = OPC_BRNEB;
			instr.br.bit_or_imm = 0;
			instr.br.src = 0x00;       /* $00 reads-back 0.. compare to 0 */
			instr.br.ioff = resolve_label(ai->label) - i;
			break;
		case T_OP_WAITIN:
			opc = OPC_WIN;
			break;
		default:
			assert(0);
		}

		afuc_set_opc(&instr, opc, ai->rep);

		write(outfd, &instr, 4);
	}

}

static int find_enum_val(struct rnnenum *en, const char *name)
{
	int i;

	for (i = 0; i < en->valsnum; i++)
		if (en->vals[i]->valvalid && !strcmp(name, en->vals[i]->name))
			return en->vals[i]->value;

	return -1;
}

static int find_reg(struct rnndomain *dom, const char *name)
{
	int i;

	for (i = 0; i < dom->subelemsnum; i++)
		if (!strcmp(name, dom->subelems[i]->name))
			return dom->subelems[i]->offset;

	return -1;
}

unsigned parse_control_reg(const char *name)
{
	/* skip leading "@" */
	int val = find_reg(control_regs, name + 1);
	if (val < 0) {
		printf("invalid control reg: %s\n", name);
		exit(2);
	}
	return (unsigned)val;
}

static void emit_jumptable(int outfd)
{
	struct rnnenum *en = rnn_findenum(ctx->db, "adreno_pm4_type3_packets");
	uint32_t jmptable[0x80] = {0};
	int i;

	for (i = 0; i < num_labels; i++) {
		struct asm_label *label = &labels[i];
		int id = find_enum_val(en, label->label);

		/* if it doesn't match a known PM4 packet-id, try to match UNKN%d: */
		if (id < 0) {
			if (sscanf(label->label, "UNKN%d", &id) != 1) {
				/* if still not found, must not belong in jump-table: */
				continue;
			}
		}

		jmptable[id] = label->offset;
	}

	write(outfd, jmptable, sizeof(jmptable));
}

static void usage(void)
{
	fprintf(stderr, "Usage:\n"
			"\tasm [-g GPUVER] filename.asm filename.fw\n"
			"\t\t-g - specify GPU version (5, etc)\n"
		);
	exit(2);
}

int main(int argc, char **argv)
{
	FILE *in;
	char *file, *outfile, *name, *control_reg_name;
	int c, ret, outfd;

	/* Argument parsing: */
	while ((c = getopt (argc, argv, "g:")) != -1) {
		switch (c) {
			case 'g':
				gpuver = atoi(optarg);
				break;
			default:
				usage();
		}
	}

	if (optind >= (argc + 1)) {
		fprintf(stderr, "no file specified!\n");
		usage();
	}

	file = argv[optind];
	outfile = argv[optind + 1];

	outfd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (outfd < 0) {
		fprintf(stderr, "could not open \"%s\"\n", outfile);
		usage();
	}

	in = fopen(file, "r");
	if (!in) {
		fprintf(stderr, "could not open \"%s\"\n", file);
		usage();
	}

	yyset_in(in);

	/* if gpu version not specified, infer from filename: */
	if (!gpuver) {
		if (strstr(file, "a5")) {
			gpuver = 5;
		} else if (strstr(file, "a6")) {
			gpuver = 6;
		}
	}

	switch (gpuver) {
	case 6:
		name = "A6XX";
		control_reg_name = "A6XX_CONTROL_REG";
		break;
	case 5:
		name = "A5XX";
		control_reg_name = "A5XX_CONTROL_REG";
		break;
	default:
		fprintf(stderr, "unknown GPU version!\n");
		usage();
	}

	rnn_init();
	db = rnn_newdb();

	ctx = rnndec_newcontext(db);

	rnn_parsefile(db, "adreno.xml");
	rnn_prepdb(db);
	if (db->estatus)
		errx(db->estatus, "failed to parse register database");
	dom[0] = rnn_finddomain(db, name);
	dom[1] = rnn_finddomain(db, "AXXX");
	control_regs = rnn_finddomain(db, control_reg_name);

	ret = yyparse();
	if (ret) {
		fprintf(stderr, "parse failed: %d\n", ret);
		return ret;
	}

	emit_instructions(outfd);
	emit_jumptable(outfd);

	close(outfd);

	return 0;
}
