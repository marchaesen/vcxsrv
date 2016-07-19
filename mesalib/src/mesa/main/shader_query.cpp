/*
 * Copyright © 2011 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file shader_query.cpp
 * C-to-C++ bridge functions to query GLSL shader data
 *
 * \author Ian Romanick <ian.d.romanick@intel.com>
 */

#include "main/context.h"
#include "main/core.h"
#include "main/enums.h"
#include "main/shaderapi.h"
#include "main/shaderobj.h"
#include "main/uniforms.h"
#include "compiler/glsl/glsl_symbol_table.h"
#include "compiler/glsl/ir.h"
#include "compiler/glsl/program.h"
#include "program/hash_table.h"
#include "util/strndup.h"


static GLint
program_resource_location(struct gl_program_resource *res,
                          unsigned array_index);

/**
 * Declare convenience functions to return resource data in a given type.
 * Warning! this is not type safe so be *very* careful when using these.
 */
#define DECL_RESOURCE_FUNC(name, type) \
const type * RESOURCE_ ## name (gl_program_resource *res) { \
   assert(res->Data); \
   return (type *) res->Data; \
}

DECL_RESOURCE_FUNC(VAR, gl_shader_variable);
DECL_RESOURCE_FUNC(UBO, gl_uniform_block);
DECL_RESOURCE_FUNC(UNI, gl_uniform_storage);
DECL_RESOURCE_FUNC(ATC, gl_active_atomic_buffer);
DECL_RESOURCE_FUNC(XFV, gl_transform_feedback_varying_info);
DECL_RESOURCE_FUNC(XFB, gl_transform_feedback_buffer);
DECL_RESOURCE_FUNC(SUB, gl_subroutine_function);

void GLAPIENTRY
_mesa_BindAttribLocation(GLuint program, GLuint index,
                         const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);

   struct gl_shader_program *const shProg =
      _mesa_lookup_shader_program_err(ctx, program, "glBindAttribLocation");
   if (!shProg)
      return;

   if (!name)
      return;

   if (strncmp(name, "gl_", 3) == 0) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glBindAttribLocation(illegal name)");
      return;
   }

   if (index >= ctx->Const.Program[MESA_SHADER_VERTEX].MaxAttribs) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glBindAttribLocation(%u >= %u)",
                  index, ctx->Const.Program[MESA_SHADER_VERTEX].MaxAttribs);
      return;
   }

   /* Replace the current value if it's already in the list.  Add
    * VERT_ATTRIB_GENERIC0 because that's how the linker differentiates
    * between built-in attributes and user-defined attributes.
    */
   shProg->AttributeBindings->put(index + VERT_ATTRIB_GENERIC0, name);

   /*
    * Note that this attribute binding won't go into effect until
    * glLinkProgram is called again.
    */
}

void GLAPIENTRY
_mesa_GetActiveAttrib(GLuint program, GLuint desired_index,
                      GLsizei maxLength, GLsizei * length, GLint * size,
                      GLenum * type, GLchar * name)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_shader_program *shProg;

   if (maxLength < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glGetActiveAttrib(maxLength < 0)");
      return;
   }

   shProg = _mesa_lookup_shader_program_err(ctx, program, "glGetActiveAttrib");
   if (!shProg)
      return;

   if (!shProg->LinkStatus) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glGetActiveAttrib(program not linked)");
      return;
   }

   if (shProg->_LinkedShaders[MESA_SHADER_VERTEX] == NULL) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glGetActiveAttrib(no vertex shader)");
      return;
   }

   struct gl_program_resource *res =
      _mesa_program_resource_find_index(shProg, GL_PROGRAM_INPUT,
                                        desired_index);

   /* User asked for index that does not exist. */
   if (!res) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glGetActiveAttrib(index)");
      return;
   }

   const gl_shader_variable *const var = RESOURCE_VAR(res);

   const char *var_name = var->name;

   _mesa_copy_string(name, maxLength, length, var_name);

   if (size)
      _mesa_program_resource_prop(shProg, res, desired_index, GL_ARRAY_SIZE,
                                  size, "glGetActiveAttrib");

   if (type)
      _mesa_program_resource_prop(shProg, res, desired_index, GL_TYPE,
                                  (GLint *) type, "glGetActiveAttrib");
}

GLint GLAPIENTRY
_mesa_GetAttribLocation(GLuint program, const GLchar * name)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_shader_program *const shProg =
      _mesa_lookup_shader_program_err(ctx, program, "glGetAttribLocation");

   if (!shProg) {
      return -1;
   }

   if (!shProg->LinkStatus) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glGetAttribLocation(program not linked)");
      return -1;
   }

   if (!name)
      return -1;

   /* Not having a vertex shader is not an error.
    */
   if (shProg->_LinkedShaders[MESA_SHADER_VERTEX] == NULL)
      return -1;

   unsigned array_index = 0;
   struct gl_program_resource *res =
      _mesa_program_resource_find_name(shProg, GL_PROGRAM_INPUT, name,
                                       &array_index);

   if (!res)
      return -1;

   return program_resource_location(res, array_index);
}

unsigned
_mesa_count_active_attribs(struct gl_shader_program *shProg)
{
   if (!shProg->LinkStatus
       || shProg->_LinkedShaders[MESA_SHADER_VERTEX] == NULL) {
      return 0;
   }

   struct gl_program_resource *res = shProg->ProgramResourceList;
   unsigned count = 0;
   for (unsigned j = 0; j < shProg->NumProgramResourceList; j++, res++) {
      if (res->Type == GL_PROGRAM_INPUT &&
          res->StageReferences & (1 << MESA_SHADER_VERTEX))
         count++;
   }
   return count;
}


size_t
_mesa_longest_attribute_name_length(struct gl_shader_program *shProg)
{
   if (!shProg->LinkStatus
       || shProg->_LinkedShaders[MESA_SHADER_VERTEX] == NULL) {
      return 0;
   }

   struct gl_program_resource *res = shProg->ProgramResourceList;
   size_t longest = 0;
   for (unsigned j = 0; j < shProg->NumProgramResourceList; j++, res++) {
      if (res->Type == GL_PROGRAM_INPUT &&
          res->StageReferences & (1 << MESA_SHADER_VERTEX)) {

          const size_t length = strlen(RESOURCE_VAR(res)->name);
          if (length >= longest)
             longest = length + 1;
      }
   }

   return longest;
}

void GLAPIENTRY
_mesa_BindFragDataLocation(GLuint program, GLuint colorNumber,
			   const GLchar *name)
{
   _mesa_BindFragDataLocationIndexed(program, colorNumber, 0, name);
}

