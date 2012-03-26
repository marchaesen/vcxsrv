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

#include <X11/Xutil.h>
#include <X11/Xlibint.h>
#include <GL/glx.h>
#include "xf86dri.h"
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
    Display *dpy = hostx_get_display();
    Bool is_ok = FALSE;

    EPHYR_RETURN_VAL_IF_FAIL(a_is_capable, FALSE);
    EPHYR_LOG("enter\n");
    is_ok = XF86DRIQueryDirectRenderingCapable(dpy, DefaultScreen(dpy),
                                               a_is_capable);
    EPHYR_LOG("leave. is_capable:%d, is_ok=%d\n", *a_is_capable, is_ok);

    return is_ok;
}

Bool
ephyrDRIOpenConnection(int a_screen,
                       drm_handle_t * a_sarea, char **a_bus_id_string)
{
    Display *dpy = hostx_get_display();
    Bool is_ok = FALSE;

    EPHYR_RETURN_VAL_IF_FAIL(a_bus_id_string, FALSE);
    EPHYR_LOG("enter. screen:%d\n", a_screen);
    is_ok = XF86DRIOpenConnection(dpy, DefaultScreen(dpy),
                                  a_sarea, a_bus_id_string);
    if (*a_bus_id_string) {
        EPHYR_LOG("leave. bus_id_string:%s, is_ok:%d\n",
                  *a_bus_id_string, is_ok);
    }
    else {
        EPHYR_LOG("leave. bus_id_string:null, is_ok:%d\n", is_ok);
    }
    return is_ok;
}

Bool
ephyrDRIAuthConnection(int a_screen, drm_magic_t a_magic)
{
    Display *dpy = hostx_get_display();
    Bool is_ok = FALSE;

    EPHYR_LOG("enter\n");
    is_ok = XF86DRIAuthConnection(dpy, DefaultScreen(dpy), a_magic);
    EPHYR_LOG("leave. is_ok:%d\n", is_ok);
    return is_ok;
}

Bool
ephyrDRICloseConnection(int a_screen)
{
    Display *dpy = hostx_get_display();
    Bool is_ok = FALSE;

    EPHYR_LOG("enter\n");
    is_ok = XF86DRICloseConnection(dpy, DefaultScreen(dpy));
    EPHYR_LOG("leave\n");
    return is_ok;
}

Bool
ephyrDRIGetClientDriverName(int a_screen,
                            int *a_ddx_driver_major_version,
                            int *a_ddx_driver_minor_version,
                            int *a_ddx_driver_patch_version,
                            char **a_client_driver_name)
{
    Display *dpy = hostx_get_display();
    Bool is_ok = FALSE;

    EPHYR_RETURN_VAL_IF_FAIL(a_ddx_driver_major_version
                             && a_ddx_driver_minor_version
                             && a_ddx_driver_patch_version
                             && a_client_driver_name, FALSE);
    EPHYR_LOG("enter\n");
    is_ok = XF86DRIGetClientDriverName(dpy, DefaultScreen(dpy),
                                       a_ddx_driver_major_version,
                                       a_ddx_driver_minor_version,
                                       a_ddx_driver_patch_version,
                                       a_client_driver_name);
    EPHYR_LOG("major:%d, minor:%d, patch:%d, name:%s\n",
              *a_ddx_driver_major_version,
              *a_ddx_driver_minor_version,
              *a_ddx_driver_patch_version, *a_client_driver_name);
    EPHYR_LOG("leave:%d\n", is_ok);
    return is_ok;
}

Bool
ephyrDRICreateContext(int a_screen,
                      int a_visual_id,
                      XID *a_returned_ctxt_id, drm_context_t * a_hw_ctxt)
{
    Display *dpy = hostx_get_display();
    Bool is_ok = FALSE;
    Visual v;

    EPHYR_LOG("enter. screen:%d, visual:%d\n", a_screen, a_visual_id);
    memset(&v, 0, sizeof(v));
    v.visualid = a_visual_id;
    is_ok = XF86DRICreateContext(dpy,
                                 DefaultScreen(dpy),
                                 &v, a_returned_ctxt_id, a_hw_ctxt);
    EPHYR_LOG("leave:%d\n", is_ok);
    return is_ok;
}

Bool
ephyrDRIDestroyContext(int a_screen, int a_context_id)
{
    Display *dpy = hostx_get_display();
    Bool is_ok = FALSE;

    EPHYR_LOG("enter\n");
    is_ok = XF86DRIDestroyContext(dpy, DefaultScreen(dpy), a_context_id);
    EPHYR_LOG("leave:%d\n", is_ok);
    return is_ok;
}

Bool
ephyrDRICreateDrawable(int a_screen,
                       int a_drawable, drm_drawable_t * a_hw_drawable)
{
    Bool is_ok = FALSE;
    Display *dpy = hostx_get_display();

    EPHYR_LOG("enter\n");
    is_ok = XF86DRICreateDrawable(dpy, DefaultScreen(dpy),
                                  a_drawable, a_hw_drawable);
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
    Display *dpy = hostx_get_display();
    EphyrHostWindowAttributes attrs;

    EPHYR_RETURN_VAL_IF_FAIL(a_x && a_y && a_w && a_h
                             && a_num_clip_rects, FALSE);

    EPHYR_LOG("enter\n");
    memset(&attrs, 0, sizeof(attrs));
    if (!hostx_get_window_attributes(a_drawable, &attrs)) {
        EPHYR_LOG_ERROR("failed to query host window attributes\n");
        goto out;
    }
    if (!XF86DRIGetDrawableInfo(dpy, DefaultScreen(dpy), a_drawable,
                                a_index, a_stamp,
                                a_x, a_y,
                                a_w, a_h,
                                a_num_clip_rects, a_clip_rects,
                                a_back_x, a_back_y,
                                a_num_back_clip_rects, a_back_clip_rects)) {
        EPHYR_LOG_ERROR("XF86DRIGetDrawableInfo ()\n");
        goto out;
    }
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
    Display *dpy = hostx_get_display();

    EPHYR_RETURN_VAL_IF_FAIL(dpy, FALSE);
    EPHYR_LOG("enter\n");
    is_ok = XF86DRIGetDeviceInfo(dpy, DefaultScreen(dpy), a_frame_buffer,
                                 a_fb_origin, a_fb_size, a_fb_stride,
                                 a_dev_private_size, a_dev_private);
    EPHYR_LOG("leave:%d\n", is_ok);
    return is_ok;
}
