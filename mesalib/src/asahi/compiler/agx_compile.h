/*
 * Copyright (C) 2018-2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __AGX_PUBLIC_H_
#define __AGX_PUBLIC_H_

#include "compiler/nir/nir.h"
#include "util/u_dynarray.h"
#include "asahi/lib/agx_pack.h"

enum agx_push_type {
   /* Array of 64-bit pointers to the base addresses (BASES) and array of
    * 16-bit sizes for optional bounds checking (SIZES) */
   AGX_PUSH_UBO_BASES = 0,
   AGX_PUSH_UBO_SIZES = 1,
   AGX_PUSH_VBO_BASES = 2,
   AGX_PUSH_VBO_SIZES = 3,
   AGX_PUSH_SSBO_BASES = 4,
   AGX_PUSH_SSBO_SIZES = 5,

   /* Push the attached constant memory */
   AGX_PUSH_CONSTANTS = 6,

   /* Push the content of a UBO */
   AGX_PUSH_UBO_DATA = 7,

   /* RGBA blend constant (FP32) */
   AGX_PUSH_BLEND_CONST = 8,

   /* Keep last */
   AGX_PUSH_NUM_TYPES
};

struct agx_push {
   /* Contents to push */
   enum agx_push_type type : 8;

   /* Base of where to push, indexed in 16-bit units. The uniform file contains
    * 512 = 2^9 such units. */
   unsigned base : 9;

   /* Number of 16-bit units to push */
   unsigned length : 9;

   /* If set, rather than pushing the specified data, push a pointer to the
    * specified data. This is slower to access but enables indirect access, as
    * the uniform file does not support indirection. */
   bool indirect : 1;

   union {
      struct {
         uint16_t ubo;
         uint16_t offset;
      } ubo_data;
   };
};

/* Arbitrary */
#define AGX_MAX_PUSH_RANGES (16)
#define AGX_MAX_VARYINGS (32)

struct agx_varyings {
   unsigned nr_descs, nr_slots;
   struct agx_varying_packed packed[AGX_MAX_VARYINGS];
};

struct agx_shader_info {
   unsigned push_ranges;
   struct agx_push push[AGX_MAX_PUSH_RANGES];
   struct agx_varyings varyings;

   /* Does the shader read the tilebuffer? */
   bool reads_tib;

   /* Does the shader write point size? */
   bool writes_psiz;

   /* Does the shader control the sample mask? */
   bool writes_sample_mask;
};

#define AGX_MAX_RTS (8)
#define AGX_MAX_ATTRIBS (16)
#define AGX_MAX_VBUFS (16)

enum agx_format {
   AGX_FORMAT_I8 = 0,
   AGX_FORMAT_I16 = 1,
   AGX_FORMAT_I32 = 2,
   AGX_FORMAT_F16 = 3,
   AGX_FORMAT_U8NORM = 4,
   AGX_FORMAT_S8NORM = 5,
   AGX_FORMAT_U16NORM = 6,
   AGX_FORMAT_S16NORM = 7,
   AGX_FORMAT_RGB10A2 = 8,
   AGX_FORMAT_SRGBA8 = 10,
   AGX_FORMAT_RG11B10F = 12,
   AGX_FORMAT_RGB9E5 = 13,

   /* Keep last */
   AGX_NUM_FORMATS,
};

/* Returns the number of bits at the bottom of the address required to be zero.
 * That is, returns the base-2 logarithm of the minimum alignment for an
 * agx_format, where the minimum alignment is 2^n where n is the result of this
 * function. The offset argument to device_load is left-shifted by this amount
 * in the hardware */

static inline unsigned
agx_format_shift(enum agx_format format)
{
   switch (format) {
   case AGX_FORMAT_I8:
   case AGX_FORMAT_U8NORM:
   case AGX_FORMAT_S8NORM:
   case AGX_FORMAT_SRGBA8:
      return 0;

   case AGX_FORMAT_I16:
   case AGX_FORMAT_F16:
   case AGX_FORMAT_U16NORM:
   case AGX_FORMAT_S16NORM:
      return 1;

   case AGX_FORMAT_I32:
   case AGX_FORMAT_RGB10A2:
   case AGX_FORMAT_RG11B10F:
   case AGX_FORMAT_RGB9E5:
      return 2;

   default:
      unreachable("invalid format");
   }
}

struct agx_attribute {
   uint32_t divisor;

   unsigned buf : 5;
   unsigned src_offset : 16;
   unsigned nr_comps_minus_1 : 2;
   enum agx_format format : 4;
   unsigned padding : 5;
};

struct agx_vs_shader_key {
   unsigned num_vbufs;
   unsigned vbuf_strides[AGX_MAX_VBUFS];

   struct agx_attribute attributes[AGX_MAX_ATTRIBS];

   /* Set to true for clip coordinates to range [0, 1] instead of [-1, 1] */
   bool clip_halfz : 1;
};

struct agx_fs_shader_key {
   enum agx_format tib_formats[AGX_MAX_RTS];
};

struct agx_shader_key {
   union {
      struct agx_vs_shader_key vs;
      struct agx_fs_shader_key fs;
   };
};

void
agx_compile_shader_nir(nir_shader *nir,
      struct agx_shader_key *key,
      struct util_dynarray *binary,
      struct agx_shader_info *out);

static const nir_shader_compiler_options agx_nir_options = {
   .lower_scmp = true,
   .lower_flrp16 = true,
   .lower_flrp32 = true,
   .lower_ffract = true,
   .lower_fmod = true,
   .lower_fdiv = true,
   .lower_isign = true,
   .lower_iabs = true,
   .lower_fpow = true,
   .lower_find_lsb = true,
   .lower_ifind_msb = true,
   .lower_fdph = true,
   .lower_wpos_pntc = true,
   .lower_fsign = true,
   .lower_rotate = true,
   .lower_pack_split = true,
   .lower_insert_byte = true,
   .lower_insert_word = true,
   .lower_uniforms_to_ubo = true,
   .lower_cs_local_index_from_id = true,

   .lower_doubles_options = nir_lower_dmod,
   .lower_int64_options = ~(nir_lower_iadd64 | nir_lower_imul_2x32_64),

   .force_indirect_unrolling = (nir_var_shader_in | nir_var_shader_out | nir_var_function_temp),

   .has_fsub = true,
   .has_isub = true,
   .has_cs_global_id = true,

   .vectorize_io = true,
   .fuse_ffma16 = true,
   .fuse_ffma32 = true,
   .use_interpolated_input_intrinsics = true,
};

#endif
