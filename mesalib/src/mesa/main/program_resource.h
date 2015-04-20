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

#ifndef PROGRAM_RESOURCE_H
#define PROGRAM_RESOURCE_H

#include "glheader.h"

extern void GLAPIENTRY
_mesa_GetProgramInterfaceiv(GLuint program, GLenum programInterface,
                            GLenum pname, GLint *params);

extern GLuint GLAPIENTRY
_mesa_GetProgramResourceIndex(GLuint program, GLenum programInterface,
                              const GLchar *name);

extern void GLAPIENTRY
_mesa_GetProgramResourceName(GLuint program, GLenum programInterface,
                             GLuint index, GLsizei bufSize, GLsizei *length,
                             GLchar *name);

extern void GLAPIENTRY
_mesa_GetProgramResourceiv(GLuint program, GLenum programInterface,
                           GLuint index, GLsizei propCount,
                           const GLenum *props, GLsizei bufSize,
                           GLsizei *length, GLint *params);

extern GLint GLAPIENTRY
_mesa_GetProgramResourceLocation(GLuint program, GLenum programInterface,
                                 const GLchar *name);

extern GLint GLAPIENTRY
_mesa_GetProgramResourceLocationIndex(GLuint program, GLenum programInterface,
                                      const GLchar *name);

#endif
