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
#include "vk_format.h"
#include "vk_util.h"
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
					 bool src_render_loop,
					 VkImageLayout dst_layout,
					 bool dst_render_loop,
					 uint32_t src_family,
					 uint32_t dst_family,
					 const VkImageSubresourceRange *range,
					 struct radv_sample_locations_state *sample_locs);

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
	.line_stipple = {
		.factor = 0u,
		.pattern = 0u,
	},
	.cull_mode = 0u,
	.front_face = 0u,
	.primitive_topology = 0u,
	.fragment_shading_rate = {
		.size = (VkExtent2D) { 1u, 1u },
		.combiner_ops = { VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR,
				  VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR
		},
	},
};

static void
radv_bind_dynamic_state(struct radv_cmd_buffer *cmd_buffer,
			const struct radv_dynamic_state *src)
{
	struct radv_dynamic_state *dest = &cmd_buffer->state.dynamic;
	uint32_t copy_mask = src->mask;
	uint32_t dest_mask = 0;

	dest->discard_rectangle.count = src->discard_rectangle.count;
	dest->sample_location.count = src->sample_location.count;

	if (copy_mask & RADV_DYNAMIC_VIEWPORT) {
		if (dest->viewport.count != src->viewport.count) {
			dest->viewport.count = src->viewport.count;
			dest_mask |= RADV_DYNAMIC_VIEWPORT;
		}

		if (memcmp(&dest->viewport.viewports, &src->viewport.viewports,
			   src->viewport.count * sizeof(VkViewport))) {
			typed_memcpy(dest->viewport.viewports,
				     src->viewport.viewports,
				     src->viewport.count);
			dest_mask |= RADV_DYNAMIC_VIEWPORT;
		}
	}

	if (copy_mask & RADV_DYNAMIC_SCISSOR) {
		if (dest->scissor.count != src->scissor.count) {
			dest->scissor.count = src->scissor.count;
			dest_mask |= RADV_DYNAMIC_SCISSOR;
		}

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

	if (copy_mask & RADV_DYNAMIC_SAMPLE_LOCATIONS) {
		if (dest->sample_location.per_pixel != src->sample_location.per_pixel ||
		    dest->sample_location.grid_size.width != src->sample_location.grid_size.width ||
		    dest->sample_location.grid_size.height != src->sample_location.grid_size.height ||
		    memcmp(&dest->sample_location.locations,
			   &src->sample_location.locations,
			   src->sample_location.count * sizeof(VkSampleLocationEXT))) {
			dest->sample_location.per_pixel = src->sample_location.per_pixel;
			dest->sample_location.grid_size = src->sample_location.grid_size;
			typed_memcpy(dest->sample_location.locations,
				     src->sample_location.locations,
				     src->sample_location.count);
			dest_mask |= RADV_DYNAMIC_SAMPLE_LOCATIONS;
		}
	}

	if (copy_mask & RADV_DYNAMIC_LINE_STIPPLE) {
		if (memcmp(&dest->line_stipple, &src->line_stipple,
			   sizeof(src->line_stipple))) {
			dest->line_stipple = src->line_stipple;
			dest_mask |= RADV_DYNAMIC_LINE_STIPPLE;
		}
	}

	if (copy_mask & RADV_DYNAMIC_CULL_MODE) {
		if (dest->cull_mode != src->cull_mode) {
			dest->cull_mode = src->cull_mode;
			dest_mask |= RADV_DYNAMIC_CULL_MODE;
		}
	}

	if (copy_mask & RADV_DYNAMIC_FRONT_FACE) {
		if (dest->front_face != src->front_face) {
			dest->front_face = src->front_face;
			dest_mask |= RADV_DYNAMIC_FRONT_FACE;
		}
	}

	if (copy_mask & RADV_DYNAMIC_PRIMITIVE_TOPOLOGY) {
		if (dest->primitive_topology != src->primitive_topology) {
			dest->primitive_topology = src->primitive_topology;
			dest_mask |= RADV_DYNAMIC_PRIMITIVE_TOPOLOGY;
		}
	}

	if (copy_mask & RADV_DYNAMIC_DEPTH_TEST_ENABLE) {
		if (dest->depth_test_enable != src->depth_test_enable) {
			dest->depth_test_enable = src->depth_test_enable;
			dest_mask |= RADV_DYNAMIC_DEPTH_TEST_ENABLE;
		}
	}

	if (copy_mask & RADV_DYNAMIC_DEPTH_WRITE_ENABLE) {
		if (dest->depth_write_enable != src->depth_write_enable) {
			dest->depth_write_enable = src->depth_write_enable;
			dest_mask |= RADV_DYNAMIC_DEPTH_WRITE_ENABLE;
		}
	}

	if (copy_mask & RADV_DYNAMIC_DEPTH_COMPARE_OP) {
		if (dest->depth_compare_op != src->depth_compare_op) {
			dest->depth_compare_op = src->depth_compare_op;
			dest_mask |= RADV_DYNAMIC_DEPTH_COMPARE_OP;
		}
	}

	if (copy_mask & RADV_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE) {
		if (dest->depth_bounds_test_enable != src->depth_bounds_test_enable) {
			dest->depth_bounds_test_enable = src->depth_bounds_test_enable;
			dest_mask |= RADV_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE;
		}
	}

	if (copy_mask & RADV_DYNAMIC_STENCIL_TEST_ENABLE) {
		if (dest->stencil_test_enable != src->stencil_test_enable) {
			dest->stencil_test_enable = src->stencil_test_enable;
			dest_mask |= RADV_DYNAMIC_STENCIL_TEST_ENABLE;
		}
	}

	if (copy_mask & RADV_DYNAMIC_STENCIL_OP) {
		if (memcmp(&dest->stencil_op, &src->stencil_op,
			   sizeof(src->stencil_op))) {
			dest->stencil_op = src->stencil_op;
			dest_mask |= RADV_DYNAMIC_STENCIL_OP;
		}
	}

	if (copy_mask & RADV_DYNAMIC_FRAGMENT_SHADING_RATE) {
		if (memcmp(&dest->fragment_shading_rate,
			   &src->fragment_shading_rate,
			   sizeof(src->fragment_shading_rate))) {
			dest->fragment_shading_rate = src->fragment_shading_rate;
			dest_mask |= RADV_DYNAMIC_FRAGMENT_SHADING_RATE;
		}
	}

	cmd_buffer->state.dirty |= dest_mask;
}

static void
radv_bind_streamout_state(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_pipeline *pipeline)
{
	struct radv_streamout_state *so = &cmd_buffer->state.streamout;
	struct radv_shader_info *info;

	if (!pipeline->streamout_shader ||
	    cmd_buffer->device->physical_device->use_ngg_streamout)
		return;

	info = &pipeline->streamout_shader->info;
	for (int i = 0; i < MAX_SO_BUFFERS; i++)
		so->stride_in_dw[i] = info->so.strides[i];

	so->enabled_stream_buffers_mask = info->so.enabled_stream_buffers_mask;
}

bool radv_cmd_buffer_uses_mec(struct radv_cmd_buffer *cmd_buffer)
{
	return cmd_buffer->queue_family_index == RADV_QUEUE_COMPUTE &&
	       cmd_buffer->device->physical_device->rad_info.chip_class >= GFX7;
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

static void
radv_destroy_cmd_buffer(struct radv_cmd_buffer *cmd_buffer)
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

	if (cmd_buffer->cs)
		cmd_buffer->device->ws->cs_destroy(cmd_buffer->cs);

	for (unsigned i = 0; i < MAX_BIND_POINTS; i++)
		free(cmd_buffer->descriptors[i].push_set.set.mapped_ptr);

	vk_object_base_finish(&cmd_buffer->base);
	vk_free(&cmd_buffer->pool->alloc, cmd_buffer);
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
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	vk_object_base_init(&device->vk, &cmd_buffer->base,
			    VK_OBJECT_TYPE_COMMAND_BUFFER);

	cmd_buffer->device = device;
	cmd_buffer->pool = pool;
	cmd_buffer->level = level;

	list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);
	cmd_buffer->queue_family_index = pool->queue_family_index;

	ring = radv_queue_family_to_ring(cmd_buffer->queue_family_index);

	cmd_buffer->cs = device->ws->cs_create(device->ws, ring);
	if (!cmd_buffer->cs) {
		radv_destroy_cmd_buffer(cmd_buffer);
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
	}

	*pCommandBuffer = radv_cmd_buffer_to_handle(cmd_buffer);

	list_inithead(&cmd_buffer->upload.list);

	return VK_SUCCESS;
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
	cmd_buffer->scratch_size_per_wave_needed = 0;
	cmd_buffer->scratch_waves_wanted = 0;
	cmd_buffer->compute_scratch_size_per_wave_needed = 0;
	cmd_buffer->compute_scratch_waves_wanted = 0;
	cmd_buffer->esgs_ring_size_needed = 0;
	cmd_buffer->gsvs_ring_size_needed = 0;
	cmd_buffer->tess_rings_needed = false;
	cmd_buffer->gds_needed = false;
	cmd_buffer->gds_oa_needed = false;
	cmd_buffer->sample_positions_needed = false;

	if (cmd_buffer->upload.upload_bo)
		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs,
				   cmd_buffer->upload.upload_bo);
	cmd_buffer->upload.offset = 0;

	cmd_buffer->record_result = VK_SUCCESS;

	memset(cmd_buffer->vertex_bindings, 0, sizeof(cmd_buffer->vertex_bindings));

	for (unsigned i = 0; i < MAX_BIND_POINTS; i++) {
		cmd_buffer->descriptors[i].dirty = 0;
		cmd_buffer->descriptors[i].valid = 0;
		cmd_buffer->descriptors[i].push_dirty = false;
	}

	if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9 &&
	    cmd_buffer->queue_family_index == RADV_QUEUE_GENERAL) {
		unsigned num_db = cmd_buffer->device->physical_device->rad_info.max_render_backends;
		unsigned fence_offset, eop_bug_offset;
		void *fence_ptr;

		radv_cmd_buffer_upload_alloc(cmd_buffer, 8, 8, &fence_offset,
					     &fence_ptr);

		cmd_buffer->gfx9_fence_va =
			radv_buffer_get_va(cmd_buffer->upload.upload_bo);
		cmd_buffer->gfx9_fence_va += fence_offset;

		if (cmd_buffer->device->physical_device->rad_info.chip_class == GFX9) {
			/* Allocate a buffer for the EOP bug on GFX9. */
			radv_cmd_buffer_upload_alloc(cmd_buffer, 16 * num_db, 8,
						     &eop_bug_offset, &fence_ptr);
			cmd_buffer->gfx9_eop_bug_va =
				radv_buffer_get_va(cmd_buffer->upload.upload_bo);
			cmd_buffer->gfx9_eop_bug_va += eop_bug_offset;
		}
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
				       RADEON_FLAG_NO_INTERPROCESS_SHARING |
				       RADEON_FLAG_32BIT |
				       RADEON_FLAG_GTT_WC,
				       RADV_BO_PRIORITY_UPLOAD_BUFFER);

	if (!bo) {
		cmd_buffer->record_result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
		return false;
	}

	radv_cs_add_buffer(device->ws, cmd_buffer->cs, bo);
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
	assert(util_is_power_of_two_nonzero(alignment));

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
radv_emit_write_data_packet(struct radv_cmd_buffer *cmd_buffer, uint64_t va,
			    unsigned count, const uint32_t *data)
{
	struct radeon_cmdbuf *cs = cmd_buffer->cs;

	radeon_check_space(cmd_buffer->device->ws, cs, 4 + count);

	radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 2 + count, 0));
	radeon_emit(cs, S_370_DST_SEL(V_370_MEM) |
		    S_370_WR_CONFIRM(1) |
		    S_370_ENGINE_SEL(V_370_ME));
	radeon_emit(cs, va);
	radeon_emit(cs, va >> 32);
	radeon_emit_array(cs, data, count);
}

void radv_cmd_buffer_trace_emit(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_device *device = cmd_buffer->device;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint64_t va;

	va = radv_buffer_get_va(device->trace_bo);
	if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
		va += 4;

	++cmd_buffer->state.trace_id;
	radv_emit_write_data_packet(cmd_buffer, va, 1,
				    &cmd_buffer->state.trace_id);

	radeon_check_space(cmd_buffer->device->ws, cs, 2);

	radeon_emit(cs, PKT3(PKT3_NOP, 0, 0));
	radeon_emit(cs, AC_ENCODE_TRACE_POINT(cmd_buffer->state.trace_id));
}

static void
radv_cmd_buffer_after_draw(struct radv_cmd_buffer *cmd_buffer,
			   enum radv_cmd_flush_bits flags)
{
	if (unlikely(cmd_buffer->device->thread_trace_bo)) {
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cmd_buffer->cs, EVENT_TYPE(V_028A90_THREAD_TRACE_MARKER) | EVENT_INDEX(0));
	}

	if (cmd_buffer->device->instance->debug_flags & RADV_DEBUG_SYNC_SHADERS) {
		enum rgp_flush_bits sqtt_flush_bits = 0;
		assert(flags & (RADV_CMD_FLAG_PS_PARTIAL_FLUSH |
				RADV_CMD_FLAG_CS_PARTIAL_FLUSH));

		radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 4);

		/* Force wait for graphics or compute engines to be idle. */
		si_cs_emit_cache_flush(cmd_buffer->cs,
				       cmd_buffer->device->physical_device->rad_info.chip_class,
				       &cmd_buffer->gfx9_fence_idx,
				       cmd_buffer->gfx9_fence_va,
				       radv_cmd_buffer_uses_mec(cmd_buffer),
				       flags, &sqtt_flush_bits, cmd_buffer->gfx9_eop_bug_va);
	}

	if (unlikely(cmd_buffer->device->trace_bo))
		radv_cmd_buffer_trace_emit(cmd_buffer);
}

static void
radv_save_pipeline(struct radv_cmd_buffer *cmd_buffer,
		   struct radv_pipeline *pipeline)
{
	struct radv_device *device = cmd_buffer->device;
	enum ring_type ring;
	uint32_t data[2];
	uint64_t va;

	va = radv_buffer_get_va(device->trace_bo);

	ring = radv_queue_family_to_ring(cmd_buffer->queue_family_index);

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

	uint64_t pipeline_address = (uintptr_t)pipeline;
	data[0] = pipeline_address;
	data[1] = pipeline_address >> 32;

	radv_emit_write_data_packet(cmd_buffer, va, 2, data);
}

static void
radv_save_vertex_descriptors(struct radv_cmd_buffer *cmd_buffer,
			     uint64_t vb_ptr)
{
	struct radv_device *device = cmd_buffer->device;
	uint32_t data[2];
	uint64_t va;

	va = radv_buffer_get_va(device->trace_bo);
	va += 24;

	data[0] = vb_ptr;
	data[1] = vb_ptr >> 32;

	radv_emit_write_data_packet(cmd_buffer, va, 2, data);
}

void radv_set_descriptor_set(struct radv_cmd_buffer *cmd_buffer,
			     VkPipelineBindPoint bind_point,
			     struct radv_descriptor_set *set,
			     unsigned idx)
{
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, bind_point);

	descriptors_state->sets[idx] = set;

	descriptors_state->valid |= (1u << idx); /* active descriptors */
	descriptors_state->dirty |= (1u << idx);
}

static void
radv_save_descriptors(struct radv_cmd_buffer *cmd_buffer,
		      VkPipelineBindPoint bind_point)
{
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, bind_point);
	struct radv_device *device = cmd_buffer->device;
	uint32_t data[MAX_SETS * 2] = {0};
	uint64_t va;
	unsigned i;
	va = radv_buffer_get_va(device->trace_bo) + 32;

	for_each_bit(i, descriptors_state->valid) {
		struct radv_descriptor_set *set = descriptors_state->sets[i];
		data[i * 2] = (uint64_t)(uintptr_t)set;
		data[i * 2 + 1] = (uint64_t)(uintptr_t)set >> 32;
	}

	radv_emit_write_data_packet(cmd_buffer, va, MAX_SETS * 2, data);
}

struct radv_userdata_info *
radv_lookup_user_sgpr(struct radv_pipeline *pipeline,
		      gl_shader_stage stage,
		      int idx)
{
	struct radv_shader_variant *shader = radv_get_shader(pipeline, stage);
	return &shader->info.user_sgprs_locs.shader_data[idx];
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

	assert(loc->num_sgprs == 1);

	radv_emit_shader_pointer(cmd_buffer->device, cmd_buffer->cs,
				 base_reg + loc->sgpr_idx * 4, va, false);
}

static void
radv_emit_descriptor_pointers(struct radv_cmd_buffer *cmd_buffer,
			      struct radv_pipeline *pipeline,
			      struct radv_descriptor_state *descriptors_state,
			      gl_shader_stage stage)
{
	struct radv_device *device = cmd_buffer->device;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint32_t sh_base = pipeline->user_data_0[stage];
	struct radv_userdata_locations *locs =
		&pipeline->shaders[stage]->info.user_sgprs_locs;
	unsigned mask = locs->descriptor_sets_enabled;

	mask &= descriptors_state->dirty & descriptors_state->valid;

	while (mask) {
		int start, count;

		u_bit_scan_consecutive_range(&mask, &start, &count);

		struct radv_userdata_info *loc = &locs->descriptor_sets[start];
		unsigned sh_offset = sh_base + loc->sgpr_idx * 4;

		radv_emit_shader_pointer_head(cs, sh_offset, count, true);
		for (int i = 0; i < count; i++) {
			struct radv_descriptor_set *set =
				descriptors_state->sets[start + i];

			radv_emit_shader_pointer_body(device, cs, set->va, true);
		}
	}
}

/**
 * Convert the user sample locations to hardware sample locations (the values
 * that will be emitted by PA_SC_AA_SAMPLE_LOCS_PIXEL_*).
 */
static void
radv_convert_user_sample_locs(struct radv_sample_locations_state *state,
			      uint32_t x, uint32_t y, VkOffset2D *sample_locs)
{
	uint32_t x_offset = x % state->grid_size.width;
	uint32_t y_offset = y % state->grid_size.height;
	uint32_t num_samples = (uint32_t)state->per_pixel;
	VkSampleLocationEXT *user_locs;
	uint32_t pixel_offset;

	pixel_offset = (x_offset + y_offset * state->grid_size.width) * num_samples;

	assert(pixel_offset <= MAX_SAMPLE_LOCATIONS);
	user_locs = &state->locations[pixel_offset];

	for (uint32_t i = 0; i < num_samples; i++) {
		float shifted_pos_x = user_locs[i].x - 0.5;
		float shifted_pos_y = user_locs[i].y - 0.5;

		int32_t scaled_pos_x = floorf(shifted_pos_x * 16);
		int32_t scaled_pos_y = floorf(shifted_pos_y * 16);

		sample_locs[i].x = CLAMP(scaled_pos_x, -8, 7);
		sample_locs[i].y = CLAMP(scaled_pos_y, -8, 7);
	}
}

/**
 * Compute the PA_SC_AA_SAMPLE_LOCS_PIXEL_* mask based on hardware sample
 * locations.
 */
static void
radv_compute_sample_locs_pixel(uint32_t num_samples, VkOffset2D *sample_locs,
			       uint32_t *sample_locs_pixel)
{
	for (uint32_t i = 0; i < num_samples; i++) {
		uint32_t sample_reg_idx = i / 4;
		uint32_t sample_loc_idx = i % 4;
		int32_t pos_x = sample_locs[i].x;
		int32_t pos_y = sample_locs[i].y;

		uint32_t shift_x = 8 * sample_loc_idx;
		uint32_t shift_y = shift_x + 4;

		sample_locs_pixel[sample_reg_idx] |= (pos_x & 0xf) << shift_x;
		sample_locs_pixel[sample_reg_idx] |= (pos_y & 0xf) << shift_y;
	}
}

/**
 * Compute the PA_SC_CENTROID_PRIORITY_* mask based on the top left hardware
 * sample locations.
 */
static uint64_t
radv_compute_centroid_priority(struct radv_cmd_buffer *cmd_buffer,
			       VkOffset2D *sample_locs,
			       uint32_t num_samples)
{
	uint32_t *centroid_priorities = alloca(num_samples * sizeof(*centroid_priorities));
	uint32_t sample_mask = num_samples - 1;
	uint32_t *distances = alloca(num_samples * sizeof(*distances));
	uint64_t centroid_priority = 0;

	/* Compute the distances from center for each sample. */
	for (int i = 0; i < num_samples; i++) {
		distances[i] = (sample_locs[i].x * sample_locs[i].x) +
			       (sample_locs[i].y * sample_locs[i].y);
	}

	/* Compute the centroid priorities by looking at the distances array. */
	for (int i = 0; i < num_samples; i++) {
		uint32_t min_idx = 0;

		for (int j = 1; j < num_samples; j++) {
			if (distances[j] < distances[min_idx])
				min_idx = j;
		}

		centroid_priorities[i] = min_idx;
		distances[min_idx] = 0xffffffff;
	}

	/* Compute the final centroid priority. */
	for (int i = 0; i < 8; i++) {
		centroid_priority |=
			centroid_priorities[i & sample_mask] << (i * 4);
	}

	return centroid_priority << 32 | centroid_priority;
}

/**
 * Emit the sample locations that are specified with VK_EXT_sample_locations.
 */
static void
radv_emit_sample_locations(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_sample_locations_state *sample_location =
		&cmd_buffer->state.dynamic.sample_location;
	uint32_t num_samples = (uint32_t)sample_location->per_pixel;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint32_t sample_locs_pixel[4][2] = {0};
	VkOffset2D sample_locs[4][8]; /* 8 is the max. sample count supported */
	uint32_t max_sample_dist = 0;
	uint64_t centroid_priority;

	if (!cmd_buffer->state.dynamic.sample_location.count)
		return;

	/* Convert the user sample locations to hardware sample locations. */
	radv_convert_user_sample_locs(sample_location, 0, 0, sample_locs[0]);
	radv_convert_user_sample_locs(sample_location, 1, 0, sample_locs[1]);
	radv_convert_user_sample_locs(sample_location, 0, 1, sample_locs[2]);
	radv_convert_user_sample_locs(sample_location, 1, 1, sample_locs[3]);

	/* Compute the PA_SC_AA_SAMPLE_LOCS_PIXEL_* mask. */
	for (uint32_t i = 0; i < 4; i++) {
		radv_compute_sample_locs_pixel(num_samples, sample_locs[i],
					       sample_locs_pixel[i]);
	}

	/* Compute the PA_SC_CENTROID_PRIORITY_* mask. */
	centroid_priority =
		radv_compute_centroid_priority(cmd_buffer, sample_locs[0],
					       num_samples);

	/* Compute the maximum sample distance from the specified locations. */
	for (unsigned i = 0; i < 4; ++i) {
		for (uint32_t j = 0; j < num_samples; j++) {
			VkOffset2D offset = sample_locs[i][j];
			max_sample_dist = MAX2(max_sample_dist,
			                       MAX2(abs(offset.x), abs(offset.y)));
		}
	}

	/* Emit the specified user sample locations. */
	switch (num_samples) {
	case 2:
	case 4:
		radeon_set_context_reg(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, sample_locs_pixel[0][0]);
		radeon_set_context_reg(cs, R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, sample_locs_pixel[1][0]);
		radeon_set_context_reg(cs, R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, sample_locs_pixel[2][0]);
		radeon_set_context_reg(cs, R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, sample_locs_pixel[3][0]);
		break;
	case 8:
		radeon_set_context_reg(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, sample_locs_pixel[0][0]);
		radeon_set_context_reg(cs, R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, sample_locs_pixel[1][0]);
		radeon_set_context_reg(cs, R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, sample_locs_pixel[2][0]);
		radeon_set_context_reg(cs, R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, sample_locs_pixel[3][0]);
		radeon_set_context_reg(cs, R_028BFC_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_1, sample_locs_pixel[0][1]);
		radeon_set_context_reg(cs, R_028C0C_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_1, sample_locs_pixel[1][1]);
		radeon_set_context_reg(cs, R_028C1C_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_1, sample_locs_pixel[2][1]);
		radeon_set_context_reg(cs, R_028C2C_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_1, sample_locs_pixel[3][1]);
		break;
	default:
		unreachable("invalid number of samples");
	}

	/* Emit the maximum sample distance and the centroid priority. */
	radeon_set_context_reg_rmw(cs, R_028BE0_PA_SC_AA_CONFIG,
				   S_028BE0_MAX_SAMPLE_DIST(max_sample_dist),
				   ~C_028BE0_MAX_SAMPLE_DIST);

	radeon_set_context_reg_seq(cs, R_028BD4_PA_SC_CENTROID_PRIORITY_0, 2);
	radeon_emit(cs, centroid_priority);
	radeon_emit(cs, centroid_priority >> 32);

	/* GFX9: Flush DFSM when the AA mode changes. */
	if (cmd_buffer->device->dfsm_allowed) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_FLUSH_DFSM) | EVENT_INDEX(0));
	}

	cmd_buffer->state.context_roll_without_scissor_emitted = true;
}

