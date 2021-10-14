/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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

#include "radv_debug.h"
#include "radv_private.h"

#include "sid.h"
#include "vk_format.h"

#include "vk_util.h"

#include "ac_drm_fourcc.h"
#include "util/format_r11g11b10f.h"
#include "util/format_rgb9e5.h"
#include "util/format_srgb.h"
#include "util/half_float.h"
#include "vulkan/util/vk_format.h"
#include "vulkan/util/vk_enum_defines.h"

uint32_t
radv_translate_buffer_dataformat(const struct util_format_description *desc, int first_non_void)
{
   unsigned type;
   int i;

   assert(util_format_get_num_planes(desc->format) == 1);

   if (desc->format == PIPE_FORMAT_R11G11B10_FLOAT)
      return V_008F0C_BUF_DATA_FORMAT_10_11_11;

   if (first_non_void < 0)
      return V_008F0C_BUF_DATA_FORMAT_INVALID;
   type = desc->channel[first_non_void].type;

   if (type == UTIL_FORMAT_TYPE_FIXED)
      return V_008F0C_BUF_DATA_FORMAT_INVALID;
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
         return V_008F0C_BUF_DATA_FORMAT_16;
      case 2:
         return V_008F0C_BUF_DATA_FORMAT_16_16;
      case 4:
         return V_008F0C_BUF_DATA_FORMAT_16_16_16_16;
      }
      break;
   case 32:
      /* From the Southern Islands ISA documentation about MTBUF:
       * 'Memory reads of data in memory that is 32 or 64 bits do not
       * undergo any format conversion.'
       */
      if (type != UTIL_FORMAT_TYPE_FLOAT && !desc->channel[first_non_void].pure_integer)
         return V_008F0C_BUF_DATA_FORMAT_INVALID;

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
      if (type != UTIL_FORMAT_TYPE_FLOAT && desc->nr_channels == 1)
         return V_008F0C_BUF_DATA_FORMAT_32_32;
   }

   return V_008F0C_BUF_DATA_FORMAT_INVALID;
}

uint32_t
radv_translate_buffer_numformat(const struct util_format_description *desc, int first_non_void)
{
   assert(util_format_get_num_planes(desc->format) == 1);

   if (desc->format == PIPE_FORMAT_R11G11B10_FLOAT)
      return V_008F0C_BUF_NUM_FORMAT_FLOAT;

   if (first_non_void < 0)
      return ~0;

   switch (desc->channel[first_non_void].type) {
   case UTIL_FORMAT_TYPE_SIGNED:
      if (desc->channel[first_non_void].normalized)
         return V_008F0C_BUF_NUM_FORMAT_SNORM;
      else if (desc->channel[first_non_void].pure_integer)
         return V_008F0C_BUF_NUM_FORMAT_SINT;
      else
         return V_008F0C_BUF_NUM_FORMAT_SSCALED;
      break;
   case UTIL_FORMAT_TYPE_UNSIGNED:
      if (desc->channel[first_non_void].normalized)
         return V_008F0C_BUF_NUM_FORMAT_UNORM;
      else if (desc->channel[first_non_void].pure_integer)
         return V_008F0C_BUF_NUM_FORMAT_UINT;
      else
         return V_008F0C_BUF_NUM_FORMAT_USCALED;
      break;
   case UTIL_FORMAT_TYPE_FLOAT:
   default:
      return V_008F0C_BUF_NUM_FORMAT_FLOAT;
   }
}

void
radv_translate_vertex_format(const struct radv_physical_device *pdevice, VkFormat format,
                             const struct util_format_description *desc, unsigned *dfmt,
                             unsigned *nfmt, bool *post_shuffle,
                             enum radv_vs_input_alpha_adjust *alpha_adjust)
{
   assert(desc->channel[0].type != UTIL_FORMAT_TYPE_VOID);
   *nfmt = radv_translate_buffer_numformat(desc, 0);
   *dfmt = radv_translate_buffer_dataformat(desc, 0);

   *alpha_adjust = ALPHA_ADJUST_NONE;
   if (pdevice->rad_info.chip_class <= GFX8 && pdevice->rad_info.family != CHIP_STONEY) {
      switch (format) {
      case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
      case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
         *alpha_adjust = ALPHA_ADJUST_SNORM;
         break;
      case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
      case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
         *alpha_adjust = ALPHA_ADJUST_SSCALED;
         break;
      case VK_FORMAT_A2R10G10B10_SINT_PACK32:
      case VK_FORMAT_A2B10G10R10_SINT_PACK32:
         *alpha_adjust = ALPHA_ADJUST_SINT;
         break;
      default:
         break;
      }
   }

   switch (format) {
   case VK_FORMAT_B8G8R8A8_UNORM:
   case VK_FORMAT_B8G8R8A8_SNORM:
   case VK_FORMAT_B8G8R8A8_USCALED:
   case VK_FORMAT_B8G8R8A8_SSCALED:
   case VK_FORMAT_B8G8R8A8_UINT:
   case VK_FORMAT_B8G8R8A8_SINT:
   case VK_FORMAT_B8G8R8A8_SRGB:
   case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
   case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
   case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
   case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
   case VK_FORMAT_A2R10G10B10_UINT_PACK32:
   case VK_FORMAT_A2R10G10B10_SINT_PACK32:
      *post_shuffle = true;
      break;
   default:
      *post_shuffle = false;
      break;
   }
}

