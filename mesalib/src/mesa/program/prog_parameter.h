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
 * \file prog_parameter.c
 * Program parameter lists and functions.
 * \author Brian Paul
 */

#ifndef PROG_PARAMETER_H
#define PROG_PARAMETER_H

#include "main/mtypes.h"
#include "prog_statevars.h"

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * Actual data for constant values of parameters.
 */
typedef union gl_constant_value
{
   GLfloat f;
   GLint b;
   GLint i;
   GLuint u;
} gl_constant_value;


/**
 * Program parameter.
 * Used by shaders/programs for uniforms, constants, varying vars, etc.
 */
struct gl_program_parameter
{
   const char *Name;        /**< Null-terminated string */
   gl_register_file Type:16;  /**< PROGRAM_CONSTANT or STATE_VAR */
   GLenum16 DataType;         /**< GL_FLOAT, GL_FLOAT_VEC2, etc */
   /**
    * Number of components (1..4), or more.
    * If the number of components is greater than 4,
    * this parameter is part of a larger uniform like a GLSL matrix or array.
    * The next program parameter's Size will be Size-4 of this parameter.
    */
   GLushort Size;
   /**
    * A sequence of STATE_* tokens and integers to identify GL state.
    */
   gl_state_index16 StateIndexes[STATE_LENGTH];
};


/**
 * List of gl_program_parameter instances.
 */
struct gl_program_parameter_list
{
   GLuint Size;           /**< allocated size of Parameters, ParameterValues */
   GLuint NumParameters;  /**< number of parameters in arrays */
   struct gl_program_parameter *Parameters; /**< Array [Size] */
   gl_constant_value (*ParameterValues)[4]; /**< Array [Size] of constant[4] */
   GLbitfield StateFlags; /**< _NEW_* flags indicating which state changes
                               might invalidate ParameterValues[] */
};


extern struct gl_program_parameter_list *
_mesa_new_parameter_list(void);

extern struct gl_program_parameter_list *
_mesa_new_parameter_list_sized(unsigned size);

extern void
_mesa_free_parameter_list(struct gl_program_parameter_list *paramList);

extern void
_mesa_reserve_parameter_storage(struct gl_program_parameter_list *paramList,
                                unsigned reserve_slots);

extern GLint
_mesa_add_parameter(struct gl_program_parameter_list *paramList,
                    gl_register_file type, const char *name,
                    GLuint size, GLenum datatype,
                    const gl_constant_value *values,
                    const gl_state_index16 state[STATE_LENGTH]);

extern GLint
_mesa_add_typed_unnamed_constant(struct gl_program_parameter_list *paramList,
                           const gl_constant_value values[4], GLuint size,
                           GLenum datatype, GLuint *swizzleOut);

static inline GLint
_mesa_add_unnamed_constant(struct gl_program_parameter_list *paramList,
                           const gl_constant_value values[4], GLuint size,
                           GLuint *swizzleOut)
{
   return _mesa_add_typed_unnamed_constant(paramList, values, size, GL_NONE,
                                           swizzleOut);
}

extern GLint
_mesa_add_state_reference(struct gl_program_parameter_list *paramList,
                          const gl_state_index16 stateTokens[]);


static inline GLint
_mesa_lookup_parameter_index(const struct gl_program_parameter_list *paramList,
                             const char *name)
{
   if (!paramList)
      return -1;

   /* name must be null-terminated */
   for (GLint i = 0; i < (GLint) paramList->NumParameters; i++) {
      if (paramList->Parameters[i].Name &&
         strcmp(paramList->Parameters[i].Name, name) == 0)
         return i;
   }

   return -1;
}

#ifdef __cplusplus
}
#endif

#endif /* PROG_PARAMETER_H */
