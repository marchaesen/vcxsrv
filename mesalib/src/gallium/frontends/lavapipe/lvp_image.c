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

#include "lvp_conv.h"
#include "util/u_sampler.h"
#include "util/u_inlines.h"

#define fix_depth_swizzle(x) do { \
  if (x > PIPE_SWIZZLE_X && x < PIPE_SWIZZLE_0) \
    x = PIPE_SWIZZLE_0;				\
  } while (0)
#define fix_depth_swizzle_a(x) do { \
  if (x > PIPE_SWIZZLE_X && x < PIPE_SWIZZLE_0) \
    x = PIPE_SWIZZLE_1;				\
  } while (0)

static struct pipe_sampler_view *
lvp_create_samplerview(struct pipe_context *pctx, struct lvp_image_view *iv)
{
   if (!iv)
      return NULL;

   struct pipe_sampler_view templ;
   enum pipe_format pformat;
   if (iv->vk.aspects == VK_IMAGE_ASPECT_DEPTH_BIT)
      pformat = lvp_vk_format_to_pipe_format(iv->vk.format);
   else if (iv->vk.aspects == VK_IMAGE_ASPECT_STENCIL_BIT)
      pformat = util_format_stencil_only(lvp_vk_format_to_pipe_format(iv->vk.format));
   else
      pformat = lvp_vk_format_to_pipe_format(iv->vk.format);
   u_sampler_view_default_template(&templ,
                                   iv->image->bo,
                                   pformat);
   if (iv->vk.view_type == VK_IMAGE_VIEW_TYPE_1D)
      templ.target = PIPE_TEXTURE_1D;
   if (iv->vk.view_type == VK_IMAGE_VIEW_TYPE_2D)
      templ.target = PIPE_TEXTURE_2D;
   if (iv->vk.view_type == VK_IMAGE_VIEW_TYPE_CUBE)
      templ.target = PIPE_TEXTURE_CUBE;
   if (iv->vk.view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
      templ.target = PIPE_TEXTURE_CUBE_ARRAY;
   templ.u.tex.first_layer = iv->vk.base_array_layer;
   templ.u.tex.last_layer = iv->vk.base_array_layer + iv->vk.layer_count - 1;
   templ.u.tex.first_level = iv->vk.base_mip_level;
   templ.u.tex.last_level = iv->vk.base_mip_level + iv->vk.level_count - 1;
   templ.swizzle_r = vk_conv_swizzle(iv->vk.swizzle.r);
   templ.swizzle_g = vk_conv_swizzle(iv->vk.swizzle.g);
   templ.swizzle_b = vk_conv_swizzle(iv->vk.swizzle.b);
   templ.swizzle_a = vk_conv_swizzle(iv->vk.swizzle.a);

   /* depth stencil swizzles need special handling to pass VK CTS
    * but also for zink GL tests.
    * piping A swizzle into R fixes GL_ALPHA depth texture mode
    * only swizzling from R/0/1 (for alpha) fixes VK CTS tests
    * and a bunch of zink tests.
   */
   if (iv->vk.aspects == VK_IMAGE_ASPECT_DEPTH_BIT ||
       iv->vk.aspects == VK_IMAGE_ASPECT_STENCIL_BIT) {
      fix_depth_swizzle(templ.swizzle_r);
      fix_depth_swizzle(templ.swizzle_g);
      fix_depth_swizzle(templ.swizzle_b);
      fix_depth_swizzle_a(templ.swizzle_a);
   }

   return pctx->create_sampler_view(pctx, iv->image->bo, &templ);
}

static struct pipe_image_view
lvp_create_imageview(const struct lvp_image_view *iv)
{
   struct pipe_image_view view = {0};
   if (!iv)
      return view;

   view.resource = iv->image->bo;
   if (iv->vk.aspects == VK_IMAGE_ASPECT_DEPTH_BIT)
      view.format = lvp_vk_format_to_pipe_format(iv->vk.format);
   else if (iv->vk.aspects == VK_IMAGE_ASPECT_STENCIL_BIT)
      view.format = util_format_stencil_only(lvp_vk_format_to_pipe_format(iv->vk.format));
   else
      view.format = lvp_vk_format_to_pipe_format(iv->vk.format);

   if (iv->vk.view_type == VK_IMAGE_VIEW_TYPE_3D) {
      view.u.tex.first_layer = 0;
      view.u.tex.last_layer = iv->vk.extent.depth - 1;
   } else {
      view.u.tex.first_layer = iv->vk.base_array_layer,
      view.u.tex.last_layer = iv->vk.base_array_layer + iv->vk.layer_count - 1;
   }
   view.u.tex.level = iv->vk.base_mip_level;
   return view;
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

   view = vk_image_view_create(&device->vk, false, pCreateInfo,
                               pAllocator, sizeof(*view));
   if (view == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   view->pformat = lvp_vk_format_to_pipe_format(view->vk.format);
   view->image = image;
   view->surface = NULL;
   view->iv = lvp_create_imageview(view);
   view->sv = lvp_create_samplerview(device->queue.ctx, view);
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

   pipe_sampler_view_reference(&iview->sv, NULL);
   pipe_surface_reference(&iview->surface, NULL);
   vk_image_view_destroy(&device->vk, pAllocator, &iview->vk);
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
   const VkBufferDeviceAddressInfo*            pInfo)
{
   LVP_FROM_HANDLE(lvp_buffer, buffer, pInfo->buffer);

   return (VkDeviceAddress)(uintptr_t)buffer->pmem;
}

VKAPI_ATTR uint64_t VKAPI_CALL lvp_GetBufferOpaqueCaptureAddress(
    VkDevice                                    device,
    const VkBufferDeviceAddressInfo*            pInfo)
{
   return 0;
}

VKAPI_ATTR uint64_t VKAPI_CALL lvp_GetDeviceMemoryOpaqueCaptureAddress(
    VkDevice                                    device,
    const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo)
{
   return 0;
}

static struct pipe_sampler_view *
lvp_create_samplerview_buffer(struct pipe_context *pctx, struct lvp_buffer_view *bv)
{
   if (!bv)
      return NULL;

   struct pipe_sampler_view templ;
   memset(&templ, 0, sizeof(templ));
   templ.target = PIPE_BUFFER;
   templ.swizzle_r = PIPE_SWIZZLE_X;
   templ.swizzle_g = PIPE_SWIZZLE_Y;
   templ.swizzle_b = PIPE_SWIZZLE_Z;
   templ.swizzle_a = PIPE_SWIZZLE_W;
   templ.format = bv->pformat;
   templ.u.buf.offset = bv->offset;
   templ.u.buf.size = bv->range;
   templ.texture = bv->buffer->bo;
   templ.context = pctx;
   return pctx->create_sampler_view(pctx, bv->buffer->bo, &templ);
}

static struct pipe_image_view
lvp_create_imageview_buffer(const struct lvp_buffer_view *bv)
{
   struct pipe_image_view view = {0};
   if (!bv)
      return view;
   view.resource = bv->buffer->bo;
   view.format = bv->pformat;
   view.u.buf.offset = bv->offset;
   view.u.buf.size = bv->range;
   return view;
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
   if (pCreateInfo->range == VK_WHOLE_SIZE)
      view->range = view->buffer->size - view->offset;
   else
      view->range = pCreateInfo->range;
   view->sv = lvp_create_samplerview_buffer(device->queue.ctx, view);
   view->iv = lvp_create_imageview_buffer(view);
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
   pipe_sampler_view_reference(&view->sv, NULL);
   vk_object_base_finish(&view->base);
   vk_free2(&device->vk.alloc, pAllocator, view);
}
