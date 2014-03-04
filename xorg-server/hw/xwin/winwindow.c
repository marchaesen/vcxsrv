/*
 *Copyright (C) 1994-2000 The XFree86 Project, Inc. All Rights Reserved.
 *
 *Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 *"Software"), to deal in the Software without restriction, including
 *without limitation the rights to use, copy, modify, merge, publish,
 *distribute, sublicense, and/or sell copies of the Software, and to
 *permit persons to whom the Software is furnished to do so, subject to
 *the following conditions:
 *
 *The above copyright notice and this permission notice shall be
 *included in all copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *NONINFRINGEMENT. IN NO EVENT SHALL THE XFREE86 PROJECT BE LIABLE FOR
 *ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *Except as contained in this notice, the name of the XFree86 Project
 *shall not be used in advertising or otherwise to promote the sale, use
 *or other dealings in this Software without prior written authorization
 *from the XFree86 Project.
 *
 * Authors:	Harold L Hunt II
 *		Kensuke Matsuzaki
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"

/*
 * Prototypes for local functions
 */

static int
 winAddRgn(WindowPtr pWindow, void *data);

static
    void
 winUpdateRgnRootless(WindowPtr pWindow);

static
    void
 winReshapeRootless(WindowPtr pWin);

#ifdef XWIN_NATIVEGDI
/* See Porting Layer Definition - p. 37 */
/* See mfb/mfbwindow.c - mfbCreateWindow() */

Bool
winCreateWindowNativeGDI(WindowPtr pWin)
{
    Bool fResult = TRUE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winCreateWindowNativeGDI (%p)\n", pWin);
#endif

    WIN_UNWRAP(CreateWindow);
    fResult = (*pScreen->CreateWindow) (pWin);
    WIN_WRAP(CreateWindow, winCreateWindowNativeGDI);

    return fResult;
}

/* See Porting Layer Definition - p. 37 */
/* See mfb/mfbwindow.c - mfbDestroyWindow() */

Bool
winDestroyWindowNativeGDI(WindowPtr pWin)
{
    Bool fResult = TRUE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winDestroyWindowNativeGDI (%p)\n", pWin);
#endif

    WIN_UNWRAP(DestroyWindow);
    fResult = (*pScreen->DestroyWindow) (pWin);
    WIN_WRAP(DestroyWindow, winDestroyWindowNativeGDI);

    return fResult;
}

/* See Porting Layer Definition - p. 37 */
/* See mfb/mfbwindow.c - mfbPositionWindow() */

Bool
winPositionWindowNativeGDI(WindowPtr pWin, int x, int y)
{
    Bool fResult = TRUE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winPositionWindowNativeGDI (%p)\n", pWin);
#endif

    WIN_UNWRAP(PositionWindow);
    fResult = (*pScreen->PositionWindow) (pWin, x, y);
    WIN_WRAP(PositionWindow, winPositionWindowNativeGDI);

    return fResult;
}

/* See Porting Layer Definition - p. 39 */
/* See mfb/mfbwindow.c - mfbCopyWindow() */

