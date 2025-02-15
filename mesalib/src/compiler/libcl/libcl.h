/*
 * Copyright 2024 Valve Corporation
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * This header adds definitions that are common between the CPU and the GPU for
 * shared headers. It also fills in basic standard library holes for internal
 * OpenCL.
 */

#ifndef __OPENCL_VERSION__

/* The OpenCL version of this header defines many OpenCL versions of stdint.h
 * and util/macros.h functions. #include both here for consistency in shared
 * headers.
 */
#include <stdint.h>
#include "util/macros.h"

/* Structures defined in common host/device headers that include device pointers
 * need to resolve to a real pointer in OpenCL but an opaque 64-bit address on
 * the host. The DEVICE macro facilitates that.
 */
#define DEVICE(type_) uint64_t

/* However, inline functions defined in common host/device headers that take
 * pointers need to resolve to pointers on either host or device. (Host pointers
 * on the host, device pointers on the device.) This would be automatic with
 * OpenCL generic pointers, but those can cause headaches and lose constantness,
 * so these defines allow GLOBAL/CONST keywords to be used even in CPU code.
 * Annoyingly, we can't use global/constant here because it conflicts with C++
 * standard library headers.
 */
#define GLOBAL
#define CONST const

#else

/* GenXML likes to use fp16. Since fp16 is supported by all grown up drivers, we
 * just enable the extension everywhere.
 */
#pragma OPENCL EXTENSION cl_khr_fp16 : enable

/* The OpenCL side of DEVICE must resolve to real pointer types, unlike
 * the host version.
 */
#define DEVICE(type_)   global type_ *

/* Passthrough */
#define GLOBAL global
#define CONST constant

/* OpenCL lacks explicitly sized integer types, but we know the sizes of
 * particular integer types. These typedefs allow defining common headers with
 * explicit integer types (and therefore compatible data layouts).
 */
typedef ulong uint64_t;
typedef uint uint32_t;
typedef ushort uint16_t;
typedef uchar uint8_t;

typedef long int64_t;
typedef int int32_t;
typedef short int16_t;
typedef char int8_t;

/* OpenCL C defines work-item functions to return a scalar for a particular
 * dimension. This is a really annoying papercut, and is not what you want for
 * either 1D or 3D dispatches.  In both cases, it's nicer to get vectors. For
 * syntax, we opt to define uint3 "magic globals" for each work-item vector.
 * This matches the GLSL convention, although retaining OpenCL names. For
 * example, `gl_GlobalInvocationID.xy` is expressed here as `cl_global_id.xy`.
 * That is much nicer than standard OpenCL C's syntax `(uint2)(get_global_id(0),
 * get_global_id(1))`.
 *
 * We define the obvious mappings for each relevant function in "Work-Item
 * Functions" in the OpenCL C specification.
 */
#define _CL_WORKITEM3(name) ((uint3)(name(0), name(1), name(2)))

#define cl_global_size         _CL_WORKITEM3(get_global_size)
#define cl_global_id           _CL_WORKITEM3(get_global_id)
#define cl_local_size          _CL_WORKITEM3(get_local_size)
#define cl_enqueued_local_size _CL_WORKITEM3(get_enqueued_local_size)
#define cl_local_id            _CL_WORKITEM3(get_local_id)
#define cl_num_groups          _CL_WORKITEM3(get_num_groups)
#define cl_group_id            _CL_WORKITEM3(get_group_id)
#define cl_global_offset       _CL_WORKITEM3(get_global_offset)

/* OpenCL C lacks static_assert, a part of C11. This makes static_assert
 * available on both host and device. It is defined as variadic to handle also
 * no-message static_asserts (standardized in C23).
 */
#define _S(x) #x
#define _PASTE_(x, y) x##y
#define _PASTE(x, y) _PASTE_(x, y)
#define static_assert(_COND, ...)                                              \
   typedef char _PASTE(static_assertion, __LINE__)[(_COND) ? 1 : -1]

/* NIR's precompilation infrastructure requires specifying a workgroup size with
 * the kernel, via reqd_work_group_size. Unfortunately, reqd_work_group_size has
 * terrible ergonomics, so we provide these aliases instead.
 */
#define KERNEL3D(x, y, z)                                                      \
   __attribute__((reqd_work_group_size(x, y, z))) kernel void

#define KERNEL2D(x, y)   KERNEL3D(x, y, 1)
#define KERNEL(x)        KERNEL2D(x, 1)

/* stddef.h usually defines this. We don't have that on the OpenCL side but we
 * can use the builtin.
 */
#define offsetof(x, y) __builtin_offsetof(x, y)

/* This is not an exact match for the util/macros.h version but without the
 * aligned(4) we get garbage code gen and in practice this is what you want.
 */
#define PACKED __attribute__((packed, aligned(4)))

/* OpenCL C doesn't seem to have an equivalent for this but it doesn't matter.
 * Compare util/macros.h
 */
#define ENUM_PACKED

/* FILE * pointers can be useful in function signatures shared across
 * host/device, but are meaningless in OpenCL. Turn them into void* to allow
 * consistent prototype across host/device even though there won't be an actual
 * file pointer on the device side.
 */
