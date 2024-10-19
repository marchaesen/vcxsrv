/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "genxml/decode.h"

#include "vulkan/util/vk_util.h"

#include "panvk_device.h"
#include "panvk_device_memory.h"
#include "panvk_entrypoints.h"

#include "vk_log.h"

VKAPI_ATTR VkResult VKAPI_CALL
panvk_AllocateMemory(VkDevice _device,
                     const VkMemoryAllocateInfo *pAllocateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkDeviceMemory *pMem)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_instance *instance =
      to_panvk_instance(device->vk.physical->instance);
   struct panvk_device_memory *mem;
   bool can_be_exported = false;
   VkResult result;

   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

   const VkExportMemoryAllocateInfo *export_info =
      vk_find_struct_const(pAllocateInfo->pNext, EXPORT_MEMORY_ALLOCATE_INFO);

   if (export_info) {
      if (export_info->handleTypes &
          ~(VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT |
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT))
         return panvk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
      else if (export_info->handleTypes)
         can_be_exported = true;
   }

   mem = vk_device_memory_create(&device->vk, pAllocateInfo, pAllocator,
                                 sizeof(*mem));
   if (mem == NULL)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   const VkImportMemoryFdInfoKHR *fd_info =
      vk_find_struct_const(pAllocateInfo->pNext, IMPORT_MEMORY_FD_INFO_KHR);

   if (fd_info && !fd_info->handleType)
      fd_info = NULL;

   if (fd_info) {
      assert(
         fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
         fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

      mem->bo = pan_kmod_bo_import(device->kmod.dev, fd_info->fd, 0);
      if (!mem->bo) {
         result = panvk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
         goto err_destroy_mem;
      }
   } else {
      mem->bo = pan_kmod_bo_alloc(device->kmod.dev,
                                  can_be_exported ? NULL : device->kmod.vm,
                                  pAllocateInfo->allocationSize, 0);
      if (!mem->bo) {
         result = panvk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto err_destroy_mem;
      }
   }

   /* Always GPU-map at creation time. */
   struct pan_kmod_vm_op op = {
      .type = PAN_KMOD_VM_OP_TYPE_MAP,
      .va = {
         .start = PAN_KMOD_VM_MAP_AUTO_VA,
         .size = pan_kmod_bo_size(mem->bo),
      },
      .map = {
         .bo = mem->bo,
         .bo_offset = 0,
      },
   };

   if (!(device->kmod.vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      simple_mtx_lock(&device->as.lock);
      op.va.start =
         util_vma_heap_alloc(&device->as.heap, op.va.size,
                             op.va.size > 0x200000 ? 0x200000 : 0x1000);
      simple_mtx_unlock(&device->as.lock);
      if (!op.va.start) {
         result = panvk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto err_put_bo;
      }
   }

   int ret =
      pan_kmod_vm_bind(device->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &op, 1);
   if (ret) {
      result = panvk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      goto err_return_va;
   }

   mem->addr.dev = op.va.start;

   if (fd_info) {
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

   if (device->debug.decode_ctx) {
      if (instance->debug_flags & (PANVK_DEBUG_DUMP | PANVK_DEBUG_TRACE)) {
         mem->debug.host_mapping =
            pan_kmod_bo_mmap(mem->bo, 0, pan_kmod_bo_size(mem->bo),
                             PROT_READ | PROT_WRITE, MAP_SHARED, NULL);
      }

      pandecode_inject_mmap(device->debug.decode_ctx, mem->addr.dev,
                            mem->debug.host_mapping, pan_kmod_bo_size(mem->bo),
                            NULL);
   }

   *pMem = panvk_device_memory_to_handle(mem);

   return VK_SUCCESS;

err_return_va:
   if (!(device->kmod.vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      simple_mtx_lock(&device->as.lock);
      util_vma_heap_free(&device->as.heap, op.va.start, op.va.size);
      simple_mtx_unlock(&device->as.lock);
   }

err_put_bo:
   pan_kmod_bo_put(mem->bo);

err_destroy_mem:
   vk_device_memory_destroy(&device->vk, pAllocator, &mem->vk);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
panvk_FreeMemory(VkDevice _device, VkDeviceMemory _mem,
                 const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_device_memory, mem, _mem);

   if (mem == NULL)
      return;

   if (device->debug.decode_ctx) {
      pandecode_inject_free(device->debug.decode_ctx, mem->addr.dev,
                            pan_kmod_bo_size(mem->bo));

      if (mem->debug.host_mapping)
         os_munmap(mem->debug.host_mapping, pan_kmod_bo_size(mem->bo));
   }

   struct pan_kmod_vm_op op = {
      .type = PAN_KMOD_VM_OP_TYPE_UNMAP,
      .va = {
         .start = mem->addr.dev,
         .size = pan_kmod_bo_size(mem->bo),
      },
   };

   ASSERTED int ret =
      pan_kmod_vm_bind(device->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &op, 1);
   assert(!ret);

   if (!(device->kmod.vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      simple_mtx_lock(&device->as.lock);
      util_vma_heap_free(&device->as.heap, op.va.start, op.va.size);
      simple_mtx_unlock(&device->as.lock);
   }

   pan_kmod_bo_put(mem->bo);
   vk_device_memory_destroy(&device->vk, pAllocator, &mem->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_MapMemory2KHR(VkDevice _device, const VkMemoryMapInfoKHR *pMemoryMapInfo,
                    void **ppData)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_device_memory, mem, pMemoryMapInfo->memory);

   if (mem == NULL) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   const VkDeviceSize offset = pMemoryMapInfo->offset;
   const VkDeviceSize size = vk_device_memory_range(
      &mem->vk, pMemoryMapInfo->offset, pMemoryMapInfo->size);

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
      return panvk_errorf(device, VK_ERROR_MEMORY_MAP_FAILED,
                          "requested size 0x%" PRIx64
                          " does not fit in %u bits",
                          size, (unsigned)(sizeof(size_t) * 8));
   }

   /* From the Vulkan 1.2.194 spec:
    *
    *    "memory must not be currently host mapped"
    */
   if (mem->addr.host)
      return panvk_errorf(device, VK_ERROR_MEMORY_MAP_FAILED,
                          "Memory object already mapped.");

   void *addr = pan_kmod_bo_mmap(mem->bo, 0, pan_kmod_bo_size(mem->bo),
                                 PROT_READ | PROT_WRITE, MAP_SHARED, NULL);
   if (addr == MAP_FAILED)
      return panvk_errorf(device, VK_ERROR_MEMORY_MAP_FAILED,
                          "Memory object couldn't be mapped.");

   mem->addr.host = addr;
   *ppData = mem->addr.host + offset;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_UnmapMemory2KHR(VkDevice _device,
                      const VkMemoryUnmapInfoKHR *pMemoryUnmapInfo)
{
   VK_FROM_HANDLE(panvk_device_memory, mem, pMemoryUnmapInfo->memory);

   if (mem->addr.host) {
      ASSERTED int ret =
         os_munmap((void *)mem->addr.host, pan_kmod_bo_size(mem->bo));

      assert(!ret);
      mem->addr.host = NULL;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_FlushMappedMemoryRanges(VkDevice _device, uint32_t memoryRangeCount,
                              const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_InvalidateMappedMemoryRanges(VkDevice _device, uint32_t memoryRangeCount,
                                   const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_GetMemoryFdKHR(VkDevice _device, const VkMemoryGetFdInfoKHR *pGetFdInfo,
                     int *pFd)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_device_memory, memory, pGetFdInfo->memory);

   assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);

   /* At the moment, we support only the below handle types. */
   assert(
      pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
      pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

   int prime_fd = pan_kmod_bo_export(memory->bo);
   if (prime_fd < 0)
      return panvk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   *pFd = prime_fd;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_GetMemoryFdPropertiesKHR(VkDevice _device,
                               VkExternalMemoryHandleTypeFlagBits handleType,
                               int fd,
                               VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   assert(handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
   pMemoryFdProperties->memoryTypeBits = 1;
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory,
                                VkDeviceSize *pCommittedMemoryInBytes)
{
   *pCommittedMemoryInBytes = 0;
}

VKAPI_ATTR uint64_t VKAPI_CALL
panvk_GetDeviceMemoryOpaqueCaptureAddress(
   VkDevice _device, const VkDeviceMemoryOpaqueCaptureAddressInfo *pInfo)
{
   VK_FROM_HANDLE(panvk_device_memory, memory, pInfo->memory);

   return memory->addr.dev;
}
