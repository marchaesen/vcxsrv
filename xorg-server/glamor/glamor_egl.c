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

#include "glamor_egl.h"

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
    int cpp;
#ifdef GLAMOR_HAS_GBM
    struct gbm_device *gbm;
#endif
    int has_gem;
    int gl_context_depth;
    int dri3_capable;

    CloseScreenProcPtr saved_close_screen;
    DestroyPixmapProcPtr saved_destroy_pixmap;
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

struct gbm_device *
glamor_egl_get_gbm_device(ScreenPtr screen)
{
#ifdef GLAMOR_HAS_GBM
    struct glamor_egl_screen_private *glamor_egl =
        glamor_egl_get_screen_private(xf86ScreenToScrn(screen));
    return glamor_egl->gbm;
#else
    return NULL;
#endif
}

Bool
glamor_egl_create_textured_screen(ScreenPtr screen, int handle, int stride)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    PixmapPtr screen_pixmap;

    screen_pixmap = screen->GetScreenPixmap(screen);

    if (!glamor_egl_create_textured_pixmap(screen_pixmap, handle, stride)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "Failed to create textured screen.");
        return FALSE;
    }
    glamor_set_screen_pixmap(screen_pixmap, NULL);
    return TRUE;
}

Bool
glamor_egl_create_textured_screen_ext(ScreenPtr screen,
                                      int handle,
                                      int stride, PixmapPtr *back_pixmap)
{
    return glamor_egl_create_textured_screen(screen, handle, stride);
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

static void
glamor_egl_set_pixmap_image(PixmapPtr pixmap, EGLImageKHR image)
{
    struct glamor_pixmap_private *pixmap_priv =
        glamor_get_pixmap_private(pixmap);
    EGLImageKHR old;

    old = pixmap_priv->image;
    if (old) {
        ScreenPtr                               screen = pixmap->drawable.pScreen;
        ScrnInfoPtr                             scrn = xf86ScreenToScrn(screen);
        struct glamor_egl_screen_private        *glamor_egl = glamor_egl_get_screen_private(scrn);

        eglDestroyImageKHR(glamor_egl->display, old);
    }
    pixmap_priv->image = image;
}

Bool
glamor_egl_create_textured_pixmap(PixmapPtr pixmap, int handle, int stride)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_screen_private *glamor_priv =
        glamor_get_screen_private(screen);
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
    glamor_egl_set_pixmap_image(pixmap, image);
    ret = TRUE;

 done:
    return ret;
}

Bool
glamor_egl_create_textured_pixmap_from_gbm_bo(PixmapPtr pixmap,
                                              struct gbm_bo *bo)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_screen_private *glamor_priv =
        glamor_get_screen_private(screen);
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
    glamor_egl_set_pixmap_image(pixmap, image);
    ret = TRUE;

 done:
    return ret;
}

#ifdef GLAMOR_HAS_GBM
static void
glamor_get_name_from_bo(int gbm_fd, struct gbm_bo *bo, int *name)
{
    union gbm_bo_handle handle;

    handle = gbm_bo_get_handle(bo);
    if (!glamor_get_flink_name(gbm_fd, handle.u32, name))
        *name = -1;
}
#endif

