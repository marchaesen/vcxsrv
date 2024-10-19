/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 * Copyright 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_descriptors.h"
#include "ac_gpu_info.h"
#include "ac_formats.h"
#include "ac_surface.h"

#include "gfx10_format_table.h"
#include "sid.h"

#include "util/u_math.h"
#include "util/format/u_format.h"

unsigned
ac_map_swizzle(unsigned swizzle)
{
   switch (swizzle) {
   case PIPE_SWIZZLE_Y:
      return V_008F0C_SQ_SEL_Y;
   case PIPE_SWIZZLE_Z:
      return V_008F0C_SQ_SEL_Z;
   case PIPE_SWIZZLE_W:
      return V_008F0C_SQ_SEL_W;
   case PIPE_SWIZZLE_0:
      return V_008F0C_SQ_SEL_0;
   case PIPE_SWIZZLE_1:
      return V_008F0C_SQ_SEL_1;
   default: /* PIPE_SWIZZLE_X */
      return V_008F0C_SQ_SEL_X;
   }
}

void
ac_build_sampler_descriptor(const enum amd_gfx_level gfx_level, const struct ac_sampler_state *state, uint32_t desc[4])
{
   const unsigned perf_mip = state->max_aniso_ratio ? state->max_aniso_ratio + 6 : 0;
   const bool compat_mode = gfx_level == GFX8 || gfx_level == GFX9;

   desc[0] = S_008F30_CLAMP_X(state->address_mode_u) |
             S_008F30_CLAMP_Y(state->address_mode_v) |
             S_008F30_CLAMP_Z(state->address_mode_w) |
             S_008F30_MAX_ANISO_RATIO(state->max_aniso_ratio) |
             S_008F30_DEPTH_COMPARE_FUNC(state->depth_compare_func) |
             S_008F30_FORCE_UNNORMALIZED(state->unnormalized_coords) |
             S_008F30_ANISO_THRESHOLD(state->max_aniso_ratio >> 1) |
             S_008F30_ANISO_BIAS(state->max_aniso_ratio) |
             S_008F30_DISABLE_CUBE_WRAP(!state->cube_wrap) |
             S_008F30_COMPAT_MODE(compat_mode) |
             S_008F30_TRUNC_COORD(state->trunc_coord) |
             S_008F30_FILTER_MODE(state->filter_mode);
   desc[1] = 0;
   desc[2] = S_008F38_XY_MAG_FILTER(state->mag_filter) |
             S_008F38_XY_MIN_FILTER(state->min_filter) |
             S_008F38_MIP_FILTER(state->mip_filter);
   desc[3] = S_008F3C_BORDER_COLOR_TYPE(state->border_color_type);

   if (gfx_level >= GFX12) {
      desc[1] |= S_008F34_MIN_LOD_GFX12(util_unsigned_fixed(CLAMP(state->min_lod, 0, 17), 8)) |
                 S_008F34_MAX_LOD_GFX12(util_unsigned_fixed(CLAMP(state->max_lod, 0, 17), 8));
      desc[2] |= S_008F38_PERF_MIP_LO(perf_mip);
      desc[3] |= S_008F3C_PERF_MIP_HI(perf_mip >> 2);
   } else {
      desc[1] |= S_008F34_MIN_LOD_GFX6(util_unsigned_fixed(CLAMP(state->min_lod, 0, 15), 8)) |
                 S_008F34_MAX_LOD_GFX6(util_unsigned_fixed(CLAMP(state->max_lod, 0, 15), 8)) |
                 S_008F34_PERF_MIP(perf_mip);
   }

   if (gfx_level >= GFX10) {
      desc[2] |= S_008F38_LOD_BIAS(util_signed_fixed(CLAMP(state->lod_bias, -32, 31), 8)) |
                 S_008F38_ANISO_OVERRIDE_GFX10(!state->aniso_single_level);
   } else {
      desc[2] |= S_008F38_LOD_BIAS(util_signed_fixed(CLAMP(state->lod_bias, -16, 16), 8)) |
                 S_008F38_DISABLE_LSB_CEIL(gfx_level <= GFX8) |
                 S_008F38_FILTER_PREC_FIX(1) |
                 S_008F38_ANISO_OVERRIDE_GFX8(gfx_level >= GFX8 && !state->aniso_single_level);
   }

   if (gfx_level >= GFX11) {
      desc[3] |= S_008F3C_BORDER_COLOR_PTR_GFX11(state->border_color_ptr);
   } else {
      desc[3] |= S_008F3C_BORDER_COLOR_PTR_GFX6(state->border_color_ptr);
   }
}

static void
ac_build_gfx6_fmask_descriptor(const enum amd_gfx_level gfx_level, const struct ac_fmask_state *state, uint32_t desc[8])
{
   const struct radeon_surf *surf = state->surf;
   const uint64_t va = state->va + surf->fmask_offset;
   uint32_t data_format, num_format;

#define FMASK(s, f) (((unsigned)(MAX2(1, s)) * 16) + (MAX2(1, f)))
   if (gfx_level == GFX9) {
      data_format = V_008F14_IMG_DATA_FORMAT_FMASK;
      switch (FMASK(state->num_samples, state->num_storage_samples)) {
      case FMASK(2, 1):
         num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_2_1;
         break;
      case FMASK(2, 2):
         num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_2_2;
         break;
      case FMASK(4, 1):
         num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_4_1;
         break;
      case FMASK(4, 2):
         num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_4_2;
         break;
      case FMASK(4, 4):
         num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_4_4;
         break;
      case FMASK(8, 1):
         num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_8_1;
         break;
      case FMASK(8, 2):
         num_format = V_008F14_IMG_NUM_FORMAT_FMASK_16_8_2;
         break;
      case FMASK(8, 4):
         num_format = V_008F14_IMG_NUM_FORMAT_FMASK_32_8_4;
         break;
      case FMASK(8, 8):
         num_format = V_008F14_IMG_NUM_FORMAT_FMASK_32_8_8;
         break;
      case FMASK(16, 1):
         num_format = V_008F14_IMG_NUM_FORMAT_FMASK_16_16_1;
         break;
      case FMASK(16, 2):
         num_format = V_008F14_IMG_NUM_FORMAT_FMASK_32_16_2;
         break;
      case FMASK(16, 4):
         num_format = V_008F14_IMG_NUM_FORMAT_FMASK_64_16_4;
         break;
      case FMASK(16, 8):
         num_format = V_008F14_IMG_NUM_FORMAT_FMASK_64_16_8;
         break;
      default:
         unreachable("invalid nr_samples");
      }
   } else {
      switch (FMASK(state->num_samples, state->num_storage_samples)) {
      case FMASK(2, 1):
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S2_F1;
         break;
      case FMASK(2, 2):
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S2_F2;
         break;
      case FMASK(4, 1):
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S4_F1;
         break;
      case FMASK(4, 2):
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S4_F2;
         break;
      case FMASK(4, 4):
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S4_F4;
         break;
      case FMASK(8, 1):
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S8_F1;
         break;
      case FMASK(8, 2):
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK16_S8_F2;
         break;
      case FMASK(8, 4):
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK32_S8_F4;
         break;
      case FMASK(8, 8):
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK32_S8_F8;
         break;
      case FMASK(16, 1):
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK16_S16_F1;
         break;
      case FMASK(16, 2):
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK32_S16_F2;
         break;
      case FMASK(16, 4):
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK64_S16_F4;
         break;
      case FMASK(16, 8):
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK64_S16_F8;
         break;
      default:
         unreachable("invalid nr_samples");
      }
      num_format = V_008F14_IMG_NUM_FORMAT_UINT;
   }
#undef FMASK

   desc[0] = (va >> 8) | surf->fmask_tile_swizzle;
   desc[1] = S_008F14_BASE_ADDRESS_HI(va >> 40) |
             S_008F14_DATA_FORMAT(data_format) |
             S_008F14_NUM_FORMAT(num_format);
   desc[2] = S_008F18_WIDTH(state->width - 1) |
             S_008F18_HEIGHT(state->height - 1);
   desc[3] = S_008F1C_DST_SEL_X(V_008F1C_SQ_SEL_X) |
             S_008F1C_DST_SEL_Y(V_008F1C_SQ_SEL_X) |
             S_008F1C_DST_SEL_Z(V_008F1C_SQ_SEL_X) |
             S_008F1C_DST_SEL_W(V_008F1C_SQ_SEL_X) |
             S_008F1C_TYPE(state->type);
   desc[4] = 0;
   desc[5] = S_008F24_BASE_ARRAY(state->first_layer);
   desc[6] = 0;
   desc[7] = 0;

   if (gfx_level == GFX9) {
      desc[3] |= S_008F1C_SW_MODE(surf->u.gfx9.color.fmask_swizzle_mode);
      desc[4] |= S_008F20_DEPTH(state->last_layer) |
                 S_008F20_PITCH(surf->u.gfx9.color.fmask_epitch);
      desc[5] |= S_008F24_META_PIPE_ALIGNED(1) |
                 S_008F24_META_RB_ALIGNED(1);

      if (state->tc_compat_cmask) {
         const uint64_t cmask_va = state->va + surf->cmask_offset;

         desc[5] |= S_008F24_META_DATA_ADDRESS(cmask_va >> 40);
         desc[6] |= S_008F28_COMPRESSION_EN(1);
         desc[7] |= cmask_va >> 8;
      }
   } else {
      desc[3] |= S_008F1C_TILING_INDEX(surf->u.legacy.color.fmask.tiling_index);
      desc[4] |= S_008F20_DEPTH(state->depth - 1) |
                 S_008F20_PITCH(surf->u.legacy.color.fmask.pitch_in_pixels - 1);
      desc[5] |= S_008F24_LAST_ARRAY(state->last_layer);

      if (state->tc_compat_cmask) {
         const uint64_t cmask_va = state->va + surf->cmask_offset;

         desc[6] |= S_008F28_COMPRESSION_EN(1);
         desc[7] |= cmask_va >> 8;
      }
   }
}

