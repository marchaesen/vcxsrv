/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_pipe.h"
#include "si_shader_internal.h"
#include "si_shader_llvm.h"
#include "sid.h"

static LLVMValueRef si_build_fs_interp(struct si_shader_context *ctx, unsigned attr_index,
                                       unsigned chan, LLVMValueRef prim_mask, LLVMValueRef i,
                                       LLVMValueRef j)
{
   if (i || j) {
      return ac_build_fs_interp(&ctx->ac, LLVMConstInt(ctx->ac.i32, chan, 0),
                                LLVMConstInt(ctx->ac.i32, attr_index, 0), prim_mask, i, j);
   }
   return ac_build_fs_interp_mov(&ctx->ac, 0, /* P0 */
                                 LLVMConstInt(ctx->ac.i32, chan, 0),
                                 LLVMConstInt(ctx->ac.i32, attr_index, 0), prim_mask);
}

/**
 * Interpolate a fragment shader input.
 *
 * @param ctx                context
 * @param input_index        index of the input in hardware
 * @param semantic_index     semantic index
 * @param num_interp_inputs  number of all interpolated inputs (= BCOLOR offset)
 * @param colors_read_mask   color components read (4 bits for each color, 8 bits in total)
 * @param interp_param       interpolation weights (i,j)
 * @param prim_mask          SI_PARAM_PRIM_MASK
 * @param face               SI_PARAM_FRONT_FACE
 * @param result             the return value (4 components)
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
      i = LLVMBuildExtractElement(ctx->ac.builder, interp_param, ctx->ac.i32_0, "");
      j = LLVMBuildExtractElement(ctx->ac.builder, interp_param, ctx->ac.i32_1, "");
   }

   if (ctx->shader->key.ps.part.prolog.color_two_side) {
      LLVMValueRef is_face_positive;

      /* If BCOLOR0 is used, BCOLOR1 is at offset "num_inputs + 1",
       * otherwise it's at offset "num_inputs".
       */
      unsigned back_attr_offset = num_interp_inputs;
      if (semantic_index == 1 && colors_read_mask & 0xf)
         back_attr_offset += 1;

      is_face_positive = LLVMBuildFCmp(ctx->ac.builder, LLVMRealOLT, ctx->ac.f32_0, face, "");

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
   if (ctx->shader->key.ps.part.epilog.alpha_func != PIPE_FUNC_NEVER) {
      static LLVMRealPredicate cond_map[PIPE_FUNC_ALWAYS + 1] = {
         [PIPE_FUNC_LESS] = LLVMRealOLT,     [PIPE_FUNC_EQUAL] = LLVMRealOEQ,
         [PIPE_FUNC_LEQUAL] = LLVMRealOLE,   [PIPE_FUNC_GREATER] = LLVMRealOGT,
         [PIPE_FUNC_NOTEQUAL] = LLVMRealONE, [PIPE_FUNC_GEQUAL] = LLVMRealOGE,
      };
      LLVMRealPredicate cond = cond_map[ctx->shader->key.ps.part.epilog.alpha_func];
      assert(cond);

      LLVMValueRef alpha_ref = ac_get_arg(&ctx->ac, ctx->args->alpha_reference);
      if (LLVMTypeOf(alpha) == ctx->ac.f16)
         alpha_ref = LLVMBuildFPTrunc(ctx->ac.builder, alpha_ref, ctx->ac.f16, "");

      LLVMValueRef alpha_pass = LLVMBuildFCmp(ctx->ac.builder, cond, alpha, alpha_ref, "");
      ac_build_kill_if_false(&ctx->ac, alpha_pass);
   } else {
      ac_build_kill_if_false(&ctx->ac, ctx->ac.i1false);
   }
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
static bool si_llvm_init_ps_export_args(struct si_shader_context *ctx, LLVMValueRef *values,
                                        unsigned cbuf, unsigned compacted_mrt_index,
                                        unsigned color_type, struct ac_export_args *args)
{
   const union si_shader_key *key = &ctx->shader->key;
   unsigned col_formats = key->ps.part.epilog.spi_shader_col_format;
   LLVMValueRef f32undef = LLVMGetUndef(ctx->ac.f32);
   unsigned spi_shader_col_format;
   unsigned chan;
   bool is_int8, is_int10;

   assert(cbuf < 8);

   spi_shader_col_format = (col_formats >> (cbuf * 4)) & 0xf;
   if (spi_shader_col_format == V_028714_SPI_SHADER_ZERO)
      return false;

   is_int8 = (key->ps.part.epilog.color_is_int8 >> cbuf) & 0x1;
   is_int10 = (key->ps.part.epilog.color_is_int10 >> cbuf) & 0x1;

   /* Default is 0xf. Adjusted below depending on the format. */
   args->enabled_channels = 0xf; /* writemask */

   /* Specify whether the EXEC mask represents the valid mask */
   args->valid_mask = 0;

   /* Specify whether this is the last export */
   args->done = 0;

   /* Specify the target we are exporting */
   args->target = V_008DFC_SQ_EXP_MRT + compacted_mrt_index;

   if (key->ps.part.epilog.dual_src_blend_swizzle &&
       (compacted_mrt_index == 0 || compacted_mrt_index == 1)) {
      assert(ctx->ac.gfx_level >= GFX11);
      args->target += 21;
   }

   args->compr = false;
   args->out[0] = f32undef;
   args->out[1] = f32undef;
   args->out[2] = f32undef;
   args->out[3] = f32undef;

   LLVMValueRef (*packf)(struct ac_llvm_context * ctx, LLVMValueRef args[2]) = NULL;
   LLVMValueRef (*packi)(struct ac_llvm_context * ctx, LLVMValueRef args[2], unsigned bits,
                         bool hi) = NULL;

   switch (spi_shader_col_format) {
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
      if (ctx->screen->info.gfx_level >= GFX10) {
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
   }
   if (packf || packi) {
      if (ctx->screen->info.gfx_level >= GFX11)
         args->enabled_channels = 0x3;
      else
         args->compr = 1; /* COMPR flag */
   }

   return true;
}

static void si_llvm_build_clamp_alpha_test(struct si_shader_context *ctx,
                                           LLVMValueRef *color, unsigned index)
{
   int i;

   /* Clamp color */
   if (ctx->shader->key.ps.part.epilog.clamp_color)
      for (i = 0; i < 4; i++)
         color[i] = ac_build_clamp(&ctx->ac, color[i]);

   /* Alpha to one */
   if (ctx->shader->key.ps.part.epilog.alpha_to_one)
      color[3] = LLVMConstReal(LLVMTypeOf(color[0]), 1);

   /* Alpha test */
   if (index == 0 && ctx->shader->key.ps.part.epilog.alpha_func != PIPE_FUNC_ALWAYS)
      si_alpha_test(ctx, color[3]);
}

static void si_export_mrt_color(struct si_shader_context *ctx, LLVMValueRef *color, unsigned index,
                                unsigned first_color_export, unsigned color_type,
                                bool writes_all_cbufs, struct si_ps_exports *exp)
{
   if (writes_all_cbufs) {
      assert(exp->num == first_color_export);

      /* This will do nothing for color buffers with SPI_SHADER_COL_FORMAT=ZERO, so always
       * iterate over all 8.
       */
      for (int c = 0; c < 8; c++) {
         if (si_llvm_init_ps_export_args(ctx, color, c, exp->num - first_color_export,
                                         color_type, &exp->args[exp->num])) {
            assert(exp->args[exp->num].enabled_channels);
            exp->num++;
         }
      }
   } else {
      /* Export */
      if (si_llvm_init_ps_export_args(ctx, color, index, exp->num - first_color_export,
                                      color_type, &exp->args[exp->num])) {
         assert(exp->args[exp->num].enabled_channels);
         exp->num++;
      }
   }
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
void si_llvm_ps_build_end(struct si_shader_context *ctx)
{
   struct si_shader *shader = ctx->shader;
   struct si_shader_info *info = &shader->selector->info;
   LLVMBuilderRef builder = ctx->ac.builder;
   unsigned i, j, vgpr;
   LLVMValueRef *addrs = ctx->abi.outputs;

   LLVMValueRef color[8][4] = {};
   LLVMValueRef depth = NULL, stencil = NULL, samplemask = NULL;
   LLVMValueRef ret;

   /* Read the output values. */
   for (i = 0; i < info->num_outputs; i++) {
      unsigned semantic = info->output_semantic[i];
      LLVMTypeRef type = ctx->abi.is_16bit[4 * i] ? ctx->ac.f16 : ctx->ac.f32;

      switch (semantic) {
      case FRAG_RESULT_DEPTH:
         depth = LLVMBuildLoad2(builder, type, addrs[4 * i + 0], "");
         break;
      case FRAG_RESULT_STENCIL:
         stencil = LLVMBuildLoad2(builder, type, addrs[4 * i + 0], "");
         break;
      case FRAG_RESULT_SAMPLE_MASK:
         samplemask = LLVMBuildLoad2(builder, type, addrs[4 * i + 0], "");
         break;
      default:
         if (semantic >= FRAG_RESULT_DATA0 && semantic <= FRAG_RESULT_DATA7) {
            unsigned index = semantic - FRAG_RESULT_DATA0;

            for (j = 0; j < 4; j++) {
               LLVMValueRef ptr = addrs[4 * i + j];
               type = ctx->abi.is_16bit[4 * i + j] ? ctx->ac.f16 : ctx->ac.f32;
               LLVMValueRef result = LLVMBuildLoad2(builder, type, ptr, "");
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
      builder, ret, ac_to_integer(&ctx->ac, LLVMGetParam(ctx->main_fn.value, SI_PARAM_ALPHA_REF)),
      SI_SGPR_ALPHA_REF, "");

   /* Set VGPRs */
   vgpr = SI_SGPR_ALPHA_REF + 1;
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

   ctx->return_value = ret;
}

static void si_llvm_emit_polygon_stipple(struct si_shader_context *ctx)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef desc, offset, row, bit, address[2];

   /* Use the fixed-point gl_FragCoord input.
    * Since the stipple pattern is 32x32 and it repeats, just get 5 bits
    * per coordinate to get the repeating effect.
    */
   address[0] = si_unpack_param(ctx, ctx->args->ac.pos_fixed_pt, 0, 5);
   address[1] = si_unpack_param(ctx, ctx->args->ac.pos_fixed_pt, 16, 5);

   /* Load the buffer descriptor. */
   desc = si_prolog_get_internal_binding_slot(ctx, SI_PS_CONST_POLY_STIPPLE);

   /* The stipple pattern is 32x32, each row has 32 bits. */
   offset = LLVMBuildMul(builder, address[1], LLVMConstInt(ctx->ac.i32, 4, 0), "");
   row = si_buffer_load_const(ctx, desc, offset);
   row = ac_to_integer(&ctx->ac, row);
   bit = LLVMBuildLShr(builder, row, address[0], "");
   bit = LLVMBuildTrunc(builder, bit, ctx->ac.i1, "");
   ac_build_kill_if_false(&ctx->ac, bit);
}

static LLVMValueRef insert_ret_of_arg(struct si_shader_context *ctx, LLVMValueRef ret,
                                      LLVMValueRef data, unsigned arg_index)
{
   unsigned base = ctx->args->ac.args[arg_index].file == AC_ARG_VGPR ?
      ctx->args->ac.num_sgprs_used : 0;
   unsigned index = base + ctx->args->ac.args[arg_index].offset;

   if (ctx->args->ac.args[arg_index].size == 1) {
      return LLVMBuildInsertValue(ctx->ac.builder, ret, data, index, "");
   } else {
      assert(ctx->args->ac.args[arg_index].size == 2);
      LLVMValueRef tmp = LLVMBuildExtractElement(ctx->ac.builder, data, ctx->ac.i32_0, "");
      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, tmp, index, "");
      tmp = LLVMBuildExtractElement(ctx->ac.builder, data, ctx->ac.i32_1, "");
      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, tmp, index + 1, "");
      return ret;
   }
}

/**
 * Build the pixel shader prolog function. This handles:
 * - two-side color selection and interpolation
 * - overriding interpolation parameters for the API PS
 * - polygon stippling
 *
 * All preloaded SGPRs and VGPRs are passed through unmodified unless they are
 * overridden by other states. (e.g. per-sample interpolation)
 * Interpolated colors are stored after the preloaded VGPRs.
 */
void si_llvm_build_ps_prolog(struct si_shader_context *ctx, union si_shader_part_key *key)
{
   struct si_shader_args *args = ctx->args;
   si_get_ps_prolog_args(args, key);

   /* Declare outputs (same as inputs + add colors if needed) */
   LLVMTypeRef return_types[AC_MAX_ARGS];
   int num_returns = 0;

   for (int i = 0; i < args->ac.num_sgprs_used; i++)
      return_types[num_returns++] = ctx->ac.i32;

   unsigned num_color_channels = util_bitcount(key->ps_prolog.colors_read);
   unsigned num_output_vgprs = args->ac.num_vgprs_used + num_color_channels;
   for (int i = 0; i < num_output_vgprs; i++)
      return_types[num_returns++] = ctx->ac.f32;

   /* Create the function. */
   si_llvm_create_func(ctx, "ps_prolog", return_types, num_returns, 0);
   LLVMValueRef func = ctx->main_fn.value;

   /* Copy inputs to outputs. This should be no-op, as the registers match,
    * but it will prevent the compiler from overwriting them unintentionally.
    */
   LLVMValueRef ret = ctx->return_value;
   for (int i = 0; i < args->ac.arg_count; i++) {
      LLVMValueRef p = LLVMGetParam(func, i);
      ret = insert_ret_of_arg(ctx, ret, p, i);
   }

   /* Polygon stippling. */
   if (key->ps_prolog.states.poly_stipple)
      si_llvm_emit_polygon_stipple(ctx);

   if (key->ps_prolog.states.bc_optimize_for_persp ||
       key->ps_prolog.states.bc_optimize_for_linear) {
      LLVMValueRef center, centroid, tmp;

      /* The shader should do: if (PRIM_MASK[31]) CENTROID = CENTER;
       * The hw doesn't compute CENTROID if the whole wave only
       * contains fully-covered quads.
       */
      LLVMValueRef bc_optimize = ac_get_arg(&ctx->ac, args->ac.prim_mask);
      bc_optimize =
         LLVMBuildLShr(ctx->ac.builder, bc_optimize, LLVMConstInt(ctx->ac.i32, 31, 0), "");
      bc_optimize = LLVMBuildTrunc(ctx->ac.builder, bc_optimize, ctx->ac.i1, "");

      if (key->ps_prolog.states.bc_optimize_for_persp) {
         center = ac_get_arg(&ctx->ac, args->ac.persp_center);
         centroid = ac_get_arg(&ctx->ac, args->ac.persp_centroid);
         /* Select PERSP_CENTROID. */
         tmp = LLVMBuildSelect(ctx->ac.builder, bc_optimize, center, centroid, "");
         ret = insert_ret_of_arg(ctx, ret, tmp, args->ac.persp_centroid.arg_index);
      }
      if (key->ps_prolog.states.bc_optimize_for_linear) {
         center = ac_get_arg(&ctx->ac, args->ac.linear_center);
         centroid = ac_get_arg(&ctx->ac, args->ac.linear_centroid);
         /* Select PERSP_CENTROID. */
         tmp = LLVMBuildSelect(ctx->ac.builder, bc_optimize, center, centroid, "");
         ret = insert_ret_of_arg(ctx, ret, tmp, args->ac.linear_centroid.arg_index);
      }
   }

   /* Force per-sample interpolation. */
   if (key->ps_prolog.states.force_persp_sample_interp) {
      LLVMValueRef persp_sample = ac_get_arg(&ctx->ac, args->ac.persp_sample);
      /* Overwrite PERSP_CENTER. */
      ret = insert_ret_of_arg(ctx, ret, persp_sample, args->ac.persp_center.arg_index);
      /* Overwrite PERSP_CENTROID. */
      ret = insert_ret_of_arg(ctx, ret, persp_sample, args->ac.persp_centroid.arg_index);
   }
   if (key->ps_prolog.states.force_linear_sample_interp) {
      LLVMValueRef linear_sample = ac_get_arg(&ctx->ac, args->ac.linear_sample);
      /* Overwrite LINEAR_CENTER. */
      ret = insert_ret_of_arg(ctx, ret, linear_sample, args->ac.linear_center.arg_index);
      /* Overwrite LINEAR_CENTROID. */
      ret = insert_ret_of_arg(ctx, ret, linear_sample, args->ac.linear_centroid.arg_index);
   }

   /* Force center interpolation. */
   if (key->ps_prolog.states.force_persp_center_interp) {
      LLVMValueRef persp_center = ac_get_arg(&ctx->ac, args->ac.persp_center);
      /* Overwrite PERSP_SAMPLE. */
      ret = insert_ret_of_arg(ctx, ret, persp_center, args->ac.persp_sample.arg_index);
      /* Overwrite PERSP_CENTROID. */
      ret = insert_ret_of_arg(ctx, ret, persp_center, args->ac.persp_centroid.arg_index);
   }
   if (key->ps_prolog.states.force_linear_center_interp) {
      LLVMValueRef linear_center = ac_get_arg(&ctx->ac, args->ac.linear_center);
      /* Overwrite LINEAR_SAMPLE. */
      ret = insert_ret_of_arg(ctx, ret, linear_center, args->ac.linear_sample.arg_index);
      /* Overwrite LINEAR_CENTROID. */
      ret = insert_ret_of_arg(ctx, ret, linear_center, args->ac.linear_centroid.arg_index);
   }

   /* Interpolate colors. */
   unsigned color_out_idx = 0;
   unsigned num_input_gprs = args->ac.num_sgprs_used + args->ac.num_vgprs_used;
   for (int i = 0; i < 2; i++) {
      unsigned writemask = (key->ps_prolog.colors_read >> (i * 4)) & 0xf;

      if (!writemask)
         continue;

      /* If the interpolation qualifier is not CONSTANT (-1). */
      LLVMValueRef interp_ij = NULL;
      if (key->ps_prolog.color_interp_vgpr_index[i] != -1) {
         unsigned index =
            args->ac.num_sgprs_used + key->ps_prolog.color_interp_vgpr_index[i];

         /* Get the (i,j) updated by bc_optimize handling. */
         LLVMValueRef interp[2] = {
            LLVMBuildExtractValue(ctx->ac.builder, ret, index, ""),
            LLVMBuildExtractValue(ctx->ac.builder, ret, index + 1, ""),
         };
         interp_ij = ac_build_gather_values(&ctx->ac, interp, 2);
      }

      LLVMValueRef prim_mask = ac_get_arg(&ctx->ac, args->ac.prim_mask);

      LLVMValueRef face = NULL;
      if (key->ps_prolog.states.color_two_side)
         face = ac_get_arg(&ctx->ac, args->ac.front_face);

      LLVMValueRef color[4];
      interp_fs_color(ctx, key->ps_prolog.color_attr_index[i], i, key->ps_prolog.num_interp_inputs,
                      key->ps_prolog.colors_read, interp_ij, prim_mask, face, color);

      while (writemask) {
         unsigned chan = u_bit_scan(&writemask);
         ret = LLVMBuildInsertValue(ctx->ac.builder, ret, color[chan],
                                    num_input_gprs + color_out_idx++, "");
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
      LLVMValueRef sample_id = si_unpack_param(ctx, args->ac.ancillary, 8, 4);
      LLVMValueRef sample_mask_in;

      /* Set samplemask_log_ps_iter=3 if full sample shading is enabled even for 2x and 4x MSAA
       * to get this fast path that fully replaces sample_mask_in with sample_id.
       */
      if (key->ps_prolog.states.samplemask_log_ps_iter == 3) {
         sample_mask_in =
            LLVMBuildSelect(ctx->ac.builder, ac_build_load_helper_invocation(&ctx->ac),
                            ctx->ac.i32_0,
                            LLVMBuildShl(ctx->ac.builder, ctx->ac.i32_1, sample_id, ""), "");
      } else {
         uint32_t ps_iter_mask =
            ac_get_ps_iter_mask(1 << key->ps_prolog.states.samplemask_log_ps_iter);
         sample_mask_in =
            LLVMBuildAnd(ctx->ac.builder,
                         ac_to_integer(&ctx->ac, ac_get_arg(&ctx->ac, args->ac.sample_coverage)),
                         LLVMBuildShl(ctx->ac.builder, LLVMConstInt(ctx->ac.i32, ps_iter_mask, false),
                                      sample_id, ""), "");
      }

      sample_mask_in = ac_to_float(&ctx->ac, sample_mask_in);
      ret = insert_ret_of_arg(ctx, ret, sample_mask_in, args->ac.sample_coverage.arg_index);
   } else if (key->ps_prolog.states.force_samplemask_to_helper_invocation) {
      LLVMValueRef sample_mask_in =
         LLVMBuildZExt(ctx->ac.builder,
                       LLVMBuildNot(ctx->ac.builder, ac_build_load_helper_invocation(&ctx->ac), ""),
                       ctx->ac.i32, "");
      ret = insert_ret_of_arg(ctx, ret, ac_to_float(&ctx->ac, sample_mask_in),
                              args->ac.sample_coverage.arg_index);
   }

   if (key->ps_prolog.states.get_frag_coord_from_pixel_coord) {
      LLVMValueRef pixel_coord = ac_get_arg(&ctx->ac, args->ac.pos_fixed_pt);
      pixel_coord = LLVMBuildBitCast(ctx->ac.builder, pixel_coord, ctx->ac.v2i16, "");
      pixel_coord = LLVMBuildUIToFP(ctx->ac.builder, pixel_coord, ctx->ac.v2f32, "");

      if (!key->ps_prolog.pixel_center_integer) {
         LLVMValueRef vec2_half = LLVMConstVector((LLVMValueRef[]){LLVMConstReal(ctx->ac.f32, 0.5),
                                                                   LLVMConstReal(ctx->ac.f32, 0.5)}, 2);
         pixel_coord = LLVMBuildFAdd(ctx->ac.builder, pixel_coord, vec2_half, "");
      }

      for (unsigned i = 0; i < 2; i++) {
         if (!args->ac.frag_pos[i].used)
            continue;

         ret = insert_ret_of_arg(ctx, ret,
                                 LLVMBuildExtractElement(ctx->ac.builder, pixel_coord,
                                                         LLVMConstInt(ctx->ac.i32, i, 0), ""),
                                 args->ac.frag_pos[i].arg_index);
      }
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
   int i;
   struct si_ps_exports exp = {};
   LLVMValueRef color[8][4] = {};

   struct si_shader_args *args = ctx->args;
   struct ac_arg color_args[MAX_DRAW_BUFFERS];
   struct ac_arg depth_arg, stencil_arg, samplemask_arg;
   si_get_ps_epilog_args(args, key, color_args, &depth_arg, &stencil_arg, &samplemask_arg);

   /* Create the function. */
   si_llvm_create_func(ctx, "ps_epilog", NULL, 0, 0);
   /* Disable elimination of unused inputs. */
   ac_llvm_add_target_dep_function_attr(ctx->main_fn.value, "InitialPSInputAddr", 0xffffff);

   /* Prepare color. */
   unsigned colors_written = key->ps_epilog.colors_written;
   LLVMValueRef mrtz_alpha = NULL;

   while (colors_written) {
      int write_i = u_bit_scan(&colors_written);
      unsigned color_type = (key->ps_epilog.color_types >> (write_i * 2)) & 0x3;
      LLVMValueRef arg = ac_get_arg(&ctx->ac, color_args[write_i]);

      if (color_type != SI_TYPE_ANY32)
         arg = LLVMBuildBitCast(ctx->ac.builder, arg, LLVMVectorType(ctx->ac.f16, 8), "");

      for (i = 0; i < 4; i++)
         color[write_i][i] = ac_llvm_extract_elem(&ctx->ac, arg, i);

      if (key->ps_epilog.states.alpha_to_coverage_via_mrtz && write_i == 0)
         mrtz_alpha = color[0][3];

      si_llvm_build_clamp_alpha_test(ctx, color[write_i], write_i);
   }
   bool writes_z = key->ps_epilog.writes_z && !key->ps_epilog.states.kill_z;
   bool writes_stencil = key->ps_epilog.writes_stencil && !key->ps_epilog.states.kill_stencil;
   bool writes_samplemask = key->ps_epilog.writes_samplemask && !key->ps_epilog.states.kill_samplemask;

   /* Prepare the mrtz export. */
   if (writes_z || writes_stencil || writes_samplemask || mrtz_alpha) {
      LLVMValueRef depth = NULL, stencil = NULL, samplemask = NULL;

      if (writes_z)
         depth = ac_get_arg(&ctx->ac, depth_arg);
      if (writes_stencil)
         stencil = ac_get_arg(&ctx->ac, stencil_arg);
      if (writes_samplemask)
         samplemask = ac_get_arg(&ctx->ac, samplemask_arg);

      ac_export_mrt_z(&ctx->ac, depth, stencil, samplemask, mrtz_alpha, false,
                      &exp.args[exp.num++]);
   }

   /* Prepare color exports. */
   const unsigned first_color_export = exp.num;
   colors_written = key->ps_epilog.colors_written;

   while (colors_written) {
      int write_i = u_bit_scan(&colors_written);
      unsigned color_type = (key->ps_epilog.color_types >> (write_i * 2)) & 0x3;

      si_export_mrt_color(ctx, color[write_i], write_i, first_color_export, color_type,
                          key->ps_epilog.writes_all_cbufs, &exp);
   }

   if (exp.num) {
      exp.args[exp.num - 1].valid_mask = 1;  /* whether the EXEC mask is valid */
      exp.args[exp.num - 1].done = 1;        /* DONE bit */

      if (key->ps_epilog.states.dual_src_blend_swizzle) {
         assert(ctx->ac.gfx_level >= GFX11);
         assert((key->ps_epilog.colors_written & 0x3) == 0x3);
         ac_build_dual_src_blend_swizzle(&ctx->ac, &exp.args[first_color_export],
                                         &exp.args[first_color_export + 1]);
      }

      for (unsigned i = 0; i < exp.num; i++)
         ac_build_export(&ctx->ac, &exp.args[i]);
   } else {
      ac_build_export_null(&ctx->ac, key->ps_epilog.uses_discard);
   }

   /* Compile. */
   LLVMBuildRetVoid(ctx->ac.builder);
}
