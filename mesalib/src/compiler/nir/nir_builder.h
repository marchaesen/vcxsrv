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

#include "nir_control_flow.h"

struct exec_list;

typedef struct nir_builder {
   nir_cursor cursor;

   nir_shader *shader;
   nir_function_impl *impl;
} nir_builder;

static inline void
nir_builder_init(nir_builder *build, nir_function_impl *impl)
{
   memset(build, 0, sizeof(*build));
   build->impl = impl;
   build->shader = impl->function->shader;
}

static inline void
nir_builder_init_simple_shader(nir_builder *build, void *mem_ctx,
                               gl_shader_stage stage,
                               const nir_shader_compiler_options *options)
{
   build->shader = nir_shader_create(mem_ctx, stage, options);
   nir_function *func = nir_function_create(build->shader, "main");
   build->impl = nir_function_impl_create(func);
   build->cursor = nir_after_cf_list(&build->impl->body);
}

static inline void
nir_builder_instr_insert(nir_builder *build, nir_instr *instr)
{
   nir_instr_insert(build->cursor, instr);

   /* Move the cursor forward. */
   build->cursor = nir_after_instr(instr);
}

static inline void
nir_builder_cf_insert(nir_builder *build, nir_cf_node *cf)
{
   nir_cf_node_insert(build->cursor, cf);
}

static inline nir_ssa_def *
nir_build_imm(nir_builder *build, unsigned num_components, nir_const_value value)
{
   nir_load_const_instr *load_const =
      nir_load_const_instr_create(build->shader, num_components);
   if (!load_const)
      return NULL;

   load_const->value = value;

   nir_builder_instr_insert(build, &load_const->instr);

   return &load_const->def;
}

static inline nir_ssa_def *
nir_imm_float(nir_builder *build, float x)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   v.f[0] = x;

   return nir_build_imm(build, 1, v);
}

static inline nir_ssa_def *
nir_imm_vec4(nir_builder *build, float x, float y, float z, float w)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   v.f[0] = x;
   v.f[1] = y;
   v.f[2] = z;
   v.f[3] = w;

   return nir_build_imm(build, 4, v);
}

static inline nir_ssa_def *
nir_imm_int(nir_builder *build, int x)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   v.i[0] = x;

   return nir_build_imm(build, 1, v);
}

static inline nir_ssa_def *
nir_imm_ivec4(nir_builder *build, int x, int y, int z, int w)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   v.i[0] = x;
   v.i[1] = y;
   v.i[2] = z;
   v.i[3] = w;

   return nir_build_imm(build, 4, v);
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

   nir_builder_instr_insert(build, &instr->instr);

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

static inline nir_ssa_def *
nir_vec(nir_builder *build, nir_ssa_def **comp, unsigned num_components)
{
   switch (num_components) {
   case 4:
      return nir_vec4(build, comp[0], comp[1], comp[2], comp[3]);
   case 3:
      return nir_vec3(build, comp[0], comp[1], comp[2]);
   case 2:
      return nir_vec2(build, comp[0], comp[1]);
   case 1:
      return comp[0];
   default:
      unreachable("bad component count");
      return NULL;
   }
}

/**
 * Similar to nir_fmov, but takes a nir_alu_src instead of a nir_ssa_def.
 */
static inline nir_ssa_def *
nir_fmov_alu(nir_builder *build, nir_alu_src src, unsigned num_components)
{
   nir_alu_instr *mov = nir_alu_instr_create(build->shader, nir_op_fmov);
   nir_ssa_dest_init(&mov->instr, &mov->dest.dest, num_components, NULL);
   mov->dest.write_mask = (1 << num_components) - 1;
   mov->src[0] = src;
   nir_builder_instr_insert(build, &mov->instr);

   return &mov->dest.dest.ssa;
}

static inline nir_ssa_def *
nir_imov_alu(nir_builder *build, nir_alu_src src, unsigned num_components)
{
   nir_alu_instr *mov = nir_alu_instr_create(build->shader, nir_op_imov);
   nir_ssa_dest_init(&mov->instr, &mov->dest.dest, num_components, NULL);
   mov->dest.write_mask = (1 << num_components) - 1;
   mov->src[0] = src;
   nir_builder_instr_insert(build, &mov->instr);

   return &mov->dest.dest.ssa;
}

