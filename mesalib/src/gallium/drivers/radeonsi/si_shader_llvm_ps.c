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

LLVMValueRef si_get_sample_id(struct si_shader_context *ctx)
{
   return si_unpack_param(ctx, ctx->args.ancillary, 8, 4);
}

static LLVMValueRef load_sample_mask_in(struct ac_shader_abi *abi)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   return ac_to_integer(&ctx->ac, ac_get_arg(&ctx->ac, ctx->args.sample_coverage));
}

static LLVMValueRef load_sample_position(struct ac_shader_abi *abi, LLVMValueRef sample_id)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   LLVMValueRef desc = ac_get_arg(&ctx->ac, ctx->internal_bindings);
   LLVMValueRef buf_index = LLVMConstInt(ctx->ac.i32, SI_PS_CONST_SAMPLE_POSITIONS, 0);
   LLVMValueRef resource = ac_build_load_to_sgpr(&ctx->ac, desc, buf_index);

   /* offset = sample_id * 8  (8 = 2 floats containing samplepos.xy) */
   LLVMValueRef offset0 =
      LLVMBuildMul(ctx->ac.builder, sample_id, LLVMConstInt(ctx->ac.i32, 8, 0), "");
   LLVMValueRef offset1 =
      LLVMBuildAdd(ctx->ac.builder, offset0, LLVMConstInt(ctx->ac.i32, 4, 0), "");

   LLVMValueRef pos[4] = {si_buffer_load_const(ctx, resource, offset0),
                          si_buffer_load_const(ctx, resource, offset1),
                          LLVMConstReal(ctx->ac.f32, 0), LLVMConstReal(ctx->ac.f32, 0)};

   return ac_build_gather_values(&ctx->ac, pos, 4);
}

static LLVMValueRef si_nir_emit_fbfetch(struct ac_shader_abi *abi)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct ac_image_args args = {};
   LLVMValueRef ptr, image, fmask;

   /* Ignore src0, because KHR_blend_func_extended disallows multiple render
    * targets.
    */

   /* Load the image descriptor. */
   STATIC_ASSERT(SI_PS_IMAGE_COLORBUF0 % 2 == 0);
   ptr = ac_get_arg(&ctx->ac, ctx->internal_bindings);
   ptr =
      LLVMBuildPointerCast(ctx->ac.builder, ptr, ac_array_in_const32_addr_space(ctx->ac.v8i32), "");
   image =
      ac_build_load_to_sgpr(&ctx->ac, ptr, LLVMConstInt(ctx->ac.i32, SI_PS_IMAGE_COLORBUF0 / 2, 0));

   unsigned chan = 0;

   args.coords[chan++] = si_unpack_param(ctx, ctx->pos_fixed_pt, 0, 16);

   if (!ctx->shader->key.mono.u.ps.fbfetch_is_1D)
      args.coords[chan++] = si_unpack_param(ctx, ctx->pos_fixed_pt, 16, 16);

   /* Get the current render target layer index. */
   if (ctx->shader->key.mono.u.ps.fbfetch_layered)
      args.coords[chan++] = si_unpack_param(ctx, ctx->args.ancillary, 16, 11);

   if (ctx->shader->key.mono.u.ps.fbfetch_msaa)
      args.coords[chan++] = si_get_sample_id(ctx);

   if (ctx->shader->key.mono.u.ps.fbfetch_msaa && !(ctx->screen->debug_flags & DBG(NO_FMASK))) {
      fmask = ac_build_load_to_sgpr(&ctx->ac, ptr,
                                    LLVMConstInt(ctx->ac.i32, SI_PS_IMAGE_COLORBUF0_FMASK / 2, 0));

      ac_apply_fmask_to_sample(&ctx->ac, fmask, args.coords,
                               ctx->shader->key.mono.u.ps.fbfetch_layered);
   }

   args.opcode = ac_image_load;
   args.resource = image;
   args.dmask = 0xf;
   args.attributes = AC_FUNC_ATTR_READNONE;

   if (ctx->shader->key.mono.u.ps.fbfetch_msaa)
      args.dim =
         ctx->shader->key.mono.u.ps.fbfetch_layered ? ac_image_2darraymsaa : ac_image_2dmsaa;
   else if (ctx->shader->key.mono.u.ps.fbfetch_is_1D)
      args.dim = ctx->shader->key.mono.u.ps.fbfetch_layered ? ac_image_1darray : ac_image_1d;
   else
      args.dim = ctx->shader->key.mono.u.ps.fbfetch_layered ? ac_image_2darray : ac_image_2d;

   return ac_build_image_opcode(&ctx->ac, &args);
}

static LLVMValueRef si_build_fs_interp(struct si_shader_context *ctx, unsigned attr_index,
                                       unsigned chan, LLVMValueRef prim_mask, LLVMValueRef i,
                                       LLVMValueRef j)
{
   if (i || j) {
      return ac_build_fs_interp(&ctx->ac, LLVMConstInt(ctx->ac.i32, chan, 0),
                                LLVMConstInt(ctx->ac.i32, attr_index, 0), prim_mask, i, j);
   }
   return ac_build_fs_interp_mov(&ctx->ac, LLVMConstInt(ctx->ac.i32, 2, 0), /* P0 */
                                 LLVMConstInt(ctx->ac.i32, chan, 0),
                                 LLVMConstInt(ctx->ac.i32, attr_index, 0), prim_mask);
}

