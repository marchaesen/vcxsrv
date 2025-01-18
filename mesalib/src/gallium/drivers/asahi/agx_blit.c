/*
 * Copyright 2021 Alyssa Rosenzweig
 * Copyright 2020-2021 Collabora, Ltd.
 * Copyright 2019 Sonny Jiang <sonnyj608@gmail.com>
 * Copyright 2019 Advanced Micro Devices, Inc.
 * Copyright 2014 Broadcom
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "asahi/layout/layout.h"
#include "asahi/lib/agx_nir_passes.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_format_convert.h"
#include "gallium/auxiliary/util/u_blitter.h"
#include "gallium/auxiliary/util/u_dump.h"
#include "nir/pipe_nir.h"
#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
#include "util/format/u_formats.h"
#include "util/hash_table.h"
#include "util/macros.h"
#include "util/ralloc.h"
#include "util/u_sampler.h"
#include "util/u_surface.h"
#include "agx_state.h"
#include "glsl_types.h"
#include "nir.h"
#include "nir_builder_opcodes.h"
#include "shader_enums.h"

/* For block based blit kernels, we hardcode the maximum tile size which we can
 * always achieve. This simplifies our life.
 */
#define TILE_WIDTH  32
#define TILE_HEIGHT 32

static enum pipe_format
effective_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_Z32_FLOAT:
   case PIPE_FORMAT_Z24X8_UNORM:
      return PIPE_FORMAT_R32_FLOAT;
   case PIPE_FORMAT_Z16_UNORM:
      return PIPE_FORMAT_R16_UNORM;
   case PIPE_FORMAT_S8_UINT:
      return PIPE_FORMAT_R8_UINT;
   default:
      return format;
   }
}

