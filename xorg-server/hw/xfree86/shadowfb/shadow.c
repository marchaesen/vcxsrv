/*
   Copyright (C) 1999.  The XFree86 Project Inc.

   Written by Mark Vojkovich (mvojkovi@ucsd.edu)

   Pre-fb-write callbacks and RENDER support - Nolan Leake (nolan@vmware.com)
*/

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include <X11/Xproto.h>
#include "misc.h"
#include "pixmapstr.h"
#include "input.h"
#include <X11/fonts/font.h>
#include "mi.h"
#include "scrnintstr.h"
#include "windowstr.h"
#include "gcstruct.h"
#include "dixfontstr.h"
#include <X11/fonts/fontstruct.h>
#include "xf86.h"
#include "xf86str.h"
#include "shadowfb.h"

#include "picturestr.h"

static Bool ShadowCloseScreen(ScreenPtr pScreen);
static void ShadowCopyWindow(WindowPtr pWin,
                             DDXPointRec ptOldOrg, RegionPtr prgn);
static Bool ShadowCreateGC(GCPtr pGC);

static Bool ShadowEnterVT(ScrnInfoPtr pScrn);
static void ShadowLeaveVT(ScrnInfoPtr pScrn);

static void ShadowComposite(CARD8 op,
                            PicturePtr pSrc,
                            PicturePtr pMask,
                            PicturePtr pDst,
                            INT16 xSrc,
                            INT16 ySrc,
                            INT16 xMask,
                            INT16 yMask,
                            INT16 xDst,
                            INT16 yDst, CARD16 width, CARD16 height);

typedef struct {
    ScrnInfoPtr pScrn;
    RefreshAreaFuncPtr preRefresh;
    RefreshAreaFuncPtr postRefresh;
    CloseScreenProcPtr CloseScreen;
    CopyWindowProcPtr CopyWindow;
    CreateGCProcPtr CreateGC;
    ModifyPixmapHeaderProcPtr ModifyPixmapHeader;
    CompositeProcPtr Composite;
    Bool (*EnterVT) (ScrnInfoPtr);
    void (*LeaveVT) (ScrnInfoPtr);
    Bool vtSema;
} ShadowScreenRec, *ShadowScreenPtr;

typedef struct {
    GCOps *ops;
    GCFuncs *funcs;
} ShadowGCRec, *ShadowGCPtr;

static DevPrivateKeyRec ShadowScreenKeyRec;

#define ShadowScreenKey (&ShadowScreenKeyRec)

static DevPrivateKeyRec ShadowGCKeyRec;

#define ShadowGCKey (&ShadowGCKeyRec)

#define GET_SCREEN_PRIVATE(pScreen) \
    (ShadowScreenPtr)dixLookupPrivate(&(pScreen)->devPrivates, ShadowScreenKey)
#define GET_GC_PRIVATE(pGC) \
    (ShadowGCPtr)dixLookupPrivate(&(pGC)->devPrivates, ShadowGCKey)

#define SHADOW_GC_FUNC_PROLOGUE(pGC)\
    ShadowGCPtr pGCPriv = GET_GC_PRIVATE(pGC);\
    (pGC)->funcs = pGCPriv->funcs;\
    if(pGCPriv->ops)\
        (pGC)->ops = pGCPriv->ops

#define SHADOW_GC_FUNC_EPILOGUE(pGC)\
    pGCPriv->funcs = (pGC)->funcs;\
    (pGC)->funcs = &ShadowGCFuncs;\
    if(pGCPriv->ops) {\
        pGCPriv->ops = (pGC)->ops;\
        (pGC)->ops = &ShadowGCOps;\
    }

#define SHADOW_GC_OP_PROLOGUE(pGC)\
    ShadowScreenPtr pPriv = GET_SCREEN_PRIVATE(pGC->pScreen); \
    ShadowGCPtr pGCPriv = GET_GC_PRIVATE(pGC);\
    GCFuncs *oldFuncs = pGC->funcs;\
    pGC->funcs = pGCPriv->funcs;\
    pGC->ops = pGCPriv->ops

#define SHADOW_GC_OP_EPILOGUE(pGC)\
    pGCPriv->ops = pGC->ops;\
    pGC->funcs = oldFuncs;\
    pGC->ops   = &ShadowGCOps

#define IS_VISIBLE(pWin) (pPriv->vtSema && \
    (((WindowPtr)pWin)->visibility != VisibilityFullyObscured))

#define TRIM_BOX(box, pGC) { \
    BoxPtr extents = &pGC->pCompositeClip->extents;\
    if(box.x1 < extents->x1) box.x1 = extents->x1; \
    if(box.x2 > extents->x2) box.x2 = extents->x2; \
    if(box.y1 < extents->y1) box.y1 = extents->y1; \
    if(box.y2 > extents->y2) box.y2 = extents->y2; \
    }

#define TRANSLATE_BOX(box, pDraw) { \
    box.x1 += pDraw->x; \
    box.x2 += pDraw->x; \
    box.y1 += pDraw->y; \
    box.y2 += pDraw->y; \
    }

#define TRIM_AND_TRANSLATE_BOX(box, pDraw, pGC) { \
    TRANSLATE_BOX(box, pDraw); \
    TRIM_BOX(box, pGC); \
    }

#define BOX_NOT_EMPTY(box) \
    (((box.x2 - box.x1) > 0) && ((box.y2 - box.y1) > 0))

