/*
 * Copyright © 2017-2018 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#define GPU 600

#include "ir3_context.h"
#include "ir3_image.h"

/*
 * Handlers for instructions changed/added in a6xx:
 *
 * Starting with a6xx, isam and stbi is used for SSBOs as well; stbi and the
 * atomic instructions (used for both SSBO and image) use a new instruction
 * encoding compared to a4xx/a5xx.
 */

static void
lower_ssbo_offset(struct ir3_context *ctx, nir_intrinsic_instr *intr,
                  nir_src *offset_src,
                  struct ir3_instruction **offset, unsigned *imm_offset)
{
   if (ctx->compiler->has_ssbo_imm_offsets) {
      ir3_lower_imm_offset(ctx, intr, offset_src, 7, offset, imm_offset);
   } else {
      assert(nir_intrinsic_base(intr) == 0);
      *offset = ir3_get_src(ctx, offset_src)[0];
      *imm_offset = 0;
   }
}

static void
emit_load_uav(struct ir3_context *ctx, nir_intrinsic_instr *intr,
              struct ir3_instruction *offset,
              unsigned imm_offset_val,
              struct ir3_instruction **dst)
{
   struct ir3_builder *b = &ctx->build;
   struct ir3_instruction *ldib;

   struct ir3_instruction *imm_offset = create_immed(b, imm_offset_val);

   ldib = ir3_LDIB(b, ir3_ssbo_to_ibo(ctx, intr->src[0]), 0, offset, 0,
                   imm_offset, 0);
   ldib->dsts[0]->wrmask = MASK(intr->num_components);
   ldib->cat6.iim_val = intr->num_components;
   ldib->cat6.d = reg_elems(offset->dsts[0]);
   switch (intr->def.bit_size) {
   case 8:
      /* This encodes the 8-bit SSBO load and matches blob's encoding of
       * imageBuffer access using VK_FORMAT_R8 and the dedicated 8-bit
       * descriptor. No vectorization is possible.
       */
      assert(intr->num_components == 1);

      ldib->cat6.type = TYPE_U16;
      ldib->cat6.typed = true;
      break;
   case 16:
      ldib->cat6.type = TYPE_U16;
      break;
   default:
      ldib->cat6.type = TYPE_U32;
      break;
   }
   ldib->barrier_class = IR3_BARRIER_BUFFER_R;
   ldib->barrier_conflict = IR3_BARRIER_BUFFER_W;

   if (imm_offset_val) {
      assert(ctx->compiler->has_ssbo_imm_offsets);
      ldib->flags |= IR3_INSTR_IMM_OFFSET;
   }

   ir3_handle_bindless_cat6(ldib, intr->src[0]);
   ir3_handle_nonuniform(ldib, intr);

   ir3_split_dest(b, dst, ldib, 0, intr->num_components);
}

/* src[] = { buffer_index, offset }. No const_index */
static void
emit_intrinsic_load_ssbo(struct ir3_context *ctx, nir_intrinsic_instr *intr,
                         struct ir3_instruction **dst)
{
   struct ir3_instruction *offset;
   unsigned imm_offset_val;

   lower_ssbo_offset(ctx, intr, &intr->src[2], &offset, &imm_offset_val);
   emit_load_uav(ctx, intr, offset, imm_offset_val, dst);
}

static void
emit_intrinsic_load_uav(struct ir3_context *ctx, nir_intrinsic_instr *intr,
                        struct ir3_instruction **dst)
{
   struct ir3_builder *b = &ctx->build;
   struct ir3_instruction *offset;

   offset = ir3_create_collect(b, ir3_get_src(ctx, &intr->src[1]), 2);

   emit_load_uav(ctx, intr, offset, 0, dst);
}

