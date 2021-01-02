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
#include "zink_render_pass.h"
#include "zink_screen.h"
#include "zink_state.h"

#include "util/hash_table.h"
#include "util/set.h"
#include "util/u_debug.h"
#include "util/u_memory.h"
#include "tgsi/tgsi_from_mesa.h"

struct pipeline_cache_entry {
   struct zink_gfx_pipeline_state state;
   VkPipeline pipeline;
};

void
debug_describe_zink_gfx_program(char *buf, const struct zink_gfx_program *ptr)
{
   sprintf(buf, "zink_gfx_program");
}

static void
debug_describe_zink_shader_module(char *buf, const struct zink_shader_module *ptr)
{
   sprintf(buf, "zink_shader_module");
}

static void
debug_describe_zink_shader_cache(char* buf, const struct zink_shader_cache *ptr)
{
   sprintf(buf, "zink_shader_cache");
}

/* copied from iris */
struct keybox {
   uint16_t size;
   gl_shader_stage stage;
   uint8_t data[0];
};

static struct keybox *
make_keybox(void *mem_ctx,
            gl_shader_stage stage,
            const void *key,
            uint32_t key_size)
{
   struct keybox *keybox =
      ralloc_size(mem_ctx, sizeof(struct keybox) + key_size);

   keybox->stage = stage;
   keybox->size = key_size;
   memcpy(keybox->data, key, key_size);

   return keybox;
}

static uint32_t
keybox_hash(const void *void_key)
{
   const struct keybox *key = void_key;
   return _mesa_hash_data(&key->stage, key->size + sizeof(key->stage));
}

static bool
keybox_equals(const void *void_a, const void *void_b)
{
   const struct keybox *a = void_a, *b = void_b;
   if (a->size != b->size)
      return false;

   return memcmp(a->data, b->data, a->size) == 0;
}

static VkDescriptorSetLayout
create_desc_set_layout(VkDevice dev,
                       struct zink_shader *stages[ZINK_SHADER_COUNT],
                       unsigned *num_descriptors)
{
   VkDescriptorSetLayoutBinding bindings[PIPE_SHADER_TYPES * PIPE_MAX_CONSTANT_BUFFERS];
   int num_bindings = 0;

   for (int i = 0; i < ZINK_SHADER_COUNT; i++) {
      struct zink_shader *shader = stages[i];
      if (!shader)
         continue;

      VkShaderStageFlagBits stage_flags = zink_shader_stage(i);
      for (int j = 0; j < shader->num_bindings; j++) {
         assert(num_bindings < ARRAY_SIZE(bindings));
         bindings[num_bindings].binding = shader->bindings[j].binding;
         bindings[num_bindings].descriptorType = shader->bindings[j].type;
         bindings[num_bindings].descriptorCount = 1;
         bindings[num_bindings].stageFlags = stage_flags;
         bindings[num_bindings].pImmutableSamplers = NULL;
         ++num_bindings;
      }
   }

   VkDescriptorSetLayoutCreateInfo dcslci = {};
   dcslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   dcslci.pNext = NULL;
   dcslci.flags = 0;
   dcslci.bindingCount = num_bindings;
   dcslci.pBindings = bindings;

   VkDescriptorSetLayout dsl;
   if (vkCreateDescriptorSetLayout(dev, &dcslci, 0, &dsl) != VK_SUCCESS) {
      debug_printf("vkCreateDescriptorSetLayout failed\n");
      return VK_NULL_HANDLE;
   }

   *num_descriptors = num_bindings;
   return dsl;
}

static VkPipelineLayout
create_pipeline_layout(VkDevice dev, VkDescriptorSetLayout dsl)
{
   assert(dsl != VK_NULL_HANDLE);

   VkPipelineLayoutCreateInfo plci = {};
   plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

   plci.pSetLayouts = &dsl;
   plci.setLayoutCount = 1;


   VkPushConstantRange pcr = {};
   pcr.stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
   pcr.offset = 0;
   pcr.size = sizeof(float) * 6;
   plci.pushConstantRangeCount = 1;
   plci.pPushConstantRanges = &pcr;

   VkPipelineLayout layout;
   if (vkCreatePipelineLayout(dev, &plci, NULL, &layout) != VK_SUCCESS) {
      debug_printf("vkCreatePipelineLayout failed!\n");
      return VK_NULL_HANDLE;
   }

   return layout;
}

 
static void
shader_key_fs_gen(struct zink_context *ctx, struct zink_shader *zs,
                  struct zink_shader *shaders[ZINK_SHADER_COUNT], struct zink_shader_key *key)
{
   struct zink_fs_key *fs_key = &key->key.fs;
   key->size = sizeof(struct zink_fs_key);