/**
 * Interpolate a fragment shader input.
 *
 * @param ctx		context
 * @param input_index		index of the input in hardware
 * @param semantic_index	semantic index
 * @param num_interp_inputs	number of all interpolated inputs (= BCOLOR offset)
 * @param colors_read_mask	color components read (4 bits for each color, 8 bits in total)
 * @param interp_param		interpolation weights (i,j)
 * @param prim_mask		SI_PARAM_PRIM_MASK
 * @param face			SI_PARAM_FRONT_FACE
 * @param result		the return value (4 components)
 */
static void interp_fs_color(struct si_shader_context *ctx, unsigned input_index,
                            unsigned semantic_index, unsigned num_interp_inputs,
                            unsigned colors_read_mask, LLVMValueRef interp_param,
                            LLVMValueRef prim_mask, LLVMValueRef face, LLVMValueRef result[4])
{
   LLVMValueRef i = NULL, j = NULL;
   unsigned chan;

   /* fs.constant returns the param from the middle vertex, so it's not
    * really useful for flat shading. It's meant to be used for custom
    * interpolation (but the intrinsic can't fetch from the other two
    * vertices).
    *
    * Luckily, it doesn't matter, because we rely on the FLAT_SHADE state
    * to do the right thing. The only reason we use fs.constant is that
    * fs.interp cannot be used on integers, because they can be equal
    * to NaN.
    *
    * When interp is false we will use fs.constant or for newer llvm,
    * amdgcn.interp.mov.
    */
   bool interp = interp_param != NULL;

   if (interp) {
      interp_param =
         LLVMBuildBitCast(ctx->ac.builder, interp_param, ctx->ac.v2f32, "");

      i = LLVMBuildExtractElement(ctx->ac.builder, interp_param, ctx->ac.i32_0, "");
      j = LLVMBuildExtractElement(ctx->ac.builder, interp_param, ctx->ac.i32_1, "");
   }

   if (ctx->shader->key.part.ps.prolog.color_two_side) {
      LLVMValueRef is_face_positive;

      /* If BCOLOR0 is used, BCOLOR1 is at offset "num_inputs + 1",
       * otherwise it's at offset "num_inputs".
       */
      unsigned back_attr_offset = num_interp_inputs;
      if (semantic_index == 1 && colors_read_mask & 0xf)
         back_attr_offset += 1;

      is_face_positive = LLVMBuildICmp(ctx->ac.builder, LLVMIntNE, face, ctx->ac.i32_0, "");

      for (chan = 0; chan < 4; chan++) {
         LLVMValueRef front, back;

         front = si_build_fs_interp(ctx, input_index, chan, prim_mask, i, j);
         back = si_build_fs_interp(ctx, back_attr_offset, chan, prim_mask, i, j);

         result[chan] = LLVMBuildSelect(ctx->ac.builder, is_face_positive, front, back, "");
      }
   } else {
      for (chan = 0; chan < 4; chan++) {
         result[chan] = si_build_fs_interp(ctx, input_index, chan, prim_mask, i, j);
      }
   }
}

static void si_alpha_test(struct si_shader_context *ctx, LLVMValueRef alpha)
{
   if (ctx->shader->key.part.ps.epilog.alpha_func != PIPE_FUNC_NEVER) {
      static LLVMRealPredicate cond_map[PIPE_FUNC_ALWAYS + 1] = {
         [PIPE_FUNC_LESS] = LLVMRealOLT,     [PIPE_FUNC_EQUAL] = LLVMRealOEQ,
         [PIPE_FUNC_LEQUAL] = LLVMRealOLE,   [PIPE_FUNC_GREATER] = LLVMRealOGT,
         [PIPE_FUNC_NOTEQUAL] = LLVMRealONE, [PIPE_FUNC_GEQUAL] = LLVMRealOGE,
      };
      LLVMRealPredicate cond = cond_map[ctx->shader->key.part.ps.epilog.alpha_func];
      assert(cond);

      LLVMValueRef alpha_ref = LLVMGetParam(ctx->main_fn, SI_PARAM_ALPHA_REF);
      if (LLVMTypeOf(alpha) == ctx->ac.f16)
         alpha_ref = LLVMBuildFPTrunc(ctx->ac.builder, alpha_ref, ctx->ac.f16, "");

      LLVMValueRef alpha_pass = LLVMBuildFCmp(ctx->ac.builder, cond, alpha, alpha_ref, "");
      ac_build_kill_if_false(&ctx->ac, alpha_pass);
   } else {
      ac_build_kill_if_false(&ctx->ac, ctx->ac.i1false);
   }
}

static LLVMValueRef si_scale_alpha_by_sample_mask(struct si_shader_context *ctx, LLVMValueRef alpha,
                                                  unsigned samplemask_param)
{
   LLVMValueRef coverage;

   /* alpha = alpha * popcount(coverage) / SI_NUM_SMOOTH_AA_SAMPLES */
   coverage = LLVMGetParam(ctx->main_fn, samplemask_param);
   coverage = ac_build_bit_count(&ctx->ac, ac_to_integer(&ctx->ac, coverage));
   coverage = LLVMBuildUIToFP(ctx->ac.builder, coverage, ctx->ac.f32, "");

   coverage = LLVMBuildFMul(ctx->ac.builder, coverage,
                            LLVMConstReal(ctx->ac.f32, 1.0 / SI_NUM_SMOOTH_AA_SAMPLES), "");

   if (LLVMTypeOf(alpha) == ctx->ac.f16)
      coverage = LLVMBuildFPTrunc(ctx->ac.builder, coverage, ctx->ac.f16, "");

   return LLVMBuildFMul(ctx->ac.builder, alpha, coverage, "");
}

