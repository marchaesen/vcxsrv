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

nir_ssa_def *nir_build_vec3_mat_mult_pre(nir_builder *b, nir_ssa_def *vec, nir_ssa_def *matrix[]);

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
#define MAX_STACK_ENTRY_COUNT 76

#endif
