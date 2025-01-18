/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 * Copyright 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_gpu_info.h"
#include "ac_formats.h"
#include "ac_gpu_info.h"

#include "sid.h"

uint32_t
ac_translate_buffer_numformat(const struct util_format_description *desc,
                              int first_non_void)
{
   if (desc->format == PIPE_FORMAT_R11G11B10_FLOAT)
      return V_008F0C_BUF_NUM_FORMAT_FLOAT;

   assert(first_non_void >= 0);

   switch (desc->channel[first_non_void].type) {
   case UTIL_FORMAT_TYPE_SIGNED:
   case UTIL_FORMAT_TYPE_FIXED:
      if (desc->channel[first_non_void].size >= 32 || desc->channel[first_non_void].pure_integer)
         return V_008F0C_BUF_NUM_FORMAT_SINT;
      else if (desc->channel[first_non_void].normalized)
         return V_008F0C_BUF_NUM_FORMAT_SNORM;
      else
         return V_008F0C_BUF_NUM_FORMAT_SSCALED;
      break;
   case UTIL_FORMAT_TYPE_UNSIGNED:
      if (desc->channel[first_non_void].size >= 32 || desc->channel[first_non_void].pure_integer)
         return V_008F0C_BUF_NUM_FORMAT_UINT;
      else if (desc->channel[first_non_void].normalized)
         return V_008F0C_BUF_NUM_FORMAT_UNORM;
      else
         return V_008F0C_BUF_NUM_FORMAT_USCALED;
      break;
   case UTIL_FORMAT_TYPE_FLOAT:
   default:
      return V_008F0C_BUF_NUM_FORMAT_FLOAT;
   }
}

uint32_t
ac_translate_buffer_dataformat(const struct util_format_description *desc,
                               int first_non_void)
{
   int i;

   if (desc->format == PIPE_FORMAT_R11G11B10_FLOAT)
      return V_008F0C_BUF_DATA_FORMAT_10_11_11;

   assert(first_non_void >= 0);

   if (desc->nr_channels == 4 && desc->channel[0].size == 10 && desc->channel[1].size == 10 &&
       desc->channel[2].size == 10 && desc->channel[3].size == 2)
      return V_008F0C_BUF_DATA_FORMAT_2_10_10_10;

   /* See whether the components are of the same size. */
   for (i = 0; i < desc->nr_channels; i++) {
      if (desc->channel[first_non_void].size != desc->channel[i].size)
         return V_008F0C_BUF_DATA_FORMAT_INVALID;
   }

   switch (desc->channel[first_non_void].size) {
   case 8:
      switch (desc->nr_channels) {
      case 1:
      case 3: /* 3 loads */
         return V_008F0C_BUF_DATA_FORMAT_8;
      case 2:
         return V_008F0C_BUF_DATA_FORMAT_8_8;
      case 4:
         return V_008F0C_BUF_DATA_FORMAT_8_8_8_8;
      }
      break;
   case 16:
      switch (desc->nr_channels) {
      case 1:
      case 3: /* 3 loads */
         return V_008F0C_BUF_DATA_FORMAT_16;
      case 2:
         return V_008F0C_BUF_DATA_FORMAT_16_16;
      case 4:
         return V_008F0C_BUF_DATA_FORMAT_16_16_16_16;
      }
      break;
   case 32:
      switch (desc->nr_channels) {
      case 1:
         return V_008F0C_BUF_DATA_FORMAT_32;
      case 2:
         return V_008F0C_BUF_DATA_FORMAT_32_32;
      case 3:
         return V_008F0C_BUF_DATA_FORMAT_32_32_32;
      case 4:
         return V_008F0C_BUF_DATA_FORMAT_32_32_32_32;
      }
      break;
   case 64:
      /* Legacy double formats. */
      switch (desc->nr_channels) {
      case 1: /* 1 load */
         return V_008F0C_BUF_DATA_FORMAT_32_32;
      case 2: /* 1 load */
         return V_008F0C_BUF_DATA_FORMAT_32_32_32_32;
      case 3: /* 3 loads */
         return V_008F0C_BUF_DATA_FORMAT_32_32;
      case 4: /* 2 loads */
         return V_008F0C_BUF_DATA_FORMAT_32_32_32_32;
      }
      break;
   }

   return V_008F0C_BUF_DATA_FORMAT_INVALID;
}

