/*
 * Copyright © 2024 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "drm-uapi/panthor_drm.h"

#include "genxml/cs_builder.h"
#include "genxml/decode.h"

#include "panvk_cmd_buffer.h"
#include "panvk_macros.h"
#include "panvk_queue.h"

#include "vk_drm_syncobj.h"
#include "vk_log.h"

static void
finish_render_desc_ringbuf(struct panvk_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_desc_ringbuf *ringbuf = &queue->render_desc_ringbuf;

   panvk_pool_free_mem(&ringbuf->syncobj);

   if (dev->debug.decode_ctx && ringbuf->addr.dev) {
      pandecode_inject_free(dev->debug.decode_ctx, ringbuf->addr.dev,
                            RENDER_DESC_RINGBUF_SIZE);
      pandecode_inject_free(dev->debug.decode_ctx,
                            ringbuf->addr.dev + RENDER_DESC_RINGBUF_SIZE,
                            RENDER_DESC_RINGBUF_SIZE);
   }

   if (ringbuf->addr.dev) {
      struct pan_kmod_vm_op op = {
         .type = PAN_KMOD_VM_OP_TYPE_UNMAP,
         .va = {
            .start = ringbuf->addr.dev,
            .size = RENDER_DESC_RINGBUF_SIZE * 2,
         },
      };

      ASSERTED int ret =
         pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &op, 1);
      assert(!ret);

      simple_mtx_lock(&dev->as.lock);
      util_vma_heap_free(&dev->as.heap, ringbuf->addr.dev,
                         RENDER_DESC_RINGBUF_SIZE * 2);
      simple_mtx_unlock(&dev->as.lock);
   }

   if (ringbuf->addr.host) {
      ASSERTED int ret =
         os_munmap(ringbuf->addr.host, RENDER_DESC_RINGBUF_SIZE);
      assert(!ret);
   }

   pan_kmod_bo_put(ringbuf->bo);
}

static VkResult
init_render_desc_ringbuf(struct panvk_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   uint32_t flags = panvk_device_adjust_bo_flags(dev, PAN_KMOD_BO_FLAG_NO_MMAP);
   struct panvk_desc_ringbuf *ringbuf = &queue->render_desc_ringbuf;
   const size_t size = RENDER_DESC_RINGBUF_SIZE;
   uint64_t dev_addr = 0;
   VkResult result;
   int ret;

   ringbuf->bo = pan_kmod_bo_alloc(dev->kmod.dev, dev->kmod.vm, size, flags);
   if (!ringbuf->bo)
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to create a descriptor ring buffer context");

   if (!(flags & PAN_KMOD_BO_FLAG_NO_MMAP)) {
      ringbuf->addr.host = pan_kmod_bo_mmap(
         ringbuf->bo, 0, size, PROT_READ | PROT_WRITE, MAP_SHARED, NULL);
      if (ringbuf->addr.host == MAP_FAILED) {
         result = panvk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                               "Failed to CPU map ringbuf BO");
         goto err_finish_ringbuf;
      }
   }

   /* We choose the alignment to guarantee that we won't ever cross a 4G
    * boundary when accessing the mapping. This way we can encode the wraparound
    * using 32-bit operations. */
   simple_mtx_lock(&dev->as.lock);
   dev_addr = util_vma_heap_alloc(&dev->as.heap, size * 2, size * 2);
   simple_mtx_unlock(&dev->as.lock);

   if (!dev_addr) {
      result =
         panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                      "Failed to allocate virtual address for ringbuf BO");
      goto err_finish_ringbuf;
   }

   struct pan_kmod_vm_op vm_ops[] = {
      {
         .type = PAN_KMOD_VM_OP_TYPE_MAP,
         .va = {
            .start = dev_addr,
            .size = RENDER_DESC_RINGBUF_SIZE,
         },
         .map = {
            .bo = ringbuf->bo,
            .bo_offset = 0,
         },
      },
      {
         .type = PAN_KMOD_VM_OP_TYPE_MAP,
         .va = {
            .start = dev_addr + RENDER_DESC_RINGBUF_SIZE,
            .size = RENDER_DESC_RINGBUF_SIZE,
         },
         .map = {
            .bo = ringbuf->bo,
            .bo_offset = 0,
         },
      },
   };

   ret = pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, vm_ops,
                          ARRAY_SIZE(vm_ops));
   if (ret) {
      result = panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                            "Failed to GPU map ringbuf BO");
      goto err_finish_ringbuf;
   }

   ringbuf->addr.dev = dev_addr;

   if (dev->debug.decode_ctx) {
      pandecode_inject_mmap(dev->debug.decode_ctx, ringbuf->addr.dev,
                            ringbuf->addr.host, RENDER_DESC_RINGBUF_SIZE, NULL);
      pandecode_inject_mmap(dev->debug.decode_ctx,
                            ringbuf->addr.dev + RENDER_DESC_RINGBUF_SIZE,
                            ringbuf->addr.host, RENDER_DESC_RINGBUF_SIZE, NULL);
   }

   struct panvk_pool_alloc_info alloc_info = {
      .size = sizeof(struct panvk_cs_sync32),
      .alignment = 64,
   };

   ringbuf->syncobj = panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);

   struct panvk_cs_sync32 *syncobj = panvk_priv_mem_host_addr(ringbuf->syncobj);

   if (!syncobj) {
      result = panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                            "Failed to create the render desc ringbuf context");
      goto err_finish_ringbuf;
   }

   *syncobj = (struct panvk_cs_sync32){
      .seqno = RENDER_DESC_RINGBUF_SIZE,
   };

   return VK_SUCCESS;