uint32_t
radv_translate_tex_dataformat(VkFormat format, const struct util_format_description *desc,
                              int first_non_void)
{
   bool uniform = true;
   int i;

   assert(vk_format_get_plane_count(format) == 1);

   if (!desc)
      return ~0;
   /* Colorspace (return non-RGB formats directly). */
   switch (desc->colorspace) {
      /* Depth stencil formats */
   case UTIL_FORMAT_COLORSPACE_ZS:
      switch (format) {
      case VK_FORMAT_D16_UNORM:
         return V_008F14_IMG_DATA_FORMAT_16;
      case VK_FORMAT_D24_UNORM_S8_UINT:
      case VK_FORMAT_X8_D24_UNORM_PACK32:
         return V_008F14_IMG_DATA_FORMAT_8_24;
      case VK_FORMAT_S8_UINT:
         return V_008F14_IMG_DATA_FORMAT_8;
      case VK_FORMAT_D32_SFLOAT:
         return V_008F14_IMG_DATA_FORMAT_32;
      case VK_FORMAT_D32_SFLOAT_S8_UINT:
         return V_008F14_IMG_DATA_FORMAT_X24_8_32;
      default:
         goto out_unknown;
      }

   case UTIL_FORMAT_COLORSPACE_YUV:
      goto out_unknown; /* TODO */

   default:
      break;
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
      switch (format) {
      /* Don't ask me why this looks inverted. PAL does the same. */
      case VK_FORMAT_G8B8G8R8_422_UNORM:
         return V_008F14_IMG_DATA_FORMAT_BG_RG;
      case VK_FORMAT_B8G8R8G8_422_UNORM:
         return V_008F14_IMG_DATA_FORMAT_GB_GR;
      default:
         goto out_unknown;
      }
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_RGTC) {
      switch (format) {
      case VK_FORMAT_BC4_UNORM_BLOCK:
      case VK_FORMAT_BC4_SNORM_BLOCK:
         return V_008F14_IMG_DATA_FORMAT_BC4;
      case VK_FORMAT_BC5_UNORM_BLOCK:
      case VK_FORMAT_BC5_SNORM_BLOCK:
         return V_008F14_IMG_DATA_FORMAT_BC5;
      default:
         break;
      }
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_S3TC) {
      switch (format) {
      case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
      case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
      case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
      case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
         return V_008F14_IMG_DATA_FORMAT_BC1;
      case VK_FORMAT_BC2_UNORM_BLOCK:
      case VK_FORMAT_BC2_SRGB_BLOCK:
         return V_008F14_IMG_DATA_FORMAT_BC2;
      case VK_FORMAT_BC3_UNORM_BLOCK:
      case VK_FORMAT_BC3_SRGB_BLOCK:
         return V_008F14_IMG_DATA_FORMAT_BC3;
      default:
         break;
      }
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_BPTC) {
      switch (format) {
      case VK_FORMAT_BC6H_UFLOAT_BLOCK:
      case VK_FORMAT_BC6H_SFLOAT_BLOCK:
         return V_008F14_IMG_DATA_FORMAT_BC6;
      case VK_FORMAT_BC7_UNORM_BLOCK:
      case VK_FORMAT_BC7_SRGB_BLOCK:
         return V_008F14_IMG_DATA_FORMAT_BC7;
      default:
         break;
      }
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_ETC) {
      switch (format) {
      case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
      case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
         return V_008F14_IMG_DATA_FORMAT_ETC2_RGB;
      case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
      case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
         return V_008F14_IMG_DATA_FORMAT_ETC2_RGBA1;
      case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
      case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
         return V_008F14_IMG_DATA_FORMAT_ETC2_RGBA;
      case VK_FORMAT_EAC_R11_UNORM_BLOCK:
      case VK_FORMAT_EAC_R11_SNORM_BLOCK:
         return V_008F14_IMG_DATA_FORMAT_ETC2_R;
      case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
      case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
         return V_008F14_IMG_DATA_FORMAT_ETC2_RG;
      default:
         break;
      }
   }

   if (format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32) {
      return V_008F14_IMG_DATA_FORMAT_5_9_9_9;
   } else if (format == VK_FORMAT_B10G11R11_UFLOAT_PACK32) {
      return V_008F14_IMG_DATA_FORMAT_10_11_11;
   }

   /* R8G8Bx_SNORM - TODO CxV8U8 */

   /* hw cannot support mixed formats (except depth/stencil, since only
    * depth is read).*/
   if (desc->is_mixed && desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
      goto out_unknown;

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
         goto out_unknown;
      case 4:
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
            /* Closed VK driver does this also no 2/10/10/10 snorm */
            if (desc->channel[0].type == UTIL_FORMAT_TYPE_SIGNED && desc->channel[0].normalized)
               goto out_unknown;
            return V_008F14_IMG_DATA_FORMAT_2_10_10_10;
         }
         goto out_unknown;
      }
      goto out_unknown;
   }

   if (first_non_void < 0 || first_non_void > 3)
      goto out_unknown;

   /* uniform formats */
   switch (desc->channel[first_non_void].size) {
   case 4:
      switch (desc->nr_channels) {
#if 0 /* Not supported for render targets */
		case 2:
			return V_008F14_IMG_DATA_FORMAT_4_4;
#endif
      case 4:
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

out_unknown:
   /* R600_ERR("Unable to handle texformat %d %s\n", format, vk_format_name(format)); */
   return ~0;
}

uint32_t
radv_translate_tex_numformat(VkFormat format, const struct util_format_description *desc,
                             int first_non_void)
{
   assert(vk_format_get_plane_count(format) == 1);

   switch (format) {
   case VK_FORMAT_D24_UNORM_S8_UINT:
      return V_008F14_IMG_NUM_FORMAT_UNORM;
   default:
      if (first_non_void < 0) {
         if (vk_format_is_compressed(format)) {
            switch (format) {
            case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
            case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            case VK_FORMAT_BC2_SRGB_BLOCK:
            case VK_FORMAT_BC3_SRGB_BLOCK:
            case VK_FORMAT_BC7_SRGB_BLOCK:
            case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
            case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
            case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
               return V_008F14_IMG_NUM_FORMAT_SRGB;
            case VK_FORMAT_BC4_SNORM_BLOCK:
            case VK_FORMAT_BC5_SNORM_BLOCK:
            case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            case VK_FORMAT_EAC_R11_SNORM_BLOCK:
            case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
               return V_008F14_IMG_NUM_FORMAT_SNORM;
            default:
               return V_008F14_IMG_NUM_FORMAT_UNORM;
            }
         } else if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
            return V_008F14_IMG_NUM_FORMAT_UNORM;
         } else {
            return V_008F14_IMG_NUM_FORMAT_FLOAT;
         }
      } else if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB) {
         return V_008F14_IMG_NUM_FORMAT_SRGB;
      } else {
         switch (desc->channel[first_non_void].type) {
         case UTIL_FORMAT_TYPE_FLOAT:
            return V_008F14_IMG_NUM_FORMAT_FLOAT;
         case UTIL_FORMAT_TYPE_SIGNED:
            if (desc->channel[first_non_void].normalized)
               return V_008F14_IMG_NUM_FORMAT_SNORM;
            else if (desc->channel[first_non_void].pure_integer)
               return V_008F14_IMG_NUM_FORMAT_SINT;
            else
               return V_008F14_IMG_NUM_FORMAT_SSCALED;
         case UTIL_FORMAT_TYPE_UNSIGNED:
            if (desc->channel[first_non_void].normalized)
               return V_008F14_IMG_NUM_FORMAT_UNORM;
            else if (desc->channel[first_non_void].pure_integer)
               return V_008F14_IMG_NUM_FORMAT_UINT;
            else
               return V_008F14_IMG_NUM_FORMAT_USCALED;
         default:
            return V_008F14_IMG_NUM_FORMAT_UNORM;
         }
      }
   }
}

uint32_t
radv_translate_color_numformat(VkFormat format, const struct util_format_description *desc,
                               int first_non_void)
{
   unsigned ntype;

   assert(vk_format_get_plane_count(format) == 1);

   if (first_non_void == -1 || desc->channel[first_non_void].type == UTIL_FORMAT_TYPE_FLOAT)
      ntype = V_028C70_NUMBER_FLOAT;
   else {
      ntype = V_028C70_NUMBER_UNORM;
      if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB)
         ntype = V_028C70_NUMBER_SRGB;
      else if (desc->channel[first_non_void].type == UTIL_FORMAT_TYPE_SIGNED) {
         if (desc->channel[first_non_void].pure_integer) {
            ntype = V_028C70_NUMBER_SINT;
         } else if (desc->channel[first_non_void].normalized) {
            ntype = V_028C70_NUMBER_SNORM;
         } else
            ntype = ~0u;
      } else if (desc->channel[first_non_void].type == UTIL_FORMAT_TYPE_UNSIGNED) {
         if (desc->channel[first_non_void].pure_integer) {
            ntype = V_028C70_NUMBER_UINT;
         } else if (desc->channel[first_non_void].normalized) {
            ntype = V_028C70_NUMBER_UNORM;
         } else
            ntype = ~0u;
      }
   }
   return ntype;
}

static bool
radv_is_sampler_format_supported(VkFormat format, bool *linear_sampling)
{
   const struct util_format_description *desc = vk_format_description(format);
   uint32_t num_format;
   if (!desc || format == VK_FORMAT_UNDEFINED || format == VK_FORMAT_R64_UINT ||
       format == VK_FORMAT_R64_SINT)
      return false;
   num_format =
      radv_translate_tex_numformat(format, desc, vk_format_get_first_non_void_channel(format));

   if (num_format == V_008F14_IMG_NUM_FORMAT_USCALED ||
       num_format == V_008F14_IMG_NUM_FORMAT_SSCALED)
      return false;

   if (num_format == V_008F14_IMG_NUM_FORMAT_UNORM || num_format == V_008F14_IMG_NUM_FORMAT_SNORM ||
       num_format == V_008F14_IMG_NUM_FORMAT_FLOAT || num_format == V_008F14_IMG_NUM_FORMAT_SRGB)
      *linear_sampling = true;
   else
      *linear_sampling = false;
   return radv_translate_tex_dataformat(format, vk_format_description(format),
                                        vk_format_get_first_non_void_channel(format)) != ~0U;
}

bool
radv_is_atomic_format_supported(VkFormat format)
{
   return format == VK_FORMAT_R32_UINT || format == VK_FORMAT_R32_SINT ||
          format == VK_FORMAT_R32_SFLOAT || format == VK_FORMAT_R64_UINT ||
          format == VK_FORMAT_R64_SINT;
}

