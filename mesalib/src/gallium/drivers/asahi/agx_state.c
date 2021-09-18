/*
 * Copyright 2021 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
 * Copyright 2010 Red Hat Inc.
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
#include <stdio.h>
#include <errno.h>
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_transfer.h"
#include "gallium/auxiliary/util/u_draw.h"
#include "gallium/auxiliary/util/u_helpers.h"
#include "gallium/auxiliary/util/u_viewport.h"
#include "gallium/auxiliary/util/u_blend.h"
#include "gallium/auxiliary/util/u_framebuffer.h"
#include "gallium/auxiliary/tgsi/tgsi_from_mesa.h"
#include "gallium/auxiliary/nir/tgsi_to_nir.h"
#include "compiler/nir/nir.h"
#include "asahi/compiler/agx_compile.h"
#include "agx_state.h"
#include "asahi/lib/agx_pack.h"
#include "asahi/lib/agx_formats.h"

static struct pipe_stream_output_target *
agx_create_stream_output_target(struct pipe_context *pctx,
                                struct pipe_resource *prsc,
                                unsigned buffer_offset,
                                unsigned buffer_size)
{
   struct pipe_stream_output_target *target;

   target = &rzalloc(pctx, struct agx_streamout_target)->base;

   if (!target)
      return NULL;

   pipe_reference_init(&target->reference, 1);
   pipe_resource_reference(&target->buffer, prsc);

   target->context = pctx;
   target->buffer_offset = buffer_offset;
   target->buffer_size = buffer_size;

   return target;
}

static void
agx_stream_output_target_destroy(struct pipe_context *pctx,
                                 struct pipe_stream_output_target *target)
{
   pipe_resource_reference(&target->buffer, NULL);
   ralloc_free(target);
}

static void
agx_set_stream_output_targets(struct pipe_context *pctx,
                              unsigned num_targets,
                              struct pipe_stream_output_target **targets,
                              const unsigned *offsets)
{
   struct agx_context *ctx = agx_context(pctx);
   struct agx_streamout *so = &ctx->streamout;

   assert(num_targets <= ARRAY_SIZE(so->targets));

   for (unsigned i = 0; i < num_targets; i++) {
      if (offsets[i] != -1)
         agx_so_target(targets[i])->offset = offsets[i];

      pipe_so_target_reference(&so->targets[i], targets[i]);
   }

   for (unsigned i = 0; i < so->num_targets; i++)
      pipe_so_target_reference(&so->targets[i], NULL);

   so->num_targets = num_targets;
}

static void
agx_set_blend_color(struct pipe_context *pctx,
                    const struct pipe_blend_color *state)
{
   struct agx_context *ctx = agx_context(pctx);

   if (state)
      memcpy(&ctx->blend_color, state, sizeof(*state));
}

static void *
agx_create_blend_state(struct pipe_context *ctx,
                       const struct pipe_blend_state *state)
{
   struct agx_blend *so = CALLOC_STRUCT(agx_blend);

   assert(!state->alpha_to_coverage);
   assert(!state->alpha_to_coverage_dither);
   assert(!state->alpha_to_one);
   assert(!state->advanced_blend_func);

   if (state->logicop_enable) {
      so->logicop_enable = true;
      so->logicop_func = state->logicop_func;
      return so;
   }

   for (unsigned i = 0; i < PIPE_MAX_COLOR_BUFS; ++i) {
      unsigned rti = state->independent_blend_enable ? i : 0;
      struct pipe_rt_blend_state rt = state->rt[rti];

      if (!rt.blend_enable) {
         static const nir_lower_blend_channel replace = {
            .func = BLEND_FUNC_ADD,
            .src_factor = BLEND_FACTOR_ZERO,
            .invert_src_factor = true,
            .dst_factor = BLEND_FACTOR_ZERO,
            .invert_dst_factor = false,
         };

         so->rt[i].rgb = replace;
         so->rt[i].alpha = replace;
      } else {
         so->rt[i].rgb.func = util_blend_func_to_shader(rt.rgb_func);
         so->rt[i].rgb.src_factor = util_blend_factor_to_shader(rt.rgb_src_factor);
         so->rt[i].rgb.invert_src_factor = util_blend_factor_is_inverted(rt.rgb_src_factor);
         so->rt[i].rgb.dst_factor = util_blend_factor_to_shader(rt.rgb_dst_factor);
         so->rt[i].rgb.invert_dst_factor = util_blend_factor_is_inverted(rt.rgb_dst_factor);

         so->rt[i].alpha.func = util_blend_func_to_shader(rt.alpha_func);
         so->rt[i].alpha.src_factor = util_blend_factor_to_shader(rt.alpha_src_factor);
         so->rt[i].alpha.invert_src_factor = util_blend_factor_is_inverted(rt.alpha_src_factor);
         so->rt[i].alpha.dst_factor = util_blend_factor_to_shader(rt.alpha_dst_factor);
         so->rt[i].alpha.invert_dst_factor = util_blend_factor_is_inverted(rt.alpha_dst_factor);

	 so->blend_enable = true;
      }

      so->rt[i].colormask = rt.colormask;
   }

   return so;
}

static void
agx_bind_blend_state(struct pipe_context *pctx, void *cso)
{
   struct agx_context *ctx = agx_context(pctx);
   ctx->blend = cso;
}

static const enum agx_stencil_op agx_stencil_ops[PIPE_STENCIL_OP_INVERT + 1] = {
   [PIPE_STENCIL_OP_KEEP] = AGX_STENCIL_OP_KEEP,
   [PIPE_STENCIL_OP_ZERO] = AGX_STENCIL_OP_ZERO,
   [PIPE_STENCIL_OP_REPLACE] = AGX_STENCIL_OP_REPLACE,
   [PIPE_STENCIL_OP_INCR] = AGX_STENCIL_OP_INCR_SAT,
   [PIPE_STENCIL_OP_DECR] = AGX_STENCIL_OP_DECR_SAT,
   [PIPE_STENCIL_OP_INCR_WRAP] = AGX_STENCIL_OP_INCR_WRAP,
   [PIPE_STENCIL_OP_DECR_WRAP] = AGX_STENCIL_OP_DECR_WRAP,
   [PIPE_STENCIL_OP_INVERT] = AGX_STENCIL_OP_INVERT,
};

static void
agx_pack_rasterizer_face(struct agx_rasterizer_face_packed *out,
                         struct pipe_stencil_state st,
                         enum agx_zs_func z_func,
                         bool disable_z_write)
{
   agx_pack(out, RASTERIZER_FACE, cfg) {
      cfg.depth_function = z_func;
      cfg.disable_depth_write = disable_z_write;

      if (st.enabled) {
         cfg.stencil_write_mask = st.writemask;
         cfg.stencil_read_mask = st.valuemask;

         cfg.depth_pass   = agx_stencil_ops[st.zpass_op];
         cfg.depth_fail   = agx_stencil_ops[st.zfail_op];
         cfg.stencil_fail = agx_stencil_ops[st.fail_op];

         cfg.stencil_compare = (enum agx_zs_func) st.func;
      } else {
         cfg.stencil_write_mask = 0xFF;
         cfg.stencil_read_mask = 0xFF;

         cfg.depth_pass = AGX_STENCIL_OP_KEEP;
         cfg.depth_fail = AGX_STENCIL_OP_KEEP;
         cfg.stencil_fail = AGX_STENCIL_OP_KEEP;

         cfg.stencil_compare = AGX_ZS_FUNC_ALWAYS;
      }
   }
}

static void *
agx_create_zsa_state(struct pipe_context *ctx,
                     const struct pipe_depth_stencil_alpha_state *state)
{
   struct agx_zsa *so = CALLOC_STRUCT(agx_zsa);
   assert(!state->depth_bounds_test && "todo");

   so->base = *state;

   /* Z func can be used as-is */
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_NEVER    == AGX_ZS_FUNC_NEVER);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_LESS     == AGX_ZS_FUNC_LESS);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_EQUAL    == AGX_ZS_FUNC_EQUAL);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_LEQUAL   == AGX_ZS_FUNC_LEQUAL);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_GREATER  == AGX_ZS_FUNC_GREATER);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_NOTEQUAL == AGX_ZS_FUNC_NOT_EQUAL);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_GEQUAL   == AGX_ZS_FUNC_GEQUAL);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_ALWAYS   == AGX_ZS_FUNC_ALWAYS);

   enum agx_zs_func z_func = state->depth_enabled ?
                ((enum agx_zs_func) state->depth_func) : AGX_ZS_FUNC_ALWAYS;

   agx_pack_rasterizer_face(&so->front,
         state->stencil[0], z_func, !state->depth_writemask);

   if (state->stencil[1].enabled) {
      agx_pack_rasterizer_face(&so->back,
            state->stencil[1], z_func, !state->depth_writemask);
   } else {
      /* One sided stencil */
      so->back = so->front;
   }

   return so;
}

