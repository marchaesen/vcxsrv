/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_CMD_BUFFER_H
#define TU_CMD_BUFFER_H

#include "tu_common.h"

#include "tu_cs.h"
#include "tu_descriptor_set.h"
#include "tu_device.h"
#include "tu_lrz.h"
#include "tu_pass.h"
#include "tu_pipeline.h"

enum tu_draw_state_group_id
{
   TU_DRAW_STATE_PROGRAM_CONFIG,
   TU_DRAW_STATE_PROGRAM,
   TU_DRAW_STATE_PROGRAM_BINNING,
   TU_DRAW_STATE_VB,
   TU_DRAW_STATE_RAST,
   TU_DRAW_STATE_CONST,
   TU_DRAW_STATE_DESC_SETS,
   TU_DRAW_STATE_DESC_SETS_LOAD,
   TU_DRAW_STATE_VS_PARAMS,
   TU_DRAW_STATE_INPUT_ATTACHMENTS_GMEM,
   TU_DRAW_STATE_INPUT_ATTACHMENTS_SYSMEM,
   TU_DRAW_STATE_LRZ_AND_DEPTH_PLANE,
   TU_DRAW_STATE_PRIM_MODE_GMEM,
   TU_DRAW_STATE_PRIM_MODE_SYSMEM,

   /* dynamic state related draw states */
   TU_DRAW_STATE_DYNAMIC,
   TU_DRAW_STATE_COUNT = TU_DRAW_STATE_DYNAMIC + TU_DYNAMIC_STATE_COUNT,
};

struct tu_descriptor_state
{
   struct tu_descriptor_set *sets[MAX_SETS];
   struct tu_descriptor_set push_set;
   uint32_t dynamic_descriptors[MAX_DYNAMIC_BUFFERS_SIZE];
   uint32_t max_sets_bound;
   bool dynamic_bound;
};

enum tu_cmd_dirty_bits
{
   TU_CMD_DIRTY_VERTEX_BUFFERS = BIT(0),
   TU_CMD_DIRTY_VB_STRIDE = BIT(1),
   TU_CMD_DIRTY_GRAS_SU_CNTL = BIT(2),
   TU_CMD_DIRTY_RB_DEPTH_CNTL = BIT(3),
   TU_CMD_DIRTY_RB_STENCIL_CNTL = BIT(4),
   TU_CMD_DIRTY_DESC_SETS_LOAD = BIT(5),
   TU_CMD_DIRTY_COMPUTE_DESC_SETS_LOAD = BIT(6),
   TU_CMD_DIRTY_SHADER_CONSTS = BIT(7),
   TU_CMD_DIRTY_LRZ = BIT(8),
   TU_CMD_DIRTY_VS_PARAMS = BIT(9),
   TU_CMD_DIRTY_RASTERIZER_DISCARD = BIT(10),
   TU_CMD_DIRTY_VIEWPORTS = BIT(11),
   TU_CMD_DIRTY_BLEND = BIT(12),
   /* all draw states were disabled and need to be re-enabled: */
   TU_CMD_DIRTY_DRAW_STATE = BIT(13)
};

/* There are only three cache domains we have to care about: the CCU, or
 * color cache unit, which is used for color and depth/stencil attachments
 * and copy/blit destinations, and is split conceptually into color and depth,
 * and the universal cache or UCHE which is used for pretty much everything
 * else, except for the CP (uncached) and host. We need to flush whenever data
 * crosses these boundaries.
 */

enum tu_cmd_access_mask {
   TU_ACCESS_UCHE_READ = 1 << 0,
   TU_ACCESS_UCHE_WRITE = 1 << 1,
   TU_ACCESS_CCU_COLOR_READ = 1 << 2,
   TU_ACCESS_CCU_COLOR_WRITE = 1 << 3,
   TU_ACCESS_CCU_DEPTH_READ = 1 << 4,
   TU_ACCESS_CCU_DEPTH_WRITE = 1 << 5,

