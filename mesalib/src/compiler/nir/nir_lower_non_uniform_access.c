/*
 * Copyright © 2019 Intel Corporation
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

#include "util/hash_table.h"
#include "util/u_dynarray.h"

struct nu_handle {
   nir_def *handle;
   nir_deref_instr *parent_deref;
   nir_def *first;
};

struct nu_handle_key {
   uint32_t block_index;
   uint32_t access_group;
   uint32_t handle_count;
   /* We can have at most one texture and one sampler handle */
   uint32_t handle_indixes[2];
   uint32_t access_type;
   /* Optional instruction index for emitting separate loops for non-reorderable instructions. */
   uint32_t instr_index;
};

DERIVE_HASH_TABLE(nu_handle_key)

struct nu_handle_data {
   struct nu_handle handles[2];
   struct util_dynarray srcs;
};

struct nu_handle_src {
   nir_src *srcs[2];
};

struct nu_access_group_state {
   uint32_t last_first_use;
   uint32_t index;
};

struct nu_state {
   struct hash_table *accesses;
   struct nu_access_group_state access_groups[nir_lower_non_uniform_access_type_count];
};

static bool
nu_handle_init(struct nu_handle *h, nir_src *src)
{
   nir_deref_instr *deref = nir_src_as_deref(*src);
   if (deref) {
      if (deref->deref_type == nir_deref_type_var)
         return false;

      nir_deref_instr *parent = nir_deref_instr_parent(deref);
      assert(parent->deref_type == nir_deref_type_var);

      assert(deref->deref_type == nir_deref_type_array);
      if (nir_src_is_const(deref->arr.index))
         return false;

      h->handle = deref->arr.index.ssa;
      h->parent_deref = parent;

      return true;
   } else {
      if (nir_src_is_const(*src))
         return false;

      h->handle = src->ssa;
      h->parent_deref = NULL;

      return true;
   }
}

static nir_def *
nu_handle_compare(const nir_lower_non_uniform_access_options *options,
                  nir_builder *b, struct nu_handle *handle, nir_src *src)
{
   nir_component_mask_t channel_mask = ~0;
   if (options->callback)
      channel_mask = options->callback(src, options->callback_data);
   channel_mask &= nir_component_mask(handle->handle->num_components);

   nir_def *channels[NIR_MAX_VEC_COMPONENTS];
   for (unsigned i = 0; i < handle->handle->num_components; i++)
      channels[i] = nir_channel(b, handle->handle, i);

   handle->first = handle->handle;
   nir_def *equal_first = nir_imm_true(b);
   u_foreach_bit(i, channel_mask) {
      nir_def *first = nir_read_first_invocation(b, channels[i]);
      handle->first = nir_vector_insert_imm(b, handle->first, first, i);

      equal_first = nir_iand(b, equal_first, nir_ieq(b, first, channels[i]));
   }

   return equal_first;
}

static void
nu_handle_rewrite(nir_builder *b, struct nu_handle *h, nir_src *src)
{
   if (h->parent_deref) {
      /* Replicate the deref. */
      nir_deref_instr *deref =
         nir_build_deref_array(b, h->parent_deref, h->first);
      nir_src_rewrite(src, &deref->def);
   } else {
      nir_src_rewrite(src, h->first);
   }
}

static bool
get_first_use(nir_def *def, void *state)
{
   uint32_t *last_first_use = state;
   nir_foreach_use(use, def)
      *last_first_use = MIN2(*last_first_use, nir_src_parent_instr(use)->index);

   return true;
}

static void
add_non_uniform_instr(struct nu_state *state, struct nu_handle *handles,
                      nir_src **srcs, uint32_t handle_count, bool group,
                      enum nir_lower_non_uniform_access_type access_type)
{
   nir_instr *instr = nir_src_parent_instr(srcs[0]);

   struct nu_access_group_state *access_group = &state->access_groups[ffs(access_type) - 1];

