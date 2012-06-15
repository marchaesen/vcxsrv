/*
 * Copyright © 2006 Red Hat, Inc
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of Red Hat,
 * Inc not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior
 * permission.  Red Hat, Inc makes no representations about the
 * suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * RED HAT, INC DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL RED HAT, INC BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <dlfcn.h>

#include <drm.h>
#include <GL/gl.h>
#include <GL/internal/dri_interface.h>
#include <GL/glxtokens.h>

#include <windowstr.h>
#include <os.h>
#include <damage.h>

#define _XF86DRI_SERVER_
#include <drm_sarea.h>
#include <xf86drm.h>
#include <X11/dri/xf86driproto.h>
#include <xf86str.h>
#include <xf86.h>
#include <dri.h>

#include "servermd.h"

#define DRI_NEW_INTERFACE_ONLY
#include "glxserver.h"
#include "glxutil.h"
#include "glxdricommon.h"

#include "glapitable.h"
#include "glapi.h"
#include "glthread.h"
#include "dispatch.h"
#include "extension_string.h"

typedef struct __GLXDRIscreen __GLXDRIscreen;
typedef struct __GLXDRIcontext __GLXDRIcontext;
typedef struct __GLXDRIdrawable __GLXDRIdrawable;

struct __GLXDRIscreen {
    __GLXscreen base;
    __DRIscreen *driScreen;
    void *driver;

    xf86EnterVTProc *enterVT;
    xf86LeaveVTProc *leaveVT;

    const __DRIcoreExtension *core;
    const __DRIlegacyExtension *legacy;
    const __DRIcopySubBufferExtension *copySubBuffer;
    const __DRIswapControlExtension *swapControl;
    const __DRIconfig **driConfigs;

#ifdef __DRI_TEX_OFFSET
    const __DRItexOffsetExtension *texOffset;
    DRITexOffsetStartProcPtr texOffsetStart;
    DRITexOffsetFinishProcPtr texOffsetFinish;
    __GLXDRIdrawable *texOffsetOverride[16];
    GLuint lastTexOffsetOverride;
#endif

    unsigned char glx_enable_bits[__GLX_EXT_BYTES];
};

struct __GLXDRIcontext {
    __GLXcontext base;
    __DRIcontext *driContext;
    XID hwContextID;
};

struct __GLXDRIdrawable {
    __GLXdrawable base;
    __DRIdrawable *driDrawable;

    /* Pulled in from old __GLXpixmap */
#ifdef __DRI_TEX_OFFSET
    GLint texname;
    __GLXDRIcontext *ctx;
    unsigned long long offset;
    DamagePtr pDamage;
#endif
};

static void
__glXDRIleaveServer(GLboolean rendering)
{
    int i;

    for (i = 0; rendering && i < screenInfo.numScreens; i++) {
        __GLXDRIscreen *const screen =
            (__GLXDRIscreen *) glxGetScreen(screenInfo.screens[i]);
        GLuint lastOverride = screen->lastTexOffsetOverride;

        if (lastOverride) {
            __GLXDRIdrawable **texOffsetOverride = screen->texOffsetOverride;
            int j;

            for (j = 0; j < lastOverride; j++) {
                __GLXDRIdrawable *pGlxPix = texOffsetOverride[j];

                if (pGlxPix && pGlxPix->texname) {
                    pGlxPix->offset =
                        screen->texOffsetStart((PixmapPtr) pGlxPix->base.pDraw);
                }
            }
        }
    }

    DRIBlockHandler(NULL, NULL, NULL);

    for (i = 0; rendering && i < screenInfo.numScreens; i++) {
        __GLXDRIscreen *const screen =
            (__GLXDRIscreen *) glxGetScreen(screenInfo.screens[i]);
        GLuint lastOverride = screen->lastTexOffsetOverride;

        if (lastOverride) {
            __GLXDRIdrawable **texOffsetOverride = screen->texOffsetOverride;
            int j;

            for (j = 0; j < lastOverride; j++) {
                __GLXDRIdrawable *pGlxPix = texOffsetOverride[j];

                if (pGlxPix && pGlxPix->texname) {
                    screen->texOffset->setTexOffset(pGlxPix->ctx->driContext,
                                                    pGlxPix->texname,
                                                    pGlxPix->offset,
                                                    pGlxPix->base.pDraw->depth,
                                                    ((PixmapPtr) pGlxPix->base.
                                                     pDraw)->devKind);
                }
            }
        }
    }
}

static void
__glXDRIenterServer(GLboolean rendering)
{
    int i;

    for (i = 0; rendering && i < screenInfo.numScreens; i++) {
        __GLXDRIscreen *const screen = (__GLXDRIscreen *)
            glxGetScreen(screenInfo.screens[i]);

        if (screen->lastTexOffsetOverride) {
            CALL_Flush(GET_DISPATCH(), ());
            break;
        }
    }

    DRIWakeupHandler(NULL, 0, NULL);
}

