/*
 * Copyright © 2015 Intel Corporation
 * Copyright © 2014-2015 Broadcom
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "glsl/list.h"
#include "main/imports.h"
#include "util/ralloc.h"

#include "prog_to_nir.h"
#include "prog_instruction.h"
#include "prog_parameter.h"
#include "prog_print.h"

/**
 * \file prog_to_nir.c
 *
 * A translator from Mesa IR (prog_instruction.h) to NIR.  This is primarily
 * intended to support ARB_vertex_program, ARB_fragment_program, and fixed-function
 * vertex processing.  Full GLSL support should use glsl_to_nir instead.
 */

struct ptn_compile {
   const struct gl_program *prog;
   nir_builder build;
   bool error;

   nir_variable *parameters;
   nir_variable *input_vars[VARYING_SLOT_MAX];
   nir_variable *output_vars[VARYING_SLOT_MAX];
   nir_register **output_regs;
   nir_register **temp_regs;

   nir_register *addr_reg;
};

#define SWIZ(X, Y, Z, W) \
   (unsigned[4]){ SWIZZLE_##X, SWIZZLE_##Y, SWIZZLE_##Z, SWIZZLE_##W }
#define ptn_swizzle(b, src, x, y, z, w) nir_swizzle(b, src, SWIZ(x, y, z, w), 4, true)
#define ptn_channel(b, src, ch) nir_swizzle(b, src, SWIZ(ch, ch, ch, ch), 1, true)

static nir_ssa_def *
ptn_src_for_dest(struct ptn_compile *c, nir_alu_dest *dest)
{
   nir_builder *b = &c->build;

   nir_alu_src src;
   memset(&src, 0, sizeof(src));

   if (dest->dest.is_ssa)
      src.src = nir_src_for_ssa(&dest->dest.ssa);
   else {
      assert(!dest->dest.reg.indirect);
      src.src = nir_src_for_reg(dest->dest.reg.reg);
      src.src.reg.base_offset = dest->dest.reg.base_offset;
   }

   for (int i = 0; i < 4; i++)
      src.swizzle[i] = i;

   return nir_fmov_alu(b, src, 4);
}

static nir_alu_dest
ptn_get_dest(struct ptn_compile *c, const struct prog_dst_register *prog_dst)
{
   nir_alu_dest dest;

   memset(&dest, 0, sizeof(dest));

   switch (prog_dst->File) {
   case PROGRAM_TEMPORARY:
      dest.dest.reg.reg = c->temp_regs[prog_dst->Index];
      break;
   case PROGRAM_OUTPUT:
      dest.dest.reg.reg = c->output_regs[prog_dst->Index];
      break;
   case PROGRAM_ADDRESS:
      assert(prog_dst->Index == 0);
      dest.dest.reg.reg = c->addr_reg;
      break;
   case PROGRAM_UNDEFINED:
      break;
   }

   dest.write_mask = prog_dst->WriteMask;
   dest.saturate = false;

   assert(!prog_dst->RelAddr);

   return dest;
}

static nir_ssa_def *
ptn_get_src(struct ptn_compile *c, const struct prog_src_register *prog_src)
{
   nir_builder *b = &c->build;
   nir_alu_src src;

   memset(&src, 0, sizeof(src));

   switch (prog_src->File) {
   case PROGRAM_UNDEFINED:
      return nir_imm_float(b, 0.0);
   case PROGRAM_TEMPORARY:
      assert(!prog_src->RelAddr && prog_src->Index >= 0);
      src.src.reg.reg = c->temp_regs[prog_src->Index];
      break;
   case PROGRAM_INPUT: {
      /* ARB_vertex_program doesn't allow relative addressing on vertex
       * attributes; ARB_fragment_program has no relative addressing at all.
       */
      assert(!prog_src->RelAddr);

      assert(prog_src->Index >= 0 && prog_src->Index < VARYING_SLOT_MAX);

      nir_intrinsic_instr *load =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_var);
      load->num_components = 4;
      load->variables[0] = nir_deref_var_create(load, c->input_vars[prog_src->Index]);

      nir_ssa_dest_init(&load->instr, &load->dest, 4, NULL);
      nir_instr_insert_after_cf_list(b->cf_node_list, &load->instr);

      src.src = nir_src_for_ssa(&load->dest.ssa);
      break;
   }
   case PROGRAM_STATE_VAR:
   case PROGRAM_CONSTANT: {
      /* We actually want to look at the type in the Parameters list for this,
       * because it lets us upload constant builtin uniforms as actual
       * constants.
       */
      struct gl_program_parameter_list *plist = c->prog->Parameters;
      gl_register_file file = prog_src->RelAddr ? prog_src->File :
         plist->Parameters[prog_src->Index].Type;

      switch (file) {
      case PROGRAM_CONSTANT:
         if ((c->prog->IndirectRegisterFiles & (1 << PROGRAM_CONSTANT)) == 0) {
            float *v = (float *) plist->ParameterValues[prog_src->Index];
            src.src = nir_src_for_ssa(nir_imm_vec4(b, v[0], v[1], v[2], v[3]));
            break;
         }
         /* FALLTHROUGH */
      case PROGRAM_STATE_VAR: {
         nir_intrinsic_instr *load =
            nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_var);
         nir_ssa_dest_init(&load->instr, &load->dest, 4, NULL);
         load->num_components = 4;

         load->variables[0] = nir_deref_var_create(load, c->parameters);
         nir_deref_array *deref_arr =
            nir_deref_array_create(load->variables[0]);
         deref_arr->deref.type = glsl_vec4_type();
         load->variables[0]->deref.child = &deref_arr->deref;

         if (prog_src->RelAddr) {
            deref_arr->deref_array_type = nir_deref_array_type_indirect;

            nir_alu_src addr_src = { NIR_SRC_INIT };
            addr_src.src = nir_src_for_reg(c->addr_reg);
            nir_ssa_def *reladdr = nir_imov_alu(b, addr_src, 1);

            if (prog_src->Index < 0) {
               /* This is a negative offset which should be added to the address
                * register's value.
                */
               reladdr = nir_iadd(b, reladdr, nir_imm_int(b, prog_src->Index));

               deref_arr->base_offset = 0;
            } else {
               deref_arr->base_offset = prog_src->Index;
            }
            deref_arr->indirect = nir_src_for_ssa(reladdr);
         } else {
            deref_arr->deref_array_type = nir_deref_array_type_direct;
            deref_arr->base_offset = prog_src->Index;
         }

         nir_instr_insert_after_cf_list(b->cf_node_list, &load->instr);

         src.src = nir_src_for_ssa(&load->dest.ssa);
         break;
      }
      default:
         fprintf(stderr, "bad uniform src register file: %s (%d)\n",
                 _mesa_register_file_name(file), file);
         abort();
      }
      break;
   }
   default:
      fprintf(stderr, "unknown src register file: %s (%d)\n",
              _mesa_register_file_name(prog_src->File), prog_src->File);
      abort();
   }

   nir_ssa_def *def;
   if (!HAS_EXTENDED_SWIZZLE(prog_src->Swizzle) &&
       (prog_src->Negate == NEGATE_NONE || prog_src->Negate == NEGATE_XYZW)) {
      /* The simple non-SWZ case. */
      for (int i = 0; i < 4; i++)
         src.swizzle[i] = GET_SWZ(prog_src->Swizzle, i);

      def = nir_fmov_alu(b, src, 4);

      if (prog_src->Abs)
         def = nir_fabs(b, def);

      if (prog_src->Negate)
         def = nir_fneg(b, def);
   } else {
      /* The SWZ instruction allows per-component zero/one swizzles, and also
       * per-component negation.
       */
      nir_ssa_def *chans[4];
      for (int i = 0; i < 4; i++) {
         int swizzle = GET_SWZ(prog_src->Swizzle, i);
         if (swizzle == SWIZZLE_ZERO) {
            chans[i] = nir_imm_float(b, 0.0);
         } else if (swizzle == SWIZZLE_ONE) {
            chans[i] = nir_imm_float(b, 1.0);
         } else {
            assert(swizzle != SWIZZLE_NIL);
            nir_alu_instr *mov = nir_alu_instr_create(b->shader, nir_op_fmov);
            nir_ssa_dest_init(&mov->instr, &mov->dest.dest, 1, NULL);
            mov->dest.write_mask = 0x1;
            mov->src[0] = src;
            mov->src[0].swizzle[0] = swizzle;
            nir_instr_insert_after_cf_list(b->cf_node_list, &mov->instr);

            chans[i] = &mov->dest.dest.ssa;
         }

         if (prog_src->Abs)
            chans[i] = nir_fabs(b, chans[i]);

         if (prog_src->Negate & (1 << i))
            chans[i] = nir_fneg(b, chans[i]);
      }
      def = nir_vec4(b, chans[0], chans[1], chans[2], chans[3]);
   }

   return def;
}

