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

/** Returns number of registers written by an instruction */
static unsigned
agx_write_registers(agx_instr *I, unsigned d)
{
   unsigned size = I->dest[d].size == AGX_SIZE_32 ? 2 : 1;

   switch (I->op) {
   case AGX_OPCODE_LD_VARY:
   case AGX_OPCODE_DEVICE_LOAD:
   case AGX_OPCODE_TEXTURE_SAMPLE:
   case AGX_OPCODE_LD_TILE:
      return 8;
   case AGX_OPCODE_LD_VARY_FLAT:
      return 6;
   case AGX_OPCODE_P_COMBINE:
   {
      unsigned components = 0;

      for (unsigned i = 0; i < 4; ++i) {
         if (!agx_is_null(I->src[i]))
            components = i + 1;
      }

      return components * size;
   }
   default:
      return size;
   }
}

static unsigned
agx_assign_regs(BITSET_WORD *used_regs, unsigned count, unsigned align, unsigned max)
{
   for (unsigned reg = 0; reg < max; reg += align) {
      bool conflict = false;

      for (unsigned j = 0; j < count; ++j)
         conflict |= BITSET_TEST(used_regs, reg + j);

      if (!conflict) {
         for (unsigned j = 0; j < count; ++j)
            BITSET_SET(used_regs, reg + j);

         return reg;
      }
   }

   /* Couldn't find a free register, dump the state of the register file */
   fprintf(stderr, "Failed to find register of size %u aligned %u max %u.\n",
           count, align, max);

   fprintf(stderr, "Register file:\n");
   for (unsigned i = 0; i < BITSET_WORDS(max); ++i)
      fprintf(stderr, "    %08X\n", used_regs[i]);

   unreachable("Could not find a free register");
}

/** Assign registers to SSA values in a block. */

static void
agx_ra_assign_local(agx_block *block, uint8_t *ssa_to_reg, uint8_t *ncomps, unsigned max_reg)
{
   BITSET_DECLARE(used_regs, AGX_NUM_REGS) = { 0 };

   agx_foreach_predecessor(block, pred) {
      for (unsigned i = 0; i < BITSET_WORDS(AGX_NUM_REGS); ++i)
         used_regs[i] |= pred->regs_out[i];
   }

   BITSET_SET(used_regs, 0); // control flow writes r0l
   BITSET_SET(used_regs, 5*2); // TODO: precolouring, don't overwrite vertex ID
   BITSET_SET(used_regs, (5*2 + 1));
   BITSET_SET(used_regs, (6*2 + 0));
   BITSET_SET(used_regs, (6*2 + 1));

   agx_foreach_instr_in_block(block, I) {
      /* First, free killed sources */
      agx_foreach_src(I, s) {
         if (I->src[s].type == AGX_INDEX_NORMAL && I->src[s].kill) {
            unsigned reg = ssa_to_reg[I->src[s].value];
            unsigned count = ncomps[I->src[s].value];

            for (unsigned i = 0; i < count; ++i)
               BITSET_CLEAR(used_regs, reg + i);
         }
      }

      /* Next, assign destinations. Always legal in SSA form. */
      agx_foreach_dest(I, d) {
         if (I->dest[d].type == AGX_INDEX_NORMAL) {
            unsigned count = agx_write_registers(I, d);
            unsigned align = (I->dest[d].size == AGX_SIZE_16) ? 1 : 2;
            unsigned reg = agx_assign_regs(used_regs, count, align, max_reg);

            ssa_to_reg[I->dest[d].value] = reg;
         }
      }
   }

   STATIC_ASSERT(sizeof(block->regs_out) == sizeof(used_regs));
   memcpy(block->regs_out, used_regs, sizeof(used_regs));
}

