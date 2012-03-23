
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <string.h>

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaalocal.h"
#include "migc.h"
#include "gcstruct.h"
#include "pixmapstr.h"
#include "xaawrap.h"

static void XAAValidateGC(GCPtr pGC, unsigned long changes, DrawablePtr pDraw);
static void XAAChangeGC(GCPtr pGC, unsigned long mask);
static void XAACopyGC(GCPtr pGCSrc, unsigned long mask, GCPtr pGCDst);
static void XAADestroyGC(GCPtr pGC);
static void XAAChangeClip(GCPtr pGC, int type, pointer pvalue, int nrects);
static void XAADestroyClip(GCPtr pGC);
static void XAACopyClip(GCPtr pgcDst, GCPtr pgcSrc);

GCFuncs XAAGCFuncs = {
    XAAValidateGC, XAAChangeGC, XAACopyGC, XAADestroyGC,
    XAAChangeClip, XAADestroyClip, XAACopyClip
};

extern GCOps XAAPixmapOps;

Bool
XAACreateGC(GCPtr pGC)
{
    ScreenPtr pScreen = pGC->pScreen;
    XAAGCPtr pGCPriv = (XAAGCPtr) dixLookupPrivate(&pGC->devPrivates,
                                                   XAAGetGCKey());
    Bool ret;

    XAA_SCREEN_PROLOGUE(pScreen, CreateGC);

    if ((ret = (*pScreen->CreateGC) (pGC))) {
        pGCPriv->wrapOps = NULL;
        pGCPriv->wrapFuncs = pGC->funcs;
        pGCPriv->XAAOps = &XAAFallbackOps;
        pGC->funcs = &XAAGCFuncs;
    }

    XAA_SCREEN_EPILOGUE(pScreen, CreateGC, XAACreateGC);

    return ret;
}

static void
XAAValidateGC(GCPtr pGC, unsigned long changes, DrawablePtr pDraw)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    XAA_GC_FUNC_PROLOGUE(pGC);

    (*pGC->funcs->ValidateGC) (pGC, changes, pDraw);

    if ((changes & GCPlaneMask) &&
        ((pGC->planemask & infoRec->FullPlanemasks[pGC->depth - 1]) ==
         infoRec->FullPlanemasks[pGC->depth - 1])) {
        pGC->planemask = ~0;
    }

    if (pGC->depth != 32) {
        /* 0xffffffff is reserved for transparency */
        if (pGC->bgPixel == 0xffffffff)
            pGC->bgPixel = 0x7fffffff;
        if (pGC->fgPixel == 0xffffffff)
            pGC->fgPixel = 0x7fffffff;
    }

    if ((pDraw->type == DRAWABLE_PIXMAP) && !IS_OFFSCREEN_PIXMAP(pDraw)) {
        pGCPriv->flags = OPS_ARE_PIXMAP;
        pGCPriv->changes |= changes;

        /* make sure we're not using videomemory pixmaps to render
           onto system memory drawables */

        if ((pGC->fillStyle == FillTiled) &&
            IS_OFFSCREEN_PIXMAP(pGC->tile.pixmap) &&
            !OFFSCREEN_PIXMAP_LOCKED(pGC->tile.pixmap)) {

            XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pGC->tile.pixmap);
            FBAreaPtr area = pPriv->offscreenArea;

            XAARemoveAreaCallback(area);        /* clobbers pPriv->offscreenArea */
            xf86FreeOffscreenArea(area);
        }
    }
    else if (!infoRec->pScrn->vtSema && (pDraw->type == DRAWABLE_WINDOW)) {
        pGCPriv->flags = 0;
        pGCPriv->changes |= changes;
    }
    else {
        if (!(pGCPriv->flags & OPS_ARE_ACCEL)) {
            changes |= pGCPriv->changes;
            pGCPriv->changes = 0;
        }
        pGCPriv->flags = OPS_ARE_ACCEL;

#if 1
        /* Ugh.  If we can't use the blitter on offscreen pixmaps used
           as tiles, then we need to move them out as cfb can't handle
           tiles with non-zero origins */

        if ((pGC->fillStyle == FillTiled) &&
            IS_OFFSCREEN_PIXMAP(pGC->tile.pixmap) &&
            (DO_PIXMAP_COPY != (*infoRec->TiledFillChooser) (pGC))) {

            XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pGC->tile.pixmap);
            FBAreaPtr area = pPriv->offscreenArea;

            XAARemoveAreaCallback(area);        /* clobbers pPriv->offscreenArea */
            xf86FreeOffscreenArea(area);
        }