   fs_key->shader_id = zs->shader_id;
   //fs_key->flat_shade = ctx->rast_state->base.flatshade;

   /* if gl_SampleMask[] is written to, we have to ensure that we get a shader with the same sample count:
    * in GL, rast_samples==1 means ignore gl_SampleMask[]
    * in VK, gl_SampleMask[] is never ignored
    */
   if (zs->nir->info.outputs_written & (1 << FRAG_RESULT_SAMPLE_MASK))
      fs_key->samples = !!ctx->fb_state.samples;
}

static void
shader_key_tcs_gen(struct zink_context *ctx, struct zink_shader *zs,
                   struct zink_shader *shaders[ZINK_SHADER_COUNT], struct zink_shader_key *key)
{
   struct zink_tcs_key *tcs_key = &key->key.tcs;
   key->size = sizeof(struct zink_tcs_key);

   tcs_key->shader_id = zs->shader_id;
   tcs_key->vertices_per_patch = ctx->gfx_pipeline_state.vertices_per_patch;
   tcs_key->vs_outputs_written = shaders[PIPE_SHADER_VERTEX]->nir->info.outputs_written;
}

static void
shader_key_dummy_gen(struct zink_context *ctx, struct zink_shader *zs,
                     struct zink_shader *shaders[ZINK_SHADER_COUNT], struct zink_shader_key *key)
{
   struct zink_fs_key *fs_key = &key->key.fs;
   key->size = sizeof(uint32_t);
 
   fs_key->shader_id = zs->shader_id;
}

typedef void (*zink_shader_key_gen)(struct zink_context *ctx, struct zink_shader *zs,
                                    struct zink_shader *shaders[ZINK_SHADER_COUNT],
                                    struct zink_shader_key *key);
static zink_shader_key_gen shader_key_vtbl[] =
{
   [MESA_SHADER_VERTEX] = shader_key_dummy_gen,
   [MESA_SHADER_TESS_CTRL] = shader_key_tcs_gen,
   [MESA_SHADER_TESS_EVAL] = shader_key_dummy_gen,
   [MESA_SHADER_GEOMETRY] = shader_key_dummy_gen,
   [MESA_SHADER_FRAGMENT] = shader_key_fs_gen,
};

static struct zink_shader_module *
get_shader_module_for_stage(struct zink_context *ctx, struct zink_shader *zs, struct zink_gfx_program *prog)
{
   gl_shader_stage stage = zs->nir->info.stage;
   struct zink_shader_key key = {};
   VkShaderModule mod;
   struct zink_shader_module *zm;
   struct keybox *keybox;
   uint32_t hash;

   shader_key_vtbl[stage](ctx, zs, ctx->gfx_stages, &key);
   keybox = make_keybox(NULL, stage, &key, key.size);
   hash = keybox_hash(keybox);
   struct hash_entry *entry = _mesa_hash_table_search_pre_hashed(prog->shader_cache->shader_cache,
                                                                 hash, keybox);

   if (entry) {
      ralloc_free(keybox);
      zm = entry->data;
   } else {
      zm = CALLOC_STRUCT(zink_shader_module);
      if (!zm) {
         ralloc_free(keybox);
         return NULL;
      }
      pipe_reference_init(&zm->reference, 1);
      mod = zink_shader_compile(zink_screen(ctx->base.screen), zs, &key,
                                prog->shader_slot_map, &prog->shader_slots_reserved);
      if (!mod) {
         ralloc_free(keybox);
         FREE(zm);
         return NULL;
      }
      zm->shader = mod;

      _mesa_hash_table_insert_pre_hashed(prog->shader_cache->shader_cache, hash, keybox, zm);
   }
   return zm;
}

static void
zink_destroy_shader_module(struct zink_screen *screen, struct zink_shader_module *zm)
{
   vkDestroyShaderModule(screen->dev, zm->shader, NULL);
   free(zm);
}

