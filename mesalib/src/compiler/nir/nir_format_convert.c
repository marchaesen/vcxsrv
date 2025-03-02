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

#include "nir_format_convert.h"

#include "util/format/u_format.h"
#include "util/format_rgb9e5.h"
#include "util/macros.h"

nir_def *
nir_format_mask_uvec(nir_builder *b, nir_def *src, const unsigned *bits)
{
   nir_const_value mask[NIR_MAX_VEC_COMPONENTS];
   memset(mask, 0, sizeof(mask));
   for (unsigned i = 0; i < src->num_components; i++) {
      assert(bits[i] <= 32);
      mask[i].u32 = BITFIELD_MASK(bits[i]);
   }
   return nir_iand(b, src, nir_build_imm(b, src->num_components, 32, mask));
}

nir_def *
nir_format_sign_extend_ivec(nir_builder *b, nir_def *src,
                            const unsigned *bits)
{
   assert(src->num_components <= 4);
   nir_def *comps[4];
   for (unsigned i = 0; i < src->num_components; i++) {
      unsigned shift = src->bit_size - bits[i];
      comps[i] = nir_ishr_imm(b, nir_ishl_imm(b, nir_channel(b, src, i), shift),
                              shift);
   }
   return nir_vec(b, comps, src->num_components);
}

nir_def *
nir_format_unpack_int(nir_builder *b, nir_def *packed,
                      const unsigned *bits, unsigned num_components,
                      bool sign_extend)
{
   assert(num_components >= 1 && num_components <= 4);
   const unsigned bit_size = packed->bit_size;
   nir_def *comps[4];

   if (bits[0] >= bit_size) {
      assert(bits[0] == bit_size);
      assert(num_components == 1);
      return packed;
   }

   unsigned next_chan = 0;
   unsigned offset = 0;
   for (unsigned i = 0; i < num_components; i++) {
      assert(bits[i] < bit_size);
      assert(offset + bits[i] <= bit_size);
      if (bits[i] == 0) {
         comps[i] = nir_imm_int(b, 0);
         continue;
      }

      nir_def *chan = nir_channel(b, packed, next_chan);
      unsigned lshift = bit_size - (offset + bits[i]);
      unsigned rshift = bit_size - bits[i];
      if (sign_extend)
         comps[i] = nir_ishr_imm(b, nir_ishl_imm(b, chan, lshift), rshift);
      else
         comps[i] = nir_ushr_imm(b, nir_ishl_imm(b, chan, lshift), rshift);
      offset += bits[i];
      if (offset >= bit_size) {
         next_chan++;
         offset -= bit_size;
      }
   }

   return nir_vec(b, comps, num_components);
}

nir_def *
nir_format_pack_uint_unmasked(nir_builder *b, nir_def *color,
                              const unsigned *bits, unsigned num_components)
{
   assert(num_components >= 1 && num_components <= 4);
   nir_def *packed = nir_imm_int(b, 0);
   unsigned offset = 0;

   color = nir_u2u32(b, color);
   for (unsigned i = 0; i < num_components; i++) {
      if (bits[i] == 0)
         continue;

      packed = nir_ior(b, packed, nir_shift_imm(b, nir_channel(b, color, i), offset));
      offset += bits[i];
   }
   assert(offset <= packed->bit_size);

   return packed;
}

nir_def *
nir_format_pack_uint_unmasked_ssa(nir_builder *b, nir_def *color,
                                  nir_def *bits)
{
   nir_def *packed = nir_imm_int(b, 0);
   nir_def *offset = nir_imm_int(b, 0);

   color = nir_u2u32(b, color);
   for (unsigned i = 0; i < bits->num_components; i++) {
      packed = nir_ior(b, packed, nir_ishl(b, nir_channel(b, color, i), offset));
      offset = nir_iadd(b, offset, nir_channel(b, bits, i));
   }
   return packed;
}

nir_def *
nir_format_pack_uint(nir_builder *b, nir_def *color,
                     const unsigned *bits, unsigned num_components)
{
   return nir_format_pack_uint_unmasked(b, nir_format_mask_uvec(b, color, bits),
                                        bits, num_components);
}

