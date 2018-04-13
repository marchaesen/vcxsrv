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

#include "util/mesa-sha1.h"
#include "util/u_atomic.h"
#include "radv_debug.h"
#include "radv_private.h"
#include "radv_cs.h"
#include "radv_shader.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "spirv/nir_spirv.h"
#include "vk_util.h"

#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>

#include "sid.h"
#include "gfx9d.h"
#include "ac_binary.h"
#include "ac_llvm_util.h"
#include "ac_nir_to_llvm.h"
#include "vk_format.h"
#include "util/debug.h"
#include "ac_exp_param.h"
#include "ac_shader_util.h"
#include "main/menums.h"

struct radv_blend_state {
	uint32_t blend_enable_4bit;
	uint32_t need_src_alpha;

	uint32_t cb_color_control;
	uint32_t cb_target_mask;
	uint32_t cb_target_enabled_4bit;
	uint32_t sx_mrt_blend_opt[8];
	uint32_t cb_blend_control[8];

	uint32_t spi_shader_col_format;
	uint32_t cb_shader_mask;
	uint32_t db_alpha_to_mask;

	uint32_t commutative_4bit;

	bool single_cb_enable;
	bool mrt0_is_dual_src;
};

struct radv_dsa_order_invariance {
	/* Whether the final result in Z/S buffers is guaranteed to be
	 * invariant under changes to the order in which fragments arrive.
	 */
	bool zs;

	/* Whether the set of fragments that pass the combined Z/S test is
	 * guaranteed to be invariant under changes to the order in which
	 * fragments arrive.
	 */
	bool pass_set;
};

struct radv_tessellation_state {
	uint32_t ls_hs_config;
	unsigned num_patches;
	unsigned lds_size;
	uint32_t tf_param;
};

struct radv_gs_state {
	uint32_t vgt_gs_onchip_cntl;
	uint32_t vgt_gs_max_prims_per_subgroup;
	uint32_t vgt_esgs_ring_itemsize;
	uint32_t lds_size;
};

static void
radv_pipeline_destroy(struct radv_device *device,
                      struct radv_pipeline *pipeline,
                      const VkAllocationCallbacks* allocator)
{
	for (unsigned i = 0; i < MESA_SHADER_STAGES; ++i)
		if (pipeline->shaders[i])
			radv_shader_variant_destroy(device, pipeline->shaders[i]);

	if (pipeline->gs_copy_shader)
		radv_shader_variant_destroy(device, pipeline->gs_copy_shader);

	if(pipeline->cs.buf)
		free(pipeline->cs.buf);
	vk_free2(&device->alloc, allocator, pipeline);
}

void radv_DestroyPipeline(
	VkDevice                                    _device,
	VkPipeline                                  _pipeline,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);

	if (!_pipeline)
		return;

	radv_pipeline_destroy(device, pipeline, pAllocator);
}

static uint32_t get_hash_flags(struct radv_device *device)
{
	uint32_t hash_flags = 0;

	if (device->instance->debug_flags & RADV_DEBUG_UNSAFE_MATH)
		hash_flags |= RADV_HASH_SHADER_UNSAFE_MATH;
	if (device->instance->perftest_flags & RADV_PERFTEST_SISCHED)
		hash_flags |= RADV_HASH_SHADER_SISCHED;
	return hash_flags;
}

static VkResult
radv_pipeline_scratch_init(struct radv_device *device,
                           struct radv_pipeline *pipeline)
{
	unsigned scratch_bytes_per_wave = 0;
	unsigned max_waves = 0;
	unsigned min_waves = 1;

	for (int i = 0; i < MESA_SHADER_STAGES; ++i) {
		if (pipeline->shaders[i]) {
			unsigned max_stage_waves = device->scratch_waves;

			scratch_bytes_per_wave = MAX2(scratch_bytes_per_wave,
			                              pipeline->shaders[i]->config.scratch_bytes_per_wave);

			max_stage_waves = MIN2(max_stage_waves,
			          4 * device->physical_device->rad_info.num_good_compute_units *
			          (256 / pipeline->shaders[i]->config.num_vgprs));
			max_waves = MAX2(max_waves, max_stage_waves);
		}
	}

	if (pipeline->shaders[MESA_SHADER_COMPUTE]) {
		unsigned group_size = pipeline->shaders[MESA_SHADER_COMPUTE]->info.cs.block_size[0] *
		                      pipeline->shaders[MESA_SHADER_COMPUTE]->info.cs.block_size[1] *
		                      pipeline->shaders[MESA_SHADER_COMPUTE]->info.cs.block_size[2];
		min_waves = MAX2(min_waves, round_up_u32(group_size, 64));
	}

	if (scratch_bytes_per_wave)
		max_waves = MIN2(max_waves, 0xffffffffu / scratch_bytes_per_wave);

	if (scratch_bytes_per_wave && max_waves < min_waves) {
		/* Not really true at this moment, but will be true on first
		 * execution. Avoid having hanging shaders. */
		return vk_error(VK_ERROR_OUT_OF_DEVICE_MEMORY);
	}
	pipeline->scratch_bytes_per_wave = scratch_bytes_per_wave;
	pipeline->max_waves = max_waves;
	return VK_SUCCESS;
}

static uint32_t si_translate_blend_function(VkBlendOp op)
{
	switch (op) {
	case VK_BLEND_OP_ADD:
		return V_028780_COMB_DST_PLUS_SRC;
	case VK_BLEND_OP_SUBTRACT:
		return V_028780_COMB_SRC_MINUS_DST;
	case VK_BLEND_OP_REVERSE_SUBTRACT:
		return V_028780_COMB_DST_MINUS_SRC;
	case VK_BLEND_OP_MIN:
		return V_028780_COMB_MIN_DST_SRC;
	case VK_BLEND_OP_MAX:
		return V_028780_COMB_MAX_DST_SRC;
	default:
		return 0;
	}
}

static uint32_t si_translate_blend_factor(VkBlendFactor factor)
{
	switch (factor) {
	case VK_BLEND_FACTOR_ZERO:
		return V_028780_BLEND_ZERO;
	case VK_BLEND_FACTOR_ONE:
		return V_028780_BLEND_ONE;
	case VK_BLEND_FACTOR_SRC_COLOR:
		return V_028780_BLEND_SRC_COLOR;
	case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
		return V_028780_BLEND_ONE_MINUS_SRC_COLOR;
	case VK_BLEND_FACTOR_DST_COLOR:
		return V_028780_BLEND_DST_COLOR;
	case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
		return V_028780_BLEND_ONE_MINUS_DST_COLOR;
	case VK_BLEND_FACTOR_SRC_ALPHA:
		return V_028780_BLEND_SRC_ALPHA;
	case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
		return V_028780_BLEND_ONE_MINUS_SRC_ALPHA;
	case VK_BLEND_FACTOR_DST_ALPHA:
		return V_028780_BLEND_DST_ALPHA;
	case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
		return V_028780_BLEND_ONE_MINUS_DST_ALPHA;
	case VK_BLEND_FACTOR_CONSTANT_COLOR:
		return V_028780_BLEND_CONSTANT_COLOR;
	case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
		return V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR;
	case VK_BLEND_FACTOR_CONSTANT_ALPHA:
		return V_028780_BLEND_CONSTANT_ALPHA;
	case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
		return V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA;
	case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
		return V_028780_BLEND_SRC_ALPHA_SATURATE;
	case VK_BLEND_FACTOR_SRC1_COLOR:
		return V_028780_BLEND_SRC1_COLOR;
	case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
		return V_028780_BLEND_INV_SRC1_COLOR;
	case VK_BLEND_FACTOR_SRC1_ALPHA:
		return V_028780_BLEND_SRC1_ALPHA;
	case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
		return V_028780_BLEND_INV_SRC1_ALPHA;
	default:
		return 0;
	}
}

static uint32_t si_translate_blend_opt_function(VkBlendOp op)
{
	switch (op) {
	case VK_BLEND_OP_ADD:
		return V_028760_OPT_COMB_ADD;
	case VK_BLEND_OP_SUBTRACT:
		return V_028760_OPT_COMB_SUBTRACT;
	case VK_BLEND_OP_REVERSE_SUBTRACT:
		return V_028760_OPT_COMB_REVSUBTRACT;
	case VK_BLEND_OP_MIN:
		return V_028760_OPT_COMB_MIN;
	case VK_BLEND_OP_MAX:
		return V_028760_OPT_COMB_MAX;
	default:
		return V_028760_OPT_COMB_BLEND_DISABLED;
	}
}

static uint32_t si_translate_blend_opt_factor(VkBlendFactor factor, bool is_alpha)
{
	switch (factor) {
	case VK_BLEND_FACTOR_ZERO:
		return V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_ALL;
	case VK_BLEND_FACTOR_ONE:
		return V_028760_BLEND_OPT_PRESERVE_ALL_IGNORE_NONE;
	case VK_BLEND_FACTOR_SRC_COLOR:
		return is_alpha ? V_028760_BLEND_OPT_PRESERVE_A1_IGNORE_A0
				: V_028760_BLEND_OPT_PRESERVE_C1_IGNORE_C0;
	case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
		return is_alpha ? V_028760_BLEND_OPT_PRESERVE_A0_IGNORE_A1
				: V_028760_BLEND_OPT_PRESERVE_C0_IGNORE_C1;
	case VK_BLEND_FACTOR_SRC_ALPHA:
		return V_028760_BLEND_OPT_PRESERVE_A1_IGNORE_A0;
	case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
		return V_028760_BLEND_OPT_PRESERVE_A0_IGNORE_A1;
	case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
		return is_alpha ? V_028760_BLEND_OPT_PRESERVE_ALL_IGNORE_NONE
				: V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_A0;
	default:
		return V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;
	}
}

/**
 * Get rid of DST in the blend factors by commuting the operands:
 *    func(src * DST, dst * 0) ---> func(src * 0, dst * SRC)
 */
static void si_blend_remove_dst(unsigned *func, unsigned *src_factor,
				unsigned *dst_factor, unsigned expected_dst,
				unsigned replacement_src)
{
	if (*src_factor == expected_dst &&
	    *dst_factor == VK_BLEND_FACTOR_ZERO) {
		*src_factor = VK_BLEND_FACTOR_ZERO;
		*dst_factor = replacement_src;

		/* Commuting the operands requires reversing subtractions. */
		if (*func == VK_BLEND_OP_SUBTRACT)
			*func = VK_BLEND_OP_REVERSE_SUBTRACT;
		else if (*func == VK_BLEND_OP_REVERSE_SUBTRACT)
			*func = VK_BLEND_OP_SUBTRACT;
	}
}

static bool si_blend_factor_uses_dst(unsigned factor)
{
	return factor == VK_BLEND_FACTOR_DST_COLOR ||
		factor == VK_BLEND_FACTOR_DST_ALPHA ||
		factor == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE ||
		factor == VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA ||
		factor == VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
}

static bool is_dual_src(VkBlendFactor factor)
{
	switch (factor) {
	case VK_BLEND_FACTOR_SRC1_COLOR:
	case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
	case VK_BLEND_FACTOR_SRC1_ALPHA:
	case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
		return true;
	default:
		return false;
	}
}

static unsigned si_choose_spi_color_format(VkFormat vk_format,
					    bool blend_enable,
					    bool blend_need_alpha)
{
	const struct vk_format_description *desc = vk_format_description(vk_format);
	unsigned format, ntype, swap;

	/* Alpha is needed for alpha-to-coverage.
	 * Blending may be with or without alpha.
	 */
	unsigned normal = 0; /* most optimal, may not support blending or export alpha */
	unsigned alpha = 0; /* exports alpha, but may not support blending */
	unsigned blend = 0; /* supports blending, but may not export alpha */
	unsigned blend_alpha = 0; /* least optimal, supports blending and exports alpha */

	format = radv_translate_colorformat(vk_format);
	ntype = radv_translate_color_numformat(vk_format, desc,
					       vk_format_get_first_non_void_channel(vk_format));
	swap = radv_translate_colorswap(vk_format, false);

	/* Choose the SPI color formats. These are required values for Stoney/RB+.
	 * Other chips have multiple choices, though they are not necessarily better.
	 */
	switch (format) {
	case V_028C70_COLOR_5_6_5:
	case V_028C70_COLOR_1_5_5_5:
	case V_028C70_COLOR_5_5_5_1:
	case V_028C70_COLOR_4_4_4_4:
	case V_028C70_COLOR_10_11_11:
	case V_028C70_COLOR_11_11_10:
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
		break;

	case V_028C70_COLOR_16:
	case V_028C70_COLOR_16_16:
	case V_028C70_COLOR_16_16_16_16:
		if (ntype == V_028C70_NUMBER_UNORM ||
		    ntype == V_028C70_NUMBER_SNORM) {
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
		unreachable("unhandled blend format");
	}

	if (blend_enable && blend_need_alpha)
		return blend_alpha;
	else if(blend_need_alpha)
		return alpha;
	else if(blend_enable)
		return blend;
	else
		return normal;
}

static void
radv_pipeline_compute_spi_color_formats(struct radv_pipeline *pipeline,
					const VkGraphicsPipelineCreateInfo *pCreateInfo,
					struct radv_blend_state *blend)
{
	RADV_FROM_HANDLE(radv_render_pass, pass, pCreateInfo->renderPass);
	struct radv_subpass *subpass = pass->subpasses + pCreateInfo->subpass;
	unsigned col_format = 0;

	for (unsigned i = 0; i < (blend->single_cb_enable ? 1 : subpass->color_count); ++i) {
		unsigned cf;

		if (subpass->color_attachments[i].attachment == VK_ATTACHMENT_UNUSED) {
			cf = V_028714_SPI_SHADER_ZERO;
		} else {
			struct radv_render_pass_attachment *attachment = pass->attachments + subpass->color_attachments[i].attachment;
			bool blend_enable =
				blend->blend_enable_4bit & (0xfu << (i * 4));

			cf = si_choose_spi_color_format(attachment->format,
			                                blend_enable,
			                                blend->need_src_alpha & (1 << i));
		}

		col_format |= cf << (4 * i);
	}

	blend->cb_shader_mask = ac_get_cb_shader_mask(col_format);

	if (blend->mrt0_is_dual_src)
		col_format |= (col_format & 0xf) << 4;
	blend->spi_shader_col_format = col_format;
}

static bool
format_is_int8(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);
	int channel =  vk_format_get_first_non_void_channel(format);

	return channel >= 0 && desc->channel[channel].pure_integer &&
	       desc->channel[channel].size == 8;
}

static bool
format_is_int10(VkFormat format)
{
	const struct vk_format_description *desc = vk_format_description(format);

	if (desc->nr_channels != 4)
		return false;
	for (unsigned i = 0; i < 4; i++) {
		if (desc->channel[i].pure_integer && desc->channel[i].size == 10)
			return true;
	}
	return false;
}

unsigned radv_format_meta_fs_key(VkFormat format)
{
	unsigned col_format = si_choose_spi_color_format(format, false, false) - 1;
	bool is_int8 = format_is_int8(format);
	bool is_int10 = format_is_int10(format);

	return col_format + (is_int8 ? 3 : is_int10 ? 5 : 0);
}

static void
radv_pipeline_compute_get_int_clamp(const VkGraphicsPipelineCreateInfo *pCreateInfo,
				    unsigned *is_int8, unsigned *is_int10)
{
	RADV_FROM_HANDLE(radv_render_pass, pass, pCreateInfo->renderPass);
	struct radv_subpass *subpass = pass->subpasses + pCreateInfo->subpass;
	*is_int8 = 0;
	*is_int10 = 0;

	for (unsigned i = 0; i < subpass->color_count; ++i) {
		struct radv_render_pass_attachment *attachment;

		if (subpass->color_attachments[i].attachment == VK_ATTACHMENT_UNUSED)
			continue;

		attachment = pass->attachments + subpass->color_attachments[i].attachment;

		if (format_is_int8(attachment->format))
			*is_int8 |= 1 << i;
		if (format_is_int10(attachment->format))
			*is_int10 |= 1 << i;
	}
}

static void
radv_blend_check_commutativity(struct radv_blend_state *blend,
			       VkBlendOp op, VkBlendFactor src,
			       VkBlendFactor dst, unsigned chanmask)
{
	/* Src factor is allowed when it does not depend on Dst. */
	static const uint32_t src_allowed =
		(1u << VK_BLEND_FACTOR_ONE) |
		(1u << VK_BLEND_FACTOR_SRC_COLOR) |
		(1u << VK_BLEND_FACTOR_SRC_ALPHA) |
		(1u << VK_BLEND_FACTOR_SRC_ALPHA_SATURATE) |
		(1u << VK_BLEND_FACTOR_CONSTANT_COLOR) |
		(1u << VK_BLEND_FACTOR_CONSTANT_ALPHA) |
		(1u << VK_BLEND_FACTOR_SRC1_COLOR) |
		(1u << VK_BLEND_FACTOR_SRC1_ALPHA) |
		(1u << VK_BLEND_FACTOR_ZERO) |
		(1u << VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR) |
		(1u << VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) |
		(1u << VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR) |
		(1u << VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA) |
		(1u << VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR) |
		(1u << VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA);

	if (dst == VK_BLEND_FACTOR_ONE &&
	    (src_allowed && (1u << src))) {
		/* Addition is commutative, but floating point addition isn't
		 * associative: subtle changes can be introduced via different
		 * rounding. Be conservative, only enable for min and max.
		 */
		if (op == VK_BLEND_OP_MAX || op == VK_BLEND_OP_MIN)
			blend->commutative_4bit |= chanmask;
	}
}

