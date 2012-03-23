
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaalocal.h"
#include "gcstruct.h"
#include "pixmapstr.h"
#include "xaawrap.h"

static void
XAAFillSpansFallback(DrawablePtr pDraw,
                     GC * pGC,
                     int nInit,
                     DDXPointPtr pptInit, int *pwidthInit, int fSorted)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->FillSpans) (pDraw, pGC, nInit, pptInit, pwidthInit, fSorted);
    XAA_GC_OP_EPILOGUE(pGC);
}

static void
XAASetSpansFallback(DrawablePtr pDraw,
                    GCPtr pGC,
                    char *pcharsrc,
                    register DDXPointPtr ppt,
                    int *pwidth, int nspans, int fSorted)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->SetSpans) (pDraw, pGC, pcharsrc, ppt, pwidth, nspans, fSorted);
    XAA_GC_OP_EPILOGUE(pGC);
}

static void
XAAPutImageFallback(DrawablePtr pDraw,
                    GCPtr pGC,
                    int depth,
                    int x, int y, int w, int h,
                    int leftPad, int format, char *pImage)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->PutImage) (pDraw, pGC, depth, x, y, w, h,
                           leftPad, format, pImage);
    XAA_GC_OP_EPILOGUE(pGC);
}

static RegionPtr
XAACopyAreaFallback(DrawablePtr pSrc,
                    DrawablePtr pDst,
                    GC * pGC,
                    int srcx, int srcy,
                    int width, int height, int dstx, int dsty)
{
    RegionPtr ret;

    XAA_GC_OP_PROLOGUE(pGC);
    if ((pSrc->type == DRAWABLE_WINDOW) || (pDst->type == DRAWABLE_WINDOW) ||
        IS_OFFSCREEN_PIXMAP(pSrc) || IS_OFFSCREEN_PIXMAP(pDst)) {
        SYNC_CHECK(pGC);
    }
    ret = (*pGC->ops->CopyArea) (pSrc, pDst,
                                 pGC, srcx, srcy, width, height, dstx, dsty);
    XAA_GC_OP_EPILOGUE(pGC);
    return ret;
}

static RegionPtr
XAACopyPlaneFallback(DrawablePtr pSrc,
                     DrawablePtr pDst,
                     GCPtr pGC,
                     int srcx, int srcy,
                     int width, int height,
                     int dstx, int dsty, unsigned long bitPlane)
{
    RegionPtr ret;

    XAA_GC_OP_PROLOGUE(pGC);
    if ((pSrc->type == DRAWABLE_WINDOW) || (pDst->type == DRAWABLE_WINDOW) ||
        IS_OFFSCREEN_PIXMAP(pSrc) || IS_OFFSCREEN_PIXMAP(pDst)) {
        SYNC_CHECK(pGC);
    }
    ret = (*pGC->ops->CopyPlane) (pSrc, pDst,
                                  pGC, srcx, srcy, width, height, dstx, dsty,
                                  bitPlane);
    XAA_GC_OP_EPILOGUE(pGC);
    return ret;
}

static void
XAAPolyPointFallback(DrawablePtr pDraw,
                     GCPtr pGC, int mode, int npt, xPoint * pptInit)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->PolyPoint) (pDraw, pGC, mode, npt, pptInit);
    XAA_GC_OP_EPILOGUE(pGC);
}

static void
XAAPolylinesFallback(DrawablePtr pDraw,
                     GCPtr pGC, int mode, int npt, DDXPointPtr pptInit)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->Polylines) (pDraw, pGC, mode, npt, pptInit);
    XAA_GC_OP_EPILOGUE(pGC);
}

static void
XAAPolySegmentFallback(DrawablePtr pDraw, GCPtr pGC, int nseg, xSegment * pSeg)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->PolySegment) (pDraw, pGC, nseg, pSeg);
    XAA_GC_OP_EPILOGUE(pGC);
}

static void
XAAPolyRectangleFallback(DrawablePtr pDraw,
                         GCPtr pGC, int nRectsInit, xRectangle *pRectsInit)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->PolyRectangle) (pDraw, pGC, nRectsInit, pRectsInit);
    XAA_GC_OP_EPILOGUE(pGC);
}

static void
XAAPolyArcFallback(DrawablePtr pDraw, GCPtr pGC, int narcs, xArc * parcs)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->PolyArc) (pDraw, pGC, narcs, parcs);
    XAA_GC_OP_EPILOGUE(pGC);
}

