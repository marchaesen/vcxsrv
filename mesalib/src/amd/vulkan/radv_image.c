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

#include "radv_debug.h"
#include "radv_private.h"
#include "vk_format.h"
#include "vk_util.h"
#include "radv_radeon_winsys.h"
#include "sid.h"
#include "gfx9d.h"
#include "util/debug.h"
#include "util/u_atomic.h"
static unsigned
radv_choose_tiling(struct radv_device *device,
		   const struct radv_image_create_info *create_info)
{
	const VkImageCreateInfo *pCreateInfo = create_info->vk_info;

	if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR) {
		assert(pCreateInfo->samples <= 1);
		return RADEON_SURF_MODE_LINEAR_ALIGNED;
	}

	if (!vk_format_is_compressed(pCreateInfo->format) &&
	    !vk_format_is_depth_or_stencil(pCreateInfo->format)
	    && device->physical_device->rad_info.chip_class <= VI) {
		/* this causes hangs in some VK CTS tests on GFX9. */
		/* Textures with a very small height are recommended to be linear. */
		if (pCreateInfo->imageType == VK_IMAGE_TYPE_1D ||
		    /* Only very thin and long 2D textures should benefit from
		     * linear_aligned. */
		    (pCreateInfo->extent.width > 8 && pCreateInfo->extent.height <= 2))
			return RADEON_SURF_MODE_LINEAR_ALIGNED;
	}

	/* MSAA resources must be 2D tiled. */
	if (pCreateInfo->samples > 1)
		return RADEON_SURF_MODE_2D;

	return RADEON_SURF_MODE_2D;
}

static bool
radv_image_is_tc_compat_htile(struct radv_device *device,
			      const VkImageCreateInfo *pCreateInfo)
{
	/* TC-compat HTILE is only available for GFX8+. */
	if (device->physical_device->rad_info.chip_class < VI)
		return false;

	if (pCreateInfo->usage & VK_IMAGE_USAGE_STORAGE_BIT)
		return false;

	if (pCreateInfo->flags & (VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
				  VK_IMAGE_CREATE_EXTENDED_USAGE_BIT_KHR))
		return false;

	if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR)
		return false;

	if (pCreateInfo->mipLevels > 1)
		return false;

	/* FIXME: for some reason TC compat with 2/4/8 samples breaks some cts
	 * tests - disable for now */
	if (pCreateInfo->samples >= 2 &&
	    pCreateInfo->format == VK_FORMAT_D32_SFLOAT_S8_UINT)
		return false;

	/* GFX9 supports both 32-bit and 16-bit depth surfaces, while GFX8 only
	 * supports 32-bit. Though, it's possible to enable TC-compat for
	 * 16-bit depth surfaces if no Z planes are compressed.
	 */
	if (pCreateInfo->format != VK_FORMAT_D32_SFLOAT_S8_UINT &&
	    pCreateInfo->format != VK_FORMAT_D32_SFLOAT &&
	    pCreateInfo->format != VK_FORMAT_D16_UNORM)
		return false;

	return true;
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

	surface->blk_w = vk_format_get_blockwidth(pCreateInfo->format);
	surface->blk_h = vk_format_get_blockheight(pCreateInfo->format);

	surface->bpe = vk_format_get_blocksize(vk_format_depth_only(pCreateInfo->format));
	/* align byte per element on dword */
	if (surface->bpe == 3) {
		surface->bpe = 4;
	}
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
		if (radv_image_is_tc_compat_htile(device, pCreateInfo))
			surface->flags |= RADEON_SURF_TC_COMPATIBLE_HTILE;
	}

	if (is_stencil)
		surface->flags |= RADEON_SURF_SBUFFER;

	surface->flags |= RADEON_SURF_OPTIMIZE_FOR_SPACE;

	bool dcc_compatible_formats = radv_is_colorbuffer_format_supported(pCreateInfo->format, &blendable);
	if (pCreateInfo->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) {
		const struct  VkImageFormatListCreateInfoKHR *format_list =
		          (const struct  VkImageFormatListCreateInfoKHR *)
		                vk_find_struct_const(pCreateInfo->pNext,
		                                     IMAGE_FORMAT_LIST_CREATE_INFO_KHR);

		/* We have to ignore the existence of the list if viewFormatCount = 0 */
		if (format_list && format_list->viewFormatCount) {
			/* compatibility is transitive, so we only need to check
			 * one format with everything else. */
			for (unsigned i = 0; i < format_list->viewFormatCount; ++i) {
				if (!radv_dcc_formats_compatible(pCreateInfo->format,
				                                 format_list->pViewFormats[i]))
					dcc_compatible_formats = false;
			}
		} else {
			dcc_compatible_formats = false;
		}
	}

	if ((pCreateInfo->usage & VK_IMAGE_USAGE_STORAGE_BIT) ||
	    (pCreateInfo->flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT_KHR) ||
	    !dcc_compatible_formats ||
            (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR) ||
            pCreateInfo->mipLevels > 1 || pCreateInfo->arrayLayers > 1 ||
            device->physical_device->rad_info.chip_class < VI ||
            create_info->scanout || (device->instance->debug_flags & RADV_DEBUG_NO_DCC) ||
	    pCreateInfo->samples >= 2)
		surface->flags |= RADEON_SURF_DISABLE_DCC;
	if (create_info->scanout)
		surface->flags |= RADEON_SURF_SCANOUT;
	return 0;
}

