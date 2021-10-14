/*
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "pipe/p_state.h"
#include "util/format/u_format.h"
#include "util/u_draw.h"
#include "util/u_helpers.h"
#include "util/u_memory.h"
#include "util/u_prim.h"
#include "util/u_string.h"

#include "freedreno_blitter.h"
#include "freedreno_context.h"
#include "freedreno_draw.h"
#include "freedreno_fence.h"
#include "freedreno_query_acc.h"
#include "freedreno_query_hw.h"
#include "freedreno_resource.h"
#include "freedreno_state.h"
#include "freedreno_util.h"

static void
resource_read(struct fd_batch *batch, struct pipe_resource *prsc) assert_dt
{
   if (!prsc)
      return;
   fd_batch_resource_read(batch, fd_resource(prsc));
}

static void
resource_written(struct fd_batch *batch, struct pipe_resource *prsc) assert_dt
{
   if (!prsc)
      return;
   fd_batch_resource_write(batch, fd_resource(prsc));
}

static void
batch_draw_tracking_for_dirty_bits(struct fd_batch *batch) assert_dt
{
   struct fd_context *ctx = batch->ctx;
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;
   unsigned buffers = 0, restore_buffers = 0;

   if (ctx->dirty & (FD_DIRTY_FRAMEBUFFER | FD_DIRTY_ZSA)) {
      if (fd_depth_enabled(ctx)) {
         if (fd_resource(pfb->zsbuf->texture)->valid) {
            restore_buffers |= FD_BUFFER_DEPTH;
         } else {
            batch->invalidated |= FD_BUFFER_DEPTH;
         }
         batch->gmem_reason |= FD_GMEM_DEPTH_ENABLED;
         if (fd_depth_write_enabled(ctx)) {
            buffers |= FD_BUFFER_DEPTH;
            resource_written(batch, pfb->zsbuf->texture);
         } else {
            resource_read(batch, pfb->zsbuf->texture);
         }
      }

      if (fd_stencil_enabled(ctx)) {
         if (fd_resource(pfb->zsbuf->texture)->valid) {
            restore_buffers |= FD_BUFFER_STENCIL;
         } else {
            batch->invalidated |= FD_BUFFER_STENCIL;
         }
         batch->gmem_reason |= FD_GMEM_STENCIL_ENABLED;
         buffers |= FD_BUFFER_STENCIL;
         resource_written(batch, pfb->zsbuf->texture);
      }
   }

   if (ctx->dirty & FD_DIRTY_FRAMEBUFFER) {
      for (unsigned i = 0; i < pfb->nr_cbufs; i++) {
         struct pipe_resource *surf;

         if (!pfb->cbufs[i])
            continue;

         surf = pfb->cbufs[i]->texture;

         if (fd_resource(surf)->valid) {
            restore_buffers |= PIPE_CLEAR_COLOR0 << i;
         } else {
            batch->invalidated |= PIPE_CLEAR_COLOR0 << i;
         }

         buffers |= PIPE_CLEAR_COLOR0 << i;

         if (ctx->dirty & FD_DIRTY_FRAMEBUFFER)
            resource_written(batch, pfb->cbufs[i]->texture);
      }
   }

   if (ctx->dirty & FD_DIRTY_BLEND) {
      if (ctx->blend->logicop_enable)
         batch->gmem_reason |= FD_GMEM_LOGICOP_ENABLED;
      for (unsigned i = 0; i < pfb->nr_cbufs; i++) {
         if (ctx->blend->rt[i].blend_enable)
            batch->gmem_reason |= FD_GMEM_BLEND_ENABLED;
      }
   }

   /* Mark SSBOs */
   if (ctx->dirty_shader[PIPE_SHADER_FRAGMENT] & FD_DIRTY_SHADER_SSBO) {
      const struct fd_shaderbuf_stateobj *so =
         &ctx->shaderbuf[PIPE_SHADER_FRAGMENT];

      u_foreach_bit (i, so->enabled_mask & so->writable_mask)
         resource_written(batch, so->sb[i].buffer);

      u_foreach_bit (i, so->enabled_mask & ~so->writable_mask)
         resource_read(batch, so->sb[i].buffer);
   }

   if (ctx->dirty_shader[PIPE_SHADER_FRAGMENT] & FD_DIRTY_SHADER_IMAGE) {
      u_foreach_bit (i, ctx->shaderimg[PIPE_SHADER_FRAGMENT].enabled_mask) {
         struct pipe_image_view *img =
            &ctx->shaderimg[PIPE_SHADER_FRAGMENT].si[i];
         if (img->access & PIPE_IMAGE_ACCESS_WRITE)
            resource_written(batch, img->resource);
         else
            resource_read(batch, img->resource);
      }
   }

   u_foreach_bit (s, ctx->bound_shader_stages) {
      /* Mark constbuf as being read: */
      if (ctx->dirty_shader[s] & FD_DIRTY_SHADER_CONST) {
         u_foreach_bit (i, ctx->constbuf[s].enabled_mask)
            resource_read(batch, ctx->constbuf[s].cb[i].buffer);
      }

      /* Mark textures as being read */
      if (ctx->dirty_shader[s] & FD_DIRTY_SHADER_TEX) {
         u_foreach_bit (i, ctx->tex[s].valid_textures)
            resource_read(batch, ctx->tex[s].textures[i]->texture);
      }
   }

   /* Mark VBOs as being read */
   if (ctx->dirty & FD_DIRTY_VTXBUF) {
      u_foreach_bit (i, ctx->vtx.vertexbuf.enabled_mask) {
         assert(!ctx->vtx.vertexbuf.vb[i].is_user_buffer);
         resource_read(batch, ctx->vtx.vertexbuf.vb[i].buffer.resource);
      }
   }

   /* Mark streamout buffers as being written.. */
   if (ctx->dirty & FD_DIRTY_STREAMOUT) {
      for (unsigned i = 0; i < ctx->streamout.num_targets; i++)
         if (ctx->streamout.targets[i])
            resource_written(batch, ctx->streamout.targets[i]->buffer);
   }

   /* any buffers that haven't been cleared yet, we need to restore: */
   batch->restore |= restore_buffers & (FD_BUFFER_ALL & ~batch->invalidated);
   /* and any buffers used, need to be resolved: */
   batch->resolve |= buffers;
}

