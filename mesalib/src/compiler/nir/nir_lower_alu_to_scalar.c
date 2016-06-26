/*
 * Copyright © 2014-2015 Broadcom
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

#include "nir.h"
#include "nir_builder.h"

/** @file nir_lower_alu_to_scalar.c
 *
 * Replaces nir_alu_instr operations with more than one channel used in the
 * arguments with individual per-channel operations.
 */

static void
nir_alu_ssa_dest_init(nir_alu_instr *instr, unsigned num_components,
                      unsigned bit_size)
{
   nir_ssa_dest_init(&instr->instr, &instr->dest.dest, num_components,
                     bit_size, NULL);
   instr->dest.write_mask = (1 << num_components) - 1;
}

static void
lower_reduction(nir_alu_instr *instr, nir_op chan_op, nir_op merge_op,
                nir_builder *builder)
{
   unsigned num_components = nir_op_infos[instr->op].input_sizes[0];

   nir_ssa_def *last = NULL;
   for (unsigned i = 0; i < num_components; i++) {
      nir_alu_instr *chan = nir_alu_instr_create(builder->shader, chan_op);
      nir_alu_ssa_dest_init(chan, 1, instr->dest.dest.ssa.bit_size);
      nir_alu_src_copy(&chan->src[0], &instr->src[0], chan);
      chan->src[0].swizzle[0] = chan->src[0].swizzle[i];
      if (nir_op_infos[chan_op].num_inputs > 1) {
         assert(nir_op_infos[chan_op].num_inputs == 2);
         nir_alu_src_copy(&chan->src[1], &instr->src[1], chan);
         chan->src[1].swizzle[0] = chan->src[1].swizzle[i];
      }
      chan->exact = instr->exact;

      nir_builder_instr_insert(builder, &chan->instr);

      if (i == 0) {
         last = &chan->dest.dest.ssa;
      } else {
         last = nir_build_alu(builder, merge_op,
                              last, &chan->dest.dest.ssa, NULL, NULL);
      }
   }

   assert(instr->dest.write_mask == 1);
   nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa, nir_src_for_ssa(last));
   nir_instr_remove(&instr->instr);
}

