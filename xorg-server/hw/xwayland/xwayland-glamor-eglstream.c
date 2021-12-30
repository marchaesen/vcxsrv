/*
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

#define MESA_EGL_NO_X11_HEADERS
#define EGL_NO_X11
#include <glamor_egl.h>
#include <glamor.h>
#include <glamor_priv.h>
#include <glamor_transform.h>
#include <glamor_transfer.h>

#include <xf86drm.h>
#include <dri3.h>
#include <drm_fourcc.h>

#include <epoxy/egl.h>

#include "xwayland-glamor.h"
#include "xwayland-pixmap.h"
#include "xwayland-screen.h"
#include "xwayland-window.h"

#include "wayland-eglstream-client-protocol.h"
#include "wayland-eglstream-controller-client-protocol.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"

struct xwl_eglstream_private {
    EGLDeviceEXT egl_device;
    struct wl_eglstream_display *display;
    struct wl_eglstream_controller *controller;
    uint32_t display_caps;

    EGLConfig config;

    Bool have_egl_damage;
    Bool have_egl_stream_flush;

    GLint blit_prog;
    GLuint blit_vao;
    GLuint blit_vbo;
    GLuint blit_is_rgba_pos;
};

enum xwl_pixmap_type {
    XWL_PIXMAP_EGLSTREAM, /* Pixmaps created by glamor. */
    XWL_PIXMAP_DMA_BUF, /* Pixmaps allocated through DRI3. */
};

struct xwl_pixmap {
    enum xwl_pixmap_type type;
    /* add any new <= 4-byte member here to avoid holes on 64-bit */
    struct xwl_screen *xwl_screen;
    struct wl_buffer *buffer;
    struct wl_callback *pending_cb;
    Bool wait_for_buffer_release;

    /* XWL_PIXMAP_EGLSTREAM. */
    EGLStreamKHR stream;
    EGLSurface surface;

    /* XWL_PIXMAP_DMA_BUF. */
    EGLImage image;
};

static DevPrivateKeyRec xwl_eglstream_private_key;

static inline struct xwl_eglstream_private *
xwl_eglstream_get(struct xwl_screen *xwl_screen)
{
    return dixLookupPrivate(&xwl_screen->screen->devPrivates,
                            &xwl_eglstream_private_key);
}

static GLuint
xwl_eglstream_build_glsl_prog(GLuint vs, GLuint fs)
{
    GLint ok;
    GLuint prog;

    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);

    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLchar *info;
        GLint size;

        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &size);
        info = malloc(size);

        glGetProgramInfoLog(prog, size, NULL, info);
        ErrorF("Failed to link: %s\n", info);
        FatalError("GLSL link failure\n");
    }

    return prog;
}

static void
xwl_eglstream_cleanup(struct xwl_screen *xwl_screen)
{
    struct xwl_eglstream_private *xwl_eglstream =
        xwl_eglstream_get(xwl_screen);

    if (xwl_eglstream->display)
        wl_eglstream_display_destroy(xwl_eglstream->display);
    if (xwl_eglstream->controller)
        wl_eglstream_controller_destroy(xwl_eglstream->controller);
    if (xwl_eglstream->blit_prog) {
        glDeleteProgram(xwl_eglstream->blit_prog);
        glDeleteBuffers(1, &xwl_eglstream->blit_vbo);
    }

    free(xwl_eglstream);
}

static Bool
xwl_glamor_egl_supports_device_probing(void)
{
    return epoxy_has_egl_extension(NULL, "EGL_EXT_device_base");
}

static void **
xwl_glamor_egl_get_devices(int *num_devices)
{
    EGLDeviceEXT *devices, *tmp;
    Bool ret;
    int drm_dev_count = 0;
    int i;

    if (!xwl_glamor_egl_supports_device_probing())
        return NULL;

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
    tmp = realloc(devices, sizeof(EGLDeviceEXT) * drm_dev_count);
    if (!tmp)
        goto error;

    devices = tmp;

    return devices;

error:
    free(devices);

    return NULL;
}

static Bool
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

