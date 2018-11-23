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
#include "util/half_float.h"

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
nir_imm_bool(nir_builder *build, bool x)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   v.u32[0] = x ? NIR_TRUE : NIR_FALSE;

   return nir_build_imm(build, 1, 32, v);
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
nir_imm_float16(nir_builder *build, float x)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   v.u16[0] = _mesa_float_to_half(x);

   return nir_build_imm(build, 1, 16, v);
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
nir_imm_floatN_t(nir_builder *build, double x, unsigned bit_size)
{
   switch (bit_size) {
   case 16:
      return nir_imm_float16(build, x);
   case 32:
      return nir_imm_float(build, x);
   case 64:
      return nir_imm_double(build, x);
   }

   unreachable("unknown float immediate bit size");
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
nir_imm_ivec2(nir_builder *build, int x, int y)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   v.i32[0] = x;
   v.i32[1] = y;

   return nir_build_imm(build, 2, 32, v);
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
nir_swizzle(nir_builder *build, nir_ssa_def *src, const unsigned *swiz,
            unsigned num_components, bool use_fmov)
{
   assert(num_components <= NIR_MAX_VEC_COMPONENTS);
   nir_alu_src alu_src = { NIR_SRC_INIT };
   alu_src.src = nir_src_for_ssa(src);
   for (unsigned i = 0; i < num_components && i < NIR_MAX_VEC_COMPONENTS; i++)
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
   return nir_bany_inequal(b, src, nir_imm_false(b));
}

static inline nir_ssa_def *
nir_channel(nir_builder *b, nir_ssa_def *def, unsigned c)
{
   return nir_swizzle(b, def, &c, 1, false);
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

   return nir_swizzle(b, def, swizzle, num_channels, false);
}

static inline nir_ssa_def *
nir_iadd_imm(nir_builder *build, nir_ssa_def *x, uint64_t y)
{
   return nir_iadd(build, x, nir_imm_intN_t(build, y, x->bit_size));
}

static inline nir_ssa_def *
nir_imul_imm(nir_builder *build, nir_ssa_def *x, uint64_t y)
{
   return nir_imul(build, x, nir_imm_intN_t(build, y, x->bit_size));
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
      nir_ssa_def *val;
      switch (dest_bit_size) {
      case 64: val = nir_u2u64(b, nir_channel(b, src, i));  break;
      case 32: val = nir_u2u32(b, nir_channel(b, src, i));  break;
      case 16: val = nir_u2u16(b, nir_channel(b, src, i));  break;
      default: unreachable("Invalid bit size");
      }
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
      nir_ssa_def *val = nir_ushr(b, src, nir_imm_int(b, i * dest_bit_size));
      switch (dest_bit_size) {
      case 32: dest_comps[i] = nir_u2u32(b, val);  break;
      case 16: dest_comps[i] = nir_u2u16(b, val);  break;
      case 8:  dest_comps[i] = nir_u2u8(b, val);   break;
      default: unreachable("Invalid bit size");
      }
   }
   return nir_vec(b, dest_comps, dest_num_components);
}

