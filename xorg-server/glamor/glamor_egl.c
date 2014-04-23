/*
 * Copyright © 2010 Intel Corporation.
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
 *    Zhigang Gong <zhigang.gong@linux.intel.com>
 *
 */

#include "dix-config.h"

#define GLAMOR_FOR_XORG
#include <xorg-server.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <xf86.h>
#include <xf86drm.h>
#define EGL_DISPLAY_NO_X_MESA

#ifdef GLAMOR_HAS_GBM
#include <gbm.h>
#include <drm_fourcc.h>
#endif

#define MESA_EGL_NO_X11_HEADERS
#include <epoxy/gl.h>
#include <epoxy/egl.h>

#include "glamor.h"
#include "glamor_priv.h"
#include "dri3.h"

static const char glamor_name[] = "glamor";

static void
glamor_identify(int flags)
{
    xf86Msg(X_INFO, "%s: OpenGL accelerated X.org driver based.\n",
            glamor_name);
}

struct glamor_egl_screen_private {
    EGLDisplay display;
    EGLContext context;
    EGLint major, minor;
    char *device_path;

    CreateScreenResourcesProcPtr CreateScreenResources;
    CloseScreenProcPtr CloseScreen;
    int fd;
    EGLImageKHR front_image;
    PixmapPtr *back_pixmap;
    int cpp;
#ifdef GLAMOR_HAS_GBM
    struct gbm_device *gbm;
#endif
    int has_gem;
    int gl_context_depth;
    int dri3_capable;

    CloseScreenProcPtr saved_close_screen;
    xf86FreeScreenProc *saved_free_screen;
};

int xf86GlamorEGLPrivateIndex = -1;


static struct glamor_egl_screen_private *
glamor_egl_get_screen_private(ScrnInfoPtr scrn)
{
    return (struct glamor_egl_screen_private *)
        scrn->privates[xf86GlamorEGLPrivateIndex].ptr;
}

static void
glamor_egl_make_current(struct glamor_context *glamor_ctx)
{
    /* There's only a single global dispatch table in Mesa.  EGL, GLX,
     * and AIGLX's direct dispatch table manipulation don't talk to
     * each other.  We need to set the context to NULL first to avoid
     * EGL's no-op context change fast path when switching back to
     * EGL.
     */
    eglMakeCurrent(glamor_ctx->display, EGL_NO_SURFACE,
                   EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if (!eglMakeCurrent(glamor_ctx->display,
                        EGL_NO_SURFACE, EGL_NO_SURFACE,
                        glamor_ctx->ctx)) {
        FatalError("Failed to make EGL context current\n");
    }
}

static EGLImageKHR
_glamor_egl_create_image(struct glamor_egl_screen_private *glamor_egl,
                         int width, int height, int stride, int name, int depth)
{
    EGLImageKHR image;

    EGLint attribs[] = {
        EGL_WIDTH, 0,
        EGL_HEIGHT, 0,
        EGL_DRM_BUFFER_STRIDE_MESA, 0,
        EGL_DRM_BUFFER_FORMAT_MESA,
        EGL_DRM_BUFFER_FORMAT_ARGB32_MESA,
        EGL_DRM_BUFFER_USE_MESA,
        EGL_DRM_BUFFER_USE_SHARE_MESA | EGL_DRM_BUFFER_USE_SCANOUT_MESA,
        EGL_NONE
    };
    attribs[1] = width;
    attribs[3] = height;
    attribs[5] = stride;
    if (depth != 32 && depth != 24)
        return EGL_NO_IMAGE_KHR;
    image = eglCreateImageKHR(glamor_egl->display,
                              glamor_egl->context,
                              EGL_DRM_BUFFER_MESA,
                              (void *) (uintptr_t) name,
                              attribs);
    if (image == EGL_NO_IMAGE_KHR)
        return EGL_NO_IMAGE_KHR;

    return image;
}

static int
glamor_get_flink_name(int fd, int handle, int *name)
{
    struct drm_gem_flink flink;

    flink.handle = handle;
    if (ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink) < 0)
        return FALSE;
    *name = flink.name;
    return TRUE;
}

static Bool
glamor_create_texture_from_image(ScreenPtr screen,
                                 EGLImageKHR image, GLuint * texture)
{
    struct glamor_screen_private *glamor_priv =
        glamor_get_screen_private(screen);

    glamor_make_current(glamor_priv);

    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
    glBindTexture(GL_TEXTURE_2D, 0);

    return TRUE;
}