Bool
ShadowFBInit2(ScreenPtr pScreen,
              RefreshAreaFuncPtr preRefreshArea,
              RefreshAreaFuncPtr postRefreshArea)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    ShadowScreenPtr pPriv;
    PictureScreenPtr ps = GetPictureScreenIfSet(pScreen);

    if (!preRefreshArea && !postRefreshArea)
        return FALSE;

    if (!dixRegisterPrivateKey(&ShadowScreenKeyRec, PRIVATE_SCREEN, 0))
        return FALSE;

    if (!dixRegisterPrivateKey
        (&ShadowGCKeyRec, PRIVATE_GC, sizeof(ShadowGCRec)))
        return FALSE;

    if (!(pPriv = (ShadowScreenPtr) malloc(sizeof(ShadowScreenRec))))
        return FALSE;

    dixSetPrivate(&pScreen->devPrivates, ShadowScreenKey, pPriv);

    pPriv->pScrn = pScrn;
    pPriv->preRefresh = preRefreshArea;
    pPriv->postRefresh = postRefreshArea;
    pPriv->vtSema = TRUE;

    pPriv->CloseScreen = pScreen->CloseScreen;
    pPriv->CopyWindow = pScreen->CopyWindow;
    pPriv->CreateGC = pScreen->CreateGC;
    pPriv->ModifyPixmapHeader = pScreen->ModifyPixmapHeader;

    pPriv->EnterVT = pScrn->EnterVT;
    pPriv->LeaveVT = pScrn->LeaveVT;

    pScreen->CloseScreen = ShadowCloseScreen;
    pScreen->CopyWindow = ShadowCopyWindow;
    pScreen->CreateGC = ShadowCreateGC;

    pScrn->EnterVT = ShadowEnterVT;
    pScrn->LeaveVT = ShadowLeaveVT;

    if (ps) {
        pPriv->Composite = ps->Composite;
        ps->Composite = ShadowComposite;
    }

    return TRUE;
}

Bool
ShadowFBInit(ScreenPtr pScreen, RefreshAreaFuncPtr refreshArea)
{
    return ShadowFBInit2(pScreen, NULL, refreshArea);
}

/**********************************************************/

static Bool
ShadowEnterVT(ScrnInfoPtr pScrn)
{
    Bool ret;
    ShadowScreenPtr pPriv = GET_SCREEN_PRIVATE(pScrn->pScreen);

    pScrn->EnterVT = pPriv->EnterVT;
    ret = (*pPriv->EnterVT) (pScrn);
    pPriv->EnterVT = pScrn->EnterVT;
    pScrn->EnterVT = ShadowEnterVT;
    if (ret) {
        pPriv->vtSema = TRUE;
        return TRUE;
    }

    return FALSE;
}

static void
ShadowLeaveVT(ScrnInfoPtr pScrn)
{
    ShadowScreenPtr pPriv = GET_SCREEN_PRIVATE(pScrn->pScreen);

    pPriv->vtSema = FALSE;

    pScrn->LeaveVT = pPriv->LeaveVT;
    (*pPriv->LeaveVT) (pScrn);
    pPriv->LeaveVT = pScrn->LeaveVT;
    pScrn->LeaveVT = ShadowLeaveVT;
}

/**********************************************************/

static Bool
ShadowCloseScreen(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    ShadowScreenPtr pPriv = GET_SCREEN_PRIVATE(pScreen);
    PictureScreenPtr ps = GetPictureScreenIfSet(pScreen);

    pScreen->CloseScreen = pPriv->CloseScreen;
    pScreen->CopyWindow = pPriv->CopyWindow;
    pScreen->CreateGC = pPriv->CreateGC;
    pScreen->ModifyPixmapHeader = pPriv->ModifyPixmapHeader;

    pScrn->EnterVT = pPriv->EnterVT;
    pScrn->LeaveVT = pPriv->LeaveVT;

    if (ps) {
        ps->Composite = pPriv->Composite;
    }

    free((pointer) pPriv);

    return (*pScreen->CloseScreen) (pScreen);
}

static void
ShadowCopyWindow(WindowPtr pWin, DDXPointRec ptOldOrg, RegionPtr prgn)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    ShadowScreenPtr pPriv = GET_SCREEN_PRIVATE(pScreen);
    int num = 0;
    RegionRec rgnDst;

    if (pPriv->vtSema) {
        RegionNull(&rgnDst);
        RegionCopy(&rgnDst, prgn);

        RegionTranslate(&rgnDst,
                        pWin->drawable.x - ptOldOrg.x,
                        pWin->drawable.y - ptOldOrg.y);
        RegionIntersect(&rgnDst, &pWin->borderClip, &rgnDst);
        if ((num = RegionNumRects(&rgnDst))) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, num, RegionRects(&rgnDst));
        }
        else {
            RegionUninit(&rgnDst);
        }
    }

    pScreen->CopyWindow = pPriv->CopyWindow;
    (*pScreen->CopyWindow) (pWin, ptOldOrg, prgn);
    pScreen->CopyWindow = ShadowCopyWindow;

    if (num) {
        if (pPriv->postRefresh)
            (*pPriv->postRefresh) (pPriv->pScrn, num, RegionRects(&rgnDst));
        RegionUninit(&rgnDst);
    }
}

static void
ShadowComposite(CARD8 op,
                PicturePtr pSrc,
                PicturePtr pMask,
                PicturePtr pDst,
                INT16 xSrc,
                INT16 ySrc,
                INT16 xMask,
                INT16 yMask,
                INT16 xDst, INT16 yDst, CARD16 width, CARD16 height)
{
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    ShadowScreenPtr pPriv = GET_SCREEN_PRIVATE(pScreen);
    PictureScreenPtr ps = GetPictureScreen(pScreen);
    BoxRec box;
    BoxPtr extents;
    Bool boxNotEmpty = FALSE;

    if (pPriv->vtSema && pDst->pDrawable->type == DRAWABLE_WINDOW) {

        box.x1 = pDst->pDrawable->x + xDst;
        box.y1 = pDst->pDrawable->y + yDst;
        box.x2 = box.x1 + width;
        box.y2 = box.y1 + height;

        extents = &pDst->pCompositeClip->extents;
        if (box.x1 < extents->x1)
            box.x1 = extents->x1;
        if (box.x2 > extents->x2)
            box.x2 = extents->x2;
        if (box.y1 < extents->y1)
            box.y1 = extents->y1;
        if (box.y2 > extents->y2)
            box.y2 = extents->y2;

        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    ps->Composite = pPriv->Composite;
    (*ps->Composite) (op, pSrc, pMask, pDst, xSrc, ySrc,
                      xMask, yMask, xDst, yDst, width, height);
    ps->Composite = ShadowComposite;

    if (pPriv->postRefresh && boxNotEmpty) {
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);
    }
}

