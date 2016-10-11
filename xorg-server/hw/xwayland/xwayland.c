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

#include "xwayland.h"

#include <stdio.h>

#include <selection.h>
#include <micmap.h>
#include <misyncshm.h>
#include <compositeext.h>
#include <glx_extinit.h>
#include <os.h>
#include <xserver_poll.h>

#ifdef XF86VIDMODE
#include <X11/extensions/xf86vmproto.h>
_X_EXPORT Bool noXFree86VidModeExtension;
#endif

void
ddxGiveUp(enum ExitCode error)
{
}

void
AbortDDX(enum ExitCode error)
{
    ddxGiveUp(error);
}

void
OsVendorInit(void)
{
    if (serverGeneration == 1)
        ForceClockId(CLOCK_MONOTONIC);
}

void
OsVendorFatalError(const char *f, va_list args)
{
}

#if defined(DDXBEFORERESET)
void
ddxBeforeReset(void)
{
    return;
}
#endif

void
ddxUseMsg(void)
{
    ErrorF("-rootless              run rootless, requires wm support\n");
    ErrorF("-wm fd                 create X client for wm on given fd\n");
    ErrorF("-listen fd             add give fd as a listen socket\n");
}

int
ddxProcessArgument(int argc, char *argv[], int i)
{
    if (strcmp(argv[i], "-rootless") == 0) {
        return 1;
    }
    else if (strcmp(argv[i], "-listen") == 0) {
        NoListenAll = TRUE;
        return 2;
    }
    else if (strcmp(argv[i], "-wm") == 0) {
        return 2;
    }
    else if (strcmp(argv[i], "-shm") == 0) {
        return 1;
    }

    return 0;
}

static DevPrivateKeyRec xwl_window_private_key;
static DevPrivateKeyRec xwl_screen_private_key;
static DevPrivateKeyRec xwl_pixmap_private_key;

static struct xwl_window *
xwl_window_get(WindowPtr window)
{
    return dixLookupPrivate(&window->devPrivates, &xwl_window_private_key);
}

struct xwl_screen *
xwl_screen_get(ScreenPtr screen)
{
    return dixLookupPrivate(&screen->devPrivates, &xwl_screen_private_key);
}

static Bool
xwl_close_screen(ScreenPtr screen)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct xwl_output *xwl_output, *next_xwl_output;
    struct xwl_seat *xwl_seat, *next_xwl_seat;

    xorg_list_for_each_entry_safe(xwl_output, next_xwl_output,
                                  &xwl_screen->output_list, link)
        xwl_output_destroy(xwl_output);

    xorg_list_for_each_entry_safe(xwl_seat, next_xwl_seat,
                                  &xwl_screen->seat_list, link)
        xwl_seat_destroy(xwl_seat);

    RemoveNotifyFd(xwl_screen->wayland_fd);

    wl_display_disconnect(xwl_screen->display);

    screen->CloseScreen = xwl_screen->CloseScreen;
    free(xwl_screen);

    return screen->CloseScreen(screen);
}

static struct xwl_window *
xwl_window_from_window(WindowPtr window)
{
    struct xwl_window *xwl_window;

    while (window) {
        xwl_window = xwl_window_get(window);
        if (xwl_window)
            return xwl_window;

        window = window->parent;
    }

    return NULL;
}

static struct xwl_seat *
xwl_screen_get_default_seat(struct xwl_screen *xwl_screen)
{
    return container_of(xwl_screen->seat_list.prev,
                        struct xwl_seat,
                        link);
}

static void
xwl_cursor_warped_to(DeviceIntPtr device,
                     ScreenPtr screen,
                     ClientPtr client,
                     WindowPtr window,
                     SpritePtr sprite,
                     int x, int y)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct xwl_seat *xwl_seat = device->public.devicePrivate;
    struct xwl_window *xwl_window;

    if (!xwl_seat)
        xwl_seat = xwl_screen_get_default_seat(xwl_screen);

    xwl_window = xwl_window_from_window(window);
    if (!xwl_window)
        return;

    xwl_seat_emulate_pointer_warp(xwl_seat, xwl_window, sprite, x, y);
}