struct si_ps_exports {
   unsigned num;
   struct ac_export_args args[10];
};

static LLVMValueRef pack_two_16bit(struct ac_llvm_context *ctx, LLVMValueRef args[2])
{
   LLVMValueRef tmp = ac_build_gather_values(ctx, args, 2);
   return LLVMBuildBitCast(ctx->builder, tmp, ctx->v2f16, "");
}

static LLVMValueRef get_color_32bit(struct si_shader_context *ctx, unsigned color_type,
                                    LLVMValueRef value)
{
   switch (color_type) {
   case SI_TYPE_FLOAT16:
      return LLVMBuildFPExt(ctx->ac.builder, value, ctx->ac.f32, "");
   case SI_TYPE_INT16:
      value = ac_to_integer(&ctx->ac, value);
      value = LLVMBuildSExt(ctx->ac.builder, value, ctx->ac.i32, "");
      return ac_to_float(&ctx->ac, value);
   case SI_TYPE_UINT16:
      value = ac_to_integer(&ctx->ac, value);
      value = LLVMBuildZExt(ctx->ac.builder, value, ctx->ac.i32, "");
      return ac_to_float(&ctx->ac, value);
   case SI_TYPE_ANY32:
      return value;
   }
   return NULL;
}

/* Initialize arguments for the shader export intrinsic */
static void si_llvm_init_ps_export_args(struct si_shader_context *ctx, LLVMValueRef *values,
                                        unsigned cbuf, unsigned compacted_mrt_index,
                                        unsigned color_type, struct ac_export_args *args)
{
   const struct si_shader_key *key = &ctx->shader->key;
   unsigned col_formats = key->part.ps.epilog.spi_shader_col_format;
   LLVMValueRef f32undef = LLVMGetUndef(ctx->ac.f32);
   unsigned spi_shader_col_format;
   unsigned chan;
   bool is_int8, is_int10;

   assert(cbuf < 8);

   spi_shader_col_format = (col_formats >> (cbuf * 4)) & 0xf;
   is_int8 = (key->part.ps.epilog.color_is_int8 >> cbuf) & 0x1;
   is_int10 = (key->part.ps.epilog.color_is_int10 >> cbuf) & 0x1;

   /* Default is 0xf. Adjusted below depending on the format. */
   args->enabled_channels = 0xf; /* writemask */

   /* Specify whether the EXEC mask represents the valid mask */
   args->valid_mask = 0;

   /* Specify whether this is the last export */
   args->done = 0;

   /* Specify the target we are exporting */
   args->target = V_008DFC_SQ_EXP_MRT + compacted_mrt_index;

   args->compr = false;
   args->out[0] = f32undef;
   args->out[1] = f32undef;
   args->out[2] = f32undef;
   args->out[3] = f32undef;

   LLVMValueRef (*packf)(struct ac_llvm_context * ctx, LLVMValueRef args[2]) = NULL;
   LLVMValueRef (*packi)(struct ac_llvm_context * ctx, LLVMValueRef args[2], unsigned bits,
                         bool hi) = NULL;

   switch (spi_shader_col_format) {
   case V_028714_SPI_SHADER_ZERO:
      args->enabled_channels = 0; /* writemask */
      args->target = V_008DFC_SQ_EXP_NULL;
      break;

   case V_028714_SPI_SHADER_32_R:
      args->enabled_channels = 1; /* writemask */
      args->out[0] = get_color_32bit(ctx, color_type, values[0]);
      break;

   case V_028714_SPI_SHADER_32_GR:
      args->enabled_channels = 0x3; /* writemask */
      args->out[0] = get_color_32bit(ctx, color_type, values[0]);
      args->out[1] = get_color_32bit(ctx, color_type, values[1]);
      break;

   case V_028714_SPI_SHADER_32_AR:
      if (ctx->screen->info.chip_class >= GFX10) {
         args->enabled_channels = 0x3; /* writemask */
         args->out[0] = get_color_32bit(ctx, color_type, values[0]);
         args->out[1] = get_color_32bit(ctx, color_type, values[3]);
      } else {
         args->enabled_channels = 0x9; /* writemask */
         args->out[0] = get_color_32bit(ctx, color_type, values[0]);
         args->out[3] = get_color_32bit(ctx, color_type, values[3]);
      }
      break;

   case V_028714_SPI_SHADER_FP16_ABGR:
      if (color_type != SI_TYPE_ANY32)
         packf = pack_two_16bit;
      else
         packf = ac_build_cvt_pkrtz_f16;
      break;

   case V_028714_SPI_SHADER_UNORM16_ABGR:
      if (color_type != SI_TYPE_ANY32)
         packf = ac_build_cvt_pknorm_u16_f16;
      else
         packf = ac_build_cvt_pknorm_u16;
      break;

   case V_028714_SPI_SHADER_SNORM16_ABGR:
      if (color_type != SI_TYPE_ANY32)
         packf = ac_build_cvt_pknorm_i16_f16;
      else
         packf = ac_build_cvt_pknorm_i16;
      break;

   case V_028714_SPI_SHADER_UINT16_ABGR:
      if (color_type != SI_TYPE_ANY32)
         packf = pack_two_16bit;
      else
         packi = ac_build_cvt_pk_u16;
      break;

   case V_028714_SPI_SHADER_SINT16_ABGR:
      if (color_type != SI_TYPE_ANY32)
         packf = pack_two_16bit;
      else
         packi = ac_build_cvt_pk_i16;
      break;

   case V_028714_SPI_SHADER_32_ABGR:
      for (unsigned i = 0; i < 4; i++)
         args->out[i] = get_color_32bit(ctx, color_type, values[i]);
      break;
   }

   /* Pack f16 or norm_i16/u16. */
   if (packf) {
      for (chan = 0; chan < 2; chan++) {
         LLVMValueRef pack_args[2] = {values[2 * chan], values[2 * chan + 1]};
         LLVMValueRef packed;

         packed = packf(&ctx->ac, pack_args);
         args->out[chan] = ac_to_float(&ctx->ac, packed);
      }
      args->compr = 1; /* COMPR flag */
   }
   /* Pack i16/u16. */
   if (packi) {
      for (chan = 0; chan < 2; chan++) {
         LLVMValueRef pack_args[2] = {ac_to_integer(&ctx->ac, values[2 * chan]),
                                      ac_to_integer(&ctx->ac, values[2 * chan + 1])};
         LLVMValueRef packed;

         packed = packi(&ctx->ac, pack_args, is_int8 ? 8 : is_int10 ? 10 : 16, chan == 1);
         args->out[chan] = ac_to_float(&ctx->ac, packed);
      }
      args->compr = 1; /* COMPR flag */
   }
}

