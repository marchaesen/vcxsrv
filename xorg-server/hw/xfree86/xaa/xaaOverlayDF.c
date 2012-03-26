/*
   Copyright (c) 1999 - The XFree86 Project Inc.

   Written by Mark Vojkovich
*/

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "pixmapstr.h"
#include "windowstr.h"
#include "xf86str.h"
#include "mi.h"
#include "miline.h"
#include "xaa.h"
#include "xaalocal.h"
#include "xaawrap.h"
#include "servermd.h"

/* Screen funcs */

static void XAAOverCopyWindow(WindowPtr, DDXPointRec, RegionPtr);
static void XAAOverWindowExposures(WindowPtr, RegionPtr, RegionPtr);

static int XAAOverStippledFillChooser(GCPtr);
static int XAAOverOpaqueStippledFillChooser(GCPtr);
static int XAAOverTiledFillChooser(GCPtr);

/* GC funcs */

static RegionPtr XAAOverCopyArea(DrawablePtr, DrawablePtr, GC *,
                                 int, int, int, int, int, int);
static RegionPtr XAAOverCopyPlane(DrawablePtr, DrawablePtr, GCPtr,
                                  int, int, int, int, int, int, unsigned long);
static void XAAOverPushPixelsSolid(GCPtr, PixmapPtr, DrawablePtr, int,
                                   int, int, int);
static void XAAOverPolyFillRectSolid(DrawablePtr, GCPtr, int, xRectangle *);
static void XAAOverPolyFillRectStippled(DrawablePtr, GCPtr, int, xRectangle *);
static void XAAOverPolyFillRectOpaqueStippled(DrawablePtr, GCPtr,
                                              int, xRectangle *);
static void XAAOverPolyFillRectTiled(DrawablePtr, GCPtr, int, xRectangle *);
static void XAAOverFillSpansSolid(DrawablePtr, GCPtr, int, DDXPointPtr,
                                  int *, int);
static void XAAOverFillSpansStippled(DrawablePtr, GCPtr, int, DDXPointPtr,
                                     int *, int);
static void XAAOverFillSpansOpaqueStippled(DrawablePtr, GCPtr, int,
                                           DDXPointPtr, int *, int);
static void XAAOverFillSpansTiled(DrawablePtr, GCPtr, int, DDXPointPtr,
                                  int *, int);
static int XAAOverPolyText8TE(DrawablePtr, GCPtr, int, int, int, char *);
static int XAAOverPolyText16TE(DrawablePtr, GCPtr, int, int, int,
                               unsigned short *);
static void XAAOverImageText8TE(DrawablePtr, GCPtr, int, int, int, char *);
static void XAAOverImageText16TE(DrawablePtr, GCPtr, int, int, int,
                                 unsigned short *);
static void XAAOverImageGlyphBltTE(DrawablePtr, GCPtr, int, int,
                                   unsigned int, CharInfoPtr *, pointer);
static void XAAOverPolyGlyphBltTE(DrawablePtr, GCPtr, int, int,
                                  unsigned int, CharInfoPtr *, pointer);
static int XAAOverPolyText8NonTE(DrawablePtr, GCPtr, int, int, int, char *);
static int XAAOverPolyText16NonTE(DrawablePtr, GCPtr, int, int, int,
                                  unsigned short *);
static void XAAOverImageText8NonTE(DrawablePtr, GCPtr, int, int, int, char *);
static void XAAOverImageText16NonTE(DrawablePtr, GCPtr, int, int, int,
                                    unsigned short *);
static void XAAOverImageGlyphBltNonTE(DrawablePtr, GCPtr, int, int,
                                      unsigned int, CharInfoPtr *, pointer);
static void XAAOverPolyGlyphBltNonTE(DrawablePtr, GCPtr, int, int,
                                     unsigned int, CharInfoPtr *, pointer);
static void XAAOverPolyRectangleThinSolid(DrawablePtr, GCPtr, int,
                                          xRectangle *);
static void XAAOverPolylinesWideSolid(DrawablePtr, GCPtr, int, int,
                                      DDXPointPtr);
static void XAAOverPolylinesThinSolid(DrawablePtr, GCPtr, int, int,
                                      DDXPointPtr);
static void XAAOverPolySegmentThinSolid(DrawablePtr, GCPtr, int, xSegment *);
static void XAAOverPolylinesThinDashed(DrawablePtr, GCPtr, int, int,
                                       DDXPointPtr);