   /* Experiments have shown that while it's safe to avoid flushing the CCU
    * after each blit/renderpass, it's not safe to assume that subsequent
    * lookups with a different attachment state will hit unflushed cache
    * entries. That is, the CCU needs to be flushed and possibly invalidated
    * when accessing memory with a different attachment state. Writing to an
    * attachment under the following conditions after clearing using the
    * normal 2d engine path is known to have issues:
    *
    * - It isn't the 0'th layer.
    * - There are more than one attachment, and this isn't the 0'th attachment
    *   (this seems to also depend on the cpp of the attachments).
    *
    * Our best guess is that the layer/MRT state is used when computing
    * the location of a cache entry in CCU, to avoid conflicts. We assume that
    * any access in a renderpass after or before an access by a transfer needs
    * a flush/invalidate, and use the _INCOHERENT variants to represent access
    * by a renderpass.
    */
   TU_ACCESS_CCU_COLOR_INCOHERENT_READ = 1 << 6,
   TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE = 1 << 7,
   TU_ACCESS_CCU_DEPTH_INCOHERENT_READ = 1 << 8,
   TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE = 1 << 9,

   /* Accesses which bypasses any cache. e.g. writes via the host,
    * CP_EVENT_WRITE::BLIT, and the CP are SYSMEM_WRITE.
    */
   TU_ACCESS_SYSMEM_READ = 1 << 10,
   TU_ACCESS_SYSMEM_WRITE = 1 << 11,

   /* Memory writes from the CP start in-order with draws and event writes,
    * but execute asynchronously and hence need a CP_WAIT_MEM_WRITES if read.
    */
   TU_ACCESS_CP_WRITE = 1 << 12,

   TU_ACCESS_READ =
      TU_ACCESS_UCHE_READ |
      TU_ACCESS_CCU_COLOR_READ |
      TU_ACCESS_CCU_DEPTH_READ |
      TU_ACCESS_CCU_COLOR_INCOHERENT_READ |
      TU_ACCESS_CCU_DEPTH_INCOHERENT_READ |
      TU_ACCESS_SYSMEM_READ,

   TU_ACCESS_WRITE =
      TU_ACCESS_UCHE_WRITE |
      TU_ACCESS_CCU_COLOR_WRITE |
      TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE |
      TU_ACCESS_CCU_DEPTH_WRITE |
      TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE |
      TU_ACCESS_SYSMEM_WRITE |
      TU_ACCESS_CP_WRITE,

   TU_ACCESS_ALL =
      TU_ACCESS_READ |
      TU_ACCESS_WRITE,
};

/* Starting with a6xx, the pipeline is split into several "clusters" (really
 * pipeline stages). Each stage has its own pair of register banks and can
 * switch them independently, so that earlier stages can run ahead of later
 * ones. e.g. the FS of draw N and the VS of draw N + 1 can be executing at
 * the same time.
 *
 * As a result of this, we need to insert a WFI when an earlier stage depends
 * on the result of a later stage. CP_DRAW_* and CP_BLIT will wait for any
 * pending WFI's to complete before starting, and usually before reading
 * indirect params even, so a WFI also acts as a full "pipeline stall".
 *
 * Note, the names of the stages come from CLUSTER_* in devcoredump. We
 * include all the stages for completeness, even ones which do not read/write
 * anything.
 */

enum tu_stage {
   /* This doesn't correspond to a cluster, but we need it for tracking
    * indirect draw parameter reads etc.
    */
   TU_STAGE_CP,

   /* - Fetch index buffer
    * - Fetch vertex attributes, dispatch VS
    */
   TU_STAGE_FE,

   /* Execute all geometry stages (VS thru GS) */
   TU_STAGE_SP_VS,

   /* Write to VPC, do primitive assembly. */
   TU_STAGE_PC_VS,

   /* Rasterization. RB_DEPTH_BUFFER_BASE only exists in CLUSTER_PS according
    * to devcoredump so presumably this stage stalls for TU_STAGE_PS when
    * early depth testing is enabled before dispatching fragments? However
    * GRAS reads and writes LRZ directly.
    */
   TU_STAGE_GRAS,

   /* Execute FS */
   TU_STAGE_SP_PS,

   /* - Fragment tests
    * - Write color/depth
    * - Streamout writes (???)
    * - Varying interpolation (???)
    */
   TU_STAGE_PS,
};

enum tu_cmd_flush_bits {
   TU_CMD_FLAG_CCU_FLUSH_DEPTH = 1 << 0,
   TU_CMD_FLAG_CCU_FLUSH_COLOR = 1 << 1,
   TU_CMD_FLAG_CCU_INVALIDATE_DEPTH = 1 << 2,
   TU_CMD_FLAG_CCU_INVALIDATE_COLOR = 1 << 3,
   TU_CMD_FLAG_CACHE_FLUSH = 1 << 4,
   TU_CMD_FLAG_CACHE_INVALIDATE = 1 << 5,
   TU_CMD_FLAG_WAIT_MEM_WRITES = 1 << 6,
   TU_CMD_FLAG_WAIT_FOR_IDLE = 1 << 7,
   TU_CMD_FLAG_WAIT_FOR_ME = 1 << 8,