bool
radv_is_storage_image_format_supported(struct radv_physical_device *physical_device,
                                       VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);
   unsigned data_format, num_format;
   if (!desc || format == VK_FORMAT_UNDEFINED)
      return false;

   data_format =
      radv_translate_tex_dataformat(format, desc, vk_format_get_first_non_void_channel(format));
   num_format =
      radv_translate_tex_numformat(format, desc, vk_format_get_first_non_void_channel(format));

   if (data_format == ~0 || num_format == ~0)
      return false;

   /* Extracted from the GCN3 ISA document. */
   switch (num_format) {
   case V_008F14_IMG_NUM_FORMAT_UNORM:
   case V_008F14_IMG_NUM_FORMAT_SNORM:
   case V_008F14_IMG_NUM_FORMAT_UINT:
   case V_008F14_IMG_NUM_FORMAT_SINT:
   case V_008F14_IMG_NUM_FORMAT_FLOAT:
      break;
   default:
      return false;
   }

   switch (data_format) {
   case V_008F14_IMG_DATA_FORMAT_8:
   case V_008F14_IMG_DATA_FORMAT_16:
   case V_008F14_IMG_DATA_FORMAT_8_8:
   case V_008F14_IMG_DATA_FORMAT_32:
   case V_008F14_IMG_DATA_FORMAT_16_16:
   case V_008F14_IMG_DATA_FORMAT_10_11_11:
   case V_008F14_IMG_DATA_FORMAT_11_11_10:
   case V_008F14_IMG_DATA_FORMAT_10_10_10_2:
   case V_008F14_IMG_DATA_FORMAT_2_10_10_10:
   case V_008F14_IMG_DATA_FORMAT_8_8_8_8:
   case V_008F14_IMG_DATA_FORMAT_32_32:
   case V_008F14_IMG_DATA_FORMAT_16_16_16_16:
   case V_008F14_IMG_DATA_FORMAT_32_32_32_32:
   case V_008F14_IMG_DATA_FORMAT_5_6_5:
   case V_008F14_IMG_DATA_FORMAT_1_5_5_5:
   case V_008F14_IMG_DATA_FORMAT_5_5_5_1:
   case V_008F14_IMG_DATA_FORMAT_4_4_4_4:
      /* TODO: FMASK formats. */
      return true;
   case V_008F14_IMG_DATA_FORMAT_5_9_9_9:
      return physical_device->rad_info.chip_class >= GFX10_3;
   default:
      return false;
   }
}

bool
radv_is_buffer_format_supported(VkFormat format, bool *scaled)
{
   const struct util_format_description *desc = vk_format_description(format);
   unsigned data_format, num_format;
   if (!desc || format == VK_FORMAT_UNDEFINED)
      return false;

   data_format =
      radv_translate_buffer_dataformat(desc, vk_format_get_first_non_void_channel(format));
   num_format = radv_translate_buffer_numformat(desc, vk_format_get_first_non_void_channel(format));

   if (scaled)
      *scaled = (num_format == V_008F0C_BUF_NUM_FORMAT_SSCALED) ||
                (num_format == V_008F0C_BUF_NUM_FORMAT_USCALED);
   return data_format != V_008F0C_BUF_DATA_FORMAT_INVALID && num_format != ~0;
}

bool
radv_is_colorbuffer_format_supported(const struct radv_physical_device *pdevice, VkFormat format,
                                     bool *blendable)
{
   const struct util_format_description *desc = vk_format_description(format);
   uint32_t color_format = radv_translate_colorformat(format);
   uint32_t color_swap = radv_translate_colorswap(format, false);
   uint32_t color_num_format =
      radv_translate_color_numformat(format, desc, vk_format_get_first_non_void_channel(format));

   if (color_num_format == V_028C70_NUMBER_UINT || color_num_format == V_028C70_NUMBER_SINT ||
       color_format == V_028C70_COLOR_8_24 || color_format == V_028C70_COLOR_24_8 ||
       color_format == V_028C70_COLOR_X24_8_32_FLOAT) {
      *blendable = false;
   } else
      *blendable = true;

   if (format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 && pdevice->rad_info.chip_class < GFX10_3)
      return false;

   return color_format != V_028C70_COLOR_INVALID && color_swap != ~0U && color_num_format != ~0;
}

static bool
radv_is_zs_format_supported(VkFormat format)
{
   return radv_translate_dbformat(format) != V_028040_Z_INVALID || format == VK_FORMAT_S8_UINT;
}

static bool
radv_is_filter_minmax_format_supported(VkFormat format)
{
   /* From the Vulkan spec 1.1.71:
    *
    * "The following formats must support the
    *  VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_MINMAX_BIT_KHR feature with
    *  VK_IMAGE_TILING_OPTIMAL, if they support
    *  VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT_KHR."
    */
   /* TODO: enable more formats. */
   switch (format) {
   case VK_FORMAT_R8_UNORM:
   case VK_FORMAT_R8_SNORM:
   case VK_FORMAT_R16_UNORM:
   case VK_FORMAT_R16_SNORM:
   case VK_FORMAT_R16_SFLOAT:
   case VK_FORMAT_R32_SFLOAT:
   case VK_FORMAT_D16_UNORM:
   case VK_FORMAT_X8_D24_UNORM_PACK32:
   case VK_FORMAT_D32_SFLOAT:
   case VK_FORMAT_D16_UNORM_S8_UINT:
   case VK_FORMAT_D24_UNORM_S8_UINT:
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return true;
   default:
      return false;
   }
}

bool
radv_device_supports_etc(struct radv_physical_device *physical_device)
{
   return physical_device->rad_info.family == CHIP_VEGA10 ||
          physical_device->rad_info.family == CHIP_RAVEN ||
          physical_device->rad_info.family == CHIP_RAVEN2 ||
          physical_device->rad_info.family == CHIP_STONEY;
}

