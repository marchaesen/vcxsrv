/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_QUEUE_H
#define PANVK_QUEUE_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "genxml/gen_macros.h"

#include <stdint.h>

#include "panvk_device.h"

#include "vk_queue.h"

enum panvk_subqueue_id {
   PANVK_SUBQUEUE_VERTEX_TILER = 0,
   PANVK_SUBQUEUE_FRAGMENT,
   PANVK_SUBQUEUE_COMPUTE,
   PANVK_SUBQUEUE_COUNT,
};

struct panvk_tiler_heap {
   uint32_t chunk_size;
   struct panvk_priv_mem desc;
   struct {
      uint32_t handle;
      uint64_t dev_addr;
   } context;
};

struct panvk_subqueue {
   struct panvk_priv_mem context;
   uint32_t *reg_file;

   struct {
      struct pan_kmod_bo *bo;
      size_t size;
      struct {
         uint64_t dev;
         void *host;
      } addr;
   } tracebuf;
};

struct panvk_desc_ringbuf {
   struct panvk_priv_mem syncobj;
   struct pan_kmod_bo *bo;
   size_t size;
   struct {
      uint64_t dev;
      void *host;
   } addr;
};

struct panvk_queue {
   struct vk_queue vk;

   uint32_t group_handle;
   uint32_t syncobj_handle;

   struct panvk_tiler_heap tiler_heap;
   struct panvk_desc_ringbuf render_desc_ringbuf;
   struct panvk_priv_mem syncobjs;
   struct panvk_priv_mem debug_syncobjs;
   struct panvk_priv_mem tiler_oom_regs_save;

   struct {
      struct vk_sync *sync;
      uint64_t next_value;
   } utrace;

   struct panvk_subqueue subqueues[PANVK_SUBQUEUE_COUNT];
};

VK_DEFINE_HANDLE_CASTS(panvk_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

void panvk_per_arch(queue_finish)(struct panvk_queue *queue);

VkResult panvk_per_arch(queue_init)(struct panvk_device *device,
                                    struct panvk_queue *queue, int idx,
                                    const VkDeviceQueueCreateInfo *create_info);

VkResult panvk_per_arch(queue_check_status)(struct panvk_queue *queue);

#endif