   if (group) {
      uint32_t first_use = UINT32_MAX;
      nir_foreach_def(instr, get_first_use, &first_use);

      /* Avoid moving accesses below their first use. */
      if (instr->index >= access_group->last_first_use) {
         access_group->last_first_use = first_use;
         access_group->index++;
      } else {
         /* Adjust the access group scope so that every access dominates its first use. */
         access_group->last_first_use = MIN2(access_group->last_first_use, first_use);
      }
   }

   struct nu_handle_key key;
   memset(&key, 0, sizeof(key));
   key.block_index = instr->block->index;
   key.access_group = access_group->index;
   key.access_type = access_type;
   key.handle_count = handle_count;

   if (!group)
      key.instr_index = instr->index;

   for (uint32_t i = 0; i < handle_count; i++)
      key.handle_indixes[i] = handles[i].handle->parent_instr->index;

   struct hash_entry *entry = _mesa_hash_table_search(state->accesses, &key);
   if (!entry) {
      struct nu_handle_data *data = ralloc(state->accesses, struct nu_handle_data);

      for (uint32_t i = 0; i < handle_count; i++)
         data->handles[i] = handles[i];

      util_dynarray_init(&data->srcs, state->accesses);

      struct nu_handle_key *key_copy = ralloc(state->accesses, struct nu_handle_key);
      memcpy(key_copy, &key, sizeof(key));

      entry = _mesa_hash_table_insert(state->accesses, key_copy, data);
   }

   struct nu_handle_data *data = entry->data;

   struct nu_handle_src src = { 0 };
   for (uint32_t i = 0; i < handle_count; i++)
      src.srcs[i] = srcs[i];

   util_dynarray_append(&data->srcs, struct nu_handle_src, src);
}

static bool
lower_non_uniform_tex_access(struct nu_state *state, nir_tex_instr *tex)
{
   if (!tex->texture_non_uniform && !tex->sampler_non_uniform)
      return false;

   /* We can have at most one texture and one sampler handle */
   unsigned num_handles = 0;
   struct nu_handle handles[2];
   nir_src *srcs[2];
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_texture_offset:
      case nir_tex_src_texture_handle:
      case nir_tex_src_texture_deref:
         if (!tex->texture_non_uniform)
            continue;
         break;

      case nir_tex_src_sampler_offset:
      case nir_tex_src_sampler_handle:
      case nir_tex_src_sampler_deref:
         if (!tex->sampler_non_uniform)
            continue;
         break;

      default:
         continue;
      }

      assert(num_handles < ARRAY_SIZE(handles));
      srcs[num_handles] = &tex->src[i].src;
      if (nu_handle_init(&handles[num_handles], &tex->src[i].src))
         num_handles++;
   }

   if (num_handles == 0) {
      /* nu_handle_init() returned false because the handles are uniform. */
      tex->texture_non_uniform = false;
      tex->sampler_non_uniform = false;
      return false;
   }

   tex->texture_non_uniform = false;
   tex->sampler_non_uniform = false;

   add_non_uniform_instr(state, handles, srcs, num_handles, true,
                         nir_lower_non_uniform_texture_access);

   return true;
}

static bool
lower_non_uniform_access_intrin(struct nu_state *state, nir_intrinsic_instr *intrin,
                                unsigned handle_src, enum nir_lower_non_uniform_access_type access_type)
{
   if (!(nir_intrinsic_access(intrin) & ACCESS_NON_UNIFORM))
      return false;

   nir_src *src = &intrin->src[handle_src];

   struct nu_handle handle;
   if (!nu_handle_init(&handle, src)) {
      nir_intrinsic_set_access(intrin, nir_intrinsic_access(intrin) & ~ACCESS_NON_UNIFORM);
      return false;
   }

   nir_intrinsic_set_access(intrin, nir_intrinsic_access(intrin) & ~ACCESS_NON_UNIFORM);

   add_non_uniform_instr(state, &handle, &src, 1, nir_intrinsic_can_reorder(intrin),
                         access_type);

   return true;
}

