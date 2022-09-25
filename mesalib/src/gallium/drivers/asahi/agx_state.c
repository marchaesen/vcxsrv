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
#include "util/u_prim.h"
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
#include "asahi/lib/agx_ppp.h"

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

   ctx->stage[PIPE_SHADER_FRAGMENT].dirty = ~0;
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
agx_pack_stencil(struct agx_fragment_stencil_packed *out,
                 struct pipe_stencil_state st)
{
   if (st.enabled) {
      agx_pack(out, FRAGMENT_STENCIL, cfg) {
         cfg.compare = (enum agx_zs_func) st.func;
         cfg.write_mask = st.writemask;
         cfg.read_mask = st.valuemask;

         cfg.depth_pass   = agx_stencil_ops[st.zpass_op];
         cfg.depth_fail   = agx_stencil_ops[st.zfail_op];
         cfg.stencil_fail = agx_stencil_ops[st.fail_op];
      }
   } else {
      agx_pack(out, FRAGMENT_STENCIL, cfg) {
         cfg.compare = AGX_ZS_FUNC_ALWAYS;
         cfg.write_mask = 0xFF;
         cfg.read_mask = 0xFF;

         cfg.depth_pass = AGX_STENCIL_OP_KEEP;
         cfg.depth_fail = AGX_STENCIL_OP_KEEP;
         cfg.stencil_fail = AGX_STENCIL_OP_KEEP;
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

   agx_pack(&so->depth, FRAGMENT_FACE, cfg) {
      cfg.depth_function = state->depth_enabled ?
         ((enum agx_zs_func) state->depth_func) : AGX_ZS_FUNC_ALWAYS;

      cfg.disable_depth_write = !state->depth_writemask;
   }

   agx_pack_stencil(&so->front_stencil, state->stencil[0]);

   if (state->stencil[1].enabled) {
      agx_pack_stencil(&so->back_stencil, state->stencil[1]);
   } else {
      /* One sided stencil */
      so->back_stencil = so->front_stencil;
   }

   return so;
}

static void
agx_bind_zsa_state(struct pipe_context *pctx, void *cso)
{
   struct agx_context *ctx = agx_context(pctx);
   ctx->zs = cso;
   ctx->dirty |= AGX_DIRTY_ZS;
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

   bool base_cso_changed = (cso == NULL) || (ctx->rast == NULL);

   /* Check if scissor or depth bias state has changed, since scissor/depth bias
    * enable is part of the rasterizer state but everything else needed for
    * scissors and depth bias is part of the scissor/depth bias arrays */
   bool scissor_zbias_changed = base_cso_changed ||
      (ctx->rast->base.scissor != so->base.scissor) ||
      (ctx->rast->base.offset_tri != so->base.offset_tri);

   ctx->rast = so;
   ctx->dirty |= AGX_DIRTY_RS;

   if (scissor_zbias_changed)
      ctx->dirty |= AGX_DIRTY_SCISSOR_ZBIAS;

   if (base_cso_changed || (ctx->rast->base.sprite_coord_mode !=
                            so->base.sprite_coord_mode))
      ctx->dirty |= AGX_DIRTY_SPRITE_COORD_MODE;
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
   struct agx_sampler_state *so = CALLOC_STRUCT(agx_sampler_state);
   so->base = *state;

   assert(state->lod_bias == 0 && "todo: lod bias");

   agx_pack(&so->desc, SAMPLER, cfg) {
      cfg.minimum_lod = state->min_lod;
      cfg.maximum_lod = state->max_lod;
      cfg.magnify_linear = (state->mag_img_filter == PIPE_TEX_FILTER_LINEAR);
      cfg.minify_linear = (state->min_img_filter == PIPE_TEX_FILTER_LINEAR);
      cfg.mip_filter = agx_mip_filter_from_pipe(state->min_mip_filter);
      cfg.wrap_s = agx_wrap_from_pipe(state->wrap_s);
      cfg.wrap_t = agx_wrap_from_pipe(state->wrap_t);
      cfg.wrap_r = agx_wrap_from_pipe(state->wrap_r);
      cfg.pixel_coordinates = !state->normalized_coords;
      cfg.compare_func = agx_compare_funcs[state->compare_func];
   }


   return so;
}

static void
agx_delete_sampler_state(struct pipe_context *ctx, void *state)
{
   struct agx_sampler_state *so = state;
   FREE(so);
}

static void
agx_bind_sampler_states(struct pipe_context *pctx,
                        enum pipe_shader_type shader,
                        unsigned start, unsigned count,
                        void **states)
{
   struct agx_context *ctx = agx_context(pctx);

   ctx->stage[shader].sampler_count = states ? count : 0;
   ctx->stage[shader].dirty = ~0;

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
   case DRM_FORMAT_MOD_APPLE_TWIDDLED:
      return AGX_LAYOUT_TWIDDLED;
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
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D: return AGX_TEXTURE_DIMENSION_2D;
   case PIPE_TEXTURE_2D_ARRAY: return AGX_TEXTURE_DIMENSION_2D_ARRAY;
   case PIPE_TEXTURE_3D: return AGX_TEXTURE_DIMENSION_3D;
   case PIPE_TEXTURE_CUBE: return AGX_TEXTURE_DIMENSION_CUBE;
   default: unreachable("Unsupported texture dimension");
   }
}

static struct pipe_sampler_view *
agx_create_sampler_view(struct pipe_context *pctx,
                        struct pipe_resource *texture,
                        const struct pipe_sampler_view *state)
{
   struct agx_resource *rsrc = agx_resource(texture);
   struct agx_sampler_view *so = CALLOC_STRUCT(agx_sampler_view);

   if (!so)
      return NULL;

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

   assert(state->u.tex.first_layer == 0);

   /* Must tile array textures */
   assert((rsrc->modifier != DRM_FORMAT_MOD_LINEAR) ||
          (state->u.tex.last_layer == state->u.tex.first_layer));

   /* Pack the descriptor into GPU memory */
   agx_pack(&so->desc, TEXTURE, cfg) {
      cfg.dimension = agx_translate_texture_dimension(state->target);
      cfg.layout = agx_translate_layout(rsrc->modifier);
      cfg.format = agx_pixel_format[state->format].hw;
      cfg.swizzle_r = agx_channel_from_pipe(out_swizzle[0]);
      cfg.swizzle_g = agx_channel_from_pipe(out_swizzle[1]);
      cfg.swizzle_b = agx_channel_from_pipe(out_swizzle[2]);
      cfg.swizzle_a = agx_channel_from_pipe(out_swizzle[3]);
      cfg.width = texture->width0;
      cfg.height = texture->height0;
      cfg.first_level = state->u.tex.first_level;
      cfg.last_level = state->u.tex.last_level;
      cfg.srgb = (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);
      cfg.address = agx_map_texture_gpu(rsrc, state->u.tex.first_layer);
      cfg.unk_mipmapped = rsrc->mipmapped;
      cfg.srgb_2_channel = cfg.srgb && util_format_colormask(desc) == 0x3;

      if (state->target == PIPE_TEXTURE_3D) {
         cfg.depth = texture->depth0;
      } else {
         unsigned layers = state->u.tex.last_layer - state->u.tex.first_layer + 1;

         if ((state->target == PIPE_TEXTURE_CUBE) || (state->target == PIPE_TEXTURE_CUBE_ARRAY))
            layers /= 6;

         cfg.depth = layers;
      }

      if (rsrc->modifier == DRM_FORMAT_MOD_LINEAR) {
         cfg.stride = ail_get_linear_stride_B(&rsrc->layout, 0) - 16;
      } else {
         assert(rsrc->modifier == DRM_FORMAT_MOD_APPLE_TWIDDLED);
         cfg.unk_tiled = true;
      }
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
   ctx->stage[shader].dirty = ~0;
}

static void
agx_sampler_view_destroy(struct pipe_context *ctx,
                         struct pipe_sampler_view *pview)
{
   struct agx_sampler_view *view = (struct agx_sampler_view *) pview;
   pipe_resource_reference(&view->base.texture, NULL);
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
   ctx->dirty |= AGX_DIRTY_SCISSOR_ZBIAS;
}

static void
agx_set_stencil_ref(struct pipe_context *pctx,
                    const struct pipe_stencil_ref state)
{
   struct agx_context *ctx = agx_context(pctx);
   ctx->stencil_ref = state;
   ctx->dirty |= AGX_DIRTY_STENCIL_REF;
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

static void
agx_upload_viewport_scissor(struct agx_pool *pool,
                            struct agx_batch *batch,
                            uint8_t **out, 
                            const struct pipe_viewport_state *vp,
                            const struct pipe_scissor_state *ss,
                            unsigned zbias)
{
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

   /* Upload state */
   struct agx_ppp_update ppp = agx_new_ppp_update(pool, (struct AGX_PPP_HEADER) {
      .depth_bias_scissor = true,
      .region_clip = true,
      .viewport = true,
   });

   agx_ppp_push(&ppp, DEPTH_BIAS_SCISSOR, cfg) {
      cfg.scissor = index;
      cfg.depth_bias = zbias;
   };

   agx_ppp_push(&ppp, REGION_CLIP, cfg) {
      cfg.enable = true;
      cfg.min_x = minx / 32;
      cfg.min_y = miny / 32;
      cfg.max_x = DIV_ROUND_UP(maxx, 32);
      cfg.max_y = DIV_ROUND_UP(maxy, 32);
   }

   agx_ppp_push(&ppp, VIEWPORT, cfg) {
      cfg.translate_x = vp->translate[0];
      cfg.translate_y = vp->translate[1];
      cfg.translate_z = vp->translate[2];
      cfg.scale_x = vp->scale[0];
      cfg.scale_y = vp->scale[1];
      cfg.scale_z = vp->scale[2];
   }

   agx_ppp_fini(out, &ppp);
}

static uint16_t
agx_upload_depth_bias(struct agx_batch *batch,
                      const struct pipe_rasterizer_state *rast)
{
   struct agx_depth_bias_packed *ptr = batch->depth_bias.bo->ptr.cpu;
   unsigned index = (batch->depth_bias.count++);

   agx_pack(ptr + index, DEPTH_BIAS, cfg) {
      cfg.depth_bias    = rast->offset_units;
      cfg.slope_scale   = rast->offset_scale;
      cfg.clamp         = rast->offset_clamp;
   }

   return index;
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
   agx_flush_all(ctx, "Framebuffer switch");

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
      unsigned level = surf->u.tex.level;
      unsigned layer = surf->u.tex.first_layer;

      assert(surf->u.tex.last_layer == layer);

      agx_pack(ctx->render_target[i], RENDER_TARGET, cfg) {
         cfg.layout = agx_translate_layout(tex->modifier);
         cfg.format = agx_pixel_format[surf->format].hw;

         assert(desc->nr_channels >= 1 && desc->nr_channels <= 4);
         cfg.swizzle_r = agx_channel_from_pipe(desc->swizzle[0]);

         if (desc->nr_channels >= 2)
            cfg.swizzle_g = agx_channel_from_pipe(desc->swizzle[1]);

         if (desc->nr_channels >= 3)
            cfg.swizzle_b = agx_channel_from_pipe(desc->swizzle[2]);

         if (desc->nr_channels >= 4)
            cfg.swizzle_a = agx_channel_from_pipe(desc->swizzle[3]);

         cfg.width = state->width;
         cfg.height = state->height;
         cfg.level = surf->u.tex.level;
         cfg.buffer = agx_map_texture_gpu(tex, layer);

         if (tex->mipmapped)
            cfg.unk_55 = 0x8;

         if (tex->modifier == DRM_FORMAT_MOD_LINEAR) {
            cfg.stride = ail_get_linear_stride_B(&tex->layout, level) - 4;
         } else {
            cfg.unk_tiled = true;
            cfg.levels = tex->base.last_level + 1;
         }
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

   ctx->stage[shader].dirty = ~0;
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
   ctx->stage[PIPE_SHADER_VERTEX].dirty = ~0;
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

static unsigned
agx_find_linked_slot(struct agx_varyings_vs *vs, struct agx_varyings_fs *fs,
                     gl_varying_slot slot, unsigned offset)
{
   assert(offset < 4);
   assert(slot != VARYING_SLOT_PNTC && "point coords aren't linked");

   if (slot == VARYING_SLOT_POS) {
      if (offset == 3) {
         return 0; /* W */
      } else if (offset == 2) {
         assert(fs->reads_z);
         return 1; /* Z */
      } else {
         unreachable("gl_Position.xy are not varyings");
      }
   }

   unsigned vs_index = vs->slots[slot];

   assert(vs_index >= 4 && "gl_Position should have been the first 4 slots");
   assert(vs_index < vs->nr_index &&
         "varyings not written by vertex shader are undefined");
   assert((vs_index < vs->base_index_fp16) ==
          ((vs_index + offset) < vs->base_index_fp16) &&
          "a given varying must have a consistent type");

   unsigned vs_user_index = (vs_index + offset) - 4;

   if (fs->reads_z)
      return vs_user_index + 2;
   else
      return vs_user_index + 1;
}

static unsigned
agx_num_general_outputs(struct agx_varyings_vs *vs)
{
   unsigned nr_vs = vs->nr_index;
   bool writes_psiz = vs->slots[VARYING_SLOT_PSIZ] < nr_vs;

   assert(nr_vs >= 4 && "gl_Position must be written");
   if (writes_psiz)
      assert(nr_vs >= 5 && "gl_PointSize is written");

   return nr_vs - (writes_psiz ? 5 : 4);
}

static uint32_t
agx_link_varyings_vs_fs(struct agx_pool *pool, struct agx_varyings_vs *vs,
                        struct agx_varyings_fs *fs, bool first_provoking_vertex)
{
   /* If there are no bindings, there's nothing to emit */
   if (fs->nr_bindings == 0)
      return 0;

   size_t linkage_size = AGX_CF_BINDING_HEADER_LENGTH +
                         (fs->nr_bindings * AGX_CF_BINDING_LENGTH);

   void *tmp = alloca(linkage_size);
   struct agx_cf_binding_header_packed *header = tmp;
   struct agx_cf_binding_packed *bindings = (void *) (header + 1);

   unsigned nr_slots = agx_num_general_outputs(vs) + 1 + (fs->reads_z ? 1 : 0);

   agx_pack(header, CF_BINDING_HEADER, cfg) {
      cfg.number_of_32_bit_slots = nr_slots;
      cfg.number_of_coefficient_registers = fs->nr_cf;
   }

   for (unsigned i = 0; i < fs->nr_bindings; ++i) {
      agx_pack(bindings + i, CF_BINDING, cfg) {
         cfg.base_coefficient_register = fs->bindings[i].cf_base;
         cfg.components = fs->bindings[i].count;
         cfg.perspective = fs->bindings[i].perspective;
 
         cfg.shade_model = fs->bindings[i].smooth ? AGX_SHADE_MODEL_GOURAUD :
                           first_provoking_vertex ? AGX_SHADE_MODEL_FLAT_VERTEX_0 :
                                                    AGX_SHADE_MODEL_FLAT_VERTEX_2;

         if (fs->bindings[i].slot == VARYING_SLOT_PNTC) {
            assert(fs->bindings[i].offset == 0);
            cfg.point_sprite = true;
         } else {
            cfg.base_slot = agx_find_linked_slot(vs, fs, fs->bindings[i].slot,
                                                 fs->bindings[i].offset);

            assert(cfg.base_slot + cfg.components <= nr_slots &&
                   "overflow slots");
         }

         if (fs->bindings[i].slot == VARYING_SLOT_POS) {
             if (fs->bindings[i].offset == 2)
                cfg.fragcoord_z = true;
             else
                assert(!cfg.perspective && "W must not be perspective divided");
         }

         assert(cfg.base_coefficient_register + cfg.components <= fs->nr_cf &&
                "overflowed coefficient registers");
      }
   }

   struct agx_ptr ptr = agx_pool_alloc_aligned(pool, (3 * linkage_size), 256);
   assert(ptr.gpu < (1ull << 32) && "varyings must be in low memory");

   /* I don't understand why the data structures are repeated thrice */
   for (unsigned i = 0; i < 3; ++i) {
      memcpy(((uint8_t *) ptr.cpu) + (i * linkage_size),
             ((uint8_t *) tmp) + (i * linkage_size),
             linkage_size);
   }

   return ptr.gpu;
}

/* Does not take ownership of key. Clones if necessary. */
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

   if (stage == PIPE_SHADER_FRAGMENT) {
      nir_lower_blend_options opts = {
         .scalar_blend_const = true,
         .logicop_enable = key->blend.logicop_enable,
         .logicop_func = key->blend.logicop_func,
      };

      static_assert(ARRAY_SIZE(opts.format) == PIPE_MAX_COLOR_BUFS,
                    "max RTs out of sync");

      for (unsigned i = 0; i < PIPE_MAX_COLOR_BUFS; ++i)
         opts.format[i] = key->rt_formats[i];

      memcpy(opts.rt, key->blend.rt, sizeof(opts.rt));
      NIR_PASS_V(nir, nir_lower_blend, &opts);

      NIR_PASS_V(nir, nir_lower_fragcolor, key->nr_cbufs);

      if (key->clip_plane_enable) {
         NIR_PASS_V(nir, nir_lower_clip_fs, key->clip_plane_enable,
                    false);
      }
   }

   agx_compile_shader_nir(nir, &key->base, &binary, &compiled->info);

   if (binary.size) {
      struct agx_device *dev = agx_device(ctx->base.screen);
      compiled->bo = agx_bo_create(dev, binary.size, AGX_MEMORY_TYPE_SHADER);
      memcpy(compiled->bo->ptr.cpu, binary.data, binary.size);
   }

   ralloc_free(nir);
   util_dynarray_fini(&binary);

   /* key may be destroyed after we return, so clone it before using it as a
    * hash table key. The clone is logically owned by the hash table.
    */
   struct asahi_shader_key *cloned_key = ralloc(so->variants, struct asahi_shader_key);
   memcpy(cloned_key, key, sizeof(struct asahi_shader_key));

   he = _mesa_hash_table_insert(so->variants, cloned_key, compiled);
   *out = he->data;
   return true;
}

static bool
agx_update_vs(struct agx_context *ctx)
{
   struct agx_vs_shader_key key = { 0 };

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
      .clip_plane_enable = ctx->rast->base.clip_plane_enable,
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

struct agx_usc_builder {
   struct agx_ptr T;
   uint8_t *head;

#ifndef NDEBUG
   size_t size;
#endif
};

static struct agx_usc_builder
agx_alloc_usc_control(struct agx_pool *pool,
                      unsigned num_reg_bindings)
{
   STATIC_ASSERT(AGX_USC_TEXTURE_LENGTH == AGX_USC_UNIFORM_LENGTH);
   STATIC_ASSERT(AGX_USC_SAMPLER_LENGTH == AGX_USC_UNIFORM_LENGTH);

   size_t size = AGX_USC_UNIFORM_LENGTH * num_reg_bindings;

   size += AGX_USC_SHARED_LENGTH;
   size += AGX_USC_SHADER_LENGTH;
   size += AGX_USC_REGISTERS_LENGTH;
   size += MAX2(AGX_USC_NO_PRESHADER_LENGTH, AGX_USC_PRESHADER_LENGTH);
   size += AGX_USC_FRAGMENT_PROPERTIES_LENGTH;

   struct agx_usc_builder b = {
      .T = agx_pool_alloc_aligned(pool, size, 64),

#ifndef NDEBUG
      .size = size,
#endif
   };

   b.head = (uint8_t *) b.T.cpu;

   return b;
}

static bool
agx_usc_builder_validate(struct agx_usc_builder *b, size_t size)
{
#ifndef NDEBUG
   assert(((b->head - (uint8_t *) b->T.cpu) + size) <= b->size);
#endif

   return true;
}

#define agx_usc_pack(b, struct_name, template) \
   for (bool it = agx_usc_builder_validate((b), AGX_USC_##struct_name##_LENGTH); \
        it; it = false, (b)->head += AGX_USC_##struct_name##_LENGTH) \
      agx_pack((b)->head, USC_##struct_name, template)

static uint32_t
agx_usc_fini(struct agx_usc_builder *b)
{
   assert(b->T.gpu <= (1ull << 32) && "pipelines must be in low memory");
   return b->T.gpu;
}

static uint32_t
agx_build_pipeline(struct agx_context *ctx, struct agx_compiled_shader *cs, enum pipe_shader_type stage)
{
   unsigned nr_textures = ctx->stage[stage].texture_count;
   unsigned nr_samplers = ctx->stage[stage].sampler_count;

   struct agx_ptr T_tex = agx_pool_alloc_aligned(&ctx->batch->pool,
         AGX_TEXTURE_LENGTH * nr_textures, 64);

   struct agx_ptr T_samp = agx_pool_alloc_aligned(&ctx->batch->pool,
         AGX_SAMPLER_LENGTH * nr_samplers, 64);

   struct agx_texture_packed *textures = T_tex.cpu;
   struct agx_sampler_packed *samplers = T_samp.cpu;

   /* TODO: Dirty track me to save some CPU cycles and maybe improve caching */
   for (unsigned i = 0; i < nr_textures; ++i) {
      struct agx_sampler_view *tex = ctx->stage[stage].textures[i];
      agx_batch_add_bo(ctx->batch, agx_resource(tex->base.texture)->bo);

      textures[i] = tex->desc;
   }

   /* TODO: Dirty track me to save some CPU cycles and maybe improve caching */
   for (unsigned i = 0; i < PIPE_MAX_SAMPLERS; ++i) {
      struct agx_sampler_state *sampler = ctx->stage[stage].samplers[i];

      if (sampler)
         samplers[i] = sampler->desc;
   }

   struct agx_usc_builder b =
      agx_alloc_usc_control(&ctx->batch->pipeline_pool,
                           cs->info.push_ranges + 2);

   if (nr_textures) {
      agx_usc_pack(&b, TEXTURE, cfg) {
         cfg.start = 0;
         cfg.count = nr_textures;
         cfg.buffer = T_tex.gpu;
      }

      ctx->batch->textures = T_tex.gpu;
   }

   if (nr_samplers) {
      agx_usc_pack(&b, SAMPLER, cfg) {
         cfg.start = 0;
         cfg.count = nr_samplers;
         cfg.buffer = T_samp.gpu;
      }
   }

   /* Must only upload uniforms after uploading textures so we can implement the
    * AGX_PUSH_TEXTURE_BASE sysval correctly.
    */
   for (unsigned i = 0; i < cs->info.push_ranges; ++i) {
      struct agx_push push = cs->info.push[i];

      agx_usc_pack(&b, UNIFORM, cfg) {
         cfg.start_halfs = push.base;
         cfg.size_halfs = push.length;
         cfg.buffer = agx_push_location(ctx, push, stage);
      }
   }

   agx_usc_pack(&b, SHARED, cfg) {
      if (stage == PIPE_SHADER_FRAGMENT) {
         cfg.uses_shared_memory = true;
         cfg.shared_layout = AGX_SHARED_LAYOUT_32X32;
         cfg.pixel_stride_in_8_bytes = 1;
         cfg.shared_memory_per_threadgroup_in_256_bytes = 32;
      } else {
         cfg.shared_layout = AGX_SHARED_LAYOUT_VERTEX_COMPUTE;
      }
   }

   agx_usc_pack(&b, SHADER, cfg) {
      cfg.loads_varyings = (stage == PIPE_SHADER_FRAGMENT);
      cfg.code = cs->bo->ptr.gpu;
      cfg.unk_2 = (stage == PIPE_SHADER_FRAGMENT) ? 2 : 3;
   }

   agx_usc_pack(&b, REGISTERS, cfg) {
      cfg.register_quadwords = 0;
      cfg.unk_1 = (stage == PIPE_SHADER_FRAGMENT);
   }

   if (stage == PIPE_SHADER_FRAGMENT) {
      agx_usc_pack(&b, FRAGMENT_PROPERTIES, cfg) {
         bool writes_sample_mask = ctx->fs->info.writes_sample_mask;
         cfg.early_z_testing = !writes_sample_mask;
         cfg.unk_4 = 0x2;
         cfg.unk_5 = 0x0;
      }
   }

   agx_usc_pack(&b, NO_PRESHADER, cfg);

   return agx_usc_fini(&b);
}

/* Internal pipelines (TODO: refactor?) */
uint64_t
agx_build_clear_pipeline(struct agx_context *ctx, uint32_t code, uint64_t clear_buf)
{
   struct agx_usc_builder b =
      agx_alloc_usc_control(&ctx->batch->pipeline_pool, 1);

   agx_usc_pack(&b, UNIFORM, cfg) {
      cfg.start_halfs = (6 * 2);
      cfg.size_halfs = 4;
      cfg.buffer = clear_buf;
   }

   agx_usc_pack(&b, SHARED, cfg) {
      cfg.uses_shared_memory = true;
      cfg.shared_layout = AGX_SHARED_LAYOUT_32X32;
      cfg.pixel_stride_in_8_bytes = 1;
      cfg.shared_memory_per_threadgroup_in_256_bytes = 32;
   }

   agx_usc_pack(&b, SHADER, cfg) {
      cfg.code = code;
      cfg.unk_2 = 3;
   }

   agx_usc_pack(&b, REGISTERS, cfg) cfg.register_quadwords = 1;
   agx_usc_pack(&b, NO_PRESHADER, cfg);

   return agx_usc_fini(&b);
}

uint64_t
agx_build_reload_pipeline(struct agx_context *ctx, uint32_t code, struct pipe_surface *surf)
{
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
      cfg.unk_3 = 0;
   }

   agx_pack(texture.cpu, TEXTURE, cfg) {
      struct agx_resource *rsrc = agx_resource(surf->texture);
      unsigned layer = surf->u.tex.first_layer;
      const struct util_format_description *desc =
         util_format_description(surf->format);

      /* To reduce shader variants, we always use a 2D texture. For reloads of
       * arrays and cube maps, we map a single layer as a 2D image.
       */
      cfg.dimension = AGX_TEXTURE_DIMENSION_2D;
      cfg.layout = agx_translate_layout(rsrc->modifier);
      cfg.format = agx_pixel_format[surf->format].hw;
      cfg.swizzle_r = agx_channel_from_pipe(desc->swizzle[0]);
      cfg.swizzle_g = agx_channel_from_pipe(desc->swizzle[1]);
      cfg.swizzle_b = agx_channel_from_pipe(desc->swizzle[2]);
      cfg.swizzle_a = agx_channel_from_pipe(desc->swizzle[3]);
      cfg.width = surf->width;
      cfg.height = surf->height;
      cfg.first_level = surf->u.tex.level;
      cfg.last_level = surf->u.tex.level;
      cfg.unk_mipmapped = rsrc->mipmapped;
      cfg.srgb = (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);
      cfg.address = agx_map_texture_gpu(rsrc, layer);

      if (rsrc->modifier == DRM_FORMAT_MOD_LINEAR)
         cfg.stride = ail_get_linear_stride_B(&rsrc->layout, surf->u.tex.level) - 16;
      else
         cfg.unk_tiled = true;
   }

   struct agx_usc_builder b =
      agx_alloc_usc_control(&ctx->batch->pipeline_pool, 2);

   agx_usc_pack(&b, TEXTURE, cfg) {
      cfg.start = 0;
      cfg.count = 1;
      cfg.buffer = texture.gpu;
   }

   agx_usc_pack(&b, SAMPLER, cfg) {
      cfg.start = 0;
      cfg.count = 1;
      cfg.buffer = sampler.gpu;
   }

   agx_usc_pack(&b, SHARED, cfg) {
      cfg.uses_shared_memory = true;
      cfg.shared_layout = AGX_SHARED_LAYOUT_32X32;
      cfg.pixel_stride_in_8_bytes = 1;
      cfg.shared_memory_per_threadgroup_in_256_bytes = 32;
   }

   agx_usc_pack(&b, SHADER, cfg) {
      cfg.code = code;
      cfg.unk_2 = 3;
   }

   agx_usc_pack(&b, REGISTERS, cfg) cfg.register_quadwords = 0;
   agx_usc_pack(&b, NO_PRESHADER, cfg);

   return agx_usc_fini(&b);
}

uint64_t
agx_build_store_pipeline(struct agx_context *ctx, uint32_t code,
                         uint64_t render_target)
{
   struct agx_usc_builder b =
      agx_alloc_usc_control(&ctx->batch->pipeline_pool, 2);

   agx_usc_pack(&b, TEXTURE, cfg) {
      cfg.start = 0;
      cfg.count = 1;
      cfg.buffer = render_target;
   }

   uint32_t unk[] = { 0, ~0 };

   agx_usc_pack(&b, UNIFORM, cfg) {
      cfg.start_halfs = 4;
      cfg.size_halfs = 4;
      cfg.buffer = agx_pool_upload_aligned(&ctx->batch->pool, unk, sizeof(unk), 16);
   }

   agx_usc_pack(&b, SHARED, cfg) {
      cfg.uses_shared_memory = true;
      cfg.shared_layout = AGX_SHARED_LAYOUT_32X32;
      cfg.pixel_stride_in_8_bytes = 1;
      cfg.shared_memory_per_threadgroup_in_256_bytes = 32;
   }

   agx_usc_pack(&b, SHADER, cfg) cfg.code = code;
   agx_usc_pack(&b, REGISTERS, cfg) cfg.register_quadwords = 1;
   agx_usc_pack(&b, NO_PRESHADER, cfg);

   return agx_usc_fini(&b);
}

void
agx_batch_init_state(struct agx_batch *batch)
{
   /* Emit state on the batch that we don't change and so don't dirty track */
   uint8_t *out = batch->encoder_current;
   struct agx_ppp_update ppp = agx_new_ppp_update(&batch->pool, (struct AGX_PPP_HEADER) {
      .w_clamp = true,
      .varying_word_1 = true,
      .cull_2 = true,
      .occlusion_query = true,
      .occlusion_query_2 = true,
      .output_unknown = true,
      .varying_word_2 = true,
   });

   agx_ppp_push(&ppp, W_CLAMP, cfg) cfg.w_clamp = 1e-10;
   agx_ppp_push(&ppp, VARYING_1, cfg);
   agx_ppp_push(&ppp, CULL_2, cfg);
   agx_ppp_push(&ppp, FRAGMENT_OCCLUSION_QUERY, cfg);
   agx_ppp_push(&ppp, FRAGMENT_OCCLUSION_QUERY_2, cfg);
   agx_ppp_push(&ppp, OUTPUT_UNKNOWN, cfg);
   agx_ppp_push(&ppp, VARYING_2, cfg);

   agx_ppp_fini(&out, &ppp);
   batch->encoder_current = out;

   /* We need to emit prim state at the start. Max collides with all. */
   batch->reduced_prim = PIPE_PRIM_MAX;
}

static enum agx_object_type
agx_point_object_type(struct agx_rasterizer *rast)
{
   return (rast->base.sprite_coord_mode == PIPE_SPRITE_COORD_UPPER_LEFT) ?
          AGX_OBJECT_TYPE_POINT_SPRITE_UV01 :
          AGX_OBJECT_TYPE_POINT_SPRITE_UV10;
}

#define MAX_PPP_UPDATES 2

static uint8_t *
agx_encode_state(struct agx_context *ctx, uint8_t *out,
                 bool is_lines, bool is_points)
{
   struct agx_rasterizer *rast = ctx->rast;
   unsigned ppp_updates = 0;

#define IS_DIRTY(ST) !!(ctx->dirty & AGX_DIRTY_##ST)

   if (IS_DIRTY(VS)) {
      agx_pack(out, VDM_STATE, cfg) {
         cfg.vertex_shader_word_0_present = true;
         cfg.vertex_shader_word_1_present = true;
         cfg.vertex_outputs_present = true;
         cfg.vertex_unknown_present = true;
      }
      out += AGX_VDM_STATE_LENGTH;

      unsigned tex_count = ctx->stage[PIPE_SHADER_VERTEX].texture_count;
      agx_pack(out, VDM_STATE_VERTEX_SHADER_WORD_0, cfg) {
         cfg.groups_of_8_immediate_textures = DIV_ROUND_UP(tex_count, 8);
         cfg.groups_of_4_samplers = DIV_ROUND_UP(tex_count, 4);
      }
      out += AGX_VDM_STATE_VERTEX_SHADER_WORD_0_LENGTH;

      agx_pack(out, VDM_STATE_VERTEX_SHADER_WORD_1, cfg) {
         cfg.pipeline = agx_build_pipeline(ctx, ctx->vs, PIPE_SHADER_VERTEX);
      }
      out += AGX_VDM_STATE_VERTEX_SHADER_WORD_1_LENGTH;

      agx_pack(out, VDM_STATE_VERTEX_OUTPUTS, cfg) {
         cfg.output_count_1 = ctx->vs->info.varyings.vs.nr_index;
         cfg.output_count_2 = cfg.output_count_1;
      }
      out += AGX_VDM_STATE_VERTEX_OUTPUTS_LENGTH;

      agx_pack(out, VDM_STATE_VERTEX_UNKNOWN, cfg) {
         cfg.more_than_4_textures = tex_count >= 4;
      }
      out += AGX_VDM_STATE_VERTEX_UNKNOWN_LENGTH;

      /* Pad up to a multiple of 8 bytes */
      memset(out, 0, 4);
      out += 4;
   }

   struct agx_pool *pool = &ctx->batch->pool;
   struct agx_compiled_shader *vs = ctx->vs, *fs = ctx->fs;
   unsigned zbias = 0;

   if (ctx->rast->base.offset_tri) {
      zbias = agx_upload_depth_bias(ctx->batch, &ctx->rast->base);
      ctx->dirty |= AGX_DIRTY_SCISSOR_ZBIAS;
   }

   if (ctx->dirty & (AGX_DIRTY_VIEWPORT | AGX_DIRTY_SCISSOR_ZBIAS)) {
      agx_upload_viewport_scissor(pool, ctx->batch, &out, &ctx->viewport,
            ctx->rast->base.scissor ? &ctx->scissor : NULL,
            zbias);
   }

   bool varyings_dirty = false;

   if (IS_DIRTY(VS_PROG) || IS_DIRTY(FS_PROG) || IS_DIRTY(RS)) {
      ctx->batch->varyings = agx_link_varyings_vs_fs(&ctx->batch->pipeline_pool,
            &ctx->vs->info.varyings.vs,
            &ctx->fs->info.varyings.fs,
            ctx->rast->base.flatshade_first);

      varyings_dirty = true;
      ppp_updates++;
   }

   bool object_type_dirty = IS_DIRTY(PRIM) ||
                            (is_points && IS_DIRTY(SPRITE_COORD_MODE));

   bool fragment_control_dirty = IS_DIRTY(ZS) || IS_DIRTY(RS) ||
                                 IS_DIRTY(PRIM);

   bool fragment_face_dirty = IS_DIRTY(ZS) || IS_DIRTY(STENCIL_REF) ||
                              IS_DIRTY(RS);

   enum agx_object_type object_type =
         is_points ? agx_point_object_type(rast) :
         is_lines ? AGX_OBJECT_TYPE_LINE :
         AGX_OBJECT_TYPE_TRIANGLE;

   /* For now, we re-emit almost all state every draw.  TODO: perf */
   struct agx_ppp_update ppp = agx_new_ppp_update(pool, (struct AGX_PPP_HEADER) {
      .fragment_control = fragment_control_dirty,
      .fragment_control_2 = IS_DIRTY(PRIM) || IS_DIRTY(FS_PROG),
      .fragment_front_face = fragment_face_dirty,
      .fragment_front_face_2 = object_type_dirty,
      .fragment_front_stencil = IS_DIRTY(ZS),
      .fragment_back_face = fragment_face_dirty,
      .fragment_back_face_2 = object_type_dirty,
      .fragment_back_stencil = IS_DIRTY(ZS),
      .output_select = IS_DIRTY(VS_PROG) || IS_DIRTY(FS_PROG),
      .varying_word_0 = IS_DIRTY(VS_PROG),
      .cull = IS_DIRTY(RS),
      .fragment_shader = IS_DIRTY(FS) || varyings_dirty,
      .output_size = IS_DIRTY(VS_PROG),
   });

   if (fragment_control_dirty) {
      agx_ppp_push(&ppp, FRAGMENT_CONTROL, cfg) {
         cfg.stencil_test_enable = ctx->zs->base.stencil[0].enabled;
         cfg.two_sided_stencil = ctx->zs->base.stencil[1].enabled;
         cfg.depth_bias_enable = rast->base.offset_tri;

         cfg.unk_fill_lines = is_points; /* XXX: what is this? */

         /* Always enable scissoring so we may scissor to the viewport (TODO:
          * optimize this out if the viewport is the default and the app does
          * not use the scissor test)
          */
         cfg.scissor_enable = true;
      }
   }

   if (IS_DIRTY(PRIM) || IS_DIRTY(FS_PROG)) {
      agx_ppp_push(&ppp, FRAGMENT_CONTROL_2, cfg) {
         cfg.lines_or_points = (is_lines || is_points);
         cfg.no_colour_output = ctx->fs->info.no_colour_output;
         cfg.reads_tilebuffer = ctx->fs->info.reads_tib;
         cfg.sample_mask_from_shader = ctx->fs->info.writes_sample_mask;
      }
   }

   struct agx_fragment_face_packed front_face, back_face;

   if (fragment_face_dirty) {
      agx_pack(&front_face, FRAGMENT_FACE, cfg) {
         cfg.stencil_reference = ctx->stencil_ref.ref_value[0];
         cfg.line_width = rast->line_width;
         cfg.polygon_mode = AGX_POLYGON_MODE_FILL;
      };

      agx_pack(&back_face, FRAGMENT_FACE, cfg) {
         bool twosided = ctx->zs->base.stencil[1].enabled;
         cfg.stencil_reference = ctx->stencil_ref.ref_value[twosided ? 1 : 0];

         cfg.line_width = rast->line_width;
         cfg.polygon_mode = AGX_POLYGON_MODE_FILL;
      };

      front_face.opaque[0] |= ctx->zs->depth.opaque[0];
      back_face.opaque[0] |= ctx->zs->depth.opaque[0];

      agx_ppp_push_packed(&ppp, &front_face, FRAGMENT_FACE);
   }

   if (object_type_dirty)
      agx_ppp_push(&ppp, FRAGMENT_FACE_2, cfg) cfg.object_type = object_type;

   if (IS_DIRTY(ZS))
      agx_ppp_push_packed(&ppp, ctx->zs->front_stencil.opaque, FRAGMENT_STENCIL);

   if (fragment_face_dirty)
      agx_ppp_push_packed(&ppp, &back_face, FRAGMENT_FACE);

   if (object_type_dirty)
      agx_ppp_push(&ppp, FRAGMENT_FACE_2, cfg) cfg.object_type = object_type;

   if (IS_DIRTY(ZS))
      agx_ppp_push_packed(&ppp, ctx->zs->back_stencil.opaque, FRAGMENT_STENCIL);

   if (IS_DIRTY(VS_PROG) || IS_DIRTY(FS_PROG)) {
      agx_ppp_push(&ppp, OUTPUT_SELECT, cfg) {
         cfg.varyings = !!fs->info.varyings.fs.nr_bindings;
         cfg.point_size = vs->info.writes_psiz;
         cfg.frag_coord_z = fs->info.varyings.fs.reads_z;
      }
   }

   if (IS_DIRTY(VS_PROG)) {
      agx_ppp_push(&ppp, VARYING_0, cfg) {
         cfg.count = agx_num_general_outputs(&ctx->vs->info.varyings.vs);
      }
   }

   if (IS_DIRTY(RS))
      agx_ppp_push_packed(&ppp, ctx->rast->cull, CULL);

   if (IS_DIRTY(FS) || varyings_dirty) {
      unsigned frag_tex_count = ctx->stage[PIPE_SHADER_FRAGMENT].texture_count;
      agx_ppp_push(&ppp, FRAGMENT_SHADER, cfg) {
         cfg.pipeline = agx_build_pipeline(ctx, ctx->fs, PIPE_SHADER_FRAGMENT),
         cfg.groups_of_8_immediate_textures = DIV_ROUND_UP(frag_tex_count, 8);
         cfg.groups_of_4_samplers = DIV_ROUND_UP(frag_tex_count, 4);
         cfg.more_than_4_textures = frag_tex_count >= 4;
         cfg.cf_binding_count = ctx->fs->info.varyings.fs.nr_bindings;
         cfg.cf_bindings = ctx->batch->varyings;
      }
   }

   if (IS_DIRTY(VS_PROG)) {
      agx_ppp_push(&ppp, OUTPUT_SIZE, cfg)
         cfg.count = vs->info.varyings.vs.nr_index;
   }

   agx_ppp_fini(&out, &ppp);
   ppp_updates++;

#undef IS_DIRTY

   assert(ppp_updates <= MAX_PPP_UPDATES);
   return out;
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
agx_ensure_cmdbuf_has_space(struct agx_batch *batch, size_t space)
{
   /* If there is room in the command buffer, we're done */
   if (likely((batch->encoder_end - batch->encoder_current) >= space))
      return;

   /* Otherwise, we need to allocate a new command buffer. We use memory owned
    * by the batch to simplify lifetime management for the BO. 
    */
   size_t size = 65536;
   struct agx_ptr T = agx_pool_alloc_aligned(&batch->pool, size, 256);

   /* Jump from the old command buffer to the new command buffer */
   agx_pack(batch->encoder_current, VDM_STREAM_LINK, cfg) {
      cfg.target_lo = T.gpu & BITFIELD_MASK(32);
      cfg.target_hi = T.gpu >> 32;
   }

   /* Swap out the command buffer */
   batch->encoder_current = T.cpu;
   batch->encoder_end = batch->encoder_current + size;
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

#ifndef NDEBUG
   /* For debugging dirty tracking, mark all state as dirty every draw, forcing
    * everything to be re-emitted fresh.
    */
   if (unlikely(agx_device(pctx->screen)->debug & AGX_DBG_DIRTY))
      agx_dirty_all(ctx);
#endif

   /* Dirty track the reduced prim: lines vs points vs triangles */
   enum pipe_prim_type reduced_prim = u_reduced_prim(info->mode);
   if (reduced_prim != batch->reduced_prim) ctx->dirty |= AGX_DIRTY_PRIM;
   batch->reduced_prim = reduced_prim;

   /* TODO: masks */
   ctx->batch->draw |= ~0;
   ctx->batch->load |= ~0;

   /* TODO: These are expensive calls, consider finer dirty tracking */
   if (agx_update_vs(ctx))
      ctx->dirty |= AGX_DIRTY_VS | AGX_DIRTY_VS_PROG;
   else if (ctx->stage[PIPE_SHADER_VERTEX].dirty)
      ctx->dirty |= AGX_DIRTY_VS;

   if (agx_update_fs(ctx))
      ctx->dirty |= AGX_DIRTY_FS | AGX_DIRTY_FS_PROG;
   else if (ctx->stage[PIPE_SHADER_FRAGMENT].dirty)
      ctx->dirty |= AGX_DIRTY_FS;

   agx_batch_add_bo(batch, ctx->vs->bo);
   agx_batch_add_bo(batch, ctx->fs->bo);

   /* When we approach the end of a command buffer, cycle it out for a new one.
    * We only need to do this once per draw as long as we conservatively
    * estimate the maximum bytes of VDM commands that this draw will emit.
    */
   agx_ensure_cmdbuf_has_space(batch,
                               (AGX_VDM_STATE_LENGTH * 2) +
                               (AGX_PPP_STATE_LENGTH * MAX_PPP_UPDATES) +
                               AGX_VDM_STATE_RESTART_INDEX_LENGTH +
                               AGX_VDM_STATE_VERTEX_SHADER_WORD_0_LENGTH +
                               AGX_VDM_STATE_VERTEX_SHADER_WORD_1_LENGTH +
                               AGX_VDM_STATE_VERTEX_OUTPUTS_LENGTH +
                               AGX_VDM_STATE_VERTEX_UNKNOWN_LENGTH +
                               4 /* padding */ +
                               AGX_INDEX_LIST_LENGTH +
                               AGX_INDEX_LIST_BUFFER_LO_LENGTH +
                               AGX_INDEX_LIST_COUNT_LENGTH +
                               AGX_INDEX_LIST_INSTANCES_LENGTH +
                               AGX_INDEX_LIST_START_LENGTH +
                               AGX_INDEX_LIST_BUFFER_SIZE_LENGTH);

   uint8_t *out = agx_encode_state(ctx, batch->encoder_current,
                                   reduced_prim == PIPE_PRIM_LINES,
                                   reduced_prim == PIPE_PRIM_POINTS);

   enum agx_primitive prim = agx_primitive_for_pipe(info->mode);
   unsigned idx_size = info->index_size;
   uint64_t ib = idx_size ? agx_index_buffer_ptr(batch, draws, info) : 0;

   if (idx_size) {
      /* Index sizes are encoded logarithmically */
      STATIC_ASSERT(__builtin_ctz(1) == AGX_INDEX_SIZE_U8);
      STATIC_ASSERT(__builtin_ctz(2) == AGX_INDEX_SIZE_U16);
      STATIC_ASSERT(__builtin_ctz(4) == AGX_INDEX_SIZE_U32);
      assert((idx_size == 1) || (idx_size == 2) || (idx_size == 4));

      agx_pack(out, VDM_STATE, cfg) cfg.restart_index_present = true;
      out += AGX_VDM_STATE_LENGTH;

      agx_pack(out, VDM_STATE_RESTART_INDEX, cfg) {
         cfg.value = info->restart_index;
      }
      out += AGX_VDM_STATE_RESTART_INDEX_LENGTH;
   }

   agx_pack(out, INDEX_LIST, cfg) {
      cfg.primitive = prim;
      cfg.index_count_present = true;
      cfg.instance_count_present = true;
      cfg.start_present = true;

      if (idx_size) {
         cfg.restart_enable = info->primitive_restart;
         cfg.index_buffer_hi = (ib >> 32);
         cfg.index_size = __builtin_ctz(idx_size);
         cfg.index_buffer_present = true;
         cfg.index_buffer_size_present = true;
      }
   }
   out += AGX_INDEX_LIST_LENGTH;

   if (idx_size) {
      agx_pack(out, INDEX_LIST_BUFFER_LO, cfg) {
         cfg.buffer_lo = ib & BITFIELD_MASK(32);
      }
      out += AGX_INDEX_LIST_BUFFER_LO_LENGTH;
   }

   agx_pack(out, INDEX_LIST_COUNT, cfg) cfg.count = draws->count;
   out += AGX_INDEX_LIST_COUNT_LENGTH;

   agx_pack(out, INDEX_LIST_INSTANCES, cfg) cfg.count = info->instance_count;
   out += AGX_INDEX_LIST_INSTANCES_LENGTH;

   agx_pack(out, INDEX_LIST_START, cfg) {
      cfg.start = idx_size ? draws->index_bias : draws->start;
   }
   out += AGX_INDEX_LIST_START_LENGTH;

   if (idx_size) {
      agx_pack(out, INDEX_LIST_BUFFER_SIZE, cfg) {
         cfg.size = ALIGN_POT(draws->count * idx_size, 4);
      }
      out += AGX_INDEX_LIST_BUFFER_SIZE_LENGTH;
   }

   batch->encoder_current = out;
   assert(batch->encoder_current <= batch->encoder_end &&
          "Failed to reserve sufficient space in encoder");
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
