/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "si_pipe.h"
#include "si_shader_internal.h"
#include "sid.h"

/**
 * Return a value that is equal to the given i32 \p index if it lies in [0,num)
 * or an undefined value in the same interval otherwise.
 */
static LLVMValueRef si_llvm_bound_index(struct si_shader_context *ctx, LLVMValueRef index,
                                        unsigned num)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef c_max = LLVMConstInt(ctx->ac.i32, num - 1, 0);
   LLVMValueRef cc;

   if (util_is_power_of_two_or_zero(num)) {
      index = LLVMBuildAnd(builder, index, c_max, "");
   } else {
      /* In theory, this MAX pattern should result in code that is
       * as good as the bit-wise AND above.
       *
       * In practice, LLVM generates worse code (at the time of
       * writing), because its value tracking is not strong enough.
       */
      cc = LLVMBuildICmp(builder, LLVMIntULE, index, c_max, "");
      index = LLVMBuildSelect(builder, cc, index, c_max, "");
   }

   return index;
}

static LLVMValueRef load_const_buffer_desc_fast_path(struct si_shader_context *ctx)
{
   LLVMValueRef ptr = ac_get_arg(&ctx->ac, ctx->const_and_shader_buffers);
   struct si_shader_selector *sel = ctx->shader->selector;

   /* Do the bounds checking with a descriptor, because
    * doing computation and manual bounds checking of 64-bit
    * addresses generates horrible VALU code with very high
    * VGPR usage and very low SIMD occupancy.
    */
   ptr = LLVMBuildPtrToInt(ctx->ac.builder, ptr, ctx->ac.intptr, "");

   LLVMValueRef desc0, desc1;
   desc0 = ptr;
   desc1 = LLVMConstInt(ctx->ac.i32, S_008F04_BASE_ADDRESS_HI(ctx->screen->info.address32_hi), 0);

   uint32_t rsrc3 = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                    S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

   if (ctx->screen->info.chip_class >= GFX10)
      rsrc3 |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
               S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) | S_008F0C_RESOURCE_LEVEL(1);
   else
      rsrc3 |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
               S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);

   LLVMValueRef desc_elems[] = {desc0, desc1,
                                LLVMConstInt(ctx->ac.i32, sel->info.constbuf0_num_slots * 16, 0),
                                LLVMConstInt(ctx->ac.i32, rsrc3, false)};

   return ac_build_gather_values(&ctx->ac, desc_elems, 4);
}

static LLVMValueRef load_ubo(struct ac_shader_abi *abi,
                             unsigned desc_set, unsigned binding,
                             bool valid_binding, LLVMValueRef index)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct si_shader_selector *sel = ctx->shader->selector;

   LLVMValueRef ptr = ac_get_arg(&ctx->ac, ctx->const_and_shader_buffers);

   if (sel->info.base.num_ubos == 1 && sel->info.base.num_ssbos == 0) {
      return load_const_buffer_desc_fast_path(ctx);
   }

   index = si_llvm_bound_index(ctx, index, ctx->num_const_buffers);
   index =
      LLVMBuildAdd(ctx->ac.builder, index, LLVMConstInt(ctx->ac.i32, SI_NUM_SHADER_BUFFERS, 0), "");

   return ac_build_load_to_sgpr(&ctx->ac, ptr, index);
}

static LLVMValueRef load_ssbo(struct ac_shader_abi *abi, LLVMValueRef index, bool write, bool non_uniform)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);

   /* Fast path if the shader buffer is in user SGPRs. */
   if (LLVMIsConstant(index) &&
       LLVMConstIntGetZExtValue(index) < ctx->shader->selector->cs_num_shaderbufs_in_user_sgprs)
      return ac_get_arg(&ctx->ac, ctx->cs_shaderbuf[LLVMConstIntGetZExtValue(index)]);

   LLVMValueRef rsrc_ptr = ac_get_arg(&ctx->ac, ctx->const_and_shader_buffers);

   index = si_llvm_bound_index(ctx, index, ctx->num_shader_buffers);
   index = LLVMBuildSub(ctx->ac.builder, LLVMConstInt(ctx->ac.i32, SI_NUM_SHADER_BUFFERS - 1, 0),
                        index, "");

   return ac_build_load_to_sgpr(&ctx->ac, rsrc_ptr, index);
}

