/**************************************************************************
 *
 * Copyright (C) 2015 Advanced Micro Devices, Inc.
 * Copyright 2007 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "st_cb_bitmap.h"
#include "tgsi/tgsi_transform.h"
#include "tgsi/tgsi_scan.h"
#include "util/u_debug.h"

struct tgsi_bitmap_transform {
   struct tgsi_transform_context base;
   struct tgsi_shader_info info;
   unsigned sampler_index;
   unsigned tex_target;
   bool use_texcoord;
   bool swizzle_xxxx;
   bool first_instruction_emitted;
};

static inline struct tgsi_bitmap_transform *
tgsi_bitmap_transform(struct tgsi_transform_context *tctx)
{
   return (struct tgsi_bitmap_transform *)tctx;
}

static void
transform_instr(struct tgsi_transform_context *tctx,
		struct tgsi_full_instruction *current_inst)
{
   struct tgsi_bitmap_transform *ctx = tgsi_bitmap_transform(tctx);
   struct tgsi_full_instruction inst;
   unsigned tgsi_tex_target = ctx->tex_target == PIPE_TEXTURE_2D
      ? TGSI_TEXTURE_2D : TGSI_TEXTURE_RECT;
   unsigned i, semantic;
   int texcoord_index = -1;

   if (ctx->first_instruction_emitted) {
      tctx->emit_instruction(tctx, current_inst);
      return;
   }

   ctx->first_instruction_emitted = true;

   /* Add TEMP[0] if it's missing. */
   if (ctx->info.file_max[TGSI_FILE_TEMPORARY] == -1) {
      tgsi_transform_temp_decl(tctx, 0);
   }

   /* Add TEXCOORD[0] if it's missing. */
   semantic = ctx->use_texcoord ? TGSI_SEMANTIC_TEXCOORD :
                                  TGSI_SEMANTIC_GENERIC;
   for (i = 0; i < ctx->info.num_inputs; i++) {
      if (ctx->info.input_semantic_name[i] == semantic &&
          ctx->info.input_semantic_index[i] == 0) {
         texcoord_index = i;
         break;
      }
   }

   if (texcoord_index == -1) {
      texcoord_index = ctx->info.num_inputs;
      tgsi_transform_input_decl(tctx, texcoord_index,
                                semantic, 0, TGSI_INTERPOLATE_PERSPECTIVE);
   }

   /* Declare the sampler. */
   tgsi_transform_sampler_decl(tctx, ctx->sampler_index);

   /* Declare the sampler view. */
   tgsi_transform_sampler_view_decl(tctx, ctx->sampler_index,
                                    tgsi_tex_target, TGSI_RETURN_TYPE_FLOAT);

   /* TEX tmp0, fragment.texcoord[0], texture[0], 2D; */
   tgsi_transform_tex_inst(tctx,
                           TGSI_FILE_TEMPORARY, 0,
                           TGSI_FILE_INPUT, texcoord_index,
                           tgsi_tex_target, ctx->sampler_index);

   /* KIL if -tmp0 < 0 # texel=0 -> keep / texel=0 -> discard */
   inst = tgsi_default_full_instruction();
   inst.Instruction.Opcode = TGSI_OPCODE_KILL_IF;
   inst.Instruction.NumDstRegs = 0;
   inst.Instruction.NumSrcRegs = 1;

   inst.Src[0].Register.File  = TGSI_FILE_TEMPORARY;
   inst.Src[0].Register.Index = 0;
   inst.Src[0].Register.Negate = 1;
   inst.Src[0].Register.SwizzleX = TGSI_SWIZZLE_X;
   if (ctx->swizzle_xxxx) {
      inst.Src[0].Register.SwizzleY = TGSI_SWIZZLE_X;
      inst.Src[0].Register.SwizzleZ = TGSI_SWIZZLE_X;
      inst.Src[0].Register.SwizzleW = TGSI_SWIZZLE_X;
   } else {
      inst.Src[0].Register.SwizzleY = TGSI_SWIZZLE_Y;
      inst.Src[0].Register.SwizzleZ = TGSI_SWIZZLE_Z;
      inst.Src[0].Register.SwizzleW = TGSI_SWIZZLE_W;
   }
   tctx->emit_instruction(tctx, &inst);

   /* And emit the instruction we got. */
   tctx->emit_instruction(tctx, current_inst);
}

const struct tgsi_token *
st_get_bitmap_shader(const struct tgsi_token *tokens,
                     unsigned tex_target, unsigned sampler_index,
                     bool use_texcoord, bool swizzle_xxxx)
{
   struct tgsi_bitmap_transform ctx;
   struct tgsi_token *newtoks;
   int newlen;

   assert(tex_target == PIPE_TEXTURE_2D ||
          tex_target == PIPE_TEXTURE_RECT);

   memset(&ctx, 0, sizeof(ctx));
   ctx.base.transform_instruction = transform_instr;
   ctx.tex_target = tex_target;
   ctx.sampler_index = sampler_index;
   ctx.use_texcoord = use_texcoord;
   ctx.swizzle_xxxx = swizzle_xxxx;
   tgsi_scan_shader(tokens, &ctx.info);

   newlen = tgsi_num_tokens(tokens) + 20;
   newtoks = tgsi_alloc_tokens(newlen);
   if (!newtoks)
      return NULL;

   tgsi_transform_shader(tokens, newtoks, newlen, &ctx.base);
   return newtoks;
}
