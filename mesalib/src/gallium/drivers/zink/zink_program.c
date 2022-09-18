/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "zink_program.h"

#include "zink_compiler.h"
#include "zink_context.h"
#include "zink_descriptors.h"
#include "zink_helpers.h"
#include "zink_pipeline.h"
#include "zink_render_pass.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "zink_state.h"
#include "zink_inlines.h"

#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_prim.h"
#include "nir_serialize.h"

/* for pipeline cache */
#define XXH_INLINE_ALL
#include "util/xxhash.h"

void
debug_describe_zink_gfx_program(char *buf, const struct zink_gfx_program *ptr)
{
   sprintf(buf, "zink_gfx_program");
}

void
debug_describe_zink_compute_program(char *buf, const struct zink_compute_program *ptr)
{
   sprintf(buf, "zink_compute_program");
}

ALWAYS_INLINE static bool
shader_key_matches_tcs_nongenerated(const struct zink_shader_module *zm, const struct zink_shader_key *key, unsigned num_uniforms)
{
   if (zm->num_uniforms != num_uniforms || zm->has_nonseamless != !!key->base.nonseamless_cube_mask)
      return false;
   const uint32_t nonseamless_size = zm->has_nonseamless ? sizeof(uint32_t) : 0;
   return (!nonseamless_size || !memcmp(zm->key + zm->key_size, &key->base.nonseamless_cube_mask, nonseamless_size)) &&
          (!num_uniforms || !memcmp(zm->key + zm->key_size + nonseamless_size,
                                    key->base.inlined_uniform_values, zm->num_uniforms * sizeof(uint32_t)));
}

ALWAYS_INLINE static bool
shader_key_matches(const struct zink_shader_module *zm,
                   const struct zink_shader_key *key, unsigned num_uniforms,
                   bool has_inline, bool has_nonseamless)
{
   const uint32_t nonseamless_size = !has_nonseamless && zm->has_nonseamless ? sizeof(uint32_t) : 0;
   if (has_inline) {
      if (zm->num_uniforms != num_uniforms ||
          (num_uniforms &&
           memcmp(zm->key + zm->key_size + nonseamless_size,
                  key->base.inlined_uniform_values, zm->num_uniforms * sizeof(uint32_t))))
         return false;
   }
   if (!has_nonseamless) {
      if (zm->has_nonseamless != !!key->base.nonseamless_cube_mask ||
          (nonseamless_size && memcmp(zm->key + zm->key_size, &key->base.nonseamless_cube_mask, nonseamless_size)))
         return false;
   }
   return !memcmp(zm->key, key, zm->key_size);
}

static uint32_t
shader_module_hash(const struct zink_shader_module *zm)
{
   const uint32_t nonseamless_size = zm->has_nonseamless ? sizeof(uint32_t) : 0;
   unsigned key_size = zm->key_size + nonseamless_size + zm->num_uniforms * sizeof(uint32_t);
   return _mesa_hash_data(zm->key, key_size);
}

ALWAYS_INLINE static void
gather_shader_module_info(struct zink_context *ctx, struct zink_screen *screen,
                          struct zink_shader *zs, struct zink_gfx_program *prog,
                          struct zink_gfx_pipeline_state *state,
                          bool has_inline, //is inlining enabled?
                          bool has_nonseamless, //is nonseamless ext present?
                          unsigned *inline_size, unsigned *nonseamless_size)
{
   gl_shader_stage stage = zs->nir->info.stage;
   struct zink_shader_key *key = &state->shader_keys.key[stage];
   if (has_inline && ctx && zs->nir->info.num_inlinable_uniforms &&
       ctx->inlinable_uniforms_valid_mask & BITFIELD64_BIT(stage)) {
      if (zs->can_inline && (screen->is_cpu || prog->inlined_variant_count[stage] < ZINK_MAX_INLINED_VARIANTS))
         *inline_size = zs->nir->info.num_inlinable_uniforms;
      else
         key->inline_uniforms = false;
   }
   if (!has_nonseamless && key->base.nonseamless_cube_mask)
      *nonseamless_size = sizeof(uint32_t);
}

ALWAYS_INLINE static struct zink_shader_module *
create_shader_module_for_stage(struct zink_context *ctx, struct zink_screen *screen,
                               struct zink_shader *zs, struct zink_gfx_program *prog,
                               gl_shader_stage stage,
                               struct zink_gfx_pipeline_state *state,
                               unsigned inline_size, unsigned nonseamless_size,
                               bool has_inline, //is inlining enabled?
                               bool has_nonseamless) //is nonseamless ext present?
{
   VkShaderModule mod;
   struct zink_shader_module *zm;
   const struct zink_shader_key *key = &state->shader_keys.key[stage];
   /* non-generated tcs won't use the shader key */
   const bool is_nongenerated_tcs = stage == MESA_SHADER_TESS_CTRL && !zs->is_generated;
   zm = malloc(sizeof(struct zink_shader_module) + key->size + (!has_nonseamless ? nonseamless_size : 0) + inline_size * sizeof(uint32_t));
   if (!zm) {
      return NULL;
   }
   unsigned patch_vertices = state->shader_keys.key[MESA_SHADER_TESS_CTRL ].key.tcs.patch_vertices;
   if (stage == MESA_SHADER_TESS_CTRL && zs->is_generated && zs->spirv) {
      assert(ctx); //TODO async
      mod = zink_shader_tcs_compile(screen, zs, patch_vertices);
   } else {
      mod = zink_shader_compile(screen, zs, prog->nir[stage], key);
   }
   if (!mod) {
      FREE(zm);
      return NULL;
   }
   zm->shader = mod;
   zm->num_uniforms = inline_size;
   if (!is_nongenerated_tcs) {
      zm->key_size = key->size;
      memcpy(zm->key, key, key->size);
   } else {
      zm->key_size = 0;
      memset(zm->key, 0, key->size);
   }
   if (!has_nonseamless && nonseamless_size) {
      /* nonseamless mask gets added to base key if it exists */
      memcpy(zm->key + key->size, &key->base.nonseamless_cube_mask, nonseamless_size);
   }
   zm->has_nonseamless = has_nonseamless ? 0 : !!nonseamless_size;
   if (inline_size)
      memcpy(zm->key + key->size + nonseamless_size, key->base.inlined_uniform_values, inline_size * sizeof(uint32_t));
   if (stage == MESA_SHADER_TESS_CTRL && zs->is_generated)
      zm->hash = patch_vertices;
   else
      zm->hash = shader_module_hash(zm);
   zm->default_variant = !inline_size && !util_dynarray_contains(&prog->shader_cache[stage][0][0], void*);
   if (inline_size)
      prog->inlined_variant_count[stage]++;
   util_dynarray_append(&prog->shader_cache[stage][has_nonseamless ? 0 : !!nonseamless_size][!!inline_size], void*, zm);
   return zm;
}

