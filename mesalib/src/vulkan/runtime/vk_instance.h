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
#ifndef VK_INSTANCE_H
#define VK_INSTANCE_H

#include "vk_dispatch_table.h"
#include "vk_extensions.h"
#include "vk_object.h"

#include "c11/threads.h"
#include "util/list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vk_app_info {
   const char*        app_name;
   uint32_t           app_version;
   const char*        engine_name;
   uint32_t           engine_version;
   uint32_t           api_version;
};

struct vk_instance {
   struct vk_object_base base;
   VkAllocationCallbacks alloc;

   struct vk_app_info app_info;
   struct vk_instance_extension_table enabled_extensions;

   struct vk_instance_dispatch_table dispatch_table;

   /* VK_EXT_debug_report debug callbacks */
   struct {
      mtx_t callbacks_mutex;
      struct list_head callbacks;
   } debug_report;

   /* VK_EXT_debug_utils */
   struct {
      /* These callbacks are only used while creating or destroying an
       * instance
       */
      struct list_head instance_callbacks;
      mtx_t callbacks_mutex;
      /* Persistent callbacks */
      struct list_head callbacks;
   } debug_utils;
};

VK_DEFINE_HANDLE_CASTS(vk_instance, base, VkInstance,
                       VK_OBJECT_TYPE_INSTANCE)

VkResult MUST_CHECK
vk_instance_init(struct vk_instance *instance,
                 const struct vk_instance_extension_table *supported_extensions,
                 const struct vk_instance_dispatch_table *dispatch_table,
                 const VkInstanceCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *alloc);

void
vk_instance_finish(struct vk_instance *instance);

VkResult
vk_enumerate_instance_extension_properties(
    const struct vk_instance_extension_table *supported_extensions,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties);

PFN_vkVoidFunction
vk_instance_get_proc_addr(const struct vk_instance *instance,
                          const struct vk_instance_entrypoint_table *entrypoints,
                          const char *name);

PFN_vkVoidFunction
vk_instance_get_proc_addr_unchecked(const struct vk_instance *instance,
                                    const char *name);

PFN_vkVoidFunction
vk_instance_get_physical_device_proc_addr(const struct vk_instance *instance,
                                          const char *name);

#ifdef __cplusplus
}
#endif

#endif /* VK_INSTANCE_H */