static inline void
zink_shader_module_reference(struct zink_screen *screen,
                           struct zink_shader_module **dst,
                           struct zink_shader_module *src)
{
   struct zink_shader_module *old_dst = dst ? *dst : NULL;

   if (pipe_reference_described(old_dst ? &old_dst->reference : NULL, &src->reference,
                                (debug_reference_descriptor)debug_describe_zink_shader_module))
      zink_destroy_shader_module(screen, old_dst);
   if (dst) *dst = src;
}

static void
zink_destroy_shader_cache(struct zink_screen *screen, struct zink_shader_cache *sc)
{
   hash_table_foreach(sc->shader_cache, entry) {
      struct zink_shader_module *zm = entry->data;
      zink_shader_module_reference(screen, &zm, NULL);
   }
   _mesa_hash_table_destroy(sc->shader_cache, NULL);
   free(sc);
}

static inline void
zink_shader_cache_reference(struct zink_screen *screen,
                           struct zink_shader_cache **dst,
                           struct zink_shader_cache *src)
{
   struct zink_shader_cache *old_dst = dst ? *dst : NULL;

   if (pipe_reference_described(old_dst ? &old_dst->reference : NULL, &src->reference,
                                (debug_reference_descriptor)debug_describe_zink_shader_cache))
      zink_destroy_shader_cache(screen, old_dst);
   if (dst) *dst = src;
}

static void
update_shader_modules(struct zink_context *ctx, struct zink_shader *stages[ZINK_SHADER_COUNT], struct zink_gfx_program *prog, bool disallow_reuse)
{
   struct zink_shader *dirty[ZINK_SHADER_COUNT] = {NULL};

   /* we need to map pipe_shader_type -> gl_shader_stage so we can ensure that we're compiling
    * the shaders in pipeline order and have builtin input/output locations match up after being compacted
    */
   unsigned dirty_shader_stages = ctx->dirty_shader_stages;
   while (dirty_shader_stages) {
      unsigned type = u_bit_scan(&dirty_shader_stages);
      dirty[tgsi_processor_to_shader_stage(type)] = stages[type];
   }
   if (ctx->dirty_shader_stages & (1 << PIPE_SHADER_TESS_EVAL)) {
      if (dirty[MESA_SHADER_TESS_EVAL] && !dirty[MESA_SHADER_TESS_CTRL] &&
          !stages[PIPE_SHADER_TESS_CTRL]) {
         dirty[MESA_SHADER_TESS_CTRL] = stages[PIPE_SHADER_TESS_CTRL] = zink_shader_tcs_create(ctx, stages[PIPE_SHADER_VERTEX]);
         dirty[MESA_SHADER_TESS_EVAL]->generated = stages[PIPE_SHADER_TESS_CTRL];
      }
   }

   for (int i = 0; i < ZINK_SHADER_COUNT; ++i) {
      enum pipe_shader_type type = pipe_shader_type_from_mesa(i);
      if (dirty[i]) {
         struct zink_shader_module *zm;
         dirty[i]->has_geometry_shader = dirty[MESA_SHADER_GEOMETRY] || stages[PIPE_SHADER_GEOMETRY];
         dirty[i]->has_tess_shader = dirty[MESA_SHADER_TESS_EVAL] || stages[PIPE_SHADER_TESS_EVAL];
         zm = get_shader_module_for_stage(ctx, dirty[i], prog);
         zink_shader_module_reference(zink_screen(ctx->base.screen), &prog->modules[type], zm);
         /* we probably need a new pipeline when we switch shader modules */
         ctx->gfx_pipeline_state.dirty = true;
      } else if (stages[type] && !disallow_reuse) /* reuse existing shader module */
         zink_shader_module_reference(zink_screen(ctx->base.screen), &prog->modules[type], ctx->curr_program->modules[type]);
      prog->shaders[type] = stages[type];
   }
   ctx->dirty_shader_stages = 0;
}

static uint32_t
hash_gfx_pipeline_state(const void *key)
{
   return _mesa_hash_data(key, offsetof(struct zink_gfx_pipeline_state, hash));
}

static bool
equals_gfx_pipeline_state(const void *a, const void *b)
{
   return !memcmp(a, b, offsetof(struct zink_gfx_pipeline_state, hash));
}

