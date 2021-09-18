/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2015 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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

#include "si_compute.h"
#include "si_pipe.h"
#include "util/format/u_format.h"
#include "util/u_log.h"
#include "util/u_surface.h"

enum
{
   SI_COPY =
      SI_SAVE_FRAMEBUFFER | SI_SAVE_TEXTURES | SI_SAVE_FRAGMENT_STATE | SI_DISABLE_RENDER_COND,

   SI_BLIT = SI_SAVE_FRAMEBUFFER | SI_SAVE_TEXTURES | SI_SAVE_FRAGMENT_STATE,

   SI_DECOMPRESS = SI_SAVE_FRAMEBUFFER | SI_SAVE_FRAGMENT_STATE | SI_DISABLE_RENDER_COND,

   SI_COLOR_RESOLVE = SI_SAVE_FRAMEBUFFER | SI_SAVE_FRAGMENT_STATE
};

void si_blitter_begin(struct si_context *sctx, enum si_blitter_op op)
{
   util_blitter_save_vertex_shader(sctx->blitter, sctx->shader.vs.cso);
   util_blitter_save_tessctrl_shader(sctx->blitter, sctx->shader.tcs.cso);
   util_blitter_save_tesseval_shader(sctx->blitter, sctx->shader.tes.cso);
   util_blitter_save_geometry_shader(sctx->blitter, sctx->shader.gs.cso);
   util_blitter_save_so_targets(sctx->blitter, sctx->streamout.num_targets,
                                (struct pipe_stream_output_target **)sctx->streamout.targets);
   util_blitter_save_rasterizer(sctx->blitter, sctx->queued.named.rasterizer);

   if (op & SI_SAVE_FRAGMENT_STATE) {
      util_blitter_save_blend(sctx->blitter, sctx->queued.named.blend);
      util_blitter_save_depth_stencil_alpha(sctx->blitter, sctx->queued.named.dsa);
      util_blitter_save_stencil_ref(sctx->blitter, &sctx->stencil_ref.state);
      util_blitter_save_fragment_shader(sctx->blitter, sctx->shader.ps.cso);
      util_blitter_save_sample_mask(sctx->blitter, sctx->sample_mask);
      util_blitter_save_scissor(sctx->blitter, &sctx->scissors[0]);
      util_blitter_save_window_rectangles(sctx->blitter, sctx->window_rectangles_include,
                                          sctx->num_window_rectangles, sctx->window_rectangles);
   }

   if (op & SI_SAVE_FRAMEBUFFER)
      util_blitter_save_framebuffer(sctx->blitter, &sctx->framebuffer.state);

   if (op & SI_SAVE_TEXTURES) {
      util_blitter_save_fragment_sampler_states(
         sctx->blitter, 2, (void **)sctx->samplers[PIPE_SHADER_FRAGMENT].sampler_states);

      util_blitter_save_fragment_sampler_views(sctx->blitter, 2,
                                               sctx->samplers[PIPE_SHADER_FRAGMENT].views);
   }

   if (op & SI_DISABLE_RENDER_COND)
      sctx->render_cond_enabled = false;

   if (sctx->screen->dpbb_allowed) {
      sctx->dpbb_force_off = true;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.dpbb_state);
   }

   sctx->blitter_running = true;
}

void si_blitter_end(struct si_context *sctx)
{
   sctx->blitter_running = false;

   if (sctx->screen->dpbb_allowed) {
      sctx->dpbb_force_off = false;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.dpbb_state);
   }

   sctx->render_cond_enabled = sctx->render_cond;

   /* Restore shader pointers because the VS blit shader changed all
    * non-global VS user SGPRs. */
   sctx->shader_pointers_dirty |= SI_DESCS_SHADER_MASK(VERTEX);

   unsigned num_vbos_in_user_sgprs = si_num_vbos_in_user_sgprs(sctx->screen);
   sctx->vertex_buffer_pointer_dirty = sctx->vb_descriptors_buffer != NULL &&
                                       sctx->num_vertex_elements >
                                       num_vbos_in_user_sgprs;
   sctx->vertex_buffer_user_sgprs_dirty = sctx->num_vertex_elements > 0 &&
                                          num_vbos_in_user_sgprs;
   si_mark_atom_dirty(sctx, &sctx->atoms.s.shader_pointers);
}

static unsigned u_max_sample(struct pipe_resource *r)
{
   return r->nr_samples ? r->nr_samples - 1 : 0;
}

static unsigned si_blit_dbcb_copy(struct si_context *sctx, struct si_texture *src,
                                  struct si_texture *dst, unsigned planes, unsigned level_mask,
                                  unsigned first_layer, unsigned last_layer, unsigned first_sample,
                                  unsigned last_sample)
{
   struct pipe_surface surf_tmpl = {{0}};
   unsigned layer, sample, checked_last_layer, max_layer;
   unsigned fully_copied_levels = 0;

   if (planes & PIPE_MASK_Z)
      sctx->dbcb_depth_copy_enabled = true;
   if (planes & PIPE_MASK_S)
      sctx->dbcb_stencil_copy_enabled = true;
   si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);

   assert(sctx->dbcb_depth_copy_enabled || sctx->dbcb_stencil_copy_enabled);

   sctx->decompression_enabled = true;

   while (level_mask) {
      unsigned level = u_bit_scan(&level_mask);

      /* The smaller the mipmap level, the less layers there are
       * as far as 3D textures are concerned. */
      max_layer = util_max_layer(&src->buffer.b.b, level);
      checked_last_layer = MIN2(last_layer, max_layer);

      surf_tmpl.u.tex.level = level;

      for (layer = first_layer; layer <= checked_last_layer; layer++) {
         struct pipe_surface *zsurf, *cbsurf;

         surf_tmpl.format = src->buffer.b.b.format;
         surf_tmpl.u.tex.first_layer = layer;
         surf_tmpl.u.tex.last_layer = layer;

         zsurf = sctx->b.create_surface(&sctx->b, &src->buffer.b.b, &surf_tmpl);

         surf_tmpl.format = dst->buffer.b.b.format;
         cbsurf = sctx->b.create_surface(&sctx->b, &dst->buffer.b.b, &surf_tmpl);

         for (sample = first_sample; sample <= last_sample; sample++) {
            if (sample != sctx->dbcb_copy_sample) {
               sctx->dbcb_copy_sample = sample;
               si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);
            }

            si_blitter_begin(sctx, SI_DECOMPRESS);
            util_blitter_custom_depth_stencil(sctx->blitter, zsurf, cbsurf, 1 << sample,
                                              sctx->custom_dsa_flush, 1.0f);
            si_blitter_end(sctx);
         }

         pipe_surface_reference(&zsurf, NULL);
         pipe_surface_reference(&cbsurf, NULL);
      }

      if (first_layer == 0 && last_layer >= max_layer && first_sample == 0 &&
          last_sample >= u_max_sample(&src->buffer.b.b))
         fully_copied_levels |= 1u << level;
   }

   sctx->decompression_enabled = false;
   sctx->dbcb_depth_copy_enabled = false;
   sctx->dbcb_stencil_copy_enabled = false;
   si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);

   return fully_copied_levels;
}

