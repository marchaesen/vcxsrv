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
#ifndef VK_OBJECT_H
#define VK_OBJECT_H

#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

#include "c11/threads.h"
#include "util/macros.h"
#include "util/sparse_array.h"

#ifdef __cplusplus
extern "C" {
#endif

struct hash_table;

struct vk_device;

struct vk_object_base {
   VK_LOADER_DATA _loader_data;
   VkObjectType type;

   struct vk_device *device;

   /* For VK_EXT_private_data */
   struct util_sparse_array private_data;
};

void vk_object_base_init(UNUSED struct vk_device *device,
                         struct vk_object_base *base,
                         UNUSED VkObjectType obj_type);
void vk_object_base_finish(UNUSED struct vk_object_base *base);
void vk_object_base_reset(struct vk_object_base *base);

static inline void
vk_object_base_assert_valid(ASSERTED struct vk_object_base *base,
                            ASSERTED VkObjectType obj_type)
{
   assert(base == NULL || base->type == obj_type);
}

static inline struct vk_object_base *
vk_object_base_from_u64_handle(uint64_t handle, VkObjectType obj_type)
{
   struct vk_object_base *base = (struct vk_object_base *)(uintptr_t)handle;
   vk_object_base_assert_valid(base, obj_type);
   return base;
}

#define VK_DEFINE_HANDLE_CASTS(__driver_type, __base, __VkType, __VK_TYPE) \
   static inline struct __driver_type *                                    \
   __driver_type ## _from_handle(__VkType _handle)                         \
   {                                                                       \
      struct vk_object_base *base = (struct vk_object_base *)_handle;      \
      vk_object_base_assert_valid(base, __VK_TYPE);                        \
      STATIC_ASSERT(offsetof(struct __driver_type, __base) == 0);          \
      return (struct __driver_type *) base;                                \
   }                                                                       \
                                                                           \
   static inline __VkType                                                  \
   __driver_type ## _to_handle(struct __driver_type *_obj)                 \
   {                                                                       \
      vk_object_base_assert_valid(&_obj->__base, __VK_TYPE);               \
      return (__VkType) _obj;                                              \
   }

#define VK_DEFINE_NONDISP_HANDLE_CASTS(__driver_type, __base, __VkType, __VK_TYPE) \
   static inline struct __driver_type *                                    \
   __driver_type ## _from_handle(__VkType _handle)                         \
   {                                                                       \
      struct vk_object_base *base =                                        \
         (struct vk_object_base *)(uintptr_t)_handle;                      \
      vk_object_base_assert_valid(base, __VK_TYPE);                        \
      STATIC_ASSERT(offsetof(struct __driver_type, __base) == 0);          \
      return (struct __driver_type *)base;                                 \
   }                                                                       \
                                                                           \
   static inline __VkType                                                  \
   __driver_type ## _to_handle(struct __driver_type *_obj)                 \
   {                                                                       \
      vk_object_base_assert_valid(&_obj->__base, __VK_TYPE);               \
      return (__VkType)(uintptr_t) _obj;                                   \
   }

#define VK_FROM_HANDLE(__driver_type, __name, __handle) \
   struct __driver_type *__name = __driver_type ## _from_handle(__handle)

/* Helpers for vk object (de)allocation and (de)initialization */
void *
vk_object_alloc(struct vk_device *device,
                const VkAllocationCallbacks *alloc,
                size_t size,
                VkObjectType vk_obj_type);

void *
vk_object_zalloc(struct vk_device *device,
                const VkAllocationCallbacks *alloc,
                size_t size,
                VkObjectType vk_obj_type);

void
vk_object_free(struct vk_device *device,
               const VkAllocationCallbacks *alloc,
               void *data);


struct vk_private_data_slot {
   struct vk_object_base base;
   uint32_t index;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vk_private_data_slot, base,
                               VkPrivateDataSlotEXT,
                               VK_OBJECT_TYPE_PRIVATE_DATA_SLOT_EXT);

VkResult
vk_private_data_slot_create(struct vk_device *device,
                            const VkPrivateDataSlotCreateInfoEXT* pCreateInfo,
                            const VkAllocationCallbacks* pAllocator,
                            VkPrivateDataSlotEXT* pPrivateDataSlot);
void
vk_private_data_slot_destroy(struct vk_device *device,
                             VkPrivateDataSlotEXT privateDataSlot,
                             const VkAllocationCallbacks *pAllocator);
VkResult
vk_object_base_set_private_data(struct vk_device *device,
                                VkObjectType objectType,
                                uint64_t objectHandle,
                                VkPrivateDataSlotEXT privateDataSlot,
                                uint64_t data);
void
vk_object_base_get_private_data(struct vk_device *device,
                                VkObjectType objectType,
                                uint64_t objectHandle,
                                VkPrivateDataSlotEXT privateDataSlot,
                                uint64_t *pData);

#ifdef __cplusplus
}
#endif

#endif /* VK_OBJECT_H */
