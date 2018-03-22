/**************************************************************************
 *
 * Copyright 2007-2008 VMware, Inc.
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

/*
 * \author
 * Michal Krol,
 * Keith Whitwell
 */

#include "pipe/p_compiler.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_shader_tokens.h"
#include "pipe/p_state.h"
#include "tgsi/tgsi_ureg.h"
#include "st_mesa_to_tgsi.h"
#include "st_context.h"
#include "program/prog_instruction.h"
#include "program/prog_parameter.h"
#include "util/u_debug.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "st_glsl_to_tgsi.h" /* for _mesa_sysval_to_semantic */


#define PROGRAM_ANY_CONST ((1 << PROGRAM_STATE_VAR) |    \
                           (1 << PROGRAM_CONSTANT) |     \
                           (1 << PROGRAM_UNIFORM))

/**
 * Intermediate state used during shader translation.
 */
struct st_translate {
   struct ureg_program *ureg;

   struct ureg_dst temps[MAX_PROGRAM_TEMPS];
   struct ureg_src *constants;
   struct ureg_dst outputs[PIPE_MAX_SHADER_OUTPUTS];
   struct ureg_src inputs[PIPE_MAX_SHADER_INPUTS];
   struct ureg_dst address[1];
   struct ureg_src samplers[PIPE_MAX_SAMPLERS];
   struct ureg_src systemValues[SYSTEM_VALUE_MAX];

   const ubyte *inputMapping;
   const ubyte *outputMapping;

   unsigned procType;  /**< PIPE_SHADER_VERTEX/FRAGMENT */
};


/**
 * Map a Mesa dst register to a TGSI ureg_dst register.
 */
static struct ureg_dst
dst_register(struct st_translate *t, gl_register_file file, GLuint index)
{
   switch(file) {
   case PROGRAM_UNDEFINED:
      return ureg_dst_undef();

   case PROGRAM_TEMPORARY:
      if (ureg_dst_is_undef(t->temps[index]))
         t->temps[index] = ureg_DECL_temporary(t->ureg);

      return t->temps[index];

   case PROGRAM_OUTPUT:
      if (t->procType == PIPE_SHADER_VERTEX)
         assert(index < VARYING_SLOT_MAX);
      else if (t->procType == PIPE_SHADER_FRAGMENT)
         assert(index < FRAG_RESULT_MAX);
      else
         assert(index < VARYING_SLOT_MAX);

      assert(t->outputMapping[index] < ARRAY_SIZE(t->outputs));

      return t->outputs[t->outputMapping[index]];

   case PROGRAM_ADDRESS:
      return t->address[index];

   default:
      debug_assert(0);
      return ureg_dst_undef();
   }
}


/**
 * Map a Mesa src register to a TGSI ureg_src register.
 */
static struct ureg_src
src_register(struct st_translate *t,
              gl_register_file file,
              GLint index)
{
   switch(file) {
   case PROGRAM_UNDEFINED:
      return ureg_src_undef();

   case PROGRAM_TEMPORARY:
      assert(index >= 0);
      assert(index < ARRAY_SIZE(t->temps));
      if (ureg_dst_is_undef(t->temps[index]))
         t->temps[index] = ureg_DECL_temporary(t->ureg);
      return ureg_src(t->temps[index]);

   case PROGRAM_UNIFORM:
      assert(index >= 0);
      return t->constants[index];
   case PROGRAM_STATE_VAR:
   case PROGRAM_CONSTANT:       /* ie, immediate */
      if (index < 0)
         return ureg_DECL_constant(t->ureg, 0);
      else
         return t->constants[index];

   case PROGRAM_INPUT:
      assert(t->inputMapping[index] < ARRAY_SIZE(t->inputs));
      return t->inputs[t->inputMapping[index]];

   case PROGRAM_OUTPUT:
      assert(t->outputMapping[index] < ARRAY_SIZE(t->outputs));
      return ureg_src(t->outputs[t->outputMapping[index]]); /* not needed? */

   case PROGRAM_ADDRESS:
      return ureg_src(t->address[index]);

   case PROGRAM_SYSTEM_VALUE:
      assert(index < ARRAY_SIZE(t->systemValues));
      return t->systemValues[index];

   default:
      debug_assert(0);
      return ureg_src_undef();
   }
}


/**
 * Map mesa texture target to TGSI texture target.
 */
