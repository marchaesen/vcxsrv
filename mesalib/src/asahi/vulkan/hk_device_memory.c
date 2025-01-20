/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_device_memory.h"

#include "hk_device.h"
#include "hk_entrypoints.h"
#include "hk_image.h"
#include "hk_physical_device.h"

#include "asahi/lib/agx_bo.h"
#include "util/u_atomic.h"

#include <inttypes.h>
#include <sys/mman.h>

/* Supports opaque fd only */
const VkExternalMemoryProperties hk_opaque_fd_mem_props = {
   .externalMemoryFeatures = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                             VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
   .exportFromImportedHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
   .compatibleHandleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
};

/* Supports opaque fd and dma_buf. */
const VkExternalMemoryProperties hk_dma_buf_mem_props = {
   .externalMemoryFeatures = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                             VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
   .exportFromImportedHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   .compatibleHandleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
                            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
};

static enum agx_bo_flags
hk_memory_type_flags(const VkMemoryType *type,
                     VkExternalMemoryHandleTypeFlagBits handle_types)
{
   unsigned flags = 0;

   if (handle_types)
      flags |= AGX_BO_SHARED | AGX_BO_SHAREABLE;

   return flags;
}

static void
hk_add_ext_bo_locked(struct hk_device *dev, struct agx_bo *bo)
{
   uint32_t id = bo->vbo_res_id;

   unsigned count = util_dynarray_num_elements(&dev->external_bos.list,
                                               struct asahi_ccmd_submit_res);

   for (unsigned i = 0; i < count; i++) {
      struct asahi_ccmd_submit_res *p = util_dynarray_element(
         &dev->external_bos.list, struct asahi_ccmd_submit_res, i);

      if (p->res_id == id) {
         ++*util_dynarray_element(&dev->external_bos.counts, unsigned, i);
         return;
      }
   }

   struct asahi_ccmd_submit_res res = {
      .res_id = id,
      .flags = ASAHI_EXTRES_READ | ASAHI_EXTRES_WRITE,
   };
   util_dynarray_append(&dev->external_bos.list, struct asahi_ccmd_submit_res,
                        res);
   util_dynarray_append(&dev->external_bos.counts, unsigned, 1);
}

static void
hk_add_ext_bo(struct hk_device *dev, struct agx_bo *bo)
{
   if (dev->dev.is_virtio) {
      u_rwlock_wrlock(&dev->external_bos.lock);
      hk_add_ext_bo_locked(dev, bo);
      u_rwlock_wrunlock(&dev->external_bos.lock);
   }
}

static void
hk_remove_ext_bo_locked(struct hk_device *dev, struct agx_bo *bo)
{
   uint32_t id = bo->vbo_res_id;
   unsigned count = util_dynarray_num_elements(&dev->external_bos.list,
                                               struct asahi_ccmd_submit_res);

   for (unsigned i = 0; i < count; i++) {
      struct asahi_ccmd_submit_res *p = util_dynarray_element(
         &dev->external_bos.list, struct asahi_ccmd_submit_res, i);

      if (p->res_id == id) {
         unsigned *ctr =
            util_dynarray_element(&dev->external_bos.counts, unsigned, i);
         if (!--*ctr) {
            *ctr = util_dynarray_pop(&dev->external_bos.counts, unsigned);
            *p = util_dynarray_pop(&dev->external_bos.list,
                                   struct asahi_ccmd_submit_res);
         }
         return;
      }
   }

   unreachable("BO not found");
}

