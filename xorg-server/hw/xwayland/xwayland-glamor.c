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

#include <fcntl.h>
#include <sys/stat.h>
#include <xf86drm.h>

#define MESA_EGL_NO_X11_HEADERS
#include <gbm.h>
#include <epoxy/egl.h>
#include <epoxy/gl.h>

#include <glamor.h>
#include <glamor_context.h>
#include <dri3.h>
#include "drm-client-protocol.h"

struct xwl_pixmap {
    struct wl_buffer *buffer;
    struct gbm_bo *bo;
    void *image;
    unsigned int texture;
};

static void
xwl_glamor_egl_make_current(struct glamor_context *glamor_ctx)
{
    eglMakeCurrent(glamor_ctx->display, EGL_NO_SURFACE,
                   EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (!eglMakeCurrent(glamor_ctx->display,
                        EGL_NO_SURFACE, EGL_NO_SURFACE,
                        glamor_ctx->ctx))
        FatalError("Failed to make EGL context current\n");
}

static uint32_t
drm_format_for_depth(int depth)
{
    switch (depth) {
    case 15:
        return WL_DRM_FORMAT_XRGB1555;
    case 16:
        return WL_DRM_FORMAT_RGB565;
    case 24:
        return WL_DRM_FORMAT_XRGB8888;
    default:
        ErrorF("unexpected depth: %d\n", depth);
    case 32:
        return WL_DRM_FORMAT_ARGB8888;
    }
}

static uint32_t
gbm_format_for_depth(int depth)
{
    switch (depth) {
    case 16:
        return GBM_FORMAT_RGB565;
    case 24:
        return GBM_FORMAT_XRGB8888;
    default:
        ErrorF("unexpected depth: %d\n", depth);
    case 32:
        return GBM_FORMAT_ARGB8888;
    }
}

void
glamor_egl_screen_init(ScreenPtr screen, struct glamor_context *glamor_ctx)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);

    glamor_ctx->ctx = xwl_screen->egl_context;
    glamor_ctx->display = xwl_screen->egl_display;

    glamor_ctx->make_current = xwl_glamor_egl_make_current;

    xwl_screen->glamor_ctx = glamor_ctx;
}

static PixmapPtr
xwl_glamor_create_pixmap_for_bo(ScreenPtr screen, struct gbm_bo *bo, int depth)
{
    PixmapPtr pixmap;
    struct xwl_pixmap *xwl_pixmap;
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);

    xwl_pixmap = malloc(sizeof *xwl_pixmap);
    if (xwl_pixmap == NULL)
        return NULL;

    pixmap = glamor_create_pixmap(screen,
                                  gbm_bo_get_width(bo),
                                  gbm_bo_get_height(bo),
                                  depth,
                                  GLAMOR_CREATE_PIXMAP_NO_TEXTURE);
    if (pixmap == NULL) {
        free(xwl_pixmap);
        return NULL;
    }

    if (lastGLContext != xwl_screen->glamor_ctx) {
        lastGLContext = xwl_screen->glamor_ctx;
        xwl_glamor_egl_make_current(xwl_screen->glamor_ctx);
    }

    xwl_pixmap->bo = bo;
    xwl_pixmap->buffer = NULL;
    xwl_pixmap->image = eglCreateImageKHR(xwl_screen->egl_display,
                                          xwl_screen->egl_context,
                                          EGL_NATIVE_PIXMAP_KHR,
                                          xwl_pixmap->bo, NULL);

    glGenTextures(1, &xwl_pixmap->texture);
    glBindTexture(GL_TEXTURE_2D, xwl_pixmap->texture);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, xwl_pixmap->image);
    glBindTexture(GL_TEXTURE_2D, 0);

    xwl_pixmap_set_private(pixmap, xwl_pixmap);

    glamor_set_pixmap_texture(pixmap, xwl_pixmap->texture);
    glamor_set_pixmap_type(pixmap, GLAMOR_TEXTURE_DRM);

    return pixmap;
}

