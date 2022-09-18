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

#include "vk_common_entrypoints.h"
#include "wsi_common_entrypoints.h"
#include "radv_private.h"
#include "radv_shader.h"

#include "ac_rgp.h"
#include "ac_sqtt.h"

static void
radv_write_begin_general_api_marker(struct radv_cmd_buffer *cmd_buffer,
                                    enum rgp_sqtt_marker_general_api_type api_type)
{
   struct rgp_sqtt_marker_general_api marker = {0};

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_GENERAL_API;
   marker.api_type = api_type;

   radv_emit_thread_trace_userdata(cmd_buffer, &marker, sizeof(marker) / 4);
}

static void
radv_write_end_general_api_marker(struct radv_cmd_buffer *cmd_buffer,
                                  enum rgp_sqtt_marker_general_api_type api_type)
{
   struct rgp_sqtt_marker_general_api marker = {0};

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_GENERAL_API;
   marker.api_type = api_type;
   marker.is_end = 1;

   radv_emit_thread_trace_userdata(cmd_buffer, &marker, sizeof(marker) / 4);
}

static void
radv_write_event_marker(struct radv_cmd_buffer *cmd_buffer,
                        enum rgp_sqtt_marker_event_type api_type, uint32_t vertex_offset_user_data,
                        uint32_t instance_offset_user_data, uint32_t draw_index_user_data)
{
   struct rgp_sqtt_marker_event marker = {0};

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_EVENT;
   marker.api_type = api_type;
   marker.cmd_id = cmd_buffer->state.num_events++;
   marker.cb_id = 0;

   if (vertex_offset_user_data == UINT_MAX || instance_offset_user_data == UINT_MAX) {
      vertex_offset_user_data = 0;
      instance_offset_user_data = 0;
   }

   if (draw_index_user_data == UINT_MAX)
      draw_index_user_data = vertex_offset_user_data;

   marker.vertex_offset_reg_idx = vertex_offset_user_data;
   marker.instance_offset_reg_idx = instance_offset_user_data;
   marker.draw_index_reg_idx = draw_index_user_data;

   radv_emit_thread_trace_userdata(cmd_buffer, &marker, sizeof(marker) / 4);
}

static void
radv_write_event_with_dims_marker(struct radv_cmd_buffer *cmd_buffer,
                                  enum rgp_sqtt_marker_event_type api_type, uint32_t x, uint32_t y,
                                  uint32_t z)
{
   struct rgp_sqtt_marker_event_with_dims marker = {0};

   marker.event.identifier = RGP_SQTT_MARKER_IDENTIFIER_EVENT;
   marker.event.api_type = api_type;
   marker.event.cmd_id = cmd_buffer->state.num_events++;
   marker.event.cb_id = 0;
   marker.event.has_thread_dims = 1;

   marker.thread_x = x;
   marker.thread_y = y;
   marker.thread_z = z;

   radv_emit_thread_trace_userdata(cmd_buffer, &marker, sizeof(marker) / 4);
}

static void
radv_write_user_event_marker(struct radv_cmd_buffer *cmd_buffer,
                             enum rgp_sqtt_marker_user_event_type type, const char *str)
{
   if (type == UserEventPop) {
      assert(str == NULL);
      struct rgp_sqtt_marker_user_event marker = {0};
      marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_USER_EVENT;
      marker.data_type = type;

      radv_emit_thread_trace_userdata(cmd_buffer, &marker, sizeof(marker) / 4);
   } else {
      assert(str != NULL);
      unsigned len = strlen(str);
      struct rgp_sqtt_marker_user_event_with_length marker = {0};
      marker.user_event.identifier = RGP_SQTT_MARKER_IDENTIFIER_USER_EVENT;
      marker.user_event.data_type = type;
      marker.length = align(len, 4);

      uint8_t *buffer = alloca(sizeof(marker) + marker.length);
      memset(buffer, 0, sizeof(marker) + marker.length);
      memcpy(buffer, &marker, sizeof(marker));
      memcpy(buffer + sizeof(marker), str, len);

      radv_emit_thread_trace_userdata(cmd_buffer, buffer,
                                      sizeof(marker) / 4 + marker.length / 4);
   }
}

