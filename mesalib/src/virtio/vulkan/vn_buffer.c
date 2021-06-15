/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_buffer.h"

#include "venus-protocol/vn_protocol_driver_buffer.h"
#include "venus-protocol/vn_protocol_driver_buffer_view.h"

#include "vn_device.h"
#include "vn_device_memory.h"

/* buffer commands */

VkResult
vn_CreateBuffer(VkDevice device,
                const VkBufferCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator,
                VkBuffer *pBuffer)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_buffer *buf = vk_zalloc(alloc, sizeof(*buf), VN_DEFAULT_ALIGN,
                                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!buf)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&buf->base, VK_OBJECT_TYPE_BUFFER, &dev->base);

   VkBuffer buf_handle = vn_buffer_to_handle(buf);
   /* TODO async */
   VkResult result = vn_call_vkCreateBuffer(dev->instance, device,
                                            pCreateInfo, NULL, &buf_handle);
   if (result != VK_SUCCESS) {
      vk_free(alloc, buf);
      return vn_error(dev->instance, result);
   }

   /* TODO add a per-device cache for the requirements */
   buf->memory_requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
   buf->memory_requirements.pNext = &buf->dedicated_requirements;
   buf->dedicated_requirements.sType =
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
   buf->dedicated_requirements.pNext = NULL;

   vn_call_vkGetBufferMemoryRequirements2(
      dev->instance, device,
      &(VkBufferMemoryRequirementsInfo2){
         .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
         .buffer = vn_buffer_to_handle(buf),
      },
      &buf->memory_requirements);

   *pBuffer = buf_handle;

   return VK_SUCCESS;
}

void
vn_DestroyBuffer(VkDevice device,
                 VkBuffer buffer,
                 const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_buffer *buf = vn_buffer_from_handle(buffer);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!buf)
      return;

   vn_async_vkDestroyBuffer(dev->instance, device, buffer, NULL);

   vn_object_base_fini(&buf->base);
   vk_free(alloc, buf);
}

VkDeviceAddress
vn_GetBufferDeviceAddress(VkDevice device,
                          const VkBufferDeviceAddressInfo *pInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);

   return vn_call_vkGetBufferDeviceAddress(dev->instance, device, pInfo);
}

uint64_t
vn_GetBufferOpaqueCaptureAddress(VkDevice device,
                                 const VkBufferDeviceAddressInfo *pInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);

   return vn_call_vkGetBufferOpaqueCaptureAddress(dev->instance, device,
                                                  pInfo);
}

void
vn_GetBufferMemoryRequirements(VkDevice device,
                               VkBuffer buffer,
                               VkMemoryRequirements *pMemoryRequirements)
{
   const struct vn_buffer *buf = vn_buffer_from_handle(buffer);

   *pMemoryRequirements = buf->memory_requirements.memoryRequirements;
}

void
vn_GetBufferMemoryRequirements2(VkDevice device,
                                const VkBufferMemoryRequirementsInfo2 *pInfo,
                                VkMemoryRequirements2 *pMemoryRequirements)
{
   const struct vn_buffer *buf = vn_buffer_from_handle(pInfo->buffer);
   union {
      VkBaseOutStructure *pnext;
      VkMemoryRequirements2 *two;
      VkMemoryDedicatedRequirements *dedicated;
   } u = { .two = pMemoryRequirements };

   while (u.pnext) {
      switch (u.pnext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2:
         u.two->memoryRequirements =
            buf->memory_requirements.memoryRequirements;
         break;
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS:
         u.dedicated->prefersDedicatedAllocation =
            buf->dedicated_requirements.prefersDedicatedAllocation;
         u.dedicated->requiresDedicatedAllocation =
            buf->dedicated_requirements.requiresDedicatedAllocation;
         break;
      default:
         break;
      }
      u.pnext = u.pnext->pNext;
   }
}

VkResult
vn_BindBufferMemory(VkDevice device,
                    VkBuffer buffer,
                    VkDeviceMemory memory,
                    VkDeviceSize memoryOffset)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_device_memory *mem = vn_device_memory_from_handle(memory);

   if (mem->base_memory) {
      memory = vn_device_memory_to_handle(mem->base_memory);
      memoryOffset += mem->base_offset;
   }

   vn_async_vkBindBufferMemory(dev->instance, device, buffer, memory,
                               memoryOffset);

   return VK_SUCCESS;
}

VkResult
vn_BindBufferMemory2(VkDevice device,
                     uint32_t bindInfoCount,
                     const VkBindBufferMemoryInfo *pBindInfos)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;

   VkBindBufferMemoryInfo *local_infos = NULL;
   for (uint32_t i = 0; i < bindInfoCount; i++) {
      const VkBindBufferMemoryInfo *info = &pBindInfos[i];
      struct vn_device_memory *mem =
         vn_device_memory_from_handle(info->memory);
      if (!mem->base_memory)
         continue;

      if (!local_infos) {
         const size_t size = sizeof(*local_infos) * bindInfoCount;
         local_infos = vk_alloc(alloc, size, VN_DEFAULT_ALIGN,
                                VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
         if (!local_infos)
            return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

         memcpy(local_infos, pBindInfos, size);
      }

      local_infos[i].memory = vn_device_memory_to_handle(mem->base_memory);
      local_infos[i].memoryOffset += mem->base_offset;
   }
   if (local_infos)
      pBindInfos = local_infos;

   vn_async_vkBindBufferMemory2(dev->instance, device, bindInfoCount,
                                pBindInfos);

   vk_free(alloc, local_infos);

   return VK_SUCCESS;
}

/* buffer view commands */

VkResult
vn_CreateBufferView(VkDevice device,
                    const VkBufferViewCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkBufferView *pView)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_buffer_view *view =
      vk_zalloc(alloc, sizeof(*view), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!view)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&view->base, VK_OBJECT_TYPE_BUFFER_VIEW, &dev->base);

   VkBufferView view_handle = vn_buffer_view_to_handle(view);
   vn_async_vkCreateBufferView(dev->instance, device, pCreateInfo, NULL,
                               &view_handle);

   *pView = view_handle;

   return VK_SUCCESS;
}

void
vn_DestroyBufferView(VkDevice device,
                     VkBufferView bufferView,
                     const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_buffer_view *view = vn_buffer_view_from_handle(bufferView);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!view)
      return;

   vn_async_vkDestroyBufferView(dev->instance, device, bufferView, NULL);

   vn_object_base_fini(&view->base);
   vk_free(alloc, view);
}
