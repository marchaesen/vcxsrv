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
#include "util/bitscan.h"
#include "util/half_float.h"

struct exec_list;

typedef struct nir_builder {
   nir_cursor cursor;

   /* Whether new ALU instructions will be marked "exact" */
   bool exact;

   /* Whether to run divergence analysis on inserted instructions (loop merge
    * and header phis are not updated). */
   bool update_divergence;

   nir_shader *shader;
   nir_function_impl *impl;
} nir_builder;

static inline void
nir_builder_init(nir_builder *build, nir_function_impl *impl)
{
   memset(build, 0, sizeof(*build));
   build->exact = false;
   build->impl = impl;
   build->shader = impl->function->shader;
}

static inline void
nir_builder_init_simple_shader(nir_builder *build, void *mem_ctx,
                               gl_shader_stage stage,
                               const nir_shader_compiler_options *options)
{
   memset(build, 0, sizeof(*build));
   build->shader = nir_shader_create(mem_ctx, stage, options, NULL);
   nir_function *func = nir_function_create(build->shader, "main");
   func->is_entrypoint = true;
   build->exact = false;
   build->impl = nir_function_impl_create(func);
   build->cursor = nir_after_cf_list(&build->impl->body);
}

typedef bool (*nir_instr_pass_cb)(struct nir_builder *, nir_instr *, void *);

/**
 * Iterates over all the instructions in a NIR shader and calls the given pass
 * on them.
 *
 * The pass should return true if it modified the shader.  In that case, only
 * the preserved metadata flags will be preserved in the function impl.
 *
 * The builder will be initialized to point at the function impl, but its
 * cursor is unset.
 */
static inline bool
nir_shader_instructions_pass(nir_shader *shader,
                             nir_instr_pass_cb pass,
                             nir_metadata preserved,
                             void *cb_data)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_builder b;
      nir_builder_init(&b, function->impl);

      nir_foreach_block_safe(block, function->impl) {
         nir_foreach_instr_safe(instr, block) {
            progress |= pass(&b, instr, cb_data);
         }
      }

      if (progress) {
         nir_metadata_preserve(function->impl, preserved);
      } else {
         nir_metadata_preserve(function->impl, nir_metadata_all);
      }
   }

   return progress;
}

static inline void
nir_builder_instr_insert(nir_builder *build, nir_instr *instr)
{
   nir_instr_insert(build->cursor, instr);

   if (build->update_divergence)
      nir_update_instr_divergence(build->shader, instr);

   /* Move the cursor forward. */
   build->cursor = nir_after_instr(instr);
}

static inline nir_instr *
nir_builder_last_instr(nir_builder *build)
{
   assert(build->cursor.option == nir_cursor_after_instr);
   return build->cursor.instr;
}

static inline void
nir_builder_cf_insert(nir_builder *build, nir_cf_node *cf)
{
   nir_cf_node_insert(build->cursor, cf);
}

static inline bool
nir_builder_is_inside_cf(nir_builder *build, nir_cf_node *cf_node)
{
   nir_block *block = nir_cursor_current_block(build->cursor);
   for (nir_cf_node *n = &block->cf_node; n; n = n->parent) {
      if (n == cf_node)
         return true;
   }
   return false;
}

static inline nir_if *
nir_push_if_src(nir_builder *build, nir_src condition)
{
   nir_if *nif = nir_if_create(build->shader);
   nif->condition = condition;
   nir_builder_cf_insert(build, &nif->cf_node);
   build->cursor = nir_before_cf_list(&nif->then_list);
   return nif;
}

static inline nir_if *
nir_push_if(nir_builder *build, nir_ssa_def *condition)
{
   return nir_push_if_src(build, nir_src_for_ssa(condition));
}

static inline nir_if *
nir_push_else(nir_builder *build, nir_if *nif)
{
   if (nif) {
      assert(nir_builder_is_inside_cf(build, &nif->cf_node));
   } else {
      nir_block *block = nir_cursor_current_block(build->cursor);
      nif = nir_cf_node_as_if(block->cf_node.parent);
   }
   build->cursor = nir_before_cf_list(&nif->else_list);
   return nif;
}

static inline void
nir_pop_if(nir_builder *build, nir_if *nif)
{
   if (nif) {
      assert(nir_builder_is_inside_cf(build, &nif->cf_node));
   } else {
      nir_block *block = nir_cursor_current_block(build->cursor);
      nif = nir_cf_node_as_if(block->cf_node.parent);
   }
   build->cursor = nir_after_cf_node(&nif->cf_node);
}

static inline nir_ssa_def *
nir_if_phi(nir_builder *build, nir_ssa_def *then_def, nir_ssa_def *else_def)
{
   nir_block *block = nir_cursor_current_block(build->cursor);
   nir_if *nif = nir_cf_node_as_if(nir_cf_node_prev(&block->cf_node));

   nir_phi_instr *phi = nir_phi_instr_create(build->shader);

   nir_phi_src *src = ralloc(phi, nir_phi_src);
   src->pred = nir_if_last_then_block(nif);
   src->src = nir_src_for_ssa(then_def);
   exec_list_push_tail(&phi->srcs, &src->node);

   src = ralloc(phi, nir_phi_src);
   src->pred = nir_if_last_else_block(nif);
   src->src = nir_src_for_ssa(else_def);
   exec_list_push_tail(&phi->srcs, &src->node);

   assert(then_def->num_components == else_def->num_components);
   assert(then_def->bit_size == else_def->bit_size);
   nir_ssa_dest_init(&phi->instr, &phi->dest,
                     then_def->num_components, then_def->bit_size, NULL);

   nir_builder_instr_insert(build, &phi->instr);

   return &phi->dest.ssa;
}

static inline nir_loop *
nir_push_loop(nir_builder *build)
{
   nir_loop *loop = nir_loop_create(build->shader);
   nir_builder_cf_insert(build, &loop->cf_node);
   build->cursor = nir_before_cf_list(&loop->body);
   return loop;
}

static inline void
nir_pop_loop(nir_builder *build, nir_loop *loop)
{
   if (loop) {
      assert(nir_builder_is_inside_cf(build, &loop->cf_node));
   } else {
      nir_block *block = nir_cursor_current_block(build->cursor);
      loop = nir_cf_node_as_loop(block->cf_node.parent);
   }
   build->cursor = nir_after_cf_node(&loop->cf_node);
}

static inline nir_ssa_def *
nir_ssa_undef(nir_builder *build, unsigned num_components, unsigned bit_size)
{
   nir_ssa_undef_instr *undef =
      nir_ssa_undef_instr_create(build->shader, num_components, bit_size);
   if (!undef)
      return NULL;

   nir_instr_insert(nir_before_cf_list(&build->impl->body), &undef->instr);
   if (build->update_divergence)
      nir_update_instr_divergence(build->shader, &undef->instr);

   return &undef->def;
}

