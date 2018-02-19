/*
 * Copyright (C) 2016 Miklós Máté
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "main/mtypes.h"
#include "main/atifragshader.h"
#include "main/errors.h"
#include "program/prog_parameter.h"

#include "tgsi/tgsi_ureg.h"
#include "tgsi/tgsi_scan.h"
#include "tgsi/tgsi_transform.h"

#include "st_program.h"
#include "st_atifs_to_tgsi.h"

/**
 * Intermediate state used during shader translation.
 */
struct st_translate {
   struct ureg_program *ureg;
   struct ati_fragment_shader *atifs;

   struct ureg_dst temps[MAX_PROGRAM_TEMPS];
   struct ureg_src *constants;
   struct ureg_dst outputs[PIPE_MAX_SHADER_OUTPUTS];
   struct ureg_src inputs[PIPE_MAX_SHADER_INPUTS];
   struct ureg_src samplers[PIPE_MAX_SAMPLERS];

   const ubyte *inputMapping;
   const ubyte *outputMapping;

   unsigned current_pass;

   bool regs_written[MAX_NUM_PASSES_ATI][MAX_NUM_FRAGMENT_REGISTERS_ATI];

   boolean error;
};

struct instruction_desc {
   unsigned TGSI_opcode;
   const char *name;
   unsigned char arg_count;
};

static const struct instruction_desc inst_desc[] = {
   {TGSI_OPCODE_MOV, "MOV", 1},
   {TGSI_OPCODE_NOP, "UND", 0}, /* unused */
   {TGSI_OPCODE_ADD, "ADD", 2},
   {TGSI_OPCODE_MUL, "MUL", 2},
   {TGSI_OPCODE_NOP, "SUB", 2},
   {TGSI_OPCODE_DP3, "DOT3", 2},
   {TGSI_OPCODE_DP4, "DOT4", 2},
   {TGSI_OPCODE_MAD, "MAD", 3},
   {TGSI_OPCODE_LRP, "LERP", 3},
   {TGSI_OPCODE_NOP, "CND", 3},
   {TGSI_OPCODE_NOP, "CND0", 3},
   {TGSI_OPCODE_NOP, "DOT2_ADD", 3}
};

static struct ureg_dst
get_temp(struct st_translate *t, unsigned index)
{
   if (ureg_dst_is_undef(t->temps[index]))
      t->temps[index] = ureg_DECL_temporary(t->ureg);
   return t->temps[index];
}

static struct ureg_src
apply_swizzle(struct st_translate *t,
              struct ureg_src src, GLuint swizzle)
{
   if (swizzle == GL_SWIZZLE_STR_ATI) {
      return src;
   } else if (swizzle == GL_SWIZZLE_STQ_ATI) {
      return ureg_swizzle(src,
                          TGSI_SWIZZLE_X,
                          TGSI_SWIZZLE_Y,
                          TGSI_SWIZZLE_W,
                          TGSI_SWIZZLE_Z);
   } else {
      struct ureg_dst tmp[2];
      struct ureg_src imm[3];

      tmp[0] = get_temp(t, MAX_NUM_FRAGMENT_REGISTERS_ATI);
      tmp[1] = get_temp(t, MAX_NUM_FRAGMENT_REGISTERS_ATI + 1);
      imm[0] = src;
      imm[1] = ureg_imm4f(t->ureg, 1.0f, 1.0f, 0.0f, 0.0f);
      imm[2] = ureg_imm4f(t->ureg, 0.0f, 0.0f, 1.0f, 1.0f);
      ureg_insn(t->ureg, TGSI_OPCODE_MAD, &tmp[0], 1, imm, 3, 0);

      if (swizzle == GL_SWIZZLE_STR_DR_ATI) {
         imm[0] = ureg_scalar(src, TGSI_SWIZZLE_Z);
      } else {
         imm[0] = ureg_scalar(src, TGSI_SWIZZLE_W);
      }
      ureg_insn(t->ureg, TGSI_OPCODE_RCP, &tmp[1], 1, &imm[0], 1, 0);

      imm[0] = ureg_src(tmp[0]);
      imm[1] = ureg_src(tmp[1]);
      ureg_insn(t->ureg, TGSI_OPCODE_MUL, &tmp[0], 1, imm, 2, 0);

      return ureg_src(tmp[0]);
   }
}

