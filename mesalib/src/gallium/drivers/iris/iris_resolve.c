/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file iris_resolve.c
 *
 * This file handles resolve tracking for main and auxiliary surfaces.
 *
 * It also handles our cache tracking.  We have sets for the render cache,
 * depth cache, and so on.  If a BO is in a cache's set, then it may have
 * data in that cache.  The helpers take care of emitting flushes for
 * render-to-texture, format reinterpretation issues, and other situations.
 */

#include "util/hash_table.h"
#include "util/set.h"
#include "iris_context.h"
#include "compiler/nir/nir.h"

/**
 * Disable auxiliary buffers if a renderbuffer is also bound as a texture
 * or shader image.  This causes a self-dependency, where both rendering
 * and sampling may concurrently read or write the CCS buffer, causing
 * incorrect pixels.
 */
static bool
disable_rb_aux_buffer(struct iris_context *ice,
                      bool *draw_aux_buffer_disabled,
                      struct iris_resource *tex_res,
                      unsigned min_level, unsigned num_levels,
                      const char *usage)
{
   struct pipe_framebuffer_state *cso_fb = &ice->state.framebuffer;
   bool found = false;

   /* We only need to worry about color compression and fast clears. */
   if (tex_res->aux.usage != ISL_AUX_USAGE_CCS_D &&
       tex_res->aux.usage != ISL_AUX_USAGE_CCS_E &&
       tex_res->aux.usage != ISL_AUX_USAGE_GFX12_CCS_E)
      return false;

   for (unsigned i = 0; i < cso_fb->nr_cbufs; i++) {
      struct iris_surface *surf = (void *) cso_fb->cbufs[i];
      if (!surf)
         continue;

      struct iris_resource *rb_res = (void *) surf->base.texture;

      if (rb_res->bo == tex_res->bo &&
          surf->base.u.tex.level >= min_level &&
          surf->base.u.tex.level < min_level + num_levels) {
         found = draw_aux_buffer_disabled[i] = true;
      }
   }

   if (found) {
      perf_debug(&ice->dbg,
                 "Disabling CCS because a renderbuffer is also bound %s.\n",
                 usage);
   }

   return found;
}

static void
resolve_sampler_views(struct iris_context *ice,
                      struct iris_batch *batch,
                      struct iris_shader_state *shs,
                      const struct shader_info *info,
                      bool *draw_aux_buffer_disabled,
                      bool consider_framebuffer)
{
   uint32_t views = info ? (shs->bound_sampler_views & info->textures_used[0]) : 0;

   while (views) {
      const int i = u_bit_scan(&views);
      struct iris_sampler_view *isv = shs->textures[i];

      if (isv->res->base.b.target != PIPE_BUFFER) {
         if (consider_framebuffer) {
            disable_rb_aux_buffer(ice, draw_aux_buffer_disabled, isv->res,
                                  isv->view.base_level, isv->view.levels,
                                  "for sampling");
         }

         iris_resource_prepare_texture(ice, isv->res, isv->view.format,
                                       isv->view.base_level, isv->view.levels,
                                       isv->view.base_array_layer,
                                       isv->view.array_len);
      }

      iris_emit_buffer_barrier_for(batch, isv->res->bo,
                                   IRIS_DOMAIN_OTHER_READ);
   }
}

static void
resolve_image_views(struct iris_context *ice,
                    struct iris_batch *batch,
                    struct iris_shader_state *shs,
                    const struct shader_info *info,
                    bool *draw_aux_buffer_disabled,
                    bool consider_framebuffer)
{
   uint32_t views = info ? (shs->bound_image_views & info->images_used) : 0;

   while (views) {
      const int i = u_bit_scan(&views);
      struct pipe_image_view *pview = &shs->image[i].base;
      struct iris_resource *res = (void *) pview->resource;

      if (res->base.b.target != PIPE_BUFFER) {
         if (consider_framebuffer) {
            disable_rb_aux_buffer(ice, draw_aux_buffer_disabled,
                                  res, pview->u.tex.level, 1,
                                  "as a shader image");
         }

         unsigned num_layers =
            pview->u.tex.last_layer - pview->u.tex.first_layer + 1;

         enum isl_aux_usage aux_usage =
            iris_image_view_aux_usage(ice, pview, info);

         iris_resource_prepare_access(ice, res,
                                      pview->u.tex.level, 1,
                                      pview->u.tex.first_layer, num_layers,
                                      aux_usage, false);
      }

      iris_emit_buffer_barrier_for(batch, res->bo, IRIS_DOMAIN_DATA_WRITE);
   }
}

/**
 * \brief Resolve buffers before drawing.
 *
 * Resolve the depth buffer's HiZ buffer, resolve the depth buffer of each
 * enabled depth texture, and flush the render cache for any dirty textures.
 */