static void
batch_draw_tracking(struct fd_batch *batch, const struct pipe_draw_info *info,
                    const struct pipe_draw_indirect_info *indirect) assert_dt
{
   struct fd_context *ctx = batch->ctx;

   /* NOTE: needs to be before resource_written(batch->query_buf), otherwise
    * query_buf may not be created yet.
    */
   fd_batch_update_queries(batch);

   /*
    * Figure out the buffers/features we need:
    */

   fd_screen_lock(ctx->screen);

   if (ctx->dirty & FD_DIRTY_RESOURCE)
      batch_draw_tracking_for_dirty_bits(batch);

   /* Mark index buffer as being read */
   if (info->index_size)
      resource_read(batch, info->index.resource);

   /* Mark indirect draw buffer as being read */
   if (indirect) {
      if (indirect->buffer)
         resource_read(batch, indirect->buffer);
      if (indirect->count_from_stream_output)
         resource_read(
            batch, fd_stream_output_target(indirect->count_from_stream_output)
                      ->offset_buf);
   }

   resource_written(batch, batch->query_buf);

   list_for_each_entry (struct fd_acc_query, aq, &ctx->acc_active_queries, node)
      resource_written(batch, aq->prsc);

   fd_screen_unlock(ctx->screen);
}

static void
update_draw_stats(struct fd_context *ctx, const struct pipe_draw_info *info,
                  const struct pipe_draw_start_count_bias *draws,
                  unsigned num_draws) assert_dt
{
   ctx->stats.draw_calls++;

   if (ctx->screen->gen < 6) {
      /* Counting prims in sw doesn't work for GS and tesselation. For older
       * gens we don't have those stages and don't have the hw counters enabled,
       * so keep the count accurate for non-patch geometry.
       */
      unsigned prims = 0;
      if ((info->mode != PIPE_PRIM_PATCHES) && (info->mode != PIPE_PRIM_MAX)) {
         for (unsigned i = 0; i < num_draws; i++) {
            prims += u_reduced_prims_for_vertices(info->mode, draws[i].count);
         }
      }

      ctx->stats.prims_generated += prims;

      if (ctx->streamout.num_targets > 0) {
         /* Clip the prims we're writing to the size of the SO buffers. */
         enum pipe_prim_type tf_prim = u_decomposed_prim(info->mode);
         unsigned verts_written = u_vertices_for_prims(tf_prim, prims);
         unsigned remaining_vert_space =
            ctx->streamout.max_tf_vtx - ctx->streamout.verts_written;
         if (verts_written > remaining_vert_space) {
            verts_written = remaining_vert_space;
            u_trim_pipe_prim(tf_prim, &remaining_vert_space);
         }
         ctx->streamout.verts_written += verts_written;

         ctx->stats.prims_emitted +=
            u_reduced_prims_for_vertices(tf_prim, verts_written);
      }
   }
}

