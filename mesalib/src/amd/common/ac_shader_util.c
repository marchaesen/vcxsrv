/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
 */

#include "ac_shader_util.h"
#include "ac_gpu_info.h"

#include "sid.h"
#include "u_math.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

unsigned ac_get_spi_shader_z_format(bool writes_z, bool writes_stencil, bool writes_samplemask)
{
   if (writes_z) {
      /* Z needs 32 bits. */
      if (writes_samplemask)
         return V_028710_SPI_SHADER_32_ABGR;
      else if (writes_stencil)
         return V_028710_SPI_SHADER_32_GR;
      else
         return V_028710_SPI_SHADER_32_R;
   } else if (writes_stencil || writes_samplemask) {
      /* Both stencil and sample mask need only 16 bits. */
      return V_028710_SPI_SHADER_UINT16_ABGR;
   } else {
      return V_028710_SPI_SHADER_ZERO;
   }
}

unsigned ac_get_cb_shader_mask(unsigned spi_shader_col_format)
{
   unsigned i, cb_shader_mask = 0;

   for (i = 0; i < 8; i++) {
      switch ((spi_shader_col_format >> (i * 4)) & 0xf) {
      case V_028714_SPI_SHADER_ZERO:
         break;
      case V_028714_SPI_SHADER_32_R:
         cb_shader_mask |= 0x1 << (i * 4);
         break;
      case V_028714_SPI_SHADER_32_GR:
         cb_shader_mask |= 0x3 << (i * 4);
         break;
      case V_028714_SPI_SHADER_32_AR:
         cb_shader_mask |= 0x9u << (i * 4);
         break;
      case V_028714_SPI_SHADER_FP16_ABGR:
      case V_028714_SPI_SHADER_UNORM16_ABGR:
      case V_028714_SPI_SHADER_SNORM16_ABGR:
      case V_028714_SPI_SHADER_UINT16_ABGR:
      case V_028714_SPI_SHADER_SINT16_ABGR:
      case V_028714_SPI_SHADER_32_ABGR:
         cb_shader_mask |= 0xfu << (i * 4);
         break;
      default:
         assert(0);
      }
   }
   return cb_shader_mask;
}

/**
 * Calculate the appropriate setting of VGT_GS_MODE when \p shader is a
 * geometry shader.
 */
uint32_t ac_vgt_gs_mode(unsigned gs_max_vert_out, enum chip_class chip_class)
{
   unsigned cut_mode;

   if (gs_max_vert_out <= 128) {
      cut_mode = V_028A40_GS_CUT_128;
   } else if (gs_max_vert_out <= 256) {
      cut_mode = V_028A40_GS_CUT_256;
   } else if (gs_max_vert_out <= 512) {
      cut_mode = V_028A40_GS_CUT_512;
   } else {
      assert(gs_max_vert_out <= 1024);
      cut_mode = V_028A40_GS_CUT_1024;
   }

   return S_028A40_MODE(V_028A40_GS_SCENARIO_G) | S_028A40_CUT_MODE(cut_mode) |
          S_028A40_ES_WRITE_OPTIMIZE(chip_class <= GFX8) | S_028A40_GS_WRITE_OPTIMIZE(1) |
          S_028A40_ONCHIP(chip_class >= GFX9 ? 1 : 0);
}