static void
ptn_alu(nir_builder *b, nir_op op, nir_alu_dest dest, nir_ssa_def **src)
{
   unsigned num_srcs = nir_op_infos[op].num_inputs;
   nir_alu_instr *instr = nir_alu_instr_create(b->shader, op);
   unsigned i;

   for (i = 0; i < num_srcs; i++)
      instr->src[i].src = nir_src_for_ssa(src[i]);

   instr->dest = dest;
   nir_instr_insert_after_cf_list(b->cf_node_list, &instr->instr);
}

static void
ptn_move_dest_masked(nir_builder *b, nir_alu_dest dest,
                     nir_ssa_def *def, unsigned write_mask)
{
   if (!(dest.write_mask & write_mask))
      return;

   nir_alu_instr *mov = nir_alu_instr_create(b->shader, nir_op_fmov);
   if (!mov)
      return;

   mov->dest = dest;
   mov->dest.write_mask &= write_mask;
   mov->src[0].src = nir_src_for_ssa(def);
   for (unsigned i = def->num_components; i < 4; i++)
      mov->src[0].swizzle[i] = def->num_components - 1;
   nir_instr_insert_after_cf_list(b->cf_node_list, &mov->instr);
}

static void
ptn_move_dest(nir_builder *b, nir_alu_dest dest, nir_ssa_def *def)
{
   ptn_move_dest_masked(b, dest, def, WRITEMASK_XYZW);
}