enum tgsi_texture_type
st_translate_texture_target(gl_texture_index textarget, GLboolean shadow)
{
   if (shadow) {
      switch (textarget) {
      case TEXTURE_1D_INDEX:
         return TGSI_TEXTURE_SHADOW1D;
      case TEXTURE_2D_INDEX:
         return TGSI_TEXTURE_SHADOW2D;
      case TEXTURE_RECT_INDEX:
         return TGSI_TEXTURE_SHADOWRECT;
      case TEXTURE_1D_ARRAY_INDEX:
         return TGSI_TEXTURE_SHADOW1D_ARRAY;
      case TEXTURE_2D_ARRAY_INDEX:
         return TGSI_TEXTURE_SHADOW2D_ARRAY;
      case TEXTURE_CUBE_INDEX:
         return TGSI_TEXTURE_SHADOWCUBE;
      case TEXTURE_CUBE_ARRAY_INDEX:
         return TGSI_TEXTURE_SHADOWCUBE_ARRAY;
      default:
         break;
      }
   }

   switch (textarget) {
   case TEXTURE_2D_MULTISAMPLE_INDEX:
      return TGSI_TEXTURE_2D_MSAA;
   case TEXTURE_2D_MULTISAMPLE_ARRAY_INDEX:
      return TGSI_TEXTURE_2D_ARRAY_MSAA;
   case TEXTURE_BUFFER_INDEX:
      return TGSI_TEXTURE_BUFFER;
   case TEXTURE_1D_INDEX:
      return TGSI_TEXTURE_1D;
   case TEXTURE_2D_INDEX:
      return TGSI_TEXTURE_2D;
   case TEXTURE_3D_INDEX:
      return TGSI_TEXTURE_3D;
   case TEXTURE_CUBE_INDEX:
      return TGSI_TEXTURE_CUBE;
   case TEXTURE_CUBE_ARRAY_INDEX:
      return TGSI_TEXTURE_CUBE_ARRAY;
   case TEXTURE_RECT_INDEX:
      return TGSI_TEXTURE_RECT;
   case TEXTURE_1D_ARRAY_INDEX:
      return TGSI_TEXTURE_1D_ARRAY;
   case TEXTURE_2D_ARRAY_INDEX:
      return TGSI_TEXTURE_2D_ARRAY;
   case TEXTURE_EXTERNAL_INDEX:
      return TGSI_TEXTURE_2D;
   default:
      debug_assert(!"unexpected texture target index");
      return TGSI_TEXTURE_1D;
   }
}


/**
 * Map GLSL base type to TGSI return type.
 */
enum tgsi_return_type
st_translate_texture_type(enum glsl_base_type type)
{
   switch (type) {
   case GLSL_TYPE_INT:
      return TGSI_RETURN_TYPE_SINT;
   case GLSL_TYPE_UINT:
      return TGSI_RETURN_TYPE_UINT;
   case GLSL_TYPE_FLOAT:
      return TGSI_RETURN_TYPE_FLOAT;
   default:
      assert(!"unexpected texture type");
      return TGSI_RETURN_TYPE_UNKNOWN;
   }
}


/**
 * Translate a (1 << TEXTURE_x_INDEX) bit into a TGSI_TEXTURE_x enum.
 */
static unsigned
translate_texture_index(GLbitfield texBit, bool shadow)
{
   int index = ffs(texBit);
   assert(index > 0);
   assert(index - 1 < NUM_TEXTURE_TARGETS);
   return st_translate_texture_target(index - 1, shadow);
}


/**
 * Create a TGSI ureg_dst register from a Mesa dest register.
 */
static struct ureg_dst
translate_dst(struct st_translate *t,
              const struct prog_dst_register *DstReg,
              boolean saturate)
{
   struct ureg_dst dst = dst_register(t, DstReg->File, DstReg->Index);

   dst = ureg_writemask(dst, DstReg->WriteMask);

   if (saturate)
      dst = ureg_saturate(dst);

   if (DstReg->RelAddr)
      dst = ureg_dst_indirect(dst, ureg_src(t->address[0]));

   return dst;
}


/**
 * Create a TGSI ureg_src register from a Mesa src register.
 */
static struct ureg_src
translate_src(struct st_translate *t,
              const struct prog_src_register *SrcReg)
{
   struct ureg_src src = src_register(t, SrcReg->File, SrcReg->Index);

   src = ureg_swizzle(src,
                      GET_SWZ(SrcReg->Swizzle, 0) & 0x3,
                      GET_SWZ(SrcReg->Swizzle, 1) & 0x3,
                      GET_SWZ(SrcReg->Swizzle, 2) & 0x3,
                      GET_SWZ(SrcReg->Swizzle, 3) & 0x3);

   if (SrcReg->Negate == NEGATE_XYZW)
      src = ureg_negate(src);

   if (SrcReg->RelAddr) {
      src = ureg_src_indirect(src, ureg_src(t->address[0]));
      if (SrcReg->File != PROGRAM_INPUT &&
          SrcReg->File != PROGRAM_OUTPUT) {
         /* If SrcReg->Index was negative, it was set to zero in
          * src_register().  Reassign it now.  But don't do this
          * for input/output regs since they get remapped while
          * const buffers don't.
          */
         src.Index = SrcReg->Index;
      }
   }

   return src;
}


