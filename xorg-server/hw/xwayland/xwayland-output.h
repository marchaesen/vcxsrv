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

#ifndef XWAYLAND_OUTPUT_H
#define XWAYLAND_OUTPUT_H

#include <xwayland-config.h>
#include <wayland-client.h>

#include <dix.h>
#include <input.h>
#include <randrstr.h>

#include "xwayland-types.h"

struct xwl_output {
    struct xorg_list link;
    struct wl_output *output;
    struct zxdg_output_v1 *xdg_output;
    uint32_t server_output_id;
    struct xwl_screen *xwl_screen;
    RROutputPtr randr_output;
    RRCrtcPtr randr_crtc;
    int32_t x, y, width, height, refresh;
    Rotation rotation;
    Bool wl_output_done;
    Bool xdg_output_done;
};

/* Per client per output emulated randr/vidmode resolution info. */
struct xwl_emulated_mode {
    uint32_t server_output_id;
    int32_t width;
    int32_t height;
    Bool from_vidmode;
};

Bool xwl_screen_init_output(struct xwl_screen *xwl_screen);

struct xwl_output *xwl_output_create(struct xwl_screen *xwl_screen,
                                     uint32_t id);

void xwl_output_destroy(struct xwl_output *xwl_output);

void xwl_output_remove(struct xwl_output *xwl_output);

struct xwl_emulated_mode *xwl_output_get_emulated_mode_for_client(
                            struct xwl_output *xwl_output, ClientPtr client);

RRModePtr xwl_output_find_mode(struct xwl_output *xwl_output,
                               int32_t width, int32_t height);
void xwl_output_set_emulated_mode(struct xwl_output *xwl_output,
                                  ClientPtr client, RRModePtr mode,
                                  Bool from_vidmode);
void xwl_output_set_window_randr_emu_props(struct xwl_screen *xwl_screen,
                                           WindowPtr window);

void xwl_screen_init_xdg_output(struct xwl_screen *xwl_screen);

#endif /* XWAYLAND_OUTPUT_H */
