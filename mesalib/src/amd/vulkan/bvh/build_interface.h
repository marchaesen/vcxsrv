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

#ifndef BVH_BUILD_INTERFACE_H
#define BVH_BUILD_INTERFACE_H

#ifdef VULKAN
#include "build_helpers.h"
#else
#include <stdint.h>
#define REF(type) uint64_t
#define VOID_REF  uint64_t
#endif

struct leaf_args {
   VOID_REF bvh;
   REF(AABB) bounds;
   REF(key_id_pair) ids;

   VOID_REF data;
   VOID_REF indices;
   VOID_REF transform;

   uint32_t dst_offset;
   uint32_t first_id;
   uint32_t geometry_type;
   uint32_t geometry_id;

   uint32_t stride;
   uint32_t vertex_format;
   uint32_t index_format;
};

struct morton_args {
   VOID_REF bvh;
   REF(AABB) bounds;
   REF(key_id_pair) ids;
};

struct internal_args {
   VOID_REF bvh;
   REF(key_id_pair) src_ids;
   REF(key_id_pair) dst_ids;
   uint32_t dst_offset;
   uint32_t fill_count;
};

#endif
