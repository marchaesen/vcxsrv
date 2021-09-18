/*
 * Copyright © 2021 Intel Corporation
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

#include "vk_image.h"

#include <vulkan/vulkan_android.h>

#ifndef _WIN32
#include <drm-uapi/drm_fourcc.h>
#endif

#include "vk_alloc.h"
#include "vk_common_entrypoints.h"
#include "vk_device.h"
#include "vk_format.h"
#include "vk_util.h"
#include "vulkan/wsi/wsi_common.h"

static VkExtent3D
sanitize_image_extent(const VkImageType imageType,
                      const VkExtent3D imageExtent)
{
   switch (imageType) {
   case VK_IMAGE_TYPE_1D:
      return (VkExtent3D) { imageExtent.width, 1, 1 };
   case VK_IMAGE_TYPE_2D:
      return (VkExtent3D) { imageExtent.width, imageExtent.height, 1 };
   case VK_IMAGE_TYPE_3D:
      return imageExtent;
   default:
      unreachable("invalid image type");
   }
}

void
vk_image_init(struct vk_device *device,
              struct vk_image *image,
              const VkImageCreateInfo *pCreateInfo)
{
   vk_object_base_init(device, &image->base, VK_OBJECT_TYPE_IMAGE);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
   assert(pCreateInfo->mipLevels > 0);
   assert(pCreateInfo->arrayLayers > 0);
   assert(pCreateInfo->samples > 0);
   assert(pCreateInfo->extent.width > 0);
   assert(pCreateInfo->extent.height > 0);
   assert(pCreateInfo->extent.depth > 0);

   if (pCreateInfo->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
      assert(pCreateInfo->imageType == VK_IMAGE_TYPE_2D);
   if (pCreateInfo->flags & VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT)
      assert(pCreateInfo->imageType == VK_IMAGE_TYPE_3D);

   image->create_flags = pCreateInfo->flags;
   image->image_type = pCreateInfo->imageType;
   vk_image_set_format(image, pCreateInfo->format);
   image->extent = sanitize_image_extent(pCreateInfo->imageType,
                                         pCreateInfo->extent);
   image->mip_levels = pCreateInfo->mipLevels;
   image->array_layers = pCreateInfo->arrayLayers;
   image->samples = pCreateInfo->samples;
   image->tiling = pCreateInfo->tiling;
   image->usage = pCreateInfo->usage;

   if (image->aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
      const VkImageStencilUsageCreateInfoEXT *stencil_usage_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_STENCIL_USAGE_CREATE_INFO_EXT);
      image->stencil_usage =
         stencil_usage_info ? stencil_usage_info->stencilUsage :
                              pCreateInfo->usage;
   } else {
      image->stencil_usage = 0;
   }

   const VkExternalMemoryImageCreateInfo *ext_mem_info =
      vk_find_struct_const(pCreateInfo->pNext, EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
   if (ext_mem_info)
      image->external_handle_types = ext_mem_info->handleTypes;
   else
      image->external_handle_types = 0;

   const struct wsi_image_create_info *wsi_info =
      vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);
   image->wsi_legacy_scanout = wsi_info && wsi_info->scanout;

#ifndef _WIN32
   image->drm_format_mod = ((1ULL << 56) - 1) /* DRM_FORMAT_MOD_INVALID */;
#endif

#ifdef ANDROID
   const VkExternalFormatANDROID *ext_format =
      vk_find_struct_const(pCreateInfo->pNext, EXTERNAL_FORMAT_ANDROID);
   if (ext_format && ext_format->externalFormat != 0) {
      assert(image->format == VK_FORMAT_UNDEFINED);
      assert(image->external_handle_types &
             VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID);
      image->android_external_format = ext_format->externalFormat;
   } else {
      image->android_external_format = 0;
   }
#endif
}

