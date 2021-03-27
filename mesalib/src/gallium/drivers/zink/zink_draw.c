#include "zink_compiler.h"
#include "zink_context.h"
#include "zink_program.h"
#include "zink_query.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "zink_state.h"
#include "zink_surface.h"

#include "indices/u_primconvert.h"
#include "tgsi/tgsi_from_mesa.h"
#include "util/hash_table.h"
#include "util/u_debug.h"
#include "util/u_helpers.h"
#include "util/u_inlines.h"
#include "util/u_prim.h"
#include "util/u_prim_restart.h"


static void
desc_set_res_add(struct zink_descriptor_set *zds, struct zink_resource *res, unsigned int i, bool cache_hit)
{
   /* if we got a cache hit, we have to verify that the cached set is still valid;
    * we store the vk resource to the set here to avoid a more complex and costly mechanism of maintaining a
    * hash table on every resource with the associated descriptor sets that then needs to be iterated through
    * whenever a resource is destroyed
    */
   assert(!cache_hit || zds->res_objs[i] == (res ? res->obj : NULL));
   if (!cache_hit)
      zink_resource_desc_set_add(res, zds, i);
}

static void
desc_set_sampler_add(struct zink_context *ctx, struct zink_descriptor_set *zds, struct zink_sampler_view *sv,
                     struct zink_sampler_state *state, unsigned int i, bool is_buffer, bool cache_hit)
{
   /* if we got a cache hit, we have to verify that the cached set is still valid;
    * we store the vk resource to the set here to avoid a more complex and costly mechanism of maintaining a
    * hash table on every resource with the associated descriptor sets that then needs to be iterated through
    * whenever a resource is destroyed
    */
#ifndef NDEBUG
   uint32_t cur_hash = zink_get_sampler_view_hash(ctx, zds->sampler_views[i], is_buffer);
   uint32_t new_hash = zink_get_sampler_view_hash(ctx, sv, is_buffer);
#endif
   assert(!cache_hit || cur_hash == new_hash);
   assert(!cache_hit || zds->sampler_states[i] == state);
   if (!cache_hit) {
      zink_sampler_view_desc_set_add(sv, zds, i);
      zink_sampler_state_desc_set_add(state, zds, i);
   }
}

static void
desc_set_image_add(struct zink_context *ctx, struct zink_descriptor_set *zds, struct zink_image_view *image_view,
                   unsigned int i, bool is_buffer, bool cache_hit)
{
   /* if we got a cache hit, we have to verify that the cached set is still valid;
    * we store the vk resource to the set here to avoid a more complex and costly mechanism of maintaining a
    * hash table on every resource with the associated descriptor sets that then needs to be iterated through
    * whenever a resource is destroyed
    */
#ifndef NDEBUG
   uint32_t cur_hash = zink_get_image_view_hash(ctx, zds->image_views[i], is_buffer);
   uint32_t new_hash = zink_get_image_view_hash(ctx, image_view, is_buffer);
#endif
   assert(!cache_hit || cur_hash == new_hash);
   if (!cache_hit)
      zink_image_view_desc_set_add(image_view, zds, i);
}

