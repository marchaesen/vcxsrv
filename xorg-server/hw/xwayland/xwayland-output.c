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

#include <xwayland-config.h>

#include <randrstr.h>
#include <X11/Xatom.h>

#include "xwayland-cvt.h"
#include "xwayland-output.h"
#include "xwayland-screen.h"
#include "xwayland-window.h"

#include "xdg-output-unstable-v1-client-protocol.h"

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

static int
xwl_set_pixmap_visit_window(WindowPtr window, void *data)
{
    ScreenPtr screen = window->drawable.pScreen;

    if (screen->GetWindowPixmap(window) == data) {
        screen->SetWindowPixmap(window, screen->GetScreenPixmap(screen));
        return WT_WALKCHILDREN;
    }

    return WT_DONTWALKCHILDREN;
}

static void
update_backing_pixmaps(struct xwl_screen *xwl_screen, int width, int height)
{
    ScreenPtr pScreen = xwl_screen->screen;
    WindowPtr pRoot = pScreen->root;
    PixmapPtr old_pixmap, new_pixmap;

    old_pixmap = pScreen->GetScreenPixmap(pScreen);
    new_pixmap = pScreen->CreatePixmap(pScreen, width, height,
                                       pScreen->rootDepth,
                                       CREATE_PIXMAP_USAGE_BACKING_PIXMAP);
    pScreen->SetScreenPixmap(new_pixmap);

    if (old_pixmap) {
        TraverseTree(pRoot, xwl_set_pixmap_visit_window, old_pixmap);
        pScreen->DestroyPixmap(old_pixmap);
    }

    pScreen->ResizeWindow(pRoot, 0, 0, width, height, NULL);
}

