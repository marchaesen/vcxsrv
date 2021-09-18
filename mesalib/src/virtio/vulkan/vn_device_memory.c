/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_device_memory.h"

#include "venus-protocol/vn_protocol_driver_device_memory.h"
#include "venus-protocol/vn_protocol_driver_transport.h"

#include "vn_android.h"
#include "vn_buffer.h"
#include "vn_device.h"
#include "vn_image.h"
#include "vn_physical_device.h"

/* device memory commands */

static VkResult
vn_device_memory_simple_alloc(struct vn_device *dev,
                              uint32_t mem_type_index,
                              VkDeviceSize size,
                              struct vn_device_memory **out_mem)
{
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;

   struct vn_device_memory *mem =
      vk_zalloc(alloc, sizeof(*mem), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!mem)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   vn_object_base_init(&mem->base, VK_OBJECT_TYPE_DEVICE_MEMORY, &dev->base);
   mem->size = size;

   VkDeviceMemory mem_handle = vn_device_memory_to_handle(mem);
   VkResult result = vn_call_vkAllocateMemory(
      dev->instance, vn_device_to_handle(dev),
      &(const VkMemoryAllocateInfo){
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = size,
         .memoryTypeIndex = mem_type_index,
      },
      NULL, &mem_handle);
   if (result != VK_SUCCESS) {
      vn_object_base_fini(&mem->base);
      vk_free(alloc, mem);
      return result;
   }

   const VkPhysicalDeviceMemoryProperties *mem_props =
      &dev->physical_device->memory_properties.memoryProperties;
   const VkMemoryType *mem_type = &mem_props->memoryTypes[mem_type_index];
   result = vn_renderer_bo_create_from_device_memory(
      dev->renderer, mem->size, mem->base.id, mem_type->propertyFlags, 0,
      &mem->base_bo);
   if (result != VK_SUCCESS) {
      vn_async_vkFreeMemory(dev->instance, vn_device_to_handle(dev),
                            mem_handle, NULL);
      vn_object_base_fini(&mem->base);
      vk_free(alloc, mem);
      return result;
   }
   vn_instance_roundtrip(dev->instance);

   *out_mem = mem;

   return VK_SUCCESS;
}

static void
vn_device_memory_simple_free(struct vn_device *dev,
                             struct vn_device_memory *mem)
{
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;

   if (mem->base_bo)
      vn_renderer_bo_unref(dev->renderer, mem->base_bo);

   vn_async_vkFreeMemory(dev->instance, vn_device_to_handle(dev),
                         vn_device_memory_to_handle(mem), NULL);
   vn_object_base_fini(&mem->base);
   vk_free(alloc, mem);
}

void
vn_device_memory_pool_fini(struct vn_device *dev, uint32_t mem_type_index)
{
   struct vn_device_memory_pool *pool = &dev->memory_pools[mem_type_index];
   if (pool->memory)
      vn_device_memory_simple_free(dev, pool->memory);
   mtx_destroy(&pool->mutex);
}

static VkResult
vn_device_memory_pool_grow_locked(struct vn_device *dev,
                                  uint32_t mem_type_index,
                                  VkDeviceSize size)
{
   struct vn_device_memory *mem;
   VkResult result =
      vn_device_memory_simple_alloc(dev, mem_type_index, size, &mem);
   if (result != VK_SUCCESS)
      return result;

   struct vn_device_memory_pool *pool = &dev->memory_pools[mem_type_index];
   if (pool->memory) {
      const bool bo_destroyed =
         vn_renderer_bo_unref(dev->renderer, pool->memory->base_bo);
      pool->memory->base_bo = NULL;

      /* we use pool->memory's base_bo to keep it alive */
      if (bo_destroyed)
         vn_device_memory_simple_free(dev, pool->memory);
   }

   pool->memory = mem;
   pool->used = 0;

   return VK_SUCCESS;
}