static struct ureg_src
get_source(struct st_translate *t, GLuint src_type)
{
   if (src_type >= GL_REG_0_ATI && src_type <= GL_REG_5_ATI) {
      if (t->regs_written[t->current_pass][src_type - GL_REG_0_ATI]) {
         return ureg_src(get_temp(t, src_type - GL_REG_0_ATI));
      } else {
         return ureg_imm1f(t->ureg, 0.0f);
      }
   } else if (src_type >= GL_CON_0_ATI && src_type <= GL_CON_7_ATI) {
      return t->constants[src_type - GL_CON_0_ATI];
   } else if (src_type == GL_ZERO) {
      return ureg_imm1f(t->ureg, 0.0f);
   } else if (src_type == GL_ONE) {
      return ureg_imm1f(t->ureg, 1.0f);
   } else if (src_type == GL_PRIMARY_COLOR_ARB) {
      return t->inputs[t->inputMapping[VARYING_SLOT_COL0]];
   } else if (src_type == GL_SECONDARY_INTERPOLATOR_ATI) {
      return t->inputs[t->inputMapping[VARYING_SLOT_COL1]];
   } else {
      /* frontend prevents this */
      unreachable("unknown source");
   }
}

static struct ureg_src
prepare_argument(struct st_translate *t, const unsigned argId,
                 const struct atifragshader_src_register *srcReg)
{
   struct ureg_src src = get_source(t, srcReg->Index);
   struct ureg_dst arg = get_temp(t, MAX_NUM_FRAGMENT_REGISTERS_ATI + argId);

   switch (srcReg->argRep) {
   case GL_NONE:
      break;
   case GL_RED:
      src = ureg_scalar(src, TGSI_SWIZZLE_X);
      break;
   case GL_GREEN:
      src = ureg_scalar(src, TGSI_SWIZZLE_Y);
      break;
   case GL_BLUE:
      src = ureg_scalar(src, TGSI_SWIZZLE_Z);
      break;
   case GL_ALPHA:
      src = ureg_scalar(src, TGSI_SWIZZLE_W);
      break;
   }
   ureg_insn(t->ureg, TGSI_OPCODE_MOV, &arg, 1, &src, 1, 0);

   if (srcReg->argMod & GL_COMP_BIT_ATI) {
      struct ureg_src modsrc[2];
      modsrc[0] = ureg_imm1f(t->ureg, 1.0f);
      modsrc[1] = ureg_negate(ureg_src(arg));

      ureg_insn(t->ureg, TGSI_OPCODE_ADD, &arg, 1, modsrc, 2, 0);
   }
   if (srcReg->argMod & GL_BIAS_BIT_ATI) {
      struct ureg_src modsrc[2];
      modsrc[0] = ureg_src(arg);
      modsrc[1] = ureg_imm1f(t->ureg, -0.5f);

      ureg_insn(t->ureg, TGSI_OPCODE_ADD, &arg, 1, modsrc, 2, 0);
   }
   if (srcReg->argMod & GL_2X_BIT_ATI) {
      struct ureg_src modsrc[2];
      modsrc[0] = ureg_src(arg);
      modsrc[1] = ureg_src(arg);

      ureg_insn(t->ureg, TGSI_OPCODE_ADD, &arg, 1, modsrc, 2, 0);
   }
   if (srcReg->argMod & GL_NEGATE_BIT_ATI) {
      struct ureg_src modsrc[2];
      modsrc[0] = ureg_src(arg);
      modsrc[1] = ureg_imm1f(t->ureg, -1.0f);

      ureg_insn(t->ureg, TGSI_OPCODE_MUL, &arg, 1, modsrc, 2, 0);
   }
   return  ureg_src(arg);
}

/* These instructions need special treatment */
static void
emit_special_inst(struct st_translate *t, const struct instruction_desc *desc,
                  struct ureg_dst *dst, struct ureg_src *args, unsigned argcount)
{
   struct ureg_dst tmp[1];
   struct ureg_src src[3];