static void
__glXDRIdoReleaseTexImage(__GLXDRIscreen * screen, __GLXDRIdrawable * drawable)
{
    GLuint lastOverride = screen->lastTexOffsetOverride;

    if (lastOverride) {
        __GLXDRIdrawable **texOffsetOverride = screen->texOffsetOverride;
        int i;

        for (i = 0; i < lastOverride; i++) {
            if (texOffsetOverride[i] == drawable) {
                if (screen->texOffsetFinish)
                    screen->texOffsetFinish((PixmapPtr) drawable->base.pDraw);

                texOffsetOverride[i] = NULL;

                if (i + 1 == lastOverride) {
                    lastOverride = 0;

                    while (i--) {
                        if (texOffsetOverride[i]) {
                            lastOverride = i + 1;
                            break;
                        }
                    }

                    screen->lastTexOffsetOverride = lastOverride;

                    break;
                }
            }
        }
    }
}

static void
__glXDRIdrawableDestroy(__GLXdrawable * drawable)
{
    __GLXDRIdrawable *private = (__GLXDRIdrawable *) drawable;
    __GLXDRIscreen *screen;
    int i;

    for (i = 0; i < screenInfo.numScreens; i++) {
        screen = (__GLXDRIscreen *) glxGetScreen(screenInfo.screens[i]);
        __glXDRIdoReleaseTexImage(screen, private);
    }

    /* If the X window was destroyed, the dri DestroyWindow hook will
     * aready have taken care of this, so only call if pDraw isn't NULL. */
    if (drawable->pDraw != NULL) {
        screen = (__GLXDRIscreen *) glxGetScreen(drawable->pDraw->pScreen);
        (*screen->core->destroyDrawable) (private->driDrawable);

        __glXenterServer(GL_FALSE);
        DRIDestroyDrawable(drawable->pDraw->pScreen,
                           serverClient, drawable->pDraw);
        __glXleaveServer(GL_FALSE);
    }

    __glXDrawableRelease(drawable);

    free(private);
}

static GLboolean
__glXDRIdrawableSwapBuffers(ClientPtr client, __GLXdrawable * basePrivate)
{
    __GLXDRIdrawable *private = (__GLXDRIdrawable *) basePrivate;
    __GLXDRIscreen *screen =
        (__GLXDRIscreen *) glxGetScreen(basePrivate->pDraw->pScreen);

    (*screen->core->swapBuffers) (private->driDrawable);

    return TRUE;
}

static int
__glXDRIdrawableSwapInterval(__GLXdrawable * baseDrawable, int interval)
{
    __GLXDRIdrawable *draw = (__GLXDRIdrawable *) baseDrawable;
    __GLXDRIscreen *screen =
        (__GLXDRIscreen *) glxGetScreen(baseDrawable->pDraw->pScreen);

    if (screen->swapControl)
        screen->swapControl->setSwapInterval(draw->driDrawable, interval);

    return 0;
}

static void
__glXDRIdrawableCopySubBuffer(__GLXdrawable * basePrivate,
                              int x, int y, int w, int h)
{
    __GLXDRIdrawable *private = (__GLXDRIdrawable *) basePrivate;
    __GLXDRIscreen *screen = (__GLXDRIscreen *)
        glxGetScreen(basePrivate->pDraw->pScreen);

    if (screen->copySubBuffer)
        screen->copySubBuffer->copySubBuffer(private->driDrawable, x, y, w, h);
}

static void
__glXDRIcontextDestroy(__GLXcontext * baseContext)
{
    __GLXDRIcontext *context = (__GLXDRIcontext *) baseContext;
    __GLXDRIscreen *screen = (__GLXDRIscreen *) context->base.pGlxScreen;
    Bool retval;

    screen->core->destroyContext(context->driContext);

    __glXenterServer(GL_FALSE);
    retval = DRIDestroyContext(baseContext->pGlxScreen->pScreen,
                               context->hwContextID);
    __glXleaveServer(GL_FALSE);

    __glXContextDestroy(&context->base);
    free(context);
}

static int
__glXDRIcontextMakeCurrent(__GLXcontext * baseContext)
{
    __GLXDRIcontext *context = (__GLXDRIcontext *) baseContext;
    __GLXDRIscreen *screen = (__GLXDRIscreen *) context->base.pGlxScreen;
    __GLXDRIdrawable *draw = (__GLXDRIdrawable *) baseContext->drawPriv;
    __GLXDRIdrawable *read = (__GLXDRIdrawable *) baseContext->readPriv;

    return (*screen->core->bindContext) (context->driContext,
                                         draw->driDrawable, read->driDrawable);
}

