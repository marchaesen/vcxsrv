/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2007  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * \file program.c
 * Vertex and fragment program support functions.
 * \author Brian Paul
 */


#include "main/glheader.h"
#include "main/context.h"
#include "main/framebuffer.h"
#include "main/hash.h"
#include "main/macros.h"
#include "main/shaderobj.h"
#include "program.h"
#include "prog_cache.h"
#include "prog_parameter.h"
#include "prog_instruction.h"
#include "util/bitscan.h"
#include "util/ralloc.h"
#include "util/u_atomic.h"


/**
 * A pointer to this dummy program is put into the hash table when
 * glGenPrograms is called.
 */
struct gl_program _mesa_DummyProgram;


/**
 * Init context's vertex/fragment program state
 */
void
_mesa_init_program(struct gl_context *ctx)
{
   /*
    * If this assertion fails, we need to increase the field
    * size for register indexes (see INST_INDEX_BITS).
    */
   assert(ctx->Const.Program[MESA_SHADER_VERTEX].MaxUniformComponents / 4
          <= (1 << INST_INDEX_BITS));
   assert(ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxUniformComponents / 4
          <= (1 << INST_INDEX_BITS));

   assert(ctx->Const.Program[MESA_SHADER_VERTEX].MaxTemps <= (1 << INST_INDEX_BITS));
   assert(ctx->Const.Program[MESA_SHADER_VERTEX].MaxLocalParams <= (1 << INST_INDEX_BITS));
   assert(ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTemps <= (1 << INST_INDEX_BITS));
   assert(ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxLocalParams <= (1 << INST_INDEX_BITS));

   assert(ctx->Const.Program[MESA_SHADER_VERTEX].MaxUniformComponents <= 4 * MAX_UNIFORMS);
   assert(ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxUniformComponents <= 4 * MAX_UNIFORMS);

   assert(ctx->Const.Program[MESA_SHADER_VERTEX].MaxAddressOffset <= (1 << INST_INDEX_BITS));
   assert(ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxAddressOffset <= (1 << INST_INDEX_BITS));

   /* If this fails, increase prog_instruction::TexSrcUnit size */
   STATIC_ASSERT(MAX_TEXTURE_UNITS <= (1 << 5));

   /* If this fails, increase prog_instruction::TexSrcTarget size */
   STATIC_ASSERT(NUM_TEXTURE_TARGETS <= (1 << 4));

   ctx->Program.ErrorPos = -1;
   ctx->Program.ErrorString = strdup("");

   ctx->VertexProgram.Enabled = GL_FALSE;
   ctx->VertexProgram.PointSizeEnabled =
      (ctx->API == API_OPENGLES2) ? GL_TRUE : GL_FALSE;
   ctx->VertexProgram.TwoSideEnabled = GL_FALSE;
   _mesa_reference_program(ctx, &ctx->VertexProgram.Current,
                           ctx->Shared->DefaultVertexProgram);
   assert(ctx->VertexProgram.Current);
   ctx->VertexProgram.Cache = _mesa_new_program_cache();

   ctx->FragmentProgram.Enabled = GL_FALSE;
   _mesa_reference_program(ctx, &ctx->FragmentProgram.Current,
                           ctx->Shared->DefaultFragmentProgram);
   assert(ctx->FragmentProgram.Current);
   ctx->FragmentProgram.Cache = _mesa_new_program_cache();

   /* XXX probably move this stuff */
   ctx->ATIFragmentShader.Enabled = GL_FALSE;
   ctx->ATIFragmentShader.Current = ctx->Shared->DefaultFragmentShader;
   assert(ctx->ATIFragmentShader.Current);
   ctx->ATIFragmentShader.Current->RefCount++;
}


/**
 * Free a context's vertex/fragment program state
 */
void
_mesa_free_program_data(struct gl_context *ctx)
{
   _mesa_reference_program(ctx, &ctx->VertexProgram.Current, NULL);
   _mesa_delete_program_cache(ctx, ctx->VertexProgram.Cache);
   _mesa_reference_program(ctx, &ctx->FragmentProgram.Current, NULL);
   _mesa_delete_shader_cache(ctx, ctx->FragmentProgram.Cache);

   /* XXX probably move this stuff */
   if (ctx->ATIFragmentShader.Current) {
      ctx->ATIFragmentShader.Current->RefCount--;
      if (ctx->ATIFragmentShader.Current->RefCount <= 0) {
         free(ctx->ATIFragmentShader.Current);
      }
   }

   free((void *) ctx->Program.ErrorString);
}