/**
 * Construct an fmov or imov that reswizzles the source's components.
 */
static inline nir_ssa_def *
nir_swizzle(nir_builder *build, nir_ssa_def *src, unsigned swiz[4],
            unsigned num_components, bool use_fmov)
{
   nir_alu_src alu_src = { NIR_SRC_INIT };
   alu_src.src = nir_src_for_ssa(src);
   for (unsigned i = 0; i < num_components; i++)
      alu_src.swizzle[i] = swiz[i];

   return use_fmov ? nir_fmov_alu(build, alu_src, num_components) :
                     nir_imov_alu(build, alu_src, num_components);
}

static inline nir_ssa_def *
nir_channel(nir_builder *b, nir_ssa_def *def, unsigned c)
{
   unsigned swizzle[4] = {c, c, c, c};
   return nir_swizzle(b, def, swizzle, 1, false);
}

/**
 * Turns a nir_src into a nir_ssa_def * so it can be passed to
 * nir_build_alu()-based builder calls.
 *
 * See nir_ssa_for_alu_src() for alu instructions.
 */
static inline nir_ssa_def *
nir_ssa_for_src(nir_builder *build, nir_src src, int num_components)
{
   if (src.is_ssa && src.ssa->num_components == num_components)
      return src.ssa;

   nir_alu_src alu = { NIR_SRC_INIT };
   alu.src = src;
   for (int j = 0; j < 4; j++)
      alu.swizzle[j] = j;

   return nir_imov_alu(build, alu, num_components);
}

/**
 * Similar to nir_ssa_for_src(), but for alu src's, respecting the
 * nir_alu_src's swizzle.
 */
static inline nir_ssa_def *
nir_ssa_for_alu_src(nir_builder *build, nir_alu_instr *instr, unsigned srcn)
{
   static uint8_t trivial_swizzle[4] = { 0, 1, 2, 3 };
   nir_alu_src *src = &instr->src[srcn];
   unsigned num_components = nir_ssa_alu_instr_src_components(instr, srcn);

   if (src->src.is_ssa && (src->src.ssa->num_components == num_components) &&
       !src->abs && !src->negate &&
       (memcmp(src->swizzle, trivial_swizzle, num_components) == 0))
      return src->src.ssa;

   return nir_imov_alu(build, *src, num_components);
}

static inline nir_ssa_def *
nir_load_var(nir_builder *build, nir_variable *var)
{
   const unsigned num_components = glsl_get_vector_elements(var->type);

   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_load_var);
   load->num_components = num_components;
   load->variables[0] = nir_deref_var_create(load, var);
   nir_ssa_dest_init(&load->instr, &load->dest, num_components, NULL);
   nir_builder_instr_insert(build, &load->instr);
   return &load->dest.ssa;
}

static inline void
nir_store_var(nir_builder *build, nir_variable *var, nir_ssa_def *value,
              unsigned writemask)
{
   const unsigned num_components = glsl_get_vector_elements(var->type);

   nir_intrinsic_instr *store =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_store_var);
   store->num_components = num_components;
   nir_intrinsic_set_write_mask(store, writemask);
   store->variables[0] = nir_deref_var_create(store, var);
   store->src[0] = nir_src_for_ssa(value);
   nir_builder_instr_insert(build, &store->instr);
}

static inline nir_ssa_def *
nir_load_system_value(nir_builder *build, nir_intrinsic_op op, int index)
{
   nir_intrinsic_instr *load = nir_intrinsic_instr_create(build->shader, op);
   load->num_components = nir_intrinsic_infos[op].dest_components;
   load->const_index[0] = index;
   nir_ssa_dest_init(&load->instr, &load->dest,
                     nir_intrinsic_infos[op].dest_components, NULL);
   nir_builder_instr_insert(build, &load->instr);
   return &load->dest.ssa;
}

#endif /* NIR_BUILDER_H */