#define FILE void

/* OpenCL C lacks a standard memcpy, but clang has one that will be plumbed into
 * a NIR memcpy intrinsic. This is not a competent implementation of memcpy for
 * large amounts of data, since it's necessarily single threaded, but memcpy is
 * too useful for shared CPU/GPU code that it's worth making the standard
 * library function work.
 */
#define memcpy __builtin_memcpy

/* OpenCL C lacks a standard abort, so we plumb through the NIR intrinsic. */
void nir_printf_abort(void);
static inline void abort(void) { nir_printf_abort(); }

/* OpenCL C lacks a standard assert. We implement one on top of abort. We are
 * careful to use a single printf so the lines don't get split up if multiple
 * threads assert in parallel.
 */
#ifndef NDEBUG
#define _ASSERT_STRING(x) _ASSERT_STRING_INNER(x)
#define _ASSERT_STRING_INNER(x) #x
#define assert(x) if (!(x)) { \
   printf("Shader assertion fail at " __FILE__ ":" \
          _ASSERT_STRING(__LINE__) "\nExpected " #x "\n\n"); \
   nir_printf_abort(); \
}
#else
#define assert(x)
#endif

/* This is the unreachable macro from macros.h that uses __builtin_unreachable,
 * which is a clang builtin available in OpenCL C.
 */
#define unreachable(str)                                                       \
   do {                                                                        \
      assert(!"" str);                                                         \
      __builtin_unreachable();                                                 \
   } while (0)

/* Core OpenCL C like likely/unlikely. We might be able to map to a clang built
 * in though...
 */
#define likely(x) (x)
#define unlikely(x) (x)

/* These duplicate the C standard library and are required for the
 * u_intN_min/max implementations.
 */
#define UINT64_MAX 18446744073709551615ul
#define INT64_MAX 9223372036854775807l

/* These duplicate util/macros.h. This could maybe be cleaned up */
#define BITFIELD_BIT(b)  (1u << b)
#define BITFIELD_MASK(m) (((m) == 32) ? 0xffffffff : ((1u << (m)) - 1))
#define ASSERTED
#define ALWAYS_INLINE
#define UNUSED

static inline int64_t
u_intN_max(unsigned bit_size)
{
   assert(bit_size <= 64 && bit_size > 0);
   return INT64_MAX >> (64 - bit_size);
}

static inline int64_t
u_intN_min(unsigned bit_size)
{
   return (-u_intN_max(bit_size)) - 1;
}

static inline uint64_t
u_uintN_max(unsigned bit_size)
{
   assert(bit_size <= 64 && bit_size > 0);
   return UINT64_MAX >> (64 - bit_size);
}

static inline uint
align(uint x, uint y)
{
   return (x + y - 1) & ~(y - 1);
}

static inline uint32_t
util_logbase2(uint32_t n)
{
   return (31 - clz(n | 1));
}

static inline uint32_t
util_logbase2_ceil(uint32_t n)
{
   return (n <= 1) ? 0 : 32 - clz(n - 1);
}

#define BITFIELD64_MASK(x) ((x == 64) ? ~0ul : ((1ul << x) - 1))
#define IS_POT(v)          (((v) & ((v) - 1)) == 0)
#define IS_POT_NONZERO(v)  ((v) != 0 && IS_POT(v))
#define DIV_ROUND_UP(A, B)      (((A) + (B) - 1) / (B))
#define CLAMP(X, MIN, MAX)      ((X) > (MIN) ? ((X) > (MAX) ? (MAX) : (X)) : (MIN))
#define ALIGN_POT(x, pot_align) (((x) + (pot_align) - 1) & ~((pot_align) - 1))

/* TODO: Should we define with OpenCL min/max? Do we want to match the host? */
#define MAX2( A, B )   ( (A)>(B) ? (A) : (B) )
#define MIN2( A, B )   ( (A)<(B) ? (A) : (B) )

/* Less worried about these matching */
#define MIN3(a, b, c)           min(min(a, b), c)
#define MAX3(a, b, c)           max(max(a, b), c)

static inline uint32_t
fui(float f)
{
   return as_uint(f);
}

static inline float
uif(uint32_t ui)
{
   return as_float(ui);
}

#define CL_FLT_EPSILON 1.1920928955078125e-7f

/* OpenCL C lacks roundf and llroundf, we can emulate it */
static inline float roundf(float x)
{
   return trunc(x + copysign(0.5f - 0.25f * CL_FLT_EPSILON, x));
}

static inline long long llroundf(float x)
{
   return roundf(x);
}

static inline uint16_t
_mesa_float_to_half(float f)
{
   return as_ushort(convert_half(f));
}

static inline float
_mesa_half_to_float(uint16_t w)
{
   return convert_float(as_half(w));
}

/* Duplicates u_math.h. We should make that header CL safe at some point...
 */
static inline int64_t
util_sign_extend(uint64_t val, unsigned width)
{
   unsigned shift = 64 - width;
   return (int64_t)(val << shift) >> shift;
}

#endif