void
iris_predraw_resolve_inputs(struct iris_context *ice,
                            struct iris_batch *batch,
                            bool *draw_aux_buffer_disabled,
                            gl_shader_stage stage,
                            bool consider_framebuffer)
{
   struct iris_shader_state *shs = &ice->state.shaders[stage];
   const struct shader_info *info = iris_get_shader_info(ice, stage);

   uint64_t stage_dirty = (IRIS_STAGE_DIRTY_BINDINGS_VS << stage) |
      (consider_framebuffer ? IRIS_STAGE_DIRTY_BINDINGS_FS : 0);

   if (ice->state.stage_dirty & stage_dirty) {
      resolve_sampler_views(ice, batch, shs, info, draw_aux_buffer_disabled,
                            consider_framebuffer);
      resolve_image_views(ice, batch, shs, info, draw_aux_buffer_disabled,
                          consider_framebuffer);
   }
}

void
iris_predraw_resolve_framebuffer(struct iris_context *ice,
                                 struct iris_batch *batch,
                                 bool *draw_aux_buffer_disabled)
{
   struct pipe_framebuffer_state *cso_fb = &ice->state.framebuffer;
   struct iris_screen *screen = (void *) ice->ctx.screen;
   struct intel_device_info *devinfo = &screen->devinfo;
   struct iris_uncompiled_shader *ish =
      ice->shaders.uncompiled[MESA_SHADER_FRAGMENT];
   const nir_shader *nir = ish->nir;

   if (ice->state.dirty & IRIS_DIRTY_DEPTH_BUFFER) {
      struct pipe_surface *zs_surf = cso_fb->zsbuf;

      if (zs_surf) {
         struct iris_resource *z_res, *s_res;
         iris_get_depth_stencil_resources(zs_surf->texture, &z_res, &s_res);
         unsigned num_layers =
            zs_surf->u.tex.last_layer - zs_surf->u.tex.first_layer + 1;

         if (z_res) {
            iris_resource_prepare_render(ice, z_res, zs_surf->u.tex.level,
                                         zs_surf->u.tex.first_layer,
                                         num_layers, ice->state.hiz_usage);
            iris_emit_buffer_barrier_for(batch, z_res->bo,
                                         IRIS_DOMAIN_DEPTH_WRITE);
         }

         if (s_res) {
            iris_emit_buffer_barrier_for(batch, s_res->bo,
                                         IRIS_DOMAIN_DEPTH_WRITE);
         }
      }
   }

   if (devinfo->ver == 8 && nir->info.outputs_read != 0) {
      for (unsigned i = 0; i < cso_fb->nr_cbufs; i++) {
         if (cso_fb->cbufs[i]) {
            struct iris_surface *surf = (void *) cso_fb->cbufs[i];
            struct iris_resource *res = (void *) cso_fb->cbufs[i]->texture;

            iris_resource_prepare_texture(ice, res, surf->view.format,
                                          surf->view.base_level, 1,
                                          surf->view.base_array_layer,
                                          surf->view.array_len);
         }
      }
   }

   if (ice->state.stage_dirty & IRIS_STAGE_DIRTY_BINDINGS_FS) {
      for (unsigned i = 0; i < cso_fb->nr_cbufs; i++) {
         struct iris_surface *surf = (void *) cso_fb->cbufs[i];
         if (!surf)
            continue;

         struct iris_resource *res = (void *) surf->base.texture;

         enum isl_aux_usage aux_usage =
            iris_resource_render_aux_usage(ice, res, surf->view.base_level,
                                           surf->view.format,
                                           draw_aux_buffer_disabled[i]);

         if (ice->state.draw_aux_usage[i] != aux_usage) {
            ice->state.draw_aux_usage[i] = aux_usage;
            /* XXX: Need to track which bindings to make dirty */
            ice->state.dirty |= IRIS_DIRTY_RENDER_BUFFER;
            ice->state.stage_dirty |= IRIS_ALL_STAGE_DIRTY_BINDINGS;
         }

         iris_resource_prepare_render(ice, res, surf->view.base_level,
                                      surf->view.base_array_layer,
                                      surf->view.array_len,
                                      aux_usage);

         iris_cache_flush_for_render(batch, res->bo, aux_usage);
      }
   }
}

/**
 * \brief Call this after drawing to mark which buffers need resolving
 *
 * If the depth buffer was written to and if it has an accompanying HiZ
 * buffer, then mark that it needs a depth resolve.
 *
 * If the color buffer is a multisample window system buffer, then
 * mark that it needs a downsample.
 *
 * Also mark any render targets which will be textured as needing a render
 * cache flush.
 */
void
iris_postdraw_update_resolve_tracking(struct iris_context *ice,
                                      struct iris_batch *batch)
{
   struct pipe_framebuffer_state *cso_fb = &ice->state.framebuffer;

   // XXX: front buffer drawing?

   bool may_have_resolved_depth =
      ice->state.dirty & (IRIS_DIRTY_DEPTH_BUFFER |
                          IRIS_DIRTY_WM_DEPTH_STENCIL);

   struct pipe_surface *zs_surf = cso_fb->zsbuf;
   if (zs_surf) {
      struct iris_resource *z_res, *s_res;
      iris_get_depth_stencil_resources(zs_surf->texture, &z_res, &s_res);
      unsigned num_layers =
         zs_surf->u.tex.last_layer - zs_surf->u.tex.first_layer + 1;

      if (z_res) {
         if (may_have_resolved_depth && ice->state.depth_writes_enabled) {
            iris_resource_finish_render(ice, z_res, zs_surf->u.tex.level,
                                        zs_surf->u.tex.first_layer,
                                        num_layers, ice->state.hiz_usage);
         }
      }

      if (s_res) {
         if (may_have_resolved_depth && ice->state.stencil_writes_enabled) {
            iris_resource_finish_write(ice, s_res, zs_surf->u.tex.level,
                                       zs_surf->u.tex.first_layer, num_layers,
                                       s_res->aux.usage);
         }
      }
   }

