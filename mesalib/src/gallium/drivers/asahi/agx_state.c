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
#include "gallium/auxiliary/tgsi/tgsi_from_mesa.h"
#include "compiler/nir/nir.h"
#include "asahi/compiler/agx_compile.h"
#include "agx_state.h"
#include "asahi/lib/agx_pack.h"
#include "asahi/lib/agx_formats.h"

static void
agx_set_blend_color(struct pipe_context *ctx,
                    const struct pipe_blend_color *state)
{
}

static void *
agx_create_blend_state(struct pipe_context *ctx,
                       const struct pipe_blend_state *state)
{
   return MALLOC(1);
}

static void *
agx_create_zsa_state(struct pipe_context *ctx,
                     const struct pipe_depth_stencil_alpha_state *state)
{
   struct agx_zsa *so = CALLOC_STRUCT(agx_zsa);
   assert(!state->depth_bounds_test && "todo");

   so->disable_z_write = !state->depth_writemask;

   /* Z func can be used as-is */
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_NEVER    == AGX_ZS_FUNC_NEVER);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_LESS     == AGX_ZS_FUNC_LESS);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_EQUAL    == AGX_ZS_FUNC_EQUAL);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_LEQUAL   == AGX_ZS_FUNC_LEQUAL);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_GREATER  == AGX_ZS_FUNC_GREATER);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_NOTEQUAL == AGX_ZS_FUNC_NOT_EQUAL);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_GEQUAL   == AGX_ZS_FUNC_GEQUAL);
   STATIC_ASSERT((enum agx_zs_func) PIPE_FUNC_ALWAYS   == AGX_ZS_FUNC_ALWAYS);

   so->z_func = state->depth_enabled ?
                ((enum agx_zs_func) state->depth_func) : AGX_ZS_FUNC_ALWAYS;

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

   agx_pack(so->cull, CULL, cfg) {
      /* TODO: debug culling */
      cfg.cull_front = cso->cull_face & PIPE_FACE_FRONT;
      cfg.cull_back = cso->cull_face & PIPE_FACE_BACK;
      cfg.front_face_ccw = cso->front_ccw;
      //    cfg.depth_clip = cso->depth_clip_near;
      cfg.depth_clamp = !cso->depth_clip_near;
   };

   return so;
}

