/*
 * Copyright © 2018 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_deref.h"

static bool
index_ssa_def_cb(nir_ssa_def *def, void *state)
{
   unsigned *index = (unsigned *) state;
   def->index = (*index)++;

   return true;
}

static nir_deref_instr *
get_deref_for_load_src(nir_src src, unsigned first_valid_load)
{
   nir_intrinsic_instr *load = nir_src_as_intrinsic(src);
   if (load == NULL || load->intrinsic != nir_intrinsic_load_deref)
      return NULL;

   if (load->dest.ssa.index < first_valid_load)
      return NULL;

   return nir_src_as_deref(load->src[0]);
}

struct match_state {
   /* Index into the array of the last copy or -1 for no ongoing copy. */
   unsigned next_array_idx;

   /* Length of the array we're copying */
   unsigned array_len;

   /* Index into the deref path to the array we think is being copied */
   int src_deref_array_idx;
   int dst_deref_array_idx;

   /* Deref paths of the first load/store pair or copy */
   nir_deref_path first_src_path;
   nir_deref_path first_dst_path;
};

static void
match_state_init(struct match_state *state)
{
   state->next_array_idx = 0;
   state->array_len = 0;
   state->src_deref_array_idx = -1;
   state->dst_deref_array_idx = -1;
}

static void
match_state_finish(struct match_state *state)
{
   if (state->next_array_idx > 0) {
      nir_deref_path_finish(&state->first_src_path);
      nir_deref_path_finish(&state->first_dst_path);
   }
}

static void
match_state_reset(struct match_state *state)
{
   match_state_finish(state);
   match_state_init(state);
}

static bool
try_match_deref(nir_deref_path *base_path, int *path_array_idx,
                nir_deref_instr *deref, int arr_idx, void *mem_ctx)
{
   nir_deref_path deref_path;
   nir_deref_path_init(&deref_path, deref, mem_ctx);

   bool found = false;
   for (int i = 0; ; i++) {
      nir_deref_instr *b = base_path->path[i];
      nir_deref_instr *d = deref_path.path[i];
      /* They have to be the same length */
      if ((b == NULL) != (d == NULL))
         goto fail;

      if (b == NULL)
         break;

      /* This can happen if one is a deref_array and the other a wildcard */
      if (b->deref_type != d->deref_type)
         goto fail;

      switch (b->deref_type) {
      case nir_deref_type_var:
         if (b->var != d->var)
            goto fail;
         continue;

      case nir_deref_type_array:
         assert(b->arr.index.is_ssa && d->arr.index.is_ssa);
         const bool const_b_idx = nir_src_is_const(b->arr.index);
         const bool const_d_idx = nir_src_is_const(d->arr.index);
         const unsigned b_idx = const_b_idx ? nir_src_as_uint(b->arr.index) : 0;
         const unsigned d_idx = const_d_idx ? nir_src_as_uint(d->arr.index) : 0;

         /* If we don't have an index into the path yet or if this entry in
          * the path is at the array index, see if this is a candidate.  We're
          * looking for an index which is zero in the base deref and arr_idx
          * in the search deref.
          */
         if ((*path_array_idx < 0 || *path_array_idx == i) &&
             const_b_idx && b_idx == 0 &&
             const_d_idx && d_idx == arr_idx) {
            *path_array_idx = i;
            continue;
         }

         /* We're at the array index but not a candidate */
         if (*path_array_idx == i)
            goto fail;

         /* If we're not the path array index, we must match exactly.  We
          * could probably just compare SSA values and trust in copy
          * propagation but doing it ourselves means this pass can run a bit
          * earlier.
          */
         if (b->arr.index.ssa == d->arr.index.ssa ||
             (const_b_idx && const_d_idx && b_idx == d_idx))
            continue;

         goto fail;

      case nir_deref_type_array_wildcard:
         continue;

      case nir_deref_type_struct:
         if (b->strct.index != d->strct.index)
            goto fail;
         continue;

      default:
         unreachable("Invalid deref type in a path");
      }
   }

   /* If we got here without failing, we've matched.  However, it isn't an
    * array match unless we found an altered array index.
    */
   found = *path_array_idx > 0;

fail:
   nir_deref_path_finish(&deref_path);
   return found;
}