/* Helper function for si_blit_decompress_zs_in_place.
 */
static void si_blit_decompress_zs_planes_in_place(struct si_context *sctx,
                                                  struct si_texture *texture, unsigned planes,
                                                  unsigned level_mask, unsigned first_layer,
                                                  unsigned last_layer)
{
   struct pipe_surface *zsurf, surf_tmpl = {{0}};
   unsigned layer, max_layer, checked_last_layer;
   unsigned fully_decompressed_mask = 0;

   if (!level_mask)
      return;

   if (planes & PIPE_MASK_S)
      sctx->db_flush_stencil_inplace = true;
   if (planes & PIPE_MASK_Z)
      sctx->db_flush_depth_inplace = true;
   si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);

   surf_tmpl.format = texture->buffer.b.b.format;

   sctx->decompression_enabled = true;

   while (level_mask) {
      unsigned level = u_bit_scan(&level_mask);

      surf_tmpl.u.tex.level = level;

      /* The smaller the mipmap level, the less layers there are
       * as far as 3D textures are concerned. */
      max_layer = util_max_layer(&texture->buffer.b.b, level);
      checked_last_layer = MIN2(last_layer, max_layer);

      for (layer = first_layer; layer <= checked_last_layer; layer++) {
         surf_tmpl.u.tex.first_layer = layer;
         surf_tmpl.u.tex.last_layer = layer;

         zsurf = sctx->b.create_surface(&sctx->b, &texture->buffer.b.b, &surf_tmpl);

         si_blitter_begin(sctx, SI_DECOMPRESS);
         util_blitter_custom_depth_stencil(sctx->blitter, zsurf, NULL, ~0, sctx->custom_dsa_flush,
                                           1.0f);
         si_blitter_end(sctx);

         pipe_surface_reference(&zsurf, NULL);
      }

      /* The texture will always be dirty if some layers aren't flushed.
       * I don't think this case occurs often though. */
      if (first_layer == 0 && last_layer >= max_layer) {
         fully_decompressed_mask |= 1u << level;
      }
   }

   if (planes & PIPE_MASK_Z)
      texture->dirty_level_mask &= ~fully_decompressed_mask;
   if (planes & PIPE_MASK_S)
      texture->stencil_dirty_level_mask &= ~fully_decompressed_mask;

   sctx->decompression_enabled = false;
   sctx->db_flush_depth_inplace = false;
   sctx->db_flush_stencil_inplace = false;
   si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);
}

/* Helper function of si_flush_depth_texture: decompress the given levels
 * of Z and/or S planes in place.
 */
static void si_blit_decompress_zs_in_place(struct si_context *sctx, struct si_texture *texture,
                                           unsigned levels_z, unsigned levels_s,
                                           unsigned first_layer, unsigned last_layer)
{
   unsigned both = levels_z & levels_s;

   /* First, do combined Z & S decompresses for levels that need it. */
   if (both) {
      si_blit_decompress_zs_planes_in_place(sctx, texture, PIPE_MASK_Z | PIPE_MASK_S, both,
                                            first_layer, last_layer);
      levels_z &= ~both;
      levels_s &= ~both;
   }

   /* Now do separate Z and S decompresses. */
   if (levels_z) {
      si_blit_decompress_zs_planes_in_place(sctx, texture, PIPE_MASK_Z, levels_z, first_layer,
                                            last_layer);
   }

   if (levels_s) {
      si_blit_decompress_zs_planes_in_place(sctx, texture, PIPE_MASK_S, levels_s, first_layer,
                                            last_layer);
   }
}

static void si_decompress_depth(struct si_context *sctx, struct si_texture *tex,
                                unsigned required_planes, unsigned first_level, unsigned last_level,
                                unsigned first_layer, unsigned last_layer)
{
   unsigned inplace_planes = 0;
   unsigned copy_planes = 0;
   unsigned level_mask = u_bit_consecutive(first_level, last_level - first_level + 1);
   unsigned levels_z = 0;
   unsigned levels_s = 0;

   if (required_planes & PIPE_MASK_Z) {
      levels_z = level_mask & tex->dirty_level_mask;

      if (levels_z) {
         if (si_can_sample_zs(tex, false))
            inplace_planes |= PIPE_MASK_Z;
         else
            copy_planes |= PIPE_MASK_Z;
      }
   }
   if (required_planes & PIPE_MASK_S) {
      levels_s = level_mask & tex->stencil_dirty_level_mask;

      if (levels_s) {
         if (si_can_sample_zs(tex, true))
            inplace_planes |= PIPE_MASK_S;
         else
            copy_planes |= PIPE_MASK_S;
      }
   }

   if (unlikely(sctx->log))
      u_log_printf(sctx->log,
                   "\n------------------------------------------------\n"
                   "Decompress Depth (levels %u - %u, levels Z: 0x%x S: 0x%x)\n\n",
                   first_level, last_level, levels_z, levels_s);

   /* We may have to allocate the flushed texture here when called from
    * si_decompress_subresource.
    */
   if (copy_planes &&
       (tex->flushed_depth_texture || si_init_flushed_depth_texture(&sctx->b, &tex->buffer.b.b))) {
      struct si_texture *dst = tex->flushed_depth_texture;
      unsigned fully_copied_levels;
      unsigned levels = 0;

      assert(tex->flushed_depth_texture);

      if (util_format_is_depth_and_stencil(dst->buffer.b.b.format))
         copy_planes = PIPE_MASK_Z | PIPE_MASK_S;

      if (copy_planes & PIPE_MASK_Z) {
         levels |= levels_z;
         levels_z = 0;
      }
      if (copy_planes & PIPE_MASK_S) {
         levels |= levels_s;
         levels_s = 0;
      }

      fully_copied_levels = si_blit_dbcb_copy(sctx, tex, dst, copy_planes, levels, first_layer,
                                              last_layer, 0, u_max_sample(&tex->buffer.b.b));

      if (copy_planes & PIPE_MASK_Z)
         tex->dirty_level_mask &= ~fully_copied_levels;
      if (copy_planes & PIPE_MASK_S)
         tex->stencil_dirty_level_mask &= ~fully_copied_levels;
   }

   if (inplace_planes) {
      bool has_htile = si_htile_enabled(tex, first_level, inplace_planes);
      bool tc_compat_htile = vi_tc_compat_htile_enabled(tex, first_level, inplace_planes);

      /* Don't decompress if there is no HTILE or when HTILE is
       * TC-compatible. */
      if (has_htile && !tc_compat_htile) {
         si_blit_decompress_zs_in_place(sctx, tex, levels_z, levels_s, first_layer, last_layer);
      } else {
         /* This is only a cache flush.
          *
          * Only clear the mask that we are flushing, because
          * si_make_DB_shader_coherent() treats different levels
          * and depth and stencil differently.
          */
         if (inplace_planes & PIPE_MASK_Z)
            tex->dirty_level_mask &= ~levels_z;
         if (inplace_planes & PIPE_MASK_S)
            tex->stencil_dirty_level_mask &= ~levels_s;
      }

      /* We just had to completely decompress Z/S for texturing. Enable
       * TC-compatible HTILE on the next clear, so that the decompression
       * doesn't have to be done for this texture ever again.
       *
       * TC-compatible HTILE might slightly reduce Z/S performance, but
       * the decompression is much worse.
       */
      if (has_htile && !tc_compat_htile &&
          /* We can only transition the whole buffer in one clear, so no mipmapping: */
          tex->buffer.b.b.last_level == 0 &&
          tex->surface.flags & RADEON_SURF_TC_COMPATIBLE_HTILE &&
          (inplace_planes & PIPE_MASK_Z || !tex->htile_stencil_disabled))
         tex->enable_tc_compatible_htile_next_clear = true;

      /* Only in-place decompression needs to flush DB caches, or
       * when we don't decompress but TC-compatible planes are dirty.
       */
      si_make_DB_shader_coherent(sctx, tex->buffer.b.b.nr_samples, inplace_planes & PIPE_MASK_S,
                                 tc_compat_htile);
   }
   /* set_framebuffer_state takes care of coherency for single-sample.
    * The DB->CB copy uses CB for the final writes.
    */
   if (copy_planes && tex->buffer.b.b.nr_samples > 1)
      si_make_CB_shader_coherent(sctx, tex->buffer.b.b.nr_samples, false, true /* no DCC */);
}

