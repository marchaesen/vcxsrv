/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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
#include "radv_radeon_winsys.h"
#include "sid.h"
#include "util/debug.h"
static unsigned
radv_choose_tiling(struct radv_device *Device,
		   const struct radv_image_create_info *create_info)
{
	const VkImageCreateInfo *pCreateInfo = create_info->vk_info;

	if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR) {
		assert(pCreateInfo->samples <= 1);
		return RADEON_SURF_MODE_LINEAR_ALIGNED;
	}

	/* MSAA resources must be 2D tiled. */
	if (pCreateInfo->samples > 1)
		return RADEON_SURF_MODE_2D;

	return RADEON_SURF_MODE_2D;
}
static int
radv_init_surface(struct radv_device *device,
		  struct radeon_surf *surface,
		  const struct radv_image_create_info *create_info)
{
	const VkImageCreateInfo *pCreateInfo = create_info->vk_info;
	unsigned array_mode = radv_choose_tiling(device, create_info);
	const struct vk_format_description *desc =
		vk_format_description(pCreateInfo->format);
	bool is_depth, is_stencil, blendable;

	is_depth = vk_format_has_depth(desc);
	is_stencil = vk_format_has_stencil(desc);
	surface->npix_x = pCreateInfo->extent.width;
	surface->npix_y = pCreateInfo->extent.height;
	surface->npix_z = pCreateInfo->extent.depth;

	surface->blk_w = vk_format_get_blockwidth(pCreateInfo->format);
	surface->blk_h = vk_format_get_blockheight(pCreateInfo->format);
	surface->blk_d = 1;
	surface->array_size = pCreateInfo->arrayLayers;
	surface->last_level = pCreateInfo->mipLevels - 1;

	surface->bpe = vk_format_get_blocksize(pCreateInfo->format);
	/* align byte per element on dword */
	if (surface->bpe == 3) {
		surface->bpe = 4;
	}
	surface->nsamples = pCreateInfo->samples ? pCreateInfo->samples : 1;
	surface->flags = RADEON_SURF_SET(array_mode, MODE);

	switch (pCreateInfo->imageType){
	case VK_IMAGE_TYPE_1D:
		if (pCreateInfo->arrayLayers > 1)
			surface->flags |= RADEON_SURF_SET(RADEON_SURF_TYPE_1D_ARRAY, TYPE);
		else
			surface->flags |= RADEON_SURF_SET(RADEON_SURF_TYPE_1D, TYPE);
		break;
	case VK_IMAGE_TYPE_2D:
		if (pCreateInfo->arrayLayers > 1)
			surface->flags |= RADEON_SURF_SET(RADEON_SURF_TYPE_2D_ARRAY, TYPE);
		else
			surface->flags |= RADEON_SURF_SET(RADEON_SURF_TYPE_2D, TYPE);
		break;
	case VK_IMAGE_TYPE_3D:
		surface->flags |= RADEON_SURF_SET(RADEON_SURF_TYPE_3D, TYPE);
		break;
	default:
		unreachable("unhandled image type");
	}

	if (is_depth) {
		surface->flags |= RADEON_SURF_ZBUFFER;
	}

	if (is_stencil)
		surface->flags |= RADEON_SURF_SBUFFER |
			RADEON_SURF_HAS_SBUFFER_MIPTREE;

	surface->flags |= RADEON_SURF_HAS_TILE_MODE_INDEX;

	if ((pCreateInfo->usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
	                           VK_IMAGE_USAGE_STORAGE_BIT)) ||
	    (pCreateInfo->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) ||
            (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR) ||
            device->physical_device->rad_info.chip_class < VI ||
            create_info->scanout || (device->debug_flags & RADV_DEBUG_NO_DCC) ||
            !radv_is_colorbuffer_format_supported(pCreateInfo->format, &blendable))
		surface->flags |= RADEON_SURF_DISABLE_DCC;
	if (create_info->scanout)
		surface->flags |= RADEON_SURF_SCANOUT;
	return 0;
}
#define ATI_VENDOR_ID 0x1002
static uint32_t si_get_bo_metadata_word1(struct radv_device *device)
{
	return (ATI_VENDOR_ID << 16) | device->physical_device->rad_info.pci_id;
}

static inline unsigned
si_tile_mode_index(const struct radv_image *image, unsigned level, bool stencil)
{
	if (stencil)
		return image->surface.stencil_tiling_index[level];
	else
		return image->surface.tiling_index[level];
}

