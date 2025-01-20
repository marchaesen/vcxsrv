/*
 * Copyright 2024 Autodesk, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "vk_instance.h"
#include "vk_physical_device.h"
#include "vk_util.h"

#include "util/timespec.h"
#include "util/u_vector.h"

#include "wsi_common_entrypoints.h"
#include "wsi_common_private.h"
#include "wsi_common_metal_layer.h"

#include "vulkan/vulkan_core.h"

#include <assert.h>

struct wsi_metal {
   struct wsi_interface base;

   struct wsi_device *wsi;

   const VkAllocationCallbacks *alloc;
   VkPhysicalDevice physical_device;
};

static VkResult
wsi_metal_surface_get_support(VkIcdSurfaceBase *surface,
                                 struct wsi_device *wsi_device,
                                 uint32_t queueFamilyIndex,
                                 VkBool32* pSupported)
{
   *pSupported = true;
   return VK_SUCCESS;
}

static const VkPresentModeKHR present_modes[] = {
   VK_PRESENT_MODE_IMMEDIATE_KHR,
   VK_PRESENT_MODE_FIFO_KHR,
};

static VkResult
wsi_metal_surface_get_capabilities(VkIcdSurfaceBase *surface,
                                 struct wsi_device *wsi_device,
                                 VkSurfaceCapabilitiesKHR* caps)
{
   VkIcdSurfaceMetal *metal_surface = (VkIcdSurfaceMetal *)surface;
   assert(metal_surface->pLayer);

   wsi_metal_layer_size(metal_surface->pLayer,
      &caps->currentExtent.width,
      &caps->currentExtent.height);

   caps->minImageCount = 2;
   caps->maxImageCount = 3;

   caps->minImageExtent = (VkExtent2D) { 1, 1 };
   caps->maxImageExtent = (VkExtent2D) {
      wsi_device->maxImageDimension2D,
      wsi_device->maxImageDimension2D,
   };

   caps->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   caps->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   caps->maxImageArrayLayers = 1;

   caps->supportedCompositeAlpha =
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR |
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;

   caps->supportedUsageFlags = wsi_caps_get_image_usage();

   VK_FROM_HANDLE(vk_physical_device, pdevice, wsi_device->pdevice);
   if (pdevice->supported_extensions.EXT_attachment_feedback_loop_layout)
      caps->supportedUsageFlags |= VK_IMAGE_USAGE_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT;

   return VK_SUCCESS;
}

static VkResult
wsi_metal_surface_get_capabilities2(VkIcdSurfaceBase *surface,
                                       struct wsi_device *wsi_device,
                                       const void *info_next,
                                       VkSurfaceCapabilities2KHR* caps)
{
   assert(caps->sType == VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR);

   const VkSurfacePresentModeEXT *present_mode =
      (const VkSurfacePresentModeEXT *)vk_find_struct_const(info_next, SURFACE_PRESENT_MODE_EXT);

   VkResult result =
      wsi_metal_surface_get_capabilities(surface, wsi_device,
                                      &caps->surfaceCapabilities);

   vk_foreach_struct(ext, caps->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_SURFACE_PROTECTED_CAPABILITIES_KHR: {
         VkSurfaceProtectedCapabilitiesKHR *protected = (void *)ext;
         protected->supportsProtected = VK_FALSE;
         break;
      }

      case VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_EXT: {
         /* TODO: support scaling */
         VkSurfacePresentScalingCapabilitiesEXT *scaling =
            (VkSurfacePresentScalingCapabilitiesEXT *)ext;
         scaling->supportedPresentScaling = 0;
         scaling->supportedPresentGravityX = 0;
         scaling->supportedPresentGravityY = 0;
         scaling->minScaledImageExtent = caps->surfaceCapabilities.minImageExtent;
         scaling->maxScaledImageExtent = caps->surfaceCapabilities.maxImageExtent;
         break;
      }

      case VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_EXT: {
         /* Unsupported, just report the input present mode. */
         VkSurfacePresentModeCompatibilityEXT *compat =
            (VkSurfacePresentModeCompatibilityEXT *)ext;
         if (compat->pPresentModes) {
            if (compat->presentModeCount) {
               assert(present_mode);
               compat->pPresentModes[0] = present_mode->presentMode;
               compat->presentModeCount = 1;
            }
         } else {
            if (!present_mode)
               wsi_common_vk_warn_once("Use of VkSurfacePresentModeCompatibilityEXT "
                                       "without a VkSurfacePresentModeEXT set. This is an "
                                       "application bug.\n");
            compat->presentModeCount = 1;
         }
         break;
      }

      default:
         /* Ignored */
         break;
      }
   }

   return result;
}