err_finish_ringbuf:
   if (dev_addr && !ringbuf->addr.dev) {
      simple_mtx_lock(&dev->as.lock);
      util_vma_heap_free(&dev->as.heap, dev_addr, size * 2);
      simple_mtx_unlock(&dev->as.lock);
   }

   finish_render_desc_ringbuf(queue);
   return result;
}

static VkResult
init_subqueue(struct panvk_queue *queue, enum panvk_subqueue_id subqueue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_subqueue *subq = &queue->subqueues[subqueue];
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(queue->vk.base.device->physical);
   struct panvk_instance *instance =
      to_panvk_instance(dev->vk.physical->instance);
   unsigned debug = instance->debug_flags;
   struct panvk_cs_sync64 *syncobjs = panvk_priv_mem_host_addr(queue->syncobjs);

   struct panvk_pool_alloc_info alloc_info = {
      .size = sizeof(struct panvk_cs_subqueue_context),
      .alignment = 64,
   };

   subq->context = panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
   if (!panvk_priv_mem_host_addr(subq->context))
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to create a queue context");

   struct panvk_cs_subqueue_context *cs_ctx =
      panvk_priv_mem_host_addr(subq->context);

   *cs_ctx = (struct panvk_cs_subqueue_context){
      .syncobjs = panvk_priv_mem_dev_addr(queue->syncobjs),
      .debug_syncobjs = panvk_priv_mem_dev_addr(queue->debug_syncobjs),
      .iter_sb = 0,
   };

   /* We use the geometry buffer for our temporary CS buffer. */
   struct cs_buffer root_cs = {
      .cpu = panvk_priv_mem_host_addr(queue->tiler_heap.desc) + 4096,
      .gpu = panvk_priv_mem_dev_addr(queue->tiler_heap.desc) + 4096,
      .capacity = 64 * 1024 / sizeof(uint64_t),
   };
   const struct cs_builder_conf conf = {
      .nr_registers = 96,
      .nr_kernel_registers = 4,
   };
   struct cs_builder b;

   assert(panvk_priv_mem_dev_addr(queue->tiler_heap.desc) != 0);

   cs_builder_init(&b, &conf, root_cs);
   /* Pass the context. */
   cs_move64_to(&b, cs_subqueue_ctx_reg(&b),
                panvk_priv_mem_dev_addr(subq->context));

   /* Intialize scoreboard slots used for asynchronous operations. */
   cs_set_scoreboard_entry(&b, SB_ITER(0), SB_ID(LS));

   /* We do greater than test on sync objects, and given the reference seqno
    * registers are all zero at init time, we need to initialize all syncobjs
    * with a seqno of one. */
   syncobjs[subqueue].seqno = 1;

   if (subqueue != PANVK_SUBQUEUE_COMPUTE) {
      cs_ctx->render.tiler_heap =
         panvk_priv_mem_dev_addr(queue->tiler_heap.desc);
      /* Our geometry buffer comes 4k after the tiler heap, and we encode the
       * size in the lower 12 bits so the address can be copied directly
       * to the tiler descriptors. */
      cs_ctx->render.geom_buf =
         (cs_ctx->render.tiler_heap + 4096) | ((64 * 1024) >> 12);

      /* Initialize the ringbuf */
      cs_ctx->render.desc_ringbuf = (struct panvk_cs_desc_ringbuf){
         .syncobj = panvk_priv_mem_dev_addr(queue->render_desc_ringbuf.syncobj),
         .ptr = queue->render_desc_ringbuf.addr.dev,
         .pos = 0,
      };

      struct cs_index heap_ctx_addr = cs_scratch_reg64(&b, 0);

      /* Pre-set the heap context on the vertex-tiler/fragment queues. */
      cs_move64_to(&b, heap_ctx_addr, queue->tiler_heap.context.dev_addr);
      cs_heap_set(&b, heap_ctx_addr);
   }

   cs_finish(&b);

   assert(cs_is_valid(&b));

   struct drm_panthor_sync_op syncop = {
      .flags =
         DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ | DRM_PANTHOR_SYNC_OP_SIGNAL,
      .handle = queue->syncobj_handle,
      .timeline_value = 0,
   };
   struct drm_panthor_queue_submit qsubmit = {
      .queue_index = subqueue,
      .stream_size = cs_root_chunk_size(&b),
      .stream_addr = cs_root_chunk_gpu_addr(&b),
      .latest_flush = panthor_kmod_get_flush_id(dev->kmod.dev),
      .syncs = DRM_PANTHOR_OBJ_ARRAY(1, &syncop),
   };
   struct drm_panthor_group_submit gsubmit = {
      .group_handle = queue->group_handle,
      .queue_submits = DRM_PANTHOR_OBJ_ARRAY(1, &qsubmit),
   };

   int ret = drmIoctl(dev->vk.drm_fd, DRM_IOCTL_PANTHOR_GROUP_SUBMIT, &gsubmit);
   if (ret)
      return panvk_errorf(dev->vk.physical, VK_ERROR_INITIALIZATION_FAILED,
                          "Failed to initialized subqueue: %m");

   ret = drmSyncobjWait(dev->vk.drm_fd, &queue->syncobj_handle, 1, INT64_MAX, 0,
                        NULL);
   if (ret)
      return panvk_errorf(dev->vk.physical, VK_ERROR_INITIALIZATION_FAILED,
                          "SyncobjWait failed: %m");

   if (debug & PANVK_DEBUG_TRACE) {
      uint32_t regs[256] = {0};

      pandecode_cs(dev->debug.decode_ctx, qsubmit.stream_addr,
                   qsubmit.stream_size, phys_dev->kmod.props.gpu_prod_id, regs);
      pandecode_next_frame(dev->debug.decode_ctx);
   }

   return VK_SUCCESS;
}