static void
ac_build_gfx10_fmask_descriptor(const enum amd_gfx_level gfx_level, const struct ac_fmask_state *state, uint32_t desc[8])
{
   const struct radeon_surf *surf = state->surf;
   const uint64_t va = state->va + surf->fmask_offset;
   uint32_t format;

#define FMASK(s, f) (((unsigned)(MAX2(1, s)) * 16) + (MAX2(1, f)))
   switch (FMASK(state->num_samples, state->num_storage_samples)) {
   case FMASK(2, 1):
      format = V_008F0C_GFX10_FORMAT_FMASK8_S2_F1;
      break;
   case FMASK(2, 2):
      format = V_008F0C_GFX10_FORMAT_FMASK8_S2_F2;
      break;
   case FMASK(4, 1):
      format = V_008F0C_GFX10_FORMAT_FMASK8_S4_F1;
      break;
   case FMASK(4, 2):
      format = V_008F0C_GFX10_FORMAT_FMASK8_S4_F2;
      break;
   case FMASK(4, 4):
      format = V_008F0C_GFX10_FORMAT_FMASK8_S4_F4;
      break;
   case FMASK(8, 1):
      format = V_008F0C_GFX10_FORMAT_FMASK8_S8_F1;
      break;
   case FMASK(8, 2):
      format = V_008F0C_GFX10_FORMAT_FMASK16_S8_F2;
      break;
   case FMASK(8, 4):
      format = V_008F0C_GFX10_FORMAT_FMASK32_S8_F4;
      break;
   case FMASK(8, 8):
      format = V_008F0C_GFX10_FORMAT_FMASK32_S8_F8;
      break;
   case FMASK(16, 1):
      format = V_008F0C_GFX10_FORMAT_FMASK16_S16_F1;
      break;
   case FMASK(16, 2):
      format = V_008F0C_GFX10_FORMAT_FMASK32_S16_F2;
      break;
   case FMASK(16, 4):
      format = V_008F0C_GFX10_FORMAT_FMASK64_S16_F4;
      break;
   case FMASK(16, 8):
      format = V_008F0C_GFX10_FORMAT_FMASK64_S16_F8;
      break;
   default:
      unreachable("invalid nr_samples");
   }
#undef FMASK

   desc[0] = (va >> 8) | surf->fmask_tile_swizzle;
   desc[1] = S_00A004_BASE_ADDRESS_HI(va >> 40) |
             S_00A004_FORMAT_GFX10(format) |
             S_00A004_WIDTH_LO(state->width - 1);
   desc[2] = S_00A008_WIDTH_HI((state->width - 1) >> 2) |
             S_00A008_HEIGHT(state->height - 1) |
             S_00A008_RESOURCE_LEVEL(1);
   desc[3] = S_00A00C_DST_SEL_X(V_008F1C_SQ_SEL_X) |
             S_00A00C_DST_SEL_Y(V_008F1C_SQ_SEL_X) |
             S_00A00C_DST_SEL_Z(V_008F1C_SQ_SEL_X) |
             S_00A00C_DST_SEL_W(V_008F1C_SQ_SEL_X) |
             S_00A00C_SW_MODE(surf->u.gfx9.color.fmask_swizzle_mode) |
             S_00A00C_TYPE(state->type);
   desc[4] = S_00A010_DEPTH_GFX10(state->last_layer) | S_00A010_BASE_ARRAY(state->first_layer);
   desc[5] = 0;
   desc[6] = S_00A018_META_PIPE_ALIGNED(1);
   desc[7] = 0;

   if (state->tc_compat_cmask) {
      uint64_t cmask_va = state->va + surf->cmask_offset;

      desc[6] |= S_00A018_COMPRESSION_EN(1);
      desc[6] |= S_00A018_META_DATA_ADDRESS_LO(cmask_va >> 8);
      desc[7] |= cmask_va >> 16;
   }
}

void
ac_build_fmask_descriptor(const enum amd_gfx_level gfx_level, const struct ac_fmask_state *state, uint32_t desc[8])
{
   assert(gfx_level < GFX11);

   if (gfx_level >= GFX10) {
      ac_build_gfx10_fmask_descriptor(gfx_level, state, desc);
   } else {
      ac_build_gfx6_fmask_descriptor(gfx_level, state, desc);
   }
}

static void
ac_build_gfx6_texture_descriptor(const struct radeon_info *info, const struct ac_texture_state *state, uint32_t desc[8])
{
   const struct util_format_description *fmt_desc = util_format_description(state->format);
   uint32_t num_format, data_format, num_samples;
   int first_non_void;

   num_samples = fmt_desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS ? MAX2(1, state->num_samples)
                                                                   : MAX2(1, state->num_storage_samples);

   first_non_void = util_format_get_first_non_void_channel(state->format);

   num_format = ac_translate_tex_numformat(fmt_desc, first_non_void);

   data_format = ac_translate_tex_dataformat(info, fmt_desc, first_non_void);
   if (data_format == ~0) {
      data_format = 0;
   }

   /* S8 with either Z16 or Z32 HTILE need a special format. */
   if (info->gfx_level == GFX9 && state->format == PIPE_FORMAT_S8_UINT && state->tc_compat_htile_enabled) {
      if (state->img_format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ||
          state->img_format == PIPE_FORMAT_Z24_UNORM_S8_UINT ||
          state->img_format == PIPE_FORMAT_S8_UINT_Z24_UNORM) {
         data_format = V_008F14_IMG_DATA_FORMAT_S8_32;
      } else if (state->img_format == PIPE_FORMAT_Z16_UNORM_S8_UINT) {
         data_format = V_008F14_IMG_DATA_FORMAT_S8_16;
      }
   }

   desc[0] = 0;
   desc[1] = S_008F14_MIN_LOD(util_unsigned_fixed(CLAMP(state->min_lod, 0, 15), 8)) |
             S_008F14_DATA_FORMAT(data_format) |
             S_008F14_NUM_FORMAT(num_format);
   desc[2] = S_008F18_WIDTH(state->width - 1) |
             S_008F18_HEIGHT(state->height - 1) |
             S_008F18_PERF_MOD(4);
   desc[3] = S_008F1C_DST_SEL_X(ac_map_swizzle(state->swizzle[0])) |
             S_008F1C_DST_SEL_Y(ac_map_swizzle(state->swizzle[1])) |
             S_008F1C_DST_SEL_Z(ac_map_swizzle(state->swizzle[2])) |
             S_008F1C_DST_SEL_W(ac_map_swizzle(state->swizzle[3])) |
             S_008F1C_BASE_LEVEL(num_samples > 1 ? 0 : state->first_level) |
             S_008F1C_LAST_LEVEL(num_samples > 1 ? util_logbase2(num_samples) : state->last_level) |
             S_008F1C_TYPE(state->type);
   desc[4] = 0;
   desc[5] = S_008F24_BASE_ARRAY(state->first_layer);
   desc[6] = 0;
   desc[7] = 0;

   if (info->gfx_level == GFX9) {
      const uint32_t bc_swizzle = ac_border_color_swizzle(fmt_desc);

      /* Depth is the last accessible layer on Gfx9.
       * The hw doesn't need to know the total number of layers.
       */
      if (state->type == V_008F1C_SQ_RSRC_IMG_3D)
         desc[4] |= S_008F20_DEPTH(state->depth - 1);
      else
         desc[4] |= S_008F20_DEPTH(state->last_layer);

      desc[4] |= S_008F20_BC_SWIZZLE(bc_swizzle);
      desc[5] |= S_008F24_MAX_MIP(num_samples > 1 ? util_logbase2(num_samples) : state->num_levels - 1);
   } else {
      desc[3] |= S_008F1C_POW2_PAD(state->num_levels > 1);
      desc[4] |= S_008F20_DEPTH(state->depth - 1);
      desc[5] |= S_008F24_LAST_ARRAY(state->last_layer);
   }

   if (state->dcc_enabled) {
      desc[6] = S_008F28_ALPHA_IS_ON_MSB(ac_alpha_is_on_msb(info, state->format));
   } else {
      if (!state->aniso_single_level) {
         /* The last dword is unused by hw. The shader uses it to clear
          * bits in the first dword of sampler state.
          */
         if (info->gfx_level <= GFX7 && state->num_samples <= 1) {
            if (state->first_level == state->last_level)
               desc[7] = C_008F30_MAX_ANISO_RATIO;
            else
               desc[7] = 0xffffffff;
         }
      }
   }
}