static void
xwl_eglstream_destroy_pixmap_stream(struct xwl_pixmap *xwl_pixmap)
{
    struct xwl_screen *xwl_screen = xwl_pixmap->xwl_screen;

    /* If we're using this stream in the current egl context, unbind it so the
     * driver doesn't keep it around until the next eglMakeCurrent()
     * don't have to keep it around until something else changes the surface
     */
    xwl_glamor_egl_make_current(xwl_screen);
    if (eglGetCurrentSurface(EGL_READ) == xwl_pixmap->surface ||
        eglGetCurrentSurface(EGL_DRAW) == xwl_pixmap->surface) {
        eglMakeCurrent(xwl_screen->egl_display,
                       EGL_NO_SURFACE, EGL_NO_SURFACE,
                       xwl_screen->egl_context);
    }

    if (xwl_pixmap->surface != EGL_NO_SURFACE)
        eglDestroySurface(xwl_screen->egl_display, xwl_pixmap->surface);

    if (xwl_pixmap->stream != EGL_NO_STREAM_KHR)
        eglDestroyStreamKHR(xwl_screen->egl_display, xwl_pixmap->stream);

    if (xwl_pixmap->buffer)
        wl_buffer_destroy(xwl_pixmap->buffer);

    if (xwl_pixmap->image != EGL_NO_IMAGE_KHR)
        eglDestroyImageKHR(xwl_screen->egl_display, xwl_pixmap->image);

    free(xwl_pixmap);
}

static void
xwl_eglstream_destroy_pending_cb(PixmapPtr pixmap)
{
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);

    if (xwl_pixmap && xwl_pixmap->pending_cb) {
        wl_callback_destroy(xwl_pixmap->pending_cb);
        xwl_pixmap->pending_cb = NULL;
    }
}

static Bool
xwl_glamor_eglstream_destroy_pixmap(PixmapPtr pixmap)
{
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);

    if (xwl_pixmap && pixmap->refcnt == 1) {
        xwl_eglstream_destroy_pending_cb(pixmap);
        xwl_eglstream_destroy_pixmap_stream(xwl_pixmap);
        xwl_pixmap_del_buffer_release_cb(pixmap);
    }
    return glamor_destroy_pixmap(pixmap);
}

static struct wl_buffer *
xwl_glamor_eglstream_get_wl_buffer_for_pixmap(PixmapPtr pixmap)
{
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);

    if (!xwl_pixmap)
        return NULL;

    return xwl_pixmap->buffer;
}

static const char *
xwl_eglstream_get_error_str(EGLint error)
{
    switch (error) {
    case EGL_BAD_PARAMETER:
        return "EGL_BAD_PARAMETER";
    case EGL_BAD_ATTRIBUTE:
        return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_MATCH:
        return "EGL_BAD_MATCH";
    case EGL_BAD_ACCESS:
        return "EGL_BAD_ACCESS";
    case EGL_BAD_STATE_KHR:
        return "EGL_BAD_STATE_KHR";
    case EGL_BAD_STREAM_KHR:
        return "EGL_BAD_STREAM_KHR";
    case EGL_BAD_DISPLAY:
        return "EGL_BAD_DISPLAY";
    case EGL_NOT_INITIALIZED:
        return "EGL_NOT_INITIALIZED";
    default:
        return "Unknown error";
    }
}

static const char *
xwl_eglstream_get_stream_state_str(EGLint state)
{
    switch (state) {
    case EGL_STREAM_STATE_CREATED_KHR:
        return "EGL_STREAM_STATE_CREATED_KHR";
    case EGL_STREAM_STATE_CONNECTING_KHR:
        return "EGL_STREAM_STATE_CONNECTING_KHR";
    case EGL_STREAM_STATE_EMPTY_KHR:
        return "EGL_STREAM_STATE_EMPTY_KHR";
    case EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR:
        return "EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR";
    case EGL_STREAM_STATE_OLD_FRAME_AVAILABLE_KHR:
        return "EGL_STREAM_STATE_OLD_FRAME_AVAILABLE_KHR";
    case EGL_STREAM_STATE_DISCONNECTED_KHR:
        return "EGL_STREAM_STATE_DISCONNECTED_KHR";
    default:
        return "Unknown state";
    }
}

static EGLint
xwl_eglstream_get_state(EGLDisplay egl_display, EGLStreamKHR egl_stream)
{
    EGLint state;

    eglQueryStreamKHR(egl_display, egl_stream, EGL_STREAM_STATE_KHR, &state);
    if (!eglQueryStreamKHR(egl_display, egl_stream,
                           EGL_STREAM_STATE_KHR, &state)) {
        EGLint state_error = eglGetError();
        ErrorF("eglstream: Failed to query state - error 0x%X: %s\n",
               state_error, xwl_eglstream_get_error_str(state_error));
        return EGL_FALSE;
    }

    return state;
}


static void
xwl_eglstream_print_error(EGLDisplay egl_display,
                          EGLStreamKHR egl_stream, EGLint error)
{
    ErrorF("eglstream: error 0x%X: %s\n", error,
           xwl_eglstream_get_error_str(error));

    if (error == EGL_BAD_STATE_KHR) {
        EGLint state = xwl_eglstream_get_state(egl_display, egl_stream);
        ErrorF("eglstream: stream state 0x%X: %s\n", state,
               xwl_eglstream_get_stream_state_str(state));
    }
}

