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
#include "bi_print_common.h"

static const char *
bi_segment_name(enum bi_segment seg)
{
        switch (seg) {
        case BI_SEGMENT_NONE: return "global";
        case BI_SEGMENT_WLS:  return "wls";
        case BI_SEGMENT_UBO:  return "ubo";
        case BI_SEGMENT_TLS:  return "tls";
        default: return "invalid";
        }
}

const char *
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
        case BI_IMATH: return "imath";
        case BI_LOAD: return "load";
        case BI_LOAD_UNIFORM: return "load_uniform";
        case BI_LOAD_ATTR: return "load_attr";
        case BI_LOAD_VAR: return "load_var";
        case BI_LOAD_VAR_ADDRESS: return "load_var_address";
        case BI_LOAD_TILE: return "load_tile";
        case BI_MINMAX: return "minmax";
        case BI_MOV: return "mov";
        case BI_SELECT: return "select";
        case BI_STORE: return "store";
        case BI_STORE_VAR: return "store_var";
        case BI_SPECIAL_ADD: return "special";
        case BI_SPECIAL_FMA: return "special";
        case BI_TABLE: return "table";
        case BI_TEXS: return "texs";
        case BI_TEXC: return "texc";
        case BI_TEXC_DUAL: return "texc_dual";
        case BI_ROUND: return "round";
        case BI_IMUL: return "imul";
        case BI_ZS_EMIT: return "zs_emit";
        default: return "unknown_class";
        }
}

static bool
bi_print_dest_index(FILE *fp, bi_instruction *ins, unsigned index)
{
        if ((index & BIR_SPECIAL) && (index & BIR_SPECIAL) != BIR_INDEX_REGISTER)
                return false;

        if (!index)
                fprintf(fp, "_");
        else if (index & BIR_INDEX_REGISTER)
                fprintf(fp, "br%u", index & ~BIR_INDEX_REGISTER);
        else if (index & PAN_IS_REG)
                fprintf(fp, "r%u", index >> 1);
        else
                fprintf(fp, "%u", (index >> 1) - 1);

        return true;
}

static const char *
bir_fau_name(unsigned fau_idx)
{
        const char *names[] = {
                "zero", "lane-id", "wrap-id", "core-id",
                "fb-extent", "atest-param", "sample-pos"
        };

        assert(fau_idx < ARRAY_SIZE(names));
        return names[fau_idx];
}

static void
bi_print_index(FILE *fp, bi_instruction *ins, unsigned index, unsigned s)
{
        if (bi_print_dest_index(fp, ins, index))
                return;

        if (index & BIR_INDEX_UNIFORM)
                fprintf(fp, "u%u", index & ~BIR_INDEX_UNIFORM);
        else if (index & BIR_INDEX_CONSTANT)
                fprintf(fp, "#0x%" PRIx64, bi_get_immediate(ins, s));
        else if (index & BIR_INDEX_ZERO)
                fprintf(fp, "#0");
        else if (index & BIR_INDEX_BLEND)
                fprintf(fp, "blend_descriptor_%u.%c", ins->blend_location,
                        (index & ~BIR_INDEX_BLEND) == BIFROST_SRC_FAU_HI ? 'y' : 'x');
        else if (index & BIR_INDEX_FAU)
                fprintf(fp, "%s.%c", bir_fau_name(index & BIR_FAU_TYPE_MASK),
                        (index & BIR_FAU_HI) ? 'y' : 'x');
        else
                fprintf(fp, "#err");
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

        bi_print_index(fp, ins, src, s);

        if (ins->type == BI_BITWISE && s == 1 && ins->bitwise.src1_invert) {
                /* For XOR, just use the destination invert */
                assert(ins->op.bitwise != BI_BITWISE_XOR);
                fprintf(fp, ".not");
        }

        if (abs)
                fprintf(fp, ")");
}