   if (!strcmp(desc->name, "SUB")) {
      ureg_ADD(t->ureg, *dst, args[0], ureg_negate(args[1]));
   } else if (!strcmp(desc->name, "CND")) {
      tmp[0] = get_temp(t, MAX_NUM_FRAGMENT_REGISTERS_ATI + 2); /* re-purpose a3 */
      src[0] = ureg_imm1f(t->ureg, 0.5f);
      src[1] = ureg_negate(args[2]);
      ureg_insn(t->ureg, TGSI_OPCODE_ADD, tmp, 1, src, 2, 0);
      src[0] = ureg_src(tmp[0]);
      src[1] = args[0];
      src[2] = args[1];
      ureg_insn(t->ureg, TGSI_OPCODE_CMP, dst, 1, src, 3, 0);
   } else if (!strcmp(desc->name, "CND0")) {
      src[0] = args[2];
      src[1] = args[1];
      src[2] = args[0];
      ureg_insn(t->ureg, TGSI_OPCODE_CMP, dst, 1, src, 3, 0);
   } else if (!strcmp(desc->name, "DOT2_ADD")) {
      tmp[0] = get_temp(t, MAX_NUM_FRAGMENT_REGISTERS_ATI); /* re-purpose a1 */
      src[0] = args[0];
      src[1] = args[1];
      ureg_insn(t->ureg, TGSI_OPCODE_DP2, tmp, 1, src, 2, 0);
      src[0] = ureg_src(tmp[0]);
      src[1] = ureg_scalar(args[2], TGSI_SWIZZLE_Z);
      ureg_insn(t->ureg, TGSI_OPCODE_ADD, dst, 1, src, 2, 0);
   }
}

static void
emit_arith_inst(struct st_translate *t,
                const struct instruction_desc *desc,
                struct ureg_dst *dst, struct ureg_src *args, unsigned argcount)
{
   if (desc->TGSI_opcode == TGSI_OPCODE_NOP) {
      emit_special_inst(t, desc, dst, args, argcount);
      return;
   }

   ureg_insn(t->ureg, desc->TGSI_opcode, dst, 1, args, argcount, 0);
}

static void
emit_dstmod(struct st_translate *t,
            struct ureg_dst dst, GLuint dstMod)
{
   float imm;
   struct ureg_src src[3];
   GLuint scale = dstMod & ~GL_SATURATE_BIT_ATI;

   if (dstMod == GL_NONE) {
      return;
   }

   switch (scale) {
   case GL_2X_BIT_ATI:
      imm = 2.0f;
      break;
   case GL_4X_BIT_ATI:
      imm = 4.0f;
      break;
   case GL_8X_BIT_ATI:
      imm = 8.0f;
      break;
   case GL_HALF_BIT_ATI:
      imm = 0.5f;
      break;
   case GL_QUARTER_BIT_ATI:
      imm = 0.25f;
      break;
   case GL_EIGHTH_BIT_ATI:
      imm = 0.125f;
      break;
   default:
      imm = 1.0f;
   }

   src[0] = ureg_src(dst);
   src[1] = ureg_imm1f(t->ureg, imm);
   if (dstMod & GL_SATURATE_BIT_ATI) {
      dst = ureg_saturate(dst);
   }
   ureg_insn(t->ureg, TGSI_OPCODE_MUL, &dst, 1, src, 2, 0);
}

/**
 * Compile one setup instruction to TGSI instructions.
 */
static void
compile_setupinst(struct st_translate *t,
                  const unsigned r,
                  const struct atifs_setupinst *texinst)
{
   struct ureg_dst dst[1];
   struct ureg_src src[2];

   if (!texinst->Opcode)
      return;

   dst[0] = get_temp(t, r);

   GLuint pass_tex = texinst->src;

   if (pass_tex >= GL_TEXTURE0_ARB && pass_tex <= GL_TEXTURE7_ARB) {
      unsigned attr = pass_tex - GL_TEXTURE0_ARB + VARYING_SLOT_TEX0;

      src[0] = t->inputs[t->inputMapping[attr]];
   } else if (pass_tex >= GL_REG_0_ATI && pass_tex <= GL_REG_5_ATI) {
      unsigned reg = pass_tex - GL_REG_0_ATI;

      /* the frontend already validated that REG is only allowed in second pass */
      if (t->regs_written[0][reg]) {
         src[0] = ureg_src(t->temps[reg]);
      } else {
         src[0] = ureg_imm1f(t->ureg, 0.0f);
      }
   }
   src[0] = apply_swizzle(t, src[0], texinst->swizzle);

   if (texinst->Opcode == ATI_FRAGMENT_SHADER_SAMPLE_OP) {
      /* by default texture and sampler indexes are the same */
      src[1] = t->samplers[r];
      /* the texture target is still unknown, it will be fixed in the draw call */
      ureg_tex_insn(t->ureg, TGSI_OPCODE_TEX, dst, 1, TGSI_TEXTURE_2D,
                    TGSI_RETURN_TYPE_FLOAT, NULL, 0, src, 2);
   } else if (texinst->Opcode == ATI_FRAGMENT_SHADER_PASS_OP) {
      ureg_insn(t->ureg, TGSI_OPCODE_MOV, dst, 1, src, 1, 0);
   }

   t->regs_written[t->current_pass][r] = true;
}