static void *
asahi_blit_compute_shader(struct pipe_context *ctx, struct asahi_blit_key *key)
{
   const nir_shader_compiler_options *options =
      ctx->screen->get_compiler_options(ctx->screen, PIPE_SHADER_IR_NIR,
                                        PIPE_SHADER_COMPUTE);

   nir_builder b_ =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "blit_cs");
   nir_builder *b = &b_;
   b->shader->info.workgroup_size[0] = TILE_WIDTH;
   b->shader->info.workgroup_size[1] = TILE_HEIGHT;
   b->shader->info.num_ubos = 1;

   BITSET_SET(b->shader->info.textures_used, 0);
   BITSET_SET(b->shader->info.samplers_used, 0);
   BITSET_SET(b->shader->info.images_used, 0);

   nir_def *zero = nir_imm_int(b, 0);

   nir_def *params[4];
   b->shader->num_uniforms = ARRAY_SIZE(params);
   for (unsigned i = 0; i < b->shader->num_uniforms; ++i) {
      params[i] = nir_load_ubo(b, 2, 32, zero, nir_imm_int(b, i * 8),
                               .align_mul = 4, .range = ~0);
   }

   nir_def *trans_offs = params[0];
   nir_def *trans_scale = params[1];
   nir_def *dst_offs_2d = params[2];
   nir_def *dimensions_el_2d = params[3];

   nir_def *phys_id_el_nd = nir_trim_vector(
      b, nir_load_global_invocation_id(b, 32), key->array ? 3 : 2);
   nir_def *phys_id_el_2d = nir_trim_vector(b, phys_id_el_nd, 2);
   nir_def *layer = key->array ? nir_channel(b, phys_id_el_nd, 2) : NULL;

   /* Offset within the tile. We're dispatched for the entire tile but the
    * beginning might be out-of-bounds, so fix up.
    */
   nir_def *offs_in_tile_el_2d = nir_iand_imm(b, dst_offs_2d, 31);
   nir_def *logical_id_el_2d = nir_isub(b, phys_id_el_2d, offs_in_tile_el_2d);

   nir_def *image_pos_2d = nir_iadd(b, logical_id_el_2d, dst_offs_2d);
   nir_def *image_pos_nd = image_pos_2d;
   if (layer) {
      image_pos_nd =
         nir_vector_insert_imm(b, nir_pad_vector(b, image_pos_nd, 3), layer, 2);
   }

   nir_def *in_bounds;
   if (key->aligned) {
      in_bounds = nir_imm_true(b);
   } else {
      in_bounds = nir_ige(b, logical_id_el_2d, nir_imm_ivec2(b, 0, 0));
      in_bounds =
         nir_iand(b, in_bounds, nir_ilt(b, logical_id_el_2d, dimensions_el_2d));
   }

   nir_def *colour0, *colour1;
   nir_push_if(b, nir_ball(b, in_bounds));
   {
      /* For pixels within the copy area, texture from the source */
      nir_def *coords_el_2d =
         nir_ffma(b, nir_u2f32(b, logical_id_el_2d), trans_scale, trans_offs);

      nir_def *coords_el_nd = coords_el_2d;
      if (layer) {
         coords_el_nd = nir_vector_insert_imm(
            b, nir_pad_vector(b, coords_el_nd, 3), nir_u2f32(b, layer), 2);
      }

      nir_tex_instr *tex = nir_tex_instr_create(b->shader, 1);
      tex->dest_type = nir_type_uint32; /* irrelevant */
      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
      tex->is_array = key->array;
      tex->op = nir_texop_tex;
      tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, coords_el_nd);
      tex->backend_flags = AGX_TEXTURE_FLAG_NO_CLAMP;
      tex->coord_components = coords_el_nd->num_components;
      tex->texture_index = 0;
      tex->sampler_index = 0;
      nir_def_init(&tex->instr, &tex->def, 4, 32);
      nir_builder_instr_insert(b, &tex->instr);
      colour0 = &tex->def;
   }
   nir_push_else(b, NULL);
   {
      /* For out-of-bounds pixels, copy in the destination */
      colour1 = nir_image_load(
         b, 4, 32, nir_imm_int(b, 0), nir_pad_vec4(b, image_pos_nd), zero, zero,
         .image_array = key->array, .image_dim = GLSL_SAMPLER_DIM_2D,
         .access = ACCESS_IN_BOUNDS_AGX, .dest_type = nir_type_uint32);
   }
   nir_pop_if(b, NULL);
   nir_def *color = nir_if_phi(b, colour0, colour1);

   enum asahi_blit_clamp clamp = ASAHI_BLIT_CLAMP_NONE;
   bool src_sint = util_format_is_pure_sint(key->src_format);
   bool dst_sint = util_format_is_pure_sint(key->dst_format);
   if (util_format_is_pure_integer(key->src_format) &&
       util_format_is_pure_integer(key->dst_format)) {

      if (src_sint && !dst_sint)
         clamp = ASAHI_BLIT_CLAMP_SINT_TO_UINT;
      else if (!src_sint && dst_sint)
         clamp = ASAHI_BLIT_CLAMP_UINT_TO_SINT;
   }

   if (clamp == ASAHI_BLIT_CLAMP_SINT_TO_UINT)
      color = nir_imax(b, color, nir_imm_int(b, 0));
   else if (clamp == ASAHI_BLIT_CLAMP_UINT_TO_SINT)
      color = nir_umin(b, color, nir_imm_int(b, INT32_MAX));

   nir_def *local_offset = nir_imm_intN_t(b, 0, 16);
   nir_def *lid = nir_trim_vector(b, nir_load_local_invocation_id(b), 2);
   lid = nir_u2u16(b, lid);

   /* Pure integer formatss need to be clamped in software, at least in some
    * cases. We do so on store. Piglit gl-3.0-render-integer checks this, as
    * does KHR-GL33.packed_pixels.*.
    *
    * TODO: Make this common code somehow.
    */
   const struct util_format_description *desc =
      util_format_description(key->dst_format);
   unsigned c = util_format_get_first_non_void_channel(key->dst_format);

   if (desc->channel[c].size <= 16 &&
       util_format_is_pure_integer(key->dst_format)) {

      unsigned bits[4] = {
         desc->channel[0].size ?: desc->channel[0].size,
         desc->channel[1].size ?: desc->channel[0].size,
         desc->channel[2].size ?: desc->channel[0].size,
         desc->channel[3].size ?: desc->channel[0].size,
      };

      if (util_format_is_pure_sint(key->dst_format))
         color = nir_format_clamp_sint(b, color, bits);
      else
         color = nir_format_clamp_uint(b, color, bits);

      color = nir_u2u16(b, color);
   }

   /* The source texel has been converted into a 32-bit value. We need to
    * convert it to a tilebuffer format that can then be converted to the
    * destination format in the PBE hardware. That's the renderable format for
    * the destination format, which must exist along this path. This mirrors the
    * flow of fragment and end-of-tile shaders.
    */
   enum pipe_format tib_format =
      ail_pixel_format[effective_format(key->dst_format)].renderable;

   nir_store_local_pixel_agx(b, color, nir_imm_intN_t(b, 1, 16), lid, .base = 0,
                             .write_mask = 0xf, .format = tib_format,
                             .explicit_coord = true);

   nir_barrier(b, .execution_scope = SCOPE_WORKGROUP);

   nir_push_if(b, nir_ball(b, nir_ieq_imm(b, lid, 0)));
   {
      nir_def *pbe_index = nir_imm_intN_t(b, 2, 16);
      nir_image_store_block_agx(
         b, pbe_index, local_offset, image_pos_nd, .format = tib_format,
         .image_dim = GLSL_SAMPLER_DIM_2D, .image_array = key->array,
         .explicit_coord = true);
   }
   nir_pop_if(b, NULL);
   b->shader->info.cs.image_block_size_per_thread_agx =
      util_format_get_blocksize(key->dst_format);

   return pipe_shader_from_nir(ctx, b->shader);
}

