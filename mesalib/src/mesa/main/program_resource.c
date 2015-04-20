/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2015 Intel Corporation.  All Rights Reserved.
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
 *
 */

#include "main/enums.h"
#include "main/macros.h"
#include "main/mtypes.h"
#include "main/shaderapi.h"
#include "main/shaderobj.h"
#include "program_resource.h"

static bool
supported_interface_enum(GLenum iface)
{
   switch (iface) {
   case GL_UNIFORM:
   case GL_UNIFORM_BLOCK:
   case GL_PROGRAM_INPUT:
   case GL_PROGRAM_OUTPUT:
   case GL_TRANSFORM_FEEDBACK_VARYING:
   case GL_ATOMIC_COUNTER_BUFFER:
      return true;
   case GL_VERTEX_SUBROUTINE:
   case GL_TESS_CONTROL_SUBROUTINE:
   case GL_TESS_EVALUATION_SUBROUTINE:
   case GL_GEOMETRY_SUBROUTINE:
   case GL_FRAGMENT_SUBROUTINE:
   case GL_COMPUTE_SUBROUTINE:
   case GL_VERTEX_SUBROUTINE_UNIFORM:
   case GL_TESS_CONTROL_SUBROUTINE_UNIFORM:
   case GL_TESS_EVALUATION_SUBROUTINE_UNIFORM:
   case GL_GEOMETRY_SUBROUTINE_UNIFORM:
   case GL_FRAGMENT_SUBROUTINE_UNIFORM:
   case GL_COMPUTE_SUBROUTINE_UNIFORM:
   case GL_BUFFER_VARIABLE:
   case GL_SHADER_STORAGE_BLOCK:
   default:
      return false;
   }
}

void GLAPIENTRY
_mesa_GetProgramInterfaceiv(GLuint program, GLenum programInterface,
                            GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   unsigned i;
   struct gl_shader_program *shProg =
      _mesa_lookup_shader_program_err(ctx, program,
                                      "glGetProgramInterfaceiv");
   if (!shProg)
      return;

   if (!params) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glGetProgramInterfaceiv(params NULL)");
      return;
   }

   /* Validate interface. */
   if (!supported_interface_enum(programInterface)) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glGetProgramInterfaceiv(%s)",
                  _mesa_lookup_enum_by_nr(programInterface));
      return;
   }

   /* Validate pname against interface. */
   switch(pname) {
   case GL_ACTIVE_RESOURCES:
      for (i = 0, *params = 0; i < shProg->NumProgramResourceList; i++)
         if (shProg->ProgramResourceList[i].Type == programInterface)
            (*params)++;
      break;
   case GL_MAX_NAME_LENGTH:
      if (programInterface == GL_ATOMIC_COUNTER_BUFFER) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "glGetProgramInterfaceiv(%s pname %s)",
                     _mesa_lookup_enum_by_nr(programInterface),
                     _mesa_lookup_enum_by_nr(pname));
         return;
      }
      /* Name length consists of base name, 3 additional chars '[0]' if
       * resource is an array and finally 1 char for string terminator.
       */
      for (i = 0, *params = 0; i < shProg->NumProgramResourceList; i++) {
         if (shProg->ProgramResourceList[i].Type != programInterface)
            continue;
         const char *name =
            _mesa_program_resource_name(&shProg->ProgramResourceList[i]);
         unsigned array_size =
            _mesa_program_resource_array_size(&shProg->ProgramResourceList[i]);
         *params = MAX2(*params, strlen(name) + (array_size ? 3 : 0) + 1);
      }
      break;
   case GL_MAX_NUM_ACTIVE_VARIABLES:
      switch (programInterface) {
      case GL_UNIFORM_BLOCK:
         for (i = 0, *params = 0; i < shProg->NumProgramResourceList; i++) {
            if (shProg->ProgramResourceList[i].Type == programInterface) {
               struct gl_uniform_block *block =
                  (struct gl_uniform_block *)
                  shProg->ProgramResourceList[i].Data;
               *params = MAX2(*params, block->NumUniforms);
            }
         }
         break;
      case GL_ATOMIC_COUNTER_BUFFER:
         for (i = 0, *params = 0; i < shProg->NumProgramResourceList; i++) {
            if (shProg->ProgramResourceList[i].Type == programInterface) {
               struct gl_active_atomic_buffer *buffer =
                  (struct gl_active_atomic_buffer *)
                  shProg->ProgramResourceList[i].Data;
               *params = MAX2(*params, buffer->NumUniforms);
            }
         }
         break;
      default:
        _mesa_error(ctx, GL_INVALID_OPERATION,
                    "glGetProgramInterfaceiv(%s pname %s)",
                    _mesa_lookup_enum_by_nr(programInterface),
                    _mesa_lookup_enum_by_nr(pname));
      };
      break;
   case GL_MAX_NUM_COMPATIBLE_SUBROUTINES:
   default:
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glGetProgramInterfaceiv(pname %s)",
                  _mesa_lookup_enum_by_nr(pname));
   }
}