#endif
    }

    XAA_GC_FUNC_EPILOGUE(pGC);

    if (!(pGCPriv->flags & OPS_ARE_ACCEL))
        return;

    if ((changes & GCTile) && !pGC->tileIsPixel && pGC->tile.pixmap) {
        XAAPixmapPtr pixPriv = XAA_GET_PIXMAP_PRIVATE(pGC->tile.pixmap);

        if (pixPriv->flags & DIRTY) {
            pixPriv->flags &= ~(DIRTY | REDUCIBILITY_MASK);
            pGC->tile.pixmap->drawable.serialNumber = NEXT_SERIAL_NUMBER;
        }
    }
    if ((changes & GCStipple) && pGC->stipple) {
        XAAPixmapPtr pixPriv = XAA_GET_PIXMAP_PRIVATE(pGC->stipple);

        if (pixPriv->flags & DIRTY) {
            pixPriv->flags &= ~(DIRTY | REDUCIBILITY_MASK);
            pGC->stipple->drawable.serialNumber = NEXT_SERIAL_NUMBER;
        }
    }

    /* If our Ops are still the default ones we need to allocate new ones */
    if (pGC->ops == &XAAFallbackOps) {
        if (!(pGCPriv->XAAOps = malloc(sizeof(GCOps)))) {
            pGCPriv->XAAOps = &XAAFallbackOps;
            return;
        }
        /* make a modifiable copy of the default ops */
        memcpy(pGCPriv->XAAOps, &XAAFallbackOps, sizeof(GCOps));
        pGC->ops = pGCPriv->XAAOps;
        changes = ~0;
    }

    if (!changes)
        return;

    if ((changes & GCDashList) && infoRec->ComputeDash)
        infoRec->ComputeDash(pGC);

    if (changes & infoRec->FillSpansMask)
        (*infoRec->ValidateFillSpans) (pGC, changes, pDraw);

    if (changes & infoRec->SetSpansMask)
        (*infoRec->ValidateSetSpans) (pGC, changes, pDraw);

    if (changes & infoRec->PutImageMask)
        (*infoRec->ValidatePutImage) (pGC, changes, pDraw);

    if (changes & infoRec->CopyAreaMask)
        (*infoRec->ValidateCopyArea) (pGC, changes, pDraw);

    if (changes & infoRec->CopyPlaneMask)
        (*infoRec->ValidateCopyPlane) (pGC, changes, pDraw);

    if (changes & infoRec->PolyPointMask)
        (*infoRec->ValidatePolyPoint) (pGC, changes, pDraw);

    if (changes & infoRec->PolylinesMask)
        (*infoRec->ValidatePolylines) (pGC, changes, pDraw);

    if (changes & infoRec->PolySegmentMask)
        (*infoRec->ValidatePolySegment) (pGC, changes, pDraw);

    if (changes & infoRec->PolyRectangleMask)
        (*infoRec->ValidatePolyRectangle) (pGC, changes, pDraw);

    if (changes & infoRec->PolyArcMask)
        (*infoRec->ValidatePolyArc) (pGC, changes, pDraw);

    if (changes & infoRec->FillPolygonMask)
        (*infoRec->ValidateFillPolygon) (pGC, changes, pDraw);

    if (changes & infoRec->PolyFillRectMask)
        (*infoRec->ValidatePolyFillRect) (pGC, changes, pDraw);

    if (changes & infoRec->PolyFillArcMask)
        (*infoRec->ValidatePolyFillArc) (pGC, changes, pDraw);

    if (changes & infoRec->PolyGlyphBltMask)
        (*infoRec->ValidatePolyGlyphBlt) (pGC, changes, pDraw);

    if (changes & infoRec->ImageGlyphBltMask)
        (*infoRec->ValidateImageGlyphBlt) (pGC, changes, pDraw);

    if (changes & infoRec->PolyText8Mask)
        (*infoRec->ValidatePolyText8) (pGC, changes, pDraw);

    if (changes & infoRec->PolyText16Mask)
        (*infoRec->ValidatePolyText16) (pGC, changes, pDraw);

    if (changes & infoRec->ImageText8Mask)
        (*infoRec->ValidateImageText8) (pGC, changes, pDraw);

    if (changes & infoRec->ImageText16Mask)
        (*infoRec->ValidateImageText16) (pGC, changes, pDraw);

    if (changes & infoRec->PushPixelsMask)
        (*infoRec->ValidatePushPixels) (pGC, changes, pDraw);
}

