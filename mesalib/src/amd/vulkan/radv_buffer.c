/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_buffer.h"
#include "radv_device.h"
#include "radv_device_memory.h"
#include "radv_dgc.h"
#include "radv_entrypoints.h"
#include "radv_instance.h"
#include "radv_physical_device.h"
#include "radv_rmv.h"

#include "vk_common_entrypoints.h"
#include "vk_debug_utils.h"
#include "vk_log.h"

void
radv_buffer_init(struct radv_buffer *buffer, struct radv_device *device, struct radeon_winsys_bo *bo, uint64_t size,
                 uint64_t offset)
{
   VkBufferCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
   };

   vk_buffer_init(&device->vk, &buffer->vk, &createInfo);

   buffer->bo = bo;
   buffer->addr = radv_buffer_get_va(bo) + offset;
}

void
radv_buffer_finish(struct radv_buffer *buffer)
{
   vk_buffer_finish(&buffer->vk);
}

static void
radv_destroy_buffer(struct radv_device *device, const VkAllocationCallbacks *pAllocator, struct radv_buffer *buffer)
{
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);

   if ((buffer->vk.create_flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) && buffer->bo)
      radv_bo_destroy(device, &buffer->vk.base, buffer->bo);

   if (buffer->addr)
      vk_address_binding_report(&instance->vk, &buffer->vk.base, buffer->addr, buffer->range,
                                VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);

   radv_rmv_log_resource_destroy(device, (uint64_t)radv_buffer_to_handle(buffer));
   radv_buffer_finish(buffer);
   vk_free2(&device->vk.alloc, pAllocator, buffer);
}

VkResult
radv_create_buffer(struct radv_device *device, const VkBufferCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer, bool is_internal)
{
   struct radv_buffer *buffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);

#if DETECT_OS_ANDROID
   /* reject buffers that are larger than maxBufferSize on Android, which
    * might not have VK_KHR_maintenance4
    */
   if (pCreateInfo->size > RADV_MAX_MEMORY_ALLOCATION_SIZE)
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
#endif

   buffer = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*buffer), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (buffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_buffer_init(&device->vk, &buffer->vk, pCreateInfo);
   buffer->bo = NULL;
   buffer->addr = 0;
   buffer->range = 0;

   uint64_t replay_address = 0;
   const VkBufferOpaqueCaptureAddressCreateInfo *replay_info =
      vk_find_struct_const(pCreateInfo->pNext, BUFFER_OPAQUE_CAPTURE_ADDRESS_CREATE_INFO);
   if (replay_info && replay_info->opaqueCaptureAddress)
      replay_address = replay_info->opaqueCaptureAddress;

   if (pCreateInfo->flags & VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT)
      buffer->addr = replay_address;

   if (pCreateInfo->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) {
      enum radeon_bo_flag flags = RADEON_FLAG_VIRTUAL;
      if (pCreateInfo->flags & VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT)
         flags |= RADEON_FLAG_REPLAYABLE;
      if (buffer->vk.usage &
          (VK_BUFFER_USAGE_2_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_2_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT))
         flags |= RADEON_FLAG_32BIT;

      VkResult result = radv_bo_create(device, &buffer->vk.base, align64(buffer->vk.size, 4096), 4096, 0, flags,
                                       RADV_BO_PRIORITY_VIRTUAL, replay_address, is_internal, &buffer->bo);
      if (result != VK_SUCCESS) {
         radv_destroy_buffer(device, pAllocator, buffer);
         return vk_error(device, result);
      }

      buffer->addr = radv_buffer_get_va(buffer->bo);
   }

   *pBuffer = radv_buffer_to_handle(buffer);
   vk_rmv_log_buffer_create(&device->vk, false, *pBuffer);
   if (buffer->bo)
      radv_rmv_log_buffer_bind(device, *pBuffer);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateBuffer(VkDevice _device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                  VkBuffer *pBuffer)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   return radv_create_buffer(device, pCreateInfo, pAllocator, pBuffer, false);
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyBuffer(VkDevice _device, VkBuffer _buffer, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_buffer, buffer, _buffer);

   if (!buffer)
      return;

   radv_destroy_buffer(device, pAllocator, buffer);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_BindBufferMemory2(VkDevice _device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      VK_FROM_HANDLE(radv_device_memory, mem, pBindInfos[i].memory);
      VK_FROM_HANDLE(radv_buffer, buffer, pBindInfos[i].buffer);
      VkBindMemoryStatus *status = (void *)vk_find_struct_const(&pBindInfos[i], BIND_MEMORY_STATUS);

      if (status)
         *status->pResult = VK_SUCCESS;

      VkBufferMemoryRequirementsInfo2 info = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
         .buffer = pBindInfos[i].buffer,
      };
      VkMemoryRequirements2 reqs = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
      };

      vk_common_GetBufferMemoryRequirements2(_device, &info, &reqs);

      if (mem->alloc_size) {
         if (pBindInfos[i].memoryOffset + reqs.memoryRequirements.size > mem->alloc_size) {
            if (status)
               *status->pResult = VK_ERROR_UNKNOWN;
            return vk_errorf(device, VK_ERROR_UNKNOWN, "Device memory object too small for the buffer.\n");
         }
      }

      buffer->bo = mem->bo;
      buffer->addr = radv_buffer_get_va(mem->bo) + pBindInfos[i].memoryOffset;
      buffer->range = reqs.memoryRequirements.size;

      radv_rmv_log_buffer_bind(device, pBindInfos[i].buffer);

      vk_address_binding_report(&instance->vk, &buffer->vk.base, buffer->addr, buffer->range,
                                VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);
   }
   return VK_SUCCESS;
}

