/*
 * Copyright © 2017 Intel Corporation
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

#include "wsi_common_private.h"
#include "wsi_common_entrypoints.h"
#include "util/debug.h"
#include "util/macros.h"
#include "util/os_file.h"
#include "util/xmlconfig.h"
#include "vk_device.h"
#include "vk_fence.h"
#include "vk_format.h"
#include "vk_instance.h"
#include "vk_physical_device.h"
#include "vk_queue.h"
#include "vk_semaphore.h"
#include "vk_sync.h"
#include "vk_sync_dummy.h"
#include "vk_util.h"

#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef _WIN32
#include <unistd.h>
#endif

uint64_t WSI_DEBUG;

static const struct debug_control debug_control[] = {
   { "buffer",       WSI_DEBUG_BUFFER },
   { "sw",           WSI_DEBUG_SW },
   { "noshm",        WSI_DEBUG_NOSHM },
   { "linear",       WSI_DEBUG_LINEAR },
   { NULL, },
};

VkResult
wsi_device_init(struct wsi_device *wsi,
                VkPhysicalDevice pdevice,
                WSI_FN_GetPhysicalDeviceProcAddr proc_addr,
                const VkAllocationCallbacks *alloc,
                int display_fd,
                const struct driOptionCache *dri_options,
                bool sw_device)
{
   const char *present_mode;
   UNUSED VkResult result;

   WSI_DEBUG = parse_debug_string(getenv("MESA_VK_WSI_DEBUG"), debug_control);

   util_perfetto_init();

   memset(wsi, 0, sizeof(*wsi));

   wsi->instance_alloc = *alloc;
   wsi->pdevice = pdevice;
   wsi->sw = sw_device || (WSI_DEBUG & WSI_DEBUG_SW);
   wsi->wants_linear = (WSI_DEBUG & WSI_DEBUG_LINEAR) != 0;
#define WSI_GET_CB(func) \
   PFN_vk##func func = (PFN_vk##func)proc_addr(pdevice, "vk" #func)
   WSI_GET_CB(GetPhysicalDeviceExternalSemaphoreProperties);
   WSI_GET_CB(GetPhysicalDeviceProperties2);
   WSI_GET_CB(GetPhysicalDeviceMemoryProperties);
   WSI_GET_CB(GetPhysicalDeviceQueueFamilyProperties);
#undef WSI_GET_CB

   wsi->pci_bus_info.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;
   VkPhysicalDeviceProperties2 pdp2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &wsi->pci_bus_info,
   };
   GetPhysicalDeviceProperties2(pdevice, &pdp2);

   wsi->maxImageDimension2D = pdp2.properties.limits.maxImageDimension2D;
   assert(pdp2.properties.limits.optimalBufferCopyRowPitchAlignment <= UINT32_MAX);
   wsi->optimalBufferCopyRowPitchAlignment =
      pdp2.properties.limits.optimalBufferCopyRowPitchAlignment;
   wsi->override_present_mode = VK_PRESENT_MODE_MAX_ENUM_KHR;

   GetPhysicalDeviceMemoryProperties(pdevice, &wsi->memory_props);
   GetPhysicalDeviceQueueFamilyProperties(pdevice, &wsi->queue_family_count, NULL);

   for (VkExternalSemaphoreHandleTypeFlags handle_type = 1;
        handle_type <= VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        handle_type <<= 1) {
      const VkPhysicalDeviceExternalSemaphoreInfo esi = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
         .handleType = handle_type,
      };
      VkExternalSemaphoreProperties esp = {
         .sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES,
      };
      GetPhysicalDeviceExternalSemaphoreProperties(pdevice, &esi, &esp);

      if (esp.externalSemaphoreFeatures &
          VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT)
         wsi->semaphore_export_handle_types |= handle_type;
   }

   const struct vk_device_extension_table *supported_extensions =
      &vk_physical_device_from_handle(pdevice)->supported_extensions;
   wsi->has_import_memory_host =
      supported_extensions->EXT_external_memory_host;

   list_inithead(&wsi->hotplug_fences);

#define WSI_GET_CB(func) \
   wsi->func = (PFN_vk##func)proc_addr(pdevice, "vk" #func)
   WSI_GET_CB(AllocateMemory);
   WSI_GET_CB(AllocateCommandBuffers);
   WSI_GET_CB(BindBufferMemory);
   WSI_GET_CB(BindImageMemory);
   WSI_GET_CB(BeginCommandBuffer);
   WSI_GET_CB(CmdPipelineBarrier);
   WSI_GET_CB(CmdCopyImageToBuffer);
   WSI_GET_CB(CreateBuffer);
   WSI_GET_CB(CreateCommandPool);
   WSI_GET_CB(CreateFence);
   WSI_GET_CB(CreateImage);
   WSI_GET_CB(CreateSemaphore);
   WSI_GET_CB(DestroyBuffer);
   WSI_GET_CB(DestroyCommandPool);
   WSI_GET_CB(DestroyFence);
   WSI_GET_CB(DestroyImage);
   WSI_GET_CB(DestroySemaphore);
   WSI_GET_CB(EndCommandBuffer);
   WSI_GET_CB(FreeMemory);
   WSI_GET_CB(FreeCommandBuffers);
   WSI_GET_CB(GetBufferMemoryRequirements);
   WSI_GET_CB(GetImageDrmFormatModifierPropertiesEXT);
   WSI_GET_CB(GetImageMemoryRequirements);
   WSI_GET_CB(GetImageSubresourceLayout);
   if (!wsi->sw)
      WSI_GET_CB(GetMemoryFdKHR);
   WSI_GET_CB(GetPhysicalDeviceFormatProperties);
   WSI_GET_CB(GetPhysicalDeviceFormatProperties2KHR);
   WSI_GET_CB(GetPhysicalDeviceImageFormatProperties2);
   WSI_GET_CB(GetSemaphoreFdKHR);
   WSI_GET_CB(ResetFences);
   WSI_GET_CB(QueueSubmit);
   WSI_GET_CB(WaitForFences);
   WSI_GET_CB(MapMemory);
   WSI_GET_CB(UnmapMemory);
#undef WSI_GET_CB

#ifdef VK_USE_PLATFORM_XCB_KHR
   result = wsi_x11_init_wsi(wsi, alloc, dri_options);
   if (result != VK_SUCCESS)
      goto fail;
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   result = wsi_wl_init_wsi(wsi, alloc, pdevice);
   if (result != VK_SUCCESS)
      goto fail;
#endif

#ifdef VK_USE_PLATFORM_WIN32_KHR
   result = wsi_win32_init_wsi(wsi, alloc, pdevice);
   if (result != VK_SUCCESS)
      goto fail;
#endif

#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   result = wsi_display_init_wsi(wsi, alloc, display_fd);
   if (result != VK_SUCCESS)
      goto fail;
#endif

   present_mode = getenv("MESA_VK_WSI_PRESENT_MODE");
   if (present_mode) {
      if (!strcmp(present_mode, "fifo")) {
         wsi->override_present_mode = VK_PRESENT_MODE_FIFO_KHR;
      } else if (!strcmp(present_mode, "relaxed")) {
          wsi->override_present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
      } else if (!strcmp(present_mode, "mailbox")) {
         wsi->override_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
      } else if (!strcmp(present_mode, "immediate")) {
         wsi->override_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
      } else {
         fprintf(stderr, "Invalid MESA_VK_WSI_PRESENT_MODE value!\n");
      }
   }

   if (dri_options) {
      if (driCheckOption(dri_options, "adaptive_sync", DRI_BOOL))
         wsi->enable_adaptive_sync = driQueryOptionb(dri_options,
                                                     "adaptive_sync");

      if (driCheckOption(dri_options, "vk_wsi_force_bgra8_unorm_first",  DRI_BOOL)) {
         wsi->force_bgra8_unorm_first =
            driQueryOptionb(dri_options, "vk_wsi_force_bgra8_unorm_first");
      }
   }

   return VK_SUCCESS;
#if defined(VK_USE_PLATFORM_XCB_KHR) || \
   defined(VK_USE_PLATFORM_WAYLAND_KHR) || \
   defined(VK_USE_PLATFORM_WIN32_KHR) || \
   defined(VK_USE_PLATFORM_DISPLAY_KHR)
fail:
   wsi_device_finish(wsi, alloc);
   return result;
#endif
}

void
wsi_device_finish(struct wsi_device *wsi,
                  const VkAllocationCallbacks *alloc)
{
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   wsi_display_finish_wsi(wsi, alloc);
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   wsi_wl_finish_wsi(wsi, alloc);
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
   wsi_win32_finish_wsi(wsi, alloc);
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
   wsi_x11_finish_wsi(wsi, alloc);
#endif
}

VKAPI_ATTR void VKAPI_CALL
wsi_DestroySurfaceKHR(VkInstance _instance,
                      VkSurfaceKHR _surface,
                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(vk_instance, instance, _instance);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);

   if (!surface)
      return;

   vk_free2(&instance->alloc, pAllocator, surface);
}

void
wsi_device_setup_syncobj_fd(struct wsi_device *wsi_device,
                            int fd)
{
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   wsi_display_setup_syncobj_fd(wsi_device, fd);
#endif
}

VkResult
wsi_swapchain_init(const struct wsi_device *wsi,
                   struct wsi_swapchain *chain,
                   VkDevice _device,
                   const VkSwapchainCreateInfoKHR *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   bool use_buffer_blit)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   VkResult result;

   memset(chain, 0, sizeof(*chain));

   vk_object_base_init(device, &chain->base, VK_OBJECT_TYPE_SWAPCHAIN_KHR);

   chain->wsi = wsi;
   chain->device = _device;
   chain->alloc = *pAllocator;

   chain->use_buffer_blit = use_buffer_blit || (WSI_DEBUG & WSI_DEBUG_BUFFER);
   if (wsi->sw && !wsi->wants_linear)
      chain->use_buffer_blit = true;

   chain->buffer_blit_queue = VK_NULL_HANDLE;
   if (use_buffer_blit && wsi->get_buffer_blit_queue)
      chain->buffer_blit_queue = wsi->get_buffer_blit_queue(_device);

   int cmd_pools_count = chain->buffer_blit_queue != VK_NULL_HANDLE ? 1 : wsi->queue_family_count;

   chain->cmd_pools =
      vk_zalloc(pAllocator, sizeof(VkCommandPool) * cmd_pools_count, 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!chain->cmd_pools)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   for (uint32_t i = 0; i < cmd_pools_count; i++) {
      int queue_family_index = i;

      if (chain->buffer_blit_queue != VK_NULL_HANDLE) {
         VK_FROM_HANDLE(vk_queue, queue, chain->buffer_blit_queue);
         queue_family_index = queue->queue_family_index;
      }
      const VkCommandPoolCreateInfo cmd_pool_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
         .pNext = NULL,
         .flags = 0,
         .queueFamilyIndex = queue_family_index,
      };
      result = wsi->CreateCommandPool(_device, &cmd_pool_info, &chain->alloc,
                                      &chain->cmd_pools[i]);
      if (result != VK_SUCCESS)
         goto fail;
   }

   return VK_SUCCESS;

fail:
   wsi_swapchain_finish(chain);
   return result;
}

static bool
wsi_swapchain_is_present_mode_supported(struct wsi_device *wsi,
                                        const VkSwapchainCreateInfoKHR *pCreateInfo,
                                        VkPresentModeKHR mode)
{
      ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, pCreateInfo->surface);
      struct wsi_interface *iface = wsi->wsi[surface->platform];
      VkPresentModeKHR *present_modes;
      uint32_t present_mode_count;
      bool supported = false;
      VkResult result;

      result = iface->get_present_modes(surface, &present_mode_count, NULL);
      if (result != VK_SUCCESS)
         return supported;

      present_modes = malloc(present_mode_count * sizeof(*present_modes));
      if (!present_modes)
         return supported;

      result = iface->get_present_modes(surface, &present_mode_count,
                                        present_modes);
      if (result != VK_SUCCESS)
         goto fail;

      for (uint32_t i = 0; i < present_mode_count; i++) {
         if (present_modes[i] == mode) {
            supported = true;
            break;
         }
      }

fail:
      free(present_modes);
      return supported;
}

enum VkPresentModeKHR
wsi_swapchain_get_present_mode(struct wsi_device *wsi,
                               const VkSwapchainCreateInfoKHR *pCreateInfo)
{
   if (wsi->override_present_mode == VK_PRESENT_MODE_MAX_ENUM_KHR)
      return pCreateInfo->presentMode;

   if (!wsi_swapchain_is_present_mode_supported(wsi, pCreateInfo,
                                                wsi->override_present_mode)) {
      fprintf(stderr, "Unsupported MESA_VK_WSI_PRESENT_MODE value!\n");
      return pCreateInfo->presentMode;
   }

   return wsi->override_present_mode;
}

void
wsi_swapchain_finish(struct wsi_swapchain *chain)
{
   if (chain->fences) {
      for (unsigned i = 0; i < chain->image_count; i++)
         chain->wsi->DestroyFence(chain->device, chain->fences[i], &chain->alloc);

      vk_free(&chain->alloc, chain->fences);
   }
   if (chain->buffer_blit_semaphores) {
      for (unsigned i = 0; i < chain->image_count; i++)
         chain->wsi->DestroySemaphore(chain->device, chain->buffer_blit_semaphores[i], &chain->alloc);

      vk_free(&chain->alloc, chain->buffer_blit_semaphores);
   }
   chain->wsi->DestroySemaphore(chain->device, chain->dma_buf_semaphore,
                                &chain->alloc);

   int cmd_pools_count = chain->buffer_blit_queue != VK_NULL_HANDLE ?
      1 : chain->wsi->queue_family_count;
   for (uint32_t i = 0; i < cmd_pools_count; i++) {
      chain->wsi->DestroyCommandPool(chain->device, chain->cmd_pools[i],
                                     &chain->alloc);
   }
   vk_free(&chain->alloc, chain->cmd_pools);

   vk_object_base_finish(&chain->base);
}

VkResult
wsi_configure_image(const struct wsi_swapchain *chain,
                    const VkSwapchainCreateInfoKHR *pCreateInfo,
                    VkExternalMemoryHandleTypeFlags handle_types,
                    struct wsi_image_info *info)
{
   memset(info, 0, sizeof(*info));
   uint32_t queue_family_count = 1;

   if (pCreateInfo->imageSharingMode == VK_SHARING_MODE_CONCURRENT)
      queue_family_count = pCreateInfo->queueFamilyIndexCount;

   /*
    * TODO: there should be no reason to allocate this, but
    * 15331 shows that games crashed without doing this.
    */
   uint32_t *queue_family_indices =
      vk_alloc(&chain->alloc,
               sizeof(*queue_family_indices) *
               queue_family_count,
               8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!queue_family_indices)
      goto err_oom;

   if (pCreateInfo->imageSharingMode == VK_SHARING_MODE_CONCURRENT)
      for (uint32_t i = 0; i < pCreateInfo->queueFamilyIndexCount; i++)
         queue_family_indices[i] = pCreateInfo->pQueueFamilyIndices[i];

   info->create = (VkImageCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .flags = VK_IMAGE_CREATE_ALIAS_BIT,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = pCreateInfo->imageFormat,
      .extent = {
         .width = pCreateInfo->imageExtent.width,
         .height = pCreateInfo->imageExtent.height,
         .depth = 1,
      },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = pCreateInfo->imageUsage,
      .sharingMode = pCreateInfo->imageSharingMode,
      .queueFamilyIndexCount = queue_family_count,
      .pQueueFamilyIndices = queue_family_indices,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };

   if (handle_types != 0) {
      info->ext_mem = (VkExternalMemoryImageCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
         .handleTypes = handle_types,
      };
      __vk_append_struct(&info->create, &info->ext_mem);
   }

   info->wsi = (struct wsi_image_create_info) {
      .sType = VK_STRUCTURE_TYPE_WSI_IMAGE_CREATE_INFO_MESA,
   };
   __vk_append_struct(&info->create, &info->wsi);

   if (pCreateInfo->flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) {
      info->create.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
                            VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;

      const VkImageFormatListCreateInfo *format_list_in =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_FORMAT_LIST_CREATE_INFO);

      assume(format_list_in && format_list_in->viewFormatCount > 0);

      const uint32_t view_format_count = format_list_in->viewFormatCount;
      VkFormat *view_formats =
         vk_alloc(&chain->alloc, sizeof(VkFormat) * view_format_count,
                  8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!view_formats)
         goto err_oom;

      ASSERTED bool format_found = false;
      for (uint32_t i = 0; i < format_list_in->viewFormatCount; i++) {
         if (pCreateInfo->imageFormat == format_list_in->pViewFormats[i])
            format_found = true;
         view_formats[i] = format_list_in->pViewFormats[i];
      }
      assert(format_found);

      info->format_list = (VkImageFormatListCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
         .viewFormatCount = view_format_count,
         .pViewFormats = view_formats,
      };
      __vk_append_struct(&info->create, &info->format_list);
   }

   return VK_SUCCESS;

