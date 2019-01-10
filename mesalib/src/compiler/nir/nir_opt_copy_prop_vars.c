/*
 * Copyright © 2016 Intel Corporation
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

#include "util/bitscan.h"
#include "util/u_dynarray.h"

/**
 * Variable-based copy propagation
 *
 * Normally, NIR trusts in SSA form for most of its copy-propagation needs.
 * However, there are cases, especially when dealing with indirects, where SSA
 * won't help you.  This pass is for those times.  Specifically, it handles
 * the following things that the rest of NIR can't:
 *
 *  1) Copy-propagation on variables that have indirect access.  This includes
 *     propagating from indirect stores into indirect loads.
 *
 *  2) Removal of redundant load_deref intrinsics.  We can't trust regular CSE
 *     to do this because it isn't aware of variable writes that may alias the
 *     value and make the former load invalid.
 *
 * This pass uses an intermediate solution between being local / "per-block"
 * and a complete data-flow analysis.  It follows the control flow graph, and
 * propagate the available copy information forward, invalidating data at each
 * cf_node.
 *
 * Removal of dead writes to variables is handled by another pass.
 */

struct vars_written {
   nir_variable_mode modes;

   /* Key is deref and value is the uintptr_t with the write mask. */
   struct hash_table *derefs;
};

struct value {
   bool is_ssa;
   union {
      nir_ssa_def *ssa[4];
      nir_deref_instr *deref;
   };
};

struct copy_entry {
   struct value src;

   nir_deref_instr *dst;
};

struct copy_prop_var_state {
   nir_function_impl *impl;

   void *mem_ctx;
   void *lin_ctx;

   /* Maps nodes to vars_written.  Used to invalidate copy entries when
    * visiting each node.
    */
   struct hash_table *vars_written_map;

   bool progress;
};

static bool
value_equals_store_src(struct value *value, nir_intrinsic_instr *intrin)
{
   assert(intrin->intrinsic == nir_intrinsic_store_deref);
   uintptr_t write_mask = nir_intrinsic_write_mask(intrin);

   for (unsigned i = 0; i < intrin->num_components; i++) {
      if ((write_mask & (1 << i)) &&
          value->ssa[i] != intrin->src[1].ssa)
         return false;
   }

   return true;
}

static struct vars_written *
create_vars_written(struct copy_prop_var_state *state)
{
   struct vars_written *written =
      linear_zalloc_child(state->lin_ctx, sizeof(struct vars_written));
   written->derefs = _mesa_hash_table_create(state->mem_ctx, _mesa_hash_pointer,
                                             _mesa_key_pointer_equal);
   return written;
}

