/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * Based on u_format.h which is:
 * Copyright 2009-2010 Vmware, Inc.
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

#ifndef VK_FORMAT_H
#define VK_FORMAT_H

#include <assert.h>
#include <vulkan/vulkan.h>
#include <util/macros.h>

enum vk_format_layout {
	/**
	 * Formats with vk_format_block::width == vk_format_block::height == 1
	 * that can be described as an ordinary data structure.
	 */
	VK_FORMAT_LAYOUT_PLAIN = 0,

	/**
	 * Formats with sub-sampled channels.
	 *
	 * This is for formats like YVYU where there is less than one sample per
	 * pixel.
	 */
	VK_FORMAT_LAYOUT_SUBSAMPLED = 3,

	/**
	 * S3 Texture Compression formats.
	 */
	VK_FORMAT_LAYOUT_S3TC = 4,

	/**
	 * Red-Green Texture Compression formats.
	 */
	VK_FORMAT_LAYOUT_RGTC = 5,

	/**
	 * Ericsson Texture Compression
	 */
	VK_FORMAT_LAYOUT_ETC = 6,

	/**
	 * BC6/7 Texture Compression
	 */
	VK_FORMAT_LAYOUT_BPTC = 7,

	/**
	 * ASTC
	 */
	VK_FORMAT_LAYOUT_ASTC = 8,

	/**
	 * Everything else that doesn't fit in any of the above layouts.
	 */
	VK_FORMAT_LAYOUT_OTHER = 9
};

struct vk_format_block
{
	/** Block width in pixels */
	unsigned width;

	/** Block height in pixels */
	unsigned height;

	/** Block size in bits */
	unsigned bits;
};

enum vk_format_type {
	VK_FORMAT_TYPE_VOID = 0,
	VK_FORMAT_TYPE_UNSIGNED = 1,
	VK_FORMAT_TYPE_SIGNED = 2,
	VK_FORMAT_TYPE_FIXED = 3,
	VK_FORMAT_TYPE_FLOAT = 4
};


enum vk_format_colorspace {
	VK_FORMAT_COLORSPACE_RGB = 0,
	VK_FORMAT_COLORSPACE_SRGB = 1,
	VK_FORMAT_COLORSPACE_YUV = 2,
	VK_FORMAT_COLORSPACE_ZS = 3
};

struct vk_format_channel_description {
	unsigned type:5;
	unsigned normalized:1;
	unsigned pure_integer:1;
	unsigned scaled:1;
	unsigned size:8;
	unsigned shift:16;
};

struct vk_format_description
{
	VkFormat format;
	const char *name;
	const char *short_name;

	struct vk_format_block block;
	enum vk_format_layout layout;

	unsigned nr_channels:3;
	unsigned is_array:1;
	unsigned is_bitmask:1;
	unsigned is_mixed:1;

	struct vk_format_channel_description channel[4];

	unsigned char swizzle[4];

	enum vk_format_colorspace colorspace;
};

extern const struct vk_format_description vk_format_description_table[];

const struct vk_format_description *vk_format_description(VkFormat format);

/**
 * Return total bits needed for the pixel format per block.
 */
static inline unsigned
vk_format_get_blocksizebits(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);

	assert(desc);
	if (!desc) {
		return 0;
	}

	return desc->block.bits;
}

/**
 * Return bytes per block (not pixel) for the given format.
 */
static inline unsigned
vk_format_get_blocksize(VkFormat format)
{
	unsigned bits = vk_format_get_blocksizebits(format);
	unsigned bytes = bits / 8;

	assert(bits % 8 == 0);
	assert(bytes > 0);
	if (bytes == 0) {
		bytes = 1;
	}

	return bytes;
}

static inline unsigned
vk_format_get_blockwidth(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);

	assert(desc);
	if (!desc) {
		return 1;
	}

	return desc->block.width;
}

static inline unsigned
vk_format_get_blockheight(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);

	assert(desc);
	if (!desc) {
		return 1;
	}

	return desc->block.height;
}

