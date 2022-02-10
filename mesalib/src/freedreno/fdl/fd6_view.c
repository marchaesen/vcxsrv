/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2021 Valve Corporation
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

#include "freedreno_layout.h"
#include "fd6_format_table.h"

static enum a6xx_tex_swiz
fdl6_swiz(unsigned char swiz)
{
   STATIC_ASSERT((unsigned) A6XX_TEX_X == (unsigned) PIPE_SWIZZLE_X);
   STATIC_ASSERT((unsigned) A6XX_TEX_Y == (unsigned) PIPE_SWIZZLE_Y);
   STATIC_ASSERT((unsigned) A6XX_TEX_Z == (unsigned) PIPE_SWIZZLE_Z);
   STATIC_ASSERT((unsigned) A6XX_TEX_W == (unsigned) PIPE_SWIZZLE_W);
   STATIC_ASSERT((unsigned) A6XX_TEX_ZERO == (unsigned) PIPE_SWIZZLE_0);
   STATIC_ASSERT((unsigned) A6XX_TEX_ONE == (unsigned) PIPE_SWIZZLE_1);
   return (enum a6xx_tex_swiz) swiz;
}

static enum a6xx_tex_type
fdl6_tex_type(enum fdl_view_type type, bool storage)
{
   STATIC_ASSERT((unsigned) FDL_VIEW_TYPE_1D == (unsigned) A6XX_TEX_1D);
   STATIC_ASSERT((unsigned) FDL_VIEW_TYPE_2D == (unsigned) A6XX_TEX_2D);
   STATIC_ASSERT((unsigned) FDL_VIEW_TYPE_CUBE == (unsigned) A6XX_TEX_CUBE);
   STATIC_ASSERT((unsigned) FDL_VIEW_TYPE_3D == (unsigned) A6XX_TEX_3D);
   STATIC_ASSERT((unsigned) FDL_VIEW_TYPE_BUFFER == (unsigned) A6XX_TEX_BUFFER);

   return (storage && type == FDL_VIEW_TYPE_CUBE) ?
      A6XX_TEX_2D : (enum a6xx_tex_type) type;
}

static uint32_t
fdl6_texswiz(const struct fdl_view_args *args, bool has_z24uint_s8uint)
{
   unsigned char format_swiz[4] =
      { PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W };
   switch (args->format) {
   case PIPE_FORMAT_R8G8_R8B8_UNORM:
   case PIPE_FORMAT_G8R8_B8R8_UNORM:
   case PIPE_FORMAT_G8_B8R8_420_UNORM:
   case PIPE_FORMAT_G8_B8_R8_420_UNORM:
      format_swiz[0] = PIPE_SWIZZLE_Z;
      format_swiz[1] = PIPE_SWIZZLE_X;
      format_swiz[2] = PIPE_SWIZZLE_Y;
      break;
   case PIPE_FORMAT_DXT1_RGB:
   case PIPE_FORMAT_DXT1_SRGB:
      /* same hardware format is used for BC1_RGB / BC1_RGBA */
      format_swiz[3] = PIPE_SWIZZLE_1;
      break;
   case PIPE_FORMAT_X24S8_UINT:
      if (!has_z24uint_s8uint) {
         /* using FMT6_8_8_8_8_UINT, so need to pick out the W channel and
          * swizzle (0,0,1) in the rest (see "Conversion to RGBA").
          */
         format_swiz[0] = PIPE_SWIZZLE_W;
         format_swiz[1] = PIPE_SWIZZLE_0;
         format_swiz[2] = PIPE_SWIZZLE_0;
         format_swiz[3] = PIPE_SWIZZLE_1;
      } else {
         /* using FMT6_Z24_UINT_S8_UINT, which is (d, s, 0, 1), so need to
          * swizzle away the d.
          */
         format_swiz[0] = PIPE_SWIZZLE_Y;
         format_swiz[1] = PIPE_SWIZZLE_0;
      }
      break;

   default:
      /* Our I, L, A, and LA formats use R or RG HW formats. */
      if (util_format_is_alpha(args->format)) {
         format_swiz[0] = PIPE_SWIZZLE_0;
         format_swiz[1] = PIPE_SWIZZLE_0;
         format_swiz[2] = PIPE_SWIZZLE_0;
         format_swiz[3] = PIPE_SWIZZLE_X;
      } else if (util_format_is_luminance(args->format)) {
         format_swiz[0] = PIPE_SWIZZLE_X;
         format_swiz[1] = PIPE_SWIZZLE_X;
         format_swiz[2] = PIPE_SWIZZLE_X;
         format_swiz[3] = PIPE_SWIZZLE_1;
      } else if (util_format_is_intensity(args->format)) {
         format_swiz[0] = PIPE_SWIZZLE_X;
         format_swiz[1] = PIPE_SWIZZLE_X;
         format_swiz[2] = PIPE_SWIZZLE_X;
         format_swiz[3] = PIPE_SWIZZLE_X;
      } else if (util_format_is_luminance_alpha(args->format)) {
         format_swiz[0] = PIPE_SWIZZLE_X;
         format_swiz[1] = PIPE_SWIZZLE_X;
         format_swiz[2] = PIPE_SWIZZLE_X;
         format_swiz[3] = PIPE_SWIZZLE_Y;
      } else if (!util_format_has_alpha(args->format)) {
         /* for rgbx, force A to 1.  Harmless for R/RG, where we already get 1. */
         format_swiz[3] = PIPE_SWIZZLE_1;
      }
      break;
   }

   unsigned char swiz[4];
   util_format_compose_swizzles(format_swiz, args->swiz, swiz);

   return A6XX_TEX_CONST_0_SWIZ_X(fdl6_swiz(swiz[0])) |
          A6XX_TEX_CONST_0_SWIZ_Y(fdl6_swiz(swiz[1])) |
          A6XX_TEX_CONST_0_SWIZ_Z(fdl6_swiz(swiz[2])) |
          A6XX_TEX_CONST_0_SWIZ_W(fdl6_swiz(swiz[3]));
}

