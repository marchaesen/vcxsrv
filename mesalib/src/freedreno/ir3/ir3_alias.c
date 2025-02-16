/*
 * Copyright 2024 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "ir3.h"
#include "ir3_shader.h"

#define MAX_ALIASES 16

static bool
supports_alias_srcs(struct ir3_instruction *instr)
{
   if (!is_tex(instr))
      return false;
   if (is_tex_shuffle(instr))
      return false;
   /* Descriptor prefetches don't support alias.tex. */
   if (instr->opc == OPC_SAM && instr->dsts_count == 0)
      return false;
   /* Seems to not always work properly. Blob disables it as well. */
   if (instr->opc == OPC_ISAM && (instr->flags & IR3_INSTR_IMM_OFFSET))
      return false;
   return true;
}

static bool
can_alias_src(struct ir3_register *src)
{
   return is_reg_gpr(src) && !(src->flags & IR3_REG_SHARED);
}

static bool
can_alias_srcs_of_def(struct ir3_register *src)
{
   if (!can_alias_src(src)) {
      return false;
   }

   assert(src->flags & IR3_REG_SSA);
   struct ir3_instruction *def_instr = src->def->instr;

   if (def_instr->opc == OPC_META_COLLECT) {
      return true;
   }
   if (def_instr->opc == OPC_MOV) {
      return is_same_type_mov(def_instr) &&
             !(def_instr->srcs[0]->flags & IR3_REG_SHARED);
   }

   return false;
}

static bool
alias_srcs(struct ir3_instruction *instr)
{
   bool progress = false;

   /* All sources that come from collects are replaced by the sources of the
    * collects. So allocate a new srcs array to hold all the collect'ed sources
    * as well.
    */
   unsigned new_srcs_count = 0;

   foreach_src_n (src, src_n, instr) {
      if (can_alias_srcs_of_def(src)) {
         new_srcs_count += util_last_bit(src->wrmask);
      } else {
         new_srcs_count++;
      }
   }

   struct ir3_register **old_srcs = instr->srcs;
   unsigned old_srcs_count = instr->srcs_count;
   instr->srcs =
      ir3_alloc(instr->block->shader, new_srcs_count * sizeof(instr->srcs[0]));
   instr->srcs_count = 0;
   unsigned num_aliases = 0;

#if MESA_DEBUG
   instr->srcs_max = new_srcs_count;
#endif

   for (unsigned src_n = 0; src_n < old_srcs_count; src_n++) {
      struct ir3_register *src = old_srcs[src_n];
      bool can_alias = can_alias_src(src);

      if (!can_alias || !can_alias_srcs_of_def(src)) {
         if (can_alias && num_aliases + 1 <= MAX_ALIASES) {
            src->flags |= (IR3_REG_FIRST_ALIAS | IR3_REG_ALIAS);
            num_aliases++;
            progress = true;
         }

         instr->srcs[instr->srcs_count++] = src;
         continue;
      }

      struct ir3_instruction *collect = src->def->instr;
      assert(collect->opc == OPC_META_COLLECT || collect->opc == OPC_MOV);

      /* Make sure we don't create more aliases than supported in the alias
       * table. Note that this is rather conservative because we might actually
       * need less due to reuse of GPRs. However, once we mark a src as alias
       * here, and it doesn't get reused, we have to be able to allocate an
       * alias for it. Since it's impossible to predict reuse at this point, we
       * have to be conservative.
       */
      if (num_aliases + collect->srcs_count > MAX_ALIASES) {
         instr->srcs[instr->srcs_count++] = src;
         continue;
      }

      foreach_src_n (collect_src, collect_src_n, collect) {
         struct ir3_register *alias_src;

         if (collect_src->flags & IR3_REG_SSA) {
            alias_src =
               __ssa_src(instr, collect_src->def->instr, collect_src->flags);
         } else {
            alias_src =
               ir3_src_create(instr, collect_src->num, collect_src->flags);
            alias_src->uim_val = collect_src->uim_val;
         }

         alias_src->flags |= IR3_REG_ALIAS;

         if (collect_src_n == 0) {
            alias_src->flags |= IR3_REG_FIRST_ALIAS;
         }
      }

      num_aliases += collect->srcs_count;
      progress = true;
   }

   return progress;
}