static void
xwl_cursor_confined_to(DeviceIntPtr device,
                       ScreenPtr screen,
                       WindowPtr window)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct xwl_seat *xwl_seat = device->public.devicePrivate;
    struct xwl_window *xwl_window;

    if (!xwl_seat)
        xwl_seat = xwl_screen_get_default_seat(xwl_screen);

    if (window == screen->root) {
        xwl_seat_unconfine_pointer(xwl_seat);
        return;
    }

    xwl_window = xwl_window_from_window(window);
    if (!xwl_window)
        return;

    xwl_seat_confine_pointer(xwl_seat, xwl_window);
}

static void
damage_report(DamagePtr pDamage, RegionPtr pRegion, void *data)
{
    struct xwl_window *xwl_window = data;
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;

    xorg_list_add(&xwl_window->link_damage, &xwl_screen->damage_window_list);
}

static void
damage_destroy(DamagePtr pDamage, void *data)
{
}

static void
shell_surface_ping(void *data,
                   struct wl_shell_surface *shell_surface, uint32_t serial)
{
    wl_shell_surface_pong(shell_surface, serial);
}

static void
shell_surface_configure(void *data,
                        struct wl_shell_surface *wl_shell_surface,
                        uint32_t edges, int32_t width, int32_t height)
{
}

static void
shell_surface_popup_done(void *data, struct wl_shell_surface *wl_shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
    shell_surface_ping,
    shell_surface_configure,
    shell_surface_popup_done
};

void
xwl_pixmap_set_private(PixmapPtr pixmap, struct xwl_pixmap *xwl_pixmap)
{
    dixSetPrivate(&pixmap->devPrivates, &xwl_pixmap_private_key, xwl_pixmap);
}

struct xwl_pixmap *
xwl_pixmap_get(PixmapPtr pixmap)
{
    return dixLookupPrivate(&pixmap->devPrivates, &xwl_pixmap_private_key);
}

static void
send_surface_id_event(struct xwl_window *xwl_window)
{
    static const char atom_name[] = "WL_SURFACE_ID";
    static Atom type_atom;
    DeviceIntPtr dev;
    xEvent e;

    if (type_atom == None)
        type_atom = MakeAtom(atom_name, strlen(atom_name), TRUE);

    e.u.u.type = ClientMessage;
    e.u.u.detail = 32;
    e.u.clientMessage.window = xwl_window->window->drawable.id;
    e.u.clientMessage.u.l.type = type_atom;
    e.u.clientMessage.u.l.longs0 =
        wl_proxy_get_id((struct wl_proxy *) xwl_window->surface);
    e.u.clientMessage.u.l.longs1 = 0;
    e.u.clientMessage.u.l.longs2 = 0;
    e.u.clientMessage.u.l.longs3 = 0;
    e.u.clientMessage.u.l.longs4 = 0;

    dev = PickPointer(serverClient);
    DeliverEventsToWindow(dev, xwl_window->xwl_screen->screen->root,
                          &e, 1, SubstructureRedirectMask, NullGrab);
}

