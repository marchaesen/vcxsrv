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
#include "glxutil.h"
#include <GL/glxtokens.h>
#include <g_disptab.h>
#include <pixmapstr.h>
#include <windowstr.h>
#include "unpack.h"
#include "glxcmds.h"
#include "glxext.h"
#include "glxvendor.h"

int __glXSwapGetFBConfigsSGIX(__GLXclientState * cl, GLbyte * pc);

/************************************************************************/

/*
** Byteswapping versions of GLX commands.  In most cases they just swap
** the incoming arguments and then call the unswapped routine.  For commands
** that have replies, a separate swapping routine for the reply is provided;
** it is called at the end of the unswapped routine.
*/

int
__glXSwapCreateContext(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreateContextReq *req = (xGLXCreateContextReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->context);
    __GLX_SWAP_INT(&req->visual);
    __GLX_SWAP_INT(&req->screen);
    __GLX_SWAP_INT(&req->shareList);

    return __glXCreateContext(cl, pc);
}

int
__glXSwapCreateNewContext(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreateNewContextReq *req = (xGLXCreateNewContextReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->context);
    __GLX_SWAP_INT(&req->fbconfig);
    __GLX_SWAP_INT(&req->screen);
    __GLX_SWAP_INT(&req->shareList);

    return __glXCreateNewContext(cl, pc);
}

int
__glXSwapCreateContextWithConfigSGIX(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreateContextWithConfigSGIXReq *req =
        (xGLXCreateContextWithConfigSGIXReq *) pc;
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->context);
    __GLX_SWAP_INT(&req->fbconfig);
    __GLX_SWAP_INT(&req->screen);
    __GLX_SWAP_INT(&req->shareList);

    return __glXCreateContextWithConfigSGIX(cl, pc);
}

int
__glXSwapQueryMaxSwapBarriersSGIX(__GLXclientState * cl, GLbyte * pc)
{
    xGLXQueryMaxSwapBarriersSGIXReq *req =
        (xGLXQueryMaxSwapBarriersSGIXReq *) pc;
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->screen);

    return __glXQueryMaxSwapBarriersSGIX(cl, pc);
}

int
__glXSwapBindSwapBarrierSGIX(__GLXclientState * cl, GLbyte * pc)
{
    xGLXBindSwapBarrierSGIXReq *req = (xGLXBindSwapBarrierSGIXReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->drawable);
    __GLX_SWAP_INT(&req->barrier);

    return __glXBindSwapBarrierSGIX(cl, pc);
}

int
__glXSwapJoinSwapGroupSGIX(__GLXclientState * cl, GLbyte * pc)
{
    xGLXJoinSwapGroupSGIXReq *req = (xGLXJoinSwapGroupSGIXReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->drawable);
    __GLX_SWAP_INT(&req->member);

    return __glXJoinSwapGroupSGIX(cl, pc);
}

int
__glXSwapDestroyContext(__GLXclientState * cl, GLbyte * pc)
{
    xGLXDestroyContextReq *req = (xGLXDestroyContextReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->context);

    return __glXDestroyContext(cl, pc);
}

int
__glXSwapMakeCurrent(__GLXclientState * cl, GLbyte * pc)
{
    xGLXMakeCurrentReq *req = (xGLXMakeCurrentReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->drawable);
    __GLX_SWAP_INT(&req->context);
    __GLX_SWAP_INT(&req->oldContextTag);

    return __glXMakeCurrent(cl, pc);
}

int
__glXSwapMakeContextCurrent(__GLXclientState * cl, GLbyte * pc)
{
    xGLXMakeContextCurrentReq *req = (xGLXMakeContextCurrentReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->drawable);
    __GLX_SWAP_INT(&req->readdrawable);
    __GLX_SWAP_INT(&req->context);
    __GLX_SWAP_INT(&req->oldContextTag);

    return __glXMakeContextCurrent(cl, pc);
}

int
__glXSwapMakeCurrentReadSGI(__GLXclientState * cl, GLbyte * pc)
{
    xGLXMakeCurrentReadSGIReq *req = (xGLXMakeCurrentReadSGIReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->drawable);
    __GLX_SWAP_INT(&req->readable);
    __GLX_SWAP_INT(&req->context);
    __GLX_SWAP_INT(&req->oldContextTag);

    return __glXMakeCurrentReadSGI(cl, pc);
}