void *
vk_image_create(struct vk_device *device,
                const VkImageCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *alloc,
                size_t size)
{
   struct vk_image *image =
      vk_zalloc2(&device->alloc, alloc, size, 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (image == NULL)
      return NULL;

   vk_image_init(device, image, pCreateInfo);

   return image;
}

void
vk_image_finish(struct vk_image *image)
{
   vk_object_base_finish(&image->base);
}

void
vk_image_destroy(struct vk_device *device,
                 const VkAllocationCallbacks *alloc,
                 struct vk_image *image)
{
   vk_object_free(device, alloc, image);
}

#ifndef _WIN32
VKAPI_ATTR VkResult VKAPI_CALL
vk_common_GetImageDrmFormatModifierPropertiesEXT(UNUSED VkDevice device,
                                                 VkImage _image,
                                                 VkImageDrmFormatModifierPropertiesEXT *pProperties)
{
   VK_FROM_HANDLE(vk_image, image, _image);

   assert(pProperties->sType ==
          VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT);

   assert(image->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT);
   pProperties->drmFormatModifier = image->drm_format_mod;

   return VK_SUCCESS;
}
#endif

void
vk_image_set_format(struct vk_image *image, VkFormat format)
{
   image->format = format;
   image->aspects = vk_format_aspects(format);
}

VkImageUsageFlags
vk_image_usage(const struct vk_image *image,
               VkImageAspectFlags aspect_mask)
{
   assert(!(aspect_mask & ~image->aspects));

   /* From the Vulkan 1.2.131 spec:
    *
    *    "If the image was has a depth-stencil format and was created with
    *    a VkImageStencilUsageCreateInfo structure included in the pNext
    *    chain of VkImageCreateInfo, the usage is calculated based on the
    *    subresource.aspectMask provided:
    *
    *     - If aspectMask includes only VK_IMAGE_ASPECT_STENCIL_BIT, the
    *       implicit usage is equal to
    *       VkImageStencilUsageCreateInfo::stencilUsage.
    *
    *     - If aspectMask includes only VK_IMAGE_ASPECT_DEPTH_BIT, the
    *       implicit usage is equal to VkImageCreateInfo::usage.
    *
    *     - If both aspects are included in aspectMask, the implicit usage
    *       is equal to the intersection of VkImageCreateInfo::usage and
    *       VkImageStencilUsageCreateInfo::stencilUsage.
    */
   if (aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT) {
      return image->stencil_usage;
   } else if (aspect_mask == (VK_IMAGE_ASPECT_DEPTH_BIT |
                              VK_IMAGE_ASPECT_STENCIL_BIT)) {
      return image->usage & image->stencil_usage;
   } else {
      /* This also handles the color case */
      return image->usage;
   }
}

#define VK_IMAGE_ASPECT_ANY_COLOR_MASK_MESA ( \
   VK_IMAGE_ASPECT_COLOR_BIT | \
   VK_IMAGE_ASPECT_PLANE_0_BIT | \
   VK_IMAGE_ASPECT_PLANE_1_BIT | \
   VK_IMAGE_ASPECT_PLANE_2_BIT)

/** Expands the given aspect mask relative to the image
 *
 * If the image has color plane aspects VK_IMAGE_ASPECT_COLOR_BIT has been
 * requested, this returns the aspects of the underlying image.
 *
 * For example,
 *
 *    VK_IMAGE_ASPECT_COLOR_BIT
 *
 * will be converted to
 *
 *    VK_IMAGE_ASPECT_PLANE_0_BIT |
 *    VK_IMAGE_ASPECT_PLANE_1_BIT |
 *    VK_IMAGE_ASPECT_PLANE_2_BIT
 *
 * for an image of format VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM.
 */
VkImageAspectFlags
vk_image_expand_aspect_mask(const struct vk_image *image,
                            VkImageAspectFlags aspect_mask)
{
   if (aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT) {
      assert(image->aspects & VK_IMAGE_ASPECT_ANY_COLOR_MASK_MESA);
      return image->aspects;
   } else {
      assert(aspect_mask && !(aspect_mask & ~image->aspects));
      return aspect_mask;
   }
}

