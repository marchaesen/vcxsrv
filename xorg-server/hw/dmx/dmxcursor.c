/*
 * Copyright 2001-2004 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   David H. Dawes <dawes@xfree86.org>
 *   Kevin E. Martin <kem@redhat.com>
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * This file contains code than supports cursor movement, including the
 * code that initializes and reinitializes the screen positions and
 * computes screen overlap.
 *
 * "This code is based very closely on the XFree86 equivalent
 * (xfree86/common/xf86Cursor.c)."  --David Dawes.
 *
 * "This code was then extensively re-written, as explained here."
 * --Rik Faith
 *
 * The code in xf86Cursor.c used edge lists to implement the
 * CursorOffScreen function.  The edge list computation was complex
 * (especially in the face of arbitrarily overlapping screens) compared
 * with the speed savings in the CursorOffScreen function.  The new
 * implementation has erred on the side of correctness, readability, and
 * maintainability over efficiency.  For the common (non-edge) case, the
 * dmxCursorOffScreen function does avoid a loop over all the screens.
 * When the cursor has left the screen, all the screens are searched,
 * and the first screen (in dmxScreens order) containing the cursor will
 * be returned.  If run-time profiling shows that this routing is a
 * performance bottle-neck, then an edge list may have to be
 * reimplemented.  An edge list algorithm is O(edges) whereas the new
 * algorithm is O(dmxNumScreens).  Since edges is usually 1-3 and
 * dmxNumScreens may be 30-60 for large backend walls, this trade off
 * may be compelling.
 *
 * The xf86InitOrigins routine uses bit masks during the computation and
 * is therefore limited to the length of a word (e.g., 32 or 64 bits)
 * screens.  Because Xdmx is expected to be used with a large number of
 * backend displays, this limitation was removed.  The new
 * implementation has erred on the side of readability over efficiency,
 * using the dmxSL* routines to manage a screen list instead of a
 * bitmap, and a function call to decrease the length of the main
 * routine.  Both algorithms are of the same order, and both are called
 * only at server generation time, so trading clarity and long-term
 * maintainability for efficiency does not seem justified in this case.
 */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#define DMX_CURSOR_DEBUG 0

#include "dmx.h"
#include "dmxsync.h"
#include "dmxcursor.h"
#include "dmxlog.h"
#include "dmxprop.h"
#include "dmxinput.h"

#include "mipointer.h"
#include "windowstr.h"
#include "globals.h"
#include "cursorstr.h"
#include "dixevents.h"          /* For GetSpriteCursor() */
#include "inputstr.h"           /* for inputInfo.pointer */

#if DMX_CURSOR_DEBUG
#define DMXDBG0(f)               dmxLog(dmxDebug,f)
#define DMXDBG1(f,a)             dmxLog(dmxDebug,f,a)
#define DMXDBG2(f,a,b)           dmxLog(dmxDebug,f,a,b)
#define DMXDBG3(f,a,b,c)         dmxLog(dmxDebug,f,a,b,c)
#define DMXDBG4(f,a,b,c,d)       dmxLog(dmxDebug,f,a,b,c,d)
#define DMXDBG5(f,a,b,c,d,e)     dmxLog(dmxDebug,f,a,b,c,d,e)
#define DMXDBG6(f,a,b,c,d,e,g)   dmxLog(dmxDebug,f,a,b,c,d,e,g)
#define DMXDBG7(f,a,b,c,d,e,g,h) dmxLog(dmxDebug,f,a,b,c,d,e,g,h)
#else
#define DMXDBG0(f)
#define DMXDBG1(f,a)
#define DMXDBG2(f,a,b)
#define DMXDBG3(f,a,b,c)
#define DMXDBG4(f,a,b,c,d)
#define DMXDBG5(f,a,b,c,d,e)
#define DMXDBG6(f,a,b,c,d,e,g)
#define DMXDBG7(f,a,b,c,d,e,g,h)
#endif

static int dmxCursorDoMultiCursors = 1;

/** Turn off support for displaying multiple cursors on overlapped
    back-end displays.  See #dmxCursorDoMultiCursors. */
void
dmxCursorNoMulti(void)
{
    dmxCursorDoMultiCursors = 0;
}