/* src[] = { value, block_index, offset }. const_index[] = { write_mask } */
static void
emit_intrinsic_store_ssbo(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
   struct ir3_builder *b = &ctx->build;
   struct ir3_instruction *stib, *val, *offset;
   unsigned wrmask = nir_intrinsic_write_mask(intr);
   unsigned ncomp = ffs(~wrmask) - 1;
   unsigned imm_offset_val;

   assert(wrmask == BITFIELD_MASK(intr->num_components));

   /* src0 is offset, src1 is immediate offset, src2 is value:
    */
   val = ir3_create_collect(b, ir3_get_src(ctx, &intr->src[0]), ncomp);

   /* Any 8-bit store will be done on a single-component value that additionally
    * has to be masked to clear up the higher bits or it will malfunction.
    */
   if (intr->src[0].ssa->bit_size == 8) {
      assert(ncomp == 1);

      struct ir3_instruction *mask = create_immed_typed(b, 0xff, TYPE_U8);
      val = ir3_AND_B(b, val, 0, mask, 0);
      val->dsts[0]->flags |= IR3_REG_HALF;
   }

   lower_ssbo_offset(ctx, intr, &intr->src[3], &offset, &imm_offset_val);
   struct ir3_instruction *imm_offset = create_immed(b, imm_offset_val);

   stib = ir3_STIB(b, ir3_ssbo_to_ibo(ctx, intr->src[1]), 0, offset, 0,
                   imm_offset, 0, val, 0);
   stib->cat6.iim_val = ncomp;
   stib->cat6.d = 1;
   switch (intr->src[0].ssa->bit_size) {
   case 8:
      /* As with ldib, this encodes the 8-bit SSBO store and matches blob's
       * encoding of imageBuffer access using VK_FORMAT_R8 and the extra 8-bit
       * descriptor. No vectorization is possible and we have to override the
       * relevant field anyway.
       */
      stib->cat6.type = TYPE_U16;
      stib->cat6.iim_val = 4;
      stib->cat6.typed = true;
      break;
   case 16:
      stib->cat6.type = TYPE_U16;
      break;
   default:
      stib->cat6.type = TYPE_U32;
      break;
   }
   stib->barrier_class = IR3_BARRIER_BUFFER_W;
   stib->barrier_conflict = IR3_BARRIER_BUFFER_R | IR3_BARRIER_BUFFER_W;

   if (imm_offset_val) {
      assert(ctx->compiler->has_ssbo_imm_offsets);
      stib->flags |= IR3_INSTR_IMM_OFFSET;
   }

   ir3_handle_bindless_cat6(stib, intr->src[1]);
   ir3_handle_nonuniform(stib, intr);

   array_insert(ctx->block, ctx->block->keeps, stib);
}

static struct ir3_instruction *
emit_atomic(struct ir3_builder *b, nir_atomic_op op,
            struct ir3_instruction *ibo, struct ir3_instruction *src0,
            struct ir3_instruction *src1)
{
   switch (op) {
   case nir_atomic_op_iadd:
      return ir3_ATOMIC_B_ADD(b, ibo, 0, src0, 0, src1, 0);
   case nir_atomic_op_imin:
      return ir3_ATOMIC_B_MIN(b, ibo, 0, src0, 0, src1, 0);
   case nir_atomic_op_umin:
      return ir3_ATOMIC_B_MIN(b, ibo, 0, src0, 0, src1, 0);
   case nir_atomic_op_imax:
      return ir3_ATOMIC_B_MAX(b, ibo, 0, src0, 0, src1, 0);
   case nir_atomic_op_umax:
      return ir3_ATOMIC_B_MAX(b, ibo, 0, src0, 0, src1, 0);
   case nir_atomic_op_iand:
      return ir3_ATOMIC_B_AND(b, ibo, 0, src0, 0, src1, 0);
   case nir_atomic_op_ior:
      return ir3_ATOMIC_B_OR(b, ibo, 0, src0, 0, src1, 0);
   case nir_atomic_op_ixor:
      return ir3_ATOMIC_B_XOR(b, ibo, 0, src0, 0, src1, 0);
   case nir_atomic_op_xchg:
      return ir3_ATOMIC_B_XCHG(b, ibo, 0, src0, 0, src1, 0);
   case nir_atomic_op_cmpxchg:
      return ir3_ATOMIC_B_CMPXCHG(b, ibo, 0, src0, 0, src1, 0);
   default:
      unreachable("boo");
   }
}