/// Translate a (dfmt, nfmt) pair into a chip-appropriate combined format
/// value for LLVM8+ tbuffer intrinsics.
unsigned ac_get_tbuffer_format(enum chip_class chip_class, unsigned dfmt, unsigned nfmt)
{
   // Some games try to access vertex buffers without a valid format.
   // This is a game bug, but we should still handle it gracefully.
   if (dfmt == V_008F0C_GFX10_FORMAT_INVALID)
      return V_008F0C_GFX10_FORMAT_INVALID;

   if (chip_class >= GFX10) {
      unsigned format;
      switch (dfmt) {
      default:
         unreachable("bad dfmt");
      case V_008F0C_BUF_DATA_FORMAT_INVALID:
         format = V_008F0C_GFX10_FORMAT_INVALID;
         break;
      case V_008F0C_BUF_DATA_FORMAT_8:
         format = V_008F0C_GFX10_FORMAT_8_UINT;
         break;
      case V_008F0C_BUF_DATA_FORMAT_8_8:
         format = V_008F0C_GFX10_FORMAT_8_8_UINT;
         break;
      case V_008F0C_BUF_DATA_FORMAT_8_8_8_8:
         format = V_008F0C_GFX10_FORMAT_8_8_8_8_UINT;
         break;
      case V_008F0C_BUF_DATA_FORMAT_16:
         format = V_008F0C_GFX10_FORMAT_16_UINT;
         break;
      case V_008F0C_BUF_DATA_FORMAT_16_16:
         format = V_008F0C_GFX10_FORMAT_16_16_UINT;
         break;
      case V_008F0C_BUF_DATA_FORMAT_16_16_16_16:
         format = V_008F0C_GFX10_FORMAT_16_16_16_16_UINT;
         break;
      case V_008F0C_BUF_DATA_FORMAT_32:
         format = V_008F0C_GFX10_FORMAT_32_UINT;
         break;
      case V_008F0C_BUF_DATA_FORMAT_32_32:
         format = V_008F0C_GFX10_FORMAT_32_32_UINT;
         break;
      case V_008F0C_BUF_DATA_FORMAT_32_32_32:
         format = V_008F0C_GFX10_FORMAT_32_32_32_UINT;
         break;
      case V_008F0C_BUF_DATA_FORMAT_32_32_32_32:
         format = V_008F0C_GFX10_FORMAT_32_32_32_32_UINT;
         break;
      case V_008F0C_BUF_DATA_FORMAT_2_10_10_10:
         format = V_008F0C_GFX10_FORMAT_2_10_10_10_UINT;
         break;
      case V_008F0C_BUF_DATA_FORMAT_10_11_11:
         format = V_008F0C_GFX10_FORMAT_10_11_11_UINT;
         break;
      }

      // Use the regularity properties of the combined format enum.
      //
      // Note: float is incompatible with 8-bit data formats,
      //       [us]{norm,scaled} are incomparible with 32-bit data formats.
      //       [us]scaled are not writable.
      switch (nfmt) {
      case V_008F0C_BUF_NUM_FORMAT_UNORM:
         format -= 4;
         break;
      case V_008F0C_BUF_NUM_FORMAT_SNORM:
         format -= 3;
         break;
      case V_008F0C_BUF_NUM_FORMAT_USCALED:
         format -= 2;
         break;
      case V_008F0C_BUF_NUM_FORMAT_SSCALED:
         format -= 1;
         break;
      default:
         unreachable("bad nfmt");
      case V_008F0C_BUF_NUM_FORMAT_UINT:
         break;
      case V_008F0C_BUF_NUM_FORMAT_SINT:
         format += 1;
         break;
      case V_008F0C_BUF_NUM_FORMAT_FLOAT:
         format += 2;
         break;
      }

      return format;
   } else {
      return dfmt | (nfmt << 4);
   }
}

