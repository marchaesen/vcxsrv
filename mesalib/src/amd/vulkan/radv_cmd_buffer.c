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
#include "radv_radeon_winsys.h"
#include "radv_shader.h"
#include "radv_cs.h"
#include "sid.h"
#include "gfx9d.h"
#include "vk_format.h"
#include "radv_debug.h"
#include "radv_meta.h"

#include "ac_debug.h"

enum {
	RADV_PREFETCH_VBO_DESCRIPTORS	= (1 << 0),
	RADV_PREFETCH_VS		= (1 << 1),
	RADV_PREFETCH_TCS		= (1 << 2),
	RADV_PREFETCH_TES		= (1 << 3),
	RADV_PREFETCH_GS		= (1 << 4),
	RADV_PREFETCH_PS		= (1 << 5),
	RADV_PREFETCH_SHADERS		= (RADV_PREFETCH_VS  |
					   RADV_PREFETCH_TCS |
					   RADV_PREFETCH_TES |
					   RADV_PREFETCH_GS  |
					   RADV_PREFETCH_PS)
};

static void radv_handle_image_transition(struct radv_cmd_buffer *cmd_buffer,
					 struct radv_image *image,
					 VkImageLayout src_layout,
					 VkImageLayout dst_layout,
					 uint32_t src_family,
					 uint32_t dst_family,
					 const VkImageSubresourceRange *range,
					 VkImageAspectFlags pending_clears);

const struct radv_dynamic_state default_dynamic_state = {
	.viewport = {
		.count = 0,
	},
	.scissor = {
		.count = 0,
	},
	.line_width = 1.0f,
	.depth_bias = {
		.bias = 0.0f,
		.clamp = 0.0f,
		.slope = 0.0f,
	},
	.blend_constants = { 0.0f, 0.0f, 0.0f, 0.0f },
	.depth_bounds = {
		.min = 0.0f,
		.max = 1.0f,
	},
	.stencil_compare_mask = {
		.front = ~0u,
		.back = ~0u,
	},
	.stencil_write_mask = {
		.front = ~0u,
		.back = ~0u,
	},
	.stencil_reference = {
		.front = 0u,
		.back = 0u,
	},
};

static void
radv_bind_dynamic_state(struct radv_cmd_buffer *cmd_buffer,
			const struct radv_dynamic_state *src)
{
	struct radv_dynamic_state *dest = &cmd_buffer->state.dynamic;
	uint32_t copy_mask = src->mask;
	uint32_t dest_mask = 0;

	/* Make sure to copy the number of viewports/scissors because they can
	 * only be specified at pipeline creation time.
	 */
	dest->viewport.count = src->viewport.count;
	dest->scissor.count = src->scissor.count;
	dest->discard_rectangle.count = src->discard_rectangle.count;

	if (copy_mask & RADV_DYNAMIC_VIEWPORT) {
		if (memcmp(&dest->viewport.viewports, &src->viewport.viewports,
			   src->viewport.count * sizeof(VkViewport))) {
			typed_memcpy(dest->viewport.viewports,
				     src->viewport.viewports,
				     src->viewport.count);
			dest_mask |= RADV_DYNAMIC_VIEWPORT;
		}
	}

	if (copy_mask & RADV_DYNAMIC_SCISSOR) {
		if (memcmp(&dest->scissor.scissors, &src->scissor.scissors,
			   src->scissor.count * sizeof(VkRect2D))) {
			typed_memcpy(dest->scissor.scissors,
				     src->scissor.scissors, src->scissor.count);
			dest_mask |= RADV_DYNAMIC_SCISSOR;
		}
	}

	if (copy_mask & RADV_DYNAMIC_LINE_WIDTH) {
		if (dest->line_width != src->line_width) {
			dest->line_width = src->line_width;
			dest_mask |= RADV_DYNAMIC_LINE_WIDTH;
		}
	}

	if (copy_mask & RADV_DYNAMIC_DEPTH_BIAS) {
		if (memcmp(&dest->depth_bias, &src->depth_bias,
			   sizeof(src->depth_bias))) {
			dest->depth_bias = src->depth_bias;
			dest_mask |= RADV_DYNAMIC_DEPTH_BIAS;
		}
	}

	if (copy_mask & RADV_DYNAMIC_BLEND_CONSTANTS) {
		if (memcmp(&dest->blend_constants, &src->blend_constants,
			   sizeof(src->blend_constants))) {
			typed_memcpy(dest->blend_constants,
				     src->blend_constants, 4);
			dest_mask |= RADV_DYNAMIC_BLEND_CONSTANTS;
		}
	}

	if (copy_mask & RADV_DYNAMIC_DEPTH_BOUNDS) {
		if (memcmp(&dest->depth_bounds, &src->depth_bounds,
			   sizeof(src->depth_bounds))) {
			dest->depth_bounds = src->depth_bounds;
			dest_mask |= RADV_DYNAMIC_DEPTH_BOUNDS;
		}
	}

	if (copy_mask & RADV_DYNAMIC_STENCIL_COMPARE_MASK) {
		if (memcmp(&dest->stencil_compare_mask,
			   &src->stencil_compare_mask,
			   sizeof(src->stencil_compare_mask))) {
			dest->stencil_compare_mask = src->stencil_compare_mask;
			dest_mask |= RADV_DYNAMIC_STENCIL_COMPARE_MASK;
		}
	}

	if (copy_mask & RADV_DYNAMIC_STENCIL_WRITE_MASK) {
		if (memcmp(&dest->stencil_write_mask, &src->stencil_write_mask,
			   sizeof(src->stencil_write_mask))) {
			dest->stencil_write_mask = src->stencil_write_mask;
			dest_mask |= RADV_DYNAMIC_STENCIL_WRITE_MASK;
		}
	}

	if (copy_mask & RADV_DYNAMIC_STENCIL_REFERENCE) {
		if (memcmp(&dest->stencil_reference, &src->stencil_reference,
			   sizeof(src->stencil_reference))) {
			dest->stencil_reference = src->stencil_reference;
			dest_mask |= RADV_DYNAMIC_STENCIL_REFERENCE;
		}
	}

	if (copy_mask & RADV_DYNAMIC_DISCARD_RECTANGLE) {
		if (memcmp(&dest->discard_rectangle.rectangles, &src->discard_rectangle.rectangles,
			   src->discard_rectangle.count * sizeof(VkRect2D))) {
			typed_memcpy(dest->discard_rectangle.rectangles,
				     src->discard_rectangle.rectangles,
				     src->discard_rectangle.count);
			dest_mask |= RADV_DYNAMIC_DISCARD_RECTANGLE;
		}
	}

	cmd_buffer->state.dirty |= dest_mask;
}

bool radv_cmd_buffer_uses_mec(struct radv_cmd_buffer *cmd_buffer)
{
	return cmd_buffer->queue_family_index == RADV_QUEUE_COMPUTE &&
	       cmd_buffer->device->physical_device->rad_info.chip_class >= CIK;
}

enum ring_type radv_queue_family_to_ring(int f) {
	switch (f) {
	case RADV_QUEUE_GENERAL:
		return RING_GFX;
	case RADV_QUEUE_COMPUTE:
		return RING_COMPUTE;
	case RADV_QUEUE_TRANSFER:
		return RING_DMA;
	default:
		unreachable("Unknown queue family");
	}
}

static VkResult radv_create_cmd_buffer(
	struct radv_device *                         device,
	struct radv_cmd_pool *                       pool,
	VkCommandBufferLevel                        level,
	VkCommandBuffer*                            pCommandBuffer)
{
	struct radv_cmd_buffer *cmd_buffer;
	unsigned ring;
	cmd_buffer = vk_zalloc(&pool->alloc, sizeof(*cmd_buffer), 8,
			       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (cmd_buffer == NULL)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	cmd_buffer->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
	cmd_buffer->device = device;
	cmd_buffer->pool = pool;
	cmd_buffer->level = level;

	if (pool) {
		list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);
		cmd_buffer->queue_family_index = pool->queue_family_index;

	} else {
		/* Init the pool_link so we can safefly call list_del when we destroy
		 * the command buffer
		 */
		list_inithead(&cmd_buffer->pool_link);
		cmd_buffer->queue_family_index = RADV_QUEUE_GENERAL;
	}

	ring = radv_queue_family_to_ring(cmd_buffer->queue_family_index);

	cmd_buffer->cs = device->ws->cs_create(device->ws, ring);
	if (!cmd_buffer->cs) {
		vk_free(&cmd_buffer->pool->alloc, cmd_buffer);
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
	}

	*pCommandBuffer = radv_cmd_buffer_to_handle(cmd_buffer);

	list_inithead(&cmd_buffer->upload.list);

	return VK_SUCCESS;
}

static void
radv_cmd_buffer_destroy(struct radv_cmd_buffer *cmd_buffer)
{
	list_del(&cmd_buffer->pool_link);

	list_for_each_entry_safe(struct radv_cmd_buffer_upload, up,
				 &cmd_buffer->upload.list, list) {
		cmd_buffer->device->ws->buffer_destroy(up->upload_bo);
		list_del(&up->list);
		free(up);
	}

	if (cmd_buffer->upload.upload_bo)
		cmd_buffer->device->ws->buffer_destroy(cmd_buffer->upload.upload_bo);
	cmd_buffer->device->ws->cs_destroy(cmd_buffer->cs);

	for (unsigned i = 0; i < VK_PIPELINE_BIND_POINT_RANGE_SIZE; i++)
		free(cmd_buffer->descriptors[i].push_set.set.mapped_ptr);

	vk_free(&cmd_buffer->pool->alloc, cmd_buffer);
}

static VkResult
radv_reset_cmd_buffer(struct radv_cmd_buffer *cmd_buffer)
{

	cmd_buffer->device->ws->cs_reset(cmd_buffer->cs);

	list_for_each_entry_safe(struct radv_cmd_buffer_upload, up,
				 &cmd_buffer->upload.list, list) {
		cmd_buffer->device->ws->buffer_destroy(up->upload_bo);
		list_del(&up->list);
		free(up);
	}

	cmd_buffer->push_constant_stages = 0;
	cmd_buffer->scratch_size_needed = 0;
	cmd_buffer->compute_scratch_size_needed = 0;
	cmd_buffer->esgs_ring_size_needed = 0;
	cmd_buffer->gsvs_ring_size_needed = 0;
	cmd_buffer->tess_rings_needed = false;
	cmd_buffer->sample_positions_needed = false;

	if (cmd_buffer->upload.upload_bo)
		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs,
				   cmd_buffer->upload.upload_bo, 8);
	cmd_buffer->upload.offset = 0;

	cmd_buffer->record_result = VK_SUCCESS;

	cmd_buffer->ring_offsets_idx = -1;

	for (unsigned i = 0; i < VK_PIPELINE_BIND_POINT_RANGE_SIZE; i++) {
		cmd_buffer->descriptors[i].dirty = 0;
		cmd_buffer->descriptors[i].valid = 0;
		cmd_buffer->descriptors[i].push_dirty = false;
	}

	if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9) {
		void *fence_ptr;
		radv_cmd_buffer_upload_alloc(cmd_buffer, 8, 0,
					     &cmd_buffer->gfx9_fence_offset,
					     &fence_ptr);
		cmd_buffer->gfx9_fence_bo = cmd_buffer->upload.upload_bo;
	}

	cmd_buffer->status = RADV_CMD_BUFFER_STATUS_INITIAL;

	return cmd_buffer->record_result;
}

static bool
radv_cmd_buffer_resize_upload_buf(struct radv_cmd_buffer *cmd_buffer,
				  uint64_t min_needed)
{
	uint64_t new_size;
	struct radeon_winsys_bo *bo;
	struct radv_cmd_buffer_upload *upload;
	struct radv_device *device = cmd_buffer->device;

	new_size = MAX2(min_needed, 16 * 1024);
	new_size = MAX2(new_size, 2 * cmd_buffer->upload.size);

	bo = device->ws->buffer_create(device->ws,
				       new_size, 4096,
				       RADEON_DOMAIN_GTT,
				       RADEON_FLAG_CPU_ACCESS|
				       RADEON_FLAG_NO_INTERPROCESS_SHARING);

	if (!bo) {
		cmd_buffer->record_result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
		return false;
	}

	radv_cs_add_buffer(device->ws, cmd_buffer->cs, bo, 8);
	if (cmd_buffer->upload.upload_bo) {
		upload = malloc(sizeof(*upload));

		if (!upload) {
			cmd_buffer->record_result = VK_ERROR_OUT_OF_HOST_MEMORY;
			device->ws->buffer_destroy(bo);
			return false;
		}

		memcpy(upload, &cmd_buffer->upload, sizeof(*upload));
		list_add(&upload->list, &cmd_buffer->upload.list);
	}

	cmd_buffer->upload.upload_bo = bo;
	cmd_buffer->upload.size = new_size;
	cmd_buffer->upload.offset = 0;
	cmd_buffer->upload.map = device->ws->buffer_map(cmd_buffer->upload.upload_bo);

	if (!cmd_buffer->upload.map) {
		cmd_buffer->record_result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
		return false;
	}

	return true;
}

bool
radv_cmd_buffer_upload_alloc(struct radv_cmd_buffer *cmd_buffer,
			     unsigned size,
			     unsigned alignment,
			     unsigned *out_offset,
			     void **ptr)
{
	uint64_t offset = align(cmd_buffer->upload.offset, alignment);
	if (offset + size > cmd_buffer->upload.size) {
		if (!radv_cmd_buffer_resize_upload_buf(cmd_buffer, size))
			return false;
		offset = 0;
	}

	*out_offset = offset;
	*ptr = cmd_buffer->upload.map + offset;

	cmd_buffer->upload.offset = offset + size;
	return true;
}

bool
radv_cmd_buffer_upload_data(struct radv_cmd_buffer *cmd_buffer,
			    unsigned size, unsigned alignment,
			    const void *data, unsigned *out_offset)
{
	uint8_t *ptr;

	if (!radv_cmd_buffer_upload_alloc(cmd_buffer, size, alignment,
					  out_offset, (void **)&ptr))
		return false;

	if (ptr)
		memcpy(ptr, data, size);

	return true;
}

static void
radv_emit_write_data_packet(struct radeon_winsys_cs *cs, uint64_t va,
			    unsigned count, const uint32_t *data)
{
	radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 2 + count, 0));
	radeon_emit(cs, S_370_DST_SEL(V_370_MEM_ASYNC) |
		    S_370_WR_CONFIRM(1) |
		    S_370_ENGINE_SEL(V_370_ME));
	radeon_emit(cs, va);
	radeon_emit(cs, va >> 32);
	radeon_emit_array(cs, data, count);
}

void radv_cmd_buffer_trace_emit(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_device *device = cmd_buffer->device;
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	uint64_t va;

	va = radv_buffer_get_va(device->trace_bo);
	if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
		va += 4;

	MAYBE_UNUSED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 7);

	++cmd_buffer->state.trace_id;
	radv_cs_add_buffer(device->ws, cs, device->trace_bo, 8);
	radv_emit_write_data_packet(cs, va, 1, &cmd_buffer->state.trace_id);
	radeon_emit(cs, PKT3(PKT3_NOP, 0, 0));
	radeon_emit(cs, AC_ENCODE_TRACE_POINT(cmd_buffer->state.trace_id));
}

static void
radv_cmd_buffer_after_draw(struct radv_cmd_buffer *cmd_buffer,
			   enum radv_cmd_flush_bits flags)
{
	if (cmd_buffer->device->instance->debug_flags & RADV_DEBUG_SYNC_SHADERS) {
		uint32_t *ptr = NULL;
		uint64_t va = 0;

		assert(flags & (RADV_CMD_FLAG_PS_PARTIAL_FLUSH |
				RADV_CMD_FLAG_CS_PARTIAL_FLUSH));

		if (cmd_buffer->device->physical_device->rad_info.chip_class == GFX9) {
			va = radv_buffer_get_va(cmd_buffer->gfx9_fence_bo) +
			     cmd_buffer->gfx9_fence_offset;
			ptr = &cmd_buffer->gfx9_fence_idx;
		}

		/* Force wait for graphics or compute engines to be idle. */
		si_cs_emit_cache_flush(cmd_buffer->cs,
				       cmd_buffer->device->physical_device->rad_info.chip_class,
				       ptr, va,
				       radv_cmd_buffer_uses_mec(cmd_buffer),
				       flags);
	}

	if (unlikely(cmd_buffer->device->trace_bo))
		radv_cmd_buffer_trace_emit(cmd_buffer);
}

static void
radv_save_pipeline(struct radv_cmd_buffer *cmd_buffer,
		   struct radv_pipeline *pipeline, enum ring_type ring)
{
	struct radv_device *device = cmd_buffer->device;
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	uint32_t data[2];
	uint64_t va;

	va = radv_buffer_get_va(device->trace_bo);

	switch (ring) {
	case RING_GFX:
		va += 8;
		break;
	case RING_COMPUTE:
		va += 16;
		break;
	default:
		assert(!"invalid ring type");
	}

	MAYBE_UNUSED unsigned cdw_max = radeon_check_space(device->ws,
							   cmd_buffer->cs, 6);

	data[0] = (uintptr_t)pipeline;
	data[1] = (uintptr_t)pipeline >> 32;

	radv_cs_add_buffer(device->ws, cs, device->trace_bo, 8);
	radv_emit_write_data_packet(cs, va, 2, data);
}

void radv_set_descriptor_set(struct radv_cmd_buffer *cmd_buffer,
			     VkPipelineBindPoint bind_point,
			     struct radv_descriptor_set *set,
			     unsigned idx)
{
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, bind_point);

	descriptors_state->sets[idx] = set;
	if (set)
		descriptors_state->valid |= (1u << idx);
	else
		descriptors_state->valid &= ~(1u << idx);
	descriptors_state->dirty |= (1u << idx);
}

static void
radv_save_descriptors(struct radv_cmd_buffer *cmd_buffer,
		      VkPipelineBindPoint bind_point)
{
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, bind_point);
	struct radv_device *device = cmd_buffer->device;
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	uint32_t data[MAX_SETS * 2] = {};
	uint64_t va;
	unsigned i;
	va = radv_buffer_get_va(device->trace_bo) + 24;

	MAYBE_UNUSED unsigned cdw_max = radeon_check_space(device->ws,
							   cmd_buffer->cs, 4 + MAX_SETS * 2);

	for_each_bit(i, descriptors_state->valid) {
		struct radv_descriptor_set *set = descriptors_state->sets[i];
		data[i * 2] = (uintptr_t)set;
		data[i * 2 + 1] = (uintptr_t)set >> 32;
	}

	radv_cs_add_buffer(device->ws, cs, device->trace_bo, 8);
	radv_emit_write_data_packet(cs, va, MAX_SETS * 2, data);
}

