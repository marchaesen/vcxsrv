/*
 * Copyright © 2021 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "ir3_compiler.h"
#include "ir3_nir.h"
#include "nir_instr_set.h"

/* Preamble optimization happens in two parts: first we generate the preamble
 * using the generic NIR pass, then we setup the preamble sequence and inline
 * the preamble into the main shader if there was a preamble. The first part
 * should happen before UBO lowering, because we want to prefer more complex
 * expressions over UBO loads, but the second part has to happen after UBO
 * lowering because it may add copy instructions to the preamble.
 */

static void
def_size(nir_def *def, unsigned *size, unsigned *align)
{
   unsigned bit_size = def->bit_size == 1 ? 32 : def->bit_size;
   /* Due to the implicit const file promotion we want to expand 16-bit values
    * to 32-bit so that the truncation in the main shader can hopefully be
    * folded into the use.
    */
   *size = DIV_ROUND_UP(bit_size, 32) * def->num_components;
   *align = 1;
}

static bool
all_uses_float(nir_def *def, bool allow_src2)
{
   nir_foreach_use_including_if (use, def) {
      if (nir_src_is_if(use))
         return false;

      nir_instr *use_instr = nir_src_parent_instr(use);
      if (use_instr->type != nir_instr_type_alu)
         return false;
      nir_alu_instr *use_alu = nir_instr_as_alu(use_instr);
      unsigned src_index = ~0;
      for  (unsigned i = 0; i < nir_op_infos[use_alu->op].num_inputs; i++) {
         if (&use_alu->src[i].src == use) {
            src_index = i;
            break;
         }
      }

      assert(src_index != ~0);
      nir_alu_type src_type =
         nir_alu_type_get_base_type(nir_op_infos[use_alu->op].input_types[src_index]);

      if (src_type != nir_type_float || (src_index == 2 && !allow_src2))
         return false;
   }

   return true;
}

static bool
all_uses_bit(nir_def *def)
{
   nir_foreach_use_including_if (use, def) {
      if (nir_src_is_if(use))
         return false;

      nir_instr *use_instr = nir_src_parent_instr(use);
      if (use_instr->type != nir_instr_type_alu)
         return false;
      nir_alu_instr *use_alu = nir_instr_as_alu(use_instr);
      
      /* See ir3_cat2_absneg() */
      switch (use_alu->op) {
      case nir_op_iand:
      case nir_op_ior:
      case nir_op_inot:
      case nir_op_ixor:
      case nir_op_bitfield_reverse:
      case nir_op_ufind_msb:
      case nir_op_ifind_msb:
      case nir_op_find_lsb:
      case nir_op_ishl:
      case nir_op_ushr:
      case nir_op_ishr:
      case nir_op_bit_count:
         continue;
      default:
         return false;
      }
   }

   return true;
}