   bool may_have_resolved_color =
      ice->state.stage_dirty & IRIS_STAGE_DIRTY_BINDINGS_FS;

   for (unsigned i = 0; i < cso_fb->nr_cbufs; i++) {
      struct iris_surface *surf = (void *) cso_fb->cbufs[i];
      if (!surf)
         continue;

      struct iris_resource *res = (void *) surf->base.texture;
      enum isl_aux_usage aux_usage = ice->state.draw_aux_usage[i];

      if (may_have_resolved_color) {
         union pipe_surface_desc *desc = &surf->base.u;
         unsigned num_layers =
            desc->tex.last_layer - desc->tex.first_layer + 1;
         iris_resource_finish_render(ice, res, desc->tex.level,
                                     desc->tex.first_layer, num_layers,
                                     aux_usage);
      }
   }
}

void
iris_cache_flush_for_render(struct iris_batch *batch,
                            struct iris_bo *bo,
                            enum isl_aux_usage aux_usage)
{
   iris_emit_buffer_barrier_for(batch, bo, IRIS_DOMAIN_RENDER_WRITE);

   /* Check to see if this bo has been used by a previous rendering operation
    * but with a different aux usage.  If it has, flush the render cache so we
    * ensure that it's only in there with one aux usage at a time.
    *
    * Even though it's not obvious, this can easily happen in practice.
    * Suppose a client is blending on a surface with sRGB encode enabled on
    * gfx9.  This implies that you get AUX_USAGE_CCS_D at best.  If the client
    * then disables sRGB decode and continues blending we will flip on
    * AUX_USAGE_CCS_E without doing any sort of resolve in-between (this is
    * perfectly valid since CCS_E is a subset of CCS_D).  However, this means
    * that we have fragments in-flight which are rendering with UNORM+CCS_E
    * and other fragments in-flight with SRGB+CCS_D on the same surface at the
    * same time and the pixel scoreboard and color blender are trying to sort
    * it all out.  This ends badly (i.e. GPU hangs).
    *
    * There are comments in various docs which indicate that the render cache
    * isn't 100% resilient to format changes.  However, to date, we have never
    * observed GPU hangs or even corruption to be associated with switching the
    * format, only the aux usage.  So we let that slide for now.
    */
   void *v_aux_usage = (void *) (uintptr_t) aux_usage;
   struct hash_entry *entry =
      _mesa_hash_table_search_pre_hashed(batch->cache.render, bo->hash, bo);
   if (!entry) {
      _mesa_hash_table_insert_pre_hashed(batch->cache.render, bo->hash, bo,
                                         v_aux_usage);
   } else if (entry->data != v_aux_usage) {
      iris_emit_pipe_control_flush(batch,
                                   "cache tracker: aux usage mismatch",
                                   PIPE_CONTROL_RENDER_TARGET_FLUSH |
                                   PIPE_CONTROL_TILE_CACHE_FLUSH |
                                   PIPE_CONTROL_CS_STALL);
      entry->data = v_aux_usage;
   }
}

static void
flush_ubos(struct iris_batch *batch,
            struct iris_shader_state *shs)
{
   uint32_t cbufs = shs->dirty_cbufs & shs->bound_cbufs;

   while (cbufs) {
      const int i = u_bit_scan(&cbufs);
      struct pipe_shader_buffer *cbuf = &shs->constbuf[i];
      struct iris_resource *res = (void *)cbuf->buffer;
      iris_emit_buffer_barrier_for(batch, res->bo, IRIS_DOMAIN_OTHER_READ);
   }

   shs->dirty_cbufs = 0;
}

static void
flush_ssbos(struct iris_batch *batch,
            struct iris_shader_state *shs)
{
   uint32_t ssbos = shs->bound_ssbos;

   while (ssbos) {
      const int i = u_bit_scan(&ssbos);
      struct pipe_shader_buffer *ssbo = &shs->ssbo[i];
      struct iris_resource *res = (void *)ssbo->buffer;
      iris_emit_buffer_barrier_for(batch, res->bo, IRIS_DOMAIN_DATA_WRITE);
   }
}

void
iris_predraw_flush_buffers(struct iris_context *ice,
                           struct iris_batch *batch,
                           gl_shader_stage stage)
{
   struct iris_shader_state *shs = &ice->state.shaders[stage];

   if (ice->state.stage_dirty & (IRIS_STAGE_DIRTY_CONSTANTS_VS << stage))
      flush_ubos(batch, shs);

   if (ice->state.stage_dirty & (IRIS_STAGE_DIRTY_BINDINGS_VS << stage))
      flush_ssbos(batch, shs);
}

static void
iris_resolve_color(struct iris_context *ice,
                   struct iris_batch *batch,
                   struct iris_resource *res,
                   unsigned level, unsigned layer,
                   enum isl_aux_op resolve_op)
{
   //DBG("%s to mt %p level %u layer %u\n", __FUNCTION__, mt, level, layer);