static int
__glXDRIcontextLoseCurrent(__GLXcontext * baseContext)
{
    __GLXDRIcontext *context = (__GLXDRIcontext *) baseContext;
    __GLXDRIscreen *screen = (__GLXDRIscreen *) context->base.pGlxScreen;

    return (*screen->core->unbindContext) (context->driContext);
}

static int
__glXDRIcontextCopy(__GLXcontext * baseDst, __GLXcontext * baseSrc,
                    unsigned long mask)
{
    __GLXDRIcontext *dst = (__GLXDRIcontext *) baseDst;
    __GLXDRIcontext *src = (__GLXDRIcontext *) baseSrc;
    __GLXDRIscreen *screen = (__GLXDRIscreen *) dst->base.pGlxScreen;

    return (*screen->core->copyContext) (dst->driContext,
                                         src->driContext, mask);
}

static void
glxFillAlphaChannel(CARD32 *pixels, CARD32 rowstride, int width, int height)
{
    int i;
    CARD32 *p, *end;

    rowstride /= 4;

    for (i = 0; i < height; i++) {
        p = pixels;
        end = p + width;
        while (p < end)
            *p++ |= 0xFF000000;
        pixels += rowstride;
    }
}

static Bool
testTexOffset(__GLXDRIscreen * const screen, PixmapPtr pPixmap)
{
    Bool ret;

    if (!screen->texOffsetStart || !screen->texOffset)
        return FALSE;

    __glXenterServer(GL_FALSE);
    ret = screen->texOffsetStart(pPixmap) != ~0ULL;
    __glXleaveServer(GL_FALSE);

    return ret;
}

/*
 * (sticking this here for lack of a better place)
 * Known issues with the GLX_EXT_texture_from_pixmap implementation:
 * - In general we ignore the fbconfig, lots of examples follow
 * - No fbconfig handling for multiple mipmap levels
 * - No fbconfig handling for 1D textures
 * - No fbconfig handling for TEXTURE_TARGET
 * - No fbconfig exposure of Y inversion state
 * - No GenerateMipmapEXT support (due to no FBO support)
 * - No support for anything but 16bpp and 32bpp-sparse pixmaps
 */

