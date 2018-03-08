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
#include <drm_fourcc.h>

#define MESA_EGL_NO_X11_HEADERS
#include <gbm.h>
#include <glamor_egl.h>

#include <glamor.h>
#include <glamor_context.h>
#include <dri3.h>
#include "drm-client-protocol.h"

static DevPrivateKeyRec xwl_auth_state_private_key;

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
wl_drm_format_for_depth(int depth)
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

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
    int num_planes;
    uint32_t strides[4];
    uint32_t offsets[4];
    uint64_t modifier;
    int i;

    if (xwl_pixmap->buffer)
        return xwl_pixmap->buffer;

    if (!xwl_pixmap->bo)
       return NULL;

    prime_fd = gbm_bo_get_fd(xwl_pixmap->bo);
    if (prime_fd == -1)
        return NULL;

#ifdef GBM_BO_WITH_MODIFIERS
    num_planes = gbm_bo_get_plane_count(xwl_pixmap->bo);
    modifier = gbm_bo_get_modifier(xwl_pixmap->bo);
    for (i = 0; i < num_planes; i++) {
        strides[i] = gbm_bo_get_stride_for_plane(xwl_pixmap->bo, i);
        offsets[i] = gbm_bo_get_offset(xwl_pixmap->bo, i);
    }
#else
    num_planes = 1;
    modifier = DRM_FORMAT_MOD_INVALID;
    strides[0] = gbm_go_get_stride(xwl_pixmap->bo);
    offsets[0] = 0;
#endif

    if (xwl_screen->dmabuf && modifier != DRM_FORMAT_MOD_INVALID) {
        struct zwp_linux_buffer_params_v1 *params;

        params = zwp_linux_dmabuf_v1_create_params(xwl_screen->dmabuf);
        for (i = 0; i < num_planes; i++) {
            zwp_linux_buffer_params_v1_add(params, prime_fd, i,
                                           offsets[i], strides[i],
                                           modifier >> 32, modifier & 0xffffffff);
        }

        xwl_pixmap->buffer =
           zwp_linux_buffer_params_v1_create_immed(params,
                                                   pixmap->drawable.width,
                                                   pixmap->drawable.height,
                                                   wl_drm_format_for_depth(pixmap->drawable.depth),
                                                   0);
        zwp_linux_buffer_params_v1_destroy(params);
    } else if (num_planes == 1) {
        xwl_pixmap->buffer =
            wl_drm_create_prime_buffer(xwl_screen->drm, prime_fd,
                                       pixmap->drawable.width,
                                       pixmap->drawable.height,
                                       wl_drm_format_for_depth(pixmap->drawable.depth),
                                       0, gbm_bo_get_stride(xwl_pixmap->bo),
                                       0, 0,
                                       0, 0);
    }

    close(prime_fd);
    return xwl_pixmap->buffer;
}