static VkResult
vn_device_memory_pool_alloc(struct vn_device *dev,
                            uint32_t mem_type_index,
                            VkDeviceSize size,
                            struct vn_device_memory **base_mem,
                            struct vn_renderer_bo **base_bo,
                            VkDeviceSize *base_offset)
{
   const VkDeviceSize pool_size = 16 * 1024 * 1024;
   /* XXX We don't know the alignment requirement.  We should probably use 64K
    * because some GPUs have 64K pages.
    */
   const VkDeviceSize pool_align = 4096;
   struct vn_device_memory_pool *pool = &dev->memory_pools[mem_type_index];

   assert(size <= pool_size);

   mtx_lock(&pool->mutex);

   if (!pool->memory || pool->used + size > pool_size) {
      VkResult result =
         vn_device_memory_pool_grow_locked(dev, mem_type_index, pool_size);
      if (result != VK_SUCCESS) {
         mtx_unlock(&pool->mutex);
         return result;
      }
   }

   /* we use base_bo to keep base_mem alive */
   *base_mem = pool->memory;
   *base_bo = vn_renderer_bo_ref(dev->renderer, pool->memory->base_bo);

   *base_offset = pool->used;
   pool->used += align64(size, pool_align);

   mtx_unlock(&pool->mutex);

   return VK_SUCCESS;
}

static void
vn_device_memory_pool_free(struct vn_device *dev,
                           struct vn_device_memory *base_mem,
                           struct vn_renderer_bo *base_bo)
{
   /* we use base_bo to keep base_mem alive */
   if (vn_renderer_bo_unref(dev->renderer, base_bo))
      vn_device_memory_simple_free(dev, base_mem);
}

static bool
vn_device_memory_should_suballocate(const VkMemoryAllocateInfo *alloc_info,
                                    const VkMemoryType *mem_type)
{
   /* We should not support suballocations because apps can do better.  But
    * each BO takes up a KVM memslot currently and some CTS tests exhausts
    * them.  This might not be needed on newer (host) kernels where there are
    * many more KVM memslots.
    */

   /* consider host-visible memory only */
   if (!(mem_type->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
      return false;

   /* reject larger allocations */
   if (alloc_info->allocationSize > 64 * 1024)
      return false;

   /* reject if there is any pnext struct other than
    * VkMemoryDedicatedAllocateInfo, or if dedicated allocation is required
    */
   if (alloc_info->pNext) {
      const VkMemoryDedicatedAllocateInfo *dedicated = alloc_info->pNext;
      if (dedicated->sType !=
             VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO ||
          dedicated->pNext)
         return false;

      const struct vn_image *img = vn_image_from_handle(dedicated->image);
      if (img) {
         for (uint32_t i = 0; i < ARRAY_SIZE(img->dedicated_requirements);
              i++) {
            if (img->dedicated_requirements[i].requiresDedicatedAllocation)
               return false;
         }
      }

      const struct vn_buffer *buf = vn_buffer_from_handle(dedicated->buffer);
      if (buf && buf->dedicated_requirements.requiresDedicatedAllocation)
         return false;
   }

   return true;
}

VkResult
vn_device_memory_import_dma_buf(struct vn_device *dev,
                                struct vn_device_memory *mem,
                                const VkMemoryAllocateInfo *alloc_info,
                                bool force_unmappable,
                                int fd)
{
   VkDevice device = vn_device_to_handle(dev);
   VkDeviceMemory memory = vn_device_memory_to_handle(mem);
   const VkPhysicalDeviceMemoryProperties *mem_props =
      &dev->physical_device->memory_properties.memoryProperties;
   VkMemoryPropertyFlags mem_flags =
      mem_props->memoryTypes[alloc_info->memoryTypeIndex].propertyFlags;
   struct vn_renderer_bo *bo;
   VkResult result = VK_SUCCESS;

   if (force_unmappable)
      mem_flags &= ~VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

   result = vn_renderer_bo_create_from_dma_buf(
      dev->renderer, alloc_info->allocationSize, fd, mem_flags, &bo);
   if (result != VK_SUCCESS)
      return result;

   vn_instance_roundtrip(dev->instance);

   /* XXX fix VkImportMemoryResourceInfoMESA to support memory planes */
   const VkImportMemoryResourceInfoMESA import_memory_resource_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_RESOURCE_INFO_MESA,
      .pNext = alloc_info->pNext,
      .resourceId = bo->res_id,
   };
   const VkMemoryAllocateInfo memory_allocate_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &import_memory_resource_info,
      .allocationSize = alloc_info->allocationSize,
      .memoryTypeIndex = alloc_info->memoryTypeIndex,
   };
   result = vn_call_vkAllocateMemory(dev->instance, device,
                                     &memory_allocate_info, NULL, &memory);
   if (result != VK_SUCCESS) {
      vn_renderer_bo_unref(dev->renderer, bo);
      return result;
   }

   /* need to close import fd on success to avoid fd leak */
   close(fd);
   mem->base_bo = bo;

   return VK_SUCCESS;
}