static Bool
xwl_realize_window(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen;
    struct xwl_window *xwl_window;
    struct wl_region *region;
    Bool ret;

    xwl_screen = xwl_screen_get(screen);

    screen->RealizeWindow = xwl_screen->RealizeWindow;
    ret = (*screen->RealizeWindow) (window);
    xwl_screen->RealizeWindow = screen->RealizeWindow;
    screen->RealizeWindow = xwl_realize_window;

    if (xwl_screen->rootless && !window->parent) {
        RegionNull(&window->clipList);
        RegionNull(&window->borderClip);
        RegionNull(&window->winSize);
    }

    if (xwl_screen->rootless) {
        if (window->redirectDraw != RedirectDrawManual)
            return ret;
    }
    else {
        if (window->parent)
            return ret;
    }

    xwl_window = calloc(sizeof *xwl_window, 1);
    if (xwl_window == NULL)
        return FALSE;

    xwl_window->xwl_screen = xwl_screen;
    xwl_window->window = window;
    xwl_window->surface = wl_compositor_create_surface(xwl_screen->compositor);
    if (xwl_window->surface == NULL) {
        ErrorF("wl_display_create_surface failed\n");
        goto err;
    }

    if (!xwl_screen->rootless) {
        xwl_window->shell_surface =
            wl_shell_get_shell_surface(xwl_screen->shell, xwl_window->surface);
        if (xwl_window->shell_surface == NULL) {
            ErrorF("Failed creating shell surface\n");
            goto err_surf;
        }

        wl_shell_surface_add_listener(xwl_window->shell_surface,
                                      &shell_surface_listener, xwl_window);

        wl_shell_surface_set_toplevel(xwl_window->shell_surface);

        region = wl_compositor_create_region(xwl_screen->compositor);
        if (region == NULL) {
            ErrorF("Failed creating region\n");
            goto err_surf;
        }

        wl_region_add(region, 0, 0,
                      window->drawable.width, window->drawable.height);
        wl_surface_set_opaque_region(xwl_window->surface, region);
        wl_region_destroy(region);
    }

    wl_display_flush(xwl_screen->display);

    send_surface_id_event(xwl_window);

    wl_surface_set_user_data(xwl_window->surface, xwl_window);

    xwl_window->damage =
        DamageCreate(damage_report, damage_destroy, DamageReportNonEmpty,
                     FALSE, screen, xwl_window);
    if (xwl_window->damage == NULL) {
        ErrorF("Failed creating damage\n");
        goto err_surf;
    }

    DamageRegister(&window->drawable, xwl_window->damage);
    DamageSetReportAfterOp(xwl_window->damage, TRUE);

    dixSetPrivate(&window->devPrivates, &xwl_window_private_key, xwl_window);
    xorg_list_init(&xwl_window->link_damage);

    return ret;

err_surf:
    if (xwl_window->shell_surface)
        wl_shell_surface_destroy(xwl_window->shell_surface);
    wl_surface_destroy(xwl_window->surface);
err:
    free(xwl_window);
    return FALSE;
}

static Bool
xwl_unrealize_window(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen;
    struct xwl_window *xwl_window;
    struct xwl_seat *xwl_seat;
    Bool ret;

    xwl_screen = xwl_screen_get(screen);

    xorg_list_for_each_entry(xwl_seat, &xwl_screen->seat_list, link) {
        if (xwl_seat->focus_window && xwl_seat->focus_window->window == window)
            xwl_seat->focus_window = NULL;
        if (xwl_seat->last_xwindow == window)
            xwl_seat->last_xwindow = NullWindow;
        if (xwl_seat->cursor_confinement_window &&
            xwl_seat->cursor_confinement_window->window == window)
            xwl_seat_unconfine_pointer(xwl_seat);
        if (xwl_seat->pointer_warp_emulator &&
            xwl_seat->pointer_warp_emulator->locked_window &&
            xwl_seat->pointer_warp_emulator->locked_window->window == window)
            xwl_seat_destroy_pointer_warp_emulator(xwl_seat);
        xwl_seat_clear_touch(xwl_seat, window);
    }

    screen->UnrealizeWindow = xwl_screen->UnrealizeWindow;
    ret = (*screen->UnrealizeWindow) (window);
    xwl_screen->UnrealizeWindow = screen->UnrealizeWindow;
    screen->UnrealizeWindow = xwl_unrealize_window;

    xwl_window = xwl_window_get(window);
    if (!xwl_window)
        return ret;

    wl_surface_destroy(xwl_window->surface);
    if (RegionNotEmpty(DamageRegion(xwl_window->damage)))
        xorg_list_del(&xwl_window->link_damage);
    DamageUnregister(xwl_window->damage);
    DamageDestroy(xwl_window->damage);
    if (xwl_window->frame_callback)
        wl_callback_destroy(xwl_window->frame_callback);

    free(xwl_window);
    dixSetPrivate(&window->devPrivates, &xwl_window_private_key, NULL);

    return ret;
}

