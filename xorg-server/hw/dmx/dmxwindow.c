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
 * This file provides support for window-related functions. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxsync.h"
#include "dmxwindow.h"
#include "dmxpixmap.h"
#include "dmxcmap.h"
#include "dmxvisual.h"
#include "dmxinput.h"
#include "dmxextension.h"
#include "dmxpict.h"

#include "windowstr.h"

static void dmxDoRestackWindow(WindowPtr pWindow);
static void dmxDoChangeWindowAttributes(WindowPtr pWindow,
                                        unsigned long *mask,
                                        XSetWindowAttributes * attribs);

static void dmxDoSetShape(WindowPtr pWindow);

/** Initialize the private area for the window functions. */
Bool
dmxInitWindow(ScreenPtr pScreen)
{
    if (!dixRegisterPrivateKey
        (&dmxWinPrivateKeyRec, PRIVATE_WINDOW, sizeof(dmxWinPrivRec)))
        return FALSE;

    return TRUE;
}

Window
dmxCreateRootWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);
    Window parent;
    Visual *visual;
    unsigned long mask;
    XSetWindowAttributes attribs;
    ColormapPtr pCmap;
    dmxColormapPrivPtr pCmapPriv;

    /* Create root window */

    parent = dmxScreen->scrnWin;        /* This is our "Screen" window */
    visual = dmxScreen->beVisuals[dmxScreen->beDefVisualIndex].visual;

    dixLookupResourceByType((void **) &pCmap, wColormap(pWindow),
                            RT_COLORMAP, NullClient, DixUnknownAccess);
    pCmapPriv = DMX_GET_COLORMAP_PRIV(pCmap);

    mask = CWEventMask | CWBackingStore | CWColormap | CWBorderPixel;
    attribs.event_mask = ExposureMask;
    attribs.backing_store = NotUseful;
    attribs.colormap = pCmapPriv->cmap;
    attribs.border_pixel = 0;

    /* Incorporate new attributes, if needed */
    if (pWinPriv->attribMask) {
        dmxDoChangeWindowAttributes(pWindow, &pWinPriv->attribMask, &attribs);
        mask |= pWinPriv->attribMask;
    }

    return XCreateWindow(dmxScreen->beDisplay,
                         parent,
                         pWindow->origin.x - wBorderWidth(pWindow),
                         pWindow->origin.y - wBorderWidth(pWindow),
                         pWindow->drawable.width,
                         pWindow->drawable.height,
                         pWindow->borderWidth,
                         pWindow->drawable.depth,
                         pWindow->drawable.class, visual, mask, &attribs);
}

/** Change the location and size of the "screen" window.  Called from
 *  dmxextension.c dmxConfigureScreenWindow(). */
void
dmxResizeScreenWindow(ScreenPtr pScreen, int x, int y, int w, int h)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    unsigned int m;
    XWindowChanges c;

    if (!dmxScreen->beDisplay)
        return;

    /* Handle resizing on back-end server */
    m = CWX | CWY | CWWidth | CWHeight;
    c.x = x;
    c.y = y;
    c.width = w;
    c.height = h;

    XConfigureWindow(dmxScreen->beDisplay, dmxScreen->scrnWin, m, &c);
    dmxSync(dmxScreen, False);
}

/** Change the location and size of the "root" window.  Called from
 *  #dmxCreateWindow. */
void
dmxResizeRootWindow(WindowPtr pRoot, int x, int y, int w, int h)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pRoot->drawable.pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pRoot);
    unsigned int m;
    XWindowChanges c;

    /* Handle resizing on back-end server */
    if (dmxScreen->beDisplay) {
        m = CWX | CWY | CWWidth | CWHeight;
        c.x = x;
        c.y = y;
        c.width = (w > 0) ? w : 1;
        c.height = (h > 0) ? h : 1;

        XConfigureWindow(dmxScreen->beDisplay, pWinPriv->window, m, &c);
    }

    if (w == 0 || h == 0) {
        if (pWinPriv->mapped) {
            if (dmxScreen->beDisplay)
                XUnmapWindow(dmxScreen->beDisplay, pWinPriv->window);
            pWinPriv->mapped = FALSE;
        }
    }
    else if (!pWinPriv->mapped) {
        if (dmxScreen->beDisplay)
            XMapWindow(dmxScreen->beDisplay, pWinPriv->window);
        pWinPriv->mapped = TRUE;
    }

    if (dmxScreen->beDisplay)
        dmxSync(dmxScreen, False);
}