static struct ureg_src
swizzle_4v(struct ureg_src src, const unsigned *swz)
{
   return ureg_swizzle(src, swz[0], swz[1], swz[2], swz[3]);
}


/**
 * Translate a SWZ instruction into a MOV, MUL or MAD instruction.  EG:
 *
 *   SWZ dst, src.x-y10
 *
 * becomes:
 *
 *   MAD dst {1,-1,0,0}, src.xyxx, {0,0,1,0}
 */
static void
emit_swz(struct st_translate *t,
         struct ureg_dst dst,
         const struct prog_src_register *SrcReg)
{
   struct ureg_program *ureg = t->ureg;
   struct ureg_src src = src_register(t, SrcReg->File, SrcReg->Index);

   unsigned negate_mask =  SrcReg->Negate;

   unsigned one_mask = ((GET_SWZ(SrcReg->Swizzle, 0) == SWIZZLE_ONE) << 0 |
                        (GET_SWZ(SrcReg->Swizzle, 1) == SWIZZLE_ONE) << 1 |
                        (GET_SWZ(SrcReg->Swizzle, 2) == SWIZZLE_ONE) << 2 |
                        (GET_SWZ(SrcReg->Swizzle, 3) == SWIZZLE_ONE) << 3);

   unsigned zero_mask = ((GET_SWZ(SrcReg->Swizzle, 0) == SWIZZLE_ZERO) << 0 |
                         (GET_SWZ(SrcReg->Swizzle, 1) == SWIZZLE_ZERO) << 1 |
                         (GET_SWZ(SrcReg->Swizzle, 2) == SWIZZLE_ZERO) << 2 |
                         (GET_SWZ(SrcReg->Swizzle, 3) == SWIZZLE_ZERO) << 3);

   unsigned negative_one_mask = one_mask & negate_mask;
   unsigned positive_one_mask = one_mask & ~negate_mask;

   struct ureg_src imm;
   unsigned i;
   unsigned mul_swizzle[4] = {0,0,0,0};
   unsigned add_swizzle[4] = {0,0,0,0};
   unsigned src_swizzle[4] = {0,0,0,0};
   boolean need_add = FALSE;
   boolean need_mul = FALSE;

   if (dst.WriteMask == 0)
      return;

   /* Is this just a MOV?
    */
   if (zero_mask == 0 &&
       one_mask == 0 &&
       (negate_mask == 0 || negate_mask == TGSI_WRITEMASK_XYZW)) {
      ureg_MOV(ureg, dst, translate_src(t, SrcReg));
      return;
   }

#define IMM_ZERO    0
#define IMM_ONE     1
#define IMM_NEG_ONE 2

   imm = ureg_imm3f(ureg, 0, 1, -1);

   for (i = 0; i < 4; i++) {
      unsigned bit = 1 << i;

      if (dst.WriteMask & bit) {
         if (positive_one_mask & bit) {
            mul_swizzle[i] = IMM_ZERO;
            add_swizzle[i] = IMM_ONE;
            need_add = TRUE;
         }
         else if (negative_one_mask & bit) {
            mul_swizzle[i] = IMM_ZERO;
            add_swizzle[i] = IMM_NEG_ONE;
            need_add = TRUE;
         }
         else if (zero_mask & bit) {
            mul_swizzle[i] = IMM_ZERO;
            add_swizzle[i] = IMM_ZERO;
            need_add = TRUE;
         }
         else {
            add_swizzle[i] = IMM_ZERO;
            src_swizzle[i] = GET_SWZ(SrcReg->Swizzle, i);
            need_mul = TRUE;
            if (negate_mask & bit) {
               mul_swizzle[i] = IMM_NEG_ONE;
            }
            else {
               mul_swizzle[i] = IMM_ONE;
            }
         }
      }
   }

   if (need_mul && need_add) {
      ureg_MAD(ureg,
               dst,
               swizzle_4v(src, src_swizzle),
               swizzle_4v(imm, mul_swizzle),
               swizzle_4v(imm, add_swizzle));
   }
   else if (need_mul) {
      ureg_MUL(ureg,
               dst,
               swizzle_4v(src, src_swizzle),
               swizzle_4v(imm, mul_swizzle));
   }
   else if (need_add) {
      ureg_MOV(ureg,
               dst,
               swizzle_4v(imm, add_swizzle));
   }
   else {
      debug_assert(0);
   }

#undef IMM_ZERO
#undef IMM_ONE
#undef IMM_NEG_ONE
}