static bool si_export_mrt_color(struct si_shader_context *ctx, LLVMValueRef *color, unsigned index,
                                unsigned compacted_mrt_index, unsigned samplemask_param,
                                bool is_last, unsigned color_type, struct si_ps_exports *exp)
{
   int i;

   /* Clamp color */
   if (ctx->shader->key.part.ps.epilog.clamp_color)
      for (i = 0; i < 4; i++)
         color[i] = ac_build_clamp(&ctx->ac, color[i]);

   /* Alpha to one */
   if (ctx->shader->key.part.ps.epilog.alpha_to_one)
      color[3] = LLVMConstReal(LLVMTypeOf(color[0]), 1);

   /* Alpha test */
   if (index == 0 && ctx->shader->key.part.ps.epilog.alpha_func != PIPE_FUNC_ALWAYS)
      si_alpha_test(ctx, color[3]);

   /* Line & polygon smoothing */
   if (ctx->shader->key.part.ps.epilog.poly_line_smoothing)
      color[3] = si_scale_alpha_by_sample_mask(ctx, color[3], samplemask_param);

   /* If last_cbuf > 0, FS_COLOR0_WRITES_ALL_CBUFS is true. */
   if (ctx->shader->key.part.ps.epilog.last_cbuf > 0) {
      struct ac_export_args args[8];
      int c, last = -1;

      assert(compacted_mrt_index == 0);

      /* Get the export arguments, also find out what the last one is. */
      for (c = 0; c <= ctx->shader->key.part.ps.epilog.last_cbuf; c++) {
         si_llvm_init_ps_export_args(ctx, color, c, compacted_mrt_index,
                                     color_type, &args[c]);
         if (args[c].enabled_channels) {
            compacted_mrt_index++;
            last = c;
         }
      }
      if (last == -1)
         return false;

      /* Emit all exports. */
      for (c = 0; c <= ctx->shader->key.part.ps.epilog.last_cbuf; c++) {
         if (is_last && last == c) {
            args[c].valid_mask = 1; /* whether the EXEC mask is valid */
            args[c].done = 1;       /* DONE bit */
         } else if (!args[c].enabled_channels)
            continue; /* unnecessary NULL export */

         memcpy(&exp->args[exp->num++], &args[c], sizeof(args[c]));
      }
   } else {
      struct ac_export_args args;

      /* Export */
      si_llvm_init_ps_export_args(ctx, color, index, compacted_mrt_index,
                                  color_type, &args);
      if (is_last) {
         args.valid_mask = 1; /* whether the EXEC mask is valid */
         args.done = 1;       /* DONE bit */
      } else if (!args.enabled_channels)
         return false; /* unnecessary NULL export */

      memcpy(&exp->args[exp->num++], &args, sizeof(args));
   }
   return true;
}

/**
 * Return PS outputs in this order:
 *
 * v[0:3] = color0.xyzw
 * v[4:7] = color1.xyzw
 * ...
 * vN+0 = Depth
 * vN+1 = Stencil
 * vN+2 = SampleMask
 * vN+3 = SampleMaskIn (used for OpenGL smoothing)
 *
 * The alpha-ref SGPR is returned via its original location.
 */
