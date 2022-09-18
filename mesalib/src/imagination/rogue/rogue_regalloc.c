/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stddef.h>
#include <stdint.h>

#include "rogue_operand.h"
#include "rogue_regalloc.h"
#include "rogue_shader.h"
#include "rogue_util.h"
#include "util/hash_table.h"
#include "util/list.h"
#include "util/ralloc.h"
#include "util/register_allocate.h"
#include "util/u_dynarray.h"

/**
 * \file rogue_regalloc.c
 *
 * \brief Contains register allocation helper functions.
 */

/**
 * \brief Sets up the register data with the classes to be used for allocation.
 *
 * \param[in] data The register data array.
 */
static void
rogue_reg_data_init(struct rogue_reg_data data[static ROGUE_REG_CLASS_COUNT])
{
   data[ROGUE_REG_CLASS_TEMP].type = ROGUE_OPERAND_TYPE_REG_TEMP;
   data[ROGUE_REG_CLASS_TEMP].count = ROGUE_MAX_REG_TEMP;
   data[ROGUE_REG_CLASS_TEMP].stride = 1;

   data[ROGUE_REG_CLASS_VEC4].type = ROGUE_OPERAND_TYPE_REG_INTERNAL;
   data[ROGUE_REG_CLASS_VEC4].count = ROGUE_MAX_REG_INTERNAL;
   data[ROGUE_REG_CLASS_VEC4].stride = 4;
}

/**
 * \brief Initializes the Rogue register allocation context.
 *
 * \param[in] mem_ctx The memory context for the ra context.
 * \return A rogue_ra * if successful, or NULL if unsuccessful.
 */
struct rogue_ra *rogue_ra_init(void *mem_ctx)
{
   struct rogue_ra *ra;
   size_t total_regs = 0;

   ra = rzalloc_size(mem_ctx, sizeof(*ra));
   if (!ra)
      return NULL;

   /* Initialize the register class data. */
   rogue_reg_data_init(ra->reg_data);

   /* Count up the registers classes and set up their offsets.
    *
    * The physical register numbers are sequential, even if the
    * registers are from different banks, so keeping track of
    * the offset means we can get the true physical register
    * number back after allocation.
    */
   for (size_t u = 0; u < ARRAY_SIZE(ra->reg_data); ++u) {
      ra->reg_data[u].offset = total_regs;
      total_regs += ra->reg_data[u].count;
   }

   /* Create a register set for allocation.  */
   ra->regs = ra_alloc_reg_set(ra, total_regs, true);
   if (!ra->regs) {
      ralloc_free(ra);
      return NULL;
   }

   /* Create the register class for the temps. */
   ra->reg_data[ROGUE_REG_CLASS_TEMP].class =
      ra_alloc_contig_reg_class(ra->regs, 1);

   /* Create the register class for vec4 registers
    * (using the internal register bank).
    */
   ra->reg_data[ROGUE_REG_CLASS_VEC4].class =
      ra_alloc_contig_reg_class(ra->regs, 4);

   /* Populate the register classes. */
   for (size_t u = 0; u < ARRAY_SIZE(ra->reg_data); ++u) {
      struct rogue_reg_data *reg_data = &ra->reg_data[u];
      size_t offset = reg_data->offset;
      size_t end = reg_data->offset + reg_data->count;
      size_t stride = reg_data->stride;

      for (size_t r = offset; r < end; r += stride)
         ra_class_add_reg(reg_data->class, r);
   }

   /* Finalize the set (no early conflicts passed along for now). */
   ra_set_finalize(ra->regs, NULL);

   return ra;
}

/**
 * \brief The range for which a (virtual) register is live, and its references.
 */
struct live_range {
   size_t start;
   size_t end;
   enum rogue_reg_class class;
   struct util_dynarray operand_refs;
};

/**
 * \brief Performs register allocation.
 *
 * \param[in] instr_list A linked list of instructions with virtual registers to
 * be allocated.
 * \param[in] ra The register allocation context.
 */