static Bool
xwl_save_screen(ScreenPtr pScreen, int on)
{
    return TRUE;
}

static void
frame_callback(void *data,
               struct wl_callback *callback,
               uint32_t time)
{
    struct xwl_window *xwl_window = data;

    wl_callback_destroy (xwl_window->frame_callback);
    xwl_window->frame_callback = NULL;
}

static const struct wl_callback_listener frame_listener = {
    frame_callback
};

static void
xwl_screen_post_damage(struct xwl_screen *xwl_screen)
{
    struct xwl_window *xwl_window, *next_xwl_window;
    RegionPtr region;
    BoxPtr box;
    struct wl_buffer *buffer;
    PixmapPtr pixmap;

    xorg_list_for_each_entry_safe(xwl_window, next_xwl_window,
                                  &xwl_screen->damage_window_list, link_damage) {
        /* If we're waiting on a frame callback from the server,
         * don't attach a new buffer. */
        if (xwl_window->frame_callback)
            continue;

        region = DamageRegion(xwl_window->damage);
        pixmap = (*xwl_screen->screen->GetWindowPixmap) (xwl_window->window);

#if GLAMOR_HAS_GBM
        if (xwl_screen->glamor)
            buffer = xwl_glamor_pixmap_get_wl_buffer(pixmap);
#endif
        if (!xwl_screen->glamor)
            buffer = xwl_shm_pixmap_get_wl_buffer(pixmap);

        wl_surface_attach(xwl_window->surface, buffer, 0, 0);

        box = RegionExtents(region);
        wl_surface_damage(xwl_window->surface, box->x1, box->y1,
                          box->x2 - box->x1, box->y2 - box->y1);

        xwl_window->frame_callback = wl_surface_frame(xwl_window->surface);
        wl_callback_add_listener(xwl_window->frame_callback, &frame_listener, xwl_window);

        wl_surface_commit(xwl_window->surface);
        DamageEmpty(xwl_window->damage);

        xorg_list_del(&xwl_window->link_damage);
    }
}

static void
registry_global(void *data, struct wl_registry *registry, uint32_t id,
                const char *interface, uint32_t version)
{
    struct xwl_screen *xwl_screen = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        xwl_screen->compositor =
            wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    }
    else if (strcmp(interface, "wl_shm") == 0) {
        xwl_screen->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    }
    else if (strcmp(interface, "wl_shell") == 0) {
        xwl_screen->shell =
            wl_registry_bind(registry, id, &wl_shell_interface, 1);
    }
    else if (strcmp(interface, "wl_output") == 0 && version >= 2) {
        if (xwl_output_create(xwl_screen, id))
            xwl_screen->expecting_event++;
    }
#ifdef GLAMOR_HAS_GBM
    else if (xwl_screen->glamor &&
             strcmp(interface, "wl_drm") == 0 && version >= 2) {
        xwl_screen_init_glamor(xwl_screen, id, version);
    }
