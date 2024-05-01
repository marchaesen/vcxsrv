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
#include "xwayland-drm-lease.h"

#define ALL_ROTATIONS (RR_Rotate_0   | \
                       RR_Rotate_90  | \
                       RR_Rotate_180 | \
                       RR_Rotate_270 | \
                       RR_Reflect_X  | \
                       RR_Reflect_Y)

#define MAX_OUTPUT_NAME 256

struct xwl_output {
    struct xorg_list link;
    struct xwl_screen *xwl_screen;
    RROutputPtr randr_output;
    RRCrtcPtr randr_crtc;
    RRTransformPtr transform;

    /* only for regular outputs */
    struct wl_output *output;
    struct zxdg_output_v1 *xdg_output;
    uint32_t server_output_id;
    int32_t x, y, width, height, refresh, scale;
    int32_t mode_width, mode_height;
    double xscale; /* Effective scale, can be fractional */
    Rotation rotation;
    Bool wl_output_done;
    Bool xdg_output_done;

    /* only for lease-able DRM connectors */
    struct wp_drm_lease_connector_v1 *lease_connector;
    struct xwl_drm_lease *lease;
    struct xwl_drm_lease_device *lease_device;
    Bool withdrawn_connector;
};

/* Per client per output emulated randr/vidmode resolution info. */
struct xwl_emulated_mode {
    uint32_t server_output_id;
    int32_t width;
    int32_t height;
    RRMode id;
    Bool from_vidmode;
};

Bool xwl_screen_init_output(struct xwl_screen *xwl_screen);

void xwl_output_set_name(struct xwl_output *xwl_output, const char *name);

Bool xwl_screen_init_randr_fixed(struct xwl_screen *xwl_screen);

void
xwl_output_set_xscale(struct xwl_output *xwl_output, double xscale);

Bool
xwl_randr_add_modes_fixed(struct xwl_output *xwl_output,
                          int current_width, int current_height);

void xwl_output_set_mode_fixed(struct xwl_output *xwl_output,
                               RRModePtr mode);

struct xwl_output *xwl_output_from_wl_output(struct xwl_screen *xwl_screen,
                                             struct wl_output* wl_output);
struct xwl_output *xwl_output_get_output_from_name(struct xwl_screen *xwl_screen,
                                                   const char *name);

struct xwl_output *xwl_output_create(struct xwl_screen *xwl_screen,
                                     uint32_t id, Bool connected,
                                     uint32_t version);

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