/**
 * Update the default program objects in the given context to reference those
 * specified in the shared state and release those referencing the old
 * shared state.
 */
void
_mesa_update_default_objects_program(struct gl_context *ctx)
{
   _mesa_reference_program(ctx, &ctx->VertexProgram.Current,
                           ctx->Shared->DefaultVertexProgram);
   assert(ctx->VertexProgram.Current);

   _mesa_reference_program(ctx, &ctx->FragmentProgram.Current,
                            ctx->Shared->DefaultFragmentProgram);
   assert(ctx->FragmentProgram.Current);

   /* XXX probably move this stuff */
   if (ctx->ATIFragmentShader.Current) {
      ctx->ATIFragmentShader.Current->RefCount--;
      if (ctx->ATIFragmentShader.Current->RefCount <= 0) {
         free(ctx->ATIFragmentShader.Current);
      }
   }
   ctx->ATIFragmentShader.Current = (struct ati_fragment_shader *) ctx->Shared->DefaultFragmentShader;
   assert(ctx->ATIFragmentShader.Current);
   ctx->ATIFragmentShader.Current->RefCount++;
}


/**
 * Set the vertex/fragment program error state (position and error string).
 * This is generally called from within the parsers.
 */
void
_mesa_set_program_error(struct gl_context *ctx, GLint pos, const char *string)
{
   ctx->Program.ErrorPos = pos;
   free((void *) ctx->Program.ErrorString);
   if (!string)
      string = "";
   ctx->Program.ErrorString = strdup(string);
}


/**
 * Initialize a new gl_program object.
 */
struct gl_program *
_mesa_init_gl_program(struct gl_program *prog, GLenum target, GLuint id,
                      bool is_arb_asm)
{
   if (!prog)
      return NULL;

   memset(prog, 0, sizeof(*prog));
   prog->Id = id;
   prog->Target = target;
   prog->RefCount = 1;
   prog->Format = GL_PROGRAM_FORMAT_ASCII_ARB;
   prog->info.stage = _mesa_program_enum_to_shader_stage(target);
   prog->is_arb_asm = is_arb_asm;

   /* Uniforms that lack an initializer in the shader code have an initial
    * value of zero.  This includes sampler uniforms.
    *
    * Page 24 (page 30 of the PDF) of the GLSL 1.20 spec says:
    *
    *     "The link time initial value is either the value of the variable's
    *     initializer, if present, or 0 if no initializer is present. Sampler
    *     types cannot have initializers."
    *
    * So we only initialise ARB assembly style programs.
    */
   if (is_arb_asm) {
      /* default mapping from samplers to texture units */
      for (unsigned i = 0; i < MAX_SAMPLERS; i++)
         prog->SamplerUnits[i] = i;
   }

   return prog;
}


/**
 * Allocate and initialize a new fragment/vertex program object but
 * don't put it into the program hash table.  Called via
 * ctx->Driver.NewProgram.  May be overridden (ie. replaced) by a
 * device driver function to implement OO deriviation with additional
 * types not understood by this function.
 *
 * \param ctx  context
 * \param id   program id/number
 * \param target  program target/type
 * \return  pointer to new program object
 */
struct gl_program *
_mesa_new_program(struct gl_context *ctx, GLenum target, GLuint id,
                  bool is_arb_asm)
{
   switch (target) {
   case GL_VERTEX_PROGRAM_ARB: /* == GL_VERTEX_PROGRAM_NV */
   case GL_GEOMETRY_PROGRAM_NV:
   case GL_TESS_CONTROL_PROGRAM_NV:
   case GL_TESS_EVALUATION_PROGRAM_NV:
   case GL_FRAGMENT_PROGRAM_ARB:
   case GL_COMPUTE_PROGRAM_NV: {
      struct gl_program *prog = rzalloc(NULL, struct gl_program);
      return _mesa_init_gl_program(prog, target, id, is_arb_asm);
   }
   default:
      _mesa_problem(ctx, "bad target in _mesa_new_program");
      return NULL;
   }
}


/**
 * Delete a program and remove it from the hash table, ignoring the
 * reference count.
 * Called via ctx->Driver.DeleteProgram.  May be wrapped (OO deriviation)
 * by a device driver function.
 */
