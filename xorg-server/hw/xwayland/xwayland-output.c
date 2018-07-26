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
#define ALL_ROTATIONS (RR_Rotate_0   | \
                       RR_Rotate_90  | \
                       RR_Rotate_180 | \
                       RR_Rotate_270 | \
                       RR_Reflect_X  | \
                       RR_Reflect_Y)

static void xwl_output_get_xdg_output(struct xwl_output *xwl_output);

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

    /* Apply the change from wl_output only if xdg-output is not supported */
    if (!xwl_output->xdg_output) {
        xwl_output->x = x;
        xwl_output->y = y;
    }
    xwl_output->rotation = wl_transform_to_xrandr(transform);
}

static void
output_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags,
                   int width, int height, int refresh)
{
    struct xwl_output *xwl_output = data;

    if (!(flags & WL_OUTPUT_MODE_CURRENT))
        return;

    /* Apply the change from wl_output only if xdg-output is not supported */
    if (!xwl_output->xdg_output) {
        xwl_output->width = width;
        xwl_output->height = height;
    }
    xwl_output->refresh = refresh;
}

static inline void
output_get_new_size(struct xwl_output *xwl_output,
                    Bool need_rotate,
                    int *height, int *width)
{
    int output_width, output_height;

    if (!need_rotate || (xwl_output->rotation & (RR_Rotate_0 | RR_Rotate_180))) {
        output_width = xwl_output->width;
        output_height = xwl_output->height;
    } else {
        output_width = xwl_output->height;
        output_height = xwl_output->width;
    }

    if (*width < xwl_output->x + output_width)
        *width = xwl_output->x + output_width;

    if (*height < xwl_output->y + output_height)
        *height = xwl_output->y + output_height;
}

/* Approximate some kind of mmpd (m.m. per dot) of the screen given the outputs
 * associated with it.
 *
 * It either calculates the mean mmpd of all the outputs or, if no reasonable
 * value could be calculated, defaults to the mmpd of a screen with a DPI value
 * of DEFAULT_DPI.
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
        BoxRec box = { 0, 0, width, height };

        xwl_screen->screen->root->drawable.width = width;
        xwl_screen->screen->root->drawable.height = height;
        RegionReset(&xwl_screen->screen->root->winSize, &box);
        RRScreenSizeNotify(xwl_screen->screen);
    }

    update_desktop_dimensions();
}

static void
apply_output_change(struct xwl_output *xwl_output)
{
    struct xwl_screen *xwl_screen = xwl_output->xwl_screen;
    struct xwl_output *it;
    int mode_width, mode_height;
    int width = 0, height = 0, has_this_output = 0;
    RRModePtr randr_mode;
    Bool need_rotate;

    /* Clear out the "done" received flags */
    xwl_output->wl_output_done = FALSE;
    xwl_output->xdg_output_done = FALSE;

    /* xdg-output sends output size in compositor space. so already rotated */
    need_rotate = (xwl_output->xdg_output == NULL);

    /* We need to rotate back the logical size for the mode */
    if (need_rotate || xwl_output->rotation & (RR_Rotate_0 | RR_Rotate_180)) {
        mode_width = xwl_output->width;
        mode_height = xwl_output->height;
    } else {
        mode_width = xwl_output->height;
        mode_height = xwl_output->width;
    }

    randr_mode = xwayland_cvt(mode_width, mode_height,
                              xwl_output->refresh / 1000.0, 0, 0);
    RROutputSetModes(xwl_output->randr_output, &randr_mode, 1, 1);
    RRCrtcNotify(xwl_output->randr_crtc, randr_mode,
                 xwl_output->x, xwl_output->y,
                 xwl_output->rotation, NULL, 1, &xwl_output->randr_output);

    xorg_list_for_each_entry(it, &xwl_screen->output_list, link) {
        /* output done event is sent even when some property
         * of output is changed. That means that we may already
         * have this output. If it is true, we must not add it
         * into the output_list otherwise we'll corrupt it */
        if (it == xwl_output)
            has_this_output = 1;

        output_get_new_size(it, need_rotate, &height, &width);
    }

    if (!has_this_output) {
        xorg_list_append(&xwl_output->link, &xwl_screen->output_list);

        /* we did not check this output for new screen size, do it now */
        output_get_new_size(xwl_output, need_rotate, &height, &width);

	--xwl_screen->expecting_event;
    }

    update_screen_size(xwl_output, width, height);
}

