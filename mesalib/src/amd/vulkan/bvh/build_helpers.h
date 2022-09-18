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

#ifndef BVH_BUILD_HELPERS_H
#define BVH_BUILD_HELPERS_H

#include "bvh.h"

#define VK_FORMAT_UNDEFINED                  0
#define VK_FORMAT_R4G4_UNORM_PACK8           1
#define VK_FORMAT_R4G4B4A4_UNORM_PACK16      2
#define VK_FORMAT_B4G4R4A4_UNORM_PACK16      3
#define VK_FORMAT_R5G6B5_UNORM_PACK16        4
#define VK_FORMAT_B5G6R5_UNORM_PACK16        5
#define VK_FORMAT_R5G5B5A1_UNORM_PACK16      6
#define VK_FORMAT_B5G5R5A1_UNORM_PACK16      7
#define VK_FORMAT_A1R5G5B5_UNORM_PACK16      8
#define VK_FORMAT_R8_UNORM                   9
#define VK_FORMAT_R8_SNORM                   10
#define VK_FORMAT_R8_USCALED                 11
#define VK_FORMAT_R8_SSCALED                 12
#define VK_FORMAT_R8_UINT                    13
#define VK_FORMAT_R8_SINT                    14
#define VK_FORMAT_R8_SRGB                    15
#define VK_FORMAT_R8G8_UNORM                 16
#define VK_FORMAT_R8G8_SNORM                 17
#define VK_FORMAT_R8G8_USCALED               18
#define VK_FORMAT_R8G8_SSCALED               19
#define VK_FORMAT_R8G8_UINT                  20
#define VK_FORMAT_R8G8_SINT                  21
#define VK_FORMAT_R8G8_SRGB                  22
#define VK_FORMAT_R8G8B8_UNORM               23
#define VK_FORMAT_R8G8B8_SNORM               24
#define VK_FORMAT_R8G8B8_USCALED             25
#define VK_FORMAT_R8G8B8_SSCALED             26
#define VK_FORMAT_R8G8B8_UINT                27
#define VK_FORMAT_R8G8B8_SINT                28
#define VK_FORMAT_R8G8B8_SRGB                29
#define VK_FORMAT_B8G8R8_UNORM               30
#define VK_FORMAT_B8G8R8_SNORM               31
#define VK_FORMAT_B8G8R8_USCALED             32
#define VK_FORMAT_B8G8R8_SSCALED             33
#define VK_FORMAT_B8G8R8_UINT                34
#define VK_FORMAT_B8G8R8_SINT                35
#define VK_FORMAT_B8G8R8_SRGB                36
#define VK_FORMAT_R8G8B8A8_UNORM             37
#define VK_FORMAT_R8G8B8A8_SNORM             38
#define VK_FORMAT_R8G8B8A8_USCALED           39
#define VK_FORMAT_R8G8B8A8_SSCALED           40
#define VK_FORMAT_R8G8B8A8_UINT              41
#define VK_FORMAT_R8G8B8A8_SINT              42
#define VK_FORMAT_R8G8B8A8_SRGB              43
#define VK_FORMAT_B8G8R8A8_UNORM             44
#define VK_FORMAT_B8G8R8A8_SNORM             45
#define VK_FORMAT_B8G8R8A8_USCALED           46
#define VK_FORMAT_B8G8R8A8_SSCALED           47
#define VK_FORMAT_B8G8R8A8_UINT              48
#define VK_FORMAT_B8G8R8A8_SINT              49
#define VK_FORMAT_B8G8R8A8_SRGB              50
#define VK_FORMAT_A8B8G8R8_UNORM_PACK32      51
#define VK_FORMAT_A8B8G8R8_SNORM_PACK32      52
#define VK_FORMAT_A8B8G8R8_USCALED_PACK32    53
#define VK_FORMAT_A8B8G8R8_SSCALED_PACK32    54
#define VK_FORMAT_A8B8G8R8_UINT_PACK32       55
#define VK_FORMAT_A8B8G8R8_SINT_PACK32       56
#define VK_FORMAT_A8B8G8R8_SRGB_PACK32       57
#define VK_FORMAT_A2R10G10B10_UNORM_PACK32   58
#define VK_FORMAT_A2R10G10B10_SNORM_PACK32   59
#define VK_FORMAT_A2R10G10B10_USCALED_PACK32 60
#define VK_FORMAT_A2R10G10B10_SSCALED_PACK32 61
#define VK_FORMAT_A2R10G10B10_UINT_PACK32    62
#define VK_FORMAT_A2R10G10B10_SINT_PACK32    63
#define VK_FORMAT_A2B10G10R10_UNORM_PACK32   64
#define VK_FORMAT_A2B10G10R10_SNORM_PACK32   65
#define VK_FORMAT_A2B10G10R10_USCALED_PACK32 66
#define VK_FORMAT_A2B10G10R10_SSCALED_PACK32 67
#define VK_FORMAT_A2B10G10R10_UINT_PACK32    68
#define VK_FORMAT_A2B10G10R10_SINT_PACK32    69
#define VK_FORMAT_R16_UNORM                  70
#define VK_FORMAT_R16_SNORM                  71
#define VK_FORMAT_R16_USCALED                72
#define VK_FORMAT_R16_SSCALED                73
#define VK_FORMAT_R16_UINT                   74
#define VK_FORMAT_R16_SINT                   75
#define VK_FORMAT_R16_SFLOAT                 76
#define VK_FORMAT_R16G16_UNORM               77
#define VK_FORMAT_R16G16_SNORM               78
#define VK_FORMAT_R16G16_USCALED             79
#define VK_FORMAT_R16G16_SSCALED             80
#define VK_FORMAT_R16G16_UINT                81
#define VK_FORMAT_R16G16_SINT                82
#define VK_FORMAT_R16G16_SFLOAT              83
#define VK_FORMAT_R16G16B16_UNORM            84
#define VK_FORMAT_R16G16B16_SNORM            85
#define VK_FORMAT_R16G16B16_USCALED          86
#define VK_FORMAT_R16G16B16_SSCALED          87
#define VK_FORMAT_R16G16B16_UINT             88
#define VK_FORMAT_R16G16B16_SINT             89
#define VK_FORMAT_R16G16B16_SFLOAT           90
#define VK_FORMAT_R16G16B16A16_UNORM         91
#define VK_FORMAT_R16G16B16A16_SNORM         92
#define VK_FORMAT_R16G16B16A16_USCALED       93
#define VK_FORMAT_R16G16B16A16_SSCALED       94
#define VK_FORMAT_R16G16B16A16_UINT          95
#define VK_FORMAT_R16G16B16A16_SINT          96
#define VK_FORMAT_R16G16B16A16_SFLOAT        97
#define VK_FORMAT_R32_UINT                   98
#define VK_FORMAT_R32_SINT                   99
#define VK_FORMAT_R32_SFLOAT                 100
#define VK_FORMAT_R32G32_UINT                101
#define VK_FORMAT_R32G32_SINT                102
#define VK_FORMAT_R32G32_SFLOAT              103
#define VK_FORMAT_R32G32B32_UINT             104
#define VK_FORMAT_R32G32B32_SINT             105
#define VK_FORMAT_R32G32B32_SFLOAT           106
#define VK_FORMAT_R32G32B32A32_UINT          107
#define VK_FORMAT_R32G32B32A32_SINT          108
#define VK_FORMAT_R32G32B32A32_SFLOAT        109
#define VK_FORMAT_R64_UINT                   110
#define VK_FORMAT_R64_SINT                   111
#define VK_FORMAT_R64_SFLOAT                 112
#define VK_FORMAT_R64G64_UINT                113
#define VK_FORMAT_R64G64_SINT                114
#define VK_FORMAT_R64G64_SFLOAT              115
#define VK_FORMAT_R64G64B64_UINT             116
#define VK_FORMAT_R64G64B64_SINT             117
#define VK_FORMAT_R64G64B64_SFLOAT           118
#define VK_FORMAT_R64G64B64A64_UINT          119
#define VK_FORMAT_R64G64B64A64_SINT          120
#define VK_FORMAT_R64G64B64A64_SFLOAT        121

