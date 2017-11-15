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

static void radv_dump_pipeline_stats(struct radv_device *device, struct radv_pipeline *pipeline)
{
	int i;

	for (i = 0; i < MESA_SHADER_STAGES; i++) {
		if (!pipeline->shaders[i])
			continue;

		radv_shader_dump_stats(device, pipeline->shaders[i], i, stderr);
	}
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

static unsigned si_get_cb_shader_mask(unsigned spi_shader_col_format)
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
			cb_shader_mask |= 0x9 << (i * 4);
			break;
		case V_028714_SPI_SHADER_FP16_ABGR:
		case V_028714_SPI_SHADER_UNORM16_ABGR:
		case V_028714_SPI_SHADER_SNORM16_ABGR:
		case V_028714_SPI_SHADER_UINT16_ABGR:
		case V_028714_SPI_SHADER_SINT16_ABGR:
		case V_028714_SPI_SHADER_32_ABGR:
			cb_shader_mask |= 0xf << (i * 4);
			break;
		default:
			assert(0);
		}
	}
	return cb_shader_mask;
}

static void
radv_pipeline_compute_spi_color_formats(struct radv_pipeline *pipeline,
					const VkGraphicsPipelineCreateInfo *pCreateInfo,
					uint32_t blend_enable,
					uint32_t blend_need_alpha,
					bool single_cb_enable,
					bool blend_mrt0_is_dual_src)
{
	RADV_FROM_HANDLE(radv_render_pass, pass, pCreateInfo->renderPass);
	struct radv_subpass *subpass = pass->subpasses + pCreateInfo->subpass;
	struct radv_blend_state *blend = &pipeline->graphics.blend;
	unsigned col_format = 0;

	for (unsigned i = 0; i < (single_cb_enable ? 1 : subpass->color_count); ++i) {
		unsigned cf;

		if (subpass->color_attachments[i].attachment == VK_ATTACHMENT_UNUSED) {
			cf = V_028714_SPI_SHADER_ZERO;
		} else {
			struct radv_render_pass_attachment *attachment = pass->attachments + subpass->color_attachments[i].attachment;

			cf = si_choose_spi_color_format(attachment->format,
			                                blend_enable & (1 << i),
			                                blend_need_alpha & (1 << i));
		}

		col_format |= cf << (4 * i);
	}

	blend->cb_shader_mask = si_get_cb_shader_mask(col_format);

	if (blend_mrt0_is_dual_src)
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
radv_pipeline_init_blend_state(struct radv_pipeline *pipeline,
			       const VkGraphicsPipelineCreateInfo *pCreateInfo,
			       const struct radv_graphics_pipeline_create_info *extra)
{
	const VkPipelineColorBlendStateCreateInfo *vkblend = pCreateInfo->pColorBlendState;
	const VkPipelineMultisampleStateCreateInfo *vkms = pCreateInfo->pMultisampleState;
	struct radv_blend_state *blend = &pipeline->graphics.blend;
	unsigned mode = V_028808_CB_NORMAL;
	uint32_t blend_enable = 0, blend_need_alpha = 0;
	bool blend_mrt0_is_dual_src = false;
	int i;
	bool single_cb_enable = false;

	if (!vkblend)
		return;

	if (extra && extra->custom_blend_mode) {
		single_cb_enable = true;
		mode = extra->custom_blend_mode;
	}
	blend->cb_color_control = 0;
	if (vkblend->logicOpEnable)
		blend->cb_color_control |= S_028808_ROP3(vkblend->logicOp | (vkblend->logicOp << 4));
	else
		blend->cb_color_control |= S_028808_ROP3(0xcc);

	blend->db_alpha_to_mask = S_028B70_ALPHA_TO_MASK_OFFSET0(2) |
		S_028B70_ALPHA_TO_MASK_OFFSET1(2) |
		S_028B70_ALPHA_TO_MASK_OFFSET2(2) |
		S_028B70_ALPHA_TO_MASK_OFFSET3(2);

	if (vkms && vkms->alphaToCoverageEnable) {
		blend->db_alpha_to_mask |= S_028B70_ALPHA_TO_MASK_ENABLE(1);
	}

	blend->cb_target_mask = 0;
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

		blend->sx_mrt_blend_opt[i] = S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED) | S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED);

		if (!att->colorWriteMask)
			continue;

		blend->cb_target_mask |= (unsigned)att->colorWriteMask << (4 * i);
		if (!att->blendEnable) {
			blend->cb_blend_control[i] = blend_cntl;
			continue;
		}

		if (is_dual_src(srcRGB) || is_dual_src(dstRGB) || is_dual_src(srcA) || is_dual_src(dstA))
			if (i == 0)
				blend_mrt0_is_dual_src = true;

		if (eqRGB == VK_BLEND_OP_MIN || eqRGB == VK_BLEND_OP_MAX) {
			srcRGB = VK_BLEND_FACTOR_ONE;
			dstRGB = VK_BLEND_FACTOR_ONE;
		}
		if (eqA == VK_BLEND_OP_MIN || eqA == VK_BLEND_OP_MAX) {
			srcA = VK_BLEND_FACTOR_ONE;
			dstA = VK_BLEND_FACTOR_ONE;
		}

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
		blend->sx_mrt_blend_opt[i] =
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
		blend->cb_blend_control[i] = blend_cntl;

		blend_enable |= 1 << i;

		if (srcRGB == VK_BLEND_FACTOR_SRC_ALPHA ||
		    dstRGB == VK_BLEND_FACTOR_SRC_ALPHA ||
		    srcRGB == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE ||
		    dstRGB == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE ||
		    srcRGB == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA ||
		    dstRGB == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
			blend_need_alpha |= 1 << i;
	}
	for (i = vkblend->attachmentCount; i < 8; i++) {
		blend->cb_blend_control[i] = 0;
		blend->sx_mrt_blend_opt[i] = S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED) | S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED);
	}

	/* disable RB+ for now */
	if (pipeline->device->physical_device->has_rbplus)
		blend->cb_color_control |= S_028808_DISABLE_DUAL_QUAD(1);

	if (blend->cb_target_mask)
		blend->cb_color_control |= S_028808_MODE(mode);
	else
		blend->cb_color_control |= S_028808_MODE(V_028808_CB_DISABLE);

	radv_pipeline_compute_spi_color_formats(pipeline, pCreateInfo,
						blend_enable, blend_need_alpha, single_cb_enable, blend_mrt0_is_dual_src);
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
static void
radv_pipeline_init_depth_stencil_state(struct radv_pipeline *pipeline,
				       const VkGraphicsPipelineCreateInfo *pCreateInfo,
				       const struct radv_graphics_pipeline_create_info *extra)
{
	const VkPipelineDepthStencilStateCreateInfo *vkds = pCreateInfo->pDepthStencilState;
	struct radv_depth_stencil_state *ds = &pipeline->graphics.ds;

	if (!vkds)
		return;

	RADV_FROM_HANDLE(radv_render_pass, pass, pCreateInfo->renderPass);
	struct radv_subpass *subpass = pass->subpasses + pCreateInfo->subpass;
	if (subpass->depth_stencil_attachment.attachment == VK_ATTACHMENT_UNUSED)
		return;

	struct radv_render_pass_attachment *attachment = pass->attachments + subpass->depth_stencil_attachment.attachment;
	bool has_depth_attachment = vk_format_is_depth(attachment->format);
	bool has_stencil_attachment = vk_format_is_stencil(attachment->format);

	if (has_depth_attachment) {
		ds->db_depth_control = S_028800_Z_ENABLE(vkds->depthTestEnable ? 1 : 0) |
		                       S_028800_Z_WRITE_ENABLE(vkds->depthWriteEnable ? 1 : 0) |
		                       S_028800_ZFUNC(vkds->depthCompareOp) |
		                       S_028800_DEPTH_BOUNDS_ENABLE(vkds->depthBoundsTestEnable ? 1 : 0);
	}

	if (has_stencil_attachment && vkds->stencilTestEnable) {
		ds->db_depth_control |= S_028800_STENCIL_ENABLE(1) | S_028800_BACKFACE_ENABLE(1);
		ds->db_depth_control |= S_028800_STENCILFUNC(vkds->front.compareOp);
		ds->db_stencil_control |= S_02842C_STENCILFAIL(si_translate_stencil_op(vkds->front.failOp));
		ds->db_stencil_control |= S_02842C_STENCILZPASS(si_translate_stencil_op(vkds->front.passOp));
		ds->db_stencil_control |= S_02842C_STENCILZFAIL(si_translate_stencil_op(vkds->front.depthFailOp));

		ds->db_depth_control |= S_028800_STENCILFUNC_BF(vkds->back.compareOp);
		ds->db_stencil_control |= S_02842C_STENCILFAIL_BF(si_translate_stencil_op(vkds->back.failOp));
		ds->db_stencil_control |= S_02842C_STENCILZPASS_BF(si_translate_stencil_op(vkds->back.passOp));
		ds->db_stencil_control |= S_02842C_STENCILZFAIL_BF(si_translate_stencil_op(vkds->back.depthFailOp));
	}

	if (extra) {

		ds->db_render_control |= S_028000_DEPTH_CLEAR_ENABLE(extra->db_depth_clear);
		ds->db_render_control |= S_028000_STENCIL_CLEAR_ENABLE(extra->db_stencil_clear);

		ds->db_render_control |= S_028000_RESUMMARIZE_ENABLE(extra->db_resummarize);
		ds->db_render_control |= S_028000_DEPTH_COMPRESS_DISABLE(extra->db_flush_depth_inplace);
		ds->db_render_control |= S_028000_STENCIL_COMPRESS_DISABLE(extra->db_flush_stencil_inplace);
		ds->db_render_override2 |= S_028010_DISABLE_ZMASK_EXPCLEAR_OPTIMIZATION(extra->db_depth_disable_expclear);
		ds->db_render_override2 |= S_028010_DISABLE_SMEM_EXPCLEAR_OPTIMIZATION(extra->db_stencil_disable_expclear);
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
static void
radv_pipeline_init_raster_state(struct radv_pipeline *pipeline,
				const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	const VkPipelineRasterizationStateCreateInfo *vkraster = pCreateInfo->pRasterizationState;
	struct radv_raster_state *raster = &pipeline->graphics.raster;

	raster->spi_interp_control =
		S_0286D4_FLAT_SHADE_ENA(1) |
		S_0286D4_PNT_SPRITE_ENA(1) |
		S_0286D4_PNT_SPRITE_OVRD_X(V_0286D4_SPI_PNT_SPRITE_SEL_S) |
		S_0286D4_PNT_SPRITE_OVRD_Y(V_0286D4_SPI_PNT_SPRITE_SEL_T) |
		S_0286D4_PNT_SPRITE_OVRD_Z(V_0286D4_SPI_PNT_SPRITE_SEL_0) |
		S_0286D4_PNT_SPRITE_OVRD_W(V_0286D4_SPI_PNT_SPRITE_SEL_1) |
		S_0286D4_PNT_SPRITE_TOP_1(0); // vulkan is top to bottom - 1.0 at bottom


	raster->pa_cl_clip_cntl = S_028810_PS_UCP_MODE(3) |
		S_028810_DX_CLIP_SPACE_DEF(1) | // vulkan uses DX conventions.
		S_028810_ZCLIP_NEAR_DISABLE(vkraster->depthClampEnable ? 1 : 0) |
		S_028810_ZCLIP_FAR_DISABLE(vkraster->depthClampEnable ? 1 : 0) |
		S_028810_DX_RASTERIZATION_KILL(vkraster->rasterizerDiscardEnable ? 1 : 0) |
		S_028810_DX_LINEAR_ATTR_CLIP_ENA(1);

	raster->pa_su_vtx_cntl =
		S_028BE4_PIX_CENTER(1) | // TODO verify
		S_028BE4_ROUND_MODE(V_028BE4_X_ROUND_TO_EVEN) |
		S_028BE4_QUANT_MODE(V_028BE4_X_16_8_FIXED_POINT_1_256TH);

	raster->pa_su_sc_mode_cntl =
		S_028814_FACE(vkraster->frontFace) |
		S_028814_CULL_FRONT(!!(vkraster->cullMode & VK_CULL_MODE_FRONT_BIT)) |
		S_028814_CULL_BACK(!!(vkraster->cullMode & VK_CULL_MODE_BACK_BIT)) |
		S_028814_POLY_MODE(vkraster->polygonMode != VK_POLYGON_MODE_FILL) |
		S_028814_POLYMODE_FRONT_PTYPE(si_translate_fill(vkraster->polygonMode)) |
		S_028814_POLYMODE_BACK_PTYPE(si_translate_fill(vkraster->polygonMode)) |
		S_028814_POLY_OFFSET_FRONT_ENABLE(vkraster->depthBiasEnable ? 1 : 0) |
		S_028814_POLY_OFFSET_BACK_ENABLE(vkraster->depthBiasEnable ? 1 : 0) |
		S_028814_POLY_OFFSET_PARA_ENABLE(vkraster->depthBiasEnable ? 1 : 0);

}

static void
radv_pipeline_init_multisample_state(struct radv_pipeline *pipeline,
				     const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	const VkPipelineMultisampleStateCreateInfo *vkms = pCreateInfo->pMultisampleState;
	struct radv_multisample_state *ms = &pipeline->graphics.ms;
	unsigned num_tile_pipes = pipeline->device->physical_device->rad_info.num_tile_pipes;
	int ps_iter_samples = 1;
	uint32_t mask = 0xffff;

	if (vkms)
		ms->num_samples = vkms->rasterizationSamples;
	else
		ms->num_samples = 1;

	if (vkms && vkms->sampleShadingEnable) {
		ps_iter_samples = ceil(vkms->minSampleShading * ms->num_samples);
	} else if (pipeline->shaders[MESA_SHADER_FRAGMENT]->info.info.ps.force_persample) {
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
	ms->pa_sc_mode_cntl_0 = S_028A48_ALTERNATE_RBS_PER_TILE(pipeline->device->physical_device->rad_info.chip_class >= GFX9);

	if (ms->num_samples > 1) {
		unsigned log_samples = util_logbase2(ms->num_samples);
		unsigned log_ps_iter_samples = util_logbase2(util_next_power_of_two(ps_iter_samples));
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
	}

	const struct VkPipelineRasterizationStateRasterizationOrderAMD *raster_order =
		vk_find_struct_const(pCreateInfo->pRasterizationState->pNext, PIPELINE_RASTERIZATION_STATE_RASTERIZATION_ORDER_AMD);
	if (raster_order && raster_order->rasterizationOrder == VK_RASTERIZATION_ORDER_RELAXED_AMD) {
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

static void
radv_pipeline_init_dynamic_state(struct radv_pipeline *pipeline,
				 const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	uint32_t states = RADV_CMD_DIRTY_DYNAMIC_ALL;
	RADV_FROM_HANDLE(radv_render_pass, pass, pCreateInfo->renderPass);
	struct radv_subpass *subpass = &pass->subpasses[pCreateInfo->subpass];

	pipeline->dynamic_state = default_dynamic_state;

	if (pCreateInfo->pDynamicState) {
		/* Remove all of the states that are marked as dynamic */
		uint32_t count = pCreateInfo->pDynamicState->dynamicStateCount;
		for (uint32_t s = 0; s < count; s++)
			states &= ~(1 << pCreateInfo->pDynamicState->pDynamicStates[s]);
	}

	struct radv_dynamic_state *dynamic = &pipeline->dynamic_state;

	/* Section 9.2 of the Vulkan 1.0.15 spec says:
	 *
	 *    pViewportState is [...] NULL if the pipeline
	 *    has rasterization disabled.
	 */
	if (!pCreateInfo->pRasterizationState->rasterizerDiscardEnable) {
		assert(pCreateInfo->pViewportState);

		dynamic->viewport.count = pCreateInfo->pViewportState->viewportCount;
		if (states & (1 << VK_DYNAMIC_STATE_VIEWPORT)) {
			typed_memcpy(dynamic->viewport.viewports,
				     pCreateInfo->pViewportState->pViewports,
				     pCreateInfo->pViewportState->viewportCount);
		}

		dynamic->scissor.count = pCreateInfo->pViewportState->scissorCount;
		if (states & (1 << VK_DYNAMIC_STATE_SCISSOR)) {
			typed_memcpy(dynamic->scissor.scissors,
				     pCreateInfo->pViewportState->pScissors,
				     pCreateInfo->pViewportState->scissorCount);
		}
	}

	if (states & (1 << VK_DYNAMIC_STATE_LINE_WIDTH)) {
		assert(pCreateInfo->pRasterizationState);
		dynamic->line_width = pCreateInfo->pRasterizationState->lineWidth;
	}

	if (states & (1 << VK_DYNAMIC_STATE_DEPTH_BIAS)) {
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

	if (uses_color_att && states & (1 << VK_DYNAMIC_STATE_BLEND_CONSTANTS)) {
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
	if (!pCreateInfo->pRasterizationState->rasterizerDiscardEnable &&
	    subpass->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED) {
		assert(pCreateInfo->pDepthStencilState);

		if (states & (1 << VK_DYNAMIC_STATE_DEPTH_BOUNDS)) {
			dynamic->depth_bounds.min =
				pCreateInfo->pDepthStencilState->minDepthBounds;
			dynamic->depth_bounds.max =
				pCreateInfo->pDepthStencilState->maxDepthBounds;
		}

		if (states & (1 << VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK)) {
			dynamic->stencil_compare_mask.front =
				pCreateInfo->pDepthStencilState->front.compareMask;
			dynamic->stencil_compare_mask.back =
				pCreateInfo->pDepthStencilState->back.compareMask;
		}

		if (states & (1 << VK_DYNAMIC_STATE_STENCIL_WRITE_MASK)) {
			dynamic->stencil_write_mask.front =
				pCreateInfo->pDepthStencilState->front.writeMask;
			dynamic->stencil_write_mask.back =
				pCreateInfo->pDepthStencilState->back.writeMask;
		}

		if (states & (1 << VK_DYNAMIC_STATE_STENCIL_REFERENCE)) {
			dynamic->stencil_reference.front =
				pCreateInfo->pDepthStencilState->front.reference;
			dynamic->stencil_reference.back =
				pCreateInfo->pDepthStencilState->back.reference;
		}
	}

	pipeline->dynamic_state.mask = states;
}

static void calculate_gfx9_gs_info(const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                   struct radv_pipeline *pipeline)
{
	struct ac_shader_variant_info *gs_info = &pipeline->shaders[MESA_SHADER_GEOMETRY]->info;
	struct ac_es_output_info *es_info = radv_pipeline_has_tess(pipeline) ?
		&gs_info->tes.es_info : &gs_info->vs.es_info;
	unsigned gs_num_invocations = MAX2(gs_info->gs.invocations, 1);
	bool uses_adjacency;
	switch(pCreateInfo->pInputAssemblyState->topology) {
	case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
	case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
		uses_adjacency = false;
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
	pipeline->graphics.gs.lds_size = align(esgs_lds_size, 128) / 128;
	pipeline->graphics.gs.vgt_gs_onchip_cntl =
	                       S_028A44_ES_VERTS_PER_SUBGRP(es_verts_per_subgroup) |
	                       S_028A44_GS_PRIMS_PER_SUBGRP(gs_prims_per_subgroup) |
	                       S_028A44_GS_INST_PRIMS_IN_SUBGRP(gs_inst_prims_in_subgroup);
	pipeline->graphics.gs.vgt_gs_max_prims_per_subgroup =
	                       S_028A94_MAX_PRIMS_PER_SUBGROUP(max_prims_per_subgroup);
	pipeline->graphics.gs.vgt_esgs_ring_itemsize  = esgs_itemsize;
	assert(max_prims_per_subgroup <= max_out_prims);
}

static void
calculate_gs_ring_sizes(struct radv_pipeline *pipeline)
{
	struct radv_device *device = pipeline->device;
	unsigned num_se = device->physical_device->rad_info.max_se;
	unsigned wave_size = 64;
	unsigned max_gs_waves = 32 * num_se; /* max 32 per SE on GCN */
	unsigned gs_vertex_reuse = 16 * num_se; /* GS_VERTEX_REUSE register (per SE) */
	unsigned alignment = 256 * num_se;
	/* The maximum size is 63.999 MB per SE. */
	unsigned max_size = ((unsigned)(63.999 * 1024 * 1024) & ~255) * num_se;
	struct ac_shader_variant_info *gs_info = &pipeline->shaders[MESA_SHADER_GEOMETRY]->info;
	struct ac_es_output_info *es_info;
	if (pipeline->device->physical_device->rad_info.chip_class >= GFX9) 
		es_info = radv_pipeline_has_tess(pipeline) ? &gs_info->tes.es_info : &gs_info->vs.es_info;
	else
		es_info = radv_pipeline_has_tess(pipeline) ?
			&pipeline->shaders[MESA_SHADER_TESS_EVAL]->info.tes.es_info :
			&pipeline->shaders[MESA_SHADER_VERTEX]->info.vs.es_info;

	/* Calculate the minimum size. */
	unsigned min_esgs_ring_size = align(es_info->esgs_itemsize * gs_vertex_reuse *
					    wave_size, alignment);
	/* These are recommended sizes, not minimum sizes. */
	unsigned esgs_ring_size = max_gs_waves * 2 * wave_size *
		es_info->esgs_itemsize * gs_info->gs.vertices_in;
	unsigned gsvs_ring_size = max_gs_waves * 2 * wave_size *
		gs_info->gs.max_gsvs_emit_size * 1; // no streams in VK (gs->max_gs_stream + 1);

	min_esgs_ring_size = align(min_esgs_ring_size, alignment);
	esgs_ring_size = align(esgs_ring_size, alignment);
	gsvs_ring_size = align(gsvs_ring_size, alignment);

	if (pipeline->device->physical_device->rad_info.chip_class <= VI)
		pipeline->graphics.esgs_ring_size = CLAMP(esgs_ring_size, min_esgs_ring_size, max_size);

	pipeline->graphics.gs.vgt_esgs_ring_itemsize = es_info->esgs_itemsize / 4;
	pipeline->graphics.gsvs_ring_size = MIN2(gsvs_ring_size, max_size);
}

static void si_multiwave_lds_size_workaround(struct radv_device *device,
					     unsigned *lds_size)
{
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

static void
calculate_tess_state(struct radv_pipeline *pipeline,
		     const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	unsigned num_tcs_input_cp = pCreateInfo->pTessellationState->patchControlPoints;
	unsigned num_tcs_output_cp, num_tcs_inputs, num_tcs_outputs;
	unsigned num_tcs_patch_outputs;
	unsigned input_vertex_size, output_vertex_size, pervertex_output_patch_size;
	unsigned input_patch_size, output_patch_size, output_patch0_offset;
	unsigned lds_size, hardware_lds_size;
	unsigned perpatch_output_offset;
	unsigned num_patches;
	struct radv_tessellation_state *tess = &pipeline->graphics.tess;

	/* This calculates how shader inputs and outputs among VS, TCS, and TES
	 * are laid out in LDS. */
	num_tcs_inputs = util_last_bit64(radv_get_vertex_shader(pipeline)->info.vs.outputs_written);

	num_tcs_outputs = util_last_bit64(pipeline->shaders[MESA_SHADER_TESS_CTRL]->info.tcs.outputs_written); //tcs->outputs_written
	num_tcs_output_cp = pipeline->shaders[MESA_SHADER_TESS_CTRL]->info.tcs.tcs_vertices_out; //TCS VERTICES OUT
	num_tcs_patch_outputs = util_last_bit64(pipeline->shaders[MESA_SHADER_TESS_CTRL]->info.tcs.patch_outputs_written);

	/* Ensure that we only need one wave per SIMD so we don't need to check
	 * resource usage. Also ensures that the number of tcs in and out
	 * vertices per threadgroup are at most 256.
	 */
	input_vertex_size = num_tcs_inputs * 16;
	output_vertex_size = num_tcs_outputs * 16;

	input_patch_size = num_tcs_input_cp * input_vertex_size;

	pervertex_output_patch_size = num_tcs_output_cp * output_vertex_size;
	output_patch_size = pervertex_output_patch_size + num_tcs_patch_outputs * 16;
	/* Ensure that we only need one wave per SIMD so we don't need to check
	 * resource usage. Also ensures that the number of tcs in and out
	 * vertices per threadgroup are at most 256.
	 */
	num_patches = 64 / MAX2(num_tcs_input_cp, num_tcs_output_cp) * 4;

	/* Make sure that the data fits in LDS. This assumes the shaders only
	 * use LDS for the inputs and outputs.
	 */
	hardware_lds_size = pipeline->device->physical_device->rad_info.chip_class >= CIK ? 65536 : 32768;
	num_patches = MIN2(num_patches, hardware_lds_size / (input_patch_size + output_patch_size));

	/* Make sure the output data fits in the offchip buffer */
	num_patches = MIN2(num_patches,
			    (pipeline->device->tess_offchip_block_dw_size * 4) /
			    output_patch_size);

	/* Not necessary for correctness, but improves performance. The
	 * specific value is taken from the proprietary driver.
	 */
	num_patches = MIN2(num_patches, 40);

	/* SI bug workaround - limit LS-HS threadgroups to only one wave. */
	if (pipeline->device->physical_device->rad_info.chip_class == SI) {
		unsigned one_wave = 64 / MAX2(num_tcs_input_cp, num_tcs_output_cp);
		num_patches = MIN2(num_patches, one_wave);
	}

	output_patch0_offset = input_patch_size * num_patches;
	perpatch_output_offset = output_patch0_offset + pervertex_output_patch_size;

	lds_size = output_patch0_offset + output_patch_size * num_patches;

	if (pipeline->device->physical_device->rad_info.chip_class >= CIK) {
		assert(lds_size <= 65536);
		lds_size = align(lds_size, 512) / 512;
	} else {
		assert(lds_size <= 32768);
		lds_size = align(lds_size, 256) / 256;
	}
	si_multiwave_lds_size_workaround(pipeline->device, &lds_size);

	tess->lds_size = lds_size;

	tess->tcs_in_layout = (input_patch_size / 4) |
		((input_vertex_size / 4) << 13);
	tess->tcs_out_layout = (output_patch_size / 4) |
		((output_vertex_size / 4) << 13);
	tess->tcs_out_offsets = (output_patch0_offset / 16) |
		((perpatch_output_offset / 16) << 16);
	tess->offchip_layout = (pervertex_output_patch_size * num_patches << 16) |
		(num_tcs_output_cp << 9) | num_patches;

	tess->ls_hs_config = S_028B58_NUM_PATCHES(num_patches) |
		S_028B58_HS_NUM_INPUT_CP(num_tcs_input_cp) |
		S_028B58_HS_NUM_OUTPUT_CP(num_tcs_output_cp);
	tess->num_patches = num_patches;
	tess->num_tcs_input_cp = num_tcs_input_cp;

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

	tess->tf_param = S_028B6C_TYPE(type) |
		S_028B6C_PARTITIONING(partitioning) |
		S_028B6C_TOPOLOGY(topology) |
		S_028B6C_DISTRIBUTION_MODE(distribution_mode);
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

static uint32_t si_vgt_gs_mode(struct radv_shader_variant *gs,
                               enum chip_class chip_class)
{
	unsigned gs_max_vert_out = gs->info.gs.vertices_out;
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

	return S_028A40_MODE(V_028A40_GS_SCENARIO_G) |
	       S_028A40_CUT_MODE(cut_mode)|
	       S_028A40_ES_WRITE_OPTIMIZE(chip_class <= VI) |
	       S_028A40_GS_WRITE_OPTIMIZE(1) |
	       S_028A40_ONCHIP(chip_class >= GFX9 ? 1 : 0);
}

static struct ac_vs_output_info *get_vs_output_info(struct radv_pipeline *pipeline)
{
	if (radv_pipeline_has_gs(pipeline))
		return &pipeline->gs_copy_shader->info.vs.outinfo;
	else if (radv_pipeline_has_tess(pipeline))
		return &pipeline->shaders[MESA_SHADER_TESS_EVAL]->info.tes.outinfo;
	else
		return &pipeline->shaders[MESA_SHADER_VERTEX]->info.vs.outinfo;
}

static void calculate_vgt_gs_mode(struct radv_pipeline *pipeline)
{
	struct ac_vs_output_info *outinfo = get_vs_output_info(pipeline);

	pipeline->graphics.vgt_primitiveid_en = false;
	pipeline->graphics.vgt_gs_mode = 0;

	if (radv_pipeline_has_gs(pipeline)) {
		pipeline->graphics.vgt_gs_mode = si_vgt_gs_mode(pipeline->shaders[MESA_SHADER_GEOMETRY],
		                                                pipeline->device->physical_device->rad_info.chip_class);
	} else if (outinfo->export_prim_id) {
		pipeline->graphics.vgt_gs_mode = S_028A40_MODE(V_028A40_GS_SCENARIO_A);
		pipeline->graphics.vgt_primitiveid_en = true;
	}
}

static void calculate_vs_outinfo(struct radv_pipeline *pipeline)
{
	struct ac_vs_output_info *outinfo = get_vs_output_info(pipeline);

	unsigned clip_dist_mask, cull_dist_mask, total_mask;
	clip_dist_mask = outinfo->clip_dist_mask;
	cull_dist_mask = outinfo->cull_dist_mask;
	total_mask = clip_dist_mask | cull_dist_mask;

	bool misc_vec_ena = outinfo->writes_pointsize ||
		outinfo->writes_layer ||
		outinfo->writes_viewport_index;
	pipeline->graphics.vs.pa_cl_vs_out_cntl =
		S_02881C_USE_VTX_POINT_SIZE(outinfo->writes_pointsize) |
		S_02881C_USE_VTX_RENDER_TARGET_INDX(outinfo->writes_layer) |
		S_02881C_USE_VTX_VIEWPORT_INDX(outinfo->writes_viewport_index) |
		S_02881C_VS_OUT_MISC_VEC_ENA(misc_vec_ena) |
		S_02881C_VS_OUT_MISC_SIDE_BUS_ENA(misc_vec_ena) |
		S_02881C_VS_OUT_CCDIST0_VEC_ENA((total_mask & 0x0f) != 0) |
		S_02881C_VS_OUT_CCDIST1_VEC_ENA((total_mask & 0xf0) != 0) |
		cull_dist_mask << 8 |
		clip_dist_mask;

	pipeline->graphics.vs.spi_shader_pos_format =
		S_02870C_POS0_EXPORT_FORMAT(V_02870C_SPI_SHADER_4COMP) |
		S_02870C_POS1_EXPORT_FORMAT(outinfo->pos_exports > 1 ?
					    V_02870C_SPI_SHADER_4COMP :
					    V_02870C_SPI_SHADER_NONE) |
		S_02870C_POS2_EXPORT_FORMAT(outinfo->pos_exports > 2 ?
					    V_02870C_SPI_SHADER_4COMP :
					    V_02870C_SPI_SHADER_NONE) |
		S_02870C_POS3_EXPORT_FORMAT(outinfo->pos_exports > 3 ?
					    V_02870C_SPI_SHADER_4COMP :
					    V_02870C_SPI_SHADER_NONE);

	pipeline->graphics.vs.spi_vs_out_config = S_0286C4_VS_EXPORT_COUNT(MAX2(1, outinfo->param_exports) - 1);
	/* only emitted on pre-VI */
	pipeline->graphics.vs.vgt_reuse_off = S_028AB4_REUSE_OFF(outinfo->writes_viewport_index);
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

static void calculate_ps_inputs(struct radv_pipeline *pipeline)
{
	struct radv_shader_variant *ps;
	struct ac_vs_output_info *outinfo = get_vs_output_info(pipeline);

	ps = pipeline->shaders[MESA_SHADER_FRAGMENT];

	unsigned ps_offset = 0;

	if (ps->info.fs.prim_id_input) {
		unsigned vs_offset = outinfo->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID];
		if (vs_offset != AC_EXP_PARAM_UNDEFINED) {
			pipeline->graphics.ps_input_cntl[ps_offset] = offset_to_ps_input(vs_offset, true);
			++ps_offset;
		}
	}

	if (ps->info.fs.layer_input) {
		unsigned vs_offset = outinfo->vs_output_param_offset[VARYING_SLOT_LAYER];
		if (vs_offset != AC_EXP_PARAM_UNDEFINED)
			pipeline->graphics.ps_input_cntl[ps_offset] = offset_to_ps_input(vs_offset, true);
		else
			pipeline->graphics.ps_input_cntl[ps_offset] = offset_to_ps_input(AC_EXP_PARAM_DEFAULT_VAL_0000, true);
		++ps_offset;
	}

	if (ps->info.fs.has_pcoord) {
		unsigned val;
		val = S_028644_PT_SPRITE_TEX(1) | S_028644_OFFSET(0x20);
		pipeline->graphics.ps_input_cntl[ps_offset] = val;
		ps_offset++;
	}

	for (unsigned i = 0; i < 32 && (1u << i) <= ps->info.fs.input_mask; ++i) {
		unsigned vs_offset;
		bool flat_shade;
		if (!(ps->info.fs.input_mask & (1u << i)))
			continue;

		vs_offset = outinfo->vs_output_param_offset[VARYING_SLOT_VAR0 + i];
		if (vs_offset == AC_EXP_PARAM_UNDEFINED) {
			pipeline->graphics.ps_input_cntl[ps_offset] = S_028644_OFFSET(0x20);
			++ps_offset;
			continue;
		}

		flat_shade = !!(ps->info.fs.flat_shaded_mask & (1u << ps_offset));

		pipeline->graphics.ps_input_cntl[ps_offset] = offset_to_ps_input(vs_offset, flat_shade);
		++ps_offset;
	}

	pipeline->graphics.ps_input_cntl_num = ps_offset;
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
		nir_remove_dead_variables(ordered_shaders[i],
					  nir_var_shader_out);
		nir_remove_dead_variables(ordered_shaders[i - 1],
					  nir_var_shader_in);

		bool progress = nir_remove_unused_varyings(ordered_shaders[i],
							   ordered_shaders[i - 1]);

		if (progress) {
			nir_lower_global_vars_to_local(ordered_shaders[i]);
			radv_optimize_nir(ordered_shaders[i]);
			nir_lower_global_vars_to_local(ordered_shaders[i - 1]);
			radv_optimize_nir(ordered_shaders[i - 1]);
		}
	}
}


static struct radv_pipeline_key
radv_generate_graphics_pipeline_key(struct radv_pipeline *pipeline,
                                    const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                    bool has_view_index)
{
	const VkPipelineVertexInputStateCreateInfo *input_state =
	                                         pCreateInfo->pVertexInputState;
	struct radv_pipeline_key key;
	memset(&key, 0, sizeof(key));

	key.has_multiview_view_index = has_view_index;

	for (unsigned i = 0; i < input_state->vertexAttributeDescriptionCount; ++i) {
		unsigned binding;
		binding = input_state->pVertexAttributeDescriptions[i].binding;
		if (input_state->pVertexBindingDescriptions[binding].inputRate)
			key.instance_rate_inputs |= 1u << input_state->pVertexAttributeDescriptions[i].location;
	}

	if (pCreateInfo->pTessellationState)
		key.tess_input_vertices = pCreateInfo->pTessellationState->patchControlPoints;


	if (pCreateInfo->pMultisampleState &&
	    pCreateInfo->pMultisampleState->rasterizationSamples > 1)
		key.multisample = true;

	key.col_format = pipeline->graphics.blend.spi_shader_col_format;
	if (pipeline->device->physical_device->rad_info.chip_class < VI)
		radv_pipeline_compute_get_int_clamp(pCreateInfo, &key.is_int8, &key.is_int10);

	return key;
}

static void
radv_fill_shader_keys(struct ac_shader_variant_key *keys,
                      const struct radv_pipeline_key *key,
                      nir_shader **nir)
{
	keys[MESA_SHADER_VERTEX].vs.instance_rate_inputs = key->instance_rate_inputs;

	if (nir[MESA_SHADER_TESS_CTRL]) {
		keys[MESA_SHADER_VERTEX].vs.as_ls = true;
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
	struct ac_shader_variant_key keys[MESA_SHADER_STAGES] = {{{{0}}}};
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
	}

	radv_link_shaders(pipeline, nir);

	for (int i = 0; i < MESA_SHADER_STAGES; ++i) {
		if (!(device->instance->debug_flags & RADV_DEBUG_DUMP_SHADERS))
			continue;

		if (modules[i])
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
		        pipeline->shaders[MESA_SHADER_FRAGMENT]->info.fs.prim_id_input;
		keys[MESA_SHADER_TESS_EVAL].tes.export_prim_id =
		        pipeline->shaders[MESA_SHADER_FRAGMENT]->info.fs.prim_id_input;
	}

	if (device->physical_device->rad_info.chip_class >= GFX9 && modules[MESA_SHADER_TESS_CTRL]) {
		if (!pipeline->shaders[MESA_SHADER_TESS_CTRL]) {
			struct nir_shader *combined_nir[] = {nir[MESA_SHADER_VERTEX], nir[MESA_SHADER_TESS_CTRL]};
			struct ac_shader_variant_key key = keys[MESA_SHADER_TESS_CTRL];
			key.tcs.vs_key = keys[MESA_SHADER_VERTEX].vs;
			pipeline->shaders[MESA_SHADER_TESS_CTRL] = radv_shader_variant_create(device, modules[MESA_SHADER_TESS_CTRL], combined_nir, 2,
			                                                                      pipeline->layout,
			                                                                      &key, &codes[MESA_SHADER_TESS_CTRL],
			                                                                      &code_sizes[MESA_SHADER_TESS_CTRL]);
		}
		modules[MESA_SHADER_VERTEX] = NULL;
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
		if (modules[i] && !pipeline->device->keep_shader_info)
			ralloc_free(nir[i]);
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

	radv_pipeline_init_dynamic_state(pipeline, pCreateInfo);
	radv_pipeline_init_blend_state(pipeline, pCreateInfo, extra);

	const VkPipelineShaderStageCreateInfo *pStages[MESA_SHADER_STAGES] = { 0, };
	for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
		gl_shader_stage stage = ffs(pCreateInfo->pStages[i].stage) - 1;
		pStages[stage] = &pCreateInfo->pStages[i];
	}

	radv_create_shaders(pipeline, device, cache, 
	                    radv_generate_graphics_pipeline_key(pipeline, pCreateInfo, has_view_index),
	                    pStages);

	radv_pipeline_init_depth_stencil_state(pipeline, pCreateInfo, extra);
	radv_pipeline_init_raster_state(pipeline, pCreateInfo);
	radv_pipeline_init_multisample_state(pipeline, pCreateInfo);
	pipeline->graphics.prim = si_translate_prim(pCreateInfo->pInputAssemblyState->topology);
	pipeline->graphics.can_use_guardband = radv_prim_can_use_guardband(pCreateInfo->pInputAssemblyState->topology);

	if (radv_pipeline_has_gs(pipeline)) {
		pipeline->graphics.gs_out = si_conv_gl_prim_to_gs_out(pipeline->shaders[MESA_SHADER_GEOMETRY]->info.gs.output_prim);
		pipeline->graphics.can_use_guardband = pipeline->graphics.gs_out == V_028A6C_OUTPRIM_TYPE_TRISTRIP;
	} else {
		pipeline->graphics.gs_out = si_conv_prim_to_gs_out(pCreateInfo->pInputAssemblyState->topology);
	}
	if (extra && extra->use_rectlist) {
		pipeline->graphics.prim = V_008958_DI_PT_RECTLIST;
		pipeline->graphics.gs_out = V_028A6C_OUTPRIM_TYPE_TRISTRIP;
		pipeline->graphics.can_use_guardband = true;
	}
	pipeline->graphics.prim_restart_enable = !!pCreateInfo->pInputAssemblyState->primitiveRestartEnable;
	/* prim vertex count will need TESS changes */
	pipeline->graphics.prim_vertex_count = prim_size_table[pipeline->graphics.prim];

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
	if (!pipeline->graphics.blend.spi_shader_col_format) {
		if (!ps->info.fs.writes_z &&
		    !ps->info.fs.writes_stencil &&
		    !ps->info.fs.writes_sample_mask)
			pipeline->graphics.blend.spi_shader_col_format = V_028714_SPI_SHADER_32_R;
	}

	unsigned z_order;
	pipeline->graphics.db_shader_control = 0;
	if (ps->info.fs.early_fragment_test || !ps->info.fs.writes_memory)
		z_order = V_02880C_EARLY_Z_THEN_LATE_Z;
	else
		z_order = V_02880C_LATE_Z;

	pipeline->graphics.db_shader_control =
		S_02880C_Z_EXPORT_ENABLE(ps->info.fs.writes_z) |
		S_02880C_STENCIL_TEST_VAL_EXPORT_ENABLE(ps->info.fs.writes_stencil) |
		S_02880C_KILL_ENABLE(!!ps->info.fs.can_discard) |
		S_02880C_MASK_EXPORT_ENABLE(ps->info.fs.writes_sample_mask) |
		S_02880C_Z_ORDER(z_order) |
		S_02880C_DEPTH_BEFORE_SHADER(ps->info.fs.early_fragment_test) |
		S_02880C_EXEC_ON_HIER_FAIL(ps->info.fs.writes_memory) |
		S_02880C_EXEC_ON_NOOP(ps->info.fs.writes_memory);

	if (pipeline->device->physical_device->has_rbplus)
		pipeline->graphics.db_shader_control |= S_02880C_DUAL_QUAD_DISABLE(1);

	pipeline->graphics.shader_z_format =
		ps->info.fs.writes_sample_mask ? V_028710_SPI_SHADER_32_ABGR :
		ps->info.fs.writes_stencil ? V_028710_SPI_SHADER_32_GR :
		ps->info.fs.writes_z ? V_028710_SPI_SHADER_32_R :
		V_028710_SPI_SHADER_ZERO;

	calculate_vgt_gs_mode(pipeline);
	calculate_vs_outinfo(pipeline);
	calculate_ps_inputs(pipeline);

	for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
		if (pipeline->shaders[i]) {
			pipeline->need_indirect_descriptor_sets |= pipeline->shaders[i]->info.need_indirect_descriptor_sets;
		}
	}

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

	if (device->physical_device->rad_info.chip_class >= GFX9)
		stages |= S_028B54_MAX_PRIMGRP_IN_WAVE(2);

	pipeline->graphics.vgt_shader_stages_en = stages;

	if (radv_pipeline_has_gs(pipeline)) {
		calculate_gs_ring_sizes(pipeline);
		if (device->physical_device->rad_info.chip_class >= GFX9)
			calculate_gfx9_gs_info(pCreateInfo, pipeline);
	}

	if (radv_pipeline_has_tess(pipeline)) {
		if (pipeline->graphics.prim == V_008958_DI_PT_PATCH) {
			pipeline->graphics.prim_vertex_count.min = pCreateInfo->pTessellationState->patchControlPoints;
			pipeline->graphics.prim_vertex_count.incr = 1;
		}
		calculate_tess_state(pipeline, pCreateInfo);
	}

	if (radv_pipeline_has_tess(pipeline))
		pipeline->graphics.primgroup_size = pipeline->graphics.tess.num_patches;
	else if (radv_pipeline_has_gs(pipeline))
		pipeline->graphics.primgroup_size = 64;
	else
		pipeline->graphics.primgroup_size = 128; /* recommended without a GS */

	pipeline->graphics.partial_es_wave = false;
	if (pipeline->device->has_distributed_tess) {
		if (radv_pipeline_has_gs(pipeline)) {
			if (device->physical_device->rad_info.chip_class <= VI)
				pipeline->graphics.partial_es_wave = true;
		}
	}
	/* GS requirement. */
	if (SI_GS_PER_ES / pipeline->graphics.primgroup_size >= pipeline->device->gs_table_depth - 3)
		pipeline->graphics.partial_es_wave = true;

	pipeline->graphics.wd_switch_on_eop = false;
	if (device->physical_device->rad_info.chip_class >= CIK) {
		unsigned prim = pipeline->graphics.prim;
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
			pipeline->graphics.wd_switch_on_eop = true;
	}

	pipeline->graphics.ia_switch_on_eoi = false;
	if (pipeline->shaders[MESA_SHADER_FRAGMENT]->info.fs.prim_id_input)
		pipeline->graphics.ia_switch_on_eoi = true;
	if (radv_pipeline_has_gs(pipeline) &&
	    pipeline->shaders[MESA_SHADER_GEOMETRY]->info.gs.uses_prim_id)
		pipeline->graphics.ia_switch_on_eoi = true;
	if (radv_pipeline_has_tess(pipeline)) {
		/* SWITCH_ON_EOI must be set if PrimID is used. */
		if (pipeline->shaders[MESA_SHADER_TESS_CTRL]->info.tcs.uses_prim_id ||
		    radv_get_tess_eval_shader(pipeline)->info.tes.uses_prim_id)
			pipeline->graphics.ia_switch_on_eoi = true;
	}

	pipeline->graphics.partial_vs_wave = false;
	if (radv_pipeline_has_tess(pipeline)) {
		/* Bug with tessellation and GS on Bonaire and older 2 SE chips. */
		if ((device->physical_device->rad_info.family == CHIP_TAHITI ||
		     device->physical_device->rad_info.family == CHIP_PITCAIRN ||
		     device->physical_device->rad_info.family == CHIP_BONAIRE) &&
		    radv_pipeline_has_gs(pipeline))
			pipeline->graphics.partial_vs_wave = true;
		/* Needed for 028B6C_DISTRIBUTION_MODE != 0 */
		if (device->has_distributed_tess) {
			if (radv_pipeline_has_gs(pipeline)) {
				if (device->physical_device->rad_info.family == CHIP_TONGA ||
				    device->physical_device->rad_info.family == CHIP_FIJI ||
				    device->physical_device->rad_info.family == CHIP_POLARIS10 ||
				    device->physical_device->rad_info.family == CHIP_POLARIS11 ||
				    device->physical_device->rad_info.family == CHIP_POLARIS12)
					pipeline->graphics.partial_vs_wave = true;
			} else {
				pipeline->graphics.partial_vs_wave = true;
			}
		}
	}

	pipeline->graphics.base_ia_multi_vgt_param =
		S_028AA8_PRIMGROUP_SIZE(pipeline->graphics.primgroup_size - 1) |
		/* The following field was moved to VGT_SHADER_STAGES_EN in GFX9. */
		S_028AA8_MAX_PRIMGRP_IN_WAVE(device->physical_device->rad_info.chip_class == VI ? 2 : 0) |
		S_030960_EN_INST_OPT_BASIC(device->physical_device->rad_info.chip_class >= GFX9) |
		S_030960_EN_INST_OPT_ADV(device->physical_device->rad_info.chip_class >= GFX9);

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

	for (uint32_t i = 0; i < MESA_SHADER_STAGES; i++)
		pipeline->user_data_0[i] = radv_pipeline_stage_to_user_data_0(pipeline, i, device->physical_device->rad_info.chip_class);

	struct ac_userdata_info *loc = radv_lookup_user_sgpr(pipeline, MESA_SHADER_VERTEX,
							     AC_UD_VS_BASE_VERTEX_START_INSTANCE);
	if (loc->sgpr_idx != -1) {
		pipeline->graphics.vtx_base_sgpr = pipeline->user_data_0[MESA_SHADER_VERTEX];
		pipeline->graphics.vtx_base_sgpr += loc->sgpr_idx * 4;
		if (radv_get_vertex_shader(pipeline)->info.info.vs.needs_draw_id)
			pipeline->graphics.vtx_emit_num = 3;
		else
			pipeline->graphics.vtx_emit_num = 2;
	}

	pipeline->graphics.vtx_reuse_depth = 30;
	if (radv_pipeline_has_tess(pipeline) &&
	    radv_get_tess_eval_shader(pipeline)->info.tes.spacing == TESS_SPACING_FRACTIONAL_ODD) {
		pipeline->graphics.vtx_reuse_depth = 14;
	}

	if (device->instance->debug_flags & RADV_DEBUG_DUMP_SHADER_STATS) {
		radv_dump_pipeline_stats(device, pipeline);
	}

	result = radv_pipeline_scratch_init(device, pipeline);
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

	pStages[MESA_SHADER_COMPUTE] = &pCreateInfo->stage;
	radv_create_shaders(pipeline, device, cache, (struct radv_pipeline_key) {0}, pStages);

	pipeline->user_data_0[MESA_SHADER_COMPUTE] = radv_pipeline_stage_to_user_data_0(pipeline, MESA_SHADER_COMPUTE, device->physical_device->rad_info.chip_class);
	pipeline->need_indirect_descriptor_sets |= pipeline->shaders[MESA_SHADER_COMPUTE]->info.need_indirect_descriptor_sets;
	result = radv_pipeline_scratch_init(device, pipeline);
	if (result != VK_SUCCESS) {
		radv_pipeline_destroy(device, pipeline, pAllocator);
		return result;
	}

	*pPipeline = radv_pipeline_to_handle(pipeline);

	if (device->instance->debug_flags & RADV_DEBUG_DUMP_SHADER_STATS) {
		radv_dump_pipeline_stats(device, pipeline);
	}
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
