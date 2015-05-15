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

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "xwayland.h"
#include <randrstr.h>

static Rotation
wl_transform_to_xrandr(enum wl_output_transform transform)
{
    switch (transform) {
    default:
    case WL_OUTPUT_TRANSFORM_NORMAL:
        return RR_Rotate_0;
    case WL_OUTPUT_TRANSFORM_90:
        return RR_Rotate_90;
    case WL_OUTPUT_TRANSFORM_180:
        return RR_Rotate_180;
    case WL_OUTPUT_TRANSFORM_270:
        return RR_Rotate_270;
    case WL_OUTPUT_TRANSFORM_FLIPPED:
        return RR_Reflect_X | RR_Rotate_0;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
        return RR_Reflect_X | RR_Rotate_90;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
        return RR_Reflect_X | RR_Rotate_180;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        return RR_Reflect_X | RR_Rotate_270;
    }
}

static int
wl_subpixel_to_xrandr(int subpixel)
{
    switch (subpixel) {
    default:
    case WL_OUTPUT_SUBPIXEL_UNKNOWN:
        return SubPixelUnknown;
    case WL_OUTPUT_SUBPIXEL_NONE:
        return SubPixelNone;
    case WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB:
        return SubPixelHorizontalRGB;
    case WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR:
        return SubPixelHorizontalBGR;
    case WL_OUTPUT_SUBPIXEL_VERTICAL_RGB:
        return SubPixelVerticalRGB;
    case WL_OUTPUT_SUBPIXEL_VERTICAL_BGR:
        return SubPixelVerticalBGR;
    }
}

static void
output_handle_geometry(void *data, struct wl_output *wl_output, int x, int y,
                       int physical_width, int physical_height, int subpixel,
                       const char *make, const char *model, int transform)
{
    struct xwl_output *xwl_output = data;

    RROutputSetPhysicalSize(xwl_output->randr_output,
                            physical_width, physical_height);
    RROutputSetSubpixelOrder(xwl_output->randr_output,
                             wl_subpixel_to_xrandr(subpixel));
    xwl_output->x = x;
    xwl_output->y = y;

    xwl_output->rotation = wl_transform_to_xrandr(transform);
}

static void
output_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags,
                   int width, int height, int refresh)
{
    struct xwl_output *xwl_output = data;
    RRModePtr randr_mode;

    if (!(flags & WL_OUTPUT_MODE_CURRENT))
        return;

    xwl_output->width = width;
    xwl_output->height = height;

    randr_mode = xwayland_cvt(width, height, refresh / 1000.0, 0, 0);

    RROutputSetModes(xwl_output->randr_output, &randr_mode, 1, 1);

    RRCrtcNotify(xwl_output->randr_crtc, randr_mode,
                 xwl_output->x, xwl_output->y,
                 xwl_output->rotation, NULL, 1, &xwl_output->randr_output);
}

static void
output_handle_done(void *data, struct wl_output *wl_output)
{
    struct xwl_output *xwl_output = data;
    struct xwl_screen *xwl_screen = xwl_output->xwl_screen;
    int width, height;

    xorg_list_append(&xwl_output->link, &xwl_screen->output_list);

    width = 0;
    height = 0;
    xorg_list_for_each_entry(xwl_output, &xwl_screen->output_list, link) {
        if (width < xwl_output->x + xwl_output->width)
            width = xwl_output->x + xwl_output->width;
        if (height < xwl_output->y + xwl_output->height)
            height = xwl_output->y + xwl_output->height;
    }

    xwl_screen->width = width;
    xwl_screen->height = height;
    RRScreenSizeNotify(xwl_screen->screen);

    xwl_screen->expecting_event--;
}

static void
output_handle_scale(void *data, struct wl_output *wl_output, int32_t factor)
{
}

static const struct wl_output_listener output_listener = {
    output_handle_geometry,
    output_handle_mode,
    output_handle_done,
    output_handle_scale
};

struct xwl_output *
xwl_output_create(struct xwl_screen *xwl_screen, uint32_t id)
{
    struct xwl_output *xwl_output;
    static int serial;
    char name[256];

    xwl_output = calloc(sizeof *xwl_output, 1);
    if (xwl_output == NULL) {
        ErrorF("create_output ENOMEM\n");
        return NULL;
    }

    xwl_output->output = wl_registry_bind(xwl_screen->registry, id,
                                          &wl_output_interface, 2);
    wl_output_add_listener(xwl_output->output, &output_listener, xwl_output);

    if (snprintf(name, sizeof name, "XWAYLAND%d", serial++) < 0) {
        ErrorF("create_output ENOMEM\n");
        free(xwl_output);
        return NULL;
    }

    xwl_output->xwl_screen = xwl_screen;
    xwl_output->randr_crtc = RRCrtcCreate(xwl_screen->screen, xwl_output);
    xwl_output->randr_output = RROutputCreate(xwl_screen->screen, name,
                                              strlen(name), xwl_output);
    RRCrtcGammaSetSize(xwl_output->randr_crtc, 256);
    RROutputSetCrtcs(xwl_output->randr_output, &xwl_output->randr_crtc, 1);
    RROutputSetConnection(xwl_output->randr_output, RR_Connected);

    return xwl_output;
}

void
xwl_output_destroy(struct xwl_output *xwl_output)
{
    wl_output_destroy(xwl_output->output);
    RRCrtcDestroy(xwl_output->randr_crtc);
    RROutputDestroy(xwl_output->randr_output);
    free(xwl_output);
}

static Bool
xwl_randr_get_info(ScreenPtr pScreen, Rotation * rotations)
{
    *rotations = 0;

    return TRUE;
}

static Bool
xwl_randr_set_config(ScreenPtr pScreen,
                     Rotation rotation, int rate, RRScreenSizePtr pSize)
{
    return FALSE;
}

Bool
xwl_screen_init_output(struct xwl_screen *xwl_screen)
{
    rrScrPrivPtr rp;

    if (!RRScreenInit(xwl_screen->screen))
        return FALSE;

    RRScreenSetSizeRange(xwl_screen->screen, 320, 200, 8192, 8192);

    rp = rrGetScrPriv(xwl_screen->screen);
    rp->rrGetInfo = xwl_randr_get_info;
    rp->rrSetConfig = xwl_randr_set_config;

    return TRUE;
}
