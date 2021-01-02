/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#include "si_pipe.h"
#include "sid.h"
#include "util/format/u_format.h"
#include "util/u_pack_color.h"
#include "util/u_surface.h"

enum
{
   SI_CLEAR = SI_SAVE_FRAGMENT_STATE,
   SI_CLEAR_SURFACE = SI_SAVE_FRAMEBUFFER | SI_SAVE_FRAGMENT_STATE,
};

static void si_alloc_separate_cmask(struct si_screen *sscreen, struct si_texture *tex)
{
   /* CMASK for MSAA is allocated in advance or always disabled
    * by "nofmask" option.
    */
   if (tex->cmask_buffer || !tex->surface.cmask_size || tex->buffer.b.b.nr_samples >= 2)
      return;

   tex->cmask_buffer =
      si_aligned_buffer_create(&sscreen->b, SI_RESOURCE_FLAG_UNMAPPABLE, PIPE_USAGE_DEFAULT,
                               tex->surface.cmask_size, tex->surface.cmask_alignment);
   if (tex->cmask_buffer == NULL)
      return;

   tex->cmask_base_address_reg = tex->cmask_buffer->gpu_address >> 8;
   tex->cb_color_info |= S_028C70_FAST_CLEAR(1);

   p_atomic_inc(&sscreen->compressed_colortex_counter);
}

static bool si_set_clear_color(struct si_texture *tex, enum pipe_format surface_format,
                               const union pipe_color_union *color)
{
   union util_color uc;

   memset(&uc, 0, sizeof(uc));

   if (tex->surface.bpe == 16) {
      /* DCC fast clear only:
       *   CLEAR_WORD0 = R = G = B
       *   CLEAR_WORD1 = A
       */
      assert(color->ui[0] == color->ui[1] && color->ui[0] == color->ui[2]);
      uc.ui[0] = color->ui[0];
      uc.ui[1] = color->ui[3];
   } else {
      util_pack_color_union(surface_format, &uc, color);
   }

   if (memcmp(tex->color_clear_value, &uc, 2 * sizeof(uint32_t)) == 0)
      return false;

   memcpy(tex->color_clear_value, &uc, 2 * sizeof(uint32_t));
   return true;
}

/** Linearize and convert luminace/intensity to red. */
enum pipe_format si_simplify_cb_format(enum pipe_format format)
{
   format = util_format_linear(format);
   format = util_format_luminance_to_red(format);
   return util_format_intensity_to_red(format);
}

bool vi_alpha_is_on_msb(struct si_screen *sscreen, enum pipe_format format)
{
   format = si_simplify_cb_format(format);
   const struct util_format_description *desc = util_format_description(format);

   /* Formats with 3 channels can't have alpha. */
   if (desc->nr_channels == 3)
      return true; /* same as xxxA; is any value OK here? */

   if (sscreen->info.chip_class >= GFX10 && desc->nr_channels == 1)
      return desc->swizzle[3] == PIPE_SWIZZLE_X;

   return si_translate_colorswap(format, false) <= 1;
}