/*
 * SSBO atomic intrinsics
 *
 * All of the SSBO atomic memory operations read a value from memory,
 * compute a new value using one of the operations below, write the new
 * value to memory, and return the original value read.
 *
 * All operations take 3 sources except CompSwap that takes 4. These
 * sources represent:
 *
 * 0: The SSBO buffer index.
 * 1: The offset into the SSBO buffer of the variable that the atomic
 *    operation will operate on.
 * 2: The data parameter to the atomic function (i.e. the value to add
 *    in, etc).
 * 3: For CompSwap only: the second data parameter.
 */
static struct ir3_instruction *
emit_intrinsic_atomic_ssbo(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
   struct ir3_builder *b = &ctx->build;
   struct ir3_instruction *atomic, *ibo, *src0, *src1, *data, *dummy;
   nir_atomic_op op = nir_intrinsic_atomic_op(intr);
   type_t type = nir_atomic_op_type(op) == nir_type_int ? TYPE_S32 : TYPE_U32;
   if (intr->def.bit_size == 64) {
      type = TYPE_ATOMIC_U64;
   }

   ibo = ir3_ssbo_to_ibo(ctx, intr->src[0]);

   data = ir3_get_src(ctx, &intr->src[2])[0];

   /* So this gets a bit creative:
    *
    *    src0    - vecN offset/coords
    *    src1.x  - is actually destination register
    *    src1.y  - is 'data' except for cmpxchg where src2.y is 'compare'
    *    src1.z  - is 'data' for cmpxchg
    *
    * The combining src and dest kinda doesn't work out so well with how
    * scheduling and RA work. So we create a dummy src2 which is tied to the
    * destination in RA (i.e. must be allocated to the same vec2/vec3
    * register) and then immediately extract the first component.
    *
    * Note that nir already multiplies the offset by four
    */
   dummy = create_immed(b, 0);

   if (op == nir_atomic_op_cmpxchg) {
      src0 = ir3_get_src(ctx, &intr->src[4])[0];
      struct ir3_instruction *compare = ir3_get_src(ctx, &intr->src[3])[0];
      if (intr->def.bit_size == 64) {
         struct ir3_instruction *dummy2 = create_immed(b, 0);
         struct ir3_instruction *compare2 = ir3_get_src(ctx, &intr->src[3])[1];
         struct ir3_instruction *data2 = ir3_get_src(ctx, &intr->src[2])[1];
         src1 = ir3_collect(b, dummy, dummy2, compare, compare2, data, data2);
      } else {
         src1 = ir3_collect(b, dummy, compare, data);
      }
   } else {
      src0 = ir3_get_src(ctx, &intr->src[3])[0];
      if (intr->def.bit_size == 64) {
         struct ir3_instruction *dummy2 = create_immed(b, 0);
         struct ir3_instruction *data2 = ir3_get_src(ctx, &intr->src[2])[1];
         src1 = ir3_collect(b, dummy, dummy2, data, data2);
      } else {
         src1 = ir3_collect(b, dummy, data);
      }
   }

   atomic = emit_atomic(b, op, ibo, src0, src1);
   atomic->cat6.iim_val = 1;
   atomic->cat6.d = 1;
   atomic->cat6.type = type;
   atomic->barrier_class = IR3_BARRIER_BUFFER_W;
   atomic->barrier_conflict = IR3_BARRIER_BUFFER_R | IR3_BARRIER_BUFFER_W;
   ir3_handle_bindless_cat6(atomic, intr->src[0]);

   /* even if nothing consume the result, we can't DCE the instruction: */
   array_insert(ctx->block, ctx->block->keeps, atomic);

   atomic->dsts[0]->wrmask = src1->dsts[0]->wrmask;
   ir3_reg_tie(atomic->dsts[0], atomic->srcs[2]);
   ir3_handle_nonuniform(atomic, intr);

   size_t num_results = intr->def.bit_size == 64 ? 2 : 1;
   struct ir3_instruction *defs[num_results];
   ir3_split_dest(b, defs, atomic, 0, num_results);
   return ir3_create_collect(b, defs, num_results);
  }

