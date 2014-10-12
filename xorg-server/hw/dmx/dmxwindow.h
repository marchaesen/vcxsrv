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
 *   Kevin E. Martin <kem@redhat.com>
 *
 */

/** \file
 * Interface for window support.  \see dmxwindow.c */

#ifndef DMXWINDOW_H
#define DMXWINDOW_H

#include "windowstr.h"

/** Window private area. */
typedef struct _dmxWinPriv {
    Window window;
    Bool offscreen;
    Bool mapped;
    Bool restacked;
    unsigned long attribMask;
    Colormap cmap;
    Visual *visual;
    Bool isShaped;
    Bool hasPict;
#ifdef GLXEXT
    void *swapGroup;
    int barrier;
    void (*windowDestroyed) (WindowPtr);
    void (*windowUnmapped) (WindowPtr);
#endif
} dmxWinPrivRec, *dmxWinPrivPtr;

extern Bool dmxInitWindow(ScreenPtr pScreen);

extern Window dmxCreateRootWindow(WindowPtr pWindow);

extern void dmxGetDefaultWindowAttributes(WindowPtr pWindow,
                                          Colormap * cmap, Visual ** visual);
extern void dmxCreateAndRealizeWindow(WindowPtr pWindow, Bool doSync);

extern Bool dmxCreateWindow(WindowPtr pWindow);
extern Bool dmxDestroyWindow(WindowPtr pWindow);
extern Bool dmxPositionWindow(WindowPtr pWindow, int x, int y);
extern Bool dmxChangeWindowAttributes(WindowPtr pWindow, unsigned long mask);
extern Bool dmxRealizeWindow(WindowPtr pWindow);
extern Bool dmxUnrealizeWindow(WindowPtr pWindow);
extern void dmxRestackWindow(WindowPtr pWindow, WindowPtr pOldNextSib);
extern void dmxWindowExposures(WindowPtr pWindow, RegionPtr prgn);
extern void dmxCopyWindow(WindowPtr pWindow, DDXPointRec ptOldOrg,
                          RegionPtr prgnSrc);

extern void dmxResizeWindow(WindowPtr pWindow, int x, int y,
                            unsigned int w, unsigned int h, WindowPtr pSib);
extern void dmxReparentWindow(WindowPtr pWindow, WindowPtr pPriorParent);

extern void dmxChangeBorderWidth(WindowPtr pWindow, unsigned int width);

extern void dmxResizeScreenWindow(ScreenPtr pScreen,
                                  int x, int y, int w, int h);
extern void dmxResizeRootWindow(WindowPtr pRoot, int x, int y, int w, int h);

extern Bool dmxBEDestroyWindow(WindowPtr pWindow);

/* Support for shape extension */
extern void dmxSetShape(WindowPtr pWindow, int kind);

/** Get window private pointer. */
#define DMX_GET_WINDOW_PRIV(_pWin) ((dmxWinPrivPtr) \
    dixLookupPrivate(&(_pWin)->devPrivates, dmxWinPrivateKey))

/* All of these macros are only used in dmxwindow.c */
#define DMX_WINDOW_FUNC_PROLOGUE(_pGC)					\
do {									\
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(_pGC);			\
    DMX_UNWRAP(funcs, pGCPriv, (_pGC));					\
    if (pGCPriv->ops)							\
	DMX_UNWRAP(ops, pGCPriv, (_pGC));				\
} while (0)

#define DMX_WINDOW_FUNC_EPILOGUE(_pGC)					\
do {									\
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(_pGC);			\
    DMX_WRAP(funcs, &dmxGCFuncs, pGCPriv, (_pGC));			\
    if (pGCPriv->ops)							\
	DMX_WRAP(ops, &dmxGCOps, pGCPriv, (_pGC));			\
} while (0)

#define DMX_WINDOW_X1(_pWin)						\
    ((_pWin)->drawable.x - wBorderWidth(_pWin))
#define DMX_WINDOW_Y1(_pWin)						\
    ((_pWin)->drawable.y - wBorderWidth(_pWin))
#define DMX_WINDOW_X2(_pWin)						\
    ((_pWin)->drawable.x + wBorderWidth(_pWin) + (_pWin)->drawable.width)
#define DMX_WINDOW_Y2(_pWin)						\
    ((_pWin)->drawable.y + wBorderWidth(_pWin) + (_pWin)->drawable.height)

#define DMX_WINDOW_OFFSCREEN(_pWin)					\
    (DMX_WINDOW_X1(_pWin) >= (_pWin)->drawable.pScreen->width  ||	\
     DMX_WINDOW_Y1(_pWin) >= (_pWin)->drawable.pScreen->height ||	\
     DMX_WINDOW_X2(_pWin) <= 0                                 ||	\
     DMX_WINDOW_Y2(_pWin) <= 0)

#endif                          /* DMXWINDOW_H */