int
__glXSwapIsDirect(__GLXclientState * cl, GLbyte * pc)
{
    xGLXIsDirectReq *req = (xGLXIsDirectReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->context);

    return __glXIsDirect(cl, pc);
}

int
__glXSwapQueryVersion(__GLXclientState * cl, GLbyte * pc)
{
    xGLXQueryVersionReq *req = (xGLXQueryVersionReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->majorVersion);
    __GLX_SWAP_INT(&req->minorVersion);

    return __glXQueryVersion(cl, pc);
}

int
__glXSwapWaitGL(__GLXclientState * cl, GLbyte * pc)
{
    xGLXWaitGLReq *req = (xGLXWaitGLReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->contextTag);

    return __glXWaitGL(cl, pc);
}

int
__glXSwapWaitX(__GLXclientState * cl, GLbyte * pc)
{
    xGLXWaitXReq *req = (xGLXWaitXReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->contextTag);

    return __glXWaitX(cl, pc);
}

int
__glXSwapCopyContext(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCopyContextReq *req = (xGLXCopyContextReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->source);
    __GLX_SWAP_INT(&req->dest);
    __GLX_SWAP_INT(&req->mask);

    return __glXCopyContext(cl, pc);
}

int
__glXSwapGetVisualConfigs(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXGetVisualConfigsReq *req = (xGLXGetVisualConfigsReq *) pc;
    xGLXGetVisualConfigsReply reply;
    __GLXscreenInfo *pGlxScreen;
    __GLXvisualConfig *pGlxVisual;
    CARD32 buf[__GLX_TOTAL_CONFIG];
    unsigned int screen;
    int i, p;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(&req->screen);
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
        .length = (pGlxScreen->numGLXVisuals * __GLX_SIZE_CARD32 *
                   __GLX_TOTAL_CONFIG) >> 2,
        .numVisuals = pGlxScreen->numGLXVisuals,
        .numProps = __GLX_TOTAL_CONFIG
    };

    __GLX_SWAP_SHORT(&reply.sequenceNumber);
    __GLX_SWAP_INT(&reply.length);
    __GLX_SWAP_INT(&reply.numVisuals);
    __GLX_SWAP_INT(&reply.numProps);
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

        __GLX_SWAP_INT_ARRAY(buf, __GLX_TOTAL_CONFIG);
        WriteToClient(client, __GLX_SIZE_CARD32 * __GLX_TOTAL_CONFIG, buf);
    }
    return Success;
}

int
__glXSwapCreateGLXPixmap(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreateGLXPixmapReq *req = (xGLXCreateGLXPixmapReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->screen);
    __GLX_SWAP_INT(&req->visual);
    __GLX_SWAP_INT(&req->pixmap);
    __GLX_SWAP_INT(&req->glxpixmap);

    return __glXCreateGLXPixmap(cl, pc);
}

int
__glXSwapCreatePixmap(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreatePixmapReq *req = (xGLXCreatePixmapReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->screen);
    __GLX_SWAP_INT(&req->fbconfig);
    __GLX_SWAP_INT(&req->pixmap);
    __GLX_SWAP_INT(&req->glxpixmap);
    __GLX_SWAP_INT(&req->numAttribs);

    return __glXCreatePixmap(cl, pc);
}

int
__glXSwapDestroyGLXPixmap(__GLXclientState * cl, GLbyte * pc)
{
    xGLXDestroyGLXPixmapReq *req = (xGLXDestroyGLXPixmapReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->glxpixmap);

    return __glXDestroyGLXPixmap(cl, pc);
}

int
__glXSwapSwapBuffers(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSwapBuffersReq *req = (xGLXSwapBuffersReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->contextTag);
    __GLX_SWAP_INT(&req->drawable);

    return __glXSwapBuffers(cl, pc);
}

int
__glXSwapUseXFont(__GLXclientState * cl, GLbyte * pc)
{
    xGLXUseXFontReq *req = (xGLXUseXFontReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->contextTag);
    __GLX_SWAP_INT(&req->font);
    __GLX_SWAP_INT(&req->first);
    __GLX_SWAP_INT(&req->count);
    __GLX_SWAP_INT(&req->listBase);

    return __glXUseXFont(cl, pc);
}