struct wl_buffer *
xwl_glamor_pixmap_get_wl_buffer(PixmapPtr pixmap)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(pixmap->drawable.pScreen);
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);
    int prime_fd;

    if (xwl_pixmap->buffer)
        return xwl_pixmap->buffer;

    prime_fd = gbm_bo_get_fd(xwl_pixmap->bo);
    if (prime_fd == -1)
        return NULL;

    xwl_pixmap->buffer =
        wl_drm_create_prime_buffer(xwl_screen->drm, prime_fd,
                                   pixmap->drawable.width,
                                   pixmap->drawable.height,
                                   drm_format_for_depth(pixmap->drawable.depth),
                                   0, gbm_bo_get_stride(xwl_pixmap->bo),
                                   0, 0,
                                   0, 0);

    close(prime_fd);

    return xwl_pixmap->buffer;
}

static PixmapPtr
xwl_glamor_create_pixmap(ScreenPtr screen,
                         int width, int height, int depth, unsigned int hint)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct gbm_bo *bo;

    if (width > 0 && height > 0 && depth >= 15 &&
        (hint == 0 ||
         hint == CREATE_PIXMAP_USAGE_BACKING_PIXMAP ||
         hint == CREATE_PIXMAP_USAGE_SHARED)) {
        bo = gbm_bo_create(xwl_screen->gbm, width, height,
                           gbm_format_for_depth(depth),
                           GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

        if (bo)
            return xwl_glamor_create_pixmap_for_bo(screen, bo, depth);
    }

    return glamor_create_pixmap(screen, width, height, depth, hint);
}

static Bool
xwl_glamor_destroy_pixmap(PixmapPtr pixmap)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(pixmap->drawable.pScreen);
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);

    if (xwl_pixmap && pixmap->refcnt == 1) {
        if (xwl_pixmap->buffer)
            wl_buffer_destroy(xwl_pixmap->buffer);

        eglDestroyImageKHR(xwl_screen->egl_display, xwl_pixmap->image);
        gbm_bo_destroy(xwl_pixmap->bo);
        free(xwl_pixmap);
    }

    return glamor_destroy_pixmap(pixmap);
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

    if (xwl_screen->rootless)
        screen->devPrivate =
            fbCreatePixmap(screen, 0, 0, screen->rootDepth, 0);
    else {
        screen->devPrivate =
            xwl_glamor_create_pixmap(screen, screen->width, screen->height,
                                     screen->rootDepth,
                                     CREATE_PIXMAP_USAGE_BACKING_PIXMAP);
        if (screen->devPrivate)
            glamor_set_screen_pixmap(screen->devPrivate, NULL);
    }

    return screen->devPrivate != NULL;
}

static char
is_fd_render_node(int fd)
{
    struct stat render;

    if (fstat(fd, &render))
        return 0;
    if (!S_ISCHR(render.st_mode))
        return 0;
    if (render.st_rdev & 0x80)
        return 1;

    return 0;
}

static void
xwl_drm_init_egl(struct xwl_screen *xwl_screen)
{
    EGLint major, minor;
    const char *version;

    if (xwl_screen->egl_display)
        return;

    xwl_screen->expecting_event--;

    xwl_screen->gbm = gbm_create_device(xwl_screen->drm_fd);
    if (xwl_screen->gbm == NULL) {
        ErrorF("couldn't get display device\n");
        return;
    }

    xwl_screen->egl_display = eglGetDisplay(xwl_screen->gbm);
    if (xwl_screen->egl_display == EGL_NO_DISPLAY) {
        ErrorF("eglGetDisplay() failed\n");
        return;
    }

    eglBindAPI(EGL_OPENGL_API);
    if (!eglInitialize(xwl_screen->egl_display, &major, &minor)) {
        ErrorF("eglInitialize() failed\n");
        return;
    }

    version = eglQueryString(xwl_screen->egl_display, EGL_VERSION);
    ErrorF("glamor: EGL version %s:\n", version);

    xwl_screen->egl_context = eglCreateContext(xwl_screen->egl_display,
                                               NULL, EGL_NO_CONTEXT, NULL);
    if (xwl_screen->egl_context == EGL_NO_CONTEXT) {
        ErrorF("Failed to create EGL context\n");
        return;
    }

    if (!eglMakeCurrent(xwl_screen->egl_display,
                        EGL_NO_SURFACE, EGL_NO_SURFACE,
                        xwl_screen->egl_context)) {
        ErrorF("Failed to make EGL context current\n");
        return;
    }

    if (!epoxy_has_gl_extension("GL_OES_EGL_image")) {
        ErrorF("GL_OES_EGL_image no available");
        return;
    }

    return;
}

