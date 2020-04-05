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

#include <X11/X.h>
#include <X11/Xatom.h>

#include "compositeext.h"
#include "compint.h"
#include "inputstr.h"
#include "propertyst.h"

#include "xwayland-types.h"
#include "xwayland-input.h"
#include "xwayland-present.h"
#include "xwayland-screen.h"
#include "xwayland-window.h"
#include "xwayland-window-buffers.h"
#include "xwayland-shm.h"

#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"

static DevPrivateKeyRec xwl_window_private_key;
static DevPrivateKeyRec xwl_damage_private_key;

static void
xwl_window_set_allow_commits(struct xwl_window *xwl_window, Bool allow,
                             const char *debug_msg)
{
    xwl_window->allow_commits = allow;
    DebugF("xwayland: win %d allow_commits = %d (%s)\n",
           xwl_window->window->drawable.id, allow, debug_msg);
}

static void
xwl_window_set_allow_commits_from_property(struct xwl_window *xwl_window,
                                           PropertyPtr prop)
{
    static Bool warned = FALSE;
    CARD32 *propdata;

    if (prop->propertyName != xwl_window->xwl_screen->allow_commits_prop)
        FatalError("Xwayland internal error: prop mismatch in %s.\n", __func__);

    if (prop->type != XA_CARDINAL || prop->format != 32 || prop->size != 1) {
        /* Not properly set, so fall back to safe and glitchy */
        xwl_window_set_allow_commits(xwl_window, TRUE, "WM fault");

        if (!warned) {
            LogMessage(X_WARNING, "Window manager is misusing property %s.\n",
                       NameForAtom(prop->propertyName));
            warned = TRUE;
        }
        return;
    }

    propdata = prop->data;
    xwl_window_set_allow_commits(xwl_window, !!propdata[0], "from property");
}

struct xwl_window *
xwl_window_get(WindowPtr window)
{
    return dixLookupPrivate(&window->devPrivates, &xwl_window_private_key);
}

static DamagePtr
window_get_damage(WindowPtr window)
{
    return dixLookupPrivate(&window->devPrivates, &xwl_damage_private_key);
}

struct xwl_window *
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

void
xwl_window_update_property(struct xwl_window *xwl_window,
                           PropertyStateRec *propstate)
{
    switch (propstate->state) {
    case PropertyNewValue:
        xwl_window_set_allow_commits_from_property(xwl_window, propstate->prop);
        break;

    case PropertyDelete:
        xwl_window_set_allow_commits(xwl_window, TRUE, "property deleted");
        break;

    default:
        break;
    }
}

static void
damage_report(DamagePtr pDamage, RegionPtr pRegion, void *data)
{
    WindowPtr window = data;
    struct xwl_window *xwl_window = xwl_window_get(window);
    struct xwl_screen *xwl_screen;

    if (!xwl_window)
        return;

    xwl_screen = xwl_window->xwl_screen;

#ifdef GLAMOR_HAS_GBM
    if (xwl_window->present_flipped) {
        /* This damage is from a Present flip, which already committed a new
         * buffer for the surface, so we don't need to do anything in response
         */
        RegionEmpty(DamageRegion(pDamage));
        xorg_list_del(&xwl_window->link_damage);
        xwl_window->present_flipped = FALSE;
        return;
    }
#endif

    xorg_list_add(&xwl_window->link_damage, &xwl_screen->damage_window_list);
}

static void
damage_destroy(DamagePtr pDamage, void *data)
{
}

static Bool
register_damage(WindowPtr window)
{
    DamagePtr damage;

    damage = DamageCreate(damage_report, damage_destroy, DamageReportNonEmpty,
                          FALSE, window->drawable.pScreen, window);
    if (damage == NULL) {
        ErrorF("Failed creating damage\n");
        return FALSE;
    }

    DamageRegister(&window->drawable, damage);
    DamageSetReportAfterOp(damage, TRUE);

    dixSetPrivate(&window->devPrivates, &xwl_damage_private_key, damage);

    return TRUE;
}

static void
unregister_damage(WindowPtr window)
{
    DamagePtr damage;

    damage = dixLookupPrivate(&window->devPrivates, &xwl_damage_private_key);
    if (!damage)
        return;

    DamageUnregister(damage);
    DamageDestroy(damage);

    dixSetPrivate(&window->devPrivates, &xwl_damage_private_key, NULL);
}

Bool
xwl_window_has_viewport_enabled(struct xwl_window *xwl_window)
{
    return (xwl_window->viewport != NULL);
}