static void
init_slot_map(struct zink_context *ctx, struct zink_gfx_program *prog)
{
   unsigned existing_shaders = 0;

   /* if there's a case where we'll be reusing any shaders, we need to reuse the slot map too */
   if (ctx->curr_program) {
      for (int i = 0; i < ZINK_SHADER_COUNT; ++i) {
          if (ctx->curr_program->shaders[i])
             existing_shaders |= 1 << i;
      }
   }
   if (ctx->dirty_shader_stages == existing_shaders || !existing_shaders) {
      /* all shaders are being recompiled: new slot map */
      memset(prog->shader_slot_map, -1, sizeof(prog->shader_slot_map));
      /* we need the slot map to match up, so we can't reuse the previous cache if we can't guarantee
       * the slots match up
       * TOOD: if we compact the slot map table, we can store it on the shader keys and reuse the cache
       */
      prog->shader_cache = CALLOC_STRUCT(zink_shader_cache);
      pipe_reference_init(&prog->shader_cache->reference, 1);
      prog->shader_cache->shader_cache =  _mesa_hash_table_create(NULL, keybox_hash, keybox_equals);
   } else {
      /* at least some shaders are being reused: use existing slot map so locations match up */
      memcpy(prog->shader_slot_map, ctx->curr_program->shader_slot_map, sizeof(prog->shader_slot_map));
      prog->shader_slots_reserved = ctx->curr_program->shader_slots_reserved;
      /* and then we can also reuse the shader cache since we know the slots are the same */
      zink_shader_cache_reference(zink_screen(ctx->base.screen), &prog->shader_cache, ctx->curr_program->shader_cache);
   }
}

void
zink_update_gfx_program(struct zink_context *ctx, struct zink_gfx_program *prog)
{
   update_shader_modules(ctx, ctx->gfx_stages, prog, true);
}

struct zink_gfx_program *
zink_create_gfx_program(struct zink_context *ctx,
                        struct zink_shader *stages[ZINK_SHADER_COUNT])
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_gfx_program *prog = CALLOC_STRUCT(zink_gfx_program);
   if (!prog)
      goto fail;

   pipe_reference_init(&prog->reference, 1);

   init_slot_map(ctx, prog);

   update_shader_modules(ctx, stages, prog, false);

   for (int i = 0; i < ARRAY_SIZE(prog->pipelines); ++i) {
      prog->pipelines[i] = _mesa_hash_table_create(NULL,
                                                   NULL,
                                                   equals_gfx_pipeline_state);
      if (!prog->pipelines[i])
         goto fail;
   }

   for (int i = 0; i < ZINK_SHADER_COUNT; ++i) {
      if (prog->modules[i]) {
         _mesa_set_add(stages[i]->programs, prog);
         zink_gfx_program_reference(screen, NULL, prog);
      }
   }

   prog->dsl = create_desc_set_layout(screen->dev, stages,
                                      &prog->num_descriptors);
   if (!prog->dsl)
      goto fail;

   prog->layout = create_pipeline_layout(screen->dev, prog->dsl);
   if (!prog->layout)
      goto fail;

   prog->render_passes = _mesa_set_create(NULL, _mesa_hash_pointer,
                                          _mesa_key_pointer_equal);
   if (!prog->render_passes)
      goto fail;

   return prog;

fail:
   if (prog)
      zink_destroy_gfx_program(screen, prog);
   return NULL;
}

static void
gfx_program_remove_shader(struct zink_gfx_program *prog, struct zink_shader *shader)
{
   enum pipe_shader_type p_stage = pipe_shader_type_from_mesa(shader->nir->info.stage);

   assert(prog->shaders[p_stage] == shader);
   prog->shaders[p_stage] = NULL;
   _mesa_set_remove_key(shader->programs, prog);
}

void
zink_destroy_gfx_program(struct zink_screen *screen,
                         struct zink_gfx_program *prog)
{
   if (prog->layout)
      vkDestroyPipelineLayout(screen->dev, prog->layout, NULL);

   if (prog->dsl)
      vkDestroyDescriptorSetLayout(screen->dev, prog->dsl, NULL);

   for (int i = 0; i < ZINK_SHADER_COUNT; ++i) {
      if (prog->shaders[i])
         gfx_program_remove_shader(prog, prog->shaders[i]);
      if (prog->modules[i])
         zink_shader_module_reference(screen, &prog->modules[i], NULL);
   }

