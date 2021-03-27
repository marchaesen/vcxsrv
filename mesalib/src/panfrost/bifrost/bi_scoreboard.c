/*
 * Copyright (C) 2020 Collabora Ltd.
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
 * Authors (Collabora):
 *      Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "compiler.h"

/* Assign dependency slots to each clause and calculate dependencies, This pass
 * must be run after scheduling.
 *
 * 1. A clause that does not produce a message must use the sentinel slot #0
 * 2a. A clause that depends on the results of a previous message-passing
 * instruction must depend on that instruction's dependency slot, unless all
 * reaching code paths already depended on it.
 * 2b. More generally, any dependencies must be encoded. This includes
 * Write-After-Write and Write-After-Read hazards with LOAD/STORE to memory.
 * 3. The shader must wait on slot #6 before running BLEND, ATEST
 * 4. The shader must wait on slot #7 before running BLEND, ST_TILE
 * 5. ATEST, ZS_EMIT must be issued with slot #0
 * 6. BARRIER must be issued with slot #7
 * 7. Only slots #0 through #5 may be used for clauses not otherwise specified.
 * 8. If a clause writes to a read staging register of an unresolved
 * dependency, it must set a staging barrier.
 *
 * Note it _is_ legal to reuse slots for multiple message passing instructions
 * with overlapping liveness, albeit with a slight performance penalty. As such
 * the problem is significantly easier than register allocation, rather than
 * spilling we may simply reuse slots. (TODO: does this have an optimal
 * linear-time solution).
 *
 * Within these constraints we are free to assign slots as we like. This pass
 * attempts to minimize stalls (TODO).
 */

#define BI_NUM_GENERAL_SLOTS 6

/* A model for the state of the scoreboard */

struct bi_scoreboard_state {
        /* TODO: what do we track here for a heuristic? */
};

/* Given a scoreboard model, choose a slot for a clause wrapping a given
 * message passing instruction. No side effects. */

static unsigned
bi_choose_scoreboard_slot(struct bi_scoreboard_state *st, bi_instr *message)
{
        /* A clause that does not produce a message must use slot #0 */
        if (!message)
                return 0;

        switch (message->op) {
        /* ATEST, ZS_EMIT must be issued with slot #0 */
        case BI_OPCODE_ATEST:
        case BI_OPCODE_ZS_EMIT:
                return 0;

        /* BARRIER must be issued with slot #7 */
        case BI_OPCODE_BARRIER:
                return 7;

        default:
                break;
        }

        /* TODO: Use a heuristic */
        return 0;
}

void
bi_assign_scoreboard(bi_context *ctx)
{
        struct bi_scoreboard_state st = {};

        /* Assign slots */
        bi_foreach_block(ctx, _block) {
                bi_block *block = (bi_block *) _block;

                bi_foreach_clause_in_block(block, clause) {
                        unsigned slot = bi_choose_scoreboard_slot(&st, clause->message);
                        clause->scoreboard_id = slot;

                        bi_clause *next = bi_next_clause(ctx, _block, clause);
                        if (next)
                                next->dependencies |= (1 << slot);
                }
        }
}