unsigned int
glamor_egl_create_argb8888_based_texture(ScreenPtr screen, int w, int h)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_egl_screen_private *glamor_egl;
    EGLImageKHR image;
    GLuint texture;

#ifdef GLAMOR_HAS_GBM
    struct gbm_bo *bo;
    EGLNativePixmapType native_pixmap;

    glamor_egl = glamor_egl_get_screen_private(scrn);
    bo = gbm_bo_create(glamor_egl->gbm, w, h, GBM_FORMAT_ARGB8888,
                       GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
    if (!bo)
        return 0;

    /* If the following assignment raises an error or a warning
     * then that means EGLNativePixmapType is not struct gbm_bo *
     * on your platform: This code won't work and you should not
     * compile with dri3 support enabled */
    native_pixmap = bo;

    image = eglCreateImageKHR(glamor_egl->display,
                              EGL_NO_CONTEXT,
                              EGL_NATIVE_PIXMAP_KHR,
                              native_pixmap, NULL);
    gbm_bo_destroy(bo);
    if (image == EGL_NO_IMAGE_KHR)
        return 0;
    glamor_create_texture_from_image(screen, image, &texture);
    eglDestroyImageKHR(glamor_egl->display, image);

    return texture;
#else
    return 0;                   /* this path should never happen */
#endif
}

Bool
glamor_egl_create_textured_screen(ScreenPtr screen, int handle, int stride)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_pixmap_private *pixmap_priv;
    struct glamor_egl_screen_private *glamor_egl;
    PixmapPtr screen_pixmap;

    glamor_egl = glamor_egl_get_screen_private(scrn);
    screen_pixmap = screen->GetScreenPixmap(screen);
    pixmap_priv = glamor_get_pixmap_private(screen_pixmap);

    if (!glamor_egl_create_textured_pixmap(screen_pixmap, handle, stride)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "Failed to create textured screen.");
        return FALSE;
    }

    glamor_egl->front_image = pixmap_priv->base.image;
    glamor_set_screen_pixmap(screen_pixmap, glamor_egl->back_pixmap);
    return TRUE;
}

Bool
glamor_egl_create_textured_screen_ext(ScreenPtr screen,
                                      int handle,
                                      int stride, PixmapPtr *back_pixmap)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_egl_screen_private *glamor_egl;

    glamor_egl = glamor_egl_get_screen_private(scrn);

    glamor_egl->back_pixmap = back_pixmap;
    if (!glamor_egl_create_textured_screen(screen, handle, stride))
        return FALSE;
    return TRUE;
}

static Bool
glamor_egl_check_has_gem(int fd)
{
    struct drm_gem_flink flink;

    flink.handle = 0;

    ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink);
    if (errno == ENOENT || errno == EINVAL)
        return TRUE;
    return FALSE;
}

Bool
glamor_egl_create_textured_pixmap(PixmapPtr pixmap, int handle, int stride)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_screen_private *glamor_priv =
        glamor_get_screen_private(screen);
    struct glamor_pixmap_private *pixmap_priv =
        glamor_get_pixmap_private(pixmap);
    struct glamor_egl_screen_private *glamor_egl;
    EGLImageKHR image;
    GLuint texture;
    int name;
    Bool ret = FALSE;

    glamor_egl = glamor_egl_get_screen_private(scrn);

    glamor_make_current(glamor_priv);
    if (glamor_egl->has_gem) {
        if (!glamor_get_flink_name(glamor_egl->fd, handle, &name)) {
            xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                       "Couldn't flink pixmap handle\n");
            glamor_set_pixmap_type(pixmap, GLAMOR_DRM_ONLY);
            assert(0);
            return FALSE;
        }
    }
    else
        name = handle;

    image = _glamor_egl_create_image(glamor_egl,
                                     pixmap->drawable.width,
                                     pixmap->drawable.height,
                                     ((stride * 8 +
                                       7) / pixmap->drawable.bitsPerPixel),
                                     name, pixmap->drawable.depth);
    if (image == EGL_NO_IMAGE_KHR) {
        glamor_set_pixmap_type(pixmap, GLAMOR_DRM_ONLY);
        goto done;
    }
    glamor_create_texture_from_image(screen, image, &texture);
    glamor_set_pixmap_type(pixmap, GLAMOR_TEXTURE_DRM);
    glamor_set_pixmap_texture(pixmap, texture);
    pixmap_priv->base.image = image;
    ret = TRUE;

 done:
    return ret;
}

