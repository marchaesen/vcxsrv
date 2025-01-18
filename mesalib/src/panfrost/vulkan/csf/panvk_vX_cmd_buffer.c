/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_pool.h"
#include "panvk_cmd_push_constant.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_instance.h"
#include "panvk_physical_device.h"
#include "panvk_priv_bo.h"

#include "pan_desc.h"
#include "pan_encoder.h"
#include "pan_props.h"
#include "pan_samples.h"

#include "vk_descriptor_update_template.h"
#include "vk_format.h"
#include "vk_synchronization.h"

static void
emit_tls(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   unsigned core_id_range;
   panfrost_query_core_count(&phys_dev->kmod.props, &core_id_range);

   if (cmdbuf->state.tls.info.tls.size) {
      unsigned thread_tls_alloc =
         panfrost_query_thread_tls_alloc(&phys_dev->kmod.props);
      unsigned size = panfrost_get_total_stack_size(
         cmdbuf->state.tls.info.tls.size, thread_tls_alloc, core_id_range);

      cmdbuf->state.tls.info.tls.ptr =
         panvk_cmd_alloc_dev_mem(cmdbuf, tls, size, 4096).gpu;
   }

   assert(!cmdbuf->state.tls.info.wls.size);

   if (cmdbuf->state.tls.desc.cpu) {
      GENX(pan_emit_tls)(&cmdbuf->state.tls.info, cmdbuf->state.tls.desc.cpu);
   }
}

static void
finish_cs(struct panvk_cmd_buffer *cmdbuf, uint32_t subqueue)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_instance *instance =
      to_panvk_instance(dev->vk.physical->instance);
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, subqueue);

   cs_update_progress_seqno(b) {
      for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
         uint32_t rel_sync_point = cmdbuf->state.cs[i].relative_sync_point;

         if (!rel_sync_point)
            continue;

         cs_add64(b, cs_progress_seqno_reg(b, i), cs_progress_seqno_reg(b, i),
                  rel_sync_point);
      }
   }

   /* We need a clean because descriptor/CS memory can be returned to the
    * command pool where they get recycled. If we don't clean dirty cache lines,
    * those cache lines might get evicted asynchronously and their content
    * pushed back to main memory after the CPU has written new stuff there. */
   struct cs_index flush_id = cs_scratch_reg32(b, 0);

   cs_move32_to(b, flush_id, 0);
   cs_wait_slots(b, SB_ALL_MASK, false);
   cs_flush_caches(b, MALI_CS_FLUSH_MODE_CLEAN, MALI_CS_FLUSH_MODE_CLEAN,
                   false, flush_id, cs_defer(SB_IMM_MASK, SB_ID(IMM_FLUSH)));
   cs_wait_slot(b, SB_ID(IMM_FLUSH), false);

   /* If we're in sync/trace more, we signal the debug object. */
   if (instance->debug_flags & (PANVK_DEBUG_SYNC | PANVK_DEBUG_TRACE)) {
      struct cs_index debug_sync_addr = cs_scratch_reg64(b, 0);
      struct cs_index one = cs_scratch_reg32(b, 2);
      struct cs_index error = cs_scratch_reg32(b, 3);
      struct cs_index cmp_scratch = cs_scratch_reg32(b, 2);

      cs_move32_to(b, one, 1);
      cs_load64_to(b, debug_sync_addr, cs_subqueue_ctx_reg(b),
                   offsetof(struct panvk_cs_subqueue_context, debug_syncobjs));
      cs_wait_slot(b, SB_ID(LS), false);
      cs_add64(b, debug_sync_addr, debug_sync_addr,
               sizeof(struct panvk_cs_sync32) * subqueue);
      cs_load32_to(b, error, debug_sync_addr,
                   offsetof(struct panvk_cs_sync32, error));
      cs_wait_slots(b, SB_ALL_MASK, false);
      cs_sync32_add(b, true, MALI_CS_SYNC_SCOPE_SYSTEM, one, debug_sync_addr,
                    cs_now());

      cs_match(b, error, cmp_scratch) {
         cs_case(b, 0) {
            /* Do nothing. */
         }

         cs_default(b) {
            /* Overwrite the sync error with the first error we encountered. */
            cs_store32(b, error, debug_sync_addr,
                       offsetof(struct panvk_cs_sync32, error));
            cs_wait_slots(b, SB_ID(LS), false);
         }
      }
   }

   cs_finish(&cmdbuf->state.cs[subqueue].builder);
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(EndCommandBuffer)(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);

   emit_tls(cmdbuf);

   for (uint32_t i = 0; i < ARRAY_SIZE(cmdbuf->state.cs); i++) {
      struct cs_builder *b = &cmdbuf->state.cs[i].builder;

      if (!cs_is_valid(b)) {
         vk_command_buffer_set_error(&cmdbuf->vk,
                                     VK_ERROR_OUT_OF_DEVICE_MEMORY);
      } else {
         finish_cs(cmdbuf, i);
      }
   }

   cmdbuf->flush_id = panthor_kmod_get_flush_id(dev->kmod.dev);

   return vk_command_buffer_end(&cmdbuf->vk);
}