static void
ptn_arl(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   ptn_move_dest(b, dest, nir_f2i(b, nir_ffloor(b, src[0])));
}

/* EXP - Approximate Exponential Base 2
 *  dst.x = 2^{\lfloor src.x\rfloor}
 *  dst.y = src.x - \lfloor src.x\rfloor
 *  dst.z = 2^{src.x}
 *  dst.w = 1.0
 */
static void
ptn_exp(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   nir_ssa_def *srcx = ptn_channel(b, src[0], X);

   ptn_move_dest_masked(b, dest, nir_fexp2(b, nir_ffloor(b, srcx)), WRITEMASK_X);
   ptn_move_dest_masked(b, dest, nir_fsub(b, srcx, nir_ffloor(b, srcx)), WRITEMASK_Y);
   ptn_move_dest_masked(b, dest, nir_fexp2(b, srcx), WRITEMASK_Z);
   ptn_move_dest_masked(b, dest, nir_imm_float(b, 1.0), WRITEMASK_W);
}

/* LOG - Approximate Logarithm Base 2
 *  dst.x = \lfloor\log_2{|src.x|}\rfloor
 *  dst.y = |src.x| * 2^{-\lfloor\log_2{|src.x|}\rfloor}}
 *  dst.z = \log_2{|src.x|}
 *  dst.w = 1.0
 */
static void
ptn_log(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   nir_ssa_def *abs_srcx = nir_fabs(b, ptn_channel(b, src[0], X));
   nir_ssa_def *log2 = nir_flog2(b, abs_srcx);
   nir_ssa_def *floor_log2 = nir_ffloor(b, log2);

   ptn_move_dest_masked(b, dest, floor_log2, WRITEMASK_X);
   ptn_move_dest_masked(b, dest,
                        nir_fmul(b, abs_srcx,
                                 nir_fexp2(b, nir_fneg(b, floor_log2))),
                        WRITEMASK_Y);
   ptn_move_dest_masked(b, dest, log2, WRITEMASK_Z);
   ptn_move_dest_masked(b, dest, nir_imm_float(b, 1.0), WRITEMASK_W);
}

/* DST - Distance Vector
 *   dst.x = 1.0
 *   dst.y = src0.y \times src1.y
 *   dst.z = src0.z
 *   dst.w = src1.w
 */
static void
ptn_dst(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   ptn_move_dest_masked(b, dest, nir_imm_float(b, 1.0), WRITEMASK_X);
   ptn_move_dest_masked(b, dest, nir_fmul(b, src[0], src[1]), WRITEMASK_Y);
   ptn_move_dest_masked(b, dest, nir_fmov(b, src[0]), WRITEMASK_Z);
   ptn_move_dest_masked(b, dest, nir_fmov(b, src[1]), WRITEMASK_W);
}

/* LIT - Light Coefficients
 *  dst.x = 1.0
 *  dst.y = max(src.x, 0.0)
 *  dst.z = (src.x > 0.0) ? max(src.y, 0.0)^{clamp(src.w, -128.0, 128.0))} : 0
 *  dst.w = 1.0
 */
static void
ptn_lit(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   ptn_move_dest_masked(b, dest, nir_imm_float(b, 1.0), WRITEMASK_XW);

   ptn_move_dest_masked(b, dest, nir_fmax(b, ptn_channel(b, src[0], X),
                                          nir_imm_float(b, 0.0)), WRITEMASK_Y);

   if (dest.write_mask & WRITEMASK_Z) {
      nir_ssa_def *src0_y = ptn_channel(b, src[0], Y);
      nir_ssa_def *wclamp = nir_fmax(b, nir_fmin(b, ptn_channel(b, src[0], W),
                                                 nir_imm_float(b, 128.0)),
                                     nir_imm_float(b, -128.0));
      nir_ssa_def *pow = nir_fpow(b, nir_fmax(b, src0_y, nir_imm_float(b, 0.0)),
                                  wclamp);

      nir_ssa_def *z;
      if (b->shader->options->native_integers) {
         z = nir_bcsel(b,
                       nir_fge(b, nir_imm_float(b, 0.0), ptn_channel(b, src[0], X)),
                       nir_imm_float(b, 0.0),
                       pow);
      } else {
         z = nir_fcsel(b,
                       nir_sge(b, nir_imm_float(b, 0.0), ptn_channel(b, src[0], X)),
                       nir_imm_float(b, 0.0),
                       pow);
      }

      ptn_move_dest_masked(b, dest, z, WRITEMASK_Z);
   }
}

