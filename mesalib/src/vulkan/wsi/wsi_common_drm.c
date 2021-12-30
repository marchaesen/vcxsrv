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
#include "util/macros.h"
#include "util/os_file.h"
#include "util/xmlconfig.h"
#include "vk_util.h"
#include "drm-uapi/drm_fourcc.h"

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <xf86drm.h>

bool
wsi_device_matches_drm_fd(const struct wsi_device *wsi, int drm_fd)
{
   if (wsi->can_present_on_device)
      return wsi->can_present_on_device(wsi->pdevice, drm_fd);

   drmDevicePtr fd_device;
   int ret = drmGetDevice2(drm_fd, 0, &fd_device);
   if (ret)
      return false;

   bool match = false;
   switch (fd_device->bustype) {
   case DRM_BUS_PCI:
      match = wsi->pci_bus_info.pciDomain == fd_device->businfo.pci->domain &&
              wsi->pci_bus_info.pciBus == fd_device->businfo.pci->bus &&
              wsi->pci_bus_info.pciDevice == fd_device->businfo.pci->dev &&
              wsi->pci_bus_info.pciFunction == fd_device->businfo.pci->func;
      break;

   default:
      break;
   }

   drmFreeDevice(&fd_device);

   return match;
}

static uint32_t
select_memory_type(const struct wsi_device *wsi,
                   bool want_device_local,
                   uint32_t type_bits)
{
   assert(type_bits);

   bool all_local = true;
   for (uint32_t i = 0; i < wsi->memory_props.memoryTypeCount; i++) {
       const VkMemoryType type = wsi->memory_props.memoryTypes[i];
       bool local = type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

       if ((type_bits & (1 << i)) && local == want_device_local)
         return i;
       all_local &= local;
   }

   /* ignore want_device_local when all memory types are device-local */
   if (all_local) {
      assert(!want_device_local);
      return ffs(type_bits) - 1;
   }

   unreachable("No memory type found");
}

static uint32_t
vk_format_size(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_B8G8R8A8_UNORM:
   case VK_FORMAT_B8G8R8A8_SRGB:
      return 4;
   default:
      unreachable("Unknown WSI Format");
   }
}

VkResult
wsi_create_native_image(const struct wsi_swapchain *chain,
                        const VkSwapchainCreateInfoKHR *pCreateInfo,
                        uint32_t num_modifier_lists,
                        const uint32_t *num_modifiers,
                        const uint64_t *const *modifiers,
                        uint8_t *(alloc_shm)(struct wsi_image *image, unsigned size),
                        struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   memset(image, 0, sizeof(*image));
   for (int i = 0; i < ARRAY_SIZE(image->fds); i++)
      image->fds[i] = -1;

   struct wsi_image_create_info image_wsi_info = {
      .sType = VK_STRUCTURE_TYPE_WSI_IMAGE_CREATE_INFO_MESA,
   };
   VkExternalMemoryImageCreateInfo ext_mem_image_create_info = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .pNext = &image_wsi_info,
      .handleTypes = wsi->sw ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT :
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = &ext_mem_image_create_info,
      .flags = 0,
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
      .queueFamilyIndexCount = pCreateInfo->queueFamilyIndexCount,
      .pQueueFamilyIndices = pCreateInfo->pQueueFamilyIndices,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };

   VkImageFormatListCreateInfoKHR image_format_list;
   if (pCreateInfo->flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) {
      image_info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
                          VK_IMAGE_CREATE_EXTENDED_USAGE_BIT_KHR;

      const VkImageFormatListCreateInfoKHR *format_list =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_FORMAT_LIST_CREATE_INFO_KHR);

#ifndef NDEBUG
      assume(format_list && format_list->viewFormatCount > 0);
      bool format_found = false;
      for (int i = 0; i < format_list->viewFormatCount; i++)
         if (pCreateInfo->imageFormat == format_list->pViewFormats[i])
            format_found = true;
      assert(format_found);