static void XAAOverPolySegmentThinDashed(DrawablePtr, GCPtr, int, xSegment *);
static void XAAOverFillPolygonSolid(DrawablePtr, GCPtr, int, int, int,
                                    DDXPointPtr);
static void XAAOverFillPolygonStippled(DrawablePtr, GCPtr, int, int, int,
                                       DDXPointPtr);
static void XAAOverFillPolygonOpaqueStippled(DrawablePtr, GCPtr, int, int, int,
                                             DDXPointPtr);
static void XAAOverFillPolygonTiled(DrawablePtr, GCPtr, int, int, int,
                                    DDXPointPtr);
static void XAAOverPolyFillArcSolid(DrawablePtr, GCPtr, int, xArc *);
static void XAAOverPutImage(DrawablePtr, GCPtr, int, int, int, int, int,
                            int, int, char *);

typedef struct {
    ScrnInfoPtr pScrn;
    DepthChangeFuncPtr callback;
    int currentDepth;
/* GC funcs */
    RegionPtr (*CopyArea) (DrawablePtr, DrawablePtr, GC *,
                           int, int, int, int, int, int);
    RegionPtr (*CopyPlane) (DrawablePtr, DrawablePtr, GCPtr,
                            int, int, int, int, int, int, unsigned long);
    void (*PushPixelsSolid) (GCPtr, PixmapPtr, DrawablePtr, int, int, int, int);
    void (*PolyFillRectSolid) (DrawablePtr, GCPtr, int, xRectangle *);
    void (*PolyFillRectStippled) (DrawablePtr, GCPtr, int, xRectangle *);
    void (*PolyFillRectOpaqueStippled) (DrawablePtr, GCPtr, int, xRectangle *);
    void (*PolyFillRectTiled) (DrawablePtr, GCPtr, int, xRectangle *);
    void (*FillSpansSolid) (DrawablePtr, GCPtr, int, DDXPointPtr, int *, int);
    void (*FillSpansStippled) (DrawablePtr, GCPtr, int, DDXPointPtr, int *,
                               int);
    void (*FillSpansOpaqueStippled) (DrawablePtr, GCPtr, int, DDXPointPtr,
                                     int *, int);
    void (*FillSpansTiled) (DrawablePtr, GCPtr, int, DDXPointPtr, int *, int);
    int (*PolyText8TE) (DrawablePtr, GCPtr, int, int, int, char *);
    int (*PolyText16TE) (DrawablePtr, GCPtr, int, int, int, unsigned short *);
    void (*ImageText8TE) (DrawablePtr, GCPtr, int, int, int, char *);
    void (*ImageText16TE) (DrawablePtr, GCPtr, int, int, int, unsigned short *);
    void (*ImageGlyphBltTE) (DrawablePtr, GCPtr, int, int, unsigned int,
                             CharInfoPtr *, pointer);
    void (*PolyGlyphBltTE) (DrawablePtr, GCPtr, int, int, unsigned int,
                            CharInfoPtr *, pointer);
    int (*PolyText8NonTE) (DrawablePtr, GCPtr, int, int, int, char *);
    int (*PolyText16NonTE) (DrawablePtr, GCPtr, int, int, int,
                            unsigned short *);
    void (*ImageText8NonTE) (DrawablePtr, GCPtr, int, int, int, char *);
    void (*ImageText16NonTE) (DrawablePtr, GCPtr, int, int, int,
                              unsigned short *);
    void (*ImageGlyphBltNonTE) (DrawablePtr, GCPtr, int, int, unsigned int,
                                CharInfoPtr *, pointer);
    void (*PolyGlyphBltNonTE) (DrawablePtr, GCPtr, int, int, unsigned int,
                               CharInfoPtr *, pointer);
    void (*PolyRectangleThinSolid) (DrawablePtr, GCPtr, int, xRectangle *);
    void (*PolylinesWideSolid) (DrawablePtr, GCPtr, int, int, DDXPointPtr);

    void (*PolylinesThinSolid) (DrawablePtr, GCPtr, int, int, DDXPointPtr);
    void (*PolySegmentThinSolid) (DrawablePtr, GCPtr, int, xSegment *);
    void (*PolylinesThinDashed) (DrawablePtr, GCPtr, int, int, DDXPointPtr);
    void (*PolySegmentThinDashed) (DrawablePtr, GCPtr, int, xSegment *);
    void (*FillPolygonSolid) (DrawablePtr, GCPtr, int, int, int, DDXPointPtr);
    void (*FillPolygonStippled) (DrawablePtr, GCPtr, int, int, int,
                                 DDXPointPtr);
    void (*FillPolygonOpaqueStippled) (DrawablePtr, GCPtr, int, int, int,
                                       DDXPointPtr);
    void (*FillPolygonTiled) (DrawablePtr, GCPtr, int, int, int, DDXPointPtr);
    void (*PolyFillArcSolid) (DrawablePtr, GCPtr, int, xArc *);
    void (*PutImage) (DrawablePtr, GCPtr, int, int, int, int, int, int,
                      int, char *);
    int (*StippledFillChooser) (GCPtr);
    int (*OpaqueStippledFillChooser) (GCPtr);
    int (*TiledFillChooser) (GCPtr);
} XAAOverlayRec, *XAAOverlayPtr;