static float
instr_cost(nir_instr *instr, const void *data)
{
   /* We'll assume wave64 here for simplicity and assume normal cat1-cat3 ops
    * take 1 (normalized) cycle.
    *
    * See https://gitlab.freedesktop.org/freedreno/freedreno/-/wikis/A6xx-SP
    *
    * TODO: assume wave128 on fragment/compute shaders?
    */

   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      unsigned components = alu->def.num_components;
      switch (alu->op) {
      /* cat4 */
      case nir_op_frcp:
      case nir_op_fsqrt:
      case nir_op_frsq:
      case nir_op_flog2:
      case nir_op_fexp2:
      case nir_op_fsin:
      case nir_op_fcos:
         return 4 * components;

      /* Instructions that become src modifiers. Note for conversions this is
       * really an approximation.
       *
       * This prevents silly things like lifting a negate that would become a
       * modifier.
       */
      case nir_op_f2f32:
      case nir_op_f2f16:
      case nir_op_f2fmp:
      case nir_op_fneg:
         return all_uses_float(&alu->def, true) ? 0 : 1 * components;

      case nir_op_fabs:
         return all_uses_float(&alu->def, false) ? 0 : 1 * components;

      case nir_op_inot:
         return all_uses_bit(&alu->def) ? 0 : 1 * components;

      /* Instructions that become vector split/collect */
      case nir_op_vec2:
      case nir_op_vec3:
      case nir_op_vec4:
      case nir_op_mov:
         return 0;

      /* cat1-cat3 */
      default:
         return 1 * components;
      }
      break;
   }

   case nir_instr_type_tex:
      /* cat5 */
      return 8;

   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_ubo: {
         /* If the UBO and offset are constant, then UBO lowering should do a
          * better job trying to lower this, and opt_preamble shouldn't try to
          * duplicate it. However if it has a non-constant offset then we can
          * avoid setting up a0.x etc. in the main shader and potentially have
          * to push less.
          */
         bool const_ubo = nir_src_is_const(intrin->src[0]);
         if (!const_ubo) {
            nir_intrinsic_instr *rsrc = ir3_bindless_resource(intrin->src[0]);
            if (rsrc)
               const_ubo = nir_src_is_const(rsrc->src[0]);
         }

         if (const_ubo && nir_src_is_const(intrin->src[1]))
            return 0;

         /* TODO: get actual numbers for ldc */
         return 8;
      }

      case nir_intrinsic_load_ssbo:
      case nir_intrinsic_load_ssbo_ir3:
      case nir_intrinsic_get_ssbo_size:
      case nir_intrinsic_image_load:
      case nir_intrinsic_bindless_image_load:
         /* cat5/isam */
         return 8;

      /* By default assume it's a sysval or something */
      default:
         return 0;
      }
   }

   case nir_instr_type_phi:
      /* Although we can often coalesce phis, the cost of a phi is a proxy for
       * the cost of the if-else statement... If all phis are moved, then the
       * branches move too. So this needs to have a nonzero cost, even if we're
       * optimistic about coalescing.
       *
       * Value chosen empirically. On Rob's shader-db, cost of 2 performs better
       * across the board than a cost of 1. Values greater than 2 do not seem to
       * have any change, so sticking with 2.
       */
      return 2;

   default:
      return 0;
   }
}

static float
rewrite_cost(nir_def *def, const void *data)
{
   /* We always have to expand booleans */
   if (def->bit_size == 1)
      return def->num_components;

   bool mov_needed = false;
   nir_foreach_use (use, def) {
      nir_instr *parent_instr = nir_src_parent_instr(use);
      if (parent_instr->type != nir_instr_type_alu) {
         mov_needed = true;
         break;
      } else {
         nir_alu_instr *alu = nir_instr_as_alu(parent_instr);
         if (alu->op == nir_op_vec2 ||
             alu->op == nir_op_vec3 ||
             alu->op == nir_op_vec4 ||
             alu->op == nir_op_mov) {
            mov_needed = true;
            break;
         } else {
            /* Assume for non-moves that the const is folded into the src */
         }
      }
   }

   return mov_needed ? def->num_components : 0;
}

static bool
avoid_instr(const nir_instr *instr, const void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   
   return intrin->intrinsic == nir_intrinsic_bindless_resource_ir3;
}

static bool
set_speculate(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *_)
{
   switch (intr->intrinsic) {
   /* These instructions go through bounds-checked hardware descriptors so
    * should be safe to speculate.
    *
    * TODO: This isn't necessarily true in Vulkan, where descriptors don't need
    * to be filled out and bindless descriptor offsets aren't bounds checked.
    * We may need to plumb this information through from turnip for correctness
    * to avoid regressing freedreno codegen.
    */
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ubo_vec4:
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_samples_identical:
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_ssbo_ir3:
      nir_intrinsic_set_access(intr, nir_intrinsic_access(intr) |
                                     ACCESS_CAN_SPECULATE);
      return true;

   default:
      return false;
   }
}