static int
__glXDRIbindTexImage(__GLXcontext * baseContext,
                     int buffer, __GLXdrawable * glxPixmap)
{
    RegionPtr pRegion = NULL;
    PixmapPtr pixmap;
    int bpp, override = 0, texname;
    GLenum format, type;
    ScreenPtr pScreen = glxPixmap->pDraw->pScreen;
    __GLXDRIdrawable *driDraw = (__GLXDRIdrawable *) glxPixmap;
    __GLXDRIscreen *const screen = (__GLXDRIscreen *) glxGetScreen(pScreen);

    CALL_GetIntegerv(GET_DISPATCH(), (glxPixmap->target == GL_TEXTURE_2D ?
                                      GL_TEXTURE_BINDING_2D :
                                      GL_TEXTURE_BINDING_RECTANGLE_NV,
                                      &texname));

    if (!texname)
        return __glXError(GLXBadContextState);

    pixmap = (PixmapPtr) glxPixmap->pDraw;

    if (testTexOffset(screen, pixmap)) {
        __GLXDRIdrawable **texOffsetOverride = screen->texOffsetOverride;
        int i, firstEmpty = 16;

        for (i = 0; i < 16; i++) {
            if (texOffsetOverride[i] == driDraw)
                goto alreadyin;

            if (firstEmpty == 16 && !texOffsetOverride[i])
                firstEmpty = i;
        }

        if (firstEmpty == 16) {
            ErrorF("%s: Failed to register texture offset override\n",
                   __func__);
            goto nooverride;
        }

        if (firstEmpty >= screen->lastTexOffsetOverride)
            screen->lastTexOffsetOverride = firstEmpty + 1;

        texOffsetOverride[firstEmpty] = driDraw;

 alreadyin:
        override = 1;

        driDraw->ctx = (__GLXDRIcontext *) baseContext;

        if (texname == driDraw->texname)
            return Success;

        driDraw->texname = texname;

        screen->texOffset->setTexOffset(driDraw->ctx->driContext, texname, 0,
                                        pixmap->drawable.depth,
                                        pixmap->devKind);
    }
 nooverride:

    if (!driDraw->pDamage) {
        if (!override) {
            driDraw->pDamage = DamageCreate(NULL, NULL, DamageReportNone,
                                            TRUE, pScreen, NULL);
            if (!driDraw->pDamage)
                return BadAlloc;

            DamageRegister((DrawablePtr) pixmap, driDraw->pDamage);
        }

        pRegion = NULL;
    }
    else {
        pRegion = DamageRegion(driDraw->pDamage);
        if (RegionNil(pRegion))
            return Success;
    }

    /* XXX 24bpp packed, 8, etc */
    if (pixmap->drawable.depth >= 24) {
        bpp = 4;
        format = GL_BGRA;
        type =
#if X_BYTE_ORDER == X_BIG_ENDIAN
            !override ? GL_UNSIGNED_INT_8_8_8_8_REV :
#endif
            GL_UNSIGNED_BYTE;
    }
    else {
        bpp = 2;
        format = GL_RGB;
        type = GL_UNSIGNED_SHORT_5_6_5;
    }

    if (pRegion == NULL) {
        void *data = NULL;

        if (!override) {
            unsigned pitch = PixmapBytePad(pixmap->drawable.width,
                                           pixmap->drawable.depth);

            data = malloc(pitch * pixmap->drawable.height);

            __glXenterServer(GL_FALSE);
            pScreen->GetImage(&pixmap->drawable, 0 /*pixmap->drawable.x */ ,
                              0 /*pixmap->drawable.y */ ,
                              pixmap->drawable.width,
                              pixmap->drawable.height, ZPixmap, ~0, data);
            __glXleaveServer(GL_FALSE);

            if (pixmap->drawable.depth == 24)
                glxFillAlphaChannel(data,
                                    pitch,
                                    pixmap->drawable.width,
                                    pixmap->drawable.height);

            CALL_PixelStorei(GET_DISPATCH(), (GL_UNPACK_ROW_LENGTH,
                                              pitch / bpp));
            CALL_PixelStorei(GET_DISPATCH(), (GL_UNPACK_SKIP_PIXELS, 0));
            CALL_PixelStorei(GET_DISPATCH(), (GL_UNPACK_SKIP_ROWS, 0));
        }

        CALL_TexImage2D(GET_DISPATCH(),
                        (glxPixmap->target,
                         0,
                         bpp == 4 ? 4 : 3,
                         pixmap->drawable.width,
                         pixmap->drawable.height, 0, format, type, data));

        free(data);
    }
    else if (!override) {
        int i, numRects;
        BoxPtr p;

        numRects = RegionNumRects(pRegion);
        p = RegionRects(pRegion);

        CALL_PixelStorei(GET_DISPATCH(), (GL_UNPACK_SKIP_PIXELS, 0));
        CALL_PixelStorei(GET_DISPATCH(), (GL_UNPACK_SKIP_ROWS, 0));

        for (i = 0; i < numRects; i++) {
            unsigned pitch = PixmapBytePad(p[i].x2 - p[i].x1,
                                           pixmap->drawable.depth);
            void *data = malloc(pitch * (p[i].y2 - p[i].y1));

            __glXenterServer(GL_FALSE);
            pScreen->GetImage(&pixmap->drawable, /*pixmap->drawable.x + */
                              p[i].x1,
                              /*pixmap->drawable.y */ +p[i].y1,
                              p[i].x2 - p[i].x1,
                              p[i].y2 - p[i].y1, ZPixmap, ~0, data);
            __glXleaveServer(GL_FALSE);

            if (pixmap->drawable.depth == 24)
                glxFillAlphaChannel(data,
                                    pitch,
                                    p[i].x2 - p[i].x1, p[i].y2 - p[i].y1);

            CALL_PixelStorei(GET_DISPATCH(), (GL_UNPACK_ROW_LENGTH,
                                              pitch / bpp));

            CALL_TexSubImage2D(GET_DISPATCH(),
                               (glxPixmap->target,
                                0,
                                p[i].x1, p[i].y1,
                                p[i].x2 - p[i].x1, p[i].y2 - p[i].y1,
                                format, type, data));

            free(data);
        }
    }

    if (!override)
        DamageEmpty(driDraw->pDamage);

    return Success;
}

static int
__glXDRIreleaseTexImage(__GLXcontext * baseContext,
                        int buffer, __GLXdrawable * pixmap)
{
    __GLXDRIscreen *screen =
        (__GLXDRIscreen *) glxGetScreen(pixmap->pDraw->pScreen);
    __GLXDRIdrawable *drawable = (__GLXDRIdrawable *) pixmap;

    __glXDRIdoReleaseTexImage(screen, drawable);

    return Success;
}

static __GLXtextureFromPixmap __glXDRItextureFromPixmap = {
    __glXDRIbindTexImage,
    __glXDRIreleaseTexImage
};

static void
__glXDRIscreenDestroy(__GLXscreen * baseScreen)
{
    int i;

    __GLXDRIscreen *screen = (__GLXDRIscreen *) baseScreen;

    screen->core->destroyScreen(screen->driScreen);

    dlclose(screen->driver);

    __glXScreenDestroy(baseScreen);

    if (screen->driConfigs) {
        for (i = 0; screen->driConfigs[i] != NULL; i++)
            free((__DRIconfig **) screen->driConfigs[i]);
        free(screen->driConfigs);
    }

    free(screen);
}

