/*
 * Copyright (C) 2019 Connor Abbott <cwabbott0@gmail.com>
 * Copyright (C) 2019 Lyude Paul <thatslyude@gmail.com>
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
 * Copyright (C) 2019-2020 Collabora, Ltd.
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

#include "bi_print.h"

const char *
bi_clause_type_name(enum bifrost_clause_type T)
{
        switch (T) {
        case BIFROST_CLAUSE_NONE: return "";
        case BIFROST_CLAUSE_LOAD_VARY: return "load_vary";
        case BIFROST_CLAUSE_UBO: return "ubo";
        case BIFROST_CLAUSE_TEX: return "tex";
        case BIFROST_CLAUSE_SSBO_LOAD: return "load";
        case BIFROST_CLAUSE_SSBO_STORE: return "store";
        case BIFROST_CLAUSE_BLEND: return "blend";
        case BIFROST_CLAUSE_ATEST: return "atest";
        case BIFROST_CLAUSE_64BIT: return "64";
        default: return "??";
        }
}

const char *
bi_output_mod_name(enum bifrost_outmod mod)
{
        switch (mod) {
        case BIFROST_NONE: return "";
        case BIFROST_POS: return ".pos";
        case BIFROST_SAT_SIGNED: return ".sat_signed";
        case BIFROST_SAT: return ".sat";
        default: return "invalid";
        }
}

const char *
bi_minmax_mode_name(enum bifrost_minmax_mode mod)
{
        switch (mod) {
        case BIFROST_MINMAX_NONE: return "";
        case BIFROST_NAN_WINS: return ".nan_wins";
        case BIFROST_SRC1_WINS: return ".src1_wins";
        case BIFROST_SRC0_WINS: return ".src0_wins";
        default: return "invalid";
        }
}

const char *
bi_round_mode_name(enum bifrost_roundmode mod)
{
        switch (mod) {
        case BIFROST_RTE: return "";
        case BIFROST_RTP: return ".rtp";
        case BIFROST_RTN: return ".rtn";
        case BIFROST_RTZ: return ".rtz";
        default: return "invalid";
        }
}

const char *
bi_csel_cond_name(enum bifrost_csel_cond cond)
{
        switch (cond) {
        case BIFROST_FEQ_F: return "feq.f";
        case BIFROST_FGT_F: return "fgt.f";
        case BIFROST_FGE_F: return "fge.f";
        case BIFROST_IEQ_F: return "ieq.f";
        case BIFROST_IGT_I: return "igt.i";
        case BIFROST_IGE_I: return "uge.i";
        case BIFROST_UGT_I: return "ugt.i";
        case BIFROST_UGE_I: return "uge.i";
        default: return "invalid";
        }
}

const char *
bi_interp_mode_name(enum bifrost_interp_mode mode)
{
        switch (mode) {
        case BIFROST_INTERP_PER_FRAG: return ".per_frag";
        case BIFROST_INTERP_CENTROID: return ".centroid";
        case BIFROST_INTERP_DEFAULT: return "";
        case BIFROST_INTERP_EXPLICIT: return ".explicit";
        default: return ".unknown";
        }
}

const char *
bi_ldst_type_name(enum bifrost_ldst_type type)
{
        switch (type) {
        case BIFROST_LDST_F16: return "f16";
        case BIFROST_LDST_F32: return "f32";
        case BIFROST_LDST_I32: return "i32";
        case BIFROST_LDST_U32: return "u32";
        default: return "invalid";
        }
}

/* The remaining functions in this file are for IR-internal
 * structures; the disassembler doesn't use them */

static const char *
bi_class_name(enum bi_class cl)
{
        switch (cl) {
        case BI_ADD: return "add";
        case BI_ATEST: return "atest";
        case BI_BRANCH: return "branch";
        case BI_CMP: return "cmp";
        case BI_BLEND: return "blend";
        case BI_BITWISE: return "bitwise";
        case BI_COMBINE: return "combine";
        case BI_CONVERT: return "convert";
        case BI_CSEL: return "csel";
        case BI_DISCARD: return "discard";
        case BI_FMA: return "fma";
        case BI_FMOV: return "fmov";
        case BI_FREXP: return "frexp";
        case BI_ISUB: return "isub";
        case BI_LOAD: return "load";
        case BI_LOAD_UNIFORM: return "load_uniform";
        case BI_LOAD_ATTR: return "load_attr";
        case BI_LOAD_VAR: return "load_var";
        case BI_LOAD_VAR_ADDRESS: return "load_var_address";
        case BI_MINMAX: return "minmax";
        case BI_MOV: return "mov";
        case BI_SHIFT: return "shift";
        case BI_STORE: return "store";
        case BI_STORE_VAR: return "store_var";
        case BI_SPECIAL: return "special";
        case BI_SWIZZLE: return "swizzle";
        case BI_TEX: return "tex";
        case BI_ROUND: return "round";
        default: return "unknown_class";
        }
}