static void
radv_physical_device_get_format_properties(struct radv_physical_device *physical_device,
                                           VkFormat format, VkFormatProperties3KHR *out_properties)
{
   VkFormatFeatureFlags2KHR linear = 0, tiled = 0, buffer = 0;
   const struct util_format_description *desc = vk_format_description(format);
   bool blendable;
   bool scaled = false;
   /* TODO: implement some software emulation of SUBSAMPLED formats. */
   if (!desc || vk_format_to_pipe_format(format) == PIPE_FORMAT_NONE ||
       desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
      out_properties->linearTilingFeatures = linear;
      out_properties->optimalTilingFeatures = tiled;
      out_properties->bufferFeatures = buffer;
      return;
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_ETC && !radv_device_supports_etc(physical_device)) {
      out_properties->linearTilingFeatures = linear;
      out_properties->optimalTilingFeatures = tiled;
      out_properties->bufferFeatures = buffer;
      return;
   }

   if (vk_format_get_plane_count(format) > 1 || desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
      uint64_t tiling = VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT_KHR |
                        VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT_KHR |
                        VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT_KHR |
                        VK_FORMAT_FEATURE_2_COSITED_CHROMA_SAMPLES_BIT_KHR |
                        VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT_KHR;

      /* The subsampled formats have no support for linear filters. */
      if (desc->layout != UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
         tiling |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT_KHR;
      }

      /* Fails for unknown reasons with linear tiling & subsampled formats. */
      out_properties->linearTilingFeatures =
         desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED ? 0 : tiling;
      out_properties->optimalTilingFeatures = tiling;
      out_properties->bufferFeatures = 0;
      return;
   }

   if (radv_is_storage_image_format_supported(physical_device, format)) {
      tiled |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT_KHR |
               VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT_KHR |
               VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT_KHR;
      linear |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT_KHR |
                VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT_KHR |
                VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT_KHR;
   }

   if (radv_is_buffer_format_supported(format, &scaled)) {
      if (format != VK_FORMAT_R64_UINT && format != VK_FORMAT_R64_SINT) {
         buffer |= VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT_KHR;
         if (!scaled)
            buffer |= VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT_KHR;
      }
      buffer |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT_KHR;
   }

   if (vk_format_is_depth_or_stencil(format)) {
      if (radv_is_zs_format_supported(format)) {
         tiled |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT_KHR;
         tiled |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT_KHR;
         tiled |= VK_FORMAT_FEATURE_2_BLIT_SRC_BIT_KHR | VK_FORMAT_FEATURE_2_BLIT_DST_BIT_KHR;
         tiled |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT_KHR | VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT_KHR;

         if (radv_is_filter_minmax_format_supported(format))
            tiled |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_MINMAX_BIT_KHR;

         if (vk_format_has_depth(format)) {
            tiled |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT_KHR |
                     VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT_KHR;
         }

         /* Don't support blitting surfaces with depth/stencil. */
         if (vk_format_has_depth(format) && vk_format_has_stencil(format))
            tiled &= ~VK_FORMAT_FEATURE_2_BLIT_DST_BIT_KHR;

         /* Don't support linear depth surfaces */
         linear = 0;
      }
   } else {
      bool linear_sampling;
      if (radv_is_sampler_format_supported(format, &linear_sampling)) {
         linear |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT_KHR | VK_FORMAT_FEATURE_2_BLIT_SRC_BIT_KHR;
         tiled |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT_KHR | VK_FORMAT_FEATURE_2_BLIT_SRC_BIT_KHR;

         if (radv_is_filter_minmax_format_supported(format))
            tiled |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_MINMAX_BIT_KHR;

         if (linear_sampling) {
            linear |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT_KHR;
            tiled |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT_KHR;
         }

         /* Don't support blitting for R32G32B32 formats. */
         if (format == VK_FORMAT_R32G32B32_SFLOAT || format == VK_FORMAT_R32G32B32_UINT ||
             format == VK_FORMAT_R32G32B32_SINT) {
            linear &= ~VK_FORMAT_FEATURE_2_BLIT_SRC_BIT_KHR;
         }
      }
      if (radv_is_colorbuffer_format_supported(physical_device, format, &blendable)) {
         linear |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT_KHR | VK_FORMAT_FEATURE_2_BLIT_DST_BIT_KHR;
         tiled |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT_KHR | VK_FORMAT_FEATURE_2_BLIT_DST_BIT_KHR;
         if (blendable) {
            linear |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT_KHR;
            tiled |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT_KHR;
         }
      }
      if (tiled && !scaled) {
         tiled |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT_KHR | VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT_KHR;
      }

      /* Tiled formatting does not support NPOT pixel sizes */
      if (!util_is_power_of_two_or_zero(vk_format_get_blocksize(format)))
         tiled = 0;
   }

   if (linear && !scaled) {
      linear |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT_KHR | VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT_KHR;
   }

   if (radv_is_atomic_format_supported(format)) {
      buffer |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_ATOMIC_BIT_KHR;
      linear |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT_KHR;
      tiled |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT_KHR;
   }

   switch (format) {
   case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
   case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
   case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
   case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
   case VK_FORMAT_A2R10G10B10_SINT_PACK32:
   case VK_FORMAT_A2B10G10R10_SINT_PACK32:
      buffer &=
         ~(VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT_KHR | VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT_KHR);
      linear = 0;
      tiled = 0;
      break;
   default:
      break;
   }

   switch (format) {
   case VK_FORMAT_R32G32_SFLOAT:
   case VK_FORMAT_R32G32B32_SFLOAT:
   case VK_FORMAT_R32G32B32A32_SFLOAT:
   case VK_FORMAT_R16G16_SFLOAT:
   case VK_FORMAT_R16G16B16_SFLOAT:
   case VK_FORMAT_R16G16B16A16_SFLOAT:
   case VK_FORMAT_R16G16_SNORM:
   case VK_FORMAT_R16G16B16A16_SNORM:
   case VK_FORMAT_R16G16B16A16_UNORM:
      buffer |= VK_FORMAT_FEATURE_2_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR;
      break;
   default:
      break;
   }
   /* addrlib does not support linear compressed textures. */
   if (vk_format_is_compressed(format))
      linear = 0;

   /* From the Vulkan spec 1.2.163:
    *
    * "VK_FORMAT_FEATURE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR must be supported for the
    *  following formats if the attachmentFragmentShadingRate feature is supported:"
    *
    * - VK_FORMAT_R8_UINT
    */
   if (format == VK_FORMAT_R8_UINT) {
      tiled |= VK_FORMAT_FEATURE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
   }

   out_properties->linearTilingFeatures = linear;
   out_properties->optimalTilingFeatures = tiled;
   out_properties->bufferFeatures = buffer;
}

uint32_t
radv_translate_colorformat(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);

#define HAS_SIZE(x, y, z, w)                                                                       \
   (desc->channel[0].size == (x) && desc->channel[1].size == (y) &&                                \
    desc->channel[2].size == (z) && desc->channel[3].size == (w))

   if (format == VK_FORMAT_B10G11R11_UFLOAT_PACK32) /* isn't plain */
      return V_028C70_COLOR_10_11_11;

   if (format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)
      return V_028C70_COLOR_5_9_9_9;

   if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
      return V_028C70_COLOR_INVALID;

   /* hw cannot support mixed formats (except depth/stencil, since
    * stencil is not written to). */
   if (desc->is_mixed && desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
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
      }
      break;
   }
   return V_028C70_COLOR_INVALID;
}