#endif
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    struct xwl_screen *xwl_screen = data;
    struct xwl_output *xwl_output, *tmp_xwl_output;

    xorg_list_for_each_entry_safe(xwl_output, tmp_xwl_output,
                                  &xwl_screen->output_list, link) {
        if (xwl_output->server_output_id == name) {
            xwl_output_remove(xwl_output);
            break;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    global_remove
};

static void
xwl_read_events (struct xwl_screen *xwl_screen)
{
    int ret;

    if (xwl_screen->wait_flush)
        return;

    ret = wl_display_read_events(xwl_screen->display);
    if (ret == -1)
        FatalError("failed to read Wayland events: %s\n", strerror(errno));

    xwl_screen->prepare_read = 0;

    ret = wl_display_dispatch_pending(xwl_screen->display);
    if (ret == -1)
        FatalError("failed to dispatch Wayland events: %s\n", strerror(errno));
}

static int
xwl_display_pollout (struct xwl_screen *xwl_screen, int timeout)
{
    struct pollfd poll_fd;

    poll_fd.fd = wl_display_get_fd(xwl_screen->display);
    poll_fd.events = POLLOUT;

    return xserver_poll(&poll_fd, 1, timeout);
}

static void
xwl_dispatch_events (struct xwl_screen *xwl_screen)
{
    int ret = 0;
    int ready;

    if (xwl_screen->wait_flush)
        goto pollout;

    while (xwl_screen->prepare_read == 0 &&
           wl_display_prepare_read(xwl_screen->display) == -1) {
        ret = wl_display_dispatch_pending(xwl_screen->display);
        if (ret == -1)
            FatalError("failed to dispatch Wayland events: %s\n",
                       strerror(errno));
    }

    xwl_screen->prepare_read = 1;

pollout:
    ready = xwl_display_pollout(xwl_screen, 5);
    if (ready == -1 && errno != EINTR)
        FatalError("error polling on XWayland fd: %s\n", strerror(errno));

    if (ready > 0)
        ret = wl_display_flush(xwl_screen->display);

    if (ret == -1 && errno != EAGAIN)
        FatalError("failed to write to XWayland fd: %s\n", strerror(errno));

    xwl_screen->wait_flush = (ready == 0 || ready == -1 || ret == -1);
}

static void
socket_handler(int fd, int ready, void *data)
{
    struct xwl_screen *xwl_screen = data;

    xwl_read_events (xwl_screen);
}

static void
wakeup_handler(void *data, int err)
{
}

static void
block_handler(void *data, void *timeout)
{
    struct xwl_screen *xwl_screen = data;

    xwl_screen_post_damage(xwl_screen);
    xwl_dispatch_events (xwl_screen);
}

void
xwl_sync_events (struct xwl_screen *xwl_screen)
{
    xwl_dispatch_events (xwl_screen);
    xwl_read_events (xwl_screen);
}

static CARD32
add_client_fd(OsTimerPtr timer, CARD32 time, void *arg)
{
    struct xwl_screen *xwl_screen = arg;

    if (!AddClientOnOpenFD(xwl_screen->wm_fd))
        FatalError("Failed to add wm client\n");

    TimerFree(timer);

    return 0;
}

static void
listen_on_fds(struct xwl_screen *xwl_screen)
{
    int i;

    for (i = 0; i < xwl_screen->listen_fd_count; i++)
        ListenOnOpenFD(xwl_screen->listen_fds[i], FALSE);
}

static void
wm_selection_callback(CallbackListPtr *p, void *data, void *arg)
{
    SelectionInfoRec *info = arg;
    struct xwl_screen *xwl_screen = data;
    static const char atom_name[] = "WM_S0";
    static Atom atom_wm_s0;

    if (atom_wm_s0 == None)
        atom_wm_s0 = MakeAtom(atom_name, strlen(atom_name), TRUE);
    if (info->selection->selection != atom_wm_s0 ||
        info->kind != SelectionSetOwner)
        return;

    listen_on_fds(xwl_screen);

    DeleteCallback(&SelectionCallback, wm_selection_callback, xwl_screen);
}

static Bool
xwl_screen_init(ScreenPtr pScreen, int argc, char **argv)
{
    struct xwl_screen *xwl_screen;
    Pixel red_mask, blue_mask, green_mask;
    int ret, bpc, green_bpc, i;

    xwl_screen = calloc(sizeof *xwl_screen, 1);
    if (xwl_screen == NULL)
        return FALSE;
    xwl_screen->wm_fd = -1;

    if (!dixRegisterPrivateKey(&xwl_screen_private_key, PRIVATE_SCREEN, 0))
        return FALSE;
    if (!dixRegisterPrivateKey(&xwl_window_private_key, PRIVATE_WINDOW, 0))
        return FALSE;
    if (!dixRegisterPrivateKey(&xwl_pixmap_private_key, PRIVATE_PIXMAP, 0))
        return FALSE;

    dixSetPrivate(&pScreen->devPrivates, &xwl_screen_private_key, xwl_screen);
    xwl_screen->screen = pScreen;

#ifdef GLAMOR_HAS_GBM
    xwl_screen->glamor = 1;
#endif

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-rootless") == 0) {
            xwl_screen->rootless = 1;
        }
        else if (strcmp(argv[i], "-wm") == 0) {
            xwl_screen->wm_fd = atoi(argv[i + 1]);
            i++;
            TimerSet(NULL, 0, 1, add_client_fd, xwl_screen);
        }
        else if (strcmp(argv[i], "-listen") == 0) {
            if (xwl_screen->listen_fd_count ==
                ARRAY_SIZE(xwl_screen->listen_fds))
                FatalError("Too many -listen arguments given, max is %ld\n",
                           ARRAY_SIZE(xwl_screen->listen_fds));

            xwl_screen->listen_fds[xwl_screen->listen_fd_count++] =
                atoi(argv[i + 1]);
            i++;
        }
        else if (strcmp(argv[i], "-shm") == 0) {
            xwl_screen->glamor = 0;
        }
    }

    /* In rootless mode, we don't have any screen storage, and the only
     * rendering should be to redirected mode. */
    if (xwl_screen->rootless)
        xwl_screen->root_clip_mode = ROOT_CLIP_INPUT_ONLY;
    else
        xwl_screen->root_clip_mode = ROOT_CLIP_FULL;

    if (xwl_screen->listen_fd_count > 0) {
        if (xwl_screen->wm_fd >= 0)
            AddCallback(&SelectionCallback, wm_selection_callback, xwl_screen);
        else
            listen_on_fds(xwl_screen);
    }

    xorg_list_init(&xwl_screen->output_list);
    xorg_list_init(&xwl_screen->seat_list);
    xorg_list_init(&xwl_screen->damage_window_list);
    xwl_screen->depth = 24;

    xwl_screen->display = wl_display_connect(NULL);
    if (xwl_screen->display == NULL) {
        ErrorF("could not connect to wayland server\n");
        return FALSE;
    }

    if (!xwl_screen_init_output(xwl_screen))
        return FALSE;

    xwl_screen->expecting_event = 0;
    xwl_screen->registry = wl_display_get_registry(xwl_screen->display);
    wl_registry_add_listener(xwl_screen->registry,
                             &registry_listener, xwl_screen);
    ret = wl_display_roundtrip(xwl_screen->display);
    if (ret == -1) {
        ErrorF("could not connect to wayland server\n");
        return FALSE;
    }

    while (xwl_screen->expecting_event > 0)
        wl_display_roundtrip(xwl_screen->display);

    bpc = xwl_screen->depth / 3;
    green_bpc = xwl_screen->depth - 2 * bpc;
    blue_mask = (1 << bpc) - 1;
    green_mask = ((1 << green_bpc) - 1) << bpc;
    red_mask = blue_mask << (green_bpc + bpc);

    miSetVisualTypesAndMasks(xwl_screen->depth,
                             ((1 << TrueColor) | (1 << DirectColor)),
                             green_bpc, TrueColor,
                             red_mask, green_mask, blue_mask);

    miSetPixmapDepths();

    ret = fbScreenInit(pScreen, NULL,
                       xwl_screen->width, xwl_screen->height,
                       96, 96, 0,
                       BitsPerPixel(xwl_screen->depth));
    if (!ret)
        return FALSE;

    fbPictureInit(pScreen, 0, 0);