static const VkFormat available_surface_formats[] = {
   VK_FORMAT_B8G8R8A8_SRGB,
   VK_FORMAT_B8G8R8A8_UNORM,
   VK_FORMAT_R16G16B16A16_SFLOAT,
   VK_FORMAT_A2R10G10B10_UNORM_PACK32,
   VK_FORMAT_A2B10G10R10_UNORM_PACK32,
};

static void
get_sorted_vk_formats(bool force_bgra8_unorm_first, VkFormat *sorted_formats)
{
   for (unsigned i = 0; i < ARRAY_SIZE(available_surface_formats); i++)
      sorted_formats[i] = available_surface_formats[i];

   if (force_bgra8_unorm_first) {
      for (unsigned i = 0; i < ARRAY_SIZE(available_surface_formats); i++) {
         if (sorted_formats[i] == VK_FORMAT_B8G8R8A8_UNORM) {
            sorted_formats[i] = sorted_formats[0];
            sorted_formats[0] = VK_FORMAT_B8G8R8A8_UNORM;
            break;
         }
      }
   }
}

static VkResult
wsi_metal_surface_get_formats(VkIcdSurfaceBase *icd_surface,
                                 struct wsi_device *wsi_device,
                                 uint32_t* pSurfaceFormatCount,
                                 VkSurfaceFormatKHR* pSurfaceFormats)
{
   VK_OUTARRAY_MAKE_TYPED(VkSurfaceFormatKHR, out, pSurfaceFormats, pSurfaceFormatCount);

   VkFormat sorted_formats[ARRAY_SIZE(available_surface_formats)];
   get_sorted_vk_formats(wsi_device->force_bgra8_unorm_first, sorted_formats);

   for (unsigned i = 0; i < ARRAY_SIZE(sorted_formats); i++) {
      vk_outarray_append_typed(VkSurfaceFormatKHR, &out, f) {
         f->format = sorted_formats[i];
         f->colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
      }
   }

   return vk_outarray_status(&out);
}

static VkResult
wsi_metal_surface_get_formats2(VkIcdSurfaceBase *icd_surface,
                                  struct wsi_device *wsi_device,
                                  const void *info_next,
                                  uint32_t* pSurfaceFormatCount,
                                  VkSurfaceFormat2KHR* pSurfaceFormats)
{
   VK_OUTARRAY_MAKE_TYPED(VkSurfaceFormat2KHR, out, pSurfaceFormats, pSurfaceFormatCount);

   VkFormat sorted_formats[ARRAY_SIZE(available_surface_formats)];
   get_sorted_vk_formats(wsi_device->force_bgra8_unorm_first, sorted_formats);

   for (unsigned i = 0; i < ARRAY_SIZE(sorted_formats); i++) {
      vk_outarray_append_typed(VkSurfaceFormat2KHR, &out, f) {
         assert(f->sType == VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR);
         f->surfaceFormat.format = sorted_formats[i];
         f->surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
      }
   }

   return vk_outarray_status(&out);
}

static VkResult
wsi_metal_surface_get_present_modes(VkIcdSurfaceBase *surface,
                                       struct wsi_device *wsi_device,
                                       uint32_t* pPresentModeCount,
                                       VkPresentModeKHR* pPresentModes)
{
   if (pPresentModes == NULL) {
      *pPresentModeCount = ARRAY_SIZE(present_modes);
      return VK_SUCCESS;
   }

   *pPresentModeCount = MIN2(*pPresentModeCount, ARRAY_SIZE(present_modes));
   typed_memcpy(pPresentModes, present_modes, *pPresentModeCount);

   return *pPresentModeCount < ARRAY_SIZE(present_modes) ? VK_INCOMPLETE : VK_SUCCESS;
}

static VkResult
wsi_metal_surface_get_present_rectangles(VkIcdSurfaceBase *surface,
                                            struct wsi_device *wsi_device,
                                            uint32_t* pRectCount,
                                            VkRect2D* pRects)
{
   VK_OUTARRAY_MAKE_TYPED(VkRect2D, out, pRects, pRectCount);

   vk_outarray_append_typed(VkRect2D, &out, rect) {
      /* We don't know a size so just return the usual "I don't know." */
      *rect = (VkRect2D) {
         .offset = { 0, 0 },
         .extent = { UINT32_MAX, UINT32_MAX },
      };
   }

   return vk_outarray_status(&out);
}

struct wsi_metal_image {
   struct wsi_image base;
   CAMetalDrawableBridged *drawable;
};

