/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_COMMON_H
#define VN_COMMON_H

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "c11/threads.h"
#include "util/bitscan.h"
#include "util/compiler.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/os_time.h"
#include "util/u_math.h"
#include "util/xmlconfig.h"
#include "vk_alloc.h"
#include "vk_debug_report.h"
#include "vk_device.h"
#include "vk_instance.h"
#include "vk_object.h"
#include "vk_physical_device.h"
#include "vk_util.h"

#include "vn_entrypoints.h"

#define VN_DEFAULT_ALIGN 8

#define VN_DEBUG(category) (unlikely(vn_debug & VN_DEBUG_##category))

#define vn_error(instance, error)                                            \
   (VN_DEBUG(RESULT) ? vn_log_result((instance), (error), __func__) : (error))
#define vn_result(instance, result)                                          \
   ((result) >= VK_SUCCESS ? (result) : vn_error((instance), (result)))

struct vn_instance;
struct vn_physical_device;
struct vn_device;
struct vn_queue;
struct vn_fence;
struct vn_semaphore;
struct vn_device_memory;
struct vn_buffer;
struct vn_buffer_view;
struct vn_image;
struct vn_image_view;
struct vn_sampler;
struct vn_sampler_ycbcr_conversion;
struct vn_descriptor_set_layout;
struct vn_descriptor_pool;
struct vn_descriptor_set;
struct vn_descriptor_update_template;
struct vn_render_pass;
struct vn_framebuffer;
struct vn_event;
struct vn_query_pool;
struct vn_shader_module;
struct vn_pipeline_layout;
struct vn_pipeline_cache;
struct vn_pipeline;
struct vn_command_pool;
struct vn_command_buffer;

struct vn_cs_encoder;
struct vn_cs_decoder;

struct vn_renderer;
struct vn_renderer_shmem;
struct vn_renderer_bo;
struct vn_renderer_sync;

enum vn_debug {
   VN_DEBUG_INIT = 1ull << 0,
   VN_DEBUG_RESULT = 1ull << 1,
   VN_DEBUG_VTEST = 1ull << 2,
   VN_DEBUG_WSI = 1ull << 3,
};

typedef uint64_t vn_object_id;

/* base class of vn_instance */
struct vn_instance_base {
   struct vk_instance base;
   vn_object_id id;
};

/* base class of vn_physical_device */
struct vn_physical_device_base {
   struct vk_physical_device base;
   vn_object_id id;
};

/* base class of vn_device */
struct vn_device_base {
   struct vk_device base;
   vn_object_id id;
};

/* base class of other driver objects */
struct vn_object_base {
   struct vk_object_base base;
   vn_object_id id;
};

extern uint64_t vn_debug;

void
vn_debug_init(void);

void
vn_log(struct vn_instance *instance, const char *format, ...)
   PRINTFLIKE(2, 3);

VkResult
vn_log_result(struct vn_instance *instance,
              VkResult result,
              const char *where);

void
vn_relax(uint32_t *iter, const char *reason);

static_assert(sizeof(vn_object_id) >= sizeof(uintptr_t), "");

static inline VkResult
vn_instance_base_init(
   struct vn_instance_base *instance,
   const struct vk_instance_extension_table *supported_extensions,
   const struct vk_instance_dispatch_table *dispatch_table,
   const VkInstanceCreateInfo *info,
   const VkAllocationCallbacks *alloc)
{
   VkResult result = vk_instance_init(&instance->base, supported_extensions,
                                      dispatch_table, info, alloc);
   instance->id = (uintptr_t)instance;
   return result;
}

static inline void
vn_instance_base_fini(struct vn_instance_base *instance)
{
   vk_instance_finish(&instance->base);
}

static inline VkResult
vn_physical_device_base_init(
   struct vn_physical_device_base *physical_dev,
   struct vn_instance_base *instance,
   const struct vk_device_extension_table *supported_extensions,
   const struct vk_physical_device_dispatch_table *dispatch_table)
{
   VkResult result =
      vk_physical_device_init(&physical_dev->base, &instance->base,
                              supported_extensions, dispatch_table);
   physical_dev->id = (uintptr_t)physical_dev;
   return result;
}

static inline void
vn_physical_device_base_fini(struct vn_physical_device_base *physical_dev)
{
   vk_physical_device_finish(&physical_dev->base);
}

static inline VkResult
vn_device_base_init(struct vn_device_base *dev,
                    struct vn_physical_device_base *physical_dev,
                    const struct vk_device_dispatch_table *dispatch_table,
                    const VkDeviceCreateInfo *info,
                    const VkAllocationCallbacks *alloc)
{
   VkResult result = vk_device_init(&dev->base, &physical_dev->base,
                                    dispatch_table, info, alloc);
   dev->id = (uintptr_t)dev;
   return result;
}

static inline void
vn_device_base_fini(struct vn_device_base *dev)
{
   vk_device_finish(&dev->base);
}

static inline void
vn_object_base_init(struct vn_object_base *obj,
                    VkObjectType type,
                    struct vn_device_base *dev)
{
   vk_object_base_init(&dev->base, &obj->base, type);
   obj->id = (uintptr_t)obj;
}

static inline void
vn_object_base_fini(struct vn_object_base *obj)
{
   vk_object_base_finish(&obj->base);
}

static inline void
vn_object_set_id(void *obj, vn_object_id id, VkObjectType type)
{
   assert(((const struct vk_object_base *)obj)->type == type);
   switch (type) {
   case VK_OBJECT_TYPE_INSTANCE:
      ((struct vn_instance_base *)obj)->id = id;
      break;
   case VK_OBJECT_TYPE_PHYSICAL_DEVICE:
      ((struct vn_physical_device_base *)obj)->id = id;
      break;
   case VK_OBJECT_TYPE_DEVICE:
      ((struct vn_device_base *)obj)->id = id;
      break;
   default:
      ((struct vn_object_base *)obj)->id = id;
      break;
   }
}

static inline vn_object_id
vn_object_get_id(const void *obj, VkObjectType type)
{
   assert(((const struct vk_object_base *)obj)->type == type);
   switch (type) {
   case VK_OBJECT_TYPE_INSTANCE:
      return ((struct vn_instance_base *)obj)->id;
   case VK_OBJECT_TYPE_PHYSICAL_DEVICE:
      return ((struct vn_physical_device_base *)obj)->id;
   case VK_OBJECT_TYPE_DEVICE:
      return ((struct vn_device_base *)obj)->id;
   default:
      return ((struct vn_object_base *)obj)->id;
   }
}

#endif /* VN_COMMON_H */