static uint32_t
ac_get_gfx10_img_format(const enum amd_gfx_level gfx_level, const struct ac_texture_state *state)
{
   const struct gfx10_format *fmt = &ac_get_gfx10_format_table(gfx_level)[state->format];
   const struct util_format_description *desc = util_format_description(state->format);
   uint32_t img_format = fmt->img_format;

   if (desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS &&
       state->gfx10.upgraded_depth && !util_format_has_stencil(desc)) {
      if (gfx_level >= GFX11) {
         assert(img_format == V_008F0C_GFX11_FORMAT_32_FLOAT);
         img_format = V_008F0C_GFX11_FORMAT_32_FLOAT_CLAMP;
      } else {
         assert(img_format == V_008F0C_GFX10_FORMAT_32_FLOAT);
         img_format = V_008F0C_GFX10_FORMAT_32_FLOAT_CLAMP;
      }
   }

   return img_format;
}

static void
ac_build_gfx10_texture_descriptor(const struct radeon_info *info, const struct ac_texture_state *state, uint32_t desc[8])
{
   const struct radeon_surf *surf = state->surf;
   const struct util_format_description *fmt_desc = util_format_description(state->format);
   const uint32_t img_format = ac_get_gfx10_img_format(info->gfx_level, state);
   const struct ac_surf_nbc_view *nbc_view = state->gfx9.nbc_view;
   const uint32_t field_last_level = state->num_samples > 1 ? util_logbase2(state->num_samples) : state->last_level;

   desc[0] = 0;
   desc[1] = S_00A004_FORMAT_GFX10(img_format) |
             S_00A004_WIDTH_LO(state->width - 1);
   desc[2] = S_00A008_WIDTH_HI((state->width - 1) >> 2) |
             S_00A008_HEIGHT(state->height - 1) |
             S_00A008_RESOURCE_LEVEL(info->gfx_level < GFX11);
   desc[3] = S_00A00C_DST_SEL_X(ac_map_swizzle(state->swizzle[0])) |
             S_00A00C_DST_SEL_Y(ac_map_swizzle(state->swizzle[1])) |
             S_00A00C_DST_SEL_Z(ac_map_swizzle(state->swizzle[2])) |
             S_00A00C_DST_SEL_W(ac_map_swizzle(state->swizzle[3])) |
             S_00A00C_BASE_LEVEL(state->num_samples > 1 ? 0 : state->first_level) |
             S_00A00C_LAST_LEVEL_GFX10(field_last_level) |
             S_00A00C_BC_SWIZZLE(ac_border_color_swizzle(fmt_desc)) |
             S_00A00C_TYPE(state->type);

   /* Depth is the the last accessible layer on gfx9+. The hw doesn't need
    * to know the total number of layers.
    */
   desc[4] = S_00A010_DEPTH_GFX10(state->depth) |
             S_00A010_BASE_ARRAY(state->first_layer);

   /* ARRAY_PITCH is only meaningful for 3D images, 0 means SRV, 1 means UAV.
    * In SRV mode, BASE_ARRAY is ignored and DEPTH is the last slice of mipmap level 0.
    * In UAV mode, BASE_ARRAY is the first slice and DEPTH is the last slice of the bound level.
    */
   desc[5] = S_00A014_ARRAY_PITCH(state->gfx10.uav3d) | S_00A014_PERF_MOD(4);
   desc[6] = 0;
   desc[7] = 0;

   uint32_t max_mip = state->num_samples > 1 ? util_logbase2(state->num_samples) : state->num_levels - 1;
   if (nbc_view && nbc_view->valid)
      max_mip = nbc_view->num_levels - 1;

   const uint32_t min_lod_clamped = util_unsigned_fixed(CLAMP(state->min_lod, 0, 15), 8);
   if (info->gfx_level >= GFX11) {
      desc[1] |= S_00A004_MAX_MIP_GFX11(max_mip);
      desc[5] |= S_00A014_MIN_LOD_LO_GFX11(min_lod_clamped);
      desc[6] |= S_00A018_MIN_LOD_HI(min_lod_clamped >> 5);
   } else {
      desc[1] |= S_00A004_MIN_LOD(min_lod_clamped);
      desc[5] |= S_00A014_MAX_MIP(max_mip);
   }

   if (state->dcc_enabled) {
      desc[6] |= S_00A018_MAX_UNCOMPRESSED_BLOCK_SIZE(V_028C78_MAX_BLOCK_SIZE_256B) |
                 S_00A018_MAX_COMPRESSED_BLOCK_SIZE(surf->u.gfx9.color.dcc.max_compressed_block_size) |
                 S_00A018_ALPHA_IS_ON_MSB(ac_alpha_is_on_msb(info, state->format));
   }
}

static void
ac_build_gfx12_texture_descriptor(const struct radeon_info *info, const struct ac_texture_state *state, uint32_t desc[8])
{
   const struct radeon_surf *surf = state->surf;
   const struct util_format_description *fmt_desc = util_format_description(state->format);
   const uint32_t img_format = ac_get_gfx10_img_format(info->gfx_level, state);
   const uint32_t max_mip = state->num_samples > 1 ? util_logbase2(state->num_samples) : state->num_levels - 1;
   const uint32_t field_last_level = state->num_samples > 1 ? util_logbase2(state->num_samples) : state->last_level;
   const bool no_edge_clamp = state->num_levels > 1 && util_format_is_compressed(state->img_format) &&
                              !util_format_is_compressed(state->format);
   const uint32_t min_lod_clamped = util_unsigned_fixed(CLAMP(state->min_lod, 0, 15), 8);

   desc[0] = 0;
   desc[1] = S_00A004_MAX_MIP_GFX12(max_mip) |
             S_00A004_FORMAT_GFX12(img_format) |
             S_00A004_BASE_LEVEL(state->num_samples > 1 ? 0 : state->first_level) |
             S_00A004_WIDTH_LO(state->width - 1);
   desc[2] = S_00A008_WIDTH_HI((state->width - 1) >> 2) |
             S_00A008_HEIGHT(state->height - 1);
   desc[3] = S_00A00C_DST_SEL_X(ac_map_swizzle(state->swizzle[0])) |
             S_00A00C_DST_SEL_Y(ac_map_swizzle(state->swizzle[1])) |
             S_00A00C_DST_SEL_Z(ac_map_swizzle(state->swizzle[2])) |
             S_00A00C_DST_SEL_W(ac_map_swizzle(state->swizzle[3])) |
             S_00A00C_NO_EDGE_CLAMP(no_edge_clamp) |
             S_00A00C_LAST_LEVEL_GFX12(field_last_level) |
             S_00A00C_BC_SWIZZLE(ac_border_color_swizzle(fmt_desc)) |
             S_00A00C_TYPE(state->type);

   /* Depth is the the last accessible layer on gfx9+. The hw doesn't need
    * to know the total number of layers.
    */
   desc[4] = S_00A010_DEPTH_GFX12(state->depth) |
             S_00A010_BASE_ARRAY(state->first_layer);
   desc[5] = S_00A014_UAV3D(state->gfx10.uav3d) |
             S_00A014_PERF_MOD(4) |
             S_00A014_MIN_LOD_LO_GFX12(min_lod_clamped);
   desc[6] = S_00A018_MAX_UNCOMPRESSED_BLOCK_SIZE(1 /*256B*/) |
             S_00A018_MAX_COMPRESSED_BLOCK_SIZE(surf->u.gfx9.color.dcc.max_compressed_block_size) |
             S_00A018_MIN_LOD_HI(min_lod_clamped >> 6);
   desc[7] = 0;
}

void
ac_build_texture_descriptor(const struct radeon_info *info, const struct ac_texture_state *state, uint32_t desc[8])
{
   if (info->gfx_level >= GFX12) {
      ac_build_gfx12_texture_descriptor(info, state, desc);
   } else if (info->gfx_level >= GFX10) {
      ac_build_gfx10_texture_descriptor(info, state, desc);
   } else {
      ac_build_gfx6_texture_descriptor(info, state, desc);
   }
}

uint32_t
ac_tile_mode_index(const struct radeon_surf *surf, unsigned level, bool stencil)
{
   if (stencil)
      return surf->u.legacy.zs.stencil_tiling_index[level];
   else
      return surf->u.legacy.tiling_index[level];
}

