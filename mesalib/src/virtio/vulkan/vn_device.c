/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_device.h"

#include "venus-protocol/vn_protocol_driver_device.h"

#include "vn_android.h"
#include "vn_instance.h"
#include "vn_physical_device.h"
#include "vn_queue.h"

/* device commands */

static void
vn_queue_fini(struct vn_queue *queue)
{
   if (queue->wait_fence != VK_NULL_HANDLE) {
      vn_DestroyFence(vn_device_to_handle(queue->device), queue->wait_fence,
                      NULL);
   }
   vn_object_base_fini(&queue->base);
}

static VkResult
vn_queue_init(struct vn_device *dev,
              struct vn_queue *queue,
              const VkDeviceQueueCreateInfo *queue_info,
              uint32_t queue_index)
{
   vn_object_base_init(&queue->base, VK_OBJECT_TYPE_QUEUE, &dev->base);

   VkQueue queue_handle = vn_queue_to_handle(queue);
   vn_async_vkGetDeviceQueue2(
      dev->instance, vn_device_to_handle(dev),
      &(VkDeviceQueueInfo2){
         .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
         .flags = queue_info->flags,
         .queueFamilyIndex = queue_info->queueFamilyIndex,
         .queueIndex = queue_index,
      },
      &queue_handle);

   queue->device = dev;
   queue->family = queue_info->queueFamilyIndex;
   queue->index = queue_index;
   queue->flags = queue_info->flags;

   const VkExportFenceCreateInfo export_fence_info = {
      .sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO,
      .pNext = NULL,
      .handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT,
   };
   const VkFenceCreateInfo fence_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = dev->instance->experimental.globalFencing == VK_TRUE
                  ? &export_fence_info
                  : NULL,
      .flags = 0,
   };
   VkResult result = vn_CreateFence(vn_device_to_handle(dev), &fence_info,
                                    NULL, &queue->wait_fence);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

static VkResult
vn_device_init_queues(struct vn_device *dev,
                      const VkDeviceCreateInfo *create_info)
{
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;

   uint32_t count = 0;
   for (uint32_t i = 0; i < create_info->queueCreateInfoCount; i++)
      count += create_info->pQueueCreateInfos[i].queueCount;

   struct vn_queue *queues =
      vk_zalloc(alloc, sizeof(*queues) * count, VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!queues)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   VkResult result = VK_SUCCESS;
   count = 0;
   for (uint32_t i = 0; i < create_info->queueCreateInfoCount; i++) {
      const VkDeviceQueueCreateInfo *queue_info =
         &create_info->pQueueCreateInfos[i];
      for (uint32_t j = 0; j < queue_info->queueCount; j++) {
         result = vn_queue_init(dev, &queues[count], queue_info, j);
         if (result != VK_SUCCESS)
            break;

         count++;
      }
   }

   if (result != VK_SUCCESS) {
      for (uint32_t i = 0; i < count; i++)
         vn_queue_fini(&queues[i]);
      vk_free(alloc, queues);

      return result;
   }

   dev->queues = queues;
   dev->queue_count = count;

   return VK_SUCCESS;
}

static bool
find_extension_names(const char *const *exts,
                     uint32_t ext_count,
                     const char *name)
{
   for (uint32_t i = 0; i < ext_count; i++) {
      if (!strcmp(exts[i], name))
         return true;
   }
   return false;
}

static bool
merge_extension_names(const char *const *exts,
                      uint32_t ext_count,
                      const char *const *extra_exts,
                      uint32_t extra_count,
                      const char *const *block_exts,
                      uint32_t block_count,
                      const VkAllocationCallbacks *alloc,
                      const char *const **out_exts,
                      uint32_t *out_count)
{
   const char **merged =
      vk_alloc(alloc, sizeof(*merged) * (ext_count + extra_count),
               VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!merged)
      return false;

   uint32_t count = 0;
   for (uint32_t i = 0; i < ext_count; i++) {
      if (!find_extension_names(block_exts, block_count, exts[i]))
         merged[count++] = exts[i];
   }
   for (uint32_t i = 0; i < extra_count; i++) {
      if (!find_extension_names(exts, ext_count, extra_exts[i]))
         merged[count++] = extra_exts[i];
   }

   *out_exts = merged;
   *out_count = count;
   return true;
}