nir_def *
nir_format_bitcast_uvec_unmasked(nir_builder *b, nir_def *src,
                                 unsigned src_bits, unsigned dst_bits)
{
   assert(src->bit_size >= src_bits && src->bit_size >= dst_bits);
   assert(src_bits == 8 || src_bits == 16 || src_bits == 32);
   assert(dst_bits == 8 || dst_bits == 16 || dst_bits == 32);

   if (src_bits == dst_bits)
      return src;

   const unsigned dst_components =
      DIV_ROUND_UP(src->num_components * src_bits, dst_bits);
   assert(dst_components <= 4);

   nir_def *dst_chan[4] = { 0 };
   if (dst_bits > src_bits) {
      unsigned shift = 0;
      unsigned dst_idx = 0;
      for (unsigned i = 0; i < src->num_components; i++) {
         nir_def *shifted = nir_ishl_imm(b, nir_channel(b, src, i),
                                         shift);
         if (shift == 0) {
            dst_chan[dst_idx] = shifted;
         } else {
            dst_chan[dst_idx] = nir_ior(b, dst_chan[dst_idx], shifted);
         }

         shift += src_bits;
         if (shift >= dst_bits) {
            dst_idx++;
            shift = 0;
         }
      }
   } else {
      unsigned mask = ~0u >> (32 - dst_bits);

      unsigned src_idx = 0;
      unsigned shift = 0;
      for (unsigned i = 0; i < dst_components; i++) {
         dst_chan[i] = nir_iand_imm(b,
                                    nir_ushr_imm(b,
                                                 nir_channel(b, src, src_idx),
                                                 shift),
                                    mask);
         shift += dst_bits;
         if (shift >= src_bits) {
            src_idx++;
            shift = 0;
         }
      }
   }

   return nir_vec(b, dst_chan, dst_components);
}

static nir_def *
_nir_format_norm_factor(nir_builder *b, const unsigned *bits,
                        unsigned num_components,
                        unsigned bit_size,
                        bool is_signed)
{
   nir_const_value factor[NIR_MAX_VEC_COMPONENTS];
   memset(factor, 0, sizeof(factor));
   for (unsigned i = 0; i < num_components; i++) {
      /* A 16-bit float only has 23 bits of mantissa.  This isn't enough to
       * convert 24 or 32-bit UNORM/SNORM accurately.  For that, we would need
       * fp64 or some sort of fixed-point math.
       *
       * Unfortunately, GL is silly and includes 32-bit normalized vertex
       * formats even though you're guaranteed to lose precision. Those formats
       * are broken by design, but we do need to support them with the
       * bugginess, and the loss of precision here is acceptable for GL. This
       * helper is used for the vertex format conversion on Asahi, so we can't
       * assert(bits[i] <= 16). But if it's not, you get to pick up the pieces.
       */
      switch (bit_size) {
      case 32:
         factor[i].f32 = (1ull << (bits[i] - is_signed)) - 1;
         break;
      case 64:
         factor[i].f64 = (1ull << (bits[i] - is_signed)) - 1;
         break;
      default:
         unreachable("invalid bit size");
         break;
      }
   }
   return nir_build_imm(b, num_components, bit_size, factor);
}

nir_def *
nir_format_unorm_to_float(nir_builder *b, nir_def *u, const unsigned *bits)
{
   nir_def *factor =
      _nir_format_norm_factor(b, bits, u->num_components, 32, false);

   return nir_fdiv(b, nir_u2f32(b, u), factor);
}

nir_def *
nir_format_unorm_to_float_precise(nir_builder *b, nir_def *u, const unsigned *bits)
{
   nir_def *factor =
      _nir_format_norm_factor(b, bits, u->num_components, 64, false);

   return nir_f2f32(b, nir_fdiv(b, nir_u2f64(b, u), factor));
}

nir_def *
nir_format_snorm_to_float(nir_builder *b, nir_def *s, const unsigned *bits)
{
   nir_def *factor =
      _nir_format_norm_factor(b, bits, s->num_components, 32, true);

   return nir_fmax(b, nir_fdiv(b, nir_i2f32(b, s), factor),
                   nir_imm_float(b, -1.0f));
}