void
radv_describe_begin_cmd_buffer(struct radv_cmd_buffer *cmd_buffer)
{
   uint64_t device_id = (uintptr_t)cmd_buffer->device;
   struct rgp_sqtt_marker_cb_start marker = {0};

   if (likely(!cmd_buffer->device->thread_trace.bo))
      return;

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_CB_START;
   marker.cb_id = 0;
   marker.device_id_low = device_id;
   marker.device_id_high = device_id >> 32;
   marker.queue = cmd_buffer->qf;
   marker.queue_flags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT;

   if (cmd_buffer->qf == RADV_QUEUE_GENERAL)
      marker.queue_flags |= VK_QUEUE_GRAPHICS_BIT;

   radv_emit_thread_trace_userdata(cmd_buffer, &marker, sizeof(marker) / 4);
}

void
radv_describe_end_cmd_buffer(struct radv_cmd_buffer *cmd_buffer)
{
   uint64_t device_id = (uintptr_t)cmd_buffer->device;
   struct rgp_sqtt_marker_cb_end marker = {0};

   if (likely(!cmd_buffer->device->thread_trace.bo))
      return;

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_CB_END;
   marker.cb_id = 0;
   marker.device_id_low = device_id;
   marker.device_id_high = device_id >> 32;

   radv_emit_thread_trace_userdata(cmd_buffer, &marker, sizeof(marker) / 4);
}

void
radv_describe_draw(struct radv_cmd_buffer *cmd_buffer)
{
   if (likely(!cmd_buffer->device->thread_trace.bo))
      return;

   radv_write_event_marker(cmd_buffer, cmd_buffer->state.current_event_type, UINT_MAX, UINT_MAX,
                           UINT_MAX);
}

void
radv_describe_dispatch(struct radv_cmd_buffer *cmd_buffer, int x, int y, int z)
{
   if (likely(!cmd_buffer->device->thread_trace.bo))
      return;

   radv_write_event_with_dims_marker(cmd_buffer, cmd_buffer->state.current_event_type, x, y, z);
}

void
radv_describe_begin_render_pass_clear(struct radv_cmd_buffer *cmd_buffer,
                                      VkImageAspectFlagBits aspects)
{
   cmd_buffer->state.current_event_type = (aspects & VK_IMAGE_ASPECT_COLOR_BIT)
                                             ? EventRenderPassColorClear
                                             : EventRenderPassDepthStencilClear;
}

void
radv_describe_end_render_pass_clear(struct radv_cmd_buffer *cmd_buffer)
{
   cmd_buffer->state.current_event_type = EventInternalUnknown;
}

void
radv_describe_begin_render_pass_resolve(struct radv_cmd_buffer *cmd_buffer)
{
   cmd_buffer->state.current_event_type = EventRenderPassResolve;
}

void
radv_describe_end_render_pass_resolve(struct radv_cmd_buffer *cmd_buffer)
{
   cmd_buffer->state.current_event_type = EventInternalUnknown;
}

void
radv_describe_barrier_end_delayed(struct radv_cmd_buffer *cmd_buffer)
{
   struct rgp_sqtt_marker_barrier_end marker = {0};

   if (likely(!cmd_buffer->device->thread_trace.bo) || !cmd_buffer->state.pending_sqtt_barrier_end)
      return;

   cmd_buffer->state.pending_sqtt_barrier_end = false;

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_BARRIER_END;
   marker.cb_id = 0;

   marker.num_layout_transitions = cmd_buffer->state.num_layout_transitions;

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

   radv_emit_thread_trace_userdata(cmd_buffer, &marker, sizeof(marker) / 4);

   cmd_buffer->state.num_layout_transitions = 0;
}

void
radv_describe_barrier_start(struct radv_cmd_buffer *cmd_buffer, enum rgp_barrier_reason reason)
{
   struct rgp_sqtt_marker_barrier_start marker = {0};

   if (likely(!cmd_buffer->device->thread_trace.bo))
      return;

   radv_describe_barrier_end_delayed(cmd_buffer);
   cmd_buffer->state.sqtt_flush_bits = 0;

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_BARRIER_START;
   marker.cb_id = 0;
   marker.dword02 = reason;

   radv_emit_thread_trace_userdata(cmd_buffer, &marker, sizeof(marker) / 4);
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

   if (likely(!cmd_buffer->device->thread_trace.bo))
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

   radv_emit_thread_trace_userdata(cmd_buffer, &marker, sizeof(marker) / 4);

   cmd_buffer->state.num_layout_transitions++;
}

