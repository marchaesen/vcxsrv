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
#include "dmxwindow.h"
#include "dmxpixmap.h"
#include "dmxfont.h"
#include "dmxsync.h"

#include "glxserver.h"
#include <GL/glxtokens.h>
#include "g_disptab.h"
#include <pixmapstr.h>
#include <windowstr.h>
#include "glxutil.h"
#include "glxext.h"
#include "unpack.h"

#include "GL/glxproto.h"
#include "glxvendor.h"
#include "glxvisuals.h"
#include "glxswap.h"

#include "glxcmds.h"

#ifdef PANORAMIX
#include "panoramiXsrv.h"
#endif

extern __GLXFBConfig **__glXFBConfigs;
extern int __glXNumFBConfigs;

extern int glxIsExtensionSupported(char *ext);
extern int __glXGetFBConfigsSGIX(__GLXclientState * cl, GLbyte * pc);

#define BE_TO_CLIENT_ERROR(x) \
           ( (x) >= __glXerrorBase ? \
             (x) - dmxScreen->glxErrorBase + __glXerrorBase \
	     : (x) )

static __GLXFBConfig *
glxLookupFBConfig(GLXFBConfigID id)
{
    int i, j;

    for (i = 0, j = 0; i < __glXNumFBConfigs;
         i++, j += (__glXNumActiveScreens + 1)) {
        if (__glXFBConfigs[j]->id == id)
            return __glXFBConfigs[j];
    }

    return NULL;
}

static __GLXFBConfig *
glxLookupFBConfigByVID(VisualID vid)
{
    int i, j;

    for (i = 0, j = 0; i < __glXNumFBConfigs;
         i++, j += (__glXNumActiveScreens + 1)) {
        if (__glXFBConfigs[j]->associatedVisualId == vid)
            return __glXFBConfigs[j];
    }

    return NULL;
}

static __GLXFBConfig *
glxLookupBackEndFBConfig(GLXFBConfigID id, int screen)
{
    int i;
    int j;

    for (i = 0, j = 0; i < __glXNumFBConfigs;
         i++, j += (__glXNumActiveScreens + 1)) {
        if (__glXFBConfigs[j]->id == id)
            return __glXFBConfigs[j + screen + 1];
    }

    return NULL;

}

Display *
GetBackEndDisplay(__GLXclientState * cl, int s)
{
    if (!cl->be_displays[s]) {
        cl->be_displays[s] =
            XOpenDisplay(DisplayString(dmxScreens[s].beDisplay));
    }
    return cl->be_displays[s];
}

/**
 * Convert the render type bits from fbconfig into context render type.
 */
static int
renderTypeBitsToRenderTypeEnum(int fbRenderType)
{
    if (fbRenderType & GLX_RGBA_BIT)
        return GLX_RGBA_TYPE;

    if (fbRenderType & GLX_COLOR_INDEX_BIT)
        return  GLX_COLOR_INDEX_TYPE;

    if (fbRenderType & GLX_RGBA_FLOAT_BIT_ARB)
        return GLX_RGBA_FLOAT_TYPE_ARB;

    if (fbRenderType & GLX_RGBA_UNSIGNED_FLOAT_BIT_EXT)
        return GLX_RGBA_UNSIGNED_FLOAT_TYPE_EXT;

    /* There's no recognized renderType in the config */
    return GLX_RGBA_TYPE;
}

/*
** Create a GL context with the given properties.
*/
static int
CreateContext(__GLXclientState * cl,
              GLXContextID gcId,
              VisualID vid, GLXFBConfigID fbconfigId,
              int screen, GLXContextID shareList, int isDirect)
{
    ClientPtr client = cl->client;
    xGLXCreateContextReq *be_req;
    xGLXCreateNewContextReq *be_new_req;
    VisualPtr pVisual;
    ScreenPtr pScreen;
    __GLXcontext *glxc, *shareglxc;
    __GLXvisualConfig *pGlxVisual;
    __GLXscreenInfo *pGlxScreen;
    VisualID visual = vid;
    GLint i;
    int from_screen = screen;
    int to_screen = screen;
    DMXScreenInfo *dmxScreen;
    VisualID be_vid = 0;
    GLXFBConfigID be_fbconfigId = 0;
    int num_be_screens;
    Display *dpy;

    /*
     ** Check if screen exists.
     */
    if (screen >= screenInfo.numScreens) {
        client->errorValue = screen;
        return BadValue;
    }

#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;
    }
#endif

    /*
     ** Find the display list space that we want to share.
     **
     */
    if (shareList == None) {
        shareglxc = NULL;
    }
    else {
        dixLookupResourceByType((void **) &shareglxc, shareList,
                                __glXContextRes, NullClient, DixUnknownAccess);
        if (!shareglxc) {
            client->errorValue = shareList;
            return __glXBadContext;
        }
    }

    /*
     ** Allocate memory for the new context
     */
    glxc = calloc(1, sizeof(__GLXcontext));
    if (!glxc) {
        return BadAlloc;
    }

    pScreen = screenInfo.screens[screen];
    pGlxScreen = &__glXActiveScreens[screen];

    if (fbconfigId != None) {
        glxc->pFBConfig = glxLookupFBConfig(fbconfigId);
        if (!glxc->pFBConfig) {
            client->errorValue = fbconfigId;
            free(glxc);
            return BadValue;
        }
        visual = glxc->pFBConfig->associatedVisualId;
    }
    else {
        glxc->pFBConfig = NULL;
    }

    if (visual != None) {
        /*
         ** Check if the visual ID is valid for this screen.
         */
        pVisual = pScreen->visuals;
        for (i = 0; i < pScreen->numVisuals; i++, pVisual++) {
            if (pVisual->vid == visual) {
                break;
            }
        }
        if (i == pScreen->numVisuals) {
            client->errorValue = visual;
            free(glxc);
            return BadValue;
        }

        pGlxVisual = pGlxScreen->pGlxVisual;
        for (i = 0; i < pGlxScreen->numVisuals; i++, pGlxVisual++) {
            if (pGlxVisual->vid == visual) {
                break;
            }
        }
        if (i == pGlxScreen->numVisuals) {
            /*
             ** Visual not support on this screen by this OpenGL implementation.
             */
            client->errorValue = visual;
            free(glxc);
            return BadValue;
        }

        if (glxc->pFBConfig == NULL) {
            glxc->pFBConfig = glxLookupFBConfigByVID(visual);

            if (glxc->pFBConfig == NULL) {
                /*
                 * visual does not have an FBConfig ???
                 client->errorValue = visual;
                 free( glxc );
                 return BadValue;
                 */
            }
        }
    }
    else {
        pVisual = NULL;
        pGlxVisual = NULL;
    }

    glxc->pScreen = pScreen;
    glxc->pGlxScreen = pGlxScreen;
    glxc->pVisual = pVisual;
    glxc->pGlxVisual = pGlxVisual;

    /*
     * allocate memory for back-end servers info
     */
    num_be_screens = to_screen - from_screen + 1;
    glxc->real_ids = (XID *) malloc(sizeof(XID) * num_be_screens);
    if (!glxc->real_ids) {
        return BadAlloc;
    }
    glxc->real_vids = (XID *) malloc(sizeof(XID) * num_be_screens);
    if (!glxc->real_vids) {
        return BadAlloc;
    }

    for (screen = from_screen; screen <= to_screen; screen++) {
        int sent = 0;

        pScreen = screenInfo.screens[screen];
        pGlxScreen = &__glXActiveScreens[screen];
        dmxScreen = &dmxScreens[screen];

        if (glxc->pFBConfig) {
            __GLXFBConfig *beFBConfig =
                glxLookupBackEndFBConfig(glxc->pFBConfig->id,
                                         screen);

            be_fbconfigId = beFBConfig->id;
        }

        if (pGlxVisual) {

            be_vid = glxMatchGLXVisualInConfigList(pGlxVisual,
                                                   dmxScreen->glxVisuals,
                                                   dmxScreen->numGlxVisuals);

            if (!be_vid) {
                /* visual is not supported on the back-end server */
                free(glxc->real_ids);
                free(glxc->real_vids);
                free(glxc);
                return BadValue;
            }
        }

        glxc->real_ids[screen - from_screen] =
            XAllocID(GetBackEndDisplay(cl, screen));

        /* send the create context request to the back-end server */
        dpy = GetBackEndDisplay(cl, screen);
        if (glxc->pFBConfig) {
            /* For a specific visual, multiple render types (i.e., both RGB
             * and COLOR INDEX) can be accessible. The only parameter to
             * choose the renderType should be the class of the colormap,
             * since the first classes do not support RGB mode (only COLOR
             * INDEX), and TrueColor and DirectColor do not support COLOR
             * INDEX.
             */
            int renderType = GLX_RGBA_TYPE;

            if (pVisual) {
                switch (pVisual->class) {
                case PseudoColor:
                case StaticColor:
                case GrayScale:
                case StaticGray:
                    renderType = GLX_COLOR_INDEX_TYPE;
                    break;
                case TrueColor:
                case DirectColor:
                default:
                    renderType = GLX_RGBA_TYPE;
                    break;
                }
            } else {
                renderType =
                    renderTypeBitsToRenderTypeEnum(glxc->pFBConfig->renderType);
            }

            if (__GLX_IS_VERSION_SUPPORTED(1, 3)) {
                LockDisplay(dpy);
                GetReq(GLXCreateNewContext, be_new_req);
                be_new_req->reqType = dmxScreen->glxMajorOpcode;
                be_new_req->glxCode = X_GLXCreateNewContext;
                be_new_req->context =
                    (unsigned int) glxc->real_ids[screen - from_screen];
                be_new_req->fbconfig = (unsigned int) be_fbconfigId;
                be_new_req->screen = DefaultScreen(dpy);
                be_new_req->renderType = renderType;

                be_new_req->shareList =
                    (shareglxc ? shareglxc->real_ids[screen - from_screen] : 0);
                be_new_req->isDirect = 0;
                UnlockDisplay(dpy);
                glxc->real_vids[screen - from_screen] = be_fbconfigId;
                sent = 1;
            }
            else if (glxIsExtensionSupported("GLX_SGIX_fbconfig")) {

                xGLXCreateContextWithConfigSGIXReq *ext_req;
                xGLXVendorPrivateReq *vpreq;

                LockDisplay(dpy);
                GetReqExtra(GLXVendorPrivate,
                            sz_xGLXCreateContextWithConfigSGIXReq -
                            sz_xGLXVendorPrivateReq, vpreq);
                ext_req = (xGLXCreateContextWithConfigSGIXReq *) vpreq;
                ext_req->reqType = dmxScreen->glxMajorOpcode;
                ext_req->glxCode = X_GLXVendorPrivate;
                ext_req->vendorCode = X_GLXvop_CreateContextWithConfigSGIX;
                ext_req->context =
                    (unsigned int) glxc->real_ids[screen - from_screen];
                ext_req->fbconfig = (unsigned int) be_fbconfigId;
                ext_req->screen = DefaultScreen(dpy);
                ext_req->renderType = renderType;
                ext_req->shareList =
                    (shareglxc ? shareglxc->real_ids[screen - from_screen] : 0);
                ext_req->isDirect = 0;
                UnlockDisplay(dpy);
                glxc->real_vids[screen - from_screen] = be_fbconfigId;
                sent = 1;
            }
        }

        if (!sent) {
            LockDisplay(dpy);
            GetReq(GLXCreateContext, be_req);
            be_req->reqType = dmxScreen->glxMajorOpcode;
            be_req->glxCode = X_GLXCreateContext;
            be_req->context =
                (unsigned int) glxc->real_ids[screen - from_screen];
            be_req->visual = (unsigned int) be_vid;
            be_req->screen = DefaultScreen(dpy);
            be_req->shareList =
                (shareglxc ? shareglxc->real_ids[screen - from_screen] : 0);
            be_req->isDirect = 0;
            UnlockDisplay(dpy);
            glxc->real_vids[screen - from_screen] = be_vid;
        }
        SyncHandle();

    }

    /*
     ** Register this context as a resource.
     */
    if (!AddResource(gcId, __glXContextRes, (void *) glxc)) {
        free(glxc->real_ids);
        free(glxc->real_vids);
        free(glxc);
        client->errorValue = gcId;
        return BadAlloc;
    }

    /*
     ** Finally, now that everything is working, setup the rest of the
     ** context.
     */
    glxc->id = gcId;
    glxc->share_id = shareList;
    glxc->idExists = GL_TRUE;
    glxc->isCurrent = GL_FALSE;

    return Success;
}

int
__glXCreateContext(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreateContextReq *req = (xGLXCreateContextReq *) pc;

    return (CreateContext(cl, req->context, req->visual, None,
                          req->screen, req->shareList, req->isDirect));

}

int
__glXCreateNewContext(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreateNewContextReq *req = (xGLXCreateNewContextReq *) pc;

    return (CreateContext(cl, req->context, None, req->fbconfig,
                          req->screen, req->shareList, req->isDirect));

}

int
__glXCreateContextWithConfigSGIX(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreateContextWithConfigSGIXReq *req =
        (xGLXCreateContextWithConfigSGIXReq *) pc;

    return (CreateContext(cl, req->context, None, req->fbconfig,
                          req->screen, req->shareList, req->isDirect));

}

int
__glXQueryMaxSwapBarriersSGIX(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXQueryMaxSwapBarriersSGIXReq *req =
        (xGLXQueryMaxSwapBarriersSGIXReq *) pc;
    xGLXQueryMaxSwapBarriersSGIXReply reply = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .max = QueryMaxSwapBarriersSGIX(req->screen)
    };

    if (client->swapped) {
        __glXSwapQueryMaxSwapBarriersSGIXReply(client, &reply);
    }
    else {
        WriteToClient(client, sz_xGLXQueryMaxSwapBarriersSGIXReply, &reply);
    }

    return Success;
}

int
__glXBindSwapBarrierSGIX(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXBindSwapBarrierSGIXReq *req = (xGLXBindSwapBarrierSGIXReq *) pc;
    DrawablePtr pDraw;
    __GLXpixmap *pGlxPixmap = NULL;
    __glXWindow *pGlxWindow = NULL;
    int rc;

    rc = dixLookupDrawable(&pDraw, req->drawable, client, 0, DixGetAttrAccess);
    if (rc != Success) {
        dixLookupResourceByType((void **) &pGlxPixmap, req->drawable,
                                __glXPixmapRes, NullClient, DixUnknownAccess);
        if (pGlxPixmap)
            pDraw = pGlxPixmap->pDraw;
    }

    if (!pDraw && __GLX_IS_VERSION_SUPPORTED(1, 3)) {
        dixLookupResourceByType((void **) &pGlxWindow, req->drawable,
                                __glXWindowRes, NullClient, DixUnknownAccess);
        if (pGlxWindow)
            pDraw = pGlxWindow->pDraw;
    }

    if (!pDraw) {
        client->errorValue = req->drawable;
        return __glXBadDrawable;
    }

    return BindSwapBarrierSGIX(pDraw, req->barrier);
}