static void
agx_bind_rasterizer_state(struct pipe_context *pctx, void *cso)
{
   struct agx_context *ctx = agx_context(pctx);
   ctx->rast = cso;
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

   uint64_t *m = (uint64_t *) ((uint8_t *) bo->ptr.cpu + AGX_SAMPLER_LENGTH);
   m[3] = 0x40; // XXX - what is this? maybe spurious?

   return bo;
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

   memcpy(&ctx->stage[shader].samplers[start], states,
          sizeof(struct agx_bo *) * count);
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

static struct pipe_sampler_view *
agx_create_sampler_view(struct pipe_context *pctx,
                        struct pipe_resource *texture,
                        const struct pipe_sampler_view *state)
{
   struct agx_device *dev = agx_device(pctx->screen);
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

   /* Pack the descriptor into GPU memory */
   agx_pack(so->desc->ptr.cpu, TEXTURE, cfg) {
      assert(state->format == PIPE_FORMAT_B8G8R8A8_UNORM); // TODO: format table
      cfg.format = 0xa22;
      cfg.swizzle_r = agx_channel_from_pipe(out_swizzle[0]);
      cfg.swizzle_g = agx_channel_from_pipe(out_swizzle[1]);
      cfg.swizzle_b = agx_channel_from_pipe(out_swizzle[2]);
      cfg.swizzle_a = agx_channel_from_pipe(out_swizzle[3]);
      cfg.width = texture->width0;
      cfg.height = texture->height0;
      cfg.srgb = (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);
      cfg.unk_1 = agx_resource(texture)->bo->ptr.gpu;
      cfg.unk_2 = 0x20000;
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

      pipe_sampler_view_reference((struct pipe_sampler_view **)
                                  &ctx->stage[shader].textures[i], views[i]);
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
}

static void
agx_set_scissor_states(struct pipe_context *ctx,
                       unsigned start_slot,
                       unsigned num_scissors,
                       const struct pipe_scissor_state *state)
{
}

static void
agx_set_stencil_ref(struct pipe_context *ctx,
                    const struct pipe_stencil_ref state)
{
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

   if (!vp)
      return;

   float vp_minx = vp->translate[0] - fabsf(vp->scale[0]);
   float vp_maxx = vp->translate[0] + fabsf(vp->scale[0]);
   float vp_miny = vp->translate[1] - fabsf(vp->scale[1]);
   float vp_maxy = vp->translate[1] + fabsf(vp->scale[1]);

   float near_z, far_z;
   util_viewport_zmin_zmax(vp, false, &near_z, &far_z);

   agx_pack(ctx->viewport, VIEWPORT, cfg) {
      cfg.min_tile_x = vp_minx / 32;
      cfg.min_tile_y = vp_miny / 32;
      cfg.max_tile_x = MAX2(ceilf(vp_maxx / 32.0), 1.0);
      cfg.max_tile_y = MAX2(ceilf(vp_maxy / 32.0), 1.0);
      cfg.clip_tile = true;

      cfg.translate_x = vp->translate[0];
      cfg.translate_y = vp->translate[1];
      cfg.scale_x = vp->scale[0];
      cfg.scale_y = vp->scale[1];
      cfg.near_z = near_z;
      cfg.z_range = far_z - near_z;
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

   ctx->batch->width = state->width;
   ctx->batch->height = state->height;
   ctx->batch->nr_cbufs = state->nr_cbufs;
   ctx->batch->cbufs[0] = state->cbufs[0];

   for (unsigned i = 0; i < state->nr_cbufs; ++i) {
      struct pipe_surface *surf = state->cbufs[i];
      struct agx_resource *tex = agx_resource(surf->texture);
      agx_pack(ctx->render_target[i], RENDER_TARGET, cfg) {
         assert(surf->format == PIPE_FORMAT_B8G8R8A8_UNORM); // TODO: format table
         cfg.format = 0xa22;
         cfg.swizzle_r = AGX_CHANNEL_B;
         cfg.swizzle_g = AGX_CHANNEL_G;
         cfg.swizzle_b = AGX_CHANNEL_R;
         cfg.swizzle_a = AGX_CHANNEL_A;
         cfg.width = state->width;
         cfg.height = state->height;
         cfg.buffer = tex->bo->ptr.gpu;
         cfg.unk_100 = 0x1000000;
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
agx_bind_state(struct pipe_context *ctx, void *state)
{
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
      assert(ve.instance_divisor == 0 && "no instancing");

      const struct util_format_description *desc =
         util_format_description(ve.src_format);

      assert(desc->nr_channels >= 1 && desc->nr_channels <= 4);
      assert((ve.src_offset & 0x3) == 0);

      attribs[i] = (struct agx_attribute) {
         .buf = ve.vertex_buffer_index,
         .src_offset = ve.src_offset / 4,
         .nr_comps_minus_1 = desc->nr_channels - 1,
         .format = agx_vertex_format[ve.src_format],
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

static void *
agx_create_shader_state(struct pipe_context *ctx,
                        const struct pipe_shader_state *cso)
{
   struct agx_uncompiled_shader *so = CALLOC_STRUCT(agx_uncompiled_shader);

   if (!so)
      return NULL;

   /* TGSI unsupported */
   assert(cso->type == PIPE_SHADER_IR_NIR);
   so->nir = cso->ir.nir;

   so->variants = _mesa_hash_table_create(NULL,
                                          _mesa_hash_pointer, _mesa_key_pointer_equal);
   return so;
}

static bool
agx_update_shader(struct agx_context *ctx, struct agx_compiled_shader **out,
                  enum pipe_shader_type stage, struct agx_shader_key *key)
{
   struct agx_uncompiled_shader *so = ctx->stage[stage].shader;
   assert(so != NULL);

   struct hash_entry *he = _mesa_hash_table_search(so->variants, &key);

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
   agx_compile_shader_nir(nir, key, &binary, &compiled->info);

   /* TODO: emit this properly */
   nir_variable_mode varying_mode = (nir->info.stage == MESA_SHADER_FRAGMENT) ?
                                    nir_var_shader_in : nir_var_shader_out;

   unsigned varying_count = 0;

   nir_foreach_variable_with_modes(var, nir, varying_mode) {
      unsigned loc = var->data.driver_location;
      unsigned sz = glsl_count_attribute_slots(var->type, FALSE);

      varying_count = MAX2(varying_count, loc + sz);
   }

   compiled->varying_count = varying_count;

   unsigned varying_desc_len = AGX_VARYING_HEADER_LENGTH + varying_count * AGX_VARYING_LENGTH;
   uint8_t *varying_desc = calloc(1, varying_desc_len);

   agx_pack(varying_desc, VARYING_HEADER, cfg) {
      cfg.slots_1 = 1 + (4 * varying_count);
      cfg.slots_2 = 1 + (4 * varying_count);
   }

   for (unsigned i = 0; i < varying_count; ++i) {
      agx_pack(varying_desc + AGX_VARYING_HEADER_LENGTH + (i * AGX_VARYING_LENGTH), VARYING, cfg) {
         cfg.slot_1 = 1 + (4 * i);
         cfg.slot_2 = 1 + (4 * i);
      }
   }

   if (binary.size) {
      struct agx_device *dev = agx_device(ctx->base.screen);
      compiled->bo = agx_bo_create(dev,
                                   ALIGN_POT(binary.size, 256) + ((3 * (AGX_VARYING_HEADER_LENGTH + varying_count * AGX_VARYING_LENGTH)) + 20),
                                   AGX_MEMORY_TYPE_SHADER);
      memcpy(compiled->bo->ptr.cpu, binary.data, binary.size);


      /* TODO: Why is the varying descriptor duplicated 3x? */
      unsigned offs = ALIGN_POT(binary.size, 256);
      unsigned unk_offs = offs + 0x40;
      for (unsigned copy = 0; copy < 3; ++copy) {
         memcpy(((uint8_t *) compiled->bo->ptr.cpu) + offs, varying_desc, varying_desc_len);
         offs += varying_desc_len;
      }

      uint16_t *map = (uint16_t *) (((uint8_t *) compiled->bo->ptr.cpu) + unk_offs);
      *map = 0x140; // 0x0100 with one varying



      compiled->varyings = compiled->bo->ptr.gpu + ALIGN_POT(binary.size, 256);
   }

   ralloc_free(nir);
   util_dynarray_fini(&binary);

   he = _mesa_hash_table_insert(so->variants, &key, compiled);
   *out = he->data;
   return true;
}

static bool
agx_update_vs(struct agx_context *ctx)
{
   struct agx_vs_shader_key key = {
      .num_vbufs = util_last_bit(ctx->vb_mask)
   };

   memcpy(key.attributes, ctx->attributes,
          sizeof(key.attributes[0]) * AGX_MAX_ATTRIBS);

   for (unsigned i = 0; i < key.num_vbufs; ++i) {
      assert((ctx->vertex_buffers[i].stride & 0x3) == 0);
      key.vbuf_strides[i] = ctx->vertex_buffers[i].stride / 4; // TODO: alignment
   }

   return agx_update_shader(ctx, &ctx->vs, PIPE_SHADER_VERTEX,
                            (struct agx_shader_key *) &key);
}

static bool
agx_update_fs(struct agx_context *ctx)
{
   struct agx_fs_shader_key key = {
      .tib_formats = { AGX_FORMAT_U8NORM }
   };

   return agx_update_shader(ctx, &ctx->fs, PIPE_SHADER_FRAGMENT,
                            (struct agx_shader_key *) &key);
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

   for (unsigned i = 0; i < cs->info.push_ranges; ++i) {
      struct agx_push push = cs->info.push[i];
      uint64_t buffer = agx_push_location(ctx, push, stage);
      unsigned halfs_per_record = 14;
      unsigned records = DIV_ROUND_UP(push.length, halfs_per_record);

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
      struct agx_bo *bo = ctx->stage[stage].samplers[i];

      if (!bo)
         continue;

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
         cfg.unk_2b = (cs->varying_count * 4);
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
demo_launch_fragment(struct agx_pool *pool, uint32_t pipeline, uint32_t varyings, unsigned input_count)
{
   uint32_t unk[] = {
      0x800000,
      0x1212 | (input_count << 16), // upper nibble is input count TODO: xmlify
      pipeline,
      varyings,
      0x0,
   };

   return agx_pool_upload(pool, unk, sizeof(unk));
}

static uint64_t
demo_unk8(struct agx_compiled_shader *fs, struct agx_pool *pool)
{
   /* Varying related */
   uint32_t unk[] = {
      /* interpolated count */
      0x100c0000, fs->varying_count * 4, 0x0, 0x0, 0x0,
   };

   return agx_pool_upload(pool, unk, sizeof(unk));
}

static uint64_t
demo_linkage(struct agx_compiled_shader *vs, struct agx_pool *pool)
{
   struct agx_ptr t = agx_pool_alloc_aligned(pool, AGX_LINKAGE_LENGTH, 64);

   agx_pack(t.cpu, LINKAGE, cfg) {
      cfg.varying_count = 4 * vs->varying_count;
      cfg.unk_1 = 0x10000; // varyings otherwise wrong
   };

   return t.gpu;
}

static uint64_t
demo_rasterizer(struct agx_context *ctx, struct agx_pool *pool)
{
   struct agx_ptr t = agx_pool_alloc_aligned(pool, AGX_RASTERIZER_LENGTH, 64);

   agx_pack(t.cpu, RASTERIZER, cfg) {
      cfg.front.depth_function = ctx->zs.z_func;
      cfg.back.depth_function = ctx->zs.z_func;

      cfg.front.disable_depth_write = ctx->zs.disable_z_write;
      cfg.back.disable_depth_write = ctx->zs.disable_z_write;
   };

   return t.gpu;
}

static uint64_t
demo_unk11(struct agx_pool *pool, bool prim_lines)
{
#define UNK11_FILL_MODE_LINES_1 (1 << 26)

#define UNK11_FILL_MODE_LINES_2 (0x5004 << 16)
#define UNK11_LINES (0x10000000)

   uint32_t unk[] = {
      0x200004a,
      0x200 | (prim_lines ? UNK11_FILL_MODE_LINES_1 : 0),
      0x7e00000 | (prim_lines ? UNK11_LINES : 0),
      0x7e00000 | (prim_lines ? UNK11_LINES : 0),

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
demo_unk14(struct agx_pool *pool)
{
   uint32_t unk[] = {
      0x100, 0x0,
   };

   return agx_pool_upload(pool, unk, sizeof(unk));
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
                 bool is_lines)
{
   agx_pack(out, BIND_PIPELINE, cfg) {
      cfg.pipeline = pipeline_vertex;
      cfg.vs_output_count_1 = (ctx->vs->varying_count * 4);
      cfg.vs_output_count_2 = (ctx->vs->varying_count * 4);
   }

   /* yes, it's really 17 bytes */
   out += AGX_BIND_PIPELINE_LENGTH;
   *(out++) = 0x0;

   struct agx_pool *pool = &ctx->batch->pool;
   struct agx_ptr zero = agx_pool_alloc_aligned(pool, 16, 256);
   memset(zero.cpu, 0, 16);

   agx_push_record(&out, 0, zero.gpu);
   agx_push_record(&out, 5, demo_unk8(ctx->fs, pool));
   agx_push_record(&out, 5, demo_launch_fragment(pool, pipeline_fragment, varyings, ctx->fs->varying_count + 1));
   agx_push_record(&out, 4, demo_linkage(ctx->vs, pool));
   agx_push_record(&out, 7, demo_rasterizer(ctx, pool));
   agx_push_record(&out, 5, demo_unk11(pool, is_lines));
   agx_push_record(&out, 10, agx_pool_upload(pool, ctx->viewport, sizeof(ctx->viewport)));
   agx_push_record(&out, 3, demo_unk12(pool));
   agx_push_record(&out, 2, agx_pool_upload(pool, ctx->rast->cull, sizeof(ctx->rast->cull)));
   agx_push_record(&out, 2, demo_unk14(pool));

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
   if (info->instance_count != 1)
      unreachable("todo: instancing");

   struct agx_context *ctx = agx_context(pctx);
   struct agx_batch *batch = ctx->batch;

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
                                   ctx->fs->varyings, is_lines);

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
         cfg.restart_index = 0xFFFF;//info->restart_index;
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
   ctx->bind_blend_state = agx_bind_state;
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
}