void
agx_ra(agx_context *ctx)
{
   unsigned *alloc = calloc(ctx->alloc, sizeof(unsigned));

   agx_compute_liveness(ctx);
   uint8_t *ssa_to_reg = calloc(ctx->alloc, sizeof(uint8_t));
   uint8_t *ncomps = calloc(ctx->alloc, sizeof(uint8_t));

   agx_foreach_instr_global(ctx, I) {
      agx_foreach_dest(I, d) {
         if (I->dest[d].type != AGX_INDEX_NORMAL) continue;

         unsigned v = I->dest[d].value;
         assert(ncomps[v] == 0 && "broken SSA");
         ncomps[v] = agx_write_registers(I, d);
      }
   }

   agx_foreach_block(ctx, block)
      agx_ra_assign_local(block, ssa_to_reg, ncomps, ctx->max_register);

   /* TODO: Coalesce combines */

   agx_foreach_instr_global_safe(ctx, ins) {
      /* Lower away RA pseudo-instructions */
      if (ins->op == AGX_OPCODE_P_COMBINE) {
         /* TODO: Optimize out the moves! */
         assert(ins->dest[0].type == AGX_INDEX_NORMAL);
         enum agx_size common_size = ins->dest[0].size;
         unsigned base = ssa_to_reg[ins->dest[0].value];
         unsigned size = common_size == AGX_SIZE_32 ? 2 : 1;

         /* Move the sources */
         agx_builder b = agx_init_builder(ctx, agx_after_instr(ins));

         /* TODO: Eliminate the intermediate copy by handling parallel copies */
         for (unsigned i = 0; i < 4; ++i) {
            if (agx_is_null(ins->src[i])) continue;
            unsigned base = ins->src[i].value;
            if (ins->src[i].type == AGX_INDEX_NORMAL)
               base = ssa_to_reg[base];
            else
               assert(ins->src[i].type == AGX_INDEX_REGISTER);

            assert(ins->src[i].size == common_size);

            agx_mov_to(&b, agx_register(124*2 + (i * size), common_size),
                  agx_register(base, common_size));
         }

         for (unsigned i = 0; i < 4; ++i) {
            if (agx_is_null(ins->src[i])) continue;
            agx_index src = ins->src[i];

            if (src.type == AGX_INDEX_NORMAL)
               src = agx_register(alloc[src.value], src.size);

            agx_mov_to(&b, agx_register(base + (i * size), common_size),
                  agx_register(124*2 + (i * size), common_size));
         }

         /* We've lowered away, delete the old */
         agx_remove_instruction(ins);
         continue;
      } else if (ins->op == AGX_OPCODE_P_EXTRACT) {
         /* Uses the destination size */
         assert(ins->dest[0].type == AGX_INDEX_NORMAL);
         unsigned base = ins->src[0].value;

         if (ins->src[0].type != AGX_INDEX_REGISTER) {
            assert(ins->src[0].type == AGX_INDEX_NORMAL);
            base = alloc[base];
         }

         unsigned size = ins->dest[0].size == AGX_SIZE_64 ? 4 : ins->dest[0].size == AGX_SIZE_32 ? 2 : 1;
         unsigned left = ssa_to_reg[ins->dest[0].value];
         unsigned right = ssa_to_reg[ins->src[0].value] + (size * ins->imm);

         if (left != right) {
            agx_builder b = agx_init_builder(ctx, agx_after_instr(ins));
            agx_mov_to(&b, agx_register(left, ins->dest[0].size),
                  agx_register(right, ins->src[0].size));
         }

         agx_remove_instruction(ins);
         continue;
      }

      agx_foreach_src(ins, s) {
         if (ins->src[s].type == AGX_INDEX_NORMAL) {
            unsigned v = ssa_to_reg[ins->src[s].value];
            ins->src[s] = agx_replace_index(ins->src[s], agx_register(v, ins->src[s].size));
         }
      }

      agx_foreach_dest(ins, d) {
         if (ins->dest[d].type == AGX_INDEX_NORMAL) {
            unsigned v = ssa_to_reg[ins->dest[d].value];
            ins->dest[d] = agx_replace_index(ins->dest[d], agx_register(v, ins->dest[d].size));
         }
      }
   }

   free(ssa_to_reg);
   free(ncomps);
   free(alloc);
}
