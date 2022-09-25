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

#ifndef RADV_RT_COMMON_H
#define RADV_RT_COMMON_H

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "nir/nir_vulkan.h"

#include "compiler/spirv/spirv.h"

#include "radv_private.h"

void nir_sort_hit_pair(nir_builder *b, nir_variable *var_distances, nir_variable *var_indices,
                       uint32_t chan_1, uint32_t chan_2);

nir_ssa_def *intersect_ray_amd_software_box(struct radv_device *device, nir_builder *b,
                                            nir_ssa_def *bvh_node, nir_ssa_def *ray_tmax,
                                            nir_ssa_def *origin, nir_ssa_def *dir,
                                            nir_ssa_def *inv_dir);

nir_ssa_def *intersect_ray_amd_software_tri(struct radv_device *device, nir_builder *b,
                                            nir_ssa_def *bvh_node, nir_ssa_def *ray_tmax,
                                            nir_ssa_def *origin, nir_ssa_def *dir,
                                            nir_ssa_def *inv_dir);

nir_ssa_def *build_addr_to_node(nir_builder *b, nir_ssa_def *addr);

nir_ssa_def *build_node_to_addr(struct radv_device *device, nir_builder *b, nir_ssa_def *node);

nir_ssa_def *nir_build_vec3_mat_mult(nir_builder *b, nir_ssa_def *vec, nir_ssa_def *matrix[],
                                     bool translation);

void nir_build_wto_matrix_load(nir_builder *b, nir_ssa_def *instance_addr, nir_ssa_def **out);

nir_ssa_def *hit_is_opaque(nir_builder *b, nir_ssa_def *sbt_offset_and_flags, nir_ssa_def *flags,
                           nir_ssa_def *geometry_id_and_flags);

nir_ssa_def *create_bvh_descriptor(nir_builder *b);

/*
 * A top-level AS can contain 2^24 children and a bottom-level AS can contain 2^24
 * triangles. At a branching factor of 4, that means we may need up to 24 levels of box
 * nodes + 1 triangle node
 * + 1 instance node. Furthermore, when processing a box node, worst case we actually
 * push all 4 children and remove one, so the DFS stack depth is box nodes * 3 + 2.
 */
#define MAX_STACK_ENTRY_COUNT         76
#define MAX_STACK_LDS_ENTRY_COUNT     16
#define MAX_STACK_SCRATCH_ENTRY_COUNT (MAX_STACK_ENTRY_COUNT - MAX_STACK_LDS_ENTRY_COUNT)

struct radv_ray_traversal_args;

struct radv_leaf_intersection {
   nir_ssa_def *node_addr;
   nir_ssa_def *primitive_id;
   nir_ssa_def *geometry_id_and_flags;
   nir_ssa_def *opaque;
};

typedef void (*radv_aabb_intersection_cb)(nir_builder *b,
                                          struct radv_leaf_intersection *intersection,
                                          const struct radv_ray_traversal_args *args);

struct radv_triangle_intersection {
   struct radv_leaf_intersection base;

   nir_ssa_def *t;
   nir_ssa_def *frontface;
   nir_ssa_def *barycentrics;
};

typedef void (*radv_triangle_intersection_cb)(nir_builder *b,
                                              struct radv_triangle_intersection *intersection,
                                              const struct radv_ray_traversal_args *args);

typedef void (*radv_rt_stack_store_cb)(nir_builder *b, nir_ssa_def *index, nir_ssa_def *value,
                                       const struct radv_ray_traversal_args *args);

typedef nir_ssa_def *(*radv_rt_stack_load_cb)(nir_builder *b, nir_ssa_def *index,
                                              const struct radv_ray_traversal_args *args);

typedef void (*radv_rt_check_stack_overflow_cb)(nir_builder *b,
                                                const struct radv_ray_traversal_args *args);

struct radv_ray_traversal_vars {
   /* For each accepted hit, tmax will be set to the t value. This allows for automatic intersection
    * culling.
    */
   nir_deref_instr *tmax;

   /* Those variables change when entering and exiting BLASes. */
   nir_deref_instr *origin;
   nir_deref_instr *dir;
   nir_deref_instr *inv_dir;

   /* The base address of the current TLAS/BLAS. */
   nir_deref_instr *bvh_base;

   /* stack is the current stack pointer/index. top_stack is the pointer/index that marks the end of
    * traversal for the current BLAS/TLAS.
    */
   nir_deref_instr *stack;
   nir_deref_instr *top_stack;

   nir_deref_instr *current_node;

   /* Information about the current instance used for culling. */
   nir_deref_instr *instance_id;
   nir_deref_instr *instance_addr;
   nir_deref_instr *custom_instance_and_mask;
   nir_deref_instr *sbt_offset_and_flags;
};

struct radv_ray_traversal_args {
   nir_ssa_def *accel_struct;
   nir_ssa_def *flags;
   nir_ssa_def *cull_mask;
   nir_ssa_def *origin;
   nir_ssa_def *tmin;
   nir_ssa_def *dir;

   struct radv_ray_traversal_vars vars;

   /* The increment/decrement used for radv_ray_traversal_vars::stack */
   uint32_t stack_stride;

   radv_rt_stack_store_cb stack_store_cb;
   radv_rt_stack_load_cb stack_load_cb;
   radv_rt_check_stack_overflow_cb check_stack_overflow_cb;

   radv_aabb_intersection_cb aabb_cb;
   radv_triangle_intersection_cb triangle_cb;

   void *data;
};

/* Builds the ray traversal loop and returns whether traversal is incomplete, similar to
 * rayQueryProceedEXT. Traversal will only be considered incomplete, if one of the specified
 * callbacks breaks out of the traversal loop.
 */
nir_ssa_def *radv_build_ray_traversal(struct radv_device *device, nir_builder *b,
                                      const struct radv_ray_traversal_args *args);

#endif