/* src[] = { deref, coord, sample_index }. const_index[] = {} */
static void
emit_intrinsic_load_image(struct ir3_context *ctx, nir_intrinsic_instr *intr,
                          struct ir3_instruction **dst)
{
   struct ir3_builder *b = &ctx->build;
   struct ir3_instruction *ldib;
   struct ir3_instruction *const *coords = ir3_get_src(ctx, &intr->src[1]);
   unsigned ncoords = ir3_get_image_coords(intr, NULL);

   ldib = ir3_LDIB(b, ir3_image_to_ibo(ctx, intr->src[0]), 0,
                   ir3_create_collect(b, coords, ncoords), 0,
                   create_immed(b, 0), 0);
   ldib->dsts[0]->wrmask = MASK(intr->num_components);
   ldib->cat6.iim_val = intr->num_components;
   ldib->cat6.d = ncoords;
   ldib->cat6.type = ir3_get_type_for_image_intrinsic(intr);
   ldib->cat6.typed = true;
   ldib->barrier_class = IR3_BARRIER_IMAGE_R;
   ldib->barrier_conflict = IR3_BARRIER_IMAGE_W;
   ir3_handle_bindless_cat6(ldib, intr->src[0]);
   ir3_handle_nonuniform(ldib, intr);

   ir3_split_dest(b, dst, ldib, 0, intr->num_components);
}

/* src[] = { deref, coord, sample_index, value }. const_index[] = {} */
static void
emit_intrinsic_store_image(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
   struct ir3_builder *b = &ctx->build;
   struct ir3_instruction *stib;
   struct ir3_instruction *const *value = ir3_get_src(ctx, &intr->src[3]);
   struct ir3_instruction *const *coords = ir3_get_src(ctx, &intr->src[1]);
   unsigned ncoords = ir3_get_image_coords(intr, NULL);
   enum pipe_format format = nir_intrinsic_format(intr);
   unsigned ncomp = ir3_get_num_components_for_image_format(format);

   /* src0 is offset, src1 is value:
    */
   stib =
      ir3_STIB(b, ir3_image_to_ibo(ctx, intr->src[0]), 0,
               ir3_create_collect(b, coords, ncoords), 0, create_immed(b, 0), 0,
               ir3_create_collect(b, value, ncomp), 0);
   stib->cat6.iim_val = ncomp;
   stib->cat6.d = ncoords;
   stib->cat6.type = ir3_get_type_for_image_intrinsic(intr);
   stib->cat6.typed = true;
   stib->barrier_class = IR3_BARRIER_IMAGE_W;
   stib->barrier_conflict = IR3_BARRIER_IMAGE_R | IR3_BARRIER_IMAGE_W;
   ir3_handle_bindless_cat6(stib, intr->src[0]);
   ir3_handle_nonuniform(stib, intr);

   array_insert(ctx->block, ctx->block->keeps, stib);
}

