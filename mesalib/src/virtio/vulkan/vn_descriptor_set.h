/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_DESCRIPTOR_SET_H
#define VN_DESCRIPTOR_SET_H

#include "vn_common.h"

/* TODO accommodate new discrete type enums by:
 * 1. increase the number of types here
 * 2. add a helper to map to continuous array index
 */
#define VN_NUM_DESCRIPTOR_TYPES (VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT + 1)

struct vn_descriptor_set_layout_binding {
   VkDescriptorType type;
   uint32_t count;
   bool has_immutable_samplers;
};

struct vn_descriptor_set_layout {
   struct vn_object_base base;

   uint32_t last_binding;
   bool has_variable_descriptor_count;

   /* bindings must be the last field in the layout */
   struct vn_descriptor_set_layout_binding bindings[];
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_descriptor_set_layout,
                               base.base,
                               VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)

struct vn_descriptor_pool_state {
   uint32_t set_count;
   uint32_t descriptor_counts[VN_NUM_DESCRIPTOR_TYPES];
};

struct vn_descriptor_pool {
   struct vn_object_base base;

   VkAllocationCallbacks allocator;
   bool async_set_allocation;
   struct vn_descriptor_pool_state max;
   struct vn_descriptor_pool_state used;

   struct list_head descriptor_sets;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_descriptor_pool,
                               base.base,
                               VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)

struct vn_update_descriptor_sets {
   uint32_t write_count;
   VkWriteDescriptorSet *writes;
   VkDescriptorImageInfo *images;
   VkDescriptorBufferInfo *buffers;
   VkBufferView *views;
};

struct vn_descriptor_set {
   struct vn_object_base base;

   const struct vn_descriptor_set_layout *layout;
   uint32_t last_binding_descriptor_count;

   struct list_head head;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_descriptor_set,
                               base.base,
                               VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)

struct vn_descriptor_update_template_entry {
   size_t offset;
   size_t stride;
};

struct vn_descriptor_update_template {
   struct vn_object_base base;

   mtx_t mutex;
   struct vn_update_descriptor_sets *update;

   struct vn_descriptor_update_template_entry entries[];
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_descriptor_update_template,
                               base.base,
                               VkDescriptorUpdateTemplate,
                               VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE)

#endif /* VN_DESCRIPTOR_SET_H */