   /* unref all used render-passes */
   if (prog->render_passes) {
      set_foreach(prog->render_passes, entry) {
         struct zink_render_pass *pres = (struct zink_render_pass *)entry->key;
         zink_render_pass_reference(screen, &pres, NULL);
      }
      _mesa_set_destroy(prog->render_passes, NULL);
   }

   for (int i = 0; i < ARRAY_SIZE(prog->pipelines); ++i) {
      hash_table_foreach(prog->pipelines[i], entry) {
         struct pipeline_cache_entry *pc_entry = entry->data;

         vkDestroyPipeline(screen->dev, pc_entry->pipeline, NULL);
         free(pc_entry);
      }
      _mesa_hash_table_destroy(prog->pipelines[i], NULL);
   }
   zink_shader_cache_reference(screen, &prog->shader_cache, NULL);

   FREE(prog);
}

static VkPrimitiveTopology
primitive_topology(enum pipe_prim_type mode)
{
   switch (mode) {
   case PIPE_PRIM_POINTS:
      return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

   case PIPE_PRIM_LINES:
      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

   case PIPE_PRIM_LINE_STRIP:
      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;

   case PIPE_PRIM_TRIANGLES:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

   case PIPE_PRIM_TRIANGLE_STRIP:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

   case PIPE_PRIM_TRIANGLE_FAN:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;

   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;

   case PIPE_PRIM_LINES_ADJACENCY:
      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;

   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;

   case PIPE_PRIM_TRIANGLES_ADJACENCY:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;

   case PIPE_PRIM_PATCHES:
      return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;

   default:
      unreachable("unexpected enum pipe_prim_type");
   }
}

static void
reference_render_pass(struct zink_screen *screen,
                      struct zink_gfx_program *prog,
                      struct zink_render_pass *render_pass)
{
   struct set_entry *entry = _mesa_set_search(prog->render_passes,
                                              render_pass);
   if (!entry) {
      entry = _mesa_set_add(prog->render_passes, render_pass);
      pipe_reference(NULL, &render_pass->reference);
   }
}

VkPipeline
zink_get_gfx_pipeline(struct zink_screen *screen,
                      struct zink_gfx_program *prog,
                      struct zink_gfx_pipeline_state *state,
                      enum pipe_prim_type mode)
{
   VkPrimitiveTopology vkmode = primitive_topology(mode);
   assert(vkmode <= ARRAY_SIZE(prog->pipelines));

   struct hash_entry *entry = NULL;
   
   if (state->dirty) {
      for (unsigned i = 0; i < ZINK_SHADER_COUNT; i++)
         state->modules[i] = prog->modules[i] ? prog->modules[i]->shader : VK_NULL_HANDLE;

      state->hash = hash_gfx_pipeline_state(state);
      state->dirty = false;
   }
   entry = _mesa_hash_table_search_pre_hashed(prog->pipelines[vkmode], state->hash, state);

   if (!entry) {
      VkPipeline pipeline = zink_create_gfx_pipeline(screen, prog,
                                                     state, vkmode);
      if (pipeline == VK_NULL_HANDLE)
         return VK_NULL_HANDLE;

      struct pipeline_cache_entry *pc_entry = CALLOC_STRUCT(pipeline_cache_entry);
      if (!pc_entry)
         return VK_NULL_HANDLE;

      memcpy(&pc_entry->state, state, sizeof(*state));
      pc_entry->pipeline = pipeline;

      entry = _mesa_hash_table_insert_pre_hashed(prog->pipelines[vkmode], state->hash, state, pc_entry);
      assert(entry);

      reference_render_pass(screen, prog, state->render_pass);
   }

   return ((struct pipeline_cache_entry *)(entry->data))->pipeline;
}


static void *
zink_create_vs_state(struct pipe_context *pctx,
                     const struct pipe_shader_state *shader)
{
   struct nir_shader *nir;
   if (shader->type != PIPE_SHADER_IR_NIR)
      nir = zink_tgsi_to_nir(pctx->screen, shader->tokens);
   else
      nir = (struct nir_shader *)shader->ir.nir;

   return zink_shader_create(zink_screen(pctx->screen), nir, &shader->stream_output);
}