static struct radv_blend_state
radv_pipeline_init_blend_state(struct radv_pipeline *pipeline,
			       const VkGraphicsPipelineCreateInfo *pCreateInfo,
			       const struct radv_graphics_pipeline_create_info *extra)
{
	const VkPipelineColorBlendStateCreateInfo *vkblend = pCreateInfo->pColorBlendState;
	const VkPipelineMultisampleStateCreateInfo *vkms = pCreateInfo->pMultisampleState;
	struct radv_blend_state blend = {0};
	unsigned mode = V_028808_CB_NORMAL;
	int i;

	if (!vkblend)
		return blend;

	if (extra && extra->custom_blend_mode) {
		blend.single_cb_enable = true;
		mode = extra->custom_blend_mode;
	}
	blend.cb_color_control = 0;
	if (vkblend->logicOpEnable)
		blend.cb_color_control |= S_028808_ROP3(vkblend->logicOp | (vkblend->logicOp << 4));
	else
		blend.cb_color_control |= S_028808_ROP3(0xcc);

	blend.db_alpha_to_mask = S_028B70_ALPHA_TO_MASK_OFFSET0(2) |
		S_028B70_ALPHA_TO_MASK_OFFSET1(2) |
		S_028B70_ALPHA_TO_MASK_OFFSET2(2) |
		S_028B70_ALPHA_TO_MASK_OFFSET3(2);

	if (vkms && vkms->alphaToCoverageEnable) {
		blend.db_alpha_to_mask |= S_028B70_ALPHA_TO_MASK_ENABLE(1);
	}

	blend.cb_target_mask = 0;
	for (i = 0; i < vkblend->attachmentCount; i++) {
		const VkPipelineColorBlendAttachmentState *att = &vkblend->pAttachments[i];
		unsigned blend_cntl = 0;
		unsigned srcRGB_opt, dstRGB_opt, srcA_opt, dstA_opt;
		VkBlendOp eqRGB = att->colorBlendOp;
		VkBlendFactor srcRGB = att->srcColorBlendFactor;
		VkBlendFactor dstRGB = att->dstColorBlendFactor;
		VkBlendOp eqA = att->alphaBlendOp;
		VkBlendFactor srcA = att->srcAlphaBlendFactor;
		VkBlendFactor dstA = att->dstAlphaBlendFactor;

		blend.sx_mrt_blend_opt[i] = S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED) | S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED);

		if (!att->colorWriteMask)
			continue;

		blend.cb_target_mask |= (unsigned)att->colorWriteMask << (4 * i);
		blend.cb_target_enabled_4bit |= 0xf << (4 * i);
		if (!att->blendEnable) {
			blend.cb_blend_control[i] = blend_cntl;
			continue;
		}

		if (is_dual_src(srcRGB) || is_dual_src(dstRGB) || is_dual_src(srcA) || is_dual_src(dstA))
			if (i == 0)
				blend.mrt0_is_dual_src = true;

		if (eqRGB == VK_BLEND_OP_MIN || eqRGB == VK_BLEND_OP_MAX) {
			srcRGB = VK_BLEND_FACTOR_ONE;
			dstRGB = VK_BLEND_FACTOR_ONE;
		}
		if (eqA == VK_BLEND_OP_MIN || eqA == VK_BLEND_OP_MAX) {
			srcA = VK_BLEND_FACTOR_ONE;
			dstA = VK_BLEND_FACTOR_ONE;
		}

		radv_blend_check_commutativity(&blend, eqRGB, srcRGB, dstRGB,
					       0x7 << (4 * i));
		radv_blend_check_commutativity(&blend, eqA, srcA, dstA,
					       0x8 << (4 * i));

		/* Blending optimizations for RB+.
		 * These transformations don't change the behavior.
		 *
		 * First, get rid of DST in the blend factors:
		 *    func(src * DST, dst * 0) ---> func(src * 0, dst * SRC)
		 */
		si_blend_remove_dst(&eqRGB, &srcRGB, &dstRGB,
				    VK_BLEND_FACTOR_DST_COLOR,
				    VK_BLEND_FACTOR_SRC_COLOR);

		si_blend_remove_dst(&eqA, &srcA, &dstA,
				    VK_BLEND_FACTOR_DST_COLOR,
				    VK_BLEND_FACTOR_SRC_COLOR);

		si_blend_remove_dst(&eqA, &srcA, &dstA,
				    VK_BLEND_FACTOR_DST_ALPHA,
				    VK_BLEND_FACTOR_SRC_ALPHA);

		/* Look up the ideal settings from tables. */
		srcRGB_opt = si_translate_blend_opt_factor(srcRGB, false);
		dstRGB_opt = si_translate_blend_opt_factor(dstRGB, false);
		srcA_opt = si_translate_blend_opt_factor(srcA, true);
		dstA_opt = si_translate_blend_opt_factor(dstA, true);

				/* Handle interdependencies. */
		if (si_blend_factor_uses_dst(srcRGB))
			dstRGB_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;
		if (si_blend_factor_uses_dst(srcA))
			dstA_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;

		if (srcRGB == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE &&
		    (dstRGB == VK_BLEND_FACTOR_ZERO ||
		     dstRGB == VK_BLEND_FACTOR_SRC_ALPHA ||
		     dstRGB == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE))
			dstRGB_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_A0;

		/* Set the final value. */
		blend.sx_mrt_blend_opt[i] =
			S_028760_COLOR_SRC_OPT(srcRGB_opt) |
			S_028760_COLOR_DST_OPT(dstRGB_opt) |
			S_028760_COLOR_COMB_FCN(si_translate_blend_opt_function(eqRGB)) |
			S_028760_ALPHA_SRC_OPT(srcA_opt) |
			S_028760_ALPHA_DST_OPT(dstA_opt) |
			S_028760_ALPHA_COMB_FCN(si_translate_blend_opt_function(eqA));
		blend_cntl |= S_028780_ENABLE(1);

		blend_cntl |= S_028780_COLOR_COMB_FCN(si_translate_blend_function(eqRGB));
		blend_cntl |= S_028780_COLOR_SRCBLEND(si_translate_blend_factor(srcRGB));
		blend_cntl |= S_028780_COLOR_DESTBLEND(si_translate_blend_factor(dstRGB));
		if (srcA != srcRGB || dstA != dstRGB || eqA != eqRGB) {
			blend_cntl |= S_028780_SEPARATE_ALPHA_BLEND(1);
			blend_cntl |= S_028780_ALPHA_COMB_FCN(si_translate_blend_function(eqA));
			blend_cntl |= S_028780_ALPHA_SRCBLEND(si_translate_blend_factor(srcA));
			blend_cntl |= S_028780_ALPHA_DESTBLEND(si_translate_blend_factor(dstA));
		}
		blend.cb_blend_control[i] = blend_cntl;

		blend.blend_enable_4bit |= 0xfu << (i * 4);

		if (srcRGB == VK_BLEND_FACTOR_SRC_ALPHA ||
		    dstRGB == VK_BLEND_FACTOR_SRC_ALPHA ||
		    srcRGB == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE ||
		    dstRGB == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE ||
		    srcRGB == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA ||
		    dstRGB == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
			blend.need_src_alpha |= 1 << i;
	}
	for (i = vkblend->attachmentCount; i < 8; i++) {
		blend.cb_blend_control[i] = 0;
		blend.sx_mrt_blend_opt[i] = S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED) | S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED);
	}

	if (pipeline->device->physical_device->has_rbplus) {
		/* Disable RB+ blend optimizations for dual source blending. */
		if (blend.mrt0_is_dual_src) {
			for (i = 0; i < 8; i++) {
				blend.sx_mrt_blend_opt[i] =
					S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_NONE) |
					S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_NONE);
			}
		}

		/* RB+ doesn't work with dual source blending, logic op and
		 * RESOLVE.
		 */
		if (blend.mrt0_is_dual_src || vkblend->logicOpEnable ||
		    mode == V_028808_CB_RESOLVE)
			blend.cb_color_control |= S_028808_DISABLE_DUAL_QUAD(1);
	}

	if (blend.cb_target_mask)
		blend.cb_color_control |= S_028808_MODE(mode);
	else
		blend.cb_color_control |= S_028808_MODE(V_028808_CB_DISABLE);

	radv_pipeline_compute_spi_color_formats(pipeline, pCreateInfo, &blend);
	return blend;
}

static uint32_t si_translate_stencil_op(enum VkStencilOp op)
{
	switch (op) {
	case VK_STENCIL_OP_KEEP:
		return V_02842C_STENCIL_KEEP;
	case VK_STENCIL_OP_ZERO:
		return V_02842C_STENCIL_ZERO;
	case VK_STENCIL_OP_REPLACE:
		return V_02842C_STENCIL_REPLACE_TEST;
	case VK_STENCIL_OP_INCREMENT_AND_CLAMP:
		return V_02842C_STENCIL_ADD_CLAMP;
	case VK_STENCIL_OP_DECREMENT_AND_CLAMP:
		return V_02842C_STENCIL_SUB_CLAMP;
	case VK_STENCIL_OP_INVERT:
		return V_02842C_STENCIL_INVERT;
	case VK_STENCIL_OP_INCREMENT_AND_WRAP:
		return V_02842C_STENCIL_ADD_WRAP;
	case VK_STENCIL_OP_DECREMENT_AND_WRAP:
		return V_02842C_STENCIL_SUB_WRAP;
	default:
		return 0;
	}
}

static uint32_t si_translate_fill(VkPolygonMode func)
{
	switch(func) {
	case VK_POLYGON_MODE_FILL:
		return V_028814_X_DRAW_TRIANGLES;
	case VK_POLYGON_MODE_LINE:
		return V_028814_X_DRAW_LINES;
	case VK_POLYGON_MODE_POINT:
		return V_028814_X_DRAW_POINTS;
	default:
		assert(0);
		return V_028814_X_DRAW_POINTS;
	}
}

static uint8_t radv_pipeline_get_ps_iter_samples(const VkPipelineMultisampleStateCreateInfo *vkms)
{
	uint32_t num_samples = vkms->rasterizationSamples;
	uint32_t ps_iter_samples = 1;

	if (vkms->sampleShadingEnable) {
		ps_iter_samples = ceil(vkms->minSampleShading * num_samples);
		ps_iter_samples = util_next_power_of_two(ps_iter_samples);
	}
	return ps_iter_samples;
}

static bool
radv_is_depth_write_enabled(const VkPipelineDepthStencilStateCreateInfo *pCreateInfo)
{
	return pCreateInfo->depthTestEnable &&
	       pCreateInfo->depthWriteEnable &&
	       pCreateInfo->depthCompareOp != VK_COMPARE_OP_NEVER;
}

static bool
radv_writes_stencil(const VkStencilOpState *state)
{
	return state->writeMask &&
	       (state->failOp != VK_STENCIL_OP_KEEP ||
		state->passOp != VK_STENCIL_OP_KEEP ||
		state->depthFailOp != VK_STENCIL_OP_KEEP);
}

static bool
radv_is_stencil_write_enabled(const VkPipelineDepthStencilStateCreateInfo *pCreateInfo)
{
	return pCreateInfo->stencilTestEnable &&
	       (radv_writes_stencil(&pCreateInfo->front) ||
		radv_writes_stencil(&pCreateInfo->back));
}

static bool
radv_is_ds_write_enabled(const VkPipelineDepthStencilStateCreateInfo *pCreateInfo)
{
	return radv_is_depth_write_enabled(pCreateInfo) ||
	       radv_is_stencil_write_enabled(pCreateInfo);
}

static bool
radv_order_invariant_stencil_op(VkStencilOp op)
{
	/* REPLACE is normally order invariant, except when the stencil
	 * reference value is written by the fragment shader. Tracking this
	 * interaction does not seem worth the effort, so be conservative.
	 */
	return op != VK_STENCIL_OP_INCREMENT_AND_CLAMP &&
	       op != VK_STENCIL_OP_DECREMENT_AND_CLAMP &&
	       op != VK_STENCIL_OP_REPLACE;
}

static bool
radv_order_invariant_stencil_state(const VkStencilOpState *state)
{
	/* Compute whether, assuming Z writes are disabled, this stencil state
	 * is order invariant in the sense that the set of passing fragments as
	 * well as the final stencil buffer result does not depend on the order
	 * of fragments.
	 */
	return !state->writeMask ||
	       /* The following assumes that Z writes are disabled. */
	       (state->compareOp == VK_COMPARE_OP_ALWAYS &&
		radv_order_invariant_stencil_op(state->passOp) &&
		radv_order_invariant_stencil_op(state->depthFailOp)) ||
	       (state->compareOp == VK_COMPARE_OP_NEVER &&
		radv_order_invariant_stencil_op(state->failOp));
}

static bool
radv_pipeline_out_of_order_rast(struct radv_pipeline *pipeline,
				struct radv_blend_state *blend,
				const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	RADV_FROM_HANDLE(radv_render_pass, pass, pCreateInfo->renderPass);
	struct radv_subpass *subpass = pass->subpasses + pCreateInfo->subpass;
	unsigned colormask = blend->cb_target_enabled_4bit;

	if (!pipeline->device->physical_device->out_of_order_rast_allowed)
		return false;

	/* Be conservative if a logic operation is enabled with color buffers. */
	if (colormask && pCreateInfo->pColorBlendState->logicOpEnable)
		return false;

	/* Default depth/stencil invariance when no attachment is bound. */
	struct radv_dsa_order_invariance dsa_order_invariant = {
		.zs = true, .pass_set = true
	};

	if (pCreateInfo->pDepthStencilState &&
	    subpass->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED) {
		const VkPipelineDepthStencilStateCreateInfo *vkds =
			pCreateInfo->pDepthStencilState;
		struct radv_render_pass_attachment *attachment =
			pass->attachments + subpass->depth_stencil_attachment.attachment;
		bool has_stencil = vk_format_is_stencil(attachment->format);
		struct radv_dsa_order_invariance order_invariance[2];
		struct radv_shader_variant *ps =
			pipeline->shaders[MESA_SHADER_FRAGMENT];

		/* Compute depth/stencil order invariance in order to know if
		 * it's safe to enable out-of-order.
		 */
		bool zfunc_is_ordered =
			vkds->depthCompareOp == VK_COMPARE_OP_NEVER ||
			vkds->depthCompareOp == VK_COMPARE_OP_LESS ||
			vkds->depthCompareOp == VK_COMPARE_OP_LESS_OR_EQUAL ||
			vkds->depthCompareOp == VK_COMPARE_OP_GREATER ||
			vkds->depthCompareOp == VK_COMPARE_OP_GREATER_OR_EQUAL;

		bool nozwrite_and_order_invariant_stencil =
			!radv_is_ds_write_enabled(vkds) ||
			(!radv_is_depth_write_enabled(vkds) &&
			 radv_order_invariant_stencil_state(&vkds->front) &&
			 radv_order_invariant_stencil_state(&vkds->back));

		order_invariance[1].zs =
			nozwrite_and_order_invariant_stencil ||
			(!radv_is_stencil_write_enabled(vkds) &&
			 zfunc_is_ordered);
		order_invariance[0].zs =
			!radv_is_depth_write_enabled(vkds) || zfunc_is_ordered;

		order_invariance[1].pass_set =
			nozwrite_and_order_invariant_stencil ||
			(!radv_is_stencil_write_enabled(vkds) &&
			 (vkds->depthCompareOp == VK_COMPARE_OP_ALWAYS ||
			  vkds->depthCompareOp == VK_COMPARE_OP_NEVER));
		order_invariance[0].pass_set =
			!radv_is_depth_write_enabled(vkds) ||
			(vkds->depthCompareOp == VK_COMPARE_OP_ALWAYS ||
			 vkds->depthCompareOp == VK_COMPARE_OP_NEVER);

		dsa_order_invariant = order_invariance[has_stencil];
		if (!dsa_order_invariant.zs)
			return false;

		/* The set of PS invocations is always order invariant,
		 * except when early Z/S tests are requested.
		 */
		if (ps &&
		    ps->info.info.ps.writes_memory &&
		    ps->info.fs.early_fragment_test &&
		    !dsa_order_invariant.pass_set)
			return false;

		/* Determine if out-of-order rasterization should be disabled
		 * when occlusion queries are used.
		 */
		pipeline->graphics.disable_out_of_order_rast_for_occlusion =
			!dsa_order_invariant.pass_set;
	}

	/* No color buffers are enabled for writing. */
	if (!colormask)
		return true;

	unsigned blendmask = colormask & blend->blend_enable_4bit;

	if (blendmask) {
		/* Only commutative blending. */
		if (blendmask & ~blend->commutative_4bit)
			return false;

		if (!dsa_order_invariant.pass_set)
			return false;
	}

	if (colormask & ~blendmask)
		return false;

	return true;
}

static void
radv_pipeline_init_multisample_state(struct radv_pipeline *pipeline,
				     struct radv_blend_state *blend,
				     const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	const VkPipelineMultisampleStateCreateInfo *vkms = pCreateInfo->pMultisampleState;
	struct radv_multisample_state *ms = &pipeline->graphics.ms;
	unsigned num_tile_pipes = pipeline->device->physical_device->rad_info.num_tile_pipes;
	bool out_of_order_rast = false;
	int ps_iter_samples = 1;
	uint32_t mask = 0xffff;

	if (vkms)
		ms->num_samples = vkms->rasterizationSamples;
	else
		ms->num_samples = 1;

	if (vkms)
		ps_iter_samples = radv_pipeline_get_ps_iter_samples(vkms);
	if (vkms && !vkms->sampleShadingEnable && pipeline->shaders[MESA_SHADER_FRAGMENT]->info.info.ps.force_persample) {
		ps_iter_samples = ms->num_samples;
	}

	ms->pa_sc_line_cntl = S_028BDC_DX10_DIAMOND_TEST_ENA(1);
	ms->pa_sc_aa_config = 0;
	ms->db_eqaa = S_028804_HIGH_QUALITY_INTERSECTIONS(1) |
		S_028804_STATIC_ANCHOR_ASSOCIATIONS(1);
	ms->pa_sc_mode_cntl_1 =
		S_028A4C_WALK_FENCE_ENABLE(1) | //TODO linear dst fixes
		S_028A4C_WALK_FENCE_SIZE(num_tile_pipes == 2 ? 2 : 3) |
		/* always 1: */
		S_028A4C_WALK_ALIGN8_PRIM_FITS_ST(1) |
		S_028A4C_SUPERTILE_WALK_ORDER_ENABLE(1) |
		S_028A4C_TILE_WALK_ORDER_ENABLE(1) |
		S_028A4C_MULTI_SHADER_ENGINE_PRIM_DISCARD_ENABLE(1) |
		S_028A4C_FORCE_EOV_CNTDWN_ENABLE(1) |
		S_028A4C_FORCE_EOV_REZ_ENABLE(1);
	ms->pa_sc_mode_cntl_0 = S_028A48_ALTERNATE_RBS_PER_TILE(pipeline->device->physical_device->rad_info.chip_class >= GFX9) |
	                        S_028A48_VPORT_SCISSOR_ENABLE(1);

	if (ms->num_samples > 1) {
		unsigned log_samples = util_logbase2(ms->num_samples);
		unsigned log_ps_iter_samples = util_logbase2(ps_iter_samples);
		ms->pa_sc_mode_cntl_0 |= S_028A48_MSAA_ENABLE(1);
		ms->pa_sc_line_cntl |= S_028BDC_EXPAND_LINE_WIDTH(1); /* CM_R_028BDC_PA_SC_LINE_CNTL */
		ms->db_eqaa |= S_028804_MAX_ANCHOR_SAMPLES(log_samples) |
			S_028804_PS_ITER_SAMPLES(log_ps_iter_samples) |
			S_028804_MASK_EXPORT_NUM_SAMPLES(log_samples) |
			S_028804_ALPHA_TO_MASK_NUM_SAMPLES(log_samples);
		ms->pa_sc_aa_config |= S_028BE0_MSAA_NUM_SAMPLES(log_samples) |
			S_028BE0_MAX_SAMPLE_DIST(radv_cayman_get_maxdist(log_samples)) |
			S_028BE0_MSAA_EXPOSED_SAMPLES(log_samples); /* CM_R_028BE0_PA_SC_AA_CONFIG */
		ms->pa_sc_mode_cntl_1 |= S_028A4C_PS_ITER_SAMPLE(ps_iter_samples > 1);
		if (ps_iter_samples > 1)
			pipeline->graphics.spi_baryc_cntl |= S_0286E0_POS_FLOAT_LOCATION(2);
	}

	const struct VkPipelineRasterizationStateRasterizationOrderAMD *raster_order =
		vk_find_struct_const(pCreateInfo->pRasterizationState->pNext, PIPELINE_RASTERIZATION_STATE_RASTERIZATION_ORDER_AMD);
	if (raster_order && raster_order->rasterizationOrder == VK_RASTERIZATION_ORDER_RELAXED_AMD) {
		/* Out-of-order rasterization is explicitly enabled by the
		 * application.
		 */
		out_of_order_rast = true;
	} else {
		/* Determine if the driver can enable out-of-order
		 * rasterization internally.
		 */
		out_of_order_rast =
			radv_pipeline_out_of_order_rast(pipeline, blend, pCreateInfo);
	}

	if (out_of_order_rast) {
		ms->pa_sc_mode_cntl_1 |= S_028A4C_OUT_OF_ORDER_PRIMITIVE_ENABLE(1) |
					 S_028A4C_OUT_OF_ORDER_WATER_MARK(0x7);
	}

	if (vkms && vkms->pSampleMask) {
		mask = vkms->pSampleMask[0] & 0xffff;
	}

	ms->pa_sc_aa_mask[0] = mask | (mask << 16);
	ms->pa_sc_aa_mask[1] = mask | (mask << 16);
}