static void
agx_bind_zsa_state(struct pipe_context *pctx, void *cso)
{
   struct agx_context *ctx = agx_context(pctx);

   if (cso)
      memcpy(&ctx->zs, cso, sizeof(ctx->zs));
}

static void *
agx_create_rs_state(struct pipe_context *ctx,
                    const struct pipe_rasterizer_state *cso)
{
   struct agx_rasterizer *so = CALLOC_STRUCT(agx_rasterizer);
   so->base = *cso;

   /* Line width is packed in a 4:4 fixed point format */
   unsigned line_width_fixed = ((unsigned) (cso->line_width * 16.0f)) - 1;

   /* Clamp to maximum line width */
   so->line_width = MIN2(line_width_fixed, 0xFF);

   agx_pack(so->cull, CULL, cfg) {
      cfg.cull_front = cso->cull_face & PIPE_FACE_FRONT;
      cfg.cull_back = cso->cull_face & PIPE_FACE_BACK;
      cfg.front_face_ccw = cso->front_ccw;
      cfg.depth_clip = cso->depth_clip_near;
      cfg.depth_clamp = !cso->depth_clip_near;
   };

   return so;
}

static void
agx_bind_rasterizer_state(struct pipe_context *pctx, void *cso)
{
   struct agx_context *ctx = agx_context(pctx);
   struct agx_rasterizer *so = cso;

   /* Check if scissor state has changed, since scissor enable is part of the
    * rasterizer state but everything else needed for scissors is part of
    * viewport/scissor states */
   bool scissor_changed = (cso == NULL) || (ctx->rast == NULL) ||
      (ctx->rast->base.scissor != so->base.scissor);

   ctx->rast = so;

   if (scissor_changed)
      ctx->dirty |= AGX_DIRTY_SCISSOR;
}

static enum agx_wrap
agx_wrap_from_pipe(enum pipe_tex_wrap in)
{
   switch (in) {
   case PIPE_TEX_WRAP_REPEAT: return AGX_WRAP_REPEAT;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE: return AGX_WRAP_CLAMP_TO_EDGE;
   case PIPE_TEX_WRAP_MIRROR_REPEAT: return AGX_WRAP_MIRRORED_REPEAT;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER: return AGX_WRAP_CLAMP_TO_BORDER;
   default: unreachable("todo: more wrap modes");
   }
}

static enum agx_mip_filter
agx_mip_filter_from_pipe(enum pipe_tex_mipfilter in)
{
   switch (in) {
   case PIPE_TEX_MIPFILTER_NEAREST: return AGX_MIP_FILTER_NEAREST;
   case PIPE_TEX_MIPFILTER_LINEAR: return AGX_MIP_FILTER_LINEAR;
   case PIPE_TEX_MIPFILTER_NONE: return AGX_MIP_FILTER_NONE;
   }

   unreachable("Invalid mip filter");
}

static const enum agx_compare_func agx_compare_funcs[PIPE_FUNC_ALWAYS + 1] = {
   [PIPE_FUNC_NEVER] = AGX_COMPARE_FUNC_NEVER,
   [PIPE_FUNC_LESS] = AGX_COMPARE_FUNC_LESS,
   [PIPE_FUNC_EQUAL] = AGX_COMPARE_FUNC_EQUAL,
   [PIPE_FUNC_LEQUAL] = AGX_COMPARE_FUNC_LEQUAL,
   [PIPE_FUNC_GREATER] = AGX_COMPARE_FUNC_GREATER,
   [PIPE_FUNC_NOTEQUAL] = AGX_COMPARE_FUNC_NOT_EQUAL,
   [PIPE_FUNC_GEQUAL] = AGX_COMPARE_FUNC_GEQUAL,
   [PIPE_FUNC_ALWAYS] = AGX_COMPARE_FUNC_ALWAYS,
};

static void *
agx_create_sampler_state(struct pipe_context *pctx,
                         const struct pipe_sampler_state *state)
{
   struct agx_device *dev = agx_device(pctx->screen);
   struct agx_bo *bo = agx_bo_create(dev, AGX_SAMPLER_LENGTH,
                                     AGX_MEMORY_TYPE_FRAMEBUFFER);

   assert(state->min_lod == 0 && "todo: lod clamps");
   assert(state->lod_bias == 0 && "todo: lod bias");

   agx_pack(bo->ptr.cpu, SAMPLER, cfg) {
      cfg.magnify_linear = (state->mag_img_filter == PIPE_TEX_FILTER_LINEAR);
      cfg.minify_linear = (state->min_img_filter == PIPE_TEX_FILTER_LINEAR);
      cfg.mip_filter = agx_mip_filter_from_pipe(state->min_mip_filter);
      cfg.wrap_s = agx_wrap_from_pipe(state->wrap_s);
      cfg.wrap_t = agx_wrap_from_pipe(state->wrap_t);
      cfg.wrap_r = agx_wrap_from_pipe(state->wrap_r);
      cfg.pixel_coordinates = !state->normalized_coords;
      cfg.compare_func = agx_compare_funcs[state->compare_func];
   }

   struct agx_sampler_state *so = CALLOC_STRUCT(agx_sampler_state);
   so->base = *state;
   so->desc = bo;

   return so;
}

static void
agx_delete_sampler_state(struct pipe_context *ctx, void *state)
{
   struct agx_bo *bo = state;
   agx_bo_unreference(bo);
}

static void
agx_bind_sampler_states(struct pipe_context *pctx,
                        enum pipe_shader_type shader,
                        unsigned start, unsigned count,
                        void **states)
{
   struct agx_context *ctx = agx_context(pctx);

   ctx->stage[shader].sampler_count = states ? count : 0;

   memcpy(&ctx->stage[shader].samplers[start], states,
          sizeof(struct agx_sampler_state *) * count);
}

/* Channels agree for RGBA but are weird for force 0/1 */

static enum agx_channel
agx_channel_from_pipe(enum pipe_swizzle in)
{
   STATIC_ASSERT((enum agx_channel) PIPE_SWIZZLE_X == AGX_CHANNEL_R);
   STATIC_ASSERT((enum agx_channel) PIPE_SWIZZLE_Y == AGX_CHANNEL_G);
   STATIC_ASSERT((enum agx_channel) PIPE_SWIZZLE_Z == AGX_CHANNEL_B);
   STATIC_ASSERT((enum agx_channel) PIPE_SWIZZLE_W == AGX_CHANNEL_A);
   STATIC_ASSERT((enum agx_channel) PIPE_SWIZZLE_0 & 0x4);
   STATIC_ASSERT((enum agx_channel) PIPE_SWIZZLE_1 & 0x4);
   STATIC_ASSERT((enum agx_channel) PIPE_SWIZZLE_NONE & 0x4);

   if ((in & 0x4) == 0)
      return (enum agx_channel) in;
   else if (in == PIPE_SWIZZLE_1)
      return AGX_CHANNEL_1;
   else
      return AGX_CHANNEL_0;
}