static void
cleanup_queue(struct panvk_queue *queue)
{
   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++)
      panvk_pool_free_mem(&queue->subqueues[i].context);

   finish_render_desc_ringbuf(queue);

   panvk_pool_free_mem(&queue->debug_syncobjs);
   panvk_pool_free_mem(&queue->syncobjs);
}

static VkResult
init_queue(struct panvk_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_instance *instance =
      to_panvk_instance(dev->vk.physical->instance);
   VkResult result;

   struct panvk_pool_alloc_info alloc_info = {
      .size =
         ALIGN_POT(sizeof(struct panvk_cs_sync64), 64) * PANVK_SUBQUEUE_COUNT,
      .alignment = 64,
   };

   queue->syncobjs = panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
   if (!panvk_priv_mem_host_addr(queue->syncobjs))
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to allocate subqueue sync objects");

   if (instance->debug_flags & (PANVK_DEBUG_SYNC | PANVK_DEBUG_TRACE)) {
      alloc_info.size =
         ALIGN_POT(sizeof(struct panvk_cs_sync32), 64) * PANVK_SUBQUEUE_COUNT,
      queue->debug_syncobjs =
         panvk_pool_alloc_mem(&dev->mempools.rw_nc, alloc_info);
      if (!panvk_priv_mem_host_addr(queue->debug_syncobjs)) {
         result = panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                               "Failed to allocate subqueue sync objects");
         goto err_cleanup_queue;
      }
   }

   result = init_render_desc_ringbuf(queue);
   if (result != VK_SUCCESS)
      goto err_cleanup_queue;

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      result = init_subqueue(queue, i);
      if (result != VK_SUCCESS)
         goto err_cleanup_queue;
   }

   return VK_SUCCESS;