void
ac_set_mutable_tex_desc_fields(const struct radeon_info *info, const struct ac_mutable_tex_state *state, uint32_t desc[8])
{
   const struct radeon_surf *surf = state->surf;
   const struct legacy_surf_level *base_level_info = state->gfx6.base_level_info;
   const struct ac_surf_nbc_view *nbc_view = state->gfx9.nbc_view;
   uint8_t swizzle = surf->tile_swizzle;
   uint64_t va = state->va, meta_va = 0;

   if (info->gfx_level >= GFX9) {
      if (state->is_stencil) {
         va += surf->u.gfx9.zs.stencil_offset;
      } else {
         va += surf->u.gfx9.surf_offset;
      }

      if (nbc_view && nbc_view->valid) {
         va += nbc_view->base_address_offset;
         swizzle = nbc_view->tile_swizzle;
      }
   } else {
      va += (uint64_t)base_level_info->offset_256B * 256;
   }

   if (!info->has_image_opcodes) {
      /* Set it as a buffer descriptor. */
      desc[0] = va;
      desc[1] |= S_008F04_BASE_ADDRESS_HI(va >> 32);
      return;
   }

   desc[0] = va >> 8;
   desc[1] |= S_008F14_BASE_ADDRESS_HI(va >> 40);

   if (info->gfx_level >= GFX8 && info->gfx_level < GFX12) {
      if (state->dcc_enabled) {
         meta_va = state->va + surf->meta_offset;
         if (info->gfx_level == GFX8) {
            meta_va += surf->u.legacy.color.dcc_level[state->gfx6.base_level].dcc_offset;
            assert(base_level_info->mode == RADEON_SURF_MODE_2D);
         }

         unsigned dcc_tile_swizzle = swizzle << 8;
         dcc_tile_swizzle &= (1 << surf->meta_alignment_log2) - 1;
         meta_va |= dcc_tile_swizzle;
      } else if (state->tc_compat_htile_enabled) {
         meta_va = state->va + surf->meta_offset;
      }
   }

   if (info->gfx_level >= GFX10) {
      desc[0] |= swizzle;

      if (state->is_stencil) {
         desc[3] |= S_00A00C_SW_MODE(surf->u.gfx9.zs.stencil_swizzle_mode);
      } else {
         desc[3] |= S_00A00C_SW_MODE(surf->u.gfx9.swizzle_mode);
      }

      /* GFX10.3+ can set a custom pitch for 1D and 2D non-array, but it must be a multiple
       * of 256B.
       */
      if (info->gfx_level >= GFX10_3 && surf->u.gfx9.uses_custom_pitch) {
         ASSERTED unsigned min_alignment = info->gfx_level >= GFX12 ? 128 : 256;
         assert((surf->u.gfx9.surf_pitch * surf->bpe) % min_alignment == 0);
         assert(surf->is_linear);
         unsigned pitch = surf->u.gfx9.surf_pitch;

         /* Subsampled images have the pitch in the units of blocks. */
         if (surf->blk_w == 2)
            pitch *= 2;

         if (info->gfx_level >= GFX12) {
            desc[4] |= S_00A010_DEPTH_GFX12(pitch - 1) | /* DEPTH contains low bits of PITCH. */
                       S_00A010_PITCH_MSB_GFX12((pitch - 1) >> 14);
         } else {
            desc[4] |= S_00A010_DEPTH_GFX10(pitch - 1) | /* DEPTH contains low bits of PITCH. */
                       S_00A010_PITCH_MSB_GFX103((pitch - 1) >> 13);
         }
      }

      if (info->gfx_level >= GFX12) {
         /* Color and Z/S always support compressed image stores on Gfx12. Enablement is
          * mostly controlled by PTE.D (page table bit). The rule is:
          *
          * Shader Engines (shaders, CB, DB, SC):
          *    COMPRESSION_ENABLED = PTE.D && COMPRESSION_EN;
          *
          * Central Hub (CP, SDMA, indices, tess factor loads):
          *    PTE.D is ignored. Packets and states fully determine enablement.
          *
          * If !PTE.D, the states enabling compression in shaders, CB, DB, and SC have no effect.
          * PTE.D is set per buffer allocation in Linux, not per VM page, so that it's
          * automatically propagated between processes. We could optionally allow setting it
          * per VM page too.
          *
          * The DCC/HTILE buffer isn't allocated separately on Gfx12 anymore. The DCC/HTILE
          * metadata storage is mostly hidden from userspace, and any buffer can be compressed.
          */
         if (state->dcc_enabled) {
            desc[6] |= S_00A018_COMPRESSION_EN(1) |
                       S_00A018_WRITE_COMPRESS_ENABLE(state->gfx10.write_compress_enable);
         }
      } else if (meta_va) {
         /* Gfx10-11. */
         struct gfx9_surf_meta_flags meta = {
            .rb_aligned = 1,
            .pipe_aligned = 1,
         };

         if (!(surf->flags & RADEON_SURF_Z_OR_SBUFFER) && surf->meta_offset)
            meta = surf->u.gfx9.color.dcc;

         desc[6] |= S_00A018_COMPRESSION_EN(1) |
                    S_00A018_META_PIPE_ALIGNED(meta.pipe_aligned) |
                    S_00A018_META_DATA_ADDRESS_LO(meta_va >> 8) |
                    /* DCC image stores require the following settings:
                     * - INDEPENDENT_64B_BLOCKS = 0
                     * - INDEPENDENT_128B_BLOCKS = 1
                     * - MAX_COMPRESSED_BLOCK_SIZE = 128B
                     * - MAX_UNCOMPRESSED_BLOCK_SIZE = 256B (always used)
                     *
                     * The same limitations apply to SDMA compressed stores because
                     * SDMA uses the same DCC codec.
                     */
                    S_00A018_WRITE_COMPRESS_ENABLE(state->gfx10.write_compress_enable) |
                    /* TC-compatible MSAA HTILE requires ITERATE_256. */
                    S_00A018_ITERATE_256(state->gfx10.iterate_256);

         desc[7] = meta_va >> 16;
      }
   } else if (info->gfx_level == GFX9) {
      desc[0] |= surf->tile_swizzle;

      if (state->is_stencil) {
         desc[3] |= S_008F1C_SW_MODE(surf->u.gfx9.zs.stencil_swizzle_mode);
         desc[4] |= S_008F20_PITCH(surf->u.gfx9.zs.stencil_epitch);
      } else {
         desc[3] |= S_008F1C_SW_MODE(surf->u.gfx9.swizzle_mode);
         desc[4] |= S_008F20_PITCH(surf->u.gfx9.epitch);
      }

      if (meta_va) {
         struct gfx9_surf_meta_flags meta = {
            .rb_aligned = 1,
            .pipe_aligned = 1,
         };

         if (!(surf->flags & RADEON_SURF_Z_OR_SBUFFER) && surf->meta_offset)
            meta = surf->u.gfx9.color.dcc;

         desc[5] |= S_008F24_META_DATA_ADDRESS(meta_va >> 40) |
                    S_008F24_META_PIPE_ALIGNED(meta.pipe_aligned) |
                    S_008F24_META_RB_ALIGNED(meta.rb_aligned);
         desc[6] |= S_008F28_COMPRESSION_EN(1);
         desc[7] = meta_va >> 8;
      }
   } else {
      /* GFX6-GFX8 */
      unsigned pitch = base_level_info->nblk_x * state->gfx6.block_width;
      unsigned index = ac_tile_mode_index(surf, state->gfx6.base_level, state->is_stencil);

      /* Only macrotiled modes can set tile swizzle. */
      if (base_level_info->mode == RADEON_SURF_MODE_2D)
         desc[0] |= surf->tile_swizzle;

      desc[3] |= S_008F1C_TILING_INDEX(index);
      desc[4] |= S_008F20_PITCH(pitch - 1);

      if (info->gfx_level == GFX8 && meta_va) {
         desc[6] |= S_008F28_COMPRESSION_EN(1);
         desc[7] = meta_va >> 8;
      }
   }
}

void
ac_set_buf_desc_word3(const enum amd_gfx_level gfx_level, const struct ac_buffer_state *state, uint32_t *rsrc_word3)
{
   *rsrc_word3 = S_008F0C_DST_SEL_X(ac_map_swizzle(state->swizzle[0])) |
                 S_008F0C_DST_SEL_Y(ac_map_swizzle(state->swizzle[1])) |
                 S_008F0C_DST_SEL_Z(ac_map_swizzle(state->swizzle[2])) |
                 S_008F0C_DST_SEL_W(ac_map_swizzle(state->swizzle[3])) |
                 S_008F0C_INDEX_STRIDE(state->index_stride) |
                 S_008F0C_ADD_TID_ENABLE(state->add_tid);

   if (gfx_level >= GFX10) {
      const struct gfx10_format *fmt = &ac_get_gfx10_format_table(gfx_level)[state->format];

      /* OOB_SELECT chooses the out-of-bounds check.
       *
       * GFX10:
       *  - 0: (index >= NUM_RECORDS) || (offset >= STRIDE)
       *  - 1: index >= NUM_RECORDS
       *  - 2: NUM_RECORDS == 0
       *  - 3: if SWIZZLE_ENABLE:
       *          swizzle_address >= NUM_RECORDS
       *       else:
       *          offset >= NUM_RECORDS
       *
       * GFX11+:
       *  - 0: (index >= NUM_RECORDS) || (offset+payload > STRIDE)
       *  - 1: index >= NUM_RECORDS
       *  - 2: NUM_RECORDS == 0
       *  - 3: if SWIZZLE_ENABLE && STRIDE:
       *          (index >= NUM_RECORDS) || ( offset+payload > STRIDE)
       *       else:
       *          offset+payload > NUM_RECORDS
       */
      *rsrc_word3 |= (gfx_level >= GFX12 ? S_008F0C_FORMAT_GFX12(fmt->img_format) :
                                           S_008F0C_FORMAT_GFX10(fmt->img_format)) |
                     S_008F0C_OOB_SELECT(state->gfx10_oob_select) |
                     S_008F0C_RESOURCE_LEVEL(gfx_level < GFX11);
   } else {
      const struct util_format_description * desc =  util_format_description(state->format);
      const int first_non_void = util_format_get_first_non_void_channel(state->format);
      const uint32_t num_format = ac_translate_buffer_numformat(desc, first_non_void);

      /* DATA_FORMAT is STRIDE[14:17] for MUBUF with ADD_TID_ENABLE=1 */
      const uint32_t data_format =
         gfx_level >= GFX8 && state->add_tid ? 0 : ac_translate_buffer_dataformat(desc, first_non_void);

      *rsrc_word3 |= S_008F0C_NUM_FORMAT(num_format) |
                     S_008F0C_DATA_FORMAT(data_format) |
                     S_008F0C_ELEMENT_SIZE(state->element_size);
   }
}

