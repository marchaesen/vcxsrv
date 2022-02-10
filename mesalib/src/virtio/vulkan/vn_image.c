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
#include "vn_wsi.h"

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
   assert(plane_count <= ARRAY_SIZE(img->requirements));

   /* TODO add a per-device cache for the requirements */
   for (uint32_t i = 0; i < plane_count; i++) {
      img->requirements[i].memory.sType =
         VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
      img->requirements[i].memory.pNext = &img->requirements[i].dedicated;
      img->requirements[i].dedicated.sType =
         VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
      img->requirements[i].dedicated.pNext = NULL;
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
         &img->requirements[0].memory);

      /* AHB backed image requires dedicated allocation */
      if (img->deferred_info) {
         img->requirements[0].dedicated.prefersDedicatedAllocation = VK_TRUE;
         img->requirements[0].dedicated.requiresDedicatedAllocation = VK_TRUE;
      }
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
            &img->requirements[i].memory);
      }
   }
}

static VkResult
vn_image_store_deferred_create_info(
   const VkImageCreateInfo *create_info,
   const VkAllocationCallbacks *alloc,
   struct vn_image_create_deferred_info **out_info)
{
   struct vn_image_create_deferred_info *info = NULL;
   VkBaseOutStructure *dst = NULL;

   info = vk_zalloc(alloc, sizeof(*info), VN_DEFAULT_ALIGN,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!info)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   info->create = *create_info;
   dst = (void *)&info->create;

   vk_foreach_struct_const(src, create_info->pNext) {
      void *pnext = NULL;
      switch (src->sType) {
      case VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO:
         memcpy(&info->list, src, sizeof(info->list));
         pnext = &info->list;
         break;
      case VK_STRUCTURE_TYPE_IMAGE_STENCIL_USAGE_CREATE_INFO:
         memcpy(&info->stencil, src, sizeof(info->stencil));
         pnext = &info->stencil;
         break;
      default:
         break;
      }

      if (pnext) {
         dst->pNext = pnext;
         dst = pnext;
      }
   }
   dst->pNext = NULL;

   *out_info = info;

   return VK_SUCCESS;
}

static VkResult
vn_image_init(struct vn_device *dev,
              const VkImageCreateInfo *create_info,
              struct vn_image *img)
{
   VkDevice device = vn_device_to_handle(dev);
   VkImage image = vn_image_to_handle(img);
   VkResult result = VK_SUCCESS;

   img->sharing_mode = create_info->sharingMode;

   /* TODO async */
   result =
      vn_call_vkCreateImage(dev->instance, device, create_info, NULL, &image);
   if (result != VK_SUCCESS)
      return result;

   vn_image_init_memory_requirements(img, dev, create_info);

   return VK_SUCCESS;
}

VkResult
vn_image_create(struct vn_device *dev,
                const VkImageCreateInfo *create_info,
                const VkAllocationCallbacks *alloc,
                struct vn_image **out_img)
{
   struct vn_image *img = NULL;
   VkResult result = VK_SUCCESS;

   img = vk_zalloc(alloc, sizeof(*img), VN_DEFAULT_ALIGN,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!img)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   vn_object_base_init(&img->base, VK_OBJECT_TYPE_IMAGE, &dev->base);

   result = vn_image_init(dev, create_info, img);
   if (result != VK_SUCCESS) {
      vn_object_base_fini(&img->base);
      vk_free(alloc, img);
      return result;
   }

   *out_img = img;

   return VK_SUCCESS;
}

VkResult
vn_image_init_deferred(struct vn_device *dev,
                       const VkImageCreateInfo *create_info,
                       struct vn_image *img)
{
   return vn_image_init(dev, create_info, img);
}

VkResult
vn_image_create_deferred(struct vn_device *dev,
                         const VkImageCreateInfo *create_info,
                         const VkAllocationCallbacks *alloc,
                         struct vn_image **out_img)
{
   struct vn_image *img = NULL;
   VkResult result = VK_SUCCESS;

   img = vk_zalloc(alloc, sizeof(*img), VN_DEFAULT_ALIGN,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!img)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   vn_object_base_init(&img->base, VK_OBJECT_TYPE_IMAGE, &dev->base);

   result = vn_image_store_deferred_create_info(create_info, alloc,
                                                &img->deferred_info);
   if (result != VK_SUCCESS) {
      vn_object_base_fini(&img->base);
      vk_free(alloc, img);
      return result;
   }

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

   const struct wsi_image_create_info *wsi_info =
      vn_wsi_find_wsi_image_create_info(pCreateInfo);
   const VkNativeBufferANDROID *anb_info =
      vn_android_find_native_buffer(pCreateInfo);
   const VkExternalMemoryImageCreateInfo *external_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
   const bool ahb_info =
      external_info &&
      external_info->handleTypes ==
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

#ifdef ANDROID
   /* VkImageSwapchainCreateInfoKHR is not useful at all */
   const VkImageSwapchainCreateInfoKHR *swapchain_info = NULL;
#else
   const VkImageSwapchainCreateInfoKHR *swapchain_info = vk_find_struct_const(
      pCreateInfo->pNext, IMAGE_SWAPCHAIN_CREATE_INFO_KHR);
   if (swapchain_info && !swapchain_info->swapchain)
      swapchain_info = NULL;
#endif