void
dmxGetDefaultWindowAttributes(WindowPtr pWindow,
                              Colormap * cmap, Visual ** visual)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;

    if (pWindow->drawable.class != InputOnly &&
        pWindow->optional &&
        pWindow->optional->visual != wVisual(pWindow->parent)) {

        /* Find the matching visual */
        *visual = dmxLookupVisualFromID(pScreen, wVisual(pWindow));

        /* Handle optional colormaps */
        if (pWindow->optional->colormap) {
            ColormapPtr pCmap;
            dmxColormapPrivPtr pCmapPriv;

            dixLookupResourceByType((void **) &pCmap, wColormap(pWindow),
                                    RT_COLORMAP, NullClient, DixUnknownAccess);
            pCmapPriv = DMX_GET_COLORMAP_PRIV(pCmap);
            *cmap = pCmapPriv->cmap;
        }
        else {
            *cmap = dmxColormapFromDefaultVisual(pScreen, *visual);
        }
    }
    else {
        *visual = CopyFromParent;
        *cmap = (Colormap) 0;
    }
}

static Window
dmxCreateNonRootWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);
    Window parent;
    unsigned long mask = 0L;
    XSetWindowAttributes attribs;
    dmxWinPrivPtr pParentPriv = DMX_GET_WINDOW_PRIV(pWindow->parent);

    /* Create window on back-end server */

    parent = pParentPriv->window;

    /* The parent won't exist if this call to CreateNonRootWindow came
       from ReparentWindow and the grandparent window has not yet been
       created */
    if (!parent) {
        dmxCreateAndRealizeWindow(pWindow->parent, FALSE);
        parent = pParentPriv->window;
    }

    /* Incorporate new attributes, if needed */
    if (pWinPriv->attribMask) {
        dmxDoChangeWindowAttributes(pWindow, &pWinPriv->attribMask, &attribs);
        mask |= pWinPriv->attribMask;
    }

    /* Add in default attributes */
    if (pWindow->drawable.class != InputOnly) {
        mask |= CWBackingStore;
        attribs.backing_store = NotUseful;

        if (!(mask & CWColormap) && pWinPriv->cmap) {
            mask |= CWColormap;
            attribs.colormap = pWinPriv->cmap;
            if (!(mask & CWBorderPixel)) {
                mask |= CWBorderPixel;
                attribs.border_pixel = 0;
            }
        }
    }

    /* Handle case where subwindows are being mapped, but created out of
       order -- if current window has a previous sibling, then it cannot
       be created on top of the stack, so we must restack the windows */
    pWinPriv->restacked = (pWindow->prevSib != NullWindow);

    return XCreateWindow(dmxScreen->beDisplay,
                         parent,
                         pWindow->origin.x - wBorderWidth(pWindow),
                         pWindow->origin.y - wBorderWidth(pWindow),
                         pWindow->drawable.width,
                         pWindow->drawable.height,
                         pWindow->borderWidth,
                         pWindow->drawable.depth,
                         pWindow->drawable.class,
                         pWinPriv->visual, mask, &attribs);
}

/** This function handles lazy window creation and realization.  Window
 *  creation is handled by #dmxCreateNonRootWindow().  It also handles
 *  any stacking changes that have occured since the window was
 *  originally created by calling #dmxDoRestackWindow().  If the window
 *  is shaped, the shape is set on the back-end server by calling
 *  #dmxDoSetShape(), and if the window has pictures (from RENDER)
 *  associated with it, those pictures are created on the back-end
 *  server by calling #dmxCreatePictureList().  If \a doSync is TRUE,
 *  then #dmxSync() is called. */
void
dmxCreateAndRealizeWindow(WindowPtr pWindow, Bool doSync)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);

    if (!dmxScreen->beDisplay)
        return;

    pWinPriv->window = dmxCreateNonRootWindow(pWindow);
    if (pWinPriv->restacked)
        dmxDoRestackWindow(pWindow);
    if (pWinPriv->isShaped)
        dmxDoSetShape(pWindow);
    if (pWinPriv->hasPict)
        dmxCreatePictureList(pWindow);
    if (pWinPriv->mapped)
        XMapWindow(dmxScreen->beDisplay, pWinPriv->window);
    if (doSync)
        dmxSync(dmxScreen, False);
}