void
ac_build_buffer_descriptor(const enum amd_gfx_level gfx_level, const struct ac_buffer_state *state, uint32_t desc[4])
{
   uint32_t rsrc_word1 = S_008F04_BASE_ADDRESS_HI(state->va >> 32) | S_008F04_STRIDE(state->stride);
   uint32_t rsrc_word3;

   if (gfx_level >= GFX11) {
      rsrc_word1 |= S_008F04_SWIZZLE_ENABLE_GFX11(state->swizzle_enable);
   } else {
      rsrc_word1 |= S_008F04_SWIZZLE_ENABLE_GFX6(state->swizzle_enable);
   }

   ac_set_buf_desc_word3(gfx_level, state, &rsrc_word3);

   desc[0] = state->va;
   desc[1] = rsrc_word1;
   desc[2] = state->size;
   desc[3] = rsrc_word3;
}

void
ac_build_raw_buffer_descriptor(const enum amd_gfx_level gfx_level, uint64_t va, uint32_t size, uint32_t desc[4])
{
   const struct ac_buffer_state ac_state = {
      .va = va,
      .size = size,
      .format = PIPE_FORMAT_R32_FLOAT,
      .swizzle = {
         PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W,
      },
      .gfx10_oob_select = V_008F0C_OOB_SELECT_RAW,
   };

   ac_build_buffer_descriptor(gfx_level, &ac_state, desc);
}

void
ac_build_attr_ring_descriptor(const enum amd_gfx_level gfx_level, uint64_t va, uint32_t size, uint32_t stride, uint32_t desc[4])
{
   assert(gfx_level >= GFX11);

   const struct ac_buffer_state ac_state = {
      .va = va,
      .size = size,
      .format = PIPE_FORMAT_R32G32B32A32_FLOAT,
      .swizzle = {
         PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W,
      },
      .stride = stride,
      .gfx10_oob_select = V_008F0C_OOB_SELECT_STRUCTURED_WITH_OFFSET,
      .swizzle_enable = 3, /* 16B */
      .index_stride = 2, /* 32 elements */
   };

   ac_build_buffer_descriptor(gfx_level, &ac_state, desc);
}

static void
ac_init_gfx6_ds_surface(const struct radeon_info *info, const struct ac_ds_state *state,
                        uint32_t db_format, uint32_t stencil_format, struct ac_ds_surface *ds)
{
   const struct radeon_surf *surf = state->surf;
   const struct legacy_surf_level *level_info = &surf->u.legacy.level[state->level];

   assert(level_info->nblk_x % 8 == 0 && level_info->nblk_y % 8 == 0);

   if (state->stencil_only)
      level_info = &surf->u.legacy.zs.stencil_level[state->level];

   ds->u.gfx6.db_htile_data_base = 0;
   ds->u.gfx6.db_htile_surface = 0;
   ds->db_depth_base = (state->va >> 8) + surf->u.legacy.level[state->level].offset_256B;
   ds->db_stencil_base = (state->va >> 8) + surf->u.legacy.zs.stencil_level[state->level].offset_256B;
   ds->db_depth_view = S_028008_SLICE_START(state->first_layer) |
                       S_028008_SLICE_MAX(state->last_layer) |
                       S_028008_Z_READ_ONLY(state->z_read_only) |
                       S_028008_STENCIL_READ_ONLY(state->stencil_read_only);
   ds->db_z_info = S_028040_FORMAT(db_format) |
                   S_028040_NUM_SAMPLES(util_logbase2(state->num_samples));
   ds->db_stencil_info = S_028044_FORMAT(stencil_format);

   if (info->gfx_level >= GFX7) {
      const uint32_t index = surf->u.legacy.tiling_index[state->level];
      const uint32_t stencil_index = surf->u.legacy.zs.stencil_tiling_index[state->level];
      const uint32_t macro_index = surf->u.legacy.macro_tile_index;
      const uint32_t stencil_tile_mode = info->si_tile_mode_array[stencil_index];
      const uint32_t macro_mode = info->cik_macrotile_mode_array[macro_index];
      uint32_t tile_mode = info->si_tile_mode_array[index];

      if (state->stencil_only)
         tile_mode = stencil_tile_mode;

      ds->u.gfx6.db_depth_info |= S_02803C_ARRAY_MODE(G_009910_ARRAY_MODE(tile_mode)) |
                                  S_02803C_PIPE_CONFIG(G_009910_PIPE_CONFIG(tile_mode)) |
                                  S_02803C_BANK_WIDTH(G_009990_BANK_WIDTH(macro_mode)) |
                                  S_02803C_BANK_HEIGHT(G_009990_BANK_HEIGHT(macro_mode)) |
                                  S_02803C_MACRO_TILE_ASPECT(G_009990_MACRO_TILE_ASPECT(macro_mode)) |
                                  S_02803C_NUM_BANKS(G_009990_NUM_BANKS(macro_mode));
      ds->db_z_info |= S_028040_TILE_SPLIT(G_009910_TILE_SPLIT(tile_mode));
      ds->db_stencil_info |= S_028044_TILE_SPLIT(G_009910_TILE_SPLIT(stencil_tile_mode));
   } else {
      uint32_t tile_mode_index = ac_tile_mode_index(surf, state->level, false);
      ds->db_z_info |= S_028040_TILE_MODE_INDEX(tile_mode_index);

      tile_mode_index = ac_tile_mode_index(surf, state->level, true);
      ds->db_stencil_info |= S_028044_TILE_MODE_INDEX(tile_mode_index);
      if (state->stencil_only)
         ds->db_z_info |= S_028040_TILE_MODE_INDEX(tile_mode_index);
   }

   ds->db_depth_size = S_028058_PITCH_TILE_MAX((level_info->nblk_x / 8) - 1) |
                       S_028058_HEIGHT_TILE_MAX((level_info->nblk_y / 8) - 1);
   ds->u.gfx6.db_depth_slice = S_02805C_SLICE_TILE_MAX((level_info->nblk_x * level_info->nblk_y) / 64 - 1);

   if (state->htile_enabled) {
      ds->db_z_info |= S_028040_TILE_SURFACE_ENABLE(1) |
                       S_028040_ALLOW_EXPCLEAR(state->allow_expclear);
      ds->db_stencil_info |= S_028044_TILE_STENCIL_DISABLE(state->htile_stencil_disabled);

      if (surf->has_stencil) {
         /* Workaround: For a not yet understood reason, the
          * combination of MSAA, fast stencil clear and stencil
          * decompress messes with subsequent stencil buffer
          * uses. Problem was reproduced on Verde, Bonaire,
          * Tonga, and Carrizo.
          *
          * Disabling EXPCLEAR works around the problem.
          *
          * Check piglit's arb_texture_multisample-stencil-clear
          * test if you want to try changing this.
          */
         if (state->num_samples <= 1)
            ds->db_stencil_info |= S_028044_ALLOW_EXPCLEAR(state->allow_expclear);
      }

      ds->u.gfx6.db_htile_data_base = (state->va + surf->meta_offset) >> 8;
      ds->u.gfx6.db_htile_surface = S_028ABC_FULL_CACHE(1);
   }
}

static void
ac_init_gfx9_ds_surface(const struct radeon_info *info, const struct ac_ds_state *state,
                        uint32_t db_format, uint32_t stencil_format, struct ac_ds_surface *ds)
{
   const struct radeon_surf *surf = state->surf;

   assert(surf->u.gfx9.surf_offset == 0);

   ds->u.gfx6.db_htile_data_base = 0;
   ds->u.gfx6.db_htile_surface = 0;
   ds->db_depth_base = state->va >> 8;
   ds->db_stencil_base = (state->va + surf->u.gfx9.zs.stencil_offset) >> 8;
   ds->db_depth_view = S_028008_SLICE_START(state->first_layer) |
                       S_028008_SLICE_MAX(state->last_layer) |
                       S_028008_Z_READ_ONLY(state->z_read_only) |
                       S_028008_STENCIL_READ_ONLY(state->stencil_read_only) |
                       S_028008_MIPID_GFX9(state->level);

   if (info->gfx_level >= GFX10) {
      ds->db_depth_view |= S_028008_SLICE_START_HI(state->first_layer >> 11) |
                           S_028008_SLICE_MAX_HI(state->last_layer >> 11);
   }

   ds->db_z_info = S_028038_FORMAT(db_format) |
                   S_028038_NUM_SAMPLES(util_logbase2(state->num_samples)) |
                   S_028038_SW_MODE(surf->u.gfx9.swizzle_mode) |
                   S_028038_MAXMIP(state->num_levels - 1) |
                   S_028040_ITERATE_256(info->gfx_level >= GFX11);
   ds->db_stencil_info = S_02803C_FORMAT(stencil_format) |
                         S_02803C_SW_MODE(surf->u.gfx9.zs.stencil_swizzle_mode) |
                         S_028044_ITERATE_256(info->gfx_level >= GFX11);

   if (info->gfx_level == GFX9) {
      ds->u.gfx6.db_z_info2 = S_028068_EPITCH(surf->u.gfx9.epitch);
      ds->u.gfx6.db_stencil_info2 = S_02806C_EPITCH(surf->u.gfx9.zs.stencil_epitch);
   }

   ds->db_depth_size = S_02801C_X_MAX(state->width - 1) |
                       S_02801C_Y_MAX(state->height - 1);

   if (state->htile_enabled) {
      ds->db_z_info |= S_028038_TILE_SURFACE_ENABLE(1) |
                       S_028038_ALLOW_EXPCLEAR(state->allow_expclear);
      ds->db_stencil_info |= S_02803C_TILE_STENCIL_DISABLE(state->htile_stencil_disabled);

      if (surf->has_stencil && !state->htile_stencil_disabled && state->num_samples <= 1) {
         /* Stencil buffer workaround ported from the GFX6-GFX8 code.
          * See that for explanation.
          */
         ds->db_stencil_info |= S_02803C_ALLOW_EXPCLEAR(state->allow_expclear);
      }

      ds->u.gfx6.db_htile_data_base = (state->va + surf->meta_offset) >> 8;
      ds->u.gfx6.db_htile_surface = S_028ABC_FULL_CACHE(1) |
                                    S_028ABC_PIPE_ALIGNED(1);

      if (state->vrs_enabled) {
         assert(info->gfx_level == GFX10_3);
         ds->u.gfx6.db_htile_surface |= S_028ABC_VRS_HTILE_ENCODING(V_028ABC_VRS_HTILE_4BIT_ENCODING);
      } else if (info->gfx_level == GFX9) {
         ds->u.gfx6.db_htile_surface |= S_028ABC_RB_ALIGNED(1);
      }
   }
}

