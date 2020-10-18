/*
 * Copyright © 2020 Valve Corporation
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

/**
 * Identifiers for RGP SQ thread-tracing markers (Table 1)
 */
enum rgp_sqtt_marker_identifier {
	RGP_SQTT_MARKER_IDENTIFIER_EVENT		= 0x0,
	RGP_SQTT_MARKER_IDENTIFIER_CB_START             = 0x1,
	RGP_SQTT_MARKER_IDENTIFIER_CB_END               = 0x2,
	RGP_SQTT_MARKER_IDENTIFIER_BARRIER_START        = 0x3,
	RGP_SQTT_MARKER_IDENTIFIER_BARRIER_END          = 0x4,
	RGP_SQTT_MARKER_IDENTIFIER_USER_EVENT           = 0x5,
	RGP_SQTT_MARKER_IDENTIFIER_GENERAL_API          = 0x6,
	RGP_SQTT_MARKER_IDENTIFIER_SYNC			= 0x7,
	RGP_SQTT_MARKER_IDENTIFIER_PRESENT		= 0x8,
	RGP_SQTT_MARKER_IDENTIFIER_LAYOUT_TRANSITION    = 0x9,
	RGP_SQTT_MARKER_IDENTIFIER_RENDER_PASS          = 0xA,
	RGP_SQTT_MARKER_IDENTIFIER_RESERVED2		= 0xB,
	RGP_SQTT_MARKER_IDENTIFIER_BIND_PIPELINE        = 0xC,
	RGP_SQTT_MARKER_IDENTIFIER_RESERVED4		= 0xD,
	RGP_SQTT_MARKER_IDENTIFIER_RESERVED5		= 0xE,
	RGP_SQTT_MARKER_IDENTIFIER_RESERVED6		= 0xF
};

/**
 * RGP SQ thread-tracing marker for the start of a command buffer. (Table 2)
 */
struct rgp_sqtt_marker_cb_start {
	union {
		struct {
			uint32_t identifier : 4;
			uint32_t ext_dwords : 3;
			uint32_t cb_id : 20;
			uint32_t queue : 5;
		};
		uint32_t dword01;
	};
	union {
		uint32_t device_id_low;
		uint32_t dword02;
	};
	union {
		uint32_t device_id_high;
		uint32_t dword03;
	};
	union {
		uint32_t queue_flags;
		uint32_t dword04;
	};
};

static_assert(sizeof(struct rgp_sqtt_marker_cb_start) == 16,
	      "rgp_sqtt_marker_cb_start doesn't match RGP spec");

/**
 *
 * RGP SQ thread-tracing marker for the end of a command buffer. (Table 3)
 */
struct rgp_sqtt_marker_cb_end {
	union {
		struct {
			uint32_t identifier : 4;
			uint32_t ext_dwords : 3;
			uint32_t cb_id : 20;
			uint32_t reserved : 5;
		};
		uint32_t dword01;
	};
	union {
		uint32_t device_id_low;
		uint32_t dword02;
	};
	union {
		uint32_t device_id_high;
		uint32_t dword03;
	};
};

static_assert(sizeof(struct rgp_sqtt_marker_cb_end) == 12,
	      "rgp_sqtt_marker_cb_end doesn't match RGP spec");

/**
 * API types used in RGP SQ thread-tracing markers for the "General API"
 * packet.
 */
enum rgp_sqtt_marker_general_api_type {
	ApiCmdBindPipeline                     = 0,
	ApiCmdBindDescriptorSets               = 1,
	ApiCmdBindIndexBuffer                  = 2,
	ApiCmdBindVertexBuffers                = 3,
	ApiCmdDraw                             = 4,
	ApiCmdDrawIndexed                      = 5,
	ApiCmdDrawIndirect                     = 6,
	ApiCmdDrawIndexedIndirect              = 7,
	ApiCmdDrawIndirectCountAMD             = 8,
	ApiCmdDrawIndexedIndirectCountAMD      = 9,
	ApiCmdDispatch                         = 10,
	ApiCmdDispatchIndirect                 = 11,
	ApiCmdCopyBuffer                       = 12,
	ApiCmdCopyImage                        = 13,
	ApiCmdBlitImage                        = 14,
	ApiCmdCopyBufferToImage                = 15,
	ApiCmdCopyImageToBuffer                = 16,
	ApiCmdUpdateBuffer                     = 17,
	ApiCmdFillBuffer                       = 18,
	ApiCmdClearColorImage                  = 19,
	ApiCmdClearDepthStencilImage           = 20,
	ApiCmdClearAttachments                 = 21,
	ApiCmdResolveImage                     = 22,
	ApiCmdWaitEvents                       = 23,
	ApiCmdPipelineBarrier                  = 24,
	ApiCmdBeginQuery                       = 25,
	ApiCmdEndQuery                         = 26,
	ApiCmdResetQueryPool                   = 27,
	ApiCmdWriteTimestamp                   = 28,
	ApiCmdCopyQueryPoolResults             = 29,
	ApiCmdPushConstants                    = 30,
	ApiCmdBeginRenderPass                  = 31,
	ApiCmdNextSubpass                      = 32,
	ApiCmdEndRenderPass                    = 33,
	ApiCmdExecuteCommands                  = 34,
	ApiCmdSetViewport                      = 35,
	ApiCmdSetScissor                       = 36,
	ApiCmdSetLineWidth                     = 37,
	ApiCmdSetDepthBias                     = 38,
	ApiCmdSetBlendConstants                = 39,
	ApiCmdSetDepthBounds                   = 40,
	ApiCmdSetStencilCompareMask            = 41,
	ApiCmdSetStencilWriteMask              = 42,
	ApiCmdSetStencilReference              = 43,
	ApiCmdDrawIndirectCount                = 44,
	ApiCmdDrawIndexedIndirectCount         = 45,
	ApiInvalid                             = 0xffffffff
};

