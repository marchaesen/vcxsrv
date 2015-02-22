/*
 * Copyright 2011 Apple Inc.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __compsize_h__
#define __compsize_h__

extern GLint __glColorTableParameterfv_size(GLenum pname);
extern GLint __glColorTableParameteriv_size(GLenum pname);
extern GLint __glConvolutionParameterfv_size(GLenum pname);
extern GLint __glConvolutionParameteriv_size(GLenum pname);
extern GLint __glFogfv_size(GLenum pname);
extern GLint __glFogiv_size(GLenum pname);
extern GLint __glLightModelfv_size(GLenum pname);
extern GLint __glLightModeliv_size(GLenum pname);
extern GLint __glLightfv_size(GLenum pname);
extern GLint __glLightiv_size(GLenum pname);
extern GLint __glMaterialfv_size(GLenum pname);
extern GLint __glMaterialiv_size(GLenum pname);
extern GLint __glTexEnvfv_size(GLenum e);
extern GLint __glTexEnviv_size(GLenum e);
extern GLint __glTexGendv_size(GLenum e);
extern GLint __glTexGenfv_size(GLenum e);
extern GLint __glTexGeniv_size(GLenum e);
extern GLint __glTexParameterfv_size(GLenum e);
extern GLint __glTexParameteriv_size(GLenum e);

extern GLint __glCallLists_size(GLsizei n, GLenum type);
extern GLint __glDrawPixels_size(GLenum format, GLenum type, GLsizei w, GLsizei h);
extern GLint __glBitmap_size(GLsizei w, GLsizei h);
extern GLint __glTexImage1D_size(GLenum format, GLenum type, GLsizei w);
extern GLint __glTexImage2D_size(GLenum format, GLenum type, GLsizei w, GLsizei h);
extern GLint __glTexImage3D_size(GLenum format, GLenum type, GLsizei w, GLsizei h, GLsizei d);

#endif                          /* !__compsize_h__ */