static enum agx_layout
agx_translate_layout(uint64_t modifier)
{
   switch (modifier) {
   case DRM_FORMAT_MOD_APPLE_64X64_MORTON_ORDER:
      return AGX_LAYOUT_TILED_64X64;
   case DRM_FORMAT_MOD_LINEAR:
      return AGX_LAYOUT_LINEAR;
   default:
      unreachable("Invalid modifier");
   }
}

static enum agx_texture_dimension
agx_translate_texture_dimension(enum pipe_texture_target dim)
{
   switch (dim) {
   case PIPE_TEXTURE_2D: return AGX_TEXTURE_DIMENSION_2D;
   case PIPE_TEXTURE_CUBE: return AGX_TEXTURE_DIMENSION_CUBE;
   default: unreachable("Unsupported texture dimension");
   }
}

static struct pipe_sampler_view *
agx_create_sampler_view(struct pipe_context *pctx,
                        struct pipe_resource *texture,
                        const struct pipe_sampler_view *state)
{
   struct agx_device *dev = agx_device(pctx->screen);
   struct agx_resource *rsrc = agx_resource(texture);
   struct agx_sampler_view *so = CALLOC_STRUCT(agx_sampler_view);

   if (!so)
      return NULL;

   /* We prepare the descriptor at CSO create time */
   so->desc = agx_bo_create(dev, AGX_TEXTURE_LENGTH,
                            AGX_MEMORY_TYPE_FRAMEBUFFER);

   const struct util_format_description *desc =
      util_format_description(state->format);

   /* We only have a single swizzle for the user swizzle and the format fixup,
    * so compose them now. */
   uint8_t out_swizzle[4];
   uint8_t view_swizzle[4] = {
      state->swizzle_r, state->swizzle_g,
      state->swizzle_b, state->swizzle_a
   };

   util_format_compose_swizzles(desc->swizzle, view_swizzle, out_swizzle);

   unsigned level = state->u.tex.first_level;
   assert(state->u.tex.first_layer == 0);

   /* Pack the descriptor into GPU memory */
   agx_pack(so->desc->ptr.cpu, TEXTURE, cfg) {
      cfg.dimension = agx_translate_texture_dimension(state->target);
      cfg.layout = agx_translate_layout(rsrc->modifier);
      cfg.format = agx_pixel_format[state->format].hw;
      cfg.swizzle_r = agx_channel_from_pipe(out_swizzle[0]);
      cfg.swizzle_g = agx_channel_from_pipe(out_swizzle[1]);
      cfg.swizzle_b = agx_channel_from_pipe(out_swizzle[2]);
      cfg.swizzle_a = agx_channel_from_pipe(out_swizzle[3]);
      cfg.width = u_minify(texture->width0, level);
      cfg.height = u_minify(texture->height0, level);
      cfg.levels = state->u.tex.last_level - level + 1;
      cfg.srgb = (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);
      cfg.address = rsrc->bo->ptr.gpu + rsrc->slices[level].offset;
      cfg.unk_2 = false;

      cfg.stride = (rsrc->modifier == DRM_FORMAT_MOD_LINEAR) ?
         (rsrc->slices[level].line_stride - 16) :
         AGX_RT_STRIDE_TILED;
   }

   /* Initialize base object */
   so->base = *state;
   so->base.texture = NULL;
   pipe_resource_reference(&so->base.texture, texture);
   pipe_reference_init(&so->base.reference, 1);
   so->base.context = pctx;
   return &so->base;
}

static void
agx_set_sampler_views(struct pipe_context *pctx,
                      enum pipe_shader_type shader,
                      unsigned start, unsigned count,
                      unsigned unbind_num_trailing_slots,
                      bool take_ownership,
                      struct pipe_sampler_view **views)
{
   struct agx_context *ctx = agx_context(pctx);
   unsigned new_nr = 0;
   unsigned i;

   assert(start == 0);

   if (!views)
      count = 0;

   for (i = 0; i < count; ++i) {
      if (views[i])
         new_nr = i + 1;

      if (take_ownership) {
         pipe_sampler_view_reference((struct pipe_sampler_view **)
                                     &ctx->stage[shader].textures[i], NULL);
         ctx->stage[shader].textures[i] = (struct agx_sampler_view *)views[i];
      } else {
         pipe_sampler_view_reference((struct pipe_sampler_view **)
                                     &ctx->stage[shader].textures[i], views[i]);
      }
   }

   for (; i < ctx->stage[shader].texture_count; i++) {
      pipe_sampler_view_reference((struct pipe_sampler_view **)
                                  &ctx->stage[shader].textures[i], NULL);
   }
   ctx->stage[shader].texture_count = new_nr;
}

static void
agx_sampler_view_destroy(struct pipe_context *ctx,
                         struct pipe_sampler_view *pview)
{
   struct agx_sampler_view *view = (struct agx_sampler_view *) pview;
   pipe_resource_reference(&view->base.texture, NULL);
   agx_bo_unreference(view->desc);
   FREE(view);
}

static struct pipe_surface *
agx_create_surface(struct pipe_context *ctx,
                   struct pipe_resource *texture,
                   const struct pipe_surface *surf_tmpl)
{
   struct pipe_surface *surface = CALLOC_STRUCT(pipe_surface);

   if (!surface)
      return NULL;
   pipe_reference_init(&surface->reference, 1);
   pipe_resource_reference(&surface->texture, texture);
   surface->context = ctx;
   surface->format = surf_tmpl->format;
   surface->width = texture->width0;
   surface->height = texture->height0;
   surface->texture = texture;
   surface->u.tex.first_layer = surf_tmpl->u.tex.first_layer;
   surface->u.tex.last_layer = surf_tmpl->u.tex.last_layer;
   surface->u.tex.level = surf_tmpl->u.tex.level;

   return surface;
}

static void
agx_set_clip_state(struct pipe_context *ctx,
                   const struct pipe_clip_state *state)
{
}

static void
agx_set_polygon_stipple(struct pipe_context *ctx,
                        const struct pipe_poly_stipple *state)
{
}

static void
agx_set_sample_mask(struct pipe_context *pipe, unsigned sample_mask)
{
   struct agx_context *ctx = agx_context(pipe);
   ctx->sample_mask = sample_mask;
}

static void
agx_set_scissor_states(struct pipe_context *pctx,
                       unsigned start_slot,
                       unsigned num_scissors,
                       const struct pipe_scissor_state *scissor)
{
   struct agx_context *ctx = agx_context(pctx);

   assert(start_slot == 0 && "no geometry shaders");
   assert(num_scissors == 1 && "no geometry shaders");

   ctx->scissor = *scissor;
   ctx->dirty |= AGX_DIRTY_SCISSOR;
}

static void
agx_set_stencil_ref(struct pipe_context *pctx,
                    const struct pipe_stencil_ref state)
{
   struct agx_context *ctx = agx_context(pctx);
   ctx->stencil_ref = state;
}

static void
agx_set_viewport_states(struct pipe_context *pctx,
                        unsigned start_slot,
                        unsigned num_viewports,
                        const struct pipe_viewport_state *vp)
{
   struct agx_context *ctx = agx_context(pctx);

   assert(start_slot == 0 && "no geometry shaders");
   assert(num_viewports == 1 && "no geometry shaders");

   ctx->dirty |= AGX_DIRTY_VIEWPORT;
   ctx->viewport = *vp;
}

struct agx_viewport_scissor {
   uint64_t viewport;
   unsigned scissor;
};

