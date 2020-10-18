/*
 * Copyright © 2018 Collabora Ltd
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "st_tgsi_lower_depth_clamp.h"
#include "tgsi/tgsi_transform.h"
#include "tgsi/tgsi_scan.h"

struct tgsi_depth_clamp_transform {
   struct tgsi_transform_context base;

   struct tgsi_shader_info info;

   int depth_range_const;
   int next_generic;
   int imm;
   int depth_var;
   int pos_input;
   int pos_output;
   int pos_input_temp;
   int pos_output_temp;
   int depth_range_corrected;
   bool depth_clip_minus_one_to_one;
};

static inline struct tgsi_depth_clamp_transform *
tgsi_depth_clamp_transform(struct tgsi_transform_context *tctx)
{
   return (struct tgsi_depth_clamp_transform *)tctx;
}

static void
transform_decl(struct tgsi_transform_context *tctx,
               struct tgsi_full_declaration *decl)
{
   struct tgsi_depth_clamp_transform *ctx = tgsi_depth_clamp_transform(tctx);

   /* find the next generic index usable for our inserted varying */
   if (ctx->info.processor == PIPE_SHADER_FRAGMENT) {
      if (decl->Declaration.File == TGSI_FILE_INPUT &&
          decl->Semantic.Name == TGSI_SEMANTIC_GENERIC)
         ctx->next_generic = MAX2(ctx->next_generic, decl->Semantic.Index + 1);
   } else {
      if (decl->Declaration.File == TGSI_FILE_OUTPUT &&
          decl->Semantic.Name == TGSI_SEMANTIC_GENERIC)
         ctx->next_generic = MAX2(ctx->next_generic, decl->Semantic.Index + 1);
   }

   if (decl->Declaration.File == TGSI_FILE_OUTPUT &&
       decl->Semantic.Name == TGSI_SEMANTIC_POSITION) {
      assert(decl->Semantic.Index == 0);
      ctx->pos_output = decl->Range.First;
   } else if (decl->Declaration.File == TGSI_FILE_INPUT &&
              decl->Semantic.Name == TGSI_SEMANTIC_POSITION) {
      assert(decl->Semantic.Index == 0);
      if (ctx->info.processor == PIPE_SHADER_FRAGMENT)
         ctx->pos_input = decl->Range.First;
   }

   tctx->emit_declaration(tctx, decl);
}

static void
prolog_common(struct tgsi_depth_clamp_transform *ctx)
{
   assert(ctx->depth_range_const >= 0);
   if (ctx->info.const_file_max[0] < ctx->depth_range_const)
      tgsi_transform_const_decl(&ctx->base, ctx->depth_range_const,
                                ctx->depth_range_const);

   /* declare a temp for the position-output */
   ctx->pos_output_temp = ctx->info.file_max[TGSI_FILE_TEMPORARY] + 1;
   tgsi_transform_temp_decl(&ctx->base, ctx->pos_output_temp);
}

static void
prolog_last_vertex_stage(struct tgsi_transform_context *tctx)
{
   struct tgsi_depth_clamp_transform *ctx = tgsi_depth_clamp_transform(tctx);

   prolog_common(ctx);

   ctx->imm = ctx->info.immediate_count;
   tgsi_transform_immediate_decl(tctx, 0.5, 0.0, 0.0, 0.0);

   /* declare the output */
   ctx->depth_var = ctx->info.num_outputs;
   tgsi_transform_output_decl(tctx, ctx->depth_var,
                              TGSI_SEMANTIC_GENERIC,
                              ctx->next_generic,
                              TGSI_INTERPOLATE_LINEAR);
}