static unsigned
translate_opcode(unsigned op)
{
   switch(op) {
   case OPCODE_ARL:
      return TGSI_OPCODE_ARL;
   case OPCODE_ADD:
      return TGSI_OPCODE_ADD;
   case OPCODE_CMP:
      return TGSI_OPCODE_CMP;
   case OPCODE_COS:
      return TGSI_OPCODE_COS;
   case OPCODE_DP3:
      return TGSI_OPCODE_DP3;
   case OPCODE_DP4:
      return TGSI_OPCODE_DP4;
   case OPCODE_DST:
      return TGSI_OPCODE_DST;
   case OPCODE_EX2:
      return TGSI_OPCODE_EX2;
   case OPCODE_EXP:
      return TGSI_OPCODE_EXP;
   case OPCODE_FLR:
      return TGSI_OPCODE_FLR;
   case OPCODE_FRC:
      return TGSI_OPCODE_FRC;
   case OPCODE_KIL:
      return TGSI_OPCODE_KILL_IF;
   case OPCODE_LG2:
      return TGSI_OPCODE_LG2;
   case OPCODE_LOG:
      return TGSI_OPCODE_LOG;
   case OPCODE_LIT:
      return TGSI_OPCODE_LIT;
   case OPCODE_LRP:
      return TGSI_OPCODE_LRP;
   case OPCODE_MAD:
      return TGSI_OPCODE_MAD;
   case OPCODE_MAX:
      return TGSI_OPCODE_MAX;
   case OPCODE_MIN:
      return TGSI_OPCODE_MIN;
   case OPCODE_MOV:
      return TGSI_OPCODE_MOV;
   case OPCODE_MUL:
      return TGSI_OPCODE_MUL;
   case OPCODE_POW:
      return TGSI_OPCODE_POW;
   case OPCODE_RCP:
      return TGSI_OPCODE_RCP;
   case OPCODE_SGE:
      return TGSI_OPCODE_SGE;
   case OPCODE_SIN:
      return TGSI_OPCODE_SIN;
   case OPCODE_SLT:
      return TGSI_OPCODE_SLT;
   case OPCODE_TEX:
      return TGSI_OPCODE_TEX;
   case OPCODE_TXB:
      return TGSI_OPCODE_TXB;
   case OPCODE_TXP:
      return TGSI_OPCODE_TXP;
   case OPCODE_END:
      return TGSI_OPCODE_END;
   default:
      debug_assert(0);
      return TGSI_OPCODE_NOP;
   }
}


static void
compile_instruction(struct gl_context *ctx,
                    struct st_translate *t,
                    const struct prog_instruction *inst)
{
   struct ureg_program *ureg = t->ureg;
   GLuint i;
   struct ureg_dst dst[1] = { { 0 } };
   struct ureg_src src[4];
   unsigned num_dst;
   unsigned num_src;

   num_dst = _mesa_num_inst_dst_regs(inst->Opcode);
   num_src = _mesa_num_inst_src_regs(inst->Opcode);

   if (num_dst)
      dst[0] = translate_dst(t, &inst->DstReg, inst->Saturate);

   for (i = 0; i < num_src; i++)
      src[i] = translate_src(t, &inst->SrcReg[i]);

   switch(inst->Opcode) {
   case OPCODE_SWZ:
      emit_swz(t, dst[0], &inst->SrcReg[0]);
      return;

   case OPCODE_TEX:
   case OPCODE_TXB:
   case OPCODE_TXP:
      src[num_src++] = t->samplers[inst->TexSrcUnit];
      ureg_tex_insn(ureg,
                    translate_opcode(inst->Opcode),
                    dst, num_dst,
                    st_translate_texture_target(inst->TexSrcTarget,
                                                inst->TexShadow),
                    TGSI_RETURN_TYPE_FLOAT,
                    NULL, 0,
                    src, num_src);
      return;

   case OPCODE_SCS:
      ureg_COS(ureg, ureg_writemask(dst[0], TGSI_WRITEMASK_X),
               ureg_scalar(src[0], TGSI_SWIZZLE_X));
      ureg_SIN(ureg, ureg_writemask(dst[0], TGSI_WRITEMASK_Y),
               ureg_scalar(src[0], TGSI_SWIZZLE_X));
      break;

   case OPCODE_XPD: {
      struct ureg_dst tmp = ureg_DECL_temporary(ureg);

      ureg_MUL(ureg, ureg_writemask(tmp, TGSI_WRITEMASK_XYZ),
               ureg_swizzle(src[0], TGSI_SWIZZLE_Y, TGSI_SWIZZLE_Z,
                            TGSI_SWIZZLE_X, 0),
               ureg_swizzle(src[1], TGSI_SWIZZLE_Z, TGSI_SWIZZLE_X,
                            TGSI_SWIZZLE_Y, 0));
      ureg_MAD(ureg, ureg_writemask(dst[0], TGSI_WRITEMASK_XYZ),
               ureg_swizzle(src[0], TGSI_SWIZZLE_Z, TGSI_SWIZZLE_X,
                            TGSI_SWIZZLE_Y, 0),
               ureg_negate(ureg_swizzle(src[1], TGSI_SWIZZLE_Y,
                                        TGSI_SWIZZLE_Z, TGSI_SWIZZLE_X, 0)),
               ureg_src(tmp));
      break;
   }

   case OPCODE_RSQ:
      ureg_RSQ(ureg, dst[0], ureg_abs(src[0]));
      break;

   case OPCODE_ABS:
      ureg_MOV(ureg, dst[0], ureg_abs(src[0]));
      break;

   case OPCODE_SUB:
      ureg_ADD(ureg, dst[0], src[0], ureg_negate(src[1]));
      break;

   case OPCODE_DPH: {
      struct ureg_dst temp = ureg_DECL_temporary(ureg);

      /* DPH = DP4(src0, src1) where src0.w = 1. */
      ureg_MOV(ureg, ureg_writemask(temp, TGSI_WRITEMASK_XYZ), src[0]);
      ureg_MOV(ureg, ureg_writemask(temp, TGSI_WRITEMASK_W),
               ureg_imm1f(ureg, 1));
      ureg_DP4(ureg, dst[0], ureg_src(temp), src[1]);
      break;
   }

   default:
      ureg_insn(ureg,
                 translate_opcode(inst->Opcode),
                 dst, num_dst,
                 src, num_src, 0);
      break;
   }
}