static bool
asahi_compute_blit_supported(const struct pipe_blit_info *info)
{
   return (info->src.box.depth == info->dst.box.depth) && !info->alpha_blend &&
          !info->num_window_rectangles && !info->sample0_only &&
          !info->scissor_enable && !info->window_rectangle_include &&
          info->src.resource->nr_samples <= 1 &&
          info->dst.resource->nr_samples <= 1 &&
          !util_format_is_depth_and_stencil(info->src.format) &&
          !util_format_is_depth_and_stencil(info->dst.format) &&
          info->src.box.depth >= 0 &&
          info->mask == util_format_get_mask(info->src.format) &&
          /* XXX: texsubimage pbo failing otherwise, needs investigation */
          info->dst.format != PIPE_FORMAT_B5G6R5_UNORM &&
          info->dst.format != PIPE_FORMAT_B5G5R5A1_UNORM &&
          info->dst.format != PIPE_FORMAT_B5G5R5X1_UNORM &&
          info->dst.format != PIPE_FORMAT_R5G6B5_UNORM &&
          info->dst.format != PIPE_FORMAT_R5G5B5A1_UNORM &&
          info->dst.format != PIPE_FORMAT_R5G5B5X1_UNORM;
}

static void
asahi_compute_save(struct agx_context *ctx)
{
   struct asahi_blitter *blitter = &ctx->compute_blitter;
   struct agx_stage *stage = &ctx->stage[PIPE_SHADER_COMPUTE];

   assert(!blitter->active && "recursion detected, driver bug");

   pipe_resource_reference(&blitter->saved_cb.buffer, stage->cb[0].buffer);
   memcpy(&blitter->saved_cb, &stage->cb[0],
          sizeof(struct pipe_constant_buffer));

   blitter->has_saved_image = stage->image_mask & BITFIELD_BIT(0);
   if (blitter->has_saved_image) {
      pipe_resource_reference(&blitter->saved_image.resource,
                              stage->images[0].resource);
      memcpy(&blitter->saved_image, &stage->images[0],
             sizeof(struct pipe_image_view));
   }

   pipe_sampler_view_reference(&blitter->saved_sampler_view,
                               &stage->textures[0]->base);

   blitter->saved_num_sampler_states = stage->sampler_count;
   memcpy(blitter->saved_sampler_states, stage->samplers,
          stage->sampler_count * sizeof(void *));

   blitter->saved_cs = stage->shader;
   blitter->active = true;
}

static void
asahi_compute_restore(struct agx_context *ctx)
{
   struct pipe_context *pctx = &ctx->base;
   struct asahi_blitter *blitter = &ctx->compute_blitter;

   if (blitter->has_saved_image) {
      pctx->set_shader_images(pctx, PIPE_SHADER_COMPUTE, 0, 1, 0,
                              &blitter->saved_image);
      pipe_resource_reference(&blitter->saved_image.resource, NULL);
   }

   /* take_ownership=true so do not unreference */
   pctx->set_constant_buffer(pctx, PIPE_SHADER_COMPUTE, 0, true,
                             &blitter->saved_cb);
   blitter->saved_cb.buffer = NULL;

   if (blitter->saved_sampler_view) {
      pctx->set_sampler_views(pctx, PIPE_SHADER_COMPUTE, 0, 1, 0, true,
                              &blitter->saved_sampler_view);

      blitter->saved_sampler_view = NULL;
   }

   if (blitter->saved_num_sampler_states) {
      pctx->bind_sampler_states(pctx, PIPE_SHADER_COMPUTE, 0,
                                blitter->saved_num_sampler_states,
                                blitter->saved_sampler_states);
   }

   pctx->bind_compute_state(pctx, blitter->saved_cs);
   blitter->saved_cs = NULL;
   blitter->active = false;
}