static bool
src_stages_need_draw_flush(VkPipelineStageFlags2 stages)
{
   static const VkPipelineStageFlags2 draw_flush_stage_mask =
      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
      VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT |
      VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT;

   return (stages & draw_flush_stage_mask) != 0;
}

static bool
stages_cover_subqueue(enum panvk_subqueue_id subqueue,
                      VkPipelineStageFlags2 stages)
{
   static const VkPipelineStageFlags2 queue_coverage[PANVK_SUBQUEUE_COUNT] = {
      [PANVK_SUBQUEUE_VERTEX_TILER] = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
                                      VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
                                      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
      [PANVK_SUBQUEUE_FRAGMENT] =
         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
         VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT |
         VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT,
      [PANVK_SUBQUEUE_COMPUTE] =
         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
   };

   return (stages & queue_coverage[subqueue]) != 0;
}

static uint32_t
src_stages_to_subqueue_sb_mask(enum panvk_subqueue_id subqueue,
                               VkPipelineStageFlags2 stages)
{
   if (!stages_cover_subqueue(subqueue, stages))
      return 0;

   /* Indirect draw buffers are read from the command stream, and load/store
    * operations are synchronized with the LS scoreboad immediately after the
    * read, so no need to wait in that case.
    */
   if (subqueue == PANVK_SUBQUEUE_VERTEX_TILER &&
       stages == VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT)
      return 0;

   /* We need to wait for all previously submitted jobs, and given the
    * iterator scoreboard is a moving target, we just wait for the
    * whole dynamic scoreboard range. */
   return BITFIELD_RANGE(PANVK_SB_ITER_START, PANVK_SB_ITER_COUNT);
}

static void
collect_cache_flush_info(enum panvk_subqueue_id subqueue,
                         struct panvk_cache_flush_info *cache_flush,
                         VkPipelineStageFlags2 src_stages,
                         VkPipelineStageFlags2 dst_stages,
                         VkAccessFlags2 src_access, VkAccessFlags2 dst_access)
{
   static const VkAccessFlags2 dev_writes[PANVK_SUBQUEUE_COUNT] = {
      [PANVK_SUBQUEUE_VERTEX_TILER] = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                                      VK_ACCESS_2_SHADER_WRITE_BIT |
                                      VK_ACCESS_2_TRANSFER_WRITE_BIT,
      [PANVK_SUBQUEUE_FRAGMENT] =
         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT |
         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
         VK_ACCESS_2_TRANSFER_WRITE_BIT,
      [PANVK_SUBQUEUE_COMPUTE] = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                                 VK_ACCESS_2_SHADER_WRITE_BIT |
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT,
   };
   static const VkAccessFlags2 dev_reads[PANVK_SUBQUEUE_COUNT] = {
      [PANVK_SUBQUEUE_VERTEX_TILER] =
         VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT |
         VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT |
         VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT |
         VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
         VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
      [PANVK_SUBQUEUE_FRAGMENT] =
         VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT |
         VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
         VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
         VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
      [PANVK_SUBQUEUE_COMPUTE] =
         VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT |
         VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
         VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
   };

   /* Note on the cache organization:
    * - L2 cache is unified, so all changes to this cache are automatically
    *   visible to all GPU sub-components (shader cores, tiler, ...). This
    *   means we only need to flush when the host (AKA CPU) is involved.
    * - LS caches (which are basically just read-write L1 caches) are coherent
    *   with each other and with the L2 cache, so again, we only need to flush
    *   when the host is involved.
    * - Other read-only L1 caches (like the ones in front of the texture unit)
    *   are not coherent with the LS or L2 caches, and thus need to be
    *   invalidated any time a write happens.
    */

#define ACCESS_HITS_RO_L1_CACHE                                                \
   (VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |                                      \
    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |                                    \
    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |                            \
    VK_ACCESS_2_TRANSFER_READ_BIT)

   if ((dev_writes[subqueue] & src_access) &&
       (dev_reads[subqueue] & ACCESS_HITS_RO_L1_CACHE & dst_access))
      cache_flush->others |= true;

   /* If the host wrote something, we need to clean/invalidate everything. */
   if ((src_stages & VK_PIPELINE_STAGE_2_HOST_BIT) &&
       (src_access & VK_ACCESS_2_HOST_WRITE_BIT) &&
       ((dev_reads[subqueue] | dev_writes[subqueue]) & dst_access)) {
      cache_flush->l2 |= MALI_CS_FLUSH_MODE_CLEAN_AND_INVALIDATE;
      cache_flush->lsc |= MALI_CS_FLUSH_MODE_CLEAN_AND_INVALIDATE;
      cache_flush->others |= true;
   }

   /* If the host needs to read something we wrote, we need to clean
    * everything. */
   if ((dst_stages & VK_PIPELINE_STAGE_2_HOST_BIT) &&
       (dst_access & VK_ACCESS_2_HOST_READ_BIT) &&
       (dev_writes[subqueue] & src_access)) {
      cache_flush->l2 |= MALI_CS_FLUSH_MODE_CLEAN;
      cache_flush->lsc |= MALI_CS_FLUSH_MODE_CLEAN;
   }
}