static Bool
dmxCursorOffScreen(ScreenPtr *ppScreen, int *x, int *y)
{
    DMXScreenInfo *dmxScreen;
    int i;
    int localX = *x;
    int localY = *y;
    int globalX;
    int globalY;

    if (screenInfo.numScreens == 1)
        return FALSE;

    /* On current screen? */
    dmxScreen = &dmxScreens[(*ppScreen)->myNum];
    if (localX >= 0
        && localX < dmxScreen->rootWidth
        && localY >= 0 && localY < dmxScreen->rootHeight)
        return FALSE;

    /* Convert to global coordinate space */
    globalX = dmxScreen->rootXOrigin + localX;
    globalY = dmxScreen->rootYOrigin + localY;

    /* Is cursor on the current screen?
     * This efficiently exits this routine
     * for the most common case. */
    if (ppScreen && *ppScreen) {
        dmxScreen = &dmxScreens[(*ppScreen)->myNum];
        if (globalX >= dmxScreen->rootXOrigin
            && globalX < dmxScreen->rootXOrigin + dmxScreen->rootWidth
            && globalY >= dmxScreen->rootYOrigin
            && globalY < dmxScreen->rootYOrigin + dmxScreen->rootHeight)
            return FALSE;
    }

    /* Find first screen cursor is on */
    for (i = 0; i < dmxNumScreens; i++) {
        dmxScreen = &dmxScreens[i];
        if (globalX >= dmxScreen->rootXOrigin
            && globalX < dmxScreen->rootXOrigin + dmxScreen->rootWidth
            && globalY >= dmxScreen->rootYOrigin
            && globalY < dmxScreen->rootYOrigin + dmxScreen->rootHeight) {
            if (dmxScreen->index == (*ppScreen)->myNum)
                return FALSE;
            *ppScreen = screenInfo.screens[dmxScreen->index];
            *x = globalX - dmxScreen->rootXOrigin;
            *y = globalY - dmxScreen->rootYOrigin;
            return TRUE;
        }
    }
    return FALSE;
}

static void
dmxCrossScreen(ScreenPtr pScreen, Bool entering)
{
}

static void
dmxWarpCursor(DeviceIntPtr pDev, ScreenPtr pScreen, int x, int y)
{
    DMXDBG3("dmxWarpCursor(%d,%d,%d)\n", pScreen->myNum, x, y);
#if 11 /*BP*/
        /* This call is depracated.  Replace with???? */
        miPointerWarpCursor(pDev, pScreen, x, y);
#else
    pScreen->SetCursorPosition(pDev, pScreen, x, y, FALSE);
#endif
}

miPointerScreenFuncRec dmxPointerCursorFuncs = {
    dmxCursorOffScreen,
    dmxCrossScreen,
    dmxWarpCursor,
};

/** Create a list of screens that we'll manipulate. */
static int *
dmxSLCreate(void)
{
    int *list = xallocarray(dmxNumScreens, sizeof(*list));
    int i;

    for (i = 0; i < dmxNumScreens; i++)
        list[i] = 1;
    return list;
}

/** Free list. */
static void
dmxSLFree(int *list)
{
    free(list);
}

/** Find next uninitialized entry in list. */
static int
dmxSLFindNext(int *list)
{
    int i;

    for (i = 0; i < dmxNumScreens; i++)
        if (list[i])
            return i;
    return -1;
}

/** Make one pass over all the screens and return the number updated. */
static int
dmxTryComputeScreenOrigins(int *screensLeft)
{
    ScreenPtr pScreen, refScreen;
    DMXScreenInfo *screen;
    int i, ref;
    int changed = 0;

    for (i = 0; i < dmxNumScreens; i++) {
        if (!screensLeft[i])
            continue;
        screen = &dmxScreens[i];
        pScreen = screenInfo.screens[i];
        switch (screen->where) {
        case PosAbsolute:
            pScreen->x = screen->whereX;
            pScreen->y = screen->whereY;
            ++changed, screensLeft[i] = 0;
            break;
        case PosRelative:
            ref = screen->whereRefScreen;
            if (screensLeft[ref])
                break;
            refScreen = screenInfo.screens[ref];
            pScreen->x = refScreen->x + screen->whereX;
            pScreen->y = refScreen->y + screen->whereY;
            ++changed, screensLeft[i] = 0;
            break;
        case PosRightOf:
            ref = screen->whereRefScreen;
            if (screensLeft[ref])
                break;
            refScreen = screenInfo.screens[ref];
            pScreen->x = refScreen->x + refScreen->width;
            pScreen->y = refScreen->y;
            ++changed, screensLeft[i] = 0;
            break;
        case PosLeftOf:
            ref = screen->whereRefScreen;
            if (screensLeft[ref])
                break;
            refScreen = screenInfo.screens[ref];
            pScreen->x = refScreen->x - pScreen->width;
            pScreen->y = refScreen->y;
            ++changed, screensLeft[i] = 0;
            break;
        case PosBelow:
            ref = screen->whereRefScreen;
            if (screensLeft[ref])
                break;
            refScreen = screenInfo.screens[ref];
            pScreen->x = refScreen->x;
            pScreen->y = refScreen->y + refScreen->height;
            ++changed, screensLeft[i] = 0;
            break;
        case PosAbove:
            ref = screen->whereRefScreen;
            if (screensLeft[ref])
                break;
            refScreen = screenInfo.screens[ref];
            pScreen->x = refScreen->x;
            pScreen->y = refScreen->y - pScreen->height;
            ++changed, screensLeft[i] = 0;
            break;
        case PosNone:
            dmxLog(dmxFatal, "No position information for screen %d\n", i);
        }
    }
    return changed;
}