err_cleanup_queue:
   cleanup_queue(queue);
   return result;
}

static VkResult
create_group(struct panvk_queue *queue)
{
   const struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(queue->vk.base.device->physical);

   struct drm_panthor_queue_create qc[] = {
      [PANVK_SUBQUEUE_VERTEX_TILER] =
         {
            .priority = 1,
            .ringbuf_size = 64 * 1024,
         },
      [PANVK_SUBQUEUE_FRAGMENT] =
         {
            .priority = 1,
            .ringbuf_size = 64 * 1024,
         },
      [PANVK_SUBQUEUE_COMPUTE] =
         {
            .priority = 1,
            .ringbuf_size = 64 * 1024,
         },
   };

   struct drm_panthor_group_create gc = {
      .compute_core_mask = phys_dev->kmod.props.shader_present,
      .fragment_core_mask = phys_dev->kmod.props.shader_present,
      .tiler_core_mask = 1,
      .max_compute_cores = util_bitcount64(phys_dev->kmod.props.shader_present),
      .max_fragment_cores =
         util_bitcount64(phys_dev->kmod.props.shader_present),
      .max_tiler_cores = 1,
      .priority = PANTHOR_GROUP_PRIORITY_MEDIUM,
      .queues = DRM_PANTHOR_OBJ_ARRAY(ARRAY_SIZE(qc), qc),
      .vm_id = pan_kmod_vm_handle(dev->kmod.vm),
   };

   int ret = drmIoctl(dev->vk.drm_fd, DRM_IOCTL_PANTHOR_GROUP_CREATE, &gc);
   if (ret)
      return panvk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                          "Failed to create a scheduling group");

   queue->group_handle = gc.group_handle;
   return VK_SUCCESS;
}

static void
destroy_group(struct panvk_queue *queue)
{
   const struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct drm_panthor_group_destroy gd = {
      .group_handle = queue->group_handle,
   };

   ASSERTED int ret =
      drmIoctl(dev->vk.drm_fd, DRM_IOCTL_PANTHOR_GROUP_DESTROY, &gd);
   assert(!ret);
}