nir_def *
nir_format_float_to_unorm(nir_builder *b, nir_def *f, const unsigned *bits)
{
   nir_def *factor =
      _nir_format_norm_factor(b, bits, f->num_components, 32, false);

   /* Clamp to the range [0, 1] */
   f = nir_fsat(b, f);

   return nir_f2u32(b, nir_fround_even(b, nir_fmul(b, f, factor)));
}

nir_def *
nir_format_float_to_snorm(nir_builder *b, nir_def *f, const unsigned *bits)
{
   nir_def *factor =
      _nir_format_norm_factor(b, bits, f->num_components, 32, true);

   /* Clamp to the range [-1, 1] */
   f = nir_fmin(b, nir_fmax(b, f, nir_imm_float(b, -1)), nir_imm_float(b, 1));

   return nir_f2i32(b, nir_fround_even(b, nir_fmul(b, f, factor)));
}

static nir_def *
nir_format_float_to_uscaled(nir_builder *b, nir_def *f, const unsigned *bits)
{
   nir_const_value max[NIR_MAX_VEC_COMPONENTS];
   memset(max, 0, sizeof(max));
   for (unsigned i = 0; i < f->num_components; i++) {
      assert(bits[i] <= 32);
      max[i].f32 = u_uintN_max(bits[i]);
   }

   f = nir_fclamp(b, f, nir_imm_float(b, 0),
                  nir_build_imm(b, f->num_components, 32, max));

   return nir_f2u32(b, nir_fround_even(b, f));
}

static nir_def *
nir_format_float_to_sscaled(nir_builder *b, nir_def *f, const unsigned *bits)
{
   nir_const_value min[NIR_MAX_VEC_COMPONENTS], max[NIR_MAX_VEC_COMPONENTS];
   memset(min, 0, sizeof(min));
   memset(max, 0, sizeof(max));
   for (unsigned i = 0; i < f->num_components; i++) {
      assert(bits[i] <= 32);
      max[i].f32 = u_intN_max(bits[i]);
      min[i].f32 = u_intN_min(bits[i]);
   }

   f = nir_fclamp(b, f, nir_build_imm(b, f->num_components, 32, min),
                  nir_build_imm(b, f->num_components, 32, max));

   return nir_f2i32(b, nir_fround_even(b, f));
}

/* Converts a vector of floats to a vector of half-floats packed in the low 16
 * bits.
 */
nir_def *
nir_format_float_to_half(nir_builder *b, nir_def *f)
{
   nir_def *zero = nir_imm_float(b, 0);
   nir_def *f16comps[4];
   for (unsigned i = 0; i < f->num_components; i++)
      f16comps[i] = nir_pack_half_2x16_split(b, nir_channel(b, f, i), zero);
   return nir_vec(b, f16comps, f->num_components);
}

nir_def *
nir_format_linear_to_srgb(nir_builder *b, nir_def *c)
{
   nir_def *linear = nir_fmul_imm(b, c, 12.92f);
   nir_def *curved =
      nir_fadd_imm(b, nir_fmul_imm(b, nir_fpow_imm(b, c, 1.0 / 2.4), 1.055f),
                   -0.055f);

   return nir_fsat(b, nir_bcsel(b, nir_flt_imm(b, c, 0.0031308f),
                                linear, curved));
}

nir_def *
nir_format_srgb_to_linear(nir_builder *b, nir_def *c)
{
   nir_def *linear = nir_fdiv_imm(b, c, 12.92f);
   nir_def *curved =
      nir_fpow(b, nir_fmul_imm(b, nir_fadd_imm(b, c, 0.055f), 1.0 / 1.055f),
               nir_imm_float(b, 2.4f));

   return nir_fsat(b, nir_bcsel(b, nir_fle_imm(b, c, 0.04045f),
                                linear, curved));
}

/* Clamps a vector of uints so they don't extend beyond the given number of
 * bits per channel.
 */