int
__glXJoinSwapGroupSGIX(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXJoinSwapGroupSGIXReq *req = (xGLXJoinSwapGroupSGIXReq *) pc;
    DrawablePtr pDraw, pMember = NULL;
    __GLXpixmap *pGlxPixmap = NULL;
    __glXWindow *pGlxWindow = NULL;
    int rc;

    rc = dixLookupDrawable(&pDraw, req->drawable, client, 0, DixManageAccess);
    if (rc != Success) {
        dixLookupResourceByType((void **) &pGlxPixmap, req->drawable,
                                __glXPixmapRes, NullClient, DixUnknownAccess);
        if (pGlxPixmap)
            pDraw = pGlxPixmap->pDraw;
    }

    if (!pDraw && __GLX_IS_VERSION_SUPPORTED(1, 3)) {
        dixLookupResourceByType((void **) &pGlxWindow, req->drawable,
                                __glXWindowRes, NullClient, DixUnknownAccess);
        if (pGlxWindow)
            pDraw = pGlxWindow->pDraw;
    }

    if (!pDraw) {
        client->errorValue = req->drawable;
        return __glXBadDrawable;
    }

    if (req->member != None) {
        rc = dixLookupDrawable(&pMember, req->member, client, 0,
                               DixGetAttrAccess);
        if (rc != Success) {
            dixLookupResourceByType((void **) &pGlxPixmap, req->member,
                                    __glXPixmapRes, NullClient,
                                    DixUnknownAccess);
            if (pGlxPixmap)
                pMember = pGlxPixmap->pDraw;
        }

        if (!pMember && __GLX_IS_VERSION_SUPPORTED(1, 3)) {
            dixLookupResourceByType((void **) &pGlxWindow, req->member,
                                    __glXWindowRes, NullClient,
                                    DixUnknownAccess);
            if (pGlxWindow)
                pMember = pGlxWindow->pDraw;
        }

        if (!pMember) {
            client->errorValue = req->member;
            return __glXBadDrawable;
        }
    }

    return JoinSwapGroupSGIX(pDraw, pMember);
}

/*
** Destroy a GL context as an X resource.
*/
int
__glXDestroyContext(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXDestroyContextReq *req = (xGLXDestroyContextReq *) pc;
    xGLXDestroyContextReq *be_req;
    GLXContextID gcId = req->context;
    __GLXcontext *glxc;
    int from_screen = 0;
    int to_screen = 0;
    int s;

    dixLookupResourceByType((void **) &glxc, gcId, __glXContextRes,
                            NullClient, DixUnknownAccess);
    if (glxc) {
        /*
         ** Just free the resource; don't actually destroy the context,
         ** because it might be in use.  The
         ** destroy method will be called by the resource destruction routine
         ** if necessary.
         */
        FreeResourceByType(gcId, __glXContextRes, FALSE);

        from_screen = to_screen = glxc->pScreen->myNum;

    }
    else {
        client->errorValue = gcId;
        return __glXBadContext;
    }

#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;
    }
#endif

    /*
     * send DestroyContext request to all back-end servers
     */
    for (s = from_screen; s <= to_screen; s++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[s];
        Display *dpy = GetBackEndDisplay(cl, s);

        LockDisplay(dpy);
        GetReq(GLXDestroyContext, be_req);
        be_req->reqType = dmxScreen->glxMajorOpcode;
        be_req->glxCode = X_GLXDestroyContext;
        be_req->context = glxc->real_ids[s - from_screen];
        UnlockDisplay(dpy);
        SyncHandle();
    }

    return Success;
}

/*****************************************************************************/

/*
** For each client, the server keeps a table of all the contexts that are
** current for that client (each thread of a client may have its own current
** context).  These routines add, change, and lookup contexts in the table.
*/

/*
** Add a current context, and return the tag that will be used to refer to it.
*/
static int
AddCurrentContext(__GLXclientState * cl, __GLXcontext * glxc, DrawablePtr pDraw)
{
    int i;
    int num = cl->numCurrentContexts;
    __GLXcontext **table = cl->currentContexts;

    if (!glxc)
        return -1;

    /*
     ** Try to find an empty slot and use it.
     */
    for (i = 0; i < num; i++) {
        if (!table[i]) {
            table[i] = glxc;
            return i + 1;
        }
    }
    /*
     ** Didn't find a free slot, so we'll have to grow the table.
     */
    if (!num) {
        table = (__GLXcontext **) malloc(sizeof(__GLXcontext *));
        cl->currentDrawables = (DrawablePtr *) malloc(sizeof(DrawablePtr));
        cl->be_currentCTag =
            (GLXContextTag *) malloc(screenInfo.numScreens *
                                     sizeof(GLXContextTag));
    }
    else {
        table = (__GLXcontext **) realloc(table,
                                          (num + 1) * sizeof(__GLXcontext *));
        cl->currentDrawables = (DrawablePtr *) realloc(cl->currentDrawables,
                                                       (num +
                                                        1) *
                                                       sizeof(DrawablePtr));
        cl->be_currentCTag =
            (GLXContextTag *) realloc(cl->be_currentCTag,
                                      (num +
                                       1) * screenInfo.numScreens *
                                      sizeof(GLXContextTag));
    }
    table[num] = glxc;
    cl->currentDrawables[num] = pDraw;
    cl->currentContexts = table;
    cl->numCurrentContexts++;

    memset(cl->be_currentCTag + num * screenInfo.numScreens, 0,
           screenInfo.numScreens * sizeof(GLXContextTag));

    return num + 1;
}

/*
** Given a tag, change the current context for the corresponding entry.
*/
static void
ChangeCurrentContext(__GLXclientState * cl, __GLXcontext * glxc,
                     GLXContextTag tag)
{
    __GLXcontext **table = cl->currentContexts;

    table[tag - 1] = glxc;
}

/*
** Given a tag, and back-end screen number, retrives the current back-end
** tag.
*/
int
GetCurrentBackEndTag(__GLXclientState * cl, GLXContextTag tag, int s)
{
    if (tag > 0) {
        return (cl->be_currentCTag[(tag - 1) * screenInfo.numScreens + s]);
    }
    else {
        return 0;
    }
}

/*
** Given a tag, and back-end screen number, sets the current back-end
** tag.
*/
static void
SetCurrentBackEndTag(__GLXclientState * cl, GLXContextTag tag, int s,
                     GLXContextTag be_tag)
{
    if (tag > 0) {
        cl->be_currentCTag[(tag - 1) * screenInfo.numScreens + s] = be_tag;
    }
}

/*
** For this implementation we have chosen to simply use the index of the
** context's entry in the table as the context tag.  A tag must be greater
** than 0.
*/
__GLXcontext *
__glXLookupContextByTag(__GLXclientState * cl, GLXContextTag tag)
{
    int num = cl->numCurrentContexts;

    if (tag < 1 || tag > num) {
        return 0;
    }
    else {
        return cl->currentContexts[tag - 1];
    }
}

DrawablePtr
__glXLookupDrawableByTag(__GLXclientState * cl, GLXContextTag tag)
{
    int num = cl->numCurrentContexts;

    if (tag < 1 || tag > num) {
        return 0;
    }
    else {
        return cl->currentDrawables[tag - 1];
    }
}

/*****************************************************************************/

static void
StopUsingContext(__GLXcontext * glxc)
{
    if (glxc) {
        if (glxc == __glXLastContext) {
            /* Tell server GL library */
            __glXLastContext = 0;
        }
        glxc->isCurrent = GL_FALSE;
        if (!glxc->idExists) {
            __glXFreeContext(glxc);
        }
    }
}

static void
StartUsingContext(__GLXclientState * cl, __GLXcontext * glxc)
{
    glxc->isCurrent = GL_TRUE;
}

/*****************************************************************************/
/*
** Make an OpenGL context and drawable current.
*/
static int
MakeCurrent(__GLXclientState * cl,
            GLXDrawable drawable,
            GLXDrawable readdrawable,
            GLXContextID context, GLXContextTag oldContextTag)
{
    ClientPtr client = cl->client;
    DrawablePtr pDraw = NULL;
    DrawablePtr pReadDraw = NULL;
    xGLXMakeCurrentReadSGIReply new_reply = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0
    };
    xGLXMakeCurrentReq *be_req;
    xGLXMakeCurrentReply be_reply;
    xGLXMakeContextCurrentReq *be_new_req;
    xGLXMakeContextCurrentReply be_new_reply;
    GLXDrawable drawId = drawable;
    GLXDrawable readId = readdrawable;
    GLXContextID contextId = context;
    __GLXpixmap *pGlxPixmap = 0;
    __GLXpixmap *pReadGlxPixmap = 0;
    __GLXcontext *glxc, *prevglxc;
    GLXContextTag tag = oldContextTag;
    WindowPtr pWin = NULL;
    WindowPtr pReadWin = NULL;
    __glXWindow *pGlxWindow = NULL;
    __glXWindow *pGlxReadWindow = NULL;
    __glXPbuffer *pGlxPbuffer = NULL;
    __glXPbuffer *pGlxReadPbuffer = NULL;

#ifdef PANORAMIX
    PanoramiXRes *pXinDraw = NULL;
    PanoramiXRes *pXinReadDraw = NULL;
