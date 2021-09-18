/*
 * Copyright (C) 2018 Alyssa Rosenzweig
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

#include "compiler.h"

/* A simple scalar-only SSA-based copy-propagation pass. TODO: vectors */

static bool
bi_is_copy(bi_instr *ins)
{
        return (ins->op == BI_OPCODE_MOV_I32) && bi_is_ssa(ins->dest[0])
                && (bi_is_ssa(ins->src[0]) || ins->src[0].type == BI_INDEX_FAU
                                || ins->src[0].type == BI_INDEX_CONSTANT);
}

static bool
bi_reads_fau(bi_instr *ins)
{
        bi_foreach_src(ins, s) {
                if (ins->src[s].type == BI_INDEX_FAU)
                        return true;
        }

        return false;
}

void
bi_opt_copy_prop(bi_context *ctx)
{
        bi_index *replacement = calloc(sizeof(bi_index), ((ctx->ssa_alloc + 1) << 2));

        bi_foreach_instr_global_safe(ctx, ins) {
                if (bi_is_copy(ins)) {
                        bi_index replace = ins->src[0];

                        /* Peek through one layer so copyprop converges in one
                         * iteration for chained moves */
                        if (bi_is_ssa(replace)) {
                                bi_index chained = replacement[bi_word_node(replace)];

                                if (!bi_is_null(chained))
                                        replace = chained;
                        }

                        replacement[bi_word_node(ins->dest[0])] = replace;
                }

                bi_foreach_src(ins, s) {
                        bi_index use = ins->src[s];

                        if (use.type != BI_INDEX_NORMAL || use.reg) continue;
                        if (s == 0 && bi_opcode_props[ins->op].sr_read) continue;

                        bi_index repl = replacement[bi_word_node(use)];

                        if (repl.type == BI_INDEX_CONSTANT && bi_reads_fau(ins))
                                continue;

                        if (!bi_is_null(repl))
                                ins->src[s] = bi_replace_index(ins->src[s], repl);
                }
        }

        free(replacement);
}