static PixmapPtr
xwl_glamor_create_pixmap(ScreenPtr screen,
                         int width, int height, int depth, unsigned int hint)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct gbm_bo *bo;
    uint32_t format;

    if (width > 0 && height > 0 && depth >= 15 &&
        (hint == 0 ||
         hint == CREATE_PIXMAP_USAGE_BACKING_PIXMAP ||
         hint == CREATE_PIXMAP_USAGE_SHARED)) {
        format = gbm_format_for_depth(depth);

#ifdef GBM_BO_WITH_MODIFIERS
        if (xwl_screen->dmabuf_capable) {
            uint32_t num_modifiers;
            uint64_t *modifiers = NULL;

            glamor_get_modifiers(screen, format, &num_modifiers, &modifiers);
            bo = gbm_bo_create_with_modifiers(xwl_screen->gbm, width, height,
                                              format, modifiers, num_modifiers);
            free(modifiers);
        }
        else
#endif
        {
            bo = gbm_bo_create(xwl_screen->gbm, width, height, format,
                               GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        }

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
        if (xwl_pixmap->bo)
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

    if (xwl_screen->rootless) {
        screen->devPrivate =
            fbCreatePixmap(screen, 0, 0, screen->rootDepth, 0);
    }
    else {
        screen->devPrivate =
            xwl_glamor_create_pixmap(screen, screen->width, screen->height,
                                     screen->rootDepth,
                                     CREATE_PIXMAP_USAGE_BACKING_PIXMAP);
    }

    SetRootClip(screen, xwl_screen->root_clip_mode);

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
    static const EGLint config_attribs_core[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_CONTEXT_MAJOR_VERSION_KHR,
        GLAMOR_GL_CORE_VER_MAJOR,
        EGL_CONTEXT_MINOR_VERSION_KHR,
        GLAMOR_GL_CORE_VER_MINOR,
        EGL_NONE
    };

    if (xwl_screen->egl_display)
        return;

    xwl_screen->gbm = gbm_create_device(xwl_screen->drm_fd);
    if (xwl_screen->gbm == NULL) {
        ErrorF("couldn't get display device\n");
        return;
    }

    xwl_screen->egl_display = glamor_egl_get_display(EGL_PLATFORM_GBM_MESA,
                                                     xwl_screen->gbm);
    if (xwl_screen->egl_display == EGL_NO_DISPLAY) {
        ErrorF("glamor_egl_get_display() failed\n");
        return;
    }

    if (!eglInitialize(xwl_screen->egl_display, &major, &minor)) {
        ErrorF("eglInitialize() failed\n");
        return;
    }

    eglBindAPI(EGL_OPENGL_API);

    xwl_screen->egl_context = eglCreateContext(xwl_screen->egl_display,
                                               NULL, EGL_NO_CONTEXT, config_attribs_core);
    if (!xwl_screen->egl_context)
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
        ErrorF("GL_OES_EGL_image not available\n");
        return;
    }

    if (epoxy_has_egl_extension(xwl_screen->egl_display,
                                "EXT_image_dma_buf_import") &&
        epoxy_has_egl_extension(xwl_screen->egl_display,
                                "EXT_image_dma_buf_import_modifiers"))
       xwl_screen->dmabuf_capable = TRUE;

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
       ErrorF("wayland-egl: could not open %s (%s)\n",
              xwl_screen->device_name, strerror(errno));
       return;
   }

   xwl_screen->expecting_event--;

   if (is_fd_render_node(xwl_screen->drm_fd)) {
       xwl_screen->fd_render_node = 1;
   } else {
       drmGetMagic(xwl_screen->drm_fd, &magic);
       wl_drm_authenticate(xwl_screen->drm, magic);
       xwl_screen->expecting_event++; /* wait for 'authenticated' */
   }
}

static void
xwl_drm_handle_format(void *data, struct wl_drm *drm, uint32_t format)
{
}

static void
xwl_drm_handle_authenticated(void *data, struct wl_drm *drm)
{
    struct xwl_screen *xwl_screen = data;

    xwl_screen->drm_authenticated = 1;
    xwl_screen->expecting_event--;
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
    .format   = xwl_dmabuf_handle_format,
    .modifier = xwl_dmabuf_handle_modifier
};

