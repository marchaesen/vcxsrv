/*
 * Copyright © 2021 Collabora Ltd.
 * Copyright © 2024 Arm Ltd.
 *
 * Derived from tu_image.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "vk_cmd_enqueue_entrypoints.h"
#include "vk_common_entrypoints.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_instance.h"
#include "panvk_macros.h"
#include "panvk_physical_device.h"
#include "panvk_precomp_cache.h"
#include "panvk_priv_bo.h"
#include "panvk_queue.h"
#include "panvk_utrace.h"
#include "panvk_utrace_perfetto.h"

#include "genxml/decode.h"
#include "genxml/gen_macros.h"

#include "clc/panfrost_compile.h"
#include "kmod/pan_kmod.h"
#include "util/u_printf.h"
#include "pan_props.h"
#include "pan_samples.h"

static void *
panvk_kmod_zalloc(const struct pan_kmod_allocator *allocator, size_t size,
                  bool transient)
{
   const VkAllocationCallbacks *vkalloc = allocator->priv;

   void *obj = vk_zalloc(vkalloc, size, 8,
                         transient ? VK_SYSTEM_ALLOCATION_SCOPE_COMMAND
                                   : VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   /* We force errno to -ENOMEM on host allocation failures so we can properly
    * report it back as VK_ERROR_OUT_OF_HOST_MEMORY. */
   if (!obj)
      errno = -ENOMEM;

   return obj;
}

static void
panvk_kmod_free(const struct pan_kmod_allocator *allocator, void *data)
{
   const VkAllocationCallbacks *vkalloc = allocator->priv;

   return vk_free(vkalloc, data);
}

static void
panvk_device_init_mempools(struct panvk_device *dev)
{
   struct panvk_pool_properties rw_pool_props = {
      .create_flags = 0,
      .slab_size = 16 * 1024,
      .label = "Device RW cached memory pool",
      .owns_bos = false,
      .needs_locking = true,
      .prealloc = false,
   };

   panvk_pool_init(&dev->mempools.rw, dev, NULL, &rw_pool_props);

   struct panvk_pool_properties rw_nc_pool_props = {
      .create_flags = PAN_ARCH <= 9 ? 0 : PAN_KMOD_BO_FLAG_GPU_UNCACHED,
      .slab_size = 16 * 1024,
      .label = "Device RW uncached memory pool",
      .owns_bos = false,
      .needs_locking = true,
      .prealloc = false,
   };

   panvk_pool_init(&dev->mempools.rw_nc, dev, NULL, &rw_nc_pool_props);

   struct panvk_pool_properties exec_pool_props = {
      .create_flags = PAN_KMOD_BO_FLAG_EXECUTABLE,
      .slab_size = 16 * 1024,
      .label = "Device executable memory pool (shaders)",
      .owns_bos = false,
      .needs_locking = true,
      .prealloc = false,
   };

   panvk_pool_init(&dev->mempools.exec, dev, NULL, &exec_pool_props);
}

static void
panvk_device_cleanup_mempools(struct panvk_device *dev)
{
   panvk_pool_cleanup(&dev->mempools.rw);
   panvk_pool_cleanup(&dev->mempools.rw_nc);
   panvk_pool_cleanup(&dev->mempools.exec);
}