static void si_decompress_sampler_depth_textures(struct si_context *sctx,
                                                 struct si_samplers *textures)
{
   unsigned i;
   unsigned mask = textures->needs_depth_decompress_mask;

   while (mask) {
      struct pipe_sampler_view *view;
      struct si_sampler_view *sview;
      struct si_texture *tex;

      i = u_bit_scan(&mask);

      view = textures->views[i];
      assert(view);
      sview = (struct si_sampler_view *)view;

      tex = (struct si_texture *)view->texture;
      assert(tex->db_compatible);

      si_decompress_depth(sctx, tex, sview->is_stencil_sampler ? PIPE_MASK_S : PIPE_MASK_Z,
                          view->u.tex.first_level, view->u.tex.last_level, 0,
                          util_max_layer(&tex->buffer.b.b, view->u.tex.first_level));
   }
}

static void si_blit_decompress_color(struct si_context *sctx, struct si_texture *tex,
                                     unsigned first_level, unsigned last_level,
                                     unsigned first_layer, unsigned last_layer,
                                     bool need_dcc_decompress, bool need_fmask_expand)
{
   void *custom_blend;
   unsigned layer, checked_last_layer, max_layer;
   unsigned level_mask = u_bit_consecutive(first_level, last_level - first_level + 1);

   if (!need_dcc_decompress)
      level_mask &= tex->dirty_level_mask;
   if (!level_mask)
      goto expand_fmask;

   if (unlikely(sctx->log))
      u_log_printf(sctx->log,
                   "\n------------------------------------------------\n"
                   "Decompress Color (levels %u - %u, mask 0x%x)\n\n",
                   first_level, last_level, level_mask);

   if (need_dcc_decompress) {
      assert(sctx->chip_class == GFX8 || tex->buffer.b.b.nr_storage_samples >= 2);
      custom_blend = sctx->custom_blend_dcc_decompress;

      assert(vi_dcc_enabled(tex, first_level));

      /* disable levels without DCC */
      for (int i = first_level; i <= last_level; i++) {
         if (!vi_dcc_enabled(tex, i))
            level_mask &= ~(1 << i);
      }
   } else if (tex->surface.fmask_size) {
      custom_blend = sctx->custom_blend_fmask_decompress;
   } else {
      custom_blend = sctx->custom_blend_eliminate_fastclear;
   }

   sctx->decompression_enabled = true;

   while (level_mask) {
      unsigned level = u_bit_scan(&level_mask);

      /* The smaller the mipmap level, the less layers there are
       * as far as 3D textures are concerned. */
      max_layer = util_max_layer(&tex->buffer.b.b, level);
      checked_last_layer = MIN2(last_layer, max_layer);

      for (layer = first_layer; layer <= checked_last_layer; layer++) {
         struct pipe_surface *cbsurf, surf_tmpl;

         surf_tmpl.format = tex->buffer.b.b.format;
         surf_tmpl.u.tex.level = level;
         surf_tmpl.u.tex.first_layer = layer;
         surf_tmpl.u.tex.last_layer = layer;
         cbsurf = sctx->b.create_surface(&sctx->b, &tex->buffer.b.b, &surf_tmpl);

         /* Required before and after FMASK and DCC_DECOMPRESS. */
         if (custom_blend == sctx->custom_blend_fmask_decompress ||
             custom_blend == sctx->custom_blend_dcc_decompress)
            sctx->flags |= SI_CONTEXT_FLUSH_AND_INV_CB;

         si_blitter_begin(sctx, SI_DECOMPRESS);
         util_blitter_custom_color(sctx->blitter, cbsurf, custom_blend);
         si_blitter_end(sctx);

         if (custom_blend == sctx->custom_blend_fmask_decompress ||
             custom_blend == sctx->custom_blend_dcc_decompress)
            sctx->flags |= SI_CONTEXT_FLUSH_AND_INV_CB;

         /* When running FMASK decompresion with DCC, we need to run the "eliminate fast clear" pass
          * separately because FMASK decompression doesn't eliminate DCC fast clear. This makes
          * render->texture transitions more expensive. It can be disabled by
          * allow_dcc_msaa_clear_to_reg_for_bpp.
          *
          * TODO: When we get here, change the compression to TC-compatible on the next clear
          *       to disable both the FMASK decompression and fast clear elimination passes.
          */
         if (sctx->screen->allow_dcc_msaa_clear_to_reg_for_bpp[util_logbase2(tex->surface.bpe)] &&
             custom_blend == sctx->custom_blend_fmask_decompress &&
             vi_dcc_enabled(tex, level)) {
            si_blitter_begin(sctx, SI_DECOMPRESS);
            util_blitter_custom_color(sctx->blitter, cbsurf, sctx->custom_blend_eliminate_fastclear);
            si_blitter_end(sctx);
         }

         pipe_surface_reference(&cbsurf, NULL);
      }

      /* The texture will always be dirty if some layers aren't flushed.
       * I don't think this case occurs often though. */
      if (first_layer == 0 && last_layer >= max_layer) {
         tex->dirty_level_mask &= ~(1 << level);
      }
   }

   sctx->decompression_enabled = false;
   si_make_CB_shader_coherent(sctx, tex->buffer.b.b.nr_samples, vi_dcc_enabled(tex, first_level),
                              tex->surface.u.gfx9.color.dcc.pipe_aligned);

expand_fmask:
   if (need_fmask_expand && tex->surface.fmask_offset && !tex->fmask_is_identity) {
      si_compute_expand_fmask(&sctx->b, &tex->buffer.b.b);
      tex->fmask_is_identity = true;
   }
}

