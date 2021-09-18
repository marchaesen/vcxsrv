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

#include "swr_screen.h"
#include "swr_context.h"
#include "swr_resource.h"
#include "swr_fence.h"
#include "swr_query.h"
#include "jit_api.h"

#include "util/u_draw.h"
#include "util/u_prim.h"

#include <algorithm>
#include <iostream>
/*
 * Draw vertex arrays, with optional indexing, optional instancing.
 */
static void
swr_draw_vbo(struct pipe_context *pipe, const struct pipe_draw_info *info,
             unsigned drawid_offset,
             const struct pipe_draw_indirect_info *indirect,
             const struct pipe_draw_start_count_bias *draws,
             unsigned num_draws)
{
   if (num_draws > 1) {
      struct pipe_draw_info tmp_info = *info;
      unsigned drawid = drawid_offset;

      for (unsigned i = 0; i < num_draws; i++) {
         swr_draw_vbo(pipe, &tmp_info, drawid, indirect, &draws[i], 1);
         if (tmp_info.increment_draw_id)
            drawid++;
      }
      return;
   }

   if (!indirect && (!draws[0].count || !info->instance_count))
      return;

   struct swr_context *ctx = swr_context(pipe);

   if (!indirect &&
       !info->primitive_restart &&
       !u_trim_pipe_prim(info->mode, (unsigned*)&draws[0].count))
      return;

   if (!swr_check_render_cond(pipe))
      return;

   if (indirect && indirect->buffer) {
      util_draw_indirect(pipe, info, indirect);
      return;
   }

   /* If indexed draw, force vertex validation since index buffer comes
    * from draw info. */
   if (info->index_size)
      ctx->dirty |= SWR_NEW_VERTEX;

   /* Update derived state, pass draw info to update function. */
   swr_update_derived(pipe, info, draws);

   swr_update_draw_context(ctx);

   struct pipe_draw_info resolved_info;
   struct pipe_draw_start_count_bias resolved_draw;
   /* DrawTransformFeedback */
   if (indirect && indirect->count_from_stream_output) {
      // trick copied from softpipe to modify const struct *info
      memcpy(&resolved_info, (void*)info, sizeof(struct pipe_draw_info));
      resolved_draw.start = draws[0].start;
      resolved_draw.count = ctx->so_primCounter * ctx->patch_vertices;
      resolved_info.max_index = resolved_draw.count - 1;
      info = &resolved_info;
      indirect = NULL;
      draws = &resolved_draw;
   }

   if (ctx->vs->pipe.stream_output.num_outputs) {
      if (!ctx->vs->soFunc[info->mode]) {
         STREAMOUT_COMPILE_STATE state = {0};
         struct pipe_stream_output_info *so = &ctx->vs->pipe.stream_output;

         state.numVertsPerPrim = u_vertices_per_prim(info->mode);

         uint32_t offsets[MAX_SO_STREAMS] = {0};
         uint32_t num = 0;

         for (uint32_t i = 0; i < so->num_outputs; i++) {
            assert(so->output[i].stream == 0); // @todo
            uint32_t output_buffer = so->output[i].output_buffer;
            if (so->output[i].dst_offset != offsets[output_buffer]) {
               // hole - need to fill
               state.stream.decl[num].bufferIndex = output_buffer;
               state.stream.decl[num].hole = true;
               state.stream.decl[num].componentMask =
                  (1 << (so->output[i].dst_offset - offsets[output_buffer]))
                  - 1;
               num++;
               offsets[output_buffer] = so->output[i].dst_offset;
            }

            unsigned attrib_slot = so->output[i].register_index;
            attrib_slot = swr_so_adjust_attrib(attrib_slot, ctx->vs);

            state.stream.decl[num].bufferIndex = output_buffer;
            state.stream.decl[num].attribSlot = attrib_slot;
            state.stream.decl[num].componentMask =
               ((1 << so->output[i].num_components) - 1)
               << so->output[i].start_component;
            state.stream.decl[num].hole = false;
            num++;

            offsets[output_buffer] += so->output[i].num_components;
         }

         state.stream.numDecls = num;

         HANDLE hJitMgr = swr_screen(pipe->screen)->hJitMgr;
         ctx->vs->soFunc[info->mode] = JitCompileStreamout(hJitMgr, state);
         debug_printf("so shader    %p\n", ctx->vs->soFunc[info->mode]);
         assert(ctx->vs->soFunc[info->mode] && "Error: SoShader = NULL");
      }

      ctx->api.pfnSwrSetSoFunc(ctx->swrContext, ctx->vs->soFunc[info->mode], 0);
   }

   struct swr_vertex_element_state *velems = ctx->velems;
   if (info->primitive_restart)
      velems->fsState.cutIndex = info->restart_index;
   else
      velems->fsState.cutIndex = 0;
   velems->fsState.bEnableCutIndex = info->primitive_restart;
   velems->fsState.bPartialVertexBuffer = (info->index_bounds_valid && info->min_index > 0);

   swr_jit_fetch_key key;
   swr_generate_fetch_key(key, velems);
   auto search = velems->map.find(key);
   if (search != velems->map.end()) {
      velems->fsFunc = search->second;
   } else {
      HANDLE hJitMgr = swr_screen(ctx->pipe.screen)->hJitMgr;
      velems->fsFunc = JitCompileFetch(hJitMgr, velems->fsState);

      debug_printf("fetch shader %p\n", velems->fsFunc);
      assert(velems->fsFunc && "Error: FetchShader = NULL");

      velems->map.insert(std::make_pair(key, velems->fsFunc));
   }

   ctx->api.pfnSwrSetFetchFunc(ctx->swrContext, velems->fsFunc);

   /* Set up frontend state
    * XXX setup provokingVertex & topologyProvokingVertex */
   SWR_FRONTEND_STATE feState = {0};

   // feState.vsVertexSize seeds the PA size that is used as an interface
   // between all the shader stages, so it has to be large enough to
   // incorporate all interfaces between stages

   // max of frontend shaders num_outputs
   feState.vsVertexSize = ctx->vs->info.base.num_outputs;
   if (ctx->gs) {
      feState.vsVertexSize = std::max(feState.vsVertexSize, (uint32_t)ctx->gs->info.base.num_outputs);
   }
   if (ctx->tcs) {
      feState.vsVertexSize = std::max(feState.vsVertexSize, (uint32_t)ctx->tcs->info.base.num_outputs);
   }
   if (ctx->tes) {
      feState.vsVertexSize = std::max(feState.vsVertexSize, (uint32_t)ctx->tes->info.base.num_outputs);
   }


   if (ctx->vs->info.base.num_outputs) {
      // gs does not adjust for position in SGV slot at input from vs
      if (!ctx->gs && !ctx->tcs && !ctx->tes)
         feState.vsVertexSize--;
   }

   // other (non-SGV) slots start at VERTEX_ATTRIB_START_SLOT
   feState.vsVertexSize += VERTEX_ATTRIB_START_SLOT;

   // The PA in the clipper does not handle BE vertex sizes
   // different from FE. Increase vertexsize only for the cases that needed it

   // primid needs a slot
   if (ctx->fs->info.base.uses_primid)
      feState.vsVertexSize++;
   // sprite coord enable
   if (ctx->rasterizer->sprite_coord_enable)
      feState.vsVertexSize++;

   if (ctx->rasterizer->flatshade_first) {
      feState.provokingVertex = {1, 0, 0};
   } else {
      feState.provokingVertex = {2, 1, 2};
   }

   enum pipe_prim_type topology;
   if (ctx->gs)
      topology = (pipe_prim_type)ctx->gs->info.base.properties[TGSI_PROPERTY_GS_OUTPUT_PRIM];
   else
      topology = info->mode;

   switch (topology) {
   case PIPE_PRIM_TRIANGLE_FAN:
      feState.topologyProvokingVertex = feState.provokingVertex.triFan;
      break;
   case PIPE_PRIM_TRIANGLE_STRIP:
   case PIPE_PRIM_TRIANGLES:
      feState.topologyProvokingVertex = feState.provokingVertex.triStripList;
      break;
   case PIPE_PRIM_QUAD_STRIP:
   case PIPE_PRIM_QUADS:
      if (ctx->rasterizer->flatshade_first)
         feState.topologyProvokingVertex = 0;
      else
         feState.topologyProvokingVertex = 3;
      break;
   case PIPE_PRIM_LINES:
   case PIPE_PRIM_LINE_LOOP:
   case PIPE_PRIM_LINE_STRIP:
      feState.topologyProvokingVertex = feState.provokingVertex.lineStripList;
      break;
   default:
      feState.topologyProvokingVertex = 0;
   }

   feState.bEnableCutIndex = info->primitive_restart;
   ctx->api.pfnSwrSetFrontendState(ctx->swrContext, &feState);

   if (info->index_size)
      ctx->api.pfnSwrDrawIndexedInstanced(ctx->swrContext,
                                          swr_convert_prim_topology(info->mode, ctx->patch_vertices),
                                          draws[0].count,
                                          info->instance_count,
                                          draws[0].start,
                                          draws->index_bias,
                                          info->start_instance);
   else
      ctx->api.pfnSwrDrawInstanced(ctx->swrContext,
                                   swr_convert_prim_topology(info->mode, ctx->patch_vertices),
                                   draws[0].count,
                                   info->instance_count,
                                   draws[0].start,
                                   info->start_instance);

   /* On client-buffer draw, we used client buffer directly, without
    * copy.  Block until draw is finished.
    * VMD is an example application that benefits from this. */
   if (ctx->dirty & SWR_BLOCK_CLIENT_DRAW) {
      struct swr_screen *screen = swr_screen(pipe->screen);
      swr_fence_submit(ctx, screen->flush_fence);
      swr_fence_finish(pipe->screen, NULL, screen->flush_fence, 0);
   }
}