/**********************************************************/

static void ShadowValidateGC(GCPtr, unsigned long, DrawablePtr);
static void ShadowChangeGC(GCPtr, unsigned long);
static void ShadowCopyGC(GCPtr, unsigned long, GCPtr);
static void ShadowDestroyGC(GCPtr);
static void ShadowChangeClip(GCPtr, int, pointer, int);
static void ShadowDestroyClip(GCPtr);
static void ShadowCopyClip(GCPtr, GCPtr);

GCFuncs ShadowGCFuncs = {
    ShadowValidateGC, ShadowChangeGC, ShadowCopyGC, ShadowDestroyGC,
    ShadowChangeClip, ShadowDestroyClip, ShadowCopyClip
};

extern GCOps ShadowGCOps;

static Bool
ShadowCreateGC(GCPtr pGC)
{
    ScreenPtr pScreen = pGC->pScreen;
    ShadowScreenPtr pPriv = GET_SCREEN_PRIVATE(pScreen);
    ShadowGCPtr pGCPriv = GET_GC_PRIVATE(pGC);
    Bool ret;

    pScreen->CreateGC = pPriv->CreateGC;
    if ((ret = (*pScreen->CreateGC) (pGC))) {
        pGCPriv->ops = NULL;
        pGCPriv->funcs = pGC->funcs;
        pGC->funcs = &ShadowGCFuncs;
    }
    pScreen->CreateGC = ShadowCreateGC;

    return ret;
}

static void
ShadowValidateGC(GCPtr pGC, unsigned long changes, DrawablePtr pDraw)
{
    SHADOW_GC_FUNC_PROLOGUE(pGC);
    (*pGC->funcs->ValidateGC) (pGC, changes, pDraw);
    if (pDraw->type == DRAWABLE_WINDOW)
        pGCPriv->ops = pGC->ops;        /* just so it's not NULL */
    else
        pGCPriv->ops = NULL;
    SHADOW_GC_FUNC_EPILOGUE(pGC);
}

static void
ShadowDestroyGC(GCPtr pGC)
{
    SHADOW_GC_FUNC_PROLOGUE(pGC);
    (*pGC->funcs->DestroyGC) (pGC);
    SHADOW_GC_FUNC_EPILOGUE(pGC);
}

static void
ShadowChangeGC(GCPtr pGC, unsigned long mask)
{
    SHADOW_GC_FUNC_PROLOGUE(pGC);
    (*pGC->funcs->ChangeGC) (pGC, mask);
    SHADOW_GC_FUNC_EPILOGUE(pGC);
}

static void
ShadowCopyGC(GCPtr pGCSrc, unsigned long mask, GCPtr pGCDst)
{
    SHADOW_GC_FUNC_PROLOGUE(pGCDst);
    (*pGCDst->funcs->CopyGC) (pGCSrc, mask, pGCDst);
    SHADOW_GC_FUNC_EPILOGUE(pGCDst);
}

static void
ShadowChangeClip(GCPtr pGC, int type, pointer pvalue, int nrects)
{
    SHADOW_GC_FUNC_PROLOGUE(pGC);
    (*pGC->funcs->ChangeClip) (pGC, type, pvalue, nrects);
    SHADOW_GC_FUNC_EPILOGUE(pGC);
}

static void
ShadowCopyClip(GCPtr pgcDst, GCPtr pgcSrc)
{
    SHADOW_GC_FUNC_PROLOGUE(pgcDst);
    (*pgcDst->funcs->CopyClip) (pgcDst, pgcSrc);
    SHADOW_GC_FUNC_EPILOGUE(pgcDst);
}

static void
ShadowDestroyClip(GCPtr pGC)
{
    SHADOW_GC_FUNC_PROLOGUE(pGC);
    (*pGC->funcs->DestroyClip) (pGC);
    SHADOW_GC_FUNC_EPILOGUE(pGC);
}

/**********************************************************/

static void
ShadowFillSpans(DrawablePtr pDraw,
                GC * pGC,
                int nInit, DDXPointPtr pptInit, int *pwidthInit, int fSorted)
{
    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && nInit) {
        DDXPointPtr ppt = pptInit;
        int *pwidth = pwidthInit;
        int i = nInit;
        BoxRec box;
        Bool boxNotEmpty = FALSE;

        box.x1 = ppt->x;
        box.x2 = box.x1 + *pwidth;
        box.y2 = box.y1 = ppt->y;

        while (--i) {
            ppt++;
            pwidth++;
            if (box.x1 > ppt->x)
                box.x1 = ppt->x;
            if (box.x2 < (ppt->x + *pwidth))
                box.x2 = ppt->x + *pwidth;
            if (box.y1 > ppt->y)
                box.y1 = ppt->y;
            else if (box.y2 < ppt->y)
                box.y2 = ppt->y;
        }

        box.y2++;

        if (!pGC->miTranslate) {
            TRANSLATE_BOX(box, pDraw);
        }
        TRIM_BOX(box, pGC);

        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }

        (*pGC->ops->FillSpans) (pDraw, pGC, nInit, pptInit, pwidthInit,
                                fSorted);

        if (boxNotEmpty && pPriv->postRefresh)
            (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);
    }
    else
        (*pGC->ops->FillSpans) (pDraw, pGC, nInit, pptInit, pwidthInit,
                                fSorted);

    SHADOW_GC_OP_EPILOGUE(pGC);
}

