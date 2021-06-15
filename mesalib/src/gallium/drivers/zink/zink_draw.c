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
   unsigned bits = 1 << PIPE_SHADER_COMPUTE;
   ctx->dirty_shader_stages |= ctx->inlinable_uniforms_dirty_mask &
                               ctx->inlinable_uniforms_valid_mask &
                               ctx->shader_has_inlinable_uniforms_mask & bits;
   if (ctx->dirty_shader_stages & bits) {
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
      ctx->dirty_shader_stages &= bits;
      ctx->inlinable_uniforms_dirty_mask &= bits;
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
   unsigned bits = u_bit_consecutive(PIPE_SHADER_VERTEX, 5);
   ctx->dirty_shader_stages |= ctx->inlinable_uniforms_dirty_mask &
                               ctx->inlinable_uniforms_valid_mask &
                               ctx->shader_has_inlinable_uniforms_mask & bits;
   if (ctx->dirty_shader_stages & bits) {
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
      ctx->dirty_shader_stages &= ~bits;
      ctx->inlinable_uniforms_dirty_mask &= ~bits;
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

static void
update_drawid(struct zink_context *ctx, unsigned draw_id)
{
   struct zink_batch *batch = &ctx->batch;
   if (ctx->drawid_broken) {
      vkCmdPushConstants(batch->state->cmdbuf, ctx->curr_program->base.layout, VK_SHADER_STAGE_VERTEX_BIT,
                         offsetof(struct zink_gfx_push_constant, draw_id), sizeof(unsigned),
                         &draw_id);
   }
}

void
zink_draw_vbo(struct pipe_context *pctx,
              const struct pipe_draw_info *dinfo,
              unsigned drawid_offset,
              const struct pipe_draw_indirect_info *dindirect,
              const struct pipe_draw_start_count_bias *draws,
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
   VkDeviceSize counter_buffer_offsets[PIPE_MAX_SO_OUTPUTS];
   bool need_index_buffer_unref = false;

   /* check memory usage and flush/stall as needed to avoid oom */
   zink_maybe_flush_or_stall(ctx);

   if (dinfo->primitive_restart && !restart_supported(dinfo->mode)) {
       util_draw_vbo_without_prim_restart(pctx, dinfo, drawid_offset, dindirect, &draws[0]);
       return;
   }
   if (dinfo->mode == PIPE_PRIM_QUADS ||
       dinfo->mode == PIPE_PRIM_QUAD_STRIP ||
       dinfo->mode == PIPE_PRIM_POLYGON ||
       (dinfo->mode == PIPE_PRIM_TRIANGLE_FAN && !screen->have_triangle_fans) ||
       dinfo->mode == PIPE_PRIM_LINE_LOOP) {
      util_primconvert_save_rasterizer_state(ctx->primconvert, &rast_state->base);
      util_primconvert_draw_vbo(ctx->primconvert, dinfo, drawid_offset, dindirect, draws, num_draws);
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
      zink_descriptors_update(ctx, screen, false);

   struct zink_batch *batch = zink_batch_rp(ctx);
   VkViewport viewports[PIPE_MAX_VIEWPORTS];
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
   VkRect2D scissors[PIPE_MAX_VIEWPORTS];
   if (ctx->rast_state->base.scissor) {
      for (unsigned i = 0; i < ctx->vp_state.num_viewports; i++) {
         scissors[i].offset.x = ctx->vp_state.scissor_states[i].minx;
         scissors[i].offset.y = ctx->vp_state.scissor_states[i].miny;
         scissors[i].extent.width = ctx->vp_state.scissor_states[i].maxx - ctx->vp_state.scissor_states[i].minx;
         scissors[i].extent.height = ctx->vp_state.scissor_states[i].maxy - ctx->vp_state.scissor_states[i].miny;
      }
   } else {
      for (unsigned i = 0; i < ctx->vp_state.num_viewports; i++) {
         scissors[i].offset.x = 0;
         scissors[i].offset.y = 0;
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
                         offsetof(struct zink_gfx_push_constant, draw_mode_is_indexed), sizeof(unsigned),
                         &draw_mode_is_indexed);
   }
   if (gfx_program->shaders[PIPE_SHADER_TESS_CTRL] && gfx_program->shaders[PIPE_SHADER_TESS_CTRL]->is_generated)
      vkCmdPushConstants(batch->state->cmdbuf, gfx_program->base.layout, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                         offsetof(struct zink_gfx_push_constant, default_inner_level), sizeof(float) * 6,
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

   unsigned draw_id = drawid_offset;
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
               need_index_buffer_unref ? 0 : draws[i].start, draws[i].index_bias, dinfo->start_instance);
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
      zink_descriptors_update(ctx, screen, true);


   vkCmdBindPipeline(batch->state->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   if (BITSET_TEST(comp_program->shader->nir->info.system_values_read, SYSTEM_VALUE_WORK_DIM))
      vkCmdPushConstants(batch->state->cmdbuf, comp_program->base.layout, VK_SHADER_STAGE_COMPUTE_BIT,
                         offsetof(struct zink_cs_push_constant, work_dim), sizeof(uint32_t),
                         &info->work_dim);

   if (info->indirect) {
      vkCmdDispatchIndirect(batch->state->cmdbuf, zink_resource(info->indirect)->obj->buffer, info->indirect_offset);
      zink_batch_reference_resource_rw(batch, zink_resource(info->indirect), false);
   } else
      vkCmdDispatch(batch->state->cmdbuf, info->grid[0], info->grid[1], info->grid[2]);
   batch->has_work = true;
}