static void
xwl_eglstream_consumer_ready_callback(void *data,
                                      struct wl_callback *callback,
                                      uint32_t time)
{
    PixmapPtr pixmap = data;
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);
    struct xwl_screen *xwl_screen = xwl_pixmap->xwl_screen;
    struct xwl_eglstream_private *xwl_eglstream =
        xwl_eglstream_get(xwl_screen);

    xwl_eglstream_destroy_pending_cb(pixmap);

    xwl_glamor_egl_make_current(xwl_screen);

    xwl_pixmap->surface = eglCreateStreamProducerSurfaceKHR(
        xwl_screen->egl_display, xwl_eglstream->config,
        xwl_pixmap->stream, (int[]) {
            EGL_WIDTH,  pixmap->drawable.width,
            EGL_HEIGHT, pixmap->drawable.height,
            EGL_NONE
        });

    if (xwl_pixmap->surface == EGL_NO_SURFACE) {
        ErrorF("eglstream: Failed to create EGLSurface for pixmap\n");
        xwl_eglstream_print_error(xwl_screen->egl_display,
                                  xwl_pixmap->stream, eglGetError());
    } else {
        DebugF("eglstream: completes eglstream for pixmap %p, congrats!\n",
               pixmap);
    }
}

static const struct wl_callback_listener consumer_ready_listener = {
    xwl_eglstream_consumer_ready_callback
};

static void
xwl_eglstream_buffer_release_callback(void *data)
{
    PixmapPtr pixmap = data;
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);

    assert(xwl_pixmap);

    if (xwl_pixmap->wait_for_buffer_release) {
        xwl_pixmap->wait_for_buffer_release = FALSE;
        /* drop the reference we took in the ready callback, freeing if necessary */
        dixDestroyPixmap(pixmap, 0);
    }
}

static const struct wl_buffer_listener xwl_eglstream_buffer_release_listener = {
    xwl_pixmap_buffer_release_cb,
};

static void
xwl_eglstream_create_pixmap_and_stream(struct xwl_screen *xwl_screen,
                                       WindowPtr window, PixmapPtr pixmap)
{
    struct xwl_eglstream_private *xwl_eglstream =
        xwl_eglstream_get(xwl_screen);
    struct xwl_pixmap *xwl_pixmap;
    struct xwl_window *xwl_window = xwl_window_from_window(window);
    struct wl_array stream_attribs;
    int stream_fd = -1;

    xwl_pixmap = calloc(1, sizeof(*xwl_pixmap));
    if (!xwl_pixmap)
        FatalError("Not enough memory to create pixmap\n");
    xwl_pixmap_set_private(pixmap, xwl_pixmap);

    xwl_pixmap->type = XWL_PIXMAP_EGLSTREAM;
    xwl_pixmap->image = EGL_NO_IMAGE;

    xwl_glamor_egl_make_current(xwl_screen);

    xwl_pixmap->wait_for_buffer_release = FALSE;
    xwl_pixmap->xwl_screen = xwl_screen;
    xwl_pixmap->surface = EGL_NO_SURFACE;
    xwl_pixmap->stream = eglCreateStreamKHR(xwl_screen->egl_display, NULL);
    if (xwl_pixmap->stream == EGL_NO_STREAM_KHR) {
        ErrorF("eglstream: Couldn't create EGL stream.\n");
        goto fail;
    }
    stream_fd = eglGetStreamFileDescriptorKHR(xwl_screen->egl_display,
                                              xwl_pixmap->stream);
    if (stream_fd == EGL_NO_FILE_DESCRIPTOR_KHR) {
        ErrorF("eglstream: Couldn't get EGL stream file descriptor.\n");
        goto fail;
    }

    wl_array_init(&stream_attribs);
    xwl_pixmap->buffer =
        wl_eglstream_display_create_stream(xwl_eglstream->display,
                                           pixmap->drawable.width,
                                           pixmap->drawable.height,
                                           stream_fd,
                                           WL_EGLSTREAM_HANDLE_TYPE_FD,
                                           &stream_attribs);
    if (!xwl_pixmap->buffer) {
        ErrorF("eglstream: Failed to create buffer\n");
        goto fail;
    }

    wl_buffer_add_listener(xwl_pixmap->buffer,
                           &xwl_eglstream_buffer_release_listener,
                           pixmap);

    xwl_pixmap_set_buffer_release_cb(pixmap,
                                     xwl_eglstream_buffer_release_callback,
                                     pixmap);

    wl_eglstream_controller_attach_eglstream_consumer(
        xwl_eglstream->controller, xwl_window->surface, xwl_pixmap->buffer);

    xwl_pixmap->pending_cb = wl_display_sync(xwl_screen->display);
    wl_callback_add_listener(xwl_pixmap->pending_cb, &consumer_ready_listener,
                             pixmap);
fail:
    if (stream_fd >= 0)
        close(stream_fd);
}