ALWAYS_INLINE static struct zink_shader_module *
get_shader_module_for_stage(struct zink_context *ctx, struct zink_screen *screen,
                            struct zink_shader *zs, struct zink_gfx_program *prog,
                            gl_shader_stage stage,
                            struct zink_gfx_pipeline_state *state,
                            unsigned inline_size, unsigned nonseamless_size,
                            bool has_inline, //is inlining enabled?
                            bool has_nonseamless) //is nonseamless ext present?
{
   const struct zink_shader_key *key = &state->shader_keys.key[stage];
   /* non-generated tcs won't use the shader key */
   const bool is_nongenerated_tcs = stage == MESA_SHADER_TESS_CTRL && !zs->is_generated;

   struct util_dynarray *shader_cache = &prog->shader_cache[stage][!has_nonseamless ? !!nonseamless_size : 0][has_inline ? !!inline_size : 0];
   unsigned count = util_dynarray_num_elements(shader_cache, struct zink_shader_module *);
   struct zink_shader_module **pzm = shader_cache->data;
   for (unsigned i = 0; i < count; i++) {
      struct zink_shader_module *iter = pzm[i];
      if (is_nongenerated_tcs) {
         if (!shader_key_matches_tcs_nongenerated(iter, key, has_inline ? !!inline_size : 0))
            continue;
      } else {
         if (stage == MESA_SHADER_VERTEX && iter->key_size != key->size)
            continue;
         if (!shader_key_matches(iter, key, inline_size, has_inline, has_nonseamless))
            continue;
      }
      if (i > 0) {
         struct zink_shader_module *zero = pzm[0];
         pzm[0] = iter;
         pzm[i] = zero;
      }
      return iter;
   }

   return NULL;
}

static void
zink_destroy_shader_module(struct zink_screen *screen, struct zink_shader_module *zm)
{
   VKSCR(DestroyShaderModule)(screen->dev, zm->shader, NULL);
   free(zm);
}

static void
destroy_shader_cache(struct zink_screen *screen, struct util_dynarray *sc)
{
   while (util_dynarray_contains(sc, void*)) {
      struct zink_shader_module *zm = util_dynarray_pop(sc, struct zink_shader_module*);
      zink_destroy_shader_module(screen, zm);
   }
}

ALWAYS_INLINE static void
update_gfx_shader_modules(struct zink_context *ctx,
                      struct zink_screen *screen,
                      struct zink_gfx_program *prog, uint32_t mask,
                      struct zink_gfx_pipeline_state *state,
                      bool has_inline, //is inlining enabled?
                      bool has_nonseamless) //is nonseamless ext present?
{
   bool hash_changed = false;
   bool default_variants = true;
   assert(prog->modules[MESA_SHADER_VERTEX]);
   uint32_t variant_hash = prog->last_variant_hash;
   for (unsigned i = 0; i < MESA_SHADER_COMPUTE; i++) {
      if (!(mask & BITFIELD_BIT(i)))
         continue;

      assert(prog->shaders[i]);

      unsigned inline_size = 0, nonseamless_size = 0;
      gather_shader_module_info(ctx, screen, prog->shaders[i], prog, state, has_inline, has_nonseamless, &inline_size, &nonseamless_size);
      struct zink_shader_module *zm = get_shader_module_for_stage(ctx, screen, prog->shaders[i], prog, i, state,
                                                                  inline_size, nonseamless_size, has_inline, has_nonseamless);
      if (!zm)
         zm = create_shader_module_for_stage(ctx, screen, prog->shaders[i], prog, i, state,
                                             inline_size, nonseamless_size, has_inline, has_nonseamless);
      state->modules[i] = zm->shader;
      if (prog->modules[i] == zm)
         continue;
      variant_hash ^= prog->modules[i]->hash;
      hash_changed = true;
      default_variants &= zm->default_variant;
      prog->modules[i] = zm;
      if (has_inline) {
         if (zm->num_uniforms)
            prog->inline_variants |= BITFIELD_BIT(i);
         else
            prog->inline_variants &= ~BITFIELD_BIT(i);
      }
      variant_hash ^= prog->modules[i]->hash;
   }

   if (hash_changed && state) {
      if (default_variants)
         prog->last_variant_hash = prog->default_variant_hash;
      else
         prog->last_variant_hash = variant_hash;

      state->modules_changed = true;
   }
}

static void
generate_gfx_program_modules(struct zink_context *ctx, struct zink_screen *screen, struct zink_gfx_program *prog, struct zink_gfx_pipeline_state *state)
{
   assert(!prog->modules[MESA_SHADER_VERTEX]);
   uint32_t variant_hash = 0;
   bool default_variants = true;
   for (unsigned i = 0; i < MESA_SHADER_COMPUTE; i++) {
      if (!(prog->stages_present & BITFIELD_BIT(i)))
         continue;

      assert(prog->shaders[i]);

      unsigned inline_size = 0, nonseamless_size = 0;
      gather_shader_module_info(ctx, screen, prog->shaders[i], prog, state,
                                screen->driconf.inline_uniforms, screen->info.have_EXT_non_seamless_cube_map,
                                &inline_size, &nonseamless_size);
      struct zink_shader_module *zm = create_shader_module_for_stage(ctx, screen, prog->shaders[i], prog, i, state,
                                                                     inline_size, nonseamless_size,
                                                                     screen->driconf.inline_uniforms, screen->info.have_EXT_non_seamless_cube_map);
      state->modules[i] = zm->shader;
      prog->modules[i] = zm;
      default_variants &= zm->default_variant;
      variant_hash ^= prog->modules[i]->hash;
   }

   prog->last_variant_hash = variant_hash;
   p_atomic_dec(&prog->base.reference.count);
   if (default_variants)
      prog->default_variant_hash = prog->last_variant_hash;

   state->modules_changed = true;
}

static uint32_t
hash_pipeline_lib(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct zink_rasterizer_hw_state));
}

static bool
equals_pipeline_lib(const void *a, const void *b)
{
   return !memcmp(a, b, offsetof(struct zink_gfx_library_key, pipeline));
}

uint32_t
hash_gfx_input_dynamic(const void *key)
{
   const struct zink_gfx_input_key *ikey = key;
   return ikey->idx;
}

static bool
equals_gfx_input_dynamic(const void *a, const void *b)
{
   const struct zink_gfx_input_key *ikey_a = a;
   const struct zink_gfx_input_key *ikey_b = b;
   return ikey_a->idx == ikey_b->idx;
}

uint32_t
hash_gfx_input(const void *key)
{
   const struct zink_gfx_input_key *ikey = key;
   if (ikey->uses_dynamic_stride)
      return ikey->input;
   return _mesa_hash_data(key, offsetof(struct zink_gfx_input_key, pipeline));
}

static bool
equals_gfx_input(const void *a, const void *b)
{
   const struct zink_gfx_input_key *ikey_a = a;
   const struct zink_gfx_input_key *ikey_b = b;
   if (ikey_a->uses_dynamic_stride)
      return ikey_a->element_state == ikey_b->element_state &&
             !memcmp(a, b, offsetof(struct zink_gfx_input_key, vertex_buffers_enabled_mask));
   return !memcmp(a, b, offsetof(struct zink_gfx_input_key, pipeline));
}

uint32_t
hash_gfx_output(const void *key)
{
   const uint8_t *data = key;
   const struct zink_gfx_output_key *okey = key;
   /* manually add in force_persample_interp */
   return okey->force_persample_interp ^
          _mesa_hash_data(data + sizeof(uint16_t), offsetof(struct zink_gfx_output_key, pipeline) - sizeof(uint16_t));
}

