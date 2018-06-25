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
 *  2) Dead code elimination of store_var and copy_var intrinsics based on
 *     killed destination values.
 *
 *  3) Removal of redundant load_deref intrinsics.  We can't trust regular CSE
 *     to do this because it isn't aware of variable writes that may alias the
 *     value and make the former load invalid.
 *
 * Unfortunately, properly handling all of those cases makes this path rather
 * complex.  In order to avoid additional complexity, this pass is entirely
 * block-local.  If we tried to make it global, the data-flow analysis would
 * rapidly get out of hand.  Fortunately, for anything that is only ever
 * accessed directly, we get SSA based copy-propagation which is extremely
 * powerful so this isn't that great a loss.
 */

struct value {
   bool is_ssa;
   union {
      nir_ssa_def *ssa[4];
      nir_deref_instr *deref;
   };
};

struct copy_entry {
   struct list_head link;

   nir_instr *store_instr[4];

   unsigned comps_may_be_read;
   struct value src;

   nir_deref_instr *dst;
};

struct copy_prop_var_state {
   nir_shader *shader;

   void *mem_ctx;

   struct list_head copies;

   /* We're going to be allocating and deleting a lot of copy entries so we'll
    * keep a free list to avoid thrashing malloc too badly.
    */
   struct list_head copy_free_list;

   bool progress;
};

static struct copy_entry *
copy_entry_create(struct copy_prop_var_state *state,
                  nir_deref_instr *dst_deref)
{
   struct copy_entry *entry;
   if (!list_empty(&state->copy_free_list)) {
      struct list_head *item = state->copy_free_list.next;
      list_del(item);
      entry = LIST_ENTRY(struct copy_entry, item, link);
      memset(entry, 0, sizeof(*entry));
   } else {
      entry = rzalloc(state->mem_ctx, struct copy_entry);
   }

   entry->dst = dst_deref;
   list_add(&entry->link, &state->copies);

   return entry;
}

static void
copy_entry_remove(struct copy_prop_var_state *state, struct copy_entry *entry)
{
   list_del(&entry->link);
   list_add(&entry->link, &state->copy_free_list);
}

enum deref_compare_result {
   derefs_equal_bit = (1 << 0),
   derefs_may_alias_bit = (1 << 1),
   derefs_a_contains_b_bit = (1 << 2),
   derefs_b_contains_a_bit = (1 << 3),
};

/** Returns true if the storage referrenced to by deref completely contains
 * the storage referenced by sub.
 *
 * NOTE: This is fairly general and could be moved to core NIR if someone else
 * ever needs it.
 */
static enum deref_compare_result
compare_deref_paths(nir_deref_path *a_path,
                    nir_deref_path *b_path)
{
   if (a_path->path[0]->var != b_path->path[0]->var)
      return 0;

   /* Start off assuming they fully compare.  We ignore equality for now.  In
    * the end, we'll determine that by containment.
    */
   enum deref_compare_result result = derefs_may_alias_bit |
                                      derefs_a_contains_b_bit |
                                      derefs_b_contains_a_bit;

   nir_deref_instr **a_p = &a_path->path[1];
   nir_deref_instr **b_p = &b_path->path[1];
   while (*a_p != NULL && *b_p != NULL) {
      nir_deref_instr *a_tail = *(a_p++);
      nir_deref_instr *b_tail = *(b_p++);

      switch (a_tail->deref_type) {
      case nir_deref_type_array:
      case nir_deref_type_array_wildcard: {
         assert(b_tail->deref_type == nir_deref_type_array ||
                b_tail->deref_type == nir_deref_type_array_wildcard);

         if (a_tail->deref_type == nir_deref_type_array_wildcard) {
            if (b_tail->deref_type != nir_deref_type_array_wildcard)
               result &= ~derefs_b_contains_a_bit;
         } else if (b_tail->deref_type == nir_deref_type_array_wildcard) {
            if (a_tail->deref_type != nir_deref_type_array_wildcard)
               result &= ~derefs_a_contains_b_bit;
         } else {
            assert(a_tail->deref_type == nir_deref_type_array &&
                   b_tail->deref_type == nir_deref_type_array);
            assert(a_tail->arr.index.is_ssa && b_tail->arr.index.is_ssa);

            nir_const_value *a_index_const =
               nir_src_as_const_value(a_tail->arr.index);
            nir_const_value *b_index_const =
               nir_src_as_const_value(b_tail->arr.index);
            if (a_index_const && b_index_const) {
               /* If they're both direct and have different offsets, they
                * don't even alias much less anything else.
                */
               if (a_index_const->u32[0] != b_index_const->u32[0])
                  return 0;
            } else if (a_tail->arr.index.ssa == b_tail->arr.index.ssa) {
               /* They're the same indirect, continue on */
            } else {
               /* They're not the same index so we can't prove anything about
                * containment.
                */
               result &= ~(derefs_a_contains_b_bit | derefs_b_contains_a_bit);
            }
         }
         break;
      }

      case nir_deref_type_struct: {
         /* If they're different struct members, they don't even alias */
         if (a_tail->strct.index != b_tail->strct.index)
            return 0;
         break;
      }

      default:
         unreachable("Invalid deref type");
      }
   }

   /* If a is longer than b, then it can't contain b */
   if (*a_p != NULL)
      result &= ~derefs_a_contains_b_bit;
   if (*b_p != NULL)
      result &= ~derefs_b_contains_a_bit;

   /* If a contains b and b contains a they must be equal. */
   if ((result & derefs_a_contains_b_bit) && (result & derefs_b_contains_a_bit))
      result |= derefs_equal_bit;

   return result;
}

