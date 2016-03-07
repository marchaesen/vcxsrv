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

#define DEFAULT_DPI 96

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

    if (xwl_output->rotation & (RR_Rotate_0 | RR_Rotate_180)) {
        xwl_output->width = width;
        xwl_output->height = height;
    } else {
        xwl_output->width = height;
        xwl_output->height = width;
    }

    randr_mode = xwayland_cvt(width, height, refresh / 1000.0, 0, 0);

    RROutputSetModes(xwl_output->randr_output, &randr_mode, 1, 1);

    RRCrtcNotify(xwl_output->randr_crtc, randr_mode,
                 xwl_output->x, xwl_output->y,
                 xwl_output->rotation, NULL, 1, &xwl_output->randr_output);
}

static inline void
output_get_new_size(struct xwl_output *xwl_output,
		    int *height, int *width)
{
    if (*width < xwl_output->x + xwl_output->width)
        *width = xwl_output->x + xwl_output->width;

    if (*height < xwl_output->y + xwl_output->height)
        *height = xwl_output->y + xwl_output->height;
}

/* Approximate some kind of mmpd (m.m. per dot) of the screen given the outputs
 * associated with it.
 *
 * It will either calculate the mean mmpd of all the outputs, or default to
 * 96 DPI if no reasonable value could be calculated.
 */
static double
approximate_mmpd(struct xwl_screen *xwl_screen)
{
    struct xwl_output *it;
    int total_width_mm = 0;
    int total_width = 0;

    xorg_list_for_each_entry(it, &xwl_screen->output_list, link) {
        if (it->randr_output->mmWidth == 0)
            continue;

        total_width_mm += it->randr_output->mmWidth;
        total_width += it->width;
    }

    if (total_width_mm != 0)
        return (double)total_width_mm / total_width;
    else
        return 25.4 / DEFAULT_DPI;
}

static void
update_screen_size(struct xwl_output *xwl_output, int width, int height)
{
    struct xwl_screen *xwl_screen = xwl_output->xwl_screen;
    double mmpd;

    if (xwl_screen->root_clip_mode == ROOT_CLIP_FULL)
        SetRootClip(xwl_screen->screen, ROOT_CLIP_NONE);

    xwl_screen->width = width;
    xwl_screen->height = height;
    xwl_screen->screen->width = width;
    xwl_screen->screen->height = height;

    if (xwl_output->width == width && xwl_output->height == height) {
        xwl_screen->screen->mmWidth = xwl_output->randr_output->mmWidth;
        xwl_screen->screen->mmHeight = xwl_output->randr_output->mmHeight;
    } else {
        mmpd = approximate_mmpd(xwl_screen);
        xwl_screen->screen->mmWidth = width * mmpd;
        xwl_screen->screen->mmHeight = height * mmpd;
    }

    SetRootClip(xwl_screen->screen, xwl_screen->root_clip_mode);

    if (xwl_screen->screen->root) {
        xwl_screen->screen->root->drawable.width = width;
        xwl_screen->screen->root->drawable.height = height;
        RRScreenSizeNotify(xwl_screen->screen);
    }

    update_desktop_dimensions();
}

static void
output_handle_done(void *data, struct wl_output *wl_output)
{
    struct xwl_output *it, *xwl_output = data;
    struct xwl_screen *xwl_screen = xwl_output->xwl_screen;
    int width = 0, height = 0, has_this_output = 0;

    xorg_list_for_each_entry(it, &xwl_screen->output_list, link) {
        /* output done event is sent even when some property
         * of output is changed. That means that we may already
         * have this output. If it is true, we must not add it
         * into the output_list otherwise we'll corrupt it */
        if (it == xwl_output)
            has_this_output = 1;

        output_get_new_size(it, &height, &width);
    }

    if (!has_this_output) {
        xorg_list_append(&xwl_output->link, &xwl_screen->output_list);

        /* we did not check this output for new screen size, do it now */
        output_get_new_size(xwl_output, &height, &width);

	--xwl_screen->expecting_event;
    }

    update_screen_size(xwl_output, width, height);
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
    if (!xwl_output->output) {
        ErrorF("Failed binding wl_output\n");
        goto err;
    }

    xwl_output->server_output_id = id;
    wl_output_add_listener(xwl_output->output, &output_listener, xwl_output);

    snprintf(name, sizeof name, "XWAYLAND%d", serial++);

    xwl_output->xwl_screen = xwl_screen;
    xwl_output->randr_crtc = RRCrtcCreate(xwl_screen->screen, xwl_output);
    if (!xwl_output->randr_crtc) {
        ErrorF("Failed creating RandR CRTC\n");
        goto err;
    }

    xwl_output->randr_output = RROutputCreate(xwl_screen->screen, name,
                                              strlen(name), xwl_output);
    if (!xwl_output->randr_output) {
        ErrorF("Failed creating RandR Output\n");
        goto err;
    }

    RRCrtcGammaSetSize(xwl_output->randr_crtc, 256);
    RROutputSetCrtcs(xwl_output->randr_output, &xwl_output->randr_crtc, 1);
    RROutputSetConnection(xwl_output->randr_output, RR_Connected);

    return xwl_output;

err:
    if (xwl_output->randr_crtc)
        RRCrtcDestroy(xwl_output->randr_crtc);
    if (xwl_output->output)
        wl_output_destroy(xwl_output->output);
    free(xwl_output);
    return NULL;
}

void
xwl_output_destroy(struct xwl_output *xwl_output)
{
    struct xwl_output *it;
    struct xwl_screen *xwl_screen = xwl_output->xwl_screen;
    int width = 0, height = 0;

    wl_output_destroy(xwl_output->output);
    xorg_list_del(&xwl_output->link);
    RRCrtcDestroy(xwl_output->randr_crtc);
    RROutputDestroy(xwl_output->randr_output);

    xorg_list_for_each_entry(it, &xwl_screen->output_list, link)
        output_get_new_size(it, &height, &width);
    update_screen_size(xwl_output, width, height);

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
