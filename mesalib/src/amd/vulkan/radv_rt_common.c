/*
 * Copyright © 2021 Google
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

#include "radv_debug.h"
#include "radv_rt_common.h"
#include "radv_acceleration_structure.h"

bool
radv_enable_rt(const struct radv_physical_device *pdevice, bool rt_pipelines)
{
   if ((pdevice->rad_info.gfx_level < GFX10_3 && !radv_emulate_rt(pdevice)) || pdevice->use_llvm)
      return false;

   if (rt_pipelines)
      return pdevice->instance->perftest_flags & RADV_PERFTEST_RT;

   return true;
}

bool
radv_emulate_rt(const struct radv_physical_device *pdevice)
{
   return pdevice->instance->perftest_flags & RADV_PERFTEST_EMULATE_RT;
}

void
nir_sort_hit_pair(nir_builder *b, nir_variable *var_distances, nir_variable *var_indices,
                  uint32_t chan_1, uint32_t chan_2)
{
   nir_ssa_def *ssa_distances = nir_load_var(b, var_distances);
   nir_ssa_def *ssa_indices = nir_load_var(b, var_indices);
   /* if (distances[chan_2] < distances[chan_1]) { */
   nir_push_if(
      b, nir_flt(b, nir_channel(b, ssa_distances, chan_2), nir_channel(b, ssa_distances, chan_1)));
   {
      /* swap(distances[chan_2], distances[chan_1]); */
      nir_ssa_def *new_distances[4] = {nir_ssa_undef(b, 1, 32), nir_ssa_undef(b, 1, 32),
                                       nir_ssa_undef(b, 1, 32), nir_ssa_undef(b, 1, 32)};
      nir_ssa_def *new_indices[4] = {nir_ssa_undef(b, 1, 32), nir_ssa_undef(b, 1, 32),
                                     nir_ssa_undef(b, 1, 32), nir_ssa_undef(b, 1, 32)};
      new_distances[chan_2] = nir_channel(b, ssa_distances, chan_1);
      new_distances[chan_1] = nir_channel(b, ssa_distances, chan_2);
      new_indices[chan_2] = nir_channel(b, ssa_indices, chan_1);
      new_indices[chan_1] = nir_channel(b, ssa_indices, chan_2);
      nir_store_var(b, var_distances, nir_vec(b, new_distances, 4),
                    (1u << chan_1) | (1u << chan_2));
      nir_store_var(b, var_indices, nir_vec(b, new_indices, 4), (1u << chan_1) | (1u << chan_2));
   }
   /* } */
   nir_pop_if(b, NULL);
}