static void
radv_emit_inline_push_consts(struct radv_cmd_buffer *cmd_buffer,
			     struct radv_pipeline *pipeline,
			     gl_shader_stage stage,
			     int idx, int count, uint32_t *values)
{
	struct radv_userdata_info *loc = radv_lookup_user_sgpr(pipeline, stage, idx);
	uint32_t base_reg = pipeline->user_data_0[stage];
	if (loc->sgpr_idx == -1)
		return;

	assert(loc->num_sgprs == count);

	radeon_set_sh_reg_seq(cmd_buffer->cs, base_reg + loc->sgpr_idx * 4, count);
	radeon_emit_array(cmd_buffer->cs, values, count);
}

static void
radv_update_multisample_state(struct radv_cmd_buffer *cmd_buffer,
			      struct radv_pipeline *pipeline)
{
	int num_samples = pipeline->graphics.ms.num_samples;
	struct radv_pipeline *old_pipeline = cmd_buffer->state.emitted_pipeline;

	if (pipeline->shaders[MESA_SHADER_FRAGMENT]->info.ps.needs_sample_positions)
		cmd_buffer->sample_positions_needed = true;

	if (old_pipeline && num_samples == old_pipeline->graphics.ms.num_samples)
		return;

	radv_emit_default_sample_locations(cmd_buffer->cs, num_samples);

	cmd_buffer->state.context_roll_without_scissor_emitted = true;
}

static void
radv_update_binning_state(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_pipeline *pipeline)
{
	const struct radv_pipeline *old_pipeline = cmd_buffer->state.emitted_pipeline;


	if (pipeline->device->physical_device->rad_info.chip_class < GFX9)
		return;

	if (old_pipeline &&
	    old_pipeline->graphics.binning.pa_sc_binner_cntl_0 == pipeline->graphics.binning.pa_sc_binner_cntl_0 &&
	    old_pipeline->graphics.binning.db_dfsm_control == pipeline->graphics.binning.db_dfsm_control)
		return;

	bool binning_flush = false;
	if (cmd_buffer->device->physical_device->rad_info.family == CHIP_VEGA12 ||
	    cmd_buffer->device->physical_device->rad_info.family == CHIP_VEGA20 ||
	    cmd_buffer->device->physical_device->rad_info.family == CHIP_RAVEN2 ||
	    cmd_buffer->device->physical_device->rad_info.chip_class >= GFX10) {
		binning_flush = !old_pipeline ||
			G_028C44_BINNING_MODE(old_pipeline->graphics.binning.pa_sc_binner_cntl_0) !=
			G_028C44_BINNING_MODE(pipeline->graphics.binning.pa_sc_binner_cntl_0);
	}

	radeon_set_context_reg(cmd_buffer->cs, R_028C44_PA_SC_BINNER_CNTL_0,
			       pipeline->graphics.binning.pa_sc_binner_cntl_0 |
			       S_028C44_FLUSH_ON_BINNING_TRANSITION(!!binning_flush));

	if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX10) {
		radeon_set_context_reg(cmd_buffer->cs, R_028038_DB_DFSM_CONTROL,
				       pipeline->graphics.binning.db_dfsm_control);
	} else {
		radeon_set_context_reg(cmd_buffer->cs, R_028060_DB_DFSM_CONTROL,
				       pipeline->graphics.binning.db_dfsm_control);
	}

	cmd_buffer->state.context_roll_without_scissor_emitted = true;
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
		if (radv_pipeline_has_gs_copy_shader(pipeline))
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
	if (!cmd_buffer->device->physical_device->rad_info.rbplus_allowed)
		return;

	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;

	unsigned sx_ps_downconvert = 0;
	unsigned sx_blend_opt_epsilon = 0;
	unsigned sx_blend_opt_control = 0;

	if (!cmd_buffer->state.attachments || !subpass)
		return;

	for (unsigned i = 0; i < subpass->color_count; ++i) {
		if (subpass->color_attachments[i].attachment == VK_ATTACHMENT_UNUSED) {
			/* We don't set the DISABLE bits, because the HW can't have holes,
			 * so the SPI color format is set to 32-bit 1-component. */
			sx_ps_downconvert |= V_028754_SX_RT_EXPORT_32_R << (i * 4);
			continue;
		}

		int idx = subpass->color_attachments[i].attachment;
		struct radv_color_buffer_info *cb = &cmd_buffer->state.attachments[idx].cb;

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

		/* The HW doesn't quite blend correctly with rgb9e5 if we disable the alpha
		 * optimization, even though it has no alpha. */
		if (has_rgb && format == V_028C70_COLOR_5_9_9_9)
			has_alpha = true;

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
		case V_028C70_COLOR_5_9_9_9:
			if (spi_format == V_028714_SPI_SHADER_FP16_ABGR)
				sx_ps_downconvert |= V_028754_SX_RT_EXPORT_9_9_9_E5 << (i * 4);
			break;
		}
	}

	/* Do not set the DISABLE bits for the unused attachments, as that
	 * breaks dual source blending in SkQP and does not seem to improve
	 * performance. */

	if (sx_ps_downconvert == cmd_buffer->state.last_sx_ps_downconvert &&
	    sx_blend_opt_epsilon == cmd_buffer->state.last_sx_blend_opt_epsilon &&
	    sx_blend_opt_control == cmd_buffer->state.last_sx_blend_opt_control)
		return;

	radeon_set_context_reg_seq(cmd_buffer->cs, R_028754_SX_PS_DOWNCONVERT, 3);
	radeon_emit(cmd_buffer->cs, sx_ps_downconvert);
	radeon_emit(cmd_buffer->cs, sx_blend_opt_epsilon);
	radeon_emit(cmd_buffer->cs, sx_blend_opt_control);

	cmd_buffer->state.context_roll_without_scissor_emitted = true;

	cmd_buffer->state.last_sx_ps_downconvert = sx_ps_downconvert;
	cmd_buffer->state.last_sx_blend_opt_epsilon = sx_blend_opt_epsilon;
	cmd_buffer->state.last_sx_blend_opt_control = sx_blend_opt_control;
}

static void
radv_emit_batch_break_on_new_ps(struct radv_cmd_buffer *cmd_buffer)
{
	if (!cmd_buffer->device->pbb_allowed)
		return;

        struct radv_binning_settings settings =
                radv_get_binning_settings(cmd_buffer->device->physical_device);
	bool break_for_new_ps =
		(!cmd_buffer->state.emitted_pipeline ||
		 cmd_buffer->state.emitted_pipeline->shaders[MESA_SHADER_FRAGMENT] !=
		 cmd_buffer->state.pipeline->shaders[MESA_SHADER_FRAGMENT]) &&
		(settings.context_states_per_bin > 1 ||
		 settings.persistent_states_per_bin > 1);
	bool break_for_new_cb_target_mask =
		(!cmd_buffer->state.emitted_pipeline ||
		 cmd_buffer->state.emitted_pipeline->graphics.cb_target_mask !=
		 cmd_buffer->state.pipeline->graphics.cb_target_mask) &&
		 settings.context_states_per_bin > 1;

	if (!break_for_new_ps && !break_for_new_cb_target_mask)
		return;

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
	radeon_emit(cmd_buffer->cs, EVENT_TYPE(V_028A90_BREAK_BATCH) | EVENT_INDEX(0));
}

static void
radv_emit_graphics_pipeline(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;

	if (!pipeline || cmd_buffer->state.emitted_pipeline == pipeline)
		return;

	radv_update_multisample_state(cmd_buffer, pipeline);
	radv_update_binning_state(cmd_buffer, pipeline);

	cmd_buffer->scratch_size_per_wave_needed = MAX2(cmd_buffer->scratch_size_per_wave_needed,
	                                                pipeline->scratch_bytes_per_wave);
	cmd_buffer->scratch_waves_wanted = MAX2(cmd_buffer->scratch_waves_wanted,
	                                        pipeline->max_waves);

	if (!cmd_buffer->state.emitted_pipeline ||
	    cmd_buffer->state.emitted_pipeline->graphics.can_use_guardband !=
	     pipeline->graphics.can_use_guardband)
		cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_SCISSOR;

	if (!cmd_buffer->state.emitted_pipeline ||
	    cmd_buffer->state.emitted_pipeline->graphics.pa_su_sc_mode_cntl !=
	    pipeline->graphics.pa_su_sc_mode_cntl)
		cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_CULL_MODE |
					   RADV_CMD_DIRTY_DYNAMIC_FRONT_FACE;

	if (!cmd_buffer->state.emitted_pipeline)
		cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_PRIMITIVE_TOPOLOGY;

	if (!cmd_buffer->state.emitted_pipeline ||
	    cmd_buffer->state.emitted_pipeline->graphics.db_depth_control !=
	    pipeline->graphics.db_depth_control)
		cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_TEST_ENABLE |
					   RADV_CMD_DIRTY_DYNAMIC_DEPTH_WRITE_ENABLE |
					   RADV_CMD_DIRTY_DYNAMIC_DEPTH_COMPARE_OP |
					   RADV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE |
					   RADV_CMD_DIRTY_DYNAMIC_STENCIL_TEST_ENABLE |
					   RADV_CMD_DIRTY_DYNAMIC_STENCIL_OP;

	if (!cmd_buffer->state.emitted_pipeline)
		cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_STENCIL_OP;

	radeon_emit_array(cmd_buffer->cs, pipeline->cs.buf, pipeline->cs.cdw);

	if (!cmd_buffer->state.emitted_pipeline ||
	    cmd_buffer->state.emitted_pipeline->ctx_cs.cdw != pipeline->ctx_cs.cdw ||
	    cmd_buffer->state.emitted_pipeline->ctx_cs_hash != pipeline->ctx_cs_hash ||
	    memcmp(cmd_buffer->state.emitted_pipeline->ctx_cs.buf,
	           pipeline->ctx_cs.buf, pipeline->ctx_cs.cdw * 4)) {
		radeon_emit_array(cmd_buffer->cs, pipeline->ctx_cs.buf, pipeline->ctx_cs.cdw);
		cmd_buffer->state.context_roll_without_scissor_emitted = true;
	}

	radv_emit_batch_break_on_new_ps(cmd_buffer);

	for (unsigned i = 0; i < MESA_SHADER_COMPUTE; i++) {
		if (!pipeline->shaders[i])
			continue;

		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs,
				   pipeline->shaders[i]->bo);
	}

	if (radv_pipeline_has_gs_copy_shader(pipeline))
		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs,
				   pipeline->gs_copy_shader->bo);

	if (unlikely(cmd_buffer->device->trace_bo))
		radv_save_pipeline(cmd_buffer, pipeline);

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

	si_write_scissors(cmd_buffer->cs, 0, count,
			  cmd_buffer->state.dynamic.scissor.scissors,
			  cmd_buffer->state.dynamic.viewport.viewports,
			  cmd_buffer->state.emitted_pipeline->graphics.can_use_guardband);

	cmd_buffer->state.context_roll_without_scissor_emitted = false;
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
			       S_028A08_WIDTH(CLAMP(width, 0, 0xFFFF)));
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
radv_emit_line_stipple(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_dynamic_state *d = &cmd_buffer->state.dynamic;
	uint32_t auto_reset_cntl = 1;

	if (d->primitive_topology == V_008958_DI_PT_LINESTRIP)
		auto_reset_cntl = 2;

	radeon_set_context_reg(cmd_buffer->cs, R_028A0C_PA_SC_LINE_STIPPLE,
			       S_028A0C_LINE_PATTERN(d->line_stipple.pattern) |
			       S_028A0C_REPEAT_COUNT(d->line_stipple.factor - 1) |
			       S_028A0C_AUTO_RESET_CNTL(auto_reset_cntl));
}

static void
radv_emit_culling(struct radv_cmd_buffer *cmd_buffer, uint32_t states)
{
	unsigned pa_su_sc_mode_cntl = cmd_buffer->state.pipeline->graphics.pa_su_sc_mode_cntl;
	struct radv_dynamic_state *d = &cmd_buffer->state.dynamic;

	if (states & RADV_CMD_DIRTY_DYNAMIC_CULL_MODE) {
		pa_su_sc_mode_cntl &= C_028814_CULL_FRONT;
		pa_su_sc_mode_cntl |= S_028814_CULL_FRONT(!!(d->cull_mode & VK_CULL_MODE_FRONT_BIT));

		pa_su_sc_mode_cntl &= C_028814_CULL_BACK;
		pa_su_sc_mode_cntl |= S_028814_CULL_BACK(!!(d->cull_mode & VK_CULL_MODE_BACK_BIT));
	}

	if (states & RADV_CMD_DIRTY_DYNAMIC_FRONT_FACE) {
		pa_su_sc_mode_cntl &= C_028814_FACE;
		pa_su_sc_mode_cntl |= S_028814_FACE(d->front_face);
	}

	radeon_set_context_reg(cmd_buffer->cs, R_028814_PA_SU_SC_MODE_CNTL,
			       pa_su_sc_mode_cntl);
}

static void
radv_emit_primitive_topology(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_dynamic_state *d = &cmd_buffer->state.dynamic;

	if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX7) {
		radeon_set_uconfig_reg_idx(cmd_buffer->device->physical_device,
					   cmd_buffer->cs,
					   R_030908_VGT_PRIMITIVE_TYPE, 1,
					   d->primitive_topology);
	} else {
		radeon_set_config_reg(cmd_buffer->cs,
				      R_008958_VGT_PRIMITIVE_TYPE,
				      d->primitive_topology);
	}
}

static void
radv_emit_depth_control(struct radv_cmd_buffer *cmd_buffer, uint32_t states)
{
	unsigned db_depth_control = cmd_buffer->state.pipeline->graphics.db_depth_control;
	struct radv_dynamic_state *d = &cmd_buffer->state.dynamic;

	if (states & RADV_CMD_DIRTY_DYNAMIC_DEPTH_TEST_ENABLE) {
		db_depth_control &= C_028800_Z_ENABLE;
		db_depth_control |= S_028800_Z_ENABLE(d->depth_test_enable ? 1 : 0);
	}

	if (states & RADV_CMD_DIRTY_DYNAMIC_DEPTH_WRITE_ENABLE) {
		db_depth_control &= C_028800_Z_WRITE_ENABLE;
		db_depth_control |= S_028800_Z_WRITE_ENABLE(d->depth_write_enable ? 1 : 0);
	}

	if (states & RADV_CMD_DIRTY_DYNAMIC_DEPTH_COMPARE_OP) {
		db_depth_control &= C_028800_ZFUNC;
		db_depth_control |= S_028800_ZFUNC(d->depth_compare_op);
	}

	if (states & RADV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE) {
		db_depth_control &= C_028800_DEPTH_BOUNDS_ENABLE;
		db_depth_control |= S_028800_DEPTH_BOUNDS_ENABLE(d->depth_bounds_test_enable ? 1 : 0);
	}

	if (states & RADV_CMD_DIRTY_DYNAMIC_STENCIL_TEST_ENABLE) {
		db_depth_control &= C_028800_STENCIL_ENABLE;
		db_depth_control |= S_028800_STENCIL_ENABLE(d->stencil_test_enable ? 1 : 0);

		db_depth_control &= C_028800_BACKFACE_ENABLE;
		db_depth_control |= S_028800_BACKFACE_ENABLE(d->stencil_test_enable ? 1 : 0);
	}

	if (states & RADV_CMD_DIRTY_DYNAMIC_STENCIL_OP) {
		db_depth_control &= C_028800_STENCILFUNC;
		db_depth_control |= S_028800_STENCILFUNC(d->stencil_op.front.compare_op);

		db_depth_control &= C_028800_STENCILFUNC_BF;
		db_depth_control |= S_028800_STENCILFUNC_BF(d->stencil_op.back.compare_op);
	}

	radeon_set_context_reg(cmd_buffer->cs, R_028800_DB_DEPTH_CONTROL,
			       db_depth_control);
}

static void
radv_emit_stencil_control(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_dynamic_state *d = &cmd_buffer->state.dynamic;

	radeon_set_context_reg(cmd_buffer->cs, R_02842C_DB_STENCIL_CONTROL,
			       S_02842C_STENCILFAIL(si_translate_stencil_op(d->stencil_op.front.fail_op)) |
			       S_02842C_STENCILZPASS(si_translate_stencil_op(d->stencil_op.front.pass_op)) |
			       S_02842C_STENCILZFAIL(si_translate_stencil_op(d->stencil_op.front.depth_fail_op)) |
			       S_02842C_STENCILFAIL_BF(si_translate_stencil_op(d->stencil_op.back.fail_op)) |
			       S_02842C_STENCILZPASS_BF(si_translate_stencil_op(d->stencil_op.back.pass_op)) |
			       S_02842C_STENCILZFAIL_BF(si_translate_stencil_op(d->stencil_op.back.depth_fail_op)));
}

static void
radv_emit_fragment_shading_rate(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;
	struct radv_dynamic_state *d = &cmd_buffer->state.dynamic;
	uint32_t rate_x = MIN2(2, d->fragment_shading_rate.size.width) - 1;
	uint32_t rate_y = MIN2(2, d->fragment_shading_rate.size.height) - 1;
	uint32_t pa_cl_vrs_cntl = pipeline->graphics.vrs.pa_cl_vrs_cntl;

	/* Emit per-draw VRS rate which is the first combiner. */
	radeon_set_uconfig_reg(cmd_buffer->cs, R_03098C_GE_VRS_RATE,
			       S_03098C_RATE_X(rate_x) |
			       S_03098C_RATE_Y(rate_y));

	/* VERTEX_RATE_COMBINER_MODE controls the combiner mode between the
	 * draw rate and the vertex rate.
	 */
	pa_cl_vrs_cntl |= S_028848_VERTEX_RATE_COMBINER_MODE(d->fragment_shading_rate.combiner_ops[0]);

	radeon_set_context_reg(cmd_buffer->cs, R_028848_PA_CL_VRS_CNTL, pa_cl_vrs_cntl);
}

static void
radv_emit_fb_color_state(struct radv_cmd_buffer *cmd_buffer,
			 int index,
			 struct radv_color_buffer_info *cb,
			 struct radv_image_view *iview,
			 VkImageLayout layout,
			 bool in_render_loop,
			 bool disable_dcc)
{
	bool is_vi = cmd_buffer->device->physical_device->rad_info.chip_class >= GFX8;
	uint32_t cb_color_info = cb->cb_color_info;
	struct radv_image *image = iview->image;

	if (!radv_layout_dcc_compressed(cmd_buffer->device, image, layout, in_render_loop,
	                                radv_image_queue_family_mask(image,
	                                                             cmd_buffer->queue_family_index,
	                                                             cmd_buffer->queue_family_index)) ||
	    disable_dcc) {
		cb_color_info &= C_028C70_DCC_ENABLE;
	}

	if (!radv_layout_can_fast_clear(cmd_buffer->device, image, layout, in_render_loop,
	                                radv_image_queue_family_mask(image,
	                                                             cmd_buffer->queue_family_index,
	                                                             cmd_buffer->queue_family_index))) {
		cb_color_info &= C_028C70_COMPRESSION;
	}

	if (radv_image_is_tc_compat_cmask(image) &&
	    (radv_is_fmask_decompress_pipeline(cmd_buffer) ||
	     radv_is_dcc_decompress_pipeline(cmd_buffer))) {
		/* If this bit is set, the FMASK decompression operation
		 * doesn't occur (DCC_COMPRESS also implies FMASK_DECOMPRESS).
		 */
		cb_color_info &= C_028C70_FMASK_COMPRESS_1FRAG_ONLY;
	}

	if (radv_image_has_fmask(image) &&
	    (radv_is_fmask_decompress_pipeline(cmd_buffer) ||
	     radv_is_hw_resolve_pipeline(cmd_buffer) ||
	     radv_is_blit2d_msaa_pipeline(cmd_buffer))) {
		/* Make sure FMASK is enabled if it has been cleared because:
		 *
		 * 1) it's required for FMASK_DECOMPRESS operations to avoid
		 * GPU hangs
		 * 2) it's necessary for CB_RESOLVE which can read compressed
		 * FMASK data anyways.
		 */
		cb_color_info |= S_028C70_COMPRESSION(1);
	}

	if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX10) {
			radeon_set_context_reg_seq(cmd_buffer->cs, R_028C60_CB_COLOR0_BASE + index * 0x3c, 11);
			radeon_emit(cmd_buffer->cs, cb->cb_color_base);
			radeon_emit(cmd_buffer->cs, 0);
			radeon_emit(cmd_buffer->cs, 0);
			radeon_emit(cmd_buffer->cs, cb->cb_color_view);
			radeon_emit(cmd_buffer->cs, cb_color_info);
			radeon_emit(cmd_buffer->cs, cb->cb_color_attrib);
			radeon_emit(cmd_buffer->cs, cb->cb_dcc_control);
			radeon_emit(cmd_buffer->cs, cb->cb_color_cmask);
			radeon_emit(cmd_buffer->cs, 0);
			radeon_emit(cmd_buffer->cs, cb->cb_color_fmask);
			radeon_emit(cmd_buffer->cs, 0);

			radeon_set_context_reg_seq(cmd_buffer->cs, R_028C94_CB_COLOR0_DCC_BASE + index * 0x3c, 1);
			radeon_emit(cmd_buffer->cs, cb->cb_dcc_base);

			radeon_set_context_reg(cmd_buffer->cs, R_028E40_CB_COLOR0_BASE_EXT + index * 4,
					       cb->cb_color_base >> 32);
			radeon_set_context_reg(cmd_buffer->cs, R_028E60_CB_COLOR0_CMASK_BASE_EXT + index * 4,
					       cb->cb_color_cmask >> 32);
			radeon_set_context_reg(cmd_buffer->cs, R_028E80_CB_COLOR0_FMASK_BASE_EXT + index * 4,
					       cb->cb_color_fmask >> 32);
			radeon_set_context_reg(cmd_buffer->cs, R_028EA0_CB_COLOR0_DCC_BASE_EXT + index * 4,
					       cb->cb_dcc_base >> 32);
			radeon_set_context_reg(cmd_buffer->cs, R_028EC0_CB_COLOR0_ATTRIB2 + index * 4,
					       cb->cb_color_attrib2);
			radeon_set_context_reg(cmd_buffer->cs, R_028EE0_CB_COLOR0_ATTRIB3 + index * 4,
					       cb->cb_color_attrib3);
	} else if (cmd_buffer->device->physical_device->rad_info.chip_class == GFX9) {
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
				       cb->cb_mrt_epitch);
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

	if (radv_dcc_enabled(image, iview->base_mip)) {
		/* Drawing with DCC enabled also compresses colorbuffers. */
		VkImageSubresourceRange range = {
			.aspectMask = iview->aspect_mask,
			.baseMipLevel = iview->base_mip,
			.levelCount = iview->level_count,
			.baseArrayLayer = iview->base_layer,
			.layerCount = iview->layer_count,
		};

		radv_update_dcc_metadata(cmd_buffer, image, &range, true);
	}
}

static void
radv_update_zrange_precision(struct radv_cmd_buffer *cmd_buffer,
			     struct radv_ds_buffer_info *ds,
			     const struct radv_image_view *iview,
			     VkImageLayout layout,
			     bool in_render_loop, bool requires_cond_exec)
{
	const struct radv_image *image = iview->image;
	uint32_t db_z_info = ds->db_z_info;
	uint32_t db_z_info_reg;

	if (!cmd_buffer->device->physical_device->rad_info.has_tc_compat_zrange_bug ||
	    !radv_image_is_tc_compat_htile(image))
		return;

	if (!radv_layout_is_htile_compressed(image, layout, in_render_loop,
					     radv_image_queue_family_mask(image,
									  cmd_buffer->queue_family_index,
									  cmd_buffer->queue_family_index))) {
		db_z_info &= C_028040_TILE_SURFACE_ENABLE;
	}

	db_z_info &= C_028040_ZRANGE_PRECISION;

	if (cmd_buffer->device->physical_device->rad_info.chip_class == GFX9) {
		db_z_info_reg = R_028038_DB_Z_INFO;
	} else {
		db_z_info_reg = R_028040_DB_Z_INFO;
	}

	/* When we don't know the last fast clear value we need to emit a
	 * conditional packet that will eventually skip the following
	 * SET_CONTEXT_REG packet.
	 */
	if (requires_cond_exec) {
		uint64_t va = radv_get_tc_compat_zrange_va(image, iview->base_mip);

		radeon_emit(cmd_buffer->cs, PKT3(PKT3_COND_EXEC, 3, 0));
		radeon_emit(cmd_buffer->cs, va);
		radeon_emit(cmd_buffer->cs, va >> 32);
		radeon_emit(cmd_buffer->cs, 0);
		radeon_emit(cmd_buffer->cs, 3); /* SET_CONTEXT_REG size */
	}

	radeon_set_context_reg(cmd_buffer->cs, db_z_info_reg, db_z_info);
}