static void
collect_cs_deps(struct panvk_cmd_buffer *cmdbuf,
                VkPipelineStageFlags2 src_stages,
                VkPipelineStageFlags2 dst_stages, VkAccessFlags src_access,
                VkAccessFlags dst_access, struct panvk_cs_deps *deps)
{
   if (src_stages_need_draw_flush(src_stages) && cmdbuf->state.gfx.render.tiler)
      deps->needs_draw_flush = true;

   uint32_t wait_subqueue_mask = 0;
   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      uint32_t sb_mask = src_stages_to_subqueue_sb_mask(i, src_stages);
      assert((sb_mask != 0) == stages_cover_subqueue(i, src_stages));
      if (!sb_mask)
         continue;

      deps->src[i].wait_sb_mask |= sb_mask;
      collect_cache_flush_info(i, &deps->src[i].cache_flush, src_stages,
                               dst_stages, src_access, dst_access);
      wait_subqueue_mask |= BITFIELD_BIT(i);
   }

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      if (!stages_cover_subqueue(i, dst_stages))
         continue;

      deps->dst[i].wait_subqueue_mask |= wait_subqueue_mask & ~BITFIELD_BIT(i);
   }
}

void
panvk_per_arch(get_cs_deps)(struct panvk_cmd_buffer *cmdbuf,
                            const VkDependencyInfo *in,
                            struct panvk_cs_deps *out)
{
   memset(out, 0, sizeof(*out));

   for (uint32_t i = 0; i < in->memoryBarrierCount; i++) {
      const VkMemoryBarrier2 *barrier = &in->pMemoryBarriers[i];
      VkPipelineStageFlags2 src_stages =
         vk_expand_pipeline_stage_flags2(barrier->srcStageMask);
      VkPipelineStageFlags2 dst_stages =
         vk_expand_pipeline_stage_flags2(barrier->dstStageMask);
      VkAccessFlags2 src_access =
         vk_filter_src_access_flags2(src_stages, barrier->srcAccessMask);
      VkAccessFlags2 dst_access =
         vk_filter_dst_access_flags2(dst_stages, barrier->dstAccessMask);

      collect_cs_deps(cmdbuf, src_stages, dst_stages, src_access, dst_access,
                      out);
   }

   for (uint32_t i = 0; i < in->bufferMemoryBarrierCount; i++) {
      const VkBufferMemoryBarrier2 *barrier = &in->pBufferMemoryBarriers[i];
      VkPipelineStageFlags2 src_stages =
         vk_expand_pipeline_stage_flags2(barrier->srcStageMask);
      VkPipelineStageFlags2 dst_stages =
         vk_expand_pipeline_stage_flags2(barrier->dstStageMask);
      VkAccessFlags2 src_access =
         vk_filter_src_access_flags2(src_stages, barrier->srcAccessMask);
      VkAccessFlags2 dst_access =
         vk_filter_dst_access_flags2(dst_stages, barrier->dstAccessMask);

      collect_cs_deps(cmdbuf, src_stages, dst_stages, src_access, dst_access,
                      out);
   }

