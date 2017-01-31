/*
 * Copyright © 2016 Red Hat
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

#include <stdbool.h>

#include "st_tgsi_lower_yuv.h"
#include "tgsi/tgsi_transform.h"
#include "tgsi/tgsi_scan.h"
#include "tgsi/tgsi_dump.h"
#include "util/u_debug.h"

#include "util/bitscan.h"

struct tgsi_yuv_transform {
   struct tgsi_transform_context base;
   struct tgsi_shader_info info;
   struct tgsi_full_src_register imm[4];
   struct {
      struct tgsi_full_src_register src;
      struct tgsi_full_dst_register dst;
   } tmp[2];
#define A 0
#define B 1

   /* Maps a primary sampler (used for Y) to the U or UV sampler.  In
    * case of 3-plane YUV format, the V plane is next sampler after U.
    */
   unsigned char sampler_map[PIPE_MAX_SAMPLERS][2];

   bool first_instruction_emitted;
   unsigned free_slots;
   unsigned lower_nv12;
   unsigned lower_iyuv;
};

static inline struct tgsi_yuv_transform *
tgsi_yuv_transform(struct tgsi_transform_context *tctx)
{
   return (struct tgsi_yuv_transform *)tctx;
}

static void
reg_dst(struct tgsi_full_dst_register *dst,
        const struct tgsi_full_dst_register *orig_dst, unsigned wrmask)
{
   *dst = *orig_dst;
   dst->Register.WriteMask &= wrmask;
   assert(dst->Register.WriteMask);
}

static inline void
get_swiz(unsigned *swiz, const struct tgsi_src_register *src)
{
   swiz[0] = src->SwizzleX;
   swiz[1] = src->SwizzleY;
   swiz[2] = src->SwizzleZ;
   swiz[3] = src->SwizzleW;
}

static void
reg_src(struct tgsi_full_src_register *src,
        const struct tgsi_full_src_register *orig_src,
        unsigned sx, unsigned sy, unsigned sz, unsigned sw)
{
   unsigned swiz[4];
   get_swiz(swiz, &orig_src->Register);
   *src = *orig_src;
   src->Register.SwizzleX = swiz[sx];
   src->Register.SwizzleY = swiz[sy];
   src->Register.SwizzleZ = swiz[sz];
   src->Register.SwizzleW = swiz[sw];
}

#define TGSI_SWIZZLE__ TGSI_SWIZZLE_X  /* don't-care value! */
#define SWIZ(x,y,z,w) TGSI_SWIZZLE_ ## x, TGSI_SWIZZLE_ ## y,   \
      TGSI_SWIZZLE_ ## z, TGSI_SWIZZLE_ ## w

static inline struct tgsi_full_instruction
tex_instruction(unsigned samp)
{
   struct tgsi_full_instruction inst;

   inst = tgsi_default_full_instruction();
   inst.Instruction.Opcode = TGSI_OPCODE_TEX;
   inst.Instruction.Texture = 1;
   inst.Texture.Texture = TGSI_TEXTURE_2D;
   inst.Instruction.NumDstRegs = 1;
   inst.Instruction.NumSrcRegs = 2;
   inst.Src[1].Register.File  = TGSI_FILE_SAMPLER;
   inst.Src[1].Register.Index = samp;

   return inst;
}

static inline struct tgsi_full_instruction
mov_instruction(void)
{
   struct tgsi_full_instruction inst;

   inst = tgsi_default_full_instruction();
   inst.Instruction.Opcode = TGSI_OPCODE_MOV;
   inst.Instruction.Saturate = 0;
   inst.Instruction.NumDstRegs = 1;
   inst.Instruction.NumSrcRegs = 1;

   return inst;
}

static inline struct tgsi_full_instruction
dp3_instruction(void)
{
   struct tgsi_full_instruction inst;

   inst = tgsi_default_full_instruction();
   inst.Instruction.Opcode = TGSI_OPCODE_DP3;
   inst.Instruction.NumDstRegs = 1;
   inst.Instruction.NumSrcRegs = 2;

   return inst;
}



