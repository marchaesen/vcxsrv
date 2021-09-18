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
#ifndef VK_IMAGE_H
#define VK_IMAGE_H

#include "vk_object.h"

#include "util/u_math.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vk_image {
   struct vk_object_base base;

   VkImageCreateFlags create_flags;
   VkImageType image_type;
   VkFormat format;
   VkExtent3D extent;
   uint32_t mip_levels;
   uint32_t array_layers;
   VkSampleCountFlagBits samples;
   VkImageTiling tiling;
   VkImageUsageFlags usage;

   /* Derived from format */
   VkImageAspectFlags aspects;

   /* VK_EXT_separate_stencil_usage */
   VkImageUsageFlags stencil_usage;

   /* VK_KHR_external_memory */
   VkExternalMemoryHandleTypeFlags external_handle_types;

   /* wsi_image_create_info::scanout */
   bool wsi_legacy_scanout;

#ifndef _WIN32
   /* VK_EXT_drm_format_modifier
    *
    * Initialized by vk_image_create/init() to DRM_FORMAT_MOD_INVALID.  It's
    * the job of the driver to parse the VK_EXT_drm_format_modifier extension
    * structs and choose the actual modifier.
    *
    * Must be DRM_FORMAT_MOD_INVALID unless tiling is
    * VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT.
    */
   uint64_t drm_format_mod;
#endif

#ifdef ANDROID
   /* VK_ANDROID_external_memory_android_hardware_buffer */
   uint64_t android_external_format;
#endif
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vk_image, base, VkImage,
                               VK_OBJECT_TYPE_IMAGE);

void vk_image_init(struct vk_device *device,
                   struct vk_image *image,
                   const VkImageCreateInfo *pCreateInfo);
void vk_image_finish(struct vk_image *image);

void *vk_image_create(struct vk_device *device,
                      const VkImageCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *alloc,
                      size_t size);
void vk_image_destroy(struct vk_device *device,
                      const VkAllocationCallbacks *alloc,
                      struct vk_image *image);

void vk_image_set_format(struct vk_image *image, VkFormat format);

VkImageUsageFlags vk_image_usage(const struct vk_image *image,
                                 VkImageAspectFlags aspect_mask);

VkImageAspectFlags vk_image_expand_aspect_mask(const struct vk_image *image,
                                               VkImageAspectFlags aspect_mask);

static inline VkExtent3D
vk_image_mip_level_extent(const struct vk_image *image,
                          uint32_t mip_level)
{
   const VkExtent3D extent = {
      u_minify(image->extent.width,  mip_level),
      u_minify(image->extent.height, mip_level),
      u_minify(image->extent.depth,  mip_level),
   };
   return extent;
}

/* This is defined as a macro so that it works for both
 * VkImageSubresourceRange and VkImageSubresourceLayers
 */
#define vk_image_subresource_layer_count(_image, _range) \
   ((_range)->layerCount == VK_REMAINING_ARRAY_LAYERS ? \
    (_image)->array_layers - (_range)->baseArrayLayer : (_range)->layerCount)

static inline uint32_t
vk_image_subresource_level_count(const struct vk_image *image,
                                 const VkImageSubresourceRange *range)
{
   return range->levelCount == VK_REMAINING_MIP_LEVELS ?
          image->mip_levels - range->baseMipLevel : range->levelCount;
}

struct vk_image_view {
   struct vk_object_base base;

   VkImageViewCreateFlags create_flags;
   struct vk_image *image;
   VkImageViewType view_type;

   /** Image view format, relative to the selected aspects
    *
    * For a depth/stencil image:
    *
    *  - If vk_image_view::aspects contains both depth and stencil, this will
    *    be the full depth/stencil format of the image.
    *
    *  - If only one aspect is selected, this will be the depth-only or
    *    stencil-only format, as per the selected aspect.
    *
    * For color images, we have three cases:
    *
    *  1. It's a single-plane image in which case this is the unmodified
    *     format provided to VkImageViewCreateInfo::format.
    *
    *  2. It's a YCbCr view of a multi-plane image in which case the
    *     client will have asked for VK_IMAGE_ASPECT_COLOR_BIT and the
    *     format provided will be the full planar format.  In this case,
    *     the format will be the full format containing all the planes.
    *
    *  3. It's a single-plane view of a multi-plane image in which case
    *     the client will have asked for VK_IMAGE_ASPECT_PLANE_N_BIT and
    *     will have provided a format compatible with that specific
    *     plane of the multi-planar format.  In this case, the format will be
    *     the plane-compatible format requested by the client.
    */
   VkFormat format;

   /* Component mapping, aka swizzle
    *
    * Unlike the swizzle provided via VkImageViewCreateInfo::components, this
    * will never contain VK_COMPONENT_SWIZZLE_IDENTITY.  It will be resolved
    * to VK_COMPONENT_SWIZZLE_R/G/B/A, as appropriate.
    */
   VkComponentMapping swizzle;

   /** Aspects from the image represented by this view
    *
    * For depth/stencil images, this is the aspectMask provided by
    * VkImageViewCreateinfo::subresourceRange::aspectMask.
    *
    * For color images, we have three cases:
    *
    *  1. It's a single-plane image in which case this only aspect is
    *     VK_IMAGE_ASPECT_COLOR_BIT.
    *
    *  2. It's a YCbCr view of a multi-plane image in which case the
    *     client will have asked for VK_IMAGE_ASPECT_COLOR_BIT and the
    *     format provided will be the full planar format.  In this case,
    *     aspects will be the full set of plane aspects in the image.
    *
    *  3. It's a single-plane view of a multi-plane image in which case
    *     the client will have asked for VK_IMAGE_ASPECT_PLANE_N_BIT and
    *     will have provided a format compatible with that specific
    *     plane of the multi-planar format.  In this case, aspects will be
    *     VK_IMAGE_ASPECT_PLANE_N_BIT where N is the selected plane.
    *
    * This seems almost backwards from the API but ensures that
    * vk_image_view::aspects is always a subset of vk_image::aspects.
    */
   VkImageAspectFlags aspects;

   uint32_t base_mip_level;
   uint32_t level_count;
   uint32_t base_array_layer;
   uint32_t layer_count;

   /* Image extent at LOD 0 */
   VkExtent3D extent;

   /* VK_KHR_maintenance2 */
   VkImageUsageFlags usage;
};

void vk_image_view_init(struct vk_device *device,
                        struct vk_image_view *image_view,
                        const VkImageViewCreateInfo *pCreateInfo);
void vk_image_view_finish(struct vk_image_view *image_view);

void *vk_image_view_create(struct vk_device *device,
                           const VkImageViewCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *alloc,
                           size_t size);
void vk_image_view_destroy(struct vk_device *device,
                           const VkAllocationCallbacks *alloc,
                           struct vk_image_view *image_view);

bool vk_image_layout_is_read_only(VkImageLayout layout,
                                  VkImageAspectFlagBits aspect);
VkImageUsageFlags vk_image_layout_to_usage_flags(VkImageLayout layout,
                                                 VkImageAspectFlagBits aspect);

#ifdef __cplusplus
}
#endif

#endif /* VK_IMAGE_H */