#endif
    int from_screen = 0;
    int to_screen = 0;
    int s, rc;

    /*
     ** If one is None and the other isn't, it's a bad match.
     */
    if ((drawId == None && contextId != None) ||
        (drawId != None && contextId == None)) {
        return BadMatch;
    }

    /*
     ** Lookup old context.  If we have one, it must be in a usable state.
     */
    if (tag != 0) {
        prevglxc = __glXLookupContextByTag(cl, tag);
        if (!prevglxc) {
            /*
             ** Tag for previous context is invalid.
             */
            return __glXBadContextTag;
        }
    }
    else {
        prevglxc = 0;
    }

    /*
     ** Lookup new context.  It must not be current for someone else.
     */
    if (contextId != None) {
        dixLookupResourceByType((void **) &glxc, contextId, __glXContextRes,
                                NullClient, DixUnknownAccess);
        if (!glxc) {
            client->errorValue = contextId;
            return __glXBadContext;
        }
        if ((glxc != prevglxc) && glxc->isCurrent) {
            /* Context is current to somebody else */
            return BadAccess;
        }
    }
    else {
        /* Switching to no context.  Ignore new drawable. */
        glxc = 0;
    }

    if (drawId != None) {
        rc = dixLookupDrawable(&pDraw, drawId, client, 0, DixWriteAccess);
        if (rc == Success) {
            if (pDraw->type == DRAWABLE_WINDOW) {
                /*
                 ** Drawable is an X Window.
                 */
                VisualID vid;

                pWin = (WindowPtr) pDraw;
                vid = wVisual(pWin);

                new_reply.writeVid =
                    (glxc->pFBConfig ? glxc->pFBConfig->id : vid);
                new_reply.writeType = GLX_WINDOW_TYPE;

                /*
                 ** Check if window and context are similar.
                 */
                if ((vid != glxc->pVisual->vid) ||
                    (pWin->drawable.pScreen != glxc->pScreen)) {
                    client->errorValue = drawId;
                    return BadMatch;
                }

                from_screen = to_screen = pWin->drawable.pScreen->myNum;

            }
            else {
                /*
                 ** An X Pixmap is not allowed as a parameter (a GLX Pixmap
                 ** is, but it must first be created with glxCreateGLXPixmap).
                 */
                client->errorValue = drawId;
                return __glXBadDrawable;
            }
        }

        if (!pDraw) {
            dixLookupResourceByType((void **) &pGlxPixmap, drawId,
                                    __glXPixmapRes, NullClient,
                                    DixUnknownAccess);
            if (pGlxPixmap) {
                /*
                 ** Check if pixmap and context are similar.
                 */
                if (pGlxPixmap->pScreen != glxc->pScreen ||
                    pGlxPixmap->pGlxVisual != glxc->pGlxVisual) {
                    client->errorValue = drawId;
                    return BadMatch;
                }
                pDraw = pGlxPixmap->pDraw;

                new_reply.writeVid = (glxc->pFBConfig ? glxc->pFBConfig->id :
                                      pGlxPixmap->pGlxVisual->vid);

                new_reply.writeType = GLX_PIXMAP_TYPE;

                from_screen = to_screen = pGlxPixmap->pScreen->myNum;

            }
        }

        if (!pDraw && __GLX_IS_VERSION_SUPPORTED(1, 3)) {
            dixLookupResourceByType((void **) &pGlxWindow, drawId,
                                    __glXWindowRes, NullClient,
                                    DixUnknownAccess);
            if (pGlxWindow) {
                /*
                 ** Drawable is a GLXWindow.
                 **
                 ** Check if GLX window and context are similar.
                 */
                if (pGlxWindow->pScreen != glxc->pScreen ||
                    pGlxWindow->pGlxFBConfig != glxc->pFBConfig) {
                    client->errorValue = drawId;
                    return BadMatch;
                }

                pDraw = pGlxWindow->pDraw;
                new_reply.writeVid = pGlxWindow->pGlxFBConfig->id;
                new_reply.writeType = GLX_GLXWINDOW_TYPE;
            }

        }

        if (!pDraw && __GLX_IS_VERSION_SUPPORTED(1, 3)) {
            dixLookupResourceByType((void **) &pGlxPbuffer, drawId,
                                    __glXPbufferRes, NullClient,
                                    DixUnknownAccess);
            if (pGlxPbuffer) {
                if (pGlxPbuffer->pScreen != glxc->pScreen ||
                    pGlxPbuffer->pFBConfig != glxc->pFBConfig) {
                    client->errorValue = drawId;
                    return BadMatch;
                }

                pDraw = (DrawablePtr) pGlxPbuffer;
                new_reply.writeVid = pGlxPbuffer->pFBConfig->id;
                new_reply.writeType = GLX_PBUFFER_TYPE;
            }
        }

        if (!pDraw) {
            /*
             ** Drawable is not a Window , GLXWindow or a GLXPixmap.
             */
            client->errorValue = drawId;
            return __glXBadDrawable;
        }

    }
    else {
        pDraw = 0;
    }

    if (readId != None && readId != drawId) {
        rc = dixLookupDrawable(&pReadDraw, readId, client, 0, DixReadAccess);
        if (rc == Success) {
            if (pReadDraw->type == DRAWABLE_WINDOW) {
                /*
                 ** Drawable is an X Window.
                 */
                VisualID vid;

                pReadWin = (WindowPtr) pDraw;
                vid = wVisual(pReadWin);

                new_reply.readVid =
                    (glxc->pFBConfig ? glxc->pFBConfig->id : vid);
                new_reply.readType = GLX_WINDOW_TYPE;

                /*
                 ** Check if window and context are similar.
                 */
                if ((vid != glxc->pVisual->vid) ||
                    (pReadWin->drawable.pScreen != glxc->pScreen)) {
                    client->errorValue = readId;
                    return BadMatch;
                }

            }
            else {

                /*
                 ** An X Pixmap is not allowed as a parameter (a GLX Pixmap
                 ** is, but it must first be created with glxCreateGLXPixmap).
                 */
                client->errorValue = readId;
                return __glXBadDrawable;
            }
        }

        if (!pReadDraw) {
            dixLookupResourceByType((void **) &pReadGlxPixmap, readId,
                                    __glXPixmapRes, NullClient,
                                    DixUnknownAccess);
            if (pReadGlxPixmap) {
                /*
                 ** Check if pixmap and context are similar.
                 */
                if (pReadGlxPixmap->pScreen != glxc->pScreen ||
                    pReadGlxPixmap->pGlxVisual != glxc->pGlxVisual) {
                    client->errorValue = readId;
                    return BadMatch;
                }
                pReadDraw = pReadGlxPixmap->pDraw;

                new_reply.readVid = (glxc->pFBConfig ? glxc->pFBConfig->id :
                                     pReadGlxPixmap->pGlxVisual->vid);
                new_reply.readType = GLX_PIXMAP_TYPE;

            }
        }

        if (!pReadDraw && __GLX_IS_VERSION_SUPPORTED(1, 3)) {
            dixLookupResourceByType((void **) &pGlxReadWindow, readId,
                                    __glXWindowRes, NullClient,
                                    DixUnknownAccess);
            if (pGlxReadWindow) {
                /*
                 ** Drawable is a GLXWindow.
                 **
                 ** Check if GLX window and context are similar.
                 */
                if (pGlxReadWindow->pScreen != glxc->pScreen ||
                    pGlxReadWindow->pGlxFBConfig != glxc->pFBConfig) {
                    client->errorValue = readId;
                    return BadMatch;
                }

                pReadDraw = pGlxReadWindow->pDraw;
                new_reply.readVid = pGlxReadWindow->pGlxFBConfig->id;
                new_reply.readType = GLX_GLXWINDOW_TYPE;
            }
        }

        if (!pReadDraw && __GLX_IS_VERSION_SUPPORTED(1, 3)) {
            dixLookupResourceByType((void **) &pGlxReadPbuffer, readId,
                                    __glXPbufferRes, NullClient,
                                    DixUnknownAccess);
            if (pGlxReadPbuffer) {
                if (pGlxReadPbuffer->pScreen != glxc->pScreen ||
                    pGlxReadPbuffer->pFBConfig != glxc->pFBConfig) {
                    client->errorValue = drawId;
                    return BadMatch;
                }

                pReadDraw = (DrawablePtr) pGlxReadPbuffer;
                new_reply.readVid = pGlxReadPbuffer->pFBConfig->id;
                new_reply.readType = GLX_PBUFFER_TYPE;
            }
        }

        if (!pReadDraw) {
            /*
             ** Drawable is neither a Window nor a GLXPixmap.
             */
            client->errorValue = readId;
            return __glXBadDrawable;
        }

    }
    else {
        pReadDraw = pDraw;
        pReadGlxPixmap = pGlxPixmap;
        pReadWin = pWin;
        new_reply.readVid = new_reply.writeVid;
        new_reply.readType = new_reply.writeType;
    }

    if (prevglxc) {

        if (prevglxc->pGlxPixmap) {
            /*
             ** The previous drawable was a glx pixmap, release it.
             */
            prevglxc->pGlxPixmap->refcnt--;
            __glXFreeGLXPixmap(prevglxc->pGlxPixmap);
            prevglxc->pGlxPixmap = 0;
        }

        if (prevglxc->pGlxReadPixmap) {
            /*
             ** The previous drawable was a glx pixmap, release it.
             */
            prevglxc->pGlxReadPixmap->refcnt--;
            __glXFreeGLXPixmap(prevglxc->pGlxReadPixmap);
            prevglxc->pGlxReadPixmap = 0;
        }

        if (prevglxc->pGlxWindow) {
            /*
             ** The previous drawable was a glx window, release it.
             */
            prevglxc->pGlxWindow->refcnt--;
            __glXFreeGLXWindow(prevglxc->pGlxWindow);
            prevglxc->pGlxWindow = 0;
        }

        if (prevglxc->pGlxReadWindow) {
            /*
             ** The previous drawable was a glx window, release it.
             */
            prevglxc->pGlxReadWindow->refcnt--;
            __glXFreeGLXWindow(prevglxc->pGlxReadWindow);
            prevglxc->pGlxReadWindow = 0;
        }

        if (prevglxc->pGlxPbuffer) {
            /*
             ** The previous drawable was a glx Pbuffer, release it.
             */
            prevglxc->pGlxPbuffer->refcnt--;
            __glXFreeGLXPbuffer(prevglxc->pGlxPbuffer);
            prevglxc->pGlxPbuffer = 0;
        }

        if (prevglxc->pGlxReadPbuffer) {
            /*
             ** The previous drawable was a glx Pbuffer, release it.
             */
            prevglxc->pGlxReadPbuffer->refcnt--;
            __glXFreeGLXPbuffer(prevglxc->pGlxReadPbuffer);
            prevglxc->pGlxReadPbuffer = 0;
        }

        ChangeCurrentContext(cl, glxc, tag);
        ChangeCurrentContext(cl, glxc, tag);
        StopUsingContext(prevglxc);
    }
    else {
        tag = AddCurrentContext(cl, glxc, pDraw);
    }
    if (glxc) {

        glxc->pGlxPixmap = pGlxPixmap;
        glxc->pGlxReadPixmap = pReadGlxPixmap;
        glxc->pGlxWindow = pGlxWindow;
        glxc->pGlxReadWindow = pGlxReadWindow;
        glxc->pGlxPbuffer = pGlxPbuffer;
        glxc->pGlxReadPbuffer = pGlxReadPbuffer;

        if (pGlxPixmap) {
            pGlxPixmap->refcnt++;
        }

        if (pReadGlxPixmap) {
            pReadGlxPixmap->refcnt++;
        }

        if (pGlxWindow) {
            pGlxWindow->refcnt++;
        }

        if (pGlxReadWindow) {
            pGlxReadWindow->refcnt++;
        }

        if (pGlxPbuffer) {
            pGlxPbuffer->refcnt++;
        }

        if (pGlxReadPbuffer) {
            pGlxReadPbuffer->refcnt++;
        }

        StartUsingContext(cl, glxc);
        new_reply.contextTag = tag;
    }
    else {
        new_reply.contextTag = 0;
    }

#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;

        if (pDraw && new_reply.writeType != GLX_PBUFFER_TYPE) {
            dixLookupResourceByClass((void **) &pXinDraw,
                                     pDraw->id, XRC_DRAWABLE,
                                     client, DixReadAccess);
        }

        if (pReadDraw && pReadDraw != pDraw &&
            new_reply.readType != GLX_PBUFFER_TYPE) {
            dixLookupResourceByClass((void **) &pXinReadDraw,
                                     pReadDraw->id, XRC_DRAWABLE,
                                     client, DixReadAccess);
        }
        else {
            pXinReadDraw = pXinDraw;
        }
    }
#endif

    /* send the MakeCurrent request to all required
     * back-end servers.
     */
    for (s = from_screen; s <= to_screen; s++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[s];
        Display *dpy = GetBackEndDisplay(cl, s);
        unsigned int be_draw = None;
        unsigned int be_read_draw = None;

        if (pGlxPixmap) {
            be_draw = pGlxPixmap->be_xids[s];
        }
        else if (pGlxPbuffer) {
            be_draw = pGlxPbuffer->be_xids[s];
        }
#ifdef PANORAMIX
        else if (pXinDraw) {
            dixLookupWindow(&pWin, pXinDraw->info[s].id, client, DixReadAccess);
        }
#endif
        else if (pGlxWindow) {
            pWin = (WindowPtr) pGlxWindow->pDraw;
        }

        if (pWin && be_draw == None) {
            be_draw = (unsigned int) (DMX_GET_WINDOW_PRIV(pWin))->window;
            if (!be_draw) {
                /* it might be that the window did not created yet on the */
                /* back-end server (lazy window creation option), force   */
                /* creation of the window */
                dmxCreateAndRealizeWindow(pWin, TRUE);
                be_draw = (unsigned int) (DMX_GET_WINDOW_PRIV(pWin))->window;
            }
        }

        /*
         * Before sending the MakeCurrent request - sync the
         * X11 connection to the back-end servers to make sure
         * that drawable is already created
         */
        dmxSync(dmxScreen, 1);

        if (drawId == readId) {
            LockDisplay(dpy);
            GetReq(GLXMakeCurrent, be_req);
            be_req->reqType = dmxScreen->glxMajorOpcode;
            be_req->glxCode = X_GLXMakeCurrent;
            be_req->drawable = be_draw;
            be_req->context =
                (unsigned int) (glxc ? glxc->real_ids[s - from_screen] : 0);
            be_req->oldContextTag = GetCurrentBackEndTag(cl, tag, s);
            if (!_XReply(dpy, (xReply *) &be_reply, 0, False)) {

                /* The make current failed */
                UnlockDisplay(dpy);
                SyncHandle();
                return (BE_TO_CLIENT_ERROR(dmxLastErrorEvent.error_code));
            }

            UnlockDisplay(dpy);
            SyncHandle();

            SetCurrentBackEndTag(cl, tag, s, be_reply.contextTag);
        }
        else {

            if (pReadGlxPixmap) {
                be_read_draw = pReadGlxPixmap->be_xids[s];
            }
            else if (pGlxReadPbuffer) {
                be_read_draw = pGlxReadPbuffer->be_xids[s];
            }
#ifdef PANORAMIX
            else if (pXinReadDraw) {
                dixLookupWindow(&pReadWin, pXinReadDraw->info[s].id, client,
                                DixReadAccess);
            }
#endif
            else if (pGlxReadWindow) {
                pReadWin = (WindowPtr) pGlxReadWindow->pDraw;
            }

            if (pReadWin && be_read_draw == None) {
                be_read_draw =
                    (unsigned int) (DMX_GET_WINDOW_PRIV(pReadWin))->window;
                if (!be_read_draw) {
                    /* it might be that the window did not created yet on the */
                    /* back-end server (lazy window creation option), force   */
                    /* creation of the window */
                    dmxCreateAndRealizeWindow(pReadWin, TRUE);
                    be_read_draw =
                        (unsigned int) (DMX_GET_WINDOW_PRIV(pReadWin))->window;
                    dmxSync(dmxScreen, 1);
                }
            }

            if (__GLX_IS_VERSION_SUPPORTED(1, 3)) {
                LockDisplay(dpy);
                GetReq(GLXMakeContextCurrent, be_new_req);
                be_new_req->reqType = dmxScreen->glxMajorOpcode;
                be_new_req->glxCode = X_GLXMakeContextCurrent;
                be_new_req->drawable = be_draw;
                be_new_req->readdrawable = be_read_draw;
                be_new_req->context =
                    (unsigned int) (glxc ? glxc->real_ids[s - from_screen] : 0);
                be_new_req->oldContextTag = GetCurrentBackEndTag(cl, tag, s);
                if (!_XReply(dpy, (xReply *) &be_new_reply, 0, False)) {

                    /* The make current failed */
                    UnlockDisplay(dpy);
                    SyncHandle();
                    return (BE_TO_CLIENT_ERROR(dmxLastErrorEvent.error_code));
                }

                UnlockDisplay(dpy);
                SyncHandle();

                SetCurrentBackEndTag(cl, tag, s, be_new_reply.contextTag);
            }
            else if (glxIsExtensionSupported("GLX_SGI_make_current_read")) {
                xGLXMakeCurrentReadSGIReq *ext_req;
                xGLXVendorPrivateWithReplyReq *vpreq;
                xGLXMakeCurrentReadSGIReply ext_reply;

                LockDisplay(dpy);
                GetReqExtra(GLXVendorPrivateWithReply,
                            sz_xGLXMakeCurrentReadSGIReq -
                            sz_xGLXVendorPrivateWithReplyReq, vpreq);
                ext_req = (xGLXMakeCurrentReadSGIReq *) vpreq;
                ext_req->reqType = dmxScreen->glxMajorOpcode;
                ext_req->glxCode = X_GLXVendorPrivateWithReply;
                ext_req->vendorCode = X_GLXvop_MakeCurrentReadSGI;
                ext_req->drawable = be_draw;
                ext_req->readable = be_read_draw;
                ext_req->context =
                    (unsigned int) (glxc ? glxc->real_ids[s - from_screen] : 0);
                ext_req->oldContextTag = GetCurrentBackEndTag(cl, tag, s);
                if (!_XReply(dpy, (xReply *) &ext_reply, 0, False)) {

                    /* The make current failed */
                    UnlockDisplay(dpy);
                    SyncHandle();
                    return (BE_TO_CLIENT_ERROR(dmxLastErrorEvent.error_code));
                }

                UnlockDisplay(dpy);
                SyncHandle();

                SetCurrentBackEndTag(cl, tag, s, ext_reply.contextTag);

            }
            else {
                return BadMatch;
            }
        }

        XFlush(dpy);
    }

    if (client->swapped) {
        __glXSwapMakeCurrentReply(client, &new_reply);
    }
    else {
        WriteToClient(client, sz_xGLXMakeContextCurrentReply, &new_reply);
    }

    return Success;
}

int
__glXMakeCurrent(__GLXclientState * cl, GLbyte * pc)
{
    xGLXMakeCurrentReq *req = (xGLXMakeCurrentReq *) pc;

    return (MakeCurrent(cl, req->drawable, req->drawable,
                        req->context, req->oldContextTag));
}

int
__glXMakeContextCurrent(__GLXclientState * cl, GLbyte * pc)
{
    xGLXMakeContextCurrentReq *req = (xGLXMakeContextCurrentReq *) pc;

    return (MakeCurrent(cl, req->drawable, req->readdrawable,
                        req->context, req->oldContextTag));
}

int
__glXMakeCurrentReadSGI(__GLXclientState * cl, GLbyte * pc)
{
    xGLXMakeCurrentReadSGIReq *req = (xGLXMakeCurrentReadSGIReq *) pc;

    return (MakeCurrent(cl, req->drawable, req->readable,
                        req->context, req->oldContextTag));
}

int
__glXIsDirect(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXIsDirectReq *req = (xGLXIsDirectReq *) pc;
    xGLXIsDirectReply reply;
    __GLXcontext *glxc;

    /*
     ** Find the GL context.
     */
    dixLookupResourceByType((void **) &glxc, req->context, __glXContextRes,
                            NullClient, DixUnknownAccess);
    if (!glxc) {
        client->errorValue = req->context;
        return __glXBadContext;
    }

    reply = (xGLXIsDirectReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .isDirect = 0
    };

    if (client->swapped) {
        __glXSwapIsDirectReply(client, &reply);
    }
    else {
        WriteToClient(client, sz_xGLXIsDirectReply, &reply);
    }

    return Success;
}

