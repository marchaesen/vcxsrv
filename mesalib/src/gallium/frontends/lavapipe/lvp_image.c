/*
 * Copyright © 2019 Red Hat.
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

#include "lvp_private.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "pipe/p_state.h"

static VkResult
lvp_image_create(VkDevice _device,
                 const VkImageCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks* alloc,
                 VkImage *pImage)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_image *image;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);

   image = vk_image_create(&device->vk, pCreateInfo, alloc, sizeof(*image));
   if (image == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   image->alignment = 16;
   {
      struct pipe_resource template;

      memset(&template, 0, sizeof(template));

      template.screen = device->pscreen;
      switch (pCreateInfo->imageType) {
      case VK_IMAGE_TYPE_1D:
         template.target = pCreateInfo->arrayLayers > 1 ? PIPE_TEXTURE_1D_ARRAY : PIPE_TEXTURE_1D;
         break;
      default:
      case VK_IMAGE_TYPE_2D:
         template.target = pCreateInfo->arrayLayers > 1 ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D;
         break;
      case VK_IMAGE_TYPE_3D:
         template.target = PIPE_TEXTURE_3D;
         break;
      }

      template.format = lvp_vk_format_to_pipe_format(pCreateInfo->format);

      bool is_ds = util_format_is_depth_or_stencil(template.format);

      if (pCreateInfo->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
         template.bind |= PIPE_BIND_RENDER_TARGET;
         /* sampler view is needed for resolve blits */
         if (pCreateInfo->samples > 1)
            template.bind |= PIPE_BIND_SAMPLER_VIEW;
      }

      if (pCreateInfo->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
         if (!is_ds)
            template.bind |= PIPE_BIND_RENDER_TARGET;
         else
            template.bind |= PIPE_BIND_DEPTH_STENCIL;
      }

      if (pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
         template.bind |= PIPE_BIND_DEPTH_STENCIL;

      if (pCreateInfo->usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
         template.bind |= PIPE_BIND_SAMPLER_VIEW;

      if (pCreateInfo->usage & VK_IMAGE_USAGE_STORAGE_BIT)
         template.bind |= PIPE_BIND_SHADER_IMAGE;

      template.width0 = pCreateInfo->extent.width;
      template.height0 = pCreateInfo->extent.height;
      template.depth0 = pCreateInfo->extent.depth;
      template.array_size = pCreateInfo->arrayLayers;
      template.last_level = pCreateInfo->mipLevels - 1;
      template.nr_samples = pCreateInfo->samples;
      template.nr_storage_samples = pCreateInfo->samples;
      image->bo = device->pscreen->resource_create_unbacked(device->pscreen,
                                                            &template,
                                                            &image->size);
      if (!image->bo)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }
   *pImage = lvp_image_to_handle(image);

   return VK_SUCCESS;
}

struct lvp_image *
lvp_swapchain_get_image(VkSwapchainKHR swapchain,
                        uint32_t index)
{
   VkImage image = wsi_common_get_image(swapchain, index);
   return lvp_image_from_handle(image);
}

static VkResult
lvp_image_from_swapchain(VkDevice device,
                         const VkImageCreateInfo *pCreateInfo,
                         const VkImageSwapchainCreateInfoKHR *swapchain_info,
                         const VkAllocationCallbacks *pAllocator,
                         VkImage *pImage)
{
   ASSERTED struct lvp_image *swapchain_image = lvp_swapchain_get_image(swapchain_info->swapchain, 0);
   assert(swapchain_image);

   assert(swapchain_image->vk.image_type == pCreateInfo->imageType);

   VkImageCreateInfo local_create_info;
   local_create_info = *pCreateInfo;
   local_create_info.pNext = NULL;
   /* The following parameters are implictly selected by the wsi code. */
   local_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
   local_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
   local_create_info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

   assert(!(local_create_info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));
   return lvp_image_create(device, &local_create_info, pAllocator,
                           pImage);
}

VKAPI_ATTR VkResult VKAPI_CALL
lvp_CreateImage(VkDevice device,
                const VkImageCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator,
                VkImage *pImage)
{
   const VkImageSwapchainCreateInfoKHR *swapchain_info =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_SWAPCHAIN_CREATE_INFO_KHR);
   if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE)
      return lvp_image_from_swapchain(device, pCreateInfo, swapchain_info,
                                      pAllocator, pImage);
   return lvp_image_create(device, pCreateInfo, pAllocator,
                           pImage);
}

VKAPI_ATTR void VKAPI_CALL
lvp_DestroyImage(VkDevice _device, VkImage _image,
                 const VkAllocationCallbacks *pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_image, image, _image);

   if (!_image)
     return;
   pipe_resource_reference(&image->bo, NULL);
   vk_image_destroy(&device->vk, pAllocator, &image->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
lvp_CreateImageView(VkDevice _device,
                    const VkImageViewCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkImageView *pView)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_image, image, pCreateInfo->image);
   struct lvp_image_view *view;

   view = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*view), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (view == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &view->base,
                       VK_OBJECT_TYPE_IMAGE_VIEW);
   view->view_type = pCreateInfo->viewType;
   view->format = pCreateInfo->format;
   view->pformat = lvp_vk_format_to_pipe_format(pCreateInfo->format);
   view->components = pCreateInfo->components;
   view->subresourceRange = pCreateInfo->subresourceRange;
   view->image = image;
   view->surface = NULL;
   *pView = lvp_image_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
lvp_DestroyImageView(VkDevice _device, VkImageView _iview,
                     const VkAllocationCallbacks *pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_image_view, iview, _iview);

   if (!_iview)
     return;

   pipe_surface_reference(&iview->surface, NULL);
   vk_object_base_finish(&iview->base);
   vk_free2(&device->vk.alloc, pAllocator, iview);
}