static void
gather_vars_written(struct copy_prop_var_state *state,
                    struct vars_written *written,
                    nir_cf_node *cf_node)
{
   struct vars_written *new_written = NULL;

   switch (cf_node->type) {
   case nir_cf_node_function: {
      nir_function_impl *impl = nir_cf_node_as_function(cf_node);
      foreach_list_typed_safe(nir_cf_node, cf_node, node, &impl->body)
         gather_vars_written(state, NULL, cf_node);
      break;
   }

   case nir_cf_node_block: {
      if (!written)
         break;

      nir_block *block = nir_cf_node_as_block(cf_node);
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_call) {
            written->modes |= nir_var_shader_out |
                              nir_var_private |
                              nir_var_function |
                              nir_var_ssbo |
                              nir_var_shared;
            continue;
         }

         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         switch (intrin->intrinsic) {
         case nir_intrinsic_barrier:
         case nir_intrinsic_memory_barrier:
            written->modes |= nir_var_shader_out |
                              nir_var_ssbo |
                              nir_var_shared;
            break;

         case nir_intrinsic_emit_vertex:
         case nir_intrinsic_emit_vertex_with_counter:
            written->modes = nir_var_shader_out;
            break;

         case nir_intrinsic_store_deref:
         case nir_intrinsic_copy_deref: {
            /* Destination in _both_ store_deref and copy_deref is src[0]. */
            nir_deref_instr *dst = nir_src_as_deref(intrin->src[0]);

            uintptr_t mask = intrin->intrinsic == nir_intrinsic_store_deref ?
               nir_intrinsic_write_mask(intrin) : (1 << glsl_get_vector_elements(dst->type)) - 1;

            struct hash_entry *ht_entry = _mesa_hash_table_search(written->derefs, dst);
            if (ht_entry)
               ht_entry->data = (void *)(mask | (uintptr_t)ht_entry->data);
            else
               _mesa_hash_table_insert(written->derefs, dst, (void *)mask);

            break;
         }

         default:
            break;
         }
      }

      break;
   }

   case nir_cf_node_if: {
      nir_if *if_stmt = nir_cf_node_as_if(cf_node);

      new_written = create_vars_written(state);

      foreach_list_typed_safe(nir_cf_node, cf_node, node, &if_stmt->then_list)
         gather_vars_written(state, new_written, cf_node);

      foreach_list_typed_safe(nir_cf_node, cf_node, node, &if_stmt->else_list)
         gather_vars_written(state, new_written, cf_node);

      break;
   }

   case nir_cf_node_loop: {
      nir_loop *loop = nir_cf_node_as_loop(cf_node);

      new_written = create_vars_written(state);

      foreach_list_typed_safe(nir_cf_node, cf_node, node, &loop->body)
         gather_vars_written(state, new_written, cf_node);

      break;
   }

   default:
      unreachable("Invalid CF node type");
   }

   if (new_written) {
      /* Merge new information to the parent control flow node. */
      if (written) {
         written->modes |= new_written->modes;
         hash_table_foreach(new_written->derefs, new_entry) {
            struct hash_entry *old_entry =
               _mesa_hash_table_search_pre_hashed(written->derefs, new_entry->hash,
                                                  new_entry->key);
            if (old_entry) {
               nir_component_mask_t merged = (uintptr_t) new_entry->data |
                                             (uintptr_t) old_entry->data;
               old_entry->data = (void *) ((uintptr_t) merged);
            } else {
               _mesa_hash_table_insert_pre_hashed(written->derefs, new_entry->hash,
                                                  new_entry->key, new_entry->data);
            }
         }
      }
      _mesa_hash_table_insert(state->vars_written_map, cf_node, new_written);
   }
}

static struct copy_entry *
copy_entry_create(struct util_dynarray *copies,
                  nir_deref_instr *dst_deref)
{
   struct copy_entry new_entry = {
      .dst = dst_deref,
   };
   util_dynarray_append(copies, struct copy_entry, new_entry);
   return util_dynarray_top_ptr(copies, struct copy_entry);
}

/* Remove copy entry by swapping it with the last element and reducing the
 * size.  If used inside an iteration on copies, it must be a reverse
 * (backwards) iteration.  It is safe to use in those cases because the swap
 * will not affect the rest of the iteration.
 */
static void
copy_entry_remove(struct util_dynarray *copies,
                  struct copy_entry *entry)
{
   /* This also works when removing the last element since pop don't shrink
    * the memory used by the array, so the swap is useless but not invalid.
    */
   *entry = util_dynarray_pop(copies, struct copy_entry);
}

static struct copy_entry *
lookup_entry_for_deref(struct util_dynarray *copies,
                       nir_deref_instr *deref,
                       nir_deref_compare_result allowed_comparisons)
{
   util_dynarray_foreach(copies, struct copy_entry, iter) {
      if (nir_compare_derefs(iter->dst, deref) & allowed_comparisons)
         return iter;
   }

   return NULL;
}

