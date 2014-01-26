/*
 * Mesa 3-D graphics library
 * Version:  7.1
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
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


/**
 * \mainpage Mesa GL API Module
 *
 * \section GLAPIIntroduction Introduction
 *
 * The Mesa GL API module is responsible for dispatching all the
 * gl*() functions.  All GL functions are dispatched by jumping through
 * the current dispatch table (basically a struct full of function
 * pointers.)
 *
 * A per-thread current dispatch table and per-thread current context
 * pointer are managed by this module too.
 *
 * This module is intended to be non-Mesa-specific so it can be used
 * with the X/DRI libGL also.
 */

#ifndef _GLAPI_H
#define _GLAPI_H

#define GL_GLEXT_PROTOTYPES

#if GLAMOR_GLES2
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#else
#include <GL/gl.h>
#include "GL/glext.h"
#endif

/* Is this needed?  It is incomplete anyway. */
#ifdef USE_MGL_NAMESPACE
#define _glapi_set_dispatch _mglapi_set_dispatch
#define _glapi_get_dispatch _mglapi_get_dispatch
#define _glapi_set_context _mglapi_set_context
#define _glapi_get_context _mglapi_get_context
#define _glapi_Dispatch _mglapi_Dispatch
#define _glapi_Context _mglapi_Context
#endif

typedef void (*_glapi_proc)(void);
struct _glapi_table;


#if defined (GLX_USE_TLS)

extern __thread struct _glapi_table * _glapi_tls_Dispatch
    __attribute__((tls_model("initial-exec")));

extern __thread void * _glapi_tls_Context
    __attribute__((tls_model("initial-exec")));

extern const struct _glapi_table *_glapi_Dispatch;
extern const void *_glapi_Context;

# define GET_DISPATCH() _glapi_tls_Dispatch
# define GET_CURRENT_CONTEXT(C)  C = (typeof(C)) _glapi_tls_Context
# define SET_CURRENT_CONTEXT(C)  _glapi_tls_Context = (void*)C

#else

extern struct _glapi_table *_glapi_Dispatch;
extern void *_glapi_Context;

# ifdef THREADS

#  define GET_DISPATCH() \
     (likely(_glapi_Dispatch) ? _glapi_Dispatch : _glapi_get_dispatch())

#  define GET_CURRENT_CONTEXT(C)  C = (typeof(C)) \
     (likely(_glapi_Context) ? _glapi_Context : _glapi_get_context())


# define SET_CURRENT_CONTEXT(C) do { if (likely(_glapi_Context))   \
					_glapi_Context = (void*)C; \
				     else \
					_glapi_set_context(C); } while(0)

# else

#  define GET_DISPATCH() _glapi_Dispatch
#  define GET_CURRENT_CONTEXT(C)  C = (typeof(C)) _glapi_Context
# define SET_CURRENT_CONTEXT(C)  _glapi_Context = (void*)C

# endif

#endif /* defined (GLX_USE_TLS) */


extern void
_glapi_set_context(void *context);

extern void *
_glapi_get_context(void);

#endif