static bool
equals_gfx_output(const void *a, const void *b)
{
   const uint8_t *da = a;
   const uint8_t *db = b;
   return !memcmp(da + sizeof(uint16_t), db + sizeof(uint16_t), offsetof(struct zink_gfx_output_key, pipeline) - sizeof(uint16_t));
}

ALWAYS_INLINE static void
update_gfx_program_nonseamless(struct zink_context *ctx, struct zink_gfx_program *prog, bool has_nonseamless)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   if (screen->driconf.inline_uniforms)
      update_gfx_shader_modules(ctx, screen, prog,
                                ctx->dirty_shader_stages & prog->stages_present, &ctx->gfx_pipeline_state,
                                true, has_nonseamless);
   else
      update_gfx_shader_modules(ctx, screen, prog,
                                ctx->dirty_shader_stages & prog->stages_present, &ctx->gfx_pipeline_state,
                                false, has_nonseamless);
}

static void
update_gfx_program(struct zink_context *ctx, struct zink_gfx_program *prog)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   if (screen->info.have_EXT_non_seamless_cube_map)
      update_gfx_program_nonseamless(ctx, prog, true);
   else
      update_gfx_program_nonseamless(ctx, prog, false);
}

void
zink_gfx_program_update(struct zink_context *ctx)
{
   if (ctx->last_vertex_stage_dirty) {
      gl_shader_stage pstage = ctx->last_vertex_stage->nir->info.stage;
      ctx->dirty_shader_stages |= BITFIELD_BIT(pstage);
      memcpy(&ctx->gfx_pipeline_state.shader_keys.key[pstage].key.vs_base,
             &ctx->gfx_pipeline_state.shader_keys.last_vertex.key.vs_base,
             sizeof(struct zink_vs_key_base));
      ctx->last_vertex_stage_dirty = false;
   }
   unsigned bits = BITFIELD_MASK(MESA_SHADER_COMPUTE);
   if (ctx->gfx_dirty) {
      struct zink_gfx_program *prog = NULL;

      struct hash_table *ht = &ctx->program_cache[zink_program_cache_stages(ctx->shader_stages)];
      const uint32_t hash = ctx->gfx_hash;
      struct hash_entry *entry = _mesa_hash_table_search_pre_hashed(ht, hash, ctx->gfx_stages);
      if (entry) {
         prog = (struct zink_gfx_program*)entry->data;
         for (unsigned i = 0; i < ZINK_GFX_SHADER_COUNT; i++) {
            if (prog->stages_present & ctx->dirty_shader_stages & BITFIELD_BIT(i))
               ctx->gfx_pipeline_state.modules[i] = prog->modules[i]->shader;
         }
         /* ensure variants are always updated if keys have changed since last use */
         ctx->dirty_shader_stages |= prog->stages_present;
         update_gfx_program(ctx, prog);
      } else {
         ctx->dirty_shader_stages |= bits;
         prog = zink_create_gfx_program(ctx, ctx->gfx_stages, ctx->gfx_pipeline_state.dyn_state2.vertices_per_patch);
         _mesa_hash_table_insert_pre_hashed(ht, hash, prog->shaders, prog);
         generate_gfx_program_modules(ctx, zink_screen(ctx->base.screen), prog, &ctx->gfx_pipeline_state);
      }
      if (prog && prog != ctx->curr_program)
         zink_batch_reference_program(&ctx->batch, &prog->base);
      if (ctx->curr_program)
         ctx->gfx_pipeline_state.final_hash ^= ctx->curr_program->last_variant_hash;
      ctx->curr_program = prog;
      ctx->gfx_pipeline_state.final_hash ^= ctx->curr_program->last_variant_hash;
      ctx->gfx_dirty = false;
   } else if (ctx->dirty_shader_stages & bits) {
      /* remove old hash */
      ctx->gfx_pipeline_state.final_hash ^= ctx->curr_program->last_variant_hash;
      update_gfx_program(ctx, ctx->curr_program);
      /* apply new hash */
      ctx->gfx_pipeline_state.final_hash ^= ctx->curr_program->last_variant_hash;
   }
   ctx->dirty_shader_stages &= ~bits;
}

static void
update_cs_shader_module(struct zink_context *ctx, struct zink_compute_program *comp)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_shader *zs = comp->shader;
   VkShaderModule mod;
   struct zink_shader_module *zm = NULL;
   unsigned inline_size = 0, nonseamless_size = 0;
   struct zink_shader_key *key = &ctx->compute_pipeline_state.key;

   if (ctx && zs->nir->info.num_inlinable_uniforms &&
       ctx->inlinable_uniforms_valid_mask & BITFIELD64_BIT(MESA_SHADER_COMPUTE)) {
      if (screen->is_cpu || comp->inlined_variant_count < ZINK_MAX_INLINED_VARIANTS)
         inline_size = zs->nir->info.num_inlinable_uniforms;
      else
         key->inline_uniforms = false;
   }
   if (key->base.nonseamless_cube_mask)
      nonseamless_size = sizeof(uint32_t);

   if (inline_size || nonseamless_size) {
      struct util_dynarray *shader_cache = &comp->shader_cache[!!nonseamless_size];
      unsigned count = util_dynarray_num_elements(shader_cache, struct zink_shader_module *);
      struct zink_shader_module **pzm = shader_cache->data;
      for (unsigned i = 0; i < count; i++) {
         struct zink_shader_module *iter = pzm[i];
         if (!shader_key_matches(iter, key, inline_size,
                                 screen->driconf.inline_uniforms,
                                 screen->info.have_EXT_non_seamless_cube_map))
            continue;
         if (i > 0) {
            struct zink_shader_module *zero = pzm[0];
            pzm[0] = iter;
            pzm[i] = zero;
         }
         zm = iter;
      }
   } else {
      zm = comp->module;
   }

   if (!zm) {
      zm = malloc(sizeof(struct zink_shader_module) + nonseamless_size + inline_size * sizeof(uint32_t));
      if (!zm) {
         return;
      }
      mod = zink_shader_compile(screen, zs, comp->shader->nir, key);
      if (!mod) {
         FREE(zm);
         return;
      }
      zm->shader = mod;
      zm->num_uniforms = inline_size;
      zm->key_size = 0;
      zm->has_nonseamless = !!nonseamless_size;
      assert(nonseamless_size || inline_size);
      if (nonseamless_size)
         memcpy(zm->key, &key->base.nonseamless_cube_mask, nonseamless_size);
      if (inline_size)
         memcpy(zm->key + nonseamless_size, key->base.inlined_uniform_values, inline_size * sizeof(uint32_t));
      zm->hash = shader_module_hash(zm);
      zm->default_variant = false;
      if (inline_size)
         comp->inlined_variant_count++;

      /* this is otherwise the default variant, which is stored as comp->module */
      if (zm->num_uniforms || nonseamless_size)
         util_dynarray_append(&comp->shader_cache[!!nonseamless_size], void*, zm);
   }
   if (comp->curr == zm)
      return;
   ctx->compute_pipeline_state.final_hash ^= ctx->compute_pipeline_state.module_hash;
   comp->curr = zm;
   ctx->compute_pipeline_state.module_hash = zm->hash;
   ctx->compute_pipeline_state.final_hash ^= ctx->compute_pipeline_state.module_hash;
   ctx->compute_pipeline_state.module_changed = true;
}