static void
fd_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info,
		unsigned drawid_offset,
            const struct pipe_draw_indirect_info *indirect,
            const struct pipe_draw_start_count_bias *draws, unsigned num_draws) in_dt
{
   struct fd_context *ctx = fd_context(pctx);

   /* for debugging problems with indirect draw, it is convenient
    * to be able to emulate it, to determine if game is feeding us
    * bogus data:
    */
   if (indirect && indirect->buffer && FD_DBG(NOINDR)) {
      /* num_draws is only applicable for direct draws: */
      assert(num_draws == 1);
      util_draw_indirect(pctx, info, indirect);
      return;
   }

   /* TODO: push down the region versions into the tiles */
   if (!fd_render_condition_check(pctx))
      return;

   /* Upload a user index buffer. */
   struct pipe_resource *indexbuf = NULL;
   unsigned index_offset = 0;
   struct pipe_draw_info new_info;
   if (info->index_size) {
      if (info->has_user_indices) {
         if (num_draws > 1) {
            util_draw_multi(pctx, info, drawid_offset, indirect, draws, num_draws);
            return;
         }
         if (!util_upload_index_buffer(pctx, info, &draws[0], &indexbuf,
                                       &index_offset, 4))
            return;
         new_info = *info;
         new_info.index.resource = indexbuf;
         new_info.has_user_indices = false;
         info = &new_info;
      } else {
         indexbuf = info->index.resource;
      }
   }

   if ((ctx->streamout.num_targets > 0) && (num_draws > 1)) {
      util_draw_multi(pctx, info, drawid_offset, indirect, draws, num_draws);
      return;
   }

   struct fd_batch *batch = fd_context_batch(ctx);

   batch_draw_tracking(batch, info, indirect);

   while (unlikely(!fd_batch_lock_submit(batch))) {
      /* The current batch was flushed in batch_draw_tracking()
       * so start anew.  We know this won't happen a second time
       * since we are dealing with a fresh batch:
       */
      fd_batch_reference(&batch, NULL);
      batch = fd_context_batch(ctx);
      batch_draw_tracking(batch, info, indirect);
      assert(ctx->batch == batch);
   }

   batch->num_draws++;

   /* Marking the batch as needing flush must come after the batch
    * dependency tracking (resource_read()/resource_write()), as that
    * can trigger a flush
    */
   fd_batch_needs_flush(batch);

   struct pipe_framebuffer_state *pfb = &batch->framebuffer;
   DBG("%p: %ux%u num_draws=%u (%s/%s)", batch, pfb->width, pfb->height,
       batch->num_draws,
       util_format_short_name(pipe_surface_format(pfb->cbufs[0])),
       util_format_short_name(pipe_surface_format(pfb->zsbuf)));

   batch->cost += ctx->draw_cost;

   for (unsigned i = 0; i < num_draws; i++) {
      ctx->draw_vbo(ctx, info, drawid_offset, indirect, &draws[i], index_offset);

      batch->num_vertices += draws[i].count * info->instance_count;
   }

   if (unlikely(ctx->stats_users > 0))
      update_draw_stats(ctx, info, draws, num_draws);

   for (unsigned i = 0; i < ctx->streamout.num_targets; i++) {
      assert(num_draws == 1);
      ctx->streamout.offsets[i] += draws[0].count;
   }

   if (FD_DBG(DDRAW))
      fd_context_all_dirty(ctx);

   debug_assert(!batch->flushed);

   fd_batch_unlock_submit(batch);
   fd_batch_check_size(batch);
   fd_batch_reference(&batch, NULL);

   if (info == &new_info)
      pipe_resource_reference(&indexbuf, NULL);
}

