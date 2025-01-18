/*
 * Copyright © 2022 Konstantin Seurer
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

#ifndef VK_BVH_BUILD_INTERFACE_H
#define VK_BVH_BUILD_INTERFACE_H

#ifdef VULKAN
#include "vk_build_helpers.h"
#else
#include <stdint.h>
#include "vk_bvh.h"
#define REF(type) uint64_t
#define VOID_REF  uint64_t
#endif

#define SUBGROUP_SIZE_ID 0
#define BVH_BOUNDS_OFFSET_ID 1
#ifdef VULKAN
layout (constant_id = SUBGROUP_SIZE_ID) const int SUBGROUP_SIZE = 64;
layout (constant_id = BVH_BOUNDS_OFFSET_ID) const int BVH_BOUNDS_OFFSET = 0;
#endif

struct leaf_args {
   VOID_REF bvh;
   REF(vk_ir_header) header;
   REF(key_id_pair) ids;

   vk_bvh_geometry_data geom_data;
};

struct morton_args {
   VOID_REF bvh;
   REF(vk_ir_header) header;
   REF(key_id_pair) ids;
};

#define LBVH_RIGHT_CHILD_BIT_SHIFT 29
#define LBVH_RIGHT_CHILD_BIT       (1 << LBVH_RIGHT_CHILD_BIT_SHIFT)

struct lbvh_node_info {
   /* Number of children that have been processed (or are invalid/leaves) in
    * the lbvh_generate_ir pass.
    */
   uint32_t path_count;

   uint32_t children[2];
   uint32_t parent;
};

struct lbvh_main_args {
   VOID_REF bvh;
   REF(key_id_pair) src_ids;
   VOID_REF node_info;
   uint32_t id_count;
   uint32_t internal_node_base;
};

struct lbvh_generate_ir_args {
   VOID_REF bvh;
   VOID_REF node_info;
   VOID_REF header;
   uint32_t internal_node_base;
};

struct ploc_prefix_scan_partition {
   uint32_t aggregate;
   uint32_t inclusive_sum;
};

#define PLOC_WORKGROUP_SIZE 1024
#define PLOC_SUBGROUPS_PER_WORKGROUP                                           \
   (DIV_ROUND_UP(PLOC_WORKGROUP_SIZE, SUBGROUP_SIZE))

struct ploc_args {
   VOID_REF bvh;
   VOID_REF prefix_scan_partitions;
   REF(vk_ir_header) header;
   VOID_REF ids_0;
   VOID_REF ids_1;
   uint32_t internal_node_offset;
};

#endif
