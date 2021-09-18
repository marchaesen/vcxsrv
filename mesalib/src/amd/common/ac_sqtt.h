/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 * Copyright 2020 Valve Corporation
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef AC_SQTT_H
#define AC_SQTT_H

#include <stdint.h>
#include <stdbool.h>

#include <assert.h>
#include "c11_compat.h"
#include "ac_rgp.h"

struct radeon_cmdbuf;
struct radeon_info;

struct ac_thread_trace_data {
   struct radeon_cmdbuf *start_cs[2];
   struct radeon_cmdbuf *stop_cs[2];
   /* struct radeon_winsys_bo or struct pb_buffer */
   void *bo;
   void *ptr;
   uint32_t buffer_size;
   int start_frame;
   char *trigger_file;

   struct rgp_code_object rgp_code_object;
   struct rgp_loader_events rgp_loader_events;
   struct rgp_pso_correlation rgp_pso_correlation;
};

#define SQTT_BUFFER_ALIGN_SHIFT 12

struct ac_thread_trace_info {
   uint32_t cur_offset;
   uint32_t trace_status;
   union {
      uint32_t gfx9_write_counter;
      uint32_t gfx10_dropped_cntr;
   };
};

struct ac_thread_trace_se {
   struct ac_thread_trace_info info;
   void *data_ptr;
   uint32_t shader_engine;
   uint32_t compute_unit;
};

struct ac_thread_trace {
   struct ac_thread_trace_data *data;
   uint32_t num_traces;
   struct ac_thread_trace_se traces[4];
};

uint64_t
ac_thread_trace_get_info_offset(unsigned se);

uint64_t
ac_thread_trace_get_data_offset(const struct radeon_info *rad_info,
                                const struct ac_thread_trace_data *data, unsigned se);
uint64_t
ac_thread_trace_get_info_va(uint64_t va, unsigned se);

uint64_t
ac_thread_trace_get_data_va(const struct radeon_info *rad_info,
                            const struct ac_thread_trace_data *data, uint64_t va, unsigned se);

bool
ac_is_thread_trace_complete(struct radeon_info *rad_info,
                            const struct ac_thread_trace_data *data,
                            const struct ac_thread_trace_info *info);

uint32_t
ac_get_expected_buffer_size(struct radeon_info *rad_info,
                            const struct ac_thread_trace_info *info);

/**
 * Identifiers for RGP SQ thread-tracing markers (Table 1)
 */