/**
 * RGP SQ thread-tracing marker for a "General API" instrumentation packet.
 */
struct rgp_sqtt_marker_general_api {
	union {
		struct {
			uint32_t identifier : 4;
			uint32_t ext_dwords : 3;
			uint32_t api_type : 20;
			uint32_t is_end : 1;
			uint32_t reserved : 4;
		};
		uint32_t dword01;
	};
};

static_assert(sizeof(struct rgp_sqtt_marker_general_api) == 4,
	      "rgp_sqtt_marker_general_api doesn't match RGP spec");

/**
 * API types used in RGP SQ thread-tracing markers (Table 16).
 */
enum rgp_sqtt_marker_event_type {
	EventCmdDraw                             = 0,
	EventCmdDrawIndexed                      = 1,
	EventCmdDrawIndirect                     = 2,
	EventCmdDrawIndexedIndirect              = 3,
	EventCmdDrawIndirectCountAMD             = 4,
	EventCmdDrawIndexedIndirectCountAMD      = 5,
	EventCmdDispatch                         = 6,
	EventCmdDispatchIndirect                 = 7,
	EventCmdCopyBuffer                       = 8,
	EventCmdCopyImage                        = 9,
	EventCmdBlitImage                        = 10,
	EventCmdCopyBufferToImage                = 11,
	EventCmdCopyImageToBuffer                = 12,
	EventCmdUpdateBuffer                     = 13,
	EventCmdFillBuffer                       = 14,
	EventCmdClearColorImage                  = 15,
	EventCmdClearDepthStencilImage           = 16,
	EventCmdClearAttachments                 = 17,
	EventCmdResolveImage                     = 18,
	EventCmdWaitEvents                       = 19,
	EventCmdPipelineBarrier                  = 20,
	EventCmdResetQueryPool                   = 21,
	EventCmdCopyQueryPoolResults             = 22,
	EventRenderPassColorClear                = 23,
	EventRenderPassDepthStencilClear         = 24,
	EventRenderPassResolve                   = 25,
	EventInternalUnknown                     = 26,
	EventCmdDrawIndirectCount                = 27,
	EventCmdDrawIndexedIndirectCount         = 28,
	EventInvalid                             = 0xffffffff
};

/**
 * "Event (Per-draw/dispatch)" RGP SQ thread-tracing marker. (Table 4)
 */
struct rgp_sqtt_marker_event {
	union {
		struct {
			uint32_t identifier : 4;
			uint32_t ext_dwords : 3;
			uint32_t api_type : 24;
			uint32_t has_thread_dims : 1;
		};
		uint32_t dword01;
	};
	union {
		struct {
			uint32_t cb_id : 20;
			uint32_t vertex_offset_reg_idx : 4;
			uint32_t instance_offset_reg_idx : 4;
			uint32_t draw_index_reg_idx : 4;
		};
		uint32_t dword02;
	};
	union {
		uint32_t cmd_id;
		uint32_t dword03;
	};
};

static_assert(sizeof(struct rgp_sqtt_marker_event) == 12,
	      "rgp_sqtt_marker_event doesn't match RGP spec");

/**
 * Per-dispatch specific marker where workgroup dims are included.
 */
struct rgp_sqtt_marker_event_with_dims {
	struct rgp_sqtt_marker_event event;
	uint32_t thread_x;
	uint32_t thread_y;
	uint32_t thread_z;
};

static_assert(sizeof(struct rgp_sqtt_marker_event_with_dims) == 24,
	      "rgp_sqtt_marker_event_with_dims doesn't match RGP spec");

/**
 * "Barrier Start" RGP SQTT instrumentation marker (Table 5)
 */
struct rgp_sqtt_marker_barrier_start {
	union {
		struct {
			uint32_t identifier : 4;
			uint32_t ext_dwords : 3;
			uint32_t cb_id : 20;
			uint32_t reserved : 5;
		};
		uint32_t dword01;
	};
	union {
		struct {
			uint32_t driver_reason : 31;
			uint32_t internal : 1;
		};
		uint32_t dword02;
	};
};

static_assert(sizeof(struct rgp_sqtt_marker_barrier_start) == 8,
	      "rgp_sqtt_marker_barrier_start doesn't match RGP spec");

/**
 * "Barrier End" RGP SQTT instrumentation marker (Table 6)
 */