static void
bind_stage(struct zink_context *ctx, enum pipe_shader_type stage,
           struct zink_shader *shader)
{
   assert(stage < PIPE_SHADER_COMPUTE);
   ctx->gfx_stages[stage] = shader;
   ctx->dirty_shader_stages |= 1 << stage;
}

static void
zink_bind_vs_state(struct pipe_context *pctx,
                   void *cso)
{
   bind_stage(zink_context(pctx), PIPE_SHADER_VERTEX, cso);
}

static void *
zink_create_fs_state(struct pipe_context *pctx,
                     const struct pipe_shader_state *shader)
{
   struct nir_shader *nir;
   if (shader->type != PIPE_SHADER_IR_NIR)
      nir = zink_tgsi_to_nir(pctx->screen, shader->tokens);
   else
      nir = (struct nir_shader *)shader->ir.nir;

   return zink_shader_create(zink_screen(pctx->screen), nir, NULL);
}

static void
zink_bind_fs_state(struct pipe_context *pctx,
                   void *cso)
{
   bind_stage(zink_context(pctx), PIPE_SHADER_FRAGMENT, cso);
}

static void *
zink_create_gs_state(struct pipe_context *pctx,
                     const struct pipe_shader_state *shader)
{
   struct nir_shader *nir;
   if (shader->type != PIPE_SHADER_IR_NIR)
      nir = zink_tgsi_to_nir(pctx->screen, shader->tokens);
   else
      nir = (struct nir_shader *)shader->ir.nir;

   return zink_shader_create(zink_screen(pctx->screen), nir, &shader->stream_output);
}

static void
zink_bind_gs_state(struct pipe_context *pctx,
                   void *cso)
{
   bind_stage(zink_context(pctx), PIPE_SHADER_GEOMETRY, cso);
}

static void *
zink_create_tcs_state(struct pipe_context *pctx,
                     const struct pipe_shader_state *shader)
{
   struct nir_shader *nir;
   if (shader->type != PIPE_SHADER_IR_NIR)
      nir = zink_tgsi_to_nir(pctx->screen, shader->tokens);
   else
      nir = (struct nir_shader *)shader->ir.nir;

   return zink_shader_create(zink_screen(pctx->screen), nir, &shader->stream_output);
}

static void
zink_bind_tcs_state(struct pipe_context *pctx,
                   void *cso)
{
   bind_stage(zink_context(pctx), PIPE_SHADER_TESS_CTRL, cso);
}

static void *
zink_create_tes_state(struct pipe_context *pctx,
                     const struct pipe_shader_state *shader)
{
   struct nir_shader *nir;
   if (shader->type != PIPE_SHADER_IR_NIR)
      nir = zink_tgsi_to_nir(pctx->screen, shader->tokens);
   else
      nir = (struct nir_shader *)shader->ir.nir;

   return zink_shader_create(zink_screen(pctx->screen), nir, NULL);
}

static void
zink_bind_tes_state(struct pipe_context *pctx,
                   void *cso)
{
   bind_stage(zink_context(pctx), PIPE_SHADER_TESS_EVAL, cso);
}

static void
zink_delete_shader_state(struct pipe_context *pctx, void *cso)
{
   zink_shader_free(zink_context(pctx), cso);
}


void
zink_program_init(struct zink_context *ctx)
{
   ctx->base.create_vs_state = zink_create_vs_state;
   ctx->base.bind_vs_state = zink_bind_vs_state;
   ctx->base.delete_vs_state = zink_delete_shader_state;

   ctx->base.create_fs_state = zink_create_fs_state;
   ctx->base.bind_fs_state = zink_bind_fs_state;
   ctx->base.delete_fs_state = zink_delete_shader_state;

   ctx->base.create_gs_state = zink_create_gs_state;
   ctx->base.bind_gs_state = zink_bind_gs_state;
   ctx->base.delete_gs_state = zink_delete_shader_state;

   ctx->base.create_tcs_state = zink_create_tcs_state;
   ctx->base.bind_tcs_state = zink_bind_tcs_state;
   ctx->base.delete_tcs_state = zink_delete_shader_state;

   ctx->base.create_tes_state = zink_create_tes_state;
   ctx->base.bind_tes_state = zink_bind_tes_state;
   ctx->base.delete_tes_state = zink_delete_shader_state;
}