static void si_llvm_return_fs_outputs(struct ac_shader_abi *abi)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   struct si_shader *shader = ctx->shader;
   struct si_shader_info *info = &shader->selector->info;
   LLVMBuilderRef builder = ctx->ac.builder;
   unsigned i, j, first_vgpr, vgpr;
   LLVMValueRef *addrs = abi->outputs;

   LLVMValueRef color[8][4] = {};
   LLVMValueRef depth = NULL, stencil = NULL, samplemask = NULL;
   LLVMValueRef ret;

   /* Read the output values. */
   for (i = 0; i < info->num_outputs; i++) {
      unsigned semantic = info->output_semantic[i];

      switch (semantic) {
      case FRAG_RESULT_DEPTH:
         depth = LLVMBuildLoad(builder, addrs[4 * i + 0], "");
         break;
      case FRAG_RESULT_STENCIL:
         stencil = LLVMBuildLoad(builder, addrs[4 * i + 0], "");
         break;
      case FRAG_RESULT_SAMPLE_MASK:
         samplemask = LLVMBuildLoad(builder, addrs[4 * i + 0], "");
         break;
      default:
         if (semantic >= FRAG_RESULT_DATA0 && semantic <= FRAG_RESULT_DATA7) {
            unsigned index = semantic - FRAG_RESULT_DATA0;

            for (j = 0; j < 4; j++) {
               LLVMValueRef ptr = addrs[4 * i + j];
               LLVMValueRef result = LLVMBuildLoad(builder, ptr, "");
               color[index][j] = result;
            }
         } else {
            fprintf(stderr, "Warning: Unhandled fs output type:%d\n", semantic);
         }
         break;
      }
   }

   /* Fill the return structure. */
   ret = ctx->return_value;

   /* Set SGPRs. */
   ret = LLVMBuildInsertValue(
      builder, ret, ac_to_integer(&ctx->ac, LLVMGetParam(ctx->main_fn, SI_PARAM_ALPHA_REF)),
      SI_SGPR_ALPHA_REF, "");

   /* Set VGPRs */
   first_vgpr = vgpr = SI_SGPR_ALPHA_REF + 1;
   for (i = 0; i < ARRAY_SIZE(color); i++) {
      if (!color[i][0])
         continue;

      if (LLVMTypeOf(color[i][0]) == ctx->ac.f16) {
         for (j = 0; j < 2; j++) {
            LLVMValueRef tmp = ac_build_gather_values(&ctx->ac, &color[i][j * 2], 2);
            tmp = LLVMBuildBitCast(builder, tmp, ctx->ac.f32, "");
            ret = LLVMBuildInsertValue(builder, ret, tmp, vgpr++, "");
         }
         vgpr += 2;
      } else {
         for (j = 0; j < 4; j++)
            ret = LLVMBuildInsertValue(builder, ret, color[i][j], vgpr++, "");
      }
   }
   if (depth)
      ret = LLVMBuildInsertValue(builder, ret, depth, vgpr++, "");
   if (stencil)
      ret = LLVMBuildInsertValue(builder, ret, stencil, vgpr++, "");
   if (samplemask)
      ret = LLVMBuildInsertValue(builder, ret, samplemask, vgpr++, "");

   /* Add the input sample mask for smoothing at the end. */
   if (vgpr < first_vgpr + PS_EPILOG_SAMPLEMASK_MIN_LOC)
      vgpr = first_vgpr + PS_EPILOG_SAMPLEMASK_MIN_LOC;
   ret = LLVMBuildInsertValue(builder, ret, LLVMGetParam(ctx->main_fn, SI_PARAM_SAMPLE_COVERAGE),
                              vgpr++, "");

   ctx->return_value = ret;
}

static void si_llvm_emit_polygon_stipple(struct si_shader_context *ctx,
                                         LLVMValueRef param_internal_bindings,
                                         struct ac_arg param_pos_fixed_pt)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef slot, desc, offset, row, bit, address[2];

   /* Use the fixed-point gl_FragCoord input.
    * Since the stipple pattern is 32x32 and it repeats, just get 5 bits
    * per coordinate to get the repeating effect.
    */
   address[0] = si_unpack_param(ctx, param_pos_fixed_pt, 0, 5);
   address[1] = si_unpack_param(ctx, param_pos_fixed_pt, 16, 5);

   /* Load the buffer descriptor. */
   slot = LLVMConstInt(ctx->ac.i32, SI_PS_CONST_POLY_STIPPLE, 0);
   desc = ac_build_load_to_sgpr(&ctx->ac, param_internal_bindings, slot);

   /* The stipple pattern is 32x32, each row has 32 bits. */
   offset = LLVMBuildMul(builder, address[1], LLVMConstInt(ctx->ac.i32, 4, 0), "");
   row = si_buffer_load_const(ctx, desc, offset);
   row = ac_to_integer(&ctx->ac, row);
   bit = LLVMBuildLShr(builder, row, address[0], "");
   bit = LLVMBuildTrunc(builder, bit, ctx->ac.i1, "");
   ac_build_kill_if_false(&ctx->ac, bit);
}

/**
 * Build the pixel shader prolog function. This handles:
 * - two-side color selection and interpolation
 * - overriding interpolation parameters for the API PS
 * - polygon stippling
 *
 * All preloaded SGPRs and VGPRs are passed through unmodified unless they are
 * overriden by other states. (e.g. per-sample interpolation)
 * Interpolated colors are stored after the preloaded VGPRs.
 */