static const struct ac_data_format_info data_format_table[] = {
   [V_008F0C_BUF_DATA_FORMAT_INVALID] = {0, 4, 0, V_008F0C_BUF_DATA_FORMAT_INVALID},
   [V_008F0C_BUF_DATA_FORMAT_8] = {1, 1, 1, V_008F0C_BUF_DATA_FORMAT_8},
   [V_008F0C_BUF_DATA_FORMAT_16] = {2, 1, 2, V_008F0C_BUF_DATA_FORMAT_16},
   [V_008F0C_BUF_DATA_FORMAT_8_8] = {2, 2, 1, V_008F0C_BUF_DATA_FORMAT_8},
   [V_008F0C_BUF_DATA_FORMAT_32] = {4, 1, 4, V_008F0C_BUF_DATA_FORMAT_32},
   [V_008F0C_BUF_DATA_FORMAT_16_16] = {4, 2, 2, V_008F0C_BUF_DATA_FORMAT_16},
   [V_008F0C_BUF_DATA_FORMAT_10_11_11] = {4, 3, 0, V_008F0C_BUF_DATA_FORMAT_10_11_11},
   [V_008F0C_BUF_DATA_FORMAT_11_11_10] = {4, 3, 0, V_008F0C_BUF_DATA_FORMAT_11_11_10},
   [V_008F0C_BUF_DATA_FORMAT_10_10_10_2] = {4, 4, 0, V_008F0C_BUF_DATA_FORMAT_10_10_10_2},
   [V_008F0C_BUF_DATA_FORMAT_2_10_10_10] = {4, 4, 0, V_008F0C_BUF_DATA_FORMAT_2_10_10_10},
   [V_008F0C_BUF_DATA_FORMAT_8_8_8_8] = {4, 4, 1, V_008F0C_BUF_DATA_FORMAT_8},
   [V_008F0C_BUF_DATA_FORMAT_32_32] = {8, 2, 4, V_008F0C_BUF_DATA_FORMAT_32},
   [V_008F0C_BUF_DATA_FORMAT_16_16_16_16] = {8, 4, 2, V_008F0C_BUF_DATA_FORMAT_16},
   [V_008F0C_BUF_DATA_FORMAT_32_32_32] = {12, 3, 4, V_008F0C_BUF_DATA_FORMAT_32},
   [V_008F0C_BUF_DATA_FORMAT_32_32_32_32] = {16, 4, 4, V_008F0C_BUF_DATA_FORMAT_32},
};

const struct ac_data_format_info *ac_get_data_format_info(unsigned dfmt)
{
   assert(dfmt < ARRAY_SIZE(data_format_table));
   return &data_format_table[dfmt];
}

enum ac_image_dim ac_get_sampler_dim(enum chip_class chip_class, enum glsl_sampler_dim dim,
                                     bool is_array)
{
   switch (dim) {
   case GLSL_SAMPLER_DIM_1D:
      if (chip_class == GFX9)
         return is_array ? ac_image_2darray : ac_image_2d;
      return is_array ? ac_image_1darray : ac_image_1d;
   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_RECT:
   case GLSL_SAMPLER_DIM_EXTERNAL:
      return is_array ? ac_image_2darray : ac_image_2d;
   case GLSL_SAMPLER_DIM_3D:
      return ac_image_3d;
   case GLSL_SAMPLER_DIM_CUBE:
      return ac_image_cube;
   case GLSL_SAMPLER_DIM_MS:
      return is_array ? ac_image_2darraymsaa : ac_image_2dmsaa;
   case GLSL_SAMPLER_DIM_SUBPASS:
      return ac_image_2darray;
   case GLSL_SAMPLER_DIM_SUBPASS_MS:
      return ac_image_2darraymsaa;
   default:
      unreachable("bad sampler dim");
   }
}

enum ac_image_dim ac_get_image_dim(enum chip_class chip_class, enum glsl_sampler_dim sdim,
                                   bool is_array)
{
   enum ac_image_dim dim = ac_get_sampler_dim(chip_class, sdim, is_array);

   /* Match the resource type set in the descriptor. */
   if (dim == ac_image_cube || (chip_class <= GFX8 && dim == ac_image_3d))
      dim = ac_image_2darray;
   else if (sdim == GLSL_SAMPLER_DIM_2D && !is_array && chip_class == GFX9) {
      /* When a single layer of a 3D texture is bound, the shader
       * will refer to a 2D target, but the descriptor has a 3D type.
       * Since the HW ignores BASE_ARRAY in this case, we need to
       * send 3 coordinates. This doesn't hurt when the underlying
       * texture is non-3D.
       */
      dim = ac_image_3d;
   }

   return dim;
}

