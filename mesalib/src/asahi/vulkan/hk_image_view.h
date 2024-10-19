/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "agx_pack.h"
#include "hk_private.h"
#include "vk_image.h"

struct hk_device;

#define HK_MAX_PLANES      3
#define HK_MAX_IMAGE_DESCS (10 * HK_MAX_PLANES)

struct hk_image_view {
   struct vk_image_view vk;

   uint32_t descriptor_index[HK_MAX_IMAGE_DESCS];
   uint8_t descriptor_count;

   uint8_t plane_count;
   struct {
      uint8_t image_plane;

      /** Descriptors used for eMRT. We delay upload since we want them
       * contiguous in memory, although this could be reworked if we wanted.
       */
      struct agx_texture_packed emrt_texture;
      struct agx_pbe_packed emrt_pbe;

      /** Index in the image descriptor table for the sampled image descriptor */
      uint32_t sampled_desc_index;

      /** Index in the image descriptor table for the storage image descriptor */
      uint32_t storage_desc_index;

      /** Index in the image descriptor table for the readonly storage image
       * descriptor.
       */
      uint32_t ro_storage_desc_index;

      /** Index in the image descriptor table for the texture descriptor used
       * for background programs.
       */
      uint32_t background_desc_index;
      uint32_t layered_background_desc_index;

      /** Index in the image descriptor table for the texture descriptor used
       * for input attachments.
       */
      uint32_t ia_desc_index;

      /** Index in the image descriptor table for the PBE descriptor used for
       * end-of-tile programs.
       */
      uint32_t eot_pbe_desc_index;
      uint32_t layered_eot_pbe_desc_index;
   } planes[3];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(hk_image_view, vk.base, VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW)