static bool
radv_prim_can_use_guardband(enum VkPrimitiveTopology topology)
{
	switch (topology) {
	case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
	case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
	case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
	case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
	case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
		return false;
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
	case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
		return true;
	default:
		unreachable("unhandled primitive type");
	}
}

static uint32_t
si_translate_prim(enum VkPrimitiveTopology topology)
{
	switch (topology) {
	case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
		return V_008958_DI_PT_POINTLIST;
	case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
		return V_008958_DI_PT_LINELIST;
	case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
		return V_008958_DI_PT_LINESTRIP;
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
		return V_008958_DI_PT_TRILIST;
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
		return V_008958_DI_PT_TRISTRIP;
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
		return V_008958_DI_PT_TRIFAN;
	case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
		return V_008958_DI_PT_LINELIST_ADJ;
	case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
		return V_008958_DI_PT_LINESTRIP_ADJ;
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
		return V_008958_DI_PT_TRILIST_ADJ;
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
		return V_008958_DI_PT_TRISTRIP_ADJ;
	case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
		return V_008958_DI_PT_PATCH;
	default:
		assert(0);
		return 0;
	}
}

static uint32_t
si_conv_gl_prim_to_gs_out(unsigned gl_prim)
{
	switch (gl_prim) {
	case 0: /* GL_POINTS */
		return V_028A6C_OUTPRIM_TYPE_POINTLIST;
	case 1: /* GL_LINES */
	case 3: /* GL_LINE_STRIP */
	case 0xA: /* GL_LINE_STRIP_ADJACENCY_ARB */
	case 0x8E7A: /* GL_ISOLINES */
		return V_028A6C_OUTPRIM_TYPE_LINESTRIP;

	case 4: /* GL_TRIANGLES */
	case 0xc: /* GL_TRIANGLES_ADJACENCY_ARB */
	case 5: /* GL_TRIANGLE_STRIP */
	case 7: /* GL_QUADS */
		return V_028A6C_OUTPRIM_TYPE_TRISTRIP;
	default:
		assert(0);
		return 0;
	}
}

static uint32_t
si_conv_prim_to_gs_out(enum VkPrimitiveTopology topology)
{
	switch (topology) {
	case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
	case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
		return V_028A6C_OUTPRIM_TYPE_POINTLIST;
	case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
	case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
	case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
	case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
		return V_028A6C_OUTPRIM_TYPE_LINESTRIP;
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
		return V_028A6C_OUTPRIM_TYPE_TRISTRIP;
	default:
		assert(0);
		return 0;
	}
}

static unsigned si_map_swizzle(unsigned swizzle)
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


static unsigned radv_dynamic_state_mask(VkDynamicState state)
{
	switch(state) {
	case VK_DYNAMIC_STATE_VIEWPORT:
		return RADV_DYNAMIC_VIEWPORT;
	case VK_DYNAMIC_STATE_SCISSOR:
		return RADV_DYNAMIC_SCISSOR;
	case VK_DYNAMIC_STATE_LINE_WIDTH:
		return RADV_DYNAMIC_LINE_WIDTH;
	case VK_DYNAMIC_STATE_DEPTH_BIAS:
		return RADV_DYNAMIC_DEPTH_BIAS;
	case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
		return RADV_DYNAMIC_BLEND_CONSTANTS;
	case VK_DYNAMIC_STATE_DEPTH_BOUNDS:
		return RADV_DYNAMIC_DEPTH_BOUNDS;
	case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
		return RADV_DYNAMIC_STENCIL_COMPARE_MASK;
	case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
		return RADV_DYNAMIC_STENCIL_WRITE_MASK;
	case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
		return RADV_DYNAMIC_STENCIL_REFERENCE;
	case VK_DYNAMIC_STATE_DISCARD_RECTANGLE_EXT:
		return RADV_DYNAMIC_DISCARD_RECTANGLE;
	default:
		unreachable("Unhandled dynamic state");
	}
}

static uint32_t radv_pipeline_needed_dynamic_state(const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	uint32_t states = RADV_DYNAMIC_ALL;

	/* If rasterization is disabled we do not care about any of the dynamic states,
	 * since they are all rasterization related only. */
	if (pCreateInfo->pRasterizationState->rasterizerDiscardEnable)
		return 0;

	if (!pCreateInfo->pRasterizationState->depthBiasEnable)
		states &= ~RADV_DYNAMIC_DEPTH_BIAS;

	if (!pCreateInfo->pDepthStencilState ||
	    !pCreateInfo->pDepthStencilState->depthBoundsTestEnable)
		states &= ~RADV_DYNAMIC_DEPTH_BOUNDS;

	if (!pCreateInfo->pDepthStencilState ||
	    !pCreateInfo->pDepthStencilState->stencilTestEnable)
		states &= ~(RADV_DYNAMIC_STENCIL_COMPARE_MASK |
		            RADV_DYNAMIC_STENCIL_WRITE_MASK |
		            RADV_DYNAMIC_STENCIL_REFERENCE);

	if (!vk_find_struct_const(pCreateInfo->pNext, PIPELINE_DISCARD_RECTANGLE_STATE_CREATE_INFO_EXT))
		states &= ~RADV_DYNAMIC_DISCARD_RECTANGLE;

	/* TODO: blend constants & line width. */

	return states;
}


static void
radv_pipeline_init_dynamic_state(struct radv_pipeline *pipeline,
				 const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	uint32_t needed_states = radv_pipeline_needed_dynamic_state(pCreateInfo);
	uint32_t states = needed_states;
	RADV_FROM_HANDLE(radv_render_pass, pass, pCreateInfo->renderPass);
	struct radv_subpass *subpass = &pass->subpasses[pCreateInfo->subpass];

	pipeline->dynamic_state = default_dynamic_state;
	pipeline->graphics.needed_dynamic_state = needed_states;

	if (pCreateInfo->pDynamicState) {
		/* Remove all of the states that are marked as dynamic */
		uint32_t count = pCreateInfo->pDynamicState->dynamicStateCount;
		for (uint32_t s = 0; s < count; s++)
			states &= ~radv_dynamic_state_mask(pCreateInfo->pDynamicState->pDynamicStates[s]);
	}

	struct radv_dynamic_state *dynamic = &pipeline->dynamic_state;

	if (needed_states & RADV_DYNAMIC_VIEWPORT) {
		assert(pCreateInfo->pViewportState);

		dynamic->viewport.count = pCreateInfo->pViewportState->viewportCount;
		if (states & RADV_DYNAMIC_VIEWPORT) {
			typed_memcpy(dynamic->viewport.viewports,
			             pCreateInfo->pViewportState->pViewports,
			             pCreateInfo->pViewportState->viewportCount);
		}
	}

	if (needed_states & RADV_DYNAMIC_SCISSOR) {
		dynamic->scissor.count = pCreateInfo->pViewportState->scissorCount;
		if (states & RADV_DYNAMIC_SCISSOR) {
			typed_memcpy(dynamic->scissor.scissors,
			             pCreateInfo->pViewportState->pScissors,
			             pCreateInfo->pViewportState->scissorCount);
		}
	}

	if (states & RADV_DYNAMIC_LINE_WIDTH) {
		assert(pCreateInfo->pRasterizationState);
		dynamic->line_width = pCreateInfo->pRasterizationState->lineWidth;
	}

	if (states & RADV_DYNAMIC_DEPTH_BIAS) {
		assert(pCreateInfo->pRasterizationState);
		dynamic->depth_bias.bias =
			pCreateInfo->pRasterizationState->depthBiasConstantFactor;
		dynamic->depth_bias.clamp =
			pCreateInfo->pRasterizationState->depthBiasClamp;
		dynamic->depth_bias.slope =
			pCreateInfo->pRasterizationState->depthBiasSlopeFactor;
	}

	/* Section 9.2 of the Vulkan 1.0.15 spec says:
	 *
	 *    pColorBlendState is [...] NULL if the pipeline has rasterization
	 *    disabled or if the subpass of the render pass the pipeline is
	 *    created against does not use any color attachments.
	 */
	bool uses_color_att = false;
	for (unsigned i = 0; i < subpass->color_count; ++i) {
		if (subpass->color_attachments[i].attachment != VK_ATTACHMENT_UNUSED) {
			uses_color_att = true;
			break;
		}
	}

	if (uses_color_att && states & RADV_DYNAMIC_BLEND_CONSTANTS) {
		assert(pCreateInfo->pColorBlendState);
		typed_memcpy(dynamic->blend_constants,
			     pCreateInfo->pColorBlendState->blendConstants, 4);
	}

	/* If there is no depthstencil attachment, then don't read
	 * pDepthStencilState. The Vulkan spec states that pDepthStencilState may
	 * be NULL in this case. Even if pDepthStencilState is non-NULL, there is
	 * no need to override the depthstencil defaults in
	 * radv_pipeline::dynamic_state when there is no depthstencil attachment.
	 *
	 * Section 9.2 of the Vulkan 1.0.15 spec says:
	 *
	 *    pDepthStencilState is [...] NULL if the pipeline has rasterization
	 *    disabled or if the subpass of the render pass the pipeline is created
	 *    against does not use a depth/stencil attachment.
	 */
	if (needed_states &&
	    subpass->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED) {
		assert(pCreateInfo->pDepthStencilState);

		if (states & RADV_DYNAMIC_DEPTH_BOUNDS) {
			dynamic->depth_bounds.min =
				pCreateInfo->pDepthStencilState->minDepthBounds;
			dynamic->depth_bounds.max =
				pCreateInfo->pDepthStencilState->maxDepthBounds;
		}

		if (states & RADV_DYNAMIC_STENCIL_COMPARE_MASK) {
			dynamic->stencil_compare_mask.front =
				pCreateInfo->pDepthStencilState->front.compareMask;
			dynamic->stencil_compare_mask.back =
				pCreateInfo->pDepthStencilState->back.compareMask;
		}

		if (states & RADV_DYNAMIC_STENCIL_WRITE_MASK) {
			dynamic->stencil_write_mask.front =
				pCreateInfo->pDepthStencilState->front.writeMask;
			dynamic->stencil_write_mask.back =
				pCreateInfo->pDepthStencilState->back.writeMask;
		}

		if (states & RADV_DYNAMIC_STENCIL_REFERENCE) {
			dynamic->stencil_reference.front =
				pCreateInfo->pDepthStencilState->front.reference;
			dynamic->stencil_reference.back =
				pCreateInfo->pDepthStencilState->back.reference;
		}
	}

	const  VkPipelineDiscardRectangleStateCreateInfoEXT *discard_rectangle_info =
			vk_find_struct_const(pCreateInfo->pNext, PIPELINE_DISCARD_RECTANGLE_STATE_CREATE_INFO_EXT);
	if (states & RADV_DYNAMIC_DISCARD_RECTANGLE) {
		dynamic->discard_rectangle.count = discard_rectangle_info->discardRectangleCount;
		typed_memcpy(dynamic->discard_rectangle.rectangles,
		             discard_rectangle_info->pDiscardRectangles,
		             discard_rectangle_info->discardRectangleCount);
	}

	pipeline->dynamic_state.mask = states;
}

static struct radv_gs_state
calculate_gs_info(const VkGraphicsPipelineCreateInfo *pCreateInfo,
                       const struct radv_pipeline *pipeline)
{
	struct radv_gs_state gs = {0};
	struct radv_shader_variant_info *gs_info = &pipeline->shaders[MESA_SHADER_GEOMETRY]->info;
	struct radv_es_output_info *es_info;
	if (pipeline->device->physical_device->rad_info.chip_class >= GFX9)
		es_info = radv_pipeline_has_tess(pipeline) ? &gs_info->tes.es_info : &gs_info->vs.es_info;
	else
		es_info = radv_pipeline_has_tess(pipeline) ?
			&pipeline->shaders[MESA_SHADER_TESS_EVAL]->info.tes.es_info :
			&pipeline->shaders[MESA_SHADER_VERTEX]->info.vs.es_info;

	unsigned gs_num_invocations = MAX2(gs_info->gs.invocations, 1);
	bool uses_adjacency;
	switch(pCreateInfo->pInputAssemblyState->topology) {
	case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
	case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
		uses_adjacency = true;
		break;
	default:
		uses_adjacency = false;
		break;
	}

	/* All these are in dwords: */
	/* We can't allow using the whole LDS, because GS waves compete with
	 * other shader stages for LDS space. */
	const unsigned max_lds_size = 8 * 1024;
	const unsigned esgs_itemsize = es_info->esgs_itemsize / 4;
	unsigned esgs_lds_size;

	/* All these are per subgroup: */
	const unsigned max_out_prims = 32 * 1024;
	const unsigned max_es_verts = 255;
	const unsigned ideal_gs_prims = 64;
	unsigned max_gs_prims, gs_prims;
	unsigned min_es_verts, es_verts, worst_case_es_verts;

	if (uses_adjacency || gs_num_invocations > 1)
		max_gs_prims = 127 / gs_num_invocations;
	else
		max_gs_prims = 255;

	/* MAX_PRIMS_PER_SUBGROUP = gs_prims * max_vert_out * gs_invocations.
	 * Make sure we don't go over the maximum value.
	 */
	if (gs_info->gs.vertices_out > 0) {
		max_gs_prims = MIN2(max_gs_prims,
				    max_out_prims /
				    (gs_info->gs.vertices_out * gs_num_invocations));
	}
	assert(max_gs_prims > 0);

	/* If the primitive has adjacency, halve the number of vertices
	 * that will be reused in multiple primitives.
	 */
	min_es_verts = gs_info->gs.vertices_in / (uses_adjacency ? 2 : 1);

	gs_prims = MIN2(ideal_gs_prims, max_gs_prims);
	worst_case_es_verts = MIN2(min_es_verts * gs_prims, max_es_verts);

	/* Compute ESGS LDS size based on the worst case number of ES vertices
	 * needed to create the target number of GS prims per subgroup.
	 */
	esgs_lds_size = esgs_itemsize * worst_case_es_verts;

	/* If total LDS usage is too big, refactor partitions based on ratio
	 * of ESGS item sizes.
	 */
	if (esgs_lds_size > max_lds_size) {
		/* Our target GS Prims Per Subgroup was too large. Calculate
		 * the maximum number of GS Prims Per Subgroup that will fit
		 * into LDS, capped by the maximum that the hardware can support.
		 */
		gs_prims = MIN2((max_lds_size / (esgs_itemsize * min_es_verts)),
				max_gs_prims);
		assert(gs_prims > 0);
		worst_case_es_verts = MIN2(min_es_verts * gs_prims,
					   max_es_verts);

		esgs_lds_size = esgs_itemsize * worst_case_es_verts;
		assert(esgs_lds_size <= max_lds_size);
	}

	/* Now calculate remaining ESGS information. */
	if (esgs_lds_size)
		es_verts = MIN2(esgs_lds_size / esgs_itemsize, max_es_verts);
	else
		es_verts = max_es_verts;

	/* Vertices for adjacency primitives are not always reused, so restore
	 * it for ES_VERTS_PER_SUBGRP.
	 */
	min_es_verts = gs_info->gs.vertices_in;

	/* For normal primitives, the VGT only checks if they are past the ES
	 * verts per subgroup after allocating a full GS primitive and if they
	 * are, kick off a new subgroup.  But if those additional ES verts are
	 * unique (e.g. not reused) we need to make sure there is enough LDS
	 * space to account for those ES verts beyond ES_VERTS_PER_SUBGRP.
	 */
	es_verts -= min_es_verts - 1;

	uint32_t es_verts_per_subgroup = es_verts;
	uint32_t gs_prims_per_subgroup = gs_prims;
	uint32_t gs_inst_prims_in_subgroup = gs_prims * gs_num_invocations;
	uint32_t max_prims_per_subgroup = gs_inst_prims_in_subgroup * gs_info->gs.vertices_out;
	gs.lds_size = align(esgs_lds_size, 128) / 128;
	gs.vgt_gs_onchip_cntl = S_028A44_ES_VERTS_PER_SUBGRP(es_verts_per_subgroup) |
	                        S_028A44_GS_PRIMS_PER_SUBGRP(gs_prims_per_subgroup) |
	                        S_028A44_GS_INST_PRIMS_IN_SUBGRP(gs_inst_prims_in_subgroup);
	gs.vgt_gs_max_prims_per_subgroup = S_028A94_MAX_PRIMS_PER_SUBGROUP(max_prims_per_subgroup);
	gs.vgt_esgs_ring_itemsize  = esgs_itemsize;
	assert(max_prims_per_subgroup <= max_out_prims);

	return gs;
}

static void
calculate_gs_ring_sizes(struct radv_pipeline *pipeline, const struct radv_gs_state *gs)
{
	struct radv_device *device = pipeline->device;
	unsigned num_se = device->physical_device->rad_info.max_se;
	unsigned wave_size = 64;
	unsigned max_gs_waves = 32 * num_se; /* max 32 per SE on GCN */
	unsigned gs_vertex_reuse = 16 * num_se; /* GS_VERTEX_REUSE register (per SE) */
	unsigned alignment = 256 * num_se;
	/* The maximum size is 63.999 MB per SE. */
	unsigned max_size = ((unsigned)(63.999 * 1024 * 1024) & ~255) * num_se;
	struct radv_shader_variant_info *gs_info = &pipeline->shaders[MESA_SHADER_GEOMETRY]->info;

	/* Calculate the minimum size. */
	unsigned min_esgs_ring_size = align(gs->vgt_esgs_ring_itemsize * 4 * gs_vertex_reuse *
					    wave_size, alignment);
	/* These are recommended sizes, not minimum sizes. */
	unsigned esgs_ring_size = max_gs_waves * 2 * wave_size *
		gs->vgt_esgs_ring_itemsize * 4 * gs_info->gs.vertices_in;
	unsigned gsvs_ring_size = max_gs_waves * 2 * wave_size *
		gs_info->gs.max_gsvs_emit_size * 1; // no streams in VK (gs->max_gs_stream + 1);

	min_esgs_ring_size = align(min_esgs_ring_size, alignment);
	esgs_ring_size = align(esgs_ring_size, alignment);
	gsvs_ring_size = align(gsvs_ring_size, alignment);

	if (pipeline->device->physical_device->rad_info.chip_class <= VI)
		pipeline->graphics.esgs_ring_size = CLAMP(esgs_ring_size, min_esgs_ring_size, max_size);

	pipeline->graphics.gsvs_ring_size = MIN2(gsvs_ring_size, max_size);
}

static void si_multiwave_lds_size_workaround(struct radv_device *device,
					     unsigned *lds_size)
{
	/* If tessellation is all offchip and on-chip GS isn't used, this
	 * workaround is not needed.
	 */
	return;

	/* SPI barrier management bug:
	 *   Make sure we have at least 4k of LDS in use to avoid the bug.
	 *   It applies to workgroup sizes of more than one wavefront.
	 */
	if (device->physical_device->rad_info.family == CHIP_BONAIRE ||
	    device->physical_device->rad_info.family == CHIP_KABINI ||
	    device->physical_device->rad_info.family == CHIP_MULLINS)
		*lds_size = MAX2(*lds_size, 8);
}

