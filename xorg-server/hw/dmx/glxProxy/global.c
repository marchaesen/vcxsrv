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

#include "glxserver.h"

/*
** The last context used by the server.  It is the context that is current
** from the server's perspective.
*/
__GLXcontext *__glXLastContext;

/*
** X resources.
*/
RESTYPE __glXContextRes;
RESTYPE __glXClientRes;
RESTYPE __glXPixmapRes;
RESTYPE __glXWindowRes;
RESTYPE __glXPbufferRes;

/*
** Error codes with the extension error base already added in.
*/
int __glXerrorBase;
int __glXBadContext, __glXBadContextState, __glXBadDrawable, __glXBadPixmap;
int __glXBadContextTag, __glXBadCurrentWindow;
int __glXBadRenderRequest, __glXBadLargeRequest;
int __glXUnsupportedPrivateRequest;
int __glXBadFBConfig, __glXBadPbuffer;

/*
** Reply for most singles.
*/
xGLXSingleReply __glXReply;

/*
** A set of state for each client.  The 0th one is unused because client
** indices start at 1, not 0.
*/
__GLXclientState *__glXClients[MAXCLIENTS + 1];

int __glXVersionMajor;
int __glXVersionMinor;