/**
 * Return the index of the first non-void channel
 * -1 if no non-void channels
 */
static inline int
vk_format_get_first_non_void_channel(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);
	int i;

	for (i = 0; i < 4; i++)
		if (desc->channel[i].type != VK_FORMAT_TYPE_VOID)
			break;

	if (i == 4)
		return -1;

	return i;
}

enum vk_swizzle {
	VK_SWIZZLE_X,
	VK_SWIZZLE_Y,
	VK_SWIZZLE_Z,
	VK_SWIZZLE_W,
	VK_SWIZZLE_0,
	VK_SWIZZLE_1,
	VK_SWIZZLE_NONE,
	VK_SWIZZLE_MAX, /**< Number of enums counter (must be last) */
};

static inline VkImageAspectFlags
vk_format_aspects(VkFormat format)
{
	switch (format) {
	case VK_FORMAT_UNDEFINED:
		return 0;

	case VK_FORMAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT;

	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D32_SFLOAT:
		return VK_IMAGE_ASPECT_DEPTH_BIT;

	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

static inline enum vk_swizzle
radv_swizzle_conv(VkComponentSwizzle component, const unsigned char chan[4], VkComponentSwizzle vk_swiz)
{
	int x;

	if (vk_swiz == VK_COMPONENT_SWIZZLE_IDENTITY)
		vk_swiz = component;
	switch (vk_swiz) {
	case VK_COMPONENT_SWIZZLE_ZERO:
		return VK_SWIZZLE_0;
	case VK_COMPONENT_SWIZZLE_ONE:
		return VK_SWIZZLE_1;
	case VK_COMPONENT_SWIZZLE_R:
		for (x = 0; x < 4; x++)
			if (chan[x] == 0)
				return x;
		return VK_SWIZZLE_0;
	case VK_COMPONENT_SWIZZLE_G:
		for (x = 0; x < 4; x++)
			if (chan[x] == 1)
				return x;
		return VK_SWIZZLE_0;
	case VK_COMPONENT_SWIZZLE_B:
		for (x = 0; x < 4; x++)
			if (chan[x] == 2)
				return x;
		return VK_SWIZZLE_0;
	case VK_COMPONENT_SWIZZLE_A:
		for (x = 0; x < 4; x++)
			if (chan[x] == 3)
				return x;
		return VK_SWIZZLE_1;
	default:
		unreachable("Illegal swizzle");
	}
}

static inline void vk_format_compose_swizzles(const VkComponentMapping *mapping,
					      const unsigned char swz[4],
					      enum vk_swizzle dst[4])
{
	dst[0] = radv_swizzle_conv(VK_COMPONENT_SWIZZLE_R, swz, mapping->r);
	dst[1] = radv_swizzle_conv(VK_COMPONENT_SWIZZLE_G, swz, mapping->g);
	dst[2] = radv_swizzle_conv(VK_COMPONENT_SWIZZLE_B, swz, mapping->b);
	dst[3] = radv_swizzle_conv(VK_COMPONENT_SWIZZLE_A, swz, mapping->a);
}

static inline bool
vk_format_is_compressed(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);

	assert(desc);
	if (!desc) {
		return false;
	}

	switch (desc->layout) {
	case VK_FORMAT_LAYOUT_S3TC:
	case VK_FORMAT_LAYOUT_RGTC:
	case VK_FORMAT_LAYOUT_ETC:
	case VK_FORMAT_LAYOUT_BPTC:
	case VK_FORMAT_LAYOUT_ASTC:
		/* XXX add other formats in the future */
		return true;
	default:
		return false;
	}
}

static inline bool
vk_format_has_depth(const struct vk_format_description *desc)
{
	return desc->colorspace == VK_FORMAT_COLORSPACE_ZS &&
		desc->swizzle[0] != VK_SWIZZLE_NONE;
}

static inline bool
vk_format_has_stencil(const struct vk_format_description *desc)
{
	return desc->colorspace == VK_FORMAT_COLORSPACE_ZS &&
		desc->swizzle[1] != VK_SWIZZLE_NONE;
}

static inline bool
vk_format_is_depth_or_stencil(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);

	assert(desc);
	if (!desc) {
		return false;
	}

	return vk_format_has_depth(desc) ||
		vk_format_has_stencil(desc);
}