struct radv_shader_variant *
radv_get_vertex_shader(struct radv_pipeline *pipeline)
{
	if (pipeline->shaders[MESA_SHADER_VERTEX])
		return pipeline->shaders[MESA_SHADER_VERTEX];
	if (pipeline->shaders[MESA_SHADER_TESS_CTRL])
		return pipeline->shaders[MESA_SHADER_TESS_CTRL];
	return pipeline->shaders[MESA_SHADER_GEOMETRY];
}

static struct radv_shader_variant *
radv_get_tess_eval_shader(struct radv_pipeline *pipeline)
{
	if (pipeline->shaders[MESA_SHADER_TESS_EVAL])
		return pipeline->shaders[MESA_SHADER_TESS_EVAL];
	return pipeline->shaders[MESA_SHADER_GEOMETRY];
}

static struct radv_tessellation_state
calculate_tess_state(struct radv_pipeline *pipeline,
		     const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	unsigned num_tcs_input_cp;
	unsigned num_tcs_output_cp;
	unsigned lds_size;
	unsigned num_patches;
	struct radv_tessellation_state tess = {0};

	num_tcs_input_cp = pCreateInfo->pTessellationState->patchControlPoints;
	num_tcs_output_cp = pipeline->shaders[MESA_SHADER_TESS_CTRL]->info.tcs.tcs_vertices_out; //TCS VERTICES OUT
	num_patches = pipeline->shaders[MESA_SHADER_TESS_CTRL]->info.tcs.num_patches;

	lds_size = pipeline->shaders[MESA_SHADER_TESS_CTRL]->info.tcs.lds_size;

	if (pipeline->device->physical_device->rad_info.chip_class >= CIK) {
		assert(lds_size <= 65536);
		lds_size = align(lds_size, 512) / 512;
	} else {
		assert(lds_size <= 32768);
		lds_size = align(lds_size, 256) / 256;
	}
	si_multiwave_lds_size_workaround(pipeline->device, &lds_size);

	tess.lds_size = lds_size;

	tess.ls_hs_config = S_028B58_NUM_PATCHES(num_patches) |
		S_028B58_HS_NUM_INPUT_CP(num_tcs_input_cp) |
		S_028B58_HS_NUM_OUTPUT_CP(num_tcs_output_cp);
	tess.num_patches = num_patches;

	struct radv_shader_variant *tes = radv_get_tess_eval_shader(pipeline);
	unsigned type = 0, partitioning = 0, topology = 0, distribution_mode = 0;

	switch (tes->info.tes.primitive_mode) {
	case GL_TRIANGLES:
		type = V_028B6C_TESS_TRIANGLE;
		break;
	case GL_QUADS:
		type = V_028B6C_TESS_QUAD;
		break;
	case GL_ISOLINES:
		type = V_028B6C_TESS_ISOLINE;
		break;
	}

	switch (tes->info.tes.spacing) {
	case TESS_SPACING_EQUAL:
		partitioning = V_028B6C_PART_INTEGER;
		break;
	case TESS_SPACING_FRACTIONAL_ODD:
		partitioning = V_028B6C_PART_FRAC_ODD;
		break;
	case TESS_SPACING_FRACTIONAL_EVEN:
		partitioning = V_028B6C_PART_FRAC_EVEN;
		break;
	default:
		break;
	}

	bool ccw = tes->info.tes.ccw;
	const VkPipelineTessellationDomainOriginStateCreateInfoKHR *domain_origin_state =
	              vk_find_struct_const(pCreateInfo->pTessellationState,
	                                   PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO_KHR);

	if (domain_origin_state && domain_origin_state->domainOrigin != VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT_KHR)
		ccw = !ccw;

	if (tes->info.tes.point_mode)
		topology = V_028B6C_OUTPUT_POINT;
	else if (tes->info.tes.primitive_mode == GL_ISOLINES)
		topology = V_028B6C_OUTPUT_LINE;
	else if (ccw)
		topology = V_028B6C_OUTPUT_TRIANGLE_CCW;
	else
		topology = V_028B6C_OUTPUT_TRIANGLE_CW;

	if (pipeline->device->has_distributed_tess) {
		if (pipeline->device->physical_device->rad_info.family == CHIP_FIJI ||
		    pipeline->device->physical_device->rad_info.family >= CHIP_POLARIS10)
			distribution_mode = V_028B6C_DISTRIBUTION_MODE_TRAPEZOIDS;
		else
			distribution_mode = V_028B6C_DISTRIBUTION_MODE_DONUTS;
	} else
		distribution_mode = V_028B6C_DISTRIBUTION_MODE_NO_DIST;

	tess.tf_param = S_028B6C_TYPE(type) |
		S_028B6C_PARTITIONING(partitioning) |
		S_028B6C_TOPOLOGY(topology) |
		S_028B6C_DISTRIBUTION_MODE(distribution_mode);

	return tess;
}

static const struct radv_prim_vertex_count prim_size_table[] = {
	[V_008958_DI_PT_NONE] = {0, 0},
	[V_008958_DI_PT_POINTLIST] = {1, 1},
	[V_008958_DI_PT_LINELIST] = {2, 2},
	[V_008958_DI_PT_LINESTRIP] = {2, 1},
	[V_008958_DI_PT_TRILIST] = {3, 3},
	[V_008958_DI_PT_TRIFAN] = {3, 1},
	[V_008958_DI_PT_TRISTRIP] = {3, 1},
	[V_008958_DI_PT_LINELIST_ADJ] = {4, 4},
	[V_008958_DI_PT_LINESTRIP_ADJ] = {4, 1},
	[V_008958_DI_PT_TRILIST_ADJ] = {6, 6},
	[V_008958_DI_PT_TRISTRIP_ADJ] = {6, 2},
	[V_008958_DI_PT_RECTLIST] = {3, 3},
	[V_008958_DI_PT_LINELOOP] = {2, 1},
	[V_008958_DI_PT_POLYGON] = {3, 1},
	[V_008958_DI_PT_2D_TRI_STRIP] = {0, 0},
};

static const struct radv_vs_output_info *get_vs_output_info(const struct radv_pipeline *pipeline)
{
	if (radv_pipeline_has_gs(pipeline))
		return &pipeline->gs_copy_shader->info.vs.outinfo;
	else if (radv_pipeline_has_tess(pipeline))
		return &pipeline->shaders[MESA_SHADER_TESS_EVAL]->info.tes.outinfo;
	else
		return &pipeline->shaders[MESA_SHADER_VERTEX]->info.vs.outinfo;
}

static void
radv_link_shaders(struct radv_pipeline *pipeline, nir_shader **shaders)
{
	nir_shader* ordered_shaders[MESA_SHADER_STAGES];
	int shader_count = 0;

	if(shaders[MESA_SHADER_FRAGMENT]) {
		ordered_shaders[shader_count++] = shaders[MESA_SHADER_FRAGMENT];
	}
	if(shaders[MESA_SHADER_GEOMETRY]) {
		ordered_shaders[shader_count++] = shaders[MESA_SHADER_GEOMETRY];
	}
	if(shaders[MESA_SHADER_TESS_EVAL]) {
		ordered_shaders[shader_count++] = shaders[MESA_SHADER_TESS_EVAL];
	}
	if(shaders[MESA_SHADER_TESS_CTRL]) {
		ordered_shaders[shader_count++] = shaders[MESA_SHADER_TESS_CTRL];
	}
	if(shaders[MESA_SHADER_VERTEX]) {
		ordered_shaders[shader_count++] = shaders[MESA_SHADER_VERTEX];
	}

	for (int i = 1; i < shader_count; ++i)  {
		nir_lower_io_arrays_to_elements(ordered_shaders[i],
						ordered_shaders[i - 1]);

		nir_remove_dead_variables(ordered_shaders[i],
					  nir_var_shader_out);
		nir_remove_dead_variables(ordered_shaders[i - 1],
					  nir_var_shader_in);

		bool progress = nir_remove_unused_varyings(ordered_shaders[i],
							   ordered_shaders[i - 1]);

		nir_compact_varyings(ordered_shaders[i],
				     ordered_shaders[i - 1], true);

		if (progress) {
			if (nir_lower_global_vars_to_local(ordered_shaders[i])) {
				ac_lower_indirect_derefs(ordered_shaders[i],
				                         pipeline->device->physical_device->rad_info.chip_class);
			}
			radv_optimize_nir(ordered_shaders[i]);

			if (nir_lower_global_vars_to_local(ordered_shaders[i - 1])) {
				ac_lower_indirect_derefs(ordered_shaders[i - 1],
				                         pipeline->device->physical_device->rad_info.chip_class);
			}
			radv_optimize_nir(ordered_shaders[i - 1]);
		}
	}
}


static struct radv_pipeline_key
radv_generate_graphics_pipeline_key(struct radv_pipeline *pipeline,
                                    const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                    const struct radv_blend_state *blend,
                                    bool has_view_index)
{
	const VkPipelineVertexInputStateCreateInfo *input_state =
	                                         pCreateInfo->pVertexInputState;
	const VkPipelineVertexInputDivisorStateCreateInfoEXT *divisor_state =
		vk_find_struct_const(input_state->pNext, PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT);

	struct radv_pipeline_key key;
	memset(&key, 0, sizeof(key));

	key.has_multiview_view_index = has_view_index;

	uint32_t binding_input_rate = 0;
	uint32_t instance_rate_divisors[MAX_VERTEX_ATTRIBS];
	for (unsigned i = 0; i < input_state->vertexBindingDescriptionCount; ++i) {
		if (input_state->pVertexBindingDescriptions[i].inputRate) {
			unsigned binding = input_state->pVertexBindingDescriptions[i].binding;
			binding_input_rate |= 1u << binding;
			instance_rate_divisors[binding] = 1;
		}
	}
	if (divisor_state) {
		for (unsigned i = 0; i < divisor_state->vertexBindingDivisorCount; ++i) {
			instance_rate_divisors[divisor_state->pVertexBindingDivisors[i].binding] =
				divisor_state->pVertexBindingDivisors[i].divisor;
		}
	}

	for (unsigned i = 0; i < input_state->vertexAttributeDescriptionCount; ++i) {
		unsigned binding;
		binding = input_state->pVertexAttributeDescriptions[i].binding;
		if (binding_input_rate & (1u << binding)) {
			unsigned location = input_state->pVertexAttributeDescriptions[i].location;
			key.instance_rate_inputs |= 1u << location;
			key.instance_rate_divisors[location] = instance_rate_divisors[binding];
		}
	}

	if (pCreateInfo->pTessellationState)
		key.tess_input_vertices = pCreateInfo->pTessellationState->patchControlPoints;


	if (pCreateInfo->pMultisampleState &&
	    pCreateInfo->pMultisampleState->rasterizationSamples > 1) {
		uint32_t num_samples = pCreateInfo->pMultisampleState->rasterizationSamples;
		uint32_t ps_iter_samples = radv_pipeline_get_ps_iter_samples(pCreateInfo->pMultisampleState);
		key.multisample = true;
		key.log2_num_samples = util_logbase2(num_samples);
		key.log2_ps_iter_samples = util_logbase2(ps_iter_samples);
	}

	key.col_format = blend->spi_shader_col_format;
	if (pipeline->device->physical_device->rad_info.chip_class < VI)
		radv_pipeline_compute_get_int_clamp(pCreateInfo, &key.is_int8, &key.is_int10);

	return key;
}

static void
radv_fill_shader_keys(struct radv_shader_variant_key *keys,
                      const struct radv_pipeline_key *key,
                      nir_shader **nir)
{
	keys[MESA_SHADER_VERTEX].vs.instance_rate_inputs = key->instance_rate_inputs;
	for (unsigned i = 0; i < MAX_VERTEX_ATTRIBS; ++i)
		keys[MESA_SHADER_VERTEX].vs.instance_rate_divisors[i] = key->instance_rate_divisors[i];

	if (nir[MESA_SHADER_TESS_CTRL]) {
		keys[MESA_SHADER_VERTEX].vs.as_ls = true;
		keys[MESA_SHADER_TESS_CTRL].tcs.num_inputs = 0;
		keys[MESA_SHADER_TESS_CTRL].tcs.input_vertices = key->tess_input_vertices;
		keys[MESA_SHADER_TESS_CTRL].tcs.primitive_mode = nir[MESA_SHADER_TESS_EVAL]->info.tess.primitive_mode;

		keys[MESA_SHADER_TESS_CTRL].tcs.tes_reads_tess_factors = !!(nir[MESA_SHADER_TESS_EVAL]->info.inputs_read & (VARYING_BIT_TESS_LEVEL_INNER | VARYING_BIT_TESS_LEVEL_OUTER));
	}

	if (nir[MESA_SHADER_GEOMETRY]) {
		if (nir[MESA_SHADER_TESS_CTRL])
			keys[MESA_SHADER_TESS_EVAL].tes.as_es = true;
		else
			keys[MESA_SHADER_VERTEX].vs.as_es = true;
	}

	for(int i = 0; i < MESA_SHADER_STAGES; ++i)
		keys[i].has_multiview_view_index = key->has_multiview_view_index;

	keys[MESA_SHADER_FRAGMENT].fs.multisample = key->multisample;
	keys[MESA_SHADER_FRAGMENT].fs.col_format = key->col_format;
	keys[MESA_SHADER_FRAGMENT].fs.is_int8 = key->is_int8;
	keys[MESA_SHADER_FRAGMENT].fs.is_int10 = key->is_int10;
	keys[MESA_SHADER_FRAGMENT].fs.log2_ps_iter_samples = key->log2_ps_iter_samples;
	keys[MESA_SHADER_FRAGMENT].fs.log2_num_samples = key->log2_num_samples;
}

static void
merge_tess_info(struct shader_info *tes_info,
                const struct shader_info *tcs_info)
{
	/* The Vulkan 1.0.38 spec, section 21.1 Tessellator says:
	 *
	 *    "PointMode. Controls generation of points rather than triangles
	 *     or lines. This functionality defaults to disabled, and is
	 *     enabled if either shader stage includes the execution mode.
	 *
	 * and about Triangles, Quads, IsoLines, VertexOrderCw, VertexOrderCcw,
	 * PointMode, SpacingEqual, SpacingFractionalEven, SpacingFractionalOdd,
	 * and OutputVertices, it says:
	 *
	 *    "One mode must be set in at least one of the tessellation
	 *     shader stages."
	 *
	 * So, the fields can be set in either the TCS or TES, but they must
	 * agree if set in both.  Our backend looks at TES, so bitwise-or in
	 * the values from the TCS.
	 */
	assert(tcs_info->tess.tcs_vertices_out == 0 ||
	       tes_info->tess.tcs_vertices_out == 0 ||
	       tcs_info->tess.tcs_vertices_out == tes_info->tess.tcs_vertices_out);
	tes_info->tess.tcs_vertices_out |= tcs_info->tess.tcs_vertices_out;

	assert(tcs_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
	       tes_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
	       tcs_info->tess.spacing == tes_info->tess.spacing);
	tes_info->tess.spacing |= tcs_info->tess.spacing;

	assert(tcs_info->tess.primitive_mode == 0 ||
	       tes_info->tess.primitive_mode == 0 ||
	       tcs_info->tess.primitive_mode == tes_info->tess.primitive_mode);
	tes_info->tess.primitive_mode |= tcs_info->tess.primitive_mode;
	tes_info->tess.ccw |= tcs_info->tess.ccw;
	tes_info->tess.point_mode |= tcs_info->tess.point_mode;
}