static void
asahi_compute_blit(struct pipe_context *ctx, const struct pipe_blit_info *info,
                   struct asahi_blitter *blitter)
{
   if (info->src.box.width == 0 || info->src.box.height == 0 ||
       info->dst.box.width == 0 || info->dst.box.height == 0)
      return;

   assert(asahi_compute_blit_supported(info));
   asahi_compute_save(agx_context(ctx));

   unsigned depth = info->dst.box.depth;
   bool array = depth > 1;

   struct pipe_resource *src = info->src.resource;
   struct pipe_resource *dst = info->dst.resource;
   struct pipe_sampler_view src_templ = {0}, *src_view;

   float src_width = (float)u_minify(src->width0, info->src.level);
   float src_height = (float)u_minify(src->height0, info->src.level);

   float x_scale =
      (info->src.box.width / (float)info->dst.box.width) / src_width;

   float y_scale =
      (info->src.box.height / (float)info->dst.box.height) / src_height;

   /* Expand the grid so destinations are in tiles */
   unsigned expanded_x0 = info->dst.box.x & ~(TILE_WIDTH - 1);
   unsigned expanded_y0 = info->dst.box.y & ~(TILE_HEIGHT - 1);
   unsigned expanded_x1 =
      align(info->dst.box.x + info->dst.box.width, TILE_WIDTH);
   unsigned expanded_y1 =
      align(info->dst.box.y + info->dst.box.height, TILE_HEIGHT);

   /* But clamp to the destination size to save some redundant threads */
   expanded_x1 =
      MIN2(expanded_x1, u_minify(info->dst.resource->width0, info->dst.level));
   expanded_y1 =
      MIN2(expanded_y1, u_minify(info->dst.resource->height0, info->dst.level));

   /* Calculate the width/height based on the expanded grid */
   unsigned width = expanded_x1 - expanded_x0;
   unsigned height = expanded_y1 - expanded_y0;

   unsigned data[] = {
      fui(0.5f * x_scale + (float)info->src.box.x / src_width),
      fui(0.5f * y_scale + (float)info->src.box.y / src_height),
      fui(x_scale),
      fui(y_scale),
      info->dst.box.x,
      info->dst.box.y,
      info->dst.box.width,
      info->dst.box.height,
   };

   struct pipe_constant_buffer cb = {
      .buffer_size = sizeof(data),
      .user_buffer = data,
   };
   ctx->set_constant_buffer(ctx, PIPE_SHADER_COMPUTE, 0, false, &cb);

   struct pipe_image_view image = {
      .resource = dst,
      .access = PIPE_IMAGE_ACCESS_WRITE | PIPE_IMAGE_ACCESS_DRIVER_INTERNAL,
      .shader_access = PIPE_IMAGE_ACCESS_WRITE,
      .format = info->dst.format,
      .u.tex.level = info->dst.level,
      .u.tex.first_layer = info->dst.box.z,
      .u.tex.last_layer = info->dst.box.z + depth - 1,
      .u.tex.single_layer_view = !array,
   };
   ctx->set_shader_images(ctx, PIPE_SHADER_COMPUTE, 0, 1, 0, &image);

   if (!blitter->sampler[info->filter]) {
      struct pipe_sampler_state sampler_state = {
         .wrap_s = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
         .wrap_t = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
         .wrap_r = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
         .min_img_filter = info->filter,
         .mag_img_filter = info->filter,
         .compare_func = PIPE_FUNC_ALWAYS,
         .seamless_cube_map = true,
         .max_lod = 31.0f,
      };

      blitter->sampler[info->filter] =
         ctx->create_sampler_state(ctx, &sampler_state);
   }

