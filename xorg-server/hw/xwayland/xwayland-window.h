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

#include <stdio.h>
#include <unistd.h>
#include <X11/X.h>
#include <dix.h>
#include <propertyst.h>
#include <validate.h>

#include "xwayland-types.h"

struct xwl_window {
    struct xwl_screen *xwl_screen;
    struct wl_surface *surface;
    struct wp_viewport *viewport;
    float scale_x, scale_y;
    struct xdg_surface *xdg_surface;
    WindowPtr window;
    struct xorg_list link_damage;
    struct xorg_list link_window;
    struct wl_callback *frame_callback;
    Bool allow_commits;
    struct xorg_list window_buffers_available;
    struct xorg_list window_buffers_unavailable;
    OsTimerPtr window_buffers_timer;
#ifdef GLAMOR_HAS_GBM
    struct xorg_list frame_callback_list;
    Bool present_flipped;
#endif
};

struct xwl_window *xwl_window_get(WindowPtr window);
struct xwl_window *xwl_window_from_window(WindowPtr window);

void xwl_window_update_property(struct xwl_window *xwl_window,
                                PropertyStateRec *propstate);
Bool xwl_window_has_viewport_enabled(struct xwl_window *xwl_window);
Bool xwl_window_is_toplevel(WindowPtr window);
void xwl_window_check_resolution_change_emulation(struct xwl_window *xwl_window);

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
Bool xwl_window_init(void);

#endif /* XWAYLAND_WINDOW_H */
