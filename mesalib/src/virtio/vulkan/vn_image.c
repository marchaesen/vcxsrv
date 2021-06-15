/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_image.h"

#include "venus-protocol/vn_protocol_driver_image.h"
#include "venus-protocol/vn_protocol_driver_image_view.h"
#include "venus-protocol/vn_protocol_driver_sampler.h"
#include "venus-protocol/vn_protocol_driver_sampler_ycbcr_conversion.h"

#include "vn_android.h"
#include "vn_device.h"
#include "vn_device_memory.h"
#include "vn_queue.h"

static VkResult
vn_record_ownership_cmds(struct vn_device *dev,
                         struct vn_image *img,
                         uint32_t family,
                         uint32_t internal_index,
                         uint32_t external_index,
                         VkCommandBuffer *out_cmds)
{
   VkResult result = VK_SUCCESS;
   VkDevice device = vn_device_to_handle(dev);
   VkImage image = vn_image_to_handle(img);
   VkCommandBuffer cmds[2];

   const VkCommandBufferAllocateInfo cmd_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = NULL,
      .commandPool = dev->android_wsi->cmd_pools[family],
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 2,
   };

   mtx_lock(&dev->android_wsi->cmd_pools_lock);
   result = vn_AllocateCommandBuffers(device, &cmd_info, cmds);
   mtx_unlock(&dev->android_wsi->cmd_pools_lock);

   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   /* record the foreign/external queue to internal queue transfer */
   const VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
   };
   vn_BeginCommandBuffer(cmds[VN_IMAGE_OWNERSHIP_ACQUIRE], &begin_info);
   VkImageMemoryBarrier barrier = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .pNext = NULL,
         .srcAccessMask = 0,
         .dstAccessMask = 0,
         .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
         .newLayout = VK_IMAGE_LAYOUT_GENERAL,
         .srcQueueFamilyIndex = external_index,
         .dstQueueFamilyIndex = internal_index,
         .image = image,
         .subresourceRange =
                 {
                         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                         .baseMipLevel = 0,
                         .levelCount = 1,
                         .baseArrayLayer = 0,
                         .layerCount = 1,
                 },
   };
   vn_CmdPipelineBarrier(
      cmds[VN_IMAGE_OWNERSHIP_ACQUIRE], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
   vn_EndCommandBuffer(cmds[VN_IMAGE_OWNERSHIP_ACQUIRE]);

   /* record the internal queue to foreign/external queue transfer */
   vn_BeginCommandBuffer(cmds[VN_IMAGE_OWNERSHIP_RELEASE], &begin_info);
   barrier.srcQueueFamilyIndex = internal_index;
   barrier.dstQueueFamilyIndex = external_index;
   vn_CmdPipelineBarrier(
      cmds[VN_IMAGE_OWNERSHIP_RELEASE], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
   vn_EndCommandBuffer(cmds[VN_IMAGE_OWNERSHIP_RELEASE]);

   out_cmds[VN_IMAGE_OWNERSHIP_ACQUIRE] = cmds[VN_IMAGE_OWNERSHIP_ACQUIRE];
   out_cmds[VN_IMAGE_OWNERSHIP_RELEASE] = cmds[VN_IMAGE_OWNERSHIP_RELEASE];

   return VK_SUCCESS;
}

VkResult
vn_image_android_wsi_init(struct vn_device *dev,
                          struct vn_image *img,
                          const VkAllocationCallbacks *alloc)
{
   VkDevice device = vn_device_to_handle(dev);
   VkResult result = VK_SUCCESS;
   const uint32_t internal_index =
      img->sharing_mode == VK_SHARING_MODE_EXCLUSIVE
         ? 0
         : VK_QUEUE_FAMILY_IGNORED;
   const uint32_t external_index = VK_QUEUE_FAMILY_FOREIGN_EXT;
   const uint32_t count = dev->physical_device->queue_family_count;

   struct vn_image_ownership_cmds *local_cmds =
      vk_zalloc(alloc, sizeof(*local_cmds) * count, VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!local_cmds)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t i = 0; i < count; i++) {
      /* skip recording if no queue is created from this family */
      uint32_t j = 0;
      for (; j < dev->queue_count; j++) {
         if (dev->queues[j].family == i)
            break;
      }
      if (j == dev->queue_count)
         continue;

      result = vn_record_ownership_cmds(dev, img, i, internal_index,
                                        external_index, local_cmds[i].cmds);
      if (result != VK_SUCCESS)
         goto fail;
   }

   img->ownership_cmds = local_cmds;

   return VK_SUCCESS;

fail:
   for (uint32_t i = 0; i < count; i++) {
      if (local_cmds[i].cmds[0] != VK_NULL_HANDLE)
         vn_FreeCommandBuffers(device, dev->android_wsi->cmd_pools[i], 2,
                               local_cmds[i].cmds);
   }
   vk_free(alloc, local_cmds);
   return vn_error(dev->instance, result);
}