/* src[] = { deref, coord, sample_index, value, compare }. const_index[] = {} */
static struct ir3_instruction *
emit_intrinsic_atomic_image(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
   struct ir3_builder *b = &ctx->build;
   struct ir3_instruction *atomic, *ibo, *src0, *src1, *dummy;
   struct ir3_instruction *const *coords = ir3_get_src(ctx, &intr->src[1]);
   struct ir3_instruction *value = ir3_get_src(ctx, &intr->src[3])[0];
   unsigned ncoords = ir3_get_image_coords(intr, NULL);
   nir_atomic_op op = nir_intrinsic_atomic_op(intr);

   ibo = ir3_image_to_ibo(ctx, intr->src[0]);

   /* So this gets a bit creative:
    *
    *    src0    - vecN offset/coords
    *    src1.x  - is actually destination register
    *    src1.y  - is 'value' except for cmpxchg where src2.y is 'compare'
    *    src1.z  - is 'value' for cmpxchg
    *
    * The combining src and dest kinda doesn't work out so well with how
    * scheduling and RA work. So we create a dummy src2 which is tied to the
    * destination in RA (i.e. must be allocated to the same vec2/vec3
    * register) and then immediately extract the first component.
    */
   dummy = create_immed(b, 0);
   src0 = ir3_create_collect(b, coords, ncoords);

   if (op == nir_atomic_op_cmpxchg) {
      struct ir3_instruction *compare = ir3_get_src(ctx, &intr->src[4])[0];
      src1 = ir3_collect(b, dummy, compare, value);
   } else {
      src1 = ir3_collect(b, dummy, value);
   }

   atomic = emit_atomic(b, op, ibo, src0, src1);
   atomic->cat6.iim_val = 1;
   atomic->cat6.d = ncoords;
   atomic->cat6.type = ir3_get_type_for_image_intrinsic(intr);
   atomic->cat6.typed = true;
   atomic->barrier_class = IR3_BARRIER_IMAGE_W;
   atomic->barrier_conflict = IR3_BARRIER_IMAGE_R | IR3_BARRIER_IMAGE_W;
   ir3_handle_bindless_cat6(atomic, intr->src[0]);

   /* even if nothing consume the result, we can't DCE the instruction: */
   array_insert(ctx->block, ctx->block->keeps, atomic);

   atomic->dsts[0]->wrmask = src1->dsts[0]->wrmask;
   ir3_reg_tie(atomic->dsts[0], atomic->srcs[2]);
   ir3_handle_nonuniform(atomic, intr);
   struct ir3_instruction *split;
   ir3_split_dest(b, &split, atomic, 0, 1);
   return split;
}

static void
emit_intrinsic_image_size(struct ir3_context *ctx, nir_intrinsic_instr *intr,
                          struct ir3_instruction **dst)
{
   struct ir3_builder *b = &ctx->build;
   struct ir3_instruction *ibo = ir3_image_to_ibo(ctx, intr->src[0]);
   struct ir3_instruction *resinfo = ir3_RESINFO(b, ibo, 0);
   resinfo->cat6.iim_val = 1;
   resinfo->cat6.d = intr->num_components;
   resinfo->cat6.type = TYPE_U32;
   resinfo->cat6.typed = false;
   /* resinfo has no writemask and always writes out 3 components: */
   compile_assert(ctx, intr->num_components <= 3);
   resinfo->dsts[0]->wrmask = MASK(3);
   ir3_handle_bindless_cat6(resinfo, intr->src[0]);
   ir3_handle_nonuniform(resinfo, intr);

   ir3_split_dest(b, dst, resinfo, 0, intr->num_components);
}

static void
emit_intrinsic_load_global_ir3(struct ir3_context *ctx,
                               nir_intrinsic_instr *intr,
                               struct ir3_instruction **dst)
{
   struct ir3_builder *b = &ctx->build;
   unsigned dest_components = nir_intrinsic_dest_components(intr);
   struct ir3_instruction *addr, *offset;

   addr = ir3_collect(b, ir3_get_src(ctx, &intr->src[0])[0],
                      ir3_get_src(ctx, &intr->src[0])[1]);

   struct ir3_instruction *load;

   bool const_offset_in_bounds =
      nir_src_is_const(intr->src[1]) &&
      nir_src_as_int(intr->src[1]) < (1 << 8) &&
      nir_src_as_int(intr->src[1]) > -(1 << 8);

