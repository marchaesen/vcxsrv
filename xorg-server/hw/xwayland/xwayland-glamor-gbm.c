/*
 * Copyright © 2011-2014 Intel Corporation
 * Copyright © 2017 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including
 * the next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Lyude Paul <lyude@redhat.com>
 *
 */

#include <xwayland-config.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <xf86drm.h>
#include <drm_fourcc.h>

#define MESA_EGL_NO_X11_HEADERS
#define EGL_NO_X11
#include <gbm.h>
#include <glamor_egl.h>

#include <glamor.h>
#include <glamor_context.h>
#include <dri3.h>
#include "drm-client-protocol.h"

#include "xwayland-glamor.h"
#include "xwayland-pixmap.h"
#include "xwayland-screen.h"

#include "linux-dmabuf-unstable-v1-client-protocol.h"

struct xwl_gbm_private {
    char *device_name;
    struct gbm_device *gbm;
    struct wl_drm *drm;
    struct zwp_linux_dmabuf_v1 *dmabuf;
    int drm_fd;
    int fd_render_node;
    Bool drm_authenticated;
    uint32_t capabilities;
    int dmabuf_capable;
};

struct xwl_pixmap {
    struct wl_buffer *buffer;
    EGLImage image;
    unsigned int texture;
    struct gbm_bo *bo;
};

static DevPrivateKeyRec xwl_gbm_private_key;
static DevPrivateKeyRec xwl_auth_state_private_key;

static inline struct xwl_gbm_private *
xwl_gbm_get(struct xwl_screen *xwl_screen)
{
    return dixLookupPrivate(&xwl_screen->screen->devPrivates,
                            &xwl_gbm_private_key);
}

static uint32_t
gbm_format_for_depth(int depth)
{
    switch (depth) {
    case 16:
        return GBM_FORMAT_RGB565;
    case 24:
        return GBM_FORMAT_XRGB8888;
    case 30:
        return GBM_FORMAT_ARGB2101010;
    default:
        ErrorF("unexpected depth: %d\n", depth);
    case 32:
        return GBM_FORMAT_ARGB8888;
    }
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
    case 30:
        return WL_DRM_FORMAT_ARGB2101010;
    default:
        ErrorF("unexpected depth: %d\n", depth);
    case 32:
        return WL_DRM_FORMAT_ARGB8888;
    }
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

static char
is_device_path_render_node (const char *device_path)
{
    char is_render_node;
    int fd;

    fd = open(device_path, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return 0;

    is_render_node = is_fd_render_node(fd);
    close(fd);

    return is_render_node;
}

static PixmapPtr
xwl_glamor_gbm_create_pixmap_for_bo(ScreenPtr screen, struct gbm_bo *bo,
                                    int depth)
{
    PixmapPtr pixmap;
    struct xwl_pixmap *xwl_pixmap;
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);

    xwl_pixmap = calloc(1, sizeof(*xwl_pixmap));
    if (xwl_pixmap == NULL)
        return NULL;

    pixmap = glamor_create_pixmap(screen,
                                  gbm_bo_get_width(bo),
                                  gbm_bo_get_height(bo),
                                  depth,
                                  GLAMOR_CREATE_PIXMAP_NO_TEXTURE);
    if (!pixmap) {
        free(xwl_pixmap);
        return NULL;
    }

    xwl_glamor_egl_make_current(xwl_screen);
    xwl_pixmap->bo = bo;
    xwl_pixmap->buffer = NULL;
    xwl_pixmap->image = eglCreateImageKHR(xwl_screen->egl_display,
                                          xwl_screen->egl_context,
                                          EGL_NATIVE_PIXMAP_KHR,
                                          xwl_pixmap->bo, NULL);
    if (xwl_pixmap->image == EGL_NO_IMAGE_KHR)
      goto error;

    glGenTextures(1, &xwl_pixmap->texture);
    glBindTexture(GL_TEXTURE_2D, xwl_pixmap->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, xwl_pixmap->image);
    if (eglGetError() != EGL_SUCCESS)
      goto error;

    glBindTexture(GL_TEXTURE_2D, 0);

    if (!glamor_set_pixmap_texture(pixmap, xwl_pixmap->texture))
      goto error;

    glamor_set_pixmap_type(pixmap, GLAMOR_TEXTURE_DRM);
    xwl_pixmap_set_private(pixmap, xwl_pixmap);

    return pixmap;

error:
    if (xwl_pixmap->image != EGL_NO_IMAGE_KHR)
      eglDestroyImageKHR(xwl_screen->egl_display, xwl_pixmap->image);
    if (pixmap)
      glamor_destroy_pixmap(pixmap);
    free(xwl_pixmap);

    return NULL;
}