static struct copy_entry *
lookup_entry_and_kill_aliases(struct util_dynarray *copies,
                              nir_deref_instr *deref,
                              unsigned write_mask)
{
   /* TODO: Take into account the write_mask. */

   nir_deref_instr *dst_match = NULL;
   util_dynarray_foreach_reverse(copies, struct copy_entry, iter) {
      if (!iter->src.is_ssa) {
         /* If this write aliases the source of some entry, get rid of it */
         if (nir_compare_derefs(iter->src.deref, deref) & nir_derefs_may_alias_bit) {
            copy_entry_remove(copies, iter);
            continue;
         }
      }

      nir_deref_compare_result comp = nir_compare_derefs(iter->dst, deref);

      if (comp & nir_derefs_equal_bit) {
         /* Removing entries invalidate previous iter pointers, so we'll
          * collect the matching entry later.  Just make sure it is unique.
          */
         assert(!dst_match);
         dst_match = iter->dst;
      } else if (comp & nir_derefs_may_alias_bit) {
         copy_entry_remove(copies, iter);
      }
   }

   struct copy_entry *entry = NULL;
   if (dst_match) {
      util_dynarray_foreach(copies, struct copy_entry, iter) {
         if (iter->dst == dst_match) {
            entry = iter;
            break;
         }
      }
      assert(entry);
   }
   return entry;
}

static void
kill_aliases(struct util_dynarray *copies,
             nir_deref_instr *deref,
             unsigned write_mask)
{
   /* TODO: Take into account the write_mask. */

   struct copy_entry *entry =
      lookup_entry_and_kill_aliases(copies, deref, write_mask);
   if (entry)
      copy_entry_remove(copies, entry);
}

static struct copy_entry *
get_entry_and_kill_aliases(struct util_dynarray *copies,
                           nir_deref_instr *deref,
                           unsigned write_mask)
{
   /* TODO: Take into account the write_mask. */

   struct copy_entry *entry =
      lookup_entry_and_kill_aliases(copies, deref, write_mask);

   if (entry == NULL)
      entry = copy_entry_create(copies, deref);

   return entry;
}

static void
apply_barrier_for_modes(struct util_dynarray *copies,
                        nir_variable_mode modes)
{
   util_dynarray_foreach_reverse(copies, struct copy_entry, iter) {
      if ((iter->dst->mode & modes) ||
          (!iter->src.is_ssa && (iter->src.deref->mode & modes)))
         copy_entry_remove(copies, iter);
   }
}

static void
store_to_entry(struct copy_prop_var_state *state, struct copy_entry *entry,
               const struct value *value, unsigned write_mask)
{
   if (value->is_ssa) {
      /* Clear src if it was being used as non-SSA. */
      if (!entry->src.is_ssa)
         memset(entry->src.ssa, 0, sizeof(entry->src.ssa));
      entry->src.is_ssa = true;
      /* Only overwrite the written components */
      for (unsigned i = 0; i < 4; i++) {
         if (write_mask & (1 << i))
            entry->src.ssa[i] = value->ssa[i];
      }
   } else {
      /* Non-ssa stores always write everything */
      entry->src.is_ssa = false;
      entry->src.deref = value->deref;
   }
}

/* Do a "load" from an SSA-based entry return it in "value" as a value with a
 * single SSA def.  Because an entry could reference up to 4 different SSA
 * defs, a vecN operation may be inserted to combine them into a single SSA
 * def before handing it back to the caller.  If the load instruction is no
 * longer needed, it is removed and nir_instr::block is set to NULL.  (It is
 * possible, in some cases, for the load to be used in the vecN operation in
 * which case it isn't deleted.)
 */
static bool
load_from_ssa_entry_value(struct copy_prop_var_state *state,
                          struct copy_entry *entry,
                          nir_builder *b, nir_intrinsic_instr *intrin,
                          struct value *value)
{
   *value = entry->src;
   assert(value->is_ssa);

   const struct glsl_type *type = entry->dst->type;
   unsigned num_components = glsl_get_vector_elements(type);

   nir_component_mask_t available = 0;
   bool all_same = true;
   for (unsigned i = 0; i < num_components; i++) {
      if (value->ssa[i])
         available |= (1 << i);

      if (value->ssa[i] != value->ssa[0])
         all_same = false;
   }

   if (all_same) {
      /* Our work here is done */
      b->cursor = nir_instr_remove(&intrin->instr);
      intrin->instr.block = NULL;
      return true;
   }