static void
dmxComputeScreenOrigins(void)
{
    ScreenPtr pScreen;
    int *screensLeft;
    int i, ref;
    int minX, minY;

    /* Compute origins based on
     * configuration information. */
    screensLeft = dmxSLCreate();
    while ((i = dmxSLFindNext(screensLeft)) >= 0) {
        while (dmxTryComputeScreenOrigins(screensLeft));
        if ((i = dmxSLFindNext(screensLeft)) >= 0) {
            /* All of the remaining screens are referencing each other.
             * Assign a value to one of them and go through again.  This
             * guarantees that we will eventually terminate.
             */
            ref = dmxScreens[i].whereRefScreen;
            pScreen = screenInfo.screens[ref];
            pScreen->x = pScreen->y = 0;
            screensLeft[ref] = 0;
        }
    }
    dmxSLFree(screensLeft);

    /* Justify the topmost and leftmost to
     * (0,0). */
    minX = screenInfo.screens[0]->x;
    minY = screenInfo.screens[0]->y;
    for (i = 1; i < dmxNumScreens; i++) {       /* Compute minX, minY */
        if (screenInfo.screens[i]->x < minX)
            minX = screenInfo.screens[i]->x;
        if (screenInfo.screens[i]->y < minY)
            minY = screenInfo.screens[i]->y;
    }
    if (minX || minY) {
        for (i = 0; i < dmxNumScreens; i++) {
            screenInfo.screens[i]->x -= minX;
            screenInfo.screens[i]->y -= minY;
        }
    }

    update_desktop_dimensions();
}

/** Recompute origin information in the #dmxScreens list.  This is
 * called from #dmxInitOrigins. */
void
dmxReInitOrigins(void)
{
    int i;

    if (dmxNumScreens > MAXSCREENS)
        dmxLog(dmxFatal, "dmxNumScreens = %d > MAXSCREENS = %d\n",
               dmxNumScreens, MAXSCREENS);

    for (i = 0; i < dmxNumScreens; i++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[i];

        dmxLogOutput(dmxScreen,
                     "s=%dx%d%+d%+d r=%dx%d%+d%+d @%d,%d"
                     " (be=%dx%d depth=%d bpp=%d)\n",
                     dmxScreen->scrnWidth, dmxScreen->scrnHeight,
                     dmxScreen->scrnX, dmxScreen->scrnY,
                     dmxScreen->rootWidth, dmxScreen->rootHeight,
                     dmxScreen->rootX, dmxScreen->rootY,
                     dmxScreen->rootXOrigin, dmxScreen->rootYOrigin,
                     dmxScreen->beWidth, dmxScreen->beHeight,
                     dmxScreen->beDepth, dmxScreen->beBPP);
    }
}

/** Initialize screen origins (and relative position).  This is called
 * for each server generation.  For dynamic reconfiguration, use
 * #dmxReInitOrigins() instead. */
void
dmxInitOrigins(void)
{
    int i;

    if (dmxNumScreens > MAXSCREENS)
        dmxLog(dmxFatal, "dmxNumScreens = %d > MAXSCREENS = %d\n",
               dmxNumScreens, MAXSCREENS);

    for (i = 0; i < dmxNumScreens; i++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[i];

        dmxLogOutput(dmxScreen,
                     "(request) s=%dx%d%+d%+d r=%dx%d%+d%+d @%d,%d (%d)"
                     " (be=%dx%d depth=%d bpp=%d)\n",
                     dmxScreen->scrnWidth, dmxScreen->scrnHeight,
                     dmxScreen->scrnX, dmxScreen->scrnY,
                     dmxScreen->rootWidth, dmxScreen->rootHeight,
                     dmxScreen->rootX, dmxScreen->rootY,
                     dmxScreen->whereX, dmxScreen->whereY,
                     dmxScreen->where,
                     dmxScreen->beWidth, dmxScreen->beHeight,
                     dmxScreen->beDepth, dmxScreen->beBPP);
    }

    dmxComputeScreenOrigins();

    for (i = 0; i < dmxNumScreens; i++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[i];

        dmxScreen->rootXOrigin = screenInfo.screens[i]->x;
        dmxScreen->rootYOrigin = screenInfo.screens[i]->y;
    }

    dmxReInitOrigins();
}