bool rogue_ra_alloc(struct list_head *instr_list,
                    struct rogue_ra *ra,
                    size_t *temps_used,
                    size_t *internals_used)
{
   /* Used for ra_alloc_interference_graph() as it doesn't
    * like having gaps (e.g. with v0, v2 count = 3 rather
    * than 2).
    */
   size_t max_vreg = 0;

   struct hash_table *reg_ht =
      _mesa_hash_table_create(ra, _mesa_hash_uint, _mesa_key_uint_equal);
   if (!reg_ht)
      return false;

   /* Calculate live ranges for virtual registers. */
   size_t ip = 0U; /* "Instruction pointer". */
   foreach_instr (instr, instr_list) {
      for (size_t u = 0U; u < instr->num_operands; ++u) {
         struct hash_entry *entry;
         struct live_range *range;

         if (instr->operands[u].type != ROGUE_OPERAND_TYPE_VREG)
            continue;

         entry =
            _mesa_hash_table_search(reg_ht, &instr->operands[u].vreg.number);
         if (!entry) {
            /* First use of this virtual register: initialize live range. */
            /* TODO: Error handling. */
            range = rzalloc_size(reg_ht, sizeof(*range));

            range->start = ip;
            range->end = ip;
            range->class = instr->operands[u].vreg.is_vector
                              ? ROGUE_REG_CLASS_VEC4
                              : ROGUE_REG_CLASS_TEMP;

            entry = _mesa_hash_table_insert(reg_ht,
                                            &instr->operands[u].vreg.number,
                                            range);

            max_vreg = MAX2(max_vreg, instr->operands[u].vreg.number);

            util_dynarray_init(&range->operand_refs, range);
         } else {
            /* Subsequent uses: update live range end. */
            range = entry->data;
            range->end = MAX2(range->end, ip);
            assert(range->class == (instr->operands[u].vreg.is_vector
                                       ? ROGUE_REG_CLASS_VEC4
                                       : ROGUE_REG_CLASS_TEMP));
         }

         /* Save a reference to the operand.  */
         util_dynarray_append(&range->operand_refs,
                              struct rogue_operand *,
                              &instr->operands[u]);
      }
      ++ip;
   }

   /* Initialize the interference graph. */
   struct ra_graph *g = ra_alloc_interference_graph(ra->regs, max_vreg + 1);

   /* Set each virtual register to the appropriate class. */
   hash_table_foreach (reg_ht, entry) {
      const uint32_t *vreg = entry->key;
      struct live_range *range = entry->data;
      struct ra_class *class = ra->reg_data[range->class].class;

      ra_set_node_class(g, *vreg, class);
      /* TODO: ra_set_node_spill_cost(g, *vreg, cost); */
   }

   /* Build interference graph from overlapping live ranges. */
   hash_table_foreach (reg_ht, entry_first) {
      const uint32_t *vreg_first = entry_first->key;
      struct live_range *range_first = entry_first->data;

      hash_table_foreach (reg_ht, entry_second) {
         const uint32_t *vreg_second = entry_second->key;
         struct live_range *range_second = entry_second->data;

         if (*vreg_first == *vreg_second)
            continue;

         /* If the live ranges overlap, those register nodes interfere. */
         if (!(range_first->start >= range_second->end ||
               range_second->start >= range_first->end)) {
            ra_add_node_interference(g, *vreg_first, *vreg_second);
         }
      }
   }

   /* Add node interferences such that the same register can't be used for
    * both an instruction's source and destination.
    */
   foreach_instr (instr, instr_list) {
      for (size_t u = 0U; u < instr->num_operands; ++u) {
         if (instr->operands[u].type != ROGUE_OPERAND_TYPE_VREG)
            continue;

         /* Operand 0 (if it exists and is virtual) is always
          * the destination register.
          */
         if (u > 0 && instr->operands[0].type == ROGUE_OPERAND_TYPE_VREG)
            ra_add_node_interference(g,
                                     instr->operands[0].vreg.number,
                                     instr->operands[u].vreg.number);
      }
   }

   /* Perform register allocation. */
   /* TODO: Spilling support. */
   assert(ra_allocate(g));

   /* Replace virtual registers with allocated physical registers.
    * N.B. This is a destructive process as it overwrites the hash table key!
    */
   hash_table_foreach (reg_ht, entry) {
      uint32_t vreg = *(uint32_t *)entry->key;
      unsigned phy_reg = ra_get_node_reg(g, vreg);
      struct live_range *range = entry->data;

      struct rogue_reg_data *reg_data = &ra->reg_data[range->class];
      enum rogue_operand_type type = reg_data->type;
      size_t reg_offset = reg_data->offset;
      size_t *num_used = &reg_data->num_used;

      util_dynarray_foreach (&range->operand_refs,
                             struct rogue_operand *,
                             operand_ptr) {
         size_t num = phy_reg - reg_offset;
         struct rogue_operand *operand = *operand_ptr;

         assert(operand->type == ROGUE_OPERAND_TYPE_VREG);
         assert(operand->vreg.number == vreg);

         /* Index the component of emulated vec4 registers. */
         if (operand->vreg.is_vector &&
             operand->vreg.component != ROGUE_COMPONENT_ALL)
            num += operand->vreg.component;

         operand->type = type;
         operand->reg.number = num;

         *num_used = MAX2(*num_used, operand->reg.number);
      }

      util_dynarray_fini(&range->operand_refs);
      _mesa_hash_table_remove(reg_ht, entry);
   }

   /* Registers used = max reg number + 1. */
   for (size_t u = 0; u < ARRAY_SIZE(ra->reg_data); ++u)
      if (ra->reg_data[u].num_used)
         ++ra->reg_data[u].num_used;

   /* Pass back the registers used. */
   if (temps_used)
      *temps_used = ra->reg_data[ROGUE_REG_CLASS_TEMP].num_used;

   if (internals_used)
      *internals_used = ra->reg_data[ROGUE_REG_CLASS_VEC4].num_used;

   ralloc_free(g);

   _mesa_hash_table_destroy(reg_ht, NULL);

   return true;
}