/* SCS - Sine Cosine
 *   dst.x = \cos{src.x}
 *   dst.y = \sin{src.x}
 *   dst.z = 0.0
 *   dst.w = 1.0
 */
static void
ptn_scs(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   ptn_move_dest_masked(b, dest, nir_fcos(b, ptn_channel(b, src[0], X)),
                        WRITEMASK_X);
   ptn_move_dest_masked(b, dest, nir_fsin(b, ptn_channel(b, src[0], X)),
                        WRITEMASK_Y);
   ptn_move_dest_masked(b, dest, nir_imm_float(b, 0.0), WRITEMASK_Z);
   ptn_move_dest_masked(b, dest, nir_imm_float(b, 1.0), WRITEMASK_W);
}

/**
 * Emit SLT.  For platforms with integers, prefer b2f(flt(...)).
 */
static void
ptn_slt(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   if (b->shader->options->native_integers) {
      ptn_move_dest(b, dest, nir_b2f(b, nir_flt(b, src[0], src[1])));
   } else {
      ptn_move_dest(b, dest, nir_slt(b, src[0], src[1]));
   }
}

/**
 * Emit SGE.  For platforms with integers, prefer b2f(fge(...)).
 */
static void
ptn_sge(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   if (b->shader->options->native_integers) {
      ptn_move_dest(b, dest, nir_b2f(b, nir_fge(b, src[0], src[1])));
   } else {
      ptn_move_dest(b, dest, nir_sge(b, src[0], src[1]));
   }
}

static void
ptn_sle(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   nir_ssa_def *commuted[] = { src[1], src[0] };
   ptn_sge(b, dest, commuted);
}

static void
ptn_sgt(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   nir_ssa_def *commuted[] = { src[1], src[0] };
   ptn_slt(b, dest, commuted);
}

/**
 * Emit SEQ.  For platforms with integers, prefer b2f(feq(...)).
 */
static void
ptn_seq(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   if (b->shader->options->native_integers) {
      ptn_move_dest(b, dest, nir_b2f(b, nir_feq(b, src[0], src[1])));
   } else {
      ptn_move_dest(b, dest, nir_seq(b, src[0], src[1]));
   }
}

/**
 * Emit SNE.  For platforms with integers, prefer b2f(fne(...)).
 */
static void
ptn_sne(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   if (b->shader->options->native_integers) {
      ptn_move_dest(b, dest, nir_b2f(b, nir_fne(b, src[0], src[1])));
   } else {
      ptn_move_dest(b, dest, nir_sne(b, src[0], src[1]));
   }
}

static void
ptn_xpd(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   ptn_move_dest_masked(b, dest,
                        nir_fsub(b,
                                 nir_fmul(b,
                                          ptn_swizzle(b, src[0], Y, Z, X, X),
                                          ptn_swizzle(b, src[1], Z, X, Y, X)),
                                 nir_fmul(b,
                                          ptn_swizzle(b, src[1], Y, Z, X, X),
                                          ptn_swizzle(b, src[0], Z, X, Y, X))),
                        WRITEMASK_XYZ);
   ptn_move_dest_masked(b, dest, nir_imm_float(b, 1.0), WRITEMASK_W);
}

static void
ptn_dp2(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   ptn_move_dest(b, dest, nir_fdot2(b, src[0], src[1]));
}

static void
ptn_dp3(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   ptn_move_dest(b, dest, nir_fdot3(b, src[0], src[1]));
}

static void
ptn_dp4(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   ptn_move_dest(b, dest, nir_fdot4(b, src[0], src[1]));
}

static void
ptn_dph(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   nir_ssa_def *dp3 = nir_fdot3(b, src[0], src[1]);
   ptn_move_dest(b, dest, nir_fadd(b, dp3, ptn_channel(b, src[1], W)));
}

static void
ptn_cmp(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   if (b->shader->options->native_integers) {
      ptn_move_dest(b, dest, nir_bcsel(b,
                                       nir_flt(b, src[0], nir_imm_float(b, 0.0)),
                                       src[1], src[2]));
   } else {
      ptn_move_dest(b, dest, nir_fcsel(b,
                                       nir_slt(b, src[0], nir_imm_float(b, 0.0)),
                                       src[1], src[2]));
   }
}

static void
ptn_lrp(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   ptn_move_dest(b, dest, nir_flrp(b, src[2], src[1], src[0]));
}