unsigned ac_get_fs_input_vgpr_cnt(const struct ac_shader_config *config,
                                  signed char *face_vgpr_index_ptr,
                                  signed char *ancillary_vgpr_index_ptr)
{
   unsigned num_input_vgprs = 0;
   signed char face_vgpr_index = -1;
   signed char ancillary_vgpr_index = -1;

   if (G_0286CC_PERSP_SAMPLE_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 2;
   if (G_0286CC_PERSP_CENTER_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 2;
   if (G_0286CC_PERSP_CENTROID_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 2;
   if (G_0286CC_PERSP_PULL_MODEL_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 3;
   if (G_0286CC_LINEAR_SAMPLE_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 2;
   if (G_0286CC_LINEAR_CENTER_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 2;
   if (G_0286CC_LINEAR_CENTROID_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 2;
   if (G_0286CC_LINE_STIPPLE_TEX_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 1;
   if (G_0286CC_POS_X_FLOAT_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 1;
   if (G_0286CC_POS_Y_FLOAT_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 1;
   if (G_0286CC_POS_Z_FLOAT_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 1;
   if (G_0286CC_POS_W_FLOAT_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 1;
   if (G_0286CC_FRONT_FACE_ENA(config->spi_ps_input_addr)) {
      face_vgpr_index = num_input_vgprs;
      num_input_vgprs += 1;
   }
   if (G_0286CC_ANCILLARY_ENA(config->spi_ps_input_addr)) {
      ancillary_vgpr_index = num_input_vgprs;
      num_input_vgprs += 1;
   }
   if (G_0286CC_SAMPLE_COVERAGE_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 1;
   if (G_0286CC_POS_FIXED_PT_ENA(config->spi_ps_input_addr))
      num_input_vgprs += 1;

   if (face_vgpr_index_ptr)
      *face_vgpr_index_ptr = face_vgpr_index;
   if (ancillary_vgpr_index_ptr)
      *ancillary_vgpr_index_ptr = ancillary_vgpr_index;

   return num_input_vgprs;
}

void ac_choose_spi_color_formats(unsigned format, unsigned swap, unsigned ntype,
                                 bool is_depth, bool use_rbplus,
                                 struct ac_spi_color_formats *formats)
{
   /* Alpha is needed for alpha-to-coverage.
    * Blending may be with or without alpha.
    */
   unsigned normal = 0;      /* most optimal, may not support blending or export alpha */
   unsigned alpha = 0;       /* exports alpha, but may not support blending */
   unsigned blend = 0;       /* supports blending, but may not export alpha */
   unsigned blend_alpha = 0; /* least optimal, supports blending and exports alpha */

   /* Choose the SPI color formats. These are required values for RB+.
    * Other chips have multiple choices, though they are not necessarily better.
    */
   switch (format) {
   case V_028C70_COLOR_5_6_5:
   case V_028C70_COLOR_1_5_5_5:
   case V_028C70_COLOR_5_5_5_1:
   case V_028C70_COLOR_4_4_4_4:
   case V_028C70_COLOR_10_11_11:
   case V_028C70_COLOR_11_11_10:
   case V_028C70_COLOR_5_9_9_9:
   case V_028C70_COLOR_8:
   case V_028C70_COLOR_8_8:
   case V_028C70_COLOR_8_8_8_8:
   case V_028C70_COLOR_10_10_10_2:
   case V_028C70_COLOR_2_10_10_10:
      if (ntype == V_028C70_NUMBER_UINT)
         alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_UINT16_ABGR;
      else if (ntype == V_028C70_NUMBER_SINT)
         alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_SINT16_ABGR;
      else
         alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_FP16_ABGR;

      if (!use_rbplus && format == V_028C70_COLOR_8 &&
          ntype != V_028C70_NUMBER_SRGB && swap == V_028C70_SWAP_STD) /* R */ {
         /* When RB+ is enabled, R8_UNORM should use FP16_ABGR for 2x
          * exporting performance. Otherwise, use 32_R to remove useless
          * instructions needed for 16-bit compressed exports.
          */
         blend = normal = V_028714_SPI_SHADER_32_R;
      }
      break;

   case V_028C70_COLOR_16:
   case V_028C70_COLOR_16_16:
   case V_028C70_COLOR_16_16_16_16:
      if (ntype == V_028C70_NUMBER_UNORM || ntype == V_028C70_NUMBER_SNORM) {
         /* UNORM16 and SNORM16 don't support blending */
         if (ntype == V_028C70_NUMBER_UNORM)
            normal = alpha = V_028714_SPI_SHADER_UNORM16_ABGR;
         else
            normal = alpha = V_028714_SPI_SHADER_SNORM16_ABGR;

         /* Use 32 bits per channel for blending. */
         if (format == V_028C70_COLOR_16) {
            if (swap == V_028C70_SWAP_STD) { /* R */
               blend = V_028714_SPI_SHADER_32_R;
               blend_alpha = V_028714_SPI_SHADER_32_AR;
            } else if (swap == V_028C70_SWAP_ALT_REV) /* A */
               blend = blend_alpha = V_028714_SPI_SHADER_32_AR;
            else
               assert(0);
         } else if (format == V_028C70_COLOR_16_16) {
            if (swap == V_028C70_SWAP_STD) { /* RG */
               blend = V_028714_SPI_SHADER_32_GR;
               blend_alpha = V_028714_SPI_SHADER_32_ABGR;
            } else if (swap == V_028C70_SWAP_ALT) /* RA */
               blend = blend_alpha = V_028714_SPI_SHADER_32_AR;
            else
               assert(0);
         } else /* 16_16_16_16 */
            blend = blend_alpha = V_028714_SPI_SHADER_32_ABGR;
      } else if (ntype == V_028C70_NUMBER_UINT)
         alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_UINT16_ABGR;
      else if (ntype == V_028C70_NUMBER_SINT)
         alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_SINT16_ABGR;
      else if (ntype == V_028C70_NUMBER_FLOAT)
         alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_FP16_ABGR;
      else
         assert(0);
      break;

   case V_028C70_COLOR_32:
      if (swap == V_028C70_SWAP_STD) { /* R */
         blend = normal = V_028714_SPI_SHADER_32_R;
         alpha = blend_alpha = V_028714_SPI_SHADER_32_AR;
      } else if (swap == V_028C70_SWAP_ALT_REV) /* A */
         alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_32_AR;
      else
         assert(0);
      break;

   case V_028C70_COLOR_32_32:
      if (swap == V_028C70_SWAP_STD) { /* RG */
         blend = normal = V_028714_SPI_SHADER_32_GR;
         alpha = blend_alpha = V_028714_SPI_SHADER_32_ABGR;
      } else if (swap == V_028C70_SWAP_ALT) /* RA */
         alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_32_AR;
      else
         assert(0);
      break;

   case V_028C70_COLOR_32_32_32_32:
   case V_028C70_COLOR_8_24:
   case V_028C70_COLOR_24_8:
   case V_028C70_COLOR_X24_8_32_FLOAT:
      alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_32_ABGR;
      break;

   default:
      assert(0);
      return;
   }

   /* The DB->CB copy needs 32_ABGR. */
   if (is_depth)
      alpha = blend = blend_alpha = normal = V_028714_SPI_SHADER_32_ABGR;

   formats->normal = normal;
   formats->alpha = alpha;
   formats->blend = blend;
   formats->blend_alpha = blend_alpha;
}

void ac_compute_late_alloc(const struct radeon_info *info, bool ngg, bool ngg_culling,
                           bool uses_scratch, unsigned *late_alloc_wave64, unsigned *cu_mask)
{
   *late_alloc_wave64 = 0; /* The limit is per SA. */
   *cu_mask = 0xffff;

   /* CU masking can decrease performance and cause a hang with <= 2 CUs per SA. */
   if (info->min_good_cu_per_sa <= 2)
      return;

   /* If scratch is used with late alloc, the GPU could deadlock if PS uses scratch too. A more
    * complicated computation is needed to enable late alloc with scratch (see PAL).
    */
   if (uses_scratch)
      return;

   /* Late alloc is not used for NGG on Navi14 due to a hw bug. */
   if (ngg && info->family == CHIP_NAVI14)
      return;

   if (info->chip_class >= GFX10) {
      /* For Wave32, the hw will launch twice the number of late alloc waves, so 1 == 2x wave32.
       * These limits are estimated because they are all safe but they vary in performance.
       */
      if (ngg_culling)
         *late_alloc_wave64 = info->min_good_cu_per_sa * 10;
      else
         *late_alloc_wave64 = info->min_good_cu_per_sa * 4;

      /* Limit LATE_ALLOC_GS to prevent a hang (hw bug) on gfx10. */
      if (info->chip_class == GFX10 && ngg)
         *late_alloc_wave64 = MIN2(*late_alloc_wave64, 64);

      /* Gfx10: CU2 & CU3 must be disabled to prevent a hw deadlock.
       * Others: CU1 must be disabled to prevent a hw deadlock.
       *
       * The deadlock is caused by late alloc, which usually increases performance.
       */
      *cu_mask &= info->chip_class == GFX10 ? ~BITFIELD_RANGE(2, 2) :
                                              ~BITFIELD_RANGE(1, 1);
   } else {
      if (info->min_good_cu_per_sa <= 4) {
         /* Too few available compute units per SA. Disallowing VS to run on one CU could hurt us
          * more than late VS allocation would help.
          *
          * 2 is the highest safe number that allows us to keep all CUs enabled.
          */
         *late_alloc_wave64 = 2;
      } else {
         /* This is a good initial value, allowing 1 late_alloc wave per SIMD on num_cu - 2.
          */
         *late_alloc_wave64 = (info->min_good_cu_per_sa - 2) * 4;
      }

      /* VS can't execute on one CU if the limit is > 2. */
      if (*late_alloc_wave64 > 2)
         *cu_mask = 0xfffe; /* 1 CU disabled */
   }

   /* Max number that fits into the register field. */
   if (ngg) /* GS */
      *late_alloc_wave64 = MIN2(*late_alloc_wave64, G_00B204_SPI_SHADER_LATE_ALLOC_GS_GFX10(~0u));
   else /* VS */
      *late_alloc_wave64 = MIN2(*late_alloc_wave64, G_00B11C_LIMIT(~0u));
}

unsigned ac_compute_cs_workgroup_size(uint16_t sizes[3], bool variable, unsigned max)
{
   if (variable)
      return max;

   return sizes[0] * sizes[1] * sizes[2];
}

unsigned ac_compute_lshs_workgroup_size(enum chip_class chip_class, gl_shader_stage stage,
                                        unsigned tess_num_patches,
                                        unsigned tess_patch_in_vtx,
                                        unsigned tess_patch_out_vtx)
{
   /* When tessellation is used, API VS runs on HW LS, API TCS runs on HW HS.
    * These two HW stages are merged on GFX9+.
    */

   bool merged_shaders = chip_class >= GFX9;
   unsigned ls_workgroup_size = tess_num_patches * tess_patch_in_vtx;
   unsigned hs_workgroup_size = tess_num_patches * tess_patch_out_vtx;

   if (merged_shaders)
      return MAX2(ls_workgroup_size, hs_workgroup_size);
   else if (stage == MESA_SHADER_VERTEX)
      return ls_workgroup_size;
   else if (stage == MESA_SHADER_TESS_CTRL)
      return hs_workgroup_size;
   else
      unreachable("invalid LSHS shader stage");
}

unsigned ac_compute_esgs_workgroup_size(enum chip_class chip_class, unsigned wave_size,
                                        unsigned es_verts, unsigned gs_inst_prims)
{
   /* ESGS may operate in workgroups if on-chip GS (LDS rings) are enabled.
    *
    * GFX6: Not possible in the HW.
    * GFX7-8 (unmerged): possible in the HW, but not implemented in Mesa.
    * GFX9+ (merged): implemented in Mesa.
    */

   if (chip_class <= GFX8)
      return wave_size;

   unsigned workgroup_size = MAX2(es_verts, gs_inst_prims);
   return CLAMP(workgroup_size, 1, 256);
}

unsigned ac_compute_ngg_workgroup_size(unsigned es_verts, unsigned gs_inst_prims,
                                       unsigned max_vtx_out, unsigned prim_amp_factor)
{
   /* NGG always operates in workgroups.
    *
    * For API VS/TES/GS:
    * - 1 invocation per input vertex
    * - 1 invocation per input primitive
    *
    * The same invocation can process both an input vertex and primitive,
    * however 1 invocation can only output up to 1 vertex and 1 primitive.
    */

   unsigned max_vtx_in = es_verts < 256 ? es_verts : 3 * gs_inst_prims;
   unsigned max_prim_in = gs_inst_prims;
   unsigned max_prim_out = gs_inst_prims * prim_amp_factor;
   unsigned workgroup_size = MAX4(max_vtx_in, max_vtx_out, max_prim_in, max_prim_out);

   return CLAMP(workgroup_size, 1, 256);
}
