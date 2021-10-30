/*
 * Copyright © 2021 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "vk_queue.h"

#include "vk_device.h"

VkResult
vk_queue_init(struct vk_queue *queue, struct vk_device *device,
              const VkDeviceQueueCreateInfo *pCreateInfo,
              uint32_t index_in_family)
{
   memset(queue, 0, sizeof(*queue));
   vk_object_base_init(device, &queue->base, VK_OBJECT_TYPE_QUEUE);

   list_addtail(&queue->link, &device->queues);

   queue->flags = pCreateInfo->flags;
   queue->queue_family_index = pCreateInfo->queueFamilyIndex;

   assert(index_in_family < pCreateInfo->queueCount);
   queue->index_in_family = index_in_family;

   util_dynarray_init(&queue->labels, NULL);
   queue->region_begin = true;

   return VK_SUCCESS;
}

void
vk_queue_finish(struct vk_queue *queue)
{
   util_dynarray_fini(&queue->labels);
   list_del(&queue->link);
   vk_object_base_finish(&queue->base);
}