static void
handle_barrier(struct nu_state *state, bool affects_derivatives)
{
   enum nir_lower_non_uniform_access_type access_type =
      nir_lower_non_uniform_ssbo_access | nir_lower_non_uniform_image_access;

   if (affects_derivatives)
      access_type |= nir_lower_non_uniform_texture_access;

   u_foreach_bit(i, access_type) {
      state->access_groups[i].last_first_use = 0;
   }
}

static bool
nir_lower_non_uniform_access_impl(nir_function_impl *impl,
                                  const nir_lower_non_uniform_access_options *options)
{
   bool progress = false;

   struct nu_state state = {
      .accesses = nu_handle_key_table_create(NULL),
   };

   nir_metadata_require(impl, nir_metadata_instr_index | nir_metadata_block_index);

   nir_foreach_block_safe(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         switch (instr->type) {
         case nir_instr_type_tex: {
            nir_tex_instr *tex = nir_instr_as_tex(instr);
            if ((options->types & nir_lower_non_uniform_texture_access) &&
                lower_non_uniform_tex_access(&state, tex))
               progress = true;
            break;
         }

         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            switch (intrin->intrinsic) {
            case nir_intrinsic_terminate_if:
            case nir_intrinsic_terminate:
            case nir_intrinsic_demote_if:
            case nir_intrinsic_demote:
            case nir_intrinsic_barrier:
               handle_barrier(&state, intrin->intrinsic == nir_intrinsic_terminate_if ||
                                      intrin->intrinsic == nir_intrinsic_terminate);
               break;

            case nir_intrinsic_load_ubo:
               if ((options->types & nir_lower_non_uniform_ubo_access) &&
                   lower_non_uniform_access_intrin(&state, intrin, 0, nir_lower_non_uniform_ubo_access))
                  progress = true;
               break;

            case nir_intrinsic_load_ssbo:
            case nir_intrinsic_ssbo_atomic:
            case nir_intrinsic_ssbo_atomic_swap:
               if ((options->types & nir_lower_non_uniform_ssbo_access) &&
                   lower_non_uniform_access_intrin(&state, intrin, 0, nir_lower_non_uniform_ssbo_access))
                  progress = true;
               break;

            case nir_intrinsic_store_ssbo:
               /* SSBO Stores put the index in the second source */
               if ((options->types & nir_lower_non_uniform_ssbo_access) &&
                   lower_non_uniform_access_intrin(&state, intrin, 1, nir_lower_non_uniform_ssbo_access))
                  progress = true;
               break;

            case nir_intrinsic_get_ssbo_size:
               if ((options->types & nir_lower_non_uniform_get_ssbo_size) &&
                   lower_non_uniform_access_intrin(&state, intrin, 0, nir_lower_non_uniform_get_ssbo_size))
                  progress = true;
               break;

            case nir_intrinsic_image_load:
            case nir_intrinsic_image_sparse_load:
            case nir_intrinsic_image_store:
            case nir_intrinsic_image_atomic:
            case nir_intrinsic_image_atomic_swap:
            case nir_intrinsic_image_levels:
            case nir_intrinsic_image_size:
            case nir_intrinsic_image_samples:
            case nir_intrinsic_image_samples_identical:
            case nir_intrinsic_image_fragment_mask_load_amd:
            case nir_intrinsic_bindless_image_load:
            case nir_intrinsic_bindless_image_sparse_load:
            case nir_intrinsic_bindless_image_store:
            case nir_intrinsic_bindless_image_atomic:
            case nir_intrinsic_bindless_image_atomic_swap:
            case nir_intrinsic_bindless_image_levels:
            case nir_intrinsic_bindless_image_size:
            case nir_intrinsic_bindless_image_samples:
            case nir_intrinsic_bindless_image_samples_identical:
            case nir_intrinsic_bindless_image_fragment_mask_load_amd:
            case nir_intrinsic_image_deref_load:
            case nir_intrinsic_image_deref_sparse_load:
            case nir_intrinsic_image_deref_store:
            case nir_intrinsic_image_deref_atomic:
            case nir_intrinsic_image_deref_atomic_swap:
            case nir_intrinsic_image_deref_levels:
            case nir_intrinsic_image_deref_size:
            case nir_intrinsic_image_deref_samples:
            case nir_intrinsic_image_deref_samples_identical:
            case nir_intrinsic_image_deref_fragment_mask_load_amd:
               if ((options->types & nir_lower_non_uniform_image_access) &&
                   lower_non_uniform_access_intrin(&state, intrin, 0, nir_lower_non_uniform_image_access))
                  progress = true;
               break;

            default:
               /* Nothing to do */
               break;
            }
            break;
         }

         case nir_instr_type_call:
            handle_barrier(&state, true);
            break;

         default:
            /* Nothing to do */
            break;
         }
      }
   }

   nir_builder b = nir_builder_create(impl);

   hash_table_foreach(state.accesses, entry) {
      const struct nu_handle_key *key = entry->key;
      struct nu_handle_data data = *(struct nu_handle_data *)entry->data;

      nir_src *first_src = util_dynarray_top_ptr(&data.srcs, struct nu_handle_src)->srcs[0];
      b.cursor = nir_after_instr(nir_src_parent_instr(first_src));

      nir_push_loop(&b);

      nir_def *all_equal_first = NULL;
      for (uint32_t i = 0; i < key->handle_count; i++) {
         if (i && data.handles[i].handle == data.handles[0].handle) {
            data.handles[i].first = data.handles[0].first;
            continue;
         }

         nir_def *equal_first = nu_handle_compare(options, &b, &data.handles[i], first_src);
         if (i == 0)
            all_equal_first = equal_first;
         else
            all_equal_first = nir_iand(&b, all_equal_first, equal_first);
      }

      nir_push_if(&b, all_equal_first);

      util_dynarray_foreach(&data.srcs, struct nu_handle_src, src) {
         for (uint32_t i = 0; i < key->handle_count; i++)
            nu_handle_rewrite(&b, &data.handles[i], src->srcs[i]);

         nir_instr *instr = nir_src_parent_instr(src->srcs[0]);
         nir_instr_remove(instr);
         nir_builder_instr_insert(&b, instr);
      }

      nir_jump(&b, nir_jump_break);

      nir_pop_if(&b, NULL);
      nir_pop_loop(&b, NULL);
   }

   _mesa_hash_table_destroy(state.accesses, NULL);

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_none);

   return progress;
}

/**
 * Lowers non-uniform resource access by using a loop
 *
 * This pass lowers non-uniform resource access by using subgroup operations
 * and a loop.  Most hardware requires things like textures and UBO access
 * operations to happen on a dynamically uniform (or at least subgroup
 * uniform) resource.  This pass allows for non-uniform access by placing the
 * texture instruction in a loop that looks something like this:
 *
 * loop {
 *    bool tex_eq_first = readFirstInvocationARB(texture) == texture;
 *    bool smp_eq_first = readFirstInvocationARB(sampler) == sampler;
 *    if (tex_eq_first && smp_eq_first) {
 *       res = texture(texture, sampler, ...);
 *       break;
 *    }
 * }
 *
 * Fortunately, because the instruction is immediately followed by the only
 * break in the loop, the block containing the instruction dominates the end
 * of the loop.  Therefore, it's safe to move the instruction into the loop
 * without fixing up SSA in any way.
 */
bool
nir_lower_non_uniform_access(nir_shader *shader,
                             const nir_lower_non_uniform_access_options *options)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      if (nir_lower_non_uniform_access_impl(impl, options))
         progress = true;
   }

   return progress;
}
