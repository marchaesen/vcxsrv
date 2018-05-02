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

#define MESA_EGL_NO_X11_HEADERS
#include <glamor_egl.h>

#include <glamor.h>
#include <glamor_context.h>

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

Bool
xwl_glamor_egl_supports_device_probing(void)
{
    return epoxy_has_egl_extension(NULL, "EGL_EXT_device_base");
}

void **
xwl_glamor_egl_get_devices(int *num_devices)
{
#ifdef XWL_HAS_EGLSTREAM
    EGLDeviceEXT *devices;
    Bool ret;
    int drm_dev_count = 0;
    int i;

    /* Get the number of devices */
    ret = eglQueryDevicesEXT(0, NULL, num_devices);
    if (!ret || *num_devices < 1)
        return NULL;

    devices = calloc(*num_devices, sizeof(EGLDeviceEXT));
    if (!devices)
        return NULL;

    ret = eglQueryDevicesEXT(*num_devices, devices, num_devices);
    if (!ret)
        goto error;

    /* We're only ever going to care about devices that support
     * EGL_EXT_device_drm, so filter out the ones that don't
     */
    for (i = 0; i < *num_devices; i++) {
        const char *extension_str =
            eglQueryDeviceStringEXT(devices[i], EGL_EXTENSIONS);

        if (!epoxy_extension_in_string(extension_str, "EGL_EXT_device_drm"))
            continue;

        devices[drm_dev_count++] = devices[i];
    }
    if (!drm_dev_count)
        goto error;

    *num_devices = drm_dev_count;
    devices = realloc(devices, sizeof(EGLDeviceEXT) * drm_dev_count);

    return devices;

error:
    free(devices);
#endif
    return NULL;
}

Bool
xwl_glamor_egl_device_has_egl_extensions(void *device,
                                         const char **ext_list, size_t size)
{
    EGLDisplay egl_display;
    int i;
    Bool has_exts = TRUE;

    egl_display = glamor_egl_get_display(EGL_PLATFORM_DEVICE_EXT, device);
    if (!egl_display || !eglInitialize(egl_display, NULL, NULL))
        return FALSE;

    for (i = 0; i < size; i++) {
        if (!epoxy_has_egl_extension(egl_display, ext_list[i])) {
            has_exts = FALSE;
            break;
        }
    }

    eglTerminate(egl_display);
    return has_exts;
}

void
glamor_egl_screen_init(ScreenPtr screen, struct glamor_context *glamor_ctx)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);

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
    if (xwl_screen->egl_backend.init_wl_registry)
        xwl_screen->egl_backend.init_wl_registry(xwl_screen, registry,
                                                 interface, id, version);
}

struct wl_buffer *
xwl_glamor_pixmap_get_wl_buffer(PixmapPtr pixmap,
                                unsigned short width,
                                unsigned short height,
                                Bool *created)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(pixmap->drawable.pScreen);

    if (xwl_screen->egl_backend.get_wl_buffer_for_pixmap)
        return xwl_screen->egl_backend.get_wl_buffer_for_pixmap(pixmap,
                                                                width,
                                                                height,
                                                                created);

    return NULL;
}

void
xwl_glamor_post_damage(struct xwl_window *xwl_window,
                       PixmapPtr pixmap, RegionPtr region)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;

    if (xwl_screen->egl_backend.post_damage)
        xwl_screen->egl_backend.post_damage(xwl_window, pixmap, region);
}

Bool
xwl_glamor_allow_commits(struct xwl_window *xwl_window)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;

    if (xwl_screen->egl_backend.allow_commits)
        return xwl_screen->egl_backend.allow_commits(xwl_window);
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
xwl_glamor_init(struct xwl_screen *xwl_screen)
{
    ScreenPtr screen = xwl_screen->screen;
    const char *no_glamor_env;

    no_glamor_env = getenv("XWAYLAND_NO_GLAMOR");
    if (no_glamor_env && *no_glamor_env != '0') {
        ErrorF("Disabling glamor and dri3 support, XWAYLAND_NO_GLAMOR is set\n");
        return FALSE;
    }

    if (!xwl_screen->egl_backend.init_egl(xwl_screen)) {
        ErrorF("EGL setup failed, disabling glamor\n");
        return FALSE;
    }

    if (!glamor_init(xwl_screen->screen, GLAMOR_USE_EGL_SCREEN)) {
        ErrorF("Failed to initialize glamor\n");
        return FALSE;
    }

    if (!xwl_screen->egl_backend.init_screen(xwl_screen)) {
        ErrorF("EGL backend init_screen() failed, disabling glamor\n");
        return FALSE;
    }

    xwl_screen->CreateScreenResources = screen->CreateScreenResources;
    screen->CreateScreenResources = xwl_glamor_create_screen_resources;

#ifdef XV
    if (!xwl_glamor_xv_init(screen))
        ErrorF("Failed to initialize glamor Xv extension\n");
#endif

    return TRUE;
}