void
winCopyWindowNativeGDI(WindowPtr pWin, DDXPointRec ptOldOrg, RegionPtr prgnSrc)
{
    DDXPointPtr pptSrc;
    DDXPointPtr ppt;
    RegionPtr prgnDst;
    BoxPtr pBox;
    int dx, dy;
    int i, nbox;
    BoxPtr pBoxDst;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

#if 0
    ErrorF("winCopyWindow\n");
#endif

    /* Create a region for the destination */
    prgnDst = RegionCreate(NULL, 1);

    /* Calculate the shift from the source to the destination */
    dx = ptOldOrg.x - pWin->drawable.x;
    dy = ptOldOrg.y - pWin->drawable.y;

    /* Translate the region from the destination to the source? */
    RegionTranslate(prgnSrc, -dx, -dy);
    RegionIntersect(prgnDst, &pWin->borderClip, prgnSrc);

    /* Get a pointer to the first box in the region to be copied */
    pBox = RegionRects(prgnDst);

    /* Get the number of boxes in the region */
    nbox = RegionNumRects(prgnDst);

    /* Allocate source points for each box */
    if (!(pptSrc = malloc(nbox * sizeof(DDXPointRec))))
        return;

    /* Set an iterator pointer */
    ppt = pptSrc;

    /* Calculate the source point of each box? */
    for (i = nbox; --i >= 0; ppt++, pBox++) {
        ppt->x = pBox->x1 + dx;
        ppt->y = pBox->y1 + dy;
    }

    /* Setup loop pointers again */
    pBoxDst = RegionRects(prgnDst);
    ppt = pptSrc;

#if 0
    ErrorF("winCopyWindow - x1\tx2\ty1\ty2\tx\ty\n");
#endif

    /* BitBlt each source to the destination point */
    for (i = nbox; --i >= 0; pBoxDst++, ppt++) {
#if 0
        ErrorF("winCopyWindow - %d\t%d\t%d\t%d\t%d\t%d\n",
               pBoxDst->x1, pBoxDst->x2, pBoxDst->y1, pBoxDst->y2,
               ppt->x, ppt->y);
#endif

        BitBlt(pScreenPriv->hdcScreen,
               pBoxDst->x1, pBoxDst->y1,
               pBoxDst->x2 - pBoxDst->x1, pBoxDst->y2 - pBoxDst->y1,
               pScreenPriv->hdcScreen, ppt->x, ppt->y, SRCCOPY);
    }

    /* Cleanup the regions, etc. */
    free(pptSrc);
    RegionDestroy(prgnDst);
}

/* See Porting Layer Definition - p. 37 */
/* See mfb/mfbwindow.c - mfbChangeWindowAttributes() */

Bool
winChangeWindowAttributesNativeGDI(WindowPtr pWin, unsigned long mask)
{
    Bool fResult = TRUE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winChangeWindowAttributesNativeGDI (%p)\n", pWin);
#endif

    WIN_UNWRAP(ChangeWindowAttributes);
    fResult = (*pScreen->ChangeWindowAttributes) (pWin, mask);
    WIN_WRAP(ChangeWindowAttributes, winChangeWindowAttributesNativeGDI);

    /*
     * NOTE: We do not currently need to do anything here.
     */

    return fResult;
}

/* See Porting Layer Definition - p. 37
 * Also referred to as UnrealizeWindow
 */

Bool
winUnmapWindowNativeGDI(WindowPtr pWin)
{
    Bool fResult = TRUE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winUnmapWindowNativeGDI (%p)\n", pWin);
#endif

    WIN_UNWRAP(UnrealizeWindow);
    fResult = (*pScreen->UnrealizeWindow) (pWin);
    WIN_WRAP(UnrealizeWindow, winUnmapWindowNativeGDI);

    return fResult;
}

/* See Porting Layer Definition - p. 37
 * Also referred to as RealizeWindow
 */

Bool
winMapWindowNativeGDI(WindowPtr pWin)
{
    Bool fResult = TRUE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winMapWindowNativeGDI (%p)\n", pWin);
#endif

    WIN_UNWRAP(RealizeWindow);
    fResult = (*pScreen->RealizeWindow) (pWin);
    WIN_WRAP(RealizeWindow, winMapWindowMultiWindow);

    return fResult;

}
#endif

/* See Porting Layer Definition - p. 37 */
/* See mfb/mfbwindow.c - mfbCreateWindow() */

Bool
winCreateWindowRootless(WindowPtr pWin)
{
    Bool fResult = FALSE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winWindowPriv(pWin);
    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winCreateWindowRootless (%p)\n", pWin);
#endif

    WIN_UNWRAP(CreateWindow);
    fResult = (*pScreen->CreateWindow) (pWin);
    WIN_WRAP(CreateWindow, winCreateWindowRootless);

    pWinPriv->hRgn = NULL;

    return fResult;
}

/* See Porting Layer Definition - p. 37 */
/* See mfb/mfbwindow.c - mfbDestroyWindow() */