static void
ptn_kil(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src)
{
   nir_ssa_def *cmp = b->shader->options->native_integers ?
      nir_bany4(b, nir_flt(b, src[0], nir_imm_float(b, 0.0))) :
      nir_fany4(b, nir_slt(b, src[0], nir_imm_float(b, 0.0)));

   nir_intrinsic_instr *discard =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_discard_if);
   discard->src[0] = nir_src_for_ssa(cmp);
   nir_instr_insert_after_cf_list(b->cf_node_list, &discard->instr);
}

static void
ptn_tex(nir_builder *b, nir_alu_dest dest, nir_ssa_def **src,
        struct prog_instruction *prog_inst)
{
   nir_tex_instr *instr;
   nir_texop op;
   unsigned num_srcs;

   switch (prog_inst->Opcode) {
   case OPCODE_TEX:
      op = nir_texop_tex;
      num_srcs = 1;
      break;
   case OPCODE_TXB:
      op = nir_texop_txb;
      num_srcs = 2;
      break;
   case OPCODE_TXD:
      op = nir_texop_txd;
      num_srcs = 3;
      break;
   case OPCODE_TXL:
      op = nir_texop_txl;
      num_srcs = 2;
      break;
   case OPCODE_TXP:
      op = nir_texop_tex;
      num_srcs = 2;
      break;
   case OPCODE_TXP_NV:
      assert(!"not handled");
      op = nir_texop_tex;
      num_srcs = 2;
      break;
   default:
      fprintf(stderr, "unknown tex op %d\n", prog_inst->Opcode);
      abort();
   }

   if (prog_inst->TexShadow)
      num_srcs++;

   instr = nir_tex_instr_create(b->shader, num_srcs);
   instr->op = op;
   instr->dest_type = nir_type_float;
   instr->is_shadow = prog_inst->TexShadow;
   instr->sampler_index = prog_inst->TexSrcUnit;

   switch (prog_inst->TexSrcTarget) {
   case TEXTURE_1D_INDEX:
      instr->sampler_dim = GLSL_SAMPLER_DIM_1D;
      break;
   case TEXTURE_2D_INDEX:
      instr->sampler_dim = GLSL_SAMPLER_DIM_2D;
      break;
   case TEXTURE_3D_INDEX:
      instr->sampler_dim = GLSL_SAMPLER_DIM_3D;
      break;
   case TEXTURE_CUBE_INDEX:
      instr->sampler_dim = GLSL_SAMPLER_DIM_CUBE;
      break;
   case TEXTURE_RECT_INDEX:
      instr->sampler_dim = GLSL_SAMPLER_DIM_RECT;
      break;
   default:
      fprintf(stderr, "Unknown texture target %d\n", prog_inst->TexSrcTarget);
      abort();
   }

   switch (instr->sampler_dim) {
   case GLSL_SAMPLER_DIM_1D:
   case GLSL_SAMPLER_DIM_BUF:
      instr->coord_components = 1;
      break;
   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_RECT:
   case GLSL_SAMPLER_DIM_EXTERNAL:
   case GLSL_SAMPLER_DIM_MS:
      instr->coord_components = 2;
      break;
   case GLSL_SAMPLER_DIM_3D:
   case GLSL_SAMPLER_DIM_CUBE:
      instr->coord_components = 3;
      break;
   }

   unsigned src_number = 0;

   instr->src[src_number].src =
      nir_src_for_ssa(ptn_swizzle(b, src[0], X, Y, Z, W));
   instr->src[src_number].src_type = nir_tex_src_coord;
   src_number++;

   if (prog_inst->Opcode == OPCODE_TXP) {
      instr->src[src_number].src = nir_src_for_ssa(ptn_channel(b, src[0], W));
      instr->src[src_number].src_type = nir_tex_src_projector;
      src_number++;
   }

   if (prog_inst->Opcode == OPCODE_TXB) {
      instr->src[src_number].src = nir_src_for_ssa(ptn_channel(b, src[0], W));
      instr->src[src_number].src_type = nir_tex_src_bias;
      src_number++;
   }

   if (prog_inst->Opcode == OPCODE_TXL) {
      instr->src[src_number].src = nir_src_for_ssa(ptn_channel(b, src[0], W));
      instr->src[src_number].src_type = nir_tex_src_lod;
      src_number++;
   }

   if (instr->is_shadow) {
      if (instr->coord_components < 3)
         instr->src[src_number].src = nir_src_for_ssa(ptn_channel(b, src[0], Z));
      else
         instr->src[src_number].src = nir_src_for_ssa(ptn_channel(b, src[0], W));

      instr->src[src_number].src_type = nir_tex_src_comparitor;
      src_number++;
   }

   assert(src_number == num_srcs);

   nir_ssa_dest_init(&instr->instr, &instr->dest, 4, NULL);
   nir_instr_insert_after_cf_list(b->cf_node_list, &instr->instr);

   /* Resolve the writemask on the texture op. */
   ptn_move_dest(b, dest, &instr->dest.ssa);
}