static Bool
xwl_glamor_eglstream_allow_commits(struct xwl_window *xwl_window)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;
    PixmapPtr pixmap =
        (*xwl_screen->screen->GetWindowPixmap)(xwl_window->window);
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);

    if (xwl_pixmap) {
        if (xwl_pixmap->pending_cb) {
            /* Wait for the compositor to finish connecting the consumer for
             * this eglstream */
            return FALSE;
        }

        if (xwl_pixmap->surface != EGL_NO_SURFACE ||
            xwl_pixmap->type == XWL_PIXMAP_DMA_BUF) {
            return TRUE;
        }

        /* The pending stream got removed, we have a xwl_pixmap and
         * yet we do not have a surface.
         * So something went wrong with the surface creation, retry.
         */
         xwl_eglstream_destroy_pixmap_stream(xwl_pixmap);
    }

    /* Glamor pixmap has no backing stream yet; begin making one and disallow
     * commits until then
     */
    xwl_eglstream_create_pixmap_and_stream(xwl_screen, xwl_window->window,
                                           pixmap);

    return FALSE;
}

static Bool
xwl_glamor_eglstream_post_damage(struct xwl_window *xwl_window,
                                 PixmapPtr pixmap, RegionPtr region)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;
    struct xwl_eglstream_private *xwl_eglstream =
        xwl_eglstream_get(xwl_screen);
    struct xwl_pixmap *xwl_pixmap = xwl_pixmap_get(pixmap);
    BoxPtr box = RegionExtents(region);
    EGLint egl_damage[] = {
        box->x1,           box->y1,
        box->x2 - box->x1, box->y2 - box->y1
    };
    GLint saved_vao;
    int status;

    if (xwl_pixmap->type != XWL_PIXMAP_EGLSTREAM)
        /* This can happen if a client does X11 rendering on a
         * flipping OpenGL or Vulkan window. In that case, we don't
         * need to do the copy below.
         */
        return TRUE;

    /* Unbind the framebuffer BEFORE binding the EGLSurface, otherwise we
     * won't actually draw to it
     */
    xwl_glamor_egl_make_current(xwl_screen);
    glamor_set_alu(xwl_screen->screen, GXcopy);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (eglGetCurrentSurface(EGL_READ) != xwl_pixmap->surface ||
        eglGetCurrentSurface(EGL_DRAW) != xwl_pixmap->surface)
        eglMakeCurrent(xwl_screen->egl_display,
                       xwl_pixmap->surface, xwl_pixmap->surface,
                       xwl_screen->egl_context);

    /* Save current GL state */
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &saved_vao);

    /* Setup our GL state */
    glUseProgram(xwl_eglstream->blit_prog);
    glViewport(0, 0, pixmap->drawable.width, pixmap->drawable.height);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(xwl_eglstream->blit_vao);
    glBindTexture(GL_TEXTURE_2D, glamor_get_pixmap_texture(pixmap));

    glUniform1i(xwl_eglstream->blit_is_rgba_pos,
                pixmap->drawable.depth >= 32);

    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        ErrorF("eglstream: Framebuffer incomplete 0x%X, not posting damage\n", status);
        status = FALSE;
        goto out;
    }

    /* Blit rendered image into EGLStream surface */
    glDrawBuffer(GL_BACK);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    if (xwl_eglstream->have_egl_damage)
        status = eglSwapBuffersWithDamageKHR(xwl_screen->egl_display,
                                             xwl_pixmap->surface,
                                             egl_damage, 1);
    else
        status = eglSwapBuffers(xwl_screen->egl_display,
                                xwl_pixmap->surface);

    if (!status) {
        ErrorF("eglstream: buffer swap failed, not posting damage\n");
        goto out;
    }

#ifdef EGL_NV_stream_flush
    if (xwl_eglstream->have_egl_stream_flush)
        /* block until stream state is updated on the compositor's side */
        eglStreamFlushNV(xwl_screen->egl_display,
                         xwl_pixmap->stream);
#endif

    if (!xwl_pixmap->wait_for_buffer_release) {
        /* hang onto the pixmap until the compositor has released it */
        pixmap->refcnt++;
        xwl_pixmap->wait_for_buffer_release = TRUE;
    }