void
_mesa_delete_program(struct gl_context *ctx, struct gl_program *prog)
{
   (void) ctx;
   assert(prog);
   assert(prog->RefCount==0);

   if (prog == &_mesa_DummyProgram)
      return;

   if (prog->Parameters) {
      _mesa_free_parameter_list(prog->Parameters);
   }

   if (prog->nir) {
      ralloc_free(prog->nir);
   }

   if (prog->sh.BindlessSamplers) {
      ralloc_free(prog->sh.BindlessSamplers);
   }

   if (prog->sh.BindlessImages) {
      ralloc_free(prog->sh.BindlessImages);
   }

   if (prog->driver_cache_blob) {
      ralloc_free(prog->driver_cache_blob);
   }

   ralloc_free(prog);
}


/**
 * Return the gl_program object for a given ID.
 * Basically just a wrapper for _mesa_HashLookup() to avoid a lot of
 * casts elsewhere.
 */
struct gl_program *
_mesa_lookup_program(struct gl_context *ctx, GLuint id)
{
   if (id)
      return (struct gl_program *) _mesa_HashLookup(ctx->Shared->Programs, id);
   else
      return NULL;
}


/**
 * Reference counting for vertex/fragment programs
 * This is normally only called from the _mesa_reference_program() macro
 * when there's a real pointer change.
 */
void
_mesa_reference_program_(struct gl_context *ctx,
                         struct gl_program **ptr,
                         struct gl_program *prog)
{
#ifndef NDEBUG
   assert(ptr);
   if (*ptr && prog) {
      /* sanity check */
      if ((*ptr)->Target == GL_VERTEX_PROGRAM_ARB)
         assert(prog->Target == GL_VERTEX_PROGRAM_ARB);
      else if ((*ptr)->Target == GL_FRAGMENT_PROGRAM_ARB)
         assert(prog->Target == GL_FRAGMENT_PROGRAM_ARB ||
                prog->Target == GL_FRAGMENT_PROGRAM_NV);
      else if ((*ptr)->Target == GL_GEOMETRY_PROGRAM_NV)
         assert(prog->Target == GL_GEOMETRY_PROGRAM_NV);
   }
#endif

   if (*ptr) {
      struct gl_program *oldProg = *ptr;

      assert(oldProg->RefCount > 0);

      if (p_atomic_dec_zero(&oldProg->RefCount)) {
         assert(ctx);
         _mesa_reference_shader_program_data(ctx, &oldProg->sh.data, NULL);
         ctx->Driver.DeleteProgram(ctx, oldProg);
      }

      *ptr = NULL;
   }

   assert(!*ptr);
   if (prog) {
      p_atomic_inc(&prog->RefCount);
   }

   *ptr = prog;
}


/**
 * Insert 'count' NOP instructions at 'start' in the given program.
 * Adjust branch targets accordingly.
 */
GLboolean
_mesa_insert_instructions(struct gl_program *prog, GLuint start, GLuint count)
{
   const GLuint origLen = prog->arb.NumInstructions;
   const GLuint newLen = origLen + count;
   struct prog_instruction *newInst;
   GLuint i;

   /* adjust branches */
   for (i = 0; i < prog->arb.NumInstructions; i++) {
      struct prog_instruction *inst = prog->arb.Instructions + i;
      if (inst->BranchTarget > 0) {
         if ((GLuint)inst->BranchTarget >= start) {
            inst->BranchTarget += count;
         }
      }
   }

   /* Alloc storage for new instructions */
   newInst = rzalloc_array(prog, struct prog_instruction, newLen);
   if (!newInst) {
      return GL_FALSE;
   }

   /* Copy 'start' instructions into new instruction buffer */
   _mesa_copy_instructions(newInst, prog->arb.Instructions, start);

   /* init the new instructions */
   _mesa_init_instructions(newInst + start, count);

   /* Copy the remaining/tail instructions to new inst buffer */
   _mesa_copy_instructions(newInst + start + count,
                           prog->arb.Instructions + start,
                           origLen - start);

   /* free old instructions */
   ralloc_free(prog->arb.Instructions);

   /* install new instructions */
   prog->arb.Instructions = newInst;
   prog->arb.NumInstructions = newLen;

   return GL_TRUE;
}

/**
 * Delete 'count' instructions at 'start' in the given program.
 * Adjust branch targets accordingly.
 */