bool
ir3_nir_opt_preamble(nir_shader *nir, struct ir3_shader_variant *v)
{
   unsigned max_size;
   if (v->binning_pass) {
      const struct ir3_const_state *const_state = ir3_const_state(v);
      max_size =
         const_state->allocs.consts[IR3_CONST_ALLOC_PREAMBLE].size_vec4 * 4;
   } else {
      const struct ir3_const_state *const_state = ir3_const_state(v);
      max_size = ir3_const_state_get_free_space(
                    v, const_state, v->compiler->const_upload_unit) * 4;
   }

   if (max_size == 0)
      return false;

   bool progress = nir_shader_intrinsics_pass(nir, set_speculate,
                                              nir_metadata_control_flow, NULL);

   nir_opt_preamble_options options = {
      .drawid_uniform = true,
      .subgroup_size_uniform = true,
      .load_workgroup_size_allowed = true,
      .def_size = def_size,
      .preamble_storage_size = max_size,
      .instr_cost_cb = instr_cost,
      .avoid_instr_cb = avoid_instr,
      .rewrite_cost_cb = rewrite_cost,
   };

   unsigned size = 0;
   progress |= nir_opt_preamble(nir, &options, &size);

   if (!v->binning_pass) {
      uint32_t preamble_size_vec4 =
         align(DIV_ROUND_UP(size, 4), v->compiler->const_upload_unit);
      ir3_const_alloc(&ir3_const_state_mut(v)->allocs, IR3_CONST_ALLOC_PREAMBLE,
                      preamble_size_vec4, v->compiler->const_upload_unit);
   }

   return progress;
}

/* This isn't nearly as comprehensive as what's done in nir_opt_preamble, but in
 * various use-cases we need to hoist definitions into preambles outside of
 * opt_preamble. Currently we only handle a few uncomplicated intrinsics.
 */
bool
ir3_def_is_rematerializable_for_preamble(nir_def *def,
                                         nir_def **preamble_defs)
{
   switch (def->parent_instr->type) {
   case nir_instr_type_load_const:
      return true;
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(def->parent_instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_ubo:
         return ir3_def_is_rematerializable_for_preamble(intrin->src[0].ssa,
                                                         preamble_defs) &&
            ir3_def_is_rematerializable_for_preamble(intrin->src[1].ssa,
                                                     preamble_defs) &&
            (def->parent_instr->block->cf_node.parent->type ==
             nir_cf_node_function ||
             (nir_intrinsic_access(intrin) & ACCESS_CAN_SPECULATE));
      case nir_intrinsic_bindless_resource_ir3:
         return ir3_def_is_rematerializable_for_preamble(intrin->src[0].ssa,
                                                         preamble_defs);
      case nir_intrinsic_load_preamble:
         return !!preamble_defs;
      default:
         return false;
      }
   }
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(def->parent_instr);
      for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
         if (!ir3_def_is_rematerializable_for_preamble(alu->src[i].src.ssa,
                                                       preamble_defs))
            return false;
      }
      return true;
   }
   default:
      return false;
   }
}

struct find_insert_block_state {
   nir_block *insert_block;
};

static bool
find_dominated_src(nir_src *src, void *data)
{
   struct find_insert_block_state *state = data;
   nir_block *src_block = src->ssa->parent_instr->block;

   if (!state->insert_block) {
      state->insert_block = src_block;
      return true;
   } else if (nir_block_dominates(state->insert_block, src_block)) {
      state->insert_block = src_block;
      return true;
   } else if (nir_block_dominates(src_block, state->insert_block)) {
      return true;
   } else {
      state->insert_block = NULL;
      return false;
   }
}

/* Find the block where instr can be inserted. This is the block that is
 * dominated by all its sources. If instr doesn't have any sources, return dflt.
 */
static nir_block *
find_insert_block(nir_instr *instr, nir_block *dflt)
{
   struct find_insert_block_state state = {
      .insert_block = NULL,
   };

   if (nir_foreach_src(instr, find_dominated_src, &state)) {
      return state.insert_block ? state.insert_block : dflt;
   }

   return NULL;
}

