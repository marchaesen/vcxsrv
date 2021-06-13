/*
 * Copyright (C) 2019 Alyssa Rosenzweig
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

/**
 * @file
 *
 * Implements the fragment pipeline (blending and writeout) in software, to be
 * run as a dedicated "blend shader" stage on Midgard/Bifrost, or as a fragment
 * shader variant on typical GPUs. This pass is useful if hardware lacks
 * fixed-function blending in part or in full.
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_format_convert.h"
#include "nir_lower_blend.h"

/* Given processed factors, combine them per a blend function */

static nir_ssa_def *
nir_blend_func(
   nir_builder *b,
   enum blend_func func,
   nir_ssa_def *src, nir_ssa_def *dst)
{
   switch (func) {
   case BLEND_FUNC_ADD:
      return nir_fadd(b, src, dst);
   case BLEND_FUNC_SUBTRACT:
      return nir_fsub(b, src, dst);
   case BLEND_FUNC_REVERSE_SUBTRACT:
      return nir_fsub(b, dst, src);
   case BLEND_FUNC_MIN:
      return nir_fmin(b, src, dst);
   case BLEND_FUNC_MAX:
      return nir_fmax(b, src, dst);
   }

   unreachable("Invalid blend function");
}

/* Does this blend function multiply by a blend factor? */

static bool
nir_blend_factored(enum blend_func func)
{
   switch (func) {
   case BLEND_FUNC_ADD:
   case BLEND_FUNC_SUBTRACT:
   case BLEND_FUNC_REVERSE_SUBTRACT:
      return true;
   default:
      return false;
   }
}

/* Compute a src_alpha_saturate factor */
static nir_ssa_def *
nir_alpha_saturate(
   nir_builder *b,
   nir_ssa_def *src, nir_ssa_def *dst,
   unsigned chan,
   bool half)
{
   nir_ssa_def *Asrc = nir_channel(b, src, 3);
   nir_ssa_def *Adst = nir_channel(b, dst, 3);
   nir_ssa_def *one = half ? nir_imm_float16(b, 1.0) : nir_imm_float(b, 1.0);
   nir_ssa_def *Adsti = nir_fsub(b, one, Adst);

   return (chan < 3) ? nir_fmin(b, Asrc, Adsti) : one;
}

/* Returns a scalar single factor, unmultiplied */

static nir_ssa_def *
nir_blend_factor_value(
   nir_builder *b,
   nir_ssa_def *src, nir_ssa_def *src1, nir_ssa_def *dst, nir_ssa_def *bconst,
   unsigned chan,
   enum blend_factor factor,
   bool half)
{
   switch (factor) {
   case BLEND_FACTOR_ZERO:
      return half ? nir_imm_float16(b, 0.0) : nir_imm_float(b, 0.0);
   case BLEND_FACTOR_SRC_COLOR:
      return nir_channel(b, src, chan);
   case BLEND_FACTOR_SRC1_COLOR:
      return nir_channel(b, src1, chan);
   case BLEND_FACTOR_DST_COLOR:
      return nir_channel(b, dst, chan);
   case BLEND_FACTOR_SRC_ALPHA:
      return nir_channel(b, src, 3);
   case BLEND_FACTOR_SRC1_ALPHA:
      return nir_channel(b, src1, 3);
   case BLEND_FACTOR_DST_ALPHA:
      return nir_channel(b, dst, 3);
   case BLEND_FACTOR_CONSTANT_COLOR:
      return nir_channel(b, bconst, chan);
   case BLEND_FACTOR_CONSTANT_ALPHA:
      return nir_channel(b, bconst, 3);
   case BLEND_FACTOR_SRC_ALPHA_SATURATE:
      return nir_alpha_saturate(b, src, dst, chan, half);
   }

   unreachable("Invalid blend factor");
}

