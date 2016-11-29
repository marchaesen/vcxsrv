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

#include "radv_private.h"

#include "vk_format.h"
#include "sid.h"
#include "r600d_common.h"

#include "util/u_half.h"
#include "util/format_srgb.h"

uint32_t radv_translate_buffer_dataformat(const struct vk_format_description *desc,
					  int first_non_void)
{
	unsigned type;
	int i;

	if (desc->format == VK_FORMAT_B10G11R11_UFLOAT_PACK32)
		return V_008F0C_BUF_DATA_FORMAT_10_11_11;

	if (first_non_void < 0)
		return V_008F0C_BUF_DATA_FORMAT_INVALID;
	type = desc->channel[first_non_void].type;

	if (type == VK_FORMAT_TYPE_FIXED)
		return V_008F0C_BUF_DATA_FORMAT_INVALID;
	if (desc->nr_channels == 4 &&
	    desc->channel[0].size == 10 &&
	    desc->channel[1].size == 10 &&
	    desc->channel[2].size == 10 &&
	    desc->channel[3].size == 2)
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
		if (type != VK_FORMAT_TYPE_FLOAT &&
		    !desc->channel[first_non_void].pure_integer)
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
	}

	return V_008F0C_BUF_DATA_FORMAT_INVALID;
}

uint32_t radv_translate_buffer_numformat(const struct vk_format_description *desc,
					 int first_non_void)
{
	if (desc->format == VK_FORMAT_B10G11R11_UFLOAT_PACK32)
		return V_008F0C_BUF_NUM_FORMAT_FLOAT;

	if (first_non_void < 0)
		return ~0;

	switch (desc->channel[first_non_void].type) {
	case VK_FORMAT_TYPE_SIGNED:
		if (desc->channel[first_non_void].normalized)
			return V_008F0C_BUF_NUM_FORMAT_SNORM;
		else if (desc->channel[first_non_void].pure_integer)
			return V_008F0C_BUF_NUM_FORMAT_SINT;
		else
			return V_008F0C_BUF_NUM_FORMAT_SSCALED;
		break;
	case VK_FORMAT_TYPE_UNSIGNED:
		if (desc->channel[first_non_void].normalized)
			return V_008F0C_BUF_NUM_FORMAT_UNORM;
		else if (desc->channel[first_non_void].pure_integer)
			return V_008F0C_BUF_NUM_FORMAT_UINT;
		else
			return V_008F0C_BUF_NUM_FORMAT_USCALED;
		break;
	case VK_FORMAT_TYPE_FLOAT:
	default:
		return V_008F0C_BUF_NUM_FORMAT_FLOAT;
	}
}

