/*
 * Copyright (C) 2023 Collabora Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "decode.h"

#include "drm-uapi/panthor_drm.h"

#include "genxml/cs_builder.h"
#include "panfrost/lib/genxml/cs_builder.h"

#include "gen_macros.h"
#include "pan_cmdstream.h"
#include "pan_context.h"
#include "pan_csf.h"
#include "pan_fb_preload.h"
#include "pan_job.h"

#if PAN_ARCH < 10
#error "CSF helpers are only used for gen >= 10"
#endif

static struct cs_buffer
csf_alloc_cs_buffer(void *cookie)
{
   assert(cookie && "Self-contained queues can't be extended.");

   struct panfrost_batch *batch = cookie;
   unsigned capacity = 4096;

   struct panfrost_ptr ptr =
      pan_pool_alloc_aligned(&batch->csf.cs_chunk_pool.base, capacity * 8, 64);

   return (struct cs_buffer){
      .cpu = ptr.cpu,
      .gpu = ptr.gpu,
      .capacity = capacity,
   };
}

/*
 * Register is reserved to pass the batch tiler OOM context
 */
#define TILER_OOM_CTX_REG 76

static enum cs_reg_perm
csf_reg_perm_cb(struct cs_builder *b, unsigned reg)
{
   if (reg == TILER_OOM_CTX_REG)
      return CS_REG_RD;
   return CS_REG_RW;
}

static void
csf_update_tiler_oom_ctx(struct cs_builder *b, uint64_t addr)
{
   reg_perm_cb_t orig_cb;

   if (likely(!b->conf.reg_perm)) {
      cs_move64_to(b, cs_reg64(b, TILER_OOM_CTX_REG), addr);
      return;
   }

   orig_cb = b->conf.reg_perm;
   b->conf.reg_perm = NULL;
   cs_move64_to(b, cs_reg64(b, TILER_OOM_CTX_REG), addr);
   b->conf.reg_perm = orig_cb;
}

#define FIELD_OFFSET(_name) offsetof(struct pan_csf_tiler_oom_ctx, _name)

