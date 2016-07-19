/**************************************************************************
 *
 * Copyright 2014 Ilia Mirkin. All Rights Reserved.
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

#include "main/imports.h"
#include "program/prog_parameter.h"
#include "program/prog_print.h"
#include "compiler/glsl/ir_uniform.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "util/u_inlines.h"
#include "util/u_surface.h"

#include "st_debug.h"
#include "st_cb_bufferobjects.h"
#include "st_context.h"
#include "st_atom.h"
#include "st_program.h"

static void
st_bind_ssbos(struct st_context *st, struct gl_linked_shader *shader,
              unsigned shader_type)
{
   unsigned i;
   struct pipe_shader_buffer buffers[MAX_SHADER_STORAGE_BUFFERS];
   struct gl_program_constants *c;

   if (!shader || !st->pipe->set_shader_buffers)
      return;

   c = &st->ctx->Const.Program[shader->Stage];

   for (i = 0; i < shader->NumShaderStorageBlocks; i++) {
      struct gl_shader_storage_buffer_binding *binding;
      struct st_buffer_object *st_obj;
      struct pipe_shader_buffer *sb = &buffers[i];

      binding = &st->ctx->ShaderStorageBufferBindings[
            shader->ShaderStorageBlocks[i]->Binding];
      st_obj = st_buffer_object(binding->BufferObject);

      sb->buffer = st_obj->buffer;

      if (sb->buffer) {
         sb->buffer_offset = binding->Offset;
         sb->buffer_size = sb->buffer->width0 - binding->Offset;

         /* AutomaticSize is FALSE if the buffer was set with BindBufferRange.
          * Take the minimum just to be sure.
          */
         if (!binding->AutomaticSize)
            sb->buffer_size = MIN2(sb->buffer_size, (unsigned) binding->Size);
      }
      else {
         sb->buffer_offset = 0;
         sb->buffer_size = 0;
      }
   }
   st->pipe->set_shader_buffers(st->pipe, shader_type, c->MaxAtomicBuffers,
                                shader->NumShaderStorageBlocks, buffers);
   /* clear out any stale shader buffers */
   if (shader->NumShaderStorageBlocks < c->MaxShaderStorageBlocks)
      st->pipe->set_shader_buffers(
            st->pipe, shader_type,
            c->MaxAtomicBuffers + shader->NumShaderStorageBlocks,
            c->MaxShaderStorageBlocks - shader->NumShaderStorageBlocks,
            NULL);
}

static void bind_vs_ssbos(struct st_context *st)
{
   struct gl_shader_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_VERTEX];

   if (!prog)
      return;

   st_bind_ssbos(st, prog->_LinkedShaders[MESA_SHADER_VERTEX],
                 PIPE_SHADER_VERTEX);
}

const struct st_tracked_state st_bind_vs_ssbos = {
   "st_bind_vs_ssbos",
   {
      0,
      ST_NEW_VERTEX_PROGRAM | ST_NEW_STORAGE_BUFFER,
   },
   bind_vs_ssbos
};

static void bind_fs_ssbos(struct st_context *st)
{
   struct gl_shader_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_FRAGMENT];

   if (!prog)
      return;

   st_bind_ssbos(st, prog->_LinkedShaders[MESA_SHADER_FRAGMENT],
                 PIPE_SHADER_FRAGMENT);
}

const struct st_tracked_state st_bind_fs_ssbos = {
   "st_bind_fs_ssbos",
   {
      0,
      ST_NEW_FRAGMENT_PROGRAM | ST_NEW_STORAGE_BUFFER,
   },
   bind_fs_ssbos
};

static void bind_gs_ssbos(struct st_context *st)
{
   struct gl_shader_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_GEOMETRY];

   if (!prog)
      return;

   st_bind_ssbos(st, prog->_LinkedShaders[MESA_SHADER_GEOMETRY],
                 PIPE_SHADER_GEOMETRY);
}

const struct st_tracked_state st_bind_gs_ssbos = {
   "st_bind_gs_ssbos",
   {
      0,
      ST_NEW_GEOMETRY_PROGRAM | ST_NEW_STORAGE_BUFFER,
   },
   bind_gs_ssbos
};

static void bind_tcs_ssbos(struct st_context *st)
{
   struct gl_shader_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_CTRL];

   if (!prog)
      return;

   st_bind_ssbos(st, prog->_LinkedShaders[MESA_SHADER_TESS_CTRL],
                 PIPE_SHADER_TESS_CTRL);
}

const struct st_tracked_state st_bind_tcs_ssbos = {
   "st_bind_tcs_ssbos",
   {
      0,
      ST_NEW_TESSCTRL_PROGRAM | ST_NEW_STORAGE_BUFFER,
   },
   bind_tcs_ssbos
};

static void bind_tes_ssbos(struct st_context *st)
{
   struct gl_shader_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_EVAL];

   if (!prog)
      return;

   st_bind_ssbos(st, prog->_LinkedShaders[MESA_SHADER_TESS_EVAL],
                 PIPE_SHADER_TESS_EVAL);
}

const struct st_tracked_state st_bind_tes_ssbos = {
   "st_bind_tes_ssbos",
   {
      0,
      ST_NEW_TESSEVAL_PROGRAM | ST_NEW_STORAGE_BUFFER,
   },
   bind_tes_ssbos
};

static void bind_cs_ssbos(struct st_context *st)
{
   struct gl_shader_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_COMPUTE];

   if (!prog)
      return;

   st_bind_ssbos(st, prog->_LinkedShaders[MESA_SHADER_COMPUTE],
                 PIPE_SHADER_COMPUTE);
}

const struct st_tracked_state st_bind_cs_ssbos = {
   "st_bind_cs_ssbos",
   {
      0,
      ST_NEW_COMPUTE_PROGRAM | ST_NEW_STORAGE_BUFFER,
   },
   bind_cs_ssbos
};
