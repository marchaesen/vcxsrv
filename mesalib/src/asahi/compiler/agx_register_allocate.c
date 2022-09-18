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

/* SSA-based register allocator */

/** Returns number of registers written by an instruction */
unsigned
agx_write_registers(agx_instr *I, unsigned d)
{
   unsigned size = agx_size_align_16(I->dest[d].size);

   switch (I->op) {
   case AGX_OPCODE_ITER:
      assert(1 <= I->channels && I->channels <= 4);
      return I->channels * size;

   case AGX_OPCODE_DEVICE_LOAD:
   case AGX_OPCODE_TEXTURE_LOAD:
   case AGX_OPCODE_TEXTURE_SAMPLE:
   case AGX_OPCODE_LD_TILE:
      /* TODO: mask */
      return 4 * size;

   case AGX_OPCODE_LDCF:
      return 6;
   case AGX_OPCODE_P_COMBINE:
      return I->nr_srcs * size;
   default:
      return size;
   }
}

static inline enum agx_size
agx_split_width(const agx_instr *I)
{
   enum agx_size width = ~0;

   agx_foreach_dest(I, d) {
      if (agx_is_null(I->dest[d]))
         continue;
      else if (width != ~0)
         assert(width == I->dest[d].size);
      else
         width = I->dest[d].size;
   }

   assert(width != ~0 && "should have been DCE'd");
   return width;
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
agx_ra_assign_local(agx_block *block, uint8_t *ssa_to_reg, uint8_t *ncomps)
{
   BITSET_DECLARE(used_regs, AGX_NUM_REGS) = { 0 };

   agx_foreach_predecessor(block, pred) {
      for (unsigned i = 0; i < BITSET_WORDS(AGX_NUM_REGS); ++i)
         used_regs[i] |= (*pred)->regs_out[i];
   }

   BITSET_SET(used_regs, 0); // control flow writes r0l
   BITSET_SET(used_regs, 5*2); // TODO: precolouring, don't overwrite vertex ID
   BITSET_SET(used_regs, (5*2 + 1));
   BITSET_SET(used_regs, (6*2 + 0));
   BITSET_SET(used_regs, (6*2 + 1));

   agx_foreach_instr_in_block(block, I) {
      /* Optimization: if a split contains the last use of a vector, the split
       * can be removed by assigning the destinations overlapping the source.
       */
      if (I->op == AGX_OPCODE_P_SPLIT && I->src[0].kill) {
         unsigned reg = ssa_to_reg[I->src[0].value];
         unsigned length = ncomps[I->src[0].value];
         unsigned width = agx_size_align_16(agx_split_width(I));
         unsigned count = length / width;

         agx_foreach_dest(I, d) {
            /* Skip excess components */
            if (d >= count) {
               assert(agx_is_null(I->dest[d]));
               continue;
            }

            /* The source of the split is killed. If a destination of the split
             * is null, that channel is killed. Free it.
             */
            if (agx_is_null(I->dest[d])) {
               for (unsigned i = 0; i < width; ++i)
                  BITSET_CLEAR(used_regs, reg + (width * d) + i);

               continue;
            }

            /* Otherwise, transfer the liveness */
            unsigned offset = d * width;

            assert(I->dest[d].type == AGX_INDEX_NORMAL);
            assert(offset < length);

            ssa_to_reg[I->dest[d].value] = reg + offset;
         }

         continue;
      }

      /* First, free killed sources */
      agx_foreach_src(I, s) {
         if (I->src[s].type == AGX_INDEX_NORMAL && I->src[s].kill) {
            unsigned reg = ssa_to_reg[I->src[s].value];
            unsigned count = ncomps[I->src[s].value];

            for (unsigned i = 0; i < count; ++i)
               BITSET_CLEAR(used_regs, reg + i);
         }
      }

      /* Next, assign destinations one at a time. This is always legal
       * because of the SSA form.
       */
      agx_foreach_dest(I, d) {
         if (I->dest[d].type == AGX_INDEX_NORMAL) {
            unsigned count = agx_write_registers(I, d);
            unsigned align = agx_size_align_16(I->dest[d].size);
            unsigned reg = agx_assign_regs(used_regs, count, align, AGX_NUM_REGS);

            ssa_to_reg[I->dest[d].value] = reg;
         }
      }
   }

   STATIC_ASSERT(sizeof(block->regs_out) == sizeof(used_regs));
   memcpy(block->regs_out, used_regs, sizeof(used_regs));
}

/*
 * Resolve an agx_index of type NORMAL or REGISTER to a physical register, once
 * registers have been allocated for all SSA values.
 */
static unsigned
agx_index_to_reg(uint8_t *ssa_to_reg, agx_index idx)
{
   if (idx.type == AGX_INDEX_NORMAL) {
      return ssa_to_reg[idx.value];
   } else {
      assert(idx.type == AGX_INDEX_REGISTER);
      return idx.value;
   }
}

/*
 * Lower phis to parallel copies at the logical end of a given block. If a block
 * needs parallel copies inserted, a successor of the block has a phi node. To
 * have a (nontrivial) phi node, a block must have multiple predecessors. So the
 * edge from the block to the successor (with phi) is not the only edge entering
 * the successor. Because the control flow graph has no critical edges, this
 * edge must therefore be the only edge leaving the block, so the block must
 * have only a single successor.
 */
static void
agx_insert_parallel_copies(agx_context *ctx, agx_block *block)
{
   bool any_succ = false;
   unsigned nr_phi = 0;

   /* Phi nodes logically happen on the control flow edge, so parallel copies
    * are added at the end of the predecessor */
   agx_builder b = agx_init_builder(ctx, agx_after_block_logical(block));

   agx_foreach_successor(block, succ) {
      assert(nr_phi == 0 && "control flow graph has a critical edge");

      /* Phi nodes can only come at the start of the block */
      agx_foreach_instr_in_block(succ, phi) {
         if (phi->op != AGX_OPCODE_PHI) break;

         assert(!any_succ && "control flow graph has a critical edge");
         nr_phi++;
      }

      any_succ = true;

      /* Nothing to do if there are no phi nodes */
      if (nr_phi == 0)
         continue;

      unsigned pred_index = agx_predecessor_index(succ, block);

      /* Create a parallel copy lowering all the phi nodes */
      struct agx_copy *copies = calloc(sizeof(*copies), nr_phi);

      unsigned i = 0;

      agx_foreach_instr_in_block(succ, phi) {
         if (phi->op != AGX_OPCODE_PHI) break;

         agx_index dest = phi->dest[0];
         agx_index src = phi->src[pred_index];

         assert(dest.type == AGX_INDEX_REGISTER);
         assert(src.type == AGX_INDEX_REGISTER);
         assert(dest.size == src.size);

         copies[i++] = (struct agx_copy) {
            .dest = dest.value,
            .src = src.value,
            .size = src.size
         };
      }

      agx_emit_parallel_copies(&b, copies, nr_phi);

      free(copies);
   }
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

   /* Assign registers in dominance-order. This coincides with source-order due
    * to a NIR invariant, so we do not need special handling for this.
    */
   agx_foreach_block(ctx, block) {
      agx_ra_assign_local(block, ssa_to_reg, ncomps);
   }

   agx_foreach_instr_global(ctx, ins) {
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

   agx_foreach_instr_global_safe(ctx, ins) {
      /* Lower away RA pseudo-instructions */
      agx_builder b = agx_init_builder(ctx, agx_after_instr(ins));

      if (ins->op == AGX_OPCODE_P_COMBINE) {
         unsigned base = agx_index_to_reg(ssa_to_reg, ins->dest[0]);
         unsigned width = agx_size_align_16(ins->dest[0].size);

         struct agx_copy *copies = alloca(sizeof(copies[0]) * ins->nr_srcs);
         unsigned n = 0;

         /* Move the sources */
         agx_foreach_src(ins, i) {
            if (agx_is_null(ins->src[i])) continue;
            assert(ins->src[i].size == ins->dest[0].size);

            copies[n++] = (struct agx_copy) {
               .dest = base + (i * width),
               .src = agx_index_to_reg(ssa_to_reg, ins->src[i]) ,
               .size = ins->src[i].size
            };
         }

         agx_emit_parallel_copies(&b, copies, n);
         agx_remove_instruction(ins);
         continue;
      } else if (ins->op == AGX_OPCODE_P_SPLIT) {
         unsigned base = agx_index_to_reg(ssa_to_reg, ins->src[0]);
         unsigned width = agx_size_align_16(agx_split_width(ins));

         struct agx_copy copies[4];
         unsigned n = 0;

         /* Move the sources */
         for (unsigned i = 0; i < 4; ++i) {
            if (agx_is_null(ins->dest[i])) continue;

            copies[n++] = (struct agx_copy) {
               .dest = agx_index_to_reg(ssa_to_reg, ins->dest[i]),
               .src = base + (i * width),
               .size = ins->dest[i].size
            };
         }

         /* Lower away */
         agx_builder b = agx_init_builder(ctx, agx_after_instr(ins));
         agx_emit_parallel_copies(&b, copies, n);
         agx_remove_instruction(ins);
         continue;
      }


   }

   /* Insert parallel copies lowering phi nodes */
   agx_foreach_block(ctx, block) {
      agx_insert_parallel_copies(ctx, block);
   }

   /* Phi nodes can be removed now */
   agx_foreach_instr_global_safe(ctx, I) {
      if (I->op == AGX_OPCODE_PHI || I->op == AGX_OPCODE_P_LOGICAL_END)
         agx_remove_instruction(I);

      /* Remove identity moves */
      if (I->op == AGX_OPCODE_MOV && I->src[0].type == AGX_INDEX_REGISTER &&
          I->dest[0].size == I->src[0].size && I->src[0].value == I->dest[0].value) {

         assert(I->dest[0].type == AGX_INDEX_REGISTER);
         agx_remove_instruction(I);
      }
   }

   free(ssa_to_reg);
   free(ncomps);
   free(alloc);
}