static VkResult
vn_device_memory_alloc(struct vn_device *dev,
                       struct vn_device_memory *mem,
                       const VkMemoryAllocateInfo *alloc_info,
                       bool need_bo,
                       VkMemoryPropertyFlags flags,
                       VkExternalMemoryHandleTypeFlags external_handles)
{
   VkDevice dev_handle = vn_device_to_handle(dev);
   VkDeviceMemory mem_handle = vn_device_memory_to_handle(mem);
   VkResult result = vn_call_vkAllocateMemory(dev->instance, dev_handle,
                                              alloc_info, NULL, &mem_handle);
   if (result != VK_SUCCESS || !need_bo)
      return result;

   result = vn_renderer_bo_create_from_device_memory(
      dev->renderer, mem->size, mem->base.id, flags, external_handles,
      &mem->base_bo);
   if (result != VK_SUCCESS) {
      vn_async_vkFreeMemory(dev->instance, dev_handle, mem_handle, NULL);
      return result;
   }

   vn_instance_roundtrip(dev->instance);

   return VK_SUCCESS;
}

VkResult
vn_AllocateMemory(VkDevice device,
                  const VkMemoryAllocateInfo *pAllocateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkDeviceMemory *pMemory)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   const VkPhysicalDeviceMemoryProperties *mem_props =
      &dev->physical_device->memory_properties.memoryProperties;
   const VkMemoryType *mem_type =
      &mem_props->memoryTypes[pAllocateInfo->memoryTypeIndex];

   const VkExportMemoryAllocateInfo *export_info = NULL;
   const VkImportAndroidHardwareBufferInfoANDROID *import_ahb_info = NULL;
   const VkImportMemoryFdInfoKHR *import_fd_info = NULL;
   bool export_ahb = false;

   vk_foreach_struct_const(pnext, pAllocateInfo->pNext) {
      switch (pnext->sType) {
      case VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO:
         export_info = (void *)pnext;
         if (export_info->handleTypes &
             VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID)
            export_ahb = true;
         else if (!export_info->handleTypes)
            export_info = NULL;
         break;
      case VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID:
         import_ahb_info = (void *)pnext;
         break;
      case VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR:
         import_fd_info = (void *)pnext;
         break;
      default:
         break;
      }
   }

   struct vn_device_memory *mem =
      vk_zalloc(alloc, sizeof(*mem), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!mem)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&mem->base, VK_OBJECT_TYPE_DEVICE_MEMORY, &dev->base);
   mem->size = pAllocateInfo->allocationSize;

   VkDeviceMemory mem_handle = vn_device_memory_to_handle(mem);
   VkResult result;
   if (import_ahb_info) {
      result = vn_android_device_import_ahb(dev, mem, pAllocateInfo, alloc,
                                            import_ahb_info->buffer);
   } else if (export_ahb) {
      result = vn_android_device_allocate_ahb(dev, mem, pAllocateInfo, alloc);
   } else if (import_fd_info) {
      result = vn_device_memory_import_dma_buf(dev, mem, pAllocateInfo, false,
                                               import_fd_info->fd);
   } else if (export_info) {
      result = vn_device_memory_alloc(dev, mem, pAllocateInfo, true,
                                      mem_type->propertyFlags,
                                      export_info->handleTypes);
   } else if (vn_device_memory_should_suballocate(pAllocateInfo, mem_type)) {
      result = vn_device_memory_pool_alloc(
         dev, pAllocateInfo->memoryTypeIndex, mem->size, &mem->base_memory,
         &mem->base_bo, &mem->base_offset);
   } else {
      const bool need_bo =
         mem_type->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
      result = vn_device_memory_alloc(dev, mem, pAllocateInfo, need_bo,
                                      mem_type->propertyFlags, 0);
   }
   if (result != VK_SUCCESS) {
      vn_object_base_fini(&mem->base);
      vk_free(alloc, mem);
      return vn_error(dev->instance, result);
   }

   *pMemory = mem_handle;

   return VK_SUCCESS;
}