nir_def *
nir_format_clamp_uint(nir_builder *b, nir_def *f, const unsigned *bits)
{
   if (bits[0] == 32)
      return f;

   nir_const_value max[NIR_MAX_VEC_COMPONENTS];
   memset(max, 0, sizeof(max));
   for (unsigned i = 0; i < f->num_components; i++) {
      assert(bits[i] < 32 && bits[i] <= f->bit_size);
      max[i].u32 = u_uintN_max(bits[i]);
   }
   return nir_umin(b, f, nir_u2uN(b, nir_build_imm(b, f->num_components, 32, max), f->bit_size));
}

/* Clamps a vector of sints so they don't extend beyond the given number of
 * bits per channel.
 */
nir_def *
nir_format_clamp_sint(nir_builder *b, nir_def *f, const unsigned *bits)
{
   if (bits[0] == 32)
      return f;

   nir_const_value min[NIR_MAX_VEC_COMPONENTS], max[NIR_MAX_VEC_COMPONENTS];
   memset(min, 0, sizeof(min));
   memset(max, 0, sizeof(max));
   for (unsigned i = 0; i < f->num_components; i++) {
      assert(bits[i] < 32 && bits[i] <= f->bit_size);
      max[i].i32 = u_intN_max(bits[i]);
      min[i].i32 = u_intN_min(bits[i]);
   }
   f = nir_imin(b, f, nir_i2iN(b, nir_build_imm(b, f->num_components, 32, max), f->bit_size));
   f = nir_imax(b, f, nir_i2iN(b, nir_build_imm(b, f->num_components, 32, min), f->bit_size));

   return f;
}

nir_def *
nir_format_unpack_11f11f10f(nir_builder *b, nir_def *packed)
{
   nir_def *chans[3];
   chans[0] = nir_mask_shift(b, packed, 0x000007ff, 4);
   chans[1] = nir_mask_shift(b, packed, 0x003ff800, -7);
   chans[2] = nir_mask_shift(b, packed, 0xffc00000, -17);

   for (unsigned i = 0; i < 3; i++)
      chans[i] = nir_unpack_half_2x16_split_x(b, chans[i]);

   return nir_vec(b, chans, 3);
}

nir_def *
nir_format_pack_11f11f10f(nir_builder *b, nir_def *color)
{
   /* 10 and 11-bit floats are unsigned.  Clamp to non-negative */
   nir_def *clamped = nir_fmax(b, color, nir_imm_float(b, 0));

   nir_def *undef = nir_undef(b, 1, color->bit_size);
   nir_def *p1 = nir_pack_half_2x16_split(b, nir_channel(b, clamped, 0),
                                          nir_channel(b, clamped, 1));
   nir_def *p2 = nir_pack_half_2x16_split(b, nir_channel(b, clamped, 2),
                                          undef);

   /* A 10 or 11-bit float has the same exponent as a 16-bit float but with
    * fewer mantissa bits and no sign bit.  All we have to do is throw away
    * the sign bit and the bottom mantissa bits and shift it into place.
    */
   nir_def *packed = nir_imm_int(b, 0);
   packed = nir_mask_shift_or(b, packed, p1, 0x00007ff0, -4);
   packed = nir_mask_shift_or(b, packed, p1, 0x7ff00000, -9);
   packed = nir_mask_shift_or(b, packed, p2, 0x00007fe0, 17);

   return packed;
}

nir_def *
nir_format_unpack_r9g9b9e5(nir_builder *b, nir_def *packed)
{
   nir_def *rgb = nir_vec3(b, nir_ubitfield_extract_imm(b, packed, 0, 9),
                           nir_ubitfield_extract_imm(b, packed, 9, 9),
                           nir_ubitfield_extract_imm(b, packed, 18, 9));

   /* exponent = (rgb >> 27) - RGB9E5_EXP_BIAS - RGB9E5_MANTISSA_BITS;
    * scale.u = (exponent + 127) << 23;
    */
   nir_def *exp = nir_ubitfield_extract_imm(b, packed, 27, 5);
   exp = nir_iadd_imm(b, exp, 127 - RGB9E5_EXP_BIAS - RGB9E5_MANTISSA_BITS);
   nir_def *scale = nir_ishl_imm(b, exp, 23);

   return nir_fmul(b, rgb, scale);
}