static void
xwl_window_disable_viewport(struct xwl_window *xwl_window)
{
    assert (xwl_window->viewport);

    DebugF("XWAYLAND: disabling viewport\n");
    wp_viewport_destroy(xwl_window->viewport);
    xwl_window->viewport = NULL;
}

static void
xwl_window_enable_viewport(struct xwl_window *xwl_window,
                           struct xwl_output *xwl_output,
                           struct xwl_emulated_mode *emulated_mode)
{
    if (!xwl_window_has_viewport_enabled(xwl_window)) {
        DebugF("XWAYLAND: enabling viewport %dx%d -> %dx%d\n",
               emulated_mode->width, emulated_mode->height,
               xwl_output->width, xwl_output->height);
        xwl_window->viewport = wp_viewporter_get_viewport(xwl_window->xwl_screen->viewporter,
                                                          xwl_window->surface);
    }

    wp_viewport_set_source(xwl_window->viewport,
                           wl_fixed_from_int(0),
                           wl_fixed_from_int(0),
                           wl_fixed_from_int(emulated_mode->width),
                           wl_fixed_from_int(emulated_mode->height));
    wp_viewport_set_destination(xwl_window->viewport,
                                xwl_output->width,
                                xwl_output->height);

    xwl_window->scale_x = (float)emulated_mode->width  / xwl_output->width;
    xwl_window->scale_y = (float)emulated_mode->height / xwl_output->height;
}

static Bool
window_is_wm_window(WindowPtr window)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(window->drawable.pScreen);

    return CLIENT_ID(window->drawable.id) == xwl_screen->wm_client_id;
}

static WindowPtr
window_get_client_toplevel(WindowPtr window)
{
    assert(window);

    /* If the toplevel window is owned by the window-manager, then the
     * actual client toplevel window has been reparented to some window-manager
     * decoration/wrapper windows. In that case recurse by checking the client
     * of the first *and only* child of the decoration/wrapper window.
     */
    if (window_is_wm_window(window)) {
        if (window->firstChild && window->firstChild == window->lastChild)
            return window_get_client_toplevel(window->firstChild);
        else
            return NULL; /* Should never happen, skip resolution emulation */
    }

    return window;
}

static Bool
xwl_window_should_enable_viewport(struct xwl_window *xwl_window,
                                  struct xwl_output **xwl_output_ret,
                                  struct xwl_emulated_mode **emulated_mode_ret)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;
    struct xwl_emulated_mode *emulated_mode;
    struct xwl_output *xwl_output;
    ClientPtr owner;
    WindowPtr window;
    DrawablePtr drawable;

    if (!xwl_screen_has_resolution_change_emulation(xwl_screen))
        return FALSE;

    window = window_get_client_toplevel(xwl_window->window);
    if (!window)
        return FALSE;

    owner = wClient(window);
    drawable = &window->drawable;

    /* 1. Test if the window matches the emulated mode on one of the outputs
     * This path gets hit by most games / libs (e.g. SDL, SFML, OGRE)
     */
    xorg_list_for_each_entry(xwl_output, &xwl_screen->output_list, link) {
        emulated_mode = xwl_output_get_emulated_mode_for_client(xwl_output, owner);
        if (!emulated_mode)
            continue;

        if (drawable->x == xwl_output->x &&
            drawable->y == xwl_output->y &&
            drawable->width  == emulated_mode->width &&
            drawable->height == emulated_mode->height) {

            *emulated_mode_ret = emulated_mode;
            *xwl_output_ret = xwl_output;
            return TRUE;
        }
    }

    /* 2. Test if the window uses override-redirect + vidmode
     * and matches (fully covers) the entire screen.
     * This path gets hit by: allegro4, ClanLib-1.0.
     */
    xwl_output = xwl_screen_get_first_output(xwl_screen);
    emulated_mode = xwl_output_get_emulated_mode_for_client(xwl_output, owner);
    if (xwl_output && xwl_window->window->overrideRedirect &&
        emulated_mode && emulated_mode->from_vidmode &&
        drawable->x == 0 && drawable->y == 0 &&
        drawable->width  == xwl_screen->width &&
        drawable->height == xwl_screen->height) {

        *emulated_mode_ret = emulated_mode;
        *xwl_output_ret = xwl_output;
        return TRUE;
    }

    return FALSE;
}

void
xwl_window_check_resolution_change_emulation(struct xwl_window *xwl_window)
{
    struct xwl_emulated_mode *emulated_mode;
    struct xwl_output *xwl_output;

    if (xwl_window_should_enable_viewport(xwl_window, &xwl_output, &emulated_mode))
        xwl_window_enable_viewport(xwl_window, xwl_output, emulated_mode);
    else if (xwl_window_has_viewport_enabled(xwl_window))
        xwl_window_disable_viewport(xwl_window);
}

