/*
 * Copyright © 2018, Google Inc.
 *
 * based on the anv driver which is:
 * Copyright © 2017 Intel Corporation
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
#ifndef VK_DEBUG_REPORT_H
#define VK_DEBUG_REPORT_H

#include <pthread.h>

#include "util/list.h"
#include <vulkan/vulkan.h>

struct vk_debug_report_callback {
   /* Link in the 'callbacks' list in anv_instance struct. */
   struct list_head                             link;
   VkDebugReportFlagsEXT                        flags;
   PFN_vkDebugReportCallbackEXT                 callback;
   void *                                       data;
};

struct vk_debug_report_instance {
   /* VK_EXT_debug_report debug callbacks */
   pthread_mutex_t                             callbacks_mutex;
   struct list_head                            callbacks;
};

VkResult vk_debug_report_instance_init(struct vk_debug_report_instance *instance);
void vk_debug_report_instance_destroy(struct vk_debug_report_instance *instance);

VkResult
vk_create_debug_report_callback(struct vk_debug_report_instance *instance,
                                const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
                                const VkAllocationCallbacks* pAllocator,
                                const VkAllocationCallbacks* instance_allocator,
                                VkDebugReportCallbackEXT* pCallback);
void
vk_destroy_debug_report_callback(struct vk_debug_report_instance *instance,
                                 VkDebugReportCallbackEXT _callback,
                                 const VkAllocationCallbacks* pAllocator,
                                 const VkAllocationCallbacks* instance_allocator);

void
vk_debug_report(struct vk_debug_report_instance *instance,
                VkDebugReportFlagsEXT flags,
                VkDebugReportObjectTypeEXT object_type,
                uint64_t handle,
                size_t location,
                int32_t messageCode,
                const char* pLayerPrefix,
                const char *pMessage);
#endif