static inline nir_ssa_def *
nir_build_imm(nir_builder *build, unsigned num_components,
              unsigned bit_size, const nir_const_value *value)
{
   nir_load_const_instr *load_const =
      nir_load_const_instr_create(build->shader, num_components, bit_size);
   if (!load_const)
      return NULL;

   memcpy(load_const->value, value, sizeof(nir_const_value) * num_components);

   nir_builder_instr_insert(build, &load_const->instr);

   return &load_const->def;
}

static inline nir_ssa_def *
nir_imm_zero(nir_builder *build, unsigned num_components, unsigned bit_size)
{
   nir_load_const_instr *load_const =
      nir_load_const_instr_create(build->shader, num_components, bit_size);

   /* nir_load_const_instr_create uses rzalloc so it's already zero */

   nir_builder_instr_insert(build, &load_const->instr);

   return &load_const->def;
}

static inline nir_ssa_def *
nir_imm_boolN_t(nir_builder *build, bool x, unsigned bit_size)
{
   nir_const_value v = nir_const_value_for_bool(x, bit_size);
   return nir_build_imm(build, 1, bit_size, &v);
}

static inline nir_ssa_def *
nir_imm_bool(nir_builder *build, bool x)
{
   return nir_imm_boolN_t(build, x, 1);
}

static inline nir_ssa_def *
nir_imm_true(nir_builder *build)
{
   return nir_imm_bool(build, true);
}

static inline nir_ssa_def *
nir_imm_false(nir_builder *build)
{
   return nir_imm_bool(build, false);
}

static inline nir_ssa_def *
nir_imm_floatN_t(nir_builder *build, double x, unsigned bit_size)
{
   nir_const_value v = nir_const_value_for_float(x, bit_size);
   return nir_build_imm(build, 1, bit_size, &v);
}

static inline nir_ssa_def *
nir_imm_float16(nir_builder *build, float x)
{
   return nir_imm_floatN_t(build, x, 16);
}

static inline nir_ssa_def *
nir_imm_float(nir_builder *build, float x)
{
   return nir_imm_floatN_t(build, x, 32);
}

static inline nir_ssa_def *
nir_imm_double(nir_builder *build, double x)
{
   return nir_imm_floatN_t(build, x, 64);
}

static inline nir_ssa_def *
nir_imm_vec2(nir_builder *build, float x, float y)
{
   nir_const_value v[2] = {
      nir_const_value_for_float(x, 32),
      nir_const_value_for_float(y, 32),
   };
   return nir_build_imm(build, 2, 32, v);
}

static inline nir_ssa_def *
nir_imm_vec4(nir_builder *build, float x, float y, float z, float w)
{
   nir_const_value v[4] = {
      nir_const_value_for_float(x, 32),
      nir_const_value_for_float(y, 32),
      nir_const_value_for_float(z, 32),
      nir_const_value_for_float(w, 32),
   };

   return nir_build_imm(build, 4, 32, v);
}

static inline nir_ssa_def *
nir_imm_vec4_16(nir_builder *build, float x, float y, float z, float w)
{
   nir_const_value v[4] = {
      nir_const_value_for_float(x, 16),
      nir_const_value_for_float(y, 16),
      nir_const_value_for_float(z, 16),
      nir_const_value_for_float(w, 16),
   };

   return nir_build_imm(build, 4, 16, v);
}

static inline nir_ssa_def *
nir_imm_intN_t(nir_builder *build, uint64_t x, unsigned bit_size)
{
   nir_const_value v = nir_const_value_for_raw_uint(x, bit_size);
   return nir_build_imm(build, 1, bit_size, &v);
}

static inline nir_ssa_def *
nir_imm_int(nir_builder *build, int x)
{
   return nir_imm_intN_t(build, x, 32);
}

static inline nir_ssa_def *
nir_imm_int64(nir_builder *build, int64_t x)
{
   return nir_imm_intN_t(build, x, 64);
}

static inline nir_ssa_def *
nir_imm_ivec2(nir_builder *build, int x, int y)
{
   nir_const_value v[2] = {
      nir_const_value_for_int(x, 32),
      nir_const_value_for_int(y, 32),
   };

   return nir_build_imm(build, 2, 32, v);
}

static inline nir_ssa_def *
nir_imm_ivec4(nir_builder *build, int x, int y, int z, int w)
{
   nir_const_value v[4] = {
      nir_const_value_for_int(x, 32),
      nir_const_value_for_int(y, 32),
      nir_const_value_for_int(z, 32),
      nir_const_value_for_int(w, 32),
   };

   return nir_build_imm(build, 4, 32, v);
}

static inline nir_ssa_def *
nir_builder_alu_instr_finish_and_insert(nir_builder *build, nir_alu_instr *instr)
{
   const nir_op_info *op_info = &nir_op_infos[instr->op];

   instr->exact = build->exact;

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

   /* Figure out the bitwidth based on the source bitwidth if the instruction
    * is variable-width.
    */
   unsigned bit_size = nir_alu_type_get_type_size(op_info->output_type);
   if (bit_size == 0) {
      for (unsigned i = 0; i < op_info->num_inputs; i++) {
         unsigned src_bit_size = instr->src[i].src.ssa->bit_size;
         if (nir_alu_type_get_type_size(op_info->input_types[i]) == 0) {
            if (bit_size)
               assert(src_bit_size == bit_size);
            else
               bit_size = src_bit_size;
         } else {
            assert(src_bit_size ==
               nir_alu_type_get_type_size(op_info->input_types[i]));
         }
      }
   }

   /* When in doubt, assume 32. */
   if (bit_size == 0)
      bit_size = 32;

   /* Make sure we don't swizzle from outside of our source vector (like if a
    * scalar value was passed into a multiply with a vector).
    */
   for (unsigned i = 0; i < op_info->num_inputs; i++) {
      for (unsigned j = instr->src[i].src.ssa->num_components;
           j < NIR_MAX_VEC_COMPONENTS; j++) {
         instr->src[i].swizzle[j] = instr->src[i].src.ssa->num_components - 1;
      }
   }

   nir_ssa_dest_init(&instr->instr, &instr->dest.dest, num_components,
                     bit_size, NULL);
   instr->dest.write_mask = (1 << num_components) - 1;

   nir_builder_instr_insert(build, &instr->instr);

   return &instr->dest.dest.ssa;
}

static inline nir_ssa_def *
nir_build_alu(nir_builder *build, nir_op op, nir_ssa_def *src0,
              nir_ssa_def *src1, nir_ssa_def *src2, nir_ssa_def *src3)
{
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

   return nir_builder_alu_instr_finish_and_insert(build, instr);
}

/* for the couple special cases with more than 4 src args: */
static inline nir_ssa_def *
nir_build_alu_src_arr(nir_builder *build, nir_op op, nir_ssa_def **srcs)
{
   const nir_op_info *op_info = &nir_op_infos[op];
   nir_alu_instr *instr = nir_alu_instr_create(build->shader, op);
   if (!instr)
      return NULL;

   for (unsigned i = 0; i < op_info->num_inputs; i++)
      instr->src[i].src = nir_src_for_ssa(srcs[i]);

   return nir_builder_alu_instr_finish_and_insert(build, instr);
}

#include "nir_builder_opcodes.h"