uint32_t
ac_translate_tex_numformat(const struct util_format_description *desc,
                           int first_non_void)
{
   uint32_t num_format;

   switch (desc->format) {
   case PIPE_FORMAT_S8_UINT_Z24_UNORM:
      num_format = V_008F14_IMG_NUM_FORMAT_UNORM;
      break;
   default:
      if (first_non_void < 0) {
         if (util_format_is_compressed(desc->format)) {
            switch (desc->format) {
            case PIPE_FORMAT_DXT1_SRGB:
            case PIPE_FORMAT_DXT1_SRGBA:
            case PIPE_FORMAT_DXT3_SRGBA:
            case PIPE_FORMAT_DXT5_SRGBA:
            case PIPE_FORMAT_BPTC_SRGBA:
            case PIPE_FORMAT_ETC2_SRGB8:
            case PIPE_FORMAT_ETC2_SRGB8A1:
            case PIPE_FORMAT_ETC2_SRGBA8:
               num_format = V_008F14_IMG_NUM_FORMAT_SRGB;
               break;
            case PIPE_FORMAT_RGTC1_SNORM:
            case PIPE_FORMAT_LATC1_SNORM:
            case PIPE_FORMAT_RGTC2_SNORM:
            case PIPE_FORMAT_LATC2_SNORM:
            case PIPE_FORMAT_ETC2_R11_SNORM:
            case PIPE_FORMAT_ETC2_RG11_SNORM:
            /* implies float, so use SNORM/UNORM to determine
               whether data is signed or not */
            case PIPE_FORMAT_BPTC_RGB_FLOAT:
               num_format = V_008F14_IMG_NUM_FORMAT_SNORM;
               break;
            default:
               num_format = V_008F14_IMG_NUM_FORMAT_UNORM;
               break;
            }
         } else if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
            num_format = V_008F14_IMG_NUM_FORMAT_UNORM;
         } else {
            num_format = V_008F14_IMG_NUM_FORMAT_FLOAT;
         }
      } else if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB) {
         num_format = V_008F14_IMG_NUM_FORMAT_SRGB;
      } else {
         switch (desc->channel[first_non_void].type) {
         case UTIL_FORMAT_TYPE_FLOAT:
            num_format = V_008F14_IMG_NUM_FORMAT_FLOAT;
            break;
         case UTIL_FORMAT_TYPE_SIGNED:
            if (desc->channel[first_non_void].normalized)
               num_format = V_008F14_IMG_NUM_FORMAT_SNORM;
            else if (desc->channel[first_non_void].pure_integer)
               num_format = V_008F14_IMG_NUM_FORMAT_SINT;
            else
               num_format = V_008F14_IMG_NUM_FORMAT_SSCALED;
            break;
         case UTIL_FORMAT_TYPE_UNSIGNED:
            if (desc->channel[first_non_void].normalized)
               num_format = V_008F14_IMG_NUM_FORMAT_UNORM;
            else if (desc->channel[first_non_void].pure_integer)
               num_format = V_008F14_IMG_NUM_FORMAT_UINT;
            else
               num_format = V_008F14_IMG_NUM_FORMAT_USCALED;
            break;
         default:
            num_format = V_008F14_IMG_NUM_FORMAT_UNORM;
            break;
         }
      }
   }

   return num_format;
}