struct radv_userdata_info *
radv_lookup_user_sgpr(struct radv_pipeline *pipeline,
		      gl_shader_stage stage,
		      int idx)
{
	if (stage == MESA_SHADER_VERTEX) {
		if (pipeline->shaders[MESA_SHADER_VERTEX])
			return &pipeline->shaders[MESA_SHADER_VERTEX]->info.user_sgprs_locs.shader_data[idx];
		if (pipeline->shaders[MESA_SHADER_TESS_CTRL])
			return &pipeline->shaders[MESA_SHADER_TESS_CTRL]->info.user_sgprs_locs.shader_data[idx];
		if (pipeline->shaders[MESA_SHADER_GEOMETRY])
			return &pipeline->shaders[MESA_SHADER_GEOMETRY]->info.user_sgprs_locs.shader_data[idx];
	} else if (stage == MESA_SHADER_TESS_EVAL) {
		if (pipeline->shaders[MESA_SHADER_TESS_EVAL])
			return &pipeline->shaders[MESA_SHADER_TESS_EVAL]->info.user_sgprs_locs.shader_data[idx];
		if (pipeline->shaders[MESA_SHADER_GEOMETRY])
			return &pipeline->shaders[MESA_SHADER_GEOMETRY]->info.user_sgprs_locs.shader_data[idx];
	}
	return &pipeline->shaders[stage]->info.user_sgprs_locs.shader_data[idx];
}

static void
radv_emit_userdata_address(struct radv_cmd_buffer *cmd_buffer,
			   struct radv_pipeline *pipeline,
			   gl_shader_stage stage,
			   int idx, uint64_t va)
{
	struct radv_userdata_info *loc = radv_lookup_user_sgpr(pipeline, stage, idx);
	uint32_t base_reg = pipeline->user_data_0[stage];
	if (loc->sgpr_idx == -1)
		return;
	assert(loc->num_sgprs == 2);
	assert(!loc->indirect);
	radeon_set_sh_reg_seq(cmd_buffer->cs, base_reg + loc->sgpr_idx * 4, 2);
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);
}

static void
radv_update_multisample_state(struct radv_cmd_buffer *cmd_buffer,
			      struct radv_pipeline *pipeline)
{
	int num_samples = pipeline->graphics.ms.num_samples;
	struct radv_multisample_state *ms = &pipeline->graphics.ms;
	struct radv_pipeline *old_pipeline = cmd_buffer->state.emitted_pipeline;

	if (pipeline->shaders[MESA_SHADER_FRAGMENT]->info.info.ps.needs_sample_positions)
		cmd_buffer->sample_positions_needed = true;

	if (old_pipeline && num_samples == old_pipeline->graphics.ms.num_samples)
		return;

	radeon_set_context_reg_seq(cmd_buffer->cs, R_028BDC_PA_SC_LINE_CNTL, 2);
	radeon_emit(cmd_buffer->cs, ms->pa_sc_line_cntl);
	radeon_emit(cmd_buffer->cs, ms->pa_sc_aa_config);

	radeon_set_context_reg(cmd_buffer->cs, R_028A48_PA_SC_MODE_CNTL_0, ms->pa_sc_mode_cntl_0);

	radv_cayman_emit_msaa_sample_locs(cmd_buffer->cs, num_samples);

	/* GFX9: Flush DFSM when the AA mode changes. */
	if (cmd_buffer->device->dfsm_allowed) {
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cmd_buffer->cs, EVENT_TYPE(V_028A90_FLUSH_DFSM) | EVENT_INDEX(0));
	}
}

static void
radv_emit_shader_prefetch(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_shader_variant *shader)
{
	uint64_t va;

	if (!shader)
		return;

	va = radv_buffer_get_va(shader->bo) + shader->bo_offset;

	si_cp_dma_prefetch(cmd_buffer, va, shader->code_size);
}

static void
radv_emit_prefetch_L2(struct radv_cmd_buffer *cmd_buffer,
		      struct radv_pipeline *pipeline,
		      bool vertex_stage_only)
{
	struct radv_cmd_state *state = &cmd_buffer->state;
	uint32_t mask = state->prefetch_L2_mask;

	if (vertex_stage_only) {
		/* Fast prefetch path for starting draws as soon as possible.
		 */
		mask = state->prefetch_L2_mask & (RADV_PREFETCH_VS |
						  RADV_PREFETCH_VBO_DESCRIPTORS);
	}

	if (mask & RADV_PREFETCH_VS)
		radv_emit_shader_prefetch(cmd_buffer,
					  pipeline->shaders[MESA_SHADER_VERTEX]);

	if (mask & RADV_PREFETCH_VBO_DESCRIPTORS)
		si_cp_dma_prefetch(cmd_buffer, state->vb_va, state->vb_size);

	if (mask & RADV_PREFETCH_TCS)
		radv_emit_shader_prefetch(cmd_buffer,
					  pipeline->shaders[MESA_SHADER_TESS_CTRL]);

	if (mask & RADV_PREFETCH_TES)
		radv_emit_shader_prefetch(cmd_buffer,
					  pipeline->shaders[MESA_SHADER_TESS_EVAL]);

	if (mask & RADV_PREFETCH_GS) {
		radv_emit_shader_prefetch(cmd_buffer,
					  pipeline->shaders[MESA_SHADER_GEOMETRY]);
		radv_emit_shader_prefetch(cmd_buffer, pipeline->gs_copy_shader);
	}

	if (mask & RADV_PREFETCH_PS)
		radv_emit_shader_prefetch(cmd_buffer,
					  pipeline->shaders[MESA_SHADER_FRAGMENT]);

	state->prefetch_L2_mask &= ~mask;
}

static void
radv_emit_rbplus_state(struct radv_cmd_buffer *cmd_buffer)
{
	if (!cmd_buffer->device->physical_device->rbplus_allowed)
		return;

	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;
	struct radv_framebuffer *framebuffer = cmd_buffer->state.framebuffer;
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;

	unsigned sx_ps_downconvert = 0;
	unsigned sx_blend_opt_epsilon = 0;
	unsigned sx_blend_opt_control = 0;

	for (unsigned i = 0; i < subpass->color_count; ++i) {
		if (subpass->color_attachments[i].attachment == VK_ATTACHMENT_UNUSED)
			continue;

		int idx = subpass->color_attachments[i].attachment;
		struct radv_color_buffer_info *cb = &framebuffer->attachments[idx].cb;

		unsigned format = G_028C70_FORMAT(cb->cb_color_info);
		unsigned swap = G_028C70_COMP_SWAP(cb->cb_color_info);
		uint32_t spi_format = (pipeline->graphics.col_format >> (i * 4)) & 0xf;
		uint32_t colormask = (pipeline->graphics.cb_target_mask >> (i * 4)) & 0xf;

		bool has_alpha, has_rgb;

		/* Set if RGB and A are present. */
		has_alpha = !G_028C74_FORCE_DST_ALPHA_1(cb->cb_color_attrib);

		if (format == V_028C70_COLOR_8 ||
		    format == V_028C70_COLOR_16 ||
		    format == V_028C70_COLOR_32)
			has_rgb = !has_alpha;
		else
			has_rgb = true;

		/* Check the colormask and export format. */
		if (!(colormask & 0x7))
			has_rgb = false;
		if (!(colormask & 0x8))
			has_alpha = false;

		if (spi_format == V_028714_SPI_SHADER_ZERO) {
			has_rgb = false;
			has_alpha = false;
		}

		/* Disable value checking for disabled channels. */
		if (!has_rgb)
			sx_blend_opt_control |= S_02875C_MRT0_COLOR_OPT_DISABLE(1) << (i * 4);
		if (!has_alpha)
			sx_blend_opt_control |= S_02875C_MRT0_ALPHA_OPT_DISABLE(1) << (i * 4);

		/* Enable down-conversion for 32bpp and smaller formats. */
		switch (format) {
		case V_028C70_COLOR_8:
		case V_028C70_COLOR_8_8:
		case V_028C70_COLOR_8_8_8_8:
			/* For 1 and 2-channel formats, use the superset thereof. */
			if (spi_format == V_028714_SPI_SHADER_FP16_ABGR ||
			    spi_format == V_028714_SPI_SHADER_UINT16_ABGR ||
			    spi_format == V_028714_SPI_SHADER_SINT16_ABGR) {
				sx_ps_downconvert |= V_028754_SX_RT_EXPORT_8_8_8_8 << (i * 4);
				sx_blend_opt_epsilon |= V_028758_8BIT_FORMAT << (i * 4);
			}
			break;

		case V_028C70_COLOR_5_6_5:
			if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
				sx_ps_downconvert |= V_028754_SX_RT_EXPORT_5_6_5 << (i * 4);
				sx_blend_opt_epsilon |= V_028758_6BIT_FORMAT << (i * 4);
			}
			break;

		case V_028C70_COLOR_1_5_5_5:
			if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
				sx_ps_downconvert |= V_028754_SX_RT_EXPORT_1_5_5_5 << (i * 4);
				sx_blend_opt_epsilon |= V_028758_5BIT_FORMAT << (i * 4);
			}
			break;

		case V_028C70_COLOR_4_4_4_4:
			if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
				sx_ps_downconvert |= V_028754_SX_RT_EXPORT_4_4_4_4 << (i * 4);
				sx_blend_opt_epsilon |= V_028758_4BIT_FORMAT << (i * 4);
			}
			break;

		case V_028C70_COLOR_32:
			if (swap == V_028C70_SWAP_STD &&
			    spi_format == V_028714_SPI_SHADER_32_R)
				sx_ps_downconvert |= V_028754_SX_RT_EXPORT_32_R << (i * 4);
			else if (swap == V_028C70_SWAP_ALT_REV &&
				 spi_format == V_028714_SPI_SHADER_32_AR)
				sx_ps_downconvert |= V_028754_SX_RT_EXPORT_32_A << (i * 4);
			break;

		case V_028C70_COLOR_16:
		case V_028C70_COLOR_16_16:
			/* For 1-channel formats, use the superset thereof. */
			if (spi_format == V_028714_SPI_SHADER_UNORM16_ABGR ||
			    spi_format == V_028714_SPI_SHADER_SNORM16_ABGR ||
			    spi_format == V_028714_SPI_SHADER_UINT16_ABGR ||
			    spi_format == V_028714_SPI_SHADER_SINT16_ABGR) {
				if (swap == V_028C70_SWAP_STD ||
				    swap == V_028C70_SWAP_STD_REV)
					sx_ps_downconvert |= V_028754_SX_RT_EXPORT_16_16_GR << (i * 4);
				else
					sx_ps_downconvert |= V_028754_SX_RT_EXPORT_16_16_AR << (i * 4);
			}
			break;

		case V_028C70_COLOR_10_11_11:
			if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
				sx_ps_downconvert |= V_028754_SX_RT_EXPORT_10_11_11 << (i * 4);
				sx_blend_opt_epsilon |= V_028758_11BIT_FORMAT << (i * 4);
			}
			break;

		case V_028C70_COLOR_2_10_10_10:
			if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
				sx_ps_downconvert |= V_028754_SX_RT_EXPORT_2_10_10_10 << (i * 4);
				sx_blend_opt_epsilon |= V_028758_10BIT_FORMAT << (i * 4);
			}
			break;
		}
	}

	radeon_set_context_reg_seq(cmd_buffer->cs, R_028754_SX_PS_DOWNCONVERT, 3);
	radeon_emit(cmd_buffer->cs, sx_ps_downconvert);
	radeon_emit(cmd_buffer->cs, sx_blend_opt_epsilon);
	radeon_emit(cmd_buffer->cs, sx_blend_opt_control);
}

static void
radv_emit_graphics_pipeline(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;

	if (!pipeline || cmd_buffer->state.emitted_pipeline == pipeline)
		return;

	radv_update_multisample_state(cmd_buffer, pipeline);

	cmd_buffer->scratch_size_needed =
	                          MAX2(cmd_buffer->scratch_size_needed,
	                               pipeline->max_waves * pipeline->scratch_bytes_per_wave);

	if (!cmd_buffer->state.emitted_pipeline ||
	    cmd_buffer->state.emitted_pipeline->graphics.can_use_guardband !=
	     pipeline->graphics.can_use_guardband)
		cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_SCISSOR;

	radeon_emit_array(cmd_buffer->cs, pipeline->cs.buf, pipeline->cs.cdw);

	for (unsigned i = 0; i < MESA_SHADER_COMPUTE; i++) {
		if (!pipeline->shaders[i])
			continue;

		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs,
				   pipeline->shaders[i]->bo, 8);
	}

	if (radv_pipeline_has_gs(pipeline))
		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs,
				   pipeline->gs_copy_shader->bo, 8);

	if (unlikely(cmd_buffer->device->trace_bo))
		radv_save_pipeline(cmd_buffer, pipeline, RING_GFX);

	cmd_buffer->state.emitted_pipeline = pipeline;

	cmd_buffer->state.dirty &= ~RADV_CMD_DIRTY_PIPELINE;
}

static void
radv_emit_viewport(struct radv_cmd_buffer *cmd_buffer)
{
	si_write_viewport(cmd_buffer->cs, 0, cmd_buffer->state.dynamic.viewport.count,
			  cmd_buffer->state.dynamic.viewport.viewports);
}

static void
radv_emit_scissor(struct radv_cmd_buffer *cmd_buffer)
{
	uint32_t count = cmd_buffer->state.dynamic.scissor.count;

	/* Vega10/Raven scissor bug workaround. This must be done before VPORT
	 * scissor registers are changed. There is also a more efficient but
	 * more involved alternative workaround.
	 */
	if (cmd_buffer->device->physical_device->has_scissor_bug) {
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_PS_PARTIAL_FLUSH;
		si_emit_cache_flush(cmd_buffer);
	}
	si_write_scissors(cmd_buffer->cs, 0, count,
			  cmd_buffer->state.dynamic.scissor.scissors,
			  cmd_buffer->state.dynamic.viewport.viewports,
			  cmd_buffer->state.emitted_pipeline->graphics.can_use_guardband);
}

static void
radv_emit_discard_rectangle(struct radv_cmd_buffer *cmd_buffer)
{
	if (!cmd_buffer->state.dynamic.discard_rectangle.count)
		return;

	radeon_set_context_reg_seq(cmd_buffer->cs, R_028210_PA_SC_CLIPRECT_0_TL,
	                           cmd_buffer->state.dynamic.discard_rectangle.count * 2);
	for (unsigned i = 0; i < cmd_buffer->state.dynamic.discard_rectangle.count; ++i) {
		VkRect2D rect = cmd_buffer->state.dynamic.discard_rectangle.rectangles[i];
		radeon_emit(cmd_buffer->cs, S_028210_TL_X(rect.offset.x) | S_028210_TL_Y(rect.offset.y));
		radeon_emit(cmd_buffer->cs, S_028214_BR_X(rect.offset.x + rect.extent.width) |
		                            S_028214_BR_Y(rect.offset.y + rect.extent.height));
	}
}

static void
radv_emit_line_width(struct radv_cmd_buffer *cmd_buffer)
{
	unsigned width = cmd_buffer->state.dynamic.line_width * 8;

	radeon_set_context_reg(cmd_buffer->cs, R_028A08_PA_SU_LINE_CNTL,
			       S_028A08_WIDTH(CLAMP(width, 0, 0xFFF)));
}

static void
radv_emit_blend_constants(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_dynamic_state *d = &cmd_buffer->state.dynamic;

	radeon_set_context_reg_seq(cmd_buffer->cs, R_028414_CB_BLEND_RED, 4);
	radeon_emit_array(cmd_buffer->cs, (uint32_t *)d->blend_constants, 4);
}

static void
radv_emit_stencil(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_dynamic_state *d = &cmd_buffer->state.dynamic;

	radeon_set_context_reg_seq(cmd_buffer->cs,
				   R_028430_DB_STENCILREFMASK, 2);
	radeon_emit(cmd_buffer->cs,
		    S_028430_STENCILTESTVAL(d->stencil_reference.front) |
		    S_028430_STENCILMASK(d->stencil_compare_mask.front) |
		    S_028430_STENCILWRITEMASK(d->stencil_write_mask.front) |
		    S_028430_STENCILOPVAL(1));
	radeon_emit(cmd_buffer->cs,
		    S_028434_STENCILTESTVAL_BF(d->stencil_reference.back) |
		    S_028434_STENCILMASK_BF(d->stencil_compare_mask.back) |
		    S_028434_STENCILWRITEMASK_BF(d->stencil_write_mask.back) |
		    S_028434_STENCILOPVAL_BF(1));
}

static void
radv_emit_depth_bounds(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_dynamic_state *d = &cmd_buffer->state.dynamic;

	radeon_set_context_reg(cmd_buffer->cs, R_028020_DB_DEPTH_BOUNDS_MIN,
			       fui(d->depth_bounds.min));
	radeon_set_context_reg(cmd_buffer->cs, R_028024_DB_DEPTH_BOUNDS_MAX,
			       fui(d->depth_bounds.max));
}

static void
radv_emit_depth_bias(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_dynamic_state *d = &cmd_buffer->state.dynamic;
	unsigned slope = fui(d->depth_bias.slope * 16.0f);
	unsigned bias = fui(d->depth_bias.bias * cmd_buffer->state.offset_scale);


	radeon_set_context_reg_seq(cmd_buffer->cs,
				   R_028B7C_PA_SU_POLY_OFFSET_CLAMP, 5);
	radeon_emit(cmd_buffer->cs, fui(d->depth_bias.clamp)); /* CLAMP */
	radeon_emit(cmd_buffer->cs, slope); /* FRONT SCALE */
	radeon_emit(cmd_buffer->cs, bias); /* FRONT OFFSET */
	radeon_emit(cmd_buffer->cs, slope); /* BACK SCALE */
	radeon_emit(cmd_buffer->cs, bias); /* BACK OFFSET */
}

static void
radv_emit_fb_color_state(struct radv_cmd_buffer *cmd_buffer,
			 int index,
			 struct radv_attachment_info *att,
			 struct radv_image *image,
			 VkImageLayout layout)
{
	bool is_vi = cmd_buffer->device->physical_device->rad_info.chip_class >= VI;
	struct radv_color_buffer_info *cb = &att->cb;
	uint32_t cb_color_info = cb->cb_color_info;

	if (!radv_layout_dcc_compressed(image, layout,
	                                radv_image_queue_family_mask(image,
	                                                             cmd_buffer->queue_family_index,
	                                                             cmd_buffer->queue_family_index))) {
		cb_color_info &= C_028C70_DCC_ENABLE;
	}

	if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9) {
		radeon_set_context_reg_seq(cmd_buffer->cs, R_028C60_CB_COLOR0_BASE + index * 0x3c, 11);
		radeon_emit(cmd_buffer->cs, cb->cb_color_base);
		radeon_emit(cmd_buffer->cs, S_028C64_BASE_256B(cb->cb_color_base >> 32));
		radeon_emit(cmd_buffer->cs, cb->cb_color_attrib2);
		radeon_emit(cmd_buffer->cs, cb->cb_color_view);
		radeon_emit(cmd_buffer->cs, cb_color_info);
		radeon_emit(cmd_buffer->cs, cb->cb_color_attrib);
		radeon_emit(cmd_buffer->cs, cb->cb_dcc_control);
		radeon_emit(cmd_buffer->cs, cb->cb_color_cmask);
		radeon_emit(cmd_buffer->cs, S_028C80_BASE_256B(cb->cb_color_cmask >> 32));
		radeon_emit(cmd_buffer->cs, cb->cb_color_fmask);
		radeon_emit(cmd_buffer->cs, S_028C88_BASE_256B(cb->cb_color_fmask >> 32));

		radeon_set_context_reg_seq(cmd_buffer->cs, R_028C94_CB_COLOR0_DCC_BASE + index * 0x3c, 2);
		radeon_emit(cmd_buffer->cs, cb->cb_dcc_base);
		radeon_emit(cmd_buffer->cs, S_028C98_BASE_256B(cb->cb_dcc_base >> 32));
		
		radeon_set_context_reg(cmd_buffer->cs, R_0287A0_CB_MRT0_EPITCH + index * 4,
				       S_0287A0_EPITCH(att->attachment->image->surface.u.gfx9.surf.epitch));
	} else {
		radeon_set_context_reg_seq(cmd_buffer->cs, R_028C60_CB_COLOR0_BASE + index * 0x3c, 11);
		radeon_emit(cmd_buffer->cs, cb->cb_color_base);
		radeon_emit(cmd_buffer->cs, cb->cb_color_pitch);
		radeon_emit(cmd_buffer->cs, cb->cb_color_slice);
		radeon_emit(cmd_buffer->cs, cb->cb_color_view);
		radeon_emit(cmd_buffer->cs, cb_color_info);
		radeon_emit(cmd_buffer->cs, cb->cb_color_attrib);
		radeon_emit(cmd_buffer->cs, cb->cb_dcc_control);
		radeon_emit(cmd_buffer->cs, cb->cb_color_cmask);
		radeon_emit(cmd_buffer->cs, cb->cb_color_cmask_slice);
		radeon_emit(cmd_buffer->cs, cb->cb_color_fmask);
		radeon_emit(cmd_buffer->cs, cb->cb_color_fmask_slice);

		if (is_vi) { /* DCC BASE */
			radeon_set_context_reg(cmd_buffer->cs, R_028C94_CB_COLOR0_DCC_BASE + index * 0x3c, cb->cb_dcc_base);
		}
	}
}