static void
swr_flush(struct pipe_context *pipe,
          struct pipe_fence_handle **fence,
          unsigned flags)
{
   struct swr_context *ctx = swr_context(pipe);
   struct swr_screen *screen = swr_screen(pipe->screen);

   for (int i=0; i < ctx->framebuffer.nr_cbufs; i++) {
      struct pipe_surface *cb = ctx->framebuffer.cbufs[i];
      if (cb) {
         swr_store_dirty_resource(pipe, cb->texture, SWR_TILE_RESOLVED);
      }
   }
   if (ctx->framebuffer.zsbuf) {
      swr_store_dirty_resource(pipe, ctx->framebuffer.zsbuf->texture,
                               SWR_TILE_RESOLVED);
   }

   if (fence)
      swr_fence_reference(pipe->screen, fence, screen->flush_fence);
}

void
swr_finish(struct pipe_context *pipe)
{
   struct pipe_fence_handle *fence = nullptr;

   swr_flush(pipe, &fence, 0);
   swr_fence_finish(pipe->screen, NULL, fence, 0);
   swr_fence_reference(pipe->screen, &fence, NULL);
}

/*
 * Invalidate tiles so they can be reloaded back when needed
 */
void
swr_invalidate_render_target(struct pipe_context *pipe,
                             uint32_t attachment,
                             uint16_t width, uint16_t height)
{
   struct swr_context *ctx = swr_context(pipe);

   /* grab the rect from the passed in arguments */
   swr_update_draw_context(ctx);
   SWR_RECT full_rect =
      {0, 0, (int32_t)width, (int32_t)height};
   ctx->api.pfnSwrInvalidateTiles(ctx->swrContext,
                                  1 << attachment,
                                  full_rect);
}