int
__glXSwapQueryExtensionsString(__GLXclientState * cl, GLbyte * pc)
{
    xGLXQueryExtensionsStringReq *req = (xGLXQueryExtensionsStringReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->screen);

    return __glXQueryExtensionsString(cl, pc);
}

int
__glXSwapQueryServerString(__GLXclientState * cl, GLbyte * pc)
{
    xGLXQueryServerStringReq *req = (xGLXQueryServerStringReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->screen);
    __GLX_SWAP_INT(&req->name);

    return __glXQueryServerString(cl, pc);
}

int
__glXSwapClientInfo(__GLXclientState * cl, GLbyte * pc)
{
    xGLXClientInfoReq *req = (xGLXClientInfoReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->major);
    __GLX_SWAP_INT(&req->minor);
    __GLX_SWAP_INT(&req->numbytes);

    return __glXClientInfo(cl, pc);
}

int
__glXSwapQueryContextInfoEXT(__GLXclientState * cl, char *pc)
{
    xGLXQueryContextInfoEXTReq *req = (xGLXQueryContextInfoEXTReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->context);

    return __glXQueryContextInfoEXT(cl, (GLbyte *) pc);
}

/************************************************************************/

/*
** Swap replies.
*/

void
__glXSwapMakeCurrentReply(ClientPtr client, xGLXMakeCurrentReadSGIReply * reply)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_SWAP_SHORT(&reply->sequenceNumber);
    __GLX_SWAP_INT(&reply->length);
    __GLX_SWAP_INT(&reply->contextTag);
    __GLX_SWAP_INT(&reply->writeVid);
    __GLX_SWAP_INT(&reply->writeType);
    __GLX_SWAP_INT(&reply->readVid);
    __GLX_SWAP_INT(&reply->readType);
    WriteToClient(client, sz_xGLXMakeCurrentReadSGIReply, reply);
}

void
__glXSwapIsDirectReply(ClientPtr client, xGLXIsDirectReply * reply)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_SWAP_SHORT(&reply->sequenceNumber);
    __GLX_SWAP_INT(&reply->length);
    WriteToClient(client, sz_xGLXIsDirectReply, reply);
}

void
__glXSwapQueryVersionReply(ClientPtr client, xGLXQueryVersionReply * reply)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_SWAP_SHORT(&reply->sequenceNumber);
    __GLX_SWAP_INT(&reply->length);
    __GLX_SWAP_INT(&reply->majorVersion);
    __GLX_SWAP_INT(&reply->minorVersion);
    WriteToClient(client, sz_xGLXQueryVersionReply, reply);
}

void
glxSwapQueryExtensionsStringReply(ClientPtr client,
                                  xGLXQueryExtensionsStringReply * reply,
                                  char *buf)
{
    int length = reply->length;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;
    __GLX_SWAP_SHORT(&reply->sequenceNumber);
    __GLX_SWAP_INT(&reply->length);
    __GLX_SWAP_INT(&reply->n);
    WriteToClient(client, sz_xGLXQueryExtensionsStringReply, reply);
    __GLX_SWAP_INT_ARRAY((int *) buf, length);
    WriteToClient(client, length << 2, buf);
}

void
glxSwapQueryServerStringReply(ClientPtr client,
                              xGLXQueryServerStringReply * reply, char *buf)
{
    int length = reply->length;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_SWAP_SHORT(&reply->sequenceNumber);
    __GLX_SWAP_INT(&reply->length);
    __GLX_SWAP_INT(&reply->n);
    WriteToClient(client, sz_xGLXQueryServerStringReply, reply);
    /** no swap is needed for an array of chars **/
    /* __GLX_SWAP_INT_ARRAY((int *)buf, length); */
    WriteToClient(client, length << 2, buf);
}

void
__glXSwapQueryContextInfoEXTReply(ClientPtr client,
                                  xGLXQueryContextInfoEXTReply * reply,
                                  int *buf)
{
    int length = reply->length;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;
    __GLX_SWAP_SHORT(&reply->sequenceNumber);
    __GLX_SWAP_INT(&reply->length);
    __GLX_SWAP_INT(&reply->n);
    WriteToClient(client, sz_xGLXQueryContextInfoEXTReply, reply);
    __GLX_SWAP_INT_ARRAY((int *) buf, length);
    WriteToClient(client, length << 2, buf);
}