static bool
dominates(const nir_instr *old_instr, const nir_instr *new_instr)
{
   return nir_block_dominates(old_instr->block, new_instr->block);
}

static nir_def *
_rematerialize_def(nir_builder *b, struct hash_table *remap_ht,
                   struct set *instr_set, nir_def **preamble_defs,
                   nir_def *def)
{
   if (_mesa_hash_table_search(remap_ht, def->parent_instr))
      return NULL;

   switch (def->parent_instr->type) {
   case nir_instr_type_load_const:
      break;
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(def->parent_instr);
      if (intrin->intrinsic == nir_intrinsic_load_preamble) {
         _mesa_hash_table_insert(remap_ht, def,
                                 preamble_defs[nir_intrinsic_base(intrin)]);
         return preamble_defs[nir_intrinsic_base(intrin)];
      } else {
         for (unsigned i = 0; i < nir_intrinsic_infos[intrin->intrinsic].num_srcs;
              i++)
            _rematerialize_def(b, remap_ht, instr_set, preamble_defs,
                               intrin->src[i].ssa);
      }
      break;
   }
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(def->parent_instr);
      for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++)
         _rematerialize_def(b, remap_ht, instr_set, preamble_defs,
                            alu->src[i].src.ssa);
      break;
   }
   default:
      unreachable("should not get here");
   }

   nir_instr *instr = nir_instr_clone_deep(b->shader, def->parent_instr,
                                           remap_ht);

   /* Find a legal place to insert the new instruction. We cannot simply put it
    * at the end of the preamble since the original instruction and its sources
    * may be defined inside control flow.
    */
   nir_metadata_require(b->impl, nir_metadata_dominance);
   nir_block *insert_block =
      find_insert_block(instr, nir_cursor_current_block(b->cursor));

   /* Since the preamble control flow was reconstructed from the original one,
    * we must be able to find a legal place to insert the instruction.
    */
   assert(insert_block);
   b->cursor = nir_after_block(insert_block);
   nir_builder_instr_insert(b, instr);

   if (instr_set) {
      nir_instr *other_instr =
         nir_instr_set_add_or_rewrite(instr_set, instr, dominates);
      if (other_instr) {
         instr = other_instr;
         _mesa_hash_table_insert(remap_ht, def, nir_instr_def(other_instr));
      }
   }

   return nir_instr_def(instr);
}

/* Hoist a given definition into the preamble. If "instr_set" is non-NULL,
 * de-duplicate the hoisted definitions, and if "preamble_defs" is non-NULL then
 * it is used to remap load_preamble instructions back to the original
 * definition in the preamble, if the definition uses load_preamble
 * instructions.
 */

nir_def *
ir3_rematerialize_def_for_preamble(nir_builder *b, nir_def *def,
                                   struct set *instr_set,
                                   nir_def **preamble_defs)
{
   struct hash_table *remap_ht = _mesa_pointer_hash_table_create(NULL);

   nir_def *new_def =
      _rematerialize_def(b, remap_ht, instr_set, preamble_defs, def);

   _mesa_hash_table_destroy(remap_ht, NULL);

   return new_def;
}


static void
get_descriptors(nir_instr *instr, nir_def **descs)
{
   if (instr->type == nir_instr_type_tex) {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      /* TODO: handle non-bindless tex instructions. These are more complicated,
       * because of the implicit addition in the instruction.
       */
      int texture_index =
         nir_tex_instr_src_index(tex, nir_tex_src_texture_handle);
      int sampler_index =
         nir_tex_instr_src_index(tex, nir_tex_src_sampler_handle);
      if (texture_index >= 0)
         descs[0] = tex->src[texture_index].src.ssa;
      if (sampler_index >= 0)
         descs[1] = tex->src[sampler_index].src.ssa;
   } else if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_ssbo:
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_ssbo_atomic:
      case nir_intrinsic_ssbo_atomic_swap:
      case nir_intrinsic_get_ssbo_size:
      case nir_intrinsic_image_load:
      case nir_intrinsic_bindless_image_load:
      case nir_intrinsic_image_store:
      case nir_intrinsic_bindless_image_store:
      case nir_intrinsic_image_atomic:
      case nir_intrinsic_bindless_image_atomic:
      case nir_intrinsic_image_size:
      case nir_intrinsic_bindless_image_size:
         descs[0] = intrin->src[0].ssa;
         break;
      case nir_intrinsic_store_ssbo:
         descs[0] = intrin->src[1].ssa;
         break;
      default:
         break;
      }
   }
}