static void
vn_image_android_wsi_fini(struct vn_device *dev,
                          struct vn_image *img,
                          const VkAllocationCallbacks *alloc)
{
   if (!dev->android_wsi || !img->ownership_cmds)
      return;

   VkDevice device = vn_device_to_handle(dev);

   mtx_lock(&dev->android_wsi->cmd_pools_lock);
   for (uint32_t i = 0; i < dev->physical_device->queue_family_count; i++) {
      if (img->ownership_cmds[i].cmds[0] != VK_NULL_HANDLE)
         vn_FreeCommandBuffers(device, dev->android_wsi->cmd_pools[i], 2,
                               img->ownership_cmds[i].cmds);
   }
   mtx_unlock(&dev->android_wsi->cmd_pools_lock);

   vk_free(alloc, img->ownership_cmds);
}

static void
vn_image_init_memory_requirements(struct vn_image *img,
                                  struct vn_device *dev,
                                  const VkImageCreateInfo *create_info)
{
   uint32_t plane_count = 1;
   if (create_info->flags & VK_IMAGE_CREATE_DISJOINT_BIT) {
      /* TODO VkDrmFormatModifierPropertiesEXT::drmFormatModifierPlaneCount */
      assert(create_info->tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT);

      switch (create_info->format) {
      case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
      case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
      case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
      case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
      case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
      case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
      case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
         plane_count = 2;
         break;
      case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
      case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
      case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
      case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
      case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
      case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
      case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
      case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
      case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
      case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
      case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
      case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
         plane_count = 3;
         break;
      default:
         plane_count = 1;
         break;
      }
   }
   assert(plane_count <= ARRAY_SIZE(img->memory_requirements));

   /* TODO add a per-device cache for the requirements */
   for (uint32_t i = 0; i < plane_count; i++) {
      img->memory_requirements[i].sType =
         VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
      img->memory_requirements[i].pNext = &img->dedicated_requirements[i];
      img->dedicated_requirements[i].sType =
         VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
      img->dedicated_requirements[i].pNext = NULL;
   }

   VkDevice dev_handle = vn_device_to_handle(dev);
   VkImage img_handle = vn_image_to_handle(img);
   if (plane_count == 1) {
      vn_call_vkGetImageMemoryRequirements2(
         dev->instance, dev_handle,
         &(VkImageMemoryRequirementsInfo2){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
            .image = img_handle,
         },
         &img->memory_requirements[0]);
   } else {
      for (uint32_t i = 0; i < plane_count; i++) {
         vn_call_vkGetImageMemoryRequirements2(
            dev->instance, dev_handle,
            &(VkImageMemoryRequirementsInfo2){
               .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
               .pNext =
                  &(VkImagePlaneMemoryRequirementsInfo){
                     .sType =
                        VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO,
                     .planeAspect = VK_IMAGE_ASPECT_PLANE_0_BIT << i,
                  },
               .image = img_handle,
            },
            &img->memory_requirements[i]);
      }
   }
}

VkResult
vn_image_create(struct vn_device *dev,
                const VkImageCreateInfo *create_info,
                const VkAllocationCallbacks *alloc,
                struct vn_image **out_img)
{
   struct vn_image *img = vk_zalloc(alloc, sizeof(*img), VN_DEFAULT_ALIGN,
                                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!img)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   vn_object_base_init(&img->base, VK_OBJECT_TYPE_IMAGE, &dev->base);

   VkDevice dev_handle = vn_device_to_handle(dev);
   VkImage img_handle = vn_image_to_handle(img);
   /* TODO async */
   VkResult result = vn_call_vkCreateImage(dev->instance, dev_handle,
                                           create_info, NULL, &img_handle);
   if (result != VK_SUCCESS) {
      vk_free(alloc, img);
      return result;
   }

   vn_image_init_memory_requirements(img, dev, create_info);

   img->sharing_mode = create_info->sharingMode;

   *out_img = img;

   return VK_SUCCESS;
}

/* image commands */

VkResult
vn_CreateImage(VkDevice device,
               const VkImageCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator,
               VkImage *pImage)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;
   struct vn_image *img;
   VkResult result;