out:
    /* Restore previous state */
    glBindVertexArray(saved_vao);
    glBindTexture(GL_TEXTURE_2D, 0);

    return status;
}

static Bool
xwl_glamor_eglstream_check_flip(PixmapPtr pixmap)
{
    return xwl_pixmap_get(pixmap)->type == XWL_PIXMAP_DMA_BUF;
}

static void
xwl_eglstream_display_handle_caps(void *data,
                                  struct wl_eglstream_display *disp,
                                  int32_t caps)
{
    xwl_eglstream_get(data)->display_caps = caps;
}

static void
xwl_eglstream_display_handle_swapinterval_override(void *data,
                                                   struct wl_eglstream_display *disp,
                                                   int32_t swapinterval,
                                                   struct wl_buffer *stream)
{
}

const struct wl_eglstream_display_listener eglstream_display_listener = {
    .caps = xwl_eglstream_display_handle_caps,
    .swapinterval_override = xwl_eglstream_display_handle_swapinterval_override,
};

static Bool
xwl_glamor_eglstream_init_wl_registry(struct xwl_screen *xwl_screen,
                                      struct wl_registry *wl_registry,
                                      uint32_t id, const char *name,
                                      uint32_t version)
{
    struct xwl_eglstream_private *xwl_eglstream =
        xwl_eglstream_get(xwl_screen);

    if (strcmp(name, "wl_eglstream_display") == 0) {
        xwl_eglstream->display = wl_registry_bind(
            wl_registry, id, &wl_eglstream_display_interface, version);

        wl_eglstream_display_add_listener(xwl_eglstream->display,
                                          &eglstream_display_listener,
                                          xwl_screen);
        return TRUE;
    } else if (strcmp(name, "wl_eglstream_controller") == 0) {
        xwl_eglstream->controller = wl_registry_bind(
            wl_registry, id, &wl_eglstream_controller_interface, version);
        return TRUE;
    } else if (strcmp(name, "zwp_linux_dmabuf_v1") == 0) {
        xwl_screen_set_dmabuf_interface(xwl_screen, id, version);
        return TRUE;
    }

    /* no match */
    return FALSE;
}

static Bool
xwl_glamor_eglstream_has_wl_interfaces(struct xwl_screen *xwl_screen)
{
    struct xwl_eglstream_private *xwl_eglstream =
        xwl_eglstream_get(xwl_screen);

    if (xwl_eglstream->display == NULL) {
        LogMessageVerb(X_INFO, 3,
                       "glamor: 'wl_eglstream_display' not supported\n");
        return FALSE;
    }

    if (xwl_eglstream->controller == NULL) {
        LogMessageVerb(X_INFO, 3,
                       "glamor: 'wl_eglstream_controller' not supported\n");
        return FALSE;
    }

    return TRUE;
}

static inline void
xwl_eglstream_init_shaders(struct xwl_screen *xwl_screen)
{
    struct xwl_eglstream_private *xwl_eglstream =
        xwl_eglstream_get(xwl_screen);
    GLint fs, vs, attrib;
    GLuint vbo;

    const char *blit_vs_src =
        "attribute vec2 texcoord;\n"
        "attribute vec2 position;\n"
        "varying vec2 t;\n"
        "void main() {\n"
        "    t = texcoord;\n"
        "    gl_Position = vec4(position, 0, 1);\n"
        "}";

    const char *blit_fs_src =
        "varying vec2 t;\n"
        "uniform sampler2D s;\n"
        "uniform bool is_rgba;\n"
        "void main() {\n"
        "    if (is_rgba)\n"
        "        gl_FragColor = texture2D(s, t);\n"
        "    else\n"
        "        gl_FragColor = vec4(texture2D(s, t).rgb, 1.0);\n"
        "}";

    static const float position[] = {
        /* position */
        -1, -1,
         1, -1,
         1,  1,
        -1,  1,
        /* texcoord */
         0,  1,
         1,  1,
         1,  0,
         0,  0,
    };

    vs = glamor_compile_glsl_prog(GL_VERTEX_SHADER, blit_vs_src);
    fs = glamor_compile_glsl_prog(GL_FRAGMENT_SHADER, blit_fs_src);

    xwl_eglstream->blit_prog = xwl_eglstream_build_glsl_prog(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    /* Create the blitter's vao */
    glGenVertexArrays(1, &xwl_eglstream->blit_vao);
    glBindVertexArray(xwl_eglstream->blit_vao);

    /* Set the data for both position and texcoord in the vbo */
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(position), position, GL_STATIC_DRAW);
    xwl_eglstream->blit_vbo = vbo;

    /* Define each shader attribute's data location in our vbo */
    attrib = glGetAttribLocation(xwl_eglstream->blit_prog, "position");
    glVertexAttribPointer(attrib, 2, GL_FLOAT, TRUE, 0, NULL);
    glEnableVertexAttribArray(attrib);

    attrib = glGetAttribLocation(xwl_eglstream->blit_prog, "texcoord");
    glVertexAttribPointer(attrib, 2, GL_FLOAT, TRUE, 0,
                          (void*)(sizeof(float) * 8));
    glEnableVertexAttribArray(attrib);

    /* Save the location of uniforms we'll set later */
    xwl_eglstream->blit_is_rgba_pos =
        glGetUniformLocation(xwl_eglstream->blit_prog, "is_rgba");
}

