/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * This pass:
 * - vectorizes lowered input/output loads and stores
 * - vectorizes low and high 16-bit loads and stores by merging them into
 *   a single 32-bit load or store (except load_interpolated_input, which has
 *   to keep bit_size=16)
 * - performs DCE of output stores that overwrite the previous value by writing
 *   into the same slot and component.
 *
 * Vectorization is only local within basic blocks. No vectorization occurs
 * across basic block boundaries, barriers (only TCS outputs), emits (only
 * GS outputs), and output load <-> output store dependencies.
 *
 * All loads and stores must be scalar. 64-bit loads and stores are forbidden.
 *
 * For each basic block, the time complexity is O(n*log(n)) where n is
 * the number of IO instructions within that block.
 */

#include "nir.h"
#include "nir_builder.h"
#include "util/u_dynarray.h"

/* Return 0 if loads/stores are vectorizable. Return 1 or -1 to define
 * an ordering between non-vectorizable instructions. This is used by qsort,
 * to sort all gathered instructions into groups of vectorizable instructions.
 */
static int
compare_is_not_vectorizable(nir_intrinsic_instr *a, nir_intrinsic_instr *b)
{
   if (a->intrinsic != b->intrinsic)
      return a->intrinsic > b->intrinsic ? 1 : -1;

   nir_src *offset0 = nir_get_io_offset_src(a);
   nir_src *offset1 = nir_get_io_offset_src(b);
   if (offset0 && offset0->ssa != offset1->ssa)
      return offset0->ssa->index > offset1->ssa->index ? 1 : -1;

   nir_src *array_idx0 = nir_get_io_arrayed_index_src(a);
   nir_src *array_idx1 = nir_get_io_arrayed_index_src(b);
   if (array_idx0 && array_idx0->ssa != array_idx1->ssa)
      return array_idx0->ssa->index > array_idx1->ssa->index ? 1 : -1;

   /* Compare barycentrics or vertex index. */
   if ((a->intrinsic == nir_intrinsic_load_interpolated_input ||
        a->intrinsic == nir_intrinsic_load_input_vertex) &&
       a->src[0].ssa != b->src[0].ssa)
      return a->src[0].ssa->index > b->src[0].ssa->index ? 1 : -1;

   nir_io_semantics sem0 = nir_intrinsic_io_semantics(a);
   nir_io_semantics sem1 = nir_intrinsic_io_semantics(b);
   if (sem0.location != sem1.location)
      return sem0.location > sem1.location ? 1 : -1;

   /* The mediump flag isn't mergable. */
   if (sem0.medium_precision != sem1.medium_precision)
      return sem0.medium_precision > sem1.medium_precision ? 1 : -1;

   /* Don't merge per-view attributes with non-per-view attributes. */
   if (sem0.per_view != sem1.per_view)
      return sem0.per_view > sem1.per_view ? 1 : -1;

   if (sem0.interp_explicit_strict != sem1.interp_explicit_strict)
      return sem0.interp_explicit_strict > sem1.interp_explicit_strict ? 1 : -1;

   /* Only load_interpolated_input can't merge low and high halves of 16-bit
    * loads/stores.
    */
   if (a->intrinsic == nir_intrinsic_load_interpolated_input &&
       sem0.high_16bits != sem1.high_16bits)
      return sem0.high_16bits > sem1.high_16bits ? 1 : -1;

   nir_shader *shader =
      nir_cf_node_get_function(&a->instr.block->cf_node)->function->shader;

   /* Compare the types. */
   if (!(shader->options->io_options & nir_io_vectorizer_ignores_types)) {
      unsigned type_a, type_b;

      if (nir_intrinsic_has_src_type(a)) {
         type_a = nir_intrinsic_src_type(a);
         type_b = nir_intrinsic_src_type(b);
      } else {
         type_a = nir_intrinsic_dest_type(a);
         type_b = nir_intrinsic_dest_type(b);
      }

      if (type_a != type_b)
         return type_a > type_b ? 1 : -1;
   }

   return 0;
}

static int
compare_intr(const void *xa, const void *xb)
{
   nir_intrinsic_instr *a = *(nir_intrinsic_instr **)xa;
   nir_intrinsic_instr *b = *(nir_intrinsic_instr **)xb;

   int comp = compare_is_not_vectorizable(a, b);
   if (comp)
      return comp;

   /* qsort isn't stable. This ensures that later stores aren't moved before earlier stores. */
   return a->instr.index > b->instr.index ? 1 : -1;
}