static void
ShadowSetSpans(DrawablePtr pDraw,
               GCPtr pGC,
               char *pcharsrc,
               DDXPointPtr pptInit, int *pwidthInit, int nspans, int fSorted)
{
    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && nspans) {
        DDXPointPtr ppt = pptInit;
        int *pwidth = pwidthInit;
        int i = nspans;
        BoxRec box;
        Bool boxNotEmpty = FALSE;

        box.x1 = ppt->x;
        box.x2 = box.x1 + *pwidth;
        box.y2 = box.y1 = ppt->y;

        while (--i) {
            ppt++;
            pwidth++;
            if (box.x1 > ppt->x)
                box.x1 = ppt->x;
            if (box.x2 < (ppt->x + *pwidth))
                box.x2 = ppt->x + *pwidth;
            if (box.y1 > ppt->y)
                box.y1 = ppt->y;
            else if (box.y2 < ppt->y)
                box.y2 = ppt->y;
        }

        box.y2++;

        if (!pGC->miTranslate) {
            TRANSLATE_BOX(box, pDraw);
        }
        TRIM_BOX(box, pGC);

        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }

        (*pGC->ops->SetSpans) (pDraw, pGC, pcharsrc, pptInit,
                               pwidthInit, nspans, fSorted);

        if (boxNotEmpty && pPriv->postRefresh)
            (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);
    }
    else
        (*pGC->ops->SetSpans) (pDraw, pGC, pcharsrc, pptInit,
                               pwidthInit, nspans, fSorted);

    SHADOW_GC_OP_EPILOGUE(pGC);
}

static void
ShadowPutImage(DrawablePtr pDraw,
               GCPtr pGC,
               int depth,
               int x, int y, int w, int h,
               int leftPad, int format, char *pImage)
{
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw)) {
        box.x1 = x + pDraw->x;
        box.x2 = box.x1 + w;
        box.y1 = y + pDraw->y;
        box.y2 = box.y1 + h;

        TRIM_BOX(box, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    (*pGC->ops->PutImage) (pDraw, pGC, depth, x, y, w, h,
                           leftPad, format, pImage);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);

}

static RegionPtr
ShadowCopyArea(DrawablePtr pSrc,
               DrawablePtr pDst,
               GC * pGC,
               int srcx, int srcy, int width, int height, int dstx, int dsty)
{
    RegionPtr ret;
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDst)) {
        box.x1 = dstx + pDst->x;
        box.x2 = box.x1 + width;
        box.y1 = dsty + pDst->y;
        box.y2 = box.y1 + height;

        TRIM_BOX(box, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    ret = (*pGC->ops->CopyArea) (pSrc, pDst,
                                 pGC, srcx, srcy, width, height, dstx, dsty);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);

    return ret;
}

static RegionPtr
ShadowCopyPlane(DrawablePtr pSrc,
                DrawablePtr pDst,
                GCPtr pGC,
                int srcx, int srcy,
                int width, int height,
                int dstx, int dsty, unsigned long bitPlane)
{
    RegionPtr ret;
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDst)) {
        box.x1 = dstx + pDst->x;
        box.x2 = box.x1 + width;
        box.y1 = dsty + pDst->y;
        box.y2 = box.y1 + height;

        TRIM_BOX(box, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    ret = (*pGC->ops->CopyPlane) (pSrc, pDst,
                                  pGC, srcx, srcy, width, height, dstx, dsty,
                                  bitPlane);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);

    return ret;
}

static void
ShadowPolyPoint(DrawablePtr pDraw,
                GCPtr pGC, int mode, int nptInit, xPoint * pptInit)
{
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && nptInit) {
        xPoint *ppt = pptInit;
        int npt = nptInit;

        box.x2 = box.x1 = pptInit->x;
        box.y2 = box.y1 = pptInit->y;

        /* this could be slow if the points were spread out */

        while (--npt) {
            ppt++;
            if (box.x1 > ppt->x)
                box.x1 = ppt->x;
            else if (box.x2 < ppt->x)
                box.x2 = ppt->x;
            if (box.y1 > ppt->y)
                box.y1 = ppt->y;
            else if (box.y2 < ppt->y)
                box.y2 = ppt->y;
        }

        box.x2++;
        box.y2++;

        TRIM_AND_TRANSLATE_BOX(box, pDraw, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    (*pGC->ops->PolyPoint) (pDraw, pGC, mode, nptInit, pptInit);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);
}

static void
ShadowPolylines(DrawablePtr pDraw,
                GCPtr pGC, int mode, int nptInit, DDXPointPtr pptInit)
{
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && nptInit) {
        DDXPointPtr ppt = pptInit;
        int npt = nptInit;
        int extra = pGC->lineWidth >> 1;

        box.x2 = box.x1 = pptInit->x;
        box.y2 = box.y1 = pptInit->y;

        if (npt > 1) {
            if (pGC->joinStyle == JoinMiter)
                extra = 6 * pGC->lineWidth;
            else if (pGC->capStyle == CapProjecting)
                extra = pGC->lineWidth;
        }

        if (mode == CoordModePrevious) {
            int x = box.x1;
            int y = box.y1;

            while (--npt) {
                ppt++;
                x += ppt->x;
                y += ppt->y;
                if (box.x1 > x)
                    box.x1 = x;
                else if (box.x2 < x)
                    box.x2 = x;
                if (box.y1 > y)
                    box.y1 = y;
                else if (box.y2 < y)
                    box.y2 = y;
            }
        }
        else {
            while (--npt) {
                ppt++;
                if (box.x1 > ppt->x)
                    box.x1 = ppt->x;
                else if (box.x2 < ppt->x)
                    box.x2 = ppt->x;
                if (box.y1 > ppt->y)
                    box.y1 = ppt->y;
                else if (box.y2 < ppt->y)
                    box.y2 = ppt->y;
            }
        }

        box.x2++;
        box.y2++;

        if (extra) {
            box.x1 -= extra;
            box.x2 += extra;
            box.y1 -= extra;
            box.y2 += extra;
        }

        TRIM_AND_TRANSLATE_BOX(box, pDraw, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    (*pGC->ops->Polylines) (pDraw, pGC, mode, nptInit, pptInit);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);
}