static void
lower_alu_instr_scalar(nir_alu_instr *instr, nir_builder *b)
{
   unsigned num_src = nir_op_infos[instr->op].num_inputs;
   unsigned i, chan;

   assert(instr->dest.dest.is_ssa);
   assert(instr->dest.write_mask != 0);

   b->cursor = nir_before_instr(&instr->instr);
   b->exact = instr->exact;

#define LOWER_REDUCTION(name, chan, merge) \
   case name##2: \
   case name##3: \
   case name##4: \
      lower_reduction(instr, chan, merge, b); \
      return;

   switch (instr->op) {
   case nir_op_vec4:
   case nir_op_vec3:
   case nir_op_vec2:
      /* We don't need to scalarize these ops, they're the ones generated to
       * group up outputs into a value that can be SSAed.
       */
      return;

   case nir_op_pack_half_2x16:
      if (!b->shader->options->lower_pack_half_2x16)
         return;

      nir_ssa_def *val =
         nir_pack_half_2x16_split(b, nir_channel(b, instr->src[0].src.ssa,
                                                 instr->src[0].swizzle[0]),
                                     nir_channel(b, instr->src[0].src.ssa,
                                                 instr->src[0].swizzle[1]));

      nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa, nir_src_for_ssa(val));
      nir_instr_remove(&instr->instr);
      return;

   case nir_op_unpack_unorm_4x8:
   case nir_op_unpack_snorm_4x8:
   case nir_op_unpack_unorm_2x16:
   case nir_op_unpack_snorm_2x16:
      /* There is no scalar version of these ops, unless we were to break it
       * down to bitshifts and math (which is definitely not intended).
       */
      return;

   case nir_op_unpack_half_2x16: {
      if (!b->shader->options->lower_unpack_half_2x16)
         return;

      nir_ssa_def *comps[2];
      comps[0] = nir_unpack_half_2x16_split_x(b, instr->src[0].src.ssa);
      comps[1] = nir_unpack_half_2x16_split_y(b, instr->src[0].src.ssa);
      nir_ssa_def *vec = nir_vec(b, comps, 2);

      nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa, nir_src_for_ssa(vec));
      nir_instr_remove(&instr->instr);
      return;
   }

   case nir_op_pack_uvec2_to_uint: {
      assert(b->shader->options->lower_pack_snorm_2x16 ||
             b->shader->options->lower_pack_unorm_2x16);

      nir_ssa_def *word =
         nir_extract_u16(b, instr->src[0].src.ssa, nir_imm_int(b, 0));
      nir_ssa_def *val =
         nir_ior(b, nir_ishl(b, nir_channel(b, word, 1), nir_imm_int(b, 16)),
                                nir_channel(b, word, 0));

      nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa, nir_src_for_ssa(val));
      nir_instr_remove(&instr->instr);
      break;
   }

   case nir_op_pack_uvec4_to_uint: {
      assert(b->shader->options->lower_pack_snorm_4x8 ||
             b->shader->options->lower_pack_unorm_4x8);

      nir_ssa_def *byte =
         nir_extract_u8(b, instr->src[0].src.ssa, nir_imm_int(b, 0));
      nir_ssa_def *val =
         nir_ior(b, nir_ior(b, nir_ishl(b, nir_channel(b, byte, 3), nir_imm_int(b, 24)),
                               nir_ishl(b, nir_channel(b, byte, 2), nir_imm_int(b, 16))),
                    nir_ior(b, nir_ishl(b, nir_channel(b, byte, 1), nir_imm_int(b, 8)),
                               nir_channel(b, byte, 0)));

      nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa, nir_src_for_ssa(val));
      nir_instr_remove(&instr->instr);
      break;
   }

   case nir_op_fdph: {
      nir_ssa_def *sum[4];
      for (unsigned i = 0; i < 3; i++) {
         sum[i] = nir_fmul(b, nir_channel(b, instr->src[0].src.ssa,
                                          instr->src[0].swizzle[i]),
                              nir_channel(b, instr->src[1].src.ssa,
                                          instr->src[1].swizzle[i]));
      }
      sum[3] = nir_channel(b, instr->src[1].src.ssa, instr->src[1].swizzle[3]);

      nir_ssa_def *val = nir_fadd(b, nir_fadd(b, sum[0], sum[1]),
                                     nir_fadd(b, sum[2], sum[3]));

      nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa, nir_src_for_ssa(val));
      nir_instr_remove(&instr->instr);
      return;
   }

   case nir_op_unpack_double_2x32:
      return;

      LOWER_REDUCTION(nir_op_fdot, nir_op_fmul, nir_op_fadd);
      LOWER_REDUCTION(nir_op_ball_fequal, nir_op_feq, nir_op_iand);
      LOWER_REDUCTION(nir_op_ball_iequal, nir_op_ieq, nir_op_iand);
      LOWER_REDUCTION(nir_op_bany_fnequal, nir_op_fne, nir_op_ior);
      LOWER_REDUCTION(nir_op_bany_inequal, nir_op_ine, nir_op_ior);
      LOWER_REDUCTION(nir_op_fall_equal, nir_op_seq, nir_op_fand);
      LOWER_REDUCTION(nir_op_fany_nequal, nir_op_sne, nir_op_for);

   default:
      break;
   }

   if (instr->dest.dest.ssa.num_components == 1)
      return;

   unsigned num_components = instr->dest.dest.ssa.num_components;
   nir_ssa_def *comps[] = { NULL, NULL, NULL, NULL };

   for (chan = 0; chan < 4; chan++) {
      if (!(instr->dest.write_mask & (1 << chan)))
         continue;

      nir_alu_instr *lower = nir_alu_instr_create(b->shader, instr->op);
      for (i = 0; i < num_src; i++) {
         /* We only handle same-size-as-dest (input_sizes[] == 0) or scalar
          * args (input_sizes[] == 1).
          */
         assert(nir_op_infos[instr->op].input_sizes[i] < 2);
         unsigned src_chan = (nir_op_infos[instr->op].input_sizes[i] == 1 ?
                              0 : chan);

         nir_alu_src_copy(&lower->src[i], &instr->src[i], lower);
         for (int j = 0; j < 4; j++)
            lower->src[i].swizzle[j] = instr->src[i].swizzle[src_chan];
      }

      nir_alu_ssa_dest_init(lower, 1, instr->dest.dest.ssa.bit_size);
      lower->dest.saturate = instr->dest.saturate;
      comps[chan] = &lower->dest.dest.ssa;
      lower->exact = instr->exact;

      nir_builder_instr_insert(b, &lower->instr);
   }

   nir_ssa_def *vec = nir_vec(b, comps, num_components);

   nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa, nir_src_for_ssa(vec));

   nir_instr_remove(&instr->instr);
}

static void
nir_lower_alu_to_scalar_impl(nir_function_impl *impl)
{
   nir_builder builder;
   nir_builder_init(&builder, impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type == nir_instr_type_alu)
            lower_alu_instr_scalar(nir_instr_as_alu(instr), &builder);
      }
   }
}

void
nir_lower_alu_to_scalar(nir_shader *shader)
{
   nir_foreach_function(function, shader) {
      if (function->impl)
         nir_lower_alu_to_scalar_impl(function->impl);
   }
}