uint32_t
ac_translate_tex_dataformat(const struct radeon_info *info,
                            const struct util_format_description *desc,
                            int first_non_void)
{
   bool uniform = true;
   int i;

   /* Colorspace (return non-RGB formats directly). */
   switch (desc->colorspace) {
   /* Depth stencil formats */
   case UTIL_FORMAT_COLORSPACE_ZS:
      switch (desc->format) {
      case PIPE_FORMAT_Z16_UNORM:
         return V_008F14_IMG_DATA_FORMAT_16;
      case PIPE_FORMAT_X24S8_UINT:
      case PIPE_FORMAT_S8X24_UINT:
         /*
          * Implemented as an 8_8_8_8 data format to fix texture
          * gathers in stencil sampling. This affects at least
          * GL45-CTS.texture_cube_map_array.sampling on GFX8.
          */
         if (info->gfx_level <= GFX8)
            return V_008F14_IMG_DATA_FORMAT_8_8_8_8;

         if (desc->format == PIPE_FORMAT_X24S8_UINT)
            return V_008F14_IMG_DATA_FORMAT_8_24;
         else
            return V_008F14_IMG_DATA_FORMAT_24_8;
      case PIPE_FORMAT_Z24X8_UNORM:
      case PIPE_FORMAT_Z24_UNORM_S8_UINT:
         return V_008F14_IMG_DATA_FORMAT_8_24;
      case PIPE_FORMAT_X8Z24_UNORM:
      case PIPE_FORMAT_S8_UINT_Z24_UNORM:
         return V_008F14_IMG_DATA_FORMAT_24_8;
      case PIPE_FORMAT_S8_UINT:
         return V_008F14_IMG_DATA_FORMAT_8;
      case PIPE_FORMAT_Z32_FLOAT:
         return V_008F14_IMG_DATA_FORMAT_32;
      case PIPE_FORMAT_X32_S8X24_UINT:
      case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
         return V_008F14_IMG_DATA_FORMAT_X24_8_32;
      default:
         return ~0;
      }

   case UTIL_FORMAT_COLORSPACE_YUV:
      return ~0; /* TODO */

   default:
      break;
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_RGTC) {
      switch (desc->format) {
      case PIPE_FORMAT_RGTC1_SNORM:
      case PIPE_FORMAT_LATC1_SNORM:
      case PIPE_FORMAT_RGTC1_UNORM:
      case PIPE_FORMAT_LATC1_UNORM:
         return V_008F14_IMG_DATA_FORMAT_BC4;
      case PIPE_FORMAT_RGTC2_SNORM:
      case PIPE_FORMAT_LATC2_SNORM:
      case PIPE_FORMAT_RGTC2_UNORM:
      case PIPE_FORMAT_LATC2_UNORM:
         return V_008F14_IMG_DATA_FORMAT_BC5;
      default:
         return ~0;
      }
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_ETC) {
      switch (desc->format) {
      case PIPE_FORMAT_ETC1_RGB8:
      case PIPE_FORMAT_ETC2_RGB8:
      case PIPE_FORMAT_ETC2_SRGB8:
         return V_008F14_IMG_DATA_FORMAT_ETC2_RGB;
      case PIPE_FORMAT_ETC2_RGB8A1:
      case PIPE_FORMAT_ETC2_SRGB8A1:
         return V_008F14_IMG_DATA_FORMAT_ETC2_RGBA1;
      case PIPE_FORMAT_ETC2_RGBA8:
      case PIPE_FORMAT_ETC2_SRGBA8:
         return V_008F14_IMG_DATA_FORMAT_ETC2_RGBA;
      case PIPE_FORMAT_ETC2_R11_UNORM:
      case PIPE_FORMAT_ETC2_R11_SNORM:
         return V_008F14_IMG_DATA_FORMAT_ETC2_R;
      case PIPE_FORMAT_ETC2_RG11_UNORM:
      case PIPE_FORMAT_ETC2_RG11_SNORM:
         return V_008F14_IMG_DATA_FORMAT_ETC2_RG;
      default:
         break;
      }
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_BPTC) {
      switch (desc->format) {
      case PIPE_FORMAT_BPTC_RGBA_UNORM:
      case PIPE_FORMAT_BPTC_SRGBA:
         return V_008F14_IMG_DATA_FORMAT_BC7;
      case PIPE_FORMAT_BPTC_RGB_FLOAT:
      case PIPE_FORMAT_BPTC_RGB_UFLOAT:
         return V_008F14_IMG_DATA_FORMAT_BC6;
      default:
         return ~0;
      }
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
      switch (desc->format) {
      case PIPE_FORMAT_R8G8_B8G8_UNORM:
      case PIPE_FORMAT_G8R8_B8R8_UNORM:
      case PIPE_FORMAT_B8G8_R8G8_UNORM:
         return V_008F14_IMG_DATA_FORMAT_GB_GR;
      case PIPE_FORMAT_G8R8_G8B8_UNORM:
      case PIPE_FORMAT_R8G8_R8B8_UNORM:
      case PIPE_FORMAT_G8B8_G8R8_UNORM:
         return V_008F14_IMG_DATA_FORMAT_BG_RG;
      default:
         return ~0;
      }
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_S3TC) {
      switch (desc->format) {
      case PIPE_FORMAT_DXT1_RGB:
      case PIPE_FORMAT_DXT1_RGBA:
      case PIPE_FORMAT_DXT1_SRGB:
      case PIPE_FORMAT_DXT1_SRGBA:
         return V_008F14_IMG_DATA_FORMAT_BC1;
      case PIPE_FORMAT_DXT3_RGBA:
      case PIPE_FORMAT_DXT3_SRGBA:
         return V_008F14_IMG_DATA_FORMAT_BC2;
      case PIPE_FORMAT_DXT5_RGBA:
      case PIPE_FORMAT_DXT5_SRGBA:
         return V_008F14_IMG_DATA_FORMAT_BC3;
      default:
         return ~0;
      }
   }

   if (desc->format == PIPE_FORMAT_R9G9B9E5_FLOAT) {
      return V_008F14_IMG_DATA_FORMAT_5_9_9_9;
   } else if (desc->format == PIPE_FORMAT_R11G11B10_FLOAT) {
      return V_008F14_IMG_DATA_FORMAT_10_11_11;
   }

   /* hw cannot support mixed formats (except depth/stencil, since only
    * depth is read).*/
   if (desc->is_mixed && desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
      return ~0;

   if (first_non_void < 0 || first_non_void > 3)
      return ~0;

   /* See whether the components are of the same size. */
   for (i = 1; i < desc->nr_channels; i++) {
      uniform = uniform && desc->channel[0].size == desc->channel[i].size;
   }

   /* Non-uniform formats. */
   if (!uniform) {
      switch (desc->nr_channels) {
      case 3:
         if (desc->channel[0].size == 5 && desc->channel[1].size == 6 &&
             desc->channel[2].size == 5) {
            return V_008F14_IMG_DATA_FORMAT_5_6_5;
         }
         return ~0;
      case 4:
         /* 5551 and 1555 UINT formats fail on Gfx8/Carrizo´. */
         if (info->family == CHIP_CARRIZO &&
             desc->channel[1].size == 5 &&
             desc->channel[2].size == 5 &&
             desc->channel[first_non_void].type == UTIL_FORMAT_TYPE_UNSIGNED &&
             desc->channel[first_non_void].pure_integer)
            return ~0;

         if (desc->channel[0].size == 5 && desc->channel[1].size == 5 &&
             desc->channel[2].size == 5 && desc->channel[3].size == 1) {
            return V_008F14_IMG_DATA_FORMAT_1_5_5_5;
         }
         if (desc->channel[0].size == 1 && desc->channel[1].size == 5 &&
             desc->channel[2].size == 5 && desc->channel[3].size == 5) {
            return V_008F14_IMG_DATA_FORMAT_5_5_5_1;
         }
         if (desc->channel[0].size == 10 && desc->channel[1].size == 10 &&
             desc->channel[2].size == 10 && desc->channel[3].size == 2) {
            return V_008F14_IMG_DATA_FORMAT_2_10_10_10;
         }
         return ~0;
      }
      return ~0;
   }

   /* uniform formats */
   switch (desc->channel[first_non_void].size) {
   case 4:
      switch (desc->nr_channels) {
      case 4:
         /* 4444 UINT formats fail on Gfx8/Carrizo´. */
         if (info->family == CHIP_CARRIZO &&
             desc->channel[first_non_void].type == UTIL_FORMAT_TYPE_UNSIGNED &&
             desc->channel[first_non_void].pure_integer)
            return ~0;

         return V_008F14_IMG_DATA_FORMAT_4_4_4_4;
      }
      break;
   case 8:
      switch (desc->nr_channels) {
      case 1:
         return V_008F14_IMG_DATA_FORMAT_8;
      case 2:
         return V_008F14_IMG_DATA_FORMAT_8_8;
      case 4:
         return V_008F14_IMG_DATA_FORMAT_8_8_8_8;
      }
      break;
   case 16:
      switch (desc->nr_channels) {
      case 1:
         return V_008F14_IMG_DATA_FORMAT_16;
      case 2:
         return V_008F14_IMG_DATA_FORMAT_16_16;
      case 4:
         return V_008F14_IMG_DATA_FORMAT_16_16_16_16;
      }
      break;
   case 32:
      switch (desc->nr_channels) {
      case 1:
         return V_008F14_IMG_DATA_FORMAT_32;
      case 2:
         return V_008F14_IMG_DATA_FORMAT_32_32;
      /* Not supported for render targets */
      case 3:
         return V_008F14_IMG_DATA_FORMAT_32_32_32;
      case 4:
         return V_008F14_IMG_DATA_FORMAT_32_32_32_32;
      }
      break;
   case 64:
      if (desc->channel[0].type != UTIL_FORMAT_TYPE_FLOAT && desc->nr_channels == 1)
         return V_008F14_IMG_DATA_FORMAT_32_32;
      break;
   }

   return ~0;
}

unsigned
ac_get_cb_format(enum amd_gfx_level gfx_level, enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

#define HAS_SIZE(x, y, z, w)                                                                       \
   (desc->channel[0].size == (x) && desc->channel[1].size == (y) &&                                \
    desc->channel[2].size == (z) && desc->channel[3].size == (w))

   if (format == PIPE_FORMAT_R11G11B10_FLOAT) /* isn't plain */
      return V_028C70_COLOR_10_11_11;

   if (gfx_level >= GFX10_3 &&
       format == PIPE_FORMAT_R9G9B9E5_FLOAT) /* isn't plain */
      return V_028C70_COLOR_5_9_9_9;

   if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
      return V_028C70_COLOR_INVALID;

   /* hw cannot support mixed formats (except depth/stencil, since
    * stencil is not written to). */
   if (desc->is_mixed && desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
      return V_028C70_COLOR_INVALID;

   int first_non_void = util_format_get_first_non_void_channel(format);

   /* Reject SCALED formats because we don't implement them for CB. */
   if (first_non_void >= 0 && first_non_void <= 3 &&
       (desc->channel[first_non_void].type == UTIL_FORMAT_TYPE_UNSIGNED ||
        desc->channel[first_non_void].type == UTIL_FORMAT_TYPE_SIGNED) &&
       !desc->channel[first_non_void].normalized &&
       !desc->channel[first_non_void].pure_integer)
      return V_028C70_COLOR_INVALID;

   switch (desc->nr_channels) {
   case 1:
      switch (desc->channel[0].size) {
      case 8:
         return V_028C70_COLOR_8;
      case 16:
         return V_028C70_COLOR_16;
      case 32:
         return V_028C70_COLOR_32;
      case 64:
         return V_028C70_COLOR_32_32;
      }
      break;
   case 2:
      if (desc->channel[0].size == desc->channel[1].size) {
         switch (desc->channel[0].size) {
         case 8:
            return V_028C70_COLOR_8_8;
         case 16:
            return V_028C70_COLOR_16_16;
         case 32:
            return V_028C70_COLOR_32_32;
         }
      } else if (HAS_SIZE(8, 24, 0, 0)) {
         return V_028C70_COLOR_24_8;
      } else if (HAS_SIZE(24, 8, 0, 0)) {
         return V_028C70_COLOR_8_24;
      }
      break;
   case 3:
      if (HAS_SIZE(5, 6, 5, 0)) {
         return V_028C70_COLOR_5_6_5;
      } else if (HAS_SIZE(32, 8, 24, 0)) {
         return V_028C70_COLOR_X24_8_32_FLOAT;
      }
      break;
   case 4:
      if (desc->channel[0].size == desc->channel[1].size &&
          desc->channel[0].size == desc->channel[2].size &&
          desc->channel[0].size == desc->channel[3].size) {
         switch (desc->channel[0].size) {
         case 4:
            return V_028C70_COLOR_4_4_4_4;
         case 8:
            return V_028C70_COLOR_8_8_8_8;
         case 16:
            return V_028C70_COLOR_16_16_16_16;
         case 32:
            return V_028C70_COLOR_32_32_32_32;
         }
      } else if (HAS_SIZE(5, 5, 5, 1)) {
         return V_028C70_COLOR_1_5_5_5;
      } else if (HAS_SIZE(1, 5, 5, 5)) {
         return V_028C70_COLOR_5_5_5_1;
      } else if (HAS_SIZE(10, 10, 10, 2)) {
         return V_028C70_COLOR_2_10_10_10;
      } else if (HAS_SIZE(2, 10, 10, 10)) {
         return V_028C70_COLOR_10_10_10_2;
      }
      break;
   }
   return V_028C70_COLOR_INVALID;
}

unsigned ac_get_cb_number_type(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   int chan = util_format_get_first_non_void_channel(format);

   if (chan == -1 || desc->channel[chan].type == UTIL_FORMAT_TYPE_FLOAT) {
      return V_028C70_NUMBER_FLOAT;
   } else {
      if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB) {
         return V_028C70_NUMBER_SRGB;
      } else if (desc->channel[chan].type == UTIL_FORMAT_TYPE_SIGNED) {
         return desc->channel[chan].pure_integer ? V_028C70_NUMBER_SINT : V_028C70_NUMBER_SNORM;
      } else if (desc->channel[chan].type == UTIL_FORMAT_TYPE_UNSIGNED) {
         return desc->channel[chan].pure_integer ? V_028C70_NUMBER_UINT : V_028C70_NUMBER_UNORM;
      } else {
         return V_028C70_NUMBER_UNORM;
      }
   }
}

unsigned
ac_translate_colorswap(enum amd_gfx_level gfx_level, enum pipe_format format, bool do_endian_swap)
{
   const struct util_format_description *desc = util_format_description(format);

#define HAS_SWIZZLE(chan, swz) (desc->swizzle[chan] == PIPE_SWIZZLE_##swz)

   if (format == PIPE_FORMAT_R11G11B10_FLOAT) /* isn't plain */
      return V_028C70_SWAP_STD;

   if (gfx_level >= GFX10_3 &&
       format == PIPE_FORMAT_R9G9B9E5_FLOAT) /* isn't plain */
      return V_028C70_SWAP_STD;

   if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
      return ~0U;

   switch (desc->nr_channels) {
   case 1:
      if (HAS_SWIZZLE(0, X))
         return V_028C70_SWAP_STD; /* X___ */
      else if (HAS_SWIZZLE(3, X))
         return V_028C70_SWAP_ALT_REV; /* ___X */
      break;
   case 2:
      if ((HAS_SWIZZLE(0, X) && HAS_SWIZZLE(1, Y)) || (HAS_SWIZZLE(0, X) && HAS_SWIZZLE(1, NONE)) ||
          (HAS_SWIZZLE(0, NONE) && HAS_SWIZZLE(1, Y)))
         return V_028C70_SWAP_STD; /* XY__ */
      else if ((HAS_SWIZZLE(0, Y) && HAS_SWIZZLE(1, X)) ||
               (HAS_SWIZZLE(0, Y) && HAS_SWIZZLE(1, NONE)) ||
               (HAS_SWIZZLE(0, NONE) && HAS_SWIZZLE(1, X)))
         /* YX__ */
         return (do_endian_swap ? V_028C70_SWAP_STD : V_028C70_SWAP_STD_REV);
      else if (HAS_SWIZZLE(0, X) && HAS_SWIZZLE(3, Y))
         return V_028C70_SWAP_ALT; /* X__Y */
      else if (HAS_SWIZZLE(0, Y) && HAS_SWIZZLE(3, X))
         return V_028C70_SWAP_ALT_REV; /* Y__X */
      break;
   case 3:
      if (HAS_SWIZZLE(0, X))
         return (do_endian_swap ? V_028C70_SWAP_STD_REV : V_028C70_SWAP_STD);
      else if (HAS_SWIZZLE(0, Z))
         return V_028C70_SWAP_STD_REV; /* ZYX */
      break;
   case 4:
      /* check the middle channels, the 1st and 4th channel can be NONE */
      if (HAS_SWIZZLE(1, Y) && HAS_SWIZZLE(2, Z)) {
         return V_028C70_SWAP_STD; /* XYZW */
      } else if (HAS_SWIZZLE(1, Z) && HAS_SWIZZLE(2, Y)) {
         return V_028C70_SWAP_STD_REV; /* WZYX */
      } else if (HAS_SWIZZLE(1, Y) && HAS_SWIZZLE(2, X)) {
         return V_028C70_SWAP_ALT; /* ZYXW */
      } else if (HAS_SWIZZLE(1, Z) && HAS_SWIZZLE(2, W)) {
         /* YZWX */
         if (desc->is_array)
            return V_028C70_SWAP_ALT_REV;
         else
            return (do_endian_swap ? V_028C70_SWAP_ALT : V_028C70_SWAP_ALT_REV);
      }
      break;
   }
   return ~0U;
}

bool
ac_is_colorbuffer_format_supported(enum amd_gfx_level gfx_level, enum pipe_format format)
{
   return ac_get_cb_format(gfx_level, format) != V_028C70_COLOR_INVALID &&
          ac_translate_colorswap(gfx_level, format, false) != ~0U;
}

uint32_t
ac_colorformat_endian_swap(uint32_t colorformat)
{
   if (UTIL_ARCH_BIG_ENDIAN) {
      switch (colorformat) {
      /* 8-bit buffers. */
      case V_028C70_COLOR_8:
         return V_028C70_ENDIAN_NONE;

      /* 16-bit buffers. */
      case V_028C70_COLOR_5_6_5:
      case V_028C70_COLOR_1_5_5_5:
      case V_028C70_COLOR_4_4_4_4:
      case V_028C70_COLOR_16:
      case V_028C70_COLOR_8_8:
         return V_028C70_ENDIAN_8IN16;

      /* 32-bit buffers. */
      case V_028C70_COLOR_8_8_8_8:
      case V_028C70_COLOR_2_10_10_10:
      case V_028C70_COLOR_10_10_10_2:
      case V_028C70_COLOR_8_24:
      case V_028C70_COLOR_24_8:
      case V_028C70_COLOR_16_16:
         return V_028C70_ENDIAN_8IN32;

      /* 64-bit buffers. */
      case V_028C70_COLOR_16_16_16_16:
         return V_028C70_ENDIAN_8IN16;

      case V_028C70_COLOR_32_32:
         return V_028C70_ENDIAN_8IN32;

      /* 128-bit buffers. */
      case V_028C70_COLOR_32_32_32_32:
         return V_028C70_ENDIAN_8IN32;
      default:
         return V_028C70_ENDIAN_NONE; /* Unsupported. */
      }
   } else {
      return V_028C70_ENDIAN_NONE;
   }
}

uint32_t
ac_translate_dbformat(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_Z16_UNORM:
   case PIPE_FORMAT_Z16_UNORM_S8_UINT:
      return V_028040_Z_16;
   case PIPE_FORMAT_S8_UINT_Z24_UNORM:
   case PIPE_FORMAT_X8Z24_UNORM:
   case PIPE_FORMAT_Z24X8_UNORM:
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      return V_028040_Z_24; /* not present on GFX12 */
   case PIPE_FORMAT_Z32_FLOAT:
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      return V_028040_Z_32_FLOAT;
   default:
      return V_028040_Z_INVALID;
   }
}