   TU_CMD_FLAG_ALL_FLUSH =
      TU_CMD_FLAG_CCU_FLUSH_DEPTH |
      TU_CMD_FLAG_CCU_FLUSH_COLOR |
      TU_CMD_FLAG_CACHE_FLUSH |
      /* Treat the CP as a sort of "cache" which may need to be "flushed" via
       * waiting for writes to land with WAIT_FOR_MEM_WRITES.
       */
      TU_CMD_FLAG_WAIT_MEM_WRITES,

   TU_CMD_FLAG_ALL_INVALIDATE =
      TU_CMD_FLAG_CCU_INVALIDATE_DEPTH |
      TU_CMD_FLAG_CCU_INVALIDATE_COLOR |
      TU_CMD_FLAG_CACHE_INVALIDATE |
      /* Treat CP_WAIT_FOR_ME as a "cache" that needs to be invalidated when a
       * a command that needs CP_WAIT_FOR_ME is executed. This means we may
       * insert an extra WAIT_FOR_ME before an indirect command requiring it
       * in case there was another command before the current command buffer
       * that it needs to wait for.
       */
      TU_CMD_FLAG_WAIT_FOR_ME,
};

/* Changing the CCU from sysmem mode to gmem mode or vice-versa is pretty
 * heavy, involving a CCU cache flush/invalidate and a WFI in order to change
 * which part of the gmem is used by the CCU. Here we keep track of what the
 * state of the CCU.
 */
enum tu_cmd_ccu_state {
   TU_CMD_CCU_SYSMEM,
   TU_CMD_CCU_GMEM,
   TU_CMD_CCU_UNKNOWN,
};

struct tu_cache_state {
   /* Caches which must be made available (flushed) eventually if there are
    * any users outside that cache domain, and caches which must be
    * invalidated eventually if there are any reads.
    */
   enum tu_cmd_flush_bits pending_flush_bits;
   /* Pending flushes */
   enum tu_cmd_flush_bits flush_bits;
};

struct tu_vs_params {
   uint32_t vertex_offset;
   uint32_t first_instance;
   uint32_t draw_id;
};

/* This should be for state that is set inside a renderpass and used at
 * renderpass end time, e.g. to decide whether to use sysmem. This needs
 * special handling for secondary cmdbufs and suspending/resuming render
 * passes where the state may need to be combined afterwards.
 */
struct tu_render_pass_state
{
   bool xfb_used;
   bool has_tess;
   bool has_prim_generated_query_in_rp;
   bool disable_gmem;
   bool sysmem_single_prim_mode;

   /* Track whether conditional predicate for COND_REG_EXEC is changed in draw_cs */
   bool draw_cs_writes_to_cond_pred;

   uint32_t drawcall_count;

   /* A calculated "draw cost" value for renderpass, which tries to
    * estimate the bandwidth-per-sample of all the draws according
    * to:
    *
    *    foreach_draw (...) {
    *      sum += pipeline->color_bandwidth_per_sample;
    *      if (depth_test_enabled)
    *        sum += pipeline->depth_cpp_per_sample;
    *      if (depth_write_enabled)
    *        sum += pipeline->depth_cpp_per_sample;
    *      if (stencil_write_enabled)
    *        sum += pipeline->stencil_cpp_per_sample * 2;
    *    }
    *    drawcall_bandwidth_per_sample = sum / drawcall_count;
    *
    * It allows us to estimate the total bandwidth of drawcalls later, by
    * calculating (drawcall_bandwidth_per_sample * zpass_sample_count).
    *
    * This does ignore depth buffer traffic for samples which do not
    * pass due to depth-test fail, and some other details.  But it is
    * just intended to be a rough estimate that is easy to calculate.
    */
   uint32_t drawcall_bandwidth_per_sample_sum;
};

struct tu_cmd_state
{
   uint32_t dirty;

   struct tu_pipeline *pipeline;
   struct tu_pipeline *compute_pipeline;

   struct tu_render_pass_state rp;

