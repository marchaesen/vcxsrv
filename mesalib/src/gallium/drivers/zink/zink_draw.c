#include "zink_compiler.h"
#include "zink_context.h"
#include "zink_program.h"
#include "zink_query.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "zink_state.h"

#include "indices/u_primconvert.h"
#include "util/hash_table.h"
#include "util/u_debug.h"
#include "util/u_helpers.h"
#include "util/u_inlines.h"
#include "util/u_prim.h"
#include "util/u_prim_restart.h"

static VkDescriptorSet
allocate_descriptor_set(struct zink_screen *screen,
                        struct zink_batch *batch,
                        struct zink_gfx_program *prog)
{
   assert(batch->descs_left >= prog->num_descriptors);
   VkDescriptorSetAllocateInfo dsai;
   memset((void *)&dsai, 0, sizeof(dsai));
   dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   dsai.pNext = NULL;
   dsai.descriptorPool = batch->descpool;
   dsai.descriptorSetCount = 1;
   dsai.pSetLayouts = &prog->dsl;

   VkDescriptorSet desc_set;
   if (vkAllocateDescriptorSets(screen->dev, &dsai, &desc_set) != VK_SUCCESS) {
      debug_printf("ZINK: failed to allocate descriptor set :/");
      return VK_NULL_HANDLE;
   }

   batch->descs_left -= prog->num_descriptors;
   return desc_set;
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
   VkBufferMemoryBarrier barriers[PIPE_MAX_SO_OUTPUTS] = {};
   unsigned barrier_count = 0;

   for (unsigned i = 0; i < ctx->num_so_targets; i++) {
      struct zink_so_target *t = zink_so_target(ctx->so_targets[i]);
      if (t->counter_buffer_valid) {
          barriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
          barriers[i].srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT;
          barriers[i].dstAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT;
          barriers[i].buffer = zink_resource(t->counter_buffer)->buffer;
          barriers[i].size = VK_WHOLE_SIZE;
          barrier_count++;
      }
   }
   struct zink_batch *batch = zink_batch_no_rp(ctx);
   vkCmdPipelineBarrier(batch->cmdbuf,
      VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
      VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
      0,
      0, NULL,
      barrier_count, barriers,
      0, NULL
   );
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
   VkBufferMemoryBarrier barriers[1] = {};
   barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
   barriers[0].srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT;
   barriers[0].dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
   barriers[0].buffer = res->buffer;
   barriers[0].size = VK_WHOLE_SIZE;
   struct zink_batch *batch = zink_batch_no_rp(ctx);
   zink_batch_reference_resource_rw(batch, res, false);
   vkCmdPipelineBarrier(batch->cmdbuf,
      VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
      0,
      0, NULL,
      ARRAY_SIZE(barriers), barriers,
      0, NULL
   );
   res->needs_xfb_barrier = false;
}

static void
zink_emit_stream_output_targets(struct pipe_context *pctx)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_batch *batch = zink_curr_batch(ctx);
   VkBuffer buffers[PIPE_MAX_SO_OUTPUTS];
   VkDeviceSize buffer_offsets[PIPE_MAX_SO_OUTPUTS];
   VkDeviceSize buffer_sizes[PIPE_MAX_SO_OUTPUTS];

   for (unsigned i = 0; i < ctx->num_so_targets; i++) {
      struct zink_so_target *t = (struct zink_so_target *)ctx->so_targets[i];
      buffers[i] = zink_resource(t->base.buffer)->buffer;
      zink_batch_reference_resource_rw(batch, zink_resource(t->base.buffer), true);
      buffer_offsets[i] = t->base.buffer_offset;
      buffer_sizes[i] = t->base.buffer_size;
   }

   screen->vk_CmdBindTransformFeedbackBuffersEXT(batch->cmdbuf, 0, ctx->num_so_targets,
                                                 buffers, buffer_offsets,
                                                 buffer_sizes);
   ctx->dirty_so_targets = false;
}