static PixmapPtr
xwl_glamor_gbm_create_pixmap(ScreenPtr screen,
                             int width, int height, int depth,
                             unsigned int hint)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);
    struct gbm_bo *bo;
    PixmapPtr pixmap = NULL;

    if (width > 0 && height > 0 && depth >= 15 &&
        (hint == CREATE_PIXMAP_USAGE_BACKING_PIXMAP ||
         hint == CREATE_PIXMAP_USAGE_SHARED ||
         (xwl_screen->rootless && hint == 0))) {
        uint32_t format = gbm_format_for_depth(depth);

#ifdef GBM_BO_WITH_MODIFIERS
        if (xwl_gbm->dmabuf_capable) {
            uint32_t num_modifiers;
            uint64_t *modifiers = NULL;

            glamor_get_modifiers(screen, format, &num_modifiers, &modifiers);
            bo = gbm_bo_create_with_modifiers(xwl_gbm->gbm, width, height,
                                              format, modifiers, num_modifiers);
            free(modifiers);
        }
        else
#endif
        {
            bo = gbm_bo_create(xwl_gbm->gbm, width, height, format,
                               GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        }

        if (bo) {
            pixmap = xwl_glamor_gbm_create_pixmap_for_bo(screen, bo, depth);

            if (!pixmap) {
                gbm_bo_destroy(bo);
            }
            else if (xwl_screen->rootless && hint == CREATE_PIXMAP_USAGE_BACKING_PIXMAP) {
                glamor_clear_pixmap(pixmap);
            }
        }
    }

    if (!pixmap)
        pixmap = glamor_create_pixmap(screen, width, height, depth, hint);

    return pixmap;
}

static Bool
xwl_glamor_gbm_destroy_pixmap(PixmapPtr pixmap)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(pixmap->drawable.pScreen);
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);

    if (xwl_pixmap && pixmap->refcnt == 1) {
        xwl_pixmap_del_buffer_release_cb(pixmap);
        if (xwl_pixmap->buffer)
            wl_buffer_destroy(xwl_pixmap->buffer);

        eglDestroyImageKHR(xwl_screen->egl_display, xwl_pixmap->image);
        if (xwl_pixmap->bo)
           gbm_bo_destroy(xwl_pixmap->bo);
        free(xwl_pixmap);
    }

    return glamor_destroy_pixmap(pixmap);
}

static const struct wl_buffer_listener xwl_glamor_gbm_buffer_listener = {
    xwl_pixmap_buffer_release_cb,
};

static struct wl_buffer *
xwl_glamor_gbm_get_wl_buffer_for_pixmap(PixmapPtr pixmap,
                                        Bool *created)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(pixmap->drawable.pScreen);
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);
    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);
    unsigned short width = pixmap->drawable.width;
    unsigned short height = pixmap->drawable.height;
    int prime_fd;
    int num_planes;
    uint32_t strides[4];
    uint32_t offsets[4];
    uint64_t modifier;
    int i;

    if (xwl_pixmap == NULL)
       return NULL;

    if (xwl_pixmap->buffer) {
        /* Buffer already exists. Return it and inform caller if interested. */
        if (created)
            *created = FALSE;
        return xwl_pixmap->buffer;
    }

    /* Buffer does not exist yet. Create now and inform caller if interested. */
    if (created)
        *created = TRUE;

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
    strides[0] = gbm_bo_get_stride(xwl_pixmap->bo);
    offsets[0] = 0;
#endif

    if (xwl_gbm->dmabuf && modifier != DRM_FORMAT_MOD_INVALID) {
        struct zwp_linux_buffer_params_v1 *params;

        params = zwp_linux_dmabuf_v1_create_params(xwl_gbm->dmabuf);
        for (i = 0; i < num_planes; i++) {
            zwp_linux_buffer_params_v1_add(params, prime_fd, i,
                                           offsets[i], strides[i],
                                           modifier >> 32, modifier & 0xffffffff);
        }

        xwl_pixmap->buffer =
           zwp_linux_buffer_params_v1_create_immed(params, width, height,
                                                   wl_drm_format_for_depth(pixmap->drawable.depth),
                                                   0);
        zwp_linux_buffer_params_v1_destroy(params);
    } else if (num_planes == 1) {
        xwl_pixmap->buffer =
            wl_drm_create_prime_buffer(xwl_gbm->drm, prime_fd, width, height,
                                       wl_drm_format_for_depth(pixmap->drawable.depth),
                                       0, gbm_bo_get_stride(xwl_pixmap->bo),
                                       0, 0,
                                       0, 0);
    }

    close(prime_fd);

    /* Add our listener now */
    wl_buffer_add_listener(xwl_pixmap->buffer,
                           &xwl_glamor_gbm_buffer_listener, pixmap);

    return xwl_pixmap->buffer;
}

