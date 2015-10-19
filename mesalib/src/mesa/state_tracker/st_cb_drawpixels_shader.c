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

#include "st_cb_drawpixels.h"
#include "tgsi/tgsi_transform.h"
#include "tgsi/tgsi_scan.h"

struct tgsi_drawpix_transform {
   struct tgsi_transform_context base;
   struct tgsi_shader_info info;
   bool use_texcoord;
   bool scale_and_bias;
   bool pixel_maps;
   bool first_instruction_emitted;
   unsigned scale_const;
   unsigned bias_const;
   unsigned color_temp;
   unsigned drawpix_sampler;
   unsigned pixelmap_sampler;
   unsigned texcoord_const;
};

static inline struct tgsi_drawpix_transform *
tgsi_drawpix_transform(struct tgsi_transform_context *tctx)
{
   return (struct tgsi_drawpix_transform *)tctx;
}

static void
set_src(struct tgsi_full_instruction *inst, unsigned i, unsigned file, unsigned index,
        unsigned x, unsigned y, unsigned z, unsigned w)
{
   inst->Src[i].Register.File  = file;
   inst->Src[i].Register.Index = index;
   inst->Src[i].Register.SwizzleX = x;
   inst->Src[i].Register.SwizzleY = y;
   inst->Src[i].Register.SwizzleZ = z;
   inst->Src[i].Register.SwizzleW = w;
}