static bool vi_get_fast_clear_parameters(struct si_screen *sscreen, enum pipe_format base_format,
                                         enum pipe_format surface_format,
                                         const union pipe_color_union *color, uint32_t *clear_value,
                                         bool *eliminate_needed)
{
   /* If we want to clear without needing a fast clear eliminate step, we
    * can set color and alpha independently to 0 or 1 (or 0/max for integer
    * formats).
    */
   bool values[4] = {};      /* whether to clear to 0 or 1 */
   bool color_value = false; /* clear color to 0 or 1 */
   bool alpha_value = false; /* clear alpha to 0 or 1 */
   int alpha_channel;        /* index of the alpha component */
   bool has_color = false;
   bool has_alpha = false;

   const struct util_format_description *desc =
      util_format_description(si_simplify_cb_format(surface_format));

   /* 128-bit fast clear with different R,G,B values is unsupported. */
   if (desc->block.bits == 128 && (color->ui[0] != color->ui[1] || color->ui[0] != color->ui[2]))
      return false;

   *eliminate_needed = true;
   *clear_value = DCC_CLEAR_COLOR_REG;

   if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
      return true; /* need ELIMINATE_FAST_CLEAR */

   bool base_alpha_is_on_msb = vi_alpha_is_on_msb(sscreen, base_format);
   bool surf_alpha_is_on_msb = vi_alpha_is_on_msb(sscreen, surface_format);

   /* Formats with 3 channels can't have alpha. */
   if (desc->nr_channels == 3)
      alpha_channel = -1;
   else if (surf_alpha_is_on_msb)
      alpha_channel = desc->nr_channels - 1;
   else
      alpha_channel = 0;

   for (int i = 0; i < 4; ++i) {
      if (desc->swizzle[i] >= PIPE_SWIZZLE_0)
         continue;

      if (desc->channel[i].pure_integer && desc->channel[i].type == UTIL_FORMAT_TYPE_SIGNED) {
         /* Use the maximum value for clamping the clear color. */
         int max = u_bit_consecutive(0, desc->channel[i].size - 1);

         values[i] = color->i[i] != 0;
         if (color->i[i] != 0 && MIN2(color->i[i], max) != max)
            return true; /* need ELIMINATE_FAST_CLEAR */
      } else if (desc->channel[i].pure_integer &&
                 desc->channel[i].type == UTIL_FORMAT_TYPE_UNSIGNED) {
         /* Use the maximum value for clamping the clear color. */
         unsigned max = u_bit_consecutive(0, desc->channel[i].size);

         values[i] = color->ui[i] != 0U;
         if (color->ui[i] != 0U && MIN2(color->ui[i], max) != max)
            return true; /* need ELIMINATE_FAST_CLEAR */
      } else {
         values[i] = color->f[i] != 0.0F;
         if (color->f[i] != 0.0F && color->f[i] != 1.0F)
            return true; /* need ELIMINATE_FAST_CLEAR */
      }

      if (desc->swizzle[i] == alpha_channel) {
         alpha_value = values[i];
         has_alpha = true;
      } else {
         color_value = values[i];
         has_color = true;
      }
   }

   /* If alpha isn't present, make it the same as color, and vice versa. */
   if (!has_alpha)
      alpha_value = color_value;
   else if (!has_color)
      color_value = alpha_value;

   if (color_value != alpha_value && base_alpha_is_on_msb != surf_alpha_is_on_msb)
      return true; /* require ELIMINATE_FAST_CLEAR */

   /* Check if all color values are equal if they are present. */
   for (int i = 0; i < 4; ++i) {
      if (desc->swizzle[i] <= PIPE_SWIZZLE_W && desc->swizzle[i] != alpha_channel &&
          values[i] != color_value)
         return true; /* require ELIMINATE_FAST_CLEAR */
   }

   /* This doesn't need ELIMINATE_FAST_CLEAR.
    * On chips predating Raven2, the DCC clear codes and the CB clear
    * color registers must match.
    */
   *eliminate_needed = false;

   if (color_value) {
      if (alpha_value)
         *clear_value = DCC_CLEAR_COLOR_1111;
      else
         *clear_value = DCC_CLEAR_COLOR_1110;
   } else {
      if (alpha_value)
         *clear_value = DCC_CLEAR_COLOR_0001;
      else
         *clear_value = DCC_CLEAR_COLOR_0000;
   }
   return true;
}

bool vi_dcc_clear_level(struct si_context *sctx, struct si_texture *tex, unsigned level,
                        unsigned clear_value)
{
   struct pipe_resource *dcc_buffer;
   uint64_t dcc_offset, clear_size;

   assert(vi_dcc_enabled(tex, level));

   if (tex->dcc_separate_buffer) {
      dcc_buffer = &tex->dcc_separate_buffer->b.b;
      dcc_offset = 0;
   } else {
      dcc_buffer = &tex->buffer.b.b;
      dcc_offset = tex->surface.dcc_offset;
   }

   if (sctx->chip_class >= GFX9) {
      /* Mipmap level clears aren't implemented. */
      if (tex->buffer.b.b.last_level > 0)
         return false;

      /* 4x and 8x MSAA needs a sophisticated compute shader for
       * the clear. See AMDVLK. */
      if (tex->buffer.b.b.nr_storage_samples >= 4)
         return false;

      clear_size = tex->surface.dcc_size;
   } else {
      unsigned num_layers = util_num_layers(&tex->buffer.b.b, level);

      /* If this is 0, fast clear isn't possible. (can occur with MSAA) */
      if (!tex->surface.u.legacy.level[level].dcc_fast_clear_size)
         return false;

      /* Layered 4x and 8x MSAA DCC fast clears need to clear
       * dcc_fast_clear_size bytes for each layer. A compute shader
       * would be more efficient than separate per-layer clear operations.
       */
      if (tex->buffer.b.b.nr_storage_samples >= 4 && num_layers > 1)
         return false;

      dcc_offset += tex->surface.u.legacy.level[level].dcc_offset;
      clear_size = tex->surface.u.legacy.level[level].dcc_fast_clear_size * num_layers;
   }

   si_clear_buffer(sctx, dcc_buffer, dcc_offset, clear_size, &clear_value, 4, SI_COHERENCY_CB_META,
                   false);
   return true;
}