   struct blorp_surf surf;
   iris_blorp_surf_for_resource(&batch->screen->isl_dev, &surf,
                                &res->base.b, res->aux.usage, level, true);

   iris_batch_maybe_flush(batch, 1500);

   /* Ivybridge PRM Vol 2, Part 1, "11.7 MCS Buffer for Render Target(s)":
    *
    *    "Any transition from any value in {Clear, Render, Resolve} to a
    *     different value in {Clear, Render, Resolve} requires end of pipe
    *     synchronization."
    *
    * In other words, fast clear ops are not properly synchronized with
    * other drawing.  We need to use a PIPE_CONTROL to ensure that the
    * contents of the previous draw hit the render target before we resolve
    * and again afterwards to ensure that the resolve is complete before we
    * do any more regular drawing.
    */
   iris_emit_end_of_pipe_sync(batch, "color resolve: pre-flush",
                              PIPE_CONTROL_RENDER_TARGET_FLUSH);

   iris_batch_sync_region_start(batch);
   struct blorp_batch blorp_batch;
   blorp_batch_init(&ice->blorp, &blorp_batch, batch, 0);
   blorp_ccs_resolve(&blorp_batch, &surf, level, layer, 1, res->surf.format,
                     resolve_op);
   blorp_batch_finish(&blorp_batch);

   /* See comment above */
   iris_emit_end_of_pipe_sync(batch, "color resolve: post-flush",
                              PIPE_CONTROL_RENDER_TARGET_FLUSH);
   iris_batch_sync_region_end(batch);
}

static void
iris_mcs_partial_resolve(struct iris_context *ice,
                         struct iris_batch *batch,
                         struct iris_resource *res,
                         uint32_t start_layer,
                         uint32_t num_layers)
{
   //DBG("%s to mt %p layers %u-%u\n", __FUNCTION__, mt,
       //start_layer, start_layer + num_layers - 1);

   assert(isl_aux_usage_has_mcs(res->aux.usage));

   iris_batch_maybe_flush(batch, 1500);

   struct blorp_surf surf;
   iris_blorp_surf_for_resource(&batch->screen->isl_dev, &surf,
                                &res->base.b, res->aux.usage, 0, true);
   iris_emit_buffer_barrier_for(batch, res->bo, IRIS_DOMAIN_RENDER_WRITE);

   struct blorp_batch blorp_batch;
   iris_batch_sync_region_start(batch);
   blorp_batch_init(&ice->blorp, &blorp_batch, batch, 0);
   blorp_mcs_partial_resolve(&blorp_batch, &surf, res->surf.format,
                             start_layer, num_layers);
   blorp_batch_finish(&blorp_batch);
   iris_batch_sync_region_end(batch);
}

bool
iris_sample_with_depth_aux(const struct intel_device_info *devinfo,
                           const struct iris_resource *res)
{
   switch (res->aux.usage) {
   case ISL_AUX_USAGE_HIZ:
      if (devinfo->has_sample_with_hiz)
         break;
      return false;
   case ISL_AUX_USAGE_HIZ_CCS:
      return false;
   case ISL_AUX_USAGE_HIZ_CCS_WT:
      break;
   default:
      return false;
   }

   for (unsigned level = 0; level < res->surf.levels; ++level) {
      if (!iris_resource_level_has_hiz(res, level))
         return false;
   }

   /* From the BDW PRM (Volume 2d: Command Reference: Structures
    *                   RENDER_SURFACE_STATE.AuxiliarySurfaceMode):
    *
    *  "If this field is set to AUX_HIZ, Number of Multisamples must be
    *   MULTISAMPLECOUNT_1, and Surface Type cannot be SURFTYPE_3D.
    *
    * There is no such blurb for 1D textures, but there is sufficient evidence
    * that this is broken on SKL+.
    */
   return res->surf.samples == 1 && res->surf.dim == ISL_SURF_DIM_2D;
}

/**
 * Perform a HiZ or depth resolve operation.
 *
 * For an overview of HiZ ops, see the following sections of the Sandy Bridge
 * PRM, Volume 1, Part 2:
 *   - 7.5.3.1 Depth Buffer Clear
 *   - 7.5.3.2 Depth Buffer Resolve
 *   - 7.5.3.3 Hierarchical Depth Buffer Resolve
 */