void
zink_update_compute_program(struct zink_context *ctx)
{
   util_queue_fence_wait(&ctx->curr_compute->base.cache_fence);
   update_cs_shader_module(ctx, ctx->curr_compute);
}

VkPipelineLayout
zink_pipeline_layout_create(struct zink_screen *screen, struct zink_program *pg, uint32_t *compat)
{
   VkPipelineLayoutCreateInfo plci = {0};
   plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

   plci.pSetLayouts = pg->dsl;
   plci.setLayoutCount = pg->num_dsl;

   VkPushConstantRange pcr[2] = {0};
   if (pg->is_compute) {
      if (((struct zink_compute_program*)pg)->shader->nir->info.stage == MESA_SHADER_KERNEL) {
         pcr[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
         pcr[0].offset = 0;
         pcr[0].size = sizeof(struct zink_cs_push_constant);
         plci.pushConstantRangeCount = 1;
      }
   } else {
      pcr[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
      pcr[0].offset = offsetof(struct zink_gfx_push_constant, draw_mode_is_indexed);
      pcr[0].size = 2 * sizeof(unsigned);
      pcr[1].stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
      pcr[1].offset = offsetof(struct zink_gfx_push_constant, default_inner_level);
      pcr[1].size = sizeof(float) * 6;
      plci.pushConstantRangeCount = 2;
   }
   plci.pPushConstantRanges = &pcr[0];

   VkPipelineLayout layout;
   VkResult result = VKSCR(CreatePipelineLayout)(screen->dev, &plci, NULL, &layout);
   if (result != VK_SUCCESS) {
      mesa_loge("vkCreatePipelineLayout failed (%s)", vk_Result_to_str(result));
      return VK_NULL_HANDLE;
   }

   *compat = _mesa_hash_data(pg->dsl, pg->num_dsl * sizeof(pg->dsl[0]));

   return layout;
}

static void *
create_program(struct zink_context *ctx, bool is_compute)
{
   struct zink_program *pg = rzalloc_size(NULL, is_compute ? sizeof(struct zink_compute_program) : sizeof(struct zink_gfx_program));
   if (!pg)
      return NULL;

   pipe_reference_init(&pg->reference, 1);
   util_queue_fence_init(&pg->cache_fence);
   pg->is_compute = is_compute;
   pg->ctx = ctx;
   return (void*)pg;
}

static void
assign_io(struct zink_gfx_program *prog, struct zink_shader *stages[ZINK_GFX_SHADER_COUNT])
{
   struct zink_shader *shaders[MESA_SHADER_STAGES];

   /* build array in pipeline order */
   for (unsigned i = 0; i < ZINK_GFX_SHADER_COUNT; i++)
      shaders[i] = stages[i];

   for (unsigned i = 0; i < MESA_SHADER_FRAGMENT;) {
      nir_shader *producer = shaders[i]->nir;
      for (unsigned j = i + 1; j < ZINK_GFX_SHADER_COUNT; i++, j++) {
         struct zink_shader *consumer = shaders[j];
         if (!consumer)
            continue;
         if (!prog->nir[producer->info.stage])
            prog->nir[producer->info.stage] = nir_shader_clone(prog, producer);
         if (!prog->nir[j])
            prog->nir[j] = nir_shader_clone(prog, consumer->nir);
         zink_compiler_assign_io(prog->nir[producer->info.stage], prog->nir[j]);
         i = j;
         break;
      }
   }
}

struct zink_gfx_program *
zink_create_gfx_program(struct zink_context *ctx,
                        struct zink_shader **stages,
                        unsigned vertices_per_patch)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_gfx_program *prog = create_program(ctx, false);
   if (!prog)
      goto fail;

   for (int i = 0; i < ZINK_GFX_SHADER_COUNT; ++i) {
      util_dynarray_init(&prog->shader_cache[i][0][0], NULL);
      util_dynarray_init(&prog->shader_cache[i][0][1], NULL);
      util_dynarray_init(&prog->shader_cache[i][1][0], NULL);
      util_dynarray_init(&prog->shader_cache[i][1][1], NULL);
      if (stages[i]) {
         prog->shaders[i] = stages[i];
         prog->stages_present |= BITFIELD_BIT(i);
      }
   }
   if (stages[MESA_SHADER_TESS_EVAL] && !stages[MESA_SHADER_TESS_CTRL]) {
      prog->shaders[MESA_SHADER_TESS_EVAL]->generated =
      prog->shaders[MESA_SHADER_TESS_CTRL] =
        zink_shader_tcs_create(screen, stages[MESA_SHADER_VERTEX], vertices_per_patch);
      prog->stages_present |= BITFIELD_BIT(MESA_SHADER_TESS_CTRL);
   }

   assign_io(prog, prog->shaders);

   if (stages[MESA_SHADER_GEOMETRY])
      prog->last_vertex_stage = stages[MESA_SHADER_GEOMETRY];
   else if (stages[MESA_SHADER_TESS_EVAL])
      prog->last_vertex_stage = stages[MESA_SHADER_TESS_EVAL];
   else
      prog->last_vertex_stage = stages[MESA_SHADER_VERTEX];

   for (int r = 0; r < ARRAY_SIZE(prog->pipelines); ++r) {
      for (int i = 0; i < ARRAY_SIZE(prog->pipelines[0]); ++i) {
         _mesa_hash_table_init(&prog->pipelines[r][i], prog, NULL, zink_get_gfx_pipeline_eq_func(screen, prog));
         /* only need first 3/4 for point/line/tri/patch */
         if (screen->info.have_EXT_extended_dynamic_state &&
             i == (prog->last_vertex_stage->nir->info.stage == MESA_SHADER_TESS_EVAL ? 4 : 3))
            break;
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(prog->libs); i++)
      _mesa_set_init(&prog->libs[i], prog, hash_pipeline_lib, equals_pipeline_lib);

   struct mesa_sha1 sctx;
   _mesa_sha1_init(&sctx);
   for (int i = 0; i < ZINK_GFX_SHADER_COUNT; ++i) {
      if (prog->shaders[i]) {
         simple_mtx_lock(&prog->shaders[i]->lock);
         _mesa_set_add(prog->shaders[i]->programs, prog);
         simple_mtx_unlock(&prog->shaders[i]->lock);
         zink_gfx_program_reference(ctx, NULL, prog);
         _mesa_sha1_update(&sctx, prog->shaders[i]->base.sha1, sizeof(prog->shaders[i]->base.sha1));
      }
   }
   _mesa_sha1_final(&sctx, prog->base.sha1);

   if (!zink_descriptor_program_init(ctx, &prog->base))
      goto fail;

   zink_screen_get_pipeline_cache(screen, &prog->base, false);
   return prog;

fail:
   if (prog)
      zink_destroy_gfx_program(ctx, prog);
   return NULL;
}

static uint32_t
hash_compute_pipeline_state_local_size(const void *key)
{
   const struct zink_compute_pipeline_state *state = key;
   uint32_t hash = _mesa_hash_data(state, offsetof(struct zink_compute_pipeline_state, hash));
   hash = XXH32(&state->local_size[0], sizeof(state->local_size), hash);
   return hash;
}

static uint32_t
hash_compute_pipeline_state(const void *key)
{
   const struct zink_compute_pipeline_state *state = key;
   return _mesa_hash_data(state, offsetof(struct zink_compute_pipeline_state, hash));
}

void
zink_program_update_compute_pipeline_state(struct zink_context *ctx, struct zink_compute_program *comp, const uint block[3])
{
   if (comp->use_local_size) {
      for (int i = 0; i < ARRAY_SIZE(ctx->compute_pipeline_state.local_size); i++) {
         if (ctx->compute_pipeline_state.local_size[i] != block[i])
            ctx->compute_pipeline_state.dirty = true;
         ctx->compute_pipeline_state.local_size[i] = block[i];
      }
   }
}

static bool
equals_compute_pipeline_state(const void *a, const void *b)
{
   const struct zink_compute_pipeline_state *sa = a;
   const struct zink_compute_pipeline_state *sb = b;
   return !memcmp(a, b, offsetof(struct zink_compute_pipeline_state, hash)) &&
          sa->module == sb->module;
}

static bool
equals_compute_pipeline_state_local_size(const void *a, const void *b)
{
   const struct zink_compute_pipeline_state *sa = a;
   const struct zink_compute_pipeline_state *sb = b;
   return !memcmp(a, b, offsetof(struct zink_compute_pipeline_state, hash)) &&
          !memcmp(sa->local_size, sb->local_size, sizeof(sa->local_size)) &&
          sa->module == sb->module;
}

static void
precompile_compute_job(void *data, void *gdata, int thread_index)
{
   struct zink_compute_program *comp = data;
   struct zink_screen *screen = gdata;

   comp->shader = zink_shader_create(screen, comp->nir, NULL);
   comp->curr = comp->module = CALLOC_STRUCT(zink_shader_module);
   assert(comp->module);
   comp->module->shader = zink_shader_compile(screen, comp->shader, comp->shader->nir, NULL);
   assert(comp->module->shader);
   util_dynarray_init(&comp->shader_cache[0], NULL);
   util_dynarray_init(&comp->shader_cache[1], NULL);

   struct blob blob = {0};
   blob_init(&blob);
   nir_serialize(&blob, comp->shader->nir, true);

   struct mesa_sha1 sha1_ctx;
   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, blob.data, blob.size);
   _mesa_sha1_final(&sha1_ctx, comp->base.sha1);
   blob_finish(&blob);

   zink_descriptor_program_init(comp->base.ctx, &comp->base);

   zink_screen_get_pipeline_cache(screen, &comp->base, true);
   if (comp->base.can_precompile)
      comp->base_pipeline = zink_create_compute_pipeline(screen, comp, NULL);
   if (comp->base_pipeline)
      zink_screen_update_pipeline_cache(screen, &comp->base, true);
}

static struct zink_compute_program *
create_compute_program(struct zink_context *ctx, nir_shader *nir)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_compute_program *comp = create_program(ctx, true);
   if (!comp)
      return NULL;
   comp->nir = nir;

   comp->use_local_size = !(nir->info.workgroup_size[0] ||
                            nir->info.workgroup_size[1] ||
                            nir->info.workgroup_size[2]);
   comp->base.can_precompile = !comp->use_local_size && (screen->info.have_EXT_non_seamless_cube_map || !zink_shader_has_cubes(nir));
   _mesa_hash_table_init(&comp->pipelines, comp, NULL, comp->use_local_size ?
                                                       equals_compute_pipeline_state_local_size :
                                                       equals_compute_pipeline_state);
   util_queue_add_job(&screen->cache_get_thread, comp, &comp->base.cache_fence,
                      precompile_compute_job, NULL, 0);
   return comp;
}

