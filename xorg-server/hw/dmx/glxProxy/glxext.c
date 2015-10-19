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

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"

#include "glxserver.h"
#include <windowstr.h>
#include <propertyst.h>
#include <os.h>
#include "g_disptab.h"
#include "glxutil.h"
#include "glxext.h"
#include "glxvisuals.h"
#include "micmap.h"
#include "glxswap.h"
#include "extinit.h"
#include "glx_extinit.h"

/*
** Forward declarations.
*/
static int __glXSwapDispatch(ClientPtr);
static int __glXDispatch(ClientPtr);

/*
** Called when the extension is reset.
*/
static void
ResetExtension(ExtensionEntry * extEntry)
{
    __glXFlushContextCache();
    __glXScreenReset();
    SwapBarrierReset();
}

/*
** Initialize the per-client context storage.
*/
static void
ResetClientState(int clientIndex)
{
    __GLXclientState *cl = __glXClients[clientIndex];
    Display **keep_be_displays;
    int i;

    free(cl->returnBuf);
    free(cl->currentContexts);
    free(cl->currentDrawables);
    free(cl->largeCmdBuf);

    for (i = 0; i < screenInfo.numScreens; i++) {
        if (cl->be_displays[i])
            XCloseDisplay(cl->be_displays[i]);
    }

    keep_be_displays = cl->be_displays;
    memset(cl, 0, sizeof(__GLXclientState));
    cl->be_displays = keep_be_displays;

    free(cl->GLClientextensions);

    memset(cl->be_displays, 0, screenInfo.numScreens * sizeof(Display *));
}

/*
** This procedure is called when the client who created the context goes
** away OR when glXDestroyContext is called.  In either case, all we do is
** flag that the ID is no longer valid, and (maybe) free the context.
** use.
*/
static int
ContextGone(__GLXcontext * cx, XID id)
{
    cx->idExists = GL_FALSE;
    if (!cx->isCurrent) {
        __glXFreeContext(cx);
    }

    return True;
}

/*
** Free a client's state.
*/
static int
ClientGone(int clientIndex, XID id)
{
    __GLXcontext *cx;
    __GLXclientState *cl = __glXClients[clientIndex];
    int i;

    if (cl) {
        /*
         ** Free all the contexts that are current for this client.
         */
        for (i = 0; i < cl->numCurrentContexts; i++) {
            cx = cl->currentContexts[i];
            if (cx) {
                cx->isCurrent = GL_FALSE;
                if (!cx->idExists) {
                    __glXFreeContext(cx);
                }
            }
        }
        /*
         ** Re-initialize the client state structure.  Don't free it because
         ** we'll probably get another client with this index and use the struct
         ** again.  There is a maximum of MAXCLIENTS of these structures.
         */
        ResetClientState(clientIndex);
    }

    return True;
}

/*
** Free a GLX Pixmap.
*/
void
__glXFreeGLXPixmap(__GLXpixmap * pGlxPixmap)
{
    if (!pGlxPixmap->idExists && !pGlxPixmap->refcnt) {

        PixmapPtr pPixmap = (PixmapPtr) pGlxPixmap->pDraw;

        /*
         ** The DestroyPixmap routine should decrement the refcount and free
         ** only if it's zero.
         */
        (*pGlxPixmap->pScreen->DestroyPixmap) (pPixmap);
        free(pGlxPixmap->be_xids);
        free(pGlxPixmap);
    }

}

static int
PixmapGone(__GLXpixmap * pGlxPixmap, XID id)
{

    pGlxPixmap->idExists = False;
    __glXFreeGLXPixmap(pGlxPixmap);

    return True;
}

void
__glXFreeGLXWindow(__glXWindow * pGlxWindow)
{
    if (!pGlxWindow->idExists && !pGlxWindow->refcnt) {
        WindowPtr pWindow = (WindowPtr) pGlxWindow->pDraw;
        WindowPtr ret;

        dixLookupResourceByType((void *) &ret,
                                pWindow->drawable.id, RT_WINDOW,
                                NullClient, DixUnknownAccess);
        if (ret == pWindow) {
            (*pGlxWindow->pScreen->DestroyWindow) (pWindow);
        }

        free(pGlxWindow);
    }
}

static void
WindowGone(__glXWindow * pGlxWindow, XID id)
{
    pGlxWindow->idExists = False;
    __glXFreeGLXWindow(pGlxWindow);
}

