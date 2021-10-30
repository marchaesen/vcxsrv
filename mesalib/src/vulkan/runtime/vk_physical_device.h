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
#ifndef VK_PHYSICAL_DEVICE_H
#define VK_PHYSICAL_DEVICE_H

#include "vk_dispatch_table.h"
#include "vk_extensions.h"
#include "vk_object.h"

#ifdef __cplusplus
extern "C" {
#endif

struct wsi_device;

struct vk_physical_device {
   struct vk_object_base base;
   struct vk_instance *instance;

   struct vk_device_extension_table supported_extensions;

   struct vk_physical_device_dispatch_table dispatch_table;

   struct wsi_device *wsi_device;
};

VK_DEFINE_HANDLE_CASTS(vk_physical_device, base, VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)

VkResult MUST_CHECK
vk_physical_device_init(struct vk_physical_device *physical_device,
                        struct vk_instance *instance,
                        const struct vk_device_extension_table *supported_extensions,
                        const struct vk_physical_device_dispatch_table *dispatch_table);

void
vk_physical_device_finish(struct vk_physical_device *physical_device);

VkResult
vk_physical_device_check_device_features(struct vk_physical_device *physical_device,
                                         const VkDeviceCreateInfo *pCreateInfo);

#ifdef __cplusplus
}
#endif

#endif /* VK_PHYSICAL_DEVICE_H */