struct rgp_sqtt_marker_barrier_end {
	union {
		struct {
			uint32_t identifier : 4;
			uint32_t ext_dwords : 3;
			uint32_t cb_id : 20;
			uint32_t wait_on_eop_ts : 1;
			uint32_t vs_partial_flush : 1;
			uint32_t ps_partial_flush : 1;
			uint32_t cs_partial_flush : 1;
			uint32_t pfp_sync_me : 1;
		};
		uint32_t dword01;
	};
	union {
		struct {
			uint32_t sync_cp_dma : 1;
			uint32_t inval_tcp : 1;
			uint32_t inval_sqI : 1;
			uint32_t inval_sqK : 1;
			uint32_t flush_tcc : 1;
			uint32_t inval_tcc : 1;
			uint32_t flush_cb : 1;
			uint32_t inval_cb : 1;
			uint32_t flush_db : 1;
			uint32_t inval_db : 1;
			uint32_t num_layout_transitions : 16;
			uint32_t inval_gl1 : 1;
			uint32_t reserved : 5;
		};
		uint32_t dword02;
	};
};

static_assert(sizeof(struct rgp_sqtt_marker_barrier_end) == 8,
	      "rgp_sqtt_marker_barrier_end doesn't match RGP spec");

/**
 * "Layout Transition" RGP SQTT instrumentation marker (Table 7)
 */
struct rgp_sqtt_marker_layout_transition {
	union {
		struct {
			uint32_t identifier : 4;
			uint32_t ext_dwords : 3;
			uint32_t depth_stencil_expand : 1;
			uint32_t htile_hiz_range_expand : 1;
			uint32_t depth_stencil_resummarize : 1;
			uint32_t dcc_decompress : 1;
			uint32_t fmask_decompress : 1;
			uint32_t fast_clear_eliminate : 1;
			uint32_t fmask_color_expand : 1;
			uint32_t init_mask_ram : 1;
			uint32_t reserved1 : 17;
		};
		uint32_t dword01;
	};
	union {
		struct {
			uint32_t reserved2 : 32;
		};
		uint32_t dword02;
	};
};

static_assert(sizeof(struct rgp_sqtt_marker_layout_transition) == 8,
	      "rgp_sqtt_marker_layout_transition doesn't match RGP spec");

static void
radv_write_begin_general_api_marker(struct radv_cmd_buffer *cmd_buffer,
				    enum rgp_sqtt_marker_general_api_type api_type)
{
	struct rgp_sqtt_marker_general_api marker = {0};
	struct radeon_cmdbuf *cs = cmd_buffer->cs;

	marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_GENERAL_API;
	marker.api_type = api_type;

	radv_emit_thread_trace_userdata(cmd_buffer->device, cs, &marker, sizeof(marker) / 4);
}

static void
radv_write_end_general_api_marker(struct radv_cmd_buffer *cmd_buffer,
				  enum rgp_sqtt_marker_general_api_type api_type)
{
	struct rgp_sqtt_marker_general_api marker = {0};
	struct radeon_cmdbuf *cs = cmd_buffer->cs;

	marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_GENERAL_API;
	marker.api_type = api_type;
	marker.is_end = 1;

	radv_emit_thread_trace_userdata(cmd_buffer->device, cs, &marker, sizeof(marker) / 4);
}

static void
radv_write_event_marker(struct radv_cmd_buffer *cmd_buffer,
			enum rgp_sqtt_marker_event_type api_type,
			uint32_t vertex_offset_user_data,
			uint32_t instance_offset_user_data,
			uint32_t draw_index_user_data)
{
	struct rgp_sqtt_marker_event marker = {0};
	struct radeon_cmdbuf *cs = cmd_buffer->cs;

	marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_EVENT;
	marker.api_type = api_type;
	marker.cmd_id = cmd_buffer->state.num_events++;
	marker.cb_id = 0;

	if (vertex_offset_user_data == UINT_MAX ||
	    instance_offset_user_data == UINT_MAX) {
		vertex_offset_user_data = 0;
		instance_offset_user_data = 0;
	}

	if (draw_index_user_data == UINT_MAX)
		draw_index_user_data = vertex_offset_user_data;

	marker.vertex_offset_reg_idx = vertex_offset_user_data;
	marker.instance_offset_reg_idx = instance_offset_user_data;
	marker.draw_index_reg_idx = draw_index_user_data;

	radv_emit_thread_trace_userdata(cmd_buffer->device, cs, &marker, sizeof(marker) / 4);
}

static void
radv_write_event_with_dims_marker(struct radv_cmd_buffer *cmd_buffer,
				  enum rgp_sqtt_marker_event_type api_type,
				  uint32_t x, uint32_t y, uint32_t z)
{
	struct rgp_sqtt_marker_event_with_dims marker = {0};
	struct radeon_cmdbuf *cs = cmd_buffer->cs;

	marker.event.identifier = RGP_SQTT_MARKER_IDENTIFIER_EVENT;
	marker.event.api_type = api_type;
	marker.event.cmd_id = cmd_buffer->state.num_events++;
	marker.event.cb_id = 0;
	marker.event.has_thread_dims = 1;

	marker.thread_x = x;
	marker.thread_y = y;
	marker.thread_z = z;

	radv_emit_thread_trace_userdata(cmd_buffer->device, cs, &marker, sizeof(marker) / 4);
}