static void si_decompress_color_texture(struct si_context *sctx, struct si_texture *tex,
                                        unsigned first_level, unsigned last_level,
                                        bool need_fmask_expand)
{
   /* CMASK or DCC can be discarded and we can still end up here. */
   if (!tex->cmask_buffer && !tex->surface.fmask_size &&
       !vi_dcc_enabled(tex, first_level))
      return;

   si_blit_decompress_color(sctx, tex, first_level, last_level, 0,
                            util_max_layer(&tex->buffer.b.b, first_level), false,
                            need_fmask_expand);
}

static void si_decompress_sampler_color_textures(struct si_context *sctx,
                                                 struct si_samplers *textures)
{
   unsigned i;
   unsigned mask = textures->needs_color_decompress_mask;

   while (mask) {
      struct pipe_sampler_view *view;
      struct si_texture *tex;

      i = u_bit_scan(&mask);

      view = textures->views[i];
      assert(view);

      tex = (struct si_texture *)view->texture;

      si_decompress_color_texture(sctx, tex, view->u.tex.first_level, view->u.tex.last_level,
                                  false);
   }
}

static void si_decompress_image_color_textures(struct si_context *sctx, struct si_images *images)
{
   unsigned i;
   unsigned mask = images->needs_color_decompress_mask;

   while (mask) {
      const struct pipe_image_view *view;
      struct si_texture *tex;

      i = u_bit_scan(&mask);

      view = &images->views[i];
      assert(view->resource->target != PIPE_BUFFER);

      tex = (struct si_texture *)view->resource;

      si_decompress_color_texture(sctx, tex, view->u.tex.level, view->u.tex.level,
                                  view->access & PIPE_IMAGE_ACCESS_WRITE);
   }
}

static void si_check_render_feedback_texture(struct si_context *sctx, struct si_texture *tex,
                                             unsigned first_level, unsigned last_level,
                                             unsigned first_layer, unsigned last_layer)
{
   bool render_feedback = false;

   if (!vi_dcc_enabled(tex, first_level))
      return;

   for (unsigned j = 0; j < sctx->framebuffer.state.nr_cbufs; ++j) {
      struct si_surface *surf;

      if (!sctx->framebuffer.state.cbufs[j])
         continue;

      surf = (struct si_surface *)sctx->framebuffer.state.cbufs[j];

      if (tex == (struct si_texture *)surf->base.texture && surf->base.u.tex.level >= first_level &&
          surf->base.u.tex.level <= last_level && surf->base.u.tex.first_layer <= last_layer &&
          surf->base.u.tex.last_layer >= first_layer) {
         render_feedback = true;
         break;
      }
   }

   if (render_feedback)
      si_texture_disable_dcc(sctx, tex);
}

static void si_check_render_feedback_textures(struct si_context *sctx, struct si_samplers *textures,
                                              uint32_t in_use_mask)
{
   uint32_t mask = textures->enabled_mask & in_use_mask;

   while (mask) {
      const struct pipe_sampler_view *view;
      struct si_texture *tex;

      unsigned i = u_bit_scan(&mask);

      view = textures->views[i];
      if (view->texture->target == PIPE_BUFFER)
         continue;

      tex = (struct si_texture *)view->texture;

      si_check_render_feedback_texture(sctx, tex, view->u.tex.first_level, view->u.tex.last_level,
                                       view->u.tex.first_layer, view->u.tex.last_layer);
   }
}

static void si_check_render_feedback_images(struct si_context *sctx, struct si_images *images,
                                            uint32_t in_use_mask)
{
   uint32_t mask = images->enabled_mask & in_use_mask;

   while (mask) {
      const struct pipe_image_view *view;
      struct si_texture *tex;

      unsigned i = u_bit_scan(&mask);

      view = &images->views[i];
      if (view->resource->target == PIPE_BUFFER)
         continue;

      tex = (struct si_texture *)view->resource;

      si_check_render_feedback_texture(sctx, tex, view->u.tex.level, view->u.tex.level,
                                       view->u.tex.first_layer, view->u.tex.last_layer);
   }
}

static void si_check_render_feedback_resident_textures(struct si_context *sctx)
{
   util_dynarray_foreach (&sctx->resident_tex_handles, struct si_texture_handle *, tex_handle) {
      struct pipe_sampler_view *view;
      struct si_texture *tex;

      view = (*tex_handle)->view;
      if (view->texture->target == PIPE_BUFFER)
         continue;

      tex = (struct si_texture *)view->texture;

      si_check_render_feedback_texture(sctx, tex, view->u.tex.first_level, view->u.tex.last_level,
                                       view->u.tex.first_layer, view->u.tex.last_layer);
   }
}

static void si_check_render_feedback_resident_images(struct si_context *sctx)
{
   util_dynarray_foreach (&sctx->resident_img_handles, struct si_image_handle *, img_handle) {
      struct pipe_image_view *view;
      struct si_texture *tex;

      view = &(*img_handle)->view;
      if (view->resource->target == PIPE_BUFFER)
         continue;

      tex = (struct si_texture *)view->resource;

      si_check_render_feedback_texture(sctx, tex, view->u.tex.level, view->u.tex.level,
                                       view->u.tex.first_layer, view->u.tex.last_layer);
   }
}

static void si_check_render_feedback(struct si_context *sctx)
{
   if (!sctx->need_check_render_feedback)
      return;

   /* There is no render feedback if color writes are disabled.
    * (e.g. a pixel shader with image stores)
    */
   if (!si_get_total_colormask(sctx))
      return;

   for (int i = 0; i < SI_NUM_GRAPHICS_SHADERS; ++i) {
      if (!sctx->shaders[i].cso)
         continue;

      struct si_shader_info *info = &sctx->shaders[i].cso->info;
      si_check_render_feedback_images(sctx, &sctx->images[i],
                                      u_bit_consecutive(0, info->base.num_images));
      si_check_render_feedback_textures(sctx, &sctx->samplers[i],
                                        info->base.textures_used[0]);
   }

   si_check_render_feedback_resident_images(sctx);
   si_check_render_feedback_resident_textures(sctx);

   sctx->need_check_render_feedback = false;
}

static void si_decompress_resident_textures(struct si_context *sctx)
{
   util_dynarray_foreach (&sctx->resident_tex_needs_color_decompress, struct si_texture_handle *,
                          tex_handle) {
      struct pipe_sampler_view *view = (*tex_handle)->view;
      struct si_texture *tex = (struct si_texture *)view->texture;

      si_decompress_color_texture(sctx, tex, view->u.tex.first_level, view->u.tex.last_level,
                                  false);
   }

   util_dynarray_foreach (&sctx->resident_tex_needs_depth_decompress, struct si_texture_handle *,
                          tex_handle) {
      struct pipe_sampler_view *view = (*tex_handle)->view;
      struct si_sampler_view *sview = (struct si_sampler_view *)view;
      struct si_texture *tex = (struct si_texture *)view->texture;

      si_decompress_depth(sctx, tex, sview->is_stencil_sampler ? PIPE_MASK_S : PIPE_MASK_Z,
                          view->u.tex.first_level, view->u.tex.last_level, 0,
                          util_max_layer(&tex->buffer.b.b, view->u.tex.first_level));
   }
}

