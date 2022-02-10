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

#include "vn_android.h"
#include "vn_device.h"
#include "vn_device_memory.h"

/* buffer commands */

/* mandatory buffer create infos to cache */
static const VkBufferCreateInfo cache_infos[] = {
   {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = 1,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   },
   {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = 1,
      .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   },
   {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = 1,
      .usage =
         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   },
   {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = 1,
      .usage =
         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   },
};

static inline bool
vn_buffer_create_info_can_be_cached(const VkBufferCreateInfo *create_info)
{
   /* cache only VK_SHARING_MODE_EXCLUSIVE and without pNext for simplicity */
   return (create_info->pNext == NULL) &&
          (create_info->sharingMode == VK_SHARING_MODE_EXCLUSIVE);
}

static VkResult
vn_buffer_cache_entries_create(struct vn_device *dev,
                               struct vn_buffer_cache_entry **out_entries,
                               uint32_t *out_entry_count)
{
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;
   VkDevice dev_handle = vn_device_to_handle(dev);
   struct vn_buffer_cache_entry *entries;
   const uint32_t entry_count = ARRAY_SIZE(cache_infos);
   VkResult result;

   entries = vk_zalloc(alloc, sizeof(*entries) * entry_count,
                       VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!entries)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   for (uint32_t i = 0; i < entry_count; i++) {
      VkBuffer buf_handle = VK_NULL_HANDLE;
      struct vn_buffer *buf = NULL;

      assert(vn_buffer_create_info_can_be_cached(&cache_infos[i]));

      result =
         vn_CreateBuffer(dev_handle, &cache_infos[i], alloc, &buf_handle);
      if (result != VK_SUCCESS) {
         vk_free(alloc, entries);
         return result;
      }

      buf = vn_buffer_from_handle(buf_handle);

      /* TODO remove below after VK_KHR_maintenance4 is available */
      if (buf->requirements.memory.memoryRequirements.alignment <
          buf->requirements.memory.memoryRequirements.size) {
         vk_free(alloc, entries);
         *out_entries = entries;
         *out_entry_count = entry_count;
         return VK_SUCCESS;
      }

      entries[i].create_info = &cache_infos[i];
      entries[i].requirements.memory = buf->requirements.memory;
      entries[i].requirements.dedicated = buf->requirements.dedicated;

      vn_DestroyBuffer(dev_handle, buf_handle, alloc);
   }

   *out_entries = entries;
   *out_entry_count = entry_count;
   return VK_SUCCESS;
}

static void
vn_buffer_cache_entries_destroy(struct vn_device *dev,
                                struct vn_buffer_cache_entry *entries)
{
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;

   if (entries)
      vk_free(alloc, entries);
}

static VkResult
vn_buffer_get_max_buffer_size(struct vn_device *dev,
                              uint64_t *out_max_buffer_size)
{
   /* TODO use VK_KHR_maintenance4 when available */
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;
   VkDevice dev_handle = vn_device_to_handle(dev);
   VkBuffer buf_handle;
   VkBufferCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   uint64_t max_buffer_size = 0;
   uint8_t begin = 0;
   uint8_t end = 64;

   while (begin < end) {
      uint8_t mid = (begin + end) >> 1;
      create_info.size = 1ull << mid;
      if (vn_CreateBuffer(dev_handle, &create_info, alloc, &buf_handle) ==
          VK_SUCCESS) {
         vn_DestroyBuffer(dev_handle, buf_handle, alloc);
         max_buffer_size = create_info.size;
         begin = mid + 1;
      } else {
         end = mid;
      }
   }

   *out_max_buffer_size = max_buffer_size;
   return VK_SUCCESS;
}

VkResult
vn_buffer_cache_init(struct vn_device *dev)
{
   uint32_t ahb_mem_type_bits = 0;
   uint64_t max_buffer_size = 0;
   struct vn_buffer_cache_entry *entries = NULL;
   uint32_t entry_count = 0;
   VkResult result;

   if (dev->base.base.enabled_extensions
          .ANDROID_external_memory_android_hardware_buffer) {
      result =
         vn_android_get_ahb_buffer_memory_type_bits(dev, &ahb_mem_type_bits);
      if (result != VK_SUCCESS)
         return result;
   }

   result = vn_buffer_get_max_buffer_size(dev, &max_buffer_size);
   if (result != VK_SUCCESS)
      return result;

   result = vn_buffer_cache_entries_create(dev, &entries, &entry_count);
   if (result != VK_SUCCESS)
      return result;

   dev->buffer_cache.ahb_mem_type_bits = ahb_mem_type_bits;
   dev->buffer_cache.max_buffer_size = max_buffer_size;
   dev->buffer_cache.entries = entries;
   dev->buffer_cache.entry_count = entry_count;
   return VK_SUCCESS;
}

void
vn_buffer_cache_fini(struct vn_device *dev)
{
   vn_buffer_cache_entries_destroy(dev, dev->buffer_cache.entries);
}