static void
radv_emit_fb_ds_state(struct radv_cmd_buffer *cmd_buffer,
		      struct radv_ds_buffer_info *ds,
		      struct radv_image_view *iview,
		      VkImageLayout layout,
		      bool in_render_loop)
{
	const struct radv_image *image = iview->image;
	uint32_t db_z_info = ds->db_z_info;
	uint32_t db_stencil_info = ds->db_stencil_info;

	if (!radv_layout_is_htile_compressed(image, layout, in_render_loop,
					     radv_image_queue_family_mask(image,
									  cmd_buffer->queue_family_index,
									  cmd_buffer->queue_family_index))) {
		db_z_info &= C_028040_TILE_SURFACE_ENABLE;
		db_stencil_info |= S_028044_TILE_STENCIL_DISABLE(1);
	}

	radeon_set_context_reg(cmd_buffer->cs, R_028008_DB_DEPTH_VIEW, ds->db_depth_view);
	radeon_set_context_reg(cmd_buffer->cs, R_028ABC_DB_HTILE_SURFACE, ds->db_htile_surface);

	if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX10) {
		radeon_set_context_reg(cmd_buffer->cs, R_028014_DB_HTILE_DATA_BASE, ds->db_htile_data_base);
		radeon_set_context_reg(cmd_buffer->cs, R_02801C_DB_DEPTH_SIZE_XY, ds->db_depth_size);

		radeon_set_context_reg_seq(cmd_buffer->cs, R_02803C_DB_DEPTH_INFO, 7);
		radeon_emit(cmd_buffer->cs, S_02803C_RESOURCE_LEVEL(1));
		radeon_emit(cmd_buffer->cs, db_z_info);
		radeon_emit(cmd_buffer->cs, db_stencil_info);
		radeon_emit(cmd_buffer->cs, ds->db_z_read_base);
		radeon_emit(cmd_buffer->cs, ds->db_stencil_read_base);
		radeon_emit(cmd_buffer->cs, ds->db_z_read_base);
		radeon_emit(cmd_buffer->cs, ds->db_stencil_read_base);

		radeon_set_context_reg_seq(cmd_buffer->cs, R_028068_DB_Z_READ_BASE_HI, 5);
		radeon_emit(cmd_buffer->cs, ds->db_z_read_base >> 32);
		radeon_emit(cmd_buffer->cs, ds->db_stencil_read_base >> 32);
		radeon_emit(cmd_buffer->cs, ds->db_z_read_base >> 32);
		radeon_emit(cmd_buffer->cs, ds->db_stencil_read_base >> 32);
		radeon_emit(cmd_buffer->cs, ds->db_htile_data_base >> 32);
	} else if (cmd_buffer->device->physical_device->rad_info.chip_class == GFX9) {
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

	/* Update the ZRANGE_PRECISION value for the TC-compat bug. */
	radv_update_zrange_precision(cmd_buffer, ds, iview, layout,
				     in_render_loop, true);

	radeon_set_context_reg(cmd_buffer->cs, R_028B78_PA_SU_POLY_OFFSET_DB_FMT_CNTL,
			       ds->pa_su_poly_offset_db_fmt_cntl);
}

/**
 * Update the fast clear depth/stencil values if the image is bound as a
 * depth/stencil buffer.
 */
static void
radv_update_bound_fast_clear_ds(struct radv_cmd_buffer *cmd_buffer,
				const struct radv_image_view *iview,
				VkClearDepthStencilValue ds_clear_value,
				VkImageAspectFlags aspects)
{
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	const struct radv_image *image = iview->image;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint32_t att_idx;

	if (!cmd_buffer->state.attachments || !subpass)
		return;

	if (!subpass->depth_stencil_attachment)
		return;

	att_idx = subpass->depth_stencil_attachment->attachment;
	if (cmd_buffer->state.attachments[att_idx].iview->image != image)
		return;

	if (aspects == (VK_IMAGE_ASPECT_DEPTH_BIT |
			VK_IMAGE_ASPECT_STENCIL_BIT)) {
		radeon_set_context_reg_seq(cs, R_028028_DB_STENCIL_CLEAR, 2);
		radeon_emit(cs, ds_clear_value.stencil);
		radeon_emit(cs, fui(ds_clear_value.depth));
	} else if (aspects == VK_IMAGE_ASPECT_DEPTH_BIT) {
		radeon_set_context_reg_seq(cs, R_02802C_DB_DEPTH_CLEAR, 1);
		radeon_emit(cs, fui(ds_clear_value.depth));
	} else {
		assert(aspects == VK_IMAGE_ASPECT_STENCIL_BIT);
		radeon_set_context_reg_seq(cs, R_028028_DB_STENCIL_CLEAR, 1);
		radeon_emit(cs, ds_clear_value.stencil);
	}

	/* Update the ZRANGE_PRECISION value for the TC-compat bug. This is
	 * only needed when clearing Z to 0.0.
	 */
	if ((aspects & VK_IMAGE_ASPECT_DEPTH_BIT) &&
	    ds_clear_value.depth == 0.0) {
		VkImageLayout layout = subpass->depth_stencil_attachment->layout;
		bool in_render_loop = subpass->depth_stencil_attachment->in_render_loop;

		radv_update_zrange_precision(cmd_buffer, &cmd_buffer->state.attachments[att_idx].ds,
					     iview, layout, in_render_loop, false);
	}

	cmd_buffer->state.context_roll_without_scissor_emitted = true;
}

/**
 * Set the clear depth/stencil values to the image's metadata.
 */
static void
radv_set_ds_clear_metadata(struct radv_cmd_buffer *cmd_buffer,
			   struct radv_image *image,
			   const VkImageSubresourceRange *range,
			   VkClearDepthStencilValue ds_clear_value,
			   VkImageAspectFlags aspects)
{
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint32_t level_count = radv_get_levelCount(image, range);

	if (aspects == (VK_IMAGE_ASPECT_DEPTH_BIT |
		        VK_IMAGE_ASPECT_STENCIL_BIT)) {
		uint64_t va = radv_get_ds_clear_value_va(image, range->baseMipLevel);

		/* Use the fastest way when both aspects are used. */
		radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 2 + 2 * level_count, cmd_buffer->state.predicating));
		radeon_emit(cs, S_370_DST_SEL(V_370_MEM) |
				S_370_WR_CONFIRM(1) |
				S_370_ENGINE_SEL(V_370_PFP));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);

		for (uint32_t l = 0; l < level_count; l++) {
			radeon_emit(cs, ds_clear_value.stencil);
			radeon_emit(cs, fui(ds_clear_value.depth));
		}
	} else {
		/* Otherwise we need one WRITE_DATA packet per level. */
		for (uint32_t l = 0; l < level_count; l++) {
			uint64_t va = radv_get_ds_clear_value_va(image, range->baseMipLevel + l);
			unsigned value;

			if (aspects == VK_IMAGE_ASPECT_DEPTH_BIT) {
				value = fui(ds_clear_value.depth);
				va += 4;
			} else {
				assert(aspects == VK_IMAGE_ASPECT_STENCIL_BIT);
				value = ds_clear_value.stencil;
			}

			radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 3, cmd_buffer->state.predicating));
			radeon_emit(cs, S_370_DST_SEL(V_370_MEM) |
					S_370_WR_CONFIRM(1) |
					S_370_ENGINE_SEL(V_370_PFP));
			radeon_emit(cs, va);
			radeon_emit(cs, va >> 32);
			radeon_emit(cs, value);
		}
	}
}

/**
 * Update the TC-compat metadata value for this image.
 */
static void
radv_set_tc_compat_zrange_metadata(struct radv_cmd_buffer *cmd_buffer,
				   struct radv_image *image,
				   const VkImageSubresourceRange *range,
				   uint32_t value)
{
	struct radeon_cmdbuf *cs = cmd_buffer->cs;

	if (!cmd_buffer->device->physical_device->rad_info.has_tc_compat_zrange_bug)
		return;

	uint64_t va = radv_get_tc_compat_zrange_va(image, range->baseMipLevel);
	uint32_t level_count = radv_get_levelCount(image, range);

	radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 2 + level_count, cmd_buffer->state.predicating));
	radeon_emit(cs, S_370_DST_SEL(V_370_MEM) |
			S_370_WR_CONFIRM(1) |
			S_370_ENGINE_SEL(V_370_PFP));
	radeon_emit(cs, va);
	radeon_emit(cs, va >> 32);

	for (uint32_t l = 0; l < level_count; l++)
		radeon_emit(cs, value);
}

static void
radv_update_tc_compat_zrange_metadata(struct radv_cmd_buffer *cmd_buffer,
				      const struct radv_image_view *iview,
				      VkClearDepthStencilValue ds_clear_value)
{
	VkImageSubresourceRange range = {
		.aspectMask = iview->aspect_mask,
		.baseMipLevel = iview->base_mip,
		.levelCount = iview->level_count,
		.baseArrayLayer = iview->base_layer,
		.layerCount = iview->layer_count,
	};
	uint32_t cond_val;

	/* Conditionally set DB_Z_INFO.ZRANGE_PRECISION to 0 when the last
	 * depth clear value is 0.0f.
	 */
	cond_val = ds_clear_value.depth == 0.0f ? UINT_MAX : 0;

	radv_set_tc_compat_zrange_metadata(cmd_buffer, iview->image, &range,
					   cond_val);
}

/**
 * Update the clear depth/stencil values for this image.
 */
void
radv_update_ds_clear_metadata(struct radv_cmd_buffer *cmd_buffer,
			      const struct radv_image_view *iview,
			      VkClearDepthStencilValue ds_clear_value,
			      VkImageAspectFlags aspects)
{
	VkImageSubresourceRange range = {
		.aspectMask = iview->aspect_mask,
		.baseMipLevel = iview->base_mip,
		.levelCount = iview->level_count,
		.baseArrayLayer = iview->base_layer,
		.layerCount = iview->layer_count,
	};
	struct radv_image *image = iview->image;

	assert(radv_image_has_htile(image));

	radv_set_ds_clear_metadata(cmd_buffer, iview->image, &range,
				   ds_clear_value, aspects);

	if (radv_image_is_tc_compat_htile(image) &&
	    (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)) {
		radv_update_tc_compat_zrange_metadata(cmd_buffer, iview,
						      ds_clear_value);
	}

	radv_update_bound_fast_clear_ds(cmd_buffer, iview, ds_clear_value,
					aspects);
}

/**
 * Load the clear depth/stencil values from the image's metadata.
 */
static void
radv_load_ds_clear_metadata(struct radv_cmd_buffer *cmd_buffer,
			    const struct radv_image_view *iview)
{
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	const struct radv_image *image = iview->image;
	VkImageAspectFlags aspects = vk_format_aspects(image->vk_format);
	uint64_t va = radv_get_ds_clear_value_va(image, iview->base_mip);
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

	uint32_t reg = R_028028_DB_STENCIL_CLEAR + 4 * reg_offset;

	if (cmd_buffer->device->physical_device->rad_info.has_load_ctx_reg_pkt) {
		radeon_emit(cs, PKT3(PKT3_LOAD_CONTEXT_REG_INDEX, 3, 0));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		radeon_emit(cs, (reg - SI_CONTEXT_REG_OFFSET) >> 2);
		radeon_emit(cs, reg_count);
	} else {
		radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
		radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_SRC_MEM) |
				COPY_DATA_DST_SEL(COPY_DATA_REG) |
				(reg_count == 2 ? COPY_DATA_COUNT_SEL : 0));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		radeon_emit(cs, reg >> 2);
		radeon_emit(cs, 0);

		radeon_emit(cs, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
		radeon_emit(cs, 0);
	}
}

/*
 * With DCC some colors don't require CMASK elimination before being
 * used as a texture. This sets a predicate value to determine if the
 * cmask eliminate is required.
 */
void
radv_update_fce_metadata(struct radv_cmd_buffer *cmd_buffer,
			 struct radv_image *image,
			 const VkImageSubresourceRange *range, bool value)
{
	uint64_t pred_val = value;
	uint64_t va = radv_image_get_fce_pred_va(image, range->baseMipLevel);
	uint32_t level_count = radv_get_levelCount(image, range);
	uint32_t count = 2 * level_count;

	assert(radv_dcc_enabled(image, range->baseMipLevel));

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_WRITE_DATA, 2 + count, 0));
	radeon_emit(cmd_buffer->cs, S_370_DST_SEL(V_370_MEM) |
				    S_370_WR_CONFIRM(1) |
				    S_370_ENGINE_SEL(V_370_PFP));
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);

	for (uint32_t l = 0; l < level_count; l++) {
		radeon_emit(cmd_buffer->cs, pred_val);
		radeon_emit(cmd_buffer->cs, pred_val >> 32);
	}
}

/**
 * Update the DCC predicate to reflect the compression state.
 */
void
radv_update_dcc_metadata(struct radv_cmd_buffer *cmd_buffer,
			 struct radv_image *image,
			 const VkImageSubresourceRange *range, bool value)
{
	uint64_t pred_val = value;
	uint64_t va = radv_image_get_dcc_pred_va(image, range->baseMipLevel);
	uint32_t level_count = radv_get_levelCount(image, range);
	uint32_t count = 2 * level_count;

	assert(radv_dcc_enabled(image, range->baseMipLevel));

	radeon_emit(cmd_buffer->cs, PKT3(PKT3_WRITE_DATA, 2 + count, 0));
	radeon_emit(cmd_buffer->cs, S_370_DST_SEL(V_370_MEM) |
				    S_370_WR_CONFIRM(1) |
				    S_370_ENGINE_SEL(V_370_PFP));
	radeon_emit(cmd_buffer->cs, va);
	radeon_emit(cmd_buffer->cs, va >> 32);

	for (uint32_t l = 0; l < level_count; l++) {
		radeon_emit(cmd_buffer->cs, pred_val);
		radeon_emit(cmd_buffer->cs, pred_val >> 32);
	}
}

/**
 * Update the fast clear color values if the image is bound as a color buffer.
 */
static void
radv_update_bound_fast_clear_color(struct radv_cmd_buffer *cmd_buffer,
				   struct radv_image *image,
				   int cb_idx,
				   uint32_t color_values[2])
{
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint32_t att_idx;

	if (!cmd_buffer->state.attachments || !subpass)
		return;

	att_idx = subpass->color_attachments[cb_idx].attachment;
	if (att_idx == VK_ATTACHMENT_UNUSED)
		return;

	if (cmd_buffer->state.attachments[att_idx].iview->image != image)
		return;

	radeon_set_context_reg_seq(cs, R_028C8C_CB_COLOR0_CLEAR_WORD0 + cb_idx * 0x3c, 2);
	radeon_emit(cs, color_values[0]);
	radeon_emit(cs, color_values[1]);

	cmd_buffer->state.context_roll_without_scissor_emitted = true;
}

/**
 * Set the clear color values to the image's metadata.
 */
static void
radv_set_color_clear_metadata(struct radv_cmd_buffer *cmd_buffer,
			      struct radv_image *image,
			      const VkImageSubresourceRange *range,
			      uint32_t color_values[2])
{
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint64_t va = radv_image_get_fast_clear_va(image, range->baseMipLevel);
	uint32_t level_count = radv_get_levelCount(image, range);
	uint32_t count = 2 * level_count;

	assert(radv_image_has_cmask(image) ||
	       radv_dcc_enabled(image, range->baseMipLevel));

	radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 2 + count, cmd_buffer->state.predicating));
	radeon_emit(cs, S_370_DST_SEL(V_370_MEM) |
			S_370_WR_CONFIRM(1) |
			S_370_ENGINE_SEL(V_370_PFP));
	radeon_emit(cs, va);
	radeon_emit(cs, va >> 32);

	for (uint32_t l = 0; l < level_count; l++) {
		radeon_emit(cs, color_values[0]);
		radeon_emit(cs, color_values[1]);
	}
}

/**
 * Update the clear color values for this image.
 */
void
radv_update_color_clear_metadata(struct radv_cmd_buffer *cmd_buffer,
				 const struct radv_image_view *iview,
				 int cb_idx,
				 uint32_t color_values[2])
{
	struct radv_image *image = iview->image;
	VkImageSubresourceRange range = {
		.aspectMask = iview->aspect_mask,
		.baseMipLevel = iview->base_mip,
		.levelCount = iview->level_count,
		.baseArrayLayer = iview->base_layer,
		.layerCount = iview->layer_count,
	};

	assert(radv_image_has_cmask(image) ||
	       radv_dcc_enabled(image, iview->base_mip));

	radv_set_color_clear_metadata(cmd_buffer, image, &range, color_values);

	radv_update_bound_fast_clear_color(cmd_buffer, image, cb_idx,
					   color_values);
}

/**
 * Load the clear color values from the image's metadata.
 */
static void
radv_load_color_clear_metadata(struct radv_cmd_buffer *cmd_buffer,
			       struct radv_image_view *iview,
			       int cb_idx)
{
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	struct radv_image *image = iview->image;
	uint64_t va = radv_image_get_fast_clear_va(image, iview->base_mip);

	if (!radv_image_has_cmask(image) &&
	    !radv_dcc_enabled(image, iview->base_mip))
		return;

	uint32_t reg = R_028C8C_CB_COLOR0_CLEAR_WORD0 + cb_idx * 0x3c;

	if (cmd_buffer->device->physical_device->rad_info.has_load_ctx_reg_pkt) {
		radeon_emit(cs, PKT3(PKT3_LOAD_CONTEXT_REG_INDEX, 3, cmd_buffer->state.predicating));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		radeon_emit(cs, (reg - SI_CONTEXT_REG_OFFSET) >> 2);
		radeon_emit(cs, 2);
	} else {
		radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, cmd_buffer->state.predicating));
		radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_SRC_MEM) |
				COPY_DATA_DST_SEL(COPY_DATA_REG) |
				COPY_DATA_COUNT_SEL);
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		radeon_emit(cs, reg >> 2);
		radeon_emit(cs, 0);

		radeon_emit(cs, PKT3(PKT3_PFP_SYNC_ME, 0, cmd_buffer->state.predicating));
		radeon_emit(cs, 0);
	}
}

/* GFX9+ metadata cache flushing workaround. metadata cache coherency is
 * broken if the CB caches data of multiple mips of the same image at the
 * same time.
 *
 * Insert some flushes to avoid this.
 */
static void
radv_emit_fb_mip_change_flush(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_framebuffer *framebuffer = cmd_buffer->state.framebuffer;
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	bool color_mip_changed = false;

	/* Entire workaround is not applicable before GFX9 */
	if (cmd_buffer->device->physical_device->rad_info.chip_class < GFX9)
		return;

	if (!framebuffer)
		return;

	for (int i = 0; i < subpass->color_count; ++i) {
		int idx = subpass->color_attachments[i].attachment;
		if (idx == VK_ATTACHMENT_UNUSED)
			continue;

		struct radv_image_view *iview = cmd_buffer->state.attachments[idx].iview;

		if ((radv_image_has_CB_metadata(iview->image) ||
		     radv_image_has_dcc(iview->image)) &&
		    cmd_buffer->state.cb_mip[i] != iview->base_mip)
			color_mip_changed = true;

		cmd_buffer->state.cb_mip[i] = iview->base_mip;
	}

	if (color_mip_changed) {
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
		                                RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
	}
}

/* This function does the flushes for mip changes if the levels are not zero for
 * all render targets. This way we can assume at the start of the next cmd_buffer
 * that rendering to mip 0 doesn't need any flushes. As that is the most common
 * case that saves some flushes. */
static void
radv_emit_mip_change_flush_default(struct radv_cmd_buffer *cmd_buffer)
{
	/* Entire workaround is not applicable before GFX9 */
	if (cmd_buffer->device->physical_device->rad_info.chip_class < GFX9)
		return;

	bool need_color_mip_flush = false;
	for (unsigned i = 0; i < 8; ++i) {
		if (cmd_buffer->state.cb_mip[i]) {
			need_color_mip_flush = true;
			break;
		}
	}

	if (need_color_mip_flush) {
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
		                                RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
	}

	memset(cmd_buffer->state.cb_mip, 0, sizeof(cmd_buffer->state.cb_mip));
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
		struct radv_image_view *iview = cmd_buffer->state.attachments[idx].iview;
		VkImageLayout layout = subpass->color_attachments[i].layout;
		bool in_render_loop = subpass->color_attachments[i].in_render_loop;

		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, iview->bo);

		assert(iview->aspect_mask & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_PLANE_0_BIT |
		                                       VK_IMAGE_ASPECT_PLANE_1_BIT | VK_IMAGE_ASPECT_PLANE_2_BIT));
		radv_emit_fb_color_state(cmd_buffer, i, &cmd_buffer->state.attachments[idx].cb, iview, layout,
					 in_render_loop, cmd_buffer->state.attachments[idx].disable_dcc);

		radv_load_color_clear_metadata(cmd_buffer, iview, i);
	}

	if (subpass->depth_stencil_attachment) {
		int idx = subpass->depth_stencil_attachment->attachment;
		VkImageLayout layout = subpass->depth_stencil_attachment->layout;
		bool in_render_loop = subpass->depth_stencil_attachment->in_render_loop;
		struct radv_image_view *iview = cmd_buffer->state.attachments[idx].iview;
		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, cmd_buffer->state.attachments[idx].iview->bo);

		radv_emit_fb_ds_state(cmd_buffer, &cmd_buffer->state.attachments[idx].ds, iview, layout, in_render_loop);

		if (cmd_buffer->state.attachments[idx].ds.offset_scale != cmd_buffer->state.offset_scale) {
			cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS;
			cmd_buffer->state.offset_scale = cmd_buffer->state.attachments[idx].ds.offset_scale;
		}
		radv_load_ds_clear_metadata(cmd_buffer, iview);
	} else {
		if (cmd_buffer->device->physical_device->rad_info.chip_class == GFX9)
			radeon_set_context_reg_seq(cmd_buffer->cs, R_028038_DB_Z_INFO, 2);
		else
			radeon_set_context_reg_seq(cmd_buffer->cs, R_028040_DB_Z_INFO, 2);

		radeon_emit(cmd_buffer->cs, S_028040_FORMAT(V_028040_Z_INVALID)); /* DB_Z_INFO */
		radeon_emit(cmd_buffer->cs, S_028044_FORMAT(V_028044_STENCIL_INVALID)); /* DB_STENCIL_INFO */
	}
	radeon_set_context_reg(cmd_buffer->cs, R_028208_PA_SC_WINDOW_SCISSOR_BR,
			       S_028208_BR_X(framebuffer->width) |
			       S_028208_BR_Y(framebuffer->height));

	if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX8) {
		bool disable_constant_encode =
			cmd_buffer->device->physical_device->rad_info.has_dcc_constant_encode;
		enum chip_class chip_class =
			cmd_buffer->device->physical_device->rad_info.chip_class;
		uint8_t watermark = chip_class >= GFX10 ? 6 : 4;

		radeon_set_context_reg(cmd_buffer->cs, R_028424_CB_DCC_CONTROL,
				       S_028424_OVERWRITE_COMBINER_MRT_SHARING_DISABLE(chip_class <= GFX9) |
				       S_028424_OVERWRITE_COMBINER_WATERMARK(watermark) |
				       S_028424_DISABLE_CONSTANT_ENCODE_REG(disable_constant_encode));
	}

	if (cmd_buffer->device->dfsm_allowed) {
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cmd_buffer->cs, EVENT_TYPE(V_028A90_BREAK_BATCH) | EVENT_INDEX(0));
	}

	cmd_buffer->state.dirty &= ~RADV_CMD_DIRTY_FRAMEBUFFER;
}

static void
radv_emit_index_buffer(struct radv_cmd_buffer *cmd_buffer, bool indirect)
{
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (state->index_type != state->last_index_type) {
		if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9) {
			radeon_set_uconfig_reg_idx(cmd_buffer->device->physical_device,
						   cs, R_03090C_VGT_INDEX_TYPE,
						   2, state->index_type);
		} else {
			radeon_emit(cs, PKT3(PKT3_INDEX_TYPE, 0, 0));
			radeon_emit(cs, state->index_type);
		}

		state->last_index_type = state->index_type;
	}

	/* For the direct indexed draws we use DRAW_INDEX_2, which includes
	 * the index_va and max_index_count already. */
	if (!indirect)
		return;

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
		if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX7) {
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
		}
		db_count_control = S_028004_ZPASS_INCREMENT_DISABLE(1);
	} else {
		const struct radv_subpass *subpass = cmd_buffer->state.subpass;
		uint32_t sample_rate = subpass ? util_logbase2(subpass->max_sample_count) : 0;
		bool gfx10_perfect = cmd_buffer->device->physical_device->rad_info.chip_class >= GFX10 && has_perfect_queries;

		if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX7) {
			/* Always enable PERFECT_ZPASS_COUNTS due to issues with partially
			 * covered tiles, discards, and early depth testing. For more details,
			 * see https://gitlab.freedesktop.org/mesa/mesa/-/issues/3218 */
			db_count_control =
				S_028004_PERFECT_ZPASS_COUNTS(1) |
				S_028004_DISABLE_CONSERVATIVE_ZPASS_COUNTS(gfx10_perfect) |
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

	cmd_buffer->state.context_roll_without_scissor_emitted = true;
}