/* Set the same micro tile mode as the destination of the last MSAA resolve.
 * This allows hitting the MSAA resolve fast path, which requires that both
 * src and dst micro tile modes match.
 */
static void si_set_optimal_micro_tile_mode(struct si_screen *sscreen, struct si_texture *tex)
{
   if (sscreen->info.chip_class >= GFX10 || tex->buffer.b.is_shared ||
       tex->buffer.b.b.nr_samples <= 1 ||
       tex->surface.micro_tile_mode == tex->last_msaa_resolve_target_micro_mode)
      return;

   assert(sscreen->info.chip_class >= GFX9 ||
          tex->surface.u.legacy.level[0].mode == RADEON_SURF_MODE_2D);
   assert(tex->buffer.b.b.last_level == 0);

   if (sscreen->info.chip_class >= GFX9) {
      /* 4K or larger tiles only. 0 is linear. 1-3 are 256B tiles. */
      assert(tex->surface.u.gfx9.surf.swizzle_mode >= 4);

      /* If you do swizzle_mode % 4, you'll get:
       *   0 = Depth
       *   1 = Standard,
       *   2 = Displayable
       *   3 = Rotated
       *
       * Depth-sample order isn't allowed:
       */
      assert(tex->surface.u.gfx9.surf.swizzle_mode % 4 != 0);

      switch (tex->last_msaa_resolve_target_micro_mode) {
      case RADEON_MICRO_MODE_DISPLAY:
         tex->surface.u.gfx9.surf.swizzle_mode &= ~0x3;
         tex->surface.u.gfx9.surf.swizzle_mode += 2; /* D */
         break;
      case RADEON_MICRO_MODE_STANDARD:
         tex->surface.u.gfx9.surf.swizzle_mode &= ~0x3;
         tex->surface.u.gfx9.surf.swizzle_mode += 1; /* S */
         break;
      case RADEON_MICRO_MODE_RENDER:
         tex->surface.u.gfx9.surf.swizzle_mode &= ~0x3;
         tex->surface.u.gfx9.surf.swizzle_mode += 3; /* R */
         break;
      default: /* depth */
         assert(!"unexpected micro mode");
         return;
      }
   } else if (sscreen->info.chip_class >= GFX7) {
      /* These magic numbers were copied from addrlib. It doesn't use
       * any definitions for them either. They are all 2D_TILED_THIN1
       * modes with different bpp and micro tile mode.
       */
      switch (tex->last_msaa_resolve_target_micro_mode) {
      case RADEON_MICRO_MODE_DISPLAY:
         tex->surface.u.legacy.tiling_index[0] = 10;
         break;
      case RADEON_MICRO_MODE_STANDARD:
         tex->surface.u.legacy.tiling_index[0] = 14;
         break;
      case RADEON_MICRO_MODE_RENDER:
         tex->surface.u.legacy.tiling_index[0] = 28;
         break;
      default: /* depth, thick */
         assert(!"unexpected micro mode");
         return;
      }
   } else { /* GFX6 */
      switch (tex->last_msaa_resolve_target_micro_mode) {
      case RADEON_MICRO_MODE_DISPLAY:
         switch (tex->surface.bpe) {
         case 1:
            tex->surface.u.legacy.tiling_index[0] = 10;
            break;
         case 2:
            tex->surface.u.legacy.tiling_index[0] = 11;
            break;
         default: /* 4, 8 */
            tex->surface.u.legacy.tiling_index[0] = 12;
            break;
         }
         break;
      case RADEON_MICRO_MODE_STANDARD:
         switch (tex->surface.bpe) {
         case 1:
            tex->surface.u.legacy.tiling_index[0] = 14;
            break;
         case 2:
            tex->surface.u.legacy.tiling_index[0] = 15;
            break;
         case 4:
            tex->surface.u.legacy.tiling_index[0] = 16;
            break;
         default: /* 8, 16 */
            tex->surface.u.legacy.tiling_index[0] = 17;
            break;
         }
         break;
      default: /* depth, thick */
         assert(!"unexpected micro mode");
         return;
      }
   }

   tex->surface.micro_tile_mode = tex->last_msaa_resolve_target_micro_mode;

   p_atomic_inc(&sscreen->dirty_tex_counter);
}