static void
epilog_last_vertex_stage(struct tgsi_transform_context *tctx)
{
   struct tgsi_depth_clamp_transform *ctx = tgsi_depth_clamp_transform(tctx);

   int mad_dst_file = TGSI_FILE_TEMPORARY;
   int mad_dst_index = ctx->pos_output_temp;

   if (!ctx->depth_clip_minus_one_to_one) {
      mad_dst_file = TGSI_FILE_OUTPUT;
      mad_dst_index = ctx->depth_var;
   }

   /* move from temp-register to output */
   tgsi_transform_op1_inst(tctx, TGSI_OPCODE_MOV,
                           TGSI_FILE_OUTPUT, ctx->pos_output,
                           TGSI_WRITEMASK_XYZW,
                           TGSI_FILE_TEMPORARY, ctx->pos_output_temp);

   /* Set gl_position.z to 0.0 to avoid clipping */
   tgsi_transform_op1_swz_inst(tctx, TGSI_OPCODE_MOV,
                               TGSI_FILE_OUTPUT, ctx->pos_output,
                               TGSI_WRITEMASK_Z,
                               TGSI_FILE_IMMEDIATE, ctx->imm,
                               TGSI_SWIZZLE_Y);

   /* Evaluate and pass true depth value in depthRange terms */
   /* z = gl_Position.z / gl_Position.w */

   struct tgsi_full_instruction inst;

   inst = tgsi_default_full_instruction();
   inst.Instruction.Opcode = TGSI_OPCODE_DIV;
   inst.Instruction.NumDstRegs = 1;
   inst.Dst[0].Register.File = TGSI_FILE_TEMPORARY;
   inst.Dst[0].Register.Index = ctx->pos_output_temp;
   inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_X;
   inst.Instruction.NumSrcRegs = 2;
   tgsi_transform_src_reg_xyzw(&inst.Src[0], TGSI_FILE_TEMPORARY, ctx->pos_output_temp);
   tgsi_transform_src_reg_xyzw(&inst.Src[1], TGSI_FILE_TEMPORARY, ctx->pos_output_temp);
   inst.Src[0].Register.SwizzleX =
         inst.Src[0].Register.SwizzleY =
         inst.Src[0].Register.SwizzleZ =
         inst.Src[0].Register.SwizzleW = TGSI_SWIZZLE_Z;

   inst.Src[1].Register.SwizzleX =
         inst.Src[1].Register.SwizzleY =
         inst.Src[1].Register.SwizzleZ =
         inst.Src[1].Register.SwizzleW = TGSI_SWIZZLE_W;

   tctx->emit_instruction(tctx, &inst);


   /* OpenGL Core Profile 4.5 - 13.6.1
    * The vertex's windows z coordinate zw is given by zw = s * z + b.
    *
    * *  With clip control depth mode ZERO_TO_ONE
    *      s = f - n, b = n, and hence
    *
    *     zw_0_1 = z * gl_DepthRange.diff + gl_DepthRange.near
    */
   tgsi_transform_op3_swz_inst(tctx, TGSI_OPCODE_MAD,
                               mad_dst_file, mad_dst_index,
                               TGSI_WRITEMASK_X,
                               TGSI_FILE_TEMPORARY, ctx->pos_output_temp,
                               TGSI_SWIZZLE_X,
                               false,
                               TGSI_FILE_CONSTANT, ctx->depth_range_const,
                               TGSI_SWIZZLE_Z,
                               TGSI_FILE_CONSTANT, ctx->depth_range_const,
                               TGSI_SWIZZLE_X);

   /* If clip control depth mode is NEGATIVE_ONE_TO_ONE, then
   *     s = 0.5 * (f - n), b = 0.5 * (n + f), and hence
   *
   *     zw_m1_1 = 0.5 * (zw_01 + gl_DepthRange.far)
   */
   if (ctx->depth_clip_minus_one_to_one) {
       /* z += gl_DepthRange.far */
      tgsi_transform_op2_swz_inst(tctx, TGSI_OPCODE_ADD,
                                  TGSI_FILE_TEMPORARY, ctx->pos_output_temp,
                                  TGSI_WRITEMASK_X,
                                  TGSI_FILE_TEMPORARY, ctx->pos_output_temp,
                                  TGSI_SWIZZLE_X,
                                  TGSI_FILE_CONSTANT, ctx->depth_range_const,
                                  TGSI_SWIZZLE_Y, false);
      /* z *=  0.5 */
      tgsi_transform_op2_swz_inst(tctx, TGSI_OPCODE_MUL,
                                  TGSI_FILE_OUTPUT, ctx->depth_var,
                                  TGSI_WRITEMASK_X,
                                  TGSI_FILE_TEMPORARY, ctx->pos_output_temp,
                                  TGSI_SWIZZLE_X,
                                  TGSI_FILE_IMMEDIATE, ctx->imm,
                                  TGSI_SWIZZLE_X, false);
   }
}