static nir_deref_instr *
build_wildcard_deref(nir_builder *b, nir_deref_path *path,
                     unsigned wildcard_idx)
{
   assert(path->path[wildcard_idx]->deref_type == nir_deref_type_array);

   nir_deref_instr *tail =
      nir_build_deref_array_wildcard(b, path->path[wildcard_idx - 1]);

   for (unsigned i = wildcard_idx + 1; path->path[i]; i++)
      tail = nir_build_deref_follower(b, tail, path->path[i]);

   return tail;
}

static bool
opt_find_array_copies_block(nir_builder *b, nir_block *block,
                            unsigned *num_ssa_defs, void *mem_ctx)
{
   bool progress = false;

   struct match_state s;
   match_state_init(&s);

   nir_variable *dst_var = NULL;
   unsigned prev_dst_var_last_write = *num_ssa_defs;
   unsigned dst_var_last_write = *num_ssa_defs;

   nir_foreach_instr(instr, block) {
      /* Index the SSA defs before we do anything else. */
      nir_foreach_ssa_def(instr, index_ssa_def_cb, num_ssa_defs);

      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      if (intrin->intrinsic != nir_intrinsic_copy_deref &&
          intrin->intrinsic != nir_intrinsic_store_deref)
         continue;

      nir_deref_instr *dst_deref = nir_src_as_deref(intrin->src[0]);

      /* The destination must be local.  If we see a non-local store, we
       * continue on because it won't affect local stores or read-only
       * variables.
       */
      if (dst_deref->mode != nir_var_function_temp)
         continue;

      /* We keep track of the SSA indices where the two last-written
       * variables are written.  The prev_dst_var_last_write tells us when
       * the last store_deref to something other than dst happened.  If the
       * SSA def index from a load is greater than or equal to this number
       * then we know it happened afterwards and no writes to anything other
       * than dst occur between the load and the current instruction.
       */
      if (nir_deref_instr_get_variable(dst_deref) != dst_var) {
         prev_dst_var_last_write = dst_var_last_write;
         dst_var = nir_deref_instr_get_variable(dst_deref);
      }
      dst_var_last_write = *num_ssa_defs;

      /* If it's a full variable store or copy, reset.  This will trigger
       * eventually because we'll fail to match an array element.  However,
       * it's a cheap early-exit.
       */
      if (dst_deref->deref_type == nir_deref_type_var)
         goto reset;

      nir_deref_instr *src_deref;
      if (intrin->intrinsic == nir_intrinsic_copy_deref) {
         src_deref = nir_src_as_deref(intrin->src[1]);
      } else {
         assert(intrin->intrinsic == nir_intrinsic_store_deref);
         src_deref = get_deref_for_load_src(intrin->src[1],
                                            prev_dst_var_last_write);

         /* We can only handle full writes */
         if (nir_intrinsic_write_mask(intrin) !=
             (1 << glsl_get_components(dst_deref->type)) - 1)
            goto reset;
      }

      /* If we didn't find a valid src, then we have an unknown store and it
       * could mess things up.
       */
      if (src_deref == NULL)
         goto reset;

      /* The source must be either local or something that's guaranteed to be
       * read-only.
       */
      const nir_variable_mode read_only_modes =
         nir_var_shader_in | nir_var_uniform | nir_var_system_value;
      if (!(src_deref->mode & (nir_var_function_temp | read_only_modes)))
         goto reset;

      /* If we don't yet have an active copy, then make this instruction the
       * active copy.
       */
      if (s.next_array_idx == 0) {
         /* We can't combine a copy if there is any chance the source and
          * destination will end up aliasing.  Just bail if they're the same
          * variable.
          */
         if (nir_deref_instr_get_variable(src_deref) == dst_var)
            goto reset;

         /* The load/store pair is enough to guarantee the same bit size and
          * number of components but a copy_var requires the actual types to
          * match.
          */
         if (dst_deref->type != src_deref->type)
            continue;

         /* The first time we see a store, we don't know which array in the
          * deref path is the one being copied so we just record the paths
          * as-is and continue.  On the next iteration, it will try to match
          * based on which array index changed.
          */
         nir_deref_path_init(&s.first_dst_path, dst_deref, mem_ctx);
         nir_deref_path_init(&s.first_src_path, src_deref, mem_ctx);
         s.next_array_idx = 1;
         continue;
      }

      if (!try_match_deref(&s.first_dst_path, &s.dst_deref_array_idx,
                           dst_deref, s.next_array_idx, mem_ctx) ||
          !try_match_deref(&s.first_src_path, &s.src_deref_array_idx,
                           src_deref, s.next_array_idx, mem_ctx))
         goto reset;

      if (s.next_array_idx == 1) {
         /* This is our first non-trivial match.  We now have indices into
          * the search paths so we can do a couple more checks.
          */
         assert(s.dst_deref_array_idx > 0 && s.src_deref_array_idx > 0);
         const struct glsl_type *dst_arr_type =
            s.first_dst_path.path[s.dst_deref_array_idx - 1]->type;
         const struct glsl_type *src_arr_type =
            s.first_src_path.path[s.src_deref_array_idx - 1]->type;

         assert(glsl_type_is_array(dst_arr_type) ||
                glsl_type_is_matrix(dst_arr_type));
         assert(glsl_type_is_array(src_arr_type) ||
                glsl_type_is_matrix(src_arr_type));

         /* They must be the same length */
         s.array_len = glsl_get_length(dst_arr_type);
         if (s.array_len != glsl_get_length(src_arr_type))
            goto reset;
      }

      s.next_array_idx++;

      if (s.next_array_idx == s.array_len) {
         /* Hooray, We found a copy! */
         b->cursor = nir_after_instr(instr);
         nir_copy_deref(b, build_wildcard_deref(b, &s.first_dst_path,
                                                s.dst_deref_array_idx),
                           build_wildcard_deref(b, &s.first_src_path,
                                                s.src_deref_array_idx));
         match_state_reset(&s);
         progress = true;
      }

      continue;

   reset:
      match_state_reset(&s);
   }

   return progress;
}