static void si_decompress_resident_images(struct si_context *sctx)
{
   util_dynarray_foreach (&sctx->resident_img_needs_color_decompress, struct si_image_handle *,
                          img_handle) {
      struct pipe_image_view *view = &(*img_handle)->view;
      struct si_texture *tex = (struct si_texture *)view->resource;

      si_decompress_color_texture(sctx, tex, view->u.tex.level, view->u.tex.level,
                                  view->access & PIPE_IMAGE_ACCESS_WRITE);
   }
}

void si_decompress_textures(struct si_context *sctx, unsigned shader_mask)
{
   unsigned compressed_colortex_counter, mask;

   if (sctx->blitter_running)
      return;

   /* Update the compressed_colortex_mask if necessary. */
   compressed_colortex_counter = p_atomic_read(&sctx->screen->compressed_colortex_counter);
   if (compressed_colortex_counter != sctx->last_compressed_colortex_counter) {
      sctx->last_compressed_colortex_counter = compressed_colortex_counter;
      si_update_needs_color_decompress_masks(sctx);
   }

   /* Decompress color & depth textures if needed. */
   mask = sctx->shader_needs_decompress_mask & shader_mask;
   while (mask) {
      unsigned i = u_bit_scan(&mask);

      if (sctx->samplers[i].needs_depth_decompress_mask) {
         si_decompress_sampler_depth_textures(sctx, &sctx->samplers[i]);
      }
      if (sctx->samplers[i].needs_color_decompress_mask) {
         si_decompress_sampler_color_textures(sctx, &sctx->samplers[i]);
      }
      if (sctx->images[i].needs_color_decompress_mask) {
         si_decompress_image_color_textures(sctx, &sctx->images[i]);
      }
   }

   if (shader_mask & u_bit_consecutive(0, SI_NUM_GRAPHICS_SHADERS)) {
      if (sctx->uses_bindless_samplers)
         si_decompress_resident_textures(sctx);
      if (sctx->uses_bindless_images)
         si_decompress_resident_images(sctx);

      if (sctx->ps_uses_fbfetch) {
         struct pipe_surface *cb0 = sctx->framebuffer.state.cbufs[0];
         si_decompress_color_texture(sctx, (struct si_texture *)cb0->texture,
                                     cb0->u.tex.first_layer, cb0->u.tex.last_layer, false);
      }

      si_check_render_feedback(sctx);
   } else if (shader_mask & (1 << PIPE_SHADER_COMPUTE)) {
      if (sctx->cs_shader_state.program->sel.info.uses_bindless_samplers)
         si_decompress_resident_textures(sctx);
      if (sctx->cs_shader_state.program->sel.info.uses_bindless_images)
         si_decompress_resident_images(sctx);
   }
}

/* Helper for decompressing a portion of a color or depth resource before
 * blitting if any decompression is needed.
 * The driver doesn't decompress resources automatically while u_blitter is
 * rendering. */
void si_decompress_subresource(struct pipe_context *ctx, struct pipe_resource *tex, unsigned planes,
                               unsigned level, unsigned first_layer, unsigned last_layer)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_texture *stex = (struct si_texture *)tex;

   if (stex->db_compatible) {
      planes &= PIPE_MASK_Z | PIPE_MASK_S;

      if (!stex->surface.has_stencil)
         planes &= ~PIPE_MASK_S;

      /* If we've rendered into the framebuffer and it's a blitting
       * source, make sure the decompression pass is invoked
       * by dirtying the framebuffer.
       */
      if (sctx->framebuffer.state.zsbuf && sctx->framebuffer.state.zsbuf->u.tex.level == level &&
          sctx->framebuffer.state.zsbuf->texture == tex)
         si_update_fb_dirtiness_after_rendering(sctx);

      si_decompress_depth(sctx, stex, planes, level, level, first_layer, last_layer);
   } else if (stex->surface.fmask_size || stex->cmask_buffer ||
              vi_dcc_enabled(stex, level)) {
      /* If we've rendered into the framebuffer and it's a blitting
       * source, make sure the decompression pass is invoked
       * by dirtying the framebuffer.
       */
      for (unsigned i = 0; i < sctx->framebuffer.state.nr_cbufs; i++) {
         if (sctx->framebuffer.state.cbufs[i] &&
             sctx->framebuffer.state.cbufs[i]->u.tex.level == level &&
             sctx->framebuffer.state.cbufs[i]->texture == tex) {
            si_update_fb_dirtiness_after_rendering(sctx);
            break;
         }
      }

      si_blit_decompress_color(sctx, stex, level, level, first_layer, last_layer, false, false);
   }
}

struct texture_orig_info {
   unsigned format;
   unsigned width0;
   unsigned height0;
   unsigned npix_x;
   unsigned npix_y;
   unsigned npix0_x;
   unsigned npix0_y;
};

static void si_use_compute_copy_for_float_formats(struct si_context *sctx,
                                                  struct pipe_resource *texture,
                                                  unsigned level) {
   struct si_texture *tex = (struct si_texture *)texture;

   /* If we are uploading into FP16 or R11G11B10_FLOAT via a blit, CB clobbers NaNs,
    * so in order to preserve them exactly, we have to use the compute blit.
    * The compute blit is used only when the destination doesn't have DCC, so
    * disable it here, which is kinda a hack.
    * If we are uploading into 32-bit floats with DCC via a blit, NaNs will also get
    * lost so we need to disable DCC as well.
    *
    * This makes KHR-GL45.texture_view.view_classes pass on gfx9.
    */
   if (vi_dcc_enabled(tex, level) &&
       util_format_is_float(texture->format) &&
       sctx->chip_class < GFX10) {
      si_texture_disable_dcc(sctx, tex);
   }
}

