/*
 * Copyright © 2020 Intel Corporation
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
#ifndef VK_DEVICE_H
#define VK_DEVICE_H

#include "vk_dispatch_table.h"
#include "vk_extensions.h"
#include "vk_object.h"

#include "util/list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vk_device {
   struct vk_object_base base;
   VkAllocationCallbacks alloc;
   struct vk_physical_device *physical;

   struct vk_device_extension_table enabled_extensions;

   struct vk_device_dispatch_table dispatch_table;

   /* For VK_EXT_private_data */
   uint32_t private_data_next_index;

   struct list_head queues;

#ifdef ANDROID
   mtx_t swapchain_private_mtx;
   struct hash_table *swapchain_private;
#endif
};

VK_DEFINE_HANDLE_CASTS(vk_device, base, VkDevice,
                       VK_OBJECT_TYPE_DEVICE)

VkResult MUST_CHECK
vk_device_init(struct vk_device *device,
               struct vk_physical_device *physical_device,
               const struct vk_device_dispatch_table *dispatch_table,
               const VkDeviceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *alloc);

void
vk_device_finish(struct vk_device *device);

PFN_vkVoidFunction
vk_device_get_proc_addr(const struct vk_device *device,
                        const char *name);

bool vk_get_physical_device_core_1_1_feature_ext(struct VkBaseOutStructure *ext,
                                                 const VkPhysicalDeviceVulkan11Features *core);
bool vk_get_physical_device_core_1_2_feature_ext(struct VkBaseOutStructure *ext,
                                                 const VkPhysicalDeviceVulkan12Features *core);

bool vk_get_physical_device_core_1_1_property_ext(struct VkBaseOutStructure *ext,
                                                     const VkPhysicalDeviceVulkan11Properties *core);
bool vk_get_physical_device_core_1_2_property_ext(struct VkBaseOutStructure *ext,
                                                     const VkPhysicalDeviceVulkan12Properties *core);

#ifdef __cplusplus
}
#endif

#endif /* VK_DEVICE_H */