static VkResult
init_tiler(struct panvk_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_tiler_heap *tiler_heap = &queue->tiler_heap;
   VkResult result;

   /* We allocate the tiler heap descriptor and geometry buffer in one go,
    * so we can pass it through a single 64-bit register to the VERTEX_TILER
    * command streams. */
   struct panvk_pool_alloc_info alloc_info = {
      .size = (64 * 1024) + 4096,
      .alignment = 4096,
   };

   tiler_heap->desc = panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
   if (!panvk_priv_mem_host_addr(tiler_heap->desc)) {
      result = panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                            "Failed to create a tiler heap context");
      goto err_free_desc;
   }

   tiler_heap->chunk_size = 2 * 1024 * 1024;

   struct drm_panthor_tiler_heap_create thc = {
      .vm_id = pan_kmod_vm_handle(dev->kmod.vm),
      .chunk_size = tiler_heap->chunk_size,
      .initial_chunk_count = 5,
      .max_chunks = 64,
      .target_in_flight = 65535,
   };

   int ret =
      drmIoctl(dev->vk.drm_fd, DRM_IOCTL_PANTHOR_TILER_HEAP_CREATE, &thc);
   if (ret) {
      result = panvk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                            "Failed to create a tiler heap context");
      goto err_free_desc;
   }

   tiler_heap->context.handle = thc.handle;
   tiler_heap->context.dev_addr = thc.tiler_heap_ctx_gpu_va;

   pan_pack(panvk_priv_mem_host_addr(tiler_heap->desc), TILER_HEAP, cfg) {
      cfg.size = tiler_heap->chunk_size;
      cfg.base = thc.first_heap_chunk_gpu_va;
      cfg.bottom = cfg.base + 64;
      cfg.top = cfg.base + cfg.size;
   }

   return VK_SUCCESS;

err_free_desc:
   panvk_pool_free_mem(&tiler_heap->desc);
   return result;
}

static void
cleanup_tiler(struct panvk_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_tiler_heap *tiler_heap = &queue->tiler_heap;
   struct drm_panthor_tiler_heap_destroy thd = {
      .handle = tiler_heap->context.handle,
   };
   ASSERTED int ret =
      drmIoctl(dev->vk.drm_fd, DRM_IOCTL_PANTHOR_TILER_HEAP_DESTROY, &thd);
   assert(!ret);

   panvk_pool_free_mem(&tiler_heap->desc);
}