void si_resource_copy_region(struct pipe_context *ctx, struct pipe_resource *dst,
                             unsigned dst_level, unsigned dstx, unsigned dsty, unsigned dstz,
                             struct pipe_resource *src, unsigned src_level,
                             const struct pipe_box *src_box)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_texture *ssrc = (struct si_texture *)src;
   struct si_texture *sdst = (struct si_texture *)dst;
   struct pipe_surface *dst_view, dst_templ;
   struct pipe_sampler_view src_templ, *src_view;
   unsigned dst_width, dst_height, src_width0, src_height0;
   unsigned dst_width0, dst_height0, src_force_level = 0;
   struct pipe_box sbox, dstbox;

   /* Handle buffers first. */
   if (dst->target == PIPE_BUFFER && src->target == PIPE_BUFFER) {
      si_copy_buffer(sctx, dst, src, dstx, src_box->x, src_box->width, SI_OP_SYNC_BEFORE_AFTER);
      return;
   }

   si_use_compute_copy_for_float_formats(sctx, dst, dst_level);

   if (!util_format_is_compressed(src->format) && !util_format_is_compressed(dst->format) &&
       !util_format_is_depth_or_stencil(src->format) && src->nr_samples <= 1 &&
       /* DCC compression from image store is enabled for GFX10+. */
       (!vi_dcc_enabled(sdst, dst_level) || sctx->chip_class >= GFX10) &&
       !(dst->target != src->target &&
         (src->target == PIPE_TEXTURE_1D_ARRAY || dst->target == PIPE_TEXTURE_1D_ARRAY))) {
      si_compute_copy_image(sctx, dst, dst_level, src, src_level, dstx, dsty, dstz,
                            src_box, false, SI_OP_SYNC_BEFORE_AFTER);
      return;
   }

   assert(u_max_sample(dst) == u_max_sample(src));

   /* The driver doesn't decompress resources automatically while
    * u_blitter is rendering. */
   si_decompress_subresource(ctx, src, PIPE_MASK_RGBAZS, src_level, src_box->z,
                             src_box->z + src_box->depth - 1);

   dst_width = u_minify(dst->width0, dst_level);
   dst_height = u_minify(dst->height0, dst_level);
   dst_width0 = dst->width0;
   dst_height0 = dst->height0;
   src_width0 = src->width0;
   src_height0 = src->height0;

   util_blitter_default_dst_texture(&dst_templ, dst, dst_level, dstz);
   util_blitter_default_src_texture(sctx->blitter, &src_templ, src, src_level);

   if (util_format_is_compressed(src->format) || util_format_is_compressed(dst->format)) {
      unsigned blocksize = ssrc->surface.bpe;

      if (blocksize == 8)
         src_templ.format = PIPE_FORMAT_R16G16B16A16_UINT; /* 64-bit block */
      else
         src_templ.format = PIPE_FORMAT_R32G32B32A32_UINT; /* 128-bit block */
      dst_templ.format = src_templ.format;

      dst_width = util_format_get_nblocksx(dst->format, dst_width);
      dst_height = util_format_get_nblocksy(dst->format, dst_height);
      dst_width0 = util_format_get_nblocksx(dst->format, dst_width0);
      dst_height0 = util_format_get_nblocksy(dst->format, dst_height0);
      src_width0 = util_format_get_nblocksx(src->format, src_width0);
      src_height0 = util_format_get_nblocksy(src->format, src_height0);

      dstx = util_format_get_nblocksx(dst->format, dstx);
      dsty = util_format_get_nblocksy(dst->format, dsty);

      sbox.x = util_format_get_nblocksx(src->format, src_box->x);
      sbox.y = util_format_get_nblocksy(src->format, src_box->y);
      sbox.z = src_box->z;
      sbox.width = util_format_get_nblocksx(src->format, src_box->width);
      sbox.height = util_format_get_nblocksy(src->format, src_box->height);
      sbox.depth = src_box->depth;
      src_box = &sbox;

      src_force_level = src_level;
   } else if (!util_blitter_is_copy_supported(sctx->blitter, dst, src)) {
      if (util_format_is_subsampled_422(src->format)) {
         src_templ.format = PIPE_FORMAT_R8G8B8A8_UINT;
         dst_templ.format = PIPE_FORMAT_R8G8B8A8_UINT;

         dst_width = util_format_get_nblocksx(dst->format, dst_width);
         dst_width0 = util_format_get_nblocksx(dst->format, dst_width0);
         src_width0 = util_format_get_nblocksx(src->format, src_width0);

         dstx = util_format_get_nblocksx(dst->format, dstx);

         sbox = *src_box;
         sbox.x = util_format_get_nblocksx(src->format, src_box->x);
         sbox.width = util_format_get_nblocksx(src->format, src_box->width);
         src_box = &sbox;
      } else {
         unsigned blocksize = ssrc->surface.bpe;

         switch (blocksize) {
         case 1:
            dst_templ.format = PIPE_FORMAT_R8_UNORM;
            src_templ.format = PIPE_FORMAT_R8_UNORM;
            break;
         case 2:
            dst_templ.format = PIPE_FORMAT_R8G8_UNORM;
            src_templ.format = PIPE_FORMAT_R8G8_UNORM;
            break;
         case 4:
            dst_templ.format = PIPE_FORMAT_R8G8B8A8_UNORM;
            src_templ.format = PIPE_FORMAT_R8G8B8A8_UNORM;
            break;
         case 8:
            dst_templ.format = PIPE_FORMAT_R16G16B16A16_UINT;
            src_templ.format = PIPE_FORMAT_R16G16B16A16_UINT;
            break;
         case 16:
            dst_templ.format = PIPE_FORMAT_R32G32B32A32_UINT;
            src_templ.format = PIPE_FORMAT_R32G32B32A32_UINT;
            break;
         default:
            fprintf(stderr, "Unhandled format %s with blocksize %u\n",
                    util_format_short_name(src->format), blocksize);
            assert(0);
         }
      }
   }

   /* SNORM8 blitting has precision issues on some chips. Use the SINT
    * equivalent instead, which doesn't force DCC decompression.
    */
   if (util_format_is_snorm8(dst_templ.format)) {
      dst_templ.format = src_templ.format = util_format_snorm8_to_sint8(dst_templ.format);
   }

   vi_disable_dcc_if_incompatible_format(sctx, dst, dst_level, dst_templ.format);
   vi_disable_dcc_if_incompatible_format(sctx, src, src_level, src_templ.format);

   /* Initialize the surface. */
   dst_view = si_create_surface_custom(ctx, dst, &dst_templ, dst_width0, dst_height0, dst_width,
                                       dst_height);

   /* Initialize the sampler view. */
   src_view =
      si_create_sampler_view_custom(ctx, src, &src_templ, src_width0, src_height0, src_force_level);

   u_box_3d(dstx, dsty, dstz, abs(src_box->width), abs(src_box->height), abs(src_box->depth),
            &dstbox);

   /* Copy. */
   si_blitter_begin(sctx, SI_COPY);
   util_blitter_blit_generic(sctx->blitter, dst_view, &dstbox, src_view, src_box, src_width0,
                             src_height0, PIPE_MASK_RGBAZS, PIPE_TEX_FILTER_NEAREST, NULL, false, false);
   si_blitter_end(sctx);

   pipe_surface_reference(&dst_view, NULL);
   pipe_sampler_view_reference(&src_view, NULL);
}

static void si_do_CB_resolve(struct si_context *sctx, const struct pipe_blit_info *info,
                             struct pipe_resource *dst, unsigned dst_level, unsigned dst_z,
                             enum pipe_format format)
{
   /* Required before and after CB_RESOLVE. */
   sctx->flags |= SI_CONTEXT_FLUSH_AND_INV_CB;

   si_blitter_begin(
      sctx, SI_COLOR_RESOLVE | (info->render_condition_enable ? 0 : SI_DISABLE_RENDER_COND));
   util_blitter_custom_resolve_color(sctx->blitter, dst, dst_level, dst_z, info->src.resource,
                                     info->src.box.z, ~0, sctx->custom_blend_resolve, format);
   si_blitter_end(sctx);

   /* Flush caches for possible texturing. */
   si_make_CB_shader_coherent(sctx, 1, false, true /* no DCC */);
}

