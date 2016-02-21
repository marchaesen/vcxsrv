/**************************************************************************
 * 
 * Copyright 2003 VMware, Inc.
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
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  */

#include "main/glheader.h"
#include "main/macros.h"
#include "main/enums.h"
#include "main/shaderapi.h"
#include "program/prog_instruction.h"
#include "program/program.h"

#include "cso_cache/cso_context.h"
#include "draw/draw_context.h"

#include "st_context.h"
#include "st_debug.h"
#include "st_program.h"
#include "st_mesa_to_tgsi.h"
#include "st_cb_program.h"
#include "st_glsl_to_tgsi.h"



/**
 * Called via ctx->Driver.BindProgram() to bind an ARB vertex or
 * fragment program.
 */
static void
st_bind_program(struct gl_context *ctx, GLenum target, struct gl_program *prog)
{
   struct st_context *st = st_context(ctx);

   switch (target) {
   case GL_VERTEX_PROGRAM_ARB: 
      st->dirty.st |= ST_NEW_VERTEX_PROGRAM;
      break;
   case GL_FRAGMENT_PROGRAM_ARB:
      st->dirty.st |= ST_NEW_FRAGMENT_PROGRAM;
      break;
   case GL_GEOMETRY_PROGRAM_NV:
      st->dirty.st |= ST_NEW_GEOMETRY_PROGRAM;
      break;
   case GL_TESS_CONTROL_PROGRAM_NV:
      st->dirty.st |= ST_NEW_TESSCTRL_PROGRAM;
      break;
   case GL_TESS_EVALUATION_PROGRAM_NV:
      st->dirty.st |= ST_NEW_TESSEVAL_PROGRAM;
      break;
   case GL_COMPUTE_PROGRAM_NV:
      st->dirty_cp.st |= ST_NEW_COMPUTE_PROGRAM;
      break;
   }
}


/**
 * Called via ctx->Driver.UseProgram() to bind a linked GLSL program
 * (vertex shader + fragment shader).
 */
static void
st_use_program(struct gl_context *ctx, struct gl_shader_program *shProg)
{
   struct st_context *st = st_context(ctx);

   st->dirty.st |= ST_NEW_FRAGMENT_PROGRAM;
   st->dirty.st |= ST_NEW_VERTEX_PROGRAM;
   st->dirty.st |= ST_NEW_GEOMETRY_PROGRAM;
   st->dirty.st |= ST_NEW_TESSCTRL_PROGRAM;
   st->dirty.st |= ST_NEW_TESSEVAL_PROGRAM;
   st->dirty_cp.st |= ST_NEW_COMPUTE_PROGRAM;
}


/**
 * Called via ctx->Driver.NewProgram() to allocate a new vertex or
 * fragment program.
 */
static struct gl_program *
st_new_program(struct gl_context *ctx, GLenum target, GLuint id)
{
   switch (target) {
   case GL_VERTEX_PROGRAM_ARB: {
      struct st_vertex_program *prog = ST_CALLOC_STRUCT(st_vertex_program);
      return _mesa_init_gl_program(&prog->Base.Base, target, id);
   }
   case GL_FRAGMENT_PROGRAM_ARB: {
      struct st_fragment_program *prog = ST_CALLOC_STRUCT(st_fragment_program);
      return _mesa_init_gl_program(&prog->Base.Base, target, id);
   }
   case GL_GEOMETRY_PROGRAM_NV: {
      struct st_geometry_program *prog = ST_CALLOC_STRUCT(st_geometry_program);
      return _mesa_init_gl_program(&prog->Base.Base, target, id);
   }
   case GL_TESS_CONTROL_PROGRAM_NV: {
      struct st_tessctrl_program *prog = ST_CALLOC_STRUCT(st_tessctrl_program);
      return _mesa_init_gl_program(&prog->Base.Base, target, id);
   }
   case GL_TESS_EVALUATION_PROGRAM_NV: {
      struct st_tesseval_program *prog = ST_CALLOC_STRUCT(st_tesseval_program);
      return _mesa_init_gl_program(&prog->Base.Base, target, id);
   }
   case GL_COMPUTE_PROGRAM_NV: {
      struct st_compute_program *prog = ST_CALLOC_STRUCT(st_compute_program);
      return _mesa_init_gl_program(&prog->Base.Base, target, id);
   }
   default:
      assert(0);
      return NULL;
   }
}


/**
 * Called via ctx->Driver.DeleteProgram()
 */
