/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "util/u_math.h"

#include "ir3.h"
#include "ir3_shader.h"

/*
 * Dead code elimination:
 */

static void
mark_array_use(struct ir3_instruction *instr, struct ir3_register *reg)
{
   if (reg->flags & IR3_REG_ARRAY) {
      struct ir3_array *arr =
         ir3_lookup_array(instr->block->shader, reg->array.id);
      arr->unused = false;
   }
}

static void
instr_dce(struct ir3_instruction *instr, bool falsedep)
{
   /* don't mark falsedep's as used, but otherwise process them normally: */
   if (!falsedep)
      instr->flags &= ~IR3_INSTR_UNUSED;

   if (ir3_instr_check_mark(instr))
      return;

   foreach_dst (dst, instr) {
      if (is_dest_gpr(dst))
         mark_array_use(instr, dst);
   }

   foreach_src (reg, instr)
      mark_array_use(instr, reg); /* src */

   foreach_ssa_src_n (src, i, instr) {
      if (!__is_false_dep(instr, i)) {
         if (instr->opc == OPC_META_COLLECT &&
             !(instr->dsts[0]->wrmask & (1 << i))) {
            /* Ignore sources of collects for which the corresponding dst is not
             * written since they are unused.
             */
            continue;
         }

         /* Propagate the wrmask of sources to their defs. */
         struct ir3_register *src_reg = instr->srcs[i];
         src_reg->def->wrmask |= src_reg->wrmask;

         if (!src_reg->wrmask) {
            /* If no components are read, the def is unused. */
            continue;
         }
      }

      instr_dce(src, __is_false_dep(instr, i));
   }
}

static bool
remove_unused_by_block(struct ir3_block *block)
{
   bool progress = false;
   foreach_instr_safe (instr, &block->instr_list) {
      if (instr->opc == OPC_END || instr->opc == OPC_CHSH ||
          instr->opc == OPC_CHMASK || instr->opc == OPC_LOCK ||
          instr->opc == OPC_UNLOCK)
         continue;
      if (instr->flags & IR3_INSTR_UNUSED) {
         if (instr->opc == OPC_META_SPLIT) {
            struct ir3_instruction *src = ssa(instr->srcs[0]);
            /* tex (cat5) instructions have a writemask, so we can
             * mask off unused components.  Other instructions do not.
             */
            if (src && is_tex_or_prefetch(src) && (src->dsts[0]->wrmask > 1)) {
               src->dsts[0]->wrmask &= ~(1 << instr->split.off);
            }
         }

         /* prune false-deps, etc: */
         foreach_ssa_use (use, instr)
            foreach_ssa_srcp_n (srcp, n, use)
               if (*srcp == instr)
                  *srcp = NULL;

         ir3_instr_remove(instr);
         progress = true;
      } else if (instr->opc == OPC_META_COLLECT) {
         struct ir3_register *dst = instr->dsts[0];

         /* Trim unused trailing components. While it's tempting to just remove
          * all unused components, this doesn't work for a few reasons. Note
          * that currently, collects with unused components are only created
          * when certain FS output components are aliased using alias.rt. The
          * important part here is that the collect will be used for an output.
          * Even if only certain components of an output are written to GPRs, we
          * still need to allocate the correct consecutive registers. For
          * example, if we only write out.xz, we have to make sure there is
          * still a register in between the registers allocated for the x and z
          * components. In other words, we have to be able to allocate a base
          * register for the output such that all components written to GPRs
          * have the correct offset from the base register. So we cannot remove
          * any unused holes in the collect. We also cannot remove the leading
          * unused components because then RA might decide put the first used
          * component in, say, r0.x, leaving no space to allocate a base
          * register. Therefore, we only trim trailing components.
          *
          * TODO: we could probably trim leading components by having a way to
          * request a minimum register number from RA.
          */
         instr->srcs_count = util_last_bit(dst->wrmask);

         /* Mark sources for which the corresponding dst is not written as
          * undef.
          */
         foreach_src_n (src, src_n, instr) {
            if (!(dst->wrmask & (1 << src_n))) {
               src->def = NULL;
               src->num = INVALID_REG;
               src->flags &= ~(IR3_REG_CONST | IR3_REG_IMMED);
            }
         }
      }
   }
   return progress;
}

static bool
find_and_remove_unused(struct ir3 *ir, struct ir3_shader_variant *so)
{
   unsigned i;
   bool progress = false;

   ir3_clear_mark(ir);

   /* initially mark everything as unused, we'll clear the flag as we
    * visit the instructions:
    */
   foreach_block (block, &ir->block_list) {
      foreach_instr (instr, &block->instr_list) {
         if (instr->opc == OPC_META_INPUT) {
            /* Without GS header geometry shader is never invoked. */
            if (instr->input.sysval == SYSTEM_VALUE_GS_HEADER_IR3)
               continue;
            if (instr->input.sysval == SYSTEM_VALUE_SAMPLE_MASK_IN &&
                so->reads_shading_rate &&
                ir->compiler->reading_shading_rate_requires_smask_quirk)
               continue;
         }

         instr->flags |= IR3_INSTR_UNUSED;

         /* To eliminate unused components in collect, we zero the wrmask and
          * update it using the wrmask of its users.
          */
         if (instr->opc == OPC_META_COLLECT) {
            instr->dsts[0]->wrmask = 0;
         }
      }
   }

   foreach_array (arr, &ir->array_list)
      arr->unused = true;

   foreach_block (block, &ir->block_list) {
      for (i = 0; i < block->keeps_count; i++)
         instr_dce(block->keeps[i], false);

      /* We also need to account for if-condition: */
      struct ir3_instruction *terminator = ir3_block_get_terminator(block);
      if (terminator) {
         instr_dce(terminator, false);
      }
   }

   /* remove un-used instructions: */
   foreach_block (block, &ir->block_list) {
      progress |= remove_unused_by_block(block);
   }

   /* remove un-used arrays: */
   foreach_array_safe (arr, &ir->array_list) {
      if (arr->unused)
         list_delinit(&arr->node);
   }

   /* fixup wrmask of split instructions to account for adjusted tex
    * wrmask's:
    */
   foreach_block (block, &ir->block_list) {
      foreach_instr (instr, &block->instr_list) {
         if (instr->opc != OPC_META_SPLIT)
            continue;

         struct ir3_instruction *src = ssa(instr->srcs[0]);
         if (!is_tex_or_prefetch(src))
            continue;

         instr->srcs[0]->wrmask = src->dsts[0]->wrmask;
      }
   }

   for (i = 0; i < ir->a0_users_count; i++) {
      struct ir3_instruction *instr = ir->a0_users[i];
      if (instr && (instr->flags & IR3_INSTR_UNUSED))
         ir->a0_users[i] = NULL;
   }

   for (i = 0; i < ir->a1_users_count; i++) {
      struct ir3_instruction *instr = ir->a1_users[i];
      if (instr && (instr->flags & IR3_INSTR_UNUSED))
         ir->a1_users[i] = NULL;
   }

   /* cleanup unused inputs: */
   foreach_input_n (in, n, ir)
      if (in->flags & IR3_INSTR_UNUSED)
         ir->inputs[n] = NULL;

   return progress;
}

bool
ir3_dce(struct ir3 *ir, struct ir3_shader_variant *so)
{
   void *mem_ctx = ralloc_context(NULL);
   bool progress, made_progress = false;

   ir3_find_ssa_uses(ir, mem_ctx, true);

   do {
      progress = find_and_remove_unused(ir, so);
      made_progress |= progress;
   } while (progress);

   ralloc_free(mem_ctx);

   return made_progress;
}