void
__glXSwapQueryContextReply(ClientPtr client,
                           xGLXQueryContextReply * reply, int *buf)
{
    int length = reply->length;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;
    __GLX_SWAP_SHORT(&reply->sequenceNumber);
    __GLX_SWAP_INT(&reply->length);
    __GLX_SWAP_INT(&reply->n);
    WriteToClient(client, sz_xGLXQueryContextReply, reply);
    __GLX_SWAP_INT_ARRAY((int *) buf, length);
    WriteToClient(client, length << 2, buf);
}

void
__glXSwapGetDrawableAttributesReply(ClientPtr client,
                                    xGLXGetDrawableAttributesReply * reply,
                                    int *buf)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;
    __GLX_SWAP_SHORT(&reply->sequenceNumber);
    __GLX_SWAP_INT(&reply->length);
    __GLX_SWAP_INT(&reply->numAttribs);
    __GLX_SWAP_INT_ARRAY(buf, reply->length);
    WriteToClient(client, sz_xGLXGetDrawableAttributesReply, reply);
    WriteToClient(client, reply->length << 2, buf);
}

void
__glXSwapQueryMaxSwapBarriersSGIXReply(ClientPtr client,
                                       xGLXQueryMaxSwapBarriersSGIXReply *
                                       reply)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_SWAP_SHORT(&reply->sequenceNumber);
    __GLX_SWAP_INT(&reply->length);
    __GLX_SWAP_INT(&reply->max);
    WriteToClient(client, sz_xGLXQueryMaxSwapBarriersSGIXReply, reply);
}

/************************************************************************/

/*
** Render and Renderlarge are not in the GLX API.  They are used by the GLX
** client library to send batches of GL rendering commands.
*/

int
__glXSwapRender(__GLXclientState * cl, GLbyte * pc)
{
    xGLXRenderReq *req;
    int left;
    __GLXrenderHeader *hdr;
    ClientPtr client = cl->client;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    /*
     ** NOTE: much of this code also appears in the nonswapping version of this
     ** routine, __glXRender().  Any changes made here should also be
     ** duplicated there.
     */

    req = (xGLXRenderReq *) pc;
    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->contextTag);

    pc += sz_xGLXRenderReq;
    left = (req->length << 2) - sz_xGLXRenderReq;
    while (left > 0) {
        void (*proc) (GLbyte *);
        CARD16 opcode;

        /*
         ** Verify that the header length and the overall length agree.
         ** Also, each command must be word aligned.
         */
        hdr = (__GLXrenderHeader *) pc;
        __GLX_SWAP_SHORT(&hdr->length);
        __GLX_SWAP_SHORT(&hdr->opcode);

        /*
         * call the command procedure to swap any arguments
         */
        opcode = hdr->opcode;
        if ((opcode >= __GLX_MIN_RENDER_OPCODE) &&
            (opcode <= __GLX_MAX_RENDER_OPCODE)) {
            proc = __glXSwapRenderTable[opcode];
#if __GLX_MAX_RENDER_OPCODE_EXT > __GLX_MIN_RENDER_OPCODE_EXT
        }
        else if ((opcode >= __GLX_MIN_RENDER_OPCODE_EXT) &&
                 (opcode <= __GLX_MAX_RENDER_OPCODE_EXT)) {
            int index = opcode - __GLX_MIN_RENDER_OPCODE_EXT;
            __GLXRenderSwapInfo *info = &__glXSwapRenderTable_EXT[index];

            if (info->swapfunc) {
                proc = info->swapfunc;
            }
            else {
                proc = NULL;
                if (info->elem_size == 4 && info->nelems > 0) {
                    __GLX_SWAP_INT_ARRAY((int *) (pc + __GLX_RENDER_HDR_SIZE),
                                         info->nelems);
                }
                else if (info->elem_size == 2 && info->nelems > 0) {
                    __GLX_SWAP_SHORT_ARRAY((short *) (pc +
                                                      __GLX_RENDER_HDR_SIZE),
                                           info->nelems);
                }
                else if (info->elem_size == 8 && info->nelems > 0) {
                    __GLX_SWAP_DOUBLE_ARRAY((double *) (pc +
                                                        __GLX_RENDER_HDR_SIZE),
                                            info->nelems);
                }
            }
#endif                          /* __GLX_MAX_RENDER_OPCODE_EXT > __GLX_MIN_RENDER_OPCODE_EXT */
        }
        else {
            client->errorValue = 0;
            return __glXBadRenderRequest;
        }

        if (proc != NULL)
            (*proc) (pc + __GLX_RENDER_HDR_SIZE);

        /*
         * proceed to the next command
         */
        pc += hdr->length;
        left -= hdr->length;
    }

    return __glXRender(cl, (GLbyte *) req);
}