/* This checks if the passed in Window is a toplevel client window, note this
 * returns false for window-manager decoration windows and returns true for
 * the actual client top-level window even if it has been reparented to
 * a window-manager decoration window.
 */
Bool
xwl_window_is_toplevel(WindowPtr window)
{
    if (window_is_wm_window(window))
        return FALSE;

    /* CSD and override-redirect toplevel windows */
    if (window_get_damage(window))
        return TRUE;

    /* Normal toplevel client windows, reparented to a window-manager window */
    return window->parent && window_is_wm_window(window->parent);
}

static void
xwl_window_init_allow_commits(struct xwl_window *xwl_window)
{
    PropertyPtr prop = NULL;
    int ret;

    ret = dixLookupProperty(&prop, xwl_window->window,
                            xwl_window->xwl_screen->allow_commits_prop,
                            serverClient, DixReadAccess);
    if (ret == Success && prop)
        xwl_window_set_allow_commits_from_property(xwl_window, prop);
    else
        xwl_window_set_allow_commits(xwl_window, TRUE, "no property");
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

static void
xdg_surface_handle_configure(void *data,
                             struct xdg_surface *xdg_surface,
                             uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_handle_configure,
};

static Bool
ensure_surface_for_window(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen;
    struct xwl_window *xwl_window;
    struct wl_region *region;
    WindowPtr toplevel;

    if (xwl_window_get(window))
        return TRUE;

    xwl_screen = xwl_screen_get(screen);

    if (xwl_screen->rootless) {
        if (window->redirectDraw != RedirectDrawManual)
            return TRUE;
    }
    else {
        if (window->parent)
            return TRUE;
    }

    xwl_window = calloc(1, sizeof *xwl_window);
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
        xwl_window->xdg_surface =
            xdg_wm_base_get_xdg_surface(xwl_screen->xdg_wm_base, xwl_window->surface);
        if (xwl_window->xdg_surface == NULL) {
            ErrorF("Failed creating xdg_wm_base xdg_surface\n");
            goto err_surf;
        }

        xdg_surface_add_listener(xwl_window->xdg_surface,
                                 &xdg_surface_listener, xwl_window);

        xdg_surface_get_toplevel(xwl_window->xdg_surface);

        wl_surface_commit(xwl_window->surface);

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

    compRedirectWindow(serverClient, window, CompositeRedirectManual);

    dixSetPrivate(&window->devPrivates, &xwl_window_private_key, xwl_window);
    xorg_list_init(&xwl_window->link_damage);
    xorg_list_add(&xwl_window->link_window, &xwl_screen->window_list);

#ifdef GLAMOR_HAS_GBM
    xorg_list_init(&xwl_window->frame_callback_list);
#endif

    xwl_window_buffers_init(xwl_window);

    xwl_window_init_allow_commits(xwl_window);

    /* When a new window-manager window is realized, then the randr emulation
     * props may have not been set on the managed client window yet.
     */
    if (window_is_wm_window(window)) {
        toplevel = window_get_client_toplevel(window);
        if (toplevel)
            xwl_output_set_window_randr_emu_props(xwl_screen, toplevel);
    } else {
        /* CSD or O-R toplevel window, check viewport on creation */
        xwl_window_check_resolution_change_emulation(xwl_window);
    }

    return TRUE;

err_surf:
    if (xwl_window->xdg_surface)
        xdg_surface_destroy(xwl_window->xdg_surface);
    wl_surface_destroy(xwl_window->surface);
err:
    free(xwl_window);
    return FALSE;
}

Bool
xwl_realize_window(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen;
    Bool ret;

    xwl_screen = xwl_screen_get(screen);

    screen->RealizeWindow = xwl_screen->RealizeWindow;
    ret = (*screen->RealizeWindow) (window);
    xwl_screen->RealizeWindow = screen->RealizeWindow;
    screen->RealizeWindow = xwl_realize_window;

    if (!ret)
        return FALSE;

    if (xwl_screen->rootless && !window->parent) {
        BoxRec box = { 0, 0, xwl_screen->width, xwl_screen->height };

        RegionReset(&window->winSize, &box);
        RegionNull(&window->clipList);
        RegionNull(&window->borderClip);
    }

    if (xwl_screen->rootless ?
        (window->drawable.class == InputOutput &&
         window->parent == window->drawable.pScreen->root) :
        !window->parent) {
        if (!register_damage(window))
            return FALSE;
    }

    return ensure_surface_for_window(window);
}

Bool
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
        if (xwl_seat->tablet_focus_window && xwl_seat->tablet_focus_window->window == window)
            xwl_seat->tablet_focus_window = NULL;
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

    compUnredirectWindow(serverClient, window, CompositeRedirectManual);

    screen->UnrealizeWindow = xwl_screen->UnrealizeWindow;
    ret = (*screen->UnrealizeWindow) (window);
    xwl_screen->UnrealizeWindow = screen->UnrealizeWindow;
    screen->UnrealizeWindow = xwl_unrealize_window;

    xwl_window = xwl_window_get(window);
    if (!xwl_window)
        return ret;

    if (xwl_window_has_viewport_enabled(xwl_window))
        xwl_window_disable_viewport(xwl_window);

    wl_surface_destroy(xwl_window->surface);
    xorg_list_del(&xwl_window->link_damage);
    xorg_list_del(&xwl_window->link_window);
    unregister_damage(window);

    xwl_window_buffers_dispose(xwl_window);

    if (xwl_window->frame_callback)
        wl_callback_destroy(xwl_window->frame_callback);

#ifdef GLAMOR_HAS_GBM
    if (xwl_screen->present) {
        struct xwl_present_window *xwl_present_window, *tmp;

        xorg_list_for_each_entry_safe(xwl_present_window, tmp,
                                      &xwl_window->frame_callback_list,
                                      frame_callback_list) {
            xwl_present_unrealize_window(xwl_present_window);
        }
    }
#endif

    free(xwl_window);
    dixSetPrivate(&window->devPrivates, &xwl_window_private_key, NULL);

    return ret;
}