static unsigned radv_map_swizzle(unsigned swizzle)
{
	switch (swizzle) {
	case VK_SWIZZLE_Y:
		return V_008F0C_SQ_SEL_Y;
	case VK_SWIZZLE_Z:
		return V_008F0C_SQ_SEL_Z;
	case VK_SWIZZLE_W:
		return V_008F0C_SQ_SEL_W;
	case VK_SWIZZLE_0:
		return V_008F0C_SQ_SEL_0;
	case VK_SWIZZLE_1:
		return V_008F0C_SQ_SEL_1;
	default: /* VK_SWIZZLE_X */
		return V_008F0C_SQ_SEL_X;
	}
}

static void
radv_make_buffer_descriptor(struct radv_device *device,
			    struct radv_buffer *buffer,
			    VkFormat vk_format,
			    unsigned offset,
			    unsigned range,
			    uint32_t *state)
{
	const struct vk_format_description *desc;
	unsigned stride;
	uint64_t gpu_address = device->ws->buffer_get_va(buffer->bo);
	uint64_t va = gpu_address + buffer->offset;
	unsigned num_format, data_format;
	int first_non_void;
	desc = vk_format_description(vk_format);
	first_non_void = vk_format_get_first_non_void_channel(vk_format);
	stride = desc->block.bits / 8;

	num_format = radv_translate_buffer_numformat(desc, first_non_void);
	data_format = radv_translate_buffer_dataformat(desc, first_non_void);

	va += offset;
	state[0] = va;
	state[1] = S_008F04_BASE_ADDRESS_HI(va >> 32) |
		S_008F04_STRIDE(stride);
	state[2] = range;
	state[3] = S_008F0C_DST_SEL_X(radv_map_swizzle(desc->swizzle[0])) |
		   S_008F0C_DST_SEL_Y(radv_map_swizzle(desc->swizzle[1])) |
		   S_008F0C_DST_SEL_Z(radv_map_swizzle(desc->swizzle[2])) |
		   S_008F0C_DST_SEL_W(radv_map_swizzle(desc->swizzle[3])) |
		   S_008F0C_NUM_FORMAT(num_format) |
		   S_008F0C_DATA_FORMAT(data_format);
}

static void
si_set_mutable_tex_desc_fields(struct radv_device *device,
			       struct radv_image *image,
			       const struct radeon_surf_level *base_level_info,
			       unsigned base_level, unsigned first_level,
			       unsigned block_width, bool is_stencil,
			       uint32_t *state)
{
	uint64_t gpu_address = device->ws->buffer_get_va(image->bo) + image->offset;
	uint64_t va = gpu_address + base_level_info->offset;
	unsigned pitch = base_level_info->nblk_x * block_width;

	state[1] &= C_008F14_BASE_ADDRESS_HI;
	state[3] &= C_008F1C_TILING_INDEX;
	state[4] &= C_008F20_PITCH;
	state[6] &= C_008F28_COMPRESSION_EN;

	assert(!(va & 255));

	state[0] = va >> 8;
	state[1] |= S_008F14_BASE_ADDRESS_HI(va >> 40);
	state[3] |= S_008F1C_TILING_INDEX(si_tile_mode_index(image, base_level,
							     is_stencil));
	state[4] |= S_008F20_PITCH(pitch - 1);

	if (image->surface.dcc_size && image->surface.level[first_level].dcc_enabled) {
		state[6] |= S_008F28_COMPRESSION_EN(1);
		state[7] = (gpu_address +
			    image->dcc_offset +
			    base_level_info->dcc_offset) >> 8;
	}
}

