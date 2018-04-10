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

   /* Whether new ALU instructions will be marked "exact" */
   bool exact;

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
   build->shader = nir_shader_create(mem_ctx, stage, options, NULL);
   nir_function *func = nir_function_create(build->shader, "main");
   build->exact = false;
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
nir_push_if(nir_builder *build, nir_ssa_def *condition)
{
   nir_if *nif = nir_if_create(build->shader);
   nif->condition = nir_src_for_ssa(condition);
   nir_builder_cf_insert(build, &nif->cf_node);
   build->cursor = nir_before_cf_list(&nif->then_list);
   return nif;
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

   return &undef->def;
}

static inline nir_ssa_def *
nir_build_imm(nir_builder *build, unsigned num_components,
              unsigned bit_size, nir_const_value value)
{
   nir_load_const_instr *load_const =
      nir_load_const_instr_create(build->shader, num_components, bit_size);
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
   v.f32[0] = x;

   return nir_build_imm(build, 1, 32, v);
}

static inline nir_ssa_def *
nir_imm_double(nir_builder *build, double x)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   v.f64[0] = x;

   return nir_build_imm(build, 1, 64, v);
}

static inline nir_ssa_def *
nir_imm_vec4(nir_builder *build, float x, float y, float z, float w)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   v.f32[0] = x;
   v.f32[1] = y;
   v.f32[2] = z;
   v.f32[3] = w;

   return nir_build_imm(build, 4, 32, v);
}

static inline nir_ssa_def *
nir_imm_int(nir_builder *build, int x)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   v.i32[0] = x;

   return nir_build_imm(build, 1, 32, v);
}

static inline nir_ssa_def *
nir_imm_int64(nir_builder *build, int64_t x)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   v.i64[0] = x;

   return nir_build_imm(build, 1, 64, v);
}

static inline nir_ssa_def *
nir_imm_intN_t(nir_builder *build, uint64_t x, unsigned bit_size)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   assert(bit_size <= 64);
   v.i64[0] = x & (~0ull >> (64 - bit_size));

   return nir_build_imm(build, 1, bit_size, v);
}

static inline nir_ssa_def *
nir_imm_ivec4(nir_builder *build, int x, int y, int z, int w)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   v.i32[0] = x;
   v.i32[1] = y;
   v.i32[2] = z;
   v.i32[3] = w;

   return nir_build_imm(build, 4, 32, v);
}

static inline nir_ssa_def *
nir_build_alu(nir_builder *build, nir_op op, nir_ssa_def *src0,
              nir_ssa_def *src1, nir_ssa_def *src2, nir_ssa_def *src3)
{
   const nir_op_info *op_info = &nir_op_infos[op];
   nir_alu_instr *instr = nir_alu_instr_create(build->shader, op);
   if (!instr)
      return NULL;

   instr->exact = build->exact;

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
      for (unsigned j = instr->src[i].src.ssa->num_components; j < 4; j++) {
         instr->src[i].swizzle[j] = instr->src[i].src.ssa->num_components - 1;
      }
   }

   nir_ssa_dest_init(&instr->instr, &instr->dest.dest, num_components,
                     bit_size, NULL);
   instr->dest.write_mask = (1 << num_components) - 1;

   nir_builder_instr_insert(build, &instr->instr);

   return &instr->dest.dest.ssa;
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
   nir_ssa_dest_init(&mov->instr, &mov->dest.dest, num_components,
                     nir_src_bit_size(src.src), NULL);
   mov->exact = build->exact;
   mov->dest.write_mask = (1 << num_components) - 1;
   mov->src[0] = src;
   nir_builder_instr_insert(build, &mov->instr);

   return &mov->dest.dest.ssa;
}

static inline nir_ssa_def *
nir_imov_alu(nir_builder *build, nir_alu_src src, unsigned num_components)
{
   nir_alu_instr *mov = nir_alu_instr_create(build->shader, nir_op_imov);
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
nir_swizzle(nir_builder *build, nir_ssa_def *src, const unsigned swiz[4],
            unsigned num_components, bool use_fmov)
{
   nir_alu_src alu_src = { NIR_SRC_INIT };
   alu_src.src = nir_src_for_ssa(src);
   for (unsigned i = 0; i < num_components; i++)
      alu_src.swizzle[i] = swiz[i];

   return use_fmov ? nir_fmov_alu(build, alu_src, num_components) :
                     nir_imov_alu(build, alu_src, num_components);
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
   default:
      unreachable("bad component size");
   }

   return NULL;
}