/** Returns non-zero if the global \a x, \a y coordinate is on the
 * screen window of the \a dmxScreen. */
int
dmxOnScreen(int x, int y, DMXScreenInfo * dmxScreen)
{
#if DMX_CURSOR_DEBUG > 1
    dmxLog(dmxDebug,
           "dmxOnScreen %d %d,%d (r=%dx%d%+d%+d@%d,%d s=%dx%d%+d%+d)\n",
           dmxScreen->index, x, y,
           dmxScreen->rootWidth, dmxScreen->rootHeight,
           dmxScreen->rootX, dmxScreen->rootY,
           dmxScreen->rootXOrigin, dmxScreen->rootYOrigin,
           dmxScreen->scrnWidth, dmxScreen->scrnHeight,
           dmxScreen->scrnX, dmxScreen->scrnY);
#endif
    if (x >= dmxScreen->rootXOrigin
        && x < dmxScreen->rootXOrigin + dmxScreen->rootWidth
        && y >= dmxScreen->rootYOrigin
        && y < dmxScreen->rootYOrigin + dmxScreen->rootHeight)
        return 1;
    return 0;
}

/** Returns non-zero if \a a overlaps \a b. */
static int
dmxDoesOverlap(DMXScreenInfo * a, DMXScreenInfo * b)
{
    if (dmxOnScreen(a->rootXOrigin, a->rootYOrigin, b))
        return 1;

    if (dmxOnScreen(a->rootXOrigin, a->rootYOrigin + a->scrnWidth, b))
        return 1;

    if (dmxOnScreen(a->rootXOrigin + a->scrnHeight, a->rootYOrigin, b))
        return 1;

    if (dmxOnScreen(a->rootXOrigin + a->scrnHeight,
                    a->rootYOrigin + a->scrnWidth, b))
        return 1;

    if (dmxOnScreen(b->rootXOrigin, b->rootYOrigin, a))
        return 1;

    if (dmxOnScreen(b->rootXOrigin, b->rootYOrigin + b->scrnWidth, a))
        return 1;

    if (dmxOnScreen(b->rootXOrigin + b->scrnHeight, b->rootYOrigin, a))
        return 1;

    if (dmxOnScreen(b->rootXOrigin + b->scrnHeight,
                    b->rootYOrigin + b->scrnWidth, a))
        return 1;

    return 0;
}

/** Used with \a dmxInterateOverlap to print out a list of screens which
 * overlap each other. */
static void *
dmxPrintOverlap(DMXScreenInfo * dmxScreen, void *closure)
{
    DMXScreenInfo *a = closure;

    if (dmxScreen != a) {
        if (dmxScreen->cursorNotShared)
            dmxLogOutputCont(a, " [%d/%s]", dmxScreen->index, dmxScreen->name);
        else
            dmxLogOutputCont(a, " %d/%s", dmxScreen->index, dmxScreen->name);
    }
    return NULL;
}

/** Iterate over the screens which overlap with the \a start screen,
 * calling \a f with the \a closure for each argument.  Often used with
 * #dmxPrintOverlap. */
static void *
dmxIterateOverlap(DMXScreenInfo * start,
                  void *(*f) (DMXScreenInfo * dmxScreen, void *), void *closure)
{
    DMXScreenInfo *pt;

    if (!start->over)
        return f(start, closure);

    for (pt = start->over; /* condition at end of loop */ ; pt = pt->over) {
        void *retval;

        if ((retval = f(pt, closure)))
            return retval;
        if (pt == start)
            break;
    }
    return NULL;
}

/** Used with #dmxPropertyIterate to determine if screen \a a is the
 * same as the screen \a closure. */
static void *
dmxTestSameDisplay(DMXScreenInfo * a, void *closure)
{
    DMXScreenInfo *b = closure;

    if (a == b)
        return a;
    return NULL;
}