static void
prolog_fs(struct tgsi_transform_context *tctx)
{
   struct tgsi_depth_clamp_transform *ctx = tgsi_depth_clamp_transform(tctx);

   prolog_common(ctx);

   ctx->depth_range_corrected = ctx->info.file_max[TGSI_FILE_TEMPORARY] + 2;
   tgsi_transform_temp_decl(tctx, ctx->depth_range_corrected);

   /* declare the input */
   ctx->depth_var = ctx->info.num_inputs;
   tgsi_transform_input_decl(tctx, ctx->depth_var,
                             TGSI_SEMANTIC_GENERIC,
                             ctx->next_generic,
                             TGSI_INTERPOLATE_LINEAR);

   /* declare the output */
   if (ctx->pos_output < 0) {
      ctx->pos_output = ctx->info.num_outputs;
      tgsi_transform_output_decl(tctx, ctx->pos_output,
                                 TGSI_SEMANTIC_POSITION,
                                 0,
                                 TGSI_INTERPOLATE_LINEAR);
   }

   if (ctx->info.reads_z) {
      ctx->pos_input_temp = ctx->info.file_max[TGSI_FILE_TEMPORARY] + 3;
      tgsi_transform_temp_decl(tctx, ctx->pos_input_temp);

      assert(ctx->pos_input_temp >= 0);
      /* copy normal position */
      tgsi_transform_op1_inst(tctx, TGSI_OPCODE_MOV,
                              TGSI_FILE_TEMPORARY, ctx->pos_input_temp,
                              TGSI_WRITEMASK_XYZW,
                              TGSI_FILE_INPUT, ctx->pos_input);
      /* replace z-component with varying */
      tgsi_transform_op1_swz_inst(tctx, TGSI_OPCODE_MOV,
                                  TGSI_FILE_TEMPORARY, ctx->pos_input_temp,
                                  TGSI_WRITEMASK_Z,
                                  TGSI_FILE_INPUT, ctx->depth_var,
                                  TGSI_SWIZZLE_X);
   }
}

static void
epilog_fs(struct tgsi_transform_context *tctx)
{
   struct tgsi_depth_clamp_transform *ctx = tgsi_depth_clamp_transform(tctx);

   unsigned src0_file = TGSI_FILE_INPUT;
   unsigned src0_index = ctx->depth_var;
   unsigned src0_swizzle = TGSI_SWIZZLE_X;

   if (ctx->info.writes_z) {
      src0_file = TGSI_FILE_TEMPORARY;
      src0_index = ctx->pos_output_temp;
      src0_swizzle = TGSI_SWIZZLE_Z;
   }

   /* it is possible to have gl_DepthRange.near > gl_DepthRange.far, so first
    * we have to sort the two */
   tgsi_transform_op2_swz_inst(tctx, TGSI_OPCODE_MIN,
                               TGSI_FILE_TEMPORARY, ctx->depth_range_corrected,
                               TGSI_WRITEMASK_X,
                               TGSI_FILE_CONSTANT, ctx->depth_range_const,
                               TGSI_SWIZZLE_X,
                               TGSI_FILE_CONSTANT, ctx->depth_range_const,
                               TGSI_SWIZZLE_Y,
                               false);

   tgsi_transform_op2_swz_inst(tctx, TGSI_OPCODE_MAX,
                               TGSI_FILE_TEMPORARY, ctx->depth_range_corrected,
                               TGSI_WRITEMASK_Y,
                               TGSI_FILE_CONSTANT, ctx->depth_range_const,
                               TGSI_SWIZZLE_X,
                               TGSI_FILE_CONSTANT, ctx->depth_range_const,
                               TGSI_SWIZZLE_Y,
                               false);

   /* gl_FragDepth = max(gl_FragDepth, min(gl_DepthRange.near, gl_DepthRange.far)) */
   tgsi_transform_op2_swz_inst(tctx, TGSI_OPCODE_MAX,
                               TGSI_FILE_TEMPORARY, ctx->pos_output_temp,
                               TGSI_WRITEMASK_X,
                               src0_file, src0_index, src0_swizzle,
                               TGSI_FILE_TEMPORARY, ctx->depth_range_corrected,
                               TGSI_SWIZZLE_X, false);

   /* gl_FragDepth = min(gl_FragDepth, max(gl_DepthRange.near, gl_DepthRange.far)) */
   tgsi_transform_op2_swz_inst(tctx, TGSI_OPCODE_MIN,
                               TGSI_FILE_OUTPUT, ctx->pos_output,
                               TGSI_WRITEMASK_Z,
                               TGSI_FILE_TEMPORARY, ctx->pos_output_temp,
                               TGSI_SWIZZLE_X,
                               TGSI_FILE_TEMPORARY, ctx->depth_range_corrected,
                               TGSI_SWIZZLE_Y, false);
}