static inline nir_ssa_def *
nir_vec(nir_builder *build, nir_ssa_def **comp, unsigned num_components)
{
   return nir_build_alu_src_arr(build, nir_op_vec(num_components), comp);
}

static inline nir_ssa_def *
nir_mov_alu(nir_builder *build, nir_alu_src src, unsigned num_components)
{
   assert(!src.abs && !src.negate);
   if (src.src.is_ssa && src.src.ssa->num_components == num_components) {
      bool any_swizzles = false;
      for (unsigned i = 0; i < num_components; i++) {
         if (src.swizzle[i] != i)
            any_swizzles = true;
      }
      if (!any_swizzles)
         return src.src.ssa;
   }

   nir_alu_instr *mov = nir_alu_instr_create(build->shader, nir_op_mov);
   nir_ssa_dest_init(&mov->instr, &mov->dest.dest, num_components,
                     nir_src_bit_size(src.src), NULL);
   mov->exact = build->exact;
   mov->dest.write_mask = (1 << num_components) - 1;
   mov->src[0] = src;
   nir_builder_instr_insert(build, &mov->instr);

   return &mov->dest.dest.ssa;
}

/**
 * Construct an fmov or imov that reswizzles the source's components.
 */
static inline nir_ssa_def *
nir_swizzle(nir_builder *build, nir_ssa_def *src, const unsigned *swiz,
            unsigned num_components)
{
   assert(num_components <= NIR_MAX_VEC_COMPONENTS);
   nir_alu_src alu_src = { NIR_SRC_INIT };
   alu_src.src = nir_src_for_ssa(src);

   bool is_identity_swizzle = true;
   for (unsigned i = 0; i < num_components && i < NIR_MAX_VEC_COMPONENTS; i++) {
      if (swiz[i] != i)
         is_identity_swizzle = false;
      alu_src.swizzle[i] = swiz[i];
   }

   if (num_components == src->num_components && is_identity_swizzle)
      return src;

   return nir_mov_alu(build, alu_src, num_components);
}

/* Selects the right fdot given the number of components in each source. */
static inline nir_ssa_def *
nir_fdot(nir_builder *build, nir_ssa_def *src0, nir_ssa_def *src1)
{
   assert(src0->num_components == src1->num_components);
   switch (src0->num_components) {
   case 1: return nir_fmul(build, src0, src1);
   case 2: return nir_fdot2(build, src0, src1);
   case 3: return nir_fdot3(build, src0, src1);
   case 4: return nir_fdot4(build, src0, src1);
   case 8: return nir_fdot8(build, src0, src1);
   case 16: return nir_fdot16(build, src0, src1);
   default:
      unreachable("bad component size");
   }

   return NULL;
}

static inline nir_ssa_def *
nir_ball_iequal(nir_builder *b, nir_ssa_def *src0, nir_ssa_def *src1)
{
   switch (src0->num_components) {
   case 1: return nir_ieq(b, src0, src1);
   case 2: return nir_ball_iequal2(b, src0, src1);
   case 3: return nir_ball_iequal3(b, src0, src1);
   case 4: return nir_ball_iequal4(b, src0, src1);
   case 8: return nir_ball_iequal8(b, src0, src1);
   case 16: return nir_ball_iequal16(b, src0, src1);
   default:
      unreachable("bad component size");
   }
}

static inline nir_ssa_def *
nir_ball(nir_builder *b, nir_ssa_def *src)
{
   return nir_ball_iequal(b, src, nir_imm_true(b));
}

static inline nir_ssa_def *
nir_bany_inequal(nir_builder *b, nir_ssa_def *src0, nir_ssa_def *src1)
{
   switch (src0->num_components) {
   case 1: return nir_ine(b, src0, src1);
   case 2: return nir_bany_inequal2(b, src0, src1);
   case 3: return nir_bany_inequal3(b, src0, src1);
   case 4: return nir_bany_inequal4(b, src0, src1);
   case 8: return nir_bany_inequal8(b, src0, src1);
   case 16: return nir_bany_inequal16(b, src0, src1);
   default:
      unreachable("bad component size");
   }
}

static inline nir_ssa_def *
nir_bany(nir_builder *b, nir_ssa_def *src)
{
   return nir_bany_inequal(b, src, nir_imm_false(b));
}

static inline nir_ssa_def *
nir_channel(nir_builder *b, nir_ssa_def *def, unsigned c)
{
   return nir_swizzle(b, def, &c, 1);
}

static inline nir_ssa_def *
nir_channels(nir_builder *b, nir_ssa_def *def, nir_component_mask_t mask)
{
   unsigned num_channels = 0, swizzle[NIR_MAX_VEC_COMPONENTS] = { 0 };

   for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; i++) {
      if ((mask & (1 << i)) == 0)
         continue;
      swizzle[num_channels++] = i;
   }

   return nir_swizzle(b, def, swizzle, num_channels);
}

static inline nir_ssa_def *
_nir_vector_extract_helper(nir_builder *b, nir_ssa_def *vec, nir_ssa_def *c,
                           unsigned start, unsigned end)
{
   if (start == end - 1) {
      return nir_channel(b, vec, start);
   } else {
      unsigned mid = start + (end - start) / 2;
      return nir_bcsel(b, nir_ilt(b, c, nir_imm_intN_t(b, mid, c->bit_size)),
                       _nir_vector_extract_helper(b, vec, c, start, mid),
                       _nir_vector_extract_helper(b, vec, c, mid, end));
   }
}

static inline nir_ssa_def *
nir_vector_extract(nir_builder *b, nir_ssa_def *vec, nir_ssa_def *c)
{
   nir_src c_src = nir_src_for_ssa(c);
   if (nir_src_is_const(c_src)) {
      uint64_t c_const = nir_src_as_uint(c_src);
      if (c_const < vec->num_components)
         return nir_channel(b, vec, c_const);
      else
         return nir_ssa_undef(b, 1, vec->bit_size);
   } else {
      return _nir_vector_extract_helper(b, vec, c, 0, vec->num_components);
   }
}

/** Replaces the component of `vec` specified by `c` with `scalar` */
static inline nir_ssa_def *
nir_vector_insert_imm(nir_builder *b, nir_ssa_def *vec,
                      nir_ssa_def *scalar, unsigned c)
{
   assert(scalar->num_components == 1);
   assert(c < vec->num_components);

   nir_op vec_op = nir_op_vec(vec->num_components);
   nir_alu_instr *vec_instr = nir_alu_instr_create(b->shader, vec_op);

   for (unsigned i = 0; i < vec->num_components; i++) {
      if (i == c) {
         vec_instr->src[i].src = nir_src_for_ssa(scalar);
         vec_instr->src[i].swizzle[0] = 0;
      } else {
         vec_instr->src[i].src = nir_src_for_ssa(vec);
         vec_instr->src[i].swizzle[0] = i;
      }
   }

   return nir_builder_alu_instr_finish_and_insert(b, vec_instr);
}