static void
radv_describe_pipeline_bind(struct radv_cmd_buffer *cmd_buffer,
                            VkPipelineBindPoint pipelineBindPoint, struct radv_pipeline *pipeline)
{
   struct rgp_sqtt_marker_pipeline_bind marker = {0};

   if (likely(!cmd_buffer->device->thread_trace.bo))
      return;

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_BIND_PIPELINE;
   marker.cb_id = 0;
   marker.bind_point = pipelineBindPoint;
   marker.api_pso_hash[0] = pipeline->pipeline_hash;
   marker.api_pso_hash[1] = pipeline->pipeline_hash >> 32;

   radv_emit_thread_trace_userdata(cmd_buffer, &marker, sizeof(marker) / 4);
}

/* TODO: Improve the way to trigger capture (overlay, etc). */
static void
radv_handle_thread_trace(VkQueue _queue)
{
   RADV_FROM_HANDLE(radv_queue, queue, _queue);
   static bool thread_trace_enabled = false;
   static uint64_t num_frames = 0;
   bool resize_trigger = false;

   if (thread_trace_enabled) {
      struct ac_thread_trace thread_trace = {0};

      radv_end_thread_trace(queue);
      thread_trace_enabled = false;

      /* TODO: Do something better than this whole sync. */
      queue->device->vk.dispatch_table.QueueWaitIdle(_queue);

      if (radv_get_thread_trace(queue, &thread_trace)) {
         struct ac_spm_trace_data *spm_trace = NULL;

         if (queue->device->spm_trace.bo)
            spm_trace = &queue->device->spm_trace;

         ac_dump_rgp_capture(&queue->device->physical_device->rad_info, &thread_trace, spm_trace);
      } else {
         /* Trigger a new capture if the driver failed to get
          * the trace because the buffer was too small.
          */
         resize_trigger = true;
      }
   }

   if (!thread_trace_enabled) {
      bool frame_trigger = num_frames == queue->device->thread_trace.start_frame;
      bool file_trigger = false;
#ifndef _WIN32
      if (queue->device->thread_trace.trigger_file &&
          access(queue->device->thread_trace.trigger_file, W_OK) == 0) {
         if (unlink(queue->device->thread_trace.trigger_file) == 0) {
            file_trigger = true;
         } else {
            /* Do not enable tracing if we cannot remove the file,
             * because by then we'll trace every frame ... */
            fprintf(stderr, "RADV: could not remove thread trace trigger file, ignoring\n");
         }
      }
#endif

      if (frame_trigger || file_trigger || resize_trigger) {
         if (ac_check_profile_state(&queue->device->physical_device->rad_info)) {
            fprintf(stderr, "radv: Canceling RGP trace request as a hang condition has been "
                            "detected. Force the GPU into a profiling mode with e.g. "
                            "\"echo profile_peak  > "
                            "/sys/class/drm/card0/device/power_dpm_force_performance_level\"\n");
            return;
         }

         radv_begin_thread_trace(queue);
         assert(!thread_trace_enabled);
         thread_trace_enabled = true;
      }
   }
   num_frames++;
}

VKAPI_ATTR VkResult VKAPI_CALL
sqtt_QueuePresentKHR(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   VkResult result;

   result = wsi_QueuePresentKHR(_queue, pPresentInfo);
   if (result != VK_SUCCESS)
      return result;

   radv_handle_thread_trace(_queue);

   return VK_SUCCESS;
}