void
iris_hiz_exec(struct iris_context *ice,
              struct iris_batch *batch,
              struct iris_resource *res,
              unsigned int level, unsigned int start_layer,
              unsigned int num_layers, enum isl_aux_op op,
              bool update_clear_depth)
{
   assert(iris_resource_level_has_hiz(res, level));
   assert(op != ISL_AUX_OP_NONE);
   UNUSED const char *name = NULL;

   iris_batch_maybe_flush(batch, 1500);

   switch (op) {
   case ISL_AUX_OP_FULL_RESOLVE:
      name = "depth resolve";
      break;
   case ISL_AUX_OP_AMBIGUATE:
      name = "hiz ambiguate";
      break;
   case ISL_AUX_OP_FAST_CLEAR:
      name = "depth clear";
      break;
   case ISL_AUX_OP_PARTIAL_RESOLVE:
   case ISL_AUX_OP_NONE:
      unreachable("Invalid HiZ op");
   }

   //DBG("%s %s to mt %p level %d layers %d-%d\n",
       //__func__, name, mt, level, start_layer, start_layer + num_layers - 1);

   /* The following stalls and flushes are only documented to be required
    * for HiZ clear operations.  However, they also seem to be required for
    * resolve operations.
    *
    * From the Ivybridge PRM, volume 2, "Depth Buffer Clear":
    *
    *   "If other rendering operations have preceded this clear, a
    *    PIPE_CONTROL with depth cache flush enabled, Depth Stall bit
    *    enabled must be issued before the rectangle primitive used for
    *    the depth buffer clear operation."
    *
    * Same applies for Gfx8 and Gfx9.
    */
   iris_emit_pipe_control_flush(batch,
                                "hiz op: pre-flush",
                                PIPE_CONTROL_DEPTH_CACHE_FLUSH |
                                PIPE_CONTROL_DEPTH_STALL |
                                PIPE_CONTROL_CS_STALL);

   iris_batch_sync_region_start(batch);

   struct blorp_surf surf;
   iris_blorp_surf_for_resource(&batch->screen->isl_dev, &surf,
                                &res->base.b, res->aux.usage, level, true);

   struct blorp_batch blorp_batch;
   enum blorp_batch_flags flags = 0;
   flags |= update_clear_depth ? 0 : BLORP_BATCH_NO_UPDATE_CLEAR_COLOR;
   blorp_batch_init(&ice->blorp, &blorp_batch, batch, flags);
   blorp_hiz_op(&blorp_batch, &surf, level, start_layer, num_layers, op);
   blorp_batch_finish(&blorp_batch);

   /* The following stalls and flushes are only documented to be required
    * for HiZ clear operations.  However, they also seem to be required for
    * resolve operations.
    *
    * From the Broadwell PRM, volume 7, "Depth Buffer Clear":
    *
    *    "Depth buffer clear pass using any of the methods (WM_STATE,
    *     3DSTATE_WM or 3DSTATE_WM_HZ_OP) must be followed by a
    *     PIPE_CONTROL command with DEPTH_STALL bit and Depth FLUSH bits
    *     "set" before starting to render.  DepthStall and DepthFlush are
    *     not needed between consecutive depth clear passes nor is it
    *     required if the depth clear pass was done with
    *     'full_surf_clear' bit set in the 3DSTATE_WM_HZ_OP."
    *
    * TODO: Such as the spec says, this could be conditional.
    */
   iris_emit_pipe_control_flush(batch,
                                "hiz op: post flush",
                                PIPE_CONTROL_DEPTH_CACHE_FLUSH |
                                PIPE_CONTROL_DEPTH_STALL);

   iris_batch_sync_region_end(batch);
}

/**
 * Does the resource's slice have hiz enabled?
 */
bool
iris_resource_level_has_hiz(const struct iris_resource *res, uint32_t level)
{
   iris_resource_check_level_layer(res, level, 0);

   if (!isl_aux_usage_has_hiz(res->aux.usage))
      return false;

   /* Disable HiZ for LOD > 0 unless the width/height are 8x4 aligned.
    * For LOD == 0, we can grow the dimensions to make it work.
    */
   if (level > 0) {
      if (u_minify(res->base.b.width0, level) & 7)
         return false;

      if (u_minify(res->base.b.height0, level) & 3)
         return false;
   }

   return true;
}

/** \brief Assert that the level and layer are valid for the resource. */
void
iris_resource_check_level_layer(UNUSED const struct iris_resource *res,
                                UNUSED uint32_t level, UNUSED uint32_t layer)
{
   assert(level < res->surf.levels);
   assert(layer < util_num_layers(&res->base.b, level));
}

static inline uint32_t
miptree_level_range_length(const struct iris_resource *res,
                           uint32_t start_level, uint32_t num_levels)
{
   assert(start_level < res->surf.levels);

   if (num_levels == INTEL_REMAINING_LAYERS)
      num_levels = res->surf.levels;

   /* Check for overflow */
   assert(start_level + num_levels >= start_level);
   assert(start_level + num_levels <= res->surf.levels);

   return num_levels;
}

static inline uint32_t
miptree_layer_range_length(const struct iris_resource *res, uint32_t level,
                           uint32_t start_layer, uint32_t num_layers)
{
   assert(level <= res->base.b.last_level);

   const uint32_t total_num_layers = iris_get_num_logical_layers(res, level);
   assert(start_layer < total_num_layers);
   if (num_layers == INTEL_REMAINING_LAYERS)
      num_layers = total_num_layers - start_layer;
   /* Check for overflow */
   assert(start_layer + num_layers >= start_layer);
   assert(start_layer + num_layers <= total_num_layers);

   return num_layers;
}

bool
iris_has_invalid_primary(const struct iris_resource *res,
                         unsigned start_level, unsigned num_levels,
                         unsigned start_layer, unsigned num_layers)
{
   if (res->aux.usage == ISL_AUX_USAGE_NONE)
      return false;

   /* Clamp the level range to fit the resource */
   num_levels = miptree_level_range_length(res, start_level, num_levels);

   for (uint32_t l = 0; l < num_levels; l++) {
      const uint32_t level = start_level + l;
      const uint32_t level_layers =
         miptree_layer_range_length(res, level, start_layer, num_layers);
      for (unsigned a = 0; a < level_layers; a++) {
         enum isl_aux_state aux_state =
            iris_resource_get_aux_state(res, level, start_layer + a);
         if (!isl_aux_state_has_valid_primary(aux_state))
            return true;
      }
   }

   return false;
}