   /* Vertex buffers, viewports, and scissors
    * the states for these can be updated partially, so we need to save these
    * to be able to emit a complete draw state
    */
   struct {
      uint64_t base;
      uint32_t size;
      uint32_t stride;
   } vb[MAX_VBS];

   uint32_t max_vbs_bound;

   VkViewport viewport[MAX_VIEWPORTS];
   VkRect2D scissor[MAX_SCISSORS];
   uint32_t max_viewport, max_scissor;

   /* for dynamic states that can't be emitted directly */
   uint32_t dynamic_stencil_mask;
   uint32_t dynamic_stencil_wrmask;
   uint32_t dynamic_stencil_ref;

   uint32_t gras_su_cntl, rb_depth_cntl, rb_stencil_cntl;
   uint32_t pc_raster_cntl, vpc_unknown_9107;
   uint32_t rb_mrt_control[MAX_RTS], rb_mrt_blend_control[MAX_RTS];
   uint32_t rb_mrt_control_rop;
   uint32_t rb_blend_cntl, sp_blend_cntl;
   uint32_t pipeline_color_write_enable, pipeline_blend_enable;
   uint32_t color_write_enable;
   bool logic_op_enabled;
   bool rop_reads_dst;
   enum pc_di_primtype primtype;
   bool primitive_restart_enable;

   /* saved states to re-emit in TU_CMD_DIRTY_DRAW_STATE case */
   struct tu_draw_state dynamic_state[TU_DYNAMIC_STATE_COUNT];
   struct tu_draw_state vertex_buffers;
   struct tu_draw_state shader_const;
   struct tu_draw_state desc_sets;

   struct tu_draw_state vs_params;

   /* Index buffer */
   uint64_t index_va;
   uint32_t max_index_count;
   uint8_t index_size;

   /* because streamout base has to be 32-byte aligned
    * there is an extra offset to deal with when it is
    * unaligned
    */
   uint8_t streamout_offset[IR3_MAX_SO_BUFFERS];

   /* Renderpasses are tricky, because we may need to flush differently if
    * using sysmem vs. gmem and therefore we have to delay any flushing that
    * happens before a renderpass. So we have to have two copies of the flush
    * state, one for intra-renderpass flushes (i.e. renderpass dependencies)
    * and one for outside a renderpass.
    */
   struct tu_cache_state cache;
   struct tu_cache_state renderpass_cache;

   enum tu_cmd_ccu_state ccu_state;

   /* Decides which GMEM layout to use from the tu_pass, based on whether the CCU
    * might get used by tu_store_gmem_attachment().
    */
   enum tu_gmem_layout gmem_layout;

   const struct tu_render_pass *pass;
   const struct tu_subpass *subpass;
   const struct tu_framebuffer *framebuffer;
   const struct tu_tiling_config *tiling;
   VkRect2D render_area;

   const struct tu_image_view **attachments;

   /* State that in the dynamic case comes from VkRenderingInfo and needs to
    * be saved/restored when suspending. This holds the state for the last
    * suspended renderpass, which may point to this command buffer's dynamic_*
    * or another command buffer if executed on a secondary.
    */
   struct {
      const struct tu_render_pass *pass;
      const struct tu_subpass *subpass;
      const struct tu_framebuffer *framebuffer;
      VkRect2D render_area;
      enum tu_gmem_layout gmem_layout;

      const struct tu_image_view **attachments;

      struct tu_lrz_state lrz;
   } suspended_pass;

   bool tessfactor_addr_set;
   bool predication_active;
   enum a5xx_line_mode line_mode;
   bool z_negative_one_to_one;

   /* VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT and
    * VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT are allowed to run simultaniously,
    * but they use the same {START,STOP}_PRIMITIVE_CTRS control.
    */
   uint32_t prim_counters_running;

   bool prim_generated_query_running_before_rp;

   /* These are the states of the suspend/resume state machine. In addition to
    * tracking whether we're in the middle of a chain of suspending and
    * resuming passes that will be merged, we need to track whether the
    * command buffer begins in the middle of such a chain, for when it gets
    * merged with other command buffers. We call such a chain that begins
    * before the command buffer starts a "pre-chain".
    *
    * Note that when this command buffer is finished, this state is untouched
    * but it gains a different meaning. For example, if we finish in state
    * SR_IN_CHAIN, we finished in the middle of a suspend/resume chain, so
    * there's a suspend/resume chain that extends past the end of the command
    * buffer. In this sense it's the "opposite" of SR_AFTER_PRE_CHAIN, which
    * means that there's a suspend/resume chain that extends before the
    * beginning.
    */
   enum {
      /* Either there are no suspend/resume chains, or they are entirely
       * contained in the current command buffer.
       *
       *   BeginCommandBuffer() <- start of current command buffer
       *       ...
       *       // we are here
       */
      SR_NONE = 0,