static void
batch_clear_tracking(struct fd_batch *batch, unsigned buffers) assert_dt
{
   struct fd_context *ctx = batch->ctx;
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;
   unsigned cleared_buffers;

   /* pctx->clear() is only for full-surface clears, so scissor is
    * equivalent to having GL_SCISSOR_TEST disabled:
    */
   batch->max_scissor.minx = 0;
   batch->max_scissor.miny = 0;
   batch->max_scissor.maxx = pfb->width;
   batch->max_scissor.maxy = pfb->height;

   /* for bookkeeping about which buffers have been cleared (and thus
    * can fully or partially skip mem2gmem) we need to ignore buffers
    * that have already had a draw, in case apps do silly things like
    * clear after draw (ie. if you only clear the color buffer, but
    * something like alpha-test causes side effects from the draw in
    * the depth buffer, etc)
    */
   cleared_buffers = buffers & (FD_BUFFER_ALL & ~batch->restore);
   batch->cleared |= buffers;
   batch->invalidated |= cleared_buffers;

   batch->resolve |= buffers;

   fd_screen_lock(ctx->screen);

   if (buffers & PIPE_CLEAR_COLOR)
      for (unsigned i = 0; i < pfb->nr_cbufs; i++)
         if (buffers & (PIPE_CLEAR_COLOR0 << i))
            resource_written(batch, pfb->cbufs[i]->texture);

   if (buffers & (PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL)) {
      resource_written(batch, pfb->zsbuf->texture);
      batch->gmem_reason |= FD_GMEM_CLEARS_DEPTH_STENCIL;
   }

   resource_written(batch, batch->query_buf);

   list_for_each_entry (struct fd_acc_query, aq, &ctx->acc_active_queries, node)
      resource_written(batch, aq->prsc);

   fd_screen_unlock(ctx->screen);
}

static void
fd_clear(struct pipe_context *pctx, unsigned buffers,
         const struct pipe_scissor_state *scissor_state,
         const union pipe_color_union *color, double depth,
         unsigned stencil) in_dt
{
   struct fd_context *ctx = fd_context(pctx);

   /* TODO: push down the region versions into the tiles */
   if (!fd_render_condition_check(pctx))
      return;

   struct fd_batch *batch = fd_context_batch(ctx);

   batch_clear_tracking(batch, buffers);

   while (unlikely(!fd_batch_lock_submit(batch))) {
      /* The current batch was flushed in batch_clear_tracking()
       * so start anew.  We know this won't happen a second time
       * since we are dealing with a fresh batch:
       */
      fd_batch_reference(&batch, NULL);
      batch = fd_context_batch(ctx);
      batch_clear_tracking(batch, buffers);
      assert(ctx->batch == batch);
   }

   /* Marking the batch as needing flush must come after the batch
    * dependency tracking (resource_read()/resource_write()), as that
    * can trigger a flush
    */
   fd_batch_needs_flush(batch);

   struct pipe_framebuffer_state *pfb = &batch->framebuffer;
   DBG("%p: %x %ux%u depth=%f, stencil=%u (%s/%s)", batch, buffers, pfb->width,
       pfb->height, depth, stencil,
       util_format_short_name(pipe_surface_format(pfb->cbufs[0])),
       util_format_short_name(pipe_surface_format(pfb->zsbuf)));

   /* if per-gen backend doesn't implement ctx->clear() generic
    * blitter clear:
    */
   bool fallback = true;

   if (ctx->clear) {
      fd_batch_update_queries(batch);

      if (ctx->clear(ctx, buffers, color, depth, stencil)) {
         if (FD_DBG(DCLEAR))
            fd_context_all_dirty(ctx);

         fallback = false;
      }
   }

   debug_assert(!batch->flushed);

   fd_batch_unlock_submit(batch);
   fd_batch_check_size(batch);

   if (fallback) {
      fd_blitter_clear(pctx, buffers, color, depth, stencil);
   }

   fd_batch_reference(&batch, NULL);
}

