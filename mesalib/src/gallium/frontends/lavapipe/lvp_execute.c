/*
 * Copyright © 2019 Red Hat.
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

/* use a gallium context to execute a command buffer */

#include "lvp_private.h"

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "lvp_conv.h"

#include "pipe/p_shader_tokens.h"
#include "tgsi/tgsi_text.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_from_mesa.h"

#include "util/format/u_format.h"
#include "util/u_surface.h"
#include "util/u_sampler.h"
#include "util/u_box.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_prim.h"
#include "util/u_prim_restart.h"
#include "util/format/u_format_zs.h"
#include "util/ptralloc.h"
#include "tgsi/tgsi_from_mesa.h"

#include "vk_cmd_enqueue_entrypoints.h"
#include "vk_util.h"

#define VK_PROTOTYPES
#include <vulkan/vulkan.h>

#define DOUBLE_EQ(a, b) (fabs((a) - (b)) < DBL_EPSILON)

enum gs_output {
  GS_OUTPUT_NONE,
  GS_OUTPUT_NOT_LINES,
  GS_OUTPUT_LINES,
};

struct lvp_render_attachment {
   struct lvp_image_view *imgv;
   VkResolveModeFlags resolve_mode;
   struct lvp_image_view *resolve_imgv;
   VkAttachmentLoadOp load_op;
   VkClearValue clear_value;
};

struct rendering_state {
   struct pipe_context *pctx;
   struct u_upload_mgr *uploader;
   struct cso_context *cso;

   bool blend_dirty;
   bool rs_dirty;
   bool dsa_dirty;
   bool stencil_ref_dirty;
   bool clip_state_dirty;
   bool blend_color_dirty;
   bool ve_dirty;
   bool vb_dirty;
   bool constbuf_dirty[PIPE_SHADER_TYPES];
   bool pcbuf_dirty[PIPE_SHADER_TYPES];
   bool has_pcbuf[PIPE_SHADER_TYPES];
   bool inlines_dirty[PIPE_SHADER_TYPES];
   bool vp_dirty;
   bool scissor_dirty;
   bool ib_dirty;
   bool sample_mask_dirty;
   bool min_samples_dirty;
   struct pipe_draw_indirect_info indirect_info;
   struct pipe_draw_info info;

   struct pipe_grid_info dispatch_info;
   struct pipe_framebuffer_state framebuffer;

   struct pipe_blend_state blend_state;
   struct {
      float offset_units;
      float offset_scale;
      float offset_clamp;
      bool enabled;
   } depth_bias;
   struct pipe_rasterizer_state rs_state;
   struct pipe_depth_stencil_alpha_state dsa_state;

   struct pipe_blend_color blend_color;
   struct pipe_stencil_ref stencil_ref;
   struct pipe_clip_state clip_state;

   int num_scissors;
   struct pipe_scissor_state scissors[16];

   int num_viewports;
   struct pipe_viewport_state viewports[16];
   struct {
      float min, max;
   } depth[16];

   uint8_t patch_vertices;
   ubyte index_size;
   unsigned index_offset;
   struct pipe_resource *index_buffer;
   struct pipe_constant_buffer const_buffer[PIPE_SHADER_TYPES][16];
   int num_const_bufs[PIPE_SHADER_TYPES];
   int num_vb;
   unsigned start_vb;
   struct pipe_vertex_buffer vb[PIPE_MAX_ATTRIBS];
   struct cso_velems_state velem;