static DevPrivateKeyRec XAAOverlayKeyRec;

#define XAAOverlayKey (&XAAOverlayKeyRec)

#define GET_OVERLAY_PRIV(pScreen) \
    (XAAOverlayPtr)dixLookupPrivate(&(pScreen)->devPrivates, XAAOverlayKey)

#define SWITCH_DEPTH(d) \
   if(pOverPriv->currentDepth != d) { \
	(*pOverPriv->callback)(pOverPriv->pScrn, d); \
	pOverPriv->currentDepth = d; \
   }

Bool
XAAInitDualFramebufferOverlay(ScreenPtr pScreen, DepthChangeFuncPtr callback)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    XAAOverlayPtr pOverPriv;

    if (!dixRegisterPrivateKey(&XAAOverlayKeyRec, PRIVATE_SCREEN, 0))
        return FALSE;

    if (!(pOverPriv = malloc(sizeof(XAAOverlayRec))))
        return FALSE;

    dixSetPrivate(&pScreen->devPrivates, XAAOverlayKey, pOverPriv);

    pOverPriv->pScrn = pScrn;
    pOverPriv->callback = callback;
    pOverPriv->currentDepth = -1;

    /* Overwrite key screen functions.  The XAA core will clean up */

    pScreen->CopyWindow = XAAOverCopyWindow;
    pScreen->WindowExposures = XAAOverWindowExposures;

    pOverPriv->StippledFillChooser = infoRec->StippledFillChooser;
    pOverPriv->OpaqueStippledFillChooser = infoRec->OpaqueStippledFillChooser;
    pOverPriv->TiledFillChooser = infoRec->TiledFillChooser;

    infoRec->StippledFillChooser = XAAOverStippledFillChooser;
    infoRec->OpaqueStippledFillChooser = XAAOverOpaqueStippledFillChooser;
    infoRec->TiledFillChooser = XAAOverTiledFillChooser;

    /* wrap all XAA GC rendering */

    pOverPriv->CopyArea = infoRec->CopyArea;
    pOverPriv->CopyPlane = infoRec->CopyPlane;
    pOverPriv->PushPixelsSolid = infoRec->PushPixelsSolid;
    pOverPriv->PolyFillRectSolid = infoRec->PolyFillRectSolid;
    pOverPriv->PolyFillRectStippled = infoRec->PolyFillRectStippled;
    pOverPriv->PolyFillRectOpaqueStippled = infoRec->PolyFillRectOpaqueStippled;
    pOverPriv->PolyFillRectTiled = infoRec->PolyFillRectTiled;
    pOverPriv->FillSpansSolid = infoRec->FillSpansSolid;
    pOverPriv->FillSpansStippled = infoRec->FillSpansStippled;
    pOverPriv->FillSpansOpaqueStippled = infoRec->FillSpansOpaqueStippled;
    pOverPriv->FillSpansTiled = infoRec->FillSpansTiled;
    pOverPriv->PolyText8TE = infoRec->PolyText8TE;
    pOverPriv->PolyText16TE = infoRec->PolyText16TE;
    pOverPriv->ImageText8TE = infoRec->ImageText8TE;
    pOverPriv->ImageText16TE = infoRec->ImageText16TE;
    pOverPriv->ImageGlyphBltTE = infoRec->ImageGlyphBltTE;
    pOverPriv->PolyGlyphBltTE = infoRec->PolyGlyphBltTE;
    pOverPriv->PolyText8NonTE = infoRec->PolyText8NonTE;
    pOverPriv->PolyText16NonTE = infoRec->PolyText16NonTE;
    pOverPriv->ImageText8NonTE = infoRec->ImageText8NonTE;
    pOverPriv->ImageText16NonTE = infoRec->ImageText16NonTE;
    pOverPriv->ImageGlyphBltNonTE = infoRec->ImageGlyphBltNonTE;
    pOverPriv->PolyGlyphBltNonTE = infoRec->PolyGlyphBltNonTE;
    pOverPriv->PolyRectangleThinSolid = infoRec->PolyRectangleThinSolid;
    pOverPriv->PolylinesWideSolid = infoRec->PolylinesWideSolid;
    pOverPriv->PolylinesThinSolid = infoRec->PolylinesThinSolid;
    pOverPriv->PolySegmentThinSolid = infoRec->PolySegmentThinSolid;
    pOverPriv->PolylinesThinDashed = infoRec->PolylinesThinDashed;
    pOverPriv->PolySegmentThinDashed = infoRec->PolySegmentThinDashed;
    pOverPriv->FillPolygonSolid = infoRec->FillPolygonSolid;
    pOverPriv->FillPolygonStippled = infoRec->FillPolygonStippled;
    pOverPriv->FillPolygonOpaqueStippled = infoRec->FillPolygonOpaqueStippled;
    pOverPriv->FillPolygonTiled = infoRec->FillPolygonTiled;
    pOverPriv->PolyFillArcSolid = infoRec->PolyFillArcSolid;
    pOverPriv->PutImage = infoRec->PutImage;

    if (infoRec->CopyArea)
        infoRec->CopyArea = XAAOverCopyArea;
    if (infoRec->CopyPlane)
        infoRec->CopyPlane = XAAOverCopyPlane;
    if (infoRec->PushPixelsSolid)
        infoRec->PushPixelsSolid = XAAOverPushPixelsSolid;
    if (infoRec->PolyFillRectSolid)
        infoRec->PolyFillRectSolid = XAAOverPolyFillRectSolid;
    if (infoRec->PolyFillRectStippled)
        infoRec->PolyFillRectStippled = XAAOverPolyFillRectStippled;
    if (infoRec->PolyFillRectOpaqueStippled)
        infoRec->PolyFillRectOpaqueStippled = XAAOverPolyFillRectOpaqueStippled;
    if (infoRec->PolyFillRectTiled)
        infoRec->PolyFillRectTiled = XAAOverPolyFillRectTiled;
    if (infoRec->FillSpansSolid)
        infoRec->FillSpansSolid = XAAOverFillSpansSolid;
    if (infoRec->FillSpansStippled)
        infoRec->FillSpansStippled = XAAOverFillSpansStippled;
    if (infoRec->FillSpansOpaqueStippled)
        infoRec->FillSpansOpaqueStippled = XAAOverFillSpansOpaqueStippled;
    if (infoRec->FillSpansTiled)
        infoRec->FillSpansTiled = XAAOverFillSpansTiled;
    if (infoRec->PolyText8TE)
        infoRec->PolyText8TE = XAAOverPolyText8TE;
    if (infoRec->PolyText16TE)
        infoRec->PolyText16TE = XAAOverPolyText16TE;
    if (infoRec->ImageText8TE)
        infoRec->ImageText8TE = XAAOverImageText8TE;
    if (infoRec->ImageText16TE)
        infoRec->ImageText16TE = XAAOverImageText16TE;
    if (infoRec->ImageGlyphBltTE)
        infoRec->ImageGlyphBltTE = XAAOverImageGlyphBltTE;
    if (infoRec->PolyGlyphBltTE)
        infoRec->PolyGlyphBltTE = XAAOverPolyGlyphBltTE;
    if (infoRec->PolyText8NonTE)
        infoRec->PolyText8NonTE = XAAOverPolyText8NonTE;
    if (infoRec->PolyText16NonTE)
        infoRec->PolyText16NonTE = XAAOverPolyText16NonTE;
    if (infoRec->ImageText8NonTE)
        infoRec->ImageText8NonTE = XAAOverImageText8NonTE;
    if (infoRec->ImageText16NonTE)
        infoRec->ImageText16NonTE = XAAOverImageText16NonTE;
    if (infoRec->ImageGlyphBltNonTE)
        infoRec->ImageGlyphBltNonTE = XAAOverImageGlyphBltNonTE;
    if (infoRec->PolyGlyphBltNonTE)
        infoRec->PolyGlyphBltNonTE = XAAOverPolyGlyphBltNonTE;
    if (infoRec->PolyRectangleThinSolid)
        infoRec->PolyRectangleThinSolid = XAAOverPolyRectangleThinSolid;
    if (infoRec->PolylinesWideSolid)
        infoRec->PolylinesWideSolid = XAAOverPolylinesWideSolid;
    if (infoRec->PolylinesThinSolid)
        infoRec->PolylinesThinSolid = XAAOverPolylinesThinSolid;
    if (infoRec->PolySegmentThinSolid)
        infoRec->PolySegmentThinSolid = XAAOverPolySegmentThinSolid;
    if (infoRec->PolylinesThinDashed)
        infoRec->PolylinesThinDashed = XAAOverPolylinesThinDashed;
    if (infoRec->PolySegmentThinDashed)
        infoRec->PolySegmentThinDashed = XAAOverPolySegmentThinDashed;
    if (infoRec->FillPolygonSolid)
        infoRec->FillPolygonSolid = XAAOverFillPolygonSolid;
    if (infoRec->FillPolygonStippled)
        infoRec->FillPolygonStippled = XAAOverFillPolygonStippled;
    if (infoRec->FillPolygonOpaqueStippled)
        infoRec->FillPolygonOpaqueStippled = XAAOverFillPolygonOpaqueStippled;
    if (infoRec->FillPolygonTiled)
        infoRec->FillPolygonTiled = XAAOverFillPolygonTiled;
    if (infoRec->PolyFillArcSolid)
        infoRec->PolyFillArcSolid = XAAOverPolyFillArcSolid;
    if (infoRec->PutImage)
        infoRec->PutImage = XAAOverPutImage;

    return TRUE;
}

