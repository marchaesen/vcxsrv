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

#ifndef XWAYLAND_GLAMOR_H
#define XWAYLAND_GLAMOR_H

#include <xwayland-config.h>

#include <wayland-client.h>

#include "xwayland-types.h"

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

#ifdef XV
/* glamor Xv Adaptor */
Bool xwl_glamor_xv_init(ScreenPtr pScreen);
#endif /* XV */

#endif /* XWL_HAS_GLAMOR */

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

#endif /* XWAYLAND_GLAMOR_H */