static void
radv_cmd_buffer_flush_dynamic_state(struct radv_cmd_buffer *cmd_buffer)
{
	uint32_t states = cmd_buffer->state.dirty & cmd_buffer->state.emitted_pipeline->graphics.needed_dynamic_state;

	if (states & (RADV_CMD_DIRTY_DYNAMIC_VIEWPORT))
		radv_emit_viewport(cmd_buffer);

	if (states & (RADV_CMD_DIRTY_DYNAMIC_SCISSOR | RADV_CMD_DIRTY_DYNAMIC_VIEWPORT) &&
	    !cmd_buffer->device->physical_device->rad_info.has_gfx9_scissor_bug)
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

	if (states & RADV_CMD_DIRTY_DYNAMIC_SAMPLE_LOCATIONS)
		radv_emit_sample_locations(cmd_buffer);

	if (states & RADV_CMD_DIRTY_DYNAMIC_LINE_STIPPLE)
		radv_emit_line_stipple(cmd_buffer);

	if (states & (RADV_CMD_DIRTY_DYNAMIC_CULL_MODE |
		      RADV_CMD_DIRTY_DYNAMIC_FRONT_FACE))
		radv_emit_culling(cmd_buffer, states);

	if (states & RADV_CMD_DIRTY_DYNAMIC_PRIMITIVE_TOPOLOGY)
		radv_emit_primitive_topology(cmd_buffer);

	if (states & (RADV_CMD_DIRTY_DYNAMIC_DEPTH_TEST_ENABLE |
		      RADV_CMD_DIRTY_DYNAMIC_DEPTH_WRITE_ENABLE |
		      RADV_CMD_DIRTY_DYNAMIC_DEPTH_COMPARE_OP |
		      RADV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE |
		      RADV_CMD_DIRTY_DYNAMIC_STENCIL_TEST_ENABLE |
		      RADV_CMD_DIRTY_DYNAMIC_STENCIL_OP))
		radv_emit_depth_control(cmd_buffer, states);

	if (states & RADV_CMD_DIRTY_DYNAMIC_STENCIL_OP)
		radv_emit_stencil_control(cmd_buffer);

	if (states & RADV_CMD_DIRTY_DYNAMIC_FRAGMENT_SHADING_RATE)
		radv_emit_fragment_shading_rate(cmd_buffer);

	cmd_buffer->state.dirty &= ~states;
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
	uint32_t size = MAX_SETS * 4;
	uint32_t offset;
	void *ptr;

	if (!radv_cmd_buffer_upload_alloc(cmd_buffer, size,
					  256, &offset, &ptr))
		return;

	for (unsigned i = 0; i < MAX_SETS; i++) {
		uint32_t *uptr = ((uint32_t *)ptr) + i;
		uint64_t set_va = 0;
		struct radv_descriptor_set *set = descriptors_state->sets[i];
		if (descriptors_state->valid & (1u << i))
			set_va = set->va;
		uptr[0] = set_va & 0xffffffff;
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
	struct radv_cmd_state *state = &cmd_buffer->state;
	bool flush_indirect_descriptors;

	if (!descriptors_state->dirty)
		return;

	if (descriptors_state->push_dirty)
		radv_flush_push_descriptors(cmd_buffer, bind_point);

	flush_indirect_descriptors =
		(bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS &&
		 state->pipeline && state->pipeline->need_indirect_descriptor_sets) ||
		(bind_point == VK_PIPELINE_BIND_POINT_COMPUTE &&
		 state->compute_pipeline && state->compute_pipeline->need_indirect_descriptor_sets);

	if (flush_indirect_descriptors)
		radv_flush_indirect_descriptor_sets(cmd_buffer, bind_point);

	ASSERTED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws,
	                                                   cmd_buffer->cs,
	                                                   MAX_SETS * MESA_SHADER_STAGES * 4);

	if (cmd_buffer->state.pipeline) {
		radv_foreach_stage(stage, stages) {
			if (!cmd_buffer->state.pipeline->shaders[stage])
				continue;

			radv_emit_descriptor_pointers(cmd_buffer,
						      cmd_buffer->state.pipeline,
						      descriptors_state, stage);
		}
	}

	if (cmd_buffer->state.compute_pipeline &&
	    (stages & VK_SHADER_STAGE_COMPUTE_BIT)) {
		radv_emit_descriptor_pointers(cmd_buffer,
					      cmd_buffer->state.compute_pipeline,
					      descriptors_state,
					      MESA_SHADER_COMPUTE);
	}

	descriptors_state->dirty = 0;
	descriptors_state->push_dirty = false;

	assert(cmd_buffer->cs->cdw <= cdw_max);

	if (unlikely(cmd_buffer->device->trace_bo))
		radv_save_descriptors(cmd_buffer, bind_point);
}

static void
radv_flush_constants(struct radv_cmd_buffer *cmd_buffer,
		     VkShaderStageFlags stages)
{
	struct radv_pipeline *pipeline = stages & VK_SHADER_STAGE_COMPUTE_BIT
					 ? cmd_buffer->state.compute_pipeline
					 : cmd_buffer->state.pipeline;
	VkPipelineBindPoint bind_point = stages & VK_SHADER_STAGE_COMPUTE_BIT ?
					 VK_PIPELINE_BIND_POINT_COMPUTE :
					 VK_PIPELINE_BIND_POINT_GRAPHICS;
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, bind_point);
	struct radv_pipeline_layout *layout = pipeline->layout;
	struct radv_shader_variant *shader, *prev_shader;
	bool need_push_constants = false;
	unsigned offset;
	void *ptr;
	uint64_t va;

	stages &= cmd_buffer->push_constant_stages;
	if (!stages ||
	    (!layout->push_constant_size && !layout->dynamic_offset_count))
		return;

	radv_foreach_stage(stage, stages) {
		shader = radv_get_shader(pipeline, stage);
		if (!shader)
			continue;

		need_push_constants |= shader->info.loads_push_constants;
		need_push_constants |= shader->info.loads_dynamic_offsets;

		uint8_t base = shader->info.base_inline_push_consts;
		uint8_t count = shader->info.num_inline_push_consts;

		radv_emit_inline_push_consts(cmd_buffer, pipeline, stage,
					     AC_UD_INLINE_PUSH_CONSTANTS,
					     count,
					     (uint32_t *)&cmd_buffer->push_constants[base * 4]);
	}

	if (need_push_constants) {
		if (!radv_cmd_buffer_upload_alloc(cmd_buffer, layout->push_constant_size +
						  16 * layout->dynamic_offset_count,
						  256, &offset, &ptr))
			return;

		memcpy(ptr, cmd_buffer->push_constants, layout->push_constant_size);
		memcpy((char*)ptr + layout->push_constant_size,
		       descriptors_state->dynamic_buffers,
		       16 * layout->dynamic_offset_count);

		va = radv_buffer_get_va(cmd_buffer->upload.upload_bo);
		va += offset;

		ASSERTED unsigned cdw_max =
			radeon_check_space(cmd_buffer->device->ws,
	                                   cmd_buffer->cs, MESA_SHADER_STAGES * 4);

		prev_shader = NULL;
		radv_foreach_stage(stage, stages) {
			shader = radv_get_shader(pipeline, stage);

			/* Avoid redundantly emitting the address for merged stages. */
			if (shader && shader != prev_shader) {
				radv_emit_userdata_address(cmd_buffer, pipeline, stage,
							   AC_UD_PUSH_CONSTANTS, va);

				prev_shader = shader;
			}
		}
		assert(cmd_buffer->cs->cdw <= cdw_max);
	}

	cmd_buffer->push_constant_stages &= ~stages;
}

static void
radv_flush_vertex_descriptors(struct radv_cmd_buffer *cmd_buffer,
			      bool pipeline_is_dirty)
{
	if ((pipeline_is_dirty ||
	    (cmd_buffer->state.dirty & RADV_CMD_DIRTY_VERTEX_BUFFER)) &&
	    cmd_buffer->state.pipeline->num_vertex_bindings &&
	    radv_get_shader(cmd_buffer->state.pipeline, MESA_SHADER_VERTEX)->info.vs.has_vertex_buffers) {
		unsigned vb_offset;
		void *vb_ptr;
		uint32_t i = 0;
		uint32_t count = cmd_buffer->state.pipeline->num_vertex_bindings;
		uint64_t va;

		/* allocate some descriptor state for vertex buffers */
		if (!radv_cmd_buffer_upload_alloc(cmd_buffer, count * 16, 256,
						  &vb_offset, &vb_ptr))
			return;

		for (i = 0; i < count; i++) {
			uint32_t *desc = &((uint32_t *)vb_ptr)[i * 4];
			uint32_t offset;
			struct radv_buffer *buffer = cmd_buffer->vertex_bindings[i].buffer;
			unsigned num_records;
			unsigned stride;

			if (!buffer) {
				memset(desc, 0, 4 * 4);
				continue;
			}

			va = radv_buffer_get_va(buffer->bo);

			offset = cmd_buffer->vertex_bindings[i].offset;
			va += offset + buffer->offset;

			if (cmd_buffer->vertex_bindings[i].size) {
				num_records = cmd_buffer->vertex_bindings[i].size;
			} else {
				num_records = buffer->size - offset;
			}

			if (cmd_buffer->state.pipeline->graphics.uses_dynamic_stride) {
				stride = cmd_buffer->vertex_bindings[i].stride;
			} else {
				stride = cmd_buffer->state.pipeline->binding_stride[i];
			}

			if (cmd_buffer->device->physical_device->rad_info.chip_class != GFX8 && stride)
				num_records /= stride;

			uint32_t rsrc_word3 = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
					      S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
					      S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
					      S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

			if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX10) {
				/* OOB_SELECT chooses the out-of-bounds check:
				 * - 1: index >= NUM_RECORDS (Structured)
				 * - 3: offset >= NUM_RECORDS (Raw)
				 */
                               int oob_select = stride ? V_008F0C_OOB_SELECT_STRUCTURED : V_008F0C_OOB_SELECT_RAW;

                               rsrc_word3 |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_UINT) |
					     S_008F0C_OOB_SELECT(oob_select) |
					     S_008F0C_RESOURCE_LEVEL(1);
                       } else {
                               rsrc_word3 |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_UINT) |
					     S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
                       }

			desc[0] = va;
			desc[1] = S_008F04_BASE_ADDRESS_HI(va >> 32) | S_008F04_STRIDE(stride);
			desc[2] = num_records;
			desc[3] = rsrc_word3;
		}

		va = radv_buffer_get_va(cmd_buffer->upload.upload_bo);
		va += vb_offset;

		radv_emit_userdata_address(cmd_buffer, cmd_buffer->state.pipeline, MESA_SHADER_VERTEX,
					   AC_UD_VS_VERTEX_BUFFERS, va);

		cmd_buffer->state.vb_va = va;
		cmd_buffer->state.vb_size = count * 16;
		cmd_buffer->state.prefetch_L2_mask |= RADV_PREFETCH_VBO_DESCRIPTORS;

		if (unlikely(cmd_buffer->device->trace_bo))
			radv_save_vertex_descriptors(cmd_buffer, (uintptr_t)vb_ptr);
	}
	cmd_buffer->state.dirty &= ~RADV_CMD_DIRTY_VERTEX_BUFFER;
}

static void
radv_emit_streamout_buffers(struct radv_cmd_buffer *cmd_buffer, uint64_t va)
{
	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;
	struct radv_userdata_info *loc;
	uint32_t base_reg;

	for (unsigned stage = 0; stage < MESA_SHADER_STAGES; ++stage) {
		if (!radv_get_shader(pipeline, stage))
			continue;

		loc = radv_lookup_user_sgpr(pipeline, stage,
					    AC_UD_STREAMOUT_BUFFERS);
		if (loc->sgpr_idx == -1)
			continue;

		base_reg = pipeline->user_data_0[stage];

		radv_emit_shader_pointer(cmd_buffer->device, cmd_buffer->cs,
					 base_reg + loc->sgpr_idx * 4, va, false);
	}

	if (radv_pipeline_has_gs_copy_shader(pipeline)) {
		loc = &pipeline->gs_copy_shader->info.user_sgprs_locs.shader_data[AC_UD_STREAMOUT_BUFFERS];
		if (loc->sgpr_idx != -1) {
			base_reg = R_00B130_SPI_SHADER_USER_DATA_VS_0;

			radv_emit_shader_pointer(cmd_buffer->device, cmd_buffer->cs,
						 base_reg + loc->sgpr_idx * 4, va, false);
		}
	}
}

static void
radv_flush_streamout_descriptors(struct radv_cmd_buffer *cmd_buffer)
{
	if (cmd_buffer->state.dirty & RADV_CMD_DIRTY_STREAMOUT_BUFFER) {
		struct radv_streamout_binding *sb = cmd_buffer->streamout_bindings;
		struct radv_streamout_state *so = &cmd_buffer->state.streamout;
		unsigned so_offset;
		void *so_ptr;
		uint64_t va;

		/* Allocate some descriptor state for streamout buffers. */
		if (!radv_cmd_buffer_upload_alloc(cmd_buffer,
						  MAX_SO_BUFFERS * 16, 256,
						  &so_offset, &so_ptr))
			return;

		for (uint32_t i = 0; i < MAX_SO_BUFFERS; i++) {
			struct radv_buffer *buffer = sb[i].buffer;
			uint32_t *desc = &((uint32_t *)so_ptr)[i * 4];

			if (!(so->enabled_mask & (1 << i)))
				continue;

			va = radv_buffer_get_va(buffer->bo) + buffer->offset;

			va += sb[i].offset;

			/* Set the descriptor.
			 *
			 * On GFX8, the format must be non-INVALID, otherwise
			 * the buffer will be considered not bound and store
			 * instructions will be no-ops.
			 */
			uint32_t size = 0xffffffff;

			/* Compute the correct buffer size for NGG streamout
			 * because it's used to determine the max emit per
			 * buffer.
			 */
			if (cmd_buffer->device->physical_device->use_ngg_streamout)
				size = buffer->size - sb[i].offset;

			uint32_t rsrc_word3 = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
					      S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
					      S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
					      S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

			if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX10) {
				rsrc_word3 |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
					      S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) |
					      S_008F0C_RESOURCE_LEVEL(1);
			} else {
				rsrc_word3 |= S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
			}

			desc[0] = va;
			desc[1] = S_008F04_BASE_ADDRESS_HI(va >> 32);
			desc[2] = size;
			desc[3] = rsrc_word3;
		}

		va = radv_buffer_get_va(cmd_buffer->upload.upload_bo);
		va += so_offset;

		radv_emit_streamout_buffers(cmd_buffer, va);
	}

	cmd_buffer->state.dirty &= ~RADV_CMD_DIRTY_STREAMOUT_BUFFER;
}

static void
radv_flush_ngg_gs_state(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;
	struct radv_userdata_info *loc;
	uint32_t ngg_gs_state = 0;
	uint32_t base_reg;

	if (!radv_pipeline_has_gs(pipeline) ||
	    !radv_pipeline_has_ngg(pipeline))
		return;

	/* By default NGG GS queries are disabled but they are enabled if the
	 * command buffer has active GDS queries or if it's a secondary command
	 * buffer that inherits the number of generated primitives.
	 */
	if (cmd_buffer->state.active_pipeline_gds_queries ||
	    (cmd_buffer->state.inherited_pipeline_statistics & VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT))
		ngg_gs_state = 1;

	loc = radv_lookup_user_sgpr(pipeline, MESA_SHADER_GEOMETRY,
				    AC_UD_NGG_GS_STATE);
	base_reg = pipeline->user_data_0[MESA_SHADER_GEOMETRY];
	assert(loc->sgpr_idx != -1);

	radeon_set_sh_reg(cmd_buffer->cs, base_reg + loc->sgpr_idx * 4,
			  ngg_gs_state);
}

static void
radv_upload_graphics_shader_descriptors(struct radv_cmd_buffer *cmd_buffer, bool pipeline_is_dirty)
{
	radv_flush_vertex_descriptors(cmd_buffer, pipeline_is_dirty);
	radv_flush_streamout_descriptors(cmd_buffer);
	radv_flush_descriptors(cmd_buffer, VK_SHADER_STAGE_ALL_GRAPHICS);
	radv_flush_constants(cmd_buffer, VK_SHADER_STAGE_ALL_GRAPHICS);
	radv_flush_ngg_gs_state(cmd_buffer);
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

	/**
	 * Stream output parameters resource.
	 */
	struct radv_buffer *strmout_buffer;
	uint64_t strmout_buffer_offset;
};

static uint32_t
radv_get_primitive_reset_index(struct radv_cmd_buffer *cmd_buffer)
{
	switch (cmd_buffer->state.index_type) {
	case V_028A7C_VGT_INDEX_8:
		return 0xffu;
	case V_028A7C_VGT_INDEX_16:
		return 0xffffu;
	case V_028A7C_VGT_INDEX_32:
		return 0xffffffffu;
	default:
		unreachable("invalid index type");
	}
}

static void
si_emit_ia_multi_vgt_param(struct radv_cmd_buffer *cmd_buffer,
			   bool instanced_draw, bool indirect_draw,
			   bool count_from_stream_output,
			   uint32_t draw_vertex_count)
{
	struct radeon_info *info = &cmd_buffer->device->physical_device->rad_info;
	struct radv_cmd_state *state = &cmd_buffer->state;
	unsigned topology = state->dynamic.primitive_topology;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	unsigned ia_multi_vgt_param;

	ia_multi_vgt_param =
		si_get_ia_multi_vgt_param(cmd_buffer, instanced_draw,
					  indirect_draw,
					  count_from_stream_output,
					  draw_vertex_count,
					  topology);

	if (state->last_ia_multi_vgt_param != ia_multi_vgt_param) {
		if (info->chip_class == GFX9) {
			radeon_set_uconfig_reg_idx(cmd_buffer->device->physical_device,
						   cs,
						   R_030960_IA_MULTI_VGT_PARAM,
						   4, ia_multi_vgt_param);
		} else if (info->chip_class >= GFX7) {
			radeon_set_context_reg_idx(cs,
						   R_028AA8_IA_MULTI_VGT_PARAM,
						   1, ia_multi_vgt_param);
		} else {
			radeon_set_context_reg(cs, R_028AA8_IA_MULTI_VGT_PARAM,
					       ia_multi_vgt_param);
		}
		state->last_ia_multi_vgt_param = ia_multi_vgt_param;
	}
}

static void
radv_emit_draw_registers(struct radv_cmd_buffer *cmd_buffer,
			 const struct radv_draw_info *draw_info)
{
	struct radeon_info *info = &cmd_buffer->device->physical_device->rad_info;
	struct radv_cmd_state *state = &cmd_buffer->state;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	int32_t primitive_reset_en;

	/* Draw state. */
	if (info->chip_class < GFX10) {
		si_emit_ia_multi_vgt_param(cmd_buffer, draw_info->instance_count > 1,
					   draw_info->indirect,
					   !!draw_info->strmout_buffer,
					   draw_info->indirect ? 0 : draw_info->count);
	}

	/* Primitive restart. */
	primitive_reset_en =
		draw_info->indexed && state->pipeline->graphics.prim_restart_enable;

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
			radv_get_primitive_reset_index(cmd_buffer);

		if (primitive_reset_index != state->last_primitive_reset_index) {
			radeon_set_context_reg(cs,
					       R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX,
					       primitive_reset_index);
			state->last_primitive_reset_index = primitive_reset_index;
		}
	}

	if (draw_info->strmout_buffer) {
		uint64_t va = radv_buffer_get_va(draw_info->strmout_buffer->bo);

		va += draw_info->strmout_buffer->offset +
		      draw_info->strmout_buffer_offset;

		radeon_set_context_reg(cs, R_028B30_VGT_STRMOUT_DRAW_OPAQUE_VERTEX_STRIDE,
				       draw_info->stride);

		radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
		radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_SRC_MEM) |
				COPY_DATA_DST_SEL(COPY_DATA_REG) |
				COPY_DATA_WR_CONFIRM);
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		radeon_emit(cs, R_028B2C_VGT_STRMOUT_DRAW_OPAQUE_BUFFER_FILLED_SIZE >> 2);
		radeon_emit(cs, 0); /* unused */

		radv_cs_add_buffer(cmd_buffer->device->ws, cs, draw_info->strmout_buffer->bo);
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

	if (src_stage_mask & (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
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
	                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
				     VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
				     VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
				     VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
				     VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT)) {
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_VS_PARTIAL_FLUSH;
	}
}

static enum radv_cmd_flush_bits
radv_src_access_flush(struct radv_cmd_buffer *cmd_buffer,
		      VkAccessFlags src_flags,
		      struct radv_image *image)
{
	bool flush_CB_meta = true, flush_DB_meta = true;
	enum radv_cmd_flush_bits flush_bits = 0;
	uint32_t b;

	if (image) {
		if (!radv_image_has_CB_metadata(image))
			flush_CB_meta = false;
		if (!radv_image_has_htile(image))
			flush_DB_meta = false;
	}