static unsigned radv_tex_dim(VkImageType image_type, VkImageViewType view_type,
			     unsigned nr_layers, unsigned nr_samples, bool is_storage_image)
{
	if (view_type == VK_IMAGE_VIEW_TYPE_CUBE || view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
		return is_storage_image ? V_008F1C_SQ_RSRC_IMG_2D_ARRAY : V_008F1C_SQ_RSRC_IMG_CUBE;
	switch (image_type) {
	case VK_IMAGE_TYPE_1D:
		return nr_layers > 1 ? V_008F1C_SQ_RSRC_IMG_1D_ARRAY : V_008F1C_SQ_RSRC_IMG_1D;
	case VK_IMAGE_TYPE_2D:
		if (nr_samples > 1)
			return nr_layers > 1 ? V_008F1C_SQ_RSRC_IMG_2D_MSAA_ARRAY : V_008F1C_SQ_RSRC_IMG_2D_MSAA;
		else
			return nr_layers > 1 ? V_008F1C_SQ_RSRC_IMG_2D_ARRAY : V_008F1C_SQ_RSRC_IMG_2D;
	case VK_IMAGE_TYPE_3D:
		if (view_type == VK_IMAGE_VIEW_TYPE_3D)
			return V_008F1C_SQ_RSRC_IMG_3D;
		else
			return V_008F1C_SQ_RSRC_IMG_2D_ARRAY;
	default:
		unreachable("illegale image type");
	}
}
/**
 * Build the sampler view descriptor for a texture.
 */
static void
si_make_texture_descriptor(struct radv_device *device,
			   struct radv_image *image,
			   bool sampler,
			   VkImageViewType view_type,
			   VkFormat vk_format,
			   const VkComponentMapping *mapping,
			   unsigned first_level, unsigned last_level,
			   unsigned first_layer, unsigned last_layer,
			   unsigned width, unsigned height, unsigned depth,
			   uint32_t *state,
			   uint32_t *fmask_state)
{
	const struct vk_format_description *desc;
	enum vk_swizzle swizzle[4];
	int first_non_void;
	unsigned num_format, data_format, type;

	desc = vk_format_description(vk_format);

	if (desc->colorspace == VK_FORMAT_COLORSPACE_ZS) {
		const unsigned char swizzle_xxxx[4] = {0, 0, 0, 0};
		vk_format_compose_swizzles(mapping, swizzle_xxxx, swizzle);
	} else {
		vk_format_compose_swizzles(mapping, desc->swizzle, swizzle);
	}

	first_non_void = vk_format_get_first_non_void_channel(vk_format);

	num_format = radv_translate_tex_numformat(vk_format, desc, first_non_void);
	if (num_format == ~0) {
		num_format = 0;
	}

	data_format = radv_translate_tex_dataformat(vk_format, desc, first_non_void);
	if (data_format == ~0) {
		data_format = 0;
	}

	type = radv_tex_dim(image->type, view_type, image->array_size, image->samples,
			    (image->usage & VK_IMAGE_USAGE_STORAGE_BIT));
	if (type == V_008F1C_SQ_RSRC_IMG_1D_ARRAY) {
	        height = 1;
		depth = image->array_size;
	} else if (type == V_008F1C_SQ_RSRC_IMG_2D_ARRAY ||
		   type == V_008F1C_SQ_RSRC_IMG_2D_MSAA_ARRAY) {
		if (view_type != VK_IMAGE_VIEW_TYPE_3D)
			depth = image->array_size;
	} else if (type == V_008F1C_SQ_RSRC_IMG_CUBE)
		depth = image->array_size / 6;

	state[0] = 0;
	state[1] = (S_008F14_DATA_FORMAT(data_format) |
		    S_008F14_NUM_FORMAT(num_format));
	state[2] = (S_008F18_WIDTH(width - 1) |
		    S_008F18_HEIGHT(height - 1));
	state[3] = (S_008F1C_DST_SEL_X(radv_map_swizzle(swizzle[0])) |
		    S_008F1C_DST_SEL_Y(radv_map_swizzle(swizzle[1])) |
		    S_008F1C_DST_SEL_Z(radv_map_swizzle(swizzle[2])) |
		    S_008F1C_DST_SEL_W(radv_map_swizzle(swizzle[3])) |
		    S_008F1C_BASE_LEVEL(image->samples > 1 ?
					0 : first_level) |
		    S_008F1C_LAST_LEVEL(image->samples > 1 ?
					util_logbase2(image->samples) :
					last_level) |
		    S_008F1C_POW2_PAD(image->levels > 1) |
		    S_008F1C_TYPE(type));
	state[4] = S_008F20_DEPTH(depth - 1);
	state[5] = (S_008F24_BASE_ARRAY(first_layer) |
		    S_008F24_LAST_ARRAY(last_layer));
	state[6] = 0;
	state[7] = 0;

	if (image->dcc_offset) {
		unsigned swap = radv_translate_colorswap(vk_format, FALSE);

		state[6] = S_008F28_ALPHA_IS_ON_MSB(swap <= 1);
	} else {
		/* The last dword is unused by hw. The shader uses it to clear
		 * bits in the first dword of sampler state.
		 */
		if (device->physical_device->rad_info.chip_class <= CIK && image->samples <= 1) {
			if (first_level == last_level)
				state[7] = C_008F30_MAX_ANISO_RATIO;
			else
				state[7] = 0xffffffff;
		}
	}

	/* Initialize the sampler view for FMASK. */
	if (image->fmask.size) {
		uint32_t fmask_format;
		uint64_t gpu_address = device->ws->buffer_get_va(image->bo);
		uint64_t va;

		va = gpu_address + image->offset + image->fmask.offset;

		switch (image->samples) {
		case 2:
			fmask_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S2_F2;
			break;
		case 4:
			fmask_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S4_F4;
			break;
		case 8:
			fmask_format = V_008F14_IMG_DATA_FORMAT_FMASK32_S8_F8;
			break;
		default:
			assert(0);
			fmask_format = V_008F14_IMG_DATA_FORMAT_INVALID;
		}

		fmask_state[0] = va >> 8;
		fmask_state[1] = S_008F14_BASE_ADDRESS_HI(va >> 40) |
			S_008F14_DATA_FORMAT(fmask_format) |
			S_008F14_NUM_FORMAT(V_008F14_IMG_NUM_FORMAT_UINT);
		fmask_state[2] = S_008F18_WIDTH(width - 1) |
			S_008F18_HEIGHT(height - 1);
		fmask_state[3] = S_008F1C_DST_SEL_X(V_008F1C_SQ_SEL_X) |
			S_008F1C_DST_SEL_Y(V_008F1C_SQ_SEL_X) |
			S_008F1C_DST_SEL_Z(V_008F1C_SQ_SEL_X) |
			S_008F1C_DST_SEL_W(V_008F1C_SQ_SEL_X) |
			S_008F1C_TILING_INDEX(image->fmask.tile_mode_index) |
			S_008F1C_TYPE(radv_tex_dim(image->type, view_type, 1, 0, false));
		fmask_state[4] = S_008F20_DEPTH(depth - 1) |
			S_008F20_PITCH(image->fmask.pitch_in_pixels - 1);
		fmask_state[5] = S_008F24_BASE_ARRAY(first_layer) |
			S_008F24_LAST_ARRAY(last_layer);
		fmask_state[6] = 0;
		fmask_state[7] = 0;
	}
}

static void
radv_query_opaque_metadata(struct radv_device *device,
			   struct radv_image *image,
			   struct radeon_bo_metadata *md)
{
	static const VkComponentMapping fixedmapping;
	uint32_t desc[8], i;

	/* Metadata image format format version 1:
	 * [0] = 1 (metadata format identifier)
	 * [1] = (VENDOR_ID << 16) | PCI_ID
	 * [2:9] = image descriptor for the whole resource
	 *         [2] is always 0, because the base address is cleared
	 *         [9] is the DCC offset bits [39:8] from the beginning of
	 *             the buffer
	 * [10:10+LAST_LEVEL] = mipmap level offset bits [39:8] for each level
	 */
	md->metadata[0] = 1; /* metadata image format version 1 */

	/* TILE_MODE_INDEX is ambiguous without a PCI ID. */
	md->metadata[1] = si_get_bo_metadata_word1(device);


	si_make_texture_descriptor(device, image, true,
				   (VkImageViewType)image->type, image->vk_format,
				   &fixedmapping, 0, image->levels - 1, 0,
				   image->array_size,
				   image->extent.width, image->extent.height,
				   image->extent.depth,
				   desc, NULL);

	si_set_mutable_tex_desc_fields(device, image, &image->surface.level[0], 0, 0,
				       image->surface.blk_w, false, desc);

	/* Clear the base address and set the relative DCC offset. */
	desc[0] = 0;
	desc[1] &= C_008F14_BASE_ADDRESS_HI;
	desc[7] = image->dcc_offset >> 8;

	/* Dwords [2:9] contain the image descriptor. */
	memcpy(&md->metadata[2], desc, sizeof(desc));

	/* Dwords [10:..] contain the mipmap level offsets. */
	for (i = 0; i <= image->levels - 1; i++)
		md->metadata[10+i] = image->surface.level[i].offset >> 8;

	md->size_metadata = (11 + image->levels - 1) * 4;
}

void
radv_init_metadata(struct radv_device *device,
		   struct radv_image *image,
		   struct radeon_bo_metadata *metadata)
{
	struct radeon_surf *surface = &image->surface;

	memset(metadata, 0, sizeof(*metadata));
	metadata->microtile = surface->level[0].mode >= RADEON_SURF_MODE_1D ?
		RADEON_LAYOUT_TILED : RADEON_LAYOUT_LINEAR;
	metadata->macrotile = surface->level[0].mode >= RADEON_SURF_MODE_2D ?
		RADEON_LAYOUT_TILED : RADEON_LAYOUT_LINEAR;
	metadata->pipe_config = surface->pipe_config;
	metadata->bankw = surface->bankw;
	metadata->bankh = surface->bankh;
	metadata->tile_split = surface->tile_split;
	metadata->mtilea = surface->mtilea;
	metadata->num_banks = surface->num_banks;
	metadata->stride = surface->level[0].pitch_bytes;
	metadata->scanout = (surface->flags & RADEON_SURF_SCANOUT) != 0;

	radv_query_opaque_metadata(device, image, metadata);
}

/* The number of samples can be specified independently of the texture. */
static void
radv_image_get_fmask_info(struct radv_device *device,
			  struct radv_image *image,
			  unsigned nr_samples,
			  struct radv_fmask_info *out)
{
	/* FMASK is allocated like an ordinary texture. */
	struct radeon_surf fmask = image->surface;

	memset(out, 0, sizeof(*out));

	fmask.bo_alignment = 0;
	fmask.bo_size = 0;
	fmask.nsamples = 1;
	fmask.flags |= RADEON_SURF_FMASK;

	/* Force 2D tiling if it wasn't set. This may occur when creating
	 * FMASK for MSAA resolve on R6xx. On R6xx, the single-sample
	 * destination buffer must have an FMASK too. */
	fmask.flags = RADEON_SURF_CLR(fmask.flags, MODE);
	fmask.flags |= RADEON_SURF_SET(RADEON_SURF_MODE_2D, MODE);

	fmask.flags |= RADEON_SURF_HAS_TILE_MODE_INDEX;

	switch (nr_samples) {
	case 2:
	case 4:
		fmask.bpe = 1;
		break;
	case 8:
		fmask.bpe = 4;
		break;
	default:
		return;
	}

	device->ws->surface_init(device->ws, &fmask);
	assert(fmask.level[0].mode == RADEON_SURF_MODE_2D);

	out->slice_tile_max = (fmask.level[0].nblk_x * fmask.level[0].nblk_y) / 64;
	if (out->slice_tile_max)
		out->slice_tile_max -= 1;

	out->tile_mode_index = fmask.tiling_index[0];
	out->pitch_in_pixels = fmask.level[0].nblk_x;
	out->bank_height = fmask.bankh;
	out->alignment = MAX2(256, fmask.bo_alignment);
	out->size = fmask.bo_size;
}

static void
radv_image_alloc_fmask(struct radv_device *device,
		       struct radv_image *image)
{
	radv_image_get_fmask_info(device, image, image->samples, &image->fmask);

	image->fmask.offset = align64(image->size, image->fmask.alignment);
	image->size = image->fmask.offset + image->fmask.size;
}

static void
radv_image_get_cmask_info(struct radv_device *device,
			  struct radv_image *image,
			  struct radv_cmask_info *out)
{
	unsigned pipe_interleave_bytes = device->physical_device->rad_info.pipe_interleave_bytes;
	unsigned num_pipes = device->physical_device->rad_info.num_tile_pipes;
	unsigned cl_width, cl_height;

	switch (num_pipes) {
	case 2:
		cl_width = 32;
		cl_height = 16;
		break;
	case 4:
		cl_width = 32;
		cl_height = 32;
		break;
	case 8:
		cl_width = 64;
		cl_height = 32;
		break;
	case 16: /* Hawaii */
		cl_width = 64;
		cl_height = 64;
		break;
	default:
		assert(0);
		return;
	}

	unsigned base_align = num_pipes * pipe_interleave_bytes;

	unsigned width = align(image->surface.npix_x, cl_width*8);
	unsigned height = align(image->surface.npix_y, cl_height*8);
	unsigned slice_elements = (width * height) / (8*8);

	/* Each element of CMASK is a nibble. */
	unsigned slice_bytes = slice_elements / 2;

	out->slice_tile_max = (width * height) / (128*128);
	if (out->slice_tile_max)
		out->slice_tile_max -= 1;

	out->alignment = MAX2(256, base_align);
	out->size = (image->type == VK_IMAGE_TYPE_3D ? image->extent.depth : image->array_size) *
		    align(slice_bytes, base_align);
}

static void
radv_image_alloc_cmask(struct radv_device *device,
		       struct radv_image *image)
{
	radv_image_get_cmask_info(device, image, &image->cmask);

	image->cmask.offset = align64(image->size, image->cmask.alignment);
	/* + 8 for storing the clear values */
	image->clear_value_offset = image->cmask.offset + image->cmask.size;
	image->size = image->cmask.offset + image->cmask.size + 8;
}

static void
radv_image_alloc_dcc(struct radv_device *device,
		       struct radv_image *image)
{
	image->dcc_offset = align64(image->size, image->surface.dcc_alignment);
	/* + 8 for storing the clear values */
	image->clear_value_offset = image->dcc_offset + image->surface.dcc_size;
	image->size = image->dcc_offset + image->surface.dcc_size + 8;
}

static unsigned
radv_image_get_htile_size(struct radv_device *device,
			  struct radv_image *image)
{
	unsigned cl_width, cl_height, width, height;
	unsigned slice_elements, slice_bytes, base_align;
	unsigned num_pipes = device->physical_device->rad_info.num_tile_pipes;
	unsigned pipe_interleave_bytes = device->physical_device->rad_info.pipe_interleave_bytes;

	/* Overalign HTILE on P2 configs to work around GPU hangs in
	 * piglit/depthstencil-render-miplevels 585.
	 *
	 * This has been confirmed to help Kabini & Stoney, where the hangs
	 * are always reproducible. I think I have seen the test hang
	 * on Carrizo too, though it was very rare there.
	 */
	if (device->physical_device->rad_info.chip_class >= CIK && num_pipes < 4)
		num_pipes = 4;

	switch (num_pipes) {
	case 1:
		cl_width = 32;
		cl_height = 16;
		break;
	case 2:
		cl_width = 32;
		cl_height = 32;
		break;
	case 4:
		cl_width = 64;
		cl_height = 32;
		break;
	case 8:
		cl_width = 64;
		cl_height = 64;
		break;
	case 16:
		cl_width = 128;
		cl_height = 64;
		break;
	default:
		assert(0);
		return 0;
	}

	width = align(image->surface.npix_x, cl_width * 8);
	height = align(image->surface.npix_y, cl_height * 8);

	slice_elements = (width * height) / (8 * 8);
	slice_bytes = slice_elements * 4;

	base_align = num_pipes * pipe_interleave_bytes;

	image->htile.pitch = width;
	image->htile.height = height;
	image->htile.xalign = cl_width * 8;
	image->htile.yalign = cl_height * 8;

	return image->array_size *
		align(slice_bytes, base_align);
}

static void
radv_image_alloc_htile(struct radv_device *device,
		       struct radv_image *image)
{
	if (device->debug_flags & RADV_DEBUG_NO_HIZ)
		return;

	image->htile.size = radv_image_get_htile_size(device, image);

	if (!image->htile.size)
		return;

	image->htile.offset = align64(image->size, 32768);

	/* + 8 for storing the clear values */
	image->clear_value_offset = image->htile.offset + image->htile.size;
	image->size = image->htile.offset + image->htile.size + 8;
	image->alignment = align64(image->alignment, 32768);
}

VkResult
radv_image_create(VkDevice _device,
		  const struct radv_image_create_info *create_info,
		  const VkAllocationCallbacks* alloc,
		  VkImage *pImage)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	const VkImageCreateInfo *pCreateInfo = create_info->vk_info;
	struct radv_image *image = NULL;
	bool can_cmask_dcc = false;
	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);

	radv_assert(pCreateInfo->mipLevels > 0);
	radv_assert(pCreateInfo->arrayLayers > 0);
	radv_assert(pCreateInfo->samples > 0);
	radv_assert(pCreateInfo->extent.width > 0);
	radv_assert(pCreateInfo->extent.height > 0);
	radv_assert(pCreateInfo->extent.depth > 0);

	image = vk_alloc2(&device->alloc, alloc, sizeof(*image), 8,
			    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (!image)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	memset(image, 0, sizeof(*image));
	image->type = pCreateInfo->imageType;
	image->extent = pCreateInfo->extent;
	image->vk_format = pCreateInfo->format;
	image->levels = pCreateInfo->mipLevels;
	image->array_size = pCreateInfo->arrayLayers;
	image->samples = pCreateInfo->samples;
	image->tiling = pCreateInfo->tiling;
	image->usage = pCreateInfo->usage;

	image->exclusive = pCreateInfo->sharingMode == VK_SHARING_MODE_EXCLUSIVE;
	if (pCreateInfo->sharingMode == VK_SHARING_MODE_CONCURRENT) {
		for (uint32_t i = 0; i < pCreateInfo->queueFamilyIndexCount; ++i)
			image->queue_family_mask |= 1u << pCreateInfo->pQueueFamilyIndices[i];
	}

	radv_init_surface(device, &image->surface, create_info);

	device->ws->surface_init(device->ws, &image->surface);

	image->size = image->surface.bo_size;
	image->alignment = image->surface.bo_alignment;

	if (image->exclusive || image->queue_family_mask == 1)
		can_cmask_dcc = true;

	if ((pCreateInfo->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
	    image->surface.dcc_size && can_cmask_dcc)
		radv_image_alloc_dcc(device, image);
	else
		image->surface.dcc_size = 0;

	if ((pCreateInfo->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
	    pCreateInfo->mipLevels == 1 &&
	    !image->surface.dcc_size && image->extent.depth == 1 && can_cmask_dcc)
		radv_image_alloc_cmask(device, image);
	if (image->samples > 1 && vk_format_is_color(pCreateInfo->format)) {
		radv_image_alloc_fmask(device, image);
	} else if (vk_format_is_depth(pCreateInfo->format)) {

		radv_image_alloc_htile(device, image);
	}


	if (create_info->stride && create_info->stride != image->surface.level[0].pitch_bytes) {
		image->surface.level[0].nblk_x = create_info->stride / image->surface.bpe;
		image->surface.level[0].pitch_bytes = create_info->stride;
		image->surface.level[0].slice_size = create_info->stride * image->surface.level[0].nblk_y;
	}
	*pImage = radv_image_to_handle(image);

	return VK_SUCCESS;
}

void
radv_image_view_init(struct radv_image_view *iview,
		     struct radv_device *device,
		     const VkImageViewCreateInfo* pCreateInfo,
		     struct radv_cmd_buffer *cmd_buffer,
		     VkImageUsageFlags usage_mask)
{
	RADV_FROM_HANDLE(radv_image, image, pCreateInfo->image);
	const VkImageSubresourceRange *range = &pCreateInfo->subresourceRange;
	uint32_t blk_w;
	bool is_stencil = false;
	switch (image->type) {
	case VK_IMAGE_TYPE_1D:
	case VK_IMAGE_TYPE_2D:
		assert(range->baseArrayLayer + radv_get_layerCount(image, range) - 1 <= image->array_size);
		break;
	case VK_IMAGE_TYPE_3D:
		assert(range->baseArrayLayer + radv_get_layerCount(image, range) - 1
		       <= radv_minify(image->extent.depth, range->baseMipLevel));
		break;
	default:
		unreachable("bad VkImageType");
	}
	iview->image = image;
	iview->bo = image->bo;
	iview->type = pCreateInfo->viewType;
	iview->vk_format = pCreateInfo->format;
	iview->aspect_mask = pCreateInfo->subresourceRange.aspectMask;

	if (iview->aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT) {
		is_stencil = true;
		iview->vk_format = vk_format_stencil_only(iview->vk_format);
	} else if (iview->aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT) {
		iview->vk_format = vk_format_depth_only(iview->vk_format);
	}

	iview->extent = (VkExtent3D) {
		.width  = radv_minify(image->extent.width , range->baseMipLevel),
		.height = radv_minify(image->extent.height, range->baseMipLevel),
		.depth  = radv_minify(image->extent.depth , range->baseMipLevel),
	};

	iview->extent.width = round_up_u32(iview->extent.width * vk_format_get_blockwidth(iview->vk_format),
					   vk_format_get_blockwidth(image->vk_format));
	iview->extent.height = round_up_u32(iview->extent.height * vk_format_get_blockheight(iview->vk_format),
					    vk_format_get_blockheight(image->vk_format));

	assert(image->surface.blk_w % vk_format_get_blockwidth(image->vk_format) == 0);
	blk_w = image->surface.blk_w / vk_format_get_blockwidth(image->vk_format) * vk_format_get_blockwidth(iview->vk_format);
	iview->base_layer = range->baseArrayLayer;
	iview->layer_count = radv_get_layerCount(image, range);
	iview->base_mip = range->baseMipLevel;

	si_make_texture_descriptor(device, image, false,
				   iview->type,
				   iview->vk_format,
				   &pCreateInfo->components,
				   0, radv_get_levelCount(image, range) - 1,
				   range->baseArrayLayer,
				   range->baseArrayLayer + radv_get_layerCount(image, range) - 1,
				   iview->extent.width,
				   iview->extent.height,
				   iview->extent.depth,
				   iview->descriptor,
				   iview->fmask_descriptor);
	si_set_mutable_tex_desc_fields(device, image,
				       is_stencil ? &image->surface.stencil_level[range->baseMipLevel] : &image->surface.level[range->baseMipLevel], range->baseMipLevel,
				       range->baseMipLevel,
				       blk_w, is_stencil, iview->descriptor);
}

void radv_image_set_optimal_micro_tile_mode(struct radv_device *device,
					    struct radv_image *image, uint32_t micro_tile_mode)
{
	/* These magic numbers were copied from addrlib. It doesn't use any
	 * definitions for them either. They are all 2D_TILED_THIN1 modes with
	 * different bpp and micro tile mode.
	 */
	if (device->physical_device->rad_info.chip_class >= CIK) {
		switch (micro_tile_mode) {
		case 0: /* displayable */
			image->surface.tiling_index[0] = 10;
			break;
		case 1: /* thin */
			image->surface.tiling_index[0] = 14;
			break;
		case 3: /* rotated */
			image->surface.tiling_index[0] = 28;
			break;
		default: /* depth, thick */
			assert(!"unexpected micro mode");
			return;
		}
	} else { /* SI */
		switch (micro_tile_mode) {
		case 0: /* displayable */
			switch (image->surface.bpe) {
			case 1:
                            image->surface.tiling_index[0] = 10;
                            break;
			case 2:
                            image->surface.tiling_index[0] = 11;
                            break;
			default: /* 4, 8 */
                            image->surface.tiling_index[0] = 12;
                            break;
			}
			break;
		case 1: /* thin */
			switch (image->surface.bpe) {
			case 1:
                                image->surface.tiling_index[0] = 14;
                                break;
			case 2:
                                image->surface.tiling_index[0] = 15;
                                break;
			case 4:
                                image->surface.tiling_index[0] = 16;
                                break;
			default: /* 8, 16 */
                                image->surface.tiling_index[0] = 17;
                                break;
			}
			break;
		default: /* depth, thick */
			assert(!"unexpected micro mode");
			return;
		}
	}

	image->surface.micro_tile_mode = micro_tile_mode;
}

bool radv_layout_has_htile(const struct radv_image *image,
                           VkImageLayout layout)
{
	return (layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
		layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}

bool radv_layout_is_htile_compressed(const struct radv_image *image,
                                     VkImageLayout layout)
{
	return layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
}

bool radv_layout_can_expclear(const struct radv_image *image,
                              VkImageLayout layout)
{
	return (layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
		layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}

bool radv_layout_can_fast_clear(const struct radv_image *image,
			        VkImageLayout layout,
			        unsigned queue_mask)
{
	return layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
		queue_mask == (1u << RADV_QUEUE_GENERAL);
}


unsigned radv_image_queue_family_mask(const struct radv_image *image, int family) {
	if (image->exclusive)
		return 1u <<family;
	return image->queue_family_mask;
}

VkResult
radv_CreateImage(VkDevice device,
		 const VkImageCreateInfo *pCreateInfo,
		 const VkAllocationCallbacks *pAllocator,
		 VkImage *pImage)
{
	return radv_image_create(device,
				 &(struct radv_image_create_info) {
					 .vk_info = pCreateInfo,
						 .scanout = false,
						 },
				 pAllocator,
				 pImage);
}

void
radv_DestroyImage(VkDevice _device, VkImage _image,
		  const VkAllocationCallbacks *pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);

	if (!_image)
		return;

	vk_free2(&device->alloc, pAllocator, radv_image_from_handle(_image));
}

void radv_GetImageSubresourceLayout(
	VkDevice                                    device,
	VkImage                                     _image,
	const VkImageSubresource*                   pSubresource,
	VkSubresourceLayout*                        pLayout)
{
	RADV_FROM_HANDLE(radv_image, image, _image);
	int level = pSubresource->mipLevel;
	int layer = pSubresource->arrayLayer;

	pLayout->offset = image->surface.level[level].offset + image->surface.level[level].slice_size * layer;
	pLayout->rowPitch = image->surface.level[level].pitch_bytes;
	pLayout->arrayPitch = image->surface.level[level].slice_size;
	pLayout->depthPitch = image->surface.level[level].slice_size;
	pLayout->size = image->surface.level[level].slice_size;
	if (image->type == VK_IMAGE_TYPE_3D)
		pLayout->size *= image->surface.level[level].nblk_z;
}


VkResult
radv_CreateImageView(VkDevice _device,
		     const VkImageViewCreateInfo *pCreateInfo,
		     const VkAllocationCallbacks *pAllocator,
		     VkImageView *pView)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_image_view *view;

	view = vk_alloc2(&device->alloc, pAllocator, sizeof(*view), 8,
			   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (view == NULL)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	radv_image_view_init(view, device, pCreateInfo, NULL, ~0);

	*pView = radv_image_view_to_handle(view);

	return VK_SUCCESS;
}

void
radv_DestroyImageView(VkDevice _device, VkImageView _iview,
		      const VkAllocationCallbacks *pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_image_view, iview, _iview);

	if (!iview)
		return;
	vk_free2(&device->alloc, pAllocator, iview);
}

void radv_buffer_view_init(struct radv_buffer_view *view,
			   struct radv_device *device,
			   const VkBufferViewCreateInfo* pCreateInfo,
			   struct radv_cmd_buffer *cmd_buffer)
{
	RADV_FROM_HANDLE(radv_buffer, buffer, pCreateInfo->buffer);

	view->bo = buffer->bo;
	view->range = pCreateInfo->range == VK_WHOLE_SIZE ?
		buffer->size - pCreateInfo->offset : pCreateInfo->range;
	view->vk_format = pCreateInfo->format;

	radv_make_buffer_descriptor(device, buffer, view->vk_format,
				    pCreateInfo->offset, view->range, view->state);
}

VkResult
radv_CreateBufferView(VkDevice _device,
		      const VkBufferViewCreateInfo *pCreateInfo,
		      const VkAllocationCallbacks *pAllocator,
		      VkBufferView *pView)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_buffer_view *view;

	view = vk_alloc2(&device->alloc, pAllocator, sizeof(*view), 8,
			   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (!view)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	radv_buffer_view_init(view, device, pCreateInfo, NULL);

	*pView = radv_buffer_view_to_handle(view);

	return VK_SUCCESS;
}

void
radv_DestroyBufferView(VkDevice _device, VkBufferView bufferView,
		       const VkAllocationCallbacks *pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_buffer_view, view, bufferView);

	if (!view)
		return;

	vk_free2(&device->alloc, pAllocator, view);
}