/** Create \a pWindow on the back-end server.  If the lazy window
 *  creation optimization is enabled, then the actual creation and
 *  realization of the window is handled by
 *  #dmxCreateAndRealizeWindow(). */
Bool
dmxCreateWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);
    Bool ret = TRUE;

    DMX_UNWRAP(CreateWindow, dmxScreen, pScreen);
#if 0
    if (pScreen->CreateWindow)
        ret = pScreen->CreateWindow(pWindow);
#endif

    /* Set up the defaults */
    pWinPriv->window = (Window) 0;
    pWinPriv->offscreen = TRUE;
    pWinPriv->mapped = FALSE;
    pWinPriv->restacked = FALSE;
    pWinPriv->attribMask = 0;
    pWinPriv->isShaped = FALSE;
    pWinPriv->hasPict = FALSE;
#ifdef GLXEXT
    pWinPriv->swapGroup = NULL;
    pWinPriv->barrier = 0;
#endif

    if (dmxScreen->beDisplay) {
        /* Only create the root window at this stage -- non-root windows are
           created when they are mapped and are on-screen */
        if (!pWindow->parent) {
            dmxScreen->rootWin = pWinPriv->window
                = dmxCreateRootWindow(pWindow);
            if (dmxScreen->scrnX != dmxScreen->rootX
                || dmxScreen->scrnY != dmxScreen->rootY
                || dmxScreen->scrnWidth != dmxScreen->rootWidth
                || dmxScreen->scrnHeight != dmxScreen->rootHeight) {
                dmxResizeRootWindow(pWindow,
                                    dmxScreen->rootX,
                                    dmxScreen->rootY,
                                    dmxScreen->rootWidth,
                                    dmxScreen->rootHeight);
                dmxUpdateScreenResources(screenInfo.screens[dmxScreen->index],
                                         dmxScreen->rootX,
                                         dmxScreen->rootY,
                                         dmxScreen->rootWidth,
                                         dmxScreen->rootHeight);
                pWindow->origin.x = dmxScreen->rootX;
                pWindow->origin.y = dmxScreen->rootY;
            }
        }
        else {
            dmxGetDefaultWindowAttributes(pWindow,
                                          &pWinPriv->cmap, &pWinPriv->visual);

            if (dmxLazyWindowCreation) {
                /* Save parent's visual for use later */
                if (pWinPriv->visual == CopyFromParent)
                    pWinPriv->visual =
                        dmxLookupVisualFromID(pScreen,
                                              wVisual(pWindow->parent));
            }
            else {
                pWinPriv->window = dmxCreateNonRootWindow(pWindow);
            }
        }

        dmxSync(dmxScreen, False);
    }

    DMX_WRAP(CreateWindow, dmxCreateWindow, dmxScreen, pScreen);

    return ret;
}

/** Destroy \a pWindow on the back-end server. */
Bool
dmxBEDestroyWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);

    if (pWinPriv->window) {
        XDestroyWindow(dmxScreen->beDisplay, pWinPriv->window);
        pWinPriv->window = (Window) 0;
        return TRUE;
    }

    return FALSE;
}

/** Destroy \a pWindow on the back-end server.  If any RENDER pictures
    were created, destroy them as well. */
Bool
dmxDestroyWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    Bool ret = TRUE;
    Bool needSync = FALSE;

#ifdef GLXEXT
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);
#endif

    DMX_UNWRAP(DestroyWindow, dmxScreen, pScreen);

    /* Destroy any picture list associated with this window */
    needSync |= dmxDestroyPictureList(pWindow);

    /* Destroy window on back-end server */
    needSync |= dmxBEDestroyWindow(pWindow);
    if (needSync)
        dmxSync(dmxScreen, FALSE);

#ifdef GLXEXT
    if (pWinPriv->swapGroup && pWinPriv->windowDestroyed)
        pWinPriv->windowDestroyed(pWindow);
