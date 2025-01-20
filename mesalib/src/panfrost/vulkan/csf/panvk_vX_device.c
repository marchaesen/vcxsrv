/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_device.h"
#include "panvk_queue.h"

VkResult
panvk_per_arch(device_check_status)(struct vk_device *vk_dev)
{
   struct panvk_device *dev = to_panvk_device(vk_dev);
   VkResult result = VK_SUCCESS;

   for (uint32_t qfi = 0; qfi < PANVK_MAX_QUEUE_FAMILIES; qfi++) {
      for (uint32_t q = 0; q < dev->queue_count[qfi]; q++) {
         struct panvk_queue *queue = &dev->queues[qfi][q];
         if (panvk_per_arch(queue_check_status)(queue) != VK_SUCCESS)
            result = VK_ERROR_DEVICE_LOST;
      }
   }

   if (pan_kmod_vm_query_state(dev->kmod.vm) != PAN_KMOD_VM_USABLE) {
      vk_device_set_lost(&dev->vk, "vm state: not usable");
      result = VK_ERROR_DEVICE_LOST;
   }

   return result;
}
