/*
 *
 * Copyright © 2000 Keith Packard, member of The XFree86 Project, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <string.h>

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "pixmapstr.h"
#include "windowstr.h"
#include "xf86str.h"
#include "mi.h"
#include "picturestr.h"
#include "glyphstr.h"
#include "picture.h"
#include "mipict.h"
#include "xaa.h"
#include "xaalocal.h"
#include "xaawrap.h"
#include "xaacexp.h"
#include "xf86fbman.h"
#include "servermd.h"

Bool
XAAGetPixelFromRGBA(CARD32 *pixel,
                    CARD16 red,
                    CARD16 green, CARD16 blue, CARD16 alpha, CARD32 format)
{
    int rbits, bbits, gbits, abits;
    int rshift, bshift, gshift, ashift;

    *pixel = 0;

    if (!PICT_FORMAT_COLOR(format))
        return FALSE;

    rbits = PICT_FORMAT_R(format);
    gbits = PICT_FORMAT_G(format);
    bbits = PICT_FORMAT_B(format);
    abits = PICT_FORMAT_A(format);

    if (PICT_FORMAT_TYPE(format) == PICT_TYPE_ARGB) {
        bshift = 0;
        gshift = bbits;
        rshift = gshift + gbits;
        ashift = rshift + rbits;
    }
    else if (PICT_FORMAT_TYPE(format) == PICT_TYPE_ABGR) {
        rshift = 0;
        gshift = rbits;
        bshift = gshift + gbits;
        ashift = bshift + bbits;
    }
    else if (PICT_FORMAT_TYPE(format) == PICT_TYPE_BGRA) {
        bshift = PICT_FORMAT_BPP(format) - bbits;
        gshift = bshift - gbits;
        rshift = gshift - rbits;
        ashift = 0;
    }
    else
        return FALSE;

    *pixel |= (blue >> (16 - bbits)) << bshift;
    *pixel |= (red >> (16 - rbits)) << rshift;
    *pixel |= (green >> (16 - gbits)) << gshift;
    *pixel |= (alpha >> (16 - abits)) << ashift;

    return TRUE;
}

Bool
XAAGetRGBAFromPixel(CARD32 pixel,
                    CARD16 *red,
                    CARD16 *green, CARD16 *blue, CARD16 *alpha, CARD32 format)
{
    int rbits, bbits, gbits, abits;
    int rshift, bshift, gshift, ashift;

    if (!PICT_FORMAT_COLOR(format))
        return FALSE;

    rbits = PICT_FORMAT_R(format);
    gbits = PICT_FORMAT_G(format);
    bbits = PICT_FORMAT_B(format);
    abits = PICT_FORMAT_A(format);

    if (PICT_FORMAT_TYPE(format) == PICT_TYPE_ARGB) {
        bshift = 0;
        gshift = bbits;
        rshift = gshift + gbits;
        ashift = rshift + rbits;
    }
    else if (PICT_FORMAT_TYPE(format) == PICT_TYPE_ABGR) {
        rshift = 0;
        gshift = rbits;
        bshift = gshift + gbits;
        ashift = bshift + bbits;
    }
    else if (PICT_FORMAT_TYPE(format) == PICT_TYPE_BGRA) {
        bshift = PICT_FORMAT_BPP(format) - bbits;
        gshift = bshift - gbits;
        rshift = gshift - rbits;
        ashift = 0;
    }
    else
        return FALSE;

    *red = ((pixel >> rshift) & ((1 << rbits) - 1)) << (16 - rbits);
    while (rbits < 16) {
        *red |= *red >> rbits;
        rbits <<= 1;
    }

    *green = ((pixel >> gshift) & ((1 << gbits) - 1)) << (16 - gbits);
    while (gbits < 16) {
        *green |= *green >> gbits;
        gbits <<= 1;
    }

    *blue = ((pixel >> bshift) & ((1 << bbits) - 1)) << (16 - bbits);
    while (bbits < 16) {
        *blue |= *blue >> bbits;
        bbits <<= 1;
    }

    if (abits) {
        *alpha = ((pixel >> ashift) & ((1 << abits) - 1)) << (16 - abits);
        while (abits < 16) {
            *alpha |= *alpha >> abits;
            abits <<= 1;
        }
    }
    else
        *alpha = 0xffff;

    return TRUE;
}

/* 8:8:8 + PICT_a8 -> 8:8:8:8 texture */