static void
zink_bind_vertex_buffers(struct zink_batch *batch, struct zink_context *ctx)
{
   VkBuffer buffers[PIPE_MAX_ATTRIBS];
   VkDeviceSize buffer_offsets[PIPE_MAX_ATTRIBS];
   const struct zink_vertex_elements_state *elems = ctx->element_state;
   for (unsigned i = 0; i < elems->hw_state.num_bindings; i++) {
      struct pipe_vertex_buffer *vb = ctx->buffers + ctx->element_state->binding_map[i];
      assert(vb);
      if (vb->buffer.resource) {
         struct zink_resource *res = zink_resource(vb->buffer.resource);
         buffers[i] = res->buffer;
         buffer_offsets[i] = vb->buffer_offset;
         zink_batch_reference_resource_rw(batch, res, false);
      } else {
         buffers[i] = zink_resource(ctx->dummy_buffer)->buffer;
         buffer_offsets[i] = 0;
      }
   }

   if (elems->hw_state.num_bindings > 0)
      vkCmdBindVertexBuffers(batch->cmdbuf, 0,
                             elems->hw_state.num_bindings,
                             buffers, buffer_offsets);
}

static struct zink_gfx_program *
get_gfx_program(struct zink_context *ctx)
{
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
      ctx->curr_program = entry->data;
      ctx->dirty_shader_stages = 0;
   }

   assert(ctx->curr_program);
   return ctx->curr_program;
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

