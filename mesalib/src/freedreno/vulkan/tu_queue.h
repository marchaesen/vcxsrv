/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_QUEUE_H
#define TU_QUEUE_H

#include "tu_common.h"

struct tu_queue
{
   struct vk_queue vk;

   struct tu_device *device;

   uint32_t msm_queue_id;
   uint32_t priority;

   int fence;           /* timestamp/fence of the last queue submission */
};
VK_DEFINE_HANDLE_CASTS(tu_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

VkResult
tu_queue_init(struct tu_device *device,
              struct tu_queue *queue,
              int idx,
              const VkDeviceQueueCreateInfo *create_info);

void
tu_queue_finish(struct tu_queue *queue);

#endif
