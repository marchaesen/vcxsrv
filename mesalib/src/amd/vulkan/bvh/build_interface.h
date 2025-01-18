/*
 * Copyright © 2022 Konstantin Seurer
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BVH_BUILD_INTERFACE_H
#define BVH_BUILD_INTERFACE_H

#ifdef VULKAN
#include "build_helpers.h"
#else
#include <stdint.h>
#include "bvh.h"
#define REF(type) uint64_t
#define VOID_REF  uint64_t
#endif

#define RADV_COPY_MODE_COPY        0
#define RADV_COPY_MODE_SERIALIZE   1
#define RADV_COPY_MODE_DESERIALIZE 2

struct copy_args {
   VOID_REF src_addr;
   VOID_REF dst_addr;
   uint32_t mode;
};

struct encode_args {
   VOID_REF intermediate_bvh;
   VOID_REF output_bvh;
   REF(vk_ir_header) header;
   uint32_t output_bvh_offset;
   uint32_t leaf_node_count;
   uint32_t geometry_type;
};

struct header_args {
   REF(vk_ir_header) src;
   REF(radv_accel_struct_header) dst;
   uint32_t bvh_offset;
   uint32_t instance_count;
};

struct update_args {
   REF(radv_accel_struct_header) src;
   REF(radv_accel_struct_header) dst;
   REF(vk_aabb) leaf_bounds;
   REF(uint32_t) internal_ready_count;
   uint32_t leaf_node_count;

   vk_bvh_geometry_data geom_data;
};

#endif /* BUILD_INTERFACE_H */