/** Replaces the component of `vec` specified by `c` with `scalar` */
static inline nir_ssa_def *
nir_vector_insert(nir_builder *b, nir_ssa_def *vec, nir_ssa_def *scalar,
                  nir_ssa_def *c)
{
   assert(scalar->num_components == 1);
   assert(c->num_components == 1);

   nir_src c_src = nir_src_for_ssa(c);
   if (nir_src_is_const(c_src)) {
      uint64_t c_const = nir_src_as_uint(c_src);
      if (c_const < vec->num_components)
         return nir_vector_insert_imm(b, vec, scalar, c_const);
      else
         return vec;
   } else {
      nir_const_value per_comp_idx_const[NIR_MAX_VEC_COMPONENTS];
      for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; i++)
         per_comp_idx_const[i] = nir_const_value_for_int(i, c->bit_size);
      nir_ssa_def *per_comp_idx =
         nir_build_imm(b, vec->num_components,
                       c->bit_size, per_comp_idx_const);

      /* nir_builder will automatically splat out scalars to vectors so an
       * insert is as simple as "if I'm the channel, replace me with the
       * scalar."
       */
      return nir_bcsel(b, nir_ieq(b, c, per_comp_idx), scalar, vec);
   }
}

static inline nir_ssa_def *
nir_i2i(nir_builder *build, nir_ssa_def *x, unsigned dest_bit_size)
{
   if (x->bit_size == dest_bit_size)
      return x;

   switch (dest_bit_size) {
   case 64: return nir_i2i64(build, x);
   case 32: return nir_i2i32(build, x);
   case 16: return nir_i2i16(build, x);
   case 8:  return nir_i2i8(build, x);
   default: unreachable("Invalid bit size");
   }
}

static inline nir_ssa_def *
nir_u2u(nir_builder *build, nir_ssa_def *x, unsigned dest_bit_size)
{
   if (x->bit_size == dest_bit_size)
      return x;

   switch (dest_bit_size) {
   case 64: return nir_u2u64(build, x);
   case 32: return nir_u2u32(build, x);
   case 16: return nir_u2u16(build, x);
   case 8:  return nir_u2u8(build, x);
   default: unreachable("Invalid bit size");
   }
}

static inline nir_ssa_def *
nir_iadd_imm(nir_builder *build, nir_ssa_def *x, uint64_t y)
{
   assert(x->bit_size <= 64);
   y &= BITFIELD64_MASK(x->bit_size);

   if (y == 0) {
      return x;
   } else {
      return nir_iadd(build, x, nir_imm_intN_t(build, y, x->bit_size));
   }
}

static inline nir_ssa_def *
_nir_mul_imm(nir_builder *build, nir_ssa_def *x, uint64_t y, bool amul)
{
   assert(x->bit_size <= 64);
   y &= BITFIELD64_MASK(x->bit_size);

   if (y == 0) {
      return nir_imm_intN_t(build, 0, x->bit_size);
   } else if (y == 1) {
      return x;
   } else if (!build->shader->options->lower_bitops &&
              util_is_power_of_two_or_zero64(y)) {
      return nir_ishl(build, x, nir_imm_int(build, ffsll(y) - 1));
   } else if (amul) {
      return nir_amul(build, x, nir_imm_intN_t(build, y, x->bit_size));
   } else {
      return nir_imul(build, x, nir_imm_intN_t(build, y, x->bit_size));
   }
}

static inline nir_ssa_def *
nir_imul_imm(nir_builder *build, nir_ssa_def *x, uint64_t y)
{
   return _nir_mul_imm(build, x, y, false);
}

static inline nir_ssa_def *
nir_amul_imm(nir_builder *build, nir_ssa_def *x, uint64_t y)
{
   return _nir_mul_imm(build, x, y, true);
}

static inline nir_ssa_def *
nir_fadd_imm(nir_builder *build, nir_ssa_def *x, double y)
{
   return nir_fadd(build, x, nir_imm_floatN_t(build, y, x->bit_size));
}

static inline nir_ssa_def *
nir_fmul_imm(nir_builder *build, nir_ssa_def *x, double y)
{
   return nir_fmul(build, x, nir_imm_floatN_t(build, y, x->bit_size));
}

static inline nir_ssa_def *
nir_iand_imm(nir_builder *build, nir_ssa_def *x, uint64_t y)
{
   assert(x->bit_size <= 64);
   y &= BITFIELD64_MASK(x->bit_size);

   if (y == 0) {
      return nir_imm_intN_t(build, 0, x->bit_size);
   } else if (y == BITFIELD64_MASK(x->bit_size)) {
      return x;
   } else {
      return nir_iand(build, x, nir_imm_intN_t(build, y, x->bit_size));
   }
}

static inline nir_ssa_def *
nir_ishr_imm(nir_builder *build, nir_ssa_def *x, uint32_t y)
{
   if (y == 0) {
      return x;
   } else {
      return nir_ishr(build, x, nir_imm_int(build, y));
   }
}

static inline nir_ssa_def *
nir_ushr_imm(nir_builder *build, nir_ssa_def *x, uint32_t y)
{
   if (y == 0) {
      return x;
   } else {
      return nir_ushr(build, x, nir_imm_int(build, y));
   }
}

static inline nir_ssa_def *
nir_udiv_imm(nir_builder *build, nir_ssa_def *x, uint64_t y)
{
   assert(x->bit_size <= 64);
   y &= BITFIELD64_MASK(x->bit_size);

   if (y == 1) {
      return x;
   } else if (util_is_power_of_two_nonzero(y)) {
      return nir_ushr_imm(build, x, ffsll(y) - 1);
   } else {
      return nir_udiv(build, x, nir_imm_intN_t(build, y, x->bit_size));
   }
}

static inline nir_ssa_def *
nir_pack_bits(nir_builder *b, nir_ssa_def *src, unsigned dest_bit_size)
{
   assert(src->num_components * src->bit_size == dest_bit_size);

   switch (dest_bit_size) {
   case 64:
      switch (src->bit_size) {
      case 32: return nir_pack_64_2x32(b, src);
      case 16: return nir_pack_64_4x16(b, src);
      default: break;
      }
      break;

   case 32:
      if (src->bit_size == 16)
         return nir_pack_32_2x16(b, src);
      break;

   default:
      break;
   }

   /* If we got here, we have no dedicated unpack opcode. */
   nir_ssa_def *dest = nir_imm_intN_t(b, 0, dest_bit_size);
   for (unsigned i = 0; i < src->num_components; i++) {
      nir_ssa_def *val = nir_u2u(b, nir_channel(b, src, i), dest_bit_size);
      val = nir_ishl(b, val, nir_imm_int(b, i * src->bit_size));
      dest = nir_ior(b, dest, val);
   }
   return dest;
}

