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

#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_KHR_shader_subgroup_vote : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_ballot : require

#include "vk_build_interface.h"

layout(local_size_x_id = SUBGROUP_SIZE_ID, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform CONSTS {
   leaf_args args;
};

bool
build_triangle(inout vk_aabb bounds, VOID_REF dst_ptr, vk_bvh_geometry_data geom_data, uint32_t global_id)
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

   REF(vk_ir_triangle_node) node = REF(vk_ir_triangle_node)(dst_ptr);

   bounds.min = vec3(INFINITY);
   bounds.max = vec3(-INFINITY);

   for (uint32_t coord = 0; coord < 3; coord++)
   for (uint32_t comp = 0; comp < 3; comp++) {
      DEREF(node).coords[coord][comp] = vertices.vertex[coord][comp];
      bounds.min[comp] = min(bounds.min[comp], vertices.vertex[coord][comp]);
      bounds.max[comp] = max(bounds.max[comp], vertices.vertex[coord][comp]);
   }

   DEREF(node).base.aabb = bounds;
   DEREF(node).triangle_id = global_id;
   DEREF(node).geometry_id_and_flags = geom_data.geometry_id;
   DEREF(node).id = 9;

   return is_valid;
}

bool
build_aabb(inout vk_aabb bounds, VOID_REF src_ptr, VOID_REF dst_ptr, uint32_t geometry_id, uint32_t global_id)
{
   bool is_valid = true;
   REF(vk_ir_aabb_node) node = REF(vk_ir_aabb_node)(dst_ptr);

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

   DEREF(node).base.aabb = bounds;
   DEREF(node).primitive_id = global_id;
   DEREF(node).geometry_id_and_flags = geometry_id;

   return is_valid;
}

vk_aabb
calculate_instance_node_bounds(vk_aabb blas_aabb, mat3x4 otw_matrix)
{
   vk_aabb aabb;

   for (uint32_t comp = 0; comp < 3; ++comp) {
      aabb.min[comp] = otw_matrix[comp][3];
      aabb.max[comp] = otw_matrix[comp][3];
      for (uint32_t col = 0; col < 3; ++col) {
         aabb.min[comp] +=
            min(otw_matrix[comp][col] * blas_aabb.min[col], otw_matrix[comp][col] * blas_aabb.max[col]);
         aabb.max[comp] +=
            max(otw_matrix[comp][col] * blas_aabb.min[col], otw_matrix[comp][col] * blas_aabb.max[col]);
      }
   }
   return aabb;
}

bool
build_instance(inout vk_aabb bounds, VOID_REF src_ptr, VOID_REF dst_ptr, uint32_t global_id)
{
   REF(vk_ir_instance_node) node = REF(vk_ir_instance_node)(dst_ptr);

   AccelerationStructureInstance instance = DEREF(REF(AccelerationStructureInstance)(src_ptr));

   /* An inactive instance is one whose acceleration structure handle is VK_NULL_HANDLE. Since the active terminology is
    * only relevant for BVH updates, which we do not implement, we can also skip instances with mask == 0.
    */
   if (instance.accelerationStructureReference == 0 || instance.custom_instance_and_mask < (1u << 24u))
      return false;

   DEREF(node).base_ptr = instance.accelerationStructureReference;

   mat4 transform = mat4(instance.transform);
   DEREF(node).otw_matrix = mat3x4(transform);

   vk_aabb blas_aabb = DEREF(REF(vk_aabb)(instance.accelerationStructureReference + BVH_BOUNDS_OFFSET));

   bounds = calculate_instance_node_bounds(blas_aabb, mat3x4(transform));

#ifdef CALCULATE_FINE_INSTANCE_NODE_BOUNDS
   vec3 blas_aabb_extent = blas_aabb.max - blas_aabb.min;
   float blas_aabb_volume = blas_aabb_extent.x * blas_aabb_extent.y * blas_aabb_extent.z;
   blas_aabb_volume *= abs(determinant(mat3(transform)));

   vec3 bounds_extent = bounds.max - bounds.min;
   float bounds_volume = bounds_extent.x * bounds_extent.y * bounds_extent.z;

   /* Only try calculating finer-grained instance node bounds if the volume of the transformed
    * instance AABB is significantly higher than the volume of the BLAS without transformations
    * applied. Otherwise, the finer-grained bounds won't be much smaller and the additional overhead
    * wouldn't be worth it.
    */
   if (bounds_volume > 1.4f * blas_aabb_volume)
      bounds = CALCULATE_FINE_INSTANCE_NODE_BOUNDS(instance.accelerationStructureReference, mat3x4(transform));
#endif

   DEREF(node).base.aabb = bounds;
   DEREF(node).custom_instance_and_mask = instance.custom_instance_and_mask;
   DEREF(node).sbt_offset_and_flags = instance.sbt_offset_and_flags;
   DEREF(node).instance_id = global_id;

   return true;
}

