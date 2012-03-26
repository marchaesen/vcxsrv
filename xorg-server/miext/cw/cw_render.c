/*
 * Copyright © 2004 Eric Anholt
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Eric Anholt not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Eric Anholt makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * ERIC ANHOLT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL ERIC ANHOLT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <string.h>

#include "gcstruct.h"
#include "windowstr.h"
#include "cw.h"

#define cwPsDecl(pScreen)	\
    PictureScreenPtr	ps = GetPictureScreen (pScreen);	\
    cwScreenPtr		pCwScreen = getCwScreen (pScreen)

#define cwPicturePrivate					\
    cwPicturePtr    pPicturePrivate = getCwPicture(pPicture)

#define cwSrcPictureDecl							\
    int		    src_picture_x_off, src_picture_y_off;			\
    PicturePtr	    pBackingSrcPicture = cwGetBackingPicture(pSrcPicture,	\
							     &src_picture_x_off,\
							     &src_picture_y_off)

#define cwDstPictureDecl							\
    int		    dst_picture_x_off, dst_picture_y_off;			\
    PicturePtr	    pBackingDstPicture = cwGetBackingPicture(pDstPicture,	\
							     &dst_picture_x_off,\
							     &dst_picture_y_off)

#define cwMskPictureDecl							\
    int		    msk_picture_x_off = 0, msk_picture_y_off = 0;		\
    PicturePtr	    pBackingMskPicture = (!pMskPicture ? 0 :	    		\
					  cwGetBackingPicture(pMskPicture,	\
							      &msk_picture_x_off,\
							      &msk_picture_y_off))

#define cwPsUnwrap(elt) {	\
    ps->elt = pCwScreen->elt;	\
}

#define cwPsWrap(elt,func) {	\
    pCwScreen->elt = ps->elt;	\
    ps->elt = func;		\
}

static cwPicturePtr
cwCreatePicturePrivate(PicturePtr pPicture)
{
    WindowPtr pWindow = (WindowPtr) pPicture->pDrawable;
    PixmapPtr pPixmap = getCwPixmap(pWindow);
    int error;
    cwPicturePtr pPicturePrivate;

    pPicturePrivate = malloc(sizeof(cwPictureRec));
    if (!pPicturePrivate)
        return NULL;

    pPicturePrivate->pBackingPicture = CreatePicture(0, &pPixmap->drawable,
                                                     pPicture->pFormat,
                                                     0, 0, serverClient,
                                                     &error);
    if (!pPicturePrivate->pBackingPicture) {
        free(pPicturePrivate);
        return NULL;
    }

    /*
     * Ensure that this serial number does not match the window's
     */
    pPicturePrivate->serialNumber = pPixmap->drawable.serialNumber;
    pPicturePrivate->stateChanges = (1 << (CPLastBit + 1)) - 1;

    setCwPicture(pPicture, pPicturePrivate);

    return pPicturePrivate;
}

static void
cwDestroyPicturePrivate(PicturePtr pPicture)
{
    cwPicturePrivate;

    if (pPicturePrivate) {
        if (pPicturePrivate->pBackingPicture)
            FreePicture(pPicturePrivate->pBackingPicture, 0);
        free(pPicturePrivate);
        setCwPicture(pPicture, NULL);
    }
}

static PicturePtr
cwGetBackingPicture(PicturePtr pPicture, int *x_off, int *y_off)
{
    cwPicturePrivate;

    if (pPicturePrivate) {
        DrawablePtr pDrawable = pPicture->pDrawable;
        WindowPtr pWindow = (WindowPtr) pDrawable;
        PixmapPtr pPixmap = getCwPixmap(pWindow);

        *x_off = pDrawable->x - pPixmap->screen_x;
        *y_off = pDrawable->y - pPixmap->screen_y;

        return pPicturePrivate->pBackingPicture;
    }
    else {
        *x_off = *y_off = 0;
        return pPicture;
    }
}

static void
cwDestroyPicture(PicturePtr pPicture)
{
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;

    cwPsDecl(pScreen);

    cwPsUnwrap(DestroyPicture);
    cwDestroyPicturePrivate(pPicture);
    (*ps->DestroyPicture) (pPicture);
    cwPsWrap(DestroyPicture, cwDestroyPicture);
}