uint32_t
zink_program_get_descriptor_usage(struct zink_context *ctx, gl_shader_stage stage, enum zink_descriptor_type type)
{
   struct zink_shader *zs = NULL;
   switch (stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_TESS_EVAL:
   case MESA_SHADER_GEOMETRY:
   case MESA_SHADER_FRAGMENT:
      zs = ctx->gfx_stages[stage];
      break;
   case MESA_SHADER_COMPUTE: {
      zs = ctx->curr_compute->shader;
      break;
   }
   default:
      unreachable("unknown shader type");
   }
   if (!zs)
      return 0;
   switch (type) {
   case ZINK_DESCRIPTOR_TYPE_UBO:
      return zs->ubos_used;
   case ZINK_DESCRIPTOR_TYPE_SSBO:
      return zs->ssbos_used;
   case ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW:
      return BITSET_TEST_RANGE(zs->nir->info.textures_used, 0, PIPE_MAX_SAMPLERS - 1);
   case ZINK_DESCRIPTOR_TYPE_IMAGE:
      return BITSET_TEST_RANGE(zs->nir->info.images_used, 0, PIPE_MAX_SAMPLERS - 1);
   default:
      unreachable("unknown descriptor type!");
   }
   return 0;
}

bool
zink_program_descriptor_is_buffer(struct zink_context *ctx, gl_shader_stage stage, enum zink_descriptor_type type, unsigned i)
{
   struct zink_shader *zs = NULL;
   switch (stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_TESS_EVAL:
   case MESA_SHADER_GEOMETRY:
   case MESA_SHADER_FRAGMENT:
      zs = ctx->gfx_stages[stage];
      break;
   case MESA_SHADER_COMPUTE: {
      zs = ctx->curr_compute->shader;
      break;
   }
   default:
      unreachable("unknown shader type");
   }
   if (!zs)
      return false;
   return zink_shader_descriptor_is_buffer(zs, type, i);
}

static unsigned
get_num_bindings(struct zink_shader *zs, enum zink_descriptor_type type)
{
   switch (type) {
   case ZINK_DESCRIPTOR_TYPE_UBO:
   case ZINK_DESCRIPTOR_TYPE_SSBO:
      return zs->num_bindings[type];
   default:
      break;
   }
   unsigned num_bindings = 0;
   for (int i = 0; i < zs->num_bindings[type]; i++)
      num_bindings += zs->bindings[type][i].size;
   return num_bindings;
}

unsigned
zink_program_num_bindings_typed(const struct zink_program *pg, enum zink_descriptor_type type, bool is_compute)
{
   unsigned num_bindings = 0;
   if (is_compute) {
      struct zink_compute_program *comp = (void*)pg;
      return get_num_bindings(comp->shader, type);
   }
   struct zink_gfx_program *prog = (void*)pg;
   for (unsigned i = 0; i < ZINK_GFX_SHADER_COUNT; i++) {
      if (prog->shaders[i])
         num_bindings += get_num_bindings(prog->shaders[i], type);
   }
   return num_bindings;
}

unsigned
zink_program_num_bindings(const struct zink_program *pg, bool is_compute)
{
   unsigned num_bindings = 0;
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++)
      num_bindings += zink_program_num_bindings_typed(pg, i, is_compute);
   return num_bindings;
}