   struct lvp_access_info access[MESA_SHADER_STAGES];
   struct pipe_sampler_view *sv[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_SAMPLER_VIEWS];
   int num_sampler_views[PIPE_SHADER_TYPES];
   struct pipe_sampler_state ss[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   /* cso_context api is stupid */
   const struct pipe_sampler_state *cso_ss_ptr[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   int num_sampler_states[PIPE_SHADER_TYPES];
   bool sv_dirty[PIPE_SHADER_TYPES];
   bool ss_dirty[PIPE_SHADER_TYPES];

   struct pipe_image_view iv[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_IMAGES];
   int num_shader_images[PIPE_SHADER_TYPES];
   struct pipe_shader_buffer sb[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_BUFFERS];
   int num_shader_buffers[PIPE_SHADER_TYPES];
   bool iv_dirty[PIPE_SHADER_TYPES];
   bool sb_dirty[PIPE_SHADER_TYPES];
   bool disable_multisample;
   enum gs_output gs_output_lines : 2;

   uint32_t color_write_disables:8;
   uint32_t pad:13;

   void *velems_cso;

   uint8_t push_constants[128 * 4];
   uint16_t push_size[2]; //gfx, compute
   struct {
      void *block[MAX_PER_STAGE_DESCRIPTOR_UNIFORM_BLOCKS * MAX_SETS];
      uint16_t size[MAX_PER_STAGE_DESCRIPTOR_UNIFORM_BLOCKS * MAX_SETS];
      uint16_t count;
   } uniform_blocks[PIPE_SHADER_TYPES];

   VkRect2D render_area;
   bool suspending;
   uint32_t color_att_count;
   struct lvp_render_attachment *color_att;
   struct lvp_render_attachment depth_att;
   struct lvp_render_attachment stencil_att;
   struct lvp_image_view *ds_imgv;
   struct lvp_image_view *ds_resolve_imgv;
   uint32_t                                     forced_sample_count;
   VkResolveModeFlagBits                        forced_depth_resolve_mode;
   VkResolveModeFlagBits                        forced_stencil_resolve_mode;

   uint32_t sample_mask;
   unsigned min_samples;

   uint32_t num_so_targets;
   struct pipe_stream_output_target *so_targets[PIPE_MAX_SO_BUFFERS];
   uint32_t so_offsets[PIPE_MAX_SO_BUFFERS];

   struct lvp_pipeline *pipeline[2];
};

ALWAYS_INLINE static void
assert_subresource_layers(const struct pipe_resource *pres, const VkImageSubresourceLayers *layers, const VkOffset3D *offsets)
{
#ifndef NDEBUG
   if (pres->target == PIPE_TEXTURE_3D) {
      assert(layers->baseArrayLayer == 0);
      assert(layers->layerCount == 1);
      assert(offsets[0].z <= pres->depth0);
      assert(offsets[1].z <= pres->depth0);
   } else {
      assert(layers->baseArrayLayer < pres->array_size);
      assert(layers->baseArrayLayer + layers->layerCount <= pres->array_size);
      assert(offsets[0].z == 0);
      assert(offsets[1].z == 1);
   }
#endif
}

static void finish_fence(struct rendering_state *state)
{
   struct pipe_fence_handle *handle = NULL;

   state->pctx->flush(state->pctx, &handle, 0);

   state->pctx->screen->fence_finish(state->pctx->screen,
                                     NULL,
                                     handle, PIPE_TIMEOUT_INFINITE);
   state->pctx->screen->fence_reference(state->pctx->screen,
                                        &handle, NULL);
}

static unsigned
get_pcbuf_size(struct rendering_state *state, enum pipe_shader_type pstage)
{
   bool is_compute = pstage == PIPE_SHADER_COMPUTE;
   return state->has_pcbuf[pstage] ? state->push_size[is_compute] : 0;
}

static unsigned
calc_ubo0_size(struct rendering_state *state, enum pipe_shader_type pstage)
{
   unsigned size = get_pcbuf_size(state, pstage);
   for (unsigned i = 0; i < state->uniform_blocks[pstage].count; i++)
      size += state->uniform_blocks[pstage].size[i];
   return size;
}

static void
fill_ubo0(struct rendering_state *state, uint8_t *mem, enum pipe_shader_type pstage)
{
   unsigned push_size = get_pcbuf_size(state, pstage);
   if (push_size)
      memcpy(mem, state->push_constants, push_size);

   mem += push_size;
   for (unsigned i = 0; i < state->uniform_blocks[pstage].count; i++) {
      unsigned size = state->uniform_blocks[pstage].size[i];
      memcpy(mem, state->uniform_blocks[pstage].block[i], size);
      mem += size;
   }
}

static void
update_pcbuf(struct rendering_state *state, enum pipe_shader_type pstage)
{
   uint8_t *mem;
   struct pipe_constant_buffer cbuf;
   unsigned size = calc_ubo0_size(state, pstage);
   cbuf.buffer_size = size;
   cbuf.buffer = NULL;
   cbuf.user_buffer = NULL;
   u_upload_alloc(state->uploader, 0, size, 64, &cbuf.buffer_offset, &cbuf.buffer, (void**)&mem);
   fill_ubo0(state, mem, pstage);
   state->pctx->set_constant_buffer(state->pctx, pstage, 0, true, &cbuf);
   state->pcbuf_dirty[pstage] = false;
}

static void
update_inline_shader_state(struct rendering_state *state, enum pipe_shader_type sh, bool pcbuf_dirty, bool constbuf_dirty)
{
   bool is_compute = sh == PIPE_SHADER_COMPUTE;
   uint32_t inline_uniforms[MAX_INLINABLE_UNIFORMS];
   unsigned stage = tgsi_processor_to_shader_stage(sh);
   state->inlines_dirty[sh] = false;
   if (!state->pipeline[is_compute]->inlines[stage].can_inline)
      return;
   struct lvp_pipeline *pipeline = state->pipeline[is_compute];
   /* these buffers have already been flushed in llvmpipe, so they're safe to read */
   nir_shader *nir = nir_shader_clone(pipeline->pipeline_nir[stage], pipeline->pipeline_nir[stage]);
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   unsigned ssa_alloc = impl->ssa_alloc;
   unsigned count = pipeline->inlines[stage].count[0];
   if (count && pcbuf_dirty) {
      unsigned push_size = get_pcbuf_size(state, sh);
      for (unsigned i = 0; i < count; i++) {
         unsigned offset = pipeline->inlines[stage].uniform_offsets[0][i];
         if (offset < push_size) {
            memcpy(&inline_uniforms[i], &state->push_constants[offset], sizeof(uint32_t));
         } else {
            for (unsigned i = 0; i < state->uniform_blocks[sh].count; i++) {
               if (offset < push_size + state->uniform_blocks[sh].size[i]) {
                  unsigned ubo_offset = offset - push_size;
                  uint8_t *block = state->uniform_blocks[sh].block[i];
                  memcpy(&inline_uniforms[i], &block[ubo_offset], sizeof(uint32_t));
                  break;
               }
               push_size += state->uniform_blocks[sh].size[i];
            }
         }
      }
      NIR_PASS_V(nir, lvp_inline_uniforms, pipeline, inline_uniforms, 0);
   }
   if (constbuf_dirty) {
      struct pipe_box box = {0};
      u_foreach_bit(slot, pipeline->inlines[stage].can_inline) {
         unsigned count = pipeline->inlines[stage].count[slot];
         struct pipe_constant_buffer *cbuf = &state->const_buffer[sh][slot - 1];
         struct pipe_resource *pres = cbuf->buffer;
         box.x = cbuf->buffer_offset;
         box.width = cbuf->buffer_size - cbuf->buffer_offset;
         struct pipe_transfer *xfer;
         uint8_t *map = state->pctx->buffer_map(state->pctx, pres, 0, PIPE_MAP_READ, &box, &xfer);
         for (unsigned i = 0; i < count; i++) {
            unsigned offset = pipeline->inlines[stage].uniform_offsets[slot][i];
            memcpy(&inline_uniforms[i], map + offset, sizeof(uint32_t));
         }
         state->pctx->buffer_unmap(state->pctx, xfer);
         NIR_PASS_V(nir, lvp_inline_uniforms, pipeline, inline_uniforms, slot);
      }
   }
   lvp_shader_optimize(nir);
   impl = nir_shader_get_entrypoint(nir);
   void *shader_state;
   if (ssa_alloc - impl->ssa_alloc < ssa_alloc / 2 &&
       !pipeline->inlines[stage].must_inline) {
      /* not enough change; don't inline further */
      pipeline->inlines[stage].can_inline = 0;
      ralloc_free(nir);
      pipeline->shader_cso[sh] = lvp_pipeline_compile(pipeline, nir_shader_clone(NULL, pipeline->pipeline_nir[stage]));
      shader_state = pipeline->shader_cso[sh];
   } else {
      shader_state = lvp_pipeline_compile(pipeline, nir);
   }
   switch (sh) {
   case PIPE_SHADER_VERTEX:
      state->pctx->bind_vs_state(state->pctx, shader_state);
      break;
   case PIPE_SHADER_TESS_CTRL:
      state->pctx->bind_tcs_state(state->pctx, shader_state);
      break;
   case PIPE_SHADER_TESS_EVAL:
      state->pctx->bind_tes_state(state->pctx, shader_state);
      break;
   case PIPE_SHADER_GEOMETRY:
      state->pctx->bind_gs_state(state->pctx, shader_state);
      break;
   case PIPE_SHADER_FRAGMENT:
      state->pctx->bind_fs_state(state->pctx, shader_state);
      break;
   case PIPE_SHADER_COMPUTE:
      state->pctx->bind_compute_state(state->pctx, shader_state);
      break;
   default: break;
   }
}

static void emit_compute_state(struct rendering_state *state)
{
   if (state->iv_dirty[PIPE_SHADER_COMPUTE]) {
      state->pctx->set_shader_images(state->pctx, PIPE_SHADER_COMPUTE,
                                     0, state->num_shader_images[PIPE_SHADER_COMPUTE],
                                     0, state->iv[PIPE_SHADER_COMPUTE]);
      state->iv_dirty[PIPE_SHADER_COMPUTE] = false;
   }

   bool pcbuf_dirty = state->pcbuf_dirty[PIPE_SHADER_COMPUTE];
   if (state->pcbuf_dirty[PIPE_SHADER_COMPUTE])
      update_pcbuf(state, PIPE_SHADER_COMPUTE);

   bool constbuf_dirty = state->constbuf_dirty[PIPE_SHADER_COMPUTE];
   if (state->constbuf_dirty[PIPE_SHADER_COMPUTE]) {
      for (unsigned i = 0; i < state->num_const_bufs[PIPE_SHADER_COMPUTE]; i++)
         state->pctx->set_constant_buffer(state->pctx, PIPE_SHADER_COMPUTE,
                                          i + 1, false, &state->const_buffer[PIPE_SHADER_COMPUTE][i]);
      state->constbuf_dirty[PIPE_SHADER_COMPUTE] = false;
   }

   if (state->inlines_dirty[PIPE_SHADER_COMPUTE])
      update_inline_shader_state(state, PIPE_SHADER_COMPUTE, pcbuf_dirty, constbuf_dirty);

   if (state->sb_dirty[PIPE_SHADER_COMPUTE]) {
      state->pctx->set_shader_buffers(state->pctx, PIPE_SHADER_COMPUTE,
                                      0, state->num_shader_buffers[PIPE_SHADER_COMPUTE],
                                      state->sb[PIPE_SHADER_COMPUTE], 0);
      state->sb_dirty[PIPE_SHADER_COMPUTE] = false;
   }

   if (state->sv_dirty[PIPE_SHADER_COMPUTE]) {
      state->pctx->set_sampler_views(state->pctx, PIPE_SHADER_COMPUTE, 0, state->num_sampler_views[PIPE_SHADER_COMPUTE],
                                     0, false, state->sv[PIPE_SHADER_COMPUTE]);
      state->sv_dirty[PIPE_SHADER_COMPUTE] = false;
   }

   if (state->ss_dirty[PIPE_SHADER_COMPUTE]) {
      cso_set_samplers(state->cso, PIPE_SHADER_COMPUTE, state->num_sampler_states[PIPE_SHADER_COMPUTE], state->cso_ss_ptr[PIPE_SHADER_COMPUTE]);
      state->ss_dirty[PIPE_SHADER_COMPUTE] = false;
   }
}

static void emit_state(struct rendering_state *state)
{
   int sh;
   if (state->blend_dirty) {
      uint32_t mask = 0;
      /* zero out the colormask values for disabled attachments */
      if (state->color_write_disables) {
         u_foreach_bit(att, state->color_write_disables) {
            mask |= state->blend_state.rt[att].colormask << (att * 4);
            state->blend_state.rt[att].colormask = 0;
         }
      }
      cso_set_blend(state->cso, &state->blend_state);
      /* reset colormasks using saved bitmask */
      if (state->color_write_disables) {
         const uint32_t att_mask = BITFIELD_MASK(4);
         u_foreach_bit(att, state->color_write_disables) {
            state->blend_state.rt[att].colormask = (mask >> (att * 4)) & att_mask;
         }
      }
      state->blend_dirty = false;
   }

   if (state->rs_dirty) {
      bool ms = state->rs_state.multisample;
      if (state->disable_multisample &&
          (state->gs_output_lines == GS_OUTPUT_LINES ||
           (state->gs_output_lines == GS_OUTPUT_NONE && u_reduced_prim(state->info.mode) == PIPE_PRIM_LINES)))
         state->rs_state.multisample = false;
      assert(offsetof(struct pipe_rasterizer_state, offset_clamp) - offsetof(struct pipe_rasterizer_state, offset_units) == sizeof(float) * 2);
      if (state->depth_bias.enabled) {
         memcpy(&state->rs_state.offset_units, &state->depth_bias, sizeof(float) * 3);
         state->rs_state.offset_tri = true;
         state->rs_state.offset_line = true;
         state->rs_state.offset_point = true;
      } else {
         memset(&state->rs_state.offset_units, 0, sizeof(float) * 3);
         state->rs_state.offset_tri = false;
         state->rs_state.offset_line = false;
         state->rs_state.offset_point = false;
      }
      cso_set_rasterizer(state->cso, &state->rs_state);
      state->rs_dirty = false;
      state->rs_state.multisample = ms;
   }

   if (state->dsa_dirty) {
      cso_set_depth_stencil_alpha(state->cso, &state->dsa_state);
      state->dsa_dirty = false;
   }

   if (state->sample_mask_dirty) {
      cso_set_sample_mask(state->cso, state->sample_mask);
      state->sample_mask_dirty = false;
   }

   if (state->min_samples_dirty) {
      cso_set_min_samples(state->cso, state->min_samples);
      state->min_samples_dirty = false;
   }

   if (state->blend_color_dirty) {
      state->pctx->set_blend_color(state->pctx, &state->blend_color);
      state->blend_color_dirty = false;
   }

   if (state->stencil_ref_dirty) {
      cso_set_stencil_ref(state->cso, state->stencil_ref);
      state->stencil_ref_dirty = false;
   }

   if (state->vb_dirty) {
      cso_set_vertex_buffers(state->cso, state->start_vb, state->num_vb, 0, false, state->vb);
      state->vb_dirty = false;
   }

   if (state->ve_dirty) {
      cso_set_vertex_elements(state->cso, &state->velem);
      state->ve_dirty = false;
   }

   bool constbuf_dirty[PIPE_SHADER_TYPES] = {false};
   bool pcbuf_dirty[PIPE_SHADER_TYPES] = {false};
   for (sh = 0; sh < PIPE_SHADER_COMPUTE; sh++) {
      constbuf_dirty[sh] = state->constbuf_dirty[sh];
      if (state->constbuf_dirty[sh]) {
         for (unsigned idx = 0; idx < state->num_const_bufs[sh]; idx++)
            state->pctx->set_constant_buffer(state->pctx, sh,
                                             idx + 1, false, &state->const_buffer[sh][idx]);
      }
      state->constbuf_dirty[sh] = false;
   }

   for (sh = 0; sh < PIPE_SHADER_COMPUTE; sh++) {
      pcbuf_dirty[sh] = state->pcbuf_dirty[sh];
      if (state->pcbuf_dirty[sh])
         update_pcbuf(state, sh);
   }

   for (sh = 0; sh < PIPE_SHADER_COMPUTE; sh++) {
      if (state->inlines_dirty[sh])
         update_inline_shader_state(state, sh, pcbuf_dirty[sh], constbuf_dirty[sh]);
   }

   for (sh = 0; sh < PIPE_SHADER_COMPUTE; sh++) {
      if (state->sb_dirty[sh]) {
         state->pctx->set_shader_buffers(state->pctx, sh,
                                         0, state->num_shader_buffers[sh],
                                         state->sb[sh], state->access[tgsi_processor_to_shader_stage(sh)].buffers_written);
      }
   }

   for (sh = 0; sh < PIPE_SHADER_COMPUTE; sh++) {
      if (state->iv_dirty[sh]) {
         state->pctx->set_shader_images(state->pctx, sh,
                                        0, state->num_shader_images[sh], 0,
                                        state->iv[sh]);
      }
   }

   for (sh = 0; sh < PIPE_SHADER_COMPUTE; sh++) {

      if (!state->sv_dirty[sh])
         continue;

      state->pctx->set_sampler_views(state->pctx, sh, 0, state->num_sampler_views[sh],
                                     0, false, state->sv[sh]);
      state->sv_dirty[sh] = false;
   }

   for (sh = 0; sh < PIPE_SHADER_COMPUTE; sh++) {
      if (!state->ss_dirty[sh])
         continue;

      cso_set_samplers(state->cso, sh, state->num_sampler_states[sh], state->cso_ss_ptr[sh]);
      state->ss_dirty[sh] = false;
   }

   if (state->vp_dirty) {
      state->pctx->set_viewport_states(state->pctx, 0, state->num_viewports, state->viewports);
      state->vp_dirty = false;
   }

   if (state->scissor_dirty) {
      state->pctx->set_scissor_states(state->pctx, 0, state->num_scissors, state->scissors);
      state->scissor_dirty = false;
   }
}

static void handle_compute_pipeline(struct vk_cmd_queue_entry *cmd,
                                    struct rendering_state *state)
{
   LVP_FROM_HANDLE(lvp_pipeline, pipeline, cmd->u.bind_pipeline.pipeline);

   if ((pipeline->layout->push_constant_stages & VK_SHADER_STAGE_COMPUTE_BIT) > 0)
      state->has_pcbuf[PIPE_SHADER_COMPUTE] = pipeline->layout->push_constant_size > 0;
   state->uniform_blocks[PIPE_SHADER_COMPUTE].count = pipeline->layout->stage[MESA_SHADER_COMPUTE].uniform_block_count;
   for (unsigned j = 0; j < pipeline->layout->stage[MESA_SHADER_COMPUTE].uniform_block_count; j++)
      state->uniform_blocks[PIPE_SHADER_COMPUTE].size[j] = pipeline->layout->stage[MESA_SHADER_COMPUTE].uniform_block_sizes[j];
   if (!state->has_pcbuf[PIPE_SHADER_COMPUTE] && !pipeline->layout->stage[MESA_SHADER_COMPUTE].uniform_block_count)
      state->pcbuf_dirty[PIPE_SHADER_COMPUTE] = false;

   state->iv_dirty[MESA_SHADER_COMPUTE] |= state->num_shader_images[MESA_SHADER_COMPUTE] &&
                          (state->access[MESA_SHADER_COMPUTE].images_read != pipeline->access[MESA_SHADER_COMPUTE].images_read ||
                           state->access[MESA_SHADER_COMPUTE].images_written != pipeline->access[MESA_SHADER_COMPUTE].images_written);
   state->sb_dirty[MESA_SHADER_COMPUTE] |= state->num_shader_buffers[MESA_SHADER_COMPUTE] &&
                                           state->access[MESA_SHADER_COMPUTE].buffers_written != pipeline->access[MESA_SHADER_COMPUTE].buffers_written;
   memcpy(&state->access[MESA_SHADER_COMPUTE], &pipeline->access[MESA_SHADER_COMPUTE], sizeof(struct lvp_access_info));

   state->dispatch_info.block[0] = pipeline->pipeline_nir[MESA_SHADER_COMPUTE]->info.workgroup_size[0];
   state->dispatch_info.block[1] = pipeline->pipeline_nir[MESA_SHADER_COMPUTE]->info.workgroup_size[1];
   state->dispatch_info.block[2] = pipeline->pipeline_nir[MESA_SHADER_COMPUTE]->info.workgroup_size[2];
   state->inlines_dirty[PIPE_SHADER_COMPUTE] = pipeline->inlines[MESA_SHADER_COMPUTE].can_inline;
   if (!pipeline->inlines[MESA_SHADER_COMPUTE].can_inline)
      state->pctx->bind_compute_state(state->pctx, pipeline->shader_cso[PIPE_SHADER_COMPUTE]);
}

static void
set_viewport_depth_xform(struct rendering_state *state, unsigned idx)
{
   double n = state->depth[idx].min;
   double f = state->depth[idx].max;

   if (!state->rs_state.clip_halfz) {
      state->viewports[idx].scale[2] = 0.5 * (f - n);
      state->viewports[idx].translate[2] = 0.5 * (n + f);
   } else {
      state->viewports[idx].scale[2] = (f - n);
      state->viewports[idx].translate[2] = n;
   }
}

static void
get_viewport_xform(struct rendering_state *state,
                   const VkViewport *viewport,
                   unsigned idx)
{
   float x = viewport->x;
   float y = viewport->y;
   float half_width = 0.5f * viewport->width;
   float half_height = 0.5f * viewport->height;

   state->viewports[idx].scale[0] = half_width;
   state->viewports[idx].translate[0] = half_width + x;
   state->viewports[idx].scale[1] = half_height;
   state->viewports[idx].translate[1] = half_height + y;

   memcpy(&state->depth[idx].min, &viewport->minDepth, sizeof(float) * 2);
}

static void handle_graphics_pipeline(struct vk_cmd_queue_entry *cmd,
                                     struct rendering_state *state)
{
   LVP_FROM_HANDLE(lvp_pipeline, pipeline, cmd->u.bind_pipeline.pipeline);
   const struct vk_graphics_pipeline_state *ps = &pipeline->graphics_state;
   unsigned fb_samples = 0;

   for (enum pipe_shader_type sh = PIPE_SHADER_VERTEX; sh < PIPE_SHADER_COMPUTE; sh++) {
      state->iv_dirty[sh] |= state->num_shader_images[sh] &&
                             (state->access[sh].images_read != pipeline->access[sh].images_read ||
                              state->access[sh].images_written != pipeline->access[sh].images_written);
      state->sb_dirty[sh] |= state->num_shader_buffers[sh] && state->access[sh].buffers_written != pipeline->access[sh].buffers_written;
   }
   memcpy(state->access, pipeline->access, sizeof(struct lvp_access_info) * 5); //4 vertex stages + fragment

   for (enum pipe_shader_type sh = PIPE_SHADER_VERTEX; sh < PIPE_SHADER_COMPUTE; sh++)
      state->has_pcbuf[sh] = false;

   for (unsigned i = 0; i < MESA_SHADER_COMPUTE; i++) {
      enum pipe_shader_type sh = pipe_shader_type_from_mesa(i);
      state->uniform_blocks[sh].count = pipeline->layout->stage[i].uniform_block_count;
      for (unsigned j = 0; j < pipeline->layout->stage[i].uniform_block_count; j++)
         state->uniform_blocks[sh].size[j] = pipeline->layout->stage[i].uniform_block_sizes[j];
   }
   u_foreach_bit(stage, pipeline->layout->push_constant_stages) {
      enum pipe_shader_type sh = pipe_shader_type_from_mesa(stage);
      state->has_pcbuf[sh] = pipeline->layout->push_constant_size > 0;
      if (!state->has_pcbuf[sh] && !state->uniform_blocks[sh].count)
         state->pcbuf_dirty[sh] = false;
   }

   bool has_stage[PIPE_SHADER_TYPES] = { false };

   state->pctx->bind_gs_state(state->pctx, NULL);
   if (state->pctx->bind_tcs_state)
      state->pctx->bind_tcs_state(state->pctx, NULL);
   if (state->pctx->bind_tes_state)
      state->pctx->bind_tes_state(state->pctx, NULL);
   state->gs_output_lines = GS_OUTPUT_NONE;
   {
      u_foreach_bit(b, pipeline->graphics_state.shader_stages) {
         VkShaderStageFlagBits vk_stage = (1 << b);
         switch (vk_stage) {
         case VK_SHADER_STAGE_FRAGMENT_BIT:
            state->inlines_dirty[PIPE_SHADER_FRAGMENT] = pipeline->inlines[MESA_SHADER_FRAGMENT].can_inline;
            if (!pipeline->inlines[MESA_SHADER_FRAGMENT].can_inline)
               state->pctx->bind_fs_state(state->pctx, pipeline->shader_cso[PIPE_SHADER_FRAGMENT]);
            has_stage[PIPE_SHADER_FRAGMENT] = true;
            break;
         case VK_SHADER_STAGE_VERTEX_BIT:
            state->inlines_dirty[PIPE_SHADER_VERTEX] = pipeline->inlines[MESA_SHADER_VERTEX].can_inline;
            if (!pipeline->inlines[MESA_SHADER_VERTEX].can_inline)
               state->pctx->bind_vs_state(state->pctx, pipeline->shader_cso[PIPE_SHADER_VERTEX]);
            has_stage[PIPE_SHADER_VERTEX] = true;
            break;
         case VK_SHADER_STAGE_GEOMETRY_BIT:
            state->inlines_dirty[PIPE_SHADER_GEOMETRY] = pipeline->inlines[MESA_SHADER_GEOMETRY].can_inline;
            if (!pipeline->inlines[MESA_SHADER_GEOMETRY].can_inline)
               state->pctx->bind_gs_state(state->pctx, pipeline->shader_cso[PIPE_SHADER_GEOMETRY]);
            state->gs_output_lines = pipeline->gs_output_lines ? GS_OUTPUT_LINES : GS_OUTPUT_NOT_LINES;
            has_stage[PIPE_SHADER_GEOMETRY] = true;
            break;
         case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
            state->inlines_dirty[PIPE_SHADER_TESS_CTRL] = pipeline->inlines[MESA_SHADER_TESS_CTRL].can_inline;
            if (!pipeline->inlines[MESA_SHADER_TESS_CTRL].can_inline)
               state->pctx->bind_tcs_state(state->pctx, pipeline->shader_cso[PIPE_SHADER_TESS_CTRL]);
            has_stage[PIPE_SHADER_TESS_CTRL] = true;
            break;
         case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
            state->inlines_dirty[PIPE_SHADER_TESS_EVAL] = pipeline->inlines[MESA_SHADER_TESS_EVAL].can_inline;
            if (!pipeline->inlines[MESA_SHADER_TESS_EVAL].can_inline)
               state->pctx->bind_tes_state(state->pctx, pipeline->shader_cso[PIPE_SHADER_TESS_EVAL]);
            has_stage[PIPE_SHADER_TESS_EVAL] = true;
            break;
         default:
            assert(0);
            break;
         }
      }
   }

   /* there should always be a dummy fs. */
   if (!has_stage[PIPE_SHADER_FRAGMENT])
      state->pctx->bind_fs_state(state->pctx, pipeline->shader_cso[PIPE_SHADER_FRAGMENT]);
   if (state->pctx->bind_gs_state && !has_stage[PIPE_SHADER_GEOMETRY])
      state->pctx->bind_gs_state(state->pctx, NULL);
   if (state->pctx->bind_tcs_state && !has_stage[PIPE_SHADER_TESS_CTRL])
      state->pctx->bind_tcs_state(state->pctx, NULL);
   if (state->pctx->bind_tes_state && !has_stage[PIPE_SHADER_TESS_EVAL])
      state->pctx->bind_tes_state(state->pctx, NULL);

   /* rasterization state */
   if (ps->rs) {
      state->rs_state.depth_clamp = ps->rs->depth_clamp_enable;
      state->rs_state.depth_clip_near = ps->rs->depth_clip_enable;

      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_RASTERIZER_DISCARD_ENABLE))
         state->rs_state.rasterizer_discard = ps->rs->rasterizer_discard_enable;

      state->rs_state.line_smooth = pipeline->line_smooth;
      state->rs_state.line_stipple_enable = ps->rs->line.stipple.enable;
      state->rs_state.fill_front = vk_polygon_mode_to_pipe(ps->rs->polygon_mode);
      state->rs_state.fill_back = vk_polygon_mode_to_pipe(ps->rs->polygon_mode);
      state->rs_state.point_size_per_vertex = true;
      state->rs_state.flatshade_first =
         ps->rs->provoking_vertex == VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT;
      state->rs_state.point_quad_rasterization = true;
      state->rs_state.half_pixel_center = true;
      state->rs_state.scissor = true;
      state->rs_state.no_ms_sample_mask_out = true;
      state->rs_state.line_rectangular = pipeline->line_rectangular;

      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_LINE_WIDTH))
         state->rs_state.line_width = ps->rs->line.width;
      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_LINE_STIPPLE)) {
         state->rs_state.line_stipple_factor = ps->rs->line.stipple.factor - 1;
         state->rs_state.line_stipple_pattern = ps->rs->line.stipple.pattern;
      }

      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_DEPTH_BIAS_ENABLE))
         state->depth_bias.enabled = ps->rs->depth_bias.enable;
      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_DEPTH_BIAS_FACTORS)) {
         state->depth_bias.offset_units = ps->rs->depth_bias.constant;
         state->depth_bias.offset_scale = ps->rs->depth_bias.slope;
         state->depth_bias.offset_clamp = ps->rs->depth_bias.clamp;
      }

      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_CULL_MODE))
         state->rs_state.cull_face = vk_cull_to_pipe(ps->rs->cull_mode);

      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_FRONT_FACE))
         state->rs_state.front_ccw = (ps->rs->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE);
      state->rs_dirty = true;
   }

   if (ps->ds) {
      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE))
         state->dsa_state.depth_enabled = ps->ds->depth.test_enable;
      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE))
         state->dsa_state.depth_writemask = ps->ds->depth.write_enable;
      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_COMPARE_OP))
         state->dsa_state.depth_func = ps->ds->depth.compare_op;
      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE))
         state->dsa_state.depth_bounds_test = ps->ds->depth.bounds_test.enable;

      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_BOUNDS)) {
         state->dsa_state.depth_bounds_min = ps->ds->depth.bounds_test.min;
         state->dsa_state.depth_bounds_max = ps->ds->depth.bounds_test.max;
      }

      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE)) {
         state->dsa_state.stencil[0].enabled = ps->ds->stencil.test_enable;
         state->dsa_state.stencil[1].enabled = ps->ds->stencil.test_enable;
      }

      const struct vk_stencil_test_face_state *front = &ps->ds->stencil.front;
      const struct vk_stencil_test_face_state *back = &ps->ds->stencil.back;

      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_OP)) {
         state->dsa_state.stencil[0].func = front->op.compare;
         state->dsa_state.stencil[0].fail_op = vk_conv_stencil_op(front->op.fail);
         state->dsa_state.stencil[0].zpass_op = vk_conv_stencil_op(front->op.pass);
         state->dsa_state.stencil[0].zfail_op = vk_conv_stencil_op(front->op.depth_fail);

         state->dsa_state.stencil[1].func = back->op.compare;
         state->dsa_state.stencil[1].fail_op = vk_conv_stencil_op(back->op.fail);
         state->dsa_state.stencil[1].zpass_op = vk_conv_stencil_op(back->op.pass);
         state->dsa_state.stencil[1].zfail_op = vk_conv_stencil_op(back->op.depth_fail);
      }

      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_COMPARE_MASK)) {
         state->dsa_state.stencil[0].valuemask = front->compare_mask;
         state->dsa_state.stencil[1].valuemask = back->compare_mask;
      }

      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_WRITE_MASK)) {
         state->dsa_state.stencil[0].writemask = front->write_mask;
         state->dsa_state.stencil[1].writemask = back->write_mask;
      }

      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_REFERENCE)) {
         state->stencil_ref.ref_value[0] = front->reference;
         state->stencil_ref.ref_value[1] = back->reference;
         state->stencil_ref_dirty = true;
      }
      state->dsa_dirty = true;
   }

   if (ps->cb) {
      state->blend_state.logicop_enable = ps->cb->logic_op_enable;
      if (ps->cb->logic_op_enable) {
         if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_CB_LOGIC_OP))
            state->blend_state.logicop_func = vk_conv_logic_op(ps->cb->logic_op);
      }

      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES))
         state->color_write_disables = ~ps->cb->color_write_enables;

      state->blend_state.independent_blend_enable = (ps->cb->attachment_count > 1);

      for (unsigned i = 0; i < ps->cb->attachment_count; i++) {
         const struct vk_color_blend_attachment_state *att = &ps->cb->attachments[i];
         state->blend_state.rt[i].colormask = att->write_mask;
         state->blend_state.rt[i].blend_enable = att->blend_enable;
         if (att->blend_enable) {
            state->blend_state.rt[i].rgb_func = vk_conv_blend_func(att->color_blend_op);
            state->blend_state.rt[i].rgb_src_factor = vk_conv_blend_factor(att->src_color_blend_factor);
            state->blend_state.rt[i].rgb_dst_factor = vk_conv_blend_factor(att->dst_color_blend_factor);
            state->blend_state.rt[i].alpha_func = vk_conv_blend_func(att->alpha_blend_op);
            state->blend_state.rt[i].alpha_src_factor = vk_conv_blend_factor(att->src_alpha_blend_factor);
            state->blend_state.rt[i].alpha_dst_factor = vk_conv_blend_factor(att->dst_alpha_blend_factor);
         } else {
            state->blend_state.rt[i].rgb_func = 0;
            state->blend_state.rt[i].rgb_src_factor = 0;
            state->blend_state.rt[i].rgb_dst_factor = 0;
            state->blend_state.rt[i].alpha_func = 0;
            state->blend_state.rt[i].alpha_src_factor = 0;
            state->blend_state.rt[i].alpha_dst_factor = 0;
         }

         /* At least llvmpipe applies the blend factor prior to the blend function,
          * regardless of what function is used. (like i965 hardware).
          * It means for MIN/MAX the blend factor has to be stomped to ONE.
          */
         if (att->color_blend_op == VK_BLEND_OP_MIN ||
             att->color_blend_op == VK_BLEND_OP_MAX) {
            state->blend_state.rt[i].rgb_src_factor = PIPE_BLENDFACTOR_ONE;
            state->blend_state.rt[i].rgb_dst_factor = PIPE_BLENDFACTOR_ONE;
         }

         if (att->alpha_blend_op == VK_BLEND_OP_MIN ||
             att->alpha_blend_op == VK_BLEND_OP_MAX) {
            state->blend_state.rt[i].alpha_src_factor = PIPE_BLENDFACTOR_ONE;
            state->blend_state.rt[i].alpha_dst_factor = PIPE_BLENDFACTOR_ONE;
         }
      }
      state->blend_dirty = true;
      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_CB_BLEND_CONSTANTS)) {
         memcpy(state->blend_color.color, ps->cb->blend_constants, 4 * sizeof(float));
         state->blend_color_dirty = true;
      }
   } else {
      memset(&state->blend_state, 0, sizeof(state->blend_state));
      state->blend_dirty = true;
   }

   state->disable_multisample = pipeline->disable_multisample;
   if (ps->ms) {
      state->rs_state.multisample = ps->ms->rasterization_samples > 1;
      state->sample_mask = ps->ms->sample_mask;
      state->blend_state.alpha_to_coverage = ps->ms->alpha_to_coverage_enable;
      state->blend_state.alpha_to_one = ps->ms->alpha_to_one_enable;
      state->blend_dirty = true;
      state->rs_dirty = true;
      state->min_samples = 1;
      state->sample_mask_dirty = true;
      fb_samples = ps->ms->rasterization_samples;
      if (ps->ms->sample_shading_enable) {
         state->min_samples = ceil(ps->ms->rasterization_samples *
                                   ps->ms->min_sample_shading);
         if (state->min_samples > 1)
            state->min_samples = ps->ms->rasterization_samples;
         if (state->min_samples < 1)
            state->min_samples = 1;
      }
      if (pipeline->force_min_sample)
         state->min_samples = ps->ms->rasterization_samples;
      state->min_samples_dirty = true;
   } else {
      state->rs_state.multisample = false;
      state->sample_mask_dirty = state->sample_mask != 0xffffffff;
      state->sample_mask = 0xffffffff;
      state->min_samples_dirty = state->min_samples;
      state->min_samples = 0;
      state->blend_dirty |= state->blend_state.alpha_to_coverage || state->blend_state.alpha_to_one;
      state->blend_state.alpha_to_coverage = false;
      state->blend_state.alpha_to_one = false;
      state->rs_dirty = true;
   }

   if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_VI_BINDING_STRIDES)) {
      u_foreach_bit(b, ps->vi->bindings_valid)
         state->vb[b].stride = ps->vi->bindings[b].stride;
      state->vb_dirty = true;
   }

   if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_VI)) {
      u_foreach_bit(a, ps->vi->attributes_valid) {
         uint32_t b = ps->vi->attributes[a].binding;
         state->velem.velems[a].src_offset = ps->vi->attributes[a].offset;
         state->velem.velems[a].vertex_buffer_index = b;
         state->velem.velems[a].src_format =
            lvp_vk_format_to_pipe_format(ps->vi->attributes[a].format);
         state->velem.velems[a].dual_slot = false;

         uint32_t d = ps->vi->bindings[b].divisor;
         switch (ps->vi->bindings[b].input_rate) {
         case VK_VERTEX_INPUT_RATE_VERTEX:
            state->velem.velems[a].instance_divisor = 0;
            break;
         case VK_VERTEX_INPUT_RATE_INSTANCE:
            state->velem.velems[a].instance_divisor = d ? d : UINT32_MAX;
            break;
         default:
            unreachable("Invalid vertex input rate");
         }
      }

      state->velem.count = util_last_bit(ps->vi->attributes_valid);
      state->vb_dirty = true;
      state->ve_dirty = true;
   }

   if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_IA_PRIMITIVE_TOPOLOGY)) {
      state->info.mode = vk_conv_topology(ps->ia->primitive_topology);
      state->rs_dirty = true;
   }
   if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE))
      state->info.primitive_restart = ps->ia->primitive_restart_enable;

   if (ps->ts && !BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_TS_PATCH_CONTROL_POINTS))
      state->patch_vertices = ps->ts->patch_control_points;

   if (ps->vp) {
      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_VP_VIEWPORT_COUNT)) {
         state->num_viewports = ps->vp->viewport_count;
         state->vp_dirty = true;
      }
      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_VP_SCISSOR_COUNT)) {
         state->num_scissors = ps->vp->scissor_count;
         state->scissor_dirty = true;
      }

      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_VP_VIEWPORTS)) {
         for (uint32_t i = 0; i < ps->vp->viewport_count; i++) {
            get_viewport_xform(state, &ps->vp->viewports[i], i);
            set_viewport_depth_xform(state, i);
         }
         state->vp_dirty = true;
      }
      if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_VP_SCISSORS)) {
         for (uint32_t i = 0; i < ps->vp->scissor_count; i++) {
            const VkRect2D *ss = &ps->vp->scissors[i];
            state->scissors[i].minx = ss->offset.x;
            state->scissors[i].miny = ss->offset.y;
            state->scissors[i].maxx = ss->offset.x + ss->extent.width;
            state->scissors[i].maxy = ss->offset.y + ss->extent.height;
         }
         state->scissor_dirty = true;
      }

      if (state->rs_state.clip_halfz != !ps->vp->negative_one_to_one) {
         state->rs_state.clip_halfz = !ps->vp->negative_one_to_one;
         state->rs_dirty = true;
         for (uint32_t i = 0; i < state->num_viewports; i++)
            set_viewport_depth_xform(state, i);
         state->vp_dirty = true;
      }
   }

   if (fb_samples != state->framebuffer.samples) {
      state->framebuffer.samples = fb_samples;
      state->pctx->set_framebuffer_state(state->pctx, &state->framebuffer);
   }
}