static Bool
glamor_make_pixmap_exportable(PixmapPtr pixmap)
{
#ifdef GLAMOR_HAS_GBM
    ScreenPtr screen = pixmap->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_egl_screen_private *glamor_egl =
        glamor_egl_get_screen_private(scrn);
    struct glamor_pixmap_private *pixmap_priv =
        glamor_get_pixmap_private(pixmap);
    unsigned width = pixmap->drawable.width;
    unsigned height = pixmap->drawable.height;
    struct gbm_bo *bo;
    PixmapPtr exported;
    GCPtr scratch_gc;

    if (pixmap_priv->image)
        return TRUE;

    if (pixmap->drawable.bitsPerPixel != 32) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "Failed to make %dbpp pixmap exportable\n",
                   pixmap->drawable.bitsPerPixel);
        return FALSE;
    }

    bo = gbm_bo_create(glamor_egl->gbm, width, height,
                       GBM_FORMAT_ARGB8888,
#ifdef GLAMOR_HAS_GBM_LINEAR
                       (pixmap->usage_hint == CREATE_PIXMAP_USAGE_SHARED ?
                        GBM_BO_USE_LINEAR : 0) |
#endif
                       GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
    if (!bo) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "Failed to make %dx%dx%dbpp GBM bo\n",
                   width, height, pixmap->drawable.bitsPerPixel);
        return FALSE;
    }

    exported = screen->CreatePixmap(screen, 0, 0, pixmap->drawable.depth, 0);
    screen->ModifyPixmapHeader(exported, width, height, 0, 0,
                               gbm_bo_get_stride(bo), NULL);
    if (!glamor_egl_create_textured_pixmap_from_gbm_bo(exported, bo)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "Failed to make %dx%dx%dbpp pixmap from GBM bo\n",
                   width, height, pixmap->drawable.bitsPerPixel);
        screen->DestroyPixmap(exported);
        gbm_bo_destroy(bo);
        return FALSE;
    }
    gbm_bo_destroy(bo);

    scratch_gc = GetScratchGC(pixmap->drawable.depth, screen);
    ValidateGC(&pixmap->drawable, scratch_gc);
    scratch_gc->ops->CopyArea(&pixmap->drawable, &exported->drawable,
                              scratch_gc,
                              0, 0, width, height, 0, 0);
    FreeScratchGC(scratch_gc);

    /* Now, swap the tex/gbm/EGLImage/etc. of the exported pixmap into
     * the original pixmap struct.
     */
    glamor_egl_exchange_buffers(pixmap, exported);

    screen->DestroyPixmap(exported);

    return TRUE;
#else
    return FALSE;
#endif
}

struct gbm_bo *
glamor_gbm_bo_from_pixmap(ScreenPtr screen, PixmapPtr pixmap)
{
    struct glamor_egl_screen_private *glamor_egl =
        glamor_egl_get_screen_private(xf86ScreenToScrn(screen));
    struct glamor_pixmap_private *pixmap_priv =
        glamor_get_pixmap_private(pixmap);

    if (!glamor_make_pixmap_exportable(pixmap))
        return NULL;

    return gbm_bo_import(glamor_egl->gbm, GBM_BO_IMPORT_EGL_IMAGE,
                         pixmap_priv->image, 0);
}

