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

/** @file nir_lower_alu_to_scalar.c
 *
 * Replaces nir_alu_instr operations with more than one channel used in the
 * arguments with individual per-channel operations.
 */

static void
nir_alu_ssa_dest_init(nir_alu_instr *instr, unsigned num_components)
{
   nir_ssa_dest_init(&instr->instr, &instr->dest.dest, num_components, NULL);
   instr->dest.write_mask = (1 << num_components) - 1;
}

static void
lower_reduction(nir_alu_instr *instr, nir_op chan_op, nir_op merge_op,
                void *mem_ctx)
{
   unsigned num_components = nir_op_infos[instr->op].input_sizes[0];

   nir_ssa_def *last = NULL;
   for (unsigned i = 0; i < num_components; i++) {
      nir_alu_instr *chan = nir_alu_instr_create(mem_ctx, chan_op);
      nir_alu_ssa_dest_init(chan, 1);
      nir_alu_src_copy(&chan->src[0], &instr->src[0], mem_ctx);
      chan->src[0].swizzle[0] = chan->src[0].swizzle[i];
      if (nir_op_infos[chan_op].num_inputs > 1) {
         assert(nir_op_infos[chan_op].num_inputs == 2);
         nir_alu_src_copy(&chan->src[1], &instr->src[1], mem_ctx);
         chan->src[1].swizzle[0] = chan->src[1].swizzle[i];
      }

      nir_instr_insert_before(&instr->instr, &chan->instr);

      if (i == 0) {
         last = &chan->dest.dest.ssa;
      } else {
         nir_alu_instr *merge = nir_alu_instr_create(mem_ctx, merge_op);
         nir_alu_ssa_dest_init(merge, 1);
         merge->dest.write_mask = 1;
         merge->src[0].src = nir_src_for_ssa(last);
         merge->src[1].src = nir_src_for_ssa(&chan->dest.dest.ssa);
         nir_instr_insert_before(&instr->instr, &merge->instr);
         last = &merge->dest.dest.ssa;
      }
   }

   assert(instr->dest.write_mask == 1);
   nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa, nir_src_for_ssa(last),
                            mem_ctx);
   nir_instr_remove(&instr->instr);
}

static void
lower_alu_instr_scalar(nir_alu_instr *instr, void *mem_ctx)
{
   unsigned num_src = nir_op_infos[instr->op].num_inputs;
   unsigned i, chan;

   assert(instr->dest.dest.is_ssa);
   assert(instr->dest.write_mask != 0);

#define LOWER_REDUCTION(name, chan, merge) \
   case name##2: \
   case name##3: \
   case name##4: \
      lower_reduction(instr, chan, merge, mem_ctx); \
      break;

   switch (instr->op) {
   case nir_op_vec4:
   case nir_op_vec3:
   case nir_op_vec2:
      /* We don't need to scalarize these ops, they're the ones generated to
       * group up outputs into a value that can be SSAed.
       */
      return;

      LOWER_REDUCTION(nir_op_fdot, nir_op_fmul, nir_op_fadd);
      LOWER_REDUCTION(nir_op_ball_fequal, nir_op_feq, nir_op_iand);
      LOWER_REDUCTION(nir_op_ball_iequal, nir_op_ieq, nir_op_iand);
      LOWER_REDUCTION(nir_op_bany_fnequal, nir_op_fne, nir_op_ior);
      LOWER_REDUCTION(nir_op_bany_inequal, nir_op_ine, nir_op_ior);
      LOWER_REDUCTION(nir_op_fall_equal, nir_op_seq, nir_op_fand);
      LOWER_REDUCTION(nir_op_fany_nequal, nir_op_sne, nir_op_for);
      LOWER_REDUCTION(nir_op_ball, nir_op_imov, nir_op_iand);
      LOWER_REDUCTION(nir_op_bany, nir_op_imov, nir_op_ior);
      LOWER_REDUCTION(nir_op_fall, nir_op_fmov, nir_op_fand);
      LOWER_REDUCTION(nir_op_fany, nir_op_fmov, nir_op_for);

   default:
      break;
   }

   if (instr->dest.dest.ssa.num_components == 1)
      return;

   unsigned num_components = instr->dest.dest.ssa.num_components;
   static const nir_op nir_op_map[] = {nir_op_vec2, nir_op_vec3, nir_op_vec4};
   nir_alu_instr *vec_instr =
      nir_alu_instr_create(mem_ctx, nir_op_map[num_components - 2]);
   nir_alu_ssa_dest_init(vec_instr, num_components);

   for (chan = 0; chan < 4; chan++) {
      if (!(instr->dest.write_mask & (1 << chan)))
         continue;

      nir_alu_instr *lower = nir_alu_instr_create(mem_ctx, instr->op);
      for (i = 0; i < num_src; i++) {
         /* We only handle same-size-as-dest (input_sizes[] == 0) or scalar
          * args (input_sizes[] == 1).
          */
         assert(nir_op_infos[instr->op].input_sizes[i] < 2);
         unsigned src_chan = (nir_op_infos[instr->op].input_sizes[i] == 1 ?
                              0 : chan);

         nir_alu_src_copy(&lower->src[i], &instr->src[i], mem_ctx);
         for (int j = 0; j < 4; j++)
            lower->src[i].swizzle[j] = instr->src[i].swizzle[src_chan];
      }

      nir_alu_ssa_dest_init(lower, 1);
      lower->dest.saturate = instr->dest.saturate;
      vec_instr->src[chan].src = nir_src_for_ssa(&lower->dest.dest.ssa);

      nir_instr_insert_before(&instr->instr, &lower->instr);
   }

   nir_instr_insert_before(&instr->instr, &vec_instr->instr);

   nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa,
                            nir_src_for_ssa(&vec_instr->dest.dest.ssa),
                            mem_ctx);

   nir_instr_remove(&instr->instr);
}

static bool
lower_alu_to_scalar_block(nir_block *block, void *data)
{
   nir_foreach_instr_safe(block, instr) {
      if (instr->type == nir_instr_type_alu)
         lower_alu_instr_scalar((nir_alu_instr *)instr, data);
   }

   return true;
}

static void
nir_lower_alu_to_scalar_impl(nir_function_impl *impl)
{
   nir_foreach_block(impl, lower_alu_to_scalar_block, ralloc_parent(impl));
}

void
nir_lower_alu_to_scalar(nir_shader *shader)
{
   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         nir_lower_alu_to_scalar_impl(overload->impl);
   }
}