struct wsi_metal_swapchain {
   struct wsi_swapchain base;

   VkExtent2D extent;
   VkFormat vk_format;

   struct u_vector modifiers;

   VkPresentModeKHR present_mode;
   bool fifo_ready;

   VkIcdSurfaceMetal *surface;

   struct wsi_metal_layer_blit_context *blit_context;

   uint32_t current_image_index;
   struct wsi_metal_image images[0];
};
VK_DEFINE_NONDISP_HANDLE_CASTS(wsi_metal_swapchain, base.base, VkSwapchainKHR,
                               VK_OBJECT_TYPE_SWAPCHAIN_KHR)

static struct wsi_image *
wsi_metal_swapchain_get_wsi_image(struct wsi_swapchain *wsi_chain,
                                     uint32_t image_index)
{
   struct wsi_metal_swapchain *chain =
      (struct wsi_metal_swapchain *)wsi_chain;
   return &chain->images[image_index].base;
}

static VkResult
wsi_metal_swapchain_acquire_next_image(struct wsi_swapchain *wsi_chain,
                                       const VkAcquireNextImageInfoKHR *info,
                                       uint32_t *image_index)
{
   struct wsi_metal_swapchain *chain =
      (struct wsi_metal_swapchain *)wsi_chain;
   struct timespec start_time, end_time;
   struct timespec rel_timeout;

   timespec_from_nsec(&rel_timeout, info->timeout);

   clock_gettime(CLOCK_MONOTONIC, &start_time);
   timespec_add(&end_time, &rel_timeout, &start_time);

   while (1) {
      /* Try to acquire an drawable. Unfortunately we might block for up to 1 second. */
      CAMetalDrawableBridged *drawable = wsi_metal_layer_acquire_drawable(chain->surface->pLayer);
      if (drawable) {
         uint32_t i = (chain->current_image_index++) % chain->base.image_count;
         *image_index = i;
         chain->images[i].drawable = drawable;
         return VK_SUCCESS;
      }

      /* Check for timeout. */
      struct timespec current_time;
      clock_gettime(CLOCK_MONOTONIC, &current_time);
      if (timespec_after(&current_time, &end_time))
         return VK_NOT_READY;
   }
}

static VkResult
wsi_metal_swapchain_queue_present(struct wsi_swapchain *wsi_chain,
                                     uint32_t image_index,
                                     uint64_t present_id,
                                     const VkPresentRegionKHR *damage)
{
   struct wsi_metal_swapchain *chain =
      (struct wsi_metal_swapchain *)wsi_chain;

   assert(image_index < chain->base.image_count);

   struct wsi_metal_image *image = &chain->images[image_index];

   wsi_metal_layer_blit_and_present(chain->blit_context,
      &image->drawable,
      image->base.cpu_map,
      chain->extent.width, chain->extent.height,
      image->base.row_pitches[0]);

   return VK_SUCCESS;
}

static VkResult
wsi_metal_swapchain_destroy(struct wsi_swapchain *wsi_chain,
                               const VkAllocationCallbacks *pAllocator)
{
   struct wsi_metal_swapchain *chain =
      (struct wsi_metal_swapchain *)wsi_chain;

   for (uint32_t i = 0; i < chain->base.image_count; i++) {
      wsi_metal_layer_cancel_present(chain->blit_context, &chain->images[i].drawable);
      if (chain->images[i].base.image != VK_NULL_HANDLE)
         wsi_destroy_image(&chain->base, &chain->images[i].base);
   }

   u_vector_finish(&chain->modifiers);

   wsi_destroy_metal_layer_blit_context(chain->blit_context);

   wsi_swapchain_finish(&chain->base);

   vk_free(pAllocator, chain);

   return VK_SUCCESS;
}

static VkResult
wsi_metal_surface_create_swapchain(VkIcdSurfaceBase *icd_surface,
                                      VkDevice device,
                                      struct wsi_device *wsi_device,
                                      const VkSwapchainCreateInfoKHR* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator,
                                      struct wsi_swapchain **swapchain_out)
{
   VkResult result;