int
__glXQueryVersion(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;

/*    xGLXQueryVersionReq *req = (xGLXQueryVersionReq *) pc; */

    xGLXQueryVersionReply reply = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
    /*
     ** Server should take into consideration the version numbers sent by the
     ** client if it wants to work with older clients; however, in this
     ** implementation the server just returns its version number.
     */
        .majorVersion = __glXVersionMajor,
        .minorVersion = __glXVersionMinor
    };

    if (client->swapped) {
        __glXSwapQueryVersionReply(client, &reply);
    }
    else {
        WriteToClient(client, sz_xGLXQueryVersionReply, &reply);
    }
    return Success;
}

int
__glXWaitGL(__GLXclientState * cl, GLbyte * pc)
{
    xGLXWaitGLReq *req = (xGLXWaitGLReq *) pc;
    xGLXWaitGLReq *be_req = (xGLXWaitGLReq *) pc;
    int from_screen = 0;
    int to_screen = 0;
    int s;
    __GLXcontext *glxc = NULL;

    if (req->contextTag != 0) {
        glxc = __glXLookupContextByTag(cl, req->contextTag);
        if (glxc) {
            from_screen = to_screen = glxc->pScreen->myNum;
        }
    }

#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;
    }
#endif

    for (s = from_screen; s <= to_screen; s++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[s];
        Display *dpy = GetBackEndDisplay(cl, s);

        LockDisplay(dpy);
        GetReq(GLXWaitGL, be_req);
        be_req->reqType = dmxScreen->glxMajorOpcode;
        be_req->glxCode = X_GLXWaitGL;
        be_req->contextTag =
            (glxc ? GetCurrentBackEndTag(cl, req->contextTag, s) : 0);
        UnlockDisplay(dpy);
        SyncHandle();

        XSync(dpy, False);
    }

    return Success;
}

int
__glXWaitX(__GLXclientState * cl, GLbyte * pc)
{
    xGLXWaitXReq *req = (xGLXWaitXReq *) pc;
    xGLXWaitXReq *be_req;
    int from_screen = 0;
    int to_screen = 0;
    int s;
    __GLXcontext *glxc = NULL;

    if (req->contextTag != 0) {
        glxc = __glXLookupContextByTag(cl, req->contextTag);
        if (glxc) {
            from_screen = to_screen = glxc->pScreen->myNum;
        }
    }

#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;
    }
#endif

    for (s = from_screen; s <= to_screen; s++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[s];
        Display *dpy = GetBackEndDisplay(cl, s);

        dmxSync(dmxScreen, 1);

        LockDisplay(dpy);
        GetReq(GLXWaitX, be_req);
        be_req->reqType = dmxScreen->glxMajorOpcode;
        be_req->glxCode = X_GLXWaitX;
        be_req->contextTag =
            (glxc ? GetCurrentBackEndTag(cl, req->contextTag, s) : 0);
        UnlockDisplay(dpy);
        SyncHandle();

        XFlush(dpy);
    }

    return Success;
}

int
__glXCopyContext(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXCopyContextReq *be_req;
    xGLXCopyContextReq *req = (xGLXCopyContextReq *) pc;
    GLXContextID source = req->source;
    GLXContextID dest = req->dest;
    GLXContextTag tag = req->contextTag;
    unsigned long mask = req->mask;
    __GLXcontext *src, *dst;
    int s;
    int from_screen = 0;
    int to_screen = 0;

    /*
     ** Check that each context exists.
     */
    dixLookupResourceByType((void **) &src, source, __glXContextRes,
                            NullClient, DixUnknownAccess);
    if (!src) {
        client->errorValue = source;
        return __glXBadContext;
    }
    dixLookupResourceByType((void **) &dst, dest, __glXContextRes,
                            NullClient, DixUnknownAccess);
    if (!dst) {
        client->errorValue = dest;
        return __glXBadContext;
    }

    /*
     ** They must be in the same address space, and same screen.
     */
    if (src->pGlxScreen != dst->pGlxScreen) {
        client->errorValue = source;
        return BadMatch;
    }

    /*
     ** The destination context must not be current for any client.
     */
    if (dst->isCurrent) {
        client->errorValue = dest;
        return BadAccess;
    }

    if (tag) {
        __GLXcontext *tagcx = __glXLookupContextByTag(cl, tag);

        if (!tagcx) {
            return __glXBadContextTag;
        }
        if (tagcx != src) {
            /*
             ** This would be caused by a faulty implementation of the client
             ** library.
             */
            return BadMatch;
        }
    }

    from_screen = to_screen = src->pScreen->myNum;

#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;
    }
#endif

    for (s = from_screen; s <= to_screen; s++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[s];
        Display *dpy = GetBackEndDisplay(cl, s);

        LockDisplay(dpy);
        GetReq(GLXCopyContext, be_req);
        be_req->reqType = dmxScreen->glxMajorOpcode;
        be_req->glxCode = X_GLXCopyContext;
        be_req->source = (unsigned int) src->real_ids[s - from_screen];
        be_req->dest = (unsigned int) dst->real_ids[s - from_screen];
        be_req->mask = mask;
        be_req->contextTag =
            (tag ? GetCurrentBackEndTag(cl, req->contextTag, s) : 0);
        UnlockDisplay(dpy);
        SyncHandle();
    }

    return Success;
}

int
__glXGetVisualConfigs(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXGetVisualConfigsReq *req = (xGLXGetVisualConfigsReq *) pc;
    xGLXGetVisualConfigsReply reply;
    __GLXscreenInfo *pGlxScreen;
    __GLXvisualConfig *pGlxVisual;
    CARD32 buf[__GLX_TOTAL_CONFIG];
    unsigned int screen;
    int i, p;

    screen = req->screen;
    if (screen >= screenInfo.numScreens) {
        /* The client library must send a valid screen number. */
        client->errorValue = screen;
        return BadValue;
    }
    pGlxScreen = &__glXActiveScreens[screen];

    reply = (xGLXGetVisualConfigsReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .numVisuals = pGlxScreen->numGLXVisuals,
        .numProps = __GLX_TOTAL_CONFIG,
        .length = (pGlxScreen->numGLXVisuals * __GLX_SIZE_CARD32 *
                    __GLX_TOTAL_CONFIG) >> 2
    };

    WriteToClient(client, sz_xGLXGetVisualConfigsReply, &reply);

    for (i = 0; i < pGlxScreen->numVisuals; i++) {
        pGlxVisual = &pGlxScreen->pGlxVisual[i];
        if (!pGlxScreen->isGLXvis[i] || pGlxVisual->vid == 0) {
            /* not a usable visual */
            continue;
        }
        p = 0;
        buf[p++] = pGlxVisual->vid;
        buf[p++] = pGlxVisual->class;
        buf[p++] = pGlxVisual->rgba;

        buf[p++] = pGlxVisual->redSize;
        buf[p++] = pGlxVisual->greenSize;
        buf[p++] = pGlxVisual->blueSize;
        buf[p++] = pGlxVisual->alphaSize;
        buf[p++] = pGlxVisual->accumRedSize;
        buf[p++] = pGlxVisual->accumGreenSize;
        buf[p++] = pGlxVisual->accumBlueSize;
        buf[p++] = pGlxVisual->accumAlphaSize;

        buf[p++] = pGlxVisual->doubleBuffer;
        buf[p++] = pGlxVisual->stereo;

        buf[p++] = pGlxVisual->bufferSize;
        buf[p++] = pGlxVisual->depthSize;
        buf[p++] = pGlxVisual->stencilSize;
        buf[p++] = pGlxVisual->auxBuffers;
        buf[p++] = pGlxVisual->level;
        /*
         ** Add token/value pairs for extensions.
         */
        buf[p++] = GLX_VISUAL_CAVEAT_EXT;
        buf[p++] = pGlxVisual->visualRating;
        buf[p++] = GLX_TRANSPARENT_TYPE_EXT;
        buf[p++] = pGlxVisual->transparentPixel;
        buf[p++] = GLX_TRANSPARENT_RED_VALUE_EXT;
        buf[p++] = pGlxVisual->transparentRed;
        buf[p++] = GLX_TRANSPARENT_GREEN_VALUE_EXT;
        buf[p++] = pGlxVisual->transparentGreen;
        buf[p++] = GLX_TRANSPARENT_BLUE_VALUE_EXT;
        buf[p++] = pGlxVisual->transparentBlue;
        buf[p++] = GLX_TRANSPARENT_ALPHA_VALUE_EXT;
        buf[p++] = pGlxVisual->transparentAlpha;
        buf[p++] = GLX_TRANSPARENT_INDEX_VALUE_EXT;
        buf[p++] = pGlxVisual->transparentIndex;
        buf[p++] = GLX_SAMPLES_SGIS;
        buf[p++] = pGlxVisual->multiSampleSize;
        buf[p++] = GLX_SAMPLE_BUFFERS_SGIS;
        buf[p++] = pGlxVisual->nMultiSampleBuffers;
        buf[p++] = GLX_VISUAL_SELECT_GROUP_SGIX;
        buf[p++] = pGlxVisual->visualSelectGroup;

        WriteToClient(client, __GLX_SIZE_CARD32 * __GLX_TOTAL_CONFIG, buf);
    }
    return Success;
}

/*
** Create a GLX Pixmap from an X Pixmap.
*/
static int
CreateGLXPixmap(__GLXclientState * cl,
                VisualID visual, GLXFBConfigID fbconfigId,
                int screenNum, XID pixmapId, XID glxpixmapId)
{
    ClientPtr client = cl->client;
    xGLXCreateGLXPixmapReq *be_req;
    xGLXCreatePixmapReq *be_new_req;
    DrawablePtr pDraw;
    ScreenPtr pScreen;
    VisualPtr pVisual;
    __GLXpixmap *pGlxPixmap;
    __GLXscreenInfo *pGlxScreen;
    __GLXvisualConfig *pGlxVisual;
    __GLXFBConfig *pFBConfig;
    int i, s, rc;
    int from_screen, to_screen;

#ifdef PANORAMIX
    PanoramiXRes *pXinDraw = NULL;
#endif

    rc = dixLookupDrawable(&pDraw, pixmapId, client, M_DRAWABLE_PIXMAP,
                           DixAddAccess);
    if (rc != Success)
        return rc;

    /*
     ** Check if screen of visual matches screen of pixmap.
     */
    pScreen = pDraw->pScreen;
    if (screenNum != pScreen->myNum) {
        return BadMatch;
    }

    if (fbconfigId == 0 && visual == 0) {
        return BadValue;
    }

    if (fbconfigId != None) {
        pFBConfig = glxLookupFBConfig(fbconfigId);
        if (!pFBConfig) {
            client->errorValue = fbconfigId;
            return BadValue;
        }
        visual = pFBConfig->associatedVisualId;
    }
    else {
        pFBConfig = NULL;
    }

    if (visual != None) {
        /*
         ** Find the VisualRec for this visual.
         */
        pVisual = pScreen->visuals;
        for (i = 0; i < pScreen->numVisuals; i++, pVisual++) {
            if (pVisual->vid == visual) {
                break;
            }
        }
        if (i == pScreen->numVisuals) {
            client->errorValue = visual;
            return BadValue;
        }
        /*
         ** Check if depth of visual matches depth of pixmap.
         */
        if (pVisual->nplanes != pDraw->depth) {
            client->errorValue = visual;
            return BadMatch;
        }

        /*
         ** Get configuration of the visual.
         */
        pGlxScreen = &__glXActiveScreens[screenNum];
        pGlxVisual = pGlxScreen->pGlxVisual;
        for (i = 0; i < pGlxScreen->numVisuals; i++, pGlxVisual++) {
            if (pGlxVisual->vid == visual) {
                break;
            }
        }
        if (i == pGlxScreen->numVisuals) {
            /*
             ** Visual not support on this screen by this OpenGL implementation.
             */
            client->errorValue = visual;
            return BadValue;
        }

        /* find the FBConfig for that visual (if any) */
        if (pFBConfig == NULL) {
            pFBConfig = glxLookupFBConfigByVID(visual);

            if (pFBConfig == NULL) {
                /*
                 * visual does not have an FBConfig ???
                 client->errorValue = visual;
                 return BadValue;
                 */
            }
        }
    }
    else {
        pVisual = NULL;
        pGlxVisual = NULL;
        pGlxScreen = &__glXActiveScreens[pDraw->pScreen->myNum];
    }

    pGlxPixmap = (__GLXpixmap *) malloc(sizeof(__GLXpixmap));
    if (!pGlxPixmap) {
        return BadAlloc;
    }
    pGlxPixmap->be_xids = (XID *) malloc(sizeof(XID) * screenInfo.numScreens);
    if (!pGlxPixmap->be_xids) {
        free(pGlxPixmap);
        return BadAlloc;
    }

    pGlxPixmap->pDraw = pDraw;
    pGlxPixmap->pGlxScreen = pGlxScreen;
    pGlxPixmap->pGlxVisual = pGlxVisual;
    pGlxPixmap->pFBConfig = pFBConfig;
    pGlxPixmap->pScreen = pScreen;
    pGlxPixmap->idExists = True;
    pGlxPixmap->refcnt = 0;

    /*
     ** Bump the ref count on the X pixmap so it won't disappear.
     */
    ((PixmapPtr) pDraw)->refcnt++;

    /*
     * send the request to the back-end server(s)
     */
    from_screen = to_screen = screenNum;
#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;

        dixLookupResourceByClass((void **) &pXinDraw,
                                 pDraw->id, XRC_DRAWABLE,
                                 client, DixReadAccess);
    }