Bool
winDestroyWindowRootless(WindowPtr pWin)
{
    Bool fResult = FALSE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winWindowPriv(pWin);
    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winDestroyWindowRootless (%p)\n", pWin);
#endif

    WIN_UNWRAP(DestroyWindow);
    fResult = (*pScreen->DestroyWindow) (pWin);
    WIN_WRAP(DestroyWindow, winDestroyWindowRootless);

    if (pWinPriv->hRgn != NULL) {
        DeleteObject(pWinPriv->hRgn);
        pWinPriv->hRgn = NULL;
    }

    winUpdateRgnRootless(pWin);

    return fResult;
}

/* See Porting Layer Definition - p. 37 */
/* See mfb/mfbwindow.c - mfbPositionWindow() */

Bool
winPositionWindowRootless(WindowPtr pWin, int x, int y)
{
    Bool fResult = FALSE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winPositionWindowRootless (%p)\n", pWin);
#endif

    WIN_UNWRAP(PositionWindow);
    fResult = (*pScreen->PositionWindow) (pWin, x, y);
    WIN_WRAP(PositionWindow, winPositionWindowRootless);

    winUpdateRgnRootless(pWin);

    return fResult;
}

/* See Porting Layer Definition - p. 37 */
/* See mfb/mfbwindow.c - mfbChangeWindowAttributes() */

Bool
winChangeWindowAttributesRootless(WindowPtr pWin, unsigned long mask)
{
    Bool fResult = FALSE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winChangeWindowAttributesRootless (%p)\n", pWin);
#endif

    WIN_UNWRAP(ChangeWindowAttributes);
    fResult = (*pScreen->ChangeWindowAttributes) (pWin, mask);
    WIN_WRAP(ChangeWindowAttributes, winChangeWindowAttributesRootless);

    winUpdateRgnRootless(pWin);

    return fResult;
}

/* See Porting Layer Definition - p. 37
 * Also referred to as UnrealizeWindow
 */

Bool
winUnmapWindowRootless(WindowPtr pWin)
{
    Bool fResult = FALSE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winWindowPriv(pWin);
    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winUnmapWindowRootless (%p)\n", pWin);
#endif

    WIN_UNWRAP(UnrealizeWindow);
    fResult = (*pScreen->UnrealizeWindow) (pWin);
    WIN_WRAP(UnrealizeWindow, winUnmapWindowRootless);

    if (pWinPriv->hRgn != NULL) {
        DeleteObject(pWinPriv->hRgn);
        pWinPriv->hRgn = NULL;
    }

    winUpdateRgnRootless(pWin);

    return fResult;
}

/* See Porting Layer Definition - p. 37
 * Also referred to as RealizeWindow
 */

Bool
winMapWindowRootless(WindowPtr pWin)
{
    Bool fResult = FALSE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winMapWindowRootless (%p)\n", pWin);
#endif

    WIN_UNWRAP(RealizeWindow);
    fResult = (*pScreen->RealizeWindow) (pWin);
    WIN_WRAP(RealizeWindow, winMapWindowRootless);

    winReshapeRootless(pWin);

    winUpdateRgnRootless(pWin);

    return fResult;
}

void
winSetShapeRootless(WindowPtr pWin, int kind)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

#if CYGDEBUG
    winTrace("winSetShapeRootless (%p, %i)\n", pWin, kind);
#endif

    WIN_UNWRAP(SetShape);
    (*pScreen->SetShape) (pWin, kind);
    WIN_WRAP(SetShape, winSetShapeRootless);

    winReshapeRootless(pWin);
    winUpdateRgnRootless(pWin);

    return;
}

/*
 * Local function for adding a region to the Windows window region
 */

static
    int
