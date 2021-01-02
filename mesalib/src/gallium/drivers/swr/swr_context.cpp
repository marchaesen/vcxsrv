/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 ***************************************************************************/

#include "swr_context.h"
#include "swr_memory.h"
#include "swr_screen.h"
#include "swr_resource.h"
#include "swr_scratch.h"
#include "swr_query.h"
#include "swr_fence.h"

#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/format/u_format.h"
#include "util/u_atomic.h"
#include "util/u_upload_mgr.h"
#include "util/u_transfer.h"
#include "util/u_surface.h"

#include "api.h"
#include "backend.h"
#include "knobs.h"

static struct pipe_surface *
swr_create_surface(struct pipe_context *pipe,
                   struct pipe_resource *pt,
                   const struct pipe_surface *surf_tmpl)
{
   struct pipe_surface *ps;

   ps = CALLOC_STRUCT(pipe_surface);
   if (ps) {
      pipe_reference_init(&ps->reference, 1);
      pipe_resource_reference(&ps->texture, pt);
      ps->context = pipe;
      ps->format = surf_tmpl->format;
      if (pt->target != PIPE_BUFFER) {
         assert(surf_tmpl->u.tex.level <= pt->last_level);
         ps->width = u_minify(pt->width0, surf_tmpl->u.tex.level);
         ps->height = u_minify(pt->height0, surf_tmpl->u.tex.level);
         ps->u.tex.level = surf_tmpl->u.tex.level;
         ps->u.tex.first_layer = surf_tmpl->u.tex.first_layer;
         ps->u.tex.last_layer = surf_tmpl->u.tex.last_layer;
      } else {
         /* setting width as number of elements should get us correct
          * renderbuffer width */
         ps->width = surf_tmpl->u.buf.last_element
            - surf_tmpl->u.buf.first_element + 1;
         ps->height = pt->height0;
         ps->u.buf.first_element = surf_tmpl->u.buf.first_element;
         ps->u.buf.last_element = surf_tmpl->u.buf.last_element;
         assert(ps->u.buf.first_element <= ps->u.buf.last_element);
         assert(ps->u.buf.last_element < ps->width);
      }
   }
   return ps;
}

static void
swr_surface_destroy(struct pipe_context *pipe, struct pipe_surface *surf)
{
   assert(surf->texture);
   struct pipe_resource *resource = surf->texture;

   /* If the resource has been drawn to, store tiles. */
   swr_store_dirty_resource(pipe, resource, SWR_TILE_RESOLVED);

   pipe_resource_reference(&resource, NULL);
   FREE(surf);
}


static void *
swr_transfer_map(struct pipe_context *pipe,
                 struct pipe_resource *resource,
                 unsigned level,
                 unsigned usage,
                 const struct pipe_box *box,
                 struct pipe_transfer **transfer)
{
   struct swr_screen *screen = swr_screen(pipe->screen);
   struct swr_resource *spr = swr_resource(resource);
   struct pipe_transfer *pt;
   enum pipe_format format = resource->format;

   assert(resource);
   assert(level <= resource->last_level);

   /* If mapping an attached rendertarget, store tiles to surface and set
    * postStoreTileState to SWR_TILE_INVALID so tiles get reloaded on next use
    * and nothing needs to be done at unmap. */
   swr_store_dirty_resource(pipe, resource, SWR_TILE_INVALID);

   if (!(usage & PIPE_MAP_UNSYNCHRONIZED)) {
      /* If resource is in use, finish fence before mapping.
       * Unless requested not to block, then if not done return NULL map */
      if (usage & PIPE_MAP_DONTBLOCK) {
         if (swr_is_fence_pending(screen->flush_fence))
            return NULL;
      } else {
         if (spr->status) {
            /* But, if there's no fence pending, submit one.
             * XXX: Remove once draw timestamps are finished. */
            if (!swr_is_fence_pending(screen->flush_fence))
               swr_fence_submit(swr_context(pipe), screen->flush_fence);

            swr_fence_finish(pipe->screen, NULL, screen->flush_fence, 0);
            swr_resource_unused(resource);
         }
      }
   }

