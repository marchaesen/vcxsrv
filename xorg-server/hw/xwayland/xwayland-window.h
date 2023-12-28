/*
 * Copyright © 2011-2014 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of the
 * copyright holders not be used in advertising or publicity
 * pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#ifndef XWAYLAND_WINDOW_H
#define XWAYLAND_WINDOW_H

#include <xwayland-config.h>

#include <sys/types.h>

#include <stdio.h>
#include <unistd.h>

#include <X11/X.h>
#include <dix.h>
#include <propertyst.h>
#include <validate.h>
#include <wayland-util.h>
#include <xf86drm.h>

#include "xwayland-types.h"

struct xwl_wl_surface {
    OsTimerPtr wl_surface_destroy_timer;
    struct wl_surface *wl_surface;
    struct xorg_list link;
};

struct xwl_format_table_entry {
    uint32_t format;
    uint32_t pad;
    uint64_t modifier;
};

struct xwl_device_formats {
    drmDevice *drm_dev;
    int supports_scanout;
    uint32_t num_formats;
    struct xwl_format *formats;
};

struct xwl_format_table {
    /* This is mmapped from the fd given to us by the compositor */
    int len;
    struct xwl_format_table_entry *entry;
};

/*
 * Helper struct for sharing dmabuf feedback logic between
 * a screen and a window. The screen will get the default
 * feedback, and a window will get a per-surface feedback.
 */
struct xwl_dmabuf_feedback {
    struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback;
    struct xwl_format_table format_table;
    drmDevice *main_dev;
    /*
     * This will be filled in during wl events and copied to
     * dev_formats on dmabuf_feedback.tranche_done
     */
    struct xwl_device_formats tmp_tranche;
    int feedback_done;
    int dev_formats_len;
    struct xwl_device_formats *dev_formats;
    /*
     * This flag is used to identify if the feedback
     * has been resent. If this is true, then the xwayland
     * clients need to be sent PresentCompleteModeSuboptimalCopy
     * to tell them to re-request modifiers.
     */
    int unprocessed_feedback_pending;
};

struct xwl_window {
    struct xwl_screen *xwl_screen;
    struct wl_surface *surface;
    struct wp_viewport *viewport;
    float scale_x, scale_y;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    WindowPtr window;
    struct xorg_list link_damage;
    struct xorg_list link_window;
    struct wl_callback *frame_callback;
    Bool allow_commits;
    struct xorg_list window_buffers_available;
    struct xorg_list window_buffers_unavailable;
    OsTimerPtr window_buffers_timer;
    struct wl_output *wl_output;
    struct wl_output *wl_output_fullscreen;
#ifdef GLAMOR_HAS_GBM
    struct xorg_list frame_callback_list;
    Bool present_flipped;
#endif
#ifdef XWL_HAS_LIBDECOR
    struct libdecor_frame *libdecor_frame;
#endif
    struct xwayland_surface_v1 *xwayland_surface;
    struct xwl_dmabuf_feedback feedback;
    /* If TRUE, the window buffer format supports scanout with implicit modifier */
    Bool has_implicit_scanout_support;
    struct wp_tearing_control_v1 *tearing_control;
};

struct xwl_window *xwl_window_get(WindowPtr window);
struct xwl_window *xwl_window_from_window(WindowPtr window);

Bool is_surface_from_xwl_window(struct wl_surface *surface);

void xwl_window_update_property(struct xwl_window *xwl_window,
                                PropertyStateRec *propstate);
Bool xwl_window_has_viewport_enabled(struct xwl_window *xwl_window);
Bool xwl_window_is_toplevel(WindowPtr window);
void xwl_window_check_resolution_change_emulation(struct xwl_window *xwl_window);
void xwl_window_rootful_update_title(struct xwl_window *xwl_window);
void xwl_window_rootful_update_fullscreen(struct xwl_window *xwl_window,
                                          struct xwl_output *xwl_output);
void xwl_window_set_window_pixmap(WindowPtr window, PixmapPtr pixmap);

Bool xwl_realize_window(WindowPtr window);
Bool xwl_unrealize_window(WindowPtr window);
Bool xwl_change_window_attributes(WindowPtr window, unsigned long mask);
void xwl_resize_window(WindowPtr window,
                       int x, int y,
                       unsigned int width, unsigned int height,
                       WindowPtr sib);
void xwl_move_window(WindowPtr window,
                     int x, int y,
                     WindowPtr next_sib,
                     VTKind kind);
Bool xwl_destroy_window(WindowPtr window);
void xwl_window_post_damage(struct xwl_window *xwl_window);
void xwl_window_create_frame_callback(struct xwl_window *xwl_window);
void xwl_window_surface_do_destroy(struct xwl_wl_surface *xwl_wl_surface);

Bool xwl_window_init(void);

void xwl_dmabuf_feedback_destroy(struct xwl_dmabuf_feedback *xwl_feedback);
void xwl_dmabuf_feedback_clear_dev_formats(struct xwl_dmabuf_feedback *xwl_feedback);
void xwl_device_formats_destroy(struct xwl_device_formats *dev_formats);

#endif /* XWAYLAND_WINDOW_H */