static void
radv_emit_fb_ds_state(struct radv_cmd_buffer *cmd_buffer,
		      struct radv_ds_buffer_info *ds,
		      struct radv_image *image,
		      VkImageLayout layout)
{
	uint32_t db_z_info = ds->db_z_info;
	uint32_t db_stencil_info = ds->db_stencil_info;

	if (!radv_layout_has_htile(image, layout,
	                           radv_image_queue_family_mask(image,
	                                                        cmd_buffer->queue_family_index,
	                                                        cmd_buffer->queue_family_index))) {
		db_z_info &= C_028040_TILE_SURFACE_ENABLE;
		db_stencil_info |= S_028044_TILE_STENCIL_DISABLE(1);
	}

	radeon_set_context_reg(cmd_buffer->cs, R_028008_DB_DEPTH_VIEW, ds->db_depth_view);
	radeon_set_context_reg(cmd_buffer->cs, R_028ABC_DB_HTILE_SURFACE, ds->db_htile_surface);


	if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9) {
		radeon_set_context_reg_seq(cmd_buffer->cs, R_028014_DB_HTILE_DATA_BASE, 3);
		radeon_emit(cmd_buffer->cs, ds->db_htile_data_base);
		radeon_emit(cmd_buffer->cs, S_028018_BASE_HI(ds->db_htile_data_base >> 32));
		radeon_emit(cmd_buffer->cs, ds->db_depth_size);

		radeon_set_context_reg_seq(cmd_buffer->cs, R_028038_DB_Z_INFO, 10);
		radeon_emit(cmd_buffer->cs, db_z_info);			/* DB_Z_INFO */
		radeon_emit(cmd_buffer->cs, db_stencil_info);	        /* DB_STENCIL_INFO */
		radeon_emit(cmd_buffer->cs, ds->db_z_read_base);	/* DB_Z_READ_BASE */
		radeon_emit(cmd_buffer->cs, S_028044_BASE_HI(ds->db_z_read_base >> 32));	/* DB_Z_READ_BASE_HI */
		radeon_emit(cmd_buffer->cs, ds->db_stencil_read_base);	/* DB_STENCIL_READ_BASE */
		radeon_emit(cmd_buffer->cs, S_02804C_BASE_HI(ds->db_stencil_read_base >> 32)); /* DB_STENCIL_READ_BASE_HI */
		radeon_emit(cmd_buffer->cs, ds->db_z_write_base);	/* DB_Z_WRITE_BASE */
		radeon_emit(cmd_buffer->cs, S_028054_BASE_HI(ds->db_z_write_base >> 32));	/* DB_Z_WRITE_BASE_HI */
		radeon_emit(cmd_buffer->cs, ds->db_stencil_write_base);	/* DB_STENCIL_WRITE_BASE */
		radeon_emit(cmd_buffer->cs, S_02805C_BASE_HI(ds->db_stencil_write_base >> 32)); /* DB_STENCIL_WRITE_BASE_HI */

		radeon_set_context_reg_seq(cmd_buffer->cs, R_028068_DB_Z_INFO2, 2);
		radeon_emit(cmd_buffer->cs, ds->db_z_info2);
		radeon_emit(cmd_buffer->cs, ds->db_stencil_info2);
	} else {
		radeon_set_context_reg(cmd_buffer->cs, R_028014_DB_HTILE_DATA_BASE, ds->db_htile_data_base);

		radeon_set_context_reg_seq(cmd_buffer->cs, R_02803C_DB_DEPTH_INFO, 9);
		radeon_emit(cmd_buffer->cs, ds->db_depth_info);	/* R_02803C_DB_DEPTH_INFO */
		radeon_emit(cmd_buffer->cs, db_z_info);			/* R_028040_DB_Z_INFO */
		radeon_emit(cmd_buffer->cs, db_stencil_info);	        /* R_028044_DB_STENCIL_INFO */
		radeon_emit(cmd_buffer->cs, ds->db_z_read_base);	/* R_028048_DB_Z_READ_BASE */
		radeon_emit(cmd_buffer->cs, ds->db_stencil_read_base);	/* R_02804C_DB_STENCIL_READ_BASE */
		radeon_emit(cmd_buffer->cs, ds->db_z_write_base);	/* R_028050_DB_Z_WRITE_BASE */
		radeon_emit(cmd_buffer->cs, ds->db_stencil_write_base);	/* R_028054_DB_STENCIL_WRITE_BASE */
		radeon_emit(cmd_buffer->cs, ds->db_depth_size);	/* R_028058_DB_DEPTH_SIZE */
		radeon_emit(cmd_buffer->cs, ds->db_depth_slice);	/* R_02805C_DB_DEPTH_SLICE */

	}

	radeon_set_context_reg(cmd_buffer->cs, R_028B78_PA_SU_POLY_OFFSET_DB_FMT_CNTL,
			       ds->pa_su_poly_offset_db_fmt_cntl);
}

void
radv_set_depth_clear_regs(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_image *image,
			  VkClearDepthStencilValue ds_clear_value,
			  VkImageAspectFlags aspects)
{
	uint64_t va = radv_buffer_get_va(image->bo);
	va += image->offset + image->clear_value_offset;
	unsigned reg_offset = 0, reg_count = 0;

	assert(radv_image_has_htile(image));

	if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
		++reg_count;
	} else {
		++reg_offset;
		va += 4;
	}
	if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
		++reg_count;

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_WRITE_DATA, 2 + reg_count, 0));
	radeon_emit(cmd_buffer->cs, S_370_DST_SEL(V_370_MEM_ASYNC) |
				    S_370_WR_CONFIRM(1) |
				    S_370_ENGINE_SEL(V_370_PFP));
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);
	if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
		radeon_emit(cmd_buffer->cs, ds_clear_value.stencil);
	if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
		radeon_emit(cmd_buffer->cs, fui(ds_clear_value.depth));

	radeon_set_context_reg_seq(cmd_buffer->cs, R_028028_DB_STENCIL_CLEAR + 4 * reg_offset, reg_count);
	if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
		radeon_emit(cmd_buffer->cs, ds_clear_value.stencil); /* R_028028_DB_STENCIL_CLEAR */
	if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
		radeon_emit(cmd_buffer->cs, fui(ds_clear_value.depth)); /* R_02802C_DB_DEPTH_CLEAR */
}

static void
radv_load_depth_clear_regs(struct radv_cmd_buffer *cmd_buffer,
			   struct radv_image *image)
{
	VkImageAspectFlags aspects = vk_format_aspects(image->vk_format);
	uint64_t va = radv_buffer_get_va(image->bo);
	va += image->offset + image->clear_value_offset;
	unsigned reg_offset = 0, reg_count = 0;

	if (!radv_image_has_htile(image))
		return;

	if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
		++reg_count;
	} else {
		++reg_offset;
		va += 4;
	}
	if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
		++reg_count;

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_COPY_DATA, 4, 0));
	radeon_emit(cmd_buffer->cs, COPY_DATA_SRC_SEL(COPY_DATA_MEM) |
				    COPY_DATA_DST_SEL(COPY_DATA_REG) |
				    (reg_count == 2 ? COPY_DATA_COUNT_SEL : 0));
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);
	radeon_emit(cmd_buffer->cs, (R_028028_DB_STENCIL_CLEAR + 4 * reg_offset) >> 2);
	radeon_emit(cmd_buffer->cs, 0);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
	radeon_emit(cmd_buffer->cs, 0);
}

/*
 *with DCC some colors don't require CMASK elimiation before being
 * used as a texture. This sets a predicate value to determine if the
 * cmask eliminate is required.
 */
void
radv_set_dcc_need_cmask_elim_pred(struct radv_cmd_buffer *cmd_buffer,
				  struct radv_image *image,
				  bool value)
{
	uint64_t pred_val = value;
	uint64_t va = radv_buffer_get_va(image->bo);
	va += image->offset + image->dcc_pred_offset;

	assert(radv_image_has_dcc(image));

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_WRITE_DATA, 4, 0));
	radeon_emit(cmd_buffer->cs, S_370_DST_SEL(V_370_MEM_ASYNC) |
				    S_370_WR_CONFIRM(1) |
				    S_370_ENGINE_SEL(V_370_PFP));
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);
	radeon_emit(cmd_buffer->cs, pred_val);
	radeon_emit(cmd_buffer->cs, pred_val >> 32);
}

void
radv_set_color_clear_regs(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_image *image,
			  int idx,
			  uint32_t color_values[2])
{
	uint64_t va = radv_buffer_get_va(image->bo);
	va += image->offset + image->clear_value_offset;

	assert(radv_image_has_cmask(image) || radv_image_has_dcc(image));

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_WRITE_DATA, 4, 0));
	radeon_emit(cmd_buffer->cs, S_370_DST_SEL(V_370_MEM_ASYNC) |
				    S_370_WR_CONFIRM(1) |
				    S_370_ENGINE_SEL(V_370_PFP));
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);
	radeon_emit(cmd_buffer->cs, color_values[0]);
	radeon_emit(cmd_buffer->cs, color_values[1]);

	radeon_set_context_reg_seq(cmd_buffer->cs, R_028C8C_CB_COLOR0_CLEAR_WORD0 + idx * 0x3c, 2);
	radeon_emit(cmd_buffer->cs, color_values[0]);
	radeon_emit(cmd_buffer->cs, color_values[1]);
}

static void
radv_load_color_clear_regs(struct radv_cmd_buffer *cmd_buffer,
			   struct radv_image *image,
			   int idx)
{
	uint64_t va = radv_buffer_get_va(image->bo);
	va += image->offset + image->clear_value_offset;

	if (!radv_image_has_cmask(image) && !radv_image_has_dcc(image))
		return;

	uint32_t reg = R_028C8C_CB_COLOR0_CLEAR_WORD0 + idx * 0x3c;

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_COPY_DATA, 4, cmd_buffer->state.predicating));
	radeon_emit(cmd_buffer->cs, COPY_DATA_SRC_SEL(COPY_DATA_MEM) |
				    COPY_DATA_DST_SEL(COPY_DATA_REG) |
				    COPY_DATA_COUNT_SEL);
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);
	radeon_emit(cmd_buffer->cs, reg >> 2);
	radeon_emit(cmd_buffer->cs, 0);

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_PFP_SYNC_ME, 0, cmd_buffer->state.predicating));
	radeon_emit(cmd_buffer->cs, 0);
}

static void
radv_emit_framebuffer_state(struct radv_cmd_buffer *cmd_buffer)
{
	int i;
	struct radv_framebuffer *framebuffer = cmd_buffer->state.framebuffer;
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;

	/* this may happen for inherited secondary recording */
	if (!framebuffer)
		return;

	for (i = 0; i < 8; ++i) {
		if (i >= subpass->color_count || subpass->color_attachments[i].attachment == VK_ATTACHMENT_UNUSED) {
			radeon_set_context_reg(cmd_buffer->cs, R_028C70_CB_COLOR0_INFO + i * 0x3C,
				       S_028C70_FORMAT(V_028C70_COLOR_INVALID));
			continue;
		}

		int idx = subpass->color_attachments[i].attachment;
		struct radv_attachment_info *att = &framebuffer->attachments[idx];
		struct radv_image *image = att->attachment->image;
		VkImageLayout layout = subpass->color_attachments[i].layout;

		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, att->attachment->bo, 8);

		assert(att->attachment->aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT);
		radv_emit_fb_color_state(cmd_buffer, i, att, image, layout);

		radv_load_color_clear_regs(cmd_buffer, image, i);
	}

	if(subpass->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED) {
		int idx = subpass->depth_stencil_attachment.attachment;
		VkImageLayout layout = subpass->depth_stencil_attachment.layout;
		struct radv_attachment_info *att = &framebuffer->attachments[idx];
		struct radv_image *image = att->attachment->image;
		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, att->attachment->bo, 8);
		MAYBE_UNUSED uint32_t queue_mask = radv_image_queue_family_mask(image,
										cmd_buffer->queue_family_index,
										cmd_buffer->queue_family_index);
		/* We currently don't support writing decompressed HTILE */
		assert(radv_layout_has_htile(image, layout, queue_mask) ==
		       radv_layout_is_htile_compressed(image, layout, queue_mask));

		radv_emit_fb_ds_state(cmd_buffer, &att->ds, image, layout);

		if (att->ds.offset_scale != cmd_buffer->state.offset_scale) {
			cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS;
			cmd_buffer->state.offset_scale = att->ds.offset_scale;
		}
		radv_load_depth_clear_regs(cmd_buffer, image);
	} else {
		if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9)
			radeon_set_context_reg_seq(cmd_buffer->cs, R_028038_DB_Z_INFO, 2);
		else
			radeon_set_context_reg_seq(cmd_buffer->cs, R_028040_DB_Z_INFO, 2);

		radeon_emit(cmd_buffer->cs, S_028040_FORMAT(V_028040_Z_INVALID)); /* DB_Z_INFO */
		radeon_emit(cmd_buffer->cs, S_028044_FORMAT(V_028044_STENCIL_INVALID)); /* DB_STENCIL_INFO */
	}
	radeon_set_context_reg(cmd_buffer->cs, R_028208_PA_SC_WINDOW_SCISSOR_BR,
			       S_028208_BR_X(framebuffer->width) |
			       S_028208_BR_Y(framebuffer->height));

	if (cmd_buffer->device->dfsm_allowed) {
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cmd_buffer->cs, EVENT_TYPE(V_028A90_BREAK_BATCH) | EVENT_INDEX(0));
	}

	cmd_buffer->state.dirty &= ~RADV_CMD_DIRTY_FRAMEBUFFER;
}

static void
radv_emit_index_buffer(struct radv_cmd_buffer *cmd_buffer)
{
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (state->index_type != state->last_index_type) {
		if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9) {
			radeon_set_uconfig_reg_idx(cs, R_03090C_VGT_INDEX_TYPE,
						   2, state->index_type);
		} else {
			radeon_emit(cs, PKT3(PKT3_INDEX_TYPE, 0, 0));
			radeon_emit(cs, state->index_type);
		}

		state->last_index_type = state->index_type;
	}

	radeon_emit(cs, PKT3(PKT3_INDEX_BASE, 1, 0));
	radeon_emit(cs, state->index_va);
	radeon_emit(cs, state->index_va >> 32);

	radeon_emit(cs, PKT3(PKT3_INDEX_BUFFER_SIZE, 0, 0));
	radeon_emit(cs, state->max_index_count);

	cmd_buffer->state.dirty &= ~RADV_CMD_DIRTY_INDEX_BUFFER;
}

void radv_set_db_count_control(struct radv_cmd_buffer *cmd_buffer)
{
	bool has_perfect_queries = cmd_buffer->state.perfect_occlusion_queries_enabled;
	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;
	uint32_t pa_sc_mode_cntl_1 =
		pipeline ? pipeline->graphics.ms.pa_sc_mode_cntl_1 : 0;
	uint32_t db_count_control;

	if(!cmd_buffer->state.active_occlusion_queries) {
		if (cmd_buffer->device->physical_device->rad_info.chip_class >= CIK) {
			if (G_028A4C_OUT_OF_ORDER_PRIMITIVE_ENABLE(pa_sc_mode_cntl_1) &&
			    pipeline->graphics.disable_out_of_order_rast_for_occlusion &&
			    has_perfect_queries) {
				/* Re-enable out-of-order rasterization if the
				 * bound pipeline supports it and if it's has
				 * been disabled before starting any perfect
				 * occlusion queries.
				 */
				radeon_set_context_reg(cmd_buffer->cs,
						       R_028A4C_PA_SC_MODE_CNTL_1,
						       pa_sc_mode_cntl_1);
			}
			db_count_control = 0;
		} else {
			db_count_control = S_028004_ZPASS_INCREMENT_DISABLE(1);
		}
	} else {
		const struct radv_subpass *subpass = cmd_buffer->state.subpass;
		uint32_t sample_rate = subpass ? util_logbase2(subpass->max_sample_count) : 0;

		if (cmd_buffer->device->physical_device->rad_info.chip_class >= CIK) {
			db_count_control =
				S_028004_PERFECT_ZPASS_COUNTS(has_perfect_queries) |
				S_028004_SAMPLE_RATE(sample_rate) |
				S_028004_ZPASS_ENABLE(1) |
				S_028004_SLICE_EVEN_ENABLE(1) |
				S_028004_SLICE_ODD_ENABLE(1);

			if (G_028A4C_OUT_OF_ORDER_PRIMITIVE_ENABLE(pa_sc_mode_cntl_1) &&
			    pipeline->graphics.disable_out_of_order_rast_for_occlusion &&
			    has_perfect_queries) {
				/* If the bound pipeline has enabled
				 * out-of-order rasterization, we should
				 * disable it before starting any perfect
				 * occlusion queries.
				 */
				pa_sc_mode_cntl_1 &= C_028A4C_OUT_OF_ORDER_PRIMITIVE_ENABLE;

				radeon_set_context_reg(cmd_buffer->cs,
						       R_028A4C_PA_SC_MODE_CNTL_1,
						       pa_sc_mode_cntl_1);
			}
		} else {
			db_count_control = S_028004_PERFECT_ZPASS_COUNTS(1) |
				S_028004_SAMPLE_RATE(sample_rate);
		}
	}

	radeon_set_context_reg(cmd_buffer->cs, R_028004_DB_COUNT_CONTROL, db_count_control);
}

