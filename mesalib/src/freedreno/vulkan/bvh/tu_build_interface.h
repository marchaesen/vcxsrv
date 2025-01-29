/*
 * Copyright © 2022 Konstantin Seurer
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

#ifndef TU_BVH_BUILD_INTERFACE_H
#define TU_BVH_BUILD_INTERFACE_H

#ifdef VULKAN
#include "tu_build_helpers.h"
#else
#include <stdint.h>
#include "tu_bvh.h"
#define REF(type) uint64_t
#define VOID_REF  uint64_t
#endif

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
   REF(tu_accel_struct_header) dst;
   uint32_t bvh_offset;
   uint32_t instance_count;
};

#define TU_COPY_MODE_COPY        0
#define TU_COPY_MODE_SERIALIZE   1
#define TU_COPY_MODE_DESERIALIZE 2

struct copy_args {
   VOID_REF src_addr;
   VOID_REF dst_addr;
   uint32_t mode;
};

#endif

