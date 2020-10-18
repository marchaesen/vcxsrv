/*
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

#include "vk_debug_report.h"

#include "vk_alloc.h"
#include "vk_util.h"

VkResult vk_debug_report_instance_init(struct vk_debug_report_instance *instance)
{
   if (pthread_mutex_init(&instance->callbacks_mutex, NULL) != 0) {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   list_inithead(&instance->callbacks);

   return VK_SUCCESS;
}

void vk_debug_report_instance_destroy(struct vk_debug_report_instance *instance)
{
   pthread_mutex_destroy(&instance->callbacks_mutex);
}

VkResult
vk_create_debug_report_callback(struct vk_debug_report_instance *instance,
                                const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
                                const VkAllocationCallbacks* pAllocator,
                                const VkAllocationCallbacks* instance_allocator,
                                VkDebugReportCallbackEXT* pCallback)
{

   struct vk_debug_report_callback *cb =
      vk_alloc2(instance_allocator, pAllocator,
                sizeof(struct vk_debug_report_callback), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (!cb)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   cb->flags = pCreateInfo->flags;
   cb->callback = pCreateInfo->pfnCallback;
   cb->data = pCreateInfo->pUserData;

   pthread_mutex_lock(&instance->callbacks_mutex);
   list_addtail(&cb->link, &instance->callbacks);
   pthread_mutex_unlock(&instance->callbacks_mutex);

   *pCallback = (VkDebugReportCallbackEXT)(uintptr_t)cb;

   return VK_SUCCESS;
}

void
vk_destroy_debug_report_callback(struct vk_debug_report_instance *instance,
                                 VkDebugReportCallbackEXT _callback,
                                 const VkAllocationCallbacks* pAllocator,
                                 const VkAllocationCallbacks* instance_allocator)
{
   if (_callback == VK_NULL_HANDLE)
      return;

   struct vk_debug_report_callback *callback =
            (struct vk_debug_report_callback *)(uintptr_t)_callback;

   /* Remove from list and destroy given callback. */
   pthread_mutex_lock(&instance->callbacks_mutex);
   list_del(&callback->link);
   vk_free2(instance_allocator, pAllocator, callback);
   pthread_mutex_unlock(&instance->callbacks_mutex);
}


void
vk_debug_report(struct vk_debug_report_instance *instance,
                VkDebugReportFlagsEXT flags,
                VkDebugReportObjectTypeEXT object_type,
                uint64_t handle,
                size_t location,
                int32_t messageCode,
                const char* pLayerPrefix,
                const char *pMessage)
{
   /* Allow NULL for convinience, return if no callbacks registered. */
   if (!instance || list_is_empty(&instance->callbacks))
      return;

   pthread_mutex_lock(&instance->callbacks_mutex);

   /* Section 33.2 of the Vulkan 1.0.59 spec says:
    *
    *    "callback is an externally synchronized object and must not be
    *    used on more than one thread at a time. This means that
    *    vkDestroyDebugReportCallbackEXT must not be called when a callback
    *    is active."
    */
   list_for_each_entry(struct vk_debug_report_callback, cb,
                       &instance->callbacks, link) {
      if (cb->flags & flags)
         cb->callback(flags, object_type, handle, location, messageCode,
                      pLayerPrefix, pMessage, cb->data);
   }

   pthread_mutex_unlock(&instance->callbacks_mutex);
}