static void
radv_cmd_buffer_flush_dynamic_state(struct radv_cmd_buffer *cmd_buffer)
{
	uint32_t states = cmd_buffer->state.dirty & cmd_buffer->state.emitted_pipeline->graphics.needed_dynamic_state;

	if (states & (RADV_CMD_DIRTY_DYNAMIC_VIEWPORT))
		radv_emit_viewport(cmd_buffer);

	if (states & (RADV_CMD_DIRTY_DYNAMIC_SCISSOR | RADV_CMD_DIRTY_DYNAMIC_VIEWPORT))
		radv_emit_scissor(cmd_buffer);

	if (states & RADV_CMD_DIRTY_DYNAMIC_LINE_WIDTH)
		radv_emit_line_width(cmd_buffer);

	if (states & RADV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS)
		radv_emit_blend_constants(cmd_buffer);

	if (states & (RADV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE |
				       RADV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK |
				       RADV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK))
		radv_emit_stencil(cmd_buffer);

	if (states & RADV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS)
		radv_emit_depth_bounds(cmd_buffer);

	if (states & RADV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS)
		radv_emit_depth_bias(cmd_buffer);

	if (states & RADV_CMD_DIRTY_DYNAMIC_DISCARD_RECTANGLE)
		radv_emit_discard_rectangle(cmd_buffer);

	cmd_buffer->state.dirty &= ~states;
}

static void
emit_stage_descriptor_set_userdata(struct radv_cmd_buffer *cmd_buffer,
				   struct radv_pipeline *pipeline,
				   int idx,
				   uint64_t va,
				   gl_shader_stage stage)
{
	struct radv_userdata_info *desc_set_loc = &pipeline->shaders[stage]->info.user_sgprs_locs.descriptor_sets[idx];
	uint32_t base_reg = pipeline->user_data_0[stage];

	if (desc_set_loc->sgpr_idx == -1 || desc_set_loc->indirect)
		return;

	assert(!desc_set_loc->indirect);
	assert(desc_set_loc->num_sgprs == 2);
	radeon_set_sh_reg_seq(cmd_buffer->cs,
			      base_reg + desc_set_loc->sgpr_idx * 4, 2);
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);
}

static void
radv_emit_descriptor_set_userdata(struct radv_cmd_buffer *cmd_buffer,
				  VkShaderStageFlags stages,
				  struct radv_descriptor_set *set,
				  unsigned idx)
{
	if (cmd_buffer->state.pipeline) {
		radv_foreach_stage(stage, stages) {
			if (cmd_buffer->state.pipeline->shaders[stage])
				emit_stage_descriptor_set_userdata(cmd_buffer, cmd_buffer->state.pipeline,
								   idx, set->va,
								   stage);
		}
	}

	if (cmd_buffer->state.compute_pipeline && (stages & VK_SHADER_STAGE_COMPUTE_BIT))
		emit_stage_descriptor_set_userdata(cmd_buffer, cmd_buffer->state.compute_pipeline,
						   idx, set->va,
						   MESA_SHADER_COMPUTE);
}

static void
radv_flush_push_descriptors(struct radv_cmd_buffer *cmd_buffer,
			    VkPipelineBindPoint bind_point)
{
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, bind_point);
	struct radv_descriptor_set *set = &descriptors_state->push_set.set;
	unsigned bo_offset;

	if (!radv_cmd_buffer_upload_data(cmd_buffer, set->size, 32,
					 set->mapped_ptr,
					 &bo_offset))
		return;

	set->va = radv_buffer_get_va(cmd_buffer->upload.upload_bo);
	set->va += bo_offset;
}

static void
radv_flush_indirect_descriptor_sets(struct radv_cmd_buffer *cmd_buffer,
				    VkPipelineBindPoint bind_point)
{
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, bind_point);
	uint32_t size = MAX_SETS * 2 * 4;
	uint32_t offset;
	void *ptr;
	
	if (!radv_cmd_buffer_upload_alloc(cmd_buffer, size,
					  256, &offset, &ptr))
		return;

	for (unsigned i = 0; i < MAX_SETS; i++) {
		uint32_t *uptr = ((uint32_t *)ptr) + i * 2;
		uint64_t set_va = 0;
		struct radv_descriptor_set *set = descriptors_state->sets[i];
		if (descriptors_state->valid & (1u << i))
			set_va = set->va;
		uptr[0] = set_va & 0xffffffff;
		uptr[1] = set_va >> 32;
	}

	uint64_t va = radv_buffer_get_va(cmd_buffer->upload.upload_bo);
	va += offset;

	if (cmd_buffer->state.pipeline) {
		if (cmd_buffer->state.pipeline->shaders[MESA_SHADER_VERTEX])
			radv_emit_userdata_address(cmd_buffer, cmd_buffer->state.pipeline, MESA_SHADER_VERTEX,
						   AC_UD_INDIRECT_DESCRIPTOR_SETS, va);

		if (cmd_buffer->state.pipeline->shaders[MESA_SHADER_FRAGMENT])
			radv_emit_userdata_address(cmd_buffer, cmd_buffer->state.pipeline, MESA_SHADER_FRAGMENT,
						   AC_UD_INDIRECT_DESCRIPTOR_SETS, va);

		if (radv_pipeline_has_gs(cmd_buffer->state.pipeline))
			radv_emit_userdata_address(cmd_buffer, cmd_buffer->state.pipeline, MESA_SHADER_GEOMETRY,
						   AC_UD_INDIRECT_DESCRIPTOR_SETS, va);

		if (radv_pipeline_has_tess(cmd_buffer->state.pipeline))
			radv_emit_userdata_address(cmd_buffer, cmd_buffer->state.pipeline, MESA_SHADER_TESS_CTRL,
						   AC_UD_INDIRECT_DESCRIPTOR_SETS, va);

		if (radv_pipeline_has_tess(cmd_buffer->state.pipeline))
			radv_emit_userdata_address(cmd_buffer, cmd_buffer->state.pipeline, MESA_SHADER_TESS_EVAL,
						   AC_UD_INDIRECT_DESCRIPTOR_SETS, va);
	}

	if (cmd_buffer->state.compute_pipeline)
		radv_emit_userdata_address(cmd_buffer, cmd_buffer->state.compute_pipeline, MESA_SHADER_COMPUTE,
					   AC_UD_INDIRECT_DESCRIPTOR_SETS, va);
}

static void
radv_flush_descriptors(struct radv_cmd_buffer *cmd_buffer,
		       VkShaderStageFlags stages)
{
	VkPipelineBindPoint bind_point = stages & VK_SHADER_STAGE_COMPUTE_BIT ?
					 VK_PIPELINE_BIND_POINT_COMPUTE :
					 VK_PIPELINE_BIND_POINT_GRAPHICS;
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, bind_point);
	unsigned i;

	if (!descriptors_state->dirty)
		return;

	if (descriptors_state->push_dirty)
		radv_flush_push_descriptors(cmd_buffer, bind_point);

	if ((cmd_buffer->state.pipeline && cmd_buffer->state.pipeline->need_indirect_descriptor_sets) ||
	    (cmd_buffer->state.compute_pipeline && cmd_buffer->state.compute_pipeline->need_indirect_descriptor_sets)) {
		radv_flush_indirect_descriptor_sets(cmd_buffer, bind_point);
	}

	MAYBE_UNUSED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws,
	                                                   cmd_buffer->cs,
	                                                   MAX_SETS * MESA_SHADER_STAGES * 4);

	for_each_bit(i, descriptors_state->dirty) {
		struct radv_descriptor_set *set = descriptors_state->sets[i];
		if (!(descriptors_state->valid & (1u << i)))
			continue;

		radv_emit_descriptor_set_userdata(cmd_buffer, stages, set, i);
	}
	descriptors_state->dirty = 0;
	descriptors_state->push_dirty = false;

	if (unlikely(cmd_buffer->device->trace_bo))
		radv_save_descriptors(cmd_buffer, bind_point);

	assert(cmd_buffer->cs->cdw <= cdw_max);
}

static void
radv_flush_constants(struct radv_cmd_buffer *cmd_buffer,
		     VkShaderStageFlags stages)
{
	struct radv_pipeline *pipeline = stages & VK_SHADER_STAGE_COMPUTE_BIT
					 ? cmd_buffer->state.compute_pipeline
					 : cmd_buffer->state.pipeline;
	struct radv_pipeline_layout *layout = pipeline->layout;
	unsigned offset;
	void *ptr;
	uint64_t va;

	stages &= cmd_buffer->push_constant_stages;
	if (!stages ||
	    (!layout->push_constant_size && !layout->dynamic_offset_count))
		return;

	if (!radv_cmd_buffer_upload_alloc(cmd_buffer, layout->push_constant_size +
					  16 * layout->dynamic_offset_count,
					  256, &offset, &ptr))
		return;

	memcpy(ptr, cmd_buffer->push_constants, layout->push_constant_size);
	memcpy((char*)ptr + layout->push_constant_size, cmd_buffer->dynamic_buffers,
	       16 * layout->dynamic_offset_count);

	va = radv_buffer_get_va(cmd_buffer->upload.upload_bo);
	va += offset;

	MAYBE_UNUSED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws,
	                                                   cmd_buffer->cs, MESA_SHADER_STAGES * 4);

	radv_foreach_stage(stage, stages) {
		if (pipeline->shaders[stage]) {
			radv_emit_userdata_address(cmd_buffer, pipeline, stage,
						   AC_UD_PUSH_CONSTANTS, va);
		}
	}

	cmd_buffer->push_constant_stages &= ~stages;
	assert(cmd_buffer->cs->cdw <= cdw_max);
}

static void
radv_flush_vertex_descriptors(struct radv_cmd_buffer *cmd_buffer,
			      bool pipeline_is_dirty)
{
	if ((pipeline_is_dirty ||
	    (cmd_buffer->state.dirty & RADV_CMD_DIRTY_VERTEX_BUFFER)) &&
	    cmd_buffer->state.pipeline->vertex_elements.count &&
	    radv_get_vertex_shader(cmd_buffer->state.pipeline)->info.info.vs.has_vertex_buffers) {
		struct radv_vertex_elements_info *velems = &cmd_buffer->state.pipeline->vertex_elements;
		unsigned vb_offset;
		void *vb_ptr;
		uint32_t i = 0;
		uint32_t count = velems->count;
		uint64_t va;

		/* allocate some descriptor state for vertex buffers */
		if (!radv_cmd_buffer_upload_alloc(cmd_buffer, count * 16, 256,
						  &vb_offset, &vb_ptr))
			return;

		for (i = 0; i < count; i++) {
			uint32_t *desc = &((uint32_t *)vb_ptr)[i * 4];
			uint32_t offset;
			int vb = velems->binding[i];
			struct radv_buffer *buffer = cmd_buffer->vertex_bindings[vb].buffer;
			uint32_t stride = cmd_buffer->state.pipeline->binding_stride[vb];

			va = radv_buffer_get_va(buffer->bo);

			offset = cmd_buffer->vertex_bindings[vb].offset + velems->offset[i];
			va += offset + buffer->offset;
			desc[0] = va;
			desc[1] = S_008F04_BASE_ADDRESS_HI(va >> 32) | S_008F04_STRIDE(stride);
			if (cmd_buffer->device->physical_device->rad_info.chip_class <= CIK && stride)
				desc[2] = (buffer->size - offset - velems->format_size[i]) / stride + 1;
			else
				desc[2] = buffer->size - offset;
			desc[3] = velems->rsrc_word3[i];
		}

		va = radv_buffer_get_va(cmd_buffer->upload.upload_bo);
		va += vb_offset;

		radv_emit_userdata_address(cmd_buffer, cmd_buffer->state.pipeline, MESA_SHADER_VERTEX,
					   AC_UD_VS_VERTEX_BUFFERS, va);

		cmd_buffer->state.vb_va = va;
		cmd_buffer->state.vb_size = count * 16;
		cmd_buffer->state.prefetch_L2_mask |= RADV_PREFETCH_VBO_DESCRIPTORS;
	}
	cmd_buffer->state.dirty &= ~RADV_CMD_DIRTY_VERTEX_BUFFER;
}

static void
radv_upload_graphics_shader_descriptors(struct radv_cmd_buffer *cmd_buffer, bool pipeline_is_dirty)
{
	radv_flush_vertex_descriptors(cmd_buffer, pipeline_is_dirty);
	radv_flush_descriptors(cmd_buffer, VK_SHADER_STAGE_ALL_GRAPHICS);
	radv_flush_constants(cmd_buffer, VK_SHADER_STAGE_ALL_GRAPHICS);
}

static void
radv_emit_draw_registers(struct radv_cmd_buffer *cmd_buffer, bool indexed_draw,
			 bool instanced_draw, bool indirect_draw,
			 uint32_t draw_vertex_count)
{
	struct radeon_info *info = &cmd_buffer->device->physical_device->rad_info;
	struct radv_cmd_state *state = &cmd_buffer->state;
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	uint32_t ia_multi_vgt_param;
	int32_t primitive_reset_en;

	/* Draw state. */
	ia_multi_vgt_param =
		si_get_ia_multi_vgt_param(cmd_buffer, instanced_draw,
					  indirect_draw, draw_vertex_count);

	if (state->last_ia_multi_vgt_param != ia_multi_vgt_param) {
		if (info->chip_class >= GFX9) {
			radeon_set_uconfig_reg_idx(cs,
						   R_030960_IA_MULTI_VGT_PARAM,
						   4, ia_multi_vgt_param);
		} else if (info->chip_class >= CIK) {
			radeon_set_context_reg_idx(cs,
						   R_028AA8_IA_MULTI_VGT_PARAM,
						   1, ia_multi_vgt_param);
		} else {
			radeon_set_context_reg(cs, R_028AA8_IA_MULTI_VGT_PARAM,
					       ia_multi_vgt_param);
		}
		state->last_ia_multi_vgt_param = ia_multi_vgt_param;
	}

	/* Primitive restart. */
	primitive_reset_en =
		indexed_draw && state->pipeline->graphics.prim_restart_enable;

	if (primitive_reset_en != state->last_primitive_reset_en) {
		state->last_primitive_reset_en = primitive_reset_en;
		if (info->chip_class >= GFX9) {
			radeon_set_uconfig_reg(cs,
					       R_03092C_VGT_MULTI_PRIM_IB_RESET_EN,
					       primitive_reset_en);
		} else {
			radeon_set_context_reg(cs,
					       R_028A94_VGT_MULTI_PRIM_IB_RESET_EN,
					       primitive_reset_en);
		}
	}

	if (primitive_reset_en) {
		uint32_t primitive_reset_index =
			state->index_type ? 0xffffffffu : 0xffffu;

		if (primitive_reset_index != state->last_primitive_reset_index) {
			radeon_set_context_reg(cs,
					       R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX,
					       primitive_reset_index);
			state->last_primitive_reset_index = primitive_reset_index;
		}
	}
}

static void radv_stage_flush(struct radv_cmd_buffer *cmd_buffer,
			     VkPipelineStageFlags src_stage_mask)
{
	if (src_stage_mask & (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
	                      VK_PIPELINE_STAGE_TRANSFER_BIT |
	                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT |
	                      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)) {
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH;
	}

	if (src_stage_mask & (VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
			      VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
			      VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
			      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
			      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
			      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			      VK_PIPELINE_STAGE_TRANSFER_BIT |
			      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT |
			      VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT |
			      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)) {
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_PS_PARTIAL_FLUSH;
	} else if (src_stage_mask & (VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
	                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
	                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)) {
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_VS_PARTIAL_FLUSH;
	}
}

static enum radv_cmd_flush_bits
radv_src_access_flush(struct radv_cmd_buffer *cmd_buffer,
				  VkAccessFlags src_flags)
{
	enum radv_cmd_flush_bits flush_bits = 0;
	uint32_t b;
	for_each_bit(b, src_flags) {
		switch ((VkAccessFlagBits)(1 << b)) {
		case VK_ACCESS_SHADER_WRITE_BIT:
			flush_bits |= RADV_CMD_FLAG_WRITEBACK_GLOBAL_L2;
			break;
		case VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:
			flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
			              RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
			break;
		case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
			flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB |
			              RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;
			break;
		case VK_ACCESS_TRANSFER_WRITE_BIT:
			flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
			              RADV_CMD_FLAG_FLUSH_AND_INV_CB_META |
			              RADV_CMD_FLAG_FLUSH_AND_INV_DB |
			              RADV_CMD_FLAG_FLUSH_AND_INV_DB_META |
			              RADV_CMD_FLAG_INV_GLOBAL_L2;
			break;
		default:
			break;
		}
	}
	return flush_bits;
}

static enum radv_cmd_flush_bits
radv_dst_access_flush(struct radv_cmd_buffer *cmd_buffer,
                      VkAccessFlags dst_flags,
                      struct radv_image *image)
{
	enum radv_cmd_flush_bits flush_bits = 0;
	uint32_t b;
	for_each_bit(b, dst_flags) {
		switch ((VkAccessFlagBits)(1 << b)) {
		case VK_ACCESS_INDIRECT_COMMAND_READ_BIT:
		case VK_ACCESS_INDEX_READ_BIT:
			break;
		case VK_ACCESS_UNIFORM_READ_BIT:
			flush_bits |= RADV_CMD_FLAG_INV_VMEM_L1 | RADV_CMD_FLAG_INV_SMEM_L1;
			break;
		case VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:
		case VK_ACCESS_SHADER_READ_BIT:
		case VK_ACCESS_TRANSFER_READ_BIT:
		case VK_ACCESS_INPUT_ATTACHMENT_READ_BIT:
			flush_bits |= RADV_CMD_FLAG_INV_VMEM_L1 |
			              RADV_CMD_FLAG_INV_GLOBAL_L2;
			break;
		case VK_ACCESS_COLOR_ATTACHMENT_READ_BIT:
			/* TODO: change to image && when the image gets passed
			 * through from the subpass. */
			if (!image || (image->usage & VK_IMAGE_USAGE_STORAGE_BIT))
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
				              RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
			break;
		case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT:
			if (!image || (image->usage & VK_IMAGE_USAGE_STORAGE_BIT))
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB |
				              RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;
			break;
		default:
			break;
		}
	}
	return flush_bits;
}

static void radv_subpass_barrier(struct radv_cmd_buffer *cmd_buffer, const struct radv_subpass_barrier *barrier)
{
	cmd_buffer->state.flush_bits |= radv_src_access_flush(cmd_buffer, barrier->src_access_mask);
	radv_stage_flush(cmd_buffer, barrier->src_stage_mask);
	cmd_buffer->state.flush_bits |= radv_dst_access_flush(cmd_buffer, barrier->dst_access_mask,
	                                                      NULL);
}

static void radv_handle_subpass_image_transition(struct radv_cmd_buffer *cmd_buffer,
						 VkAttachmentReference att)
{
	unsigned idx = att.attachment;
	struct radv_image_view *view = cmd_buffer->state.framebuffer->attachments[idx].attachment;
	VkImageSubresourceRange range;
	range.aspectMask = 0;
	range.baseMipLevel = view->base_mip;
	range.levelCount = 1;
	range.baseArrayLayer = view->base_layer;
	range.layerCount = cmd_buffer->state.framebuffer->layers;

	radv_handle_image_transition(cmd_buffer,
				     view->image,
				     cmd_buffer->state.attachments[idx].current_layout,
				     att.layout, 0, 0, &range,
				     cmd_buffer->state.attachments[idx].pending_clear_aspects);

	cmd_buffer->state.attachments[idx].current_layout = att.layout;


}