static void
cwChangePicture(PicturePtr pPicture, Mask mask)
{
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;

    cwPsDecl(pScreen);
    cwPicturePtr pPicturePrivate = getCwPicture(pPicture);

    cwPsUnwrap(ChangePicture);
    (*ps->ChangePicture) (pPicture, mask);
    if (pPicturePrivate)
        pPicturePrivate->stateChanges |= mask;
    cwPsWrap(ChangePicture, cwChangePicture);
}

static void
cwValidatePicture(PicturePtr pPicture, Mask mask)
{
    DrawablePtr pDrawable = pPicture->pDrawable;
    ScreenPtr pScreen = pDrawable->pScreen;

    cwPsDecl(pScreen);
    cwPicturePrivate;

    cwPsUnwrap(ValidatePicture);

    /*
     * Must call ValidatePicture to ensure pPicture->pCompositeClip is valid
     */
    (*ps->ValidatePicture) (pPicture, mask);

    if (!cwDrawableIsRedirWindow(pDrawable)) {
        if (pPicturePrivate)
            cwDestroyPicturePrivate(pPicture);
    }
    else {
        PicturePtr pBackingPicture;
        DrawablePtr pBackingDrawable;
        int x_off, y_off;

        pBackingDrawable = cwGetBackingDrawable(pDrawable, &x_off, &y_off);

        if (pPicturePrivate &&
            pPicturePrivate->pBackingPicture->pDrawable != pBackingDrawable) {
            cwDestroyPicturePrivate(pPicture);
            pPicturePrivate = 0;
        }

        if (!pPicturePrivate) {
            pPicturePrivate = cwCreatePicturePrivate(pPicture);
            if (!pPicturePrivate) {
                cwPsWrap(ValidatePicture, cwValidatePicture);
                return;
            }
        }

        pBackingPicture = pPicturePrivate->pBackingPicture;

        /*
         * Always copy transform and filters because there's no
         * indication of when they've changed
         */
        SetPictureTransform(pBackingPicture, pPicture->transform);

        if (pBackingPicture->filter != pPicture->filter ||
            pPicture->filter_nparams > 0) {
            char *filter = PictureGetFilterName(pPicture->filter);

            SetPictureFilter(pBackingPicture,
                             filter, strlen(filter),
                             pPicture->filter_params, pPicture->filter_nparams);
        }

        pPicturePrivate->stateChanges |= mask;

        if (pPicturePrivate->serialNumber != pDrawable->serialNumber ||
            (pPicturePrivate->
             stateChanges & (CPClipXOrigin | CPClipYOrigin | CPClipMask))) {
            SetPictureClipRegion(pBackingPicture, x_off - pDrawable->x,
                                 y_off - pDrawable->y,
                                 pPicture->pCompositeClip);

            pPicturePrivate->serialNumber = pDrawable->serialNumber;
            pPicturePrivate->stateChanges &=
                ~(CPClipXOrigin | CPClipYOrigin | CPClipMask);
        }

        CopyPicture(pPicture, pPicturePrivate->stateChanges, pBackingPicture);

        ValidatePicture(pBackingPicture);
    }
    cwPsWrap(ValidatePicture, cwValidatePicture);
}

static void
cwComposite(CARD8 op,
            PicturePtr pSrcPicture,
            PicturePtr pMskPicture,
            PicturePtr pDstPicture,
            INT16 xSrc,
            INT16 ySrc,
            INT16 xMsk,
            INT16 yMsk, INT16 xDst, INT16 yDst, CARD16 width, CARD16 height)
{
    ScreenPtr pScreen = pDstPicture->pDrawable->pScreen;

    cwPsDecl(pScreen);
    cwSrcPictureDecl;
    cwMskPictureDecl;
    cwDstPictureDecl;

    cwPsUnwrap(Composite);
    (*ps->Composite) (op, pBackingSrcPicture, pBackingMskPicture,
                      pBackingDstPicture, xSrc + src_picture_x_off,
                      ySrc + src_picture_y_off, xMsk + msk_picture_x_off,
                      yMsk + msk_picture_y_off, xDst + dst_picture_x_off,
                      yDst + dst_picture_y_off, width, height);
    cwPsWrap(Composite, cwComposite);
}