#endif

      image_format_list = *format_list;
      image_format_list.pNext = NULL;
      __vk_append_struct(&image_info, &image_format_list);
   }

   VkImageDrmFormatModifierListCreateInfoEXT image_modifier_list;

   uint32_t image_modifier_count = 0, modifier_prop_count = 0;
   struct VkDrmFormatModifierPropertiesEXT *modifier_props = NULL;
   uint64_t *image_modifiers = NULL;
   if (num_modifier_lists == 0) {
      /* If we don't have modifiers, fall back to the legacy "scanout" flag */
      image_wsi_info.scanout = true;
   } else {
      /* The winsys can't request modifiers if we don't support them. */
      assert(wsi->supports_modifiers);
      struct VkDrmFormatModifierPropertiesListEXT modifier_props_list = {
         .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
      };
      VkFormatProperties2 format_props = {
         .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
         .pNext = &modifier_props_list,
      };
      wsi->GetPhysicalDeviceFormatProperties2KHR(wsi->pdevice,
                                                 pCreateInfo->imageFormat,
                                                 &format_props);
      assert(modifier_props_list.drmFormatModifierCount > 0);
      modifier_props = vk_alloc(&chain->alloc,
                                sizeof(*modifier_props) *
                                modifier_props_list.drmFormatModifierCount,
                                8,
                                VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!modifier_props) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      modifier_props_list.pDrmFormatModifierProperties = modifier_props;
      wsi->GetPhysicalDeviceFormatProperties2KHR(wsi->pdevice,
                                                 pCreateInfo->imageFormat,
                                                 &format_props);

      /* Call GetImageFormatProperties with every modifier and filter the list
       * down to those that we know work.
       */
      modifier_prop_count = 0;
      for (uint32_t i = 0; i < modifier_props_list.drmFormatModifierCount; i++) {
         VkPhysicalDeviceImageDrmFormatModifierInfoEXT mod_info = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
            .drmFormatModifier = modifier_props[i].drmFormatModifier,
            .sharingMode = pCreateInfo->imageSharingMode,
            .queueFamilyIndexCount = pCreateInfo->queueFamilyIndexCount,
            .pQueueFamilyIndices = pCreateInfo->pQueueFamilyIndices,
         };
         VkPhysicalDeviceImageFormatInfo2 format_info = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
            .format = pCreateInfo->imageFormat,
            .type = VK_IMAGE_TYPE_2D,
            .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
            .usage = pCreateInfo->imageUsage,
            .flags = image_info.flags,
         };

         VkImageFormatListCreateInfoKHR format_list;
         if (image_info.flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) {
            format_list = image_format_list;
            format_list.pNext = NULL;
            __vk_append_struct(&format_info, &format_list);
         }

         VkImageFormatProperties2 format_props = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
            .pNext = NULL,
         };
         __vk_append_struct(&format_info, &mod_info);
         result = wsi->GetPhysicalDeviceImageFormatProperties2(wsi->pdevice,
                                                               &format_info,
                                                               &format_props);
         if (result == VK_SUCCESS)
            modifier_props[modifier_prop_count++] = modifier_props[i];
      }

      uint32_t max_modifier_count = 0;
      for (uint32_t l = 0; l < num_modifier_lists; l++)
         max_modifier_count = MAX2(max_modifier_count, num_modifiers[l]);

      image_modifiers = vk_alloc(&chain->alloc,
                                 sizeof(*image_modifiers) *
                                 max_modifier_count,
                                 8,
                                 VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!image_modifiers) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      image_modifier_count = 0;
      for (uint32_t l = 0; l < num_modifier_lists; l++) {
         /* Walk the modifier lists and construct a list of supported
          * modifiers.
          */
         for (uint32_t i = 0; i < num_modifiers[l]; i++) {
            for (uint32_t j = 0; j < modifier_prop_count; j++) {
               if (modifier_props[j].drmFormatModifier == modifiers[l][i])
                  image_modifiers[image_modifier_count++] = modifiers[l][i];
            }
         }

         /* We only want to take the modifiers from the first list */
         if (image_modifier_count > 0)
            break;
      }

      if (image_modifier_count > 0) {
         image_modifier_list = (VkImageDrmFormatModifierListCreateInfoEXT) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
            .drmFormatModifierCount = image_modifier_count,
            .pDrmFormatModifiers = image_modifiers,
         };
         image_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
         __vk_append_struct(&image_info, &image_modifier_list);
      } else {
         /* TODO: Add a proper error here */
         assert(!"Failed to find a supported modifier!  This should never "
                 "happen because LINEAR should always be available");
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }
   }

   result = wsi->CreateImage(chain->device, &image_info,
                             &chain->alloc, &image->image);
   if (result != VK_SUCCESS)
      goto fail;

   VkMemoryRequirements reqs;
   wsi->GetImageMemoryRequirements(chain->device, image->image, &reqs);

   void *sw_host_ptr = NULL;
   if (alloc_shm) {
      VkSubresourceLayout layout;

      wsi->GetImageSubresourceLayout(chain->device, image->image,
                                     &(VkImageSubresource) {
                                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                        .mipLevel = 0,
                                        .arrayLayer = 0,
                                     }, &layout);
      sw_host_ptr = (*alloc_shm)(image, layout.size);
   }

   const struct wsi_memory_allocate_info memory_wsi_info = {
      .sType = VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA,
      .pNext = NULL,
      .implicit_sync = true,
   };
   const VkExportMemoryAllocateInfo memory_export_info = {
      .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
      .pNext = &memory_wsi_info,
      .handleTypes = wsi->sw ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT :
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   const VkMemoryDedicatedAllocateInfo memory_dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = &memory_export_info,
      .image = image->image,
      .buffer = VK_NULL_HANDLE,
   };
   const VkImportMemoryHostPointerInfoEXT host_ptr_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
      .pNext = &memory_dedicated_info,
      .pHostPointer = sw_host_ptr,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
   };
   const VkMemoryAllocateInfo memory_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = sw_host_ptr ? (void *)&host_ptr_info : (void *)&memory_dedicated_info,
      .allocationSize = reqs.size,
      .memoryTypeIndex = select_memory_type(wsi, true, reqs.memoryTypeBits),
   };
   result = wsi->AllocateMemory(chain->device, &memory_info,
                                &chain->alloc, &image->memory);
   if (result != VK_SUCCESS)
      goto fail;

   result = wsi->BindImageMemory(chain->device, image->image,
                                 image->memory, 0);
   if (result != VK_SUCCESS)
      goto fail;

   int fd = -1;
   if (!wsi->sw) {
      const VkMemoryGetFdInfoKHR memory_get_fd_info = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
         .pNext = NULL,
         .memory = image->memory,
         .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      };

      result = wsi->GetMemoryFdKHR(chain->device, &memory_get_fd_info, &fd);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (!wsi->sw && num_modifier_lists > 0) {
      VkImageDrmFormatModifierPropertiesEXT image_mod_props = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT,
      };
      result = wsi->GetImageDrmFormatModifierPropertiesEXT(chain->device,
                                                           image->image,
                                                           &image_mod_props);
      if (result != VK_SUCCESS) {
         close(fd);
         goto fail;
      }
      image->drm_modifier = image_mod_props.drmFormatModifier;
      assert(image->drm_modifier != DRM_FORMAT_MOD_INVALID);

      for (uint32_t j = 0; j < modifier_prop_count; j++) {
         if (modifier_props[j].drmFormatModifier == image->drm_modifier) {
            image->num_planes = modifier_props[j].drmFormatModifierPlaneCount;
            break;
         }
      }

      for (uint32_t p = 0; p < image->num_planes; p++) {
         const VkImageSubresource image_subresource = {
            .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT << p,
            .mipLevel = 0,
            .arrayLayer = 0,
         };
         VkSubresourceLayout image_layout;
         wsi->GetImageSubresourceLayout(chain->device, image->image,
                                        &image_subresource, &image_layout);
         image->sizes[p] = image_layout.size;
         image->row_pitches[p] = image_layout.rowPitch;
         image->offsets[p] = image_layout.offset;
         if (p == 0) {
            image->fds[p] = fd;
         } else {
            image->fds[p] = os_dupfd_cloexec(fd);
            if (image->fds[p] == -1) {
               for (uint32_t i = 0; i < p; i++)
                  close(image->fds[i]);

               result = VK_ERROR_OUT_OF_HOST_MEMORY;
               goto fail;
            }
         }
      }
   } else {
      const VkImageSubresource image_subresource = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .mipLevel = 0,
         .arrayLayer = 0,
      };
      VkSubresourceLayout image_layout;
      wsi->GetImageSubresourceLayout(chain->device, image->image,
                                     &image_subresource, &image_layout);

      image->drm_modifier = DRM_FORMAT_MOD_INVALID;
      image->num_planes = 1;
      image->sizes[0] = reqs.size;
      image->row_pitches[0] = image_layout.rowPitch;
      image->offsets[0] = 0;
      image->fds[0] = fd;
   }

   vk_free(&chain->alloc, modifier_props);
   vk_free(&chain->alloc, image_modifiers);

   return VK_SUCCESS;