void
iris_resource_prepare_access(struct iris_context *ice,
                             struct iris_resource *res,
                             uint32_t start_level, uint32_t num_levels,
                             uint32_t start_layer, uint32_t num_layers,
                             enum isl_aux_usage aux_usage,
                             bool fast_clear_supported)
{
   if (res->aux.usage == ISL_AUX_USAGE_NONE)
      return;

   /* We can't do resolves on the compute engine, so awkwardly, we have to
    * do them on the render batch...
    */
   struct iris_batch *batch = &ice->batches[IRIS_BATCH_RENDER];

   const uint32_t clamped_levels =
      miptree_level_range_length(res, start_level, num_levels);
   for (uint32_t l = 0; l < clamped_levels; l++) {
      const uint32_t level = start_level + l;
      const uint32_t level_layers =
         miptree_layer_range_length(res, level, start_layer, num_layers);
      for (uint32_t a = 0; a < level_layers; a++) {
         const uint32_t layer = start_layer + a;
         const enum isl_aux_state aux_state =
            iris_resource_get_aux_state(res, level, layer);
         const enum isl_aux_op aux_op =
            isl_aux_prepare_access(aux_state, aux_usage, fast_clear_supported);

         /* Prepare the aux buffer for a conditional or unconditional access.
          * A conditional access is handled by assuming that the access will
          * not evaluate to a no-op. If the access does in fact occur, the aux
          * will be in the required state. If it does not, no data is lost
          * because the aux_op performed is lossless.
          */
         if (aux_op == ISL_AUX_OP_NONE) {
            /* Nothing to do here. */
         } else if (isl_aux_usage_has_mcs(res->aux.usage)) {
            assert(aux_op == ISL_AUX_OP_PARTIAL_RESOLVE);
            iris_mcs_partial_resolve(ice, batch, res, layer, 1);
         } else if (isl_aux_usage_has_hiz(res->aux.usage)) {
            iris_hiz_exec(ice, batch, res, level, layer, 1, aux_op, false);
         } else if (res->aux.usage == ISL_AUX_USAGE_STC_CCS) {
            unreachable("iris doesn't resolve STC_CCS resources");
         } else {
            assert(isl_aux_usage_has_ccs(res->aux.usage));
            iris_resolve_color(ice, batch, res, level, layer, aux_op);
         }

         const enum isl_aux_state new_state =
            isl_aux_state_transition_aux_op(aux_state, res->aux.usage, aux_op);
         iris_resource_set_aux_state(ice, res, level, layer, 1, new_state);
      }
   }
}

void
iris_resource_finish_write(struct iris_context *ice,
                           struct iris_resource *res, uint32_t level,
                           uint32_t start_layer, uint32_t num_layers,
                           enum isl_aux_usage aux_usage)
{
   if (res->aux.usage == ISL_AUX_USAGE_NONE)
      return;

   const uint32_t level_layers =
      miptree_layer_range_length(res, level, start_layer, num_layers);

   for (uint32_t a = 0; a < level_layers; a++) {
      const uint32_t layer = start_layer + a;
      const enum isl_aux_state aux_state =
         iris_resource_get_aux_state(res, level, layer);

      /* Transition the aux state for a conditional or unconditional write. A
       * conditional write is handled by assuming that the write applies to
       * only part of the render target. This prevents the new state from
       * losing the types of compression that might exist in the current state
       * (e.g. CLEAR). If the write evaluates to a no-op, the state will still
       * be able to communicate when resolves are necessary (but it may
       * falsely communicate this as well).
       */
      const enum isl_aux_state new_aux_state =
         isl_aux_state_transition_write(aux_state, aux_usage, false);

      iris_resource_set_aux_state(ice, res, level, layer, 1, new_aux_state);
   }
}

enum isl_aux_state
iris_resource_get_aux_state(const struct iris_resource *res,
                            uint32_t level, uint32_t layer)
{
   iris_resource_check_level_layer(res, level, layer);

   if (res->surf.usage & ISL_SURF_USAGE_DEPTH_BIT) {
      assert(isl_aux_usage_has_hiz(res->aux.usage));
   } else {
      assert(res->surf.samples == 1 ||
             res->surf.msaa_layout == ISL_MSAA_LAYOUT_ARRAY);
   }

   return res->aux.state[level][layer];
}

void
iris_resource_set_aux_state(struct iris_context *ice,
                            struct iris_resource *res, uint32_t level,
                            uint32_t start_layer, uint32_t num_layers,
                            enum isl_aux_state aux_state)
{
   num_layers = miptree_layer_range_length(res, level, start_layer, num_layers);

   if (res->surf.usage & ISL_SURF_USAGE_DEPTH_BIT) {
      assert(iris_resource_level_has_hiz(res, level) ||
             !isl_aux_state_has_valid_aux(aux_state));
   } else {
      assert(res->surf.samples == 1 ||
             res->surf.msaa_layout == ISL_MSAA_LAYOUT_ARRAY);
   }