void
zink_draw_vbo(struct pipe_context *pctx,
              const struct pipe_draw_info *dinfo,
              const struct pipe_draw_indirect_info *dindirect,
              const struct pipe_draw_start_count *draws,
              unsigned num_draws)
{
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


   if (dinfo->primitive_restart && !restart_supported(dinfo->mode)) {
       util_draw_vbo_without_prim_restart(pctx, dinfo, dindirect, &draws[0]);
       return;
   }
   if (dinfo->mode == PIPE_PRIM_QUADS ||
       dinfo->mode == PIPE_PRIM_QUAD_STRIP ||
       dinfo->mode == PIPE_PRIM_POLYGON ||
       (dinfo->mode == PIPE_PRIM_TRIANGLE_FAN && !screen->have_triangle_fans) ||
       dinfo->mode == PIPE_PRIM_LINE_LOOP) {
      if (!u_trim_pipe_prim(dinfo->mode, (unsigned *)&draws[0].count))
         return;

      util_primconvert_save_rasterizer_state(ctx->primconvert, &rast_state->base);
      util_primconvert_draw_vbo(ctx->primconvert, dinfo, &draws[0]);
      return;
   }
   if (ctx->gfx_pipeline_state.vertices_per_patch != dinfo->vertices_per_patch)
      ctx->gfx_pipeline_state.hash = 0;
   ctx->gfx_pipeline_state.vertices_per_patch = dinfo->vertices_per_patch;
   struct zink_gfx_program *gfx_program = get_gfx_program(ctx);
   if (!gfx_program)
      return;

   if (ctx->gfx_pipeline_state.primitive_restart != !!dinfo->primitive_restart)
      ctx->gfx_pipeline_state.dirty = true;
   ctx->gfx_pipeline_state.primitive_restart = !!dinfo->primitive_restart;

   VkPipeline pipeline = zink_get_gfx_pipeline(screen, gfx_program,
                                               &ctx->gfx_pipeline_state,
                                               dinfo->mode);

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

   VkWriteDescriptorSet wds[PIPE_SHADER_TYPES * PIPE_MAX_CONSTANT_BUFFERS + PIPE_SHADER_TYPES * PIPE_MAX_SHADER_SAMPLER_VIEWS];
   struct zink_resource *write_desc_resources[PIPE_SHADER_TYPES * PIPE_MAX_CONSTANT_BUFFERS + PIPE_SHADER_TYPES * PIPE_MAX_SHADER_SAMPLER_VIEWS];
   VkDescriptorBufferInfo buffer_infos[PIPE_SHADER_TYPES * PIPE_MAX_CONSTANT_BUFFERS];
   VkDescriptorImageInfo image_infos[PIPE_SHADER_TYPES * PIPE_MAX_SHADER_SAMPLER_VIEWS];
   VkBufferView buffer_view[] = {VK_NULL_HANDLE};
   int num_wds = 0, num_buffer_info = 0, num_image_info = 0;

   struct zink_resource *transitions[PIPE_SHADER_TYPES * PIPE_MAX_SHADER_SAMPLER_VIEWS];
   int num_transitions = 0;

   for (int i = 0; i < ARRAY_SIZE(ctx->gfx_stages); i++) {
      struct zink_shader *shader = ctx->gfx_stages[i];
      if (!shader)
         continue;

      if (ctx->num_so_targets &&
          (i == PIPE_SHADER_GEOMETRY ||
          (i == PIPE_SHADER_TESS_EVAL && !ctx->gfx_stages[PIPE_SHADER_GEOMETRY]) ||
          (i == PIPE_SHADER_VERTEX && !ctx->gfx_stages[PIPE_SHADER_GEOMETRY] && !ctx->gfx_stages[PIPE_SHADER_TESS_EVAL]))) {
         for (unsigned i = 0; i < ctx->num_so_targets; i++) {
            struct zink_so_target *t = zink_so_target(ctx->so_targets[i]);
            t->stride = shader->streamout.so_info.stride[i] * sizeof(uint32_t);
         }
      }

      for (int j = 0; j < shader->num_bindings; j++) {
         int index = shader->bindings[j].index;
         if (shader->bindings[j].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            assert(ctx->ubos[i][index].buffer_size <= screen->info.props.limits.maxUniformBufferRange);
            struct zink_resource *res = zink_resource(ctx->ubos[i][index].buffer);
            assert(!res || ctx->ubos[i][index].buffer_size > 0);
            assert(!res || ctx->ubos[i][index].buffer);
            write_desc_resources[num_wds] = res;
            buffer_infos[num_buffer_info].buffer = res ? res->buffer :
                                                   (screen->info.rb2_feats.nullDescriptor ?
                                                    VK_NULL_HANDLE :
                                                    zink_resource(ctx->dummy_buffer)->buffer);
            buffer_infos[num_buffer_info].offset = res ? ctx->ubos[i][index].buffer_offset : 0;
            buffer_infos[num_buffer_info].range  = res ? ctx->ubos[i][index].buffer_size : VK_WHOLE_SIZE;
            wds[num_wds].pBufferInfo = buffer_infos + num_buffer_info;
            ++num_buffer_info;
         } else {
            struct pipe_sampler_view *psampler_view = ctx->image_views[i][index];
            struct zink_sampler_view *sampler_view = zink_sampler_view(psampler_view);

            struct zink_resource *res = psampler_view ? zink_resource(psampler_view->texture) : NULL;
            write_desc_resources[num_wds] = res;
            if (!res) {
               /* if we're hitting this assert often, we can probably just throw a junk buffer in since
                * the results of this codepath are undefined in ARB_texture_buffer_object spec
                */
               assert(screen->info.rb2_feats.nullDescriptor);
               if (shader->bindings[j].type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER)
                  wds[num_wds].pTexelBufferView = &buffer_view[0];
               else {
                  image_infos[num_image_info].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                  image_infos[num_image_info].imageView = VK_NULL_HANDLE;
                  image_infos[num_image_info].sampler = ctx->samplers[i][index];
                  wds[num_wds].pImageInfo = image_infos + num_image_info;
                  ++num_image_info;
               }
            } else if (res->base.target == PIPE_BUFFER)
               wds[num_wds].pTexelBufferView = &sampler_view->buffer_view;
            else {
               if (res->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                  transitions[num_transitions++] = res;
               image_infos[num_image_info].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
               image_infos[num_image_info].imageView = sampler_view->image_view;
               image_infos[num_image_info].sampler = ctx->samplers[i][index];
               wds[num_wds].pImageInfo = image_infos + num_image_info;
               ++num_image_info;
            }
         }

         wds[num_wds].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
         wds[num_wds].pNext = NULL;
         wds[num_wds].dstBinding = shader->bindings[j].binding;
         wds[num_wds].dstArrayElement = 0;
         wds[num_wds].descriptorCount = 1;
         wds[num_wds].descriptorType = shader->bindings[j].type;
         ++num_wds;
      }
   }

   struct zink_batch *batch;
   if (num_transitions > 0) {
      batch = zink_batch_no_rp(ctx);

      for (int i = 0; i < num_transitions; ++i)
         zink_resource_barrier(batch->cmdbuf, transitions[i],
                               transitions[i]->aspect,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
   }

   if (ctx->xfb_barrier)
      zink_emit_xfb_counter_barrier(ctx);

   if (ctx->dirty_so_targets)
      zink_emit_stream_output_targets(pctx);

   if (so_target && zink_resource(so_target->base.buffer)->needs_xfb_barrier)
      zink_emit_xfb_vertex_input_barrier(ctx, zink_resource(so_target->base.buffer));


   batch = zink_batch_rp(ctx);

   if (batch->descs_left < gfx_program->num_descriptors) {
      ctx->base.flush(&ctx->base, NULL, 0);
      batch = zink_batch_rp(ctx);
      assert(batch->descs_left >= gfx_program->num_descriptors);
   }
   zink_batch_reference_program(batch, ctx->curr_program);

   VkDescriptorSet desc_set = allocate_descriptor_set(screen, batch,
                                                      gfx_program);
   assert(desc_set != VK_NULL_HANDLE);

   for (int i = 0; i < ARRAY_SIZE(ctx->gfx_stages); i++) {
      struct zink_shader *shader = ctx->gfx_stages[i];
      if (!shader)
         continue;

      for (int j = 0; j < shader->num_bindings; j++) {
         int index = shader->bindings[j].index;
         if (shader->bindings[j].type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            struct zink_sampler_view *sampler_view = zink_sampler_view(ctx->image_views[i][index]);
            if (sampler_view)
               zink_batch_reference_sampler_view(batch, sampler_view);
         }
      }
   }

   vkCmdSetViewport(batch->cmdbuf, 0, ctx->gfx_pipeline_state.num_viewports, ctx->viewports);
   if (ctx->rast_state->base.scissor)
      vkCmdSetScissor(batch->cmdbuf, 0, ctx->gfx_pipeline_state.num_viewports, ctx->scissors);
   else if (ctx->fb_state.width && ctx->fb_state.height) {
      VkRect2D fb_scissor[ctx->gfx_pipeline_state.num_viewports];
      for (unsigned i = 0; i < ctx->gfx_pipeline_state.num_viewports; i++) {
         fb_scissor[i].offset.x = fb_scissor[i].offset.y = 0;
         fb_scissor[i].extent.width = ctx->fb_state.width;
         fb_scissor[i].extent.height = ctx->fb_state.height;
      }
      vkCmdSetScissor(batch->cmdbuf, 0, ctx->gfx_pipeline_state.num_viewports, fb_scissor);
   }

   if (line_width_needed(reduced_prim, rast_state->hw_state.polygon_mode)) {
      if (screen->info.feats.features.wideLines || ctx->line_width == 1.0f)
         vkCmdSetLineWidth(batch->cmdbuf, ctx->line_width);
      else
         debug_printf("BUG: wide lines not supported, needs fallback!");
   }

   if (dsa_state->base.stencil[0].enabled) {
      if (dsa_state->base.stencil[1].enabled) {
         vkCmdSetStencilReference(batch->cmdbuf, VK_STENCIL_FACE_FRONT_BIT,
                                  ctx->stencil_ref.ref_value[0]);
         vkCmdSetStencilReference(batch->cmdbuf, VK_STENCIL_FACE_BACK_BIT,
                                  ctx->stencil_ref.ref_value[1]);
      } else
         vkCmdSetStencilReference(batch->cmdbuf,
                                  VK_STENCIL_FACE_FRONT_AND_BACK,
                                  ctx->stencil_ref.ref_value[0]);
   }

   if (depth_bias)
      vkCmdSetDepthBias(batch->cmdbuf, rast_state->offset_units, rast_state->offset_clamp, rast_state->offset_scale);
   else
      vkCmdSetDepthBias(batch->cmdbuf, 0.0f, 0.0f, 0.0f);

   if (ctx->gfx_pipeline_state.blend_state->need_blend_constants)
      vkCmdSetBlendConstants(batch->cmdbuf, ctx->blend_constants);

   if (num_wds > 0) {
      for (int i = 0; i < num_wds; ++i) {
         wds[i].dstSet = desc_set;
         if (write_desc_resources[i])
            zink_batch_reference_resource_rw(batch, write_desc_resources[i], false);
      }
      vkUpdateDescriptorSets(screen->dev, num_wds, wds, 0, NULL);
   }

   vkCmdBindPipeline(batch->cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   vkCmdBindDescriptorSets(batch->cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           gfx_program->layout, 0, 1, &desc_set, 0, NULL);
   zink_bind_vertex_buffers(batch, ctx);

   if (gfx_program->shaders[PIPE_SHADER_TESS_CTRL] && gfx_program->shaders[PIPE_SHADER_TESS_CTRL]->is_generated)
      vkCmdPushConstants(batch->cmdbuf, gfx_program->layout, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                         0, sizeof(float) * 6,
                         &ctx->tess_levels[0]);

   zink_query_update_gs_states(ctx);

   if (ctx->num_so_targets) {
      for (unsigned i = 0; i < ctx->num_so_targets; i++) {
         struct zink_so_target *t = zink_so_target(ctx->so_targets[i]);
         struct zink_resource *res = zink_resource(t->counter_buffer);
         if (t->counter_buffer_valid) {
            zink_batch_reference_resource_rw(batch, res, true);
            counter_buffers[i] = res->buffer;
            counter_buffer_offsets[i] = t->counter_buffer_offset;
         } else
            counter_buffers[i] = VK_NULL_HANDLE;
      }
      screen->vk_CmdBeginTransformFeedbackEXT(batch->cmdbuf, 0, ctx->num_so_targets, counter_buffers, counter_buffer_offsets);
   }

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
      vkCmdBindIndexBuffer(batch->cmdbuf, res->buffer, index_offset, index_type);
      zink_batch_reference_resource_rw(batch, res, false);
      if (dindirect && dindirect->buffer) {
         struct zink_resource *indirect = zink_resource(dindirect->buffer);
         zink_batch_reference_resource_rw(batch, indirect, false);
         if (dindirect->indirect_draw_count) {
             struct zink_resource *indirect_draw_count = zink_resource(dindirect->indirect_draw_count);
             zink_batch_reference_resource_rw(batch, indirect_draw_count, false);
             screen->vk_CmdDrawIndexedIndirectCount(batch->cmdbuf, indirect->buffer, dindirect->offset,
                                           indirect_draw_count->buffer, dindirect->indirect_draw_count_offset,
                                           dindirect->draw_count, dindirect->stride);
         } else
            vkCmdDrawIndexedIndirect(batch->cmdbuf, indirect->buffer, dindirect->offset, dindirect->draw_count, dindirect->stride);
      } else
         vkCmdDrawIndexed(batch->cmdbuf,
            draws[0].count, dinfo->instance_count,
            need_index_buffer_unref ? 0 : draws[0].start, dinfo->index_bias, dinfo->start_instance);
   } else {
      if (so_target && screen->info.tf_props.transformFeedbackDraw) {
         zink_batch_reference_resource_rw(batch, zink_resource(so_target->counter_buffer), true);
         screen->vk_CmdDrawIndirectByteCountEXT(batch->cmdbuf, dinfo->instance_count, dinfo->start_instance,
                                       zink_resource(so_target->counter_buffer)->buffer, so_target->counter_buffer_offset, 0,
                                       MIN2(so_target->stride, screen->info.tf_props.maxTransformFeedbackBufferDataStride));
      } else if (dindirect && dindirect->buffer) {
         struct zink_resource *indirect = zink_resource(dindirect->buffer);
         zink_batch_reference_resource_rw(batch, indirect, false);
         if (dindirect->indirect_draw_count) {
             struct zink_resource *indirect_draw_count = zink_resource(dindirect->indirect_draw_count);
             zink_batch_reference_resource_rw(batch, indirect_draw_count, false);
             screen->vk_CmdDrawIndirectCount(batch->cmdbuf, indirect->buffer, dindirect->offset,
                                           indirect_draw_count->buffer, dindirect->indirect_draw_count_offset,
                                           dindirect->draw_count, dindirect->stride);
         } else
            vkCmdDrawIndirect(batch->cmdbuf, indirect->buffer, dindirect->offset, dindirect->draw_count, dindirect->stride);
      } else
         vkCmdDraw(batch->cmdbuf, draws[0].count, dinfo->instance_count, draws[0].start, dinfo->start_instance);
   }

   if (dinfo->index_size > 0 && (dinfo->has_user_indices || need_index_buffer_unref))
      pipe_resource_reference(&index_buffer, NULL);

   if (ctx->num_so_targets) {
      for (unsigned i = 0; i < ctx->num_so_targets; i++) {
         struct zink_so_target *t = zink_so_target(ctx->so_targets[i]);
         counter_buffers[i] = zink_resource(t->counter_buffer)->buffer;
         counter_buffer_offsets[i] = t->counter_buffer_offset;
         t->counter_buffer_valid = true;
         zink_resource(ctx->so_targets[i]->buffer)->needs_xfb_barrier = true;
      }
      screen->vk_CmdEndTransformFeedbackEXT(batch->cmdbuf, 0, ctx->num_so_targets, counter_buffers, counter_buffer_offsets);
   }
}