Bool
glamor_egl_create_textured_pixmap_from_gbm_bo(PixmapPtr pixmap, void *bo)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_screen_private *glamor_priv =
        glamor_get_screen_private(screen);
    struct glamor_pixmap_private *pixmap_priv =
        glamor_get_pixmap_private(pixmap);
    struct glamor_egl_screen_private *glamor_egl;
    EGLImageKHR image;
    GLuint texture;
    Bool ret = FALSE;

    glamor_egl = glamor_egl_get_screen_private(scrn);

    glamor_make_current(glamor_priv);

    image = eglCreateImageKHR(glamor_egl->display,
                              glamor_egl->context,
                              EGL_NATIVE_PIXMAP_KHR, bo, NULL);
    if (image == EGL_NO_IMAGE_KHR) {
        glamor_set_pixmap_type(pixmap, GLAMOR_DRM_ONLY);
        goto done;
    }
    glamor_create_texture_from_image(screen, image, &texture);
    glamor_set_pixmap_type(pixmap, GLAMOR_TEXTURE_DRM);
    glamor_set_pixmap_texture(pixmap, texture);
    pixmap_priv->base.image = image;
    ret = TRUE;

 done:
    return ret;
}

#ifdef GLAMOR_HAS_GBM
int glamor_get_fd_from_bo(int gbm_fd, struct gbm_bo *bo, int *fd);
void glamor_get_name_from_bo(int gbm_fd, struct gbm_bo *bo, int *name);
int
glamor_get_fd_from_bo(int gbm_fd, struct gbm_bo *bo, int *fd)
{
    union gbm_bo_handle handle;
    struct drm_prime_handle args;

    handle = gbm_bo_get_handle(bo);
    args.handle = handle.u32;
    args.flags = DRM_CLOEXEC;
    if (ioctl(gbm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args))
        return FALSE;
    *fd = args.fd;
    return TRUE;
}

void
glamor_get_name_from_bo(int gbm_fd, struct gbm_bo *bo, int *name)
{
    union gbm_bo_handle handle;

    handle = gbm_bo_get_handle(bo);
    if (!glamor_get_flink_name(gbm_fd, handle.u32, name))
        *name = -1;
}
#endif

int
glamor_egl_dri3_fd_name_from_tex(ScreenPtr screen,
                                 PixmapPtr pixmap,
                                 unsigned int tex,
                                 Bool want_name, CARD16 *stride, CARD32 *size)
{
#ifdef GLAMOR_HAS_GBM
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_pixmap_private *pixmap_priv =
        glamor_get_pixmap_private(pixmap);
    struct glamor_screen_private *glamor_priv =
        glamor_get_screen_private(screen);
    struct glamor_egl_screen_private *glamor_egl;
    EGLImageKHR image;
    struct gbm_bo *bo;
    int fd = -1;

    EGLint attribs[] = {
        EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
        EGL_GL_TEXTURE_LEVEL_KHR, 0,
        EGL_NONE
    };

    glamor_egl = glamor_egl_get_screen_private(scrn);

    glamor_make_current(glamor_priv);

    image = pixmap_priv->base.image;
    if (!image) {
        image = eglCreateImageKHR(glamor_egl->display,
                                  glamor_egl->context,
                                  EGL_GL_TEXTURE_2D_KHR,
                                  (EGLClientBuffer) (uintptr_t)
                                  tex, attribs);
        if (image == EGL_NO_IMAGE_KHR)
            goto failure;

        pixmap_priv->base.image = image;
        glamor_set_pixmap_type(pixmap, GLAMOR_TEXTURE_DRM);
    }

    bo = gbm_bo_import(glamor_egl->gbm, GBM_BO_IMPORT_EGL_IMAGE, image, 0);
    if (!bo)
        goto failure;

    pixmap->devKind = gbm_bo_get_stride(bo);

    if (want_name) {
        if (glamor_egl->has_gem)
            glamor_get_name_from_bo(glamor_egl->fd, bo, &fd);
    }
    else {
        if (glamor_get_fd_from_bo(glamor_egl->fd, bo, &fd)) {
        }
    }
    *stride = pixmap->devKind;
    *size = pixmap->devKind * gbm_bo_get_height(bo);

    gbm_bo_destroy(bo);
 failure:
    return fd;
#else
    return -1;
#endif
}