   for (unsigned a = 0; a < num_layers; a++) {
      if (res->aux.state[level][start_layer + a] != aux_state) {
         res->aux.state[level][start_layer + a] = aux_state;
         /* XXX: Need to track which bindings to make dirty */
         ice->state.dirty |= IRIS_DIRTY_RENDER_BUFFER |
                             IRIS_DIRTY_RENDER_RESOLVES_AND_FLUSHES |
                             IRIS_DIRTY_COMPUTE_RESOLVES_AND_FLUSHES;
         ice->state.stage_dirty |= IRIS_ALL_STAGE_DIRTY_BINDINGS;
      }
   }

   if (res->mod_info && !res->mod_info->supports_clear_color) {
      assert(res->mod_info->aux_usage != ISL_AUX_USAGE_NONE);
      if (aux_state == ISL_AUX_STATE_CLEAR ||
          aux_state == ISL_AUX_STATE_COMPRESSED_CLEAR ||
          aux_state == ISL_AUX_STATE_PARTIAL_CLEAR) {
         iris_mark_dirty_dmabuf(ice, &res->base.b);
      }
   }
}

enum isl_aux_usage
iris_resource_texture_aux_usage(struct iris_context *ice,
                                const struct iris_resource *res,
                                enum isl_format view_format)
{
   struct iris_screen *screen = (void *) ice->ctx.screen;
   struct intel_device_info *devinfo = &screen->devinfo;

   switch (res->aux.usage) {
   case ISL_AUX_USAGE_HIZ:
   case ISL_AUX_USAGE_HIZ_CCS:
   case ISL_AUX_USAGE_HIZ_CCS_WT:
      assert(res->surf.format == view_format);
      return util_last_bit(res->aux.sampler_usages) - 1;

   case ISL_AUX_USAGE_MCS:
   case ISL_AUX_USAGE_MCS_CCS:
   case ISL_AUX_USAGE_STC_CCS:
   case ISL_AUX_USAGE_MC:
      return res->aux.usage;

   case ISL_AUX_USAGE_CCS_E:
   case ISL_AUX_USAGE_GFX12_CCS_E:
      /* If we don't have any unresolved color, report an aux usage of
       * ISL_AUX_USAGE_NONE.  This way, texturing won't even look at the
       * aux surface and we can save some bandwidth.
       */
      if (!iris_has_invalid_primary(res, 0, INTEL_REMAINING_LEVELS,
                                    0, INTEL_REMAINING_LAYERS))
         return ISL_AUX_USAGE_NONE;

      /* On Gfx9 color buffers may be compressed by the hardware (lossless
       * compression). There are, however, format restrictions and care needs
       * to be taken that the sampler engine is capable for re-interpreting a
       * buffer with format different the buffer was originally written with.
       *
       * For example, SRGB formats are not compressible and the sampler engine
       * isn't capable of treating RGBA_UNORM as SRGB_ALPHA. In such a case
       * the underlying color buffer needs to be resolved so that the sampling
       * surface can be sampled as non-compressed (i.e., without the auxiliary
       * MCS buffer being set).
       */
      if (isl_formats_are_ccs_e_compatible(devinfo, res->surf.format,
                                           view_format))
         return res->aux.usage;
      break;

   default:
      break;
   }

   return ISL_AUX_USAGE_NONE;
}

enum isl_aux_usage
iris_image_view_aux_usage(struct iris_context *ice,
                          const struct pipe_image_view *pview,
                          const struct shader_info *info)
{
   if (!info)
      return ISL_AUX_USAGE_NONE;

   struct iris_resource *res = (void *) pview->resource;

   enum isl_format view_format = iris_image_view_get_format(ice, pview);
   enum isl_aux_usage aux_usage =
      iris_resource_texture_aux_usage(ice, res, view_format);

   bool uses_atomic_load_store =
      ice->shaders.uncompiled[info->stage]->uses_atomic_load_store;

   if (aux_usage == ISL_AUX_USAGE_GFX12_CCS_E && !uses_atomic_load_store)
      return ISL_AUX_USAGE_GFX12_CCS_E;

   return ISL_AUX_USAGE_NONE;
}

bool
iris_can_sample_mcs_with_clear(const struct intel_device_info *devinfo,
                               const struct iris_resource *res)
{
   assert(isl_aux_usage_has_mcs(res->aux.usage));

   /* On TGL, the sampler has an issue with some 8 and 16bpp MSAA fast clears.
    * See HSD 1707282275, wa_14013111325. Due to the use of
    * format-reinterpretation, a simplified workaround is implemented.
    */
   if (devinfo->ver >= 12 &&
       isl_format_get_layout(res->surf.format)->bpb <= 16) {
      return false;
   }

   return true;
}

static bool
isl_formats_are_fast_clear_compatible(enum isl_format a, enum isl_format b)
{
   /* On gfx8 and earlier, the hardware was only capable of handling 0/1 clear
    * values so sRGB curve application was a no-op for all fast-clearable
    * formats.
    *
    * On gfx9+, the hardware supports arbitrary clear values.  For sRGB clear
    * values, the hardware interprets the floats, not as what would be
    * returned from the sampler (or written by the shader), but as being
    * between format conversion and sRGB curve application.  This means that
    * we can switch between sRGB and UNORM without having to whack the clear
    * color.
    */
   return isl_format_srgb_to_linear(a) == isl_format_srgb_to_linear(b);
}