err_oom:
   wsi_destroy_image_info(chain, info);
   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

void
wsi_destroy_image_info(const struct wsi_swapchain *chain,
                       struct wsi_image_info *info)
{
   vk_free(&chain->alloc, (void *)info->create.pQueueFamilyIndices);
   vk_free(&chain->alloc, (void *)info->format_list.pViewFormats);
   vk_free(&chain->alloc, (void *)info->drm_mod_list.pDrmFormatModifiers);
   vk_free(&chain->alloc, info->modifier_props);
}

VkResult
wsi_create_image(const struct wsi_swapchain *chain,
                 const struct wsi_image_info *info,
                 struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   memset(image, 0, sizeof(*image));

#ifndef _WIN32
   image->dma_buf_fd = -1;
#endif

   result = wsi->CreateImage(chain->device, &info->create,
                             &chain->alloc, &image->image);
   if (result != VK_SUCCESS)
      goto fail;

   result = info->create_mem(chain, info, image);
   if (result != VK_SUCCESS)
      goto fail;

   result = wsi->BindImageMemory(chain->device, image->image,
                                 image->memory, 0);
   if (result != VK_SUCCESS)
      goto fail;

   if (info->finish_create) {
      result = info->finish_create(chain, info, image);
      if (result != VK_SUCCESS)
         goto fail;
   }

   return VK_SUCCESS;

fail:
   wsi_destroy_image(chain, image);
   return result;
}