/**
 * Emit the TGSI instructions for inverting and adjusting WPOS.
 * This code is unavoidable because it also depends on whether
 * a FBO is bound (STATE_FB_WPOS_Y_TRANSFORM).
 */
static void
emit_wpos_adjustment(struct gl_context *ctx,
                     struct st_translate *t,
                     const struct gl_program *program,
                     boolean invert,
                     GLfloat adjX, GLfloat adjY[2])
{
   struct ureg_program *ureg = t->ureg;

   /* Fragment program uses fragment position input.
    * Need to replace instances of INPUT[WPOS] with temp T
    * where T = INPUT[WPOS] by y is inverted.
    */
   static const gl_state_index16 wposTransformState[STATE_LENGTH]
      = { STATE_INTERNAL, STATE_FB_WPOS_Y_TRANSFORM, 0, 0, 0 };

   /* XXX: note we are modifying the incoming shader here!  Need to
    * do this before emitting the constant decls below, or this
    * will be missed:
    */
   unsigned wposTransConst = _mesa_add_state_reference(program->Parameters,
                                                       wposTransformState);

   struct ureg_src wpostrans = ureg_DECL_constant(ureg, wposTransConst);
   struct ureg_dst wpos_temp = ureg_DECL_temporary(ureg);
   struct ureg_src *wpos =
      ctx->Const.GLSLFragCoordIsSysVal ?
         &t->systemValues[SYSTEM_VALUE_FRAG_COORD] :
         &t->inputs[t->inputMapping[VARYING_SLOT_POS]];
   struct ureg_src wpos_input = *wpos;

   /* First, apply the coordinate shift: */
   if (adjX || adjY[0] || adjY[1]) {
      if (adjY[0] != adjY[1]) {
         /* Adjust the y coordinate by adjY[1] or adjY[0] respectively
          * depending on whether inversion is actually going to be applied
          * or not, which is determined by testing against the inversion
          * state variable used below, which will be either +1 or -1.
          */
         struct ureg_dst adj_temp = ureg_DECL_temporary(ureg);

         ureg_CMP(ureg, adj_temp,
                  ureg_scalar(wpostrans, invert ? 2 : 0),
                  ureg_imm4f(ureg, adjX, adjY[0], 0.0f, 0.0f),
                  ureg_imm4f(ureg, adjX, adjY[1], 0.0f, 0.0f));
         ureg_ADD(ureg, wpos_temp, wpos_input, ureg_src(adj_temp));
      } else {
         ureg_ADD(ureg, wpos_temp, wpos_input,
                  ureg_imm4f(ureg, adjX, adjY[0], 0.0f, 0.0f));
      }
      wpos_input = ureg_src(wpos_temp);
   } else {
      /* MOV wpos_temp, input[wpos]
       */
      ureg_MOV(ureg, wpos_temp, wpos_input);
   }

   /* Now the conditional y flip: STATE_FB_WPOS_Y_TRANSFORM.xy/zw will be
    * inversion/identity, or the other way around if we're drawing to an FBO.
    */
   if (invert) {
      /* MAD wpos_temp.y, wpos_input, wpostrans.xxxx, wpostrans.yyyy
       */
      ureg_MAD(ureg,
                ureg_writemask(wpos_temp, TGSI_WRITEMASK_Y),
                wpos_input,
                ureg_scalar(wpostrans, 0),
                ureg_scalar(wpostrans, 1));
   } else {
      /* MAD wpos_temp.y, wpos_input, wpostrans.zzzz, wpostrans.wwww
       */
      ureg_MAD(ureg,
                ureg_writemask(wpos_temp, TGSI_WRITEMASK_Y),
                wpos_input,
                ureg_scalar(wpostrans, 2),
                ureg_scalar(wpostrans, 3));
   }

   /* Use wpos_temp as position input from here on:
    */
   *wpos = ureg_src(wpos_temp);
}


