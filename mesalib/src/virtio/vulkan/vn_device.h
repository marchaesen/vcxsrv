/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_DEVICE_H
#define VN_DEVICE_H

#include "vn_common.h"

#include "vn_buffer.h"
#include "vn_device_memory.h"
#include "vn_feedback.h"

struct vn_device {
   struct vn_device_base base;

   struct vn_instance *instance;
   struct vn_physical_device *physical_device;
   struct vn_renderer *renderer;

   /* unique queue family indices in which to create the device queues */
   uint32_t *queue_families;
   uint32_t queue_family_count;

   struct vn_device_memory_pool memory_pools[VK_MAX_MEMORY_TYPES];

   struct vn_buffer_cache buffer_cache;

   struct vn_feedback_pool feedback_pool;

   /* feedback cmd pool per queue family used by the device
    * - length matches queue_family_count
    * - order matches queue_families
    */
   struct vn_feedback_cmd_pool *cmd_pools;

   struct vn_queue *queues;
   uint32_t queue_count;
};
VK_DEFINE_HANDLE_CASTS(vn_device,
                       base.base.base,
                       VkDevice,
                       VK_OBJECT_TYPE_DEVICE)

#endif /* VN_DEVICE_H */