int
glamor_egl_dri3_fd_name_from_tex(ScreenPtr screen,
                                 PixmapPtr pixmap,
                                 unsigned int tex,
                                 Bool want_name, CARD16 *stride, CARD32 *size)
{
#ifdef GLAMOR_HAS_GBM
    struct glamor_egl_screen_private *glamor_egl;
    struct gbm_bo *bo;
    int fd = -1;

    glamor_egl = glamor_egl_get_screen_private(xf86ScreenToScrn(screen));

    bo = glamor_gbm_bo_from_pixmap(screen, pixmap);
    if (!bo)
        goto failure;

    pixmap->devKind = gbm_bo_get_stride(bo);

    if (want_name) {
        if (glamor_egl->has_gem)
            glamor_get_name_from_bo(glamor_egl->fd, bo, &fd);
    }
    else {
        fd = gbm_bo_get_fd(bo);
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

_X_EXPORT Bool
glamor_back_pixmap_from_fd(PixmapPtr pixmap,
                           int fd,
                           CARD16 width,
                           CARD16 height,
                           CARD16 stride, CARD8 depth, CARD8 bpp)
{
#ifdef GLAMOR_HAS_GBM
    ScreenPtr screen = pixmap->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_egl_screen_private *glamor_egl;
    struct gbm_bo *bo;
    struct gbm_import_fd_data import_data = { 0 };
    Bool ret;

    glamor_egl = glamor_egl_get_screen_private(scrn);

    if (!glamor_egl->dri3_capable)
        return FALSE;

    if (bpp != 32 || !(depth == 24 || depth == 32) || width == 0 || height == 0)
        return FALSE;

    import_data.fd = fd;
    import_data.width = width;
    import_data.height = height;
    import_data.stride = stride;
    import_data.format = GBM_FORMAT_ARGB8888;
    bo = gbm_bo_import(glamor_egl->gbm, GBM_BO_IMPORT_FD, &import_data, 0);
    if (!bo)
        return FALSE;

    screen->ModifyPixmapHeader(pixmap, width, height, 0, 0, stride, NULL);

    ret = glamor_egl_create_textured_pixmap_from_gbm_bo(pixmap, bo);
    gbm_bo_destroy(bo);
    return ret;
#else
    return FALSE;
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
    PixmapPtr pixmap;
    Bool ret;

    pixmap = screen->CreatePixmap(screen, 0, 0, depth, 0);
    ret = glamor_back_pixmap_from_fd(pixmap, fd, width, height,
                                     stride, depth, bpp);
    if (ret == FALSE) {
        screen->DestroyPixmap(pixmap);
        return NULL;
    }
    return pixmap;
#else
    return NULL;
#endif
}

static Bool
glamor_egl_destroy_pixmap(PixmapPtr pixmap)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_egl_screen_private *glamor_egl =
        glamor_egl_get_screen_private(scrn);
    Bool ret;

    if (pixmap->refcnt == 1) {
        struct glamor_pixmap_private *pixmap_priv =
            glamor_get_pixmap_private(pixmap);

        if (pixmap_priv->image)
            eglDestroyImageKHR(glamor_egl->display, pixmap_priv->image);
    }

    screen->DestroyPixmap = glamor_egl->saved_destroy_pixmap;
    ret = screen->DestroyPixmap(pixmap);
    glamor_egl->saved_destroy_pixmap = screen->DestroyPixmap;
    screen->DestroyPixmap = glamor_egl_destroy_pixmap;

    return ret;
}

_X_EXPORT void
glamor_egl_exchange_buffers(PixmapPtr front, PixmapPtr back)
{
    EGLImageKHR temp;
    struct glamor_pixmap_private *front_priv =
        glamor_get_pixmap_private(front);
    struct glamor_pixmap_private *back_priv =
        glamor_get_pixmap_private(back);

    glamor_pixmap_exchange_fbos(front, back);

    temp = back_priv->image;
    back_priv->image = front_priv->image;
    front_priv->image = temp;

    glamor_set_pixmap_type(front, GLAMOR_TEXTURE_DRM);
    glamor_set_pixmap_type(back, GLAMOR_TEXTURE_DRM);
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

    eglDestroyImageKHR(glamor_egl->display, pixmap_priv->image);
    pixmap_priv->image = NULL;

    screen->CloseScreen = glamor_egl->saved_close_screen;

    return screen->CloseScreen(screen);
}

#ifdef DRI3
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
#endif /* DRI3 */

void
glamor_egl_screen_init(ScreenPtr screen, struct glamor_context *glamor_ctx)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    struct glamor_egl_screen_private *glamor_egl =
        glamor_egl_get_screen_private(scrn);

    glamor_egl->saved_close_screen = screen->CloseScreen;
    screen->CloseScreen = glamor_egl_close_screen;

    glamor_egl->saved_destroy_pixmap = screen->DestroyPixmap;
    screen->DestroyPixmap = glamor_egl_destroy_pixmap;

    glamor_ctx->ctx = glamor_egl->context;
    glamor_ctx->display = glamor_egl->display;

    glamor_ctx->make_current = glamor_egl_make_current;

#ifdef DRI3
    if (glamor_egl->dri3_capable) {
    	glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
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
#endif
}

static void glamor_egl_cleanup(struct glamor_egl_screen_private *glamor_egl)
{
    if (glamor_egl->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(glamor_egl->display,
                       EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        /*
         * Force the next glamor_make_current call to update the context
         * (on hot unplug another GPU may still be using glamor)
         */
        lastGLContext = NULL;
        eglTerminate(glamor_egl->display);
    }
#ifdef GLAMOR_HAS_GBM
    if (glamor_egl->gbm)
        gbm_device_destroy(glamor_egl->gbm);
#endif
    free(glamor_egl->device_path);
    free(glamor_egl);
}

static void
glamor_egl_free_screen(ScrnInfoPtr scrn)
{
    struct glamor_egl_screen_private *glamor_egl;

    glamor_egl = glamor_egl_get_screen_private(scrn);
    if (glamor_egl != NULL) {
        scrn->FreeScreen = glamor_egl->saved_free_screen;
        glamor_egl_cleanup(glamor_egl);
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
    static const EGLint config_attribs_core[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_CONTEXT_MAJOR_VERSION_KHR,
        GLAMOR_GL_CORE_VER_MAJOR,
        EGL_CONTEXT_MINOR_VERSION_KHR,
        GLAMOR_GL_CORE_VER_MINOR,
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
        goto error;
    }

    glamor_egl->display = glamor_egl_get_display(EGL_PLATFORM_GBM_MESA,
                                                 glamor_egl->gbm);
    if (!glamor_egl->display) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR, "eglGetDisplay() failed\n");
        goto error;
    }
#else
    glamor_egl->display = eglGetDisplay((EGLNativeDisplayType) (intptr_t) fd);
#endif

    glamor_egl->has_gem = glamor_egl_check_has_gem(fd);

    if (!eglInitialize
        (glamor_egl->display, &glamor_egl->major, &glamor_egl->minor)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR, "eglInitialize() failed\n");
        glamor_egl->display = EGL_NO_DISPLAY;
        goto error;
    }

#ifndef GLAMOR_GLES2
    eglBindAPI(EGL_OPENGL_API);
#else
    eglBindAPI(EGL_OPENGL_ES_API);
#endif

    version = eglQueryString(glamor_egl->display, EGL_VERSION);
    xf86Msg(X_INFO, "%s: EGL version %s:\n", glamor_name, version);

#define GLAMOR_CHECK_EGL_EXTENSION(EXT)  \
	if (!epoxy_has_egl_extension(glamor_egl->display, "EGL_" #EXT)) {  \
		ErrorF("EGL_" #EXT " required.\n");  \
		goto error;  \
	}

#define GLAMOR_CHECK_EGL_EXTENSIONS(EXT1, EXT2)	 \
	if (!epoxy_has_egl_extension(glamor_egl->display, "EGL_" #EXT1) &&  \
	    !epoxy_has_egl_extension(glamor_egl->display, "EGL_" #EXT2)) {  \
		ErrorF("EGL_" #EXT1 " or EGL_" #EXT2 " required.\n");  \
		goto error;  \
	}

    GLAMOR_CHECK_EGL_EXTENSION(MESA_drm_image);
    GLAMOR_CHECK_EGL_EXTENSION(KHR_gl_renderbuffer_image);
#ifdef GLAMOR_GLES2
    GLAMOR_CHECK_EGL_EXTENSIONS(KHR_surfaceless_context, KHR_surfaceless_gles2);
#else
    GLAMOR_CHECK_EGL_EXTENSIONS(KHR_surfaceless_context,
                                KHR_surfaceless_opengl);
#endif

#ifndef GLAMOR_GLES2
    glamor_egl->context = eglCreateContext(glamor_egl->display,
                                           NULL, EGL_NO_CONTEXT,
                                           config_attribs_core);
#else
    glamor_egl->context = NULL;
#endif
    if (!glamor_egl->context) {
        glamor_egl->context = eglCreateContext(glamor_egl->display,
                                               NULL, EGL_NO_CONTEXT,
                                               config_attribs);
        if (glamor_egl->context == EGL_NO_CONTEXT) {
            xf86DrvMsg(scrn->scrnIndex, X_ERROR, "Failed to create EGL context\n");
            goto error;
        }
    }

    if (!eglMakeCurrent(glamor_egl->display,
                        EGL_NO_SURFACE, EGL_NO_SURFACE, glamor_egl->context)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "Failed to make EGL context current\n");
        goto error;
    }
    /*
     * Force the next glamor_make_current call to set the right context
     * (in case of multiple GPUs using glamor)
     */
    lastGLContext = NULL;
#ifdef GLAMOR_HAS_GBM
    if (epoxy_has_egl_extension(glamor_egl->display,
                                "EGL_KHR_gl_texture_2D_image") &&
        epoxy_has_gl_extension("GL_OES_EGL_image"))
        glamor_egl->dri3_capable = TRUE;
#endif

    glamor_egl->saved_free_screen = scrn->FreeScreen;
    scrn->FreeScreen = glamor_egl_free_screen;
#ifdef GLAMOR_GLES2
    xf86DrvMsg(scrn->scrnIndex, X_INFO, "Using GLES2.\n");
    xf86DrvMsg(scrn->scrnIndex, X_WARNING,
               "Glamor is using GLES2 but GLX needs GL. "
               "Indirect GLX may not work correctly.\n");
#endif
    return TRUE;

error:
    glamor_egl_cleanup(glamor_egl);
    return FALSE;
}

/** Stub to retain compatibility with pre-server-1.16 ABI. */
Bool
glamor_egl_init_textured_pixmap(ScreenPtr screen)
{
    return TRUE;
}