static void
XAADestroyGC(GCPtr pGC)
{
    XAA_GC_FUNC_PROLOGUE(pGC);

    if (pGCPriv->XAAOps != &XAAFallbackOps)
        free(pGCPriv->XAAOps);

    free(pGCPriv->DashPattern);
    pGCPriv->flags = 0;

    (*pGC->funcs->DestroyGC) (pGC);
    XAA_GC_FUNC_EPILOGUE(pGC);
}

static void
XAAChangeGC(GCPtr pGC, unsigned long mask)
{
    XAA_GC_FUNC_PROLOGUE(pGC);
    (*pGC->funcs->ChangeGC) (pGC, mask);
    XAA_GC_FUNC_EPILOGUE(pGC);

    /* we have to assume that shared memory pixmaps are dirty 
       because we can't wrap all operations on them */

    if ((mask & GCTile) && !pGC->tileIsPixel &&
        PIXMAP_IS_SHARED(pGC->tile.pixmap)) {
        XAAPixmapPtr pPixPriv = XAA_GET_PIXMAP_PRIVATE(pGC->tile.pixmap);

        pPixPriv->flags |= DIRTY;
    }

    if ((mask & GCStipple) && PIXMAP_IS_SHARED(pGC->stipple)) {
        XAAPixmapPtr pPixPriv = XAA_GET_PIXMAP_PRIVATE(pGC->stipple);

        pPixPriv->flags |= DIRTY;
    }
}

static void
XAACopyGC(GCPtr pGCSrc, unsigned long mask, GCPtr pGCDst)
{
    XAA_GC_FUNC_PROLOGUE(pGCDst);
    (*pGCDst->funcs->CopyGC) (pGCSrc, mask, pGCDst);
    XAA_GC_FUNC_EPILOGUE(pGCDst);
}

static void
XAAChangeClip(GCPtr pGC, int type, pointer pvalue, int nrects)
{
    XAA_GC_FUNC_PROLOGUE(pGC);
    (*pGC->funcs->ChangeClip) (pGC, type, pvalue, nrects);
    XAA_GC_FUNC_EPILOGUE(pGC);
}

static void
XAACopyClip(GCPtr pgcDst, GCPtr pgcSrc)
{
    XAA_GC_FUNC_PROLOGUE(pgcDst);
    (*pgcDst->funcs->CopyClip) (pgcDst, pgcSrc);
    XAA_GC_FUNC_EPILOGUE(pgcDst);
}

static void
XAADestroyClip(GCPtr pGC)
{
    XAA_GC_FUNC_PROLOGUE(pGC);
    (*pGC->funcs->DestroyClip) (pGC);
    XAA_GC_FUNC_EPILOGUE(pGC);
}

/**** Pixmap Wrappers ****/

static void
XAAFillSpansPixmap(DrawablePtr pDraw,
                   GC * pGC,
                   int nInit, DDXPointPtr pptInit, int *pwidthInit, int fSorted)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->FillSpans) (pDraw, pGC, nInit, pptInit, pwidthInit, fSorted);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static void