static bool
is_xfb_marker(const char *str)
{
   static const char *markers[] = {
      "gl_NextBuffer",
      "gl_SkipComponents1",
      "gl_SkipComponents2",
      "gl_SkipComponents3",
      "gl_SkipComponents4",
      NULL
   };
   const char **m = markers;

   if (strncmp(str, "gl_", 3) != 0)
      return false;

   for (; *m; m++)
      if (strcmp(*m, str) == 0)
         return true;

   return false;
}

/**
 * Checks if given name index is legal for GetProgramResourceIndex,
 * check is written to be compatible with GL_ARB_array_of_arrays.
 */
static bool
valid_program_resource_index_name(const GLchar *name)
{
   const char *array = strstr(name, "[");
   const char *close = strrchr(name, ']');

   /* Not array, no need for the check. */
   if (!array)
      return true;

   /* Last array index has to be zero. */
   if (!close || *--close != '0')
      return false;

   return true;
}

GLuint GLAPIENTRY
_mesa_GetProgramResourceIndex(GLuint program, GLenum programInterface,
                              const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_program_resource *res;
   struct gl_shader_program *shProg =
      _mesa_lookup_shader_program_err(ctx, program,
                                      "glGetProgramResourceIndex");
   if (!shProg || !name)
      return GL_INVALID_INDEX;

   /*
    * For the interface TRANSFORM_FEEDBACK_VARYING, the value INVALID_INDEX
    * should be returned when querying the index assigned to the special names
    * "gl_NextBuffer", "gl_SkipComponents1", "gl_SkipComponents2",
    * "gl_SkipComponents3", and "gl_SkipComponents4".
    */
   if (programInterface == GL_TRANSFORM_FEEDBACK_VARYING &&
       is_xfb_marker(name))
      return GL_INVALID_INDEX;

   switch (programInterface) {
   case GL_PROGRAM_INPUT:
   case GL_PROGRAM_OUTPUT:
   case GL_UNIFORM:
   case GL_UNIFORM_BLOCK:
   case GL_TRANSFORM_FEEDBACK_VARYING:
      /* Validate name syntax for arrays. */
      if (!valid_program_resource_index_name(name))
         return GL_INVALID_INDEX;

      res = _mesa_program_resource_find_name(shProg, programInterface, name);
      if (!res)
         return GL_INVALID_INDEX;

      return _mesa_program_resource_index(shProg, res);
   case GL_ATOMIC_COUNTER_BUFFER:
   default:
      _mesa_error(ctx, GL_INVALID_ENUM, "glGetProgramResourceIndex(%s)",
                  _mesa_lookup_enum_by_nr(programInterface));
   }

   return GL_INVALID_INDEX;
}

void GLAPIENTRY
_mesa_GetProgramResourceName(GLuint program, GLenum programInterface,
                             GLuint index, GLsizei bufSize, GLsizei *length,
                             GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_shader_program *shProg =
      _mesa_lookup_shader_program_err(ctx, program,
                                      "glGetProgramResourceName");

   /* Set user friendly return values in case of errors. */
   if (name)
      *name = '\0';
   if (length)
      *length = 0;

   if (!shProg || !name)
      return;

   if (programInterface == GL_ATOMIC_COUNTER_BUFFER ||
       !supported_interface_enum(programInterface)) {
      _mesa_error(ctx, GL_INVALID_ENUM, "glGetProgramResourceName(%s)",
                  _mesa_lookup_enum_by_nr(programInterface));
      return;
   }

   _mesa_get_program_resource_name(shProg, programInterface, index, bufSize,
                                   length, name, "glGetProgramResourceName");
}