void GLAPIENTRY
_mesa_BindFragDataLocationIndexed(GLuint program, GLuint colorNumber,
                                  GLuint index, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);

   struct gl_shader_program *const shProg =
      _mesa_lookup_shader_program_err(ctx, program, "glBindFragDataLocationIndexed");
   if (!shProg)
      return;

   if (!name)
      return;

   if (strncmp(name, "gl_", 3) == 0) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glBindFragDataLocationIndexed(illegal name)");
      return;
   }

   if (index > 1) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glBindFragDataLocationIndexed(index)");
      return;
   }

   if (index == 0 && colorNumber >= ctx->Const.MaxDrawBuffers) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glBindFragDataLocationIndexed(colorNumber)");
      return;
   }

   if (index == 1 && colorNumber >= ctx->Const.MaxDualSourceDrawBuffers) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glBindFragDataLocationIndexed(colorNumber)");
      return;
   }

   /* Replace the current value if it's already in the list.  Add
    * FRAG_RESULT_DATA0 because that's how the linker differentiates
    * between built-in attributes and user-defined attributes.
    */
   shProg->FragDataBindings->put(colorNumber + FRAG_RESULT_DATA0, name);
   shProg->FragDataIndexBindings->put(index, name);
   /*
    * Note that this binding won't go into effect until
    * glLinkProgram is called again.
    */

}

GLint GLAPIENTRY
_mesa_GetFragDataIndex(GLuint program, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_shader_program *const shProg =
      _mesa_lookup_shader_program_err(ctx, program, "glGetFragDataIndex");

   if (!shProg) {
      return -1;
   }

   if (!shProg->LinkStatus) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glGetFragDataIndex(program not linked)");
      return -1;
   }

   if (!name)
      return -1;

   if (strncmp(name, "gl_", 3) == 0) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glGetFragDataIndex(illegal name)");
      return -1;
   }

   /* Not having a fragment shader is not an error.
    */
   if (shProg->_LinkedShaders[MESA_SHADER_FRAGMENT] == NULL)
      return -1;

   return _mesa_program_resource_location_index(shProg, GL_PROGRAM_OUTPUT,
                                                name);
}

GLint GLAPIENTRY
_mesa_GetFragDataLocation(GLuint program, const GLchar *name)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_shader_program *const shProg =
      _mesa_lookup_shader_program_err(ctx, program, "glGetFragDataLocation");

   if (!shProg) {
      return -1;
   }

   if (!shProg->LinkStatus) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glGetFragDataLocation(program not linked)");
      return -1;
   }

   if (!name)
      return -1;

   if (strncmp(name, "gl_", 3) == 0) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glGetFragDataLocation(illegal name)");
      return -1;
   }

   /* Not having a fragment shader is not an error.
    */
   if (shProg->_LinkedShaders[MESA_SHADER_FRAGMENT] == NULL)
      return -1;

   unsigned array_index = 0;
   struct gl_program_resource *res =
      _mesa_program_resource_find_name(shProg, GL_PROGRAM_OUTPUT, name,
                                       &array_index);

   if (!res)
      return -1;

   return program_resource_location(res, array_index);
}

const char*
_mesa_program_resource_name(struct gl_program_resource *res)
{
   switch (res->Type) {
   case GL_UNIFORM_BLOCK:
   case GL_SHADER_STORAGE_BLOCK:
      return RESOURCE_UBO(res)->Name;
   case GL_TRANSFORM_FEEDBACK_VARYING:
      return RESOURCE_XFV(res)->Name;
   case GL_PROGRAM_INPUT:
   case GL_PROGRAM_OUTPUT:
      return RESOURCE_VAR(res)->name;
   case GL_UNIFORM:
   case GL_BUFFER_VARIABLE:
      return RESOURCE_UNI(res)->name;
   case GL_VERTEX_SUBROUTINE_UNIFORM:
   case GL_GEOMETRY_SUBROUTINE_UNIFORM:
   case GL_FRAGMENT_SUBROUTINE_UNIFORM:
   case GL_COMPUTE_SUBROUTINE_UNIFORM:
   case GL_TESS_CONTROL_SUBROUTINE_UNIFORM:
   case GL_TESS_EVALUATION_SUBROUTINE_UNIFORM:
      return RESOURCE_UNI(res)->name + MESA_SUBROUTINE_PREFIX_LEN;
   case GL_VERTEX_SUBROUTINE:
   case GL_GEOMETRY_SUBROUTINE:
   case GL_FRAGMENT_SUBROUTINE:
   case GL_COMPUTE_SUBROUTINE:
   case GL_TESS_CONTROL_SUBROUTINE:
   case GL_TESS_EVALUATION_SUBROUTINE:
      return RESOURCE_SUB(res)->name;
   default:
      assert(!"support for resource type not implemented");
   }
   return NULL;
}


unsigned
_mesa_program_resource_array_size(struct gl_program_resource *res)
{
   switch (res->Type) {
   case GL_TRANSFORM_FEEDBACK_VARYING:
      return RESOURCE_XFV(res)->Size > 1 ?
             RESOURCE_XFV(res)->Size : 0;
   case GL_PROGRAM_INPUT:
   case GL_PROGRAM_OUTPUT:
      return RESOURCE_VAR(res)->type->length;
   case GL_UNIFORM:
   case GL_VERTEX_SUBROUTINE_UNIFORM:
   case GL_GEOMETRY_SUBROUTINE_UNIFORM:
   case GL_FRAGMENT_SUBROUTINE_UNIFORM:
   case GL_COMPUTE_SUBROUTINE_UNIFORM:
   case GL_TESS_CONTROL_SUBROUTINE_UNIFORM:
   case GL_TESS_EVALUATION_SUBROUTINE_UNIFORM:
      return RESOURCE_UNI(res)->array_elements;
   case GL_BUFFER_VARIABLE:
      /* Unsized arrays */
      if (RESOURCE_UNI(res)->array_stride > 0 &&
          RESOURCE_UNI(res)->array_elements == 0)
         return 1;
      else
         return RESOURCE_UNI(res)->array_elements;
   case GL_VERTEX_SUBROUTINE:
   case GL_GEOMETRY_SUBROUTINE:
   case GL_FRAGMENT_SUBROUTINE:
   case GL_COMPUTE_SUBROUTINE:
   case GL_TESS_CONTROL_SUBROUTINE:
   case GL_TESS_EVALUATION_SUBROUTINE:
   case GL_ATOMIC_COUNTER_BUFFER:
   case GL_UNIFORM_BLOCK:
   case GL_SHADER_STORAGE_BLOCK:
      return 0;
   default:
      assert(!"support for resource type not implemented");
   }
   return 0;
}

/**
 * Checks if array subscript is valid and if so sets array_index.
 */
static bool
valid_array_index(const GLchar *name, unsigned *array_index)
{
   long idx = 0;
   const GLchar *out_base_name_end;

   idx = parse_program_resource_name(name, &out_base_name_end);
   if (idx < 0)
      return false;

   if (array_index)
      *array_index = idx;

   return true;
}

/* Find a program resource with specific name in given interface.
 */