static void
radv_get_buffer_memory_requirements(struct radv_device *device, VkDeviceSize size, VkBufferCreateFlags flags,
                                    VkBufferUsageFlags2 usage, VkMemoryRequirements2 *pMemoryRequirements)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      ((1u << pdev->memory_properties.memoryTypeCount) - 1u) & ~pdev->memory_types_32bit;

   /* Force 32-bit address-space for descriptor buffers usage because they are passed to shaders
    * through 32-bit pointers.
    */
   if (usage & (VK_BUFFER_USAGE_2_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                VK_BUFFER_USAGE_2_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_2_PREPROCESS_BUFFER_BIT_EXT))
      pMemoryRequirements->memoryRequirements.memoryTypeBits = pdev->memory_types_32bit;

   if (flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) {
      pMemoryRequirements->memoryRequirements.alignment = 4096;
   } else {
      if (usage & VK_BUFFER_USAGE_2_PREPROCESS_BUFFER_BIT_EXT)
         pMemoryRequirements->memoryRequirements.alignment = radv_dgc_get_buffer_alignment(device);
      else
         pMemoryRequirements->memoryRequirements.alignment = 16;
   }

   /* Top level acceleration structures need the bottom 6 bits to store
    * the root ids of instances. The hardware also needs bvh nodes to
    * be 64 byte aligned.
    */
   if (usage & VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)
      pMemoryRequirements->memoryRequirements.alignment = MAX2(pMemoryRequirements->memoryRequirements.alignment, 64);

   pMemoryRequirements->memoryRequirements.size = align64(size, pMemoryRequirements->memoryRequirements.alignment);

   vk_foreach_struct (ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req = (VkMemoryDedicatedRequirements *)ext;
         req->requiresDedicatedAllocation = false;
         req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
         break;
      }
      default:
         break;
      }
   }
}