static void
xwl_glamor_gbm_cleanup(struct xwl_screen *xwl_screen)
{
    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);

    if (xwl_gbm->device_name)
        free(xwl_gbm->device_name);
    if (xwl_gbm->drm_fd)
        close(xwl_gbm->drm_fd);
    if (xwl_gbm->drm)
        wl_drm_destroy(xwl_gbm->drm);
    if (xwl_gbm->gbm)
        gbm_device_destroy(xwl_gbm->gbm);

    free(xwl_gbm);
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
        state = dixLookupPrivate(&pClient->devPrivates,
                                 &xwl_auth_state_private_key);
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
    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);
    struct xwl_auth_state *state;
    drm_magic_t magic;
    int fd;

    fd = open(xwl_gbm->device_name, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return BadAlloc;
    if (xwl_gbm->fd_render_node) {
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

    wl_drm_authenticate(xwl_gbm->drm, magic);
    state->callback = wl_display_sync(xwl_screen->display);
    wl_callback_add_listener(state->callback, &sync_listener, state);
    dixSetPrivate(&client->devPrivates, &xwl_auth_state_private_key, state);

    IgnoreClient(client);

    return Success;
}

_X_EXPORT PixmapPtr
glamor_pixmap_from_fds(ScreenPtr screen, CARD8 num_fds, const int *fds,
                       CARD16 width, CARD16 height,
                       const CARD32 *strides, const CARD32 *offsets,
                       CARD8 depth, CARD8 bpp, uint64_t modifier)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);
    struct gbm_bo *bo = NULL;
    PixmapPtr pixmap;
    int i;

    if (width == 0 || height == 0 || num_fds == 0 ||
        depth < 15 || bpp != BitsPerPixel(depth) ||
        strides[0] < width * bpp / 8)
       goto error;

    if (xwl_gbm->dmabuf_capable && modifier != DRM_FORMAT_MOD_INVALID) {
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
       bo = gbm_bo_import(xwl_gbm->gbm, GBM_BO_IMPORT_FD_MODIFIER, &data, 0);
#endif
    } else if (num_fds == 1) {
       struct gbm_import_fd_data data;

       data.fd = fds[0];
       data.width = width;
       data.height = height;
       data.stride = strides[0];
       data.format = gbm_format_for_depth(depth);
       bo = gbm_bo_import(xwl_gbm->gbm, GBM_BO_IMPORT_FD, &data,
             GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    } else {
       goto error;
    }

    if (bo == NULL)
       goto error;

    pixmap = xwl_glamor_gbm_create_pixmap_for_bo(screen, bo, depth);
    if (pixmap == NULL) {
       gbm_bo_destroy(bo);
       goto error;
    }

    return pixmap;

error:
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

    if (xwl_pixmap == NULL)
       return 0;

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

/* Not actually used, just defined here so there's something for
 * _glamor_egl_fds_from_pixmap() to link against
 */
_X_EXPORT int
glamor_egl_fd_from_pixmap(ScreenPtr screen, PixmapPtr pixmap,
                          CARD16 *stride, CARD32 *size)
{
    return -1;
}

_X_EXPORT Bool
glamor_get_formats(ScreenPtr screen,
                   CARD32 *num_formats, CARD32 **formats)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);
    int i;

    /* Explicitly zero the count as the caller may ignore the return value */
    *num_formats = 0;

    if (!xwl_gbm->dmabuf_capable || !xwl_gbm->dmabuf)
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

_X_EXPORT Bool
glamor_get_modifiers(ScreenPtr screen, uint32_t format,
                     uint32_t *num_modifiers, uint64_t **modifiers)
{
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);
    struct xwl_format *xwl_format = NULL;
    int i;

    /* Explicitly zero the count as the caller may ignore the return value */
    *num_modifiers = 0;

    if (!xwl_gbm->dmabuf_capable || !xwl_gbm->dmabuf)
        return FALSE;

    if (xwl_screen->num_formats == 0)
       return TRUE;

    for (i = 0; i < xwl_screen->num_formats; i++) {
       if (xwl_screen->formats[i].format == format) {
          xwl_format = &xwl_screen->formats[i];
          break;
       }
    }

    if (!xwl_format)
        return FALSE;

    *modifiers = calloc(xwl_format->num_modifiers, sizeof(uint64_t));
    if (*modifiers == NULL)
        return FALSE;

    for (i = 0; i < xwl_format->num_modifiers; i++)
       (*modifiers)[i] = xwl_format->modifiers[i];
    *num_modifiers = xwl_format->num_modifiers;

    return TRUE;
}