static void
bi_print_index(FILE *fp, bi_instruction *ins, unsigned index)
{
        if (!index)
                fprintf(fp, "_");
        else if (index & BIR_INDEX_REGISTER)
                fprintf(fp, "br%u", index & ~BIR_INDEX_REGISTER);
        else if (index & BIR_INDEX_UNIFORM)
                fprintf(fp, "u%u", index & ~BIR_INDEX_UNIFORM);
        else if (index & BIR_INDEX_CONSTANT)
                fprintf(fp, "#0x%" PRIx64, bi_get_immediate(ins, index));
        else if (index & BIR_INDEX_ZERO)
                fprintf(fp, "#0");
        else if (index & BIR_IS_REG)
                fprintf(fp, "r%u", index >> 1);
        else
                fprintf(fp, "%u", (index >> 1) - 1);
}

static void
bi_print_src(FILE *fp, bi_instruction *ins, unsigned s)
{
        unsigned src = ins->src[s];
        bool mods = bi_has_source_mods(ins);
        bool abs = ins->src_abs[s] && mods;
        bool neg = ins->src_neg[s] && mods;

        if (neg)
                fprintf(fp, "-");

        if (abs)
                fprintf(fp, "abs(");

        bi_print_index(fp, ins, src);

        if (abs)
                fprintf(fp, ")");
}

/* Prints a NIR ALU type in Bifrost-style ".f32" ".i8" etc */

static void
bi_print_alu_type(nir_alu_type t, FILE *fp)
{
        unsigned size = nir_alu_type_get_type_size(t);
        nir_alu_type base = nir_alu_type_get_base_type(t);

        switch (base) {
        case nir_type_int:
                fprintf(fp, ".i");
                break;
        case nir_type_uint:
                fprintf(fp, ".u");
                break;
        case nir_type_bool:
                fprintf(fp, ".b");
                break;
        case nir_type_float:
                fprintf(fp, ".f");
                break;
        default:
                fprintf(fp, ".unknown");
                break;
        }

        fprintf(fp, "%u", size);
}

static void
bi_print_swizzle(bi_instruction *ins, unsigned src, FILE *fp)
{
        unsigned size = nir_alu_type_get_type_size(ins->dest_type);
        unsigned count = (size == 64) ? 1 : (32 / size);

        fprintf(fp, ".");

        for (unsigned u = 0; u < count; ++u) {
                assert(ins->swizzle[src][u] < 4);
                fputc("xyzw"[ins->swizzle[src][u]], fp);
        }
}

static const char *
bi_bitwise_op_name(enum bi_bitwise_op op)
{
        switch (op) {
        case BI_BITWISE_AND: return "and";
        case BI_BITWISE_OR: return "or";
        case BI_BITWISE_XOR: return "xor";
        default: return "invalid";
        }
}

static const char *
bi_special_op_name(enum bi_special_op op)
{
        switch (op) {
        case BI_SPECIAL_FRCP: return "frcp";
        case BI_SPECIAL_FRSQ: return "frsq";
        case BI_SPECIAL_FATAN: return "fatan";
        case BI_SPECIAL_FSIN: return "fsin";
        case BI_SPECIAL_FCOS: return "fcos";
        case BI_SPECIAL_FEXP: return "fexp";
        case BI_SPECIAL_FLOG2: return "flog2";
        case BI_SPECIAL_FLOGE: return "flog";
        default: return "invalid";
        }
}

static void
bi_print_load_vary(struct bi_load_vary *load, FILE *fp)
{
        fprintf(fp, "%s", bi_interp_mode_name(load->interp_mode));

        if (load->reuse)
                fprintf(fp, ".reuse");

        if (load->flat)
                fprintf(fp, ".flat");
}

static const char *
bi_cond_name(enum bi_cond cond)
{
        switch (cond) {
        case BI_COND_ALWAYS: return "always";
        case BI_COND_LT: return "lt";
        case BI_COND_LE: return "le";
        case BI_COND_GE: return "ge";
        case BI_COND_GT: return "gt";
        case BI_COND_EQ: return "eq";
        case BI_COND_NE: return "ne";
        default: return "invalid";
        }
}

static void
bi_print_branch(struct bi_branch *branch, FILE *fp)
{
        fprintf(fp, ".%s", bi_cond_name(branch->cond));
}

static void
bi_print_writemask(bi_instruction *ins, FILE *fp)
{
        unsigned bits_per_comp = nir_alu_type_get_type_size(ins->dest_type);
        assert(bits_per_comp);
        unsigned bytes_per_comp = bits_per_comp / 8;
        unsigned comps = 16 / bytes_per_comp;
        unsigned smask = (1 << bytes_per_comp) - 1;
        fprintf(fp, ".");

        for (unsigned i = 0; i < comps; ++i) {
                unsigned masked = (ins->writemask >> (i * bytes_per_comp)) & smask;
                if (!masked)
                        continue;

                assert(masked == smask);
                assert(i < 4);
                fputc("xyzw"[i], fp);
        }
}