void
main(void)
{
   uint32_t global_id = gl_GlobalInvocationID.x;
   uint32_t primitive_id = args.geom_data.first_id + global_id;

   REF(key_id_pair) id_ptr = INDEX(key_id_pair, args.ids, primitive_id);
   uint32_t src_offset = global_id * args.geom_data.stride;

   uint32_t dst_stride;
   uint32_t node_type;
   if (args.geom_data.geometry_type == VK_GEOMETRY_TYPE_TRIANGLES_KHR) {
      dst_stride = SIZEOF(vk_ir_triangle_node);
      node_type = vk_ir_node_triangle;
   } else if (args.geom_data.geometry_type == VK_GEOMETRY_TYPE_AABBS_KHR) {
      dst_stride = SIZEOF(vk_ir_aabb_node);
      node_type = vk_ir_node_aabb;
   } else {
      dst_stride = SIZEOF(vk_ir_instance_node);
      node_type = vk_ir_node_instance;
   }

   uint32_t dst_offset = primitive_id * dst_stride;
   VOID_REF dst_ptr = OFFSET(args.bvh, dst_offset);

   vk_aabb bounds;
   bool is_active;
   if (args.geom_data.geometry_type == VK_GEOMETRY_TYPE_TRIANGLES_KHR) {
      is_active = build_triangle(bounds, dst_ptr, args.geom_data, global_id);
   } else if (args.geom_data.geometry_type == VK_GEOMETRY_TYPE_AABBS_KHR) {
      VOID_REF src_ptr = OFFSET(args.geom_data.data, src_offset);
      is_active = build_aabb(bounds, src_ptr, dst_ptr, args.geom_data.geometry_id, global_id);
   } else {
      VOID_REF src_ptr = OFFSET(args.geom_data.data, src_offset);
      /* arrayOfPointers */
      if (args.geom_data.stride == 8) {
         src_ptr = DEREF(REF(VOID_REF)(src_ptr));
      }

      is_active = build_instance(bounds, src_ptr, dst_ptr, global_id);
   }

#if ALWAYS_ACTIVE
   if (!is_active && args.geom_data.geometry_type != VK_GEOMETRY_TYPE_INSTANCES_KHR) {
      bounds.min = vec3(0.0);
      bounds.max = vec3(0.0);
      is_active = true;
   }
#endif

   DEREF(id_ptr).id = is_active ? pack_ir_node_id(dst_offset, node_type) : VK_BVH_INVALID_NODE;

   uvec4 ballot = subgroupBallot(is_active);
   if (subgroupElect())
      atomicAdd(DEREF(args.header).active_leaf_count, subgroupBallotBitCount(ballot));

   atomicMin(DEREF(args.header).min_bounds[0], to_emulated_float(bounds.min.x));
   atomicMin(DEREF(args.header).min_bounds[1], to_emulated_float(bounds.min.y));
   atomicMin(DEREF(args.header).min_bounds[2], to_emulated_float(bounds.min.z));
   atomicMax(DEREF(args.header).max_bounds[0], to_emulated_float(bounds.max.x));
   atomicMax(DEREF(args.header).max_bounds[1], to_emulated_float(bounds.max.y));
   atomicMax(DEREF(args.header).max_bounds[2], to_emulated_float(bounds.max.z));
}