/** Detects overlapping dmxScreens and creates circular lists.  This
 * uses an O(dmxNumScreens^2) algorithm, but dmxNumScreens is < 100 and
 * the computation only needs to be performed for every server
 * generation or dynamic reconfiguration . */
void
dmxInitOverlap(void)
{
    int i, j;
    DMXScreenInfo *a, *b, *pt;

    for (i = 0; i < dmxNumScreens; i++)
        dmxScreens[i].over = NULL;

    for (i = 0; i < dmxNumScreens; i++) {
        a = &dmxScreens[i];

        for (j = i + 1; j < dmxNumScreens; j++) {
            b = &dmxScreens[j];
            if (b->over)
                continue;

            if (dmxDoesOverlap(a, b)) {
                DMXDBG6("%d overlaps %d: a=%p %p b=%p %p\n",
                        a->index, b->index, a, a->over, b, b->over);
                b->over = (a->over ? a->over : a);
                a->over = b;
            }
        }
    }

    for (i = 0; i < dmxNumScreens; i++) {
        a = &dmxScreens[i];

        if (!a->over)
            continue;

        /* Flag all pairs that are on same display */
        for (pt = a->over; pt != a; pt = pt->over) {
            if (dmxPropertyIterate(a, dmxTestSameDisplay, pt)) {
                /* The ->over sets contain the transitive set of screens
                 * that overlap.  For screens that are on the same
                 * backend display, we only want to exclude pairs of
                 * screens that mutually overlap on the backend display,
                 * so we call dmxDoesOverlap, which is stricter than the
                 * ->over set. */
                if (!dmxDoesOverlap(a, pt))
                    continue;
                a->cursorNotShared = 1;
                pt->cursorNotShared = 1;
                dmxLog(dmxInfo,
                       "Screen %d and %d overlap on %s\n",
                       a->index, pt->index, a->name);
            }
        }
    }

    for (i = 0; i < dmxNumScreens; i++) {
        a = &dmxScreens[i];

        if (a->over) {
            dmxLogOutput(a, "Overlaps");
            dmxIterateOverlap(a, dmxPrintOverlap, a);
            dmxLogOutputCont(a, "\n");
        }
    }
}

/** Create \a pCursor on the back-end associated with \a pScreen. */
void
dmxBECreateCursor(ScreenPtr pScreen, CursorPtr pCursor)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxCursorPrivPtr pCursorPriv = DMX_GET_CURSOR_PRIV(pCursor, pScreen);
    CursorBitsPtr pBits = pCursor->bits;
    Pixmap src, msk;
    XColor fg, bg;
    XImage *img;
    XlibGC gc = NULL;
    XGCValues v;
    unsigned long m;
    int i;

    if (!pCursorPriv)
        return;

    m = GCFunction | GCPlaneMask | GCForeground | GCBackground | GCClipMask;
    v.function = GXcopy;
    v.plane_mask = AllPlanes;
    v.foreground = 1L;
    v.background = 0L;
    v.clip_mask = None;

    for (i = 0; i < dmxScreen->beNumPixmapFormats; i++) {
        if (dmxScreen->bePixmapFormats[i].depth == 1) {
            /* Create GC in the back-end servers */
            gc = XCreateGC(dmxScreen->beDisplay, dmxScreen->scrnDefDrawables[i],
                           m, &v);
            break;
        }
    }
    if (!gc)
        dmxLog(dmxFatal, "dmxRealizeCursor: gc not initialized\n");

    src = XCreatePixmap(dmxScreen->beDisplay, dmxScreen->scrnWin,
                        pBits->width, pBits->height, 1);
    msk = XCreatePixmap(dmxScreen->beDisplay, dmxScreen->scrnWin,
                        pBits->width, pBits->height, 1);

    img = XCreateImage(dmxScreen->beDisplay,
                       dmxScreen->beVisuals[dmxScreen->beDefVisualIndex].visual,
                       1, XYBitmap, 0, (char *) pBits->source,
                       pBits->width, pBits->height,
                       BitmapPad(dmxScreen->beDisplay), 0);

    XPutImage(dmxScreen->beDisplay, src, gc, img, 0, 0, 0, 0,
              pBits->width, pBits->height);

    XFree(img);

    img = XCreateImage(dmxScreen->beDisplay,
                       dmxScreen->beVisuals[dmxScreen->beDefVisualIndex].visual,
                       1, XYBitmap, 0, (char *) pBits->mask,
                       pBits->width, pBits->height,
                       BitmapPad(dmxScreen->beDisplay), 0);

    XPutImage(dmxScreen->beDisplay, msk, gc, img, 0, 0, 0, 0,
              pBits->width, pBits->height);

    XFree(img);

    fg.red = pCursor->foreRed;
    fg.green = pCursor->foreGreen;
    fg.blue = pCursor->foreBlue;

    bg.red = pCursor->backRed;
    bg.green = pCursor->backGreen;
    bg.blue = pCursor->backBlue;

    pCursorPriv->cursor = XCreatePixmapCursor(dmxScreen->beDisplay,
                                              src, msk,
                                              &fg, &bg,
                                              pBits->xhot, pBits->yhot);

    XFreePixmap(dmxScreen->beDisplay, src);
    XFreePixmap(dmxScreen->beDisplay, msk);
    XFreeGC(dmxScreen->beDisplay, gc);

    dmxSync(dmxScreen, FALSE);
}

