/*
 * File: glwrap.c
 * Purpose: Wrapper functions for Win32 OpenGL functions
 *
 * Authors: Alexander Gottwald
 *          Jon TURNEY
 *
 * Copyright (c) Jon TURNEY 2009
 * Copyright (c) Alexander Gottwald 2004
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE ABOVE LISTED COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

// define USE_OPENGL32 makes gl.h declare gl*() function prototypes with stdcall linkage,
// so our generated wrappers will correctly link with the functions in opengl32.dll
#define USE_OPENGL32

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif

#include <X11/Xwindows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <glx/glxserver.h>
#include <glx/glxext.h>
#include <glx/glapi.h>
#include <glx/dispatch.h>
#include <glwindows.h>

static unsigned int glWinIndirectProcCalls = 0;
static unsigned int glWinDirectProcCalls = 0;

void
glWinCallDelta(void)
{
    static unsigned int glWinIndirectProcCallsLast = 0;
    static unsigned int glWinDirectProcCallsLast = 0;

    if ((glWinIndirectProcCalls != glWinIndirectProcCallsLast) ||
        (glWinDirectProcCalls != glWinDirectProcCallsLast)) {
        if (glxWinDebugSettings.enableTrace) {
            ErrorF("after %d direct and %d indirect GL calls\n",
                   glWinDirectProcCalls - glWinDirectProcCallsLast,
                   glWinIndirectProcCalls - glWinIndirectProcCallsLast);
        }
        glWinDirectProcCallsLast = glWinDirectProcCalls;
        glWinIndirectProcCallsLast = glWinIndirectProcCalls;
    }
}

static PROC
glWinResolveHelper(PROC * cache, char *symbol)
{
    PROC proc = NULL;

    /* If not yet cached, call wglGetProcAddress */
    if ((*cache) == NULL) {
        proc = wglGetProcAddress(symbol);
        if (proc == NULL) {
            ErrorF("glwrap: Can't resolve \"%s\"\n", symbol);
            (*cache) = (PROC) - 1;
        }
        else {
            ErrorF("glwrap: Resolved \"%s\"\n", symbol);
            (*cache) = proc;
        }
    }
    /* Cached wglGetProcAddress failure */
    else if ((*cache) == (PROC) - 1) {
        proc = 0;
    }
    /* Cached wglGetProcAddress result */
    else {
        proc = (*cache);
    }

    return proc;
}

#define RESOLVE_RET(proctype, symbol, retval) \
    static PROC cache = NULL; \
    __stdcall proctype proc = (proctype)glWinResolveHelper(&cache, symbol); \
    if (proc == NULL) { \
        __glXErrorCallBack(0); \
        return retval; \
    } \
    glWinIndirectProcCalls++;

#define RESOLVE(proctype, symbol) RESOLVE_RET(proctype, symbol,)

#define RESOLVED_PROC(proctype) proc

/*
  Include generated cdecl wrappers for stdcall gl*() functions in opengl32.dll

  OpenGL 1.2 and upward is treated as extensions, function address must
  found using wglGetProcAddress(), but also stdcall so still need wrappers...

  Include generated dispatch table setup function
*/

#include "generated_gl_wrappers.c"

/*
  Special non-static wrapper for glGetString for debug output
*/

const GLubyte *
glGetStringWrapperNonstatic(GLenum name)
{
    return glGetString(name);
}

/*
  Special non-static wrapper for glAddSwapHintRectWIN for copySubBuffers
*/

typedef void (__stdcall * PFNGLADDSWAPHINTRECTWIN) (GLint x, GLint y,
                                                    GLsizei width,
                                                    GLsizei height);

void
glAddSwapHintRectWINWrapperNonstatic(GLint x, GLint y, GLsizei width,
                                     GLsizei height)
{
    RESOLVE(PFNGLADDSWAPHINTRECTWIN, "glAddSwapHintRectWIN");
    proc(x, y, width, height);
}