static void
bi_print_swizzle(bi_instruction *ins, unsigned src, FILE *fp)
{
        fprintf(fp, ".");

        for (unsigned u = 0; u < bi_get_component_count(ins, src); ++u) {
                assert(ins->swizzle[src][u] < 16);
                fputc("xyzwefghijklmnop"[ins->swizzle[src][u]], fp);
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
bi_imath_op_name(enum bi_imath_op op)
{
        switch (op) {
        case BI_IMATH_ADD: return "iadd";
        case BI_IMATH_SUB: return "isub";
        default: return "invalid";
        }
}

const char *
bi_table_op_name(enum bi_table_op op)
{
        switch (op) {
        case BI_TABLE_LOG2_U_OVER_U_1_LOW: return "log2.help";
        default: return "invalid";
        }
}

const char *
bi_special_op_name(enum bi_special_op op)
{
        switch (op) {
        case BI_SPECIAL_FRCP: return "frcp";
        case BI_SPECIAL_FRSQ: return "frsq";
        case BI_SPECIAL_EXP2_LOW: return "exp2_low";
        case BI_SPECIAL_CUBEFACE1: return "cubeface1";
        case BI_SPECIAL_CUBEFACE2: return "cubeface2";
        case BI_SPECIAL_CUBE_SSEL: return "cube_ssel";
        case BI_SPECIAL_CUBE_TSEL: return "cube_tsel";
        case BI_SPECIAL_CLPER_V6: return "clper_v6";
        case BI_SPECIAL_CLPER_V7: return "clper_v7";
        default: return "invalid";
        }
}

const char *
bi_reduce_op_name(enum bi_reduce_op op)
{
        switch (op) {
        case BI_REDUCE_ADD_FREXPM: return "add_frexpm";
        default: return "invalid";
        }
}

const char *
bi_frexp_op_name(enum bi_frexp_op op)
{
        switch (op) {
        case BI_FREXPE_LOG: return "frexpe_log";
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

const char *
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
bi_print_texture(struct bi_texture *tex, FILE *fp)
{
        fprintf(fp, " - texture %u, sampler %u%s",
                        tex->texture_index, tex->sampler_index,
                        tex->compute_lod ? ", compute lod" : "");
}

void
bi_print_instruction(bi_instruction *ins, FILE *fp)
{
        if (ins->type == BI_MINMAX)
                fprintf(fp, "%s", ins->op.minmax == BI_MINMAX_MIN ? "min" : "max");
        else if (ins->type == BI_BITWISE)
                fprintf(fp, "%s", bi_bitwise_op_name(ins->op.bitwise));
        else if (ins->type == BI_IMATH)
                fprintf(fp, "%s", bi_imath_op_name(ins->op.imath));
        else if (ins->type == BI_SPECIAL_ADD || ins->type == BI_SPECIAL_FMA)
                fprintf(fp, "%s", bi_special_op_name(ins->op.special));
        else if (ins->type == BI_TABLE)
                fprintf(fp, "%s", bi_table_op_name(ins->op.table));
        else if (ins->type == BI_REDUCE_FMA)
                fprintf(fp, "%s", bi_reduce_op_name(ins->op.reduce));
        else if (ins->type == BI_FREXP)
                fprintf(fp, "%s", bi_frexp_op_name(ins->op.frexp));
        else
                fprintf(fp, "%s", bi_class_name(ins->type));

        if ((ins->type == BI_ADD || ins->type == BI_FMA) && ins->op.mscale)
                fprintf(fp, ".mscale");

        if (ins->type == BI_MINMAX)
                fprintf(fp, "%s", bi_minmax_mode_name(ins->minmax));
        else if (ins->type == BI_LOAD_VAR)
                bi_print_load_vary(&ins->load_vary, fp);
        else if (ins->type == BI_BLEND)
                fprintf(fp, ".loc%u", ins->blend_location);
        else if (ins->type == BI_BITWISE)
                fprintf(fp, ".%cshift", ins->bitwise.rshift ? 'r' : 'l');

        if (bi_class_props[ins->type] & BI_CONDITIONAL)
                fprintf(fp, ".%s", bi_cond_name(ins->cond));

        if (ins->skip)
                fprintf(fp, ".skip");

        if (ins->no_spill)
                fprintf(fp, ".no_spill");

        if (ins->vector_channels)
                fprintf(fp, ".v%u", ins->vector_channels);

        if (ins->segment)
                fprintf(fp, ".%s", bi_segment_name(ins->segment));

        if (ins->dest)
                pan_print_alu_type(ins->dest_type, fp);

        if (ins->format && ins->dest != ins->format)
                pan_print_alu_type(ins->format, fp);

        if (bi_has_outmod(ins))
                fprintf(fp, "%s", bi_output_mod_name(ins->outmod));

        if (bi_class_props[ins->type] & BI_ROUNDMODE)
                fprintf(fp, "%s", bi_round_mode_name(ins->roundmode));

        if (ins->type == BI_BITWISE && ins->bitwise.dest_invert)
                fprintf(fp, ".not");

        fprintf(fp, " ");
        ASSERTED bool succ = bi_print_dest_index(fp, ins, ins->dest);
        assert(succ);

        if (ins->dest_offset)
                fprintf(fp, "+%u", ins->dest_offset);

        fprintf(fp, ", ");

        bi_foreach_src(ins, s) {
                bi_print_src(fp, ins, s);

                if (ins->src[s] &&
                    !(ins->src[s] &
                      (BIR_INDEX_CONSTANT | BIR_INDEX_ZERO | BIR_INDEX_FAU))) {
                        pan_print_alu_type(ins->src_types[s], fp);
                        bi_print_swizzle(ins, s, fp);
                }

                if (s < BIR_SRC_COUNT)
                        fprintf(fp, ", ");
        }

        if (ins->type == BI_BRANCH) {
                if (ins->branch_target) {
                        fprintf(fp, "-> block%u", ins->branch_target->base.name);
                } else {
                        fprintf(fp, "-> void");
                }
        } else if (ins->type == BI_TEXS) {
                bi_print_texture(&ins->texture, fp);
        }

        fprintf(fp, "\n");
}

static const char *
bi_reg_op_name(enum bifrost_reg_op op)
{
        switch (op) {
        case BIFROST_OP_IDLE: return "idle";
        case BIFROST_OP_READ: return "read";
        case BIFROST_OP_WRITE: return "write";
        case BIFROST_OP_WRITE_LO: return "write lo";
        case BIFROST_OP_WRITE_HI: return "write hi";
        default: return "invalid";
        }
}

void
bi_print_slots(bi_registers *regs, FILE *fp)
{
        for (unsigned i = 0; i < 2; ++i) {
                if (regs->enabled[i])
                        fprintf(fp, "slot %u: %u\n", i, regs->slot[i]);
        }

        if (regs->slot23.slot2) {
                fprintf(fp, "slot 2 (%s%s): %u\n",
                                bi_reg_op_name(regs->slot23.slot2),
                                regs->slot23.slot2 >= BIFROST_OP_WRITE ?
                                        " FMA": "",
                                regs->slot[2]);
        }

        if (regs->slot23.slot3) {
                fprintf(fp, "slot 3 (%s %s): %u\n",
                                bi_reg_op_name(regs->slot23.slot3),
                                regs->slot23.slot3_fma ? "FMA" : "ADD",
                                regs->slot[3]);
        }
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

        fprintf(fp, " %s", bi_flow_control_name(clause->flow_control));

        if (!clause->next_clause_prefetch)
               fprintf(fp, " no_prefetch");

        if (clause->staging_barrier)
                fprintf(fp, " osrb");

        fprintf(fp, "\n");

        for (unsigned i = 0; i < clause->bundle_count; ++i)
                bi_print_bundle(&clause->bundles[i], fp);

        if (clause->constant_count) {
                for (unsigned i = 0; i < clause->constant_count; ++i)
                        fprintf(fp, "%" PRIx64 " ", clause->constants[i]);

                if (clause->branch_constant)
                        fprintf(fp, "*");

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