static void
update_screen_size(struct xwl_output *xwl_output, int width, int height)
{
    struct xwl_screen *xwl_screen = xwl_output->xwl_screen;
    double mmpd;

    if (xwl_screen->root_clip_mode == ROOT_CLIP_FULL)
        SetRootClip(xwl_screen->screen, ROOT_CLIP_NONE);

    if (!xwl_screen->rootless && xwl_screen->screen->root)
        update_backing_pixmaps (xwl_screen, width, height);

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

struct xwl_emulated_mode *
xwl_output_get_emulated_mode_for_client(struct xwl_output *xwl_output,
                                        ClientPtr client)
{
    struct xwl_client *xwl_client = xwl_client_get(client);
    int i;

    if (!xwl_output)
        return NULL;

    for (i = 0; i < XWL_CLIENT_MAX_EMULATED_MODES; i++) {
        if (xwl_client->emulated_modes[i].server_output_id ==
            xwl_output->server_output_id)
            return &xwl_client->emulated_modes[i];
    }

    return NULL;
}

static void
xwl_output_add_emulated_mode_for_client(struct xwl_output *xwl_output,
                                        ClientPtr client,
                                        RRModePtr mode,
                                        Bool from_vidmode)
{
    struct xwl_client *xwl_client = xwl_client_get(client);
    struct xwl_emulated_mode *emulated_mode;
    int i;

    emulated_mode = xwl_output_get_emulated_mode_for_client(xwl_output, client);
    if (!emulated_mode) {
        /* Find a free spot in the emulated modes array */
        for (i = 0; i < XWL_CLIENT_MAX_EMULATED_MODES; i++) {
            if (xwl_client->emulated_modes[i].server_output_id == 0) {
                emulated_mode = &xwl_client->emulated_modes[i];
                break;
            }
        }
    }
    if (!emulated_mode) {
        static Bool warned;

        if (!warned) {
            ErrorF("Ran out of space for emulated-modes, not adding mode");
            warned = TRUE;
        }

        return;
    }

    emulated_mode->server_output_id = xwl_output->server_output_id;
    emulated_mode->width  = mode->mode.width;
    emulated_mode->height = mode->mode.height;
    emulated_mode->from_vidmode = from_vidmode;
}

static void
xwl_output_remove_emulated_mode_for_client(struct xwl_output *xwl_output,
                                           ClientPtr client)
{
    struct xwl_emulated_mode *emulated_mode;

    emulated_mode = xwl_output_get_emulated_mode_for_client(xwl_output, client);
    if (emulated_mode) {
        DebugF("XWAYLAND: xwl_output_remove_emulated_mode: %dx%d\n",
               emulated_mode->width, emulated_mode->height);
        memset(emulated_mode, 0, sizeof(*emulated_mode));
    }
}

/* From hw/xfree86/common/xf86DefModeSet.c with some obscure modes dropped */
const int32_t xwl_output_fake_modes[][2] = {
    /* 4:3 (1.33) */
    { 2048, 1536 },
    { 1920, 1440 },
    { 1600, 1200 },
    { 1440, 1080 },
    { 1400, 1050 },
    { 1280, 1024 }, /* 5:4 (1.25) */
    { 1280,  960 },
    { 1152,  864 },
    { 1024,  768 },
    {  800,  600 },
    {  640,  480 },
    {  320,  240 },
    /* 16:10 (1.6) */
    { 2560, 1600 },
    { 1920, 1200 },
    { 1680, 1050 },
    { 1440,  900 },
    { 1280,  800 },
    {  720,  480 }, /* 3:2 (1.5) */
    {  640,  400 },
    {  320,  200 },
    /* 16:9 (1.77) */
    { 5120, 2880 },
    { 4096, 2304 },
    { 3840, 2160 },
    { 3200, 1800 },
    { 2880, 1620 },
    { 2560, 1440 },
    { 2048, 1152 },
    { 1920, 1080 },
    { 1600,  900 },
    { 1368,  768 },
    { 1280,  720 },
    { 1024,  576 },
    {  864,  486 },
    {  720,  400 },
    {  640,  350 },
};

/* Build an array with RRModes the first mode is the actual output mode, the
 * rest are fake modes from the xwl_output_fake_modes list. We do this for apps
 * which want to change resolution when they go fullscreen.
 * When an app requests a mode-change, we fake it using WPviewport.
 */
static RRModePtr *
output_get_rr_modes(struct xwl_output *xwl_output,
                    int32_t width, int32_t height,
                    int *count)
{
    struct xwl_screen *xwl_screen = xwl_output->xwl_screen;
    RRModePtr *rr_modes;
    int i;

    rr_modes = xallocarray(ARRAY_SIZE(xwl_output_fake_modes) + 1, sizeof(RRModePtr));
    if (!rr_modes)
        goto err;

    /* Add actual output mode */
    rr_modes[0] = xwayland_cvt(width, height, xwl_output->refresh / 1000.0, 0, 0);
    if (!rr_modes[0])
        goto err;

    *count = 1;

    if (!xwl_screen_has_resolution_change_emulation(xwl_screen))
        return rr_modes;

    /* Add fake modes */
    for (i = 0; i < ARRAY_SIZE(xwl_output_fake_modes); i++) {
        /* Skip actual output mode, already added */
        if (xwl_output_fake_modes[i][0] == width &&
            xwl_output_fake_modes[i][1] == height)
            continue;

        /* Skip modes which are too big, avoid downscaling */
        if (xwl_output_fake_modes[i][0] > width ||
            xwl_output_fake_modes[i][1] > height)
            continue;

        rr_modes[*count] = xwayland_cvt(xwl_output_fake_modes[i][0],
                                        xwl_output_fake_modes[i][1],
                                        xwl_output->refresh / 1000.0, 0, 0);
        if (!rr_modes[*count])
            goto err;

        (*count)++;
    }

    return rr_modes;
err:
    FatalError("Failed to allocate memory for list of RR modes");
}

RRModePtr
xwl_output_find_mode(struct xwl_output *xwl_output,
                     int32_t width, int32_t height)
{
    RROutputPtr output = xwl_output->randr_output;
    int i;

    /* width & height -1 means we want the actual output mode, which is idx 0 */
    if (width == -1 && height == -1 && output->modes)
        return output->modes[0];

    for (i = 0; i < output->numModes; i++) {
        if (output->modes[i]->mode.width == width && output->modes[i]->mode.height == height)
            return output->modes[i];
    }

    ErrorF("XWAYLAND: mode %dx%d is not available\n", width, height);
    return NULL;
}

struct xwl_output_randr_emu_prop {
    Atom atom;
    uint32_t rects[XWL_CLIENT_MAX_EMULATED_MODES][4];
    int rect_count;
};

static void
xwl_output_randr_emu_prop(struct xwl_screen *xwl_screen, ClientPtr client,
                          struct xwl_output_randr_emu_prop *prop)
{
    static const char atom_name[] = "_XWAYLAND_RANDR_EMU_MONITOR_RECTS";
    struct xwl_emulated_mode *emulated_mode;
    struct xwl_output *xwl_output;
    int index = 0;

    prop->atom = MakeAtom(atom_name, strlen(atom_name), TRUE);

    xorg_list_for_each_entry(xwl_output, &xwl_screen->output_list, link) {
        emulated_mode = xwl_output_get_emulated_mode_for_client(xwl_output, client);
        if (!emulated_mode)
            continue;

        prop->rects[index][0] = xwl_output->x;
        prop->rects[index][1] = xwl_output->y;
        prop->rects[index][2] = emulated_mode->width;
        prop->rects[index][3] = emulated_mode->height;
        index++;
    }

    prop->rect_count = index;
}

static void
xwl_output_set_randr_emu_prop(WindowPtr window,
                              struct xwl_output_randr_emu_prop *prop)
{
    if (prop->rect_count) {
        dixChangeWindowProperty(serverClient, window, prop->atom,
                                XA_CARDINAL, 32, PropModeReplace,
                                prop->rect_count * 4, prop->rects, TRUE);
    } else {
        DeleteProperty(serverClient, window, prop->atom);
    }
}

static void
xwl_output_set_randr_emu_prop_callback(void *resource, XID id, void *user_data)
{
    if (xwl_window_is_toplevel(resource))
        xwl_output_set_randr_emu_prop(resource, user_data);
}

static void
xwl_output_set_randr_emu_props(struct xwl_screen *xwl_screen, ClientPtr client)
{
    struct xwl_output_randr_emu_prop prop = {};

    xwl_output_randr_emu_prop(xwl_screen, client, &prop);
    FindClientResourcesByType(client, RT_WINDOW,
                              xwl_output_set_randr_emu_prop_callback, &prop);
}

void
xwl_output_set_window_randr_emu_props(struct xwl_screen *xwl_screen,
                                      WindowPtr window)
{
    struct xwl_output_randr_emu_prop prop = {};

    xwl_output_randr_emu_prop(xwl_screen, wClient(window), &prop);
    xwl_output_set_randr_emu_prop(window, &prop);
}

void
xwl_output_set_emulated_mode(struct xwl_output *xwl_output, ClientPtr client,
                             RRModePtr mode, Bool from_vidmode)
{
    DebugF("XWAYLAND: xwl_output_set_emulated_mode from %s: %dx%d\n",
           from_vidmode ? "vidmode" : "randr",
           mode->mode.width, mode->mode.height);

    /* modes[0] is the actual (not-emulated) output mode */
    if (mode == xwl_output->randr_output->modes[0])
        xwl_output_remove_emulated_mode_for_client(xwl_output, client);
    else
        xwl_output_add_emulated_mode_for_client(xwl_output, client, mode, from_vidmode);

    xwl_screen_check_resolution_change_emulation(xwl_output->xwl_screen);

    xwl_output_set_randr_emu_props(xwl_output->xwl_screen, client);
}

static void
apply_output_change(struct xwl_output *xwl_output)
{
    struct xwl_screen *xwl_screen = xwl_output->xwl_screen;
    struct xwl_output *it;
    int mode_width, mode_height, count;
    int width = 0, height = 0, has_this_output = 0;
    RRModePtr *randr_modes;
    Bool need_rotate;

    /* Clear out the "done" received flags */
    xwl_output->wl_output_done = FALSE;
    xwl_output->xdg_output_done = FALSE;

    /* xdg-output sends output size in compositor space. so already rotated */
    need_rotate = (xwl_output->xdg_output == NULL);

    /* We need to rotate back the logical size for the mode */
    if (!need_rotate || xwl_output->rotation & (RR_Rotate_0 | RR_Rotate_180)) {
        mode_width = xwl_output->width;
        mode_height = xwl_output->height;
    } else {
        mode_width = xwl_output->height;
        mode_height = xwl_output->width;
    }

    /* Build a fresh modes array using the current refresh rate */
    randr_modes = output_get_rr_modes(xwl_output, mode_width, mode_height, &count);
    RROutputSetModes(xwl_output->randr_output, randr_modes, count, 1);
    RRCrtcNotify(xwl_output->randr_crtc, randr_modes[0],
                 xwl_output->x, xwl_output->y,
                 xwl_output->rotation, NULL, 1, &xwl_output->randr_output);
    /* RROutputSetModes takes ownership of the passed in modes, so we only
     * have to free the pointer array.
     */
    free(randr_modes);

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
     * if xdg-output is not supported or if xdg-output version is high enough.
     */
    if (xwl_output->xdg_output_done || !xwl_output->xdg_output ||
        zxdg_output_v1_get_version(xwl_output->xdg_output) >= 3)
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
    if (xwl_output->wl_output_done &&
        zxdg_output_v1_get_version(xdg_output) < 3)
        apply_output_change(xwl_output);
}

static void
xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output,
                       const char *name)
{
}