   if (const_offset_in_bounds) {
      load = ir3_LDG(b, addr, 0,
                     create_immed(b, nir_src_as_int(intr->src[1]) * 4),
                     0, create_immed(b, dest_components), 0);
   } else {
      unsigned shift = ctx->compiler->gen >= 7 ? 2 : 0;
      offset = ir3_get_src(ctx, &intr->src[1])[0];
      if (shift) {
         /* A7XX TODO: Move to NIR for it to be properly optimized? */
         offset = ir3_SHL_B(b, offset, 0, create_immed(b, shift), 0);
      }
      load =
         ir3_LDG_A(b, addr, 0, offset, 0, create_immed(b, 0), 0,
                   create_immed(b, 0), 0, create_immed(b, dest_components), 0);
   }

   load->cat6.type = type_uint_size(intr->def.bit_size);
   load->dsts[0]->wrmask = MASK(dest_components);

   load->barrier_class = IR3_BARRIER_BUFFER_R;
   load->barrier_conflict = IR3_BARRIER_BUFFER_W;

   ir3_split_dest(b, dst, load, 0, dest_components);
}

static void
emit_intrinsic_store_global_ir3(struct ir3_context *ctx,
                                nir_intrinsic_instr *intr)
{
   struct ir3_builder *b = &ctx->build;
   struct ir3_instruction *value, *addr, *offset;
   unsigned ncomp = nir_intrinsic_src_components(intr, 0);

   addr = ir3_collect(b, ir3_get_src(ctx, &intr->src[1])[0],
                      ir3_get_src(ctx, &intr->src[1])[1]);

   value = ir3_create_collect(b, ir3_get_src(ctx, &intr->src[0]), ncomp);

   struct ir3_instruction *stg;

   bool const_offset_in_bounds = nir_src_is_const(intr->src[2]) &&
                                 nir_src_as_int(intr->src[2]) < (1 << 10) &&
                                 nir_src_as_int(intr->src[2]) > -(1 << 10);

   if (const_offset_in_bounds) {
      stg = ir3_STG(b, addr, 0,
                    create_immed(b, nir_src_as_int(intr->src[2]) * 4), 0,
                    value, 0,
                    create_immed(b, ncomp), 0);
   } else {
      offset = ir3_get_src(ctx, &intr->src[2])[0];
      if (ctx->compiler->gen >= 7) {
         /* A7XX TODO: Move to NIR for it to be properly optimized? */
         offset = ir3_SHL_B(b, offset, 0, create_immed(b, 2), 0);
      }
      stg =
         ir3_STG_A(b, addr, 0, offset, 0, create_immed(b, 0), 0,
                   create_immed(b, 0), 0, value, 0, create_immed(b, ncomp), 0);
   }

   stg->cat6.type = type_uint_size(intr->src[0].ssa->bit_size);
   stg->cat6.iim_val = 1;

   array_insert(ctx->block, ctx->block->keeps, stg);

   stg->barrier_class = IR3_BARRIER_BUFFER_W;
   stg->barrier_conflict = IR3_BARRIER_BUFFER_R | IR3_BARRIER_BUFFER_W;
}