/*
** Execute a large rendering request (one that spans multiple X requests).
*/
int
__glXSwapRenderLarge(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXRenderLargeReq *req;
    __GLXrenderLargeHeader *hdr;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    req = (xGLXRenderLargeReq *) pc;
    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->contextTag);
    __GLX_SWAP_INT(&req->dataBytes);
    __GLX_SWAP_SHORT(&req->requestNumber);
    __GLX_SWAP_SHORT(&req->requestTotal);

    pc += sz_xGLXRenderLargeReq;

    if (req->requestNumber == 1) {
        void (*proc) (GLbyte *) = NULL;
        __GLXRenderSwapInfo *info = NULL;
        CARD16 opcode;

        hdr = (__GLXrenderLargeHeader *) pc;
        __GLX_SWAP_INT(&hdr->length);
        __GLX_SWAP_INT(&hdr->opcode);

        /*
         * call the command procedure to swap any arguments
         * Note that we are assuming that all arguments that needs to be
         * swaped are on the first req only !
         */
        opcode = hdr->opcode;
        if ((opcode >= __GLX_MIN_RENDER_OPCODE) &&
            (opcode <= __GLX_MAX_RENDER_OPCODE)) {
            proc = __glXSwapRenderTable[opcode];
#if __GLX_MAX_RENDER_OPCODE_EXT > __GLX_MIN_RENDER_OPCODE_EXT
        }
        else if ((opcode >= __GLX_MIN_RENDER_OPCODE_EXT) &&
                 (opcode <= __GLX_MAX_RENDER_OPCODE_EXT)) {
            int index = opcode - __GLX_MIN_RENDER_OPCODE_EXT;

            info = &__glXSwapRenderTable_EXT[index];
            if (info->swapfunc) {
                proc = info->swapfunc;
            }
#endif                          /* __GLX_MAX_RENDER_OPCODE_EXT > __GLX_MIN_RENDER_OPCODE_EXT */
        }
        else {
            client->errorValue = 0;
            cl->largeCmdRequestsTotal = 0;
            return __glXBadLargeRequest;
        }

        /*
         ** Make enough space in the buffer, then copy the entire request.
         */
        if (cl->largeCmdBufSize < hdr->length) {
            if (!cl->largeCmdBuf) {
                cl->largeCmdBuf = (GLbyte *) malloc(hdr->length);
            }
            else {
                cl->largeCmdBuf =
                    (GLbyte *) realloc(cl->largeCmdBuf, hdr->length);
            }
            if (!cl->largeCmdBuf) {
                cl->largeCmdRequestsTotal = 0;
                return BadAlloc;
            }
            cl->largeCmdBufSize = hdr->length;
        }
        memcpy(cl->largeCmdBuf, pc, req->dataBytes);

        cl->largeCmdBytesSoFar = req->dataBytes;
        cl->largeCmdBytesTotal = hdr->length;
        cl->largeCmdRequestsSoFar = 1;
        cl->largeCmdRequestsTotal = req->requestTotal;
        cl->largeCmdRequestsSwapProc = proc;
        cl->largeCmdMaxReqDataSize = req->dataBytes;
        cl->largeCmdRequestsSwap_info = info;

        return Success;

    }
    else if (req->requestNumber < cl->largeCmdRequestsTotal) {
        /*
         * This is not the first nor last request - just copy the data
         */
        if (cl->largeCmdBytesSoFar + req->dataBytes > cl->largeCmdBytesTotal) {
            cl->largeCmdRequestsTotal = 0;
            return __glXBadLargeRequest;
        }

        memcpy(cl->largeCmdBuf + cl->largeCmdBytesSoFar, pc, req->dataBytes);

        cl->largeCmdBytesSoFar += req->dataBytes;

        if (req->dataBytes > cl->largeCmdMaxReqDataSize)
            cl->largeCmdMaxReqDataSize = req->dataBytes;

        return Success;
    }
    else if (req->requestNumber == cl->largeCmdRequestsTotal) {
        /*
         * this is the last request
         * copy the remainder bytes, call the procedure to swap any
         * needed data, and then call to transfer the command to all
         * back-end servers
         */
        if (cl->largeCmdBytesSoFar + req->dataBytes > cl->largeCmdBytesTotal) {
            cl->largeCmdRequestsTotal = 0;
            return __glXBadLargeRequest;
        }

        memcpy(cl->largeCmdBuf + cl->largeCmdBytesSoFar, pc, req->dataBytes);

        cl->largeCmdBytesSoFar += req->dataBytes;

        if (req->dataBytes > cl->largeCmdMaxReqDataSize)
            cl->largeCmdMaxReqDataSize = req->dataBytes;

        if (cl->largeCmdRequestsSwapProc != NULL) {
            (*cl->largeCmdRequestsSwapProc) (cl->largeCmdBuf +
                                             __GLX_RENDER_LARGE_HDR_SIZE);
        }
        else if (cl->largeCmdRequestsSwap_info &&
                 cl->largeCmdRequestsSwap_info->nelems > 0) {
            if (cl->largeCmdRequestsSwap_info->elem_size == 4) {
                __GLX_SWAP_INT_ARRAY((int *) (pc + __GLX_RENDER_LARGE_HDR_SIZE),
                                     cl->largeCmdRequestsSwap_info->nelems);
            }
            else if (cl->largeCmdRequestsSwap_info->elem_size == 2) {
                __GLX_SWAP_SHORT_ARRAY((short *) (pc +
                                                  __GLX_RENDER_LARGE_HDR_SIZE),
                                       cl->largeCmdRequestsSwap_info->nelems);
            }
            else if (cl->largeCmdRequestsSwap_info->elem_size == 8) {
                __GLX_SWAP_DOUBLE_ARRAY((double *) (pc +
                                                    __GLX_RENDER_LARGE_HDR_SIZE),
                                        cl->largeCmdRequestsSwap_info->nelems);
            }
        }

        cl->largeCmdRequestsTotal = 0;
        return (__glXSendLargeCommand(cl, req->contextTag));

    }
    else {
        cl->largeCmdRequestsTotal = 0;
        return __glXBadLargeRequest;
    }

}