Bool
xwl_screen_set_drm_interface(struct xwl_screen *xwl_screen,
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

int
glamor_egl_fd_name_from_pixmap(ScreenPtr screen,
                               PixmapPtr pixmap,
                               CARD16 *stride, CARD32 *size)
{
    return 0;
}

struct xwl_auth_state {
    int fd;
    ClientPtr client;
    struct wl_callback *callback;
};

static void
free_xwl_auth_state(ClientPtr pClient, struct xwl_auth_state *state)
{
    dixSetPrivate(&pClient->devPrivates, &xwl_auth_state_private_key, NULL);
    if (state) {
        wl_callback_destroy(state->callback);
        free(state);
    }
}

static void
xwl_auth_state_client_callback(CallbackListPtr *pcbl, void *unused, void *data)
{
    NewClientInfoRec *clientinfo = (NewClientInfoRec *) data;
    ClientPtr pClient = clientinfo->client;
    struct xwl_auth_state *state;

    switch (pClient->clientState) {
    case ClientStateGone:
    case ClientStateRetained:
        state = dixLookupPrivate(&pClient->devPrivates, &xwl_auth_state_private_key);
        free_xwl_auth_state(pClient, state);
        break;
    default:
        break;
    }
}

static void
sync_callback(void *data, struct wl_callback *callback, uint32_t serial)
{
    struct xwl_auth_state *state = data;
    ClientPtr client = state->client;

    /* if the client is gone, the callback is cancelled so it's safe to
     * assume the client is still in ClientStateRunning at this point...
     */
    dri3_send_open_reply(client, state->fd);
    AttendClient(client);
    free_xwl_auth_state(client, state);
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
    state->callback = wl_display_sync(xwl_screen->display);
    wl_callback_add_listener(state->callback, &sync_listener, state);
    dixSetPrivate(&client->devPrivates, &xwl_auth_state_private_key, state);

    IgnoreClient(client);

    return Success;
}

_X_EXPORT PixmapPtr
glamor_pixmap_from_fds(ScreenPtr screen,
                         CARD8 num_fds, int *fds,
                         CARD16 width, CARD16 height,
                         CARD32 *strides, CARD32 *offsets,
                         CARD8 depth, CARD8 bpp, uint64_t modifier)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct gbm_bo *bo = NULL;
    PixmapPtr pixmap;
    int i;

    if (width == 0 || height == 0 || num_fds == 0 ||
        depth < 15 || bpp != BitsPerPixel(depth) ||
        strides[0] < width * bpp / 8)
       goto error;

    if (xwl_screen->dmabuf_capable && modifier != DRM_FORMAT_MOD_INVALID) {
#ifdef GBM_BO_WITH_MODIFIERS
       struct gbm_import_fd_modifier_data data;

       data.width = width;
       data.height = height;
       data.num_fds = num_fds;
       data.format = gbm_format_for_depth(depth);
       data.modifier = modifier;
       for (i = 0; i < num_fds; i++) {
          data.fds[i] = fds[i];
          data.strides[i] = strides[i];
          data.offsets[i] = offsets[i];
       }
       bo = gbm_bo_import(xwl_screen->gbm, GBM_BO_IMPORT_FD_MODIFIER, &data, 0);
#endif
    } else if (num_fds == 1) {
       struct gbm_import_fd_data data;

       data.fd = fds[0];
       data.width = width;
       data.height = height;
       data.stride = strides[0];
       data.format = gbm_format_for_depth(depth);
       bo = gbm_bo_import(xwl_screen->gbm, GBM_BO_IMPORT_FD, &data,
             GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    } else {
       goto error;
    }

    if (bo == NULL)
       goto error;

    pixmap = xwl_glamor_create_pixmap_for_bo(screen, bo, depth);
    if (pixmap == NULL) {
       gbm_bo_destroy(bo);
       goto error;
    }

    return pixmap;

error:
    for (i = 0; i < num_fds; i++)
       close(fds[i]);
    return NULL;
}

_X_EXPORT int
glamor_egl_fds_from_pixmap(ScreenPtr screen, PixmapPtr pixmap, int *fds,
                           uint32_t *strides, uint32_t *offsets,
                           uint64_t *modifier)
{
    struct xwl_pixmap *xwl_pixmap;
#ifdef GBM_BO_WITH_MODIFIERS
    uint32_t num_fds;
    int i;
#endif

    xwl_pixmap = xwl_pixmap_get(pixmap);

    if (!xwl_pixmap->bo)
       return 0;

#ifdef GBM_BO_WITH_MODIFIERS
    num_fds = gbm_bo_get_plane_count(xwl_pixmap->bo);
    *modifier = gbm_bo_get_modifier(xwl_pixmap->bo);

    for (i = 0; i < num_fds; i++) {
        fds[i] = gbm_bo_get_fd(xwl_pixmap->bo);
        strides[i] = gbm_bo_get_stride_for_plane(xwl_pixmap->bo, i);
        offsets[i] = gbm_bo_get_offset(xwl_pixmap->bo, i);
    }

    return num_fds;
#else
    *modifier = DRM_FORMAT_MOD_INVALID;
    fds[0] = gbm_bo_get_fd(xwl_pixmap->bo);
    strides[0] = gbm_bo_get_stride(xwl_pixmap->bo);
    offsets[0] = 0;
    return 1;
#endif
}

_X_EXPORT Bool
glamor_get_formats(ScreenPtr screen,
                   CARD32 *num_formats, CARD32 **formats)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    int i;

    if (!xwl_screen->dmabuf_capable || !xwl_screen->dmabuf)
        return FALSE;

    if (xwl_screen->num_formats == 0) {
       *num_formats = 0;
       return TRUE;
    }

    *formats = calloc(xwl_screen->num_formats, sizeof(CARD32));
    if (*formats == NULL) {
        *num_formats = 0;
        return FALSE;
    }

    for (i = 0; i < xwl_screen->num_formats; i++)
       (*formats)[i] = xwl_screen->formats[i].format;
    *num_formats = xwl_screen->num_formats;

    return TRUE;
}