static void
xwl_drm_handle_device(void *data, struct wl_drm *drm, const char *device)
{
   struct xwl_screen *xwl_screen = data;
   drm_magic_t magic;

   xwl_screen->device_name = strdup(device);
   if (!xwl_screen->device_name)
      return;

   xwl_screen->drm_fd = open(xwl_screen->device_name, O_RDWR | O_CLOEXEC);
   if (xwl_screen->drm_fd == -1) {
       ErrorF("wayland-egl: could not open %s (%s)",
	      xwl_screen->device_name, strerror(errno));
       return;
   }

   if (is_fd_render_node(xwl_screen->drm_fd)) {
       xwl_screen->fd_render_node = 1;
       xwl_drm_init_egl(xwl_screen);
   } else {
       drmGetMagic(xwl_screen->drm_fd, &magic);
       wl_drm_authenticate(xwl_screen->drm, magic);
   }
}

static void
xwl_drm_handle_format(void *data, struct wl_drm *drm, uint32_t format)
{
   struct xwl_screen *xwl_screen = data;

   switch (format) {
   case WL_DRM_FORMAT_ARGB8888:
      xwl_screen->formats |= XWL_FORMAT_ARGB8888;
      break;
   case WL_DRM_FORMAT_XRGB8888:
      xwl_screen->formats |= XWL_FORMAT_XRGB8888;
      break;
   case WL_DRM_FORMAT_RGB565:
      xwl_screen->formats |= XWL_FORMAT_RGB565;
      break;
   }
}

static void
xwl_drm_handle_authenticated(void *data, struct wl_drm *drm)
{
    struct xwl_screen *xwl_screen = data;

    if (!xwl_screen->egl_display)
        xwl_drm_init_egl(xwl_screen);
}

static void
xwl_drm_handle_capabilities(void *data, struct wl_drm *drm, uint32_t value)
{
   struct xwl_screen *xwl_screen = data;

   xwl_screen->capabilities = value;
}

static const struct wl_drm_listener xwl_drm_listener = {
    xwl_drm_handle_device,
    xwl_drm_handle_format,
    xwl_drm_handle_authenticated,
    xwl_drm_handle_capabilities
};

Bool
xwl_screen_init_glamor(struct xwl_screen *xwl_screen,
                       uint32_t id, uint32_t version)
{
    if (version < 2)
        return FALSE;

    xwl_screen->drm =
        wl_registry_bind(xwl_screen->registry, id, &wl_drm_interface, 2);
    wl_drm_add_listener(xwl_screen->drm, &xwl_drm_listener, xwl_screen);
    xwl_screen->expecting_event++;

    return TRUE;
}

void
glamor_egl_destroy_textured_pixmap(PixmapPtr pixmap)
{
    glamor_destroy_textured_pixmap(pixmap);
}

int
glamor_egl_dri3_fd_name_from_tex(ScreenPtr screen,
                                 PixmapPtr pixmap,
                                 unsigned int tex,
                                 Bool want_name, CARD16 *stride, CARD32 *size)
{
    return 0;
}

unsigned int
glamor_egl_create_argb8888_based_texture(ScreenPtr screen, int w, int h)
{
    return 0;
}

struct xwl_auth_state {
    int fd;
    ClientPtr client;
};