uint32_t
radv_colorformat_endian_swap(uint32_t colorformat)
{
   if (0 /*SI_BIG_ENDIAN*/) {
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
radv_translate_dbformat(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_D16_UNORM:
   case VK_FORMAT_D16_UNORM_S8_UINT:
      return V_028040_Z_16;
   case VK_FORMAT_D32_SFLOAT:
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return V_028040_Z_32_FLOAT;
   default:
      return V_028040_Z_INVALID;
   }
}

unsigned
radv_translate_colorswap(VkFormat format, bool do_endian_swap)
{
   const struct util_format_description *desc = vk_format_description(format);

#define HAS_SWIZZLE(chan, swz) (desc->swizzle[chan] == PIPE_SWIZZLE_##swz)

   if (format == VK_FORMAT_B10G11R11_UFLOAT_PACK32)
      return V_028C70_SWAP_STD;

   if (format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)
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
radv_format_pack_clear_color(VkFormat format, uint32_t clear_vals[2], VkClearColorValue *value)
{
   const struct util_format_description *desc = vk_format_description(format);

   if (format == VK_FORMAT_B10G11R11_UFLOAT_PACK32) {
      clear_vals[0] = float3_to_r11g11b10f(value->float32);
      clear_vals[1] = 0;
      return true;
   } else if (format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32) {
      clear_vals[0] = float3_to_rgb9e5(value->float32);
      clear_vals[1] = 0;
      return true;
   }

   if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN) {
      fprintf(stderr, "failed to fast clear for non-plain format %d\n", format);
      return false;
   }

   if (!util_is_power_of_two_or_zero(desc->block.bits)) {
      fprintf(stderr, "failed to fast clear for NPOT format %d\n", format);
      return false;
   }

   if (desc->block.bits > 64) {
      /*
       * We have a 128 bits format, check if the first 3 components are the same.
       * Every elements has to be 32 bits since we don't support 64-bit formats,
       * and we can skip swizzling checks as alpha always comes last for these and
       * we do not care about the rest as they have to be the same.
       */
      if (desc->channel[0].type == UTIL_FORMAT_TYPE_FLOAT) {
         if (value->float32[0] != value->float32[1] || value->float32[0] != value->float32[2])
            return false;
      } else {
         if (value->uint32[0] != value->uint32[1] || value->uint32[0] != value->uint32[2])
            return false;
      }
      clear_vals[0] = value->uint32[0];
      clear_vals[1] = value->uint32[3];
      return true;
   }
   uint64_t clear_val = 0;

   for (unsigned c = 0; c < 4; ++c) {
      if (desc->swizzle[c] >= 4)
         continue;

      const struct util_format_channel_description *channel = &desc->channel[desc->swizzle[c]];
      assert(channel->size);

      uint64_t v = 0;
      if (channel->pure_integer) {
         v = value->uint32[c] & ((1ULL << channel->size) - 1);
      } else if (channel->normalized) {
         if (channel->type == UTIL_FORMAT_TYPE_UNSIGNED && desc->swizzle[c] < 3 &&
             desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB) {
            assert(channel->size == 8);

            v = util_format_linear_float_to_srgb_8unorm(value->float32[c]);
         } else {
            float f = MIN2(value->float32[c], 1.0f);

            if (channel->type == UTIL_FORMAT_TYPE_UNSIGNED) {
               f = MAX2(f, 0.0f) * ((1ULL << channel->size) - 1);
            } else {
               f = MAX2(f, -1.0f) * ((1ULL << (channel->size - 1)) - 1);
            }

            /* The hardware rounds before conversion. */
            if (f > 0)
               f += 0.5f;
            else
               f -= 0.5f;

            v = (uint64_t)f;
         }
      } else if (channel->type == UTIL_FORMAT_TYPE_FLOAT) {
         if (channel->size == 32) {
            memcpy(&v, &value->float32[c], 4);
         } else if (channel->size == 16) {
            v = _mesa_float_to_float16_rtz(value->float32[c]);
         } else {
            fprintf(stderr, "failed to fast clear for unhandled float size in format %d\n", format);
            return false;
         }
      } else {
         fprintf(stderr, "failed to fast clear for unhandled component type in format %d\n",
                 format);
         return false;
      }
      clear_val |= (v & ((1ULL << channel->size) - 1)) << channel->shift;
   }

   clear_vals[0] = clear_val;
   clear_vals[1] = clear_val >> 32;

   return true;
}

static const struct ac_modifier_options radv_modifier_options = {
   .dcc = true,
   .dcc_retile = true,
};

static VkFormatFeatureFlags2KHR
radv_get_modifier_flags(struct radv_physical_device *dev, VkFormat format, uint64_t modifier,
                        const VkFormatProperties3KHR *props)
{
   VkFormatFeatureFlags2KHR features;

   if (vk_format_is_compressed(format) || vk_format_is_depth_or_stencil(format))
      return 0;

   if (modifier == DRM_FORMAT_MOD_LINEAR)
      features = props->linearTilingFeatures;
   else
      features = props->optimalTilingFeatures;

   if (ac_modifier_has_dcc(modifier)) {
      /* Only disable support for STORAGE_IMAGE on modifiers that
       * do not support DCC image stores.
       */
      if (!ac_modifier_supports_dcc_image_stores(modifier) || radv_is_atomic_format_supported(format))
         features &= ~VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT_KHR;

      if (dev->instance->debug_flags & (RADV_DEBUG_NO_DCC | RADV_DEBUG_NO_DISPLAY_DCC))
         return 0;
   }

   return features;
}

static VkFormatFeatureFlags
features2_to_features(VkFormatFeatureFlags2KHR features2)
{
   return features2 & VK_ALL_FORMAT_FEATURE_FLAG_BITS;
}

static void
radv_list_drm_format_modifiers(struct radv_physical_device *dev, VkFormat format,
                               const VkFormatProperties3KHR *format_props,
                               VkDrmFormatModifierPropertiesListEXT *mod_list)
{
   unsigned mod_count;

   if (!mod_list)
      return;

   if (vk_format_is_compressed(format) || vk_format_is_depth_or_stencil(format)) {
      mod_list->drmFormatModifierCount = 0;
      return;
   }

   ac_get_supported_modifiers(&dev->rad_info, &radv_modifier_options,
                              vk_format_to_pipe_format(format), &mod_count, NULL);
   if (!mod_list->pDrmFormatModifierProperties) {
      mod_list->drmFormatModifierCount = mod_count;
      return;
   }

   mod_count = MIN2(mod_count, mod_list->drmFormatModifierCount);

   uint64_t *mods = malloc(mod_count * sizeof(uint64_t));
   if (!mods) {
      /* We can't return an error here ... */
      mod_list->drmFormatModifierCount = 0;
      return;
   }
   ac_get_supported_modifiers(&dev->rad_info, &radv_modifier_options,
                              vk_format_to_pipe_format(format), &mod_count, mods);

   mod_list->drmFormatModifierCount = 0;
   for (unsigned i = 0; i < mod_count; ++i) {
      VkFormatFeatureFlags2KHR features =
         radv_get_modifier_flags(dev, format, mods[i], format_props);
      unsigned planes = vk_format_get_plane_count(format);
      if (planes == 1) {
         if (ac_modifier_has_dcc_retile(mods[i]))
            planes = 3;
         else if (ac_modifier_has_dcc(mods[i]))
            planes = 2;
      }

      if (!features)
         continue;

      mod_list->pDrmFormatModifierProperties[mod_list->drmFormatModifierCount].drmFormatModifier =
         mods[i];
      mod_list->pDrmFormatModifierProperties[mod_list->drmFormatModifierCount]
         .drmFormatModifierPlaneCount = planes;
      mod_list->pDrmFormatModifierProperties[mod_list->drmFormatModifierCount]
         .drmFormatModifierTilingFeatures = features2_to_features(features);

      ++mod_list->drmFormatModifierCount;
   }

   free(mods);
}

static void
radv_list_drm_format_modifiers_2(struct radv_physical_device *dev, VkFormat format,
                                 const VkFormatProperties3KHR *format_props,
                                 VkDrmFormatModifierPropertiesList2EXT *mod_list)
{
   unsigned mod_count;

   if (!mod_list)
      return;

   if (vk_format_is_compressed(format) || vk_format_is_depth_or_stencil(format)) {
      mod_list->drmFormatModifierCount = 0;
      return;
   }

   ac_get_supported_modifiers(&dev->rad_info, &radv_modifier_options,
                              vk_format_to_pipe_format(format), &mod_count, NULL);
   if (!mod_list->pDrmFormatModifierProperties) {
      mod_list->drmFormatModifierCount = mod_count;
      return;
   }

   mod_count = MIN2(mod_count, mod_list->drmFormatModifierCount);

   uint64_t *mods = malloc(mod_count * sizeof(uint64_t));
   if (!mods) {
      /* We can't return an error here ... */
      mod_list->drmFormatModifierCount = 0;
      return;
   }
   ac_get_supported_modifiers(&dev->rad_info, &radv_modifier_options,
                              vk_format_to_pipe_format(format), &mod_count, mods);

   mod_list->drmFormatModifierCount = 0;
   for (unsigned i = 0; i < mod_count; ++i) {
      VkFormatFeatureFlags2KHR features =
         radv_get_modifier_flags(dev, format, mods[i], format_props);
      unsigned planes = vk_format_get_plane_count(format);
      if (planes == 1) {
         if (ac_modifier_has_dcc_retile(mods[i]))
            planes = 3;
         else if (ac_modifier_has_dcc(mods[i]))
            planes = 2;
      }

      if (!features)
         continue;

      mod_list->pDrmFormatModifierProperties[mod_list->drmFormatModifierCount].drmFormatModifier =
         mods[i];
      mod_list->pDrmFormatModifierProperties[mod_list->drmFormatModifierCount]
         .drmFormatModifierPlaneCount = planes;
      mod_list->pDrmFormatModifierProperties[mod_list->drmFormatModifierCount]
         .drmFormatModifierTilingFeatures = features;

      ++mod_list->drmFormatModifierCount;
   }

   free(mods);
}

static VkResult
radv_check_modifier_support(struct radv_physical_device *dev,
                            const VkPhysicalDeviceImageFormatInfo2 *info,
                            VkImageFormatProperties *props, VkFormat format, uint64_t modifier)
{
   uint32_t max_width, max_height;

   if (info->type != VK_IMAGE_TYPE_2D)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* We did not add modifiers for sparse textures. */
   if (info->flags & (VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT |
                      VK_IMAGE_CREATE_SPARSE_ALIASED_BIT))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /*
    * Need to check the modifier is supported in general:
    * "If the drmFormatModifier is incompatible with the parameters specified
    * in VkPhysicalDeviceImageFormatInfo2 and its pNext chain, then
    * vkGetPhysicalDeviceImageFormatProperties2 returns VK_ERROR_FORMAT_NOT_SUPPORTED.
    * The implementation must support the query of any drmFormatModifier,
    * including unknown and invalid modifier values."
    */
   VkDrmFormatModifierPropertiesListEXT mod_list = {
      .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
   };

   VkFormatProperties2 format_props2 = {.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
                                        .pNext = &mod_list};

   radv_GetPhysicalDeviceFormatProperties2(radv_physical_device_to_handle(dev), format,
                                           &format_props2);

   if (!mod_list.drmFormatModifierCount)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   mod_list.pDrmFormatModifierProperties =
      calloc(mod_list.drmFormatModifierCount, sizeof(*mod_list.pDrmFormatModifierProperties));
   if (!mod_list.pDrmFormatModifierProperties)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   radv_GetPhysicalDeviceFormatProperties2(radv_physical_device_to_handle(dev), format,
                                           &format_props2);

   bool found = false;
   for (uint32_t i = 0; i < mod_list.drmFormatModifierCount && !found; ++i)
      if (mod_list.pDrmFormatModifierProperties[i].drmFormatModifier == modifier)
         found = true;

   free(mod_list.pDrmFormatModifierProperties);

   if (!found)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   bool need_dcc_sign_reinterpret = false;
   if (ac_modifier_has_dcc(modifier) &&
       !radv_are_formats_dcc_compatible(dev, info->pNext, format, info->flags,
                                        &need_dcc_sign_reinterpret) &&
       !need_dcc_sign_reinterpret)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* We can expand this as needed and implemented but there is not much demand
    * for more. */
   if (ac_modifier_has_dcc(modifier)) {
      props->maxMipLevels = 1;
      props->maxArrayLayers = 1;
   }

   ac_modifier_max_extent(&dev->rad_info, modifier, &max_width, &max_height);
   props->maxExtent.width = MIN2(props->maxExtent.width, max_width);
   props->maxExtent.height = MIN2(props->maxExtent.width, max_height);

   /* We don't support MSAA for modifiers */
   props->sampleCounts &= VK_SAMPLE_COUNT_1_BIT;
   return VK_SUCCESS;
}

void
radv_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format,
                                        VkFormatProperties2 *pFormatProperties)
{
   RADV_FROM_HANDLE(radv_physical_device, physical_device, physicalDevice);
   VkFormatProperties3KHR format_props;

   radv_physical_device_get_format_properties(physical_device, format, &format_props);

   pFormatProperties->formatProperties.linearTilingFeatures =
      features2_to_features(format_props.linearTilingFeatures);
   pFormatProperties->formatProperties.optimalTilingFeatures =
      features2_to_features(format_props.optimalTilingFeatures);
   pFormatProperties->formatProperties.bufferFeatures =
      features2_to_features(format_props.bufferFeatures);

   VkFormatProperties3KHR *format_props_extended =
      vk_find_struct(pFormatProperties, FORMAT_PROPERTIES_3_KHR);
   if (format_props_extended) {
      format_props_extended->linearTilingFeatures = format_props.linearTilingFeatures;
      format_props_extended->optimalTilingFeatures = format_props.optimalTilingFeatures;
      format_props_extended->bufferFeatures = format_props.bufferFeatures;
   }

   radv_list_drm_format_modifiers(
      physical_device, format, &format_props,
      vk_find_struct(pFormatProperties, DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT));
   radv_list_drm_format_modifiers_2(
      physical_device, format, &format_props,
      vk_find_struct(pFormatProperties, DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT));
}

