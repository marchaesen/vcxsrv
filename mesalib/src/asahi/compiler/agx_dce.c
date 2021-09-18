/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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

#include "agx_compiler.h"

/* SSA-based scalar dead code elimination */

void
agx_dce(agx_context *ctx)
{
   BITSET_WORD *seen = calloc(BITSET_WORDS(ctx->alloc), sizeof(BITSET_WORD));

   agx_foreach_instr_global_safe_rev(ctx, I) {
      if (agx_opcodes_info[I->op].can_eliminate) {
         bool needed = false;

         agx_foreach_dest(I, d) {
            if (I->dest[d].type == AGX_INDEX_NORMAL)
               needed |= BITSET_TEST(seen, I->dest[d].value);
            else if (I->dest[d].type != AGX_INDEX_NULL)
               needed = true;
         }

         if (!needed) {
            agx_remove_instruction(I);
            continue;
         }
      }

      agx_foreach_src(I, s) {
         if (I->src[s].type == AGX_INDEX_NORMAL)
            BITSET_SET(seen, I->src[s].value);
      }
   }

   free(seen);
}