   pt = CALLOC_STRUCT(pipe_transfer);
   if (!pt)
      return NULL;
   pipe_resource_reference(&pt->resource, resource);
   pt->usage = (pipe_map_flags)usage;
   pt->level = level;
   pt->box = *box;
   pt->stride = spr->swr.pitch;
   pt->layer_stride = spr->swr.qpitch * spr->swr.pitch;

   /* if we're mapping the depth/stencil, copy in stencil for the section
    * being read in
    */
   if (usage & PIPE_MAP_READ && spr->has_depth && spr->has_stencil) {
      size_t zbase, sbase;
      for (int z = box->z; z < box->z + box->depth; z++) {
         zbase = (z * spr->swr.qpitch + box->y) * spr->swr.pitch +
            spr->mip_offsets[level];
         sbase = (z * spr->secondary.qpitch + box->y) * spr->secondary.pitch +
            spr->secondary_mip_offsets[level];
         for (int y = box->y; y < box->y + box->height; y++) {
            if (spr->base.format == PIPE_FORMAT_Z24_UNORM_S8_UINT) {
               for (int x = box->x; x < box->x + box->width; x++)
                  ((uint8_t*)(spr->swr.xpBaseAddress))[zbase + 4 * x + 3] =
                     ((uint8_t*)(spr->secondary.xpBaseAddress))[sbase + x];
            } else if (spr->base.format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
               for (int x = box->x; x < box->x + box->width; x++)
                  ((uint8_t*)(spr->swr.xpBaseAddress))[zbase + 8 * x + 4] =
                     ((uint8_t*)(spr->secondary.xpBaseAddress))[sbase + x];
            }
            zbase += spr->swr.pitch;
            sbase += spr->secondary.pitch;
         }
      }
   }

   unsigned offset = box->z * pt->layer_stride +
      util_format_get_nblocksy(format, box->y) * pt->stride +
      util_format_get_stride(format, box->x);

   *transfer = pt;

   return (void*)(spr->swr.xpBaseAddress + offset + spr->mip_offsets[level]);
}

static void
swr_transfer_flush_region(struct pipe_context *pipe,
                          struct pipe_transfer *transfer,
                          const struct pipe_box *flush_box)
{
   assert(transfer->resource);
   assert(transfer->usage & PIPE_MAP_WRITE);

   struct swr_resource *spr = swr_resource(transfer->resource);
   if (!spr->has_depth || !spr->has_stencil)
      return;

   size_t zbase, sbase;
   struct pipe_box box = *flush_box;
   box.x += transfer->box.x;
   box.y += transfer->box.y;
   box.z += transfer->box.z;
   for (int z = box.z; z < box.z + box.depth; z++) {
      zbase = (z * spr->swr.qpitch + box.y) * spr->swr.pitch +
         spr->mip_offsets[transfer->level];
      sbase = (z * spr->secondary.qpitch + box.y) * spr->secondary.pitch +
         spr->secondary_mip_offsets[transfer->level];
      for (int y = box.y; y < box.y + box.height; y++) {
         if (spr->base.format == PIPE_FORMAT_Z24_UNORM_S8_UINT) {
            for (int x = box.x; x < box.x + box.width; x++)
               ((uint8_t*)(spr->secondary.xpBaseAddress))[sbase + x] =
                  ((uint8_t*)(spr->swr.xpBaseAddress))[zbase + 4 * x + 3];
         } else if (spr->base.format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
            for (int x = box.x; x < box.x + box.width; x++)
               ((uint8_t*)(spr->secondary.xpBaseAddress))[sbase + x] =
                  ((uint8_t*)(spr->swr.xpBaseAddress))[zbase + 8 * x + 4];
         }
         zbase += spr->swr.pitch;
         sbase += spr->secondary.pitch;
      }
   }
}