static void
vectorize_load(nir_intrinsic_instr *chan[8], unsigned start, unsigned count,
               bool merge_low_high_16_to_32)
{
   nir_intrinsic_instr *first = NULL;

   /* Find the first instruction where the vectorized load will be
    * inserted.
    */
   for (unsigned i = start; i < start + count; i++) {
      first = !first || chan[i]->instr.index < first->instr.index ?
                 chan[i] : first;
      if (merge_low_high_16_to_32) {
         first = !first || chan[4 + i]->instr.index < first->instr.index ?
                    chan[4 + i] : first;
      }
   }

   /* Insert the vectorized load. */
   nir_builder b = nir_builder_at(nir_before_instr(&first->instr));
   nir_intrinsic_instr *new_intr =
      nir_intrinsic_instr_create(b.shader, first->intrinsic);

   new_intr->num_components = count;
   nir_def_init(&new_intr->instr, &new_intr->def, count,
                merge_low_high_16_to_32 ? 32 : first->def.bit_size);
   memcpy(new_intr->src, first->src,
          nir_intrinsic_infos[first->intrinsic].num_srcs * sizeof(nir_src));
   nir_intrinsic_copy_const_indices(new_intr, first);
   nir_intrinsic_set_component(new_intr, start);

   if (merge_low_high_16_to_32) {
      nir_io_semantics sem = nir_intrinsic_io_semantics(new_intr);
      sem.high_16bits = 0;
      nir_intrinsic_set_io_semantics(new_intr, sem);
      nir_intrinsic_set_dest_type(new_intr,
                                  (nir_intrinsic_dest_type(new_intr) & ~16) | 32);
   }

   nir_builder_instr_insert(&b, &new_intr->instr);
   nir_def *def = &new_intr->def;

   /* Replace the scalar loads. */
   if (merge_low_high_16_to_32) {
      for (unsigned i = start; i < start + count; i++) {
         nir_def *comp = nir_channel(&b, def, i - start);

         nir_def_rewrite_uses(&chan[i]->def,
                              nir_unpack_32_2x16_split_x(&b, comp));
         nir_def_rewrite_uses(&chan[4 + i]->def,
                              nir_unpack_32_2x16_split_y(&b, comp));
         nir_instr_remove(&chan[i]->instr);
         nir_instr_remove(&chan[4 + i]->instr);
      }
   } else {
      for (unsigned i = start; i < start + count; i++) {
         nir_def_replace(&chan[i]->def, nir_channel(&b, def, i - start));
      }
   }
}