winAddRgn(WindowPtr pWin, void *data)
{
    int iX, iY, iWidth, iHeight, iBorder;
    HRGN hRgn = *(HRGN *) data;
    HRGN hRgnWin;

    winWindowPriv(pWin);

    /* If pWin is not Root */
    if (pWin->parent != NULL) {
#if CYGDEBUG
        winDebug("winAddRgn ()\n");
#endif
        if (pWin->mapped) {
            iBorder = wBorderWidth(pWin);

            iX = pWin->drawable.x - iBorder;
            iY = pWin->drawable.y - iBorder;

            iWidth = pWin->drawable.width + iBorder * 2;
            iHeight = pWin->drawable.height + iBorder * 2;

            hRgnWin = CreateRectRgn(0, 0, iWidth, iHeight);

            if (hRgnWin == NULL) {
                ErrorF("winAddRgn - CreateRectRgn () failed\n");
                ErrorF("  Rect %d %d %d %d\n",
                       iX, iY, iX + iWidth, iY + iHeight);
            }

            if (pWinPriv->hRgn) {
                if (CombineRgn(hRgnWin, hRgnWin, pWinPriv->hRgn, RGN_AND)
                    == ERROR) {
                    ErrorF("winAddRgn - CombineRgn () failed\n");
                }
            }

            OffsetRgn(hRgnWin, iX, iY);

            if (CombineRgn(hRgn, hRgn, hRgnWin, RGN_OR) == ERROR) {
                ErrorF("winAddRgn - CombineRgn () failed\n");
            }

            DeleteObject(hRgnWin);
        }
        return WT_DONTWALKCHILDREN;
    }
    else {
        return WT_WALKCHILDREN;
    }
}

/*
 * Local function to update the Windows window's region
 */

static
    void
winUpdateRgnRootless(WindowPtr pWin)
{
    HRGN hRgn = CreateRectRgn(0, 0, 0, 0);

    if (hRgn != NULL) {
        WalkTree(pWin->drawable.pScreen, winAddRgn, &hRgn);
        SetWindowRgn(winGetScreenPriv(pWin->drawable.pScreen)->hwndScreen,
                     hRgn, TRUE);
    }
    else {
        ErrorF("winUpdateRgnRootless - CreateRectRgn failed.\n");
    }
}

static
    void
winReshapeRootless(WindowPtr pWin)
{
    int nRects;
    RegionRec rrNewShape;
    BoxPtr pShape, pRects, pEnd;
    HRGN hRgn, hRgnRect;

    winWindowPriv(pWin);

#if CYGDEBUG
    winDebug("winReshapeRootless ()\n");
#endif

    /* Bail if the window is the root window */
    if (pWin->parent == NULL)
        return;

    /* Bail if the window is not top level */
    if (pWin->parent->parent != NULL)
        return;

    /* Free any existing window region stored in the window privates */
    if (pWinPriv->hRgn != NULL) {
        DeleteObject(pWinPriv->hRgn);
        pWinPriv->hRgn = NULL;
    }

    /* Bail if the window has no bounding region defined */
    if (!wBoundingShape(pWin))
        return;

    RegionNull(&rrNewShape);
    RegionCopy(&rrNewShape, wBoundingShape(pWin));
    RegionTranslate(&rrNewShape, pWin->borderWidth, pWin->borderWidth);

    nRects = RegionNumRects(&rrNewShape);
    pShape = RegionRects(&rrNewShape);

    if (nRects > 0) {
        /* Create initial empty Windows region */
        hRgn = CreateRectRgn(0, 0, 0, 0);

        /* Loop through all rectangles in the X region */
        for (pRects = pShape, pEnd = pShape + nRects; pRects < pEnd; pRects++) {
            /* Create a Windows region for the X rectangle */
            hRgnRect = CreateRectRgn(pRects->x1, pRects->y1,
                                     pRects->x2, pRects->y2);
            if (hRgnRect == NULL) {
                ErrorF("winReshapeRootless - CreateRectRgn() failed\n");
            }

            /* Merge the Windows region with the accumulated region */
            if (CombineRgn(hRgn, hRgn, hRgnRect, RGN_OR) == ERROR) {
                ErrorF("winReshapeRootless - CombineRgn() failed\n");
            }

            /* Delete the temporary Windows region */
            DeleteObject(hRgnRect);
        }

        /* Save a handle to the composite region in the window privates */
        pWinPriv->hRgn = hRgn;
    }

    RegionUninit(&rrNewShape);

    return;
}