/* First alias.tex pass: replace sources of tex instructions with alias sources
 * (IR3_REG_ALIAS):
 * - movs from const/imm: replace with the const/imm;
 * - collects: replace with the sources of the collect;
 * - GPR sources: simply mark as alias.
 *
 * This way, RA won't be forced to allocate consecutive registers for collects
 * and useless collects/movs can be DCE'd. Note that simply lowering collects to
 * aliases doesn't work because RA would assume that killed sources of aliases
 * are dead, while they are in fact live until the tex instruction that uses
 * them.
 */
bool
ir3_create_alias_tex_regs(struct ir3 *ir)
{
   if (!ir->compiler->has_alias_tex)
      return false;
   if (ir3_shader_debug & IR3_DBG_NOALIASTEX)
      return false;

   bool progress = false;

   foreach_block (block, &ir->block_list) {
      foreach_instr (instr, &block->instr_list) {
         if (supports_alias_srcs(instr)) {
            progress |= alias_srcs(instr);
         }
      }
   }

   return progress;
}

#define FIRST_ALIAS_REG regid(40, 0)

struct alias_table_entry {
   unsigned alias_reg;
   struct ir3_register *src;
};

typedef BITSET_DECLARE(reg_bitset, GPR_REG_SIZE);

struct alias_table_state {
   struct alias_table_entry entries[16];
   unsigned num_entries;

   /* The registers currently allocated for the instruction. Note that this
    * includes both alias registers as well as GPRs that are reused.
    */
   reg_bitset full_alloc;
   reg_bitset half_alloc;
};

static void
add_table_entry(struct alias_table_state *state, unsigned alias_reg,
                struct ir3_register *src)
{
   assert(state->num_entries < ARRAY_SIZE(state->entries));
   struct alias_table_entry *entry = &state->entries[state->num_entries++];
   entry->alias_reg = alias_reg;
   entry->src = src;
}

static void
clear_table(struct alias_table_state *state)
{
   BITSET_ZERO(state->full_alloc);
   BITSET_ZERO(state->half_alloc);
   state->num_entries = 0;
}

static unsigned
lookup_alias(struct alias_table_state *state, struct ir3_register *alias)
{
   for (unsigned i = 0; i < state->num_entries; i++) {
      struct alias_table_entry *entry = &state->entries[i];
      unsigned match_flags = (IR3_REG_CONST | IR3_REG_IMMED | IR3_REG_HALF);

      if ((alias->flags & match_flags) != (entry->src->flags & match_flags)) {
         continue;
      }

      if (alias->flags & IR3_REG_IMMED) {
         if (alias->uim_val == entry->src->uim_val) {
            return entry->alias_reg;
         }
      } else if (alias->num == entry->src->num) {
         return entry->alias_reg;
      }
   }

   return INVALID_REG;
}

/* Find existing entries in the alias table for all aliases in this alias group.
 * If all aliases are already in the table, and they are in consecutive
 * registers, we can simply reuse these registers without creating new table
 * entries.
 * TODO if there's a partial overlap between the start of the alias group and
 * the end of an existing allocation range, we might be able to partially reuse
 * table entries.
 */
static unsigned
find_existing_alloc(struct alias_table_state *state,
                    struct ir3_instruction *instr, unsigned first_src_n)
{
   if (state->num_entries == 0) {
      return INVALID_REG;
   }

   unsigned first_reg = INVALID_REG;

   foreach_src_in_alias_group_n (alias, alias_n, instr, first_src_n) {
      unsigned reg = lookup_alias(state, alias);

      if (reg == INVALID_REG) {
         return INVALID_REG;
      }

      if (alias_n == 0) {
         first_reg = reg;
      } else if (reg != first_reg + alias_n) {
         return INVALID_REG;
      }
   }

   assert(first_reg != INVALID_REG);
   return first_reg;
}