	for_each_bit(b, src_flags) {
		switch ((VkAccessFlagBits)(1 << b)) {
		case VK_ACCESS_SHADER_WRITE_BIT:
		case VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT:
		case VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT:
			flush_bits |= RADV_CMD_FLAG_WB_L2;
			break;
		case VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:
			flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB;
			if (flush_CB_meta)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
			break;
		case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
			flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB;
			if (flush_DB_meta)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;
			break;
		case VK_ACCESS_TRANSFER_WRITE_BIT:
			flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
			              RADV_CMD_FLAG_FLUSH_AND_INV_DB |
			              RADV_CMD_FLAG_INV_L2;

			if (flush_CB_meta)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
			if (flush_DB_meta)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;
			break;
		case VK_ACCESS_MEMORY_WRITE_BIT:
			flush_bits |= RADV_CMD_FLAG_INV_L2 |
				      RADV_CMD_FLAG_WB_L2 |
				      RADV_CMD_FLAG_FLUSH_AND_INV_CB |
				      RADV_CMD_FLAG_FLUSH_AND_INV_DB;

			if (flush_CB_meta)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
			if (flush_DB_meta)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;
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
	bool flush_CB_meta = true, flush_DB_meta = true;
	enum radv_cmd_flush_bits flush_bits = 0;
	bool flush_CB = true, flush_DB = true;
	bool image_is_coherent = false;
	uint32_t b;

	if (image) {
		if (!(image->usage & VK_IMAGE_USAGE_STORAGE_BIT)) {
			flush_CB = false;
			flush_DB = false;
		}

		if (!radv_image_has_CB_metadata(image))
			flush_CB_meta = false;
		if (!radv_image_has_htile(image))
			flush_DB_meta = false;

		/* TODO: implement shader coherent for GFX10 */

		if (cmd_buffer->device->physical_device->rad_info.chip_class == GFX9) {
			if (image->info.samples == 1 &&
			    (image->usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
					     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
			    !vk_format_is_stencil(image->vk_format)) {
				/* Single-sample color and single-sample depth
				 * (not stencil) are coherent with shaders on
				 * GFX9.
				 */
				image_is_coherent = true;
			}
		}
	}

	for_each_bit(b, dst_flags) {
		switch ((VkAccessFlagBits)(1 << b)) {
		case VK_ACCESS_INDIRECT_COMMAND_READ_BIT:
		case VK_ACCESS_INDEX_READ_BIT:
		case VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT:
			break;
		case VK_ACCESS_UNIFORM_READ_BIT:
			flush_bits |= RADV_CMD_FLAG_INV_VCACHE | RADV_CMD_FLAG_INV_SCACHE;
			break;
		case VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:
		case VK_ACCESS_TRANSFER_READ_BIT:
		case VK_ACCESS_INPUT_ATTACHMENT_READ_BIT:
			flush_bits |= RADV_CMD_FLAG_INV_VCACHE |
			              RADV_CMD_FLAG_INV_L2;
			break;
		case VK_ACCESS_SHADER_READ_BIT:
			flush_bits |= RADV_CMD_FLAG_INV_VCACHE;
			/* Unlike LLVM, ACO uses SMEM for SSBOs and we have to
			 * invalidate the scalar cache. */
			if (!cmd_buffer->device->physical_device->use_llvm && !image)
				flush_bits |= RADV_CMD_FLAG_INV_SCACHE;

			if (!image_is_coherent)
				flush_bits |= RADV_CMD_FLAG_INV_L2;
			break;
		case VK_ACCESS_COLOR_ATTACHMENT_READ_BIT:
			if (flush_CB)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB;
			if (flush_CB_meta)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
			break;
		case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT:
			if (flush_DB)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB;
			if (flush_DB_meta)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;
			break;
		case VK_ACCESS_MEMORY_READ_BIT:
			flush_bits |= RADV_CMD_FLAG_INV_VCACHE |
				      RADV_CMD_FLAG_INV_SCACHE |
			              RADV_CMD_FLAG_INV_L2;
			if (flush_CB)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB;
			if (flush_CB_meta)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
			if (flush_DB)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB;
			if (flush_DB_meta)
				flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;
			break;
		default:
			break;
		}
	}
	return flush_bits;
}

void radv_subpass_barrier(struct radv_cmd_buffer *cmd_buffer,
			  const struct radv_subpass_barrier *barrier)
{
	cmd_buffer->state.flush_bits |= radv_src_access_flush(cmd_buffer, barrier->src_access_mask,
							      NULL);
	radv_stage_flush(cmd_buffer, barrier->src_stage_mask);
	cmd_buffer->state.flush_bits |= radv_dst_access_flush(cmd_buffer, barrier->dst_access_mask,
	                                                      NULL);
}

uint32_t
radv_get_subpass_id(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_cmd_state *state = &cmd_buffer->state;
	uint32_t subpass_id = state->subpass - state->pass->subpasses;

	/* The id of this subpass shouldn't exceed the number of subpasses in
	 * this render pass minus 1.
	 */
	assert(subpass_id < state->pass->subpass_count);
	return subpass_id;
}

static struct radv_sample_locations_state *
radv_get_attachment_sample_locations(struct radv_cmd_buffer *cmd_buffer,
				     uint32_t att_idx,
				     bool begin_subpass)
{
	struct radv_cmd_state *state = &cmd_buffer->state;
	uint32_t subpass_id = radv_get_subpass_id(cmd_buffer);
	struct radv_image_view *view = state->attachments[att_idx].iview;

	if (view->image->info.samples == 1)
		return NULL;

	if (state->pass->attachments[att_idx].first_subpass_idx == subpass_id) {
		/* Return the initial sample locations if this is the initial
		 * layout transition of the given subpass attachemnt.
		 */
		if (state->attachments[att_idx].sample_location.count > 0)
			return &state->attachments[att_idx].sample_location;
	} else {
		/* Otherwise return the subpass sample locations if defined. */
		if (state->subpass_sample_locs) {
			/* Because the driver sets the current subpass before
			 * initial layout transitions, we should use the sample
			 * locations from the previous subpass to avoid an
			 * off-by-one problem. Otherwise, use the sample
			 * locations for the current subpass for final layout
			 * transitions.
			 */
			if (begin_subpass)
				subpass_id--;

			for (uint32_t i = 0; i < state->num_subpass_sample_locs; i++) {
				if (state->subpass_sample_locs[i].subpass_idx == subpass_id)
					return &state->subpass_sample_locs[i].sample_location;
			}
		}
	}

	return NULL;
}

static void radv_handle_subpass_image_transition(struct radv_cmd_buffer *cmd_buffer,
						 struct radv_subpass_attachment att,
						 bool begin_subpass)
{
	unsigned idx = att.attachment;
	struct radv_image_view *view = cmd_buffer->state.attachments[idx].iview;
	struct radv_sample_locations_state *sample_locs;
	VkImageSubresourceRange range;
	range.aspectMask = view->aspect_mask;
	range.baseMipLevel = view->base_mip;
	range.levelCount = 1;
	range.baseArrayLayer = view->base_layer;
	range.layerCount = cmd_buffer->state.framebuffer->layers;

	if (cmd_buffer->state.subpass->view_mask) {
		/* If the current subpass uses multiview, the driver might have
		 * performed a fast color/depth clear to the whole image
		 * (including all layers). To make sure the driver will
		 * decompress the image correctly (if needed), we have to
		 * account for the "real" number of layers. If the view mask is
		 * sparse, this will decompress more layers than needed.
		 */
		range.layerCount = util_last_bit(cmd_buffer->state.subpass->view_mask);
	}

	/* Get the subpass sample locations for the given attachment, if NULL
	 * is returned the driver will use the default HW locations.
	 */
	sample_locs = radv_get_attachment_sample_locations(cmd_buffer, idx,
							   begin_subpass);

	/* Determine if the subpass uses separate depth/stencil layouts. */
	bool uses_separate_depth_stencil_layouts = false;
	if ((cmd_buffer->state.attachments[idx].current_layout !=
	     cmd_buffer->state.attachments[idx].current_stencil_layout) ||
	    (att.layout != att.stencil_layout)) {
		uses_separate_depth_stencil_layouts = true;
	}

	/* For separate layouts, perform depth and stencil transitions
	 * separately.
	 */
	if (uses_separate_depth_stencil_layouts &&
	    (range.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT |
				  VK_IMAGE_ASPECT_STENCIL_BIT))) {
		/* Depth-only transitions. */
		range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		radv_handle_image_transition(cmd_buffer,
					     view->image,
					     cmd_buffer->state.attachments[idx].current_layout,
					     cmd_buffer->state.attachments[idx].current_in_render_loop,
					     att.layout, att.in_render_loop,
					     0, 0, &range, sample_locs);

		/* Stencil-only transitions. */
		range.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		radv_handle_image_transition(cmd_buffer,
					     view->image,
					     cmd_buffer->state.attachments[idx].current_stencil_layout,
					     cmd_buffer->state.attachments[idx].current_in_render_loop,
					     att.stencil_layout, att.in_render_loop,
					     0, 0, &range, sample_locs);
	} else {
		radv_handle_image_transition(cmd_buffer,
					     view->image,
					     cmd_buffer->state.attachments[idx].current_layout,
					     cmd_buffer->state.attachments[idx].current_in_render_loop,
					     att.layout, att.in_render_loop,
					     0, 0, &range, sample_locs);
	}

	cmd_buffer->state.attachments[idx].current_layout = att.layout;
	cmd_buffer->state.attachments[idx].current_stencil_layout = att.stencil_layout;
	cmd_buffer->state.attachments[idx].current_in_render_loop = att.in_render_loop;


}

void
radv_cmd_buffer_set_subpass(struct radv_cmd_buffer *cmd_buffer,
			    const struct radv_subpass *subpass)
{
	cmd_buffer->state.subpass = subpass;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_FRAMEBUFFER;
}

static VkResult
radv_cmd_state_setup_sample_locations(struct radv_cmd_buffer *cmd_buffer,
				      struct radv_render_pass *pass,
				      const VkRenderPassBeginInfo *info)
{
	const struct VkRenderPassSampleLocationsBeginInfoEXT *sample_locs =
		vk_find_struct_const(info->pNext,
				     RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT);
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (!sample_locs) {
		state->subpass_sample_locs = NULL;
		return VK_SUCCESS;
	}

	for (uint32_t i = 0; i < sample_locs->attachmentInitialSampleLocationsCount; i++) {
		const VkAttachmentSampleLocationsEXT *att_sample_locs =
			&sample_locs->pAttachmentInitialSampleLocations[i];
		uint32_t att_idx = att_sample_locs->attachmentIndex;
		struct radv_image *image = cmd_buffer->state.attachments[att_idx].iview->image;

		assert(vk_format_is_depth_or_stencil(image->vk_format));

		/* From the Vulkan spec 1.1.108:
		 *
		 * "If the image referenced by the framebuffer attachment at
		 *  index attachmentIndex was not created with
		 *  VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT
		 *  then the values specified in sampleLocationsInfo are
		 *  ignored."
		 */
		if (!(image->flags & VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT))
			continue;

		const VkSampleLocationsInfoEXT *sample_locs_info =
			&att_sample_locs->sampleLocationsInfo;

		state->attachments[att_idx].sample_location.per_pixel =
			sample_locs_info->sampleLocationsPerPixel;
		state->attachments[att_idx].sample_location.grid_size =
			sample_locs_info->sampleLocationGridSize;
		state->attachments[att_idx].sample_location.count =
			sample_locs_info->sampleLocationsCount;
		typed_memcpy(&state->attachments[att_idx].sample_location.locations[0],
			     sample_locs_info->pSampleLocations,
			     sample_locs_info->sampleLocationsCount);
	}

	state->subpass_sample_locs = vk_alloc(&cmd_buffer->pool->alloc,
					      sample_locs->postSubpassSampleLocationsCount *
					      sizeof(state->subpass_sample_locs[0]),
					      8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (state->subpass_sample_locs == NULL) {
		cmd_buffer->record_result = VK_ERROR_OUT_OF_HOST_MEMORY;
		return cmd_buffer->record_result;
	}

	state->num_subpass_sample_locs = sample_locs->postSubpassSampleLocationsCount;

	for (uint32_t i = 0; i < sample_locs->postSubpassSampleLocationsCount; i++) {
		const VkSubpassSampleLocationsEXT *subpass_sample_locs_info =
			&sample_locs->pPostSubpassSampleLocations[i];
		const VkSampleLocationsInfoEXT *sample_locs_info =
			&subpass_sample_locs_info->sampleLocationsInfo;

		state->subpass_sample_locs[i].subpass_idx =
			subpass_sample_locs_info->subpassIndex;
		state->subpass_sample_locs[i].sample_location.per_pixel =
			sample_locs_info->sampleLocationsPerPixel;
		state->subpass_sample_locs[i].sample_location.grid_size =
			sample_locs_info->sampleLocationGridSize;
		state->subpass_sample_locs[i].sample_location.count =
			sample_locs_info->sampleLocationsCount;
		typed_memcpy(&state->subpass_sample_locs[i].sample_location.locations[0],
			     sample_locs_info->pSampleLocations,
			     sample_locs_info->sampleLocationsCount);
	}

	return VK_SUCCESS;
}

static VkResult
radv_cmd_state_setup_attachments(struct radv_cmd_buffer *cmd_buffer,
				 struct radv_render_pass *pass,
				 const VkRenderPassBeginInfo *info,
				 const struct radv_extra_render_pass_begin_info *extra)
{
	struct radv_cmd_state *state = &cmd_buffer->state;
	const struct VkRenderPassAttachmentBeginInfo *attachment_info = NULL;

	if (info) {
		attachment_info = vk_find_struct_const(info->pNext,
		                                       RENDER_PASS_ATTACHMENT_BEGIN_INFO);
	}


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
		state->attachments[i].current_in_render_loop = false;
		state->attachments[i].current_stencil_layout = att->stencil_initial_layout;
		state->attachments[i].disable_dcc = extra && extra->disable_dcc;
		state->attachments[i].sample_location.count = 0;

		struct radv_image_view *iview;
		if (attachment_info && attachment_info->attachmentCount > i) {
			iview = radv_image_view_from_handle(attachment_info->pAttachments[i]);
		} else {
			iview = state->framebuffer->attachments[i];
		}

		state->attachments[i].iview = iview;
		if (iview->aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
			radv_initialise_ds_surface(cmd_buffer->device, &state->attachments[i].ds, iview);
		} else {
			radv_initialise_color_surface(cmd_buffer->device, &state->attachments[i].cb, iview);
		}
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

		if (!list_is_empty(&pool->free_cmd_buffers)) {
			struct radv_cmd_buffer *cmd_buffer = list_first_entry(&pool->free_cmd_buffers, struct radv_cmd_buffer, pool_link);

			list_del(&cmd_buffer->pool_link);
			list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);

			result = radv_reset_cmd_buffer(cmd_buffer);
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
				radv_destroy_cmd_buffer(cmd_buffer);

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
	cmd_buffer->state.predication_type = -1;
	cmd_buffer->state.last_sx_ps_downconvert = -1;
	cmd_buffer->state.last_sx_blend_opt_epsilon = -1;
	cmd_buffer->state.last_sx_blend_opt_control = -1;
	cmd_buffer->usage_flags = pBeginInfo->flags;

	if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY &&
	    (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)) {
		assert(pBeginInfo->pInheritanceInfo);
		cmd_buffer->state.framebuffer = radv_framebuffer_from_handle(pBeginInfo->pInheritanceInfo->framebuffer);
		cmd_buffer->state.pass = radv_render_pass_from_handle(pBeginInfo->pInheritanceInfo->renderPass);

		struct radv_subpass *subpass =
			&cmd_buffer->state.pass->subpasses[pBeginInfo->pInheritanceInfo->subpass];

		if (cmd_buffer->state.framebuffer) {
			result = radv_cmd_state_setup_attachments(cmd_buffer, cmd_buffer->state.pass, NULL, NULL);
			if (result != VK_SUCCESS)
				return result;
		}

		cmd_buffer->state.inherited_pipeline_statistics =
			pBeginInfo->pInheritanceInfo->pipelineStatistics;

		radv_cmd_buffer_set_subpass(cmd_buffer, subpass);
	}

	if (unlikely(cmd_buffer->device->trace_bo))
		radv_cmd_buffer_trace_emit(cmd_buffer);

	radv_describe_begin_cmd_buffer(cmd_buffer);

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
	radv_CmdBindVertexBuffers2EXT(commandBuffer, firstBinding,
				      bindingCount, pBuffers, pOffsets,
				      NULL, NULL);
}

void radv_CmdBindVertexBuffers2EXT(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    firstBinding,
	uint32_t                                    bindingCount,
	const VkBuffer*                             pBuffers,
	const VkDeviceSize*                         pOffsets,
	const VkDeviceSize*                         pSizes,
	const VkDeviceSize*                         pStrides)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_vertex_binding *vb = cmd_buffer->vertex_bindings;
	bool changed = false;

	/* We have to defer setting up vertex buffer since we need the buffer
	 * stride from the pipeline. */

	assert(firstBinding + bindingCount <= MAX_VBS);
	for (uint32_t i = 0; i < bindingCount; i++) {
		RADV_FROM_HANDLE(radv_buffer, buffer, pBuffers[i]);
		uint32_t idx = firstBinding + i;
		VkDeviceSize size = pSizes ? pSizes[i] : 0;
		VkDeviceSize stride = pStrides ? pStrides[i] : 0;

		/* pSizes and pStrides are optional. */
		if (!changed &&
		    (vb[idx].buffer != buffer ||
		     vb[idx].offset != pOffsets[i] ||
		     vb[idx].size != size ||
		     vb[idx].stride != stride)) {
			changed = true;
		}

		vb[idx].buffer = buffer;
		vb[idx].offset = pOffsets[i];
		vb[idx].size = size;
		vb[idx].stride = stride;

		if (buffer) {
			radv_cs_add_buffer(cmd_buffer->device->ws,
					   cmd_buffer->cs, vb[idx].buffer->bo);
		}
	}

	if (!changed) {
		/* No state changes. */
		return;
	}

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_VERTEX_BUFFER;
}

static uint32_t
vk_to_index_type(VkIndexType type)
{
	switch (type) {
	case VK_INDEX_TYPE_UINT8_EXT:
		return V_028A7C_VGT_INDEX_8;
	case VK_INDEX_TYPE_UINT16:
		return V_028A7C_VGT_INDEX_16;
	case VK_INDEX_TYPE_UINT32:
		return V_028A7C_VGT_INDEX_32;
	default:
		unreachable("invalid index type");
	}
}

static uint32_t
radv_get_vgt_index_size(uint32_t type)
{
	switch (type) {
	case V_028A7C_VGT_INDEX_8:
		return 1;
	case V_028A7C_VGT_INDEX_16:
		return 2;
	case V_028A7C_VGT_INDEX_32:
		return 4;
	default:
		unreachable("invalid index type");
	}
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
	cmd_buffer->state.index_type = vk_to_index_type(indexType);
	cmd_buffer->state.index_va = radv_buffer_get_va(index_buffer->bo);
	cmd_buffer->state.index_va += index_buffer->offset + offset;

	int index_size = radv_get_vgt_index_size(vk_to_index_type(indexType));
	cmd_buffer->state.max_index_count = (index_buffer->size - offset) / index_size;
	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_INDEX_BUFFER;
	radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, index_buffer->bo);
}


static void
radv_bind_descriptor_set(struct radv_cmd_buffer *cmd_buffer,
			 VkPipelineBindPoint bind_point,
			 struct radv_descriptor_set *set, unsigned idx)
{
	struct radeon_winsys *ws = cmd_buffer->device->ws;

	radv_set_descriptor_set(cmd_buffer, bind_point, set, idx);

	assert(set);

	if (!cmd_buffer->device->use_global_bo_list) {
		for (unsigned j = 0; j < set->buffer_count; ++j)
			if (set->descriptors[j])
				radv_cs_add_buffer(ws, cmd_buffer->cs, set->descriptors[j]);
	}

	if(set->bo)
		radv_cs_add_buffer(ws, cmd_buffer->cs, set->bo);
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
	struct radv_descriptor_state *descriptors_state =
		radv_get_descriptors_state(cmd_buffer, pipelineBindPoint);

	for (unsigned i = 0; i < descriptorSetCount; ++i) {
		unsigned set_idx = i + firstSet;
		RADV_FROM_HANDLE(radv_descriptor_set, set, pDescriptorSets[i]);

		/* If the set is already bound we only need to update the
		 * (potentially changed) dynamic offsets. */
		if (descriptors_state->sets[set_idx] != set ||
		    !(descriptors_state->valid & (1u << set_idx))) {
			radv_bind_descriptor_set(cmd_buffer, pipelineBindPoint, set, set_idx);
		}

		for(unsigned j = 0; j < layout->set[set_idx].dynamic_offset_count; ++j, ++dyn_idx) {
			unsigned idx = j + layout->set[i + firstSet].dynamic_offset_start;
			uint32_t *dst = descriptors_state->dynamic_buffers + idx * 4;
			assert(dyn_idx < dynamicOffsetCount);

			struct radv_descriptor_range *range = set->dynamic_descriptors + j;

			if (!range->va) {
				memset(dst, 0, 4 * 4);
			} else {
				uint64_t va = range->va + pDynamicOffsets[dyn_idx];
				dst[0] = va;
				dst[1] = S_008F04_BASE_ADDRESS_HI(va >> 32);
				dst[2] = no_dynamic_bounds ? 0xffffffffu : range->size;
				dst[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
					 S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
					 S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
					 S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

				if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX10) {
					dst[3] |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
						  S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) |
						  S_008F0C_RESOURCE_LEVEL(1);
				} else {
					dst[3] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
						  S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
				}
			}

			cmd_buffer->push_constant_stages |= layout->set[set_idx].dynamic_offset_stages;
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

	/* Check that there are no inline uniform block updates when calling vkCmdPushDescriptorSetKHR()
	 * because it is invalid, according to Vulkan spec.
	 */
	for (int i = 0; i < descriptorWriteCount; i++) {
		ASSERTED const VkWriteDescriptorSet *writeset = &pDescriptorWrites[i];
		assert(writeset->descriptorType != VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT);
	}

	radv_update_descriptor_sets(cmd_buffer->device, cmd_buffer,
	                            radv_descriptor_set_to_handle(push_set),
	                            descriptorWriteCount, pDescriptorWrites, 0, NULL);

	radv_set_descriptor_set(cmd_buffer, pipelineBindPoint, push_set, set);
	descriptors_state->push_dirty = true;
}

void radv_CmdPushDescriptorSetWithTemplateKHR(
	VkCommandBuffer                             commandBuffer,
	VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
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

	radv_emit_mip_change_flush_default(cmd_buffer);

	if (cmd_buffer->queue_family_index != RADV_QUEUE_TRANSFER) {
		if (cmd_buffer->device->physical_device->rad_info.chip_class == GFX6)
			cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_PS_PARTIAL_FLUSH | RADV_CMD_FLAG_WB_L2;

		/* Make sure to sync all pending active queries at the end of
		 * command buffer.
		 */
		cmd_buffer->state.flush_bits |= cmd_buffer->active_query_flush_bits;

		/* Since NGG streamout uses GDS, we need to make GDS idle when
		 * we leave the IB, otherwise another process might overwrite
		 * it while our shaders are busy.
		 */
		if (cmd_buffer->gds_needed)
			cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_PS_PARTIAL_FLUSH;

		si_emit_cache_flush(cmd_buffer);
	}

	/* Make sure CP DMA is idle at the end of IBs because the kernel
	 * doesn't wait for it.
	 */
	si_cp_dma_wait_for_idle(cmd_buffer);

	radv_describe_end_cmd_buffer(cmd_buffer);

	vk_free(&cmd_buffer->pool->alloc, cmd_buffer->state.attachments);
	vk_free(&cmd_buffer->pool->alloc, cmd_buffer->state.subpass_sample_locs);

	VkResult result = cmd_buffer->device->ws->cs_finalize(cmd_buffer->cs);
	if (result != VK_SUCCESS)
		return vk_error(cmd_buffer->device->instance, result);

	cmd_buffer->status = RADV_CMD_BUFFER_STATUS_EXECUTABLE;

	return cmd_buffer->record_result;
}

static void
radv_emit_compute_pipeline(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;

	if (!pipeline || pipeline == cmd_buffer->state.emitted_compute_pipeline)
		return;

	assert(!pipeline->ctx_cs.cdw);

	cmd_buffer->state.emitted_compute_pipeline = pipeline;

	radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, pipeline->cs.cdw);
	radeon_emit_array(cmd_buffer->cs, pipeline->cs.buf, pipeline->cs.cdw);

	cmd_buffer->compute_scratch_size_per_wave_needed = MAX2(cmd_buffer->compute_scratch_size_per_wave_needed,
	                                                        pipeline->scratch_bytes_per_wave);
	cmd_buffer->compute_scratch_waves_wanted = MAX2(cmd_buffer->compute_scratch_waves_wanted,
	                                                pipeline->max_waves);

	radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs,
			   pipeline->shaders[MESA_SHADER_COMPUTE]->bo);

	if (unlikely(cmd_buffer->device->trace_bo))
		radv_save_pipeline(cmd_buffer, pipeline);
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

		if ((cmd_buffer->device->physical_device->rad_info.chip_class == GFX10 ||
		     cmd_buffer->device->physical_device->rad_info.family == CHIP_SIENNA_CICHLID) &&
		    cmd_buffer->state.emitted_pipeline &&
		    radv_pipeline_has_ngg(cmd_buffer->state.emitted_pipeline) &&
		    !radv_pipeline_has_ngg(cmd_buffer->state.pipeline)) {
			/* Transitioning from NGG to legacy GS requires
			 * VGT_FLUSH on GFX10 and Sienna Cichlid. VGT_FLUSH
			 * is also emitted at the beginning of IBs when legacy
			 * GS ring pointers are set.
			 */
			cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_VGT_FLUSH;
		}

		radv_bind_dynamic_state(cmd_buffer, &pipeline->dynamic_state);
		radv_bind_streamout_state(cmd_buffer, pipeline);

		if (pipeline->graphics.esgs_ring_size > cmd_buffer->esgs_ring_size_needed)
			cmd_buffer->esgs_ring_size_needed = pipeline->graphics.esgs_ring_size;
		if (pipeline->graphics.gsvs_ring_size > cmd_buffer->gsvs_ring_size_needed)
			cmd_buffer->gsvs_ring_size_needed = pipeline->graphics.gsvs_ring_size;

		if (radv_pipeline_has_tess(pipeline))
			cmd_buffer->tess_rings_needed = true;
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
	ASSERTED const uint32_t total_count = firstViewport + viewportCount;

	assert(firstViewport < MAX_VIEWPORTS);
	assert(total_count >= 1 && total_count <= MAX_VIEWPORTS);

	if (total_count <= state->dynamic.viewport.count &&
	    !memcmp(state->dynamic.viewport.viewports + firstViewport,
		    pViewports, viewportCount * sizeof(*pViewports))) {
		return;
	}

	if (state->dynamic.viewport.count < total_count)
		state->dynamic.viewport.count = total_count;

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
	ASSERTED const uint32_t total_count = firstScissor + scissorCount;

	assert(firstScissor < MAX_SCISSORS);
	assert(total_count >= 1 && total_count <= MAX_SCISSORS);

	if (total_count <= state->dynamic.scissor.count &&
	    !memcmp(state->dynamic.scissor.scissors + firstScissor, pScissors,
		    scissorCount * sizeof(*pScissors))) {
		return;
	}

	if (state->dynamic.scissor.count < total_count)
		state->dynamic.scissor.count = total_count;

	memcpy(state->dynamic.scissor.scissors + firstScissor, pScissors,
	       scissorCount * sizeof(*pScissors));

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_SCISSOR;
}

void radv_CmdSetLineWidth(
	VkCommandBuffer                             commandBuffer,
	float                                       lineWidth)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	if (cmd_buffer->state.dynamic.line_width == lineWidth)
		return;

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
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (state->dynamic.depth_bias.bias == depthBiasConstantFactor &&
	    state->dynamic.depth_bias.clamp == depthBiasClamp &&
	    state->dynamic.depth_bias.slope == depthBiasSlopeFactor) {
		return;
	}

	state->dynamic.depth_bias.bias = depthBiasConstantFactor;
	state->dynamic.depth_bias.clamp = depthBiasClamp;
	state->dynamic.depth_bias.slope = depthBiasSlopeFactor;

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS;
}

void radv_CmdSetBlendConstants(
	VkCommandBuffer                             commandBuffer,
	const float                                 blendConstants[4])
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (!memcmp(state->dynamic.blend_constants, blendConstants, sizeof(float) * 4))
		return;

	memcpy(state->dynamic.blend_constants, blendConstants, sizeof(float) * 4);

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS;
}

