/**************************************************************************
 *
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

/*
 * Authors:
 *   Keith Whitwell <keithw@vmware.com>
 *   Brian Paul
 */


#include "program/prog_parameter.h"
#include "program/prog_print.h"
#include "main/shaderapi.h"
#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "util/u_inlines.h"
#include "util/u_upload_mgr.h"
#include "cso_cache/cso_context.h"

#include "st_debug.h"
#include "st_context.h"
#include "st_atom.h"
#include "st_atom_constbuf.h"
#include "st_program.h"
#include "st_cb_bufferobjects.h"

/**
 * Pass the given program parameters to the graphics pipe as a
 * constant buffer.
 */
void
st_upload_constants(struct st_context *st, struct gl_program *prog)
{
   gl_shader_stage stage = prog->info.stage;
   struct gl_program_parameter_list *params = prog->Parameters;
   enum pipe_shader_type shader_type = pipe_shader_type_from_mesa(stage);

   assert(shader_type == PIPE_SHADER_VERTEX ||
          shader_type == PIPE_SHADER_FRAGMENT ||
          shader_type == PIPE_SHADER_GEOMETRY ||
          shader_type == PIPE_SHADER_TESS_CTRL ||
          shader_type == PIPE_SHADER_TESS_EVAL ||
          shader_type == PIPE_SHADER_COMPUTE);

   /* update the ATI constants before rendering */
   if (shader_type == PIPE_SHADER_FRAGMENT && st->fp->ati_fs) {
      struct ati_fragment_shader *ati_fs = st->fp->ati_fs;
      unsigned c;

      for (c = 0; c < MAX_NUM_FRAGMENT_CONSTANTS_ATI; c++) {
         unsigned offset = params->ParameterValueOffset[c];
         if (ati_fs->LocalConstDef & (1 << c))
            memcpy(params->ParameterValues + offset,
                   ati_fs->Constants[c], sizeof(GLfloat) * 4);
         else
            memcpy(params->ParameterValues + offset,
                   st->ctx->ATIFragmentShader.GlobalConstants[c],
                   sizeof(GLfloat) * 4);
      }
   }

   /* Make all bindless samplers/images bound texture/image units resident in
    * the context.
    */
   st_make_bound_samplers_resident(st, prog);
   st_make_bound_images_resident(st, prog);

   /* update constants */
   if (params && params->NumParameters) {
      struct pipe_constant_buffer cb;
      const uint paramBytes = params->NumParameterValues * sizeof(GLfloat);

      /* Update the constants which come from fixed-function state, such as
       * transformation matrices, fog factors, etc.  The rest of the values in
       * the parameters list are explicitly set by the user with glUniform,
       * glProgramParameter(), etc.
       */
      if (params->StateFlags)
         _mesa_load_state_parameters(st->ctx, params);

      _mesa_shader_write_subroutine_indices(st->ctx, stage);

      cb.buffer = NULL;
      cb.user_buffer = params->ParameterValues;
      cb.buffer_offset = 0;
      cb.buffer_size = paramBytes;

      if (ST_DEBUG & DEBUG_CONSTANTS) {
         debug_printf("%s(shader=%d, numParams=%d, stateFlags=0x%x)\n",
                      __func__, shader_type, params->NumParameters,
                      params->StateFlags);
         _mesa_print_parameter_list(params);
      }

      cso_set_constant_buffer(st->cso_context, shader_type, 0, &cb);
      pipe_resource_reference(&cb.buffer, NULL);

      /* Set inlinable constants. */
      unsigned num_inlinable_uniforms = prog->info.num_inlinable_uniforms;
      if (num_inlinable_uniforms) {
         struct pipe_context *pipe = st->pipe;
         uint32_t values[MAX_INLINABLE_UNIFORMS];
         gl_constant_value *constbuf = params->ParameterValues;

         for (unsigned i = 0; i < num_inlinable_uniforms; i++)
            values[i] = constbuf[prog->info.inlinable_uniform_dw_offsets[i]].u;

         pipe->set_inlinable_constants(pipe, shader_type,
                                       prog->info.num_inlinable_uniforms,
                                       values);
      }

      st->state.constants[shader_type].ptr = params->ParameterValues;
      st->state.constants[shader_type].size = paramBytes;
   }
   else if (st->state.constants[shader_type].ptr) {
      /* Unbind. */
      st->state.constants[shader_type].ptr = NULL;
      st->state.constants[shader_type].size = 0;
      cso_set_constant_buffer(st->cso_context, shader_type, 0, NULL);
   }
}