static void
emit_immed(struct tgsi_transform_context *tctx, int idx,
           float x, float y, float z, float w)
{
   struct tgsi_yuv_transform *ctx = tgsi_yuv_transform(tctx);
   struct tgsi_shader_info *info = &ctx->info;
   struct tgsi_full_immediate immed;

   immed = tgsi_default_full_immediate();
   immed.Immediate.NrTokens = 1 + 4; /* one for the token itself */
   immed.u[0].Float = x;
   immed.u[1].Float = y;
   immed.u[2].Float = z;
   immed.u[3].Float = w;
   tctx->emit_immediate(tctx, &immed);

   ctx->imm[idx].Register.File = TGSI_FILE_IMMEDIATE;
   ctx->imm[idx].Register.Index = info->immediate_count + idx;
   ctx->imm[idx].Register.SwizzleX = TGSI_SWIZZLE_X;
   ctx->imm[idx].Register.SwizzleY = TGSI_SWIZZLE_Y;
   ctx->imm[idx].Register.SwizzleZ = TGSI_SWIZZLE_Z;
   ctx->imm[idx].Register.SwizzleW = TGSI_SWIZZLE_W;
}

static void
emit_samp(struct tgsi_transform_context *tctx, unsigned samp)
{
   tgsi_transform_sampler_decl(tctx, samp);
   tgsi_transform_sampler_view_decl(tctx, samp, PIPE_TEXTURE_2D,
                                    TGSI_RETURN_TYPE_FLOAT);
}

/* Emit extra declarations we need:
 *  + 2 TEMP to hold intermediate results
 *  + 1 (for 2-plane YUV) or 2 (for 3-plane YUV) extra samplers per
 *    lowered YUV sampler
 *  + extra immediates for doing CSC
 */
static void
emit_decls(struct tgsi_transform_context *tctx)
{
   struct tgsi_yuv_transform *ctx = tgsi_yuv_transform(tctx);
   struct tgsi_shader_info *info = &ctx->info;
   unsigned mask, tempbase, i;
   struct tgsi_full_declaration decl;

   /*
    * Declare immediates for CSC conversion:
    */

   /* ITU-R BT.601 conversion */
   emit_immed(tctx, 0, 1.164,  0.000,  1.596,  0.0);
   emit_immed(tctx, 1, 1.164, -0.392, -0.813,  0.0);
   emit_immed(tctx, 2, 1.164,  2.017,  0.000,  0.0);
   emit_immed(tctx, 3, 0.0625, 0.500,  0.500,  1.0);

   /*
    * Declare extra samplers / sampler-views:
    */

   mask = ctx->lower_nv12 | ctx->lower_iyuv;
   while (mask) {
      unsigned extra, y_samp = u_bit_scan(&mask);

      extra = u_bit_scan(&ctx->free_slots);
      ctx->sampler_map[y_samp][0] = extra;
      emit_samp(tctx, extra);

      if (ctx->lower_iyuv & (1 << y_samp)) {
         extra = u_bit_scan(&ctx->free_slots);
         ctx->sampler_map[y_samp][1] = extra;
         emit_samp(tctx, extra);
      }
   }

   /*
    * Declare extra temp:
    */

   tempbase = info->file_max[TGSI_FILE_TEMPORARY] + 1;

   for (i = 0; i < 2; i++) {
      decl = tgsi_default_full_declaration();
      decl.Declaration.File = TGSI_FILE_TEMPORARY;
      decl.Range.First = decl.Range.Last = tempbase + i;
      tctx->emit_declaration(tctx, &decl);

      ctx->tmp[i].src.Register.File  = TGSI_FILE_TEMPORARY;
      ctx->tmp[i].src.Register.Index = tempbase + i;
      ctx->tmp[i].src.Register.SwizzleX = TGSI_SWIZZLE_X;
      ctx->tmp[i].src.Register.SwizzleY = TGSI_SWIZZLE_Y;
      ctx->tmp[i].src.Register.SwizzleZ = TGSI_SWIZZLE_Z;
      ctx->tmp[i].src.Register.SwizzleW = TGSI_SWIZZLE_W;

      ctx->tmp[i].dst.Register.File  = TGSI_FILE_TEMPORARY;
      ctx->tmp[i].dst.Register.Index = tempbase + i;
      ctx->tmp[i].dst.Register.WriteMask = TGSI_WRITEMASK_XYZW;
   }
}

/* call with YUV in tmpA.xyz */
static void
yuv_to_rgb(struct tgsi_transform_context *tctx,
           struct tgsi_full_dst_register *dst)
{
   struct tgsi_yuv_transform *ctx = tgsi_yuv_transform(tctx);
   struct tgsi_full_instruction inst;

   /*
    * IMM[0] FLT32 { 1.164,  0.000,  1.596,  0.0 }
    * IMM[1] FLT32 { 1.164, -0.392, -0.813,  0.0 }
    * IMM[2] FLT32 { 1.164,  2.017,  0.000,  0.0 }
    * IMM[3] FLT32 { 0.0625, 0.500,  0.500,  1.0 }
    */