static uint32_t si_get_bo_metadata_word1(struct radv_device *device)
{
	return (ATI_VENDOR_ID << 16) | device->physical_device->rad_info.pci_id;
}

static inline unsigned
si_tile_mode_index(const struct radv_image *image, unsigned level, bool stencil)
{
	if (stencil)
		return image->surface.u.legacy.stencil_tiling_index[level];
	else
		return image->surface.u.legacy.tiling_index[level];
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
	uint64_t gpu_address = radv_buffer_get_va(buffer->bo);
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

	if (device->physical_device->rad_info.chip_class != VI && stride) {
		range /= stride;
	}

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
			       const struct legacy_surf_level *base_level_info,
			       unsigned base_level, unsigned first_level,
			       unsigned block_width, bool is_stencil,
			       bool is_storage_image, uint32_t *state)
{
	uint64_t gpu_address = image->bo ? radv_buffer_get_va(image->bo) + image->offset : 0;
	uint64_t va = gpu_address;
	enum chip_class chip_class = device->physical_device->rad_info.chip_class;
	uint64_t meta_va = 0;
	if (chip_class >= GFX9) {
		if (is_stencil)
			va += image->surface.u.gfx9.stencil_offset;
		else
			va += image->surface.u.gfx9.surf_offset;
	} else
		va += base_level_info->offset;

	state[0] = va >> 8;
	if (chip_class >= GFX9 ||
	    base_level_info->mode == RADEON_SURF_MODE_2D)
		state[0] |= image->surface.tile_swizzle;
	state[1] &= C_008F14_BASE_ADDRESS_HI;
	state[1] |= S_008F14_BASE_ADDRESS_HI(va >> 40);

	if (chip_class >= VI) {
		state[6] &= C_008F28_COMPRESSION_EN;
		state[7] = 0;
		if (!is_storage_image && radv_vi_dcc_enabled(image, first_level)) {
			meta_va = gpu_address + image->dcc_offset;
			if (chip_class <= VI)
				meta_va += base_level_info->dcc_offset;
		} else if(!is_storage_image && image->tc_compatible_htile &&
		          image->surface.htile_size) {
			meta_va = gpu_address + image->htile_offset;
		}

		if (meta_va) {
			state[6] |= S_008F28_COMPRESSION_EN(1);
			state[7] = meta_va >> 8;
			state[7] |= image->surface.tile_swizzle;
		}
	}

	if (chip_class >= GFX9) {
		state[3] &= C_008F1C_SW_MODE;
		state[4] &= C_008F20_PITCH_GFX9;

		if (is_stencil) {
			state[3] |= S_008F1C_SW_MODE(image->surface.u.gfx9.stencil.swizzle_mode);
			state[4] |= S_008F20_PITCH_GFX9(image->surface.u.gfx9.stencil.epitch);
		} else {
			state[3] |= S_008F1C_SW_MODE(image->surface.u.gfx9.surf.swizzle_mode);
			state[4] |= S_008F20_PITCH_GFX9(image->surface.u.gfx9.surf.epitch);
		}

		state[5] &= C_008F24_META_DATA_ADDRESS &
			    C_008F24_META_PIPE_ALIGNED &
			    C_008F24_META_RB_ALIGNED;
		if (meta_va) {
			struct gfx9_surf_meta_flags meta;

			if (image->dcc_offset)
				meta = image->surface.u.gfx9.dcc;
			else
				meta = image->surface.u.gfx9.htile;

			state[5] |= S_008F24_META_DATA_ADDRESS(meta_va >> 40) |
				    S_008F24_META_PIPE_ALIGNED(meta.pipe_aligned) |
				    S_008F24_META_RB_ALIGNED(meta.rb_aligned);
		}
	} else {
		/* SI-CI-VI */
		unsigned pitch = base_level_info->nblk_x * block_width;
		unsigned index = si_tile_mode_index(image, base_level, is_stencil);

		state[3] &= C_008F1C_TILING_INDEX;
		state[3] |= S_008F1C_TILING_INDEX(index);
		state[4] &= C_008F20_PITCH_GFX6;
		state[4] |= S_008F20_PITCH_GFX6(pitch - 1);
	}
}