/**
 * Given a 256-bit resource descriptor, force the DCC enable bit to off.
 *
 * At least on Tonga, executing image stores on images with DCC enabled and
 * non-trivial can eventually lead to lockups. This can occur when an
 * application binds an image as read-only but then uses a shader that writes
 * to it. The OpenGL spec allows almost arbitrarily bad behavior (including
 * program termination) in this case, but it doesn't cost much to be a bit
 * nicer: disabling DCC in the shader still leads to undefined results but
 * avoids the lockup.
 */
static LLVMValueRef force_dcc_off(struct si_shader_context *ctx, LLVMValueRef rsrc)
{
   if (ctx->screen->info.chip_class <= GFX7) {
      return rsrc;
   } else {
      LLVMValueRef i32_6 = LLVMConstInt(ctx->ac.i32, 6, 0);
      LLVMValueRef i32_C = LLVMConstInt(ctx->ac.i32, C_008F28_COMPRESSION_EN, 0);
      LLVMValueRef tmp;

      tmp = LLVMBuildExtractElement(ctx->ac.builder, rsrc, i32_6, "");
      tmp = LLVMBuildAnd(ctx->ac.builder, tmp, i32_C, "");
      return LLVMBuildInsertElement(ctx->ac.builder, rsrc, tmp, i32_6, "");
   }
}

/* AC_DESC_FMASK is handled exactly like AC_DESC_IMAGE. The caller should
 * adjust "index" to point to FMASK. */
static LLVMValueRef si_load_image_desc(struct si_shader_context *ctx, LLVMValueRef list,
                                       LLVMValueRef index, enum ac_descriptor_type desc_type,
                                       bool uses_store, bool bindless)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef rsrc;

   if (desc_type == AC_DESC_BUFFER) {
      index = ac_build_imad(&ctx->ac, index, LLVMConstInt(ctx->ac.i32, 2, 0), ctx->ac.i32_1);
      list = LLVMBuildPointerCast(builder, list, ac_array_in_const32_addr_space(ctx->ac.v4i32), "");
   } else {
      assert(desc_type == AC_DESC_IMAGE || desc_type == AC_DESC_FMASK);
   }

   if (bindless)
      rsrc = ac_build_load_to_sgpr_uint_wraparound(&ctx->ac, list, index);
   else
      rsrc = ac_build_load_to_sgpr(&ctx->ac, list, index);

   if (desc_type == AC_DESC_IMAGE && uses_store && ctx->ac.chip_class <= GFX9)
      rsrc = force_dcc_off(ctx, rsrc);
   return rsrc;
}

/**
 * Load an image view, fmask view. or sampler state descriptor.
 */
static LLVMValueRef si_load_sampler_desc(struct si_shader_context *ctx, LLVMValueRef list,
                                         LLVMValueRef index, enum ac_descriptor_type type)
{
   LLVMBuilderRef builder = ctx->ac.builder;

   switch (type) {
   case AC_DESC_IMAGE:
      /* The image is at [0:7]. */
      index = LLVMBuildMul(builder, index, LLVMConstInt(ctx->ac.i32, 2, 0), "");
      break;
   case AC_DESC_BUFFER:
      /* The buffer is in [4:7]. */
      index = ac_build_imad(&ctx->ac, index, LLVMConstInt(ctx->ac.i32, 4, 0), ctx->ac.i32_1);
      list = LLVMBuildPointerCast(builder, list, ac_array_in_const32_addr_space(ctx->ac.v4i32), "");
      break;
   case AC_DESC_FMASK:
      /* The FMASK is at [8:15]. */
      index = ac_build_imad(&ctx->ac, index, LLVMConstInt(ctx->ac.i32, 2, 0), ctx->ac.i32_1);
      break;
   case AC_DESC_SAMPLER:
      /* The sampler state is at [12:15]. */
      index = ac_build_imad(&ctx->ac, index, LLVMConstInt(ctx->ac.i32, 4, 0),
                            LLVMConstInt(ctx->ac.i32, 3, 0));
      list = LLVMBuildPointerCast(builder, list, ac_array_in_const32_addr_space(ctx->ac.v4i32), "");
      break;
   case AC_DESC_PLANE_0:
   case AC_DESC_PLANE_1:
   case AC_DESC_PLANE_2:
      /* Only used for the multiplane image support for Vulkan. Should
       * never be reached in radeonsi.
       */
      unreachable("Plane descriptor requested in radeonsi.");
   }

   return ac_build_load_to_sgpr(&ctx->ac, list, index);
}