static VkResult
radv_get_image_format_properties(struct radv_physical_device *physical_device,
                                 const VkPhysicalDeviceImageFormatInfo2 *info, VkFormat format,
                                 VkImageFormatProperties *pImageFormatProperties)

{
   VkFormatProperties3KHR format_props;
   VkFormatFeatureFlags2KHR format_feature_flags;
   VkExtent3D maxExtent;
   uint32_t maxMipLevels;
   uint32_t maxArraySize;
   VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   const struct util_format_description *desc = vk_format_description(format);
   enum chip_class chip_class = physical_device->rad_info.chip_class;
   VkImageTiling tiling = info->tiling;
   const VkPhysicalDeviceImageDrmFormatModifierInfoEXT *mod_info =
      vk_find_struct_const(info->pNext, PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT);
   VkResult result = VK_ERROR_FORMAT_NOT_SUPPORTED;

   radv_physical_device_get_format_properties(physical_device, format, &format_props);
   if (tiling == VK_IMAGE_TILING_LINEAR) {
      format_feature_flags = format_props.linearTilingFeatures;
   } else if (tiling == VK_IMAGE_TILING_OPTIMAL) {
      format_feature_flags = format_props.optimalTilingFeatures;
   } else if (tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      format_feature_flags = radv_get_modifier_flags(physical_device, format,
                                                     mod_info->drmFormatModifier, &format_props);
   } else {
      unreachable("bad VkImageTiling");
   }

   if (format_feature_flags == 0)
      goto unsupported;

   if (info->type != VK_IMAGE_TYPE_2D && vk_format_is_depth_or_stencil(format))
      goto unsupported;

   switch (info->type) {
   default:
      unreachable("bad vkimage type\n");
   case VK_IMAGE_TYPE_1D:
      maxExtent.width = 16384;
      maxExtent.height = 1;
      maxExtent.depth = 1;
      maxMipLevels = 15; /* log2(maxWidth) + 1 */
      maxArraySize = chip_class >= GFX10 ? 8192 : 2048;
      break;
   case VK_IMAGE_TYPE_2D:
      maxExtent.width = 16384;
      maxExtent.height = 16384;
      maxExtent.depth = 1;
      maxMipLevels = 15; /* log2(maxWidth) + 1 */
      maxArraySize = chip_class >= GFX10 ? 8192 : 2048;
      break;
   case VK_IMAGE_TYPE_3D:
      if (chip_class >= GFX10) {
         maxExtent.width = 8192;
         maxExtent.height = 8192;
         maxExtent.depth = 8192;
      } else {
         maxExtent.width = 2048;
         maxExtent.height = 2048;
         maxExtent.depth = 2048;
      }
      maxMipLevels = util_logbase2(maxExtent.width) + 1;
      maxArraySize = 1;
      break;
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
      /* Might be able to support but the entire format support is
       * messy, so taking the lazy way out. */
      maxArraySize = 1;
   }

   if (tiling == VK_IMAGE_TILING_OPTIMAL && info->type == VK_IMAGE_TYPE_2D &&
       (format_feature_flags & (VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT_KHR |
                                VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT_KHR)) &&
       !(info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       !(info->usage & VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR)) {
      sampleCounts |= VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
   }

   if (tiling == VK_IMAGE_TILING_LINEAR &&
       (format == VK_FORMAT_R32G32B32_SFLOAT || format == VK_FORMAT_R32G32B32_SINT ||
        format == VK_FORMAT_R32G32B32_UINT)) {
      /* R32G32B32 is a weird format and the driver currently only
       * supports the barely minimum.
       * TODO: Implement more if we really need to.
       */
      if (info->type == VK_IMAGE_TYPE_3D)
         goto unsupported;
      maxArraySize = 1;
      maxMipLevels = 1;
   }

   /* We can't create 3d compressed 128bpp images that can be rendered to on GFX9 */
   if (physical_device->rad_info.chip_class >= GFX9 && info->type == VK_IMAGE_TYPE_3D &&
       vk_format_get_blocksizebits(format) == 128 && vk_format_is_compressed(format) &&
       (info->flags & VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT) &&
       ((info->flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT) ||
        (info->usage & VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT_KHR))) {
      goto unsupported;
   }

   if (info->usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT_KHR)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_STORAGE_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT_KHR)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT_KHR)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT_KHR)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT_KHR)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT_KHR)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
      if (!(format_feature_flags & (VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT_KHR |
                                    VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT_KHR))) {
         goto unsupported;
      }
   }

   /* Sparse resources with multi-planar formats are unsupported. */
   if (info->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) {
      if (vk_format_get_plane_count(format) > 1)
         goto unsupported;
   }

   if (info->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT) {
      /* Sparse textures are only supported on GFX8+. */
      if (physical_device->rad_info.chip_class < GFX8)
         goto unsupported;

      if (vk_format_get_plane_count(format) > 1 || info->type != VK_IMAGE_TYPE_2D ||
          info->tiling != VK_IMAGE_TILING_OPTIMAL || vk_format_is_depth_or_stencil(format))
         goto unsupported;
   }

   *pImageFormatProperties = (VkImageFormatProperties){
      .maxExtent = maxExtent,
      .maxMipLevels = maxMipLevels,
      .maxArrayLayers = maxArraySize,
      .sampleCounts = sampleCounts,

      /* FINISHME: Accurately calculate
       * VkImageFormatProperties::maxResourceSize.
       */
      .maxResourceSize = UINT32_MAX,
   };

   if (mod_info) {
      result = radv_check_modifier_support(physical_device, info, pImageFormatProperties, format,
                                           mod_info->drmFormatModifier);
      if (result != VK_SUCCESS)
         goto unsupported;
   }

   return VK_SUCCESS;