/***********************  Screen functions ************************/

void
XAAOverCopyWindow(WindowPtr pWin, DDXPointRec ptOldOrg, RegionPtr prgnSrc)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pScreen);
    ScrnInfoPtr pScrn = infoRec->pScrn;
    DDXPointPtr ppt, pptSrc;
    RegionRec rgnDst;
    BoxPtr pbox;
    int i, nbox, dx, dy;
    WindowPtr pRoot = pScreen->root;

    if (!pScrn->vtSema || !infoRec->ScreenToScreenBitBlt) {
        XAA_SCREEN_PROLOGUE(pScreen, CopyWindow);
        if (pScrn->vtSema && infoRec->NeedToSync) {
            (*infoRec->Sync) (pScrn);
            infoRec->NeedToSync = FALSE;
        }
        (*pScreen->CopyWindow) (pWin, ptOldOrg, prgnSrc);
        XAA_SCREEN_EPILOGUE(pScreen, CopyWindow, XAAOverCopyWindow);
        return;
    }

    infoRec->ScratchGC.alu = GXcopy;
    infoRec->ScratchGC.planemask = ~0;

    RegionNull(&rgnDst);

    dx = ptOldOrg.x - pWin->drawable.x;
    dy = ptOldOrg.y - pWin->drawable.y;
    RegionTranslate(prgnSrc, -dx, -dy);
    RegionIntersect(&rgnDst, &pWin->borderClip, prgnSrc);

    nbox = RegionNumRects(&rgnDst);
    if (nbox && (pptSrc = (DDXPointPtr) malloc(nbox * sizeof(DDXPointRec)))) {

        pbox = RegionRects(&rgnDst);
        for (i = nbox, ppt = pptSrc; i--; ppt++, pbox++) {
            ppt->x = pbox->x1 + dx;
            ppt->y = pbox->y1 + dy;
        }

        SWITCH_DEPTH(8);
        XAADoBitBlt((DrawablePtr) pRoot, (DrawablePtr) pRoot,
                    &(infoRec->ScratchGC), &rgnDst, pptSrc);

        if (pWin->drawable.bitsPerPixel != 8) {
            SWITCH_DEPTH(pScrn->depth);
            XAADoBitBlt((DrawablePtr) pRoot, (DrawablePtr) pRoot,
                        &(infoRec->ScratchGC), &rgnDst, pptSrc);
        }

        free(pptSrc);
    }

    RegionUninit(&rgnDst);

    if (pWin->drawable.depth == 8) {
        RegionNull(&rgnDst);
        miSegregateChildren(pWin, &rgnDst, pScrn->depth);
        if (RegionNotEmpty(&rgnDst)) {
            RegionIntersect(&rgnDst, &rgnDst, prgnSrc);
            nbox = RegionNumRects(&rgnDst);
            if (nbox &&
                (pptSrc = (DDXPointPtr) malloc(nbox * sizeof(DDXPointRec)))) {

                pbox = RegionRects(&rgnDst);
                for (i = nbox, ppt = pptSrc; i--; ppt++, pbox++) {
                    ppt->x = pbox->x1 + dx;
                    ppt->y = pbox->y1 + dy;
                }

                SWITCH_DEPTH(pScrn->depth);
                XAADoBitBlt((DrawablePtr) pRoot, (DrawablePtr) pRoot,
                            &(infoRec->ScratchGC), &rgnDst, pptSrc);
                free(pptSrc);
            }
        }
        RegionUninit(&rgnDst);
    }
}