GLboolean
_mesa_delete_instructions(struct gl_program *prog, GLuint start, GLuint count,
                          void *mem_ctx)
{
   const GLuint origLen = prog->arb.NumInstructions;
   const GLuint newLen = origLen - count;
   struct prog_instruction *newInst;
   GLuint i;

   /* adjust branches */
   for (i = 0; i < prog->arb.NumInstructions; i++) {
      struct prog_instruction *inst = prog->arb.Instructions + i;
      if (inst->BranchTarget > 0) {
         if (inst->BranchTarget > (GLint) start) {
            inst->BranchTarget -= count;
         }
      }
   }

   /* Alloc storage for new instructions */
   newInst = rzalloc_array(mem_ctx, struct prog_instruction, newLen);
   if (!newInst) {
      return GL_FALSE;
   }

   /* Copy 'start' instructions into new instruction buffer */
   _mesa_copy_instructions(newInst, prog->arb.Instructions, start);

   /* Copy the remaining/tail instructions to new inst buffer */
   _mesa_copy_instructions(newInst + start,
                           prog->arb.Instructions + start + count,
                           newLen - start);

   /* free old instructions */
   ralloc_free(prog->arb.Instructions);

   /* install new instructions */
   prog->arb.Instructions = newInst;
   prog->arb.NumInstructions = newLen;

   return GL_TRUE;
}


/**
 * Populate the 'used' array with flags indicating which registers (TEMPs,
 * INPUTs, OUTPUTs, etc, are used by the given program.
 * \param file  type of register to scan for
 * \param used  returns true/false flags for in use / free
 * \param usedSize  size of the 'used' array
 */
void
_mesa_find_used_registers(const struct gl_program *prog,
                          gl_register_file file,
                          GLboolean used[], GLuint usedSize)
{
   GLuint i, j;

   memset(used, 0, usedSize);

   for (i = 0; i < prog->arb.NumInstructions; i++) {
      const struct prog_instruction *inst = prog->arb.Instructions + i;
      const GLuint n = _mesa_num_inst_src_regs(inst->Opcode);

      if (inst->DstReg.File == file) {
         assert(inst->DstReg.Index < usedSize);
         if(inst->DstReg.Index < usedSize)
            used[inst->DstReg.Index] = GL_TRUE;
      }

      for (j = 0; j < n; j++) {
         if (inst->SrcReg[j].File == file) {
            assert(inst->SrcReg[j].Index < (GLint) usedSize);
            if (inst->SrcReg[j].Index < (GLint) usedSize)
               used[inst->SrcReg[j].Index] = GL_TRUE;
         }
      }
   }
}


/**
 * Scan the given 'used' register flag array for the first entry
 * that's >= firstReg.
 * \param used  vector of flags indicating registers in use (as returned
 *              by _mesa_find_used_registers())
 * \param usedSize  size of the 'used' array
 * \param firstReg  first register to start searching at
 * \return index of unused register, or -1 if none.
 */
GLint
_mesa_find_free_register(const GLboolean used[],
                         GLuint usedSize, GLuint firstReg)
{
   GLuint i;

   assert(firstReg < usedSize);

   for (i = firstReg; i < usedSize; i++)
      if (!used[i])
         return i;

   return -1;
}


/* Gets the minimum number of shader invocations per fragment.
 * This function is useful to determine if we need to do per
 * sample shading or per fragment shading.
 */
GLint
_mesa_get_min_invocations_per_fragment(struct gl_context *ctx,
                                       const struct gl_program *prog)
{
   /* From ARB_sample_shading specification:
    * "Using gl_SampleID in a fragment shader causes the entire shader
    *  to be evaluated per-sample."
    *
    * "Using gl_SamplePosition in a fragment shader causes the entire
    *  shader to be evaluated per-sample."
    *
    * "If MULTISAMPLE or SAMPLE_SHADING_ARB is disabled, sample shading
    *  has no effect."
    */
   if (ctx->Multisample.Enabled) {
      /* The ARB_gpu_shader5 specification says:
       *
       * "Use of the "sample" qualifier on a fragment shader input
       *  forces per-sample shading"
       */
      if (prog->info.fs.uses_sample_qualifier ||
          (prog->info.system_values_read & (SYSTEM_BIT_SAMPLE_ID |
                                            SYSTEM_BIT_SAMPLE_POS)))
         return MAX2(_mesa_geometric_samples(ctx->DrawBuffer), 1);
      else if (ctx->Multisample.SampleShading)
         return MAX2(ceil(ctx->Multisample.MinSampleShadingValue *
                          _mesa_geometric_samples(ctx->DrawBuffer)), 1);
      else
         return 1;
   }
   return 1;
}


GLbitfield
gl_external_samplers(const struct gl_program *prog)
{
   GLbitfield external_samplers = 0;
   GLbitfield mask = prog->SamplersUsed;

   while (mask) {
      int idx = u_bit_scan(&mask);
      if (prog->sh.SamplerTargets[idx] == TEXTURE_EXTERNAL_INDEX)
         external_samplers |= (1 << idx);
   }

   return external_samplers;
}