XAASetSpansPixmap(DrawablePtr pDraw,
                  GCPtr pGC,
                  char *pcharsrc,
                  register DDXPointPtr ppt,
                  int *pwidth, int nspans, int fSorted)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->SetSpans) (pDraw, pGC, pcharsrc, ppt, pwidth, nspans, fSorted);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static void
XAAPutImagePixmap(DrawablePtr pDraw,
                  GCPtr pGC,
                  int depth,
                  int x, int y, int w, int h,
                  int leftPad, int format, char *pImage)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->PutImage) (pDraw, pGC, depth, x, y, w, h,
                           leftPad, format, pImage);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static RegionPtr
XAACopyAreaPixmap(DrawablePtr pSrc,
                  DrawablePtr pDst,
                  GC * pGC,
                  int srcx, int srcy, int width, int height, int dstx, int dsty)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    RegionPtr ret;

    if (infoRec->pScrn->vtSema &&
        ((pSrc->type == DRAWABLE_WINDOW) || IS_OFFSCREEN_PIXMAP(pSrc))) {
        if (infoRec->ReadPixmap && (pGC->alu == GXcopy) &&
            (pSrc->bitsPerPixel == pDst->bitsPerPixel) &&
            ((pGC->planemask & infoRec->FullPlanemasks[pSrc->depth - 1])
             == infoRec->FullPlanemasks[pSrc->depth - 1])) {
            XAAPixmapPtr pixPriv = XAA_GET_PIXMAP_PRIVATE((PixmapPtr) (pDst));

            pixPriv->flags |= DIRTY;

            return (XAABitBlt(pSrc, pDst, pGC,
                              srcx, srcy, width, height, dstx, dsty,
                              XAADoImageRead, 0L));
        }
        else if (infoRec->NeedToSync) {
            (*infoRec->Sync) (infoRec->pScrn);
            infoRec->NeedToSync = FALSE;
        }
    }

    {
        XAA_PIXMAP_OP_PROLOGUE(pGC, pDst);
        ret = (*pGC->ops->CopyArea) (pSrc, pDst,
                                     pGC, srcx, srcy, width, height, dstx,
                                     dsty);
        XAA_PIXMAP_OP_EPILOGUE(pGC);
    }
    return ret;
}

static RegionPtr
XAACopyPlanePixmap(DrawablePtr pSrc,
                   DrawablePtr pDst,
                   GCPtr pGC,
                   int srcx, int srcy,
                   int width, int height,
                   int dstx, int dsty, unsigned long bitPlane)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    RegionPtr ret;

    XAA_PIXMAP_OP_PROLOGUE(pGC, pDst);

    if (infoRec->pScrn->vtSema &&
        ((pSrc->type == DRAWABLE_WINDOW) || IS_OFFSCREEN_PIXMAP(pSrc))) {
        if (infoRec->NeedToSync) {
            (*infoRec->Sync) (infoRec->pScrn);
            infoRec->NeedToSync = FALSE;
        }
    }

    ret = (*pGC->ops->CopyPlane) (pSrc, pDst,
                                  pGC, srcx, srcy, width, height, dstx, dsty,
                                  bitPlane);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
    return ret;
}

static void
XAAPolyPointPixmap(DrawablePtr pDraw,
                   GCPtr pGC, int mode, int npt, xPoint * pptInit)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->PolyPoint) (pDraw, pGC, mode, npt, pptInit);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static void
XAAPolylinesPixmap(DrawablePtr pDraw,
                   GCPtr pGC, int mode, int npt, DDXPointPtr pptInit)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->Polylines) (pDraw, pGC, mode, npt, pptInit);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static void
XAAPolySegmentPixmap(DrawablePtr pDraw, GCPtr pGC, int nseg, xSegment * pSeg)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->PolySegment) (pDraw, pGC, nseg, pSeg);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static void
XAAPolyRectanglePixmap(DrawablePtr pDraw,
                       GCPtr pGC, int nRectsInit, xRectangle *pRectsInit)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->PolyRectangle) (pDraw, pGC, nRectsInit, pRectsInit);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static void