static void
ShadowPolySegment(DrawablePtr pDraw,
                  GCPtr pGC, int nsegInit, xSegment * pSegInit)
{
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && nsegInit) {
        int extra = pGC->lineWidth;
        xSegment *pSeg = pSegInit;
        int nseg = nsegInit;

        if (pGC->capStyle != CapProjecting)
            extra >>= 1;

        if (pSeg->x2 > pSeg->x1) {
            box.x1 = pSeg->x1;
            box.x2 = pSeg->x2;
        }
        else {
            box.x2 = pSeg->x1;
            box.x1 = pSeg->x2;
        }

        if (pSeg->y2 > pSeg->y1) {
            box.y1 = pSeg->y1;
            box.y2 = pSeg->y2;
        }
        else {
            box.y2 = pSeg->y1;
            box.y1 = pSeg->y2;
        }

        while (--nseg) {
            pSeg++;
            if (pSeg->x2 > pSeg->x1) {
                if (pSeg->x1 < box.x1)
                    box.x1 = pSeg->x1;
                if (pSeg->x2 > box.x2)
                    box.x2 = pSeg->x2;
            }
            else {
                if (pSeg->x2 < box.x1)
                    box.x1 = pSeg->x2;
                if (pSeg->x1 > box.x2)
                    box.x2 = pSeg->x1;
            }
            if (pSeg->y2 > pSeg->y1) {
                if (pSeg->y1 < box.y1)
                    box.y1 = pSeg->y1;
                if (pSeg->y2 > box.y2)
                    box.y2 = pSeg->y2;
            }
            else {
                if (pSeg->y2 < box.y1)
                    box.y1 = pSeg->y2;
                if (pSeg->y1 > box.y2)
                    box.y2 = pSeg->y1;
            }
        }

        box.x2++;
        box.y2++;

        if (extra) {
            box.x1 -= extra;
            box.x2 += extra;
            box.y1 -= extra;
            box.y2 += extra;
        }

        TRIM_AND_TRANSLATE_BOX(box, pDraw, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    (*pGC->ops->PolySegment) (pDraw, pGC, nsegInit, pSegInit);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);
}

static void
ShadowPolyRectangle(DrawablePtr pDraw,
                    GCPtr pGC, int nRectsInit, xRectangle *pRectsInit)
{
    BoxRec box;
    BoxPtr pBoxInit = NULL;
    Bool boxNotEmpty = FALSE;
    int num = 0;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && nRectsInit) {
        xRectangle *pRects = pRectsInit;
        int nRects = nRectsInit;

        if (nRects >= 32) {
            int extra = pGC->lineWidth >> 1;

            box.x1 = pRects->x;
            box.x2 = box.x1 + pRects->width;
            box.y1 = pRects->y;
            box.y2 = box.y1 + pRects->height;

            while (--nRects) {
                pRects++;
                if (box.x1 > pRects->x)
                    box.x1 = pRects->x;
                if (box.x2 < (pRects->x + pRects->width))
                    box.x2 = pRects->x + pRects->width;
                if (box.y1 > pRects->y)
                    box.y1 = pRects->y;
                if (box.y2 < (pRects->y + pRects->height))
                    box.y2 = pRects->y + pRects->height;
            }

            if (extra) {
                box.x1 -= extra;
                box.x2 += extra;
                box.y1 -= extra;
                box.y2 += extra;
            }

            box.x2++;
            box.y2++;

            TRIM_AND_TRANSLATE_BOX(box, pDraw, pGC);
            if (BOX_NOT_EMPTY(box)) {
                if (pPriv->preRefresh)
                    (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
                boxNotEmpty = TRUE;
            }
        }
        else {
            BoxPtr pbox;
            int offset1, offset2, offset3;

            offset2 = pGC->lineWidth;
            if (!offset2)
                offset2 = 1;
            offset1 = offset2 >> 1;
            offset3 = offset2 - offset1;

            pBoxInit = (BoxPtr) malloc(nRects * 4 * sizeof(BoxRec));
            pbox = pBoxInit;

            while (nRects--) {
                pbox->x1 = pRects->x - offset1;
                pbox->y1 = pRects->y - offset1;
                pbox->x2 = pbox->x1 + pRects->width + offset2;
                pbox->y2 = pbox->y1 + offset2;
                TRIM_AND_TRANSLATE_BOX((*pbox), pDraw, pGC);
                if (BOX_NOT_EMPTY((*pbox))) {
                    num++;
                    pbox++;
                }

                pbox->x1 = pRects->x - offset1;
                pbox->y1 = pRects->y + offset3;
                pbox->x2 = pbox->x1 + offset2;
                pbox->y2 = pbox->y1 + pRects->height - offset2;
                TRIM_AND_TRANSLATE_BOX((*pbox), pDraw, pGC);
                if (BOX_NOT_EMPTY((*pbox))) {
                    num++;
                    pbox++;
                }

                pbox->x1 = pRects->x + pRects->width - offset1;
                pbox->y1 = pRects->y + offset3;
                pbox->x2 = pbox->x1 + offset2;
                pbox->y2 = pbox->y1 + pRects->height - offset2;
                TRIM_AND_TRANSLATE_BOX((*pbox), pDraw, pGC);
                if (BOX_NOT_EMPTY((*pbox))) {
                    num++;
                    pbox++;
                }

                pbox->x1 = pRects->x - offset1;
                pbox->y1 = pRects->y + pRects->height - offset1;
                pbox->x2 = pbox->x1 + pRects->width + offset2;
                pbox->y2 = pbox->y1 + offset2;
                TRIM_AND_TRANSLATE_BOX((*pbox), pDraw, pGC);
                if (BOX_NOT_EMPTY((*pbox))) {
                    num++;
                    pbox++;
                }

                pRects++;
            }

            if (num) {
                if (pPriv->preRefresh)
                    (*pPriv->preRefresh) (pPriv->pScrn, num, pBoxInit);
            }
            else {
                free(pBoxInit);
            }
        }
    }

    (*pGC->ops->PolyRectangle) (pDraw, pGC, nRectsInit, pRectsInit);

    if (boxNotEmpty && pPriv->postRefresh) {
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);
    }
    else if (num) {
        if (pPriv->postRefresh)
            (*pPriv->postRefresh) (pPriv->pScrn, num, pBoxInit);
        free(pBoxInit);
    }

    SHADOW_GC_OP_EPILOGUE(pGC);

}