static inline nir_ssa_def *
nir_unpack_bits(nir_builder *b, nir_ssa_def *src, unsigned dest_bit_size)
{
   assert(src->num_components == 1);
   assert(src->bit_size > dest_bit_size);
   const unsigned dest_num_components = src->bit_size / dest_bit_size;
   assert(dest_num_components <= NIR_MAX_VEC_COMPONENTS);

   switch (src->bit_size) {
   case 64:
      switch (dest_bit_size) {
      case 32: return nir_unpack_64_2x32(b, src);
      case 16: return nir_unpack_64_4x16(b, src);
      default: break;
      }
      break;

   case 32:
      if (dest_bit_size == 16)
         return nir_unpack_32_2x16(b, src);
      break;

   default:
      break;
   }

   /* If we got here, we have no dedicated unpack opcode. */
   nir_ssa_def *dest_comps[NIR_MAX_VEC_COMPONENTS];
   for (unsigned i = 0; i < dest_num_components; i++) {
      nir_ssa_def *val = nir_ushr_imm(b, src, i * dest_bit_size);
      dest_comps[i] = nir_u2u(b, val, dest_bit_size);
   }
   return nir_vec(b, dest_comps, dest_num_components);
}

/**
 * Treats srcs as if it's one big blob of bits and extracts the range of bits
 * given by
 *
 *       [first_bit, first_bit + dest_num_components * dest_bit_size)
 *
 * The range can have any alignment or size as long as it's an integer number
 * of destination components and fits inside the concatenated sources.
 *
 * TODO: The one caveat here is that we can't handle byte alignment if 64-bit
 * values are involved because that would require pack/unpack to/from a vec8
 * which NIR currently does not support.
 */
static inline nir_ssa_def *
nir_extract_bits(nir_builder *b, nir_ssa_def **srcs, unsigned num_srcs,
                 unsigned first_bit,
                 unsigned dest_num_components, unsigned dest_bit_size)
{
   const unsigned num_bits = dest_num_components * dest_bit_size;

   /* Figure out the common bit size */
   unsigned common_bit_size = dest_bit_size;
   for (unsigned i = 0; i < num_srcs; i++)
      common_bit_size = MIN2(common_bit_size, srcs[i]->bit_size);
   if (first_bit > 0)
      common_bit_size = MIN2(common_bit_size, (1u << (ffs(first_bit) - 1)));

   /* We don't want to have to deal with 1-bit values */
   assert(common_bit_size >= 8);

   nir_ssa_def *common_comps[NIR_MAX_VEC_COMPONENTS * sizeof(uint64_t)];
   assert(num_bits / common_bit_size <= ARRAY_SIZE(common_comps));

   /* First, unpack to the common bit size and select the components from the
    * source.
    */
   int src_idx = -1;
   unsigned src_start_bit = 0;
   unsigned src_end_bit = 0;
   for (unsigned i = 0; i < num_bits / common_bit_size; i++) {
      const unsigned bit = first_bit + (i * common_bit_size);
      while (bit >= src_end_bit) {
         src_idx++;
         assert(src_idx < (int) num_srcs);
         src_start_bit = src_end_bit;
         src_end_bit += srcs[src_idx]->bit_size *
                        srcs[src_idx]->num_components;
      }
      assert(bit >= src_start_bit);
      assert(bit + common_bit_size <= src_end_bit);
      const unsigned rel_bit = bit - src_start_bit;
      const unsigned src_bit_size = srcs[src_idx]->bit_size;

      nir_ssa_def *comp = nir_channel(b, srcs[src_idx],
                                      rel_bit / src_bit_size);
      if (srcs[src_idx]->bit_size > common_bit_size) {
         nir_ssa_def *unpacked = nir_unpack_bits(b, comp, common_bit_size);
         comp = nir_channel(b, unpacked, (rel_bit % src_bit_size) /
                                         common_bit_size);
      }
      common_comps[i] = comp;
   }

   /* Now, re-pack the destination if we have to */
   if (dest_bit_size > common_bit_size) {
      unsigned common_per_dest = dest_bit_size / common_bit_size;
      nir_ssa_def *dest_comps[NIR_MAX_VEC_COMPONENTS];
      for (unsigned i = 0; i < dest_num_components; i++) {
         nir_ssa_def *unpacked = nir_vec(b, common_comps + i * common_per_dest,
                                         common_per_dest);
         dest_comps[i] = nir_pack_bits(b, unpacked, dest_bit_size);
      }
      return nir_vec(b, dest_comps, dest_num_components);
   } else {
      assert(dest_bit_size == common_bit_size);
      return nir_vec(b, common_comps, dest_num_components);
   }
}

static inline nir_ssa_def *
nir_bitcast_vector(nir_builder *b, nir_ssa_def *src, unsigned dest_bit_size)
{
   assert((src->bit_size * src->num_components) % dest_bit_size == 0);
   const unsigned dest_num_components =
      (src->bit_size * src->num_components) / dest_bit_size;
   assert(dest_num_components <= NIR_MAX_VEC_COMPONENTS);

   return nir_extract_bits(b, &src, 1, 0, dest_num_components, dest_bit_size);
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
   for (int j = 0; j < NIR_MAX_VEC_COMPONENTS; j++)
      alu.swizzle[j] = j;

   return nir_mov_alu(build, alu, num_components);
}

/**
 * Similar to nir_ssa_for_src(), but for alu srcs, respecting the
 * nir_alu_src's swizzle.
 */