void
radv_cmd_buffer_set_subpass(struct radv_cmd_buffer *cmd_buffer,
			    const struct radv_subpass *subpass, bool transitions)
{
	if (transitions) {
		radv_subpass_barrier(cmd_buffer, &subpass->start_barrier);

		for (unsigned i = 0; i < subpass->color_count; ++i) {
			if (subpass->color_attachments[i].attachment != VK_ATTACHMENT_UNUSED)
				radv_handle_subpass_image_transition(cmd_buffer,
				                                     subpass->color_attachments[i]);
		}

		for (unsigned i = 0; i < subpass->input_count; ++i) {
			radv_handle_subpass_image_transition(cmd_buffer,
							subpass->input_attachments[i]);
		}

		if (subpass->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED) {
			radv_handle_subpass_image_transition(cmd_buffer,
							subpass->depth_stencil_attachment);
		}
	}

	cmd_buffer->state.subpass = subpass;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_FRAMEBUFFER;
}

static VkResult
radv_cmd_state_setup_attachments(struct radv_cmd_buffer *cmd_buffer,
				 struct radv_render_pass *pass,
				 const VkRenderPassBeginInfo *info)
{
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (pass->attachment_count == 0) {
		state->attachments = NULL;
		return VK_SUCCESS;
	}

	state->attachments = vk_alloc(&cmd_buffer->pool->alloc,
					pass->attachment_count *
					sizeof(state->attachments[0]),
					8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (state->attachments == NULL) {
		cmd_buffer->record_result = VK_ERROR_OUT_OF_HOST_MEMORY;
		return cmd_buffer->record_result;
	}

	for (uint32_t i = 0; i < pass->attachment_count; ++i) {
		struct radv_render_pass_attachment *att = &pass->attachments[i];
		VkImageAspectFlags att_aspects = vk_format_aspects(att->format);
		VkImageAspectFlags clear_aspects = 0;

		if (att_aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
			/* color attachment */
			if (att->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
				clear_aspects |= VK_IMAGE_ASPECT_COLOR_BIT;
			}
		} else {
			/* depthstencil attachment */
			if ((att_aspects & VK_IMAGE_ASPECT_DEPTH_BIT) &&
			    att->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
				clear_aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
				if ((att_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) &&
				    att->stencil_load_op == VK_ATTACHMENT_LOAD_OP_DONT_CARE)
					clear_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
			if ((att_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) &&
			    att->stencil_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
				clear_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
		}

		state->attachments[i].pending_clear_aspects = clear_aspects;
		state->attachments[i].cleared_views = 0;
		if (clear_aspects && info) {
			assert(info->clearValueCount > i);
			state->attachments[i].clear_value = info->pClearValues[i];
		}

		state->attachments[i].current_layout = att->initial_layout;
	}

	return VK_SUCCESS;
}

VkResult radv_AllocateCommandBuffers(
	VkDevice _device,
	const VkCommandBufferAllocateInfo *pAllocateInfo,
	VkCommandBuffer *pCommandBuffers)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_cmd_pool, pool, pAllocateInfo->commandPool);

	VkResult result = VK_SUCCESS;
	uint32_t i;

	for (i = 0; i < pAllocateInfo->commandBufferCount; i++) {

		if (!list_empty(&pool->free_cmd_buffers)) {
			struct radv_cmd_buffer *cmd_buffer = list_first_entry(&pool->free_cmd_buffers, struct radv_cmd_buffer, pool_link);

			list_del(&cmd_buffer->pool_link);
			list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);

			result = radv_reset_cmd_buffer(cmd_buffer);
			cmd_buffer->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
			cmd_buffer->level = pAllocateInfo->level;

			pCommandBuffers[i] = radv_cmd_buffer_to_handle(cmd_buffer);
		} else {
			result = radv_create_cmd_buffer(device, pool, pAllocateInfo->level,
			                                &pCommandBuffers[i]);
		}
		if (result != VK_SUCCESS)
			break;
	}

	if (result != VK_SUCCESS) {
		radv_FreeCommandBuffers(_device, pAllocateInfo->commandPool,
					i, pCommandBuffers);

		/* From the Vulkan 1.0.66 spec:
		 *
		 * "vkAllocateCommandBuffers can be used to create multiple
		 *  command buffers. If the creation of any of those command
		 *  buffers fails, the implementation must destroy all
		 *  successfully created command buffer objects from this
		 *  command, set all entries of the pCommandBuffers array to
		 *  NULL and return the error."
		 */
		memset(pCommandBuffers, 0,
		       sizeof(*pCommandBuffers) * pAllocateInfo->commandBufferCount);
	}

	return result;
}

void radv_FreeCommandBuffers(
	VkDevice device,
	VkCommandPool commandPool,
	uint32_t commandBufferCount,
	const VkCommandBuffer *pCommandBuffers)
{
	for (uint32_t i = 0; i < commandBufferCount; i++) {
		RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, pCommandBuffers[i]);

		if (cmd_buffer) {
			if (cmd_buffer->pool) {
				list_del(&cmd_buffer->pool_link);
				list_addtail(&cmd_buffer->pool_link, &cmd_buffer->pool->free_cmd_buffers);
			} else
				radv_cmd_buffer_destroy(cmd_buffer);

		}
	}
}

VkResult radv_ResetCommandBuffer(
	VkCommandBuffer commandBuffer,
	VkCommandBufferResetFlags flags)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	return radv_reset_cmd_buffer(cmd_buffer);
}

static void emit_gfx_buffer_state(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_device *device = cmd_buffer->device;
	if (device->gfx_init) {
		uint64_t va = radv_buffer_get_va(device->gfx_init);
		radv_cs_add_buffer(device->ws, cmd_buffer->cs, device->gfx_init, 8);
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_INDIRECT_BUFFER_CIK, 2, 0));
		radeon_emit(cmd_buffer->cs, va);
		radeon_emit(cmd_buffer->cs, va >> 32);
		radeon_emit(cmd_buffer->cs, device->gfx_init_size_dw & 0xffff);
	} else
		si_init_config(cmd_buffer);
}

VkResult radv_BeginCommandBuffer(
	VkCommandBuffer commandBuffer,
	const VkCommandBufferBeginInfo *pBeginInfo)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	VkResult result = VK_SUCCESS;

	if (cmd_buffer->status != RADV_CMD_BUFFER_STATUS_INITIAL) {
		/* If the command buffer has already been resetted with
		 * vkResetCommandBuffer, no need to do it again.
		 */
		result = radv_reset_cmd_buffer(cmd_buffer);
		if (result != VK_SUCCESS)
			return result;
	}

	memset(&cmd_buffer->state, 0, sizeof(cmd_buffer->state));
	cmd_buffer->state.last_primitive_reset_en = -1;
	cmd_buffer->state.last_index_type = -1;
	cmd_buffer->state.last_num_instances = -1;
	cmd_buffer->state.last_vertex_offset = -1;
	cmd_buffer->state.last_first_instance = -1;
	cmd_buffer->usage_flags = pBeginInfo->flags;

	/* setup initial configuration into command buffer */
	if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
		switch (cmd_buffer->queue_family_index) {
		case RADV_QUEUE_GENERAL:
			emit_gfx_buffer_state(cmd_buffer);
			break;
		case RADV_QUEUE_COMPUTE:
			si_init_compute(cmd_buffer);
			break;
		case RADV_QUEUE_TRANSFER:
		default:
			break;
		}
	}

	if (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT) {
		assert(pBeginInfo->pInheritanceInfo);
		cmd_buffer->state.framebuffer = radv_framebuffer_from_handle(pBeginInfo->pInheritanceInfo->framebuffer);
		cmd_buffer->state.pass = radv_render_pass_from_handle(pBeginInfo->pInheritanceInfo->renderPass);

		struct radv_subpass *subpass =
			&cmd_buffer->state.pass->subpasses[pBeginInfo->pInheritanceInfo->subpass];

		result = radv_cmd_state_setup_attachments(cmd_buffer, cmd_buffer->state.pass, NULL);
		if (result != VK_SUCCESS)
			return result;

		radv_cmd_buffer_set_subpass(cmd_buffer, subpass, false);
	}

	if (unlikely(cmd_buffer->device->trace_bo))
		radv_cmd_buffer_trace_emit(cmd_buffer);

	cmd_buffer->status = RADV_CMD_BUFFER_STATUS_RECORDING;

	return result;
}

void radv_CmdBindVertexBuffers(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    firstBinding,
	uint32_t                                    bindingCount,
	const VkBuffer*                             pBuffers,
	const VkDeviceSize*                         pOffsets)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_vertex_binding *vb = cmd_buffer->vertex_bindings;
	bool changed = false;

	/* We have to defer setting up vertex buffer since we need the buffer
	 * stride from the pipeline. */

	assert(firstBinding + bindingCount <= MAX_VBS);
	for (uint32_t i = 0; i < bindingCount; i++) {
		uint32_t idx = firstBinding + i;

		if (!changed &&
		    (vb[idx].buffer != radv_buffer_from_handle(pBuffers[i]) ||
		     vb[idx].offset != pOffsets[i])) {
			changed = true;
		}

		vb[idx].buffer = radv_buffer_from_handle(pBuffers[i]);
		vb[idx].offset = pOffsets[i];

		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs,
				   vb[idx].buffer->bo, 8);
	}

	if (!changed) {
		/* No state changes. */
		return;
	}

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_VERTEX_BUFFER;
}

void radv_CmdBindIndexBuffer(
	VkCommandBuffer                             commandBuffer,
	VkBuffer buffer,
	VkDeviceSize offset,
	VkIndexType indexType)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, index_buffer, buffer);

	if (cmd_buffer->state.index_buffer == index_buffer &&
	    cmd_buffer->state.index_offset == offset &&
	    cmd_buffer->state.index_type == indexType) {
		/* No state changes. */
		return;
	}

	cmd_buffer->state.index_buffer = index_buffer;
	cmd_buffer->state.index_offset = offset;
	cmd_buffer->state.index_type = indexType; /* vk matches hw */
	cmd_buffer->state.index_va = radv_buffer_get_va(index_buffer->bo);
	cmd_buffer->state.index_va += index_buffer->offset + offset;

	int index_size_shift = cmd_buffer->state.index_type ? 2 : 1;
	cmd_buffer->state.max_index_count = (index_buffer->size - offset) >> index_size_shift;
	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_INDEX_BUFFER;
	radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, index_buffer->bo, 8);
}


static void
radv_bind_descriptor_set(struct radv_cmd_buffer *cmd_buffer,
			 VkPipelineBindPoint bind_point,
			 struct radv_descriptor_set *set, unsigned idx)
{
	struct radeon_winsys *ws = cmd_buffer->device->ws;

	radv_set_descriptor_set(cmd_buffer, bind_point, set, idx);
	if (!set)
		return;

	assert(!(set->layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));

	if (!cmd_buffer->device->use_global_bo_list) {
		for (unsigned j = 0; j < set->layout->buffer_count; ++j)
			if (set->descriptors[j])
				radv_cs_add_buffer(ws, cmd_buffer->cs, set->descriptors[j], 7);
	}

	if(set->bo)
		radv_cs_add_buffer(ws, cmd_buffer->cs, set->bo, 8);
}

void radv_CmdBindDescriptorSets(
	VkCommandBuffer                             commandBuffer,
	VkPipelineBindPoint                         pipelineBindPoint,
	VkPipelineLayout                            _layout,
	uint32_t                                    firstSet,
	uint32_t                                    descriptorSetCount,
	const VkDescriptorSet*                      pDescriptorSets,
	uint32_t                                    dynamicOffsetCount,
	const uint32_t*                             pDynamicOffsets)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_pipeline_layout, layout, _layout);
	unsigned dyn_idx = 0;

	const bool no_dynamic_bounds = cmd_buffer->device->instance->debug_flags & RADV_DEBUG_NO_DYNAMIC_BOUNDS;

	for (unsigned i = 0; i < descriptorSetCount; ++i) {
		unsigned idx = i + firstSet;
		RADV_FROM_HANDLE(radv_descriptor_set, set, pDescriptorSets[i]);
		radv_bind_descriptor_set(cmd_buffer, pipelineBindPoint, set, idx);

		for(unsigned j = 0; j < set->layout->dynamic_offset_count; ++j, ++dyn_idx) {
			unsigned idx = j + layout->set[i + firstSet].dynamic_offset_start;
			uint32_t *dst = cmd_buffer->dynamic_buffers + idx * 4;
			assert(dyn_idx < dynamicOffsetCount);

			struct radv_descriptor_range *range = set->dynamic_descriptors + j;
			uint64_t va = range->va + pDynamicOffsets[dyn_idx];
			dst[0] = va;
			dst[1] = S_008F04_BASE_ADDRESS_HI(va >> 32);
			dst[2] = no_dynamic_bounds ? 0xffffffffu : range->size;
			dst[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
			         S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
			         S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
			         S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
			         S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
			         S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
			cmd_buffer->push_constant_stages |=
			                     set->layout->dynamic_shader_stages;
		}
	}
}

static bool radv_init_push_descriptor_set(struct radv_cmd_buffer *cmd_buffer,
                                          struct radv_descriptor_set *set,
                                          struct radv_descriptor_set_layout *layout,
					  VkPipelineBindPoint bind_point)
{
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, bind_point);
	set->size = layout->size;
	set->layout = layout;

	if (descriptors_state->push_set.capacity < set->size) {
		size_t new_size = MAX2(set->size, 1024);
		new_size = MAX2(new_size, 2 * descriptors_state->push_set.capacity);
		new_size = MIN2(new_size, 96 * MAX_PUSH_DESCRIPTORS);

		free(set->mapped_ptr);
		set->mapped_ptr = malloc(new_size);

		if (!set->mapped_ptr) {
			descriptors_state->push_set.capacity = 0;
			cmd_buffer->record_result = VK_ERROR_OUT_OF_HOST_MEMORY;
			return false;
		}

		descriptors_state->push_set.capacity = new_size;
	}

	return true;
}

void radv_meta_push_descriptor_set(
	struct radv_cmd_buffer*              cmd_buffer,
	VkPipelineBindPoint                  pipelineBindPoint,
	VkPipelineLayout                     _layout,
	uint32_t                             set,
	uint32_t                             descriptorWriteCount,
	const VkWriteDescriptorSet*          pDescriptorWrites)
{
	RADV_FROM_HANDLE(radv_pipeline_layout, layout, _layout);
	struct radv_descriptor_set *push_set = &cmd_buffer->meta_push_descriptors;
	unsigned bo_offset;

	assert(set == 0);
	assert(layout->set[set].layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);

	push_set->size = layout->set[set].layout->size;
	push_set->layout = layout->set[set].layout;

	if (!radv_cmd_buffer_upload_alloc(cmd_buffer, push_set->size, 32,
	                                  &bo_offset,
	                                  (void**) &push_set->mapped_ptr))
		return;

	push_set->va = radv_buffer_get_va(cmd_buffer->upload.upload_bo);
	push_set->va += bo_offset;

	radv_update_descriptor_sets(cmd_buffer->device, cmd_buffer,
	                            radv_descriptor_set_to_handle(push_set),
	                            descriptorWriteCount, pDescriptorWrites, 0, NULL);

	radv_set_descriptor_set(cmd_buffer, pipelineBindPoint, push_set, set);
}

void radv_CmdPushDescriptorSetKHR(
	VkCommandBuffer                             commandBuffer,
	VkPipelineBindPoint                         pipelineBindPoint,
	VkPipelineLayout                            _layout,
	uint32_t                                    set,
	uint32_t                                    descriptorWriteCount,
	const VkWriteDescriptorSet*                 pDescriptorWrites)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_pipeline_layout, layout, _layout);
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, pipelineBindPoint);
	struct radv_descriptor_set *push_set = &descriptors_state->push_set.set;

	assert(layout->set[set].layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);

	if (!radv_init_push_descriptor_set(cmd_buffer, push_set,
					   layout->set[set].layout,
					   pipelineBindPoint))
		return;

	radv_update_descriptor_sets(cmd_buffer->device, cmd_buffer,
	                            radv_descriptor_set_to_handle(push_set),
	                            descriptorWriteCount, pDescriptorWrites, 0, NULL);

	radv_set_descriptor_set(cmd_buffer, pipelineBindPoint, push_set, set);
	descriptors_state->push_dirty = true;
}

void radv_CmdPushDescriptorSetWithTemplateKHR(
	VkCommandBuffer                             commandBuffer,
	VkDescriptorUpdateTemplateKHR               descriptorUpdateTemplate,
	VkPipelineLayout                            _layout,
	uint32_t                                    set,
	const void*                                 pData)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_pipeline_layout, layout, _layout);
	RADV_FROM_HANDLE(radv_descriptor_update_template, templ, descriptorUpdateTemplate);
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, templ->bind_point);
	struct radv_descriptor_set *push_set = &descriptors_state->push_set.set;

	assert(layout->set[set].layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);

	if (!radv_init_push_descriptor_set(cmd_buffer, push_set,
					   layout->set[set].layout,
					   templ->bind_point))
		return;

	radv_update_descriptor_set_with_template(cmd_buffer->device, cmd_buffer, push_set,
						 descriptorUpdateTemplate, pData);

	radv_set_descriptor_set(cmd_buffer, templ->bind_point, push_set, set);
	descriptors_state->push_dirty = true;
}

void radv_CmdPushConstants(VkCommandBuffer commandBuffer,
			   VkPipelineLayout layout,
			   VkShaderStageFlags stageFlags,
			   uint32_t offset,
			   uint32_t size,
			   const void* pValues)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	memcpy(cmd_buffer->push_constants + offset, pValues, size);
	cmd_buffer->push_constant_stages |= stageFlags;
}

VkResult radv_EndCommandBuffer(
	VkCommandBuffer                             commandBuffer)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	if (cmd_buffer->queue_family_index != RADV_QUEUE_TRANSFER) {
		if (cmd_buffer->device->physical_device->rad_info.chip_class == SI)
			cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_PS_PARTIAL_FLUSH | RADV_CMD_FLAG_WRITEBACK_GLOBAL_L2;
		si_emit_cache_flush(cmd_buffer);
	}

	vk_free(&cmd_buffer->pool->alloc, cmd_buffer->state.attachments);

	if (!cmd_buffer->device->ws->cs_finalize(cmd_buffer->cs))
		return vk_error(VK_ERROR_OUT_OF_DEVICE_MEMORY);

	cmd_buffer->status = RADV_CMD_BUFFER_STATUS_EXECUTABLE;

	return cmd_buffer->record_result;
}

static void
radv_emit_compute_pipeline(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;

	if (!pipeline || pipeline == cmd_buffer->state.emitted_compute_pipeline)
		return;

	cmd_buffer->state.emitted_compute_pipeline = pipeline;

	radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, pipeline->cs.cdw);
	radeon_emit_array(cmd_buffer->cs, pipeline->cs.buf, pipeline->cs.cdw);

	cmd_buffer->compute_scratch_size_needed =
	                          MAX2(cmd_buffer->compute_scratch_size_needed,
	                               pipeline->max_waves * pipeline->scratch_bytes_per_wave);

	radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs,
			   pipeline->shaders[MESA_SHADER_COMPUTE]->bo, 8);

	if (unlikely(cmd_buffer->device->trace_bo))
		radv_save_pipeline(cmd_buffer, pipeline, RING_COMPUTE);
}