bool
ac_is_zs_format_supported(enum pipe_format format)
{
   return ac_translate_dbformat(format) != V_028040_Z_INVALID;
}

uint32_t
ac_border_color_swizzle(const struct util_format_description *desc)
{
   unsigned bc_swizzle = V_008F20_BC_SWIZZLE_XYZW;

   if (desc->format == PIPE_FORMAT_S8_UINT) {
      /* Swizzle of 8-bit stencil format is defined as _x__ but the hw expects XYZW. */
      assert(desc->swizzle[1] == PIPE_SWIZZLE_X);
      return bc_swizzle;
   }

   if (desc->swizzle[3] == PIPE_SWIZZLE_X) {
      /* For the pre-defined border color values (white, opaque
       * black, transparent black), the only thing that matters is
       * that the alpha channel winds up in the correct place
       * (because the RGB channels are all the same) so either of
       * these enumerations will work.
       */
      if (desc->swizzle[2] == PIPE_SWIZZLE_Y)
         bc_swizzle = V_008F20_BC_SWIZZLE_WZYX;
      else
         bc_swizzle = V_008F20_BC_SWIZZLE_WXYZ;
   } else if (desc->swizzle[0] == PIPE_SWIZZLE_X) {
      if (desc->swizzle[1] == PIPE_SWIZZLE_Y)
         bc_swizzle = V_008F20_BC_SWIZZLE_XYZW;
      else
         bc_swizzle = V_008F20_BC_SWIZZLE_XWYZ;
   } else if (desc->swizzle[1] == PIPE_SWIZZLE_X) {
      bc_swizzle = V_008F20_BC_SWIZZLE_YXWZ;
   } else if (desc->swizzle[2] == PIPE_SWIZZLE_X) {
      bc_swizzle = V_008F20_BC_SWIZZLE_ZYXW;
   }

   return bc_swizzle;
}