void radv_CmdSetDepthBounds(
	VkCommandBuffer                             commandBuffer,
	float                                       minDepthBounds,
	float                                       maxDepthBounds)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (state->dynamic.depth_bounds.min == minDepthBounds &&
	    state->dynamic.depth_bounds.max == maxDepthBounds) {
		return;
	}

	state->dynamic.depth_bounds.min = minDepthBounds;
	state->dynamic.depth_bounds.max = maxDepthBounds;

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS;
}

void radv_CmdSetStencilCompareMask(
	VkCommandBuffer                             commandBuffer,
	VkStencilFaceFlags                          faceMask,
	uint32_t                                    compareMask)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;
	bool front_same = state->dynamic.stencil_compare_mask.front == compareMask;
	bool back_same = state->dynamic.stencil_compare_mask.back == compareMask;

	if ((!(faceMask & VK_STENCIL_FACE_FRONT_BIT) || front_same) &&
	    (!(faceMask & VK_STENCIL_FACE_BACK_BIT) || back_same)) {
		return;
	}

	if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
		state->dynamic.stencil_compare_mask.front = compareMask;
	if (faceMask & VK_STENCIL_FACE_BACK_BIT)
		state->dynamic.stencil_compare_mask.back = compareMask;

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK;
}

void radv_CmdSetStencilWriteMask(
	VkCommandBuffer                             commandBuffer,
	VkStencilFaceFlags                          faceMask,
	uint32_t                                    writeMask)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;
	bool front_same = state->dynamic.stencil_write_mask.front == writeMask;
	bool back_same = state->dynamic.stencil_write_mask.back == writeMask;

	if ((!(faceMask & VK_STENCIL_FACE_FRONT_BIT) || front_same) &&
	    (!(faceMask & VK_STENCIL_FACE_BACK_BIT) || back_same)) {
		return;
	}

	if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
		state->dynamic.stencil_write_mask.front = writeMask;
	if (faceMask & VK_STENCIL_FACE_BACK_BIT)
		state->dynamic.stencil_write_mask.back = writeMask;

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK;
}

void radv_CmdSetStencilReference(
	VkCommandBuffer                             commandBuffer,
	VkStencilFaceFlags                          faceMask,
	uint32_t                                    reference)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;
	bool front_same = state->dynamic.stencil_reference.front == reference;
	bool back_same = state->dynamic.stencil_reference.back == reference;

	if ((!(faceMask & VK_STENCIL_FACE_FRONT_BIT) || front_same) &&
	    (!(faceMask & VK_STENCIL_FACE_BACK_BIT) || back_same)) {
		return;
	}

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
	ASSERTED const uint32_t total_count = firstDiscardRectangle + discardRectangleCount;

	assert(firstDiscardRectangle < MAX_DISCARD_RECTANGLES);
	assert(total_count >= 1 && total_count <= MAX_DISCARD_RECTANGLES);

	if (!memcmp(state->dynamic.discard_rectangle.rectangles + firstDiscardRectangle,
		    pDiscardRectangles, discardRectangleCount * sizeof(*pDiscardRectangles))) {
		return;
	}

	typed_memcpy(&state->dynamic.discard_rectangle.rectangles[firstDiscardRectangle],
	             pDiscardRectangles, discardRectangleCount);

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_DISCARD_RECTANGLE;
}

void radv_CmdSetSampleLocationsEXT(
	VkCommandBuffer                             commandBuffer,
	const VkSampleLocationsInfoEXT*             pSampleLocationsInfo)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;

	assert(pSampleLocationsInfo->sampleLocationsCount <= MAX_SAMPLE_LOCATIONS);

	state->dynamic.sample_location.per_pixel = pSampleLocationsInfo->sampleLocationsPerPixel;
	state->dynamic.sample_location.grid_size = pSampleLocationsInfo->sampleLocationGridSize;
	state->dynamic.sample_location.count = pSampleLocationsInfo->sampleLocationsCount;
	typed_memcpy(&state->dynamic.sample_location.locations[0],
		     pSampleLocationsInfo->pSampleLocations,
		     pSampleLocationsInfo->sampleLocationsCount);

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_SAMPLE_LOCATIONS;
}

void radv_CmdSetLineStippleEXT(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    lineStippleFactor,
	uint16_t                                    lineStipplePattern)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;

	state->dynamic.line_stipple.factor = lineStippleFactor;
	state->dynamic.line_stipple.pattern = lineStipplePattern;

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_LINE_STIPPLE;
}

void radv_CmdSetCullModeEXT(
	VkCommandBuffer                             commandBuffer,
	VkCullModeFlags                             cullMode)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (state->dynamic.cull_mode == cullMode)
		return;

	state->dynamic.cull_mode = cullMode;

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_CULL_MODE;
}

void radv_CmdSetFrontFaceEXT(
	VkCommandBuffer                             commandBuffer,
	VkFrontFace                                 frontFace)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (state->dynamic.front_face == frontFace)
		return;

	state->dynamic.front_face = frontFace;

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_FRONT_FACE;
}

void radv_CmdSetPrimitiveTopologyEXT(
	VkCommandBuffer                             commandBuffer,
	VkPrimitiveTopology                         primitiveTopology)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;
	unsigned primitive_topology = si_translate_prim(primitiveTopology);

	if (state->dynamic.primitive_topology == primitive_topology)
		return;

	state->dynamic.primitive_topology = primitive_topology;

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_PRIMITIVE_TOPOLOGY;
}

void radv_CmdSetViewportWithCountEXT(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    viewportCount,
	const VkViewport*                           pViewports)
{
	radv_CmdSetViewport(commandBuffer, 0, viewportCount, pViewports);
}

void radv_CmdSetScissorWithCountEXT(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    scissorCount,
	const VkRect2D*                             pScissors)
{
	radv_CmdSetScissor(commandBuffer, 0, scissorCount, pScissors);
}

void radv_CmdSetDepthTestEnableEXT(
	VkCommandBuffer                             commandBuffer,
	VkBool32                                    depthTestEnable)

{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (state->dynamic.depth_test_enable == depthTestEnable)
		return;

	state->dynamic.depth_test_enable = depthTestEnable;

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_TEST_ENABLE;
}

void radv_CmdSetDepthWriteEnableEXT(
	VkCommandBuffer                             commandBuffer,
	VkBool32                                    depthWriteEnable)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (state->dynamic.depth_write_enable == depthWriteEnable)
		return;

	state->dynamic.depth_write_enable = depthWriteEnable;

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_WRITE_ENABLE;
}

void radv_CmdSetDepthCompareOpEXT(
	VkCommandBuffer                             commandBuffer,
	VkCompareOp                                 depthCompareOp)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (state->dynamic.depth_compare_op == depthCompareOp)
		return;

	state->dynamic.depth_compare_op = depthCompareOp;

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_COMPARE_OP;
}

void radv_CmdSetDepthBoundsTestEnableEXT(
	VkCommandBuffer                             commandBuffer,
	VkBool32                                    depthBoundsTestEnable)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (state->dynamic.depth_bounds_test_enable == depthBoundsTestEnable)
		return;

	state->dynamic.depth_bounds_test_enable = depthBoundsTestEnable;

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE;
}

void radv_CmdSetStencilTestEnableEXT(
	VkCommandBuffer                             commandBuffer,
	VkBool32                                    stencilTestEnable)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (state->dynamic.stencil_test_enable == stencilTestEnable)
		return;

	state->dynamic.stencil_test_enable = stencilTestEnable;

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_STENCIL_TEST_ENABLE;
}

void radv_CmdSetStencilOpEXT(
	VkCommandBuffer                             commandBuffer,
	VkStencilFaceFlags                          faceMask,
	VkStencilOp                                 failOp,
	VkStencilOp                                 passOp,
	VkStencilOp                                 depthFailOp,
	VkCompareOp                                 compareOp)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;
	bool front_same =
		state->dynamic.stencil_op.front.fail_op == failOp &&
		state->dynamic.stencil_op.front.pass_op == passOp &&
		state->dynamic.stencil_op.front.depth_fail_op == depthFailOp &&
		state->dynamic.stencil_op.front.compare_op == compareOp;
	bool back_same =
		state->dynamic.stencil_op.back.fail_op == failOp &&
		state->dynamic.stencil_op.back.pass_op == passOp &&
		state->dynamic.stencil_op.back.depth_fail_op == depthFailOp &&
		state->dynamic.stencil_op.back.compare_op == compareOp;

	if ((!(faceMask & VK_STENCIL_FACE_FRONT_BIT) || front_same) &&
	    (!(faceMask & VK_STENCIL_FACE_BACK_BIT) || back_same))
		return;

	if (faceMask & VK_STENCIL_FACE_FRONT_BIT) {
		state->dynamic.stencil_op.front.fail_op = failOp;
		state->dynamic.stencil_op.front.pass_op = passOp;
		state->dynamic.stencil_op.front.depth_fail_op = depthFailOp;
		state->dynamic.stencil_op.front.compare_op = compareOp;
	}

	if (faceMask & VK_STENCIL_FACE_BACK_BIT) {
		state->dynamic.stencil_op.back.fail_op = failOp;
		state->dynamic.stencil_op.back.pass_op = passOp;
		state->dynamic.stencil_op.back.depth_fail_op = depthFailOp;
		state->dynamic.stencil_op.back.compare_op = compareOp;
	}

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_STENCIL_OP;
}

void radv_CmdSetFragmentShadingRateKHR(
	VkCommandBuffer                             commandBuffer,
	const VkExtent2D*                           pFragmentSize,
	const VkFragmentShadingRateCombinerOpKHR    combinerOps[2])
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_cmd_state *state = &cmd_buffer->state;

	state->dynamic.fragment_shading_rate.size = *pFragmentSize;
	for (unsigned i = 0; i < 2; i++)
		state->dynamic.fragment_shading_rate.combiner_ops[i] = combinerOps[i];

	state->dirty |= RADV_CMD_DIRTY_DYNAMIC_FRAGMENT_SHADING_RATE;
}

void radv_CmdExecuteCommands(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    commandBufferCount,
	const VkCommandBuffer*                      pCmdBuffers)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, primary, commandBuffer);

	assert(commandBufferCount > 0);

	radv_emit_mip_change_flush_default(primary);

	/* Emit pending flushes on primary prior to executing secondary */
	si_emit_cache_flush(primary);

	for (uint32_t i = 0; i < commandBufferCount; i++) {
		RADV_FROM_HANDLE(radv_cmd_buffer, secondary, pCmdBuffers[i]);

		primary->scratch_size_per_wave_needed = MAX2(primary->scratch_size_per_wave_needed,
		                                             secondary->scratch_size_per_wave_needed);
		primary->scratch_waves_wanted = MAX2(primary->scratch_waves_wanted,
		                                     secondary->scratch_waves_wanted);
		primary->compute_scratch_size_per_wave_needed = MAX2(primary->compute_scratch_size_per_wave_needed,
		                                                     secondary->compute_scratch_size_per_wave_needed);
		primary->compute_scratch_waves_wanted = MAX2(primary->compute_scratch_waves_wanted,
		                                             secondary->compute_scratch_waves_wanted);

		if (secondary->esgs_ring_size_needed > primary->esgs_ring_size_needed)
			primary->esgs_ring_size_needed = secondary->esgs_ring_size_needed;
		if (secondary->gsvs_ring_size_needed > primary->gsvs_ring_size_needed)
			primary->gsvs_ring_size_needed = secondary->gsvs_ring_size_needed;
		if (secondary->tess_rings_needed)
			primary->tess_rings_needed = true;
		if (secondary->sample_positions_needed)
			primary->sample_positions_needed = true;
		if (secondary->gds_needed)
			primary->gds_needed = true;

		if (!secondary->state.framebuffer &&
		    (primary->state.dirty & RADV_CMD_DIRTY_FRAMEBUFFER)) {
			/* Emit the framebuffer state from primary if secondary
			 * has been recorded without a framebuffer, otherwise
			 * fast color/depth clears can't work.
			 */
			radv_emit_fb_mip_change_flush(primary);
			radv_emit_framebuffer_state(primary);
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
		primary->state.last_sx_ps_downconvert = secondary->state.last_sx_ps_downconvert;
		primary->state.last_sx_blend_opt_epsilon = secondary->state.last_sx_blend_opt_epsilon;
		primary->state.last_sx_blend_opt_control = secondary->state.last_sx_blend_opt_control;

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

	pool = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*pool), 8,
			   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (pool == NULL)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	vk_object_base_init(&device->vk, &pool->base,
			    VK_OBJECT_TYPE_COMMAND_POOL);

	if (pAllocator)
		pool->alloc = *pAllocator;
	else
		pool->alloc = device->vk.alloc;

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
		radv_destroy_cmd_buffer(cmd_buffer);
	}

	list_for_each_entry_safe(struct radv_cmd_buffer, cmd_buffer,
				 &pool->free_cmd_buffers, pool_link) {
		radv_destroy_cmd_buffer(cmd_buffer);
	}

	vk_object_base_finish(&pool->base);
	vk_free2(&device->vk.alloc, pAllocator, pool);
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
    VkCommandPoolTrimFlags                      flags)
{
	RADV_FROM_HANDLE(radv_cmd_pool, pool, commandPool);

	if (!pool)
		return;

	list_for_each_entry_safe(struct radv_cmd_buffer, cmd_buffer,
				 &pool->free_cmd_buffers, pool_link) {
		radv_destroy_cmd_buffer(cmd_buffer);
	}
}

static void
radv_cmd_buffer_begin_subpass(struct radv_cmd_buffer *cmd_buffer,
			      uint32_t subpass_id)
{
	struct radv_cmd_state *state = &cmd_buffer->state;
	struct radv_subpass *subpass = &state->pass->subpasses[subpass_id];

	ASSERTED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws,
							   cmd_buffer->cs, 4096);

	radv_subpass_barrier(cmd_buffer, &subpass->start_barrier);

	radv_cmd_buffer_set_subpass(cmd_buffer, subpass);

	radv_describe_barrier_start(cmd_buffer, RGP_BARRIER_EXTERNAL_RENDER_PASS_SYNC);

	for (uint32_t i = 0; i < subpass->attachment_count; ++i) {
		const uint32_t a = subpass->attachments[i].attachment;
		if (a == VK_ATTACHMENT_UNUSED)
			continue;

		radv_handle_subpass_image_transition(cmd_buffer,
						     subpass->attachments[i],
						     true);
	}

	radv_describe_barrier_end(cmd_buffer);

	radv_cmd_buffer_clear_subpass(cmd_buffer);

	assert(cmd_buffer->cs->cdw <= cdw_max);
}

static void
radv_cmd_buffer_end_subpass(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_cmd_state *state = &cmd_buffer->state;
	const struct radv_subpass *subpass = state->subpass;
	uint32_t subpass_id = radv_get_subpass_id(cmd_buffer);

	radv_cmd_buffer_resolve_subpass(cmd_buffer);

	radv_describe_barrier_start(cmd_buffer, RGP_BARRIER_EXTERNAL_RENDER_PASS_SYNC);

	for (uint32_t i = 0; i < subpass->attachment_count; ++i) {
		const uint32_t a = subpass->attachments[i].attachment;
		if (a == VK_ATTACHMENT_UNUSED)
			continue;

		if (state->pass->attachments[a].last_subpass_idx != subpass_id)
			continue;

		VkImageLayout layout = state->pass->attachments[a].final_layout;
		VkImageLayout stencil_layout = state->pass->attachments[a].stencil_final_layout;
		struct radv_subpass_attachment att = { a, layout, stencil_layout };
		radv_handle_subpass_image_transition(cmd_buffer, att, false);
	}

	radv_describe_barrier_end(cmd_buffer);
}

void
radv_cmd_buffer_begin_render_pass(struct radv_cmd_buffer *cmd_buffer,
				  const VkRenderPassBeginInfo *pRenderPassBegin,
				  const struct radv_extra_render_pass_begin_info *extra_info)
{
	RADV_FROM_HANDLE(radv_render_pass, pass, pRenderPassBegin->renderPass);
	RADV_FROM_HANDLE(radv_framebuffer, framebuffer, pRenderPassBegin->framebuffer);
	VkResult result;

	cmd_buffer->state.framebuffer = framebuffer;
	cmd_buffer->state.pass = pass;
	cmd_buffer->state.render_area = pRenderPassBegin->renderArea;

	result = radv_cmd_state_setup_attachments(cmd_buffer, pass, pRenderPassBegin, extra_info);
	if (result != VK_SUCCESS)
		return;

	result = radv_cmd_state_setup_sample_locations(cmd_buffer, pass, pRenderPassBegin);
	if (result != VK_SUCCESS)
		return;
}

void radv_CmdBeginRenderPass(
	VkCommandBuffer                             commandBuffer,
	const VkRenderPassBeginInfo*                pRenderPassBegin,
	VkSubpassContents                           contents)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	radv_cmd_buffer_begin_render_pass(cmd_buffer, pRenderPassBegin, NULL);

	radv_cmd_buffer_begin_subpass(cmd_buffer, 0);
}

void radv_CmdBeginRenderPass2(
    VkCommandBuffer                             commandBuffer,
    const VkRenderPassBeginInfo*                pRenderPassBeginInfo,
    const VkSubpassBeginInfo*                   pSubpassBeginInfo)
{
	radv_CmdBeginRenderPass(commandBuffer, pRenderPassBeginInfo,
				pSubpassBeginInfo->contents);
}

void radv_CmdNextSubpass(
    VkCommandBuffer                             commandBuffer,
    VkSubpassContents                           contents)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	uint32_t prev_subpass = radv_get_subpass_id(cmd_buffer);
	radv_cmd_buffer_end_subpass(cmd_buffer);
	radv_cmd_buffer_begin_subpass(cmd_buffer, prev_subpass + 1);
}

void radv_CmdNextSubpass2(
    VkCommandBuffer                             commandBuffer,
    const VkSubpassBeginInfo*                   pSubpassBeginInfo,
    const VkSubpassEndInfo*                     pSubpassEndInfo)
{
	radv_CmdNextSubpass(commandBuffer, pSubpassBeginInfo->contents);
}

static void radv_emit_view_index(struct radv_cmd_buffer *cmd_buffer, unsigned index)
{
	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;
	for (unsigned stage = 0; stage < MESA_SHADER_STAGES; ++stage) {
		if (!radv_get_shader(pipeline, stage))
			continue;

		struct radv_userdata_info *loc = radv_lookup_user_sgpr(pipeline, stage, AC_UD_VIEW_INDEX);
		if (loc->sgpr_idx == -1)
			continue;
		uint32_t base_reg = pipeline->user_data_0[stage];
		radeon_set_sh_reg(cmd_buffer->cs, base_reg + loc->sgpr_idx * 4, index);

	}
	if (radv_pipeline_has_gs_copy_shader(pipeline)) {
		struct radv_userdata_info *loc = &pipeline->gs_copy_shader->info.user_sgprs_locs.shader_data[AC_UD_VIEW_INDEX];
		if (loc->sgpr_idx != -1) {
			uint32_t base_reg = R_00B130_SPI_SHADER_USER_DATA_VS_0;
			radeon_set_sh_reg(cmd_buffer->cs, base_reg + loc->sgpr_idx * 4, index);
		}
	}
}

static void
radv_cs_emit_draw_packet(struct radv_cmd_buffer *cmd_buffer,
                         uint32_t vertex_count,
			 bool use_opaque)
{
	radeon_emit(cmd_buffer->cs, PKT3(PKT3_DRAW_INDEX_AUTO, 1, cmd_buffer->state.predicating));
	radeon_emit(cmd_buffer->cs, vertex_count);
	radeon_emit(cmd_buffer->cs, V_0287F0_DI_SRC_SEL_AUTO_INDEX |
	                            S_0287F0_USE_OPAQUE(use_opaque));
}

static void
radv_cs_emit_draw_indexed_packet(struct radv_cmd_buffer *cmd_buffer,
                                 uint64_t index_va,
                                 uint32_t index_count)
{
	radeon_emit(cmd_buffer->cs, PKT3(PKT3_DRAW_INDEX_2, 4, cmd_buffer->state.predicating));
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
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	unsigned di_src_sel = indexed ? V_0287F0_DI_SRC_SEL_DMA
	                              : V_0287F0_DI_SRC_SEL_AUTO_INDEX;
	bool draw_id_enable = radv_get_shader(cmd_buffer->state.pipeline, MESA_SHADER_VERTEX)->info.vs.needs_draw_id;
	uint32_t base_reg = cmd_buffer->state.pipeline->graphics.vtx_base_sgpr;
	bool predicating = cmd_buffer->state.predicating;
	assert(base_reg);

	/* just reset draw state for vertex data */
	cmd_buffer->state.last_first_instance = -1;
	cmd_buffer->state.last_num_instances = -1;
	cmd_buffer->state.last_vertex_offset = -1;

	if (draw_count == 1 && !count_va && !draw_id_enable) {
		radeon_emit(cs, PKT3(indexed ? PKT3_DRAW_INDEX_INDIRECT :
				     PKT3_DRAW_INDIRECT, 3, predicating));
		radeon_emit(cs, 0);
		radeon_emit(cs, (base_reg - SI_SH_REG_OFFSET) >> 2);
		radeon_emit(cs, ((base_reg + 4) - SI_SH_REG_OFFSET) >> 2);
		radeon_emit(cs, di_src_sel);
	} else {
		radeon_emit(cs, PKT3(indexed ? PKT3_DRAW_INDEX_INDIRECT_MULTI :
				     PKT3_DRAW_INDIRECT_MULTI,
				     8, predicating));
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

static void
radv_emit_draw_packets(struct radv_cmd_buffer *cmd_buffer,
		       const struct radv_draw_info *info)
{
	struct radv_cmd_state *state = &cmd_buffer->state;
	struct radeon_winsys *ws = cmd_buffer->device->ws;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;

	radv_describe_draw(cmd_buffer);

	if (info->indirect) {
		uint64_t va = radv_buffer_get_va(info->indirect->bo);
		uint64_t count_va = 0;

		va += info->indirect->offset + info->indirect_offset;

		radv_cs_add_buffer(ws, cs, info->indirect->bo);

		radeon_emit(cs, PKT3(PKT3_SET_BASE, 2, 0));
		radeon_emit(cs, 1);
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);

		if (info->count_buffer) {
			count_va = radv_buffer_get_va(info->count_buffer->bo);
			count_va += info->count_buffer->offset +
				    info->count_buffer_offset;

			radv_cs_add_buffer(ws, cs, info->count_buffer->bo);
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
			int index_size = radv_get_vgt_index_size(state->index_type);
			uint64_t index_va;

			/* Skip draw calls with 0-sized index buffers. They
			 * cause a hang on some chips, like Navi10-14.
			 */
			if (!cmd_buffer->state.max_index_count)
				return;

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
				radv_cs_emit_draw_packet(cmd_buffer,
							 info->count,
							 !!info->strmout_buffer);
			} else {
				unsigned i;
				for_each_bit(i, state->subpass->view_mask) {
					radv_emit_view_index(cmd_buffer, i);

					radv_cs_emit_draw_packet(cmd_buffer,
								 info->count,
								 !!info->strmout_buffer);
				}
			}
		}
	}
}

/*
 * Vega and raven have a bug which triggers if there are multiple context
 * register contexts active at the same time with different scissor values.
 *
 * There are two possible workarounds:
 * 1) Wait for PS_PARTIAL_FLUSH every time the scissor is changed. That way
 *    there is only ever 1 active set of scissor values at the same time.
 *
 * 2) Whenever the hardware switches contexts we have to set the scissor
 *    registers again even if it is a noop. That way the new context gets
 *    the correct scissor values.
 *
 * This implements option 2. radv_need_late_scissor_emission needs to
 * return true on affected HW if radv_emit_all_graphics_states sets
 * any context registers.
 */
static bool radv_need_late_scissor_emission(struct radv_cmd_buffer *cmd_buffer,
                                            const struct radv_draw_info *info)
{
	struct radv_cmd_state *state = &cmd_buffer->state;

	if (!cmd_buffer->device->physical_device->rad_info.has_gfx9_scissor_bug)
		return false;

	if (cmd_buffer->state.context_roll_without_scissor_emitted || info->strmout_buffer)
		return true;

	uint32_t used_states = cmd_buffer->state.pipeline->graphics.needed_dynamic_state | ~RADV_CMD_DIRTY_DYNAMIC_ALL;

	/* Index, vertex and streamout buffers don't change context regs, and
	 * pipeline is already handled.
	 */
	used_states &= ~(RADV_CMD_DIRTY_INDEX_BUFFER |
			 RADV_CMD_DIRTY_VERTEX_BUFFER |
			 RADV_CMD_DIRTY_STREAMOUT_BUFFER |
			 RADV_CMD_DIRTY_PIPELINE);

	if (cmd_buffer->state.dirty & used_states)
		return true;

	uint32_t primitive_reset_index =
		radv_get_primitive_reset_index(cmd_buffer);

	if (info->indexed && state->pipeline->graphics.prim_restart_enable &&
	    primitive_reset_index != state->last_primitive_reset_index)
		return true;

	return false;
}