static void
deinit_program(struct zink_context *ctx, struct zink_program *pg)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   util_queue_fence_wait(&pg->cache_fence);
   if (pg->layout)
      VKSCR(DestroyPipelineLayout)(screen->dev, pg->layout, NULL);

   if (pg->pipeline_cache)
      VKSCR(DestroyPipelineCache)(screen->dev, pg->pipeline_cache, NULL);
   zink_descriptor_program_deinit(screen, pg);
}

void
zink_destroy_gfx_program(struct zink_context *ctx,
                         struct zink_gfx_program *prog)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   deinit_program(ctx, &prog->base);

   for (int i = 0; i < ZINK_GFX_SHADER_COUNT; ++i) {
      if (prog->shaders[i]) {
         _mesa_set_remove_key(prog->shaders[i]->programs, prog);
         prog->shaders[i] = NULL;
      }
      destroy_shader_cache(screen, &prog->shader_cache[i][0][0]);
      destroy_shader_cache(screen, &prog->shader_cache[i][0][1]);
      destroy_shader_cache(screen, &prog->shader_cache[i][1][0]);
      destroy_shader_cache(screen, &prog->shader_cache[i][1][1]);
      ralloc_free(prog->nir[i]);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(prog->libs); i++) {
      set_foreach_remove(&prog->libs[i], he) {
         struct zink_gfx_library_key *gkey = (void*)he->key;
         VKSCR(DestroyPipeline)(screen->dev, gkey->pipeline, NULL);
      }
   }

   unsigned max_idx = ARRAY_SIZE(prog->pipelines[0]);
   if (screen->info.have_EXT_extended_dynamic_state) {
      /* only need first 3/4 for point/line/tri/patch */
      if ((prog->stages_present &
          (BITFIELD_BIT(MESA_SHADER_TESS_EVAL) | BITFIELD_BIT(MESA_SHADER_GEOMETRY))) ==
          BITFIELD_BIT(MESA_SHADER_TESS_EVAL))
         max_idx = 4;
      else
         max_idx = 3;
      max_idx++;
   }

   for (unsigned r = 0; r < ARRAY_SIZE(prog->pipelines); r++) {
      for (int i = 0; i < max_idx; ++i) {
         hash_table_foreach(&prog->pipelines[r][i], entry) {
            struct gfx_pipeline_cache_entry *pc_entry = entry->data;

            VKSCR(DestroyPipeline)(screen->dev, pc_entry->pipeline, NULL);
            free(pc_entry);
         }
      }
   }

   ralloc_free(prog);
}

void
zink_destroy_compute_program(struct zink_context *ctx,
                             struct zink_compute_program *comp)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   deinit_program(ctx, &comp->base);

   if (comp->shader)
      _mesa_set_remove_key(comp->shader->programs, comp);
   destroy_shader_cache(screen, &comp->shader_cache[0]);
   destroy_shader_cache(screen, &comp->shader_cache[1]);

   hash_table_foreach(&comp->pipelines, entry) {
      struct compute_pipeline_cache_entry *pc_entry = entry->data;

      VKSCR(DestroyPipeline)(screen->dev, pc_entry->pipeline, NULL);
      free(pc_entry);
   }
   VKSCR(DestroyPipeline)(screen->dev, comp->base_pipeline, NULL);
   VKSCR(DestroyShaderModule)(screen->dev, comp->module->shader, NULL);
   free(comp->module);

   ralloc_free(comp);
}

ALWAYS_INLINE static bool
compute_can_shortcut(const struct zink_compute_program *comp)
{
   return !comp->use_local_size && !comp->curr->num_uniforms && !comp->curr->has_nonseamless;
}

VkPipeline
zink_get_compute_pipeline(struct zink_screen *screen,
                      struct zink_compute_program *comp,
                      struct zink_compute_pipeline_state *state)
{
   struct hash_entry *entry = NULL;

   if (!state->dirty && !state->module_changed)
      return state->pipeline;
   if (state->dirty) {
      if (state->pipeline) //avoid on first hash
         state->final_hash ^= state->hash;
      if (comp->use_local_size)
         state->hash = hash_compute_pipeline_state_local_size(state);
      else
         state->hash = hash_compute_pipeline_state(state);
      state->dirty = false;
      state->final_hash ^= state->hash;
   }

   util_queue_fence_wait(&comp->base.cache_fence);
   if (comp->base_pipeline && compute_can_shortcut(comp)) {
      state->pipeline = comp->base_pipeline;
      return state->pipeline;
   }
   entry = _mesa_hash_table_search_pre_hashed(&comp->pipelines, state->final_hash, state);

   if (!entry) {
      VkPipeline pipeline = zink_create_compute_pipeline(screen, comp, state);

      if (pipeline == VK_NULL_HANDLE)
         return VK_NULL_HANDLE;

      zink_screen_update_pipeline_cache(screen, &comp->base, false);
      if (compute_can_shortcut(comp)) {
         /* don't add base pipeline to cache */
         state->pipeline = comp->base_pipeline = pipeline;
         return state->pipeline;
      }

      struct compute_pipeline_cache_entry *pc_entry = CALLOC_STRUCT(compute_pipeline_cache_entry);
      if (!pc_entry)
         return VK_NULL_HANDLE;

      memcpy(&pc_entry->state, state, sizeof(*state));
      pc_entry->pipeline = pipeline;

      entry = _mesa_hash_table_insert_pre_hashed(&comp->pipelines, state->final_hash, pc_entry, pc_entry);
      assert(entry);
   }

   struct compute_pipeline_cache_entry *cache_entry = entry->data;
   state->pipeline = cache_entry->pipeline;
   return state->pipeline;
}

static inline void
bind_gfx_stage(struct zink_context *ctx, gl_shader_stage stage,
           struct zink_shader *shader)
{
   if (shader && shader->nir->info.num_inlinable_uniforms)
      ctx->shader_has_inlinable_uniforms_mask |= 1 << stage;
   else
      ctx->shader_has_inlinable_uniforms_mask &= ~(1 << stage);

   if (ctx->gfx_stages[stage])
      ctx->gfx_hash ^= ctx->gfx_stages[stage]->hash;
   ctx->gfx_stages[stage] = shader;
   ctx->gfx_dirty = ctx->gfx_stages[MESA_SHADER_FRAGMENT] && ctx->gfx_stages[MESA_SHADER_VERTEX];
   ctx->gfx_pipeline_state.modules_changed = true;
   if (shader) {
      ctx->shader_stages |= BITFIELD_BIT(stage);
      ctx->gfx_hash ^= ctx->gfx_stages[stage]->hash;
   } else {
      ctx->gfx_pipeline_state.modules[stage] = VK_NULL_HANDLE;
      if (ctx->curr_program)
         ctx->gfx_pipeline_state.final_hash ^= ctx->curr_program->last_variant_hash;
      ctx->curr_program = NULL;
      ctx->shader_stages &= ~BITFIELD_BIT(stage);
   }
}