#define MAX_PREFETCHES 32

struct prefetches {
   nir_def *prefetches[MAX_PREFETCHES];
   unsigned num_prefetches;
};

static bool
is_already_prefetched(struct prefetches *prefetches, nir_def *def)
{
   for (unsigned i = 0; i < prefetches->num_prefetches; i++) {
      if (prefetches->prefetches[i] == def)
         return true;
   }

   return false;
}

static void
add_prefetch(struct prefetches *prefetches, nir_def *def)
{
   assert(prefetches->num_prefetches < MAX_PREFETCHES);
   prefetches->prefetches[prefetches->num_prefetches++] = def;
}

struct prefetch_state {
   struct prefetches tex, sampler;
};

static bool
emit_descriptor_prefetch(nir_builder *b, nir_instr *instr, nir_def **descs,
                         struct prefetch_state *state)
{
   if (instr->type == nir_instr_type_tex) {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      int sampler_index =
         nir_tex_instr_src_index(tex, nir_tex_src_sampler_handle);
      int texture_index =
         nir_tex_instr_src_index(tex, nir_tex_src_texture_handle);

      /* For texture instructions, prefetch if at least one source hasn't been
       * prefetched already. For example, the same sampler may be used with
       * different textures, and we still want to prefetch the texture
       * descriptor if we've already prefetched the sampler descriptor.
       */

      bool tex_already_prefetched = is_already_prefetched(&state->tex, descs[0]);

      if (!tex_already_prefetched &&
          state->tex.num_prefetches == MAX_PREFETCHES)
         return false;

      assert(texture_index >= 0);
      if (sampler_index >= 0) {
         bool sampler_already_prefetched =
            is_already_prefetched(&state->sampler, descs[1]);

         if (!sampler_already_prefetched &&
             state->sampler.num_prefetches == MAX_PREFETCHES)
            return false;

         if (tex_already_prefetched && sampler_already_prefetched)
            return false;

         if (!tex_already_prefetched)
            add_prefetch(&state->tex, descs[0]);
         if (!sampler_already_prefetched)
            add_prefetch(&state->sampler, descs[1]);

         nir_prefetch_sam_ir3(b, descs[0], descs[1]);
      } else {
         if (tex_already_prefetched)
            return false;

         add_prefetch(&state->tex, descs[0]);
         nir_prefetch_tex_ir3(b, descs[0]);
      }
   } else {
      assert(instr->type == nir_instr_type_intrinsic);

      if (state->tex.num_prefetches == MAX_PREFETCHES)
         return false;

      if (is_already_prefetched(&state->tex, descs[0]))
         return false;

      add_prefetch(&state->tex, descs[0]);

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      if (intrin->intrinsic == nir_intrinsic_load_ubo)
         nir_prefetch_ubo_ir3(b, descs[0]);
      else
         nir_prefetch_tex_ir3(b, descs[0]);
   }

   return true;
}

static unsigned
get_preamble_offset(nir_def *def)
{
   return nir_intrinsic_base(nir_instr_as_intrinsic(def->parent_instr));
}

/* Prefetch descriptors in the preamble. This is an optimization introduced on
 * a7xx, mainly useful when the preamble is an early preamble, and replaces the
 * use of CP_LOAD_STATE on a6xx to prefetch descriptors in HLSQ.
 */