/** Linearize and convert luminance/intensity to red. */
enum pipe_format
ac_simplify_cb_format(enum pipe_format format)
{
   format = util_format_linear(format);
   format = util_format_luminance_to_red(format);
   return util_format_intensity_to_red(format);
}

bool
ac_alpha_is_on_msb(const struct radeon_info *info, enum pipe_format format)
{
   if (info->gfx_level >= GFX11)
      return false;

   format = ac_simplify_cb_format(format);
   const struct util_format_description *desc = util_format_description(format);
   unsigned comp_swap = ac_translate_colorswap(info->gfx_level, format, false);

   /* The following code matches the hw behavior. */
   if (desc->nr_channels == 1) {
      return (comp_swap == V_028C70_SWAP_ALT_REV) != (info->family == CHIP_RAVEN2 ||
                                                      info->family == CHIP_RENOIR);
   }

   return comp_swap != V_028C70_SWAP_STD_REV && comp_swap != V_028C70_SWAP_ALT_REV;
}

/* GFX6-8:
 * - no integer format support
 * - no depth format support (depth formats without shadow samplers are supported,
 *   but that's not enough)
 * - only single-channel formats are supported
 * - limitations of early chips (GFX6 only): no R9G9B9E5 support
 *
 * GFX9+:
 * - all formats are supported
 */
bool
ac_is_reduction_mode_supported(const struct radeon_info *info, enum pipe_format format,
                               bool shadow_samplers)
{
   const struct util_format_description *desc = util_format_description(format);

   if (info->gfx_level <= GFX8) {
      /* old HW limitations */
      if (info->gfx_level == GFX6 && format == PIPE_FORMAT_R9G9B9E5_FLOAT)
         return false;

      /* reject if more than one channel */
      if (desc->nr_channels > 1)
         return false;

      /* no integer or depth format support */
      if (util_format_is_pure_integer(format) ||
          (shadow_samplers && util_format_has_depth(desc)))
         return false;
   }

   return true;
}