void
xwl_window_set_window_pixmap(WindowPtr window,
                             PixmapPtr pixmap)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen;
    struct xwl_window *xwl_window;
    PixmapPtr old_pixmap;

    old_pixmap = (*screen->GetWindowPixmap) (window);
    xwl_screen = xwl_screen_get(screen);

    screen->SetWindowPixmap = xwl_screen->SetWindowPixmap;
    (*screen->SetWindowPixmap) (window, pixmap);
    xwl_screen->SetWindowPixmap = screen->SetWindowPixmap;
    screen->SetWindowPixmap = xwl_window_set_window_pixmap;

    if (!RegionNotEmpty(&window->winSize))
        return;

    ensure_surface_for_window(window);

    if (old_pixmap->drawable.width == pixmap->drawable.width &&
        old_pixmap->drawable.height == pixmap->drawable.height)
       return;

    xwl_window = xwl_window_get(window);
    if (xwl_window)
            xwl_window_buffers_recycle(xwl_window);
}

Bool
xwl_change_window_attributes(WindowPtr window, unsigned long mask)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    OtherClients *others;
    Bool ret;

    screen->ChangeWindowAttributes = xwl_screen->ChangeWindowAttributes;
    ret = (*screen->ChangeWindowAttributes) (window, mask);
    xwl_screen->ChangeWindowAttributes = screen->ChangeWindowAttributes;
    screen->ChangeWindowAttributes = xwl_change_window_attributes;

    if (window != screen->root || !(mask & CWEventMask))
        return ret;

    for (others = wOtherClients(window); others; others = others->next) {
        if (others->mask & (SubstructureRedirectMask | ResizeRedirectMask))
            xwl_screen->wm_client_id = CLIENT_ID(others->resource);
    }

    return ret;
}

void
xwl_resize_window(WindowPtr window,
                  int x, int y,
                  unsigned int width, unsigned int height,
                  WindowPtr sib)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen;
    struct xwl_window *xwl_window;

    xwl_screen = xwl_screen_get(screen);
    xwl_window = xwl_window_from_window(window);

    screen->ResizeWindow = xwl_screen->ResizeWindow;
    (*screen->ResizeWindow) (window, x, y, width, height, sib);
    xwl_screen->ResizeWindow = screen->ResizeWindow;
    screen->ResizeWindow = xwl_resize_window;

    if (xwl_window && (xwl_window_get(window) || xwl_window_is_toplevel(window)))
        xwl_window_check_resolution_change_emulation(xwl_window);
}