static void
bind_last_vertex_stage(struct zink_context *ctx)
{
   gl_shader_stage old = ctx->last_vertex_stage ? ctx->last_vertex_stage->nir->info.stage : MESA_SHADER_STAGES;
   if (ctx->gfx_stages[MESA_SHADER_GEOMETRY])
      ctx->last_vertex_stage = ctx->gfx_stages[MESA_SHADER_GEOMETRY];
   else if (ctx->gfx_stages[MESA_SHADER_TESS_EVAL])
      ctx->last_vertex_stage = ctx->gfx_stages[MESA_SHADER_TESS_EVAL];
   else
      ctx->last_vertex_stage = ctx->gfx_stages[MESA_SHADER_VERTEX];
   gl_shader_stage current = ctx->last_vertex_stage ? ctx->last_vertex_stage->nir->info.stage : MESA_SHADER_VERTEX;
   if (old != current) {
      if (old != MESA_SHADER_STAGES) {
         memset(&ctx->gfx_pipeline_state.shader_keys.key[old].key.vs_base, 0, sizeof(struct zink_vs_key_base));
         ctx->dirty_shader_stages |= BITFIELD_BIT(old);
      } else {
         /* always unset vertex shader values when changing to a non-vs last stage */
         memset(&ctx->gfx_pipeline_state.shader_keys.key[MESA_SHADER_VERTEX].key.vs_base, 0, sizeof(struct zink_vs_key_base));
      }

      unsigned num_viewports = ctx->vp_state.num_viewports;
      struct zink_screen *screen = zink_screen(ctx->base.screen);
      /* number of enabled viewports is based on whether last vertex stage writes viewport index */
      if (ctx->last_vertex_stage) {
         if (ctx->last_vertex_stage->nir->info.outputs_written & (VARYING_BIT_VIEWPORT | VARYING_BIT_VIEWPORT_MASK))
            ctx->vp_state.num_viewports = MIN2(screen->info.props.limits.maxViewports, PIPE_MAX_VIEWPORTS);
         else
            ctx->vp_state.num_viewports = 1;
      } else {
         ctx->vp_state.num_viewports = 1;
      }
      ctx->vp_state_changed |= num_viewports != ctx->vp_state.num_viewports;
      if (!screen->info.have_EXT_extended_dynamic_state) {
         if (ctx->gfx_pipeline_state.dyn_state1.num_viewports != ctx->vp_state.num_viewports)
            ctx->gfx_pipeline_state.dirty = true;
         ctx->gfx_pipeline_state.dyn_state1.num_viewports = ctx->vp_state.num_viewports;
      }
      ctx->last_vertex_stage_dirty = true;
   }
}

static void
zink_bind_vs_state(struct pipe_context *pctx,
                   void *cso)
{
   struct zink_context *ctx = zink_context(pctx);
   if (!cso && !ctx->gfx_stages[MESA_SHADER_VERTEX])
      return;
   bind_gfx_stage(ctx, MESA_SHADER_VERTEX, cso);
   bind_last_vertex_stage(ctx);
   if (cso) {
      struct zink_shader *zs = cso;
      ctx->shader_reads_drawid = BITSET_TEST(zs->nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID);
      ctx->shader_reads_basevertex = BITSET_TEST(zs->nir->info.system_values_read, SYSTEM_VALUE_BASE_VERTEX);
   } else {
      ctx->shader_reads_drawid = false;
      ctx->shader_reads_basevertex = false;
   }
}

/* if gl_SampleMask[] is written to, we have to ensure that we get a shader with the same sample count:
 * in GL, samples==1 means ignore gl_SampleMask[]
 * in VK, gl_SampleMask[] is never ignored
 */
void
zink_update_fs_key_samples(struct zink_context *ctx)
{
   if (!ctx->gfx_stages[MESA_SHADER_FRAGMENT])
      return;
   nir_shader *nir = ctx->gfx_stages[MESA_SHADER_FRAGMENT]->nir;
   if (nir->info.outputs_written & (1 << FRAG_RESULT_SAMPLE_MASK)) {
      bool samples = zink_get_fs_key(ctx)->samples;
      if (samples != (ctx->fb_state.samples > 1))
         zink_set_fs_key(ctx)->samples = ctx->fb_state.samples > 1;
   }
}

static void
zink_bind_fs_state(struct pipe_context *pctx,
                   void *cso)
{
   struct zink_context *ctx = zink_context(pctx);
   if (!cso && !ctx->gfx_stages[MESA_SHADER_FRAGMENT])
      return;
   bind_gfx_stage(ctx, MESA_SHADER_FRAGMENT, cso);
   ctx->fbfetch_outputs = 0;
   if (cso) {
      nir_shader *nir = ctx->gfx_stages[MESA_SHADER_FRAGMENT]->nir;
      if (nir->info.fs.uses_fbfetch_output) {
         nir_foreach_shader_out_variable(var, ctx->gfx_stages[MESA_SHADER_FRAGMENT]->nir) {
            if (var->data.fb_fetch_output)
               ctx->fbfetch_outputs |= BITFIELD_BIT(var->data.location - FRAG_RESULT_DATA0);
         }
      }
      zink_update_fs_key_samples(ctx);
   }
   zink_update_fbfetch(ctx);
}

static void
zink_bind_gs_state(struct pipe_context *pctx,
                   void *cso)
{
   struct zink_context *ctx = zink_context(pctx);
   if (!cso && !ctx->gfx_stages[MESA_SHADER_GEOMETRY])
      return;
   bool had_points = ctx->gfx_stages[MESA_SHADER_GEOMETRY] ? ctx->gfx_stages[MESA_SHADER_GEOMETRY]->nir->info.gs.output_primitive == SHADER_PRIM_POINTS : false;
   bind_gfx_stage(ctx, MESA_SHADER_GEOMETRY, cso);
   bind_last_vertex_stage(ctx);
   if (cso) {
      if (!had_points && ctx->last_vertex_stage->nir->info.gs.output_primitive == SHADER_PRIM_POINTS)
         ctx->gfx_pipeline_state.has_points++;
   } else {
      if (had_points)
         ctx->gfx_pipeline_state.has_points--;
   }
}

static void
zink_bind_tcs_state(struct pipe_context *pctx,
                   void *cso)
{
   bind_gfx_stage(zink_context(pctx), MESA_SHADER_TESS_CTRL, cso);
}

static void
zink_bind_tes_state(struct pipe_context *pctx,
                   void *cso)
{
   struct zink_context *ctx = zink_context(pctx);
   if (!cso && !ctx->gfx_stages[MESA_SHADER_TESS_EVAL])
      return;
   if (!!ctx->gfx_stages[MESA_SHADER_TESS_EVAL] != !!cso) {
      if (!cso) {
         /* if unsetting a TESS that uses a generated TCS, ensure the TCS is unset */
         if (ctx->gfx_stages[MESA_SHADER_TESS_EVAL]->generated)
            ctx->gfx_stages[MESA_SHADER_TESS_CTRL] = NULL;
      }
   }
   bind_gfx_stage(ctx, MESA_SHADER_TESS_EVAL, cso);
   bind_last_vertex_stage(ctx);
}

static void *
zink_create_cs_state(struct pipe_context *pctx,
                     const struct pipe_compute_state *shader)
{
   struct nir_shader *nir;
   if (shader->ir_type != PIPE_SHADER_IR_NIR)
      nir = zink_tgsi_to_nir(pctx->screen, shader->prog);
   else
      nir = (struct nir_shader *)shader->prog;

   if (nir->info.uses_bindless)
      zink_descriptors_init_bindless(zink_context(pctx));

   return create_compute_program(zink_context(pctx), nir);
}