static void
output_handle_done(void *data, struct wl_output *wl_output)
{
    struct xwl_output *xwl_output = data;

    xwl_output->wl_output_done = TRUE;
    /* Apply the changes from wl_output only if both "done" events are received,
     * or if xdg-output is not supported.
     */
    if (xwl_output->xdg_output_done || !xwl_output->xdg_output)
        apply_output_change(xwl_output);
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

static void
xdg_output_handle_logical_position(void *data, struct zxdg_output_v1 *xdg_output,
                                   int32_t x, int32_t y)
{
    struct xwl_output *xwl_output = data;

    xwl_output->x = x;
    xwl_output->y = y;
}

static void
xdg_output_handle_logical_size(void *data, struct zxdg_output_v1 *xdg_output,
                               int32_t width, int32_t height)
{
    struct xwl_output *xwl_output = data;

    xwl_output->width = width;
    xwl_output->height = height;
}

static void
xdg_output_handle_done(void *data, struct zxdg_output_v1 *xdg_output)
{
    struct xwl_output *xwl_output = data;

    xwl_output->xdg_output_done = TRUE;
    if (xwl_output->wl_output_done)
        apply_output_change(xwl_output);
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    xdg_output_handle_logical_position,
    xdg_output_handle_logical_size,
    xdg_output_handle_done,
};

struct xwl_output *
xwl_output_create(struct xwl_screen *xwl_screen, uint32_t id)
{
    struct xwl_output *xwl_output;
    static int serial;
    char name[256];

    xwl_output = calloc(1, sizeof *xwl_output);
    if (xwl_output == NULL) {
        ErrorF("%s ENOMEM\n", __func__);
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
    RRCrtcSetRotations (xwl_output->randr_crtc, ALL_ROTATIONS);

    xwl_output->randr_output = RROutputCreate(xwl_screen->screen, name,
                                              strlen(name), xwl_output);
    if (!xwl_output->randr_output) {
        ErrorF("Failed creating RandR Output\n");
        goto err;
    }

    RRCrtcGammaSetSize(xwl_output->randr_crtc, 256);
    RROutputSetCrtcs(xwl_output->randr_output, &xwl_output->randr_crtc, 1);
    RROutputSetConnection(xwl_output->randr_output, RR_Connected);

    /* We want the output to be in the list as soon as created so we can
     * use it when binding to the xdg-output protocol...
     */
    xorg_list_append(&xwl_output->link, &xwl_screen->output_list);
    --xwl_screen->expecting_event;

    if (xwl_screen->xdg_output_manager)
        xwl_output_get_xdg_output(xwl_output);

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
    wl_output_destroy(xwl_output->output);
    free(xwl_output);
}

void
xwl_output_remove(struct xwl_output *xwl_output)
{
    struct xwl_output *it;
    struct xwl_screen *xwl_screen = xwl_output->xwl_screen;
    int width = 0, height = 0;
    Bool need_rotate = (xwl_output->xdg_output == NULL);

    RRCrtcDestroy(xwl_output->randr_crtc);
    RROutputDestroy(xwl_output->randr_output);
    xorg_list_del(&xwl_output->link);

    xorg_list_for_each_entry(it, &xwl_screen->output_list, link)
        output_get_new_size(it, need_rotate, &height, &width);
    update_screen_size(xwl_output, width, height);

    xwl_output_destroy(xwl_output);
}

static Bool
xwl_randr_get_info(ScreenPtr pScreen, Rotation * rotations)
{
    *rotations = ALL_ROTATIONS;

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

static void
xwl_output_get_xdg_output(struct xwl_output *xwl_output)
{
    struct xwl_screen *xwl_screen = xwl_output->xwl_screen;

    xwl_output->xdg_output =
        zxdg_output_manager_v1_get_xdg_output (xwl_screen->xdg_output_manager,
                                               xwl_output->output);

    zxdg_output_v1_add_listener(xwl_output->xdg_output,
                                &xdg_output_listener,
                                xwl_output);
}

void
xwl_screen_init_xdg_output(struct xwl_screen *xwl_screen)
{
    struct xwl_output *it;

    assert(xwl_screen->xdg_output_manager);

    xorg_list_for_each_entry(it, &xwl_screen->output_list, link)
        xwl_output_get_xdg_output(it);
}