static enum deref_compare_result
compare_derefs(nir_deref_instr *a, nir_deref_instr *b)
{
   if (a == b) {
      return derefs_equal_bit | derefs_may_alias_bit |
             derefs_a_contains_b_bit | derefs_b_contains_a_bit;
   }

   nir_deref_path a_path, b_path;
   nir_deref_path_init(&a_path, a, NULL);
   nir_deref_path_init(&b_path, b, NULL);
   assert(a_path.path[0]->deref_type == nir_deref_type_var);
   assert(b_path.path[0]->deref_type == nir_deref_type_var);

   enum deref_compare_result result = compare_deref_paths(&a_path, &b_path);

   nir_deref_path_finish(&a_path);
   nir_deref_path_finish(&b_path);

   return result;
}

static void
remove_dead_writes(struct copy_prop_var_state *state,
                   struct copy_entry *entry, unsigned write_mask)
{
   /* We're overwriting another entry.  Some of it's components may not
    * have been read yet and, if that's the case, we may be able to delete
    * some instructions but we have to be careful.
    */
   unsigned dead_comps = write_mask & ~entry->comps_may_be_read;

   for (unsigned mask = dead_comps; mask;) {
      unsigned i = u_bit_scan(&mask);

      nir_instr *instr = entry->store_instr[i];

      /* We may have already deleted it on a previous iteration */
      if (!instr)
         continue;

      /* See if this instr is used anywhere that it's not dead */
      bool keep = false;
      for (unsigned j = 0; j < 4; j++) {
         if (entry->store_instr[j] == instr) {
            if (dead_comps & (1 << j)) {
               entry->store_instr[j] = NULL;
            } else {
               keep = true;
            }
         }
      }

      if (!keep) {
         nir_instr_remove(instr);
         state->progress = true;
      }
   }
}

static struct copy_entry *
lookup_entry_for_deref(struct copy_prop_var_state *state,
                       nir_deref_instr *deref,
                       enum deref_compare_result allowed_comparisons)
{
   list_for_each_entry(struct copy_entry, iter, &state->copies, link) {
      if (compare_derefs(iter->dst, deref) & allowed_comparisons)
         return iter;
   }

   return NULL;
}

static void
mark_aliased_entries_as_read(struct copy_prop_var_state *state,
                             nir_deref_instr *deref, unsigned components)
{
   list_for_each_entry(struct copy_entry, iter, &state->copies, link) {
      if (compare_derefs(iter->dst, deref) & derefs_may_alias_bit)
         iter->comps_may_be_read |= components;
   }
}

static struct copy_entry *
get_entry_and_kill_aliases(struct copy_prop_var_state *state,
                           nir_deref_instr *deref,
                           unsigned write_mask)
{
   struct copy_entry *entry = NULL;
   list_for_each_entry_safe(struct copy_entry, iter, &state->copies, link) {
      if (!iter->src.is_ssa) {
         /* If this write aliases the source of some entry, get rid of it */
         if (compare_derefs(iter->src.deref, deref) & derefs_may_alias_bit) {
            copy_entry_remove(state, iter);
            continue;
         }
      }

      enum deref_compare_result comp = compare_derefs(iter->dst, deref);
      /* This is a store operation.  If we completely overwrite some value, we
       * want to delete any dead writes that may be present.
       */
      if (comp & derefs_b_contains_a_bit)
         remove_dead_writes(state, iter, write_mask);

      if (comp & derefs_equal_bit) {
         assert(entry == NULL);
         entry = iter;
      } else if (comp & derefs_may_alias_bit) {
         copy_entry_remove(state, iter);
      }
   }

   if (entry == NULL)
      entry = copy_entry_create(state, deref);

   return entry;
}

static void
apply_barrier_for_modes(struct copy_prop_var_state *state,
                        nir_variable_mode modes)
{
   list_for_each_entry_safe(struct copy_entry, iter, &state->copies, link) {
      nir_variable *dst_var = nir_deref_instr_get_variable(iter->dst);
      nir_variable *src_var = iter->src.is_ssa ? NULL :
         nir_deref_instr_get_variable(iter->src.deref);

      if ((dst_var->data.mode & modes) ||
          (src_var && (src_var->data.mode & modes)))
         copy_entry_remove(state, iter);
   }
}

