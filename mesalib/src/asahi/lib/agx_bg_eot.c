/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "agx_bg_eot.h"
#include "util/simple_mtx.h"
#include "util/u_debug.h"
#include "agx_compile.h"
#include "agx_device.h"
#include "agx_nir.h"
#include "agx_nir_texture.h"
#include "agx_tilebuffer.h"
#include "agx_usc.h"
#include "libagx_shaders.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_intrinsics.h"
#include "pool.h"

static bool
lower_tex_handle_to_u0(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_texture_handle_agx)
      return false;

   b->cursor = nir_instr_remove(&intr->instr);
   nir_def_rewrite_uses(
      &intr->def,
      nir_vec2(b, nir_imm_int(b, 0), nir_imul_imm(b, intr->src[0].ssa, 24)));

   return true;
}

static struct agx_bg_eot_shader *
agx_compile_bg_eot_shader(struct agx_bg_eot_cache *cache, nir_shader *shader,
                          struct agx_shader_key *key,
                          struct agx_tilebuffer_layout *tib)
{
   agx_nir_lower_texture(shader);
   agx_preprocess_nir(shader);
   if (tib) {
      unsigned bindless_base = 0;
      agx_nir_lower_tilebuffer(shader, tib, NULL, &bindless_base, NULL, NULL);
      agx_nir_lower_monolithic_msaa(shader, tib->nr_samples);
      agx_nir_lower_multisampled_image_store(shader);
      agx_nir_lower_texture(shader);

      nir_shader_intrinsics_pass(shader, lower_tex_handle_to_u0,
                                 nir_metadata_control_flow, NULL);
   }

   struct agx_bg_eot_shader *res = rzalloc(cache->ht, struct agx_bg_eot_shader);
   struct agx_shader_part bin;
   agx_compile_shader_nir(shader, key, NULL, &bin);

   res->info = bin.info;
   res->ptr = agx_pool_upload_aligned_with_bo(
      &cache->pool, bin.binary, bin.info.binary_size, 128, &res->bo);
   free(bin.binary);
   ralloc_free(shader);

   return res;
}

static nir_def *
build_background_op(nir_builder *b, enum agx_bg_eot_op op, unsigned rt,
                    unsigned nr, bool msaa, bool layered)
{
   if (op == AGX_BG_LOAD) {
      nir_def *coord = nir_u2u32(b, nir_load_pixel_coord(b));

      if (layered) {
         coord = nir_vec3(b, nir_channel(b, coord, 0), nir_channel(b, coord, 1),
                          nir_load_layer_id(b));
      }

      nir_tex_instr *tex = nir_tex_instr_create(b->shader, 2);
      /* The type doesn't matter as long as it matches the store */
      tex->dest_type = nir_type_uint32;
      tex->sampler_dim = msaa ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D;
      tex->is_array = layered;
      tex->op = msaa ? nir_texop_txf_ms : nir_texop_txf;
      tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, coord);

      /* Layer is necessarily already in-bounds so we do not want the compiler
       * to clamp it, which would require reading the descriptor
       */
      tex->backend_flags = AGX_TEXTURE_FLAG_NO_CLAMP;

      if (msaa) {
         tex->src[1] =
            nir_tex_src_for_ssa(nir_tex_src_ms_index, nir_load_sample_id(b));
         b->shader->info.fs.uses_sample_shading = true;
      } else {
         tex->src[1] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_int(b, 0));
      }

      tex->coord_components = layered ? 3 : 2;
      tex->texture_index = rt * 2;
      nir_def_init(&tex->instr, &tex->def, 4, 32);
      nir_builder_instr_insert(b, &tex->instr);

      return nir_trim_vector(b, &tex->def, nr);
   } else {
      assert(op == AGX_BG_CLEAR);

      return nir_load_preamble(b, nr, 32, 4 + (rt * 8));
   }
}

