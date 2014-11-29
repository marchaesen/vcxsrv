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

#include <X11/Xdefs.h>
#include <xcb/xf86dri.h>
#include "hostx.h"
#include "ephyrdri.h"
#define _HAVE_XALLOC_DECLS
#include "ephyrlog.h"
#include "dixstruct.h"
#include "pixmapstr.h"

#ifndef TRUE
#define TRUE 1
#endif /*TRUE*/
#ifndef FALSE
#define FALSE 0
#endif /*FALSE*/
    Bool
ephyrDRIQueryDirectRenderingCapable(int a_screen, Bool *a_is_capable)
{
    xcb_connection_t *conn = hostx_get_xcbconn();
    Bool is_ok = FALSE;
    xcb_xf86dri_query_direct_rendering_capable_cookie_t cookie;
    xcb_xf86dri_query_direct_rendering_capable_reply_t *reply;

    EPHYR_RETURN_VAL_IF_FAIL(a_is_capable, FALSE);
    EPHYR_LOG("enter\n");
    cookie = xcb_xf86dri_query_direct_rendering_capable(conn,
                                                        hostx_get_screen());
    reply = xcb_xf86dri_query_direct_rendering_capable_reply(conn, cookie, NULL);
    if (reply) {
        is_ok = TRUE;
        *a_is_capable = reply->is_capable;
        free(reply);
    }
    EPHYR_LOG("leave. is_capable:%d, is_ok=%d\n", *a_is_capable, is_ok);

    return is_ok;
}

Bool
ephyrDRIOpenConnection(int a_screen,
                       drm_handle_t * a_sarea, char **a_bus_id_string)
{
    xcb_connection_t *conn = hostx_get_xcbconn();
    Bool is_ok = FALSE;
    xcb_xf86dri_open_connection_cookie_t cookie;
    xcb_xf86dri_open_connection_reply_t *reply;

    EPHYR_RETURN_VAL_IF_FAIL(a_bus_id_string, FALSE);
    EPHYR_LOG("enter. screen:%d\n", a_screen);
    cookie = xcb_xf86dri_open_connection(conn, hostx_get_screen());
    reply = xcb_xf86dri_open_connection_reply(conn, cookie, NULL);
    if (!reply)
        goto out;
    *a_sarea = reply->sarea_handle_low;
    if (sizeof(drm_handle_t) == 8) {
        int shift = 32;
        *a_sarea |= ((drm_handle_t) reply->sarea_handle_high) << shift;
    }
    *a_bus_id_string = malloc(reply->bus_id_len + 1);
    if (!*a_bus_id_string)
        goto out;
    memcpy(*a_bus_id_string, xcb_xf86dri_open_connection_bus_id(reply), reply->bus_id_len);
    *a_bus_id_string[reply->bus_id_len] = '\0';
    is_ok = TRUE;
out:
    free(reply);
    EPHYR_LOG("leave. bus_id_string:%s, is_ok:%d\n", *a_bus_id_string, is_ok);
    return is_ok;
}

Bool
ephyrDRIAuthConnection(int a_screen, drm_magic_t a_magic)
{
    xcb_connection_t *conn = hostx_get_xcbconn();
    int screen = hostx_get_screen();
    xcb_xf86dri_auth_connection_cookie_t cookie;
    xcb_xf86dri_auth_connection_reply_t *reply;
    Bool is_ok = FALSE;

    EPHYR_LOG("enter\n");
    cookie = xcb_xf86dri_auth_connection(conn, screen, a_magic);
    reply = xcb_xf86dri_auth_connection_reply(conn, cookie, NULL);
    is_ok = reply->authenticated;
    free(reply);
    EPHYR_LOG("leave. is_ok:%d\n", is_ok);
    return is_ok;
}

Bool
ephyrDRICloseConnection(int a_screen)
{
    xcb_connection_t *conn = hostx_get_xcbconn();
    int screen = hostx_get_screen();

    EPHYR_LOG("enter\n");
    xcb_xf86dri_close_connection(conn, screen);
    EPHYR_LOG("leave\n");
    return TRUE;
}