static __GLXcontext *
__glXDRIscreenCreateContext(__GLXscreen * baseScreen,
                            __GLXconfig * glxConfig,
                            __GLXcontext * baseShareContext,
                            unsigned num_attribs,
                            const uint32_t *attribs,
                            int *error)
{
    __GLXDRIscreen *screen = (__GLXDRIscreen *) baseScreen;
    __GLXDRIcontext *context, *shareContext;
    __GLXDRIconfig *config = (__GLXDRIconfig *) glxConfig;
    VisualPtr visual;
    int i;
    GLboolean retval;
    __DRIcontext *driShare;
    drm_context_t hwContext;
    ScreenPtr pScreen = baseScreen->pScreen;

    /* DRI1 cannot support createContextAttribs, so these parameters will
     * never be used.
     */
    (void) num_attribs;
    (void) attribs;
    (void) error;

    shareContext = (__GLXDRIcontext *) baseShareContext;
    if (shareContext)
        driShare = shareContext->driContext;
    else
        driShare = NULL;

    if (baseShareContext && baseShareContext->isDirect)
        return NULL;

    context = calloc(1, sizeof *context);
    if (context == NULL)
        return NULL;

    context->base.destroy = __glXDRIcontextDestroy;
    context->base.makeCurrent = __glXDRIcontextMakeCurrent;
    context->base.loseCurrent = __glXDRIcontextLoseCurrent;
    context->base.copy = __glXDRIcontextCopy;

    context->base.textureFromPixmap = &__glXDRItextureFromPixmap;
    /* Find the requested X visual */
    visual = pScreen->visuals;
    for (i = 0; i < pScreen->numVisuals; i++, visual++)
        if (visual->vid == glxConfig->visualID)
            break;
    if (i == pScreen->numVisuals)
        return NULL;

    context->hwContextID = FakeClientID(0);

    __glXenterServer(GL_FALSE);
    retval = DRICreateContext(baseScreen->pScreen, visual,
                              context->hwContextID, &hwContext);
    __glXleaveServer(GL_FALSE);

    if (!retval)
        return NULL;

    context->driContext = screen->legacy->createNewContext(screen->driScreen, config->driConfig, 0,     /* render type */
                                                           driShare,
                                                           hwContext, context);

    if (context->driContext == NULL) {
        __glXenterServer(GL_FALSE);
        retval = DRIDestroyContext(baseScreen->pScreen, context->hwContextID);
        __glXleaveServer(GL_FALSE);
        free(context);
        return NULL;
    }

    return &context->base;
}

static __GLXdrawable *
__glXDRIscreenCreateDrawable(ClientPtr client,
                             __GLXscreen * screen,
                             DrawablePtr pDraw,
                             XID drawId,
                             int type, XID glxDrawId, __GLXconfig * glxConfig)
{
    __GLXDRIscreen *driScreen = (__GLXDRIscreen *) screen;
    __GLXDRIconfig *config = (__GLXDRIconfig *) glxConfig;
    __GLXDRIdrawable *private;
    GLboolean retval;
    drm_drawable_t hwDrawable;

    private = calloc(1, sizeof *private);
    if (private == NULL)
        return NULL;

    if (!__glXDrawableInit(&private->base, screen,
                           pDraw, type, glxDrawId, glxConfig)) {
        free(private);
        return NULL;
    }

    private->base.destroy = __glXDRIdrawableDestroy;
    private->base.swapBuffers = __glXDRIdrawableSwapBuffers;
    private->base.copySubBuffer = __glXDRIdrawableCopySubBuffer;
    private->base.waitX = NULL;
    private->base.waitGL = NULL;

    __glXenterServer(GL_FALSE);
    retval = DRICreateDrawable(screen->pScreen, serverClient,
                               pDraw, &hwDrawable);
    __glXleaveServer(GL_FALSE);

    if (!retval) {
        free(private);
        return NULL;
    }

    /* The last argument is 'attrs', which is used with pbuffers which
     * we currently don't support. */

    private->driDrawable =
        (driScreen->legacy->createNewDrawable) (driScreen->driScreen,
                                                config->driConfig,
                                                hwDrawable, 0, NULL, private);

    if (private->driDrawable == NULL) {
        __glXenterServer(GL_FALSE);
        DRIDestroyDrawable(screen->pScreen, serverClient, pDraw);
        __glXleaveServer(GL_FALSE);
        free(private);
        return NULL;
    }

    return &private->base;
}