static void radv_mark_descriptor_sets_dirty(struct radv_cmd_buffer *cmd_buffer,
					    VkPipelineBindPoint bind_point)
{
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, bind_point);

	descriptors_state->dirty |= descriptors_state->valid;
}

void radv_CmdBindPipeline(
	VkCommandBuffer                             commandBuffer,
	VkPipelineBindPoint                         pipelineBindPoint,
	VkPipeline                                  _pipeline)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);

	switch (pipelineBindPoint) {
	case VK_PIPELINE_BIND_POINT_COMPUTE:
		if (cmd_buffer->state.compute_pipeline == pipeline)
			return;
		radv_mark_descriptor_sets_dirty(cmd_buffer, pipelineBindPoint);

		cmd_buffer->state.compute_pipeline = pipeline;
		cmd_buffer->push_constant_stages |= VK_SHADER_STAGE_COMPUTE_BIT;
		break;
	case VK_PIPELINE_BIND_POINT_GRAPHICS:
		if (cmd_buffer->state.pipeline == pipeline)
			return;
		radv_mark_descriptor_sets_dirty(cmd_buffer, pipelineBindPoint);

		cmd_buffer->state.pipeline = pipeline;
		if (!pipeline)
			break;

		cmd_buffer->state.dirty |= RADV_CMD_DIRTY_PIPELINE;
		cmd_buffer->push_constant_stages |= pipeline->active_stages;

		/* the new vertex shader might not have the same user regs */
		cmd_buffer->state.last_first_instance = -1;
		cmd_buffer->state.last_vertex_offset = -1;

		/* Prefetch all pipeline shaders at first draw time. */
		cmd_buffer->state.prefetch_L2_mask |= RADV_PREFETCH_SHADERS;

		radv_bind_dynamic_state(cmd_buffer, &pipeline->dynamic_state);

		if (pipeline->graphics.esgs_ring_size > cmd_buffer->esgs_ring_size_needed)
			cmd_buffer->esgs_ring_size_needed = pipeline->graphics.esgs_ring_size;
		if (pipeline->graphics.gsvs_ring_size > cmd_buffer->gsvs_ring_size_needed)
			cmd_buffer->gsvs_ring_size_needed = pipeline->graphics.gsvs_ring_size;

		if (radv_pipeline_has_tess(pipeline))
			cmd_buffer->tess_rings_needed = true;

		if (radv_pipeline_has_gs(pipeline)) {
			struct radv_userdata_info *loc = radv_lookup_user_sgpr(cmd_buffer->state.pipeline, MESA_SHADER_GEOMETRY,
									     AC_UD_SCRATCH_RING_OFFSETS);
			if (cmd_buffer->ring_offsets_idx == -1)
				cmd_buffer->ring_offsets_idx = loc->sgpr_idx;
			else if (loc->sgpr_idx != -1)
				assert(loc->sgpr_idx == cmd_buffer->ring_offsets_idx);
		}
		break;
	default:
		assert(!"invalid bind point");
		break;
	}
}

void radv_CmdSetViewport(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    firstViewport,
	uint32_t                                    viewportCount,
	const VkViewport*                           pViewports)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;
	MAYBE_UNUSED const uint32_t total_count = firstViewport + viewportCount;

	assert(firstViewport < MAX_VIEWPORTS);
	assert(total_count >= 1 && total_count <= MAX_VIEWPORTS);

	if (cmd_buffer->device->physical_device->has_scissor_bug) {
		/* Try to skip unnecessary PS partial flushes when the viewports
		 * don't change.
		 */
		if (!(state->dirty & (RADV_CMD_DIRTY_DYNAMIC_VIEWPORT |
				      RADV_CMD_DIRTY_DYNAMIC_SCISSOR)) &&
		    !memcmp(state->dynamic.viewport.viewports + firstViewport,
			    pViewports, viewportCount * sizeof(*pViewports))) {
			return;
		}
	}

	memcpy(state->dynamic.viewport.viewports + firstViewport, pViewports,
	       viewportCount * sizeof(*pViewports));

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_VIEWPORT;
}

void radv_CmdSetScissor(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    firstScissor,
	uint32_t                                    scissorCount,
	const VkRect2D*                             pScissors)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;
	MAYBE_UNUSED const uint32_t total_count = firstScissor + scissorCount;

	assert(firstScissor < MAX_SCISSORS);
	assert(total_count >= 1 && total_count <= MAX_SCISSORS);

	if (cmd_buffer->device->physical_device->has_scissor_bug) {
		/* Try to skip unnecessary PS partial flushes when the scissors
		 * don't change.
		 */
		if (!(state->dirty & (RADV_CMD_DIRTY_DYNAMIC_VIEWPORT |
				      RADV_CMD_DIRTY_DYNAMIC_SCISSOR)) &&
		    !memcmp(state->dynamic.scissor.scissors + firstScissor,
			    pScissors, scissorCount * sizeof(*pScissors))) {
			return;
		}
	}

	memcpy(state->dynamic.scissor.scissors + firstScissor, pScissors,
	       scissorCount * sizeof(*pScissors));

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_SCISSOR;
}

void radv_CmdSetLineWidth(
	VkCommandBuffer                             commandBuffer,
	float                                       lineWidth)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	cmd_buffer->state.dynamic.line_width = lineWidth;
	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_LINE_WIDTH;
}

void radv_CmdSetDepthBias(
	VkCommandBuffer                             commandBuffer,
	float                                       depthBiasConstantFactor,
	float                                       depthBiasClamp,
	float                                       depthBiasSlopeFactor)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	cmd_buffer->state.dynamic.depth_bias.bias = depthBiasConstantFactor;
	cmd_buffer->state.dynamic.depth_bias.clamp = depthBiasClamp;
	cmd_buffer->state.dynamic.depth_bias.slope = depthBiasSlopeFactor;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS;
}

void radv_CmdSetBlendConstants(
	VkCommandBuffer                             commandBuffer,
	const float                                 blendConstants[4])
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	memcpy(cmd_buffer->state.dynamic.blend_constants,
	       blendConstants, sizeof(float) * 4);

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS;
}

void radv_CmdSetDepthBounds(
	VkCommandBuffer                             commandBuffer,
	float                                       minDepthBounds,
	float                                       maxDepthBounds)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	cmd_buffer->state.dynamic.depth_bounds.min = minDepthBounds;
	cmd_buffer->state.dynamic.depth_bounds.max = maxDepthBounds;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS;
}

void radv_CmdSetStencilCompareMask(
	VkCommandBuffer                             commandBuffer,
	VkStencilFaceFlags                          faceMask,
	uint32_t                                    compareMask)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
		cmd_buffer->state.dynamic.stencil_compare_mask.front = compareMask;
	if (faceMask & VK_STENCIL_FACE_BACK_BIT)
		cmd_buffer->state.dynamic.stencil_compare_mask.back = compareMask;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK;
}

void radv_CmdSetStencilWriteMask(
	VkCommandBuffer                             commandBuffer,
	VkStencilFaceFlags                          faceMask,
	uint32_t                                    writeMask)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
		cmd_buffer->state.dynamic.stencil_write_mask.front = writeMask;
	if (faceMask & VK_STENCIL_FACE_BACK_BIT)
		cmd_buffer->state.dynamic.stencil_write_mask.back = writeMask;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK;
}

void radv_CmdSetStencilReference(
	VkCommandBuffer                             commandBuffer,
	VkStencilFaceFlags                          faceMask,
	uint32_t                                    reference)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
		cmd_buffer->state.dynamic.stencil_reference.front = reference;
	if (faceMask & VK_STENCIL_FACE_BACK_BIT)
		cmd_buffer->state.dynamic.stencil_reference.back = reference;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE;
}

void radv_CmdSetDiscardRectangleEXT(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    firstDiscardRectangle,
	uint32_t                                    discardRectangleCount,
	const VkRect2D*                             pDiscardRectangles)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;
	MAYBE_UNUSED const uint32_t total_count = firstDiscardRectangle + discardRectangleCount;

	assert(firstDiscardRectangle < MAX_DISCARD_RECTANGLES);
	assert(total_count >= 1 && total_count <= MAX_DISCARD_RECTANGLES);

	typed_memcpy(&state->dynamic.discard_rectangle.rectangles[firstDiscardRectangle],
	             pDiscardRectangles, discardRectangleCount);

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_DISCARD_RECTANGLE;
}

void radv_CmdExecuteCommands(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    commandBufferCount,
	const VkCommandBuffer*                      pCmdBuffers)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, primary, commandBuffer);

	assert(commandBufferCount > 0);

	/* Emit pending flushes on primary prior to executing secondary */
	si_emit_cache_flush(primary);

	for (uint32_t i = 0; i < commandBufferCount; i++) {
		RADV_FROM_HANDLE(radv_cmd_buffer, secondary, pCmdBuffers[i]);

		primary->scratch_size_needed = MAX2(primary->scratch_size_needed,
		                                    secondary->scratch_size_needed);
		primary->compute_scratch_size_needed = MAX2(primary->compute_scratch_size_needed,
		                                            secondary->compute_scratch_size_needed);

		if (secondary->esgs_ring_size_needed > primary->esgs_ring_size_needed)
			primary->esgs_ring_size_needed = secondary->esgs_ring_size_needed;
		if (secondary->gsvs_ring_size_needed > primary->gsvs_ring_size_needed)
			primary->gsvs_ring_size_needed = secondary->gsvs_ring_size_needed;
		if (secondary->tess_rings_needed)
			primary->tess_rings_needed = true;
		if (secondary->sample_positions_needed)
			primary->sample_positions_needed = true;

		if (secondary->ring_offsets_idx != -1) {
			if (primary->ring_offsets_idx == -1)
				primary->ring_offsets_idx = secondary->ring_offsets_idx;
			else
				assert(secondary->ring_offsets_idx == primary->ring_offsets_idx);
		}
		primary->device->ws->cs_execute_secondary(primary->cs, secondary->cs);


		/* When the secondary command buffer is compute only we don't
		 * need to re-emit the current graphics pipeline.
		 */
		if (secondary->state.emitted_pipeline) {
			primary->state.emitted_pipeline =
				secondary->state.emitted_pipeline;
		}

		/* When the secondary command buffer is graphics only we don't
		 * need to re-emit the current compute pipeline.
		 */
		if (secondary->state.emitted_compute_pipeline) {
			primary->state.emitted_compute_pipeline =
				secondary->state.emitted_compute_pipeline;
		}

		/* Only re-emit the draw packets when needed. */
		if (secondary->state.last_primitive_reset_en != -1) {
			primary->state.last_primitive_reset_en =
				secondary->state.last_primitive_reset_en;
		}

		if (secondary->state.last_primitive_reset_index) {
			primary->state.last_primitive_reset_index =
				secondary->state.last_primitive_reset_index;
		}

		if (secondary->state.last_ia_multi_vgt_param) {
			primary->state.last_ia_multi_vgt_param =
				secondary->state.last_ia_multi_vgt_param;
		}

		primary->state.last_first_instance = secondary->state.last_first_instance;
		primary->state.last_num_instances = secondary->state.last_num_instances;
		primary->state.last_vertex_offset = secondary->state.last_vertex_offset;

		if (secondary->state.last_index_type != -1) {
			primary->state.last_index_type =
				secondary->state.last_index_type;
		}
	}

	/* After executing commands from secondary buffers we have to dirty
	 * some states.
	 */
	primary->state.dirty |= RADV_CMD_DIRTY_PIPELINE |
				RADV_CMD_DIRTY_INDEX_BUFFER |
				RADV_CMD_DIRTY_DYNAMIC_ALL;
	radv_mark_descriptor_sets_dirty(primary, VK_PIPELINE_BIND_POINT_GRAPHICS);
	radv_mark_descriptor_sets_dirty(primary, VK_PIPELINE_BIND_POINT_COMPUTE);
}

VkResult radv_CreateCommandPool(
	VkDevice                                    _device,
	const VkCommandPoolCreateInfo*              pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkCommandPool*                              pCmdPool)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_cmd_pool *pool;

	pool = vk_alloc2(&device->alloc, pAllocator, sizeof(*pool), 8,
			   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (pool == NULL)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	if (pAllocator)
		pool->alloc = *pAllocator;
	else
		pool->alloc = device->alloc;

	list_inithead(&pool->cmd_buffers);
	list_inithead(&pool->free_cmd_buffers);

	pool->queue_family_index = pCreateInfo->queueFamilyIndex;

	*pCmdPool = radv_cmd_pool_to_handle(pool);

	return VK_SUCCESS;

}

void radv_DestroyCommandPool(
	VkDevice                                    _device,
	VkCommandPool                               commandPool,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_cmd_pool, pool, commandPool);

	if (!pool)
		return;

	list_for_each_entry_safe(struct radv_cmd_buffer, cmd_buffer,
				 &pool->cmd_buffers, pool_link) {
		radv_cmd_buffer_destroy(cmd_buffer);
	}

	list_for_each_entry_safe(struct radv_cmd_buffer, cmd_buffer,
				 &pool->free_cmd_buffers, pool_link) {
		radv_cmd_buffer_destroy(cmd_buffer);
	}

	vk_free2(&device->alloc, pAllocator, pool);
}

VkResult radv_ResetCommandPool(
	VkDevice                                    device,
	VkCommandPool                               commandPool,
	VkCommandPoolResetFlags                     flags)
{
	RADV_FROM_HANDLE(radv_cmd_pool, pool, commandPool);
	VkResult result;

	list_for_each_entry(struct radv_cmd_buffer, cmd_buffer,
			    &pool->cmd_buffers, pool_link) {
		result = radv_reset_cmd_buffer(cmd_buffer);
		if (result != VK_SUCCESS)
			return result;
	}

	return VK_SUCCESS;
}

void radv_TrimCommandPool(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolTrimFlagsKHR                   flags)
{
	RADV_FROM_HANDLE(radv_cmd_pool, pool, commandPool);

	if (!pool)
		return;

	list_for_each_entry_safe(struct radv_cmd_buffer, cmd_buffer,
				 &pool->free_cmd_buffers, pool_link) {
		radv_cmd_buffer_destroy(cmd_buffer);
	}
}

void radv_CmdBeginRenderPass(
	VkCommandBuffer                             commandBuffer,
	const VkRenderPassBeginInfo*                pRenderPassBegin,
	VkSubpassContents                           contents)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_render_pass, pass, pRenderPassBegin->renderPass);
	RADV_FROM_HANDLE(radv_framebuffer, framebuffer, pRenderPassBegin->framebuffer);

	MAYBE_UNUSED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws,
							   cmd_buffer->cs, 2048);
	MAYBE_UNUSED VkResult result;

	cmd_buffer->state.framebuffer = framebuffer;
	cmd_buffer->state.pass = pass;
	cmd_buffer->state.render_area = pRenderPassBegin->renderArea;

	result = radv_cmd_state_setup_attachments(cmd_buffer, pass, pRenderPassBegin);
	if (result != VK_SUCCESS)
		return;

	radv_cmd_buffer_set_subpass(cmd_buffer, pass->subpasses, true);
	assert(cmd_buffer->cs->cdw <= cdw_max);

	radv_cmd_buffer_clear_subpass(cmd_buffer);
}

void radv_CmdNextSubpass(
    VkCommandBuffer                             commandBuffer,
    VkSubpassContents                           contents)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	radv_cmd_buffer_resolve_subpass(cmd_buffer);

	radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs,
					      2048);

	radv_cmd_buffer_set_subpass(cmd_buffer, cmd_buffer->state.subpass + 1, true);
	radv_cmd_buffer_clear_subpass(cmd_buffer);
}

static void radv_emit_view_index(struct radv_cmd_buffer *cmd_buffer, unsigned index)
{
	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;
	for (unsigned stage = 0; stage < MESA_SHADER_STAGES; ++stage) {
		if (!pipeline->shaders[stage])
			continue;
		struct radv_userdata_info *loc = radv_lookup_user_sgpr(pipeline, stage, AC_UD_VIEW_INDEX);
		if (loc->sgpr_idx == -1)
			continue;
		uint32_t base_reg = pipeline->user_data_0[stage];
		radeon_set_sh_reg(cmd_buffer->cs, base_reg + loc->sgpr_idx * 4, index);

	}
	if (pipeline->gs_copy_shader) {
		struct radv_userdata_info *loc = &pipeline->gs_copy_shader->info.user_sgprs_locs.shader_data[AC_UD_VIEW_INDEX];
		if (loc->sgpr_idx != -1) {
			uint32_t base_reg = R_00B130_SPI_SHADER_USER_DATA_VS_0;
			radeon_set_sh_reg(cmd_buffer->cs, base_reg + loc->sgpr_idx * 4, index);
		}
	}
}

static void
radv_cs_emit_draw_packet(struct radv_cmd_buffer *cmd_buffer,
                         uint32_t vertex_count)
{
	radeon_emit(cmd_buffer->cs, PKT3(PKT3_DRAW_INDEX_AUTO, 1, cmd_buffer->state.predicating));
	radeon_emit(cmd_buffer->cs, vertex_count);
	radeon_emit(cmd_buffer->cs, V_0287F0_DI_SRC_SEL_AUTO_INDEX |
	                            S_0287F0_USE_OPAQUE(0));
}

static void
radv_cs_emit_draw_indexed_packet(struct radv_cmd_buffer *cmd_buffer,
                                 uint64_t index_va,
                                 uint32_t index_count)
{
	radeon_emit(cmd_buffer->cs, PKT3(PKT3_DRAW_INDEX_2, 4, false));
	radeon_emit(cmd_buffer->cs, cmd_buffer->state.max_index_count);
	radeon_emit(cmd_buffer->cs, index_va);
	radeon_emit(cmd_buffer->cs, index_va >> 32);
	radeon_emit(cmd_buffer->cs, index_count);
	radeon_emit(cmd_buffer->cs, V_0287F0_DI_SRC_SEL_DMA);
}

static void
radv_cs_emit_indirect_draw_packet(struct radv_cmd_buffer *cmd_buffer,
                                  bool indexed,
                                  uint32_t draw_count,
                                  uint64_t count_va,
                                  uint32_t stride)
{
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	unsigned di_src_sel = indexed ? V_0287F0_DI_SRC_SEL_DMA
	                              : V_0287F0_DI_SRC_SEL_AUTO_INDEX;
	bool draw_id_enable = radv_get_vertex_shader(cmd_buffer->state.pipeline)->info.info.vs.needs_draw_id;
	uint32_t base_reg = cmd_buffer->state.pipeline->graphics.vtx_base_sgpr;
	assert(base_reg);

	/* just reset draw state for vertex data */
	cmd_buffer->state.last_first_instance = -1;
	cmd_buffer->state.last_num_instances = -1;
	cmd_buffer->state.last_vertex_offset = -1;

	if (draw_count == 1 && !count_va && !draw_id_enable) {
		radeon_emit(cs, PKT3(indexed ? PKT3_DRAW_INDEX_INDIRECT :
				     PKT3_DRAW_INDIRECT, 3, false));
		radeon_emit(cs, 0);
		radeon_emit(cs, (base_reg - SI_SH_REG_OFFSET) >> 2);
		radeon_emit(cs, ((base_reg + 4) - SI_SH_REG_OFFSET) >> 2);
		radeon_emit(cs, di_src_sel);
	} else {
		radeon_emit(cs, PKT3(indexed ? PKT3_DRAW_INDEX_INDIRECT_MULTI :
				     PKT3_DRAW_INDIRECT_MULTI,
				     8, false));
		radeon_emit(cs, 0);
		radeon_emit(cs, (base_reg - SI_SH_REG_OFFSET) >> 2);
		radeon_emit(cs, ((base_reg + 4) - SI_SH_REG_OFFSET) >> 2);
		radeon_emit(cs, (((base_reg + 8) - SI_SH_REG_OFFSET) >> 2) |
			    S_2C3_DRAW_INDEX_ENABLE(draw_id_enable) |
			    S_2C3_COUNT_INDIRECT_ENABLE(!!count_va));
		radeon_emit(cs, draw_count); /* count */
		radeon_emit(cs, count_va); /* count_addr */
		radeon_emit(cs, count_va >> 32);
		radeon_emit(cs, stride); /* stride */
		radeon_emit(cs, di_src_sel);
	}
}