struct gl_program_resource *
_mesa_program_resource_find_name(struct gl_shader_program *shProg,
                                 GLenum programInterface, const char *name,
                                 unsigned *array_index)
{
   struct gl_program_resource *res = shProg->ProgramResourceList;
   for (unsigned i = 0; i < shProg->NumProgramResourceList; i++, res++) {
      if (res->Type != programInterface)
         continue;

      /* Resource basename. */
      const char *rname = _mesa_program_resource_name(res);
      unsigned baselen = strlen(rname);
      unsigned baselen_without_array_index = baselen;
      const char *rname_last_square_bracket = strrchr(rname, '[');
      bool found = false;
      bool rname_has_array_index_zero = false;
      /* From ARB_program_interface_query spec:
       *
       * "uint GetProgramResourceIndex(uint program, enum programInterface,
       *                               const char *name);
       *  [...]
       *  If <name> exactly matches the name string of one of the active
       *  resources for <programInterface>, the index of the matched resource is
       *  returned. Additionally, if <name> would exactly match the name string
       *  of an active resource if "[0]" were appended to <name>, the index of
       *  the matched resource is returned. [...]"
       *
       * "A string provided to GetProgramResourceLocation or
       * GetProgramResourceLocationIndex is considered to match an active variable
       * if:
       *
       *  * the string exactly matches the name of the active variable;
       *
       *  * if the string identifies the base name of an active array, where the
       *    string would exactly match the name of the variable if the suffix
       *    "[0]" were appended to the string; [...]"
       */
      /* Remove array's index from interface block name comparison only if
       * array's index is zero and the resulting string length is the same
       * than the provided name's length.
       */
      if (rname_last_square_bracket) {
         baselen_without_array_index -= strlen(rname_last_square_bracket);
         rname_has_array_index_zero =
            (strcmp(rname_last_square_bracket, "[0]") == 0) &&
            (baselen_without_array_index == strlen(name));
      }

      if (strncmp(rname, name, baselen) == 0)
         found = true;
      else if (rname_has_array_index_zero &&
               strncmp(rname, name, baselen_without_array_index) == 0)
         found = true;

      if (found) {
         switch (programInterface) {
         case GL_UNIFORM_BLOCK:
         case GL_SHADER_STORAGE_BLOCK:
            /* Basename match, check if array or struct. */
            if (rname_has_array_index_zero ||
                name[baselen] == '\0' ||
                name[baselen] == '[' ||
                name[baselen] == '.') {
               return res;
            }
            break;
         case GL_TRANSFORM_FEEDBACK_VARYING:
         case GL_BUFFER_VARIABLE:
         case GL_UNIFORM:
         case GL_VERTEX_SUBROUTINE_UNIFORM:
         case GL_GEOMETRY_SUBROUTINE_UNIFORM:
         case GL_FRAGMENT_SUBROUTINE_UNIFORM:
         case GL_COMPUTE_SUBROUTINE_UNIFORM:
         case GL_TESS_CONTROL_SUBROUTINE_UNIFORM:
         case GL_TESS_EVALUATION_SUBROUTINE_UNIFORM:
         case GL_VERTEX_SUBROUTINE:
         case GL_GEOMETRY_SUBROUTINE:
         case GL_FRAGMENT_SUBROUTINE:
         case GL_COMPUTE_SUBROUTINE:
         case GL_TESS_CONTROL_SUBROUTINE:
         case GL_TESS_EVALUATION_SUBROUTINE:
            if (name[baselen] == '.') {
               return res;
            }
            /* fall-through */
         case GL_PROGRAM_INPUT:
         case GL_PROGRAM_OUTPUT:
            if (name[baselen] == '\0') {
               return res;
            } else if (name[baselen] == '[' &&
                valid_array_index(name, array_index)) {
               return res;
            }
            break;
         default:
            assert(!"not implemented for given interface");
         }
      }
   }
   return NULL;
}

static GLuint
calc_resource_index(struct gl_shader_program *shProg,
                    struct gl_program_resource *res)
{
   unsigned i;
   GLuint index = 0;
   for (i = 0; i < shProg->NumProgramResourceList; i++) {
      if (&shProg->ProgramResourceList[i] == res)
         return index;
      if (shProg->ProgramResourceList[i].Type == res->Type)
         index++;
   }
   return GL_INVALID_INDEX;
}

/**
 * Calculate index for the given resource.
 */
GLuint
_mesa_program_resource_index(struct gl_shader_program *shProg,
                             struct gl_program_resource *res)
{
   if (!res)
      return GL_INVALID_INDEX;

   switch (res->Type) {
   case GL_ATOMIC_COUNTER_BUFFER:
      return RESOURCE_ATC(res) - shProg->AtomicBuffers;
   case GL_VERTEX_SUBROUTINE:
   case GL_GEOMETRY_SUBROUTINE:
   case GL_FRAGMENT_SUBROUTINE:
   case GL_COMPUTE_SUBROUTINE:
   case GL_TESS_CONTROL_SUBROUTINE:
   case GL_TESS_EVALUATION_SUBROUTINE:
      return RESOURCE_SUB(res)->index;
   case GL_UNIFORM_BLOCK:
   case GL_SHADER_STORAGE_BLOCK:
   case GL_TRANSFORM_FEEDBACK_BUFFER:
   case GL_TRANSFORM_FEEDBACK_VARYING:
   default:
      return calc_resource_index(shProg, res);
   }
}

/**
 * Find a program resource that points to given data.
 */
static struct gl_program_resource*
program_resource_find_data(struct gl_shader_program *shProg, void *data)
{
   struct gl_program_resource *res = shProg->ProgramResourceList;
   for (unsigned i = 0; i < shProg->NumProgramResourceList; i++, res++) {
      if (res->Data == data)
         return res;
   }
   return NULL;
}

/* Find a program resource with specific index in given interface.
 */
struct gl_program_resource *
_mesa_program_resource_find_index(struct gl_shader_program *shProg,
                                  GLenum programInterface, GLuint index)
{
   struct gl_program_resource *res = shProg->ProgramResourceList;
   int idx = -1;

   for (unsigned i = 0; i < shProg->NumProgramResourceList; i++, res++) {
      if (res->Type != programInterface)
         continue;

      switch (res->Type) {
      case GL_UNIFORM_BLOCK:
      case GL_ATOMIC_COUNTER_BUFFER:
      case GL_SHADER_STORAGE_BLOCK:
      case GL_TRANSFORM_FEEDBACK_BUFFER:
         if (_mesa_program_resource_index(shProg, res) == index)
            return res;
         break;
      case GL_TRANSFORM_FEEDBACK_VARYING:
      case GL_PROGRAM_INPUT:
      case GL_PROGRAM_OUTPUT:
      case GL_UNIFORM:
      case GL_VERTEX_SUBROUTINE_UNIFORM:
      case GL_GEOMETRY_SUBROUTINE_UNIFORM:
      case GL_FRAGMENT_SUBROUTINE_UNIFORM:
      case GL_COMPUTE_SUBROUTINE_UNIFORM:
      case GL_TESS_CONTROL_SUBROUTINE_UNIFORM:
      case GL_TESS_EVALUATION_SUBROUTINE_UNIFORM:
      case GL_VERTEX_SUBROUTINE:
      case GL_GEOMETRY_SUBROUTINE:
      case GL_FRAGMENT_SUBROUTINE:
      case GL_COMPUTE_SUBROUTINE:
      case GL_TESS_CONTROL_SUBROUTINE:
      case GL_TESS_EVALUATION_SUBROUTINE:
      case GL_BUFFER_VARIABLE:
         if (++idx == (int) index)
            return res;
         break;
      default:
         assert(!"not implemented for given interface");
      }
   }
   return NULL;
}

