/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "hk_private.h"
#include "vk_queue.h"

struct hk_device;

struct hk_queue {
   struct vk_queue vk;

   struct {
      /* Asahi kernel queue ID */
      uint32_t id;

      /* Timeline syncobj backing the queue */
      uint32_t syncobj;

      /* Current maximum timeline value for the queue's syncobj. If the
       * syncobj's value equals timeline_value, then all work is complete.
       */
      uint32_t timeline_value;
   } drm;
};

static inline struct hk_device *
hk_queue_device(struct hk_queue *queue)
{
   return (struct hk_device *)queue->vk.base.device;
}

VkResult hk_queue_init(struct hk_device *dev, struct hk_queue *queue,
                       const VkDeviceQueueCreateInfo *pCreateInfo,
                       uint32_t index_in_family);

void hk_queue_finish(struct hk_device *dev, struct hk_queue *queue);
