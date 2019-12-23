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

#define MESA_EGL_NO_X11_HEADERS
#define EGL_NO_X11
#include <glamor_egl.h>

#include <glamor.h>
#include <glamor_context.h>
#ifdef GLXEXT
#include "glx_extinit.h"
#endif

#include "xwayland-glamor.h"
#include "xwayland-glx.h"
#include "xwayland-screen.h"
#include "xwayland-window.h"

static void
glamor_egl_make_current(struct glamor_context *glamor_ctx)
{
    eglMakeCurrent(glamor_ctx->display, EGL_NO_SURFACE,
                   EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (!eglMakeCurrent(glamor_ctx->display,
                        EGL_NO_SURFACE, EGL_NO_SURFACE,
                        glamor_ctx->ctx))
        FatalError("Failed to make EGL context current\n");
}

void
xwl_glamor_egl_make_current(struct xwl_screen *xwl_screen)
{
    if (lastGLContext == xwl_screen->glamor_ctx)
        return;

    lastGLContext = xwl_screen->glamor_ctx;
    xwl_screen->glamor_ctx->make_current(xwl_screen->glamor_ctx);
}

void
glamor_egl_screen_init(ScreenPtr screen, struct glamor_context *glamor_ctx)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);

    glamor_enable_dri3(screen);
    glamor_ctx->ctx = xwl_screen->egl_context;
    glamor_ctx->display = xwl_screen->egl_display;

    glamor_ctx->make_current = glamor_egl_make_current;

    xwl_screen->glamor_ctx = glamor_ctx;
}

void
xwl_glamor_init_wl_registry(struct xwl_screen *xwl_screen,
                            struct wl_registry *registry,
                            uint32_t id, const char *interface,
                            uint32_t version)
{
    if (xwl_screen->gbm_backend.is_available &&
        xwl_screen->gbm_backend.init_wl_registry(xwl_screen,
                                                 registry,
                                                 id,
                                                 interface,
                                                 version)) {
        /* no-op */
    } else if (xwl_screen->eglstream_backend.is_available &&
             xwl_screen->eglstream_backend.init_wl_registry(xwl_screen,
                                                            registry,
                                                            id,
                                                            interface,
                                                            version)) {
        /* no-op */
    }
}

Bool
xwl_glamor_has_wl_interfaces(struct xwl_screen *xwl_screen,
                            struct xwl_egl_backend *xwl_egl_backend)
{
    return xwl_egl_backend->has_wl_interfaces(xwl_screen);
}

struct wl_buffer *
xwl_glamor_pixmap_get_wl_buffer(PixmapPtr pixmap,
                                Bool *created)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(pixmap->drawable.pScreen);

    if (xwl_screen->egl_backend->get_wl_buffer_for_pixmap)
        return xwl_screen->egl_backend->get_wl_buffer_for_pixmap(pixmap,
                                                                 created);

    return NULL;
}

void
xwl_glamor_post_damage(struct xwl_window *xwl_window,
                       PixmapPtr pixmap, RegionPtr region)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;

    if (xwl_screen->egl_backend->post_damage)
        xwl_screen->egl_backend->post_damage(xwl_window, pixmap, region);
}

Bool
xwl_glamor_allow_commits(struct xwl_window *xwl_window)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;

    if (xwl_screen->egl_backend->allow_commits)
        return xwl_screen->egl_backend->allow_commits(xwl_window);
    else
        return TRUE;
}

static Bool
xwl_glamor_create_screen_resources(ScreenPtr screen)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    int ret;

    screen->CreateScreenResources = xwl_screen->CreateScreenResources;
    ret = (*screen->CreateScreenResources) (screen);
    xwl_screen->CreateScreenResources = screen->CreateScreenResources;
    screen->CreateScreenResources = xwl_glamor_create_screen_resources;

    if (!ret)
        return ret;

    if (xwl_screen->rootless) {
        screen->devPrivate =
            fbCreatePixmap(screen, 0, 0, screen->rootDepth, 0);
    }
    else {
        screen->devPrivate = screen->CreatePixmap(
            screen, screen->width, screen->height, screen->rootDepth,
            CREATE_PIXMAP_USAGE_BACKING_PIXMAP);
    }

    SetRootClip(screen, xwl_screen->root_clip_mode);

    return screen->devPrivate != NULL;
}