static void
cwCompositeRects(CARD8 op,
                 PicturePtr pDstPicture,
                 xRenderColor * color, int nRect, xRectangle *rects)
{
    ScreenPtr pScreen = pDstPicture->pDrawable->pScreen;

    cwPsDecl(pScreen);
    cwDstPictureDecl;
    int i;

    cwPsUnwrap(CompositeRects);
    for (i = 0; i < nRect; i++) {
        rects[i].x += dst_picture_x_off;
        rects[i].y += dst_picture_y_off;
    }
    (*ps->CompositeRects) (op, pBackingDstPicture, color, nRect, rects);
    cwPsWrap(CompositeRects, cwCompositeRects);
}

static void
cwTrapezoids(CARD8 op,
             PicturePtr pSrcPicture,
             PicturePtr pDstPicture,
             PictFormatPtr maskFormat,
             INT16 xSrc, INT16 ySrc, int ntrap, xTrapezoid * traps)
{
    ScreenPtr pScreen = pDstPicture->pDrawable->pScreen;

    cwPsDecl(pScreen);
    cwSrcPictureDecl;
    cwDstPictureDecl;
    int i;

    cwPsUnwrap(Trapezoids);
    if (dst_picture_x_off || dst_picture_y_off) {
        for (i = 0; i < ntrap; i++) {
            traps[i].top += dst_picture_y_off << 16;
            traps[i].bottom += dst_picture_y_off << 16;
            traps[i].left.p1.x += dst_picture_x_off << 16;
            traps[i].left.p1.y += dst_picture_y_off << 16;
            traps[i].left.p2.x += dst_picture_x_off << 16;
            traps[i].left.p2.y += dst_picture_y_off << 16;
            traps[i].right.p1.x += dst_picture_x_off << 16;
            traps[i].right.p1.y += dst_picture_y_off << 16;
            traps[i].right.p2.x += dst_picture_x_off << 16;
            traps[i].right.p2.y += dst_picture_y_off << 16;
        }
    }
    (*ps->Trapezoids) (op, pBackingSrcPicture, pBackingDstPicture, maskFormat,
                       xSrc + src_picture_x_off, ySrc + src_picture_y_off,
                       ntrap, traps);
    cwPsWrap(Trapezoids, cwTrapezoids);
}

static void
cwTriangles(CARD8 op,
            PicturePtr pSrcPicture,
            PicturePtr pDstPicture,
            PictFormatPtr maskFormat,
            INT16 xSrc, INT16 ySrc, int ntri, xTriangle * tris)
{
    ScreenPtr pScreen = pDstPicture->pDrawable->pScreen;

    cwPsDecl(pScreen);
    cwSrcPictureDecl;
    cwDstPictureDecl;
    int i;

    cwPsUnwrap(Triangles);
    if (dst_picture_x_off || dst_picture_y_off) {
        for (i = 0; i < ntri; i++) {
            tris[i].p1.x += dst_picture_x_off << 16;
            tris[i].p1.y += dst_picture_y_off << 16;
            tris[i].p2.x += dst_picture_x_off << 16;
            tris[i].p2.y += dst_picture_y_off << 16;
            tris[i].p3.x += dst_picture_x_off << 16;
            tris[i].p3.y += dst_picture_y_off << 16;
        }
    }
    (*ps->Triangles) (op, pBackingSrcPicture, pBackingDstPicture, maskFormat,
                      xSrc + src_picture_x_off, ySrc + src_picture_y_off,
                      ntri, tris);
    cwPsWrap(Triangles, cwTriangles);
}

void
cwInitializeRender(ScreenPtr pScreen)
{
    cwPsDecl(pScreen);

    cwPsWrap(DestroyPicture, cwDestroyPicture);
    cwPsWrap(ChangePicture, cwChangePicture);
    cwPsWrap(ValidatePicture, cwValidatePicture);
    cwPsWrap(Composite, cwComposite);
    cwPsWrap(CompositeRects, cwCompositeRects);
    cwPsWrap(Trapezoids, cwTrapezoids);
    cwPsWrap(Triangles, cwTriangles);
    /* There is no need to wrap AddTraps as far as we can tell.  AddTraps can
     * only be done on alpha-only pictures, and we won't be getting
     * alpha-only window pictures, so there's no need to translate.
     */
}

void
cwFiniRender(ScreenPtr pScreen)
{
    cwPsDecl(pScreen);

    cwPsUnwrap(DestroyPicture);
    cwPsUnwrap(ChangePicture);
    cwPsUnwrap(ValidatePicture);
    cwPsUnwrap(Composite);
    cwPsUnwrap(CompositeRects);
    cwPsUnwrap(Trapezoids);
    cwPsUnwrap(Triangles);
}
