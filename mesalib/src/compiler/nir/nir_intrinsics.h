/*
 * Copyright © 2014 Intel Corporation
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
 *
 * Authors:
 *    Connor Abbott (cwabbott0@gmail.com)
 *
 */

/**
 * This header file defines all the available intrinsics in one place. It
 * expands to a list of macros of the form:
 *
 * INTRINSIC(name, num_srcs, src_components, has_dest, dest_components,
 *              num_variables, num_indices, idx0, idx1, idx2, flags)
 *
 * Which should correspond one-to-one with the nir_intrinsic_info structure. It
 * is included in both ir.h to create the nir_intrinsic enum (with members of
 * the form nir_intrinsic_(name)) and and in opcodes.c to create
 * nir_intrinsic_infos, which is a const array of nir_intrinsic_info structures
 * for each intrinsic.
 */

#define ARR(...) { __VA_ARGS__ }

INTRINSIC(nop, 0, ARR(0), false, 0, 0, 0, xx, xx, xx,
          NIR_INTRINSIC_CAN_ELIMINATE)

INTRINSIC(load_var, 0, ARR(0), true, 0, 1, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(store_var, 1, ARR(0), false, 0, 1, 1, WRMASK, xx, xx, 0)
INTRINSIC(copy_var, 0, ARR(0), false, 0, 2, 0, xx, xx, xx, 0)

/*
 * Interpolation of input.  The interp_var_at* intrinsics are similar to the
 * load_var intrinsic acting on a shader input except that they interpolate
 * the input differently.  The at_sample and at_offset intrinsics take an
 * additional source that is an integer sample id or a vec2 position offset
 * respectively.
 */

INTRINSIC(interp_var_at_centroid, 0, ARR(0), true, 0, 1, 0, xx, xx, xx,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
INTRINSIC(interp_var_at_sample, 1, ARR(1), true, 0, 1, 0, xx, xx, xx,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
INTRINSIC(interp_var_at_offset, 1, ARR(2), true, 0, 1, 0, xx, xx, xx,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)

/*
 * Ask the driver for the size of a given buffer. It takes the buffer index
 * as source.
 */
INTRINSIC(get_buffer_size, 1, ARR(1), true, 1, 0, 0, xx, xx, xx,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)

/*
 * a barrier is an intrinsic with no inputs/outputs but which can't be moved
 * around/optimized in general
 */
#define BARRIER(name) INTRINSIC(name, 0, ARR(0), false, 0, 0, 0, xx, xx, xx, 0)

BARRIER(barrier)
BARRIER(discard)

/*
 * Memory barrier with semantics analogous to the memoryBarrier() GLSL
 * intrinsic.
 */
BARRIER(memory_barrier)

/*
 * Shader clock intrinsic with semantics analogous to the clock2x32ARB()
 * GLSL intrinsic.
 * The latter can be used as code motion barrier, which is currently not
 * feasible with NIR.
 */
INTRINSIC(shader_clock, 0, ARR(0), true, 2, 0, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)

/*
 * Shader ballot intrinsics with semantics analogous to the
 *
 *    ballotARB()
 *    readInvocationARB()
 *    readFirstInvocationARB()
 *
 * GLSL functions from ARB_shader_ballot.
 */
INTRINSIC(ballot, 1, ARR(1), true, 0, 0, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(read_invocation, 2, ARR(0, 1), true, 0, 0, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(read_first_invocation, 1, ARR(0), true, 0, 0, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)

/** Additional SPIR-V ballot intrinsics
 *
 * These correspond to the SPIR-V opcodes
 *
 *    OpGroupUniformElect
 *    OpSubgroupFirstInvocationKHR
 */
INTRINSIC(elect, 0, ARR(0), true, 1, 0, 0, xx, xx, xx,
          NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(first_invocation, 0, ARR(0), true, 1, 0, 0, xx, xx, xx,
          NIR_INTRINSIC_CAN_ELIMINATE)

/*
 * Memory barrier with semantics analogous to the compute shader
 * groupMemoryBarrier(), memoryBarrierAtomicCounter(), memoryBarrierBuffer(),
 * memoryBarrierImage() and memoryBarrierShared() GLSL intrinsics.
 */
BARRIER(group_memory_barrier)
BARRIER(memory_barrier_atomic_counter)
BARRIER(memory_barrier_buffer)
BARRIER(memory_barrier_image)
BARRIER(memory_barrier_shared)

/** A conditional discard, with a single boolean source. */
INTRINSIC(discard_if, 1, ARR(1), false, 0, 0, 0, xx, xx, xx, 0)

/** ARB_shader_group_vote intrinsics */
INTRINSIC(vote_any, 1, ARR(1), true, 1, 0, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(vote_all, 1, ARR(1), true, 1, 0, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(vote_feq, 1, ARR(0), true, 1, 0, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(vote_ieq, 1, ARR(0), true, 1, 0, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)

/** Ballot ALU operations from SPIR-V.
 *
 * These operations work like their ALU counterparts except that the operate
 * on a uvec4 which is treated as a 128bit integer.  Also, they are, in
 * general, free to ignore any bits which are above the subgroup size.
 */
INTRINSIC(ballot_bitfield_extract, 2, ARR(4, 1), true, 1, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(ballot_bit_count_reduce, 1, ARR(4), true, 1, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(ballot_bit_count_inclusive, 1, ARR(4), true, 1, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(ballot_bit_count_exclusive, 1, ARR(4), true, 1, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(ballot_find_lsb, 1, ARR(4), true, 1, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(ballot_find_msb, 1, ARR(4), true, 1, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)

/** Shuffle operations from SPIR-V. */
INTRINSIC(shuffle, 2, ARR(0, 1), true, 0, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(shuffle_xor, 2, ARR(0, 1), true, 0, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(shuffle_up, 2, ARR(0, 1), true, 0, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(shuffle_down, 2, ARR(0, 1), true, 0, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)

/** Quad operations from SPIR-V. */
INTRINSIC(quad_broadcast, 2, ARR(0, 1), true, 0, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(quad_swap_horizontal, 1, ARR(0), true, 0, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(quad_swap_vertical, 1, ARR(0), true, 0, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(quad_swap_diagonal, 1, ARR(0), true, 0, 0,
          0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)

INTRINSIC(reduce, 1, ARR(0), true, 0, 0,
          2, REDUCTION_OP, CLUSTER_SIZE, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(inclusive_scan, 1, ARR(0), true, 0, 0,
          1, REDUCTION_OP, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(exclusive_scan, 1, ARR(0), true, 0, 0,
          1, REDUCTION_OP, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)

/**
 * Basic Geometry Shader intrinsics.
 *
 * emit_vertex implements GLSL's EmitStreamVertex() built-in.  It takes a single
 * index, which is the stream ID to write to.
 *
 * end_primitive implements GLSL's EndPrimitive() built-in.
 */
INTRINSIC(emit_vertex,   0, ARR(0), false, 0, 0, 1, STREAM_ID, xx, xx, 0)
INTRINSIC(end_primitive, 0, ARR(0), false, 0, 0, 1, STREAM_ID, xx, xx, 0)

/**
 * Geometry Shader intrinsics with a vertex count.
 *
 * Alternatively, drivers may implement these intrinsics, and use
 * nir_lower_gs_intrinsics() to convert from the basic intrinsics.
 *
 * These maintain a count of the number of vertices emitted, as an additional
 * unsigned integer source.
 */
INTRINSIC(emit_vertex_with_counter, 1, ARR(1), false, 0, 0, 1, STREAM_ID, xx, xx, 0)
INTRINSIC(end_primitive_with_counter, 1, ARR(1), false, 0, 0, 1, STREAM_ID, xx, xx, 0)
INTRINSIC(set_vertex_count, 1, ARR(1), false, 0, 0, 0, xx, xx, xx, 0)

/*
 * Atomic counters
 *
 * The *_var variants take an atomic_uint nir_variable, while the other,
 * lowered, variants take a constant buffer index and register offset.
 */

#define ATOMIC(name, flags) \
   INTRINSIC(name##_var, 0, ARR(0), true, 1, 1, 0, xx, xx, xx, flags) \
   INTRINSIC(name, 1, ARR(1), true, 1, 0, 1, BASE, xx, xx, flags)
#define ATOMIC2(name) \
   INTRINSIC(name##_var, 1, ARR(1), true, 1, 1, 0, xx, xx, xx, 0) \
   INTRINSIC(name, 2, ARR(1, 1), true, 1, 0, 1, BASE, xx, xx, 0)
#define ATOMIC3(name) \
   INTRINSIC(name##_var, 2, ARR(1, 1), true, 1, 1, 0, xx, xx, xx, 0) \
   INTRINSIC(name, 3, ARR(1, 1, 1), true, 1, 0, 1, BASE, xx, xx, 0)

ATOMIC(atomic_counter_inc, 0)
ATOMIC(atomic_counter_dec, 0)
ATOMIC(atomic_counter_read, NIR_INTRINSIC_CAN_ELIMINATE)
ATOMIC2(atomic_counter_add)
ATOMIC2(atomic_counter_min)
ATOMIC2(atomic_counter_max)
ATOMIC2(atomic_counter_and)
ATOMIC2(atomic_counter_or)
ATOMIC2(atomic_counter_xor)
ATOMIC2(atomic_counter_exchange)
ATOMIC3(atomic_counter_comp_swap)

/*
 * Image load, store and atomic intrinsics.
 *
 * All image intrinsics take an image target passed as a nir_variable.  Image
 * variables contain a number of memory and layout qualifiers that influence
 * the semantics of the intrinsic.
 *
 * All image intrinsics take a four-coordinate vector and a sample index as
 * first two sources, determining the location within the image that will be
 * accessed by the intrinsic.  Components not applicable to the image target
 * in use are undefined.  Image store takes an additional four-component
 * argument with the value to be written, and image atomic operations take
 * either one or two additional scalar arguments with the same meaning as in
 * the ARB_shader_image_load_store specification.
 */
INTRINSIC(image_load, 2, ARR(4, 1), true, 4, 1, 0, xx, xx, xx,
          NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(image_store, 3, ARR(4, 1, 4), false, 0, 1, 0, xx, xx, xx, 0)
INTRINSIC(image_atomic_add, 3, ARR(4, 1, 1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(image_atomic_min, 3, ARR(4, 1, 1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(image_atomic_max, 3, ARR(4, 1, 1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(image_atomic_and, 3, ARR(4, 1, 1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(image_atomic_or, 3, ARR(4, 1, 1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(image_atomic_xor, 3, ARR(4, 1, 1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(image_atomic_exchange, 3, ARR(4, 1, 1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(image_atomic_comp_swap, 4, ARR(4, 1, 1, 1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(image_size, 0, ARR(0), true, 0, 1, 0, xx, xx, xx,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
INTRINSIC(image_samples, 0, ARR(0), true, 1, 1, 0, xx, xx, xx,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)

/*
 * Vulkan descriptor set intrinsics
 *
 * The Vulkan API uses a different binding model from GL.  In the Vulkan
 * API, all external resources are represented by a tuple:
 *
 * (descriptor set, binding, array index)
 *
 * where the array index is the only thing allowed to be indirect.  The
 * vulkan_surface_index intrinsic takes the descriptor set and binding as
 * its first two indices and the array index as its source.  The third
 * index is a nir_variable_mode in case that's useful to the backend.
 *
 * The intended usage is that the shader will call vulkan_surface_index to
 * get an index and then pass that as the buffer index ubo/ssbo calls.
 *
 * The vulkan_resource_reindex intrinsic takes a resource index in src0
 * (the result of a vulkan_resource_index or vulkan_resource_reindex) which
 * corresponds to the tuple (set, binding, index) and computes an index
 * corresponding to tuple (set, binding, idx + src1).
 */
INTRINSIC(vulkan_resource_index, 1, ARR(1), true, 1, 0, 2,
          DESC_SET, BINDING, xx,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
INTRINSIC(vulkan_resource_reindex, 2, ARR(1, 1), true, 1, 0, 0, xx, xx, xx,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)

/*
 * variable atomic intrinsics
 *
 * All of these variable atomic memory operations read a value from memory,
 * compute a new value using one of the operations below, write the new value
 * to memory, and return the original value read.
 *
 * All operations take 1 source except CompSwap that takes 2. These sources
 * represent:
 *
 * 0: The data parameter to the atomic function (i.e. the value to add
 *    in shared_atomic_add, etc).
 * 1: For CompSwap only: the second data parameter.
 *
 * All operations take 1 variable deref.
 */
INTRINSIC(var_atomic_add, 1, ARR(1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(var_atomic_imin, 1, ARR(1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(var_atomic_umin, 1, ARR(1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(var_atomic_imax, 1, ARR(1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(var_atomic_umax, 1, ARR(1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(var_atomic_and, 1, ARR(1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(var_atomic_or, 1, ARR(1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(var_atomic_xor, 1, ARR(1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(var_atomic_exchange, 1, ARR(1), true, 1, 1, 0, xx, xx, xx, 0)
INTRINSIC(var_atomic_comp_swap, 2, ARR(1, 1), true, 1, 1, 0, xx, xx, xx, 0)

/*
 * SSBO atomic intrinsics
 *
 * All of the SSBO atomic memory operations read a value from memory,
 * compute a new value using one of the operations below, write the new
 * value to memory, and return the original value read.
 *
 * All operations take 3 sources except CompSwap that takes 4. These
 * sources represent:
 *
 * 0: The SSBO buffer index.
 * 1: The offset into the SSBO buffer of the variable that the atomic
 *    operation will operate on.
 * 2: The data parameter to the atomic function (i.e. the value to add
 *    in ssbo_atomic_add, etc).
 * 3: For CompSwap only: the second data parameter.
 */
INTRINSIC(ssbo_atomic_add, 3, ARR(1, 1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(ssbo_atomic_imin, 3, ARR(1, 1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(ssbo_atomic_umin, 3, ARR(1, 1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(ssbo_atomic_imax, 3, ARR(1, 1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(ssbo_atomic_umax, 3, ARR(1, 1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(ssbo_atomic_and, 3, ARR(1, 1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(ssbo_atomic_or, 3, ARR(1, 1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(ssbo_atomic_xor, 3, ARR(1, 1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(ssbo_atomic_exchange, 3, ARR(1, 1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(ssbo_atomic_comp_swap, 4, ARR(1, 1, 1, 1), true, 1, 0, 0, xx, xx, xx, 0)

/*
 * CS shared variable atomic intrinsics
 *
 * All of the shared variable atomic memory operations read a value from
 * memory, compute a new value using one of the operations below, write the
 * new value to memory, and return the original value read.
 *
 * All operations take 2 sources except CompSwap that takes 3. These
 * sources represent:
 *
 * 0: The offset into the shared variable storage region that the atomic
 *    operation will operate on.
 * 1: The data parameter to the atomic function (i.e. the value to add
 *    in shared_atomic_add, etc).
 * 2: For CompSwap only: the second data parameter.
 */
INTRINSIC(shared_atomic_add, 2, ARR(1, 1), true, 1, 0, 1, BASE, xx, xx, 0)
INTRINSIC(shared_atomic_imin, 2, ARR(1, 1), true, 1, 0, 1, BASE, xx, xx, 0)
INTRINSIC(shared_atomic_umin, 2, ARR(1, 1), true, 1, 0, 1, BASE, xx, xx, 0)
INTRINSIC(shared_atomic_imax, 2, ARR(1, 1), true, 1, 0, 1, BASE, xx, xx, 0)
INTRINSIC(shared_atomic_umax, 2, ARR(1, 1), true, 1, 0, 1, BASE, xx, xx, 0)
INTRINSIC(shared_atomic_and, 2, ARR(1, 1), true, 1, 0, 1, BASE, xx, xx, 0)
INTRINSIC(shared_atomic_or, 2, ARR(1, 1), true, 1, 0, 1, BASE, xx, xx, 0)
INTRINSIC(shared_atomic_xor, 2, ARR(1, 1), true, 1, 0, 1, BASE, xx, xx, 0)
INTRINSIC(shared_atomic_exchange, 2, ARR(1, 1), true, 1, 0, 1, BASE, xx, xx, 0)
INTRINSIC(shared_atomic_comp_swap, 3, ARR(1, 1, 1), true, 1, 0, 1, BASE, xx, xx, 0)

/* Used by nir_builder.h to generate loader helpers for the system values. */
#ifndef DEFINE_SYSTEM_VALUE
#define DEFINE_SYSTEM_VALUE(name)
#endif

#define SYSTEM_VALUE(name, components, num_indices, idx0, idx1, idx2) \
   DEFINE_SYSTEM_VALUE(name) \
   INTRINSIC(load_##name, 0, ARR(0), true, components, 0, num_indices, \
   idx0, idx1, idx2, \
   NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)

SYSTEM_VALUE(frag_coord, 4, 0, xx, xx, xx)
SYSTEM_VALUE(front_face, 1, 0, xx, xx, xx)
SYSTEM_VALUE(vertex_id, 1, 0, xx, xx, xx)
SYSTEM_VALUE(vertex_id_zero_base, 1, 0, xx, xx, xx)
SYSTEM_VALUE(base_vertex, 1, 0, xx, xx, xx)
SYSTEM_VALUE(instance_id, 1, 0, xx, xx, xx)
SYSTEM_VALUE(base_instance, 1, 0, xx, xx, xx)
SYSTEM_VALUE(draw_id, 1, 0, xx, xx, xx)
SYSTEM_VALUE(sample_id, 1, 0, xx, xx, xx)
SYSTEM_VALUE(sample_pos, 2, 0, xx, xx, xx)
SYSTEM_VALUE(sample_mask_in, 1, 0, xx, xx, xx)
SYSTEM_VALUE(primitive_id, 1, 0, xx, xx, xx)
SYSTEM_VALUE(invocation_id, 1, 0, xx, xx, xx)
SYSTEM_VALUE(tess_coord, 3, 0, xx, xx, xx)
SYSTEM_VALUE(tess_level_outer, 4, 0, xx, xx, xx)
SYSTEM_VALUE(tess_level_inner, 2, 0, xx, xx, xx)
SYSTEM_VALUE(patch_vertices_in, 1, 0, xx, xx, xx)
SYSTEM_VALUE(local_invocation_id, 3, 0, xx, xx, xx)
SYSTEM_VALUE(local_invocation_index, 1, 0, xx, xx, xx)
SYSTEM_VALUE(work_group_id, 3, 0, xx, xx, xx)
SYSTEM_VALUE(user_clip_plane, 4, 1, UCP_ID, xx, xx)
SYSTEM_VALUE(num_work_groups, 3, 0, xx, xx, xx)
SYSTEM_VALUE(helper_invocation, 1, 0, xx, xx, xx)
SYSTEM_VALUE(alpha_ref_float, 1, 0, xx, xx, xx)
SYSTEM_VALUE(layer_id, 1, 0, xx, xx, xx)
SYSTEM_VALUE(view_index, 1, 0, xx, xx, xx)
SYSTEM_VALUE(subgroup_size, 1, 0, xx, xx, xx)
SYSTEM_VALUE(subgroup_invocation, 1, 0, xx, xx, xx)
SYSTEM_VALUE(subgroup_eq_mask, 0, 0, xx, xx, xx)
SYSTEM_VALUE(subgroup_ge_mask, 0, 0, xx, xx, xx)
SYSTEM_VALUE(subgroup_gt_mask, 0, 0, xx, xx, xx)
SYSTEM_VALUE(subgroup_le_mask, 0, 0, xx, xx, xx)
SYSTEM_VALUE(subgroup_lt_mask, 0, 0, xx, xx, xx)
SYSTEM_VALUE(num_subgroups, 1, 0, xx, xx, xx)
SYSTEM_VALUE(subgroup_id, 1, 0, xx, xx, xx)
SYSTEM_VALUE(local_group_size, 3, 0, xx, xx, xx)

/* Blend constant color values.  Float values are clamped. */
SYSTEM_VALUE(blend_const_color_r_float, 1, 0, xx, xx, xx)
SYSTEM_VALUE(blend_const_color_g_float, 1, 0, xx, xx, xx)
SYSTEM_VALUE(blend_const_color_b_float, 1, 0, xx, xx, xx)
SYSTEM_VALUE(blend_const_color_a_float, 1, 0, xx, xx, xx)
SYSTEM_VALUE(blend_const_color_rgba8888_unorm, 1, 0, xx, xx, xx)
SYSTEM_VALUE(blend_const_color_aaaa8888_unorm, 1, 0, xx, xx, xx)

/**
 * Barycentric coordinate intrinsics.
 *
 * These set up the barycentric coordinates for a particular interpolation.
 * The first three are for the simple cases: pixel, centroid, or per-sample
 * (at gl_SampleID).  The next two handle interpolating at a specified
 * sample location, or interpolating with a vec2 offset,
 *
 * The interp_mode index should be either the INTERP_MODE_SMOOTH or
 * INTERP_MODE_NOPERSPECTIVE enum values.
 *
 * The vec2 value produced by these intrinsics is intended for use as the
 * barycoord source of a load_interpolated_input intrinsic.
 */

#define BARYCENTRIC(name, sources, source_components) \
   INTRINSIC(load_barycentric_##name, sources, ARR(source_components), \
             true, 2, 0, 1, INTERP_MODE, xx, xx, \
             NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)

/* no sources.  const_index[] = { interp_mode } */
BARYCENTRIC(pixel, 0, 0)
BARYCENTRIC(centroid, 0, 0)
BARYCENTRIC(sample, 0, 0)
/* src[] = { sample_id }.  const_index[] = { interp_mode } */
BARYCENTRIC(at_sample, 1, 1)
/* src[] = { offset.xy }.  const_index[] = { interp_mode } */
BARYCENTRIC(at_offset, 1, 2)

/*
 * Load operations pull data from some piece of GPU memory.  All load
 * operations operate in terms of offsets into some piece of theoretical
 * memory.  Loads from externally visible memory (UBO and SSBO) simply take a
 * byte offset as a source.  Loads from opaque memory (uniforms, inputs, etc.)
 * take a base+offset pair where the base (const_index[0]) gives the location
 * of the start of the variable being loaded and and the offset source is a
 * offset into that variable.
 *
 * Uniform load operations have a second "range" index that specifies the
 * range (starting at base) of the data from which we are loading.  If
 * const_index[1] == 0, then the range is unknown.
 *
 * Some load operations such as UBO/SSBO load and per_vertex loads take an
 * additional source to specify which UBO/SSBO/vertex to load from.
 *
 * The exact address type depends on the lowering pass that generates the
 * load/store intrinsics.  Typically, this is vec4 units for things such as
 * varying slots and float units for fragment shader inputs.  UBO and SSBO
 * offsets are always in bytes.
 */

#define LOAD(name, srcs, num_indices, idx0, idx1, idx2, flags) \
   INTRINSIC(load_##name, srcs, ARR(1, 1, 1, 1), true, 0, 0, num_indices, idx0, idx1, idx2, flags)

/* src[] = { offset }. const_index[] = { base, range } */
LOAD(uniform, 1, 2, BASE, RANGE, xx, NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
/* src[] = { buffer_index, offset }. No const_index */
LOAD(ubo, 2, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
/* src[] = { offset }. const_index[] = { base, component } */
LOAD(input, 1, 2, BASE, COMPONENT, xx, NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
/* src[] = { vertex, offset }. const_index[] = { base, component } */
LOAD(per_vertex_input, 2, 2, BASE, COMPONENT, xx, NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
/* src[] = { barycoord, offset }. const_index[] = { base, component } */
INTRINSIC(load_interpolated_input, 2, ARR(2, 1), true, 0, 0,
          2, BASE, COMPONENT, xx,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)

/* src[] = { buffer_index, offset }. No const_index */
LOAD(ssbo, 2, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
/* src[] = { offset }. const_index[] = { base, component } */
LOAD(output, 1, 2, BASE, COMPONENT, xx, NIR_INTRINSIC_CAN_ELIMINATE)
/* src[] = { vertex, offset }. const_index[] = { base, component } */
LOAD(per_vertex_output, 2, 1, BASE, COMPONENT, xx, NIR_INTRINSIC_CAN_ELIMINATE)
/* src[] = { offset }. const_index[] = { base } */
LOAD(shared, 1, 1, BASE, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
/* src[] = { offset }. const_index[] = { base, range } */
LOAD(push_constant, 1, 2, BASE, RANGE, xx,
     NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)

/*
 * Stores work the same way as loads, except now the first source is the value
 * to store and the second (and possibly third) source specify where to store
 * the value.  SSBO and shared memory stores also have a write mask as
 * const_index[0].
 */

#define STORE(name, srcs, num_indices, idx0, idx1, idx2, flags) \
   INTRINSIC(store_##name, srcs, ARR(0, 1, 1, 1), false, 0, 0, num_indices, idx0, idx1, idx2, flags)

/* src[] = { value, offset }. const_index[] = { base, write_mask, component } */
STORE(output, 2, 3, BASE, WRMASK, COMPONENT, 0)
/* src[] = { value, vertex, offset }.
 * const_index[] = { base, write_mask, component }
 */
STORE(per_vertex_output, 3, 3, BASE, WRMASK, COMPONENT, 0)
/* src[] = { value, block_index, offset }. const_index[] = { write_mask } */
STORE(ssbo, 3, 1, WRMASK, xx, xx, 0)
/* src[] = { value, offset }. const_index[] = { base, write_mask } */
STORE(shared, 2, 2, BASE, WRMASK, xx, 0)

LAST_INTRINSIC(store_shared)

#undef DEFINE_SYSTEM_VALUE
#undef INTRINSIC
#undef LAST_INTRINSIC