#define VK_INDEX_TYPE_UINT16    0
#define VK_INDEX_TYPE_UINT32    1
#define VK_INDEX_TYPE_NONE_KHR  1000165000
#define VK_INDEX_TYPE_UINT8_EXT 1000265000

#define VK_GEOMETRY_TYPE_TRIANGLES_KHR 0
#define VK_GEOMETRY_TYPE_AABBS_KHR     1

#define TYPE(type, size)                                                                           \
   layout(buffer_reference) buffer type##_ref                                                      \
   {                                                                                               \
      type value;                                                                                  \
   };                                                                                              \
   const uint32_t type##_size = size

#define REF(type)  type##_ref
#define VOID_REF   uint64_t
#define NULL       0
#define DEREF(var) var.value

#define SIZEOF(type) type##_size

#define OFFSET(ptr, offset) (uint64_t(ptr) + offset)

#define INFINITY (1.0 / 0.0)
#define NAN      (0.0 / 0.0)

#define INDEX(type, ptr, index) REF(type)(OFFSET(ptr, (index)*SIZEOF(type)))

TYPE(int8_t, 1);
TYPE(uint8_t, 1);
TYPE(int16_t, 2);
TYPE(uint16_t, 2);
TYPE(int32_t, 4);
TYPE(uint32_t, 4);
TYPE(int64_t, 8);
TYPE(uint64_t, 8);