#ifdef VN_USE_WSI_PLATFORM
   const struct wsi_image_create_info *wsi_info =
      vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);
   if (wsi_info) {
      assert(wsi_info->scanout);
      result = vn_wsi_create_scanout_image(dev, pCreateInfo, alloc, &img);
      goto out;
   }
#endif

#ifdef ANDROID
   const VkNativeBufferANDROID *anb_info =
      vk_find_struct_const(pCreateInfo->pNext, NATIVE_BUFFER_ANDROID);
   if (anb_info) {
      result = vn_image_from_anb(dev, pCreateInfo, anb_info, alloc, &img);
      goto out;
   }
#endif

   result = vn_image_create(dev, pCreateInfo, alloc, &img);

out:
   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   *pImage = vn_image_to_handle(img);
   return VK_SUCCESS;
}

void
vn_DestroyImage(VkDevice device,
                VkImage image,
                const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_image *img = vn_image_from_handle(image);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!img)
      return;

   vn_image_android_wsi_fini(dev, img, alloc);

   if (img->private_memory != VK_NULL_HANDLE)
      vn_FreeMemory(device, img->private_memory, pAllocator);

   vn_async_vkDestroyImage(dev->instance, device, image, NULL);

   vn_object_base_fini(&img->base);
   vk_free(alloc, img);
}

void
vn_GetImageMemoryRequirements(VkDevice device,
                              VkImage image,
                              VkMemoryRequirements *pMemoryRequirements)
{
   const struct vn_image *img = vn_image_from_handle(image);

   *pMemoryRequirements = img->memory_requirements[0].memoryRequirements;
}

void
vn_GetImageSparseMemoryRequirements(
   VkDevice device,
   VkImage image,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements *pSparseMemoryRequirements)
{
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO per-device cache */
   vn_call_vkGetImageSparseMemoryRequirements(dev->instance, device, image,
                                              pSparseMemoryRequirementCount,
                                              pSparseMemoryRequirements);
}

void
vn_GetImageMemoryRequirements2(VkDevice device,
                               const VkImageMemoryRequirementsInfo2 *pInfo,
                               VkMemoryRequirements2 *pMemoryRequirements)
{
   const struct vn_image *img = vn_image_from_handle(pInfo->image);
   union {
      VkBaseOutStructure *pnext;
      VkMemoryRequirements2 *two;
      VkMemoryDedicatedRequirements *dedicated;
   } u = { .two = pMemoryRequirements };

   uint32_t plane = 0;
   const VkImagePlaneMemoryRequirementsInfo *plane_info =
      vk_find_struct_const(pInfo->pNext,
                           IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO);
   if (plane_info) {
      switch (plane_info->planeAspect) {
      case VK_IMAGE_ASPECT_PLANE_1_BIT:
         plane = 1;
         break;
      case VK_IMAGE_ASPECT_PLANE_2_BIT:
         plane = 2;
         break;
      default:
         plane = 0;
         break;
      }
   }

   while (u.pnext) {
      switch (u.pnext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2:
         u.two->memoryRequirements =
            img->memory_requirements[plane].memoryRequirements;
         break;
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS:
         u.dedicated->prefersDedicatedAllocation =
            img->dedicated_requirements[plane].prefersDedicatedAllocation;
         u.dedicated->requiresDedicatedAllocation =
            img->dedicated_requirements[plane].requiresDedicatedAllocation;
         break;
      default:
         break;
      }
      u.pnext = u.pnext->pNext;
   }
}

void
vn_GetImageSparseMemoryRequirements2(
   VkDevice device,
   const VkImageSparseMemoryRequirementsInfo2 *pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO per-device cache */
   vn_call_vkGetImageSparseMemoryRequirements2(dev->instance, device, pInfo,
                                               pSparseMemoryRequirementCount,
                                               pSparseMemoryRequirements);
}

VkResult
vn_BindImageMemory(VkDevice device,
                   VkImage image,
                   VkDeviceMemory memory,
                   VkDeviceSize memoryOffset)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_device_memory *mem = vn_device_memory_from_handle(memory);

   if (mem->base_memory) {
      memory = vn_device_memory_to_handle(mem->base_memory);
      memoryOffset += mem->base_offset;
   }

   vn_async_vkBindImageMemory(dev->instance, device, image, memory,
                              memoryOffset);

   return VK_SUCCESS;
}