bool
ir3_nir_opt_prefetch_descriptors(nir_shader *nir, struct ir3_shader_variant *v)
{
   const struct ir3_const_state *const_state = ir3_const_state(v);

   nir_function_impl *main = nir_shader_get_entrypoint(nir);
   struct set *instr_set = nir_instr_set_create(NULL);
   nir_function_impl *preamble = main->preamble ? main->preamble->impl : NULL;
   nir_builder b;
   bool progress = false;
   struct prefetch_state state = {};

   nir_def **preamble_defs =
      calloc(const_state->allocs.consts[IR3_CONST_ALLOC_PREAMBLE].size_vec4 * 4,
             sizeof(nir_def *));

   /* Collect preamble defs. This is useful if the computation of the offset has
    * already been hoisted to the preamble.
    */
   if (preamble) {
      nir_foreach_block (block, preamble) {
         nir_foreach_instr (instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

            if (intrin->intrinsic != nir_intrinsic_store_preamble)
               continue;

            assert(
               nir_intrinsic_base(intrin) <
               const_state->allocs.consts[IR3_CONST_ALLOC_PREAMBLE].size_vec4 * 4);
            preamble_defs[nir_intrinsic_base(intrin)] = intrin->src[0].ssa;
         }
      }
   }

   nir_foreach_block (block, main) {
      nir_foreach_instr (instr, block) {
         nir_def *descs[2] = { NULL, NULL };
         nir_def *preamble_descs[2] = { NULL, NULL };
         get_descriptors(instr, descs);

         /* We must have found at least one descriptor */
         if (!descs[0] && !descs[1])
            continue;

         /* The instruction itself must be hoistable.
          * TODO: If the descriptor is statically referenced and in-bounds, then
          * we should be able to hoist the descriptor load even if the
          * descriptor contents aren't guaranteed. This would require more
          * plumbing.
          * TODO: Textures. This is broken in nir_opt_preamble at the moment and
          * handling them would also require more plumbing.
          */
         if (instr->type == nir_instr_type_intrinsic &&
             nir_intrinsic_has_access(nir_instr_as_intrinsic(instr)) &&
             !(nir_intrinsic_access(nir_instr_as_intrinsic(instr)) &
               ACCESS_CAN_SPECULATE) &&
             block->cf_node.parent->type != nir_cf_node_function)
            continue;

         /* Each descriptor must be rematerializable */
         if (descs[0] &&
             !ir3_def_is_rematerializable_for_preamble(descs[0], preamble_defs))
            continue;
         if (descs[1] &&
             !ir3_def_is_rematerializable_for_preamble(descs[1], preamble_defs))
            continue;

         /* If the preamble hasn't been created then this descriptor isn't a
          * duplicate and we will definitely insert an instruction, so create
          * the preamble if it hasn't already been created.
          */
         if (!preamble) {
            preamble = nir_shader_get_preamble(nir);
         }

         b = nir_builder_at(nir_after_impl(preamble));

         /* Materialize descriptors for the prefetch. Note that we deduplicate
          * descriptors so that we don't blow our budget when repeatedly loading
          * from the same descriptor, even if the calculation of the descriptor
          * offset hasn't been CSE'd because the accesses are in different
          * blocks. This is common because we emit the bindless_resource_ir3
          * intrinsic right before the access.
          */
         for (unsigned i = 0; i < 2; i++) {
            if (!descs[i])
               continue;

            preamble_descs[i] =
               ir3_rematerialize_def_for_preamble(&b, descs[i], instr_set,
                                                  preamble_defs);
         }

         /* ir3_rematerialize_def_for_preamble may have moved the cursor. */
         b.cursor = nir_after_impl(preamble);
         progress |= emit_descriptor_prefetch(&b, instr, preamble_descs, &state);

         if (state.sampler.num_prefetches == MAX_PREFETCHES &&
             state.tex.num_prefetches == MAX_PREFETCHES)
            goto finished;
      }
   }

finished:
   nir_metadata_preserve(main, nir_metadata_all);
   if (preamble) {
      nir_metadata_preserve(preamble,
                            nir_metadata_block_index |
                            nir_metadata_dominance);
   }
   nir_instr_set_destroy(instr_set);
   free(preamble_defs);
   return progress;
}