static const nir_op op_trans[MAX_OPCODE] = {
   [OPCODE_NOP] = 0,
   [OPCODE_ABS] = nir_op_fabs,
   [OPCODE_ADD] = nir_op_fadd,
   [OPCODE_ARL] = 0,
   [OPCODE_CMP] = 0,
   [OPCODE_COS] = 0,
   [OPCODE_DDX] = nir_op_fddx,
   [OPCODE_DDY] = nir_op_fddy,
   [OPCODE_DP2] = 0,
   [OPCODE_DP3] = 0,
   [OPCODE_DP4] = 0,
   [OPCODE_DPH] = 0,
   [OPCODE_DST] = 0,
   [OPCODE_END] = 0,
   [OPCODE_EX2] = 0,
   [OPCODE_EXP] = 0,
   [OPCODE_FLR] = nir_op_ffloor,
   [OPCODE_FRC] = nir_op_ffract,
   [OPCODE_LG2] = 0,
   [OPCODE_LIT] = 0,
   [OPCODE_LOG] = 0,
   [OPCODE_LRP] = 0,
   [OPCODE_MAD] = nir_op_ffma,
   [OPCODE_MAX] = nir_op_fmax,
   [OPCODE_MIN] = nir_op_fmin,
   [OPCODE_MOV] = nir_op_fmov,
   [OPCODE_MUL] = nir_op_fmul,
   [OPCODE_POW] = 0,
   [OPCODE_RCP] = 0,

   [OPCODE_RSQ] = 0,
   [OPCODE_SCS] = 0,
   [OPCODE_SEQ] = 0,
   [OPCODE_SGE] = 0,
   [OPCODE_SGT] = 0,
   [OPCODE_SIN] = 0,
   [OPCODE_SLE] = 0,
   [OPCODE_SLT] = 0,
   [OPCODE_SNE] = 0,
   [OPCODE_SSG] = nir_op_fsign,
   [OPCODE_SUB] = nir_op_fsub,
   [OPCODE_SWZ] = 0,
   [OPCODE_TEX] = 0,
   [OPCODE_TRUNC] = nir_op_ftrunc,
   [OPCODE_TXB] = 0,
   [OPCODE_TXD] = 0,
   [OPCODE_TXL] = 0,
   [OPCODE_TXP] = 0,
   [OPCODE_TXP_NV] = 0,
   [OPCODE_XPD] = 0,
};

static void
ptn_emit_instruction(struct ptn_compile *c, struct prog_instruction *prog_inst)
{
   nir_builder *b = &c->build;
   unsigned i;
   const unsigned op = prog_inst->Opcode;

   if (op == OPCODE_END)
      return;

   nir_ssa_def *src[3];
   for (i = 0; i < 3; i++) {
      src[i] = ptn_get_src(c, &prog_inst->SrcReg[i]);
   }
   nir_alu_dest dest = ptn_get_dest(c, &prog_inst->DstReg);
   if (c->error)
      return;

   switch (op) {
   case OPCODE_RSQ:
      ptn_move_dest(b, dest,
                    nir_frsq(b, nir_fabs(b, ptn_channel(b, src[0], X))));
      break;

   case OPCODE_RCP:
      ptn_move_dest(b, dest, nir_frcp(b, ptn_channel(b, src[0], X)));
      break;

   case OPCODE_EX2:
      ptn_move_dest(b, dest, nir_fexp2(b, ptn_channel(b, src[0], X)));
      break;

   case OPCODE_LG2:
      ptn_move_dest(b, dest, nir_flog2(b, ptn_channel(b, src[0], X)));
      break;

   case OPCODE_POW:
      ptn_move_dest(b, dest, nir_fpow(b,
                                      ptn_channel(b, src[0], X),
                                      ptn_channel(b, src[1], X)));
      break;

   case OPCODE_COS:
      ptn_move_dest(b, dest, nir_fcos(b, ptn_channel(b, src[0], X)));
      break;

   case OPCODE_SIN:
      ptn_move_dest(b, dest, nir_fsin(b, ptn_channel(b, src[0], X)));
      break;

   case OPCODE_ARL:
      ptn_arl(b, dest, src);
      break;

   case OPCODE_EXP:
      ptn_exp(b, dest, src);
      break;

   case OPCODE_LOG:
      ptn_log(b, dest, src);
      break;

   case OPCODE_LRP:
      ptn_lrp(b, dest, src);
      break;

   case OPCODE_DST:
      ptn_dst(b, dest, src);
      break;

   case OPCODE_LIT:
      ptn_lit(b, dest, src);
      break;

   case OPCODE_XPD:
      ptn_xpd(b, dest, src);
      break;

   case OPCODE_DP2:
      ptn_dp2(b, dest, src);
      break;

   case OPCODE_DP3:
      ptn_dp3(b, dest, src);
      break;

   case OPCODE_DP4:
      ptn_dp4(b, dest, src);
      break;

   case OPCODE_DPH:
      ptn_dph(b, dest, src);
      break;

   case OPCODE_KIL:
      ptn_kil(b, dest, src);
      break;

   case OPCODE_CMP:
      ptn_cmp(b, dest, src);
      break;

   case OPCODE_SCS:
      ptn_scs(b, dest, src);
      break;

   case OPCODE_SLT:
      ptn_slt(b, dest, src);
      break;

   case OPCODE_SGT:
      ptn_sgt(b, dest, src);
      break;

   case OPCODE_SLE:
      ptn_sle(b, dest, src);
      break;

   case OPCODE_SGE:
      ptn_sge(b, dest, src);
      break;

   case OPCODE_SEQ:
      ptn_seq(b, dest, src);
      break;

   case OPCODE_SNE:
      ptn_sne(b, dest, src);
      break;

   case OPCODE_TEX:
   case OPCODE_TXB:
   case OPCODE_TXD:
   case OPCODE_TXL:
   case OPCODE_TXP:
   case OPCODE_TXP_NV:
      ptn_tex(b, dest, src, prog_inst);
      break;

   case OPCODE_SWZ:
      /* Extended swizzles were already handled in ptn_get_src(). */
      ptn_alu(b, nir_op_fmov, dest, src);
      break;

   case OPCODE_NOP:
      break;

   default:
      if (op_trans[op] != 0) {
         ptn_alu(b, op_trans[op], dest, src);
      } else {
         fprintf(stderr, "unknown opcode: %s\n", _mesa_opcode_string(op));
         abort();
      }
      break;
   }

   if (prog_inst->Saturate) {
      assert(prog_inst->Saturate);
      assert(!dest.dest.is_ssa);
      ptn_move_dest(b, dest, nir_fsat(b, ptn_src_for_dest(c, &dest)));
   }
}