VkResult
vn_BindImageMemory2(VkDevice device,
                    uint32_t bindInfoCount,
                    const VkBindImageMemoryInfo *pBindInfos)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;

   VkBindImageMemoryInfo *local_infos = NULL;
   for (uint32_t i = 0; i < bindInfoCount; i++) {
      const VkBindImageMemoryInfo *info = &pBindInfos[i];
      struct vn_device_memory *mem =
         vn_device_memory_from_handle(info->memory);
      /* TODO handle VkBindImageMemorySwapchainInfoKHR */
      if (!mem || !mem->base_memory)
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

   vn_async_vkBindImageMemory2(dev->instance, device, bindInfoCount,
                               pBindInfos);

   vk_free(alloc, local_infos);

   return VK_SUCCESS;
}

VkResult
vn_GetImageDrmFormatModifierPropertiesEXT(
   VkDevice device,
   VkImage image,
   VkImageDrmFormatModifierPropertiesEXT *pProperties)
{
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO local cache */
   return vn_call_vkGetImageDrmFormatModifierPropertiesEXT(
      dev->instance, device, image, pProperties);
}

void
vn_GetImageSubresourceLayout(VkDevice device,
                             VkImage image,
                             const VkImageSubresource *pSubresource,
                             VkSubresourceLayout *pLayout)
{
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO local cache */
   vn_call_vkGetImageSubresourceLayout(dev->instance, device, image,
                                       pSubresource, pLayout);
}

/* image view commands */

VkResult
vn_CreateImageView(VkDevice device,
                   const VkImageViewCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkImageView *pView)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_image_view *view =
      vk_zalloc(alloc, sizeof(*view), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!view)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&view->base, VK_OBJECT_TYPE_IMAGE_VIEW, &dev->base);

   VkImageView view_handle = vn_image_view_to_handle(view);
   vn_async_vkCreateImageView(dev->instance, device, pCreateInfo, NULL,
                              &view_handle);

   *pView = view_handle;

   return VK_SUCCESS;
}

void
vn_DestroyImageView(VkDevice device,
                    VkImageView imageView,
                    const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_image_view *view = vn_image_view_from_handle(imageView);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!view)
      return;

   vn_async_vkDestroyImageView(dev->instance, device, imageView, NULL);

   vn_object_base_fini(&view->base);
   vk_free(alloc, view);
}

/* sampler commands */

VkResult
vn_CreateSampler(VkDevice device,
                 const VkSamplerCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkSampler *pSampler)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_sampler *sampler =
      vk_zalloc(alloc, sizeof(*sampler), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!sampler)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&sampler->base, VK_OBJECT_TYPE_SAMPLER, &dev->base);

   VkSampler sampler_handle = vn_sampler_to_handle(sampler);
   vn_async_vkCreateSampler(dev->instance, device, pCreateInfo, NULL,
                            &sampler_handle);

   *pSampler = sampler_handle;

   return VK_SUCCESS;
}

void
vn_DestroySampler(VkDevice device,
                  VkSampler _sampler,
                  const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_sampler *sampler = vn_sampler_from_handle(_sampler);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!sampler)
      return;

   vn_async_vkDestroySampler(dev->instance, device, _sampler, NULL);

   vn_object_base_fini(&sampler->base);
   vk_free(alloc, sampler);
}

/* sampler YCbCr conversion commands */

VkResult
vn_CreateSamplerYcbcrConversion(
   VkDevice device,
   const VkSamplerYcbcrConversionCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkSamplerYcbcrConversion *pYcbcrConversion)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_sampler_ycbcr_conversion *conv =
      vk_zalloc(alloc, sizeof(*conv), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!conv)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&conv->base, VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION,
                       &dev->base);

   VkSamplerYcbcrConversion conv_handle =
      vn_sampler_ycbcr_conversion_to_handle(conv);
   vn_async_vkCreateSamplerYcbcrConversion(dev->instance, device, pCreateInfo,
                                           NULL, &conv_handle);

   *pYcbcrConversion = conv_handle;

   return VK_SUCCESS;
}

void
vn_DestroySamplerYcbcrConversion(VkDevice device,
                                 VkSamplerYcbcrConversion ycbcrConversion,
                                 const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_sampler_ycbcr_conversion *conv =
      vn_sampler_ycbcr_conversion_from_handle(ycbcrConversion);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!conv)
      return;

   vn_async_vkDestroySamplerYcbcrConversion(dev->instance, device,
                                            ycbcrConversion, NULL);

   vn_object_base_fini(&conv->base);
   vk_free(alloc, conv);
}