static void si_do_fast_color_clear(struct si_context *sctx, unsigned *buffers,
                                   const union pipe_color_union *color)
{
   struct pipe_framebuffer_state *fb = &sctx->framebuffer.state;
   int i;

   /* This function is broken in BE, so just disable this path for now */
#if UTIL_ARCH_BIG_ENDIAN
   return;
#endif

   if (sctx->render_cond)
      return;

   for (i = 0; i < fb->nr_cbufs; i++) {
      struct si_texture *tex;
      unsigned clear_bit = PIPE_CLEAR_COLOR0 << i;

      if (!fb->cbufs[i])
         continue;

      /* if this colorbuffer is not being cleared */
      if (!(*buffers & clear_bit))
         continue;

      unsigned level = fb->cbufs[i]->u.tex.level;
      if (level > 0)
         continue;

      tex = (struct si_texture *)fb->cbufs[i]->texture;

      /* TODO: GFX9: Implement DCC fast clear for level 0 of
       * mipmapped textures. Mipmapped DCC has to clear a rectangular
       * area of DCC for level 0 (because the whole miptree is
       * organized in a 2D plane).
       */
      if (sctx->chip_class >= GFX9 && tex->buffer.b.b.last_level > 0)
         continue;

      /* the clear is allowed if all layers are bound */
      if (fb->cbufs[i]->u.tex.first_layer != 0 ||
          fb->cbufs[i]->u.tex.last_layer != util_max_layer(&tex->buffer.b.b, 0)) {
         continue;
      }

      /* only supported on tiled surfaces */
      if (tex->surface.is_linear) {
         continue;
      }

      /* shared textures can't use fast clear without an explicit flush,
       * because there is no way to communicate the clear color among
       * all clients
       */
      if (tex->buffer.b.is_shared &&
          !(tex->buffer.external_usage & PIPE_HANDLE_USAGE_EXPLICIT_FLUSH))
         continue;

      if (sctx->chip_class <= GFX8 && tex->surface.u.legacy.level[0].mode == RADEON_SURF_MODE_1D &&
          !sctx->screen->info.htile_cmask_support_1d_tiling)
         continue;

      /* Use a slow clear for small surfaces where the cost of
       * the eliminate pass can be higher than the benefit of fast
       * clear. The closed driver does this, but the numbers may differ.
       *
       * This helps on both dGPUs and APUs, even small APUs like Mullins.
       */
      bool too_small = tex->buffer.b.b.nr_samples <= 1 &&
                       tex->buffer.b.b.width0 * tex->buffer.b.b.height0 <= 512 * 512;
      bool eliminate_needed = false;
      bool fmask_decompress_needed = false;

      /* Fast clear is the most appropriate place to enable DCC for
       * displayable surfaces.
       */
      if (sctx->family == CHIP_STONEY && !too_small) {
         vi_separate_dcc_try_enable(sctx, tex);

         /* RB+ isn't supported with a CMASK clear only on Stoney,
          * so all clears are considered to be hypothetically slow
          * clears, which is weighed when determining whether to
          * enable separate DCC.
          */
         if (tex->dcc_gather_statistics) /* only for Stoney */
            tex->num_slow_clears++;
      }

      /* Try to clear DCC first, otherwise try CMASK. */
      if (vi_dcc_enabled(tex, 0)) {
         uint32_t reset_value;

         if (sctx->screen->debug_flags & DBG(NO_DCC_CLEAR))
            continue;

         if (!vi_get_fast_clear_parameters(sctx->screen, tex->buffer.b.b.format,
                                           fb->cbufs[i]->format, color, &reset_value,
                                           &eliminate_needed))
            continue;

         if (eliminate_needed && too_small)
            continue;

         /* TODO: This DCC+CMASK clear doesn't work with MSAA. */
         if (tex->buffer.b.b.nr_samples >= 2 && tex->cmask_buffer && eliminate_needed)
            continue;

         if (!vi_dcc_clear_level(sctx, tex, 0, reset_value))
            continue;

         tex->separate_dcc_dirty = true;
         tex->displayable_dcc_dirty = true;

         /* DCC fast clear with MSAA should clear CMASK to 0xC. */
         if (tex->buffer.b.b.nr_samples >= 2 && tex->cmask_buffer) {
            uint32_t clear_value = 0xCCCCCCCC;
            si_clear_buffer(sctx, &tex->cmask_buffer->b.b, tex->surface.cmask_offset,
                            tex->surface.cmask_size, &clear_value, 4, SI_COHERENCY_CB_META, false);
            fmask_decompress_needed = true;
         }
      } else {
         if (too_small)
            continue;

         /* 128-bit formats are unusupported */
         if (tex->surface.bpe > 8) {
            continue;
         }

         /* RB+ doesn't work with CMASK fast clear on Stoney. */
         if (sctx->family == CHIP_STONEY)
            continue;

         /* Disable fast clear if tex is encrypted */
         if (tex->buffer.flags & RADEON_FLAG_ENCRYPTED)
            continue;

         /* ensure CMASK is enabled */
         si_alloc_separate_cmask(sctx->screen, tex);
         if (!tex->cmask_buffer)
            continue;

         /* Do the fast clear. */
         uint32_t clear_value = 0;
         si_clear_buffer(sctx, &tex->cmask_buffer->b.b, tex->surface.cmask_offset,
                         tex->surface.cmask_size, &clear_value, 4, SI_COHERENCY_CB_META, false);
         eliminate_needed = true;
      }

      if ((eliminate_needed || fmask_decompress_needed) &&
          !(tex->dirty_level_mask & (1 << level))) {
         tex->dirty_level_mask |= 1 << level;
         p_atomic_inc(&sctx->screen->compressed_colortex_counter);
      }

      /* We can change the micro tile mode before a full clear. */
      si_set_optimal_micro_tile_mode(sctx->screen, tex);

      *buffers &= ~clear_bit;

      /* Chips with DCC constant encoding don't need to set the clear
       * color registers for DCC clear values 0 and 1.
       */
      if (sctx->screen->info.has_dcc_constant_encode && !eliminate_needed)
         continue;

      if (si_set_clear_color(tex, fb->cbufs[i]->format, color)) {
         sctx->framebuffer.dirty_cbufs |= 1 << i;
         si_mark_atom_dirty(sctx, &sctx->atoms.s.framebuffer);
      }
   }
}

