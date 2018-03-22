/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2008  Brian Paul   All Rights Reserved.
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
 * \file state.c
 * State management.
 * 
 * This file manages recalculation of derived values in struct gl_context.
 */


#include "glheader.h"
#include "mtypes.h"
#include "arrayobj.h"
#include "context.h"
#include "debug.h"
#include "macros.h"
#include "ffvertex_prog.h"
#include "framebuffer.h"
#include "light.h"
#include "matrix.h"
#include "pixel.h"
#include "program/program.h"
#include "program/prog_parameter.h"
#include "shaderobj.h"
#include "state.h"
#include "stencil.h"
#include "texenvprogram.h"
#include "texobj.h"
#include "texstate.h"
#include "varray.h"
#include "vbo/vbo.h"
#include "viewport.h"
#include "blend.h"


/**
 * Update the ctx->*Program._Current pointers to point to the
 * current/active programs.
 *
 * Programs may come from 3 sources: GLSL shaders, ARB/NV_vertex/fragment
 * programs or programs derived from fixed-function state.
 *
 * This function needs to be called after texture state validation in case
 * we're generating a fragment program from fixed-function texture state.
 *
 * \return bitfield which will indicate _NEW_PROGRAM state if a new vertex
 * or fragment program is being used.
 */
static GLbitfield
update_program(struct gl_context *ctx)
{
   struct gl_program *vsProg =
      ctx->_Shader->CurrentProgram[MESA_SHADER_VERTEX];
   struct gl_program *tcsProg =
      ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_CTRL];
   struct gl_program *tesProg =
      ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_EVAL];
   struct gl_program *gsProg =
      ctx->_Shader->CurrentProgram[MESA_SHADER_GEOMETRY];
   struct gl_program *fsProg =
      ctx->_Shader->CurrentProgram[MESA_SHADER_FRAGMENT];
   struct gl_program *csProg =
      ctx->_Shader->CurrentProgram[MESA_SHADER_COMPUTE];
   const struct gl_program *prevVP = ctx->VertexProgram._Current;
   const struct gl_program *prevFP = ctx->FragmentProgram._Current;
   const struct gl_program *prevGP = ctx->GeometryProgram._Current;
   const struct gl_program *prevTCP = ctx->TessCtrlProgram._Current;
   const struct gl_program *prevTEP = ctx->TessEvalProgram._Current;
   const struct gl_program *prevCP = ctx->ComputeProgram._Current;

   /*
    * Set the ctx->VertexProgram._Current and ctx->FragmentProgram._Current
    * pointers to the programs that should be used for rendering.  If either
    * is NULL, use fixed-function code paths.
    *
    * These programs may come from several sources.  The priority is as
    * follows:
    *   1. OpenGL 2.0/ARB vertex/fragment shaders
    *   2. ARB/NV vertex/fragment programs
    *   3. ATI fragment shader
    *   4. Programs derived from fixed-function state.
    *
    * Note: it's possible for a vertex shader to get used with a fragment
    * program (and vice versa) here, but in practice that shouldn't ever
    * come up, or matter.
    */

   if (fsProg) {
      /* Use GLSL fragment shader */
      _mesa_reference_program(ctx, &ctx->FragmentProgram._Current, fsProg);
      _mesa_reference_program(ctx, &ctx->FragmentProgram._TexEnvProgram,
                              NULL);
   }
   else if (_mesa_arb_fragment_program_enabled(ctx)) {
      /* Use user-defined fragment program */
      _mesa_reference_program(ctx, &ctx->FragmentProgram._Current,
                              ctx->FragmentProgram.Current);
      _mesa_reference_program(ctx, &ctx->FragmentProgram._TexEnvProgram,
			      NULL);
   }
   else if (_mesa_ati_fragment_shader_enabled(ctx) &&
            ctx->ATIFragmentShader.Current->Program) {
       /* Use the enabled ATI fragment shader's associated program */
      _mesa_reference_program(ctx, &ctx->FragmentProgram._Current,
                              ctx->ATIFragmentShader.Current->Program);
      _mesa_reference_program(ctx, &ctx->FragmentProgram._TexEnvProgram,
                              NULL);
   }
   else if (ctx->FragmentProgram._MaintainTexEnvProgram) {
      /* Use fragment program generated from fixed-function state */
      struct gl_shader_program *f = _mesa_get_fixed_func_fragment_program(ctx);

      _mesa_reference_program(ctx, &ctx->FragmentProgram._Current,
			      f->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program);
      _mesa_reference_program(ctx, &ctx->FragmentProgram._TexEnvProgram,
			      f->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program);
   }
   else {
      /* No fragment program */
      _mesa_reference_program(ctx, &ctx->FragmentProgram._Current, NULL);
      _mesa_reference_program(ctx, &ctx->FragmentProgram._TexEnvProgram,
			      NULL);
   }

   if (gsProg) {
      /* Use GLSL geometry shader */
      _mesa_reference_program(ctx, &ctx->GeometryProgram._Current, gsProg);
   } else {
      /* No geometry program */
      _mesa_reference_program(ctx, &ctx->GeometryProgram._Current, NULL);
   }

   if (tesProg) {
      /* Use GLSL tessellation evaluation shader */
      _mesa_reference_program(ctx, &ctx->TessEvalProgram._Current, tesProg);
   }
   else {
      /* No tessellation evaluation program */
      _mesa_reference_program(ctx, &ctx->TessEvalProgram._Current, NULL);
   }

   if (tcsProg) {
      /* Use GLSL tessellation control shader */
      _mesa_reference_program(ctx, &ctx->TessCtrlProgram._Current, tcsProg);
   }
   else {
      /* No tessellation control program */
      _mesa_reference_program(ctx, &ctx->TessCtrlProgram._Current, NULL);
   }

   /* Examine vertex program after fragment program as
    * _mesa_get_fixed_func_vertex_program() needs to know active
    * fragprog inputs.
    */
   if (vsProg) {
      /* Use GLSL vertex shader */
      assert(VP_MODE_SHADER == ctx->VertexProgram._VPMode);
      _mesa_reference_program(ctx, &ctx->VertexProgram._Current, vsProg);
   }
   else if (_mesa_arb_vertex_program_enabled(ctx)) {
      /* Use user-defined vertex program */
      assert(VP_MODE_SHADER == ctx->VertexProgram._VPMode);
      _mesa_reference_program(ctx, &ctx->VertexProgram._Current,
                              ctx->VertexProgram.Current);
   }
   else if (ctx->VertexProgram._MaintainTnlProgram) {
      /* Use vertex program generated from fixed-function state */
      assert(VP_MODE_FF == ctx->VertexProgram._VPMode);
      _mesa_reference_program(ctx, &ctx->VertexProgram._Current,
                              _mesa_get_fixed_func_vertex_program(ctx));
      _mesa_reference_program(ctx, &ctx->VertexProgram._TnlProgram,
                              ctx->VertexProgram._Current);
   }
   else {
      /* no vertex program */
      assert(VP_MODE_FF == ctx->VertexProgram._VPMode);
      _mesa_reference_program(ctx, &ctx->VertexProgram._Current, NULL);
   }

   if (csProg) {
      /* Use GLSL compute shader */
      _mesa_reference_program(ctx, &ctx->ComputeProgram._Current, csProg);
   } else {
      /* no compute program */
      _mesa_reference_program(ctx, &ctx->ComputeProgram._Current, NULL);
   }

   /* Let the driver know what's happening:
    */
   if (ctx->FragmentProgram._Current != prevFP ||
       ctx->VertexProgram._Current != prevVP ||
       ctx->GeometryProgram._Current != prevGP ||
       ctx->TessEvalProgram._Current != prevTEP ||
       ctx->TessCtrlProgram._Current != prevTCP ||
       ctx->ComputeProgram._Current != prevCP)
      return _NEW_PROGRAM;

   return 0;
}