static void
ShadowPolyArc(DrawablePtr pDraw, GCPtr pGC, int narcsInit, xArc * parcsInit)
{
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && narcsInit) {
        int narcs = narcsInit;
        xArc *parcs = parcsInit;
        int extra = pGC->lineWidth >> 1;

        box.x1 = parcs->x;
        box.x2 = box.x1 + parcs->width;
        box.y1 = parcs->y;
        box.y2 = box.y1 + parcs->height;

        /* should I break these up instead ? */

        while (--narcs) {
            parcs++;
            if (box.x1 > parcs->x)
                box.x1 = parcs->x;
            if (box.x2 < (parcs->x + parcs->width))
                box.x2 = parcs->x + parcs->width;
            if (box.y1 > parcs->y)
                box.y1 = parcs->y;
            if (box.y2 < (parcs->y + parcs->height))
                box.y2 = parcs->y + parcs->height;
        }

        if (extra) {
            box.x1 -= extra;
            box.x2 += extra;
            box.y1 -= extra;
            box.y2 += extra;
        }

        box.x2++;
        box.y2++;

        TRIM_AND_TRANSLATE_BOX(box, pDraw, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    (*pGC->ops->PolyArc) (pDraw, pGC, narcsInit, parcsInit);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);

}

static void
ShadowFillPolygon(DrawablePtr pDraw,
                  GCPtr pGC,
                  int shape, int mode, int count, DDXPointPtr pptInit)
{
    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && (count > 2)) {
        DDXPointPtr ppt = pptInit;
        int i = count;
        BoxRec box;
        Bool boxNotEmpty = FALSE;

        box.x2 = box.x1 = ppt->x;
        box.y2 = box.y1 = ppt->y;

        if (mode != CoordModeOrigin) {
            int x = box.x1;
            int y = box.y1;

            while (--i) {
                ppt++;
                x += ppt->x;
                y += ppt->y;
                if (box.x1 > x)
                    box.x1 = x;
                else if (box.x2 < x)
                    box.x2 = x;
                if (box.y1 > y)
                    box.y1 = y;
                else if (box.y2 < y)
                    box.y2 = y;
            }
        }
        else {
            while (--i) {
                ppt++;
                if (box.x1 > ppt->x)
                    box.x1 = ppt->x;
                else if (box.x2 < ppt->x)
                    box.x2 = ppt->x;
                if (box.y1 > ppt->y)
                    box.y1 = ppt->y;
                else if (box.y2 < ppt->y)
                    box.y2 = ppt->y;
            }
        }

        box.x2++;
        box.y2++;

        TRIM_AND_TRANSLATE_BOX(box, pDraw, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }

        (*pGC->ops->FillPolygon) (pDraw, pGC, shape, mode, count, pptInit);

        if (boxNotEmpty && pPriv->postRefresh)
            (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);
    }
    else
        (*pGC->ops->FillPolygon) (pDraw, pGC, shape, mode, count, pptInit);

    SHADOW_GC_OP_EPILOGUE(pGC);
}

static void
ShadowPolyFillRect(DrawablePtr pDraw,
                   GCPtr pGC, int nRectsInit, xRectangle *pRectsInit)
{
    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && nRectsInit) {
        BoxRec box;
        Bool boxNotEmpty = FALSE;
        xRectangle *pRects = pRectsInit;
        int nRects = nRectsInit;

        box.x1 = pRects->x;
        box.x2 = box.x1 + pRects->width;
        box.y1 = pRects->y;
        box.y2 = box.y1 + pRects->height;

        while (--nRects) {
            pRects++;
            if (box.x1 > pRects->x)
                box.x1 = pRects->x;
            if (box.x2 < (pRects->x + pRects->width))
                box.x2 = pRects->x + pRects->width;
            if (box.y1 > pRects->y)
                box.y1 = pRects->y;
            if (box.y2 < (pRects->y + pRects->height))
                box.y2 = pRects->y + pRects->height;
        }

        /* cfb messes with the pRectsInit so we have to do our
           calculations first */

        TRIM_AND_TRANSLATE_BOX(box, pDraw, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }

        (*pGC->ops->PolyFillRect) (pDraw, pGC, nRectsInit, pRectsInit);

        if (boxNotEmpty && pPriv->postRefresh)
            (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);
    }
    else
        (*pGC->ops->PolyFillRect) (pDraw, pGC, nRectsInit, pRectsInit);

    SHADOW_GC_OP_EPILOGUE(pGC);
}