#define FBD_OFFSET(_pass)                                                      \
   (FIELD_OFFSET(fbds) +                                                       \
    (PAN_INCREMENTAL_RENDERING_##_pass##_PASS * sizeof(struct panfrost_ptr)) + \
    offsetof(struct panfrost_ptr, gpu))

static int
csf_oom_handler_init(struct panfrost_context *ctx)
{
   struct panfrost_bo *cs_bo = NULL, *reg_save_bo = NULL;
   struct panfrost_device *dev = pan_device(ctx->base.screen);

   cs_bo =
      panfrost_bo_create(dev, 4096, 0, "Temporary CS buffer");
   reg_save_bo =
      panfrost_bo_create(dev, 4096, 0, "reg save bo");

   if (!cs_bo || !reg_save_bo)
      goto fail;

   struct cs_buffer queue = {
      .cpu = cs_bo->ptr.cpu,
      .gpu = cs_bo->ptr.gpu,
      .capacity = panfrost_bo_size(cs_bo) / sizeof(uint64_t),
   };
   struct cs_builder b;
   const struct cs_builder_conf conf = {
      .nr_registers = 96,
      .nr_kernel_registers = 4,
      .reg_perm = (dev->debug & PAN_DBG_CS) ? csf_reg_perm_cb : NULL,
   };
   cs_builder_init(&b, &conf, queue);

   struct cs_exception_handler_ctx handler_ctx = {
      .ctx_reg = cs_reg64(&b, TILER_OOM_CTX_REG),
      .dump_addr_offset = offsetof(struct pan_csf_tiler_oom_ctx, dump_addr),
      .ls_sb_slot = 0,
   };
   struct cs_exception_handler handler;

   cs_exception_handler_def(&b, &handler, handler_ctx) {
      struct cs_index tiler_oom_ctx = cs_reg64(&b, TILER_OOM_CTX_REG);
      struct cs_index counter = cs_reg32(&b, 47);
      struct cs_index zero = cs_reg64(&b, 48);
      struct cs_index flush_id = cs_reg32(&b, 48);
      struct cs_index tiler_ctx = cs_reg64(&b, 50);
      struct cs_index completed_top = cs_reg64(&b, 52);
      struct cs_index completed_bottom = cs_reg64(&b, 54);
      struct cs_index completed_chunks = cs_reg_tuple(&b, 52, 4);

      /* Use different framebuffer descriptor depending on whether incremental
       * rendering has already been triggered */
      cs_load32_to(&b, counter, tiler_oom_ctx, FIELD_OFFSET(counter));
      cs_wait_slot(&b, 0, false);
      cs_if(&b, MALI_CS_CONDITION_GREATER, counter) {
         cs_load64_to(&b, cs_reg64(&b, 40), tiler_oom_ctx, FBD_OFFSET(MIDDLE));
      }
      cs_else(&b) {
         cs_load64_to(&b, cs_reg64(&b, 40), tiler_oom_ctx, FBD_OFFSET(FIRST));
      }

      cs_load32_to(&b, cs_reg32(&b, 42), tiler_oom_ctx, FIELD_OFFSET(bbox_min));
      cs_load32_to(&b, cs_reg32(&b, 43), tiler_oom_ctx, FIELD_OFFSET(bbox_max));
      cs_move64_to(&b, cs_reg64(&b, 44), 0);
      cs_move32_to(&b, cs_reg32(&b, 46), 0);
      cs_wait_slot(&b, 0, false);

      /* Run the fragment job and wait */
      cs_set_scoreboard_entry(&b, 3, 0);
      cs_run_fragment(&b, false, MALI_TILE_RENDER_ORDER_Z_ORDER, false);
      cs_wait_slot(&b, 3, false);

      /* Increment counter */
      cs_add32(&b, counter, counter, 1);
      cs_store32(&b, counter, tiler_oom_ctx, FIELD_OFFSET(counter));

      /* Load completed chunks */
      cs_load64_to(&b, tiler_ctx, tiler_oom_ctx, FIELD_OFFSET(tiler_desc));
      cs_wait_slot(&b, 0, false);
      cs_load_to(&b, completed_chunks, tiler_ctx, BITFIELD_MASK(4), 10 * 4);
      cs_wait_slot(&b, 0, false);

      cs_finish_fragment(&b, false, completed_top, completed_bottom, cs_now());

      /* Zero out polygon list, completed_top and completed_bottom */
      cs_move64_to(&b, zero, 0);
      cs_store64(&b, zero, tiler_ctx, 0);
      cs_store64(&b, zero, tiler_ctx, 10 * 4);
      cs_store64(&b, zero, tiler_ctx, 12 * 4);

      /* We need to flush the texture caches so future preloads see the new
       * content. */
      cs_flush_caches(&b, MALI_CS_FLUSH_MODE_NONE, MALI_CS_FLUSH_MODE_NONE,
                      true, flush_id, cs_defer(0, 0));

      cs_wait_slot(&b, 0, false);

      cs_set_scoreboard_entry(&b, 2, 0);
   }

   assert(cs_is_valid(&b));
   cs_finish(&b);
   ctx->csf.tiler_oom_handler.cs_bo = cs_bo;
   ctx->csf.tiler_oom_handler.length = handler.length * sizeof(uint64_t);
   ctx->csf.tiler_oom_handler.save_bo = reg_save_bo;

   return 0;

fail:
   if (cs_bo)
      panfrost_bo_unreference(cs_bo);

   if (reg_save_bo)
      panfrost_bo_unreference(reg_save_bo);

   return -1;
}

#undef FBD_OFFSET
#undef FIELD_OFFSET

void
GENX(csf_cleanup_batch)(struct panfrost_batch *batch)
{
   free(batch->csf.cs.builder);
   free(batch->csf.cs.ls_tracker);

   panfrost_pool_cleanup(&batch->csf.cs_chunk_pool);
}

static inline struct panfrost_ptr
alloc_fbd(struct panfrost_batch *batch)
{
   return pan_pool_alloc_desc_aggregate(
      &batch->pool.base, PAN_DESC(FRAMEBUFFER), PAN_DESC(ZS_CRC_EXTENSION),
      PAN_DESC_ARRAY(MAX2(batch->key.nr_cbufs, 1), RENDER_TARGET));
}

int
GENX(csf_init_batch)(struct panfrost_batch *batch)
{
   struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

   /* Initialize the CS chunk pool. */
   if (panfrost_pool_init(&batch->csf.cs_chunk_pool, NULL, dev, 0, 32768,
                          "CS chunk pool", false, true))
      return -1;

   if (dev->debug & PAN_DBG_CS) {
      /* Load/store tracker if extra checks are enabled. */
      batch->csf.cs.ls_tracker =
         calloc(1, sizeof(struct cs_load_store_tracker));
      batch->csf.cs.ls_tracker->sb_slot = 0;
   }

   /* Allocate and bind the command queue */
   struct cs_buffer queue = csf_alloc_cs_buffer(batch);
   if (!queue.gpu)
      return -1;

   const struct cs_builder_conf conf = {
      .nr_registers = 96,
      .nr_kernel_registers = 4,
      .alloc_buffer = csf_alloc_cs_buffer,
      .cookie = batch,
      .ls_tracker = batch->csf.cs.ls_tracker,
      .reg_perm = (dev->debug & PAN_DBG_CS) ? csf_reg_perm_cb : NULL,
   };

   /* Setup the queue builder */
   batch->csf.cs.builder = malloc(sizeof(struct cs_builder));
   cs_builder_init(batch->csf.cs.builder, &conf, queue);
   cs_req_res(batch->csf.cs.builder,
              CS_COMPUTE_RES | CS_TILER_RES | CS_IDVS_RES | CS_FRAG_RES);

   /* Set up entries */
   struct cs_builder *b = batch->csf.cs.builder;
   cs_set_scoreboard_entry(b, 2, 0);

   batch->framebuffer = alloc_fbd(batch);
   if (!batch->framebuffer.gpu)
      return -1;

   batch->tls = pan_pool_alloc_desc(&batch->pool.base, LOCAL_STORAGE);
   if (!batch->tls.cpu)
      return -1;

   return 0;
}

static void
csf_prepare_qsubmit(struct panfrost_context *ctx,
                    struct drm_panthor_queue_submit *submit, uint8_t queue,
                    uint64_t cs_start, uint32_t cs_size,
                    struct drm_panthor_sync_op *syncs, uint32_t sync_count)
{
   struct panfrost_device *dev = pan_device(ctx->base.screen);

   *submit = (struct drm_panthor_queue_submit){
      .queue_index = queue,
      .stream_addr = cs_start,
      .stream_size = cs_size,
      .latest_flush = panthor_kmod_get_flush_id(dev->kmod.dev),
      .syncs = DRM_PANTHOR_OBJ_ARRAY(sync_count, syncs),
   };
}

static void
csf_prepare_gsubmit(struct panfrost_context *ctx,
                    struct drm_panthor_group_submit *gsubmit,
                    struct drm_panthor_queue_submit *qsubmits,
                    uint32_t qsubmit_count)
{
   *gsubmit = (struct drm_panthor_group_submit){
      .group_handle = ctx->csf.group_handle,
      .queue_submits = DRM_PANTHOR_OBJ_ARRAY(qsubmit_count, qsubmits),
   };
}

static int
csf_submit_gsubmit(struct panfrost_context *ctx,
                   struct drm_panthor_group_submit *gsubmit)
{
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   int ret = 0;

   if (!ctx->is_noop) {
      ret = drmIoctl(panfrost_device_fd(dev), DRM_IOCTL_PANTHOR_GROUP_SUBMIT,
                     gsubmit);
   }

   if (ret)
      return errno;

   return 0;
}

static int
csf_emit_batch_end(struct panfrost_batch *batch)
{
   struct panfrost_device *dev = pan_device(batch->ctx->base.screen);
   struct cs_builder *b = batch->csf.cs.builder;

   /* Barrier to let everything finish */
   cs_wait_slots(b, BITFIELD_MASK(8), false);

   if (dev->debug & PAN_DBG_SYNC) {
      /* Get the CS state */
      batch->csf.cs.state = pan_pool_alloc_aligned(&batch->pool.base, 8, 8);
      if (!batch->csf.cs.state.cpu)
         return -1;

      memset(batch->csf.cs.state.cpu, ~0, 8);
      cs_move64_to(b, cs_reg64(b, 90), batch->csf.cs.state.gpu);
      cs_store_state(b, cs_reg64(b, 90), 0, MALI_CS_STATE_ERROR_STATUS,
                     cs_now());
   }

   /* Flush caches now that we're done (synchronous) */
   struct cs_index flush_id = cs_reg32(b, 74);
   cs_move32_to(b, flush_id, 0);
   cs_flush_caches(b, MALI_CS_FLUSH_MODE_CLEAN, MALI_CS_FLUSH_MODE_CLEAN, true,
                   flush_id, cs_defer(0, 0));
   cs_wait_slot(b, 0, false);

   /* Finish the command stream */
   if (!cs_is_valid(batch->csf.cs.builder))
      return -1;

   cs_finish(batch->csf.cs.builder);
   return 0;
}

static int
csf_submit_collect_wait_ops(struct panfrost_batch *batch,
                            struct util_dynarray *syncops,
                            uint32_t vm_sync_handle)
{
   struct panfrost_context *ctx = batch->ctx;
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   uint64_t vm_sync_wait_point = 0, bo_sync_point;
   uint32_t bo_sync_handle;
   int ret;

   /* We don't wait on BOs attached to the various batch pools, because those
    * are private to the batch, and are guaranteed to be idle at allocation
    * time. We need to iterate over other BOs accessed by the batch though,
    * to add the corresponding wait operations.
    */
   util_dynarray_foreach(&batch->bos, pan_bo_access, ptr) {
      unsigned i = ptr - util_dynarray_element(&batch->bos, pan_bo_access, 0);
      pan_bo_access flags = *ptr;

      if (!flags)
         continue;

      /* Update the BO access flags so that panfrost_bo_wait() knows
       * about all pending accesses.
       * We only keep the READ/WRITE info since this is all the BO
       * wait logic cares about.
       * We also preserve existing flags as this batch might not
       * be the first one to access the BO.
       */
      struct panfrost_bo *bo = pan_lookup_bo(dev, i);

      ret = panthor_kmod_bo_get_sync_point(bo->kmod_bo, &bo_sync_handle,
                                           &bo_sync_point,
                                           !(flags & PAN_BO_ACCESS_WRITE));
      if (ret)
         return ret;

      if (bo_sync_handle == vm_sync_handle) {
         vm_sync_wait_point = MAX2(vm_sync_wait_point, bo_sync_point);
         continue;
      }

      assert(bo_sync_point == 0 || !bo->kmod_bo->exclusive_vm);

      struct drm_panthor_sync_op waitop = {
         .flags =
            DRM_PANTHOR_SYNC_OP_WAIT |
            (bo_sync_point ? DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ
                           : DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ),
         .handle = bo_sync_handle,
         .timeline_value = bo_sync_point,
      };

      util_dynarray_append(syncops, struct drm_panthor_sync_op, waitop);
   }

   if (vm_sync_wait_point > 0) {
      struct drm_panthor_sync_op waitop = {
         .flags = DRM_PANTHOR_SYNC_OP_WAIT |
                  DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ,
         .handle = vm_sync_handle,
         .timeline_value = vm_sync_wait_point,
      };

      util_dynarray_append(syncops, struct drm_panthor_sync_op, waitop);
   }

   if (ctx->in_sync_fd >= 0) {
      ret = drmSyncobjImportSyncFile(panfrost_device_fd(dev), ctx->in_sync_obj,
                                     ctx->in_sync_fd);
      if (ret)
         return ret;

      struct drm_panthor_sync_op waitop = {
         .flags =
            DRM_PANTHOR_SYNC_OP_WAIT | DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ,
         .handle = ctx->in_sync_obj,
      };

      util_dynarray_append(syncops, struct drm_panthor_sync_op, waitop);

      close(ctx->in_sync_fd);
      ctx->in_sync_fd = -1;
   }

   return 0;
}

static int
csf_attach_sync_points(struct panfrost_batch *batch, uint32_t vm_sync_handle,
                       uint64_t vm_sync_signal_point)
{
   struct panfrost_context *ctx = batch->ctx;
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   int ret;

   /* There should be no invisble allocation on CSF. */
   assert(batch->invisible_pool.bos.size == 0);

   /* Attach sync points to batch-private BOs first. We assume BOs can
    * be written by the GPU to keep things simple.
    */
   util_dynarray_foreach(&batch->pool.bos, struct panfrost_bo *, bo) {
      (*bo)->gpu_access |= PAN_BO_ACCESS_RW;
      ret = panthor_kmod_bo_attach_sync_point((*bo)->kmod_bo, vm_sync_handle,
                                              vm_sync_signal_point, true);
      if (ret)
         return ret;
   }

   util_dynarray_foreach(&batch->csf.cs_chunk_pool.bos, struct panfrost_bo *,
                         bo) {
      (*bo)->gpu_access |= PAN_BO_ACCESS_RW;
      ret = panthor_kmod_bo_attach_sync_point((*bo)->kmod_bo, vm_sync_handle,
                                              vm_sync_signal_point, true);
      if (ret)
         return ret;
   }

   /* Attach the VM sync point to all resources accessed by the batch. */
   util_dynarray_foreach(&batch->bos, pan_bo_access, ptr) {
      unsigned i = ptr - util_dynarray_element(&batch->bos, pan_bo_access, 0);
      pan_bo_access flags = *ptr;

      if (!flags)
         continue;

      struct panfrost_bo *bo = pan_lookup_bo(dev, i);

      bo->gpu_access |= flags & (PAN_BO_ACCESS_RW);
      ret = panthor_kmod_bo_attach_sync_point(bo->kmod_bo, vm_sync_handle,
                                              vm_sync_signal_point,
                                              flags & PAN_BO_ACCESS_WRITE);
      if (ret)
         return ret;
   }

   /* And finally transfer the VM sync point to the context syncobj. */
   return drmSyncobjTransfer(panfrost_device_fd(dev), ctx->syncobj, 0,
                             vm_sync_handle, vm_sync_signal_point, 0);
}

static void
csf_check_ctx_state_and_reinit(struct panfrost_context *ctx)
{
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   struct drm_panthor_group_get_state state = {
      .group_handle = ctx->csf.group_handle,
   };
   int ret;

   ret = drmIoctl(panfrost_device_fd(dev), DRM_IOCTL_PANTHOR_GROUP_GET_STATE,
                  &state);
   if (ret) {
      mesa_loge("DRM_IOCTL_PANTHOR_GROUP_GET_STATE failed (err=%d)", errno);
      return;
   }

   /* Context is still usable. This was a transient error. */
   if (state.state == 0)
      return;

   /* If the VM is unusable, we can't do much, as this is shared between all
    * contexts, and restoring the VM state is non-trivial.
    */
   if (pan_kmod_vm_query_state(dev->kmod.vm) != PAN_KMOD_VM_USABLE) {
      mesa_loge("VM became unusable, we can't reset the context");
      assert(!"VM became unusable, we can't reset the context");
   }

   panfrost_context_reinit(ctx);
}

static void
csf_submit_wait_and_dump(struct panfrost_batch *batch,
                         const struct drm_panthor_group_submit *gsubmit,
                         uint32_t vm_sync_handle, uint64_t vm_sync_signal_point)
{
   struct panfrost_context *ctx = batch->ctx;
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   bool wait = (dev->debug & (PAN_DBG_TRACE | PAN_DBG_SYNC)) && !ctx->is_noop;
   bool dump = (dev->debug & PAN_DBG_TRACE);
   bool crash = false;

   if (!wait && !dump)
      return;

   /* Wait so we can get errors reported back */
   if (wait) {
      int ret =
         drmSyncobjTimelineWait(panfrost_device_fd(dev), &vm_sync_handle,
                                &vm_sync_signal_point, 1, INT64_MAX, 0, NULL);
      assert(ret >= 0);

      struct pan_csf_tiler_oom_ctx *tiler_oom_ctx =
         batch->csf.tiler_oom_ctx.cpu;
      if (tiler_oom_ctx != NULL && tiler_oom_ctx->counter > 0) {
         perf_debug(ctx, "Incremental rendering was triggered %i time(s)",
                    tiler_oom_ctx->counter);
      }
   }

   /* Jobs won't be complete if blackhole rendering, that's ok */
   if (!ctx->is_noop && (dev->debug & PAN_DBG_SYNC) &&
       *((uint64_t *)batch->csf.cs.state.cpu) != 0) {
      crash = true;
      dump = true;
   }

   if (dump) {
      const struct drm_panthor_queue_submit *qsubmits =
         (void *)(uintptr_t)gsubmit->queue_submits.array;

      for (unsigned i = 0; i < gsubmit->queue_submits.count; i++) {
         uint32_t regs[256] = {0};
         pandecode_interpret_cs(dev->decode_ctx, qsubmits[i].stream_addr,
                                qsubmits[i].stream_size,
                                panfrost_device_gpu_id(dev), regs);
      }

      if (dev->debug & PAN_DBG_DUMP)
         pandecode_dump_mappings(dev->decode_ctx);
   }

   if (crash) {
      mesa_loge("Incomplete job or timeout\n");
      abort();
   }
}

int
GENX(csf_submit_batch)(struct panfrost_batch *batch)
{
   int ret;

   /* Close the batch before submitting. */
   ret = csf_emit_batch_end(batch);
   if (ret)
      return ret;

   uint64_t cs_start = cs_root_chunk_gpu_addr(batch->csf.cs.builder);
   uint32_t cs_size = cs_root_chunk_size(batch->csf.cs.builder);
   struct panfrost_context *ctx = batch->ctx;
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   uint32_t vm_sync_handle = panthor_kmod_vm_sync_handle(dev->kmod.vm);
   struct util_dynarray syncops;

   util_dynarray_init(&syncops, NULL);

   ret = csf_submit_collect_wait_ops(batch, &syncops, vm_sync_handle);
   if (ret)
      goto out_free_syncops;

   uint64_t vm_sync_cur_point = panthor_kmod_vm_sync_lock(dev->kmod.vm);
   uint64_t vm_sync_signal_point = vm_sync_cur_point + 1;

   struct drm_panthor_sync_op signalop = {
      .flags = DRM_PANTHOR_SYNC_OP_SIGNAL |
               DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ,
      .handle = vm_sync_handle,
      .timeline_value = vm_sync_signal_point,
   };

   util_dynarray_append(&syncops, struct drm_panthor_sync_op, signalop);

   struct drm_panthor_queue_submit qsubmit;
   struct drm_panthor_group_submit gsubmit;

   csf_prepare_qsubmit(
      ctx, &qsubmit, 0, cs_start, cs_size, util_dynarray_begin(&syncops),
      util_dynarray_num_elements(&syncops, struct drm_panthor_sync_op));
   csf_prepare_gsubmit(ctx, &gsubmit, &qsubmit, 1);
   ret = csf_submit_gsubmit(ctx, &gsubmit);
   panthor_kmod_vm_sync_unlock(dev->kmod.vm,
                               ret ? vm_sync_cur_point : vm_sync_signal_point);

   if (!ret) {
      csf_submit_wait_and_dump(batch, &gsubmit, vm_sync_handle,
                               vm_sync_signal_point);
      ret = csf_attach_sync_points(batch, vm_sync_handle, vm_sync_signal_point);
   } else {
      csf_check_ctx_state_and_reinit(batch->ctx);
   }

out_free_syncops:
   util_dynarray_fini(&syncops);
   return ret;
}

static uint64_t
csf_get_tiler_desc(struct panfrost_batch *batch)
{
   if (batch->tiler_ctx.valhall.desc)
      return batch->tiler_ctx.valhall.desc;

   struct panfrost_ptr t =
      pan_pool_alloc_desc(&batch->pool.base, TILER_CONTEXT);

   batch->csf.pending_tiler_desc = t.cpu;
   batch->tiler_ctx.valhall.desc = t.gpu;
   return batch->tiler_ctx.valhall.desc;
}

static void
csf_emit_tiler_desc(struct panfrost_batch *batch, const struct pan_fb_info *fb)
{
   struct panfrost_context *ctx = batch->ctx;
   struct panfrost_device *dev = pan_device(ctx->base.screen);

   if (!batch->csf.pending_tiler_desc)
      return;

   pan_pack(batch->csf.pending_tiler_desc, TILER_CONTEXT, tiler) {
      tiler.hierarchy_mask =
         pan_select_tiler_hierarchy_mask(batch->key.width,
                                         batch->key.height,
                                         dev->tiler_features.max_levels);

      /* For effective tile size larger than 16x16, disable first level */
      if (fb->tile_size > 16 * 16)
         tiler.hierarchy_mask &= ~1;

      tiler.fb_width = batch->key.width;
      tiler.fb_height = batch->key.height;
      tiler.heap = batch->ctx->csf.heap.desc_bo->ptr.gpu;
      tiler.sample_pattern =
         pan_sample_pattern(util_framebuffer_get_num_samples(&batch->key));
      tiler.first_provoking_vertex =
         batch->first_provoking_vertex == U_TRISTATE_YES;
      tiler.geometry_buffer = ctx->csf.tmp_geom_bo->ptr.gpu;
      tiler.geometry_buffer_size = ctx->csf.tmp_geom_bo->kmod_bo->size;
   }

   batch->csf.pending_tiler_desc = NULL;
}

void
GENX(csf_prepare_tiler)(struct panfrost_batch *batch, struct pan_fb_info *fb)
{
   csf_emit_tiler_desc(batch, fb);
}

void
GENX(csf_preload_fb)(struct panfrost_batch *batch, struct pan_fb_info *fb)
{
   struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

   GENX(pan_preload_fb)
   (&dev->fb_preload_cache, &batch->pool.base, fb, batch->tls.gpu, NULL);
}

#define GET_FBD(_ctx, _pass)                                                   \
   (_ctx)->fbds[PAN_INCREMENTAL_RENDERING_##_pass##_PASS]
#define EMIT_FBD(_ctx, _pass, _fb, _tls, _tiler_ctx)                           \
   GET_FBD(_ctx, _pass).gpu |=                                                 \
      GENX(pan_emit_fbd)(_fb, 0, _tls, _tiler_ctx, GET_FBD(_ctx, _pass).cpu)

void
GENX(csf_emit_fbds)(struct panfrost_batch *batch, struct pan_fb_info *fb,
                    struct pan_tls_info *tls)
{
   struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

   /* Default framebuffer descriptor */

   batch->framebuffer.gpu |=
      GENX(pan_emit_fbd)(fb, 0, tls, &batch->tiler_ctx, batch->framebuffer.cpu);

   if (batch->draw_count == 0)
      return;

   struct pan_csf_tiler_oom_ctx *tiler_oom_ctx = batch->csf.tiler_oom_ctx.cpu;
   struct pan_fb_info alt_fb;
   bool changed = false;

   /* First incremental rendering pass: don't discard result */

   memcpy(&alt_fb, fb, sizeof(alt_fb));
   for (unsigned i = 0; i < fb->rt_count; i++)
      alt_fb.rts[i].discard = false;
   alt_fb.zs.discard.z = false;
   alt_fb.zs.discard.s = false;

   EMIT_FBD(tiler_oom_ctx, FIRST, &alt_fb, tls, &batch->tiler_ctx);

   /* Subsequent incremental rendering passes: preload old content and don't
    * discard result */

   for (unsigned i = 0; i < fb->rt_count; i++) {
      if (fb->rts[i].view && !fb->rts[i].preload) {
         alt_fb.rts[i].preload = true;
         changed = true;
      }

      if (alt_fb.rts[i].clear) {
         alt_fb.rts[i].clear = false;
         changed = true;
      }
   }
   if (fb->zs.view.zs && !fb->zs.preload.z && !fb->zs.preload.s) {
      alt_fb.zs.preload.z = true;
      alt_fb.zs.preload.s = true;
      changed = true;
   } else if (fb->zs.view.s && !fb->zs.preload.s) {
      alt_fb.zs.preload.s = true;
      changed = true;
   }

   if (alt_fb.zs.clear.z || alt_fb.zs.clear.s) {
      alt_fb.zs.clear.z = false;
      alt_fb.zs.clear.s = false;
      changed = true;
   }

   if (changed) {
      alt_fb.bifrost.pre_post.dcds.gpu = 0;
      GENX(pan_preload_fb)
      (&dev->fb_preload_cache, &batch->pool.base, &alt_fb, batch->tls.gpu, NULL);
   }

   EMIT_FBD(tiler_oom_ctx, MIDDLE, &alt_fb, tls, &batch->tiler_ctx);

   /* Last incremental rendering pass: preload previous content and deal with
    * results as specified by user */

   for (unsigned i = 0; i < fb->rt_count; i++)
      alt_fb.rts[i].discard = fb->rts[i].discard;
   alt_fb.zs.discard.z = fb->zs.discard.z;
   alt_fb.zs.discard.s = fb->zs.discard.s;

   EMIT_FBD(tiler_oom_ctx, LAST, &alt_fb, tls, &batch->tiler_ctx);
}

void
GENX(csf_emit_fragment_job)(struct panfrost_batch *batch,
                            const struct pan_fb_info *pfb)
{
   struct cs_builder *b = batch->csf.cs.builder;
   struct pan_csf_tiler_oom_ctx *oom_ctx = batch->csf.tiler_oom_ctx.cpu;

   if (batch->draw_count > 0) {
      /* Finish tiling and wait for IDVS and tiling */
      cs_finish_tiling(b, false);
      cs_wait_slot(b, 2, false);
      cs_vt_end(b, cs_now());
   }

   /* Set up the fragment job */
   cs_move64_to(b, cs_reg64(b, 40), batch->framebuffer.gpu);
   cs_move32_to(b, cs_reg32(b, 42), (batch->miny << 16) | batch->minx);
   cs_move32_to(b, cs_reg32(b, 43),
                ((batch->maxy - 1) << 16) | (batch->maxx - 1));
   cs_move64_to(b, cs_reg64(b, 44), 0);
   cs_move32_to(b, cs_reg32(b, 46), 0);

   /* Use different framebuffer descriptor if incremental rendering was
    * triggered while tiling */
   if (batch->draw_count > 0) {
      struct cs_index counter = cs_reg32(b, 78);
      cs_load32_to(b, counter, cs_reg64(b, TILER_OOM_CTX_REG), 0);
      cs_wait_slot(b, 0, false);
      cs_if(b, MALI_CS_CONDITION_GREATER, counter) {
         cs_move64_to(b, cs_reg64(b, 40), GET_FBD(oom_ctx, LAST).gpu);
      }
   }

   /* Run the fragment job and wait */
   cs_run_fragment(b, false, MALI_TILE_RENDER_ORDER_Z_ORDER, false);
   cs_wait_slot(b, 2, false);

   /* Gather freed heap chunks and add them to the heap context free list
    * so they can be re-used next time the tiler heap runs out of chunks.
    * That's what cs_finish_fragment() is all about. The list of freed
    * chunks is in the tiler context descriptor
    * (completed_{top,bottom fields}). */
   if (batch->draw_count > 0) {
      assert(batch->tiler_ctx.valhall.desc);
      cs_move64_to(b, cs_reg64(b, 90), batch->tiler_ctx.valhall.desc);
      cs_load_to(b, cs_reg_tuple(b, 86, 4), cs_reg64(b, 90), BITFIELD_MASK(4),
                 40);
      cs_wait_slot(b, 0, false);
      cs_finish_fragment(b, true, cs_reg64(b, 86), cs_reg64(b, 88), cs_now());
   }
}

static void
csf_emit_shader_regs(struct panfrost_batch *batch, enum pipe_shader_type stage,
                     uint64_t shader)
{
   uint64_t resources = panfrost_emit_resources(batch, stage);

   assert(stage == PIPE_SHADER_VERTEX || stage == PIPE_SHADER_FRAGMENT ||
          stage == PIPE_SHADER_COMPUTE);

   unsigned offset = (stage == PIPE_SHADER_FRAGMENT) ? 4 : 0;
   unsigned fau_count = DIV_ROUND_UP(batch->nr_push_uniforms[stage], 2);

   struct cs_builder *b = batch->csf.cs.builder;
   cs_move64_to(b, cs_reg64(b, 0 + offset), resources);
   cs_move64_to(b, cs_reg64(b, 8 + offset),
                batch->push_uniforms[stage] | ((uint64_t)fau_count << 56));
   cs_move64_to(b, cs_reg64(b, 16 + offset), shader);
}

void
GENX(csf_launch_grid)(struct panfrost_batch *batch,
                      const struct pipe_grid_info *info)
{
   /* Empty compute programs are invalid and don't make sense */
   if (batch->rsd[PIPE_SHADER_COMPUTE] == 0)
      return;

   struct panfrost_context *ctx = batch->ctx;
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   struct panfrost_compiled_shader *cs = ctx->prog[PIPE_SHADER_COMPUTE];
   struct cs_builder *b = batch->csf.cs.builder;

   csf_emit_shader_regs(batch, PIPE_SHADER_COMPUTE,
                        batch->rsd[PIPE_SHADER_COMPUTE]);

   cs_move64_to(b, cs_reg64(b, 24), batch->tls.gpu);

   /* Global attribute offset */
   cs_move32_to(b, cs_reg32(b, 32), 0);

   /* Compute workgroup size */
   struct mali_compute_size_workgroup_packed wg_size;
   pan_pack(&wg_size, COMPUTE_SIZE_WORKGROUP, cfg) {
      cfg.workgroup_size_x = info->block[0];
      cfg.workgroup_size_y = info->block[1];
      cfg.workgroup_size_z = info->block[2];

      /* Workgroups may be merged if the shader does not use barriers
       * or shared memory. This condition is checked against the
       * static shared_size at compile-time. We need to check the
       * variable shared size at launch_grid time, because the
       * compiler doesn't know about that.
       */
      cfg.allow_merging_workgroups = cs->info.cs.allow_merging_workgroups &&
                                     (info->variable_shared_mem == 0);
   }

   cs_move32_to(b, cs_reg32(b, 33), wg_size.opaque[0]);

   /* Offset */
   for (unsigned i = 0; i < 3; ++i)
      cs_move32_to(b, cs_reg32(b, 34 + i), 0);

   unsigned threads_per_wg = info->block[0] * info->block[1] * info->block[2];
   unsigned max_thread_cnt = panfrost_compute_max_thread_count(
      &dev->kmod.props, cs->info.work_reg_count);

   if (info->indirect) {
      /* Load size in workgroups per dimension from memory */
      struct cs_index address = cs_reg64(b, 64);
      cs_move64_to(
         b, address,
         pan_resource(info->indirect)->image.data.base + info->indirect_offset);

      struct cs_index grid_xyz = cs_reg_tuple(b, 37, 3);
      cs_load_to(b, grid_xyz, address, BITFIELD_MASK(3), 0);

      /* Wait for the load */
      cs_wait_slot(b, 0, false);

      /* Copy to FAU */
      for (unsigned i = 0; i < 3; ++i) {
         if (batch->num_wg_sysval[i]) {
            cs_move64_to(b, address, batch->num_wg_sysval[i]);
            cs_store(b, cs_extract32(b, grid_xyz, i), address, BITFIELD_MASK(1),
                     0);
         }
      }

      /* Wait for the stores */
      cs_wait_slot(b, 0, false);

      cs_run_compute_indirect(b, DIV_ROUND_UP(max_thread_cnt, threads_per_wg),
                              false, cs_shader_res_sel(0, 0, 0, 0));
   } else {
      /* Set size in workgroups per dimension immediately */
      for (unsigned i = 0; i < 3; ++i)
         cs_move32_to(b, cs_reg32(b, 37 + i), info->grid[i]);

      /* Pick the task_axis and task_increment to maximize thread utilization. */
      unsigned task_axis = MALI_TASK_AXIS_X;
      unsigned threads_per_task = threads_per_wg;
      unsigned task_increment = 0;

      for (unsigned i = 0; i < 3; i++) {
         if (threads_per_task * info->grid[i] >= max_thread_cnt) {
            /* We reached out thread limit, stop at the current axis and
             * calculate the increment so it doesn't exceed the per-core
             * thread capacity.
             */
            task_increment = max_thread_cnt / threads_per_task;
            break;
         } else if (task_axis == MALI_TASK_AXIS_Z) {
            /* We reached the Z axis, and there's still room to stuff more
             * threads. Pick the current axis grid size as our increment
             * as there's no point using something bigger.
             */
            task_increment = info->grid[i];
            break;
         }

         threads_per_task *= info->grid[i];
         task_axis++;
      }

      assert(task_axis <= MALI_TASK_AXIS_Z);
      assert(task_increment > 0);
      cs_run_compute(b, task_increment, task_axis, false,
                     cs_shader_res_sel(0, 0, 0, 0));
   }
}

void
GENX(csf_launch_xfb)(struct panfrost_batch *batch,
                     const struct pipe_draw_info *info, unsigned count)
{
   struct cs_builder *b = batch->csf.cs.builder;

   cs_move64_to(b, cs_reg64(b, 24), batch->tls.gpu);

   /* TODO: Indexing. Also, attribute_offset is a legacy feature.. */
   cs_move32_to(b, cs_reg32(b, 32), batch->ctx->offset_start);

   /* Compute workgroup size */
   struct mali_compute_size_workgroup_packed wg_size;
   pan_pack(&wg_size, COMPUTE_SIZE_WORKGROUP, cfg) {
      cfg.workgroup_size_x = 1;
      cfg.workgroup_size_y = 1;
      cfg.workgroup_size_z = 1;

      /* Transform feedback shaders do not use barriers or
       * shared memory, so we may merge workgroups.
       */
      cfg.allow_merging_workgroups = true;
   }
   cs_move32_to(b, cs_reg32(b, 33), wg_size.opaque[0]);

   /* Offset */
   for (unsigned i = 0; i < 3; ++i)
      cs_move32_to(b, cs_reg32(b, 34 + i), 0);

   cs_move32_to(b, cs_reg32(b, 37), count);
   cs_move32_to(b, cs_reg32(b, 38), info->instance_count);
   cs_move32_to(b, cs_reg32(b, 39), 1);

   csf_emit_shader_regs(batch, PIPE_SHADER_VERTEX,
                        batch->rsd[PIPE_SHADER_VERTEX]);
   /* force a barrier to avoid read/write sync issues with buffers */
   cs_wait_slot(b, 2, false);

   /* XXX: Choose correctly */
   cs_run_compute(b, 1, MALI_TASK_AXIS_Z, false, cs_shader_res_sel(0, 0, 0, 0));
}

static void
emit_tiler_oom_context(struct cs_builder *b, struct panfrost_batch *batch)
{
   struct pan_csf_tiler_oom_ctx *ctx;

   batch->csf.tiler_oom_ctx =
      pan_pool_alloc_aligned(&batch->pool.base, sizeof(*ctx), 8);
   ctx = batch->csf.tiler_oom_ctx.cpu;

   ctx->tiler_desc = csf_get_tiler_desc(batch);
   ctx->counter = 0;
   ctx->bbox_min = (batch->miny << 16) | batch->minx;
   ctx->bbox_max = ((batch->maxy - 1) << 16) | (batch->maxx - 1);
   ctx->dump_addr = batch->ctx->csf.tiler_oom_handler.save_bo->ptr.gpu;

   for (unsigned i = 0; i < PAN_INCREMENTAL_RENDERING_PASS_COUNT; ++i)
      ctx->fbds[i] = alloc_fbd(batch);

   csf_update_tiler_oom_ctx(b, batch->csf.tiler_oom_ctx.gpu);
}

static uint32_t
csf_emit_draw_state(struct panfrost_batch *batch,
                    const struct pipe_draw_info *info, unsigned drawid_offset)
{
   struct panfrost_context *ctx = batch->ctx;
   struct panfrost_compiled_shader *vs = ctx->prog[PIPE_SHADER_VERTEX];
   struct panfrost_compiled_shader *fs = ctx->prog[PIPE_SHADER_FRAGMENT];

   bool idvs = vs->info.vs.idvs;
   bool fs_required = panfrost_fs_required(
      fs, ctx->blend, &ctx->pipe_framebuffer, ctx->depth_stencil);
   bool secondary_shader = vs->info.vs.secondary_enable && fs_required;

   assert(idvs && "IDVS required for CSF");

   struct cs_builder *b = batch->csf.cs.builder;

   if (batch->draw_count == 0) {
      emit_tiler_oom_context(b, batch);
      cs_vt_start(batch->csf.cs.builder, cs_now());
   }

   csf_emit_shader_regs(batch, PIPE_SHADER_VERTEX,
                        panfrost_get_position_shader(batch, info));

   if (fs_required) {
      csf_emit_shader_regs(batch, PIPE_SHADER_FRAGMENT,
                           batch->rsd[PIPE_SHADER_FRAGMENT]);
   } else {
      cs_move64_to(b, cs_reg64(b, 4), 0);
      cs_move64_to(b, cs_reg64(b, 12), 0);
      cs_move64_to(b, cs_reg64(b, 20), 0);
   }

   if (secondary_shader) {
      cs_move64_to(b, cs_reg64(b, 18), panfrost_get_varying_shader(batch));
   }

   cs_move64_to(b, cs_reg64(b, 24), batch->tls.gpu);
   cs_move64_to(b, cs_reg64(b, 30), batch->tls.gpu);
   cs_move32_to(b, cs_reg32(b, 32), 0);
   cs_move32_to(b, cs_reg32(b, 37), 0);
   cs_move32_to(b, cs_reg32(b, 38), 0);

   cs_move64_to(b, cs_reg64(b, 40), csf_get_tiler_desc(batch));

   STATIC_ASSERT(sizeof(batch->scissor) == pan_size(SCISSOR));
   STATIC_ASSERT(sizeof(uint64_t) == pan_size(SCISSOR));
   uint64_t *sbd = (uint64_t *)&batch->scissor[0];
   cs_move64_to(b, cs_reg64(b, 42), *sbd);

   cs_move32_to(b, cs_reg32(b, 44), fui(batch->minimum_z));
   cs_move32_to(b, cs_reg32(b, 45), fui(batch->maximum_z));

   if (ctx->occlusion_query && ctx->active_queries) {
      struct panfrost_resource *rsrc = pan_resource(ctx->occlusion_query->rsrc);
      cs_move64_to(b, cs_reg64(b, 46), rsrc->image.data.base);
      panfrost_batch_write_rsrc(ctx->batch, rsrc, PIPE_SHADER_FRAGMENT);
   }

   cs_move32_to(b, cs_reg32(b, 48), panfrost_vertex_attribute_stride(vs, fs));
   cs_move64_to(b, cs_reg64(b, 50),
                batch->blend | MAX2(batch->key.nr_cbufs, 1));
   cs_move64_to(b, cs_reg64(b, 52), batch->depth_stencil);

   if (info->index_size)
      cs_move64_to(b, cs_reg64(b, 54), batch->indices);

   struct pipe_rasterizer_state *rast = &ctx->rasterizer->base;

   struct mali_primitive_flags_packed primitive_flags;
   pan_pack(&primitive_flags, PRIMITIVE_FLAGS, cfg) {
      if (panfrost_writes_point_size(ctx))
         cfg.point_size_array_format = MALI_POINT_SIZE_ARRAY_FORMAT_FP16;

      cfg.allow_rotating_primitives = allow_rotating_primitives(fs, info);

      cfg.low_depth_cull = rast->depth_clip_near;
      cfg.high_depth_cull = rast->depth_clip_far;

      /* Non-fixed restart indices should have been lowered */
      assert(!cfg.primitive_restart || panfrost_is_implicit_prim_restart(info));
      cfg.primitive_restart = info->primitive_restart;

      cfg.position_fifo_format = panfrost_writes_point_size(ctx)
                                    ? MALI_FIFO_FORMAT_EXTENDED
                                    : MALI_FIFO_FORMAT_BASIC;
   }

   cs_move32_to(b, cs_reg32(b, 56), primitive_flags.opaque[0]);

   struct mali_dcd_flags_0_packed dcd_flags0;
   struct mali_dcd_flags_1_packed dcd_flags1;

   pan_pack(&dcd_flags0, DCD_FLAGS_0, cfg) {
      enum mesa_prim reduced_mode = u_reduced_prim(info->mode);
      bool polygon = reduced_mode == MESA_PRIM_TRIANGLES;
      bool lines = reduced_mode == MESA_PRIM_LINES;

      /*
       * From the Gallium documentation,
       * pipe_rasterizer_state::cull_face "indicates which faces of
       * polygons to cull". Points and lines are not considered
       * polygons and should be drawn even if all faces are culled.
       * The hardware does not take primitive type into account when
       * culling, so we need to do that check ourselves.
       */
      cfg.cull_front_face = polygon && (rast->cull_face & PIPE_FACE_FRONT);
      cfg.cull_back_face = polygon && (rast->cull_face & PIPE_FACE_BACK);
      cfg.front_face_ccw = rast->front_ccw;

      cfg.multisample_enable = rast->multisample;

      /* Use per-sample shading if required by API Also use it when a
       * blend shader is used with multisampling, as this is handled
       * by a single ST_TILE in the blend shader with the current
       * sample ID, requiring per-sample shading.
       */
      cfg.evaluate_per_sample =
         (rast->multisample &&
          ((ctx->min_samples > 1) || ctx->valhall_has_blend_shader));

      cfg.single_sampled_lines = !rast->multisample;

      if (lines && rast->line_smooth) {
         cfg.multisample_enable = true;
         cfg.single_sampled_lines = false;
      }

      bool has_oq = ctx->occlusion_query && ctx->active_queries;
      if (has_oq) {
         if (ctx->occlusion_query->type == PIPE_QUERY_OCCLUSION_COUNTER)
            cfg.occlusion_query = MALI_OCCLUSION_MODE_COUNTER;
         else
            cfg.occlusion_query = MALI_OCCLUSION_MODE_PREDICATE;
      }

      if (fs_required) {
         struct pan_earlyzs_state earlyzs = pan_earlyzs_get(
            fs->earlyzs, ctx->depth_stencil->writes_zs || has_oq,
            ctx->blend->base.alpha_to_coverage,
            ctx->depth_stencil->zs_always_passes);

         cfg.pixel_kill_operation = earlyzs.kill;
         cfg.zs_update_operation = earlyzs.update;

         cfg.allow_forward_pixel_to_kill =
            pan_allow_forward_pixel_to_kill(ctx, fs);
         cfg.allow_forward_pixel_to_be_killed = !fs->info.writes_global;

         cfg.overdraw_alpha0 = panfrost_overdraw_alpha(ctx, 0);
         cfg.overdraw_alpha1 = panfrost_overdraw_alpha(ctx, 1);

         /* Also use per-sample shading if required by the shader
          */
         cfg.evaluate_per_sample |=
            (fs->info.fs.sample_shading && rast->multisample);

         /* Unlike Bifrost, alpha-to-coverage must be included in
          * this identically-named flag. Confusing, isn't it?
          */
         cfg.shader_modifies_coverage = fs->info.fs.writes_coverage ||
                                        fs->info.fs.can_discard ||
                                        ctx->blend->base.alpha_to_coverage;

         cfg.alpha_to_coverage = ctx->blend->base.alpha_to_coverage;
      } else {
         /* These operations need to be FORCE to benefit from the
          * depth-only pass optimizations.
          */
         cfg.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_EARLY;
         cfg.zs_update_operation = MALI_PIXEL_KILL_FORCE_EARLY;

         /* No shader and no blend => no shader or blend
          * reasons to disable FPK. The only FPK-related state
          * not covered is alpha-to-coverage which we don't set
          * without blend.
          */
         cfg.allow_forward_pixel_to_kill = true;

         /* No shader => no shader side effects */
         cfg.allow_forward_pixel_to_be_killed = true;

         /* Alpha isn't written so these are vacuous */
         cfg.overdraw_alpha0 = true;
         cfg.overdraw_alpha1 = true;
      }
   }

   pan_pack(&dcd_flags1, DCD_FLAGS_1, cfg) {
      cfg.sample_mask = rast->multisample ? ctx->sample_mask : 0xFFFF;

      if (fs_required) {
         /* See JM Valhall equivalent code */
         cfg.render_target_mask =
            (fs->info.outputs_written >> FRAG_RESULT_DATA0) & ctx->fb_rt_mask;
      }
   }

   cs_move32_to(b, cs_reg32(b, 57), dcd_flags0.opaque[0]);
   cs_move32_to(b, cs_reg32(b, 58), dcd_flags1.opaque[0]);

   struct mali_primitive_size_packed primsize;
   panfrost_emit_primitive_size(ctx, info->mode == MESA_PRIM_POINTS, 0,
                                &primsize);
   struct mali_primitive_size_packed *primsize_ptr = &primsize;
   cs_move64_to(b, cs_reg64(b, 60), *((uint64_t*)primsize_ptr));

   struct mali_primitive_flags_packed flags_override;
   /* Pack with nodefaults so only explicitly set override fields affect the
    * previously set register values */
   pan_pack_nodefaults(&flags_override, PRIMITIVE_FLAGS, cfg) {
      cfg.draw_mode = pan_draw_mode(info->mode);
      cfg.index_type = panfrost_translate_index_size(info->index_size);
      cfg.secondary_shader = secondary_shader;
   };

   return flags_override.opaque[0];
}

static struct cs_index
csf_emit_draw_id_register(struct panfrost_batch *batch, unsigned offset)
{
   struct cs_builder *b = batch->csf.cs.builder;
   struct panfrost_context *ctx = batch->ctx;
   struct panfrost_uncompiled_shader *vs = ctx->uncompiled[PIPE_SHADER_VERTEX];

   if (!BITSET_TEST(vs->nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID))
      return cs_undef();

   struct cs_index drawid = cs_reg32(b, 67);
   cs_move32_to(b, drawid, offset);

   return drawid;
}

void
GENX(csf_launch_draw)(struct panfrost_batch *batch,
                      const struct pipe_draw_info *info, unsigned drawid_offset,
                      const struct pipe_draw_start_count_bias *draw,
                      unsigned vertex_count)
{
   struct cs_builder *b = batch->csf.cs.builder;

   uint32_t flags_override = csf_emit_draw_state(batch, info, drawid_offset);
   struct cs_index drawid = csf_emit_draw_id_register(batch, drawid_offset);

   cs_move32_to(b, cs_reg32(b, 33), draw->count);
   cs_move32_to(b, cs_reg32(b, 34), info->instance_count);
   cs_move32_to(b, cs_reg32(b, 35), 0);

   /* Base vertex offset on Valhall is used for both indexed and
    * non-indexed draws, in a simple way for either. Handle both cases.
    */
   if (info->index_size) {
      cs_move32_to(b, cs_reg32(b, 36), draw->index_bias);
      cs_move32_to(b, cs_reg32(b, 39), info->index_size * draw->count);
   } else {
      cs_move32_to(b, cs_reg32(b, 36), draw->start);
      cs_move32_to(b, cs_reg32(b, 39), 0);
   }

   cs_run_idvs(b, flags_override, false, true, cs_shader_res_sel(0, 0, 1, 0),
               cs_shader_res_sel(2, 2, 2, 0), drawid);
}

void
GENX(csf_launch_draw_indirect)(struct panfrost_batch *batch,
                               const struct pipe_draw_info *info,
                               unsigned drawid_offset,
                               const struct pipe_draw_indirect_info *indirect)
{
   struct cs_builder *b = batch->csf.cs.builder;

   uint32_t flags_override = csf_emit_draw_state(batch, info, drawid_offset);
   struct cs_index drawid = csf_emit_draw_id_register(batch, drawid_offset);

   struct cs_index address = cs_reg64(b, 64);
   struct cs_index counter = cs_reg32(b, 66);
   cs_move64_to(
      b, address,
      pan_resource(indirect->buffer)->image.data.base + indirect->offset);
   cs_move32_to(b, counter, indirect->draw_count);

   cs_while(b, MALI_CS_CONDITION_GREATER, counter) {
      if (info->index_size) {
         /* loads vertex count, instance count, index offset, vertex offset */
         cs_load_to(b, cs_reg_tuple(b, 33, 4), address, BITFIELD_MASK(4), 0);
         cs_move32_to(b, cs_reg32(b, 39), info->index.resource->width0);
      } else {
         /* vertex count, instance count */
         cs_load_to(b, cs_reg_tuple(b, 33, 2), address, BITFIELD_MASK(2), 0);
         cs_move32_to(b, cs_reg32(b, 35), 0);
         cs_load_to(b, cs_reg_tuple(b, 36, 1), address, BITFIELD_MASK(1),
                    2 * sizeof(uint32_t)); // instance offset
         cs_move32_to(b, cs_reg32(b, 37), 0);
         cs_move32_to(b, cs_reg32(b, 39), 0);
      }

      cs_wait_slot(b, 0, false);
      cs_run_idvs(b, flags_override, false, true, cs_shader_res_sel(0, 0, 1, 0),
                  cs_shader_res_sel(2, 2, 2, 0), drawid);

      cs_add64(b, address, address, indirect->stride);
      cs_add32(b, counter, counter, (unsigned int)-1);
      if (drawid.type != CS_INDEX_UNDEF)
         cs_add32(b, drawid, drawid, 1);
   }
}

#define POSITION_FIFO_SIZE (64 * 1024)

static enum drm_panthor_group_priority
get_panthor_group_priority(struct panfrost_context *ctx)
{
   if (ctx->flags & PIPE_CONTEXT_REALTIME_PRIORITY)
      return PANTHOR_GROUP_PRIORITY_REALTIME;
   else if (ctx->flags & PIPE_CONTEXT_HIGH_PRIORITY)
      return PANTHOR_GROUP_PRIORITY_HIGH;
   else if (ctx->flags & PIPE_CONTEXT_LOW_PRIORITY)
      return PANTHOR_GROUP_PRIORITY_LOW;

   return PANTHOR_GROUP_PRIORITY_MEDIUM;
}

int
GENX(csf_init_context)(struct panfrost_context *ctx)
{
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   struct drm_panthor_queue_create qc[] = {{
      .priority = 1,
      .ringbuf_size = 64 * 1024,
   }};

   struct drm_panthor_group_create gc = {
      .compute_core_mask = dev->kmod.props.shader_present,
      .fragment_core_mask = dev->kmod.props.shader_present,
      .tiler_core_mask = 1,
      .max_compute_cores = util_bitcount64(dev->kmod.props.shader_present),
      .max_fragment_cores = util_bitcount64(dev->kmod.props.shader_present),
      .max_tiler_cores = 1,
      .priority = get_panthor_group_priority(ctx),
      .queues = DRM_PANTHOR_OBJ_ARRAY(ARRAY_SIZE(qc), qc),
      .vm_id = pan_kmod_vm_handle(dev->kmod.vm),
   };

   int ret =
      drmIoctl(panfrost_device_fd(dev), DRM_IOCTL_PANTHOR_GROUP_CREATE, &gc);

   if (ret)
      goto err_group_create;

   ctx->csf.group_handle = gc.group_handle;

   struct drm_panthor_group_destroy gd = {
      .group_handle = ctx->csf.group_handle,
   };

   /* Get tiler heap */
   struct drm_panthor_tiler_heap_create thc = {
      .vm_id = pan_kmod_vm_handle(dev->kmod.vm),
      .chunk_size = pan_screen(ctx->base.screen)->csf_tiler_heap.chunk_size,
      .initial_chunk_count =
         pan_screen(ctx->base.screen)->csf_tiler_heap.initial_chunks,
      .max_chunks = pan_screen(ctx->base.screen)->csf_tiler_heap.max_chunks,
      .target_in_flight = 65535,
   };
   ret = drmIoctl(panfrost_device_fd(dev), DRM_IOCTL_PANTHOR_TILER_HEAP_CREATE,
                  &thc);

   if (ret)
      goto err_tiler_heap;

   ctx->csf.heap.handle = thc.handle;

   struct drm_panthor_tiler_heap_destroy thd = {
      .handle = ctx->csf.heap.handle,
   };

   ctx->csf.heap.desc_bo =
      panfrost_bo_create(dev, pan_size(TILER_HEAP), 0, "Tiler Heap");

   if (ctx->csf.heap.desc_bo == NULL)
      goto err_tiler_heap_desc_bo;

   pan_cast_and_pack(ctx->csf.heap.desc_bo->ptr.cpu, TILER_HEAP, heap) {
      heap.size = pan_screen(ctx->base.screen)->csf_tiler_heap.chunk_size;
      heap.base = thc.first_heap_chunk_gpu_va;
      heap.bottom = heap.base + 64;
      heap.top = heap.base + heap.size;
   }

   ctx->csf.tmp_geom_bo = panfrost_bo_create(
      dev, POSITION_FIFO_SIZE, PAN_BO_INVISIBLE, "Temporary Geometry buffer");

   if (ctx->csf.tmp_geom_bo == NULL)
      goto err_tiler_heap_tmp_geom_bo;

   /* Setup the tiler heap */
   struct panfrost_bo *cs_bo =
      panfrost_bo_create(dev, 4096, 0, "Temporary CS buffer");

   if (cs_bo == NULL)
      goto err_tiler_heap_cs_bo;

   if (csf_oom_handler_init(ctx))
      goto err_g_submit;

   struct cs_buffer init_buffer = {
      .cpu = cs_bo->ptr.cpu,
      .gpu = cs_bo->ptr.gpu,
      .capacity = panfrost_bo_size(cs_bo) / sizeof(uint64_t),
   };
   const struct cs_builder_conf bconf = {
      .nr_registers = 96,
      .nr_kernel_registers = 4,
   };
   struct cs_builder b;
   cs_builder_init(&b, &bconf, init_buffer);
   struct cs_index heap = cs_reg64(&b, 72);
   cs_move64_to(&b, heap, thc.tiler_heap_ctx_gpu_va);
   cs_heap_set(&b, heap);

   struct cs_index addr_reg = cs_reg64(&b, 86);
   struct cs_index length_reg = cs_reg32(&b, 88);
   cs_move64_to(&b, addr_reg, ctx->csf.tiler_oom_handler.cs_bo->ptr.gpu);
   cs_move32_to(&b, length_reg, ctx->csf.tiler_oom_handler.length);
   cs_set_exception_handler(&b, MALI_CS_EXCEPTION_TYPE_TILER_OOM,
                            addr_reg, length_reg);

   struct drm_panthor_queue_submit qsubmit;
   struct drm_panthor_group_submit gsubmit;
   struct drm_panthor_sync_op sync = {
      .flags =
         DRM_PANTHOR_SYNC_OP_SIGNAL | DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ,
      .handle = ctx->syncobj,
   };

   assert(cs_is_valid(&b));
   cs_finish(&b);

   uint64_t cs_start = cs_root_chunk_gpu_addr(&b);
   uint32_t cs_size = cs_root_chunk_size(&b);

   csf_prepare_qsubmit(ctx, &qsubmit, 0, cs_start, cs_size, &sync, 1);
   csf_prepare_gsubmit(ctx, &gsubmit, &qsubmit, 1);
   ret = csf_submit_gsubmit(ctx, &gsubmit);

   if (dev->debug & PAN_DBG_TRACE) {
      uint32_t regs[256] = {0};
      pandecode_interpret_cs(dev->decode_ctx, qsubmit.stream_addr,
                             qsubmit.stream_size, panfrost_device_gpu_id(dev),
                             regs);
   }

   if (ret)
      goto err_g_submit;

   /* Wait before freeing the buffer. */
   ret = drmSyncobjWait(panfrost_device_fd(dev), &ctx->syncobj, 1, INT64_MAX, 0,
                        NULL);
   assert(!ret);

   panfrost_bo_unreference(cs_bo);

   ctx->csf.is_init = true;
   return 0;
err_g_submit:
   panfrost_bo_unreference(cs_bo);
err_tiler_heap_cs_bo:
   panfrost_bo_unreference(ctx->csf.tmp_geom_bo);
err_tiler_heap_tmp_geom_bo:
   panfrost_bo_unreference(ctx->csf.heap.desc_bo);
err_tiler_heap_desc_bo:
   drmIoctl(panfrost_device_fd(dev), DRM_IOCTL_PANTHOR_TILER_HEAP_DESTROY,
            &thd);
err_tiler_heap:
   drmIoctl(panfrost_device_fd(dev), DRM_IOCTL_PANTHOR_GROUP_DESTROY, &gd);
err_group_create:
   return -1;
}

void
GENX(csf_cleanup_context)(struct panfrost_context *ctx)
{
   if (!ctx->csf.is_init)
      return;

   struct panfrost_device *dev = pan_device(ctx->base.screen);
   struct drm_panthor_tiler_heap_destroy thd = {
      .handle = ctx->csf.heap.handle,
   };
   int ret;

   /* Make sure all jobs are done before destroying the heap. */
   ret = drmSyncobjWait(panfrost_device_fd(dev), &ctx->syncobj, 1, INT64_MAX, 0,
                        NULL);
   assert(!ret);

   ret = drmIoctl(panfrost_device_fd(dev), DRM_IOCTL_PANTHOR_TILER_HEAP_DESTROY,
                  &thd);
   assert(!ret);

   struct drm_panthor_group_destroy gd = {
      .group_handle = ctx->csf.group_handle,
   };

   ret =
      drmIoctl(panfrost_device_fd(dev), DRM_IOCTL_PANTHOR_GROUP_DESTROY, &gd);
   assert(!ret);

   panfrost_bo_unreference(ctx->csf.tmp_geom_bo);
   panfrost_bo_unreference(ctx->csf.heap.desc_bo);
   panfrost_bo_unreference(ctx->csf.tiler_oom_handler.cs_bo);
   panfrost_bo_unreference(ctx->csf.tiler_oom_handler.save_bo);
   ctx->csf.is_init = false;
}

void
GENX(csf_emit_write_timestamp)(struct panfrost_batch *batch,
                               struct panfrost_resource *dst, unsigned offset)
{
   struct cs_builder *b = batch->csf.cs.builder;

   struct cs_index address = cs_reg64(b, 40);
   cs_move64_to(b, address,
                dst->image.data.base + dst->image.data.offset + offset);
   cs_store_state(b, address, 0, MALI_CS_STATE_TIMESTAMP, cs_now());

   panfrost_batch_write_rsrc(batch, dst, PIPE_SHADER_VERTEX);
}
