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

#include <X11/Xdefs.h>
#include <X11/Xmd.h>
#include <GL/glxproto.h>
#include <xcb/glx.h>
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
     xcb_glx_get_visual_configs_reply_t *reply,
     int32_t a_screen,
     int32_t *a_num_visuals,
     int32_t *a_num_props,
     int32_t *a_props_buf_size,
     int32_t **a_props_buf);

Bool
ephyrHostGLXQueryVersion(int *a_major, int *a_minor)
{
    Bool is_ok = FALSE;
    xcb_connection_t *conn = hostx_get_xcbconn();
    xcb_glx_query_version_cookie_t cookie;
    xcb_glx_query_version_reply_t *reply;

    EPHYR_RETURN_VAL_IF_FAIL(a_major && a_minor, FALSE);
    EPHYR_LOG("enter\n");

    if (glx_major) {
        *a_major = glx_major;
        *a_minor = glx_minor;
        return TRUE;
    }

    /* Send the glXQueryVersion request */
    cookie = xcb_glx_query_version(conn, 2, 1);
    reply = xcb_glx_query_version_reply(conn, cookie, NULL);
    if (!reply)
        goto out;
    *a_major = reply->major_version;
    *a_minor = reply->minor_version;
    free(reply);

    EPHYR_LOG("major:%d, minor:%d\n", *a_major, *a_minor);

    is_ok = TRUE;
 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

Bool
ephyrHostGLXGetString(int a_context_tag,
                      int a_string_name,
                      char **a_string)
{
    Bool is_ok = FALSE;
    xcb_connection_t *conn = hostx_get_xcbconn();
    xcb_glx_get_string_cookie_t cookie;
    xcb_glx_get_string_reply_t *reply;

    EPHYR_RETURN_VAL_IF_FAIL(conn && a_string, FALSE);

    EPHYR_LOG("enter\n");
    cookie = xcb_glx_get_string(conn, a_context_tag, a_string_name);
    reply = xcb_glx_get_string_reply(conn, cookie, NULL);
    if (!reply)
        goto out;
    *a_string = malloc(reply->n + 1);
    memcpy(*a_string, xcb_glx_get_string_string(reply), reply->n);
    (*a_string)[reply->n] = '\0';
    free(reply);
    is_ok = TRUE;
out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

Bool ephyrHostGLXQueryServerString(int a_screen_number,
                                   int a_string_name,
                                   char **a_string)
{
    Bool is_ok = FALSE;
    xcb_connection_t *conn = hostx_get_xcbconn();
    int default_screen = hostx_get_screen();
    xcb_glx_query_server_string_cookie_t cookie;
    xcb_glx_query_server_string_reply_t *reply;

    EPHYR_RETURN_VAL_IF_FAIL(conn && a_string, FALSE);

    EPHYR_LOG("enter\n");
    cookie = xcb_glx_query_server_string(conn, default_screen, a_string_name);
    reply = xcb_glx_query_server_string_reply(conn, cookie, NULL);
    if (!reply)
        goto out;
    *a_string = malloc(reply->str_len + 1);
    memcpy(*a_string, xcb_glx_query_server_string_string(reply), reply->str_len);
    (*a_string)[reply->str_len] = '\0';
    free(reply);
    is_ok = TRUE;
out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

static Bool
ephyrHostGLXGetVisualConfigsInternal(enum VisualConfRequestType a_type,
                                     xcb_glx_get_visual_configs_reply_t *reply,
                                     int32_t a_screen,
                                     int32_t * a_num_visuals,
                                     int32_t * a_num_props,
                                     int32_t * a_props_buf_size,
                                     int32_t ** a_props_buf)
{
    Bool is_ok = FALSE;
    int num_props = 0, num_visuals = 0, props_buf_size = 0;
    int props_per_visual_size = 0;
    int32_t *props_buf = NULL;

   if (!reply->num_visuals) {
        EPHYR_LOG_ERROR("screen does not support GL rendering\n");
        goto out;
    }
    num_visuals = reply->num_visuals;

    num_props = reply->num_properties;

    if (a_type != EPHYR_GET_VISUAL_CONFIGS) {
        num_props *= 2;
    }
    props_per_visual_size = num_props * sizeof(uint32_t);
    props_buf_size = props_per_visual_size * reply->num_visuals;
    props_buf = malloc(props_buf_size);
    if (!props_buf)
        goto out;
    memcpy(props_buf, xcb_glx_get_visual_configs_property_list(reply),
           props_buf_size);

    *a_num_visuals = num_visuals;
    *a_num_props = reply->num_properties;
    *a_props_buf_size = props_buf_size;
    *a_props_buf = props_buf;
    is_ok = TRUE;

out:
    return is_ok;
}

Bool
ephyrHostGLXGetVisualConfigs(int32_t a_screen,
                             int32_t * a_num_visuals,
                             int32_t * a_num_props,
                             int32_t * a_props_buf_size, int32_t ** a_props_buf)
{
    Bool is_ok = FALSE;
    xcb_glx_get_visual_configs_cookie_t cookie;
    xcb_glx_get_visual_configs_reply_t *reply;
    xcb_connection_t *conn = hostx_get_xcbconn();
    int screen = hostx_get_screen();

    EPHYR_LOG("enter\n");
    cookie = xcb_glx_get_visual_configs(conn, screen);
    reply = xcb_glx_get_visual_configs_reply(conn, cookie, NULL);
    if (!reply)
        goto out;
    is_ok = ephyrHostGLXGetVisualConfigsInternal
        (EPHYR_GET_VISUAL_CONFIGS,
         reply,
         a_screen,
         a_num_visuals,
         a_num_props,
         a_props_buf_size,
         a_props_buf);

out:
    free(reply);
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
    Bool is_ok=FALSE;
    xcb_connection_t *conn = hostx_get_xcbconn();
    int screen = hostx_get_screen();
    xcb_glx_vendor_private_with_reply_cookie_t cookie;
    union {
        xcb_glx_vendor_private_with_reply_reply_t *vprep;
        xcb_glx_get_visual_configs_reply_t *rep;
    } reply;

    EPHYR_LOG("enter\n");
    cookie = xcb_glx_vendor_private_with_reply(conn,
                                               X_GLXvop_GetFBConfigsSGIX,
                                               0, 4, (uint8_t *)&screen);
    reply.vprep = xcb_glx_vendor_private_with_reply_reply(conn, cookie, NULL);
    if (!reply.vprep)
        goto out;
    is_ok = ephyrHostGLXGetVisualConfigsInternal
        (EPHYR_VENDOR_PRIV_GET_FB_CONFIG_SGIX,
         reply.rep,
         a_screen,
         a_num_visuals,
         a_num_props,
         a_props_buf_size,
         a_props_buf);
out:
    free(reply.vprep);
    EPHYR_LOG("leave\n");
    return is_ok;
}

Bool
ephyrHostGLXSendClientInfo(int32_t a_major, int32_t a_minor,
                           const char *a_extension_list)
{
    xcb_connection_t *conn = hostx_get_xcbconn();
    int size;

    EPHYR_RETURN_VAL_IF_FAIL(conn && a_extension_list, FALSE);

    size = strlen (a_extension_list) + 1;
    xcb_glx_client_info(conn, a_major, a_minor, size, a_extension_list);

    return TRUE;
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
    xcb_connection_t *conn = hostx_get_xcbconn();
    Bool is_ok = FALSE;
    int remote_context_id = 0;

    EPHYR_LOG("enter. screen:%d, generic_id:%d, contextid:%d, rendertype:%d, "
                 "direct:%d\n", a_screen, a_generic_id, a_context_id,
                 a_render_type, a_direct);

    if (!hostx_allocate_resource_id_peer(a_context_id, &remote_context_id)) {
        EPHYR_LOG_ERROR("failed to peer the context id %d host X",
                        remote_context_id);
        goto out;
    }

    switch (code) {
    case X_GLXCreateContext: {
        xcb_glx_create_context(conn,
                               remote_context_id,
                               a_generic_id,
                               hostx_get_screen(),
                               a_share_list_ctxt_id,
                               a_direct);
   }

    case X_GLXCreateNewContext: {
        xcb_glx_create_new_context(conn,
                                   remote_context_id,
                                   a_generic_id,
                                   hostx_get_screen(),
                                   a_render_type,
                                   a_share_list_ctxt_id,
                                   a_direct);
    }

    default:
        /* This should never be reached !*/
        EPHYR_LOG("Internal error! Invalid CreateContext code!\n");
    }

    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

Bool
ephyrHostDestroyContext(int a_ctxt_id)
{
    xcb_connection_t *conn = hostx_get_xcbconn();
    Bool is_ok = FALSE;
    int remote_ctxt_id = 0;

    EPHYR_LOG("enter:%d\n", a_ctxt_id);

    if (!hostx_get_resource_id_peer(a_ctxt_id, &remote_ctxt_id)) {
        EPHYR_LOG_ERROR("failed to get remote glx ctxt id\n");
        goto out;
    }
    EPHYR_LOG("host context id:%d\n", remote_ctxt_id);

    xcb_glx_destroy_context(conn, remote_ctxt_id);

    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

Bool
ephyrHostGLXMakeCurrent(int a_drawable, int a_readable,
                        int a_glx_ctxt_id, int a_old_ctxt_tag, int *a_ctxt_tag)
{
    xcb_connection_t *conn = hostx_get_xcbconn();
    Bool is_ok = FALSE;
    int remote_glx_ctxt_id = 0;

    EPHYR_RETURN_VAL_IF_FAIL(a_ctxt_tag, FALSE);

    EPHYR_LOG("enter. drawable:%d, read:%d, context:%d, oldtag:%d\n",
              a_drawable, a_readable, a_glx_ctxt_id, a_old_ctxt_tag);

    if (!hostx_get_resource_id_peer(a_glx_ctxt_id, &remote_glx_ctxt_id)) {
        EPHYR_LOG_ERROR("failed to get remote glx ctxt id\n");
        goto out;
    }

    /* If both drawables are the same, use the old MakeCurrent request.
     * Otherwise, if we have GLX 1.3 or higher, use the MakeContextCurrent
     * request which supports separate read and draw targets.  Failing that,
     * try the SGI MakeCurrentRead extension.  Logic cribbed from Mesa. */
    if (a_drawable == a_readable) {
        xcb_glx_make_current_cookie_t cookie;
        xcb_glx_make_current_reply_t *reply;
        cookie = xcb_glx_make_current(conn,
                                      a_drawable,
                                      remote_glx_ctxt_id,
                                      a_old_ctxt_tag);
        reply = xcb_glx_make_current_reply(conn, cookie, NULL);
        if (!reply)
            goto out;
        *a_ctxt_tag = reply->context_tag;
        free(reply);
    }
    else if (glx_major > 1 || glx_minor >= 3) {
        xcb_glx_make_context_current_cookie_t cookie;
        xcb_glx_make_context_current_reply_t *reply;
        cookie = xcb_glx_make_context_current(conn,
                                              a_old_ctxt_tag,
                                              a_drawable,
                                              a_readable,
                                              remote_glx_ctxt_id);
        reply = xcb_glx_make_context_current_reply(conn, cookie, NULL);
        if (!reply)
            goto out;
        *a_ctxt_tag = reply->context_tag;
        free(reply);
    }
    else {
        xcb_glx_vendor_private_with_reply_cookie_t cookie;
        xcb_glx_vendor_private_with_reply_reply_t *reply;
        uint32_t data[3] = {
            a_drawable, a_readable, remote_glx_ctxt_id,
        };

        EPHYR_LOG("enter\n");
        cookie = xcb_glx_vendor_private_with_reply(conn,
                                                   X_GLXvop_MakeCurrentReadSGI,
                                                   a_old_ctxt_tag,
                                                   sizeof(data),
                                                   (uint8_t *)data);
        reply = xcb_glx_vendor_private_with_reply_reply(conn, cookie, NULL);

        *a_ctxt_tag = reply->retval;

        free(reply);
    }

    EPHYR_LOG("context tag:%d\n", *a_ctxt_tag);
    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

Bool
ephyrHostGetIntegerValue(int a_current_context_tag, int a_int, int *a_val)
{
    xcb_connection_t *conn = hostx_get_xcbconn();
    Bool is_ok = FALSE;
    int size = 0;
    xcb_glx_get_integerv_cookie_t cookie;
    xcb_glx_get_integerv_reply_t *reply;

    EPHYR_RETURN_VAL_IF_FAIL(a_val, FALSE);

    EPHYR_LOG("enter\n");
    cookie = xcb_glx_get_integerv(conn, a_current_context_tag, a_int);
    reply = xcb_glx_get_integerv_reply(conn, cookie, NULL);
    if (!reply)
        goto out;
    size = reply->n;
    if (!size) {
        EPHYR_LOG_ERROR("X_GLsop_GetIngerv failed\n");
        goto out;
    }
    *a_val = reply->datum;
    is_ok = TRUE;

out:
    free(reply);
    EPHYR_LOG("leave\n");
    return is_ok;
}

Bool
ephyrHostIsContextDirect(int a_ctxt_id, int *a_is_direct)
{
    Bool is_ok = FALSE;
    xcb_connection_t *conn = hostx_get_xcbconn();
    xcb_glx_is_direct_cookie_t cookie;
    xcb_glx_is_direct_reply_t *reply = NULL;
    int remote_glx_ctxt_id = 0;

    EPHYR_LOG("enter\n");
    if (!hostx_get_resource_id_peer (a_ctxt_id, &remote_glx_ctxt_id)) {
        EPHYR_LOG_ERROR ("failed to get remote glx ctxt id\n");
        goto out;
    }

    /* Send the glXIsDirect request */
    cookie = xcb_glx_is_direct(conn, remote_glx_ctxt_id);
    reply = xcb_glx_is_direct_reply(conn, cookie, NULL);
    if (!reply) {
        EPHYR_LOG_ERROR("fail in reading reply from host\n");
        goto out;
    }
    *a_is_direct = reply->is_direct;
    is_ok = TRUE;

out:
    free(reply);
    EPHYR_LOG("leave\n");
    return is_ok;
}