static VkResult
panvk_queue_submit(struct vk_queue *vk_queue, struct vk_queue_submit *submit)
{
   struct panvk_queue *queue = container_of(vk_queue, struct panvk_queue, vk);
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(queue->vk.base.device->physical);
   VkResult result = VK_SUCCESS;
   int ret;

   if (vk_queue_is_lost(&queue->vk))
      return VK_ERROR_DEVICE_LOST;

   struct panvk_instance *instance =
      to_panvk_instance(dev->vk.physical->instance);
   unsigned debug = instance->debug_flags;
   bool force_sync = debug & (PANVK_DEBUG_TRACE | PANVK_DEBUG_SYNC);
   uint32_t qsubmit_count = 0;
   uint32_t used_queue_mask = 0;
   for (uint32_t i = 0; i < submit->command_buffer_count; i++) {
      struct panvk_cmd_buffer *cmdbuf =
         container_of(submit->command_buffers[i], struct panvk_cmd_buffer, vk);

      for (uint32_t j = 0; j < ARRAY_SIZE(cmdbuf->state.cs); j++) {
         assert(cs_is_valid(&cmdbuf->state.cs[j].builder));
         if (!cs_is_empty(&cmdbuf->state.cs[j].builder)) {
            used_queue_mask |= BITFIELD_BIT(j);
            qsubmit_count++;
         }
      }
   }

   /* Synchronize all subqueues if we have no command buffer submitted. */
   if (!qsubmit_count)
      used_queue_mask = BITFIELD_MASK(PANVK_SUBQUEUE_COUNT);

   /* We add sync-only queue submits to place our wait/signal operations. */
   if (submit->wait_count > 0)
      qsubmit_count += util_bitcount(used_queue_mask);

   if (submit->signal_count > 0 || force_sync)
      qsubmit_count += util_bitcount(used_queue_mask);

   uint32_t syncop_count = submit->wait_count + util_bitcount(used_queue_mask);

   STACK_ARRAY(struct drm_panthor_queue_submit, qsubmits, qsubmit_count);
   STACK_ARRAY(struct drm_panthor_sync_op, syncops, syncop_count);
   struct drm_panthor_sync_op *wait_ops = syncops;
   struct drm_panthor_sync_op *signal_ops = syncops + submit->wait_count;

   qsubmit_count = 0;
   if (submit->wait_count) {
      for (uint32_t i = 0; i < submit->wait_count; i++) {
         assert(vk_sync_type_is_drm_syncobj(submit->waits[i].sync->type));
         struct vk_drm_syncobj *syncobj =
            vk_sync_as_drm_syncobj(submit->waits[i].sync);

         wait_ops[i] = (struct drm_panthor_sync_op){
            .flags = (submit->waits[i].sync->flags & VK_SYNC_IS_TIMELINE
                         ? DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ
                         : DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ) |
                     DRM_PANTHOR_SYNC_OP_WAIT,
            .handle = syncobj->syncobj,
            .timeline_value = submit->waits[i].wait_value,
         };
      }

      for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
         if (used_queue_mask & BITFIELD_BIT(i)) {
            qsubmits[qsubmit_count++] = (struct drm_panthor_queue_submit){
               .queue_index = i,
               .syncs = DRM_PANTHOR_OBJ_ARRAY(submit->wait_count, wait_ops),
            };
         }
      }
   }

   for (uint32_t i = 0; i < submit->command_buffer_count; i++) {
      struct panvk_cmd_buffer *cmdbuf =
         container_of(submit->command_buffers[i], struct panvk_cmd_buffer, vk);

      for (uint32_t j = 0; j < ARRAY_SIZE(cmdbuf->state.cs); j++) {
         if (cs_is_empty(&cmdbuf->state.cs[j].builder))
            continue;

         qsubmits[qsubmit_count++] = (struct drm_panthor_queue_submit){
            .queue_index = j,
            .stream_size = cs_root_chunk_size(&cmdbuf->state.cs[j].builder),
            .stream_addr = cs_root_chunk_gpu_addr(&cmdbuf->state.cs[j].builder),
            .latest_flush = cmdbuf->flush_id,
         };
      }
   }

   if (submit->signal_count || force_sync) {
      uint32_t signal_op = 0;
      for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
         if (used_queue_mask & BITFIELD_BIT(i)) {
            signal_ops[signal_op] = (struct drm_panthor_sync_op){
               .flags = DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ |
                        DRM_PANTHOR_SYNC_OP_SIGNAL,
               .handle = queue->syncobj_handle,
               .timeline_value = signal_op + 1,
            };

            qsubmits[qsubmit_count++] = (struct drm_panthor_queue_submit){
               .queue_index = i,
               .syncs = DRM_PANTHOR_OBJ_ARRAY(1, &signal_ops[signal_op++]),
            };
         }
      }
   }

   if (force_sync) {
      struct panvk_cs_sync32 *debug_syncs =
         panvk_priv_mem_host_addr(queue->debug_syncobjs);

      assert(debug_syncs);
      memset(debug_syncs, 0, sizeof(*debug_syncs) * PANVK_SUBQUEUE_COUNT);
   }

   struct drm_panthor_group_submit gsubmit = {
      .group_handle = queue->group_handle,
      .queue_submits = DRM_PANTHOR_OBJ_ARRAY(qsubmit_count, qsubmits),
   };

   ret = drmIoctl(dev->vk.drm_fd, DRM_IOCTL_PANTHOR_GROUP_SUBMIT, &gsubmit);
   if (ret) {
      result = vk_queue_set_lost(&queue->vk, "GROUP_SUBMIT: %m");
      goto out;
   }

   if (submit->signal_count || force_sync) {
      if (force_sync) {
         uint64_t point = util_bitcount(used_queue_mask);
         ret = drmSyncobjTimelineWait(dev->vk.drm_fd, &queue->syncobj_handle,
                                      &point, 1, INT64_MAX,
                                      DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL, NULL);
         assert(!ret);
      }

      for (uint32_t i = 0; i < submit->signal_count; i++) {
         assert(vk_sync_type_is_drm_syncobj(submit->signals[i].sync->type));
         struct vk_drm_syncobj *syncobj =
            vk_sync_as_drm_syncobj(submit->signals[i].sync);

         drmSyncobjTransfer(dev->vk.drm_fd, syncobj->syncobj,
                            submit->signals[i].signal_value,
                            queue->syncobj_handle, 0, 0);
      }

      drmSyncobjReset(dev->vk.drm_fd, &queue->syncobj_handle, 1);
   }

   if (debug & PANVK_DEBUG_TRACE) {
      for (uint32_t i = 0; i < qsubmit_count; i++) {
         if (!qsubmits[i].stream_size)
            continue;

         uint32_t subqueue = qsubmits[i].queue_index;
         uint32_t regs[256] = {0};
         uint64_t ctx =
            panvk_priv_mem_dev_addr(queue->subqueues[subqueue].context);

         regs[PANVK_CS_REG_SUBQUEUE_CTX_START] = ctx;
         regs[PANVK_CS_REG_SUBQUEUE_CTX_START + 1] = ctx >> 32;

         simple_mtx_lock(&dev->debug.decode_ctx->lock);
         pandecode_dump_file_open(dev->debug.decode_ctx);
         pandecode_log(dev->debug.decode_ctx, "CS%d\n",
                       qsubmits[i].queue_index);
         simple_mtx_unlock(&dev->debug.decode_ctx->lock);
         pandecode_cs(dev->debug.decode_ctx, qsubmits[i].stream_addr,
                      qsubmits[i].stream_size, phys_dev->kmod.props.gpu_prod_id,
                      regs);
      }
   }

   if (debug & PANVK_DEBUG_DUMP)
      pandecode_dump_mappings(dev->debug.decode_ctx);

   if (force_sync) {
      struct panvk_cs_sync32 *debug_syncs =
         panvk_priv_mem_host_addr(queue->debug_syncobjs);
      uint32_t debug_sync_points[PANVK_SUBQUEUE_COUNT] = {0};

      for (uint32_t i = 0; i < qsubmit_count; i++) {
         if (qsubmits[i].stream_size)
            debug_sync_points[qsubmits[i].queue_index]++;
      }

      for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
         if (debug_syncs[i].seqno != debug_sync_points[i] ||
             debug_syncs[i].error != 0)
            assert(!"Incomplete job or timeout\n");
      }
   }

   if (debug & PANVK_DEBUG_TRACE)
      pandecode_next_frame(dev->debug.decode_ctx);

