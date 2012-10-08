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
 * a lots of the content of this file has been adapted from the mesa source
 * code.
 * Authors:
 *    Dodji Seketeli <dodji@openedhand.com>
 */
#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

/*
 * including some server headers (like kdrive-config.h)
 * might define the macro _XSERVER64
 * on 64 bits machines. That macro must _NOT_ be defined for Xlib
 * client code, otherwise bad things happen.
 * So let's undef that macro if necessary.
 */
#ifdef _XSERVER64
#undef _XSERVER64
#endif

#include <X11/Xlibint.h>
#include <GL/glx.h>
#include <GL/internal/glcore.h>
#include <GL/glxproto.h>
#include <GL/glxint.h>
#include "ephyrhostglx.h"
#define _HAVE_XALLOC_DECLS
#include "ephyrlog.h"
#include "hostx.h"

static int glx_major, glx_minor;

enum VisualConfRequestType {
    EPHYR_GET_FB_CONFIG,
    EPHYR_VENDOR_PRIV_GET_FB_CONFIG_SGIX,
    EPHYR_GET_VISUAL_CONFIGS
};

static Bool ephyrHostGLXGetVisualConfigsInternal
    (enum VisualConfRequestType a_type,
     int32_t a_screen,
     int32_t * a_num_visuals,
     int32_t * a_num_props, int32_t * a_props_buf_size, int32_t ** a_props_buf);
Bool
ephyrHostGLXGetMajorOpcode(int *a_opcode)
{
    Bool is_ok = FALSE;
    Display *dpy = hostx_get_display();
    static int opcode;
    int first_event_return = 0, first_error_return = 0;

    EPHYR_RETURN_VAL_IF_FAIL(dpy, FALSE);
    EPHYR_LOG("enter\n");
    if (!opcode) {
        if (!XQueryExtension(dpy, GLX_EXTENSION_NAME, &opcode,
                             &first_event_return, &first_error_return)) {
            EPHYR_LOG_ERROR("XQueryExtension() failed\n");
            goto out;
        }
    }
    *a_opcode = opcode;
    is_ok = TRUE;
 out:
    EPHYR_LOG("release\n");
    return is_ok;
}