fail:
   vk_free(&chain->alloc, modifier_props);
   vk_free(&chain->alloc, image_modifiers);
   wsi_destroy_image(chain, image);

   return result;
}

static inline uint32_t
align_u32(uint32_t v, uint32_t a)
{
   assert(a != 0 && a == (a & -a));
   return (v + a - 1) & ~(a - 1);
}

#define WSI_PRIME_LINEAR_STRIDE_ALIGN 256

VkResult
wsi_create_prime_image(const struct wsi_swapchain *chain,
                       const VkSwapchainCreateInfoKHR *pCreateInfo,
                       bool use_modifier,
                       struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   memset(image, 0, sizeof(*image));

   const uint32_t cpp = vk_format_size(pCreateInfo->imageFormat);
   const uint32_t linear_stride = align_u32(pCreateInfo->imageExtent.width * cpp,
                                            WSI_PRIME_LINEAR_STRIDE_ALIGN);

   uint32_t linear_size = linear_stride * pCreateInfo->imageExtent.height;
   linear_size = align_u32(linear_size, 4096);

   const VkExternalMemoryBufferCreateInfo prime_buffer_external_info = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
      .pNext = NULL,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   const VkBufferCreateInfo prime_buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = &prime_buffer_external_info,
      .size = linear_size,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   result = wsi->CreateBuffer(chain->device, &prime_buffer_info,
                              &chain->alloc, &image->prime.buffer);
   if (result != VK_SUCCESS)
      goto fail;

   VkMemoryRequirements reqs;
   wsi->GetBufferMemoryRequirements(chain->device, image->prime.buffer, &reqs);
   assert(reqs.size <= linear_size);

   const struct wsi_memory_allocate_info memory_wsi_info = {
      .sType = VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA,
      .pNext = NULL,
      .implicit_sync = true,
   };
   const VkExportMemoryAllocateInfo prime_memory_export_info = {
      .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
      .pNext = &memory_wsi_info,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   const VkMemoryDedicatedAllocateInfo prime_memory_dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = &prime_memory_export_info,
      .image = VK_NULL_HANDLE,
      .buffer = image->prime.buffer,
   };
   const VkMemoryAllocateInfo prime_memory_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &prime_memory_dedicated_info,
      .allocationSize = linear_size,
      .memoryTypeIndex = select_memory_type(wsi, false, reqs.memoryTypeBits),
   };
   result = wsi->AllocateMemory(chain->device, &prime_memory_info,
                                &chain->alloc, &image->prime.memory);
   if (result != VK_SUCCESS)
      goto fail;

   result = wsi->BindBufferMemory(chain->device, image->prime.buffer,
                                  image->prime.memory, 0);
   if (result != VK_SUCCESS)
      goto fail;

   const struct wsi_image_create_info image_wsi_info = {
      .sType = VK_STRUCTURE_TYPE_WSI_IMAGE_CREATE_INFO_MESA,
      .prime_blit_src = true,
   };
   const VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = &image_wsi_info,
      .flags = 0,
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
      .usage = pCreateInfo->imageUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .sharingMode = pCreateInfo->imageSharingMode,
      .queueFamilyIndexCount = pCreateInfo->queueFamilyIndexCount,
      .pQueueFamilyIndices = pCreateInfo->pQueueFamilyIndices,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   result = wsi->CreateImage(chain->device, &image_info,
                             &chain->alloc, &image->image);
   if (result != VK_SUCCESS)
      goto fail;

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
      .memoryTypeIndex = select_memory_type(wsi, true, reqs.memoryTypeBits),
   };
   result = wsi->AllocateMemory(chain->device, &memory_info,
                                &chain->alloc, &image->memory);
   if (result != VK_SUCCESS)
      goto fail;

   result = wsi->BindImageMemory(chain->device, image->image,
                                 image->memory, 0);
   if (result != VK_SUCCESS)
      goto fail;

   image->prime.blit_cmd_buffers =
      vk_zalloc(&chain->alloc,
                sizeof(VkCommandBuffer) * wsi->queue_family_count, 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!image->prime.blit_cmd_buffers) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   for (uint32_t i = 0; i < wsi->queue_family_count; i++) {
      const VkCommandBufferAllocateInfo cmd_buffer_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
         .pNext = NULL,
         .commandPool = chain->cmd_pools[i],
         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
         .commandBufferCount = 1,
      };
      result = wsi->AllocateCommandBuffers(chain->device, &cmd_buffer_info,
                                           &image->prime.blit_cmd_buffers[i]);
      if (result != VK_SUCCESS)
         goto fail;

      const VkCommandBufferBeginInfo begin_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      wsi->BeginCommandBuffer(image->prime.blit_cmd_buffers[i], &begin_info);

      struct VkBufferImageCopy buffer_image_copy = {
         .bufferOffset = 0,
         .bufferRowLength = linear_stride / cpp,
         .bufferImageHeight = 0,
         .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
         .imageOffset = { .x = 0, .y = 0, .z = 0 },
         .imageExtent = {
            .width = pCreateInfo->imageExtent.width,
            .height = pCreateInfo->imageExtent.height,
            .depth = 1,
         },
      };
      wsi->CmdCopyImageToBuffer(image->prime.blit_cmd_buffers[i],
                                image->image,
                                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                image->prime.buffer,
                                1, &buffer_image_copy);

      result = wsi->EndCommandBuffer(image->prime.blit_cmd_buffers[i]);
      if (result != VK_SUCCESS)
         goto fail;
   }

   const VkMemoryGetFdInfoKHR linear_memory_get_fd_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
      .pNext = NULL,
      .memory = image->prime.memory,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   int fd;
   result = wsi->GetMemoryFdKHR(chain->device, &linear_memory_get_fd_info, &fd);
   if (result != VK_SUCCESS)
      goto fail;

   image->drm_modifier = use_modifier ? DRM_FORMAT_MOD_LINEAR : DRM_FORMAT_MOD_INVALID;
   image->num_planes = 1;
   image->sizes[0] = linear_size;
   image->row_pitches[0] = linear_stride;
   image->offsets[0] = 0;
   image->fds[0] = fd;

   return VK_SUCCESS;

fail:
   wsi_destroy_image(chain, image);

   return result;
}