static void
radv_emit_all_graphics_states(struct radv_cmd_buffer *cmd_buffer,
			      const struct radv_draw_info *info)
{
	bool late_scissor_emission;

	if ((cmd_buffer->state.dirty & RADV_CMD_DIRTY_FRAMEBUFFER) ||
	    cmd_buffer->state.emitted_pipeline != cmd_buffer->state.pipeline)
		radv_emit_rbplus_state(cmd_buffer);

	if (cmd_buffer->state.dirty & RADV_CMD_DIRTY_PIPELINE)
		radv_emit_graphics_pipeline(cmd_buffer);

	/* This should be before the cmd_buffer->state.dirty is cleared
	 * (excluding RADV_CMD_DIRTY_PIPELINE) and after
	 * cmd_buffer->state.context_roll_without_scissor_emitted is set. */
	late_scissor_emission =
		radv_need_late_scissor_emission(cmd_buffer, info);

	if (cmd_buffer->state.dirty & RADV_CMD_DIRTY_FRAMEBUFFER)
		radv_emit_framebuffer_state(cmd_buffer);

	if (info->indexed) {
		if (cmd_buffer->state.dirty & RADV_CMD_DIRTY_INDEX_BUFFER)
			radv_emit_index_buffer(cmd_buffer, info->indirect);
	} else {
		/* On GFX7 and later, non-indexed draws overwrite VGT_INDEX_TYPE,
		 * so the state must be re-emitted before the next indexed
		 * draw.
		 */
		if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX7) {
			cmd_buffer->state.last_index_type = -1;
			cmd_buffer->state.dirty |= RADV_CMD_DIRTY_INDEX_BUFFER;
		}
	}

	radv_cmd_buffer_flush_dynamic_state(cmd_buffer);

	radv_emit_draw_registers(cmd_buffer, info);

	if (late_scissor_emission)
		radv_emit_scissor(cmd_buffer);
}

static void
radv_draw(struct radv_cmd_buffer *cmd_buffer,
	  const struct radv_draw_info *info)
{
	struct radeon_info *rad_info =
		&cmd_buffer->device->physical_device->rad_info;
	bool has_prefetch =
		cmd_buffer->device->physical_device->rad_info.chip_class >= GFX7;
	bool pipeline_is_dirty =
		(cmd_buffer->state.dirty & RADV_CMD_DIRTY_PIPELINE) &&
		cmd_buffer->state.pipeline != cmd_buffer->state.emitted_pipeline;

	ASSERTED unsigned cdw_max =
		radeon_check_space(cmd_buffer->device->ws,
				   cmd_buffer->cs, 4096);

	if (likely(!info->indirect)) {
		/* GFX6-GFX7 treat instance_count==0 as instance_count==1. There is
		 * no workaround for indirect draws, but we can at least skip
		 * direct draws.
		 */
		if (unlikely(!info->instance_count))
			return;

		/* Handle count == 0. */
		if (unlikely(!info->count && !info->strmout_buffer))
			return;
	}

	/* Need to apply this workaround early as it can set flush flags. */
	if (cmd_buffer->state.dirty & RADV_CMD_DIRTY_FRAMEBUFFER)
		radv_emit_fb_mip_change_flush(cmd_buffer);

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

	/* Workaround for a VGT hang when streamout is enabled.
	 * It must be done after drawing.
	 */
	if (cmd_buffer->state.streamout.streamout_enabled &&
	    (rad_info->family == CHIP_HAWAII ||
	     rad_info->family == CHIP_TONGA ||
	     rad_info->family == CHIP_FIJI)) {
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_VGT_STREAMOUT_SYNC;
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
	struct radv_draw_info info = {0};

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
	struct radv_draw_info info = {0};

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
	struct radv_draw_info info = {0};

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
	struct radv_draw_info info = {0};

	info.indexed = true;
	info.count = drawCount;
	info.indirect = buffer;
	info.indirect_offset = offset;
	info.stride = stride;

	radv_draw(cmd_buffer, &info);
}

void radv_CmdDrawIndirectCount(
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
	struct radv_draw_info info = {0};

	info.count = maxDrawCount;
	info.indirect = buffer;
	info.indirect_offset = offset;
	info.count_buffer = count_buffer;
	info.count_buffer_offset = countBufferOffset;
	info.stride = stride;

	radv_draw(cmd_buffer, &info);
}

void radv_CmdDrawIndexedIndirectCount(
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
	struct radv_draw_info info = {0};

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
	bool predicating = cmd_buffer->state.predicating;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	struct radv_userdata_info *loc;

	radv_describe_dispatch(cmd_buffer, info->blocks[0], info->blocks[1],
	                       info->blocks[2]);

	loc = radv_lookup_user_sgpr(pipeline, MESA_SHADER_COMPUTE,
				    AC_UD_CS_GRID_SIZE);

	ASSERTED unsigned cdw_max = radeon_check_space(ws, cs, 25);

	if (compute_shader->info.wave_size == 32) {
		assert(cmd_buffer->device->physical_device->rad_info.chip_class >= GFX10);
		dispatch_initiator |= S_00B800_CS_W32_EN(1);
	}

	if (info->indirect) {
		uint64_t va = radv_buffer_get_va(info->indirect->bo);

		va += info->indirect->offset + info->indirect_offset;

		radv_cs_add_buffer(ws, cs, info->indirect->bo);

		if (loc->sgpr_idx != -1) {
			for (unsigned i = 0; i < 3; ++i) {
				radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
				radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_SRC_MEM) |
						COPY_DATA_DST_SEL(COPY_DATA_REG));
				radeon_emit(cs, (va +  4 * i));
				radeon_emit(cs, (va + 4 * i) >> 32);
				radeon_emit(cs, ((R_00B900_COMPUTE_USER_DATA_0
						 + loc->sgpr_idx * 4) >> 2) + i);
				radeon_emit(cs, 0);
			}
		}

		if (radv_cmd_buffer_uses_mec(cmd_buffer)) {
			radeon_emit(cs, PKT3(PKT3_DISPATCH_INDIRECT, 2, predicating) |
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

			radeon_emit(cs, PKT3(PKT3_DISPATCH_INDIRECT, 1, predicating) |
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

		radeon_emit(cs, PKT3(PKT3_DISPATCH_DIRECT, 3, predicating) |
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
		cmd_buffer->device->physical_device->rad_info.chip_class >= GFX7;
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
	struct radv_dispatch_info info = {0};

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
	struct radv_dispatch_info info = {0};

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
	struct radv_dispatch_info info = {0};

	info.blocks[0] = x;
	info.blocks[1] = y;
	info.blocks[2] = z;
	info.unaligned = 1;

	radv_dispatch(cmd_buffer, &info);
}

void
radv_cmd_buffer_end_render_pass(struct radv_cmd_buffer *cmd_buffer)
{
	vk_free(&cmd_buffer->pool->alloc, cmd_buffer->state.attachments);
	vk_free(&cmd_buffer->pool->alloc, cmd_buffer->state.subpass_sample_locs);

	cmd_buffer->state.pass = NULL;
	cmd_buffer->state.subpass = NULL;
	cmd_buffer->state.attachments = NULL;
	cmd_buffer->state.framebuffer = NULL;
	cmd_buffer->state.subpass_sample_locs = NULL;
}

void radv_CmdEndRenderPass(
	VkCommandBuffer                             commandBuffer)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	radv_subpass_barrier(cmd_buffer, &cmd_buffer->state.pass->end_barrier);

	radv_cmd_buffer_end_subpass(cmd_buffer);

	radv_cmd_buffer_end_render_pass(cmd_buffer);
}

void radv_CmdEndRenderPass2(
    VkCommandBuffer                             commandBuffer,
    const VkSubpassEndInfo*                     pSubpassEndInfo)
{
	radv_CmdEndRenderPass(commandBuffer);
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
                                  const VkImageSubresourceRange *range)
{
	assert(range->baseMipLevel == 0);
	assert(range->levelCount == 1 || range->levelCount == VK_REMAINING_ARRAY_LAYERS);
	VkImageAspectFlags aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
	struct radv_cmd_state *state = &cmd_buffer->state;
	uint32_t htile_value = vk_format_is_stencil(image->vk_format) ? 0xfffff30f : 0xfffc000f;
	VkClearDepthStencilValue value = {0};
	struct radv_barrier_data barrier = {0};

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB |
			     RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;

	barrier.layout_transitions.init_mask_ram = 1;
	radv_describe_layout_transition(cmd_buffer, &barrier);

	state->flush_bits |= radv_clear_htile(cmd_buffer, image, range, htile_value);

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;

	if (vk_format_is_stencil(image->vk_format))
		aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;

	radv_set_ds_clear_metadata(cmd_buffer, image, range, value, aspects);

	if (radv_image_is_tc_compat_htile(image)) {
		/* Initialize the TC-compat metada value to 0 because by
		 * default DB_Z_INFO.RANGE_PRECISION is set to 1, and we only
		 * need have to conditionally update its value when performing
		 * a fast depth clear.
		 */
		radv_set_tc_compat_zrange_metadata(cmd_buffer, image, range, 0);
	}
}

static void radv_handle_depth_image_transition(struct radv_cmd_buffer *cmd_buffer,
					       struct radv_image *image,
					       VkImageLayout src_layout,
					       bool src_render_loop,
					       VkImageLayout dst_layout,
					       bool dst_render_loop,
					       unsigned src_queue_mask,
					       unsigned dst_queue_mask,
					       const VkImageSubresourceRange *range,
					       struct radv_sample_locations_state *sample_locs)
{
	if (!radv_image_has_htile(image))
		return;

	if (src_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
		radv_initialize_htile(cmd_buffer, image, range);
	} else if (!radv_layout_is_htile_compressed(image, src_layout, src_render_loop, src_queue_mask) &&
	           radv_layout_is_htile_compressed(image, dst_layout, dst_render_loop, dst_queue_mask)) {
		radv_initialize_htile(cmd_buffer, image, range);
	} else if (radv_layout_is_htile_compressed(image, src_layout, src_render_loop, src_queue_mask) &&
	           !radv_layout_is_htile_compressed(image, dst_layout, dst_render_loop, dst_queue_mask)) {
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB |
		                                RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;

		radv_decompress_depth_stencil(cmd_buffer, image, range,
					      sample_locs);

		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB |
		                                RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;
	}
}

static void radv_initialise_cmask(struct radv_cmd_buffer *cmd_buffer,
				  struct radv_image *image,
				  const VkImageSubresourceRange *range,
				  uint32_t value)
{
	struct radv_cmd_state *state = &cmd_buffer->state;
	struct radv_barrier_data barrier = {0};

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
			    RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;

	barrier.layout_transitions.init_mask_ram = 1;
	radv_describe_layout_transition(cmd_buffer, &barrier);

	state->flush_bits |= radv_clear_cmask(cmd_buffer, image, range, value);

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
}

void radv_initialize_fmask(struct radv_cmd_buffer *cmd_buffer,
			   struct radv_image *image,
			   const VkImageSubresourceRange *range)
{
	struct radv_cmd_state *state = &cmd_buffer->state;
	static const uint32_t fmask_clear_values[4] = {
		0x00000000,
		0x02020202,
		0xE4E4E4E4,
		0x76543210
	};
	uint32_t log2_samples = util_logbase2(image->info.samples);
	uint32_t value = fmask_clear_values[log2_samples];
	struct radv_barrier_data barrier = {0};

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
			     RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;

	barrier.layout_transitions.init_mask_ram = 1;
	radv_describe_layout_transition(cmd_buffer, &barrier);

	state->flush_bits |= radv_clear_fmask(cmd_buffer, image, range, value);

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
}

void radv_initialize_dcc(struct radv_cmd_buffer *cmd_buffer,
			 struct radv_image *image,
			 const VkImageSubresourceRange *range, uint32_t value)
{
	struct radv_cmd_state *state = &cmd_buffer->state;
	struct radv_barrier_data barrier = {0};
	unsigned size = 0;

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
			     RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;

	barrier.layout_transitions.init_mask_ram = 1;
	radv_describe_layout_transition(cmd_buffer, &barrier);

	state->flush_bits |= radv_clear_dcc(cmd_buffer, image, range, value);

	if (cmd_buffer->device->physical_device->rad_info.chip_class == GFX8) {
		/* When DCC is enabled with mipmaps, some levels might not
		 * support fast clears and we have to initialize them as "fully
		 * expanded".
		 */
		/* Compute the size of all fast clearable DCC levels. */
		for (unsigned i = 0; i < image->planes[0].surface.num_dcc_levels; i++) {
			struct legacy_surf_level *surf_level =
				&image->planes[0].surface.u.legacy.level[i];
			unsigned dcc_fast_clear_size =
				surf_level->dcc_slice_fast_clear_size * image->info.array_size;

			if (!dcc_fast_clear_size)
				break;

			size = surf_level->dcc_offset + dcc_fast_clear_size;
		}

		/* Initialize the mipmap levels without DCC. */
		if (size != image->planes[0].surface.dcc_size) {
			state->flush_bits |=
				radv_fill_buffer(cmd_buffer, image->bo,
						 image->offset + image->planes[0].surface.dcc_offset + size,
						 image->planes[0].surface.dcc_size - size,
						 0xffffffff);
		}
	}

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
			     RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
}

/**
 * Initialize DCC/FMASK/CMASK metadata for a color image.
 */
static void radv_init_color_image_metadata(struct radv_cmd_buffer *cmd_buffer,
					   struct radv_image *image,
					   VkImageLayout src_layout,
					   bool src_render_loop,
					   VkImageLayout dst_layout,
					   bool dst_render_loop,
					   unsigned src_queue_mask,
					   unsigned dst_queue_mask,
					   const VkImageSubresourceRange *range)
{
	if (radv_image_has_cmask(image)) {
		uint32_t value = 0xffffffffu; /* Fully expanded mode. */

		/*  TODO: clarify why 0xccccccccu is used. */

		/* If CMASK isn't updated with the new layout, we should use the
		 * fully expanded mode so that the image is read correctly if
		 * CMASK is used (such as when transitioning to a compressed
		 * layout).
		 */
		if (radv_image_has_fmask(image) &&
		    radv_layout_can_fast_clear(cmd_buffer->device, image, dst_layout,
					       dst_render_loop, dst_queue_mask)) {
			value = 0xccccccccu;
		}

		radv_initialise_cmask(cmd_buffer, image, range, value);
	}

	if (radv_image_has_fmask(image)) {
		radv_initialize_fmask(cmd_buffer, image, range);
	}

	if (radv_dcc_enabled(image, range->baseMipLevel)) {
		uint32_t value = 0xffffffffu; /* Fully expanded mode. */

		if (radv_layout_dcc_compressed(cmd_buffer->device, image, dst_layout,
					       dst_render_loop,
					       dst_queue_mask)) {
			value = 0u;
		}

		radv_initialize_dcc(cmd_buffer, image, range, value);

		radv_update_fce_metadata(cmd_buffer, image, range, false);
	}

	if (radv_image_has_cmask(image) ||
	    radv_dcc_enabled(image, range->baseMipLevel)) {
		uint32_t color_values[2] = {0};
		radv_set_color_clear_metadata(cmd_buffer, image, range,
					      color_values);
	}
}

/**
 * Handle color image transitions for DCC/FMASK/CMASK.
 */
static void radv_handle_color_image_transition(struct radv_cmd_buffer *cmd_buffer,
					       struct radv_image *image,
					       VkImageLayout src_layout,
					       bool src_render_loop,
					       VkImageLayout dst_layout,
					       bool dst_render_loop,
					       unsigned src_queue_mask,
					       unsigned dst_queue_mask,
					       const VkImageSubresourceRange *range)
{
	if (src_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
		radv_init_color_image_metadata(cmd_buffer, image,
					       src_layout, src_render_loop,
					       dst_layout, dst_render_loop,
					       src_queue_mask, dst_queue_mask,
					       range);
		return;
	}

	if (radv_dcc_enabled(image, range->baseMipLevel)) {
		if (src_layout == VK_IMAGE_LAYOUT_PREINITIALIZED) {
			radv_initialize_dcc(cmd_buffer, image, range, 0xffffffffu);
		} else if (radv_layout_dcc_compressed(cmd_buffer->device, image, src_layout, src_render_loop, src_queue_mask) &&
		           !radv_layout_dcc_compressed(cmd_buffer->device, image, dst_layout, dst_render_loop, dst_queue_mask)) {
			radv_decompress_dcc(cmd_buffer, image, range);
		} else if (radv_layout_can_fast_clear(cmd_buffer->device, image, src_layout,
		                                      src_render_loop, src_queue_mask) &&
		           !radv_layout_can_fast_clear(cmd_buffer->device, image, dst_layout,
		                                       dst_render_loop, dst_queue_mask)) {
			radv_fast_clear_flush_image_inplace(cmd_buffer, image, range);
		}
	} else if (radv_image_has_cmask(image) || radv_image_has_fmask(image)) {
		bool fce_eliminate = false, fmask_expand = false;

		if (radv_layout_can_fast_clear(cmd_buffer->device, image, src_layout,
		                               src_render_loop, src_queue_mask) &&
		    !radv_layout_can_fast_clear(cmd_buffer->device, image, dst_layout,
		                                dst_render_loop, dst_queue_mask)) {
			fce_eliminate = true;
		}

		if (radv_image_has_fmask(image) &&
		    (image->usage & (VK_IMAGE_USAGE_STORAGE_BIT |
				     VK_IMAGE_USAGE_TRANSFER_DST_BIT))) {
			if (src_layout != VK_IMAGE_LAYOUT_GENERAL &&
			    dst_layout == VK_IMAGE_LAYOUT_GENERAL) {
				/* A FMASK decompress is required before doing
				 * a MSAA decompress using FMASK.
				 */
				fmask_expand = true;
			}
		}

		if (fce_eliminate || fmask_expand)
			radv_fast_clear_flush_image_inplace(cmd_buffer, image, range);

		if (fmask_expand) {
			struct radv_barrier_data barrier = {0};
			barrier.layout_transitions.fmask_color_expand = 1;
			radv_describe_layout_transition(cmd_buffer, &barrier);

			radv_expand_fmask_image_inplace(cmd_buffer, image, range);
		}
	}
}

static void radv_handle_image_transition(struct radv_cmd_buffer *cmd_buffer,
					 struct radv_image *image,
					 VkImageLayout src_layout,
					 bool src_render_loop,
					 VkImageLayout dst_layout,
					 bool dst_render_loop,
					 uint32_t src_family,
					 uint32_t dst_family,
					 const VkImageSubresourceRange *range,
					 struct radv_sample_locations_state *sample_locs)
{
	if (image->exclusive && src_family != dst_family) {
		/* This is an acquire or a release operation and there will be
		 * a corresponding release/acquire. Do the transition in the
		 * most flexible queue. */

		assert(src_family == cmd_buffer->queue_family_index ||
		       dst_family == cmd_buffer->queue_family_index);

		if (src_family == VK_QUEUE_FAMILY_EXTERNAL ||
		    src_family == VK_QUEUE_FAMILY_FOREIGN_EXT)
			return;

		if (cmd_buffer->queue_family_index == RADV_QUEUE_TRANSFER)
			return;

		if (cmd_buffer->queue_family_index == RADV_QUEUE_COMPUTE &&
		    (src_family == RADV_QUEUE_GENERAL ||
		     dst_family == RADV_QUEUE_GENERAL))
			return;
	}

	if (src_layout == dst_layout && src_render_loop == dst_render_loop)
		return;

	unsigned src_queue_mask =
		radv_image_queue_family_mask(image, src_family,
					     cmd_buffer->queue_family_index);
	unsigned dst_queue_mask =
		radv_image_queue_family_mask(image, dst_family,
					     cmd_buffer->queue_family_index);

	if (vk_format_is_depth(image->vk_format)) {
		radv_handle_depth_image_transition(cmd_buffer, image,
						   src_layout, src_render_loop,
						   dst_layout, dst_render_loop,
						   src_queue_mask, dst_queue_mask,
						   range, sample_locs);
	} else {
		radv_handle_color_image_transition(cmd_buffer, image,
						   src_layout, src_render_loop,
						   dst_layout, dst_render_loop,
						   src_queue_mask, dst_queue_mask,
						   range);
	}
}

struct radv_barrier_info {
	enum rgp_barrier_reason reason;
	uint32_t eventCount;
	const VkEvent *pEvents;
	VkPipelineStageFlags srcStageMask;
	VkPipelineStageFlags dstStageMask;
};

static void
radv_barrier(struct radv_cmd_buffer *cmd_buffer,
	     uint32_t memoryBarrierCount,
	     const VkMemoryBarrier *pMemoryBarriers,
	     uint32_t bufferMemoryBarrierCount,
	     const VkBufferMemoryBarrier *pBufferMemoryBarriers,
	     uint32_t imageMemoryBarrierCount,
	     const VkImageMemoryBarrier *pImageMemoryBarriers,
	     const struct radv_barrier_info *info)
{
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	enum radv_cmd_flush_bits src_flush_bits = 0;
	enum radv_cmd_flush_bits dst_flush_bits = 0;

	radv_describe_barrier_start(cmd_buffer, info->reason);

	for (unsigned i = 0; i < info->eventCount; ++i) {
		RADV_FROM_HANDLE(radv_event, event, info->pEvents[i]);
		uint64_t va = radv_buffer_get_va(event->bo);

		radv_cs_add_buffer(cmd_buffer->device->ws, cs, event->bo);

		ASSERTED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cs, 7);

		radv_cp_wait_mem(cs, WAIT_REG_MEM_EQUAL, va, 1, 0xffffffff);
		assert(cmd_buffer->cs->cdw <= cdw_max);
	}

	for (uint32_t i = 0; i < memoryBarrierCount; i++) {
		src_flush_bits |= radv_src_access_flush(cmd_buffer, pMemoryBarriers[i].srcAccessMask,
							NULL);
		dst_flush_bits |= radv_dst_access_flush(cmd_buffer, pMemoryBarriers[i].dstAccessMask,
		                                        NULL);
	}

	for (uint32_t i = 0; i < bufferMemoryBarrierCount; i++) {
		src_flush_bits |= radv_src_access_flush(cmd_buffer, pBufferMemoryBarriers[i].srcAccessMask,
							NULL);
		dst_flush_bits |= radv_dst_access_flush(cmd_buffer, pBufferMemoryBarriers[i].dstAccessMask,
		                                        NULL);
	}

	for (uint32_t i = 0; i < imageMemoryBarrierCount; i++) {
		RADV_FROM_HANDLE(radv_image, image, pImageMemoryBarriers[i].image);

		src_flush_bits |= radv_src_access_flush(cmd_buffer, pImageMemoryBarriers[i].srcAccessMask,
							image);
		dst_flush_bits |= radv_dst_access_flush(cmd_buffer, pImageMemoryBarriers[i].dstAccessMask,
		                                        image);
	}

	/* The Vulkan spec 1.1.98 says:
	 *
	 * "An execution dependency with only
	 *  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT in the destination stage mask
	 *  will only prevent that stage from executing in subsequently
	 *  submitted commands. As this stage does not perform any actual
	 *  execution, this is not observable - in effect, it does not delay
	 *  processing of subsequent commands. Similarly an execution dependency
	 *  with only VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT in the source stage mask
	 *  will effectively not wait for any prior commands to complete."
	 */
	if (info->dstStageMask != VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
		radv_stage_flush(cmd_buffer, info->srcStageMask);
	cmd_buffer->state.flush_bits |= src_flush_bits;

	for (uint32_t i = 0; i < imageMemoryBarrierCount; i++) {
		RADV_FROM_HANDLE(radv_image, image, pImageMemoryBarriers[i].image);

		const struct VkSampleLocationsInfoEXT *sample_locs_info =
			vk_find_struct_const(pImageMemoryBarriers[i].pNext,
					     SAMPLE_LOCATIONS_INFO_EXT);
		struct radv_sample_locations_state sample_locations = {0};

		if (sample_locs_info) {
			assert(image->flags & VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT);
			sample_locations.per_pixel = sample_locs_info->sampleLocationsPerPixel;
			sample_locations.grid_size = sample_locs_info->sampleLocationGridSize;
			sample_locations.count = sample_locs_info->sampleLocationsCount;
			typed_memcpy(&sample_locations.locations[0],
				     sample_locs_info->pSampleLocations,
				     sample_locs_info->sampleLocationsCount);
		}

		radv_handle_image_transition(cmd_buffer, image,
					     pImageMemoryBarriers[i].oldLayout,
					     false, /* Outside of a renderpass we are never in a renderloop */
					     pImageMemoryBarriers[i].newLayout,
					     false, /* Outside of a renderpass we are never in a renderloop */
					     pImageMemoryBarriers[i].srcQueueFamilyIndex,
					     pImageMemoryBarriers[i].dstQueueFamilyIndex,
					     &pImageMemoryBarriers[i].subresourceRange,
					     sample_locs_info ? &sample_locations : NULL);
	}

	/* Make sure CP DMA is idle because the driver might have performed a
	 * DMA operation for copying or filling buffers/images.
	 */
	if (info->srcStageMask & (VK_PIPELINE_STAGE_TRANSFER_BIT |
				  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT))
		si_cp_dma_wait_for_idle(cmd_buffer);

	cmd_buffer->state.flush_bits |= dst_flush_bits;

	radv_describe_barrier_end(cmd_buffer);
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
	struct radv_barrier_info info;

	info.reason = RGP_BARRIER_EXTERNAL_CMD_PIPELINE_BARRIER;
	info.eventCount = 0;
	info.pEvents = NULL;
	info.srcStageMask = srcStageMask;
	info.dstStageMask = destStageMask;

	radv_barrier(cmd_buffer, memoryBarrierCount, pMemoryBarriers,
		     bufferMemoryBarrierCount, pBufferMemoryBarriers,
		     imageMemoryBarrierCount, pImageMemoryBarriers, &info);
}


static void write_event(struct radv_cmd_buffer *cmd_buffer,
			struct radv_event *event,
			VkPipelineStageFlags stageMask,
			unsigned value)
{
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint64_t va = radv_buffer_get_va(event->bo);

	si_emit_cache_flush(cmd_buffer);

	radv_cs_add_buffer(cmd_buffer->device->ws, cs, event->bo);

	ASSERTED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cs, 28);

	/* Flags that only require a top-of-pipe event. */
	VkPipelineStageFlags top_of_pipe_flags =
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

	/* Flags that only require a post-index-fetch event. */
	VkPipelineStageFlags post_index_fetch_flags =
		top_of_pipe_flags |
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;

	/* Make sure CP DMA is idle because the driver might have performed a
	 * DMA operation for copying or filling buffers/images.
	 */
	if (stageMask & (VK_PIPELINE_STAGE_TRANSFER_BIT |
			 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT))
		si_cp_dma_wait_for_idle(cmd_buffer);

	/* TODO: Emit EOS events for syncing PS/CS stages. */

	if (!(stageMask & ~top_of_pipe_flags)) {
		/* Just need to sync the PFP engine. */
		radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 3, 0));
		radeon_emit(cs, S_370_DST_SEL(V_370_MEM) |
				S_370_WR_CONFIRM(1) |
				S_370_ENGINE_SEL(V_370_PFP));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		radeon_emit(cs, value);
	} else if (!(stageMask & ~post_index_fetch_flags)) {
		/* Sync ME because PFP reads index and indirect buffers. */
		radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 3, 0));
		radeon_emit(cs, S_370_DST_SEL(V_370_MEM) |
				S_370_WR_CONFIRM(1) |
				S_370_ENGINE_SEL(V_370_ME));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		radeon_emit(cs, value);
	} else {
		/* Otherwise, sync all prior GPU work using an EOP event. */
		si_cs_emit_write_event_eop(cs,
					   cmd_buffer->device->physical_device->rad_info.chip_class,
					   radv_cmd_buffer_uses_mec(cmd_buffer),
					   V_028A90_BOTTOM_OF_PIPE_TS, 0,
					   EOP_DST_SEL_MEM,
					   EOP_DATA_SEL_VALUE_32BIT, va, value,
					   cmd_buffer->gfx9_eop_bug_va);
	}

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
	struct radv_barrier_info info;

	info.reason = RGP_BARRIER_EXTERNAL_CMD_WAIT_EVENTS;
	info.eventCount = eventCount;
	info.pEvents = pEvents;
	info.srcStageMask = 0;

	radv_barrier(cmd_buffer, memoryBarrierCount, pMemoryBarriers,
		     bufferMemoryBarrierCount, pBufferMemoryBarriers,
		     imageMemoryBarrierCount, pImageMemoryBarriers, &info);
}