   for (uint32_t i = 0; i < in->imageMemoryBarrierCount; i++) {
      const VkImageMemoryBarrier2 *barrier = &in->pImageMemoryBarriers[i];
      VkPipelineStageFlags2 src_stages =
         vk_expand_pipeline_stage_flags2(barrier->srcStageMask);
      VkPipelineStageFlags2 dst_stages =
         vk_expand_pipeline_stage_flags2(barrier->dstStageMask);
      VkAccessFlags2 src_access =
         vk_filter_src_access_flags2(src_stages, barrier->srcAccessMask);
      VkAccessFlags2 dst_access =
         vk_filter_dst_access_flags2(dst_stages, barrier->dstAccessMask);

      collect_cs_deps(cmdbuf, src_stages, dst_stages, src_access, dst_access,
                      out);
   }

   /* The draw flush will add a vertex -> fragment dependency, so we can skip
    * the one described in the deps. */
   if (out->needs_draw_flush)
      out->dst[PANVK_SUBQUEUE_FRAGMENT].wait_subqueue_mask &=
         ~BITFIELD_BIT(PANVK_SUBQUEUE_VERTEX_TILER);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdPipelineBarrier2)(VkCommandBuffer commandBuffer,
                                    const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_cs_deps deps;

   panvk_per_arch(get_cs_deps)(cmdbuf, pDependencyInfo, &deps);

   if (deps.needs_draw_flush)
      panvk_per_arch(cmd_flush_draws)(cmdbuf);

   uint32_t wait_subqueue_mask = 0;
   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++)
      wait_subqueue_mask |= deps.dst[i].wait_subqueue_mask;

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      if (!deps.src[i].wait_sb_mask)
         continue;

      struct cs_builder *b = panvk_get_cs_builder(cmdbuf, i);
      struct panvk_cs_state *cs_state = &cmdbuf->state.cs[i];

      cs_wait_slots(b, deps.src[i].wait_sb_mask, false);

      struct panvk_cache_flush_info cache_flush = deps.src[i].cache_flush;
      if (cache_flush.l2 != MALI_CS_FLUSH_MODE_NONE ||
          cache_flush.lsc != MALI_CS_FLUSH_MODE_NONE || cache_flush.others) {
         struct cs_index flush_id = cs_scratch_reg32(b, 0);

         cs_move32_to(b, flush_id, 0);
         cs_flush_caches(b, cache_flush.l2, cache_flush.lsc, cache_flush.others,
                         flush_id, cs_defer(SB_IMM_MASK, SB_ID(IMM_FLUSH)));
         cs_wait_slot(b, SB_ID(IMM_FLUSH), false);
      }

      /* If no one waits on us, there's no point signaling the sync object. */
      if (wait_subqueue_mask & BITFIELD_BIT(i)) {
         struct cs_index sync_addr = cs_scratch_reg64(b, 0);
         struct cs_index add_val = cs_scratch_reg64(b, 2);

         cs_load64_to(b, sync_addr, cs_subqueue_ctx_reg(b),
                      offsetof(struct panvk_cs_subqueue_context, syncobjs));
         cs_wait_slot(b, SB_ID(LS), false);
         cs_add64(b, sync_addr, sync_addr, sizeof(struct panvk_cs_sync64) * i);
         cs_move64_to(b, add_val, 1);
         cs_sync64_add(b, false, MALI_CS_SYNC_SCOPE_CSG, add_val, sync_addr,
                       cs_now());
         ++cs_state->relative_sync_point;
      }
   }

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      if (!deps.dst[i].wait_subqueue_mask)
         continue;

      struct cs_builder *b = panvk_get_cs_builder(cmdbuf, i);
      for (uint32_t j = 0; j < PANVK_SUBQUEUE_COUNT; j++) {
         if (!(deps.dst[i].wait_subqueue_mask & BITFIELD_BIT(j)))
            continue;

         struct panvk_cs_state *cs_state = &cmdbuf->state.cs[j];
         struct cs_index sync_addr = cs_scratch_reg64(b, 0);
         struct cs_index wait_val = cs_scratch_reg64(b, 2);

         cs_load64_to(b, sync_addr, cs_subqueue_ctx_reg(b),
                      offsetof(struct panvk_cs_subqueue_context, syncobjs));
         cs_wait_slot(b, SB_ID(LS), false);
         cs_add64(b, sync_addr, sync_addr, sizeof(struct panvk_cs_sync64) * j);

         cs_add64(b, wait_val, cs_progress_seqno_reg(b, j),
                  cs_state->relative_sync_point);
         cs_sync64_wait(b, false, MALI_CS_CONDITION_GREATER, wait_val,
                        sync_addr);
      }
   }
}