void
radv_describe_begin_cmd_buffer(struct radv_cmd_buffer *cmd_buffer)
{
	uint64_t device_id = (uintptr_t)cmd_buffer->device;
	struct rgp_sqtt_marker_cb_start marker = {0};
	struct radeon_cmdbuf *cs = cmd_buffer->cs;

	if (likely(!cmd_buffer->device->thread_trace_bo))
		return;

	marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_CB_START;
	marker.cb_id = 0;
	marker.device_id_low = device_id;
	marker.device_id_high = device_id >> 32;
	marker.queue = cmd_buffer->queue_family_index;
	marker.queue_flags = VK_QUEUE_COMPUTE_BIT |
			     VK_QUEUE_TRANSFER_BIT |
			     VK_QUEUE_SPARSE_BINDING_BIT;

	if (cmd_buffer->queue_family_index == RADV_QUEUE_GENERAL)
		marker.queue_flags |= VK_QUEUE_GRAPHICS_BIT;

	radv_emit_thread_trace_userdata(cmd_buffer->device, cs, &marker, sizeof(marker) / 4);
}

void
radv_describe_end_cmd_buffer(struct radv_cmd_buffer *cmd_buffer)
{
	uint64_t device_id = (uintptr_t)cmd_buffer->device;
	struct rgp_sqtt_marker_cb_end marker = {0};
	struct radeon_cmdbuf *cs = cmd_buffer->cs;

	if (likely(!cmd_buffer->device->thread_trace_bo))
		return;

	marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_CB_END;
	marker.cb_id = 0;
	marker.device_id_low = device_id;
	marker.device_id_high = device_id >> 32;

	radv_emit_thread_trace_userdata(cmd_buffer->device, cs, &marker, sizeof(marker) / 4);
}

void
radv_describe_draw(struct radv_cmd_buffer *cmd_buffer)
{
	if (likely(!cmd_buffer->device->thread_trace_bo))
		return;

	radv_write_event_marker(cmd_buffer, cmd_buffer->state.current_event_type,
				UINT_MAX, UINT_MAX, UINT_MAX);
}

void
radv_describe_dispatch(struct radv_cmd_buffer *cmd_buffer, int x, int y, int z)
{
	if (likely(!cmd_buffer->device->thread_trace_bo))
		return;

	radv_write_event_with_dims_marker(cmd_buffer,
					  cmd_buffer->state.current_event_type,
					  x, y, z);
}

void
radv_describe_begin_render_pass_clear(struct radv_cmd_buffer *cmd_buffer,
				      VkImageAspectFlagBits aspects)
{
	cmd_buffer->state.current_event_type = (aspects & VK_IMAGE_ASPECT_COLOR_BIT) ?
		EventRenderPassColorClear : EventRenderPassDepthStencilClear;
}

void
radv_describe_end_render_pass_clear(struct radv_cmd_buffer *cmd_buffer)
{
	cmd_buffer->state.current_event_type = EventInternalUnknown;
}

void
radv_describe_barrier_end_delayed(struct radv_cmd_buffer *cmd_buffer)
{
	struct rgp_sqtt_marker_barrier_end marker = {0};
	struct radeon_cmdbuf *cs = cmd_buffer->cs;

	if (likely(!cmd_buffer->device->thread_trace_bo) ||
	    !cmd_buffer->state.pending_sqtt_barrier_end)
		return;

	cmd_buffer->state.pending_sqtt_barrier_end = false;

	marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_BARRIER_END;
	marker.cb_id = 0;

	marker.num_layout_transitions = cmd_buffer->state.num_layout_transitions;

	/* TODO: fill pipeline stalls, cache flushes, etc */
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_WAIT_ON_EOP_TS)
		marker.wait_on_eop_ts = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_VS_PARTIAL_FLUSH)
		marker.vs_partial_flush = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_PS_PARTIAL_FLUSH)
		marker.ps_partial_flush = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_CS_PARTIAL_FLUSH)
		marker.cs_partial_flush = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_PFP_SYNC_ME)
		marker.pfp_sync_me = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_SYNC_CP_DMA)
		marker.sync_cp_dma = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_INVAL_VMEM_L0)
		marker.inval_tcp = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_INVAL_ICACHE)
		marker.inval_sqI = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_INVAL_SMEM_L0)
		marker.inval_sqK = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_FLUSH_L2)
		marker.flush_tcc = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_INVAL_L2)
		marker.inval_tcc = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_FLUSH_CB)
		marker.flush_cb = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_INVAL_CB)
		marker.inval_cb = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_FLUSH_DB)
		marker.flush_db = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_INVAL_DB)
		marker.inval_db = true;
	if (cmd_buffer->state.sqtt_flush_bits & RGP_FLUSH_INVAL_L1)
		marker.inval_gl1 = true;

	radv_emit_thread_trace_userdata(cmd_buffer->device, cs, &marker, sizeof(marker) / 4);

	cmd_buffer->state.num_layout_transitions = 0;
}