uint32_t radv_translate_tex_dataformat(VkFormat format,
				       const struct vk_format_description *desc,
				       int first_non_void)
{
	bool uniform = true;
	int i;

	if (!desc)
		return ~0;
	/* Colorspace (return non-RGB formats directly). */
	switch (desc->colorspace) {
		/* Depth stencil formats */
	case VK_FORMAT_COLORSPACE_ZS:
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

	case VK_FORMAT_COLORSPACE_YUV:
		goto out_unknown; /* TODO */

	case VK_FORMAT_COLORSPACE_SRGB:
		if (desc->nr_channels != 4 && desc->nr_channels != 1)
			goto out_unknown;
		break;

	default:
		break;
	}

	if (desc->layout == VK_FORMAT_LAYOUT_RGTC) {
		switch(format) {
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

	if (desc->layout == VK_FORMAT_LAYOUT_S3TC) {
		switch(format) {
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

	if (desc->layout == VK_FORMAT_LAYOUT_BPTC) {
		switch(format) {
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

	if (format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32) {
		return V_008F14_IMG_DATA_FORMAT_5_9_9_9;
	} else if (format == VK_FORMAT_B10G11R11_UFLOAT_PACK32) {
		return V_008F14_IMG_DATA_FORMAT_10_11_11;
	}

	/* R8G8Bx_SNORM - TODO CxV8U8 */

	/* hw cannot support mixed formats (except depth/stencil, since only
	 * depth is read).*/
	if (desc->is_mixed && desc->colorspace != VK_FORMAT_COLORSPACE_ZS)
		goto out_unknown;

	/* See whether the components are of the same size. */
	for (i = 1; i < desc->nr_channels; i++) {
		uniform = uniform && desc->channel[0].size == desc->channel[i].size;
	}

	/* Non-uniform formats. */
	if (!uniform) {
		switch(desc->nr_channels) {
		case 3:
			if (desc->channel[0].size == 5 &&
			    desc->channel[1].size == 6 &&
			    desc->channel[2].size == 5) {
				return V_008F14_IMG_DATA_FORMAT_5_6_5;
			}
			goto out_unknown;
		case 4:
			if (desc->channel[0].size == 5 &&
			    desc->channel[1].size == 5 &&
			    desc->channel[2].size == 5 &&
			    desc->channel[3].size == 1) {
				return V_008F14_IMG_DATA_FORMAT_1_5_5_5;
			}
			if (desc->channel[0].size == 1 &&
			    desc->channel[1].size == 5 &&
			    desc->channel[2].size == 5 &&
			    desc->channel[3].size == 5) {
				return V_008F14_IMG_DATA_FORMAT_5_5_5_1;
			}
			if (desc->channel[0].size == 10 &&
			    desc->channel[1].size == 10 &&
			    desc->channel[2].size == 10 &&
			    desc->channel[3].size == 2) {
				/* Closed VK driver does this also no 2/10/10/10 snorm */
				if (desc->channel[0].type == VK_FORMAT_TYPE_SIGNED &&
				    desc->channel[0].normalized)
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
#if 0 /* Not supported for render targets */
		case 3:
			return V_008F14_IMG_DATA_FORMAT_32_32_32;
#endif
		case 4:
			return V_008F14_IMG_DATA_FORMAT_32_32_32_32;
		}
	}

out_unknown:
	/* R600_ERR("Unable to handle texformat %d %s\n", format, vk_format_name(format)); */
	return ~0;
}

uint32_t radv_translate_tex_numformat(VkFormat format,
				      const struct vk_format_description *desc,
				      int first_non_void)
{
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
					return V_008F14_IMG_NUM_FORMAT_SRGB;
				case VK_FORMAT_BC4_SNORM_BLOCK:
				case VK_FORMAT_BC5_SNORM_BLOCK:
			        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
					return V_008F14_IMG_NUM_FORMAT_SNORM;
				default:
					return V_008F14_IMG_NUM_FORMAT_UNORM;
				}
			} else if (desc->layout == VK_FORMAT_LAYOUT_SUBSAMPLED) {
				return V_008F14_IMG_NUM_FORMAT_UNORM;
			} else {
				return V_008F14_IMG_NUM_FORMAT_FLOAT;
			}
		} else if (desc->colorspace == VK_FORMAT_COLORSPACE_SRGB) {
			return V_008F14_IMG_NUM_FORMAT_SRGB;
		} else {
			switch (desc->channel[first_non_void].type) {
			case VK_FORMAT_TYPE_FLOAT:
				return V_008F14_IMG_NUM_FORMAT_FLOAT;
			case VK_FORMAT_TYPE_SIGNED:
				if (desc->channel[first_non_void].normalized)
					return V_008F14_IMG_NUM_FORMAT_SNORM;
				else if (desc->channel[first_non_void].pure_integer)
					return V_008F14_IMG_NUM_FORMAT_SINT;
				else
					return V_008F14_IMG_NUM_FORMAT_SSCALED;
			case VK_FORMAT_TYPE_UNSIGNED:
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

uint32_t radv_translate_color_numformat(VkFormat format,
					const struct vk_format_description *desc,
					int first_non_void)
{
	unsigned ntype;
	if (first_non_void == 4 || desc->channel[first_non_void].type == VK_FORMAT_TYPE_FLOAT)
		ntype = V_028C70_NUMBER_FLOAT;
	else {
		ntype = V_028C70_NUMBER_UNORM;
		if (desc->colorspace == VK_FORMAT_COLORSPACE_SRGB)
			ntype = V_028C70_NUMBER_SRGB;
		else if (desc->channel[first_non_void].type == VK_FORMAT_TYPE_SIGNED) {
			if (desc->channel[first_non_void].pure_integer) {
				ntype = V_028C70_NUMBER_SINT;
			} else if (desc->channel[first_non_void].normalized) {
				ntype = V_028C70_NUMBER_SNORM;
			} else
				ntype = ~0u;
		} else if (desc->channel[first_non_void].type == VK_FORMAT_TYPE_UNSIGNED) {
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

static bool radv_is_sampler_format_supported(VkFormat format, bool *linear_sampling)
{
	const struct vk_format_description *desc = vk_format_description(format);
	uint32_t num_format;
	if (!desc || format == VK_FORMAT_UNDEFINED)
		return false;
	num_format = radv_translate_tex_numformat(format, desc,
						  vk_format_get_first_non_void_channel(format));

	if (num_format == V_008F14_IMG_NUM_FORMAT_USCALED ||
	    num_format == V_008F14_IMG_NUM_FORMAT_SSCALED)
		return false;

	if (num_format == V_008F14_IMG_NUM_FORMAT_UNORM ||
	    num_format == V_008F14_IMG_NUM_FORMAT_SNORM ||
	    num_format == V_008F14_IMG_NUM_FORMAT_FLOAT ||
	    num_format == V_008F14_IMG_NUM_FORMAT_SRGB)
		*linear_sampling = true;
	else
		*linear_sampling = false;
	return radv_translate_tex_dataformat(format, vk_format_description(format),
					     vk_format_get_first_non_void_channel(format)) != ~0U;
}


static bool radv_is_storage_image_format_supported(struct radv_physical_device *physical_device,
						   VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);
	unsigned data_format, num_format;
	if (!desc || format == VK_FORMAT_UNDEFINED)
		return false;

	data_format = radv_translate_tex_dataformat(format, desc,
						    vk_format_get_first_non_void_channel(format));
	num_format = radv_translate_tex_numformat(format, desc,
						  vk_format_get_first_non_void_channel(format));

	if(data_format == ~0 || num_format == ~0)
		return false;

	/* Extracted from the GCN3 ISA document. */
	switch(num_format) {
	case V_008F14_IMG_NUM_FORMAT_UNORM:
	case V_008F14_IMG_NUM_FORMAT_SNORM:
	case V_008F14_IMG_NUM_FORMAT_UINT:
	case V_008F14_IMG_NUM_FORMAT_SINT:
	case V_008F14_IMG_NUM_FORMAT_FLOAT:
		break;
	default:
		return false;
	}

	switch(data_format) {
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
	default:
		return false;
	}
}

static bool radv_is_buffer_format_supported(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);
	unsigned data_format, num_format;
	if (!desc || format == VK_FORMAT_UNDEFINED)
		return false;

	data_format = radv_translate_buffer_dataformat(desc,
						       vk_format_get_first_non_void_channel(format));
	num_format = radv_translate_buffer_numformat(desc,
						     vk_format_get_first_non_void_channel(format));

	return data_format != V_008F0C_BUF_DATA_FORMAT_INVALID &&
		num_format != ~0;
}

bool radv_is_colorbuffer_format_supported(VkFormat format, bool *blendable)
{
	const struct vk_format_description *desc = vk_format_description(format);
	uint32_t color_format = radv_translate_colorformat(format);
	uint32_t color_swap = radv_translate_colorswap(format, false);
	uint32_t color_num_format = radv_translate_color_numformat(format,
								   desc,
								   vk_format_get_first_non_void_channel(format));

	if (color_num_format == V_028C70_NUMBER_UINT || color_num_format == V_028C70_NUMBER_SINT ||
	    color_format == V_028C70_COLOR_8_24 || color_format == V_028C70_COLOR_24_8 ||
	    color_format == V_028C70_COLOR_X24_8_32_FLOAT) {
		*blendable = false;
	} else
		*blendable = true;
	return color_format != V_028C70_COLOR_INVALID &&
		color_swap != ~0U &&
		color_num_format != ~0;
}

static bool radv_is_zs_format_supported(VkFormat format)
{
	return radv_translate_dbformat(format) != V_028040_Z_INVALID;
}

static void
radv_physical_device_get_format_properties(struct radv_physical_device *physical_device,
					   VkFormat format,
					   VkFormatProperties *out_properties)
{
	VkFormatFeatureFlags linear = 0, tiled = 0, buffer = 0;
	const struct vk_format_description *desc = vk_format_description(format);
	bool blendable;
	if (!desc) {
		out_properties->linearTilingFeatures = linear;
		out_properties->optimalTilingFeatures = tiled;
		out_properties->bufferFeatures = buffer;
		return;
	}

	if (radv_is_storage_image_format_supported(physical_device, format)) {
		tiled |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
		linear |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
	}

	if (radv_is_buffer_format_supported(format)) {
		buffer |= VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT |
			VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT |
			VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
	}

	if (vk_format_is_depth_or_stencil(format)) {
		if (radv_is_zs_format_supported(format))
			tiled |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
		tiled |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
		tiled |= VK_FORMAT_FEATURE_BLIT_SRC_BIT |
			VK_FORMAT_FEATURE_BLIT_DST_BIT;
	} else {
		bool linear_sampling;
		if (radv_is_sampler_format_supported(format, &linear_sampling)) {
			linear |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
				VK_FORMAT_FEATURE_BLIT_SRC_BIT;
			tiled |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
				VK_FORMAT_FEATURE_BLIT_SRC_BIT;
			if (linear_sampling) {
				linear |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
				tiled |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
			}
		}
		if (radv_is_colorbuffer_format_supported(format, &blendable)) {
			linear |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
			tiled |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
			if (blendable) {
				linear |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
				tiled |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
			}
		}
	}

	if (format == VK_FORMAT_R32_UINT || format == VK_FORMAT_R32_SINT) {
		buffer |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
		linear |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;
		tiled |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;
	}

	out_properties->linearTilingFeatures = linear;
	out_properties->optimalTilingFeatures = tiled;
	out_properties->bufferFeatures = buffer;
}

uint32_t radv_translate_colorformat(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);

#define HAS_SIZE(x,y,z,w)						\
	(desc->channel[0].size == (x) && desc->channel[1].size == (y) && \
         desc->channel[2].size == (z) && desc->channel[3].size == (w))

	if (format == VK_FORMAT_B10G11R11_UFLOAT_PACK32) /* isn't plain */
		return V_028C70_COLOR_10_11_11;

	if (desc->layout != VK_FORMAT_LAYOUT_PLAIN)
		return V_028C70_COLOR_INVALID;

	/* hw cannot support mixed formats (except depth/stencil, since
	 * stencil is not written to). */
	if (desc->is_mixed && desc->colorspace != VK_FORMAT_COLORSPACE_ZS)
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
		} else if (HAS_SIZE(8,24,0,0)) {
			return V_028C70_COLOR_24_8;
		} else if (HAS_SIZE(24,8,0,0)) {
			return V_028C70_COLOR_8_24;
		}
		break;
	case 3:
		if (HAS_SIZE(5,6,5,0)) {
			return V_028C70_COLOR_5_6_5;
		} else if (HAS_SIZE(32,8,24,0)) {
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
		} else if (HAS_SIZE(5,5,5,1)) {
			return V_028C70_COLOR_1_5_5_5;
		} else if (HAS_SIZE(1,5,5,5)) {
			return V_028C70_COLOR_5_5_5_1;
		} else if (HAS_SIZE(10,10,10,2)) {
			return V_028C70_COLOR_2_10_10_10;
		}
		break;
	}
	return V_028C70_COLOR_INVALID;
}

uint32_t radv_colorformat_endian_swap(uint32_t colorformat)
{
	if (0/*SI_BIG_ENDIAN*/) {
		switch(colorformat) {
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

uint32_t radv_translate_dbformat(VkFormat format)
{
	switch (format) {
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D16_UNORM_S8_UINT:
		return V_028040_Z_16;
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D24_UNORM_S8_UINT:
		return V_028040_Z_24; /* deprecated on SI */
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return V_028040_Z_32_FLOAT;
	default:
		return V_028040_Z_INVALID;
	}
}

unsigned radv_translate_colorswap(VkFormat format, bool do_endian_swap)
{
	const struct vk_format_description *desc = vk_format_description(format);

#define HAS_SWIZZLE(chan,swz) (desc->swizzle[chan] == VK_SWIZZLE_##swz)

	if (format == VK_FORMAT_B10G11R11_UFLOAT_PACK32)
		return V_0280A0_SWAP_STD;

	if (desc->layout != VK_FORMAT_LAYOUT_PLAIN)
		return ~0U;

	switch (desc->nr_channels) {
	case 1:
		if (HAS_SWIZZLE(0,X))
			return V_0280A0_SWAP_STD; /* X___ */
		else if (HAS_SWIZZLE(3,X))
			return V_0280A0_SWAP_ALT_REV; /* ___X */
		break;
	case 2:
		if ((HAS_SWIZZLE(0,X) && HAS_SWIZZLE(1,Y)) ||
		    (HAS_SWIZZLE(0,X) && HAS_SWIZZLE(1,NONE)) ||
		    (HAS_SWIZZLE(0,NONE) && HAS_SWIZZLE(1,Y)))
			return V_0280A0_SWAP_STD; /* XY__ */
		else if ((HAS_SWIZZLE(0,Y) && HAS_SWIZZLE(1,X)) ||
			 (HAS_SWIZZLE(0,Y) && HAS_SWIZZLE(1,NONE)) ||
		         (HAS_SWIZZLE(0,NONE) && HAS_SWIZZLE(1,X)))
			/* YX__ */
			return (do_endian_swap ? V_0280A0_SWAP_STD : V_0280A0_SWAP_STD_REV);
		else if (HAS_SWIZZLE(0,X) && HAS_SWIZZLE(3,Y))
			return V_0280A0_SWAP_ALT; /* X__Y */
		else if (HAS_SWIZZLE(0,Y) && HAS_SWIZZLE(3,X))
			return V_0280A0_SWAP_ALT_REV; /* Y__X */
		break;
	case 3:
		if (HAS_SWIZZLE(0,X))
			return (do_endian_swap ? V_0280A0_SWAP_STD_REV : V_0280A0_SWAP_STD);
		else if (HAS_SWIZZLE(0,Z))
			return V_0280A0_SWAP_STD_REV; /* ZYX */
		break;
	case 4:
		/* check the middle channels, the 1st and 4th channel can be NONE */
		if (HAS_SWIZZLE(1,Y) && HAS_SWIZZLE(2,Z)) {
			return V_0280A0_SWAP_STD; /* XYZW */
		} else if (HAS_SWIZZLE(1,Z) && HAS_SWIZZLE(2,Y)) {
			return V_0280A0_SWAP_STD_REV; /* WZYX */
		} else if (HAS_SWIZZLE(1,Y) && HAS_SWIZZLE(2,X)) {
			return V_0280A0_SWAP_ALT; /* ZYXW */
		} else if (HAS_SWIZZLE(1,Z) && HAS_SWIZZLE(2,W)) {
			/* YZWX */
			if (desc->is_array)
				return V_0280A0_SWAP_ALT_REV;
			else
				return (do_endian_swap ? V_0280A0_SWAP_ALT : V_0280A0_SWAP_ALT_REV);
		}
		break;
	}
	return ~0U;
}

bool radv_format_pack_clear_color(VkFormat format,
				  uint32_t clear_vals[2],
				  VkClearColorValue *value)
{
	uint8_t r = 0, g = 0, b = 0, a = 0;
	const struct vk_format_description *desc = vk_format_description(format);

	if (vk_format_get_component_bits(format, VK_FORMAT_COLORSPACE_RGB, 0) <= 8) {
		if (desc->colorspace == VK_FORMAT_COLORSPACE_RGB) {
			r = float_to_ubyte(value->float32[0]);
			g = float_to_ubyte(value->float32[1]);
			b = float_to_ubyte(value->float32[2]);
			a = float_to_ubyte(value->float32[3]);
		} else if (desc->colorspace == VK_FORMAT_COLORSPACE_SRGB) {
			r = util_format_linear_float_to_srgb_8unorm(value->float32[0]);
			g = util_format_linear_float_to_srgb_8unorm(value->float32[1]);
			b = util_format_linear_float_to_srgb_8unorm(value->float32[2]);
			a = float_to_ubyte(value->float32[3]);
		}
	}
	switch (format) {
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8_SRGB:
		clear_vals[0] = r;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R8G8_SRGB:
		clear_vals[0] = r | g << 8;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_R8G8B8A8_UNORM:
		clear_vals[0] = r | g << 8 | b << 16 | a << 24;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_UNORM:
		clear_vals[0] = b | g << 8 | r << 16 | a << 24;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
	case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
		clear_vals[0] = r | g << 8 | b << 16 | a << 24;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R8_UINT:
		clear_vals[0] = value->uint32[0] & 0xff;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R16_UINT:
		clear_vals[0] = value->uint32[0] & 0xffff;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R8G8_UINT:
		clear_vals[0] = value->uint32[0] & 0xff;
		clear_vals[0] |= (value->uint32[1] & 0xff) << 8;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R8G8B8A8_UINT:
		clear_vals[0] = value->uint32[0] & 0xff;
		clear_vals[0] |= (value->uint32[1] & 0xff) << 8;
		clear_vals[0] |= (value->uint32[2] & 0xff) << 16;
		clear_vals[0] |= (value->uint32[3] & 0xff) << 24;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_A8B8G8R8_UINT_PACK32:
		clear_vals[0] = value->uint32[0] & 0xff;
		clear_vals[0] |= (value->uint32[1] & 0xff) << 8;
		clear_vals[0] |= (value->uint32[2] & 0xff) << 16;
		clear_vals[0] |= (value->uint32[3] & 0xff) << 24;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R16G16_UINT:
		clear_vals[0] = value->uint32[0] & 0xffff;
		clear_vals[0] |= (value->uint32[1] & 0xffff) << 16;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R16G16B16A16_UINT:
		clear_vals[0] = value->uint32[0] & 0xffff;
		clear_vals[0] |= (value->uint32[1] & 0xffff) << 16;
		clear_vals[1] = value->uint32[2] & 0xffff;
		clear_vals[1] |= (value->uint32[3] & 0xffff) << 16;
		break;
	case VK_FORMAT_R32_UINT:
		clear_vals[0] = value->uint32[0];
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R32G32_UINT:
		clear_vals[0] = value->uint32[0];
		clear_vals[1] = value->uint32[1];
		break;
	case VK_FORMAT_R32_SINT:
		clear_vals[0] = value->int32[0];
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R16_SFLOAT:
		clear_vals[0] = util_float_to_half(value->float32[0]);
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R16G16_SFLOAT:
		clear_vals[0] = util_float_to_half(value->float32[0]);
		clear_vals[0] |= (uint32_t)util_float_to_half(value->float32[1]) << 16;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		clear_vals[0] = util_float_to_half(value->float32[0]);
		clear_vals[0] |= (uint32_t)util_float_to_half(value->float32[1]) << 16;
		clear_vals[1] = util_float_to_half(value->float32[2]);
		clear_vals[1] |= (uint32_t)util_float_to_half(value->float32[3]) << 16;
		break;
	case VK_FORMAT_R16_UNORM:
		clear_vals[0] = ((uint16_t)util_iround(CLAMP(value->float32[0], 0.0f, 1.0f) * 0xffff)) & 0xffff;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R16G16_UNORM:
		clear_vals[0] = ((uint16_t)util_iround(CLAMP(value->float32[0], 0.0f, 1.0f) * 0xffff)) & 0xffff;
		clear_vals[0] |= ((uint16_t)util_iround(CLAMP(value->float32[1], 0.0f, 1.0f) * 0xffff)) << 16;
		clear_vals[1] = 0;
		break;
	case VK_FORMAT_R16G16B16A16_UNORM:
		clear_vals[0] = ((uint16_t)util_iround(CLAMP(value->float32[0], 0.0f, 1.0f) * 0xffff)) & 0xffff;
		clear_vals[0] |= ((uint16_t)util_iround(CLAMP(value->float32[1], 0.0f, 1.0f) * 0xffff)) << 16;
		clear_vals[1] = ((uint16_t)util_iround(CLAMP(value->float32[2], 0.0f, 1.0f) * 0xffff)) & 0xffff;
		clear_vals[1] |= ((uint16_t)util_iround(CLAMP(value->float32[3], 0.0f, 1.0f) * 0xffff)) << 16;
		break;
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		/* TODO */
		return false;
	case VK_FORMAT_R32G32_SFLOAT:
		clear_vals[0] = fui(value->float32[0]);
		clear_vals[1] = fui(value->float32[1]);
		break;
	case VK_FORMAT_R32_SFLOAT:
		clear_vals[1] = 0;
		clear_vals[0] = fui(value->float32[0]);
		break;
	default:
		fprintf(stderr, "failed to fast clear %d\n", format);
		return false;
	}
	return true;
}

void radv_GetPhysicalDeviceFormatProperties(
	VkPhysicalDevice                            physicalDevice,
	VkFormat                                    format,
	VkFormatProperties*                         pFormatProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, physical_device, physicalDevice);

	radv_physical_device_get_format_properties(physical_device,
						   format,
						   pFormatProperties);
}

VkResult radv_GetPhysicalDeviceImageFormatProperties(
	VkPhysicalDevice                            physicalDevice,
	VkFormat                                    format,
	VkImageType                                 type,
	VkImageTiling                               tiling,
	VkImageUsageFlags                           usage,
	VkImageCreateFlags                          createFlags,
	VkImageFormatProperties*                    pImageFormatProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, physical_device, physicalDevice);
	VkFormatProperties format_props;
	VkFormatFeatureFlags format_feature_flags;
	VkExtent3D maxExtent;
	uint32_t maxMipLevels;
	uint32_t maxArraySize;
	VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;

	radv_physical_device_get_format_properties(physical_device, format,
						   &format_props);
	if (tiling == VK_IMAGE_TILING_LINEAR) {
		format_feature_flags = format_props.linearTilingFeatures;
	} else if (tiling == VK_IMAGE_TILING_OPTIMAL) {
		format_feature_flags = format_props.optimalTilingFeatures;
	} else {
		unreachable("bad VkImageTiling");
	}

	if (format_feature_flags == 0)
		goto unsupported;

	switch (type) {
	default:
		unreachable("bad vkimage type\n");
	case VK_IMAGE_TYPE_1D:
		maxExtent.width = 16384;
		maxExtent.height = 1;
		maxExtent.depth = 1;
		maxMipLevels = 15; /* log2(maxWidth) + 1 */
		maxArraySize = 2048;
		break;
	case VK_IMAGE_TYPE_2D:
		maxExtent.width = 16384;
		maxExtent.height = 16384;
		maxExtent.depth = 1;
		maxMipLevels = 15; /* log2(maxWidth) + 1 */
		maxArraySize = 2048;
		break;
	case VK_IMAGE_TYPE_3D:
		maxExtent.width = 2048;
		maxExtent.height = 2048;
		maxExtent.depth = 2048;
		maxMipLevels = 12; /* log2(maxWidth) + 1 */
		maxArraySize = 1;
		break;
	}

	if (tiling == VK_IMAGE_TILING_OPTIMAL &&
	    type == VK_IMAGE_TYPE_2D &&
	    (format_feature_flags & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
				     VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
	    !(createFlags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
	    !(usage & VK_IMAGE_USAGE_STORAGE_BIT)) {
		sampleCounts |= VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
	}

	if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
		if (!(format_feature_flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
			goto unsupported;
		}
	}

	if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
		if (!(format_feature_flags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
			goto unsupported;
		}
	}

	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
		if (!(format_feature_flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
			goto unsupported;
		}
	}

	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
		if (!(format_feature_flags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
			goto unsupported;
		}
	}

	*pImageFormatProperties = (VkImageFormatProperties) {
		.maxExtent = maxExtent,
		.maxMipLevels = maxMipLevels,
		.maxArrayLayers = maxArraySize,
		.sampleCounts = sampleCounts,

		/* FINISHME: Accurately calculate
		 * VkImageFormatProperties::maxResourceSize.
		 */
		.maxResourceSize = UINT32_MAX,
	};

	return VK_SUCCESS;
unsupported:
	*pImageFormatProperties = (VkImageFormatProperties) {
		.maxExtent = { 0, 0, 0 },
		.maxMipLevels = 0,
		.maxArrayLayers = 0,
		.sampleCounts = 0,
		.maxResourceSize = 0,
	};

	return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

void radv_GetPhysicalDeviceSparseImageFormatProperties(
	VkPhysicalDevice                            physicalDevice,
	VkFormat                                    format,
	VkImageType                                 type,
	uint32_t                                    samples,
	VkImageUsageFlags                           usage,
	VkImageTiling                               tiling,
	uint32_t*                                   pNumProperties,
	VkSparseImageFormatProperties*              pProperties)
{
	/* Sparse images are not yet supported. */
	*pNumProperties = 0;
}
