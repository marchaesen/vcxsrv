/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_image.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "genxml/gen_macros.h"
#include "panvk_private.h"

#include "util/debug.h"
#include "util/u_atomic.h"
#include "vk_format.h"
#include "vk_object.h"
#include "vk_util.h"
#include "drm-uapi/drm_fourcc.h"

static enum mali_texture_dimension
panvk_view_type_to_mali_tex_dim(VkImageViewType type)
{
   switch (type) {
   case VK_IMAGE_VIEW_TYPE_1D:
   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
      return MALI_TEXTURE_DIMENSION_1D;
   case VK_IMAGE_VIEW_TYPE_2D:
   case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
      return MALI_TEXTURE_DIMENSION_2D;
   case VK_IMAGE_VIEW_TYPE_3D:
      return MALI_TEXTURE_DIMENSION_3D;
   case VK_IMAGE_VIEW_TYPE_CUBE:
   case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
      return MALI_TEXTURE_DIMENSION_CUBE;
   default:
      unreachable("Invalid view type");
   }
}

static void
panvk_convert_swizzle(const VkComponentMapping *in,
                      unsigned char *out)
{
   const VkComponentSwizzle *comp = &in->r;
   for (unsigned i = 0; i < 4; i++) {
      switch (comp[i]) {
      case VK_COMPONENT_SWIZZLE_IDENTITY:
         out[i] = PIPE_SWIZZLE_X + i;
         break;
      case VK_COMPONENT_SWIZZLE_ZERO:
         out[i] = PIPE_SWIZZLE_0;
         break;
      case VK_COMPONENT_SWIZZLE_ONE:
         out[i] = PIPE_SWIZZLE_1;
         break;
      case VK_COMPONENT_SWIZZLE_R:
         out[i] = PIPE_SWIZZLE_X;
         break;
      case VK_COMPONENT_SWIZZLE_G:
         out[i] = PIPE_SWIZZLE_Y;
         break;
      case VK_COMPONENT_SWIZZLE_B:
         out[i] = PIPE_SWIZZLE_Z;
         break;
      case VK_COMPONENT_SWIZZLE_A:
         out[i] = PIPE_SWIZZLE_W;
         break;
      default:
         unreachable("Invalid swizzle");
      }
   }
}

VkResult
panvk_per_arch(CreateImageView)(VkDevice _device,
                                const VkImageViewCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkImageView *pView)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_image, image, pCreateInfo->image);
   struct panvk_image_view *view;

   view = vk_object_zalloc(&device->vk, pAllocator, sizeof(*view),
                          VK_OBJECT_TYPE_IMAGE_VIEW);
   if (view == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   view->pview.format = vk_format_to_pipe_format(pCreateInfo->format);

   if (pCreateInfo->subresourceRange.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT)
      view->pview.format = util_format_get_depth_only(view->pview.format);
   else if (pCreateInfo->subresourceRange.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT)
      view->pview.format = util_format_stencil_only(view->pview.format);

   unsigned level_count =
      pCreateInfo->subresourceRange.levelCount == VK_REMAINING_MIP_LEVELS ?
      image->pimage.layout.nr_slices - pCreateInfo->subresourceRange.baseMipLevel :
      pCreateInfo->subresourceRange.levelCount;
   unsigned layer_count =
      pCreateInfo->subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS ?
      image->pimage.layout.array_size - pCreateInfo->subresourceRange.baseArrayLayer :
      pCreateInfo->subresourceRange.layerCount;

   view->pview.dim = panvk_view_type_to_mali_tex_dim(pCreateInfo->viewType);
   view->pview.first_level = pCreateInfo->subresourceRange.baseMipLevel;
   view->pview.last_level = pCreateInfo->subresourceRange.baseMipLevel + level_count - 1;
   view->pview.first_layer = pCreateInfo->subresourceRange.baseArrayLayer;
   view->pview.last_layer = pCreateInfo->subresourceRange.baseArrayLayer + layer_count - 1;
   panvk_convert_swizzle(&pCreateInfo->components, view->pview.swizzle);
   view->pview.image = &image->pimage;
   view->pview.nr_samples = image->pimage.layout.nr_samples;
   view->vk_format = pCreateInfo->format;

   struct panfrost_device *pdev = &device->physical_device->pdev;

   if (image->usage &
       (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) {
      unsigned bo_size =
         GENX(panfrost_estimate_texture_payload_size)(&view->pview) +
         pan_size(TEXTURE);

      unsigned surf_descs_offset = PAN_ARCH <= 5 ? pan_size(TEXTURE) : 0;

      view->bo = panfrost_bo_create(pdev, bo_size, 0, "Texture descriptor");

      struct panfrost_ptr surf_descs = {
         .cpu = view->bo->ptr.cpu + surf_descs_offset,
         .gpu = view->bo->ptr.gpu + surf_descs_offset,
      };
      void *tex_desc = PAN_ARCH >= 6 ?
                       &view->descs.tex : view->bo->ptr.cpu;

      STATIC_ASSERT(sizeof(view->descs.tex) >= pan_size(TEXTURE));
      GENX(panfrost_new_texture)(pdev, &view->pview, tex_desc, &surf_descs);
   }

   *pView = panvk_image_view_to_handle(view);
   return VK_SUCCESS;
}
