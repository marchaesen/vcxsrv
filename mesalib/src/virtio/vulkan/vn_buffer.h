/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_BUFFER_H
#define VN_BUFFER_H

#include "vn_common.h"

struct vn_buffer {
   struct vn_object_base base;

   VkMemoryRequirements2 memory_requirements;
   VkMemoryDedicatedRequirements dedicated_requirements;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_buffer,
                               base.base,
                               VkBuffer,
                               VK_OBJECT_TYPE_BUFFER)

struct vn_buffer_view {
   struct vn_object_base base;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_buffer_view,
                               base.base,
                               VkBufferView,
                               VK_OBJECT_TYPE_BUFFER_VIEW)

#endif /* VN_BUFFER_H */