/**
 * Compile one arithmetic operation COLOR&ALPHA pair into TGSI instructions.
 */
static void
compile_instruction(struct st_translate *t,
                    const struct atifs_instruction *inst)
{
   unsigned optype;

   for (optype = 0; optype < 2; optype++) { /* color, alpha */
      const struct instruction_desc *desc;
      struct ureg_dst dst[1];
      struct ureg_src args[3]; /* arguments for the main operation */
      unsigned arg;
      unsigned dstreg = inst->DstReg[optype].Index - GL_REG_0_ATI;

      if (!inst->Opcode[optype])
         continue;

      desc = &inst_desc[inst->Opcode[optype] - GL_MOV_ATI];

      /* prepare the arguments */
      for (arg = 0; arg < desc->arg_count; arg++) {
         if (arg >= inst->ArgCount[optype]) {
            _mesa_warning(0, "Using 0 for missing argument %d of %s\n",
                          arg, desc->name);
            args[arg] = ureg_imm1f(t->ureg, 0.0f);
         } else {
            args[arg] = prepare_argument(t, arg,
                                         &inst->SrcReg[optype][arg]);
         }
      }

      /* prepare dst */
      dst[0] = get_temp(t, dstreg);

      if (optype) {
         dst[0] = ureg_writemask(dst[0], TGSI_WRITEMASK_W);
      } else {
         GLuint dstMask = inst->DstReg[optype].dstMask;
         if (dstMask == GL_NONE) {
            dst[0] = ureg_writemask(dst[0], TGSI_WRITEMASK_XYZ);
         } else {
            dst[0] = ureg_writemask(dst[0], dstMask); /* the enum values match */
         }
      }

      /* emit the main instruction */
      emit_arith_inst(t, desc, dst, args, arg);

      emit_dstmod(t, *dst, inst->DstReg[optype].dstMod);

      t->regs_written[t->current_pass][dstreg] = true;
   }
}

static void
finalize_shader(struct st_translate *t, unsigned numPasses)
{
   struct ureg_dst dst[1] = { { 0 } };
   struct ureg_src src[1] = { { 0 } };

   if (t->regs_written[numPasses-1][0]) {
      /* copy the result into the OUT slot */
      dst[0] = t->outputs[t->outputMapping[FRAG_RESULT_COLOR]];
      src[0] = ureg_src(t->temps[0]);
      ureg_insn(t->ureg, TGSI_OPCODE_MOV, dst, 1, src, 1, 0);
   }

   /* signal the end of the program */
   ureg_insn(t->ureg, TGSI_OPCODE_END, dst, 0, src, 0, 0);
}

/**
 * Called when a new variant is needed, we need to translate
 * the ATI fragment shader to TGSI
 */