static void
store_to_entry(struct copy_prop_var_state *state, struct copy_entry *entry,
               const struct value *value, unsigned write_mask,
               nir_instr *store_instr)
{
   entry->comps_may_be_read &= ~write_mask;
   if (value->is_ssa) {
      entry->src.is_ssa = true;
      /* Only overwrite the written components */
      for (unsigned i = 0; i < 4; i++) {
         if (write_mask & (1 << i)) {
            entry->store_instr[i] = store_instr;
            entry->src.ssa[i] = value->ssa[i];
         }
      }
   } else {
      /* Non-ssa stores always write everything */
      entry->src.is_ssa = false;
      entry->src.deref = value->deref;
      for (unsigned i = 0; i < 4; i++)
         entry->store_instr[i] = store_instr;
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

   uint8_t available = 0;
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
   nir_ssa_def *comps[4];
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
copy_prop_vars_block(struct copy_prop_var_state *state,
                     nir_builder *b, nir_block *block)
{
   /* Start each block with a blank slate */
   list_for_each_entry_safe(struct copy_entry, iter, &state->copies, link)
      copy_entry_remove(state, iter);

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_barrier:
      case nir_intrinsic_memory_barrier:
         /* If we hit a barrier, we need to trash everything that may possibly
          * be accessible to another thread.  Locals, globals, and things of
          * the like are safe, however.
          */
         apply_barrier_for_modes(state, ~(nir_var_local | nir_var_global |
                                          nir_var_shader_in | nir_var_uniform));
         break;

      case nir_intrinsic_emit_vertex:
      case nir_intrinsic_emit_vertex_with_counter:
         apply_barrier_for_modes(state, nir_var_shader_out);
         break;

      case nir_intrinsic_load_deref: {
         nir_deref_instr *src = nir_src_as_deref(intrin->src[0]);

         uint8_t comps_read = nir_ssa_def_components_read(&intrin->dest.ssa);
         mark_aliased_entries_as_read(state, src, comps_read);

         struct copy_entry *src_entry =
            lookup_entry_for_deref(state, src, derefs_a_contains_b_bit);
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
            lookup_entry_for_deref(state, src, derefs_equal_bit);
         if (!store_entry)
            store_entry = copy_entry_create(state, src);

         /* Set up a store to this entry with the value of the load.  This way
          * we can potentially remove subsequent loads.  However, we use a
          * NULL instruction so we don't try and delete the load on a
          * subsequent store.
          */
         store_to_entry(state, store_entry, &value,
                        ((1 << intrin->num_components) - 1), NULL);
         break;
      }

      case nir_intrinsic_store_deref: {
         struct value value = {
            .is_ssa = true
         };

         for (unsigned i = 0; i < intrin->num_components; i++)
            value.ssa[i] = intrin->src[1].ssa;

         nir_deref_instr *dst = nir_src_as_deref(intrin->src[0]);
         unsigned wrmask = nir_intrinsic_write_mask(intrin);
         struct copy_entry *entry =
            get_entry_and_kill_aliases(state, dst, wrmask);
         store_to_entry(state, entry, &value, wrmask, &intrin->instr);
         break;
      }

      case nir_intrinsic_copy_deref: {
         nir_deref_instr *dst = nir_src_as_deref(intrin->src[0]);
         nir_deref_instr *src = nir_src_as_deref(intrin->src[1]);

         if (compare_derefs(src, dst) & derefs_equal_bit) {
            /* This is a no-op self-copy.  Get rid of it */
            nir_instr_remove(instr);
            continue;
         }

         mark_aliased_entries_as_read(state, src, 0xf);

         struct copy_entry *src_entry =
            lookup_entry_for_deref(state, src, derefs_a_contains_b_bit);
         struct value value;
         if (try_load_from_entry(state, src_entry, b, intrin, src, &value)) {
            if (value.is_ssa) {
               nir_store_deref(b, dst, value.ssa[0], 0xf);
               intrin = nir_instr_as_intrinsic(nir_builder_last_instr(b));
            } else {
               /* If this would be a no-op self-copy, don't bother. */
               if (compare_derefs(value.deref, dst) & derefs_equal_bit)
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
            get_entry_and_kill_aliases(state, dst, 0xf);
         store_to_entry(state, dst_entry, &value, 0xf, &intrin->instr);
         break;
      }

      default:
         break;
      }
   }
}

bool
nir_opt_copy_prop_vars(nir_shader *shader)
{
   struct copy_prop_var_state state;

   state.shader = shader;
   state.mem_ctx = ralloc_context(NULL);
   list_inithead(&state.copies);
   list_inithead(&state.copy_free_list);

   bool global_progress = false;
   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_builder b;
      nir_builder_init(&b, function->impl);

      state.progress = false;
      nir_foreach_block(block, function->impl)
         copy_prop_vars_block(&state, &b, block);

      if (state.progress) {
         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
         global_progress = true;
      }
   }

   ralloc_free(state.mem_ctx);

   return global_progress;
}