static Bool
_dmxRealizeCursor(ScreenPtr pScreen, CursorPtr pCursor)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxCursorPrivPtr pCursorPriv;

    DMXDBG2("_dmxRealizeCursor(%d,%p)\n", pScreen->myNum, pCursor);

    DMX_SET_CURSOR_PRIV(pCursor, pScreen, malloc(sizeof(*pCursorPriv)));
    if (!DMX_GET_CURSOR_PRIV(pCursor, pScreen))
        return FALSE;

    pCursorPriv = DMX_GET_CURSOR_PRIV(pCursor, pScreen);
    pCursorPriv->cursor = (Cursor) 0;

    if (!dmxScreen->beDisplay)
        return TRUE;

    dmxBECreateCursor(pScreen, pCursor);
    return TRUE;
}

/** Free \a pCursor on the back-end associated with \a pScreen. */
Bool
dmxBEFreeCursor(ScreenPtr pScreen, CursorPtr pCursor)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxCursorPrivPtr pCursorPriv = DMX_GET_CURSOR_PRIV(pCursor, pScreen);

    if (pCursorPriv) {
        XFreeCursor(dmxScreen->beDisplay, pCursorPriv->cursor);
        pCursorPriv->cursor = (Cursor) 0;
        return TRUE;
    }

    return FALSE;
}

static Bool
_dmxUnrealizeCursor(ScreenPtr pScreen, CursorPtr pCursor)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];

    DMXDBG2("_dmxUnrealizeCursor(%d,%p)\n", pScreen->myNum, pCursor);

    if (dmxScreen->beDisplay) {
        if (dmxBEFreeCursor(pScreen, pCursor))
            free(DMX_GET_CURSOR_PRIV(pCursor, pScreen));
    }
    DMX_SET_CURSOR_PRIV(pCursor, pScreen, NULL);

    return TRUE;
}

static void
_dmxMoveCursor(ScreenPtr pScreen, int x, int y)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    int newX = x + dmxScreen->rootX;
    int newY = y + dmxScreen->rootY;

    if (newX < 0)
        newX = 0;
    if (newY < 0)
        newY = 0;

    DMXDBG5("_dmxMoveCursor(%d,%d,%d) -> %d,%d\n",
            pScreen->myNum, x, y, newX, newY);
    if (dmxScreen->beDisplay) {
        XWarpPointer(dmxScreen->beDisplay, None, dmxScreen->scrnWin,
                     0, 0, 0, 0, newX, newY);
        dmxSync(dmxScreen, TRUE);
    }
}

static void
_dmxSetCursor(ScreenPtr pScreen, CursorPtr pCursor, int x, int y)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];

    DMXDBG4("_dmxSetCursor(%d,%p,%d,%d)\n", pScreen->myNum, pCursor, x, y);

    if (pCursor) {
        dmxCursorPrivPtr pCursorPriv = DMX_GET_CURSOR_PRIV(pCursor, pScreen);

        if (pCursorPriv && dmxScreen->curCursor != pCursorPriv->cursor) {
            if (dmxScreen->beDisplay)
                XDefineCursor(dmxScreen->beDisplay, dmxScreen->scrnWin,
                              pCursorPriv->cursor);
            dmxScreen->cursor = pCursor;
            dmxScreen->curCursor = pCursorPriv->cursor;
            dmxScreen->cursorVisible = 1;
        }
        _dmxMoveCursor(pScreen, x, y);
    }
    else {
        if (dmxScreen->beDisplay)
            XDefineCursor(dmxScreen->beDisplay, dmxScreen->scrnWin,
                          dmxScreen->noCursor);
        dmxScreen->cursor = NULL;
        dmxScreen->curCursor = (Cursor) 0;
        dmxScreen->cursorVisible = 0;
    }
    if (dmxScreen->beDisplay)
        dmxSync(dmxScreen, TRUE);
}