static void
st_delete_program(struct gl_context *ctx, struct gl_program *prog)
{
   struct st_context *st = st_context(ctx);

   switch( prog->Target ) {
   case GL_VERTEX_PROGRAM_ARB:
      {
         struct st_vertex_program *stvp = (struct st_vertex_program *) prog;
         st_release_vp_variants( st, stvp );
         
         if (stvp->glsl_to_tgsi)
            free_glsl_to_tgsi_visitor(stvp->glsl_to_tgsi);
      }
      break;
   case GL_GEOMETRY_PROGRAM_NV:
      {
         struct st_geometry_program *stgp =
            (struct st_geometry_program *) prog;

         st_release_basic_variants(st, stgp->Base.Base.Target,
                                   &stgp->variants, &stgp->tgsi);
         
         if (stgp->glsl_to_tgsi)
            free_glsl_to_tgsi_visitor(stgp->glsl_to_tgsi);
      }
      break;
   case GL_FRAGMENT_PROGRAM_ARB:
      {
         struct st_fragment_program *stfp =
            (struct st_fragment_program *) prog;

         st_release_fp_variants(st, stfp);
         
         if (stfp->glsl_to_tgsi)
            free_glsl_to_tgsi_visitor(stfp->glsl_to_tgsi);
      }
      break;
   case GL_TESS_CONTROL_PROGRAM_NV:
      {
         struct st_tessctrl_program *sttcp =
            (struct st_tessctrl_program *) prog;

         st_release_basic_variants(st, sttcp->Base.Base.Target,
                                   &sttcp->variants, &sttcp->tgsi);

         if (sttcp->glsl_to_tgsi)
            free_glsl_to_tgsi_visitor(sttcp->glsl_to_tgsi);
      }
      break;
   case GL_TESS_EVALUATION_PROGRAM_NV:
      {
         struct st_tesseval_program *sttep =
            (struct st_tesseval_program *) prog;

         st_release_basic_variants(st, sttep->Base.Base.Target,
                                   &sttep->variants, &sttep->tgsi);

         if (sttep->glsl_to_tgsi)
            free_glsl_to_tgsi_visitor(sttep->glsl_to_tgsi);
      }
      break;
   case GL_COMPUTE_PROGRAM_NV:
      {
         struct st_compute_program *stcp =
            (struct st_compute_program *) prog;

         st_release_cp_variants(st, stcp);

         if (stcp->glsl_to_tgsi)
            free_glsl_to_tgsi_visitor(stcp->glsl_to_tgsi);
      }
      break;
   default:
      assert(0); /* problem */
   }

   /* delete base class */
   _mesa_delete_program( ctx, prog );
}


/**
 * Called via ctx->Driver.ProgramStringNotify()
 * Called when the program's text/code is changed.  We have to free
 * all shader variants and corresponding gallium shaders when this happens.
 */
static GLboolean
st_program_string_notify( struct gl_context *ctx,
                                           GLenum target,
                                           struct gl_program *prog )
{
   struct st_context *st = st_context(ctx);
   gl_shader_stage stage = _mesa_program_enum_to_shader_stage(target);

   if (target == GL_FRAGMENT_PROGRAM_ARB) {
      struct st_fragment_program *stfp = (struct st_fragment_program *) prog;

      st_release_fp_variants(st, stfp);
      if (!st_translate_fragment_program(st, stfp))
         return false;

      if (st->fp == stfp)
	 st->dirty.st |= ST_NEW_FRAGMENT_PROGRAM;
   }
   else if (target == GL_GEOMETRY_PROGRAM_NV) {
      struct st_geometry_program *stgp = (struct st_geometry_program *) prog;

      st_release_basic_variants(st, stgp->Base.Base.Target,
                                &stgp->variants, &stgp->tgsi);
      if (!st_translate_geometry_program(st, stgp))
         return false;

      if (st->gp == stgp)
	 st->dirty.st |= ST_NEW_GEOMETRY_PROGRAM;
   }
   else if (target == GL_VERTEX_PROGRAM_ARB) {
      struct st_vertex_program *stvp = (struct st_vertex_program *) prog;

      st_release_vp_variants(st, stvp);
      if (!st_translate_vertex_program(st, stvp))
         return false;

      if (st->vp == stvp)
	 st->dirty.st |= ST_NEW_VERTEX_PROGRAM;
   }
   else if (target == GL_TESS_CONTROL_PROGRAM_NV) {
      struct st_tessctrl_program *sttcp =
         (struct st_tessctrl_program *) prog;

      st_release_basic_variants(st, sttcp->Base.Base.Target,
                                &sttcp->variants, &sttcp->tgsi);
      if (!st_translate_tessctrl_program(st, sttcp))
         return false;

      if (st->tcp == sttcp)
         st->dirty.st |= ST_NEW_TESSCTRL_PROGRAM;
   }
   else if (target == GL_TESS_EVALUATION_PROGRAM_NV) {
      struct st_tesseval_program *sttep =
         (struct st_tesseval_program *) prog;

      st_release_basic_variants(st, sttep->Base.Base.Target,
                                &sttep->variants, &sttep->tgsi);
      if (!st_translate_tesseval_program(st, sttep))
         return false;

      if (st->tep == sttep)
         st->dirty.st |= ST_NEW_TESSEVAL_PROGRAM;
   }
   else if (target == GL_COMPUTE_PROGRAM_NV) {
      struct st_compute_program *stcp =
         (struct st_compute_program *) prog;

      st_release_cp_variants(st, stcp);
      if (!st_translate_compute_program(st, stcp))
         return false;

      if (st->cp == stcp)
         st->dirty_cp.st |= ST_NEW_COMPUTE_PROGRAM;
   }

   if (ST_DEBUG & DEBUG_PRECOMPILE ||
       st->shader_has_one_variant[stage])
      st_precompile_shader_variant(st, prog);

   return GL_TRUE;
}


/**
 * Plug in the program and shader-related device driver functions.
 */
void
st_init_program_functions(struct dd_function_table *functions)
{
   functions->BindProgram = st_bind_program;
   functions->UseProgram = st_use_program;
   functions->NewProgram = st_new_program;
   functions->DeleteProgram = st_delete_program;
   functions->ProgramStringNotify = st_program_string_notify;
   
   functions->LinkShader = st_link_shader;
}