bool
ir3_nir_lower_preamble(nir_shader *nir, struct ir3_shader_variant *v)
{
   nir_function_impl *main = nir_shader_get_entrypoint(nir);
   
   if (!main->preamble)
      return false;

   nir_function_impl *preamble = main->preamble->impl;

   /* First, lower load/store_preamble. */  
   const struct ir3_const_state *const_state = ir3_const_state(v);
   unsigned preamble_base =
      const_state->allocs.consts[IR3_CONST_ALLOC_PREAMBLE].offset_vec4 * 4;
   unsigned preamble_size =
      const_state->allocs.consts[IR3_CONST_ALLOC_PREAMBLE].size_vec4 * 4;

   BITSET_DECLARE(promoted_to_float, preamble_size);
   memset(promoted_to_float, 0, sizeof(promoted_to_float));

   nir_builder builder_main = nir_builder_create(main);
   nir_builder *b = &builder_main;

   nir_foreach_block (block, main) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_load_preamble)
            continue;

         nir_def *dest = &intrin->def;

         unsigned offset = preamble_base + nir_intrinsic_base(intrin);
         b->cursor = nir_before_instr(instr);

         nir_def *new_dest = nir_load_const_ir3(
            b, dest->num_components, 32, nir_imm_int(b, 0), .base = offset);

         if (dest->bit_size == 1) {
            new_dest = nir_i2b(b, new_dest);
         } else if (dest->bit_size != 32) {
            if (all_uses_float(dest, true)) {
               assert(dest->bit_size == 16);
               new_dest = nir_f2f16(b, new_dest);
               BITSET_SET(promoted_to_float, nir_intrinsic_base(intrin));
            } else {
               new_dest = nir_u2uN(b, new_dest, dest->bit_size);
            }
         }

         nir_def_rewrite_uses(dest, new_dest);
         nir_instr_remove(instr);
         nir_instr_free(instr);
      }
   }

   nir_builder builder_preamble = nir_builder_create(preamble);
   b = &builder_preamble;

   nir_foreach_block (block, preamble) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_store_preamble)
            continue;

         nir_def *src = intrin->src[0].ssa;
         unsigned offset = preamble_base + nir_intrinsic_base(intrin);

         b->cursor = nir_before_instr(instr);

         if (src->bit_size == 1)
            src = nir_b2i32(b, src);
         if (src->bit_size != 32) {
            if (BITSET_TEST(promoted_to_float, nir_intrinsic_base(intrin))){
               assert(src->bit_size == 16);
               src = nir_f2f32(b, src);
            } else {
               src = nir_u2u32(b, src);
            }
         }

         nir_store_const_ir3(b, src, .base = offset);
         nir_instr_remove(instr);
         nir_instr_free(instr);
      }
   }

   /* Now, create the preamble sequence and move the preamble into the main
    * shader:
    *
    * if (preamble_start_ir3()) {
    *    if (subgroupElect()) {
    *       preamble();
    *       preamble_end_ir3();
    *    }
    * }
    * ...
    */

   /* @decl_regs need to stay in the first block. */
   b->cursor = nir_after_reg_decls(main);

   nir_if *outer_if = nir_push_if(b, nir_preamble_start_ir3(b, 1));
   {
      nir_if *inner_if = nir_push_if(b, nir_elect_any_ir3(b, 1));
      {
         nir_call_instr *call = nir_call_instr_create(nir, main->preamble);
         nir_builder_instr_insert(b, &call->instr);
         nir_preamble_end_ir3(b);
      }
      nir_pop_if(b, inner_if);
   }
   nir_pop_if(b, outer_if);

   nir_inline_functions(nir);
   exec_node_remove(&main->preamble->node);
   main->preamble = NULL;

   nir_metadata_preserve(main, nir_metadata_none);
   return true;
}
