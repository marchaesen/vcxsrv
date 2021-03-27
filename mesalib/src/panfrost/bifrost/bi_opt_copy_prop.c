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
                && (bi_is_ssa(ins->src[0]) || ins->src[0].type == BI_INDEX_FAU);
}

static inline unsigned
bi_word_node(bi_index idx)
{
        assert(idx.type == BI_INDEX_NORMAL && !idx.reg);
        return (idx.value << 2) | idx.offset;
}

bool
bi_opt_copy_prop(bi_context *ctx)
{
        bool progress = false;

        bi_index *replacement = calloc(sizeof(bi_index), ((ctx->ssa_alloc + 1) << 2));

        bi_foreach_instr_global_safe(ctx, ins) {
                if (bi_is_copy(ins))
                        replacement[bi_word_node(ins->dest[0])] = ins->src[0];

                bi_foreach_src(ins, s) {
                        bi_index use = ins->src[s];

                        if (use.type != BI_INDEX_NORMAL || use.reg) continue;
                        if (bi_count_read_registers(ins, s) != 1) continue;

                        bi_index repl = replacement[bi_word_node(use)];

                        if (!bi_is_null(repl))
                                ins->src[s] = bi_replace_index(ins->src[s], repl);
                }
        }

        free(replacement);
        return progress;
}