static Bool
dmxRealizeCursor(DeviceIntPtr pDev, ScreenPtr pScreen, CursorPtr pCursor)
{
    DMXScreenInfo *start = &dmxScreens[pScreen->myNum];
    DMXScreenInfo *pt;

    if (!start->over || !dmxCursorDoMultiCursors || start->cursorNotShared)
        return _dmxRealizeCursor(pScreen, pCursor);

    for (pt = start->over; /* condition at end of loop */ ; pt = pt->over) {
        if (pt->cursorNotShared)
            continue;
        _dmxRealizeCursor(screenInfo.screens[pt->index], pCursor);
        if (pt == start)
            break;
    }
    return TRUE;
}

static Bool
dmxUnrealizeCursor(DeviceIntPtr pDev, ScreenPtr pScreen, CursorPtr pCursor)
{
    DMXScreenInfo *start = &dmxScreens[pScreen->myNum];
    DMXScreenInfo *pt;

    if (!start->over || !dmxCursorDoMultiCursors || start->cursorNotShared)
        return _dmxUnrealizeCursor(pScreen, pCursor);

    for (pt = start->over; /* condition at end of loop */ ; pt = pt->over) {
        if (pt->cursorNotShared)
            continue;
        _dmxUnrealizeCursor(screenInfo.screens[pt->index], pCursor);
        if (pt == start)
            break;
    }
    return TRUE;
}

static CursorPtr
dmxFindCursor(DMXScreenInfo * start)
{
    DMXScreenInfo *pt;

    if (!start || !start->over)
        return GetSpriteCursor(inputInfo.pointer);
    for (pt = start->over; /* condition at end of loop */ ; pt = pt->over) {
        if (pt->cursor)
            return pt->cursor;
        if (pt == start)
            break;
    }
    return GetSpriteCursor(inputInfo.pointer);
}

/** Move the cursor to coordinates (\a x, \a y)on \a pScreen.  This
 * function is usually called via #dmxPointerSpriteFuncs, except during
 * reconfiguration when the cursor is repositioned to force an update on
 * newley overlapping screens and on screens that no longer overlap.
 *
 * The coords (x,y) are in global coord space.  We'll loop over the
 * back-end screens and see if they contain the global coord.  If so, call
 * _dmxMoveCursor() (XWarpPointer) to position the pointer on that screen.
 */
void
dmxMoveCursor(DeviceIntPtr pDev, ScreenPtr pScreen, int x, int y)
{
    DMXScreenInfo *start = &dmxScreens[pScreen->myNum];
    DMXScreenInfo *pt;

    DMXDBG3("dmxMoveCursor(%d,%d,%d)\n", pScreen->myNum, x, y);

    if (!start->over || !dmxCursorDoMultiCursors || start->cursorNotShared) {
        _dmxMoveCursor(pScreen, x, y);
        return;
    }

    for (pt = start->over; /* condition at end of loop */ ; pt = pt->over) {
        if (pt->cursorNotShared)
            continue;
        if (dmxOnScreen(x + start->rootXOrigin, y + start->rootYOrigin, pt)) {
            if ( /* pt != start && */ !pt->cursorVisible) {
                if (!pt->cursor) {
                    /* This only happens during
                     * reconfiguration when a new overlap
                     * occurs. */
                    CursorPtr pCursor;

                    if ((pCursor = dmxFindCursor(start)))
                        _dmxRealizeCursor(screenInfo.screens[pt->index],
                                          pt->cursor = pCursor);

                }
                _dmxSetCursor(screenInfo.screens[pt->index],
                              pt->cursor,
                              x + start->rootXOrigin - pt->rootXOrigin,
                              y + start->rootYOrigin - pt->rootYOrigin);
            }
            _dmxMoveCursor(screenInfo.screens[pt->index],
                           x + start->rootXOrigin - pt->rootXOrigin,
                           y + start->rootYOrigin - pt->rootYOrigin);
        }
        else if ( /* pt != start && */ pt->cursorVisible) {
            _dmxSetCursor(screenInfo.screens[pt->index],
                          NULL,
                          x + start->rootXOrigin - pt->rootXOrigin,
                          y + start->rootYOrigin - pt->rootYOrigin);
        }
        if (pt == start)
            break;
    }
}

