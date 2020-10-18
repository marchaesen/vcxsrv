/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2008  Brian Paul   All Rights Reserved.
 * Copyright (C) 2009  VMware, Inc.   All Rights Reserved.
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


#ifndef MATRIX_H
#define MATRIX_H


#include "glheader.h"

struct gl_context;
struct gl_matrix_stack;

extern void
_mesa_load_identity_matrix(struct gl_context *ctx, struct gl_matrix_stack *s);

extern void
_mesa_load_matrix(struct gl_context *ctx, struct gl_matrix_stack *s,
                  const GLfloat *m);

extern void GLAPIENTRY
_mesa_Frustum( GLdouble left, GLdouble right,
               GLdouble bottom, GLdouble top,
               GLdouble nearval, GLdouble farval );

extern void GLAPIENTRY
_mesa_Ortho( GLdouble left, GLdouble right,
             GLdouble bottom, GLdouble top,
             GLdouble nearval, GLdouble farval );

extern void GLAPIENTRY
_mesa_PushMatrix( void );

extern void GLAPIENTRY
_mesa_PopMatrix( void );

extern void GLAPIENTRY
_mesa_LoadIdentity( void );

extern void GLAPIENTRY
_mesa_LoadMatrixf( const GLfloat *m );

extern void GLAPIENTRY
_mesa_LoadMatrixd( const GLdouble *m );

extern void GLAPIENTRY
_mesa_MatrixMode( GLenum mode );

extern void GLAPIENTRY
_mesa_MultMatrixf( const GLfloat *m );

extern void GLAPIENTRY
_mesa_MultMatrixd( const GLdouble *m );

extern void GLAPIENTRY
_mesa_Rotatef( GLfloat angle, GLfloat x, GLfloat y, GLfloat z );

extern void GLAPIENTRY
_mesa_Rotated( GLdouble angle, GLdouble x, GLdouble y, GLdouble z );

extern void GLAPIENTRY
_mesa_Scalef( GLfloat x, GLfloat y, GLfloat z );

extern void GLAPIENTRY
_mesa_Scaled( GLdouble x, GLdouble y, GLdouble z );

extern void GLAPIENTRY
_mesa_Translatef( GLfloat x, GLfloat y, GLfloat z );

extern void GLAPIENTRY
_mesa_Translated( GLdouble x, GLdouble y, GLdouble z );

extern void GLAPIENTRY
_mesa_LoadTransposeMatrixf( const GLfloat *m );

extern void GLAPIENTRY
_mesa_LoadTransposeMatrixd( const GLdouble *m );

extern void GLAPIENTRY
_mesa_MultTransposeMatrixf( const GLfloat *m );

extern void GLAPIENTRY
_mesa_MultTransposeMatrixd( const GLdouble *m );

extern void GLAPIENTRY
_mesa_MatrixLoadfEXT( GLenum matrixMode, const GLfloat *m );

extern void GLAPIENTRY
_mesa_MatrixLoaddEXT( GLenum matrixMode, const GLdouble *m );

extern void GLAPIENTRY
_mesa_MatrixMultfEXT( GLenum matrixMode, const GLfloat *m );

extern void GLAPIENTRY
_mesa_MatrixMultdEXT( GLenum matrixMode, const GLdouble *m );

extern void GLAPIENTRY
_mesa_MatrixLoadIdentityEXT( GLenum matrixMode );

extern void GLAPIENTRY
_mesa_MatrixRotatefEXT( GLenum matrixMode, GLfloat angle, GLfloat x, GLfloat y, GLfloat z );

extern void GLAPIENTRY
_mesa_MatrixRotatedEXT( GLenum matrixMode, GLdouble angle, GLdouble x, GLdouble y, GLdouble z );

extern void GLAPIENTRY
_mesa_MatrixScalefEXT( GLenum matrixMode, GLfloat x, GLfloat y, GLfloat z );

extern void GLAPIENTRY
_mesa_MatrixScaledEXT( GLenum matrixMode, GLdouble x, GLdouble y, GLdouble z );

extern void GLAPIENTRY
_mesa_MatrixTranslatefEXT( GLenum matrixMode, GLfloat x, GLfloat y, GLfloat z );

extern void GLAPIENTRY
_mesa_MatrixTranslatedEXT( GLenum matrixMode, GLdouble x, GLdouble y, GLdouble z );

extern void GLAPIENTRY
_mesa_MatrixOrthoEXT( GLenum matrixMode, GLdouble l, GLdouble r, GLdouble b, GLdouble t,
      GLdouble n, GLdouble f );

extern void GLAPIENTRY
_mesa_MatrixFrustumEXT( GLenum matrixMode, GLdouble l, GLdouble r, GLdouble b, GLdouble t,
      GLdouble n, GLdouble f );

extern void GLAPIENTRY
_mesa_MatrixPushEXT( GLenum matrixMode );

extern void GLAPIENTRY
_mesa_MatrixPopEXT( GLenum matrixMode );

extern void GLAPIENTRY
_mesa_MatrixLoadTransposefEXT( GLenum matrixMode, const GLfloat* m );

extern void GLAPIENTRY
_mesa_MatrixLoadTransposedEXT( GLenum matrixMode, const GLdouble* m );

extern void GLAPIENTRY
_mesa_MatrixMultTransposefEXT( GLenum matrixMode, const GLfloat* m );

extern void GLAPIENTRY
_mesa_MatrixMultTransposedEXT( GLenum matrixMode, const GLdouble* m );

extern void 
_mesa_init_matrix( struct gl_context * ctx );

extern void 
_mesa_init_transform( struct gl_context *ctx );

extern void
_mesa_free_matrix_data( struct gl_context *ctx );

extern void 
_mesa_update_modelview_project( struct gl_context *ctx, GLuint newstate );


#endif