static bool resolve_formats_compatible(enum pipe_format src, enum pipe_format dst,
                                       bool src_swaps_rgb_to_bgr, bool *need_rgb_to_bgr)
{
   *need_rgb_to_bgr = false;

   if (src_swaps_rgb_to_bgr) {
      /* We must only check the swapped format. */
      enum pipe_format swapped_src = util_format_rgb_to_bgr(src);
      assert(swapped_src);
      return util_is_format_compatible(util_format_description(swapped_src),
                                       util_format_description(dst));
   }

   if (util_is_format_compatible(util_format_description(src), util_format_description(dst)))
      return true;

   enum pipe_format swapped_src = util_format_rgb_to_bgr(src);
   *need_rgb_to_bgr = util_is_format_compatible(util_format_description(swapped_src),
                                                util_format_description(dst));
   return *need_rgb_to_bgr;
}

static bool do_hardware_msaa_resolve(struct pipe_context *ctx, const struct pipe_blit_info *info)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_texture *src = (struct si_texture *)info->src.resource;
   struct si_texture *dst = (struct si_texture *)info->dst.resource;
   ASSERTED struct si_texture *stmp;
   unsigned dst_width = u_minify(info->dst.resource->width0, info->dst.level);
   unsigned dst_height = u_minify(info->dst.resource->height0, info->dst.level);
   enum pipe_format format = info->src.format;
   struct pipe_resource *tmp, templ;
   struct pipe_blit_info blit;

   /* Check basic requirements for hw resolve. */
   if (!(info->src.resource->nr_samples > 1 && info->dst.resource->nr_samples <= 1 &&
         !util_format_is_pure_integer(format) && !util_format_is_depth_or_stencil(format) &&
         util_max_layer(info->src.resource, 0) == 0))
      return false;

   /* Hardware MSAA resolve doesn't work if SPI format = NORM16_ABGR and
    * the format is R16G16. Use R16A16, which does work.
    */
   if (format == PIPE_FORMAT_R16G16_UNORM)
      format = PIPE_FORMAT_R16A16_UNORM;
   if (format == PIPE_FORMAT_R16G16_SNORM)
      format = PIPE_FORMAT_R16A16_SNORM;

   bool need_rgb_to_bgr = false;

   /* Check the remaining requirements for hw resolve. */
   if (util_max_layer(info->dst.resource, info->dst.level) == 0 && !info->scissor_enable &&
       (info->mask & PIPE_MASK_RGBA) == PIPE_MASK_RGBA &&
       resolve_formats_compatible(info->src.format, info->dst.format,
                                  src->swap_rgb_to_bgr, &need_rgb_to_bgr) &&
       dst_width == info->src.resource->width0 && dst_height == info->src.resource->height0 &&
       info->dst.box.x == 0 && info->dst.box.y == 0 && info->dst.box.width == dst_width &&
       info->dst.box.height == dst_height && info->dst.box.depth == 1 && info->src.box.x == 0 &&
       info->src.box.y == 0 && info->src.box.width == dst_width &&
       info->src.box.height == dst_height && info->src.box.depth == 1 && !dst->surface.is_linear &&
       (!dst->cmask_buffer || !dst->dirty_level_mask)) { /* dst cannot be fast-cleared */
      /* Check the remaining constraints. */
      if (src->surface.micro_tile_mode != dst->surface.micro_tile_mode ||
          need_rgb_to_bgr) {
         /* The next fast clear will switch to this mode to
          * get direct hw resolve next time if the mode is
          * different now.
          *
          * TODO-GFX10: This does not work in GFX10 because MSAA
          * is restricted to 64KB_R_X and 64KB_Z_X swizzle modes.
          * In some cases we could change the swizzle of the
          * destination texture instead, but the more general
          * solution is to implement compute shader resolve.
          */
         if (src->surface.micro_tile_mode != dst->surface.micro_tile_mode)
            src->last_msaa_resolve_target_micro_mode = dst->surface.micro_tile_mode;
         if (need_rgb_to_bgr)
            src->swap_rgb_to_bgr_on_next_clear = true;

         goto resolve_to_temp;
      }

      /* Resolving into a surface with DCC is unsupported. Since
       * it's being overwritten anyway, clear it to uncompressed.
       * This is still the fastest codepath even with this clear.
       */
      if (vi_dcc_enabled(dst, info->dst.level)) {
         struct si_clear_info clear_info;

         if (!vi_dcc_get_clear_info(sctx, dst, info->dst.level, DCC_UNCOMPRESSED, &clear_info))
            goto resolve_to_temp;

         si_execute_clears(sctx, &clear_info, 1, SI_CLEAR_TYPE_DCC);
         dst->dirty_level_mask &= ~(1 << info->dst.level);
      }

      /* Resolve directly from src to dst. */
      si_do_CB_resolve(sctx, info, info->dst.resource, info->dst.level, info->dst.box.z, format);
      return true;
   }

resolve_to_temp:
   /* Shader-based resolve is VERY SLOW. Instead, resolve into
    * a temporary texture and blit.
    */
   memset(&templ, 0, sizeof(templ));
   templ.target = PIPE_TEXTURE_2D;
   templ.format = info->src.resource->format;
   templ.width0 = info->src.resource->width0;
   templ.height0 = info->src.resource->height0;
   templ.depth0 = 1;
   templ.array_size = 1;
   templ.usage = PIPE_USAGE_DEFAULT;
   templ.flags = SI_RESOURCE_FLAG_FORCE_MSAA_TILING | SI_RESOURCE_FLAG_FORCE_MICRO_TILE_MODE |
                 SI_RESOURCE_FLAG_MICRO_TILE_MODE_SET(src->surface.micro_tile_mode) |
                 SI_RESOURCE_FLAG_DISABLE_DCC | SI_RESOURCE_FLAG_DRIVER_INTERNAL;

   /* The src and dst microtile modes must be the same. */
   if (sctx->chip_class <= GFX8 && src->surface.micro_tile_mode == RADEON_MICRO_MODE_DISPLAY)
      templ.bind = PIPE_BIND_SCANOUT;
   else
      templ.bind = 0;

   tmp = ctx->screen->resource_create(ctx->screen, &templ);
   if (!tmp)
      return false;
   stmp = (struct si_texture *)tmp;
   /* Match the channel order of src. */
   stmp->swap_rgb_to_bgr = src->swap_rgb_to_bgr;

   assert(!stmp->surface.is_linear);
   assert(src->surface.micro_tile_mode == stmp->surface.micro_tile_mode);

   /* resolve */
   si_do_CB_resolve(sctx, info, tmp, 0, 0, format);

   /* blit */
   blit = *info;
   blit.src.resource = tmp;
   blit.src.box.z = 0;

   si_blitter_begin(sctx, SI_BLIT | (info->render_condition_enable ? 0 : SI_DISABLE_RENDER_COND));
   util_blitter_blit(sctx->blitter, &blit);
   si_blitter_end(sctx);

   pipe_resource_reference(&tmp, NULL);
   return true;
}

