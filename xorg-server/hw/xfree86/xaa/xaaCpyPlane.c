
/*
   A CopyPlane function that handles bitmap->screen copies and
   sends anything else to the Fallback.

   Also, a PushPixels for solid fill styles.

   Written by Mark Vojkovich (markv@valinux.com)

*/

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <string.h>

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"
#include "servermd.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "mi.h"
#include "pixmapstr.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaalocal.h"
#include "xaawrap.h"

static void XAACopyPlane1toNColorExpand(DrawablePtr pSrc, DrawablePtr pDst,
                                        GCPtr pGC, RegionPtr rgnDst,
                                        DDXPointPtr pptSrc);
static void XAACopyPlaneNtoNColorExpand(DrawablePtr pSrc, DrawablePtr pDst,
                                        GCPtr pGC, RegionPtr rgnDst,
                                        DDXPointPtr pptSrc);

static unsigned long TmpBitPlane;

RegionPtr
XAACopyPlaneColorExpansion(DrawablePtr pSrc,
                           DrawablePtr pDst,
                           GCPtr pGC,
                           int srcx, int srcy,
                           int width, int height,
                           int dstx, int dsty, unsigned long bitPlane)
{
    if ((pSrc->type == DRAWABLE_PIXMAP) && !XAA_DEPTH_BUG(pGC)) {
        if (pSrc->bitsPerPixel == 1) {
            return (XAABitBlt(pSrc, pDst, pGC, srcx, srcy,
                              width, height, dstx, dsty,
                              XAACopyPlane1toNColorExpand, bitPlane));
        }
        else if (bitPlane < (1 << pDst->depth)) {
            TmpBitPlane = bitPlane;
            return (XAABitBlt(pSrc, pDst, pGC, srcx, srcy,
                              width, height, dstx, dsty,
                              XAACopyPlaneNtoNColorExpand, bitPlane));
        }
    }

    return (XAAFallbackOps.CopyPlane(pSrc, pDst, pGC, srcx, srcy,
                                     width, height, dstx, dsty, bitPlane));
}

static void
XAACopyPlane1toNColorExpand(DrawablePtr pSrc,
                            DrawablePtr pDst,
                            GCPtr pGC, RegionPtr rgnDst, DDXPointPtr pptSrc)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    BoxPtr pbox = RegionRects(rgnDst);
    int numrects = RegionNumRects(rgnDst);
    unsigned char *src = ((PixmapPtr) pSrc)->devPrivate.ptr;
    int srcwidth = ((PixmapPtr) pSrc)->devKind;

    while (numrects--) {
        (*infoRec->WriteBitmap) (infoRec->pScrn, pbox->x1, pbox->y1,
                                 pbox->x2 - pbox->x1, pbox->y2 - pbox->y1,
                                 src + (srcwidth * pptSrc->y) +
                                 ((pptSrc->x >> 5) << 2), srcwidth,
                                 pptSrc->x & 31, pGC->fgPixel, pGC->bgPixel,
                                 pGC->alu, pGC->planemask);
        pbox++;
        pptSrc++;
    }
}

static void
XAACopyPlaneNtoNColorExpand(DrawablePtr pSrc,
                            DrawablePtr pDst,
                            GCPtr pGC, RegionPtr rgnDst, DDXPointPtr pptSrc)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    BoxPtr pbox = RegionRects(rgnDst);
    int numrects = RegionNumRects(rgnDst);
    unsigned char *src = ((PixmapPtr) pSrc)->devPrivate.ptr;
    unsigned char *data, *srcPtr, *dataPtr;
    int srcwidth = ((PixmapPtr) pSrc)->devKind;
    int pitch, width, height, h, i, index, offset;
    int Bpp = pSrc->bitsPerPixel >> 3;
    unsigned long mask = TmpBitPlane;

    if (TmpBitPlane < (1 << 8)) {
        offset = 0;
    }
    else if (TmpBitPlane < (1 << 16)) {
        offset = 1;
        mask >>= 8;
    }
    else if (TmpBitPlane < (1 << 24)) {
        offset = 2;
        mask >>= 16;
    }
    else {
        offset = 3;
        mask >>= 24;
    }

    if (IS_OFFSCREEN_PIXMAP(pSrc))
        SYNC_CHECK(pSrc);

    while (numrects--) {
        width = pbox->x2 - pbox->x1;
        h = height = pbox->y2 - pbox->y1;
        pitch = BitmapBytePad(width);

        if (!(data = calloc(height, pitch)))
            goto ALLOC_FAILED;

        dataPtr = data;
        srcPtr = ((pptSrc->y) * srcwidth) + src + ((pptSrc->x) * Bpp) + offset;

        while (h--) {
            for (i = index = 0; i < width; i++, index += Bpp) {
                if (mask & srcPtr[index])
                    dataPtr[i >> 3] |= (1 << (i & 7));
            }
            dataPtr += pitch;
            srcPtr += srcwidth;
        }

        (*infoRec->WriteBitmap) (infoRec->pScrn,
                                 pbox->x1, pbox->y1, width, height, data, pitch,
                                 0, pGC->fgPixel, pGC->bgPixel, pGC->alu,
                                 pGC->planemask);

        free(data);

 ALLOC_FAILED:

        pbox++;
        pptSrc++;
    }
}

void
XAAPushPixelsSolidColorExpansion(GCPtr pGC,
                                 PixmapPtr pBitMap,
                                 DrawablePtr pDraw,
                                 int dx, int dy, int xOrg, int yOrg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int MaxBoxes = RegionNumRects(pGC->pCompositeClip);
    BoxPtr pbox, pClipBoxes;
    int nboxes, srcx, srcy;
    xRectangle TheRect;
    unsigned char *src = pBitMap->devPrivate.ptr;
    int srcwidth = pBitMap->devKind;

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    TheRect.x = xOrg;
    TheRect.y = yOrg;
    TheRect.width = dx;
    TheRect.height = dy;

    if (MaxBoxes > (infoRec->PreAllocSize / sizeof(BoxRec))) {
        pClipBoxes = malloc(MaxBoxes * sizeof(BoxRec));
        if (!pClipBoxes)
            return;
    }
    else
        pClipBoxes = (BoxPtr) infoRec->PreAllocMem;

    nboxes = XAAGetRectClipBoxes(pGC, pClipBoxes, 1, &TheRect);
    pbox = pClipBoxes;

    while (nboxes--) {
        srcx = pbox->x1 - xOrg;
        srcy = pbox->y1 - yOrg;
        (*infoRec->WriteBitmap) (infoRec->pScrn, pbox->x1, pbox->y1,
                                 pbox->x2 - pbox->x1, pbox->y2 - pbox->y1,
                                 src + (srcwidth * srcy) + ((srcx >> 5) << 2),
                                 srcwidth, srcx & 31,
                                 pGC->fgPixel, -1, pGC->alu, pGC->planemask);
        pbox++;
    }

    if (pClipBoxes != (BoxPtr) infoRec->PreAllocMem)
        free(pClipBoxes);
}
