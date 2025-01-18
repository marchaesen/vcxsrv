/*
 * Copyright 2024 Alyssa Rosenzweig
 */

#include "agx_compiler.h"
#include "agx_opcodes.h"
#include "nir.h"

/* Validatation doesn't make sense in release builds */
#ifndef NDEBUG

/* Represents a single 16-bit slice of an SSA var */
struct var_offset {
   uint32_t var;
   uint8_t offset;
   uint8_t defined;
   uint8_t pad[2];
};
static_assert(sizeof(struct var_offset) == 8);

static struct var_offset
var_index(agx_index idx, uint32_t offset)
{
   assert(idx.type == AGX_INDEX_NORMAL);

   return (struct var_offset){
      .var = idx.value,
      .offset = offset,
      .defined = true,
   };
}

static struct var_offset
var_undef()
{
   return (struct var_offset){.defined = false};
}

static bool
vars_equal(struct var_offset x, struct var_offset y)
{
   return x.defined && y.defined && x.var == y.var && x.offset == y.offset;
}

/* Represents the contents of a single register file */
struct regfile {
   struct var_offset r[RA_CLASSES][AGX_NUM_MODELED_REGS];
};

static void
print_regfile(struct regfile *file, FILE *fp)
{
   fprintf(fp, "regfile: \n");
   for (enum ra_class cls = 0; cls < RA_CLASSES; ++cls) {
      for (unsigned r = 0; r < AGX_NUM_MODELED_REGS; ++r) {
         struct var_offset v = file->r[cls][r];

         if (v.defined) {
            fprintf(fp, "   %c%u = %u[%u]\n", cls == RA_MEM ? 'm' : 'h', r,
                    v.var, v.offset);
         }
      }
   }
   fprintf(fp, "\n");
}

#define agx_validate_assert(file, I, s, offs, stmt)                            \
   if (!(stmt)) {                                                              \
      fprintf(stderr, "failed to validate RA source %u offs %u: " #stmt "\n",  \
              s, offs);                                                        \
      agx_print_instr(I, stderr);                                              \
      print_regfile(file, stderr);                                             \
      return false;                                                            \
   }

static void
copy_reg(struct regfile *file, agx_index dst, agx_index src)
{
   assert(dst.type == AGX_INDEX_REGISTER);
   assert(src.type == AGX_INDEX_REGISTER);

   enum ra_class dst_cls = ra_class_for_index(dst);
   enum ra_class src_cls = ra_class_for_index(src);

   for (uint8_t offs = 0; offs < agx_index_size_16(dst); ++offs) {
      assert(dst.value + offs < ARRAY_SIZE(file->r[dst_cls]));
      file->r[dst_cls][dst.value + offs] = file->r[src_cls][src.value + offs];
   }
}

static void
swap_regs(struct regfile *file, agx_index a, agx_index b)
{
   assert(a.type == AGX_INDEX_REGISTER);
   assert(b.type == AGX_INDEX_REGISTER);

   enum ra_class a_cls = ra_class_for_index(a);
   enum ra_class b_cls = ra_class_for_index(b);

   unsigned size = agx_index_size_16(a);
   assert(size == agx_index_size_16(b));

   for (uint8_t offs = 0; offs < size; ++offs) {
      assert(a.value + offs < ARRAY_SIZE(file->r[a_cls]));
      assert(b.value + offs < ARRAY_SIZE(file->r[b_cls]));

      struct var_offset tmp = file->r[a_cls][a.value + offs];
      file->r[a_cls][a.value + offs] = file->r[b_cls][b.value + offs];
      file->r[b_cls][b.value + offs] = tmp;
   }
}

static void
record_dest(struct regfile *file, agx_index idx)
{
   assert(idx.type == AGX_INDEX_NORMAL && idx.has_reg);
   enum ra_class cls = ra_class_for_index(idx);

   for (uint8_t offs = 0; offs < agx_index_size_16(idx); ++offs) {
      assert(idx.reg + offs < ARRAY_SIZE(file->r[cls]));
      file->r[cls][idx.reg + offs] = var_index(idx, offs);
   }
}

static bool
validate_src(agx_instr *I, unsigned s, struct regfile *file, agx_index idx)
{
   assert(idx.type == AGX_INDEX_NORMAL && idx.has_reg);
   enum ra_class cls = ra_class_for_index(idx);

   for (uint8_t offs = 0; offs < agx_index_size_16(idx); ++offs) {
      assert(idx.reg + offs < ARRAY_SIZE(file->r[cls]));
      struct var_offset actual = file->r[cls][idx.reg + offs];

      agx_validate_assert(file, I, s, offs, actual.defined);
      agx_validate_assert(file, I, s, offs, actual.var == idx.value);
      agx_validate_assert(file, I, s, offs, actual.offset == offs);
   }

   return true;
}