nir_ssa_def *
intersect_ray_amd_software_box(struct radv_device *device, nir_builder *b, nir_ssa_def *bvh_node,
                               nir_ssa_def *ray_tmax, nir_ssa_def *origin, nir_ssa_def *dir,
                               nir_ssa_def *inv_dir)
{
   const struct glsl_type *vec4_type = glsl_vector_type(GLSL_TYPE_FLOAT, 4);
   const struct glsl_type *uvec4_type = glsl_vector_type(GLSL_TYPE_UINT, 4);

   nir_ssa_def *node_addr = build_node_to_addr(device, b, bvh_node);

   /* vec4 distances = vec4(INF, INF, INF, INF); */
   nir_variable *distances =
      nir_variable_create(b->shader, nir_var_shader_temp, vec4_type, "distances");
   nir_store_var(b, distances, nir_imm_vec4(b, INFINITY, INFINITY, INFINITY, INFINITY), 0xf);

   /* uvec4 child_indices = uvec4(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff); */
   nir_variable *child_indices =
      nir_variable_create(b->shader, nir_var_shader_temp, uvec4_type, "child_indices");
   nir_store_var(b, child_indices,
                 nir_imm_ivec4(b, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu), 0xf);

   /* Need to remove infinities here because otherwise we get nasty NaN propogation
    * if the direction has 0s in it. */
   /* inv_dir = clamp(inv_dir, -FLT_MAX, FLT_MAX); */
   inv_dir = nir_fclamp(b, inv_dir, nir_imm_float(b, -FLT_MAX), nir_imm_float(b, FLT_MAX));

   for (int i = 0; i < 4; i++) {
      const uint32_t child_offset = offsetof(struct radv_bvh_box32_node, children[i]);
      const uint32_t coord_offsets[2] = {
         offsetof(struct radv_bvh_box32_node, coords[i][0][0]),
         offsetof(struct radv_bvh_box32_node, coords[i][1][0]),
      };

      /* node->children[i] -> uint */
      nir_ssa_def *child_index =
         nir_build_load_global(b, 1, 32, nir_iadd_imm(b, node_addr, child_offset), .align_mul = 64,
                               .align_offset = child_offset % 64);
      /* node->coords[i][0], node->coords[i][1] -> vec3 */
      nir_ssa_def *node_coords[2] = {
         nir_build_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, coord_offsets[0]),
                               .align_mul = 64, .align_offset = coord_offsets[0] % 64),
         nir_build_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, coord_offsets[1]),
                               .align_mul = 64, .align_offset = coord_offsets[1] % 64),
      };

      /* If x of the aabb min is NaN, then this is an inactive aabb.
       * We don't need to care about any other components being NaN as that is UB.
       * https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/chap36.html#VkAabbPositionsKHR
       */
      nir_ssa_def *min_x = nir_channel(b, node_coords[0], 0);
      nir_ssa_def *min_x_is_not_nan =
         nir_inot(b, nir_fneu(b, min_x, min_x)); /* NaN != NaN -> true */

      /* vec3 bound0 = (node->coords[i][0] - origin) * inv_dir; */
      nir_ssa_def *bound0 = nir_fmul(b, nir_fsub(b, node_coords[0], origin), inv_dir);
      /* vec3 bound1 = (node->coords[i][1] - origin) * inv_dir; */
      nir_ssa_def *bound1 = nir_fmul(b, nir_fsub(b, node_coords[1], origin), inv_dir);

      /* float tmin = max(max(min(bound0.x, bound1.x), min(bound0.y, bound1.y)), min(bound0.z,
       * bound1.z)); */
      nir_ssa_def *tmin =
         nir_fmax(b,
                  nir_fmax(b, nir_fmin(b, nir_channel(b, bound0, 0), nir_channel(b, bound1, 0)),
                           nir_fmin(b, nir_channel(b, bound0, 1), nir_channel(b, bound1, 1))),
                  nir_fmin(b, nir_channel(b, bound0, 2), nir_channel(b, bound1, 2)));

      /* float tmax = min(min(max(bound0.x, bound1.x), max(bound0.y, bound1.y)), max(bound0.z,
       * bound1.z)); */
      nir_ssa_def *tmax =
         nir_fmin(b,
                  nir_fmin(b, nir_fmax(b, nir_channel(b, bound0, 0), nir_channel(b, bound1, 0)),
                           nir_fmax(b, nir_channel(b, bound0, 1), nir_channel(b, bound1, 1))),
                  nir_fmax(b, nir_channel(b, bound0, 2), nir_channel(b, bound1, 2)));

      /* if (!isnan(node->coords[i][0].x) && tmax >= max(0.0f, tmin) && tmin < ray_tmax) { */
      nir_push_if(b,
                  nir_iand(b, min_x_is_not_nan,
                           nir_iand(b, nir_fge(b, tmax, nir_fmax(b, nir_imm_float(b, 0.0f), tmin)),
                                    nir_flt(b, tmin, ray_tmax))));
      {
         /* child_indices[i] = node->children[i]; */
         nir_ssa_def *new_child_indices[4] = {child_index, child_index, child_index, child_index};
         nir_store_var(b, child_indices, nir_vec(b, new_child_indices, 4), 1u << i);

         /* distances[i] = tmin; */
         nir_ssa_def *new_distances[4] = {tmin, tmin, tmin, tmin};
         nir_store_var(b, distances, nir_vec(b, new_distances, 4), 1u << i);
      }
      /* } */
      nir_pop_if(b, NULL);
   }

   /* Sort our distances with a sorting network. */
   nir_sort_hit_pair(b, distances, child_indices, 0, 1);
   nir_sort_hit_pair(b, distances, child_indices, 2, 3);
   nir_sort_hit_pair(b, distances, child_indices, 0, 2);
   nir_sort_hit_pair(b, distances, child_indices, 1, 3);
   nir_sort_hit_pair(b, distances, child_indices, 1, 2);

   return nir_load_var(b, child_indices);
}