static void
sync_callback(void *data, struct wl_callback *callback, uint32_t serial)
{
    struct xwl_auth_state *state = data;

    dri3_send_open_reply(state->client, state->fd);
    AttendClient(state->client);
    free(state);
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener sync_listener = {
   sync_callback
};

static int
xwl_dri3_open_client(ClientPtr client,
                     ScreenPtr screen,
                     RRProviderPtr provider,
                     int *pfd)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct xwl_auth_state *state;
    struct wl_callback *callback;
    drm_magic_t magic;
    int fd;

    fd = open(xwl_screen->device_name, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return BadAlloc;
    if (xwl_screen->fd_render_node) {
        *pfd = fd;
        return Success;
    }

    state = malloc(sizeof *state);
    if (state == NULL) {
        close(fd);
        return BadAlloc;
    }

    state->client = client;
    state->fd = fd;

    if (drmGetMagic(state->fd, &magic) < 0) {
        close(state->fd);
        free(state);
        return BadMatch;
    }

    wl_drm_authenticate(xwl_screen->drm, magic);
    callback = wl_display_sync(xwl_screen->display);
    wl_callback_add_listener(callback, &sync_listener, state);

    IgnoreClient(client);

    return Success;
}

static PixmapPtr
xwl_dri3_pixmap_from_fd(ScreenPtr screen, int fd,
                        CARD16 width, CARD16 height, CARD16 stride,
                        CARD8 depth, CARD8 bpp)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct gbm_import_fd_data data;
    struct gbm_bo *bo;
    PixmapPtr pixmap;

    if (width == 0 || height == 0 ||
        depth < 15 || bpp != BitsPerPixel(depth) || stride < width * bpp / 8)
        return NULL;

    data.fd = fd;
    data.width = width;
    data.height = height;
    data.stride = stride;
    data.format = gbm_format_for_depth(depth);
    bo = gbm_bo_import(xwl_screen->gbm, GBM_BO_IMPORT_FD, &data,
                       GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (bo == NULL)
        return NULL;

    pixmap = xwl_glamor_create_pixmap_for_bo(screen, bo, depth);
    if (pixmap == NULL) {
        gbm_bo_destroy(bo);
        return NULL;
    }

    return pixmap;
}

static int
xwl_dri3_fd_from_pixmap(ScreenPtr screen, PixmapPtr pixmap,
                        CARD16 *stride, CARD32 *size)
{
    struct xwl_pixmap *xwl_pixmap;

    xwl_pixmap = xwl_pixmap_get(pixmap);

    *stride = gbm_bo_get_stride(xwl_pixmap->bo);
    *size = pixmap->drawable.width * *stride;

    return gbm_bo_get_fd(xwl_pixmap->bo);
}

static dri3_screen_info_rec xwl_dri3_info = {
    .version = 1,
    .open = NULL,
    .pixmap_from_fd = xwl_dri3_pixmap_from_fd,
    .fd_from_pixmap = xwl_dri3_fd_from_pixmap,
    .open_client = xwl_dri3_open_client,
};

Bool
xwl_glamor_init(struct xwl_screen *xwl_screen)
{
    ScreenPtr screen = xwl_screen->screen;

    if (xwl_screen->egl_context == EGL_NO_CONTEXT) {
        ErrorF("Disabling glamor and dri3, EGL setup failed\n");
        return FALSE;
    }

    if (!glamor_init(xwl_screen->screen,
                     GLAMOR_INVERTED_Y_AXIS |
                     GLAMOR_USE_EGL_SCREEN |
                     GLAMOR_USE_SCREEN |
                     GLAMOR_USE_PICTURE_SCREEN)) {
        ErrorF("Failed to initialize glamor\n");
        return FALSE;
    }

    if (!dri3_screen_init(xwl_screen->screen, &xwl_dri3_info)) {
        ErrorF("Failed to initialize dri3\n");
        return FALSE;
    }

    xwl_screen->CreateScreenResources = screen->CreateScreenResources;
    screen->CreateScreenResources = xwl_glamor_create_screen_resources;
    screen->CreatePixmap = xwl_glamor_create_pixmap;
    screen->DestroyPixmap = xwl_glamor_destroy_pixmap;

    return TRUE;
}
