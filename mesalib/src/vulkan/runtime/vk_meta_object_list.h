/*
 * Copyright 2022 Collabora Ltd
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VK_META_OBJECT_LIST_H
#define VK_META_OBJECT_LIST_H

#include "vk_object.h"

#include "util/u_dynarray.h"

struct vk_device;

struct vk_meta_object_list {
   struct util_dynarray arr;
};

void vk_meta_object_list_init(struct vk_meta_object_list *mol);
void vk_meta_object_list_reset(struct vk_device *device,
                               struct vk_meta_object_list *mol);
void vk_meta_object_list_finish(struct vk_device *device,
                                struct vk_meta_object_list *mol);

static inline void
vk_meta_object_list_add_obj(struct vk_meta_object_list *mol,
                            struct vk_object_base *obj)
{
   util_dynarray_append(&mol->arr, struct vk_object_base *, obj);
}

static inline void
vk_meta_object_list_add_handle(struct vk_meta_object_list *mol,
                               VkObjectType obj_type,
                               uint64_t handle)
{
   vk_meta_object_list_add_obj(mol,
      vk_object_base_from_u64_handle(handle, obj_type));
}

void vk_meta_destroy_object(struct vk_device *device,
		            struct vk_object_base *obj);

#endif