/************************************************************************/

/*
** No support is provided for the vendor-private requests other than
** allocating these entry points in the dispatch table.
*/

int
__glXSwapVendorPrivate(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *req;
    CARD32 vendorCode;

    __GLX_DECLARE_SWAP_VARIABLES;

    req = (xGLXVendorPrivateReq *) pc;
    vendorCode = req->vendorCode;
    __GLX_SWAP_INT(&vendorCode);

    switch (vendorCode) {

    case X_GLvop_DeleteTexturesEXT:
        return __glXVForwardSingleReqSwap(cl, pc);
        break;

    case X_GLXvop_SwapIntervalSGI:
        if (glxIsExtensionSupported("SGI_swap_control")) {
            return __glXVForwardSingleReqSwap(cl, pc);
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

    case X_GLXvop_JoinSwapGroupSGIX:
        return __glXSwapJoinSwapGroupSGIX(cl, pc);
        break;

    case X_GLXvop_BindSwapBarrierSGIX:
        return __glXSwapBindSwapBarrierSGIX(cl, pc);
        break;

    case X_GLXvop_CreateContextWithConfigSGIX:
        return __glXSwapCreateContextWithConfigSGIX(cl, pc);
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
__glXSwapVendorPrivateWithReply(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateWithReplyReq *req;
    CARD32 vendorCode;

    __GLX_DECLARE_SWAP_VARIABLES;

    req = (xGLXVendorPrivateWithReplyReq *) pc;
    vendorCode = req->vendorCode;
    __GLX_SWAP_INT(&vendorCode);

    switch (vendorCode) {

    case X_GLvop_GetConvolutionFilterEXT:
    case X_GLvop_GetSeparableFilterEXT:
    case X_GLvop_GetHistogramEXT:
    case X_GLvop_GetMinmaxEXT:
        return (__glXNoSuchSingleOpcode(cl, pc));
        break;

    case X_GLvop_GetConvolutionParameterfvEXT:
    case X_GLvop_GetConvolutionParameterivEXT:
    case X_GLvop_GetHistogramParameterivEXT:
    case X_GLvop_GetMinmaxParameterfvEXT:
    case X_GLvop_GetMinmaxParameterivEXT:
    case X_GLvop_GenTexturesEXT:
        return (__glXVForwardAllWithReplySwapiv(cl, pc));
        break;

    case X_GLvop_AreTexturesResidentEXT:
    case X_GLvop_IsTextureEXT:
        return (__glXVForwardPipe0WithReplySwap(cl, pc));
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
        return (__glXSwapQueryMaxSwapBarriersSGIX(cl, pc));
        break;

    case X_GLXvop_GetFBConfigsSGIX:
        return (__glXSwapGetFBConfigsSGIX(cl, pc));
        break;

    case X_GLXvop_MakeCurrentReadSGI:
        return (__glXSwapMakeCurrentReadSGI(cl, pc));
        break;

    case X_GLXvop_QueryContextInfoEXT:
        return (__glXSwapQueryContextInfoEXT(cl, (char *) pc));
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
__glXSwapGetFBConfigs(__GLXclientState * cl, GLbyte * pc)
{
    xGLXGetFBConfigsReq *req = (xGLXGetFBConfigsReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->screen);

    return __glXGetFBConfigs(cl, pc);
}

int
__glXSwapGetFBConfigsSGIX(__GLXclientState * cl, GLbyte * pc)
{
    xGLXGetFBConfigsSGIXReq *req = (xGLXGetFBConfigsSGIXReq *) pc;
    xGLXGetFBConfigsReq new_req;

    new_req.reqType = req->reqType;
    new_req.glxCode = req->glxCode;
    new_req.length = req->length;
    new_req.screen = req->screen;

    return (__glXSwapGetFBConfigs(cl, (GLbyte *) &new_req));
}

int
__glXSwapCreateWindow(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreateWindowReq *req = (xGLXCreateWindowReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->screen);
    __GLX_SWAP_INT(&req->fbconfig);
    __GLX_SWAP_INT(&req->window);
    __GLX_SWAP_INT(&req->glxwindow);
    __GLX_SWAP_INT(&req->numAttribs);

    return (__glXCreateWindow(cl, (GLbyte *) pc));
}

int
__glXSwapDestroyWindow(__GLXclientState * cl, GLbyte * pc)
{
    xGLXDestroyWindowReq *req = (xGLXDestroyWindowReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->glxwindow);

    return (__glXDestroyWindow(cl, (GLbyte *) pc));
}

int
__glXSwapQueryContext(__GLXclientState * cl, GLbyte * pc)
{
    xGLXQueryContextReq *req = (xGLXQueryContextReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->context);

    return (__glXQueryContext(cl, (GLbyte *) pc));

}

int
__glXSwapCreatePbuffer(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreatePbufferReq *req = (xGLXCreatePbufferReq *) pc;
    int nattr = req->numAttribs;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->screen);
    __GLX_SWAP_INT(&req->fbconfig);
    __GLX_SWAP_INT(&req->pbuffer);
    __GLX_SWAP_INT(&req->numAttribs);
    __GLX_SWAP_INT_ARRAY((int *) (req + 1), nattr * 2);

    return (__glXCreatePbuffer(cl, pc));
}

int
__glXSwapDestroyPbuffer(__GLXclientState * cl, GLbyte * pc)
{
    xGLXDestroyPbufferReq *req = (xGLXDestroyPbufferReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->pbuffer);

    return (__glXDestroyPbuffer(cl, (GLbyte *) pc));
}

int
__glXSwapGetDrawableAttributes(__GLXclientState * cl, GLbyte * pc)
{
    xGLXGetDrawableAttributesReq *req = (xGLXGetDrawableAttributesReq *) pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->drawable);

    return (__glXGetDrawableAttributes(cl, pc));
}

int
__glXSwapChangeDrawableAttributes(__GLXclientState * cl, GLbyte * pc)
{
    xGLXChangeDrawableAttributesReq *req =
        (xGLXChangeDrawableAttributesReq *) pc;
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->drawable);
    __GLX_SWAP_INT(&req->numAttribs);
    __GLX_SWAP_INT_ARRAY((int *) (req + 1), req->numAttribs * 2);

    return (__glXChangeDrawableAttributes(cl, pc));
}