#define EVENT_MARKER_BASE(cmd_name, api_name, event_name, ...)                                     \
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);                                   \
   radv_write_begin_general_api_marker(cmd_buffer, ApiCmd##api_name);                              \
   cmd_buffer->state.current_event_type = EventCmd##event_name;                                    \
   radv_Cmd##cmd_name(__VA_ARGS__);                                                                \
   cmd_buffer->state.current_event_type = EventInternalUnknown;                                    \
   radv_write_end_general_api_marker(cmd_buffer, ApiCmd##api_name);

#define EVENT_MARKER_ALIAS(cmd_name, api_name, ...)                                                \
   EVENT_MARKER_BASE(cmd_name, api_name, api_name, __VA_ARGS__);

#define EVENT_MARKER(cmd_name, ...)                                                                \
   EVENT_MARKER_ALIAS(cmd_name, cmd_name, __VA_ARGS__);

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount,
             uint32_t firstVertex, uint32_t firstInstance)
{
   EVENT_MARKER(Draw, commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
                    uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
   EVENT_MARKER(DrawIndexed, commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset,
                firstInstance);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                     uint32_t drawCount, uint32_t stride)
{
   EVENT_MARKER(DrawIndirect, commandBuffer, buffer, offset, drawCount, stride);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                            uint32_t drawCount, uint32_t stride)
{
   EVENT_MARKER(DrawIndexedIndirect, commandBuffer, buffer, offset, drawCount, stride);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                          VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                          uint32_t maxDrawCount, uint32_t stride)
{
   EVENT_MARKER(DrawIndirectCount, commandBuffer, buffer, offset, countBuffer, countBufferOffset,
                maxDrawCount, stride);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                 VkDeviceSize offset, VkBuffer countBuffer,
                                 VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                                 uint32_t stride)
{
   EVENT_MARKER(DrawIndexedIndirectCount, commandBuffer, buffer, offset, countBuffer,
                countBufferOffset, maxDrawCount, stride);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z)
{
   EVENT_MARKER(Dispatch, commandBuffer, x, y, z);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset)
{
   EVENT_MARKER(DispatchIndirect, commandBuffer, buffer, offset);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   EVENT_MARKER_ALIAS(CopyBuffer2, CopyBuffer, commandBuffer, pCopyBufferInfo);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                   VkDeviceSize fillSize, uint32_t data)
{
   EVENT_MARKER(FillBuffer, commandBuffer, dstBuffer, dstOffset, fillSize, data);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                     VkDeviceSize dataSize, const void *pData)
{
   EVENT_MARKER(UpdateBuffer, commandBuffer, dstBuffer, dstOffset, dataSize, pData);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2 *pCopyImageInfo)
{
   EVENT_MARKER_ALIAS(CopyImage2, CopyImage, commandBuffer, pCopyImageInfo);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                           const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
   EVENT_MARKER_ALIAS(CopyBufferToImage2, CopyBufferToImage, commandBuffer,
                      pCopyBufferToImageInfo);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                           const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   EVENT_MARKER_ALIAS(CopyImageToBuffer2, CopyImageToBuffer, commandBuffer,
                      pCopyImageToBufferInfo);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2 *pBlitImageInfo)
{
   EVENT_MARKER_ALIAS(BlitImage2, BlitImage, commandBuffer, pBlitImageInfo);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image_h, VkImageLayout imageLayout,
                        const VkClearColorValue *pColor, uint32_t rangeCount,
                        const VkImageSubresourceRange *pRanges)
{
   EVENT_MARKER(ClearColorImage, commandBuffer, image_h, imageLayout, pColor, rangeCount, pRanges);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image_h,
                               VkImageLayout imageLayout,
                               const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount,
                               const VkImageSubresourceRange *pRanges)
{
   EVENT_MARKER(ClearDepthStencilImage, commandBuffer, image_h, imageLayout, pDepthStencil,
                rangeCount, pRanges);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                         const VkClearAttachment *pAttachments, uint32_t rectCount,
                         const VkClearRect *pRects)
{
   EVENT_MARKER(ClearAttachments, commandBuffer, attachmentCount, pAttachments, rectCount, pRects);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdResolveImage2(VkCommandBuffer commandBuffer,
                      const VkResolveImageInfo2 *pResolveImageInfo)
{
   EVENT_MARKER_ALIAS(ResolveImage2, ResolveImage, commandBuffer, pResolveImageInfo);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents,
                    const VkDependencyInfo* pDependencyInfos)
{
   EVENT_MARKER_ALIAS(WaitEvents2, WaitEvents, commandBuffer, eventCount, pEvents,
                      pDependencyInfos);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                         const VkDependencyInfo* pDependencyInfo)
{
   EVENT_MARKER_ALIAS(PipelineBarrier2, PipelineBarrier, commandBuffer, pDependencyInfo);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery,
                       uint32_t queryCount)
{
   EVENT_MARKER(ResetQueryPool, commandBuffer, queryPool, firstQuery, queryCount);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                             uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer,
                             VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags)
{
   EVENT_MARKER(CopyQueryPoolResults, commandBuffer, queryPool, firstQuery, queryCount, dstBuffer,
                dstOffset, stride, flags);
}

#define EVENT_RT_MARKER(cmd_name, ...) \
   EVENT_MARKER_BASE(cmd_name, Dispatch, cmd_name, __VA_ARGS__);

#define EVENT_RT_MARKER_ALIAS(cmd_name, event_name, ...) \
   EVENT_MARKER_BASE(cmd_name, Dispatch, event_name, __VA_ARGS__);

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdTraceRaysKHR(VkCommandBuffer commandBuffer,
                     const VkStridedDeviceAddressRegionKHR *pRaygenShaderBindingTable,
                     const VkStridedDeviceAddressRegionKHR *pMissShaderBindingTable,
                     const VkStridedDeviceAddressRegionKHR *pHitShaderBindingTable,
                     const VkStridedDeviceAddressRegionKHR *pCallableShaderBindingTable,
                     uint32_t width, uint32_t height, uint32_t depth)
{
   EVENT_RT_MARKER(TraceRaysKHR, commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable,
                   pHitShaderBindingTable, pCallableShaderBindingTable, width, height, depth);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdTraceRaysIndirectKHR(VkCommandBuffer commandBuffer,
                             const VkStridedDeviceAddressRegionKHR *pRaygenShaderBindingTable,
                             const VkStridedDeviceAddressRegionKHR *pMissShaderBindingTable,
                             const VkStridedDeviceAddressRegionKHR *pHitShaderBindingTable,
                             const VkStridedDeviceAddressRegionKHR *pCallableShaderBindingTable,
                             VkDeviceAddress indirectDeviceAddress)
{
   EVENT_RT_MARKER(TraceRaysIndirectKHR, commandBuffer, pRaygenShaderBindingTable,
                   pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable,
                   indirectDeviceAddress);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdTraceRaysIndirect2KHR(VkCommandBuffer commandBuffer, VkDeviceAddress indirectDeviceAddress)
{
   EVENT_RT_MARKER_ALIAS(TraceRaysIndirect2KHR, TraceRaysIndirectKHR, commandBuffer,
                         indirectDeviceAddress);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                       const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
                                       const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   EVENT_RT_MARKER(BuildAccelerationStructuresKHR, commandBuffer, infoCount, pInfos,
                   ppBuildRangeInfos);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                     const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   EVENT_RT_MARKER(CopyAccelerationStructureKHR, commandBuffer, pInfo);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer commandBuffer,
                                             const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo)
{
   EVENT_RT_MARKER(CopyAccelerationStructureToMemoryKHR, commandBuffer, pInfo);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                             const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   EVENT_RT_MARKER(CopyMemoryToAccelerationStructureKHR, commandBuffer, pInfo);
}

#undef EVENT_RT_MARKER_ALIAS
#undef EVENT_RT_MARKER

#undef EVENT_MARKER
#undef EVENT_MARKER_ALIAS
#undef EVENT_MARKER_BASE

#define API_MARKER_ALIAS(cmd_name, api_name, ...)                                                  \
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);                                   \
   radv_write_begin_general_api_marker(cmd_buffer, ApiCmd##api_name);                              \
   radv_Cmd##cmd_name(__VA_ARGS__);                                                                \
   radv_write_end_general_api_marker(cmd_buffer, ApiCmd##api_name);

#define API_MARKER(cmd_name, ...) API_MARKER_ALIAS(cmd_name, cmd_name, __VA_ARGS__);

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                     VkPipeline _pipeline)
{
   RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);

   API_MARKER(BindPipeline, commandBuffer, pipelineBindPoint, _pipeline);

   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR) {
      /* RGP seems to expect a compute bind point to detect and report RT pipelines, which makes
       * sense somehow given that RT shaders are compiled to an unified compute shader.
       */
      radv_describe_pipeline_bind(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
   } else {
      radv_describe_pipeline_bind(cmd_buffer, pipelineBindPoint, pipeline);
   }
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                           VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount,
                           const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
                           const uint32_t *pDynamicOffsets)
{
   API_MARKER(BindDescriptorSets, commandBuffer, pipelineBindPoint, layout, firstSet,
              descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                        VkIndexType indexType)
{
   API_MARKER(BindIndexBuffer, commandBuffer, buffer, offset, indexType);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                           uint32_t bindingCount, const VkBuffer *pBuffers,
                           const VkDeviceSize *pOffsets, const VkDeviceSize* pSizes,
                           const VkDeviceSize* pStrides)
{
   API_MARKER_ALIAS(BindVertexBuffers2, BindVertexBuffers, commandBuffer, firstBinding,
                    bindingCount, pBuffers, pOffsets, pSizes, pStrides);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query,
                   VkQueryControlFlags flags)
{
   API_MARKER(BeginQuery, commandBuffer, queryPool, query, flags);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query)
{
   API_MARKER(EndQuery, commandBuffer, queryPool, query);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdWriteTimestamp2(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage,
                        VkQueryPool queryPool, uint32_t query)
{
   API_MARKER_ALIAS(WriteTimestamp2, WriteTimestamp, commandBuffer, stage, queryPool, query);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                      VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size,
                      const void *pValues)
{
   API_MARKER(PushConstants, commandBuffer, layout, stageFlags, offset, size, pValues);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdBeginRendering(VkCommandBuffer commandBuffer,
                       const VkRenderingInfo *pRenderingInfo)
{
   API_MARKER_ALIAS(BeginRendering, BeginRenderPass, commandBuffer, pRenderingInfo);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdEndRendering(VkCommandBuffer commandBuffer)
{
   API_MARKER_ALIAS(EndRendering, EndRenderPass, commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount,
                        const VkCommandBuffer *pCmdBuffers)
{
   API_MARKER(ExecuteCommands, commandBuffer, commandBufferCount, pCmdBuffers);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount,
                    const VkViewport *pViewports)
{
   API_MARKER(SetViewport, commandBuffer, firstViewport, viewportCount, pViewports);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount,
                   const VkRect2D *pScissors)
{
   API_MARKER(SetScissor, commandBuffer, firstScissor, scissorCount, pScissors);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
   API_MARKER(SetLineWidth, commandBuffer, lineWidth);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor,
                     float depthBiasClamp, float depthBiasSlopeFactor)
{
   API_MARKER(SetDepthBias, commandBuffer, depthBiasConstantFactor, depthBiasClamp,
              depthBiasSlopeFactor);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4])
{
   API_MARKER(SetBlendConstants, commandBuffer, blendConstants);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds)
{
   API_MARKER(SetDepthBounds, commandBuffer, minDepthBounds, maxDepthBounds);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                              uint32_t compareMask)
{
   API_MARKER(SetStencilCompareMask, commandBuffer, faceMask, compareMask);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                            uint32_t writeMask)
{
   API_MARKER(SetStencilWriteMask, commandBuffer, faceMask, writeMask);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                            uint32_t reference)
{
   API_MARKER(SetStencilReference, commandBuffer, faceMask, reference);
}

/* VK_EXT_debug_marker */
VKAPI_ATTR void VKAPI_CALL
sqtt_CmdDebugMarkerBeginEXT(VkCommandBuffer commandBuffer,
                            const VkDebugMarkerMarkerInfoEXT *pMarkerInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   radv_write_user_event_marker(cmd_buffer, UserEventPush, pMarkerInfo->pMarkerName);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdDebugMarkerEndEXT(VkCommandBuffer commandBuffer)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   radv_write_user_event_marker(cmd_buffer, UserEventPop, NULL);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdDebugMarkerInsertEXT(VkCommandBuffer commandBuffer,
                             const VkDebugMarkerMarkerInfoEXT *pMarkerInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   radv_write_user_event_marker(cmd_buffer, UserEventTrigger, pMarkerInfo->pMarkerName);
}

VKAPI_ATTR VkResult VKAPI_CALL
sqtt_DebugMarkerSetObjectNameEXT(VkDevice device, const VkDebugMarkerObjectNameInfoEXT *pNameInfo)
{
   /* no-op */
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
sqtt_DebugMarkerSetObjectTagEXT(VkDevice device, const VkDebugMarkerObjectTagInfoEXT *pTagInfo)
{
   /* no-op */
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdBeginDebugUtilsLabelEXT(VkCommandBuffer commandBuffer,
                                const VkDebugUtilsLabelEXT *pLabelInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   radv_write_user_event_marker(cmd_buffer, UserEventPush, pLabelInfo->pLabelName);

   vk_common_CmdBeginDebugUtilsLabelEXT(commandBuffer, pLabelInfo);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdEndDebugUtilsLabelEXT(VkCommandBuffer commandBuffer)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   radv_write_user_event_marker(cmd_buffer, UserEventPop, NULL);

   vk_common_CmdEndDebugUtilsLabelEXT(commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL
sqtt_CmdInsertDebugUtilsLabelEXT(VkCommandBuffer commandBuffer,
                                 const VkDebugUtilsLabelEXT *pLabelInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   radv_write_user_event_marker(cmd_buffer, UserEventTrigger, pLabelInfo->pLabelName);

   vk_common_CmdInsertDebugUtilsLabelEXT(commandBuffer, pLabelInfo);
}

/* Pipelines */
static enum rgp_hardware_stages
radv_mesa_to_rgp_shader_stage(struct radv_pipeline *pipeline, gl_shader_stage stage)
{
   struct radv_shader *shader = pipeline->shaders[stage];

   switch (stage) {
   case MESA_SHADER_VERTEX:
      if (shader->info.vs.as_ls)
         return RGP_HW_STAGE_LS;
      else if (shader->info.vs.as_es)
         return RGP_HW_STAGE_ES;
      else if (shader->info.is_ngg)
         return RGP_HW_STAGE_GS;
      else
         return RGP_HW_STAGE_VS;
   case MESA_SHADER_TESS_CTRL:
      return RGP_HW_STAGE_HS;
   case MESA_SHADER_TESS_EVAL:
      if (shader->info.tes.as_es)
         return RGP_HW_STAGE_ES;
      else if (shader->info.is_ngg)
         return RGP_HW_STAGE_GS;
      else
         return RGP_HW_STAGE_VS;
   case MESA_SHADER_GEOMETRY:
      return RGP_HW_STAGE_GS;
   case MESA_SHADER_FRAGMENT:
      return RGP_HW_STAGE_PS;
   case MESA_SHADER_COMPUTE:
      return RGP_HW_STAGE_CS;
   default:
      unreachable("invalid mesa shader stage");
   }
}

static VkResult
radv_add_code_object(struct radv_device *device, struct radv_pipeline *pipeline)
{
   struct ac_thread_trace_data *thread_trace_data = &device->thread_trace;
   struct rgp_code_object *code_object = &thread_trace_data->rgp_code_object;
   struct rgp_code_object_record *record;

   record = malloc(sizeof(struct rgp_code_object_record));
   if (!record)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   record->shader_stages_mask = 0;
   record->num_shaders_combined = 0;
   record->pipeline_hash[0] = pipeline->pipeline_hash;
   record->pipeline_hash[1] = pipeline->pipeline_hash;

   for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      struct radv_shader *shader = pipeline->shaders[i];
      uint8_t *code;
      uint64_t va;

      if (!shader)
         continue;

      code = malloc(shader->code_size);
      if (!code) {
         free(record);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      memcpy(code, shader->code_ptr, shader->code_size);

      va = radv_shader_get_va(shader);

      record->shader_data[i].hash[0] = (uint64_t)(uintptr_t)shader;
      record->shader_data[i].hash[1] = (uint64_t)(uintptr_t)shader >> 32;
      record->shader_data[i].code_size = shader->code_size;
      record->shader_data[i].code = code;
      record->shader_data[i].vgpr_count = shader->config.num_vgprs;
      record->shader_data[i].sgpr_count = shader->config.num_sgprs;
      record->shader_data[i].scratch_memory_size = shader->config.scratch_bytes_per_wave;
      record->shader_data[i].wavefront_size = shader->info.wave_size;
      record->shader_data[i].base_address = va & 0xffffffffffff;
      record->shader_data[i].elf_symbol_offset = 0;
      record->shader_data[i].hw_stage = radv_mesa_to_rgp_shader_stage(pipeline, i);
      record->shader_data[i].is_combined = false;

      record->shader_stages_mask |= (1 << i);
      record->num_shaders_combined++;
   }

   simple_mtx_lock(&code_object->lock);
   list_addtail(&record->list, &code_object->record);
   code_object->record_count++;
   simple_mtx_unlock(&code_object->lock);

   return VK_SUCCESS;
}

static VkResult
radv_register_pipeline(struct radv_device *device, struct radv_pipeline *pipeline)
{
   bool result;
   uint64_t base_va = ~0;

   result = ac_sqtt_add_pso_correlation(&device->thread_trace, pipeline->pipeline_hash);
   if (!result)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* Find the lowest shader BO VA. */
   for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      struct radv_shader *shader = pipeline->shaders[i];
      uint64_t va;

      if (!shader)
         continue;

      va = radv_shader_get_va(shader);
      base_va = MIN2(base_va, va);
   }

   result =
      ac_sqtt_add_code_object_loader_event(&device->thread_trace, pipeline->pipeline_hash, base_va);
   if (!result)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   result = radv_add_code_object(device, pipeline);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

static void
radv_unregister_pipeline(struct radv_device *device, struct radv_pipeline *pipeline)
{
   struct ac_thread_trace_data *thread_trace_data = &device->thread_trace;
   struct rgp_pso_correlation *pso_correlation = &thread_trace_data->rgp_pso_correlation;
   struct rgp_loader_events *loader_events = &thread_trace_data->rgp_loader_events;
   struct rgp_code_object *code_object = &thread_trace_data->rgp_code_object;

   /* Destroy the PSO correlation record. */
   simple_mtx_lock(&pso_correlation->lock);
   list_for_each_entry_safe(struct rgp_pso_correlation_record, record, &pso_correlation->record,
                            list)
   {
      if (record->pipeline_hash[0] == pipeline->pipeline_hash) {
         pso_correlation->record_count--;
         list_del(&record->list);
         free(record);
         break;
      }
   }
   simple_mtx_unlock(&pso_correlation->lock);

   /* Destroy the code object loader record. */
   simple_mtx_lock(&loader_events->lock);
   list_for_each_entry_safe(struct rgp_loader_events_record, record, &loader_events->record, list)
   {
      if (record->code_object_hash[0] == pipeline->pipeline_hash) {
         loader_events->record_count--;
         list_del(&record->list);
         free(record);
         break;
      }
   }
   simple_mtx_unlock(&loader_events->lock);

   /* Destroy the code object record. */
   simple_mtx_lock(&code_object->lock);
   list_for_each_entry_safe(struct rgp_code_object_record, record, &code_object->record, list)
   {
      if (record->pipeline_hash[0] == pipeline->pipeline_hash) {
         uint32_t mask = record->shader_stages_mask;
         int i;

         /* Free the disassembly. */
         while (mask) {
            i = u_bit_scan(&mask);
            free(record->shader_data[i].code);
         }

         code_object->record_count--;
         list_del(&record->list);
         free(record);
         break;
      }
   }
   simple_mtx_unlock(&code_object->lock);
}

VKAPI_ATTR VkResult VKAPI_CALL
sqtt_CreateGraphicsPipelines(VkDevice _device, VkPipelineCache pipelineCache, uint32_t count,
                             const VkGraphicsPipelineCreateInfo *pCreateInfos,
                             const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VkResult result;

   result = radv_CreateGraphicsPipelines(_device, pipelineCache, count, pCreateInfos, pAllocator,
                                         pPipelines);
   if (result != VK_SUCCESS)
      return result;

   for (unsigned i = 0; i < count; i++) {
      RADV_FROM_HANDLE(radv_pipeline, pipeline, pPipelines[i]);

      if (!pipeline)
         continue;

      result = radv_register_pipeline(device, pipeline);
      if (result != VK_SUCCESS)
         goto fail;
   }

   return VK_SUCCESS;

fail:
   for (unsigned i = 0; i < count; i++) {
      sqtt_DestroyPipeline(_device, pPipelines[i], pAllocator);
      pPipelines[i] = VK_NULL_HANDLE;
   }
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
sqtt_CreateComputePipelines(VkDevice _device, VkPipelineCache pipelineCache, uint32_t count,
                            const VkComputePipelineCreateInfo *pCreateInfos,
                            const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VkResult result;

   result = radv_CreateComputePipelines(_device, pipelineCache, count, pCreateInfos, pAllocator,
                                        pPipelines);
   if (result != VK_SUCCESS)
      return result;

   for (unsigned i = 0; i < count; i++) {
      RADV_FROM_HANDLE(radv_pipeline, pipeline, pPipelines[i]);

      if (!pipeline)
         continue;

      result = radv_register_pipeline(device, pipeline);
      if (result != VK_SUCCESS)
         goto fail;
   }

   return VK_SUCCESS;

fail:
   for (unsigned i = 0; i < count; i++) {
      sqtt_DestroyPipeline(_device, pPipelines[i], pAllocator);
      pPipelines[i] = VK_NULL_HANDLE;
   }
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
sqtt_CreateRayTracingPipelinesKHR(VkDevice _device, VkDeferredOperationKHR deferredOperation,
                                  VkPipelineCache pipelineCache, uint32_t count,
                                  const VkRayTracingPipelineCreateInfoKHR *pCreateInfos,
                                  const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VkResult result;

   result = radv_CreateRayTracingPipelinesKHR(_device, deferredOperation, pipelineCache, count,
                                              pCreateInfos, pAllocator, pPipelines);
   if (result != VK_SUCCESS)
      return result;

   for (unsigned i = 0; i < count; i++) {
      RADV_FROM_HANDLE(radv_pipeline, pipeline, pPipelines[i]);

      if (!pipeline)
         continue;

      result = radv_register_pipeline(device, pipeline);
      if (result != VK_SUCCESS)
         goto fail;
   }

   return VK_SUCCESS;

fail:
   for (unsigned i = 0; i < count; i++) {
      sqtt_DestroyPipeline(_device, pPipelines[i], pAllocator);
      pPipelines[i] = VK_NULL_HANDLE;
   }
   return result;
}

VKAPI_ATTR void VKAPI_CALL
sqtt_DestroyPipeline(VkDevice _device, VkPipeline _pipeline,
                     const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);

   if (!_pipeline)
      return;

   radv_unregister_pipeline(device, pipeline);

   radv_DestroyPipeline(_device, _pipeline, pAllocator);
}

#undef API_MARKER
