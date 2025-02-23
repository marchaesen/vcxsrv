/*
 * Copyright © 2022 Konstantin Seurer
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BVH_BUILD_HELPERS_H
#define BVH_BUILD_HELPERS_H

#include "bvh.h"
#include "vk_build_helpers.h"

TYPE(radv_accel_struct_serialization_header, 8);
TYPE(radv_accel_struct_header, 8);
TYPE(radv_bvh_triangle_node, 4);
TYPE(radv_bvh_aabb_node, 4);
TYPE(radv_bvh_instance_node, 8);
TYPE(radv_bvh_box16_node, 4);
TYPE(radv_bvh_box32_node, 4);

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

uint64_t
node_to_addr(uint64_t node)
{
   node &= ~7ul;
   node <<= 19;
   return int64_t(node) >> 16;
}

uint64_t
addr_to_node(uint64_t addr)
{
   return (addr >> 3) & ((1ul << 45) - 1);
}

uint32_t
ir_type_to_bvh_type(uint32_t type)
{
   switch (type) {
   case vk_ir_node_triangle:
      return radv_bvh_node_triangle;
   case vk_ir_node_internal:
      return radv_bvh_node_box32;
   case vk_ir_node_instance:
      return radv_bvh_node_instance;
   case vk_ir_node_aabb:
      return radv_bvh_node_aabb;
   }
   /* unreachable in valid nodes */
   return RADV_BVH_INVALID_NODE;
}

bool
radv_build_triangle(inout vk_aabb bounds, VOID_REF dst_ptr, vk_bvh_geometry_data geom_data, uint32_t global_id)
{
   bool is_valid = true;
   triangle_indices indices = load_indices(geom_data.indices, geom_data.index_format, global_id);

   triangle_vertices vertices = load_vertices(geom_data.data, indices, geom_data.vertex_format, geom_data.stride);

   /* An inactive triangle is one for which the first (X) component of any vertex is NaN. If any
    * other vertex component is NaN, and the first is not, the behavior is undefined. If the vertex
    * format does not have a NaN representation, then all triangles are considered active.
    */
   if (isnan(vertices.vertex[0].x) || isnan(vertices.vertex[1].x) || isnan(vertices.vertex[2].x))
#if ALWAYS_ACTIVE
      is_valid = false;
#else
      return false;
#endif

   if (geom_data.transform != NULL) {
      mat4 transform = mat4(1.0);

      for (uint32_t col = 0; col < 4; col++)
         for (uint32_t row = 0; row < 3; row++)
            transform[col][row] = DEREF(INDEX(float, geom_data.transform, col + row * 4));

      for (uint32_t i = 0; i < 3; i++)
         vertices.vertex[i] = transform * vertices.vertex[i];
   }

   REF(radv_bvh_triangle_node) node = REF(radv_bvh_triangle_node)(dst_ptr);

   bounds.min = vec3(INFINITY);
   bounds.max = vec3(-INFINITY);

   for (uint32_t coord = 0; coord < 3; coord++)
      for (uint32_t comp = 0; comp < 3; comp++) {
         DEREF(node).coords[coord][comp] = vertices.vertex[coord][comp];
         bounds.min[comp] = min(bounds.min[comp], vertices.vertex[coord][comp]);
         bounds.max[comp] = max(bounds.max[comp], vertices.vertex[coord][comp]);
      }

   DEREF(node).triangle_id = global_id;
   DEREF(node).geometry_id_and_flags = geom_data.geometry_id;
   DEREF(node).id = 9;

   return is_valid;
}

bool
radv_build_aabb(inout vk_aabb bounds, VOID_REF src_ptr, VOID_REF dst_ptr, uint32_t geometry_id, uint32_t global_id)
{
   bool is_valid = true;
   REF(radv_bvh_aabb_node) node = REF(radv_bvh_aabb_node)(dst_ptr);

   for (uint32_t vec = 0; vec < 2; vec++)
      for (uint32_t comp = 0; comp < 3; comp++) {
         float coord = DEREF(INDEX(float, src_ptr, comp + vec * 3));

         if (vec == 0)
            bounds.min[comp] = coord;
         else
            bounds.max[comp] = coord;
      }

   /* An inactive AABB is one for which the minimum X coordinate is NaN. If any other component is
    * NaN, and the first is not, the behavior is undefined.
    */
   if (isnan(bounds.min.x))
#if ALWAYS_ACTIVE
      is_valid = false;
#else
      return false;
#endif

   DEREF(node).primitive_id = global_id;
   DEREF(node).geometry_id_and_flags = geometry_id;

   return is_valid;
}

uint32_t
radv_encode_sbt_offset_and_flags(uint32_t src)
{
   uint32_t flags = src >> 24;
   uint32_t ret = src & 0xffffffu;
   if ((flags & VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR) != 0)
      ret |= RADV_INSTANCE_FORCE_OPAQUE;
   if ((flags & VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR) == 0)
      ret |= RADV_INSTANCE_NO_FORCE_NOT_OPAQUE;
   if ((flags & VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR) != 0)
      ret |= RADV_INSTANCE_TRIANGLE_FACING_CULL_DISABLE;
   if ((flags & VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR) != 0)
      ret |= RADV_INSTANCE_TRIANGLE_FLIP_FACING;
   return ret;
}

/** Compute ceiling of integer quotient of A divided by B.
    From macros.h */
#define DIV_ROUND_UP(A, B) (((A) + (B)-1) / (B))

#endif /* BUILD_HELPERS_H */