unsupported:
   *pImageFormatProperties = (VkImageFormatProperties){
      .maxExtent = {0, 0, 0},
      .maxMipLevels = 0,
      .maxArrayLayers = 0,
      .sampleCounts = 0,
      .maxResourceSize = 0,
   };

   return result;
}

static void
get_external_image_format_properties(struct radv_physical_device *physical_device,
                                     const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
                                     VkExternalMemoryHandleTypeFlagBits handleType,
                                     VkExternalMemoryProperties *external_properties,
                                     VkImageFormatProperties *format_properties)
{
   VkExternalMemoryFeatureFlagBits flags = 0;
   VkExternalMemoryHandleTypeFlags export_flags = 0;
   VkExternalMemoryHandleTypeFlags compat_flags = 0;

   if (pImageFormatInfo->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT)
      return;

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      if (pImageFormatInfo->tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
         break;

      switch (pImageFormatInfo->type) {
      case VK_IMAGE_TYPE_2D:
         flags =
            VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;

         compat_flags = export_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
         break;
      default:
         break;
      }
      break;
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
      switch (pImageFormatInfo->type) {
      case VK_IMAGE_TYPE_2D:
         flags =
            VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
         if (pImageFormatInfo->tiling != VK_IMAGE_TILING_LINEAR)
            flags |= VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT;

         compat_flags = export_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
         break;
      default:
         break;
      }
      break;
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID:
      if (!physical_device->vk.supported_extensions.ANDROID_external_memory_android_hardware_buffer)
         break;

      if (!radv_android_gralloc_supports_format(pImageFormatInfo->format, pImageFormatInfo->usage))
         break;

      if (pImageFormatInfo->type != VK_IMAGE_TYPE_2D)
         break;

      format_properties->maxMipLevels = MIN2(1, format_properties->maxMipLevels);
      format_properties->maxArrayLayers = MIN2(1, format_properties->maxArrayLayers);
      format_properties->sampleCounts &= VK_SAMPLE_COUNT_1_BIT;

      flags = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      if (pImageFormatInfo->tiling != VK_IMAGE_TILING_LINEAR)
         flags |= VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT;

      compat_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
      break;
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT:
      flags = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
      break;
   default:
      break;
   }

   *external_properties = (VkExternalMemoryProperties){
      .externalMemoryFeatures = flags,
      .exportFromImportedHandleTypes = export_flags,
      .compatibleHandleTypes = compat_flags,
   };
}

VkResult
radv_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice,
                                             const VkPhysicalDeviceImageFormatInfo2 *base_info,
                                             VkImageFormatProperties2 *base_props)
{
   RADV_FROM_HANDLE(radv_physical_device, physical_device, physicalDevice);
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   VkExternalImageFormatProperties *external_props = NULL;
   struct VkAndroidHardwareBufferUsageANDROID *android_usage = NULL;
   VkSamplerYcbcrConversionImageFormatProperties *ycbcr_props = NULL;
   VkTextureLODGatherFormatPropertiesAMD *texture_lod_props = NULL;
   VkResult result;
   VkFormat format = radv_select_android_external_format(base_info->pNext, base_info->format);

   result = radv_get_image_format_properties(physical_device, base_info, format,
                                             &base_props->imageFormatProperties);
   if (result != VK_SUCCESS)
      return result;

   /* Extract input structs */
   vk_foreach_struct_const(s, base_info->pNext)
   {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const void *)s;
         break;
      default:
         break;
      }
   }

   /* Extract output structs */
   vk_foreach_struct(s, base_props->pNext)
   {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         external_props = (void *)s;
         break;
      case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES:
         ycbcr_props = (void *)s;
         break;
      case VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID:
         android_usage = (void *)s;
         break;
      case VK_STRUCTURE_TYPE_TEXTURE_LOD_GATHER_FORMAT_PROPERTIES_AMD:
         texture_lod_props = (void *)s;
         break;
      default:
         break;
      }
   }

   bool ahb_supported =
      physical_device->vk.supported_extensions.ANDROID_external_memory_android_hardware_buffer;
   if (android_usage && ahb_supported) {
#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
      android_usage->androidHardwareBufferUsage =
         radv_ahb_usage_from_vk_usage(base_info->flags, base_info->usage);
#endif
   }

   /* From the Vulkan 1.0.97 spec:
    *
    *    If handleType is 0, vkGetPhysicalDeviceImageFormatProperties2 will
    *    behave as if VkPhysicalDeviceExternalImageFormatInfo was not
    *    present and VkExternalImageFormatProperties will be ignored.
    */
   if (external_info && external_info->handleType != 0) {
      get_external_image_format_properties(physical_device, base_info, external_info->handleType,
                                           &external_props->externalMemoryProperties,
                                           &base_props->imageFormatProperties);
      if (!external_props->externalMemoryProperties.externalMemoryFeatures) {
         /* From the Vulkan 1.0.97 spec:
          *
          *    If handleType is not compatible with the [parameters] specified
          *    in VkPhysicalDeviceImageFormatInfo2, then
          *    vkGetPhysicalDeviceImageFormatProperties2 returns
          *    VK_ERROR_FORMAT_NOT_SUPPORTED.
          */
         result = vk_errorf(physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
                            "unsupported VkExternalMemoryTypeFlagBitsKHR 0x%x",
                            external_info->handleType);
         goto fail;
      }
   }

   if (ycbcr_props) {
      ycbcr_props->combinedImageSamplerDescriptorCount = vk_format_get_plane_count(format);
   }

   if (texture_lod_props) {
      if (physical_device->rad_info.chip_class >= GFX9) {
         texture_lod_props->supportsTextureGatherLODBiasAMD = true;
      } else {
         texture_lod_props->supportsTextureGatherLODBiasAMD = !vk_format_is_int(format);
      }
   }

   return VK_SUCCESS;

fail:
   if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
      /* From the Vulkan 1.0.97 spec:
       *
       *    If the combination of parameters to
       *    vkGetPhysicalDeviceImageFormatProperties2 is not supported by
       *    the implementation for use in vkCreateImage, then all members of
       *    imageFormatProperties will be filled with zero.
       */
      base_props->imageFormatProperties = (VkImageFormatProperties){0};
   }

   return result;
}

