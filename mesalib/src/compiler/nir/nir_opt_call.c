/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_phi_builder.h"

struct call_liveness_entry {
   struct list_head list;
   nir_call_instr *instr;
   const BITSET_WORD *live_set;
};

static bool
can_remat_instr(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_alu:
   case nir_instr_type_load_const:
   case nir_instr_type_undef:
      return true;
   case nir_instr_type_intrinsic:
      switch (nir_instr_as_intrinsic(instr)->intrinsic) {
      case nir_intrinsic_load_ray_launch_id:
      case nir_intrinsic_load_ray_launch_size:
      case nir_intrinsic_vulkan_resource_index:
      case nir_intrinsic_vulkan_resource_reindex:
      case nir_intrinsic_load_vulkan_descriptor:
      case nir_intrinsic_load_push_constant:
      case nir_intrinsic_load_global_constant:
      case nir_intrinsic_load_smem_amd:
      case nir_intrinsic_load_scalar_arg_amd:
      case nir_intrinsic_load_vector_arg_amd:
         return true;
      default:
         return false;
      }
   default:
      return false;
   }
}

static void
remat_ssa_def(nir_builder *b, nir_def *def, struct hash_table *remap_table,
              struct hash_table *phi_value_table,
              struct nir_phi_builder *phi_builder, BITSET_WORD *def_blocks)
{
   memset(def_blocks, 0, BITSET_WORDS(b->impl->num_blocks) * sizeof(BITSET_WORD));
   BITSET_SET(def_blocks, def->parent_instr->block->index);
   BITSET_SET(def_blocks, nir_cursor_current_block(b->cursor)->index);
   struct nir_phi_builder_value *val =
      nir_phi_builder_add_value(phi_builder, def->num_components,
                                def->bit_size, def_blocks);
   _mesa_hash_table_insert(phi_value_table, def, val);

   nir_instr *clone = nir_instr_clone_deep(b->shader, def->parent_instr,
                                           remap_table);
   nir_builder_instr_insert(b, clone);
   nir_def *new_def = nir_instr_def(clone);

   _mesa_hash_table_insert(remap_table, def, new_def);
   if (nir_cursor_current_block(b->cursor)->index !=
       def->parent_instr->block->index)
      nir_phi_builder_value_set_block_def(val, def->parent_instr->block, def);
   nir_phi_builder_value_set_block_def(val, nir_cursor_current_block(b->cursor),
                                       new_def);
}

struct remat_chain_check_data {
   struct hash_table *remap_table;
   unsigned chain_length;
};

static bool
can_remat_chain(nir_src *src, void *data)
{
   struct remat_chain_check_data *check_data = data;

   if (_mesa_hash_table_search(check_data->remap_table, src->ssa))
      return true;

   if (!can_remat_instr(src->ssa->parent_instr))
      return false;

   if (check_data->chain_length++ >= 16)
      return false;

   return nir_foreach_src(src->ssa->parent_instr, can_remat_chain, check_data);
}

struct remat_chain_data {
   nir_builder *b;
   struct hash_table *remap_table;
   struct hash_table *phi_value_table;
   struct nir_phi_builder *phi_builder;
   BITSET_WORD *def_blocks;
};

static bool
do_remat_chain(nir_src *src, void *data)
{
   struct remat_chain_data *remat_data = data;

   if (_mesa_hash_table_search(remat_data->remap_table, src->ssa))
      return true;

   nir_foreach_src(src->ssa->parent_instr, do_remat_chain, remat_data);

   remat_ssa_def(remat_data->b, src->ssa, remat_data->remap_table,
                 remat_data->phi_value_table, remat_data->phi_builder,
                 remat_data->def_blocks);
   return true;
}