/**
 * Emit fragment position/coordinate code.
 */
static void
emit_wpos(struct st_context *st,
          struct st_translate *t,
          const struct gl_program *program,
          struct ureg_program *ureg)
{
   struct pipe_screen *pscreen = st->pipe->screen;
   GLfloat adjX = 0.0f;
   GLfloat adjY[2] = { 0.0f, 0.0f };
   boolean invert = FALSE;

   /* Query the pixel center conventions supported by the pipe driver and set
    * adjX, adjY to help out if it cannot handle the requested one internally.
    *
    * The bias of the y-coordinate depends on whether y-inversion takes place
    * (adjY[1]) or not (adjY[0]), which is in turn dependent on whether we are
    * drawing to an FBO (causes additional inversion), and whether the pipe
    * driver origin and the requested origin differ (the latter condition is
    * stored in the 'invert' variable).
    *
    * For height = 100 (i = integer, h = half-integer, l = lower, u = upper):
    *
    * center shift only:
    * i -> h: +0.5
    * h -> i: -0.5
    *
    * inversion only:
    * l,i -> u,i: ( 0.0 + 1.0) * -1 + 100 = 99
    * l,h -> u,h: ( 0.5 + 0.0) * -1 + 100 = 99.5
    * u,i -> l,i: (99.0 + 1.0) * -1 + 100 = 0
    * u,h -> l,h: (99.5 + 0.0) * -1 + 100 = 0.5
    *
    * inversion and center shift:
    * l,i -> u,h: ( 0.0 + 0.5) * -1 + 100 = 99.5
    * l,h -> u,i: ( 0.5 + 0.5) * -1 + 100 = 99
    * u,i -> l,h: (99.0 + 0.5) * -1 + 100 = 0.5
    * u,h -> l,i: (99.5 + 0.5) * -1 + 100 = 0
    */
   if (program->OriginUpperLeft) {
      /* Fragment shader wants origin in upper-left */
      if (pscreen->get_param(pscreen,
                             PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT)) {
         /* the driver supports upper-left origin */
      }
      else if (pscreen->get_param(pscreen,
                                  PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT)) {
         /* the driver supports lower-left origin, need to invert Y */
         ureg_property(ureg, TGSI_PROPERTY_FS_COORD_ORIGIN,
                       TGSI_FS_COORD_ORIGIN_LOWER_LEFT);
         invert = TRUE;
      }
      else
         assert(0);
   }
   else {
      /* Fragment shader wants origin in lower-left */
      if (pscreen->get_param(pscreen, PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT))
         /* the driver supports lower-left origin */
         ureg_property(ureg, TGSI_PROPERTY_FS_COORD_ORIGIN,
                       TGSI_FS_COORD_ORIGIN_LOWER_LEFT);
      else if (pscreen->get_param(pscreen,
                                  PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT))
         /* the driver supports upper-left origin, need to invert Y */
         invert = TRUE;
      else
         assert(0);
   }

   if (program->PixelCenterInteger) {
      /* Fragment shader wants pixel center integer */
      if (pscreen->get_param(pscreen,
                             PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER)) {
         /* the driver supports pixel center integer */
         adjY[1] = 1.0f;
         ureg_property(ureg, TGSI_PROPERTY_FS_COORD_PIXEL_CENTER,
                       TGSI_FS_COORD_PIXEL_CENTER_INTEGER);
      }
      else if (pscreen->get_param(pscreen,
                            PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER)) {
         /* the driver supports pixel center half integer, need to bias X,Y */
         adjX = -0.5f;
         adjY[0] = -0.5f;
         adjY[1] = 0.5f;
      }
      else
         assert(0);
   }
   else {
      /* Fragment shader wants pixel center half integer */
      if (pscreen->get_param(pscreen,
                          PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER)) {
         /* the driver supports pixel center half integer */
      }
      else if (pscreen->get_param(pscreen,
                               PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER)) {
         /* the driver supports pixel center integer, need to bias X,Y */
         adjX = adjY[0] = adjY[1] = 0.5f;
         ureg_property(ureg, TGSI_PROPERTY_FS_COORD_PIXEL_CENTER,
                       TGSI_FS_COORD_PIXEL_CENTER_INTEGER);
      }
      else
         assert(0);
   }

   /* we invert after adjustment so that we avoid the MOV to temporary,
    * and reuse the adjustment ADD instead */
   emit_wpos_adjustment(st->ctx, t, program, invert, adjX, adjY);
}