nir_def *
nir_format_pack_r9g9b9e5(nir_builder *b, nir_def *color)
{
   /* See also float3_to_rgb9e5 */

   /* First, we need to clamp it to range. The fmax(color, 0) will also flush
    * NaN to 0.  We set exact to ensure that nothing optimizes this behavior
    * away from us.
    */
   float exact_save = b->exact;
   b->exact = true;
   nir_def *clamped =
      nir_fmin(b, nir_fmax(b, color, nir_imm_float(b, 0)),
               nir_imm_float(b, MAX_RGB9E5));
   b->exact = exact_save;

   /* maxrgb.u = MAX3(rc.u, gc.u, bc.u); */
   nir_def *maxu = nir_umax(b, nir_channel(b, clamped, 0),
                            nir_umax(b, nir_channel(b, clamped, 1),
                                     nir_channel(b, clamped, 2)));

   /* maxrgb.u += maxrgb.u & (1 << (23-9)); */
   maxu = nir_iadd(b, maxu, nir_iand_imm(b, maxu, 1 << 14));

   /* exp_shared = MAX2((maxrgb.u >> 23), -RGB9E5_EXP_BIAS - 1 + 127) +
    *              1 + RGB9E5_EXP_BIAS - 127;
    */
   nir_def *exp_shared =
      nir_iadd_imm(b, nir_umax(b, nir_ushr_imm(b, maxu, 23), nir_imm_int(b, -RGB9E5_EXP_BIAS - 1 + 127)),
                   1 + RGB9E5_EXP_BIAS - 127);

   /* revdenom_biasedexp = 127 - (exp_shared - RGB9E5_EXP_BIAS -
    *                             RGB9E5_MANTISSA_BITS) + 1;
    */
   nir_def *revdenom_biasedexp =
      nir_isub_imm(b, 127 + RGB9E5_EXP_BIAS + RGB9E5_MANTISSA_BITS + 1,
                   exp_shared);

   /* revdenom.u = revdenom_biasedexp << 23; */
   nir_def *revdenom =
      nir_ishl_imm(b, revdenom_biasedexp, 23);

   /* rm = (int) (rc.f * revdenom.f);
    * gm = (int) (gc.f * revdenom.f);
    * bm = (int) (bc.f * revdenom.f);
    */
   nir_def *mantissa =
      nir_f2i32(b, nir_fmul(b, clamped, revdenom));

   /* rm = (rm & 1) + (rm >> 1);
    * gm = (gm & 1) + (gm >> 1);
    * bm = (bm & 1) + (bm >> 1);
    */
   mantissa = nir_iadd(b, nir_iand_imm(b, mantissa, 1),
                       nir_ushr_imm(b, mantissa, 1));

   nir_def *packed = nir_channel(b, mantissa, 0);
   packed = nir_mask_shift_or(b, packed, nir_channel(b, mantissa, 1), ~0, 9);
   packed = nir_mask_shift_or(b, packed, nir_channel(b, mantissa, 2), ~0, 18);
   packed = nir_mask_shift_or(b, packed, exp_shared, ~0, 27);

   return packed;
}