static struct ir3_instruction *
emit_intrinsic_atomic_global(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
   struct ir3_builder *b = &ctx->build;
   struct ir3_instruction *addr, *atomic, *src1;
   struct ir3_instruction *value = ir3_get_src(ctx, &intr->src[1])[0];
   nir_atomic_op op = nir_intrinsic_atomic_op(intr);
   type_t type = nir_atomic_op_type(op) == nir_type_int ? TYPE_S32 : TYPE_U32;
   if (intr->def.bit_size == 64) {
      type = TYPE_ATOMIC_U64;
   }

   addr = ir3_collect(b, ir3_get_src(ctx, &intr->src[0])[0],
                      ir3_get_src(ctx, &intr->src[0])[1]);

   if (op == nir_atomic_op_cmpxchg) {
      struct ir3_instruction *compare = ir3_get_src(ctx, &intr->src[2])[0];
      src1 = ir3_collect(b, compare, value);
      if (intr->def.bit_size == 64) {
         struct ir3_instruction *compare2 = ir3_get_src(ctx, &intr->src[2])[1];
         struct ir3_instruction *value2 = ir3_get_src(ctx, &intr->src[1])[1];
         src1 = ir3_collect(b, compare, compare2, value, value2);
      } else {
         src1 = ir3_collect(b, compare, value);
      }
   } else {
      if (intr->def.bit_size == 64) {
         struct ir3_instruction *value2 = ir3_get_src(ctx, &intr->src[1])[1];
         src1 = ir3_collect(b, value, value2);
      } else {
         src1 = value;
      }
   }

   switch (op) {
   case nir_atomic_op_iadd:
      atomic = ir3_ATOMIC_G_ADD(b, addr, 0, src1, 0);
      break;
   case nir_atomic_op_imin:
      atomic = ir3_ATOMIC_G_MIN(b, addr, 0, src1, 0);
      type = TYPE_S32;
      break;
   case nir_atomic_op_umin:
      atomic = ir3_ATOMIC_G_MIN(b, addr, 0, src1, 0);
      break;
   case nir_atomic_op_imax:
      atomic = ir3_ATOMIC_G_MAX(b, addr, 0, src1, 0);
      type = TYPE_S32;
      break;
   case nir_atomic_op_umax:
      atomic = ir3_ATOMIC_G_MAX(b, addr, 0, src1, 0);
      break;
   case nir_atomic_op_iand:
      atomic = ir3_ATOMIC_G_AND(b, addr, 0, src1, 0);
      break;
   case nir_atomic_op_ior:
      atomic = ir3_ATOMIC_G_OR(b, addr, 0, src1, 0);
      break;
   case nir_atomic_op_ixor:
      atomic = ir3_ATOMIC_G_XOR(b, addr, 0, src1, 0);
      break;
   case nir_atomic_op_xchg:
      atomic = ir3_ATOMIC_G_XCHG(b, addr, 0, src1, 0);
      break;
   case nir_atomic_op_cmpxchg:
      atomic = ir3_ATOMIC_G_CMPXCHG(b, addr, 0, src1, 0);
      break;
   default:
      unreachable("Unknown global atomic op");
   }

   atomic->cat6.iim_val = 1;
   atomic->cat6.d = 1;
   atomic->cat6.type = type;
   atomic->barrier_class = IR3_BARRIER_BUFFER_W;
   atomic->barrier_conflict = IR3_BARRIER_BUFFER_R | IR3_BARRIER_BUFFER_W;
   atomic->dsts[0]->wrmask = MASK(intr->def.bit_size == 64 ? 2 : 1);

   /* even if nothing consume the result, we can't DCE the instruction: */
   array_insert(ctx->block, ctx->block->keeps, atomic);

   return atomic;
}

const struct ir3_context_funcs ir3_a6xx_funcs = {
   .emit_intrinsic_load_ssbo = emit_intrinsic_load_ssbo,
   .emit_intrinsic_load_uav = emit_intrinsic_load_uav,
   .emit_intrinsic_store_ssbo = emit_intrinsic_store_ssbo,
   .emit_intrinsic_atomic_ssbo = emit_intrinsic_atomic_ssbo,
   .emit_intrinsic_load_image = emit_intrinsic_load_image,
   .emit_intrinsic_store_image = emit_intrinsic_store_image,
   .emit_intrinsic_atomic_image = emit_intrinsic_atomic_image,
   .emit_intrinsic_image_size = emit_intrinsic_image_size,
   .emit_intrinsic_load_global_ir3 = emit_intrinsic_load_global_ir3,
   .emit_intrinsic_store_global_ir3 = emit_intrinsic_store_global_ir3,
   .emit_intrinsic_atomic_global = emit_intrinsic_atomic_global,
};