static inline bool
vk_format_is_depth(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);

	assert(desc);
	if (!desc) {
		return false;
	}

	return vk_format_has_depth(desc);
}

static inline bool
vk_format_is_stencil(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);

	assert(desc);
	if (!desc) {
		return false;
	}

	return vk_format_has_stencil(desc);
}

static inline bool
vk_format_is_color(VkFormat format)
{
	return !vk_format_is_depth_or_stencil(format);
}

static inline VkFormat
vk_format_depth_only(VkFormat format)
{
	switch (format) {
	case VK_FORMAT_D16_UNORM_S8_UINT:
		return VK_FORMAT_D16_UNORM;
	case VK_FORMAT_D24_UNORM_S8_UINT:
		return VK_FORMAT_X8_D24_UNORM_PACK32;
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_FORMAT_D32_SFLOAT;
	default:
		return format;
	}
}

static inline bool
vk_format_is_int(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);
	int channel =  vk_format_get_first_non_void_channel(format);

	return channel >= 0 && desc->channel[channel].pure_integer;
}

static inline bool
vk_format_is_srgb(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);
	return desc->colorspace == VK_FORMAT_COLORSPACE_SRGB;
}

static inline VkFormat
vk_format_stencil_only(VkFormat format)
{
	return VK_FORMAT_S8_UINT;
}

static inline unsigned
vk_format_get_component_bits(VkFormat format,
			     enum vk_format_colorspace colorspace,
			     unsigned component)
{
	const struct vk_format_description *desc = vk_format_description(format);
	enum vk_format_colorspace desc_colorspace;

	assert(format);
	if (!format) {
		return 0;
	}

	assert(component < 4);

	/* Treat RGB and SRGB as equivalent. */
	if (colorspace == VK_FORMAT_COLORSPACE_SRGB) {
		colorspace = VK_FORMAT_COLORSPACE_RGB;
	}
	if (desc->colorspace == VK_FORMAT_COLORSPACE_SRGB) {
		desc_colorspace = VK_FORMAT_COLORSPACE_RGB;
	} else {
		desc_colorspace = desc->colorspace;
	}

	if (desc_colorspace != colorspace) {
		return 0;
	}

	switch (desc->swizzle[component]) {
	case VK_SWIZZLE_X:
		return desc->channel[0].size;
	case VK_SWIZZLE_Y:
		return desc->channel[1].size;
	case VK_SWIZZLE_Z:
		return desc->channel[2].size;
	case VK_SWIZZLE_W:
		return desc->channel[3].size;
	default:
		return 0;
	}
}

static inline VkFormat
vk_to_non_srgb_format(VkFormat format)
{
	switch(format) {
	case VK_FORMAT_R8_SRGB :
		return VK_FORMAT_R8_UNORM;
	case VK_FORMAT_R8G8_SRGB:
		return VK_FORMAT_R8G8_UNORM;
	case VK_FORMAT_R8G8B8_SRGB:
		return VK_FORMAT_R8G8B8_UNORM;
	case VK_FORMAT_B8G8R8_SRGB:
		return VK_FORMAT_B8G8R8_UNORM;
	case VK_FORMAT_R8G8B8A8_SRGB :
		return VK_FORMAT_R8G8B8A8_UNORM;
	case VK_FORMAT_B8G8R8A8_SRGB:
		return VK_FORMAT_B8G8R8A8_UNORM;
	case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
		return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
	default:
		return format;
	}
}

static inline unsigned
vk_format_get_nr_components(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);
	return desc->nr_channels;
}

#endif /* VK_FORMAT_H */