static struct agx_bg_eot_shader *
agx_build_background_shader(struct agx_bg_eot_cache *cache,
                            struct agx_bg_eot_key *key)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "agx_background");
   b.shader->info.fs.untyped_color_outputs = true;

   struct agx_shader_key compiler_key = {
      .fs.ignore_tib_dependencies = true,
      .reserved_preamble = key->reserved_preamble,
   };

   for (unsigned rt = 0; rt < ARRAY_SIZE(key->op); ++rt) {
      if (key->op[rt] == AGX_BG_EOT_NONE)
         continue;

      unsigned nr = util_format_get_nr_components(key->tib.logical_format[rt]);
      bool msaa = key->tib.nr_samples > 1;
      bool layered = key->tib.layered;
      assert(nr > 0);

      nir_store_output(
         &b, build_background_op(&b, key->op[rt], rt, nr, msaa, layered),
         nir_imm_int(&b, 0), .write_mask = BITFIELD_MASK(nr),
         .src_type = nir_type_uint32,
         .io_semantics.location = FRAG_RESULT_DATA0 + rt,
         .io_semantics.num_slots = 1);

      b.shader->info.outputs_written |= BITFIELD64_BIT(FRAG_RESULT_DATA0 + rt);
   }

   return agx_compile_bg_eot_shader(cache, b.shader, &compiler_key, &key->tib);
}

static struct agx_bg_eot_shader *
agx_build_end_of_tile_shader(struct agx_bg_eot_cache *cache,
                             struct agx_bg_eot_key *key)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
                                                  &agx_nir_options, "agx_eot");

   enum glsl_sampler_dim dim =
      (key->tib.nr_samples > 1) ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D;

   for (unsigned rt = 0; rt < ARRAY_SIZE(key->op); ++rt) {
      if (key->op[rt] == AGX_BG_EOT_NONE)
         continue;

      /* The end-of-tile shader is unsuitable to handle spilled render targets.
       * Skip them. If blits are needed with spilled render targets, other parts
       * of the driver need to implement them.
       */
      if (key->tib.spilled[rt])
         continue;

      assert(key->op[rt] == AGX_EOT_STORE);
      unsigned offset_B = agx_tilebuffer_offset_B(&key->tib, rt);

      nir_def *layer = nir_undef(&b, 1, 16);
      if (key->tib.layered)
         layer = nir_u2u16(&b, nir_load_layer_id(&b));

      nir_image_store_block_agx(
         &b, nir_imm_intN_t(&b, rt, 16), nir_imm_intN_t(&b, offset_B, 16),
         layer, .format = agx_tilebuffer_physical_format(&key->tib, rt),
         .image_dim = dim, .image_array = key->tib.layered);
   }

   struct agx_shader_key compiler_key = {
      .reserved_preamble = key->reserved_preamble,
   };

   return agx_compile_bg_eot_shader(cache, b.shader, &compiler_key, NULL);
}

struct agx_bg_eot_shader *
agx_get_bg_eot_shader(struct agx_bg_eot_cache *cache,
                      struct agx_bg_eot_key *key)
{
   struct hash_entry *ent = _mesa_hash_table_search(cache->ht, key);
   if (ent)
      return ent->data;

   struct agx_bg_eot_shader *ret = NULL;

   for (unsigned rt = 0; rt < ARRAY_SIZE(key->op); ++rt) {
      if (key->op[rt] == AGX_EOT_STORE) {
         ret = agx_build_end_of_tile_shader(cache, key);
         break;
      }
   }

   if (!ret)
      ret = agx_build_background_shader(cache, key);

   ret->key = *key;
   _mesa_hash_table_insert(cache->ht, &ret->key, ret);
   return ret;
}

DERIVE_HASH_TABLE(agx_bg_eot_key);

void
agx_bg_eot_init(struct agx_bg_eot_cache *cache, struct agx_device *dev)
{
   agx_pool_init(&cache->pool, dev, "Internal programs",
                 AGX_BO_EXEC | AGX_BO_LOW_VA, true);
   simple_mtx_init(&cache->lock, mtx_plain);
   cache->ht = agx_bg_eot_key_table_create(NULL);
   cache->dev = dev;
}