static struct agx_viewport_scissor
agx_upload_viewport_scissor(struct agx_pool *pool,
                            struct agx_batch *batch,
                            const struct pipe_viewport_state *vp,
                            const struct pipe_scissor_state *ss)
{
   struct agx_ptr T = agx_pool_alloc_aligned(pool, AGX_VIEWPORT_LENGTH, 64);

   float trans_x = vp->translate[0], trans_y = vp->translate[1];
   float abs_scale_x = fabsf(vp->scale[0]), abs_scale_y = fabsf(vp->scale[1]);

   /* Calculate the extent of the viewport. Note if a particular dimension of
    * the viewport is an odd number of pixels, both the translate and the scale
    * will have a fractional part of 0.5, so adding and subtracting them yields
    * an integer. Therefore we don't need to round explicitly */
   unsigned minx = CLAMP((int) (trans_x - abs_scale_x), 0, batch->width);
   unsigned miny = CLAMP((int) (trans_y - abs_scale_y), 0, batch->height);
   unsigned maxx = CLAMP((int) (trans_x + abs_scale_x), 0, batch->width);
   unsigned maxy = CLAMP((int) (trans_y + abs_scale_y), 0, batch->height);

   if (ss) {
      minx = MAX2(ss->minx, minx);
      miny = MAX2(ss->miny, miny);
      maxx = MIN2(ss->maxx, maxx);
      maxy = MIN2(ss->maxy, maxy);
   }

   assert(maxx > minx && maxy > miny);

   float minz, maxz;
   util_viewport_zmin_zmax(vp, false, &minz, &maxz);

   agx_pack(T.cpu, VIEWPORT, cfg) {
      cfg.min_tile_x = minx / 32;
      cfg.min_tile_y = miny / 32;
      cfg.max_tile_x = DIV_ROUND_UP(maxx, 32);
      cfg.max_tile_y = DIV_ROUND_UP(maxy, 32);
      cfg.clip_tile = true;

      cfg.translate_x = vp->translate[0];
      cfg.translate_y = vp->translate[1];
      cfg.scale_x = vp->scale[0];
      cfg.scale_y = vp->scale[1];

      /* Assumes [0, 1] clip coordinates. If half-z is not in use, lower_half_z
       * is called to ensure this works. */
      cfg.translate_z = minz;
      cfg.scale_z = maxz - minz;
   };

   /* Allocate a new scissor descriptor */
   struct agx_scissor_packed *ptr = batch->scissor.bo->ptr.cpu;
   unsigned index = (batch->scissor.count++);

   agx_pack(ptr + index, SCISSOR, cfg) {
      cfg.min_x = minx;
      cfg.min_y = miny;
      cfg.min_z = minz;
      cfg.max_x = maxx;
      cfg.max_y = maxy;
      cfg.max_z = maxz;
   }

   return (struct agx_viewport_scissor) {
      .viewport = T.gpu,
      .scissor = index
   };
}

/* A framebuffer state can be reused across batches, so it doesn't make sense
 * to add surfaces to the BO list here. Instead we added them when flushing.
 */

static void
agx_set_framebuffer_state(struct pipe_context *pctx,
                          const struct pipe_framebuffer_state *state)
{
   struct agx_context *ctx = agx_context(pctx);

   if (!state)
      return;

   /* XXX: eliminate this flush with batch tracking logic */
   pctx->flush(pctx, NULL, 0);

   util_copy_framebuffer_state(&ctx->framebuffer, state);
   ctx->batch->width = state->width;
   ctx->batch->height = state->height;
   ctx->batch->nr_cbufs = state->nr_cbufs;
   ctx->batch->cbufs[0] = state->cbufs[0];
   ctx->batch->zsbuf = state->zsbuf;
   ctx->dirty = ~0;

   for (unsigned i = 0; i < state->nr_cbufs; ++i) {
      struct pipe_surface *surf = state->cbufs[i];
      struct agx_resource *tex = agx_resource(surf->texture);
      const struct util_format_description *desc =
         util_format_description(surf->format);

      agx_pack(ctx->render_target[i], RENDER_TARGET, cfg) {
         cfg.layout = agx_translate_layout(tex->modifier);
         cfg.format = agx_pixel_format[surf->format].hw;
         cfg.swizzle_r = agx_channel_from_pipe(desc->swizzle[0]);
         cfg.swizzle_g = agx_channel_from_pipe(desc->swizzle[1]);
         cfg.swizzle_b = agx_channel_from_pipe(desc->swizzle[2]);
         cfg.swizzle_a = agx_channel_from_pipe(desc->swizzle[3]);
         cfg.width = state->width;
         cfg.height = state->height;
         cfg.buffer = tex->bo->ptr.gpu;

         cfg.stride = (tex->modifier == DRM_FORMAT_MOD_LINEAR) ?
            (tex->slices[0].line_stride - 4) :
            AGX_RT_STRIDE_TILED;
      };
   }
}

/* Likewise constant buffers, textures, and samplers are handled in a common
 * per-draw path, with dirty tracking to reduce the costs involved.
 */

static void
agx_set_constant_buffer(struct pipe_context *pctx,
                        enum pipe_shader_type shader, uint index,
                        bool take_ownership,
                        const struct pipe_constant_buffer *cb)
{
   struct agx_context *ctx = agx_context(pctx);
   struct agx_stage *s = &ctx->stage[shader];

   util_copy_constant_buffer(&s->cb[index], cb, take_ownership);

   unsigned mask = (1 << index);

   if (cb)
      s->cb_mask |= mask;
   else
      s->cb_mask &= ~mask;
}

static void
agx_surface_destroy(struct pipe_context *ctx,
                    struct pipe_surface *surface)
{
   pipe_resource_reference(&surface->texture, NULL);
   FREE(surface);
}

static void
agx_delete_state(struct pipe_context *ctx, void *state)
{
   FREE(state);
}

/* BOs added to the batch in the uniform upload path */

static void
agx_set_vertex_buffers(struct pipe_context *pctx,
                       unsigned start_slot, unsigned count,
                       unsigned unbind_num_trailing_slots,
                       bool take_ownership,
                       const struct pipe_vertex_buffer *buffers)
{
   struct agx_context *ctx = agx_context(pctx);

   util_set_vertex_buffers_mask(ctx->vertex_buffers, &ctx->vb_mask, buffers,
                                start_slot, count, unbind_num_trailing_slots, take_ownership);

   ctx->dirty |= AGX_DIRTY_VERTEX;
}

static void *
agx_create_vertex_elements(struct pipe_context *ctx,
                           unsigned count,
                           const struct pipe_vertex_element *state)
{
   assert(count < AGX_MAX_ATTRIBS);

   struct agx_attribute *attribs = calloc(sizeof(*attribs), AGX_MAX_ATTRIBS);
   for (unsigned i = 0; i < count; ++i) {
      const struct pipe_vertex_element ve = state[i];

      const struct util_format_description *desc =
         util_format_description(ve.src_format);

      unsigned chan_size = desc->channel[0].size / 8;

      assert(chan_size == 1 || chan_size == 2 || chan_size == 4);
      assert(desc->nr_channels >= 1 && desc->nr_channels <= 4);
      assert((ve.src_offset & (chan_size - 1)) == 0);

      attribs[i] = (struct agx_attribute) {
         .buf = ve.vertex_buffer_index,
         .src_offset = ve.src_offset / chan_size,
         .nr_comps_minus_1 = desc->nr_channels - 1,
         .format = agx_vertex_format[ve.src_format],
         .divisor = ve.instance_divisor
      };
   }

   return attribs;
}

static void
agx_bind_vertex_elements_state(struct pipe_context *pctx, void *cso)
{
   struct agx_context *ctx = agx_context(pctx);
   ctx->attributes = cso;
   ctx->dirty |= AGX_DIRTY_VERTEX;
}

static uint32_t asahi_shader_key_hash(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct asahi_shader_key));
}

static bool asahi_shader_key_equal(const void *a, const void *b)
{
   return memcmp(a, b, sizeof(struct asahi_shader_key)) == 0;
}

static void *
agx_create_shader_state(struct pipe_context *pctx,
                        const struct pipe_shader_state *cso)
{
   struct agx_uncompiled_shader *so = CALLOC_STRUCT(agx_uncompiled_shader);

   if (!so)
      return NULL;

   so->base = *cso;

   if (cso->type == PIPE_SHADER_IR_NIR) {
      so->nir = cso->ir.nir;
   } else {
      assert(cso->type == PIPE_SHADER_IR_TGSI);
      so->nir = tgsi_to_nir(cso->tokens, pctx->screen, false);
   }

   so->variants = _mesa_hash_table_create(NULL, asahi_shader_key_hash, asahi_shader_key_equal);
   return so;
}