/**
 * Examine shader constants and return either _NEW_PROGRAM_CONSTANTS or 0.
 */
static GLbitfield
update_program_constants(struct gl_context *ctx)
{
   GLbitfield new_state = 0x0;

   if (ctx->FragmentProgram._Current) {
      const struct gl_program_parameter_list *params =
         ctx->FragmentProgram._Current->Parameters;
      if (params && params->StateFlags & ctx->NewState) {
         if (ctx->DriverFlags.NewShaderConstants[MESA_SHADER_FRAGMENT]) {
            ctx->NewDriverState |=
               ctx->DriverFlags.NewShaderConstants[MESA_SHADER_FRAGMENT];
         } else {
            new_state |= _NEW_PROGRAM_CONSTANTS;
         }
      }
   }

   /* Don't handle tessellation and geometry shaders here. They don't use
    * any state constants.
    */

   if (ctx->VertexProgram._Current) {
      const struct gl_program_parameter_list *params =
         ctx->VertexProgram._Current->Parameters;
      if (params && params->StateFlags & ctx->NewState) {
         if (ctx->DriverFlags.NewShaderConstants[MESA_SHADER_VERTEX]) {
            ctx->NewDriverState |=
               ctx->DriverFlags.NewShaderConstants[MESA_SHADER_VERTEX];
         } else {
            new_state |= _NEW_PROGRAM_CONSTANTS;
         }
      }
   }

   return new_state;
}


/**
 * Compute derived GL state.
 * If __struct gl_contextRec::NewState is non-zero then this function \b must
 * be called before rendering anything.
 *
 * Calls dd_function_table::UpdateState to perform any internal state
 * management necessary.
 * 
 * \sa _mesa_update_modelview_project(), _mesa_update_texture(),
 * _mesa_update_buffer_bounds(),
 * _mesa_update_lighting() and _mesa_update_tnl_spaces().
 */