static inline nir_ssa_def *
nir_bitcast_vector(nir_builder *b, nir_ssa_def *src, unsigned dest_bit_size)
{
   assert((src->bit_size * src->num_components) % dest_bit_size == 0);
   const unsigned dest_num_components =
      (src->bit_size * src->num_components) / dest_bit_size;
   assert(dest_num_components <= NIR_MAX_VEC_COMPONENTS);

   if (src->bit_size > dest_bit_size) {
      assert(src->bit_size % dest_bit_size == 0);
      if (src->num_components == 1) {
         return nir_unpack_bits(b, src, dest_bit_size);
      } else {
         const unsigned divisor = src->bit_size / dest_bit_size;
         assert(src->num_components * divisor == dest_num_components);
         nir_ssa_def *dest[NIR_MAX_VEC_COMPONENTS];
         for (unsigned i = 0; i < src->num_components; i++) {
            nir_ssa_def *unpacked =
               nir_unpack_bits(b, nir_channel(b, src, i), dest_bit_size);
            assert(unpacked->num_components == divisor);
            for (unsigned j = 0; j < divisor; j++)
               dest[i * divisor + j] = nir_channel(b, unpacked, j);
         }
         return nir_vec(b, dest, dest_num_components);
      }
   } else if (src->bit_size < dest_bit_size) {
      assert(dest_bit_size % src->bit_size == 0);
      if (dest_num_components == 1) {
         return nir_pack_bits(b, src, dest_bit_size);
      } else {
         const unsigned divisor = dest_bit_size / src->bit_size;
         assert(src->num_components == dest_num_components * divisor);
         nir_ssa_def *dest[NIR_MAX_VEC_COMPONENTS];
         for (unsigned i = 0; i < dest_num_components; i++) {
            nir_component_mask_t src_mask =
               ((1 << divisor) - 1) << (i * divisor);
            dest[i] = nir_pack_bits(b, nir_channels(b, src, src_mask),
                                       dest_bit_size);
         }
         return nir_vec(b, dest, dest_num_components);
      }
   } else {
      assert(src->bit_size == dest_bit_size);
      return src;
   }
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
   static uint8_t trivial_swizzle[NIR_MAX_VEC_COMPONENTS];
   for (int i = 0; i < NIR_MAX_VEC_COMPONENTS; ++i)
      trivial_swizzle[i] = i;
   nir_alu_src *src = &instr->src[srcn];
   unsigned num_components = nir_ssa_alu_instr_src_components(instr, srcn);

   if (src->src.is_ssa && (src->src.ssa->num_components == num_components) &&
       !src->abs && !src->negate &&
       (memcmp(src->swizzle, trivial_swizzle, num_components) == 0))
      return src->src.ssa;

   return nir_imov_alu(build, *src, num_components);
}

static inline nir_deref_instr *
nir_build_deref_var(nir_builder *build, nir_variable *var)
{
   nir_deref_instr *deref =
      nir_deref_instr_create(build->shader, nir_deref_type_var);

   deref->mode = var->data.mode;
   deref->type = var->type;
   deref->var = var;

   nir_ssa_dest_init(&deref->instr, &deref->dest, 1, 32, NULL);

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
   assert(glsl_type_is_struct(parent->type));

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
                     nir_variable_mode mode, const struct glsl_type *type)
{
   nir_deref_instr *deref =
      nir_deref_instr_create(build->shader, nir_deref_type_cast);

   deref->mode = mode;
   deref->type = type;
   deref->parent = nir_src_for_ssa(parent);

   nir_ssa_dest_init(&deref->instr, &deref->dest,
                     parent->num_components, parent->bit_size, NULL);

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
             glsl_type_is_array(parent->type));
      assert(glsl_get_length(parent->type) ==
             glsl_get_length(leader_parent->type));

      if (leader->deref_type == nir_deref_type_array) {
         assert(leader->arr.index.is_ssa);
         return nir_build_deref_array(b, parent, leader->arr.index.ssa);
      } else {
         return nir_build_deref_array_wildcard(b, parent);
      }

   case nir_deref_type_struct:
      assert(glsl_type_is_struct(parent->type));
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

static inline nir_ssa_def *
nir_load_deref(nir_builder *build, nir_deref_instr *deref)
{
   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_load_deref);
   load->num_components = glsl_get_vector_elements(deref->type);
   load->src[0] = nir_src_for_ssa(&deref->dest.ssa);
   nir_ssa_dest_init(&load->instr, &load->dest, load->num_components,
                     glsl_get_bit_size(deref->type), NULL);
   nir_builder_instr_insert(build, &load->instr);
   return &load->dest.ssa;
}

static inline void
nir_store_deref(nir_builder *build, nir_deref_instr *deref,
                nir_ssa_def *value, unsigned writemask)
{
   nir_intrinsic_instr *store =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_store_deref);
   store->num_components = glsl_get_vector_elements(deref->type);
   store->src[0] = nir_src_for_ssa(&deref->dest.ssa);
   store->src[1] = nir_src_for_ssa(value);
   nir_intrinsic_set_write_mask(store,
                                writemask & ((1 << store->num_components) - 1));
   nir_builder_instr_insert(build, &store->instr);
}

static inline void
nir_copy_deref(nir_builder *build, nir_deref_instr *dest, nir_deref_instr *src)
{
   nir_intrinsic_instr *copy =
      nir_intrinsic_instr_create(build->shader, nir_intrinsic_copy_deref);
   copy->src[0] = nir_src_for_ssa(&dest->dest.ssa);
   copy->src[1] = nir_src_for_ssa(&src->dest.ssa);
   nir_builder_instr_insert(build, &copy->instr);
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