static void
hk_remove_ext_bo(struct hk_device *dev, struct agx_bo *bo)
{
   if (dev->dev.is_virtio) {
      u_rwlock_wrlock(&dev->external_bos.lock);
      hk_remove_ext_bo_locked(dev, bo);
      u_rwlock_wrunlock(&dev->external_bos.lock);
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_GetMemoryFdPropertiesKHR(VkDevice device,
                            VkExternalMemoryHandleTypeFlagBits handleType,
                            int fd,
                            VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   struct hk_physical_device *pdev = hk_device_physical(dev);
   struct agx_bo *bo;

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      bo = agx_bo_import(&dev->dev, fd);
      if (bo == NULL)
         return vk_error(dev, VK_ERROR_INVALID_EXTERNAL_HANDLE);
      break;
   default:
      return vk_error(dev, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }

   uint32_t type_bits = 0;
   for (unsigned t = 0; t < ARRAY_SIZE(pdev->mem_types); t++) {
      const unsigned flags =
         hk_memory_type_flags(&pdev->mem_types[t], handleType);
      if (!(flags & ~bo->flags))
         type_bits |= (1 << t);
   }

   pMemoryFdProperties->memoryTypeBits = type_bits;

   agx_bo_unreference(&dev->dev, bo);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
                  const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMem)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   struct hk_physical_device *pdev = hk_device_physical(dev);
   struct hk_device_memory *mem;
   VkResult result = VK_SUCCESS;

   const VkImportMemoryFdInfoKHR *fd_info =
      vk_find_struct_const(pAllocateInfo->pNext, IMPORT_MEMORY_FD_INFO_KHR);
   const VkExportMemoryAllocateInfo *export_info =
      vk_find_struct_const(pAllocateInfo->pNext, EXPORT_MEMORY_ALLOCATE_INFO);
   const VkMemoryType *type = &pdev->mem_types[pAllocateInfo->memoryTypeIndex];

   VkExternalMemoryHandleTypeFlagBits handle_types = 0;
   if (export_info != NULL)
      handle_types |= export_info->handleTypes;
   if (fd_info != NULL)
      handle_types |= fd_info->handleType;

   const unsigned flags = hk_memory_type_flags(type, handle_types);

   uint32_t alignment = 16384; /* Apple page size */

   struct hk_memory_heap *heap = &pdev->mem_heaps[type->heapIndex];
   if (p_atomic_read(&heap->used) > heap->size)
      return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   const uint64_t aligned_size =
      align64(pAllocateInfo->allocationSize, alignment);

   mem = vk_device_memory_create(&dev->vk, pAllocateInfo, pAllocator,
                                 sizeof(*mem));
   if (!mem)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   mem->map = NULL;
   if (fd_info && fd_info->handleType) {
      assert(
         fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
         fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

      mem->bo = agx_bo_import(&dev->dev, fd_info->fd);
      if (mem->bo == NULL) {
         result = vk_error(dev, VK_ERROR_INVALID_EXTERNAL_HANDLE);
         goto fail_alloc;
      }
      assert(!(flags & ~mem->bo->flags));
   } else {
      enum agx_bo_flags flags = 0;
      if (handle_types)
         flags |= AGX_BO_SHAREABLE;

      mem->bo = agx_bo_create(&dev->dev, aligned_size, 0, flags, "App memory");
      if (!mem->bo) {
         result = vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto fail_alloc;
      }
   }

   if (mem->bo->flags & (AGX_BO_SHAREABLE | AGX_BO_SHARED))
      hk_add_ext_bo(dev, mem->bo);

   if (fd_info && fd_info->handleType) {
      /* From the Vulkan spec:
       *
       *    "Importing memory from a file descriptor transfers ownership of
       *    the file descriptor from the application to the Vulkan
       *    implementation. The application must not perform any operations on
       *    the file descriptor after a successful import."
       *
       * If the import fails, we leave the file descriptor open.
       */
      close(fd_info->fd);
   }

   uint64_t heap_used = p_atomic_add_return(&heap->used, mem->bo->size);
   if (heap_used > heap->size) {
      hk_FreeMemory(device, hk_device_memory_to_handle(mem), pAllocator);
      return vk_errorf(device, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                       "Out of heap memory");
   }

   *pMem = hk_device_memory_to_handle(mem);

   return VK_SUCCESS;

fail_alloc:
   vk_device_memory_destroy(&dev->vk, pAllocator, &mem->vk);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
hk_FreeMemory(VkDevice device, VkDeviceMemory _mem,
              const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   VK_FROM_HANDLE(hk_device_memory, mem, _mem);
   struct hk_physical_device *pdev = hk_device_physical(dev);

   if (!mem)
      return;

   const VkMemoryType *type = &pdev->mem_types[mem->vk.memory_type_index];
   struct hk_memory_heap *heap = &pdev->mem_heaps[type->heapIndex];
   p_atomic_add(&heap->used, -((int64_t)mem->bo->size));

   if (mem->bo->flags & (AGX_BO_SHAREABLE | AGX_BO_SHARED))
      hk_remove_ext_bo(dev, mem->bo);

   agx_bo_unreference(&dev->dev, mem->bo);

   vk_device_memory_destroy(&dev->vk, pAllocator, &mem->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_MapMemory2KHR(VkDevice device, const VkMemoryMapInfoKHR *pMemoryMapInfo,
                 void **ppData)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   VK_FROM_HANDLE(hk_device_memory, mem, pMemoryMapInfo->memory);

   if (mem == NULL) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   const VkDeviceSize offset = pMemoryMapInfo->offset;
   const VkDeviceSize size = vk_device_memory_range(
      &mem->vk, pMemoryMapInfo->offset, pMemoryMapInfo->size);

   UNUSED void *fixed_addr = NULL;
   if (pMemoryMapInfo->flags & VK_MEMORY_MAP_PLACED_BIT_EXT) {
      const VkMemoryMapPlacedInfoEXT *placed_info = vk_find_struct_const(
         pMemoryMapInfo->pNext, MEMORY_MAP_PLACED_INFO_EXT);
      fixed_addr = placed_info->pPlacedAddress;
   }

   /* From the Vulkan spec version 1.0.32 docs for MapMemory:
    *
    *  * If size is not equal to VK_WHOLE_SIZE, size must be greater than 0
    *    assert(size != 0);
    *  * If size is not equal to VK_WHOLE_SIZE, size must be less than or
    *    equal to the size of the memory minus offset
    */
   assert(size > 0);
   assert(offset + size <= mem->bo->size);

   if (size != (size_t)size) {
      return vk_errorf(dev, VK_ERROR_MEMORY_MAP_FAILED,
                       "requested size 0x%" PRIx64 " does not fit in %u bits",
                       size, (unsigned)(sizeof(size_t) * 8));
   }

   /* From the Vulkan 1.2.194 spec:
    *
    *    "memory must not be currently host mapped"
    */
   if (mem->map != NULL) {
      return vk_errorf(dev, VK_ERROR_MEMORY_MAP_FAILED,
                       "Memory object already mapped.");
   }

   mem->map = agx_bo_map(mem->bo);
   *ppData = mem->map + offset;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_UnmapMemory2KHR(VkDevice device,
                   const VkMemoryUnmapInfoKHR *pMemoryUnmapInfo)
{
   VK_FROM_HANDLE(hk_device_memory, mem, pMemoryUnmapInfo->memory);

   if (mem == NULL)
      return VK_SUCCESS;

   if (pMemoryUnmapInfo->flags & VK_MEMORY_UNMAP_RESERVE_BIT_EXT) {
      unreachable("todo");
#if 0
      VK_FROM_HANDLE(hk_device, dev, device);

      int err = agx_bo_overmap(mem->bo, mem->map);
      if (err) {
         return vk_errorf(dev, VK_ERROR_MEMORY_MAP_FAILED,
                          "Failed to map over original mapping");
      }
#endif
   } else {
      /* TODO */
      //// agx_bo_unmap(mem->bo, mem->map);
   }

   mem->map = NULL;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                           const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
hk_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory _mem,
                             VkDeviceSize *pCommittedMemoryInBytes)
{
   VK_FROM_HANDLE(hk_device_memory, mem, _mem);

   *pCommittedMemoryInBytes = mem->bo->size;
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR *pGetFdInfo,
                  int *pFD)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   VK_FROM_HANDLE(hk_device_memory, memory, pGetFdInfo->memory);

   switch (pGetFdInfo->handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      *pFD = agx_bo_export(&dev->dev, memory->bo);
      return VK_SUCCESS;
   default:
      assert(!"unsupported handle type");
      return vk_error(dev, VK_ERROR_FEATURE_NOT_PRESENT);
   }
}

VKAPI_ATTR uint64_t VKAPI_CALL
hk_GetDeviceMemoryOpaqueCaptureAddress(
   UNUSED VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo *pInfo)
{
   VK_FROM_HANDLE(hk_device_memory, mem, pInfo->memory);

   return mem->bo->va->addr;
}