void
_mesa_update_state_locked( struct gl_context *ctx )
{
   GLbitfield new_state = ctx->NewState;
   GLbitfield new_prog_state = 0x0;
   const GLbitfield computed_states = ~(_NEW_CURRENT_ATTRIB | _NEW_LINE);

   /* we can skip a bunch of state validation checks if the dirty
    * state matches one or more bits in 'computed_states'.
    */
   if ((new_state & computed_states) == 0)
      goto out;

   if (MESA_VERBOSE & VERBOSE_STATE)
      _mesa_print_state("_mesa_update_state", new_state);

   if (new_state & _NEW_BUFFERS)
      _mesa_update_framebuffer(ctx, ctx->ReadBuffer, ctx->DrawBuffer);

   /* Handle Core and Compatibility contexts separately. */
   if (ctx->API == API_OPENGL_COMPAT ||
       ctx->API == API_OPENGLES) {
      GLbitfield prog_flags = _NEW_PROGRAM;

      /* Determine which state flags effect vertex/fragment program state */
      if (ctx->FragmentProgram._MaintainTexEnvProgram) {
         prog_flags |= (_NEW_BUFFERS | _NEW_TEXTURE_OBJECT | _NEW_FOG |
                        _NEW_VARYING_VP_INPUTS | _NEW_LIGHT | _NEW_POINT |
                        _NEW_RENDERMODE | _NEW_PROGRAM | _NEW_FRAG_CLAMP |
                        _NEW_COLOR | _NEW_TEXTURE_STATE);
      }
      if (ctx->VertexProgram._MaintainTnlProgram) {
         prog_flags |= (_NEW_VARYING_VP_INPUTS | _NEW_TEXTURE_OBJECT |
                        _NEW_TEXTURE_MATRIX | _NEW_TRANSFORM | _NEW_POINT |
                        _NEW_FOG | _NEW_LIGHT | _NEW_TEXTURE_STATE |
                        _MESA_NEW_NEED_EYE_COORDS);
      }

      /*
       * Now update derived state info
       */
      if (new_state & (_NEW_MODELVIEW|_NEW_PROJECTION))
         _mesa_update_modelview_project( ctx, new_state );

      if (new_state & _NEW_TEXTURE_MATRIX)
         _mesa_update_texture_matrices(ctx);

      if (new_state & (_NEW_TEXTURE_OBJECT | _NEW_TEXTURE_STATE | _NEW_PROGRAM))
         _mesa_update_texture_state(ctx);

      if (new_state & _NEW_LIGHT)
         _mesa_update_lighting(ctx);

      if (new_state & _NEW_PIXEL)
         _mesa_update_pixel( ctx );

      /* ctx->_NeedEyeCoords is now up to date.
       *
       * If the truth value of this variable has changed, update for the
       * new lighting space and recompute the positions of lights and the
       * normal transform.
       *
       * If the lighting space hasn't changed, may still need to recompute
       * light positions & normal transforms for other reasons.
       */
      if (new_state & _MESA_NEW_NEED_EYE_COORDS)
         _mesa_update_tnl_spaces( ctx, new_state );

      if (new_state & prog_flags) {
         /* When we generate programs from fixed-function vertex/fragment state
          * this call may generate/bind a new program.  If so, we need to
          * propogate the _NEW_PROGRAM flag to the driver.
          */
         new_prog_state |= update_program(ctx);
      }
   } else {
      /* GL Core and GLES 2/3 contexts */
      if (new_state & (_NEW_TEXTURE_OBJECT | _NEW_PROGRAM))
         _mesa_update_texture_state(ctx);

      if (new_state & _NEW_PROGRAM)
         update_program(ctx);
   }

 out:
   new_prog_state |= update_program_constants(ctx);

   ctx->NewState |= new_prog_state;
   vbo_exec_invalidate_state(ctx);

   /*
    * Give the driver a chance to act upon the new_state flags.
    * The driver might plug in different span functions, for example.
    * Also, this is where the driver can invalidate the state of any
    * active modules (such as swrast_setup, swrast, tnl, etc).
    */
   ctx->Driver.UpdateState(ctx);
   ctx->NewState = 0;
}


/* This is the usual entrypoint for state updates:
 */
void
_mesa_update_state( struct gl_context *ctx )
{
   _mesa_lock_context_textures(ctx);
   _mesa_update_state_locked(ctx);
   _mesa_unlock_context_textures(ctx);
}




