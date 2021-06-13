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
#include "agx_builder.h"

/* Trivial register allocator that never frees anything.
 *
 * TODO: Write a real register allocator.
 * TODO: Handle phi nodes.
 */

void
agx_ra(agx_context *ctx)
{
   unsigned *alloc = calloc(ctx->alloc, sizeof(unsigned));
   unsigned usage = 6*2;

   agx_foreach_instr_global_safe(ctx, ins) {
      /* Lower away RA pseudo-instructions */
      if (ins->op == AGX_OPCODE_P_COMBINE) {
         /* TODO: Optimize out the moves! */
         unsigned components = 0;

         for (unsigned i = 0; i < 4; ++i) {
            if (!agx_is_null(ins->src[i]))
               components = i + 1;
         }

         unsigned size = ins->dest[0].size == AGX_SIZE_32 ? 2 : 1;
         if (size == 2 && usage & 1) usage++;
         unsigned base = usage;
         assert(ins->dest[0].type == AGX_INDEX_NORMAL);
         alloc[ins->dest[0].value] = base;
         usage += (components * size);

         /* Move the sources */
         agx_builder b = agx_init_builder(ctx, agx_after_instr(ins));

         for (unsigned i = 0; i < 4; ++i) {
            if (agx_is_null(ins->src[i])) continue;
            assert(ins->src[0].type == AGX_INDEX_NORMAL);
            agx_mov_to(&b, agx_register(base + (i * size), ins->dest[0].size),
                  agx_register(alloc[ins->src[i].value], ins->src[0].size));
         }

         /* We've lowered away, delete the old */
         agx_remove_instruction(ins);
         continue;
      } else if (ins->op == AGX_OPCODE_P_EXTRACT) {
         assert(ins->dest[0].type == AGX_INDEX_NORMAL);
         assert(ins->src[0].type == AGX_INDEX_NORMAL);
         assert(ins->dest[0].size == ins->src[0].size);

         unsigned size = ins->dest[0].size == AGX_SIZE_32 ? 2 : 1;
         alloc[ins->dest[0].value] = alloc[ins->src[0].value] + (size * ins->imm);
         agx_remove_instruction(ins);
         continue;
      }

      agx_foreach_src(ins, s) {
         if (ins->src[s].type == AGX_INDEX_NORMAL) {
            unsigned v = alloc[ins->src[s].value];
            ins->src[s] = agx_replace_index(ins->src[s], agx_register(v, ins->src[s].size));
         }
      }

      agx_foreach_dest(ins, d) {
         if (ins->dest[d].type == AGX_INDEX_NORMAL) {
            unsigned size = ins->dest[d].size == AGX_SIZE_32 ? 2 : 1;
            if (size == 2 && usage & 1) usage++;
            unsigned v = usage;
            unsigned comps = (ins->op == AGX_OPCODE_LD_VARY || ins->op == AGX_OPCODE_DEVICE_LOAD || ins->op == AGX_OPCODE_TEXTURE_SAMPLE) ? 4 : 1; // todo systematic
            usage += comps * size;
            alloc[ins->dest[d].value] = v;
            ins->dest[d] = agx_replace_index(ins->dest[d], agx_register(v, ins->dest[d].size));
         }
      }
   }

   assert(usage < 256 && "dummy RA");
   free(alloc);
}