static const VkBufferUsageFlagBits2
radv_get_buffer_usage_flags(const VkBufferCreateInfo *pCreateInfo)
{
   const VkBufferUsageFlags2CreateInfo *flags2 =
      vk_find_struct_const(pCreateInfo->pNext, BUFFER_USAGE_FLAGS_2_CREATE_INFO);
   return flags2 ? flags2->usage : pCreateInfo->usage;
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDeviceBufferMemoryRequirements(VkDevice _device, const VkDeviceBufferMemoryRequirements *pInfo,
                                       VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   const VkBufferUsageFlagBits2 usage_flags = radv_get_buffer_usage_flags(pInfo->pCreateInfo);

   radv_get_buffer_memory_requirements(device, pInfo->pCreateInfo->size, pInfo->pCreateInfo->flags, usage_flags,
                                       pMemoryRequirements);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL
radv_GetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo *pInfo)
{
   VK_FROM_HANDLE(radv_buffer, buffer, pInfo->buffer);
   return buffer->addr;
}

VKAPI_ATTR uint64_t VKAPI_CALL
radv_GetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo *pInfo)
{
   VK_FROM_HANDLE(radv_buffer, buffer, pInfo->buffer);
   return buffer->addr;
}

VkResult
radv_bo_create(struct radv_device *device, struct vk_object_base *object, uint64_t size, unsigned alignment,
               enum radeon_bo_domain domain, enum radeon_bo_flag flags, unsigned priority, uint64_t address,
               bool is_internal, struct radeon_winsys_bo **out_bo)
{
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);
   struct radeon_winsys *ws = device->ws;
   VkResult result;

   result = ws->buffer_create(ws, size, alignment, domain, flags, priority, address, out_bo);
   if (result != VK_SUCCESS)
      return result;

   radv_rmv_log_bo_allocate(device, *out_bo, is_internal);

   vk_address_binding_report(&instance->vk, object ? object : &device->vk.base, radv_buffer_get_va(*out_bo),
                             (*out_bo)->size, VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);
   return VK_SUCCESS;
}

void
radv_bo_destroy(struct radv_device *device, struct vk_object_base *object, struct radeon_winsys_bo *bo)
{
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);
   struct radeon_winsys *ws = device->ws;

   radv_rmv_log_bo_destroy(device, bo);

   vk_address_binding_report(&instance->vk, object ? object : &device->vk.base, radv_buffer_get_va(bo), bo->size,
                             VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);

   ws->buffer_destroy(ws, bo);
}

VkResult
radv_bo_virtual_bind(struct radv_device *device, struct vk_object_base *object, struct radeon_winsys_bo *parent,
                     uint64_t offset, uint64_t size, struct radeon_winsys_bo *bo, uint64_t bo_offset)
{
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);
   struct radeon_winsys *ws = device->ws;
   VkResult result;

   result = ws->buffer_virtual_bind(ws, parent, offset, size, bo, bo_offset);
   if (result != VK_SUCCESS)
      return result;

   if (bo)
      radv_rmv_log_sparse_add_residency(device, parent, offset);
   else
      radv_rmv_log_sparse_remove_residency(device, parent, offset);

   vk_address_binding_report(&instance->vk, object, radv_buffer_get_va(parent) + offset, size,
                             bo ? VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT : VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);

   return VK_SUCCESS;
}

VkResult
radv_bo_from_fd(struct radv_device *device, int fd, unsigned priority, struct radv_device_memory *mem,
                uint64_t *alloc_size)
{
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);
   struct radeon_winsys *ws = device->ws;
   VkResult result;

   result = ws->buffer_from_fd(ws, fd, priority, &mem->bo, alloc_size);
   if (result != VK_SUCCESS)
      return result;

   vk_address_binding_report(&instance->vk, &mem->base, radv_buffer_get_va(mem->bo), mem->bo->size,
                             VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);

   return result;
}

VkResult
radv_bo_from_ptr(struct radv_device *device, void *host_ptr, uint64_t alloc_size, unsigned priority,
                 struct radv_device_memory *mem)
{
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);
   struct radeon_winsys *ws = device->ws;
   VkResult result;

   result = ws->buffer_from_ptr(ws, host_ptr, alloc_size, priority, &mem->bo);
   if (result != VK_SUCCESS)
      return result;

   vk_address_binding_report(&instance->vk, &mem->base, radv_buffer_get_va(mem->bo), mem->bo->size,
                             VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);

   return result;
}
