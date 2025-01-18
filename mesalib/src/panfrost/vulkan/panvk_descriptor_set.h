/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_DESCRIPTOR_SET_H
#define PANVK_DESCRIPTOR_SET_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "util/bitset.h"
#include "util/vma.h"

#include "panvk_macros.h"

#include "vk_descriptor_update_template.h"
#include "vk_object.h"

#include "panvk_descriptor_set_layout.h"

struct panvk_priv_bo;
struct panvk_sysvals;
struct panvk_descriptor_set_layout;

struct panvk_opaque_desc {
   uint32_t data[PANVK_DESCRIPTOR_SIZE / sizeof(uint32_t)];
};

#if PAN_ARCH < 9
struct panvk_ssbo_addr {
   uint64_t base_addr;
   uint32_t size;
   uint32_t zero[5]; /* Must be zero! */
};
#endif

struct panvk_descriptor_set {
   struct vk_object_base base;
   const struct panvk_descriptor_set_layout *layout;
   struct {
      uint64_t dev;
      void *host;
   } descs;

   struct {
      uint64_t dev_addr;
      uint64_t size;
   } dyn_bufs[MAX_DYNAMIC_BUFFERS];

   /* Includes adjustment for variable-sized descriptors */
   unsigned desc_count;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_descriptor_set, base, VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)

struct panvk_descriptor_pool {
   struct vk_object_base base;
   struct panvk_priv_bo *desc_bo;
   struct util_vma_heap desc_heap;

   /* Initialize to ones */
   BITSET_WORD *free_sets;

   uint32_t max_sets;
   struct panvk_descriptor_set *sets;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_descriptor_pool, base, VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)

VkResult panvk_per_arch(descriptor_set_write)(struct panvk_descriptor_set *set,
                                              const VkWriteDescriptorSet *write,
                                              bool write_immutable_samplers);

void panvk_per_arch(descriptor_set_write_template)(
   struct panvk_descriptor_set *set,
   const struct vk_descriptor_update_template *template, const void *data,
   bool write_immutable_samplers);

#endif /* PANVK_VX_DESCRIPTOR_SET_H */