static void
xdg_output_handle_description(void *data, struct zxdg_output_v1 *xdg_output,
                              const char *description)
{
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    xdg_output_handle_logical_position,
    xdg_output_handle_logical_size,
    xdg_output_handle_done,
    xdg_output_handle_name,
    xdg_output_handle_description,
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

    xorg_list_del(&xwl_output->link);

    xorg_list_for_each_entry(it, &xwl_screen->output_list, link)
        output_get_new_size(it, need_rotate, &height, &width);
    update_screen_size(xwl_output, width, height);

    RRCrtcDestroy(xwl_output->randr_crtc);
    RROutputDestroy(xwl_output->randr_output);

    xwl_output_destroy(xwl_output);
}

static Bool
xwl_randr_get_info(ScreenPtr pScreen, Rotation * rotations)
{
    *rotations = ALL_ROTATIONS;

    return TRUE;
}

#ifdef RANDR_10_INTERFACE
static Bool
xwl_randr_set_config(ScreenPtr pScreen,
                     Rotation rotation, int rate, RRScreenSizePtr pSize)
{
    return FALSE;
}
#endif

#if RANDR_12_INTERFACE
static Bool
xwl_randr_screen_set_size(ScreenPtr pScreen,
                          CARD16 width,
                          CARD16 height,
                          CARD32 mmWidth, CARD32 mmHeight)
{
    return TRUE;
}