/*
 * Store SWR HotTiles back to renderTarget surface.
 */
void
swr_store_render_target(struct pipe_context *pipe,
                        uint32_t attachment,
                        enum SWR_TILE_STATE post_tile_state)
{
   struct swr_context *ctx = swr_context(pipe);
   struct swr_draw_context *pDC = &ctx->swrDC;
   struct SWR_SURFACE_STATE *renderTarget = &pDC->renderTargets[attachment];

   /* Only proceed if there's a valid surface to store to */
   if (renderTarget->xpBaseAddress) {
      swr_update_draw_context(ctx);
      SWR_RECT full_rect =
         {0, 0,
          (int32_t)u_minify(renderTarget->width, renderTarget->lod),
          (int32_t)u_minify(renderTarget->height, renderTarget->lod)};
      ctx->api.pfnSwrStoreTiles(ctx->swrContext,
                                1 << attachment,
                                post_tile_state,
                                full_rect);
   }
}

void
swr_store_dirty_resource(struct pipe_context *pipe,
                         struct pipe_resource *resource,
                         enum SWR_TILE_STATE post_tile_state)
{
   /* Only store resource if it has been written to */
   if (swr_resource(resource)->status & SWR_RESOURCE_WRITE) {
      struct swr_context *ctx = swr_context(pipe);
      struct swr_screen *screen = swr_screen(pipe->screen);
      struct swr_resource *spr = swr_resource(resource);

      swr_draw_context *pDC = &ctx->swrDC;
      SWR_SURFACE_STATE *renderTargets = pDC->renderTargets;
      for (uint32_t i = 0; i < SWR_NUM_ATTACHMENTS; i++)
         if (renderTargets[i].xpBaseAddress == spr->swr.xpBaseAddress ||
             (spr->secondary.xpBaseAddress &&
              renderTargets[i].xpBaseAddress == spr->secondary.xpBaseAddress)) {
            swr_store_render_target(pipe, i, post_tile_state);

            /* Mesa thinks depth/stencil are fused, so we'll never get an
             * explicit resource for stencil.  So, if checking depth, then
             * also check for stencil. */
            if (spr->has_stencil && (i == SWR_ATTACHMENT_DEPTH)) {
               swr_store_render_target(
                  pipe, SWR_ATTACHMENT_STENCIL, post_tile_state);
            }

            /* This fence signals StoreTiles completion */
            swr_fence_submit(ctx, screen->flush_fence);

            break;
         }
   }
}

void
swr_draw_init(struct pipe_context *pipe)
{
   pipe->draw_vbo = swr_draw_vbo;
   pipe->flush = swr_flush;
}