static const VkDeviceCreateInfo *
vn_device_fix_create_info(const struct vn_device *dev,
                          const VkDeviceCreateInfo *dev_info,
                          const VkAllocationCallbacks *alloc,
                          VkDeviceCreateInfo *local_info)
{
   const struct vn_physical_device *physical_dev = dev->physical_device;
   const struct vk_device_extension_table *app_exts =
      &dev->base.base.enabled_extensions;
   /* extra_exts and block_exts must not overlap */
   const char *extra_exts[16];
   const char *block_exts[16];
   uint32_t extra_count = 0;
   uint32_t block_count = 0;

   /* fix for WSI (treat AHB as WSI extension for simplicity) */
   const bool has_wsi =
      app_exts->KHR_swapchain || app_exts->ANDROID_native_buffer ||
      app_exts->ANDROID_external_memory_android_hardware_buffer;
   if (has_wsi) {
      /* KHR_swapchain may be advertised without the renderer support for
       * EXT_image_drm_format_modifier
       */
      if (!app_exts->EXT_image_drm_format_modifier &&
          physical_dev->renderer_extensions.EXT_image_drm_format_modifier) {
         extra_exts[extra_count++] =
            VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME;

         if (physical_dev->renderer_version < VK_API_VERSION_1_2 &&
             !app_exts->KHR_image_format_list) {
            extra_exts[extra_count++] =
               VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME;
         }
      }

      /* XXX KHR_swapchain may be advertised without the renderer support for
       * EXT_queue_family_foreign
       */
      if (!app_exts->EXT_queue_family_foreign &&
          physical_dev->renderer_extensions.EXT_queue_family_foreign) {
         extra_exts[extra_count++] =
            VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME;
      }

      if (app_exts->KHR_swapchain) {
         /* see vn_physical_device_get_native_extensions */
         block_exts[block_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
         block_exts[block_count++] =
            VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME;
         block_exts[block_count++] =
            VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME;
      }

      if (app_exts->ANDROID_native_buffer)
         block_exts[block_count++] = VK_ANDROID_NATIVE_BUFFER_EXTENSION_NAME;

      if (app_exts->ANDROID_external_memory_android_hardware_buffer) {
         block_exts[block_count++] =
            VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME;
      }
   }

   if (app_exts->KHR_external_memory_fd ||
       app_exts->EXT_external_memory_dma_buf || has_wsi) {
      switch (physical_dev->external_memory.renderer_handle_type) {
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
         if (!app_exts->EXT_external_memory_dma_buf) {
            extra_exts[extra_count++] =
               VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
         }
         FALLTHROUGH;
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
         if (!app_exts->KHR_external_memory_fd) {
            extra_exts[extra_count++] =
               VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
         }
         break;
      default:
         /* TODO other handle types */
         break;
      }
   }

   assert(extra_count <= ARRAY_SIZE(extra_exts));
   assert(block_count <= ARRAY_SIZE(block_exts));

   if (!extra_count && (!block_count || !dev_info->enabledExtensionCount))
      return dev_info;

   *local_info = *dev_info;
   if (!merge_extension_names(dev_info->ppEnabledExtensionNames,
                              dev_info->enabledExtensionCount, extra_exts,
                              extra_count, block_exts, block_count, alloc,
                              &local_info->ppEnabledExtensionNames,
                              &local_info->enabledExtensionCount))
      return NULL;

   return local_info;
}

VkResult
vn_CreateDevice(VkPhysicalDevice physicalDevice,
                const VkDeviceCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator,
                VkDevice *pDevice)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   struct vn_instance *instance = physical_dev->instance;
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &instance->base.base.alloc;
   struct vn_device *dev;
   VkResult result;

   dev = vk_zalloc(alloc, sizeof(*dev), VN_DEFAULT_ALIGN,
                   VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!dev)
      return vn_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_device_dispatch_table dispatch_table;
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &vn_device_entrypoints, true);
   result = vn_device_base_init(&dev->base, &physical_dev->base,
                                &dispatch_table, pCreateInfo, alloc);
   if (result != VK_SUCCESS) {
      vk_free(alloc, dev);
      return vn_error(instance, result);
   }

   dev->instance = instance;
   dev->physical_device = physical_dev;
   dev->renderer = instance->renderer;

   VkDeviceCreateInfo local_create_info;
   pCreateInfo =
      vn_device_fix_create_info(dev, pCreateInfo, alloc, &local_create_info);
   if (!pCreateInfo) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   VkDevice dev_handle = vn_device_to_handle(dev);
   result = vn_call_vkCreateDevice(instance, physicalDevice, pCreateInfo,
                                   NULL, &dev_handle);
   if (result != VK_SUCCESS)
      goto fail;

   result = vn_device_init_queues(dev, pCreateInfo);
   if (result != VK_SUCCESS) {
      vn_call_vkDestroyDevice(instance, dev_handle, NULL);
      goto fail;
   }

   for (uint32_t i = 0; i < ARRAY_SIZE(dev->memory_pools); i++) {
      struct vn_device_memory_pool *pool = &dev->memory_pools[i];
      mtx_init(&pool->mutex, mtx_plain);
   }

   if (dev->base.base.enabled_extensions
          .ANDROID_external_memory_android_hardware_buffer) {
      result = vn_android_init_ahb_buffer_memory_type_bits(dev);
      if (result != VK_SUCCESS) {
         vn_call_vkDestroyDevice(instance, dev_handle, NULL);
         goto fail;
      }
   }

   *pDevice = dev_handle;

   if (pCreateInfo == &local_create_info)
      vk_free(alloc, (void *)pCreateInfo->ppEnabledExtensionNames);

   return VK_SUCCESS;