void
panvk_per_arch(cs_pick_iter_sb)(struct panvk_cmd_buffer *cmdbuf,
                                enum panvk_subqueue_id subqueue)
{
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, subqueue);
   struct cs_index iter_sb = cs_scratch_reg32(b, 0);
   struct cs_index cmp_scratch = cs_scratch_reg32(b, 1);

   cs_load32_to(b, iter_sb, cs_subqueue_ctx_reg(b),
                offsetof(struct panvk_cs_subqueue_context, iter_sb));
   cs_wait_slot(b, SB_ID(LS), false);

   cs_match(b, iter_sb, cmp_scratch) {
#define CASE(x)                                                                \
      cs_case(b, x) {                                                          \
         cs_wait_slot(b, SB_ITER(x), false);                                   \
         cs_set_scoreboard_entry(b, SB_ITER(x), SB_ID(LS));                    \
      }

      CASE(0)
      CASE(1)
      CASE(2)
      CASE(3)
      CASE(4)
#undef CASE
   }
}

static struct cs_buffer
alloc_cs_buffer(void *cookie)
{
   struct panvk_cmd_buffer *cmdbuf = cookie;
   const unsigned capacity = 64 * 1024 / sizeof(uint64_t);

   struct panfrost_ptr ptr =
      panvk_cmd_alloc_dev_mem(cmdbuf, cs, capacity * 8, 64);

   return (struct cs_buffer){
      .cpu = ptr.cpu,
      .gpu = ptr.gpu,
      .capacity = capacity,
   };
}

static enum cs_reg_perm
cs_reg_perm(struct cs_builder *b, unsigned reg)
{
   struct panvk_cs_state *cs_state =
      container_of(b, struct panvk_cs_state, builder);
   struct panvk_cs_reg_upd_context *upd_ctx;

   for (upd_ctx = cs_state->reg_access.upd_ctx_stack; upd_ctx;
        upd_ctx = upd_ctx->next) {
      if (upd_ctx->reg_perm(b, reg) == CS_REG_RW)
         return CS_REG_RW;
   }

   return cs_state->reg_access.base_perm(b, reg);
}

static void
init_cs_builders(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_instance *instance =
      to_panvk_instance(dev->vk.physical->instance);
   const reg_perm_cb_t base_reg_perms[PANVK_SUBQUEUE_COUNT] = {
      [PANVK_SUBQUEUE_VERTEX_TILER] = panvk_cs_vt_reg_perm,
      [PANVK_SUBQUEUE_FRAGMENT] = panvk_cs_frag_reg_perm,
      [PANVK_SUBQUEUE_COMPUTE] = panvk_cs_compute_reg_perm,
   };

   for (uint32_t i = 0; i < ARRAY_SIZE(cmdbuf->state.cs); i++) {
      /* Lazy allocation of the root CS. */
      struct cs_buffer root_cs = {0};

      struct cs_builder_conf conf = {
         .nr_registers = 96,
         .nr_kernel_registers = 4,
         .alloc_buffer = alloc_cs_buffer,
         .cookie = cmdbuf,
      };

      if (instance->debug_flags & PANVK_DEBUG_CS) {
         cmdbuf->state.cs[i].ls_tracker = (struct cs_load_store_tracker){
            .sb_slot = SB_ID(LS),
         };

         conf.ls_tracker = &cmdbuf->state.cs[i].ls_tracker;

         cmdbuf->state.cs[i].reg_access.upd_ctx_stack = NULL;
         cmdbuf->state.cs[i].reg_access.base_perm = base_reg_perms[i];
         conf.reg_perm = cs_reg_perm;
      }

      cs_builder_init(&cmdbuf->state.cs[i].builder, &conf, root_cs);
   }
}

static void
panvk_reset_cmdbuf(struct vk_command_buffer *vk_cmdbuf,
                   VkCommandBufferResetFlags flags)
{
   struct panvk_cmd_buffer *cmdbuf =
      container_of(vk_cmdbuf, struct panvk_cmd_buffer, vk);
   struct panvk_cmd_pool *pool =
      container_of(vk_cmdbuf->pool, struct panvk_cmd_pool, vk);

   vk_command_buffer_reset(&cmdbuf->vk);

   panvk_pool_reset(&cmdbuf->cs_pool);
   panvk_pool_reset(&cmdbuf->desc_pool);
   panvk_pool_reset(&cmdbuf->tls_pool);
   list_splicetail(&cmdbuf->push_sets, &pool->push_sets);
   list_inithead(&cmdbuf->push_sets);

   memset(&cmdbuf->state, 0, sizeof(cmdbuf->state));
   init_cs_builders(cmdbuf);
}