static void
ShadowPolyFillArc(DrawablePtr pDraw, GCPtr pGC, int narcsInit, xArc * parcsInit)
{
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && narcsInit) {
        xArc *parcs = parcsInit;
        int narcs = narcsInit;

        box.x1 = parcs->x;
        box.x2 = box.x1 + parcs->width;
        box.y1 = parcs->y;
        box.y2 = box.y1 + parcs->height;

        /* should I break these up instead ? */

        while (--narcs) {
            parcs++;
            if (box.x1 > parcs->x)
                box.x1 = parcs->x;
            if (box.x2 < (parcs->x + parcs->width))
                box.x2 = parcs->x + parcs->width;
            if (box.y1 > parcs->y)
                box.y1 = parcs->y;
            if (box.y2 < (parcs->y + parcs->height))
                box.y2 = parcs->y + parcs->height;
        }

        TRIM_AND_TRANSLATE_BOX(box, pDraw, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    (*pGC->ops->PolyFillArc) (pDraw, pGC, narcsInit, parcsInit);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);
}

static void
ShadowTextExtent(FontPtr pFont, int count, char *chars,
                 FontEncoding fontEncoding, BoxPtr box)
{
    unsigned long n, i;
    int w;
    CharInfoPtr charinfo[255];  /* encoding only has 1 byte for count */

    GetGlyphs(pFont, (unsigned long) count, (unsigned char *) chars,
              fontEncoding, &n, charinfo);
    w = 0;
    for (i = 0; i < n; i++) {
        w += charinfo[i]->metrics.characterWidth;
    }
    if (i) {
        w += charinfo[i - 1]->metrics.rightSideBearing;
    }

    box->x1 = 0;
    if (n) {
        if (charinfo[0]->metrics.leftSideBearing < 0) {
            box->x1 = charinfo[0]->metrics.leftSideBearing;
        }
    }
    box->x2 = w;
    box->y1 = -FONTMAXBOUNDS(pFont, ascent);
    box->y2 = FONTMAXBOUNDS(pFont, descent);
}

static void
ShadowFontToBox(BoxPtr BB, DrawablePtr pDrawable, GCPtr pGC, int x, int y,
                int count, char *chars, int wide)
{
    FontPtr pFont;

    pFont = pGC->font;
    if (pFont->info.constantWidth) {
        int ascent, descent, left, right = 0;

        ascent = max(pFont->info.fontAscent, pFont->info.maxbounds.ascent);
        descent = max(pFont->info.fontDescent, pFont->info.maxbounds.descent);
        left = pFont->info.maxbounds.leftSideBearing;
        if (count > 0) {
            right = (count - 1) * pFont->info.maxbounds.characterWidth;
        }
        right += pFont->info.maxbounds.rightSideBearing;
        BB->x1 =
            max(pDrawable->x + x - left,
                RegionExtents(&((WindowPtr) pDrawable)->winSize)->x1);
        BB->y1 =
            max(pDrawable->y + y - ascent,
                RegionExtents(&((WindowPtr) pDrawable)->winSize)->y1);
        BB->x2 =
            min(pDrawable->x + x + right,
                RegionExtents(&((WindowPtr) pDrawable)->winSize)->x2);
        BB->y2 =
            min(pDrawable->y + y + descent,
                RegionExtents(&((WindowPtr) pDrawable)->winSize)->y2);
    }
    else {
        ShadowTextExtent(pFont, count, chars, wide ? (FONTLASTROW(pFont) == 0)
                         ? Linear16Bit : TwoD16Bit : Linear8Bit, BB);
        BB->x1 =
            max(pDrawable->x + x + BB->x1,
                RegionExtents(&((WindowPtr) pDrawable)->winSize)->x1);
        BB->y1 =
            max(pDrawable->y + y + BB->y1,
                RegionExtents(&((WindowPtr) pDrawable)->winSize)->y1);
        BB->x2 =
            min(pDrawable->x + x + BB->x2,
                RegionExtents(&((WindowPtr) pDrawable)->winSize)->x2);
        BB->y2 =
            min(pDrawable->y + y + BB->y2,
                RegionExtents(&((WindowPtr) pDrawable)->winSize)->y2);
    }
}

static int
ShadowPolyText8(DrawablePtr pDraw,
                GCPtr pGC, int x, int y, int count, char *chars)
{
    int width;
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw)) {
        ShadowFontToBox(&box, pDraw, pGC, x, y, count, chars, 0);

        TRIM_BOX(box, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    width = (*pGC->ops->PolyText8) (pDraw, pGC, x, y, count, chars);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);

    return width;
}

static int
ShadowPolyText16(DrawablePtr pDraw,
                 GCPtr pGC, int x, int y, int count, unsigned short *chars)
{
    int width;
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw)) {
        ShadowFontToBox(&box, pDraw, pGC, x, y, count, (char *) chars, 1);

        TRIM_BOX(box, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    width = (*pGC->ops->PolyText16) (pDraw, pGC, x, y, count, chars);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);

    return width;
}

static void
ShadowImageText8(DrawablePtr pDraw,
                 GCPtr pGC, int x, int y, int count, char *chars)
{
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && count) {
        int top, bot, Min, Max;

        top = max(FONTMAXBOUNDS(pGC->font, ascent), FONTASCENT(pGC->font));
        bot = max(FONTMAXBOUNDS(pGC->font, descent), FONTDESCENT(pGC->font));

        Min = count * FONTMINBOUNDS(pGC->font, characterWidth);
        if (Min > 0)
            Min = 0;
        Max = count * FONTMAXBOUNDS(pGC->font, characterWidth);
        if (Max < 0)
            Max = 0;

        /* ugh */
        box.x1 = pDraw->x + x + Min + FONTMINBOUNDS(pGC->font, leftSideBearing);
        box.x2 = pDraw->x + x + Max +
            FONTMAXBOUNDS(pGC->font, rightSideBearing);

        box.y1 = pDraw->y + y - top;
        box.y2 = pDraw->y + y + bot;

        TRIM_BOX(box, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    (*pGC->ops->ImageText8) (pDraw, pGC, x, y, count, chars);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);
}