enum pipe_error
st_translate_atifs_program(
   struct ureg_program *ureg,
   struct ati_fragment_shader *atifs,
   struct gl_program *program,
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
   enum pipe_error ret = PIPE_OK;

   unsigned pass, i, r;

   struct st_translate translate, *t;
   t = &translate;
   memset(t, 0, sizeof *t);

   t->inputMapping = inputMapping;
   t->outputMapping = outputMapping;
   t->ureg = ureg;
   t->atifs = atifs;

   /*
    * Declare input attributes.
    */
   for (i = 0; i < numInputs; i++) {
      t->inputs[i] = ureg_DECL_fs_input(ureg,
                                        inputSemanticName[i],
                                        inputSemanticIndex[i],
                                        interpMode[i]);
   }

   /*
    * Declare output attributes:
    *  we always have numOutputs=1 and it's FRAG_RESULT_COLOR
    */
   t->outputs[0] = ureg_DECL_output(ureg,
                                    TGSI_SEMANTIC_COLOR,
                                    outputSemanticIndex[0]);

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
         switch (program->Parameters->Parameters[i].Type) {
         case PROGRAM_STATE_VAR:
         case PROGRAM_UNIFORM:
            t->constants[i] = ureg_DECL_constant(ureg, i);
            break;
         case PROGRAM_CONSTANT:
            t->constants[i] =
               ureg_DECL_immediate(ureg,
                                   (const float*)program->Parameters->ParameterValues[i],
                                   4);
            break;
         default:
            break;
         }
      }
   }

   /* texture samplers */
   for (i = 0; i < MAX_NUM_FRAGMENT_REGISTERS_ATI; i++) {
      if (program->SamplersUsed & (1 << i)) {
         t->samplers[i] = ureg_DECL_sampler(ureg, i);
         /* the texture target is still unknown, it will be fixed in the draw call */
         ureg_DECL_sampler_view(ureg, i, TGSI_TEXTURE_2D,
                                TGSI_RETURN_TYPE_FLOAT,
                                TGSI_RETURN_TYPE_FLOAT,
                                TGSI_RETURN_TYPE_FLOAT,
                                TGSI_RETURN_TYPE_FLOAT);
      }
   }

   /* emit instructions */
   for (pass = 0; pass < atifs->NumPasses; pass++) {
      t->current_pass = pass;
      for (r = 0; r < MAX_NUM_FRAGMENT_REGISTERS_ATI; r++) {
         struct atifs_setupinst *texinst = &atifs->SetupInst[pass][r];
         compile_setupinst(t, r, texinst);
      }
      for (i = 0; i < atifs->numArithInstr[pass]; i++) {
         struct atifs_instruction *inst = &atifs->Instructions[pass][i];
         compile_instruction(t, inst);
      }
   }

   finalize_shader(t, atifs->NumPasses);

out:
   free(t->constants);

   if (t->error) {
      debug_printf("%s: translate error flag set\n", __func__);
   }

   return ret;
}

/**
 * Called in ProgramStringNotify, we need to fill the metadata of the
 * gl_program attached to the ati_fragment_shader
 */
void
st_init_atifs_prog(struct gl_context *ctx, struct gl_program *prog)
{
   /* we know this is st_fragment_program, because of st_new_ati_fs() */
   struct st_fragment_program *stfp = (struct st_fragment_program *) prog;
   struct ati_fragment_shader *atifs = stfp->ati_fs;

   unsigned pass, i, r, optype, arg;

   static const gl_state_index16 fog_params_state[STATE_LENGTH] =
      {STATE_INTERNAL, STATE_FOG_PARAMS_OPTIMIZED, 0, 0, 0};
   static const gl_state_index16 fog_color[STATE_LENGTH] =
      {STATE_FOG_COLOR, 0, 0, 0, 0};

   prog->info.inputs_read = 0;
   prog->info.outputs_written = BITFIELD64_BIT(FRAG_RESULT_COLOR);
   prog->SamplersUsed = 0;
   prog->Parameters = _mesa_new_parameter_list();

   /* fill in inputs_read, SamplersUsed, TexturesUsed */
   for (pass = 0; pass < atifs->NumPasses; pass++) {
      for (r = 0; r < MAX_NUM_FRAGMENT_REGISTERS_ATI; r++) {
         struct atifs_setupinst *texinst = &atifs->SetupInst[pass][r];
         GLuint pass_tex = texinst->src;

         if (texinst->Opcode == ATI_FRAGMENT_SHADER_SAMPLE_OP) {
            /* mark which texcoords are used */
            prog->info.inputs_read |= BITFIELD64_BIT(VARYING_SLOT_TEX0 + pass_tex - GL_TEXTURE0_ARB);
            /* by default there is 1:1 mapping between samplers and textures */
            prog->SamplersUsed |= (1 << r);
            /* the target is unknown here, it will be fixed in the draw call */
            prog->TexturesUsed[r] = TEXTURE_2D_BIT;
         } else if (texinst->Opcode == ATI_FRAGMENT_SHADER_PASS_OP) {
            if (pass_tex >= GL_TEXTURE0_ARB && pass_tex <= GL_TEXTURE7_ARB) {
               prog->info.inputs_read |= BITFIELD64_BIT(VARYING_SLOT_TEX0 + pass_tex - GL_TEXTURE0_ARB);
            }
         }
      }
   }
   for (pass = 0; pass < atifs->NumPasses; pass++) {
      for (i = 0; i < atifs->numArithInstr[pass]; i++) {
         struct atifs_instruction *inst = &atifs->Instructions[pass][i];

         for (optype = 0; optype < 2; optype++) { /* color, alpha */
            if (inst->Opcode[optype]) {
               for (arg = 0; arg < inst->ArgCount[optype]; arg++) {
                  GLint index = inst->SrcReg[optype][arg].Index;
                  if (index == GL_PRIMARY_COLOR_EXT) {
                     prog->info.inputs_read |= BITFIELD64_BIT(VARYING_SLOT_COL0);
                  } else if (index == GL_SECONDARY_INTERPOLATOR_ATI) {
                     /* note: ATI_fragment_shader.txt never specifies what
                      * GL_SECONDARY_INTERPOLATOR_ATI is, swrast uses
                      * VARYING_SLOT_COL1 for this input */
                     prog->info.inputs_read |= BITFIELD64_BIT(VARYING_SLOT_COL1);
                  }
               }
            }
         }
      }
   }
   /* we may need fog */
   prog->info.inputs_read |= BITFIELD64_BIT(VARYING_SLOT_FOGC);

   /* we always have the ATI_fs constants, and the fog params */
   for (i = 0; i < MAX_NUM_FRAGMENT_CONSTANTS_ATI; i++) {
      _mesa_add_parameter(prog->Parameters, PROGRAM_UNIFORM,
                          NULL, 4, GL_FLOAT, NULL, NULL);
   }
   _mesa_add_state_reference(prog->Parameters, fog_params_state);
   _mesa_add_state_reference(prog->Parameters, fog_color);
}