static void
handle_pipeline_access(struct rendering_state *state, gl_shader_stage stage)
{
   enum pipe_shader_type pstage = pipe_shader_type_from_mesa(stage);
   for (unsigned i = 0; i < PIPE_MAX_SHADER_IMAGES; i++) {
      state->iv[pstage][i].access = 0;
      state->iv[pstage][i].shader_access = 0;
   }
   u_foreach_bit(idx, state->access[stage].images_read) {
      state->iv[pstage][idx].access |= PIPE_IMAGE_ACCESS_READ;
      state->iv[pstage][idx].shader_access |= PIPE_IMAGE_ACCESS_READ;
   }
   u_foreach_bit(idx, state->access[stage].images_written) {
      state->iv[pstage][idx].access |= PIPE_IMAGE_ACCESS_WRITE;
      state->iv[pstage][idx].shader_access |= PIPE_IMAGE_ACCESS_WRITE;
   }
}

static void handle_pipeline(struct vk_cmd_queue_entry *cmd,
                            struct rendering_state *state)
{
   LVP_FROM_HANDLE(lvp_pipeline, pipeline, cmd->u.bind_pipeline.pipeline);
   if (pipeline->is_compute_pipeline) {
      handle_compute_pipeline(cmd, state);
      handle_pipeline_access(state, MESA_SHADER_COMPUTE);
   } else {
      handle_graphics_pipeline(cmd, state);
      for (unsigned i = 0; i < MESA_SHADER_COMPUTE; i++)
         handle_pipeline_access(state, i);
   }
   state->push_size[pipeline->is_compute_pipeline] = pipeline->layout->push_constant_size;
   state->pipeline[pipeline->is_compute_pipeline] = pipeline;
}

static void handle_vertex_buffers2(struct vk_cmd_queue_entry *cmd,
                                   struct rendering_state *state)
{
   struct vk_cmd_bind_vertex_buffers2 *vcb = &cmd->u.bind_vertex_buffers2;

   int i;
   for (i = 0; i < vcb->binding_count; i++) {
      int idx = i + vcb->first_binding;

      state->vb[idx].buffer_offset = vcb->offsets[i];
      state->vb[idx].buffer.resource =
         vcb->buffers[i] ? lvp_buffer_from_handle(vcb->buffers[i])->bo : NULL;

      if (vcb->strides)
         state->vb[idx].stride = vcb->strides[i];
   }
   if (vcb->first_binding < state->start_vb)
      state->start_vb = vcb->first_binding;
   if (vcb->first_binding + vcb->binding_count >= state->num_vb)
      state->num_vb = vcb->first_binding + vcb->binding_count;
   state->vb_dirty = true;
}

struct dyn_info {
   struct {
      uint16_t const_buffer_count;
      uint16_t shader_buffer_count;
      uint16_t sampler_count;
      uint16_t sampler_view_count;
      uint16_t image_count;
      uint16_t uniform_block_count;
   } stage[MESA_SHADER_STAGES];

   uint32_t dyn_index;
   const uint32_t *dynamic_offsets;
   uint32_t dynamic_offset_count;
};

static void fill_sampler_stage(struct rendering_state *state,
                               struct dyn_info *dyn_info,
                               gl_shader_stage stage,
                               enum pipe_shader_type p_stage,
                               int array_idx,
                               const union lvp_descriptor_info *descriptor,
                               const struct lvp_descriptor_set_binding_layout *binding)
{
   int ss_idx = binding->stage[stage].sampler_index;
   if (ss_idx == -1)
      return;
   ss_idx += array_idx;
   ss_idx += dyn_info->stage[stage].sampler_count;
   struct pipe_sampler_state *ss = binding->immutable_samplers ? binding->immutable_samplers[array_idx] : descriptor->sampler;
   state->ss[p_stage][ss_idx] = *ss;
   if (state->num_sampler_states[p_stage] <= ss_idx)
      state->num_sampler_states[p_stage] = ss_idx + 1;
   state->ss_dirty[p_stage] = true;
}

static void fill_sampler_view_stage(struct rendering_state *state,
                                    struct dyn_info *dyn_info,
                                    gl_shader_stage stage,
                                    enum pipe_shader_type p_stage,
                                    int array_idx,
                                    const union lvp_descriptor_info *descriptor,
                                    const struct lvp_descriptor_set_binding_layout *binding)
{
   int sv_idx = binding->stage[stage].sampler_view_index;
   if (sv_idx == -1)
      return;
   sv_idx += array_idx;
   sv_idx += dyn_info->stage[stage].sampler_view_count;

   assert(sv_idx < ARRAY_SIZE(state->sv[p_stage]));
   state->sv[p_stage][sv_idx] = descriptor->sampler_view;

   if (state->num_sampler_views[p_stage] <= sv_idx)
      state->num_sampler_views[p_stage] = sv_idx + 1;
   state->sv_dirty[p_stage] = true;
}

static void fill_image_view_stage(struct rendering_state *state,
                                  struct dyn_info *dyn_info,
                                  gl_shader_stage stage,
                                  enum pipe_shader_type p_stage,
                                  int array_idx,
                                  const union lvp_descriptor_info *descriptor,
                                  const struct lvp_descriptor_set_binding_layout *binding)
{
   int idx = binding->stage[stage].image_index;
   if (idx == -1)
      return;
   idx += array_idx;
   idx += dyn_info->stage[stage].image_count;
   uint16_t access = state->iv[p_stage][idx].access;
   uint16_t shader_access = state->iv[p_stage][idx].shader_access;
   state->iv[p_stage][idx] = descriptor->image_view;
   state->iv[p_stage][idx].access = access;
   state->iv[p_stage][idx].shader_access = shader_access;

   if (state->num_shader_images[p_stage] <= idx)
      state->num_shader_images[p_stage] = idx + 1;

   state->iv_dirty[p_stage] = true;
}

static void handle_descriptor(struct rendering_state *state,
                              struct dyn_info *dyn_info,
                              const struct lvp_descriptor_set_binding_layout *binding,
                              gl_shader_stage stage,
                              enum pipe_shader_type p_stage,
                              int array_idx,
                              VkDescriptorType type,
                              const union lvp_descriptor_info *descriptor)
{
   bool is_dynamic = type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
      type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;

   switch (type) {
   case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: {
      int idx = binding->stage[stage].uniform_block_index;
      if (idx == -1)
         return;
      idx += dyn_info->stage[stage].uniform_block_count;
      assert(descriptor->uniform);
      state->uniform_blocks[p_stage].block[idx] = descriptor->uniform;
      state->pcbuf_dirty[p_stage] = true;
      state->inlines_dirty[p_stage] = true;
      break;
   }
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
      fill_image_view_stage(state, dyn_info, stage, p_stage, array_idx, descriptor, binding);
      break;
   }
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: {
      int idx = binding->stage[stage].const_buffer_index;
      if (idx == -1)
         return;
      idx += array_idx;
      idx += dyn_info->stage[stage].const_buffer_count;
      state->const_buffer[p_stage][idx] = descriptor->ubo;
      if (is_dynamic) {
         uint32_t offset = dyn_info->dynamic_offsets[dyn_info->dyn_index + binding->dynamic_index + array_idx];
         state->const_buffer[p_stage][idx].buffer_offset += offset;
      }
      if (state->num_const_bufs[p_stage] <= idx)
         state->num_const_bufs[p_stage] = idx + 1;
      state->constbuf_dirty[p_stage] = true;
      state->inlines_dirty[p_stage] = true;
      break;
   }
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
      int idx = binding->stage[stage].shader_buffer_index;
      if (idx == -1)
         return;
      idx += array_idx;
      idx += dyn_info->stage[stage].shader_buffer_count;
      state->sb[p_stage][idx] = descriptor->ssbo;
      if (is_dynamic) {
         uint32_t offset = dyn_info->dynamic_offsets[dyn_info->dyn_index + binding->dynamic_index + array_idx];
         state->sb[p_stage][idx].buffer_offset += offset;
      }
      if (state->num_shader_buffers[p_stage] <= idx)
         state->num_shader_buffers[p_stage] = idx + 1;
      state->sb_dirty[p_stage] = true;
      break;
   }
   case VK_DESCRIPTOR_TYPE_SAMPLER:
      if (!descriptor->sampler)
         return;
      fill_sampler_stage(state, dyn_info, stage, p_stage, array_idx, descriptor, binding);
      break;
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      fill_sampler_view_stage(state, dyn_info, stage, p_stage, array_idx, descriptor, binding);
      break;
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      fill_sampler_stage(state, dyn_info, stage, p_stage, array_idx, descriptor, binding);
      fill_sampler_view_stage(state, dyn_info, stage, p_stage, array_idx, descriptor, binding);
      break;
   default:
      fprintf(stderr, "Unhandled descriptor set %d\n", type);
      unreachable("oops");
      break;
   }
}

static void handle_set_stage(struct rendering_state *state,
                             struct dyn_info *dyn_info,
                             const struct lvp_descriptor_set *set,
                             gl_shader_stage stage,
                             enum pipe_shader_type p_stage)
{
   int j;
   for (j = 0; j < set->layout->binding_count; j++) {
      const struct lvp_descriptor_set_binding_layout *binding;
      const struct lvp_descriptor *descriptor;
      binding = &set->layout->binding[j];

      if (binding->valid) {
         unsigned array_size = binding->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK ? 1 : binding->array_size;
         for (int i = 0; i < array_size; i++) {
            descriptor = &set->descriptors[binding->descriptor_index + i];
            handle_descriptor(state, dyn_info, binding, stage, p_stage, i, descriptor->type, &descriptor->info);
         }
      }
   }
}

static void increment_dyn_info(struct dyn_info *dyn_info,
                               const struct vk_descriptor_set_layout *vk_layout,
                               bool inc_dyn)
{
   const struct lvp_descriptor_set_layout *layout =
      vk_to_lvp_descriptor_set_layout(vk_layout);

   for (gl_shader_stage stage = MESA_SHADER_VERTEX; stage < MESA_SHADER_STAGES; stage++) {
      dyn_info->stage[stage].const_buffer_count += layout->stage[stage].const_buffer_count;
      dyn_info->stage[stage].shader_buffer_count += layout->stage[stage].shader_buffer_count;
      dyn_info->stage[stage].sampler_count += layout->stage[stage].sampler_count;
      dyn_info->stage[stage].sampler_view_count += layout->stage[stage].sampler_view_count;
      dyn_info->stage[stage].image_count += layout->stage[stage].image_count;
      dyn_info->stage[stage].uniform_block_count += layout->stage[stage].uniform_block_count;
   }
   if (inc_dyn)
      dyn_info->dyn_index += layout->dynamic_offset_count;
}

static void handle_compute_descriptor_sets(struct vk_cmd_queue_entry *cmd,
                                           struct dyn_info *dyn_info,
                                           struct rendering_state *state)
{
   struct vk_cmd_bind_descriptor_sets *bds = &cmd->u.bind_descriptor_sets;
   LVP_FROM_HANDLE(lvp_pipeline_layout, layout, bds->layout);
   int i;

   for (i = 0; i < bds->first_set; i++) {
      increment_dyn_info(dyn_info, layout->vk.set_layouts[i], false);
   }
   for (i = 0; i < bds->descriptor_set_count; i++) {
      const struct lvp_descriptor_set *set = lvp_descriptor_set_from_handle(bds->descriptor_sets[i]);

      if (set->layout->shader_stages & VK_SHADER_STAGE_COMPUTE_BIT)
         handle_set_stage(state, dyn_info, set, MESA_SHADER_COMPUTE, PIPE_SHADER_COMPUTE);
      increment_dyn_info(dyn_info, layout->vk.set_layouts[bds->first_set + i], true);
   }
}

static void handle_descriptor_sets(struct vk_cmd_queue_entry *cmd,
                                   struct rendering_state *state)
{
   struct vk_cmd_bind_descriptor_sets *bds = &cmd->u.bind_descriptor_sets;
   LVP_FROM_HANDLE(lvp_pipeline_layout, layout, bds->layout);
   int i;
   struct dyn_info dyn_info;

   dyn_info.dyn_index = 0;
   dyn_info.dynamic_offsets = bds->dynamic_offsets;
   dyn_info.dynamic_offset_count = bds->dynamic_offset_count;

   memset(dyn_info.stage, 0, sizeof(dyn_info.stage));
   if (bds->pipeline_bind_point == VK_PIPELINE_BIND_POINT_COMPUTE) {
      handle_compute_descriptor_sets(cmd, &dyn_info, state);
      return;
   }

   for (i = 0; i < bds->first_set; i++) {
      increment_dyn_info(&dyn_info, layout->vk.set_layouts[i], false);
   }