static bool
validate_block(agx_context *ctx, agx_block *block, struct regfile *blocks)
{
   struct regfile *file = &blocks[block->index];
   bool success = true;

   /* Pathological shaders can end up with loop headers that have only a single
    * predecessor and act like normal blocks. Validate them as such, since RA
    * treats them as such implicitly. Affects:
    *
    * dEQP-VK.graphicsfuzz.spv-stable-mergesort-dead-code
    */
   bool loop_header = block->loop_header && agx_num_predecessors(block) > 1;

   /* Initialize the register file based on predecessors. This only works in
    * non-loop headers, since loop headers have unprocessed predecessors.
    * However, loop headers phi-declare everything instead of using implicit
    * live-in sources, so that's ok.
    */
   if (!loop_header) {
      bool first_pred = true;
      agx_foreach_predecessor(block, pred) {
         struct regfile *pred_file = &blocks[(*pred)->index];

         for (enum ra_class cls = 0; cls < RA_CLASSES; ++cls) {
            for (unsigned r = 0; r < AGX_NUM_MODELED_REGS; ++r) {
               if (first_pred)
                  file->r[cls][r] = pred_file->r[cls][r];
               else if (!vars_equal(file->r[cls][r], pred_file->r[cls][r]))
                  file->r[cls][r] = var_undef();
            }
         }

         first_pred = false;
      }
   }

   agx_foreach_instr_in_block(block, I) {
      /* Phis are special since they happen along the edge */
      if (I->op != AGX_OPCODE_PHI) {
         agx_foreach_ssa_src(I, s) {
            success &= validate_src(I, s, file, I->src[s]);
         }
      }

      agx_foreach_ssa_dest(I, d) {
         record_dest(file, I->dest[d]);
      }

      /* Lowered live range splits don't have SSA associated, handle
       * directly at the register level.
       */
      if (I->op == AGX_OPCODE_MOV && I->dest[0].type == AGX_INDEX_REGISTER &&
          I->src[0].type == AGX_INDEX_REGISTER) {

         copy_reg(file, I->dest[0], I->src[0]);
      } else if (I->op == AGX_OPCODE_SWAP) {
         swap_regs(file, I->src[0], I->src[1]);
      } else if (I->op == AGX_OPCODE_PHI &&
                 I->dest[0].type == AGX_INDEX_REGISTER) {
         /* Register-only phis which resolve to the same variable in all blocks.
          * This is generated for edge case live range splits.
          */
         assert(!I->dest[0].memory);
         assert(!loop_header);
         for (uint8_t offs = 0; offs < agx_index_size_16(I->dest[0]); ++offs) {
            bool all_same = true;
            bool first = true;
            struct var_offset same = var_undef();
            agx_foreach_predecessor(block, pred) {
               unsigned idx = agx_predecessor_index(block, *pred);
               agx_index src = I->src[idx];

               assert(!src.memory);
               if (src.type != AGX_INDEX_REGISTER) {
                  all_same = false;
                  first = false;
                  continue;
               }

               struct regfile *pred_file = &blocks[(*pred)->index];
               struct var_offset var = pred_file->r[RA_GPR][src.value + offs];
               all_same &= first || vars_equal(var, same);
               same = var;
               first = false;
            }

            if (all_same) {
               file->r[RA_GPR][I->dest[0].value + offs] = same;
            }
         }
      } else if (I->op == AGX_OPCODE_PHI && I->dest[0].has_reg &&
                 !loop_header) {
         /* Phis which resolve to the same variable in all blocks.
          * This is generated for live range splits.
          */
         enum ra_class cls = ra_class_for_index(I->dest[0]);
         for (uint8_t offs = 0; offs < agx_index_size_16(I->dest[0]); ++offs) {
            bool all_same = true;
            bool first = true;
            struct var_offset same = var_undef();
            agx_foreach_predecessor(block, pred) {
               unsigned idx = agx_predecessor_index(block, *pred);
               agx_index src = I->src[idx];

               assert(ra_class_for_index(src) == cls);
               if (!src.has_reg) {
                  all_same = false;
                  first = false;
                  continue;
               }

               struct regfile *pred_file = &blocks[(*pred)->index];
               struct var_offset var = pred_file->r[cls][src.reg + offs];
               all_same &= first || vars_equal(var, same);
               same = var;
               first = false;
            }

            if (all_same && I->dest[0].value == I->src[0].value) {
               file->r[cls][I->dest[0].reg + offs] = same;
            }
         }
      }
   }

   /* After processing a block, process the block's source in its successors'
    * phis. These happen on the edge so we have all the information here, even
    * with backedges.
    */
   agx_foreach_successor(block, succ) {
      unsigned idx = agx_predecessor_index(succ, block);

      agx_foreach_phi_in_block(succ, phi) {
         if (phi->src[idx].type == AGX_INDEX_NORMAL) {
            success &= validate_src(phi, idx, file, phi->src[idx]);
         }
      }
   }

   return success;
}

void
agx_validate_ra(agx_context *ctx)
{
   bool succ = true;
   struct regfile *blocks = calloc(ctx->num_blocks, sizeof(*blocks));

   agx_foreach_block(ctx, block) {
      succ &= validate_block(ctx, block, blocks);
   }

   if (!succ) {
      agx_print_shader(ctx, stderr);
      unreachable("invalid RA");
   }

   free(blocks);
}

#endif /* NDEBUG */