   VkIcdSurfaceMetal *metal_surface = (VkIcdSurfaceMetal *)icd_surface;
   assert(metal_surface->pLayer);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);

   MTLPixelFormat metal_format;
   switch (pCreateInfo->imageFormat)
   {
      case VK_FORMAT_B8G8R8A8_SRGB:
         metal_format = MTLPixelFormatBGRA8Unorm_sRGB;
         break;
      case VK_FORMAT_B8G8R8A8_UNORM:
         metal_format = MTLPixelFormatBGRA8Unorm;
         break;
      case VK_FORMAT_R16G16B16A16_SFLOAT:
         metal_format = MTLPixelFormatRGBA16Float;
         break;
      case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
         metal_format = MTLPixelFormatRGB10A2Unorm;
         break;
      case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
         metal_format = MTLPixelFormatBGR10A2Unorm;
         break;
      default:
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
   }

   int num_images = pCreateInfo->minImageCount;

   struct wsi_metal_swapchain *chain;
   size_t size = sizeof(*chain) + num_images * sizeof(chain->images[0]);
   chain = vk_zalloc(pAllocator, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (chain == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   struct wsi_cpu_image_params cpu_params = {
      .base.image_type = WSI_IMAGE_TYPE_CPU,
   };

   result = wsi_swapchain_init(wsi_device, &chain->base, device,
                               pCreateInfo, &cpu_params.base, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(pAllocator, chain);
      return result;
   }

   chain->base.destroy = wsi_metal_swapchain_destroy;
   chain->base.get_wsi_image = wsi_metal_swapchain_get_wsi_image;
   chain->base.acquire_next_image = wsi_metal_swapchain_acquire_next_image;
   chain->base.queue_present = wsi_metal_swapchain_queue_present;
   chain->base.present_mode = wsi_swapchain_get_present_mode(wsi_device, pCreateInfo);
   chain->base.image_count = num_images;
   chain->extent = pCreateInfo->imageExtent;
   chain->vk_format = pCreateInfo->imageFormat;
   chain->surface = metal_surface;

   wsi_metal_layer_configure(metal_surface->pLayer,
      pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height,
      num_images, metal_format,
      pCreateInfo->compositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      pCreateInfo->presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR);

   chain->current_image_index = 0;
   for (uint32_t i = 0; i < chain->base.image_count; i++) {
      result = wsi_create_image(&chain->base, &chain->base.image_info,
                                &chain->images[i].base);
      if (result != VK_SUCCESS)
         return result;

      chain->images[i].drawable = NULL;
   }

   chain->blit_context = wsi_create_metal_layer_blit_context();

   *swapchain_out = &chain->base;

   return VK_SUCCESS;
}

VkResult
wsi_metal_init_wsi(struct wsi_device *wsi_device,
                    const VkAllocationCallbacks *alloc,
                    VkPhysicalDevice physical_device)
{
   struct wsi_metal *wsi;
   VkResult result;

   wsi = vk_alloc(alloc, sizeof(*wsi), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!wsi) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   wsi->physical_device = physical_device;
   wsi->alloc = alloc;
   wsi->wsi = wsi_device;

   wsi->base.get_support = wsi_metal_surface_get_support;
   wsi->base.get_capabilities2 = wsi_metal_surface_get_capabilities2;
   wsi->base.get_formats = wsi_metal_surface_get_formats;
   wsi->base.get_formats2 = wsi_metal_surface_get_formats2;
   wsi->base.get_present_modes = wsi_metal_surface_get_present_modes;
   wsi->base.get_present_rectangles = wsi_metal_surface_get_present_rectangles;
   wsi->base.create_swapchain = wsi_metal_surface_create_swapchain;

   wsi_device->wsi[VK_ICD_WSI_PLATFORM_METAL] = &wsi->base;

   return VK_SUCCESS;

fail:
   wsi_device->wsi[VK_ICD_WSI_PLATFORM_METAL] = NULL;

   return result;
}

void
wsi_metal_finish_wsi(struct wsi_device *wsi_device,
                    const VkAllocationCallbacks *alloc)
{
   struct wsi_metal *wsi =
      (struct wsi_metal *)wsi_device->wsi[VK_ICD_WSI_PLATFORM_METAL];
   if (!wsi)
      return;

   vk_free(alloc, wsi);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_CreateMetalSurfaceEXT(
   VkInstance _instance,
   const VkMetalSurfaceCreateInfoEXT* pCreateInfo,
   const VkAllocationCallbacks* pAllocator,
   VkSurfaceKHR* pSurface)
{
   VK_FROM_HANDLE(vk_instance, instance, _instance);
   VkIcdSurfaceMetal *surface;

   surface = vk_alloc2(&instance->alloc, pAllocator, sizeof *surface, 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (surface == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   surface->base.platform = VK_ICD_WSI_PLATFORM_METAL;
   surface->pLayer = pCreateInfo->pLayer;
   assert(surface->pLayer);

   *pSurface = VkIcdSurfaceBase_to_handle(&surface->base);
   return VK_SUCCESS;
}