nir_ssa_def *
intersect_ray_amd_software_tri(struct radv_device *device, nir_builder *b, nir_ssa_def *bvh_node,
                               nir_ssa_def *ray_tmax, nir_ssa_def *origin, nir_ssa_def *dir,
                               nir_ssa_def *inv_dir)
{
   const struct glsl_type *vec4_type = glsl_vector_type(GLSL_TYPE_FLOAT, 4);

   nir_ssa_def *node_addr = build_node_to_addr(device, b, bvh_node);

   const uint32_t coord_offsets[3] = {
      offsetof(struct radv_bvh_triangle_node, coords[0]),
      offsetof(struct radv_bvh_triangle_node, coords[1]),
      offsetof(struct radv_bvh_triangle_node, coords[2]),
   };

   /* node->coords[0], node->coords[1], node->coords[2] -> vec3 */
   nir_ssa_def *node_coords[3] = {
      nir_build_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, coord_offsets[0]), .align_mul = 64,
                            .align_offset = coord_offsets[0] % 64),
      nir_build_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, coord_offsets[1]), .align_mul = 64,
                            .align_offset = coord_offsets[1] % 64),
      nir_build_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, coord_offsets[2]), .align_mul = 64,
                            .align_offset = coord_offsets[2] % 64),
   };

   nir_variable *result = nir_variable_create(b->shader, nir_var_shader_temp, vec4_type, "result");
   nir_store_var(b, result, nir_imm_vec4(b, INFINITY, 1.0f, 0.0f, 0.0f), 0xf);

   /* Based on watertight Ray/Triangle intersection from
    * http://jcgt.org/published/0002/01/05/paper.pdf */

   /* Calculate the dimension where the ray direction is largest */
   nir_ssa_def *abs_dir = nir_fabs(b, dir);

   nir_ssa_def *abs_dirs[3] = {
      nir_channel(b, abs_dir, 0),
      nir_channel(b, abs_dir, 1),
      nir_channel(b, abs_dir, 2),
   };
   /* Find index of greatest value of abs_dir and put that as kz. */
   nir_ssa_def *kz = nir_bcsel(
      b, nir_fge(b, abs_dirs[0], abs_dirs[1]),
      nir_bcsel(b, nir_fge(b, abs_dirs[0], abs_dirs[2]), nir_imm_int(b, 0), nir_imm_int(b, 2)),
      nir_bcsel(b, nir_fge(b, abs_dirs[1], abs_dirs[2]), nir_imm_int(b, 1), nir_imm_int(b, 2)));
   nir_ssa_def *kx = nir_imod(b, nir_iadd_imm(b, kz, 1), nir_imm_int(b, 3));
   nir_ssa_def *ky = nir_imod(b, nir_iadd_imm(b, kx, 1), nir_imm_int(b, 3));
   nir_ssa_def *k_indices[3] = {kx, ky, kz};
   nir_ssa_def *k = nir_vec(b, k_indices, 3);

   /* Swap kx and ky dimensions to preseve winding order */
   unsigned swap_xy_swizzle[4] = {1, 0, 2, 3};
   k = nir_bcsel(b, nir_flt(b, nir_vector_extract(b, dir, kz), nir_imm_float(b, 0.0f)),
                 nir_swizzle(b, k, swap_xy_swizzle, 3), k);

   kx = nir_channel(b, k, 0);
   ky = nir_channel(b, k, 1);
   kz = nir_channel(b, k, 2);

   /* Calculate shear constants */
   nir_ssa_def *sz = nir_frcp(b, nir_vector_extract(b, dir, kz));
   nir_ssa_def *sx = nir_fmul(b, nir_vector_extract(b, dir, kx), sz);
   nir_ssa_def *sy = nir_fmul(b, nir_vector_extract(b, dir, ky), sz);

   /* Calculate vertices relative to ray origin */
   nir_ssa_def *v_a = nir_fsub(b, node_coords[0], origin);
   nir_ssa_def *v_b = nir_fsub(b, node_coords[1], origin);
   nir_ssa_def *v_c = nir_fsub(b, node_coords[2], origin);

   /* Perform shear and scale */
   nir_ssa_def *ax =
      nir_fsub(b, nir_vector_extract(b, v_a, kx), nir_fmul(b, sx, nir_vector_extract(b, v_a, kz)));
   nir_ssa_def *ay =
      nir_fsub(b, nir_vector_extract(b, v_a, ky), nir_fmul(b, sy, nir_vector_extract(b, v_a, kz)));
   nir_ssa_def *bx =
      nir_fsub(b, nir_vector_extract(b, v_b, kx), nir_fmul(b, sx, nir_vector_extract(b, v_b, kz)));
   nir_ssa_def *by =
      nir_fsub(b, nir_vector_extract(b, v_b, ky), nir_fmul(b, sy, nir_vector_extract(b, v_b, kz)));
   nir_ssa_def *cx =
      nir_fsub(b, nir_vector_extract(b, v_c, kx), nir_fmul(b, sx, nir_vector_extract(b, v_c, kz)));
   nir_ssa_def *cy =
      nir_fsub(b, nir_vector_extract(b, v_c, ky), nir_fmul(b, sy, nir_vector_extract(b, v_c, kz)));

   nir_ssa_def *u = nir_fsub(b, nir_fmul(b, cx, by), nir_fmul(b, cy, bx));
   nir_ssa_def *v = nir_fsub(b, nir_fmul(b, ax, cy), nir_fmul(b, ay, cx));
   nir_ssa_def *w = nir_fsub(b, nir_fmul(b, bx, ay), nir_fmul(b, by, ax));

   nir_variable *u_var =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_float_type(), "u");
   nir_variable *v_var =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_float_type(), "v");
   nir_variable *w_var =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_float_type(), "w");
   nir_store_var(b, u_var, u, 0x1);
   nir_store_var(b, v_var, v, 0x1);
   nir_store_var(b, w_var, w, 0x1);

   /* Fallback to testing edges with double precision...
    *
    * The Vulkan spec states it only needs single precision watertightness
    * but we fail dEQP-VK.ray_tracing_pipeline.watertightness.closedFan2.1024 with
    * failures = 1 without doing this. :( */
   nir_ssa_def *cond_retest = nir_ior(
      b, nir_ior(b, nir_feq(b, u, nir_imm_float(b, 0.0f)), nir_feq(b, v, nir_imm_float(b, 0.0f))),
      nir_feq(b, w, nir_imm_float(b, 0.0f)));

   nir_push_if(b, cond_retest);
   {
      ax = nir_f2f64(b, ax);
      ay = nir_f2f64(b, ay);
      bx = nir_f2f64(b, bx);
      by = nir_f2f64(b, by);
      cx = nir_f2f64(b, cx);
      cy = nir_f2f64(b, cy);

      nir_store_var(b, u_var, nir_f2f32(b, nir_fsub(b, nir_fmul(b, cx, by), nir_fmul(b, cy, bx))),
                    0x1);
      nir_store_var(b, v_var, nir_f2f32(b, nir_fsub(b, nir_fmul(b, ax, cy), nir_fmul(b, ay, cx))),
                    0x1);
      nir_store_var(b, w_var, nir_f2f32(b, nir_fsub(b, nir_fmul(b, bx, ay), nir_fmul(b, by, ax))),
                    0x1);
   }
   nir_pop_if(b, NULL);

   u = nir_load_var(b, u_var);
   v = nir_load_var(b, v_var);
   w = nir_load_var(b, w_var);

   /* Perform edge tests. */
   nir_ssa_def *cond_back = nir_ior(
      b, nir_ior(b, nir_flt(b, u, nir_imm_float(b, 0.0f)), nir_flt(b, v, nir_imm_float(b, 0.0f))),
      nir_flt(b, w, nir_imm_float(b, 0.0f)));

   nir_ssa_def *cond_front = nir_ior(
      b, nir_ior(b, nir_flt(b, nir_imm_float(b, 0.0f), u), nir_flt(b, nir_imm_float(b, 0.0f), v)),
      nir_flt(b, nir_imm_float(b, 0.0f), w));

   nir_ssa_def *cond = nir_inot(b, nir_iand(b, cond_back, cond_front));

   nir_push_if(b, cond);
   {
      nir_ssa_def *det = nir_fadd(b, u, nir_fadd(b, v, w));

      nir_ssa_def *az = nir_fmul(b, sz, nir_vector_extract(b, v_a, kz));
      nir_ssa_def *bz = nir_fmul(b, sz, nir_vector_extract(b, v_b, kz));
      nir_ssa_def *cz = nir_fmul(b, sz, nir_vector_extract(b, v_c, kz));

      nir_ssa_def *t =
         nir_fadd(b, nir_fadd(b, nir_fmul(b, u, az), nir_fmul(b, v, bz)), nir_fmul(b, w, cz));

      nir_ssa_def *t_signed = nir_fmul(b, nir_fsign(b, det), t);

      nir_ssa_def *det_cond_front = nir_inot(b, nir_flt(b, t_signed, nir_imm_float(b, 0.0f)));

      nir_push_if(b, det_cond_front);
      {
         nir_ssa_def *indices[4] = {t, det, v, w};
         nir_store_var(b, result, nir_vec(b, indices, 4), 0xf);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);

   return nir_load_var(b, result);
}