void
xwl_move_window(WindowPtr window,
                int x, int y,
                WindowPtr next_sib,
                VTKind kind)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen;
    struct xwl_window *xwl_window;

    xwl_screen = xwl_screen_get(screen);
    xwl_window = xwl_window_from_window(window);

    screen->MoveWindow = xwl_screen->MoveWindow;
    (*screen->MoveWindow) (window, x, y, next_sib, kind);
    xwl_screen->MoveWindow = screen->MoveWindow;
    screen->MoveWindow = xwl_move_window;

    if (xwl_window && (xwl_window_get(window) || xwl_window_is_toplevel(window)))
        xwl_window_check_resolution_change_emulation(xwl_window);
}

static void
frame_callback(void *data,
               struct wl_callback *callback,
               uint32_t time)
{
    struct xwl_window *xwl_window = data;

    wl_callback_destroy (xwl_window->frame_callback);
    xwl_window->frame_callback = NULL;

#ifdef GLAMOR_HAS_GBM
    if (xwl_window->xwl_screen->present) {
        struct xwl_present_window *xwl_present_window, *tmp;

        xorg_list_for_each_entry_safe(xwl_present_window, tmp,
                                      &xwl_window->frame_callback_list,
                                      frame_callback_list) {
            xwl_present_frame_callback(xwl_present_window);
        }
    }
#endif
}

static const struct wl_callback_listener frame_listener = {
    frame_callback
};

void
xwl_window_create_frame_callback(struct xwl_window *xwl_window)
{
    xwl_window->frame_callback = wl_surface_frame(xwl_window->surface);
    wl_callback_add_listener(xwl_window->frame_callback, &frame_listener,
                             xwl_window);
}

Bool
xwl_destroy_window(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    Bool ret;

#ifdef GLAMOR_HAS_GBM
    if (xwl_screen->present)
        xwl_present_cleanup(window);
#endif

    screen->DestroyWindow = xwl_screen->DestroyWindow;

    if (screen->DestroyWindow)
        ret = screen->DestroyWindow (window);
    else
        ret = TRUE;

    xwl_screen->DestroyWindow = screen->DestroyWindow;
    screen->DestroyWindow = xwl_destroy_window;

    return ret;
}

void xwl_surface_damage(struct xwl_screen *xwl_screen,
                        struct wl_surface *surface,
                        int32_t x, int32_t y, int32_t width, int32_t height)
{
    if (wl_surface_get_version(surface) >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION)
        wl_surface_damage_buffer(surface, x, y, width, height);
    else
        wl_surface_damage(surface, x, y, width, height);
}

void
xwl_window_post_damage(struct xwl_window *xwl_window)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;
    RegionPtr region;
    BoxPtr box;
    struct wl_buffer *buffer;
    PixmapPtr pixmap;
    int i;

    assert(!xwl_window->frame_callback);

    region = DamageRegion(window_get_damage(xwl_window->window));
    pixmap = xwl_window_buffers_get_pixmap(xwl_window, region);

#ifdef XWL_HAS_GLAMOR
    if (xwl_screen->glamor)
        buffer = xwl_glamor_pixmap_get_wl_buffer(pixmap,
                                                 NULL);
    else
#endif
        buffer = xwl_shm_pixmap_get_wl_buffer(pixmap);

#ifdef XWL_HAS_GLAMOR
    if (xwl_screen->glamor)
        xwl_glamor_post_damage(xwl_window, pixmap, region);
#endif

    wl_surface_attach(xwl_window->surface, buffer, 0, 0);

    /* Arbitrary limit to try to avoid flooding the Wayland
     * connection. If we flood it too much anyway, this could
     * abort in libwayland-client.
     */
    if (RegionNumRects(region) > 256) {
        box = RegionExtents(region);
        xwl_surface_damage(xwl_screen, xwl_window->surface,
                           box->x1 + xwl_window->window->borderWidth,
                           box->y1 + xwl_window->window->borderWidth,
                           box->x2 - box->x1, box->y2 - box->y1);
    } else {
        box = RegionRects(region);
        for (i = 0; i < RegionNumRects(region); i++, box++) {
            xwl_surface_damage(xwl_screen, xwl_window->surface,
                               box->x1 + xwl_window->window->borderWidth,
                               box->y1 + xwl_window->window->borderWidth,
                               box->x2 - box->x1, box->y2 - box->y1);
        }
    }

    xwl_window_create_frame_callback(xwl_window);
    DamageEmpty(window_get_damage(xwl_window->window));
}

Bool
xwl_window_init(void)
{
    if (!dixRegisterPrivateKey(&xwl_window_private_key, PRIVATE_WINDOW, 0))
        return FALSE;

    if (!dixRegisterPrivateKey(&xwl_damage_private_key, PRIVATE_WINDOW, 0))
        return FALSE;

    return TRUE;
}