_X_EXPORT PixmapPtr
glamor_pixmap_from_fd(ScreenPtr screen,
                      int fd,
                      CARD16 width,
                      CARD16 height,
                      CARD16 stride, CARD8 depth, CARD8 bpp)
{
#ifdef GLAMOR_HAS_GBM
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_egl_screen_private *glamor_egl;
    struct gbm_bo *bo;
    EGLImageKHR image;
    PixmapPtr pixmap;
    Bool ret = FALSE;

    EGLint attribs[] = {
        EGL_WIDTH, 0,
        EGL_HEIGHT, 0,
        EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_ARGB8888,
        EGL_DMA_BUF_PLANE0_FD_EXT, 0,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, 0,
        EGL_NONE
    };

    glamor_egl = glamor_egl_get_screen_private(scrn);

    if (!glamor_egl->dri3_capable)
        return NULL;

    if (bpp != 32 || !(depth == 24 || depth == 32) || width == 0 || height == 0)
        return NULL;

    attribs[1] = width;
    attribs[3] = height;
    attribs[7] = fd;
    attribs[11] = stride;
    image = eglCreateImageKHR(glamor_egl->display,
                              EGL_NO_CONTEXT,
                              EGL_LINUX_DMA_BUF_EXT,
                              NULL, attribs);

    if (image == EGL_NO_IMAGE_KHR)
        return NULL;

    /* EGL_EXT_image_dma_buf_import can impose restrictions on the
     * usage of the image. Use gbm_bo to bypass the limitations. */

    bo = gbm_bo_import(glamor_egl->gbm, GBM_BO_IMPORT_EGL_IMAGE, image, 0);
    eglDestroyImageKHR(glamor_egl->display, image);

    if (!bo)
        return NULL;

    pixmap = screen->CreatePixmap(screen, 0, 0, depth, 0);
    screen->ModifyPixmapHeader(pixmap, width, height, 0, 0, stride, NULL);

    ret = glamor_egl_create_textured_pixmap_from_gbm_bo(pixmap, bo);
    gbm_bo_destroy(bo);

    if (ret)
        return pixmap;
    else {
        screen->DestroyPixmap(pixmap);
        return NULL;
    }
#else
    return NULL;
#endif
}

static void
_glamor_egl_destroy_pixmap_image(PixmapPtr pixmap)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pixmap->drawable.pScreen);
    struct glamor_egl_screen_private *glamor_egl =
        glamor_egl_get_screen_private(scrn);
    struct glamor_pixmap_private *pixmap_priv =
        glamor_get_pixmap_private(pixmap);

    if (pixmap_priv->base.image) {
        /* Before destroy an image which was attached to
         * a texture. we must call glFlush to make sure the
         * operation on that texture has been done.*/
        glamor_block_handler(pixmap->drawable.pScreen);
        eglDestroyImageKHR(glamor_egl->display, pixmap_priv->base.image);
        pixmap_priv->base.image = NULL;
    }
}

_X_EXPORT void
glamor_egl_exchange_buffers(PixmapPtr front, PixmapPtr back)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(front->drawable.pScreen);
    struct glamor_egl_screen_private *glamor_egl =
        glamor_egl_get_screen_private(scrn);
    EGLImageKHR temp;
    struct glamor_pixmap_private *front_priv =
        glamor_get_pixmap_private(front);
    struct glamor_pixmap_private *back_priv =
        glamor_get_pixmap_private(back);

    glamor_pixmap_exchange_fbos(front, back);

    temp = back_priv->base.image;
    back_priv->base.image = front_priv->base.image;
    front_priv->base.image = temp;

    glamor_set_pixmap_type(front, GLAMOR_TEXTURE_DRM);
    glamor_set_pixmap_type(back, GLAMOR_TEXTURE_DRM);
    glamor_egl->front_image = front_priv->base.image;

}

void
glamor_egl_destroy_textured_pixmap(PixmapPtr pixmap)
{
    if (pixmap->refcnt == 1)
        _glamor_egl_destroy_pixmap_image(pixmap);
    glamor_destroy_textured_pixmap(pixmap);
}