static unsigned
find_free_alias_regs_in_range(const reg_bitset *alloc_regs,
                              unsigned num_aliases, unsigned start,
                              unsigned end)
{
   assert(end >= num_aliases);

   for (unsigned reg = start; reg < end - num_aliases; reg++) {
      if (!BITSET_TEST_RANGE(*alloc_regs, reg, reg + num_aliases - 1)) {
         return reg;
      }
   }

   return INVALID_REG;
}

static unsigned
find_free_alias_regs(const reg_bitset *alloc_regs, unsigned num_aliases)
{
   unsigned reg = find_free_alias_regs_in_range(alloc_regs, num_aliases,
                                                FIRST_ALIAS_REG, GPR_REG_SIZE);

   if (reg != INVALID_REG) {
      return reg;
   }

   return find_free_alias_regs_in_range(alloc_regs, num_aliases, 0,
                                        FIRST_ALIAS_REG);
}

struct reg_alloc_info {
   unsigned first_src_n;
   unsigned reg;
   unsigned num_reused;
};

/* Allocate alias registers for an alias group while trying to minimize the
 * number of needed aliases. That is, if the allocated GPRs for the group are
 * (partially) consecutive, only allocate aliases to fill-in the gaps. For
 * example:
 *    sam ..., @{r1.x, r5.z, r1.z}, ...
 * only needs a single alias:
 *    alias.tex.b32.0 r1.y, r5.z
 *    sam ..., r1.x, ...
 */
static struct reg_alloc_info
alloc_alias(struct alias_table_state *state, struct ir3_instruction *instr,
            unsigned first_src_n)
{
   assert(first_src_n < instr->srcs_count);

   struct ir3_register *src0 = instr->srcs[first_src_n];
   assert(src0->flags & IR3_REG_FIRST_ALIAS);

   unsigned num_aliases = 0;

   foreach_src_in_alias_group (alias, instr, first_src_n) {
      num_aliases++;
   }

   assert(num_aliases > 0);

   reg_bitset *alloc_regs =
      (src0->flags & IR3_REG_HALF) ? &state->half_alloc : &state->full_alloc;

   /* All the GPRs used by this alias group that aren't already allocated by
    * previous alias groups.
    */
   unsigned used_regs[num_aliases];

   foreach_src_in_alias_group_n (alias, alias_n, instr, first_src_n) {
      if (is_reg_gpr(alias) && !BITSET_TEST(*alloc_regs, alias->num)) {
         used_regs[alias_n] = alias->num;
      } else {
         used_regs[alias_n] = INVALID_REG;
      }
   }

   /* Find the register that, when allocated to the first src in the alias
    * group, will maximize the number of GPRs reused (i.e., that don't need an
    * alias) in the group.
    */
   unsigned best_reg = INVALID_REG;
   unsigned best_num_reused = 0;

   foreach_src_in_alias_group_n (alias, alias_n, instr, first_src_n) {
      if (used_regs[alias_n] == INVALID_REG) {
         /* No (free) GPR is used by this alias. */
         continue;
      }

      if (alias->num < alias_n) {
         /* To be able to fit the current alias reg in a valid consecutive
          * range, its GPR number needs to be at least its index in the alias
          * group. Otherwise, there won't be enough GPR space left before it:
          * sam, ..., @{r5.w, r0.x, r0.y}, ...
          * Even though r0.x and r0.y are consecutive, we won't be able to reuse
          * them since there's no GPR before r0.x to alias to r5.w.
          */
         continue;
      }

      if (alias->num + num_aliases - alias_n >= GPR_REG_SIZE) {
         /* Same reasoning as above but for the end of the GPR space. */
         continue;
      }

      /* Check if it's possible to reuse the allocated GPR of the current alias
       * reg. If we reuse it, all other aliases in this group will have their
       * GPR number based on the current one and need to be free.
       */
      unsigned first_reg = alias->num - alias_n;

      if (BITSET_TEST_RANGE(*alloc_regs, first_reg,
                            first_reg + num_aliases - 1)) {
         continue;
      }

      /* Check how many GPRs will be reused with this choice. Note that we don't
       * have to check previous registers in the alias group since if we can
       * reuse those, the current alias would have been counted there as well.
       */
      unsigned num_reused = 1;

      for (unsigned i = alias_n + 1; i < num_aliases; i++) {
         if (used_regs[i] == first_reg + i) {
            num_reused++;
         }
      }

      if (num_reused > best_num_reused) {
         best_num_reused = num_reused;
         best_reg = alias->num - alias_n;
      }
   }

   if (best_reg == INVALID_REG) {
      /* No reuse possible, just allocate fresh registers. */
      best_reg = find_free_alias_regs(alloc_regs, num_aliases);

      /* We can use the full GPR space (4 * 48 regs) to allocate aliases which
       * is enough to always find a free range that is large enough. The maximum
       * number of aliases is 12 (src0) + 4 (src1) + 2 (samp_tex) so the worst
       * case reuse looks something like this (note that the number of aliases
       * is limited to 16 so in practice, it will never be this bad):
       *     [ ... src1.x..src1.w ... samp_tex.x samp_tex.y ... ]
       * #GPR 0    11      14         26         27
       * Here, src1 and samp_tex reuse GPRs in such a way that they leave a gap
       * of 11 GPRs around them so that the src0 will not fit. There is ample
       * GPR space left for src0 even in this scenario.
       */
      assert(best_reg != INVALID_REG);
   }

   /* Mark used registers as allocated. */
   unsigned end_reg = best_reg + num_aliases - 1;
   assert(end_reg < GPR_REG_SIZE);
   assert(!BITSET_TEST_RANGE(*alloc_regs, best_reg, end_reg));
   BITSET_SET_RANGE(*alloc_regs, best_reg, end_reg);

   /* Add the allocated registers that differ from the ones already used to the
    * alias table.
    */
   for (unsigned i = 0; i < num_aliases; i++) {
      unsigned reg = best_reg + i;

      if (used_regs[i] != reg) {
         struct ir3_register *src = instr->srcs[first_src_n + i];
         add_table_entry(state, reg, src);
      }
   }

   return (struct reg_alloc_info){
      .first_src_n = first_src_n,
      .reg = best_reg,
      .num_reused = best_num_reused,
   };
}