static void
ac_init_gfx12_ds_surface(const struct radeon_info *info, const struct ac_ds_state *state,
                         uint32_t db_format, uint32_t stencil_format, struct ac_ds_surface *ds)
{
   const struct radeon_surf *surf = state->surf;

   assert(db_format != V_028040_Z_24);

   ds->db_depth_view = S_028004_SLICE_START(state->first_layer) |
                       S_028004_SLICE_MAX(state->last_layer);
   ds->u.gfx12.db_depth_view1 = S_028008_MIPID_GFX12(state->level);
   ds->db_depth_size = S_028014_X_MAX(state->width - 1) |
                       S_028014_Y_MAX(state->height - 1);
   ds->db_z_info = S_028018_FORMAT(db_format) |
                   S_028018_NUM_SAMPLES(util_logbase2(state->num_samples)) |
                   S_028018_SW_MODE(surf->u.gfx9.swizzle_mode) |
                   S_028018_MAXMIP(state->num_levels - 1);
   ds->db_stencil_info = S_02801C_FORMAT(stencil_format) |
                         S_02801C_SW_MODE(surf->u.gfx9.zs.stencil_swizzle_mode) |
                         S_02801C_TILE_STENCIL_DISABLE(1);
   ds->db_depth_base = state->va >> 8;
   ds->db_stencil_base = (state->va + surf->u.gfx9.zs.stencil_offset) >> 8;
   ds->u.gfx12.hiz_info = 0;
   ds->u.gfx12.his_info = 0;

   /* HiZ. */
   if (surf->u.gfx9.zs.hiz.offset) {
      ds->u.gfx12.hiz_info = S_028B94_SURFACE_ENABLE(1) |
                             S_028B94_FORMAT(0) | /* unorm16 */
                             S_028B94_SW_MODE(surf->u.gfx9.zs.hiz.swizzle_mode);
      ds->u.gfx12.hiz_size_xy = S_028BA4_X_MAX(surf->u.gfx9.zs.hiz.width_in_tiles - 1) |
                                S_028BA4_Y_MAX(surf->u.gfx9.zs.hiz.height_in_tiles - 1);
      ds->u.gfx12.hiz_base = (state->va + surf->u.gfx9.zs.hiz.offset) >> 8;
   }

   /* HiS. */
   if (surf->u.gfx9.zs.his.offset) {
      ds->u.gfx12.his_info = S_028B98_SURFACE_ENABLE(1) |
                             S_028B98_SW_MODE(surf->u.gfx9.zs.his.swizzle_mode);
      ds->u.gfx12.his_size_xy = S_028BB0_X_MAX(surf->u.gfx9.zs.his.width_in_tiles - 1) |
                                S_028BB0_Y_MAX(surf->u.gfx9.zs.his.height_in_tiles - 1);
      ds->u.gfx12.his_base = (state->va + surf->u.gfx9.zs.his.offset) >> 8;
   }
}

void
ac_init_ds_surface(const struct radeon_info *info, const struct ac_ds_state *state, struct ac_ds_surface *ds)
{
   const struct radeon_surf *surf = state->surf;
   const uint32_t db_format = ac_translate_dbformat(state->format);
   const uint32_t stencil_format = surf->has_stencil ? V_028044_STENCIL_8 : V_028044_STENCIL_INVALID;

   if (info->gfx_level >= GFX12) {
      ac_init_gfx12_ds_surface(info, state, db_format, stencil_format, ds);
   } else if (info->gfx_level >= GFX9) {
      ac_init_gfx9_ds_surface(info, state, db_format, stencil_format, ds);
   } else {
      ac_init_gfx6_ds_surface(info, state, db_format, stencil_format, ds);
   }
}

static unsigned
ac_get_decompress_on_z_planes(const struct radeon_info *info, enum pipe_format format, uint8_t log_num_samples,
                              bool htile_stencil_disabled, bool no_d16_compression)
{
   uint32_t max_zplanes = 0;

   if (info->gfx_level >= GFX9) {
      const bool iterate256 = info->gfx_level >= GFX10 && log_num_samples >= 1;

      /* Default value for 32-bit depth surfaces. */
      max_zplanes = 4;

      if (format == PIPE_FORMAT_Z16_UNORM && log_num_samples > 0)
         max_zplanes = 2;

      /* Workaround for a DB hang when ITERATE_256 is set to 1. Only affects 4X MSAA D/S images. */
      if (info->has_two_planes_iterate256_bug && iterate256 && !htile_stencil_disabled && log_num_samples == 2)
         max_zplanes = 1;

      max_zplanes++;
   } else {
      if (format == PIPE_FORMAT_Z16_UNORM && no_d16_compression) {
         /* Do not enable Z plane compression for 16-bit depth
          * surfaces because isn't supported on GFX8. Only
          * 32-bit depth surfaces are supported by the hardware.
          * This allows to maintain shader compatibility and to
          * reduce the number of depth decompressions.
          */
         max_zplanes = 1;
      } else {
         /* 0 = full compression. N = only compress up to N-1 Z planes. */
         if (log_num_samples == 0)
            max_zplanes = 5;
         else if (log_num_samples <= 2)
            max_zplanes = 3;
         else
            max_zplanes = 2;
      }
   }

   return max_zplanes;
}

void
ac_set_mutable_ds_surface_fields(const struct radeon_info *info, const struct ac_mutable_ds_state *state,
                                 struct ac_ds_surface *ds)
{
   bool tile_stencil_disable = false;
   uint32_t log_num_samples;

   memcpy(ds, state->ds, sizeof(*ds));

   if (info->gfx_level >= GFX12)
      return;

   if (info->gfx_level >= GFX9) {
      log_num_samples = G_028038_NUM_SAMPLES(ds->db_z_info);
      tile_stencil_disable = G_02803C_TILE_STENCIL_DISABLE(ds->db_stencil_info);
   } else {
      log_num_samples = G_028040_NUM_SAMPLES(ds->db_z_info);
   }

   const uint32_t max_zplanes =
      ac_get_decompress_on_z_planes(info, state->format, log_num_samples,
                                    tile_stencil_disable, state->no_d16_compression);

   if (info->gfx_level >= GFX9) {
      if (state->tc_compat_htile_enabled) {
         ds->db_z_info |= S_028038_DECOMPRESS_ON_N_ZPLANES(max_zplanes);

         if (info->gfx_level >= GFX10) {
            const bool iterate256 = log_num_samples >= 1;

            ds->db_z_info |= S_028040_ITERATE_FLUSH(1);
            ds->db_stencil_info |= S_028044_ITERATE_FLUSH(!tile_stencil_disable);
            ds->db_z_info |= S_028040_ITERATE_256(iterate256);
            ds->db_stencil_info |= S_028044_ITERATE_256(iterate256);
         } else {
            ds->db_z_info |= S_028038_ITERATE_FLUSH(1);
            ds->db_stencil_info |= S_02803C_ITERATE_FLUSH(1);
         }
      }

      ds->db_z_info |= S_028038_ZRANGE_PRECISION(state->zrange_precision);
   } else {
      if (state->tc_compat_htile_enabled) {
         ds->u.gfx6.db_htile_surface |= S_028ABC_TC_COMPATIBLE(1);
         ds->db_z_info |= S_028040_DECOMPRESS_ON_N_ZPLANES(max_zplanes);
      } else {
         ds->u.gfx6.db_depth_info |= S_02803C_ADDR5_SWIZZLE_MASK(1);
      }

      ds->db_z_info |= S_028040_ZRANGE_PRECISION(state->zrange_precision);
   }
}

static uint32_t
ac_get_dcc_min_compressed_block_size(const struct radeon_info *info)
{
   /* This should typically match the request size of the memory type. DIMMs have 64B minimum
    * request size, which means compressing 64B to 32B has no benefit, while GDDR and HBM have
    * 32B minimum request size. Sometimes a different size is used depending on the data fabric,
    * etc.
    */
   return info->has_dedicated_vram || info->family == CHIP_GFX1151 ?
            V_028C78_MIN_BLOCK_SIZE_32B : V_028C78_MIN_BLOCK_SIZE_64B;
}