static LLVMValueRef si_nir_load_sampler_desc(struct ac_shader_abi *abi, unsigned descriptor_set,
                                             unsigned base_index, unsigned constant_index,
                                             LLVMValueRef dynamic_index,
                                             enum ac_descriptor_type desc_type, bool image,
                                             bool write, bool bindless)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   LLVMBuilderRef builder = ctx->ac.builder;
   unsigned const_index = base_index + constant_index;

   assert(!descriptor_set);
   assert(desc_type <= AC_DESC_BUFFER);

   if (bindless) {
      LLVMValueRef list = ac_get_arg(&ctx->ac, ctx->bindless_samplers_and_images);

      /* dynamic_index is the bindless handle */
      if (image) {
         /* Bindless image descriptors use 16-dword slots. */
         dynamic_index =
            LLVMBuildMul(ctx->ac.builder, dynamic_index, LLVMConstInt(ctx->ac.i64, 2, 0), "");
         /* FMASK is right after the image. */
         if (desc_type == AC_DESC_FMASK) {
            dynamic_index = LLVMBuildAdd(ctx->ac.builder, dynamic_index, ctx->ac.i32_1, "");
         }

         return si_load_image_desc(ctx, list, dynamic_index, desc_type, write, true);
      }

      /* Since bindless handle arithmetic can contain an unsigned integer
       * wraparound and si_load_sampler_desc assumes there isn't any,
       * use GEP without "inbounds" (inside ac_build_pointer_add)
       * to prevent incorrect code generation and hangs.
       */
      dynamic_index =
         LLVMBuildMul(ctx->ac.builder, dynamic_index, LLVMConstInt(ctx->ac.i64, 2, 0), "");
      list = ac_build_pointer_add(&ctx->ac, list, dynamic_index);
      return si_load_sampler_desc(ctx, list, ctx->ac.i32_0, desc_type);
   }

   unsigned num_slots = image ? ctx->num_images : ctx->num_samplers;
   assert(const_index < num_slots || dynamic_index);

   LLVMValueRef list = ac_get_arg(&ctx->ac, ctx->samplers_and_images);
   LLVMValueRef index = LLVMConstInt(ctx->ac.i32, const_index, false);

   if (dynamic_index) {
      index = LLVMBuildAdd(builder, index, dynamic_index, "");

      /* From the GL_ARB_shader_image_load_store extension spec:
       *
       *    If a shader performs an image load, store, or atomic
       *    operation using an image variable declared as an array,
       *    and if the index used to select an individual element is
       *    negative or greater than or equal to the size of the
       *    array, the results of the operation are undefined but may
       *    not lead to termination.
       */
      index = si_llvm_bound_index(ctx, index, num_slots);
   }

   if (image) {
      /* Fast path if the image is in user SGPRs. */
      if (!dynamic_index &&
          const_index < ctx->shader->selector->cs_num_images_in_user_sgprs &&
          (desc_type == AC_DESC_IMAGE || desc_type == AC_DESC_BUFFER))
         return ac_get_arg(&ctx->ac, ctx->cs_image[const_index]);

      /* FMASKs are separate from images. */
      if (desc_type == AC_DESC_FMASK) {
         index =
            LLVMBuildAdd(ctx->ac.builder, index, LLVMConstInt(ctx->ac.i32, SI_NUM_IMAGES, 0), "");
      }
      index = LLVMBuildSub(ctx->ac.builder, LLVMConstInt(ctx->ac.i32, SI_NUM_IMAGE_SLOTS - 1, 0),
                           index, "");
      return si_load_image_desc(ctx, list, index, desc_type, write, false);
   }

   index = LLVMBuildAdd(ctx->ac.builder, index,
                        LLVMConstInt(ctx->ac.i32, SI_NUM_IMAGE_SLOTS / 2, 0), "");
   return si_load_sampler_desc(ctx, list, index, desc_type);
}

void si_llvm_init_resource_callbacks(struct si_shader_context *ctx)
{
   ctx->abi.load_ubo = load_ubo;
   ctx->abi.load_ssbo = load_ssbo;
   ctx->abi.load_sampler_desc = si_nir_load_sampler_desc;
}