struct radv_draw_info {
	/**
	 * Number of vertices.
	 */
	uint32_t count;

	/**
	 * Index of the first vertex.
	 */
	int32_t vertex_offset;

	/**
	 * First instance id.
	 */
	uint32_t first_instance;

	/**
	 * Number of instances.
	 */
	uint32_t instance_count;

	/**
	 * First index (indexed draws only).
	 */
	uint32_t first_index;

	/**
	 * Whether it's an indexed draw.
	 */
	bool indexed;

	/**
	 * Indirect draw parameters resource.
	 */
	struct radv_buffer *indirect;
	uint64_t indirect_offset;
	uint32_t stride;

	/**
	 * Draw count parameters resource.
	 */
	struct radv_buffer *count_buffer;
	uint64_t count_buffer_offset;
};

static void
radv_emit_draw_packets(struct radv_cmd_buffer *cmd_buffer,
		       const struct radv_draw_info *info)
{
	struct radv_cmd_state *state = &cmd_buffer->state;
	struct radeon_winsys *ws = cmd_buffer->device->ws;
	struct radeon_winsys_cs *cs = cmd_buffer->cs;

	if (info->indirect) {
		uint64_t va = radv_buffer_get_va(info->indirect->bo);
		uint64_t count_va = 0;

		va += info->indirect->offset + info->indirect_offset;

		radv_cs_add_buffer(ws, cs, info->indirect->bo, 8);

		radeon_emit(cs, PKT3(PKT3_SET_BASE, 2, 0));
		radeon_emit(cs, 1);
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);

		if (info->count_buffer) {
			count_va = radv_buffer_get_va(info->count_buffer->bo);
			count_va += info->count_buffer->offset +
				    info->count_buffer_offset;

			radv_cs_add_buffer(ws, cs, info->count_buffer->bo, 8);
		}

		if (!state->subpass->view_mask) {
			radv_cs_emit_indirect_draw_packet(cmd_buffer,
							  info->indexed,
							  info->count,
							  count_va,
							  info->stride);
		} else {
			unsigned i;
			for_each_bit(i, state->subpass->view_mask) {
				radv_emit_view_index(cmd_buffer, i);

				radv_cs_emit_indirect_draw_packet(cmd_buffer,
								  info->indexed,
								  info->count,
								  count_va,
								  info->stride);
			}
		}
	} else {
		assert(state->pipeline->graphics.vtx_base_sgpr);

		if (info->vertex_offset != state->last_vertex_offset ||
		    info->first_instance != state->last_first_instance) {
			radeon_set_sh_reg_seq(cs, state->pipeline->graphics.vtx_base_sgpr,
					      state->pipeline->graphics.vtx_emit_num);

			radeon_emit(cs, info->vertex_offset);
			radeon_emit(cs, info->first_instance);
			if (state->pipeline->graphics.vtx_emit_num == 3)
				radeon_emit(cs, 0);
			state->last_first_instance = info->first_instance;
			state->last_vertex_offset = info->vertex_offset;
		}

		if (state->last_num_instances != info->instance_count) {
			radeon_emit(cs, PKT3(PKT3_NUM_INSTANCES, 0, false));
			radeon_emit(cs, info->instance_count);
			state->last_num_instances = info->instance_count;
		}

		if (info->indexed) {
			int index_size = state->index_type ? 4 : 2;
			uint64_t index_va;

			index_va = state->index_va;
			index_va += info->first_index * index_size;

			if (!state->subpass->view_mask) {
				radv_cs_emit_draw_indexed_packet(cmd_buffer,
								 index_va,
								 info->count);
			} else {
				unsigned i;
				for_each_bit(i, state->subpass->view_mask) {
					radv_emit_view_index(cmd_buffer, i);

					radv_cs_emit_draw_indexed_packet(cmd_buffer,
									 index_va,
									 info->count);
				}
			}
		} else {
			if (!state->subpass->view_mask) {
				radv_cs_emit_draw_packet(cmd_buffer, info->count);
			} else {
				unsigned i;
				for_each_bit(i, state->subpass->view_mask) {
					radv_emit_view_index(cmd_buffer, i);

					radv_cs_emit_draw_packet(cmd_buffer,
								 info->count);
				}
			}
		}
	}
}

static void
radv_emit_all_graphics_states(struct radv_cmd_buffer *cmd_buffer,
			      const struct radv_draw_info *info)
{
	if ((cmd_buffer->state.dirty & RADV_CMD_DIRTY_FRAMEBUFFER) ||
	    cmd_buffer->state.emitted_pipeline != cmd_buffer->state.pipeline)
		radv_emit_rbplus_state(cmd_buffer);

	if (cmd_buffer->state.dirty & RADV_CMD_DIRTY_PIPELINE)
		radv_emit_graphics_pipeline(cmd_buffer);

	if (cmd_buffer->state.dirty & RADV_CMD_DIRTY_FRAMEBUFFER)
		radv_emit_framebuffer_state(cmd_buffer);

	if (info->indexed) {
		if (cmd_buffer->state.dirty & RADV_CMD_DIRTY_INDEX_BUFFER)
			radv_emit_index_buffer(cmd_buffer);
	} else {
		/* On CI and later, non-indexed draws overwrite VGT_INDEX_TYPE,
		 * so the state must be re-emitted before the next indexed
		 * draw.
		 */
		if (cmd_buffer->device->physical_device->rad_info.chip_class >= CIK) {
			cmd_buffer->state.last_index_type = -1;
			cmd_buffer->state.dirty |= RADV_CMD_DIRTY_INDEX_BUFFER;
		}
	}

	radv_cmd_buffer_flush_dynamic_state(cmd_buffer);

	radv_emit_draw_registers(cmd_buffer, info->indexed,
				 info->instance_count > 1, info->indirect,
				 info->indirect ? 0 : info->count);
}

static void
radv_draw(struct radv_cmd_buffer *cmd_buffer,
	  const struct radv_draw_info *info)
{
	bool has_prefetch =
		cmd_buffer->device->physical_device->rad_info.chip_class >= CIK;
	bool pipeline_is_dirty =
		(cmd_buffer->state.dirty & RADV_CMD_DIRTY_PIPELINE) &&
		cmd_buffer->state.pipeline &&
		cmd_buffer->state.pipeline != cmd_buffer->state.emitted_pipeline;

	MAYBE_UNUSED unsigned cdw_max =
		radeon_check_space(cmd_buffer->device->ws,
				   cmd_buffer->cs, 4096);

	/* Use optimal packet order based on whether we need to sync the
	 * pipeline.
	 */
	if (cmd_buffer->state.flush_bits & (RADV_CMD_FLAG_FLUSH_AND_INV_CB |
					    RADV_CMD_FLAG_FLUSH_AND_INV_DB |
					    RADV_CMD_FLAG_PS_PARTIAL_FLUSH |
					    RADV_CMD_FLAG_CS_PARTIAL_FLUSH)) {
		/* If we have to wait for idle, set all states first, so that
		 * all SET packets are processed in parallel with previous draw
		 * calls. Then upload descriptors, set shader pointers, and
		 * draw, and prefetch at the end. This ensures that the time
		 * the CUs are idle is very short. (there are only SET_SH
		 * packets between the wait and the draw)
		 */
		radv_emit_all_graphics_states(cmd_buffer, info);
		si_emit_cache_flush(cmd_buffer);
		/* <-- CUs are idle here --> */

		radv_upload_graphics_shader_descriptors(cmd_buffer, pipeline_is_dirty);

		radv_emit_draw_packets(cmd_buffer, info);
		/* <-- CUs are busy here --> */

		/* Start prefetches after the draw has been started. Both will
		 * run in parallel, but starting the draw first is more
		 * important.
		 */
		if (has_prefetch && cmd_buffer->state.prefetch_L2_mask) {
			radv_emit_prefetch_L2(cmd_buffer,
					      cmd_buffer->state.pipeline, false);
		}
	} else {
		/* If we don't wait for idle, start prefetches first, then set
		 * states, and draw at the end.
		 */
		si_emit_cache_flush(cmd_buffer);

		if (has_prefetch && cmd_buffer->state.prefetch_L2_mask) {
			/* Only prefetch the vertex shader and VBO descriptors
			 * in order to start the draw as soon as possible.
			 */
			radv_emit_prefetch_L2(cmd_buffer,
					      cmd_buffer->state.pipeline, true);
		}

		radv_upload_graphics_shader_descriptors(cmd_buffer, pipeline_is_dirty);

		radv_emit_all_graphics_states(cmd_buffer, info);
		radv_emit_draw_packets(cmd_buffer, info);

		/* Prefetch the remaining shaders after the draw has been
		 * started.
		 */
		if (has_prefetch && cmd_buffer->state.prefetch_L2_mask) {
			radv_emit_prefetch_L2(cmd_buffer,
					      cmd_buffer->state.pipeline, false);
		}
	}

	assert(cmd_buffer->cs->cdw <= cdw_max);
	radv_cmd_buffer_after_draw(cmd_buffer, RADV_CMD_FLAG_PS_PARTIAL_FLUSH);
}

void radv_CmdDraw(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    vertexCount,
	uint32_t                                    instanceCount,
	uint32_t                                    firstVertex,
	uint32_t                                    firstInstance)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_draw_info info = {};

	info.count = vertexCount;
	info.instance_count = instanceCount;
	info.first_instance = firstInstance;
	info.vertex_offset = firstVertex;

	radv_draw(cmd_buffer, &info);
}

void radv_CmdDrawIndexed(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    indexCount,
	uint32_t                                    instanceCount,
	uint32_t                                    firstIndex,
	int32_t                                     vertexOffset,
	uint32_t                                    firstInstance)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_draw_info info = {};

	info.indexed = true;
	info.count = indexCount;
	info.instance_count = instanceCount;
	info.first_index = firstIndex;
	info.vertex_offset = vertexOffset;
	info.first_instance = firstInstance;

	radv_draw(cmd_buffer, &info);
}

void radv_CmdDrawIndirect(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    _buffer,
	VkDeviceSize                                offset,
	uint32_t                                    drawCount,
	uint32_t                                    stride)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);
	struct radv_draw_info info = {};

	info.count = drawCount;
	info.indirect = buffer;
	info.indirect_offset = offset;
	info.stride = stride;

	radv_draw(cmd_buffer, &info);
}

void radv_CmdDrawIndexedIndirect(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    _buffer,
	VkDeviceSize                                offset,
	uint32_t                                    drawCount,
	uint32_t                                    stride)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);
	struct radv_draw_info info = {};

	info.indexed = true;
	info.count = drawCount;
	info.indirect = buffer;
	info.indirect_offset = offset;
	info.stride = stride;

	radv_draw(cmd_buffer, &info);
}

void radv_CmdDrawIndirectCountAMD(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    _buffer,
	VkDeviceSize                                offset,
	VkBuffer                                    _countBuffer,
	VkDeviceSize                                countBufferOffset,
	uint32_t                                    maxDrawCount,
	uint32_t                                    stride)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);
	RADV_FROM_HANDLE(radv_buffer, count_buffer, _countBuffer);
	struct radv_draw_info info = {};

	info.count = maxDrawCount;
	info.indirect = buffer;
	info.indirect_offset = offset;
	info.count_buffer = count_buffer;
	info.count_buffer_offset = countBufferOffset;
	info.stride = stride;

	radv_draw(cmd_buffer, &info);
}

void radv_CmdDrawIndexedIndirectCountAMD(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    _buffer,
	VkDeviceSize                                offset,
	VkBuffer                                    _countBuffer,
	VkDeviceSize                                countBufferOffset,
	uint32_t                                    maxDrawCount,
	uint32_t                                    stride)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);
	RADV_FROM_HANDLE(radv_buffer, count_buffer, _countBuffer);
	struct radv_draw_info info = {};

	info.indexed = true;
	info.count = maxDrawCount;
	info.indirect = buffer;
	info.indirect_offset = offset;
	info.count_buffer = count_buffer;
	info.count_buffer_offset = countBufferOffset;
	info.stride = stride;

	radv_draw(cmd_buffer, &info);
}

struct radv_dispatch_info {
	/**
	 * Determine the layout of the grid (in block units) to be used.
	 */
	uint32_t blocks[3];

	/**
	 * A starting offset for the grid. If unaligned is set, the offset
	 * must still be aligned.
	 */
	uint32_t offsets[3];
	/**
	 * Whether it's an unaligned compute dispatch.
	 */
	bool unaligned;

	/**
	 * Indirect compute parameters resource.
	 */
	struct radv_buffer *indirect;
	uint64_t indirect_offset;
};

static void
radv_emit_dispatch_packets(struct radv_cmd_buffer *cmd_buffer,
			   const struct radv_dispatch_info *info)
{
	struct radv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;
	struct radv_shader_variant *compute_shader = pipeline->shaders[MESA_SHADER_COMPUTE];
	unsigned dispatch_initiator = cmd_buffer->device->dispatch_initiator;
	struct radeon_winsys *ws = cmd_buffer->device->ws;
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	struct radv_userdata_info *loc;

	loc = radv_lookup_user_sgpr(pipeline, MESA_SHADER_COMPUTE,
				    AC_UD_CS_GRID_SIZE);

	MAYBE_UNUSED unsigned cdw_max = radeon_check_space(ws, cs, 25);

	if (info->indirect) {
		uint64_t va = radv_buffer_get_va(info->indirect->bo);

		va += info->indirect->offset + info->indirect_offset;

		radv_cs_add_buffer(ws, cs, info->indirect->bo, 8);

		if (loc->sgpr_idx != -1) {
			for (unsigned i = 0; i < 3; ++i) {
				radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
				radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_MEM) |
						COPY_DATA_DST_SEL(COPY_DATA_REG));
				radeon_emit(cs, (va +  4 * i));
				radeon_emit(cs, (va + 4 * i) >> 32);
				radeon_emit(cs, ((R_00B900_COMPUTE_USER_DATA_0
						 + loc->sgpr_idx * 4) >> 2) + i);
				radeon_emit(cs, 0);
			}
		}

		if (radv_cmd_buffer_uses_mec(cmd_buffer)) {
			radeon_emit(cs, PKT3(PKT3_DISPATCH_INDIRECT, 2, 0) |
					PKT3_SHADER_TYPE_S(1));
			radeon_emit(cs, va);
			radeon_emit(cs, va >> 32);
			radeon_emit(cs, dispatch_initiator);
		} else {
			radeon_emit(cs, PKT3(PKT3_SET_BASE, 2, 0) |
					PKT3_SHADER_TYPE_S(1));
			radeon_emit(cs, 1);
			radeon_emit(cs, va);
			radeon_emit(cs, va >> 32);

			radeon_emit(cs, PKT3(PKT3_DISPATCH_INDIRECT, 1, 0) |
					PKT3_SHADER_TYPE_S(1));
			radeon_emit(cs, 0);
			radeon_emit(cs, dispatch_initiator);
		}
	} else {
		unsigned blocks[3] = { info->blocks[0], info->blocks[1], info->blocks[2] };
		unsigned offsets[3] = { info->offsets[0], info->offsets[1], info->offsets[2] };

		if (info->unaligned) {
			unsigned *cs_block_size = compute_shader->info.cs.block_size;
			unsigned remainder[3];

			/* If aligned, these should be an entire block size,
			 * not 0.
			 */
			remainder[0] = blocks[0] + cs_block_size[0] -
				       align_u32_npot(blocks[0], cs_block_size[0]);
			remainder[1] = blocks[1] + cs_block_size[1] -
				       align_u32_npot(blocks[1], cs_block_size[1]);
			remainder[2] = blocks[2] + cs_block_size[2] -
				       align_u32_npot(blocks[2], cs_block_size[2]);

			blocks[0] = round_up_u32(blocks[0], cs_block_size[0]);
			blocks[1] = round_up_u32(blocks[1], cs_block_size[1]);
			blocks[2] = round_up_u32(blocks[2], cs_block_size[2]);

			for(unsigned i = 0; i < 3; ++i) {
				assert(offsets[i] % cs_block_size[i] == 0);
				offsets[i] /= cs_block_size[i];
			}

			radeon_set_sh_reg_seq(cs, R_00B81C_COMPUTE_NUM_THREAD_X, 3);
			radeon_emit(cs,
				    S_00B81C_NUM_THREAD_FULL(cs_block_size[0]) |
				    S_00B81C_NUM_THREAD_PARTIAL(remainder[0]));
			radeon_emit(cs,
				    S_00B81C_NUM_THREAD_FULL(cs_block_size[1]) |
				    S_00B81C_NUM_THREAD_PARTIAL(remainder[1]));
			radeon_emit(cs,
				    S_00B81C_NUM_THREAD_FULL(cs_block_size[2]) |
				    S_00B81C_NUM_THREAD_PARTIAL(remainder[2]));

			dispatch_initiator |= S_00B800_PARTIAL_TG_EN(1);
		}

		if (loc->sgpr_idx != -1) {
			assert(!loc->indirect);
			assert(loc->num_sgprs == 3);

			radeon_set_sh_reg_seq(cs, R_00B900_COMPUTE_USER_DATA_0 +
						  loc->sgpr_idx * 4, 3);
			radeon_emit(cs, blocks[0]);
			radeon_emit(cs, blocks[1]);
			radeon_emit(cs, blocks[2]);
		}

		if (offsets[0] || offsets[1] || offsets[2]) {
			radeon_set_sh_reg_seq(cs, R_00B810_COMPUTE_START_X, 3);
			radeon_emit(cs, offsets[0]);
			radeon_emit(cs, offsets[1]);
			radeon_emit(cs, offsets[2]);

			/* The blocks in the packet are not counts but end values. */
			for (unsigned i = 0; i < 3; ++i)
				blocks[i] += offsets[i];
		} else {
			dispatch_initiator |= S_00B800_FORCE_START_AT_000(1);
		}

		radeon_emit(cs, PKT3(PKT3_DISPATCH_DIRECT, 3, 0) |
				PKT3_SHADER_TYPE_S(1));
		radeon_emit(cs, blocks[0]);
		radeon_emit(cs, blocks[1]);
		radeon_emit(cs, blocks[2]);
		radeon_emit(cs, dispatch_initiator);
	}

	assert(cmd_buffer->cs->cdw <= cdw_max);
}