#define SET_SRC(inst, i, file, index, x, y, z, w) \
   set_src(inst, i, file, index, TGSI_SWIZZLE_##x, TGSI_SWIZZLE_##y, \
           TGSI_SWIZZLE_##z, TGSI_SWIZZLE_##w)

static void
transform_instr(struct tgsi_transform_context *tctx,
		struct tgsi_full_instruction *current_inst)
{
   struct tgsi_drawpix_transform *ctx = tgsi_drawpix_transform(tctx);
   struct tgsi_full_declaration decl;
   struct tgsi_full_instruction inst;
   unsigned i, sem_texcoord = ctx->use_texcoord ? TGSI_SEMANTIC_TEXCOORD :
                                                  TGSI_SEMANTIC_GENERIC;
   int texcoord_index = -1;

   if (ctx->first_instruction_emitted)
      goto transform_inst;

   ctx->first_instruction_emitted = true;

   /* Add scale and bias constants. */
   if (ctx->scale_and_bias) {
      if (ctx->info.const_file_max[0] < (int)ctx->scale_const) {
         decl = tgsi_default_full_declaration();
         decl.Declaration.File = TGSI_FILE_CONSTANT;
         decl.Range.First = decl.Range.Last = ctx->scale_const;
         tctx->emit_declaration(tctx, &decl);
      }

      if (ctx->info.const_file_max[0] < (int)ctx->bias_const) {
         decl = tgsi_default_full_declaration();
         decl.Declaration.File = TGSI_FILE_CONSTANT;
         decl.Range.First = decl.Range.Last = ctx->bias_const;
         tctx->emit_declaration(tctx, &decl);
      }
   }

   if (ctx->info.const_file_max[0] < (int)ctx->texcoord_const) {
      decl = tgsi_default_full_declaration();
      decl.Declaration.File = TGSI_FILE_CONSTANT;
      decl.Range.First = decl.Range.Last = ctx->texcoord_const;
      tctx->emit_declaration(tctx, &decl);
   }

   /* Add a new temp. */
   ctx->color_temp = ctx->info.file_max[TGSI_FILE_TEMPORARY] + 1;
   decl = tgsi_default_full_declaration();
   decl.Declaration.File = TGSI_FILE_TEMPORARY;
   decl.Range.First = decl.Range.Last = ctx->color_temp;
   tctx->emit_declaration(tctx, &decl);

   /* Add TEXCOORD[texcoord_slot] if it's missing. */
   for (i = 0; i < ctx->info.num_inputs; i++) {
      if (ctx->info.input_semantic_name[i] == sem_texcoord &&
          ctx->info.input_semantic_index[i] == 0) {
         texcoord_index = i;
         break;
      }
   }

   if (texcoord_index == -1) {
      decl = tgsi_default_full_declaration();
      decl.Declaration.File = TGSI_FILE_INPUT;
      decl.Declaration.Semantic = 1;
      decl.Semantic.Name = sem_texcoord;
      decl.Declaration.Interpolate = 1;
      decl.Interp.Interpolate = TGSI_INTERPOLATE_PERSPECTIVE;
      decl.Range.First = decl.Range.Last = ctx->info.num_inputs;
      texcoord_index = ctx->info.num_inputs;
      tctx->emit_declaration(tctx, &decl);
   }

   /* Declare the drawpix sampler if it's missing. */
   if (!(ctx->info.samplers_declared & (1 << ctx->drawpix_sampler))) {
      decl = tgsi_default_full_declaration();
      decl.Declaration.File = TGSI_FILE_SAMPLER;
      decl.Range.First = decl.Range.Last = ctx->drawpix_sampler;
      tctx->emit_declaration(tctx, &decl);
   }

   /* Declare the pixel map sampler if it's missing. */
   if (ctx->pixel_maps &&
       !(ctx->info.samplers_declared & (1 << ctx->pixelmap_sampler))) {
      decl = tgsi_default_full_declaration();
      decl.Declaration.File = TGSI_FILE_SAMPLER;
      decl.Range.First = decl.Range.Last = ctx->pixelmap_sampler;
      tctx->emit_declaration(tctx, &decl);
   }

   /* Get initial pixel color from the texture.
    * TEX temp, fragment.texcoord[0], texture[0], 2D;
    */
   inst = tgsi_default_full_instruction();
   inst.Instruction.Opcode = TGSI_OPCODE_TEX;
   inst.Instruction.Texture = 1;
   inst.Texture.Texture = TGSI_TEXTURE_2D;

   inst.Instruction.NumDstRegs = 1;
   inst.Dst[0].Register.File  = TGSI_FILE_TEMPORARY;
   inst.Dst[0].Register.Index = ctx->color_temp;
   inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_XYZW;

   inst.Instruction.NumSrcRegs = 2;
   SET_SRC(&inst, 0, TGSI_FILE_INPUT, texcoord_index, X, Y, Z, W);
   inst.Src[1].Register.File  = TGSI_FILE_SAMPLER;
   inst.Src[1].Register.Index = ctx->drawpix_sampler;

   tctx->emit_instruction(tctx, &inst);

   /* Apply the scale and bias. */
   if (ctx->scale_and_bias) {
      /* MAD temp, temp, scale, bias; */
      inst = tgsi_default_full_instruction();
      inst.Instruction.Opcode = TGSI_OPCODE_MAD;

      inst.Instruction.NumDstRegs = 1;
      inst.Dst[0].Register.File  = TGSI_FILE_TEMPORARY;
      inst.Dst[0].Register.Index = ctx->color_temp;
      inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_XYZW;

      inst.Instruction.NumSrcRegs = 3;
      SET_SRC(&inst, 0, TGSI_FILE_TEMPORARY, ctx->color_temp, X, Y, Z, W);
      SET_SRC(&inst, 1, TGSI_FILE_CONSTANT, ctx->scale_const, X, Y, Z, W);
      SET_SRC(&inst, 2, TGSI_FILE_CONSTANT, ctx->bias_const, X, Y, Z, W);

      tctx->emit_instruction(tctx, &inst);
   }

   if (ctx->pixel_maps) {
      /* do four pixel map look-ups with two TEX instructions: */

      /* TEX temp.xy, temp.xyyy, texture[1], 2D; */
      inst = tgsi_default_full_instruction();
      inst.Instruction.Opcode = TGSI_OPCODE_TEX;
      inst.Instruction.Texture = 1;
      inst.Texture.Texture = TGSI_TEXTURE_2D;

      inst.Instruction.NumDstRegs = 1;
      inst.Dst[0].Register.File  = TGSI_FILE_TEMPORARY;
      inst.Dst[0].Register.Index = ctx->color_temp;
      inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_XY;

      inst.Instruction.NumSrcRegs = 2;
      SET_SRC(&inst, 0, TGSI_FILE_TEMPORARY, ctx->color_temp, X, Y, Y, Y);
      inst.Src[1].Register.File  = TGSI_FILE_SAMPLER;
      inst.Src[1].Register.Index = ctx->pixelmap_sampler;

      tctx->emit_instruction(tctx, &inst);

      /* TEX temp.zw, temp.zwww, texture[1], 2D; */
      inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_ZW;
      SET_SRC(&inst, 0, TGSI_FILE_TEMPORARY, ctx->color_temp, Z, W, W, W);
      tctx->emit_instruction(tctx, &inst);
   }

   /* Now, "color_temp" should be used in place of IN:COLOR0,
    * and CONST[texcoord_slot] should be used in place of IN:TEXCOORD0.
    */

transform_inst:

   for (i = 0; i < current_inst->Instruction.NumSrcRegs; i++) {
      struct tgsi_full_src_register *src = &current_inst->Src[i];
      unsigned reg = src->Register.Index;

      if (src->Register.File != TGSI_FILE_INPUT || src->Register.Indirect)
         continue;

      if (ctx->info.input_semantic_name[reg] == TGSI_SEMANTIC_COLOR &&
          ctx->info.input_semantic_index[reg] == 0) {
         src->Register.File = TGSI_FILE_TEMPORARY;
         src->Register.Index = ctx->color_temp;
      } else if (ctx->info.input_semantic_name[reg] == sem_texcoord &&
                 ctx->info.input_semantic_index[reg] == 0) {
         src->Register.File = TGSI_FILE_CONSTANT;
         src->Register.Index = ctx->texcoord_const;
      }
   }

   tctx->emit_instruction(tctx, current_inst);
}

const struct tgsi_token *
st_get_drawpix_shader(const struct tgsi_token *tokens, bool use_texcoord,
                      bool scale_and_bias, unsigned scale_const,
                      unsigned bias_const, bool pixel_maps,
                      unsigned drawpix_sampler, unsigned pixelmap_sampler,
                      unsigned texcoord_const)
{
   struct tgsi_drawpix_transform ctx;
   struct tgsi_token *newtoks;
   int newlen;

   memset(&ctx, 0, sizeof(ctx));
   ctx.base.transform_instruction = transform_instr;
   ctx.use_texcoord = use_texcoord;
   ctx.scale_and_bias = scale_and_bias;
   ctx.scale_const = scale_const;
   ctx.bias_const = bias_const;
   ctx.pixel_maps = pixel_maps;
   ctx.drawpix_sampler = drawpix_sampler;
   ctx.pixelmap_sampler = pixelmap_sampler;
   ctx.texcoord_const = texcoord_const;
   tgsi_scan_shader(tokens, &ctx.info);

   newlen = tgsi_num_tokens(tokens) + 30;
   newtoks = tgsi_alloc_tokens(newlen);
   if (!newtoks)
      return NULL;

   tgsi_transform_shader(tokens, newtoks, newlen, &ctx.base);
   return newtoks;
}