static VkComponentSwizzle
remap_swizzle(VkComponentSwizzle swizzle, VkComponentSwizzle component)
{
   return swizzle == VK_COMPONENT_SWIZZLE_IDENTITY ? component : swizzle;
}

void
vk_image_view_init(struct vk_device *device,
                   struct vk_image_view *image_view,
                   const VkImageViewCreateInfo *pCreateInfo)
{
   vk_object_base_init(device, &image_view->base, VK_OBJECT_TYPE_IMAGE_VIEW);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
   VK_FROM_HANDLE(vk_image, image, pCreateInfo->image);

   image_view->create_flags = pCreateInfo->flags;
   image_view->image = image;
   image_view->view_type = pCreateInfo->viewType;

   switch (image_view->view_type) {
   case VK_IMAGE_VIEW_TYPE_1D:
   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
      assert(image->image_type == VK_IMAGE_TYPE_1D);
      break;
   case VK_IMAGE_VIEW_TYPE_2D:
   case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
      if (image->create_flags & VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT)
         assert(image->image_type == VK_IMAGE_TYPE_3D);
      else
         assert(image->image_type == VK_IMAGE_TYPE_2D);
      break;
   case VK_IMAGE_VIEW_TYPE_3D:
      assert(image->image_type == VK_IMAGE_TYPE_3D);
      break;
   case VK_IMAGE_VIEW_TYPE_CUBE:
   case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
      assert(image->image_type == VK_IMAGE_TYPE_2D);
      assert(image->create_flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
      break;
   default:
      unreachable("Invalid image view type");
   }

   const VkImageSubresourceRange *range = &pCreateInfo->subresourceRange;

   /* Some drivers may want to create color views of depth/stencil images
    * to implement certain operations, which is not strictly allowed by the
    * Vulkan spec, so handle this case separately.
    */
   bool is_color_view_of_depth_stencil =
      vk_format_is_depth_or_stencil(image->format) &&
      vk_format_is_color(pCreateInfo->format);
   if (is_color_view_of_depth_stencil) {
      assert(range->aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
      assert(util_format_get_blocksize(vk_format_to_pipe_format(image->format)) ==
             util_format_get_blocksize(vk_format_to_pipe_format(pCreateInfo->format)));
      image_view->aspects = range->aspectMask;
   } else {
      image_view->aspects =
         vk_image_expand_aspect_mask(image, range->aspectMask);

      /* From the Vulkan 1.2.184 spec:
       *
       *    "If the image has a multi-planar format and
       *    subresourceRange.aspectMask is VK_IMAGE_ASPECT_COLOR_BIT, and image
       *    has been created with a usage value not containing any of the
       *    VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR,
       *    VK_IMAGE_USAGE_VIDEO_DECODE_SRC_BIT_KHR,
       *    VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR,
       *    VK_IMAGE_USAGE_VIDEO_ENCODE_DST_BIT_KHR,
       *    VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR, and
       *    VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR flags, then the format must
       *    be identical to the image format, and the sampler to be used with the
       *    image view must enable sampler Y′CBCR conversion."
       *
       * Since no one implements video yet, we can ignore the bits about video
       * create flags and assume YCbCr formats match.
       */
      if ((image->aspects & VK_IMAGE_ASPECT_PLANE_1_BIT) &&
          (range->aspectMask == VK_IMAGE_ASPECT_COLOR_BIT))
         assert(pCreateInfo->format == image->format);

      /* From the Vulkan 1.2.184 spec:
       *
       *    "Each depth/stencil format is only compatible with itself."
       */
      if (image_view->aspects & (VK_IMAGE_ASPECT_DEPTH_BIT |
                                 VK_IMAGE_ASPECT_STENCIL_BIT))
         assert(pCreateInfo->format == image->format);

      if (!(image->create_flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT))
         assert(pCreateInfo->format == image->format);
   }

   /* Restrict the format to only the planes chosen.
    *
    * For combined depth and stencil images, this means the depth-only or
    * stencil-only format if only one aspect is chosen and the full combined
    * format if both aspects are chosen.
    *
    * For single-plane color images, we just take the format as-is.  For
    * multi-plane views of multi-plane images, this means we want the full
    * multi-plane format.  For single-plane views of multi-plane images, we
    * want a format compatible with the one plane.  Fortunately, this is
    * already what the client gives us.  The Vulkan 1.2.184 spec says:
    *
    *    "If image was created with the VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT and
    *    the image has a multi-planar format, and if
    *    subresourceRange.aspectMask is VK_IMAGE_ASPECT_PLANE_0_BIT,
    *    VK_IMAGE_ASPECT_PLANE_1_BIT, or VK_IMAGE_ASPECT_PLANE_2_BIT, format
    *    must be compatible with the corresponding plane of the image, and the
    *    sampler to be used with the image view must not enable sampler Y′CBCR
    *    conversion."
    */
   if (image_view->aspects == VK_IMAGE_ASPECT_STENCIL_BIT) {
      image_view->format = vk_format_stencil_only(pCreateInfo->format);
   } else if (image_view->aspects == VK_IMAGE_ASPECT_DEPTH_BIT) {
      image_view->format = vk_format_depth_only(pCreateInfo->format);
   } else {
      image_view->format = pCreateInfo->format;
   }

   image_view->swizzle = (VkComponentMapping) {
      .r = remap_swizzle(pCreateInfo->components.r, VK_COMPONENT_SWIZZLE_R),
      .g = remap_swizzle(pCreateInfo->components.g, VK_COMPONENT_SWIZZLE_G),
      .b = remap_swizzle(pCreateInfo->components.b, VK_COMPONENT_SWIZZLE_B),
      .a = remap_swizzle(pCreateInfo->components.a, VK_COMPONENT_SWIZZLE_A),
   };

   assert(range->layerCount > 0);
   assert(range->baseMipLevel < image->mip_levels);

   image_view->base_mip_level = range->baseMipLevel;
   image_view->level_count = vk_image_subresource_level_count(image, range);
   image_view->base_array_layer = range->baseArrayLayer;
   image_view->layer_count = vk_image_subresource_layer_count(image, range);

   image_view->extent =
      vk_image_mip_level_extent(image, image_view->base_mip_level);

   assert(image_view->base_mip_level + image_view->level_count
          <= image->mip_levels);
   switch (image->image_type) {
   default:
      unreachable("bad VkImageType");
   case VK_IMAGE_TYPE_1D:
   case VK_IMAGE_TYPE_2D:
      assert(image_view->base_array_layer + image_view->layer_count
             <= image->array_layers);
      break;
   case VK_IMAGE_TYPE_3D:
      assert(image_view->base_array_layer + image_view->layer_count
             <= image_view->extent.depth);
      break;
   }

   /* If we are creating a color view from a depth/stencil image we compute
    * usage from the underlying depth/stencil aspects.
    */
   const VkImageUsageFlags image_usage = is_color_view_of_depth_stencil ?
      vk_image_usage(image, image->aspects) :
      vk_image_usage(image, image_view->aspects);
   const VkImageViewUsageCreateInfo *usage_info =
      vk_find_struct_const(pCreateInfo, IMAGE_VIEW_USAGE_CREATE_INFO);
   image_view->usage = usage_info ? usage_info->usage : image_usage;
   assert(!(image_view->usage & ~image_usage));
}

void
vk_image_view_finish(struct vk_image_view *image_view)
{
   vk_object_base_finish(&image_view->base);
}

void *
vk_image_view_create(struct vk_device *device,
                     const VkImageViewCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *alloc,
                     size_t size)
{
   struct vk_image_view *image_view =
      vk_zalloc2(&device->alloc, alloc, size, 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (image_view == NULL)
      return NULL;

   vk_image_view_init(device, image_view, pCreateInfo);

   return image_view;
}

void
vk_image_view_destroy(struct vk_device *device,
                      const VkAllocationCallbacks *alloc,
                      struct vk_image_view *image_view)
{
   vk_object_free(device, alloc, image_view);
}

bool
vk_image_layout_is_read_only(VkImageLayout layout,
                             VkImageAspectFlagBits aspect)
{
   assert(util_bitcount(aspect) == 1);

   switch (layout) {
   case VK_IMAGE_LAYOUT_UNDEFINED:
   case VK_IMAGE_LAYOUT_PREINITIALIZED:
      return true; /* These are only used for layout transitions */

   case VK_IMAGE_LAYOUT_GENERAL:
   case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
   case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
   case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR:
   case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
   case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
      return false;

   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
   case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
   case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
   case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
   case VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV:
   case VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT:
   case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
   case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
      return true;

   case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
      return aspect == VK_IMAGE_ASPECT_DEPTH_BIT;

   case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
      return aspect == VK_IMAGE_ASPECT_STENCIL_BIT;

   case VK_IMAGE_LAYOUT_MAX_ENUM:
   case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR:
   case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR:
      unreachable("Invalid image layout.");
   }

   unreachable("Invalid image layout.");
}

VkImageUsageFlags
vk_image_layout_to_usage_flags(VkImageLayout layout,
                               VkImageAspectFlagBits aspect)
{
   assert(util_bitcount(aspect) == 1);

   switch (layout) {
   case VK_IMAGE_LAYOUT_UNDEFINED:
   case VK_IMAGE_LAYOUT_PREINITIALIZED:
      return 0u;

   case VK_IMAGE_LAYOUT_GENERAL:
      return ~0u;

   case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      assert(aspect & VK_IMAGE_ASPECT_ANY_COLOR_MASK_MESA);
      return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      assert(aspect & (VK_IMAGE_ASPECT_DEPTH_BIT |
                       VK_IMAGE_ASPECT_STENCIL_BIT));
      return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

   case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
      assert(aspect & VK_IMAGE_ASPECT_DEPTH_BIT);
      return vk_image_layout_to_usage_flags(
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspect);

   case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
      assert(aspect & VK_IMAGE_ASPECT_STENCIL_BIT);
      return vk_image_layout_to_usage_flags(
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspect);

   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
      assert(aspect & (VK_IMAGE_ASPECT_DEPTH_BIT |
                       VK_IMAGE_ASPECT_STENCIL_BIT));
      return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
             VK_IMAGE_USAGE_SAMPLED_BIT |
             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

   case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
      assert(aspect & VK_IMAGE_ASPECT_DEPTH_BIT);
      return vk_image_layout_to_usage_flags(
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, aspect);

   case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
      assert(aspect & VK_IMAGE_ASPECT_STENCIL_BIT);
      return vk_image_layout_to_usage_flags(
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, aspect);

   case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      return VK_IMAGE_USAGE_SAMPLED_BIT |
             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

   case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

   case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      return VK_IMAGE_USAGE_TRANSFER_DST_BIT;

   case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
      if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
         return vk_image_layout_to_usage_flags(
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, aspect);
      } else if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
         return vk_image_layout_to_usage_flags(
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspect);
      } else {
         assert(!"Must be a depth/stencil aspect");
         return 0;
      }

   case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
      if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
         return vk_image_layout_to_usage_flags(
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspect);
      } else if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
         return vk_image_layout_to_usage_flags(
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, aspect);
      } else {
         assert(!"Must be a depth/stencil aspect");
         return 0;
      }

   case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      assert(aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      /* This needs to be handled specially by the caller */
      return 0;

   case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR:
      assert(aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      return vk_image_layout_to_usage_flags(VK_IMAGE_LAYOUT_GENERAL, aspect);

   case VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV:
      assert(aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      return VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV;

   case VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT:
      assert(aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      return VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT;

   case VK_IMAGE_LAYOUT_MAX_ENUM:
   case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR:
   case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR:
      unreachable("Invalid image layout.");
   }

   unreachable("Invalid image layout.");
}