static int
cmp_alloc(const void *ptr1, const void *ptr2)
{
   const struct reg_alloc_info *alloc1 = ptr1;
   const struct reg_alloc_info *alloc2 = ptr2;
   return alloc2->num_reused - alloc1->num_reused;
}

static void
alloc_aliases(struct alias_table_state *state, struct ir3_instruction *instr,
              unsigned *regs)
{
   unsigned num_alias_groups = 0;

   foreach_src (src, instr) {
      if (src->flags & IR3_REG_FIRST_ALIAS) {
         num_alias_groups++;
      }
   }

   assert(num_alias_groups > 0);
   struct reg_alloc_info allocs[num_alias_groups];
   unsigned alloc_i = 0;

   /* We allocate alias registers in two phases:
    * 1. Allocate each alias group as if they are the only group. This way, the
    * number of registers they can reuse is maximized (because they will never
    * conflict with other groups). We keep track of the number of reused
    * registers per group.
    */
   foreach_src_n (src, src_n, instr) {
      if (src->flags & IR3_REG_FIRST_ALIAS) {
         allocs[alloc_i++] = alloc_alias(state, instr, src_n);
         clear_table(state);
      }
   }

   /* 2. Do the actual allocation of the groups ordered by decreasing number of
    * reused registers. This results in a greater (though not necessarily
    * optimal) total number of reused registers and, thus, a smaller number of
    * table entries. This helps in situations like this:
    *    sam ..., @{r0.z, r1.y}, @{r0.w, r1.x}
    * The first group can reuse 1 register while the second 2. All valid
    * choices to reuse one register in the first group (r0.z/r0.w or r1.x/r1.y)
    * lead to an overlap with the second group which means that no reuse is
    * possible in the second group:
    *    alias.tex.b32.2 r0.w, r1.y
    *    alias.tex.b32.0 r40.x, r0.w
    *    alias.tex.b32.0 r40.y, r1.x
    *    sam ..., r0.z, r40.x
    * Allocating the second group first leads to an optimal allocation:
    *    alias.tex.b32.1 r40.x, r0.z
    *    alias.tex.b32.0 r40.y, r1.y
    *    sam ..., r40.x, r0.w
    */
   qsort(allocs, num_alias_groups, sizeof(allocs[0]), cmp_alloc);

   /* Mark all GPR sources that cannot be aliased as allocated since we have to
    * make sure no alias overlaps with them.
    */
   foreach_src (src, instr) {
      if (can_alias_src(src) && !(src->flags & IR3_REG_ALIAS)) {
         reg_bitset *alloc_regs = (src->flags & IR3_REG_HALF)
                                     ? &state->half_alloc
                                     : &state->full_alloc;
         BITSET_SET(*alloc_regs, src->num);
      }
   }

   for (unsigned i = 0; i < num_alias_groups; i++) {
      struct reg_alloc_info *alloc = &allocs[i];

      /* Check if any allocations made by previous groups can be reused for this
       * one. For example, this is relatively common:
       *    sam ..., @{r2.z, 0}, @{0}
       * Reusing the allocation of the first group for the second one gives
       * this:
       *    alias.tex.b32.0 r2.w, 0
       *    sam ..., r2.z, r2.w
       */
      alloc->reg = find_existing_alloc(state, instr, alloc->first_src_n);

      if (alloc->reg == INVALID_REG) {
         *alloc = alloc_alias(state, instr, alloc->first_src_n);
      }

      regs[alloc->first_src_n] = alloc->reg;
   }
}