static void
vectorize_store(nir_intrinsic_instr *chan[8], unsigned start, unsigned count,
                bool merge_low_high_16_to_32)
{
   nir_intrinsic_instr *last = NULL;

   /* Find the last instruction where the vectorized store will be
    * inserted.
    */
   for (unsigned i = start; i < start + count; i++) {
      last = !last || chan[i]->instr.index > last->instr.index ?
                chan[i] : last;
      if (merge_low_high_16_to_32) {
         last = !last || chan[4 + i]->instr.index > last->instr.index ?
                   chan[4 + i] : last;
      }
   }

   /* Change the last instruction to a vectorized store. Update xfb first
    * because we need to read some info from "last" before overwriting it.
    */
   if (nir_intrinsic_has_io_xfb(last)) {
      /* 0 = low/full XY channels
       * 1 = low/full ZW channels
       * 2 = high XY channels
       * 3 = high ZW channels
       */
      nir_io_xfb xfb[4] = {{{{0}}}};

      for (unsigned i = start; i < start + count; i++) {
         xfb[i / 2].out[i % 2] =
            (i < 2 ? nir_intrinsic_io_xfb(chan[i]) :
                     nir_intrinsic_io_xfb2(chan[i])).out[i % 2];

         /* Merging low and high 16 bits to 32 bits is not possible
          * with xfb in some cases. (and it's not implemented for
          * cases where it's possible)
          */
         assert(!xfb[i / 2].out[i % 2].num_components ||
                !merge_low_high_16_to_32);
      }

      /* Now vectorize xfb info by merging the individual elements. */
      for (unsigned i = start; i < start + count; i++) {
         /* mediump means that xfb upconverts to 32 bits when writing to
          * memory.
          */
         unsigned xfb_comp_size =
            nir_intrinsic_io_semantics(chan[i]).medium_precision ?
                  32 : chan[i]->src[0].ssa->bit_size;

         for (unsigned j = i + 1; j < start + count; j++) {
            if (xfb[i / 2].out[i % 2].buffer != xfb[j / 2].out[j % 2].buffer ||
                xfb[i / 2].out[i % 2].offset != xfb[j / 2].out[j % 2].offset +
                xfb_comp_size * (j - i))
               break;

            xfb[i / 2].out[i % 2].num_components++;
            memset(&xfb[j / 2].out[j % 2], 0, sizeof(xfb[j / 2].out[j % 2]));
         }
      }

      nir_intrinsic_set_io_xfb(last, xfb[0]);
      nir_intrinsic_set_io_xfb2(last, xfb[1]);
   }

   /* Update gs_streams. */
   unsigned gs_streams = 0;
   for (unsigned i = start; i < start + count; i++) {
      gs_streams |= (nir_intrinsic_io_semantics(chan[i]).gs_streams & 0x3) <<
                    ((i - start) * 2);
   }

   nir_io_semantics sem = nir_intrinsic_io_semantics(last);
   sem.gs_streams = gs_streams;

   /* Update other flags. */
   for (unsigned i = start; i < start + count; i++) {
      if (!nir_intrinsic_io_semantics(chan[i]).no_sysval_output)
         sem.no_sysval_output = 0;
      if (!nir_intrinsic_io_semantics(chan[i]).no_varying)
         sem.no_varying = 0;
      if (nir_intrinsic_io_semantics(chan[i]).invariant)
         sem.invariant = 1;
   }

   if (merge_low_high_16_to_32) {
      /* Update "no" flags for high bits. */
      for (unsigned i = start; i < start + count; i++) {
         if (!nir_intrinsic_io_semantics(chan[4 + i]).no_sysval_output)
            sem.no_sysval_output = 0;
         if (!nir_intrinsic_io_semantics(chan[4 + i]).no_varying)
            sem.no_varying = 0;
         if (nir_intrinsic_io_semantics(chan[4 + i]).invariant)
            sem.invariant = 1;
      }

      /* Update the type. */
      sem.high_16bits = 0;
      nir_intrinsic_set_src_type(last,
                                 (nir_intrinsic_src_type(last) & ~16) | 32);
   }

   /* TODO: Merge names? */

   /* Update the rest. */
   nir_intrinsic_set_io_semantics(last, sem);
   nir_intrinsic_set_component(last, start);
   nir_intrinsic_set_write_mask(last, BITFIELD_MASK(count));
   last->num_components = count;

   nir_builder b = nir_builder_at(nir_before_instr(&last->instr));

   /* Replace the stored scalar with the vector. */
   if (merge_low_high_16_to_32) {
      nir_def *value[4];
      for (unsigned i = start; i < start + count; i++) {
         value[i] = nir_pack_32_2x16_split(&b, chan[i]->src[0].ssa,
                                           chan[4 + i]->src[0].ssa);
      }

      nir_src_rewrite(&last->src[0], nir_vec(&b, &value[start], count));
   } else {
      nir_def *value[8];
      for (unsigned i = start; i < start + count; i++)
         value[i] = chan[i]->src[0].ssa;

      nir_src_rewrite(&last->src[0], nir_vec(&b, &value[start], count));
   }

   /* Remove the scalar stores. */
   for (unsigned i = start; i < start + count; i++) {
      if (chan[i] != last)
         nir_instr_remove(&chan[i]->instr);
      if (merge_low_high_16_to_32 && chan[4 + i] != last)
         nir_instr_remove(&chan[4 + i]->instr);
   }
}

/* Vectorize a vector of scalar instructions. chan[8] are the channels.
 * (the last 4 are the high 16-bit channels)
 */