nir_ssa_def *
build_addr_to_node(nir_builder *b, nir_ssa_def *addr)
{
   const uint64_t bvh_size = 1ull << 42;
   nir_ssa_def *node = nir_ushr_imm(b, addr, 3);
   return nir_iand_imm(b, node, (bvh_size - 1) << 3);
}

nir_ssa_def *
build_node_to_addr(struct radv_device *device, nir_builder *b, nir_ssa_def *node)
{
   nir_ssa_def *addr = nir_iand_imm(b, node, ~7ull);
   addr = nir_ishl_imm(b, addr, 3);
   /* Assumes everything is in the top half of address space, which is true in
    * GFX9+ for now. */
   return device->physical_device->rad_info.gfx_level >= GFX9
             ? nir_ior_imm(b, addr, 0xffffull << 48)
             : addr;
}

nir_ssa_def *
nir_build_vec3_mat_mult(nir_builder *b, nir_ssa_def *vec, nir_ssa_def *matrix[], bool translation)
{
   nir_ssa_def *result_components[3] = {
      nir_channel(b, matrix[0], 3),
      nir_channel(b, matrix[1], 3),
      nir_channel(b, matrix[2], 3),
   };
   for (unsigned i = 0; i < 3; ++i) {
      for (unsigned j = 0; j < 3; ++j) {
         nir_ssa_def *v =
            nir_fmul(b, nir_channels(b, vec, 1 << j), nir_channels(b, matrix[i], 1 << j));
         result_components[i] = (translation || j) ? nir_fadd(b, result_components[i], v) : v;
      }
   }
   return nir_vec(b, result_components, 3);
}

