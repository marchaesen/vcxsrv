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
#include "util/hash_table.h"

void
nir_deref_path_init(nir_deref_path *path,
                    nir_deref_instr *deref, void *mem_ctx)
{
   assert(deref != NULL);

   /* The length of the short path is at most ARRAY_SIZE - 1 because we need
    * room for the NULL terminator.
    */
   static const int max_short_path_len = ARRAY_SIZE(path->_short_path) - 1;

   int count = 0;

   nir_deref_instr **tail = &path->_short_path[max_short_path_len];
   nir_deref_instr **head = tail;

   *tail = NULL;
   for (nir_deref_instr *d = deref; d; d = nir_deref_instr_parent(d)) {
      count++;
      if (count <= max_short_path_len)
         *(--head) = d;
   }

   if (count <= max_short_path_len) {
      /* If we're under max_short_path_len, just use the short path. */
      path->path = head;
      goto done;
   }

#ifndef NDEBUG
   /* Just in case someone uses short_path by accident */
   for (unsigned i = 0; i < ARRAY_SIZE(path->_short_path); i++)
      path->_short_path[i] = (void *)0xdeadbeef;
#endif

   path->path = ralloc_array(mem_ctx, nir_deref_instr *, count + 1);
   head = tail = path->path + count;
   *tail = NULL;
   for (nir_deref_instr *d = deref; d; d = nir_deref_instr_parent(d))
      *(--head) = d;

done:
   assert(head == path->path);
   assert(tail == head + count);
   assert((*head)->deref_type == nir_deref_type_var);
   assert(*tail == NULL);
}

void
nir_deref_path_finish(nir_deref_path *path)
{
   if (path->path < &path->_short_path[0] ||
       path->path > &path->_short_path[ARRAY_SIZE(path->_short_path) - 1])
      ralloc_free(path->path);
}

/**
 * Recursively removes unused deref instructions
 */
bool
nir_deref_instr_remove_if_unused(nir_deref_instr *instr)
{
   bool progress = false;

   for (nir_deref_instr *d = instr; d; d = nir_deref_instr_parent(d)) {
      /* If anyone is using this deref, leave it alone */
      assert(d->dest.is_ssa);
      if (!list_empty(&d->dest.ssa.uses))
         break;

      nir_instr_remove(&d->instr);
      progress = true;
   }

   return progress;
}

bool
nir_deref_instr_has_indirect(nir_deref_instr *instr)
{
   while (instr->deref_type != nir_deref_type_var) {
      /* Consider casts to be indirects */
      if (instr->deref_type == nir_deref_type_cast)
         return true;

      if (instr->deref_type == nir_deref_type_array &&
          !nir_src_is_const(instr->arr.index))
         return true;

      instr = nir_deref_instr_parent(instr);
   }

   return false;
}

static unsigned
type_get_array_stride(const struct glsl_type *elem_type,
                      glsl_type_size_align_func size_align)
{
   unsigned elem_size, elem_align;
   glsl_get_natural_size_align_bytes(elem_type, &elem_size, &elem_align);
   return ALIGN_POT(elem_size, elem_align);
}

static unsigned
struct_type_get_field_offset(const struct glsl_type *struct_type,
                             glsl_type_size_align_func size_align,
                             unsigned field_idx)
{
   assert(glsl_type_is_struct(struct_type));
   unsigned offset = 0;
   for (unsigned i = 0; i <= field_idx; i++) {
      unsigned elem_size, elem_align;
      glsl_get_natural_size_align_bytes(glsl_get_struct_field(struct_type, i),
                                        &elem_size, &elem_align);
      offset = ALIGN_POT(offset, elem_align);
      if (i < field_idx)
         offset += elem_size;
   }
   return offset;
}

unsigned
nir_deref_instr_get_const_offset(nir_deref_instr *deref,
                                 glsl_type_size_align_func size_align)
{
   nir_deref_path path;
   nir_deref_path_init(&path, deref, NULL);

   assert(path.path[0]->deref_type == nir_deref_type_var);

   unsigned offset = 0;
   for (nir_deref_instr **p = &path.path[1]; *p; p++) {
      if ((*p)->deref_type == nir_deref_type_array) {
         offset += nir_src_as_uint((*p)->arr.index) *
                   type_get_array_stride((*p)->type, size_align);
      } else if ((*p)->deref_type == nir_deref_type_struct) {
         /* p starts at path[1], so this is safe */
         nir_deref_instr *parent = *(p - 1);
         offset += struct_type_get_field_offset(parent->type, size_align,
                                                (*p)->strct.index);
      } else {
         unreachable("Unsupported deref type");
      }
   }

   nir_deref_path_finish(&path);

   return offset;
}