static void
ac_init_gfx6_cb_surface(const struct radeon_info *info, const struct ac_cb_state *state,
                        uint32_t cb_format, bool force_dst_alpha_1, struct ac_cb_surface *cb)
{
   const struct radeon_surf *surf = state->surf;
   const uint32_t endian = ac_colorformat_endian_swap(cb_format);

   cb->cb_color_info |= S_028C70_ENDIAN(endian) |
                        S_028C70_FORMAT_GFX6(cb_format) |
                        S_028C70_COMPRESSION(!!surf->fmask_offset);
   cb->cb_color_view = S_028C6C_SLICE_START(state->first_layer) |
                       S_028C6C_SLICE_MAX_GFX6(state->last_layer);
   cb->cb_color_attrib = S_028C74_NUM_SAMPLES(util_logbase2(state->num_samples)) |
                         S_028C74_NUM_FRAGMENTS_GFX6(util_logbase2(state->num_storage_samples)) |
                         S_028C74_FORCE_DST_ALPHA_1_GFX6(force_dst_alpha_1);
   cb->cb_color_attrib2 = 0;
   cb->cb_dcc_control = 0;

   if (info->gfx_level == GFX9) {
      cb->cb_color_view |= S_028C6C_MIP_LEVEL_GFX9(state->base_level);
      cb->cb_color_attrib |= S_028C74_MIP0_DEPTH(state->num_layers) |
                             S_028C74_RESOURCE_TYPE(surf->u.gfx9.resource_type);
      cb->cb_color_attrib2 |= S_028C68_MIP0_WIDTH(state->width - 1) |
                              S_028C68_MIP0_HEIGHT(state->height - 1) |
                              S_028C68_MAX_MIP(state->num_levels - 1);
   }

   if (info->gfx_level >= GFX8) {
      uint32_t max_uncompressed_block_size = V_028C78_MAX_BLOCK_SIZE_256B;

      if (state->num_storage_samples > 1) {
         if (surf->bpe == 1)
            max_uncompressed_block_size = V_028C78_MAX_BLOCK_SIZE_64B;
         else if (surf->bpe == 2)
            max_uncompressed_block_size = V_028C78_MAX_BLOCK_SIZE_128B;
      }

      cb->cb_dcc_control |= S_028C78_MAX_UNCOMPRESSED_BLOCK_SIZE(max_uncompressed_block_size) |
                            S_028C78_MIN_COMPRESSED_BLOCK_SIZE(ac_get_dcc_min_compressed_block_size(info)) |
                            S_028C78_INDEPENDENT_64B_BLOCKS(1);
   }

   if (info->gfx_level == GFX6) {
      /* Due to a hw bug, FMASK_BANK_HEIGHT must still be set on GFX6. (inherited from GFX5) */
      /* This must also be set for fast clear to work without FMASK. */
      const uint32_t fmask_bankh = surf->fmask_offset ? surf->u.legacy.color.fmask.bankh
                                                      : surf->u.legacy.bankh;
      cb->cb_color_attrib |= S_028C74_FMASK_BANK_HEIGHT(util_logbase2(fmask_bankh));
   }
}

static void
ac_init_gfx10_cb_surface(const struct radeon_info *info, const struct ac_cb_state *state,
                         uint32_t cb_format, bool force_dst_alpha_1, uint32_t width,
                         struct ac_cb_surface *cb)
{
   const struct radeon_surf *surf = state->surf;
   uint32_t first_layer = state->first_layer;
   uint32_t base_level = state->base_level;
   uint32_t num_levels = state->num_levels;

   if (state->gfx10.nbc_view) {
      assert(state->gfx10.nbc_view->valid);
      first_layer = 0;
      base_level = state->gfx10.nbc_view->level;
      num_levels = state->gfx10.nbc_view->num_levels;
   }

   cb->cb_color_view = S_028C6C_SLICE_START(first_layer) |
                       S_028C6C_SLICE_MAX_GFX10(state->last_layer) |
                       S_028C6C_MIP_LEVEL_GFX10(base_level);
   cb->cb_color_attrib = 0;
   cb->cb_color_attrib2 = S_028C68_MIP0_WIDTH(width - 1) |
                          S_028C68_MIP0_HEIGHT(state->height - 1) |
                          S_028C68_MAX_MIP(num_levels - 1);
   cb->cb_color_attrib3 = S_028EE0_MIP0_DEPTH(state->num_layers) |
                          S_028EE0_RESOURCE_TYPE(surf->u.gfx9.resource_type) |
                          S_028EE0_RESOURCE_LEVEL(info->gfx_level >= GFX11 ? 0 : 1);
   cb->cb_dcc_control = S_028C78_MAX_UNCOMPRESSED_BLOCK_SIZE(V_028C78_MAX_BLOCK_SIZE_256B) |
                        S_028C78_MAX_COMPRESSED_BLOCK_SIZE(surf->u.gfx9.color.dcc.max_compressed_block_size) |
                        S_028C78_MIN_COMPRESSED_BLOCK_SIZE(ac_get_dcc_min_compressed_block_size(info)) |
                        S_028C78_INDEPENDENT_64B_BLOCKS(surf->u.gfx9.color.dcc.independent_64B_blocks);

   if (info->gfx_level >= GFX11) {
      assert(!UTIL_ARCH_BIG_ENDIAN);
      cb->cb_color_info |= S_028C70_FORMAT_GFX11(cb_format);
      cb->cb_color_attrib |= S_028C74_NUM_FRAGMENTS_GFX11(util_logbase2(state->num_storage_samples)) |
                             S_028C74_FORCE_DST_ALPHA_1_GFX11(force_dst_alpha_1);
      cb->cb_dcc_control |= S_028C78_INDEPENDENT_128B_BLOCKS_GFX11(surf->u.gfx9.color.dcc.independent_128B_blocks);
   } else {
      const uint32_t endian = ac_colorformat_endian_swap(cb_format);

      cb->cb_color_info |= S_028C70_ENDIAN(endian) |
                           S_028C70_FORMAT_GFX6(cb_format) |
                           S_028C70_COMPRESSION(!!surf->fmask_offset);
      cb->cb_color_attrib |= S_028C74_NUM_SAMPLES(util_logbase2(state->num_samples)) |
                             S_028C74_NUM_FRAGMENTS_GFX6(util_logbase2(state->num_storage_samples)) |
                             S_028C74_FORCE_DST_ALPHA_1_GFX6(force_dst_alpha_1);
      cb->cb_dcc_control |= S_028C78_INDEPENDENT_128B_BLOCKS_GFX10(surf->u.gfx9.color.dcc.independent_128B_blocks);
   }
}

static void
ac_init_gfx12_cb_surface(const struct radeon_info *info, const struct ac_cb_state *state,
                         uint32_t cb_format, bool force_dst_alpha_1, uint32_t width,
                         struct ac_cb_surface *cb)
{
   const struct radeon_surf *surf = state->surf;

   assert(!UTIL_ARCH_BIG_ENDIAN);
   cb->cb_color_info |= S_028EC0_FORMAT(cb_format);
   cb->cb_color_view = S_028C64_SLICE_START(state->first_layer) |
                       S_028C64_SLICE_MAX(state->last_layer);
   cb->cb_color_view2 = S_028C68_MIP_LEVEL(state->base_level);
   cb->cb_color_attrib = S_028C6C_NUM_FRAGMENTS(util_logbase2(state->num_storage_samples)) |
                         S_028C6C_FORCE_DST_ALPHA_1(force_dst_alpha_1);
   cb->cb_color_attrib2 = S_028C78_MIP0_HEIGHT(state->height - 1) |
                          S_028C78_MIP0_WIDTH(width - 1);
   cb->cb_color_attrib3 = S_028C7C_MIP0_DEPTH(state->num_layers) |
                          S_028C7C_MAX_MIP(state->num_levels - 1) |
                          S_028C7C_RESOURCE_TYPE(surf->u.gfx9.resource_type);
   cb->cb_dcc_control = S_028C70_MAX_UNCOMPRESSED_BLOCK_SIZE(1) | /* 256B */
                        S_028C70_MAX_COMPRESSED_BLOCK_SIZE(surf->u.gfx9.color.dcc.max_compressed_block_size) |
                        S_028C70_ENABLE_MAX_COMP_FRAG_OVERRIDE(1) |
                        S_028C70_MAX_COMP_FRAGS(state->num_samples >= 8 ? 3 :
                                                state->num_samples >= 4 ? 2 : 0);
}