#endif

    for (s = from_screen; s <= to_screen; s++) {

        DMXScreenInfo *dmxScreen = &dmxScreens[s];
        Display *dpy = GetBackEndDisplay(cl, s);
        Pixmap be_pixmap;
        DrawablePtr pRealDraw = pDraw;

#ifdef PANORAMIX
        if (pXinDraw) {
            dixLookupDrawable(&pRealDraw, pXinDraw->info[s].id, client, 0,
                              DixAddAccess);
        }
#endif

        be_pixmap = (DMX_GET_PIXMAP_PRIV((PixmapPtr) pRealDraw))->pixmap;

        /* make sure pixmap already created on back-end */
        dmxSync(dmxScreen, 1);

        if (pFBConfig && __GLX_IS_VERSION_SUPPORTED(1, 3)) {
            __GLXFBConfig *be_FBConfig =
                glxLookupBackEndFBConfig(pFBConfig->id, s);

            LockDisplay(dpy);
            pGlxPixmap->be_xids[s] = XAllocID(dpy);
            GetReq(GLXCreatePixmap, be_new_req);
            be_new_req->reqType = dmxScreen->glxMajorOpcode;
            be_new_req->glxCode = X_GLXCreatePixmap;
            be_new_req->screen = DefaultScreen(dpy);
            be_new_req->fbconfig = be_FBConfig->id;
            be_new_req->pixmap = (unsigned int) be_pixmap;
            be_new_req->glxpixmap = (unsigned int) pGlxPixmap->be_xids[s];
            be_new_req->numAttribs = 0;
            UnlockDisplay(dpy);
            SyncHandle();
        }
        else if (pFBConfig && glxIsExtensionSupported("GLX_SGIX_fbconfig")) {
            __GLXFBConfig *be_FBConfig =
                glxLookupBackEndFBConfig(pFBConfig->id, s);
            xGLXCreateGLXPixmapWithConfigSGIXReq *ext_req;
            xGLXVendorPrivateReq *vpreq;

            LockDisplay(dpy);
            pGlxPixmap->be_xids[s] = XAllocID(dpy);
            GetReqExtra(GLXVendorPrivate,
                        sz_xGLXCreateGLXPixmapWithConfigSGIXReq -
                        sz_xGLXVendorPrivateReq, vpreq);
            ext_req = (xGLXCreateGLXPixmapWithConfigSGIXReq *) vpreq;
            ext_req->reqType = dmxScreen->glxMajorOpcode;
            ext_req->glxCode = X_GLXVendorPrivate;
            ext_req->vendorCode = X_GLXvop_CreateGLXPixmapWithConfigSGIX;
            ext_req->screen = DefaultScreen(dpy);
            ext_req->fbconfig = be_FBConfig->id;
            ext_req->pixmap = (unsigned int) be_pixmap;
            ext_req->glxpixmap = (unsigned int) pGlxPixmap->be_xids[s];
            UnlockDisplay(dpy);
            SyncHandle();
        }
        else if (pGlxVisual) {
            LockDisplay(dpy);
            pGlxPixmap->be_xids[s] = XAllocID(dpy);
            GetReq(GLXCreateGLXPixmap, be_req);
            be_req->reqType = dmxScreen->glxMajorOpcode;
            be_req->glxCode = X_GLXCreateGLXPixmap;
            be_req->screen = DefaultScreen(dpy);
            be_req->visual =
                (unsigned int) glxMatchGLXVisualInConfigList(pGlxVisual,
                                                             dmxScreen->
                                                             glxVisuals,
                                                             dmxScreen->
                                                             numGlxVisuals);
            be_req->pixmap = (unsigned int) be_pixmap;
            be_req->glxpixmap = (unsigned int) pGlxPixmap->be_xids[s];
            UnlockDisplay(dpy);
            SyncHandle();
        }
        else {
            client->errorValue = (visual ? visual : fbconfigId);
            free(pGlxPixmap->be_xids);
            free(pGlxPixmap);
            return BadValue;
        }

        XFlush(dpy);
    }

    if (!(AddResource(glxpixmapId, __glXPixmapRes, pGlxPixmap))) {
        free(pGlxPixmap->be_xids);
        free(pGlxPixmap);
        return BadAlloc;
    }

    return Success;
}

int
__glXCreateGLXPixmap(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreateGLXPixmapReq *req = (xGLXCreateGLXPixmapReq *) pc;

    return (CreateGLXPixmap(cl, req->visual, None,
                            req->screen, req->pixmap, req->glxpixmap));
}

int
__glXCreatePixmap(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreatePixmapReq *req = (xGLXCreatePixmapReq *) pc;

    return (CreateGLXPixmap(cl, None, req->fbconfig,
                            req->screen, req->pixmap, req->glxpixmap));
}

int
__glXDestroyGLXPixmap(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXDestroyGLXPixmapReq *req = (xGLXDestroyGLXPixmapReq *) pc;
    XID glxpixmap = req->glxpixmap;
    __GLXpixmap *pGlxPixmap;
    int s;
    int from_screen, to_screen;

    /*
     ** Check if it's a valid GLX pixmap.
     */
    dixLookupResourceByType((void **) &pGlxPixmap, glxpixmap,
                            __glXPixmapRes, NullClient, DixUnknownAccess);
    if (!pGlxPixmap) {
        client->errorValue = glxpixmap;
        return __glXBadPixmap;
    }
    FreeResource(glxpixmap, FALSE);

    /*
     * destroy the pixmap on the back-end server(s).
     */
    from_screen = to_screen = pGlxPixmap->pDraw->pScreen->myNum;
#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;
    }
#endif

    for (s = from_screen; s <= to_screen; s++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[s];
        Display *dpy = GetBackEndDisplay(cl, s);

        /* make sure pixmap exist in back-end */
        dmxSync(dmxScreen, 1);

        LockDisplay(dpy);
        GetReq(GLXDestroyGLXPixmap, req);
        req->reqType = dmxScreen->glxMajorOpcode;
        req->glxCode = X_GLXDestroyGLXPixmap;
        req->glxpixmap = (unsigned int) pGlxPixmap->be_xids[s];
        UnlockDisplay(dpy);
        SyncHandle();
    }

    return Success;
}

/*****************************************************************************/

/*
** NOTE: There is no portable implementation for swap buffers as of
** this time that is of value.  Consequently, this code must be
** implemented by somebody other than SGI.
*/
int
__glXDoSwapBuffers(__GLXclientState * cl, XID drawId, GLXContextTag tag)
{
    ClientPtr client = cl->client;
    DrawablePtr pDraw;
    xGLXSwapBuffersReq *be_req;
    WindowPtr pWin = NULL;
    __GLXpixmap *pGlxPixmap = NULL;
    __GLXcontext *glxc = NULL;

#ifdef PANORAMIX
    PanoramiXRes *pXinDraw = NULL;
#endif
    __glXWindow *pGlxWindow = NULL;
    int from_screen = 0;
    int to_screen = 0;
    int s, rc;

    /*
     ** Check that the GLX drawable is valid.
     */
    rc = dixLookupDrawable(&pDraw, drawId, client, 0, DixWriteAccess);
    if (rc == Success) {
        from_screen = to_screen = pDraw->pScreen->myNum;

        if (pDraw->type == DRAWABLE_WINDOW) {
            /*
             ** Drawable is an X window.
             */
            pWin = (WindowPtr) pDraw;
        }
        else {
            /*
             ** Drawable is an X pixmap, which is not allowed.
             */
            client->errorValue = drawId;
            return __glXBadDrawable;
        }
    }

    if (!pDraw) {
        dixLookupResourceByType((void **) &pGlxPixmap, drawId,
                                __glXPixmapRes, NullClient, DixUnknownAccess);
        if (pGlxPixmap) {
            /*
             ** Drawable is a GLX pixmap.
             */
            pDraw = pGlxPixmap->pDraw;
            from_screen = to_screen = pGlxPixmap->pScreen->myNum;
        }
    }

    if (!pDraw && __GLX_IS_VERSION_SUPPORTED(1, 3)) {
        dixLookupResourceByType((void **) &pGlxWindow, drawId,
                                __glXWindowRes, NullClient, DixUnknownAccess);
        if (pGlxWindow) {
            /*
             ** Drawable is a GLXWindow.
             */
            pDraw = pGlxWindow->pDraw;
            from_screen = to_screen = pGlxWindow->pScreen->myNum;
        }
    }

    if (!pDraw) {
        /*
         ** Drawable is neither a X window nor a GLX pixmap.
         */
        client->errorValue = drawId;
        return __glXBadDrawable;
    }

    if (tag) {
        glxc = __glXLookupContextByTag(cl, tag);
        if (!glxc) {
            return __glXBadContextTag;
        }
    }

#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;
        dixLookupResourceByClass((void **) &pXinDraw,
                                 pDraw->id, XRC_DRAWABLE,
                                 client, DixReadAccess);
    }
#endif

    /* If requested, send a glFinish to all back-end servers before swapping. */
    if (dmxGLXFinishSwap) {
        for (s = from_screen; s <= to_screen; s++) {
            Display *dpy = GetBackEndDisplay(cl, s);
            DMXScreenInfo *dmxScreen = &dmxScreens[s];
            xGLXSingleReq *finishReq;
            xGLXSingleReply reply;

#define X_GLXSingle 0           /* needed by GetReq below */

            LockDisplay(dpy);
            GetReq(GLXSingle, finishReq);
            finishReq->reqType = dmxScreen->glxMajorOpcode;
            finishReq->glxCode = X_GLsop_Finish;
            finishReq->contextTag =
                (tag ? GetCurrentBackEndTag(cl, tag, s) : 0);
            (void) _XReply(dpy, (xReply *) &reply, 0, False);
            UnlockDisplay(dpy);
            SyncHandle();
        }
    }

    /* If requested, send an XSync to all back-end servers before swapping. */
    if (dmxGLXSyncSwap) {
        for (s = from_screen; s <= to_screen; s++)
            XSync(GetBackEndDisplay(cl, s), False);
    }

    /* send the SwapBuffers request to all back-end servers */

    for (s = from_screen; s <= to_screen; s++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[s];
        Display *dpy = GetBackEndDisplay(cl, s);
        unsigned int be_draw = 0;

        if (pGlxPixmap) {
            be_draw = (unsigned int) pGlxPixmap->be_xids[s];
        }
#ifdef PANORAMIX
        else if (pXinDraw) {
            dixLookupWindow(&pWin, pXinDraw->info[s].id, client, DixReadAccess);
        }
#endif
        else if (pGlxWindow) {
            pWin = (WindowPtr) pGlxWindow->pDraw;
        }

        if (pWin && !be_draw) {
            be_draw = (unsigned int) (DMX_GET_WINDOW_PRIV(pWin))->window;
            if (!be_draw) {
                /* it might be that the window did not created yet on the */
                /* back-end server (lazy window creation option), force   */
                /* creation of the window */
                dmxCreateAndRealizeWindow(pWin, TRUE);
                be_draw = (unsigned int) (DMX_GET_WINDOW_PRIV(pWin))->window;
            }
        }

        dmxSync(dmxScreen, 1);

        LockDisplay(dpy);
        GetReq(GLXSwapBuffers, be_req);
        be_req->reqType = dmxScreen->glxMajorOpcode;
        be_req->glxCode = X_GLXSwapBuffers;
        be_req->drawable = be_draw;
        be_req->contextTag = (tag ? GetCurrentBackEndTag(cl, tag, s) : 0);
        UnlockDisplay(dpy);
        SyncHandle();
        XFlush(dpy);
    }

    return Success;
}

int
__glXSwapBuffers(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    DrawablePtr pDraw;
    xGLXSwapBuffersReq *req = (xGLXSwapBuffersReq *) pc;
    GLXContextTag tag = req->contextTag;
    XID drawId = req->drawable;
    __GLXpixmap *pGlxPixmap = NULL;
    __GLXcontext *glxc = NULL;
    __glXWindow *pGlxWindow = NULL;
    int rc;

    /*
     ** Check that the GLX drawable is valid.
     */
    rc = dixLookupDrawable(&pDraw, drawId, client, 0, DixWriteAccess);
    if (rc == Success) {
        if (pDraw->type != DRAWABLE_WINDOW) {
            /*
             ** Drawable is an X pixmap, which is not allowed.
             */
            client->errorValue = drawId;
            return __glXBadDrawable;
        }
    }

    if (!pDraw) {
        dixLookupResourceByType((void **) &pGlxPixmap, drawId,
                                __glXPixmapRes, NullClient, DixUnknownAccess);
        if (pGlxPixmap) {
            /*
             ** Drawable is a GLX pixmap.
             */
            pDraw = pGlxPixmap->pDraw;
        }
    }

    if (!pDraw && __GLX_IS_VERSION_SUPPORTED(1, 3)) {
        dixLookupResourceByType((void **) &pGlxWindow, drawId,
                                __glXWindowRes, NullClient, DixUnknownAccess);
        if (pGlxWindow) {
            /*
             ** Drawable is a GLXWindow.
             */
            pDraw = pGlxWindow->pDraw;
        }
    }

    if (!pDraw) {
        /*
         ** Drawable is neither a X window nor a GLX pixmap.
         */
        client->errorValue = drawId;
        return __glXBadDrawable;
    }

    if (tag) {
        glxc = __glXLookupContextByTag(cl, tag);
        if (!glxc) {
            return __glXBadContextTag;
        }
    }

    if (pDraw &&
        pDraw->type == DRAWABLE_WINDOW &&
        DMX_GET_WINDOW_PRIV((WindowPtr) pDraw)->swapGroup) {
        return SGSwapBuffers(cl, drawId, tag, pDraw);
    }

    return __glXDoSwapBuffers(cl, drawId, tag);
}

/************************************************************************/

/*
** Render and Renderlarge are not in the GLX API.  They are used by the GLX
** client library to send batches of GL rendering commands.
*/

/*
** Execute all the drawing commands in a request.
*/
int
__glXRender(__GLXclientState * cl, GLbyte * pc)
{
    xGLXRenderReq *req;
    xGLXRenderReq *be_req;
    int size;
    __GLXcontext *glxc;
    int from_screen = 0;
    int to_screen = 0;
    int s;

    /*
     ** NOTE: much of this code also appears in the byteswapping version of this
     ** routine, __glXSwapRender().  Any changes made here should also be
     ** duplicated there.
     */

    req = (xGLXRenderReq *) pc;

    glxc = __glXLookupContextByTag(cl, req->contextTag);
    if (!glxc) {
        return 0;
    }
    from_screen = to_screen = glxc->pScreen->myNum;

#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;
    }
#endif

    pc += sz_xGLXRenderReq;
    size = (req->length << 2) - sz_xGLXRenderReq;

    /*
     * just forward the request to back-end server(s)
     */
    for (s = from_screen; s <= to_screen; s++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[s];
        Display *dpy = GetBackEndDisplay(cl, s);

        LockDisplay(dpy);
        GetReq(GLXRender, be_req);
        be_req->reqType = dmxScreen->glxMajorOpcode;
        be_req->glxCode = X_GLXRender;
        be_req->length = req->length;
        be_req->contextTag = GetCurrentBackEndTag(cl, req->contextTag, s);
        _XSend(dpy, (const char *) pc, size);
        UnlockDisplay(dpy);
        SyncHandle();
    }

    return Success;
}

/*
** Execute a large rendering request (one that spans multiple X requests).
*/
int
__glXRenderLarge(__GLXclientState * cl, GLbyte * pc)
{
    xGLXRenderLargeReq *req;
    xGLXRenderLargeReq *be_req;
    __GLXcontext *glxc;
    int from_screen = 0;
    int to_screen = 0;
    int s;

    /*
     ** NOTE: much of this code also appears in the byteswapping version of this
     ** routine, __glXSwapRenderLarge().  Any changes made here should also be
     ** duplicated there.
     */

    req = (xGLXRenderLargeReq *) pc;
    glxc = __glXLookupContextByTag(cl, req->contextTag);
    if (!glxc) {
        return 0;
    }
    from_screen = to_screen = glxc->pScreen->myNum;

#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;
    }
#endif

    pc += sz_xGLXRenderLargeReq;

    /*
     * just forward the request to back-end server(s)
     */
    for (s = from_screen; s <= to_screen; s++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[s];
        Display *dpy = GetBackEndDisplay(cl, s);

        GetReq(GLXRenderLarge, be_req);
        be_req->reqType = dmxScreen->glxMajorOpcode;
        be_req->glxCode = X_GLXRenderLarge;
        be_req->contextTag = GetCurrentBackEndTag(cl, req->contextTag, s);
        be_req->length = req->length;
        be_req->requestNumber = req->requestNumber;
        be_req->requestTotal = req->requestTotal;
        be_req->dataBytes = req->dataBytes;
        Data(dpy, (const char *) pc, req->dataBytes);
        UnlockDisplay(dpy);
        SyncHandle();

    }

    return Success;
}

