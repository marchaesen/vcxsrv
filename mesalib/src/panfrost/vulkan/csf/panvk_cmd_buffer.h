/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_BUFFER_H
#define PANVK_CMD_BUFFER_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "genxml/cs_builder.h"

#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_dispatch.h"
#include "panvk_cmd_draw.h"
#include "panvk_cmd_push_constant.h"
#include "panvk_queue.h"

#include "vk_command_buffer.h"

#include "util/list.h"
#include "util/perf/u_trace.h"

#define MAX_VBS 16
#define MAX_RTS 8
#define MAX_LAYERS_PER_TILER_DESC 8

struct panvk_cs_sync32 {
   uint32_t seqno;
   uint32_t error;
};

struct panvk_cs_sync64 {
   uint64_t seqno;
   uint32_t error;
   uint32_t pad;
};

struct panvk_cs_desc_ringbuf {
   uint64_t syncobj;
   uint64_t ptr;
   uint32_t pos;
   uint32_t pad;
};

enum panvk_incremental_rendering_pass {
   PANVK_IR_FIRST_PASS,
   PANVK_IR_MIDDLE_PASS,
   PANVK_IR_LAST_PASS,
   PANVK_IR_PASS_COUNT
};

static inline uint32_t
get_tiler_oom_handler_idx(bool has_zs_ext, uint32_t rt_count)
{
   assert(rt_count >= 1 && rt_count <= MAX_RTS);
   uint32_t idx = has_zs_ext * MAX_RTS + (rt_count - 1);
   assert(idx < 2 * MAX_RTS);
   return idx;
}

static inline uint32_t
get_fbd_size(bool has_zs_ext, uint32_t rt_count)
{
   assert(rt_count >= 1 && rt_count <= MAX_RTS);
   uint32_t fbd_size = pan_size(FRAMEBUFFER);
   if (has_zs_ext)
      fbd_size += pan_size(ZS_CRC_EXTENSION);
   fbd_size += pan_size(RENDER_TARGET) * rt_count;
   return fbd_size;
}

/* 512k of render descriptors that can be used when
 * VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT is set on the command buffer. */
#define RENDER_DESC_RINGBUF_SIZE (512 * 1024)

/* Helper defines to get specific fields in the tiler_oom_ctx. */
#define TILER_OOM_CTX_FIELD_OFFSET(_name)                                      \
   offsetof(struct panvk_cs_subqueue_context, tiler_oom_ctx._name)