   for (i = 0; i < bds->descriptor_set_count; i++) {
      if (!layout->vk.set_layouts[bds->first_set + i])
         continue;

      const struct lvp_descriptor_set *set = lvp_descriptor_set_from_handle(bds->descriptor_sets[i]);
      if (!set)
         continue;
      /* verify that there's enough total offsets */
      assert(set->layout->dynamic_offset_count <= dyn_info.dynamic_offset_count);
      /* verify there's either no offsets... */
      assert(!dyn_info.dynamic_offset_count ||
             /* or that the total number of offsets required is <= the number remaining */
             set->layout->dynamic_offset_count <= dyn_info.dynamic_offset_count - dyn_info.dyn_index);

      if (set->layout->shader_stages & VK_SHADER_STAGE_VERTEX_BIT)
         handle_set_stage(state, &dyn_info, set, MESA_SHADER_VERTEX, PIPE_SHADER_VERTEX);

      if (set->layout->shader_stages & VK_SHADER_STAGE_GEOMETRY_BIT)
         handle_set_stage(state, &dyn_info, set, MESA_SHADER_GEOMETRY, PIPE_SHADER_GEOMETRY);

      if (set->layout->shader_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
         handle_set_stage(state, &dyn_info, set, MESA_SHADER_TESS_CTRL, PIPE_SHADER_TESS_CTRL);

      if (set->layout->shader_stages & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
         handle_set_stage(state, &dyn_info, set, MESA_SHADER_TESS_EVAL, PIPE_SHADER_TESS_EVAL);

      if (set->layout->shader_stages & VK_SHADER_STAGE_FRAGMENT_BIT)
         handle_set_stage(state, &dyn_info, set, MESA_SHADER_FRAGMENT, PIPE_SHADER_FRAGMENT);

      increment_dyn_info(&dyn_info, layout->vk.set_layouts[bds->first_set + i], true);
   }
}

static struct pipe_surface *create_img_surface_bo(struct rendering_state *state,
                                                  VkImageSubresourceRange *range,
                                                  struct pipe_resource *bo,
                                                  enum pipe_format pformat,
                                                  int width,
                                                  int height,
                                                  int base_layer, int layer_count,
                                                  int level)
{
   struct pipe_surface template;

   memset(&template, 0, sizeof(struct pipe_surface));

   template.format = pformat;
   template.width = width;
   template.height = height;
   template.u.tex.first_layer = range->baseArrayLayer + base_layer;
   template.u.tex.last_layer = range->baseArrayLayer + layer_count;
   template.u.tex.level = range->baseMipLevel + level;

   if (template.format == PIPE_FORMAT_NONE)
      return NULL;
   return state->pctx->create_surface(state->pctx,
                                      bo, &template);

}
static struct pipe_surface *create_img_surface(struct rendering_state *state,
                                               struct lvp_image_view *imgv,
                                               VkFormat format, int width,
                                               int height,
                                               int base_layer, int layer_count)
{
   VkImageSubresourceRange imgv_subres =
      vk_image_view_subresource_range(&imgv->vk);

   return create_img_surface_bo(state, &imgv_subres, imgv->image->bo,
                                lvp_vk_format_to_pipe_format(format),
                                width, height, base_layer, layer_count, 0);
}

static void add_img_view_surface(struct rendering_state *state,
                                 struct lvp_image_view *imgv, int width, int height)
{
   if (!imgv->surface) {
      imgv->surface = create_img_surface(state, imgv, imgv->vk.format,
                                         width, height,
                                         0, imgv->vk.layer_count - 1);
   }
}

static bool
render_needs_clear(struct rendering_state *state)
{
   for (uint32_t i = 0; i < state->color_att_count; i++) {
      if (state->color_att[i].load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
         return true;
   }
   if (state->depth_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
      return true;
   if (state->stencil_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
      return true;
   return false;
}

static void clear_attachment_layers(struct rendering_state *state,
                                    struct lvp_image_view *imgv,
                                    const VkRect2D *rect,
                                    unsigned base_layer, unsigned layer_count,
                                    unsigned ds_clear_flags, double dclear_val,
                                    uint32_t sclear_val,
                                    union pipe_color_union *col_val)
{
   struct pipe_surface *clear_surf = create_img_surface(state,
                                                        imgv,
                                                        imgv->vk.format,
                                                        state->framebuffer.width,
                                                        state->framebuffer.height,
                                                        base_layer,
                                                        base_layer + layer_count - 1);

   if (ds_clear_flags) {
      state->pctx->clear_depth_stencil(state->pctx,
                                       clear_surf,
                                       ds_clear_flags,
                                       dclear_val, sclear_val,
                                       rect->offset.x, rect->offset.y,
                                       rect->extent.width, rect->extent.height,
                                       true);
   } else {
      state->pctx->clear_render_target(state->pctx, clear_surf,
                                       col_val,
                                       rect->offset.x, rect->offset.y,
                                       rect->extent.width, rect->extent.height,
                                       true);
   }
   state->pctx->surface_destroy(state->pctx, clear_surf);
}

static void render_clear(struct rendering_state *state)
{
   for (uint32_t i = 0; i < state->color_att_count; i++) {
      if (state->color_att[i].load_op != VK_ATTACHMENT_LOAD_OP_CLEAR)
         continue;

      union pipe_color_union color_clear_val = { 0 };
      const VkClearValue value = state->color_att[i].clear_value;
      color_clear_val.ui[0] = value.color.uint32[0];
      color_clear_val.ui[1] = value.color.uint32[1];
      color_clear_val.ui[2] = value.color.uint32[2];
      color_clear_val.ui[3] = value.color.uint32[3];

      struct lvp_image_view *imgv = state->color_att[i].imgv;
      assert(imgv->surface);

      if (state->info.view_mask) {
         u_foreach_bit(i, state->info.view_mask)
            clear_attachment_layers(state, imgv, &state->render_area,
                                    i, 1, 0, 0, 0, &color_clear_val);
      } else {
         state->pctx->clear_render_target(state->pctx,
                                          imgv->surface,
                                          &color_clear_val,
                                          state->render_area.offset.x,
                                          state->render_area.offset.y,
                                          state->render_area.extent.width,
                                          state->render_area.extent.height,
                                          false);
      }
   }

   uint32_t ds_clear_flags = 0;
   double dclear_val = 0;
   if (state->depth_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
      ds_clear_flags |= PIPE_CLEAR_DEPTH;
      dclear_val = state->depth_att.clear_value.depthStencil.depth;
   }

   uint32_t sclear_val = 0;
   if (state->stencil_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
      ds_clear_flags |= PIPE_CLEAR_STENCIL;
      sclear_val = state->stencil_att.clear_value.depthStencil.stencil;
   }

   if (ds_clear_flags) {
      if (state->info.view_mask) {
         u_foreach_bit(i, state->info.view_mask)
            clear_attachment_layers(state, state->ds_imgv, &state->render_area,
                                    i, 1, ds_clear_flags, dclear_val, sclear_val, NULL);
      } else {
         state->pctx->clear_depth_stencil(state->pctx,
                                          state->ds_imgv->surface,
                                          ds_clear_flags,
                                          dclear_val, sclear_val,
                                          state->render_area.offset.x,
                                          state->render_area.offset.y,
                                          state->render_area.extent.width,
                                          state->render_area.extent.height,
                                          false);
      }
   }
}

static void render_clear_fast(struct rendering_state *state)
{
   /*
    * the state tracker clear interface only works if all the attachments have the same
    * clear color.
    */
   /* llvmpipe doesn't support scissored clears yet */
   if (state->render_area.offset.x || state->render_area.offset.y)
      goto slow_clear;

   if (state->render_area.extent.width != state->framebuffer.width ||
       state->render_area.extent.height != state->framebuffer.height)
      goto slow_clear;

   if (state->info.view_mask)
      goto slow_clear;

   uint32_t buffers = 0;
   bool has_color_value = false;
   VkClearValue color_value = {0};
   for (uint32_t i = 0; i < state->color_att_count; i++) {
      if (state->color_att[i].load_op != VK_ATTACHMENT_LOAD_OP_CLEAR)
         continue;

      buffers |= (PIPE_CLEAR_COLOR0 << i);

      if (has_color_value) {
         if (memcmp(&color_value, &state->color_att[i].clear_value, sizeof(VkClearValue)))
            goto slow_clear;
      } else {
         memcpy(&color_value, &state->color_att[i].clear_value, sizeof(VkClearValue));
         has_color_value = true;
      }
   }

   double dclear_val = 0;
   if (state->depth_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
      buffers |= PIPE_CLEAR_DEPTH;
      dclear_val = state->depth_att.clear_value.depthStencil.depth;
   }

   uint32_t sclear_val = 0;
   if (state->stencil_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
      buffers |= PIPE_CLEAR_STENCIL;
      sclear_val = state->stencil_att.clear_value.depthStencil.stencil;
   }

   union pipe_color_union col_val;
   for (unsigned i = 0; i < 4; i++)
      col_val.ui[i] = color_value.color.uint32[i];

   state->pctx->clear(state->pctx, buffers,
                      NULL, &col_val,
                      dclear_val, sclear_val);
   return;

slow_clear:
   render_clear(state);
}

static struct lvp_image_view *
destroy_multisample_surface(struct rendering_state *state, struct lvp_image_view *imgv)
{
   assert(imgv->image->vk.samples > 1);
   struct lvp_image_view *base = imgv->multisample;
   base->multisample = NULL;
   free((void*)imgv->image);
   pipe_surface_reference(&imgv->surface, NULL);
   free(imgv);
   return base;
}

static void
resolve_ds(struct rendering_state *state, bool multi)
{
   VkResolveModeFlagBits depth_resolve_mode = multi ? state->forced_depth_resolve_mode : state->depth_att.resolve_mode;
   VkResolveModeFlagBits stencil_resolve_mode = multi ? state->forced_stencil_resolve_mode : state->stencil_att.resolve_mode;
   if (!depth_resolve_mode && !stencil_resolve_mode)
      return;

   struct lvp_image_view *src_imgv = state->ds_imgv;
   if (multi && !src_imgv->multisample)
      return;
   if (!multi && src_imgv->image->vk.samples == 1)
      return;

   assert(state->depth_att.resolve_imgv == NULL ||
          state->stencil_att.resolve_imgv == NULL ||
          state->depth_att.resolve_imgv == state->stencil_att.resolve_imgv ||
          multi);
   struct lvp_image_view *dst_imgv =
      multi ? src_imgv->multisample :
      state->depth_att.resolve_imgv ? state->depth_att.resolve_imgv :
                                      state->stencil_att.resolve_imgv;

   int num_blits = 1;
   if (depth_resolve_mode != stencil_resolve_mode)
      num_blits = 2;

   for (unsigned i = 0; i < num_blits; i++) {
      if (i == 0 && depth_resolve_mode == VK_RESOLVE_MODE_NONE)
         continue;

      if (i == 1 && stencil_resolve_mode == VK_RESOLVE_MODE_NONE)
         continue;

      struct pipe_blit_info info;
      memset(&info, 0, sizeof(info));

      info.src.resource = src_imgv->image->bo;
      info.dst.resource = dst_imgv->image->bo;
      info.src.format = src_imgv->pformat;
      info.dst.format = dst_imgv->pformat;
      info.filter = PIPE_TEX_FILTER_NEAREST;

      if (num_blits == 1)
         info.mask = PIPE_MASK_ZS;
      else if (i == 0)
         info.mask = PIPE_MASK_Z;
      else
         info.mask = PIPE_MASK_S;

      if (i == 0 && depth_resolve_mode == VK_RESOLVE_MODE_SAMPLE_ZERO_BIT)
         info.sample0_only = true;
      if (i == 1 && stencil_resolve_mode == VK_RESOLVE_MODE_SAMPLE_ZERO_BIT)
         info.sample0_only = true;

      info.src.box.x = state->render_area.offset.x;
      info.src.box.y = state->render_area.offset.y;
      info.src.box.width = state->render_area.extent.width;
      info.src.box.height = state->render_area.extent.height;
      info.src.box.depth = state->framebuffer.layers;

      info.dst.box = info.src.box;

      state->pctx->blit(state->pctx, &info);
   }
   if (multi)
      state->ds_imgv = destroy_multisample_surface(state, state->ds_imgv);
}

static void
resolve_color(struct rendering_state *state, bool multi)
{
   for (uint32_t i = 0; i < state->color_att_count; i++) {
      if (!state->color_att[i].resolve_mode &&
          !(multi && state->forced_sample_count && state->color_att[i].imgv))
         continue;

      struct lvp_image_view *src_imgv = state->color_att[i].imgv;
      /* skip non-msrtss resolves during msrtss resolve */
      if (multi && !src_imgv->multisample)
         continue;
      struct lvp_image_view *dst_imgv = multi ? src_imgv->multisample : state->color_att[i].resolve_imgv;

      struct pipe_blit_info info;
      memset(&info, 0, sizeof(info));

      info.src.resource = src_imgv->image->bo;
      info.dst.resource = dst_imgv->image->bo;
      info.src.format = src_imgv->pformat;
      info.dst.format = dst_imgv->pformat;
      info.filter = PIPE_TEX_FILTER_NEAREST;
      info.mask = PIPE_MASK_RGBA;
      info.src.box.x = state->render_area.offset.x;
      info.src.box.y = state->render_area.offset.y;
      info.src.box.width = state->render_area.extent.width;
      info.src.box.height = state->render_area.extent.height;
      info.src.box.depth = state->framebuffer.layers;

      info.dst.box = info.src.box;

      info.src.level = src_imgv->vk.base_mip_level;
      info.dst.level = dst_imgv->vk.base_mip_level;

      state->pctx->blit(state->pctx, &info);
   }

   if (!multi)
      return;
   for (uint32_t i = 0; i < state->color_att_count; i++) {
      struct lvp_image_view *src_imgv = state->color_att[i].imgv;
      if (src_imgv && src_imgv->multisample) //check if it has a msrtss view
         state->color_att[i].imgv = destroy_multisample_surface(state, src_imgv);
   }
}

static void render_resolve(struct rendering_state *state)
{
   if (state->forced_sample_count) {
      resolve_ds(state, true);
      resolve_color(state, true);
   }
   resolve_ds(state, false);
   resolve_color(state, false);
}

static void
replicate_attachment(struct rendering_state *state, struct lvp_image_view *src, struct lvp_image_view *dst)
{
   unsigned level = dst->surface->u.tex.level;
   struct pipe_box box;
   u_box_3d(0, 0, 0,
            u_minify(dst->image->bo->width0, level),
            u_minify(dst->image->bo->height0, level),
            u_minify(dst->image->bo->depth0, level),
            &box);
   state->pctx->resource_copy_region(state->pctx, dst->image->bo, level, 0, 0, 0, src->image->bo, level, &box);
}

static struct lvp_image_view *
create_multisample_surface(struct rendering_state *state, struct lvp_image_view *imgv, uint32_t samples, bool replicate)
{
   assert(!imgv->multisample);

   struct pipe_resource templ = *imgv->surface->texture;
   templ.nr_samples = samples;
   struct lvp_image *image = mem_dup(imgv->image, sizeof(struct lvp_image));
   image->vk.samples = samples;
   image->pmem = NULL;
   image->bo = state->pctx->screen->resource_create(state->pctx->screen, &templ);

   struct lvp_image_view *multi = mem_dup(imgv, sizeof(struct lvp_image_view));
   multi->image = image;
   multi->surface = state->pctx->create_surface(state->pctx, image->bo, imgv->surface);
   struct pipe_resource *ref = image->bo;
   pipe_resource_reference(&ref, NULL);
   imgv->multisample = multi;
   multi->multisample = imgv;
   if (replicate)
      replicate_attachment(state, imgv, multi);
   return multi;
}

static bool
att_needs_replicate(const struct rendering_state *state, const struct lvp_image_view *imgv, VkAttachmentLoadOp load_op)
{
   if (load_op == VK_ATTACHMENT_LOAD_OP_LOAD || load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
      return true;
   if (state->render_area.offset.x || state->render_area.offset.y)
      return true;
   if (state->render_area.extent.width < imgv->image->vk.extent.width ||
       state->render_area.extent.height < imgv->image->vk.extent.height)
      return true;
   return false;
}

static void render_att_init(struct lvp_render_attachment* att,
                            const VkRenderingAttachmentInfo *vk_att)
{
   if (vk_att == NULL || vk_att->imageView == VK_NULL_HANDLE) {
      *att = (struct lvp_render_attachment) {
         .load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      };
      return;
   }

   *att = (struct lvp_render_attachment) {
      .imgv = lvp_image_view_from_handle(vk_att->imageView),
      .load_op = vk_att->loadOp,
      .clear_value = vk_att->clearValue,
   };

   if (vk_att->resolveImageView && vk_att->resolveMode) {
      att->resolve_imgv = lvp_image_view_from_handle(vk_att->resolveImageView);
      att->resolve_mode = vk_att->resolveMode;
   }
}

static void handle_begin_rendering(struct vk_cmd_queue_entry *cmd,
                                   struct rendering_state *state)
{
   const VkRenderingInfo *info = cmd->u.begin_rendering.rendering_info;
   bool resuming = (info->flags & VK_RENDERING_RESUMING_BIT) == VK_RENDERING_RESUMING_BIT;
   bool suspending = (info->flags & VK_RENDERING_SUSPENDING_BIT) == VK_RENDERING_SUSPENDING_BIT;

   const VkMultisampledRenderToSingleSampledInfoEXT *ssi =
         vk_find_struct_const(info->pNext, MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_INFO_EXT);
   if (ssi && ssi->multisampledRenderToSingleSampledEnable) {
      state->forced_sample_count = ssi->rasterizationSamples;
      state->forced_depth_resolve_mode = info->pDepthAttachment ? info->pDepthAttachment->resolveMode : 0;
      state->forced_stencil_resolve_mode = info->pStencilAttachment ? info->pStencilAttachment->resolveMode : 0;
   } else {
      state->forced_sample_count = 0;
      state->forced_depth_resolve_mode = 0;
      state->forced_stencil_resolve_mode = 0;
   }

   state->info.view_mask = info->viewMask;
   state->render_area = info->renderArea;
   state->suspending = suspending;
   state->framebuffer.width = info->renderArea.offset.x +
                              info->renderArea.extent.width;
   state->framebuffer.height = info->renderArea.offset.y +
                               info->renderArea.extent.height;
   state->framebuffer.layers = info->layerCount;
   state->framebuffer.nr_cbufs = info->colorAttachmentCount;

   state->color_att_count = info->colorAttachmentCount;
   state->color_att = realloc(state->color_att, sizeof(*state->color_att) * state->color_att_count);
   for (unsigned i = 0; i < info->colorAttachmentCount; i++) {
      render_att_init(&state->color_att[i], &info->pColorAttachments[i]);
      if (state->color_att[i].imgv) {
         struct lvp_image_view *imgv = state->color_att[i].imgv;
         add_img_view_surface(state, imgv,
                              state->framebuffer.width, state->framebuffer.height);
         if (state->forced_sample_count && imgv->image->vk.samples == 1)
            state->color_att[i].imgv = create_multisample_surface(state, imgv, state->forced_sample_count,
                                                                  att_needs_replicate(state, imgv, state->color_att[i].load_op));
         state->framebuffer.cbufs[i] = state->color_att[i].imgv->surface;
      } else {
         state->framebuffer.cbufs[i] = NULL;
      }
   }

   render_att_init(&state->depth_att, info->pDepthAttachment);
   render_att_init(&state->stencil_att, info->pStencilAttachment);
   if (state->depth_att.imgv || state->stencil_att.imgv) {
      assert(state->depth_att.imgv == NULL ||
             state->stencil_att.imgv == NULL ||
             state->depth_att.imgv == state->stencil_att.imgv);
      state->ds_imgv = state->depth_att.imgv ? state->depth_att.imgv :
                                               state->stencil_att.imgv;
      struct lvp_image_view *imgv = state->ds_imgv;
      add_img_view_surface(state, imgv,
                           state->framebuffer.width, state->framebuffer.height);
      if (state->forced_sample_count && imgv->image->vk.samples == 1) {
         VkAttachmentLoadOp load_op;
         if (state->depth_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR ||
             state->stencil_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
            load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
         else if (state->depth_att.load_op == VK_ATTACHMENT_LOAD_OP_LOAD ||
                  state->stencil_att.load_op == VK_ATTACHMENT_LOAD_OP_LOAD)
            load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
         else
            load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
         state->ds_imgv = create_multisample_surface(state, imgv, state->forced_sample_count,
                                                     att_needs_replicate(state, imgv, load_op));
      }
      state->framebuffer.zsbuf = state->ds_imgv->surface;
   } else {
      state->ds_imgv = NULL;
      state->framebuffer.zsbuf = NULL;
   }

   state->pctx->set_framebuffer_state(state->pctx,
                                      &state->framebuffer);

   if (!resuming && render_needs_clear(state))
      render_clear_fast(state);
}

static void handle_end_rendering(struct vk_cmd_queue_entry *cmd,
                                 struct rendering_state *state)
{
   if (!state->suspending)
      render_resolve(state);
}

static void handle_draw(struct vk_cmd_queue_entry *cmd,
                        struct rendering_state *state)
{
   struct pipe_draw_start_count_bias draw;

   state->info.index_size = 0;
   state->info.index.resource = NULL;
   state->info.start_instance = cmd->u.draw.first_instance;
   state->info.instance_count = cmd->u.draw.instance_count;

   draw.start = cmd->u.draw.first_vertex;
   draw.count = cmd->u.draw.vertex_count;
   draw.index_bias = 0;

   state->pctx->set_patch_vertices(state->pctx, state->patch_vertices);
   state->pctx->draw_vbo(state->pctx, &state->info, 0, NULL, &draw, 1);
}

static void handle_draw_multi(struct vk_cmd_queue_entry *cmd,
                              struct rendering_state *state)
{
   struct pipe_draw_start_count_bias *draws = calloc(cmd->u.draw_multi_ext.draw_count,
                                                     sizeof(*draws));

   state->info.index_size = 0;
   state->info.index.resource = NULL;
   state->info.start_instance = cmd->u.draw_multi_ext.first_instance;
   state->info.instance_count = cmd->u.draw_multi_ext.instance_count;
   if (cmd->u.draw_multi_ext.draw_count > 1)
      state->info.increment_draw_id = true;

   for(unsigned i = 0; i < cmd->u.draw_multi_ext.draw_count; i++) {
      draws[i].start = cmd->u.draw_multi_ext.vertex_info[i].firstVertex;
      draws[i].count = cmd->u.draw_multi_ext.vertex_info[i].vertexCount;
      draws[i].index_bias = 0;
   }

   state->pctx->set_patch_vertices(state->pctx, state->patch_vertices);

   if (cmd->u.draw_multi_indexed_ext.draw_count)
      state->pctx->draw_vbo(state->pctx, &state->info, 0, NULL, draws, cmd->u.draw_multi_ext.draw_count);

   free(draws);
}

static void set_viewport(unsigned first_viewport, unsigned viewport_count,
                         const VkViewport* viewports,
                         struct rendering_state *state)
{
   int i;
   unsigned base = 0;
   if (first_viewport == UINT32_MAX)
      state->num_viewports = viewport_count;
   else
      base = first_viewport;

   for (i = 0; i < viewport_count; i++) {
      int idx = i + base;
      const VkViewport *vp = &viewports[i];
      get_viewport_xform(state, vp, idx);
      set_viewport_depth_xform(state, idx);
   }
   state->vp_dirty = true;
}

static void handle_set_viewport(struct vk_cmd_queue_entry *cmd,
                                struct rendering_state *state)
{
   set_viewport(cmd->u.set_viewport.first_viewport,
                cmd->u.set_viewport.viewport_count,
                cmd->u.set_viewport.viewports,
                state);
}

static void handle_set_viewport_with_count(struct vk_cmd_queue_entry *cmd,
                                           struct rendering_state *state)
{
   set_viewport(UINT32_MAX,
                cmd->u.set_viewport_with_count.viewport_count,
                cmd->u.set_viewport_with_count.viewports,
                state);
}

static void set_scissor(unsigned first_scissor,
                        unsigned scissor_count,
                        const VkRect2D *scissors,
                        struct rendering_state *state)
{
   int i;
   unsigned base = 0;
   if (first_scissor == UINT32_MAX)
      state->num_scissors = scissor_count;
   else
      base = first_scissor;

   for (i = 0; i < scissor_count; i++) {
      int idx = i + base;
      const VkRect2D *ss = &scissors[i];
      state->scissors[idx].minx = ss->offset.x;
      state->scissors[idx].miny = ss->offset.y;
      state->scissors[idx].maxx = ss->offset.x + ss->extent.width;
      state->scissors[idx].maxy = ss->offset.y + ss->extent.height;
   }
   state->scissor_dirty = true;
}

static void handle_set_scissor(struct vk_cmd_queue_entry *cmd,
                               struct rendering_state *state)
{
   set_scissor(cmd->u.set_scissor.first_scissor,
               cmd->u.set_scissor.scissor_count,
               cmd->u.set_scissor.scissors,
               state);
}

static void handle_set_scissor_with_count(struct vk_cmd_queue_entry *cmd,
                                          struct rendering_state *state)
{
   set_scissor(UINT32_MAX,
               cmd->u.set_scissor_with_count.scissor_count,
               cmd->u.set_scissor_with_count.scissors,
               state);
}

static void handle_set_line_width(struct vk_cmd_queue_entry *cmd,
                                  struct rendering_state *state)
{
   state->rs_state.line_width = cmd->u.set_line_width.line_width;
   state->rs_dirty = true;
}

static void handle_set_depth_bias(struct vk_cmd_queue_entry *cmd,
                                  struct rendering_state *state)
{
   state->depth_bias.offset_units = cmd->u.set_depth_bias.depth_bias_constant_factor;
   state->depth_bias.offset_scale = cmd->u.set_depth_bias.depth_bias_slope_factor;
   state->depth_bias.offset_clamp = cmd->u.set_depth_bias.depth_bias_clamp;
   state->rs_dirty = true;
}

static void handle_set_blend_constants(struct vk_cmd_queue_entry *cmd,
                                       struct rendering_state *state)
{
   memcpy(state->blend_color.color, cmd->u.set_blend_constants.blend_constants, 4 * sizeof(float));
   state->blend_color_dirty = true;
}

static void handle_set_depth_bounds(struct vk_cmd_queue_entry *cmd,
                                    struct rendering_state *state)
{
   state->dsa_dirty |= !DOUBLE_EQ(state->dsa_state.depth_bounds_min, cmd->u.set_depth_bounds.min_depth_bounds);
   state->dsa_dirty |= !DOUBLE_EQ(state->dsa_state.depth_bounds_max, cmd->u.set_depth_bounds.max_depth_bounds);
   state->dsa_state.depth_bounds_min = cmd->u.set_depth_bounds.min_depth_bounds;
   state->dsa_state.depth_bounds_max = cmd->u.set_depth_bounds.max_depth_bounds;
}

static void handle_set_stencil_compare_mask(struct vk_cmd_queue_entry *cmd,
                                            struct rendering_state *state)
{
   if (cmd->u.set_stencil_compare_mask.face_mask & VK_STENCIL_FACE_FRONT_BIT)
      state->dsa_state.stencil[0].valuemask = cmd->u.set_stencil_compare_mask.compare_mask;
   if (cmd->u.set_stencil_compare_mask.face_mask & VK_STENCIL_FACE_BACK_BIT)
      state->dsa_state.stencil[1].valuemask = cmd->u.set_stencil_compare_mask.compare_mask;
   state->dsa_dirty = true;
}

static void handle_set_stencil_write_mask(struct vk_cmd_queue_entry *cmd,
                                          struct rendering_state *state)
{
   if (cmd->u.set_stencil_write_mask.face_mask & VK_STENCIL_FACE_FRONT_BIT)
      state->dsa_state.stencil[0].writemask = cmd->u.set_stencil_write_mask.write_mask;
   if (cmd->u.set_stencil_write_mask.face_mask & VK_STENCIL_FACE_BACK_BIT)
      state->dsa_state.stencil[1].writemask = cmd->u.set_stencil_write_mask.write_mask;
   state->dsa_dirty = true;
}

static void handle_set_stencil_reference(struct vk_cmd_queue_entry *cmd,
                                         struct rendering_state *state)
{
   if (cmd->u.set_stencil_reference.face_mask & VK_STENCIL_FACE_FRONT_BIT)
      state->stencil_ref.ref_value[0] = cmd->u.set_stencil_reference.reference;
   if (cmd->u.set_stencil_reference.face_mask & VK_STENCIL_FACE_BACK_BIT)
      state->stencil_ref.ref_value[1] = cmd->u.set_stencil_reference.reference;
   state->stencil_ref_dirty = true;
}

static void
copy_depth_rect(ubyte * dst,
                enum pipe_format dst_format,
                unsigned dst_stride,
                unsigned dst_x,
                unsigned dst_y,
                unsigned width,
                unsigned height,
                const ubyte * src,
                enum pipe_format src_format,
                int src_stride,
                unsigned src_x,
                unsigned src_y)
{
   int src_stride_pos = src_stride < 0 ? -src_stride : src_stride;
   int src_blocksize = util_format_get_blocksize(src_format);
   int src_blockwidth = util_format_get_blockwidth(src_format);
   int src_blockheight = util_format_get_blockheight(src_format);
   int dst_blocksize = util_format_get_blocksize(dst_format);
   int dst_blockwidth = util_format_get_blockwidth(dst_format);
   int dst_blockheight = util_format_get_blockheight(dst_format);

   assert(src_blocksize > 0);
   assert(src_blockwidth > 0);
   assert(src_blockheight > 0);

   dst_x /= dst_blockwidth;
   dst_y /= dst_blockheight;
   width = (width + src_blockwidth - 1)/src_blockwidth;
   height = (height + src_blockheight - 1)/src_blockheight;
   src_x /= src_blockwidth;
   src_y /= src_blockheight;

   dst += dst_x * dst_blocksize;
   src += src_x * src_blocksize;
   dst += dst_y * dst_stride;
   src += src_y * src_stride_pos;

   if (dst_format == PIPE_FORMAT_S8_UINT) {
      if (src_format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
         util_format_z32_float_s8x24_uint_unpack_s_8uint(dst, dst_stride,
                                                         src, src_stride,
                                                         width, height);
      } else if (src_format == PIPE_FORMAT_Z24_UNORM_S8_UINT) {
         util_format_z24_unorm_s8_uint_unpack_s_8uint(dst, dst_stride,
                                                      src, src_stride,
                                                      width, height);
      } else {
      }
   } else if (dst_format == PIPE_FORMAT_Z24X8_UNORM) {
      util_format_z24_unorm_s8_uint_unpack_z24(dst, dst_stride,
                                               src, src_stride,
                                               width, height);
   } else if (dst_format == PIPE_FORMAT_Z32_FLOAT) {
      if (src_format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
         util_format_z32_float_s8x24_uint_unpack_z_float((float *)dst, dst_stride,
                                                         src, src_stride,
                                                         width, height);
      }
   } else if (dst_format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
      if (src_format == PIPE_FORMAT_Z32_FLOAT)
         util_format_z32_float_s8x24_uint_pack_z_float(dst, dst_stride,
                                                       (float *)src, src_stride,
                                                       width, height);
      else if (src_format == PIPE_FORMAT_S8_UINT)
         util_format_z32_float_s8x24_uint_pack_s_8uint(dst, dst_stride,
                                                       src, src_stride,
                                                       width, height);
   } else if (dst_format == PIPE_FORMAT_Z24_UNORM_S8_UINT) {
      if (src_format == PIPE_FORMAT_S8_UINT)
         util_format_z24_unorm_s8_uint_pack_s_8uint(dst, dst_stride,
                                                    src, src_stride,
                                                    width, height);
      if (src_format == PIPE_FORMAT_Z24X8_UNORM)
         util_format_z24_unorm_s8_uint_pack_z24(dst, dst_stride,
                                                src, src_stride,
                                                width, height);
   }
}

static void
copy_depth_box(ubyte *dst,
               enum pipe_format dst_format,
               unsigned dst_stride, unsigned dst_slice_stride,
               unsigned dst_x, unsigned dst_y, unsigned dst_z,
               unsigned width, unsigned height, unsigned depth,
               const ubyte * src,
               enum pipe_format src_format,
               int src_stride, unsigned src_slice_stride,
               unsigned src_x, unsigned src_y, unsigned src_z)
{
   unsigned z;
   dst += dst_z * dst_slice_stride;
   src += src_z * src_slice_stride;
   for (z = 0; z < depth; ++z) {
      copy_depth_rect(dst,
                      dst_format,
                      dst_stride,
                      dst_x, dst_y,
                      width, height,
                      src,
                      src_format,
                      src_stride,
                      src_x, src_y);

      dst += dst_slice_stride;
      src += src_slice_stride;
   }
}

static void handle_copy_image_to_buffer2(struct vk_cmd_queue_entry *cmd,
                                             struct rendering_state *state)
{
   int i;
   struct VkCopyImageToBufferInfo2 *copycmd = cmd->u.copy_image_to_buffer2.copy_image_to_buffer_info;
   LVP_FROM_HANDLE(lvp_image, src_image, copycmd->srcImage);
   struct pipe_box box, dbox;
   struct pipe_transfer *src_t, *dst_t;
   ubyte *src_data, *dst_data;

   for (i = 0; i < copycmd->regionCount; i++) {

      box.x = copycmd->pRegions[i].imageOffset.x;
      box.y = copycmd->pRegions[i].imageOffset.y;
      box.z = src_image->vk.image_type == VK_IMAGE_TYPE_3D ? copycmd->pRegions[i].imageOffset.z : copycmd->pRegions[i].imageSubresource.baseArrayLayer;
      box.width = copycmd->pRegions[i].imageExtent.width;
      box.height = copycmd->pRegions[i].imageExtent.height;
      box.depth = src_image->vk.image_type == VK_IMAGE_TYPE_3D ? copycmd->pRegions[i].imageExtent.depth : copycmd->pRegions[i].imageSubresource.layerCount;

      src_data = state->pctx->texture_map(state->pctx,
                                           src_image->bo,
                                           copycmd->pRegions[i].imageSubresource.mipLevel,
                                           PIPE_MAP_READ,
                                           &box,
                                           &src_t);

      dbox.x = copycmd->pRegions[i].bufferOffset;
      dbox.y = 0;
      dbox.z = 0;
      dbox.width = lvp_buffer_from_handle(copycmd->dstBuffer)->bo->width0 - copycmd->pRegions[i].bufferOffset;
      dbox.height = 1;
      dbox.depth = 1;
      dst_data = state->pctx->buffer_map(state->pctx,
                                           lvp_buffer_from_handle(copycmd->dstBuffer)->bo,
                                           0,
                                           PIPE_MAP_WRITE,
                                           &dbox,
                                           &dst_t);

      enum pipe_format src_format = src_image->bo->format;
      enum pipe_format dst_format = src_format;
      if (util_format_is_depth_or_stencil(src_format)) {
         if (copycmd->pRegions[i].imageSubresource.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT) {
            dst_format = util_format_get_depth_only(src_format);
         } else if (copycmd->pRegions[i].imageSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT) {
            dst_format = PIPE_FORMAT_S8_UINT;
         }
      }

      const struct vk_image_buffer_layout buffer_layout =
         vk_image_buffer_copy_layout(&src_image->vk, &copycmd->pRegions[i]);
      if (src_format != dst_format) {
         copy_depth_box(dst_data, dst_format,
                        buffer_layout.row_stride_B,
                        buffer_layout.image_stride_B,
                        0, 0, 0,
                        copycmd->pRegions[i].imageExtent.width,
                        copycmd->pRegions[i].imageExtent.height,
                        box.depth,
                        src_data, src_format, src_t->stride, src_t->layer_stride, 0, 0, 0);
      } else {
         util_copy_box((ubyte *)dst_data, src_format,
                       buffer_layout.row_stride_B,
                       buffer_layout.image_stride_B,
                       0, 0, 0,
                       copycmd->pRegions[i].imageExtent.width,
                       copycmd->pRegions[i].imageExtent.height,
                       box.depth,
                       src_data, src_t->stride, src_t->layer_stride, 0, 0, 0);
      }
      state->pctx->texture_unmap(state->pctx, src_t);
      state->pctx->buffer_unmap(state->pctx, dst_t);
   }
}

static void handle_copy_buffer_to_image(struct vk_cmd_queue_entry *cmd,
                                        struct rendering_state *state)
{
   int i;
   struct VkCopyBufferToImageInfo2 *copycmd = cmd->u.copy_buffer_to_image2.copy_buffer_to_image_info;
   LVP_FROM_HANDLE(lvp_image, dst_image, copycmd->dstImage);
   struct pipe_box box, sbox;
   struct pipe_transfer *src_t, *dst_t;
   void *src_data, *dst_data;

   for (i = 0; i < copycmd->regionCount; i++) {

      sbox.x = copycmd->pRegions[i].bufferOffset;
      sbox.y = 0;
      sbox.z = 0;
      sbox.width = lvp_buffer_from_handle(copycmd->srcBuffer)->bo->width0;
      sbox.height = 1;
      sbox.depth = 1;
      src_data = state->pctx->buffer_map(state->pctx,
                                           lvp_buffer_from_handle(copycmd->srcBuffer)->bo,
                                           0,
                                           PIPE_MAP_READ,
                                           &sbox,
                                           &src_t);


      box.x = copycmd->pRegions[i].imageOffset.x;
      box.y = copycmd->pRegions[i].imageOffset.y;
      box.z = dst_image->vk.image_type == VK_IMAGE_TYPE_3D ? copycmd->pRegions[i].imageOffset.z : copycmd->pRegions[i].imageSubresource.baseArrayLayer;
      box.width = copycmd->pRegions[i].imageExtent.width;
      box.height = copycmd->pRegions[i].imageExtent.height;
      box.depth = dst_image->vk.image_type == VK_IMAGE_TYPE_3D ? copycmd->pRegions[i].imageExtent.depth : copycmd->pRegions[i].imageSubresource.layerCount;

      dst_data = state->pctx->texture_map(state->pctx,
                                           dst_image->bo,
                                           copycmd->pRegions[i].imageSubresource.mipLevel,
                                           PIPE_MAP_WRITE,
                                           &box,
                                           &dst_t);

      enum pipe_format dst_format = dst_image->bo->format;
      enum pipe_format src_format = dst_format;
      if (util_format_is_depth_or_stencil(dst_format)) {
         if (copycmd->pRegions[i].imageSubresource.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT) {
            src_format = util_format_get_depth_only(dst_image->bo->format);
         } else if (copycmd->pRegions[i].imageSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT) {
            src_format = PIPE_FORMAT_S8_UINT;
         }
      }

      const struct vk_image_buffer_layout buffer_layout =
         vk_image_buffer_copy_layout(&dst_image->vk, &copycmd->pRegions[i]);
      if (src_format != dst_format) {
         copy_depth_box(dst_data, dst_format,
                        dst_t->stride, dst_t->layer_stride,
                        0, 0, 0,
                        copycmd->pRegions[i].imageExtent.width,
                        copycmd->pRegions[i].imageExtent.height,
                        box.depth,
                        src_data, src_format,
                        buffer_layout.row_stride_B,
                        buffer_layout.image_stride_B,
                        0, 0, 0);
      } else {
         util_copy_box(dst_data, dst_format,
                       dst_t->stride, dst_t->layer_stride,
                       0, 0, 0,
                       copycmd->pRegions[i].imageExtent.width,
                       copycmd->pRegions[i].imageExtent.height,
                       box.depth,
                       src_data,
                       buffer_layout.row_stride_B,
                       buffer_layout.image_stride_B,
                       0, 0, 0);
      }
      state->pctx->buffer_unmap(state->pctx, src_t);
      state->pctx->texture_unmap(state->pctx, dst_t);
   }
}

static void handle_copy_image(struct vk_cmd_queue_entry *cmd,
                              struct rendering_state *state)
{
   int i;
   struct VkCopyImageInfo2 *copycmd = cmd->u.copy_image2.copy_image_info;
   LVP_FROM_HANDLE(lvp_image, src_image, copycmd->srcImage);
   LVP_FROM_HANDLE(lvp_image, dst_image, copycmd->dstImage);

   for (i = 0; i < copycmd->regionCount; i++) {
      struct pipe_box src_box;
      src_box.x = copycmd->pRegions[i].srcOffset.x;
      src_box.y = copycmd->pRegions[i].srcOffset.y;
      src_box.width = copycmd->pRegions[i].extent.width;
      src_box.height = copycmd->pRegions[i].extent.height;
      if (src_image->bo->target == PIPE_TEXTURE_3D) {
         src_box.depth = copycmd->pRegions[i].extent.depth;
         src_box.z = copycmd->pRegions[i].srcOffset.z;
      } else {
         src_box.depth = copycmd->pRegions[i].srcSubresource.layerCount;
         src_box.z = copycmd->pRegions[i].srcSubresource.baseArrayLayer;
      }

      unsigned dstz = dst_image->bo->target == PIPE_TEXTURE_3D ?
                      copycmd->pRegions[i].dstOffset.z :
                      copycmd->pRegions[i].dstSubresource.baseArrayLayer;
      state->pctx->resource_copy_region(state->pctx, dst_image->bo,
                                        copycmd->pRegions[i].dstSubresource.mipLevel,
                                        copycmd->pRegions[i].dstOffset.x,
                                        copycmd->pRegions[i].dstOffset.y,
                                        dstz,
                                        src_image->bo,
                                        copycmd->pRegions[i].srcSubresource.mipLevel,
                                        &src_box);
   }
}

static void handle_copy_buffer(struct vk_cmd_queue_entry *cmd,
                               struct rendering_state *state)
{
   int i;
   VkCopyBufferInfo2 *copycmd = cmd->u.copy_buffer2.copy_buffer_info;

   for (i = 0; i < copycmd->regionCount; i++) {
      struct pipe_box box = { 0 };
      u_box_1d(copycmd->pRegions[i].srcOffset, copycmd->pRegions[i].size, &box);
      state->pctx->resource_copy_region(state->pctx, lvp_buffer_from_handle(copycmd->dstBuffer)->bo, 0,
                                        copycmd->pRegions[i].dstOffset, 0, 0,
                                        lvp_buffer_from_handle(copycmd->srcBuffer)->bo, 0, &box);
   }
}

static void handle_blit_image(struct vk_cmd_queue_entry *cmd,
                              struct rendering_state *state)
{
   int i;
   VkBlitImageInfo2 *blitcmd = cmd->u.blit_image2.blit_image_info;
   LVP_FROM_HANDLE(lvp_image, src_image, blitcmd->srcImage);
   LVP_FROM_HANDLE(lvp_image, dst_image, blitcmd->dstImage);
   struct pipe_blit_info info;

   memset(&info, 0, sizeof(info));

   info.src.resource = src_image->bo;
   info.dst.resource = dst_image->bo;
   info.src.format = src_image->bo->format;
   info.dst.format = dst_image->bo->format;
   info.mask = util_format_is_depth_or_stencil(info.src.format) ? PIPE_MASK_ZS : PIPE_MASK_RGBA;
   info.filter = blitcmd->filter == VK_FILTER_NEAREST ? PIPE_TEX_FILTER_NEAREST : PIPE_TEX_FILTER_LINEAR;
   for (i = 0; i < blitcmd->regionCount; i++) {
      int srcX0, srcX1, srcY0, srcY1, srcZ0, srcZ1;
      unsigned dstX0, dstX1, dstY0, dstY1, dstZ0, dstZ1;

      srcX0 = blitcmd->pRegions[i].srcOffsets[0].x;
      srcX1 = blitcmd->pRegions[i].srcOffsets[1].x;
      srcY0 = blitcmd->pRegions[i].srcOffsets[0].y;
      srcY1 = blitcmd->pRegions[i].srcOffsets[1].y;
      srcZ0 = blitcmd->pRegions[i].srcOffsets[0].z;
      srcZ1 = blitcmd->pRegions[i].srcOffsets[1].z;

      dstX0 = blitcmd->pRegions[i].dstOffsets[0].x;
      dstX1 = blitcmd->pRegions[i].dstOffsets[1].x;
      dstY0 = blitcmd->pRegions[i].dstOffsets[0].y;
      dstY1 = blitcmd->pRegions[i].dstOffsets[1].y;
      dstZ0 = blitcmd->pRegions[i].dstOffsets[0].z;
      dstZ1 = blitcmd->pRegions[i].dstOffsets[1].z;

      if (dstX0 < dstX1) {
         info.dst.box.x = dstX0;
         info.src.box.x = srcX0;
         info.dst.box.width = dstX1 - dstX0;
         info.src.box.width = srcX1 - srcX0;
      } else {
         info.dst.box.x = dstX1;
         info.src.box.x = srcX1;
         info.dst.box.width = dstX0 - dstX1;
         info.src.box.width = srcX0 - srcX1;
      }

      if (dstY0 < dstY1) {
         info.dst.box.y = dstY0;
         info.src.box.y = srcY0;
         info.dst.box.height = dstY1 - dstY0;
         info.src.box.height = srcY1 - srcY0;
      } else {
         info.dst.box.y = dstY1;
         info.src.box.y = srcY1;
         info.dst.box.height = dstY0 - dstY1;
         info.src.box.height = srcY0 - srcY1;
      }

      assert_subresource_layers(info.src.resource, &blitcmd->pRegions[i].srcSubresource, blitcmd->pRegions[i].srcOffsets);
      assert_subresource_layers(info.dst.resource, &blitcmd->pRegions[i].dstSubresource, blitcmd->pRegions[i].dstOffsets);
      if (src_image->bo->target == PIPE_TEXTURE_3D) {
         if (dstZ0 < dstZ1) {
            info.dst.box.z = dstZ0;
            info.src.box.z = srcZ0;
            info.dst.box.depth = dstZ1 - dstZ0;
            info.src.box.depth = srcZ1 - srcZ0;
         } else {
            info.dst.box.z = dstZ1;
            info.src.box.z = srcZ1;
            info.dst.box.depth = dstZ0 - dstZ1;
            info.src.box.depth = srcZ0 - srcZ1;
         }
      } else {
         info.src.box.z = blitcmd->pRegions[i].srcSubresource.baseArrayLayer;
         info.dst.box.z = blitcmd->pRegions[i].dstSubresource.baseArrayLayer;
         info.src.box.depth = blitcmd->pRegions[i].srcSubresource.layerCount;
         info.dst.box.depth = blitcmd->pRegions[i].dstSubresource.layerCount;
      }

      info.src.level = blitcmd->pRegions[i].srcSubresource.mipLevel;
      info.dst.level = blitcmd->pRegions[i].dstSubresource.mipLevel;
      state->pctx->blit(state->pctx, &info);
   }
}

static void handle_fill_buffer(struct vk_cmd_queue_entry *cmd,
                               struct rendering_state *state)
{
   struct vk_cmd_fill_buffer *fillcmd = &cmd->u.fill_buffer;
   uint32_t size = fillcmd->size;

   if (fillcmd->size == VK_WHOLE_SIZE) {
      size = lvp_buffer_from_handle(fillcmd->dst_buffer)->bo->width0 - fillcmd->dst_offset;
      size = ROUND_DOWN_TO(size, 4);
   }

   state->pctx->clear_buffer(state->pctx,
                             lvp_buffer_from_handle(fillcmd->dst_buffer)->bo,
                             fillcmd->dst_offset,
                             size,
                             &fillcmd->data,
                             4);
}

static void handle_update_buffer(struct vk_cmd_queue_entry *cmd,
                                 struct rendering_state *state)
{
   struct vk_cmd_update_buffer *updcmd = &cmd->u.update_buffer;
   uint32_t *dst;
   struct pipe_transfer *dst_t;
   struct pipe_box box;

   u_box_1d(updcmd->dst_offset, updcmd->data_size, &box);
   dst = state->pctx->buffer_map(state->pctx,
                                   lvp_buffer_from_handle(updcmd->dst_buffer)->bo,
                                   0,
                                   PIPE_MAP_WRITE,
                                   &box,
                                   &dst_t);

   memcpy(dst, updcmd->data, updcmd->data_size);
   state->pctx->buffer_unmap(state->pctx, dst_t);
}

static void handle_draw_indexed(struct vk_cmd_queue_entry *cmd,
                                struct rendering_state *state)
{
   struct pipe_draw_start_count_bias draw = {0};

   state->info.index_bounds_valid = false;
   state->info.min_index = 0;
   state->info.max_index = ~0;
   state->info.index_size = state->index_size;
   state->info.index.resource = state->index_buffer;
   state->info.start_instance = cmd->u.draw_indexed.first_instance;
   state->info.instance_count = cmd->u.draw_indexed.instance_count;

   if (state->info.primitive_restart)
      state->info.restart_index = util_prim_restart_index_from_size(state->info.index_size);

   draw.count = cmd->u.draw_indexed.index_count;
   draw.index_bias = cmd->u.draw_indexed.vertex_offset;
   /* TODO: avoid calculating multiple times if cmdbuf is submitted again */
   draw.start = (state->index_offset / state->index_size) + cmd->u.draw_indexed.first_index;

   state->info.index_bias_varies = !cmd->u.draw_indexed.vertex_offset;
   state->pctx->set_patch_vertices(state->pctx, state->patch_vertices);
   state->pctx->draw_vbo(state->pctx, &state->info, 0, NULL, &draw, 1);
}

static void handle_draw_multi_indexed(struct vk_cmd_queue_entry *cmd,
                                      struct rendering_state *state)
{
   struct pipe_draw_start_count_bias *draws = calloc(cmd->u.draw_multi_indexed_ext.draw_count,
                                                     sizeof(*draws));

   state->info.index_bounds_valid = false;
   state->info.min_index = 0;
   state->info.max_index = ~0;
   state->info.index_size = state->index_size;
   state->info.index.resource = state->index_buffer;
   state->info.start_instance = cmd->u.draw_multi_indexed_ext.first_instance;
   state->info.instance_count = cmd->u.draw_multi_indexed_ext.instance_count;
   if (cmd->u.draw_multi_indexed_ext.draw_count > 1)
      state->info.increment_draw_id = true;

   if (state->info.primitive_restart)
      state->info.restart_index = util_prim_restart_index_from_size(state->info.index_size);

   unsigned size = cmd->u.draw_multi_indexed_ext.draw_count * sizeof(struct pipe_draw_start_count_bias);
   memcpy(draws, cmd->u.draw_multi_indexed_ext.index_info, size);

   /* only the first member is read if index_bias_varies is true */
   if (cmd->u.draw_multi_indexed_ext.draw_count &&
       cmd->u.draw_multi_indexed_ext.vertex_offset)
      draws[0].index_bias = *cmd->u.draw_multi_indexed_ext.vertex_offset;

   /* TODO: avoid calculating multiple times if cmdbuf is submitted again */
   for (unsigned i = 0; i < cmd->u.draw_multi_indexed_ext.draw_count; i++)
      draws[i].start = (state->index_offset / state->index_size) + draws[i].start;

   state->info.index_bias_varies = !cmd->u.draw_multi_indexed_ext.vertex_offset;
   state->pctx->set_patch_vertices(state->pctx, state->patch_vertices);

   if (cmd->u.draw_multi_indexed_ext.draw_count)
      state->pctx->draw_vbo(state->pctx, &state->info, 0, NULL, draws, cmd->u.draw_multi_indexed_ext.draw_count);

   free(draws);
}

static void handle_draw_indirect(struct vk_cmd_queue_entry *cmd,
                                 struct rendering_state *state, bool indexed)
{
   struct pipe_draw_start_count_bias draw = {0};
   if (indexed) {
      state->info.index_bounds_valid = false;
      state->info.index_size = state->index_size;
      state->info.index.resource = state->index_buffer;
      state->info.max_index = ~0;
      if (state->info.primitive_restart)
         state->info.restart_index = util_prim_restart_index_from_size(state->info.index_size);
   } else
      state->info.index_size = 0;
   state->indirect_info.offset = cmd->u.draw_indirect.offset;
   state->indirect_info.stride = cmd->u.draw_indirect.stride;
   state->indirect_info.draw_count = cmd->u.draw_indirect.draw_count;
   state->indirect_info.buffer = lvp_buffer_from_handle(cmd->u.draw_indirect.buffer)->bo;

   state->pctx->set_patch_vertices(state->pctx, state->patch_vertices);
   state->pctx->draw_vbo(state->pctx, &state->info, 0, &state->indirect_info, &draw, 1);
}

static void handle_index_buffer(struct vk_cmd_queue_entry *cmd,
                                struct rendering_state *state)
{
   struct vk_cmd_bind_index_buffer *ib = &cmd->u.bind_index_buffer;
   switch (ib->index_type) {
   case VK_INDEX_TYPE_UINT8_EXT:
      state->index_size = 1;
      break;
   case VK_INDEX_TYPE_UINT16:
      state->index_size = 2;
      break;
   case VK_INDEX_TYPE_UINT32:
      state->index_size = 4;
      break;
   default:
      break;
   }
   state->index_offset = ib->offset;
   if (ib->buffer)
      state->index_buffer = lvp_buffer_from_handle(ib->buffer)->bo;
   else
      state->index_buffer = NULL;

   state->ib_dirty = true;
}

static void handle_dispatch(struct vk_cmd_queue_entry *cmd,
                            struct rendering_state *state)
{
   state->dispatch_info.grid[0] = cmd->u.dispatch.group_count_x;
   state->dispatch_info.grid[1] = cmd->u.dispatch.group_count_y;
   state->dispatch_info.grid[2] = cmd->u.dispatch.group_count_z;
   state->dispatch_info.grid_base[0] = 0;
   state->dispatch_info.grid_base[1] = 0;
   state->dispatch_info.grid_base[2] = 0;
   state->dispatch_info.indirect = NULL;
   state->pctx->launch_grid(state->pctx, &state->dispatch_info);
}

static void handle_dispatch_base(struct vk_cmd_queue_entry *cmd,
                                 struct rendering_state *state)
{
   state->dispatch_info.grid[0] = cmd->u.dispatch_base.group_count_x;
   state->dispatch_info.grid[1] = cmd->u.dispatch_base.group_count_y;
   state->dispatch_info.grid[2] = cmd->u.dispatch_base.group_count_z;
   state->dispatch_info.grid_base[0] = cmd->u.dispatch_base.base_group_x;
   state->dispatch_info.grid_base[1] = cmd->u.dispatch_base.base_group_y;
   state->dispatch_info.grid_base[2] = cmd->u.dispatch_base.base_group_z;
   state->dispatch_info.indirect = NULL;
   state->pctx->launch_grid(state->pctx, &state->dispatch_info);
}

static void handle_dispatch_indirect(struct vk_cmd_queue_entry *cmd,
                                     struct rendering_state *state)
{
   state->dispatch_info.indirect = lvp_buffer_from_handle(cmd->u.dispatch_indirect.buffer)->bo;
   state->dispatch_info.indirect_offset = cmd->u.dispatch_indirect.offset;
   state->pctx->launch_grid(state->pctx, &state->dispatch_info);
}

static void handle_push_constants(struct vk_cmd_queue_entry *cmd,
                                  struct rendering_state *state)
{
   memcpy(state->push_constants + cmd->u.push_constants.offset, cmd->u.push_constants.values, cmd->u.push_constants.size);

   VkShaderStageFlags stage_flags = cmd->u.push_constants.stage_flags;
   state->pcbuf_dirty[PIPE_SHADER_VERTEX] |= (stage_flags & VK_SHADER_STAGE_VERTEX_BIT) > 0;
   state->pcbuf_dirty[PIPE_SHADER_FRAGMENT] |= (stage_flags & VK_SHADER_STAGE_FRAGMENT_BIT) > 0;
   state->pcbuf_dirty[PIPE_SHADER_GEOMETRY] |= (stage_flags & VK_SHADER_STAGE_GEOMETRY_BIT) > 0;
   state->pcbuf_dirty[PIPE_SHADER_TESS_CTRL] |= (stage_flags & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) > 0;
   state->pcbuf_dirty[PIPE_SHADER_TESS_EVAL] |= (stage_flags & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) > 0;
   state->pcbuf_dirty[PIPE_SHADER_COMPUTE] |= (stage_flags & VK_SHADER_STAGE_COMPUTE_BIT) > 0;
   state->inlines_dirty[PIPE_SHADER_VERTEX] |= (stage_flags & VK_SHADER_STAGE_VERTEX_BIT) > 0;
   state->inlines_dirty[PIPE_SHADER_FRAGMENT] |= (stage_flags & VK_SHADER_STAGE_FRAGMENT_BIT) > 0;
   state->inlines_dirty[PIPE_SHADER_GEOMETRY] |= (stage_flags & VK_SHADER_STAGE_GEOMETRY_BIT) > 0;
   state->inlines_dirty[PIPE_SHADER_TESS_CTRL] |= (stage_flags & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) > 0;
   state->inlines_dirty[PIPE_SHADER_TESS_EVAL] |= (stage_flags & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) > 0;
   state->inlines_dirty[PIPE_SHADER_COMPUTE] |= (stage_flags & VK_SHADER_STAGE_COMPUTE_BIT) > 0;
}

static void lvp_execute_cmd_buffer(struct lvp_cmd_buffer *cmd_buffer,
                                   struct rendering_state *state);

static void handle_execute_commands(struct vk_cmd_queue_entry *cmd,
                                    struct rendering_state *state)
{
   for (unsigned i = 0; i < cmd->u.execute_commands.command_buffer_count; i++) {
      LVP_FROM_HANDLE(lvp_cmd_buffer, secondary_buf, cmd->u.execute_commands.command_buffers[i]);
      lvp_execute_cmd_buffer(secondary_buf, state);
   }
}

static void handle_event_set2(struct vk_cmd_queue_entry *cmd,
                             struct rendering_state *state)
{
   LVP_FROM_HANDLE(lvp_event, event, cmd->u.set_event2.event);

   VkPipelineStageFlags2 src_stage_mask = 0;

   for (uint32_t i = 0; i < cmd->u.set_event2.dependency_info->memoryBarrierCount; i++)
      src_stage_mask |= cmd->u.set_event2.dependency_info->pMemoryBarriers[i].srcStageMask;
   for (uint32_t i = 0; i < cmd->u.set_event2.dependency_info->bufferMemoryBarrierCount; i++)
      src_stage_mask |= cmd->u.set_event2.dependency_info->pBufferMemoryBarriers[i].srcStageMask;
   for (uint32_t i = 0; i < cmd->u.set_event2.dependency_info->imageMemoryBarrierCount; i++)
      src_stage_mask |= cmd->u.set_event2.dependency_info->pImageMemoryBarriers[i].srcStageMask;

   if (src_stage_mask & VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT)
      state->pctx->flush(state->pctx, NULL, 0);
   event->event_storage = 1;
}

static void handle_event_reset2(struct vk_cmd_queue_entry *cmd,
                               struct rendering_state *state)
{
   LVP_FROM_HANDLE(lvp_event, event, cmd->u.reset_event2.event);

   if (cmd->u.reset_event2.stage_mask == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
      state->pctx->flush(state->pctx, NULL, 0);
   event->event_storage = 0;
}

static void handle_wait_events2(struct vk_cmd_queue_entry *cmd,
                               struct rendering_state *state)
{
   finish_fence(state);
   for (unsigned i = 0; i < cmd->u.wait_events2.event_count; i++) {
      LVP_FROM_HANDLE(lvp_event, event, cmd->u.wait_events2.events[i]);

      while (event->event_storage != true);
   }
}

static void handle_pipeline_barrier(struct vk_cmd_queue_entry *cmd,
                                    struct rendering_state *state)
{
   finish_fence(state);
}

static void handle_begin_query(struct vk_cmd_queue_entry *cmd,
                               struct rendering_state *state)
{
   struct vk_cmd_begin_query *qcmd = &cmd->u.begin_query;
   LVP_FROM_HANDLE(lvp_query_pool, pool, qcmd->query_pool);

   if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS &&
       pool->pipeline_stats & VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT)
      emit_compute_state(state);

   emit_state(state);

   if (!pool->queries[qcmd->query]) {
      enum pipe_query_type qtype = pool->base_type;
      pool->queries[qcmd->query] = state->pctx->create_query(state->pctx,
                                                             qtype, 0);
   }

   state->pctx->begin_query(state->pctx, pool->queries[qcmd->query]);
}

static void handle_end_query(struct vk_cmd_queue_entry *cmd,
                             struct rendering_state *state)
{
   struct vk_cmd_end_query *qcmd = &cmd->u.end_query;
   LVP_FROM_HANDLE(lvp_query_pool, pool, qcmd->query_pool);
   assert(pool->queries[qcmd->query]);

   state->pctx->end_query(state->pctx, pool->queries[qcmd->query]);
}


static void handle_begin_query_indexed_ext(struct vk_cmd_queue_entry *cmd,
                                           struct rendering_state *state)
{
   struct vk_cmd_begin_query_indexed_ext *qcmd = &cmd->u.begin_query_indexed_ext;
   LVP_FROM_HANDLE(lvp_query_pool, pool, qcmd->query_pool);

   if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS &&
       pool->pipeline_stats & VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT)
      emit_compute_state(state);

   emit_state(state);

   if (!pool->queries[qcmd->query]) {
      enum pipe_query_type qtype = pool->base_type;
      pool->queries[qcmd->query] = state->pctx->create_query(state->pctx,
                                                             qtype, qcmd->index);
   }

   state->pctx->begin_query(state->pctx, pool->queries[qcmd->query]);
}

static void handle_end_query_indexed_ext(struct vk_cmd_queue_entry *cmd,
                                         struct rendering_state *state)
{
   struct vk_cmd_end_query_indexed_ext *qcmd = &cmd->u.end_query_indexed_ext;
   LVP_FROM_HANDLE(lvp_query_pool, pool, qcmd->query_pool);
   assert(pool->queries[qcmd->query]);

   state->pctx->end_query(state->pctx, pool->queries[qcmd->query]);
}

static void handle_reset_query_pool(struct vk_cmd_queue_entry *cmd,
                                    struct rendering_state *state)
{
   struct vk_cmd_reset_query_pool *qcmd = &cmd->u.reset_query_pool;
   LVP_FROM_HANDLE(lvp_query_pool, pool, qcmd->query_pool);
   for (unsigned i = qcmd->first_query; i < qcmd->first_query + qcmd->query_count; i++) {
      if (pool->queries[i]) {
         state->pctx->destroy_query(state->pctx, pool->queries[i]);
         pool->queries[i] = NULL;
      }
   }
}

static void handle_write_timestamp2(struct vk_cmd_queue_entry *cmd,
                                    struct rendering_state *state)
{
   struct vk_cmd_write_timestamp2 *qcmd = &cmd->u.write_timestamp2;
   LVP_FROM_HANDLE(lvp_query_pool, pool, qcmd->query_pool);
   if (!pool->queries[qcmd->query]) {
      pool->queries[qcmd->query] = state->pctx->create_query(state->pctx,
                                                             PIPE_QUERY_TIMESTAMP, 0);
   }

   if (!(qcmd->stage == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT))
      state->pctx->flush(state->pctx, NULL, 0);
   state->pctx->end_query(state->pctx, pool->queries[qcmd->query]);

}

static void handle_copy_query_pool_results(struct vk_cmd_queue_entry *cmd,
                                           struct rendering_state *state)
{
   struct vk_cmd_copy_query_pool_results *copycmd = &cmd->u.copy_query_pool_results;
   LVP_FROM_HANDLE(lvp_query_pool, pool, copycmd->query_pool);
   enum pipe_query_flags flags = (copycmd->flags & VK_QUERY_RESULT_WAIT_BIT) ? PIPE_QUERY_WAIT : 0;

   if (copycmd->flags & VK_QUERY_RESULT_PARTIAL_BIT)
      flags |= PIPE_QUERY_PARTIAL;
   unsigned result_size = copycmd->flags & VK_QUERY_RESULT_64_BIT ? 8 : 4;
   for (unsigned i = copycmd->first_query; i < copycmd->first_query + copycmd->query_count; i++) {
      unsigned offset = copycmd->dst_offset + (copycmd->stride * (i - copycmd->first_query));
      if (pool->queries[i]) {
         unsigned num_results = 0;
         if (copycmd->flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
            if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
               num_results = util_bitcount(pool->pipeline_stats);
            } else
               num_results = pool-> type == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT ? 2 : 1;
            state->pctx->get_query_result_resource(state->pctx,
                                                   pool->queries[i],
                                                   flags,
                                                   copycmd->flags & VK_QUERY_RESULT_64_BIT ? PIPE_QUERY_TYPE_U64 : PIPE_QUERY_TYPE_U32,
                                                   -1,
                                                   lvp_buffer_from_handle(copycmd->dst_buffer)->bo,
                                                   offset + num_results * result_size);
         }
         if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
            num_results = 0;
            u_foreach_bit(bit, pool->pipeline_stats)
               state->pctx->get_query_result_resource(state->pctx,
                                                      pool->queries[i],
                                                      flags,
                                                      copycmd->flags & VK_QUERY_RESULT_64_BIT ? PIPE_QUERY_TYPE_U64 : PIPE_QUERY_TYPE_U32,
                                                      bit,
                                                      lvp_buffer_from_handle(copycmd->dst_buffer)->bo,
                                                      offset + num_results++ * result_size);
         } else {
            state->pctx->get_query_result_resource(state->pctx,
                                                   pool->queries[i],
                                                   flags,
                                                   copycmd->flags & VK_QUERY_RESULT_64_BIT ? PIPE_QUERY_TYPE_U64 : PIPE_QUERY_TYPE_U32,
                                                   0,
                                                   lvp_buffer_from_handle(copycmd->dst_buffer)->bo,
                                                   offset);
         }
      } else {
         /* if no queries emitted yet, just reset the buffer to 0 so avail is reported correctly */
         if (copycmd->flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
            struct pipe_transfer *src_t;
            uint32_t *map;

            struct pipe_box box = {0};
            box.x = offset;
            box.width = copycmd->stride;
            box.height = 1;
            box.depth = 1;
            map = state->pctx->buffer_map(state->pctx,
                                            lvp_buffer_from_handle(copycmd->dst_buffer)->bo, 0, PIPE_MAP_READ, &box,
                                            &src_t);

            memset(map, 0, box.width);
            state->pctx->buffer_unmap(state->pctx, src_t);
         }
      }
   }
}

static void handle_clear_color_image(struct vk_cmd_queue_entry *cmd,
                                     struct rendering_state *state)
{
   LVP_FROM_HANDLE(lvp_image, image, cmd->u.clear_color_image.image);
   union util_color uc;
   uint32_t *col_val = uc.ui;
   util_pack_color_union(image->bo->format, &uc, (void*)cmd->u.clear_color_image.color);
   for (unsigned i = 0; i < cmd->u.clear_color_image.range_count; i++) {
      VkImageSubresourceRange *range = &cmd->u.clear_color_image.ranges[i];
      struct pipe_box box;
      box.x = 0;
      box.y = 0;
      box.z = 0;

      uint32_t level_count = vk_image_subresource_level_count(&image->vk, range);
      for (unsigned j = range->baseMipLevel; j < range->baseMipLevel + level_count; j++) {
         box.width = u_minify(image->bo->width0, j);
         box.height = u_minify(image->bo->height0, j);
         box.depth = 1;
         if (image->bo->target == PIPE_TEXTURE_3D)
            box.depth = u_minify(image->bo->depth0, j);
         else if (image->bo->target == PIPE_TEXTURE_1D_ARRAY) {
            box.y = range->baseArrayLayer;
            box.height = vk_image_subresource_layer_count(&image->vk, range);
            box.depth = 1;
         } else {
            box.z = range->baseArrayLayer;
            box.depth = vk_image_subresource_layer_count(&image->vk, range);
         }

         state->pctx->clear_texture(state->pctx, image->bo,
                                    j, &box, (void *)col_val);
      }
   }
}

static void handle_clear_ds_image(struct vk_cmd_queue_entry *cmd,
                                  struct rendering_state *state)
{
   LVP_FROM_HANDLE(lvp_image, image, cmd->u.clear_depth_stencil_image.image);
   for (unsigned i = 0; i < cmd->u.clear_depth_stencil_image.range_count; i++) {
      VkImageSubresourceRange *range = &cmd->u.clear_depth_stencil_image.ranges[i];
      uint32_t ds_clear_flags = 0;
      if (range->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
         ds_clear_flags |= PIPE_CLEAR_DEPTH;
      if (range->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
         ds_clear_flags |= PIPE_CLEAR_STENCIL;

      uint32_t level_count = vk_image_subresource_level_count(&image->vk, range);
      for (unsigned j = 0; j < level_count; j++) {
         struct pipe_surface *surf;
         unsigned width, height;

         width = u_minify(image->bo->width0, range->baseMipLevel + j);
         height = u_minify(image->bo->height0, range->baseMipLevel + j);

         surf = create_img_surface_bo(state, range,
                                      image->bo, image->bo->format,
                                      width, height,
                                      0, vk_image_subresource_layer_count(&image->vk, range) - 1, j);

         state->pctx->clear_depth_stencil(state->pctx,
                                          surf,
                                          ds_clear_flags,
                                          cmd->u.clear_depth_stencil_image.depth_stencil->depth,
                                          cmd->u.clear_depth_stencil_image.depth_stencil->stencil,
                                          0, 0,
                                          width, height, true);
         state->pctx->surface_destroy(state->pctx, surf);
      }
   }
}

static void handle_clear_attachments(struct vk_cmd_queue_entry *cmd,
                                     struct rendering_state *state)
{
   for (uint32_t a = 0; a < cmd->u.clear_attachments.attachment_count; a++) {
      VkClearAttachment *att = &cmd->u.clear_attachments.attachments[a];
      struct lvp_image_view *imgv;

      if (att->aspectMask == VK_IMAGE_ASPECT_COLOR_BIT) {
         imgv = state->color_att[att->colorAttachment].imgv;
      } else {
         imgv = state->ds_imgv;
      }
      if (!imgv)
         continue;

      union pipe_color_union col_val;
      double dclear_val = 0;
      uint32_t sclear_val = 0;
      uint32_t ds_clear_flags = 0;
      if (att->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
         ds_clear_flags |= PIPE_CLEAR_DEPTH;
         dclear_val = att->clearValue.depthStencil.depth;
      }
      if (att->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
         ds_clear_flags |= PIPE_CLEAR_STENCIL;
         sclear_val = att->clearValue.depthStencil.stencil;
      }
      if (att->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
         for (unsigned i = 0; i < 4; i++)
            col_val.ui[i] = att->clearValue.color.uint32[i];
      }

      for (uint32_t r = 0; r < cmd->u.clear_attachments.rect_count; r++) {

         VkClearRect *rect = &cmd->u.clear_attachments.rects[r];
         /* avoid crashing on spec violations */
         rect->rect.offset.x = MAX2(rect->rect.offset.x, 0);
         rect->rect.offset.y = MAX2(rect->rect.offset.y, 0);
         rect->rect.extent.width = MIN2(rect->rect.extent.width, state->framebuffer.width - rect->rect.offset.x);
         rect->rect.extent.height = MIN2(rect->rect.extent.height, state->framebuffer.height - rect->rect.offset.y);
         if (state->info.view_mask) {
            u_foreach_bit(i, state->info.view_mask)
               clear_attachment_layers(state, imgv, &rect->rect,
                                       i, 1,
                                       ds_clear_flags, dclear_val, sclear_val,
                                       &col_val);
         } else
            clear_attachment_layers(state, imgv, &rect->rect,
                                    rect->baseArrayLayer, rect->layerCount,
                                    ds_clear_flags, dclear_val, sclear_val,
                                    &col_val);
      }
   }
}

static void handle_resolve_image(struct vk_cmd_queue_entry *cmd,
                                 struct rendering_state *state)
{
   int i;
   VkResolveImageInfo2 *resolvecmd = cmd->u.resolve_image2.resolve_image_info;
   LVP_FROM_HANDLE(lvp_image, src_image, resolvecmd->srcImage);
   LVP_FROM_HANDLE(lvp_image, dst_image, resolvecmd->dstImage);
   struct pipe_blit_info info;

   memset(&info, 0, sizeof(info));

   info.src.resource = src_image->bo;
   info.dst.resource = dst_image->bo;
   info.src.format = src_image->bo->format;
   info.dst.format = dst_image->bo->format;
   info.mask = util_format_is_depth_or_stencil(info.src.format) ? PIPE_MASK_ZS : PIPE_MASK_RGBA;
   info.filter = PIPE_TEX_FILTER_NEAREST;
   for (i = 0; i < resolvecmd->regionCount; i++) {
      int srcX0, srcY0;
      unsigned dstX0, dstY0;

      srcX0 = resolvecmd->pRegions[i].srcOffset.x;
      srcY0 = resolvecmd->pRegions[i].srcOffset.y;

      dstX0 = resolvecmd->pRegions[i].dstOffset.x;
      dstY0 = resolvecmd->pRegions[i].dstOffset.y;

      info.dst.box.x = dstX0;
      info.dst.box.y = dstY0;
      info.src.box.x = srcX0;
      info.src.box.y = srcY0;

      info.dst.box.width = resolvecmd->pRegions[i].extent.width;
      info.src.box.width = resolvecmd->pRegions[i].extent.width;
      info.dst.box.height = resolvecmd->pRegions[i].extent.height;
      info.src.box.height = resolvecmd->pRegions[i].extent.height;

      info.dst.box.depth = resolvecmd->pRegions[i].dstSubresource.layerCount;
      info.src.box.depth = resolvecmd->pRegions[i].srcSubresource.layerCount;

      info.src.level = resolvecmd->pRegions[i].srcSubresource.mipLevel;
      info.src.box.z = resolvecmd->pRegions[i].srcOffset.z + resolvecmd->pRegions[i].srcSubresource.baseArrayLayer;

      info.dst.level = resolvecmd->pRegions[i].dstSubresource.mipLevel;
      info.dst.box.z = resolvecmd->pRegions[i].dstOffset.z + resolvecmd->pRegions[i].dstSubresource.baseArrayLayer;

      state->pctx->blit(state->pctx, &info);
   }
}

static void handle_draw_indirect_count(struct vk_cmd_queue_entry *cmd,
                                       struct rendering_state *state, bool indexed)
{
   struct pipe_draw_start_count_bias draw = {0};
   if (indexed) {
      state->info.index_bounds_valid = false;
      state->info.index_size = state->index_size;
      state->info.index.resource = state->index_buffer;
      state->info.max_index = ~0;
   } else
      state->info.index_size = 0;
   state->indirect_info.offset = cmd->u.draw_indirect_count.offset;
   state->indirect_info.stride = cmd->u.draw_indirect_count.stride;
   state->indirect_info.draw_count = cmd->u.draw_indirect_count.max_draw_count;
   state->indirect_info.buffer = lvp_buffer_from_handle(cmd->u.draw_indirect_count.buffer)->bo;
   state->indirect_info.indirect_draw_count_offset = cmd->u.draw_indirect_count.count_buffer_offset;
   state->indirect_info.indirect_draw_count = lvp_buffer_from_handle(cmd->u.draw_indirect_count.count_buffer)->bo;

   state->pctx->set_patch_vertices(state->pctx, state->patch_vertices);
   state->pctx->draw_vbo(state->pctx, &state->info, 0, &state->indirect_info, &draw, 1);
}

static void handle_compute_push_descriptor_set(struct lvp_cmd_push_descriptor_set *pds,
                                               struct dyn_info *dyn_info,
                                               struct rendering_state *state)
{
   const struct lvp_descriptor_set_layout *layout =
      vk_to_lvp_descriptor_set_layout(pds->layout->vk.set_layouts[pds->set]);

   if (!(layout->shader_stages & VK_SHADER_STAGE_COMPUTE_BIT))
      return;
   for (unsigned i = 0; i < pds->set; i++) {
      increment_dyn_info(dyn_info, pds->layout->vk.set_layouts[i], false);
   }
   unsigned info_idx = 0;
   for (unsigned i = 0; i < pds->descriptor_write_count; i++) {
      struct lvp_write_descriptor *desc = &pds->descriptors[i];
      const struct lvp_descriptor_set_binding_layout *binding =
         &layout->binding[desc->dst_binding];

      if (!binding->valid)
         continue;

      for (unsigned j = 0; j < desc->descriptor_count; j++) {
         union lvp_descriptor_info *info = &pds->infos[info_idx + j];

         handle_descriptor(state, dyn_info, binding,
                           MESA_SHADER_COMPUTE, PIPE_SHADER_COMPUTE,
                           j, desc->descriptor_type,
                           info);
      }
      info_idx += desc->descriptor_count;
   }
}

static struct lvp_cmd_push_descriptor_set *
create_push_descriptor_set(struct rendering_state *state, struct vk_cmd_push_descriptor_set_khr *in_cmd)
{
   LVP_FROM_HANDLE(lvp_pipeline_layout, layout, in_cmd->layout);
   struct lvp_cmd_push_descriptor_set *out_cmd;
   int count_descriptors = 0;

   for (unsigned i = 0; i < in_cmd->descriptor_write_count; i++) {
      count_descriptors += in_cmd->descriptor_writes[i].descriptorCount;
   }

   void *descriptors;
   void *infos;
   void **ptrs[] = {&descriptors, &infos};
   size_t sizes[] = {
      in_cmd->descriptor_write_count * sizeof(struct lvp_write_descriptor),
      count_descriptors * sizeof(union lvp_descriptor_info),
   };
   out_cmd = ptrzalloc(sizeof(struct lvp_cmd_push_descriptor_set), 2, sizes, ptrs);
   if (!out_cmd)
      return NULL;

   out_cmd->bind_point = in_cmd->pipeline_bind_point;
   out_cmd->layout = layout;
   out_cmd->set = in_cmd->set;
   out_cmd->descriptor_write_count = in_cmd->descriptor_write_count;
   out_cmd->descriptors = descriptors;
   out_cmd->infos = infos;

   unsigned descriptor_index = 0;

   for (unsigned i = 0; i < in_cmd->descriptor_write_count; i++) {
      struct lvp_write_descriptor *desc = &out_cmd->descriptors[i];

      /* dstSet is ignored */
      desc->dst_binding = in_cmd->descriptor_writes[i].dstBinding;
      desc->dst_array_element = in_cmd->descriptor_writes[i].dstArrayElement;
      desc->descriptor_count = in_cmd->descriptor_writes[i].descriptorCount;
      desc->descriptor_type = in_cmd->descriptor_writes[i].descriptorType;

      for (unsigned j = 0; j < desc->descriptor_count; j++) {
         union lvp_descriptor_info *info = &out_cmd->infos[descriptor_index + j];
         switch (desc->descriptor_type) {
         case VK_DESCRIPTOR_TYPE_SAMPLER:
            if (in_cmd->descriptor_writes[i].pImageInfo[j].sampler)
               info->sampler = &lvp_sampler_from_handle(in_cmd->descriptor_writes[i].pImageInfo[j].sampler)->state;
            else
               info->sampler = NULL;
            break;
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            if (in_cmd->descriptor_writes[i].pImageInfo[j].sampler)
               info->sampler = &lvp_sampler_from_handle(in_cmd->descriptor_writes[i].pImageInfo[j].sampler)->state;
            else
               info->sampler = NULL;
            if (in_cmd->descriptor_writes[i].pImageInfo[j].imageView)
               info->sampler_view = lvp_image_view_from_handle(in_cmd->descriptor_writes[i].pImageInfo[j].imageView)->sv;
            else
               info->sampler_view = NULL;
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            if (in_cmd->descriptor_writes[i].pImageInfo[j].imageView)
               info->sampler_view = lvp_image_view_from_handle(in_cmd->descriptor_writes[i].pImageInfo[j].imageView)->sv;
            else
               info->sampler_view = NULL;
            break;
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            if (in_cmd->descriptor_writes[i].pImageInfo[j].imageView)
               info->image_view = lvp_image_view_from_handle(in_cmd->descriptor_writes[i].pImageInfo[j].imageView)->iv;
            else
               info->image_view = ((struct pipe_image_view){0});
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: {
            struct lvp_buffer_view *bview = lvp_buffer_view_from_handle(in_cmd->descriptor_writes[i].pTexelBufferView[j]);
            info->sampler_view = bview ? bview->sv : NULL;
            break;
         }
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
            struct lvp_buffer_view *bview = lvp_buffer_view_from_handle(in_cmd->descriptor_writes[i].pTexelBufferView[j]);
            info->image_view = bview ? bview->iv : ((struct pipe_image_view){0});
            break;
         }
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: {
            LVP_FROM_HANDLE(lvp_buffer, buffer, in_cmd->descriptor_writes[i].pBufferInfo[j].buffer);
            info->ubo.buffer = buffer ? buffer->bo : NULL;
            info->ubo.buffer_offset = buffer ? in_cmd->descriptor_writes[i].pBufferInfo[j].offset : 0;
            info->ubo.buffer_size = buffer ? in_cmd->descriptor_writes[i].pBufferInfo[j].range : 0;
            if (buffer && in_cmd->descriptor_writes[i].pBufferInfo[j].range == VK_WHOLE_SIZE)
               info->ubo.buffer_size = info->ubo.buffer->width0 - info->ubo.buffer_offset;
            break;
         }
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
            LVP_FROM_HANDLE(lvp_buffer, buffer, in_cmd->descriptor_writes[i].pBufferInfo[j].buffer);
            info->ssbo.buffer = buffer ? buffer->bo : NULL;
            info->ssbo.buffer_offset = buffer ? in_cmd->descriptor_writes[i].pBufferInfo[j].offset : 0;
            info->ssbo.buffer_size = buffer ? in_cmd->descriptor_writes[i].pBufferInfo[j].range : 0;
            if (buffer && in_cmd->descriptor_writes[i].pBufferInfo[j].range == VK_WHOLE_SIZE)
               info->ssbo.buffer_size = info->ssbo.buffer->width0 - info->ssbo.buffer_offset;
            break;
         }
         default:
            break;
         }
      }
      descriptor_index += desc->descriptor_count;
   }

   return out_cmd;
}

static void handle_push_descriptor_set_generic(struct vk_cmd_push_descriptor_set_khr *_pds,
                                               struct rendering_state *state)
{
   struct lvp_cmd_push_descriptor_set *pds = create_push_descriptor_set(state, _pds);
   const struct lvp_descriptor_set_layout *layout =
      vk_to_lvp_descriptor_set_layout(pds->layout->vk.set_layouts[pds->set]);

   struct dyn_info dyn_info;
   memset(&dyn_info.stage, 0, sizeof(dyn_info.stage));
   dyn_info.dyn_index = 0;
   if (pds->bind_point == VK_PIPELINE_BIND_POINT_COMPUTE) {
      handle_compute_push_descriptor_set(pds, &dyn_info, state);
   }

   for (unsigned i = 0; i < pds->set; i++) {
      increment_dyn_info(&dyn_info, pds->layout->vk.set_layouts[i], false);
   }

   unsigned info_idx = 0;
   for (unsigned i = 0; i < pds->descriptor_write_count; i++) {
      struct lvp_write_descriptor *desc = &pds->descriptors[i];
      const struct lvp_descriptor_set_binding_layout *binding =
         &layout->binding[desc->dst_binding];

      if (!binding->valid)
         continue;

      for (unsigned j = 0; j < desc->descriptor_count; j++) {
         union lvp_descriptor_info *info = &pds->infos[info_idx + j];

         if (layout->shader_stages & VK_SHADER_STAGE_VERTEX_BIT)
            handle_descriptor(state, &dyn_info, binding,
                              MESA_SHADER_VERTEX, PIPE_SHADER_VERTEX,
                              j, desc->descriptor_type,
                              info);
         if (layout->shader_stages & VK_SHADER_STAGE_FRAGMENT_BIT)
            handle_descriptor(state, &dyn_info, binding,
                              MESA_SHADER_FRAGMENT, PIPE_SHADER_FRAGMENT,
                              j, desc->descriptor_type,
                              info);
         if (layout->shader_stages & VK_SHADER_STAGE_GEOMETRY_BIT)
            handle_descriptor(state, &dyn_info, binding,
                              MESA_SHADER_GEOMETRY, PIPE_SHADER_GEOMETRY,
                              j, desc->descriptor_type,
                              info);
         if (layout->shader_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
            handle_descriptor(state, &dyn_info, binding,
                              MESA_SHADER_TESS_CTRL, PIPE_SHADER_TESS_CTRL,
                              j, desc->descriptor_type,
                              info);
         if (layout->shader_stages & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
            handle_descriptor(state, &dyn_info, binding,
                              MESA_SHADER_TESS_EVAL, PIPE_SHADER_TESS_EVAL,
                              j, desc->descriptor_type,
                              info);
      }
      info_idx += desc->descriptor_count;
   }
   free(pds);
}

static void handle_push_descriptor_set(struct vk_cmd_queue_entry *cmd,
                                       struct rendering_state *state)
{
   handle_push_descriptor_set_generic(&cmd->u.push_descriptor_set_khr, state);
}

static void handle_push_descriptor_set_with_template(struct vk_cmd_queue_entry *cmd,
                                                     struct rendering_state *state)
{
   LVP_FROM_HANDLE(lvp_descriptor_update_template, templ, cmd->u.push_descriptor_set_with_template_khr.descriptor_update_template);
   struct vk_cmd_push_descriptor_set_khr *pds;
   int pds_size = sizeof(*pds);

   pds_size += templ->entry_count * sizeof(struct VkWriteDescriptorSet);

   for (unsigned i = 0; i < templ->entry_count; i++) {
      VkDescriptorUpdateTemplateEntry *entry = &templ->entry[i];
      switch (entry->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         pds_size += sizeof(VkDescriptorImageInfo) * entry->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         pds_size += sizeof(VkBufferView) * entry->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      default:
         pds_size += sizeof(VkDescriptorBufferInfo) * entry->descriptorCount;
         break;
      }
   }

   pds = calloc(1, pds_size);
   if (!pds)
      return;

   pds->pipeline_bind_point = templ->bind_point;
   pds->layout = lvp_pipeline_layout_to_handle(templ->pipeline_layout);
   pds->set = templ->set;
   pds->descriptor_write_count = templ->entry_count;
   pds->descriptor_writes = (struct VkWriteDescriptorSet *)(pds + 1);
   const uint8_t *next_info = (const uint8_t *) (pds->descriptor_writes + templ->entry_count);

   const uint8_t *pSrc = cmd->u.push_descriptor_set_with_template_khr.data;
   for (unsigned i = 0; i < templ->entry_count; i++) {
      struct VkWriteDescriptorSet *desc = &pds->descriptor_writes[i];
      struct VkDescriptorUpdateTemplateEntry *entry = &templ->entry[i];

      /* dstSet is ignored */
      desc->dstBinding = entry->dstBinding;
      desc->dstArrayElement = entry->dstArrayElement;
      desc->descriptorCount = entry->descriptorCount;
      desc->descriptorType = entry->descriptorType;
      desc->pImageInfo = (const VkDescriptorImageInfo *) next_info;
      desc->pTexelBufferView = (const VkBufferView *) next_info;
      desc->pBufferInfo = (const VkDescriptorBufferInfo *) next_info;

      for (unsigned j = 0; j < desc->descriptorCount; j++) {
         switch (desc->descriptorType) {
         case VK_DESCRIPTOR_TYPE_SAMPLER:
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            memcpy((VkDescriptorImageInfo*)&desc->pImageInfo[j], pSrc, sizeof(VkDescriptorImageInfo));
            next_info += sizeof(VkDescriptorImageInfo);
            pSrc += sizeof(VkDescriptorImageInfo);
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            memcpy((VkBufferView*)&desc->pTexelBufferView[j], pSrc, sizeof(VkBufferView));
            next_info += sizeof(VkBufferView);
            pSrc += sizeof(VkBufferView);
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         default:
            memcpy((VkDescriptorBufferInfo*)&desc->pBufferInfo[j], pSrc, sizeof(VkDescriptorBufferInfo));
            next_info += sizeof(VkDescriptorBufferInfo);
            pSrc += sizeof(VkDescriptorBufferInfo);
            break;
         }
      }
   }
   handle_push_descriptor_set_generic(pds, state);
   free(pds);
}

static void handle_bind_transform_feedback_buffers(struct vk_cmd_queue_entry *cmd,
                                                   struct rendering_state *state)
{
   struct vk_cmd_bind_transform_feedback_buffers_ext *btfb = &cmd->u.bind_transform_feedback_buffers_ext;

   for (unsigned i = 0; i < btfb->binding_count; i++) {
      int idx = i + btfb->first_binding;
      uint32_t size;
      if (btfb->sizes && btfb->sizes[i] != VK_WHOLE_SIZE)
         size = btfb->sizes[i];
      else
         size = lvp_buffer_from_handle(btfb->buffers[i])->size - btfb->offsets[i];

      if (state->so_targets[idx])
         state->pctx->stream_output_target_destroy(state->pctx, state->so_targets[idx]);

      state->so_targets[idx] = state->pctx->create_stream_output_target(state->pctx,
                                                                        lvp_buffer_from_handle(btfb->buffers[i])->bo,
                                                                        btfb->offsets[i],
                                                                        size);
   }
   state->num_so_targets = btfb->first_binding + btfb->binding_count;
}

static void handle_begin_transform_feedback(struct vk_cmd_queue_entry *cmd,
                                            struct rendering_state *state)
{
   struct vk_cmd_begin_transform_feedback_ext *btf = &cmd->u.begin_transform_feedback_ext;
   uint32_t offsets[4];

   memset(offsets, 0, sizeof(uint32_t)*4);

   for (unsigned i = 0; btf->counter_buffers && i < btf->counter_buffer_count; i++) {
      if (!btf->counter_buffers[i])
         continue;

      pipe_buffer_read(state->pctx,
                       btf->counter_buffers ? lvp_buffer_from_handle(btf->counter_buffers[i])->bo : NULL,
                       btf->counter_buffer_offsets ? btf->counter_buffer_offsets[i] : 0,
                       4,
                       &offsets[i]);
   }
   state->pctx->set_stream_output_targets(state->pctx, state->num_so_targets,
                                          state->so_targets, offsets);
}

static void handle_end_transform_feedback(struct vk_cmd_queue_entry *cmd,
                                          struct rendering_state *state)
{
   struct vk_cmd_end_transform_feedback_ext *etf = &cmd->u.end_transform_feedback_ext;

   if (etf->counter_buffer_count) {
      for (unsigned i = 0; etf->counter_buffers && i < etf->counter_buffer_count; i++) {
         if (!etf->counter_buffers[i])
            continue;

         uint32_t offset;
         offset = state->pctx->stream_output_target_offset(state->so_targets[i]);

         pipe_buffer_write(state->pctx,
                           etf->counter_buffers ? lvp_buffer_from_handle(etf->counter_buffers[i])->bo : NULL,
                           etf->counter_buffer_offsets ? etf->counter_buffer_offsets[i] : 0,
                           4,
                           &offset);
      }
   }
   state->pctx->set_stream_output_targets(state->pctx, 0, NULL, NULL);
}

static void handle_draw_indirect_byte_count(struct vk_cmd_queue_entry *cmd,
                                            struct rendering_state *state)
{
   struct vk_cmd_draw_indirect_byte_count_ext *dibc = &cmd->u.draw_indirect_byte_count_ext;
   struct pipe_draw_start_count_bias draw = {0};

   pipe_buffer_read(state->pctx,
                    lvp_buffer_from_handle(dibc->counter_buffer)->bo,
                    dibc->counter_buffer_offset,
                    4, &draw.count);

   state->info.start_instance = cmd->u.draw_indirect_byte_count_ext.first_instance;
   state->info.instance_count = cmd->u.draw_indirect_byte_count_ext.instance_count;
   state->info.index_size = 0;

   draw.count /= cmd->u.draw_indirect_byte_count_ext.vertex_stride;
   state->pctx->set_patch_vertices(state->pctx, state->patch_vertices);
   state->pctx->draw_vbo(state->pctx, &state->info, 0, &state->indirect_info, &draw, 1);
}

static void handle_begin_conditional_rendering(struct vk_cmd_queue_entry *cmd,
                                               struct rendering_state *state)
{
   struct VkConditionalRenderingBeginInfoEXT *bcr = cmd->u.begin_conditional_rendering_ext.conditional_rendering_begin;
   state->pctx->render_condition_mem(state->pctx,
                                     lvp_buffer_from_handle(bcr->buffer)->bo,
                                     bcr->offset,
                                     bcr->flags & VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT);
}

static void handle_end_conditional_rendering(struct rendering_state *state)
{
   state->pctx->render_condition_mem(state->pctx, NULL, 0, false);
}

static void handle_set_vertex_input(struct vk_cmd_queue_entry *cmd,
                                    struct rendering_state *state)
{
   const struct vk_cmd_set_vertex_input_ext *vertex_input = &cmd->u.set_vertex_input_ext;
   const struct VkVertexInputBindingDescription2EXT *bindings = vertex_input->vertex_binding_descriptions;
   const struct VkVertexInputAttributeDescription2EXT *attrs = vertex_input->vertex_attribute_descriptions;
   int max_location = -1;
   for (unsigned i = 0; i < vertex_input->vertex_attribute_description_count; i++) {
      const struct VkVertexInputBindingDescription2EXT *binding = NULL;
      unsigned location = attrs[i].location;

      for (unsigned j = 0; j < vertex_input->vertex_binding_description_count; j++) {
         const struct VkVertexInputBindingDescription2EXT *b = &bindings[j];
         if (b->binding == attrs[i].binding) {
            binding = b;
            break;
         }
      }
      assert(binding);
      state->velem.velems[location].src_offset = attrs[i].offset;
      state->velem.velems[location].vertex_buffer_index = attrs[i].binding;
      state->velem.velems[location].src_format = lvp_vk_format_to_pipe_format(attrs[i].format);
      state->vb[attrs[i].binding].stride = binding->stride;
      uint32_t d = binding->divisor;
      switch (binding->inputRate) {
      case VK_VERTEX_INPUT_RATE_VERTEX:
         state->velem.velems[location].instance_divisor = 0;
         break;
      case VK_VERTEX_INPUT_RATE_INSTANCE:
         state->velem.velems[location].instance_divisor = d ? d : UINT32_MAX;
         break;
      default:
         assert(0);
         break;
      }

      if ((int)location > max_location)
         max_location = location;
   }
   state->velem.count = max_location + 1;
   state->vb_dirty = true;
   state->ve_dirty = true;
}

static void handle_set_cull_mode(struct vk_cmd_queue_entry *cmd,
                                 struct rendering_state *state)
{
   state->rs_state.cull_face = vk_cull_to_pipe(cmd->u.set_cull_mode.cull_mode);
   state->rs_dirty = true;
}

static void handle_set_front_face(struct vk_cmd_queue_entry *cmd,
                                  struct rendering_state *state)
{
   state->rs_state.front_ccw = (cmd->u.set_front_face.front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE);
   state->rs_dirty = true;
}

static void handle_set_primitive_topology(struct vk_cmd_queue_entry *cmd,
                                          struct rendering_state *state)
{
   state->info.mode = vk_conv_topology(cmd->u.set_primitive_topology.primitive_topology);
   state->rs_dirty = true;
}


static void handle_set_depth_test_enable(struct vk_cmd_queue_entry *cmd,
                                         struct rendering_state *state)
{
   state->dsa_dirty |= state->dsa_state.depth_enabled != cmd->u.set_depth_test_enable.depth_test_enable;
   state->dsa_state.depth_enabled = cmd->u.set_depth_test_enable.depth_test_enable;
}

static void handle_set_depth_write_enable(struct vk_cmd_queue_entry *cmd,
                                          struct rendering_state *state)
{
   state->dsa_dirty |= state->dsa_state.depth_writemask != cmd->u.set_depth_write_enable.depth_write_enable;
   state->dsa_state.depth_writemask = cmd->u.set_depth_write_enable.depth_write_enable;
}

static void handle_set_depth_compare_op(struct vk_cmd_queue_entry *cmd,
                                        struct rendering_state *state)
{
   state->dsa_dirty |= state->dsa_state.depth_func != cmd->u.set_depth_compare_op.depth_compare_op;
   state->dsa_state.depth_func = cmd->u.set_depth_compare_op.depth_compare_op;
}

static void handle_set_depth_bounds_test_enable(struct vk_cmd_queue_entry *cmd,
                                                struct rendering_state *state)
{
   state->dsa_dirty |= state->dsa_state.depth_bounds_test != cmd->u.set_depth_bounds_test_enable.depth_bounds_test_enable;
   state->dsa_state.depth_bounds_test = cmd->u.set_depth_bounds_test_enable.depth_bounds_test_enable;
}

static void handle_set_stencil_test_enable(struct vk_cmd_queue_entry *cmd,
                                           struct rendering_state *state)
{
   state->dsa_dirty |= state->dsa_state.stencil[0].enabled != cmd->u.set_stencil_test_enable.stencil_test_enable ||
                       state->dsa_state.stencil[1].enabled != cmd->u.set_stencil_test_enable.stencil_test_enable;
   state->dsa_state.stencil[0].enabled = cmd->u.set_stencil_test_enable.stencil_test_enable;
   state->dsa_state.stencil[1].enabled = cmd->u.set_stencil_test_enable.stencil_test_enable;
}

static void handle_set_stencil_op(struct vk_cmd_queue_entry *cmd,
                                  struct rendering_state *state)
{
   if (cmd->u.set_stencil_op.face_mask & VK_STENCIL_FACE_FRONT_BIT) {
      state->dsa_state.stencil[0].func = cmd->u.set_stencil_op.compare_op;
      state->dsa_state.stencil[0].fail_op = vk_conv_stencil_op(cmd->u.set_stencil_op.fail_op);
      state->dsa_state.stencil[0].zpass_op = vk_conv_stencil_op(cmd->u.set_stencil_op.pass_op);
      state->dsa_state.stencil[0].zfail_op = vk_conv_stencil_op(cmd->u.set_stencil_op.depth_fail_op);
   }

   if (cmd->u.set_stencil_op.face_mask & VK_STENCIL_FACE_BACK_BIT) {
      state->dsa_state.stencil[1].func = cmd->u.set_stencil_op.compare_op;
      state->dsa_state.stencil[1].fail_op = vk_conv_stencil_op(cmd->u.set_stencil_op.fail_op);
      state->dsa_state.stencil[1].zpass_op = vk_conv_stencil_op(cmd->u.set_stencil_op.pass_op);
      state->dsa_state.stencil[1].zfail_op = vk_conv_stencil_op(cmd->u.set_stencil_op.depth_fail_op);
   }
   state->dsa_dirty = true;
}

static void handle_set_line_stipple(struct vk_cmd_queue_entry *cmd,
                                    struct rendering_state *state)
{
   state->rs_state.line_stipple_factor = cmd->u.set_line_stipple_ext.line_stipple_factor - 1;
   state->rs_state.line_stipple_pattern = cmd->u.set_line_stipple_ext.line_stipple_pattern;
   state->rs_dirty = true;
}

static void handle_set_depth_bias_enable(struct vk_cmd_queue_entry *cmd,
                                         struct rendering_state *state)
{
   state->rs_dirty |= state->depth_bias.enabled != cmd->u.set_depth_bias_enable.depth_bias_enable;
   state->depth_bias.enabled = cmd->u.set_depth_bias_enable.depth_bias_enable;
}

static void handle_set_logic_op(struct vk_cmd_queue_entry *cmd,
                                struct rendering_state *state)
{
   unsigned op = vk_conv_logic_op(cmd->u.set_logic_op_ext.logic_op);
   state->rs_dirty |= state->blend_state.logicop_func != op;
   state->blend_state.logicop_func = op;
}

static void handle_set_patch_control_points(struct vk_cmd_queue_entry *cmd,
                                            struct rendering_state *state)
{
   state->patch_vertices = cmd->u.set_patch_control_points_ext.patch_control_points;
}

static void handle_set_primitive_restart_enable(struct vk_cmd_queue_entry *cmd,
                                                struct rendering_state *state)
{
   state->info.primitive_restart = cmd->u.set_primitive_restart_enable.primitive_restart_enable;
}

static void handle_set_rasterizer_discard_enable(struct vk_cmd_queue_entry *cmd,
                                                 struct rendering_state *state)
{
   state->rs_dirty |= state->rs_state.rasterizer_discard != cmd->u.set_rasterizer_discard_enable.rasterizer_discard_enable;
   state->rs_state.rasterizer_discard = cmd->u.set_rasterizer_discard_enable.rasterizer_discard_enable;
}

static void handle_set_color_write_enable(struct vk_cmd_queue_entry *cmd,
                                          struct rendering_state *state)
{
   uint8_t disable_mask = 0; //PIPE_MAX_COLOR_BUFS is max attachment count

   for (unsigned i = 0; i < cmd->u.set_color_write_enable_ext.attachment_count; i++) {
      /* this is inverted because cmdbufs are zero-initialized, meaning only 'true'
       * can be detected with a bool, and the default is to enable color writes
       */
      if (cmd->u.set_color_write_enable_ext.color_write_enables[i] != VK_TRUE)
         disable_mask |= BITFIELD_BIT(i);
   }

   state->blend_dirty |= state->color_write_disables != disable_mask;
   state->color_write_disables = disable_mask;
}

void lvp_add_enqueue_cmd_entrypoints(struct vk_device_dispatch_table *disp)
{
   struct vk_device_dispatch_table cmd_enqueue_dispatch;
   vk_device_dispatch_table_from_entrypoints(&cmd_enqueue_dispatch,
      &vk_cmd_enqueue_device_entrypoints, true);

#define ENQUEUE_CMD(CmdName) \
   assert(cmd_enqueue_dispatch.CmdName != NULL); \
   disp->CmdName = cmd_enqueue_dispatch.CmdName;

   /* This list needs to match what's in lvp_execute_cmd_buffer exactly */
   ENQUEUE_CMD(CmdBindPipeline)
   ENQUEUE_CMD(CmdSetViewport)
   ENQUEUE_CMD(CmdSetViewportWithCount)
   ENQUEUE_CMD(CmdSetScissor)
   ENQUEUE_CMD(CmdSetScissorWithCount)
   ENQUEUE_CMD(CmdSetLineWidth)
   ENQUEUE_CMD(CmdSetDepthBias)
   ENQUEUE_CMD(CmdSetBlendConstants)
   ENQUEUE_CMD(CmdSetDepthBounds)
   ENQUEUE_CMD(CmdSetStencilCompareMask)
   ENQUEUE_CMD(CmdSetStencilWriteMask)
   ENQUEUE_CMD(CmdSetStencilReference)
   ENQUEUE_CMD(CmdBindDescriptorSets)
   ENQUEUE_CMD(CmdBindIndexBuffer)
   ENQUEUE_CMD(CmdBindVertexBuffers2)
   ENQUEUE_CMD(CmdDraw)
   ENQUEUE_CMD(CmdDrawMultiEXT)
   ENQUEUE_CMD(CmdDrawIndexed)
   ENQUEUE_CMD(CmdDrawIndirect)
   ENQUEUE_CMD(CmdDrawIndexedIndirect)
   ENQUEUE_CMD(CmdDrawMultiIndexedEXT)
   ENQUEUE_CMD(CmdDispatch)
   ENQUEUE_CMD(CmdDispatchBase)
   ENQUEUE_CMD(CmdDispatchIndirect)
   ENQUEUE_CMD(CmdCopyBuffer2)
   ENQUEUE_CMD(CmdCopyImage2)
   ENQUEUE_CMD(CmdBlitImage2)
   ENQUEUE_CMD(CmdCopyBufferToImage2)
   ENQUEUE_CMD(CmdCopyImageToBuffer2)
   ENQUEUE_CMD(CmdUpdateBuffer)
   ENQUEUE_CMD(CmdFillBuffer)
   ENQUEUE_CMD(CmdClearColorImage)
   ENQUEUE_CMD(CmdClearDepthStencilImage)
   ENQUEUE_CMD(CmdClearAttachments)
   ENQUEUE_CMD(CmdResolveImage2)
   ENQUEUE_CMD(CmdBeginQueryIndexedEXT)
   ENQUEUE_CMD(CmdEndQueryIndexedEXT)
   ENQUEUE_CMD(CmdBeginQuery)
   ENQUEUE_CMD(CmdEndQuery)
   ENQUEUE_CMD(CmdResetQueryPool)
   ENQUEUE_CMD(CmdCopyQueryPoolResults)
   ENQUEUE_CMD(CmdPushConstants)
   ENQUEUE_CMD(CmdExecuteCommands)
   ENQUEUE_CMD(CmdDrawIndirectCount)
   ENQUEUE_CMD(CmdDrawIndexedIndirectCount)
   ENQUEUE_CMD(CmdPushDescriptorSetKHR)
//   ENQUEUE_CMD(CmdPushDescriptorSetWithTemplateKHR)
   ENQUEUE_CMD(CmdBindTransformFeedbackBuffersEXT)
   ENQUEUE_CMD(CmdBeginTransformFeedbackEXT)
   ENQUEUE_CMD(CmdEndTransformFeedbackEXT)
   ENQUEUE_CMD(CmdDrawIndirectByteCountEXT)
   ENQUEUE_CMD(CmdBeginConditionalRenderingEXT)
   ENQUEUE_CMD(CmdEndConditionalRenderingEXT)
   ENQUEUE_CMD(CmdSetVertexInputEXT)
   ENQUEUE_CMD(CmdSetCullMode)
   ENQUEUE_CMD(CmdSetFrontFace)
   ENQUEUE_CMD(CmdSetPrimitiveTopology)
   ENQUEUE_CMD(CmdSetDepthTestEnable)
   ENQUEUE_CMD(CmdSetDepthWriteEnable)
   ENQUEUE_CMD(CmdSetDepthCompareOp)
   ENQUEUE_CMD(CmdSetDepthBoundsTestEnable)
   ENQUEUE_CMD(CmdSetStencilTestEnable)
   ENQUEUE_CMD(CmdSetStencilOp)
   ENQUEUE_CMD(CmdSetLineStippleEXT)
   ENQUEUE_CMD(CmdSetDepthBiasEnable)
   ENQUEUE_CMD(CmdSetLogicOpEXT)
   ENQUEUE_CMD(CmdSetPatchControlPointsEXT)
   ENQUEUE_CMD(CmdSetPrimitiveRestartEnable)
   ENQUEUE_CMD(CmdSetRasterizerDiscardEnable)
   ENQUEUE_CMD(CmdSetColorWriteEnableEXT)
   ENQUEUE_CMD(CmdBeginRendering)
   ENQUEUE_CMD(CmdEndRendering)
   ENQUEUE_CMD(CmdSetDeviceMask)
   ENQUEUE_CMD(CmdPipelineBarrier2)
   ENQUEUE_CMD(CmdResetEvent2)
   ENQUEUE_CMD(CmdSetEvent2)
   ENQUEUE_CMD(CmdWaitEvents2)
   ENQUEUE_CMD(CmdWriteTimestamp2)

#undef ENQUEUE_CMD
}

static void lvp_execute_cmd_buffer(struct lvp_cmd_buffer *cmd_buffer,
                                   struct rendering_state *state)
{
   struct vk_cmd_queue_entry *cmd;
   bool first = true;
   bool did_flush = false;

   LIST_FOR_EACH_ENTRY(cmd, &cmd_buffer->vk.cmd_queue.cmds, cmd_link) {
      switch (cmd->type) {
      case VK_CMD_BIND_PIPELINE:
         handle_pipeline(cmd, state);
         break;
      case VK_CMD_SET_VIEWPORT:
         handle_set_viewport(cmd, state);
         break;
      case VK_CMD_SET_VIEWPORT_WITH_COUNT:
         handle_set_viewport_with_count(cmd, state);
         break;
      case VK_CMD_SET_SCISSOR:
         handle_set_scissor(cmd, state);
         break;
      case VK_CMD_SET_SCISSOR_WITH_COUNT:
         handle_set_scissor_with_count(cmd, state);
         break;
      case VK_CMD_SET_LINE_WIDTH:
         handle_set_line_width(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_BIAS:
         handle_set_depth_bias(cmd, state);
         break;
      case VK_CMD_SET_BLEND_CONSTANTS:
         handle_set_blend_constants(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_BOUNDS:
         handle_set_depth_bounds(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_COMPARE_MASK:
         handle_set_stencil_compare_mask(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_WRITE_MASK:
         handle_set_stencil_write_mask(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_REFERENCE:
         handle_set_stencil_reference(cmd, state);
         break;
      case VK_CMD_BIND_DESCRIPTOR_SETS:
         handle_descriptor_sets(cmd, state);
         break;
      case VK_CMD_BIND_INDEX_BUFFER:
         handle_index_buffer(cmd, state);
         break;
      case VK_CMD_BIND_VERTEX_BUFFERS2:
         handle_vertex_buffers2(cmd, state);
         break;
      case VK_CMD_DRAW:
         emit_state(state);
         handle_draw(cmd, state);
         break;
      case VK_CMD_DRAW_MULTI_EXT:
         emit_state(state);
         handle_draw_multi(cmd, state);
         break;
      case VK_CMD_DRAW_INDEXED:
         emit_state(state);
         handle_draw_indexed(cmd, state);
         break;
      case VK_CMD_DRAW_INDIRECT:
         emit_state(state);
         handle_draw_indirect(cmd, state, false);
         break;
      case VK_CMD_DRAW_INDEXED_INDIRECT:
         emit_state(state);
         handle_draw_indirect(cmd, state, true);
         break;
      case VK_CMD_DRAW_MULTI_INDEXED_EXT:
         emit_state(state);
         handle_draw_multi_indexed(cmd, state);
         break;
      case VK_CMD_DISPATCH:
         emit_compute_state(state);
         handle_dispatch(cmd, state);
         break;
      case VK_CMD_DISPATCH_BASE:
         emit_compute_state(state);
         handle_dispatch_base(cmd, state);
         break;
      case VK_CMD_DISPATCH_INDIRECT:
         emit_compute_state(state);
         handle_dispatch_indirect(cmd, state);
         break;
      case VK_CMD_COPY_BUFFER2:
         handle_copy_buffer(cmd, state);
         break;
      case VK_CMD_COPY_IMAGE2:
         handle_copy_image(cmd, state);
         break;
      case VK_CMD_BLIT_IMAGE2:
         handle_blit_image(cmd, state);
         break;
      case VK_CMD_COPY_BUFFER_TO_IMAGE2:
         handle_copy_buffer_to_image(cmd, state);
         break;
      case VK_CMD_COPY_IMAGE_TO_BUFFER2:
         handle_copy_image_to_buffer2(cmd, state);
         break;
      case VK_CMD_UPDATE_BUFFER:
         handle_update_buffer(cmd, state);
         break;
      case VK_CMD_FILL_BUFFER:
         handle_fill_buffer(cmd, state);
         break;
      case VK_CMD_CLEAR_COLOR_IMAGE:
         handle_clear_color_image(cmd, state);
         break;
      case VK_CMD_CLEAR_DEPTH_STENCIL_IMAGE:
         handle_clear_ds_image(cmd, state);
         break;
      case VK_CMD_CLEAR_ATTACHMENTS:
         handle_clear_attachments(cmd, state);
         break;
      case VK_CMD_RESOLVE_IMAGE2:
         handle_resolve_image(cmd, state);
         break;
      case VK_CMD_PIPELINE_BARRIER2:
         /* skip flushes since every cmdbuf does a flush
            after iterating its cmds and so this is redundant
          */
         if (first || did_flush || cmd->cmd_link.next == &cmd_buffer->vk.cmd_queue.cmds)
            continue;
         handle_pipeline_barrier(cmd, state);
         did_flush = true;
         continue;
      case VK_CMD_BEGIN_QUERY_INDEXED_EXT:
         handle_begin_query_indexed_ext(cmd, state);
         break;
      case VK_CMD_END_QUERY_INDEXED_EXT:
         handle_end_query_indexed_ext(cmd, state);
         break;
      case VK_CMD_BEGIN_QUERY:
         handle_begin_query(cmd, state);
         break;
      case VK_CMD_END_QUERY:
         handle_end_query(cmd, state);
         break;
      case VK_CMD_RESET_QUERY_POOL:
         handle_reset_query_pool(cmd, state);
         break;
      case VK_CMD_COPY_QUERY_POOL_RESULTS:
         handle_copy_query_pool_results(cmd, state);
         break;
      case VK_CMD_PUSH_CONSTANTS:
         handle_push_constants(cmd, state);
         break;
      case VK_CMD_EXECUTE_COMMANDS:
         handle_execute_commands(cmd, state);
         break;
      case VK_CMD_DRAW_INDIRECT_COUNT:
         emit_state(state);
         handle_draw_indirect_count(cmd, state, false);
         break;
      case VK_CMD_DRAW_INDEXED_INDIRECT_COUNT:
         emit_state(state);
         handle_draw_indirect_count(cmd, state, true);
         break;
      case VK_CMD_PUSH_DESCRIPTOR_SET_KHR:
         handle_push_descriptor_set(cmd, state);
         break;
      case VK_CMD_PUSH_DESCRIPTOR_SET_WITH_TEMPLATE_KHR:
         handle_push_descriptor_set_with_template(cmd, state);
         break;
      case VK_CMD_BIND_TRANSFORM_FEEDBACK_BUFFERS_EXT:
         handle_bind_transform_feedback_buffers(cmd, state);
         break;
      case VK_CMD_BEGIN_TRANSFORM_FEEDBACK_EXT:
         handle_begin_transform_feedback(cmd, state);
         break;
      case VK_CMD_END_TRANSFORM_FEEDBACK_EXT:
         handle_end_transform_feedback(cmd, state);
         break;
      case VK_CMD_DRAW_INDIRECT_BYTE_COUNT_EXT:
         emit_state(state);
         handle_draw_indirect_byte_count(cmd, state);
         break;
      case VK_CMD_BEGIN_CONDITIONAL_RENDERING_EXT:
         handle_begin_conditional_rendering(cmd, state);
         break;
      case VK_CMD_END_CONDITIONAL_RENDERING_EXT:
         handle_end_conditional_rendering(state);
         break;
      case VK_CMD_SET_VERTEX_INPUT_EXT:
         handle_set_vertex_input(cmd, state);
         break;
      case VK_CMD_SET_CULL_MODE:
         handle_set_cull_mode(cmd, state);
         break;
      case VK_CMD_SET_FRONT_FACE:
         handle_set_front_face(cmd, state);
         break;
      case VK_CMD_SET_PRIMITIVE_TOPOLOGY:
         handle_set_primitive_topology(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_TEST_ENABLE:
         handle_set_depth_test_enable(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_WRITE_ENABLE:
         handle_set_depth_write_enable(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_COMPARE_OP:
         handle_set_depth_compare_op(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_BOUNDS_TEST_ENABLE:
         handle_set_depth_bounds_test_enable(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_TEST_ENABLE:
         handle_set_stencil_test_enable(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_OP:
         handle_set_stencil_op(cmd, state);
         break;
      case VK_CMD_SET_LINE_STIPPLE_EXT:
         handle_set_line_stipple(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_BIAS_ENABLE:
         handle_set_depth_bias_enable(cmd, state);
         break;
      case VK_CMD_SET_LOGIC_OP_EXT:
         handle_set_logic_op(cmd, state);
         break;
      case VK_CMD_SET_PATCH_CONTROL_POINTS_EXT:
         handle_set_patch_control_points(cmd, state);
         break;
      case VK_CMD_SET_PRIMITIVE_RESTART_ENABLE:
         handle_set_primitive_restart_enable(cmd, state);
         break;
      case VK_CMD_SET_RASTERIZER_DISCARD_ENABLE:
         handle_set_rasterizer_discard_enable(cmd, state);
         break;
      case VK_CMD_SET_COLOR_WRITE_ENABLE_EXT:
         handle_set_color_write_enable(cmd, state);
         break;
      case VK_CMD_BEGIN_RENDERING:
         handle_begin_rendering(cmd, state);
         break;
      case VK_CMD_END_RENDERING:
         handle_end_rendering(cmd, state);
         break;
      case VK_CMD_SET_DEVICE_MASK:
         /* no-op */
         break;
      case VK_CMD_RESET_EVENT2:
         handle_event_reset2(cmd, state);
         break;
      case VK_CMD_SET_EVENT2:
         handle_event_set2(cmd, state);
         break;
      case VK_CMD_WAIT_EVENTS2:
         handle_wait_events2(cmd, state);
         break;
      case VK_CMD_WRITE_TIMESTAMP2:
         handle_write_timestamp2(cmd, state);
         break;
      default:
         fprintf(stderr, "Unsupported command %s\n", vk_cmd_queue_type_names[cmd->type]);
         unreachable("Unsupported command");
         break;
      }
      first = false;
      did_flush = false;
   }
}

VkResult lvp_execute_cmds(struct lvp_device *device,
                          struct lvp_queue *queue,
                          struct lvp_cmd_buffer *cmd_buffer)
{
   struct rendering_state *state = queue->state;
   memset(state, 0, sizeof(*state));
   state->pctx = queue->ctx;
   state->uploader = queue->uploader;
   state->cso = queue->cso;
   state->blend_dirty = true;
   state->dsa_dirty = true;
   state->rs_dirty = true;
   state->vp_dirty = true;
   state->rs_state.point_tri_clip = true;
   state->rs_state.unclamped_fragment_depth_values = device->vk.enabled_extensions.EXT_depth_range_unrestricted;
   for (enum pipe_shader_type s = PIPE_SHADER_VERTEX; s < PIPE_SHADER_TYPES; s++) {
      for (unsigned i = 0; i < ARRAY_SIZE(state->cso_ss_ptr[s]); i++)
         state->cso_ss_ptr[s][i] = &state->ss[s][i];
   }
   /* create a gallium context */
   lvp_execute_cmd_buffer(cmd_buffer, state);

   state->start_vb = -1;
   state->num_vb = 0;
   cso_unbind_context(queue->cso);
   for (unsigned i = 0; i < ARRAY_SIZE(state->so_targets); i++) {
      if (state->so_targets[i]) {
         state->pctx->stream_output_target_destroy(state->pctx, state->so_targets[i]);
      }
   }

   free(state->color_att);
   return VK_SUCCESS;
}

size_t
lvp_get_rendering_state_size(void)
{
   return sizeof(struct rendering_state);
}