void
radv_describe_barrier_start(struct radv_cmd_buffer *cmd_buffer,
			   enum rgp_barrier_reason reason)
{
	struct rgp_sqtt_marker_barrier_start marker = {0};
	struct radeon_cmdbuf *cs = cmd_buffer->cs;

	if (likely(!cmd_buffer->device->thread_trace_bo))
		return;

	radv_describe_barrier_end_delayed(cmd_buffer);
	cmd_buffer->state.sqtt_flush_bits = 0;

	marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_BARRIER_START;
	marker.cb_id = 0;
	marker.dword02 = reason;

	radv_emit_thread_trace_userdata(cmd_buffer->device, cs, &marker, sizeof(marker) / 4);
}

void
radv_describe_barrier_end(struct radv_cmd_buffer *cmd_buffer)
{
	cmd_buffer->state.pending_sqtt_barrier_end = true;
}

void
radv_describe_layout_transition(struct radv_cmd_buffer *cmd_buffer,
				const struct radv_barrier_data *barrier)
{
	struct rgp_sqtt_marker_layout_transition marker = {0};
	struct radeon_cmdbuf *cs = cmd_buffer->cs;

	if (likely(!cmd_buffer->device->thread_trace_bo))
		return;

	marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_LAYOUT_TRANSITION;
	marker.depth_stencil_expand = barrier->layout_transitions.depth_stencil_expand;
	marker.htile_hiz_range_expand = barrier->layout_transitions.htile_hiz_range_expand;
	marker.depth_stencil_resummarize = barrier->layout_transitions.depth_stencil_resummarize;
	marker.dcc_decompress = barrier->layout_transitions.dcc_decompress;
	marker.fmask_decompress = barrier->layout_transitions.fmask_decompress;
	marker.fast_clear_eliminate = barrier->layout_transitions.fast_clear_eliminate;
	marker.fmask_color_expand = barrier->layout_transitions.fmask_color_expand;
	marker.init_mask_ram = barrier->layout_transitions.init_mask_ram;

	radv_emit_thread_trace_userdata(cmd_buffer->device, cs, &marker, sizeof(marker) / 4);

	cmd_buffer->state.num_layout_transitions++;
}

/* TODO: Improve the way to trigger capture (overlay, etc). */
static void
radv_handle_thread_trace(VkQueue _queue)
{
	RADV_FROM_HANDLE(radv_queue, queue, _queue);
	static bool thread_trace_enabled = false;
	static uint64_t num_frames = 0;

	if (thread_trace_enabled) {
		struct radv_thread_trace thread_trace = {0};

		radv_end_thread_trace(queue);
		thread_trace_enabled = false;

		/* TODO: Do something better than this whole sync. */
		radv_QueueWaitIdle(_queue);

		if (radv_get_thread_trace(queue, &thread_trace))
			radv_dump_thread_trace(queue->device, &thread_trace);
	} else {
		bool frame_trigger = num_frames == queue->device->thread_trace_start_frame;
		bool file_trigger = false;
		if (queue->device->thread_trace_trigger_file &&
		    access(queue->device->thread_trace_trigger_file, W_OK) == 0) {
			if (unlink(queue->device->thread_trace_trigger_file) == 0) {
				file_trigger = true;
			} else {
				/* Do not enable tracing if we cannot remove the file,
				 * because by then we'll trace every frame ... */
				fprintf(stderr, "RADV: could not remove thread trace trigger file, ignoring\n");
			}
		}

		if (frame_trigger || file_trigger) {
			radv_begin_thread_trace(queue);
			assert(!thread_trace_enabled);
			thread_trace_enabled = true;
		}
	}
	num_frames++;
}

VkResult sqtt_QueuePresentKHR(
	VkQueue                                  _queue,
	const VkPresentInfoKHR*                  pPresentInfo)
{
	VkResult result;

	result = radv_QueuePresentKHR(_queue, pPresentInfo);
	if (result != VK_SUCCESS)
		return result;

	radv_handle_thread_trace(_queue);

	return VK_SUCCESS;
}