static inline nir_ssa_def *
nir_ssa_for_alu_src(nir_builder *build, nir_alu_instr *instr, unsigned srcn)
{
   static uint8_t trivial_swizzle[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
   STATIC_ASSERT(ARRAY_SIZE(trivial_swizzle) == NIR_MAX_VEC_COMPONENTS);

   nir_alu_src *src = &instr->src[srcn];
   unsigned num_components = nir_ssa_alu_instr_src_components(instr, srcn);

   if (src->src.is_ssa && (src->src.ssa->num_components == num_components) &&
       !src->abs && !src->negate &&
       (memcmp(src->swizzle, trivial_swizzle, num_components) == 0))
      return src->src.ssa;

   return nir_mov_alu(build, *src, num_components);
}

static inline unsigned
nir_get_ptr_bitsize(nir_shader *shader)
{
   if (shader->info.stage == MESA_SHADER_KERNEL)
      return shader->info.cs.ptr_size;
   return 32;
}

static inline nir_deref_instr *
nir_build_deref_var(nir_builder *build, nir_variable *var)
{
   nir_deref_instr *deref =
      nir_deref_instr_create(build->shader, nir_deref_type_var);

   deref->mode = (nir_variable_mode)var->data.mode;
   deref->type = var->type;
   deref->var = var;

   nir_ssa_dest_init(&deref->instr, &deref->dest, 1,
                     nir_get_ptr_bitsize(build->shader), NULL);

   nir_builder_instr_insert(build, &deref->instr);

   return deref;
}

static inline nir_deref_instr *
nir_build_deref_array(nir_builder *build, nir_deref_instr *parent,
                      nir_ssa_def *index)
{
   assert(glsl_type_is_array(parent->type) ||
          glsl_type_is_matrix(parent->type) ||
          glsl_type_is_vector(parent->type));

   assert(index->bit_size == parent->dest.ssa.bit_size);

   nir_deref_instr *deref =
      nir_deref_instr_create(build->shader, nir_deref_type_array);

   deref->mode = parent->mode;
   deref->type = glsl_get_array_element(parent->type);
   deref->parent = nir_src_for_ssa(&parent->dest.ssa);
   deref->arr.index = nir_src_for_ssa(index);

   nir_ssa_dest_init(&deref->instr, &deref->dest,
                     parent->dest.ssa.num_components,
                     parent->dest.ssa.bit_size, NULL);

   nir_builder_instr_insert(build, &deref->instr);

   return deref;
}

static inline nir_deref_instr *
nir_build_deref_array_imm(nir_builder *build, nir_deref_instr *parent,
                          int64_t index)
{
   assert(parent->dest.is_ssa);
   nir_ssa_def *idx_ssa = nir_imm_intN_t(build, index,
                                         parent->dest.ssa.bit_size);

   return nir_build_deref_array(build, parent, idx_ssa);
}

static inline nir_deref_instr *
nir_build_deref_ptr_as_array(nir_builder *build, nir_deref_instr *parent,
                             nir_ssa_def *index)
{
   assert(parent->deref_type == nir_deref_type_array ||
          parent->deref_type == nir_deref_type_ptr_as_array ||
          parent->deref_type == nir_deref_type_cast);

   assert(index->bit_size == parent->dest.ssa.bit_size);

   nir_deref_instr *deref =
      nir_deref_instr_create(build->shader, nir_deref_type_ptr_as_array);

   deref->mode = parent->mode;
   deref->type = parent->type;
   deref->parent = nir_src_for_ssa(&parent->dest.ssa);
   deref->arr.index = nir_src_for_ssa(index);

   nir_ssa_dest_init(&deref->instr, &deref->dest,
                     parent->dest.ssa.num_components,
                     parent->dest.ssa.bit_size, NULL);

   nir_builder_instr_insert(build, &deref->instr);

   return deref;
}

static inline nir_deref_instr *
nir_build_deref_array_wildcard(nir_builder *build, nir_deref_instr *parent)
{
   assert(glsl_type_is_array(parent->type) ||
          glsl_type_is_matrix(parent->type));

   nir_deref_instr *deref =
      nir_deref_instr_create(build->shader, nir_deref_type_array_wildcard);

   deref->mode = parent->mode;
   deref->type = glsl_get_array_element(parent->type);
   deref->parent = nir_src_for_ssa(&parent->dest.ssa);

   nir_ssa_dest_init(&deref->instr, &deref->dest,
                     parent->dest.ssa.num_components,
                     parent->dest.ssa.bit_size, NULL);

   nir_builder_instr_insert(build, &deref->instr);

   return deref;
}

static inline nir_deref_instr *
nir_build_deref_struct(nir_builder *build, nir_deref_instr *parent,
                       unsigned index)
{
   assert(glsl_type_is_struct_or_ifc(parent->type));

   nir_deref_instr *deref =
      nir_deref_instr_create(build->shader, nir_deref_type_struct);

   deref->mode = parent->mode;
   deref->type = glsl_get_struct_field(parent->type, index);
   deref->parent = nir_src_for_ssa(&parent->dest.ssa);
   deref->strct.index = index;

   nir_ssa_dest_init(&deref->instr, &deref->dest,
                     parent->dest.ssa.num_components,
                     parent->dest.ssa.bit_size, NULL);

   nir_builder_instr_insert(build, &deref->instr);

   return deref;
}

static inline nir_deref_instr *
nir_build_deref_cast(nir_builder *build, nir_ssa_def *parent,
                     nir_variable_mode mode, const struct glsl_type *type,
                     unsigned ptr_stride)
{
   nir_deref_instr *deref =
      nir_deref_instr_create(build->shader, nir_deref_type_cast);

   deref->mode = mode;
   deref->type = type;
   deref->parent = nir_src_for_ssa(parent);
   deref->cast.ptr_stride = ptr_stride;

   nir_ssa_dest_init(&deref->instr, &deref->dest,
                     parent->num_components, parent->bit_size, NULL);

   nir_builder_instr_insert(build, &deref->instr);

   return deref;
}

static inline nir_deref_instr *
nir_alignment_deref_cast(nir_builder *build, nir_deref_instr *parent,
                         uint32_t align_mul, uint32_t align_offset)
{
   nir_deref_instr *deref =
      nir_deref_instr_create(build->shader, nir_deref_type_cast);

   deref->mode = parent->mode;
   deref->type = parent->type;
   deref->parent = nir_src_for_ssa(&parent->dest.ssa);
   deref->cast.ptr_stride = nir_deref_instr_array_stride(deref);
   deref->cast.align_mul = align_mul;
   deref->cast.align_offset = align_offset;

   nir_ssa_dest_init(&deref->instr, &deref->dest,
                     parent->dest.ssa.num_components,
                     parent->dest.ssa.bit_size, NULL);

   nir_builder_instr_insert(build, &deref->instr);

   return deref;
}

/** Returns a deref that follows another but starting from the given parent
 *
 * The new deref will be the same type and take the same array or struct index
 * as the leader deref but it may have a different parent.  This is very
 * useful for walking deref paths.
 */
static inline nir_deref_instr *
nir_build_deref_follower(nir_builder *b, nir_deref_instr *parent,
                         nir_deref_instr *leader)
{
   /* If the derefs would have the same parent, don't make a new one */
   assert(leader->parent.is_ssa);
   if (leader->parent.ssa == &parent->dest.ssa)
      return leader;

   UNUSED nir_deref_instr *leader_parent = nir_src_as_deref(leader->parent);

   switch (leader->deref_type) {
   case nir_deref_type_var:
      unreachable("A var dereference cannot have a parent");
      break;

   case nir_deref_type_array:
   case nir_deref_type_array_wildcard:
      assert(glsl_type_is_matrix(parent->type) ||
             glsl_type_is_array(parent->type) ||
             (leader->deref_type == nir_deref_type_array &&
              glsl_type_is_vector(parent->type)));
      assert(glsl_get_length(parent->type) ==
             glsl_get_length(leader_parent->type));

      if (leader->deref_type == nir_deref_type_array) {
         assert(leader->arr.index.is_ssa);
         nir_ssa_def *index = nir_i2i(b, leader->arr.index.ssa,
                                         parent->dest.ssa.bit_size);
         return nir_build_deref_array(b, parent, index);
      } else {
         return nir_build_deref_array_wildcard(b, parent);
      }

   case nir_deref_type_struct:
      assert(glsl_type_is_struct_or_ifc(parent->type));
      assert(glsl_get_length(parent->type) ==
             glsl_get_length(leader_parent->type));

      return nir_build_deref_struct(b, parent, leader->strct.index);

   default:
      unreachable("Invalid deref instruction type");
   }
}

static inline nir_ssa_def *
nir_load_reg(nir_builder *build, nir_register *reg)
{
   return nir_ssa_for_src(build, nir_src_for_reg(reg), reg->num_components);
}

static inline void
nir_store_reg(nir_builder *build, nir_register *reg,
              nir_ssa_def *def, nir_component_mask_t write_mask)
{
   assert(reg->num_components == def->num_components);
   assert(reg->bit_size == def->bit_size);

   nir_alu_instr *mov = nir_alu_instr_create(build->shader, nir_op_mov);
   mov->src[0].src = nir_src_for_ssa(def);
   mov->dest.dest = nir_dest_for_reg(reg);
   mov->dest.write_mask = write_mask & BITFIELD_MASK(reg->num_components);
   nir_builder_instr_insert(build, &mov->instr);
}

static inline nir_ssa_def *
nir_load_deref_with_access(nir_builder *build, nir_deref_instr *deref,
                           enum gl_access_qualifier access)
{
   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_load_deref);
   load->num_components = glsl_get_vector_elements(deref->type);
   load->src[0] = nir_src_for_ssa(&deref->dest.ssa);
   nir_ssa_dest_init(&load->instr, &load->dest, load->num_components,
                     glsl_get_bit_size(deref->type), NULL);
   nir_intrinsic_set_access(load, access);
   nir_builder_instr_insert(build, &load->instr);
   return &load->dest.ssa;
}