/**
 * Want to figure out which fragment program inputs are actually
 * constant/current values from ctx->Current.  These should be
 * referenced as a tracked state variable rather than a fragment
 * program input, to save the overhead of putting a constant value in
 * every submitted vertex, transferring it to hardware, interpolating
 * it across the triangle, etc...
 *
 * When there is a VP bound, just use vp->outputs.  But when we're
 * generating vp from fixed function state, basically want to
 * calculate:
 *
 * vp_out_2_fp_in( vp_in_2_vp_out( varying_inputs ) | 
 *                 potential_vp_outputs )
 *
 * Where potential_vp_outputs is calculated by looking at enabled
 * texgen, etc.
 * 
 * The generated fragment program should then only declare inputs that
 * may vary or otherwise differ from the ctx->Current values.
 * Otherwise, the fp should track them as state values instead.
 */
void
_mesa_set_varying_vp_inputs( struct gl_context *ctx,
                             GLbitfield varying_inputs )
{
   if (ctx->API != API_OPENGL_COMPAT &&
       ctx->API != API_OPENGLES)
      return;

   if (ctx->varying_vp_inputs != varying_inputs) {
      ctx->varying_vp_inputs = varying_inputs;

      /* Only the fixed-func generated programs need to use the flag
       * and the fixed-func fragment program uses it only if there is also
       * a fixed-func vertex program, so this only depends on the latter.
       *
       * It's okay to check the VP pointer here, because this is called after
       * _mesa_update_state in the vbo module. */
      if (ctx->VertexProgram._TnlProgram ||
          ctx->FragmentProgram._TexEnvProgram) {
         ctx->NewState |= _NEW_VARYING_VP_INPUTS;
      }
      /*printf("%s %x\n", __func__, varying_inputs);*/
   }
}


/**
 * Used by drivers to tell core Mesa that the driver is going to
 * install/ use its own vertex program.  In particular, this will
 * prevent generated fragment programs from using state vars instead
 * of ordinary varyings/inputs.
 */
void
_mesa_set_vp_override(struct gl_context *ctx, GLboolean flag)
{
   if (ctx->VertexProgram._Overriden != flag) {
      ctx->VertexProgram._Overriden = flag;

      /* Set one of the bits which will trigger fragment program
       * regeneration:
       */
      ctx->NewState |= _NEW_PROGRAM;
   }
}


static void
set_new_array(struct gl_context *ctx)
{
   _vbo_set_recalculate_inputs(ctx);
   ctx->NewDriverState |= ctx->DriverFlags.NewArray;
}


static void
set_vertex_processing_mode(struct gl_context *ctx, gl_vertex_processing_mode m)
{
   if (ctx->VertexProgram._VPMode == m)
      return;

   /* On change we may get new maps into the current values */
   set_new_array(ctx);

   /* Finally memorize the value */
   ctx->VertexProgram._VPMode = m;
}


/**
 * Update ctx->VertexProgram._VPMode.
 * This is to distinguish whether we're running
 *   a vertex program/shader,
 *   a fixed-function TNL program or
 *   a fixed function vertex transformation without any program.
 */
void
_mesa_update_vertex_processing_mode(struct gl_context *ctx)
{
   if (ctx->_Shader->CurrentProgram[MESA_SHADER_VERTEX])
      set_vertex_processing_mode(ctx, VP_MODE_SHADER);
   else if (_mesa_arb_vertex_program_enabled(ctx))
      set_vertex_processing_mode(ctx, VP_MODE_SHADER);
   else
      set_vertex_processing_mode(ctx, VP_MODE_FF);
}


/**
 * Set the _DrawVAO and the net enabled arrays.
 * The vao->_Enabled bitmask is transformed due to position/generic0
 * as stored in vao->_AttributeMapMode. Then the filter bitmask is applied
 * to filter out arrays unwanted for the currently executed draw operation.
 * For example, the generic attributes are masked out form the _DrawVAO's
 * enabled arrays when a fixed function array draw is executed.
 */
void
_mesa_set_draw_vao(struct gl_context *ctx, struct gl_vertex_array_object *vao,
                   GLbitfield filter)
{
   struct gl_vertex_array_object **ptr = &ctx->Array._DrawVAO;
   bool new_array = false;
   if (*ptr != vao) {
      _mesa_reference_vao_(ctx, ptr, vao);

      new_array = true;
   }

   if (vao->NewArrays) {
      _mesa_update_vao_derived_arrays(ctx, vao);
      vao->NewArrays = 0;

      new_array = true;
   }

   /* May shuffle the position and generic0 bits around, filter out unwanted */
   const GLbitfield enabled = filter & _mesa_get_vao_vp_inputs(vao);
   if (ctx->Array._DrawVAOEnabledAttribs != enabled)
      new_array = true;

   if (new_array)
      set_new_array(ctx);

   ctx->Array._DrawVAOEnabledAttribs = enabled;
   _mesa_set_varying_vp_inputs(ctx, enabled);
}