static GLboolean
getDrawableInfo(__DRIdrawable * driDrawable,
                unsigned int *index, unsigned int *stamp,
                int *x, int *y, int *width, int *height,
                int *numClipRects, drm_clip_rect_t ** ppClipRects,
                int *backX, int *backY,
                int *numBackClipRects, drm_clip_rect_t ** ppBackClipRects,
                void *data)
{
    __GLXDRIdrawable *drawable = data;
    ScreenPtr pScreen;
    drm_clip_rect_t *pClipRects, *pBackClipRects;
    GLboolean retval;
    size_t size;

    /* If the X window has been destroyed, give up here. */
    if (drawable->base.pDraw == NULL)
        return GL_FALSE;

    pScreen = drawable->base.pDraw->pScreen;
    __glXenterServer(GL_FALSE);
    retval = DRIGetDrawableInfo(pScreen, drawable->base.pDraw, index, stamp,
                                x, y, width, height,
                                numClipRects, &pClipRects,
                                backX, backY,
                                numBackClipRects, &pBackClipRects);
    __glXleaveServer(GL_FALSE);

    if (retval && *numClipRects > 0) {
        size = sizeof(drm_clip_rect_t) * *numClipRects;
        *ppClipRects = malloc(size);

        /* Clip cliprects to screen dimensions (redirected windows) */
        if (*ppClipRects != NULL) {
            int i, j;

            for (i = 0, j = 0; i < *numClipRects; i++) {
                (*ppClipRects)[j].x1 = max(pClipRects[i].x1, 0);
                (*ppClipRects)[j].y1 = max(pClipRects[i].y1, 0);
                (*ppClipRects)[j].x2 = min(pClipRects[i].x2, pScreen->width);
                (*ppClipRects)[j].y2 = min(pClipRects[i].y2, pScreen->height);

                if ((*ppClipRects)[j].x1 < (*ppClipRects)[j].x2 &&
                    (*ppClipRects)[j].y1 < (*ppClipRects)[j].y2) {
                    j++;
                }
            }

            if (*numClipRects != j) {
                *numClipRects = j;
                *ppClipRects = realloc(*ppClipRects,
                                       sizeof(drm_clip_rect_t) * *numClipRects);
            }
        }
        else
            *numClipRects = 0;
    }
    else {
        *ppClipRects = NULL;
        *numClipRects = 0;
    }

    if (retval && *numBackClipRects > 0) {
        size = sizeof(drm_clip_rect_t) * *numBackClipRects;
        *ppBackClipRects = malloc(size);
        if (*ppBackClipRects != NULL)
            memcpy(*ppBackClipRects, pBackClipRects, size);
        else
            *numBackClipRects = 0;
    }
    else {
        *ppBackClipRects = NULL;
        *numBackClipRects = 0;
    }

    return retval;
}

static void
__glXReportDamage(__DRIdrawable * driDraw,
                  int x, int y,
                  drm_clip_rect_t * rects, int num_rects,
                  GLboolean front_buffer, void *data)
{
    __GLXDRIdrawable *drawable = data;
    DrawablePtr pDraw = drawable->base.pDraw;
    RegionRec region;

    __glXenterServer(GL_FALSE);

    if (RegionInitBoxes(&region, (BoxPtr) rects, num_rects)) {
        RegionTranslate(&region, pDraw->x, pDraw->y);
        DamageDamageRegion(pDraw, &region);
        RegionUninit(&region);
    }
    else {
        while (num_rects--) {
            RegionInit(&region, (BoxPtr) rects++, 1);
            RegionTranslate(&region, pDraw->x, pDraw->y);
            DamageDamageRegion(pDraw, &region);
            RegionUninit(&region);
        }
    }

    __glXleaveServer(GL_FALSE);
}

static const __DRIgetDrawableInfoExtension getDrawableInfoExtension = {
    {__DRI_GET_DRAWABLE_INFO, __DRI_GET_DRAWABLE_INFO_VERSION},
    getDrawableInfo
};

static const __DRIdamageExtension damageExtension = {
    {__DRI_DAMAGE, __DRI_DAMAGE_VERSION},
    __glXReportDamage,
};

static const __DRIextension *loader_extensions[] = {
    &systemTimeExtension.base,
    &getDrawableInfoExtension.base,
    &damageExtension.base,
    NULL
};

static Bool
glxDRIEnterVT(ScrnInfoPtr scrn)
{
    Bool ret;
    __GLXDRIscreen *screen = (__GLXDRIscreen *)
        glxGetScreen(xf86ScrnToScreen(scrn));

    LogMessage(X_INFO, "AIGLX: Resuming AIGLX clients after VT switch\n");

    scrn->EnterVT = screen->enterVT;

    ret = scrn->EnterVT(scrn);

    screen->enterVT = scrn->EnterVT;
    scrn->EnterVT = glxDRIEnterVT;

    if (!ret)
        return FALSE;

    glxResumeClients();

    return TRUE;
}

static void
glxDRILeaveVT(ScrnInfoPtr scrn)
{
    __GLXDRIscreen *screen = (__GLXDRIscreen *)
        glxGetScreen(xf86ScrnToScreen(scrn));

    LogMessage(X_INFO, "AIGLX: Suspending AIGLX clients for VT switch\n");

    glxSuspendClients();

    scrn->LeaveVT = screen->leaveVT;
    (*screen->leaveVT) (scrn);
    screen->leaveVT = scrn->LeaveVT;
    scrn->LeaveVT = glxDRILeaveVT;
}