static void
fill_sparse_image_format_properties(struct radv_physical_device *pdev, VkFormat format,
                                    VkSparseImageFormatProperties *prop)
{
   prop->aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   prop->flags = 0;

   /* On GFX8 we first subdivide by level and then layer, leading to a single
    * miptail. On GFX9+ we first subdivide by layer and then level which results
    * in a miptail per layer. */
   if (pdev->rad_info.chip_class < GFX9)
      prop->flags |= VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT;

   /* This assumes the sparse image tile size is always 64 KiB (1 << 16) */
   unsigned l2_size = 16 - util_logbase2(vk_format_get_blocksize(format));
   unsigned w = (1u << ((l2_size + 1) / 2)) * vk_format_get_blockwidth(format);
   unsigned h = (1u << (l2_size / 2)) * vk_format_get_blockheight(format);

   prop->imageGranularity = (VkExtent3D){w, h, 1};
}

void
radv_GetPhysicalDeviceSparseImageFormatProperties2(
   VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
   uint32_t *pPropertyCount, VkSparseImageFormatProperties2 *pProperties)
{
   RADV_FROM_HANDLE(radv_physical_device, pdev, physicalDevice);
   VkResult result;

   if (pFormatInfo->samples > VK_SAMPLE_COUNT_1_BIT) {
      *pPropertyCount = 0;
      return;
   }

   const VkPhysicalDeviceImageFormatInfo2 fmt_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .format = pFormatInfo->format,
      .type = pFormatInfo->type,
      .tiling = pFormatInfo->tiling,
      .usage = pFormatInfo->usage,
      .flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT};

   VkImageFormatProperties fmt_props;
   result = radv_get_image_format_properties(pdev, &fmt_info, pFormatInfo->format, &fmt_props);
   if (result != VK_SUCCESS) {
      *pPropertyCount = 0;
      return;
   }

   VK_OUTARRAY_MAKE_TYPED(VkSparseImageFormatProperties2, out, pProperties, pPropertyCount);

   vk_outarray_append_typed(VkSparseImageFormatProperties2, &out, prop)
   {
      fill_sparse_image_format_properties(pdev, pFormatInfo->format, &prop->properties);
   };
}

void
radv_GetImageSparseMemoryRequirements2(VkDevice _device,
                                       const VkImageSparseMemoryRequirementsInfo2 *pInfo,
                                       uint32_t *pSparseMemoryRequirementCount,
                                       VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_image, image, pInfo->image);

   if (!(image->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT)) {
      *pSparseMemoryRequirementCount = 0;
      return;
   }

   VK_OUTARRAY_MAKE_TYPED(VkSparseImageMemoryRequirements2, out, pSparseMemoryRequirements,
                          pSparseMemoryRequirementCount);

   vk_outarray_append_typed(VkSparseImageMemoryRequirements2, &out, req)
   {
      fill_sparse_image_format_properties(device->physical_device, image->vk_format,
                                          &req->memoryRequirements.formatProperties);
      req->memoryRequirements.imageMipTailFirstLod = image->planes[0].surface.first_mip_tail_level;

      if (req->memoryRequirements.imageMipTailFirstLod < image->info.levels) {
         if (device->physical_device->rad_info.chip_class >= GFX9) {
            /* The tail is always a single tile per layer. */
            req->memoryRequirements.imageMipTailSize = 65536;
            req->memoryRequirements.imageMipTailOffset =
               image->planes[0]
                  .surface.u.gfx9.prt_level_offset[req->memoryRequirements.imageMipTailFirstLod] &
               ~65535;
            req->memoryRequirements.imageMipTailStride =
               image->planes[0].surface.u.gfx9.surf_slice_size;
         } else {
            req->memoryRequirements.imageMipTailOffset =
               (uint64_t)image->planes[0]
                  .surface.u.legacy.level[req->memoryRequirements.imageMipTailFirstLod]
                  .offset_256B * 256;
            req->memoryRequirements.imageMipTailSize =
               image->size - req->memoryRequirements.imageMipTailOffset;
            req->memoryRequirements.imageMipTailStride = 0;
         }
      } else {
         req->memoryRequirements.imageMipTailSize = 0;
         req->memoryRequirements.imageMipTailOffset = 0;
         req->memoryRequirements.imageMipTailStride = 0;
      }
   };
}

void
radv_GetDeviceImageSparseMemoryRequirementsKHR(VkDevice device,
                                               const VkDeviceImageMemoryRequirementsKHR* pInfo,
                                               uint32_t *pSparseMemoryRequirementCount,
                                               VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   UNUSED VkResult result;
   VkImage image;

   /* Determining the image size/alignment require to create a surface, which is complicated without
    * creating an image.
    * TODO: Avoid creating an image.
    */
   result = radv_CreateImage(device, pInfo->pCreateInfo, NULL, &image);
   assert(result == VK_SUCCESS);

   VkImageSparseMemoryRequirementsInfo2 info2 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_SPARSE_MEMORY_REQUIREMENTS_INFO_2,
      .image = image,
   };

   radv_GetImageSparseMemoryRequirements2(device, &info2, pSparseMemoryRequirementCount,
                                          pSparseMemoryRequirements);

   radv_DestroyImage(device, image, NULL);
}

void
radv_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   VkExternalMemoryFeatureFlagBits flags = 0;
   VkExternalMemoryHandleTypeFlags export_flags = 0;
   VkExternalMemoryHandleTypeFlags compat_flags = 0;
   switch (pExternalBufferInfo->handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      flags = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = export_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
                                    VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      break;
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT:
      flags = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
      break;
   default:
      break;
   }
   pExternalBufferProperties->externalMemoryProperties = (VkExternalMemoryProperties){
      .externalMemoryFeatures = flags,
      .exportFromImportedHandleTypes = export_flags,
      .compatibleHandleTypes = compat_flags,
   };
}

/* DCC channel type categories within which formats can be reinterpreted
 * while keeping the same DCC encoding. The swizzle must also match. */
enum dcc_channel_type {
   dcc_channel_float,
   dcc_channel_uint,
   dcc_channel_sint,
   dcc_channel_incompatible,
};

/* Return the type of DCC encoding. */
static void
radv_get_dcc_channel_type(const struct util_format_description *desc, enum dcc_channel_type *type,
                          unsigned *size)
{
   int i;

   /* Find the first non-void channel. */
   for (i = 0; i < desc->nr_channels; i++)
      if (desc->channel[i].type != UTIL_FORMAT_TYPE_VOID)
         break;
   if (i == desc->nr_channels) {
      *type = dcc_channel_incompatible;
      return;
   }

   switch (desc->channel[i].size) {
   case 32:
   case 16:
   case 10:
   case 8:
      *size = desc->channel[i].size;
      if (desc->channel[i].type == UTIL_FORMAT_TYPE_FLOAT)
         *type = dcc_channel_float;
      else if (desc->channel[i].type == UTIL_FORMAT_TYPE_UNSIGNED)
         *type = dcc_channel_uint;
      else
         *type = dcc_channel_sint;
      break;
   default:
      *type = dcc_channel_incompatible;
      break;
   }
}

/* Return if it's allowed to reinterpret one format as another with DCC enabled. */
bool
radv_dcc_formats_compatible(VkFormat format1, VkFormat format2, bool *sign_reinterpret)
{
   const struct util_format_description *desc1, *desc2;
   enum dcc_channel_type type1, type2;
   unsigned size1, size2;
   int i;

   if (format1 == format2)
      return true;

   desc1 = vk_format_description(format1);
   desc2 = vk_format_description(format2);

   if (desc1->nr_channels != desc2->nr_channels)
      return false;

   /* Swizzles must be the same. */
   for (i = 0; i < desc1->nr_channels; i++)
      if (desc1->swizzle[i] <= PIPE_SWIZZLE_W && desc2->swizzle[i] <= PIPE_SWIZZLE_W &&
          desc1->swizzle[i] != desc2->swizzle[i])
         return false;

   radv_get_dcc_channel_type(desc1, &type1, &size1);
   radv_get_dcc_channel_type(desc2, &type2, &size2);

   if (type1 == dcc_channel_incompatible || type2 == dcc_channel_incompatible ||
       (type1 == dcc_channel_float) != (type2 == dcc_channel_float) || size1 != size2)
      return false;

   if (type1 != type2)
      *sign_reinterpret = true;

   return true;
}