      /* We are in the middle of a suspend/resume chain that starts before the
       * current command buffer. This happens when the command buffer begins
       * with a resuming render pass and all of the passes up to the current
       * one are suspending. In this state, our part of the chain is not saved
       * and is in the current draw_cs/state.
       *
       *   BeginRendering() ... EndRendering(suspending)
       *   BeginCommandBuffer() <- start of current command buffer
       *       BeginRendering(resuming) ... EndRendering(suspending)
       *       BeginRendering(resuming) ... EndRendering(suspending)
       *       ...
       *       // we are here
       */
      SR_IN_PRE_CHAIN,

      /* We are currently outside of any suspend/resume chains, but there is a
       * chain starting before the current command buffer. It is saved in
       * pre_chain.
       *
       *   BeginRendering() ... EndRendering(suspending)
       *   BeginCommandBuffer() <- start of current command buffer
       *       // This part is stashed in pre_chain
       *       BeginRendering(resuming) ... EndRendering(suspending)
       *       BeginRendering(resuming) ... EndRendering(suspending)
       *       ...
       *       BeginRendering(resuming) ... EndRendering() // end of chain
       *       ...
       *       // we are here
       */
      SR_AFTER_PRE_CHAIN,

      /* We are in the middle of a suspend/resume chain and there is no chain
       * starting before the current command buffer.
       *
       *   BeginCommandBuffer() <- start of current command buffer
       *       ...
       *       BeginRendering() ... EndRendering(suspending)
       *       BeginRendering(resuming) ... EndRendering(suspending)
       *       BeginRendering(resuming) ... EndRendering(suspending)
       *       ...
       *       // we are here
       */
      SR_IN_CHAIN,

      /* We are in the middle of a suspend/resume chain and there is another,
       * separate, chain starting before the current command buffer.
       *
       *   BeginRendering() ... EndRendering(suspending)
       *   CommandBufferBegin() <- start of current command buffer
       *       // This part is stashed in pre_chain
       *       BeginRendering(resuming) ... EndRendering(suspending)
       *       BeginRendering(resuming) ... EndRendering(suspending)
       *       ...
       *       BeginRendering(resuming) ... EndRendering() // end of chain
       *       ...
       *       BeginRendering() ... EndRendering(suspending)
       *       BeginRendering(resuming) ... EndRendering(suspending)
       *       BeginRendering(resuming) ... EndRendering(suspending)
       *       ...
       *       // we are here
       */
      SR_IN_CHAIN_AFTER_PRE_CHAIN,
   } suspend_resume;

   bool suspending, resuming;

   struct tu_lrz_state lrz;

   struct tu_draw_state lrz_and_depth_plane_state;

   struct tu_vs_params last_vs_params;
};

enum tu_cmd_buffer_status
{
   TU_CMD_BUFFER_STATUS_INVALID,
   TU_CMD_BUFFER_STATUS_INITIAL,
   TU_CMD_BUFFER_STATUS_RECORDING,
   TU_CMD_BUFFER_STATUS_EXECUTABLE,
   TU_CMD_BUFFER_STATUS_PENDING,
};

struct tu_cmd_buffer
{
   struct vk_command_buffer vk;

   struct tu_device *device;

   struct u_trace trace;
   struct u_trace_iterator trace_renderpass_start;
   struct u_trace_iterator trace_renderpass_end;

   struct list_head renderpass_autotune_results;
   struct tu_autotune_results_buffer* autotune_buffer;

   VkCommandBufferUsageFlags usage_flags;
   enum tu_cmd_buffer_status status;

   VkQueryPipelineStatisticFlags inherited_pipeline_statistics;

   struct tu_cmd_state state;
   uint32_t queue_family_index;

   uint32_t push_constants[MAX_PUSH_CONSTANTS_SIZE / 4];
   VkShaderStageFlags push_constant_stages;
   struct tu_descriptor_set meta_push_descriptors;

   struct tu_descriptor_state descriptors[MAX_BIND_POINTS];