void
__glXFreeGLXPbuffer(__glXPbuffer * pGlxPbuffer)
{
    if (!pGlxPbuffer->idExists && !pGlxPbuffer->refcnt) {
        free(pGlxPbuffer->be_xids);
        free(pGlxPbuffer);
    }
}

static void
PbufferGone(__glXPbuffer * pGlxPbuffer, XID id)
{
    pGlxPbuffer->idExists = False;
    __glXFreeGLXPbuffer(pGlxPbuffer);
}

/*
** Free a context.
*/
GLboolean
__glXFreeContext(__GLXcontext * cx)
{
    if (cx->idExists || cx->isCurrent)
        return GL_FALSE;

    free(cx->feedbackBuf);
    free(cx->selectBuf);
    free(cx->real_ids);
    free(cx->real_vids);

    if (cx->pGlxPixmap) {
        /*
         ** The previous drawable was a glx pixmap, release it.
         */
        cx->pGlxPixmap->refcnt--;
        __glXFreeGLXPixmap(cx->pGlxPixmap);
        cx->pGlxPixmap = 0;
    }

    if (cx->pGlxReadPixmap) {
        /*
         ** The previous drawable was a glx pixmap, release it.
         */
        cx->pGlxReadPixmap->refcnt--;
        __glXFreeGLXPixmap(cx->pGlxReadPixmap);
        cx->pGlxReadPixmap = 0;
    }

    if (cx->pGlxWindow) {
        /*
         ** The previous drawable was a glx window, release it.
         */
        cx->pGlxWindow->refcnt--;
        __glXFreeGLXWindow(cx->pGlxWindow);
        cx->pGlxWindow = 0;
    }

    if (cx->pGlxReadWindow) {
        /*
         ** The previous drawable was a glx window, release it.
         */
        cx->pGlxReadWindow->refcnt--;
        __glXFreeGLXWindow(cx->pGlxReadWindow);
        cx->pGlxReadWindow = 0;
    }

    free(cx);

    if (cx == __glXLastContext) {
        __glXFlushContextCache();
    }

    return GL_TRUE;
}

/*
** Initialize the GLX extension.
*/
void
GlxExtensionInit(void)
{
    ExtensionEntry *extEntry;
    int i;
    int glxSupported = 1;

    /*
       // do not initialize GLX extension if GLX is not supported
       // by ALL back-end servers.
     */
    for (i = 0; i < screenInfo.numScreens; i++) {
        glxSupported &= (dmxScreens[i].glxMajorOpcode > 0);
    }

    if (!glxSupported || !dmxGLXProxy) {
        return;
    }

    __glXContextRes = CreateNewResourceType((DeleteType) ContextGone,
                                            "GLXContext");
    __glXClientRes = CreateNewResourceType((DeleteType) ClientGone,
                                           "GLXClient");
    __glXPixmapRes = CreateNewResourceType((DeleteType) PixmapGone,
                                           "GLXPixmap");
    __glXWindowRes = CreateNewResourceType((DeleteType) WindowGone,
                                           "GLXWindow");
    __glXPbufferRes = CreateNewResourceType((DeleteType) PbufferGone,
                                            "GLXPbuffer");

    if (!__glXContextRes || !__glXClientRes || !__glXPixmapRes ||
        !__glXWindowRes || !__glXPbufferRes)
        return;

    /*
     ** Add extension to server extensions.
     */
    extEntry = AddExtension(GLX_EXTENSION_NAME, __GLX_NUMBER_EVENTS,
                            __GLX_NUMBER_ERRORS, __glXDispatch,
                            __glXSwapDispatch, ResetExtension,
                            StandardMinorOpcode);
    if (!extEntry) {
        FatalError("__glXExtensionInit: AddExtensions failed\n");
        return;
    }
    /*
       if (!AddExtensionAlias(GLX_EXTENSION_ALIAS, extEntry)) {
       ErrorF("__glXExtensionInit: AddExtensionAlias failed\n");
       return;
       }
     */

    __glXerrorBase = extEntry->errorBase;
    __glXBadContext = extEntry->errorBase + GLXBadContext;
    __glXBadContextState = extEntry->errorBase + GLXBadContextState;
    __glXBadDrawable = extEntry->errorBase + GLXBadDrawable;
    __glXBadPixmap = extEntry->errorBase + GLXBadPixmap;
    __glXBadContextTag = extEntry->errorBase + GLXBadContextTag;
    __glXBadCurrentWindow = extEntry->errorBase + GLXBadCurrentWindow;
    __glXBadRenderRequest = extEntry->errorBase + GLXBadRenderRequest;
    __glXBadLargeRequest = extEntry->errorBase + GLXBadLargeRequest;
    __glXUnsupportedPrivateRequest = extEntry->errorBase +
        GLXUnsupportedPrivateRequest;
    __glXBadFBConfig = extEntry->errorBase + GLXBadFBConfig;
    __glXBadPbuffer = extEntry->errorBase + GLXBadPbuffer;

    /*
     ** Initialize table of client state.  There is never a client 0.
     */
    for (i = 1; i <= LimitClients; i++) {
        __glXClients[i] = 0;
    }

    /*
     ** Initialize screen specific data.
     */
    __glXScreenInit(screenInfo.numScreens);

    /*
     ** Initialize swap barrier support.
     */
    SwapBarrierInit();
}

