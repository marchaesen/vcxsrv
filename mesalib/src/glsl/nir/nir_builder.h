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

#ifndef NIR_BUILDER_H
#define NIR_BUILDER_H

struct exec_list;

typedef struct nir_builder {
   struct exec_list *cf_node_list;
   nir_shader *shader;
   nir_function_impl *impl;
} nir_builder;

static inline void
nir_builder_init(nir_builder *build, nir_function_impl *impl)
{
   memset(build, 0, sizeof(*build));
   build->impl = impl;
   build->shader = impl->overload->function->shader;
}

static inline void
nir_builder_insert_after_cf_list(nir_builder *build,
                                 struct exec_list *cf_node_list)
{
   build->cf_node_list = cf_node_list;
}


static inline nir_ssa_def *
nir_build_alu(nir_builder *build, nir_op op, nir_ssa_def *src0,
              nir_ssa_def *src1, nir_ssa_def *src2, nir_ssa_def *src3)
{
   const nir_op_info *op_info = &nir_op_infos[op];
   nir_alu_instr *instr = nir_alu_instr_create(build->shader, op);
   if (!instr)
      return NULL;

   instr->src[0].src = nir_src_for_ssa(src0);
   if (src1)
      instr->src[1].src = nir_src_for_ssa(src1);
   if (src2)
      instr->src[2].src = nir_src_for_ssa(src2);
   if (src3)
      instr->src[3].src = nir_src_for_ssa(src3);

   /* Guess the number of components the destination temporary should have
    * based on our input sizes, if it's not fixed for the op.
    */
   unsigned num_components = op_info->output_size;
   if (num_components == 0) {
      for (unsigned i = 0; i < op_info->num_inputs; i++) {
         if (op_info->input_sizes[i] == 0)
            num_components = MAX2(num_components,
                                  instr->src[i].src.ssa->num_components);
      }
   }
   assert(num_components != 0);

   /* Make sure we don't swizzle from outside of our source vector (like if a
    * scalar value was passed into a multiply with a vector).
    */
   for (unsigned i = 0; i < op_info->num_inputs; i++) {
      for (unsigned j = instr->src[i].src.ssa->num_components; j < 4; j++) {
         instr->src[i].swizzle[j] = instr->src[i].src.ssa->num_components - 1;
      }
   }

   nir_ssa_dest_init(&instr->instr, &instr->dest.dest, num_components, NULL);
   instr->dest.write_mask = (1 << num_components) - 1;

   nir_instr_insert_after_cf_list(build->cf_node_list, &instr->instr);

   return &instr->dest.dest.ssa;
}

#define ALU1(op)                                                          \
static inline nir_ssa_def *                                               \
nir_##op(nir_builder *build, nir_ssa_def *src0)                           \
{                                                                         \
   return nir_build_alu(build, nir_op_##op, src0, NULL, NULL, NULL);      \
}

#define ALU2(op)                                                          \
static inline nir_ssa_def *                                               \
nir_##op(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)        \
{                                                                         \
   return nir_build_alu(build, nir_op_##op, src0, src1, NULL, NULL);      \
}

#define ALU3(op)                                                          \
static inline nir_ssa_def *                                               \
nir_##op(nir_builder *build, nir_ssa_def *src0,                           \
         nir_ssa_def *src1, nir_ssa_def *src2)                            \
{                                                                         \
   return nir_build_alu(build, nir_op_##op, src0, src1, src2, NULL);      \
}

#define ALU4(op)                                                          \
static inline nir_ssa_def *                                               \
nir_##op(nir_builder *build, nir_ssa_def *src0,                           \
         nir_ssa_def *src1, nir_ssa_def *src2, nir_ssa_def *src3)         \
{                                                                         \
   return nir_build_alu(build, nir_op_##op, src0, src1, src2, src3);      \
}

#include "nir_builder_opcodes.h"

#endif /* NIR_BUILDER_H */