void
XAAOverWindowExposures(WindowPtr pWin, RegionPtr pReg, RegionPtr pOtherReg)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCREEN(pScreen);

    if ((pWin->drawable.bitsPerPixel != 8) && infoRec->pScrn->vtSema) {
        if (RegionNumRects(pReg) && infoRec->FillSolidRects) {
            XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pScreen);

            SWITCH_DEPTH(8);
            (*infoRec->FillSolidRects) (infoRec->pScrn,
                                        infoRec->pScrn->colorKey, GXcopy, ~0,
                                        RegionNumRects(pReg),
                                        RegionRects(pReg));
            miWindowExposures(pWin, pReg, pOtherReg);
            return;
        }
        else if (infoRec->NeedToSync) {
            (*infoRec->Sync) (infoRec->pScrn);
            infoRec->NeedToSync = FALSE;
        }
    }

    XAA_SCREEN_PROLOGUE(pScreen, WindowExposures);
    (*pScreen->WindowExposures) (pWin, pReg, pOtherReg);
    XAA_SCREEN_EPILOGUE(pScreen, WindowExposures, XAAOverWindowExposures);
}

/*********************  Choosers *************************/

static int
XAAOverStippledFillChooser(GCPtr pGC)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);
    int ret;

    ret = (*pOverPriv->StippledFillChooser) (pGC);

    if ((pGC->depth == 8) && ((ret == DO_COLOR_8x8) || (ret == DO_CACHE_BLT))) {
        ret = 0;
    }

    return ret;
}