static void
swr_transfer_unmap(struct pipe_context *pipe, struct pipe_transfer *transfer)
{
   assert(transfer->resource);

   struct swr_resource *spr = swr_resource(transfer->resource);
   /* if we're mapping the depth/stencil, copy in stencil for the section
    * being written out
    */
   if (transfer->usage & PIPE_MAP_WRITE &&
       !(transfer->usage & PIPE_MAP_FLUSH_EXPLICIT) &&
       spr->has_depth && spr->has_stencil) {
      struct pipe_box box;
      u_box_3d(0, 0, 0, transfer->box.width, transfer->box.height,
               transfer->box.depth, &box);
      swr_transfer_flush_region(pipe, transfer, &box);
   }

   pipe_resource_reference(&transfer->resource, NULL);
   FREE(transfer);
}


static void
swr_resource_copy(struct pipe_context *pipe,
                  struct pipe_resource *dst,
                  unsigned dst_level,
                  unsigned dstx,
                  unsigned dsty,
                  unsigned dstz,
                  struct pipe_resource *src,
                  unsigned src_level,
                  const struct pipe_box *src_box)
{
   struct swr_screen *screen = swr_screen(pipe->screen);

   /* If either the src or dst is a renderTarget, store tiles before copy */
   swr_store_dirty_resource(pipe, src, SWR_TILE_RESOLVED);
   swr_store_dirty_resource(pipe, dst, SWR_TILE_RESOLVED);

   swr_fence_finish(pipe->screen, NULL, screen->flush_fence, 0);
   swr_resource_unused(src);
   swr_resource_unused(dst);

   if ((dst->target == PIPE_BUFFER && src->target == PIPE_BUFFER)
       || (dst->target != PIPE_BUFFER && src->target != PIPE_BUFFER)) {
      util_resource_copy_region(
         pipe, dst, dst_level, dstx, dsty, dstz, src, src_level, src_box);
      return;
   }

   debug_printf("unhandled swr_resource_copy\n");
}