void si_llvm_build_ps_prolog(struct si_shader_context *ctx, union si_shader_part_key *key)
{
   LLVMValueRef ret, func;
   int num_returns, i, num_color_channels;

   memset(&ctx->args, 0, sizeof(ctx->args));

   /* Declare inputs. */
   LLVMTypeRef return_types[AC_MAX_ARGS];
   num_returns = 0;
   num_color_channels = util_bitcount(key->ps_prolog.colors_read);
   assert(key->ps_prolog.num_input_sgprs + key->ps_prolog.num_input_vgprs + num_color_channels <=
          AC_MAX_ARGS);
   for (i = 0; i < key->ps_prolog.num_input_sgprs; i++) {
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      return_types[num_returns++] = ctx->ac.i32;
   }

   struct ac_arg pos_fixed_pt;
   struct ac_arg ancillary;
   struct ac_arg param_sample_mask;
   for (i = 0; i < key->ps_prolog.num_input_vgprs; i++) {
      struct ac_arg *arg = NULL;
      if (i == key->ps_prolog.ancillary_vgpr_index) {
         arg = &ancillary;
      } else if (i == key->ps_prolog.ancillary_vgpr_index + 1) {
         arg = &param_sample_mask;
      } else if (i == key->ps_prolog.num_input_vgprs - 1) {
         /* POS_FIXED_PT is always last. */
         arg = &pos_fixed_pt;
      }
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_FLOAT, arg);
      return_types[num_returns++] = ctx->ac.f32;
   }

   /* Declare outputs (same as inputs + add colors if needed) */
   for (i = 0; i < num_color_channels; i++)
      return_types[num_returns++] = ctx->ac.f32;

   /* Create the function. */
   si_llvm_create_func(ctx, "ps_prolog", return_types, num_returns, 0);
   func = ctx->main_fn;

   /* Copy inputs to outputs. This should be no-op, as the registers match,
    * but it will prevent the compiler from overwriting them unintentionally.
    */
   ret = ctx->return_value;
   for (i = 0; i < ctx->args.arg_count; i++) {
      LLVMValueRef p = LLVMGetParam(func, i);
      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, p, i, "");
   }

   /* Polygon stippling. */
   if (key->ps_prolog.states.poly_stipple) {
      LLVMValueRef list = si_prolog_get_internal_bindings(ctx);

      si_llvm_emit_polygon_stipple(ctx, list, pos_fixed_pt);
   }

   if (key->ps_prolog.states.bc_optimize_for_persp ||
       key->ps_prolog.states.bc_optimize_for_linear) {
      unsigned i, base = key->ps_prolog.num_input_sgprs;
      LLVMValueRef center[2], centroid[2], tmp, bc_optimize;

      /* The shader should do: if (PRIM_MASK[31]) CENTROID = CENTER;
       * The hw doesn't compute CENTROID if the whole wave only
       * contains fully-covered quads.
       *
       * PRIM_MASK is after user SGPRs.
       */
      bc_optimize = LLVMGetParam(func, SI_PS_NUM_USER_SGPR);
      bc_optimize =
         LLVMBuildLShr(ctx->ac.builder, bc_optimize, LLVMConstInt(ctx->ac.i32, 31, 0), "");
      bc_optimize = LLVMBuildTrunc(ctx->ac.builder, bc_optimize, ctx->ac.i1, "");

      if (key->ps_prolog.states.bc_optimize_for_persp) {
         /* Read PERSP_CENTER. */
         for (i = 0; i < 2; i++)
            center[i] = LLVMGetParam(func, base + 2 + i);
         /* Read PERSP_CENTROID. */
         for (i = 0; i < 2; i++)
            centroid[i] = LLVMGetParam(func, base + 4 + i);
         /* Select PERSP_CENTROID. */
         for (i = 0; i < 2; i++) {
            tmp = LLVMBuildSelect(ctx->ac.builder, bc_optimize, center[i], centroid[i], "");
            ret = LLVMBuildInsertValue(ctx->ac.builder, ret, tmp, base + 4 + i, "");
         }
      }
      if (key->ps_prolog.states.bc_optimize_for_linear) {
         /* Read LINEAR_CENTER. */
         for (i = 0; i < 2; i++)
            center[i] = LLVMGetParam(func, base + 8 + i);
         /* Read LINEAR_CENTROID. */
         for (i = 0; i < 2; i++)
            centroid[i] = LLVMGetParam(func, base + 10 + i);
         /* Select LINEAR_CENTROID. */
         for (i = 0; i < 2; i++) {
            tmp = LLVMBuildSelect(ctx->ac.builder, bc_optimize, center[i], centroid[i], "");
            ret = LLVMBuildInsertValue(ctx->ac.builder, ret, tmp, base + 10 + i, "");
         }
      }
   }

   /* Force per-sample interpolation. */
   if (key->ps_prolog.states.force_persp_sample_interp) {
      unsigned i, base = key->ps_prolog.num_input_sgprs;
      LLVMValueRef persp_sample[2];

      /* Read PERSP_SAMPLE. */
      for (i = 0; i < 2; i++)
         persp_sample[i] = LLVMGetParam(func, base + i);
      /* Overwrite PERSP_CENTER. */
      for (i = 0; i < 2; i++)
         ret = LLVMBuildInsertValue(ctx->ac.builder, ret, persp_sample[i], base + 2 + i, "");
      /* Overwrite PERSP_CENTROID. */
      for (i = 0; i < 2; i++)
         ret = LLVMBuildInsertValue(ctx->ac.builder, ret, persp_sample[i], base + 4 + i, "");
   }
   if (key->ps_prolog.states.force_linear_sample_interp) {
      unsigned i, base = key->ps_prolog.num_input_sgprs;
      LLVMValueRef linear_sample[2];

      /* Read LINEAR_SAMPLE. */
      for (i = 0; i < 2; i++)
         linear_sample[i] = LLVMGetParam(func, base + 6 + i);
      /* Overwrite LINEAR_CENTER. */
      for (i = 0; i < 2; i++)
         ret = LLVMBuildInsertValue(ctx->ac.builder, ret, linear_sample[i], base + 8 + i, "");
      /* Overwrite LINEAR_CENTROID. */
      for (i = 0; i < 2; i++)
         ret = LLVMBuildInsertValue(ctx->ac.builder, ret, linear_sample[i], base + 10 + i, "");
   }

   /* Force center interpolation. */
   if (key->ps_prolog.states.force_persp_center_interp) {
      unsigned i, base = key->ps_prolog.num_input_sgprs;
      LLVMValueRef persp_center[2];

      /* Read PERSP_CENTER. */
      for (i = 0; i < 2; i++)
         persp_center[i] = LLVMGetParam(func, base + 2 + i);
      /* Overwrite PERSP_SAMPLE. */
      for (i = 0; i < 2; i++)
         ret = LLVMBuildInsertValue(ctx->ac.builder, ret, persp_center[i], base + i, "");
      /* Overwrite PERSP_CENTROID. */
      for (i = 0; i < 2; i++)
         ret = LLVMBuildInsertValue(ctx->ac.builder, ret, persp_center[i], base + 4 + i, "");
   }
   if (key->ps_prolog.states.force_linear_center_interp) {
      unsigned i, base = key->ps_prolog.num_input_sgprs;
      LLVMValueRef linear_center[2];

      /* Read LINEAR_CENTER. */
      for (i = 0; i < 2; i++)
         linear_center[i] = LLVMGetParam(func, base + 8 + i);
      /* Overwrite LINEAR_SAMPLE. */
      for (i = 0; i < 2; i++)
         ret = LLVMBuildInsertValue(ctx->ac.builder, ret, linear_center[i], base + 6 + i, "");
      /* Overwrite LINEAR_CENTROID. */
      for (i = 0; i < 2; i++)
         ret = LLVMBuildInsertValue(ctx->ac.builder, ret, linear_center[i], base + 10 + i, "");
   }

   /* Interpolate colors. */
   unsigned color_out_idx = 0;
   for (i = 0; i < 2; i++) {
      unsigned writemask = (key->ps_prolog.colors_read >> (i * 4)) & 0xf;
      unsigned face_vgpr = key->ps_prolog.num_input_sgprs + key->ps_prolog.face_vgpr_index;
      LLVMValueRef interp[2], color[4];
      LLVMValueRef interp_ij = NULL, prim_mask = NULL, face = NULL;

      if (!writemask)
         continue;

      /* If the interpolation qualifier is not CONSTANT (-1). */
      if (key->ps_prolog.color_interp_vgpr_index[i] != -1) {
         unsigned interp_vgpr =
            key->ps_prolog.num_input_sgprs + key->ps_prolog.color_interp_vgpr_index[i];

         /* Get the (i,j) updated by bc_optimize handling. */
         interp[0] = LLVMBuildExtractValue(ctx->ac.builder, ret, interp_vgpr, "");
         interp[1] = LLVMBuildExtractValue(ctx->ac.builder, ret, interp_vgpr + 1, "");
         interp_ij = ac_build_gather_values(&ctx->ac, interp, 2);
      }

      /* Use the absolute location of the input. */
      prim_mask = LLVMGetParam(func, SI_PS_NUM_USER_SGPR);

      if (key->ps_prolog.states.color_two_side) {
         face = LLVMGetParam(func, face_vgpr);
         face = ac_to_integer(&ctx->ac, face);
      }

      interp_fs_color(ctx, key->ps_prolog.color_attr_index[i], i, key->ps_prolog.num_interp_inputs,
                      key->ps_prolog.colors_read, interp_ij, prim_mask, face, color);

      while (writemask) {
         unsigned chan = u_bit_scan(&writemask);
         ret = LLVMBuildInsertValue(ctx->ac.builder, ret, color[chan],
                                    ctx->args.arg_count + color_out_idx++, "");
      }
   }

   /* Section 15.2.2 (Shader Inputs) of the OpenGL 4.5 (Core Profile) spec
    * says:
    *
    *    "When per-sample shading is active due to the use of a fragment
    *     input qualified by sample or due to the use of the gl_SampleID
    *     or gl_SamplePosition variables, only the bit for the current
    *     sample is set in gl_SampleMaskIn. When state specifies multiple
    *     fragment shader invocations for a given fragment, the sample
    *     mask for any single fragment shader invocation may specify a
    *     subset of the covered samples for the fragment. In this case,
    *     the bit corresponding to each covered sample will be set in
    *     exactly one fragment shader invocation."
    *
    * The samplemask loaded by hardware is always the coverage of the
    * entire pixel/fragment, so mask bits out based on the sample ID.
    */
   if (key->ps_prolog.states.samplemask_log_ps_iter) {
      /* The bit pattern matches that used by fixed function fragment
       * processing. */
      static const uint16_t ps_iter_masks[] = {
         0xffff, /* not used */
         0x5555, 0x1111, 0x0101, 0x0001,
      };
      assert(key->ps_prolog.states.samplemask_log_ps_iter < ARRAY_SIZE(ps_iter_masks));

      uint32_t ps_iter_mask = ps_iter_masks[key->ps_prolog.states.samplemask_log_ps_iter];
      LLVMValueRef sampleid = si_unpack_param(ctx, ancillary, 8, 4);
      LLVMValueRef samplemask = ac_get_arg(&ctx->ac, param_sample_mask);

      samplemask = ac_to_integer(&ctx->ac, samplemask);
      samplemask =
         LLVMBuildAnd(ctx->ac.builder, samplemask,
                      LLVMBuildShl(ctx->ac.builder, LLVMConstInt(ctx->ac.i32, ps_iter_mask, false),
                                   sampleid, ""),
                      "");
      samplemask = ac_to_float(&ctx->ac, samplemask);

      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, samplemask, param_sample_mask.arg_index, "");
   }

   /* Tell LLVM to insert WQM instruction sequence when needed. */
   if (key->ps_prolog.wqm) {
      LLVMAddTargetDependentFunctionAttr(func, "amdgpu-ps-wqm-outputs", "");
   }

   si_llvm_build_ret(ctx, ret);
}

