/*
 * Copyright © 1998 Keith Packard
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

#include <dix-config.h>

#include <stdlib.h>

#include "fb.h"

Bool
fbCreateWindow(WindowPtr pWin)
{
    dixSetPrivate(&pWin->devPrivates, fbGetWinPrivateKey(pWin),
                  fbGetScreenPixmap(pWin->drawable.pScreen));
    return TRUE;
}

Bool
fbDestroyWindow(WindowPtr pWin)
{
    return TRUE;
}

Bool
fbRealizeWindow(WindowPtr pWindow)
{
    return TRUE;
}

Bool
fbPositionWindow(WindowPtr pWin, int x, int y)
{
    return TRUE;
}

Bool
fbUnrealizeWindow(WindowPtr pWindow)
{
    return TRUE;
}

void
fbCopyWindowProc(DrawablePtr pSrcDrawable,
                 DrawablePtr pDstDrawable,
                 GCPtr pGC,
                 BoxPtr pbox,
                 int nbox,
                 int dx,
                 int dy,
                 Bool reverse, Bool upsidedown, Pixel bitplane, void *closure)
{
    FbBits *src;
    FbStride srcStride;
    int srcBpp;
    int srcXoff, srcYoff;
    FbBits *dst;
    FbStride dstStride;
    int dstBpp;
    int numerr = 0;
    int dstXoff, dstYoff;

    fbGetDrawable(pSrcDrawable, src, srcStride, srcBpp, srcXoff, srcYoff);
    fbGetDrawable(pDstDrawable, dst, dstStride, dstBpp, dstXoff, dstYoff);

    while (nbox--)
    {
        int srcYoffset = pbox->y1 + dy + srcYoff;
        int dstYoffset = pbox->y1 + dstYoff;
        int copyLines = (pbox->y2 - pbox->y1);
        int copyLines_src = copyLines;
        int copyLines_dst = copyLines;

        // There is a crash with BitBlt, when moving bits on same source, but shifted for some pixels
        if (srcYoffset >= pSrcDrawable->height ||
            dstYoffset >= pDstDrawable->height )
        {
            numerr++;
            ErrorF("fbCopyWindowProc ERROR\n");
        }
        else
        {
            if (srcYoffset < 0 )
            {
                copyLines_src += srcYoffset+1;
                srcYoffset = 0;
            }
            else if ( (srcYoffset+copyLines) > pSrcDrawable->height )
            {
                copyLines_src = pSrcDrawable->height - srcYoffset;
            }

            if (dstYoffset < 0 )
            {
                copyLines_dst += dstYoffset+1;
                dstYoffset = 0;
            }
            else if ( (dstYoffset+copyLines) > pDstDrawable->height )
            {
                copyLines_dst = pDstDrawable->height - dstYoffset;
            }
            copyLines = copyLines_src < copyLines_dst ? copyLines_src : copyLines_dst;

            fbBlt(src + srcYoffset * srcStride,
                  srcStride,
                  (pbox->x1 + dx + srcXoff) * srcBpp,
                  dst + dstYoffset * dstStride,
                  dstStride,
                  (pbox->x1 + dstXoff) * dstBpp,
                  (pbox->x2 - pbox->x1) * dstBpp,
                  copyLines,
                  GXcopy, FB_ALLONES, dstBpp, reverse, upsidedown);
        }
        pbox++;
    }

    fbFinishAccess(pDstDrawable);
    fbFinishAccess(pSrcDrawable);
}

void
fbCopyWindow(WindowPtr pWin, DDXPointRec ptOldOrg, RegionPtr prgnSrc)
{
    RegionRec rgnDst;
    int dx, dy;

    PixmapPtr pPixmap = fbGetWindowPixmap(pWin);
    DrawablePtr pDrawable = &pPixmap->drawable;

    dx = ptOldOrg.x - pWin->drawable.x;
    dy = ptOldOrg.y - pWin->drawable.y;
    RegionTranslate(prgnSrc, -dx, -dy);

    RegionNull(&rgnDst);

    RegionIntersect(&rgnDst, &pWin->borderClip, prgnSrc);

#if defined(COMPOSITE) || defined(ROOTLESS)
    if (pPixmap->screen_x || pPixmap->screen_y)
        RegionTranslate(&rgnDst, -pPixmap->screen_x, -pPixmap->screen_y);
#endif

    miCopyRegion(pDrawable, pDrawable,
                 0, &rgnDst, dx, dy, fbCopyWindowProc, 0, 0);

    RegionUninit(&rgnDst);
    fbValidateDrawable(&pWin->drawable);
}

static void
fbFixupWindowPixmap(DrawablePtr pDrawable, PixmapPtr *ppPixmap)
{
    PixmapPtr pPixmap = *ppPixmap;

    if (FbEvenTile(pPixmap->drawable.width * pPixmap->drawable.bitsPerPixel))
        fbPadPixmap(pPixmap);
}

Bool
fbChangeWindowAttributes(WindowPtr pWin, unsigned long mask)
{
    if (mask & CWBackPixmap) {
        if (pWin->backgroundState == BackgroundPixmap)
            fbFixupWindowPixmap(&pWin->drawable, &pWin->background.pixmap);
    }
    if (mask & CWBorderPixmap) {
        if (pWin->borderIsPixel == FALSE)
            fbFixupWindowPixmap(&pWin->drawable, &pWin->border.pixmap);
    }
    return TRUE;
}

void
fbFillRegionSolid(DrawablePtr pDrawable,
                  RegionPtr pRegion, FbBits and, FbBits xor)
{
    FbBits *dst;
    FbStride dstStride;
    int dstBpp;
    int dstXoff, dstYoff;
    int n = RegionNumRects(pRegion);
    BoxPtr pbox = RegionRects(pRegion);

#ifndef FB_ACCESS_WRAPPER
    int try_mmx = 0;

    if (!and)
        try_mmx = 1;
#endif

    fbGetDrawable(pDrawable, dst, dstStride, dstBpp, dstXoff, dstYoff);

    while (n--) {
#ifndef FB_ACCESS_WRAPPER
        if (!try_mmx || !pixman_fill((uint32_t *) dst, dstStride, dstBpp,
                                     pbox->x1 + dstXoff, pbox->y1 + dstYoff,
                                     (pbox->x2 - pbox->x1),
                                     (pbox->y2 - pbox->y1), xor)) {
#endif
            fbSolid(dst + (pbox->y1 + dstYoff) * dstStride,
                    dstStride,
                    (pbox->x1 + dstXoff) * dstBpp,
                    dstBpp,
                    (pbox->x2 - pbox->x1) * dstBpp,
                    pbox->y2 - pbox->y1, and, xor);
#ifndef FB_ACCESS_WRAPPER
        }
#endif
        fbValidateDrawable(pDrawable);
        pbox++;
    }

    fbFinishAccess(pDrawable);
}
