/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_BUFFER_H
#define TU_BUFFER_H

#include "tu_common.h"

#include "vk_buffer.h"

struct tu_buffer
{
   struct vk_buffer vk;

   struct tu_bo *bo;
   uint64_t iova;
   uint64_t bo_size;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(tu_buffer, vk.base, VkBuffer,
                               VK_OBJECT_TYPE_BUFFER)

#endif /* TU_BUFFER_H */
