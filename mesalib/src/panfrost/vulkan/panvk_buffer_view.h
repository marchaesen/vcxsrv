/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_BUFFER_VIEW_H
#define PANVK_BUFFER_VIEW_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "panvk_mempool.h"

#include "vk_buffer_view.h"

#include "genxml/gen_macros.h"

struct panvk_buffer_view {
   struct vk_buffer_view vk;
   struct panvk_priv_mem mem;

   struct {
      struct mali_texture_packed tex;

#if PAN_ARCH <= 7
      /* Valhall passes a texture descriptor to the LEA_TEX instruction. */
      struct mali_attribute_buffer_packed img_attrib_buf[2];
#endif
   } descs;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_buffer_view, vk.base, VkBufferView,
                               VK_OBJECT_TYPE_BUFFER_VIEW)

#endif