/************************************************************************/

int
__glXVendorPrivate(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *req;

    req = (xGLXVendorPrivateReq *) pc;

    switch (req->vendorCode) {

    case X_GLvop_DeleteTexturesEXT:
        return __glXVForwardSingleReq(cl, pc);
        break;

    case X_GLXvop_SwapIntervalSGI:
        if (glxIsExtensionSupported("SGI_swap_control")) {
            return __glXVForwardSingleReq(cl, pc);
        }
        else {
            return Success;
        }
        break;

#if 0                           /* glx 1.3 */
    case X_GLXvop_CreateGLXVideoSourceSGIX:
        break;
    case X_GLXvop_DestroyGLXVideoSourceSGIX:
        break;
    case X_GLXvop_CreateGLXPixmapWithConfigSGIX:
        break;
    case X_GLXvop_DestroyGLXPbufferSGIX:
        break;
    case X_GLXvop_ChangeDrawableAttributesSGIX:
        break;
#endif

    case X_GLXvop_BindSwapBarrierSGIX:
        return __glXBindSwapBarrierSGIX(cl, pc);
        break;

    case X_GLXvop_JoinSwapGroupSGIX:
        return __glXJoinSwapGroupSGIX(cl, pc);
        break;

    case X_GLXvop_CreateContextWithConfigSGIX:
        return __glXCreateContextWithConfigSGIX(cl, pc);
        break;

    default:
        /*
         ** unsupported private request
         */
        cl->client->errorValue = req->vendorCode;
        return __glXUnsupportedPrivateRequest;
    }

    cl->client->errorValue = req->vendorCode;
    return __glXUnsupportedPrivateRequest;

}

int
__glXVendorPrivateWithReply(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateWithReplyReq *req;

    req = (xGLXVendorPrivateWithReplyReq *) pc;

    switch (req->vendorCode) {

    case X_GLvop_GetConvolutionFilterEXT:
    case X_GLvop_GetConvolutionParameterfvEXT:
    case X_GLvop_GetConvolutionParameterivEXT:
    case X_GLvop_GetSeparableFilterEXT:
    case X_GLvop_GetHistogramEXT:
    case X_GLvop_GetHistogramParameterivEXT:
    case X_GLvop_GetMinmaxEXT:
    case X_GLvop_GetMinmaxParameterfvEXT:
    case X_GLvop_GetMinmaxParameterivEXT:
    case X_GLvop_AreTexturesResidentEXT:
    case X_GLvop_IsTextureEXT:
        return (__glXVForwardPipe0WithReply(cl, pc));
        break;

    case X_GLvop_GenTexturesEXT:
        return (__glXVForwardAllWithReply(cl, pc));
        break;

#if 0                           /* glx1.3 */
    case X_GLvop_GetDetailTexFuncSGIS:
    case X_GLvop_GetSharpenTexFuncSGIS:
    case X_GLvop_GetColorTableSGI:
    case X_GLvop_GetColorTableParameterfvSGI:
    case X_GLvop_GetColorTableParameterivSGI:
    case X_GLvop_GetTexFilterFuncSGIS:
    case X_GLvop_GetInstrumentsSGIX:
    case X_GLvop_InstrumentsBufferSGIX:
    case X_GLvop_PollInstrumentsSGIX:
    case X_GLvop_FlushRasterSGIX:
    case X_GLXvop_CreateGLXPbufferSGIX:
    case X_GLXvop_GetDrawableAttributesSGIX:
    case X_GLXvop_QueryHyperpipeNetworkSGIX:
    case X_GLXvop_QueryHyperpipeConfigSGIX:
    case X_GLXvop_HyperpipeConfigSGIX:
    case X_GLXvop_DestroyHyperpipeConfigSGIX:
#endif
    case X_GLXvop_QueryMaxSwapBarriersSGIX:
        return (__glXQueryMaxSwapBarriersSGIX(cl, pc));
        break;

    case X_GLXvop_GetFBConfigsSGIX:
        return (__glXGetFBConfigsSGIX(cl, pc));
        break;

    case X_GLXvop_MakeCurrentReadSGI:
        return (__glXMakeCurrentReadSGI(cl, pc));
        break;

    case X_GLXvop_QueryContextInfoEXT:
        return (__glXQueryContextInfoEXT(cl, pc));
        break;

    default:
        /*
         ** unsupported private request
         */
        cl->client->errorValue = req->vendorCode;
        return __glXUnsupportedPrivateRequest;
    }

}

int
__glXQueryExtensionsString(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXQueryExtensionsStringReq *req = (xGLXQueryExtensionsStringReq *) pc;
    xGLXQueryExtensionsStringReply reply;
    GLint screen;
    size_t length;
    int len, numbytes;
    char *be_buf;

#ifdef FWD_QUERY_REQ
    xGLXQueryExtensionsStringReq *be_req;
    xGLXQueryExtensionsStringReply be_reply;
    DMXScreenInfo *dmxScreen;
    Display *dpy;
#endif

    screen = req->screen;

    /*
     ** Check if screen exists.
     */
    if ((screen < 0) || (screen >= screenInfo.numScreens)) {
        client->errorValue = screen;
        return BadValue;
    }

#ifdef FWD_QUERY_REQ
    dmxScreen = &dmxScreens[screen];

    /* Send the glXQueryServerString request */
    dpy = GetBackEndDisplay(cl, screen);
    LockDisplay(dpy);
    GetReq(GLXQueryExtensionsString, be_req);
    be_req->reqType = dmxScreen->glxMajorOpcode;
    be_req->glxCode = X_GLXQueryServerString;
    be_req->screen = DefaultScreen(dpy);
    _XReply(dpy, (xReply *) &be_reply, 0, False);
    len = (int) be_reply.length;
    numbytes = (int) be_reply.n;
    be_buf = (char *) malloc(numbytes);
    if (!be_buf) {
        /* Throw data on the floor */
        _XEatDataWords(dpy, len);
    }
    else {
        _XReadPad(dpy, (char *) be_buf, numbytes);
    }
    UnlockDisplay(dpy);
    SyncHandle();

#else

    be_buf = __glXGetServerString(GLX_EXTENSIONS);
    numbytes = strlen(be_buf) + 1;
    len = __GLX_PAD(numbytes) >> 2;

#endif

    length = len;
    reply = (xGLXQueryExtensionsStringReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = len,
        .n = numbytes
    };

    if (client->swapped) {
        glxSwapQueryExtensionsStringReply(client, &reply, be_buf);
    }
    else {
        WriteToClient(client, sz_xGLXQueryExtensionsStringReply, &reply);
        WriteToClient(client, (int) (length << 2), be_buf);
    }

    return Success;
}

int
__glXQueryServerString(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXQueryServerStringReq *req = (xGLXQueryServerStringReq *) pc;
    xGLXQueryServerStringReply reply;
    int name;
    GLint screen;
    size_t length;
    int len, numbytes;
    char *be_buf;

#ifdef FWD_QUERY_REQ
    xGLXQueryServerStringReq *be_req;
    xGLXQueryServerStringReply be_reply;
    DMXScreenInfo *dmxScreen;
    Display *dpy;
#endif

    name = req->name;
    screen = req->screen;
    /*
     ** Check if screen exists.
     */
    if ((screen < 0) || (screen >= screenInfo.numScreens)) {
        client->errorValue = screen;
        return BadValue;
    }

#ifdef FWD_QUERY_REQ
    dmxScreen = &dmxScreens[screen];

    /* Send the glXQueryServerString request */
    dpy = GetBackEndDisplay(cl, screen);
    LockDisplay(dpy);
    GetReq(GLXQueryServerString, be_req);
    be_req->reqType = dmxScreen->glxMajorOpcode;
    be_req->glxCode = X_GLXQueryServerString;
    be_req->screen = DefaultScreen(dpy);
    be_req->name = name;
    _XReply(dpy, (xReply *) &be_reply, 0, False);
    len = (int) be_reply.length;
    numbytes = (int) be_reply.n;
    be_buf = (char *) malloc(numbytes);
    if (!be_buf) {
        /* Throw data on the floor */
        _XEatDataWords(dpy, len);
    }
    else {
        _XReadPad(dpy, (char *) be_buf, numbytes);
    }
    UnlockDisplay(dpy);
    SyncHandle();

#else
    be_buf = __glXGetServerString(name);
    numbytes = strlen(be_buf) + 1;
    len = __GLX_PAD(numbytes) >> 2;
#endif

    length = len;
    reply = (xGLXQueryServerStringReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = length,
        .n = numbytes
    };

    if (client->swapped) {
        glxSwapQueryServerStringReply(client, &reply, be_buf);
    }
    else {
        WriteToClient(client, sz_xGLXQueryServerStringReply, &reply);
        WriteToClient(client, (int) (length << 2), be_buf);
    }

    return Success;
}

int
__glXClientInfo(__GLXclientState * cl, GLbyte * pc)
{
    xGLXClientInfoReq *req = (xGLXClientInfoReq *) pc;
    xGLXClientInfoReq *be_req;
    const char *buf;
    int from_screen = 0;
    int to_screen = 0;
    int s;

    free(cl->GLClientextensions);
    buf = (const char *) (req + 1);
    cl->GLClientextensions = strdup(buf);

    to_screen = screenInfo.numScreens - 1;

    for (s = from_screen; s <= to_screen; s++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[s];
        Display *dpy = GetBackEndDisplay(cl, s);

        LockDisplay(dpy);
        GetReq(GLXClientInfo, be_req);
        be_req->reqType = dmxScreen->glxMajorOpcode;
        be_req->glxCode = X_GLXClientInfo;
        be_req->major = req->major;
        be_req->minor = req->minor;
        be_req->length = req->length;
        be_req->numbytes = req->numbytes;
        Data(dpy, buf, req->numbytes);

        UnlockDisplay(dpy);
        SyncHandle();
    }

    return Success;
}

int
__glXUseXFont(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXUseXFontReq *req;
    xGLXUseXFontReq *be_req;
    FontPtr pFont;
    __GLXcontext *glxc = NULL;
    int from_screen = 0;
    int to_screen = 0;
    int s;
    dmxFontPrivPtr pFontPriv;
    DMXScreenInfo *dmxScreen;
    Display *dpy;

    req = (xGLXUseXFontReq *) pc;

    if (req->contextTag != 0) {
        glxc = __glXLookupContextByTag(cl, req->contextTag);
        if (glxc) {
            from_screen = to_screen = glxc->pScreen->myNum;
        }
    }

    /*
     ** Font can actually be either the ID of a font or the ID of a GC
     ** containing a font.
     */
    dixLookupResourceByType((void **) &pFont, req->font, RT_FONT,
                            NullClient, DixUnknownAccess);
    if (!pFont) {
        GC *pGC;

        dixLookupResourceByType((void **) &pGC, req->font,
                                RT_GC, NullClient, DixUnknownAccess);
        if (!pGC) {
            client->errorValue = req->font;
            return BadFont;
        }
        pFont = pGC->font;
    }

    pFontPriv = FontGetPrivate(pFont, dmxFontPrivateIndex);

#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;
    }
#endif

    for (s = from_screen; s <= to_screen; s++) {
        dmxScreen = &dmxScreens[s];
        dpy = GetBackEndDisplay(cl, s);

        dmxSync(dmxScreen, 1);

        LockDisplay(dpy);
        GetReq(GLXUseXFont, be_req);
        be_req->reqType = dmxScreen->glxMajorOpcode;
        be_req->glxCode = X_GLXUseXFont;
        be_req->contextTag =
            (glxc ? GetCurrentBackEndTag(cl, req->contextTag, s) : 0);
        be_req->font = pFontPriv->font[s]->fid;
        be_req->first = req->first;
        be_req->count = req->count;
        be_req->listBase = req->listBase;
        UnlockDisplay(dpy);
        SyncHandle();

        XSync(dpy, False);
    }

    return Success;
}

/*
 * start GLX 1.3 here
 */