   ctx->bind_sampler_states(ctx, PIPE_SHADER_COMPUTE, 0, 1,
                            &blitter->sampler[info->filter]);

   /* Initialize the sampler view. */
   u_sampler_view_default_template(&src_templ, src, src->format);
   src_templ.format = info->src.format;
   src_templ.target = array ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D;
   src_templ.swizzle_r = PIPE_SWIZZLE_X;
   src_templ.swizzle_g = PIPE_SWIZZLE_Y;
   src_templ.swizzle_b = PIPE_SWIZZLE_Z;
   src_templ.swizzle_a = PIPE_SWIZZLE_W;
   src_templ.u.tex.first_layer = info->src.box.z;
   src_templ.u.tex.last_layer = info->src.box.z + depth - 1;
   src_templ.u.tex.first_level = info->src.level;
   src_templ.u.tex.last_level = info->src.level;
   src_view = ctx->create_sampler_view(ctx, src, &src_templ);
   ctx->set_sampler_views(ctx, PIPE_SHADER_COMPUTE, 0, 1, 0, true, &src_view);

   struct asahi_blit_key key = {
      .src_format = info->src.format,
      .dst_format = info->dst.format,
      .array = array,
      .aligned = info->dst.box.width == width && info->dst.box.height == height,
   };
   struct hash_entry *ent = _mesa_hash_table_search(blitter->blit_cs, &key);
   void *cs = NULL;

   if (ent) {
      cs = ent->data;
   } else {
      cs = asahi_blit_compute_shader(ctx, &key);
      _mesa_hash_table_insert(
         blitter->blit_cs, ralloc_memdup(blitter->blit_cs, &key, sizeof(key)),
         cs);
   }

   assert(cs != NULL);
   ctx->bind_compute_state(ctx, cs);

   struct pipe_grid_info grid_info = {
      .block = {TILE_WIDTH, TILE_HEIGHT, 1},
      .last_block = {width % TILE_WIDTH, height % TILE_HEIGHT, 1},
      .grid =
         {
            DIV_ROUND_UP(width, TILE_WIDTH),
            DIV_ROUND_UP(height, TILE_HEIGHT),
            depth,
         },
   };
   ctx->launch_grid(ctx, &grid_info);
   ctx->set_shader_images(ctx, PIPE_SHADER_COMPUTE, 0, 0, 1, NULL);
   ctx->set_constant_buffer(ctx, PIPE_SHADER_COMPUTE, 0, false, NULL);
   ctx->set_sampler_views(ctx, PIPE_SHADER_COMPUTE, 0, 0, 1, false, NULL);

   asahi_compute_restore(agx_context(ctx));
}

void
agx_blitter_save(struct agx_context *ctx, struct blitter_context *blitter,
                 bool render_cond)
{
   util_blitter_save_vertex_buffers(blitter, ctx->vertex_buffers,
                                    util_last_bit(ctx->vb_mask));
   util_blitter_save_vertex_elements(blitter, ctx->attributes);
   util_blitter_save_vertex_shader(blitter,
                                   ctx->stage[PIPE_SHADER_VERTEX].shader);
   util_blitter_save_tessctrl_shader(blitter,
                                     ctx->stage[PIPE_SHADER_TESS_CTRL].shader);
   util_blitter_save_tesseval_shader(blitter,
                                     ctx->stage[PIPE_SHADER_TESS_EVAL].shader);
   util_blitter_save_geometry_shader(blitter,
                                     ctx->stage[PIPE_SHADER_GEOMETRY].shader);
   util_blitter_save_rasterizer(blitter, ctx->rast);
   util_blitter_save_viewport(blitter, &ctx->viewport[0]);
   util_blitter_save_scissor(blitter, &ctx->scissor[0]);
   util_blitter_save_fragment_shader(blitter,
                                     ctx->stage[PIPE_SHADER_FRAGMENT].shader);
   util_blitter_save_blend(blitter, ctx->blend);
   util_blitter_save_depth_stencil_alpha(blitter, ctx->zs);
   util_blitter_save_stencil_ref(blitter, &ctx->stencil_ref);
   util_blitter_save_so_targets(blitter, ctx->streamout.num_targets,
                                ctx->streamout.targets);
   util_blitter_save_sample_mask(blitter, ctx->sample_mask, 0);

   util_blitter_save_framebuffer(blitter, &ctx->framebuffer);
   util_blitter_save_fragment_sampler_states(
      blitter, ctx->stage[PIPE_SHADER_FRAGMENT].sampler_count,
      (void **)(ctx->stage[PIPE_SHADER_FRAGMENT].samplers));
   util_blitter_save_fragment_sampler_views(
      blitter, ctx->stage[PIPE_SHADER_FRAGMENT].texture_count,
      (struct pipe_sampler_view **)ctx->stage[PIPE_SHADER_FRAGMENT].textures);
   util_blitter_save_fragment_constant_buffer_slot(
      blitter, ctx->stage[PIPE_SHADER_FRAGMENT].cb);

   if (!render_cond) {
      util_blitter_save_render_condition(blitter,
                                         (struct pipe_query *)ctx->cond_query,
                                         ctx->cond_cond, ctx->cond_mode);
   }
}