static void
zink_bind_cs_state(struct pipe_context *pctx,
                   void *cso)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_compute_program *comp = cso;
   if (comp && comp->nir->info.num_inlinable_uniforms)
      ctx->shader_has_inlinable_uniforms_mask |= 1 << MESA_SHADER_COMPUTE;
   else
      ctx->shader_has_inlinable_uniforms_mask &= ~(1 << MESA_SHADER_COMPUTE);

   if (ctx->curr_compute) {
      zink_batch_reference_program(&ctx->batch, &ctx->curr_compute->base);
      ctx->compute_pipeline_state.final_hash ^= ctx->compute_pipeline_state.module_hash;
      ctx->compute_pipeline_state.module = VK_NULL_HANDLE;
      ctx->compute_pipeline_state.module_hash = 0;
   }
   ctx->compute_pipeline_state.dirty = true;
   ctx->curr_compute = comp;
   if (comp && comp != ctx->curr_compute) {
      ctx->compute_pipeline_state.module_hash = ctx->curr_compute->curr->hash;
      if (util_queue_fence_is_signalled(&comp->base.cache_fence))
         ctx->compute_pipeline_state.module = ctx->curr_compute->curr->shader;
      ctx->compute_pipeline_state.final_hash ^= ctx->compute_pipeline_state.module_hash;
      if (ctx->compute_pipeline_state.key.base.nonseamless_cube_mask)
         ctx->dirty_shader_stages |= BITFIELD_BIT(MESA_SHADER_COMPUTE);
   }
   zink_select_launch_grid(ctx);
}

static void
zink_delete_cs_shader_state(struct pipe_context *pctx, void *cso)
{
   struct zink_compute_program *comp = cso;
   zink_compute_program_reference(zink_context(pctx), &comp, NULL);
}

void
zink_delete_shader_state(struct pipe_context *pctx, void *cso)
{
   zink_shader_free(zink_context(pctx), cso);
}

void *
zink_create_gfx_shader_state(struct pipe_context *pctx, const struct pipe_shader_state *shader)
{
   nir_shader *nir;
   if (shader->type != PIPE_SHADER_IR_NIR)
      nir = zink_tgsi_to_nir(pctx->screen, shader->tokens);
   else
      nir = (struct nir_shader *)shader->ir.nir;

   if (nir->info.stage == MESA_SHADER_FRAGMENT && nir->info.fs.uses_fbfetch_output)
      zink_descriptor_util_init_fbfetch(zink_context(pctx));
   if (nir->info.uses_bindless)
      zink_descriptors_init_bindless(zink_context(pctx));

   return zink_shader_create(zink_screen(pctx->screen), nir, &shader->stream_output);
}

static void
zink_delete_cached_shader_state(struct pipe_context *pctx, void *cso)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   util_shader_reference(pctx, &screen->shaders, &cso, NULL);
}

static void *
zink_create_cached_shader_state(struct pipe_context *pctx, const struct pipe_shader_state *shader)
{
   bool cache_hit;
   struct zink_screen *screen = zink_screen(pctx->screen);
   return util_live_shader_cache_get(pctx, &screen->shaders, shader, &cache_hit);
}

void
zink_program_init(struct zink_context *ctx)
{
   ctx->base.create_vs_state = zink_create_cached_shader_state;
   ctx->base.bind_vs_state = zink_bind_vs_state;
   ctx->base.delete_vs_state = zink_delete_cached_shader_state;

   ctx->base.create_fs_state = zink_create_cached_shader_state;
   ctx->base.bind_fs_state = zink_bind_fs_state;
   ctx->base.delete_fs_state = zink_delete_cached_shader_state;

   ctx->base.create_gs_state = zink_create_cached_shader_state;
   ctx->base.bind_gs_state = zink_bind_gs_state;
   ctx->base.delete_gs_state = zink_delete_cached_shader_state;

   ctx->base.create_tcs_state = zink_create_cached_shader_state;
   ctx->base.bind_tcs_state = zink_bind_tcs_state;
   ctx->base.delete_tcs_state = zink_delete_cached_shader_state;

   ctx->base.create_tes_state = zink_create_cached_shader_state;
   ctx->base.bind_tes_state = zink_bind_tes_state;
   ctx->base.delete_tes_state = zink_delete_cached_shader_state;

   ctx->base.create_compute_state = zink_create_cs_state;
   ctx->base.bind_compute_state = zink_bind_cs_state;
   ctx->base.delete_compute_state = zink_delete_cs_shader_state;

   if (zink_screen(ctx->base.screen)->info.have_EXT_vertex_input_dynamic_state)
      _mesa_set_init(&ctx->gfx_inputs, ctx, hash_gfx_input_dynamic, equals_gfx_input_dynamic);
   else
      _mesa_set_init(&ctx->gfx_inputs, ctx, hash_gfx_input, equals_gfx_input);
   _mesa_set_init(&ctx->gfx_outputs, ctx, hash_gfx_output, equals_gfx_output);
   /* validate struct packing */
   STATIC_ASSERT(offsetof(struct zink_gfx_pipeline_state, vertex_buffers_enabled_mask) - offsetof(struct zink_gfx_pipeline_state, input) ==
                 offsetof(struct zink_gfx_input_key, vertex_buffers_enabled_mask) - offsetof(struct zink_gfx_input_key, input));
   STATIC_ASSERT(offsetof(struct zink_gfx_pipeline_state, vertex_strides) - offsetof(struct zink_gfx_pipeline_state, input) ==
                 offsetof(struct zink_gfx_input_key, vertex_strides) - offsetof(struct zink_gfx_input_key, input));
   STATIC_ASSERT(offsetof(struct zink_gfx_pipeline_state, element_state) - offsetof(struct zink_gfx_pipeline_state, input) ==
                 offsetof(struct zink_gfx_input_key, element_state) - offsetof(struct zink_gfx_input_key, input));

   STATIC_ASSERT(offsetof(struct zink_gfx_pipeline_state, modules) - offsetof(struct zink_gfx_pipeline_state, gkey) ==
                 offsetof(struct zink_gfx_library_key, modules) - offsetof(struct zink_gfx_library_key, hw_rast_state));
}

bool
zink_set_rasterizer_discard(struct zink_context *ctx, bool disable)
{
   bool value = disable ? false : (ctx->rast_state ? ctx->rast_state->base.rasterizer_discard : false);
   bool changed = ctx->gfx_pipeline_state.dyn_state2.rasterizer_discard != value;
   ctx->gfx_pipeline_state.dyn_state2.rasterizer_discard = value;
   if (!changed)
      return false;
   if (!zink_screen(ctx->base.screen)->info.have_EXT_extended_dynamic_state2)
      ctx->gfx_pipeline_state.dirty |= true;
   ctx->rasterizer_discard_changed = true;
   return true;
}

void
zink_driver_thread_add_job(struct pipe_screen *pscreen, void *data,
                           struct util_queue_fence *fence,
                           pipe_driver_thread_func execute,
                           pipe_driver_thread_func cleanup,
                           const size_t job_size)
{
   struct zink_screen *screen = zink_screen(pscreen);
   util_queue_add_job(&screen->cache_get_thread, data, fence, execute, cleanup, job_size);
}