TYPE(float, 4);

TYPE(vec2, 8);
TYPE(vec3, 12);
TYPE(vec4, 16);

TYPE(VOID_REF, 8);

void
min_float_emulated(REF(int32_t) addr, float f)
{
   int32_t bits = floatBitsToInt(f);
   atomicMin(DEREF(addr), f < 0 ? -2147483648 - bits : bits);
}

void
max_float_emulated(REF(int32_t) addr, float f)
{
   int32_t bits = floatBitsToInt(f);
   atomicMax(DEREF(addr), f < 0 ? -2147483648 - bits : bits);
}

float
load_minmax_float_emulated(VOID_REF addr)
{
   int32_t bits = DEREF(REF(int32_t)(addr));
   return intBitsToFloat(bits < 0 ? -2147483648 - bits : bits);
}

struct AABB {
   vec3 min;
   vec3 max;
};
TYPE(AABB, 24);

struct key_id_pair {
   uint32_t id;
   uint32_t key;
};
TYPE(key_id_pair, 8);

TYPE(radv_accel_struct_serialization_header, 56);
TYPE(radv_accel_struct_header, 88);
TYPE(radv_bvh_triangle_node, 64);
TYPE(radv_bvh_aabb_node, 64);
TYPE(radv_bvh_instance_node, 128);
TYPE(radv_bvh_box16_node, 64);
TYPE(radv_bvh_box32_node, 128);

struct bvh_node {
   uint8_t reserved[128];
};
TYPE(bvh_node, 128);

uint32_t
id_to_offset(uint32_t id)
{
   return (id & (~7u)) << 3;
}

uint32_t
id_to_type(uint32_t id)
{
   return id & 7u;
}

uint32_t
pack_node_id(uint32_t offset, uint32_t type)
{
   return (offset >> 3) | type;
}

#define NULL_NODE_ID 0xFFFFFFFF

AABB
calculate_node_bounds(VOID_REF bvh, uint32_t id)
{
   AABB aabb;

   VOID_REF node = OFFSET(bvh, id_to_offset(id));
   switch (id_to_type(id)) {
   case radv_bvh_node_triangle: {
      radv_bvh_triangle_node triangle = DEREF(REF(radv_bvh_triangle_node)(node));

      vec3 v0 = vec3(triangle.coords[0][0], triangle.coords[0][1], triangle.coords[0][2]);
      vec3 v1 = vec3(triangle.coords[1][0], triangle.coords[1][1], triangle.coords[1][2]);
      vec3 v2 = vec3(triangle.coords[2][0], triangle.coords[2][1], triangle.coords[2][2]);

      aabb.min = min(min(v0, v1), v2);
      aabb.max = max(max(v0, v1), v2);
      break;
   }
   case radv_bvh_node_internal: {
      radv_bvh_box32_node internal = DEREF(REF(radv_bvh_box32_node)(node));
      aabb.min = vec3(INFINITY);
      aabb.max = vec3(-INFINITY);
      for (uint32_t i = 0; i < 4; i++) {
         aabb.min.x = min(aabb.min.x, internal.coords[i][0][0]);
         aabb.min.y = min(aabb.min.y, internal.coords[i][0][1]);
         aabb.min.z = min(aabb.min.z, internal.coords[i][0][2]);

         aabb.max.x = max(aabb.max.x, internal.coords[i][1][0]);
         aabb.max.y = max(aabb.max.y, internal.coords[i][1][1]);
         aabb.max.z = max(aabb.max.z, internal.coords[i][1][2]);
      }
      break;
   }
   case radv_bvh_node_instance: {
      radv_bvh_instance_node instance = DEREF(REF(radv_bvh_instance_node)(node));

      aabb.min.x = instance.aabb[0][0];
      aabb.min.y = instance.aabb[0][1];
      aabb.min.z = instance.aabb[0][2];

      aabb.max.x = instance.aabb[1][0];
      aabb.max.y = instance.aabb[1][1];
      aabb.max.z = instance.aabb[1][2];
      break;
   }
   case radv_bvh_node_aabb: {
      radv_bvh_aabb_node custom = DEREF(REF(radv_bvh_aabb_node)(node));

      aabb.min.x = custom.aabb[0][0];
      aabb.min.y = custom.aabb[0][1];
      aabb.min.z = custom.aabb[0][2];

      aabb.max.x = custom.aabb[1][0];
      aabb.max.y = custom.aabb[1][1];
      aabb.max.z = custom.aabb[1][2];
      break;
   }
   }

   return aabb;
}

#endif