static
void radv_create_shaders(struct radv_pipeline *pipeline,
                         struct radv_device *device,
                         struct radv_pipeline_cache *cache,
                         struct radv_pipeline_key key,
                         const VkPipelineShaderStageCreateInfo **pStages)
{
	struct radv_shader_module fs_m = {0};
	struct radv_shader_module *modules[MESA_SHADER_STAGES] = { 0, };
	nir_shader *nir[MESA_SHADER_STAGES] = {0};
	void *codes[MESA_SHADER_STAGES] = {0};
	unsigned code_sizes[MESA_SHADER_STAGES] = {0};
	struct radv_shader_variant_key keys[MESA_SHADER_STAGES] = {{{{0}}}};
	unsigned char hash[20], gs_copy_hash[20];

	for (unsigned i = 0; i < MESA_SHADER_STAGES; ++i) {
		if (pStages[i]) {
			modules[i] = radv_shader_module_from_handle(pStages[i]->module);
			if (modules[i]->nir)
				_mesa_sha1_compute(modules[i]->nir->info.name,
				                   strlen(modules[i]->nir->info.name),
				                   modules[i]->sha1);
		}
	}

	radv_hash_shaders(hash, pStages, pipeline->layout, &key, get_hash_flags(device));
	memcpy(gs_copy_hash, hash, 20);
	gs_copy_hash[0] ^= 1;

	if (modules[MESA_SHADER_GEOMETRY]) {
		struct radv_shader_variant *variants[MESA_SHADER_STAGES] = {0};
		radv_create_shader_variants_from_pipeline_cache(device, cache, gs_copy_hash, variants);
		pipeline->gs_copy_shader = variants[MESA_SHADER_GEOMETRY];
	}

	if (radv_create_shader_variants_from_pipeline_cache(device, cache, hash, pipeline->shaders) &&
	    (!modules[MESA_SHADER_GEOMETRY] || pipeline->gs_copy_shader)) {
		for (unsigned i = 0; i < MESA_SHADER_STAGES; ++i) {
			if (pipeline->shaders[i])
				pipeline->active_stages |= mesa_to_vk_shader_stage(i);
		}
		return;
	}

	if (!modules[MESA_SHADER_FRAGMENT] && !modules[MESA_SHADER_COMPUTE]) {
		nir_builder fs_b;
		nir_builder_init_simple_shader(&fs_b, NULL, MESA_SHADER_FRAGMENT, NULL);
		fs_b.shader->info.name = ralloc_strdup(fs_b.shader, "noop_fs");
		fs_m.nir = fs_b.shader;
		modules[MESA_SHADER_FRAGMENT] = &fs_m;
	}

	/* Determine first and last stage. */
	unsigned first = MESA_SHADER_STAGES;
	unsigned last = 0;
	for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
		if (!pStages[i])
			continue;
		if (first == MESA_SHADER_STAGES)
			first = i;
		last = i;
	}

	for (unsigned i = 0; i < MESA_SHADER_STAGES; ++i) {
		const VkPipelineShaderStageCreateInfo *stage = pStages[i];

		if (!modules[i])
			continue;

		nir[i] = radv_shader_compile_to_nir(device, modules[i],
						    stage ? stage->pName : "main", i,
						    stage ? stage->pSpecializationInfo : NULL);
		pipeline->active_stages |= mesa_to_vk_shader_stage(i);

		/* We don't want to alter meta shaders IR directly so clone it
		 * first.
		 */
		if (nir[i]->info.name) {
			nir[i] = nir_shader_clone(NULL, nir[i]);
		}

		if (first != last) {
			nir_variable_mode mask = 0;

			if (i != first)
				mask = mask | nir_var_shader_in;

			if (i != last)
				mask = mask | nir_var_shader_out;

			nir_lower_io_to_scalar_early(nir[i], mask);
			radv_optimize_nir(nir[i]);
		}
	}

	if (nir[MESA_SHADER_TESS_CTRL]) {
		nir_lower_tes_patch_vertices(nir[MESA_SHADER_TESS_EVAL], nir[MESA_SHADER_TESS_CTRL]->info.tess.tcs_vertices_out);
		merge_tess_info(&nir[MESA_SHADER_TESS_EVAL]->info, &nir[MESA_SHADER_TESS_CTRL]->info);
	}

	radv_link_shaders(pipeline, nir);

	for (int i = 0; i < MESA_SHADER_STAGES; ++i) {
		if (modules[i] && radv_can_dump_shader(device, modules[i]))
			nir_print_shader(nir[i], stderr);
	}

	radv_fill_shader_keys(keys, &key, nir);

	if (nir[MESA_SHADER_FRAGMENT]) {
		if (!pipeline->shaders[MESA_SHADER_FRAGMENT]) {
			pipeline->shaders[MESA_SHADER_FRAGMENT] =
			       radv_shader_variant_create(device, modules[MESA_SHADER_FRAGMENT], &nir[MESA_SHADER_FRAGMENT], 1,
			                                  pipeline->layout, keys + MESA_SHADER_FRAGMENT,
			                                  &codes[MESA_SHADER_FRAGMENT], &code_sizes[MESA_SHADER_FRAGMENT]);
		}

		/* TODO: These are no longer used as keys we should refactor this */
		keys[MESA_SHADER_VERTEX].vs.export_prim_id =
		        pipeline->shaders[MESA_SHADER_FRAGMENT]->info.info.ps.prim_id_input;
		keys[MESA_SHADER_VERTEX].vs.export_layer_id =
		        pipeline->shaders[MESA_SHADER_FRAGMENT]->info.info.ps.layer_input;
		keys[MESA_SHADER_TESS_EVAL].tes.export_prim_id =
		        pipeline->shaders[MESA_SHADER_FRAGMENT]->info.info.ps.prim_id_input;
		keys[MESA_SHADER_TESS_EVAL].tes.export_layer_id =
		        pipeline->shaders[MESA_SHADER_FRAGMENT]->info.info.ps.layer_input;
	}

	if (device->physical_device->rad_info.chip_class >= GFX9 && modules[MESA_SHADER_TESS_CTRL]) {
		if (!pipeline->shaders[MESA_SHADER_TESS_CTRL]) {
			struct nir_shader *combined_nir[] = {nir[MESA_SHADER_VERTEX], nir[MESA_SHADER_TESS_CTRL]};
			struct radv_shader_variant_key key = keys[MESA_SHADER_TESS_CTRL];
			key.tcs.vs_key = keys[MESA_SHADER_VERTEX].vs;
			pipeline->shaders[MESA_SHADER_TESS_CTRL] = radv_shader_variant_create(device, modules[MESA_SHADER_TESS_CTRL], combined_nir, 2,
			                                                                      pipeline->layout,
			                                                                      &key, &codes[MESA_SHADER_TESS_CTRL],
			                                                                      &code_sizes[MESA_SHADER_TESS_CTRL]);
		}
		modules[MESA_SHADER_VERTEX] = NULL;
		keys[MESA_SHADER_TESS_EVAL].tes.num_patches = pipeline->shaders[MESA_SHADER_TESS_CTRL]->info.tcs.num_patches;
		keys[MESA_SHADER_TESS_EVAL].tes.tcs_num_outputs = util_last_bit64(pipeline->shaders[MESA_SHADER_TESS_CTRL]->info.info.tcs.outputs_written);
	}

	if (device->physical_device->rad_info.chip_class >= GFX9 && modules[MESA_SHADER_GEOMETRY]) {
		gl_shader_stage pre_stage = modules[MESA_SHADER_TESS_EVAL] ? MESA_SHADER_TESS_EVAL : MESA_SHADER_VERTEX;
		if (!pipeline->shaders[MESA_SHADER_GEOMETRY]) {
			struct nir_shader *combined_nir[] = {nir[pre_stage], nir[MESA_SHADER_GEOMETRY]};
			pipeline->shaders[MESA_SHADER_GEOMETRY] = radv_shader_variant_create(device, modules[MESA_SHADER_GEOMETRY], combined_nir, 2,
			                                                                     pipeline->layout,
			                                                                     &keys[pre_stage] , &codes[MESA_SHADER_GEOMETRY],
		                                                                     &code_sizes[MESA_SHADER_GEOMETRY]);
		}
		modules[pre_stage] = NULL;
	}

	for (int i = 0; i < MESA_SHADER_STAGES; ++i) {
		if(modules[i] && !pipeline->shaders[i]) {
			if (i == MESA_SHADER_TESS_CTRL) {
				keys[MESA_SHADER_TESS_CTRL].tcs.num_inputs = util_last_bit64(pipeline->shaders[MESA_SHADER_VERTEX]->info.info.vs.ls_outputs_written);
			}
			if (i == MESA_SHADER_TESS_EVAL) {
				keys[MESA_SHADER_TESS_EVAL].tes.num_patches = pipeline->shaders[MESA_SHADER_TESS_CTRL]->info.tcs.num_patches;
				keys[MESA_SHADER_TESS_EVAL].tes.tcs_num_outputs = util_last_bit64(pipeline->shaders[MESA_SHADER_TESS_CTRL]->info.info.tcs.outputs_written);
			}
			pipeline->shaders[i] = radv_shader_variant_create(device, modules[i], &nir[i], 1,
									  pipeline->layout,
									  keys + i, &codes[i],
									  &code_sizes[i]);
		}
	}

	if(modules[MESA_SHADER_GEOMETRY]) {
		void *gs_copy_code = NULL;
		unsigned gs_copy_code_size = 0;
		if (!pipeline->gs_copy_shader) {
			pipeline->gs_copy_shader = radv_create_gs_copy_shader(
					device, nir[MESA_SHADER_GEOMETRY], &gs_copy_code,
					&gs_copy_code_size,
					keys[MESA_SHADER_GEOMETRY].has_multiview_view_index);
		}

		if (pipeline->gs_copy_shader) {
			void *code[MESA_SHADER_STAGES] = {0};
			unsigned code_size[MESA_SHADER_STAGES] = {0};
			struct radv_shader_variant *variants[MESA_SHADER_STAGES] = {0};

			code[MESA_SHADER_GEOMETRY] = gs_copy_code;
			code_size[MESA_SHADER_GEOMETRY] = gs_copy_code_size;
			variants[MESA_SHADER_GEOMETRY] = pipeline->gs_copy_shader;

			radv_pipeline_cache_insert_shaders(device, cache,
							   gs_copy_hash,
							   variants,
							   (const void**)code,
							   code_size);
		}
		free(gs_copy_code);
	}

	radv_pipeline_cache_insert_shaders(device, cache, hash, pipeline->shaders,
					   (const void**)codes, code_sizes);

	for (int i = 0; i < MESA_SHADER_STAGES; ++i) {
		free(codes[i]);
		if (modules[i]) {
			if (!pipeline->device->keep_shader_info)
				ralloc_free(nir[i]);

			if (radv_can_dump_shader_stats(device, modules[i]))
				radv_shader_dump_stats(device,
						       pipeline->shaders[i],
						       i, stderr);
		}
	}

	if (fs_m.nir)
		ralloc_free(fs_m.nir);
}

static uint32_t
radv_pipeline_stage_to_user_data_0(struct radv_pipeline *pipeline,
				   gl_shader_stage stage, enum chip_class chip_class)
{
	bool has_gs = radv_pipeline_has_gs(pipeline);
	bool has_tess = radv_pipeline_has_tess(pipeline);
	switch (stage) {
	case MESA_SHADER_FRAGMENT:
		return R_00B030_SPI_SHADER_USER_DATA_PS_0;
	case MESA_SHADER_VERTEX:
		if (chip_class >= GFX9) {
			return has_tess ? R_00B430_SPI_SHADER_USER_DATA_LS_0 :
			       has_gs ? R_00B330_SPI_SHADER_USER_DATA_ES_0 :
			       R_00B130_SPI_SHADER_USER_DATA_VS_0;
		}
		if (has_tess)
			return R_00B530_SPI_SHADER_USER_DATA_LS_0;
		else
			return has_gs ? R_00B330_SPI_SHADER_USER_DATA_ES_0 : R_00B130_SPI_SHADER_USER_DATA_VS_0;
	case MESA_SHADER_GEOMETRY:
		return chip_class >= GFX9 ? R_00B330_SPI_SHADER_USER_DATA_ES_0 :
		                            R_00B230_SPI_SHADER_USER_DATA_GS_0;
	case MESA_SHADER_COMPUTE:
		return R_00B900_COMPUTE_USER_DATA_0;
	case MESA_SHADER_TESS_CTRL:
		return chip_class >= GFX9 ? R_00B430_SPI_SHADER_USER_DATA_LS_0 :
		                            R_00B430_SPI_SHADER_USER_DATA_HS_0;
	case MESA_SHADER_TESS_EVAL:
		if (chip_class >= GFX9) {
			return has_gs ? R_00B330_SPI_SHADER_USER_DATA_ES_0 :
			       R_00B130_SPI_SHADER_USER_DATA_VS_0;
		}
		if (has_gs)
			return R_00B330_SPI_SHADER_USER_DATA_ES_0;
		else
			return R_00B130_SPI_SHADER_USER_DATA_VS_0;
	default:
		unreachable("unknown shader");
	}
}

struct radv_bin_size_entry {
	unsigned bpp;
	VkExtent2D extent;
};