/**
 * Puts a NIR intrinsic to store of each PROGRAM_OUTPUT value to the output
 * variables at the end of the shader.
 *
 * We don't generate these incrementally as the PROGRAM_OUTPUT values are
 * written, because there's no output load intrinsic, which means we couldn't
 * handle writemasks.
 */
static void
ptn_add_output_stores(struct ptn_compile *c)
{
   nir_builder *b = &c->build;

   foreach_list_typed(nir_variable, var, node, &b->shader->outputs) {
      nir_intrinsic_instr *store =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_store_var);
      store->num_components = glsl_get_vector_elements(var->type);
      store->variables[0] =
         nir_deref_var_create(store, c->output_vars[var->data.location]);

      if (c->prog->Target == GL_FRAGMENT_PROGRAM_ARB &&
          var->data.location == FRAG_RESULT_DEPTH) {
         /* result.depth has this strange convention of being the .z component of
          * a vec4 with undefined .xyw components.  We resolve it to a scalar, to
          * match GLSL's gl_FragDepth and the expectations of most backends.
          */
         nir_alu_src alu_src = { NIR_SRC_INIT };
         alu_src.src = nir_src_for_reg(c->output_regs[FRAG_RESULT_DEPTH]);
         alu_src.swizzle[0] = SWIZZLE_Z;
         store->src[0] = nir_src_for_ssa(nir_fmov_alu(b, alu_src, 1));
      } else {
         store->src[0].reg.reg = c->output_regs[var->data.location];
      }
      nir_instr_insert_after_cf_list(c->build.cf_node_list, &store->instr);
   }
}

