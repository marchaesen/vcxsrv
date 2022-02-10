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
#include "wsi_common_drm.h"
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
wsi_common_drm_devices_equal(int fd_a, int fd_b)
{
   drmDevicePtr device_a, device_b;
   int ret;

   ret = drmGetDevice2(fd_a, 0, &device_a);
   if (ret)
      return false;

   ret = drmGetDevice2(fd_b, 0, &device_b);
   if (ret) {
      drmFreeDevice(&device_a);
      return false;
   }

   bool result = drmDevicesEqual(device_a, device_b);

   drmFreeDevice(&device_a);
   drmFreeDevice(&device_b);

   return result;
}

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

static const struct VkDrmFormatModifierPropertiesEXT *
get_modifier_props(const struct wsi_image_info *info, uint64_t modifier)
{
   for (uint32_t i = 0; i < info->modifier_prop_count; i++) {
      if (info->modifier_props[i].drmFormatModifier == modifier)
         return &info->modifier_props[i];
   }
   return NULL;
}

static VkResult
wsi_create_native_image_mem(const struct wsi_swapchain *chain,
                            const struct wsi_image_info *info,
                            struct wsi_image *image);

VkResult
wsi_configure_native_image(const struct wsi_swapchain *chain,
                           const VkSwapchainCreateInfoKHR *pCreateInfo,
                           uint32_t num_modifier_lists,
                           const uint32_t *num_modifiers,
                           const uint64_t *const *modifiers,
                           uint8_t *(alloc_shm)(struct wsi_image *image,
                                                unsigned size),
                           struct wsi_image_info *info)
{
   const struct wsi_device *wsi = chain->wsi;

   VkExternalMemoryHandleTypeFlags handle_type =
      wsi->sw ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT :
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

   VkResult result = wsi_configure_image(chain, pCreateInfo, handle_type, info);
   if (result != VK_SUCCESS)
      return result;

   if (num_modifier_lists == 0) {
      /* If we don't have modifiers, fall back to the legacy "scanout" flag */
      info->wsi.scanout = true;
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
      info->modifier_props =
         vk_alloc(&chain->alloc,
                  sizeof(*info->modifier_props) *
                  modifier_props_list.drmFormatModifierCount,
                  8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (info->modifier_props == NULL)
         goto fail_oom;

      modifier_props_list.pDrmFormatModifierProperties = info->modifier_props;
      wsi->GetPhysicalDeviceFormatProperties2KHR(wsi->pdevice,
                                                 pCreateInfo->imageFormat,
                                                 &format_props);

      /* Call GetImageFormatProperties with every modifier and filter the list
       * down to those that we know work.
       */
      info->modifier_prop_count = 0;
      for (uint32_t i = 0; i < modifier_props_list.drmFormatModifierCount; i++) {
         VkPhysicalDeviceImageDrmFormatModifierInfoEXT mod_info = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
            .drmFormatModifier = info->modifier_props[i].drmFormatModifier,
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
            .flags = info->create.flags,
         };

         VkImageFormatListCreateInfoKHR format_list;
         if (info->create.flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) {
            format_list = info->format_list;
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
            info->modifier_props[info->modifier_prop_count++] = info->modifier_props[i];
      }

      uint32_t max_modifier_count = 0;
      for (uint32_t l = 0; l < num_modifier_lists; l++)
         max_modifier_count = MAX2(max_modifier_count, num_modifiers[l]);

      uint64_t *image_modifiers =
         vk_alloc(&chain->alloc, sizeof(*image_modifiers) * max_modifier_count,
                  8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!image_modifiers)
         goto fail_oom;

      uint32_t image_modifier_count = 0;
      for (uint32_t l = 0; l < num_modifier_lists; l++) {
         /* Walk the modifier lists and construct a list of supported
          * modifiers.
          */
         for (uint32_t i = 0; i < num_modifiers[l]; i++) {
            if (get_modifier_props(info, modifiers[l][i]))
               image_modifiers[image_modifier_count++] = modifiers[l][i];
         }

         /* We only want to take the modifiers from the first list */
         if (image_modifier_count > 0)
            break;
      }

      if (image_modifier_count > 0) {
         info->create.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
         info->drm_mod_list = (VkImageDrmFormatModifierListCreateInfoEXT) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
            .drmFormatModifierCount = image_modifier_count,
            .pDrmFormatModifiers = image_modifiers,
         };
         image_modifiers = NULL;
         __vk_append_struct(&info->create, &info->drm_mod_list);
      } else {
         vk_free(&chain->alloc, image_modifiers);
         /* TODO: Add a proper error here */
         assert(!"Failed to find a supported modifier!  This should never "
                 "happen because LINEAR should always be available");
         goto fail_oom;
      }
   }

   info->alloc_shm = alloc_shm;
   info->create_mem = wsi_create_native_image_mem;