/************************************************************************/

Bool
__glXCoreType(void)
{
    return 0;
}

/************************************************************************/

void
__glXFlushContextCache(void)
{
    __glXLastContext = 0;
}

/************************************************************************/

/*
** Top level dispatcher; all commands are executed from here down.
*/
static int
__glXDispatch(ClientPtr client)
{
    REQUEST(xGLXSingleReq);
    CARD8 opcode;
    int (*proc) (__GLXclientState * cl, GLbyte * pc);
    __GLXclientState *cl;

    opcode = stuff->glxCode;
    cl = __glXClients[client->index];
    if (!cl) {
        cl = calloc(1, sizeof(__GLXclientState));
        __glXClients[client->index] = cl;
        if (!cl) {
            return BadAlloc;
        }

        cl->be_displays = calloc(screenInfo.numScreens, sizeof(Display *));
        if (!cl->be_displays) {
            free(cl);
            return BadAlloc;
        }
    }

    if (!cl->inUse) {
        /*
         ** This is first request from this client.  Associate a resource
         ** with the client so we will be notified when the client dies.
         */
        XID xid = FakeClientID(client->index);

        if (!AddResource(xid, __glXClientRes, (void *) (long) client->index)) {
            return BadAlloc;
        }
        ResetClientState(client->index);
        cl->largeCmdRequestsTotal = 0;
        cl->inUse = GL_TRUE;
        cl->client = client;
    }

    /*
     ** Check for valid opcode.
     */
    if (opcode >= __GLX_SINGLE_TABLE_SIZE) {
        return BadRequest;
    }

    /*
     ** Use the opcode to index into the procedure table.
     */
    proc = __glXSingleTable[opcode];
    return (*proc) (cl, (GLbyte *) stuff);
}

static int
__glXSwapDispatch(ClientPtr client)
{
    REQUEST(xGLXSingleReq);
    CARD8 opcode;
    int (*proc) (__GLXclientState * cl, GLbyte * pc);
    __GLXclientState *cl;

    opcode = stuff->glxCode;
    cl = __glXClients[client->index];
    if (!cl) {
        cl = calloc(1, sizeof(__GLXclientState));
        __glXClients[client->index] = cl;
        if (!cl) {
            return BadAlloc;
        }

        cl->be_displays = calloc(screenInfo.numScreens, sizeof(Display *));
        if (!cl->be_displays) {
            free(cl);
            return BadAlloc;
        }
    }

    if (!cl->inUse) {
        /*
         ** This is first request from this client.  Associate a resource
         ** with the client so we will be notified when the client dies.
         */
        XID xid = FakeClientID(client->index);

        if (!AddResource(xid, __glXClientRes, (void *) (long) client->index)) {
            return BadAlloc;
        }
        ResetClientState(client->index);
        cl->inUse = GL_TRUE;
        cl->client = client;
    }

    /*
     ** Check for valid opcode.
     */
    if (opcode >= __GLX_SINGLE_TABLE_SIZE) {
        return BadRequest;
    }

    /*
     ** Use the opcode to index into the procedure table.
     */
    proc = __glXSwapSingleTable[opcode];
    return (*proc) (cl, (GLbyte *) stuff);
}

int
__glXNoSuchSingleOpcode(__GLXclientState * cl, GLbyte * pc)
{
    return BadRequest;
}

void
__glXNoSuchRenderOpcode(GLbyte * pc)
{
    return;
}
