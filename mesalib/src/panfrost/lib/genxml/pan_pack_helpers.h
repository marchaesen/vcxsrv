/*
 * Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_PACK_HELPERS_H
#define PAN_PACK_HELPERS_H

#include "compiler/libcl/libcl.h"
#include "util/bitpack_helpers.h"

#ifndef __OPENCL_VERSION__
#include <inttypes.h>
#include <stdio.h>
#endif

#ifdef __OPENCL_VERSION__
#define fprintf(...)                                                           \
   do {                                                                        \
   } while (0)
#endif

static inline uint32_t
__gen_padded(uint32_t v, uint32_t start, uint32_t end)
{
   unsigned shift = __builtin_ctz(v);
   unsigned odd = v >> (shift + 1);

#ifndef NDEBUG
   assert((v >> shift) & 1);
   assert(shift <= 31);
   assert(odd <= 7);
   assert((end - start + 1) == 8);
#endif

   return util_bitpack_uint(shift | (odd << 5), start, end);
}

#define __gen_unpack_uint(out, cl, start, end)                                 \
   do {                                                                        \
      uint64_t __val = 0;                                                      \
      const int __width = (end) - (start) + 1;                                 \
      const uint64_t __mask =                                                  \
         (__width == 64) ? ~((uint64_t)0) : ((uint64_t)1 << __width) - 1;      \
                                                                               \
      for (unsigned __word = (start) / 32; __word < ((end) / 32) + 1;          \
           __word++) {                                                         \
         __val |= ((uint64_t)(cl)[__word]) << ((__word - (start) / 32) * 32);  \
      }                                                                        \
                                                                               \
      (out) = (__val >> ((start) % 32)) & __mask;                              \
   } while (0)

#define __gen_unpack_sint(out, cl, start, end)                                 \
   do {                                                                        \
      int size = (end) - (start) + 1;                                          \
      int64_t __tmp_sint;                                                      \
      __gen_unpack_uint(__tmp_sint, cl, start, end);                           \
      (out) = util_sign_extend(__tmp_sint, size);                              \
   } while (0)

#define __gen_unpack_ulod(out, cl, start, end)                                 \
   do {                                                                        \
      uint32_t __tmp_ulod;                                                     \
      __gen_unpack_uint(__tmp_ulod, cl, start, end);                           \
      (out) = ((float)__tmp_ulod) / 256.0;                                     \
   } while (0)

#define __gen_unpack_slod(out, cl, start, end)                                 \
   do {                                                                        \
      int32_t __tmp_slod;                                                      \
      __gen_unpack_sint(__tmp_slod, cl, start, end);                           \
      (out) = ((float)__tmp_slod) / 256.0;                                     \
   } while (0)

#define __gen_unpack_float(out, cl, start, end)                                \
   do {                                                                        \
      uint32_t __tmp_float;                                                    \
      __gen_unpack_uint(__tmp_float, cl, start, end);                          \
      (out) = uif(__tmp_float);                                                \
   } while (0)

#define __gen_unpack_padded(out, cl, start, end)                               \
   do {                                                                        \
      uint32_t __tmp_padded;                                                   \
      __gen_unpack_uint(__tmp_padded, cl, start, end);                         \
      (out) = (2 * (__tmp_padded >> 5) + 1) << (__tmp_padded & 0b11111);       \
   } while (0)

#define PREFIX1(A)          MALI_##A
#define PREFIX2(A, B)       MALI_##A##_##B
#define PREFIX4(A, B, C, D) MALI_##A##_##B##_##C##_##D

#define pan_pack(dst, T, name)                                                 \
   for (UNUSED struct PREFIX1(T) name = {PREFIX2(T, header)},                  \
                                 *_loop_terminate = &name;                     \
        __builtin_expect(_loop_terminate != NULL, 1); ({                       \
           PREFIX2(T, pack)((dst), &name);                                     \
           _loop_terminate = NULL;                                             \
        }))

#define pan_pack_nodefaults(dst, T, name)                                      \
   for (UNUSED struct PREFIX1(T) name = {0}, *_loop_terminate = &name;         \
        __builtin_expect(_loop_terminate != NULL, 1); ({                       \
           PREFIX2(T, pack)((dst), &name);                                     \
           _loop_terminate = NULL;                                             \
        }))

#define pan_unpack(src, T, name)                                               \
   UNUSED struct PREFIX1(T) name;                                              \
   PREFIX2(T, unpack)((src), &name)

#define pan_print(fp, T, var, indent) PREFIX2(T, print)(fp, &(var), indent)

#define pan_size(T)      PREFIX2(T, LENGTH)
#define pan_alignment(T) PREFIX2(T, ALIGN)

#define pan_section_offset(A, S) PREFIX4(A, SECTION, S, OFFSET)

/* Those APIs aren't safe in OpenCL C because we lose information on the
 * pointer address space */
#ifndef __OPENCL_VERSION__
#define pan_cast_and_pack(dst, T, name)                                        \
   pan_pack((PREFIX2(T, PACKED_T) *)dst, T, name)

#define pan_cast_and_pack_nodefaults(dst, T, name)                             \
   pan_pack_nodefaults((PREFIX2(T, PACKED_T) *)dst, T, name)

#define pan_cast_and_unpack(src, T, name)                                      \
   pan_unpack((const PREFIX2(T, PACKED_T) *)(src), T, name)

#define pan_section_ptr(base, A, S)                                            \
   ((PREFIX4(A, SECTION, S, PACKED_TYPE) *)((uint8_t *)(base) +                \
                                            pan_section_offset(A, S)))

#define pan_section_pack(dst, A, S, name)                                      \
   for (UNUSED PREFIX4(A, SECTION, S, TYPE)                                    \
           name = {PREFIX4(A, SECTION, S, header)},                            \
           *_loop_terminate = (void *)(dst);                                   \
        __builtin_expect(_loop_terminate != NULL, 1); ({                       \
           PREFIX4(A, SECTION, S, pack)(pan_section_ptr(dst, A, S), &name);    \
           _loop_terminate = NULL;                                             \
        }))

#define pan_section_unpack(src, A, S, name)                                    \
   UNUSED PREFIX4(A, SECTION, S, TYPE) name;                                   \
   PREFIX4(A, SECTION, S, unpack)(pan_section_ptr(src, A, S), &name)
#endif

#define pan_section_print(fp, A, S, var, indent)                               \
   PREFIX4(A, SECTION, S, print)(fp, &(var), indent)

static inline void
pan_merge_helper(uint32_t *dst, const uint32_t *src, size_t bytes)
{
   assert((bytes & 3) == 0);

   for (unsigned i = 0; i < (bytes / 4); ++i)
      dst[i] |= src[i];
}

#define pan_merge(packed1, packed2, type)                                      \
   pan_merge_helper((packed1).opaque, (packed2).opaque, pan_size(type))

#ifndef __OPENCL_VERSION__
static inline const char *
mali_component_swizzle(unsigned val)
{
   static const char swiz_name[] = "RGBA01??";
   static char out_str[5], *outp;
   outp = out_str;
   for (int i = 0; i < 12; i += 3) {
      *outp++ = swiz_name[(val >> i) & 7];
   }
   *outp = 0;
   return out_str;
}
#endif

/* From presentations, 16x16 tiles externally. Use shift for fast computation
 * of tile numbers. */

#define MALI_TILE_SHIFT  4
#define MALI_TILE_LENGTH (1 << MALI_TILE_SHIFT)

#endif