static void
dmxSetCursor(DeviceIntPtr pDev, ScreenPtr pScreen, CursorPtr pCursor, int x,
             int y)
{
    DMXScreenInfo *start = &dmxScreens[pScreen->myNum];
    DMXScreenInfo *pt;
    int GX, GY, gx, gy;

    DMXDBG5("dmxSetCursor(%d %p, %p,%d,%d)\n",
            pScreen->myNum, start, pCursor, x, y);

    /* We do this check here because of two cases:
     *
     * 1) if a client calls XWarpPointer()
     * and Xinerama is not running, we can
     * have mi's notion of the pointer
     * position out of phase with DMX's
     * notion.
     *
     * 2) if a down button is held while the
     * cursor moves outside the root window,
     * mi's notion of the pointer position
     * is out of phase with DMX's notion and
     * the cursor can remain visible when it
     * shouldn't be. */

    dmxGetGlobalPosition(&GX, &GY);
    gx = start->rootXOrigin + x;
    gy = start->rootYOrigin + y;
    if (x && y && (GX != gx || GY != gy))
        dmxCoreMotion(NULL, gx, gy, 0, DMX_NO_BLOCK);

    if (!start->over || !dmxCursorDoMultiCursors || start->cursorNotShared) {
        _dmxSetCursor(pScreen, pCursor, x, y);
        return;
    }

    for (pt = start->over; /* condition at end of loop */ ; pt = pt->over) {
        if (pt->cursorNotShared)
            continue;
        if (dmxOnScreen(x + start->rootXOrigin, y + start->rootYOrigin, pt)) {
            _dmxSetCursor(screenInfo.screens[pt->index], pCursor,
                          x + start->rootXOrigin - pt->rootXOrigin,
                          y + start->rootYOrigin - pt->rootYOrigin);
        }
        else {
            _dmxSetCursor(screenInfo.screens[pt->index], NULL,
                          x + start->rootXOrigin - pt->rootXOrigin,
                          y + start->rootYOrigin - pt->rootYOrigin);
        }
        if (pt == start)
            break;
    }
}

/** This routine is used by the backend input routines to hide the
 * cursor on a screen that is being used for relative input.  \see
 * dmxbackend.c */
void
dmxHideCursor(DMXScreenInfo * dmxScreen)
{
    int x, y;
    ScreenPtr pScreen = screenInfo.screens[dmxScreen->index];

    dmxGetGlobalPosition(&x, &y);
    _dmxSetCursor(pScreen, NULL, x, y);
}

/** This routine is called during reconfiguration to make sure the
 * cursor is visible. */
void
dmxCheckCursor(void)
{
    int i;
    int x, y;
    ScreenPtr pScreen;
    DMXScreenInfo *firstScreen;

    dmxGetGlobalPosition(&x, &y);
    firstScreen = dmxFindFirstScreen(x, y);

    DMXDBG2("dmxCheckCursor %d %d\n", x, y);
    for (i = 0; i < dmxNumScreens; i++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[i];

        pScreen = screenInfo.screens[dmxScreen->index];

        if (!dmxOnScreen(x, y, dmxScreen)) {
            if (firstScreen &&
                i == miPointerGetScreen(inputInfo.pointer)->myNum)
                 miPointerSetScreen(inputInfo.pointer, firstScreen->index, x,
                                    y);
            _dmxSetCursor(pScreen, NULL, x - dmxScreen->rootXOrigin,
                          y - dmxScreen->rootYOrigin);
        }
        else {
            if (!dmxScreen->cursor) {
                CursorPtr pCursor;

                if ((pCursor = dmxFindCursor(dmxScreen))) {
                    _dmxRealizeCursor(pScreen, dmxScreen->cursor = pCursor);
                }
            }
            _dmxSetCursor(pScreen, dmxScreen->cursor,
                          x - dmxScreen->rootXOrigin,
                          y - dmxScreen->rootYOrigin);
        }
    }
    DMXDBG2("   leave dmxCheckCursor %d %d\n", x, y);
}

static Bool
dmxDeviceCursorInitialize(DeviceIntPtr pDev, ScreenPtr pScr)
{
    return TRUE;
}

static void
dmxDeviceCursorCleanup(DeviceIntPtr pDev, ScreenPtr pScr)
{
}

miPointerSpriteFuncRec dmxPointerSpriteFuncs = {
    dmxRealizeCursor,
    dmxUnrealizeCursor,
    dmxSetCursor,
    dmxMoveCursor,
    dmxDeviceCursorInitialize,
    dmxDeviceCursorCleanup
};