static VkResult
panvk_meta_cmd_bind_map_buffer(struct vk_command_buffer *cmd,
                               struct vk_meta_device *meta, VkBuffer buf,
                               void **map_out)
{
   VK_FROM_HANDLE(panvk_buffer, buffer, buf);
   struct panvk_cmd_buffer *cmdbuf =
      container_of(cmd, struct panvk_cmd_buffer, vk);
   struct panfrost_ptr mem =
      panvk_cmd_alloc_dev_mem(cmdbuf, desc, buffer->vk.size, 64);

   if (!mem.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   buffer->dev_addr = mem.gpu;
   *map_out = mem.cpu;
   return VK_SUCCESS;
}

static VkResult
panvk_meta_init(struct panvk_device *device)
{
   const struct vk_physical_device *pdev = device->vk.physical;

   VkResult result = vk_meta_device_init(&device->vk, &device->meta);
   if (result != VK_SUCCESS)
      return result;

   device->meta.use_stencil_export = true;
   device->meta.use_rect_list_pipeline = true;
   device->meta.max_bind_map_buffer_size_B = 64 * 1024;
   device->meta.cmd_bind_map_buffer = panvk_meta_cmd_bind_map_buffer;

   /* Assume a maximum of 1024 bytes per worgroup and choose the workgroup size
    * accordingly. */
   for (uint32_t i = 0;
        i < ARRAY_SIZE(device->meta.buffer_access.optimal_wg_size); i++) {
      device->meta.buffer_access.optimal_wg_size[i] =
         MIN2(1024 >> i, pdev->properties.maxComputeWorkGroupSize[0]);
   }

   return VK_SUCCESS;
}

static void
panvk_meta_cleanup(struct panvk_device *device)
{
   vk_meta_device_finish(&device->vk, &device->meta);
}

static VkResult
panvk_precomp_init(struct panvk_device *device)
{
   device->precomp_cache = panvk_per_arch(precomp_cache_init)(device);

   if (device->precomp_cache == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   return VK_SUCCESS;
}

static void
panvk_precomp_cleanup(struct panvk_device *device)
{
   panvk_per_arch(precomp_cache_cleanup)(device->precomp_cache);
}

/* Always reserve the lower 32MB. */
#define PANVK_VA_RESERVE_BOTTOM 0x2000000ull

static enum pan_kmod_group_allow_priority_flags
global_priority_to_group_allow_priority_flag(
   VkQueueGlobalPriorityKHR priority)
{
   switch (priority) {
   case VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR:
      return PAN_KMOD_GROUP_ALLOW_PRIORITY_LOW;
   case VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR:
      return PAN_KMOD_GROUP_ALLOW_PRIORITY_MEDIUM;
   case VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR:
      return PAN_KMOD_GROUP_ALLOW_PRIORITY_HIGH;
   case VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR:
      return PAN_KMOD_GROUP_ALLOW_PRIORITY_REALTIME;
   default:
      unreachable("Invalid global priority");
   }
}

static VkResult
check_global_priority(const struct panvk_physical_device *phys_dev,
                      const VkDeviceQueueCreateInfo *create_info)
{
   const VkDeviceQueueGlobalPriorityCreateInfoKHR *priority_info =
      vk_find_struct_const(create_info->pNext,
                           DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR);
   const VkQueueGlobalPriorityKHR priority =
      priority_info ? priority_info->globalPriority
                    : VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;

   enum pan_kmod_group_allow_priority_flags requested_prio =
      global_priority_to_group_allow_priority_flag(priority);
   enum pan_kmod_group_allow_priority_flags allowed_prio_mask =
      phys_dev->kmod.props.allowed_group_priorities_mask;

   if (requested_prio & allowed_prio_mask)
      return VK_SUCCESS;

   return VK_ERROR_NOT_PERMITTED_KHR;
}

VkResult
panvk_per_arch(create_device)(struct panvk_physical_device *physical_device,
                              const VkDeviceCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *pAllocator,
                              VkDevice *pDevice)
{
   struct panvk_instance *instance =
      to_panvk_instance(physical_device->vk.instance);
   VkResult result;
   struct panvk_device *device;

   device = vk_zalloc2(&instance->vk.alloc, pAllocator, sizeof(*device), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return panvk_error(physical_device, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_device_dispatch_table dispatch_table;



   if (PAN_ARCH <= 9) {
      /* For secondary command buffer support, overwrite any command entrypoints
       * in the main device-level dispatch table with
       * vk_cmd_enqueue_unless_primary_Cmd*.
       */
      vk_device_dispatch_table_from_entrypoints(
         &dispatch_table, &vk_cmd_enqueue_unless_primary_device_entrypoints, true);

      /* Populate our primary cmd_dispatch table. */
      vk_device_dispatch_table_from_entrypoints(
         &device->cmd_dispatch, &panvk_per_arch(device_entrypoints), true);
      vk_device_dispatch_table_from_entrypoints(&device->cmd_dispatch,
                                                &panvk_device_entrypoints,
                                                false);
      vk_device_dispatch_table_from_entrypoints(&device->cmd_dispatch,
		                                &vk_common_device_entrypoints,
                                                false);
   }

   vk_device_dispatch_table_from_entrypoints(
      &dispatch_table, &panvk_per_arch(device_entrypoints), PAN_ARCH > 9);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &panvk_device_entrypoints, false);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &wsi_device_entrypoints, false);

   result = vk_device_init(&device->vk, &physical_device->vk, &dispatch_table,
                           pCreateInfo, pAllocator);
   if (result != VK_SUCCESS)
      goto err_free_dev;

   /* Must be done after vk_device_init() because this function memset(0) the
    * whole struct.
    */
   device->vk.command_dispatch_table = &device->cmd_dispatch;
   device->vk.command_buffer_ops = &panvk_per_arch(cmd_buffer_ops);
   device->vk.shader_ops = &panvk_per_arch(device_shader_ops);
   device->vk.check_status = panvk_per_arch(device_check_status);

   device->kmod.allocator = (struct pan_kmod_allocator){
      .zalloc = panvk_kmod_zalloc,
      .free = panvk_kmod_free,
      .priv = &device->vk.alloc,
   };
   device->kmod.dev =
      pan_kmod_dev_create(dup(physical_device->kmod.dev->fd),
                          PAN_KMOD_DEV_FLAG_OWNS_FD, &device->kmod.allocator);

   if (!device->kmod.dev) {
      result = panvk_errorf(instance, VK_ERROR_OUT_OF_HOST_MEMORY,
                            "cannot create device");
      goto err_finish_dev;
   }

   if (instance->debug_flags &
       (PANVK_DEBUG_TRACE | PANVK_DEBUG_SYNC | PANVK_DEBUG_DUMP))
      device->debug.decode_ctx = pandecode_create_context(false);

   /* 32bit address space, with the lower 32MB reserved. We clamp
    * things so it matches kmod VA range limitations.
    */
   uint64_t user_va_start = panfrost_clamp_to_usable_va_range(
      device->kmod.dev, PANVK_VA_RESERVE_BOTTOM);
   uint64_t user_va_end =
      panfrost_clamp_to_usable_va_range(device->kmod.dev, 1ull << 32);
   uint32_t vm_flags = PAN_ARCH <= 7 ? PAN_KMOD_VM_FLAG_AUTO_VA : 0;

   device->kmod.vm =
      pan_kmod_vm_create(device->kmod.dev, vm_flags,
                         user_va_start, user_va_end - user_va_start);

   if (!device->kmod.vm) {
      result = panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_destroy_kdev;
   }

   simple_mtx_init(&device->as.lock, mtx_plain);
   util_vma_heap_init(&device->as.heap, user_va_start,
                      user_va_end - user_va_start);

   panvk_device_init_mempools(device);

#if PAN_ARCH <= 9
   result = panvk_priv_bo_create(
      device, 128 * 1024 * 1024,
      PAN_KMOD_BO_FLAG_NO_MMAP | PAN_KMOD_BO_FLAG_ALLOC_ON_FAULT,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &device->tiler_heap);
   if (result != VK_SUCCESS)
      goto err_free_priv_bos;
#endif

   result = panvk_priv_bo_create(
      device, panfrost_sample_positions_buffer_size(), 0,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &device->sample_positions);
   if (result != VK_SUCCESS)
      goto err_free_priv_bos;

   panfrost_upload_sample_positions(device->sample_positions->addr.host);

#if PAN_ARCH >= 10
   result = panvk_per_arch(init_tiler_oom)(device);
   if (result != VK_SUCCESS)
      goto err_free_priv_bos;
#endif

   result = panvk_priv_bo_create(device, LIBPAN_PRINTF_BUFFER_SIZE, 0,
                                 VK_SYSTEM_ALLOCATION_SCOPE_DEVICE,
                                 &device->printf.bo);
   if (result != VK_SUCCESS)
      goto err_free_priv_bos;

   u_printf_init(&device->printf.ctx, device->printf.bo,
                 device->printf.bo->addr.host);

   vk_device_set_drm_fd(&device->vk, device->kmod.dev->fd);


   result = panvk_precomp_init(device);
   if (result != VK_SUCCESS)
      goto err_free_priv_bos;

   result = panvk_meta_init(device);
   if (result != VK_SUCCESS)
      goto err_free_precomp;

   for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      const VkDeviceQueueCreateInfo *queue_create =
         &pCreateInfo->pQueueCreateInfos[i];

      result = check_global_priority(physical_device, queue_create);
      if (result != VK_SUCCESS)
         goto err_finish_queues;

      uint32_t qfi = queue_create->queueFamilyIndex;
      device->queues[qfi] =
         vk_zalloc(&device->vk.alloc,
                  queue_create->queueCount * sizeof(struct panvk_queue), 8,
                  VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!device->queues[qfi]) {
         result = panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
         goto err_finish_queues;
      }

      for (unsigned q = 0; q < queue_create->queueCount; q++) {
         result = panvk_per_arch(queue_init)(device, &device->queues[qfi][q], q,
                                             queue_create);
         if (result != VK_SUCCESS)
            goto err_finish_queues;

         device->queue_count[qfi]++;
      }
   }

   panvk_per_arch(utrace_context_init)(device);
#if PAN_ARCH >= 10
   panvk_utrace_perfetto_init(device, PANVK_SUBQUEUE_COUNT);
#else
   panvk_utrace_perfetto_init(device, 2);
#endif

   *pDevice = panvk_device_to_handle(device);
   return VK_SUCCESS;

err_finish_queues:
   for (unsigned i = 0; i < PANVK_MAX_QUEUE_FAMILIES; i++) {
      for (unsigned q = 0; q < device->queue_count[i]; q++)
         panvk_per_arch(queue_finish)(&device->queues[i][q]);
      if (device->queues[i])
         vk_free(&device->vk.alloc, device->queues[i]);
   }

   panvk_meta_cleanup(device);

err_free_precomp:
   panvk_precomp_cleanup(device);
err_free_priv_bos:
   if (device->printf.bo)
      u_printf_destroy(&device->printf.ctx);
   panvk_priv_bo_unref(device->printf.bo);
   panvk_priv_bo_unref(device->tiler_oom.handlers_bo);
   panvk_priv_bo_unref(device->sample_positions);
   panvk_priv_bo_unref(device->tiler_heap);
   panvk_device_cleanup_mempools(device);
   pan_kmod_vm_destroy(device->kmod.vm);
   util_vma_heap_finish(&device->as.heap);
   simple_mtx_destroy(&device->as.lock);

err_destroy_kdev:
   pan_kmod_dev_destroy(device->kmod.dev);

err_finish_dev:
   vk_device_finish(&device->vk);

err_free_dev:
   vk_free(&device->vk.alloc, device);
   return result;
}

void
panvk_per_arch(destroy_device)(struct panvk_device *device,
                               const VkAllocationCallbacks *pAllocator)
{
   if (!device)
      return;

   panvk_per_arch(utrace_context_fini)(device);

   for (unsigned i = 0; i < PANVK_MAX_QUEUE_FAMILIES; i++) {
      for (unsigned q = 0; q < device->queue_count[i]; q++)
         panvk_per_arch(queue_finish)(&device->queues[i][q]);
      if (device->queue_count[i])
         vk_free(&device->vk.alloc, device->queues[i]);
   }

   panvk_precomp_cleanup(device);
   panvk_meta_cleanup(device);
   u_printf_destroy(&device->printf.ctx);
   panvk_priv_bo_unref(device->printf.bo);
   panvk_priv_bo_unref(device->tiler_oom.handlers_bo);
   panvk_priv_bo_unref(device->tiler_heap);
   panvk_priv_bo_unref(device->sample_positions);
   panvk_device_cleanup_mempools(device);
   pan_kmod_vm_destroy(device->kmod.vm);
   util_vma_heap_finish(&device->as.heap);
   simple_mtx_destroy(&device->as.lock);

   if (device->debug.decode_ctx)
      pandecode_destroy_context(device->debug.decode_ctx);

   pan_kmod_dev_destroy(device->kmod.dev);
   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(GetRenderAreaGranularity)(VkDevice device,
                                         VkRenderPass renderPass,
                                         VkExtent2D *pGranularity)
{
   *pGranularity = (VkExtent2D){32, 32};
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(GetRenderingAreaGranularityKHR)(
   VkDevice _device, const VkRenderingAreaInfoKHR *pRenderingAreaInfo,
   VkExtent2D *pGranularity)
{
   *pGranularity = (VkExtent2D){32, 32};
}