int
__glXGetFBConfigs(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXGetFBConfigsReq *req = (xGLXGetFBConfigsReq *) pc;
    xGLXGetFBConfigsReply reply;
    __GLXFBConfig *pFBConfig;
    CARD32 buf[2 * __GLX_TOTAL_FBCONFIG_PROPS];
    int numAttribs = __GLX_TOTAL_FBCONFIG_PROPS;
    unsigned int screen = req->screen;
    int numFBConfigs, i, p;
    __GLXscreenInfo *pGlxScreen;

    if (screen >= screenInfo.numScreens) {
        /* The client library must send a valid screen number. */
        client->errorValue = screen;
        return BadValue;
    }

    pGlxScreen = &__glXActiveScreens[screen];
    numFBConfigs = __glXNumFBConfigs;

    reply = (xGLXGetFBConfigsReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = (numFBConfigs * 2 * numAttribs * __GLX_SIZE_CARD32) >> 2,
        .numFBConfigs = numFBConfigs,
        .numAttribs = numAttribs
    };

    if (client->swapped) {
        __GLX_DECLARE_SWAP_VARIABLES;
        __GLX_SWAP_SHORT(&reply.sequenceNumber);
        __GLX_SWAP_INT(&reply.length);
        __GLX_SWAP_INT(&reply.numFBConfigs);
        __GLX_SWAP_INT(&reply.numAttribs);
    }
    WriteToClient(client, sz_xGLXGetFBConfigsReply, &reply);

    for (i = 0; i < numFBConfigs; i++) {
        int associatedVisualId = 0;
        int drawableTypeIndex;

        pFBConfig = __glXFBConfigs[i * (screenInfo.numScreens + 1)];

        p = 0;
        /* core attributes */
        buf[p++] = GLX_FBCONFIG_ID;
        buf[p++] = pFBConfig->id;
        buf[p++] = GLX_BUFFER_SIZE;
        buf[p++] = pFBConfig->indexBits;
        buf[p++] = GLX_LEVEL;
        buf[p++] = pFBConfig->level;
        buf[p++] = GLX_DOUBLEBUFFER;
        buf[p++] = pFBConfig->doubleBufferMode;
        buf[p++] = GLX_STEREO;
        buf[p++] = pFBConfig->stereoMode;
        buf[p++] = GLX_AUX_BUFFERS;
        buf[p++] = pFBConfig->maxAuxBuffers;
        buf[p++] = GLX_RED_SIZE;
        buf[p++] = pFBConfig->redBits;
        buf[p++] = GLX_GREEN_SIZE;
        buf[p++] = pFBConfig->greenBits;
        buf[p++] = GLX_BLUE_SIZE;
        buf[p++] = pFBConfig->blueBits;
        buf[p++] = GLX_ALPHA_SIZE;
        buf[p++] = pFBConfig->alphaBits;
        buf[p++] = GLX_DEPTH_SIZE;
        buf[p++] = pFBConfig->depthBits;
        buf[p++] = GLX_STENCIL_SIZE;
        buf[p++] = pFBConfig->stencilBits;
        buf[p++] = GLX_ACCUM_RED_SIZE;
        buf[p++] = pFBConfig->accumRedBits;
        buf[p++] = GLX_ACCUM_GREEN_SIZE;
        buf[p++] = pFBConfig->accumGreenBits;
        buf[p++] = GLX_ACCUM_BLUE_SIZE;
        buf[p++] = pFBConfig->accumBlueBits;
        buf[p++] = GLX_ACCUM_ALPHA_SIZE;
        buf[p++] = pFBConfig->accumAlphaBits;
        buf[p++] = GLX_RENDER_TYPE;
        buf[p++] = pFBConfig->renderType;
        buf[p++] = GLX_DRAWABLE_TYPE;
        drawableTypeIndex = p;
        buf[p++] = pFBConfig->drawableType;
        buf[p++] = GLX_X_VISUAL_TYPE;
        buf[p++] = pFBConfig->visualType;
        buf[p++] = GLX_CONFIG_CAVEAT;
        buf[p++] = pFBConfig->visualCaveat;
        buf[p++] = GLX_TRANSPARENT_TYPE;
        buf[p++] = pFBConfig->transparentType;
        buf[p++] = GLX_TRANSPARENT_RED_VALUE;
        buf[p++] = pFBConfig->transparentRed;
        buf[p++] = GLX_TRANSPARENT_GREEN_VALUE;
        buf[p++] = pFBConfig->transparentGreen;
        buf[p++] = GLX_TRANSPARENT_BLUE_VALUE;
        buf[p++] = pFBConfig->transparentBlue;
        buf[p++] = GLX_TRANSPARENT_ALPHA_VALUE;
        buf[p++] = pFBConfig->transparentAlpha;
        buf[p++] = GLX_TRANSPARENT_INDEX_VALUE;
        buf[p++] = pFBConfig->transparentIndex;
        buf[p++] = GLX_MAX_PBUFFER_WIDTH;
        buf[p++] = pFBConfig->maxPbufferWidth;
        buf[p++] = GLX_MAX_PBUFFER_HEIGHT;
        buf[p++] = pFBConfig->maxPbufferHeight;
        buf[p++] = GLX_MAX_PBUFFER_PIXELS;
        buf[p++] = pFBConfig->maxPbufferPixels;

        /*
         * find the visual of the back-end server and match a visual
         * on the proxy.
         * do only once - if a visual is not yet associated.
         */
        if (pFBConfig->associatedVisualId == (unsigned int) -1) {
            DMXScreenInfo *dmxScreen = &dmxScreens[screen];
            __GLXFBConfig *be_pFBConfig =
                __glXFBConfigs[i * (screenInfo.numScreens + 1) + screen + 1];
            __GLXvisualConfig *pGlxVisual = NULL;
            int v;
            int found = 0;

            for (v = 0; v < dmxScreen->numGlxVisuals; v++) {
                if (dmxScreen->glxVisuals[v].vid ==
                    be_pFBConfig->associatedVisualId) {
                    pGlxVisual = &dmxScreen->glxVisuals[v];
                    break;
                }
            }

            if (pGlxVisual) {
                for (v = 0; v < pGlxScreen->numVisuals; v++) {
                    if (glxVisualsMatch(&pGlxScreen->pGlxVisual[v], pGlxVisual)) {
                        associatedVisualId = pGlxScreen->pGlxVisual[v].vid;
                        found = 1;
                        break;
                    }
                }
            }

            if (!found) {
                associatedVisualId = 0;
                pFBConfig->drawableType &= ~(GLX_WINDOW_BIT);
                buf[drawableTypeIndex] = pFBConfig->drawableType;
            }
#ifdef PANORAMIX
            else if (!noPanoramiXExtension) {
                /* convert the associated visualId to the panoramix one */
                pFBConfig->associatedVisualId =
                    PanoramiXTranslateVisualID(screen, v);
            }
#endif
        }
        else {
            associatedVisualId = pFBConfig->associatedVisualId;
        }

        buf[p++] = GLX_VISUAL_ID;
        buf[p++] = associatedVisualId;

        /* SGIS_multisample attributes */
        buf[p++] = GLX_SAMPLES_SGIS;
        buf[p++] = pFBConfig->multiSampleSize;
        buf[p++] = GLX_SAMPLE_BUFFERS_SGIS;
        buf[p++] = pFBConfig->nMultiSampleBuffers;

        /* SGIX_pbuffer specific attributes */
        buf[p++] = GLX_OPTIMAL_PBUFFER_WIDTH_SGIX;
        buf[p++] = pFBConfig->optimalPbufferWidth;
        buf[p++] = GLX_OPTIMAL_PBUFFER_HEIGHT_SGIX;
        buf[p++] = pFBConfig->optimalPbufferHeight;

        buf[p++] = GLX_VISUAL_SELECT_GROUP_SGIX;
        buf[p++] = pFBConfig->visualSelectGroup;

        if (client->swapped) {
            __GLX_DECLARE_SWAP_VARIABLES;
            __GLX_DECLARE_SWAP_ARRAY_VARIABLES;
            __GLX_SWAP_INT_ARRAY((int *) buf, 2 * numAttribs);
        }
        WriteToClient(client, 2 * numAttribs * __GLX_SIZE_CARD32, buf);
    }
    return Success;
}

int
__glXGetFBConfigsSGIX(__GLXclientState * cl, GLbyte * pc)
{
    xGLXGetFBConfigsSGIXReq *req = (xGLXGetFBConfigsSGIXReq *) pc;
    xGLXGetFBConfigsReq new_req;

    new_req.reqType = req->reqType;
    new_req.glxCode = req->glxCode;
    new_req.length = req->length;
    new_req.screen = req->screen;

    return (__glXGetFBConfigs(cl, (GLbyte *) &new_req));
}

int
__glXCreateWindow(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXCreateWindowReq *req = (xGLXCreateWindowReq *) pc;
    int screen = req->screen;
    GLXFBConfigID fbconfigId = req->fbconfig;
    XID windowId = req->window;
    XID glxwindowId = req->glxwindow;
    DrawablePtr pDraw;
    ScreenPtr pScreen;
    __glXWindow *pGlxWindow;
    __GLXFBConfig *pGlxFBConfig = NULL;
    VisualPtr pVisual;
    VisualID visId;
    int i, rc;
    void *val;

    /*
     ** Check if windowId is valid
     */
    rc = dixLookupDrawable(&pDraw, windowId, client, M_DRAWABLE_WINDOW,
                           DixAddAccess);
    if (rc != Success)
        return rc;

    /*
     ** Check if screen of window matches screen of fbconfig.
     */
    pScreen = pDraw->pScreen;
    if (screen != pScreen->myNum) {
        return BadMatch;
    }

    /*
     ** Find the FBConfigRec for this fbconfigid.
     */
    if (!(pGlxFBConfig = glxLookupFBConfig(fbconfigId))) {
        client->errorValue = fbconfigId;
        return __glXBadFBConfig;
    }
    visId = pGlxFBConfig->associatedVisualId;

    /*
     ** Check if the fbconfig supports rendering to windows
     */
    if (!(pGlxFBConfig->drawableType & GLX_WINDOW_BIT)) {
        return BadMatch;
    }

    if (visId != None) {
        /*
         ** Check if the visual ID is valid for this screen.
         */
        pVisual = pScreen->visuals;
        for (i = 0; i < pScreen->numVisuals; i++, pVisual++) {
            if (pVisual->vid == visId) {
                break;
            }
        }
        if (i == pScreen->numVisuals) {
            client->errorValue = visId;
            return BadValue;
        }

        /*
         ** Check if color buffer depth of fbconfig matches depth
         ** of window.
         */
        if (pVisual->nplanes != pDraw->depth) {
            return BadMatch;
        }
    }
    else
        /*
         ** The window was created with no visual that corresponds
         ** to fbconfig
         */
        return BadMatch;

    /*
     ** Check if there is already a fbconfig associated with this window
     */
    if (Success == dixLookupResourceByType(&val,
                                           glxwindowId, __glXWindowRes,
                                           NullClient, DixUnknownAccess)) {
        client->errorValue = glxwindowId;
        return BadAlloc;
    }

    pGlxWindow = (__glXWindow *) malloc(sizeof(__glXWindow));
    if (!pGlxWindow) {
        return BadAlloc;
    }

    /*
     ** Register this GLX window as a resource
     */
    if (!(AddResource(glxwindowId, __glXWindowRes, pGlxWindow))) {
        return BadAlloc;
    }

    pGlxWindow->pDraw = pDraw;
    pGlxWindow->type = GLX_GLXWINDOW_TYPE;
    pGlxWindow->idExists = True;
    pGlxWindow->refcnt = 0;
    pGlxWindow->pGlxFBConfig = pGlxFBConfig;
    pGlxWindow->pScreen = pScreen;

    return Success;
}

int
__glXDestroyWindow(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXDestroyWindowReq *req = (xGLXDestroyWindowReq *) pc;
    XID glxwindow = req->glxwindow;
    void *val;

    /*
     ** Check if it's a valid GLX window.
     */
    if (Success != dixLookupResourceByType(&val,
                                           glxwindow, __glXWindowRes,
                                           NullClient, DixUnknownAccess)) {
        client->errorValue = glxwindow;
        return __glXBadDrawable;
    }
    /*
     ** The glx window destructor will check whether it's current before
     ** freeing anything.
     */
    FreeResource(glxwindow, RT_NONE);

    return Success;
}

int
__glXQueryContext(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    __GLXcontext *ctx;
    xGLXQueryContextReq *req;
    xGLXQueryContextReply reply;
    int nProps;
    int *sendBuf, *pSendBuf;
    int nReplyBytes;

    req = (xGLXQueryContextReq *) pc;
    dixLookupResourceByType((void **) &ctx, req->context, __glXContextRes,
                            NullClient, DixUnknownAccess);
    if (!ctx) {
        client->errorValue = req->context;
        return __glXBadContext;
    }

    nProps = 3;

    reply = (xGLXQueryContextReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = nProps << 1,
        .n = nProps
    };

    nReplyBytes = reply.length << 2;
    sendBuf = (int *) malloc(nReplyBytes);
    pSendBuf = sendBuf;
    *pSendBuf++ = GLX_FBCONFIG_ID;
    *pSendBuf++ = (int) (ctx->pFBConfig->id);
    *pSendBuf++ = GLX_RENDER_TYPE;
    *pSendBuf++ = renderTypeBitsToRenderTypeEnum(ctx->pFBConfig->renderType);
    *pSendBuf++ = GLX_SCREEN;
    *pSendBuf++ = (int) (ctx->pScreen->myNum);

    if (client->swapped) {
        __glXSwapQueryContextReply(client, &reply, sendBuf);
    }
    else {
        WriteToClient(client, sz_xGLXQueryContextReply, &reply);
        WriteToClient(client, nReplyBytes, sendBuf);
    }
    free((char *) sendBuf);

    return Success;
}

int
__glXQueryContextInfoEXT(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    __GLXcontext *ctx;
    xGLXQueryContextInfoEXTReq *req;
    xGLXQueryContextInfoEXTReply reply;
    int nProps;
    int *sendBuf, *pSendBuf;
    int nReplyBytes;

    req = (xGLXQueryContextInfoEXTReq *) pc;
    dixLookupResourceByType((void **) &ctx,
                            req->context, __glXContextRes,
                            client, DixReadAccess);

    if (!ctx) {
        client->errorValue = req->context;
        return __glXBadContext;
    }

    nProps = 4;

    reply = (xGLXQueryContextInfoEXTReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = nProps << 1,
        .n = nProps
    };

    nReplyBytes = reply.length << 2;
    sendBuf = (int *) malloc(nReplyBytes);
    pSendBuf = sendBuf;
    *pSendBuf++ = GLX_SHARE_CONTEXT_EXT;
    *pSendBuf++ = (int) (ctx->share_id);
    *pSendBuf++ = GLX_VISUAL_ID_EXT;
    *pSendBuf++ = (int) (ctx->pVisual ? ctx->pVisual->vid : 0);
    *pSendBuf++ = GLX_SCREEN_EXT;
    *pSendBuf++ = (int) (ctx->pScreen->myNum);
    *pSendBuf++ = GLX_FBCONFIG_ID;
    *pSendBuf++ = (int) (ctx->pFBConfig ? ctx->pFBConfig->id : 0);

    if (client->swapped) {
        __glXSwapQueryContextInfoEXTReply(client, &reply, sendBuf);
    }
    else {
        WriteToClient(client, sz_xGLXQueryContextInfoEXTReply, &reply);
        WriteToClient(client, nReplyBytes, sendBuf);
    }
    free((char *) sendBuf);

    return Success;
}

int
__glXCreatePbuffer(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXCreatePbufferReq *req = (xGLXCreatePbufferReq *) pc;
    xGLXCreatePbufferReq *be_req;
    int screen = req->screen;
    GLXFBConfigID fbconfigId = req->fbconfig;
    GLXPbuffer pbuffer = req->pbuffer;
    __glXPbuffer *pGlxPbuffer;
    int numAttribs = req->numAttribs;
    int *attr;
    ScreenPtr pScreen;
    __GLXFBConfig *pGlxFBConfig;
    __GLXFBConfig *be_pGlxFBConfig;
    XID be_xid;
    Display *dpy;
    DMXScreenInfo *dmxScreen;
    int s;
    int from_screen, to_screen;

    /*
     ** Look up screen and FBConfig.
     */
    if (screen >= screenInfo.numScreens) {
        /* The client library must send a valid screen number. */
        client->errorValue = screen;
        return BadValue;
    }
    pScreen = screenInfo.screens[screen];

    /*
     ** Find the FBConfigRec for this fbconfigid.
     */
    if (!(pGlxFBConfig = glxLookupFBConfig(fbconfigId))) {
        client->errorValue = fbconfigId;
        return __glXBadFBConfig;
    }

    /*
     ** Create the GLX part of the Pbuffer.
     */
    pGlxPbuffer = (__glXPbuffer *) malloc(sizeof(__glXPbuffer));
    if (!pGlxPbuffer) {
        return BadAlloc;
    }

    pGlxPbuffer->be_xids = (XID *) malloc(sizeof(XID) * screenInfo.numScreens);
    if (!pGlxPbuffer->be_xids) {
        free(pGlxPbuffer);
        return BadAlloc;
    }

    /*
     * Allocate an XID on the back-end server(s) and send him the request
     */
    from_screen = to_screen = screen;
#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;
    }