Bool
ephyrDRIGetClientDriverName(int a_screen,
                            int *a_ddx_driver_major_version,
                            int *a_ddx_driver_minor_version,
                            int *a_ddx_driver_patch_version,
                            char **a_client_driver_name)
{
    xcb_connection_t *conn = hostx_get_xcbconn();
    int screen = hostx_get_screen();
    xcb_xf86dri_get_client_driver_name_cookie_t cookie;
    xcb_xf86dri_get_client_driver_name_reply_t *reply;
    Bool is_ok = FALSE;

    EPHYR_RETURN_VAL_IF_FAIL(a_ddx_driver_major_version
                             && a_ddx_driver_minor_version
                             && a_ddx_driver_patch_version
                             && a_client_driver_name, FALSE);
    EPHYR_LOG("enter\n");
    cookie = xcb_xf86dri_get_client_driver_name(conn, screen);
    reply = xcb_xf86dri_get_client_driver_name_reply(conn, cookie, NULL);
    if (!reply)
        goto out;
    *a_ddx_driver_major_version = reply->client_driver_major_version;
    *a_ddx_driver_minor_version = reply->client_driver_minor_version;
    *a_ddx_driver_patch_version = reply->client_driver_patch_version;
    *a_client_driver_name = malloc(reply->client_driver_name_len + 1);
    if (!*a_client_driver_name)
        goto out;
    memcpy(*a_client_driver_name,
           xcb_xf86dri_get_client_driver_name_client_driver_name(reply),
           reply->client_driver_name_len);
    (*a_client_driver_name)[reply->client_driver_name_len] = '\0';
    is_ok = TRUE;
    EPHYR_LOG("major:%d, minor:%d, patch:%d, name:%s\n",
              *a_ddx_driver_major_version,
              *a_ddx_driver_minor_version,
              *a_ddx_driver_patch_version, *a_client_driver_name);
 out:
    free(reply);
    EPHYR_LOG("leave:%d\n", is_ok);
    return is_ok;
}

Bool
ephyrDRICreateContext(int a_screen,
                      int a_visual_id,
                      CARD32 ctxt_id, drm_context_t * a_hw_ctxt)
{
    xcb_connection_t *conn = hostx_get_xcbconn();
    int screen = hostx_get_screen();
    Bool is_ok = FALSE;
    xcb_xf86dri_create_context_cookie_t cookie;
    xcb_xf86dri_create_context_reply_t *reply;

    ctxt_id = xcb_generate_id(conn);

    EPHYR_LOG("enter. screen:%d, visual:%d\n", a_screen, a_visual_id);
    cookie = xcb_xf86dri_create_context(conn, screen, a_visual_id, ctxt_id);
    reply = xcb_xf86dri_create_context_reply(conn, cookie, NULL);
    if (!reply)
        goto out;
    *a_hw_ctxt = reply->hw_context;
    is_ok = TRUE;
out:
    free(reply);
    EPHYR_LOG("leave:%d\n", is_ok);
    return is_ok;
}

Bool
ephyrDRIDestroyContext(int a_screen, int a_context_id)
{
    xcb_connection_t *conn = hostx_get_xcbconn ();
    int screen = hostx_get_screen();

    EPHYR_LOG("enter\n");
    xcb_xf86dri_destroy_context(conn, screen, a_context_id);
    EPHYR_LOG("leave\n");
    return TRUE;
}

Bool
ephyrDRICreateDrawable(int a_screen,
                       int a_drawable, drm_drawable_t * a_hw_drawable)
{
    Bool is_ok = FALSE;
    xcb_connection_t *conn = hostx_get_xcbconn();
    int screen = hostx_get_screen();
    xcb_xf86dri_create_drawable_cookie_t cookie;
    xcb_xf86dri_create_drawable_reply_t *reply;

    EPHYR_LOG("enter\n");
    cookie = xcb_xf86dri_create_drawable(conn, screen, a_drawable);
    reply = xcb_xf86dri_create_drawable_reply(conn, cookie, NULL);
    if (!reply)
        goto out;
    *a_hw_drawable = reply->hw_drawable_handle;
    is_ok = TRUE;
out:
    free(reply);
    EPHYR_LOG("leave. is_ok:%d\n", is_ok);
    return is_ok;
}

Bool
ephyrDRIDestroyDrawable(int a_screen, int a_drawable)
{
    EPHYR_LOG("enter\n");
    EPHYR_LOG_ERROR("not implemented yet\n");
    EPHYR_LOG("leave\n");
    return FALSE;
}