fail:
   if (pCreateInfo == &local_create_info)
      vk_free(alloc, (void *)pCreateInfo->ppEnabledExtensionNames);
   vn_device_base_fini(&dev->base);
   vk_free(alloc, dev);
   return vn_error(instance, result);
}

void
vn_DestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!dev)
      return;

   for (uint32_t i = 0; i < ARRAY_SIZE(dev->memory_pools); i++)
      vn_device_memory_pool_fini(dev, i);

   for (uint32_t i = 0; i < dev->queue_count; i++)
      vn_queue_fini(&dev->queues[i]);

   /* We must emit vkDestroyDevice before freeing dev->queues.  Otherwise,
    * another thread might reuse their object ids while they still refer to
    * the queues in the renderer.
    */
   vn_async_vkDestroyDevice(dev->instance, device, NULL);

   vk_free(alloc, dev->queues);

   vn_device_base_fini(&dev->base);
   vk_free(alloc, dev);
}

PFN_vkVoidFunction
vn_GetDeviceProcAddr(VkDevice device, const char *pName)
{
   struct vn_device *dev = vn_device_from_handle(device);
   return vk_device_get_proc_addr(&dev->base.base, pName);
}

void
vn_GetDeviceGroupPeerMemoryFeatures(
   VkDevice device,
   uint32_t heapIndex,
   uint32_t localDeviceIndex,
   uint32_t remoteDeviceIndex,
   VkPeerMemoryFeatureFlags *pPeerMemoryFeatures)
{
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO get and cache the values in vkCreateDevice */
   vn_call_vkGetDeviceGroupPeerMemoryFeatures(
      dev->instance, device, heapIndex, localDeviceIndex, remoteDeviceIndex,
      pPeerMemoryFeatures);
}

VkResult
vn_DeviceWaitIdle(VkDevice device)
{
   struct vn_device *dev = vn_device_from_handle(device);

   for (uint32_t i = 0; i < dev->queue_count; i++) {
      struct vn_queue *queue = &dev->queues[i];
      VkResult result = vn_QueueWaitIdle(vn_queue_to_handle(queue));
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);
   }

   return VK_SUCCESS;
}
