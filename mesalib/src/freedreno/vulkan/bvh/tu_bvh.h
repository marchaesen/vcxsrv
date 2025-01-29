/*
 * Copyright © 2021 Bas Nieuwenhuizen
 * Copyright © 2024 Valve Corporation
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

#ifndef TU_BVH_H
#define TU_BVH_H

#ifdef VULKAN
#define VK_UUID_SIZE 16
#else
#include <vulkan/vulkan.h>
#endif

#include "vk_bvh.h"

/* The size in bytes of each record in the D3D-style UAV descriptor for
 * acceleration structures. The first record is the acceleration struct header
 * and the rest are the instances.
 */
#define AS_RECORD_SIZE 128

/* The size of a BVH node as defined by the HW. */
#define AS_NODE_SIZE 64

struct tu_accel_struct_header {
   vk_aabb aabb;

   uint64_t bvh_ptr;

   /* This word contains flags that should be set in the leaf nodes for
    * instances pointing to this BLAS. ALL_NODES_{OPAQUE_NONOPAQUE} may be
    * modified by the FORCE_OPAQUE and FORCE_NON_OPAQUE instance flags.
    */
   uint32_t instance_flags;

   /* Everything after this gets either updated/copied from the CPU or written by header.comp. */
   uint32_t copy_dispatch_size[3];

   uint64_t compacted_size;
   uint64_t serialization_size;
   uint64_t size;

   /* Everything after this gets updated/copied from the CPU. */
   uint64_t instance_count;

   uint64_t self_ptr;

   uint32_t padding[10];
};

/* See
 * https://gitlab.freedesktop.org/freedreno/freedreno/-/wikis/a7xx-ray-tracing
 * for details of the encoding.
 */

#define TU_NODE_TYPE_TLAS      (1u << 24)
#define TU_NODE_TYPE_LEAF      (1u << 25)
#define TU_NODE_TYPE_NONOPAQUE (1u << 26)
#define TU_NODE_TYPE_AABB      (1u << 27)

#define TU_INTERSECTION_TYPE_TLAS      (1u << 8)
#define TU_INTERSECTION_TYPE_LEAF      (1u << 9)
#define TU_INTERSECTION_TYPE_NONOPAQUE (1u << 10)
#define TU_INTERSECTION_TYPE_AABB      (1u << 11)
#define TU_INTERSECTION_BACK_FACE      (1u << 12)

#define TU_INSTANCE_ALL_OPAQUE    (1u << 2)
#define TU_INSTANCE_ALL_NONOPAQUE (1u << 3)
#define TU_INSTANCE_ALL_AABB      (1u << 6)

struct tu_leaf_node {
   uint32_t id;
   float coords[3][3];
   uint32_t geometry_id; /* Ignored by HW, we use it to stash the geometry ID */
   uint32_t padding[4];
   uint32_t type_flags;
};

struct tu_internal_node {
   uint32_t id;
   uint16_t bases[3];
   uint8_t mantissas[8][2][3];
   uint8_t exponents[3];
   uint8_t child_count;
   uint16_t type_flags;
};

struct tu_compressed_node {
   uint32_t id;
   uint32_t bases[3];
   uint32_t data[12];
};

struct tu_instance_descriptor {
   uint64_t bvh_ptr;

   uint32_t custom_instance_index;

   /* lower 24 bits are the sbt offset, upper 8 bits are the
    * VkGeometryInstanceFlagsKHR
    */
   uint32_t sbt_offset_and_flags;

   mat3x4 wto_matrix;

   uint32_t bvh_offset;

   /* Pad to make the size a power of 2 so that addressing math is
    * simplified.
    */
   uint32_t reserved[3];
   
   /* Object to world matrix inverted from the initial transform. */
   mat3x4 otw_matrix;
};

#endif