static void
setup_registers_and_variables(struct ptn_compile *c)
{
   nir_builder *b = &c->build;
   struct nir_shader *shader = b->shader;

   /* Create input variables. */
   const int num_inputs = _mesa_flsll(c->prog->InputsRead);
   for (int i = 0; i < num_inputs; i++) {
      if (!(c->prog->InputsRead & BITFIELD64_BIT(i)))
         continue;
      nir_variable *var = rzalloc(shader, nir_variable);
      var->type = glsl_vec4_type();
      var->data.read_only = true;
      var->data.mode = nir_var_shader_in;
      var->name = ralloc_asprintf(var, "in_%d", i);
      var->data.location = i;
      var->data.index = 0;

      if (c->prog->Target == GL_FRAGMENT_PROGRAM_ARB) {
         struct gl_fragment_program *fp =
            (struct gl_fragment_program *) c->prog;

         var->data.interpolation = fp->InterpQualifier[i];

         if (i == VARYING_SLOT_POS) {
            var->data.origin_upper_left = fp->OriginUpperLeft;
            var->data.pixel_center_integer = fp->PixelCenterInteger;
         } else if (i == VARYING_SLOT_FOGC) {
            /* fogcoord is defined as <f, 0.0, 0.0, 1.0>.  Make the actual
             * input variable a float, and create a local containing the
             * full vec4 value.
             */
            var->type = glsl_float_type();

            nir_intrinsic_instr *load_x =
               nir_intrinsic_instr_create(shader, nir_intrinsic_load_var);
            load_x->num_components = 1;
            load_x->variables[0] = nir_deref_var_create(load_x, var);
            nir_ssa_dest_init(&load_x->instr, &load_x->dest, 1, NULL);
            nir_instr_insert_after_cf_list(b->cf_node_list, &load_x->instr);

            nir_ssa_def *f001 = nir_vec4(b, &load_x->dest.ssa, nir_imm_float(b, 0.0),
                                         nir_imm_float(b, 0.0), nir_imm_float(b, 1.0));

            nir_variable *fullvar = rzalloc(shader, nir_variable);
            fullvar->type = glsl_vec4_type();
            fullvar->data.mode = nir_var_local;
            fullvar->name = "fogcoord_tmp";
            exec_list_push_tail(&b->impl->locals, &fullvar->node);

            nir_intrinsic_instr *store =
               nir_intrinsic_instr_create(shader, nir_intrinsic_store_var);
            store->num_components = 4;
            store->variables[0] = nir_deref_var_create(store, fullvar);
            store->src[0] = nir_src_for_ssa(f001);
            nir_instr_insert_after_cf_list(b->cf_node_list, &store->instr);

            /* Insert the real input into the list so the driver has real
             * inputs, but set c->input_vars[i] to the temporary so we use
             * the splatted value.
             */
            exec_list_push_tail(&shader->inputs, &var->node);
            c->input_vars[i] = fullvar;
            continue;
         }
      }

      exec_list_push_tail(&shader->inputs, &var->node);
      c->input_vars[i] = var;
   }

   /* Create output registers and variables. */
   int max_outputs = _mesa_fls(c->prog->OutputsWritten);
   c->output_regs = rzalloc_array(c, nir_register *, max_outputs);

   for (int i = 0; i < max_outputs; i++) {
      if (!(c->prog->OutputsWritten & BITFIELD64_BIT(i)))
         continue;

      /* Since we can't load from outputs in the IR, we make temporaries
       * for the outputs and emit stores to the real outputs at the end of
       * the shader.
       */
      nir_register *reg = nir_local_reg_create(b->impl);
      reg->num_components = 4;

      nir_variable *var = rzalloc(shader, nir_variable);
      if (c->prog->Target == GL_FRAGMENT_PROGRAM_ARB && i == FRAG_RESULT_DEPTH)
         var->type = glsl_float_type();
      else
         var->type = glsl_vec4_type();
      var->data.mode = nir_var_shader_out;
      var->name = ralloc_asprintf(var, "out_%d", i);

      var->data.location = i;
      var->data.index = 0;

      c->output_regs[i] = reg;

      exec_list_push_tail(&shader->outputs, &var->node);
      c->output_vars[i] = var;
   }

   /* Create temporary registers. */
   c->temp_regs = rzalloc_array(c, nir_register *, c->prog->NumTemporaries);

   nir_register *reg;
   for (int i = 0; i < c->prog->NumTemporaries; i++) {
      reg = nir_local_reg_create(b->impl);
      if (!reg) {
         c->error = true;
         return;
      }
      reg->num_components = 4;
      c->temp_regs[i] = reg;
   }

   /* Create the address register (for ARB_vertex_program). */
   reg = nir_local_reg_create(b->impl);
   if (!reg) {
      c->error = true;
      return;
   }
   reg->num_components = 1;
   c->addr_reg = reg;
}

struct nir_shader *
prog_to_nir(const struct gl_program *prog,
            const nir_shader_compiler_options *options)
{
   struct ptn_compile *c;
   struct nir_shader *s;

   c = rzalloc(NULL, struct ptn_compile);
   if (!c)
      return NULL;
   s = nir_shader_create(NULL, options);
   if (!s)
      goto fail;
   c->prog = prog;

   c->parameters = rzalloc(s, nir_variable);
   c->parameters->type = glsl_array_type(glsl_vec4_type(),
                                            prog->Parameters->NumParameters);
   c->parameters->name = "parameters";
   c->parameters->data.read_only = true;
   c->parameters->data.mode = nir_var_uniform;
   exec_list_push_tail(&s->uniforms, &c->parameters->node);

   nir_function *func = nir_function_create(s, "main");
   nir_function_overload *overload = nir_function_overload_create(func);
   nir_function_impl *impl = nir_function_impl_create(overload);

   c->build.shader = s;
   c->build.impl = impl;
   c->build.cf_node_list = &impl->body;

   setup_registers_and_variables(c);
   if (unlikely(c->error))
      goto fail;

   for (unsigned int i = 0; i < prog->NumInstructions; i++) {
      ptn_emit_instruction(c, &prog->Instructions[i]);

      if (unlikely(c->error))
         break;
   }

   ptn_add_output_stores(c);

fail:
   if (c->error) {
      ralloc_free(s);
      s = NULL;
   }
   ralloc_free(c);
   return s;
}