   /* SUB tmpA.xyz, tmpA, imm[3] */
   inst = tgsi_default_full_instruction();
   inst.Instruction.Opcode = TGSI_OPCODE_ADD;
   inst.Instruction.Saturate = 0;
   inst.Instruction.NumDstRegs = 1;
   inst.Instruction.NumSrcRegs = 2;
   reg_dst(&inst.Dst[0], &ctx->tmp[A].dst, TGSI_WRITEMASK_XYZ);
   reg_src(&inst.Src[0], &ctx->tmp[A].src, SWIZ(X, Y, Z, _));
   reg_src(&inst.Src[1], &ctx->imm[3], SWIZ(X, Y, Z, _));
   inst.Src[1].Register.Negate = 1;
   tctx->emit_instruction(tctx, &inst);

   /* DP3 dst.x, tmpA, imm[0] */
   inst = dp3_instruction();
   reg_dst(&inst.Dst[0], dst, TGSI_WRITEMASK_X);
   reg_src(&inst.Src[0], &ctx->tmp[A].src, SWIZ(X, Y, Z, W));
   reg_src(&inst.Src[1], &ctx->imm[0], SWIZ(X, Y, Z, W));
   tctx->emit_instruction(tctx, &inst);

   /* DP3 dst.y, tmpA, imm[1] */
   inst = dp3_instruction();
   reg_dst(&inst.Dst[0], dst, TGSI_WRITEMASK_Y);
   reg_src(&inst.Src[0], &ctx->tmp[A].src, SWIZ(X, Y, Z, W));
   reg_src(&inst.Src[1], &ctx->imm[1], SWIZ(X, Y, Z, W));
   tctx->emit_instruction(tctx, &inst);

   /* DP3 dst.z, tmpA, imm[2] */
   inst = dp3_instruction();
   reg_dst(&inst.Dst[0], dst, TGSI_WRITEMASK_Z);
   reg_src(&inst.Src[0], &ctx->tmp[A].src, SWIZ(X, Y, Z, W));
   reg_src(&inst.Src[1], &ctx->imm[2], SWIZ(X, Y, Z, W));
   tctx->emit_instruction(tctx, &inst);

   /* MOV dst.w, imm[0].x */
   inst = mov_instruction();
   reg_dst(&inst.Dst[0], dst, TGSI_WRITEMASK_W);
   reg_src(&inst.Src[0], &ctx->imm[3], SWIZ(_, _, _, W));
   tctx->emit_instruction(tctx, &inst);
}

static void
lower_nv12(struct tgsi_transform_context *tctx,
           struct tgsi_full_instruction *originst)
{
   struct tgsi_yuv_transform *ctx = tgsi_yuv_transform(tctx);
   struct tgsi_full_instruction inst;
   struct tgsi_full_src_register *coord = &originst->Src[0];
   unsigned samp = originst->Src[1].Register.Index;

   /* sample Y:
    *    TEX tempA.x, coord, texture[samp], 2D;
    */
   inst = tex_instruction(samp);
   reg_dst(&inst.Dst[0], &ctx->tmp[A].dst, TGSI_WRITEMASK_X);
   reg_src(&inst.Src[0], coord, SWIZ(X, Y, Z, W));
   tctx->emit_instruction(tctx, &inst);

   /* sample UV:
    *    TEX tempB.xy, coord, texture[sampler_map[samp][0]], 2D;
    *    MOV tempA.yz, tempB._xy_
    */
   inst = tex_instruction(ctx->sampler_map[samp][0]);
   reg_dst(&inst.Dst[0], &ctx->tmp[B].dst, TGSI_WRITEMASK_XY);
   reg_src(&inst.Src[0], coord, SWIZ(X, Y, Z, W));
   tctx->emit_instruction(tctx, &inst);

   inst = mov_instruction();
   reg_dst(&inst.Dst[0], &ctx->tmp[A].dst, TGSI_WRITEMASK_YZ);
   reg_src(&inst.Src[0], &ctx->tmp[B].src, SWIZ(_, X, Y, _));
   tctx->emit_instruction(tctx, &inst);

   /* At this point, we have YUV in tempA.xyz, rest is common: */
   yuv_to_rgb(tctx, &originst->Dst[0]);
}

static void
lower_iyuv(struct tgsi_transform_context *tctx,
           struct tgsi_full_instruction *originst)
{
   struct tgsi_yuv_transform *ctx = tgsi_yuv_transform(tctx);
   struct tgsi_full_instruction inst;
   struct tgsi_full_src_register *coord = &originst->Src[0];
   unsigned samp = originst->Src[1].Register.Index;

