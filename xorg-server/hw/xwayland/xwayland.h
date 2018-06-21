/*
 * Copyright © 2014 Intel Corporation
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

#ifndef XWAYLAND_H
#define XWAYLAND_H

#include <xwayland-config.h>

#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <wayland-client.h>

#include <X11/X.h>

#include <fb.h>
#include <input.h>
#include <dix.h>
#include <randrstr.h>
#include <exevents.h>

#include "relative-pointer-unstable-v1-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "tablet-unstable-v2-client-protocol.h"
#include "xwayland-keyboard-grab-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"

struct xwl_format {
    uint32_t format;
    int num_modifiers;
    uint64_t *modifiers;
};

struct xwl_pixmap;
struct xwl_window;
struct xwl_screen;

struct xwl_egl_backend {
    /* Set by the backend if available */
    Bool is_available;

    /* Called once for each interface in the global registry. Backends
     * should use this to bind to any wayland interfaces they need.
     */
    Bool (*init_wl_registry)(struct xwl_screen *xwl_screen,
                             struct wl_registry *wl_registry,
                             uint32_t id, const char *name,
                             uint32_t version);

    /* Check that the required Wayland interfaces are available.
     */
    Bool (*has_wl_interfaces)(struct xwl_screen *xwl_screen);

    /* Called before glamor has been initialized. Backends should setup a
     * valid, glamor compatible EGL context in this hook.
     */
    Bool (*init_egl)(struct xwl_screen *xwl_screen);

    /* Called after glamor has been initialized, and after all of the
     * common Xwayland DDX hooks have been connected. Backends should use
     * this to setup any required wraps around X server callbacks like
     * CreatePixmap.
     */
     Bool (*init_screen)(struct xwl_screen *xwl_screen);

     /* Called by Xwayland to retrieve a pointer to a valid wl_buffer for
      * the given window/pixmap combo so that damage to the pixmap may be
      * displayed on-screen. Backends should use this to create a new
      * wl_buffer for a currently buffer-less pixmap, or simply return the
      * pixmap they've prepared beforehand.
      */
     struct wl_buffer *(*get_wl_buffer_for_pixmap)(PixmapPtr pixmap,
                                                   Bool *created);

     /* Called by Xwayland to perform any pre-wl_surface damage routines
      * that are required by the backend. If your backend is poorly
      * designed and lacks the ability to render directly to a surface,
      * you should implement blitting from the glamor pixmap to the wayland
      * pixmap here. Otherwise, this callback is optional.
      */
     void (*post_damage)(struct xwl_window *xwl_window,
                         PixmapPtr pixmap, RegionPtr region);

     /* Called by Xwayland to confirm with the egl backend that the given
      * pixmap is completely setup and ready for display on-screen. This
      * callback is optional.
      */
     Bool (*allow_commits)(struct xwl_window *xwl_window);
};

struct xwl_screen {
    int width;
    int height;
    int depth;
    ScreenPtr screen;
    int expecting_event;
    enum RootClipMode root_clip_mode;

    int wm_fd;
    int listen_fds[5];
    int listen_fd_count;
    int rootless;
    int glamor;
    int present;

    CreateScreenResourcesProcPtr CreateScreenResources;
    CloseScreenProcPtr CloseScreen;
    RealizeWindowProcPtr RealizeWindow;
    UnrealizeWindowProcPtr UnrealizeWindow;
    DestroyWindowProcPtr DestroyWindow;
    XYToWindowProcPtr XYToWindow;

    struct xorg_list output_list;
    struct xorg_list seat_list;
    struct xorg_list damage_window_list;

    int wayland_fd;
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_registry *input_registry;
    struct wl_compositor *compositor;
    struct zwp_tablet_manager_v2 *tablet_manager;
    struct wl_shm *shm;
    struct wl_shell *shell;
    struct zwp_relative_pointer_manager_v1 *relative_pointer_manager;
    struct zwp_pointer_constraints_v1 *pointer_constraints;
    struct zwp_xwayland_keyboard_grab_manager_v1 *wp_grab;
    struct zxdg_output_manager_v1 *xdg_output_manager;
    uint32_t serial;

#define XWL_FORMAT_ARGB8888 (1 << 0)
#define XWL_FORMAT_XRGB8888 (1 << 1)
#define XWL_FORMAT_RGB565   (1 << 2)

    int prepare_read;
    int wait_flush;

    uint32_t num_formats;
    struct xwl_format *formats;
    void *egl_display, *egl_context;

    struct xwl_egl_backend gbm_backend;
    struct xwl_egl_backend eglstream_backend;
    /* pointer to the current backend for creating pixmaps on wayland */
    struct xwl_egl_backend *egl_backend;

    struct glamor_context *glamor_ctx;

    Atom allow_commits_prop;
};