XAAPolyArcPixmap(DrawablePtr pDraw, GCPtr pGC, int narcs, xArc * parcs)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->PolyArc) (pDraw, pGC, narcs, parcs);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static void
XAAFillPolygonPixmap(DrawablePtr pDraw,
                     GCPtr pGC,
                     int shape, int mode, int count, DDXPointPtr ptsIn)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->FillPolygon) (pDraw, pGC, shape, mode, count, ptsIn);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static void
XAAPolyFillRectPixmap(DrawablePtr pDraw,
                      GCPtr pGC, int nrectFill, xRectangle *prectInit)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->PolyFillRect) (pDraw, pGC, nrectFill, prectInit);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static void
XAAPolyFillArcPixmap(DrawablePtr pDraw, GCPtr pGC, int narcs, xArc * parcs)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->PolyFillArc) (pDraw, pGC, narcs, parcs);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static int
XAAPolyText8Pixmap(DrawablePtr pDraw,
                   GCPtr pGC, int x, int y, int count, char *chars)
{
    int ret;

    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    ret = (*pGC->ops->PolyText8) (pDraw, pGC, x, y, count, chars);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
    return ret;
}

static int
XAAPolyText16Pixmap(DrawablePtr pDraw,
                    GCPtr pGC, int x, int y, int count, unsigned short *chars)
{
    int ret;

    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    ret = (*pGC->ops->PolyText16) (pDraw, pGC, x, y, count, chars);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
    return ret;
}

static void
XAAImageText8Pixmap(DrawablePtr pDraw,
                    GCPtr pGC, int x, int y, int count, char *chars)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->ImageText8) (pDraw, pGC, x, y, count, chars);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static void
XAAImageText16Pixmap(DrawablePtr pDraw,
                     GCPtr pGC, int x, int y, int count, unsigned short *chars)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->ImageText16) (pDraw, pGC, x, y, count, chars);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static void
XAAImageGlyphBltPixmap(DrawablePtr pDraw,
                       GCPtr pGC,
                       int xInit, int yInit,
                       unsigned int nglyph,
                       CharInfoPtr * ppci, pointer pglyphBase)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->ImageGlyphBlt) (pDraw, pGC, xInit, yInit, nglyph,
                                ppci, pglyphBase);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static void
XAAPolyGlyphBltPixmap(DrawablePtr pDraw,
                      GCPtr pGC,
                      int xInit, int yInit,
                      unsigned int nglyph,
                      CharInfoPtr * ppci, pointer pglyphBase)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->PolyGlyphBlt) (pDraw, pGC, xInit, yInit, nglyph,
                               ppci, pglyphBase);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

static void
XAAPushPixelsPixmap(GCPtr pGC,
                    PixmapPtr pBitMap,
                    DrawablePtr pDraw, int dx, int dy, int xOrg, int yOrg)
{
    XAA_PIXMAP_OP_PROLOGUE(pGC, pDraw);
    (*pGC->ops->PushPixels) (pGC, pBitMap, pDraw, dx, dy, xOrg, yOrg);
    XAA_PIXMAP_OP_EPILOGUE(pGC);
}

GCOps XAAPixmapOps = {
    XAAFillSpansPixmap, XAASetSpansPixmap,
    XAAPutImagePixmap, XAACopyAreaPixmap,
    XAACopyPlanePixmap, XAAPolyPointPixmap,
    XAAPolylinesPixmap, XAAPolySegmentPixmap,
    XAAPolyRectanglePixmap, XAAPolyArcPixmap,
    XAAFillPolygonPixmap, XAAPolyFillRectPixmap,
    XAAPolyFillArcPixmap, XAAPolyText8Pixmap,
    XAAPolyText16Pixmap, XAAImageText8Pixmap,
    XAAImageText16Pixmap, XAAImageGlyphBltPixmap,
    XAAPolyGlyphBltPixmap, XAAPushPixelsPixmap,
};