static void
XAAFillPolygonFallback(DrawablePtr pDraw,
                       GCPtr pGC,
                       int shape, int mode, int count, DDXPointPtr ptsIn)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->FillPolygon) (pDraw, pGC, shape, mode, count, ptsIn);
    XAA_GC_OP_EPILOGUE(pGC);
}

static void
XAAPolyFillRectFallback(DrawablePtr pDraw,
                        GCPtr pGC, int nrectFill, xRectangle *prectInit)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->PolyFillRect) (pDraw, pGC, nrectFill, prectInit);
    XAA_GC_OP_EPILOGUE(pGC);
}

static void
XAAPolyFillArcFallback(DrawablePtr pDraw, GCPtr pGC, int narcs, xArc * parcs)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->PolyFillArc) (pDraw, pGC, narcs, parcs);
    XAA_GC_OP_EPILOGUE(pGC);
}

static int
XAAPolyText8Fallback(DrawablePtr pDraw,
                     GCPtr pGC, int x, int y, int count, char *chars)
{
    int ret;

    XAA_GC_OP_PROLOGUE(pGC);
    SYNC_CHECK(pGC);
    ret = (*pGC->ops->PolyText8) (pDraw, pGC, x, y, count, chars);
    XAA_GC_OP_EPILOGUE(pGC);
    return ret;
}

static int
XAAPolyText16Fallback(DrawablePtr pDraw,
                      GCPtr pGC, int x, int y, int count, unsigned short *chars)
{
    int ret;

    XAA_GC_OP_PROLOGUE(pGC);
    SYNC_CHECK(pGC);
    ret = (*pGC->ops->PolyText16) (pDraw, pGC, x, y, count, chars);
    XAA_GC_OP_EPILOGUE(pGC);
    return ret;
}

static void
XAAImageText8Fallback(DrawablePtr pDraw,
                      GCPtr pGC, int x, int y, int count, char *chars)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->ImageText8) (pDraw, pGC, x, y, count, chars);
    XAA_GC_OP_EPILOGUE(pGC);
}

static void
XAAImageText16Fallback(DrawablePtr pDraw,
                       GCPtr pGC,
                       int x, int y, int count, unsigned short *chars)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->ImageText16) (pDraw, pGC, x, y, count, chars);
    XAA_GC_OP_EPILOGUE(pGC);
}

static void
XAAImageGlyphBltFallback(DrawablePtr pDraw,
                         GCPtr pGC,
                         int xInit, int yInit,
                         unsigned int nglyph,
                         CharInfoPtr * ppci, pointer pglyphBase)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->ImageGlyphBlt) (pDraw, pGC, xInit, yInit, nglyph, ppci,
                                pglyphBase);
    XAA_GC_OP_EPILOGUE(pGC);
}

static void
XAAPolyGlyphBltFallback(DrawablePtr pDraw,
                        GCPtr pGC,
                        int xInit, int yInit,
                        unsigned int nglyph,
                        CharInfoPtr * ppci, pointer pglyphBase)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->PolyGlyphBlt) (pDraw, pGC, xInit, yInit, nglyph, ppci,
                               pglyphBase);
    XAA_GC_OP_EPILOGUE(pGC);
}

static void
XAAPushPixelsFallback(GCPtr pGC,
                      PixmapPtr pBitMap,
                      DrawablePtr pDraw, int dx, int dy, int xOrg, int yOrg)
{
    XAA_GC_OP_PROLOGUE_WITH_RETURN(pGC);
    SYNC_CHECK(pGC);
    (*pGC->ops->PushPixels) (pGC, pBitMap, pDraw, dx, dy, xOrg, yOrg);
    XAA_GC_OP_EPILOGUE(pGC);
}

GCOps XAAFallbackOps = {
    XAAFillSpansFallback, XAASetSpansFallback,
    XAAPutImageFallback, XAACopyAreaFallback,
    XAACopyPlaneFallback, XAAPolyPointFallback,
    XAAPolylinesFallback, XAAPolySegmentFallback,
    XAAPolyRectangleFallback, XAAPolyArcFallback,
    XAAFillPolygonFallback, XAAPolyFillRectFallback,
    XAAPolyFillArcFallback, XAAPolyText8Fallback,
    XAAPolyText16Fallback, XAAImageText8Fallback,
    XAAImageText16Fallback, XAAImageGlyphBltFallback,
    XAAPolyGlyphBltFallback, XAAPushPixelsFallback,
};

GCOps *
XAAGetFallbackOps(void)
{
    return &XAAFallbackOps;
}
