/*
 * Copyright © 2017 Intel Corporation
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

#ifndef NIR_FORMAT_CONVERT_H
#define NIR_FORMAT_CONVERT_H

#include "nir_builder.h"
#include "util/format/u_formats.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline nir_def *
nir_shift_imm(nir_builder *b, nir_def *value, int left_shift)
{
   if (left_shift > 0)
      return nir_ishl_imm(b, value, left_shift);
   else if (left_shift < 0)
      return nir_ushr_imm(b, value, -left_shift);
   else
      return value;
}

static inline nir_def *
nir_shift(nir_builder *b, nir_def *value, nir_def *left_shift)
{
   return nir_bcsel(b,
                    nir_ige_imm(b, left_shift, 0),
                    nir_ishl(b, value, left_shift),
                    nir_ushr(b, value, nir_ineg(b, left_shift)));
}

static inline nir_def *
nir_mask_shift(struct nir_builder *b, nir_def *src,
               uint32_t mask, int left_shift)
{
   return nir_shift_imm(b, nir_iand_imm(b, src, mask), left_shift);
}

static inline nir_def *
nir_mask_shift_or(struct nir_builder *b, nir_def *dst, nir_def *src,
                  uint32_t src_mask, int src_left_shift)
{
   return nir_ior(b, nir_mask_shift(b, src, src_mask, src_left_shift), dst);
}

nir_def *nir_format_mask_uvec(nir_builder *b, nir_def *src,
                              const unsigned *bits);
nir_def *nir_format_sign_extend_ivec(nir_builder *b, nir_def *src,
                                     const unsigned *bits);
nir_def *nir_format_unpack_int(nir_builder *b, nir_def *packed,
                               const unsigned *bits,
                               unsigned num_components,
                               bool sign_extend);

static inline nir_def *
nir_format_unpack_uint(nir_builder *b, nir_def *packed,
                       const unsigned *bits, unsigned num_components)
{
   return nir_format_unpack_int(b, packed, bits, num_components, false);
}

static inline nir_def *
nir_format_unpack_sint(nir_builder *b, nir_def *packed,
                       const unsigned *bits, unsigned num_components)
{
   return nir_format_unpack_int(b, packed, bits, num_components, true);
}

nir_def *nir_format_pack_uint_unmasked(nir_builder *b, nir_def *color,
                                       const unsigned *bits,
                                       unsigned num_components);
nir_def *nir_format_pack_uint_unmasked_ssa(nir_builder *b, nir_def *color,
                                           nir_def *bits);
nir_def *nir_format_pack_uint(nir_builder *b, nir_def *color,
                              const unsigned *bits,
                              unsigned num_components);

nir_def *nir_format_bitcast_uvec_unmasked(nir_builder *b, nir_def *src,
                                          unsigned src_bits,
                                          unsigned dst_bits);

nir_def *nir_format_unorm_to_float(nir_builder *b, nir_def *u,
                                   const unsigned *bits);
nir_def *nir_format_snorm_to_float(nir_builder *b, nir_def *s,
                                   const unsigned *bits);
nir_def *nir_format_float_to_unorm(nir_builder *b, nir_def *f,
                                   const unsigned *bits);
nir_def *nir_format_float_to_snorm(nir_builder *b, nir_def *f,
                                   const unsigned *bits);

nir_def *nir_format_float_to_half(nir_builder *b, nir_def *f);
nir_def *nir_format_linear_to_srgb(nir_builder *b, nir_def *c);
nir_def *nir_format_srgb_to_linear(nir_builder *b, nir_def *c);

nir_def *nir_format_clamp_uint(nir_builder *b, nir_def *f,
                               const unsigned *bits);
nir_def *nir_format_clamp_sint(nir_builder *b, nir_def *f,
                               const unsigned *bits);

nir_def *nir_format_unpack_11f11f10f(nir_builder *b, nir_def *packed);
nir_def *nir_format_pack_11f11f10f(nir_builder *b, nir_def *color);
nir_def *nir_format_unpack_r9g9b9e5(nir_builder *b, nir_def *packed);
nir_def *nir_format_pack_r9g9b9e5(nir_builder *b, nir_def *color);

nir_def *nir_format_unpack_rgba(nir_builder *b, nir_def *packed,
                                enum pipe_format format);
nir_def *nir_format_pack_rgba(nir_builder *b, enum pipe_format format,
                              nir_def *rgba);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NIR_FORMAT_CONVERT_H */