/**
 * Vertex shader:
 */
void
st_update_vs_constants(struct st_context *st)
{
   st_upload_constants(st, &st->vp->Base);
}

/**
 * Fragment shader:
 */
void
st_update_fs_constants(struct st_context *st)
{
   st_upload_constants(st, &st->fp->Base);
}


/* Geometry shader:
 */
void
st_update_gs_constants(struct st_context *st)
{
   struct st_program *gp = st->gp;

   if (gp)
      st_upload_constants(st, &gp->Base);
}

/* Tessellation control shader:
 */
void
st_update_tcs_constants(struct st_context *st)
{
   struct st_program *tcp = st->tcp;

   if (tcp)
      st_upload_constants(st, &tcp->Base);
}

/* Tessellation evaluation shader:
 */
void
st_update_tes_constants(struct st_context *st)
{
   struct st_program *tep = st->tep;

   if (tep)
      st_upload_constants(st, &tep->Base);
}

/* Compute shader:
 */
void
st_update_cs_constants(struct st_context *st)
{
   struct st_program *cp = st->cp;

   if (cp)
      st_upload_constants(st, &cp->Base);
}

static void
st_bind_ubos(struct st_context *st, struct gl_program *prog,
             enum pipe_shader_type shader_type)
{
   unsigned i;
   struct pipe_constant_buffer cb = { 0 };

   if (!prog)
      return;

   for (i = 0; i < prog->sh.NumUniformBlocks; i++) {
      struct gl_buffer_binding *binding;
      struct st_buffer_object *st_obj;

      binding =
         &st->ctx->UniformBufferBindings[prog->sh.UniformBlocks[i]->Binding];
      st_obj = st_buffer_object(binding->BufferObject);

      cb.buffer = st_obj ? st_obj->buffer : NULL;

      if (cb.buffer) {
         cb.buffer_offset = binding->Offset;
         cb.buffer_size = cb.buffer->width0 - binding->Offset;

         /* AutomaticSize is FALSE if the buffer was set with BindBufferRange.
          * Take the minimum just to be sure.
          */
         if (!binding->AutomaticSize)
            cb.buffer_size = MIN2(cb.buffer_size, (unsigned) binding->Size);
      }
      else {
         cb.buffer_offset = 0;
         cb.buffer_size = 0;
      }

      cso_set_constant_buffer(st->cso_context, shader_type, 1 + i, &cb);
   }
}

void
st_bind_vs_ubos(struct st_context *st)
{
   struct gl_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_VERTEX];

   st_bind_ubos(st, prog, PIPE_SHADER_VERTEX);
}

void
st_bind_fs_ubos(struct st_context *st)
{
   struct gl_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_FRAGMENT];

   st_bind_ubos(st, prog, PIPE_SHADER_FRAGMENT);
}

void
st_bind_gs_ubos(struct st_context *st)
{
   struct gl_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_GEOMETRY];

   st_bind_ubos(st, prog, PIPE_SHADER_GEOMETRY);
}

void
st_bind_tcs_ubos(struct st_context *st)
{
   struct gl_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_CTRL];

   st_bind_ubos(st, prog, PIPE_SHADER_TESS_CTRL);
}

void
st_bind_tes_ubos(struct st_context *st)
{
   struct gl_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_EVAL];

   st_bind_ubos(st, prog, PIPE_SHADER_TESS_EVAL);
}

void
st_bind_cs_ubos(struct st_context *st)
{
   struct gl_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_COMPUTE];

   st_bind_ubos(st, prog, PIPE_SHADER_COMPUTE);
}