#define COND(bool, val) ((bool) ? (val) : 0)

void
fdl6_view_init(struct fdl6_view *view, const struct fdl_layout **layouts,
               const struct fdl_view_args *args, bool has_z24uint_s8uint)
{
   const struct fdl_layout *layout = layouts[0];
   uint32_t width = u_minify(layout->width0, args->base_miplevel);
   uint32_t height = u_minify(layout->height0, args->base_miplevel);

   /* If reinterpreting a compressed format as a size-compatible uncompressed
    * format, we need width/height in blocks, and vice-versa. In vulkan this
    * includes single-plane 422 formats which util/format doesn't consider
    * "compressed" (get_compressed() returns false).
    */
   if (util_format_get_blockwidth(layout->format) > 1 &&
       util_format_get_blockwidth(args->format) == 1) {
      width = util_format_get_nblocksx(layout->format, width);
   } else if (util_format_get_blockwidth(layout->format) == 1 &&
              util_format_get_blockwidth(args->format) > 1) {
      width *= util_format_get_blockwidth(args->format);
   }

   if (util_format_get_blockheight(layout->format) > 1 &&
       util_format_get_blockheight(args->format) == 1) {
      height = util_format_get_nblocksy(layout->format, height);
   } else if (util_format_get_blockheight(layout->format) == 1 &&
              util_format_get_blockheight(args->format) > 1) {
      height *= util_format_get_blockheight(args->format);
   }

   uint32_t storage_depth = args->layer_count;
   if (args->type == FDL_VIEW_TYPE_3D) {
      storage_depth = u_minify(layout->depth0, args->base_miplevel);
   }

   uint32_t depth = storage_depth;
   if (args->type == FDL_VIEW_TYPE_CUBE) {
      /* Cubes are treated as 2D arrays for storage images, so only divide the
       * depth by 6 for the texture descriptor.
       */
      depth /= 6;
   }

   uint64_t base_addr = args->iova +
      fdl_surface_offset(layout, args->base_miplevel, args->base_array_layer);
   uint64_t ubwc_addr = args->iova +
      fdl_ubwc_offset(layout, args->base_miplevel, args->base_array_layer);

   uint32_t pitch = fdl_pitch(layout, args->base_miplevel);
   uint32_t ubwc_pitch = fdl_ubwc_pitch(layout, args->base_miplevel);
   uint32_t layer_size = fdl_layer_stride(layout, args->base_miplevel);

   enum a6xx_format texture_format =
      fd6_texture_format(args->format, layout->tile_mode);
   enum a3xx_color_swap swap =
      fd6_texture_swap(args->format, layout->tile_mode);
   enum a6xx_tile_mode tile_mode = fdl_tile_mode(layout, args->base_miplevel);

   bool ubwc_enabled = fdl_ubwc_enabled(layout, args->base_miplevel);

   bool is_d24s8 = (args->format == PIPE_FORMAT_Z24_UNORM_S8_UINT ||
                    args->format == PIPE_FORMAT_Z24X8_UNORM ||
                    args->format == PIPE_FORMAT_X24S8_UINT);

   if (args->format == PIPE_FORMAT_X24S8_UINT && has_z24uint_s8uint)
      texture_format = FMT6_Z24_UINT_S8_UINT;

   if (texture_format == FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8 && !ubwc_enabled)
      texture_format = FMT6_8_8_8_8_UNORM;

   enum a6xx_format storage_format = texture_format;
   if (is_d24s8) {
      if (ubwc_enabled)
         storage_format = FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8;
      else
         storage_format = FMT6_8_8_8_8_UNORM;
   }

   memset(view->descriptor, 0, sizeof(view->descriptor));

   view->descriptor[0] =
      A6XX_TEX_CONST_0_TILE_MODE(tile_mode) |
      COND(util_format_is_srgb(args->format), A6XX_TEX_CONST_0_SRGB) |
      A6XX_TEX_CONST_0_FMT(texture_format) |
      A6XX_TEX_CONST_0_SAMPLES(util_logbase2(layout->nr_samples)) |
      A6XX_TEX_CONST_0_SWAP(swap) |
      fdl6_texswiz(args, has_z24uint_s8uint) |
      A6XX_TEX_CONST_0_MIPLVLS(args->level_count - 1);
   view->descriptor[1] = A6XX_TEX_CONST_1_WIDTH(width) | A6XX_TEX_CONST_1_HEIGHT(height);
   view->descriptor[2] =
      A6XX_TEX_CONST_2_PITCHALIGN(layout->pitchalign - 6) |
      A6XX_TEX_CONST_2_PITCH(pitch) |
      A6XX_TEX_CONST_2_TYPE(fdl6_tex_type(args->type, false));
   view->descriptor[3] = A6XX_TEX_CONST_3_ARRAY_PITCH(layer_size);
   view->descriptor[4] = base_addr;
   view->descriptor[5] = (base_addr >> 32) | A6XX_TEX_CONST_5_DEPTH(depth);

   if (layout->tile_all)
      view->descriptor[3] |= A6XX_TEX_CONST_3_TILE_ALL;

   if (args->format == PIPE_FORMAT_R8_G8B8_420_UNORM ||
       args->format == PIPE_FORMAT_G8_B8R8_420_UNORM ||
       args->format == PIPE_FORMAT_G8_B8_R8_420_UNORM) {
      /* chroma offset re-uses MIPLVLS bits */
      assert(args->level_count == 1);
      if (args->chroma_offsets[0] == FDL_CHROMA_LOCATION_MIDPOINT)
         view->descriptor[0] |= A6XX_TEX_CONST_0_CHROMA_MIDPOINT_X;
      if (args->chroma_offsets[1] == FDL_CHROMA_LOCATION_MIDPOINT)
         view->descriptor[0] |= A6XX_TEX_CONST_0_CHROMA_MIDPOINT_Y;

      uint64_t base_addr[3];

      if (ubwc_enabled) {
         view->descriptor[3] |= A6XX_TEX_CONST_3_FLAG;
         /* no separate ubwc base, image must have the expected layout */
         for (uint32_t i = 0; i < 3; i++) {
            base_addr[i] = args->iova +
               fdl_ubwc_offset(layouts[i], args->base_miplevel, args->base_array_layer);
         }
      } else {
         for (uint32_t i = 0; i < 3; i++) {
            base_addr[i] = args->iova +
               fdl_surface_offset(layouts[i], args->base_miplevel, args->base_array_layer);
         }
      }

      view->descriptor[4] = base_addr[0];
      view->descriptor[5] |= base_addr[0] >> 32;
      view->descriptor[6] =
         A6XX_TEX_CONST_6_PLANE_PITCH(fdl_pitch(layouts[1], args->base_miplevel));
      view->descriptor[7] = base_addr[1];
      view->descriptor[8] = base_addr[1] >> 32;
      view->descriptor[9] = base_addr[2];
      view->descriptor[10] = base_addr[2] >> 32;

      assert(args->type != FDL_VIEW_TYPE_3D);
      return;
   }

   if (ubwc_enabled) {
      uint32_t block_width, block_height;
      fdl6_get_ubwc_blockwidth(layout, &block_width, &block_height);

      view->descriptor[3] |= A6XX_TEX_CONST_3_FLAG;
      view->descriptor[7] = ubwc_addr;
      view->descriptor[8] = ubwc_addr >> 32;
      view->descriptor[9] |= A6XX_TEX_CONST_9_FLAG_BUFFER_ARRAY_PITCH(layout->ubwc_layer_size >> 2);
      view->descriptor[10] |=
         A6XX_TEX_CONST_10_FLAG_BUFFER_PITCH(ubwc_pitch) |
         A6XX_TEX_CONST_10_FLAG_BUFFER_LOGW(util_logbase2_ceil(DIV_ROUND_UP(width, block_width))) |
         A6XX_TEX_CONST_10_FLAG_BUFFER_LOGH(util_logbase2_ceil(DIV_ROUND_UP(height, block_height)));
   }

   if (args->type == FDL_VIEW_TYPE_3D) {
      view->descriptor[3] |=
         A6XX_TEX_CONST_3_MIN_LAYERSZ(layout->slices[layout->mip_levels - 1].size0);
   }

   bool samples_average =
      layout->nr_samples > 1 &&
      !util_format_is_pure_integer(args->format) &&
      !util_format_is_depth_or_stencil(args->format);

   view->SP_PS_2D_SRC_INFO =
      A6XX_SP_PS_2D_SRC_INFO_COLOR_FORMAT(storage_format) |
      A6XX_SP_PS_2D_SRC_INFO_TILE_MODE(tile_mode) |
      A6XX_SP_PS_2D_SRC_INFO_COLOR_SWAP(swap) |
      COND(ubwc_enabled, A6XX_SP_PS_2D_SRC_INFO_FLAGS) |
      COND(util_format_is_srgb(args->format), A6XX_SP_PS_2D_SRC_INFO_SRGB) |
      A6XX_SP_PS_2D_SRC_INFO_SAMPLES(util_logbase2(layout->nr_samples)) |
      COND(samples_average, A6XX_SP_PS_2D_SRC_INFO_SAMPLES_AVERAGE) |
      A6XX_SP_PS_2D_SRC_INFO_UNK20 |
      A6XX_SP_PS_2D_SRC_INFO_UNK22;

   view->SP_PS_2D_SRC_SIZE =
      A6XX_SP_PS_2D_SRC_SIZE_WIDTH(width) |
      A6XX_SP_PS_2D_SRC_SIZE_HEIGHT(height);

   /* note: these have same encoding for MRT and 2D (except 2D PITCH src) */
   view->PITCH = A6XX_RB_DEPTH_BUFFER_PITCH(pitch);
   view->FLAG_BUFFER_PITCH =
      A6XX_RB_DEPTH_FLAG_BUFFER_PITCH_PITCH(ubwc_pitch) |
      A6XX_RB_DEPTH_FLAG_BUFFER_PITCH_ARRAY_PITCH(layout->ubwc_layer_size >> 2);

   view->base_addr = base_addr;
   view->ubwc_addr = ubwc_addr;
   view->layer_size = layer_size;
   view->ubwc_layer_size = layout->ubwc_layer_size;

   enum a6xx_format color_format =
      fd6_color_format(args->format, layout->tile_mode);

   /* Don't set fields that are only used for attachments/blit dest if COLOR
    * is unsupported.
    */
   if (color_format == FMT6_NONE)
      return;

   enum a3xx_color_swap color_swap =
      fd6_color_swap(args->format, layout->tile_mode);

   if (is_d24s8)
      color_format = FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8;

   if (color_format == FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8 && !ubwc_enabled)
      color_format = FMT6_8_8_8_8_UNORM;

   memset(view->storage_descriptor, 0, sizeof(view->storage_descriptor));

   view->storage_descriptor[0] =
      A6XX_IBO_0_FMT(storage_format) |
      A6XX_IBO_0_TILE_MODE(tile_mode);
   view->storage_descriptor[1] =
      A6XX_IBO_1_WIDTH(width) |
      A6XX_IBO_1_HEIGHT(height);
   view->storage_descriptor[2] =
      A6XX_IBO_2_PITCH(pitch) |
      A6XX_IBO_2_TYPE(fdl6_tex_type(args->type, true));
   view->storage_descriptor[3] = A6XX_IBO_3_ARRAY_PITCH(layer_size);

   view->storage_descriptor[4] = base_addr;
   view->storage_descriptor[5] = (base_addr >> 32) | A6XX_IBO_5_DEPTH(storage_depth);

   if (ubwc_enabled) {
      view->storage_descriptor[3] |= A6XX_IBO_3_FLAG | A6XX_IBO_3_UNK27;
      view->storage_descriptor[7] |= ubwc_addr;
      view->storage_descriptor[8] |= ubwc_addr >> 32;
      view->storage_descriptor[9] = A6XX_IBO_9_FLAG_BUFFER_ARRAY_PITCH(layout->ubwc_layer_size >> 2);
      view->storage_descriptor[10] =
         A6XX_IBO_10_FLAG_BUFFER_PITCH(ubwc_pitch);
   }

   view->width = width;
   view->height = height;
   view->need_y2_align =
      tile_mode == TILE6_LINEAR && args->base_miplevel != layout->mip_levels - 1;

   view->ubwc_enabled = ubwc_enabled;

   view->RB_MRT_BUF_INFO =
      A6XX_RB_MRT_BUF_INFO_COLOR_TILE_MODE(tile_mode) |
      A6XX_RB_MRT_BUF_INFO_COLOR_FORMAT(color_format) |
      A6XX_RB_MRT_BUF_INFO_COLOR_SWAP(color_swap);

   view->SP_FS_MRT_REG =
      A6XX_SP_FS_MRT_REG_COLOR_FORMAT(color_format) |
      COND(util_format_is_pure_sint(args->format), A6XX_SP_FS_MRT_REG_COLOR_SINT) |
      COND(util_format_is_pure_uint(args->format), A6XX_SP_FS_MRT_REG_COLOR_UINT);

   view->RB_2D_DST_INFO =
      A6XX_RB_2D_DST_INFO_COLOR_FORMAT(color_format) |
      A6XX_RB_2D_DST_INFO_TILE_MODE(tile_mode) |
      A6XX_RB_2D_DST_INFO_COLOR_SWAP(color_swap) |
      COND(ubwc_enabled, A6XX_RB_2D_DST_INFO_FLAGS) |
      COND(util_format_is_srgb(args->format), A6XX_RB_2D_DST_INFO_SRGB);

   view->RB_BLIT_DST_INFO =
      A6XX_RB_BLIT_DST_INFO_TILE_MODE(tile_mode) |
      A6XX_RB_BLIT_DST_INFO_SAMPLES(util_logbase2(layout->nr_samples)) |
      A6XX_RB_BLIT_DST_INFO_COLOR_FORMAT(color_format) |
      A6XX_RB_BLIT_DST_INFO_COLOR_SWAP(color_swap) |
      COND(ubwc_enabled, A6XX_RB_BLIT_DST_INFO_FLAGS);
}