   if (available != (1 << num_components) - 1 &&
       intrin->intrinsic == nir_intrinsic_load_deref &&
       (available & nir_ssa_def_components_read(&intrin->dest.ssa)) == 0) {
      /* If none of the components read are available as SSA values, then we
       * should just bail.  Otherwise, we would end up replacing the uses of
       * the load_deref a vecN() that just gathers up its components.
       */
      return false;
   }

   b->cursor = nir_after_instr(&intrin->instr);

   nir_ssa_def *load_def =
      intrin->intrinsic == nir_intrinsic_load_deref ? &intrin->dest.ssa : NULL;

   bool keep_intrin = false;
   nir_ssa_def *comps[NIR_MAX_VEC_COMPONENTS];
   for (unsigned i = 0; i < num_components; i++) {
      if (value->ssa[i]) {
         comps[i] = nir_channel(b, value->ssa[i], i);
      } else {
         /* We don't have anything for this component in our
          * list.  Just re-use a channel from the load.
          */
         if (load_def == NULL)
            load_def = nir_load_deref(b, entry->dst);

         if (load_def->parent_instr == &intrin->instr)
            keep_intrin = true;

         comps[i] = nir_channel(b, load_def, i);
      }
   }

   nir_ssa_def *vec = nir_vec(b, comps, num_components);
   for (unsigned i = 0; i < num_components; i++)
      value->ssa[i] = vec;

   if (!keep_intrin) {
      /* Removing this instruction should not touch the cursor because we
       * created the cursor after the intrinsic and have added at least one
       * instruction (the vec) since then.
       */
      assert(b->cursor.instr != &intrin->instr);
      nir_instr_remove(&intrin->instr);
      intrin->instr.block = NULL;
   }

   return true;
}

/**
 * Specialize the wildcards in a deref chain
 *
 * This function returns a deref chain identical to \param deref except that
 * some of its wildcards are replaced with indices from \param specific.  The
 * process is guided by \param guide which references the same type as \param
 * specific but has the same wildcard array lengths as \param deref.
 */
static nir_deref_instr *
specialize_wildcards(nir_builder *b,
                     nir_deref_path *deref,
                     nir_deref_path *guide,
                     nir_deref_path *specific)
{
   nir_deref_instr **deref_p = &deref->path[1];
   nir_deref_instr **guide_p = &guide->path[1];
   nir_deref_instr **spec_p = &specific->path[1];
   nir_deref_instr *ret_tail = deref->path[0];
   for (; *deref_p; deref_p++) {
      if ((*deref_p)->deref_type == nir_deref_type_array_wildcard) {
         /* This is where things get tricky.  We have to search through
          * the entry deref to find its corresponding wildcard and fill
          * this slot in with the value from the src.
          */
         while (*guide_p &&
                (*guide_p)->deref_type != nir_deref_type_array_wildcard) {
            guide_p++;
            spec_p++;
         }
         assert(*guide_p && *spec_p);

         ret_tail = nir_build_deref_follower(b, ret_tail, *spec_p);

         guide_p++;
         spec_p++;
      } else {
         ret_tail = nir_build_deref_follower(b, ret_tail, *deref_p);
      }
   }

   return ret_tail;
}

/* Do a "load" from an deref-based entry return it in "value" as a value.  The
 * deref returned in "value" will always be a fresh copy so the caller can
 * steal it and assign it to the instruction directly without copying it
 * again.
 */
