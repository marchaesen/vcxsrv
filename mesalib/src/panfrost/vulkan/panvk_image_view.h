/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_IMAGE_VIEW_H
#define PANVK_IMAGE_VIEW_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "vk_image.h"

#include "pan_texture.h"

#include "genxml/gen_macros.h"
#include "panvk_image.h"

struct panvk_priv_bo;

struct panvk_image_view {
   struct vk_image_view vk;

   struct pan_image_view pview;

   struct panvk_priv_mem mem;

   struct {
      union {
         struct mali_texture_packed tex[PANVK_MAX_PLANES];
         struct {
            struct mali_texture_packed tex;
            struct mali_texture_packed other_aspect_tex;
         } zs;
      };

#if PAN_ARCH <= 7
      /* Valhall passes a texture descriptor to the LEA_TEX instruction. */
      struct mali_attribute_buffer_packed img_attrib_buf[2];
#endif
   } descs;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_image_view, vk.base, VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW);

static_assert(offsetof(struct panvk_image_view, descs.zs.tex) ==
                       offsetof(struct panvk_image_view, descs.tex),
              "ZS texture descriptor must alias with color texture descriptor");

#endif