void
XAA_888_plus_PICT_a8_to_8888(CARD32 color, CARD8 *alphaPtr,     /* in bytes */
                             int alphaPitch, CARD32 *dstPtr, int dstPitch,      /* in dwords */
                             int width, int height)
{
    int x;

    color &= 0x00ffffff;

    while (height--) {
        for (x = 0; x < width; x++)
            dstPtr[x] = color | (alphaPtr[x] << 24);
        dstPtr += dstPitch;
        alphaPtr += alphaPitch;
    }
}

#define DRAWABLE_IS_ON_CARD(pDraw) \
    (pDraw->type == DRAWABLE_WINDOW || \
     (pDraw->type == DRAWABLE_PIXMAP && IS_OFFSCREEN_PIXMAP(pDraw)))

Bool
XAADoComposite(CARD8 op,
               PicturePtr pSrc,
               PicturePtr pMask,
               PicturePtr pDst,
               INT16 xSrc,
               INT16 ySrc,
               INT16 xMask,
               INT16 yMask, INT16 xDst, INT16 yDst, CARD16 width, CARD16 height)
{
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    RegionRec region;
    CARD32 *formats, *dstformats;
    int flags = 0;
    BoxPtr pbox;
    int nbox, w, h;

    if (!RegionNumRects(pDst->pCompositeClip))
        return TRUE;

    if (!infoRec->pScrn->vtSema || !DRAWABLE_IS_ON_CARD(pDst->pDrawable))
        return FALSE;

    if (DRAWABLE_IS_ON_CARD(pSrc->pDrawable))
        return FALSE;

    if (pSrc->transform || (pMask && pMask->transform))
        return FALSE;

    if (pDst->alphaMap || pSrc->alphaMap || (pMask && pMask->alphaMap))
        return FALSE;

    if ((pSrc->repeat && pSrc->repeatType != RepeatNormal) ||
        (pMask && pMask->repeat && pMask->repeatType != RepeatNormal)) {
        return FALSE;
    }

    xDst += pDst->pDrawable->x;
    yDst += pDst->pDrawable->y;
    xSrc += pSrc->pDrawable->x;
    ySrc += pSrc->pDrawable->y;

    if (pMask) {
        if (pMask->componentAlpha)
            return FALSE;

        /* for now we only do it if there is a 1x1 (solid) source */

        if ((pSrc->pDrawable->width == 1) && (pSrc->pDrawable->height == 1)) {
            CARD16 red, green, blue, alpha;
            CARD32 pixel =
                *((CARD32 *) (((PixmapPtr) (pSrc->pDrawable))->devPrivate.ptr));

            if (!XAAGetRGBAFromPixel
                (pixel, &red, &green, &blue, &alpha, pSrc->format))
                return FALSE;

            xMask += pMask->pDrawable->x;
            yMask += pMask->pDrawable->y;

            /* pull out color expandable operations here */
            if ((pMask->format == PICT_a1) && (alpha == 0xffff) &&
                (op == PictOpOver) && infoRec->WriteBitmap && !pMask->repeat &&
                !(infoRec->WriteBitmapFlags & NO_TRANSPARENCY) &&
                (!(infoRec->WriteBitmapFlags & RGB_EQUAL) ||
                 ((red == green) && (green == blue)))) {
                PixmapPtr pPix = (PixmapPtr) (pMask->pDrawable);
                int skipleft;

                if (!miComputeCompositeRegion(&region, pSrc, pMask, pDst,
                                              xSrc, ySrc, xMask, yMask, xDst,
                                              yDst, width, height))
                    return TRUE;

                nbox = RegionNumRects(&region);
                pbox = RegionRects(&region);

                if (!nbox)
                    return TRUE;

                XAAGetPixelFromRGBA(&pixel, red, green, blue, 0, pDst->format);

                xMask -= xDst;
                yMask -= yDst;

                while (nbox--) {
                    skipleft = pbox->x1 + xMask;

                    (*infoRec->WriteBitmap) (infoRec->pScrn,
                                             pbox->x1, pbox->y1,
                                             pbox->x2 - pbox->x1,
                                             pbox->y2 - pbox->y1,
                                             (unsigned char *) (pPix->
                                                                devPrivate.
                                                                ptr) +
                                             (pPix->devKind *
                                              (pbox->y1 + yMask)) +
                                             ((skipleft >> 3) & ~3),
                                             pPix->devKind, skipleft & 31,
                                             pixel, -1, GXcopy, ~0);
                    pbox++;
                }

                /* WriteBitmap sets the Sync flag */
                RegionUninit(&region);
                return TRUE;
            }

            formats = infoRec->CPUToScreenAlphaTextureFormats;
            dstformats = infoRec->CPUToScreenAlphaTextureDstFormats;
            if (!formats || !dstformats)
                return FALSE;

            w = pMask->pDrawable->width;
            h = pMask->pDrawable->height;

            if (pMask->repeat) {
                if ((infoRec->CPUToScreenAlphaTextureFlags & XAA_RENDER_NO_TILE)
                    ||
                    ((infoRec->
                      CPUToScreenAlphaTextureFlags &
                      XAA_RENDER_POWER_OF_2_TILE_ONLY) && ((h & (h - 1)) ||
                                                           (w & (w - 1))))) {
                    return FALSE;
                }
                flags |= XAA_RENDER_REPEAT;
            }

            if ((alpha != 0xffff) &&
                (infoRec->
                 CPUToScreenAlphaTextureFlags & XAA_RENDER_NO_SRC_ALPHA))
                return FALSE;

            while (*formats != pMask->format) {
                if (!(*formats))
                    return FALSE;
                formats++;
            }
            while (*dstformats != pDst->format) {
                if (!(*dstformats))
                    return FALSE;
                dstformats++;
            }

            if (!miComputeCompositeRegion(&region, pSrc, pMask, pDst,
                                          xSrc, ySrc, xMask, yMask, xDst, yDst,
                                          width, height))
                return TRUE;

            nbox = RegionNumRects(&region);
            pbox = RegionRects(&region);

            if (!nbox) {
                RegionUninit(&region);
                return TRUE;
            }

            if (!(infoRec->SetupForCPUToScreenAlphaTexture2) (infoRec->pScrn,
                                                              op, red, green,
                                                              blue, alpha,
                                                              pMask->format,
                                                              pDst->format,
                                                              ((PixmapPtr)
                                                               (pMask->
                                                                pDrawable))->
                                                              devPrivate.ptr,
                                                              ((PixmapPtr)
                                                               (pMask->
                                                                pDrawable))->
                                                              devKind, w, h,
                                                              flags)) {
                RegionUninit(&region);
                return FALSE;
            }

            xMask -= xDst;
            yMask -= yDst;

            while (nbox--) {
                (*infoRec->SubsequentCPUToScreenAlphaTexture) (infoRec->pScrn,
                                                               pbox->x1,
                                                               pbox->y1,
                                                               pbox->x1 + xMask,
                                                               pbox->y1 + yMask,
                                                               pbox->x2 -
                                                               pbox->x1,
                                                               pbox->y2 -
                                                               pbox->y1);
                pbox++;
            }

            SET_SYNC_FLAG(infoRec);
            RegionUninit(&region);
            return TRUE;
        }
    }
    else {
        formats = infoRec->CPUToScreenTextureFormats;
        dstformats = infoRec->CPUToScreenTextureDstFormats;
        if (!formats || !dstformats)
            return FALSE;

        w = pSrc->pDrawable->width;
        h = pSrc->pDrawable->height;

        if (pSrc->repeat) {
            if ((infoRec->CPUToScreenTextureFlags & XAA_RENDER_NO_TILE) ||
                ((infoRec->CPUToScreenTextureFlags &
                  XAA_RENDER_POWER_OF_2_TILE_ONLY) &&
                 ((h & (h - 1)) || (w & (w - 1))))) {
                return FALSE;
            }
            flags |= XAA_RENDER_REPEAT;
        }

        while (*formats != pSrc->format) {
            if (!(*formats))
                return FALSE;
            formats++;
        }
        while (*dstformats != pDst->format) {
            if (!(*dstformats))
                return FALSE;
            dstformats++;
        }

        if (!miComputeCompositeRegion(&region, pSrc, pMask, pDst,
                                      xSrc, ySrc, xMask, yMask, xDst, yDst,
                                      width, height))
            return TRUE;

        nbox = RegionNumRects(&region);
        pbox = RegionRects(&region);

        if (!nbox) {
            RegionUninit(&region);
            return TRUE;
        }

        if (!(infoRec->SetupForCPUToScreenTexture2) (infoRec->pScrn,
                                                     op, pSrc->format,
                                                     pDst->format,
                                                     ((PixmapPtr)
                                                      (pSrc->pDrawable))->
                                                     devPrivate.ptr,
                                                     ((PixmapPtr)
                                                      (pSrc->pDrawable))->
                                                     devKind, w, h, flags)) {
            RegionUninit(&region);
            return FALSE;
        }

        xSrc -= xDst;
        ySrc -= yDst;

        while (nbox--) {
            (*infoRec->SubsequentCPUToScreenTexture) (infoRec->pScrn,
                                                      pbox->x1, pbox->y1,
                                                      pbox->x1 + xSrc,
                                                      pbox->y1 + ySrc,
                                                      pbox->x2 - pbox->x1,
                                                      pbox->y2 - pbox->y1);
            pbox++;
        }

        SET_SYNC_FLAG(infoRec);
        RegionUninit(&region);
        return TRUE;
    }

    return FALSE;
}