void
vn_FreeMemory(VkDevice device,
              VkDeviceMemory memory,
              const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_device_memory *mem = vn_device_memory_from_handle(memory);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!mem)
      return;

   if (mem->base_memory) {
      vn_device_memory_pool_free(dev, mem->base_memory, mem->base_bo);
   } else {
      if (mem->base_bo)
         vn_renderer_bo_unref(dev->renderer, mem->base_bo);
      vn_async_vkFreeMemory(dev->instance, device, memory, NULL);
   }

   if (mem->ahb)
      vn_android_release_ahb(mem->ahb);

   vn_object_base_fini(&mem->base);
   vk_free(alloc, mem);
}

uint64_t
vn_GetDeviceMemoryOpaqueCaptureAddress(
   VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo *pInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);
   ASSERTED struct vn_device_memory *mem =
      vn_device_memory_from_handle(pInfo->memory);

   assert(!mem->base_memory);
   return vn_call_vkGetDeviceMemoryOpaqueCaptureAddress(dev->instance, device,
                                                        pInfo);
}

VkResult
vn_MapMemory(VkDevice device,
             VkDeviceMemory memory,
             VkDeviceSize offset,
             VkDeviceSize size,
             VkMemoryMapFlags flags,
             void **ppData)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_device_memory *mem = vn_device_memory_from_handle(memory);

   void *ptr = vn_renderer_bo_map(dev->renderer, mem->base_bo);
   if (!ptr)
      return vn_error(dev->instance, VK_ERROR_MEMORY_MAP_FAILED);

   mem->map_end = size == VK_WHOLE_SIZE ? mem->size : offset + size;

   *ppData = ptr + mem->base_offset + offset;

   return VK_SUCCESS;
}

void
vn_UnmapMemory(VkDevice device, VkDeviceMemory memory)
{
}

VkResult
vn_FlushMappedMemoryRanges(VkDevice device,
                           uint32_t memoryRangeCount,
                           const VkMappedMemoryRange *pMemoryRanges)
{
   struct vn_device *dev = vn_device_from_handle(device);

   for (uint32_t i = 0; i < memoryRangeCount; i++) {
      const VkMappedMemoryRange *range = &pMemoryRanges[i];
      struct vn_device_memory *mem =
         vn_device_memory_from_handle(range->memory);

      const VkDeviceSize size = range->size == VK_WHOLE_SIZE
                                   ? mem->map_end - range->offset
                                   : range->size;
      vn_renderer_bo_flush(dev->renderer, mem->base_bo,
                           mem->base_offset + range->offset, size);
   }

   return VK_SUCCESS;
}