#endif

    if (pScreen->DestroyWindow)
        ret = pScreen->DestroyWindow(pWindow);

    DMX_WRAP(DestroyWindow, dmxDestroyWindow, dmxScreen, pScreen);

    return ret;
}

/** Change the position of \a pWindow to be \a x, \a y. */
Bool
dmxPositionWindow(WindowPtr pWindow, int x, int y)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    Bool ret = TRUE;
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);
    unsigned int m;
    XWindowChanges c;

    DMX_UNWRAP(PositionWindow, dmxScreen, pScreen);
#if 0
    if (pScreen->PositionWindow)
        ret = pScreen->PositionWindow(pWindow, x, y);
#endif

    /* Determine if the window is completely off the visible portion of
       the screen */
    pWinPriv->offscreen = DMX_WINDOW_OFFSCREEN(pWindow);

    /* If the window is now on-screen and it is mapped and it has not
       been created yet, create it and map it */
    if (!pWinPriv->window && pWinPriv->mapped && !pWinPriv->offscreen) {
        dmxCreateAndRealizeWindow(pWindow, TRUE);
    }
    else if (pWinPriv->window) {
        /* Position window on back-end server */
        m = CWX | CWY | CWWidth | CWHeight;
        c.x = pWindow->origin.x - wBorderWidth(pWindow);
        c.y = pWindow->origin.y - wBorderWidth(pWindow);
        c.width = pWindow->drawable.width;
        c.height = pWindow->drawable.height;
        if (pWindow->drawable.class != InputOnly) {
            m |= CWBorderWidth;
            c.border_width = pWindow->borderWidth;
        }

        XConfigureWindow(dmxScreen->beDisplay, pWinPriv->window, m, &c);
        dmxSync(dmxScreen, False);
    }

    DMX_WRAP(PositionWindow, dmxPositionWindow, dmxScreen, pScreen);

    return ret;
}

static void
dmxDoChangeWindowAttributes(WindowPtr pWindow,
                            unsigned long *mask, XSetWindowAttributes * attribs)
{
    dmxPixPrivPtr pPixPriv;

    if (*mask & CWBackPixmap) {
        switch (pWindow->backgroundState) {
        case None:
            attribs->background_pixmap = None;
            break;

        case ParentRelative:
            attribs->background_pixmap = ParentRelative;
            break;

        case BackgroundPixmap:
            pPixPriv = DMX_GET_PIXMAP_PRIV(pWindow->background.pixmap);
            attribs->background_pixmap = pPixPriv->pixmap;
            break;

        case BackgroundPixel:
            *mask &= ~CWBackPixmap;
            break;
        }
    }

    if (*mask & CWBackPixel) {
        if (pWindow->backgroundState == BackgroundPixel)
            attribs->background_pixel = pWindow->background.pixel;
        else
            *mask &= ~CWBackPixel;
    }

    if (*mask & CWBorderPixmap) {
        if (pWindow->borderIsPixel)
            *mask &= ~CWBorderPixmap;
        else {
            pPixPriv = DMX_GET_PIXMAP_PRIV(pWindow->border.pixmap);
            attribs->border_pixmap = pPixPriv->pixmap;
        }
    }

    if (*mask & CWBorderPixel) {
        if (pWindow->borderIsPixel)
            attribs->border_pixel = pWindow->border.pixel;
        else
            *mask &= ~CWBorderPixel;
    }

    if (*mask & CWBitGravity)
        attribs->bit_gravity = pWindow->bitGravity;

    if (*mask & CWWinGravity)
        *mask &= ~CWWinGravity; /* Handled by dix */

    if (*mask & CWBackingStore)
        *mask &= ~CWBackingStore;       /* Backing store not supported */

    if (*mask & CWBackingPlanes)
        *mask &= ~CWBackingPlanes;      /* Backing store not supported */

    if (*mask & CWBackingPixel)
        *mask &= ~CWBackingPixel;       /* Backing store not supported */

    if (*mask & CWOverrideRedirect)
        attribs->override_redirect = pWindow->overrideRedirect;

    if (*mask & CWSaveUnder)
        *mask &= ~CWSaveUnder;  /* Save unders not supported */

    if (*mask & CWEventMask)
        *mask &= ~CWEventMask;  /* Events are handled by dix */

    if (*mask & CWDontPropagate)
        *mask &= ~CWDontPropagate;      /* Events are handled by dix */

    if (*mask & CWColormap) {
        ColormapPtr pCmap;
        dmxColormapPrivPtr pCmapPriv;

        dixLookupResourceByType((void **) &pCmap, wColormap(pWindow),
                                RT_COLORMAP, NullClient, DixUnknownAccess);
        pCmapPriv = DMX_GET_COLORMAP_PRIV(pCmap);
        attribs->colormap = pCmapPriv->cmap;
    }

    if (*mask & CWCursor)
        *mask &= ~CWCursor;     /* Handled by the cursor code */
}