static bool
insert_aliases(struct ir3_instruction *instr)
{
   bool progress = false;

   struct alias_table_state state = {0};
   struct ir3_cursor cursor = ir3_before_instr(instr);

   unsigned regs[instr->srcs_count];
   alloc_aliases(&state, instr, regs);
   assert(state.num_entries <= MAX_ALIASES);

   for (unsigned i = 0; i < state.num_entries; i++) {
      struct alias_table_entry *entry = &state.entries[i];

      struct ir3_instruction *alias =
         ir3_instr_create_at(cursor, OPC_ALIAS, 1, 2);
      alias->cat7.alias_scope = ALIAS_TEX;
      struct ir3_register *src = ir3_src_create(
         alias, entry->src->num,
         entry->src->flags & ~(IR3_REG_FIRST_ALIAS | IR3_REG_ALIAS));
      src->uim_val = entry->src->uim_val;
      ir3_dst_create(alias, entry->alias_reg,
                     (entry->src->flags & IR3_REG_HALF) | IR3_REG_ALIAS);

      if (i == 0) {
         alias->cat7.alias_table_size_minus_one = state.num_entries - 1;
      }

      progress = true;
   }

   unsigned next_src_n = 0;

   for (unsigned src_n = 0; src_n < instr->srcs_count;) {
      struct ir3_register *src0 = instr->srcs[src_n];
      unsigned num_srcs = 0;

      if (src0->flags & IR3_REG_FIRST_ALIAS) {
         foreach_src_in_alias_group (src, instr, src_n) {
            num_srcs++;
         }

         src0->num = regs[src_n];
         src0->flags &= ~(IR3_REG_IMMED | IR3_REG_CONST);
         src0->wrmask = MASK(num_srcs);
      } else {
         num_srcs = 1;
      }

      instr->srcs[next_src_n++] = src0;
      src_n += num_srcs;
   }

   instr->srcs_count = next_src_n;
   return progress;
}

static bool
has_alias_srcs(struct ir3_instruction *instr)
{
   if (!supports_alias_srcs(instr)) {
      return false;
   }

   foreach_src (src, instr) {
      if (src->flags & IR3_REG_FIRST_ALIAS) {
         return true;
      }
   }

   return false;
}

/* Second alias.tex pass: insert alias.tex instructions in front of the tex
 * instructions that need them and fix up the tex instruction's sources. This
 * pass needs to run post-RA (see ir3_create_alias_tex_regs). It also needs to
 * run post-legalization as all the sync flags need to be inserted based on the
 * registers instructions actually use, not on the alias registers they have as
 * sources.
 */
bool
ir3_insert_alias_tex(struct ir3 *ir)
{
   if (!ir->compiler->has_alias_tex)
      return false;
   if (ir3_shader_debug & IR3_DBG_NOALIASTEX)
      return false;

   bool progress = false;

   foreach_block (block, &ir->block_list) {
      foreach_instr_safe (instr, &block->instr_list) {
         if (has_alias_srcs(instr)) {
            progress |= insert_aliases(instr);
         }
      }
   }

   return progress;
}

