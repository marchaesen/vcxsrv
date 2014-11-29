/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 *
 * Copyright © 2007 OpenedHand Ltd
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of OpenedHand Ltd not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. OpenedHand Ltd makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * OpenedHand Ltd DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OpenedHand Ltd BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *    Dodji Seketeli <dodji@openedhand.com>
 */
#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <xcb/glx.h>
#include "extnsionst.h"
#include "ephyrglxext.h"
#include "ephyrhostglx.h"
#define _HAVE_XALLOC_DECLS
#include "ephyrlog.h"
#include <GL/glxproto.h>
#include "glx/glxserver.h"
#include "glx/indirect_table.h"
#include "glx/indirect_util.h"
#include "glx/unpack.h"
#include "hostx.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

int ephyrGLXQueryVersion(__GLXclientState * cl, GLbyte * pc);
int ephyrGLXQueryVersionSwap(__GLXclientState * cl, GLbyte * pc);
int ephyrGLXGetVisualConfigs(__GLXclientState * cl, GLbyte * pc);
int ephyrGLXGetVisualConfigsSwap(__GLXclientState * cl, GLbyte * pc);
int ephyrGLXClientInfo(__GLXclientState * cl, GLbyte * pc);
int ephyrGLXClientInfoSwap(__GLXclientState * cl, GLbyte * pc);
int ephyrGLXQueryServerString(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXQueryServerStringSwap(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXGetFBConfigsSGIX(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXGetFBConfigsSGIXSwap(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXCreateContext(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXCreateContextSwap(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXCreateNewContext(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXCreateNewContextSwap(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXDestroyContext(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXDestroyContextSwap(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXMakeCurrent(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXMakeCurrentSwap(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXMakeCurrentReadSGI(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXMakeCurrentReadSGISwap(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXMakeContextCurrent(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXMakeContextCurrentSwap(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXGetString(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXGetStringSwap(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXGetIntegerv(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXGetIntegervSwap(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXIsDirect(__GLXclientState * a_cl, GLbyte * a_pc);
int ephyrGLXIsDirectSwap(__GLXclientState * a_cl, GLbyte * a_pc);

Bool
ephyrHijackGLXExtension(void)
{
    const void *(*dispatch_functions)[2];

    if (!host_has_extension(&xcb_glx_id)) {
        EPHYR_LOG("host X does not have GLX\n");
        return FALSE;
    }
    EPHYR_LOG("host X does have GLX\n");

    if (!Single_dispatch_info.dispatch_functions) {
        EPHYR_LOG_ERROR("could not get dispatch functions table\n");
        return FALSE;
    }
    /*
     * hijack some single entry point dispatch functions
     */
    dispatch_functions = Single_dispatch_info.dispatch_functions;
    EPHYR_RETURN_VAL_IF_FAIL(dispatch_functions, FALSE);

    dispatch_functions[X_GLXQueryVersion][0] = ephyrGLXQueryVersion;
    dispatch_functions[X_GLXQueryVersion][1] = ephyrGLXQueryVersionSwap;

    dispatch_functions[X_GLXGetVisualConfigs][0] = ephyrGLXGetVisualConfigs;
    dispatch_functions[X_GLXGetVisualConfigs][1] = ephyrGLXGetVisualConfigsSwap;
    dispatch_functions[X_GLXClientInfo][0] = ephyrGLXClientInfo;
    dispatch_functions[X_GLXClientInfo][1] = ephyrGLXClientInfoSwap;

    dispatch_functions[X_GLXQueryServerString][0] = ephyrGLXQueryServerString;
    dispatch_functions[X_GLXQueryServerString][1] =
        ephyrGLXQueryServerStringSwap;

    dispatch_functions[X_GLXCreateContext][0] = ephyrGLXCreateContext;
    dispatch_functions[X_GLXCreateContext][1] = ephyrGLXCreateContextSwap;

    dispatch_functions[X_GLXCreateNewContext][0] = ephyrGLXCreateNewContext;
    dispatch_functions[X_GLXCreateNewContext][1] = ephyrGLXCreateNewContextSwap;

    dispatch_functions[X_GLXDestroyContext][0] = ephyrGLXDestroyContext;
    dispatch_functions[X_GLXDestroyContext][1] = ephyrGLXDestroyContextSwap;

    dispatch_functions[X_GLXMakeCurrent][0] = ephyrGLXMakeCurrent;
    dispatch_functions[X_GLXMakeCurrent][1] = ephyrGLXMakeCurrentSwap;

    dispatch_functions[X_GLXIsDirect][0] = ephyrGLXIsDirect;
    dispatch_functions[X_GLXIsDirect][1] = ephyrGLXIsDirectSwap;

    dispatch_functions[73][0] = ephyrGLXGetString;
    dispatch_functions[73][1] = ephyrGLXGetStringSwap;

    dispatch_functions[61][0] = ephyrGLXGetIntegerv;
    dispatch_functions[61][1] = ephyrGLXGetIntegervSwap;

    dispatch_functions[X_GLXMakeContextCurrent][0] =
        ephyrGLXMakeContextCurrent;
    dispatch_functions[X_GLXMakeContextCurrent][1] =
        ephyrGLXMakeContextCurrentSwap;

    /*
     * hijack some vendor priv entry point dispatch functions
     */
    dispatch_functions = VendorPriv_dispatch_info.dispatch_functions;
    dispatch_functions[92][0] = ephyrGLXGetFBConfigsSGIX;
    dispatch_functions[92][1] = ephyrGLXGetFBConfigsSGIXSwap;

    dispatch_functions[89][0] = ephyrGLXMakeCurrentReadSGI;
    dispatch_functions[89][1] = ephyrGLXMakeCurrentReadSGISwap;

    EPHYR_LOG("hijacked glx entry points to forward requests to host X\n");


    return TRUE;
}

/*********************
 * implementation of
 * hijacked GLX entry
 * points
 ********************/

int
ephyrGLXQueryVersion(__GLXclientState * a_cl, GLbyte * a_pc)
{
    ClientPtr client = a_cl->client;
    xGLXQueryVersionReq *req = (xGLXQueryVersionReq *) a_pc;
    xGLXQueryVersionReply reply;
    int major, minor;
    int res = BadImplementation;

    EPHYR_LOG("enter\n");

    major = req->majorVersion;
    minor = req->minorVersion;

    if (!ephyrHostGLXQueryVersion(&major, &minor)) {
        EPHYR_LOG_ERROR("ephyrHostGLXQueryVersion() failed\n");
        goto out;
    }
    EPHYR_LOG("major:%d, minor:%d\n", major, minor);
    reply = (xGLXQueryVersionReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .majorVersion = major,
        .minorVersion = minor
    };

    if (client->swapped) {
        __glXSwapQueryVersionReply(client, &reply);
    }
    else {
        WriteToClient(client, sz_xGLXQueryVersionReply, &reply);
    }

    res = Success;
 out:
    EPHYR_LOG("leave\n");
    return res;
}

int
ephyrGLXQueryVersionSwap(__GLXclientState * a_cl, GLbyte * a_pc)
{
    xGLXQueryVersionReq *req = (xGLXQueryVersionReq *) a_pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->majorVersion);
    __GLX_SWAP_INT(&req->minorVersion);
    return ephyrGLXQueryVersion(a_cl, a_pc);
}

static int
ephyrGLXGetVisualConfigsReal(__GLXclientState * a_cl,
                             GLbyte * a_pc, Bool a_do_swap)
{
    xGLXGetVisualConfigsReq *req = (xGLXGetVisualConfigsReq *) a_pc;
    ClientPtr client = a_cl->client;
    xGLXGetVisualConfigsReply reply;
    int32_t *props_buf = NULL, num_visuals = 0,
        num_props = 0, res = BadImplementation, i = 0,
        props_per_visual_size = 0, props_buf_size = 0;
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    EPHYR_LOG("enter\n");

    if (!ephyrHostGLXGetVisualConfigs(req->screen,
                                      &num_visuals,
                                      &num_props,
                                      &props_buf_size, &props_buf)) {
        EPHYR_LOG_ERROR("ephyrHostGLXGetVisualConfigs() failed\n");
        goto out;
    }
    EPHYR_LOG("num_visuals:%d, num_props:%d\n", num_visuals, num_props);

    reply = (xGLXGetVisualConfigsReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = (num_visuals * __GLX_SIZE_CARD32 * num_props) >> 2,
        .numVisuals = num_visuals,
        .numProps = num_props
    };

    if (a_do_swap) {
        __GLX_SWAP_SHORT(&reply.sequenceNumber);
        __GLX_SWAP_INT(&reply.length);
        __GLX_SWAP_INT(&reply.numVisuals);
        __GLX_SWAP_INT(&reply.numProps);
        __GLX_SWAP_INT_ARRAY(props_buf, num_props);
    }
    WriteToClient(client, sz_xGLXGetVisualConfigsReply, &reply);
    props_per_visual_size = props_buf_size / num_visuals;
    for (i = 0; i < num_visuals; i++) {
        WriteToClient(client,
                      props_per_visual_size,
                      (char *) props_buf + i * props_per_visual_size);
    }
    res = Success;

 out:
    EPHYR_LOG("leave\n");
    free(props_buf);
    props_buf = NULL;

    return res;
}

static int
ephyrGLXGetFBConfigsSGIXReal(__GLXclientState * a_cl,
                             GLbyte * a_pc, Bool a_do_swap)
{
    xGLXGetFBConfigsSGIXReq *req = (xGLXGetFBConfigsSGIXReq *) a_pc;
    ClientPtr client = a_cl->client;
    xGLXGetVisualConfigsReply reply;
    int32_t *props_buf = NULL, num_visuals = 0,
        num_props = 0, res = BadImplementation, i = 0,
        props_per_visual_size = 0, props_buf_size = 0;
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    EPHYR_LOG("enter\n");

    if (!ephyrHostGLXVendorPrivGetFBConfigsSGIX(req->screen,
                                                &num_visuals,
                                                &num_props,
                                                &props_buf_size, &props_buf)) {
        EPHYR_LOG_ERROR("ephyrHostGLXGetVisualConfigs() failed\n");
        goto out;
    }
    EPHYR_LOG("num_visuals:%d, num_props:%d\n", num_visuals, num_props);

    reply = (xGLXGetVisualConfigsReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = props_buf_size >> 2,
        .numVisuals = num_visuals,
        .numProps = num_props
    };

    if (a_do_swap) {
        __GLX_SWAP_SHORT(&reply.sequenceNumber);
        __GLX_SWAP_INT(&reply.length);
        __GLX_SWAP_INT(&reply.numVisuals);
        __GLX_SWAP_INT(&reply.numProps);
        __GLX_SWAP_INT_ARRAY(props_buf, num_props);
    }
    WriteToClient(client, sz_xGLXGetVisualConfigsReply, &reply);
    props_per_visual_size = props_buf_size / num_visuals;
    for (i = 0; i < num_visuals; i++) {
        WriteToClient(client,
                      props_per_visual_size,
                      &((char *) props_buf)[i * props_per_visual_size]);
    }
    res = Success;

 out:
    EPHYR_LOG("leave\n");
    free(props_buf);
    props_buf = NULL;

    return res;
}

int
ephyrGLXGetVisualConfigs(__GLXclientState * a_cl, GLbyte * a_pc)
{
    return ephyrGLXGetVisualConfigsReal(a_cl, a_pc, FALSE);
}

int
ephyrGLXGetVisualConfigsSwap(__GLXclientState * a_cl, GLbyte * a_pc)
{
    return ephyrGLXGetVisualConfigsReal(a_cl, a_pc, TRUE);
}

int
ephyrGLXClientInfo(__GLXclientState * a_cl, GLbyte * a_pc)
{
    int res = BadImplementation;
    xGLXClientInfoReq *req = (xGLXClientInfoReq *) a_pc;

    EPHYR_LOG("enter\n");
    if (!ephyrHostGLXSendClientInfo(req->major, req->minor, (char *) req + 1)) {
        EPHYR_LOG_ERROR("failed to send client info to host\n");
        goto out;
    }
    res = Success;

 out:
    EPHYR_LOG("leave\n");
    return res;
}

int
ephyrGLXClientInfoSwap(__GLXclientState * a_cl, GLbyte * a_pc)
{
    xGLXClientInfoReq *req = (xGLXClientInfoReq *) a_pc;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_SHORT(&req->length);
    __GLX_SWAP_INT(&req->major);
    __GLX_SWAP_INT(&req->minor);
    __GLX_SWAP_INT(&req->numbytes);

    return ephyrGLXClientInfo(a_cl, a_pc);
}

int
ephyrGLXQueryServerString(__GLXclientState * a_cl, GLbyte * a_pc)
{
    int res = BadImplementation;
    ClientPtr client = a_cl->client;
    xGLXQueryServerStringReq *req = (xGLXQueryServerStringReq *) a_pc;
    xGLXQueryServerStringReply reply;
    char *server_string = NULL;
    int length = 0;

    EPHYR_LOG("enter\n");
    if (!ephyrHostGLXQueryServerString(req->screen,
                                       req->name,
                                       &server_string)) {
        EPHYR_LOG_ERROR("failed to query string from host\n");
        goto out;
    }
    EPHYR_LOG("string: %s\n", server_string);
    length = strlen(server_string) + 1;
    reply = (xGLXQueryServerStringReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = __GLX_PAD(length) >> 2,
        .n = length
    };

    WriteToClient(client, sz_xGLXQueryServerStringReply, &reply);
    WriteToClient(client, (int) (reply.length << 2), server_string);

    res = Success;

 out:
    EPHYR_LOG("leave\n");
    free(server_string);
    server_string = NULL;

    return res;
}

int
ephyrGLXQueryServerStringSwap(__GLXclientState * a_cl, GLbyte * a_pc)
{
    EPHYR_LOG_ERROR("not yet implemented\n");
    return BadImplementation;
}

int
ephyrGLXGetFBConfigsSGIX(__GLXclientState * a_cl, GLbyte * a_pc)
{
    return ephyrGLXGetFBConfigsSGIXReal(a_cl, a_pc, FALSE);
}

int
ephyrGLXGetFBConfigsSGIXSwap(__GLXclientState * a_cl, GLbyte * a_pc)
{
    return ephyrGLXGetFBConfigsSGIXReal(a_cl, a_pc, TRUE);
}

static int
ephyrGLXCreateContextReal(xGLXCreateContextReq * a_req, Bool a_do_swap)
{
    int res = BadImplementation;
    EphyrHostWindowAttributes host_w_attrs;

    __GLX_DECLARE_SWAP_VARIABLES;

    EPHYR_RETURN_VAL_IF_FAIL(a_req, BadValue);
    EPHYR_LOG("enter\n");

    if (a_do_swap) {
        __GLX_SWAP_SHORT(&a_req->length);
        __GLX_SWAP_INT(&a_req->context);
        __GLX_SWAP_INT(&a_req->visual);
        __GLX_SWAP_INT(&a_req->screen);
        __GLX_SWAP_INT(&a_req->shareList);
    }

    EPHYR_LOG("context creation requested. localid:%d, "
              "screen:%d, visual:%d, direct:%d\n",
              (int) a_req->context, (int) a_req->screen,
              (int) a_req->visual, (int) a_req->isDirect);

    memset(&host_w_attrs, 0, sizeof(host_w_attrs));
    if (!hostx_get_window_attributes(hostx_get_window(a_req->screen),
                                     &host_w_attrs)) {
        EPHYR_LOG_ERROR("failed to get host window attrs\n");
        goto out;
    }

    EPHYR_LOG("host window visual id: %d\n", host_w_attrs.visualid);

    if (!ephyrHostGLXCreateContext(a_req->screen,
                                   host_w_attrs.visualid,
                                   a_req->context,
                                   a_req->shareList, 0,
                                   a_req->isDirect, X_GLXCreateContext)) {
        EPHYR_LOG_ERROR("ephyrHostGLXCreateContext() failed\n");
        goto out;
    }
    res = Success;
 out:
    EPHYR_LOG("leave\n");
    return res;
}

static int
ephyrGLXCreateNewContextReal(xGLXCreateNewContextReq * a_req, Bool a_do_swap)
{
    int res = BadImplementation;

    __GLX_DECLARE_SWAP_VARIABLES;

    EPHYR_RETURN_VAL_IF_FAIL(a_req, BadValue);
    EPHYR_LOG("enter\n");

    if (a_do_swap) {
        __GLX_SWAP_SHORT(&a_req->length);
        __GLX_SWAP_INT(&a_req->context);
        __GLX_SWAP_INT(&a_req->fbconfig);
        __GLX_SWAP_INT(&a_req->screen);
        __GLX_SWAP_INT(&a_req->renderType);
        __GLX_SWAP_INT(&a_req->shareList);
    }

    EPHYR_LOG("context creation requested. localid:%d, "
              "screen:%d, fbconfig:%d, renderType:%d, direct:%d\n",
              (int) a_req->context, (int) a_req->screen,
              (int) a_req->fbconfig, (int) a_req->renderType,
              (int) a_req->isDirect);

    if (!ephyrHostGLXCreateContext(a_req->screen,
                                   a_req->fbconfig,
                                   a_req->context,
                                   a_req->shareList, a_req->renderType,
                                   a_req->isDirect, X_GLXCreateNewContext)) {
        EPHYR_LOG_ERROR("ephyrHostGLXCreateNewContext() failed\n");
        goto out;
    }
    res = Success;
 out:
    EPHYR_LOG("leave\n");
    return res;
}

int
ephyrGLXCreateContext(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreateContextReq *req = (xGLXCreateContextReq *) pc;

    return ephyrGLXCreateContextReal(req, FALSE);
}

int
ephyrGLXCreateContextSwap(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreateContextReq *req = (xGLXCreateContextReq *) pc;

    return ephyrGLXCreateContextReal(req, TRUE);
}

int
ephyrGLXCreateNewContext(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreateNewContextReq *req = (xGLXCreateNewContextReq *) pc;

    return ephyrGLXCreateNewContextReal(req, FALSE);
}

int
ephyrGLXCreateNewContextSwap(__GLXclientState * cl, GLbyte * pc)
{
    xGLXCreateNewContextReq *req = (xGLXCreateNewContextReq *) pc;

    return ephyrGLXCreateNewContextReal(req, TRUE);
}

static int
ephyrGLXDestroyContextReal(__GLXclientState * a_cl,
                           GLbyte * a_pc, Bool a_do_swap)
{
    int res = BadImplementation;
    ClientPtr client = a_cl->client;
    xGLXDestroyContextReq *req = (xGLXDestroyContextReq *) a_pc;

    EPHYR_LOG("enter. id:%d\n", (int) req->context);
    if (!ephyrHostDestroyContext(req->context)) {
        EPHYR_LOG_ERROR("ephyrHostDestroyContext() failed\n");
        client->errorValue = req->context;
        goto out;
    }
    res = Success;

 out:
    EPHYR_LOG("leave\n");
    return res;
}

int
ephyrGLXDestroyContext(__GLXclientState * a_cl, GLbyte * a_pc)
{
    return ephyrGLXDestroyContextReal(a_cl, a_pc, FALSE);
}

int
ephyrGLXDestroyContextSwap(__GLXclientState * a_cl, GLbyte * a_pc)
{
    return ephyrGLXDestroyContextReal(a_cl, a_pc, TRUE);
}

static int
ephyrGLXMakeCurrentReal(__GLXclientState * a_cl, GLXDrawable write,
                        GLXDrawable read, GLXContextTag ctx,
                        GLXContextTag old_ctx, Bool a_do_swap)
{
    int res = BadImplementation;
    xGLXMakeCurrentReply reply;
    DrawablePtr drawableR = NULL, drawableW = NULL;
    GLXContextTag new_ctx = 0;

    EPHYR_LOG("enter\n");
    res = dixLookupDrawable(&drawableW, write, a_cl->client, 0, DixReadAccess);
    EPHYR_RETURN_VAL_IF_FAIL(drawableW, BadValue);
    EPHYR_RETURN_VAL_IF_FAIL(drawableW->pScreen, BadValue);
    EPHYR_LOG("screen nummber requested:%d\n", drawableW->pScreen->myNum);

    if (read != write) {
        res = dixLookupDrawable(&drawableR, read, a_cl->client, 0,
                                DixReadAccess);
        EPHYR_RETURN_VAL_IF_FAIL(drawableR, BadValue);
        EPHYR_RETURN_VAL_IF_FAIL(drawableR->pScreen, BadValue);
    }
    else {
        drawableR = drawableW;
    }

    if (!ephyrHostGLXMakeCurrent(hostx_get_window(drawableW->pScreen->myNum),
                                 hostx_get_window(drawableR->pScreen->myNum),
                                 ctx, old_ctx, (int *) &new_ctx)) {
        EPHYR_LOG_ERROR("ephyrHostGLXMakeCurrent() failed\n");
        goto out;
    }
    reply = (xGLXMakeCurrentReply) {
        .type = X_Reply,
        .sequenceNumber = a_cl->client->sequence,
        .length = 0,
        .contextTag = new_ctx
    };
    if (a_do_swap) {
        __GLX_DECLARE_SWAP_VARIABLES;
        __GLX_SWAP_SHORT(&reply.sequenceNumber);
        __GLX_SWAP_INT(&reply.length);
        __GLX_SWAP_INT(&reply.contextTag);
    }
    WriteToClient(a_cl->client, sz_xGLXMakeCurrentReply, &reply);

    res = Success;
 out:
    EPHYR_LOG("leave\n");
    return res;
}

int
ephyrGLXMakeCurrent(__GLXclientState * a_cl, GLbyte * a_pc)
{
    xGLXMakeCurrentReq *req = (xGLXMakeCurrentReq *) a_pc;
    return ephyrGLXMakeCurrentReal(a_cl, req->drawable, req->drawable,
                                   req->context, req->oldContextTag, FALSE);
}

int
ephyrGLXMakeCurrentSwap(__GLXclientState * a_cl, GLbyte * a_pc)
{
    xGLXMakeCurrentReq *req = (xGLXMakeCurrentReq *) a_pc;
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(&req->drawable);
    __GLX_SWAP_INT(&req->context);
    __GLX_SWAP_INT(&req->oldContextTag);

    return ephyrGLXMakeCurrentReal(a_cl, req->drawable, req->drawable,
                                   req->context, req->oldContextTag, TRUE);
}

int
ephyrGLXMakeCurrentReadSGI(__GLXclientState * a_cl, GLbyte * a_pc)
{
    xGLXMakeCurrentReadSGIReq *req = (xGLXMakeCurrentReadSGIReq *) a_pc;

    return ephyrGLXMakeCurrentReal(a_cl, req->drawable, req->readable,
                                   req->context, req->oldContextTag, FALSE);
}

int
ephyrGLXMakeCurrentReadSGISwap(__GLXclientState * a_cl, GLbyte * a_pc)
{
    xGLXMakeCurrentReadSGIReq *req = (xGLXMakeCurrentReadSGIReq *) a_pc;
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(&req->drawable);
    __GLX_SWAP_INT(&req->readable);
    __GLX_SWAP_INT(&req->context);
    __GLX_SWAP_INT(&req->oldContextTag);

    return ephyrGLXMakeCurrentReal(a_cl, req->drawable, req->readable,
                                   req->context, req->oldContextTag, TRUE);
}

int
ephyrGLXMakeContextCurrent(__GLXclientState * a_cl, GLbyte * a_pc)
{
    xGLXMakeContextCurrentReq *req = (xGLXMakeContextCurrentReq *) a_pc;

    return ephyrGLXMakeCurrentReal(a_cl, req->drawable, req->readdrawable,
                                   req->context, req->oldContextTag, FALSE);
}

int
ephyrGLXMakeContextCurrentSwap(__GLXclientState * a_cl, GLbyte * a_pc)
{
    xGLXMakeContextCurrentReq *req = (xGLXMakeContextCurrentReq *) a_pc;
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(&req->drawable);
    __GLX_SWAP_INT(&req->readdrawable);
    __GLX_SWAP_INT(&req->context);
    __GLX_SWAP_INT(&req->oldContextTag);

    return ephyrGLXMakeCurrentReal(a_cl, req->drawable, req->readdrawable,
                                   req->context, req->oldContextTag, TRUE);
}

static int
ephyrGLXGetStringReal(__GLXclientState * a_cl, GLbyte * a_pc, Bool a_do_swap)
{
    ClientPtr client = NULL;
    int context_tag = 0, name = 0, res = BadImplementation, length = 0;
    char *string = NULL;

    __GLX_DECLARE_SWAP_VARIABLES;

    EPHYR_RETURN_VAL_IF_FAIL(a_cl && a_pc, BadValue);

    EPHYR_LOG("enter\n");

    client = a_cl->client;

    if (a_do_swap) {
        __GLX_SWAP_INT(a_pc + 4);
        __GLX_SWAP_INT(a_pc + __GLX_SINGLE_HDR_SIZE);
    }
    context_tag = __GLX_GET_SINGLE_CONTEXT_TAG(a_pc);
    a_pc += __GLX_SINGLE_HDR_SIZE;
    name = *(GLenum *) (a_pc + 0);
    EPHYR_LOG("context_tag:%d, name:%d\n", context_tag, name);
    if (!ephyrHostGLXGetString(context_tag, name, &string)) {
        EPHYR_LOG_ERROR("failed to get string from server\n");
        goto out;
    }
    if (string) {
        length = strlen(string) + 1;
        EPHYR_LOG("got string:'%s', size:%d\n", string, length);
    }
    else {
        EPHYR_LOG("got string: string (null)\n");
    }
    __GLX_BEGIN_REPLY(length);
    __GLX_PUT_SIZE(length);
    __GLX_SEND_HEADER();
    if (a_do_swap) {
        __GLX_SWAP_REPLY_SIZE();
        __GLX_SWAP_REPLY_HEADER();
    }
    WriteToClient(client, length, string);

    res = Success;
 out:
    EPHYR_LOG("leave\n");
    return res;
}

int
ephyrGLXGetString(__GLXclientState * a_cl, GLbyte * a_pc)
{
    return ephyrGLXGetStringReal(a_cl, a_pc, FALSE);
}

int
ephyrGLXGetStringSwap(__GLXclientState * a_cl, GLbyte * a_pc)
{
    return ephyrGLXGetStringReal(a_cl, a_pc, TRUE);
}

static int
ephyrGLXGetIntegervReal(__GLXclientState * a_cl, GLbyte * a_pc, Bool a_do_swap)
{
    int res = BadImplementation;
    xGLXSingleReq *const req = (xGLXSingleReq *) a_pc;
    GLenum int_name;
    int value = 0;
    GLint answer_buf_room[200];
    GLint *buf = NULL;

    EPHYR_LOG("enter\n");

    a_pc += __GLX_SINGLE_HDR_SIZE;

    int_name = *(GLenum *) (a_pc + 0);
    if (!ephyrHostGetIntegerValue(req->contextTag, int_name, &value)) {
        EPHYR_LOG_ERROR("ephyrHostGetIntegerValue() failed\n");
        goto out;
    }
    buf = __glXGetAnswerBuffer(a_cl, sizeof(value),
                               answer_buf_room, sizeof(answer_buf_room), 4);

    if (!buf) {
        EPHYR_LOG_ERROR("failed to allocate reply buffer\n");
        res = BadAlloc;
        goto out;
    }
    __glXSendReply(a_cl->client, buf, 1, sizeof(value), GL_FALSE, 0);
    res = Success;

 out:
    EPHYR_LOG("leave\n");
    return res;
}

int
ephyrGLXGetIntegerv(__GLXclientState * a_cl, GLbyte * a_pc)
{
    return ephyrGLXGetIntegervReal(a_cl, a_pc, FALSE);
}

int
ephyrGLXGetIntegervSwap(__GLXclientState * a_cl, GLbyte * a_pc)
{
    return ephyrGLXGetIntegervReal(a_cl, a_pc, TRUE);
}

static int
ephyrGLXIsDirectReal(__GLXclientState * a_cl, GLbyte * a_pc, Bool a_do_swap)
{
    int res = BadImplementation;
    ClientPtr client = a_cl->client;
    xGLXIsDirectReq *req = (xGLXIsDirectReq *) a_pc;
    xGLXIsDirectReply reply;
    int is_direct = 0;

    EPHYR_RETURN_VAL_IF_FAIL(a_cl && a_pc, FALSE);

    EPHYR_LOG("enter\n");

    if (!ephyrHostIsContextDirect(req->context, (int *) &is_direct)) {
        EPHYR_LOG_ERROR("ephyrHostIsContextDirect() failed\n");
        goto out;
    }
    reply = (xGLXIsDirectReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .isDirect = is_direct
    };

    WriteToClient(client, sz_xGLXIsDirectReply, &reply);
    res = Success;

 out:
    EPHYR_LOG("leave\n");
    return res;
}

int
ephyrGLXIsDirect(__GLXclientState * a_cl, GLbyte * a_pc)
{
    return ephyrGLXIsDirectReal(a_cl, a_pc, FALSE);
}

int
ephyrGLXIsDirectSwap(__GLXclientState * a_cl, GLbyte * a_pc)
{
    return ephyrGLXIsDirectReal(a_cl, a_pc, TRUE);
}