static void
XAACompositeSrcCopy(PicturePtr pSrc,
                    PicturePtr pDst,
                    INT16 xSrc,
                    INT16 ySrc,
                    INT16 xDst, INT16 yDst, CARD16 width, CARD16 height)
{
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    int i, nbox;
    int xoff, yoff;
    BoxPtr pbox;
    DDXPointPtr pptSrc;
    RegionRec region;

    xDst += pDst->pDrawable->x;
    yDst += pDst->pDrawable->y;
    xSrc += pSrc->pDrawable->x;
    ySrc += pSrc->pDrawable->y;

    if (!miComputeCompositeRegion(&region, pSrc, NULL, pDst,
                                  xSrc, ySrc, 0, 0, xDst, yDst, width, height))
        return;

    nbox = RegionNumRects(&region);
    pbox = RegionRects(&region);

    if (!nbox) {
        RegionUninit(&region);
        return;
    }
    pptSrc = malloc(sizeof(DDXPointRec) * nbox);
    if (!pptSrc) {
        RegionUninit(&region);
        return;
    }
    xoff = xSrc - xDst;
    yoff = ySrc - yDst;
    for (i = 0; i < nbox; i++) {
        pptSrc[i].x = pbox[i].x1 + xoff;
        pptSrc[i].y = pbox[i].y1 + yoff;
    }

    infoRec->ScratchGC.planemask = ~0L;
    infoRec->ScratchGC.alu = GXcopy;

    XAADoBitBlt(pSrc->pDrawable, pDst->pDrawable, &infoRec->ScratchGC, &region,
                pptSrc);

    free(pptSrc);
    RegionUninit(&region);
    return;
}