static void si_blit(struct pipe_context *ctx, const struct pipe_blit_info *info)
{
   struct si_context *sctx = (struct si_context *)ctx;

   if (do_hardware_msaa_resolve(ctx, info)) {
      return;
   }

   if (unlikely(sctx->thread_trace_enabled))
      sctx->sqtt_next_event = EventCmdCopyImage;

   /* Using compute for copying to a linear texture in GTT is much faster than
    * going through RBs (render backends). This improves DRI PRIME performance.
    */
   if (util_can_blit_via_copy_region(info, false)) {
      si_resource_copy_region(ctx, info->dst.resource, info->dst.level,
                              info->dst.box.x, info->dst.box.y, info->dst.box.z,
                              info->src.resource, info->src.level, &info->src.box);
      return;
   }

   assert(util_blitter_is_blit_supported(sctx->blitter, info));

   /* The driver doesn't decompress resources automatically while
    * u_blitter is rendering. */
   vi_disable_dcc_if_incompatible_format(sctx, info->src.resource, info->src.level,
                                         info->src.format);
   vi_disable_dcc_if_incompatible_format(sctx, info->dst.resource, info->dst.level,
                                         info->dst.format);
   si_decompress_subresource(ctx, info->src.resource, PIPE_MASK_RGBAZS, info->src.level,
                             info->src.box.z, info->src.box.z + info->src.box.depth - 1);

   if (unlikely(sctx->thread_trace_enabled))
      sctx->sqtt_next_event = EventCmdBlitImage;

   si_blitter_begin(sctx, SI_BLIT | (info->render_condition_enable ? 0 : SI_DISABLE_RENDER_COND));
   util_blitter_blit(sctx->blitter, info);
   si_blitter_end(sctx);
}

static bool si_generate_mipmap(struct pipe_context *ctx, struct pipe_resource *tex,
                               enum pipe_format format, unsigned base_level, unsigned last_level,
                               unsigned first_layer, unsigned last_layer)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_texture *stex = (struct si_texture *)tex;

   if (!util_blitter_is_copy_supported(sctx->blitter, tex, tex))
      return false;

   /* The driver doesn't decompress resources automatically while
    * u_blitter is rendering. */
   vi_disable_dcc_if_incompatible_format(sctx, tex, base_level, format);
   si_decompress_subresource(ctx, tex, PIPE_MASK_RGBAZS, base_level, first_layer, last_layer);

   /* Clear dirty_level_mask for the levels that will be overwritten. */
   assert(base_level < last_level);
   stex->dirty_level_mask &= ~u_bit_consecutive(base_level + 1, last_level - base_level);

   sctx->generate_mipmap_for_depth = stex->is_depth;

   si_blitter_begin(sctx, SI_BLIT | SI_DISABLE_RENDER_COND);
   util_blitter_generate_mipmap(sctx->blitter, tex, format, base_level, last_level, first_layer,
                                last_layer);
   si_blitter_end(sctx);

   sctx->generate_mipmap_for_depth = false;
   return true;
}

static void si_flush_resource(struct pipe_context *ctx, struct pipe_resource *res)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_texture *tex = (struct si_texture *)res;

   assert(res->target != PIPE_BUFFER);

   if (!tex->is_depth && (tex->cmask_buffer || vi_dcc_enabled(tex, 0))) {
      si_blit_decompress_color(sctx, tex, 0, res->last_level, 0, util_max_layer(res, 0),
                               false, false);

      if (tex->surface.display_dcc_offset && tex->displayable_dcc_dirty) {
         si_retile_dcc(sctx, tex);
         tex->displayable_dcc_dirty = false;
      }
   }
}

void si_flush_implicit_resources(struct si_context *sctx)
{
   hash_table_foreach(sctx->dirty_implicit_resources, entry) {
      si_flush_resource(&sctx->b, entry->data);
      pipe_resource_reference((struct pipe_resource **)&entry->data, NULL);
   }
   _mesa_hash_table_clear(sctx->dirty_implicit_resources, NULL);
}

void si_decompress_dcc(struct si_context *sctx, struct si_texture *tex)
{
   assert(!tex->is_depth);

   /* If graphics is disabled, we can't decompress DCC, but it shouldn't
    * be compressed either. The caller should simply discard it.
    */
   if (!tex->surface.meta_offset || !sctx->has_graphics)
      return;

   if (sctx->chip_class == GFX8 || tex->buffer.b.b.nr_storage_samples >= 2) {
      si_blit_decompress_color(sctx, tex, 0, tex->buffer.b.b.last_level, 0,
                               util_max_layer(&tex->buffer.b.b, 0), true, false);
   } else {
      struct pipe_resource *ptex = &tex->buffer.b.b;
      assert(ptex->nr_storage_samples <= 1);

      /* DCC decompression using a compute shader. */
      for (unsigned level = 0; level < tex->surface.num_meta_levels; level++) {
         struct pipe_box box;

         u_box_3d(0, 0, 0, u_minify(ptex->width0, level),
                  u_minify(ptex->height0, level),
                  util_num_layers(ptex, level), &box);
         si_compute_copy_image(sctx, ptex, level, ptex, level, 0, 0, 0, &box, true,
                               /* Sync before the first copy and after the last copy */
                               (level == 0 ? SI_OP_SYNC_BEFORE : 0) |
                               (level == tex->surface.num_meta_levels - 1 ? SI_OP_SYNC_AFTER : 0));
      }

      /* Now clear DCC metadata to uncompressed.
       *
       * This uses SI_COMPUTE_CLEAR_METHOD to avoid a failure when running this
       * deqp caselist on gfx10:
       *  dEQP-GLES31.functional.image_load_store.2d.format_reinterpret.rgba32f_rgba32ui
       *  dEQP-GLES31.functional.image_load_store.2d.format_reinterpret.rgba32f_rgba32i
       */
      uint32_t clear_value = DCC_UNCOMPRESSED;
      si_clear_buffer(sctx, ptex, tex->surface.meta_offset,
                      tex->surface.meta_size, &clear_value, 4, SI_OP_SYNC_AFTER,
                      SI_COHERENCY_CB_META, SI_COMPUTE_CLEAR_METHOD);
      si_mark_display_dcc_dirty(sctx, tex);

      /* Clearing DCC metadata requires flushing L2 and invalidating L2 metadata to make
       * the metadata visible to L2 caches. This is because clear_buffer uses plain stores
       * that can go to different L2 channels than where L2 metadata caches expect them.
       * This is not done for fast clears because plain stores are visible to CB/DB. Only
       * L2 metadata caches have the problem.
       */
      sctx->flags |= SI_CONTEXT_WB_L2 | SI_CONTEXT_INV_L2_METADATA;
   }
}

void si_init_blit_functions(struct si_context *sctx)
{
   sctx->b.resource_copy_region = si_resource_copy_region;

   if (sctx->has_graphics) {
      sctx->b.blit = si_blit;
      sctx->b.flush_resource = si_flush_resource;
      sctx->b.generate_mipmap = si_generate_mipmap;
   }
}