static void si_clear(struct pipe_context *ctx, unsigned buffers,
                     const struct pipe_scissor_state *scissor_state,
                     const union pipe_color_union *color, double depth, unsigned stencil)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct pipe_framebuffer_state *fb = &sctx->framebuffer.state;
   struct pipe_surface *zsbuf = fb->zsbuf;
   struct si_texture *zstex = zsbuf ? (struct si_texture *)zsbuf->texture : NULL;
   bool needs_db_flush = false;

   if (buffers & PIPE_CLEAR_COLOR) {
      si_do_fast_color_clear(sctx, &buffers, color);
      if (!buffers)
         return; /* all buffers have been fast cleared */

      /* These buffers cannot use fast clear, make sure to disable expansion. */
      for (unsigned i = 0; i < fb->nr_cbufs; i++) {
         struct si_texture *tex;

         /* If not clearing this buffer, skip. */
         if (!(buffers & (PIPE_CLEAR_COLOR0 << i)) || !fb->cbufs[i])
            continue;

         tex = (struct si_texture *)fb->cbufs[i]->texture;
         if (tex->surface.fmask_size == 0)
            tex->dirty_level_mask &= ~(1 << fb->cbufs[i]->u.tex.level);
      }
   }

   if (zstex && zsbuf->u.tex.first_layer == 0 &&
       zsbuf->u.tex.last_layer == util_max_layer(&zstex->buffer.b.b, 0)) {
      /* See whether we should enable TC-compatible HTILE. */
      if (zstex->enable_tc_compatible_htile_next_clear &&
          !zstex->tc_compatible_htile &&
          si_htile_enabled(zstex, zsbuf->u.tex.level, PIPE_MASK_ZS) &&
          /* If both depth and stencil are present, they must be cleared together. */
          ((buffers & PIPE_CLEAR_DEPTHSTENCIL) == PIPE_CLEAR_DEPTHSTENCIL ||
           (buffers & PIPE_CLEAR_DEPTH && (!zstex->surface.has_stencil ||
                                           zstex->htile_stencil_disabled)))) {
         /* Enable TC-compatible HTILE. */
         zstex->enable_tc_compatible_htile_next_clear = false;
         zstex->tc_compatible_htile = true;

         /* Update the framebuffer state to reflect the change. */
         sctx->framebuffer.DB_has_shader_readable_metadata = true;
         sctx->framebuffer.dirty_zsbuf = true;
         si_mark_atom_dirty(sctx, &sctx->atoms.s.framebuffer);

         /* Update all sampler views and shader images in all contexts. */
         p_atomic_inc(&sctx->screen->dirty_tex_counter);

         /* Re-initialize HTILE, so that it doesn't contain values incompatible
          * with the new TC-compatible HTILE setting.
          *
          * 0xfffff30f = uncompressed Z + S
          * 0xfffc000f = uncompressed Z only
          *
          * GFX8 always uses the Z+S HTILE format for TC-compatible HTILE even
          * when stencil is not present.
          */
         uint32_t clear_value = (zstex->surface.has_stencil &&
                                 !zstex->htile_stencil_disabled) ||
                                sctx->chip_class == GFX8 ? 0xfffff30f : 0xfffc000f;
         si_clear_buffer(sctx, &zstex->buffer.b.b, zstex->surface.htile_offset,
                         zstex->surface.htile_size, &clear_value, 4,
                         SI_COHERENCY_DB_META, false);
      }

      /* TC-compatible HTILE only supports depth clears to 0 or 1. */
      if (buffers & PIPE_CLEAR_DEPTH && si_htile_enabled(zstex, zsbuf->u.tex.level, PIPE_MASK_Z) &&
          (!zstex->tc_compatible_htile || depth == 0 || depth == 1)) {
         /* Need to disable EXPCLEAR temporarily if clearing
          * to a new value. */
         if (!zstex->depth_cleared || zstex->depth_clear_value != depth) {
            sctx->db_depth_disable_expclear = true;
         }

         if (zstex->depth_clear_value != (float)depth) {
            if ((zstex->depth_clear_value != 0) != (depth != 0)) {
               /* ZRANGE_PRECISION register of a bound surface will change so we
                * must flush the DB caches. */
               needs_db_flush = true;
            }
            /* Update DB_DEPTH_CLEAR. */
            zstex->depth_clear_value = depth;
            sctx->framebuffer.dirty_zsbuf = true;
            si_mark_atom_dirty(sctx, &sctx->atoms.s.framebuffer);
         }
         sctx->db_depth_clear = true;
         si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);
      }

      /* TC-compatible HTILE only supports stencil clears to 0. */
      if (buffers & PIPE_CLEAR_STENCIL &&
          si_htile_enabled(zstex, zsbuf->u.tex.level, PIPE_MASK_S) &&
          (!zstex->tc_compatible_htile || stencil == 0)) {
         stencil &= 0xff;

         /* Need to disable EXPCLEAR temporarily if clearing
          * to a new value. */
         if (!zstex->stencil_cleared || zstex->stencil_clear_value != stencil) {
            sctx->db_stencil_disable_expclear = true;
         }

         if (zstex->stencil_clear_value != (uint8_t)stencil) {
            /* Update DB_STENCIL_CLEAR. */
            zstex->stencil_clear_value = stencil;
            sctx->framebuffer.dirty_zsbuf = true;
            si_mark_atom_dirty(sctx, &sctx->atoms.s.framebuffer);
         }
         sctx->db_stencil_clear = true;
         si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);
      }

      if (needs_db_flush)
         sctx->flags |= SI_CONTEXT_FLUSH_AND_INV_DB;
   }

   si_blitter_begin(sctx, SI_CLEAR);
   util_blitter_clear(sctx->blitter, fb->width, fb->height, util_framebuffer_get_num_layers(fb),
                      buffers, color, depth, stencil, sctx->framebuffer.nr_samples > 1);
   si_blitter_end(sctx);

   if (sctx->db_depth_clear) {
      sctx->db_depth_clear = false;
      sctx->db_depth_disable_expclear = false;
      zstex->depth_cleared = true;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);
   }

   if (sctx->db_stencil_clear) {
      sctx->db_stencil_clear = false;
      sctx->db_stencil_disable_expclear = false;
      zstex->stencil_cleared = true;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);
   }
}