static bool
vectorize_slot(nir_intrinsic_instr *chan[8], unsigned mask)
{
   bool progress = false;

   /* First, merge low and high 16-bit halves into 32 bits separately when
    * possible. Then vectorize what's left.
    */
   for (int merge_low_high_16_to_32 = 1; merge_low_high_16_to_32 >= 0;
        merge_low_high_16_to_32--) {
      unsigned scan_mask;

      if (merge_low_high_16_to_32) {
         /* Get the subset of the mask where both low and high bits are set. */
         scan_mask = 0;
         for (unsigned i = 0; i < 4; i++) {
            unsigned low_high_bits = BITFIELD_BIT(i) | BITFIELD_BIT(i + 4);

            if ((mask & low_high_bits) == low_high_bits) {
               /* Merging low and high 16 bits to 32 bits is not possible
                * with xfb in some cases. (and it's not implemented for
                * cases where it's possible)
                */
               if (nir_intrinsic_has_io_xfb(chan[i])) {
                  unsigned hi = i + 4;

                  if ((i < 2 ? nir_intrinsic_io_xfb(chan[i])
                             : nir_intrinsic_io_xfb2(chan[i])).out[i % 2].num_components ||
                      (i < 2 ? nir_intrinsic_io_xfb(chan[hi])
                             : nir_intrinsic_io_xfb2(chan[hi])).out[i % 2].num_components)
                     continue;
               }

               /* The GS stream must be the same for both halves. */
               if ((nir_intrinsic_io_semantics(chan[i]).gs_streams & 0x3) !=
                   (nir_intrinsic_io_semantics(chan[4 + i]).gs_streams & 0x3))
                  continue;

               scan_mask |= BITFIELD_BIT(i);
               mask &= ~low_high_bits;
            }
         }
      } else {
         scan_mask = mask;
      }

      while (scan_mask) {
         int start, count;

         u_bit_scan_consecutive_range(&scan_mask, &start, &count);

         if (count == 1 && !merge_low_high_16_to_32)
            continue; /* There is nothing to vectorize. */

         bool is_load = nir_intrinsic_infos[chan[start]->intrinsic].has_dest;

         if (is_load)
            vectorize_load(chan, start, count, merge_low_high_16_to_32);
         else
            vectorize_store(chan, start, count, merge_low_high_16_to_32);

         progress = true;
      }
   }

   return progress;
}

static bool
vectorize_batch(struct util_dynarray *io_instructions)
{
   unsigned num_instr = util_dynarray_num_elements(io_instructions, void *);

   /* We need to at least 2 instructions to have something to do. */
   if (num_instr <= 1) {
      /* Clear the array. The next block will reuse it. */
      util_dynarray_clear(io_instructions);
      return false;
   }

   /* The instructions are sorted such that groups of vectorizable
    * instructions are next to each other. Multiple incompatible
    * groups of vectorizable instructions can occur in this array.
    * The reason why 2 groups would be incompatible is that they
    * could have a different intrinsic, indirect index, array index,
    * vertex index, barycentrics, or location. Each group is vectorized
    * separately.
    *
    * This reorders instructions in the array, but not in the shader.
    */
   qsort(io_instructions->data, num_instr, sizeof(void*), compare_intr);

   nir_intrinsic_instr *chan[8] = {0}, *prev = NULL;
   unsigned chan_mask = 0;
   bool progress = false;

   /* Vectorize all groups.
    *
    * The channels for each group are gathered. If 2 stores overwrite
    * the same channel, the earlier store is DCE'd here.
    */
   util_dynarray_foreach(io_instructions, nir_intrinsic_instr *, intr) {
      /* If the next instruction is not vectorizable, vectorize what
       * we have gathered so far.
       */
      if (prev && compare_is_not_vectorizable(prev, *intr)) {
         /* We need at least 2 instructions to have something to do. */
         if (util_bitcount(chan_mask) > 1)
            progress |= vectorize_slot(chan, chan_mask);

         prev = NULL;
         memset(chan, 0, sizeof(chan));
         chan_mask = 0;
      }

      /* This performs DCE of output stores because the previous value
       * is being overwritten.
       */
      unsigned index = nir_intrinsic_io_semantics(*intr).high_16bits * 4 +
                       nir_intrinsic_component(*intr);
      bool is_store = !nir_intrinsic_infos[(*intr)->intrinsic].has_dest;
      if (is_store && chan[index])
         nir_instr_remove(&chan[index]->instr);

      /* Gather the channel. */
      chan[index] = *intr;
      prev = *intr;
      chan_mask |= BITFIELD_BIT(index);
   }

   /* Vectorize the last group. */
   if (prev && util_bitcount(chan_mask) > 1)
      progress |= vectorize_slot(chan, chan_mask);

   /* Clear the array. The next block will reuse it. */
   util_dynarray_clear(io_instructions);
   return progress;
}