static int
XAAOverOpaqueStippledFillChooser(GCPtr pGC)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);
    int ret;

    ret = (*pOverPriv->OpaqueStippledFillChooser) (pGC);

    if ((pGC->depth == 8) && ((ret == DO_COLOR_8x8) || (ret == DO_CACHE_BLT))) {
        ret = 0;
    }

    return ret;
}

static int
XAAOverTiledFillChooser(GCPtr pGC)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);
    int ret;

    ret = (*pOverPriv->TiledFillChooser) (pGC);

    if ((pGC->depth == 8) && ((ret == DO_COLOR_8x8) || (ret == DO_CACHE_BLT))) {
        ret = 0;
    }

    return ret;
}

/**************************** GC Functions **************************/

static RegionPtr
XAAOverCopyArea(DrawablePtr pSrc,
                DrawablePtr pDst,
                GC * pGC,
                int srcx, int srcy, int width, int height, int dstx, int dsty)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    return (*pOverPriv->CopyArea) (pSrc, pDst,
                                   pGC, srcx, srcy, width, height, dstx, dsty);
}

static RegionPtr
XAAOverCopyPlane(DrawablePtr pSrc,
                 DrawablePtr pDst,
                 GCPtr pGC,
                 int srcx, int srcy,
                 int width, int height,
                 int dstx, int dsty, unsigned long bitPlane)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    return (*pOverPriv->CopyPlane) (pSrc, pDst,
                                    pGC, srcx, srcy, width, height, dstx, dsty,
                                    bitPlane);

}

static void
XAAOverPushPixelsSolid(GCPtr pGC,
                       PixmapPtr pBitMap,
                       DrawablePtr pDraw, int dx, int dy, int xOrg, int yOrg)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PushPixelsSolid) (pGC, pBitMap, pDraw, dx, dy, xOrg, yOrg);
}

static void
XAAOverPolyFillRectSolid(DrawablePtr pDraw,
                         GCPtr pGC, int nrectFill, xRectangle *prectInit)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PolyFillRectSolid) (pDraw, pGC, nrectFill, prectInit);
}

