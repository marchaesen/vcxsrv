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

#ifndef RADV_ACCELERATION_STRUCTURE_H
#define RADV_ACCELERATION_STRUCTURE_H

#include <stdint.h>
#include <vulkan/vulkan.h>

struct radv_accel_struct_serialization_header {
   uint8_t driver_uuid[VK_UUID_SIZE];
   uint8_t accel_struct_compat[VK_UUID_SIZE];
   uint64_t serialization_size;
   uint64_t compacted_size;
   uint64_t instance_count;
   uint64_t instances[];
};

struct radv_accel_struct_header {
   uint32_t root_node_offset;
   uint32_t reserved;
   float aabb[2][3];

   /* Everything after this gets updated/copied from the CPU. */
   uint64_t compacted_size;
   uint64_t serialization_size;
   uint32_t copy_dispatch_size[3];
   uint64_t instance_offset;
   uint64_t instance_count;
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
   float aabb[2][3];
   uint32_t primitive_id;
   /* flags in upper 4 bits */
   uint32_t geometry_id_and_flags;
   uint32_t reserved[8];
};

struct radv_bvh_instance_node {
   uint64_t base_ptr;
   /* lower 24 bits are the custom instance index, upper 8 bits are the visibility mask */
   uint32_t custom_instance_and_mask;
   /* lower 24 bits are the sbt offset, upper 8 bits are VkGeometryInstanceFlagsKHR */
   uint32_t sbt_offset_and_flags;

   /* The translation component is actually a pre-translation instead of a post-translation. If you
    * want to get a proper matrix out of it you need to apply the directional component of the
    * matrix to it. The pre-translation of the world->object matrix is the same as the
    * post-translation of the object->world matrix so this way we can share data between both
    * matrices. */
   float wto_matrix[12];
   float aabb[2][3];
   uint32_t instance_id;

   /* Object to world matrix transposed from the initial transform. Translate part is store in the
    * wto_matrix. */
   float otw_matrix[9];
};

struct radv_bvh_box16_node {
   uint32_t children[4];
   uint32_t coords[4][3];
};

struct radv_bvh_box32_node {
   uint32_t children[4];
   float coords[4][2][3];
   uint32_t reserved[4];
};

#endif