/** Change the window attributes of \a pWindow. */
Bool
dmxChangeWindowAttributes(WindowPtr pWindow, unsigned long mask)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    Bool ret = TRUE;
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);
    XSetWindowAttributes attribs;

    DMX_UNWRAP(ChangeWindowAttributes, dmxScreen, pScreen);
#if 0
    if (pScreen->ChangeWindowAttributes)
        ret = pScreen->ChangeWindowAttributes(pWindow, mask);
#endif

    /* Change window attribs on back-end server */
    dmxDoChangeWindowAttributes(pWindow, &mask, &attribs);

    /* Save mask for lazy window creation optimization */
    pWinPriv->attribMask |= mask;

    if (mask && pWinPriv->window) {
        XChangeWindowAttributes(dmxScreen->beDisplay, pWinPriv->window,
                                mask, &attribs);
        dmxSync(dmxScreen, False);
    }

    DMX_WRAP(ChangeWindowAttributes, dmxChangeWindowAttributes, dmxScreen,
             pScreen);

    return ret;
}

/** Realize \a pWindow on the back-end server.  If the lazy window
 *  creation optimization is enabled, the window is only realized when
 *  it at least partially overlaps the screen. */
Bool
dmxRealizeWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    Bool ret = TRUE;
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);

    DMX_UNWRAP(RealizeWindow, dmxScreen, pScreen);
#if 0
    if (pScreen->RealizeWindow)
        ret = pScreen->RealizeWindow(pWindow);
#endif

    /* Determine if the window is completely off the visible portion of
       the screen */
    pWinPriv->offscreen = DMX_WINDOW_OFFSCREEN(pWindow);

    /* If the window hasn't been created and it's not offscreen, then
       create it */
    if (!pWinPriv->window && !pWinPriv->offscreen) {
        dmxCreateAndRealizeWindow(pWindow, FALSE);
    }

    if (pWinPriv->window) {
        /* Realize window on back-end server */
        XMapWindow(dmxScreen->beDisplay, pWinPriv->window);
        dmxSync(dmxScreen, False);
    }

    /* Let the other functions know that the window is now mapped */
    pWinPriv->mapped = TRUE;

    DMX_WRAP(RealizeWindow, dmxRealizeWindow, dmxScreen, pScreen);

    dmxUpdateWindowInfo(DMX_UPDATE_REALIZE, pWindow);
    return ret;
}

/** Unrealize \a pWindow on the back-end server. */
Bool
dmxUnrealizeWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    Bool ret = TRUE;
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);

    DMX_UNWRAP(UnrealizeWindow, dmxScreen, pScreen);
#if 0
    if (pScreen->UnrealizeWindow)
        ret = pScreen->UnrealizeWindow(pWindow);
#endif

    if (pWinPriv->window) {
        /* Unrealize window on back-end server */
        XUnmapWindow(dmxScreen->beDisplay, pWinPriv->window);
        dmxSync(dmxScreen, False);
    }

    /* When unrealized (i.e., unmapped), the window is always considered
       off of the visible portion of the screen */
    pWinPriv->offscreen = TRUE;
    pWinPriv->mapped = FALSE;

#ifdef GLXEXT
    if (pWinPriv->swapGroup && pWinPriv->windowUnmapped)
        pWinPriv->windowUnmapped(pWindow);
#endif

    DMX_WRAP(UnrealizeWindow, dmxUnrealizeWindow, dmxScreen, pScreen);

    dmxUpdateWindowInfo(DMX_UPDATE_UNREALIZE, pWindow);
    return ret;
}