void
wsi_destroy_image(const struct wsi_swapchain *chain,
                  struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;

#ifndef _WIN32
   if (image->dma_buf_fd >= 0)
      close(image->dma_buf_fd);
#endif

   if (image->cpu_map != NULL) {
      wsi->UnmapMemory(chain->device, image->buffer.buffer != VK_NULL_HANDLE ?
                                      image->buffer.memory : image->memory);
   }

   if (image->buffer.blit_cmd_buffers) {
      int cmd_buffer_count =
         chain->buffer_blit_queue != VK_NULL_HANDLE ? 1 : wsi->queue_family_count;

      for (uint32_t i = 0; i < cmd_buffer_count; i++) {
         wsi->FreeCommandBuffers(chain->device, chain->cmd_pools[i],
                                 1, &image->buffer.blit_cmd_buffers[i]);
      }
      vk_free(&chain->alloc, image->buffer.blit_cmd_buffers);
   }

   wsi->FreeMemory(chain->device, image->memory, &chain->alloc);
   wsi->DestroyImage(chain->device, image->image, &chain->alloc);
   wsi->FreeMemory(chain->device, image->buffer.memory, &chain->alloc);
   wsi->DestroyBuffer(chain->device, image->buffer.buffer, &chain->alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                       uint32_t queueFamilyIndex,
                                       VkSurfaceKHR _surface,
                                       VkBool32 *pSupported)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   return iface->get_support(surface, wsi_device,
                             queueFamilyIndex, pSupported);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfaceCapabilitiesKHR(
   VkPhysicalDevice physicalDevice,
   VkSurfaceKHR _surface,
   VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   VkSurfaceCapabilities2KHR caps2 = {
      .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
   };

   VkResult result = iface->get_capabilities2(surface, wsi_device, NULL, &caps2);

   if (result == VK_SUCCESS)
      *pSurfaceCapabilities = caps2.surfaceCapabilities;

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfaceCapabilities2KHR(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
   VkSurfaceCapabilities2KHR *pSurfaceCapabilities)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, pSurfaceInfo->surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   return iface->get_capabilities2(surface, wsi_device, pSurfaceInfo->pNext,
                                   pSurfaceCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfaceCapabilities2EXT(
   VkPhysicalDevice physicalDevice,
   VkSurfaceKHR _surface,
   VkSurfaceCapabilities2EXT *pSurfaceCapabilities)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   assert(pSurfaceCapabilities->sType ==
          VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_EXT);

   struct wsi_surface_supported_counters counters = {
      .sType = VK_STRUCTURE_TYPE_WSI_SURFACE_SUPPORTED_COUNTERS_MESA,
      .pNext = pSurfaceCapabilities->pNext,
      .supported_surface_counters = 0,
   };

   VkSurfaceCapabilities2KHR caps2 = {
      .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
      .pNext = &counters,
   };

   VkResult result = iface->get_capabilities2(surface, wsi_device, NULL, &caps2);

   if (result == VK_SUCCESS) {
      VkSurfaceCapabilities2EXT *ext_caps = pSurfaceCapabilities;
      VkSurfaceCapabilitiesKHR khr_caps = caps2.surfaceCapabilities;

      ext_caps->minImageCount = khr_caps.minImageCount;
      ext_caps->maxImageCount = khr_caps.maxImageCount;
      ext_caps->currentExtent = khr_caps.currentExtent;
      ext_caps->minImageExtent = khr_caps.minImageExtent;
      ext_caps->maxImageExtent = khr_caps.maxImageExtent;
      ext_caps->maxImageArrayLayers = khr_caps.maxImageArrayLayers;
      ext_caps->supportedTransforms = khr_caps.supportedTransforms;
      ext_caps->currentTransform = khr_caps.currentTransform;
      ext_caps->supportedCompositeAlpha = khr_caps.supportedCompositeAlpha;
      ext_caps->supportedUsageFlags = khr_caps.supportedUsageFlags;
      ext_caps->supportedSurfaceCounters = counters.supported_surface_counters;
   }

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice,
                                       VkSurfaceKHR _surface,
                                       uint32_t *pSurfaceFormatCount,
                                       VkSurfaceFormatKHR *pSurfaceFormats)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   return iface->get_formats(surface, wsi_device,
                             pSurfaceFormatCount, pSurfaceFormats);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice,
                                        const VkPhysicalDeviceSurfaceInfo2KHR * pSurfaceInfo,
                                        uint32_t *pSurfaceFormatCount,
                                        VkSurfaceFormat2KHR *pSurfaceFormats)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, pSurfaceInfo->surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   return iface->get_formats2(surface, wsi_device, pSurfaceInfo->pNext,
                              pSurfaceFormatCount, pSurfaceFormats);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice,
                                            VkSurfaceKHR _surface,
                                            uint32_t *pPresentModeCount,
                                            VkPresentModeKHR *pPresentModes)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   return iface->get_present_modes(surface, pPresentModeCount,
                                   pPresentModes);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice,
                                          VkSurfaceKHR _surface,
                                          uint32_t *pRectCount,
                                          VkRect2D *pRects)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   return iface->get_present_rectangles(surface, wsi_device,
                                        pRectCount, pRects);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_CreateSwapchainKHR(VkDevice _device,
                       const VkSwapchainCreateInfoKHR *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkSwapchainKHR *pSwapchain)
{
   MESA_TRACE_FUNC();
   VK_FROM_HANDLE(vk_device, device, _device);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, pCreateInfo->surface);
   struct wsi_device *wsi_device = device->physical->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];
   const VkAllocationCallbacks *alloc;
   struct wsi_swapchain *swapchain;

   if (pAllocator)
     alloc = pAllocator;
   else
     alloc = &device->alloc;

   VkResult result = iface->create_swapchain(surface, _device, wsi_device,
                                             pCreateInfo, alloc,
                                             &swapchain);
   if (result != VK_SUCCESS)
      return result;

   swapchain->fences = vk_zalloc(alloc,
                                 sizeof (*swapchain->fences) * swapchain->image_count,
                                 sizeof (*swapchain->fences),
                                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!swapchain->fences) {
      swapchain->destroy(swapchain, alloc);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   if (swapchain->buffer_blit_queue != VK_NULL_HANDLE) {
      swapchain->buffer_blit_semaphores = vk_zalloc(alloc,
                                         sizeof (*swapchain->buffer_blit_semaphores) * swapchain->image_count,
                                         sizeof (*swapchain->buffer_blit_semaphores),
                                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!swapchain->buffer_blit_semaphores) {
         swapchain->destroy(swapchain, alloc);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   *pSwapchain = wsi_swapchain_to_handle(swapchain);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
wsi_DestroySwapchainKHR(VkDevice _device,
                        VkSwapchainKHR _swapchain,
                        const VkAllocationCallbacks *pAllocator)
{
   MESA_TRACE_FUNC();
   VK_FROM_HANDLE(vk_device, device, _device);
   VK_FROM_HANDLE(wsi_swapchain, swapchain, _swapchain);
   const VkAllocationCallbacks *alloc;

   if (!swapchain)
      return;

   if (pAllocator)
     alloc = pAllocator;
   else
     alloc = &device->alloc;

   swapchain->destroy(swapchain, alloc);
}

VkResult
wsi_common_get_images(VkSwapchainKHR _swapchain,
                      uint32_t *pSwapchainImageCount,
                      VkImage *pSwapchainImages)
{
   VK_FROM_HANDLE(wsi_swapchain, swapchain, _swapchain);
   VK_OUTARRAY_MAKE_TYPED(VkImage, images, pSwapchainImages, pSwapchainImageCount);

   for (uint32_t i = 0; i < swapchain->image_count; i++) {
      vk_outarray_append_typed(VkImage, &images, image) {
         *image = swapchain->get_wsi_image(swapchain, i)->image;
      }
   }

   return vk_outarray_status(&images);
}

VkImage
wsi_common_get_image(VkSwapchainKHR _swapchain, uint32_t index)
{
   VK_FROM_HANDLE(wsi_swapchain, swapchain, _swapchain);
   assert(index < swapchain->image_count);
   return swapchain->get_wsi_image(swapchain, index)->image;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetSwapchainImagesKHR(VkDevice device,
                          VkSwapchainKHR swapchain,
                          uint32_t *pSwapchainImageCount,
                          VkImage *pSwapchainImages)
{
   MESA_TRACE_FUNC();
   return wsi_common_get_images(swapchain,
                                pSwapchainImageCount,
                                pSwapchainImages);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_AcquireNextImageKHR(VkDevice _device,
                        VkSwapchainKHR swapchain,
                        uint64_t timeout,
                        VkSemaphore semaphore,
                        VkFence fence,
                        uint32_t *pImageIndex)
{
   MESA_TRACE_FUNC();
   VK_FROM_HANDLE(vk_device, device, _device);

   const VkAcquireNextImageInfoKHR acquire_info = {
      .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
      .swapchain = swapchain,
      .timeout = timeout,
      .semaphore = semaphore,
      .fence = fence,
      .deviceMask = 0,
   };

   return device->dispatch_table.AcquireNextImage2KHR(_device, &acquire_info,
                                                      pImageIndex);
}

static VkResult
wsi_signal_semaphore_for_image(struct vk_device *device,
                               const struct wsi_swapchain *chain,
                               const struct wsi_image *image,
                               VkSemaphore _semaphore)
{
   if (device->physical->supported_sync_types == NULL)
      return VK_SUCCESS;

   VK_FROM_HANDLE(vk_semaphore, semaphore, _semaphore);

   vk_semaphore_reset_temporary(device, semaphore);

#ifdef HAVE_LIBDRM
   VkResult result = wsi_create_sync_for_dma_buf_wait(chain, image,
                                                      VK_SYNC_FEATURE_GPU_WAIT,
                                                      &semaphore->temporary);
   if (result != VK_ERROR_FEATURE_NOT_PRESENT)
      return result;
#endif

   if (chain->wsi->signal_semaphore_with_memory) {
      return device->create_sync_for_memory(device, image->memory,
                                            false /* signal_memory */,
                                            &semaphore->temporary);
   } else {
      return vk_sync_create(device, &vk_sync_dummy_type,
                            0 /* flags */, 0 /* initial_value */,
                            &semaphore->temporary);
   }
}

static VkResult
wsi_signal_fence_for_image(struct vk_device *device,
                           const struct wsi_swapchain *chain,
                           const struct wsi_image *image,
                           VkFence _fence)
{
   if (device->physical->supported_sync_types == NULL)
      return VK_SUCCESS;

   VK_FROM_HANDLE(vk_fence, fence, _fence);

   vk_fence_reset_temporary(device, fence);

#ifdef HAVE_LIBDRM
   VkResult result = wsi_create_sync_for_dma_buf_wait(chain, image,
                                                      VK_SYNC_FEATURE_CPU_WAIT,
                                                      &fence->temporary);
   if (result != VK_ERROR_FEATURE_NOT_PRESENT)
      return result;
#endif

   if (chain->wsi->signal_fence_with_memory) {
      return device->create_sync_for_memory(device, image->memory,
                                            false /* signal_memory */,
                                            &fence->temporary);
   } else {
      return vk_sync_create(device, &vk_sync_dummy_type,
                            0 /* flags */, 0 /* initial_value */,
                            &fence->temporary);
   }
}

VkResult
wsi_common_acquire_next_image2(const struct wsi_device *wsi,
                               VkDevice _device,
                               const VkAcquireNextImageInfoKHR *pAcquireInfo,
                               uint32_t *pImageIndex)
{
   VK_FROM_HANDLE(wsi_swapchain, swapchain, pAcquireInfo->swapchain);
   VK_FROM_HANDLE(vk_device, device, _device);

   VkResult result = swapchain->acquire_next_image(swapchain, pAcquireInfo,
                                                   pImageIndex);
   if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
      return result;
   struct wsi_image *image =
      swapchain->get_wsi_image(swapchain, *pImageIndex);

   if (pAcquireInfo->semaphore != VK_NULL_HANDLE) {
      VkResult signal_result =
         wsi_signal_semaphore_for_image(device, swapchain, image,
                                        pAcquireInfo->semaphore);
      if (signal_result != VK_SUCCESS)
         return signal_result;
   }

   if (pAcquireInfo->fence != VK_NULL_HANDLE) {
      VkResult signal_result =
         wsi_signal_fence_for_image(device, swapchain, image,
                                    pAcquireInfo->fence);
      if (signal_result != VK_SUCCESS)
         return signal_result;
   }

   if (wsi->set_memory_ownership)
      wsi->set_memory_ownership(swapchain->device, image->memory, true);

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_AcquireNextImage2KHR(VkDevice _device,
                         const VkAcquireNextImageInfoKHR *pAcquireInfo,
                         uint32_t *pImageIndex)
{
   MESA_TRACE_FUNC();
   VK_FROM_HANDLE(vk_device, device, _device);

   return wsi_common_acquire_next_image2(device->physical->wsi_device,
                                         _device, pAcquireInfo, pImageIndex);
}

VkResult
wsi_common_queue_present(const struct wsi_device *wsi,
                         VkDevice device,
                         VkQueue queue,
                         int queue_family_index,
                         const VkPresentInfoKHR *pPresentInfo)
{
   VkResult final_result = VK_SUCCESS;

   STACK_ARRAY(VkPipelineStageFlags, stage_flags,
               MAX2(1, pPresentInfo->waitSemaphoreCount));
   for (uint32_t s = 0; s < MAX2(1, pPresentInfo->waitSemaphoreCount); s++)
      stage_flags[s] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

   const VkPresentRegionsKHR *regions =
      vk_find_struct_const(pPresentInfo->pNext, PRESENT_REGIONS_KHR);

   for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
      VK_FROM_HANDLE(wsi_swapchain, swapchain, pPresentInfo->pSwapchains[i]);
      uint32_t image_index = pPresentInfo->pImageIndices[i];
      VkResult result;

      if (swapchain->fences[image_index] == VK_NULL_HANDLE) {
         const VkFenceCreateInfo fence_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = NULL,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
         };
         result = wsi->CreateFence(device, &fence_info,
                                   &swapchain->alloc,
                                   &swapchain->fences[image_index]);
         if (result != VK_SUCCESS)
            goto fail_present;

         if (swapchain->use_buffer_blit && swapchain->buffer_blit_queue != VK_NULL_HANDLE) {
            const VkSemaphoreCreateInfo sem_info = {
               .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
               .pNext = NULL,
               .flags = 0,
            };
            result = wsi->CreateSemaphore(device, &sem_info,
                                          &swapchain->alloc,
                                          &swapchain->buffer_blit_semaphores[image_index]);
            if (result != VK_SUCCESS)
               goto fail_present;
         }
      } else {
         MESA_TRACE_SCOPE("throttle");
         result =
            wsi->WaitForFences(device, 1, &swapchain->fences[image_index],
                               true, ~0ull);
         if (result != VK_SUCCESS)
            goto fail_present;
      }

      result = wsi->ResetFences(device, 1, &swapchain->fences[image_index]);
      if (result != VK_SUCCESS)
         goto fail_present;

      VkSubmitInfo submit_info = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      };

      if (i == 0) {
         /* We only need/want to wait on semaphores once.  After that, we're
          * guaranteed ordering since it all happens on the same queue.
          */
         submit_info.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
         submit_info.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
         submit_info.pWaitDstStageMask = stage_flags;
      }

      struct wsi_image *image =
         swapchain->get_wsi_image(swapchain, image_index);

      VkQueue submit_queue = queue;
      if (swapchain->use_buffer_blit) {
         if (swapchain->buffer_blit_queue == VK_NULL_HANDLE) {
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers =
               &image->buffer.blit_cmd_buffers[queue_family_index];
         } else {
            /* If we are using a blit using the driver's private queue, then
             * do an empty submit signalling a semaphore, and then submit the
             * blit waiting on that.  This ensures proper queue ordering of
             * vkQueueSubmit() calls.
             */
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores =
               &swapchain->buffer_blit_semaphores[image_index];

            result = wsi->QueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
            if (result != VK_SUCCESS)
               goto fail_present;

            /* Now prepare the blit submit.  It needs to then wait on the
             * semaphore we signaled above.
             */
            submit_queue = swapchain->buffer_blit_queue;
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = submit_info.pSignalSemaphores;
            submit_info.signalSemaphoreCount = 0;
            submit_info.pSignalSemaphores = NULL;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &image->buffer.blit_cmd_buffers[0];
            submit_info.pWaitDstStageMask = stage_flags;
         }
      }

      VkFence fence = swapchain->fences[image_index];

      bool has_signal_dma_buf = false;
#ifdef HAVE_LIBDRM
      result = wsi_prepare_signal_dma_buf_from_semaphore(swapchain, image);
      if (result == VK_SUCCESS) {
         assert(submit_info.signalSemaphoreCount == 0);
         submit_info.signalSemaphoreCount = 1;
         submit_info.pSignalSemaphores = &swapchain->dma_buf_semaphore;
         has_signal_dma_buf = true;
      } else if (result == VK_ERROR_FEATURE_NOT_PRESENT) {
         result = VK_SUCCESS;
         has_signal_dma_buf = false;
      } else {
         goto fail_present;
      }
#endif

      struct wsi_memory_signal_submit_info mem_signal;
      if (!has_signal_dma_buf) {
         /* If we don't have dma-buf signaling, signal the memory object by
          * chaining wsi_memory_signal_submit_info into VkSubmitInfo.
          */
         result = VK_SUCCESS;
         has_signal_dma_buf = false;
         mem_signal = (struct wsi_memory_signal_submit_info) {
            .sType = VK_STRUCTURE_TYPE_WSI_MEMORY_SIGNAL_SUBMIT_INFO_MESA,
            .memory = image->memory,
         };
         __vk_append_struct(&submit_info, &mem_signal);
      }

      result = wsi->QueueSubmit(submit_queue, 1, &submit_info, fence);
      if (result != VK_SUCCESS)
         goto fail_present;

#ifdef HAVE_LIBDRM
      if (has_signal_dma_buf) {
         result = wsi_signal_dma_buf_from_semaphore(swapchain, image);
         if (result != VK_SUCCESS)
            goto fail_present;
      }
#else
      assert(!has_signal_dma_buf);
#endif

      if (wsi->sw)
	      wsi->WaitForFences(device, 1, &swapchain->fences[image_index],
				 true, ~0ull);

      const VkPresentRegionKHR *region = NULL;
      if (regions && regions->pRegions)
         region = &regions->pRegions[i];

      result = swapchain->queue_present(swapchain, image_index, region);
      if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
         goto fail_present;

      if (wsi->set_memory_ownership) {
         VkDeviceMemory mem = swapchain->get_wsi_image(swapchain, image_index)->memory;
         wsi->set_memory_ownership(swapchain->device, mem, false);
      }

   fail_present:
      if (pPresentInfo->pResults != NULL)
         pPresentInfo->pResults[i] = result;

      /* Let the final result be our first unsuccessful result */
      if (final_result == VK_SUCCESS)
         final_result = result;
   }

   STACK_ARRAY_FINISH(stage_flags);

   return final_result;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_QueuePresentKHR(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   MESA_TRACE_FUNC();
   VK_FROM_HANDLE(vk_queue, queue, _queue);

   return wsi_common_queue_present(queue->base.device->physical->wsi_device,
                                   vk_device_to_handle(queue->base.device),
                                   _queue,
                                   queue->queue_family_index,
                                   pPresentInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetDeviceGroupPresentCapabilitiesKHR(VkDevice device,
                                         VkDeviceGroupPresentCapabilitiesKHR *pCapabilities)
{
   memset(pCapabilities->presentMask, 0,
          sizeof(pCapabilities->presentMask));
   pCapabilities->presentMask[0] = 0x1;
   pCapabilities->modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetDeviceGroupSurfacePresentModesKHR(VkDevice device,
                                         VkSurfaceKHR surface,
                                         VkDeviceGroupPresentModeFlagsKHR *pModes)
{
   *pModes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VkResult
wsi_common_create_swapchain_image(const struct wsi_device *wsi,
                                  const VkImageCreateInfo *pCreateInfo,
                                  VkSwapchainKHR _swapchain,
                                  VkImage *pImage)
{
   VK_FROM_HANDLE(wsi_swapchain, chain, _swapchain);

#ifndef NDEBUG
   const VkImageCreateInfo *swcInfo = &chain->image_info.create;
   assert(pCreateInfo->flags == 0);
   assert(pCreateInfo->imageType == swcInfo->imageType);
   assert(pCreateInfo->format == swcInfo->format);
   assert(pCreateInfo->extent.width == swcInfo->extent.width);
   assert(pCreateInfo->extent.height == swcInfo->extent.height);
   assert(pCreateInfo->extent.depth == swcInfo->extent.depth);
   assert(pCreateInfo->mipLevels == swcInfo->mipLevels);
   assert(pCreateInfo->arrayLayers == swcInfo->arrayLayers);
   assert(pCreateInfo->samples == swcInfo->samples);
   assert(pCreateInfo->tiling == VK_IMAGE_TILING_OPTIMAL);
   assert(!(pCreateInfo->usage & ~swcInfo->usage));

   vk_foreach_struct_const(ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO: {
         const VkImageFormatListCreateInfo *iflci =
            (const VkImageFormatListCreateInfo *)ext;
         const VkImageFormatListCreateInfo *swc_iflci =
            &chain->image_info.format_list;

         for (uint32_t i = 0; i < iflci->viewFormatCount; i++) {
            bool found = false;
            for (uint32_t j = 0; j < swc_iflci->viewFormatCount; j++) {
               if (iflci->pViewFormats[i] == swc_iflci->pViewFormats[j]) {
                  found = true;
                  break;
               }
            }
            assert(found);
         }
         break;
      }

      case VK_STRUCTURE_TYPE_IMAGE_SWAPCHAIN_CREATE_INFO_KHR:
         break;

      default:
         assert(!"Unsupported image create extension");
      }
   }
#endif

   return wsi->CreateImage(chain->device, &chain->image_info.create,
                           &chain->alloc, pImage);
}

VkResult
wsi_common_bind_swapchain_image(const struct wsi_device *wsi,
                                VkImage vk_image,
                                VkSwapchainKHR _swapchain,
                                uint32_t image_idx)
{
   VK_FROM_HANDLE(wsi_swapchain, chain, _swapchain);
   struct wsi_image *image = chain->get_wsi_image(chain, image_idx);

   return wsi->BindImageMemory(chain->device, vk_image, image->memory, 0);
}

uint32_t
wsi_select_memory_type(const struct wsi_device *wsi,
                       VkMemoryPropertyFlags req_props,
                       VkMemoryPropertyFlags deny_props,
                       uint32_t type_bits)
{
   assert(type_bits != 0);

   VkMemoryPropertyFlags common_props = ~0;
   u_foreach_bit(t, type_bits) {
      const VkMemoryType type = wsi->memory_props.memoryTypes[t];

      common_props &= type.propertyFlags;

      if (deny_props & type.propertyFlags)
         continue;

      if (!(req_props & ~type.propertyFlags))
         return t;
   }

   if ((deny_props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
       (common_props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      /* If they asked for non-device-local and all the types are device-local
       * (this is commonly true for UMA platforms), try again without denying
       * device-local types
       */
      deny_props &= ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      return wsi_select_memory_type(wsi, req_props, deny_props, type_bits);
   }

   unreachable("No memory type found");
}

uint32_t
wsi_select_device_memory_type(const struct wsi_device *wsi,
                              uint32_t type_bits)
{
   return wsi_select_memory_type(wsi, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 0 /* deny_props */, type_bits);
}

static uint32_t
wsi_select_host_memory_type(const struct wsi_device *wsi,
                            uint32_t type_bits)
{
   return wsi_select_memory_type(wsi, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 0 /* deny_props */, type_bits);
}

VkResult
wsi_create_buffer_image_mem(const struct wsi_swapchain *chain,
                            const struct wsi_image_info *info,
                            struct wsi_image *image,
                            VkExternalMemoryHandleTypeFlags handle_types,
                            bool implicit_sync)
{
   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   const VkExternalMemoryBufferCreateInfo buffer_external_info = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
      .pNext = NULL,
      .handleTypes = handle_types,
   };
   const VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = &buffer_external_info,
      .size = info->linear_size,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   result = wsi->CreateBuffer(chain->device, &buffer_info,
                              &chain->alloc, &image->buffer.buffer);
   if (result != VK_SUCCESS)
      return result;

   VkMemoryRequirements reqs;
   wsi->GetBufferMemoryRequirements(chain->device, image->buffer.buffer, &reqs);
   assert(reqs.size <= info->linear_size);

   struct wsi_memory_allocate_info memory_wsi_info = {
      .sType = VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA,
      .pNext = NULL,
      .implicit_sync = implicit_sync,
   };
   VkMemoryDedicatedAllocateInfo buf_mem_dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = &memory_wsi_info,
      .image = VK_NULL_HANDLE,
      .buffer = image->buffer.buffer,
   };
   VkMemoryAllocateInfo buf_mem_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &buf_mem_dedicated_info,
      .allocationSize = info->linear_size,
      .memoryTypeIndex =
         info->select_buffer_memory_type(wsi, reqs.memoryTypeBits),
   };

   void *sw_host_ptr = NULL;
   if (info->alloc_shm)
      sw_host_ptr = info->alloc_shm(image, info->linear_size);

   VkExportMemoryAllocateInfo memory_export_info;
   VkImportMemoryHostPointerInfoEXT host_ptr_info;
   if (sw_host_ptr != NULL) {
      host_ptr_info = (VkImportMemoryHostPointerInfoEXT) {
         .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
         .pHostPointer = sw_host_ptr,
         .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
      };
      __vk_append_struct(&buf_mem_info, &host_ptr_info);
   } else if (handle_types != 0) {
      memory_export_info = (VkExportMemoryAllocateInfo) {
         .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
         .handleTypes = handle_types,
      };
      __vk_append_struct(&buf_mem_info, &memory_export_info);
   }

   result = wsi->AllocateMemory(chain->device, &buf_mem_info,
                                &chain->alloc, &image->buffer.memory);
   if (result != VK_SUCCESS)
      return result;

   result = wsi->BindBufferMemory(chain->device, image->buffer.buffer,
                                  image->buffer.memory, 0);
   if (result != VK_SUCCESS)
      return result;

   wsi->GetImageMemoryRequirements(chain->device, image->image, &reqs);

   const VkMemoryDedicatedAllocateInfo memory_dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = NULL,
      .image = image->image,
      .buffer = VK_NULL_HANDLE,
   };
   const VkMemoryAllocateInfo memory_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &memory_dedicated_info,
      .allocationSize = reqs.size,
      .memoryTypeIndex =
         info->select_image_memory_type(wsi, reqs.memoryTypeBits),
   };

   result = wsi->AllocateMemory(chain->device, &memory_info,
                                &chain->alloc, &image->memory);
   if (result != VK_SUCCESS)
      return result;

   image->num_planes = 1;
   image->sizes[0] = info->linear_size;
   image->row_pitches[0] = info->linear_stride;
   image->offsets[0] = 0;

   return VK_SUCCESS;
}

VkResult
wsi_finish_create_buffer_image(const struct wsi_swapchain *chain,
                               const struct wsi_image_info *info,
                               struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   int cmd_buffer_count =
      chain->buffer_blit_queue != VK_NULL_HANDLE ? 1 : wsi->queue_family_count;
   image->buffer.blit_cmd_buffers =
      vk_zalloc(&chain->alloc,
                sizeof(VkCommandBuffer) * cmd_buffer_count, 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!image->buffer.blit_cmd_buffers)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   for (uint32_t i = 0; i < cmd_buffer_count; i++) {
      const VkCommandBufferAllocateInfo cmd_buffer_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
         .pNext = NULL,
         .commandPool = chain->cmd_pools[i],
         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
         .commandBufferCount = 1,
      };
      result = wsi->AllocateCommandBuffers(chain->device, &cmd_buffer_info,
                                           &image->buffer.blit_cmd_buffers[i]);
      if (result != VK_SUCCESS)
         return result;

      const VkCommandBufferBeginInfo begin_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      wsi->BeginCommandBuffer(image->buffer.blit_cmd_buffers[i], &begin_info);

      VkImageMemoryBarrier img_mem_barrier = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .pNext = NULL,
         .srcAccessMask = 0,
         .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = image->image,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      };
      wsi->CmdPipelineBarrier(image->buffer.blit_cmd_buffers[i],
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0,
                              0, NULL,
                              0, NULL,
                              1, &img_mem_barrier);

      struct VkBufferImageCopy buffer_image_copy = {
         .bufferOffset = 0,
         .bufferRowLength = info->linear_stride /
                            vk_format_get_blocksize(info->create.format),
         .bufferImageHeight = 0,
         .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
         .imageOffset = { .x = 0, .y = 0, .z = 0 },
         .imageExtent = info->create.extent,
      };
      wsi->CmdCopyImageToBuffer(image->buffer.blit_cmd_buffers[i],
                                image->image,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                image->buffer.buffer,
                                1, &buffer_image_copy);

      img_mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      img_mem_barrier.dstAccessMask = 0;
      img_mem_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      img_mem_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      wsi->CmdPipelineBarrier(image->buffer.blit_cmd_buffers[i],
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                              0,
                              0, NULL,
                              0, NULL,
                              1, &img_mem_barrier);

      result = wsi->EndCommandBuffer(image->buffer.blit_cmd_buffers[i]);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

VkResult
wsi_configure_buffer_image(UNUSED const struct wsi_swapchain *chain,
                           const VkSwapchainCreateInfoKHR *pCreateInfo,
                           uint32_t stride_align, uint32_t size_align,
                           struct wsi_image_info *info)
{
   const struct wsi_device *wsi = chain->wsi;

   assert(util_is_power_of_two_nonzero(stride_align));
   assert(util_is_power_of_two_nonzero(size_align));

   VkResult result = wsi_configure_image(chain, pCreateInfo,
                                         0 /* handle_types */, info);
   if (result != VK_SUCCESS)
      return result;

   info->create.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
   info->wsi.buffer_blit_src = true;

   const uint32_t cpp = vk_format_get_blocksize(pCreateInfo->imageFormat);
   info->linear_stride = pCreateInfo->imageExtent.width * cpp;
   info->linear_stride = ALIGN_POT(info->linear_stride, stride_align);

   /* Since we can pick the stride to be whatever we want, also align to the
    * device's optimalBufferCopyRowPitchAlignment so we get efficient copies.
    */
   assert(wsi->optimalBufferCopyRowPitchAlignment > 0);
   info->linear_stride = ALIGN_POT(info->linear_stride,
                                   wsi->optimalBufferCopyRowPitchAlignment);

   info->linear_size = info->linear_stride * pCreateInfo->imageExtent.height;
   info->linear_size = ALIGN_POT(info->linear_size, size_align);

   info->finish_create = wsi_finish_create_buffer_image;

   return VK_SUCCESS;
}

static VkResult
wsi_create_cpu_linear_image_mem(const struct wsi_swapchain *chain,
                                const struct wsi_image_info *info,
                                struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   VkMemoryRequirements reqs;
   wsi->GetImageMemoryRequirements(chain->device, image->image, &reqs);

   VkSubresourceLayout layout;
   wsi->GetImageSubresourceLayout(chain->device, image->image,
                                  &(VkImageSubresource) {
                                     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .mipLevel = 0,
                                     .arrayLayer = 0,
                                  }, &layout);
   assert(layout.offset == 0);

   const VkMemoryDedicatedAllocateInfo memory_dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .image = image->image,
      .buffer = VK_NULL_HANDLE,
   };
   VkMemoryAllocateInfo memory_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &memory_dedicated_info,
      .allocationSize = reqs.size,
      .memoryTypeIndex =
         wsi_select_host_memory_type(wsi, reqs.memoryTypeBits),
   };

   void *sw_host_ptr = NULL;
   if (info->alloc_shm)
      sw_host_ptr = info->alloc_shm(image, layout.size);

   VkImportMemoryHostPointerInfoEXT host_ptr_info;
   if (sw_host_ptr != NULL) {
      host_ptr_info = (VkImportMemoryHostPointerInfoEXT) {
         .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
         .pHostPointer = sw_host_ptr,
         .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
      };
      __vk_append_struct(&memory_info, &host_ptr_info);
   }

   result = wsi->AllocateMemory(chain->device, &memory_info,
                                &chain->alloc, &image->memory);
   if (result != VK_SUCCESS)
      return result;

   result = wsi->MapMemory(chain->device, image->memory,
                           0, VK_WHOLE_SIZE, 0, &image->cpu_map);
   if (result != VK_SUCCESS)
      return result;

   image->num_planes = 1;
   image->sizes[0] = reqs.size;
   image->row_pitches[0] = layout.rowPitch;
   image->offsets[0] = 0;

   return VK_SUCCESS;
}

static VkResult
wsi_create_cpu_buffer_image_mem(const struct wsi_swapchain *chain,
                                const struct wsi_image_info *info,
                                struct wsi_image *image)
{
   VkResult result;

   result = wsi_create_buffer_image_mem(chain, info, image, 0,
                                        false /* implicit_sync */);
   if (result != VK_SUCCESS)
      return result;

   result = chain->wsi->MapMemory(chain->device, image->buffer.memory,
                                  0, VK_WHOLE_SIZE, 0, &image->cpu_map);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

VkResult
wsi_configure_cpu_image(const struct wsi_swapchain *chain,
                        const VkSwapchainCreateInfoKHR *pCreateInfo,
                        uint8_t *(alloc_shm)(struct wsi_image *image,
                                             unsigned size),
                        struct wsi_image_info *info)
{
   const VkExternalMemoryHandleTypeFlags handle_types =
      alloc_shm ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT : 0;

   if (chain->use_buffer_blit) {
      VkResult result = wsi_configure_buffer_image(chain, pCreateInfo,
                                                   1 /* stride_align */,
                                                   1 /* size_align */,
                                                   info);
      if (result != VK_SUCCESS)
         return result;

      info->select_buffer_memory_type = wsi_select_host_memory_type;
      info->select_image_memory_type = wsi_select_device_memory_type;
      info->create_mem = wsi_create_cpu_buffer_image_mem;
   } else {
      VkResult result = wsi_configure_image(chain, pCreateInfo,
                                            handle_types, info);
      if (result != VK_SUCCESS)
         return result;

      /* Force the image to be linear */
      info->create.tiling = VK_IMAGE_TILING_LINEAR;

      info->create_mem = wsi_create_cpu_linear_image_mem;
   }

   info->alloc_shm = alloc_shm;

   return VK_SUCCESS;
}