static void
ShadowImageText16(DrawablePtr pDraw,
                  GCPtr pGC, int x, int y, int count, unsigned short *chars)
{
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && count) {
        int top, bot, Min, Max;

        top = max(FONTMAXBOUNDS(pGC->font, ascent), FONTASCENT(pGC->font));
        bot = max(FONTMAXBOUNDS(pGC->font, descent), FONTDESCENT(pGC->font));

        Min = count * FONTMINBOUNDS(pGC->font, characterWidth);
        if (Min > 0)
            Min = 0;
        Max = count * FONTMAXBOUNDS(pGC->font, characterWidth);
        if (Max < 0)
            Max = 0;

        /* ugh */
        box.x1 = pDraw->x + x + Min + FONTMINBOUNDS(pGC->font, leftSideBearing);
        box.x2 = pDraw->x + x + Max +
            FONTMAXBOUNDS(pGC->font, rightSideBearing);

        box.y1 = pDraw->y + y - top;
        box.y2 = pDraw->y + y + bot;

        TRIM_BOX(box, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    (*pGC->ops->ImageText16) (pDraw, pGC, x, y, count, chars);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);
}

static void
ShadowImageGlyphBlt(DrawablePtr pDraw,
                    GCPtr pGC,
                    int x, int y,
                    unsigned int nglyphInit,
                    CharInfoPtr * ppciInit, pointer pglyphBase)
{
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && nglyphInit) {
        CharInfoPtr *ppci = ppciInit;
        unsigned int nglyph = nglyphInit;
        int top, bot, width = 0;

        top = max(FONTMAXBOUNDS(pGC->font, ascent), FONTASCENT(pGC->font));
        bot = max(FONTMAXBOUNDS(pGC->font, descent), FONTDESCENT(pGC->font));

        box.x1 = ppci[0]->metrics.leftSideBearing;
        if (box.x1 > 0)
            box.x1 = 0;
        box.x2 = ppci[nglyph - 1]->metrics.rightSideBearing -
            ppci[nglyph - 1]->metrics.characterWidth;
        if (box.x2 < 0)
            box.x2 = 0;

        box.x2 += pDraw->x + x;
        box.x1 += pDraw->x + x;

        while (nglyph--) {
            width += (*ppci)->metrics.characterWidth;
            ppci++;
        }

        if (width > 0)
            box.x2 += width;
        else
            box.x1 += width;

        box.y1 = pDraw->y + y - top;
        box.y2 = pDraw->y + y + bot;

        TRIM_BOX(box, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    (*pGC->ops->ImageGlyphBlt) (pDraw, pGC, x, y, nglyphInit,
                                ppciInit, pglyphBase);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);
}

static void
ShadowPolyGlyphBlt(DrawablePtr pDraw,
                   GCPtr pGC,
                   int x, int y,
                   unsigned int nglyphInit,
                   CharInfoPtr * ppciInit, pointer pglyphBase)
{
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw) && nglyphInit) {
        CharInfoPtr *ppci = ppciInit;
        unsigned int nglyph = nglyphInit;

        /* ugh */
        box.x1 = pDraw->x + x + ppci[0]->metrics.leftSideBearing;
        box.x2 = pDraw->x + x + ppci[nglyph - 1]->metrics.rightSideBearing;

        if (nglyph > 1) {
            int width = 0;

            while (--nglyph) {
                width += (*ppci)->metrics.characterWidth;
                ppci++;
            }

            if (width > 0)
                box.x2 += width;
            else
                box.x1 += width;
        }

        box.y1 = pDraw->y + y - FONTMAXBOUNDS(pGC->font, ascent);
        box.y2 = pDraw->y + y + FONTMAXBOUNDS(pGC->font, descent);

        TRIM_BOX(box, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    (*pGC->ops->PolyGlyphBlt) (pDraw, pGC, x, y, nglyphInit,
                               ppciInit, pglyphBase);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);
}

static void
ShadowPushPixels(GCPtr pGC,
                 PixmapPtr pBitMap,
                 DrawablePtr pDraw, int dx, int dy, int xOrg, int yOrg)
{
    BoxRec box;
    Bool boxNotEmpty = FALSE;

    SHADOW_GC_OP_PROLOGUE(pGC);

    if (IS_VISIBLE(pDraw)) {
        box.x1 = xOrg;
        box.y1 = yOrg;

        if (!pGC->miTranslate) {
            box.x1 += pDraw->x;
            box.y1 += pDraw->y;
        }

        box.x2 = box.x1 + dx;
        box.y2 = box.y1 + dy;

        TRIM_BOX(box, pGC);
        if (BOX_NOT_EMPTY(box)) {
            if (pPriv->preRefresh)
                (*pPriv->preRefresh) (pPriv->pScrn, 1, &box);
            boxNotEmpty = TRUE;
        }
    }

    (*pGC->ops->PushPixels) (pGC, pBitMap, pDraw, dx, dy, xOrg, yOrg);

    if (boxNotEmpty && pPriv->postRefresh)
        (*pPriv->postRefresh) (pPriv->pScrn, 1, &box);

    SHADOW_GC_OP_EPILOGUE(pGC);
}

GCOps ShadowGCOps = {
    ShadowFillSpans, ShadowSetSpans,
    ShadowPutImage, ShadowCopyArea,
    ShadowCopyPlane, ShadowPolyPoint,
    ShadowPolylines, ShadowPolySegment,
    ShadowPolyRectangle, ShadowPolyArc,
    ShadowFillPolygon, ShadowPolyFillRect,
    ShadowPolyFillArc, ShadowPolyText8,
    ShadowPolyText16, ShadowImageText8,
    ShadowImageText16, ShadowImageGlyphBlt,
    ShadowPolyGlyphBlt, ShadowPushPixels,
};