static bool
load_from_deref_entry_value(struct copy_prop_var_state *state,
                            struct copy_entry *entry,
                            nir_builder *b, nir_intrinsic_instr *intrin,
                            nir_deref_instr *src, struct value *value)
{
   *value = entry->src;

   b->cursor = nir_instr_remove(&intrin->instr);

   nir_deref_path entry_dst_path, src_path;
   nir_deref_path_init(&entry_dst_path, entry->dst, state->mem_ctx);
   nir_deref_path_init(&src_path, src, state->mem_ctx);

   bool need_to_specialize_wildcards = false;
   nir_deref_instr **entry_p = &entry_dst_path.path[1];
   nir_deref_instr **src_p = &src_path.path[1];
   while (*entry_p && *src_p) {
      nir_deref_instr *entry_tail = *entry_p++;
      nir_deref_instr *src_tail = *src_p++;

      if (src_tail->deref_type == nir_deref_type_array &&
          entry_tail->deref_type == nir_deref_type_array_wildcard)
         need_to_specialize_wildcards = true;
   }

   /* If the entry deref is longer than the source deref then it refers to a
    * smaller type and we can't source from it.
    */
   assert(*entry_p == NULL);

   if (need_to_specialize_wildcards) {
      /* The entry has some wildcards that are not in src.  This means we need
       * to construct a new deref based on the entry but using the wildcards
       * from the source and guided by the entry dst.  Oof.
       */
      nir_deref_path entry_src_path;
      nir_deref_path_init(&entry_src_path, entry->src.deref, state->mem_ctx);
      value->deref = specialize_wildcards(b, &entry_src_path,
                                          &entry_dst_path, &src_path);
      nir_deref_path_finish(&entry_src_path);
   }

   /* If our source deref is longer than the entry deref, that's ok because
    * it just means the entry deref needs to be extended a bit.
    */
   while (*src_p) {
      nir_deref_instr *src_tail = *src_p++;
      value->deref = nir_build_deref_follower(b, value->deref, src_tail);
   }

   nir_deref_path_finish(&entry_dst_path);
   nir_deref_path_finish(&src_path);

   return true;
}

static bool
try_load_from_entry(struct copy_prop_var_state *state, struct copy_entry *entry,
                    nir_builder *b, nir_intrinsic_instr *intrin,
                    nir_deref_instr *src, struct value *value)
{
   if (entry == NULL)
      return false;

   if (entry->src.is_ssa) {
      return load_from_ssa_entry_value(state, entry, b, intrin, value);
   } else {
      return load_from_deref_entry_value(state, entry, b, intrin, src, value);
   }
}

static void
invalidate_copies_for_cf_node(struct copy_prop_var_state *state,
                              struct util_dynarray *copies,
                              nir_cf_node *cf_node)
{
   struct hash_entry *ht_entry = _mesa_hash_table_search(state->vars_written_map, cf_node);
   assert(ht_entry);

   struct vars_written *written = ht_entry->data;
   if (written->modes) {
      util_dynarray_foreach_reverse(copies, struct copy_entry, entry) {
         if (entry->dst->mode & written->modes)
            copy_entry_remove(copies, entry);
      }
   }

   hash_table_foreach (written->derefs, entry) {
      nir_deref_instr *deref_written = (nir_deref_instr *)entry->key;
      kill_aliases(copies, deref_written, (uintptr_t)entry->data);
   }
}

