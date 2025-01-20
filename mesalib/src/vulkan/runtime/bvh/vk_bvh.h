/*
 * Copyright © 2021 Bas Nieuwenhuizen
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

#ifndef BVH_VK_BVH_H
#define BVH_VK_BVH_H

#define vk_ir_node_triangle 0
#define vk_ir_node_internal 1
#define vk_ir_node_instance 2
#define vk_ir_node_aabb     3

#define VK_GEOMETRY_OPAQUE (1u << 31)

#ifdef VULKAN
#define VK_UUID_SIZE 16
#else
#include <vulkan/vulkan.h>
typedef struct vk_ir_node vk_ir_node;
typedef struct vk_global_sync_data vk_global_sync_data;
typedef struct vk_bvh_geometry_data vk_bvh_geometry_data;

typedef struct {
   float values[3][4];
} mat3x4;

typedef struct {
   float x;
   float y;
   float z;
} vec3;

typedef struct vk_aabb vk_aabb;
#endif

struct vk_aabb {
   vec3 min;
   vec3 max;
};

/* This is the header structure for serialized acceleration structures, as
 * defined by the Vulkan spec.
 */
struct vk_accel_struct_serialization_header {
   uint8_t driver_uuid[VK_UUID_SIZE];
   uint8_t accel_struct_compat[VK_UUID_SIZE];
   uint64_t serialization_size;
   uint64_t deserialization_size;
   uint64_t instance_count;
#ifndef VULKAN
   uint64_t instances[];
#endif
};

struct vk_global_sync_data {
   uint32_t task_counts[2];
   uint32_t task_started_counter;
   uint32_t task_done_counter;
   uint32_t current_phase_start_counter;
   uint32_t current_phase_end_counter;
   uint32_t phase_index;
   /* If this flag is set, the shader should exit
    * instead of executing another phase */
   uint32_t next_phase_exit_flag;
};

struct vk_ir_header {
   int32_t min_bounds[3];
   int32_t max_bounds[3];
   uint32_t active_leaf_count;
   /* Indirect dispatch dimensions for the encoder.
    * ir_internal_node_count is the thread count in the X dimension,
    * while Y and Z are always set to 1. */
   uint32_t ir_internal_node_count;
   uint32_t dispatch_size_y;
   uint32_t dispatch_size_z;
   vk_global_sync_data sync_data;
   uint32_t dst_node_offset;
};

struct vk_ir_node {
   vk_aabb aabb;
};

#define VK_UNKNOWN_BVH_OFFSET 0xFFFFFFFF
#define VK_NULL_BVH_OFFSET    0xFFFFFFFE

struct vk_ir_box_node {
   vk_ir_node base;
   uint32_t children[2];
   uint32_t bvh_offset;
};

struct vk_ir_aabb_node {
   vk_ir_node base;
   uint32_t primitive_id;
   uint32_t geometry_id_and_flags;
};

struct vk_ir_triangle_node {
   vk_ir_node base;
   float coords[3][3];
   uint32_t triangle_id;
   uint32_t id;
   uint32_t geometry_id_and_flags;
};

struct vk_ir_instance_node {
   vk_ir_node base;
   /* See radv_bvh_instance_node */
   uint64_t base_ptr;
   uint32_t custom_instance_and_mask;
   uint32_t sbt_offset_and_flags;
   mat3x4 otw_matrix;
   uint32_t instance_id;
};

#define VK_BVH_INVALID_NODE 0xFFFFFFFF

/* If the task index is set to this value, there is no
 * more work to do. */
#define TASK_INDEX_INVALID 0xFFFFFFFF

struct vk_bvh_geometry_data {
   uint64_t data;
   uint64_t indices;
   uint64_t transform;

   uint32_t geometry_id;
   uint32_t geometry_type;
   uint32_t first_id;
   uint32_t stride;
   uint32_t vertex_format;
   uint32_t index_format;
};

#endif