static void si_clear_render_target(struct pipe_context *ctx, struct pipe_surface *dst,
                                   const union pipe_color_union *color, unsigned dstx,
                                   unsigned dsty, unsigned width, unsigned height,
                                   bool render_condition_enabled)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_texture *sdst = (struct si_texture *)dst->texture;

   if (dst->texture->nr_samples <= 1 && !vi_dcc_enabled(sdst, dst->u.tex.level)) {
      si_compute_clear_render_target(ctx, dst, color, dstx, dsty, width, height,
                                     render_condition_enabled);
      return;
   }

   si_blitter_begin(sctx,
                    SI_CLEAR_SURFACE | (render_condition_enabled ? 0 : SI_DISABLE_RENDER_COND));
   util_blitter_clear_render_target(sctx->blitter, dst, color, dstx, dsty, width, height);
   si_blitter_end(sctx);
}

static void si_clear_depth_stencil(struct pipe_context *ctx, struct pipe_surface *dst,
                                   unsigned clear_flags, double depth, unsigned stencil,
                                   unsigned dstx, unsigned dsty, unsigned width, unsigned height,
                                   bool render_condition_enabled)
{
   struct si_context *sctx = (struct si_context *)ctx;

   si_blitter_begin(sctx,
                    SI_CLEAR_SURFACE | (render_condition_enabled ? 0 : SI_DISABLE_RENDER_COND));
   util_blitter_clear_depth_stencil(sctx->blitter, dst, clear_flags, depth, stencil, dstx, dsty,
                                    width, height);
   si_blitter_end(sctx);
}