static void
initializeExtensions(__GLXDRIscreen * screen)
{
    const __DRIextension **extensions;
    int i;

    extensions = screen->core->getExtensions(screen->driScreen);

    for (i = 0; extensions[i]; i++) {
#ifdef __DRI_READ_DRAWABLE
        if (strcmp(extensions[i]->name, __DRI_READ_DRAWABLE) == 0) {
            __glXEnableExtension(screen->glx_enable_bits,
                                 "GLX_SGI_make_current_read");

            LogMessage(X_INFO, "AIGLX: enabled GLX_SGI_make_current_read\n");
        }
#endif

#ifdef __DRI_COPY_SUB_BUFFER
        if (strcmp(extensions[i]->name, __DRI_COPY_SUB_BUFFER) == 0) {
            screen->copySubBuffer =
                (__DRIcopySubBufferExtension *) extensions[i];
            __glXEnableExtension(screen->glx_enable_bits,
                                 "GLX_MESA_copy_sub_buffer");

            LogMessage(X_INFO, "AIGLX: enabled GLX_MESA_copy_sub_buffer\n");
        }
#endif

#ifdef __DRI_SWAP_CONTROL
        if (strcmp(extensions[i]->name, __DRI_SWAP_CONTROL) == 0) {
            screen->swapControl = (__DRIswapControlExtension *) extensions[i];
            __glXEnableExtension(screen->glx_enable_bits,
                                 "GLX_SGI_swap_control");
            __glXEnableExtension(screen->glx_enable_bits,
                                 "GLX_MESA_swap_control");

            LogMessage(X_INFO,
                       "AIGLX: enabled GLX_SGI_swap_control and GLX_MESA_swap_control\n");
        }
#endif

#ifdef __DRI_TEX_OFFSET
        if (strcmp(extensions[i]->name, __DRI_TEX_OFFSET) == 0) {
            screen->texOffset = (__DRItexOffsetExtension *) extensions[i];
            LogMessage(X_INFO,
                       "AIGLX: enabled GLX_texture_from_pixmap with driver support\n");
        }
#endif
        /* Ignore unknown extensions */
    }
}