nir_ssa_def *
nir_build_deref_offset(nir_builder *b, nir_deref_instr *deref,
                       glsl_type_size_align_func size_align)
{
   nir_deref_path path;
   nir_deref_path_init(&path, deref, NULL);

   assert(path.path[0]->deref_type == nir_deref_type_var);

   nir_ssa_def *offset = nir_imm_int(b, 0);
   for (nir_deref_instr **p = &path.path[1]; *p; p++) {
      if ((*p)->deref_type == nir_deref_type_array) {
         nir_ssa_def *index = nir_ssa_for_src(b, (*p)->arr.index, 1);
         nir_ssa_def *stride =
            nir_imm_int(b, type_get_array_stride((*p)->type, size_align));
         offset = nir_iadd(b, offset, nir_imul(b, index, stride));
      } else if ((*p)->deref_type == nir_deref_type_struct) {
         /* p starts at path[1], so this is safe */
         nir_deref_instr *parent = *(p - 1);
         unsigned field_offset =
            struct_type_get_field_offset(parent->type, size_align,
                                         (*p)->strct.index);
         nir_iadd(b, offset, nir_imm_int(b, field_offset));
      } else {
         unreachable("Unsupported deref type");
      }
   }

   nir_deref_path_finish(&path);

   return offset;
}

bool
nir_remove_dead_derefs_impl(nir_function_impl *impl)
{
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type == nir_instr_type_deref &&
             nir_deref_instr_remove_if_unused(nir_instr_as_deref(instr)))
            progress = true;
      }
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);

   return progress;
}

bool
nir_remove_dead_derefs(nir_shader *shader)
{
   bool progress = false;
   nir_foreach_function(function, shader) {
      if (function->impl && nir_remove_dead_derefs_impl(function->impl))
         progress = true;
   }

   return progress;
}

void
nir_fixup_deref_modes(nir_shader *shader)
{
   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_deref)
               continue;

            nir_deref_instr *deref = nir_instr_as_deref(instr);

            nir_variable_mode parent_mode;
            if (deref->deref_type == nir_deref_type_var) {
               parent_mode = deref->var->data.mode;
            } else {
               assert(deref->parent.is_ssa);
               nir_deref_instr *parent =
                  nir_instr_as_deref(deref->parent.ssa->parent_instr);
               parent_mode = parent->mode;
            }

            deref->mode = parent_mode;
         }
      }
   }
}

nir_deref_compare_result
nir_compare_deref_paths(nir_deref_path *a_path,
                        nir_deref_path *b_path)
{
   if (a_path->path[0]->var != b_path->path[0]->var)
      return nir_derefs_do_not_alias;

   /* Start off assuming they fully compare.  We ignore equality for now.  In
    * the end, we'll determine that by containment.
    */
   nir_deref_compare_result result = nir_derefs_may_alias_bit |
                                     nir_derefs_a_contains_b_bit |
                                     nir_derefs_b_contains_a_bit;

   nir_deref_instr **a_p = &a_path->path[1];
   nir_deref_instr **b_p = &b_path->path[1];
   while (*a_p != NULL && *b_p != NULL) {
      nir_deref_instr *a_tail = *(a_p++);
      nir_deref_instr *b_tail = *(b_p++);

      if (a_tail == b_tail)
         continue;

      switch (a_tail->deref_type) {
      case nir_deref_type_array:
      case nir_deref_type_array_wildcard: {
         assert(b_tail->deref_type == nir_deref_type_array ||
                b_tail->deref_type == nir_deref_type_array_wildcard);

         if (a_tail->deref_type == nir_deref_type_array_wildcard) {
            if (b_tail->deref_type != nir_deref_type_array_wildcard)
               result &= ~nir_derefs_b_contains_a_bit;
         } else if (b_tail->deref_type == nir_deref_type_array_wildcard) {
            if (a_tail->deref_type != nir_deref_type_array_wildcard)
               result &= ~nir_derefs_a_contains_b_bit;
         } else {
            assert(a_tail->deref_type == nir_deref_type_array &&
                   b_tail->deref_type == nir_deref_type_array);
            assert(a_tail->arr.index.is_ssa && b_tail->arr.index.is_ssa);

            if (nir_src_is_const(a_tail->arr.index) &&
                nir_src_is_const(b_tail->arr.index)) {
               /* If they're both direct and have different offsets, they
                * don't even alias much less anything else.
                */
               if (nir_src_as_uint(a_tail->arr.index) !=
                   nir_src_as_uint(b_tail->arr.index))
                  return nir_derefs_do_not_alias;
            } else if (a_tail->arr.index.ssa == b_tail->arr.index.ssa) {
               /* They're the same indirect, continue on */
            } else {
               /* They're not the same index so we can't prove anything about
                * containment.
                */
               result &= ~(nir_derefs_a_contains_b_bit | nir_derefs_b_contains_a_bit);
            }
         }
         break;
      }

      case nir_deref_type_struct: {
         /* If they're different struct members, they don't even alias */
         if (a_tail->strct.index != b_tail->strct.index)
            return nir_derefs_do_not_alias;
         break;
      }

      default:
         unreachable("Invalid deref type");
      }
   }

   /* If a is longer than b, then it can't contain b */
   if (*a_p != NULL)
      result &= ~nir_derefs_a_contains_b_bit;
   if (*b_p != NULL)
      result &= ~nir_derefs_b_contains_a_bit;

   /* If a contains b and b contains a they must be equal. */
   if ((result & nir_derefs_a_contains_b_bit) && (result & nir_derefs_b_contains_a_bit))
      result |= nir_derefs_equal_bit;

   return result;
}