void
XAAComposite(CARD8 op,
             PicturePtr pSrc,
             PicturePtr pMask,
             PicturePtr pDst,
             INT16 xSrc,
             INT16 ySrc,
             INT16 xMask,
             INT16 yMask, INT16 xDst, INT16 yDst, CARD16 width, CARD16 height)
{
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);

    XAA_RENDER_PROLOGUE(pScreen, Composite);

    if (!pMask && infoRec->pScrn->vtSema &&
        infoRec->ScreenToScreenBitBlt &&
        pSrc->pDrawable &&
        DRAWABLE_IS_ON_CARD(pSrc->pDrawable) &&
        DRAWABLE_IS_ON_CARD(pDst->pDrawable) &&
        !pSrc->transform &&
        (!pSrc->repeat || (xSrc >= 0 && ySrc >= 0 &&
                           xSrc + width <= pSrc->pDrawable->width &&
                           ySrc + height <= pSrc->pDrawable->height)) &&
        ((op == PictOpSrc &&
          ((pSrc->format == pDst->format) ||
           (pSrc->format == PICT_a8r8g8b8 && pDst->format == PICT_x8r8g8b8) ||
           (pSrc->format == PICT_a8b8g8r8 && pDst->format == PICT_x8b8g8r8))) ||
         (op == PictOpOver && !pSrc->alphaMap && !pDst->alphaMap &&
          pSrc->format == pDst->format &&
          (pSrc->format == PICT_x8r8g8b8 || pSrc->format == PICT_x8b8g8r8)))) {
        XAACompositeSrcCopy(pSrc, pDst, xSrc, ySrc, xDst, yDst, width, height);
    }
    else if (!pSrc->pDrawable || (pMask && !pMask->pDrawable) ||
             !infoRec->Composite ||
             !(*infoRec->Composite) (op, pSrc, pMask, pDst,
                                     xSrc, ySrc, xMask, yMask, xDst, yDst,
                                     width, height)) {
        if (infoRec->pScrn->vtSema &&
            ((pSrc->pDrawable &&
              (pSrc->pDrawable->type == DRAWABLE_WINDOW ||
               IS_OFFSCREEN_PIXMAP(pSrc->pDrawable))) ||
             pDst->pDrawable->type == DRAWABLE_WINDOW ||
             IS_OFFSCREEN_PIXMAP(pDst->pDrawable))) {
            SYNC_CHECK(pDst->pDrawable);
        }
        (*GetPictureScreen(pScreen)->Composite) (op,
                                                 pSrc,
                                                 pMask,
                                                 pDst,
                                                 xSrc,
                                                 ySrc,
                                                 xMask,
                                                 yMask,
                                                 xDst, yDst, width, height);
    }

    if (pDst->pDrawable->type == DRAWABLE_PIXMAP)
        (XAA_GET_PIXMAP_PRIVATE((PixmapPtr) (pDst->pDrawable)))->flags |= DIRTY;

    XAA_RENDER_EPILOGUE(pScreen, Composite, XAAComposite);
}

