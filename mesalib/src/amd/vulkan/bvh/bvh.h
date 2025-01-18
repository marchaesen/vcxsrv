/*
 * Copyright © 2021 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BVH_BVH_H
#define BVH_BVH_H

#include "vk_bvh.h"

#define radv_bvh_node_triangle 0
#define radv_bvh_node_box16    4
#define radv_bvh_node_box32    5
#define radv_bvh_node_instance 6
#define radv_bvh_node_aabb     7

#define RADV_GEOMETRY_OPAQUE (1u << 31)

#define RADV_INSTANCE_FORCE_OPAQUE                 (1u << 31)
#define RADV_INSTANCE_NO_FORCE_NOT_OPAQUE          (1u << 30)
#define RADV_INSTANCE_TRIANGLE_FACING_CULL_DISABLE (1u << 29)
#define RADV_INSTANCE_TRIANGLE_FLIP_FACING         (1u << 28)

#ifdef VULKAN
#define VK_UUID_SIZE 16
#else
#include <vulkan/vulkan.h>
typedef uint16_t float16_t;
#endif

struct radv_accel_struct_serialization_header {
   uint8_t driver_uuid[VK_UUID_SIZE];
   uint8_t accel_struct_compat[VK_UUID_SIZE];
   uint64_t serialization_size;
   uint64_t compacted_size;
   uint64_t instance_count;
#ifndef VULKAN
   uint64_t instances[];
#endif
};

struct radv_accel_struct_geometry_info {
   uint32_t primitive_count;
   uint32_t flags;
   uint32_t type;
};

struct radv_accel_struct_header {
   uint32_t bvh_offset;
   uint32_t reserved;
   vk_aabb aabb;

   /* Everything after this gets either updated/copied from the CPU or written by header.comp. */
   uint64_t compacted_size;
   uint64_t serialization_size;
   uint32_t copy_dispatch_size[3];
   uint64_t size;

   /* Everything after this gets updated/copied from the CPU. */
   uint32_t geometry_count;
   uint64_t instance_offset;
   uint64_t instance_count;
   uint32_t build_flags;
};

struct radv_bvh_triangle_node {
   float coords[3][3];
   uint32_t reserved[3];
   uint32_t triangle_id;
   /* flags in upper 4 bits */
   uint32_t geometry_id_and_flags;
   uint32_t reserved2;
   uint32_t id;
};

struct radv_bvh_aabb_node {
   uint32_t primitive_id;
   /* flags in upper 4 bits */
   uint32_t geometry_id_and_flags;
   uint32_t reserved[14];
};

struct radv_bvh_instance_node {
   uint64_t bvh_ptr; /* pre-shifted/masked to serve as node base */

   /* lower 24 bits are the custom instance index, upper 8 bits are the visibility mask */
   uint32_t custom_instance_and_mask;
   /* lower 24 bits are the sbt offset, upper 8 bits are VkGeometryInstanceFlagsKHR */
   uint32_t sbt_offset_and_flags;

   mat3x4 wto_matrix;

   uint32_t instance_id;
   uint32_t bvh_offset;
   uint32_t reserved[2];

   /* Object to world matrix transposed from the initial transform. */
   mat3x4 otw_matrix;
};

struct radv_bvh_box16_node {
   uint32_t children[4];
   float16_t coords[4][2][3];
};

struct radv_bvh_box32_node {
   uint32_t children[4];
   vk_aabb coords[4];
   uint32_t reserved[4];
};

#define RADV_BVH_ROOT_NODE    radv_bvh_node_box32
#define RADV_BVH_INVALID_NODE 0xffffffffu

#endif /* BVH_H */
