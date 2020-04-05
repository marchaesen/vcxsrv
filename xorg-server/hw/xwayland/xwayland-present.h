/*
 * Copyright © 2018 Roman Gilg
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

#ifndef XWAYLAND_PRESENT_H
#define XWAYLAND_PRESENT_H

#include <xwayland-config.h>

#include <dix.h>

#include "xwayland-types.h"

#ifdef GLAMOR_HAS_GBM
struct xwl_present_window {
    struct xwl_screen *xwl_screen;
    struct xwl_present_event *sync_flip;
    WindowPtr window;
    struct xorg_list frame_callback_list;

    uint64_t msc;
    uint64_t ust;

    OsTimerPtr frame_timer;

    struct wl_callback *sync_callback;

    struct xorg_list event_list;
    struct xorg_list release_queue;
};

struct xwl_present_event {
    uint64_t event_id;
    uint64_t target_msc;

    Bool abort;
    Bool pending;
    Bool buffer_released;

    struct xwl_present_window *xwl_present_window;
    struct wl_buffer *buffer;

    struct xorg_list list;
};

void xwl_present_frame_callback(struct xwl_present_window *xwl_present_window);
Bool xwl_present_init(ScreenPtr screen);
void xwl_present_cleanup(WindowPtr window);
void xwl_present_unrealize_window(struct xwl_present_window *xwl_present_window);

#endif /* GLAMOR_HAS_GBM */

#endif /* XWAYLAND_PRESENT_H */
