/*
 * Copyright © 2014 Intel Corporation
 * Copyright © 2008 Kristian Høgsberg
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

#ifndef XWAYLAND_INPUT_H
#define XWAYLAND_INPUT_H

#include <xwayland-config.h>
#include <wayland-client.h>

#include <dix.h>
#include <input.h>

struct xwl_touch {
    struct xwl_window *window;
    int32_t id;
    int x, y;
    struct xorg_list link_touch;
};

struct xwl_pointer_warp_emulator {
    struct xwl_seat *xwl_seat;
    struct xwl_window *locked_window;
    struct zwp_locked_pointer_v1 *locked_pointer;
};

struct xwl_cursor {
    void (* update_proc) (struct xwl_cursor *);
    struct wl_surface *surface;
    struct wl_callback *frame_cb;
    Bool needs_update;
};

struct xwl_seat {
    DeviceIntPtr pointer;
    DeviceIntPtr relative_pointer;
    DeviceIntPtr keyboard;
    DeviceIntPtr touch;
    DeviceIntPtr stylus;
    DeviceIntPtr eraser;
    DeviceIntPtr puck;
    struct xwl_screen *xwl_screen;
    struct wl_seat *seat;
    struct wl_pointer *wl_pointer;
    struct zwp_relative_pointer_v1 *wp_relative_pointer;
    struct wl_keyboard *wl_keyboard;
    struct wl_touch *wl_touch;
    struct zwp_tablet_seat_v2 *tablet_seat;
    struct wl_array keys;
    struct xwl_window *focus_window;
    struct xwl_window *tablet_focus_window;
    uint32_t id;
    uint32_t pointer_enter_serial;
    struct xorg_list link;
    CursorPtr x_cursor;
    struct xwl_cursor cursor;
    WindowPtr last_xwindow;

    struct xorg_list touches;

    size_t keymap_size;
    char *keymap;
    struct wl_surface *keyboard_focus;

    struct xorg_list axis_discrete_pending;
    struct xorg_list sync_pending;

    struct xwl_pointer_warp_emulator *pointer_warp_emulator;

    struct xwl_window *cursor_confinement_window;
    struct zwp_confined_pointer_v1 *confined_pointer;

    struct {
        Bool has_absolute;
        wl_fixed_t x;
        wl_fixed_t y;

        Bool has_relative;
        double dx;
        double dy;
        double dx_unaccel;
        double dy_unaccel;
    } pending_pointer_event;

    struct xorg_list tablets;
    struct xorg_list tablet_tools;
    struct xorg_list tablet_pads;
    struct zwp_xwayland_keyboard_grab_v1 *keyboard_grab;
};

struct xwl_tablet {
    struct xorg_list link;
    struct zwp_tablet_v2 *tablet;
    struct xwl_seat *seat;
};

struct xwl_tablet_tool {
    struct xorg_list link;
    struct zwp_tablet_tool_v2 *tool;
    struct xwl_seat *seat;

    DeviceIntPtr xdevice;
    uint32_t proximity_in_serial;
    double x;
    double y;
    uint32_t pressure;
    double tilt_x;
    double tilt_y;
    double rotation;
    double slider;

    uint32_t buttons_now,
             buttons_prev;

    int32_t wheel_clicks;

    struct xwl_cursor cursor;
};

struct xwl_tablet_pad_ring {
    unsigned int index;
    struct xorg_list link;
    struct xwl_tablet_pad_group *group;
    struct zwp_tablet_pad_ring_v2 *ring;
};

struct xwl_tablet_pad_strip {
    unsigned int index;
    struct xorg_list link;
    struct xwl_tablet_pad_group *group;
    struct zwp_tablet_pad_strip_v2 *strip;
};

struct xwl_tablet_pad_group {
    struct xorg_list link;
    struct xwl_tablet_pad *pad;
    struct zwp_tablet_pad_group_v2 *group;

    struct xorg_list pad_group_ring_list;
    struct xorg_list pad_group_strip_list;
};

struct xwl_tablet_pad {
    struct xorg_list link;
    struct zwp_tablet_pad_v2 *pad;
    struct xwl_seat *seat;

    DeviceIntPtr xdevice;

    unsigned int nbuttons;
    struct xorg_list pad_group_list;
};

void xwl_seat_destroy(struct xwl_seat *xwl_seat);

void xwl_seat_clear_touch(struct xwl_seat *xwl_seat, WindowPtr window);

void xwl_seat_emulate_pointer_warp(struct xwl_seat *xwl_seat,
                                   struct xwl_window *xwl_window,
                                   SpritePtr sprite,
                                   int x, int y);

void xwl_seat_destroy_pointer_warp_emulator(struct xwl_seat *xwl_seat);

void xwl_seat_cursor_visibility_changed(struct xwl_seat *xwl_seat);

void xwl_seat_confine_pointer(struct xwl_seat *xwl_seat,
                              struct xwl_window *xwl_window);
void xwl_seat_unconfine_pointer(struct xwl_seat *xwl_seat);

void xwl_screen_release_tablet_manager(struct xwl_screen *xwl_screen);

#endif /* XWAYLAND_INPUT_H */