static void
copy_prop_vars_block(struct copy_prop_var_state *state,
                     nir_builder *b, nir_block *block,
                     struct util_dynarray *copies)
{
   nir_foreach_instr_safe(instr, block) {
      if (instr->type == nir_instr_type_call) {
         apply_barrier_for_modes(copies, nir_var_shader_out |
                                         nir_var_private |
                                         nir_var_function |
                                         nir_var_ssbo |
                                         nir_var_shared);
         continue;
      }

      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_barrier:
      case nir_intrinsic_memory_barrier:
         apply_barrier_for_modes(copies, nir_var_shader_out |
                                         nir_var_ssbo |
                                         nir_var_shared);
         break;

      case nir_intrinsic_emit_vertex:
      case nir_intrinsic_emit_vertex_with_counter:
         apply_barrier_for_modes(copies, nir_var_shader_out);
         break;

      case nir_intrinsic_load_deref: {
         nir_deref_instr *src = nir_src_as_deref(intrin->src[0]);

         struct copy_entry *src_entry =
            lookup_entry_for_deref(copies, src, nir_derefs_a_contains_b_bit);
         struct value value;
         if (try_load_from_entry(state, src_entry, b, intrin, src, &value)) {
            if (value.is_ssa) {
               /* lookup_load has already ensured that we get a single SSA
                * value that has all of the channels.  We just have to do the
                * rewrite operation.
                */
               if (intrin->instr.block) {
                  /* The lookup left our instruction in-place.  This means it
                   * must have used it to vec up a bunch of different sources.
                   * We need to be careful when rewriting uses so we don't
                   * rewrite the vecN itself.
                   */
                  nir_ssa_def_rewrite_uses_after(&intrin->dest.ssa,
                                                 nir_src_for_ssa(value.ssa[0]),
                                                 value.ssa[0]->parent_instr);
               } else {
                  nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                                           nir_src_for_ssa(value.ssa[0]));
               }
            } else {
               /* We're turning it into a load of a different variable */
               intrin->src[0] = nir_src_for_ssa(&value.deref->dest.ssa);

               /* Put it back in again. */
               nir_builder_instr_insert(b, instr);

               value.is_ssa = true;
               for (unsigned i = 0; i < intrin->num_components; i++)
                  value.ssa[i] = &intrin->dest.ssa;
            }
            state->progress = true;
         } else {
            value.is_ssa = true;
            for (unsigned i = 0; i < intrin->num_components; i++)
               value.ssa[i] = &intrin->dest.ssa;
         }

         /* Now that we have a value, we're going to store it back so that we
          * have the right value next time we come looking for it.  In order
          * to do this, we need an exact match, not just something that
          * contains what we're looking for.
          */
         struct copy_entry *store_entry =
            lookup_entry_for_deref(copies, src, nir_derefs_equal_bit);
         if (!store_entry)
            store_entry = copy_entry_create(copies, src);

         /* Set up a store to this entry with the value of the load.  This way
          * we can potentially remove subsequent loads.  However, we use a
          * NULL instruction so we don't try and delete the load on a
          * subsequent store.
          */
         store_to_entry(state, store_entry, &value,
                        ((1 << intrin->num_components) - 1));
         break;
      }

      case nir_intrinsic_store_deref: {
         nir_deref_instr *dst = nir_src_as_deref(intrin->src[0]);
         struct copy_entry *entry =
            lookup_entry_for_deref(copies, dst, nir_derefs_equal_bit);
         if (entry && value_equals_store_src(&entry->src, intrin)) {
            /* If we are storing the value from a load of the same var the
             * store is redundant so remove it.
             */
            nir_instr_remove(instr);
         } else {
            struct value value = {
               .is_ssa = true
            };

            for (unsigned i = 0; i < intrin->num_components; i++)
               value.ssa[i] = intrin->src[1].ssa;

            unsigned wrmask = nir_intrinsic_write_mask(intrin);
            struct copy_entry *entry =
               get_entry_and_kill_aliases(copies, dst, wrmask);
            store_to_entry(state, entry, &value, wrmask);
         }

         break;
      }

      case nir_intrinsic_copy_deref: {
         nir_deref_instr *dst = nir_src_as_deref(intrin->src[0]);
         nir_deref_instr *src = nir_src_as_deref(intrin->src[1]);

         if (nir_compare_derefs(src, dst) & nir_derefs_equal_bit) {
            /* This is a no-op self-copy.  Get rid of it */
            nir_instr_remove(instr);
            continue;
         }

         struct copy_entry *src_entry =
            lookup_entry_for_deref(copies, src, nir_derefs_a_contains_b_bit);
         struct value value;
         if (try_load_from_entry(state, src_entry, b, intrin, src, &value)) {
            /* If load works, intrin (the copy_deref) is removed. */
            if (value.is_ssa) {
               nir_store_deref(b, dst, value.ssa[0], 0xf);
            } else {
               /* If this would be a no-op self-copy, don't bother. */
               if (nir_compare_derefs(value.deref, dst) & nir_derefs_equal_bit)
                  continue;

               /* Just turn it into a copy of a different deref */
               intrin->src[1] = nir_src_for_ssa(&value.deref->dest.ssa);

               /* Put it back in again. */
               nir_builder_instr_insert(b, instr);
            }

            state->progress = true;
         } else {
            value = (struct value) {
               .is_ssa = false,
               { .deref = src },
            };
         }

         struct copy_entry *dst_entry =
            get_entry_and_kill_aliases(copies, dst, 0xf);
         store_to_entry(state, dst_entry, &value, 0xf);
         break;
      }