int
glamor_egl_fd_name_from_pixmap(ScreenPtr screen,
                               PixmapPtr pixmap,
                               CARD16 *stride, CARD32 *size)
{
    return 0;
}

void
xwl_glamor_init_backends(struct xwl_screen *xwl_screen, Bool use_eglstream)
{
#ifdef GLAMOR_HAS_GBM
    xwl_glamor_init_gbm(xwl_screen);
    if (!xwl_screen->gbm_backend.is_available && !use_eglstream)
        ErrorF("xwayland glamor: GBM backend (default) is not available\n");
#endif
#ifdef XWL_HAS_EGLSTREAM
    xwl_glamor_init_eglstream(xwl_screen);
    if (!xwl_screen->eglstream_backend.is_available && use_eglstream)
        ErrorF("xwayland glamor: EGLStream backend requested but not available\n");
#endif
}

static Bool
xwl_glamor_select_gbm_backend(struct xwl_screen *xwl_screen)
{
#ifdef GLAMOR_HAS_GBM
    if (xwl_screen->gbm_backend.is_available &&
        xwl_glamor_has_wl_interfaces(xwl_screen, &xwl_screen->gbm_backend)) {
        xwl_screen->egl_backend = &xwl_screen->gbm_backend;
        return TRUE;
    }
    else
        ErrorF("Missing Wayland requirements for glamor GBM backend\n");
#endif

    return FALSE;
}

static Bool
xwl_glamor_select_eglstream_backend(struct xwl_screen *xwl_screen)
{
#ifdef XWL_HAS_EGLSTREAM
    if (xwl_screen->eglstream_backend.is_available &&
        xwl_glamor_has_wl_interfaces(xwl_screen, &xwl_screen->eglstream_backend)) {
        ErrorF("glamor: Using nvidia's EGLStream interface, direct rendering impossible.\n");
        ErrorF("glamor: Performance may be affected. Ask your vendor to support GBM!\n");
        xwl_screen->egl_backend = &xwl_screen->eglstream_backend;
        return TRUE;
    }
    else
        ErrorF("Missing Wayland requirements for glamor EGLStream backend\n");
#endif

    return FALSE;
}

void
xwl_glamor_select_backend(struct xwl_screen *xwl_screen, Bool use_eglstream)
{
    if (use_eglstream) {
        if (!xwl_glamor_select_eglstream_backend(xwl_screen))
            xwl_glamor_select_gbm_backend(xwl_screen);
    }
    else {
        if (!xwl_glamor_select_gbm_backend(xwl_screen))
            xwl_glamor_select_eglstream_backend(xwl_screen);
    }
}

Bool
xwl_glamor_init(struct xwl_screen *xwl_screen)
{
    ScreenPtr screen = xwl_screen->screen;
    const char *no_glamor_env;

    no_glamor_env = getenv("XWAYLAND_NO_GLAMOR");
    if (no_glamor_env && *no_glamor_env != '0') {
        ErrorF("Disabling glamor and dri3 support, XWAYLAND_NO_GLAMOR is set\n");
        return FALSE;
    }

    if (!xwl_screen->egl_backend->init_egl(xwl_screen)) {
        ErrorF("EGL setup failed, disabling glamor\n");
        return FALSE;
    }

    if (!glamor_init(xwl_screen->screen, GLAMOR_USE_EGL_SCREEN)) {
        ErrorF("Failed to initialize glamor\n");
        return FALSE;
    }

    if (!xwl_screen->egl_backend->init_screen(xwl_screen)) {
        ErrorF("EGL backend init_screen() failed, disabling glamor\n");
        return FALSE;
    }

    xwl_screen->CreateScreenResources = screen->CreateScreenResources;
    screen->CreateScreenResources = xwl_glamor_create_screen_resources;

#ifdef XV
    if (!xwl_glamor_xv_init(screen))
        ErrorF("Failed to initialize glamor Xv extension\n");
#endif

#ifdef GLXEXT
    GlxPushProvider(&glamor_provider);
#endif

    return TRUE;
}