static void
XAAOverPolyFillRectStippled(DrawablePtr pDraw,
                            GCPtr pGC, int nrectFill, xRectangle *prectInit)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PolyFillRectStippled) (pDraw, pGC, nrectFill, prectInit);
}

static void
XAAOverPolyFillRectOpaqueStippled(DrawablePtr pDraw,
                                  GCPtr pGC,
                                  int nrectFill, xRectangle *prectInit)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PolyFillRectOpaqueStippled) (pDraw, pGC, nrectFill, prectInit);
}

static void
XAAOverPolyFillRectTiled(DrawablePtr pDraw,
                         GCPtr pGC, int nrectFill, xRectangle *prectInit)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PolyFillRectTiled) (pDraw, pGC, nrectFill, prectInit);
}

static void
XAAOverFillSpansSolid(DrawablePtr pDraw,
                      GCPtr pGC,
                      int nInit, DDXPointPtr ppt, int *pwidth, int fSorted)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->FillSpansSolid) (pDraw, pGC, nInit, ppt, pwidth, fSorted);
}

static void
XAAOverFillSpansStippled(DrawablePtr pDraw,
                         GCPtr pGC,
                         int nInit, DDXPointPtr ppt, int *pwidth, int fSorted)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->FillSpansStippled) (pDraw, pGC, nInit, ppt, pwidth, fSorted);
}

static void
XAAOverFillSpansOpaqueStippled(DrawablePtr pDraw,
                               GCPtr pGC,
                               int nInit,
                               DDXPointPtr ppt, int *pwidth, int fSorted)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->FillSpansOpaqueStippled) (pDraw, pGC, nInit, ppt, pwidth,
                                           fSorted);
}

static void
XAAOverFillSpansTiled(DrawablePtr pDraw,
                      GCPtr pGC,
                      int nInit, DDXPointPtr ppt, int *pwidth, int fSorted)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->FillSpansTiled) (pDraw, pGC, nInit, ppt, pwidth, fSorted);
}

static int
XAAOverPolyText8TE(DrawablePtr pDraw,
                   GCPtr pGC, int x, int y, int count, char *chars)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    return (*pOverPriv->PolyText8TE) (pDraw, pGC, x, y, count, chars);
}

static int
XAAOverPolyText16TE(DrawablePtr pDraw,
                    GCPtr pGC, int x, int y, int count, unsigned short *chars)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    return (*pOverPriv->PolyText16TE) (pDraw, pGC, x, y, count, chars);
}

static void
XAAOverImageText8TE(DrawablePtr pDraw,
                    GCPtr pGC, int x, int y, int count, char *chars)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->ImageText8TE) (pDraw, pGC, x, y, count, chars);
}

static void
XAAOverImageText16TE(DrawablePtr pDraw,
                     GCPtr pGC, int x, int y, int count, unsigned short *chars)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->ImageText16TE) (pDraw, pGC, x, y, count, chars);
}

static void
XAAOverImageGlyphBltTE(DrawablePtr pDraw,
                       GCPtr pGC,
                       int xInit, int yInit,
                       unsigned int nglyph,
                       CharInfoPtr * ppci, pointer pglyphBase)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->ImageGlyphBltTE) (pDraw, pGC, xInit, yInit, nglyph, ppci,
                                   pglyphBase);
}

static void
XAAOverPolyGlyphBltTE(DrawablePtr pDraw,
                      GCPtr pGC,
                      int xInit, int yInit,
                      unsigned int nglyph,
                      CharInfoPtr * ppci, pointer pglyphBase)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PolyGlyphBltTE) (pDraw, pGC, xInit, yInit, nglyph, ppci,
                                  pglyphBase);
}

static int
XAAOverPolyText8NonTE(DrawablePtr pDraw,
                      GCPtr pGC, int x, int y, int count, char *chars)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    return (*pOverPriv->PolyText8NonTE) (pDraw, pGC, x, y, count, chars);
}

static int
XAAOverPolyText16NonTE(DrawablePtr pDraw,
                       GCPtr pGC,
                       int x, int y, int count, unsigned short *chars)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    return (*pOverPriv->PolyText16NonTE) (pDraw, pGC, x, y, count, chars);
}

static void
XAAOverImageText8NonTE(DrawablePtr pDraw,
                       GCPtr pGC, int x, int y, int count, char *chars)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->ImageText8NonTE) (pDraw, pGC, x, y, count, chars);
}