static const dri3_screen_info_rec xwl_dri3_info = {
    .version = 2,
    .open = NULL,
    .pixmap_from_fds = glamor_pixmap_from_fds,
    .fds_from_pixmap = glamor_fds_from_pixmap,
    .open_client = xwl_dri3_open_client,
    .get_formats = glamor_get_formats,
    .get_modifiers = glamor_get_modifiers,
    .get_drawable_modifiers = glamor_get_drawable_modifiers,
};

static const char *
get_render_node_path_for_device(const drmDevicePtr drm_device,
                                const char *device_path)
{
    char *render_node_path = NULL;
    char device_found = 0;
    int i;

    for (i = 0; i < DRM_NODE_MAX; i++) {
        if ((drm_device->available_nodes & (1 << i)) == 0)
           continue;

        if (!strcmp (device_path, drm_device->nodes[i]))
            device_found = 1;

        if (is_device_path_render_node(drm_device->nodes[i]))
            render_node_path = drm_device->nodes[i];

        if (device_found && render_node_path)
            return render_node_path;
    }

    return NULL;
}

static char *
get_render_node_path(const char *device_path)
{
    drmDevicePtr *devices = NULL;
    char *render_node_path = NULL;
    int i, n_devices, max_devices;

    max_devices = drmGetDevices2(0, NULL, 0);
    if (max_devices <= 0)
        goto out;

    devices = calloc(max_devices, sizeof(drmDevicePtr));
    if (!devices)
        goto out;

    n_devices = drmGetDevices2(0, devices, max_devices);
    if (n_devices < 0)
        goto out;

    for (i = 0; i < n_devices; i++) {
       const char *node_path = get_render_node_path_for_device(devices[i],
                                                               device_path);
       if (node_path) {
           render_node_path = strdup(node_path);
           break;
       }
    }

out:
    free(devices);
    return render_node_path;
}

static void
xwl_drm_handle_device(void *data, struct wl_drm *drm, const char *device)
{
   struct xwl_screen *xwl_screen = data;
   struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);
   drm_magic_t magic;
   char *render_node_path = NULL;

   if (!is_device_path_render_node(device))
       render_node_path = get_render_node_path(device);

   if (render_node_path)
       xwl_gbm->device_name = render_node_path;
   else
       xwl_gbm->device_name = strdup(device);

   if (!xwl_gbm->device_name) {
       xwl_glamor_gbm_cleanup(xwl_screen);
       return;
   }

   xwl_gbm->drm_fd = open(xwl_gbm->device_name, O_RDWR | O_CLOEXEC);
   if (xwl_gbm->drm_fd == -1) {
       ErrorF("wayland-egl: could not open %s (%s)\n",
              xwl_gbm->device_name, strerror(errno));
       xwl_glamor_gbm_cleanup(xwl_screen);
       return;
   }

   if (is_fd_render_node(xwl_gbm->drm_fd)) {
       xwl_gbm->fd_render_node = 1;
       xwl_screen->expecting_event--;
   } else {
       drmGetMagic(xwl_gbm->drm_fd, &magic);
       wl_drm_authenticate(xwl_gbm->drm, magic);
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
    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);

    xwl_gbm->drm_authenticated = TRUE;
    xwl_screen->expecting_event--;
}

static void
xwl_drm_handle_capabilities(void *data, struct wl_drm *drm, uint32_t value)
{
    xwl_gbm_get(data)->capabilities = value;
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

    if (modifier_hi == (DRM_FORMAT_MOD_INVALID >> 32) &&
        modifier_lo == (DRM_FORMAT_MOD_INVALID & 0xffffffff))
        return;

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
    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);

    if (version < 2)
        return FALSE;

    xwl_gbm->drm =
        wl_registry_bind(xwl_screen->registry, id, &wl_drm_interface, 2);
    wl_drm_add_listener(xwl_gbm->drm, &xwl_drm_listener, xwl_screen);
    xwl_screen->expecting_event++;

    return TRUE;
}