static inline nir_ssa_def *
nir_load_deref(nir_builder *build, nir_deref_instr *deref)
{
   return nir_load_deref_with_access(build, deref, (enum gl_access_qualifier)0);
}

static inline void
nir_store_deref_with_access(nir_builder *build, nir_deref_instr *deref,
                            nir_ssa_def *value, unsigned writemask,
                            enum gl_access_qualifier access)
{
   nir_intrinsic_instr *store =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_store_deref);
   store->num_components = glsl_get_vector_elements(deref->type);
   store->src[0] = nir_src_for_ssa(&deref->dest.ssa);
   store->src[1] = nir_src_for_ssa(value);
   nir_intrinsic_set_write_mask(store,
                                writemask & ((1 << store->num_components) - 1));
   nir_intrinsic_set_access(store, access);
   nir_builder_instr_insert(build, &store->instr);
}

static inline void
nir_store_deref(nir_builder *build, nir_deref_instr *deref,
                nir_ssa_def *value, unsigned writemask)
{
   nir_store_deref_with_access(build, deref, value, writemask,
                               (enum gl_access_qualifier)0);
}

static inline void
nir_copy_deref_with_access(nir_builder *build, nir_deref_instr *dest,
                           nir_deref_instr *src,
                           enum gl_access_qualifier dest_access,
                           enum gl_access_qualifier src_access)
{
   nir_intrinsic_instr *copy =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_copy_deref);
   copy->src[0] = nir_src_for_ssa(&dest->dest.ssa);
   copy->src[1] = nir_src_for_ssa(&src->dest.ssa);
   nir_intrinsic_set_dst_access(copy, dest_access);
   nir_intrinsic_set_src_access(copy, src_access);
   nir_builder_instr_insert(build, &copy->instr);
}

static inline void
nir_copy_deref(nir_builder *build, nir_deref_instr *dest, nir_deref_instr *src)
{
   nir_copy_deref_with_access(build, dest, src,
                              (enum gl_access_qualifier) 0,
                              (enum gl_access_qualifier) 0);
}

static inline void
nir_memcpy_deref_with_access(nir_builder *build, nir_deref_instr *dest,
                             nir_deref_instr *src, nir_ssa_def *size,
                             enum gl_access_qualifier dest_access,
                             enum gl_access_qualifier src_access)
{
   nir_intrinsic_instr *copy =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_memcpy_deref);
   copy->src[0] = nir_src_for_ssa(&dest->dest.ssa);
   copy->src[1] = nir_src_for_ssa(&src->dest.ssa);
   copy->src[2] = nir_src_for_ssa(size);
   nir_intrinsic_set_dst_access(copy, dest_access);
   nir_intrinsic_set_src_access(copy, src_access);
   nir_builder_instr_insert(build, &copy->instr);
}

static inline void
nir_memcpy_deref(nir_builder *build, nir_deref_instr *dest,
                 nir_deref_instr *src, nir_ssa_def *size)
{
   nir_memcpy_deref_with_access(build, dest, src, size,
                                (enum gl_access_qualifier)0,
                                (enum gl_access_qualifier)0);
}

static inline nir_ssa_def *
nir_load_var(nir_builder *build, nir_variable *var)
{
   return nir_load_deref(build, nir_build_deref_var(build, var));
}

static inline void
nir_store_var(nir_builder *build, nir_variable *var, nir_ssa_def *value,
              unsigned writemask)
{
   nir_store_deref(build, nir_build_deref_var(build, var), value, writemask);
}

static inline void
nir_copy_var(nir_builder *build, nir_variable *dest, nir_variable *src)
{
   nir_copy_deref(build, nir_build_deref_var(build, dest),
                         nir_build_deref_var(build, src));
}

static inline nir_ssa_def *
nir_load_global(nir_builder *build, nir_ssa_def *addr, unsigned align,
                unsigned num_components, unsigned bit_size)
{
   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_load_global);
   load->num_components = num_components;
   load->src[0] = nir_src_for_ssa(addr);
   nir_intrinsic_set_align(load, align, 0);
   nir_ssa_dest_init(&load->instr, &load->dest,
                     num_components, bit_size, NULL);
   nir_builder_instr_insert(build, &load->instr);
   return &load->dest.ssa;
}

static inline void
nir_store_global(nir_builder *build, nir_ssa_def *addr, unsigned align,
                 nir_ssa_def *value, nir_component_mask_t write_mask)
{
   nir_intrinsic_instr *store =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_store_global);
   store->num_components = value->num_components;
   store->src[0] = nir_src_for_ssa(value);
   store->src[1] = nir_src_for_ssa(addr);
   nir_intrinsic_set_write_mask(store,
      write_mask & BITFIELD_MASK(value->num_components));
   nir_intrinsic_set_align(store, align, 0);
   nir_builder_instr_insert(build, &store->instr);
}

static inline nir_ssa_def *
nir_load_param(nir_builder *build, uint32_t param_idx)
{
   assert(param_idx < build->impl->function->num_params);
   nir_parameter *param = &build->impl->function->params[param_idx];

   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_load_param);
   nir_intrinsic_set_param_idx(load, param_idx);
   load->num_components = param->num_components;
   nir_ssa_dest_init(&load->instr, &load->dest,
                     param->num_components, param->bit_size, NULL);
   nir_builder_instr_insert(build, &load->instr);
   return &load->dest.ssa;
}

static inline nir_ssa_def *
nir_load_reloc_const_intel(nir_builder *b, uint32_t id)
{
   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(b->shader,
                                 nir_intrinsic_load_reloc_const_intel);
   nir_intrinsic_set_param_idx(load, id);
   nir_ssa_dest_init(&load->instr, &load->dest, 1, 32, NULL);
   nir_builder_instr_insert(b, &load->instr);
   return &load->dest.ssa;
}