static bool
vn_buffer_cache_get_memory_requirements(
   struct vn_buffer_cache *cache,
   const VkBufferCreateInfo *create_info,
   struct vn_buffer_memory_requirements *out)
{
   if (create_info->size > cache->max_buffer_size)
      return false;

   if (!vn_buffer_create_info_can_be_cached(create_info))
      return false;

   /* 12.7. Resource Memory Association
    *
    * The memoryTypeBits member is identical for all VkBuffer objects created
    * with the same value for the flags and usage members in the
    * VkBufferCreateInfo structure and the handleTypes member of the
    * VkExternalMemoryBufferCreateInfo structure passed to vkCreateBuffer.
    * Further, if usage1 and usage2 of type VkBufferUsageFlags are such that
    * the bits set in usage2 are a subset of the bits set in usage1, and they
    * have the same flags and VkExternalMemoryBufferCreateInfo::handleTypes,
    * then the bits set in memoryTypeBits returned for usage1 must be a subset
    * of the bits set in memoryTypeBits returned for usage2, for all values of
    * flags.
    */
   for (uint32_t i = 0; i < cache->entry_count; i++) {
      const struct vn_buffer_cache_entry *entry = &cache->entries[i];
      if ((entry->create_info->flags == create_info->flags) &&
          ((entry->create_info->usage & create_info->usage) ==
           create_info->usage)) {
         *out = entry->requirements;

         /* TODO remove the comment after VK_KHR_maintenance4 is available
          *
          * This is based on below implementation defined behavior:
          *
          *    req.size <= align64(info.size, req.alignment)
          */
         out->memory.memoryRequirements.size = align64(
            create_info->size, out->memory.memoryRequirements.alignment);
         return true;
      }
   }

   return false;
}

static VkResult
vn_buffer_init(struct vn_device *dev,
               const VkBufferCreateInfo *create_info,
               struct vn_buffer *buf)
{
   VkDevice dev_handle = vn_device_to_handle(dev);
   VkBuffer buf_handle = vn_buffer_to_handle(buf);
   VkResult result;

   if (vn_buffer_cache_get_memory_requirements(
          &dev->buffer_cache, create_info, &buf->requirements)) {
      vn_async_vkCreateBuffer(dev->instance, dev_handle, create_info, NULL,
                              &buf_handle);
      return VK_SUCCESS;
   }

   result = vn_call_vkCreateBuffer(dev->instance, dev_handle, create_info,
                                   NULL, &buf_handle);
   if (result != VK_SUCCESS)
      return result;

   buf->requirements.memory.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
   buf->requirements.memory.pNext = &buf->requirements.dedicated;
   buf->requirements.dedicated.sType =
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
   buf->requirements.dedicated.pNext = NULL;

   vn_call_vkGetBufferMemoryRequirements2(
      dev->instance, dev_handle,
      &(VkBufferMemoryRequirementsInfo2){
         .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
         .buffer = buf_handle,
      },
      &buf->requirements.memory);

   return VK_SUCCESS;
}

VkResult
vn_buffer_create(struct vn_device *dev,
                 const VkBufferCreateInfo *create_info,
                 const VkAllocationCallbacks *alloc,
                 struct vn_buffer **out_buf)
{
   struct vn_buffer *buf = NULL;
   VkResult result;

   buf = vk_zalloc(alloc, sizeof(*buf), VN_DEFAULT_ALIGN,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!buf)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   vn_object_base_init(&buf->base, VK_OBJECT_TYPE_BUFFER, &dev->base);

   result = vn_buffer_init(dev, create_info, buf);
   if (result != VK_SUCCESS) {
      vn_object_base_fini(&buf->base);
      vk_free(alloc, buf);
      return result;
   }

   *out_buf = buf;

   return VK_SUCCESS;
}

VkResult
vn_CreateBuffer(VkDevice device,
                const VkBufferCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator,
                VkBuffer *pBuffer)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;
   struct vn_buffer *buf = NULL;
   VkResult result;

   const VkExternalMemoryBufferCreateInfo *external_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           EXTERNAL_MEMORY_BUFFER_CREATE_INFO);
   const bool ahb_info =
      external_info &&
      external_info->handleTypes ==
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

   if (ahb_info)
      result = vn_android_buffer_from_ahb(dev, pCreateInfo, alloc, &buf);
   else
      result = vn_buffer_create(dev, pCreateInfo, alloc, &buf);

   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   *pBuffer = vn_buffer_to_handle(buf);

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

   *pMemoryRequirements = buf->requirements.memory.memoryRequirements;
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
            buf->requirements.memory.memoryRequirements;
         break;
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS:
         u.dedicated->prefersDedicatedAllocation =
            buf->requirements.dedicated.prefersDedicatedAllocation;
         u.dedicated->requiresDedicatedAllocation =
            buf->requirements.dedicated.requiresDedicatedAllocation;
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