Bool
xwl_screen_set_dmabuf_interface(struct xwl_screen *xwl_screen,
                                uint32_t id, uint32_t version)
{
    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);

    if (version < 3)
        return FALSE;

    xwl_gbm->dmabuf =
        wl_registry_bind(xwl_screen->registry, id, &zwp_linux_dmabuf_v1_interface, 3);
    zwp_linux_dmabuf_v1_add_listener(xwl_gbm->dmabuf, &xwl_dmabuf_listener, xwl_screen);

    return TRUE;
}

static Bool
xwl_glamor_gbm_init_wl_registry(struct xwl_screen *xwl_screen,
                                struct wl_registry *wl_registry,
                                uint32_t id, const char *name,
                                uint32_t version)
{
    if (strcmp(name, "wl_drm") == 0) {
        xwl_screen_set_drm_interface(xwl_screen, id, version);
        return TRUE;
    } else if (strcmp(name, "zwp_linux_dmabuf_v1") == 0) {
        xwl_screen_set_dmabuf_interface(xwl_screen, id, version);
        return TRUE;
    }

    /* no match */
    return FALSE;
}

static Bool
xwl_glamor_gbm_has_egl_extension(void)
{
    return (epoxy_has_egl_extension(NULL, "EGL_MESA_platform_gbm") ||
            epoxy_has_egl_extension(NULL, "EGL_KHR_platform_gbm"));
}

static Bool
xwl_glamor_gbm_has_wl_interfaces(struct xwl_screen *xwl_screen)
{
    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);

    if (xwl_gbm->drm == NULL) {
        ErrorF("glamor: 'wl_drm' not supported\n");
        return FALSE;
    }

    return TRUE;
}

static Bool
xwl_glamor_gbm_init_egl(struct xwl_screen *xwl_screen)
{
    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);
    EGLint major, minor;
    Bool egl_initialized = FALSE;
    static const EGLint config_attribs_core[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_CONTEXT_MAJOR_VERSION_KHR,
        GLAMOR_GL_CORE_VER_MAJOR,
        EGL_CONTEXT_MINOR_VERSION_KHR,
        GLAMOR_GL_CORE_VER_MINOR,
        EGL_NONE
    };
    const GLubyte *renderer;

    if (!xwl_gbm->fd_render_node && !xwl_gbm->drm_authenticated) {
        ErrorF("Failed to get wl_drm, disabling Glamor and DRI3\n");
	return FALSE;
    }

    xwl_gbm->gbm = gbm_create_device(xwl_gbm->drm_fd);
    if (!xwl_gbm->gbm) {
        ErrorF("couldn't create gbm device\n");
        goto error;
    }

    xwl_screen->egl_display = glamor_egl_get_display(EGL_PLATFORM_GBM_MESA,
                                                     xwl_gbm->gbm);
    if (xwl_screen->egl_display == EGL_NO_DISPLAY) {
        ErrorF("glamor_egl_get_display() failed\n");
        goto error;
    }

    egl_initialized = eglInitialize(xwl_screen->egl_display, &major, &minor);
    if (!egl_initialized) {
        ErrorF("eglInitialize() failed\n");
        goto error;
    }

    eglBindAPI(EGL_OPENGL_API);

    xwl_screen->egl_context = eglCreateContext(
        xwl_screen->egl_display, NULL, EGL_NO_CONTEXT, config_attribs_core);
    if (xwl_screen->egl_context == EGL_NO_CONTEXT) {
        xwl_screen->egl_context = eglCreateContext(
            xwl_screen->egl_display, NULL, EGL_NO_CONTEXT, NULL);
    }

    if (xwl_screen->egl_context == EGL_NO_CONTEXT) {
        ErrorF("Failed to create EGL context with GL\n");
        goto error;
    }

    if (!eglMakeCurrent(xwl_screen->egl_display,
                        EGL_NO_SURFACE, EGL_NO_SURFACE,
                        xwl_screen->egl_context)) {
        ErrorF("Failed to make EGL context current with GL\n");
        goto error;
    }

    /* glamor needs either big-GL 2.1 or GLES2 */
    if (epoxy_gl_version() < 21) {
        const EGLint gles_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION,
            2,
            EGL_NONE,
        };

        /* Recreate the context with GLES2 instead */
        eglMakeCurrent(xwl_screen->egl_display,EGL_NO_SURFACE,
                       EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(xwl_screen->egl_display, xwl_screen->egl_context);

        eglBindAPI(EGL_OPENGL_ES_API);
        xwl_screen->egl_context = eglCreateContext(xwl_screen->egl_display,
                                                   NULL, EGL_NO_CONTEXT,
                                                   gles_attribs);

        if (xwl_screen->egl_context == EGL_NO_CONTEXT) {
            ErrorF("Failed to create EGL context with GLES2\n");
            goto error;
        }

        if (!eglMakeCurrent(xwl_screen->egl_display,
                            EGL_NO_SURFACE, EGL_NO_SURFACE,
                            xwl_screen->egl_context)) {
            ErrorF("Failed to make EGL context current with GLES2\n");
            goto error;
        }
    }

    renderer = glGetString(GL_RENDERER);
    if (!renderer) {
        ErrorF("glGetString() returned NULL, your GL is broken\n");
        goto error;
    }
    if (strstr((const char *)renderer, "llvmpipe")) {
        ErrorF("Refusing to try glamor on llvmpipe\n");
        goto error;
    }

    if (!epoxy_has_gl_extension("GL_OES_EGL_image")) {
        ErrorF("GL_OES_EGL_image not available\n");
        goto error;
    }

    if (epoxy_has_egl_extension(xwl_screen->egl_display,
                                "EXT_image_dma_buf_import") &&
        epoxy_has_egl_extension(xwl_screen->egl_display,
                                "EXT_image_dma_buf_import_modifiers"))
       xwl_gbm->dmabuf_capable = TRUE;

    return TRUE;