nir_ssa_def *
nir_build_vec3_mat_mult_pre(nir_builder *b, nir_ssa_def *vec, nir_ssa_def *matrix[])
{
   nir_ssa_def *result_components[3] = {
      nir_channel(b, matrix[0], 3),
      nir_channel(b, matrix[1], 3),
      nir_channel(b, matrix[2], 3),
   };
   return nir_build_vec3_mat_mult(b, nir_fsub(b, vec, nir_vec(b, result_components, 3)), matrix,
                                  false);
}

void
nir_build_wto_matrix_load(nir_builder *b, nir_ssa_def *instance_addr, nir_ssa_def **out)
{
   unsigned offset = offsetof(struct radv_bvh_instance_node, wto_matrix);
   for (unsigned i = 0; i < 3; ++i) {
      out[i] = nir_build_load_global(b, 4, 32, nir_iadd_imm(b, instance_addr, offset + i * 16),
                                     .align_mul = 64, .align_offset = offset + i * 16);
   }
}

/* When a hit is opaque the any_hit shader is skipped for this hit and the hit
 * is assumed to be an actual hit. */
nir_ssa_def *
hit_is_opaque(nir_builder *b, nir_ssa_def *sbt_offset_and_flags, nir_ssa_def *flags,
              nir_ssa_def *geometry_id_and_flags)
{
   nir_ssa_def *geom_force_opaque =
      nir_test_mask(b, geometry_id_and_flags, VK_GEOMETRY_OPAQUE_BIT_KHR << 28);
   nir_ssa_def *instance_force_opaque =
      nir_test_mask(b, sbt_offset_and_flags, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR << 24);
   nir_ssa_def *instance_force_non_opaque =
      nir_test_mask(b, sbt_offset_and_flags, VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR << 24);

   nir_ssa_def *opaque = geom_force_opaque;
   opaque = nir_bcsel(b, instance_force_opaque, nir_imm_bool(b, true), opaque);
   opaque = nir_bcsel(b, instance_force_non_opaque, nir_imm_bool(b, false), opaque);

   nir_ssa_def *ray_force_opaque = nir_test_mask(b, flags, SpvRayFlagsOpaqueKHRMask);
   nir_ssa_def *ray_force_non_opaque = nir_test_mask(b, flags, SpvRayFlagsNoOpaqueKHRMask);

   opaque = nir_bcsel(b, ray_force_opaque, nir_imm_bool(b, true), opaque);
   opaque = nir_bcsel(b, ray_force_non_opaque, nir_imm_bool(b, false), opaque);
   return opaque;
}

nir_ssa_def *
create_bvh_descriptor(nir_builder *b)
{
   /* We create a BVH descriptor that covers the entire memory range. That way we can always
    * use the same descriptor, which avoids divergence when different rays hit different
    * instances at the cost of having to use 64-bit node ids. */
   const uint64_t bvh_size = 1ull << 42;
   return nir_imm_ivec4(
      b, 0, 1u << 31 /* Enable box sorting */, (bvh_size - 1) & 0xFFFFFFFFu,
      ((bvh_size - 1) >> 32) | (1u << 24 /* Return IJ for triangles */) | (1u << 31));
}
