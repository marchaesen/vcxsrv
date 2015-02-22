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


#ifndef TEXPARAM_H
#define TEXPARAM_H


#include "main/glheader.h"

/**
 * \name Internal functions
 */
/*@{*/

extern void
_mesa_texture_parameterf(struct gl_context *ctx,
                         struct gl_texture_object *texObj,
                         GLenum pname, GLfloat param, bool dsa);

extern void
_mesa_texture_parameterfv(struct gl_context *ctx,
                          struct gl_texture_object *texObj,
                          GLenum pname, const GLfloat *params, bool dsa);


extern void
_mesa_texture_parameteri(struct gl_context *ctx,
                         struct gl_texture_object *texObj,
                         GLenum pname, GLint param, bool dsa);

extern void
_mesa_texture_parameteriv(struct gl_context *ctx,
                          struct gl_texture_object *texObj,
                          GLenum pname, const GLint *params, bool dsa);

extern void
_mesa_texture_parameterIiv(struct gl_context *ctx,
                           struct gl_texture_object *texObj,
                           GLenum pname, const GLint *params, bool dsa);

extern void
_mesa_texture_parameterIuiv(struct gl_context *ctx,
                            struct gl_texture_object *texObj,
                            GLenum pname, const GLuint *params, bool dsa);

/*@}*/

/**
 * \name API functions
 */
/*@{*/


extern void GLAPIENTRY
_mesa_GetTexLevelParameterfv( GLenum target, GLint level,
                              GLenum pname, GLfloat *params );

extern void GLAPIENTRY
_mesa_GetTexLevelParameteriv( GLenum target, GLint level,
                              GLenum pname, GLint *params );

extern void GLAPIENTRY
_mesa_GetTextureLevelParameterfv(GLuint texture, GLint level,
                                 GLenum pname, GLfloat *params);

extern void GLAPIENTRY
_mesa_GetTextureLevelParameteriv(GLuint texture, GLint level,
                                 GLenum pname, GLint *params);


extern void GLAPIENTRY
_mesa_GetTexParameterfv( GLenum target, GLenum pname, GLfloat *params );

extern void GLAPIENTRY
_mesa_GetTexParameteriv( GLenum target, GLenum pname, GLint *params );

extern void GLAPIENTRY
_mesa_GetTexParameterIiv(GLenum target, GLenum pname, GLint *params);

extern void GLAPIENTRY
_mesa_GetTexParameterIuiv(GLenum target, GLenum pname, GLuint *params);


extern void GLAPIENTRY
_mesa_GetTextureParameterfv(GLuint texture, GLenum pname, GLfloat *params);

extern void GLAPIENTRY
_mesa_GetTextureParameteriv(GLuint texture, GLenum pname, GLint *params);

extern void GLAPIENTRY
_mesa_GetTextureParameterIiv(GLuint texture, GLenum pname, GLint *params);

extern void GLAPIENTRY
_mesa_GetTextureParameterIuiv(GLuint texture, GLenum pname, GLuint *params);


extern void GLAPIENTRY
_mesa_TexParameterfv( GLenum target, GLenum pname, const GLfloat *params );

extern void GLAPIENTRY
_mesa_TexParameterf( GLenum target, GLenum pname, GLfloat param );

extern void GLAPIENTRY
_mesa_TexParameteri( GLenum target, GLenum pname, GLint param );

extern void GLAPIENTRY
_mesa_TexParameteriv( GLenum target, GLenum pname, const GLint *params );

extern void GLAPIENTRY
_mesa_TexParameterIiv(GLenum target, GLenum pname, const GLint *params);

extern void GLAPIENTRY
_mesa_TexParameterIuiv(GLenum target, GLenum pname, const GLuint *params);

extern void GLAPIENTRY
_mesa_TextureParameterfv(GLuint texture, GLenum pname, const GLfloat *params);

extern void GLAPIENTRY
_mesa_TextureParameterf(GLuint texture, GLenum pname, GLfloat param);

extern void GLAPIENTRY
_mesa_TextureParameteri(GLuint texture, GLenum pname, GLint param);

extern void GLAPIENTRY
_mesa_TextureParameteriv(GLuint texture, GLenum pname, const GLint *params);

extern void GLAPIENTRY
_mesa_TextureParameterIiv(GLuint texture, GLenum pname, const GLint *params);

extern void GLAPIENTRY
_mesa_TextureParameterIuiv(GLuint texture, GLenum pname, const GLuint *params);

#endif /* TEXPARAM_H */