static bool
agx_update_shader(struct agx_context *ctx, struct agx_compiled_shader **out,
                  enum pipe_shader_type stage, struct asahi_shader_key *key)
{
   struct agx_uncompiled_shader *so = ctx->stage[stage].shader;
   assert(so != NULL);

   struct hash_entry *he = _mesa_hash_table_search(so->variants, key);

   if (he) {
      if ((*out) == he->data)
         return false;

      *out = he->data;
      return true;
   }

   struct agx_compiled_shader *compiled = CALLOC_STRUCT(agx_compiled_shader);
   struct util_dynarray binary;
   util_dynarray_init(&binary, NULL);

   nir_shader *nir = nir_shader_clone(NULL, so->nir);

   if (key->blend.blend_enable) {
      nir_lower_blend_options opts = {
         .format = { key->rt_formats[0] },
         .scalar_blend_const = true
      };

      memcpy(opts.rt, key->blend.rt, sizeof(opts.rt));
      NIR_PASS_V(nir, nir_lower_blend, opts);
   } else if (key->blend.logicop_enable) {
      nir_lower_blend_options opts = {
         .format = { key->rt_formats[0] },
         .logicop_enable = true,
         .logicop_func = key->blend.logicop_func,
      };

      NIR_PASS_V(nir, nir_lower_blend, opts);
   }

   if (stage == PIPE_SHADER_FRAGMENT)
      NIR_PASS_V(nir, nir_lower_fragcolor, key->nr_cbufs);

   agx_compile_shader_nir(nir, &key->base, &binary, &compiled->info);

   struct agx_varyings *varyings = &compiled->info.varyings;
   unsigned packed_varying_sz = (AGX_VARYING_HEADER_LENGTH + varyings->nr_descs * AGX_VARYING_LENGTH);
   uint8_t *packed_varyings = alloca(packed_varying_sz);

   agx_pack(packed_varyings, VARYING_HEADER, cfg) {
      cfg.triangle_slots = cfg.point_slots = varyings->nr_slots;
   }

   memcpy(packed_varyings + AGX_VARYING_HEADER_LENGTH, varyings->packed,
         varyings->nr_descs * AGX_VARYING_LENGTH);

   if (binary.size) {
      struct agx_device *dev = agx_device(ctx->base.screen);
      compiled->bo = agx_bo_create(dev,
                                   ALIGN_POT(binary.size, 256) + (3 * packed_varying_sz),
                                   AGX_MEMORY_TYPE_SHADER);
      memcpy(compiled->bo->ptr.cpu, binary.data, binary.size);


      /* TODO: Why is the varying descriptor duplicated 3x? */
      unsigned offs = ALIGN_POT(binary.size, 256);
      for (unsigned copy = 0; copy < 3; ++copy) {
         memcpy(((uint8_t *) compiled->bo->ptr.cpu) + offs, packed_varyings, packed_varying_sz);
         offs += packed_varying_sz;
      }

      compiled->varyings = compiled->bo->ptr.gpu + ALIGN_POT(binary.size, 256);
   }

   ralloc_free(nir);
   util_dynarray_fini(&binary);

   he = _mesa_hash_table_insert(so->variants, key, compiled);
   *out = he->data;
   return true;
}

static bool
agx_update_vs(struct agx_context *ctx)
{
   struct agx_vs_shader_key key = {
      .num_vbufs = util_last_bit(ctx->vb_mask),
      .clip_halfz = ctx->rast->base.clip_halfz,
   };

   memcpy(key.attributes, ctx->attributes,
          sizeof(key.attributes[0]) * AGX_MAX_ATTRIBS);

   u_foreach_bit(i, ctx->vb_mask) {
      key.vbuf_strides[i] = ctx->vertex_buffers[i].stride;
   }

   struct asahi_shader_key akey = {
      .base.vs = key
   };

   return agx_update_shader(ctx, &ctx->vs, PIPE_SHADER_VERTEX, &akey);
}

static bool
agx_update_fs(struct agx_context *ctx)
{
   struct asahi_shader_key key = {
      .nr_cbufs = ctx->batch->nr_cbufs,
   };

   for (unsigned i = 0; i < key.nr_cbufs; ++i) {
      struct pipe_surface *surf = ctx->batch->cbufs[i];

      if (surf) {
         enum pipe_format fmt = surf->format;
         key.rt_formats[i] = fmt;
         key.base.fs.tib_formats[i] = agx_pixel_format[fmt].internal;
      } else {
         key.rt_formats[i] = PIPE_FORMAT_NONE;
      }
   }

   memcpy(&key.blend, ctx->blend, sizeof(key.blend));

   return agx_update_shader(ctx, &ctx->fs, PIPE_SHADER_FRAGMENT, &key);
}

static void
agx_bind_shader_state(struct pipe_context *pctx, void *cso)
{
   if (!cso)
      return;

   struct agx_context *ctx = agx_context(pctx);
   struct agx_uncompiled_shader *so = cso;

   enum pipe_shader_type type = pipe_shader_type_from_mesa(so->nir->info.stage);
   ctx->stage[type].shader = so;
}

static void
agx_delete_compiled_shader(struct hash_entry *ent)
{
   struct agx_compiled_shader *so = ent->data;
   agx_bo_unreference(so->bo);
   FREE(so);
}

static void
agx_delete_shader_state(struct pipe_context *ctx,
                        void *cso)
{
   struct agx_uncompiled_shader *so = cso;
   _mesa_hash_table_destroy(so->variants, agx_delete_compiled_shader);
   free(so);
}

/* Pipeline consists of a sequence of binding commands followed by a set shader command */
static uint32_t
agx_build_pipeline(struct agx_context *ctx, struct agx_compiled_shader *cs, enum pipe_shader_type stage)
{
   /* Pipelines must be 64-byte aligned */
   struct agx_ptr ptr = agx_pool_alloc_aligned(&ctx->batch->pipeline_pool,
                        (16 * AGX_BIND_UNIFORM_LENGTH) + // XXX: correct sizes, break up at compile time
                        (ctx->stage[stage].texture_count * AGX_BIND_TEXTURE_LENGTH) +
                        (PIPE_MAX_SAMPLERS * AGX_BIND_SAMPLER_LENGTH) +
                        AGX_SET_SHADER_EXTENDED_LENGTH + 8,
                        64);

   uint8_t *record = ptr.cpu;

   /* There is a maximum number of half words we may push with a single
    * BIND_UNIFORM record, so split up the range to fit. We only need to call
    * agx_push_location once, however, which reduces the cost. */
   unsigned unif_records = 0;

   for (unsigned i = 0; i < cs->info.push_ranges; ++i) {
      struct agx_push push = cs->info.push[i];
      uint64_t buffer = agx_push_location(ctx, push, stage);
      unsigned halfs_per_record = 14;
      unsigned records = DIV_ROUND_UP(push.length, halfs_per_record);

      /* Ensure we don't overflow */
      unif_records += records;
      assert(unif_records < 16);

      for (unsigned j = 0; j < records; ++j) {
         agx_pack(record, BIND_UNIFORM, cfg) {
            cfg.start_halfs = push.base + (j * halfs_per_record);
            cfg.size_halfs = MIN2(push.length - (j * halfs_per_record), halfs_per_record);
            cfg.buffer = buffer + (j * halfs_per_record * 2);
         }

         record += AGX_BIND_UNIFORM_LENGTH;
      }
   }

   for (unsigned i = 0; i < ctx->stage[stage].texture_count; ++i) {
      struct agx_sampler_view *tex = ctx->stage[stage].textures[i];
      agx_batch_add_bo(ctx->batch, tex->desc);
      agx_batch_add_bo(ctx->batch, agx_resource(tex->base.texture)->bo);


      agx_pack(record, BIND_TEXTURE, cfg) {
         cfg.start = i;
         cfg.count = 1;
         cfg.buffer = tex->desc->ptr.gpu;
      }

      record += AGX_BIND_TEXTURE_LENGTH;
   }

   for (unsigned i = 0; i < PIPE_MAX_SAMPLERS; ++i) {
      struct agx_sampler_state *sampler = ctx->stage[stage].samplers[i];

      if (!sampler)
         continue;

      struct agx_bo *bo = sampler->desc;
      agx_batch_add_bo(ctx->batch, bo);

      agx_pack(record, BIND_SAMPLER, cfg) {
         cfg.start = i;
         cfg.count = 1;
         cfg.buffer = bo->ptr.gpu;
      }

      record += AGX_BIND_SAMPLER_LENGTH;
   }

   /* TODO: Can we prepack this? */
   if (stage == PIPE_SHADER_FRAGMENT) {
      agx_pack(record, SET_SHADER_EXTENDED, cfg) {
         cfg.code = cs->bo->ptr.gpu;
         cfg.register_quadwords = 0;
         cfg.unk_3 = 0x8d;
         cfg.unk_1 = 0x2010bd;
         cfg.unk_2 = 0x0d;
         cfg.unk_2b = 1;
         cfg.unk_3b = 0x1;
         cfg.unk_4 = 0x800;
         cfg.preshader_unk = 0xc080;
         cfg.spill_size = 0x2;
      }

      record += AGX_SET_SHADER_EXTENDED_LENGTH;
   } else {
      agx_pack(record, SET_SHADER, cfg) {
         cfg.code = cs->bo->ptr.gpu;
         cfg.register_quadwords = 0;
         cfg.unk_2b = cs->info.varyings.nr_slots;
         cfg.unk_2 = 0x0d;
      }

      record += AGX_SET_SHADER_LENGTH;
   }

   /* End pipeline */
   memset(record, 0, 8);
   assert(ptr.gpu < (1ull << 32));
   return ptr.gpu;
}