static inline nir_ssa_def *
nir_bany_inequal(nir_builder *b, nir_ssa_def *src0, nir_ssa_def *src1)
{
   switch (src0->num_components) {
   case 1: return nir_ine(b, src0, src1);
   case 2: return nir_bany_inequal2(b, src0, src1);
   case 3: return nir_bany_inequal3(b, src0, src1);
   case 4: return nir_bany_inequal4(b, src0, src1);
   default:
      unreachable("bad component size");
   }
}

static inline nir_ssa_def *
nir_bany(nir_builder *b, nir_ssa_def *src)
{
   return nir_bany_inequal(b, src, nir_imm_int(b, 0));
}

static inline nir_ssa_def *
nir_channel(nir_builder *b, nir_ssa_def *def, unsigned c)
{
   unsigned swizzle[4] = {c, c, c, c};
   return nir_swizzle(b, def, swizzle, 1, false);
}

static inline nir_ssa_def *
nir_channels(nir_builder *b, nir_ssa_def *def, unsigned mask)
{
   unsigned num_channels = 0, swizzle[4] = { 0, 0, 0, 0 };

   for (unsigned i = 0; i < 4; i++) {
      if ((mask & (1 << i)) == 0)
         continue;
      swizzle[num_channels++] = i;
   }

   return nir_swizzle(b, def, swizzle, num_channels, false);
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
 * Similar to nir_ssa_for_src(), but for alu srcs, respecting the
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
nir_load_reg(nir_builder *build, nir_register *reg)
{
   return nir_ssa_for_src(build, nir_src_for_reg(reg), reg->num_components);
}

static inline nir_ssa_def *
nir_load_var(nir_builder *build, nir_variable *var)
{
   const unsigned num_components = glsl_get_vector_elements(var->type);

   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_load_var);
   load->num_components = num_components;
   load->variables[0] = nir_deref_var_create(load, var);
   nir_ssa_dest_init(&load->instr, &load->dest, num_components,
                     glsl_get_bit_size(var->type), NULL);
   nir_builder_instr_insert(build, &load->instr);
   return &load->dest.ssa;
}

static inline nir_ssa_def *
nir_load_deref_var(nir_builder *build, nir_deref_var *deref)
{
   const struct glsl_type *type = nir_deref_tail(&deref->deref)->type;
   const unsigned num_components = glsl_get_vector_elements(type);

   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_load_var);
   load->num_components = num_components;
   load->variables[0] = nir_deref_var_clone(deref, load);
   nir_ssa_dest_init(&load->instr, &load->dest, num_components,
                     glsl_get_bit_size(type), NULL);
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

static inline void
nir_store_deref_var(nir_builder *build, nir_deref_var *deref,
                    nir_ssa_def *value, unsigned writemask)
{
   const unsigned num_components =
      glsl_get_vector_elements(nir_deref_tail(&deref->deref)->type);

   nir_intrinsic_instr *store =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_store_var);
   store->num_components = num_components;
   store->const_index[0] = writemask & ((1 << num_components) - 1);
   store->variables[0] = nir_deref_var_clone(deref, store);
   store->src[0] = nir_src_for_ssa(value);
   nir_builder_instr_insert(build, &store->instr);
}

static inline void
nir_copy_deref_var(nir_builder *build, nir_deref_var *dest, nir_deref_var *src)
{
   assert(nir_deref_tail(&dest->deref)->type ==
          nir_deref_tail(&src->deref)->type);

   nir_intrinsic_instr *copy =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_copy_var);
   copy->variables[0] = nir_deref_var_clone(dest, copy);
   copy->variables[1] = nir_deref_var_clone(src, copy);
   nir_builder_instr_insert(build, &copy->instr);
}

static inline void
nir_copy_var(nir_builder *build, nir_variable *dest, nir_variable *src)
{
   nir_intrinsic_instr *copy =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_copy_var);
   copy->variables[0] = nir_deref_var_create(copy, dest);
   copy->variables[1] = nir_deref_var_create(copy, src);
   nir_builder_instr_insert(build, &copy->instr);
}

#include "nir_builder_opcodes.h"

static inline nir_ssa_def *
nir_load_barycentric(nir_builder *build, nir_intrinsic_op op,
                     unsigned interp_mode)
{
   nir_intrinsic_instr *bary = nir_intrinsic_instr_create(build->shader, op);
   nir_ssa_dest_init(&bary->instr, &bary->dest, 2, 32, NULL);
   nir_intrinsic_set_interp_mode(bary, interp_mode);
   nir_builder_instr_insert(build, &bary->instr);
   return &bary->dest.ssa;
}

static inline void
nir_jump(nir_builder *build, nir_jump_type jump_type)
{
   nir_jump_instr *jump = nir_jump_instr_create(build->shader, jump_type);
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
      return nir_fne(b, src0, src1);
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

#endif /* NIR_BUILDER_H */