error:
    if (xwl_screen->egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(xwl_screen->egl_display, xwl_screen->egl_context);
        xwl_screen->egl_context = EGL_NO_CONTEXT;
    }

    if (xwl_screen->egl_display != EGL_NO_DISPLAY) {
        eglTerminate(xwl_screen->egl_display);
        xwl_screen->egl_display = EGL_NO_DISPLAY;
    }

    xwl_glamor_gbm_cleanup(xwl_screen);
    return FALSE;
}

static Bool
xwl_glamor_gbm_init_screen(struct xwl_screen *xwl_screen)
{
    struct xwl_gbm_private *xwl_gbm = xwl_gbm_get(xwl_screen);

    if (!dri3_screen_init(xwl_screen->screen, &xwl_dri3_info)) {
        ErrorF("Failed to initialize dri3\n");
        goto error;
    }

    if (xwl_gbm->fd_render_node)
        goto skip_drm_auth;

    if (!dixRegisterPrivateKey(&xwl_auth_state_private_key, PRIVATE_CLIENT,
                               0)) {
        ErrorF("Failed to register private key\n");
        goto error;
    }

    if (!AddCallback(&ClientStateCallback, xwl_auth_state_client_callback,
                     NULL)) {
        ErrorF("Failed to add client state callback\n");
        goto error;
    }

skip_drm_auth:
    xwl_screen->screen->CreatePixmap = xwl_glamor_gbm_create_pixmap;
    xwl_screen->screen->DestroyPixmap = xwl_glamor_gbm_destroy_pixmap;

    return TRUE;
error:
    xwl_glamor_gbm_cleanup(xwl_screen);
    return FALSE;
}

void
xwl_glamor_init_gbm(struct xwl_screen *xwl_screen)
{
    struct xwl_gbm_private *xwl_gbm;

    xwl_screen->gbm_backend.is_available = FALSE;

    if (!xwl_glamor_gbm_has_egl_extension())
        return;

    if (!dixRegisterPrivateKey(&xwl_gbm_private_key, PRIVATE_SCREEN, 0))
        return;

    xwl_gbm = calloc(sizeof(*xwl_gbm), 1);
    if (!xwl_gbm) {
        ErrorF("glamor: Not enough memory to setup GBM, disabling\n");
        return;
    }

    dixSetPrivate(&xwl_screen->screen->devPrivates, &xwl_gbm_private_key,
                  xwl_gbm);

    xwl_screen->gbm_backend.init_wl_registry = xwl_glamor_gbm_init_wl_registry;
    xwl_screen->gbm_backend.has_wl_interfaces = xwl_glamor_gbm_has_wl_interfaces;
    xwl_screen->gbm_backend.init_egl = xwl_glamor_gbm_init_egl;
    xwl_screen->gbm_backend.init_screen = xwl_glamor_gbm_init_screen;
    xwl_screen->gbm_backend.get_wl_buffer_for_pixmap = xwl_glamor_gbm_get_wl_buffer_for_pixmap;
    xwl_screen->gbm_backend.is_available = TRUE;
}