out:
   STACK_ARRAY_FINISH(syncops);
   STACK_ARRAY_FINISH(qsubmits);
   return result;
}

VkResult
panvk_per_arch(queue_init)(struct panvk_device *dev, struct panvk_queue *queue,
                           int idx, const VkDeviceQueueCreateInfo *create_info)
{
   VkResult result = vk_queue_init(&queue->vk, &dev->vk, create_info, idx);
   if (result != VK_SUCCESS)
      return result;

   int ret = drmSyncobjCreate(dev->vk.drm_fd, 0, &queue->syncobj_handle);
   if (ret) {
      result = panvk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                            "Failed to create our internal sync object");
      goto err_finish_queue;
   }

   result = init_tiler(queue);
   if (result != VK_SUCCESS)
      goto err_destroy_syncobj;

   result = create_group(queue);
   if (result != VK_SUCCESS)
      goto err_cleanup_tiler;

   result = init_queue(queue);
   if (result != VK_SUCCESS)
      goto err_destroy_group;

   queue->vk.driver_submit = panvk_queue_submit;
   return VK_SUCCESS;

err_destroy_group:
   destroy_group(queue);

err_cleanup_tiler:
   cleanup_tiler(queue);

err_destroy_syncobj:
   drmSyncobjDestroy(dev->vk.drm_fd, queue->syncobj_handle);

err_finish_queue:
   vk_queue_finish(&queue->vk);
   return result;
}

void
panvk_per_arch(queue_finish)(struct panvk_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);

   cleanup_queue(queue);
   destroy_group(queue);
   cleanup_tiler(queue);
   drmSyncobjDestroy(dev->vk.drm_fd, queue->syncobj_handle);
   vk_queue_finish(&queue->vk);
}