void
iris_resource_prepare_texture(struct iris_context *ice,
                              struct iris_resource *res,
                              enum isl_format view_format,
                              uint32_t start_level, uint32_t num_levels,
                              uint32_t start_layer, uint32_t num_layers)
{
   const struct iris_screen *screen = (void *) ice->ctx.screen;
   const struct intel_device_info *devinfo = &screen->devinfo;

   enum isl_aux_usage aux_usage =
      iris_resource_texture_aux_usage(ice, res, view_format);

   bool clear_supported = isl_aux_usage_has_fast_clears(aux_usage);

   /* Clear color is specified as ints or floats and the conversion is done by
    * the sampler.  If we have a texture view, we would have to perform the
    * clear color conversion manually.  Just disable clear color.
    */
   if (!isl_formats_are_fast_clear_compatible(res->surf.format, view_format))
      clear_supported = false;

   if (isl_aux_usage_has_mcs(aux_usage) &&
       !iris_can_sample_mcs_with_clear(devinfo, res)) {
      clear_supported = false;
   }

   iris_resource_prepare_access(ice, res, start_level, num_levels,
                                start_layer, num_layers,
                                aux_usage, clear_supported);
}

/* Whether or not rendering a color value with either format results in the
 * same pixel. This can return false negatives.
 */
bool
iris_render_formats_color_compatible(enum isl_format a, enum isl_format b,
                                     union isl_color_value color,
                                     bool clear_color_unknown)
{
   if (a == b)
      return true;

   /* A difference in color space doesn't matter for 0/1 values. */
   if (!clear_color_unknown &&
       isl_format_srgb_to_linear(a) == isl_format_srgb_to_linear(b) &&
       isl_color_value_is_zero_one(color, a)) {
      return true;
   }

   return false;
}

enum isl_aux_usage
iris_resource_render_aux_usage(struct iris_context *ice,
                               struct iris_resource *res, uint32_t level,
                               enum isl_format render_format,
                               bool draw_aux_disabled)
{
   struct iris_screen *screen = (void *) ice->ctx.screen;
   struct intel_device_info *devinfo = &screen->devinfo;

   if (draw_aux_disabled)
      return ISL_AUX_USAGE_NONE;

   switch (res->aux.usage) {
   case ISL_AUX_USAGE_HIZ:
   case ISL_AUX_USAGE_HIZ_CCS:
   case ISL_AUX_USAGE_HIZ_CCS_WT:
      assert(render_format == res->surf.format);
      return iris_resource_level_has_hiz(res, level) ?
             res->aux.usage : ISL_AUX_USAGE_NONE;

   case ISL_AUX_USAGE_STC_CCS:
      assert(render_format == res->surf.format);
      return res->aux.usage;

   case ISL_AUX_USAGE_MCS:
   case ISL_AUX_USAGE_MCS_CCS:
      return res->aux.usage;

   case ISL_AUX_USAGE_CCS_D:
   case ISL_AUX_USAGE_CCS_E:
   case ISL_AUX_USAGE_GFX12_CCS_E:
      /* Disable CCS for some cases of texture-view rendering. On gfx12, HW
       * may convert some subregions of shader output to fast-cleared blocks
       * if CCS is enabled and the shader output matches the clear color.
       * Existing fast-cleared blocks are correctly interpreted by the clear
       * color and the resource format (see can_fast_clear_color). To avoid
       * gaining new fast-cleared blocks that can't be interpreted by the
       * resource format (and to avoid misinterpreting existing ones), shut
       * off CCS when the interpretation of the clear color differs between
       * the render_format and the resource format.
       */
      if (!iris_render_formats_color_compatible(render_format,
                                                res->surf.format,
                                                res->aux.clear_color,
                                                res->aux.clear_color_unknown)) {
         return ISL_AUX_USAGE_NONE;
      }

      if (res->aux.usage == ISL_AUX_USAGE_CCS_D)
         return ISL_AUX_USAGE_CCS_D;

      if (isl_formats_are_ccs_e_compatible(devinfo, res->surf.format,
                                           render_format)) {
         return res->aux.usage;
      }
      FALLTHROUGH;

   default:
      return ISL_AUX_USAGE_NONE;
   }
}

void
iris_resource_prepare_render(struct iris_context *ice,
                             struct iris_resource *res, uint32_t level,
                             uint32_t start_layer, uint32_t layer_count,
                             enum isl_aux_usage aux_usage)
{
   iris_resource_prepare_access(ice, res, level, 1, start_layer,
                                layer_count, aux_usage,
                                isl_aux_usage_has_fast_clears(aux_usage));
}

void
iris_resource_finish_render(struct iris_context *ice,
                            struct iris_resource *res, uint32_t level,
                            uint32_t start_layer, uint32_t layer_count,
                            enum isl_aux_usage aux_usage)
{
   iris_resource_finish_write(ice, res, level, start_layer, layer_count,
                              aux_usage);
}