void
ac_init_cb_surface(const struct radeon_info *info, const struct ac_cb_state *state, struct ac_cb_surface *cb)
{
   const struct util_format_description *desc = util_format_description(state->format);
   const uint32_t cb_format = ac_get_cb_format(info->gfx_level, state->format);
   const struct radeon_surf *surf = state->surf;
   uint32_t width = state->width;

   assert(cb_format != V_028C70_COLOR_INVALID);

   /* Intensity is implemented as Red, so treat it that way. */
   const bool force_dst_alpha_1 =
      desc->swizzle[3] == PIPE_SWIZZLE_1 || util_format_is_intensity(state->format);

   /* GFX10.3+ can set a custom pitch for 1D and 2D non-array, but it must be a multiple of
    * 256B for GFX10.3-11 and 128B for GFX12.
    *
    * We set the pitch in MIP0_WIDTH.
    */
   if (info->gfx_level >= GFX10_3 && surf->u.gfx9.uses_custom_pitch) {
      ASSERTED unsigned min_alignment = info->gfx_level >= GFX12 ? 128 : 256;
      assert((surf->u.gfx9.surf_pitch * surf->bpe) % min_alignment == 0);
      assert(surf->is_linear);

      width = surf->u.gfx9.surf_pitch;

      /* Subsampled images have the pitch in the units of blocks. */
      if (surf->blk_w == 2)
         width *= 2;
   }

   const uint32_t swap = ac_translate_colorswap(info->gfx_level, state->format, false);
   const uint32_t ntype = ac_get_cb_number_type(state->format);
   uint32_t blend_clamp = 0, blend_bypass = 0;

   /* blend clamp should be set for all NORM/SRGB types */
   if (ntype == V_028C70_NUMBER_UNORM || ntype == V_028C70_NUMBER_SNORM ||
       ntype == V_028C70_NUMBER_SRGB)
      blend_clamp = 1;

   /* set blend bypass according to docs if SINT/UINT or 8/24 COLOR variants */
   if (ntype == V_028C70_NUMBER_UINT || ntype == V_028C70_NUMBER_SINT ||
       cb_format == V_028C70_COLOR_8_24 || cb_format == V_028C70_COLOR_24_8 ||
       cb_format == V_028C70_COLOR_X24_8_32_FLOAT) {
      blend_clamp = 0;
      blend_bypass = 1;
   }

   const bool round_mode = ntype != V_028C70_NUMBER_UNORM &&
                           ntype != V_028C70_NUMBER_SNORM &&
                           ntype != V_028C70_NUMBER_SRGB &&
                           cb_format != V_028C70_COLOR_8_24 &&
                           cb_format != V_028C70_COLOR_24_8;

   cb->cb_color_info = S_028C70_COMP_SWAP(swap) |
                       S_028C70_BLEND_CLAMP(blend_clamp) |
                       S_028C70_BLEND_BYPASS(blend_bypass) |
                       S_028C70_SIMPLE_FLOAT(1) |
                       S_028C70_ROUND_MODE(round_mode) |
                       S_028C70_NUMBER_TYPE(ntype);

   if (info->gfx_level >= GFX12) {
      ac_init_gfx12_cb_surface(info, state, cb_format, force_dst_alpha_1, width, cb);
   } else if (info->gfx_level >= GFX10) {
      ac_init_gfx10_cb_surface(info, state, cb_format, force_dst_alpha_1, width, cb);
   } else {
      ac_init_gfx6_cb_surface(info, state, cb_format, force_dst_alpha_1, cb);
   }
}

void
ac_set_mutable_cb_surface_fields(const struct radeon_info *info, const struct ac_mutable_cb_state *state,
                                 struct ac_cb_surface *cb)
{
   const struct radeon_surf *surf = state->surf;
   uint8_t tile_swizzle = surf->tile_swizzle;
   uint64_t va = state->va;

   memcpy(cb, state->cb, sizeof(*cb));

   if (state->gfx10.nbc_view) {
      assert(state->gfx10.nbc_view->valid);
      va += state->gfx10.nbc_view->base_address_offset;
      tile_swizzle = state->gfx10.nbc_view->tile_swizzle;
   }

   cb->cb_color_base = va >> 8;

   if (info->gfx_level >= GFX9) {
      cb->cb_color_base += surf->u.gfx9.surf_offset >> 8;
      cb->cb_color_base |= tile_swizzle;
   } else {
      const struct legacy_surf_level *level_info = &surf->u.legacy.level[state->base_level];

      cb->cb_color_base += level_info->offset_256B;

      /* Only macrotiled modes can set tile swizzle. */
      if (level_info->mode == RADEON_SURF_MODE_2D)
         cb->cb_color_base |= tile_swizzle;
   }

   if (info->gfx_level >= GFX12) {
      cb->cb_color_attrib3 |= S_028C7C_COLOR_SW_MODE(surf->u.gfx9.swizzle_mode);
      return;
   }

   /* Set up DCC. */
   if (state->dcc_enabled) {
      cb->cb_dcc_base = (va + surf->meta_offset) >> 8;

      if (info->gfx_level == GFX8)
         cb->cb_dcc_base += surf->u.legacy.color.dcc_level[state->base_level].dcc_offset >> 8;

      uint32_t dcc_tile_swizzle = tile_swizzle;
      dcc_tile_swizzle &= ((1 << surf->meta_alignment_log2) - 1) >> 8;
      cb->cb_dcc_base |= dcc_tile_swizzle;
   }

   if (info->gfx_level >= GFX11) {
      cb->cb_color_attrib3 |= S_028EE0_COLOR_SW_MODE(surf->u.gfx9.swizzle_mode) |
                              S_028EE0_DCC_PIPE_ALIGNED(surf->u.gfx9.color.dcc.pipe_aligned);

      if (state->dcc_enabled) {
         cb->cb_dcc_control |= S_028C78_DISABLE_CONSTANT_ENCODE_REG(1) |
                               S_028C78_FDCC_ENABLE(1);

         if (info->family >= CHIP_GFX1103_R2) {
            cb->cb_dcc_control |= S_028C78_ENABLE_MAX_COMP_FRAG_OVERRIDE(1) |
                                  S_028C78_MAX_COMP_FRAGS(state->num_samples >= 4);
         }
      }
   } else if (info->gfx_level >= GFX10) {
      cb->cb_color_attrib3 |= S_028EE0_COLOR_SW_MODE(surf->u.gfx9.swizzle_mode) |
                              S_028EE0_FMASK_SW_MODE(surf->u.gfx9.color.fmask_swizzle_mode) |
                              S_028EE0_CMASK_PIPE_ALIGNED(1) |
                              S_028EE0_DCC_PIPE_ALIGNED(surf->u.gfx9.color.dcc.pipe_aligned);
   } else if (info->gfx_level == GFX9) {
      struct gfx9_surf_meta_flags meta = {
         .rb_aligned = 1,
         .pipe_aligned = 1,
      };

      if (!(surf->flags & RADEON_SURF_Z_OR_SBUFFER) && surf->meta_offset)
         meta = surf->u.gfx9.color.dcc;

      cb->cb_color_attrib |= S_028C74_COLOR_SW_MODE(surf->u.gfx9.swizzle_mode) |
                             S_028C74_FMASK_SW_MODE(surf->u.gfx9.color.fmask_swizzle_mode) |
                             S_028C74_RB_ALIGNED(meta.rb_aligned) |
                             S_028C74_PIPE_ALIGNED(meta.pipe_aligned);
      cb->cb_mrt_epitch = S_0287A0_EPITCH(surf->u.gfx9.epitch);
   } else {
      /* GFX6-8 */
      const struct legacy_surf_level *level_info = &surf->u.legacy.level[state->base_level];
      uint32_t pitch_tile_max, slice_tile_max, tile_mode_index;

      pitch_tile_max = level_info->nblk_x / 8 - 1;
      slice_tile_max = (level_info->nblk_x * level_info->nblk_y) / 64 - 1;
      tile_mode_index = ac_tile_mode_index(surf, state->base_level, false);

      cb->cb_color_attrib |= S_028C74_TILE_MODE_INDEX(tile_mode_index);
      cb->cb_color_pitch = S_028C64_TILE_MAX(pitch_tile_max);
      cb->cb_color_slice = S_028C68_TILE_MAX(slice_tile_max);

      cb->cb_color_cmask_slice = surf->u.legacy.color.cmask_slice_tile_max;

      if (state->fmask_enabled) {
         if (info->gfx_level >= GFX7)
            cb->cb_color_pitch |= S_028C64_FMASK_TILE_MAX(surf->u.legacy.color.fmask.pitch_in_pixels / 8 - 1);
         cb->cb_color_attrib |= S_028C74_FMASK_TILE_MODE_INDEX(surf->u.legacy.color.fmask.tiling_index);
         cb->cb_color_fmask_slice = S_028C88_TILE_MAX(surf->u.legacy.color.fmask.slice_tile_max);
      } else {
         /* This must be set for fast clear to work without FMASK. */
         if (info->gfx_level >= GFX7)
            cb->cb_color_pitch |= S_028C64_FMASK_TILE_MAX(pitch_tile_max);
         cb->cb_color_attrib |= S_028C74_FMASK_TILE_MODE_INDEX(tile_mode_index);
         cb->cb_color_fmask_slice = S_028C88_TILE_MAX(slice_tile_max);
      }
   }

   if (state->cmask_enabled) {
      cb->cb_color_cmask = (va + surf->cmask_offset) >> 8;
      cb->cb_color_info |= S_028C70_FAST_CLEAR(state->fast_clear_enabled);
   } else {
      cb->cb_color_cmask = cb->cb_color_base;
   }

   if (state->fmask_enabled) {
      cb->cb_color_fmask = (va + surf->fmask_offset) >> 8;
      cb->cb_color_fmask |= surf->fmask_tile_swizzle;

      if (state->tc_compat_cmask_enabled) {
         assert(state->cmask_enabled);

         /* Allow the texture block to read FMASK directly without decompressing it. */
         cb->cb_color_info |= S_028C70_FMASK_COMPRESS_1FRAG_ONLY(1);

         if (info->gfx_level == GFX8) {
            /* Set CMASK into a tiling format that allows
             * the texture block to read it.
             */
            cb->cb_color_info |= S_028C70_CMASK_ADDR_TYPE(2);
         }
      }
   } else {
      cb->cb_color_fmask = cb->cb_color_base;
   }

   if (info->gfx_level < GFX11)
      cb->cb_color_info |= S_028C70_DCC_ENABLE(state->dcc_enabled);
}