static Bool
glamor_egl_close_screen(ScreenPtr screen)
{
    ScrnInfoPtr scrn;
    struct glamor_egl_screen_private *glamor_egl;
    struct glamor_pixmap_private *pixmap_priv;
    PixmapPtr screen_pixmap;

    scrn = xf86ScreenToScrn(screen);
    glamor_egl = glamor_egl_get_screen_private(scrn);
    screen_pixmap = screen->GetScreenPixmap(screen);
    pixmap_priv = glamor_get_pixmap_private(screen_pixmap);

    eglDestroyImageKHR(glamor_egl->display, glamor_egl->front_image);
    pixmap_priv->base.image = NULL;
    glamor_egl->front_image = NULL;

    if (glamor_egl->back_pixmap && *glamor_egl->back_pixmap) {
        pixmap_priv = glamor_get_pixmap_private(*glamor_egl->back_pixmap);
        if (pixmap_priv->base.image) {
            eglDestroyImageKHR(glamor_egl->display, pixmap_priv->base.image);
            pixmap_priv->base.image = NULL;
        }
    }

    screen->CloseScreen = glamor_egl->saved_close_screen;

    return screen->CloseScreen(screen);
}

static int
glamor_dri3_open_client(ClientPtr client,
                        ScreenPtr screen,
                        RRProviderPtr provider,
                        int *fdp)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_egl_screen_private *glamor_egl =
        glamor_egl_get_screen_private(scrn);
    int fd;
    drm_magic_t magic;

    fd = open(glamor_egl->device_path, O_RDWR|O_CLOEXEC);
    if (fd < 0)
        return BadAlloc;

    /* Before FD passing in the X protocol with DRI3 (and increased
     * security of rendering with per-process address spaces on the
     * GPU), the kernel had to come up with a way to have the server
     * decide which clients got to access the GPU, which was done by
     * each client getting a unique (magic) number from the kernel,
     * passing it to the server, and the server then telling the
     * kernel which clients were authenticated for using the device.
     *
     * Now that we have FD passing, the server can just set up the
     * authentication on its own and hand the prepared FD off to the
     * client.
     */
    if (drmGetMagic(fd, &magic) < 0) {
        if (errno == EACCES) {
            /* Assume that we're on a render node, and the fd is
             * already as authenticated as it should be.
             */
            *fdp = fd;
            return Success;
        } else {
            close(fd);
            return BadMatch;
        }
    }

    if (drmAuthMagic(glamor_egl->fd, magic) < 0) {
        close(fd);
        return BadMatch;
    }

    *fdp = fd;
    return Success;
}

static dri3_screen_info_rec glamor_dri3_info = {
    .version = 1,
    .open_client = glamor_dri3_open_client,
    .pixmap_from_fd = glamor_pixmap_from_fd,
    .fd_from_pixmap = glamor_fd_from_pixmap,
};

void
glamor_egl_screen_init(ScreenPtr screen, struct glamor_context *glamor_ctx)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    struct glamor_egl_screen_private *glamor_egl =
        glamor_egl_get_screen_private(scrn);

    glamor_egl->saved_close_screen = screen->CloseScreen;
    screen->CloseScreen = glamor_egl_close_screen;

    glamor_ctx->ctx = glamor_egl->context;
    glamor_ctx->display = glamor_egl->display;

    glamor_ctx->make_current = glamor_egl_make_current;

    if (glamor_egl->dri3_capable) {
        /* Tell the core that we have the interfaces for import/export
         * of pixmaps.
         */
        glamor_enable_dri3(screen);

        /* If the driver wants to do its own auth dance (e.g. Xwayland
         * on pre-3.15 kernels that don't have render nodes and thus
         * has the wayland compositor as a master), then it needs us
         * to stay out of the way and let it init DRI3 on its own.
         */
        if (!(glamor_priv->flags & GLAMOR_NO_DRI3)) {
            /* To do DRI3 device FD generation, we need to open a new fd
             * to the same device we were handed in originally.
             */
            glamor_egl->device_path = drmGetDeviceNameFromFd(glamor_egl->fd);

            if (!dri3_screen_init(screen, &glamor_dri3_info)) {
                xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                           "Failed to initialize DRI3.\n");
            }
        }
    }
}