static nir_ssa_def *
nir_blend_factor(
   nir_builder *b,
   nir_ssa_def *raw_scalar,
   nir_ssa_def *src, nir_ssa_def *src1, nir_ssa_def *dst, nir_ssa_def *bconst,
   unsigned chan,
   enum blend_factor factor,
   bool inverted,
   bool half)
{
   nir_ssa_def *f =
      nir_blend_factor_value(b, src, src1, dst, bconst, chan, factor, half);

   nir_ssa_def *unity = half ? nir_imm_float16(b, 1.0) : nir_imm_float(b, 1.0);

   if (inverted)
      f = nir_fsub(b, unity, f);

   return nir_fmul(b, raw_scalar, f);
}

/* Given a colormask, "blend" with the destination */

static nir_ssa_def *
nir_color_mask(
   nir_builder *b,
   unsigned mask,
   nir_ssa_def *src,
   nir_ssa_def *dst)
{
   nir_ssa_def *masked[4];

   for (unsigned c = 0; c < 4; ++c) {
      bool enab = (mask & (1 << c));
      masked[c] = enab ? nir_channel(b, src, c) : nir_channel(b, dst, c);
   }

   return nir_vec(b, masked, 4);
}

static nir_ssa_def *
nir_logicop_func(
   nir_builder *b,
   unsigned func,
   nir_ssa_def *src, nir_ssa_def *dst)
{
   switch (func) {
   case PIPE_LOGICOP_CLEAR:
      return nir_imm_ivec4(b, 0, 0, 0, 0);
   case PIPE_LOGICOP_NOR:
      return nir_inot(b, nir_ior(b, src, dst));
   case PIPE_LOGICOP_AND_INVERTED:
      return nir_iand(b, nir_inot(b, src), dst);
   case PIPE_LOGICOP_COPY_INVERTED:
      return nir_inot(b, src);
   case PIPE_LOGICOP_AND_REVERSE:
      return nir_iand(b, src, nir_inot(b, dst));
   case PIPE_LOGICOP_INVERT:
      return nir_inot(b, dst);
   case PIPE_LOGICOP_XOR:
      return nir_ixor(b, src, dst);
   case PIPE_LOGICOP_NAND:
      return nir_inot(b, nir_iand(b, src, dst));
   case PIPE_LOGICOP_AND:
      return nir_iand(b, src, dst);
   case PIPE_LOGICOP_EQUIV:
      return nir_inot(b, nir_ixor(b, src, dst));
   case PIPE_LOGICOP_NOOP:
      return dst;
   case PIPE_LOGICOP_OR_INVERTED:
      return nir_ior(b, nir_inot(b, src), dst);
   case PIPE_LOGICOP_COPY:
      return src;
   case PIPE_LOGICOP_OR_REVERSE:
      return nir_ior(b, src, nir_inot(b, dst));
   case PIPE_LOGICOP_OR:
      return nir_ior(b, src, dst);
   case PIPE_LOGICOP_SET:
      return nir_imm_ivec4(b, ~0, ~0, ~0, ~0);
   }

   unreachable("Invalid logciop function");
}

static nir_ssa_def *
nir_blend_logicop(
   nir_builder *b,
   nir_lower_blend_options options,
   nir_ssa_def *src, nir_ssa_def *dst)
{
   const struct util_format_description *format_desc =
      util_format_description(options.format);

   if (options.half) {
      src = nir_f2f32(b, src);
      dst = nir_f2f32(b, dst);
   }

   assert(src->num_components <= 4);
   assert(dst->num_components <= 4);

   unsigned bits[4];
   for (int i = 0; i < 4; ++i)
       bits[i] = format_desc->channel[i].size;

   src = nir_format_float_to_unorm(b, src, bits);
   dst = nir_format_float_to_unorm(b, dst, bits);

   nir_ssa_def *out = nir_logicop_func(b, options.logicop_func, src, dst);

   if (bits[0] < 32) {
       nir_const_value mask[4];
       for (int i = 0; i < 4; ++i)
           mask[i] = nir_const_value_for_int((1u << bits[i]) - 1, 32);

       out = nir_iand(b, out, nir_build_imm(b, 4, 32, mask));
   }

   out = nir_format_unorm_to_float(b, out, bits);

   if (options.half)
      out = nir_f2f16(b, out);

   return out;
}

/* Given a blend state, the source color, and the destination color,
 * return the blended color
 */