/* Function returns if resource name is expected to have index
 * appended into it.
 *
 *
 * Page 61 (page 73 of the PDF) in section 2.11 of the OpenGL ES 3.0
 * spec says:
 *
 *     "If the active uniform is an array, the uniform name returned in
 *     name will always be the name of the uniform array appended with
 *     "[0]"."
 *
 * The same text also appears in the OpenGL 4.2 spec.  It does not,
 * however, appear in any previous spec.  Previous specifications are
 * ambiguous in this regard.  However, either name can later be passed
 * to glGetUniformLocation (and related APIs), so there shouldn't be any
 * harm in always appending "[0]" to uniform array names.
 *
 * Geometry shader stage has different naming convention where the 'normal'
 * condition is an array, therefore for variables referenced in geometry
 * stage we do not add '[0]'.
 *
 * Note, that TCS outputs and TES inputs should not have index appended
 * either.
 */
static bool
add_index_to_name(struct gl_program_resource *res)
{
   bool add_index = !((res->Type == GL_PROGRAM_INPUT &&
                       res->StageReferences & (1 << MESA_SHADER_GEOMETRY |
                                               1 << MESA_SHADER_TESS_CTRL |
                                               1 << MESA_SHADER_TESS_EVAL)) ||
                      (res->Type == GL_PROGRAM_OUTPUT &&
                       res->StageReferences & 1 << MESA_SHADER_TESS_CTRL));

   /* Transform feedback varyings have array index already appended
    * in their names.
    */
   if (res->Type == GL_TRANSFORM_FEEDBACK_VARYING)
      add_index = false;

   return add_index;
}

/* Get name length of a program resource. This consists of
 * base name + 3 for '[0]' if resource is an array.
 */
extern unsigned
_mesa_program_resource_name_len(struct gl_program_resource *res)
{
   unsigned length = strlen(_mesa_program_resource_name(res));
   if (_mesa_program_resource_array_size(res) && add_index_to_name(res))
      length += 3;
   return length;
}

/* Get full name of a program resource.
 */
bool
_mesa_get_program_resource_name(struct gl_shader_program *shProg,
                                GLenum programInterface, GLuint index,
                                GLsizei bufSize, GLsizei *length,
                                GLchar *name, const char *caller)
{
   GET_CURRENT_CONTEXT(ctx);

   /* Find resource with given interface and index. */
   struct gl_program_resource *res =
      _mesa_program_resource_find_index(shProg, programInterface, index);

   /* The error INVALID_VALUE is generated if <index> is greater than
   * or equal to the number of entries in the active resource list for
   * <programInterface>.
   */
   if (!res) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(index %u)", caller, index);
      return false;
   }

   if (bufSize < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(bufSize %d)", caller, bufSize);
      return false;
   }

   GLsizei localLength;

   if (length == NULL)
      length = &localLength;

   _mesa_copy_string(name, bufSize, length, _mesa_program_resource_name(res));

   if (_mesa_program_resource_array_size(res) && add_index_to_name(res)) {
      int i;

      /* The comparison is strange because *length does *NOT* include the
       * terminating NUL, but maxLength does.
       */
      for (i = 0; i < 3 && (*length + i + 1) < bufSize; i++)
         name[*length + i] = "[0]"[i];

      name[*length + i] = '\0';
      *length += i;
   }
   return true;
}

static GLint
program_resource_location(struct gl_program_resource *res, unsigned array_index)
{
   switch (res->Type) {
   case GL_PROGRAM_INPUT: {
      const gl_shader_variable *var = RESOURCE_VAR(res);

      if (var->location == -1)
         return -1;

      /* If the input is an array, fail if the index is out of bounds. */
      if (array_index > 0
          && array_index >= var->type->length) {
         return -1;
      }
      return var->location +
	     (array_index * var->type->without_array()->matrix_columns);
   }
   case GL_PROGRAM_OUTPUT:
      if (RESOURCE_VAR(res)->location == -1)
         return -1;

      /* If the output is an array, fail if the index is out of bounds. */
      if (array_index > 0
          && array_index >= RESOURCE_VAR(res)->type->length) {
         return -1;
      }
      return RESOURCE_VAR(res)->location + array_index;
   case GL_UNIFORM:
      /* If the uniform is built-in, fail. */
      if (RESOURCE_UNI(res)->builtin)
         return -1;

     /* From page 79 of the OpenGL 4.2 spec:
      *
      *     "A valid name cannot be a structure, an array of structures, or any
      *     portion of a single vector or a matrix."
      */
      if (RESOURCE_UNI(res)->type->without_array()->is_record())
         return -1;

      /* From the GL_ARB_uniform_buffer_object spec:
       *
       *     "The value -1 will be returned if <name> does not correspond to an
       *     active uniform variable name in <program>, if <name> is associated
       *     with a named uniform block, or if <name> starts with the reserved
       *     prefix "gl_"."
       */
      if (RESOURCE_UNI(res)->block_index != -1 ||
          RESOURCE_UNI(res)->atomic_buffer_index != -1)
         return -1;

      /* fallthrough */
   case GL_VERTEX_SUBROUTINE_UNIFORM:
   case GL_GEOMETRY_SUBROUTINE_UNIFORM:
   case GL_FRAGMENT_SUBROUTINE_UNIFORM:
   case GL_COMPUTE_SUBROUTINE_UNIFORM:
   case GL_TESS_CONTROL_SUBROUTINE_UNIFORM:
   case GL_TESS_EVALUATION_SUBROUTINE_UNIFORM:
      /* If the uniform is an array, fail if the index is out of bounds. */
      if (array_index > 0
          && array_index >= RESOURCE_UNI(res)->array_elements) {
         return -1;
      }

      /* location in remap table + array element offset */
      return RESOURCE_UNI(res)->remap_location + array_index;
   default:
      return -1;
   }
}

/**
 * Function implements following location queries:
 *    glGetUniformLocation
 */
GLint
_mesa_program_resource_location(struct gl_shader_program *shProg,
                                GLenum programInterface, const char *name)
{
   unsigned array_index = 0;
   struct gl_program_resource *res =
      _mesa_program_resource_find_name(shProg, programInterface, name,
                                       &array_index);

   /* Resource not found. */
   if (!res)
      return -1;

   return program_resource_location(res, array_index);
}

/**
 * Function implements following index queries:
 *    glGetFragDataIndex
 */
GLint
_mesa_program_resource_location_index(struct gl_shader_program *shProg,
                                      GLenum programInterface, const char *name)
{
   struct gl_program_resource *res =
      _mesa_program_resource_find_name(shProg, programInterface, name, NULL);

   /* Non-existent variable or resource is not referenced by fragment stage. */
   if (!res || !(res->StageReferences & (1 << MESA_SHADER_FRAGMENT)))
      return -1;

   /* From OpenGL 4.5 spec, 7.3 Program Objects
    * "The value -1 will be returned by either command...
    *  ... or if name identifies an active variable that does not have a
    * valid location assigned.
    */
   if (RESOURCE_VAR(res)->location == -1)
      return -1;
   return RESOURCE_VAR(res)->index;
}