   return VK_SUCCESS;

fail_oom:
   wsi_destroy_image_info(chain, info);
   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

static VkResult
wsi_create_native_image_mem(const struct wsi_swapchain *chain,
                            const struct wsi_image_info *info,
                            struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   VkMemoryRequirements reqs;
   wsi->GetImageMemoryRequirements(chain->device, image->image, &reqs);

   void *sw_host_ptr = NULL;
   if (info->alloc_shm) {
      VkSubresourceLayout layout;

      wsi->GetImageSubresourceLayout(chain->device, image->image,
                                     &(VkImageSubresource) {
                                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                        .mipLevel = 0,
                                        .arrayLayer = 0,
                                     }, &layout);
      sw_host_ptr = info->alloc_shm(image, layout.size);
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
      return result;

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
         return result;
   }

   if (!wsi->sw && info->drm_mod_list.drmFormatModifierCount > 0) {
      VkImageDrmFormatModifierPropertiesEXT image_mod_props = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT,
      };
      result = wsi->GetImageDrmFormatModifierPropertiesEXT(chain->device,
                                                           image->image,
                                                           &image_mod_props);
      if (result != VK_SUCCESS) {
         close(fd);
         return result;
      }
      image->drm_modifier = image_mod_props.drmFormatModifier;
      assert(image->drm_modifier != DRM_FORMAT_MOD_INVALID);

      const struct VkDrmFormatModifierPropertiesEXT *mod_props =
         get_modifier_props(info, image->drm_modifier);
      image->num_planes = mod_props->drmFormatModifierPlaneCount;

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

               return VK_ERROR_OUT_OF_HOST_MEMORY;
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

   return VK_SUCCESS;
}

static inline uint32_t
align_u32(uint32_t v, uint32_t a)
{
   assert(a != 0 && a == (a & -a));
   return (v + a - 1) & ~(a - 1);
}

#define WSI_PRIME_LINEAR_STRIDE_ALIGN 256

static VkResult
wsi_create_prime_image_mem(const struct wsi_swapchain *chain,
                           const struct wsi_image_info *info,
                           struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   uint32_t linear_size = info->linear_stride * info->create.extent.height;
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
      return result;

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
      return result;

   result = wsi->BindBufferMemory(chain->device, image->prime.buffer,
                                  image->prime.memory, 0);
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
      .memoryTypeIndex = select_memory_type(wsi, true, reqs.memoryTypeBits),
   };
   result = wsi->AllocateMemory(chain->device, &memory_info,
                                &chain->alloc, &image->memory);
   if (result != VK_SUCCESS)
      return result;

   const VkMemoryGetFdInfoKHR linear_memory_get_fd_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
      .pNext = NULL,
      .memory = image->prime.memory,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   int fd;
   result = wsi->GetMemoryFdKHR(chain->device, &linear_memory_get_fd_info, &fd);
   if (result != VK_SUCCESS)
      return result;

   image->drm_modifier = info->prime_use_linear_modifier ?
                         DRM_FORMAT_MOD_LINEAR : DRM_FORMAT_MOD_INVALID;
   image->num_planes = 1;
   image->sizes[0] = linear_size;
   image->row_pitches[0] = info->linear_stride;
   image->offsets[0] = 0;
   image->fds[0] = fd;

   return VK_SUCCESS;
}

static VkResult
wsi_finish_create_prime_image(const struct wsi_swapchain *chain,
                              const struct wsi_image_info *info,
                              struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   image->prime.blit_cmd_buffers =
      vk_zalloc(&chain->alloc,
                sizeof(VkCommandBuffer) * wsi->queue_family_count, 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!image->prime.blit_cmd_buffers)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   int cmd_buffer_count = chain->prime_blit_queue != VK_NULL_HANDLE ? 1 : wsi->queue_family_count;
   for (uint32_t i = 0; i < cmd_buffer_count; i++) {
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
         return result;

      const VkCommandBufferBeginInfo begin_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      wsi->BeginCommandBuffer(image->prime.blit_cmd_buffers[i], &begin_info);

      struct VkBufferImageCopy buffer_image_copy = {
         .bufferOffset = 0,
         .bufferRowLength = info->linear_stride /
                            vk_format_size(info->create.format),
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
      wsi->CmdCopyImageToBuffer(image->prime.blit_cmd_buffers[i],
                                image->image,
                                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                image->prime.buffer,
                                1, &buffer_image_copy);

      result = wsi->EndCommandBuffer(image->prime.blit_cmd_buffers[i]);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

VkResult
wsi_configure_prime_image(UNUSED const struct wsi_swapchain *chain,
                          const VkSwapchainCreateInfoKHR *pCreateInfo,
                          bool use_modifier,
                          struct wsi_image_info *info)
{
   VkResult result = wsi_configure_image(chain, pCreateInfo,
                                         0 /* handle_types */, info);
   if (result != VK_SUCCESS)
      return result;

   info->create.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
   info->wsi.prime_blit_src = true,
   info->prime_use_linear_modifier = use_modifier;

   const uint32_t cpp = vk_format_size(info->create.format);
   info->linear_stride = align_u32(info->create.extent.width * cpp,
                                   WSI_PRIME_LINEAR_STRIDE_ALIGN);

   info->create_mem = wsi_create_prime_image_mem;
   info->finish_create = wsi_finish_create_prime_image;

   return VK_SUCCESS;
}