static int
xwl_dri3_open_client(ClientPtr client,
                     ScreenPtr screen,
                     RRProviderPtr provider,
                     int *pfd)
{
    /* Not supported with this backend. */
    return BadImplementation;
}

static PixmapPtr
xwl_dri3_pixmap_from_fds(ScreenPtr screen,
                         CARD8 num_fds, const int *fds,
                         CARD16 width, CARD16 height,
                         const CARD32 *strides, const CARD32 *offsets,
                         CARD8 depth, CARD8 bpp,
                         uint64_t modifier)
{
    PixmapPtr pixmap;
    struct xwl_screen *xwl_screen = xwl_screen_get(screen);
    struct xwl_pixmap *xwl_pixmap;
    unsigned int texture;
    EGLint image_attribs[48];
    uint32_t mod_hi = modifier >> 32, mod_lo = modifier & 0xffffffff, format;
    int attrib = 0, i;
    struct zwp_linux_buffer_params_v1 *params;

    format = wl_drm_format_for_depth(depth);
    if (!xwl_glamor_is_modifier_supported(xwl_screen, format, modifier)) {
        ErrorF("glamor: unsupported format modifier\n");
        return NULL;
    }

    xwl_pixmap = calloc(1, sizeof (*xwl_pixmap));
    if (!xwl_pixmap)
        return NULL;
    xwl_pixmap->type = XWL_PIXMAP_DMA_BUF;
    xwl_pixmap->xwl_screen = xwl_screen;

    xwl_pixmap->buffer = NULL;
    xwl_pixmap->stream = EGL_NO_STREAM_KHR;
    xwl_pixmap->surface = EGL_NO_SURFACE;

    params = zwp_linux_dmabuf_v1_create_params(xwl_screen->dmabuf);
    for (i = 0; i < num_fds; i++) {
        zwp_linux_buffer_params_v1_add(params, fds[i], i,
                                       offsets[i], strides[i],
                                       mod_hi, mod_lo);
    }
    xwl_pixmap->buffer =
        zwp_linux_buffer_params_v1_create_immed(params, width, height,
                                                format, 0);
    zwp_linux_buffer_params_v1_destroy(params);


    image_attribs[attrib++] = EGL_WIDTH;
    image_attribs[attrib++] = width;
    image_attribs[attrib++] = EGL_HEIGHT;
    image_attribs[attrib++] = height;
    image_attribs[attrib++] = EGL_LINUX_DRM_FOURCC_EXT;
    image_attribs[attrib++] = drm_format_for_depth(depth, bpp);

    if (num_fds > 0) {
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE0_FD_EXT;
        image_attribs[attrib++] = fds[0];
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
        image_attribs[attrib++] = offsets[0];
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
        image_attribs[attrib++] = strides[0];
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
        image_attribs[attrib++] = mod_hi;
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
        image_attribs[attrib++] = mod_lo;
    }
    if (num_fds > 1) {
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE1_FD_EXT;
        image_attribs[attrib++] = fds[1];
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
        image_attribs[attrib++] = offsets[1];
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
        image_attribs[attrib++] = strides[1];
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
        image_attribs[attrib++] = mod_hi;
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
        image_attribs[attrib++] = mod_lo;
    }
    if (num_fds > 2) {
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE2_FD_EXT;
        image_attribs[attrib++] = fds[2];
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
        image_attribs[attrib++] = offsets[2];
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
        image_attribs[attrib++] = strides[2];
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT;
        image_attribs[attrib++] = mod_hi;
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT;
        image_attribs[attrib++] = mod_lo;
    }
    if (num_fds > 3) {
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE3_FD_EXT;
        image_attribs[attrib++] = fds[3];
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE3_OFFSET_EXT;
        image_attribs[attrib++] = offsets[3];
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE3_PITCH_EXT;
        image_attribs[attrib++] = strides[3];
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT;
        image_attribs[attrib++] = mod_hi;
        image_attribs[attrib++] = EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT;
        image_attribs[attrib++] = mod_lo;
    }
    image_attribs[attrib++] = EGL_NONE;

    xwl_glamor_egl_make_current(xwl_screen);

    /* eglCreateImageKHR will close fds */
    xwl_pixmap->image = eglCreateImageKHR(xwl_screen->egl_display,
                                          EGL_NO_CONTEXT,
                                          EGL_LINUX_DMA_BUF_EXT,
                                          NULL, image_attribs);
    if (xwl_pixmap->image == EGL_NO_IMAGE_KHR) {
        ErrorF("eglCreateImageKHR failed!\n");
        if (xwl_pixmap->buffer)
            wl_buffer_destroy(xwl_pixmap->buffer);
        free(xwl_pixmap);
        return NULL;
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, xwl_pixmap->image);
    glBindTexture(GL_TEXTURE_2D, 0);

    pixmap = glamor_create_pixmap(screen, width, height, depth,
                                  GLAMOR_CREATE_PIXMAP_NO_TEXTURE);
    glamor_set_pixmap_texture(pixmap, texture);
    glamor_set_pixmap_type(pixmap, GLAMOR_TEXTURE_DRM);
    wl_buffer_add_listener(xwl_pixmap->buffer,
                           &xwl_eglstream_buffer_release_listener,
                           pixmap);
    xwl_pixmap_set_private(pixmap, xwl_pixmap);

    return pixmap;
}

