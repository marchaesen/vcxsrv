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

#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "drm-client-protocol.h"
#include <drm_fourcc.h>

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
    EGLContext ctx = xwl_screen->glamor_ctx->ctx;
    
    if (lastGLContext == ctx)
        return;

    lastGLContext = ctx;
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

Bool
xwl_glamor_check_flip(PixmapPtr pixmap)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(pixmap->drawable.pScreen);

    if (!xwl_glamor_pixmap_get_wl_buffer(pixmap))
        return FALSE;

    if (xwl_screen->egl_backend->check_flip)
        return xwl_screen->egl_backend->check_flip(pixmap);

    return TRUE;
}

Bool
xwl_glamor_is_modifier_supported(struct xwl_screen *xwl_screen,
                                 uint32_t format, uint64_t modifier)
{
    struct xwl_format *xwl_format = NULL;
    int i;

    for (i = 0; i < xwl_screen->num_formats; i++) {
        if (xwl_screen->formats[i].format == format) {
            xwl_format = &xwl_screen->formats[i];
            break;
        }
    }

    if (xwl_format) {
        for (i = 0; i < xwl_format->num_modifiers; i++) {
            if (xwl_format->modifiers[i] == modifier) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

uint32_t
wl_drm_format_for_depth(int depth)
{
    switch (depth) {
    case 15:
        return WL_DRM_FORMAT_XRGB1555;
    case 16:
        return WL_DRM_FORMAT_RGB565;
    case 24:
        return WL_DRM_FORMAT_XRGB8888;
    case 30:
        return WL_DRM_FORMAT_ARGB2101010;
    default:
        ErrorF("unexpected depth: %d\n", depth);
    case 32:
        return WL_DRM_FORMAT_ARGB8888;
    }
}

Bool
xwl_glamor_get_formats(ScreenPtr screen,
                       CARD32 *num_formats, CARD32 **formats)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    int i;

    /* Explicitly zero the count as the caller may ignore the return value */
    *num_formats = 0;

    if (!xwl_screen->dmabuf)
        return FALSE;

    if (xwl_screen->num_formats == 0)
       return TRUE;

    *formats = calloc(xwl_screen->num_formats, sizeof(CARD32));
    if (*formats == NULL)
        return FALSE;

    for (i = 0; i < xwl_screen->num_formats; i++)
       (*formats)[i] = xwl_screen->formats[i].format;
    *num_formats = xwl_screen->num_formats;

    return TRUE;
}

Bool
xwl_glamor_get_modifiers(ScreenPtr screen, uint32_t format,
                         uint32_t *num_modifiers, uint64_t **modifiers)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct xwl_format *xwl_format = NULL;
    int i;

    /* Explicitly zero the count as the caller may ignore the return value */
    *num_modifiers = 0;

    if (!xwl_screen->dmabuf)
        return FALSE;

    if (xwl_screen->num_formats == 0)
       return TRUE;

    for (i = 0; i < xwl_screen->num_formats; i++) {
       if (xwl_screen->formats[i].format == format) {
          xwl_format = &xwl_screen->formats[i];
          break;
       }
    }

    if (!xwl_format ||
        (xwl_format->num_modifiers == 1 &&
         xwl_format->modifiers[0] == DRM_FORMAT_MOD_INVALID))
        return FALSE;

    *modifiers = calloc(xwl_format->num_modifiers, sizeof(uint64_t));
    if (*modifiers == NULL)
        return FALSE;

    for (i = 0; i < xwl_format->num_modifiers; i++)
       (*modifiers)[i] = xwl_format->modifiers[i];
    *num_modifiers = xwl_format->num_modifiers;

    return TRUE;
}

static void
xwl_dmabuf_handle_format(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
                         uint32_t format)
{
}

static void
xwl_dmabuf_handle_modifier(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
                           uint32_t format, uint32_t modifier_hi,
                           uint32_t modifier_lo)
{
    struct xwl_screen *xwl_screen = data;
    struct xwl_format *xwl_format = NULL;
    int i;

    for (i = 0; i < xwl_screen->num_formats; i++) {
        if (xwl_screen->formats[i].format == format) {
            xwl_format = &xwl_screen->formats[i];
            break;
        }
    }

    if (xwl_format == NULL) {
        xwl_screen->num_formats++;
        xwl_screen->formats = realloc(xwl_screen->formats,
                                      xwl_screen->num_formats * sizeof(*xwl_format));
        if (!xwl_screen->formats)
            return;
        xwl_format = &xwl_screen->formats[xwl_screen->num_formats - 1];
        xwl_format->format = format;
        xwl_format->num_modifiers = 0;
        xwl_format->modifiers = NULL;
    }

    xwl_format->num_modifiers++;
    xwl_format->modifiers = realloc(xwl_format->modifiers,
                                    xwl_format->num_modifiers * sizeof(uint64_t));
    if (!xwl_format->modifiers)
        return;
    xwl_format->modifiers[xwl_format->num_modifiers - 1]  = (uint64_t) modifier_lo;
    xwl_format->modifiers[xwl_format->num_modifiers - 1] |= (uint64_t) modifier_hi << 32;
}

static const struct zwp_linux_dmabuf_v1_listener xwl_dmabuf_listener = {
    .format = xwl_dmabuf_handle_format,
    .modifier = xwl_dmabuf_handle_modifier
};

Bool
xwl_screen_set_dmabuf_interface(struct xwl_screen *xwl_screen,
                                uint32_t id, uint32_t version)
{
    if (version < 3)
        return FALSE;

    xwl_screen->dmabuf =
        wl_registry_bind(xwl_screen->registry, id, &zwp_linux_dmabuf_v1_interface, 3);
    zwp_linux_dmabuf_v1_add_listener(xwl_screen->dmabuf, &xwl_dmabuf_listener, xwl_screen);

    return TRUE;
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
xwl_glamor_pixmap_get_wl_buffer(PixmapPtr pixmap)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(pixmap->drawable.pScreen);

    if (xwl_screen->egl_backend->get_wl_buffer_for_pixmap)
        return xwl_screen->egl_backend->get_wl_buffer_for_pixmap(pixmap);

    return NULL;
}

Bool
xwl_glamor_post_damage(struct xwl_window *xwl_window,
                       PixmapPtr pixmap, RegionPtr region)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;

    if (xwl_screen->egl_backend->post_damage)
        return xwl_screen->egl_backend->post_damage(xwl_window, pixmap, region);

    return TRUE;
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

Bool
xwl_glamor_needs_buffer_flush(struct xwl_screen *xwl_screen)
{
    if (!xwl_screen->glamor || !xwl_screen->egl_backend)
        return FALSE;

    return (xwl_screen->egl_backend->backend_flags &
                XWL_EGL_BACKEND_NEEDS_BUFFER_FLUSH);
}

Bool
xwl_glamor_needs_n_buffering(struct xwl_screen *xwl_screen)
{
    /* wl_shm benefits from n-buffering */
    if (!xwl_screen->glamor || !xwl_screen->egl_backend)
        return TRUE;

    return (xwl_screen->egl_backend->backend_flags &
                XWL_EGL_BACKEND_NEEDS_N_BUFFERING);
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
        LogMessageVerb(X_INFO, 3, "glamor: Using GBM backend\n");
        return TRUE;
    }
    else
        LogMessageVerb(X_INFO, 3,
                       "Missing Wayland requirements for glamor GBM backend\n");
#endif

    return FALSE;
}

static Bool
xwl_glamor_select_eglstream_backend(struct xwl_screen *xwl_screen)
{
#ifdef XWL_HAS_EGLSTREAM
    if (xwl_screen->eglstream_backend.is_available &&
        xwl_glamor_has_wl_interfaces(xwl_screen, &xwl_screen->eglstream_backend)) {
        xwl_screen->egl_backend = &xwl_screen->eglstream_backend;
        LogMessageVerb(X_INFO, 3, "glamor: Using EGLStream backend\n");
        return TRUE;
    }
    else
        LogMessageVerb(X_INFO, 3,
                       "Missing Wayland requirements for glamor EGLStream backend\n");
#endif

    return FALSE;
}

void
xwl_glamor_select_backend(struct xwl_screen *xwl_screen, Bool use_eglstream)
{
    if (!xwl_glamor_select_eglstream_backend(xwl_screen)) {
        if (!use_eglstream)
            xwl_glamor_select_gbm_backend(xwl_screen);
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
