/*
 * Copyright © 2012 Intel Corporation
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

#include "glthread_marshal.h"
#include "dispatch.h"
#include "uniforms.h"

struct marshal_cmd_ShaderSource
{
   struct marshal_cmd_base cmd_base;
   GLuint shader;
   GLsizei count;
   /* Followed by GLint length[count], then the contents of all strings,
    * concatenated.
    */
};


uint32_t
_mesa_unmarshal_ShaderSource(struct gl_context *ctx,
                             const struct marshal_cmd_ShaderSource *cmd,
                             const uint64_t *last)
{
   const GLint *cmd_length = (const GLint *) (cmd + 1);
   const GLchar *cmd_strings = (const GLchar *) (cmd_length + cmd->count);
   /* TODO: how to deal with malloc failure? */
   const GLchar * *string = malloc(cmd->count * sizeof(const GLchar *));
   int i;

   for (i = 0; i < cmd->count; ++i) {
      string[i] = cmd_strings;
      cmd_strings += cmd_length[i];
   }
   CALL_ShaderSource(ctx->CurrentServerDispatch,
                     (cmd->shader, cmd->count, string, cmd_length));
   free((void *)string);
   return cmd->cmd_base.cmd_size;
}


static size_t
measure_ShaderSource_strings(GLsizei count, const GLchar * const *string,
                             const GLint *length_in, GLint *length_out)
{
   int i;
   size_t total_string_length = 0;

   for (i = 0; i < count; ++i) {
      if (length_in == NULL || length_in[i] < 0) {
         if (string[i])
            length_out[i] = strlen(string[i]);
      } else {
         length_out[i] = length_in[i];
      }
      total_string_length += length_out[i];
   }
   return total_string_length;
}


void GLAPIENTRY
_mesa_marshal_ShaderSource(GLuint shader, GLsizei count,
                           const GLchar * const *string, const GLint *length)
{
   /* TODO: how to report an error if count < 0? */

   GET_CURRENT_CONTEXT(ctx);
   /* TODO: how to deal with malloc failure? */
   const size_t fixed_cmd_size = sizeof(struct marshal_cmd_ShaderSource);
   STATIC_ASSERT(sizeof(struct marshal_cmd_ShaderSource) % sizeof(GLint) == 0);
   size_t length_size = count * sizeof(GLint);
   GLint *length_tmp = malloc(length_size);
   size_t total_string_length =
      measure_ShaderSource_strings(count, string, length, length_tmp);
   size_t total_cmd_size = fixed_cmd_size + length_size + total_string_length;

   if (total_cmd_size <= MARSHAL_MAX_CMD_SIZE && count > 0) {
      struct marshal_cmd_ShaderSource *cmd =
         _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_ShaderSource,
                                         total_cmd_size);
      GLint *cmd_length = (GLint *) (cmd + 1);
      GLchar *cmd_strings = (GLchar *) (cmd_length + count);
      int i;

      cmd->shader = shader;
      cmd->count = count;
      memcpy(cmd_length, length_tmp, length_size);
      for (i = 0; i < count; ++i) {
         memcpy(cmd_strings, string[i], cmd_length[i]);
         cmd_strings += cmd_length[i];
      }
   } else {
      _mesa_glthread_finish(ctx);
      CALL_ShaderSource(ctx->CurrentServerDispatch,
                        (shader, count, string, length_tmp));
   }
   free(length_tmp);
}

void
_mesa_glthread_ProgramChanged(struct gl_context *ctx)
{
   struct glthread_state *glthread = &ctx->GLThread;

   /* Track the last change. */
   p_atomic_set(&glthread->LastProgramChangeBatch, glthread->next);
   _mesa_glthread_flush_batch(ctx);
}

uint32_t
_mesa_unmarshal_GetActiveUniform(struct gl_context *ctx,
                                 const struct marshal_cmd_GetActiveUniform *cmd,
                                 const uint64_t *last)
{
   unreachable("never executed");
   return 0;
}

static void
wait_for_glLinkProgram(struct gl_context *ctx)
{
   /* Wait for the last glLinkProgram call. */
   int batch = p_atomic_read(&ctx->GLThread.LastProgramChangeBatch);
   if (batch != -1) {
      util_queue_fence_wait(&ctx->GLThread.batches[batch].fence);
      assert(p_atomic_read(&ctx->GLThread.LastProgramChangeBatch) == -1);
   }
}

void GLAPIENTRY
_mesa_marshal_GetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                               GLsizei *length, GLint *size, GLenum *type,
                               GLchar * name)
{
   GET_CURRENT_CONTEXT(ctx);

   wait_for_glLinkProgram(ctx);

   /* We can execute glGetActiveUniform without syncing if we are sync'd to
    * the last calls of glLinkProgram and glDeleteProgram because shader
    * object IDs and their contents are immutable after those calls and
    * also thread-safe because they are shared between contexts.
    * glCreateShaderProgram calls glLinkProgram internally and it always
    * syncs, so it doesn't need any handling.
    */
   _mesa_GetActiveUniform_impl(program, index, bufSize, length, size, type,
                               name, true);
}

uint32_t
_mesa_unmarshal_GetUniformLocation(struct gl_context *ctx,
                                   const struct marshal_cmd_GetUniformLocation *cmd,
                                   const uint64_t *last)
{
   unreachable("never executed");
   return 0;
}

GLint GLAPIENTRY
_mesa_marshal_GetUniformLocation(GLuint program, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);

   wait_for_glLinkProgram(ctx);

   /* This is thread-safe. See the comment in _mesa_marshal_GetActiveUniform. */
   return _mesa_GetUniformLocation_impl(program, name, true);
}