/* Internal pipelines (TODO: refactor?) */
uint64_t
agx_build_clear_pipeline(struct agx_context *ctx, uint32_t code, uint64_t clear_buf)
{
   struct agx_ptr ptr = agx_pool_alloc_aligned(&ctx->batch->pipeline_pool,
                        (1 * AGX_BIND_UNIFORM_LENGTH) +
                        AGX_SET_SHADER_EXTENDED_LENGTH + 8,
                        64);

   uint8_t *record = ptr.cpu;

   agx_pack(record, BIND_UNIFORM, cfg) {
      cfg.start_halfs = (6 * 2);
      cfg.size_halfs = 4;
      cfg.buffer = clear_buf;
   }

   record += AGX_BIND_UNIFORM_LENGTH;

   /* TODO: Can we prepack this? */
   agx_pack(record, SET_SHADER_EXTENDED, cfg) {
      cfg.code = code;
      cfg.register_quadwords = 1;
      cfg.unk_3 = 0x8d;
      cfg.unk_2 = 0x0d;
      cfg.unk_2b = 4;
      cfg.frag_unk = 0x880100;
      cfg.preshader_mode = 0; // XXX
   }

   record += AGX_SET_SHADER_EXTENDED_LENGTH;

   /* End pipeline */
   memset(record, 0, 8);
   return ptr.gpu;
}

uint64_t
agx_build_reload_pipeline(struct agx_context *ctx, uint32_t code, struct pipe_surface *surf)
{
   struct agx_ptr ptr = agx_pool_alloc_aligned(&ctx->batch->pipeline_pool,
                        (1 * AGX_BIND_TEXTURE_LENGTH) +
                        (1 * AGX_BIND_SAMPLER_LENGTH) +
                        AGX_SET_SHADER_EXTENDED_LENGTH + 8,
                        64);

   uint8_t *record = ptr.cpu;
   struct agx_ptr sampler = agx_pool_alloc_aligned(&ctx->batch->pool, AGX_SAMPLER_LENGTH, 64);
   struct agx_ptr texture = agx_pool_alloc_aligned(&ctx->batch->pool, AGX_TEXTURE_LENGTH, 64);

   agx_pack(sampler.cpu, SAMPLER, cfg) {
      cfg.magnify_linear = true;
      cfg.minify_linear = false;
      cfg.mip_filter = AGX_MIP_FILTER_NONE;
      cfg.wrap_s = AGX_WRAP_CLAMP_TO_EDGE;
      cfg.wrap_t = AGX_WRAP_CLAMP_TO_EDGE;
      cfg.wrap_r = AGX_WRAP_CLAMP_TO_EDGE;
      cfg.pixel_coordinates = true;
      cfg.compare_func = AGX_COMPARE_FUNC_ALWAYS;
      cfg.unk_2 = 0;
      cfg.unk_3 = 0;
   }

   agx_pack(texture.cpu, TEXTURE, cfg) {
      struct agx_resource *rsrc = agx_resource(surf->texture);
      const struct util_format_description *desc =
         util_format_description(surf->format);

      cfg.layout = agx_translate_layout(rsrc->modifier);
      cfg.format = agx_pixel_format[surf->format].hw;
      cfg.swizzle_r = agx_channel_from_pipe(desc->swizzle[0]);
      cfg.swizzle_g = agx_channel_from_pipe(desc->swizzle[1]);
      cfg.swizzle_b = agx_channel_from_pipe(desc->swizzle[2]);
      cfg.swizzle_a = agx_channel_from_pipe(desc->swizzle[3]);
      cfg.width = surf->width;
      cfg.height = surf->height;
      cfg.levels = 1;
      cfg.srgb = (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);
      cfg.address = rsrc->bo->ptr.gpu;
      cfg.unk_2 = false;

      cfg.stride = (rsrc->modifier == DRM_FORMAT_MOD_LINEAR) ?
         (rsrc->slices[0].line_stride - 16) :
         AGX_RT_STRIDE_TILED;
   }

   agx_pack(record, BIND_TEXTURE, cfg) {
      cfg.start = 0;
      cfg.count = 1;
      cfg.buffer = texture.gpu;
   }

   record += AGX_BIND_TEXTURE_LENGTH;

   agx_pack(record, BIND_SAMPLER, cfg) {
      cfg.start = 0;
      cfg.count = 1;
      cfg.buffer = sampler.gpu;
   }

   record += AGX_BIND_SAMPLER_LENGTH;

   /* TODO: Can we prepack this? */
   agx_pack(record, SET_SHADER_EXTENDED, cfg) {
      cfg.code = code;
      cfg.register_quadwords = 0;
      cfg.unk_3 = 0x8d;
      cfg.unk_2 = 0x0d;
      cfg.unk_2b = 4;
      cfg.unk_4 = 0;
      cfg.frag_unk = 0x880100;
      cfg.preshader_mode = 0; // XXX
   }

   record += AGX_SET_SHADER_EXTENDED_LENGTH;

   /* End pipeline */
   memset(record, 0, 8);
   return ptr.gpu;
}

uint64_t
agx_build_store_pipeline(struct agx_context *ctx, uint32_t code,
                         uint64_t render_target)
{
   struct agx_ptr ptr = agx_pool_alloc_aligned(&ctx->batch->pipeline_pool,
                        (1 * AGX_BIND_TEXTURE_LENGTH) +
                        (1 * AGX_BIND_UNIFORM_LENGTH) +
                        AGX_SET_SHADER_EXTENDED_LENGTH + 8,
                        64);

   uint8_t *record = ptr.cpu;

   agx_pack(record, BIND_TEXTURE, cfg) {
      cfg.start = 0;
      cfg.count = 1;
      cfg.buffer = render_target;
   }

   record += AGX_BIND_TEXTURE_LENGTH;

   uint32_t unk[] = { 0, ~0 };

   agx_pack(record, BIND_UNIFORM, cfg) {
      cfg.start_halfs = 4;
      cfg.size_halfs = 4;
      cfg.buffer = agx_pool_upload_aligned(&ctx->batch->pool, unk, sizeof(unk), 16);
   }

   record += AGX_BIND_UNIFORM_LENGTH;

   /* TODO: Can we prepack this? */
   agx_pack(record, SET_SHADER_EXTENDED, cfg) {
      cfg.code = code;
      cfg.register_quadwords = 1;
      cfg.unk_2 = 0xd;
      cfg.unk_3 = 0x8d;
      cfg.frag_unk = 0x880100;
      cfg.preshader_mode = 0; // XXX
   }

   record += AGX_SET_SHADER_EXTENDED_LENGTH;

   /* End pipeline */
   memset(record, 0, 8);
   return ptr.gpu;
}