#ifdef HAVE_XSHMFENCE
    if (!miSyncShmScreenInit(pScreen))
        return FALSE;
#endif

    xwl_screen->wayland_fd = wl_display_get_fd(xwl_screen->display);
    SetNotifyFd(xwl_screen->wayland_fd, socket_handler, X_NOTIFY_READ, xwl_screen);
    RegisterBlockAndWakeupHandlers(block_handler, wakeup_handler, xwl_screen);

    pScreen->SaveScreen = xwl_save_screen;

    pScreen->blackPixel = 0;
    pScreen->whitePixel = 1;

    ret = fbCreateDefColormap(pScreen);

    if (!xwl_screen_init_cursor(xwl_screen))
        return FALSE;

#ifdef GLAMOR_HAS_GBM
    if (xwl_screen->glamor && !xwl_glamor_init(xwl_screen)) {
        ErrorF("Failed to initialize glamor, falling back to sw\n");
        xwl_screen->glamor = 0;
    }
#endif

    if (!xwl_screen->glamor) {
        xwl_screen->CreateScreenResources = pScreen->CreateScreenResources;
        pScreen->CreateScreenResources = xwl_shm_create_screen_resources;
        pScreen->CreatePixmap = xwl_shm_create_pixmap;
        pScreen->DestroyPixmap = xwl_shm_destroy_pixmap;
    }

    xwl_screen->RealizeWindow = pScreen->RealizeWindow;
    pScreen->RealizeWindow = xwl_realize_window;

    xwl_screen->UnrealizeWindow = pScreen->UnrealizeWindow;
    pScreen->UnrealizeWindow = xwl_unrealize_window;

    xwl_screen->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = xwl_close_screen;

    pScreen->CursorWarpedTo = xwl_cursor_warped_to;
    pScreen->CursorConfinedTo = xwl_cursor_confined_to;

    return ret;
}