void GLAPIENTRY
_mesa_GetProgramResourceiv(GLuint program, GLenum programInterface,
                           GLuint index, GLsizei propCount,
                           const GLenum *props, GLsizei bufSize,
                           GLsizei *length, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_shader_program *shProg =
      _mesa_lookup_shader_program_err(ctx, program, "glGetProgramResourceiv");

   if (!shProg || !params)
      return;

   /* The error INVALID_VALUE is generated if <propCount> is zero.
    * Note that we check < 0 here because it makes sense to bail early.
    */
   if (propCount <= 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glGetProgramResourceiv(propCount <= 0)");
      return;
   }

   /* No need to write any properties, user requested none. */
   if (bufSize == 0)
      return;

   _mesa_get_program_resourceiv(shProg, programInterface, index,
                                propCount, props, bufSize, length, params);
}

/**
 * Function verifies syntax of given name for GetProgramResourceLocation
 * and GetProgramResourceLocationIndex for the following cases:
 *
 * "array element portion of a string passed to GetProgramResourceLocation
 * or GetProgramResourceLocationIndex must not have, a "+" sign, extra
 * leading zeroes, or whitespace".
 *
 * Check is written to be compatible with GL_ARB_array_of_arrays.
 */
static bool
invalid_array_element_syntax(const GLchar *name)
{
   char *first = strchr(name, '[');
   char *last = strrchr(name, '[');

   if (!first)
      return false;

   /* No '+' or ' ' allowed anywhere. */
   if (strchr(first, '+') || strchr(first, ' '))
      return true;

   /* Check that last array index is 0. */
   if (last[1] == '0' && last[2] != ']')
      return true;

   return false;
}

static struct gl_shader_program *
lookup_linked_program(GLuint program, const char *caller)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_shader_program *prog =
      _mesa_lookup_shader_program_err(ctx, program, caller);

   if (!prog)
      return NULL;

   if (prog->LinkStatus == GL_FALSE) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(program not linked)",
                  caller);
      return NULL;
   }
   return prog;
}

GLint GLAPIENTRY
_mesa_GetProgramResourceLocation(GLuint program, GLenum programInterface,
                                 const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_shader_program *shProg =
      lookup_linked_program(program, "glGetProgramResourceLocation");

   if (!shProg || !name || invalid_array_element_syntax(name))
      return -1;

   /* Validate programInterface. */
   switch (programInterface) {
   case GL_UNIFORM:
   case GL_PROGRAM_INPUT:
   case GL_PROGRAM_OUTPUT:
      break;

   /* For reference valid cases requiring additional extension support:
    * GL_ARB_shader_subroutine
    * GL_ARB_tessellation_shader
    * GL_ARB_compute_shader
    */
   case GL_VERTEX_SUBROUTINE_UNIFORM:
   case GL_TESS_CONTROL_SUBROUTINE_UNIFORM:
   case GL_TESS_EVALUATION_SUBROUTINE_UNIFORM:
   case GL_GEOMETRY_SUBROUTINE_UNIFORM:
   case GL_FRAGMENT_SUBROUTINE_UNIFORM:
   case GL_COMPUTE_SUBROUTINE_UNIFORM:

   default:
      _mesa_error(ctx, GL_INVALID_ENUM, "glGetProgramResourceLocation(%s %s)",
                  _mesa_lookup_enum_by_nr(programInterface), name);
   }

   return _mesa_program_resource_location(shProg, programInterface, name);
}

/**
 * Returns output index for dual source blending.
 */
GLint GLAPIENTRY
_mesa_GetProgramResourceLocationIndex(GLuint program, GLenum programInterface,
                                      const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_shader_program *shProg =
      lookup_linked_program(program, "glGetProgramResourceLocationIndex");

   if (!shProg || !name || invalid_array_element_syntax(name))
      return -1;

   /* From the GL_ARB_program_interface_query spec:
    *
    * "For GetProgramResourceLocationIndex, <programInterface> must be
    * PROGRAM_OUTPUT."
    */
   if (programInterface != GL_PROGRAM_OUTPUT) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glGetProgramResourceLocationIndex(%s)",
                  _mesa_lookup_enum_by_nr(programInterface));
      return -1;
   }

   return _mesa_program_resource_location_index(shProg, GL_PROGRAM_OUTPUT,
                                                name);
}