static void
glamor_egl_free_screen(ScrnInfoPtr scrn)
{
    struct glamor_egl_screen_private *glamor_egl;

    glamor_egl = glamor_egl_get_screen_private(scrn);
    if (glamor_egl != NULL) {

        eglMakeCurrent(glamor_egl->display,
                       EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
#ifdef GLAMOR_HAS_GBM
        if (glamor_egl->gbm)
            gbm_device_destroy(glamor_egl->gbm);
#endif
        free(glamor_egl->device_path);

        scrn->FreeScreen = glamor_egl->saved_free_screen;
        free(glamor_egl);
        scrn->FreeScreen(scrn);
    }
}

Bool
glamor_egl_init(ScrnInfoPtr scrn, int fd)
{
    struct glamor_egl_screen_private *glamor_egl;
    const char *version;

    EGLint config_attribs[] = {
#ifdef GLAMOR_GLES2
        EGL_CONTEXT_CLIENT_VERSION, 2,
#endif
        EGL_NONE
    };

    glamor_identify(0);
    glamor_egl = calloc(sizeof(*glamor_egl), 1);
    if (glamor_egl == NULL)
        return FALSE;
    if (xf86GlamorEGLPrivateIndex == -1)
        xf86GlamorEGLPrivateIndex = xf86AllocateScrnInfoPrivateIndex();

    scrn->privates[xf86GlamorEGLPrivateIndex].ptr = glamor_egl;
    glamor_egl->fd = fd;
#ifdef GLAMOR_HAS_GBM
    glamor_egl->gbm = gbm_create_device(glamor_egl->fd);
    if (glamor_egl->gbm == NULL) {
        ErrorF("couldn't get display device\n");
        return FALSE;
    }
    glamor_egl->display = eglGetDisplay(glamor_egl->gbm);
#else
    glamor_egl->display = eglGetDisplay((EGLNativeDisplayType) (intptr_t) fd);
#endif

    glamor_egl->has_gem = glamor_egl_check_has_gem(fd);

#ifndef GLAMOR_GLES2
    eglBindAPI(EGL_OPENGL_API);
#else
    eglBindAPI(EGL_OPENGL_ES_API);
#endif
    if (!eglInitialize
        (glamor_egl->display, &glamor_egl->major, &glamor_egl->minor)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR, "eglInitialize() failed\n");
        return FALSE;
    }

    version = eglQueryString(glamor_egl->display, EGL_VERSION);
    xf86Msg(X_INFO, "%s: EGL version %s:\n", glamor_name, version);

#define GLAMOR_CHECK_EGL_EXTENSION(EXT)  \
	if (!epoxy_has_egl_extension(glamor_egl->display, "EGL_" #EXT)) {  \
		ErrorF("EGL_" #EXT " required.\n");  \
		return FALSE;  \
	}

#define GLAMOR_CHECK_EGL_EXTENSIONS(EXT1, EXT2)	 \
	if (!epoxy_has_egl_extension(glamor_egl->display, "EGL_" #EXT1) &&  \
	    !epoxy_has_egl_extension(glamor_egl->display, "EGL_" #EXT2)) {  \
		ErrorF("EGL_" #EXT1 " or EGL_" #EXT2 " required.\n");  \
		return FALSE;  \
	}

    GLAMOR_CHECK_EGL_EXTENSION(MESA_drm_image);
    GLAMOR_CHECK_EGL_EXTENSION(KHR_gl_renderbuffer_image);
#ifdef GLAMOR_GLES2
    GLAMOR_CHECK_EGL_EXTENSIONS(KHR_surfaceless_context, KHR_surfaceless_gles2);
#else
    GLAMOR_CHECK_EGL_EXTENSIONS(KHR_surfaceless_context,
                                KHR_surfaceless_opengl);
#endif

#ifdef GLAMOR_HAS_GBM
    if (epoxy_has_egl_extension(glamor_egl->display,
                                "EGL_KHR_gl_texture_2D_image") &&
        epoxy_has_egl_extension(glamor_egl->display,
                                "EGL_EXT_image_dma_buf_import"))
        glamor_egl->dri3_capable = TRUE;
#endif

    glamor_egl->context = eglCreateContext(glamor_egl->display,
                                           NULL, EGL_NO_CONTEXT,
                                           config_attribs);
    if (glamor_egl->context == EGL_NO_CONTEXT) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR, "Failed to create EGL context\n");
        return FALSE;
    }

    if (!eglMakeCurrent(glamor_egl->display,
                        EGL_NO_SURFACE, EGL_NO_SURFACE, glamor_egl->context)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "Failed to make EGL context current\n");
        return FALSE;
    }
    glamor_egl->saved_free_screen = scrn->FreeScreen;
    scrn->FreeScreen = glamor_egl_free_screen;
#ifdef GLAMOR_GLES2
    xf86DrvMsg(scrn->scrnIndex, X_INFO, "Using GLES2.\n");
    xf86DrvMsg(scrn->scrnIndex, X_WARNING,
               "Glamor is using GLES2 but GLX needs GL. "
               "Indirect GLX may not work correctly.\n");
#endif
    return TRUE;
}

/** Stub to retain compatibility with pre-server-1.16 ABI. */
Bool
glamor_egl_init_textured_pixmap(ScreenPtr screen)
{
    return TRUE;
}