bool
nir_opt_vectorize_io(nir_shader *shader, nir_variable_mode modes)
{
   assert(!(modes & ~(nir_var_shader_in | nir_var_shader_out)));

   if (shader->info.stage == MESA_SHADER_FRAGMENT &&
       shader->options->io_options & nir_io_prefer_scalar_fs_inputs)
      modes &= ~nir_var_shader_in;

   if ((shader->info.stage == MESA_SHADER_TESS_CTRL ||
        shader->info.stage == MESA_SHADER_GEOMETRY) &&
       util_bitcount(modes) == 2) {
      /* When vectorizing TCS and GS IO, inputs can ignore barriers and emits,
       * but that is only done when outputs are ignored, so vectorize them
       * separately.
       */
      bool progress_in = nir_opt_vectorize_io(shader, nir_var_shader_in);
      bool progress_out = nir_opt_vectorize_io(shader, nir_var_shader_out);
      return progress_in || progress_out;
   }

   /* Initialize dynamic arrays. */
   struct util_dynarray io_instructions;
   util_dynarray_init(&io_instructions, NULL);
   bool global_progress = false;

   nir_foreach_function_impl(impl, shader) {
      bool progress = false;
      nir_metadata_require(impl, nir_metadata_instr_index);

      nir_foreach_block(block, impl) {
         BITSET_DECLARE(has_output_loads, NUM_TOTAL_VARYING_SLOTS * 8);
         BITSET_DECLARE(has_output_stores, NUM_TOTAL_VARYING_SLOTS * 8);
         BITSET_ZERO(has_output_loads);
         BITSET_ZERO(has_output_stores);

         /* Gather load/store intrinsics within the block. */
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            bool is_load = nir_intrinsic_infos[intr->intrinsic].has_dest;
            bool is_output = false;
            nir_io_semantics sem = {0};
            unsigned index = 0;

            if (nir_intrinsic_has_io_semantics(intr)) {
               sem = nir_intrinsic_io_semantics(intr);
               assert(sem.location < NUM_TOTAL_VARYING_SLOTS);
               index = sem.location * 8 + sem.high_16bits * 4 +
                       nir_intrinsic_component(intr);
            }

            switch (intr->intrinsic) {
            case nir_intrinsic_load_input:
            case nir_intrinsic_load_per_primitive_input:
            case nir_intrinsic_load_input_vertex:
            case nir_intrinsic_load_interpolated_input:
            case nir_intrinsic_load_per_vertex_input:
               if (!(modes & nir_var_shader_in))
                  continue;
               break;

            case nir_intrinsic_load_output:
            case nir_intrinsic_load_per_vertex_output:
            case nir_intrinsic_load_per_primitive_output:
            case nir_intrinsic_store_output:
            case nir_intrinsic_store_per_vertex_output:
            case nir_intrinsic_store_per_primitive_output:
               if (!(modes & nir_var_shader_out))
                  continue;

               /* Break the batch if an output load is followed by an output
                * store to the same channel and vice versa.
                */
               if (BITSET_TEST(is_load ? has_output_stores : has_output_loads,
                               index)) {
                  progress |= vectorize_batch(&io_instructions);
                  BITSET_ZERO(has_output_loads);
                  BITSET_ZERO(has_output_stores);
               }
               is_output = true;
               break;

            case nir_intrinsic_barrier:
               /* Don't vectorize across TCS barriers. */
               if (modes & nir_var_shader_out &&
                   nir_intrinsic_memory_modes(intr) & nir_var_shader_out) {
                  progress |= vectorize_batch(&io_instructions);
                  BITSET_ZERO(has_output_loads);
                  BITSET_ZERO(has_output_stores);
               }
               continue;

            case nir_intrinsic_emit_vertex:
               /* Don't vectorize across GS emits. */
               progress |= vectorize_batch(&io_instructions);
               BITSET_ZERO(has_output_loads);
               BITSET_ZERO(has_output_stores);
               continue;

            default:
               continue;
            }

            /* Only scalar 16 and 32-bit instructions are allowed. */
            ASSERTED nir_def *value = is_load ? &intr->def : intr->src[0].ssa;
            assert(value->num_components == 1);
            assert(value->bit_size == 16 || value->bit_size == 32);

            util_dynarray_append(&io_instructions, void *, intr);
            if (is_output)
               BITSET_SET(is_load ? has_output_loads : has_output_stores, index);
         }

         progress |= vectorize_batch(&io_instructions);
      }

      nir_metadata_preserve(impl, progress ? (nir_metadata_block_index |
                                              nir_metadata_dominance) :
                                             nir_metadata_all);
      global_progress |= progress;
   }
   util_dynarray_fini(&io_instructions);

   return global_progress;
}