void
fdl6_buffer_view_init(uint32_t *descriptor, enum pipe_format format,
                      const uint8_t *swiz, uint64_t iova, uint32_t size)
{
   unsigned elements = size / util_format_get_blocksize(format);

   struct fdl_view_args args = {
      .format = format,
      .swiz = {swiz[0], swiz[1], swiz[2], swiz[3]},
   };

   memset(descriptor, 0, 4 * FDL6_TEX_CONST_DWORDS);

   descriptor[0] =
      A6XX_TEX_CONST_0_TILE_MODE(TILE6_LINEAR) |
      A6XX_TEX_CONST_0_SWAP(fd6_texture_swap(format, TILE6_LINEAR)) |
      A6XX_TEX_CONST_0_FMT(fd6_texture_format(format, TILE6_LINEAR)) |
      A6XX_TEX_CONST_0_MIPLVLS(0) | fdl6_texswiz(&args, false) |
      COND(util_format_is_srgb(format), A6XX_TEX_CONST_0_SRGB);
   descriptor[1] = A6XX_TEX_CONST_1_WIDTH(elements & ((1 << 15) - 1)) |
                   A6XX_TEX_CONST_1_HEIGHT(elements >> 15);
   descriptor[2] = A6XX_TEX_CONST_2_BUFFER |
                   A6XX_TEX_CONST_2_TYPE(A6XX_TEX_BUFFER);
   descriptor[4] = iova;
   descriptor[5] = iova >> 32;
}