void radv_CmdSetDeviceMask(VkCommandBuffer commandBuffer,
                           uint32_t deviceMask)
{
   /* No-op */
}

/* VK_EXT_conditional_rendering */
void radv_CmdBeginConditionalRenderingEXT(
	VkCommandBuffer                             commandBuffer,
	const VkConditionalRenderingBeginInfoEXT*   pConditionalRenderingBegin)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, buffer, pConditionalRenderingBegin->buffer);
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	unsigned pred_op = PREDICATION_OP_BOOL32;
	bool draw_visible = true;
	uint64_t va;

	va = radv_buffer_get_va(buffer->bo) + pConditionalRenderingBegin->offset;

	/* By default, if the 32-bit value at offset in buffer memory is zero,
	 * then the rendering commands are discarded, otherwise they are
	 * executed as normal. If the inverted flag is set, all commands are
	 * discarded if the value is non zero.
	 */
	if (pConditionalRenderingBegin->flags &
	    VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT) {
		draw_visible = false;
	}

	si_emit_cache_flush(cmd_buffer);

	if (cmd_buffer->queue_family_index == RADV_QUEUE_GENERAL &&
	    !cmd_buffer->device->physical_device->rad_info.has_32bit_predication) {
		uint64_t pred_value = 0, pred_va;
		unsigned pred_offset;

		/* From the Vulkan spec 1.1.107:
		 *
		 * "If the 32-bit value at offset in buffer memory is zero,
		 *  then the rendering commands are discarded, otherwise they
		 *  are executed as normal. If the value of the predicate in
		 *  buffer memory changes while conditional rendering is
		 *  active, the rendering commands may be discarded in an
		 *  implementation-dependent way. Some implementations may
		 *  latch the value of the predicate upon beginning conditional
		 *  rendering while others may read it before every rendering
		 *  command."
		 *
		 * But, the AMD hardware treats the predicate as a 64-bit
		 * value which means we need a workaround in the driver.
		 * Luckily, it's not required to support if the value changes
		 * when predication is active.
		 *
		 * The workaround is as follows:
		 * 1) allocate a 64-value in the upload BO and initialize it
		 *    to 0
		 * 2) copy the 32-bit predicate value to the upload BO
		 * 3) use the new allocated VA address for predication
		 *
		 * Based on the conditionalrender demo, it's faster to do the
		 * COPY_DATA in ME  (+ sync PFP) instead of PFP.
		 */
		radv_cmd_buffer_upload_data(cmd_buffer, 8, 16, &pred_value, &pred_offset);

		pred_va = radv_buffer_get_va(cmd_buffer->upload.upload_bo) + pred_offset;

		radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
		radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_SRC_MEM) |
				COPY_DATA_DST_SEL(COPY_DATA_DST_MEM) |
				COPY_DATA_WR_CONFIRM);
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		radeon_emit(cs, pred_va);
		radeon_emit(cs, pred_va >> 32);

		radeon_emit(cs, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
		radeon_emit(cs, 0);

		va = pred_va;
		pred_op = PREDICATION_OP_BOOL64;
	}

	/* Enable predication for this command buffer. */
	si_emit_set_predication_state(cmd_buffer, draw_visible, pred_op, va);
	cmd_buffer->state.predicating = true;

	/* Store conditional rendering user info. */
	cmd_buffer->state.predication_type = draw_visible;
	cmd_buffer->state.predication_op = pred_op;
	cmd_buffer->state.predication_va = va;
}

void radv_CmdEndConditionalRenderingEXT(
	VkCommandBuffer                             commandBuffer)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	/* Disable predication for this command buffer. */
	si_emit_set_predication_state(cmd_buffer, false, 0, 0);
	cmd_buffer->state.predicating = false;

	/* Reset conditional rendering user info. */
	cmd_buffer->state.predication_type = -1;
	cmd_buffer->state.predication_op = 0;
	cmd_buffer->state.predication_va = 0;
}

/* VK_EXT_transform_feedback */
void radv_CmdBindTransformFeedbackBuffersEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets,
    const VkDeviceSize*                         pSizes)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_streamout_binding *sb = cmd_buffer->streamout_bindings;
	uint8_t enabled_mask = 0;

	assert(firstBinding + bindingCount <= MAX_SO_BUFFERS);
	for (uint32_t i = 0; i < bindingCount; i++) {
		uint32_t idx = firstBinding + i;

		sb[idx].buffer = radv_buffer_from_handle(pBuffers[i]);
		sb[idx].offset = pOffsets[i];

		if (!pSizes || pSizes[i] == VK_WHOLE_SIZE) {
			sb[idx].size = sb[idx].buffer->size - sb[idx].offset;
		} else {
			sb[idx].size = pSizes[i];
		}

		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs,
				   sb[idx].buffer->bo);

		enabled_mask |= 1 << idx;
	}

	cmd_buffer->state.streamout.enabled_mask |= enabled_mask;

	cmd_buffer->state.dirty |= RADV_CMD_DIRTY_STREAMOUT_BUFFER;
}

static void
radv_emit_streamout_enable(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_streamout_state *so = &cmd_buffer->state.streamout;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;

	radeon_set_context_reg_seq(cs, R_028B94_VGT_STRMOUT_CONFIG, 2);
	radeon_emit(cs,
		    S_028B94_STREAMOUT_0_EN(so->streamout_enabled) |
		    S_028B94_RAST_STREAM(0) |
		    S_028B94_STREAMOUT_1_EN(so->streamout_enabled) |
		    S_028B94_STREAMOUT_2_EN(so->streamout_enabled) |
		    S_028B94_STREAMOUT_3_EN(so->streamout_enabled));
	radeon_emit(cs, so->hw_enabled_mask &
			so->enabled_stream_buffers_mask);

	cmd_buffer->state.context_roll_without_scissor_emitted = true;
}

static void
radv_set_streamout_enable(struct radv_cmd_buffer *cmd_buffer, bool enable)
{
	struct radv_streamout_state *so = &cmd_buffer->state.streamout;
	bool old_streamout_enabled = so->streamout_enabled;
	uint32_t old_hw_enabled_mask = so->hw_enabled_mask;

	so->streamout_enabled = enable;

	so->hw_enabled_mask = so->enabled_mask |
			      (so->enabled_mask << 4) |
			      (so->enabled_mask << 8) |
			      (so->enabled_mask << 12);

	if (!cmd_buffer->device->physical_device->use_ngg_streamout &&
	    ((old_streamout_enabled != so->streamout_enabled) ||
	     (old_hw_enabled_mask != so->hw_enabled_mask)))
		radv_emit_streamout_enable(cmd_buffer);

	if (cmd_buffer->device->physical_device->use_ngg_streamout) {
		cmd_buffer->gds_needed = true;
		cmd_buffer->gds_oa_needed = true;
	}
}

static void radv_flush_vgt_streamout(struct radv_cmd_buffer *cmd_buffer)
{
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	unsigned reg_strmout_cntl;

	/* The register is at different places on different ASICs. */
	if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX7) {
		reg_strmout_cntl = R_0300FC_CP_STRMOUT_CNTL;
		radeon_set_uconfig_reg(cs, reg_strmout_cntl, 0);
	} else {
		reg_strmout_cntl = R_0084FC_CP_STRMOUT_CNTL;
		radeon_set_config_reg(cs, reg_strmout_cntl, 0);
	}

	radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
	radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_SO_VGTSTREAMOUT_FLUSH) | EVENT_INDEX(0));

	radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
	radeon_emit(cs, WAIT_REG_MEM_EQUAL); /* wait until the register is equal to the reference value */
	radeon_emit(cs, reg_strmout_cntl >> 2);  /* register */
	radeon_emit(cs, 0);
	radeon_emit(cs, S_0084FC_OFFSET_UPDATE_DONE(1)); /* reference value */
	radeon_emit(cs, S_0084FC_OFFSET_UPDATE_DONE(1)); /* mask */
	radeon_emit(cs, 4); /* poll interval */
}

static void
radv_emit_streamout_begin(struct radv_cmd_buffer *cmd_buffer,
			  uint32_t firstCounterBuffer,
			  uint32_t counterBufferCount,
			  const VkBuffer *pCounterBuffers,
			  const VkDeviceSize *pCounterBufferOffsets)

{
	struct radv_streamout_binding *sb = cmd_buffer->streamout_bindings;
	struct radv_streamout_state *so = &cmd_buffer->state.streamout;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint32_t i;

	radv_flush_vgt_streamout(cmd_buffer);

	assert(firstCounterBuffer + counterBufferCount <= MAX_SO_BUFFERS);
	for_each_bit(i, so->enabled_mask) {
		int32_t counter_buffer_idx = i - firstCounterBuffer;
		if (counter_buffer_idx >= 0 && counter_buffer_idx >= counterBufferCount)
			counter_buffer_idx = -1;

		/* AMD GCN binds streamout buffers as shader resources.
		 * VGT only counts primitives and tells the shader through
		 * SGPRs what to do.
		 */
		radeon_set_context_reg_seq(cs, R_028AD0_VGT_STRMOUT_BUFFER_SIZE_0 + 16*i, 2);
		radeon_emit(cs, sb[i].size >> 2);	/* BUFFER_SIZE (in DW) */
		radeon_emit(cs, so->stride_in_dw[i]);			/* VTX_STRIDE (in DW) */

		cmd_buffer->state.context_roll_without_scissor_emitted = true;

		if (counter_buffer_idx >= 0 && pCounterBuffers && pCounterBuffers[counter_buffer_idx]) {
			/* The array of counter buffers is optional. */
			RADV_FROM_HANDLE(radv_buffer, buffer, pCounterBuffers[counter_buffer_idx]);
			uint64_t va = radv_buffer_get_va(buffer->bo);
			uint64_t counter_buffer_offset = 0;

			if (pCounterBufferOffsets)
				counter_buffer_offset = pCounterBufferOffsets[counter_buffer_idx];

			va += buffer->offset + counter_buffer_offset;

			/* Append */
			radeon_emit(cs, PKT3(PKT3_STRMOUT_BUFFER_UPDATE, 4, 0));
			radeon_emit(cs, STRMOUT_SELECT_BUFFER(i) |
					STRMOUT_DATA_TYPE(1) | /* offset in bytes */
					STRMOUT_OFFSET_SOURCE(STRMOUT_OFFSET_FROM_MEM)); /* control */
			radeon_emit(cs, 0); /* unused */
			radeon_emit(cs, 0); /* unused */
			radeon_emit(cs, va); /* src address lo */
			radeon_emit(cs, va >> 32); /* src address hi */

			radv_cs_add_buffer(cmd_buffer->device->ws, cs, buffer->bo);
		} else {
			/* Start from the beginning. */
			radeon_emit(cs, PKT3(PKT3_STRMOUT_BUFFER_UPDATE, 4, 0));
			radeon_emit(cs, STRMOUT_SELECT_BUFFER(i) |
					STRMOUT_DATA_TYPE(1) | /* offset in bytes */
					STRMOUT_OFFSET_SOURCE(STRMOUT_OFFSET_FROM_PACKET)); /* control */
			radeon_emit(cs, 0); /* unused */
			radeon_emit(cs, 0); /* unused */
			radeon_emit(cs, 0); /* unused */
			radeon_emit(cs, 0); /* unused */
		}
	}

	radv_set_streamout_enable(cmd_buffer, true);
}

static void
gfx10_emit_streamout_begin(struct radv_cmd_buffer *cmd_buffer,
			   uint32_t firstCounterBuffer,
			   uint32_t counterBufferCount,
			   const VkBuffer *pCounterBuffers,
			   const VkDeviceSize *pCounterBufferOffsets)
{
	struct radv_streamout_state *so = &cmd_buffer->state.streamout;
	unsigned last_target = util_last_bit(so->enabled_mask) - 1;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint32_t i;

	assert(cmd_buffer->device->physical_device->rad_info.chip_class >= GFX10);
	assert(firstCounterBuffer + counterBufferCount <= MAX_SO_BUFFERS);

	/* Sync because the next streamout operation will overwrite GDS and we
	 * have to make sure it's idle.
	 * TODO: Improve by tracking if there is a streamout operation in
	 * flight.
	 */
	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_VS_PARTIAL_FLUSH;
	si_emit_cache_flush(cmd_buffer);

	for_each_bit(i, so->enabled_mask) {
		int32_t counter_buffer_idx = i - firstCounterBuffer;
		if (counter_buffer_idx >= 0 && counter_buffer_idx >= counterBufferCount)
			counter_buffer_idx = -1;

		bool append = counter_buffer_idx >= 0 &&
			      pCounterBuffers && pCounterBuffers[counter_buffer_idx];
		uint64_t va = 0;

		if (append) {
			RADV_FROM_HANDLE(radv_buffer, buffer, pCounterBuffers[counter_buffer_idx]);
			uint64_t counter_buffer_offset = 0;

			if (pCounterBufferOffsets)
				counter_buffer_offset = pCounterBufferOffsets[counter_buffer_idx];

			va += radv_buffer_get_va(buffer->bo);
			va += buffer->offset + counter_buffer_offset;

			radv_cs_add_buffer(cmd_buffer->device->ws, cs, buffer->bo);
		}

		radeon_emit(cs, PKT3(PKT3_DMA_DATA, 5, 0));
		radeon_emit(cs, S_411_SRC_SEL(append ? V_411_SRC_ADDR_TC_L2 : V_411_DATA) |
				S_411_DST_SEL(V_411_GDS) |
				S_411_CP_SYNC(i == last_target));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		radeon_emit(cs, 4 * i); /* destination in GDS */
		radeon_emit(cs, 0);
		radeon_emit(cs, S_414_BYTE_COUNT_GFX9(4) |
				S_414_DISABLE_WR_CONFIRM_GFX9(i != last_target));
	}

	radv_set_streamout_enable(cmd_buffer, true);
}

void radv_CmdBeginTransformFeedbackEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstCounterBuffer,
    uint32_t                                    counterBufferCount,
    const VkBuffer*                             pCounterBuffers,
    const VkDeviceSize*                         pCounterBufferOffsets)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	if (cmd_buffer->device->physical_device->use_ngg_streamout) {
		gfx10_emit_streamout_begin(cmd_buffer,
					   firstCounterBuffer, counterBufferCount,
					   pCounterBuffers, pCounterBufferOffsets);
	} else {
		radv_emit_streamout_begin(cmd_buffer,
					  firstCounterBuffer, counterBufferCount,
					  pCounterBuffers, pCounterBufferOffsets);
	}
}

static void
radv_emit_streamout_end(struct radv_cmd_buffer *cmd_buffer,
			uint32_t firstCounterBuffer,
			uint32_t counterBufferCount,
			const VkBuffer *pCounterBuffers,
			const VkDeviceSize *pCounterBufferOffsets)
{
	struct radv_streamout_state *so = &cmd_buffer->state.streamout;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint32_t i;

	radv_flush_vgt_streamout(cmd_buffer);

	assert(firstCounterBuffer + counterBufferCount <= MAX_SO_BUFFERS);
	for_each_bit(i, so->enabled_mask) {
		int32_t counter_buffer_idx = i - firstCounterBuffer;
		if (counter_buffer_idx >= 0 && counter_buffer_idx >= counterBufferCount)
			counter_buffer_idx = -1;

		if (counter_buffer_idx >= 0 && pCounterBuffers && pCounterBuffers[counter_buffer_idx]) {
			/* The array of counters buffer is optional. */
			RADV_FROM_HANDLE(radv_buffer, buffer, pCounterBuffers[counter_buffer_idx]);
			uint64_t va = radv_buffer_get_va(buffer->bo);
			uint64_t counter_buffer_offset = 0;

			if (pCounterBufferOffsets)
				counter_buffer_offset = pCounterBufferOffsets[counter_buffer_idx];

			va += buffer->offset + counter_buffer_offset;

			radeon_emit(cs, PKT3(PKT3_STRMOUT_BUFFER_UPDATE, 4, 0));
			radeon_emit(cs, STRMOUT_SELECT_BUFFER(i) |
					STRMOUT_DATA_TYPE(1) | /* offset in bytes */
					STRMOUT_OFFSET_SOURCE(STRMOUT_OFFSET_NONE) |
					STRMOUT_STORE_BUFFER_FILLED_SIZE); /* control */
			radeon_emit(cs, va);		/* dst address lo */
			radeon_emit(cs, va >> 32);	/* dst address hi */
			radeon_emit(cs, 0);		/* unused */
			radeon_emit(cs, 0);		/* unused */

			radv_cs_add_buffer(cmd_buffer->device->ws, cs, buffer->bo);
		}

		/* Deactivate transform feedback by zeroing the buffer size.
		 * The counters (primitives generated, primitives emitted) may
		 * be enabled even if there is not buffer bound. This ensures
		 * that the primitives-emitted query won't increment.
		 */
		radeon_set_context_reg(cs, R_028AD0_VGT_STRMOUT_BUFFER_SIZE_0 + 16*i, 0);

		cmd_buffer->state.context_roll_without_scissor_emitted = true;
	}

	radv_set_streamout_enable(cmd_buffer, false);
}

static void
gfx10_emit_streamout_end(struct radv_cmd_buffer *cmd_buffer,
			 uint32_t firstCounterBuffer,
			 uint32_t counterBufferCount,
			 const VkBuffer *pCounterBuffers,
			 const VkDeviceSize *pCounterBufferOffsets)
{
	struct radv_streamout_state *so = &cmd_buffer->state.streamout;
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint32_t i;

	assert(cmd_buffer->device->physical_device->rad_info.chip_class >= GFX10);
	assert(firstCounterBuffer + counterBufferCount <= MAX_SO_BUFFERS);

	for_each_bit(i, so->enabled_mask) {
		int32_t counter_buffer_idx = i - firstCounterBuffer;
		if (counter_buffer_idx >= 0 && counter_buffer_idx >= counterBufferCount)
			counter_buffer_idx = -1;

		if (counter_buffer_idx >= 0 && pCounterBuffers && pCounterBuffers[counter_buffer_idx]) {
			/* The array of counters buffer is optional. */
			RADV_FROM_HANDLE(radv_buffer, buffer, pCounterBuffers[counter_buffer_idx]);
			uint64_t va = radv_buffer_get_va(buffer->bo);
			uint64_t counter_buffer_offset = 0;

			if (pCounterBufferOffsets)
				counter_buffer_offset = pCounterBufferOffsets[counter_buffer_idx];

			va += buffer->offset + counter_buffer_offset;

			si_cs_emit_write_event_eop(cs,
						   cmd_buffer->device->physical_device->rad_info.chip_class,
						   radv_cmd_buffer_uses_mec(cmd_buffer),
						   V_028A90_PS_DONE, 0,
						   EOP_DST_SEL_TC_L2,
						   EOP_DATA_SEL_GDS,
						   va, EOP_DATA_GDS(i, 1), 0);

			radv_cs_add_buffer(cmd_buffer->device->ws, cs, buffer->bo);
		}
	}

	radv_set_streamout_enable(cmd_buffer, false);
}

void radv_CmdEndTransformFeedbackEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstCounterBuffer,
    uint32_t                                    counterBufferCount,
    const VkBuffer*                             pCounterBuffers,
    const VkDeviceSize*                         pCounterBufferOffsets)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

	if (cmd_buffer->device->physical_device->use_ngg_streamout) {
		gfx10_emit_streamout_end(cmd_buffer,
					 firstCounterBuffer, counterBufferCount,
					 pCounterBuffers, pCounterBufferOffsets);
	} else {
		radv_emit_streamout_end(cmd_buffer,
					firstCounterBuffer, counterBufferCount,
					pCounterBuffers, pCounterBufferOffsets);
	}
}

void radv_CmdDrawIndirectByteCountEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    VkBuffer                                    _counterBuffer,
    VkDeviceSize                                counterBufferOffset,
    uint32_t                                    counterOffset,
    uint32_t                                    vertexStride)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, counterBuffer, _counterBuffer);
	struct radv_draw_info info = {0};

	info.instance_count = instanceCount;
	info.first_instance = firstInstance;
	info.strmout_buffer = counterBuffer;
	info.strmout_buffer_offset = counterBufferOffset;
	info.stride = vertexStride;

	radv_draw(cmd_buffer, &info);
}

/* VK_AMD_buffer_marker */
void radv_CmdWriteBufferMarkerAMD(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlagBits                     pipelineStage,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    uint32_t                                    marker)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, buffer, dstBuffer);
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint64_t va = radv_buffer_get_va(buffer->bo) + dstOffset;

	si_emit_cache_flush(cmd_buffer);

	ASSERTED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 12);

	if (!(pipelineStage & ~VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)) {
		radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
		radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_IMM) |
				COPY_DATA_DST_SEL(COPY_DATA_DST_MEM) |
				COPY_DATA_WR_CONFIRM);
		radeon_emit(cs, marker);
		radeon_emit(cs, 0);
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
	} else {
		si_cs_emit_write_event_eop(cs,
					   cmd_buffer->device->physical_device->rad_info.chip_class,
					   radv_cmd_buffer_uses_mec(cmd_buffer),
					   V_028A90_BOTTOM_OF_PIPE_TS, 0,
					   EOP_DST_SEL_MEM,
					   EOP_DATA_SEL_VALUE_32BIT,
					   va, marker,
					   cmd_buffer->gfx9_eop_bug_va);
	}

	assert(cmd_buffer->cs->cdw <= cdw_max);
}