static void si_clear_texture(struct pipe_context *pipe, struct pipe_resource *tex, unsigned level,
                             const struct pipe_box *box, const void *data)
{
   struct pipe_screen *screen = pipe->screen;
   struct si_texture *stex = (struct si_texture *)tex;
   struct pipe_surface tmpl = {{0}};
   struct pipe_surface *sf;

   tmpl.format = tex->format;
   tmpl.u.tex.first_layer = box->z;
   tmpl.u.tex.last_layer = box->z + box->depth - 1;
   tmpl.u.tex.level = level;
   sf = pipe->create_surface(pipe, tex, &tmpl);
   if (!sf)
      return;

   if (stex->is_depth) {
      unsigned clear;
      float depth;
      uint8_t stencil = 0;

      /* Depth is always present. */
      clear = PIPE_CLEAR_DEPTH;
      util_format_unpack_z_float(tex->format, &depth, data, 1);

      if (stex->surface.has_stencil) {
         clear |= PIPE_CLEAR_STENCIL;
         util_format_unpack_s_8uint(tex->format, &stencil, data, 1);
      }

      si_clear_depth_stencil(pipe, sf, clear, depth, stencil, box->x, box->y, box->width,
                             box->height, false);
   } else {
      union pipe_color_union color;

      util_format_unpack_rgba(tex->format, color.ui, data, 1);

      if (screen->is_format_supported(screen, tex->format, tex->target, 0, 0,
                                      PIPE_BIND_RENDER_TARGET)) {
         si_clear_render_target(pipe, sf, &color, box->x, box->y, box->width, box->height, false);
      } else {
         /* Software fallback - just for R9G9B9E5_FLOAT */
         util_clear_render_target(pipe, sf, &color, box->x, box->y, box->width, box->height);
      }
   }
   pipe_surface_reference(&sf, NULL);
}

void si_init_clear_functions(struct si_context *sctx)
{
   sctx->b.clear_render_target = si_clear_render_target;
   sctx->b.clear_texture = si_clear_texture;

   if (sctx->has_graphics) {
      sctx->b.clear = si_clear;
      sctx->b.clear_depth_stencil = si_clear_depth_stencil;
   }
}