   struct tu_render_pass_attachment dynamic_rp_attachments[2 * (MAX_RTS + 1)];
   struct tu_subpass_attachment dynamic_color_attachments[MAX_RTS];
   struct tu_subpass_attachment dynamic_resolve_attachments[MAX_RTS + 1];
   const struct tu_image_view *dynamic_attachments[2 * (MAX_RTS + 1)];

   struct tu_render_pass dynamic_pass;
   struct tu_subpass dynamic_subpass;
   struct tu_framebuffer dynamic_framebuffer;

   struct tu_cs cs;
   struct tu_cs draw_cs;
   struct tu_cs tile_store_cs;
   struct tu_cs draw_epilogue_cs;
   struct tu_cs sub_cs;

   /* If the first render pass in the command buffer is resuming, then it is
    * part of a suspend/resume chain that starts before the current command
    * buffer and needs to be merged later. In this case, its incomplete state
    * is stored in pre_chain. In the symmetric case where the last render pass
    * is suspending, we just skip ending the render pass and its state is
    * stored in draw_cs/the current state. The first and last render pass
    * might be part of different chains, which is why all the state may need
    * to be saved separately here.
    */
   struct {
      struct tu_cs draw_cs;
      struct tu_cs draw_epilogue_cs;

      struct u_trace_iterator trace_renderpass_start, trace_renderpass_end;

      struct tu_render_pass_state state;
   } pre_chain;

   uint32_t vsc_draw_strm_pitch;
   uint32_t vsc_prim_strm_pitch;
};
VK_DEFINE_HANDLE_CASTS(tu_cmd_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

extern const struct vk_command_buffer_ops tu_cmd_buffer_ops;

static inline uint32_t
tu_attachment_gmem_offset(struct tu_cmd_buffer *cmd,
                          const struct tu_render_pass_attachment *att)
{
   assert(cmd->state.gmem_layout < TU_GMEM_LAYOUT_COUNT);
   return att->gmem_offset[cmd->state.gmem_layout];
}

static inline uint32_t
tu_attachment_gmem_offset_stencil(struct tu_cmd_buffer *cmd,
                                  const struct tu_render_pass_attachment *att)
{
   assert(cmd->state.gmem_layout < TU_GMEM_LAYOUT_COUNT);
   return att->gmem_offset_stencil[cmd->state.gmem_layout];
}

void tu_render_pass_state_merge(struct tu_render_pass_state *dst,
                                const struct tu_render_pass_state *src);

VkResult tu_cmd_buffer_begin(struct tu_cmd_buffer *cmd_buffer,
                             VkCommandBufferUsageFlags usage_flags);

void tu_emit_cache_flush_renderpass(struct tu_cmd_buffer *cmd_buffer,
                                    struct tu_cs *cs);

void tu_emit_cache_flush_ccu(struct tu_cmd_buffer *cmd_buffer,
                             struct tu_cs *cs,
                             enum tu_cmd_ccu_state ccu_state);

void
tu_append_pre_chain(struct tu_cmd_buffer *cmd,
                    struct tu_cmd_buffer *secondary);

void
tu_append_pre_post_chain(struct tu_cmd_buffer *cmd,
                         struct tu_cmd_buffer *secondary);

void
tu_append_post_chain(struct tu_cmd_buffer *cmd,
                     struct tu_cmd_buffer *secondary);

void
tu_restore_suspended_pass(struct tu_cmd_buffer *cmd,
                          struct tu_cmd_buffer *suspended);

void tu_cmd_render(struct tu_cmd_buffer *cmd);

void
tu6_emit_event_write(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     enum vgt_event_type event);

static inline struct tu_descriptor_state *
tu_get_descriptors_state(struct tu_cmd_buffer *cmd_buffer,
                         VkPipelineBindPoint bind_point)
{
   return &cmd_buffer->descriptors[bind_point];
}

void tu6_emit_msaa(struct tu_cs *cs, VkSampleCountFlagBits samples,
                   enum a5xx_line_mode line_mode);

void tu6_emit_window_scissor(struct tu_cs *cs, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2);

void tu6_emit_window_offset(struct tu_cs *cs, uint32_t x1, uint32_t y1);

void tu_disable_draw_states(struct tu_cmd_buffer *cmd, struct tu_cs *cs);

void tu6_apply_depth_bounds_workaround(struct tu_device *device,
                                       uint32_t *rb_depth_cntl);

void
update_stencil_mask(uint32_t *value, VkStencilFaceFlags face, uint32_t mask);

#endif /* TU_CMD_BUFFER_H */