static void
zink_emit_xfb_counter_barrier(struct zink_context *ctx)
{
   /* Between the pause and resume there needs to be a memory barrier for the counter buffers
    * with a source access of VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT
    * at pipeline stage VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT
    * to a destination access of VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT
    * at pipeline stage VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT.
    *
    * - from VK_EXT_transform_feedback spec
    */
   for (unsigned i = 0; i < ctx->num_so_targets; i++) {
      struct zink_so_target *t = zink_so_target(ctx->so_targets[i]);
      if (!t)
         continue;
      struct zink_resource *res = zink_resource(t->counter_buffer);
      if (t->counter_buffer_valid)
          zink_resource_buffer_barrier(ctx, NULL, res, VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT,
                                       VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
      else
          zink_resource_buffer_barrier(ctx, NULL, res, VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT,
                                       VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT);
   }
   ctx->xfb_barrier = false;
}

static void
zink_emit_xfb_vertex_input_barrier(struct zink_context *ctx, struct zink_resource *res)
{
   /* A pipeline barrier is required between using the buffers as
    * transform feedback buffers and vertex buffers to
    * ensure all writes to the transform feedback buffers are visible
    * when the data is read as vertex attributes.
    * The source access is VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT
    * and the destination access is VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
    * for the pipeline stages VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT
    * and VK_PIPELINE_STAGE_VERTEX_INPUT_BIT respectively.
    *
    * - 20.3.1. Drawing Transform Feedback
    */
   zink_resource_buffer_barrier(ctx, NULL, res, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
}

static void
zink_emit_stream_output_targets(struct pipe_context *pctx)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_batch *batch = &ctx->batch;
   VkBuffer buffers[PIPE_MAX_SO_OUTPUTS] = {};
   VkDeviceSize buffer_offsets[PIPE_MAX_SO_OUTPUTS] = {};
   VkDeviceSize buffer_sizes[PIPE_MAX_SO_OUTPUTS] = {};

   for (unsigned i = 0; i < ctx->num_so_targets; i++) {
      struct zink_so_target *t = (struct zink_so_target *)ctx->so_targets[i];
      if (!t) {
         /* no need to reference this or anything */
         buffers[i] = zink_resource(ctx->dummy_xfb_buffer)->obj->buffer;
         buffer_offsets[i] = 0;
         buffer_sizes[i] = sizeof(uint8_t);
         continue;
      }
      struct zink_resource *res = zink_resource(t->base.buffer);
      if (!(res->bind_history & ZINK_RESOURCE_USAGE_STREAMOUT))
         /* resource has been rebound */
         t->counter_buffer_valid = false;
      buffers[i] = res->obj->buffer;
      zink_resource_buffer_barrier(ctx, NULL, zink_resource(t->base.buffer),
                                   VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT);
      zink_batch_reference_resource_rw(batch, res, true);
      buffer_offsets[i] = t->base.buffer_offset;
      buffer_sizes[i] = t->base.buffer_size;
      res->bind_history |= ZINK_RESOURCE_USAGE_STREAMOUT;
      util_range_add(t->base.buffer, &res->valid_buffer_range, t->base.buffer_offset,
                     t->base.buffer_offset + t->base.buffer_size);
   }

   screen->vk_CmdBindTransformFeedbackBuffersEXT(batch->state->cmdbuf, 0, ctx->num_so_targets,
                                                 buffers, buffer_offsets,
                                                 buffer_sizes);
   ctx->dirty_so_targets = false;
}

static void
barrier_vertex_buffers(struct zink_context *ctx)
{
   const struct zink_vertex_elements_state *elems = ctx->element_state;
   for (unsigned i = 0; i < elems->hw_state.num_bindings; i++) {
      struct pipe_vertex_buffer *vb = ctx->vertex_buffers + ctx->element_state->binding_map[i];
      assert(vb);
      if (vb->buffer.resource) {
         struct zink_resource *res = zink_resource(vb->buffer.resource);
         zink_resource_buffer_barrier(ctx, NULL, res, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                                      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
      }
   }
}

static void
check_buffer_barrier(struct zink_context *ctx, struct pipe_resource *pres, VkAccessFlags flags, VkPipelineStageFlags pipeline)
{
   struct zink_resource *res = zink_resource(pres);
   zink_resource_buffer_barrier(ctx, NULL, res, flags, pipeline);
}

static void
barrier_draw_buffers(struct zink_context *ctx, const struct pipe_draw_info *dinfo,
                     const struct pipe_draw_indirect_info *dindirect, struct pipe_resource *index_buffer)
{
   if (index_buffer)
      check_buffer_barrier(ctx, index_buffer, VK_ACCESS_INDEX_READ_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
   if (dindirect && dindirect->buffer) {
      check_buffer_barrier(ctx, dindirect->buffer,
                           VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
      if (dindirect->indirect_draw_count)
         check_buffer_barrier(ctx, dindirect->indirect_draw_count,
                              VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
   }
}

static void
zink_bind_vertex_buffers(struct zink_batch *batch, struct zink_context *ctx)
{
   VkBuffer buffers[PIPE_MAX_ATTRIBS];
   VkDeviceSize buffer_offsets[PIPE_MAX_ATTRIBS];
   VkDeviceSize buffer_strides[PIPE_MAX_ATTRIBS];
   const struct zink_vertex_elements_state *elems = ctx->element_state;
   struct zink_screen *screen = zink_screen(ctx->base.screen);

   if (!elems->hw_state.num_bindings)
      return;

   for (unsigned i = 0; i < elems->hw_state.num_bindings; i++) {
      struct pipe_vertex_buffer *vb = ctx->vertex_buffers + ctx->element_state->binding_map[i];
      assert(vb);
      if (vb->buffer.resource) {
         struct zink_resource *res = zink_resource(vb->buffer.resource);
         buffers[i] = res->obj->buffer;
         buffer_offsets[i] = vb->buffer_offset;
         buffer_strides[i] = vb->stride;
         zink_batch_reference_resource_rw(batch, res, false);
      } else {
         buffers[i] = zink_resource(ctx->dummy_vertex_buffer)->obj->buffer;
         buffer_offsets[i] = 0;
         buffer_strides[i] = 0;
      }
   }

   if (screen->info.have_EXT_extended_dynamic_state)
      screen->vk_CmdBindVertexBuffers2EXT(batch->state->cmdbuf, 0,
                                          elems->hw_state.num_bindings,
                                          buffers, buffer_offsets, NULL, buffer_strides);
   else
      vkCmdBindVertexBuffers(batch->state->cmdbuf, 0,
                             elems->hw_state.num_bindings,
                             buffers, buffer_offsets);
}

static struct zink_compute_program *
get_compute_program(struct zink_context *ctx)
{
   if (ctx->dirty_shader_stages) {
      struct hash_entry *entry = _mesa_hash_table_search(ctx->compute_program_cache,
                                                         &ctx->compute_stage->shader_id);
      if (!entry) {
         struct zink_compute_program *comp;
         comp = zink_create_compute_program(ctx, ctx->compute_stage);
         entry = _mesa_hash_table_insert(ctx->compute_program_cache, &comp->shader->shader_id, comp);
         if (!entry)
            return NULL;
      }
      if (entry->data != ctx->curr_compute)
         ctx->compute_pipeline_state.dirty = true;
      ctx->curr_compute = entry->data;
      ctx->dirty_shader_stages &= (1 << PIPE_SHADER_COMPUTE);
   }

   assert(ctx->curr_compute);
   return ctx->curr_compute;
}

static struct zink_gfx_program *
get_gfx_program(struct zink_context *ctx)
{
   if (ctx->last_vertex_stage_dirty) {
      if (ctx->gfx_stages[PIPE_SHADER_GEOMETRY])
         ctx->dirty_shader_stages |= BITFIELD_BIT(PIPE_SHADER_GEOMETRY);
      else if (ctx->gfx_stages[PIPE_SHADER_TESS_EVAL])
         ctx->dirty_shader_stages |= BITFIELD_BIT(PIPE_SHADER_TESS_EVAL);
      else
         ctx->dirty_shader_stages |= BITFIELD_BIT(PIPE_SHADER_VERTEX);
      ctx->last_vertex_stage_dirty = false;
   }
   if (ctx->dirty_shader_stages) {
      struct hash_entry *entry = _mesa_hash_table_search(ctx->program_cache,
                                                         ctx->gfx_stages);
      if (entry)
         zink_update_gfx_program(ctx, entry->data);
      else {
         struct zink_gfx_program *prog;
         prog = zink_create_gfx_program(ctx, ctx->gfx_stages);
         entry = _mesa_hash_table_insert(ctx->program_cache, prog->shaders, prog);
         if (!entry)
            return NULL;
      }
      if (ctx->curr_program != entry->data)
         ctx->gfx_pipeline_state.combined_dirty = true;
      ctx->curr_program = entry->data;
      unsigned bits = u_bit_consecutive(PIPE_SHADER_VERTEX, 5);
      ctx->dirty_shader_stages &= ~bits;
   }

   assert(ctx->curr_program);
   return ctx->curr_program;
}

#define MAX_DESCRIPTORS (PIPE_SHADER_TYPES * (PIPE_MAX_CONSTANT_BUFFERS + PIPE_MAX_SAMPLERS + PIPE_MAX_SHADER_BUFFERS + PIPE_MAX_SHADER_IMAGES))

static bool
barrier_equals(const void *a, const void *b)
{
   const struct zink_descriptor_barrier *t1 = a, *t2 = b;
   if (t1->res != t2->res)
      return false;
   if ((t1->access & t2->access) != t2->access)
      return false;
   if (t1->layout != t2->layout)
      return false;
   return true;
}

static uint32_t
barrier_hash(const void *key)
{
   return _mesa_hash_data(key, offsetof(struct zink_descriptor_barrier, stage));
}

static inline void
add_barrier(struct zink_resource *res, VkImageLayout layout, VkAccessFlags flags, enum pipe_shader_type stage, struct util_dynarray *barriers, struct set *ht)
{
   VkPipelineStageFlags pipeline = zink_pipeline_flags_from_stage(zink_shader_stage(stage));
   struct zink_descriptor_barrier key = {res, layout, flags, 0}, *t;

   uint32_t hash = barrier_hash(&key);
   struct set_entry *entry = _mesa_set_search_pre_hashed(ht, hash, &key);
   if (entry)
      t = (struct zink_descriptor_barrier*)entry->key;
   else {
      util_dynarray_append(barriers, struct zink_descriptor_barrier, key);
      t = util_dynarray_element(barriers, struct zink_descriptor_barrier,
                                util_dynarray_num_elements(barriers, struct zink_descriptor_barrier) - 1);
      t->stage = 0;
      t->layout = layout;
      t->res = res;
      t->access = flags;
      _mesa_set_add_pre_hashed(ht, hash, t);
   }
   t->stage |= pipeline;
}

static int
cmp_dynamic_offset_binding(const void *a, const void *b)
{
   const uint32_t *binding_a = a, *binding_b = b;
   return *binding_a - *binding_b;
}

static void
write_descriptors(struct zink_context *ctx, struct zink_descriptor_set *zds, unsigned num_wds, VkWriteDescriptorSet *wds,
                 bool cache_hit, bool need_resource_refs)
{
   struct zink_batch *batch = &ctx->batch;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   assert(zds->desc_set);

   if (!cache_hit && num_wds)
      vkUpdateDescriptorSets(screen->dev, num_wds, wds, 0, NULL);

   for (int i = 0; zds->pool->key.num_descriptors && i < util_dynarray_num_elements(&zds->barriers, struct zink_descriptor_barrier); ++i) {
      struct zink_descriptor_barrier *barrier = util_dynarray_element(&zds->barriers, struct zink_descriptor_barrier, i);
      if (need_resource_refs)
         zink_batch_reference_resource_rw(batch, barrier->res, zink_resource_access_is_write(barrier->access));
      zink_resource_barrier(ctx, NULL, barrier->res,
                            barrier->layout, barrier->access, barrier->stage);
   }
}

static unsigned
init_write_descriptor(struct zink_shader *shader, struct zink_descriptor_set *zds, int idx, VkWriteDescriptorSet *wd, unsigned num_wds)
{
    wd->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wd->pNext = NULL;
    wd->dstBinding = shader->bindings[zds->pool->type][idx].binding;
    wd->dstArrayElement = 0;
    wd->descriptorCount = shader->bindings[zds->pool->type][idx].size;
    wd->descriptorType = shader->bindings[zds->pool->type][idx].type;
    wd->dstSet = zds->desc_set;
    return num_wds + 1;
}

static void
update_ubo_descriptors(struct zink_context *ctx, struct zink_descriptor_set *zds,
                       bool is_compute, bool cache_hit, bool need_resource_refs,
                       uint32_t *dynamic_offsets, unsigned *dynamic_offset_idx)
{
   struct zink_program *pg = is_compute ? (struct zink_program *)ctx->curr_compute : (struct zink_program *)ctx->curr_program;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   unsigned num_descriptors = pg->pool[zds->pool->type]->key.num_descriptors;
   unsigned num_bindings = zds->pool->num_resources;
   VkWriteDescriptorSet wds[num_descriptors];
   VkDescriptorBufferInfo buffer_infos[num_bindings];
   unsigned num_wds = 0;
   unsigned num_buffer_info = 0;
   unsigned num_resources = 0;
   struct zink_shader **stages;
   struct {
      uint32_t binding;
      uint32_t offset;
   } dynamic_buffers[PIPE_MAX_CONSTANT_BUFFERS];
   unsigned dynamic_offset_count = 0;
   struct set *ht = NULL;
   if (!cache_hit) {
      ht = _mesa_set_create(NULL, barrier_hash, barrier_equals);
      _mesa_set_resize(ht, num_bindings);
   }

   unsigned num_stages = is_compute ? 1 : ZINK_SHADER_COUNT;
   if (is_compute)
      stages = &ctx->curr_compute->shader;
   else
      stages = &ctx->gfx_stages[0];

   for (int i = 0; i < num_stages; i++) {
      struct zink_shader *shader = stages[i];
      if (!shader)
         continue;
      enum pipe_shader_type stage = pipe_shader_type_from_mesa(shader->nir->info.stage);

      for (int j = 0; j < shader->num_bindings[zds->pool->type]; j++) {
         int index = shader->bindings[zds->pool->type][j].index;
         assert(shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
             shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
         assert(ctx->ubos[stage][index].buffer_size <= screen->info.props.limits.maxUniformBufferRange);
         struct zink_resource *res = zink_resource(ctx->ubos[stage][index].buffer);
         assert(!res || ctx->ubos[stage][index].buffer_size > 0);
         assert(!res || ctx->ubos[stage][index].buffer);
         assert(num_resources < num_bindings);
         desc_set_res_add(zds, res, num_resources++, cache_hit);
         assert(num_buffer_info < num_bindings);
         buffer_infos[num_buffer_info].buffer = res ? res->obj->buffer :
                                                (screen->info.rb2_feats.nullDescriptor ?
                                                 VK_NULL_HANDLE :
                                                 zink_resource(ctx->dummy_vertex_buffer)->obj->buffer);
         if (shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
            buffer_infos[num_buffer_info].offset = 0;
            /* we're storing this to qsort later */
            dynamic_buffers[dynamic_offset_count].binding = shader->bindings[zds->pool->type][j].binding;
            dynamic_buffers[dynamic_offset_count++].offset = res ? ctx->ubos[stage][index].buffer_offset : 0;
         } else
            buffer_infos[num_buffer_info].offset = res ? ctx->ubos[stage][index].buffer_offset : 0;
         buffer_infos[num_buffer_info].range = res ? ctx->ubos[stage][index].buffer_size : VK_WHOLE_SIZE;
         if (res && !cache_hit)
            add_barrier(res, 0, VK_ACCESS_UNIFORM_READ_BIT, stage, &zds->barriers, ht);
         wds[num_wds].pBufferInfo = buffer_infos + num_buffer_info;
         ++num_buffer_info;

         num_wds = init_write_descriptor(shader, zds, j, &wds[num_wds], num_wds);
      }
   }
   _mesa_set_destroy(ht, NULL);
   /* Values are taken from pDynamicOffsets in an order such that all entries for set N come before set N+1;
    * within a set, entries are ordered by the binding numbers in the descriptor set layouts
    * - vkCmdBindDescriptorSets spec
    *
    * because of this, we have to sort all the dynamic offsets by their associated binding to ensure they
    * match what the driver expects
    */
   if (dynamic_offset_count > 1)
      qsort(dynamic_buffers, dynamic_offset_count, sizeof(uint32_t) * 2, cmp_dynamic_offset_binding);
   for (int i = 0; i < dynamic_offset_count; i++)
      dynamic_offsets[i] = dynamic_buffers[i].offset;
   *dynamic_offset_idx = dynamic_offset_count;

   write_descriptors(ctx, zds, num_wds, wds, cache_hit, need_resource_refs);
}

static void
update_ssbo_descriptors(struct zink_context *ctx, struct zink_descriptor_set *zds,
                        bool is_compute, bool cache_hit, bool need_resource_refs)
{
   struct zink_program *pg = is_compute ? (struct zink_program *)ctx->curr_compute : (struct zink_program *)ctx->curr_program;
   ASSERTED struct zink_screen *screen = zink_screen(ctx->base.screen);
   unsigned num_descriptors = pg->pool[zds->pool->type]->key.num_descriptors;
   unsigned num_bindings = zds->pool->num_resources;
   VkWriteDescriptorSet wds[num_descriptors];
   VkDescriptorBufferInfo buffer_infos[num_bindings];
   unsigned num_wds = 0;
   unsigned num_buffer_info = 0;
   unsigned num_resources = 0;
   struct zink_shader **stages;
   struct set *ht = NULL;
   if (!cache_hit) {
      ht = _mesa_set_create(NULL, barrier_hash, barrier_equals);
      _mesa_set_resize(ht, num_bindings);
   }

   unsigned num_stages = is_compute ? 1 : ZINK_SHADER_COUNT;
   if (is_compute)
      stages = &ctx->curr_compute->shader;
   else
      stages = &ctx->gfx_stages[0];

   for (int i = 0; (!cache_hit || need_resource_refs) && i < num_stages; i++) {
      struct zink_shader *shader = stages[i];
      if (!shader)
         continue;
      enum pipe_shader_type stage = pipe_shader_type_from_mesa(shader->nir->info.stage);

      for (int j = 0; j < shader->num_bindings[zds->pool->type]; j++) {
         int index = shader->bindings[zds->pool->type][j].index;
         assert(shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
         assert(num_resources < num_bindings);
         struct zink_resource *res = zink_resource(ctx->ssbos[stage][index].buffer);
         desc_set_res_add(zds, res, num_resources++, cache_hit);
         if (res) {
            assert(ctx->ssbos[stage][index].buffer_size > 0);
            assert(ctx->ssbos[stage][index].buffer_size <= screen->info.props.limits.maxStorageBufferRange);
            assert(num_buffer_info < num_bindings);
            unsigned flag = VK_ACCESS_SHADER_READ_BIT;
            if (ctx->writable_ssbos[stage] & (1 << index))
               flag |= VK_ACCESS_SHADER_WRITE_BIT;
            if (!cache_hit)
               add_barrier(res, 0, flag, stage, &zds->barriers, ht);
            buffer_infos[num_buffer_info].buffer = res->obj->buffer;
            buffer_infos[num_buffer_info].offset = ctx->ssbos[stage][index].buffer_offset;
            buffer_infos[num_buffer_info].range  = ctx->ssbos[stage][index].buffer_size;
         } else {
            assert(screen->info.rb2_feats.nullDescriptor);
            buffer_infos[num_buffer_info].buffer = VK_NULL_HANDLE;
            buffer_infos[num_buffer_info].offset = 0;
            buffer_infos[num_buffer_info].range  = VK_WHOLE_SIZE;
         }
         wds[num_wds].pBufferInfo = buffer_infos + num_buffer_info;
         ++num_buffer_info;

         num_wds = init_write_descriptor(shader, zds, j, &wds[num_wds], num_wds);
      }
   }
   _mesa_set_destroy(ht, NULL);
   write_descriptors(ctx, zds, num_wds, wds, cache_hit, need_resource_refs);
}

static void
handle_image_descriptor(struct zink_screen *screen, struct zink_resource *res, enum zink_descriptor_type type, VkDescriptorType vktype, VkWriteDescriptorSet *wd,
                        VkImageLayout layout, unsigned *num_image_info, VkDescriptorImageInfo *image_info,
                        unsigned *num_buffer_info, VkBufferView *buffer_info,
                        struct zink_sampler_state *sampler,
                        VkImageView imageview, VkBufferView bufferview, bool do_set)
{
    if (!res) {
        /* if we're hitting this assert often, we can probably just throw a junk buffer in since
         * the results of this codepath are undefined in ARB_texture_buffer_object spec
         */
        assert(screen->info.rb2_feats.nullDescriptor);
        
        switch (vktype) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
           *buffer_info = VK_NULL_HANDLE;
           if (do_set)
              wd->pTexelBufferView = buffer_info;
           ++(*num_buffer_info);
           break;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
           image_info->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
           image_info->imageView = VK_NULL_HANDLE;
           image_info->sampler = sampler ? sampler->sampler : VK_NULL_HANDLE;
           if (do_set)
              wd->pImageInfo = image_info;
           ++(*num_image_info);
           break;
        default:
           unreachable("unknown descriptor type");
        }
     } else if (res->base.target != PIPE_BUFFER) {
        assert(layout != VK_IMAGE_LAYOUT_UNDEFINED);
        image_info->imageLayout = layout;
        image_info->imageView = imageview;
        image_info->sampler = sampler ? sampler->sampler : VK_NULL_HANDLE;
        if (do_set)
           wd->pImageInfo = image_info;
        ++(*num_image_info);
     } else {
        if (do_set)
           wd->pTexelBufferView = buffer_info;
        *buffer_info = bufferview;
        ++(*num_buffer_info);
     }
}

static void
update_sampler_descriptors(struct zink_context *ctx, struct zink_descriptor_set *zds,
                           bool is_compute, bool cache_hit, bool need_resource_refs)
{
   struct zink_program *pg = is_compute ? (struct zink_program *)ctx->curr_compute : (struct zink_program *)ctx->curr_program;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   unsigned num_descriptors = pg->pool[zds->pool->type]->key.num_descriptors;
   unsigned num_bindings = zds->pool->num_resources;
   VkWriteDescriptorSet wds[num_descriptors];
   VkDescriptorImageInfo image_infos[num_bindings];
   VkBufferView buffer_views[num_bindings];
   unsigned num_wds = 0;
   unsigned num_image_info = 0;
   unsigned num_buffer_info = 0;
   unsigned num_resources = 0;
   struct zink_shader **stages;
   struct set *ht = NULL;
   if (!cache_hit) {
      ht = _mesa_set_create(NULL, barrier_hash, barrier_equals);
      _mesa_set_resize(ht, num_bindings);
   }

   unsigned num_stages = is_compute ? 1 : ZINK_SHADER_COUNT;
   if (is_compute)
      stages = &ctx->curr_compute->shader;
   else
      stages = &ctx->gfx_stages[0];

   for (int i = 0; (!cache_hit || need_resource_refs) && i < num_stages; i++) {
      struct zink_shader *shader = stages[i];
      if (!shader)
         continue;
      enum pipe_shader_type stage = pipe_shader_type_from_mesa(shader->nir->info.stage);

      for (int j = 0; j < shader->num_bindings[zds->pool->type]; j++) {
         int index = shader->bindings[zds->pool->type][j].index;
         assert(shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
                shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

         for (unsigned k = 0; k < shader->bindings[zds->pool->type][j].size; k++) {
            VkImageView imageview = VK_NULL_HANDLE;
            VkBufferView bufferview = VK_NULL_HANDLE;
            struct zink_resource *res = NULL;
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
            struct zink_sampler_state *sampler = NULL;

            struct pipe_sampler_view *psampler_view = ctx->sampler_views[stage][index + k];
            struct zink_sampler_view *sampler_view = zink_sampler_view(psampler_view);
            res = psampler_view ? zink_resource(psampler_view->texture) : NULL;
            if (res && res->base.target == PIPE_BUFFER) {
               bufferview = sampler_view->buffer_view->buffer_view;
            } else if (res) {
               imageview = sampler_view->image_view->image_view;
               layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
               sampler = ctx->sampler_states[stage][index + k];
            }
            assert(num_resources < num_bindings);
            if (res) {
               if (!cache_hit)
                  add_barrier(res, layout, VK_ACCESS_SHADER_READ_BIT, stage, &zds->barriers, ht);
            }
            assert(num_image_info < num_bindings);
            handle_image_descriptor(screen, res, zds->pool->type, shader->bindings[zds->pool->type][j].type,
                                    &wds[num_wds], layout, &num_image_info, &image_infos[num_image_info],
                                    &num_buffer_info, &buffer_views[num_buffer_info],
                                    sampler, imageview, bufferview, !k);
            desc_set_sampler_add(ctx, zds, sampler_view, sampler, num_resources++,
                                 zink_shader_descriptor_is_buffer(shader, ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW, j),
                                 cache_hit);
            struct zink_batch *batch = &ctx->batch;
            if (sampler_view)
               zink_batch_reference_sampler_view(batch, sampler_view);
            if (sampler)
               /* this only tracks the most recent usage for now */
               zink_batch_usage_set(&sampler->batch_uses, batch->state->fence.batch_id);
         }
         assert(num_wds < num_descriptors);

         num_wds = init_write_descriptor(shader, zds, j, &wds[num_wds], num_wds);
      }
   }
   _mesa_set_destroy(ht, NULL);
   write_descriptors(ctx, zds, num_wds, wds, cache_hit, need_resource_refs);
}

static void
update_image_descriptors(struct zink_context *ctx, struct zink_descriptor_set *zds,
                         bool is_compute, bool cache_hit, bool need_resource_refs)
{
   struct zink_program *pg = is_compute ? (struct zink_program *)ctx->curr_compute : (struct zink_program *)ctx->curr_program;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   unsigned num_descriptors = pg->pool[zds->pool->type]->key.num_descriptors;
   unsigned num_bindings = zds->pool->num_resources;
   VkWriteDescriptorSet wds[num_descriptors];
   VkDescriptorImageInfo image_infos[num_bindings];
   VkBufferView buffer_views[num_bindings];
   unsigned num_wds = 0;
   unsigned num_image_info = 0;
   unsigned num_buffer_info = 0;
   unsigned num_resources = 0;
   struct zink_shader **stages;
   struct set *ht = NULL;
   if (!cache_hit) {
      ht = _mesa_set_create(NULL, barrier_hash, barrier_equals);
      _mesa_set_resize(ht, num_bindings);
   }

   unsigned num_stages = is_compute ? 1 : ZINK_SHADER_COUNT;
   if (is_compute)
      stages = &ctx->curr_compute->shader;
   else
      stages = &ctx->gfx_stages[0];

   for (int i = 0; (!cache_hit || need_resource_refs) && i < num_stages; i++) {
      struct zink_shader *shader = stages[i];
      if (!shader)
         continue;
      enum pipe_shader_type stage = pipe_shader_type_from_mesa(shader->nir->info.stage);

      for (int j = 0; j < shader->num_bindings[zds->pool->type]; j++) {
         int index = shader->bindings[zds->pool->type][j].index;
         assert(shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER ||
                shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

         for (unsigned k = 0; k < shader->bindings[zds->pool->type][j].size; k++) {
            VkImageView imageview = VK_NULL_HANDLE;
            VkBufferView bufferview = VK_NULL_HANDLE;
            struct zink_resource *res = NULL;
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
            struct zink_image_view *image_view = &ctx->image_views[stage][index + k];
            assert(image_view);
            res = zink_resource(image_view->base.resource);

            if (res && image_view->base.resource->target == PIPE_BUFFER) {
               bufferview = image_view->buffer_view->buffer_view;
            } else if (res) {
               imageview = image_view->surface->image_view;
               layout = VK_IMAGE_LAYOUT_GENERAL;
            }
            assert(num_resources < num_bindings);
            desc_set_image_add(ctx, zds, image_view, num_resources++,
                               zink_shader_descriptor_is_buffer(shader, ZINK_DESCRIPTOR_TYPE_IMAGE, j),
                               cache_hit);
            if (res) {
               VkAccessFlags flags = 0;
               if (image_view->base.access & PIPE_IMAGE_ACCESS_READ)
                  flags |= VK_ACCESS_SHADER_READ_BIT;
               if (image_view->base.access & PIPE_IMAGE_ACCESS_WRITE)
                  flags |= VK_ACCESS_SHADER_WRITE_BIT;
               if (!cache_hit)
                  add_barrier(res, layout, flags, stage, &zds->barriers, ht);
            }

            assert(num_image_info < num_bindings);
            handle_image_descriptor(screen, res, zds->pool->type, shader->bindings[zds->pool->type][j].type,
                                    &wds[num_wds], layout, &num_image_info, &image_infos[num_image_info],
                                    &num_buffer_info, &buffer_views[num_buffer_info],
                                    NULL, imageview, bufferview, !k);

            struct zink_batch *batch = &ctx->batch;
            if (res)
               zink_batch_reference_image_view(batch, image_view);
         }
         assert(num_wds < num_descriptors);

         num_wds = init_write_descriptor(shader, zds, j, &wds[num_wds], num_wds);
      }
   }
   _mesa_set_destroy(ht, NULL);
   write_descriptors(ctx, zds, num_wds, wds, cache_hit, need_resource_refs);
}

static void
update_descriptors(struct zink_context *ctx, struct zink_screen *screen, bool is_compute)
{
   struct zink_program *pg = is_compute ? (struct zink_program *)ctx->curr_compute : (struct zink_program *)ctx->curr_program;

   zink_context_update_descriptor_states(ctx, is_compute);
   bool cache_hit[ZINK_DESCRIPTOR_TYPES];
   bool need_resource_refs[ZINK_DESCRIPTOR_TYPES];
   struct zink_descriptor_set *zds[ZINK_DESCRIPTOR_TYPES];
   for (int h = 0; h < ZINK_DESCRIPTOR_TYPES; h++) {
      if (pg->pool[h])
         zds[h] = zink_descriptor_set_get(ctx, h, is_compute, &cache_hit[h], &need_resource_refs[h]);
      else
         zds[h] = NULL;
   }
   struct zink_batch *batch = &ctx->batch;
   zink_batch_reference_program(batch, pg);

   uint32_t dynamic_offsets[PIPE_MAX_CONSTANT_BUFFERS];
   unsigned dynamic_offset_idx = 0;

   if (zds[ZINK_DESCRIPTOR_TYPE_UBO])
      update_ubo_descriptors(ctx, zds[ZINK_DESCRIPTOR_TYPE_UBO],
                                           is_compute, cache_hit[ZINK_DESCRIPTOR_TYPE_UBO],
                                           need_resource_refs[ZINK_DESCRIPTOR_TYPE_UBO], dynamic_offsets, &dynamic_offset_idx);
   if (zds[ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW])
      update_sampler_descriptors(ctx, zds[ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW],
                                               is_compute, cache_hit[ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW],
                                               need_resource_refs[ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW]);
   if (zds[ZINK_DESCRIPTOR_TYPE_SSBO])
      update_ssbo_descriptors(ctx, zds[ZINK_DESCRIPTOR_TYPE_SSBO],
                                               is_compute, cache_hit[ZINK_DESCRIPTOR_TYPE_SSBO],
                                               need_resource_refs[ZINK_DESCRIPTOR_TYPE_SSBO]);
   if (zds[ZINK_DESCRIPTOR_TYPE_IMAGE])
      update_image_descriptors(ctx, zds[ZINK_DESCRIPTOR_TYPE_IMAGE],
                                               is_compute, cache_hit[ZINK_DESCRIPTOR_TYPE_IMAGE],
                                               need_resource_refs[ZINK_DESCRIPTOR_TYPE_IMAGE]);

   for (unsigned h = 0; h < ZINK_DESCRIPTOR_TYPES; h++) {
      if (zds[h]) {
         vkCmdBindDescriptorSets(batch->state->cmdbuf, is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 pg->layout, zds[h]->pool->type, 1, &zds[h]->desc_set,
                                 zds[h]->pool->type == ZINK_DESCRIPTOR_TYPE_UBO ? dynamic_offset_idx : 0, dynamic_offsets);
      }
   }
}

static bool
line_width_needed(enum pipe_prim_type reduced_prim,
                  VkPolygonMode polygon_mode)
{
   switch (reduced_prim) {
   case PIPE_PRIM_POINTS:
      return false;

   case PIPE_PRIM_LINES:
      return true;

   case PIPE_PRIM_TRIANGLES:
      return polygon_mode == VK_POLYGON_MODE_LINE;

   default:
      unreachable("unexpected reduced prim");
   }
}

static inline bool
restart_supported(enum pipe_prim_type mode)
{
    return mode == PIPE_PRIM_LINE_STRIP || mode == PIPE_PRIM_TRIANGLE_STRIP || mode == PIPE_PRIM_TRIANGLE_FAN;
}

static void
update_drawid(struct zink_context *ctx, unsigned draw_id)
{
   struct zink_batch *batch = &ctx->batch;
   if (ctx->drawid_broken) {
      vkCmdPushConstants(batch->state->cmdbuf, ctx->curr_program->base.layout, VK_SHADER_STAGE_VERTEX_BIT,
                         offsetof(struct zink_push_constant, draw_id), sizeof(unsigned),
                         &draw_id);
   }
}

void
zink_draw_vbo(struct pipe_context *pctx,
              const struct pipe_draw_info *dinfo,
              const struct pipe_draw_indirect_info *dindirect,
              const struct pipe_draw_start_count *draws,
              unsigned num_draws)
{
   if (!dindirect && (!draws[0].count || !dinfo->instance_count))
      return;

   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_rasterizer_state *rast_state = ctx->rast_state;
   struct zink_depth_stencil_alpha_state *dsa_state = ctx->dsa_state;
   struct zink_so_target *so_target =
      dindirect && dindirect->count_from_stream_output ?
         zink_so_target(dindirect->count_from_stream_output) : NULL;
   VkBuffer counter_buffers[PIPE_MAX_SO_OUTPUTS];
   VkDeviceSize counter_buffer_offsets[PIPE_MAX_SO_OUTPUTS] = {};
   bool need_index_buffer_unref = false;

   /* check memory usage and flush/stall as needed to avoid oom */
   zink_maybe_flush_or_stall(ctx);

   if (dinfo->primitive_restart && !restart_supported(dinfo->mode)) {
       util_draw_vbo_without_prim_restart(pctx, dinfo, dindirect, &draws[0]);
       return;
   }
   if (dinfo->mode == PIPE_PRIM_QUADS ||
       dinfo->mode == PIPE_PRIM_QUAD_STRIP ||
       dinfo->mode == PIPE_PRIM_POLYGON ||
       (dinfo->mode == PIPE_PRIM_TRIANGLE_FAN && !screen->have_triangle_fans) ||
       dinfo->mode == PIPE_PRIM_LINE_LOOP) {
      util_primconvert_save_rasterizer_state(ctx->primconvert, &rast_state->base);
      for (unsigned i = 0; i < num_draws; i++) {
         /* TODO: is there actually a way to correctly handle this? no other driver does... */
         if (!u_trim_pipe_prim(dinfo->mode, (unsigned *)&draws[i].count))
            continue;
         util_primconvert_draw_vbo(ctx->primconvert, dinfo, &draws[i]);
      }
      return;
   }
   if (ctx->gfx_pipeline_state.vertices_per_patch != dinfo->vertices_per_patch)
      ctx->gfx_pipeline_state.dirty = true;
   bool drawid_broken = ctx->drawid_broken;
   ctx->drawid_broken = BITSET_TEST(ctx->gfx_stages[PIPE_SHADER_VERTEX]->nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID) &&
                        (!dindirect || !dindirect->buffer);
   if (drawid_broken != ctx->drawid_broken)
      ctx->dirty_shader_stages |= BITFIELD_BIT(PIPE_SHADER_VERTEX);
   ctx->gfx_pipeline_state.vertices_per_patch = dinfo->vertices_per_patch;
   if (ctx->rast_state->base.point_quad_rasterization &&
       ctx->gfx_prim_mode != dinfo->mode) {
      if (ctx->gfx_prim_mode == PIPE_PRIM_POINTS || dinfo->mode == PIPE_PRIM_POINTS)
         ctx->dirty_shader_stages |= BITFIELD_BIT(PIPE_SHADER_FRAGMENT);
   }
   ctx->gfx_prim_mode = dinfo->mode;
   struct zink_gfx_program *gfx_program = get_gfx_program(ctx);
   if (!gfx_program)
      return;

   if (ctx->gfx_pipeline_state.primitive_restart != !!dinfo->primitive_restart)
      ctx->gfx_pipeline_state.dirty = true;
   ctx->gfx_pipeline_state.primitive_restart = !!dinfo->primitive_restart;

   if (!zink_screen(pctx->screen)->info.have_EXT_extended_dynamic_state) {
      for (unsigned i = 0; i < ctx->element_state->hw_state.num_bindings; i++) {
         unsigned binding = ctx->element_state->binding_map[i];
         const struct pipe_vertex_buffer *vb = ctx->vertex_buffers + binding;
         if (ctx->gfx_pipeline_state.bindings[i].stride != vb->stride) {
            ctx->gfx_pipeline_state.bindings[i].stride = vb->stride;
            ctx->gfx_pipeline_state.dirty = true;
         }
      }
   }

   enum pipe_prim_type reduced_prim = u_reduced_prim(dinfo->mode);

   bool depth_bias = false;
   switch (reduced_prim) {
   case PIPE_PRIM_POINTS:
      depth_bias = rast_state->offset_point;
      break;

   case PIPE_PRIM_LINES:
      depth_bias = rast_state->offset_line;
      break;

   case PIPE_PRIM_TRIANGLES:
      depth_bias = rast_state->offset_tri;
      break;

   default:
      unreachable("unexpected reduced prim");
   }

   unsigned index_offset = 0;
   struct pipe_resource *index_buffer = NULL;
   if (dinfo->index_size > 0) {
       uint32_t restart_index = util_prim_restart_index_from_size(dinfo->index_size);
       if ((dinfo->primitive_restart && (dinfo->restart_index != restart_index)) ||
           (!screen->info.have_EXT_index_type_uint8 && dinfo->index_size == 1)) {
          util_translate_prim_restart_ib(pctx, dinfo, dindirect, &draws[0], &index_buffer);
          need_index_buffer_unref = true;
       } else {
          if (dinfo->has_user_indices) {
             if (!util_upload_index_buffer(pctx, dinfo, &draws[0], &index_buffer, &index_offset, 4)) {
                debug_printf("util_upload_index_buffer() failed\n");
                return;
             }
          } else
             index_buffer = dinfo->index.resource;
       }
   }
   if (ctx->xfb_barrier)
      zink_emit_xfb_counter_barrier(ctx);

   if (ctx->dirty_so_targets && ctx->num_so_targets)
      zink_emit_stream_output_targets(pctx);

   if (so_target)
      zink_emit_xfb_vertex_input_barrier(ctx, zink_resource(so_target->base.buffer));

   barrier_vertex_buffers(ctx);
   barrier_draw_buffers(ctx, dinfo, dindirect, index_buffer);

   for (int i = 0; i < ZINK_SHADER_COUNT; i++) {
      struct zink_shader *shader = ctx->gfx_stages[i];
      if (!shader)
         continue;
      enum pipe_shader_type stage = pipe_shader_type_from_mesa(shader->nir->info.stage);
      if (ctx->num_so_targets &&
          (stage == PIPE_SHADER_GEOMETRY ||
          (stage == PIPE_SHADER_TESS_EVAL && !ctx->gfx_stages[PIPE_SHADER_GEOMETRY]) ||
          (stage == PIPE_SHADER_VERTEX && !ctx->gfx_stages[PIPE_SHADER_GEOMETRY] && !ctx->gfx_stages[PIPE_SHADER_TESS_EVAL]))) {
         for (unsigned j = 0; j < ctx->num_so_targets; j++) {
            struct zink_so_target *t = zink_so_target(ctx->so_targets[j]);
            if (t)
               t->stride = shader->streamout.so_info.stride[j] * sizeof(uint32_t);
         }
      }
   }

   if (zink_program_has_descriptors(&gfx_program->base))
      update_descriptors(ctx, screen, false);

   struct zink_batch *batch = zink_batch_rp(ctx);
   VkViewport viewports[PIPE_MAX_VIEWPORTS] = {};
   for (unsigned i = 0; i < ctx->vp_state.num_viewports; i++) {
      VkViewport viewport = {
         ctx->vp_state.viewport_states[i].translate[0] - ctx->vp_state.viewport_states[i].scale[0],
         ctx->vp_state.viewport_states[i].translate[1] - ctx->vp_state.viewport_states[i].scale[1],
         ctx->vp_state.viewport_states[i].scale[0] * 2,
         ctx->vp_state.viewport_states[i].scale[1] * 2,
         ctx->rast_state->base.clip_halfz ?
            ctx->vp_state.viewport_states[i].translate[2] :
            ctx->vp_state.viewport_states[i].translate[2] - ctx->vp_state.viewport_states[i].scale[2],
         ctx->vp_state.viewport_states[i].translate[2] + ctx->vp_state.viewport_states[i].scale[2]
      };
      viewports[i] = viewport;
   }
   if (screen->info.have_EXT_extended_dynamic_state)
      screen->vk_CmdSetViewportWithCountEXT(batch->state->cmdbuf, ctx->vp_state.num_viewports, viewports);
   else
      vkCmdSetViewport(batch->state->cmdbuf, 0, ctx->vp_state.num_viewports, viewports);
   VkRect2D scissors[PIPE_MAX_VIEWPORTS] = {};
   if (ctx->rast_state->base.scissor) {
      for (unsigned i = 0; i < ctx->vp_state.num_viewports; i++) {
         scissors[i].offset.x = ctx->vp_state.scissor_states[i].minx;
         scissors[i].offset.y = ctx->vp_state.scissor_states[i].miny;
         scissors[i].extent.width = ctx->vp_state.scissor_states[i].maxx - ctx->vp_state.scissor_states[i].minx;
         scissors[i].extent.height = ctx->vp_state.scissor_states[i].maxy - ctx->vp_state.scissor_states[i].miny;
      }
   } else if (ctx->fb_state.width && ctx->fb_state.height) {
      for (unsigned i = 0; i < ctx->vp_state.num_viewports; i++) {
         scissors[i].extent.width = ctx->fb_state.width;
         scissors[i].extent.height = ctx->fb_state.height;
      }
   }
   if (screen->info.have_EXT_extended_dynamic_state)
      screen->vk_CmdSetScissorWithCountEXT(batch->state->cmdbuf, ctx->vp_state.num_viewports, scissors);
   else
      vkCmdSetScissor(batch->state->cmdbuf, 0, ctx->vp_state.num_viewports, scissors);

   if (line_width_needed(reduced_prim, rast_state->hw_state.polygon_mode)) {
      if (screen->info.feats.features.wideLines || ctx->line_width == 1.0f)
         vkCmdSetLineWidth(batch->state->cmdbuf, ctx->line_width);
      else
         debug_printf("BUG: wide lines not supported, needs fallback!");
   }

   if (dsa_state->base.stencil[0].enabled) {
      if (dsa_state->base.stencil[1].enabled) {
         vkCmdSetStencilReference(batch->state->cmdbuf, VK_STENCIL_FACE_FRONT_BIT,
                                  ctx->stencil_ref.ref_value[0]);
         vkCmdSetStencilReference(batch->state->cmdbuf, VK_STENCIL_FACE_BACK_BIT,
                                  ctx->stencil_ref.ref_value[1]);
      } else
         vkCmdSetStencilReference(batch->state->cmdbuf,
                                  VK_STENCIL_FACE_FRONT_AND_BACK,
                                  ctx->stencil_ref.ref_value[0]);
   }

   if (depth_bias)
      vkCmdSetDepthBias(batch->state->cmdbuf, rast_state->offset_units, rast_state->offset_clamp, rast_state->offset_scale);
   else
      vkCmdSetDepthBias(batch->state->cmdbuf, 0.0f, 0.0f, 0.0f);

   if (ctx->gfx_pipeline_state.blend_state->need_blend_constants)
      vkCmdSetBlendConstants(batch->state->cmdbuf, ctx->blend_constants);


   VkPipeline pipeline = zink_get_gfx_pipeline(screen, gfx_program,
                                               &ctx->gfx_pipeline_state,
                                               dinfo->mode);
   vkCmdBindPipeline(batch->state->cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

   zink_bind_vertex_buffers(batch, ctx);

   if (BITSET_TEST(ctx->gfx_stages[PIPE_SHADER_VERTEX]->nir->info.system_values_read, SYSTEM_VALUE_BASE_VERTEX)) {
      unsigned draw_mode_is_indexed = dinfo->index_size > 0;
      vkCmdPushConstants(batch->state->cmdbuf, gfx_program->base.layout, VK_SHADER_STAGE_VERTEX_BIT,
                         offsetof(struct zink_push_constant, draw_mode_is_indexed), sizeof(unsigned),
                         &draw_mode_is_indexed);
   }
   if (gfx_program->shaders[PIPE_SHADER_TESS_CTRL] && gfx_program->shaders[PIPE_SHADER_TESS_CTRL]->is_generated)
      vkCmdPushConstants(batch->state->cmdbuf, gfx_program->base.layout, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                         offsetof(struct zink_push_constant, default_inner_level), sizeof(float) * 6,
                         &ctx->tess_levels[0]);

   zink_query_update_gs_states(ctx);

   if (ctx->num_so_targets) {
      for (unsigned i = 0; i < ctx->num_so_targets; i++) {
         struct zink_so_target *t = zink_so_target(ctx->so_targets[i]);
         counter_buffers[i] = VK_NULL_HANDLE;
         if (t) {
            struct zink_resource *res = zink_resource(t->counter_buffer);
            zink_batch_reference_resource_rw(batch, res, true);
            if (t->counter_buffer_valid) {
               counter_buffers[i] = res->obj->buffer;
               counter_buffer_offsets[i] = t->counter_buffer_offset;
            }
         }
      }
      screen->vk_CmdBeginTransformFeedbackEXT(batch->state->cmdbuf, 0, ctx->num_so_targets, counter_buffers, counter_buffer_offsets);
   }

   unsigned draw_id = dinfo->drawid;
   if (dinfo->index_size > 0) {
      VkIndexType index_type;
      unsigned index_size = dinfo->index_size;
      if (need_index_buffer_unref)
         /* index buffer will have been promoted from uint8 to uint16 in this case */
         index_size = MAX2(index_size, 2);
      switch (index_size) {
      case 1:
         assert(screen->info.have_EXT_index_type_uint8);
         index_type = VK_INDEX_TYPE_UINT8_EXT;
         break;
      case 2:
         index_type = VK_INDEX_TYPE_UINT16;
         break;
      case 4:
         index_type = VK_INDEX_TYPE_UINT32;
         break;
      default:
         unreachable("unknown index size!");
      }
      struct zink_resource *res = zink_resource(index_buffer);
      vkCmdBindIndexBuffer(batch->state->cmdbuf, res->obj->buffer, index_offset, index_type);
      zink_batch_reference_resource_rw(batch, res, false);
      if (dindirect && dindirect->buffer) {
         assert(num_draws == 1);
         update_drawid(ctx, draw_id);
         struct zink_resource *indirect = zink_resource(dindirect->buffer);
         zink_batch_reference_resource_rw(batch, indirect, false);
         if (dindirect->indirect_draw_count) {
             struct zink_resource *indirect_draw_count = zink_resource(dindirect->indirect_draw_count);
             zink_batch_reference_resource_rw(batch, indirect_draw_count, false);
             screen->vk_CmdDrawIndexedIndirectCount(batch->state->cmdbuf, indirect->obj->buffer, dindirect->offset,
                                           indirect_draw_count->obj->buffer, dindirect->indirect_draw_count_offset,
                                           dindirect->draw_count, dindirect->stride);
         } else
            vkCmdDrawIndexedIndirect(batch->state->cmdbuf, indirect->obj->buffer, dindirect->offset, dindirect->draw_count, dindirect->stride);
      } else {
         for (unsigned i = 0; i < num_draws; i++) {
            update_drawid(ctx, draw_id);
            vkCmdDrawIndexed(batch->state->cmdbuf,
               draws[i].count, dinfo->instance_count,
               need_index_buffer_unref ? 0 : draws[i].start, dinfo->index_bias, dinfo->start_instance);
            if (dinfo->increment_draw_id)
               draw_id++;
        }
      }
   } else {
      if (so_target && screen->info.tf_props.transformFeedbackDraw) {
         update_drawid(ctx, draw_id);
         zink_batch_reference_resource_rw(batch, zink_resource(so_target->base.buffer), false);
         zink_batch_reference_resource_rw(batch, zink_resource(so_target->counter_buffer), true);
         screen->vk_CmdDrawIndirectByteCountEXT(batch->state->cmdbuf, dinfo->instance_count, dinfo->start_instance,
                                       zink_resource(so_target->counter_buffer)->obj->buffer, so_target->counter_buffer_offset, 0,
                                       MIN2(so_target->stride, screen->info.tf_props.maxTransformFeedbackBufferDataStride));
      } else if (dindirect && dindirect->buffer) {
         assert(num_draws == 1);
         update_drawid(ctx, draw_id);
         struct zink_resource *indirect = zink_resource(dindirect->buffer);
         zink_batch_reference_resource_rw(batch, indirect, false);
         if (dindirect->indirect_draw_count) {
             struct zink_resource *indirect_draw_count = zink_resource(dindirect->indirect_draw_count);
             zink_batch_reference_resource_rw(batch, indirect_draw_count, false);
             screen->vk_CmdDrawIndirectCount(batch->state->cmdbuf, indirect->obj->buffer, dindirect->offset,
                                           indirect_draw_count->obj->buffer, dindirect->indirect_draw_count_offset,
                                           dindirect->draw_count, dindirect->stride);
         } else
            vkCmdDrawIndirect(batch->state->cmdbuf, indirect->obj->buffer, dindirect->offset, dindirect->draw_count, dindirect->stride);
      } else {
         for (unsigned i = 0; i < num_draws; i++) {
            update_drawid(ctx, draw_id);
            vkCmdDraw(batch->state->cmdbuf, draws[i].count, dinfo->instance_count, draws[i].start, dinfo->start_instance);
            if (dinfo->increment_draw_id)
               draw_id++;
         }
      }
   }

   if (dinfo->index_size > 0 && (dinfo->has_user_indices || need_index_buffer_unref))
      pipe_resource_reference(&index_buffer, NULL);

   if (ctx->num_so_targets) {
      for (unsigned i = 0; i < ctx->num_so_targets; i++) {
         struct zink_so_target *t = zink_so_target(ctx->so_targets[i]);
         if (t) {
            counter_buffers[i] = zink_resource(t->counter_buffer)->obj->buffer;
            counter_buffer_offsets[i] = t->counter_buffer_offset;
            t->counter_buffer_valid = true;
         }
      }
      screen->vk_CmdEndTransformFeedbackEXT(batch->state->cmdbuf, 0, ctx->num_so_targets, counter_buffers, counter_buffer_offsets);
   }
   batch->has_work = true;
}

void
zink_launch_grid(struct pipe_context *pctx, const struct pipe_grid_info *info)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_batch *batch = &ctx->batch;

   /* check memory usage and flush/stall as needed to avoid oom */
   zink_maybe_flush_or_stall(ctx);

   struct zink_compute_program *comp_program = get_compute_program(ctx);
   if (!comp_program)
      return;

   zink_program_update_compute_pipeline_state(ctx, comp_program, info->block);
   VkPipeline pipeline = zink_get_compute_pipeline(screen, comp_program,
                                               &ctx->compute_pipeline_state);

   if (zink_program_has_descriptors(&comp_program->base))
      update_descriptors(ctx, screen, true);


   vkCmdBindPipeline(batch->state->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   if (info->indirect) {
      vkCmdDispatchIndirect(batch->state->cmdbuf, zink_resource(info->indirect)->obj->buffer, info->indirect_offset);
      zink_batch_reference_resource_rw(batch, zink_resource(info->indirect), false);
   } else
      vkCmdDispatch(batch->state->cmdbuf, info->grid[0], info->grid[1], info->grid[2]);
   batch->has_work = true;
}