_X_NORETURN
static void _X_ATTRIBUTE_PRINTF(1, 0)
xwl_log_handler(const char *format, va_list args)
{
    char msg[256];

    vsnprintf(msg, sizeof msg, format, args);
    FatalError("%s", msg);
}

static const ExtensionModule xwayland_extensions[] = {
#ifdef GLXEXT
    { GlxExtensionInit, "GLX", &noGlxExtension },
#endif
#ifdef XF86VIDMODE
    { xwlVidModeExtensionInit, XF86VIDMODENAME, &noXFree86VidModeExtension },
#endif
};

void
InitOutput(ScreenInfo * screen_info, int argc, char **argv)
{
    int depths[] = { 1, 4, 8, 15, 16, 24, 32 };
    int bpp[] =    { 1, 8, 8, 16, 16, 32, 32 };
    int i;

    for (i = 0; i < ARRAY_SIZE(depths); i++) {
        screen_info->formats[i].depth = depths[i];
        screen_info->formats[i].bitsPerPixel = bpp[i];
        screen_info->formats[i].scanlinePad = BITMAP_SCANLINE_PAD;
    }

    screen_info->imageByteOrder = IMAGE_BYTE_ORDER;
    screen_info->bitmapScanlineUnit = BITMAP_SCANLINE_UNIT;
    screen_info->bitmapScanlinePad = BITMAP_SCANLINE_PAD;
    screen_info->bitmapBitOrder = BITMAP_BIT_ORDER;
    screen_info->numPixmapFormats = ARRAY_SIZE(depths);

    LoadExtensionList(xwayland_extensions,
                      ARRAY_SIZE(xwayland_extensions), FALSE);

    /* Cast away warning from missing printf annotation for
     * wl_log_func_t.  Wayland 1.5 will have the annotation, so we can
     * remove the cast and require that when it's released. */
    wl_log_set_handler_client((void *) xwl_log_handler);

    if (AddScreen(xwl_screen_init, argc, argv) == -1) {
        FatalError("Couldn't add screen\n");
    }

    LocalAccessScopeUser();
}