#define EVENT_MARKER(cmd_name, args...) \
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer); \
	radv_write_begin_general_api_marker(cmd_buffer, ApiCmd##cmd_name); \
	cmd_buffer->state.current_event_type = EventCmd##cmd_name; \
	radv_Cmd##cmd_name(args); \
	cmd_buffer->state.current_event_type = EventInternalUnknown; \
	radv_write_end_general_api_marker(cmd_buffer, ApiCmd##cmd_name);

void sqtt_CmdDraw(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    vertexCount,
	uint32_t                                    instanceCount,
	uint32_t                                    firstVertex,
	uint32_t                                    firstInstance)
{
	EVENT_MARKER(Draw, commandBuffer, vertexCount, instanceCount,
		     firstVertex, firstInstance);
}

void sqtt_CmdDrawIndexed(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    indexCount,
	uint32_t                                    instanceCount,
	uint32_t                                    firstIndex,
	int32_t                                     vertexOffset,
	uint32_t                                    firstInstance)
{
	EVENT_MARKER(DrawIndexed, commandBuffer, indexCount, instanceCount,
		     firstIndex, vertexOffset, firstInstance);
}

void sqtt_CmdDrawIndirect(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    buffer,
	VkDeviceSize                                offset,
	uint32_t                                    drawCount,
	uint32_t                                    stride)
{
	EVENT_MARKER(DrawIndirect, commandBuffer, buffer, offset, drawCount,
		     stride);
}

void sqtt_CmdDrawIndexedIndirect(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    buffer,
	VkDeviceSize                                offset,
	uint32_t                                    drawCount,
	uint32_t                                    stride)
{
	EVENT_MARKER(DrawIndexedIndirect, commandBuffer, buffer, offset,
		     drawCount, stride);
}

void sqtt_CmdDrawIndirectCount(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    buffer,
	VkDeviceSize                                offset,
	VkBuffer                                    countBuffer,
	VkDeviceSize                                countBufferOffset,
	uint32_t                                    maxDrawCount,
	uint32_t                                    stride)
{
	EVENT_MARKER(DrawIndirectCount,commandBuffer, buffer, offset,
		     countBuffer, countBufferOffset, maxDrawCount, stride);
}

void sqtt_CmdDrawIndexedIndirectCount(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    buffer,
	VkDeviceSize                                offset,
	VkBuffer                                    countBuffer,
	VkDeviceSize                                countBufferOffset,
	uint32_t                                    maxDrawCount,
	uint32_t                                    stride)
{
	EVENT_MARKER(DrawIndexedIndirectCount, commandBuffer, buffer, offset,
		     countBuffer, countBufferOffset, maxDrawCount, stride);
}

void sqtt_CmdDispatch(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    x,
	uint32_t                                    y,
	uint32_t                                    z)
{
	EVENT_MARKER(Dispatch, commandBuffer, x, y, z);
}

void sqtt_CmdDispatchIndirect(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    buffer,
	VkDeviceSize                                offset)
{
	EVENT_MARKER(DispatchIndirect, commandBuffer, buffer, offset);
}

void sqtt_CmdCopyBuffer(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    srcBuffer,
	VkBuffer                                    destBuffer,
	uint32_t                                    regionCount,
	const VkBufferCopy*                         pRegions)
{
	EVENT_MARKER(CopyBuffer, commandBuffer, srcBuffer, destBuffer,
		     regionCount, pRegions);
}

void sqtt_CmdFillBuffer(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    dstBuffer,
	VkDeviceSize                                dstOffset,
	VkDeviceSize                                fillSize,
	uint32_t                                    data)
{
	EVENT_MARKER(FillBuffer, commandBuffer, dstBuffer, dstOffset, fillSize,
		     data);
}

void sqtt_CmdUpdateBuffer(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    dstBuffer,
	VkDeviceSize                                dstOffset,
	VkDeviceSize                                dataSize,
	const void*                                 pData)
{
	EVENT_MARKER(UpdateBuffer, commandBuffer, dstBuffer, dstOffset,
		     dataSize, pData);
}

void sqtt_CmdCopyImage(
	VkCommandBuffer                             commandBuffer,
	VkImage                                     srcImage,
	VkImageLayout                               srcImageLayout,
	VkImage                                     destImage,
	VkImageLayout                               destImageLayout,
	uint32_t                                    regionCount,
	const VkImageCopy*                          pRegions)
{
	EVENT_MARKER(CopyImage, commandBuffer, srcImage, srcImageLayout,
		     destImage, destImageLayout, regionCount, pRegions);
}

void sqtt_CmdCopyBufferToImage(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    srcBuffer,
	VkImage                                     destImage,
	VkImageLayout                               destImageLayout,
	uint32_t                                    regionCount,
	const VkBufferImageCopy*                    pRegions)
{
	EVENT_MARKER(CopyBufferToImage, commandBuffer, srcBuffer, destImage,
		     destImageLayout, regionCount, pRegions);
}

void sqtt_CmdCopyImageToBuffer(
	VkCommandBuffer                             commandBuffer,
	VkImage                                     srcImage,
	VkImageLayout                               srcImageLayout,
	VkBuffer                                    destBuffer,
	uint32_t                                    regionCount,
	const VkBufferImageCopy*                    pRegions)
{
	EVENT_MARKER(CopyImageToBuffer, commandBuffer, srcImage, srcImageLayout,
		     destBuffer, regionCount, pRegions);
}

void sqtt_CmdBlitImage(
	VkCommandBuffer                             commandBuffer,
	VkImage                                     srcImage,
	VkImageLayout                               srcImageLayout,
	VkImage                                     destImage,
	VkImageLayout                               destImageLayout,
	uint32_t                                    regionCount,
	const VkImageBlit*                          pRegions,
	VkFilter                                    filter)
{
	EVENT_MARKER(BlitImage, commandBuffer, srcImage, srcImageLayout,
		     destImage, destImageLayout, regionCount, pRegions, filter);
}

void sqtt_CmdClearColorImage(
	VkCommandBuffer                             commandBuffer,
	VkImage                                     image_h,
	VkImageLayout                               imageLayout,
	const VkClearColorValue*                    pColor,
	uint32_t                                    rangeCount,
	const VkImageSubresourceRange*              pRanges)
{
	EVENT_MARKER(ClearColorImage, commandBuffer, image_h, imageLayout,
		     pColor, rangeCount, pRanges);
}

void sqtt_CmdClearDepthStencilImage(
	VkCommandBuffer                             commandBuffer,
	VkImage                                     image_h,
	VkImageLayout                               imageLayout,
	const VkClearDepthStencilValue*             pDepthStencil,
	uint32_t                                    rangeCount,
	const VkImageSubresourceRange*              pRanges)
{
	EVENT_MARKER(ClearDepthStencilImage, commandBuffer, image_h,
		     imageLayout, pDepthStencil, rangeCount, pRanges);
}

void sqtt_CmdClearAttachments(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    attachmentCount,
	const VkClearAttachment*                    pAttachments,
	uint32_t                                    rectCount,
	const VkClearRect*                          pRects)
{
	EVENT_MARKER(ClearAttachments, commandBuffer, attachmentCount,
		     pAttachments, rectCount, pRects);
}

void sqtt_CmdResolveImage(
	VkCommandBuffer                             commandBuffer,
	VkImage                                     src_image_h,
	VkImageLayout                               src_image_layout,
	VkImage                                     dest_image_h,
	VkImageLayout                               dest_image_layout,
	uint32_t                                    region_count,
	const VkImageResolve*                       regions)
{
	EVENT_MARKER(ResolveImage, commandBuffer, src_image_h, src_image_layout,
		     dest_image_h, dest_image_layout, region_count, regions);
}

void sqtt_CmdWaitEvents(VkCommandBuffer commandBuffer,
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
	EVENT_MARKER(WaitEvents, commandBuffer, eventCount, pEvents,
		     srcStageMask, dstStageMask, memoryBarrierCount,
		     pMemoryBarriers, bufferMemoryBarrierCount,
		     pBufferMemoryBarriers, imageMemoryBarrierCount,
		     pImageMemoryBarriers);
}

void sqtt_CmdPipelineBarrier(
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
	EVENT_MARKER(PipelineBarrier, commandBuffer, srcStageMask,
		     destStageMask, byRegion, memoryBarrierCount,
		     pMemoryBarriers, bufferMemoryBarrierCount,
		     pBufferMemoryBarriers, imageMemoryBarrierCount,
		     pImageMemoryBarriers);
}

void sqtt_CmdResetQueryPool(
	VkCommandBuffer                             commandBuffer,
	VkQueryPool                                 queryPool,
	uint32_t                                    firstQuery,
	uint32_t                                    queryCount)
{
	EVENT_MARKER(ResetQueryPool, commandBuffer, queryPool, firstQuery,
		     queryCount);
}

void sqtt_CmdCopyQueryPoolResults(
	VkCommandBuffer                             commandBuffer,
	VkQueryPool                                 queryPool,
	uint32_t                                    firstQuery,
	uint32_t                                    queryCount,
	VkBuffer                                    dstBuffer,
	VkDeviceSize                                dstOffset,
	VkDeviceSize                                stride,
	VkQueryResultFlags                          flags)
{
	EVENT_MARKER(CopyQueryPoolResults, commandBuffer, queryPool, firstQuery,
				     queryCount, dstBuffer, dstOffset, stride,
				     flags);
}

#undef EVENT_MARKER
#define API_MARKER(cmd_name, args...) \
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer); \
	radv_write_begin_general_api_marker(cmd_buffer, ApiCmd##cmd_name); \
	radv_Cmd##cmd_name(args); \
	radv_write_end_general_api_marker(cmd_buffer, ApiCmd##cmd_name);

void sqtt_CmdBindPipeline(
	VkCommandBuffer                             commandBuffer,
	VkPipelineBindPoint                         pipelineBindPoint,
	VkPipeline                                  pipeline)
{
	API_MARKER(BindPipeline, commandBuffer, pipelineBindPoint, pipeline);
}

void sqtt_CmdBindDescriptorSets(
	VkCommandBuffer                             commandBuffer,
	VkPipelineBindPoint                         pipelineBindPoint,
	VkPipelineLayout                            layout,
	uint32_t                                    firstSet,
	uint32_t                                    descriptorSetCount,
	const VkDescriptorSet*                      pDescriptorSets,
	uint32_t                                    dynamicOffsetCount,
	const uint32_t*                             pDynamicOffsets)
{
	API_MARKER(BindDescriptorSets, commandBuffer, pipelineBindPoint,
		   layout, firstSet, descriptorSetCount,
		   pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}

void sqtt_CmdBindIndexBuffer(
	VkCommandBuffer                             commandBuffer,
	VkBuffer				    buffer,
	VkDeviceSize				    offset,
	VkIndexType				    indexType)
{
	API_MARKER(BindIndexBuffer, commandBuffer, buffer, offset, indexType);
}

void sqtt_CmdBindVertexBuffers(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    firstBinding,
	uint32_t                                    bindingCount,
	const VkBuffer*                             pBuffers,
	const VkDeviceSize*                         pOffsets)
{
	API_MARKER(BindVertexBuffers, commandBuffer, firstBinding, bindingCount,
		   pBuffers, pOffsets);
}

void sqtt_CmdBeginQuery(
	VkCommandBuffer                             commandBuffer,
	VkQueryPool                                 queryPool,
	uint32_t                                    query,
	VkQueryControlFlags                         flags)
{
	API_MARKER(BeginQuery, commandBuffer, queryPool, query, flags);
}

void sqtt_CmdEndQuery(
	VkCommandBuffer                             commandBuffer,
	VkQueryPool                                 queryPool,
	uint32_t                                    query)
{
	API_MARKER(EndQuery, commandBuffer, queryPool, query);
}

void sqtt_CmdWriteTimestamp(
	VkCommandBuffer                             commandBuffer,
	VkPipelineStageFlagBits                     pipelineStage,
	VkQueryPool                                 queryPool,
	uint32_t				    flags)
{
	API_MARKER(WriteTimestamp, commandBuffer, pipelineStage, queryPool, flags);
}

void sqtt_CmdPushConstants(
	VkCommandBuffer				    commandBuffer,
	VkPipelineLayout			    layout,
	VkShaderStageFlags			    stageFlags,
	uint32_t				    offset,
	uint32_t				    size,
	const void*				    pValues)
{
	API_MARKER(PushConstants, commandBuffer, layout, stageFlags, offset,
		   size, pValues);
}

void sqtt_CmdBeginRenderPass(
	VkCommandBuffer                             commandBuffer,
	const VkRenderPassBeginInfo*                pRenderPassBegin,
	VkSubpassContents                           contents)
{
	API_MARKER(BeginRenderPass, commandBuffer, pRenderPassBegin, contents);
}

void sqtt_CmdNextSubpass(
	VkCommandBuffer                             commandBuffer,
	VkSubpassContents                           contents)
{
	API_MARKER(NextSubpass, commandBuffer, contents);
}

void sqtt_CmdEndRenderPass(
	VkCommandBuffer                             commandBuffer)
{
	API_MARKER(EndRenderPass, commandBuffer);
}

void sqtt_CmdExecuteCommands(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    commandBufferCount,
	const VkCommandBuffer*                      pCmdBuffers)
{
	API_MARKER(ExecuteCommands, commandBuffer, commandBufferCount,
		   pCmdBuffers);
}

void sqtt_CmdSetViewport(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    firstViewport,
	uint32_t                                    viewportCount,
	const VkViewport*                           pViewports)
{
	API_MARKER(SetViewport, commandBuffer, firstViewport, viewportCount,
		   pViewports);
}

void sqtt_CmdSetScissor(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    firstScissor,
	uint32_t                                    scissorCount,
	const VkRect2D*                             pScissors)
{
	API_MARKER(SetScissor, commandBuffer, firstScissor, scissorCount,
		   pScissors);
}

void sqtt_CmdSetLineWidth(
	VkCommandBuffer                             commandBuffer,
	float                                       lineWidth)
{
	API_MARKER(SetLineWidth, commandBuffer, lineWidth);
}

void sqtt_CmdSetDepthBias(
	VkCommandBuffer                             commandBuffer,
	float                                       depthBiasConstantFactor,
	float                                       depthBiasClamp,
	float                                       depthBiasSlopeFactor)
{
	API_MARKER(SetDepthBias, commandBuffer, depthBiasConstantFactor,
		   depthBiasClamp, depthBiasSlopeFactor);
}

void sqtt_CmdSetBlendConstants(
	VkCommandBuffer                             commandBuffer,
	const float                                 blendConstants[4])
{
	API_MARKER(SetBlendConstants, commandBuffer, blendConstants);
}

void sqtt_CmdSetDepthBounds(
	VkCommandBuffer                             commandBuffer,
	float                                       minDepthBounds,
	float                                       maxDepthBounds)
{
	API_MARKER(SetDepthBounds, commandBuffer, minDepthBounds,
		   maxDepthBounds);
}

void sqtt_CmdSetStencilCompareMask(
	VkCommandBuffer                             commandBuffer,
	VkStencilFaceFlags                          faceMask,
	uint32_t                                    compareMask)
{
	API_MARKER(SetStencilCompareMask, commandBuffer, faceMask, compareMask);
}

void sqtt_CmdSetStencilWriteMask(
	VkCommandBuffer                             commandBuffer,
	VkStencilFaceFlags                          faceMask,
	uint32_t                                    writeMask)
{
	API_MARKER(SetStencilWriteMask, commandBuffer, faceMask, writeMask);
}

void sqtt_CmdSetStencilReference(
	VkCommandBuffer                             commandBuffer,
	VkStencilFaceFlags                          faceMask,
	uint32_t                                    reference)
{
	API_MARKER(SetStencilReference, commandBuffer, faceMask, reference);
}

#undef API_MARKER