static void
swr_blit(struct pipe_context *pipe, const struct pipe_blit_info *blit_info)
{
   struct swr_context *ctx = swr_context(pipe);
   /* Make a copy of the const blit_info, so we can modify it */
   struct pipe_blit_info info = *blit_info;

   if (info.render_condition_enable && !swr_check_render_cond(pipe))
      return;

   if (info.src.resource->nr_samples > 1 && info.dst.resource->nr_samples <= 1
       && !util_format_is_depth_or_stencil(info.src.resource->format)
       && !util_format_is_pure_integer(info.src.resource->format)) {
      debug_printf("swr_blit: color resolve : %d -> %d\n",
            info.src.resource->nr_samples, info.dst.resource->nr_samples);

      /* Resolve is done as part of the surface store. */
      swr_store_dirty_resource(pipe, info.src.resource, SWR_TILE_RESOLVED);

      struct pipe_resource *src_resource = info.src.resource;
      struct pipe_resource *resolve_target =
         swr_resource(src_resource)->resolve_target;

      /* The resolve target becomes the new source for the blit. */
      info.src.resource = resolve_target;
   }

   if (util_try_blit_via_copy_region(pipe, &info)) {
      return; /* done */
   }

   if (info.mask & PIPE_MASK_S) {
      debug_printf("swr: cannot blit stencil, skipping\n");
      info.mask &= ~PIPE_MASK_S;
   }

   if (!util_blitter_is_blit_supported(ctx->blitter, &info)) {
      debug_printf("swr: blit unsupported %s -> %s\n",
                   util_format_short_name(info.src.resource->format),
                   util_format_short_name(info.dst.resource->format));
      return;
   }

   if (ctx->active_queries) {
      ctx->api.pfnSwrEnableStatsFE(ctx->swrContext, FALSE);
      ctx->api.pfnSwrEnableStatsBE(ctx->swrContext, FALSE);
   }

   util_blitter_save_vertex_buffer_slot(ctx->blitter, ctx->vertex_buffer);
   util_blitter_save_vertex_elements(ctx->blitter, (void *)ctx->velems);
   util_blitter_save_vertex_shader(ctx->blitter, (void *)ctx->vs);
   util_blitter_save_geometry_shader(ctx->blitter, (void*)ctx->gs);
   util_blitter_save_tessctrl_shader(ctx->blitter, (void*)ctx->tcs);
   util_blitter_save_tesseval_shader(ctx->blitter, (void*)ctx->tes);
   util_blitter_save_so_targets(
      ctx->blitter,
      ctx->num_so_targets,
      (struct pipe_stream_output_target **)ctx->so_targets);
   util_blitter_save_rasterizer(ctx->blitter, (void *)ctx->rasterizer);
   util_blitter_save_viewport(ctx->blitter, &ctx->viewports[0]);
   util_blitter_save_scissor(ctx->blitter, &ctx->scissors[0]);
   util_blitter_save_fragment_shader(ctx->blitter, ctx->fs);
   util_blitter_save_blend(ctx->blitter, (void *)ctx->blend);
   util_blitter_save_depth_stencil_alpha(ctx->blitter,
                                         (void *)ctx->depth_stencil);
   util_blitter_save_stencil_ref(ctx->blitter, &ctx->stencil_ref);
   util_blitter_save_sample_mask(ctx->blitter, ctx->sample_mask);
   util_blitter_save_framebuffer(ctx->blitter, &ctx->framebuffer);
   util_blitter_save_fragment_sampler_states(
      ctx->blitter,
      ctx->num_samplers[PIPE_SHADER_FRAGMENT],
      (void **)ctx->samplers[PIPE_SHADER_FRAGMENT]);
   util_blitter_save_fragment_sampler_views(
      ctx->blitter,
      ctx->num_sampler_views[PIPE_SHADER_FRAGMENT],
      ctx->sampler_views[PIPE_SHADER_FRAGMENT]);
   util_blitter_save_render_condition(ctx->blitter,
                                      ctx->render_cond_query,
                                      ctx->render_cond_cond,
                                      ctx->render_cond_mode);

   util_blitter_blit(ctx->blitter, &info);

   if (ctx->active_queries) {
      ctx->api.pfnSwrEnableStatsFE(ctx->swrContext, TRUE);
      ctx->api.pfnSwrEnableStatsBE(ctx->swrContext, TRUE);
   }
}


static void
swr_destroy(struct pipe_context *pipe)
{
   struct swr_context *ctx = swr_context(pipe);
   struct swr_screen *screen = swr_screen(pipe->screen);

   if (ctx->blitter)
      util_blitter_destroy(ctx->blitter);

   for (unsigned i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
      if (ctx->framebuffer.cbufs[i]) {
         struct swr_resource *res = swr_resource(ctx->framebuffer.cbufs[i]->texture);
         /* NULL curr_pipe, so we don't have a reference to a deleted pipe */
         res->curr_pipe = NULL;
         pipe_surface_reference(&ctx->framebuffer.cbufs[i], NULL);
      }
   }

   if (ctx->framebuffer.zsbuf) {
      struct swr_resource *res = swr_resource(ctx->framebuffer.zsbuf->texture);
      /* NULL curr_pipe, so we don't have a reference to a deleted pipe */
      res->curr_pipe = NULL;
      pipe_surface_reference(&ctx->framebuffer.zsbuf, NULL);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(ctx->sampler_views[0]); i++) {
      pipe_sampler_view_reference(&ctx->sampler_views[PIPE_SHADER_FRAGMENT][i], NULL);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(ctx->sampler_views[0]); i++) {
      pipe_sampler_view_reference(&ctx->sampler_views[PIPE_SHADER_VERTEX][i], NULL);
   }

   if (ctx->pipe.stream_uploader)
      u_upload_destroy(ctx->pipe.stream_uploader);

   /* Idle core after destroying buffer resources, but before deleting
    * context.  Destroying resources has potentially called StoreTiles.*/
   ctx->api.pfnSwrWaitForIdle(ctx->swrContext);

   if (ctx->swrContext)
      ctx->api.pfnSwrDestroyContext(ctx->swrContext);

   delete ctx->blendJIT;

   swr_destroy_scratch_buffers(ctx);


   /* Only update screen->pipe if current context is being destroyed */
   assert(screen);
   if (screen->pipe == pipe)
      screen->pipe = NULL;

   AlignedFree(ctx);
}