   /* sample Y:
    *    TEX tempA.x, coord, texture[samp], 2D;
    */
   inst = tex_instruction(samp);
   reg_dst(&inst.Dst[0], &ctx->tmp[A].dst, TGSI_WRITEMASK_X);
   reg_src(&inst.Src[0], coord, SWIZ(X, Y, Z, W));
   tctx->emit_instruction(tctx, &inst);

   /* sample U:
    *    TEX tempB.x, coord, texture[sampler_map[samp][0]], 2D;
    *    MOV tempA.y, tempB._x__
    */
   inst = tex_instruction(ctx->sampler_map[samp][0]);
   reg_dst(&inst.Dst[0], &ctx->tmp[B].dst, TGSI_WRITEMASK_X);
   reg_src(&inst.Src[0], coord, SWIZ(X, Y, Z, W));
   tctx->emit_instruction(tctx, &inst);

   inst = mov_instruction();
   reg_dst(&inst.Dst[0], &ctx->tmp[A].dst, TGSI_WRITEMASK_Y);
   reg_src(&inst.Src[0], &ctx->tmp[B].src, SWIZ(_, X, _, _));
   tctx->emit_instruction(tctx, &inst);

   /* sample V:
    *    TEX tempB.x, coord, texture[sampler_map[samp][1]], 2D;
    *    MOV tempA.z, tempB.__x_
    */
   inst = tex_instruction(ctx->sampler_map[samp][1]);
   reg_dst(&inst.Dst[0], &ctx->tmp[B].dst, TGSI_WRITEMASK_X);
   reg_src(&inst.Src[0], coord, SWIZ(X, Y, Z, W));
   tctx->emit_instruction(tctx, &inst);

   inst = mov_instruction();
   reg_dst(&inst.Dst[0], &ctx->tmp[A].dst, TGSI_WRITEMASK_Z);
   reg_src(&inst.Src[0], &ctx->tmp[B].src, SWIZ(_, _, X, _));
   tctx->emit_instruction(tctx, &inst);

   /* At this point, we have YUV in tempA.xyz, rest is common: */
   yuv_to_rgb(tctx, &originst->Dst[0]);
}

static void
transform_instr(struct tgsi_transform_context *tctx,
                struct tgsi_full_instruction *inst)
{
   struct tgsi_yuv_transform *ctx = tgsi_yuv_transform(tctx);

   if (!ctx->first_instruction_emitted) {
      emit_decls(tctx);
      ctx->first_instruction_emitted = true;
   }

   switch (inst->Instruction.Opcode) {
   /* TODO what other tex opcode's can be used w/ external eglimgs? */
   case TGSI_OPCODE_TEX: {
      unsigned samp = inst->Src[1].Register.Index;
      if (ctx->lower_nv12 & (1 << samp)) {
         lower_nv12(tctx, inst);
      } else if (ctx->lower_iyuv & (1 << samp)) {
         lower_iyuv(tctx, inst);
      } else {
         goto skip;
      }
      break;
   }
   default:
   skip:
      tctx->emit_instruction(tctx, inst);
      return;
   }
}

extern const struct tgsi_token *
st_tgsi_lower_yuv(const struct tgsi_token *tokens, unsigned free_slots,
                  unsigned lower_nv12, unsigned lower_iyuv)
{
   struct tgsi_yuv_transform ctx;
   struct tgsi_token *newtoks;
   int newlen;

   assert(!(lower_nv12 & lower_iyuv)); /* bitmasks should be mutually exclusive */

//   tgsi_dump(tokens, 0);
//   debug_printf("\n");

   memset(&ctx, 0, sizeof(ctx));
   ctx.base.transform_instruction = transform_instr;
   ctx.free_slots = free_slots;
   ctx.lower_nv12 = lower_nv12;
   ctx.lower_iyuv = lower_iyuv;
   tgsi_scan_shader(tokens, &ctx.info);

   /* TODO better job of figuring out how many extra tokens we need..
    * this is a pain about tgsi_transform :-/
    */
   newlen = tgsi_num_tokens(tokens) + 120;
   newtoks = tgsi_alloc_tokens(newlen);
   if (!newtoks)
      return NULL;

   tgsi_transform_shader(tokens, newtoks, newlen, &ctx.base);

//   tgsi_dump(newtoks, 0);
//   debug_printf("\n");

   return newtoks;
}
