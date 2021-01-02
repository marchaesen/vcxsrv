/*
 * Copyright (C) 2015 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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
 *
 */

#include "tgsi/tgsi_transform.h"
#include "tgsi/tgsi_scan.h"
#include "tgsi/tgsi_dump.h"
#include "util/u_debug.h"

#include "tgsi_emulate.h"

struct tgsi_emulation_context {
   struct tgsi_transform_context base;
   struct tgsi_shader_info info;
   unsigned flags;
   bool first_instruction_emitted;
};

static inline struct tgsi_emulation_context *
tgsi_emulation_context(struct tgsi_transform_context *tctx)
{
   return (struct tgsi_emulation_context *)tctx;
}

static void
transform_decl(struct tgsi_transform_context *tctx,
               struct tgsi_full_declaration *decl)
{
   struct tgsi_emulation_context *ctx = tgsi_emulation_context(tctx);

   if (ctx->flags & TGSI_EMU_FORCE_PERSAMPLE_INTERP &&
       decl->Declaration.File == TGSI_FILE_INPUT) {
      assert(decl->Declaration.Interpolate);
      decl->Interp.Location = TGSI_INTERPOLATE_LOC_SAMPLE;
   }

   tctx->emit_declaration(tctx, decl);
}

static void
passthrough_edgeflag(struct tgsi_transform_context *tctx)
{
   struct tgsi_emulation_context *ctx = tgsi_emulation_context(tctx);
   struct tgsi_full_declaration decl;
   struct tgsi_full_instruction new_inst;

   /* Input */
   decl = tgsi_default_full_declaration();
   decl.Declaration.File = TGSI_FILE_INPUT;
   decl.Range.First = decl.Range.Last = ctx->info.num_inputs;
   tctx->emit_declaration(tctx, &decl);

   /* Output */
   decl = tgsi_default_full_declaration();
   decl.Declaration.File = TGSI_FILE_OUTPUT;
   decl.Declaration.Semantic = true;
   decl.Range.First = decl.Range.Last = ctx->info.num_outputs;
   decl.Semantic.Name = TGSI_SEMANTIC_EDGEFLAG;
   decl.Semantic.Index = 0;
   tctx->emit_declaration(tctx, &decl);

   /* MOV */
   new_inst = tgsi_default_full_instruction();
   new_inst.Instruction.Opcode = TGSI_OPCODE_MOV;

   new_inst.Instruction.NumDstRegs = 1;
   new_inst.Dst[0].Register.File  = TGSI_FILE_OUTPUT;
   new_inst.Dst[0].Register.Index = ctx->info.num_outputs;
   new_inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_XYZW;

   new_inst.Instruction.NumSrcRegs = 1;
   new_inst.Src[0].Register.File  = TGSI_FILE_INPUT;
   new_inst.Src[0].Register.Index = ctx->info.num_inputs;
   new_inst.Src[0].Register.SwizzleX = TGSI_SWIZZLE_X;
   new_inst.Src[0].Register.SwizzleY = TGSI_SWIZZLE_X;
   new_inst.Src[0].Register.SwizzleZ = TGSI_SWIZZLE_X;
   new_inst.Src[0].Register.SwizzleW = TGSI_SWIZZLE_X;

   tctx->emit_instruction(tctx, &new_inst);
}

static void
transform_instr(struct tgsi_transform_context *tctx,
                struct tgsi_full_instruction *inst)
{
   struct tgsi_emulation_context *ctx = tgsi_emulation_context(tctx);

   /* Pass through edgeflags. */
   if (!ctx->first_instruction_emitted) {
      ctx->first_instruction_emitted = true;

      if (ctx->flags & TGSI_EMU_PASSTHROUGH_EDGEFLAG)
         passthrough_edgeflag(tctx);
   }

   /* Clamp color outputs. */
   if (ctx->flags & TGSI_EMU_CLAMP_COLOR_OUTPUTS) {
      int i;
      for (i = 0; i < inst->Instruction.NumDstRegs; i++) {
         unsigned semantic;

         if (inst->Dst[i].Register.File != TGSI_FILE_OUTPUT ||
             inst->Dst[i].Register.Indirect)
            continue;

         semantic =
            ctx->info.output_semantic_name[inst->Dst[i].Register.Index];

         if (semantic == TGSI_SEMANTIC_COLOR ||
             semantic == TGSI_SEMANTIC_BCOLOR)
            inst->Instruction.Saturate = true;
      }
   }

   tctx->emit_instruction(tctx, inst);
}

const struct tgsi_token *
tgsi_emulate(const struct tgsi_token *tokens, unsigned flags)
{
   struct tgsi_emulation_context ctx;
   struct tgsi_token *newtoks;
   int newlen;

   if (!(flags & (TGSI_EMU_CLAMP_COLOR_OUTPUTS |
                  TGSI_EMU_PASSTHROUGH_EDGEFLAG |
                  TGSI_EMU_FORCE_PERSAMPLE_INTERP)))
      return NULL;

   memset(&ctx, 0, sizeof(ctx));
   ctx.flags = flags;
   tgsi_scan_shader(tokens, &ctx.info);

   if (flags & TGSI_EMU_FORCE_PERSAMPLE_INTERP)
      ctx.base.transform_declaration = transform_decl;

   if (flags & (TGSI_EMU_CLAMP_COLOR_OUTPUTS |
                TGSI_EMU_PASSTHROUGH_EDGEFLAG))
      ctx.base.transform_instruction = transform_instr;

   newlen = tgsi_num_tokens(tokens) + 20;
   newtoks = tgsi_alloc_tokens(newlen);
   if (!newtoks)
      return NULL;

   tgsi_transform_shader(tokens, newtoks, newlen, &ctx.base);
   return newtoks;
}