   if (wsi_info) {
      result = vn_wsi_create_image(dev, pCreateInfo, wsi_info, alloc, &img);
   } else if (anb_info) {
      result =
         vn_android_image_from_anb(dev, pCreateInfo, anb_info, alloc, &img);
   } else if (ahb_info) {
      result = vn_android_image_from_ahb(dev, pCreateInfo, alloc, &img);
   } else if (swapchain_info) {
      result = vn_wsi_create_image_from_swapchain(
         dev, pCreateInfo, swapchain_info, alloc, &img);
   } else {
      result = vn_image_create(dev, pCreateInfo, alloc, &img);
   }

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

   if (img->wsi.memory && img->wsi.memory_owned) {
      VkDeviceMemory mem_handle = vn_device_memory_to_handle(img->wsi.memory);
      vn_FreeMemory(device, mem_handle, pAllocator);
   }

   vn_async_vkDestroyImage(dev->instance, device, image, NULL);

   if (img->deferred_info)
      vk_free(alloc, img->deferred_info);

   vn_object_base_fini(&img->base);
   vk_free(alloc, img);
}

void
vn_GetImageMemoryRequirements(VkDevice device,
                              VkImage image,
                              VkMemoryRequirements *pMemoryRequirements)
{
   const struct vn_image *img = vn_image_from_handle(image);

   *pMemoryRequirements = img->requirements[0].memory.memoryRequirements;
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
            img->requirements[plane].memory.memoryRequirements;
         break;
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS:
         u.dedicated->prefersDedicatedAllocation =
            img->requirements[plane].dedicated.prefersDedicatedAllocation;
         u.dedicated->requiresDedicatedAllocation =
            img->requirements[plane].dedicated.requiresDedicatedAllocation;
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

static void
vn_image_bind_wsi_memory(struct vn_image *img, struct vn_device_memory *mem)
{
   assert(img->wsi.is_wsi && !img->wsi.memory);
   img->wsi.memory = mem;
}

VkResult
vn_BindImageMemory(VkDevice device,
                   VkImage image,
                   VkDeviceMemory memory,
                   VkDeviceSize memoryOffset)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_image *img = vn_image_from_handle(image);
   struct vn_device_memory *mem = vn_device_memory_from_handle(memory);

   if (img->wsi.is_wsi)
      vn_image_bind_wsi_memory(img, mem);

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
      struct vn_image *img = vn_image_from_handle(info->image);
      struct vn_device_memory *mem =
         vn_device_memory_from_handle(info->memory);

      /* no bind info fixup needed */
      if (mem && !mem->base_memory) {
         if (img->wsi.is_wsi)
            vn_image_bind_wsi_memory(img, mem);
         continue;
      }

      if (!mem) {
#ifdef ANDROID
         /* TODO handle VkNativeBufferANDROID when we bump up
          * VN_ANDROID_NATIVE_BUFFER_SPEC_VERSION
          */
         unreachable("VkBindImageMemoryInfo with no memory");
#else
         const VkBindImageMemorySwapchainInfoKHR *swapchain_info =
            vk_find_struct_const(info->pNext,
                                 BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR);
         assert(img->wsi.is_wsi && swapchain_info);

         struct vn_image *swapchain_img =
            vn_image_from_handle(wsi_common_get_image(
               swapchain_info->swapchain, swapchain_info->imageIndex));
         mem = swapchain_img->wsi.memory;
#endif
      }

      if (img->wsi.is_wsi)
         vn_image_bind_wsi_memory(img, mem);

      if (!local_infos) {
         const size_t size = sizeof(*local_infos) * bindInfoCount;
         local_infos = vk_alloc(alloc, size, VN_DEFAULT_ALIGN,
                                VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
         if (!local_infos)
            return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

         memcpy(local_infos, pBindInfos, size);
      }

      /* If mem is suballocated, mem->base_memory is non-NULL and we must
       * patch it in.  If VkBindImageMemorySwapchainInfoKHR is given, we've
       * looked mem up above and also need to patch it in.
       */
      local_infos[i].memory = vn_device_memory_to_handle(
         mem->base_memory ? mem->base_memory : mem);
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
   view->image = vn_image_from_handle(pCreateInfo->image);

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
   const VkExternalFormatANDROID *ext_info =
      vk_find_struct_const(pCreateInfo->pNext, EXTERNAL_FORMAT_ANDROID);

   VkSamplerYcbcrConversionCreateInfo local_info;
   if (ext_info && ext_info->externalFormat) {
      assert(pCreateInfo->format == VK_FORMAT_UNDEFINED);

      local_info = *pCreateInfo;
      local_info.format =
         vn_android_drm_format_to_vk_format(ext_info->externalFormat);
      local_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
      local_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
      local_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
      local_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
      pCreateInfo = &local_info;

      assert(pCreateInfo->format != VK_FORMAT_UNDEFINED);
   }

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