static unsigned radv_tex_dim(VkImageType image_type, VkImageViewType view_type,
			     unsigned nr_layers, unsigned nr_samples, bool is_storage_image, bool gfx9)
{
	if (view_type == VK_IMAGE_VIEW_TYPE_CUBE || view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
		return is_storage_image ? V_008F1C_SQ_RSRC_IMG_2D_ARRAY : V_008F1C_SQ_RSRC_IMG_CUBE;

	/* GFX9 allocates 1D textures as 2D. */
	if (gfx9 && image_type == VK_IMAGE_TYPE_1D)
		image_type = VK_IMAGE_TYPE_2D;
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

static unsigned gfx9_border_color_swizzle(const enum vk_swizzle swizzle[4])
{
	unsigned bc_swizzle = V_008F20_BC_SWIZZLE_XYZW;

	if (swizzle[3] == VK_SWIZZLE_X) {
		/* For the pre-defined border color values (white, opaque
		 * black, transparent black), the only thing that matters is
		 * that the alpha channel winds up in the correct place
		 * (because the RGB channels are all the same) so either of
		 * these enumerations will work.
		 */
		if (swizzle[2] == VK_SWIZZLE_Y)
			bc_swizzle = V_008F20_BC_SWIZZLE_WZYX;
		else
			bc_swizzle = V_008F20_BC_SWIZZLE_WXYZ;
	} else if (swizzle[0] == VK_SWIZZLE_X) {
		if (swizzle[1] == VK_SWIZZLE_Y)
			bc_swizzle = V_008F20_BC_SWIZZLE_XYZW;
		else
			bc_swizzle = V_008F20_BC_SWIZZLE_XWYZ;
	} else if (swizzle[1] == VK_SWIZZLE_X) {
		bc_swizzle = V_008F20_BC_SWIZZLE_YXWZ;
	} else if (swizzle[2] == VK_SWIZZLE_X) {
		bc_swizzle = V_008F20_BC_SWIZZLE_ZYXW;
	}

	return bc_swizzle;
}

/**
 * Build the sampler view descriptor for a texture.
 */
static void
si_make_texture_descriptor(struct radv_device *device,
			   struct radv_image *image,
			   bool is_storage_image,
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

	/* S8 with either Z16 or Z32 HTILE need a special format. */
	if (device->physical_device->rad_info.chip_class >= GFX9 &&
	    vk_format == VK_FORMAT_S8_UINT &&
	    image->tc_compatible_htile) {
		if (image->vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT)
			data_format = V_008F14_IMG_DATA_FORMAT_S8_32;
		else if (image->vk_format == VK_FORMAT_D16_UNORM_S8_UINT)
			data_format = V_008F14_IMG_DATA_FORMAT_S8_16;
	}
	type = radv_tex_dim(image->type, view_type, image->info.array_size, image->info.samples,
			    is_storage_image, device->physical_device->rad_info.chip_class >= GFX9);
	if (type == V_008F1C_SQ_RSRC_IMG_1D_ARRAY) {
	        height = 1;
		depth = image->info.array_size;
	} else if (type == V_008F1C_SQ_RSRC_IMG_2D_ARRAY ||
		   type == V_008F1C_SQ_RSRC_IMG_2D_MSAA_ARRAY) {
		if (view_type != VK_IMAGE_VIEW_TYPE_3D)
			depth = image->info.array_size;
	} else if (type == V_008F1C_SQ_RSRC_IMG_CUBE)
		depth = image->info.array_size / 6;

	state[0] = 0;
	state[1] = (S_008F14_DATA_FORMAT_GFX6(data_format) |
		    S_008F14_NUM_FORMAT_GFX6(num_format));
	state[2] = (S_008F18_WIDTH(width - 1) |
		    S_008F18_HEIGHT(height - 1) |
		    S_008F18_PERF_MOD(4));
	state[3] = (S_008F1C_DST_SEL_X(radv_map_swizzle(swizzle[0])) |
		    S_008F1C_DST_SEL_Y(radv_map_swizzle(swizzle[1])) |
		    S_008F1C_DST_SEL_Z(radv_map_swizzle(swizzle[2])) |
		    S_008F1C_DST_SEL_W(radv_map_swizzle(swizzle[3])) |
		    S_008F1C_BASE_LEVEL(image->info.samples > 1 ?
					0 : first_level) |
		    S_008F1C_LAST_LEVEL(image->info.samples > 1 ?
					util_logbase2(image->info.samples) :
					last_level) |
		    S_008F1C_TYPE(type));
	state[4] = 0;
	state[5] = S_008F24_BASE_ARRAY(first_layer);
	state[6] = 0;
	state[7] = 0;

	if (device->physical_device->rad_info.chip_class >= GFX9) {
		unsigned bc_swizzle = gfx9_border_color_swizzle(swizzle);

		/* Depth is the the last accessible layer on Gfx9.
		 * The hw doesn't need to know the total number of layers.
		 */
		if (type == V_008F1C_SQ_RSRC_IMG_3D)
			state[4] |= S_008F20_DEPTH(depth - 1);
		else
			state[4] |= S_008F20_DEPTH(last_layer);

		state[4] |= S_008F20_BC_SWIZZLE(bc_swizzle);
		state[5] |= S_008F24_MAX_MIP(image->info.samples > 1 ?
					     util_logbase2(image->info.samples) :
					     image->info.levels - 1);
	} else {
		state[3] |= S_008F1C_POW2_PAD(image->info.levels > 1);
		state[4] |= S_008F20_DEPTH(depth - 1);
		state[5] |= S_008F24_LAST_ARRAY(last_layer);
	}
	if (image->dcc_offset) {
		unsigned swap = radv_translate_colorswap(vk_format, FALSE);

		state[6] = S_008F28_ALPHA_IS_ON_MSB(swap <= 1);
	} else {
		/* The last dword is unused by hw. The shader uses it to clear
		 * bits in the first dword of sampler state.
		 */
		if (device->physical_device->rad_info.chip_class <= CIK && image->info.samples <= 1) {
			if (first_level == last_level)
				state[7] = C_008F30_MAX_ANISO_RATIO;
			else
				state[7] = 0xffffffff;
		}
	}

	/* Initialize the sampler view for FMASK. */
	if (image->fmask.size) {
		uint32_t fmask_format, num_format;
		uint64_t gpu_address = radv_buffer_get_va(image->bo);
		uint64_t va;

		va = gpu_address + image->offset + image->fmask.offset;

		if (device->physical_device->rad_info.chip_class >= GFX9) {
			fmask_format = V_008F14_IMG_DATA_FORMAT_FMASK;
			switch (image->info.samples) {
			case 2:
				num_format = V_008F14_IMG_FMASK_8_2_2;
				break;
			case 4:
				num_format = V_008F14_IMG_FMASK_8_4_4;
				break;
			case 8:
				num_format = V_008F14_IMG_FMASK_32_8_8;
				break;
			default:
				unreachable("invalid nr_samples");
			}
		} else {
			switch (image->info.samples) {
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
			num_format = V_008F14_IMG_NUM_FORMAT_UINT;
		}

		fmask_state[0] = va >> 8;
		fmask_state[0] |= image->fmask.tile_swizzle;
		fmask_state[1] = S_008F14_BASE_ADDRESS_HI(va >> 40) |
			S_008F14_DATA_FORMAT_GFX6(fmask_format) |
			S_008F14_NUM_FORMAT_GFX6(num_format);
		fmask_state[2] = S_008F18_WIDTH(width - 1) |
			S_008F18_HEIGHT(height - 1);
		fmask_state[3] = S_008F1C_DST_SEL_X(V_008F1C_SQ_SEL_X) |
			S_008F1C_DST_SEL_Y(V_008F1C_SQ_SEL_X) |
			S_008F1C_DST_SEL_Z(V_008F1C_SQ_SEL_X) |
			S_008F1C_DST_SEL_W(V_008F1C_SQ_SEL_X) |
			S_008F1C_TYPE(radv_tex_dim(image->type, view_type, 1, 0, false, false));
		fmask_state[4] = 0;
		fmask_state[5] = S_008F24_BASE_ARRAY(first_layer);
		fmask_state[6] = 0;
		fmask_state[7] = 0;

		if (device->physical_device->rad_info.chip_class >= GFX9) {
			fmask_state[3] |= S_008F1C_SW_MODE(image->surface.u.gfx9.fmask.swizzle_mode);
			fmask_state[4] |= S_008F20_DEPTH(last_layer) |
					  S_008F20_PITCH_GFX9(image->surface.u.gfx9.fmask.epitch);
			fmask_state[5] |= S_008F24_META_PIPE_ALIGNED(image->surface.u.gfx9.cmask.pipe_aligned) |
					  S_008F24_META_RB_ALIGNED(image->surface.u.gfx9.cmask.rb_aligned);
		} else {
			fmask_state[3] |= S_008F1C_TILING_INDEX(image->fmask.tile_mode_index);
			fmask_state[4] |= S_008F20_DEPTH(depth - 1) |
				S_008F20_PITCH_GFX6(image->fmask.pitch_in_pixels - 1);
			fmask_state[5] |= S_008F24_LAST_ARRAY(last_layer);
		}
	} else if (fmask_state)
		memset(fmask_state, 0, 8 * 4);
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


	si_make_texture_descriptor(device, image, false,
				   (VkImageViewType)image->type, image->vk_format,
				   &fixedmapping, 0, image->info.levels - 1, 0,
				   image->info.array_size,
				   image->info.width, image->info.height,
				   image->info.depth,
				   desc, NULL);

	si_set_mutable_tex_desc_fields(device, image, &image->surface.u.legacy.level[0], 0, 0,
				       image->surface.blk_w, false, false, desc);

	/* Clear the base address and set the relative DCC offset. */
	desc[0] = 0;
	desc[1] &= C_008F14_BASE_ADDRESS_HI;
	desc[7] = image->dcc_offset >> 8;

	/* Dwords [2:9] contain the image descriptor. */
	memcpy(&md->metadata[2], desc, sizeof(desc));

	/* Dwords [10:..] contain the mipmap level offsets. */
	if (device->physical_device->rad_info.chip_class <= VI) {
		for (i = 0; i <= image->info.levels - 1; i++)
			md->metadata[10+i] = image->surface.u.legacy.level[i].offset >> 8;
		md->size_metadata = (11 + image->info.levels - 1) * 4;
	}
}

void
radv_init_metadata(struct radv_device *device,
		   struct radv_image *image,
		   struct radeon_bo_metadata *metadata)
{
	struct radeon_surf *surface = &image->surface;

	memset(metadata, 0, sizeof(*metadata));

	if (device->physical_device->rad_info.chip_class >= GFX9) {
		metadata->u.gfx9.swizzle_mode = surface->u.gfx9.surf.swizzle_mode;
	} else {
		metadata->u.legacy.microtile = surface->u.legacy.level[0].mode >= RADEON_SURF_MODE_1D ?
			RADEON_LAYOUT_TILED : RADEON_LAYOUT_LINEAR;
		metadata->u.legacy.macrotile = surface->u.legacy.level[0].mode >= RADEON_SURF_MODE_2D ?
			RADEON_LAYOUT_TILED : RADEON_LAYOUT_LINEAR;
		metadata->u.legacy.pipe_config = surface->u.legacy.pipe_config;
		metadata->u.legacy.bankw = surface->u.legacy.bankw;
		metadata->u.legacy.bankh = surface->u.legacy.bankh;
		metadata->u.legacy.tile_split = surface->u.legacy.tile_split;
		metadata->u.legacy.mtilea = surface->u.legacy.mtilea;
		metadata->u.legacy.num_banks = surface->u.legacy.num_banks;
		metadata->u.legacy.stride = surface->u.legacy.level[0].nblk_x * surface->bpe;
		metadata->u.legacy.scanout = (surface->flags & RADEON_SURF_SCANOUT) != 0;
	}
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
	struct radeon_surf fmask = {};
	struct ac_surf_info info = image->info;
	memset(out, 0, sizeof(*out));

	if (device->physical_device->rad_info.chip_class >= GFX9) {
		out->alignment = image->surface.u.gfx9.fmask_alignment;
		out->size = image->surface.u.gfx9.fmask_size;
		return;
	}

	fmask.blk_w = image->surface.blk_w;
	fmask.blk_h = image->surface.blk_h;
	info.samples = 1;
	fmask.flags = image->surface.flags | RADEON_SURF_FMASK;

	if (!image->shareable)
		info.surf_index = &device->fmask_mrt_offset_counter;

	/* Force 2D tiling if it wasn't set. This may occur when creating
	 * FMASK for MSAA resolve on R6xx. On R6xx, the single-sample
	 * destination buffer must have an FMASK too. */
	fmask.flags = RADEON_SURF_CLR(fmask.flags, MODE);
	fmask.flags |= RADEON_SURF_SET(RADEON_SURF_MODE_2D, MODE);

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

	device->ws->surface_init(device->ws, &info, &fmask);
	assert(fmask.u.legacy.level[0].mode == RADEON_SURF_MODE_2D);

	out->slice_tile_max = (fmask.u.legacy.level[0].nblk_x * fmask.u.legacy.level[0].nblk_y) / 64;
	if (out->slice_tile_max)
		out->slice_tile_max -= 1;

	out->tile_mode_index = fmask.u.legacy.tiling_index[0];
	out->pitch_in_pixels = fmask.u.legacy.level[0].nblk_x;
	out->bank_height = fmask.u.legacy.bankh;
	out->tile_swizzle = fmask.tile_swizzle;
	out->alignment = MAX2(256, fmask.surf_alignment);
	out->size = fmask.surf_size;

	assert(!out->tile_swizzle || !image->shareable);
}

static void
radv_image_alloc_fmask(struct radv_device *device,
		       struct radv_image *image)
{
	radv_image_get_fmask_info(device, image, image->info.samples, &image->fmask);

	image->fmask.offset = align64(image->size, image->fmask.alignment);
	image->size = image->fmask.offset + image->fmask.size;
	image->alignment = MAX2(image->alignment, image->fmask.alignment);
}

static void
radv_image_get_cmask_info(struct radv_device *device,
			  struct radv_image *image,
			  struct radv_cmask_info *out)
{
	unsigned pipe_interleave_bytes = device->physical_device->rad_info.pipe_interleave_bytes;
	unsigned num_pipes = device->physical_device->rad_info.num_tile_pipes;
	unsigned cl_width, cl_height;

	if (device->physical_device->rad_info.chip_class >= GFX9) {
		out->alignment = image->surface.u.gfx9.cmask_alignment;
		out->size = image->surface.u.gfx9.cmask_size;
		return;
	}

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

	unsigned width = align(image->info.width, cl_width*8);
	unsigned height = align(image->info.height, cl_height*8);
	unsigned slice_elements = (width * height) / (8*8);

	/* Each element of CMASK is a nibble. */
	unsigned slice_bytes = slice_elements / 2;

	out->slice_tile_max = (width * height) / (128*128);
	if (out->slice_tile_max)
		out->slice_tile_max -= 1;

	out->alignment = MAX2(256, base_align);
	out->size = (image->type == VK_IMAGE_TYPE_3D ? image->info.depth : image->info.array_size) *
		    align(slice_bytes, base_align);
}

static void
radv_image_alloc_cmask(struct radv_device *device,
		       struct radv_image *image)
{
	uint32_t clear_value_size = 0;
	radv_image_get_cmask_info(device, image, &image->cmask);

	image->cmask.offset = align64(image->size, image->cmask.alignment);
	/* + 8 for storing the clear values */
	if (!image->clear_value_offset) {
		image->clear_value_offset = image->cmask.offset + image->cmask.size;
		clear_value_size = 8;
	}
	image->size = image->cmask.offset + image->cmask.size + clear_value_size;
	image->alignment = MAX2(image->alignment, image->cmask.alignment);
}

static void
radv_image_alloc_dcc(struct radv_image *image)
{
	image->dcc_offset = align64(image->size, image->surface.dcc_alignment);
	/* + 16 for storing the clear values + dcc pred */
	image->clear_value_offset = image->dcc_offset + image->surface.dcc_size;
	image->dcc_pred_offset = image->clear_value_offset + 8;
	image->size = image->dcc_offset + image->surface.dcc_size + 16;
	image->alignment = MAX2(image->alignment, image->surface.dcc_alignment);
}

static void
radv_image_alloc_htile(struct radv_image *image)
{
	image->htile_offset = align64(image->size, image->surface.htile_alignment);

	/* + 8 for storing the clear values */
	image->clear_value_offset = image->htile_offset + image->surface.htile_size;
	image->size = image->clear_value_offset + 8;
	image->alignment = align64(image->alignment, image->surface.htile_alignment);
}

static inline bool
radv_image_can_enable_dcc_or_cmask(struct radv_image *image)
{
	if (image->info.samples <= 1 &&
	    image->info.width * image->info.height <= 512 * 512) {
		/* Do not enable CMASK or DCC for small surfaces where the cost
		 * of the eliminate pass can be higher than the benefit of fast
		 * clear. RadeonSI does this, but the image threshold is
		 * different.
		 */
		return false;
	}

	return image->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT &&
	       (image->exclusive || image->queue_family_mask == 1);
}

static inline bool
radv_image_can_enable_dcc(struct radv_image *image)
{
	return radv_image_can_enable_dcc_or_cmask(image) &&
	       image->surface.dcc_size;
}

static inline bool
radv_image_can_enable_cmask(struct radv_image *image)
{
	if (image->surface.bpe > 8 && image->info.samples == 1) {
		/* Do not enable CMASK for non-MSAA images (fast color clear)
		 * because 128 bit formats are not supported, but FMASK might
		 * still be used.
		 */
		return false;
	}

	return radv_image_can_enable_dcc_or_cmask(image) &&
	       image->info.levels == 1 &&
	       image->info.depth == 1 &&
	       !image->surface.is_linear;
}

static inline bool
radv_image_can_enable_fmask(struct radv_image *image)
{
	return image->info.samples > 1 && vk_format_is_color(image->vk_format);
}

static inline bool
radv_image_can_enable_htile(struct radv_image *image)
{
	return image->info.levels == 1 && vk_format_is_depth(image->vk_format);
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
	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);

	radv_assert(pCreateInfo->mipLevels > 0);
	radv_assert(pCreateInfo->arrayLayers > 0);
	radv_assert(pCreateInfo->samples > 0);
	radv_assert(pCreateInfo->extent.width > 0);
	radv_assert(pCreateInfo->extent.height > 0);
	radv_assert(pCreateInfo->extent.depth > 0);

	image = vk_zalloc2(&device->alloc, alloc, sizeof(*image), 8,
			   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (!image)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	image->type = pCreateInfo->imageType;
	image->info.width = pCreateInfo->extent.width;
	image->info.height = pCreateInfo->extent.height;
	image->info.depth = pCreateInfo->extent.depth;
	image->info.samples = pCreateInfo->samples;
	image->info.array_size = pCreateInfo->arrayLayers;
	image->info.levels = pCreateInfo->mipLevels;

	image->vk_format = pCreateInfo->format;
	image->tiling = pCreateInfo->tiling;
	image->usage = pCreateInfo->usage;
	image->flags = pCreateInfo->flags;

	image->exclusive = pCreateInfo->sharingMode == VK_SHARING_MODE_EXCLUSIVE;
	if (pCreateInfo->sharingMode == VK_SHARING_MODE_CONCURRENT) {
		for (uint32_t i = 0; i < pCreateInfo->queueFamilyIndexCount; ++i)
			if (pCreateInfo->pQueueFamilyIndices[i] == VK_QUEUE_FAMILY_EXTERNAL_KHR)
				image->queue_family_mask |= (1u << RADV_MAX_QUEUE_FAMILIES) - 1u;
			else
				image->queue_family_mask |= 1u << pCreateInfo->pQueueFamilyIndices[i];
	}

	image->shareable = vk_find_struct_const(pCreateInfo->pNext,
	                                        EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR) != NULL;
	if (!vk_format_is_depth(pCreateInfo->format) && !create_info->scanout && !image->shareable) {
		image->info.surf_index = &device->image_mrt_offset_counter;
	}

	radv_init_surface(device, &image->surface, create_info);

	device->ws->surface_init(device->ws, &image->info, &image->surface);

	image->size = image->surface.surf_size;
	image->alignment = image->surface.surf_alignment;

	if (!create_info->no_metadata_planes) {
		/* Try to enable DCC first. */
		if (radv_image_can_enable_dcc(image)) {
			radv_image_alloc_dcc(image);
		} else {
			/* When DCC cannot be enabled, try CMASK. */
			image->surface.dcc_size = 0;
			if (radv_image_can_enable_cmask(image)) {
				radv_image_alloc_cmask(device, image);
			}
		}

		/* Try to enable FMASK for multisampled images. */
		if (radv_image_can_enable_fmask(image)) {
			radv_image_alloc_fmask(device, image);
		} else {
			/* Otherwise, try to enable HTILE for depth surfaces. */
			if (radv_image_can_enable_htile(image) &&
			    !(device->instance->debug_flags & RADV_DEBUG_NO_HIZ)) {
				radv_image_alloc_htile(image);
				image->tc_compatible_htile = image->surface.flags & RADEON_SURF_TC_COMPATIBLE_HTILE;
			} else {
				image->surface.htile_size = 0;
			}
		}
	} else {
		image->surface.dcc_size = 0;
		image->surface.htile_size = 0;
	}

	if (pCreateInfo->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) {
		image->alignment = MAX2(image->alignment, 4096);
		image->size = align64(image->size, image->alignment);
		image->offset = 0;

		image->bo = device->ws->buffer_create(device->ws, image->size, image->alignment,
		                                      0, RADEON_FLAG_VIRTUAL);
		if (!image->bo) {
			vk_free2(&device->alloc, alloc, image);
			return vk_error(VK_ERROR_OUT_OF_DEVICE_MEMORY);
		}
	}

	*pImage = radv_image_to_handle(image);

	return VK_SUCCESS;
}

static void
radv_image_view_make_descriptor(struct radv_image_view *iview,
				struct radv_device *device,
				const VkComponentMapping *components,
				bool is_storage_image)
{
	struct radv_image *image = iview->image;
	bool is_stencil = iview->aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT;
	uint32_t blk_w;
	uint32_t *descriptor;
	uint32_t hw_level = 0;

	if (is_storage_image) {
		descriptor = iview->storage_descriptor;
	} else {
		descriptor = iview->descriptor;
	}

	assert(image->surface.blk_w % vk_format_get_blockwidth(image->vk_format) == 0);
	blk_w = image->surface.blk_w / vk_format_get_blockwidth(image->vk_format) * vk_format_get_blockwidth(iview->vk_format);

	if (device->physical_device->rad_info.chip_class >= GFX9)
		hw_level = iview->base_mip;
	si_make_texture_descriptor(device, image, is_storage_image,
				   iview->type,
				   iview->vk_format,
				   components,
				   hw_level, hw_level + iview->level_count - 1,
				   iview->base_layer,
				   iview->base_layer + iview->layer_count - 1,
				   iview->extent.width,
				   iview->extent.height,
				   iview->extent.depth,
				   descriptor,
				   descriptor + 8);

	const struct legacy_surf_level *base_level_info = NULL;
	if (device->physical_device->rad_info.chip_class <= GFX9) {
		if (is_stencil)
			base_level_info = &image->surface.u.legacy.stencil_level[iview->base_mip];
		else
			base_level_info = &image->surface.u.legacy.level[iview->base_mip];
	}
	si_set_mutable_tex_desc_fields(device, image,
				       base_level_info,
				       iview->base_mip,
				       iview->base_mip,
				       blk_w, is_stencil, is_storage_image, descriptor);
}

void
radv_image_view_init(struct radv_image_view *iview,
		     struct radv_device *device,
		     const VkImageViewCreateInfo* pCreateInfo)
{
	RADV_FROM_HANDLE(radv_image, image, pCreateInfo->image);
	const VkImageSubresourceRange *range = &pCreateInfo->subresourceRange;

	switch (image->type) {
	case VK_IMAGE_TYPE_1D:
	case VK_IMAGE_TYPE_2D:
		assert(range->baseArrayLayer + radv_get_layerCount(image, range) - 1 <= image->info.array_size);
		break;
	case VK_IMAGE_TYPE_3D:
		assert(range->baseArrayLayer + radv_get_layerCount(image, range) - 1
		       <= radv_minify(image->info.depth, range->baseMipLevel));
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
		iview->vk_format = vk_format_stencil_only(iview->vk_format);
	} else if (iview->aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT) {
		iview->vk_format = vk_format_depth_only(iview->vk_format);
	}

	if (device->physical_device->rad_info.chip_class >= GFX9) {
		iview->extent = (VkExtent3D) {
			.width = image->info.width,
			.height = image->info.height,
			.depth = image->info.depth,
		};
	} else {
		iview->extent = (VkExtent3D) {
			.width  = radv_minify(image->info.width , range->baseMipLevel),
			.height = radv_minify(image->info.height, range->baseMipLevel),
			.depth  = radv_minify(image->info.depth , range->baseMipLevel),
		};
	}

	if (iview->vk_format != image->vk_format) {
		unsigned view_bw = vk_format_get_blockwidth(iview->vk_format);
		unsigned view_bh = vk_format_get_blockheight(iview->vk_format);
		unsigned img_bw = vk_format_get_blockwidth(image->vk_format);
		unsigned img_bh = vk_format_get_blockheight(image->vk_format);

		iview->extent.width = round_up_u32(iview->extent.width * view_bw, img_bw);
		iview->extent.height = round_up_u32(iview->extent.height * view_bh, img_bh);

		/* Comment ported from amdvlk -
		 * If we have the following image:
		 *              Uncompressed pixels   Compressed block sizes (4x4)
		 *      mip0:       22 x 22                   6 x 6
		 *      mip1:       11 x 11                   3 x 3
		 *      mip2:        5 x  5                   2 x 2
		 *      mip3:        2 x  2                   1 x 1
		 *      mip4:        1 x  1                   1 x 1
		 *
		 * On GFX9 the descriptor is always programmed with the WIDTH and HEIGHT of the base level and the HW is
		 * calculating the degradation of the block sizes down the mip-chain as follows (straight-up
		 * divide-by-two integer math):
		 *      mip0:  6x6
		 *      mip1:  3x3
		 *      mip2:  1x1
		 *      mip3:  1x1
		 *
		 * This means that mip2 will be missing texels.
		 *
		 * Fix this by calculating the base mip's width and height, then convert that, and round it
		 * back up to get the level 0 size.
		 * Clamp the converted size between the original values, and next power of two, which
		 * means we don't oversize the image.
		 */
		 if (device->physical_device->rad_info.chip_class >= GFX9 &&
		     vk_format_is_compressed(image->vk_format) &&
		     !vk_format_is_compressed(iview->vk_format)) {
			 unsigned rounded_img_w = util_next_power_of_two(iview->extent.width);
			 unsigned rounded_img_h = util_next_power_of_two(iview->extent.height);
			 unsigned lvl_width  = radv_minify(image->info.width , range->baseMipLevel);
			 unsigned lvl_height = radv_minify(image->info.height, range->baseMipLevel);

			 lvl_width = round_up_u32(lvl_width * view_bw, img_bw);
			 lvl_height = round_up_u32(lvl_height * view_bh, img_bh);

			 lvl_width <<= range->baseMipLevel;
			 lvl_height <<= range->baseMipLevel;

			 iview->extent.width = CLAMP(lvl_width, iview->extent.width, rounded_img_w);
			 iview->extent.height = CLAMP(lvl_height, iview->extent.height, rounded_img_h);
		 }
	}

	iview->base_layer = range->baseArrayLayer;
	iview->layer_count = radv_get_layerCount(image, range);
	iview->base_mip = range->baseMipLevel;
	iview->level_count = radv_get_levelCount(image, range);

	radv_image_view_make_descriptor(iview, device, &pCreateInfo->components, false);
	radv_image_view_make_descriptor(iview, device, &pCreateInfo->components, true);
}

bool radv_layout_has_htile(const struct radv_image *image,
                           VkImageLayout layout,
                           unsigned queue_mask)
{
	if (image->surface.htile_size && image->tc_compatible_htile)
		return layout != VK_IMAGE_LAYOUT_GENERAL;

	return image->surface.htile_size &&
	       (layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
	        layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) &&
	       queue_mask == (1u << RADV_QUEUE_GENERAL);
}

bool radv_layout_is_htile_compressed(const struct radv_image *image,
                                     VkImageLayout layout,
                                     unsigned queue_mask)
{
	if (image->surface.htile_size && image->tc_compatible_htile)
		return layout != VK_IMAGE_LAYOUT_GENERAL;

	return image->surface.htile_size &&
	       (layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
	        layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) &&
	       queue_mask == (1u << RADV_QUEUE_GENERAL);
}

bool radv_layout_can_fast_clear(const struct radv_image *image,
			        VkImageLayout layout,
			        unsigned queue_mask)
{
	return layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
		queue_mask == (1u << RADV_QUEUE_GENERAL);
}

bool radv_layout_dcc_compressed(const struct radv_image *image,
			        VkImageLayout layout,
			        unsigned queue_mask)
{
	/* Don't compress compute transfer dst, as image stores are not supported. */
	if (layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
	    (queue_mask & (1u << RADV_QUEUE_COMPUTE)))
		return false;

	return image->surface.num_dcc_levels > 0 && layout != VK_IMAGE_LAYOUT_GENERAL;
}


unsigned radv_image_queue_family_mask(const struct radv_image *image, uint32_t family, uint32_t queue_family)
{
	if (!image->exclusive)
		return image->queue_family_mask;
	if (family == VK_QUEUE_FAMILY_EXTERNAL_KHR)
		return (1u << RADV_MAX_QUEUE_FAMILIES) - 1u;
	if (family == VK_QUEUE_FAMILY_IGNORED)
		return 1u << queue_family;
	return 1u << family;
}

VkResult
radv_CreateImage(VkDevice device,
		 const VkImageCreateInfo *pCreateInfo,
		 const VkAllocationCallbacks *pAllocator,
		 VkImage *pImage)
{
#ifdef ANDROID
	const VkNativeBufferANDROID *gralloc_info =
		vk_find_struct_const(pCreateInfo->pNext, NATIVE_BUFFER_ANDROID);

	if (gralloc_info)
		return radv_image_from_gralloc(device, pCreateInfo, gralloc_info,
		                              pAllocator, pImage);
#endif

	const struct wsi_image_create_info *wsi_info =
		vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);
	bool scanout = wsi_info && wsi_info->scanout;

	return radv_image_create(device,
				 &(struct radv_image_create_info) {
					 .vk_info = pCreateInfo,
					 .scanout = scanout,
				 },
				 pAllocator,
				 pImage);
}

void
radv_DestroyImage(VkDevice _device, VkImage _image,
		  const VkAllocationCallbacks *pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_image, image, _image);

	if (!image)
		return;

	if (image->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT)
		device->ws->buffer_destroy(image->bo);

	if (image->owned_memory != VK_NULL_HANDLE)
		radv_FreeMemory(_device, image->owned_memory, pAllocator);

	vk_free2(&device->alloc, pAllocator, image);
}

void radv_GetImageSubresourceLayout(
	VkDevice                                    _device,
	VkImage                                     _image,
	const VkImageSubresource*                   pSubresource,
	VkSubresourceLayout*                        pLayout)
{
	RADV_FROM_HANDLE(radv_image, image, _image);
	RADV_FROM_HANDLE(radv_device, device, _device);
	int level = pSubresource->mipLevel;
	int layer = pSubresource->arrayLayer;
	struct radeon_surf *surface = &image->surface;

	if (device->physical_device->rad_info.chip_class >= GFX9) {
		pLayout->offset = surface->u.gfx9.offset[level] + surface->u.gfx9.surf_slice_size * layer;
		pLayout->rowPitch = surface->u.gfx9.surf_pitch * surface->bpe;
		pLayout->arrayPitch = surface->u.gfx9.surf_slice_size;
		pLayout->depthPitch = surface->u.gfx9.surf_slice_size;
		pLayout->size = surface->u.gfx9.surf_slice_size;
		if (image->type == VK_IMAGE_TYPE_3D)
			pLayout->size *= u_minify(image->info.depth, level);
	} else {
		pLayout->offset = surface->u.legacy.level[level].offset + (uint64_t)surface->u.legacy.level[level].slice_size_dw * 4 * layer;
		pLayout->rowPitch = surface->u.legacy.level[level].nblk_x * surface->bpe;
		pLayout->arrayPitch = (uint64_t)surface->u.legacy.level[level].slice_size_dw * 4;
		pLayout->depthPitch = (uint64_t)surface->u.legacy.level[level].slice_size_dw * 4;
		pLayout->size = (uint64_t)surface->u.legacy.level[level].slice_size_dw * 4;
		if (image->type == VK_IMAGE_TYPE_3D)
			pLayout->size *= u_minify(image->info.depth, level);
	}
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

	radv_image_view_init(view, device, pCreateInfo);

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
			   const VkBufferViewCreateInfo* pCreateInfo)
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

	radv_buffer_view_init(view, device, pCreateInfo);

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