static void
transform_instr(struct tgsi_transform_context *tctx,
                struct tgsi_full_instruction *inst)
{
   struct tgsi_depth_clamp_transform *ctx = tgsi_depth_clamp_transform(tctx);

   if (ctx->pos_output >= 0) {
      /* replace writes to gl_Position / gl_FragDepth with a temp-variable
       */
      for (int i = 0; i < inst->Instruction.NumDstRegs; ++i) {
         if (inst->Dst[i].Register.File == TGSI_FILE_OUTPUT &&
             inst->Dst[i].Register.Index == ctx->pos_output) {
            inst->Dst[i].Register.File = TGSI_FILE_TEMPORARY;
            inst->Dst[i].Register.Index = ctx->pos_output_temp;
         }
      }
   }

   if (ctx->info.reads_z) {
      /* replace reads from gl_FragCoord with temp-variable
       */
      assert(ctx->pos_input_temp >= 0);
      for (int i = 0; i < inst->Instruction.NumSrcRegs; ++i) {
         if (inst->Src[i].Register.File == TGSI_FILE_INPUT &&
             inst->Src[i].Register.Index == ctx->pos_input) {
            inst->Src[i].Register.File = TGSI_FILE_TEMPORARY;
            inst->Src[i].Register.Index = ctx->pos_input_temp;
         }
      }
   }

   /* In a GS each we have to add the z-write opilog for each emit
    */
   if (ctx->info.processor == PIPE_SHADER_GEOMETRY &&
       inst->Instruction.Opcode == TGSI_OPCODE_EMIT)
      epilog_last_vertex_stage(tctx);

   tctx->emit_instruction(tctx, inst);
}

const struct tgsi_token *
st_tgsi_lower_depth_clamp(const struct tgsi_token *tokens,
                          int depth_range_const,
                          bool clip_negative_one_to_one)
{
   struct tgsi_depth_clamp_transform ctx;
   struct tgsi_token *newtoks;
   int newlen;

   memset(&ctx, 0, sizeof(ctx));
   tgsi_scan_shader(tokens, &ctx.info);

   /* we only want to do this for the fragment shader, and the shader-stage
    * right before it, but in the first pass there might be no "next" shader
    */
   if (ctx.info.processor != PIPE_SHADER_FRAGMENT &&
       ctx.info.processor != PIPE_SHADER_GEOMETRY &&
       ctx.info.processor != PIPE_SHADER_VERTEX &&
       ctx.info.processor != PIPE_SHADER_TESS_EVAL &&
       (ctx.info.properties[TGSI_PROPERTY_NEXT_SHADER] > PIPE_SHADER_VERTEX &&
       (ctx.info.properties[TGSI_PROPERTY_NEXT_SHADER] != PIPE_SHADER_FRAGMENT)))  {
      return tokens;
   }

   ctx.base.transform_declaration = transform_decl;
   ctx.base.transform_instruction = transform_instr;

   if (ctx.info.processor == PIPE_SHADER_FRAGMENT) {
      ctx.base.prolog = prolog_fs;
      ctx.base.epilog = epilog_fs;
   } else {
      ctx.base.prolog = prolog_last_vertex_stage;
      ctx.base.epilog = epilog_last_vertex_stage;
   }

   ctx.pos_output = ctx.pos_input = -1;
   ctx.depth_range_const = depth_range_const;
   ctx.depth_clip_minus_one_to_one = clip_negative_one_to_one;

   /* We add approximately 30 tokens per Z write, so add this per vertex in
    * a GS and some additional tokes for VS and TES
    */
   newlen = tgsi_num_tokens(tokens) +
            30 * ctx.info.properties[TGSI_PROPERTY_GS_MAX_OUTPUT_VERTICES] +
            120;

   newtoks = tgsi_alloc_tokens(newlen);
   if (!newtoks)
      return tokens;

   tgsi_transform_shader(tokens, newtoks, newlen, &ctx.base);

   return newtoks;
}

const struct tgsi_token *
st_tgsi_lower_depth_clamp_fs(const struct tgsi_token *tokens,
                             int depth_range_const)
{
   return st_tgsi_lower_depth_clamp(tokens, depth_range_const, false);
}