nir_deref_compare_result
nir_compare_derefs(nir_deref_instr *a, nir_deref_instr *b)
{
   if (a == b) {
      return nir_derefs_equal_bit | nir_derefs_may_alias_bit |
             nir_derefs_a_contains_b_bit | nir_derefs_b_contains_a_bit;
   }

   nir_deref_path a_path, b_path;
   nir_deref_path_init(&a_path, a, NULL);
   nir_deref_path_init(&b_path, b, NULL);
   assert(a_path.path[0]->deref_type == nir_deref_type_var);
   assert(b_path.path[0]->deref_type == nir_deref_type_var);

   nir_deref_compare_result result = nir_compare_deref_paths(&a_path, &b_path);

   nir_deref_path_finish(&a_path);
   nir_deref_path_finish(&b_path);

   return result;
}

struct rematerialize_deref_state {
   bool progress;
   nir_builder builder;
   nir_block *block;
   struct hash_table *cache;
};

static nir_deref_instr *
rematerialize_deref_in_block(nir_deref_instr *deref,
                             struct rematerialize_deref_state *state)
{
   if (deref->instr.block == state->block)
      return deref;

   if (!state->cache) {
      state->cache = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                             _mesa_key_pointer_equal);
   }

   struct hash_entry *cached = _mesa_hash_table_search(state->cache, deref);
   if (cached)
      return cached->data;

   nir_builder *b = &state->builder;
   nir_deref_instr *new_deref =
      nir_deref_instr_create(b->shader, deref->deref_type);
   new_deref->mode = deref->mode;
   new_deref->type = deref->type;

   if (deref->deref_type == nir_deref_type_var) {
      new_deref->var = deref->var;
   } else {
      nir_deref_instr *parent = nir_src_as_deref(deref->parent);
      if (parent) {
         parent = rematerialize_deref_in_block(parent, state);
         new_deref->parent = nir_src_for_ssa(&parent->dest.ssa);
      } else {
         nir_src_copy(&new_deref->parent, &deref->parent, new_deref);
      }
   }

   switch (deref->deref_type) {
   case nir_deref_type_var:
   case nir_deref_type_array_wildcard:
   case nir_deref_type_cast:
      /* Nothing more to do */
      break;

   case nir_deref_type_array:
      assert(!nir_src_as_deref(deref->arr.index));
      nir_src_copy(&new_deref->arr.index, &deref->arr.index, new_deref);
      break;

   case nir_deref_type_struct:
      new_deref->strct.index = deref->strct.index;
      break;

   default:
      unreachable("Invalid deref instruction type");
   }

   nir_ssa_dest_init(&new_deref->instr, &new_deref->dest,
                     deref->dest.ssa.num_components,
                     deref->dest.ssa.bit_size,
                     deref->dest.ssa.name);
   nir_builder_instr_insert(b, &new_deref->instr);

   return new_deref;
}

static bool
rematerialize_deref_src(nir_src *src, void *_state)
{
   struct rematerialize_deref_state *state = _state;

   nir_deref_instr *deref = nir_src_as_deref(*src);
   if (!deref)
      return true;

   nir_deref_instr *block_deref = rematerialize_deref_in_block(deref, state);
   if (block_deref != deref) {
      nir_instr_rewrite_src(src->parent_instr, src,
                            nir_src_for_ssa(&block_deref->dest.ssa));
      nir_deref_instr_remove_if_unused(deref);
      state->progress = true;
   }

   return true;
}

/** Re-materialize derefs in every block
 *
 * This pass re-materializes deref instructions in every block in which it is
 * used.  After this pass has been run, every use of a deref will be of a
 * deref in the same block as the use.  Also, all unused derefs will be
 * deleted as a side-effect.
 */
bool
nir_rematerialize_derefs_in_use_blocks_impl(nir_function_impl *impl)
{
   struct rematerialize_deref_state state = { 0 };
   nir_builder_init(&state.builder, impl);

   nir_foreach_block(block, impl) {
      state.block = block;

      /* Start each block with a fresh cache */
      if (state.cache)
         _mesa_hash_table_clear(state.cache, NULL);

      nir_foreach_instr_safe(instr, block) {
         if (instr->type == nir_instr_type_deref) {
            nir_deref_instr_remove_if_unused(nir_instr_as_deref(instr));
            continue;
         }

         state.builder.cursor = nir_before_instr(instr);
         nir_foreach_src(instr, rematerialize_deref_src, &state);
      }

#ifndef NDEBUG
      nir_if *following_if = nir_block_get_following_if(block);
      if (following_if)
         assert(!nir_src_as_deref(following_if->condition));
#endif
   }

   _mesa_hash_table_destroy(state.cache, NULL);

   return state.progress;
}