Bool
ephyrDRIGetDrawableInfo(int a_screen,
                        int a_drawable,
                        unsigned int *a_index,
                        unsigned int *a_stamp,
                        int *a_x,
                        int *a_y,
                        int *a_w,
                        int *a_h,
                        int *a_num_clip_rects,
                        drm_clip_rect_t ** a_clip_rects,
                        int *a_back_x,
                        int *a_back_y,
                        int *a_num_back_clip_rects,
                        drm_clip_rect_t ** a_back_clip_rects)
{
    Bool is_ok = FALSE;
    xcb_connection_t *conn = hostx_get_xcbconn();
    int screen = hostx_get_screen();
    xcb_xf86dri_get_drawable_info_cookie_t cookie;
    xcb_xf86dri_get_drawable_info_reply_t *reply = NULL;
    EphyrHostWindowAttributes attrs;

    EPHYR_RETURN_VAL_IF_FAIL(a_x && a_y && a_w && a_h
                             && a_num_clip_rects, FALSE);

    EPHYR_LOG("enter\n");
    memset(&attrs, 0, sizeof(attrs));
    if (!hostx_get_window_attributes(a_drawable, &attrs)) {
        EPHYR_LOG_ERROR("failed to query host window attributes\n");
        goto out;
    }
    cookie = xcb_xf86dri_get_drawable_info(conn, screen, a_drawable);
    reply =  xcb_xf86dri_get_drawable_info_reply(conn, cookie, NULL);
    if (!reply) {
        EPHYR_LOG_ERROR ("XF86DRIGetDrawableInfo ()\n");
        goto out;
    }
    *a_index = reply->drawable_table_index;
    *a_stamp = reply->drawable_table_stamp;
    *a_x = reply->drawable_origin_X;
    *a_y = reply->drawable_origin_Y;
    *a_w = reply->drawable_size_W;
    *a_h = reply->drawable_size_H;
    *a_num_clip_rects = reply->num_clip_rects;
    *a_clip_rects = calloc(*a_num_clip_rects, sizeof(drm_clip_rect_t));
    memcpy(*a_clip_rects, xcb_xf86dri_get_drawable_info_clip_rects(reply),
           *a_num_clip_rects * sizeof(drm_clip_rect_t));
    EPHYR_LOG("host x,y,w,h: (%d,%d,%d,%d)\n", *a_x, *a_y, *a_w, *a_h);
    if (*a_num_clip_rects) {
        free(*a_back_clip_rects);
        *a_back_clip_rects = calloc(*a_num_clip_rects, sizeof(drm_clip_rect_t));
        memmove(*a_back_clip_rects,
                *a_clip_rects, *a_num_clip_rects * sizeof(drm_clip_rect_t));
        *a_num_back_clip_rects = *a_num_clip_rects;
    }
    EPHYR_LOG("num back clip rects:%d, num clip rects:%d\n",
              *a_num_clip_rects, *a_num_back_clip_rects);
    *a_back_x = *a_x;
    *a_back_y = *a_y;
    *a_w = attrs.width;
    *a_h = attrs.height;

    is_ok = TRUE;
 out:
    EPHYR_LOG("leave. index:%d, stamp:%d, x,y:(%d,%d), w,y:(%d,%d)\n",
              *a_index, *a_stamp, *a_x, *a_y, *a_w, *a_h);
    free(reply);
    return is_ok;
}

Bool
ephyrDRIGetDeviceInfo(int a_screen,
                      drm_handle_t * a_frame_buffer,
                      int *a_fb_origin,
                      int *a_fb_size,
                      int *a_fb_stride,
                      int *a_dev_private_size, void **a_dev_private)
{
    Bool is_ok = FALSE;
    xcb_connection_t *conn = hostx_get_xcbconn ();
    int screen = hostx_get_screen();
    xcb_xf86dri_get_device_info_cookie_t cookie;
    xcb_xf86dri_get_device_info_reply_t *reply;

    EPHYR_RETURN_VAL_IF_FAIL(conn, FALSE);
    EPHYR_LOG("enter\n");
    cookie = xcb_xf86dri_get_device_info(conn, screen);
    reply = xcb_xf86dri_get_device_info_reply(conn, cookie, NULL);
    if (!reply)
        goto out;
    *a_frame_buffer = reply->framebuffer_handle_low;
    if (sizeof(drm_handle_t) == 8) {
        int shift = 32;
        *a_frame_buffer |= ((drm_handle_t)reply->framebuffer_handle_high) << shift;
    }
    *a_fb_origin = reply->framebuffer_origin_offset;
    *a_fb_size = reply->framebuffer_size;
    *a_fb_stride = reply->framebuffer_stride;
    *a_dev_private_size = reply->device_private_size;
    *a_dev_private = calloc(reply->device_private_size, 1);
    if (!*a_dev_private)
        goto out;
    memcpy(*a_dev_private,
           xcb_xf86dri_get_device_info_device_private(reply),
           reply->device_private_size);
    is_ok = TRUE;
out:
    free(reply);
    EPHYR_LOG("leave:%d\n", is_ok);
    return is_ok;
}