void
bi_print_instruction(bi_instruction *ins, FILE *fp)
{
        if (ins->type == BI_MINMAX)
                fprintf(fp, "%s", ins->op.minmax == BI_MINMAX_MIN ? "min" : "max");
        else if (ins->type == BI_BITWISE)
                fprintf(fp, "%s", bi_bitwise_op_name(ins->op.bitwise));
        else if (ins->type == BI_ROUND)
                fprintf(fp, ins->op.round == BI_ROUND_MODE ? "roundMode": "round");
        else if (ins->type == BI_SPECIAL)
                fprintf(fp, "%s", bi_special_op_name(ins->op.special));
        else if (ins->type == BI_CMP)
                fprintf(fp, "%s", bi_cond_name(ins->op.compare));
        else
                fprintf(fp, "%s", bi_class_name(ins->type));

        if (ins->type == BI_MINMAX)
                fprintf(fp, "%s", bi_minmax_mode_name(ins->minmax));
        else if (ins->type == BI_LOAD_VAR)
                bi_print_load_vary(&ins->load_vary, fp);
        else if (ins->type == BI_BRANCH)
                bi_print_branch(&ins->branch, fp);
        else if (ins->type == BI_CSEL)
                fprintf(fp, ".%s", bi_cond_name(ins->csel_cond));
        else if (ins->type == BI_BLEND)
                fprintf(fp, ".loc%u", ins->blend_location);
        else if (ins->type == BI_STORE || ins->type == BI_STORE_VAR)
                fprintf(fp, ".v%u", ins->store_channels);

        if (ins->dest)
                bi_print_alu_type(ins->dest_type, fp);

        if (bi_has_outmod(ins))
                fprintf(fp, "%s", bi_output_mod_name(ins->outmod));

        if (bi_class_props[ins->type] & BI_ROUNDMODE)
                fprintf(fp, "%s", bi_round_mode_name(ins->roundmode));

        fprintf(fp, " ");
        bi_print_index(fp, ins, ins->dest);

        if (ins->dest)
                bi_print_writemask(ins, fp);

        fprintf(fp, ", ");

        bi_foreach_src(ins, s) {
                bi_print_src(fp, ins, s);

                if (ins->src[s] && !(ins->src[s] & (BIR_INDEX_CONSTANT | BIR_INDEX_ZERO))) {
                        bi_print_alu_type(ins->src_types[s], fp);
                        bi_print_swizzle(ins, s, fp);
                }

                if (s < BIR_SRC_COUNT)
                        fprintf(fp, ", ");
        }

        if (ins->type == BI_BRANCH) {
                if (ins->branch.target)
                        fprintf(fp, "-> block%u", ins->branch.target->base.name);
                else
                        fprintf(fp, "-> blockhole");
        }

        fprintf(fp, "\n");
}

void
bi_print_bundle(bi_bundle *bundle, FILE *fp)
{
        bi_instruction *ins[2] = { bundle->fma, bundle->add };

        for (unsigned i = 0; i < 2; ++i) {
                if (ins[i])
                        bi_print_instruction(ins[i], fp);
                else
                        fprintf(fp, "nop\n");
        }
}

void
bi_print_clause(bi_clause *clause, FILE *fp)
{
        fprintf(fp, "\tid(%u)", clause->scoreboard_id);

        if (clause->dependencies) {
                fprintf(fp, ", wait(");

                for (unsigned i = 0; i < 8; ++i) {
                        if (clause->dependencies & (1 << i))
                                fprintf(fp, "%u ", i);
                }

                fprintf(fp, ")");
        }

        if (!clause->back_to_back)
                fprintf(fp, " nbb %s", clause->branch_conditional ? "branch-cond" : "branch-uncond");

        if (clause->data_register_write_barrier)
                fprintf(fp, " drwb");

        fprintf(fp, "\n");

        if (clause->instruction_count) {
                assert(!clause->bundle_count);

                for (unsigned i = 0; i < clause->instruction_count; ++i)
                        bi_print_instruction(clause->instructions[i], fp);
        } else {
                assert(clause->bundle_count);

                for (unsigned i = 0; i < clause->bundle_count; ++i)
                        bi_print_bundle(&clause->bundles[i], fp);
        }

        if (clause->constant_count) {
                for (unsigned i = 0; i < clause->constant_count; ++i)
                        fprintf(fp, "%" PRIx64 " ", clause->constants[i]);

                fprintf(fp, "\n");
        }
}

void
bi_print_block(bi_block *block, FILE *fp)
{
        fprintf(fp, "block%u {\n", block->base.name);

        if (block->scheduled) {
                bi_foreach_clause_in_block(block, clause)
                        bi_print_clause(clause, fp);
        } else {
                bi_foreach_instr_in_block(block, ins)
                        bi_print_instruction(ins, fp);
        }

        fprintf(fp, "}");

        if (block->base.successors[0]) {
                fprintf(fp, " -> ");

                pan_foreach_successor((&block->base), succ)
                        fprintf(fp, "block%u ", succ->name);
        }

        if (block->base.predecessors->entries) {
                fprintf(fp, " from");

                bi_foreach_predecessor(block, pred)
                        fprintf(fp, " block%u", pred->base.name);
        }

        fprintf(fp, "\n\n");
}

void
bi_print_shader(bi_context *ctx, FILE *fp)
{
        bi_foreach_block(ctx, block)
                bi_print_block((bi_block *) block, fp);
}