static inline nir_ssa_def *
nir_convert_alu_types(nir_builder *b, nir_ssa_def *src,
                      nir_alu_type src_type, nir_alu_type dest_type,
                      nir_rounding_mode round, bool saturate)
{
   assert(nir_alu_type_get_type_size(dest_type) != 0);
   assert(nir_alu_type_get_type_size(src_type) == 0 ||
          nir_alu_type_get_type_size(src_type) == src->bit_size);
   src_type = (nir_alu_type)(src_type | src->bit_size);

   nir_intrinsic_instr *conv =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_convert_alu_types);
   conv->src[0] = nir_src_for_ssa(src);
   conv->num_components = src->num_components;
   nir_intrinsic_set_src_type(conv, src_type);
   nir_intrinsic_set_dest_type(conv, dest_type);
   nir_intrinsic_set_rounding_mode(conv, round);
   nir_intrinsic_set_saturate(conv, saturate);
   nir_ssa_dest_init(&conv->instr, &conv->dest, src->num_components,
                     nir_alu_type_get_type_size(dest_type), NULL);
   nir_builder_instr_insert(b, &conv->instr);
   return &conv->dest.ssa;
}

#include "nir_builder_opcodes.h"

static inline nir_ssa_def *
nir_f2b(nir_builder *build, nir_ssa_def *f)
{
   return nir_f2b1(build, f);
}

static inline nir_ssa_def *
nir_i2b(nir_builder *build, nir_ssa_def *i)
{
   return nir_i2b1(build, i);
}

static inline nir_ssa_def *
nir_b2f(nir_builder *build, nir_ssa_def *b, uint32_t bit_size)
{
   switch (bit_size) {
   case 64: return nir_b2f64(build, b);
   case 32: return nir_b2f32(build, b);
   case 16: return nir_b2f16(build, b);
   default:
      unreachable("Invalid bit-size");
   };
}

static inline nir_ssa_def *
nir_b2i(nir_builder *build, nir_ssa_def *b, uint32_t bit_size)
{
   switch (bit_size) {
   case 64: return nir_b2i64(build, b);
   case 32: return nir_b2i32(build, b);
   case 16: return nir_b2i16(build, b);
   case 8:  return nir_b2i8(build, b);
   default:
      unreachable("Invalid bit-size");
   };
}
static inline nir_ssa_def *
nir_load_barycentric(nir_builder *build, nir_intrinsic_op op,
                     unsigned interp_mode)
{
   unsigned num_components = op == nir_intrinsic_load_barycentric_model ? 3 : 2;
   nir_intrinsic_instr *bary = nir_intrinsic_instr_create(build->shader, op);
   nir_ssa_dest_init(&bary->instr, &bary->dest, num_components, 32, NULL);
   nir_intrinsic_set_interp_mode(bary, interp_mode);
   nir_builder_instr_insert(build, &bary->instr);
   return &bary->dest.ssa;
}

static inline void
nir_jump(nir_builder *build, nir_jump_type jump_type)
{
   assert(jump_type != nir_jump_goto && jump_type != nir_jump_goto_if);
   nir_jump_instr *jump = nir_jump_instr_create(build->shader, jump_type);
   nir_builder_instr_insert(build, &jump->instr);
}

static inline void
nir_goto(nir_builder *build, struct nir_block *target)
{
   assert(!build->impl->structured);
   nir_jump_instr *jump = nir_jump_instr_create(build->shader, nir_jump_goto);
   jump->target = target;
   nir_builder_instr_insert(build, &jump->instr);
}

static inline void
nir_goto_if(nir_builder *build, struct nir_block *target, nir_src cond,
            struct nir_block *else_target)
{
   assert(!build->impl->structured);
   nir_jump_instr *jump = nir_jump_instr_create(build->shader, nir_jump_goto_if);
   jump->condition = cond;
   jump->target = target;
   jump->else_target = else_target;
   nir_builder_instr_insert(build, &jump->instr);
}

static inline nir_ssa_def *
nir_compare_func(nir_builder *b, enum compare_func func,
                 nir_ssa_def *src0, nir_ssa_def *src1)
{
   switch (func) {
   case COMPARE_FUNC_NEVER:
      return nir_imm_int(b, 0);
   case COMPARE_FUNC_ALWAYS:
      return nir_imm_int(b, ~0);
   case COMPARE_FUNC_EQUAL:
      return nir_feq(b, src0, src1);
   case COMPARE_FUNC_NOTEQUAL:
      return nir_fneu(b, src0, src1);
   case COMPARE_FUNC_GREATER:
      return nir_flt(b, src1, src0);
   case COMPARE_FUNC_GEQUAL:
      return nir_fge(b, src0, src1);
   case COMPARE_FUNC_LESS:
      return nir_flt(b, src0, src1);
   case COMPARE_FUNC_LEQUAL:
      return nir_fge(b, src1, src0);
   }
   unreachable("bad compare func");
}

static inline void
nir_scoped_barrier(nir_builder *b,
                   nir_scope exec_scope,
                   nir_scope mem_scope,
                   nir_memory_semantics mem_semantics,
                   nir_variable_mode mem_modes)
{
   nir_intrinsic_instr *intrin =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_scoped_barrier);
   nir_intrinsic_set_execution_scope(intrin, exec_scope);
   nir_intrinsic_set_memory_scope(intrin, mem_scope);
   nir_intrinsic_set_memory_semantics(intrin, mem_semantics);
   nir_intrinsic_set_memory_modes(intrin, mem_modes);
   nir_builder_instr_insert(b, &intrin->instr);
}

static inline void
nir_scoped_memory_barrier(nir_builder *b,
                          nir_scope scope,
                          nir_memory_semantics semantics,
                          nir_variable_mode modes)
{
   nir_scoped_barrier(b, NIR_SCOPE_NONE, scope, semantics, modes);
}

static inline nir_ssa_def *
nir_convert_to_bit_size(nir_builder *b,
                    nir_ssa_def *src,
                    nir_alu_type type,
                    unsigned bit_size)
{
   nir_alu_type base_type = nir_alu_type_get_base_type(type);
   nir_alu_type dst_type = (nir_alu_type)(bit_size | base_type);

   nir_op opcode =
      nir_type_conversion_op(type, dst_type, nir_rounding_mode_undef);

   return nir_build_alu(b, opcode, src, NULL, NULL, NULL);
}

static inline nir_ssa_def *
nir_i2iN(nir_builder *b, nir_ssa_def *src, unsigned bit_size)
{
   return nir_convert_to_bit_size(b, src, nir_type_int, bit_size);
}

static inline nir_ssa_def *
nir_u2uN(nir_builder *b, nir_ssa_def *src, unsigned bit_size)
{
   return nir_convert_to_bit_size(b, src, nir_type_uint, bit_size);
}

static inline nir_ssa_def *
nir_b2bN(nir_builder *b, nir_ssa_def *src, unsigned bit_size)
{
   return nir_convert_to_bit_size(b, src, nir_type_bool, bit_size);
}

static inline nir_ssa_def *
nir_f2fN(nir_builder *b, nir_ssa_def *src, unsigned bit_size)
{
   return nir_convert_to_bit_size(b, src, nir_type_float, bit_size);
}

#endif /* NIR_BUILDER_H */