Bool
XAADoGlyphs(CARD8 op,
            PicturePtr pSrc,
            PicturePtr pDst,
            PictFormatPtr maskFormat,
            INT16 xSrc,
            INT16 ySrc, int nlist, GlyphListPtr list, GlyphPtr * glyphs)
{
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);

    if (!RegionNumRects(pDst->pCompositeClip))
        return TRUE;

    if (!infoRec->pScrn->vtSema ||
        ((pDst->pDrawable->type != DRAWABLE_WINDOW) &&
         !IS_OFFSCREEN_PIXMAP(pDst->pDrawable)))
        return FALSE;

    if ((pSrc->pDrawable->type != DRAWABLE_PIXMAP) ||
        IS_OFFSCREEN_PIXMAP(pSrc->pDrawable))
        return FALSE;

    /*
     * If it looks like we have a chance of being able to draw these
     * glyphs with an accelerated Composite, do that now to avoid
     * unneeded and costly syncs.
     */
    if (maskFormat) {
        if (!infoRec->CPUToScreenAlphaTextureFormats)
            return FALSE;
    }
    else {
        if (!infoRec->CPUToScreenTextureFormats)
            return FALSE;
    }

    miGlyphs(op, pSrc, pDst, maskFormat, xSrc, ySrc, nlist, list, glyphs);

    return TRUE;
}

void
XAAGlyphs(CARD8 op,
          PicturePtr pSrc,
          PicturePtr pDst,
          PictFormatPtr maskFormat,
          INT16 xSrc,
          INT16 ySrc, int nlist, GlyphListPtr list, GlyphPtr * glyphs)
{
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);

    XAA_RENDER_PROLOGUE(pScreen, Glyphs);

    if (!pSrc->pDrawable || !infoRec->Glyphs ||
        !(*infoRec->Glyphs) (op, pSrc, pDst, maskFormat,
                             xSrc, ySrc, nlist, list, glyphs)) {
        if (infoRec->pScrn->vtSema &&
            ((pSrc->pDrawable &&
              (pSrc->pDrawable->type == DRAWABLE_WINDOW ||
               IS_OFFSCREEN_PIXMAP(pSrc->pDrawable))) ||
             pDst->pDrawable->type == DRAWABLE_WINDOW ||
             IS_OFFSCREEN_PIXMAP(pDst->pDrawable))) {
            SYNC_CHECK(pDst->pDrawable);
        }
        (*GetPictureScreen(pScreen)->Glyphs) (op, pSrc, pDst, maskFormat,
                                              xSrc, ySrc, nlist, list, glyphs);
    }

    if (pDst->pDrawable->type == DRAWABLE_PIXMAP)
        (XAA_GET_PIXMAP_PRIVATE((PixmapPtr) (pDst->pDrawable)))->flags |= DIRTY;

    XAA_RENDER_EPILOGUE(pScreen, Glyphs, XAAGlyphs);
}