void
agx_bg_eot_cleanup(struct agx_bg_eot_cache *cache)
{
   agx_pool_cleanup(&cache->pool);
   _mesa_hash_table_destroy(cache->ht, NULL);
   simple_mtx_destroy(&cache->lock);
   cache->ht = NULL;
   cache->dev = NULL;
}

static struct agx_precompiled_shader *
agx_get_precompiled_locked(struct agx_bg_eot_cache *cache, unsigned program)
{
   simple_mtx_assert_locked(&cache->lock);

   /* It is possible that, while waiting for the lock, another thread uploaded
    * the shader. Check for that so we don't double-upload.
    */
   if (cache->precomp[program])
      return cache->precomp[program];

   /* Otherwise, we need to upload. */
   struct agx_precompiled_shader *p =
      ralloc(cache->ht, struct agx_precompiled_shader);

   const uint32_t *bin = cache->dev->libagx_programs[program];
   const struct agx_precompiled_kernel_info *info = (void *)bin;
   const void *binary = (const uint8_t *)bin + sizeof(*info);

   assert(info->main_offset == 0 || program != LIBAGX_HELPER);

   p->b.workgroup =
      agx_workgroup(info->workgroup_size[0], info->workgroup_size[1],
                    info->workgroup_size[2]);

   p->ptr = agx_pool_upload_aligned_with_bo(&cache->pool, binary,
                                            info->binary_size, 128, &p->bo);

   /* Bake launch */
   agx_pack(&p->b.launch, CDM_LAUNCH_WORD_0, cfg) {
      cfg.sampler_state_register_count = 1;
      cfg.uniform_register_count = info->push_count;
      cfg.preshader_register_count = info->nr_preamble_gprs;
   }

   /* Bake USC */
   struct agx_usc_builder b =
      agx_usc_builder(p->b.usc.data, sizeof(p->b.usc.data));

   agx_usc_immediates(&b, &info->rodata, p->ptr);

   if (info->uses_txf)
      agx_usc_push_packed(&b, SAMPLER, cache->dev->txf_sampler);

   agx_usc_shared(&b, info->local_size, info->imageblock_stride, 0);

   agx_usc_pack(&b, SHADER, cfg) {
      cfg.code = agx_usc_addr(cache->dev, p->ptr + info->main_offset);
      cfg.unk_2 = 3;
   }

   agx_usc_pack(&b, REGISTERS, cfg) {
      cfg.register_count = info->nr_gprs;
      cfg.spill_size = 0;
   }

   if (info->nr_preamble_gprs) {
      agx_usc_pack(&b, PRESHADER, cfg) {
         cfg.code = agx_usc_addr(cache->dev, p->ptr + info->preamble_offset);
      }
   } else {
      agx_usc_pack(&b, NO_PRESHADER, cfg)
         ;
   }

   p->b.usc.size = b.head - p->b.usc.data;

   /* We must only write to the cache once we are done compiling, since other
    * threads may be reading the cache concurrently. Do this last.
    */
   p_atomic_set(&cache->precomp[program], p);
   return p;
}

struct agx_precompiled_shader *
agx_get_precompiled(struct agx_bg_eot_cache *cache, unsigned program)
{
   /* Shaders are immutable once written, so if we atomically read a non-NULL
    * shader, then we have a valid cached shader and are done.
    */
   struct agx_precompiled_shader *ret = p_atomic_read(cache->precomp + program);

   if (ret != NULL)
      return ret;

   /* Otherwise, take the lock and upload. */
   simple_mtx_lock(&cache->lock);
   ret = agx_get_precompiled_locked(cache, program);
   simple_mtx_unlock(&cache->lock);

   return ret;
}

uint64_t
agx_helper_program(struct agx_bg_eot_cache *cache)
{
   struct agx_precompiled_shader *pc =
      agx_get_precompiled(cache, LIBAGX_HELPER);

   return pc->ptr | 1;
}