static uint64_t
demo_launch_fragment(struct agx_context *ctx, struct agx_pool *pool, uint32_t pipeline, uint32_t varyings, unsigned input_count)
{
   struct agx_ptr t = agx_pool_alloc_aligned(pool, AGX_BIND_PIPELINE_LENGTH, 64);

   agx_pack(t.cpu, BIND_PIPELINE, cfg) {
      cfg.tag = AGX_BIND_PIPELINE_FRAGMENT;
      cfg.sampler_count = ctx->stage[PIPE_SHADER_FRAGMENT].texture_count;
      cfg.texture_count = ctx->stage[PIPE_SHADER_FRAGMENT].texture_count;
      cfg.input_count = input_count;
      cfg.pipeline = pipeline;
      cfg.fs_varyings = varyings;
   };

   return t.gpu;
}

static uint64_t
demo_interpolation(struct agx_compiled_shader *fs, struct agx_pool *pool)
{
   struct agx_ptr t = agx_pool_alloc_aligned(pool, AGX_INTERPOLATION_LENGTH, 64);

   agx_pack(t.cpu, INTERPOLATION, cfg) {
      cfg.varying_count = fs->info.varyings.nr_slots;
   };

   return t.gpu;
}

static uint64_t
demo_linkage(struct agx_compiled_shader *vs, struct agx_pool *pool)
{
   struct agx_ptr t = agx_pool_alloc_aligned(pool, AGX_LINKAGE_LENGTH, 64);

   agx_pack(t.cpu, LINKAGE, cfg) {
      cfg.varying_count = vs->info.varyings.nr_slots;

      // 0x2 for fragcoordz, 0x1 for varyings at all
      cfg.unk_1 = 0x210000 | (vs->info.writes_psiz ? 0x40000 : 0);
   };

   return t.gpu;
}

static uint64_t
demo_rasterizer(struct agx_context *ctx, struct agx_pool *pool, bool is_points)
{
   struct agx_rasterizer *rast = ctx->rast;
   struct agx_rasterizer_packed out;

   agx_pack(&out, RASTERIZER, cfg) {
      bool back_stencil = ctx->zs.base.stencil[1].enabled;
      cfg.front.stencil_reference = ctx->stencil_ref.ref_value[0];
      cfg.back.stencil_reference = back_stencil ?
         ctx->stencil_ref.ref_value[1] :
         cfg.front.stencil_reference;

      cfg.front.line_width = cfg.back.line_width = rast->line_width;
      cfg.front.polygon_mode = cfg.back.polygon_mode = AGX_POLYGON_MODE_FILL;

      cfg.unk_fill_lines = is_points; /* XXX: what is this? */

      /* Always enable scissoring so we may scissor to the viewport (TODO:
       * optimize this out if the viewport is the default and the app does not
       * use the scissor test) */
      cfg.scissor_enable = true;
   };

   /* Words 2-3: front */
   out.opaque[2] |= ctx->zs.front.opaque[0];
   out.opaque[3] |= ctx->zs.front.opaque[1];

   /* Words 4-5: back */
   out.opaque[4] |= ctx->zs.back.opaque[0];
   out.opaque[5] |= ctx->zs.back.opaque[1];

   return agx_pool_upload_aligned(pool, &out, sizeof(out), 64);
}

static uint64_t
demo_unk11(struct agx_pool *pool, bool prim_lines, bool prim_points, bool reads_tib)
{
#define UNK11_FILL_MODE_LINES_1 (1 << 26)

#define UNK11_FILL_MODE_LINES_2 (0x5004 << 16)
#define UNK11_LINES (0x10000000)
#define UNK11_POINTS (0x40000000)

#define UNK11_READS_TIB (0x20000000)

   uint32_t unk[] = {
      0x200004a,
      0x200 | ((prim_lines || prim_points) ? UNK11_FILL_MODE_LINES_1 : 0) | (reads_tib ? UNK11_READS_TIB : 0),
      0x7e00000 | (prim_lines ? UNK11_LINES : 0) | (prim_points ? UNK11_POINTS : 0),
      0x7e00000 | (prim_lines ? UNK11_LINES : 0) | (prim_points ? UNK11_POINTS : 0),

      0x1ffff
   };

   return agx_pool_upload(pool, unk, sizeof(unk));
}

static uint64_t
demo_unk12(struct agx_pool *pool)
{
   uint32_t unk[] = {
      0x410000,
      0x1e3ce508,
      0xa0
   };

   return agx_pool_upload(pool, unk, sizeof(unk));
}

static uint64_t
agx_set_scissor_index(struct agx_pool *pool, unsigned index)
{
   struct agx_ptr T = agx_pool_alloc_aligned(pool, AGX_SET_SCISSOR_LENGTH, 64);

   agx_pack(T.cpu, SET_SCISSOR, cfg) {
      cfg.index = index;
   };

   return T.gpu;
}

static void
agx_push_record(uint8_t **out, unsigned size_words, uint64_t ptr)
{
   assert(ptr < (1ull << 40));
   assert(size_words < (1ull << 24));

   uint64_t value = (size_words | (ptr << 24));
   memcpy(*out, &value, sizeof(value));
   *out += sizeof(value);
}

static uint8_t *
agx_encode_state(struct agx_context *ctx, uint8_t *out,
                 uint32_t pipeline_vertex, uint32_t pipeline_fragment, uint32_t varyings,
                 bool is_lines, bool is_points)
{
   agx_pack(out, BIND_PIPELINE, cfg) {
      cfg.tag = AGX_BIND_PIPELINE_VERTEX;
      cfg.pipeline = pipeline_vertex;
      cfg.vs_output_count_1 = ctx->vs->info.varyings.nr_slots;
      cfg.vs_output_count_2 = ctx->vs->info.varyings.nr_slots;
      cfg.sampler_count = ctx->stage[PIPE_SHADER_VERTEX].texture_count;
      cfg.texture_count = ctx->stage[PIPE_SHADER_VERTEX].texture_count;
   }

   /* yes, it's really 17 bytes */
   out += AGX_BIND_PIPELINE_LENGTH;
   *(out++) = 0x0;

   struct agx_pool *pool = &ctx->batch->pool;
   struct agx_ptr zero = agx_pool_alloc_aligned(pool, 16, 256);
   memset(zero.cpu, 0, 16);

   bool reads_tib = ctx->fs->info.reads_tib;

   agx_push_record(&out, 0, zero.gpu);
   agx_push_record(&out, 5, demo_interpolation(ctx->fs, pool));
   agx_push_record(&out, 5, demo_launch_fragment(ctx, pool, pipeline_fragment, varyings, ctx->fs->info.varyings.nr_descs));
   agx_push_record(&out, 4, demo_linkage(ctx->vs, pool));
   agx_push_record(&out, 7, demo_rasterizer(ctx, pool, is_points));
   agx_push_record(&out, 5, demo_unk11(pool, is_lines, is_points, reads_tib));

   if (ctx->dirty & (AGX_DIRTY_VIEWPORT | AGX_DIRTY_SCISSOR)) {
      struct agx_viewport_scissor vps = agx_upload_viewport_scissor(pool,
            ctx->batch, &ctx->viewport,
            ctx->rast->base.scissor ? &ctx->scissor : NULL);

      agx_push_record(&out, 10, vps.viewport);
      agx_push_record(&out, 2, agx_set_scissor_index(pool, vps.scissor));
   }

   agx_push_record(&out, 3, demo_unk12(pool));
   agx_push_record(&out, 2, agx_pool_upload(pool, ctx->rast->cull, sizeof(ctx->rast->cull)));

   return (out - 1); // XXX: alignment fixup, or something
}