void
agx_blit(struct pipe_context *pipe, const struct pipe_blit_info *info)
{
   struct agx_context *ctx = agx_context(pipe);

   if (info->render_condition_enable && !agx_render_condition_check(ctx))
      return;

   /* Legalize compression /before/ calling into u_blitter to avoid recursion.
    * u_blitter bans recursive usage.
    */
   agx_legalize_compression(ctx, agx_resource(info->dst.resource),
                            info->dst.format);

   agx_legalize_compression(ctx, agx_resource(info->src.resource),
                            info->src.format);

   if (asahi_compute_blit_supported(info)) {
      asahi_compute_blit(pipe, info, &ctx->compute_blitter);
      return;
   }

   if (!util_blitter_is_blit_supported(ctx->blitter, info)) {
      fprintf(stderr, "\n");
      util_dump_blit_info(stderr, info);
      fprintf(stderr, "\n\n");
      unreachable("Unsupported blit");
   }

   /* Handle self-blits */
   agx_flush_writer(ctx, agx_resource(info->dst.resource), "Blit");

   agx_blitter_save(ctx, ctx->blitter, info->render_condition_enable);
   util_blitter_blit(ctx->blitter, info, NULL);
}

static bool
try_copy_via_blit(struct pipe_context *pctx, struct pipe_resource *dst,
                  unsigned dst_level, unsigned dstx, unsigned dsty,
                  unsigned dstz, struct pipe_resource *src, unsigned src_level,
                  const struct pipe_box *src_box)
{
   struct agx_context *ctx = agx_context(pctx);

   if (dst->target == PIPE_BUFFER)
      return false;

   /* TODO: Handle these for rusticl copies */
   if (dst->target != src->target)
      return false;

   struct pipe_blit_info info = {
      .dst =
         {
            .resource = dst,
            .level = dst_level,
            .box.x = dstx,
            .box.y = dsty,
            .box.z = dstz,
            .box.width = src_box->width,
            .box.height = src_box->height,
            .box.depth = src_box->depth,
            .format = dst->format,
         },
      .src =
         {
            .resource = src,
            .level = src_level,
            .box = *src_box,
            .format = src->format,
         },
      .mask = util_format_get_mask(src->format),
      .filter = PIPE_TEX_FILTER_NEAREST,
      .scissor_enable = 0,
   };

   /* snorm formats don't round trip, so don't use them for copies */
   if (util_format_is_snorm(info.dst.format))
      info.dst.format = util_format_snorm_to_sint(info.dst.format);

   if (util_format_is_snorm(info.src.format))
      info.src.format = util_format_snorm_to_sint(info.src.format);

   if (util_blitter_is_blit_supported(ctx->blitter, &info) &&
       info.dst.format == info.src.format) {

      agx_blit(pctx, &info);
      return true;
   } else {
      return false;
   }
}

void
agx_resource_copy_region(struct pipe_context *pctx, struct pipe_resource *dst,
                         unsigned dst_level, unsigned dstx, unsigned dsty,
                         unsigned dstz, struct pipe_resource *src,
                         unsigned src_level, const struct pipe_box *src_box)
{
   if (try_copy_via_blit(pctx, dst, dst_level, dstx, dsty, dstz, src, src_level,
                         src_box))
      return;

   /* CPU fallback */
   util_resource_copy_region(pctx, dst, dst_level, dstx, dsty, dstz, src,
                             src_level, src_box);
}