#endif

    for (s = from_screen; s <= to_screen; s++) {
        dpy = GetBackEndDisplay(cl, s);
        be_xid = XAllocID(dpy);
        dmxScreen = &dmxScreens[s];
        be_pGlxFBConfig = glxLookupBackEndFBConfig(pGlxFBConfig->id, s);

        attr = (int *) (req + 1);

        LockDisplay(dpy);
        GetReqExtra(GLXCreatePbuffer, 2 * numAttribs * __GLX_SIZE_CARD32,
                    be_req);
        be_req->reqType = dmxScreen->glxMajorOpcode;
        be_req->glxCode = X_GLXCreatePbuffer;
        be_req->screen = be_pGlxFBConfig->screen;
        be_req->fbconfig = be_pGlxFBConfig->id;
        be_req->pbuffer = be_xid;
        be_req->numAttribs = numAttribs;

        /* Send attributes */
        if (attr != NULL) {
            CARD32 *pc = (CARD32 *) (be_req + 1);

            while (numAttribs-- > 0) {
                *pc++ = *attr++;        /* token */
                *pc++ = *attr++;        /* value */
            }
        }

        UnlockDisplay(dpy);
        SyncHandle();

        pGlxPbuffer->be_xids[s] = be_xid;
    }

    pGlxPbuffer->idExists = True;
    pGlxPbuffer->refcnt = 0;
    pGlxPbuffer->pFBConfig = pGlxFBConfig;
    pGlxPbuffer->pScreen = pScreen;

    /*
     ** Register the resource.
     */
    if (!(AddResource(pbuffer, __glXPbufferRes, pGlxPbuffer))) {
        return BadAlloc;
    }

    return Success;

}

int
__glXDestroyPbuffer(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXDestroyPbufferReq *req = (xGLXDestroyPbufferReq *) pc;
    xGLXDestroyPbufferReq *be_req;
    GLXPbuffer pbuffer = req->pbuffer;
    Display *dpy;
    int screen;
    DMXScreenInfo *dmxScreen;
    __glXPbuffer *pGlxPbuffer;
    int s;
    int from_screen, to_screen;

    /*
     ** Check if it's a valid Pbuffer
     */
    dixLookupResourceByType((void **) &pGlxPbuffer, pbuffer,
                            __glXPbufferRes, NullClient, DixUnknownAccess);
    if (!pGlxPbuffer) {
        client->errorValue = pbuffer;
        return __glXBadPbuffer;
    }

    screen = pGlxPbuffer->pScreen->myNum;

    from_screen = to_screen = screen;
#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;
    }
#endif

    for (s = from_screen; s <= to_screen; s++) {
        dpy = GetBackEndDisplay(cl, s);
        dmxScreen = &dmxScreens[s];

        /* send the destroy request to the back-end server */
        LockDisplay(dpy);
        GetReq(GLXDestroyPbuffer, be_req);
        be_req->reqType = dmxScreen->glxMajorOpcode;
        be_req->glxCode = X_GLXDestroyPbuffer;
        be_req->pbuffer = pGlxPbuffer->be_xids[s];
        UnlockDisplay(dpy);
        SyncHandle();
    }

    FreeResource(pbuffer, RT_NONE);

    return Success;
}

int
__glXGetDrawableAttributes(__GLXclientState * cl, GLbyte * pc)
{
    xGLXGetDrawableAttributesReq *req = (xGLXGetDrawableAttributesReq *) pc;
    xGLXGetDrawableAttributesReq *be_req;
    xGLXGetDrawableAttributesReply reply;
    ClientPtr client = cl->client;
    GLXDrawable drawId = req->drawable;
    GLXDrawable be_drawable = 0;
    DrawablePtr pDraw = NULL;
    Display *dpy;
    int screen, rc;
    DMXScreenInfo *dmxScreen;
    CARD32 *attribs = NULL;
    int attribs_size = 0;

#ifdef PANORAMIX
    PanoramiXRes *pXinDraw = NULL;
#endif

    if (drawId != None) {
        rc = dixLookupDrawable(&pDraw, drawId, client, 0, DixGetAttrAccess);
        if (rc == Success && pDraw->type == DRAWABLE_WINDOW) {
            WindowPtr pWin = (WindowPtr) pDraw;

            be_drawable = 0;
            screen = pWin->drawable.pScreen->myNum;
        }
        else {
            /*
             ** Drawable is not a Window , GLXWindow or a GLXPixmap.
             */
            client->errorValue = drawId;
            return __glXBadDrawable;
        }

        if (!pDraw) {
            __GLXpixmap *pGlxPixmap;

            dixLookupResourceByType((void **) &pGlxPixmap,
                                    drawId, __glXPixmapRes,
                                    NullClient, DixUnknownAccess);
            if (pGlxPixmap) {
                pDraw = pGlxPixmap->pDraw;
                screen = pGlxPixmap->pScreen->myNum;
                be_drawable = pGlxPixmap->be_xids[screen];
            }
        }

        if (!pDraw) {
            __glXWindow *pGlxWindow;

            dixLookupResourceByType((void **) &pGlxWindow,
                                    drawId, __glXWindowRes,
                                    NullClient, DixUnknownAccess);
            if (pGlxWindow) {
                pDraw = pGlxWindow->pDraw;
                screen = pGlxWindow->pScreen->myNum;
                be_drawable = 0;
            }
        }

        if (!pDraw) {
            __glXPbuffer *pGlxPbuffer;

            dixLookupResourceByType((void **) &pGlxPbuffer,
                                    drawId, __glXPbufferRes,
                                    NullClient, DixUnknownAccess);
            if (pGlxPbuffer) {
                pDraw = (DrawablePtr) pGlxPbuffer;
                screen = pGlxPbuffer->pScreen->myNum;
                be_drawable = pGlxPbuffer->be_xids[screen];
            }
        }
    }

    if (!pDraw) {
        /*
         ** Drawable is not a Window , GLXWindow or a GLXPixmap.
         */
        client->errorValue = drawId;
        return __glXBadDrawable;
    }

    /* if the drawable is a window or GLXWindow -
     * we need to find the base id on the back-end server
     */
    if (!be_drawable) {
        WindowPtr pWin = (WindowPtr) pDraw;

#ifdef PANORAMIX
        if (!noPanoramiXExtension) {
            if (Success != dixLookupResourceByClass((void **) &pXinDraw,
                                                    pDraw->id, XRC_DRAWABLE,
                                                    client, DixReadAccess)) {
                client->errorValue = drawId;
                return __glXBadDrawable;
            }

            dixLookupWindow(&pWin, pXinDraw->info[screen].id, client,
                            DixReadAccess);
        }
#endif

        if (pWin) {
            be_drawable = (unsigned int) (DMX_GET_WINDOW_PRIV(pWin))->window;
            if (!be_drawable) {
                /* it might be that the window did not created yet on the */
                /* back-end server (lazy window creation option), force   */
                /* creation of the window */
                dmxCreateAndRealizeWindow(pWin, TRUE);
                be_drawable =
                    (unsigned int) (DMX_GET_WINDOW_PRIV(pWin))->window;
            }
        }
        else {
            client->errorValue = drawId;
            return __glXBadDrawable;
        }
    }

    /* send the request to the back-end server */
    dpy = GetBackEndDisplay(cl, screen);
    dmxScreen = &dmxScreens[screen];

    /* make sure drawable exists on back-end */
    dmxSync(dmxScreen, 1);

    LockDisplay(dpy);
    GetReq(GLXGetDrawableAttributes, be_req);
    be_req->reqType = dmxScreen->glxMajorOpcode;
    be_req->glxCode = X_GLXGetDrawableAttributes;
    be_req->drawable = be_drawable;
    be_req->length = req->length;
    if (!_XReply(dpy, (xReply *) &reply, 0, False)) {
        UnlockDisplay(dpy);
        SyncHandle();
        return (BE_TO_CLIENT_ERROR(dmxLastErrorEvent.error_code));
    }

    if (reply.numAttribs) {
        attribs_size = 2 * reply.numAttribs * __GLX_SIZE_CARD32;
        attribs = (CARD32 *) malloc(attribs_size);
        if (attribs == NULL) {
            UnlockDisplay(dpy);
            SyncHandle();
            return BadAlloc;
        }

        _XRead(dpy, (char *) attribs, attribs_size);
    }

    UnlockDisplay(dpy);
    SyncHandle();

    /* send the reply back to the client */
    reply.sequenceNumber = client->sequence;
    if (client->swapped) {
        __glXSwapGetDrawableAttributesReply(client, &reply, (int *) attribs);
    }
    else {
        WriteToClient(client, sz_xGLXGetDrawableAttributesReply, &reply);
        WriteToClient(client, attribs_size, attribs);
    }

    free(attribs);

    return Success;
}

int
__glXChangeDrawableAttributes(__GLXclientState * cl, GLbyte * pc)
{
    xGLXChangeDrawableAttributesReq *req =
        (xGLXChangeDrawableAttributesReq *) pc;
    xGLXChangeDrawableAttributesReq *be_req;
    ClientPtr client = cl->client;
    GLXDrawable drawId = req->drawable;
    GLXDrawable be_drawable = 0;
    DrawablePtr pDraw = NULL;
    Display *dpy;
    int screen, rc;
    DMXScreenInfo *dmxScreen;

    if (drawId != None) {
        rc = dixLookupDrawable(&pDraw, drawId, client, 0, DixSetAttrAccess);
        if (rc == Success && pDraw->type == DRAWABLE_WINDOW) {
            be_drawable = 0;
            screen = pDraw->pScreen->myNum;
        }
        else {
            /*
             ** Drawable is not a Window , GLXWindow or a GLXPixmap.
             */
            client->errorValue = drawId;
            return __glXBadDrawable;
        }

        if (!pDraw) {
            __GLXpixmap *pGlxPixmap;

            dixLookupResourceByType((void **) &pGlxPixmap,
                                    drawId, __glXPixmapRes,
                                    NullClient, DixUnknownAccess);
            if (pGlxPixmap) {
                pDraw = pGlxPixmap->pDraw;
                screen = pGlxPixmap->pScreen->myNum;
                be_drawable = pGlxPixmap->be_xids[screen];
            }
        }

        if (!pDraw) {
            __glXWindow *pGlxWindow;

            dixLookupResourceByType((void **) &pGlxWindow,
                                    drawId, __glXWindowRes,
                                    NullClient, DixUnknownAccess);
            if (pGlxWindow) {
                pDraw = pGlxWindow->pDraw;
                screen = pGlxWindow->pScreen->myNum;
                be_drawable = 0;
            }
        }

        if (!pDraw) {
            __glXPbuffer *pGlxPbuffer;

            dixLookupResourceByType((void **) &pGlxPbuffer,
                                    drawId, __glXPbufferRes,
                                    NullClient, DixUnknownAccess);
            if (pGlxPbuffer) {
                pDraw = (DrawablePtr) pGlxPbuffer;
                screen = pGlxPbuffer->pScreen->myNum;
                be_drawable = pGlxPbuffer->be_xids[screen];
            }
        }
    }

    if (!pDraw) {
        /*
         ** Drawable is not a Window , GLXWindow or a GLXPixmap.
         */
        client->errorValue = drawId;
        return __glXBadDrawable;
    }

    /* if the drawable is a window or GLXWindow -
     * we need to find the base id on the back-end server
     */
    if (!be_drawable) {
        WindowPtr pWin = (WindowPtr) pDraw;

#ifdef PANORAMIX
        if (!noPanoramiXExtension) {
            PanoramiXRes *pXinDraw;

            if (Success != dixLookupResourceByClass((void **) &pXinDraw,
                                                    pDraw->id, XRC_DRAWABLE,
                                                    client, DixReadAccess)) {
                client->errorValue = drawId;
                return __glXBadDrawable;
            }

            dixLookupWindow(&pWin, pXinDraw->info[screen].id, client,
                            DixReadAccess);
        }
#endif

        if (pWin) {
            be_drawable = (unsigned int) (DMX_GET_WINDOW_PRIV(pWin))->window;
            if (!be_drawable) {
                /* it might be that the window did not created yet on the */
                /* back-end server (lazy window creation option), force   */
                /* creation of the window */
                dmxCreateAndRealizeWindow(pWin, TRUE);
                be_drawable =
                    (unsigned int) (DMX_GET_WINDOW_PRIV(pWin))->window;
            }
        }
        else {
            client->errorValue = drawId;
            return __glXBadDrawable;
        }
    }

    /* send the request to the back-end server */
    dpy = GetBackEndDisplay(cl, screen);
    dmxScreen = &dmxScreens[screen];

    /* make sure drawable exists on back-end */
    dmxSync(dmxScreen, 1);

    LockDisplay(dpy);
    GetReqExtra(GLXChangeDrawableAttributes,
                2 * req->numAttribs * __GLX_SIZE_CARD32, be_req);
    be_req->reqType = dmxScreen->glxMajorOpcode;
    be_req->glxCode = X_GLXChangeDrawableAttributes;
    be_req->drawable = be_drawable;
    be_req->numAttribs = req->numAttribs;
    be_req->length = req->length;

    UnlockDisplay(dpy);
    SyncHandle();

    return Success;
}

int
__glXSendLargeCommand(__GLXclientState * cl, GLXContextTag contextTag)
{
    ClientPtr client = cl->client;
    xGLXRenderLargeReq *req;
    GLint maxSize, amount;
    GLint totalRequests, requestNumber;
    GLint dataLen;
    GLbyte *data;
    __GLXcontext *glxc;
    int s;
    int from_screen, to_screen;

    maxSize = cl->largeCmdMaxReqDataSize - (GLint) sizeof(xGLXRenderLargeReq);
    dataLen = cl->largeCmdBytesTotal;
    totalRequests = (dataLen / maxSize);
    if (dataLen % maxSize)
        totalRequests++;

    glxc = __glXLookupContextByTag(cl, contextTag);
    if (!glxc) {
        client->errorValue = contextTag;
        return __glXBadContext;
    }
    from_screen = to_screen = glxc->pScreen->myNum;

#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        from_screen = 0;
        to_screen = screenInfo.numScreens - 1;
    }
#endif

    /*
     ** Send enough requests until the whole array is sent.
     */
    requestNumber = 1;
    data = cl->largeCmdBuf;
    while (dataLen > 0) {
        amount = dataLen;
        if (amount > maxSize) {
            amount = maxSize;
        }

        for (s = from_screen; s <= to_screen; s++) {

            Display *dpy = GetBackEndDisplay(cl, s);
            DMXScreenInfo *dmxScreen = &dmxScreens[s];

            LockDisplay(dpy);
            GetReq(GLXRenderLarge, req);
            req->reqType = dmxScreen->glxMajorOpcode;
            req->glxCode = X_GLXRenderLarge;
            req->contextTag = GetCurrentBackEndTag(cl, contextTag, s);
            req->length += (amount + 3) >> 2;
            req->requestNumber = requestNumber++;
            req->requestTotal = totalRequests;
            req->dataBytes = amount;
            Data(dpy, ((const char *) data), amount);
            dataLen -= amount;
            data = ((GLbyte *) data) + amount;
            UnlockDisplay(dpy);
            SyncHandle();
        }
    }

    return Success;
}