/**
 * Build the pixel shader epilog function. This handles everything that must be
 * emulated for pixel shader exports. (alpha-test, format conversions, etc)
 */
void si_llvm_build_ps_epilog(struct si_shader_context *ctx, union si_shader_part_key *key)
{
   LLVMValueRef depth = NULL, stencil = NULL, samplemask = NULL;
   int i;
   struct si_ps_exports exp = {};

   memset(&ctx->args, 0, sizeof(ctx->args));

   /* Declare input SGPRs. */
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->internal_bindings);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->bindless_samplers_and_images);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->const_and_shader_buffers);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->samplers_and_images);
   si_add_arg_checked(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_FLOAT, NULL, SI_PARAM_ALPHA_REF);

   /* Declare input VGPRs. */
   unsigned required_num_params =
      ctx->args.num_sgprs_used + util_bitcount(key->ps_epilog.colors_written) * 4 +
      key->ps_epilog.writes_z + key->ps_epilog.writes_stencil + key->ps_epilog.writes_samplemask;

   required_num_params =
      MAX2(required_num_params, ctx->args.num_sgprs_used + PS_EPILOG_SAMPLEMASK_MIN_LOC + 1);

   while (ctx->args.arg_count < required_num_params)
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_FLOAT, NULL);

   /* Create the function. */
   si_llvm_create_func(ctx, "ps_epilog", NULL, 0, 0);
   /* Disable elimination of unused inputs. */
   ac_llvm_add_target_dep_function_attr(ctx->main_fn, "InitialPSInputAddr", 0xffffff);

   /* Process colors. */
   unsigned vgpr = ctx->args.num_sgprs_used;
   unsigned colors_written = key->ps_epilog.colors_written;
   int last_color_export = -1;

   /* Find the last color export. */
   if (!key->ps_epilog.writes_z && !key->ps_epilog.writes_stencil &&
       !key->ps_epilog.writes_samplemask) {
      unsigned spi_format = key->ps_epilog.states.spi_shader_col_format;

      /* If last_cbuf > 0, FS_COLOR0_WRITES_ALL_CBUFS is true. */
      if (colors_written == 0x1 && key->ps_epilog.states.last_cbuf > 0) {
         /* Just set this if any of the colorbuffers are enabled. */
         if (spi_format & ((1ull << (4 * (key->ps_epilog.states.last_cbuf + 1))) - 1))
            last_color_export = 0;
      } else {
         for (i = 0; i < 8; i++)
            if (colors_written & (1 << i) && (spi_format >> (i * 4)) & 0xf)
               last_color_export = i;
      }
   }

   unsigned num_compacted_mrts = 0;
   while (colors_written) {
      LLVMValueRef color[4];
      int output_index = u_bit_scan(&colors_written);
      unsigned color_type = (key->ps_epilog.color_types >> (output_index * 2)) & 0x3;

      if (color_type != SI_TYPE_ANY32) {
         for (i = 0; i < 4; i++) {
            color[i] = LLVMGetParam(ctx->main_fn, vgpr + i / 2);
            color[i] = LLVMBuildBitCast(ctx->ac.builder, color[i], ctx->ac.v2f16, "");
            color[i] = ac_llvm_extract_elem(&ctx->ac, color[i], i % 2);
         }
         vgpr += 4;
      } else {
         for (i = 0; i < 4; i++)
            color[i] = LLVMGetParam(ctx->main_fn, vgpr++);
      }

      if (si_export_mrt_color(ctx, color, output_index, num_compacted_mrts,
                              ctx->args.arg_count - 1,
                              output_index == last_color_export, color_type, &exp))
         num_compacted_mrts++;
   }

   /* Process depth, stencil, samplemask. */
   if (key->ps_epilog.writes_z)
      depth = LLVMGetParam(ctx->main_fn, vgpr++);
   if (key->ps_epilog.writes_stencil)
      stencil = LLVMGetParam(ctx->main_fn, vgpr++);
   if (key->ps_epilog.writes_samplemask)
      samplemask = LLVMGetParam(ctx->main_fn, vgpr++);

   if (depth || stencil || samplemask)
      ac_export_mrt_z(&ctx->ac, depth, stencil, samplemask, &exp.args[exp.num++]);
   else if (last_color_export == -1)
      ac_build_export_null(&ctx->ac);

   if (exp.num) {
      for (unsigned i = 0; i < exp.num; i++)
         ac_build_export(&ctx->ac, &exp.args[i]);
   }

   /* Compile. */
   LLVMBuildRetVoid(ctx->ac.builder);
}

