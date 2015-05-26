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
 *              num_variables, num_indices, flags)
 *
 * Which should correspond one-to-one with the nir_intrinsic_info structure. It
 * is included in both ir.h to create the nir_intrinsic enum (with members of
 * the form nir_intrinsic_(name)) and and in opcodes.c to create
 * nir_intrinsic_infos, which is a const array of nir_intrinsic_info structures
 * for each intrinsic.
 */

#define ARR(...) { __VA_ARGS__ }


INTRINSIC(load_var, 0, ARR(), true, 0, 1, 0, NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(store_var, 1, ARR(0), false, 0, 1, 0, 0)
INTRINSIC(copy_var, 0, ARR(), false, 0, 2, 0, 0)

/*
 * Interpolation of input.  The interp_var_at* intrinsics are similar to the
 * load_var intrinsic acting an a shader input except that they interpolate
 * the input differently.  The at_sample and at_offset intrinsics take an
 * aditional source that is a integer sample id or a vec2 position offset
 * respectively.
 */

INTRINSIC(interp_var_at_centroid, 0, ARR(0), true, 0, 1, 0,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
INTRINSIC(interp_var_at_sample, 1, ARR(1), true, 0, 1, 0,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
INTRINSIC(interp_var_at_offset, 1, ARR(2), true, 0, 1, 0,
          NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)

/*
 * a barrier is an intrinsic with no inputs/outputs but which can't be moved
 * around/optimized in general
 */
#define BARRIER(name) INTRINSIC(name, 0, ARR(), false, 0, 0, 0, 0)

BARRIER(discard)

/*
 * Memory barrier with semantics analogous to the memoryBarrier() GLSL
 * intrinsic.
 */
BARRIER(memory_barrier)

/** A conditional discard, with a single boolean source. */
INTRINSIC(discard_if, 1, ARR(1), false, 0, 0, 0, 0)

INTRINSIC(emit_vertex,   0, ARR(), false, 0, 0, 1, 0)
INTRINSIC(end_primitive, 0, ARR(), false, 0, 0, 1, 0)

/*
 * Atomic counters
 *
 * The *_var variants take an atomic_uint nir_variable, while the other,
 * lowered, variants take a constant buffer index and register offset.
 */

#define ATOMIC(name, flags) \
   INTRINSIC(atomic_counter_##name##_var, 0, ARR(), true, 1, 1, 0, flags) \
   INTRINSIC(atomic_counter_##name, 1, ARR(1), true, 1, 0, 1, flags)

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
INTRINSIC(image_load, 2, ARR(4, 1), true, 4, 1, 0,
          NIR_INTRINSIC_CAN_ELIMINATE)
INTRINSIC(image_store, 3, ARR(4, 1, 4), false, 0, 1, 0, 0)
INTRINSIC(image_atomic_add, 3, ARR(4, 1, 1), true, 1, 1, 0, 0)
INTRINSIC(image_atomic_min, 3, ARR(4, 1, 1), true, 1, 1, 0, 0)
INTRINSIC(image_atomic_max, 3, ARR(4, 1, 1), true, 1, 1, 0, 0)
INTRINSIC(image_atomic_and, 3, ARR(4, 1, 1), true, 1, 1, 0, 0)
INTRINSIC(image_atomic_or, 3, ARR(4, 1, 1), true, 1, 1, 0, 0)
INTRINSIC(image_atomic_xor, 3, ARR(4, 1, 1), true, 1, 1, 0, 0)
INTRINSIC(image_atomic_exchange, 3, ARR(4, 1, 1), true, 1, 1, 0, 0)
INTRINSIC(image_atomic_comp_swap, 4, ARR(4, 1, 1, 1), true, 1, 1, 0, 0)

#define SYSTEM_VALUE(name, components) \
   INTRINSIC(load_##name, 0, ARR(), true, components, 0, 0, \
   NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)

SYSTEM_VALUE(front_face, 1)
SYSTEM_VALUE(vertex_id, 1)
SYSTEM_VALUE(vertex_id_zero_base, 1)
SYSTEM_VALUE(base_vertex, 1)
SYSTEM_VALUE(instance_id, 1)
SYSTEM_VALUE(sample_id, 1)
SYSTEM_VALUE(sample_pos, 2)
SYSTEM_VALUE(sample_mask_in, 1)
SYSTEM_VALUE(invocation_id, 1)

/*
 * The first index is the address to load from, and the second index is the
 * number of array elements to load.  Indirect loads have an additional
 * register input, which is added to the constant address to compute the
 * final address to load from.  For UBO's (and SSBO's), the first source is
 * the (possibly constant) UBO buffer index and the indirect (if it exists)
 * is the second source.
 *
 * For vector backends, the address is in terms of one vec4, and so each array
 * element is +4 scalar components from the previous array element. For scalar
 * backends, the address is in terms of a single 4-byte float/int and arrays
 * elements begin immediately after the previous array element.
 */

#define LOAD(name, extra_srcs, flags) \
   INTRINSIC(load_##name, extra_srcs, ARR(1), true, 0, 0, 2, flags) \
   INTRINSIC(load_##name##_indirect, extra_srcs + 1, ARR(1, 1), \
             true, 0, 0, 2, flags)

LOAD(uniform, 0, NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
LOAD(ubo, 1, NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
LOAD(input, 0, NIR_INTRINSIC_CAN_ELIMINATE | NIR_INTRINSIC_CAN_REORDER)
/* LOAD(ssbo, 1, 0) */

/*
 * Stores work the same way as loads, except now the first register input is
 * the value or array to store and the optional second input is the indirect
 * offset.
 */

#define STORE(name, num_indices, flags) \
   INTRINSIC(store_##name, 1, ARR(0), false, 0, 0, num_indices, flags) \
   INTRINSIC(store_##name##_indirect, 2, ARR(0, 1), false, 0, 0, \
             num_indices, flags) \

STORE(output, 2, 0)
/* STORE(ssbo, 3, 0) */

LAST_INTRINSIC(store_output_indirect)