static const dri3_screen_info_rec xwl_dri3_info = {
    .version = 2,
    .open = NULL,
    .pixmap_from_fds = xwl_dri3_pixmap_from_fds,
    .fds_from_pixmap = NULL,
    .open_client = xwl_dri3_open_client,
    .get_formats = xwl_glamor_get_formats,
    .get_modifiers = xwl_glamor_get_modifiers,
    .get_drawable_modifiers = glamor_get_drawable_modifiers,
};

static Bool
xwl_glamor_eglstream_init_egl(struct xwl_screen *xwl_screen)
{
    struct xwl_eglstream_private *xwl_eglstream =
        xwl_eglstream_get(xwl_screen);
    EGLConfig config;
    const EGLint attrib_list[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_CONTEXT_MAJOR_VERSION_KHR,
        GLAMOR_GL_CORE_VER_MAJOR,
        EGL_CONTEXT_MINOR_VERSION_KHR,
        GLAMOR_GL_CORE_VER_MINOR,
        EGL_CONTEXT_PRIORITY_LEVEL_IMG, EGL_CONTEXT_PRIORITY_HIGH_IMG,
        EGL_NONE
    };
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_STREAM_BIT_KHR,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    int n;

    xwl_screen->egl_display = glamor_egl_get_display(
        EGL_PLATFORM_DEVICE_EXT, xwl_eglstream->egl_device);
    if (!xwl_screen->egl_display)
        goto error;

    if (!eglInitialize(xwl_screen->egl_display, NULL, NULL)) {
        xwl_screen->egl_display = NULL;
        goto error;
    }

    if (!epoxy_has_egl_extension(xwl_screen->egl_display,
                                 "EGL_IMG_context_priority")) {
        ErrorF("EGL_IMG_context_priority not available\n");
        goto error;
    }

    eglChooseConfig(xwl_screen->egl_display, config_attribs, &config, 1, &n);
    if (!n) {
        ErrorF("No acceptable EGL configs found\n");
        goto error;
    }

    xwl_eglstream->config = config;
#if 0
    xwl_screen->formats =
        XWL_FORMAT_RGB565 | XWL_FORMAT_XRGB8888 | XWL_FORMAT_ARGB8888;
#endif

    eglBindAPI(EGL_OPENGL_API);
    xwl_screen->egl_context = eglCreateContext(xwl_screen->egl_display,
                                               EGL_NO_CONFIG_KHR,
                                               EGL_NO_CONTEXT,
                                               attrib_list);
    if (xwl_screen->egl_context == EGL_NO_CONTEXT) {
        ErrorF("Failed to create main EGL context: 0x%x\n", eglGetError());
        goto error;
    }

    if (!eglMakeCurrent(xwl_screen->egl_display,
                        EGL_NO_SURFACE, EGL_NO_SURFACE,
                        xwl_screen->egl_context)) {
        ErrorF("Failed to make EGL context current\n");
        goto error;
    }

    xwl_eglstream->have_egl_damage =
        epoxy_has_egl_extension(xwl_screen->egl_display,
                                "EGL_KHR_swap_buffers_with_damage");
    if (!xwl_eglstream->have_egl_damage)
        ErrorF("Driver lacks EGL_KHR_swap_buffers_with_damage, performance "
               "will be affected\n");

#ifdef EGL_NV_stream_flush
    xwl_eglstream->have_egl_stream_flush =
        epoxy_has_egl_extension(xwl_screen->egl_display,
                                "EGL_NV_stream_flush");
#else
    xwl_eglstream->have_egl_stream_flush = FALSE;
#endif /* EGL_NV_stream_flush */

    if (!xwl_eglstream->have_egl_stream_flush)
        ErrorF("EGL_NV_stream_flush not available, "
               "this may cause visible corruption.\n");

    xwl_eglstream_init_shaders(xwl_screen);

    if (epoxy_has_gl_extension("GL_OES_EGL_image")) {
        if (dri3_screen_init(xwl_screen->screen, &xwl_dri3_info))
            xwl_screen->glvnd_vendor = "nvidia";
        else
            ErrorF("DRI3 initialization failed. Performance will be affected.\n");
    } else {
        ErrorF("Driver lacks GL_OES_EGL_image, performance will be affected.\n");
    }

    return TRUE;
error:
    xwl_eglstream_cleanup(xwl_screen);
    return FALSE;
}

