/*
 * Copyright © 2009 Intel Corporation
 * Copyright © 1998 Keith Packard
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
 *
 * Authors:
 *    Zhigang Gong <zhigang.gong@gmail.com>
 *
 */

#include "glamor_priv.h"
#include <dlfcn.h>

#define INIT_FUNC(dst,func_name,get)			\
  dst->func_name = get(#func_name);			\
  if (dst->func_name == NULL) {				\
    dst->func_name = (void *)dlsym(NULL, #func_name);	\
    if (dst->func_name == NULL) {			\
      ErrorF("Failed to get function %s\n", #func_name);\
      goto fail;					\
    }							\
  }							\

_X_EXPORT Bool
glamor_gl_dispatch_init_impl(struct glamor_gl_dispatch *dispatch,
                             int gl_version,
                             void *(*get_proc_address) (const char *))
{
#ifndef GLAMOR_GLES2
    INIT_FUNC(dispatch, glMatrixMode, get_proc_address);
    INIT_FUNC(dispatch, glLoadIdentity, get_proc_address);
    INIT_FUNC(dispatch, glRasterPos2i, get_proc_address);
    INIT_FUNC(dispatch, glDrawPixels, get_proc_address);
    INIT_FUNC(dispatch, glLogicOp, get_proc_address);
    INIT_FUNC(dispatch, glMapBuffer, get_proc_address);
    INIT_FUNC(dispatch, glMapBufferRange, get_proc_address);
    INIT_FUNC(dispatch, glUnmapBuffer, get_proc_address);
    INIT_FUNC(dispatch, glBlitFramebuffer, get_proc_address);
    INIT_FUNC(dispatch, glDrawRangeElements, get_proc_address);
#endif
    INIT_FUNC(dispatch, glViewport, get_proc_address);
    INIT_FUNC(dispatch, glDrawArrays, get_proc_address);
    INIT_FUNC(dispatch, glDrawElements, get_proc_address);
    INIT_FUNC(dispatch, glReadPixels, get_proc_address);
    INIT_FUNC(dispatch, glPixelStorei, get_proc_address);
    INIT_FUNC(dispatch, glTexParameteri, get_proc_address);
    INIT_FUNC(dispatch, glTexImage2D, get_proc_address);
    INIT_FUNC(dispatch, glGenTextures, get_proc_address);
    INIT_FUNC(dispatch, glDeleteTextures, get_proc_address);
    INIT_FUNC(dispatch, glBindTexture, get_proc_address);
    INIT_FUNC(dispatch, glTexSubImage2D, get_proc_address);
    INIT_FUNC(dispatch, glFlush, get_proc_address);
    INIT_FUNC(dispatch, glFinish, get_proc_address);
    INIT_FUNC(dispatch, glGetIntegerv, get_proc_address);
    INIT_FUNC(dispatch, glGetString, get_proc_address);
    INIT_FUNC(dispatch, glScissor, get_proc_address);
    INIT_FUNC(dispatch, glEnable, get_proc_address);
    INIT_FUNC(dispatch, glDisable, get_proc_address);
    INIT_FUNC(dispatch, glBlendFunc, get_proc_address);
    INIT_FUNC(dispatch, glActiveTexture, get_proc_address);
    INIT_FUNC(dispatch, glGenBuffers, get_proc_address);
    INIT_FUNC(dispatch, glBufferData, get_proc_address);
    INIT_FUNC(dispatch, glBindBuffer, get_proc_address);
    INIT_FUNC(dispatch, glDeleteBuffers, get_proc_address);
    INIT_FUNC(dispatch, glFramebufferTexture2D, get_proc_address);
    INIT_FUNC(dispatch, glBindFramebuffer, get_proc_address);
    INIT_FUNC(dispatch, glDeleteFramebuffers, get_proc_address);
    INIT_FUNC(dispatch, glGenFramebuffers, get_proc_address);
    INIT_FUNC(dispatch, glCheckFramebufferStatus, get_proc_address);
    INIT_FUNC(dispatch, glVertexAttribPointer, get_proc_address);
    INIT_FUNC(dispatch, glDisableVertexAttribArray, get_proc_address);
    INIT_FUNC(dispatch, glEnableVertexAttribArray, get_proc_address);
    INIT_FUNC(dispatch, glBindAttribLocation, get_proc_address);
    INIT_FUNC(dispatch, glLinkProgram, get_proc_address);
    INIT_FUNC(dispatch, glShaderSource, get_proc_address);

    INIT_FUNC(dispatch, glUseProgram, get_proc_address);
    INIT_FUNC(dispatch, glUniform1i, get_proc_address);
    INIT_FUNC(dispatch, glUniform1f, get_proc_address);
    INIT_FUNC(dispatch, glUniform4f, get_proc_address);
    INIT_FUNC(dispatch, glUniform4fv, get_proc_address);
    INIT_FUNC(dispatch, glUniform1fv, get_proc_address);
    INIT_FUNC(dispatch, glUniform2fv, get_proc_address);
    INIT_FUNC(dispatch, glUniformMatrix3fv, get_proc_address);
    INIT_FUNC(dispatch, glCreateProgram, get_proc_address);
    INIT_FUNC(dispatch, glDeleteProgram, get_proc_address);
    INIT_FUNC(dispatch, glCreateShader, get_proc_address);
    INIT_FUNC(dispatch, glCompileShader, get_proc_address);
    INIT_FUNC(dispatch, glAttachShader, get_proc_address);
    INIT_FUNC(dispatch, glDeleteShader, get_proc_address);
    INIT_FUNC(dispatch, glGetShaderiv, get_proc_address);
    INIT_FUNC(dispatch, glGetShaderInfoLog, get_proc_address);
    INIT_FUNC(dispatch, glGetProgramiv, get_proc_address);
    INIT_FUNC(dispatch, glGetProgramInfoLog, get_proc_address);
    INIT_FUNC(dispatch, glGetUniformLocation, get_proc_address);

    return TRUE;
 fail:
    return FALSE;
}