static Bool
xwl_randr_crtc_set(ScreenPtr pScreen,
                   RRCrtcPtr crtc,
                   RRModePtr new_mode,
                   int x,
                   int y,
                   Rotation rotation,
                   int numOutputs, RROutputPtr * outputs)
{
    struct xwl_output *xwl_output = crtc->devPrivate;
    RRModePtr mode;

    if (new_mode) {
        mode = xwl_output_find_mode(xwl_output,
                                    new_mode->mode.width,
                                    new_mode->mode.height);
    } else {
        mode = xwl_output_find_mode(xwl_output, -1, -1);
    }
    if (!mode)
        return FALSE;

    xwl_output_set_emulated_mode(xwl_output, GetCurrentClient(), mode, FALSE);

    /* A real randr implementation would call:
     * RRCrtcNotify(xwl_output->randr_crtc, mode, xwl_output->x, xwl_output->y,
     *              xwl_output->rotation, NULL, 1, &xwl_output->randr_output);
     * here to update the mode reported to clients querying the randr settings
     * but that influences *all* clients and we do randr mode change emulation
     * on a per client basis. So we just return success here.
     */

    return TRUE;
}

static Bool
xwl_randr_crtc_set_gamma(ScreenPtr pScreen, RRCrtcPtr crtc)
{
    return TRUE;
}

static Bool
xwl_randr_crtc_get_gamma(ScreenPtr pScreen, RRCrtcPtr crtc)
{
    return TRUE;
}

static Bool
xwl_randr_output_set_property(ScreenPtr pScreen,
                              RROutputPtr output,
                              Atom property,
                              RRPropertyValuePtr value)
{
    return TRUE;
}

static Bool
xwl_output_validate_mode(ScreenPtr pScreen,
                         RROutputPtr output,
                         RRModePtr mode)
{
    return TRUE;
}

static void
xwl_randr_mode_destroy(ScreenPtr pScreen, RRModePtr mode)
{
    return;
}
#endif

Bool
xwl_screen_init_output(struct xwl_screen *xwl_screen)
{
    rrScrPrivPtr rp;

    if (!RRScreenInit(xwl_screen->screen))
        return FALSE;

    RRScreenSetSizeRange(xwl_screen->screen, 16, 16, 32767, 32767);

    rp = rrGetScrPriv(xwl_screen->screen);
    rp->rrGetInfo = xwl_randr_get_info;

#if RANDR_10_INTERFACE
    rp->rrSetConfig = xwl_randr_set_config;
#endif

#if RANDR_12_INTERFACE
    rp->rrScreenSetSize = xwl_randr_screen_set_size;
    rp->rrCrtcSet = xwl_randr_crtc_set;
    rp->rrCrtcSetGamma = xwl_randr_crtc_set_gamma;
    rp->rrCrtcGetGamma = xwl_randr_crtc_get_gamma;
    rp->rrOutputSetProperty = xwl_randr_output_set_property;
    rp->rrOutputValidateMode = xwl_output_validate_mode;
    rp->rrModeDestroy = xwl_randr_mode_destroy;
#endif

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
