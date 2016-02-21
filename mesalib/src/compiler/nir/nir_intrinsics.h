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


INTRINSIC(load_var, 0, ARR(), true, 0, 1, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(store_var, 1, ARR(0), false, 0, 1, 1, WRMASK, xx, xx, 0)
INTRINSIC(copy_var, 0, ARR(), false, 0, 2, 0, xx, xx, xx, 0)

/*
 * Interpolation of input.  The interp_var_at* intrinsics are similar to the
 * load_var intrinsic acting an a shader input except that they interpolate
 * the input differently.  The at_sample and at_offset intrinsics take an
 * aditional source that is a integer sample id or a vec2 position offset
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
#define BARRIER(name) INTRINSIC(name, 0, ARR(), false, 0, 0, 0, xx, xx, xx, 0)

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
INTRINSIC(shader_clock, 0, ARR(), true, 1, 0, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)

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

/**
 * Basic Geometry Shader intrinsics.
 *
 * emit_vertex implements GLSL's EmitStreamVertex() built-in.  It takes a single
 * index, which is the stream ID to write to.
 *
 * end_primitive implements GLSL's EndPrimitive() built-in.
 */
INTRINSIC(emit_vertex,   0, ARR(), false, 0, 0, 1, STREAM_ID, xx, xx, 0)
INTRINSIC(end_primitive, 0, ARR(), false, 0, 0, 1, STREAM_ID, xx, xx, 0)

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
   INTRINSIC(atomic_counter_##name##_var, 0, ARR(), true, 1, 1, 0, xx, xx, xx, flags) \
   INTRINSIC(atomic_counter_##name, 1, ARR(1), true, 1, 0, 1, BASE, xx, xx, flags)

ATOMIC(inc, 0)
ATOMIC(dec, 0)
ATOMIC(read, NIR_INTRINSIC_CAN_ELIMINATE)

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
INTRINSIC(image_size, 0, ARR(), true, 4, 1, 0, xx, xx, xx,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
INTRINSIC(image_samples, 0, ARR(), true, 1, 1, 0, xx, xx, xx,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)

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
INTRINSIC(shared_atomic_add, 2, ARR(1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(shared_atomic_imin, 2, ARR(1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(shared_atomic_umin, 2, ARR(1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(shared_atomic_imax, 2, ARR(1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(shared_atomic_umax, 2, ARR(1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(shared_atomic_and, 2, ARR(1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(shared_atomic_or, 2, ARR(1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(shared_atomic_xor, 2, ARR(1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(shared_atomic_exchange, 2, ARR(1, 1), true, 1, 0, 0, xx, xx, xx, 0)
INTRINSIC(shared_atomic_comp_swap, 3, ARR(1, 1, 1), true, 1, 0, 0, xx, xx, xx, 0)

#define SYSTEM_VALUE(name, components, num_indices, idx0, idx1, idx2) \
   INTRINSIC(load_##name, 0, ARR(), true, components, 0, num_indices, \
   idx0, idx1, idx2, \
   NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)

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
SYSTEM_VALUE(work_group_id, 3, 0, xx, xx, xx)
SYSTEM_VALUE(user_clip_plane, 4, 1, UCP_ID, xx, xx)
SYSTEM_VALUE(num_work_groups, 3, 0, xx, xx, xx)
SYSTEM_VALUE(helper_invocation, 1, 0, xx, xx, xx)

/*
 * Load operations pull data from some piece of GPU memory.  All load
 * operations operate in terms of offsets into some piece of theoretical
 * memory.  Loads from externally visible memory (UBO and SSBO) simply take a
 * byte offset as a source.  Loads from opaque memory (uniforms, inputs, etc.)
 * take a base+offset pair where the base (const_index[0]) gives the location
 * of the start of the variable being loaded and and the offset source is a
 * offset into that variable.
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

/* src[] = { offset }. const_index[] = { base } */
LOAD(uniform, 1, 1, BASE, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
/* src[] = { buffer_index, offset }. No const_index */
LOAD(ubo, 2, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
/* src[] = { offset }. const_index[] = { base } */
LOAD(input, 1, 1, BASE, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
/* src[] = { vertex, offset }. const_index[] = { base } */
LOAD(per_vertex_input, 2, 1, BASE, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
/* src[] = { buffer_index, offset }. No const_index */
LOAD(ssbo, 2, 0, xx, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
/* src[] = { offset }. const_index[] = { base } */
LOAD(output, 1, 1, BASE, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
/* src[] = { vertex, offset }. const_index[] = { base } */
LOAD(per_vertex_output, 2, 1, BASE, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)
/* src[] = { offset }. const_index[] = { base } */
LOAD(shared, 1, 1, BASE, xx, xx, NIR_INTRINSIC_CAN_ELIMINATE)

/*
 * Stores work the same way as loads, except now the first source is the value
 * to store and the second (and possibly third) source specify where to store
 * the value.  SSBO and shared memory stores also have a write mask as
 * const_index[0].
 */

#define STORE(name, srcs, num_indices, idx0, idx1, idx2, flags) \
   INTRINSIC(store_##name, srcs, ARR(0, 1, 1, 1), false, 0, 0, num_indices, idx0, idx1, idx2, flags)

/* src[] = { value, offset }. const_index[] = { base, write_mask } */
STORE(output, 2, 2, BASE, WRMASK, xx, 0)
/* src[] = { value, vertex, offset }. const_index[] = { base, write_mask } */
STORE(per_vertex_output, 3, 2, BASE, WRMASK, xx, 0)
/* src[] = { value, block_index, offset }. const_index[] = { write_mask } */
STORE(ssbo, 3, 1, WRMASK, xx, xx, 0)
/* src[] = { value, offset }. const_index[] = { base, write_mask } */
STORE(shared, 2, 2, BASE, WRMASK, xx, 0)

LAST_INTRINSIC(store_shared)
