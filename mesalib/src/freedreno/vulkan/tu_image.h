/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_IMAGE_H
#define TU_IMAGE_H

#include "tu_common.h"

#define TU_MAX_PLANE_COUNT 3

#define tu_fdl_view_stencil(view, x) \
   (((view)->x & ~A6XX_##x##_COLOR_FORMAT__MASK) | A6XX_##x##_COLOR_FORMAT(FMT6_8_UINT))

#define tu_fdl_view_depth(view, x) \
   (((view)->x & ~A6XX_##x##_COLOR_FORMAT__MASK) | A6XX_##x##_COLOR_FORMAT(FMT6_32_FLOAT))

#define tu_image_view_stencil(iview, x) \
   tu_fdl_view_stencil(&iview->view, x)

#define tu_image_view_depth(iview, x) \
   tu_fdl_view_depth(&iview->view, x)

struct tu_image
{
   struct vk_image vk;

   struct fdl_layout layout[3];
   uint64_t total_size;

   /* Set when bound */
   struct tu_bo *bo;
   uint64_t bo_offset;
   uint64_t iova;

   /* For fragment density map */
   void *map;

   uint32_t lrz_height;
   uint32_t lrz_pitch;
   uint32_t lrz_offset;
   uint32_t lrz_fc_offset;
   bool has_lrz_fc;

   bool ubwc_enabled;
   bool force_linear_tile;
   bool ubwc_fc_mutable;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_image, vk.base, VkImage, VK_OBJECT_TYPE_IMAGE)

struct tu_image_view
{
   struct vk_image_view vk;

   struct tu_image *image; /**< VkImageViewCreateInfo::image */

   struct fdl6_view view;

   unsigned char swizzle[4];

   /* for d32s8 separate depth */
   uint64_t depth_base_addr;
   uint32_t depth_layer_size;
   uint32_t depth_pitch;

   /* for d32s8 separate stencil */
   uint64_t stencil_base_addr;
   uint32_t stencil_layer_size;
   uint32_t stencil_pitch;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_image_view, vk.base, VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW);

uint32_t tu6_plane_count(VkFormat format);

enum pipe_format tu6_plane_format(VkFormat format, uint32_t plane);

uint32_t tu6_plane_index(VkFormat format, VkImageAspectFlags aspect_mask);

enum pipe_format tu_format_for_aspect(enum pipe_format format,
                                      VkImageAspectFlags aspect_mask);

static inline enum pipe_format
tu_aspects_to_plane(VkFormat format, VkImageAspectFlags aspect_mask)
{
   uint32_t plane = tu6_plane_index(format, aspect_mask);
   return tu6_plane_format(format, plane);
}

uint64_t
tu_layer_address(const struct fdl6_view *iview, uint32_t layer);

void
tu_cs_image_ref(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer);

template <chip CHIP>
void
tu_cs_image_ref_2d(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer, bool src);

void
tu_cs_image_flag_ref(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer);

void
tu_cs_image_stencil_ref(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer);

void
tu_cs_image_depth_ref(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer);

bool
tiling_possible(VkFormat format);

bool
ubwc_possible(struct tu_device *device,
              VkFormat format,
              VkImageType type,
              VkImageUsageFlags usage,
              VkImageUsageFlags stencil_usage,
              const struct fd_dev_info *info,
              VkSampleCountFlagBits samples,
              bool use_z24uint_s8uint);

struct tu_frag_area {
   float width;
   float height;
};

void
tu_fragment_density_map_sample(const struct tu_image_view *fdm,
                               uint32_t x, uint32_t y,
                               uint32_t width, uint32_t height,
                               uint32_t layers, struct tu_frag_area *areas);

VkResult
tu_image_update_layout(struct tu_device *device, struct tu_image *image,
                       uint64_t modifier, const VkSubresourceLayout *plane_layouts);

#endif /* TU_IMAGE_H */