nir_def *
nir_format_unpack_rgba(nir_builder *b, nir_def *packed,
                       enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R9G9B9E5_FLOAT: {
      nir_def *rgb = nir_format_unpack_r9g9b9e5(b, packed);
      return nir_vec4(b, nir_channel(b, rgb, 0),
                      nir_channel(b, rgb, 1),
                      nir_channel(b, rgb, 2),
                      nir_imm_float(b, 1.0));
   }

   case PIPE_FORMAT_R11G11B10_FLOAT: {
      nir_def *rgb = nir_format_unpack_11f11f10f(b, packed);
      return nir_vec4(b, nir_channel(b, rgb, 0),
                      nir_channel(b, rgb, 1),
                      nir_channel(b, rgb, 2),
                      nir_imm_float(b, 1.0));
   }

   default:
      /* Handled below */
      break;
   }

   const struct util_format_description *desc = util_format_description(format);
   assert(desc->layout == UTIL_FORMAT_LAYOUT_PLAIN);

   nir_def *unpacked;
   if (desc->block.bits <= 32) {
      unsigned bits[4] = {
         0,
      };
      for (uint32_t c = 0; c < desc->nr_channels; c++) {
         if (c != 0) {
            assert(desc->channel[c].shift ==
                   desc->channel[c - 1].shift + desc->channel[c - 1].size);
         }
         bits[c] = desc->channel[c].size;
      }
      unpacked = nir_format_unpack_uint(b, packed, bits, desc->nr_channels);
   } else {
      unsigned bits = desc->channel[0].size;
      for (uint32_t c = 1; c < desc->nr_channels; c++)
         assert(desc->channel[c].size == bits);
      unpacked = nir_format_bitcast_uvec_unmasked(b, packed, 32, bits);

      /* 3-channel formats can unpack extra components */
      unpacked = nir_trim_vector(b, unpacked, desc->nr_channels);
   }

   nir_def *comps[4] = {
      NULL,
   };
   for (uint32_t c = 0; c < desc->nr_channels; c++) {
      const struct util_format_channel_description *chan = &desc->channel[c];

      nir_def *raw = nir_channel(b, unpacked, c);

      /* Most of the helpers work on an array of bits */
      unsigned bits[1] = { chan->size };

      switch (chan->type) {
      case UTIL_FORMAT_TYPE_VOID:
         comps[c] = nir_imm_int(b, 0);
         break;

      case UTIL_FORMAT_TYPE_UNSIGNED:
         if (chan->normalized) {
            comps[c] = nir_format_unorm_to_float(b, raw, bits);
         } else if (chan->pure_integer) {
            comps[c] = nir_u2u32(b, raw);
         } else {
            comps[c] = nir_u2f32(b, raw);
         }
         break;

      case UTIL_FORMAT_TYPE_SIGNED:
         raw = nir_format_sign_extend_ivec(b, raw, bits);
         if (chan->normalized) {
            comps[c] = nir_format_snorm_to_float(b, raw, bits);
         } else if (chan->pure_integer) {
            comps[c] = nir_i2i32(b, raw);
         } else {
            comps[c] = nir_i2f32(b, raw);
         }
         break;

      case UTIL_FORMAT_TYPE_FIXED:
         unreachable("Fixed formats not supported");

      case UTIL_FORMAT_TYPE_FLOAT:
         switch (chan->size) {
         case 16:
            comps[c] = nir_unpack_half_2x16_split_x(b, raw);
            break;

         case 32:
            comps[c] = raw;
            break;

         default:
            unreachable("Unknown number of float bits");
         }
         break;

      default:
         unreachable("Unknown format channel type");
      }
   }

   nir_def *swiz_comps[4] = {
      NULL,
   };
   for (uint32_t i = 0; i < 4; i++) {
      enum pipe_swizzle s = desc->swizzle[i];
      switch (s) {
      case PIPE_SWIZZLE_X:
      case PIPE_SWIZZLE_Y:
      case PIPE_SWIZZLE_Z:
      case PIPE_SWIZZLE_W:
         swiz_comps[i] = comps[s - PIPE_SWIZZLE_X];
         break;

      case PIPE_SWIZZLE_0:
      case PIPE_SWIZZLE_NONE:
         swiz_comps[i] = nir_imm_int(b, 0);
         break;

      case PIPE_SWIZZLE_1:
         if (util_format_is_pure_integer(format))
            swiz_comps[i] = nir_imm_int(b, 1);
         else
            swiz_comps[i] = nir_imm_float(b, 1.0);
         break;

      default:
         unreachable("Unknown swizzle");
      }
   }
   nir_def *rgba = nir_vec(b, swiz_comps, 4);

   assert(desc->colorspace == UTIL_FORMAT_COLORSPACE_RGB ||
          desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);
   if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB) {
      nir_def *linear = nir_format_srgb_to_linear(b, rgba);
      if (rgba->num_components == 4)
         linear = nir_vector_insert_imm(b, linear, nir_channel(b, rgba, 3), 3);
      rgba = linear;
   }

   return rgba;
}