static void
panvk_destroy_cmdbuf(struct vk_command_buffer *vk_cmdbuf)
{
   struct panvk_cmd_buffer *cmdbuf =
      container_of(vk_cmdbuf, struct panvk_cmd_buffer, vk);
   struct panvk_cmd_pool *pool =
      container_of(vk_cmdbuf->pool, struct panvk_cmd_pool, vk);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);

   panvk_pool_cleanup(&cmdbuf->cs_pool);
   panvk_pool_cleanup(&cmdbuf->desc_pool);
   panvk_pool_cleanup(&cmdbuf->tls_pool);
   list_splicetail(&cmdbuf->push_sets, &pool->push_sets);
   vk_command_buffer_finish(&cmdbuf->vk);
   vk_free(&dev->vk.alloc, cmdbuf);
}

static VkResult
panvk_create_cmdbuf(struct vk_command_pool *vk_pool, VkCommandBufferLevel level,
                    struct vk_command_buffer **cmdbuf_out)
{
   struct panvk_device *device =
      container_of(vk_pool->base.device, struct panvk_device, vk);
   struct panvk_cmd_pool *pool =
      container_of(vk_pool, struct panvk_cmd_pool, vk);
   struct panvk_cmd_buffer *cmdbuf;

   cmdbuf = vk_zalloc(&device->vk.alloc, sizeof(*cmdbuf), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmdbuf)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_command_buffer_init(
      &pool->vk, &cmdbuf->vk, &panvk_per_arch(cmd_buffer_ops), level);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, cmdbuf);
      return result;
   }

   list_inithead(&cmdbuf->push_sets);
   cmdbuf->vk.dynamic_graphics_state.vi = &cmdbuf->state.gfx.dynamic.vi;
   cmdbuf->vk.dynamic_graphics_state.ms.sample_locations =
      &cmdbuf->state.gfx.dynamic.sl;

   struct panvk_pool_properties cs_pool_props = {
      .create_flags = 0,
      .slab_size = 64 * 1024,
      .label = "Command buffer CS pool",
      .prealloc = false,
      .owns_bos = true,
      .needs_locking = false,
   };
   panvk_pool_init(&cmdbuf->cs_pool, device, &pool->cs_bo_pool, &cs_pool_props);

   struct panvk_pool_properties desc_pool_props = {
      .create_flags = 0,
      .slab_size = 64 * 1024,
      .label = "Command buffer descriptor pool",
      .prealloc = false,
      .owns_bos = true,
      .needs_locking = false,
   };
   panvk_pool_init(&cmdbuf->desc_pool, device, &pool->desc_bo_pool,
                   &desc_pool_props);

   struct panvk_pool_properties tls_pool_props = {
      .create_flags =
         panvk_device_adjust_bo_flags(device, PAN_KMOD_BO_FLAG_NO_MMAP),
      .slab_size = 64 * 1024,
      .label = "TLS pool",
      .prealloc = false,
      .owns_bos = true,
      .needs_locking = false,
   };
   panvk_pool_init(&cmdbuf->tls_pool, device, &pool->tls_bo_pool,
                   &tls_pool_props);

   init_cs_builders(cmdbuf);
   *cmdbuf_out = &cmdbuf->vk;
   return VK_SUCCESS;
}

const struct vk_command_buffer_ops panvk_per_arch(cmd_buffer_ops) = {
   .create = panvk_create_cmdbuf,
   .reset = panvk_reset_cmdbuf,
   .destroy = panvk_destroy_cmdbuf,
};

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(BeginCommandBuffer)(VkCommandBuffer commandBuffer,
                                   const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_instance *instance =
      to_panvk_instance(dev->vk.physical->instance);

   vk_command_buffer_begin(&cmdbuf->vk, pBeginInfo);
   cmdbuf->flags = pBeginInfo->flags;

   /* The descriptor ringbuf trips out pandecode because we always point to the
    * next tiler/framebuffer descriptor after CS execution, which means we're
    * decoding an uninitialized or stale descriptor.
    * FIXME: find a way to trace the simultaneous path that doesn't crash. One
    * option would be to disable CS intepretation and dump the RUN_xxx context
    * on the side at execution time.
    */
   if (instance->debug_flags & PANVK_DEBUG_TRACE)
      cmdbuf->flags &= ~VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

   return VK_SUCCESS;
}