VKAPI_ATTR void VKAPI_CALL lvp_GetImageSubresourceLayout(
    VkDevice                                    _device,
    VkImage                                     _image,
    const VkImageSubresource*                   pSubresource,
    VkSubresourceLayout*                        pLayout)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_image, image, _image);
   uint64_t value;

   device->pscreen->resource_get_param(device->pscreen,
                                       NULL,
                                       image->bo,
                                       0,
                                       pSubresource->arrayLayer,
                                       pSubresource->mipLevel,
                                       PIPE_RESOURCE_PARAM_STRIDE,
                                       0, &value);

   pLayout->rowPitch = value;

   device->pscreen->resource_get_param(device->pscreen,
                                       NULL,
                                       image->bo,
                                       0,
                                       pSubresource->arrayLayer,
                                       pSubresource->mipLevel,
                                       PIPE_RESOURCE_PARAM_OFFSET,
                                       0, &value);

   pLayout->offset = value;

   device->pscreen->resource_get_param(device->pscreen,
                                       NULL,
                                       image->bo,
                                       0,
                                       pSubresource->arrayLayer,
                                       pSubresource->mipLevel,
                                       PIPE_RESOURCE_PARAM_LAYER_STRIDE,
                                       0, &value);

   if (image->bo->target == PIPE_TEXTURE_3D) {
      pLayout->depthPitch = value;
      pLayout->arrayPitch = 0;
   } else {
      pLayout->depthPitch = 0;
      pLayout->arrayPitch = value;
   }
   pLayout->size = image->size;

   switch (pSubresource->aspectMask) {
   case VK_IMAGE_ASPECT_COLOR_BIT:
      break;
   case VK_IMAGE_ASPECT_DEPTH_BIT:
      break;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      break;
   default:
      assert(!"Invalid image aspect");
   }
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateBuffer(
    VkDevice                                    _device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_buffer *buffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);

   /* gallium has max 32-bit buffer sizes */
   if (pCreateInfo->size > UINT32_MAX)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   buffer = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*buffer), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (buffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &buffer->base, VK_OBJECT_TYPE_BUFFER);
   buffer->size = pCreateInfo->size;
   buffer->usage = pCreateInfo->usage;
   buffer->offset = 0;

   {
      struct pipe_resource template;
      memset(&template, 0, sizeof(struct pipe_resource));

      if (pCreateInfo->usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
         template.bind |= PIPE_BIND_CONSTANT_BUFFER;

      template.screen = device->pscreen;
      template.target = PIPE_BUFFER;
      template.format = PIPE_FORMAT_R8_UNORM;
      template.width0 = buffer->size;
      template.height0 = 1;
      template.depth0 = 1;
      template.array_size = 1;
      if (buffer->usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
         template.bind |= PIPE_BIND_SAMPLER_VIEW;
      if (buffer->usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
         template.bind |= PIPE_BIND_SHADER_BUFFER;
      if (buffer->usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)
         template.bind |= PIPE_BIND_SHADER_IMAGE;
      template.flags = PIPE_RESOURCE_FLAG_DONT_OVER_ALLOCATE;
      buffer->bo = device->pscreen->resource_create_unbacked(device->pscreen,
                                                             &template,
                                                             &buffer->total_size);
      if (!buffer->bo) {
         vk_free2(&device->vk.alloc, pAllocator, buffer);
         return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      }
   }
   *pBuffer = lvp_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyBuffer(
    VkDevice                                    _device,
    VkBuffer                                    _buffer,
    const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_buffer, buffer, _buffer);

   if (!_buffer)
     return;

   pipe_resource_reference(&buffer->bo, NULL);
   vk_object_base_finish(&buffer->base);
   vk_free2(&device->vk.alloc, pAllocator, buffer);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL lvp_GetBufferDeviceAddress(
   VkDevice                                    device,
   const VkBufferDeviceAddressInfoKHR*         pInfo)
{
   LVP_FROM_HANDLE(lvp_buffer, buffer, pInfo->buffer);

   return (VkDeviceAddress)(uintptr_t)buffer->pmem;
}

VKAPI_ATTR uint64_t VKAPI_CALL lvp_GetBufferOpaqueCaptureAddress(
    VkDevice                                    device,
    const VkBufferDeviceAddressInfoKHR*         pInfo)
{
   return 0;
}

VKAPI_ATTR uint64_t VKAPI_CALL lvp_GetDeviceMemoryOpaqueCaptureAddress(
    VkDevice                                    device,
    const VkDeviceMemoryOpaqueCaptureAddressInfoKHR* pInfo)
{
   return 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
lvp_CreateBufferView(VkDevice _device,
                     const VkBufferViewCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkBufferView *pView)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_buffer, buffer, pCreateInfo->buffer);
   struct lvp_buffer_view *view;
   view = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*view), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!view)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &view->base,
                       VK_OBJECT_TYPE_BUFFER_VIEW);
   view->buffer = buffer;
   view->format = pCreateInfo->format;
   view->pformat = lvp_vk_format_to_pipe_format(pCreateInfo->format);
   view->offset = pCreateInfo->offset;
   view->range = pCreateInfo->range;
   *pView = lvp_buffer_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
lvp_DestroyBufferView(VkDevice _device, VkBufferView bufferView,
                      const VkAllocationCallbacks *pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_buffer_view, view, bufferView);

   if (!bufferView)
     return;
   vk_object_base_finish(&view->base);
   vk_free2(&device->vk.alloc, pAllocator, view);
}