struct tgsi_atifs_transform {
   struct tgsi_transform_context base;
   struct tgsi_shader_info info;
   const struct st_fp_variant_key *key;
   bool first_instruction_emitted;
   unsigned fog_factor_temp;
};

static inline struct tgsi_atifs_transform *
tgsi_atifs_transform(struct tgsi_transform_context *tctx)
{
   return (struct tgsi_atifs_transform *)tctx;
}

/* copied from st_cb_drawpixels_shader.c */
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
   if (file == TGSI_FILE_CONSTANT) {
      inst->Src[i].Register.Dimension = 1;
      inst->Src[i].Dimension.Index = 0;
   }
}

#define SET_SRC(inst, i, file, index, x, y, z, w) \
   set_src(inst, i, file, index, TGSI_SWIZZLE_##x, TGSI_SWIZZLE_##y, \
           TGSI_SWIZZLE_##z, TGSI_SWIZZLE_##w)

static void
transform_decl(struct tgsi_transform_context *tctx,
               struct tgsi_full_declaration *decl)
{
   struct tgsi_atifs_transform *ctx = tgsi_atifs_transform(tctx);

   if (decl->Declaration.File == TGSI_FILE_SAMPLER_VIEW) {
      /* fix texture target */
      unsigned newtarget = ctx->key->texture_targets[decl->Range.First];
      if (newtarget)
         decl->SamplerView.Resource = newtarget;
   }

   tctx->emit_declaration(tctx, decl);
}

static void
transform_instr(struct tgsi_transform_context *tctx,
                struct tgsi_full_instruction *current_inst)
{
   struct tgsi_atifs_transform *ctx = tgsi_atifs_transform(tctx);

   if (ctx->first_instruction_emitted)
      goto transform_inst;

   ctx->first_instruction_emitted = true;

   if (ctx->key->fog) {
      /* add a new temp for the fog factor */
      ctx->fog_factor_temp = ctx->info.file_max[TGSI_FILE_TEMPORARY] + 1;
      tgsi_transform_temp_decl(tctx, ctx->fog_factor_temp);
   }

transform_inst:
   if (current_inst->Instruction.Opcode == TGSI_OPCODE_TEX) {
      /* fix texture target */
      unsigned newtarget = ctx->key->texture_targets[current_inst->Src[1].Register.Index];
      if (newtarget)
         current_inst->Texture.Texture = newtarget;

   } else if (ctx->key->fog && current_inst->Instruction.Opcode == TGSI_OPCODE_MOV &&
              current_inst->Dst[0].Register.File == TGSI_FILE_OUTPUT) {
      struct tgsi_full_instruction inst;
      unsigned i;
      int fogc_index = -1;
      int reg0_index = current_inst->Src[0].Register.Index;

      /* find FOGC input */
      for (i = 0; i < ctx->info.num_inputs; i++) {
         if (ctx->info.input_semantic_name[i] == TGSI_SEMANTIC_FOG) {
            fogc_index = i;
            break;
         }
      }
      if (fogc_index < 0) {
         /* should never be reached, because fog coord input is always declared */
         tctx->emit_instruction(tctx, current_inst);
         return;
      }

      /* compute the 1 component fog factor f */
      if (ctx->key->fog == FOG_LINEAR) {
         /* LINEAR formula: f = (end - z) / (end - start)
          * with optimized parameters:
          *    f = MAD(fogcoord, oparams.x, oparams.y)
          */
         inst = tgsi_default_full_instruction();
         inst.Instruction.Opcode = TGSI_OPCODE_MAD;
         inst.Instruction.NumDstRegs = 1;
         inst.Dst[0].Register.File  = TGSI_FILE_TEMPORARY;
         inst.Dst[0].Register.Index = ctx->fog_factor_temp;
         inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_XYZW;
         inst.Instruction.NumSrcRegs = 3;
         SET_SRC(&inst, 0, TGSI_FILE_INPUT, fogc_index, X, Y, Z, W);
         SET_SRC(&inst, 1, TGSI_FILE_CONSTANT, MAX_NUM_FRAGMENT_CONSTANTS_ATI, X, X, X, X);
         SET_SRC(&inst, 2, TGSI_FILE_CONSTANT, MAX_NUM_FRAGMENT_CONSTANTS_ATI, Y, Y, Y, Y);
         tctx->emit_instruction(tctx, &inst);
      } else if (ctx->key->fog == FOG_EXP) {
         /* EXP formula: f = exp(-dens * z)
          * with optimized parameters:
          *    f = MUL(fogcoord, oparams.z); f= EX2(-f)
          */
         inst = tgsi_default_full_instruction();
         inst.Instruction.Opcode = TGSI_OPCODE_MUL;
         inst.Instruction.NumDstRegs = 1;
         inst.Dst[0].Register.File  = TGSI_FILE_TEMPORARY;
         inst.Dst[0].Register.Index = ctx->fog_factor_temp;
         inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_XYZW;
         inst.Instruction.NumSrcRegs = 2;
         SET_SRC(&inst, 0, TGSI_FILE_INPUT, fogc_index, X, Y, Z, W);
         SET_SRC(&inst, 1, TGSI_FILE_CONSTANT, MAX_NUM_FRAGMENT_CONSTANTS_ATI, Z, Z, Z, Z);
         tctx->emit_instruction(tctx, &inst);

         inst = tgsi_default_full_instruction();
         inst.Instruction.Opcode = TGSI_OPCODE_EX2;
         inst.Instruction.NumDstRegs = 1;
         inst.Dst[0].Register.File  = TGSI_FILE_TEMPORARY;
         inst.Dst[0].Register.Index = ctx->fog_factor_temp;
         inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_XYZW;
         inst.Instruction.NumSrcRegs = 1;
         SET_SRC(&inst, 0, TGSI_FILE_TEMPORARY, ctx->fog_factor_temp, X, Y, Z, W);
         inst.Src[0].Register.Negate = 1;
         tctx->emit_instruction(tctx, &inst);
      } else if (ctx->key->fog == FOG_EXP2) {
         /* EXP2 formula: f = exp(-(dens * z)^2)
          * with optimized parameters:
          *    f = MUL(fogcoord, oparams.w); f=MUL(f, f); f= EX2(-f)
          */
         inst = tgsi_default_full_instruction();
         inst.Instruction.Opcode = TGSI_OPCODE_MUL;
         inst.Instruction.NumDstRegs = 1;
         inst.Dst[0].Register.File  = TGSI_FILE_TEMPORARY;
         inst.Dst[0].Register.Index = ctx->fog_factor_temp;
         inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_XYZW;
         inst.Instruction.NumSrcRegs = 2;
         SET_SRC(&inst, 0, TGSI_FILE_INPUT, fogc_index, X, Y, Z, W);
         SET_SRC(&inst, 1, TGSI_FILE_CONSTANT, MAX_NUM_FRAGMENT_CONSTANTS_ATI, W, W, W, W);
         tctx->emit_instruction(tctx, &inst);

         inst = tgsi_default_full_instruction();
         inst.Instruction.Opcode = TGSI_OPCODE_MUL;
         inst.Instruction.NumDstRegs = 1;
         inst.Dst[0].Register.File  = TGSI_FILE_TEMPORARY;
         inst.Dst[0].Register.Index = ctx->fog_factor_temp;
         inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_XYZW;
         inst.Instruction.NumSrcRegs = 2;
         SET_SRC(&inst, 0, TGSI_FILE_TEMPORARY, ctx->fog_factor_temp, X, Y, Z, W);
         SET_SRC(&inst, 1, TGSI_FILE_TEMPORARY, ctx->fog_factor_temp, X, Y, Z, W);
         tctx->emit_instruction(tctx, &inst);

         inst = tgsi_default_full_instruction();
         inst.Instruction.Opcode = TGSI_OPCODE_EX2;
         inst.Instruction.NumDstRegs = 1;
         inst.Dst[0].Register.File  = TGSI_FILE_TEMPORARY;
         inst.Dst[0].Register.Index = ctx->fog_factor_temp;
         inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_XYZW;
         inst.Instruction.NumSrcRegs = 1;
         SET_SRC(&inst, 0, TGSI_FILE_TEMPORARY, ctx->fog_factor_temp, X, Y, Z, W);
         inst.Src[0].Register.Negate ^= 1;
         tctx->emit_instruction(tctx, &inst);
      }
      /* f = saturate(f) */
      inst = tgsi_default_full_instruction();
      inst.Instruction.Opcode = TGSI_OPCODE_MOV;
      inst.Instruction.NumDstRegs = 1;
      inst.Instruction.Saturate = 1;
      inst.Dst[0].Register.File  = TGSI_FILE_TEMPORARY;
      inst.Dst[0].Register.Index = ctx->fog_factor_temp;
      inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_XYZW;
      inst.Instruction.NumSrcRegs = 1;
      SET_SRC(&inst, 0, TGSI_FILE_TEMPORARY, ctx->fog_factor_temp, X, Y, Z, W);
      tctx->emit_instruction(tctx, &inst);

      /* REG0 = LRP(f, REG0, fogcolor) */
      inst = tgsi_default_full_instruction();
      inst.Instruction.Opcode = TGSI_OPCODE_LRP;
      inst.Instruction.NumDstRegs = 1;
      inst.Dst[0].Register.File  = TGSI_FILE_TEMPORARY;
      inst.Dst[0].Register.Index = reg0_index;
      inst.Dst[0].Register.WriteMask = TGSI_WRITEMASK_XYZW;
      inst.Instruction.NumSrcRegs = 3;
      SET_SRC(&inst, 0, TGSI_FILE_TEMPORARY, ctx->fog_factor_temp, X, X, X, Y);
      SET_SRC(&inst, 1, TGSI_FILE_TEMPORARY, reg0_index, X, Y, Z, W);
      SET_SRC(&inst, 2, TGSI_FILE_CONSTANT, MAX_NUM_FRAGMENT_CONSTANTS_ATI + 1, X, Y, Z, W);
      tctx->emit_instruction(tctx, &inst);
   }

   tctx->emit_instruction(tctx, current_inst);
}

/*
 * A post-process step in the draw call to fix texture targets and
 * insert code for fog.
 */
const struct tgsi_token *
st_fixup_atifs(const struct tgsi_token *tokens,
               const struct st_fp_variant_key *key)
{
   struct tgsi_atifs_transform ctx;
   struct tgsi_token *newtoks;
   int newlen;

   memset(&ctx, 0, sizeof(ctx));
   ctx.base.transform_declaration = transform_decl;
   ctx.base.transform_instruction = transform_instr;
   ctx.key = key;
   tgsi_scan_shader(tokens, &ctx.info);

   newlen = tgsi_num_tokens(tokens) + 30;
   newtoks = tgsi_alloc_tokens(newlen);
   if (!newtoks)
      return NULL;

   tgsi_transform_shader(tokens, newtoks, newlen, &ctx.base);
   return newtoks;
}