VkResult
vn_InvalidateMappedMemoryRanges(VkDevice device,
                                uint32_t memoryRangeCount,
                                const VkMappedMemoryRange *pMemoryRanges)
{
   struct vn_device *dev = vn_device_from_handle(device);

   for (uint32_t i = 0; i < memoryRangeCount; i++) {
      const VkMappedMemoryRange *range = &pMemoryRanges[i];
      struct vn_device_memory *mem =
         vn_device_memory_from_handle(range->memory);

      const VkDeviceSize size = range->size == VK_WHOLE_SIZE
                                   ? mem->map_end - range->offset
                                   : range->size;
      vn_renderer_bo_invalidate(dev->renderer, mem->base_bo,
                                mem->base_offset + range->offset, size);
   }

   return VK_SUCCESS;
}

void
vn_GetDeviceMemoryCommitment(VkDevice device,
                             VkDeviceMemory memory,
                             VkDeviceSize *pCommittedMemoryInBytes)
{
   struct vn_device *dev = vn_device_from_handle(device);
   ASSERTED struct vn_device_memory *mem =
      vn_device_memory_from_handle(memory);

   assert(!mem->base_memory);
   vn_call_vkGetDeviceMemoryCommitment(dev->instance, device, memory,
                                       pCommittedMemoryInBytes);
}

VkResult
vn_GetMemoryFdKHR(VkDevice device,
                  const VkMemoryGetFdInfoKHR *pGetFdInfo,
                  int *pFd)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_device_memory *mem =
      vn_device_memory_from_handle(pGetFdInfo->memory);

   /* At the moment, we support only the below handle types. */
   assert(pGetFdInfo->handleType &
          (VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
           VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT));
   assert(!mem->base_memory && mem->base_bo);
   *pFd = vn_renderer_bo_export_dma_buf(dev->renderer, mem->base_bo);
   if (*pFd < 0)
      return vn_error(dev->instance, VK_ERROR_TOO_MANY_OBJECTS);

   return VK_SUCCESS;
}

VkResult
vn_get_memory_dma_buf_properties(struct vn_device *dev,
                                 int fd,
                                 uint64_t *out_alloc_size,
                                 uint32_t *out_mem_type_bits)
{
   VkDevice device = vn_device_to_handle(dev);
   struct vn_renderer_bo *bo = NULL;
   VkResult result = VK_SUCCESS;

   result = vn_renderer_bo_create_from_dma_buf(dev->renderer, 0 /* size */,
                                               fd, 0 /* flags */, &bo);
   if (result != VK_SUCCESS)
      return result;

   vn_instance_roundtrip(dev->instance);

   VkMemoryResourceAllocationSizeProperties100000MESA alloc_size_props = {
      .sType =
         VK_STRUCTURE_TYPE_MEMORY_RESOURCE_ALLOCATION_SIZE_PROPERTIES_100000_MESA,
      .pNext = NULL,
      .allocationSize = 0,
   };
   VkMemoryResourcePropertiesMESA props = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_RESOURCE_PROPERTIES_MESA,
      .pNext =
         dev->instance->experimental.memoryResourceAllocationSize == VK_TRUE
            ? &alloc_size_props
            : NULL,
      .memoryTypeBits = 0,
   };
   result = vn_call_vkGetMemoryResourcePropertiesMESA(dev->instance, device,
                                                      bo->res_id, &props);
   vn_renderer_bo_unref(dev->renderer, bo);
   if (result != VK_SUCCESS)
      return result;

   *out_alloc_size = alloc_size_props.allocationSize;
   *out_mem_type_bits = props.memoryTypeBits;

   return VK_SUCCESS;
}

VkResult
vn_GetMemoryFdPropertiesKHR(VkDevice device,
                            VkExternalMemoryHandleTypeFlagBits handleType,
                            int fd,
                            VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   struct vn_device *dev = vn_device_from_handle(device);
   uint64_t alloc_size = 0;
   uint32_t mem_type_bits = 0;
   VkResult result = VK_SUCCESS;

   if (handleType != VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT)
      return vn_error(dev->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   result =
      vn_get_memory_dma_buf_properties(dev, fd, &alloc_size, &mem_type_bits);
   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   pMemoryFdProperties->memoryTypeBits = mem_type_bits;

   return VK_SUCCESS;
}