static Bool
xwl_glamor_eglstream_init_screen(struct xwl_screen *xwl_screen)
{
    ScreenPtr screen = xwl_screen->screen;

    /* We can just let glamor handle CreatePixmap */
    screen->DestroyPixmap = xwl_glamor_eglstream_destroy_pixmap;

    return TRUE;
}

static EGLDeviceEXT
xwl_eglstream_get_device(struct xwl_screen *xwl_screen)
{
    void **devices = NULL;
    const char *exts[] = {
        "EGL_KHR_stream",
        "EGL_KHR_stream_producer_eglsurface",
    };
    int num_devices, i;
    EGLDeviceEXT device = EGL_NO_DEVICE_EXT;

    /* No device specified by the user, so find one ourselves */
    devices = xwl_glamor_egl_get_devices(&num_devices);
    if (!devices)
        goto out;

    for (i = 0; i < num_devices; i++) {
        if (xwl_glamor_egl_device_has_egl_extensions(devices[i], exts,
                                                     ARRAY_SIZE(exts))) {
            device = devices[i];
            break;
        }
    }

    free(devices);
out:
    if (!device)
        LogMessageVerb(X_INFO, 3, "glamor: No eglstream capable devices found\n");
    return device;
}

void
xwl_glamor_init_eglstream(struct xwl_screen *xwl_screen)
{
    struct xwl_eglstream_private *xwl_eglstream;
    EGLDeviceEXT egl_device;

    xwl_screen->eglstream_backend.is_available = FALSE;
    egl_device = xwl_eglstream_get_device(xwl_screen);
    if (egl_device == EGL_NO_DEVICE_EXT)
        return;

    if (!dixRegisterPrivateKey(&xwl_eglstream_private_key, PRIVATE_SCREEN, 0))
        return;

    xwl_eglstream = calloc(1, sizeof(*xwl_eglstream));
    if (!xwl_eglstream) {
        ErrorF("Failed to allocate memory required to init EGLStream support\n");
        return;
    }

    dixSetPrivate(&xwl_screen->screen->devPrivates,
                  &xwl_eglstream_private_key, xwl_eglstream);

    xwl_eglstream->egl_device = egl_device;

    xwl_screen->eglstream_backend.init_egl = xwl_glamor_eglstream_init_egl;
    xwl_screen->eglstream_backend.init_wl_registry = xwl_glamor_eglstream_init_wl_registry;
    xwl_screen->eglstream_backend.has_wl_interfaces = xwl_glamor_eglstream_has_wl_interfaces;
    xwl_screen->eglstream_backend.init_screen = xwl_glamor_eglstream_init_screen;
    xwl_screen->eglstream_backend.get_wl_buffer_for_pixmap = xwl_glamor_eglstream_get_wl_buffer_for_pixmap;
    xwl_screen->eglstream_backend.post_damage = xwl_glamor_eglstream_post_damage;
    xwl_screen->eglstream_backend.allow_commits = xwl_glamor_eglstream_allow_commits;
    xwl_screen->eglstream_backend.check_flip = xwl_glamor_eglstream_check_flip;
    xwl_screen->eglstream_backend.is_available = TRUE;
    xwl_screen->eglstream_backend.backend_flags = XWL_EGL_BACKEND_NO_FLAG;
}