#define TILER_OOM_CTX_FBDPTR_OFFSET(_pass)                                     \
   (TILER_OOM_CTX_FIELD_OFFSET(fbds) +                                         \
    (PANVK_IR_##_pass##_PASS * sizeof(uint64_t)))

struct panvk_cs_occlusion_query {
   uint64_t next;
   uint64_t syncobj;
};

struct panvk_cs_subqueue_context {
   uint64_t syncobjs;
   uint32_t iter_sb;
   uint32_t pad;
   struct {
      struct panvk_cs_desc_ringbuf desc_ringbuf;
      uint64_t tiler_heap;
      uint64_t geom_buf;
      uint64_t oq_chain;
   } render;
   struct {
      uint32_t counter;
      uint64_t fbds[PANVK_IR_PASS_COUNT];
      uint32_t td_count;
      uint32_t layer_count;
      uint64_t reg_dump_addr;
   } tiler_oom_ctx;
   struct {
      uint64_t syncobjs;
      struct {
         uint64_t cs;
      } tracebuf;
   } debug;
} __attribute__((aligned(64)));

struct panvk_cache_flush_info {
   enum mali_cs_flush_mode l2;
   enum mali_cs_flush_mode lsc;
   bool others;
};

struct panvk_cs_deps {
   bool needs_draw_flush;
   struct {
      uint32_t wait_sb_mask;
      struct panvk_cache_flush_info cache_flush;
   } src[PANVK_SUBQUEUE_COUNT];

   struct {
      uint32_t wait_subqueue_mask;
   } dst[PANVK_SUBQUEUE_COUNT];
};

enum panvk_sb_ids {
   PANVK_SB_LS = 0,
   PANVK_SB_IMM_FLUSH = 0,
   PANVK_SB_DEFERRED_SYNC = 1,
   PANVK_SB_DEFERRED_FLUSH = 2,
   PANVK_SB_ITER_START = 3,
   PANVK_SB_ITER_COUNT = 5,
};

#define SB_IMM_MASK     0
#define SB_MASK(nm)     BITFIELD_BIT(PANVK_SB_##nm)
#define SB_ID(nm)       PANVK_SB_##nm
#define SB_ITER(x)      (PANVK_SB_ITER_START + (x))
#define SB_WAIT_ITER(x) BITFIELD_BIT(PANVK_SB_ITER_START + (x))
#define SB_ALL_ITERS_MASK                                                      \
   BITFIELD_RANGE(PANVK_SB_ITER_START, PANVK_SB_ITER_COUNT)
#define SB_ALL_MASK     BITFIELD_MASK(8)

static inline uint32_t
next_iter_sb(uint32_t sb)
{
   return sb + 1 < PANVK_SB_ITER_COUNT ? sb + 1 : 0;
}

enum panvk_cs_regs {
   /* RUN_IDVS staging regs. */
   PANVK_CS_REG_RUN_IDVS_SR_START = 0,
   PANVK_CS_REG_RUN_IDVS_SR_END = 60,

   /* RUN_FRAGMENT staging regs.
    * SW ABI:
    * - r38:39 contain the pointer to the first tiler descriptor. This is
    *   needed to gather completed heap chunks after a run_fragment.
    */
   PANVK_CS_REG_RUN_FRAGMENT_SR_START = 38,
   PANVK_CS_REG_RUN_FRAGMENT_SR_END = 46,

   /* RUN_COMPUTE staging regs. */
   PANVK_CS_REG_RUN_COMPUTE_SR_START = 0,
   PANVK_CS_REG_RUN_COMPUTE_SR_END = 39,

   /* Range of registers that can be used to store temporary data on
    * all queues. Note that some queues have extra space they can use
    * as scratch space.*/
   PANVK_CS_REG_SCRATCH_START = 66,
   PANVK_CS_REG_SCRATCH_END = 83,

   /* Driver context. */
   PANVK_CS_REG_PROGRESS_SEQNO_START = 84,
   PANVK_CS_REG_PROGRESS_SEQNO_END = 89,
   PANVK_CS_REG_SUBQUEUE_CTX_START = 90,
   PANVK_CS_REG_SUBQUEUE_CTX_END = 91,
};

#define CS_REG_SCRATCH_COUNT                                                   \
   (PANVK_CS_REG_SCRATCH_END - PANVK_CS_REG_SCRATCH_START + 1)

static inline struct cs_index
cs_scratch_reg_tuple(struct cs_builder *b, unsigned start, unsigned count)
{
   assert(start + count <= CS_REG_SCRATCH_COUNT);
   return cs_reg_tuple(b, PANVK_CS_REG_SCRATCH_START + start, count);
}

static inline struct cs_index
cs_scratch_reg32(struct cs_builder *b, unsigned reg)
{
   return cs_scratch_reg_tuple(b, reg, 1);
}

static inline struct cs_index
cs_scratch_reg64(struct cs_builder *b, unsigned reg)
{
   assert(reg % 2 == 0);
   return cs_scratch_reg_tuple(b, reg, 2);
}

static inline struct cs_index
cs_sr_reg_tuple(struct cs_builder *b, unsigned start, unsigned count)
{
   assert(start + count - 1 < PANVK_CS_REG_SCRATCH_START);
   return cs_reg_tuple(b, start, count);
}

static inline struct cs_index
cs_sr_reg32(struct cs_builder *b, unsigned reg)
{
   return cs_sr_reg_tuple(b, reg, 1);
}

static inline struct cs_index
cs_sr_reg64(struct cs_builder *b, unsigned reg)
{
   assert(reg % 2 == 0);
   return cs_sr_reg_tuple(b, reg, 2);
}

static inline struct cs_index
cs_subqueue_ctx_reg(struct cs_builder *b)
{
   return cs_reg64(b, PANVK_CS_REG_SUBQUEUE_CTX_START);
}

static inline struct cs_index
cs_progress_seqno_reg(struct cs_builder *b, enum panvk_subqueue_id subqueue)
{
   assert(PANVK_CS_REG_PROGRESS_SEQNO_START + (subqueue * 2) <
          PANVK_CS_REG_PROGRESS_SEQNO_END);
   return cs_reg64(b, PANVK_CS_REG_PROGRESS_SEQNO_START + (subqueue * 2));
}

struct panvk_cs_reg_upd_context {
   reg_perm_cb_t reg_perm;
   struct panvk_cs_reg_upd_context *next;
};

struct panvk_cs_state {
   struct cs_builder builder;

   struct cs_load_store_tracker ls_tracker;

   /* Used to debug register writes in invalid contexts. */
   struct {
      struct panvk_cs_reg_upd_context *upd_ctx_stack;
      reg_perm_cb_t base_perm;
   } reg_access;

   /* Sync point relative to the beginning of the command buffer.
    * Needs to be offset with the subqueue sync point. */
   int32_t relative_sync_point;

   struct cs_tracing_ctx tracing;
};

static inline struct panvk_cs_reg_upd_context *
panvk_cs_reg_ctx_push(struct cs_builder *b,
                      struct panvk_cs_reg_upd_context *ctx,
                      reg_perm_cb_t reg_perm)
{
   struct panvk_cs_state *cs_state =
      container_of(b, struct panvk_cs_state, builder);

   ctx->reg_perm = reg_perm;
   ctx->next = cs_state->reg_access.upd_ctx_stack;
   cs_state->reg_access.upd_ctx_stack = ctx;
   return ctx;
}

static inline void
panvk_cs_reg_ctx_pop(struct cs_builder *b, struct panvk_cs_reg_upd_context *ctx)
{
   struct panvk_cs_state *cs_state =
      container_of(b, struct panvk_cs_state, builder);

   assert(cs_state->reg_access.upd_ctx_stack == ctx);

   cs_state->reg_access.upd_ctx_stack = ctx->next;
}

struct panvk_cs_reg_range {
   unsigned start;
   unsigned end;
};

#define PANVK_CS_REG_RANGE(__name)                                             \
   {                                                                           \
      .start = PANVK_CS_REG_##__name##_START,                                  \
      .end = PANVK_CS_REG_##__name##_END,                                      \
   }

#define panvk_cs_reg_blacklist(__name, ...)                                    \
   static inline enum cs_reg_perm panvk_cs_##__name##_reg_perm(                \
      struct cs_builder *b, unsigned reg)                                      \
   {                                                                           \
      const struct panvk_cs_reg_range ranges[] = {                             \
         __VA_ARGS__,                                                          \
      };                                                                       \
                                                                               \
      for (unsigned i = 0; i < ARRAY_SIZE(ranges); i++) {                      \
         if (reg >= ranges[i].start && reg <= ranges[i].end)                   \
            return CS_REG_RD;                                                  \
      }                                                                        \
                                                                               \
      return CS_REG_RW;                                                        \
   }

panvk_cs_reg_blacklist(vt, PANVK_CS_REG_RANGE(RUN_IDVS_SR),
                       PANVK_CS_REG_RANGE(PROGRESS_SEQNO),
                       PANVK_CS_REG_RANGE(SUBQUEUE_CTX));
panvk_cs_reg_blacklist(frag, PANVK_CS_REG_RANGE(RUN_FRAGMENT_SR),
                       PANVK_CS_REG_RANGE(PROGRESS_SEQNO),
                       PANVK_CS_REG_RANGE(SUBQUEUE_CTX));
panvk_cs_reg_blacklist(compute, PANVK_CS_REG_RANGE(RUN_COMPUTE_SR),
                       PANVK_CS_REG_RANGE(PROGRESS_SEQNO),
                       PANVK_CS_REG_RANGE(SUBQUEUE_CTX));

#define panvk_cs_reg_whitelist(__name, ...)                                    \
   static inline enum cs_reg_perm panvk_cs_##__name##_reg_perm(                \
      struct cs_builder *b, unsigned reg)                                      \
   {                                                                           \
      const struct panvk_cs_reg_range ranges[] = {                             \
         __VA_ARGS__,                                                          \
      };                                                                       \
                                                                               \
      for (unsigned i = 0; i < ARRAY_SIZE(ranges); i++) {                      \
         if (reg >= ranges[i].start && reg <= ranges[i].end)                   \
            return CS_REG_RW;                                                  \
      }                                                                        \
                                                                               \
      return CS_REG_RD;                                                        \
   }