static uint8_t
stage_from_enum(GLenum ref)
{
   switch (ref) {
   case GL_REFERENCED_BY_VERTEX_SHADER:
      return MESA_SHADER_VERTEX;
   case GL_REFERENCED_BY_TESS_CONTROL_SHADER:
      return MESA_SHADER_TESS_CTRL;
   case GL_REFERENCED_BY_TESS_EVALUATION_SHADER:
      return MESA_SHADER_TESS_EVAL;
   case GL_REFERENCED_BY_GEOMETRY_SHADER:
      return MESA_SHADER_GEOMETRY;
   case GL_REFERENCED_BY_FRAGMENT_SHADER:
      return MESA_SHADER_FRAGMENT;
   case GL_REFERENCED_BY_COMPUTE_SHADER:
      return MESA_SHADER_COMPUTE;
   default:
      assert(!"shader stage not supported");
      return MESA_SHADER_STAGES;
   }
}

/**
 * Check if resource is referenced by given 'referenced by' stage enum.
 * ATC and UBO resources hold stage references of their own.
 */
static bool
is_resource_referenced(struct gl_shader_program *shProg,
                       struct gl_program_resource *res,
                       GLuint index, uint8_t stage)
{
   /* First, check if we even have such a stage active. */
   if (!shProg->_LinkedShaders[stage])
      return false;

   if (res->Type == GL_ATOMIC_COUNTER_BUFFER)
      return RESOURCE_ATC(res)->StageReferences[stage];

   if (res->Type == GL_UNIFORM_BLOCK)
      return shProg->UniformBlocks[index].stageref & (1 << stage);

   if (res->Type == GL_SHADER_STORAGE_BLOCK)
      return shProg->ShaderStorageBlocks[index].stageref & (1 << stage);

   return res->StageReferences & (1 << stage);
}

static unsigned
get_buffer_property(struct gl_shader_program *shProg,
                    struct gl_program_resource *res, const GLenum prop,
                    GLint *val, const char *caller)
{
   GET_CURRENT_CONTEXT(ctx);
   if (res->Type != GL_UNIFORM_BLOCK &&
       res->Type != GL_ATOMIC_COUNTER_BUFFER &&
       res->Type != GL_SHADER_STORAGE_BLOCK &&
       res->Type != GL_TRANSFORM_FEEDBACK_BUFFER)
      goto invalid_operation;

   if (res->Type == GL_UNIFORM_BLOCK) {
      switch (prop) {
      case GL_BUFFER_BINDING:
         *val = RESOURCE_UBO(res)->Binding;
         return 1;
      case GL_BUFFER_DATA_SIZE:
         *val = RESOURCE_UBO(res)->UniformBufferSize;
         return 1;
      case GL_NUM_ACTIVE_VARIABLES:
         *val = 0;
         for (unsigned i = 0; i < RESOURCE_UBO(res)->NumUniforms; i++) {
            const char *iname = RESOURCE_UBO(res)->Uniforms[i].IndexName;
            struct gl_program_resource *uni =
               _mesa_program_resource_find_name(shProg, GL_UNIFORM, iname,
                                                NULL);
            if (!uni)
               continue;
            (*val)++;
         }
         return 1;
      case GL_ACTIVE_VARIABLES: {
         unsigned num_values = 0;
         for (unsigned i = 0; i < RESOURCE_UBO(res)->NumUniforms; i++) {
            const char *iname = RESOURCE_UBO(res)->Uniforms[i].IndexName;
            struct gl_program_resource *uni =
               _mesa_program_resource_find_name(shProg, GL_UNIFORM, iname,
                                                NULL);
            if (!uni)
               continue;
            *val++ =
               _mesa_program_resource_index(shProg, uni);
            num_values++;
         }
         return num_values;
      }
      }
   } else if (res->Type == GL_SHADER_STORAGE_BLOCK) {
      switch (prop) {
      case GL_BUFFER_BINDING:
         *val = RESOURCE_UBO(res)->Binding;
         return 1;
      case GL_BUFFER_DATA_SIZE:
         *val = RESOURCE_UBO(res)->UniformBufferSize;
         return 1;
      case GL_NUM_ACTIVE_VARIABLES:
         *val = 0;
         for (unsigned i = 0; i < RESOURCE_UBO(res)->NumUniforms; i++) {
            const char *iname = RESOURCE_UBO(res)->Uniforms[i].IndexName;
            struct gl_program_resource *uni =
               _mesa_program_resource_find_name(shProg, GL_BUFFER_VARIABLE,
                                                iname, NULL);
            if (!uni)
               continue;
            (*val)++;
         }
         return 1;
      case GL_ACTIVE_VARIABLES: {
         unsigned num_values = 0;
         for (unsigned i = 0; i < RESOURCE_UBO(res)->NumUniforms; i++) {
            const char *iname = RESOURCE_UBO(res)->Uniforms[i].IndexName;
            struct gl_program_resource *uni =
               _mesa_program_resource_find_name(shProg, GL_BUFFER_VARIABLE,
                                                iname, NULL);
            if (!uni)
               continue;
            *val++ =
               _mesa_program_resource_index(shProg, uni);
            num_values++;
         }
         return num_values;
      }
      }
   } else if (res->Type == GL_ATOMIC_COUNTER_BUFFER) {
      switch (prop) {
      case GL_BUFFER_BINDING:
         *val = RESOURCE_ATC(res)->Binding;
         return 1;
      case GL_BUFFER_DATA_SIZE:
         *val = RESOURCE_ATC(res)->MinimumSize;
         return 1;
      case GL_NUM_ACTIVE_VARIABLES:
         *val = RESOURCE_ATC(res)->NumUniforms;
         return 1;
      case GL_ACTIVE_VARIABLES:
         for (unsigned i = 0; i < RESOURCE_ATC(res)->NumUniforms; i++) {
            /* Active atomic buffer contains index to UniformStorage. Find
             * out gl_program_resource via data pointer and then calculate
             * index of that uniform.
             */
            unsigned idx = RESOURCE_ATC(res)->Uniforms[i];
            struct gl_program_resource *uni =
               program_resource_find_data(shProg,
                                          &shProg->UniformStorage[idx]);
            assert(uni);
            *val++ = _mesa_program_resource_index(shProg, uni);
         }
         return RESOURCE_ATC(res)->NumUniforms;
      }
   } else if (res->Type == GL_TRANSFORM_FEEDBACK_BUFFER) {
      switch (prop) {
      case GL_BUFFER_BINDING:
         *val = RESOURCE_XFB(res)->Binding;
         return 1;
      case GL_NUM_ACTIVE_VARIABLES:
         *val = RESOURCE_XFB(res)->NumVaryings;
         return 1;
      case GL_ACTIVE_VARIABLES:
         int i = 0;
         for ( ; i < shProg->LinkedTransformFeedback.NumVarying; i++) {
            unsigned index =
               shProg->LinkedTransformFeedback.Varyings[i].BufferIndex;
            struct gl_program_resource *buf_res =
               _mesa_program_resource_find_index(shProg,
                                                 GL_TRANSFORM_FEEDBACK_BUFFER,
                                                 index);
            assert(buf_res);
            if (res == buf_res) {
               *val++ = i;
            }
         }
         return RESOURCE_XFB(res)->NumVaryings;
      }
   }
   assert(!"support for property type not implemented");

invalid_operation:
   _mesa_error(ctx, GL_INVALID_OPERATION, "%s(%s prop %s)", caller,
               _mesa_enum_to_string(res->Type),
               _mesa_enum_to_string(prop));

   return 0;
}