void si_llvm_build_monolithic_ps(struct si_shader_context *ctx, struct si_shader *shader)
{
   LLVMValueRef parts[3];
   unsigned num_parts = 0, main_index;
   LLVMValueRef main_fn = ctx->main_fn;

   union si_shader_part_key prolog_key;
   si_get_ps_prolog_key(shader, &prolog_key, false);

   if (si_need_ps_prolog(&prolog_key)) {
      si_llvm_build_ps_prolog(ctx, &prolog_key);
      parts[num_parts++] = ctx->main_fn;
   }

   main_index = num_parts;
   parts[num_parts++] = main_fn;

   union si_shader_part_key epilog_key;
   si_get_ps_epilog_key(shader, &epilog_key);
   si_llvm_build_ps_epilog(ctx, &epilog_key);
   parts[num_parts++] = ctx->main_fn;

   si_build_wrapper_function(ctx, parts, num_parts, main_index, 0, false);
}

void si_llvm_init_ps_callbacks(struct si_shader_context *ctx)
{
   ctx->abi.emit_outputs = si_llvm_return_fs_outputs;
   ctx->abi.load_sample_position = load_sample_position;
   ctx->abi.load_sample_mask_in = load_sample_mask_in;
   ctx->abi.emit_fbfetch = si_nir_emit_fbfetch;
}