static void
XAAOverImageText16NonTE(DrawablePtr pDraw,
                        GCPtr pGC,
                        int x, int y, int count, unsigned short *chars)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->ImageText16NonTE) (pDraw, pGC, x, y, count, chars);
}

static void
XAAOverImageGlyphBltNonTE(DrawablePtr pDraw,
                          GCPtr pGC,
                          int xInit, int yInit,
                          unsigned int nglyph,
                          CharInfoPtr * ppci, pointer pglyphBase)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->ImageGlyphBltNonTE) (pDraw, pGC, xInit, yInit, nglyph, ppci,
                                      pglyphBase);
}

static void
XAAOverPolyGlyphBltNonTE(DrawablePtr pDraw,
                         GCPtr pGC,
                         int xInit, int yInit,
                         unsigned int nglyph,
                         CharInfoPtr * ppci, pointer pglyphBase)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PolyGlyphBltNonTE) (pDraw, pGC, xInit, yInit, nglyph, ppci,
                                     pglyphBase);
}

static void
XAAOverPolyRectangleThinSolid(DrawablePtr pDraw,
                              GCPtr pGC, int nRectsInit, xRectangle *pRectsInit)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PolyRectangleThinSolid) (pDraw, pGC, nRectsInit, pRectsInit);
}

static void
XAAOverPolylinesWideSolid(DrawablePtr pDraw,
                          GCPtr pGC, int mode, int npt, DDXPointPtr pPts)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PolylinesWideSolid) (pDraw, pGC, mode, npt, pPts);
}

static void
XAAOverPolylinesThinSolid(DrawablePtr pDraw,
                          GCPtr pGC, int mode, int npt, DDXPointPtr pPts)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PolylinesThinSolid) (pDraw, pGC, mode, npt, pPts);
}

static void
XAAOverPolySegmentThinSolid(DrawablePtr pDraw,
                            GCPtr pGC, int nseg, xSegment * pSeg)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PolySegmentThinSolid) (pDraw, pGC, nseg, pSeg);
}

static void
XAAOverPolylinesThinDashed(DrawablePtr pDraw,
                           GCPtr pGC, int mode, int npt, DDXPointPtr pPts)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PolylinesThinDashed) (pDraw, pGC, mode, npt, pPts);
}

static void
XAAOverPolySegmentThinDashed(DrawablePtr pDraw,
                             GCPtr pGC, int nseg, xSegment * pSeg)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PolySegmentThinDashed) (pDraw, pGC, nseg, pSeg);
}

static void
XAAOverFillPolygonSolid(DrawablePtr pDraw,
                        GCPtr pGC,
                        int shape, int mode, int count, DDXPointPtr ptsIn)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->FillPolygonSolid) (pDraw, pGC, shape, mode, count, ptsIn);
}

static void
XAAOverFillPolygonStippled(DrawablePtr pDraw,
                           GCPtr pGC,
                           int shape, int mode, int count, DDXPointPtr ptsIn)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->FillPolygonStippled) (pDraw, pGC, shape, mode, count, ptsIn);
}

static void
XAAOverFillPolygonOpaqueStippled(DrawablePtr pDraw,
                                 GCPtr pGC,
                                 int shape,
                                 int mode, int count, DDXPointPtr ptsIn)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->FillPolygonOpaqueStippled) (pDraw, pGC, shape, mode, count,
                                             ptsIn);
}

static void
XAAOverFillPolygonTiled(DrawablePtr pDraw,
                        GCPtr pGC,
                        int shape, int mode, int count, DDXPointPtr ptsIn)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->FillPolygonTiled) (pDraw, pGC, shape, mode, count, ptsIn);
}

static void
XAAOverPolyFillArcSolid(DrawablePtr pDraw, GCPtr pGC, int narcs, xArc * parcs)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PolyFillArcSolid) (pDraw, pGC, narcs, parcs);
}

static void
XAAOverPutImage(DrawablePtr pDraw,
                GCPtr pGC,
                int depth,
                int x,
                int y, int w, int h, int leftPad, int format, char *pImage)
{
    XAAOverlayPtr pOverPriv = GET_OVERLAY_PRIV(pGC->pScreen);

    SWITCH_DEPTH(pGC->depth);

    (*pOverPriv->PutImage) (pDraw, pGC, depth, x, y, w, h,
                            leftPad, format, pImage);
}