static bool
rewrite_instr_src_from_phi_builder(nir_src *src, void *data)
{
   struct hash_table *phi_value_table = data;

   if (nir_src_is_const(*src)) {
      nir_builder b = nir_builder_at(nir_before_instr(nir_src_parent_instr(src)));
      nir_src_rewrite(src, nir_build_imm(&b, src->ssa->num_components,
                                         src->ssa->bit_size,
                                         nir_src_as_const_value(*src)));
      return true;
   }

   struct hash_entry *entry = _mesa_hash_table_search(phi_value_table, src->ssa);
   if (!entry)
      return true;

   nir_block *block = nir_src_parent_instr(src)->block;
   nir_def *new_def = nir_phi_builder_value_get_block_def(entry->data, block);

   bool can_rewrite = true;
   if (new_def->parent_instr->block == block && new_def->index != UINT32_MAX)
      can_rewrite =
         !nir_instr_is_before(nir_src_parent_instr(src), new_def->parent_instr);

   if (can_rewrite)
      nir_src_rewrite(src, new_def);
   return true;
}

static bool
nir_minimize_call_live_states_impl(nir_function_impl *impl)
{
   nir_metadata_require(impl, nir_metadata_block_index |
                                 nir_metadata_live_defs |
                                 nir_metadata_dominance);
   bool progress = false;
   void *mem_ctx = ralloc_context(NULL);

   struct list_head call_list;
   list_inithead(&call_list);
   unsigned num_defs = impl->ssa_alloc;

   nir_def **rematerializable =
      rzalloc_array_size(mem_ctx, sizeof(nir_def *), num_defs);

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         nir_def *def = nir_instr_def(instr);
         if (def &&
             can_remat_instr(instr)) {
            rematerializable[def->index] = def;
         }

         if (instr->type != nir_instr_type_call)
            continue;
         nir_call_instr *call = nir_instr_as_call(instr);
         if (!call->indirect_callee.ssa)
            continue;

         struct call_liveness_entry *entry =
            ralloc_size(mem_ctx, sizeof(struct call_liveness_entry));
         entry->instr = call;
         entry->live_set = nir_get_live_defs(nir_after_instr(instr), mem_ctx);
         list_addtail(&entry->list, &call_list);
      }
   }

   const unsigned block_words = BITSET_WORDS(impl->num_blocks);
   BITSET_WORD *def_blocks = ralloc_array(mem_ctx, BITSET_WORD, block_words);

   list_for_each_entry(struct call_liveness_entry, entry, &call_list, list) {
      unsigned i;

      nir_builder b = nir_builder_at(nir_after_instr(&entry->instr->instr));

      struct nir_phi_builder *builder = nir_phi_builder_create(impl);
      struct hash_table *phi_value_table =
         _mesa_pointer_hash_table_create(mem_ctx);
      struct hash_table *remap_table =
         _mesa_pointer_hash_table_create(mem_ctx);

      BITSET_FOREACH_SET(i, entry->live_set, num_defs) {
         if (!rematerializable[i] ||
             _mesa_hash_table_search(remap_table, rematerializable[i]))
            continue;

         assert(!_mesa_hash_table_search(phi_value_table, rematerializable[i]));

         struct remat_chain_check_data check_data = {
            .remap_table = remap_table,
            .chain_length = 1,
         };

         if (!nir_foreach_src(rematerializable[i]->parent_instr,
                              can_remat_chain, &check_data))
            continue;

         struct remat_chain_data remat_data = {
            .b = &b,
            .remap_table = remap_table,
            .phi_value_table = phi_value_table,
            .phi_builder = builder,
            .def_blocks = def_blocks,
         };

         nir_foreach_src(rematerializable[i]->parent_instr, do_remat_chain,
                         &remat_data);

         remat_ssa_def(&b, rematerializable[i], remap_table, phi_value_table,
                       builder, def_blocks);
         progress = true;
      }
      _mesa_hash_table_destroy(remap_table, NULL);

      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type == nir_instr_type_phi)
               continue;

            nir_foreach_src(instr, rewrite_instr_src_from_phi_builder,
                            phi_value_table);
         }
      }

      nir_phi_builder_finish(builder);
      _mesa_hash_table_destroy(phi_value_table, NULL);
   }

   ralloc_free(mem_ctx);

   nir_progress(true, impl, nir_metadata_block_index | nir_metadata_dominance);
   return progress;
}

/* Tries to rematerialize as many live vars as possible after calls.
 * Note: nir_opt_cse will undo any rematerializations done by this pass,
 * so it shouldn't be run afterward.
 */
bool
nir_minimize_call_live_states(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      progress |= nir_minimize_call_live_states_impl(impl);
   }

   return progress;
}