nir_def *
nir_format_pack_rgba(nir_builder *b, enum pipe_format format, nir_def *rgba)
{
   assert(rgba->num_components <= 4);

   switch (format) {
   case PIPE_FORMAT_R9G9B9E5_FLOAT:
      return nir_format_pack_r9g9b9e5(b, rgba);

   case PIPE_FORMAT_R11G11B10_FLOAT:
      return nir_format_pack_11f11f10f(b, rgba);

   default:
      /* Handled below */
      break;
   }

   const struct util_format_description *desc = util_format_description(format);
   assert(desc->layout == UTIL_FORMAT_LAYOUT_PLAIN);

   assert(desc->colorspace == UTIL_FORMAT_COLORSPACE_RGB ||
          desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);
   if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB) {
      nir_def *srgb = nir_format_linear_to_srgb(b, rgba);
      if (rgba->num_components == 4)
         srgb = nir_vector_insert_imm(b, srgb, nir_channel(b, rgba, 3), 3);
      rgba = srgb;
   }

   nir_def *comps[4] = {
      NULL,
   };
   for (uint32_t i = 0; i < 4; i++) {
      enum pipe_swizzle s = desc->swizzle[i];
      if (s < PIPE_SWIZZLE_X || s > PIPE_SWIZZLE_W)
         continue;

      /* This is backwards from what you might think because we're packing and
       * the swizzles are in terms of unpacking.
       */
      comps[s - PIPE_SWIZZLE_X] = nir_channel(b, rgba, i);
   }

   for (uint32_t c = 0; c < desc->nr_channels; c++) {
      const struct util_format_channel_description *chan = &desc->channel[c];
      if (comps[c] == NULL) {
         comps[c] = nir_imm_int(b, 0);
         continue;
      }

      /* Most of the helpers work on an array of bits */
      assert(comps[c]->num_components == 1);
      unsigned bits[1] = { chan->size };

      switch (chan->type) {
      case UTIL_FORMAT_TYPE_VOID:
         comps[c] = nir_imm_int(b, 0);
         break;

      case UTIL_FORMAT_TYPE_UNSIGNED:
         if (chan->normalized) {
            comps[c] = nir_format_float_to_unorm(b, comps[c], bits);
         } else if (chan->pure_integer) {
            comps[c] = nir_format_clamp_uint(b, comps[c], bits);
         } else {
            comps[c] = nir_format_float_to_uscaled(b, comps[c], bits);
         }
         break;

      case UTIL_FORMAT_TYPE_SIGNED:
         if (chan->normalized) {
            comps[c] = nir_format_float_to_snorm(b, comps[c], bits);
         } else if (chan->pure_integer) {
            comps[c] = nir_format_clamp_sint(b, comps[c], bits);
         } else {
            comps[c] = nir_format_float_to_sscaled(b, comps[c], bits);
         }
         /* We don't want sign bits ending up in other channels */
         comps[c] = nir_format_mask_uvec(b, comps[c], bits);
         break;

      case UTIL_FORMAT_TYPE_FIXED:
         unreachable("Fixed formats not supported");

      case UTIL_FORMAT_TYPE_FLOAT:
         switch (chan->size) {
         case 16:
            comps[c] = nir_format_float_to_half(b, comps[c]);
            break;

         case 32:
            /* Nothing to do */
            break;

         default:
            unreachable("Unknown number of float bits");
         }
         break;

      default:
         unreachable("Unknown format channel type");
      }
   }
   nir_def *encoded = nir_vec(b, comps, desc->nr_channels);

   if (desc->block.bits <= 32) {
      unsigned bits[4] = {
         0,
      };
      for (uint32_t c = 0; c < desc->nr_channels; c++) {
         if (c != 0) {
            assert(desc->channel[c].shift ==
                   desc->channel[c - 1].shift + desc->channel[c - 1].size);
         }
         bits[c] = desc->channel[c].size;
      }
      return nir_format_pack_uint_unmasked(b, encoded, bits, desc->nr_channels);
   } else {
      unsigned bits = desc->channel[0].size;
      for (uint32_t c = 1; c < desc->nr_channels; c++)
         assert(desc->channel[c].size == bits);
      return nir_format_bitcast_uvec_unmasked(b, encoded, bits, 32);
   }
}