static VkExtent2D
radv_compute_bin_size(struct radv_pipeline *pipeline, const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	static const struct radv_bin_size_entry color_size_table[][3][9] = {
		{
			/* One RB / SE */
			{
				/* One shader engine */
				{        0, {128,  128}},
				{        1, { 64,  128}},
				{        2, { 32,  128}},
				{        3, { 16,  128}},
				{       17, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
			{
				/* Two shader engines */
				{        0, {128,  128}},
				{        2, { 64,  128}},
				{        3, { 32,  128}},
				{        5, { 16,  128}},
				{       17, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
			{
				/* Four shader engines */
				{        0, {128,  128}},
				{        3, { 64,  128}},
				{        5, { 16,  128}},
				{       17, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
		},
		{
			/* Two RB / SE */
			{
				/* One shader engine */
				{        0, {128,  128}},
				{        2, { 64,  128}},
				{        3, { 32,  128}},
				{        5, { 16,  128}},
				{       33, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
			{
				/* Two shader engines */
				{        0, {128,  128}},
				{        3, { 64,  128}},
				{        5, { 32,  128}},
				{        9, { 16,  128}},
				{       33, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
			{
				/* Four shader engines */
				{        0, {256,  256}},
				{        2, {128,  256}},
				{        3, {128,  128}},
				{        5, { 64,  128}},
				{        9, { 16,  128}},
				{       33, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
		},
		{
			/* Four RB / SE */
			{
				/* One shader engine */
				{        0, {128,  256}},
				{        2, {128,  128}},
				{        3, { 64,  128}},
				{        5, { 32,  128}},
				{        9, { 16,  128}},
				{       33, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
			{
				/* Two shader engines */
				{        0, {256,  256}},
				{        2, {128,  256}},
				{        3, {128,  128}},
				{        5, { 64,  128}},
				{        9, { 32,  128}},
				{       17, { 16,  128}},
				{       33, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
			{
				/* Four shader engines */
				{        0, {256,  512}},
				{        2, {256,  256}},
				{        3, {128,  256}},
				{        5, {128,  128}},
				{        9, { 64,  128}},
				{       17, { 16,  128}},
				{       33, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
		},
	};
	static const struct radv_bin_size_entry ds_size_table[][3][9] = {
		{
			// One RB / SE
			{
				// One shader engine
				{        0, {128,  256}},
				{        2, {128,  128}},
				{        4, { 64,  128}},
				{        7, { 32,  128}},
				{       13, { 16,  128}},
				{       49, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
			{
				// Two shader engines
				{        0, {256,  256}},
				{        2, {128,  256}},
				{        4, {128,  128}},
				{        7, { 64,  128}},
				{       13, { 32,  128}},
				{       25, { 16,  128}},
				{       49, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
			{
				// Four shader engines
				{        0, {256,  512}},
				{        2, {256,  256}},
				{        4, {128,  256}},
				{        7, {128,  128}},
				{       13, { 64,  128}},
				{       25, { 16,  128}},
				{       49, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
		},
		{
			// Two RB / SE
			{
				// One shader engine
				{        0, {256,  256}},
				{        2, {128,  256}},
				{        4, {128,  128}},
				{        7, { 64,  128}},
				{       13, { 32,  128}},
				{       25, { 16,  128}},
				{       97, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
			{
				// Two shader engines
				{        0, {256,  512}},
				{        2, {256,  256}},
				{        4, {128,  256}},
				{        7, {128,  128}},
				{       13, { 64,  128}},
				{       25, { 32,  128}},
				{       49, { 16,  128}},
				{       97, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
			{
				// Four shader engines
				{        0, {512,  512}},
				{        2, {256,  512}},
				{        4, {256,  256}},
				{        7, {128,  256}},
				{       13, {128,  128}},
				{       25, { 64,  128}},
				{       49, { 16,  128}},
				{       97, {  0,    0}},
				{ UINT_MAX, {  0,    0}},
			},
		},
		{
			// Four RB / SE
			{
				// One shader engine
				{        0, {256,  512}},
				{        2, {256,  256}},
				{        4, {128,  256}},
				{        7, {128,  128}},
				{       13, { 64,  128}},
				{       25, { 32,  128}},
				{       49, { 16,  128}},
				{ UINT_MAX, {  0,    0}},
			},
			{
				// Two shader engines
				{        0, {512,  512}},
				{        2, {256,  512}},
				{        4, {256,  256}},
				{        7, {128,  256}},
				{       13, {128,  128}},
				{       25, { 64,  128}},
				{       49, { 32,  128}},
				{       97, { 16,  128}},
				{ UINT_MAX, {  0,    0}},
			},
			{
				// Four shader engines
				{        0, {512,  512}},
				{        4, {256,  512}},
				{        7, {256,  256}},
				{       13, {128,  256}},
				{       25, {128,  128}},
				{       49, { 64,  128}},
				{       97, { 16,  128}},
				{ UINT_MAX, {  0,    0}},
			},
		},
	};

	RADV_FROM_HANDLE(radv_render_pass, pass, pCreateInfo->renderPass);
	struct radv_subpass *subpass = pass->subpasses + pCreateInfo->subpass;
	VkExtent2D extent = {512, 512};

	unsigned log_num_rb_per_se =
	    util_logbase2_ceil(pipeline->device->physical_device->rad_info.num_render_backends /
	                       pipeline->device->physical_device->rad_info.max_se);
	unsigned log_num_se = util_logbase2_ceil(pipeline->device->physical_device->rad_info.max_se);

	unsigned total_samples = 1u << G_028BE0_MSAA_NUM_SAMPLES(pipeline->graphics.ms.pa_sc_mode_cntl_1);
	unsigned ps_iter_samples = 1u << G_028804_PS_ITER_SAMPLES(pipeline->graphics.ms.db_eqaa);
	unsigned effective_samples = total_samples;
	unsigned color_bytes_per_pixel = 0;

	const VkPipelineColorBlendStateCreateInfo *vkblend = pCreateInfo->pColorBlendState;
	if (vkblend) {
		for (unsigned i = 0; i < subpass->color_count; i++) {
			if (!vkblend->pAttachments[i].colorWriteMask)
				continue;

			if (subpass->color_attachments[i].attachment == VK_ATTACHMENT_UNUSED)
				continue;

			VkFormat format = pass->attachments[subpass->color_attachments[i].attachment].format;
			color_bytes_per_pixel += vk_format_get_blocksize(format);
		}

		/* MSAA images typically don't use all samples all the time. */
		if (effective_samples >= 2 && ps_iter_samples <= 1)
			effective_samples = 2;
		color_bytes_per_pixel *= effective_samples;
	}

	const struct radv_bin_size_entry *color_entry = color_size_table[log_num_rb_per_se][log_num_se];
	while(color_entry->bpp <= color_bytes_per_pixel)
		++color_entry;

	extent = color_entry->extent;

	if (subpass->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED) {
		struct radv_render_pass_attachment *attachment = pass->attachments + subpass->depth_stencil_attachment.attachment;

		/* Coefficients taken from AMDVLK */
		unsigned depth_coeff = vk_format_is_depth(attachment->format) ? 5 : 0;
		unsigned stencil_coeff = vk_format_is_stencil(attachment->format) ? 1 : 0;
		unsigned ds_bytes_per_pixel = 4 * (depth_coeff + stencil_coeff) * total_samples;

		const struct radv_bin_size_entry *ds_entry = ds_size_table[log_num_rb_per_se][log_num_se];
		while(ds_entry->bpp <= ds_bytes_per_pixel)
			++ds_entry;

		extent.width = MIN2(extent.width, ds_entry->extent.width);
		extent.height = MIN2(extent.height, ds_entry->extent.height);
	}

	return extent;
}

static void
radv_pipeline_generate_binning_state(struct radeon_winsys_cs *cs,
				     struct radv_pipeline *pipeline,
				     const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	if (pipeline->device->physical_device->rad_info.chip_class < GFX9)
		return;

	uint32_t pa_sc_binner_cntl_0 =
	                S_028C44_BINNING_MODE(V_028C44_DISABLE_BINNING_USE_LEGACY_SC) |
	                S_028C44_DISABLE_START_OF_PRIM(1);
	uint32_t db_dfsm_control = S_028060_PUNCHOUT_MODE(V_028060_FORCE_OFF);

	VkExtent2D bin_size = radv_compute_bin_size(pipeline, pCreateInfo);

	unsigned context_states_per_bin; /* allowed range: [1, 6] */
	unsigned persistent_states_per_bin; /* allowed range: [1, 32] */
	unsigned fpovs_per_batch; /* allowed range: [0, 255], 0 = unlimited */

	switch (pipeline->device->physical_device->rad_info.family) {
	case CHIP_VEGA10:
	case CHIP_VEGA12:
		context_states_per_bin = 1;
		persistent_states_per_bin = 1;
		fpovs_per_batch = 63;
		break;
	case CHIP_RAVEN:
		context_states_per_bin = 6;
		persistent_states_per_bin = 32;
		fpovs_per_batch = 63;
		break;
	default:
		unreachable("unhandled family while determining binning state.");
	}

	if (pipeline->device->pbb_allowed && bin_size.width && bin_size.height) {
		pa_sc_binner_cntl_0 =
	                S_028C44_BINNING_MODE(V_028C44_BINNING_ALLOWED) |
	                S_028C44_BIN_SIZE_X(bin_size.width == 16) |
	                S_028C44_BIN_SIZE_Y(bin_size.height == 16) |
	                S_028C44_BIN_SIZE_X_EXTEND(util_logbase2(MAX2(bin_size.width, 32)) - 5) |
	                S_028C44_BIN_SIZE_Y_EXTEND(util_logbase2(MAX2(bin_size.height, 32)) - 5) |
	                S_028C44_CONTEXT_STATES_PER_BIN(context_states_per_bin - 1) |
	                S_028C44_PERSISTENT_STATES_PER_BIN(persistent_states_per_bin - 1) |
	                S_028C44_DISABLE_START_OF_PRIM(1) |
	                S_028C44_FPOVS_PER_BATCH(fpovs_per_batch) |
	                S_028C44_OPTIMAL_BIN_SELECTION(1);
	}

	radeon_set_context_reg(cs, R_028C44_PA_SC_BINNER_CNTL_0,
			       pa_sc_binner_cntl_0);
	radeon_set_context_reg(cs, R_028060_DB_DFSM_CONTROL,
			       db_dfsm_control);
}


static void
radv_pipeline_generate_depth_stencil_state(struct radeon_winsys_cs *cs,
                                           struct radv_pipeline *pipeline,
                                           const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                           const struct radv_graphics_pipeline_create_info *extra)
{
	const VkPipelineDepthStencilStateCreateInfo *vkds = pCreateInfo->pDepthStencilState;
	RADV_FROM_HANDLE(radv_render_pass, pass, pCreateInfo->renderPass);
	struct radv_subpass *subpass = pass->subpasses + pCreateInfo->subpass;
	struct radv_shader_variant *ps = pipeline->shaders[MESA_SHADER_FRAGMENT];
	struct radv_render_pass_attachment *attachment = NULL;
	uint32_t db_depth_control = 0, db_stencil_control = 0;
	uint32_t db_render_control = 0, db_render_override2 = 0;
	uint32_t db_render_override = 0;

	if (subpass->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED)
		attachment = pass->attachments + subpass->depth_stencil_attachment.attachment;

	bool has_depth_attachment = attachment && vk_format_is_depth(attachment->format);
	bool has_stencil_attachment = attachment && vk_format_is_stencil(attachment->format);

	if (vkds && has_depth_attachment) {
		db_depth_control = S_028800_Z_ENABLE(vkds->depthTestEnable ? 1 : 0) |
		                   S_028800_Z_WRITE_ENABLE(vkds->depthWriteEnable ? 1 : 0) |
		                   S_028800_ZFUNC(vkds->depthCompareOp) |
		                   S_028800_DEPTH_BOUNDS_ENABLE(vkds->depthBoundsTestEnable ? 1 : 0);

		/* from amdvlk: For 4xAA and 8xAA need to decompress on flush for better performance */
		db_render_override2 |= S_028010_DECOMPRESS_Z_ON_FLUSH(attachment->samples > 2);
	}

	if (has_stencil_attachment && vkds && vkds->stencilTestEnable) {
		db_depth_control |= S_028800_STENCIL_ENABLE(1) | S_028800_BACKFACE_ENABLE(1);
		db_depth_control |= S_028800_STENCILFUNC(vkds->front.compareOp);
		db_stencil_control |= S_02842C_STENCILFAIL(si_translate_stencil_op(vkds->front.failOp));
		db_stencil_control |= S_02842C_STENCILZPASS(si_translate_stencil_op(vkds->front.passOp));
		db_stencil_control |= S_02842C_STENCILZFAIL(si_translate_stencil_op(vkds->front.depthFailOp));

		db_depth_control |= S_028800_STENCILFUNC_BF(vkds->back.compareOp);
		db_stencil_control |= S_02842C_STENCILFAIL_BF(si_translate_stencil_op(vkds->back.failOp));
		db_stencil_control |= S_02842C_STENCILZPASS_BF(si_translate_stencil_op(vkds->back.passOp));
		db_stencil_control |= S_02842C_STENCILZFAIL_BF(si_translate_stencil_op(vkds->back.depthFailOp));
	}

	if (attachment && extra) {
		db_render_control |= S_028000_DEPTH_CLEAR_ENABLE(extra->db_depth_clear);
		db_render_control |= S_028000_STENCIL_CLEAR_ENABLE(extra->db_stencil_clear);

		db_render_control |= S_028000_RESUMMARIZE_ENABLE(extra->db_resummarize);
		db_render_control |= S_028000_DEPTH_COMPRESS_DISABLE(extra->db_flush_depth_inplace);
		db_render_control |= S_028000_STENCIL_COMPRESS_DISABLE(extra->db_flush_stencil_inplace);
		db_render_override2 |= S_028010_DISABLE_ZMASK_EXPCLEAR_OPTIMIZATION(extra->db_depth_disable_expclear);
		db_render_override2 |= S_028010_DISABLE_SMEM_EXPCLEAR_OPTIMIZATION(extra->db_stencil_disable_expclear);
	}

	db_render_override |= S_02800C_FORCE_HIS_ENABLE0(V_02800C_FORCE_DISABLE) |
			      S_02800C_FORCE_HIS_ENABLE1(V_02800C_FORCE_DISABLE);

	if (pipeline->device->enabled_extensions.EXT_depth_range_unrestricted &&
	    !pCreateInfo->pRasterizationState->depthClampEnable &&
	    ps->info.info.ps.writes_z) {
		/* From VK_EXT_depth_range_unrestricted spec:
		 *
		 * "The behavior described in Primitive Clipping still applies.
		 *  If depth clamping is disabled the depth values are still
		 *  clipped to 0 ≤ zc ≤ wc before the viewport transform. If
		 *  depth clamping is enabled the above equation is ignored and
		 *  the depth values are instead clamped to the VkViewport
		 *  minDepth and maxDepth values, which in the case of this
		 *  extension can be outside of the 0.0 to 1.0 range."
		 */
		db_render_override |= S_02800C_DISABLE_VIEWPORT_CLAMP(1);
	}

	radeon_set_context_reg(cs, R_028800_DB_DEPTH_CONTROL, db_depth_control);
	radeon_set_context_reg(cs, R_02842C_DB_STENCIL_CONTROL, db_stencil_control);

	radeon_set_context_reg(cs, R_028000_DB_RENDER_CONTROL, db_render_control);
	radeon_set_context_reg(cs, R_02800C_DB_RENDER_OVERRIDE, db_render_override);
	radeon_set_context_reg(cs, R_028010_DB_RENDER_OVERRIDE2, db_render_override2);
}

static void
radv_pipeline_generate_blend_state(struct radeon_winsys_cs *cs,
                                   struct radv_pipeline *pipeline,
                                   const struct radv_blend_state *blend)
{
	radeon_set_context_reg_seq(cs, R_028780_CB_BLEND0_CONTROL, 8);
	radeon_emit_array(cs, blend->cb_blend_control,
			  8);
	radeon_set_context_reg(cs, R_028808_CB_COLOR_CONTROL, blend->cb_color_control);
	radeon_set_context_reg(cs, R_028B70_DB_ALPHA_TO_MASK, blend->db_alpha_to_mask);

	if (pipeline->device->physical_device->has_rbplus) {

		radeon_set_context_reg_seq(cs, R_028760_SX_MRT0_BLEND_OPT, 8);
		radeon_emit_array(cs, blend->sx_mrt_blend_opt, 8);
	}

	radeon_set_context_reg(cs, R_028714_SPI_SHADER_COL_FORMAT, blend->spi_shader_col_format);

	radeon_set_context_reg(cs, R_028238_CB_TARGET_MASK, blend->cb_target_mask);
	radeon_set_context_reg(cs, R_02823C_CB_SHADER_MASK, blend->cb_shader_mask);

	pipeline->graphics.col_format = blend->spi_shader_col_format;
	pipeline->graphics.cb_target_mask = blend->cb_target_mask;
}


static void
radv_pipeline_generate_raster_state(struct radeon_winsys_cs *cs,
                                    const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	const VkPipelineRasterizationStateCreateInfo *vkraster = pCreateInfo->pRasterizationState;

	radeon_set_context_reg(cs, R_028810_PA_CL_CLIP_CNTL,
	                       S_028810_PS_UCP_MODE(3) |
	                       S_028810_DX_CLIP_SPACE_DEF(1) | // vulkan uses DX conventions.
	                       S_028810_ZCLIP_NEAR_DISABLE(vkraster->depthClampEnable ? 1 : 0) |
	                       S_028810_ZCLIP_FAR_DISABLE(vkraster->depthClampEnable ? 1 : 0) |
	                       S_028810_DX_RASTERIZATION_KILL(vkraster->rasterizerDiscardEnable ? 1 : 0) |
	                       S_028810_DX_LINEAR_ATTR_CLIP_ENA(1));

	radeon_set_context_reg(cs, R_0286D4_SPI_INTERP_CONTROL_0,
	                       S_0286D4_FLAT_SHADE_ENA(1) |
	                       S_0286D4_PNT_SPRITE_ENA(1) |
	                       S_0286D4_PNT_SPRITE_OVRD_X(V_0286D4_SPI_PNT_SPRITE_SEL_S) |
	                       S_0286D4_PNT_SPRITE_OVRD_Y(V_0286D4_SPI_PNT_SPRITE_SEL_T) |
	                       S_0286D4_PNT_SPRITE_OVRD_Z(V_0286D4_SPI_PNT_SPRITE_SEL_0) |
	                       S_0286D4_PNT_SPRITE_OVRD_W(V_0286D4_SPI_PNT_SPRITE_SEL_1) |
	                       S_0286D4_PNT_SPRITE_TOP_1(0)); /* vulkan is top to bottom - 1.0 at bottom */

	radeon_set_context_reg(cs, R_028BE4_PA_SU_VTX_CNTL,
	                       S_028BE4_PIX_CENTER(1) | // TODO verify
	                       S_028BE4_ROUND_MODE(V_028BE4_X_ROUND_TO_EVEN) |
	                       S_028BE4_QUANT_MODE(V_028BE4_X_16_8_FIXED_POINT_1_256TH));

	radeon_set_context_reg(cs, R_028814_PA_SU_SC_MODE_CNTL,
	                       S_028814_FACE(vkraster->frontFace) |
	                       S_028814_CULL_FRONT(!!(vkraster->cullMode & VK_CULL_MODE_FRONT_BIT)) |
	                       S_028814_CULL_BACK(!!(vkraster->cullMode & VK_CULL_MODE_BACK_BIT)) |
	                       S_028814_POLY_MODE(vkraster->polygonMode != VK_POLYGON_MODE_FILL) |
	                       S_028814_POLYMODE_FRONT_PTYPE(si_translate_fill(vkraster->polygonMode)) |
	                       S_028814_POLYMODE_BACK_PTYPE(si_translate_fill(vkraster->polygonMode)) |
	                       S_028814_POLY_OFFSET_FRONT_ENABLE(vkraster->depthBiasEnable ? 1 : 0) |
	                       S_028814_POLY_OFFSET_BACK_ENABLE(vkraster->depthBiasEnable ? 1 : 0) |
	                       S_028814_POLY_OFFSET_PARA_ENABLE(vkraster->depthBiasEnable ? 1 : 0));
}


static void
radv_pipeline_generate_multisample_state(struct radeon_winsys_cs *cs,
                                         struct radv_pipeline *pipeline)
{
	struct radv_multisample_state *ms = &pipeline->graphics.ms;

	radeon_set_context_reg_seq(cs, R_028C38_PA_SC_AA_MASK_X0Y0_X1Y0, 2);
	radeon_emit(cs, ms->pa_sc_aa_mask[0]);
	radeon_emit(cs, ms->pa_sc_aa_mask[1]);

	radeon_set_context_reg(cs, R_028804_DB_EQAA, ms->db_eqaa);
	radeon_set_context_reg(cs, R_028A4C_PA_SC_MODE_CNTL_1, ms->pa_sc_mode_cntl_1);

	if (pipeline->shaders[MESA_SHADER_FRAGMENT]->info.info.ps.needs_sample_positions) {
		uint32_t offset;
		struct radv_userdata_info *loc = radv_lookup_user_sgpr(pipeline, MESA_SHADER_FRAGMENT, AC_UD_PS_SAMPLE_POS_OFFSET);
		uint32_t base_reg = pipeline->user_data_0[MESA_SHADER_FRAGMENT];
		if (loc->sgpr_idx == -1)
			return;
		assert(loc->num_sgprs == 1);
		assert(!loc->indirect);
		switch (pipeline->graphics.ms.num_samples) {
		default:
			offset = 0;
			break;
		case 2:
			offset = 1;
			break;
		case 4:
			offset = 3;
			break;
		case 8:
			offset = 7;
			break;
		case 16:
			offset = 15;
			break;
		}

		radeon_set_sh_reg(cs, base_reg + loc->sgpr_idx * 4, offset);
	}
}

static void
radv_pipeline_generate_vgt_gs_mode(struct radeon_winsys_cs *cs,
                                   const struct radv_pipeline *pipeline)
{
	const struct radv_vs_output_info *outinfo = get_vs_output_info(pipeline);

	uint32_t vgt_primitiveid_en = false;
	uint32_t vgt_gs_mode = 0;

	if (radv_pipeline_has_gs(pipeline)) {
		const struct radv_shader_variant *gs =
			pipeline->shaders[MESA_SHADER_GEOMETRY];

		vgt_gs_mode = ac_vgt_gs_mode(gs->info.gs.vertices_out,
		                             pipeline->device->physical_device->rad_info.chip_class);
	} else if (outinfo->export_prim_id) {
		vgt_gs_mode = S_028A40_MODE(V_028A40_GS_SCENARIO_A);
		vgt_primitiveid_en = true;
	}

	radeon_set_context_reg(cs, R_028A84_VGT_PRIMITIVEID_EN, vgt_primitiveid_en);
	radeon_set_context_reg(cs, R_028A40_VGT_GS_MODE, vgt_gs_mode);
}

static void
radv_pipeline_generate_hw_vs(struct radeon_winsys_cs *cs,
			     struct radv_pipeline *pipeline,
			     struct radv_shader_variant *shader)
{
	uint64_t va = radv_buffer_get_va(shader->bo) + shader->bo_offset;

	radeon_set_sh_reg_seq(cs, R_00B120_SPI_SHADER_PGM_LO_VS, 4);
	radeon_emit(cs, va >> 8);
	radeon_emit(cs, S_00B124_MEM_BASE(va >> 40));
	radeon_emit(cs, shader->rsrc1);
	radeon_emit(cs, shader->rsrc2);

	const struct radv_vs_output_info *outinfo = get_vs_output_info(pipeline);
	unsigned clip_dist_mask, cull_dist_mask, total_mask;
	clip_dist_mask = outinfo->clip_dist_mask;
	cull_dist_mask = outinfo->cull_dist_mask;
	total_mask = clip_dist_mask | cull_dist_mask;
	bool misc_vec_ena = outinfo->writes_pointsize ||
		outinfo->writes_layer ||
		outinfo->writes_viewport_index;

	radeon_set_context_reg(cs, R_0286C4_SPI_VS_OUT_CONFIG,
	                       S_0286C4_VS_EXPORT_COUNT(MAX2(1, outinfo->param_exports) - 1));

	radeon_set_context_reg(cs, R_02870C_SPI_SHADER_POS_FORMAT,
	                       S_02870C_POS0_EXPORT_FORMAT(V_02870C_SPI_SHADER_4COMP) |
	                       S_02870C_POS1_EXPORT_FORMAT(outinfo->pos_exports > 1 ?
	                                                   V_02870C_SPI_SHADER_4COMP :
	                                                   V_02870C_SPI_SHADER_NONE) |
	                       S_02870C_POS2_EXPORT_FORMAT(outinfo->pos_exports > 2 ?
	                                                   V_02870C_SPI_SHADER_4COMP :
	                                                   V_02870C_SPI_SHADER_NONE) |
	                       S_02870C_POS3_EXPORT_FORMAT(outinfo->pos_exports > 3 ?
	                                                   V_02870C_SPI_SHADER_4COMP :
	                                                   V_02870C_SPI_SHADER_NONE));

	radeon_set_context_reg(cs, R_028818_PA_CL_VTE_CNTL,
			       S_028818_VTX_W0_FMT(1) |
			       S_028818_VPORT_X_SCALE_ENA(1) | S_028818_VPORT_X_OFFSET_ENA(1) |
			       S_028818_VPORT_Y_SCALE_ENA(1) | S_028818_VPORT_Y_OFFSET_ENA(1) |
			       S_028818_VPORT_Z_SCALE_ENA(1) | S_028818_VPORT_Z_OFFSET_ENA(1));

	radeon_set_context_reg(cs, R_02881C_PA_CL_VS_OUT_CNTL,
	                       S_02881C_USE_VTX_POINT_SIZE(outinfo->writes_pointsize) |
	                       S_02881C_USE_VTX_RENDER_TARGET_INDX(outinfo->writes_layer) |
	                       S_02881C_USE_VTX_VIEWPORT_INDX(outinfo->writes_viewport_index) |
	                       S_02881C_VS_OUT_MISC_VEC_ENA(misc_vec_ena) |
	                       S_02881C_VS_OUT_MISC_SIDE_BUS_ENA(misc_vec_ena) |
	                       S_02881C_VS_OUT_CCDIST0_VEC_ENA((total_mask & 0x0f) != 0) |
	                       S_02881C_VS_OUT_CCDIST1_VEC_ENA((total_mask & 0xf0) != 0) |
	                       cull_dist_mask << 8 |
	                       clip_dist_mask);

	if (pipeline->device->physical_device->rad_info.chip_class <= VI)
		radeon_set_context_reg(cs, R_028AB4_VGT_REUSE_OFF,
		                       outinfo->writes_viewport_index);
}

static void
radv_pipeline_generate_hw_es(struct radeon_winsys_cs *cs,
			     struct radv_pipeline *pipeline,
			     struct radv_shader_variant *shader)
{
	uint64_t va = radv_buffer_get_va(shader->bo) + shader->bo_offset;

	radeon_set_sh_reg_seq(cs, R_00B320_SPI_SHADER_PGM_LO_ES, 4);
	radeon_emit(cs, va >> 8);
	radeon_emit(cs, S_00B324_MEM_BASE(va >> 40));
	radeon_emit(cs, shader->rsrc1);
	radeon_emit(cs, shader->rsrc2);
}

static void
radv_pipeline_generate_hw_ls(struct radeon_winsys_cs *cs,
			     struct radv_pipeline *pipeline,
			     struct radv_shader_variant *shader,
			     const struct radv_tessellation_state *tess)
{
	uint64_t va = radv_buffer_get_va(shader->bo) + shader->bo_offset;
	uint32_t rsrc2 = shader->rsrc2;

	radeon_set_sh_reg_seq(cs, R_00B520_SPI_SHADER_PGM_LO_LS, 2);
	radeon_emit(cs, va >> 8);
	radeon_emit(cs, S_00B524_MEM_BASE(va >> 40));

	rsrc2 |= S_00B52C_LDS_SIZE(tess->lds_size);
	if (pipeline->device->physical_device->rad_info.chip_class == CIK &&
	    pipeline->device->physical_device->rad_info.family != CHIP_HAWAII)
		radeon_set_sh_reg(cs, R_00B52C_SPI_SHADER_PGM_RSRC2_LS, rsrc2);

	radeon_set_sh_reg_seq(cs, R_00B528_SPI_SHADER_PGM_RSRC1_LS, 2);
	radeon_emit(cs, shader->rsrc1);
	radeon_emit(cs, rsrc2);
}

static void
radv_pipeline_generate_hw_hs(struct radeon_winsys_cs *cs,
			     struct radv_pipeline *pipeline,
			     struct radv_shader_variant *shader,
			     const struct radv_tessellation_state *tess)
{
	uint64_t va = radv_buffer_get_va(shader->bo) + shader->bo_offset;

	if (pipeline->device->physical_device->rad_info.chip_class >= GFX9) {
		radeon_set_sh_reg_seq(cs, R_00B410_SPI_SHADER_PGM_LO_LS, 2);
		radeon_emit(cs, va >> 8);
		radeon_emit(cs, S_00B414_MEM_BASE(va >> 40));

		radeon_set_sh_reg_seq(cs, R_00B428_SPI_SHADER_PGM_RSRC1_HS, 2);
		radeon_emit(cs, shader->rsrc1);
		radeon_emit(cs, shader->rsrc2 |
		                S_00B42C_LDS_SIZE(tess->lds_size));
	} else {
		radeon_set_sh_reg_seq(cs, R_00B420_SPI_SHADER_PGM_LO_HS, 4);
		radeon_emit(cs, va >> 8);
		radeon_emit(cs, S_00B424_MEM_BASE(va >> 40));
		radeon_emit(cs, shader->rsrc1);
		radeon_emit(cs, shader->rsrc2);
	}
}

static void
radv_pipeline_generate_vertex_shader(struct radeon_winsys_cs *cs,
				     struct radv_pipeline *pipeline,
				     const struct radv_tessellation_state *tess)
{
	struct radv_shader_variant *vs;

	/* Skip shaders merged into HS/GS */
	vs = pipeline->shaders[MESA_SHADER_VERTEX];
	if (!vs)
		return;

	if (vs->info.vs.as_ls)
		radv_pipeline_generate_hw_ls(cs, pipeline, vs, tess);
	else if (vs->info.vs.as_es)
		radv_pipeline_generate_hw_es(cs, pipeline, vs);
	else
		radv_pipeline_generate_hw_vs(cs, pipeline, vs);
}

static void
radv_pipeline_generate_tess_shaders(struct radeon_winsys_cs *cs,
				    struct radv_pipeline *pipeline,
				    const struct radv_tessellation_state *tess)
{
	if (!radv_pipeline_has_tess(pipeline))
		return;

	struct radv_shader_variant *tes, *tcs;

	tcs = pipeline->shaders[MESA_SHADER_TESS_CTRL];
	tes = pipeline->shaders[MESA_SHADER_TESS_EVAL];

	if (tes) {
		if (tes->info.tes.as_es)
			radv_pipeline_generate_hw_es(cs, pipeline, tes);
		else
			radv_pipeline_generate_hw_vs(cs, pipeline, tes);
	}

	radv_pipeline_generate_hw_hs(cs, pipeline, tcs, tess);

	radeon_set_context_reg(cs, R_028B6C_VGT_TF_PARAM,
			       tess->tf_param);

	if (pipeline->device->physical_device->rad_info.chip_class >= CIK)
		radeon_set_context_reg_idx(cs, R_028B58_VGT_LS_HS_CONFIG, 2,
					   tess->ls_hs_config);
	else
		radeon_set_context_reg(cs, R_028B58_VGT_LS_HS_CONFIG,
				       tess->ls_hs_config);
}

static void
radv_pipeline_generate_geometry_shader(struct radeon_winsys_cs *cs,
				       struct radv_pipeline *pipeline,
				       const struct radv_gs_state *gs_state)
{
	struct radv_shader_variant *gs;
	uint64_t va;

	gs = pipeline->shaders[MESA_SHADER_GEOMETRY];
	if (!gs)
		return;

	uint32_t gsvs_itemsize = gs->info.gs.max_gsvs_emit_size >> 2;

	radeon_set_context_reg_seq(cs, R_028A60_VGT_GSVS_RING_OFFSET_1, 3);
	radeon_emit(cs, gsvs_itemsize);
	radeon_emit(cs, gsvs_itemsize);
	radeon_emit(cs, gsvs_itemsize);

	radeon_set_context_reg(cs, R_028AB0_VGT_GSVS_RING_ITEMSIZE, gsvs_itemsize);

	radeon_set_context_reg(cs, R_028B38_VGT_GS_MAX_VERT_OUT, gs->info.gs.vertices_out);

	uint32_t gs_vert_itemsize = gs->info.gs.gsvs_vertex_size;
	radeon_set_context_reg_seq(cs, R_028B5C_VGT_GS_VERT_ITEMSIZE, 4);
	radeon_emit(cs, gs_vert_itemsize >> 2);
	radeon_emit(cs, 0);
	radeon_emit(cs, 0);
	radeon_emit(cs, 0);

	uint32_t gs_num_invocations = gs->info.gs.invocations;
	radeon_set_context_reg(cs, R_028B90_VGT_GS_INSTANCE_CNT,
			       S_028B90_CNT(MIN2(gs_num_invocations, 127)) |
			       S_028B90_ENABLE(gs_num_invocations > 0));

	radeon_set_context_reg(cs, R_028AAC_VGT_ESGS_RING_ITEMSIZE,
			       gs_state->vgt_esgs_ring_itemsize);

	va = radv_buffer_get_va(gs->bo) + gs->bo_offset;

	if (pipeline->device->physical_device->rad_info.chip_class >= GFX9) {
		radeon_set_sh_reg_seq(cs, R_00B210_SPI_SHADER_PGM_LO_ES, 2);
		radeon_emit(cs, va >> 8);
		radeon_emit(cs, S_00B214_MEM_BASE(va >> 40));

		radeon_set_sh_reg_seq(cs, R_00B228_SPI_SHADER_PGM_RSRC1_GS, 2);
		radeon_emit(cs, gs->rsrc1);
		radeon_emit(cs, gs->rsrc2 | S_00B22C_LDS_SIZE(gs_state->lds_size));

		radeon_set_context_reg(cs, R_028A44_VGT_GS_ONCHIP_CNTL, gs_state->vgt_gs_onchip_cntl);
		radeon_set_context_reg(cs, R_028A94_VGT_GS_MAX_PRIMS_PER_SUBGROUP, gs_state->vgt_gs_max_prims_per_subgroup);
	} else {
		radeon_set_sh_reg_seq(cs, R_00B220_SPI_SHADER_PGM_LO_GS, 4);
		radeon_emit(cs, va >> 8);
		radeon_emit(cs, S_00B224_MEM_BASE(va >> 40));
		radeon_emit(cs, gs->rsrc1);
		radeon_emit(cs, gs->rsrc2);
	}

	radv_pipeline_generate_hw_vs(cs, pipeline, pipeline->gs_copy_shader);
}

static uint32_t offset_to_ps_input(uint32_t offset, bool flat_shade)
{
	uint32_t ps_input_cntl;
	if (offset <= AC_EXP_PARAM_OFFSET_31) {
		ps_input_cntl = S_028644_OFFSET(offset);
		if (flat_shade)
			ps_input_cntl |= S_028644_FLAT_SHADE(1);
	} else {
		/* The input is a DEFAULT_VAL constant. */
		assert(offset >= AC_EXP_PARAM_DEFAULT_VAL_0000 &&
		       offset <= AC_EXP_PARAM_DEFAULT_VAL_1111);
		offset -= AC_EXP_PARAM_DEFAULT_VAL_0000;
		ps_input_cntl = S_028644_OFFSET(0x20) |
			S_028644_DEFAULT_VAL(offset);
	}
	return ps_input_cntl;
}

static void
radv_pipeline_generate_ps_inputs(struct radeon_winsys_cs *cs,
                                 struct radv_pipeline *pipeline)
{
	struct radv_shader_variant *ps = pipeline->shaders[MESA_SHADER_FRAGMENT];
	const struct radv_vs_output_info *outinfo = get_vs_output_info(pipeline);
	uint32_t ps_input_cntl[32];

	unsigned ps_offset = 0;

	if (ps->info.info.ps.prim_id_input) {
		unsigned vs_offset = outinfo->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID];
		if (vs_offset != AC_EXP_PARAM_UNDEFINED) {
			ps_input_cntl[ps_offset] = offset_to_ps_input(vs_offset, true);
			++ps_offset;
		}
	}

	if (ps->info.info.ps.layer_input ||
	    ps->info.info.ps.uses_input_attachments ||
	    ps->info.info.needs_multiview_view_index) {
		unsigned vs_offset = outinfo->vs_output_param_offset[VARYING_SLOT_LAYER];
		if (vs_offset != AC_EXP_PARAM_UNDEFINED)
			ps_input_cntl[ps_offset] = offset_to_ps_input(vs_offset, true);
		else
			ps_input_cntl[ps_offset] = offset_to_ps_input(AC_EXP_PARAM_DEFAULT_VAL_0000, true);
		++ps_offset;
	}

	if (ps->info.info.ps.has_pcoord) {
		unsigned val;
		val = S_028644_PT_SPRITE_TEX(1) | S_028644_OFFSET(0x20);
		ps_input_cntl[ps_offset] = val;
		ps_offset++;
	}

	for (unsigned i = 0; i < 32 && (1u << i) <= ps->info.fs.input_mask; ++i) {
		unsigned vs_offset;
		bool flat_shade;
		if (!(ps->info.fs.input_mask & (1u << i)))
			continue;

		vs_offset = outinfo->vs_output_param_offset[VARYING_SLOT_VAR0 + i];
		if (vs_offset == AC_EXP_PARAM_UNDEFINED) {
			ps_input_cntl[ps_offset] = S_028644_OFFSET(0x20);
			++ps_offset;
			continue;
		}

		flat_shade = !!(ps->info.fs.flat_shaded_mask & (1u << ps_offset));

		ps_input_cntl[ps_offset] = offset_to_ps_input(vs_offset, flat_shade);
		++ps_offset;
	}

	if (ps_offset) {
		radeon_set_context_reg_seq(cs, R_028644_SPI_PS_INPUT_CNTL_0, ps_offset);
		for (unsigned i = 0; i < ps_offset; i++) {
			radeon_emit(cs, ps_input_cntl[i]);
		}
	}
}

static uint32_t
radv_compute_db_shader_control(const struct radv_device *device,
                               const struct radv_shader_variant *ps)
{
	unsigned z_order;
	if (ps->info.fs.early_fragment_test || !ps->info.info.ps.writes_memory)
		z_order = V_02880C_EARLY_Z_THEN_LATE_Z;
	else
		z_order = V_02880C_LATE_Z;

	bool disable_rbplus = device->physical_device->has_rbplus &&
	                      !device->physical_device->rbplus_allowed;

	return  S_02880C_Z_EXPORT_ENABLE(ps->info.info.ps.writes_z) |
		S_02880C_STENCIL_TEST_VAL_EXPORT_ENABLE(ps->info.info.ps.writes_stencil) |
		S_02880C_KILL_ENABLE(!!ps->info.fs.can_discard) |
		S_02880C_MASK_EXPORT_ENABLE(ps->info.info.ps.writes_sample_mask) |
		S_02880C_Z_ORDER(z_order) |
		S_02880C_DEPTH_BEFORE_SHADER(ps->info.fs.early_fragment_test) |
		S_02880C_EXEC_ON_HIER_FAIL(ps->info.info.ps.writes_memory) |
		S_02880C_EXEC_ON_NOOP(ps->info.info.ps.writes_memory) |
		S_02880C_DUAL_QUAD_DISABLE(disable_rbplus);
}

static void
radv_pipeline_generate_fragment_shader(struct radeon_winsys_cs *cs,
				       struct radv_pipeline *pipeline)
{
	struct radv_shader_variant *ps;
	uint64_t va;
	assert (pipeline->shaders[MESA_SHADER_FRAGMENT]);

	ps = pipeline->shaders[MESA_SHADER_FRAGMENT];
	va = radv_buffer_get_va(ps->bo) + ps->bo_offset;

	radeon_set_sh_reg_seq(cs, R_00B020_SPI_SHADER_PGM_LO_PS, 4);
	radeon_emit(cs, va >> 8);
	radeon_emit(cs, S_00B024_MEM_BASE(va >> 40));
	radeon_emit(cs, ps->rsrc1);
	radeon_emit(cs, ps->rsrc2);

	radeon_set_context_reg(cs, R_02880C_DB_SHADER_CONTROL,
	                       radv_compute_db_shader_control(pipeline->device, ps));

	radeon_set_context_reg(cs, R_0286CC_SPI_PS_INPUT_ENA,
			       ps->config.spi_ps_input_ena);

	radeon_set_context_reg(cs, R_0286D0_SPI_PS_INPUT_ADDR,
			       ps->config.spi_ps_input_addr);

	radeon_set_context_reg(cs, R_0286D8_SPI_PS_IN_CONTROL,
			       S_0286D8_NUM_INTERP(ps->info.fs.num_interp));

	radeon_set_context_reg(cs, R_0286E0_SPI_BARYC_CNTL, pipeline->graphics.spi_baryc_cntl);

	radeon_set_context_reg(cs, R_028710_SPI_SHADER_Z_FORMAT,
	                       ac_get_spi_shader_z_format(ps->info.info.ps.writes_z,
	                                                  ps->info.info.ps.writes_stencil,
	                                                  ps->info.info.ps.writes_sample_mask));

	if (pipeline->device->dfsm_allowed) {
		/* optimise this? */
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_FLUSH_DFSM) | EVENT_INDEX(0));
	}
}

static void
radv_pipeline_generate_vgt_vertex_reuse(struct radeon_winsys_cs *cs,
					struct radv_pipeline *pipeline)
{
	if (pipeline->device->physical_device->rad_info.family < CHIP_POLARIS10)
		return;

	unsigned vtx_reuse_depth = 30;
	if (radv_pipeline_has_tess(pipeline) &&
	    radv_get_tess_eval_shader(pipeline)->info.tes.spacing == TESS_SPACING_FRACTIONAL_ODD) {
		vtx_reuse_depth = 14;
	}
	radeon_set_context_reg(cs, R_028C58_VGT_VERTEX_REUSE_BLOCK_CNTL,
	                       S_028C58_VTX_REUSE_DEPTH(vtx_reuse_depth));
}

static uint32_t
radv_compute_vgt_shader_stages_en(const struct radv_pipeline *pipeline)
{
	uint32_t stages = 0;
	if (radv_pipeline_has_tess(pipeline)) {
		stages |= S_028B54_LS_EN(V_028B54_LS_STAGE_ON) |
			S_028B54_HS_EN(1) | S_028B54_DYNAMIC_HS(1);

		if (radv_pipeline_has_gs(pipeline))
			stages |=  S_028B54_ES_EN(V_028B54_ES_STAGE_DS) |
				S_028B54_GS_EN(1) |
				S_028B54_VS_EN(V_028B54_VS_STAGE_COPY_SHADER);
		else
			stages |= S_028B54_VS_EN(V_028B54_VS_STAGE_DS);

	} else if (radv_pipeline_has_gs(pipeline))
		stages |= S_028B54_ES_EN(V_028B54_ES_STAGE_REAL) |
			S_028B54_GS_EN(1) |
			S_028B54_VS_EN(V_028B54_VS_STAGE_COPY_SHADER);

	if (pipeline->device->physical_device->rad_info.chip_class >= GFX9)
		stages |= S_028B54_MAX_PRIMGRP_IN_WAVE(2);

	return stages;
}

static uint32_t
radv_compute_cliprect_rule(const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	const  VkPipelineDiscardRectangleStateCreateInfoEXT *discard_rectangle_info =
			vk_find_struct_const(pCreateInfo->pNext, PIPELINE_DISCARD_RECTANGLE_STATE_CREATE_INFO_EXT);

	if (!discard_rectangle_info)
		return 0xffff;

	unsigned mask = 0;

	for (unsigned i = 0; i < (1u << MAX_DISCARD_RECTANGLES); ++i) {
		/* Interpret i as a bitmask, and then set the bit in the mask if
		 * that combination of rectangles in which the pixel is contained
		 * should pass the cliprect test. */
		unsigned relevant_subset = i & ((1u << discard_rectangle_info->discardRectangleCount) - 1);

		if (discard_rectangle_info->discardRectangleMode == VK_DISCARD_RECTANGLE_MODE_INCLUSIVE_EXT &&
		    !relevant_subset)
			continue;

		if (discard_rectangle_info->discardRectangleMode == VK_DISCARD_RECTANGLE_MODE_EXCLUSIVE_EXT &&
		    relevant_subset)
			continue;

		mask |= 1u << i;
	}

	return mask;
}

static void
radv_pipeline_generate_pm4(struct radv_pipeline *pipeline,
                           const VkGraphicsPipelineCreateInfo *pCreateInfo,
                           const struct radv_graphics_pipeline_create_info *extra,
                           const struct radv_blend_state *blend,
                           const struct radv_tessellation_state *tess,
                           const struct radv_gs_state *gs,
                           unsigned prim, unsigned gs_out)
{
	pipeline->cs.buf = malloc(4 * 256);
	pipeline->cs.max_dw = 256;

	radv_pipeline_generate_depth_stencil_state(&pipeline->cs, pipeline, pCreateInfo, extra);
	radv_pipeline_generate_blend_state(&pipeline->cs, pipeline, blend);
	radv_pipeline_generate_raster_state(&pipeline->cs, pCreateInfo);
	radv_pipeline_generate_multisample_state(&pipeline->cs, pipeline);
	radv_pipeline_generate_vgt_gs_mode(&pipeline->cs, pipeline);
	radv_pipeline_generate_vertex_shader(&pipeline->cs, pipeline, tess);
	radv_pipeline_generate_tess_shaders(&pipeline->cs, pipeline, tess);
	radv_pipeline_generate_geometry_shader(&pipeline->cs, pipeline, gs);
	radv_pipeline_generate_fragment_shader(&pipeline->cs, pipeline);
	radv_pipeline_generate_ps_inputs(&pipeline->cs, pipeline);
	radv_pipeline_generate_vgt_vertex_reuse(&pipeline->cs, pipeline);
	radv_pipeline_generate_binning_state(&pipeline->cs, pipeline, pCreateInfo);

	radeon_set_context_reg(&pipeline->cs, R_0286E8_SPI_TMPRING_SIZE,
			       S_0286E8_WAVES(pipeline->max_waves) |
			       S_0286E8_WAVESIZE(pipeline->scratch_bytes_per_wave >> 10));

	radeon_set_context_reg(&pipeline->cs, R_028B54_VGT_SHADER_STAGES_EN, radv_compute_vgt_shader_stages_en(pipeline));

	if (pipeline->device->physical_device->rad_info.chip_class >= CIK) {
		radeon_set_uconfig_reg_idx(&pipeline->cs, R_030908_VGT_PRIMITIVE_TYPE, 1, prim);
	} else {
		radeon_set_config_reg(&pipeline->cs, R_008958_VGT_PRIMITIVE_TYPE, prim);
	}
	radeon_set_context_reg(&pipeline->cs, R_028A6C_VGT_GS_OUT_PRIM_TYPE, gs_out);

	radeon_set_context_reg(&pipeline->cs, R_02820C_PA_SC_CLIPRECT_RULE, radv_compute_cliprect_rule(pCreateInfo));

	assert(pipeline->cs.cdw <= pipeline->cs.max_dw);
}

static struct radv_ia_multi_vgt_param_helpers
radv_compute_ia_multi_vgt_param_helpers(struct radv_pipeline *pipeline,
                                        const struct radv_tessellation_state *tess,
                                        uint32_t prim)
{
	struct radv_ia_multi_vgt_param_helpers ia_multi_vgt_param = {0};
	const struct radv_device *device = pipeline->device;

	if (radv_pipeline_has_tess(pipeline))
		ia_multi_vgt_param.primgroup_size = tess->num_patches;
	else if (radv_pipeline_has_gs(pipeline))
		ia_multi_vgt_param.primgroup_size = 64;
	else
		ia_multi_vgt_param.primgroup_size = 128; /* recommended without a GS */

	ia_multi_vgt_param.partial_es_wave = false;
	if (pipeline->device->has_distributed_tess) {
		if (radv_pipeline_has_gs(pipeline)) {
			if (device->physical_device->rad_info.chip_class <= VI)
				ia_multi_vgt_param.partial_es_wave = true;
		}
	}
	/* GS requirement. */
	if (SI_GS_PER_ES / ia_multi_vgt_param.primgroup_size >= pipeline->device->gs_table_depth - 3)
		ia_multi_vgt_param.partial_es_wave = true;

	ia_multi_vgt_param.wd_switch_on_eop = false;
	if (device->physical_device->rad_info.chip_class >= CIK) {
		/* WD_SWITCH_ON_EOP has no effect on GPUs with less than
		 * 4 shader engines. Set 1 to pass the assertion below.
		 * The other cases are hardware requirements. */
		if (device->physical_device->rad_info.max_se < 4 ||
		    prim == V_008958_DI_PT_POLYGON ||
		    prim == V_008958_DI_PT_LINELOOP ||
		    prim == V_008958_DI_PT_TRIFAN ||
		    prim == V_008958_DI_PT_TRISTRIP_ADJ ||
		    (pipeline->graphics.prim_restart_enable &&
		     (device->physical_device->rad_info.family < CHIP_POLARIS10 ||
		      (prim != V_008958_DI_PT_POINTLIST &&
		       prim != V_008958_DI_PT_LINESTRIP &&
		       prim != V_008958_DI_PT_TRISTRIP))))
			ia_multi_vgt_param.wd_switch_on_eop = true;
	}

	ia_multi_vgt_param.ia_switch_on_eoi = false;
	if (pipeline->shaders[MESA_SHADER_FRAGMENT]->info.info.ps.prim_id_input)
		ia_multi_vgt_param.ia_switch_on_eoi = true;
	if (radv_pipeline_has_gs(pipeline) &&
	    pipeline->shaders[MESA_SHADER_GEOMETRY]->info.info.uses_prim_id)
		ia_multi_vgt_param.ia_switch_on_eoi = true;
	if (radv_pipeline_has_tess(pipeline)) {
		/* SWITCH_ON_EOI must be set if PrimID is used. */
		if (pipeline->shaders[MESA_SHADER_TESS_CTRL]->info.info.uses_prim_id ||
		    radv_get_tess_eval_shader(pipeline)->info.info.uses_prim_id)
			ia_multi_vgt_param.ia_switch_on_eoi = true;
	}

	ia_multi_vgt_param.partial_vs_wave = false;
	if (radv_pipeline_has_tess(pipeline)) {
		/* Bug with tessellation and GS on Bonaire and older 2 SE chips. */
		if ((device->physical_device->rad_info.family == CHIP_TAHITI ||
		     device->physical_device->rad_info.family == CHIP_PITCAIRN ||
		     device->physical_device->rad_info.family == CHIP_BONAIRE) &&
		    radv_pipeline_has_gs(pipeline))
			ia_multi_vgt_param.partial_vs_wave = true;
		/* Needed for 028B6C_DISTRIBUTION_MODE != 0 */
		if (device->has_distributed_tess) {
			if (radv_pipeline_has_gs(pipeline)) {
				if (device->physical_device->rad_info.family == CHIP_TONGA ||
				    device->physical_device->rad_info.family == CHIP_FIJI ||
				    device->physical_device->rad_info.family == CHIP_POLARIS10 ||
				    device->physical_device->rad_info.family == CHIP_POLARIS11 ||
				    device->physical_device->rad_info.family == CHIP_POLARIS12)
					ia_multi_vgt_param.partial_vs_wave = true;
			} else {
				ia_multi_vgt_param.partial_vs_wave = true;
			}
		}
	}

	ia_multi_vgt_param.base =
		S_028AA8_PRIMGROUP_SIZE(ia_multi_vgt_param.primgroup_size - 1) |
		/* The following field was moved to VGT_SHADER_STAGES_EN in GFX9. */
		S_028AA8_MAX_PRIMGRP_IN_WAVE(device->physical_device->rad_info.chip_class == VI ? 2 : 0) |
		S_030960_EN_INST_OPT_BASIC(device->physical_device->rad_info.chip_class >= GFX9) |
		S_030960_EN_INST_OPT_ADV(device->physical_device->rad_info.chip_class >= GFX9);

	return ia_multi_vgt_param;
}


static void
radv_compute_vertex_input_state(struct radv_pipeline *pipeline,
                                const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	const VkPipelineVertexInputStateCreateInfo *vi_info =
		pCreateInfo->pVertexInputState;
	struct radv_vertex_elements_info *velems = &pipeline->vertex_elements;

	for (uint32_t i = 0; i < vi_info->vertexAttributeDescriptionCount; i++) {
		const VkVertexInputAttributeDescription *desc =
			&vi_info->pVertexAttributeDescriptions[i];
		unsigned loc = desc->location;
		const struct vk_format_description *format_desc;
		int first_non_void;
		uint32_t num_format, data_format;
		format_desc = vk_format_description(desc->format);
		first_non_void = vk_format_get_first_non_void_channel(desc->format);

		num_format = radv_translate_buffer_numformat(format_desc, first_non_void);
		data_format = radv_translate_buffer_dataformat(format_desc, first_non_void);

		velems->rsrc_word3[loc] = S_008F0C_DST_SEL_X(si_map_swizzle(format_desc->swizzle[0])) |
			S_008F0C_DST_SEL_Y(si_map_swizzle(format_desc->swizzle[1])) |
			S_008F0C_DST_SEL_Z(si_map_swizzle(format_desc->swizzle[2])) |
			S_008F0C_DST_SEL_W(si_map_swizzle(format_desc->swizzle[3])) |
			S_008F0C_NUM_FORMAT(num_format) |
			S_008F0C_DATA_FORMAT(data_format);
		velems->format_size[loc] = format_desc->block.bits / 8;
		velems->offset[loc] = desc->offset;
		velems->binding[loc] = desc->binding;
		velems->count = MAX2(velems->count, loc + 1);
	}

	for (uint32_t i = 0; i < vi_info->vertexBindingDescriptionCount; i++) {
		const VkVertexInputBindingDescription *desc =
			&vi_info->pVertexBindingDescriptions[i];

		pipeline->binding_stride[desc->binding] = desc->stride;
	}
}

static VkResult
radv_pipeline_init(struct radv_pipeline *pipeline,
		   struct radv_device *device,
		   struct radv_pipeline_cache *cache,
		   const VkGraphicsPipelineCreateInfo *pCreateInfo,
		   const struct radv_graphics_pipeline_create_info *extra,
		   const VkAllocationCallbacks *alloc)
{
	VkResult result;
	bool has_view_index = false;

	RADV_FROM_HANDLE(radv_render_pass, pass, pCreateInfo->renderPass);
	struct radv_subpass *subpass = pass->subpasses + pCreateInfo->subpass;
	if (subpass->view_mask)
		has_view_index = true;
	if (alloc == NULL)
		alloc = &device->alloc;

	pipeline->device = device;
	pipeline->layout = radv_pipeline_layout_from_handle(pCreateInfo->layout);
	assert(pipeline->layout);

	struct radv_blend_state blend = radv_pipeline_init_blend_state(pipeline, pCreateInfo, extra);

	const VkPipelineShaderStageCreateInfo *pStages[MESA_SHADER_STAGES] = { 0, };
	for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
		gl_shader_stage stage = ffs(pCreateInfo->pStages[i].stage) - 1;
		pStages[stage] = &pCreateInfo->pStages[i];
	}

	radv_create_shaders(pipeline, device, cache, 
	                    radv_generate_graphics_pipeline_key(pipeline, pCreateInfo, &blend, has_view_index),
	                    pStages);

	pipeline->graphics.spi_baryc_cntl = S_0286E0_FRONT_FACE_ALL_BITS(1);
	radv_pipeline_init_multisample_state(pipeline, &blend, pCreateInfo);
	uint32_t gs_out;
	uint32_t prim = si_translate_prim(pCreateInfo->pInputAssemblyState->topology);

	pipeline->graphics.can_use_guardband = radv_prim_can_use_guardband(pCreateInfo->pInputAssemblyState->topology);

	if (radv_pipeline_has_gs(pipeline)) {
		gs_out = si_conv_gl_prim_to_gs_out(pipeline->shaders[MESA_SHADER_GEOMETRY]->info.gs.output_prim);
		pipeline->graphics.can_use_guardband = gs_out == V_028A6C_OUTPRIM_TYPE_TRISTRIP;
	} else {
		gs_out = si_conv_prim_to_gs_out(pCreateInfo->pInputAssemblyState->topology);
	}
	if (extra && extra->use_rectlist) {
		prim = V_008958_DI_PT_RECTLIST;
		gs_out = V_028A6C_OUTPRIM_TYPE_TRISTRIP;
		pipeline->graphics.can_use_guardband = true;
	}
	pipeline->graphics.prim_restart_enable = !!pCreateInfo->pInputAssemblyState->primitiveRestartEnable;
	/* prim vertex count will need TESS changes */
	pipeline->graphics.prim_vertex_count = prim_size_table[prim];

	radv_pipeline_init_dynamic_state(pipeline, pCreateInfo);

	/* Ensure that some export memory is always allocated, for two reasons:
	 *
	 * 1) Correctness: The hardware ignores the EXEC mask if no export
	 *    memory is allocated, so KILL and alpha test do not work correctly
	 *    without this.
	 * 2) Performance: Every shader needs at least a NULL export, even when
	 *    it writes no color/depth output. The NULL export instruction
	 *    stalls without this setting.
	 *
	 * Don't add this to CB_SHADER_MASK.
	 */
	struct radv_shader_variant *ps = pipeline->shaders[MESA_SHADER_FRAGMENT];
	if (!blend.spi_shader_col_format) {
		if (!ps->info.info.ps.writes_z &&
		    !ps->info.info.ps.writes_stencil &&
		    !ps->info.info.ps.writes_sample_mask)
			blend.spi_shader_col_format = V_028714_SPI_SHADER_32_R;
	}

	for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
		if (pipeline->shaders[i]) {
			pipeline->need_indirect_descriptor_sets |= pipeline->shaders[i]->info.need_indirect_descriptor_sets;
		}
	}

	struct radv_gs_state gs = {0};
	if (radv_pipeline_has_gs(pipeline)) {
		gs = calculate_gs_info(pCreateInfo, pipeline);
		calculate_gs_ring_sizes(pipeline, &gs);
	}

	struct radv_tessellation_state tess = {0};
	if (radv_pipeline_has_tess(pipeline)) {
		if (prim == V_008958_DI_PT_PATCH) {
			pipeline->graphics.prim_vertex_count.min = pCreateInfo->pTessellationState->patchControlPoints;
			pipeline->graphics.prim_vertex_count.incr = 1;
		}
		tess = calculate_tess_state(pipeline, pCreateInfo);
	}

	pipeline->graphics.ia_multi_vgt_param = radv_compute_ia_multi_vgt_param_helpers(pipeline, &tess, prim);

	radv_compute_vertex_input_state(pipeline, pCreateInfo);

	for (uint32_t i = 0; i < MESA_SHADER_STAGES; i++)
		pipeline->user_data_0[i] = radv_pipeline_stage_to_user_data_0(pipeline, i, device->physical_device->rad_info.chip_class);

	struct radv_userdata_info *loc = radv_lookup_user_sgpr(pipeline, MESA_SHADER_VERTEX,
							     AC_UD_VS_BASE_VERTEX_START_INSTANCE);
	if (loc->sgpr_idx != -1) {
		pipeline->graphics.vtx_base_sgpr = pipeline->user_data_0[MESA_SHADER_VERTEX];
		pipeline->graphics.vtx_base_sgpr += loc->sgpr_idx * 4;
		if (radv_get_vertex_shader(pipeline)->info.info.vs.needs_draw_id)
			pipeline->graphics.vtx_emit_num = 3;
		else
			pipeline->graphics.vtx_emit_num = 2;
	}

	result = radv_pipeline_scratch_init(device, pipeline);
	radv_pipeline_generate_pm4(pipeline, pCreateInfo, extra, &blend, &tess, &gs, prim, gs_out);

	return result;
}

VkResult
radv_graphics_pipeline_create(
	VkDevice _device,
	VkPipelineCache _cache,
	const VkGraphicsPipelineCreateInfo *pCreateInfo,
	const struct radv_graphics_pipeline_create_info *extra,
	const VkAllocationCallbacks *pAllocator,
	VkPipeline *pPipeline)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_pipeline_cache, cache, _cache);
	struct radv_pipeline *pipeline;
	VkResult result;

	pipeline = vk_zalloc2(&device->alloc, pAllocator, sizeof(*pipeline), 8,
			      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (pipeline == NULL)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	result = radv_pipeline_init(pipeline, device, cache,
				    pCreateInfo, extra, pAllocator);
	if (result != VK_SUCCESS) {
		radv_pipeline_destroy(device, pipeline, pAllocator);
		return result;
	}

	*pPipeline = radv_pipeline_to_handle(pipeline);

	return VK_SUCCESS;
}

VkResult radv_CreateGraphicsPipelines(
	VkDevice                                    _device,
	VkPipelineCache                             pipelineCache,
	uint32_t                                    count,
	const VkGraphicsPipelineCreateInfo*         pCreateInfos,
	const VkAllocationCallbacks*                pAllocator,
	VkPipeline*                                 pPipelines)
{
	VkResult result = VK_SUCCESS;
	unsigned i = 0;

	for (; i < count; i++) {
		VkResult r;
		r = radv_graphics_pipeline_create(_device,
						  pipelineCache,
						  &pCreateInfos[i],
						  NULL, pAllocator, &pPipelines[i]);
		if (r != VK_SUCCESS) {
			result = r;
			pPipelines[i] = VK_NULL_HANDLE;
		}
	}

	return result;
}


static void
radv_compute_generate_pm4(struct radv_pipeline *pipeline)
{
	struct radv_shader_variant *compute_shader;
	struct radv_device *device = pipeline->device;
	unsigned compute_resource_limits;
	unsigned waves_per_threadgroup;
	uint64_t va;

	pipeline->cs.buf = malloc(20 * 4);
	pipeline->cs.max_dw = 20;

	compute_shader = pipeline->shaders[MESA_SHADER_COMPUTE];
	va = radv_buffer_get_va(compute_shader->bo) + compute_shader->bo_offset;

	radeon_set_sh_reg_seq(&pipeline->cs, R_00B830_COMPUTE_PGM_LO, 2);
	radeon_emit(&pipeline->cs, va >> 8);
	radeon_emit(&pipeline->cs, S_00B834_DATA(va >> 40));

	radeon_set_sh_reg_seq(&pipeline->cs, R_00B848_COMPUTE_PGM_RSRC1, 2);
	radeon_emit(&pipeline->cs, compute_shader->rsrc1);
	radeon_emit(&pipeline->cs, compute_shader->rsrc2);

	radeon_set_sh_reg(&pipeline->cs, R_00B860_COMPUTE_TMPRING_SIZE,
			  S_00B860_WAVES(pipeline->max_waves) |
			  S_00B860_WAVESIZE(pipeline->scratch_bytes_per_wave >> 10));

	/* Calculate best compute resource limits. */
	waves_per_threadgroup =
		DIV_ROUND_UP(compute_shader->info.cs.block_size[0] *
			     compute_shader->info.cs.block_size[1] *
			     compute_shader->info.cs.block_size[2], 64);
	compute_resource_limits =
		S_00B854_SIMD_DEST_CNTL(waves_per_threadgroup % 4 == 0);

	if (device->physical_device->rad_info.chip_class >= CIK) {
		unsigned num_cu_per_se =
			device->physical_device->rad_info.num_good_compute_units /
			device->physical_device->rad_info.max_se;

		/* Force even distribution on all SIMDs in CU if the workgroup
		 * size is 64. This has shown some good improvements if # of
		 * CUs per SE is not a multiple of 4.
		 */
		if (num_cu_per_se % 4 && waves_per_threadgroup == 1)
			compute_resource_limits |= S_00B854_FORCE_SIMD_DIST(1);
	}

	radeon_set_sh_reg(&pipeline->cs, R_00B854_COMPUTE_RESOURCE_LIMITS,
			  compute_resource_limits);

	radeon_set_sh_reg_seq(&pipeline->cs, R_00B81C_COMPUTE_NUM_THREAD_X, 3);
	radeon_emit(&pipeline->cs,
		    S_00B81C_NUM_THREAD_FULL(compute_shader->info.cs.block_size[0]));
	radeon_emit(&pipeline->cs,
		    S_00B81C_NUM_THREAD_FULL(compute_shader->info.cs.block_size[1]));
	radeon_emit(&pipeline->cs,
		    S_00B81C_NUM_THREAD_FULL(compute_shader->info.cs.block_size[2]));

	assert(pipeline->cs.cdw <= pipeline->cs.max_dw);
}

static VkResult radv_compute_pipeline_create(
	VkDevice                                    _device,
	VkPipelineCache                             _cache,
	const VkComputePipelineCreateInfo*          pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkPipeline*                                 pPipeline)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_pipeline_cache, cache, _cache);
	const VkPipelineShaderStageCreateInfo *pStages[MESA_SHADER_STAGES] = { 0, };
	struct radv_pipeline *pipeline;
	VkResult result;

	pipeline = vk_zalloc2(&device->alloc, pAllocator, sizeof(*pipeline), 8,
			      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (pipeline == NULL)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	pipeline->device = device;
	pipeline->layout = radv_pipeline_layout_from_handle(pCreateInfo->layout);
	assert(pipeline->layout);

	pStages[MESA_SHADER_COMPUTE] = &pCreateInfo->stage;
	radv_create_shaders(pipeline, device, cache, (struct radv_pipeline_key) {0}, pStages);

	pipeline->user_data_0[MESA_SHADER_COMPUTE] = radv_pipeline_stage_to_user_data_0(pipeline, MESA_SHADER_COMPUTE, device->physical_device->rad_info.chip_class);
	pipeline->need_indirect_descriptor_sets |= pipeline->shaders[MESA_SHADER_COMPUTE]->info.need_indirect_descriptor_sets;
	result = radv_pipeline_scratch_init(device, pipeline);
	if (result != VK_SUCCESS) {
		radv_pipeline_destroy(device, pipeline, pAllocator);
		return result;
	}

	radv_compute_generate_pm4(pipeline);

	*pPipeline = radv_pipeline_to_handle(pipeline);

	return VK_SUCCESS;
}

VkResult radv_CreateComputePipelines(
	VkDevice                                    _device,
	VkPipelineCache                             pipelineCache,
	uint32_t                                    count,
	const VkComputePipelineCreateInfo*          pCreateInfos,
	const VkAllocationCallbacks*                pAllocator,
	VkPipeline*                                 pPipelines)
{
	VkResult result = VK_SUCCESS;

	unsigned i = 0;
	for (; i < count; i++) {
		VkResult r;
		r = radv_compute_pipeline_create(_device, pipelineCache,
						 &pCreateInfos[i],
						 pAllocator, &pPipelines[i]);
		if (r != VK_SUCCESS) {
			result = r;
			pPipelines[i] = VK_NULL_HANDLE;
		}
	}

	return result;
}