unsigned
_mesa_program_resource_prop(struct gl_shader_program *shProg,
                            struct gl_program_resource *res, GLuint index,
                            const GLenum prop, GLint *val, const char *caller)
{
   GET_CURRENT_CONTEXT(ctx);

#define VALIDATE_TYPE(type)\
   if (res->Type != type)\
      goto invalid_operation;

#define VALIDATE_TYPE_2(type1, type2)\
   if (res->Type != type1 && res->Type != type2)\
      goto invalid_operation;

   switch(prop) {
   case GL_NAME_LENGTH:
      switch (res->Type) {
      case GL_ATOMIC_COUNTER_BUFFER:
      case GL_TRANSFORM_FEEDBACK_BUFFER:
         goto invalid_operation;
      default:
         /* Resource name length + terminator. */
         *val = _mesa_program_resource_name_len(res) + 1;
      }
      return 1;
   case GL_TYPE:
      switch (res->Type) {
      case GL_UNIFORM:
      case GL_BUFFER_VARIABLE:
         *val = RESOURCE_UNI(res)->type->gl_type;
         return 1;
      case GL_PROGRAM_INPUT:
      case GL_PROGRAM_OUTPUT:
         *val = RESOURCE_VAR(res)->type->gl_type;
         return 1;
      case GL_TRANSFORM_FEEDBACK_VARYING:
         *val = RESOURCE_XFV(res)->Type;
         return 1;
      default:
         goto invalid_operation;
      }
   case GL_ARRAY_SIZE:
      switch (res->Type) {
      case GL_UNIFORM:
      case GL_BUFFER_VARIABLE:
      case GL_VERTEX_SUBROUTINE_UNIFORM:
      case GL_GEOMETRY_SUBROUTINE_UNIFORM:
      case GL_FRAGMENT_SUBROUTINE_UNIFORM:
      case GL_COMPUTE_SUBROUTINE_UNIFORM:
      case GL_TESS_CONTROL_SUBROUTINE_UNIFORM:
      case GL_TESS_EVALUATION_SUBROUTINE_UNIFORM:

         /* Test if a buffer variable is an array or an unsized array.
          * Unsized arrays return zero as array size.
          */
         if (RESOURCE_UNI(res)->is_shader_storage &&
             RESOURCE_UNI(res)->array_stride > 0)
            *val = RESOURCE_UNI(res)->array_elements;
         else
            *val = MAX2(RESOURCE_UNI(res)->array_elements, 1);
         return 1;
      case GL_PROGRAM_INPUT:
      case GL_PROGRAM_OUTPUT:
         *val = MAX2(_mesa_program_resource_array_size(res), 1);
         return 1;
      case GL_TRANSFORM_FEEDBACK_VARYING:
         *val = RESOURCE_XFV(res)->Size;
         return 1;
      default:
         goto invalid_operation;
      }
   case GL_OFFSET:
      switch (res->Type) {
      case GL_UNIFORM:
      case GL_BUFFER_VARIABLE:
         *val = RESOURCE_UNI(res)->offset;
         return 1;
      case GL_TRANSFORM_FEEDBACK_VARYING:
         *val = RESOURCE_XFV(res)->Offset;
         return 1;
      default:
         goto invalid_operation;
      }
   case GL_BLOCK_INDEX:
      VALIDATE_TYPE_2(GL_UNIFORM, GL_BUFFER_VARIABLE);
      *val = RESOURCE_UNI(res)->block_index;
      return 1;
   case GL_ARRAY_STRIDE:
      VALIDATE_TYPE_2(GL_UNIFORM, GL_BUFFER_VARIABLE);
      *val = RESOURCE_UNI(res)->array_stride;
      return 1;
   case GL_MATRIX_STRIDE:
      VALIDATE_TYPE_2(GL_UNIFORM, GL_BUFFER_VARIABLE);
      *val = RESOURCE_UNI(res)->matrix_stride;
      return 1;
   case GL_IS_ROW_MAJOR:
      VALIDATE_TYPE_2(GL_UNIFORM, GL_BUFFER_VARIABLE);
      *val = RESOURCE_UNI(res)->row_major;
      return 1;
   case GL_ATOMIC_COUNTER_BUFFER_INDEX:
      VALIDATE_TYPE(GL_UNIFORM);
      *val = RESOURCE_UNI(res)->atomic_buffer_index;
      return 1;
   case GL_BUFFER_BINDING:
   case GL_BUFFER_DATA_SIZE:
   case GL_NUM_ACTIVE_VARIABLES:
   case GL_ACTIVE_VARIABLES:
      return get_buffer_property(shProg, res, prop, val, caller);
   case GL_REFERENCED_BY_COMPUTE_SHADER:
      if (!_mesa_has_compute_shaders(ctx))
         goto invalid_enum;
      /* fallthrough */
   case GL_REFERENCED_BY_VERTEX_SHADER:
   case GL_REFERENCED_BY_TESS_CONTROL_SHADER:
   case GL_REFERENCED_BY_TESS_EVALUATION_SHADER:
   case GL_REFERENCED_BY_GEOMETRY_SHADER:
   case GL_REFERENCED_BY_FRAGMENT_SHADER:
      switch (res->Type) {
      case GL_UNIFORM:
      case GL_PROGRAM_INPUT:
      case GL_PROGRAM_OUTPUT:
      case GL_UNIFORM_BLOCK:
      case GL_BUFFER_VARIABLE:
      case GL_SHADER_STORAGE_BLOCK:
      case GL_ATOMIC_COUNTER_BUFFER:
         *val = is_resource_referenced(shProg, res, index,
                                       stage_from_enum(prop));
         return 1;
      default:
         goto invalid_operation;
      }
   case GL_LOCATION:
      switch (res->Type) {
      case GL_UNIFORM:
      case GL_VERTEX_SUBROUTINE_UNIFORM:
      case GL_GEOMETRY_SUBROUTINE_UNIFORM:
      case GL_FRAGMENT_SUBROUTINE_UNIFORM:
      case GL_COMPUTE_SUBROUTINE_UNIFORM:
      case GL_TESS_CONTROL_SUBROUTINE_UNIFORM:
      case GL_TESS_EVALUATION_SUBROUTINE_UNIFORM:
      case GL_PROGRAM_INPUT:
      case GL_PROGRAM_OUTPUT:
         *val = program_resource_location(res, 0);
         return 1;
      default:
         goto invalid_operation;
      }
   case GL_LOCATION_COMPONENT:
      switch (res->Type) {
      case GL_PROGRAM_INPUT:
      case GL_PROGRAM_OUTPUT:
         *val = RESOURCE_VAR(res)->component;
         return 1;
      default:
         goto invalid_operation;
      }
   case GL_LOCATION_INDEX: {
      int tmp;
      if (res->Type != GL_PROGRAM_OUTPUT)
         goto invalid_operation;
      tmp = program_resource_location(res, 0);
      if (tmp == -1)
         *val = -1;
      else
         *val = _mesa_program_resource_location_index(shProg, res->Type,
                                                      RESOURCE_VAR(res)->name);
      return 1;
   }
   case GL_NUM_COMPATIBLE_SUBROUTINES:
      if (res->Type != GL_VERTEX_SUBROUTINE_UNIFORM &&
          res->Type != GL_FRAGMENT_SUBROUTINE_UNIFORM &&
          res->Type != GL_GEOMETRY_SUBROUTINE_UNIFORM &&
          res->Type != GL_COMPUTE_SUBROUTINE_UNIFORM &&
          res->Type != GL_TESS_CONTROL_SUBROUTINE_UNIFORM &&
          res->Type != GL_TESS_EVALUATION_SUBROUTINE_UNIFORM)
         goto invalid_operation;
      *val = RESOURCE_UNI(res)->num_compatible_subroutines;
      return 1;
   case GL_COMPATIBLE_SUBROUTINES: {
      const struct gl_uniform_storage *uni;
      struct gl_linked_shader *sh;
      unsigned count, i;
      int j;

      if (res->Type != GL_VERTEX_SUBROUTINE_UNIFORM &&
          res->Type != GL_FRAGMENT_SUBROUTINE_UNIFORM &&
          res->Type != GL_GEOMETRY_SUBROUTINE_UNIFORM &&
          res->Type != GL_COMPUTE_SUBROUTINE_UNIFORM &&
          res->Type != GL_TESS_CONTROL_SUBROUTINE_UNIFORM &&
          res->Type != GL_TESS_EVALUATION_SUBROUTINE_UNIFORM)
         goto invalid_operation;
      uni = RESOURCE_UNI(res);

      sh = shProg->_LinkedShaders[_mesa_shader_stage_from_subroutine_uniform(res->Type)];
      count = 0;
      for (i = 0; i < sh->NumSubroutineFunctions; i++) {
         struct gl_subroutine_function *fn = &sh->SubroutineFunctions[i];
         for (j = 0; j < fn->num_compat_types; j++) {
            if (fn->types[j] == uni->type) {
               val[count++] = i;
               break;
            }
         }
      }
      return count;
   }

   case GL_TOP_LEVEL_ARRAY_SIZE:
      VALIDATE_TYPE(GL_BUFFER_VARIABLE);
      *val = RESOURCE_UNI(res)->top_level_array_size;
      return 1;

   case GL_TOP_LEVEL_ARRAY_STRIDE:
      VALIDATE_TYPE(GL_BUFFER_VARIABLE);
      *val = RESOURCE_UNI(res)->top_level_array_stride;
      return 1;

   /* GL_ARB_tessellation_shader */
   case GL_IS_PER_PATCH:
      switch (res->Type) {
      case GL_PROGRAM_INPUT:
      case GL_PROGRAM_OUTPUT:
         *val = RESOURCE_VAR(res)->patch;
         return 1;
      default:
         goto invalid_operation;
      }

   case GL_TRANSFORM_FEEDBACK_BUFFER_INDEX:
      VALIDATE_TYPE(GL_TRANSFORM_FEEDBACK_VARYING);
      *val = RESOURCE_XFV(res)->BufferIndex;
      return 1;
   case GL_TRANSFORM_FEEDBACK_BUFFER_STRIDE:
      VALIDATE_TYPE(GL_TRANSFORM_FEEDBACK_BUFFER);
      *val = RESOURCE_XFB(res)->Stride * 4;
      return 1;

   default:
      goto invalid_enum;
   }

#undef VALIDATE_TYPE
#undef VALIDATE_TYPE_2

invalid_enum:
   _mesa_error(ctx, GL_INVALID_ENUM, "%s(%s prop %s)", caller,
               _mesa_enum_to_string(res->Type),
               _mesa_enum_to_string(prop));
   return 0;

invalid_operation:
   _mesa_error(ctx, GL_INVALID_OPERATION, "%s(%s prop %s)", caller,
               _mesa_enum_to_string(res->Type),
               _mesa_enum_to_string(prop));
   return 0;
}