static void
dmxDoRestackWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);
    WindowPtr pNextSib = pWindow->nextSib;
    unsigned int m;
    XWindowChanges c;

    if (pNextSib == NullWindow) {
        /* Window is at the bottom of the stack */
        m = CWStackMode;
        c.sibling = (Window) 0;
        c.stack_mode = Below;
        XConfigureWindow(dmxScreen->beDisplay, pWinPriv->window, m, &c);
    }
    else {
        /* Window is not at the bottom of the stack */
        dmxWinPrivPtr pNextSibPriv = DMX_GET_WINDOW_PRIV(pNextSib);

        /* Handle case where siblings have not yet been created due to
           lazy window creation optimization by first finding the next
           sibling in the sibling list that has been created (if any)
           and then putting the current window just above that sibling,
           and if no next siblings have been created yet, then put it at
           the bottom of the stack (since it might have a previous
           sibling that should be above it). */
        while (!pNextSibPriv->window) {
            pNextSib = pNextSib->nextSib;
            if (pNextSib == NullWindow) {
                /* Window is at the bottom of the stack */
                m = CWStackMode;
                c.sibling = (Window) 0;
                c.stack_mode = Below;
                XConfigureWindow(dmxScreen->beDisplay, pWinPriv->window, m, &c);
                return;
            }
            pNextSibPriv = DMX_GET_WINDOW_PRIV(pNextSib);
        }

        m = CWStackMode | CWSibling;
        c.sibling = pNextSibPriv->window;
        c.stack_mode = Above;
        XConfigureWindow(dmxScreen->beDisplay, pWinPriv->window, m, &c);
    }
}

/** Handle window restacking.  The actual restacking occurs in
 *  #dmxDoRestackWindow(). */
void
dmxRestackWindow(WindowPtr pWindow, WindowPtr pOldNextSib)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);

    DMX_UNWRAP(RestackWindow, dmxScreen, pScreen);
#if 0
    if (pScreen->RestackWindow)
        pScreen->RestackWindow(pWindow, pOldNextSib);
#endif

    if (pOldNextSib != pWindow->nextSib) {
        /* Track restacking for lazy window creation optimization */
        pWinPriv->restacked = TRUE;

        /* Restack window on back-end server */
        if (pWinPriv->window) {
            dmxDoRestackWindow(pWindow);
            dmxSync(dmxScreen, False);
        }
    }

    DMX_WRAP(RestackWindow, dmxRestackWindow, dmxScreen, pScreen);
    dmxUpdateWindowInfo(DMX_UPDATE_RESTACK, pWindow);
}

static Bool
dmxWindowExposurePredicate(Display * dpy, XEvent * ev, XPointer ptr)
{
    return (ev->type == Expose && ev->xexpose.window == *(Window *) ptr);
}

/** Handle exposures on \a pWindow.  Since window exposures are handled
 *  in DMX, the events that are generated by the back-end server are
 *  redundant, so we eat them here. */
void
dmxWindowExposures(WindowPtr pWindow, RegionPtr prgn)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);
    XEvent ev;

    DMX_UNWRAP(WindowExposures, dmxScreen, pScreen);

    dmxSync(dmxScreen, False);

    if (pWinPriv->window) {
        while (XCheckIfEvent(dmxScreen->beDisplay, &ev,
                             dmxWindowExposurePredicate,
                             (XPointer) &pWinPriv->window)) {
            /* Handle expose events -- this should not be necessary
               since the base window in which the root window was
               created is guaranteed to be on top (override_redirect),
               so we should just swallow these events.  If for some
               reason the window is not on top, then we'd need to
               collect these events and send them to the client later
               (e.g., during the block handler as Xnest does). */
        }
    }

#if 1
    if (pScreen->WindowExposures)
        pScreen->WindowExposures(pWindow, prgn);
#endif
    DMX_WRAP(WindowExposures, dmxWindowExposures, dmxScreen, pScreen);
}

/** Move \a pWindow on the back-end server.  Determine whether or not it
 *  is on or offscreen, and realize it if it is newly on screen and the
 *  lazy window creation optimization is enabled. */