static void
radv_upload_compute_shader_descriptors(struct radv_cmd_buffer *cmd_buffer)
{
	radv_flush_descriptors(cmd_buffer, VK_SHADER_STAGE_COMPUTE_BIT);
	radv_flush_constants(cmd_buffer, VK_SHADER_STAGE_COMPUTE_BIT);
}

static void
radv_dispatch(struct radv_cmd_buffer *cmd_buffer,
	      const struct radv_dispatch_info *info)
{
	struct radv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;
	bool has_prefetch =
		cmd_buffer->device->physical_device->rad_info.chip_class >= CIK;
	bool pipeline_is_dirty = pipeline &&
				 pipeline != cmd_buffer->state.emitted_compute_pipeline;

	if (cmd_buffer->state.flush_bits & (RADV_CMD_FLAG_FLUSH_AND_INV_CB |
					    RADV_CMD_FLAG_FLUSH_AND_INV_DB |
					    RADV_CMD_FLAG_PS_PARTIAL_FLUSH |
					    RADV_CMD_FLAG_CS_PARTIAL_FLUSH)) {
		/* If we have to wait for idle, set all states first, so that
		 * all SET packets are processed in parallel with previous draw
		 * calls. Then upload descriptors, set shader pointers, and
		 * dispatch, and prefetch at the end. This ensures that the
		 * time the CUs are idle is very short. (there are only SET_SH
		 * packets between the wait and the draw)
		 */
		radv_emit_compute_pipeline(cmd_buffer);
		si_emit_cache_flush(cmd_buffer);
		/* <-- CUs are idle here --> */

		radv_upload_compute_shader_descriptors(cmd_buffer);

		radv_emit_dispatch_packets(cmd_buffer, info);
		/* <-- CUs are busy here --> */

		/* Start prefetches after the dispatch has been started. Both
		 * will run in parallel, but starting the dispatch first is
		 * more important.
		 */
		if (has_prefetch && pipeline_is_dirty) {
			radv_emit_shader_prefetch(cmd_buffer,
						  pipeline->shaders[MESA_SHADER_COMPUTE]);
		}
	} else {
		/* If we don't wait for idle, start prefetches first, then set
		 * states, and dispatch at the end.
		 */
		si_emit_cache_flush(cmd_buffer);

		if (has_prefetch && pipeline_is_dirty) {
			radv_emit_shader_prefetch(cmd_buffer,
						  pipeline->shaders[MESA_SHADER_COMPUTE]);
		}

		radv_upload_compute_shader_descriptors(cmd_buffer);

		radv_emit_compute_pipeline(cmd_buffer);
		radv_emit_dispatch_packets(cmd_buffer, info);
	}

	radv_cmd_buffer_after_draw(cmd_buffer, RADV_CMD_FLAG_CS_PARTIAL_FLUSH);
}

void radv_CmdDispatchBase(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    base_x,
	uint32_t                                    base_y,
	uint32_t                                    base_z,
	uint32_t                                    x,
	uint32_t                                    y,
	uint32_t                                    z)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_dispatch_info info = {};

	info.blocks[0] = x;
	info.blocks[1] = y;
	info.blocks[2] = z;

	info.offsets[0] = base_x;
	info.offsets[1] = base_y;
	info.offsets[2] = base_z;
	radv_dispatch(cmd_buffer, &info);
}

void radv_CmdDispatch(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    x,
	uint32_t                                    y,
	uint32_t                                    z)
{
	radv_CmdDispatchBase(commandBuffer, 0, 0, 0, x, y, z);
}

void radv_CmdDispatchIndirect(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    _buffer,
	VkDeviceSize                                offset)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);
	struct radv_dispatch_info info = {};

	info.indirect = buffer;
	info.indirect_offset = offset;

	radv_dispatch(cmd_buffer, &info);
}

void radv_unaligned_dispatch(
	struct radv_cmd_buffer                      *cmd_buffer,
	uint32_t                                    x,
	uint32_t                                    y,
	uint32_t                                    z)
{
	struct radv_dispatch_info info = {};

	info.blocks[0] = x;
	info.blocks[1] = y;
	info.blocks[2] = z;
	info.unaligned = 1;

	radv_dispatch(cmd_buffer, &info);
}

void radv_CmdEndRenderPass(
	VkCommandBuffer                             commandBuffer)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	radv_subpass_barrier(cmd_buffer, &cmd_buffer->state.pass->end_barrier);

	radv_cmd_buffer_resolve_subpass(cmd_buffer);

	for (unsigned i = 0; i < cmd_buffer->state.framebuffer->attachment_count; ++i) {
		VkImageLayout layout = cmd_buffer->state.pass->attachments[i].final_layout;
		radv_handle_subpass_image_transition(cmd_buffer,
		                      (VkAttachmentReference){i, layout});
	}

	vk_free(&cmd_buffer->pool->alloc, cmd_buffer->state.attachments);

	cmd_buffer->state.pass = NULL;
	cmd_buffer->state.subpass = NULL;
	cmd_buffer->state.attachments = NULL;
	cmd_buffer->state.framebuffer = NULL;
}

/*
 * For HTILE we have the following interesting clear words:
 *   0xfffff30f: Uncompressed, full depth range, for depth+stencil HTILE
 *   0xfffc000f: Uncompressed, full depth range, for depth only HTILE.
 *   0xfffffff0: Clear depth to 1.0
 *   0x00000000: Clear depth to 0.0
 */
static void radv_initialize_htile(struct radv_cmd_buffer *cmd_buffer,
                                  struct radv_image *image,
                                  const VkImageSubresourceRange *range,
                                  uint32_t clear_word)
{
	assert(range->baseMipLevel == 0);
	assert(range->levelCount == 1 || range->levelCount == VK_REMAINING_ARRAY_LAYERS);
	unsigned layer_count = radv_get_layerCount(image, range);
	uint64_t size = image->surface.htile_slice_size * layer_count;
	uint64_t offset = image->offset + image->htile_offset +
	                  image->surface.htile_slice_size * range->baseArrayLayer;
	struct radv_cmd_state *state = &cmd_buffer->state;

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB |
			     RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;

	state->flush_bits |= radv_fill_buffer(cmd_buffer, image->bo, offset,
					      size, clear_word);

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;
}

static void radv_handle_depth_image_transition(struct radv_cmd_buffer *cmd_buffer,
					       struct radv_image *image,
					       VkImageLayout src_layout,
					       VkImageLayout dst_layout,
					       unsigned src_queue_mask,
					       unsigned dst_queue_mask,
					       const VkImageSubresourceRange *range,
					       VkImageAspectFlags pending_clears)
{
	if (!radv_image_has_htile(image))
		return;

	if (dst_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
	    (pending_clears & vk_format_aspects(image->vk_format)) == vk_format_aspects(image->vk_format) &&
	    cmd_buffer->state.render_area.offset.x == 0 && cmd_buffer->state.render_area.offset.y == 0 &&
	    cmd_buffer->state.render_area.extent.width == image->info.width &&
	    cmd_buffer->state.render_area.extent.height == image->info.height) {
		/* The clear will initialize htile. */
		return;
	} else if (src_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
	           radv_layout_has_htile(image, dst_layout, dst_queue_mask)) {
		/* TODO: merge with the clear if applicable */
		radv_initialize_htile(cmd_buffer, image, range, 0);
	} else if (!radv_layout_is_htile_compressed(image, src_layout, src_queue_mask) &&
	           radv_layout_is_htile_compressed(image, dst_layout, dst_queue_mask)) {
		uint32_t clear_value = vk_format_is_stencil(image->vk_format) ? 0xfffff30f : 0xfffc000f;
		radv_initialize_htile(cmd_buffer, image, range, clear_value);
	} else if (radv_layout_is_htile_compressed(image, src_layout, src_queue_mask) &&
	           !radv_layout_is_htile_compressed(image, dst_layout, dst_queue_mask)) {
		VkImageSubresourceRange local_range = *range;
		local_range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		local_range.baseMipLevel = 0;
		local_range.levelCount = 1;

		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB |
		                                RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;

		radv_decompress_depth_image_inplace(cmd_buffer, image, &local_range);

		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB |
		                                RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;
	}
}

static void radv_initialise_cmask(struct radv_cmd_buffer *cmd_buffer,
				  struct radv_image *image, uint32_t value)
{
	struct radv_cmd_state *state = &cmd_buffer->state;

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
			    RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;

	state->flush_bits |= radv_clear_cmask(cmd_buffer, image, value);

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
}

void radv_initialize_dcc(struct radv_cmd_buffer *cmd_buffer,
			 struct radv_image *image, uint32_t value)
{
	struct radv_cmd_state *state = &cmd_buffer->state;

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
			     RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;

	state->flush_bits |= radv_clear_dcc(cmd_buffer, image, value);

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
			     RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
}

/**
 * Initialize DCC/FMASK/CMASK metadata for a color image.
 */
static void radv_init_color_image_metadata(struct radv_cmd_buffer *cmd_buffer,
					   struct radv_image *image,
					   VkImageLayout src_layout,
					   VkImageLayout dst_layout,
					   unsigned src_queue_mask,
					   unsigned dst_queue_mask)
{
	if (radv_image_has_cmask(image)) {
		uint32_t value = 0xffffffffu; /* Fully expanded mode. */

		/*  TODO: clarify this. */
		if (radv_image_has_fmask(image)) {
			value = 0xccccccccu;
		}

		radv_initialise_cmask(cmd_buffer, image, value);
	}

	if (radv_image_has_dcc(image)) {
		uint32_t value = 0xffffffffu; /* Fully expanded mode. */

		if (radv_layout_dcc_compressed(image, dst_layout,
					       dst_queue_mask)) {
			value = 0x20202020u;
		}

		radv_initialize_dcc(cmd_buffer, image, value);
	}
}

/**
 * Handle color image transitions for DCC/FMASK/CMASK.
 */
static void radv_handle_color_image_transition(struct radv_cmd_buffer *cmd_buffer,
					       struct radv_image *image,
					       VkImageLayout src_layout,
					       VkImageLayout dst_layout,
					       unsigned src_queue_mask,
					       unsigned dst_queue_mask,
					       const VkImageSubresourceRange *range)
{
	if (src_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
		radv_init_color_image_metadata(cmd_buffer, image,
					       src_layout, dst_layout,
					       src_queue_mask, dst_queue_mask);
		return;
	}

	if (radv_image_has_dcc(image)) {
		if (src_layout == VK_IMAGE_LAYOUT_PREINITIALIZED) {
			radv_initialize_dcc(cmd_buffer, image, 0xffffffffu);
		} else if (radv_layout_dcc_compressed(image, src_layout, src_queue_mask) &&
		           !radv_layout_dcc_compressed(image, dst_layout, dst_queue_mask)) {
			radv_decompress_dcc(cmd_buffer, image, range);
		} else if (radv_layout_can_fast_clear(image, src_layout, src_queue_mask) &&
			   !radv_layout_can_fast_clear(image, dst_layout, dst_queue_mask)) {
			radv_fast_clear_flush_image_inplace(cmd_buffer, image, range);
		}
	} else if (radv_image_has_cmask(image) || radv_image_has_fmask(image)) {
		if (radv_layout_can_fast_clear(image, src_layout, src_queue_mask) &&
		    !radv_layout_can_fast_clear(image, dst_layout, dst_queue_mask)) {
			radv_fast_clear_flush_image_inplace(cmd_buffer, image, range);
		}
	}
}

static void radv_handle_image_transition(struct radv_cmd_buffer *cmd_buffer,
					 struct radv_image *image,
					 VkImageLayout src_layout,
					 VkImageLayout dst_layout,
					 uint32_t src_family,
					 uint32_t dst_family,
					 const VkImageSubresourceRange *range,
					 VkImageAspectFlags pending_clears)
{
	if (image->exclusive && src_family != dst_family) {
		/* This is an acquire or a release operation and there will be
		 * a corresponding release/acquire. Do the transition in the
		 * most flexible queue. */

		assert(src_family == cmd_buffer->queue_family_index ||
		       dst_family == cmd_buffer->queue_family_index);

		if (cmd_buffer->queue_family_index == RADV_QUEUE_TRANSFER)
			return;

		if (cmd_buffer->queue_family_index == RADV_QUEUE_COMPUTE &&
		    (src_family == RADV_QUEUE_GENERAL ||
		     dst_family == RADV_QUEUE_GENERAL))
			return;
	}

	unsigned src_queue_mask =
		radv_image_queue_family_mask(image, src_family,
					     cmd_buffer->queue_family_index);
	unsigned dst_queue_mask =
		radv_image_queue_family_mask(image, dst_family,
					     cmd_buffer->queue_family_index);

	if (vk_format_is_depth(image->vk_format)) {
		radv_handle_depth_image_transition(cmd_buffer, image,
						   src_layout, dst_layout,
						   src_queue_mask, dst_queue_mask,
						   range, pending_clears);
	} else {
		radv_handle_color_image_transition(cmd_buffer, image,
						   src_layout, dst_layout,
						   src_queue_mask, dst_queue_mask,
						   range);
	}
}

void radv_CmdPipelineBarrier(
	VkCommandBuffer                             commandBuffer,
	VkPipelineStageFlags                        srcStageMask,
	VkPipelineStageFlags                        destStageMask,
	VkBool32                                    byRegion,
	uint32_t                                    memoryBarrierCount,
	const VkMemoryBarrier*                      pMemoryBarriers,
	uint32_t                                    bufferMemoryBarrierCount,
	const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
	uint32_t                                    imageMemoryBarrierCount,
	const VkImageMemoryBarrier*                 pImageMemoryBarriers)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	enum radv_cmd_flush_bits src_flush_bits = 0;
	enum radv_cmd_flush_bits dst_flush_bits = 0;

	for (uint32_t i = 0; i < memoryBarrierCount; i++) {
		src_flush_bits |= radv_src_access_flush(cmd_buffer, pMemoryBarriers[i].srcAccessMask);
		dst_flush_bits |= radv_dst_access_flush(cmd_buffer, pMemoryBarriers[i].dstAccessMask,
		                                        NULL);
	}

	for (uint32_t i = 0; i < bufferMemoryBarrierCount; i++) {
		src_flush_bits |= radv_src_access_flush(cmd_buffer, pBufferMemoryBarriers[i].srcAccessMask);
		dst_flush_bits |= radv_dst_access_flush(cmd_buffer, pBufferMemoryBarriers[i].dstAccessMask,
		                                        NULL);
	}

	for (uint32_t i = 0; i < imageMemoryBarrierCount; i++) {
		RADV_FROM_HANDLE(radv_image, image, pImageMemoryBarriers[i].image);
		src_flush_bits |= radv_src_access_flush(cmd_buffer, pImageMemoryBarriers[i].srcAccessMask);
		dst_flush_bits |= radv_dst_access_flush(cmd_buffer, pImageMemoryBarriers[i].dstAccessMask,
		                                        image);
	}

	radv_stage_flush(cmd_buffer, srcStageMask);
	cmd_buffer->state.flush_bits |= src_flush_bits;

	for (uint32_t i = 0; i < imageMemoryBarrierCount; i++) {
		RADV_FROM_HANDLE(radv_image, image, pImageMemoryBarriers[i].image);
		radv_handle_image_transition(cmd_buffer, image,
					     pImageMemoryBarriers[i].oldLayout,
					     pImageMemoryBarriers[i].newLayout,
					     pImageMemoryBarriers[i].srcQueueFamilyIndex,
					     pImageMemoryBarriers[i].dstQueueFamilyIndex,
					     &pImageMemoryBarriers[i].subresourceRange,
					     0);
	}

	cmd_buffer->state.flush_bits |= dst_flush_bits;
}


static void write_event(struct radv_cmd_buffer *cmd_buffer,
			struct radv_event *event,
			VkPipelineStageFlags stageMask,
			unsigned value)
{
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	uint64_t va = radv_buffer_get_va(event->bo);

	radv_cs_add_buffer(cmd_buffer->device->ws, cs, event->bo, 8);

	MAYBE_UNUSED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cs, 18);

	/* TODO: this is overkill. Probably should figure something out from
	 * the stage mask. */

	si_cs_emit_write_event_eop(cs,
				   cmd_buffer->state.predicating,
				   cmd_buffer->device->physical_device->rad_info.chip_class,
				   radv_cmd_buffer_uses_mec(cmd_buffer),
				   V_028A90_BOTTOM_OF_PIPE_TS, 0,
				   1, va, 2, value);

	assert(cmd_buffer->cs->cdw <= cdw_max);
}

void radv_CmdSetEvent(VkCommandBuffer commandBuffer,
		      VkEvent _event,
		      VkPipelineStageFlags stageMask)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_event, event, _event);

	write_event(cmd_buffer, event, stageMask, 1);
}

void radv_CmdResetEvent(VkCommandBuffer commandBuffer,
			VkEvent _event,
			VkPipelineStageFlags stageMask)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_event, event, _event);

	write_event(cmd_buffer, event, stageMask, 0);
}

void radv_CmdWaitEvents(VkCommandBuffer commandBuffer,
			uint32_t eventCount,
			const VkEvent* pEvents,
			VkPipelineStageFlags srcStageMask,
			VkPipelineStageFlags dstStageMask,
			uint32_t memoryBarrierCount,
			const VkMemoryBarrier* pMemoryBarriers,
			uint32_t bufferMemoryBarrierCount,
			const VkBufferMemoryBarrier* pBufferMemoryBarriers,
			uint32_t imageMemoryBarrierCount,
			const VkImageMemoryBarrier* pImageMemoryBarriers)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radeon_winsys_cs *cs = cmd_buffer->cs;

	for (unsigned i = 0; i < eventCount; ++i) {
		RADV_FROM_HANDLE(radv_event, event, pEvents[i]);
		uint64_t va = radv_buffer_get_va(event->bo);

		radv_cs_add_buffer(cmd_buffer->device->ws, cs, event->bo, 8);

		MAYBE_UNUSED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cs, 7);

		si_emit_wait_fence(cs, false, va, 1, 0xffffffff);
		assert(cmd_buffer->cs->cdw <= cdw_max);
	}


	for (uint32_t i = 0; i < imageMemoryBarrierCount; i++) {
		RADV_FROM_HANDLE(radv_image, image, pImageMemoryBarriers[i].image);

		radv_handle_image_transition(cmd_buffer, image,
					     pImageMemoryBarriers[i].oldLayout,
					     pImageMemoryBarriers[i].newLayout,
					     pImageMemoryBarriers[i].srcQueueFamilyIndex,
					     pImageMemoryBarriers[i].dstQueueFamilyIndex,
					     &pImageMemoryBarriers[i].subresourceRange,
					     0);
	}

	/* TODO: figure out how to do memory barriers without waiting */
	cmd_buffer->state.flush_bits |= RADV_CMD_FLUSH_AND_INV_FRAMEBUFFER |
					RADV_CMD_FLAG_INV_GLOBAL_L2 |
					RADV_CMD_FLAG_INV_VMEM_L1 |
					RADV_CMD_FLAG_INV_SMEM_L1;
}


void radv_CmdSetDeviceMask(VkCommandBuffer commandBuffer,
                           uint32_t deviceMask)
{
   /* No-op */
}