static nir_ssa_def *
nir_blend(
   nir_builder *b,
   nir_lower_blend_options options,
   nir_ssa_def *src, nir_ssa_def *src1, nir_ssa_def *dst)
{
   if (options.logicop_enable)
      return nir_blend_logicop(b, options, src, dst);

   /* Grab the blend constant ahead of time */
   nir_ssa_def *bconst;
   if (options.is_bifrost) {
      /* Bifrost is a scalar architecture, so let's split loads now to avoid a
       * lowering pass.
       */
      bconst = nir_vec4(b,
                        nir_load_blend_const_color_r_float(b),
                        nir_load_blend_const_color_g_float(b),
                        nir_load_blend_const_color_b_float(b),
                        nir_load_blend_const_color_a_float(b));
   } else {
      bconst = nir_load_blend_const_color_rgba(b);
   }

   if (options.half)
      bconst = nir_f2f16(b, bconst);

   /* We blend per channel and recombine later */
   nir_ssa_def *channels[4];

   for (unsigned c = 0; c < 4; ++c) {
      /* Decide properties based on channel */
      nir_lower_blend_channel chan =
         (c < 3) ? options.rgb : options.alpha;

      nir_ssa_def *psrc = nir_channel(b, src, c);
      nir_ssa_def *pdst = nir_channel(b, dst, c);

      if (nir_blend_factored(chan.func)) {
         psrc = nir_blend_factor(
                   b, psrc,
                   src, src1, dst, bconst, c,
                   chan.src_factor, chan.invert_src_factor, options.half);

         pdst = nir_blend_factor(
                   b, pdst,
                   src, src1, dst, bconst, c,
                   chan.dst_factor, chan.invert_dst_factor, options.half);
      }

      channels[c] = nir_blend_func(b, chan.func, psrc, pdst);
   }

   /* Then just recombine with an applied colormask */
   nir_ssa_def *blended = nir_vec(b, channels, 4);
   return nir_color_mask(b, options.colormask, blended, dst);
}

static bool
nir_is_blend_channel_replace(nir_lower_blend_channel chan)
{
   return
      (chan.src_factor == BLEND_FACTOR_ZERO) &&
      (chan.dst_factor == BLEND_FACTOR_ZERO) &&
      (chan.invert_src_factor && !chan.invert_dst_factor) &&
      (chan.func == BLEND_FUNC_ADD || chan.func == BLEND_FUNC_SUBTRACT || chan.func == BLEND_FUNC_MAX);
}

static bool
nir_is_blend_replace(nir_lower_blend_options options)
{
   return
      nir_is_blend_channel_replace(options.rgb) &&
      nir_is_blend_channel_replace(options.alpha);
}

void
nir_lower_blend(nir_shader *shader, nir_lower_blend_options options)
{
   /* Blend shaders are represented as special fragment shaders */
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   /* Special case replace, since there's nothing to do and we don't want to
    * degrade intermediate precision (e.g. for non-blendable R32F targets) */
   if (nir_is_blend_replace(options))
      return;

   nir_foreach_function(func, shader) {
      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_store_deref)
               continue;

            /* TODO: Extending to MRT */
            nir_variable *var = nir_intrinsic_get_var(intr, 0);
            if (var->data.location != FRAG_RESULT_COLOR)
               continue;

            nir_builder b;
            nir_builder_init(&b, func->impl);
            b.cursor = nir_before_instr(instr);

            /* Grab the input color */
            nir_ssa_def *src = nir_ssa_for_src(&b, intr->src[1], 4);

            /* Grab the dual-source input color */
            nir_ssa_def *src1 = options.src1;

            /* Grab the tilebuffer color - io lowered to load_output */
            nir_ssa_def *dst = nir_load_var(&b, var);

            /* Blend the two colors per the passed options */
            nir_ssa_def *blended = nir_blend(&b, options, src, src1, dst);

            /* Write out the final color instead of the input */
            nir_instr_rewrite_src(instr, &intr->src[1],
                                  nir_src_for_ssa(blended));

         }
      }

      nir_metadata_preserve(func->impl, nir_metadata_block_index |
                            nir_metadata_dominance);
   }
}