_X_EXPORT Bool
glamor_get_modifiers(ScreenPtr screen, CARD32 format,
                     CARD32 *num_modifiers, uint64_t **modifiers)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct xwl_format *xwl_format = NULL;
    int i;

    if (!xwl_screen->dmabuf_capable || !xwl_screen->dmabuf)
        return FALSE;

    if (xwl_screen->num_formats == 0) {
       *num_modifiers = 0;
       return TRUE;
    }

    for (i = 0; i < xwl_screen->num_formats; i++) {
       if (xwl_screen->formats[i].format == format) {
          xwl_format = &xwl_screen->formats[i];
          break;
       }
    }

    if (!xwl_format) {
	*num_modifiers = 0;
        return FALSE;
    }

    *modifiers = calloc(xwl_format->num_modifiers, sizeof(uint64_t));
    if (*modifiers == NULL) {
        *num_modifiers = 0;
        return FALSE;
    }

    for (i = 0; i < xwl_format->num_modifiers; i++)
       (*modifiers)[i] = xwl_format->modifiers[i];
    *num_modifiers = xwl_format->num_modifiers;

    return TRUE;
}


static dri3_screen_info_rec xwl_dri3_info = {
    .version = 2,
    .open = NULL,
    .pixmap_from_fds = glamor_pixmap_from_fds,
    .fds_from_pixmap = glamor_fds_from_pixmap,
    .open_client = xwl_dri3_open_client,
    .get_formats = glamor_get_formats,
    .get_modifiers = glamor_get_modifiers,
    .get_drawable_modifiers = glamor_get_drawable_modifiers,
};

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

    if (!xwl_screen->fd_render_node && !xwl_screen->drm_authenticated) {
        ErrorF("Failed to get wl_drm, disabling Glamor and DRI3\n");
	return FALSE;
    }

    xwl_drm_init_egl(xwl_screen);
    if (xwl_screen->egl_context == EGL_NO_CONTEXT) {
        ErrorF("Disabling glamor and dri3, EGL setup failed\n");
        return FALSE;
    }

    if (!glamor_init(xwl_screen->screen, GLAMOR_USE_EGL_SCREEN)) {
        ErrorF("Failed to initialize glamor\n");
        return FALSE;
    }

    if (!dri3_screen_init(xwl_screen->screen, &xwl_dri3_info)) {
        ErrorF("Failed to initialize dri3\n");
        return FALSE;
    }

    if (!dixRegisterPrivateKey(&xwl_auth_state_private_key, PRIVATE_CLIENT, 0)) {
        ErrorF("Failed to register private key\n");
        return FALSE;
    }

    if (!AddCallback(&ClientStateCallback, xwl_auth_state_client_callback, NULL)) {
        ErrorF("Failed to add client state callback\n");
        return FALSE;
    }

    xwl_screen->CreateScreenResources = screen->CreateScreenResources;
    screen->CreateScreenResources = xwl_glamor_create_screen_resources;
    screen->CreatePixmap = xwl_glamor_create_pixmap;
    screen->DestroyPixmap = xwl_glamor_destroy_pixmap;

#ifdef XV
    if (!xwl_glamor_xv_init(screen))
        ErrorF("Failed to initialize glamor Xv extension\n");
#endif

    return TRUE;
}