static void
swr_render_condition(struct pipe_context *pipe,
                     struct pipe_query *query,
                     bool condition,
                     enum pipe_render_cond_flag mode)
{
   struct swr_context *ctx = swr_context(pipe);

   ctx->render_cond_query = query;
   ctx->render_cond_mode = mode;
   ctx->render_cond_cond = condition;
}


static void
swr_flush_resource(struct pipe_context *ctx, struct pipe_resource *resource)
{
   // NOOP
}

static void
swr_UpdateStats(HANDLE hPrivateContext, const SWR_STATS *pStats)
{
   swr_draw_context *pDC = (swr_draw_context*)hPrivateContext;

   if (!pDC)
      return;

   struct swr_query_result *pqr = pDC->pStats;

   SWR_STATS *pSwrStats = &pqr->core;

   pSwrStats->DepthPassCount += pStats->DepthPassCount;
   pSwrStats->PsInvocations += pStats->PsInvocations;
   pSwrStats->CsInvocations += pStats->CsInvocations;
}

static void
swr_UpdateStatsFE(HANDLE hPrivateContext, const SWR_STATS_FE *pStats)
{
   swr_draw_context *pDC = (swr_draw_context*)hPrivateContext;

   if (!pDC)
      return;

   struct swr_query_result *pqr = pDC->pStats;

   SWR_STATS_FE *pSwrStats = &pqr->coreFE;
   p_atomic_add(&pSwrStats->IaVertices, pStats->IaVertices);
   p_atomic_add(&pSwrStats->IaPrimitives, pStats->IaPrimitives);
   p_atomic_add(&pSwrStats->VsInvocations, pStats->VsInvocations);
   p_atomic_add(&pSwrStats->HsInvocations, pStats->HsInvocations);
   p_atomic_add(&pSwrStats->DsInvocations, pStats->DsInvocations);
   p_atomic_add(&pSwrStats->GsInvocations, pStats->GsInvocations);
   p_atomic_add(&pSwrStats->CInvocations, pStats->CInvocations);
   p_atomic_add(&pSwrStats->CPrimitives, pStats->CPrimitives);
   p_atomic_add(&pSwrStats->GsPrimitives, pStats->GsPrimitives);

   for (unsigned i = 0; i < 4; i++) {
      p_atomic_add(&pSwrStats->SoPrimStorageNeeded[i],
            pStats->SoPrimStorageNeeded[i]);
      p_atomic_add(&pSwrStats->SoNumPrimsWritten[i],
            pStats->SoNumPrimsWritten[i]);
   }
}

static void
swr_UpdateStreamOut(HANDLE hPrivateContext, uint64_t numPrims)
{
   swr_draw_context *pDC = (swr_draw_context*)hPrivateContext;

   if (!pDC)
      return;

   if (pDC->soPrims)
       *pDC->soPrims += numPrims;
}