static bool
opt_find_array_copies_impl(nir_function_impl *impl)
{
   nir_builder b;
   nir_builder_init(&b, impl);

   bool progress = false;

   void *mem_ctx = ralloc_context(NULL);

   /* We re-index the SSA defs as we go; it makes it easier to handle
    * resetting the state machine.
    */
   unsigned num_ssa_defs = 0;

   nir_foreach_block(block, impl) {
      if (opt_find_array_copies_block(&b, block, &num_ssa_defs, mem_ctx))
         progress = true;
   }

   impl->ssa_alloc = num_ssa_defs;

   ralloc_free(mem_ctx);

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
   }

   return progress;
}

/**
 * This peephole optimization looks for a series of load/store_deref or
 * copy_deref instructions that copy an array from one variable to another and
 * turns it into a copy_deref that copies the entire array.  The pattern it
 * looks for is extremely specific but it's good enough to pick up on the
 * input array copies in DXVK and should also be able to pick up the sequence
 * generated by spirv_to_nir for a OpLoad of a large composite followed by
 * OpStore.
 *
 * TODO: Use a hash table approach to support out-of-order and interleaved
 * copies.
 */
bool
nir_opt_find_array_copies(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl && opt_find_array_copies_impl(function->impl))
         progress = true;
   }

   return progress;
}
