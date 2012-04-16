#ifndef _GLX_context_h_
#define _GLX_context_h_

/*
 * SGI FREE SOFTWARE LICENSE B (Version 2.0, Sept. 18, 2008)
 * Copyright (C) 1991-2000 Silicon Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice including the dates of first publication and
 * either this permission notice or a reference to
 * http://oss.sgi.com/projects/FreeB/
 * shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * SILICON GRAPHICS, INC. BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of Silicon Graphics, Inc.
 * shall not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization from
 * Silicon Graphics, Inc.
 */

typedef struct __GLXcontextRec __GLXcontext;

#include "GL/internal/glcore.h"

struct __GLXcontextRec {
    /*
     ** list of context structs
     */
    struct __GLXcontextRec *last;
    struct __GLXcontextRec *next;

    /*
     ** Pointer to screen info data for this context.  This is set
     ** when the context is created.
     */
    ScreenPtr pScreen;
    __GLXscreenInfo *pGlxScreen;

    /*
     ** This context is created with respect to this visual.
     */
    VisualRec *pVisual;
    __GLXvisualConfig *pGlxVisual;
    __GLXFBConfig *pFBConfig;

    /*
     ** The XID of this context.
     */
    XID id;
    XID *real_ids;

    /*
     ** The XID of the shareList context.
     */
    XID share_id;

    /*
     ** Visual id.
     */
    VisualID vid;
    VisualID *real_vids;

    /*
     ** screen number.
     */
    GLint screen;

    /*
     ** Whether this context's ID still exists.
     */
    GLboolean idExists;

    /*
     ** Whether this context is current for some client.
     */
    GLboolean isCurrent;

    /*
     ** Buffers for feedback and selection.
     */
    GLfloat *feedbackBuf;
    GLint feedbackBufSize;      /* number of elements allocated */
    GLuint *selectBuf;
    GLint selectBufSize;        /* number of elements allocated */

    /*
     ** Set only if current drawable is a glx pixmap.
     */
    __GLXpixmap *pGlxPixmap;
    __GLXpixmap *pGlxReadPixmap;
    __glXWindow *pGlxWindow;
    __glXWindow *pGlxReadWindow;
    __glXPbuffer *pGlxPbuffer;
    __glXPbuffer *pGlxReadPbuffer;

};

#endif                          /* !__GLX_context_h__ */