struct xwl_window {
    struct xwl_screen *xwl_screen;
    struct wl_surface *surface;
    struct wl_shell_surface *shell_surface;
    WindowPtr window;
    DamagePtr damage;
    struct xorg_list link_damage;
    struct wl_callback *frame_callback;
    Bool allow_commits;
    WindowPtr present_window;
};

#ifdef GLAMOR_HAS_GBM
struct xwl_present_window {
    struct xwl_screen *xwl_screen;
    WindowPtr window;
    struct xorg_list link;

    uint64_t msc;
    uint64_t ust;

    OsTimerPtr frame_timer;
    Bool frame_timer_firing;

    struct wl_callback *frame_callback;
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
#endif

#define MODIFIER_META 0x01

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
    uint32_t x;
    uint32_t y;
    uint32_t pressure;
    float tilt_x;
    float tilt_y;
    float rotation;
    float slider;

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

void xwl_sync_events (struct xwl_screen *xwl_screen);

Bool xwl_screen_init_cursor(struct xwl_screen *xwl_screen);

struct xwl_screen *xwl_screen_get(ScreenPtr screen);

void xwl_tablet_tool_set_cursor(struct xwl_tablet_tool *tool);
void xwl_seat_set_cursor(struct xwl_seat *xwl_seat);

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

Bool xwl_screen_init_output(struct xwl_screen *xwl_screen);

struct xwl_output *xwl_output_create(struct xwl_screen *xwl_screen,
                                     uint32_t id);

void xwl_output_destroy(struct xwl_output *xwl_output);

void xwl_output_remove(struct xwl_output *xwl_output);

RRModePtr xwayland_cvt(int HDisplay, int VDisplay,
                       float VRefresh, Bool Reduced, Bool Interlaced);

void xwl_pixmap_set_private(PixmapPtr pixmap, struct xwl_pixmap *xwl_pixmap);
struct xwl_pixmap *xwl_pixmap_get(PixmapPtr pixmap);

struct xwl_window *xwl_window_from_window(WindowPtr window);

Bool xwl_shm_create_screen_resources(ScreenPtr screen);
PixmapPtr xwl_shm_create_pixmap(ScreenPtr screen, int width, int height,
                                int depth, unsigned int hint);
Bool xwl_shm_destroy_pixmap(PixmapPtr pixmap);
struct wl_buffer *xwl_shm_pixmap_get_wl_buffer(PixmapPtr pixmap);

#ifdef XWL_HAS_GLAMOR
void xwl_glamor_init_backends(struct xwl_screen *xwl_screen,
                              Bool use_eglstream);
void xwl_glamor_select_backend(struct xwl_screen *xwl_screen,
                               Bool use_eglstream);
Bool xwl_glamor_init(struct xwl_screen *xwl_screen);

Bool xwl_screen_set_drm_interface(struct xwl_screen *xwl_screen,
                                  uint32_t id, uint32_t version);
Bool xwl_screen_set_dmabuf_interface(struct xwl_screen *xwl_screen,
                                     uint32_t id, uint32_t version);
struct wl_buffer *xwl_glamor_pixmap_get_wl_buffer(PixmapPtr pixmap,
                                                  Bool *created);
void xwl_glamor_init_wl_registry(struct xwl_screen *xwl_screen,
                                 struct wl_registry *registry,
                                 uint32_t id, const char *interface,
                                 uint32_t version);
Bool xwl_glamor_has_wl_interfaces(struct xwl_screen *xwl_screen,
                                 struct xwl_egl_backend *xwl_egl_backend);
void xwl_glamor_post_damage(struct xwl_window *xwl_window,
                            PixmapPtr pixmap, RegionPtr region);
Bool xwl_glamor_allow_commits(struct xwl_window *xwl_window);
void xwl_glamor_egl_make_current(struct xwl_screen *xwl_screen);

#ifdef GLAMOR_HAS_GBM
Bool xwl_present_init(ScreenPtr screen);
void xwl_present_cleanup(WindowPtr window);
#endif /* GLAMOR_HAS_GBM */

#ifdef XV
/* glamor Xv Adaptor */
Bool xwl_glamor_xv_init(ScreenPtr pScreen);
#endif /* XV */

#endif /* XWL_HAS_GLAMOR */

void xwl_screen_release_tablet_manager(struct xwl_screen *xwl_screen);

void xwl_screen_init_xdg_output(struct xwl_screen *xwl_screen);

#ifdef XF86VIDMODE
void xwlVidModeExtensionInit(void);
#endif

#ifdef GLAMOR_HAS_GBM
void xwl_glamor_init_gbm(struct xwl_screen *xwl_screen);
#else
static inline void xwl_glamor_init_gbm(struct xwl_screen *xwl_screen)
{
}
#endif

#ifdef XWL_HAS_EGLSTREAM
void xwl_glamor_init_eglstream(struct xwl_screen *xwl_screen);
#else
static inline void xwl_glamor_init_eglstream(struct xwl_screen *xwl_screen)
{
}
#endif

#endif