/**
 * Translate Mesa program to TGSI format.
 * \param program  the program to translate
 * \param numInputs  number of input registers used
 * \param inputMapping  maps Mesa fragment program inputs to TGSI generic
 *                      input indexes
 * \param inputSemanticName  the TGSI_SEMANTIC flag for each input
 * \param inputSemanticIndex  the semantic index (ex: which texcoord) for
 *                            each input
 * \param interpMode  the TGSI_INTERPOLATE_LINEAR/PERSP mode for each input
 * \param numOutputs  number of output registers used
 * \param outputMapping  maps Mesa fragment program outputs to TGSI
 *                       generic outputs
 * \param outputSemanticName  the TGSI_SEMANTIC flag for each output
 * \param outputSemanticIndex  the semantic index (ex: which texcoord) for
 *                             each output
 *
 * \return  PIPE_OK or PIPE_ERROR_OUT_OF_MEMORY
 */
enum pipe_error
st_translate_mesa_program(struct gl_context *ctx,
                          uint procType,
                          struct ureg_program *ureg,
                          const struct gl_program *program,
                          GLuint numInputs,
                          const ubyte inputMapping[],
                          const ubyte inputSemanticName[],
                          const ubyte inputSemanticIndex[],
                          const ubyte interpMode[],
                          GLuint numOutputs,
                          const ubyte outputMapping[],
                          const ubyte outputSemanticName[],
                          const ubyte outputSemanticIndex[])
{
   struct st_translate translate, *t;
   unsigned i;
   enum pipe_error ret = PIPE_OK;

   assert(numInputs <= ARRAY_SIZE(t->inputs));
   assert(numOutputs <= ARRAY_SIZE(t->outputs));

   t = &translate;
   memset(t, 0, sizeof *t);

   t->procType = procType;
   t->inputMapping = inputMapping;
   t->outputMapping = outputMapping;
   t->ureg = ureg;

   /*_mesa_print_program(program);*/

   /*
    * Declare input attributes.
    */
   if (procType == PIPE_SHADER_FRAGMENT) {
      for (i = 0; i < numInputs; i++) {
         t->inputs[i] = ureg_DECL_fs_input(ureg,
                                           inputSemanticName[i],
                                           inputSemanticIndex[i],
                                           interpMode[i]);
      }

      if (program->info.inputs_read & VARYING_BIT_POS) {
         /* Must do this after setting up t->inputs, and before
          * emitting constant references, below:
          */
         emit_wpos(st_context(ctx), t, program, ureg);
      }

      /*
       * Declare output attributes.
       */
      for (i = 0; i < numOutputs; i++) {
         switch (outputSemanticName[i]) {
         case TGSI_SEMANTIC_POSITION:
            t->outputs[i] = ureg_DECL_output(ureg,
                                             TGSI_SEMANTIC_POSITION, /* Z / Depth */
                                             outputSemanticIndex[i]);

            t->outputs[i] = ureg_writemask(t->outputs[i],
                                           TGSI_WRITEMASK_Z);
            break;
         case TGSI_SEMANTIC_STENCIL:
            t->outputs[i] = ureg_DECL_output(ureg,
                                             TGSI_SEMANTIC_STENCIL, /* Stencil */
                                             outputSemanticIndex[i]);
            t->outputs[i] = ureg_writemask(t->outputs[i],
                                           TGSI_WRITEMASK_Y);
            break;
         case TGSI_SEMANTIC_COLOR:
            t->outputs[i] = ureg_DECL_output(ureg,
                                             TGSI_SEMANTIC_COLOR,
                                             outputSemanticIndex[i]);
            break;
         default:
            debug_assert(0);
            return 0;
         }
      }
   }
   else if (procType == PIPE_SHADER_GEOMETRY) {
      for (i = 0; i < numInputs; i++) {
         t->inputs[i] = ureg_DECL_input(ureg,
                                        inputSemanticName[i],
                                        inputSemanticIndex[i], 0, 1);
      }

      for (i = 0; i < numOutputs; i++) {
         t->outputs[i] = ureg_DECL_output(ureg,
                                          outputSemanticName[i],
                                          outputSemanticIndex[i]);
      }
   }
   else {
      assert(procType == PIPE_SHADER_VERTEX);

      for (i = 0; i < numInputs; i++) {
         t->inputs[i] = ureg_DECL_vs_input(ureg, i);
      }

      for (i = 0; i < numOutputs; i++) {
         t->outputs[i] = ureg_DECL_output(ureg,
                                          outputSemanticName[i],
                                          outputSemanticIndex[i]);
         if (outputSemanticName[i] == TGSI_SEMANTIC_FOG) {
            /* force register to contain a fog coordinate in the
             * form (F, 0, 0, 1).
             */
            ureg_MOV(ureg,
                     ureg_writemask(t->outputs[i], TGSI_WRITEMASK_YZW),
                     ureg_imm4f(ureg, 0.0f, 0.0f, 0.0f, 1.0f));
            t->outputs[i] = ureg_writemask(t->outputs[i], TGSI_WRITEMASK_X);
         }
      }
   }

   /* Declare address register.
    */
   if (program->arb.NumAddressRegs > 0) {
      debug_assert(program->arb.NumAddressRegs == 1);
      t->address[0] = ureg_DECL_address(ureg);
   }

   /* Declare misc input registers
    */
   GLbitfield64 sysInputs = program->info.system_values_read;
   for (i = 0; sysInputs; i++) {
      if (sysInputs & (1ull << i)) {
         unsigned semName = _mesa_sysval_to_semantic(i);

         t->systemValues[i] = ureg_DECL_system_value(ureg, semName, 0);

         if (semName == TGSI_SEMANTIC_INSTANCEID ||
             semName == TGSI_SEMANTIC_VERTEXID) {
            /* From Gallium perspective, these system values are always
             * integer, and require native integer support.  However, if
             * native integer is supported on the vertex stage but not the
             * pixel stage (e.g, i915g + draw), Mesa will generate IR that
             * assumes these system values are floats. To resolve the
             * inconsistency, we insert a U2F.
             */
            struct st_context *st = st_context(ctx);
            struct pipe_screen *pscreen = st->pipe->screen;
            assert(procType == PIPE_SHADER_VERTEX);
            assert(pscreen->get_shader_param(pscreen, PIPE_SHADER_VERTEX,
                   PIPE_SHADER_CAP_INTEGERS));
            (void) pscreen;  /* silence non-debug build warnings */
            if (!ctx->Const.NativeIntegers) {
               struct ureg_dst temp = ureg_DECL_local_temporary(t->ureg);
               ureg_U2F(t->ureg, ureg_writemask(temp, TGSI_WRITEMASK_X),
                        t->systemValues[i]);
               t->systemValues[i] = ureg_scalar(ureg_src(temp), 0);
            }
         }

         if (procType == PIPE_SHADER_FRAGMENT &&
             semName == TGSI_SEMANTIC_POSITION)
            emit_wpos(st_context(ctx), t, program, ureg);

          sysInputs &= ~(1ull << i);
      }
   }

   if (program->arb.IndirectRegisterFiles & (1 << PROGRAM_TEMPORARY)) {
      /* If temps are accessed with indirect addressing, declare temporaries
       * in sequential order.  Else, we declare them on demand elsewhere.
       */
      for (i = 0; i < program->arb.NumTemporaries; i++) {
         /* XXX use TGSI_FILE_TEMPORARY_ARRAY when it's supported by ureg */
         t->temps[i] = ureg_DECL_temporary(t->ureg);
      }
   }

   /* Emit constants and immediates.  Mesa uses a single index space
    * for these, so we put all the translated regs in t->constants.
    */
   if (program->Parameters) {
      t->constants = calloc(program->Parameters->NumParameters,
                             sizeof t->constants[0]);
      if (t->constants == NULL) {
         ret = PIPE_ERROR_OUT_OF_MEMORY;
         goto out;
      }

      for (i = 0; i < program->Parameters->NumParameters; i++) {
         unsigned pvo = program->Parameters->ParameterValueOffset[i];

         switch (program->Parameters->Parameters[i].Type) {
         case PROGRAM_STATE_VAR:
         case PROGRAM_UNIFORM:
            t->constants[i] = ureg_DECL_constant(ureg, i);
            break;

            /* Emit immediates only when there's no indirect addressing of
             * the const buffer.
             * FIXME: Be smarter and recognize param arrays:
             * indirect addressing is only valid within the referenced
             * array.
             */
         case PROGRAM_CONSTANT:
            if (program->arb.IndirectRegisterFiles & PROGRAM_ANY_CONST)
               t->constants[i] = ureg_DECL_constant( ureg, i );
            else
               t->constants[i] = 
                  ureg_DECL_immediate(ureg,
                                      (const float *)
                                      program->Parameters->ParameterValues + pvo,
                                      4);
            break;
         default:
            break;
         }
      }
   }

   /* texture samplers */
   for (i = 0;
        i < ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits; i++) {
      if (program->SamplersUsed & (1u << i)) {
         unsigned target =
            translate_texture_index(program->TexturesUsed[i],
                                    !!(program->ShadowSamplers & (1 << i)));
         t->samplers[i] = ureg_DECL_sampler(ureg, i);
         ureg_DECL_sampler_view(ureg, i, target,
                                TGSI_RETURN_TYPE_FLOAT,
                                TGSI_RETURN_TYPE_FLOAT,
                                TGSI_RETURN_TYPE_FLOAT,
                                TGSI_RETURN_TYPE_FLOAT);

      }
   }

   /* Emit each instruction in turn:
    */
   for (i = 0; i < program->arb.NumInstructions; i++)
      compile_instruction(ctx, t, &program->arb.Instructions[i]);

out:
   free(t->constants);
   return ret;
}