#define panvk_cs_reg_upd_ctx(__b, __name)                                      \
   for (struct panvk_cs_reg_upd_context __reg_upd_ctx,                         \
        *reg_upd_ctxp = panvk_cs_reg_ctx_push(__b, &__reg_upd_ctx,             \
                                              panvk_cs_##__name##_reg_perm);   \
        reg_upd_ctxp;                                                          \
        panvk_cs_reg_ctx_pop(__b, &__reg_upd_ctx), reg_upd_ctxp = NULL)

panvk_cs_reg_whitelist(progress_seqno, PANVK_CS_REG_RANGE(PROGRESS_SEQNO));
#define cs_update_progress_seqno(__b) panvk_cs_reg_upd_ctx(__b, progress_seqno)

panvk_cs_reg_whitelist(compute_ctx, PANVK_CS_REG_RANGE(RUN_COMPUTE_SR));
#define cs_update_compute_ctx(__b) panvk_cs_reg_upd_ctx(__b, compute_ctx)

panvk_cs_reg_whitelist(frag_ctx, PANVK_CS_REG_RANGE(RUN_FRAGMENT_SR));
#define cs_update_frag_ctx(__b) panvk_cs_reg_upd_ctx(__b, frag_ctx)

panvk_cs_reg_whitelist(vt_ctx, PANVK_CS_REG_RANGE(RUN_IDVS_SR));
#define cs_update_vt_ctx(__b) panvk_cs_reg_upd_ctx(__b, vt_ctx)

panvk_cs_reg_whitelist(cmdbuf_regs, {PANVK_CS_REG_RUN_IDVS_SR_START,
                                     PANVK_CS_REG_SCRATCH_END});
#define cs_update_cmdbuf_regs(__b) panvk_cs_reg_upd_ctx(__b, cmdbuf_regs)

struct panvk_tls_state {
   struct panfrost_ptr desc;
   struct pan_tls_info info;
   unsigned max_wg_count;
};

struct panvk_cmd_buffer {
   struct vk_command_buffer vk;
   VkCommandBufferUsageFlags flags;
   struct panvk_pool cs_pool;
   struct panvk_pool desc_pool;
   struct panvk_pool tls_pool;
   struct list_head push_sets;

   uint32_t flush_id;

   struct {
      struct u_trace uts[PANVK_SUBQUEUE_COUNT];
   } utrace;

   struct {
      struct panvk_cmd_graphics_state gfx;
      struct panvk_cmd_compute_state compute;
      struct panvk_push_constant_state push_constants;
      struct panvk_cs_state cs[PANVK_SUBQUEUE_COUNT];
      struct panvk_tls_state tls;
   } state;
};

VK_DEFINE_HANDLE_CASTS(panvk_cmd_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

static bool
inherits_render_ctx(struct panvk_cmd_buffer *cmdbuf)
{
   return (cmdbuf->vk.level == VK_COMMAND_BUFFER_LEVEL_SECONDARY &&
           (cmdbuf->flags &
            VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)) ||
          (cmdbuf->state.gfx.render.flags & VK_RENDERING_RESUMING_BIT);
}

static inline struct cs_builder *
panvk_get_cs_builder(struct panvk_cmd_buffer *cmdbuf, uint32_t subqueue)
{
   return &cmdbuf->state.cs[subqueue].builder;
}

static inline struct panvk_descriptor_state *
panvk_cmd_get_desc_state(struct panvk_cmd_buffer *cmdbuf,
                         VkPipelineBindPoint bindpoint)
{
   switch (bindpoint) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      return &cmdbuf->state.gfx.desc_state;

   case VK_PIPELINE_BIND_POINT_COMPUTE:
      return &cmdbuf->state.compute.desc_state;

   default:
      assert(!"Unsupported bind point");
      return NULL;
   }
}

extern const struct vk_command_buffer_ops panvk_per_arch(cmd_buffer_ops);

void panvk_per_arch(cmd_flush_draws)(struct panvk_cmd_buffer *cmdbuf);

void panvk_per_arch(cs_pick_iter_sb)(struct panvk_cmd_buffer *cmdbuf,
                                     enum panvk_subqueue_id subqueue);

void panvk_per_arch(get_cs_deps)(struct panvk_cmd_buffer *cmdbuf,
                                 const VkDependencyInfo *in,
                                 struct panvk_cs_deps *out);

VkResult panvk_per_arch(cmd_prepare_exec_cmd_for_draws)(
   struct panvk_cmd_buffer *primary, struct panvk_cmd_buffer *secondary);

void panvk_per_arch(cmd_inherit_render_state)(
   struct panvk_cmd_buffer *cmdbuf, const VkCommandBufferBeginInfo *pBeginInfo);

static inline void
panvk_per_arch(calculate_task_axis_and_increment)(
   const struct panvk_shader *shader, struct panvk_physical_device *phys_dev,
   unsigned *task_axis, unsigned *task_increment)
{
   /* Pick the task_axis and task_increment to maximize thread
    * utilization. */
   unsigned threads_per_wg =
      shader->local_size.x * shader->local_size.y * shader->local_size.z;
   unsigned max_thread_cnt = panfrost_compute_max_thread_count(
      &phys_dev->kmod.props, shader->info.work_reg_count);
   unsigned threads_per_task = threads_per_wg;
   unsigned local_size[3] = {
      shader->local_size.x,
      shader->local_size.y,
      shader->local_size.z,
   };

   for (unsigned i = 0; i < 3; i++) {
      if (threads_per_task * local_size[i] >= max_thread_cnt) {
         /* We reached out thread limit, stop at the current axis and
          * calculate the increment so it doesn't exceed the per-core
          * thread capacity.
          */
         *task_increment = max_thread_cnt / threads_per_task;
         break;
      } else if (*task_axis == MALI_TASK_AXIS_Z) {
         /* We reached the Z axis, and there's still room to stuff more
          * threads. Pick the current axis grid size as our increment
          * as there's no point using something bigger.
          */
         *task_increment = local_size[i];
         break;
      }

      threads_per_task *= local_size[i];
      (*task_axis)++;
   }

   assert(*task_axis <= MALI_TASK_AXIS_Z);
   assert(*task_increment > 0);
}

#endif /* PANVK_CMD_BUFFER_H */