static enum agx_primitive
agx_primitive_for_pipe(enum pipe_prim_type mode)
{
   switch (mode) {
   case PIPE_PRIM_POINTS: return AGX_PRIMITIVE_POINTS;
   case PIPE_PRIM_LINES: return AGX_PRIMITIVE_LINES;
   case PIPE_PRIM_LINE_STRIP: return AGX_PRIMITIVE_LINE_STRIP;
   case PIPE_PRIM_LINE_LOOP: return AGX_PRIMITIVE_LINE_LOOP;
   case PIPE_PRIM_TRIANGLES: return AGX_PRIMITIVE_TRIANGLES;
   case PIPE_PRIM_TRIANGLE_STRIP: return AGX_PRIMITIVE_TRIANGLE_STRIP;
   case PIPE_PRIM_TRIANGLE_FAN: return AGX_PRIMITIVE_TRIANGLE_FAN;
   case PIPE_PRIM_QUADS: return AGX_PRIMITIVE_QUADS;
   case PIPE_PRIM_QUAD_STRIP: return AGX_PRIMITIVE_QUAD_STRIP;
   default: unreachable("todo: other primitive types");
   }
}

static uint64_t
agx_index_buffer_ptr(struct agx_batch *batch,
                     const struct pipe_draw_start_count_bias *draw,
                     const struct pipe_draw_info *info)
{
   off_t offset = draw->start * info->index_size;

   if (!info->has_user_indices) {
      struct agx_bo *bo = agx_resource(info->index.resource)->bo;
      agx_batch_add_bo(batch, bo);

      return bo->ptr.gpu + offset;
   } else {
      return agx_pool_upload_aligned(&batch->pool,
                                     ((uint8_t *) info->index.user) + offset,
                                     draw->count * info->index_size, 64);
   }
}

static bool
agx_scissor_culls_everything(struct agx_context *ctx)
{
        const struct pipe_scissor_state ss = ctx->scissor;

        return ctx->rast->base.scissor &&
		((ss.minx == ss.maxx) || (ss.miny == ss.maxy));
}

static void
agx_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info,
             unsigned drawid_offset,
             const struct pipe_draw_indirect_info *indirect,
             const struct pipe_draw_start_count_bias *draws,
             unsigned num_draws)
{
   if (num_draws > 1) {
      util_draw_multi(pctx, info, drawid_offset, indirect, draws, num_draws);
      return;
   }

   if (info->index_size && draws->index_bias)
      unreachable("todo: index bias");

   struct agx_context *ctx = agx_context(pctx);
   struct agx_batch *batch = ctx->batch;

   if (agx_scissor_culls_everything(ctx))
	   return;

   /* TODO: masks */
   ctx->batch->draw |= ~0;

   /* TODO: Dirty track */
   agx_update_vs(ctx);
   agx_update_fs(ctx);

   agx_batch_add_bo(batch, ctx->vs->bo);
   agx_batch_add_bo(batch, ctx->fs->bo);

   bool is_lines =
      (info->mode == PIPE_PRIM_LINES) ||
      (info->mode == PIPE_PRIM_LINE_STRIP) ||
      (info->mode == PIPE_PRIM_LINE_LOOP);

   uint8_t *out = agx_encode_state(ctx, batch->encoder_current,
                                   agx_build_pipeline(ctx, ctx->vs, PIPE_SHADER_VERTEX),
                                   agx_build_pipeline(ctx, ctx->fs, PIPE_SHADER_FRAGMENT),
                                   ctx->fs->varyings, is_lines, info->mode == PIPE_PRIM_POINTS);

   enum agx_primitive prim = agx_primitive_for_pipe(info->mode);
   unsigned idx_size = info->index_size;

   if (idx_size) {
      uint64_t ib = agx_index_buffer_ptr(batch, draws, info);

      /* Index sizes are encoded logarithmically */
      STATIC_ASSERT(__builtin_ctz(1) == AGX_INDEX_SIZE_U8);
      STATIC_ASSERT(__builtin_ctz(2) == AGX_INDEX_SIZE_U16);
      STATIC_ASSERT(__builtin_ctz(4) == AGX_INDEX_SIZE_U32);
      assert((idx_size == 1) || (idx_size == 2) || (idx_size == 4));

      agx_pack(out, INDEXED_DRAW, cfg) {
         cfg.restart_index = info->restart_index;
         cfg.unk_2a = (ib >> 32);
         cfg.primitive = prim;
         cfg.restart_enable = info->primitive_restart;
         cfg.index_size = __builtin_ctz(idx_size);
         cfg.index_buffer_offset = (ib & BITFIELD_MASK(32));
         cfg.index_buffer_size = ALIGN_POT(draws->count * idx_size, 4);
         cfg.index_count = draws->count;
         cfg.instance_count = info->instance_count;
         cfg.base_vertex = draws->index_bias;
      };

      out += AGX_INDEXED_DRAW_LENGTH;
   } else {
      agx_pack(out, DRAW, cfg) {
         cfg.primitive = prim;
         cfg.vertex_start = draws->start;
         cfg.vertex_count = draws->count;
         cfg.instance_count = info->instance_count;
      };

      out += AGX_DRAW_LENGTH;
   }

   batch->encoder_current = out;
   ctx->dirty = 0;
}

void agx_init_state_functions(struct pipe_context *ctx);

void
agx_init_state_functions(struct pipe_context *ctx)
{
   ctx->create_blend_state = agx_create_blend_state;
   ctx->create_depth_stencil_alpha_state = agx_create_zsa_state;
   ctx->create_fs_state = agx_create_shader_state;
   ctx->create_rasterizer_state = agx_create_rs_state;
   ctx->create_sampler_state = agx_create_sampler_state;
   ctx->create_sampler_view = agx_create_sampler_view;
   ctx->create_surface = agx_create_surface;
   ctx->create_vertex_elements_state = agx_create_vertex_elements;
   ctx->create_vs_state = agx_create_shader_state;
   ctx->bind_blend_state = agx_bind_blend_state;
   ctx->bind_depth_stencil_alpha_state = agx_bind_zsa_state;
   ctx->bind_sampler_states = agx_bind_sampler_states;
   ctx->bind_fs_state = agx_bind_shader_state;
   ctx->bind_rasterizer_state = agx_bind_rasterizer_state;
   ctx->bind_vertex_elements_state = agx_bind_vertex_elements_state;
   ctx->bind_vs_state = agx_bind_shader_state;
   ctx->delete_blend_state = agx_delete_state;
   ctx->delete_depth_stencil_alpha_state = agx_delete_state;
   ctx->delete_fs_state = agx_delete_shader_state;
   ctx->delete_rasterizer_state = agx_delete_state;
   ctx->delete_sampler_state = agx_delete_sampler_state;
   ctx->delete_vertex_elements_state = agx_delete_state;
   ctx->delete_vs_state = agx_delete_state;
   ctx->set_blend_color = agx_set_blend_color;
   ctx->set_clip_state = agx_set_clip_state;
   ctx->set_constant_buffer = agx_set_constant_buffer;
   ctx->set_sampler_views = agx_set_sampler_views;
   ctx->set_framebuffer_state = agx_set_framebuffer_state;
   ctx->set_polygon_stipple = agx_set_polygon_stipple;
   ctx->set_sample_mask = agx_set_sample_mask;
   ctx->set_scissor_states = agx_set_scissor_states;
   ctx->set_stencil_ref = agx_set_stencil_ref;
   ctx->set_vertex_buffers = agx_set_vertex_buffers;
   ctx->set_viewport_states = agx_set_viewport_states;
   ctx->sampler_view_destroy = agx_sampler_view_destroy;
   ctx->surface_destroy = agx_surface_destroy;
   ctx->draw_vbo = agx_draw_vbo;
   ctx->create_stream_output_target = agx_create_stream_output_target;
   ctx->stream_output_target_destroy = agx_stream_output_target_destroy;
   ctx->set_stream_output_targets = agx_set_stream_output_targets;
}
