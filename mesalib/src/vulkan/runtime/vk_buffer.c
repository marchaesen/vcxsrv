/*
 * Copyright © 2022 Collabora, Ltd.
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

#include "vk_buffer.h"

#include "vk_alloc.h"
#include "vk_device.h"

void
vk_buffer_init(struct vk_device *device,
               struct vk_buffer *buffer,
               const VkBufferCreateInfo *pCreateInfo)
{
   vk_object_base_init(device, &buffer->base, VK_OBJECT_TYPE_BUFFER);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
   assert(pCreateInfo->size > 0);

   buffer->create_flags = pCreateInfo->flags;
   buffer->size = pCreateInfo->size;
   buffer->usage = pCreateInfo->usage;
}

void *
vk_buffer_create(struct vk_device *device,
                 const VkBufferCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *alloc,
                 size_t size)
{
   struct vk_buffer *buffer =
      vk_zalloc2(&device->alloc, alloc, size, 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (buffer == NULL)
      return NULL;

   vk_buffer_init(device, buffer, pCreateInfo);

   return buffer;
}

void
vk_buffer_finish(struct vk_buffer *buffer)
{
   vk_object_base_finish(&buffer->base);
}

void
vk_buffer_destroy(struct vk_device *device,
                  const VkAllocationCallbacks *alloc,
                  struct vk_buffer *buffer)
{
   vk_object_free(device, alloc, buffer);
}