struct pipe_context *
swr_create_context(struct pipe_screen *p_screen, void *priv, unsigned flags)
{
   struct swr_context *ctx = (struct swr_context *)
      AlignedMalloc(sizeof(struct swr_context), KNOB_SIMD_BYTES);
   memset((void*)ctx, 0, sizeof(struct swr_context));

   swr_screen(p_screen)->pfnSwrGetInterface(ctx->api);
   swr_screen(p_screen)->pfnSwrGetTileInterface(ctx->tileApi);
   ctx->swrDC.pAPI = &ctx->api;
   ctx->swrDC.pTileAPI = &ctx->tileApi;

   ctx->blendJIT =
      new std::unordered_map<BLEND_COMPILE_STATE, PFN_BLEND_JIT_FUNC>;

   ctx->max_draws_in_flight = KNOB_MAX_DRAWS_IN_FLIGHT;

   SWR_CREATECONTEXT_INFO createInfo {0};

   createInfo.privateStateSize = sizeof(swr_draw_context);
   createInfo.pfnLoadTile = swr_LoadHotTile;
   createInfo.pfnStoreTile = swr_StoreHotTile;
   createInfo.pfnUpdateStats = swr_UpdateStats;
   createInfo.pfnUpdateStatsFE = swr_UpdateStatsFE;
   createInfo.pfnUpdateStreamOut = swr_UpdateStreamOut;
   createInfo.pfnMakeGfxPtr = swr_MakeGfxPtr;

   SWR_THREADING_INFO threadingInfo {0};

   threadingInfo.MAX_WORKER_THREADS        = KNOB_MAX_WORKER_THREADS;
   threadingInfo.MAX_NUMA_NODES            = KNOB_MAX_NUMA_NODES;
   threadingInfo.MAX_CORES_PER_NUMA_NODE   = KNOB_MAX_CORES_PER_NUMA_NODE;
   threadingInfo.MAX_THREADS_PER_CORE      = KNOB_MAX_THREADS_PER_CORE;
   threadingInfo.SINGLE_THREADED           = KNOB_SINGLE_THREADED;

   // Use non-standard settings for KNL
   if (swr_screen(p_screen)->is_knl)
   {
      if (nullptr == getenv("KNOB_MAX_THREADS_PER_CORE"))
         threadingInfo.MAX_THREADS_PER_CORE  = 2;

      if (nullptr == getenv("KNOB_MAX_DRAWS_IN_FLIGHT"))
      {
         ctx->max_draws_in_flight = 2048;
         createInfo.MAX_DRAWS_IN_FLIGHT = ctx->max_draws_in_flight;
      }
   }

   createInfo.pThreadInfo = &threadingInfo;

   ctx->swrContext = ctx->api.pfnSwrCreateContext(&createInfo);

   ctx->api.pfnSwrInit();

   if (ctx->swrContext == NULL)
      goto fail;

   ctx->pipe.screen = p_screen;
   ctx->pipe.destroy = swr_destroy;
   ctx->pipe.priv = priv;
   ctx->pipe.create_surface = swr_create_surface;
   ctx->pipe.surface_destroy = swr_surface_destroy;
   ctx->pipe.transfer_map = swr_transfer_map;
   ctx->pipe.transfer_unmap = swr_transfer_unmap;
   ctx->pipe.transfer_flush_region = swr_transfer_flush_region;

   ctx->pipe.buffer_subdata = u_default_buffer_subdata;
   ctx->pipe.texture_subdata = u_default_texture_subdata;

   ctx->pipe.clear_texture = util_clear_texture;
   ctx->pipe.resource_copy_region = swr_resource_copy;
   ctx->pipe.flush_resource = swr_flush_resource;
   ctx->pipe.render_condition = swr_render_condition;

   swr_state_init(&ctx->pipe);
   swr_clear_init(&ctx->pipe);
   swr_draw_init(&ctx->pipe);
   swr_query_init(&ctx->pipe);

   ctx->pipe.stream_uploader = u_upload_create_default(&ctx->pipe);
   if (!ctx->pipe.stream_uploader)
      goto fail;
   ctx->pipe.const_uploader = ctx->pipe.stream_uploader;

   ctx->pipe.blit = swr_blit;
   ctx->blitter = util_blitter_create(&ctx->pipe);
   if (!ctx->blitter)
      goto fail;

   swr_init_scratch_buffers(ctx);

   return &ctx->pipe;

fail:
   /* Should really validate the init steps and fail gracefully */
   swr_destroy(&ctx->pipe);
   return NULL;
}