void
dmxCopyWindow(WindowPtr pWindow, DDXPointRec ptOldOrg, RegionPtr prgnSrc)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);
    unsigned int m;
    XWindowChanges c;

    DMX_UNWRAP(CopyWindow, dmxScreen, pScreen);
#if 0
    if (pScreen->CopyWindow)
        pScreen->CopyWindow(pWindow, ptOldOrg, prgnSrc);
#endif

    /* Determine if the window is completely off the visible portion of
       the screen */
    pWinPriv->offscreen = DMX_WINDOW_OFFSCREEN(pWindow);

    /* If the window is now on-screen and it is mapped and it has not
       been created yet, create it and map it */
    if (!pWinPriv->window && pWinPriv->mapped && !pWinPriv->offscreen) {
        dmxCreateAndRealizeWindow(pWindow, TRUE);
    }
    else if (pWinPriv->window) {
        /* Move window on back-end server */
        m = CWX | CWY | CWWidth | CWHeight;
        c.x = pWindow->origin.x - wBorderWidth(pWindow);
        c.y = pWindow->origin.y - wBorderWidth(pWindow);
        c.width = pWindow->drawable.width;
        c.height = pWindow->drawable.height;

        XConfigureWindow(dmxScreen->beDisplay, pWinPriv->window, m, &c);
        dmxSync(dmxScreen, False);
    }

    DMX_WRAP(CopyWindow, dmxCopyWindow, dmxScreen, pScreen);
    dmxUpdateWindowInfo(DMX_UPDATE_COPY, pWindow);
}

/** Resize \a pWindow on the back-end server.  Determine whether or not
 *  it is on or offscreen, and realize it if it is newly on screen and
 *  the lazy window creation optimization is enabled. */
void
dmxResizeWindow(WindowPtr pWindow, int x, int y,
                unsigned int w, unsigned int h, WindowPtr pSib)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);
    dmxWinPrivPtr pSibPriv;
    unsigned int m;
    XWindowChanges c;

    if (pSib)
        pSibPriv = DMX_GET_WINDOW_PRIV(pSib);

    DMX_UNWRAP(ResizeWindow, dmxScreen, pScreen);
#if 1
    if (pScreen->ResizeWindow)
        pScreen->ResizeWindow(pWindow, x, y, w, h, pSib);
#endif

    /* Determine if the window is completely off the visible portion of
       the screen */
    pWinPriv->offscreen = DMX_WINDOW_OFFSCREEN(pWindow);

    /* If the window is now on-screen and it is mapped and it has not
       been created yet, create it and map it */
    if (!pWinPriv->window && pWinPriv->mapped && !pWinPriv->offscreen) {
        dmxCreateAndRealizeWindow(pWindow, TRUE);
    }
    else if (pWinPriv->window) {
        /* Handle resizing on back-end server */
        m = CWX | CWY | CWWidth | CWHeight;
        c.x = pWindow->origin.x - wBorderWidth(pWindow);
        c.y = pWindow->origin.y - wBorderWidth(pWindow);
        c.width = pWindow->drawable.width;
        c.height = pWindow->drawable.height;

        XConfigureWindow(dmxScreen->beDisplay, pWinPriv->window, m, &c);
        dmxSync(dmxScreen, False);
    }

    DMX_WRAP(ResizeWindow, dmxResizeWindow, dmxScreen, pScreen);
    dmxUpdateWindowInfo(DMX_UPDATE_RESIZE, pWindow);
}

/** Reparent \a pWindow on the back-end server. */
void
dmxReparentWindow(WindowPtr pWindow, WindowPtr pPriorParent)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);
    dmxWinPrivPtr pParentPriv = DMX_GET_WINDOW_PRIV(pWindow->parent);

    DMX_UNWRAP(ReparentWindow, dmxScreen, pScreen);
#if 0
    if (pScreen->ReparentWindow)
        pScreen->ReparentWindow(pWindow, pPriorParent);
#endif

    if (pWinPriv->window) {
        if (!pParentPriv->window) {
            dmxCreateAndRealizeWindow(pWindow->parent, FALSE);
        }

        /* Handle reparenting on back-end server */
        XReparentWindow(dmxScreen->beDisplay, pWinPriv->window,
                        pParentPriv->window,
                        pWindow->origin.x - wBorderWidth(pWindow),
                        pWindow->origin.x - wBorderWidth(pWindow));
        dmxSync(dmxScreen, False);
    }

    DMX_WRAP(ReparentWindow, dmxReparentWindow, dmxScreen, pScreen);
    dmxUpdateWindowInfo(DMX_UPDATE_REPARENT, pWindow);
}