Bool
ephyrHostGLXQueryVersion(int *a_major, int *a_minor)
{
    Bool is_ok = FALSE;
    Display *dpy = hostx_get_display();
    int major_opcode = 0;
    xGLXQueryVersionReq *req = NULL;
    xGLXQueryVersionReply reply;

    EPHYR_RETURN_VAL_IF_FAIL(a_major && a_minor, FALSE);
    EPHYR_LOG("enter\n");

    if (glx_major) {
        *a_major = glx_major;
        *a_minor = glx_minor;
        return TRUE;
    }

    if (!ephyrHostGLXGetMajorOpcode(&major_opcode)) {
        EPHYR_LOG_ERROR("failed to get major opcode\n");
        goto out;
    }
    EPHYR_LOG("major opcode: %d\n", major_opcode);

    /* Send the glXQueryVersion request */
    memset(&reply, 0, sizeof(reply));
    LockDisplay(dpy);
    GetReq(GLXQueryVersion, req);
    req->reqType = major_opcode;
    req->glxCode = X_GLXQueryVersion;
    req->majorVersion = 2;
    req->minorVersion = 1;
    _XReply(dpy, (xReply *) & reply, 0, False);
    UnlockDisplay(dpy);
    SyncHandle();

    *a_major = glx_major = reply.majorVersion;
    *a_minor = glx_minor = reply.minorVersion;

    EPHYR_LOG("major:%d, minor:%d\n", *a_major, *a_minor);

    is_ok = TRUE;
 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

/**
 * GLX protocol structure for the ficticious "GXLGenericGetString" request.
 * 
 * This is a non-existant protocol packet.  It just so happens that all of
 * the real protocol packets used to request a string from the server have
 * an identical binary layout.  The only difference between them is the
 * meaning of the \c for_whom field and the value of the \c glxCode.
 * (this has been copied from the mesa source code)
 */
typedef struct GLXGenericGetString {
    CARD8 reqType;
    CARD8 glxCode;
    CARD16 length B16;
    CARD32 for_whom B32;
    CARD32 name B32;
} xGLXGenericGetStringReq;

/* These defines are only needed to make the GetReq macro happy.
 */
#define sz_xGLXGenericGetStringReq 12
#define X_GLXGenericGetString 0

Bool
ephyrHostGLXGetStringFromServer(int a_screen_number,
                                int a_string_name,
                                enum EphyrHostGLXGetStringOps a_op,
                                char **a_string)
{
    Bool is_ok = FALSE;
    Display *dpy = hostx_get_display();
    int default_screen = DefaultScreen(dpy);
    xGLXGenericGetStringReq *req = NULL;
    xGLXSingleReply reply;
    int length = 0, numbytes = 0, major_opcode = 0, get_string_op = 0;

    EPHYR_RETURN_VAL_IF_FAIL(dpy && a_string, FALSE);

    EPHYR_LOG("enter\n");
    switch (a_op) {
    case EPHYR_HOST_GLX_QueryServerString:
        get_string_op = X_GLXQueryServerString;
        break;
    case EPHYR_HOST_GLX_GetString:
        get_string_op = X_GLsop_GetString;
        EPHYR_LOG("Going to glXGetString. strname:%#x, ctxttag:%d\n",
                  a_string_name, a_screen_number);
        break;
    default:
        EPHYR_LOG_ERROR("unknown EphyrHostGLXGetStringOp:%d\n", a_op);
        goto out;
    }

    if (!ephyrHostGLXGetMajorOpcode(&major_opcode)) {
        EPHYR_LOG_ERROR("failed to get major opcode\n");
        goto out;
    }
    EPHYR_LOG("major opcode: %d\n", major_opcode);

    LockDisplay(dpy);

    /* All of the GLX protocol requests for getting a string from the server
     * look the same.  The exact meaning of the a_for_whom field is usually
     * either the screen number (for glXQueryServerString) or the context tag
     * (for GLXSingle).
     */
    GetReq(GLXGenericGetString, req);
    req->reqType = major_opcode;
    req->glxCode = get_string_op;
    req->for_whom = default_screen;
    req->name = a_string_name;

    _XReply(dpy, (xReply *) & reply, 0, False);

    length = reply.length * 4;
    if (!length) {
        numbytes = 0;
    }
    else {
        numbytes = reply.size;
    }
    EPHYR_LOG("going to get a string of size:%d\n", numbytes);

    *a_string = (char *) Xmalloc(numbytes + 1);
    if (!a_string) {
        EPHYR_LOG_ERROR("allocation failed\n");
        goto out;
    }

    memset(*a_string, 0, numbytes + 1);
    if (_XRead(dpy, *a_string, numbytes)) {
        UnlockDisplay(dpy);
        SyncHandle();
        EPHYR_LOG_ERROR("read failed\n");
        goto out;
    }
    length -= numbytes;
    _XEatData(dpy, length);
    UnlockDisplay(dpy);
    SyncHandle();
    EPHYR_LOG("strname:%#x, strvalue:'%s', strlen:%d\n",
              a_string_name, *a_string, numbytes);

    is_ok = TRUE;
 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

static Bool
ephyrHostGLXGetVisualConfigsInternal(enum VisualConfRequestType a_type,
                                     int32_t a_screen,
                                     int32_t * a_num_visuals,
                                     int32_t * a_num_props,
                                     int32_t * a_props_buf_size,
                                     int32_t ** a_props_buf)
{
    Bool is_ok = FALSE;
    Display *dpy = hostx_get_display();
    xGLXGetVisualConfigsReq *req;
    xGLXGetFBConfigsReq *fb_req;
    xGLXVendorPrivateWithReplyReq *vpreq;
    xGLXGetFBConfigsSGIXReq *sgi_req;
    xGLXGetVisualConfigsReply reply;
    char *server_glx_version = NULL, *server_glx_extensions = NULL;
    int j = 0,
        screens = 0,
        major_opcode = 0,
        num_props = 0,
        num_visuals = 0, props_buf_size = 0, props_per_visual_size = 0;
    int32_t *props_buf = NULL;

    EPHYR_RETURN_VAL_IF_FAIL(dpy, FALSE);

    screens = ScreenCount(dpy);
    if (!ephyrHostGLXGetMajorOpcode(&major_opcode)) {
        EPHYR_LOG_ERROR("failed to get opcode\n");
        goto out;
    }

    LockDisplay(dpy);
    switch (a_type) {
    case EPHYR_GET_FB_CONFIG:
        GetReq(GLXGetFBConfigs, fb_req);
        fb_req->reqType = major_opcode;
        fb_req->glxCode = X_GLXGetFBConfigs;
        fb_req->screen = DefaultScreen(dpy);
        break;

    case EPHYR_VENDOR_PRIV_GET_FB_CONFIG_SGIX:
        GetReqExtra(GLXVendorPrivateWithReply,
                    sz_xGLXGetFBConfigsSGIXReq
                    - sz_xGLXVendorPrivateWithReplyReq, vpreq);
        sgi_req = (xGLXGetFBConfigsSGIXReq *) vpreq;
        sgi_req->reqType = major_opcode;
        sgi_req->glxCode = X_GLXVendorPrivateWithReply;
        sgi_req->vendorCode = X_GLXvop_GetFBConfigsSGIX;
        sgi_req->screen = DefaultScreen(dpy);
        break;

    case EPHYR_GET_VISUAL_CONFIGS:
        GetReq(GLXGetVisualConfigs, req);
        req->reqType = major_opcode;
        req->glxCode = X_GLXGetVisualConfigs;
        req->screen = DefaultScreen(dpy);
        break;
    }

    if (!_XReply(dpy, (xReply *) & reply, 0, False)) {
        EPHYR_LOG_ERROR("unknown error\n");
        UnlockDisplay(dpy);
        goto out;
    }
    if (!reply.numVisuals) {
        EPHYR_LOG_ERROR("screen does not support GL rendering\n");
        UnlockDisplay(dpy);
        goto out;
    }
    num_visuals = reply.numVisuals;

    /* FIXME: Is the __GLX_MIN_CONFIG_PROPS test correct for
     * FIXME: FBconfigs? 
     */
    /* Check number of properties */
    num_props = reply.numProps;
    if ((num_props < __GLX_MIN_CONFIG_PROPS) ||
        (num_props > __GLX_MAX_CONFIG_PROPS)) {
        /* Huh?  Not in protocol defined limits.  Punt */
        EPHYR_LOG_ERROR("got a bad reply to request\n");
        UnlockDisplay(dpy);
        goto out;
    }

    if (a_type != EPHYR_GET_VISUAL_CONFIGS) {
        num_props *= 2;
    }
    props_per_visual_size = num_props * __GLX_SIZE_INT32;
    props_buf_size = props_per_visual_size * reply.numVisuals;
    props_buf = malloc(props_buf_size);
    for (j = 0; j < reply.numVisuals; j++) {
        if (_XRead(dpy,
                   &((char *) props_buf)[j * props_per_visual_size],
                   props_per_visual_size) != Success) {
            EPHYR_LOG_ERROR("read failed\n");
        }
    }
    UnlockDisplay(dpy);

    *a_num_visuals = num_visuals;
    *a_num_props = reply.numProps;
    *a_props_buf_size = props_buf_size;
    *a_props_buf = props_buf;
    is_ok = TRUE;

 out:
    if (server_glx_version) {
        XFree(server_glx_version);
        server_glx_version = NULL;
    }
    if (server_glx_extensions) {
        XFree(server_glx_extensions);
        server_glx_extensions = NULL;
    }
    SyncHandle();
    return is_ok;
}

Bool
ephyrHostGLXGetVisualConfigs(int32_t a_screen,
                             int32_t * a_num_visuals,
                             int32_t * a_num_props,
                             int32_t * a_props_buf_size, int32_t ** a_props_buf)
{
    Bool is_ok = FALSE;

    EPHYR_LOG("enter\n");
    is_ok = ephyrHostGLXGetVisualConfigsInternal(EPHYR_GET_VISUAL_CONFIGS,
                                                 a_screen,
                                                 a_num_visuals,
                                                 a_num_props,
                                                 a_props_buf_size, a_props_buf);

    EPHYR_LOG("leave:%d\n", is_ok);
    return is_ok;
}

Bool
ephyrHostGLXVendorPrivGetFBConfigsSGIX(int a_screen,
                                       int32_t * a_num_visuals,
                                       int32_t * a_num_props,
                                       int32_t * a_props_buf_size,
                                       int32_t ** a_props_buf)
{
    Bool is_ok = FALSE;

    EPHYR_LOG("enter\n");
    is_ok = ephyrHostGLXGetVisualConfigsInternal
        (EPHYR_VENDOR_PRIV_GET_FB_CONFIG_SGIX,
         a_screen, a_num_visuals, a_num_props, a_props_buf_size, a_props_buf);
    EPHYR_LOG("leave\n");
    return is_ok;
}

Bool
ephyrHostGLXSendClientInfo(int32_t a_major, int32_t a_minor,
                           const char *a_extension_list)
{
    Bool is_ok = FALSE;
    Display *dpy = hostx_get_display();
    xGLXClientInfoReq *req;
    int size;
    int32_t major_opcode = 0;

    EPHYR_RETURN_VAL_IF_FAIL(dpy && a_extension_list, FALSE);

    if (!ephyrHostGLXGetMajorOpcode(&major_opcode)) {
        EPHYR_LOG_ERROR("failed to get major opcode\n");
        goto out;
    }

    LockDisplay(dpy);

    GetReq(GLXClientInfo, req);
    req->reqType = major_opcode;
    req->glxCode = X_GLXClientInfo;
    req->major = a_major;
    req->minor = a_minor;

    size = strlen(a_extension_list) + 1;
    req->length += bytes_to_int32(size);
    req->numbytes = size;
    Data(dpy, a_extension_list, size);

    UnlockDisplay(dpy);
    SyncHandle();

    is_ok = TRUE;

 out:
    return is_ok;
}

Bool
ephyrHostGLXCreateContext(int a_screen,
                          int a_generic_id,
                          int a_context_id,
                          int a_share_list_ctxt_id,
                          int a_render_type,
                          Bool a_direct,
                          int code)
{
    Bool is_ok = FALSE;
    Display *dpy = hostx_get_display();
    int major_opcode = 0, remote_context_id = 0;

    EPHYR_LOG("enter. screen:%d, generic_id:%d, contextid:%d, rendertype:%d, "
                 "direct:%d\n", a_screen, a_generic_id, a_context_id,
                 a_render_type, a_direct);

    if (!hostx_allocate_resource_id_peer(a_context_id, &remote_context_id)) {
        EPHYR_LOG_ERROR("failed to peer the context id %d host X",
                        remote_context_id);
        goto out;
    }

    if (!ephyrHostGLXGetMajorOpcode(&major_opcode)) {
        EPHYR_LOG_ERROR("failed to get major opcode\n");
        goto out;
    }

    LockDisplay(dpy);

    switch (code) {
    case X_GLXCreateContext: {
        /* Send the glXCreateContext request */
        xGLXCreateContextReq *req;
        GetReq(GLXCreateContext, req);
        req->reqType = major_opcode;
        req->glxCode = X_GLXCreateContext;
        req->context = remote_context_id;
        req->visual = a_generic_id;
        req->screen = DefaultScreen(dpy);
        req->shareList = a_share_list_ctxt_id;
        req->isDirect = a_direct;
    }

    case X_GLXCreateNewContext: {
        /* Send the glXCreateNewContext request */
        xGLXCreateNewContextReq *req;
        GetReq(GLXCreateNewContext, req);
        req->reqType = major_opcode;
        req->glxCode = X_GLXCreateNewContext;
        req->context = remote_context_id;
        req->fbconfig = a_generic_id;
        req->screen = DefaultScreen(dpy);
        req->renderType = a_render_type;
        req->shareList = a_share_list_ctxt_id;
        req->isDirect = a_direct;
    }

    default:
        /* This should never be reached !*/
        EPHYR_LOG("Internal error! Invalid CreateContext code!\n");
    }

    UnlockDisplay(dpy);
    SyncHandle();

    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

Bool
ephyrHostDestroyContext(int a_ctxt_id)
{
    Bool is_ok = FALSE;
    Display *dpy = hostx_get_display();
    int major_opcode = 0, remote_ctxt_id = 0;
    xGLXDestroyContextReq *req = NULL;

    EPHYR_LOG("enter:%d\n", a_ctxt_id);

    if (!ephyrHostGLXGetMajorOpcode(&major_opcode)) {
        EPHYR_LOG_ERROR("failed to get major opcode\n");
        goto out;
    }
    if (!hostx_get_resource_id_peer(a_ctxt_id, &remote_ctxt_id)) {
        EPHYR_LOG_ERROR("failed to get remote glx ctxt id\n");
        goto out;
    }
    EPHYR_LOG("host context id:%d\n", remote_ctxt_id);

    LockDisplay(dpy);
    GetReq(GLXDestroyContext, req);
    req->reqType = major_opcode;
    req->glxCode = X_GLXDestroyContext;
    req->context = remote_ctxt_id;
    UnlockDisplay(dpy);
    SyncHandle();

    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

Bool
ephyrHostGLXMakeCurrent(int a_drawable, int a_readable,
                        int a_glx_ctxt_id, int a_old_ctxt_tag, int *a_ctxt_tag)
{
    Bool is_ok = FALSE;
    Display *dpy = hostx_get_display();
    int32_t major_opcode = 0;
    int remote_glx_ctxt_id = 0;
    xGLXMakeCurrentReply reply;

    EPHYR_RETURN_VAL_IF_FAIL(a_ctxt_tag, FALSE);

    EPHYR_LOG("enter. drawable:%d, read:%d, context:%d, oldtag:%d\n",
              a_drawable, a_readable, a_glx_ctxt_id, a_old_ctxt_tag);

    if (!ephyrHostGLXGetMajorOpcode(&major_opcode)) {
        EPHYR_LOG_ERROR("failed to get major opcode\n");
        goto out;
    }
    if (!hostx_get_resource_id_peer(a_glx_ctxt_id, &remote_glx_ctxt_id)) {
        EPHYR_LOG_ERROR("failed to get remote glx ctxt id\n");
        goto out;
    }

    LockDisplay(dpy);

    /* If both drawables are the same, use the old MakeCurrent request.
     * Otherwise, if we have GLX 1.3 or higher, use the MakeContextCurrent
     * request which supports separate read and draw targets.  Failing that,
     * try the SGI MakeCurrentRead extension.  Logic cribbed from Mesa. */
    if (a_drawable == a_readable) {
        xGLXMakeCurrentReq *req;

        GetReq(GLXMakeCurrent, req);
        req->reqType = major_opcode;
        req->glxCode = X_GLXMakeCurrent;
        req->drawable = a_drawable;
        req->context = remote_glx_ctxt_id;
        req->oldContextTag = a_old_ctxt_tag;
    }
    else if (glx_major > 1 || glx_minor >= 3) {
        xGLXMakeContextCurrentReq *req;

        GetReq(GLXMakeContextCurrent, req);
        req->reqType = major_opcode;
        req->glxCode = X_GLXMakeContextCurrent;
        req->drawable = a_drawable;
        req->readdrawable = a_readable;
        req->context = remote_glx_ctxt_id;
        req->oldContextTag = a_old_ctxt_tag;
    }
    else {
        xGLXVendorPrivateWithReplyReq *vpreq;
        xGLXMakeCurrentReadSGIReq *req;

        GetReqExtra(GLXVendorPrivateWithReply,
                    (sz_xGLXMakeCurrentReadSGIReq -
                     sz_xGLXVendorPrivateWithReplyReq),
                    vpreq);
        req = (xGLXMakeCurrentReadSGIReq *) vpreq;
        req->reqType = major_opcode;
        req->glxCode = X_GLXVendorPrivateWithReply;
        req->vendorCode = X_GLXvop_MakeCurrentReadSGI;
        req->drawable = a_drawable;
        req->readable = a_readable;
        req->context = remote_glx_ctxt_id;
        req->oldContextTag = a_old_ctxt_tag;
    }

    memset(&reply, 0, sizeof(reply));
    if (!_XReply(dpy, (xReply *) & reply, 0, False)) {
        EPHYR_LOG_ERROR("failed to get reply from host\n");
        UnlockDisplay(dpy);
        SyncHandle();
        goto out;
    }
    UnlockDisplay(dpy);
    SyncHandle();
    *a_ctxt_tag = reply.contextTag;
    EPHYR_LOG("context tag:%d\n", *a_ctxt_tag);
    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

#define X_GLXSingle 0

#define __EPHYR_GLX_SINGLE_PUT_CHAR(offset,a) \
    *((INT8 *) (pc + offset)) = a

#define EPHYR_GLX_SINGLE_PUT_SHORT(offset,a) \
    *((INT16 *) (pc + offset)) = a

#define EPHYR_GLX_SINGLE_PUT_LONG(offset,a) \
    *((INT32 *) (pc + offset)) = a

#define EPHYR_GLX_SINGLE_PUT_FLOAT(offset,a) \
    *((FLOAT32 *) (pc + offset)) = a

#define EPHYR_GLX_SINGLE_READ_XREPLY()       \
    (void) _XReply(dpy, (xReply*) &reply, 0, False)

#define EPHYR_GLX_SINGLE_GET_RETVAL(a,cast) \
    a = (cast) reply.retval

#define EPHYR_GLX_SINGLE_GET_SIZE(a) \
    a = (GLint) reply.size

#define EPHYR_GLX_SINGLE_GET_CHAR(p) \
    *p = *(GLbyte *)&reply.pad3;

#define EPHYR_GLX_SINGLE_GET_SHORT(p) \
    *p = *(GLshort *)&reply.pad3;

#define EPHYR_GLX_SINGLE_GET_LONG(p) \
    *p = *(GLint *)&reply.pad3;

#define EPHYR_GLX_SINGLE_GET_FLOAT(p) \
    *p = *(GLfloat *)&reply.pad3;

Bool
ephyrHostGetIntegerValue(int a_current_context_tag, int a_int, int *a_val)
{
    Bool is_ok = FALSE;
    Display *dpy = hostx_get_display();
    int major_opcode = 0, size = 0;
    xGLXSingleReq *req = NULL;
    xGLXSingleReply reply;
    unsigned char *pc = NULL;

    EPHYR_RETURN_VAL_IF_FAIL(a_val, FALSE);

    EPHYR_LOG("enter\n");
    if (!ephyrHostGLXGetMajorOpcode(&major_opcode)) {
        EPHYR_LOG_ERROR("failed to get major opcode\n");
        goto out;
    }
    LockDisplay(dpy);
    GetReqExtra(GLXSingle, 4, req);
    req->reqType = major_opcode;
    req->glxCode = X_GLsop_GetIntegerv;
    req->contextTag = a_current_context_tag;
    pc = ((unsigned char *) (req) + sz_xGLXSingleReq);
    EPHYR_GLX_SINGLE_PUT_LONG(0, a_int);
    EPHYR_GLX_SINGLE_READ_XREPLY();
    EPHYR_GLX_SINGLE_GET_SIZE(size);
    if (!size) {
        UnlockDisplay(dpy);
        SyncHandle();
        EPHYR_LOG_ERROR("X_GLsop_GetIngerv failed\n");
        goto out;
    }
    EPHYR_GLX_SINGLE_GET_LONG(a_val);
    UnlockDisplay(dpy);
    SyncHandle();
    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

Bool
ephyrHostIsContextDirect(int a_ctxt_id, int *a_is_direct)
{
    Bool is_ok = FALSE;
    Display *dpy = hostx_get_display();
    xGLXIsDirectReq *req = NULL;
    xGLXIsDirectReply reply;
    int major_opcode = 0, remote_glx_ctxt_id = 0;

    EPHYR_LOG("enter\n");
    if (!ephyrHostGLXGetMajorOpcode(&major_opcode)) {
        EPHYR_LOG_ERROR("failed to get major opcode\n");
        goto out;
    }
    if (!hostx_get_resource_id_peer(a_ctxt_id, &remote_glx_ctxt_id)) {
        EPHYR_LOG_ERROR("failed to get remote glx ctxt id\n");
        goto out;
    }
    memset(&reply, 0, sizeof(reply));

    /* Send the glXIsDirect request */
    LockDisplay(dpy);
    GetReq(GLXIsDirect, req);
    req->reqType = major_opcode;
    req->glxCode = X_GLXIsDirect;
    req->context = remote_glx_ctxt_id;
    if (!_XReply(dpy, (xReply *) & reply, 0, False)) {
        EPHYR_LOG_ERROR("fail in reading reply from host\n");
        UnlockDisplay(dpy);
        SyncHandle();
        goto out;
    }
    UnlockDisplay(dpy);
    SyncHandle();
    *a_is_direct = reply.isDirect;
    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}