static __GLXscreen *
__glXDRIscreenProbe(ScreenPtr pScreen)
{
    drm_handle_t hSAREA;
    drmAddress pSAREA = NULL;
    char *BusID;
    __DRIversion ddx_version;
    __DRIversion dri_version;
    __DRIversion drm_version;
    __DRIframebuffer framebuffer;
    int fd = -1;
    int status;
    drm_magic_t magic;
    drmVersionPtr version;
    int newlyopened;
    char *driverName;
    drm_handle_t hFB;
    int junk;
    __GLXDRIscreen *screen;
    Bool isCapable;
    size_t buffer_size;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

    if (!xf86LoaderCheckSymbol("DRIQueryDirectRenderingCapable") ||
        !DRIQueryDirectRenderingCapable(pScreen, &isCapable) || !isCapable) {
        LogMessage(X_INFO,
                   "AIGLX: Screen %d is not DRI capable\n", pScreen->myNum);
        return NULL;
    }

    screen = calloc(1, sizeof *screen);
    if (screen == NULL)
        return NULL;

    screen->base.destroy = __glXDRIscreenDestroy;
    screen->base.createContext = __glXDRIscreenCreateContext;
    screen->base.createDrawable = __glXDRIscreenCreateDrawable;
    screen->base.swapInterval = __glXDRIdrawableSwapInterval;
    screen->base.pScreen = pScreen;

    __glXInitExtensionEnableBits(screen->glx_enable_bits);

    /* DRI protocol version. */
    dri_version.major = XF86DRI_MAJOR_VERSION;
    dri_version.minor = XF86DRI_MINOR_VERSION;
    dri_version.patch = XF86DRI_PATCH_VERSION;

    if (!DRIOpenConnection(pScreen, &hSAREA, &BusID)) {
        LogMessage(X_ERROR, "AIGLX error: DRIOpenConnection failed\n");
        goto handle_error;
    }

    fd = drmOpenOnce(NULL, BusID, &newlyopened);

    if (fd < 0) {
        LogMessage(X_ERROR, "AIGLX error: drmOpenOnce failed (%s)\n",
                   strerror(-fd));
        goto handle_error;
    }

    if (drmGetMagic(fd, &magic)) {
        LogMessage(X_ERROR, "AIGLX error: drmGetMagic failed\n");
        goto handle_error;
    }

    version = drmGetVersion(fd);
    if (version) {
        drm_version.major = version->version_major;
        drm_version.minor = version->version_minor;
        drm_version.patch = version->version_patchlevel;
        drmFreeVersion(version);
    }
    else {
        drm_version.major = -1;
        drm_version.minor = -1;
        drm_version.patch = -1;
    }

    if (newlyopened && !DRIAuthConnection(pScreen, magic)) {
        LogMessage(X_ERROR, "AIGLX error: DRIAuthConnection failed\n");
        goto handle_error;
    }

    /* Get device name (like "tdfx") and the ddx version numbers.
     * We'll check the version in each DRI driver's "createNewScreen"
     * function. */
    if (!DRIGetClientDriverName(pScreen,
                                &ddx_version.major,
                                &ddx_version.minor,
                                &ddx_version.patch, &driverName)) {
        LogMessage(X_ERROR, "AIGLX error: DRIGetClientDriverName failed\n");
        goto handle_error;
    }

    screen->driver = glxProbeDriver(driverName,
                                    (void **) &screen->core,
                                    __DRI_CORE, __DRI_CORE_VERSION,
                                    (void **) &screen->legacy,
                                    __DRI_LEGACY, __DRI_LEGACY_VERSION);
    if (screen->driver == NULL) {
        goto handle_error;
    }

    /*
     * Get device-specific info.  pDevPriv will point to a struct
     * (such as DRIRADEONRec in xfree86/driver/ati/radeon_dri.h) that
     * has information about the screen size, depth, pitch, ancilliary
     * buffers, DRM mmap handles, etc.
     */
    if (!DRIGetDeviceInfo(pScreen, &hFB, &junk,
                          &framebuffer.size, &framebuffer.stride,
                          &framebuffer.dev_priv_size, &framebuffer.dev_priv)) {
        LogMessage(X_ERROR, "AIGLX error: XF86DRIGetDeviceInfo failed\n");
        goto handle_error;
    }

    framebuffer.width = pScreen->width;
    framebuffer.height = pScreen->height;

    /* Map the framebuffer region. */
    status = drmMap(fd, hFB, framebuffer.size,
                    (drmAddressPtr) & framebuffer.base);
    if (status != 0) {
        LogMessage(X_ERROR, "AIGLX error: drmMap of framebuffer failed (%s)\n",
                   strerror(-status));
        goto handle_error;
    }

    /* Map the SAREA region.  Further mmap regions may be setup in
     * each DRI driver's "createNewScreen" function.
     */
    status = drmMap(fd, hSAREA, SAREA_MAX, &pSAREA);
    if (status != 0) {
        LogMessage(X_ERROR, "AIGLX error: drmMap of SAREA failed (%s)\n",
                   strerror(-status));
        goto handle_error;
    }

    screen->driScreen =
        (*screen->legacy->createNewScreen) (pScreen->myNum,
                                            &ddx_version,
                                            &dri_version,
                                            &drm_version,
                                            &framebuffer,
                                            pSAREA,
                                            fd,
                                            loader_extensions,
                                            &screen->driConfigs, screen);

    if (screen->driScreen == NULL) {
        LogMessage(X_ERROR, "AIGLX error: Calling driver entry point failed\n");
        goto handle_error;
    }

    screen->base.fbconfigs = glxConvertConfigs(screen->core,
                                               screen->driConfigs,
                                               GLX_WINDOW_BIT);

    initializeExtensions(screen);

    DRIGetTexOffsetFuncs(pScreen, &screen->texOffsetStart,
                         &screen->texOffsetFinish);

    __glXScreenInit(&screen->base, pScreen);

    /* The first call simply determines the length of the extension string.
     * This allows us to allocate some memory to hold the extension string,
     * but it requires that we call __glXGetExtensionString a second time.
     */
    buffer_size = __glXGetExtensionString(screen->glx_enable_bits, NULL);
    if (buffer_size > 0) {
        free(screen->base.GLXextensions);

        screen->base.GLXextensions = xnfalloc(buffer_size);
        (void) __glXGetExtensionString(screen->glx_enable_bits,
                                       screen->base.GLXextensions);
    }

    __glXsetEnterLeaveServerFuncs(__glXDRIenterServer, __glXDRIleaveServer);

    screen->enterVT = pScrn->EnterVT;
    pScrn->EnterVT = glxDRIEnterVT;
    screen->leaveVT = pScrn->LeaveVT;
    pScrn->LeaveVT = glxDRILeaveVT;

    LogMessage(X_INFO, "AIGLX: Loaded and initialized %s\n", driverName);

    return &screen->base;

 handle_error:
    if (pSAREA != NULL)
        drmUnmap(pSAREA, SAREA_MAX);

    if (framebuffer.base != NULL)
        drmUnmap((drmAddress) framebuffer.base, framebuffer.size);

    if (fd >= 0)
        drmCloseOnce(fd);

    DRICloseConnection(pScreen);

    if (screen->driver)
        dlclose(screen->driver);

    free(screen);

    LogMessage(X_ERROR, "AIGLX: reverting to software rendering\n");

    return NULL;
}

_X_EXPORT __GLXprovider __glXDRIProvider = {
    __glXDRIscreenProbe,
    "DRI",
    NULL
};