extern void
_mesa_get_program_resourceiv(struct gl_shader_program *shProg,
                             GLenum programInterface, GLuint index, GLsizei propCount,
                             const GLenum *props, GLsizei bufSize,
                             GLsizei *length, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   GLint *val = (GLint *) params;
   const GLenum *prop = props;
   GLsizei amount = 0;

   struct gl_program_resource *res =
      _mesa_program_resource_find_index(shProg, programInterface, index);

   /* No such resource found or bufSize negative. */
   if (!res || bufSize < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glGetProgramResourceiv(%s index %d bufSize %d)",
                  _mesa_enum_to_string(programInterface), index, bufSize);
      return;
   }

   /* Write propCount values until error occurs or bufSize reached. */
   for (int i = 0; i < propCount && i < bufSize; i++, val++, prop++) {
      int props_written =
         _mesa_program_resource_prop(shProg, res, index, *prop, val,
                                     "glGetProgramResourceiv");

      /* Error happened. */
      if (props_written == 0)
         return;

      amount += props_written;
   }

   /* If <length> is not NULL, the actual number of integer values
    * written to <params> will be written to <length>.
    */
   if (length)
      *length = amount;
}

static bool
validate_io(struct gl_shader_program *producer,
            struct gl_shader_program *consumer,
            gl_shader_stage producer_stage,
            gl_shader_stage consumer_stage)
{
   if (producer == consumer)
      return true;

   const bool nonarray_stage_to_array_stage =
      producer_stage != MESA_SHADER_TESS_CTRL &&
      (consumer_stage == MESA_SHADER_GEOMETRY ||
       consumer_stage == MESA_SHADER_TESS_CTRL ||
       consumer_stage == MESA_SHADER_TESS_EVAL);

   bool valid = true;

   void *name_buffer = NULL;
   size_t name_buffer_size = 0;

   gl_shader_variable const **outputs =
      (gl_shader_variable const **) calloc(producer->NumProgramResourceList,
                                           sizeof(gl_shader_variable *));
   if (outputs == NULL)
      return false;

   /* Section 7.4.1 (Shader Interface Matching) of the OpenGL ES 3.1 spec
    * says:
    *
    *    At an interface between program objects, the set of inputs and
    *    outputs are considered to match exactly if and only if:
    *
    *    - Every declared input variable has a matching output, as described
    *      above.
    *    - There are no user-defined output variables declared without a
    *      matching input variable declaration.
    *
    * Every input has an output, and every output has an input.  Scan the list
    * of producer resources once, and generate the list of outputs.  As inputs
    * and outputs are matched, remove the matched outputs from the set.  At
    * the end, the set must be empty.  If the set is not empty, then there is
    * some output that did not have an input.
    */
   unsigned num_outputs = 0;
   for (unsigned i = 0; i < producer->NumProgramResourceList; i++) {
      struct gl_program_resource *res = &producer->ProgramResourceList[i];

      if (res->Type != GL_PROGRAM_OUTPUT)
         continue;

      gl_shader_variable const *const var = RESOURCE_VAR(res);

      /* Section 7.4.1 (Shader Interface Matching) of the OpenGL ES 3.1 spec
       * says:
       *
       *    Built-in inputs or outputs do not affect interface matching.
       */
      if (is_gl_identifier(var->name))
         continue;

      outputs[num_outputs++] = var;
   }

   unsigned match_index = 0;
   for (unsigned i = 0; i < consumer->NumProgramResourceList; i++) {
      struct gl_program_resource *res = &consumer->ProgramResourceList[i];

      if (res->Type != GL_PROGRAM_INPUT)
         continue;

      gl_shader_variable const *const consumer_var = RESOURCE_VAR(res);
      gl_shader_variable const *producer_var = NULL;

      if (is_gl_identifier(consumer_var->name))
         continue;

      /* Inputs with explicit locations match other outputs with explicit
       * locations by location instead of by name.
       */
      if (consumer_var->explicit_location) {
         for (unsigned j = 0; j < num_outputs; j++) {
            const gl_shader_variable *const var = outputs[j];

            if (var->explicit_location &&
                consumer_var->location == var->location) {
               producer_var = var;
               match_index = j;
               break;
            }
         }
      } else {
         char *consumer_name = consumer_var->name;

         if (nonarray_stage_to_array_stage &&
             consumer_var->interface_type != NULL &&
             consumer_var->interface_type->is_array() &&
             !is_gl_identifier(consumer_var->name)) {
            const size_t name_len = strlen(consumer_var->name);

            if (name_len >= name_buffer_size) {
               free(name_buffer);

               name_buffer_size = name_len + 1;
               name_buffer = malloc(name_buffer_size);
               if (name_buffer == NULL) {
                  valid = false;
                  goto out;
               }
            }

            consumer_name = (char *) name_buffer;

            char *s = strchr(consumer_var->name, '[');
            if (s == NULL) {
               valid = false;
               goto out;
            }

            char *t = strchr(s, ']');
            if (t == NULL) {
               valid = false;
               goto out;
            }

            assert(t[1] == '.' || t[1] == '[');

            const ptrdiff_t base_name_len = s - consumer_var->name;

            memcpy(consumer_name, consumer_var->name, base_name_len);
            strcpy(consumer_name + base_name_len, t + 1);
         }

         for (unsigned j = 0; j < num_outputs; j++) {
            const gl_shader_variable *const var = outputs[j];

            if (!var->explicit_location &&
                strcmp(consumer_name, var->name) == 0) {
               producer_var = var;
               match_index = j;
               break;
            }
         }
      }

      /* Section 7.4.1 (Shader Interface Matching) of the OpenGL ES 3.1 spec
       * says:
       *
       *    - An output variable is considered to match an input variable in
       *      the subsequent shader if:
       *
       *      - the two variables match in name, type, and qualification; or
       *
       *      - the two variables are declared with the same location
       *        qualifier and match in type and qualification.
       */
      if (producer_var == NULL) {
         valid = false;
         goto out;
      }

      /* An output cannot match more than one input, so remove the output from
       * the set of possible outputs.
       */
      outputs[match_index] = NULL;
      num_outputs--;
      if (match_index < num_outputs)
         outputs[match_index] = outputs[num_outputs];

      /* Section 9.2.2 (Separable Programs) of the GLSL ES spec says:
       *
       *    Qualifier Class|  Qualifier  |in/out
       *    ---------------+-------------+------
       *    Storage        |     in      |
       *                   |     out     |  N/A
       *                   |   uniform   |
       *    ---------------+-------------+------
       *    Auxiliary      |   centroid  |   No
       *    ---------------+-------------+------
       *                   |   location  |  Yes
       *                   | Block layout|  N/A
       *                   |   binding   |  N/A
       *                   |   offset    |  N/A
       *                   |   format    |  N/A
       *    ---------------+-------------+------
       *    Interpolation  |   smooth    |
       *                   |    flat     |  Yes
       *    ---------------+-------------+------
       *                   |    lowp     |
       *    Precision      |   mediump   |  Yes
       *                   |    highp    |
       *    ---------------+-------------+------
       *    Variance       |  invariant  |   No
       *    ---------------+-------------+------
       *    Memory         |     all     |  N/A
       *
       * Note that location mismatches are detected by the loops above that
       * find the producer variable that goes with the consumer variable.
       */
      if (nonarray_stage_to_array_stage) {
         if (!consumer_var->type->is_array() ||
             consumer_var->type->fields.array != producer_var->type) {
            valid = false;
            goto out;
         }

         if (consumer_var->interface_type != NULL) {
            if (!consumer_var->interface_type->is_array() ||
                consumer_var->interface_type->fields.array != producer_var->interface_type) {
               valid = false;
               goto out;
            }
         } else if (producer_var->interface_type != NULL) {
            valid = false;
            goto out;
         }
      } else {
         if (producer_var->type != consumer_var->type) {
            valid = false;
            goto out;
         }

         if (producer_var->interface_type != consumer_var->interface_type) {
            valid = false;
            goto out;
         }
      }

      if (producer_var->interpolation != consumer_var->interpolation) {
         valid = false;
         goto out;
      }

      if (producer_var->precision != consumer_var->precision) {
         valid = false;
         goto out;
      }

      if (producer_var->outermost_struct_type != consumer_var->outermost_struct_type) {
         valid = false;
         goto out;
      }
   }

 out:
   free(name_buffer);
   free(outputs);
   return valid && num_outputs == 0;
}

/**
 * Validate inputs against outputs in a program pipeline.
 */
extern "C" bool
_mesa_validate_pipeline_io(struct gl_pipeline_object *pipeline)
{
   struct gl_shader_program **shProg =
      (struct gl_shader_program **) pipeline->CurrentProgram;

   /* Find first active stage in pipeline. */
   unsigned idx, prev = 0;
   for (idx = 0; idx < ARRAY_SIZE(pipeline->CurrentProgram); idx++) {
      if (shProg[idx]) {
         prev = idx;
         break;
      }
   }

   for (idx = prev + 1; idx < ARRAY_SIZE(pipeline->CurrentProgram); idx++) {
      if (shProg[idx]) {
         /* Pipeline might include both non-compute and a compute program, do
          * not attempt to validate varyings between non-compute and compute
          * stage.
          */
         if (shProg[idx]->_LinkedShaders[idx]->Stage == MESA_SHADER_COMPUTE)
            break;

         if (!validate_io(shProg[prev], shProg[idx],
                          shProg[prev]->_LinkedShaders[prev]->Stage,
                          shProg[idx]->_LinkedShaders[idx]->Stage))
            return false;

         prev = idx;
      }
   }
   return true;
}