/** Change border width for \a pWindow to \a width pixels. */
void
dmxChangeBorderWidth(WindowPtr pWindow, unsigned int width)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);
    unsigned int m;
    XWindowChanges c;

    DMX_UNWRAP(ChangeBorderWidth, dmxScreen, pScreen);
#if 1
    if (pScreen->ChangeBorderWidth)
        pScreen->ChangeBorderWidth(pWindow, width);
#endif

    /* NOTE: Do we need to check for on/off screen here? */

    if (pWinPriv->window) {
        /* Handle border width change on back-end server */
        m = CWBorderWidth;
        c.border_width = width;

        XConfigureWindow(dmxScreen->beDisplay, pWinPriv->window, m, &c);
        dmxSync(dmxScreen, False);
    }

    DMX_WRAP(ChangeBorderWidth, dmxChangeBorderWidth, dmxScreen, pScreen);
}

static void
dmxDoSetShape(WindowPtr pWindow)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);
    int nBox;
    BoxPtr pBox;
    int nRect;
    XRectangle *pRect;
    XRectangle *pRectFirst;

    /* First, set the bounding shape */
    if (wBoundingShape(pWindow)) {
        pBox = RegionRects(wBoundingShape(pWindow));
        nRect = nBox = RegionNumRects(wBoundingShape(pWindow));
        pRectFirst = pRect = malloc(nRect * sizeof(*pRect));
        while (nBox--) {
            pRect->x = pBox->x1;
            pRect->y = pBox->y1;
            pRect->width = pBox->x2 - pBox->x1;
            pRect->height = pBox->y2 - pBox->y1;
            pBox++;
            pRect++;
        }
        XShapeCombineRectangles(dmxScreen->beDisplay, pWinPriv->window,
                                ShapeBounding, 0, 0,
                                pRectFirst, nRect, ShapeSet, YXBanded);
        free(pRectFirst);
    }
    else {
        XShapeCombineMask(dmxScreen->beDisplay, pWinPriv->window,
                          ShapeBounding, 0, 0, None, ShapeSet);
    }

    /* Next, set the clip shape */
    if (wClipShape(pWindow)) {
        pBox = RegionRects(wClipShape(pWindow));
        nRect = nBox = RegionNumRects(wClipShape(pWindow));
        pRectFirst = pRect = malloc(nRect * sizeof(*pRect));
        while (nBox--) {
            pRect->x = pBox->x1;
            pRect->y = pBox->y1;
            pRect->width = pBox->x2 - pBox->x1;
            pRect->height = pBox->y2 - pBox->y1;
            pBox++;
            pRect++;
        }
        XShapeCombineRectangles(dmxScreen->beDisplay, pWinPriv->window,
                                ShapeClip, 0, 0,
                                pRectFirst, nRect, ShapeSet, YXBanded);
        free(pRectFirst);
    }
    else {
        XShapeCombineMask(dmxScreen->beDisplay, pWinPriv->window,
                          ShapeClip, 0, 0, None, ShapeSet);
    }

    if (XShapeInputSelected(dmxScreen->beDisplay, pWinPriv->window)) {
        ErrorF("Input selected for window %x on Screen %d\n",
               (unsigned int) pWinPriv->window, pScreen->myNum);
    }
}

/** Set shape of \a pWindow on the back-end server. */
void
dmxSetShape(WindowPtr pWindow, int kind)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);

    DMX_UNWRAP(SetShape, dmxScreen, pScreen);
#if 1
    if (pScreen->SetShape)
        pScreen->SetShape(pWindow, kind);
#endif

    if (pWinPriv->window) {
        /* Handle setting the current shape on the back-end server */
        dmxDoSetShape(pWindow);
        dmxSync(dmxScreen, False);
    }
    else {
        pWinPriv->isShaped = TRUE;
    }

    DMX_WRAP(SetShape, dmxSetShape, dmxScreen, pScreen);
}
