/*
 * Copyright (C) 2021 Collabora, Ltd.
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
#include "util/u_memory.h"

/* Validatation doesn't make sense in release builds */
#ifndef NDEBUG

/* Validate that all sources are initialized in all read components. This is
 * required for correct register allocation. We check a weaker condition, that
 * all sources that are read are written at some point (equivalently, the live
 * set is empty at the start of the program). TODO: Strengthen */

bool
bi_validate_initialization(bi_context *ctx)
{
        bool success = true;

        /* Calculate the live set */
        bi_block *entry = bi_entry_block(ctx);
        unsigned temp_count = bi_max_temp(ctx);
        bi_invalidate_liveness(ctx);
        bi_compute_liveness(ctx);

        /* Validate that the live set is indeed empty */
        for (unsigned i = 0; i < temp_count; ++i) {
                if (entry->live_in[i] == 0) continue;

                fprintf(stderr, "%s%u\n", (i & PAN_IS_REG) ? "r" : "", i >> 1);
                success = false;
        }

        return success;
}

void
bi_validate(bi_context *ctx, const char *after)
{
        bool fail = false;

        if (bifrost_debug & BIFROST_DBG_NOVALIDATE)
                return;

        if (!bi_validate_initialization(ctx)) {
                fprintf(stderr, "Uninitialized data read after %s\n", after);
                fail = true;
        }

        /* TODO: Validate more invariants */

        if (fail) {
                bi_print_shader(ctx, stderr);
                exit(1);
        }
}

#endif /* NDEBUG */