enum rgp_sqtt_marker_identifier
{
   RGP_SQTT_MARKER_IDENTIFIER_EVENT = 0x0,
   RGP_SQTT_MARKER_IDENTIFIER_CB_START = 0x1,
   RGP_SQTT_MARKER_IDENTIFIER_CB_END = 0x2,
   RGP_SQTT_MARKER_IDENTIFIER_BARRIER_START = 0x3,
   RGP_SQTT_MARKER_IDENTIFIER_BARRIER_END = 0x4,
   RGP_SQTT_MARKER_IDENTIFIER_USER_EVENT = 0x5,
   RGP_SQTT_MARKER_IDENTIFIER_GENERAL_API = 0x6,
   RGP_SQTT_MARKER_IDENTIFIER_SYNC = 0x7,
   RGP_SQTT_MARKER_IDENTIFIER_PRESENT = 0x8,
   RGP_SQTT_MARKER_IDENTIFIER_LAYOUT_TRANSITION = 0x9,
   RGP_SQTT_MARKER_IDENTIFIER_RENDER_PASS = 0xA,
   RGP_SQTT_MARKER_IDENTIFIER_RESERVED2 = 0xB,
   RGP_SQTT_MARKER_IDENTIFIER_BIND_PIPELINE = 0xC,
   RGP_SQTT_MARKER_IDENTIFIER_RESERVED4 = 0xD,
   RGP_SQTT_MARKER_IDENTIFIER_RESERVED5 = 0xE,
   RGP_SQTT_MARKER_IDENTIFIER_RESERVED6 = 0xF
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
enum rgp_sqtt_marker_general_api_type
{
   ApiCmdBindPipeline = 0,
   ApiCmdBindDescriptorSets = 1,
   ApiCmdBindIndexBuffer = 2,
   ApiCmdBindVertexBuffers = 3,
   ApiCmdDraw = 4,
   ApiCmdDrawIndexed = 5,
   ApiCmdDrawIndirect = 6,
   ApiCmdDrawIndexedIndirect = 7,
   ApiCmdDrawIndirectCountAMD = 8,
   ApiCmdDrawIndexedIndirectCountAMD = 9,
   ApiCmdDispatch = 10,
   ApiCmdDispatchIndirect = 11,
   ApiCmdCopyBuffer = 12,
   ApiCmdCopyImage = 13,
   ApiCmdBlitImage = 14,
   ApiCmdCopyBufferToImage = 15,
   ApiCmdCopyImageToBuffer = 16,
   ApiCmdUpdateBuffer = 17,
   ApiCmdFillBuffer = 18,
   ApiCmdClearColorImage = 19,
   ApiCmdClearDepthStencilImage = 20,
   ApiCmdClearAttachments = 21,
   ApiCmdResolveImage = 22,
   ApiCmdWaitEvents = 23,
   ApiCmdPipelineBarrier = 24,
   ApiCmdBeginQuery = 25,
   ApiCmdEndQuery = 26,
   ApiCmdResetQueryPool = 27,
   ApiCmdWriteTimestamp = 28,
   ApiCmdCopyQueryPoolResults = 29,
   ApiCmdPushConstants = 30,
   ApiCmdBeginRenderPass = 31,
   ApiCmdNextSubpass = 32,
   ApiCmdEndRenderPass = 33,
   ApiCmdExecuteCommands = 34,
   ApiCmdSetViewport = 35,
   ApiCmdSetScissor = 36,
   ApiCmdSetLineWidth = 37,
   ApiCmdSetDepthBias = 38,
   ApiCmdSetBlendConstants = 39,
   ApiCmdSetDepthBounds = 40,
   ApiCmdSetStencilCompareMask = 41,
   ApiCmdSetStencilWriteMask = 42,
   ApiCmdSetStencilReference = 43,
   ApiCmdDrawIndirectCount = 44,
   ApiCmdDrawIndexedIndirectCount = 45,
   ApiInvalid = 0xffffffff
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
enum rgp_sqtt_marker_event_type
{
   EventCmdDraw = 0,
   EventCmdDrawIndexed = 1,
   EventCmdDrawIndirect = 2,
   EventCmdDrawIndexedIndirect = 3,
   EventCmdDrawIndirectCountAMD = 4,
   EventCmdDrawIndexedIndirectCountAMD = 5,
   EventCmdDispatch = 6,
   EventCmdDispatchIndirect = 7,
   EventCmdCopyBuffer = 8,
   EventCmdCopyImage = 9,
   EventCmdBlitImage = 10,
   EventCmdCopyBufferToImage = 11,
   EventCmdCopyImageToBuffer = 12,
   EventCmdUpdateBuffer = 13,
   EventCmdFillBuffer = 14,
   EventCmdClearColorImage = 15,
   EventCmdClearDepthStencilImage = 16,
   EventCmdClearAttachments = 17,
   EventCmdResolveImage = 18,
   EventCmdWaitEvents = 19,
   EventCmdPipelineBarrier = 20,
   EventCmdResetQueryPool = 21,
   EventCmdCopyQueryPoolResults = 22,
   EventRenderPassColorClear = 23,
   EventRenderPassDepthStencilClear = 24,
   EventRenderPassResolve = 25,
   EventInternalUnknown = 26,
   EventCmdDrawIndirectCount = 27,
   EventCmdDrawIndexedIndirectCount = 28,
   EventInvalid = 0xffffffff
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


/**
 * "User Event" RGP SQTT instrumentation marker (Table 8)
 */
struct rgp_sqtt_marker_user_event {
   union {
      struct {
         uint32_t identifier : 4;
         uint32_t reserved0 : 8;
         uint32_t data_type : 8;
         uint32_t reserved1 : 12;
      };
      uint32_t dword01;
   };
};
struct rgp_sqtt_marker_user_event_with_length {
   struct rgp_sqtt_marker_user_event user_event;
   uint32_t length;
};

static_assert(sizeof(struct rgp_sqtt_marker_user_event) == 4,
              "rgp_sqtt_marker_user_event doesn't match RGP spec");

enum rgp_sqtt_marker_user_event_type
{
   UserEventTrigger = 0,
   UserEventPop,
   UserEventPush,
   UserEventObjectName,
};

/**
 * "Pipeline bind" RGP SQTT instrumentation marker (Table 12)
 */
struct rgp_sqtt_marker_pipeline_bind {
   union {
      struct {
         uint32_t identifier : 4;
         uint32_t ext_dwords : 3;
         uint32_t bind_point : 1;
         uint32_t cb_id : 20;
         uint32_t reserved : 4;
      };
      uint32_t dword01;
   };
   union {
      uint32_t api_pso_hash[2];
      struct {
         uint32_t dword02;
         uint32_t dword03;
      };
   };
};

static_assert(sizeof(struct rgp_sqtt_marker_pipeline_bind) == 12,
              "rgp_sqtt_marker_pipeline_bind doesn't match RGP spec");


bool ac_sqtt_add_pso_correlation(struct ac_thread_trace_data *thread_trace_data,
                                 uint64_t pipeline_hash);

bool ac_sqtt_add_code_object_loader_event(struct ac_thread_trace_data *thread_trace_data,
                                          uint64_t pipeline_hash,
                                          uint64_t base_address);

#endif