static struct ir3_instruction *
get_or_create_shpe(struct ir3 *ir)
{
   struct ir3_instruction *shpe = ir3_find_shpe(ir);

   if (!shpe) {
      shpe = ir3_create_empty_preamble(ir);
      assert(shpe);
   }

   return shpe;
}

static bool
create_output_aliases(struct ir3_shader_variant *v, struct ir3_instruction *end)
{
   bool progress = false;
   struct ir3_instruction *shpe = NULL;

   foreach_src_n (src, src_n, end) {
      struct ir3_shader_output *output = &v->outputs[end->end.outidxs[src_n]];

      if (output->slot < FRAG_RESULT_DATA0 ||
          output->slot > FRAG_RESULT_DATA7) {
         continue;
      }

      assert(src->flags & IR3_REG_SSA);
      struct ir3_instruction *src_instr = src->def->instr;

      if (src_instr->opc != OPC_META_COLLECT && src_instr->opc != OPC_MOV) {
         continue;
      }

      unsigned rt = output->slot - FRAG_RESULT_DATA0;

      foreach_src_n (comp_src, comp, src_instr) {
         if (!(comp_src->flags & (IR3_REG_IMMED | IR3_REG_CONST))) {
            /* Only const and immediate values can be aliased. */
            continue;
         }

         if ((comp_src->flags & IR3_REG_HALF) &&
             (comp_src->flags & IR3_REG_CONST)) {
            /* alias.rt doesn't seem to work with half const.
             * TODO figure out what's going wrong here. Might just be
             * unsupported because the blob only uses it in one CTS test.
             */
            continue;
         }

         if (!shpe) {
            shpe = get_or_create_shpe(v->ir);
         }

         struct ir3_instruction *alias =
            ir3_instr_create_at(ir3_before_instr(shpe), OPC_ALIAS, 1, 2);
         alias->cat7.alias_scope = ALIAS_RT;
         ir3_dst_create(alias, regid(rt, comp), IR3_REG_RT);

         unsigned src_flags =
            comp_src->flags & (IR3_REG_HALF | IR3_REG_CONST | IR3_REG_IMMED);
         ir3_src_create(alias, comp_src->num, src_flags)->uim_val =
            comp_src->uim_val;

         if (src_instr->opc == OPC_MOV) {
            /* The float type bit seems entirely optional (i.e., it only affects
             * disassembly) but since we have this info for movs, we might as
             * well set it.
             */
            alias->cat7.alias_type_float = type_float(src_instr->cat1.dst_type);
         }

         /* Scheduling an alias.rt right before an alias.tex causes a GPU hang.
          * Follow the blob and schedule all alias.rt at the end of the
          * preamble to prevent this from happening.
          */
         alias->barrier_class = alias->barrier_conflict = IR3_BARRIER_CONST_W;

         /* Nothing actually uses the alias.rt dst so make sure it doesn't get
          * DCE'd.
          */
         array_insert(shpe->block, shpe->block->keeps, alias);

         output->aliased_components |= (1 << comp);
         progress = true;
      }

      /* Remove the aliased components from the src so that they can be DCE'd.
       */
      src->wrmask &= ~output->aliased_components;

      if (!src->wrmask) {
         src->def = NULL;
      }
   }

   return progress;
}

/* Replace const and immediate components of the RT sources of end with alias.rt
 * instructions in the preamble.
 */
bool
ir3_create_alias_rt(struct ir3 *ir, struct ir3_shader_variant *v)
{
   if (!ir->compiler->has_alias_rt)
      return false;
   if (ir3_shader_debug & IR3_DBG_NOALIASRT)
      return false;
   if (v->type != MESA_SHADER_FRAGMENT)
      return false;
   if (v->shader_options.fragdata_dynamic_remap)
      return false;

   struct ir3_instruction *end = ir3_find_end(ir);
   assert(end->opc == OPC_END);
   return create_output_aliases(v, end);
}