      default:
         break;
      }
   }
}

static void
copy_prop_vars_cf_node(struct copy_prop_var_state *state,
                       struct util_dynarray *copies,
                       nir_cf_node *cf_node)
{
   switch (cf_node->type) {
   case nir_cf_node_function: {
      nir_function_impl *impl = nir_cf_node_as_function(cf_node);

      struct util_dynarray impl_copies;
      util_dynarray_init(&impl_copies, state->mem_ctx);

      foreach_list_typed_safe(nir_cf_node, cf_node, node, &impl->body)
         copy_prop_vars_cf_node(state, &impl_copies, cf_node);

      break;
   }

   case nir_cf_node_block: {
      nir_block *block = nir_cf_node_as_block(cf_node);
      nir_builder b;
      nir_builder_init(&b, state->impl);
      copy_prop_vars_block(state, &b, block, copies);
      break;
   }

   case nir_cf_node_if: {
      nir_if *if_stmt = nir_cf_node_as_if(cf_node);

      /* Clone the copies for each branch of the if statement.  The idea is
       * that they both see the same state of available copies, but do not
       * interfere to each other.
       */

      struct util_dynarray then_copies;
      util_dynarray_clone(&then_copies, state->mem_ctx, copies);

      struct util_dynarray else_copies;
      util_dynarray_clone(&else_copies, state->mem_ctx, copies);

      foreach_list_typed_safe(nir_cf_node, cf_node, node, &if_stmt->then_list)
         copy_prop_vars_cf_node(state, &then_copies, cf_node);

      foreach_list_typed_safe(nir_cf_node, cf_node, node, &if_stmt->else_list)
         copy_prop_vars_cf_node(state, &else_copies, cf_node);

      /* Both branches copies can be ignored, since the effect of running both
       * branches was captured in the first pass that collects vars_written.
       */

      invalidate_copies_for_cf_node(state, copies, cf_node);

      break;
   }

   case nir_cf_node_loop: {
      nir_loop *loop = nir_cf_node_as_loop(cf_node);

      /* Invalidate before cloning the copies for the loop, since the loop
       * body can be executed more than once.
       */

      invalidate_copies_for_cf_node(state, copies, cf_node);

      struct util_dynarray loop_copies;
      util_dynarray_clone(&loop_copies, state->mem_ctx, copies);

      foreach_list_typed_safe(nir_cf_node, cf_node, node, &loop->body)
         copy_prop_vars_cf_node(state, &loop_copies, cf_node);

      break;
   }

   default:
      unreachable("Invalid CF node type");
   }
}

static bool
nir_copy_prop_vars_impl(nir_function_impl *impl)
{
   void *mem_ctx = ralloc_context(NULL);

   struct copy_prop_var_state state = {
      .impl = impl,
      .mem_ctx = mem_ctx,
      .lin_ctx = linear_zalloc_parent(mem_ctx, 0),

      .vars_written_map = _mesa_hash_table_create(mem_ctx, _mesa_hash_pointer,
                                                  _mesa_key_pointer_equal),
   };

   gather_vars_written(&state, NULL, &impl->cf_node);

   copy_prop_vars_cf_node(&state, NULL, &impl->cf_node);

   if (state.progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
   } else {
#ifndef NDEBUG
      impl->valid_metadata &= ~nir_metadata_not_properly_reset;
#endif
   }

   ralloc_free(mem_ctx);
   return state.progress;
}

bool
nir_opt_copy_prop_vars(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;
      progress |= nir_copy_prop_vars_impl(function->impl);
   }

   return progress;
}