static void
fd_clear_render_target(struct pipe_context *pctx, struct pipe_surface *ps,
                       const union pipe_color_union *color, unsigned x,
                       unsigned y, unsigned w, unsigned h,
                       bool render_condition_enabled)
{
   DBG("TODO: x=%u, y=%u, w=%u, h=%u", x, y, w, h);
}

static void
fd_clear_depth_stencil(struct pipe_context *pctx, struct pipe_surface *ps,
                       unsigned buffers, double depth, unsigned stencil,
                       unsigned x, unsigned y, unsigned w, unsigned h,
                       bool render_condition_enabled)
{
   DBG("TODO: buffers=%u, depth=%f, stencil=%u, x=%u, y=%u, w=%u, h=%u",
       buffers, depth, stencil, x, y, w, h);
}

static void
fd_launch_grid(struct pipe_context *pctx,
               const struct pipe_grid_info *info) in_dt
{
   struct fd_context *ctx = fd_context(pctx);
   const struct fd_shaderbuf_stateobj *so =
      &ctx->shaderbuf[PIPE_SHADER_COMPUTE];
   struct fd_batch *batch, *save_batch = NULL;

   batch = fd_bc_alloc_batch(ctx, true);
   fd_batch_reference(&save_batch, ctx->batch);
   fd_batch_reference(&ctx->batch, batch);
   fd_context_all_dirty(ctx);

   fd_screen_lock(ctx->screen);

   /* Mark SSBOs */
   u_foreach_bit (i, so->enabled_mask & so->writable_mask)
      resource_written(batch, so->sb[i].buffer);

   u_foreach_bit (i, so->enabled_mask & ~so->writable_mask)
      resource_read(batch, so->sb[i].buffer);

   u_foreach_bit (i, ctx->shaderimg[PIPE_SHADER_COMPUTE].enabled_mask) {
      struct pipe_image_view *img = &ctx->shaderimg[PIPE_SHADER_COMPUTE].si[i];
      if (img->access & PIPE_IMAGE_ACCESS_WRITE)
         resource_written(batch, img->resource);
      else
         resource_read(batch, img->resource);
   }

   /* UBO's are read */
   u_foreach_bit (i, ctx->constbuf[PIPE_SHADER_COMPUTE].enabled_mask)
      resource_read(batch, ctx->constbuf[PIPE_SHADER_COMPUTE].cb[i].buffer);

   /* Mark textures as being read */
   u_foreach_bit (i, ctx->tex[PIPE_SHADER_COMPUTE].valid_textures)
      resource_read(batch, ctx->tex[PIPE_SHADER_COMPUTE].textures[i]->texture);

   /* For global buffers, we don't really know if read or written, so assume
    * the worst:
    */
   u_foreach_bit (i, ctx->global_bindings.enabled_mask)
      resource_written(batch, ctx->global_bindings.buf[i]);

   if (info->indirect)
      resource_read(batch, info->indirect);

   fd_screen_unlock(ctx->screen);

   DBG("%p: work_dim=%u, block=%ux%ux%u, grid=%ux%ux%u",
       batch, info->work_dim,
       info->block[0], info->block[1], info->block[2],
       info->grid[0], info->grid[1], info->grid[2]);

   fd_batch_needs_flush(batch);
   ctx->launch_grid(ctx, info);

   fd_batch_flush(batch);

   fd_batch_reference(&ctx->batch, save_batch);
   fd_context_all_dirty(ctx);
   fd_batch_reference(&save_batch, NULL);
   fd_batch_reference(&batch, NULL);
}

void
fd_draw_init(struct pipe_context *pctx)
{
   pctx->draw_vbo = fd_draw_vbo;
   pctx->clear = fd_clear;
   pctx->clear_render_target = fd_clear_render_target;
   pctx->clear_depth_stencil = fd_clear_depth_stencil;

   if (has_compute(fd_screen(pctx->screen))) {
      pctx->launch_grid = fd_launch_grid;
   }
}
