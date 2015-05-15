/*
 * Copyright 2003-2004 Red Hat Inc., Durham, North Carolina.
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
 * Author:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *   Kevin E. Martin <kem@redhat.com>
 *
 */

/** \file
 * This file provides the only interface to the X server extension support
 * in programs/Xserver/Xext.  Those programs should only include dmxext.h
 */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include <stdlib.h>

#include "dmx.h"
#include "dmxinit.h"
#include "dmxextension.h"
#include "dmxwindow.h"
#include "dmxcb.h"
#include "dmxcursor.h"
#include "dmxpixmap.h"
#include "dmxgc.h"
#include "dmxfont.h"
#include "dmxcmap.h"
#include "dmxpict.h"
#include "dmxinput.h"
#include "dmxsync.h"
#include "dmxscrinit.h"
#include "input/dmxinputinit.h"

#include "windowstr.h"
#include "inputstr.h"           /* For DeviceIntRec */
#include <X11/extensions/dmxproto.h>    /* For DMX_BAD_* */
#include "cursorstr.h"

/* The default font is declared in dix/globals.c, but is not included in
 * _any_ header files. */
extern FontPtr defaultFont;

/* Hack to get Present to build (present requires RandR) */
RESTYPE RRCrtcType;

/** This routine provides information to the DMX protocol extension
 * about a particular screen. */
Bool
dmxGetScreenAttributes(int physical, DMXScreenAttributesPtr attr)
{
    DMXScreenInfo *dmxScreen;

    if (physical < 0 || physical >= dmxNumScreens)
        return FALSE;

    dmxScreen = &dmxScreens[physical];
    attr->displayName = dmxScreen->name;
#ifdef PANORAMIX
    attr->logicalScreen = noPanoramiXExtension ? dmxScreen->index : 0;
#else
    attr->logicalScreen = dmxScreen->index;
#endif

    attr->screenWindowWidth = dmxScreen->scrnWidth;
    attr->screenWindowHeight = dmxScreen->scrnHeight;
    attr->screenWindowXoffset = dmxScreen->scrnX;
    attr->screenWindowYoffset = dmxScreen->scrnY;

    attr->rootWindowWidth = dmxScreen->rootWidth;
    attr->rootWindowHeight = dmxScreen->rootHeight;
    attr->rootWindowXoffset = dmxScreen->rootX;
    attr->rootWindowYoffset = dmxScreen->rootY;

    attr->rootWindowXorigin = dmxScreen->rootXOrigin;
    attr->rootWindowYorigin = dmxScreen->rootYOrigin;

    return TRUE;
}

/** This routine provides information to the DMX protocol extension
 * about a particular window. */
Bool
dmxGetWindowAttributes(WindowPtr pWindow, DMXWindowAttributesPtr attr)
{
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);

    attr->screen = pWindow->drawable.pScreen->myNum;
    attr->window = pWinPriv->window;

    attr->pos.x = pWindow->drawable.x;
    attr->pos.y = pWindow->drawable.y;
    attr->pos.width = pWindow->drawable.width;
    attr->pos.height = pWindow->drawable.height;

    if (!pWinPriv->window || pWinPriv->offscreen) {
        attr->vis.x = 0;
        attr->vis.y = 0;
        attr->vis.height = 0;
        attr->vis.width = 0;
        return pWinPriv->window ? TRUE : FALSE;
    }

    /* Compute display-relative coordinates */
    attr->vis.x = pWindow->drawable.x;
    attr->vis.y = pWindow->drawable.y;
    attr->vis.width = pWindow->drawable.width;
    attr->vis.height = pWindow->drawable.height;

    if (attr->pos.x < 0) {
        attr->vis.x -= attr->pos.x;
        attr->vis.width = attr->pos.x + attr->pos.width - attr->vis.x;
    }
    if (attr->pos.x + attr->pos.width > pWindow->drawable.pScreen->width) {
        if (attr->pos.x < 0)
            attr->vis.width = pWindow->drawable.pScreen->width;
        else
            attr->vis.width = pWindow->drawable.pScreen->width - attr->pos.x;
    }
    if (attr->pos.y < 0) {
        attr->vis.y -= attr->pos.y;
        attr->vis.height = attr->pos.y + attr->pos.height - attr->vis.y;
    }
    if (attr->pos.y + attr->pos.height > pWindow->drawable.pScreen->height) {
        if (attr->pos.y < 0)
            attr->vis.height = pWindow->drawable.pScreen->height;
        else
            attr->vis.height = pWindow->drawable.pScreen->height - attr->pos.y;
    }

    /* Convert to window-relative coordinates */
    attr->vis.x -= attr->pos.x;
    attr->vis.y -= attr->pos.y;

    return TRUE;
}

void
dmxGetDesktopAttributes(DMXDesktopAttributesPtr attr)
{
    attr->width = dmxGlobalWidth;
    attr->height = dmxGlobalHeight;
    attr->shiftX = 0;           /* NOTE: The upper left hand corner of */
    attr->shiftY = 0;           /*       the desktop is always <0,0>. */
}

/** Return the total number of devices, not just #dmxNumInputs.  The
 * number returned should be the same as that returned by
 * XListInputDevices. */
int
dmxGetInputCount(void)
{
    int i, total;

    for (total = i = 0; i < dmxNumInputs; i++)
        total += dmxInputs[i].numDevs;
    return total;
}

/** Return information about the device with id = \a deviceId.  This
 * information is primarily for the #ProcDMXGetInputAttributes()
 * function, which does not have access to the appropriate data
 * structure. */
int
dmxGetInputAttributes(int deviceId, DMXInputAttributesPtr attr)
{
    int i, j;
    DMXInputInfo *dmxInput;

    if (deviceId < 0)
        return -1;
    for (i = 0; i < dmxNumInputs; i++) {
        dmxInput = &dmxInputs[i];
        for (j = 0; j < dmxInput->numDevs; j++) {
            DMXLocalInputInfoPtr dmxLocal = dmxInput->devs[j];

            if (deviceId != dmxLocal->pDevice->id)
                continue;
            attr->isCore = ! !dmxLocal->isCore;
            attr->sendsCore = ! !dmxLocal->sendsCore;
            attr->detached = ! !dmxInput->detached;
            attr->physicalScreen = -1;
            attr->physicalId = -1;
            attr->name = NULL;
            switch (dmxLocal->extType) {
            case DMX_LOCAL_TYPE_LOCAL:
                attr->inputType = 0;
                break;
            case DMX_LOCAL_TYPE_CONSOLE:
                attr->inputType = 1;
                attr->name = dmxInput->name;
                attr->physicalId = dmxLocal->deviceId;
                break;
            case DMX_LOCAL_TYPE_BACKEND:
            case DMX_LOCAL_TYPE_COMMON:
                attr->inputType = 2;
                attr->physicalScreen = dmxInput->scrnIdx;
                attr->name = dmxInput->name;
                attr->physicalId = dmxLocal->deviceId;
                break;
            }
            return 0;           /* Success */
        }
    }
    return -1;                  /* Failure */
}

/** Reinitialized the cursor boundaries. */
static void
dmxAdjustCursorBoundaries(void)
{
    int i;

    dmxReInitOrigins();
    dmxInitOverlap();
    dmxComputeWidthHeight(DMX_NO_RECOMPUTE_BOUNDING_BOX);
    dmxConnectionBlockCallback();
    for (i = 0; i < dmxNumInputs; i++) {
        DMXInputInfo *dmxInput = &dmxInputs[i];

        if (!dmxInput->detached)
            dmxInputReInit(dmxInput);
    }

    dmxCheckCursor();

    for (i = 0; i < dmxNumInputs; i++) {
        DMXInputInfo *dmxInput = &dmxInputs[i];

        if (!dmxInput->detached)
            dmxInputLateReInit(dmxInput);
    }
}

/** Add an input with the specified attributes.  If the input is added,
 * the physical id is returned in \a deviceId. */
int
dmxAddInput(DMXInputAttributesPtr attr, int *id)
{
    int retcode = BadValue;

    if (attr->inputType == 1)   /* console */
        retcode = dmxInputAttachConsole(attr->name, attr->sendsCore, id);
    else if (attr->inputType == 2)      /* backend */
        retcode = dmxInputAttachBackend(attr->physicalScreen,
                                        attr->sendsCore, id);

    if (retcode == Success) {
        /* Adjust the cursor boundaries */
        dmxAdjustCursorBoundaries();

        /* Force completion of the changes */
        dmxSync(NULL, TRUE);
    }

    return retcode;
}

/** Remove the input with physical id \a id. */
int
dmxRemoveInput(int id)
{
    return dmxInputDetachId(id);
}

/** Return the value of #dmxNumScreens -- the total number of backend
 * screens in use (these are logical screens and may be larger than the
 * number of backend displays). */
unsigned long
dmxGetNumScreens(void)
{
    return dmxNumScreens;
}

/** Make sure that #dmxCreateAndRealizeWindow has been called for \a
 * pWindow. */
void
dmxForceWindowCreation(WindowPtr pWindow)
{
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWindow);

    if (!pWinPriv->window)
        dmxCreateAndRealizeWindow(pWindow, TRUE);
}

/** Flush pending syncs for all screens. */
void
dmxFlushPendingSyncs(void)
{
    dmxSync(NULL, TRUE);
}

/** Update DMX's screen resources to match those of the newly moved
 *  and/or resized "root" window. */
void
dmxUpdateScreenResources(ScreenPtr pScreen, int x, int y, int w, int h)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    WindowPtr pRoot = pScreen->root;
    WindowPtr pChild;
    Bool anyMarked = FALSE;

    /* Handle special case where width and/or height are zero */
    if (w == 0 || h == 0) {
        w = 1;
        h = 1;
    }

    /* Change screen size */
    pScreen->width = w;
    pScreen->height = h;

    /* Reset the root window's drawable's size */
    pRoot->drawable.width = w;
    pRoot->drawable.height = h;

    /* Set the root window's new winSize and borderSize */
    pRoot->winSize.extents.x1 = 0;
    pRoot->winSize.extents.y1 = 0;
    pRoot->winSize.extents.x2 = w;
    pRoot->winSize.extents.y2 = h;

    pRoot->borderSize.extents.x1 = 0;
    pRoot->borderSize.extents.y1 = 0;
    pRoot->borderSize.extents.x2 = w;
    pRoot->borderSize.extents.y2 = h;

    /* Recompute this screen's mmWidth & mmHeight */
    pScreen->mmWidth =
        (w * 254 + dmxScreen->beXDPI * 5) / (dmxScreen->beXDPI * 10);
    pScreen->mmHeight =
        (h * 254 + dmxScreen->beYDPI * 5) / (dmxScreen->beYDPI * 10);

    /* Recompute this screen's window's clip rects as follows: */
    /*   1. Mark all of root's children's windows */
    for (pChild = pRoot->firstChild; pChild; pChild = pChild->nextSib)
        anyMarked |= pScreen->MarkOverlappedWindows(pChild, pChild,
                                                    (WindowPtr *) NULL);

    /*   2. Set the root window's borderClip */
    pRoot->borderClip.extents.x1 = 0;
    pRoot->borderClip.extents.y1 = 0;
    pRoot->borderClip.extents.x2 = w;
    pRoot->borderClip.extents.y2 = h;

    /*   3. Set the root window's clipList */
    if (anyMarked) {
        /* If any windows have been marked, set the root window's
         * clipList to be broken since it will be recalculated in
         * ValidateTree()
         */
        RegionBreak(&pRoot->clipList);
    }
    else {
        /* Otherwise, we just set it directly since there are no
         * windows visible on this screen
         */
        pRoot->clipList.extents.x1 = 0;
        pRoot->clipList.extents.y1 = 0;
        pRoot->clipList.extents.x2 = w;
        pRoot->clipList.extents.y2 = h;
    }

    /*   4. Revalidate all clip rects and generate expose events */
    if (anyMarked) {
        pScreen->ValidateTree(pRoot, NULL, VTBroken);
        pScreen->HandleExposures(pRoot);
        if (pScreen->PostValidateTree)
            pScreen->PostValidateTree(pRoot, NULL, VTBroken);
    }
}

#ifdef PANORAMIX
#include "panoramiXsrv.h"

/** Change the "screen" window attributes by resizing the actual window
 *  on the back-end display (if necessary). */
static void
dmxConfigureScreenWindow(int idx, int x, int y, int w, int h)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[idx];
    ScreenPtr pScreen = screenInfo.screens[idx];

    /* Resize "screen" window */
    if (dmxScreen->scrnX != x ||
        dmxScreen->scrnY != y ||
        dmxScreen->scrnWidth != w || dmxScreen->scrnHeight != h) {
        dmxResizeScreenWindow(pScreen, x, y, w, h);
    }

    /* Change "screen" window values */
    dmxScreen->scrnX = x;
    dmxScreen->scrnY = y;
    dmxScreen->scrnWidth = w;
    dmxScreen->scrnHeight = h;
}

/** Change the "root" window position and size by resizing the actual
 *  window on the back-end display (if necessary) and updating all of
 *  DMX's resources by calling #dmxUpdateScreenResources. */
static void
dmxConfigureRootWindow(int idx, int x, int y, int w, int h)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[idx];
    WindowPtr pRoot = screenInfo.screens[idx]->root;

    /* NOTE: Either this function or the ones that it calls must handle
     * the case where w == 0 || h == 0.  Currently, the functions that
     * this one calls handle that case. */

    /* 1. Resize "root" window */
    if (dmxScreen->rootX != x ||
        dmxScreen->rootY != y ||
        dmxScreen->rootWidth != w || dmxScreen->rootHeight != h) {
        dmxResizeRootWindow(pRoot, x, y, w, h);
    }

    /* 2. Update all of the screen's resources associated with this root
     *    window */
    if (dmxScreen->rootWidth != w || dmxScreen->rootHeight != h) {
        dmxUpdateScreenResources(screenInfo.screens[idx], x, y, w, h);
    }

    /* Change "root" window values */
    dmxScreen->rootX = x;
    dmxScreen->rootY = y;
    dmxScreen->rootWidth = w;
    dmxScreen->rootHeight = h;
}

/** Change the "root" window's origin by updating DMX's internal data
 *  structures (dix and Xinerama) to use the new origin and adjust the
 *  positions of windows that overlap this "root" window. */
static void
dmxSetRootWindowOrigin(int idx, int x, int y)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[idx];
    ScreenPtr pScreen = screenInfo.screens[idx];
    WindowPtr pRoot = pScreen->root;
    WindowPtr pChild;
    int xoff;
    int yoff;

    /* Change "root" window's origin */
    dmxScreen->rootXOrigin = x;
    dmxScreen->rootYOrigin = y;

    /* Compute offsets here in case <x,y> has been changed above */
    xoff = x - pScreen->x;
    yoff = y - pScreen->y;

    /* Adjust the root window's position */
    pScreen->x = dmxScreen->rootXOrigin;
    pScreen->y = dmxScreen->rootYOrigin;

    /* Recalculate the Xinerama regions and data structs */
    XineramaReinitData();

    /* Adjust each of the root window's children */
    if (!idx)
        ReinitializeRootWindow(screenInfo.screens[0]->root, xoff, yoff);
    pChild = pRoot->firstChild;
    while (pChild) {
        /* Adjust child window's position */
        pScreen->MoveWindow(pChild,
                            pChild->origin.x - wBorderWidth(pChild) - xoff,
                            pChild->origin.y - wBorderWidth(pChild) - yoff,
                            pChild->nextSib, VTMove);

        /* Note that the call to MoveWindow will eventually call
         * dmxPositionWindow which will automatically create a
         * window if it is now exposed on screen (for lazy window
         * creation optimization) and it will properly set the
         * offscreen flag.
         */

        pChild = pChild->nextSib;
    }
}

/** Configure the attributes of each "screen" and "root" window. */
int
dmxConfigureScreenWindows(int nscreens,
                          CARD32 *screens,
                          DMXScreenAttributesPtr attribs, int *errorScreen)
{
    int i;

    for (i = 0; i < nscreens; i++) {
        DMXScreenAttributesPtr attr = &attribs[i];
        int idx = screens[i];
        DMXScreenInfo *dmxScreen = &dmxScreens[idx];

        if (errorScreen)
            *errorScreen = i;

        if (!dmxScreen->beDisplay)
            return DMX_BAD_VALUE;

        /* Check for illegal values */
        if (idx < 0 || idx >= dmxNumScreens)
            return BadValue;

        /* The "screen" and "root" windows must have valid sizes */
        if (attr->screenWindowWidth <= 0 || attr->screenWindowHeight <= 0 ||
            attr->rootWindowWidth < 0 || attr->rootWindowHeight < 0)
            return DMX_BAD_VALUE;

        /* The "screen" window must fit entirely within the BE display */
        if (attr->screenWindowXoffset < 0 ||
            attr->screenWindowYoffset < 0 ||
            attr->screenWindowXoffset
            + attr->screenWindowWidth > (unsigned) dmxScreen->beWidth ||
            attr->screenWindowYoffset
            + attr->screenWindowHeight > (unsigned) dmxScreen->beHeight)
            return DMX_BAD_VALUE;

        /* The "root" window must fit entirely within the "screen" window */
        if (attr->rootWindowXoffset < 0 ||
            attr->rootWindowYoffset < 0 ||
            attr->rootWindowXoffset
            + attr->rootWindowWidth > attr->screenWindowWidth ||
            attr->rootWindowYoffset
            + attr->rootWindowHeight > attr->screenWindowHeight)
            return DMX_BAD_VALUE;

        /* The "root" window must not expose unaddressable coordinates */
        if (attr->rootWindowXorigin < 0 ||
            attr->rootWindowYorigin < 0 ||
            attr->rootWindowXorigin + attr->rootWindowWidth > 32767 ||
            attr->rootWindowYorigin + attr->rootWindowHeight > 32767)
            return DMX_BAD_VALUE;

        /* The "root" window must fit within the global bounding box */
        if (attr->rootWindowXorigin
            + attr->rootWindowWidth > (unsigned) dmxGlobalWidth ||
            attr->rootWindowYorigin
            + attr->rootWindowHeight > (unsigned) dmxGlobalHeight)
            return DMX_BAD_VALUE;

        /* FIXME: Handle the rest of the illegal value checking */
    }

    /* No illegal values found */
    if (errorScreen)
        *errorScreen = 0;

    for (i = 0; i < nscreens; i++) {
        DMXScreenAttributesPtr attr = &attribs[i];
        int idx = screens[i];
        DMXScreenInfo *dmxScreen = &dmxScreens[idx];

        dmxLog(dmxInfo, "Changing screen #%d attributes "
               "from %dx%d+%d+%d %dx%d+%d+%d +%d+%d "
               "to %dx%d+%d+%d %dx%d+%d+%d +%d+%d\n",
               idx,
               dmxScreen->scrnWidth, dmxScreen->scrnHeight,
               dmxScreen->scrnX, dmxScreen->scrnY,
               dmxScreen->rootWidth, dmxScreen->rootHeight,
               dmxScreen->rootX, dmxScreen->rootY,
               dmxScreen->rootXOrigin, dmxScreen->rootYOrigin,
               attr->screenWindowWidth, attr->screenWindowHeight,
               attr->screenWindowXoffset, attr->screenWindowYoffset,
               attr->rootWindowWidth, attr->rootWindowHeight,
               attr->rootWindowXoffset, attr->rootWindowYoffset,
               attr->rootWindowXorigin, attr->rootWindowYorigin);

        /* Configure "screen" window */
        dmxConfigureScreenWindow(idx,
                                 attr->screenWindowXoffset,
                                 attr->screenWindowYoffset,
                                 attr->screenWindowWidth,
                                 attr->screenWindowHeight);

        /* Configure "root" window */
        dmxConfigureRootWindow(idx,
                               attr->rootWindowXoffset,
                               attr->rootWindowYoffset,
                               attr->rootWindowWidth, attr->rootWindowHeight);

        /* Set "root" window's origin */
        dmxSetRootWindowOrigin(idx,
                               attr->rootWindowXorigin,
                               attr->rootWindowYorigin);
    }

    /* Adjust the cursor boundaries */
    dmxAdjustCursorBoundaries();

    /* Force completion of the changes */
    dmxSync(NULL, TRUE);

    return Success;
}

/** Configure the attributes of the global desktop. */
int
dmxConfigureDesktop(DMXDesktopAttributesPtr attribs)
{
    if (attribs->width <= 0 || attribs->width >= 32767 ||
        attribs->height <= 0 || attribs->height >= 32767)
        return DMX_BAD_VALUE;

    /* If the desktop is shrinking, adjust the "root" windows on each
     * "screen" window to only show the visible desktop.  Also, handle
     * the special case where the desktop shrinks such that the it no
     * longer overlaps an portion of a "screen" window. */
    if (attribs->width < dmxGlobalWidth || attribs->height < dmxGlobalHeight) {
        int i;

        for (i = 0; i < dmxNumScreens; i++) {
            DMXScreenInfo *dmxScreen = &dmxScreens[i];

            if (dmxScreen->rootXOrigin
                + dmxScreen->rootWidth > attribs->width ||
                dmxScreen->rootYOrigin
                + dmxScreen->rootHeight > attribs->height) {
                int w, h;

                if ((w = attribs->width - dmxScreen->rootXOrigin) < 0)
                    w = 0;
                if ((h = attribs->height - dmxScreen->rootYOrigin) < 0)
                    h = 0;
                if (w > dmxScreen->scrnWidth)
                    w = dmxScreen->scrnWidth;
                if (h > dmxScreen->scrnHeight)
                    h = dmxScreen->scrnHeight;
                if (w > dmxScreen->rootWidth)
                    w = dmxScreen->rootWidth;
                if (h > dmxScreen->rootHeight)
                    h = dmxScreen->rootHeight;
                dmxConfigureRootWindow(i,
                                       dmxScreen->rootX,
                                       dmxScreen->rootY, w, h);
            }
        }
    }

    /* Set the global width/height */
    dmxSetWidthHeight(attribs->width, attribs->height);

    /* Handle shift[XY] changes */
    if (attribs->shiftX || attribs->shiftY) {
        int i;

        for (i = 0; i < dmxNumScreens; i++) {
            ScreenPtr pScreen = screenInfo.screens[i];
            WindowPtr pChild = pScreen->root->firstChild;

            while (pChild) {
                /* Adjust child window's position */
                pScreen->MoveWindow(pChild,
                                    pChild->origin.x - wBorderWidth(pChild)
                                    - attribs->shiftX,
                                    pChild->origin.y - wBorderWidth(pChild)
                                    - attribs->shiftY, pChild->nextSib, VTMove);

                /* Note that the call to MoveWindow will eventually call
                 * dmxPositionWindow which will automatically create a
                 * window if it is now exposed on screen (for lazy
                 * window creation optimization) and it will properly
                 * set the offscreen flag.
                 */

                pChild = pChild->nextSib;
            }
        }
    }

    /* Update connection block, Xinerama, etc. -- these appears to
     * already be handled in dmxConnectionBlockCallback(), which is
     * called from dmxAdjustCursorBoundaries() [below]. */

    /* Adjust the cursor boundaries */
    dmxAdjustCursorBoundaries();

    /* Force completion of the changes */
    dmxSync(NULL, TRUE);

    return Success;
}
#endif

/** Create the scratch GCs per depth. */
static void
dmxBECreateScratchGCs(int scrnNum)
{
    ScreenPtr pScreen = screenInfo.screens[scrnNum];
    GCPtr *ppGC = pScreen->GCperDepth;
    int i;

    for (i = 0; i <= pScreen->numDepths; i++)
        dmxBECreateGC(pScreen, ppGC[i]);
}

#ifdef PANORAMIX
static Bool FoundPixImage;

/** Search the Xinerama XRT_PIXMAP resources for the pixmap that needs
 *  to have its image restored.  When it is found, see if there is
 *  another screen with the same image.  If so, copy the pixmap image
 *  from the existing screen to the newly created pixmap. */
static void
dmxBERestorePixmapImage(void *value, XID id, RESTYPE type, void *p)
{
    if ((type & TypeMask) == (XRT_PIXMAP & TypeMask)) {
        PixmapPtr pDst = (PixmapPtr) p;
        int idx = pDst->drawable.pScreen->myNum;
        PanoramiXRes *pXinPix = (PanoramiXRes *) value;
        PixmapPtr pPix;
        int i;

        dixLookupResourceByType((void **) &pPix, pXinPix->info[idx].id,
                                RT_PIXMAP, NullClient, DixUnknownAccess);
        if (pPix != pDst)
            return;             /* Not a match.... Next! */

        FOR_NSCREENS(i) {
            PixmapPtr pSrc;
            dmxPixPrivPtr pSrcPriv = NULL;

            if (i == idx)
                continue;       /* Self replication is bad */

            dixLookupResourceByType((void **) &pSrc, pXinPix->info[i].id,
                                    RT_PIXMAP, NullClient, DixUnknownAccess);
            pSrcPriv = DMX_GET_PIXMAP_PRIV(pSrc);
            if (pSrcPriv->pixmap) {
                DMXScreenInfo *dmxSrcScreen = &dmxScreens[i];
                DMXScreenInfo *dmxDstScreen = &dmxScreens[idx];
                dmxPixPrivPtr pDstPriv = DMX_GET_PIXMAP_PRIV(pDst);
                XImage *img;
                int j;
                XlibGC gc = NULL;

                /* This should never happen, but just in case.... */
                if (pSrc->drawable.width != pDst->drawable.width ||
                    pSrc->drawable.height != pDst->drawable.height)
                    return;

                /* Copy from src pixmap to dst pixmap */
                img = XGetImage(dmxSrcScreen->beDisplay,
                                pSrcPriv->pixmap,
                                0, 0,
                                pSrc->drawable.width, pSrc->drawable.height,
                                -1, ZPixmap);

                for (j = 0; j < dmxDstScreen->beNumPixmapFormats; j++) {
                    if (dmxDstScreen->bePixmapFormats[j].depth == img->depth) {
                        unsigned long m;
                        XGCValues v;

                        m = GCFunction | GCPlaneMask | GCClipMask;
                        v.function = GXcopy;
                        v.plane_mask = AllPlanes;
                        v.clip_mask = None;

                        gc = XCreateGC(dmxDstScreen->beDisplay,
                                       dmxDstScreen->scrnDefDrawables[j],
                                       m, &v);
                        break;
                    }
                }

                if (gc) {
                    XPutImage(dmxDstScreen->beDisplay,
                              pDstPriv->pixmap,
                              gc, img, 0, 0, 0, 0,
                              pDst->drawable.width, pDst->drawable.height);
                    XFreeGC(dmxDstScreen->beDisplay, gc);
                    FoundPixImage = True;
                }
                else {
                    dmxLog(dmxWarning, "Could not create GC\n");
                }

                XDestroyImage(img);
                return;
            }
        }
    }
}
#endif

/** Restore the pixmap image either from another screen or from an image
 *  that was saved when the screen was previously detached. */
static void
dmxBERestorePixmap(PixmapPtr pPixmap)
{
#ifdef PANORAMIX
    int i;

    /* If Xinerama is not active, there's nothing we can do (see comment
     * in #else below for more info). */
    if (noPanoramiXExtension) {
        dmxLog(dmxWarning, "Cannot restore pixmap image\n");
        return;
    }

    FoundPixImage = False;
    for (i = currentMaxClients; --i >= 0;)
        if (clients[i])
            FindAllClientResources(clients[i], dmxBERestorePixmapImage,
                                   (void *) pPixmap);

    /* No corresponding pixmap image was found on other screens, so we
     * need to copy it from the saved image when the screen was detached
     * (if available). */
    if (!FoundPixImage) {
        dmxPixPrivPtr pPixPriv = DMX_GET_PIXMAP_PRIV(pPixmap);

        if (pPixPriv->detachedImage) {
            ScreenPtr pScreen = pPixmap->drawable.pScreen;
            DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
            XlibGC gc = NULL;

            for (i = 0; i < dmxScreen->beNumPixmapFormats; i++) {
                if (dmxScreen->bePixmapFormats[i].depth ==
                    pPixPriv->detachedImage->depth) {
                    unsigned long m;
                    XGCValues v;

                    m = GCFunction | GCPlaneMask | GCClipMask;
                    v.function = GXcopy;
                    v.plane_mask = AllPlanes;
                    v.clip_mask = None;

                    gc = XCreateGC(dmxScreen->beDisplay,
                                   dmxScreen->scrnDefDrawables[i], m, &v);
                    break;
                }
            }

            if (gc) {
                XPutImage(dmxScreen->beDisplay,
                          pPixPriv->pixmap,
                          gc,
                          pPixPriv->detachedImage,
                          0, 0, 0, 0,
                          pPixmap->drawable.width, pPixmap->drawable.height);
                XFreeGC(dmxScreen->beDisplay, gc);
            }
            else {
                dmxLog(dmxWarning, "Cannot restore pixmap image\n");
            }

            XDestroyImage(pPixPriv->detachedImage);
            pPixPriv->detachedImage = NULL;
        }
        else {
            dmxLog(dmxWarning, "Cannot restore pixmap image\n");
        }
    }
#else
    /* If Xinerama is not enabled, then there is no other copy of the
     * pixmap image that we can restore.  Saving all pixmap data is not
     * a feasible option since there is no mechanism for updating pixmap
     * data when a screen is detached, which means that the data that
     * was previously saved would most likely be out of date. */
    dmxLog(dmxWarning, "Cannot restore pixmap image\n");
    return;
#endif
}

/** Create resources on the back-end server.  This function is called
 *  from #dmxAttachScreen() via the dix layer's FindAllResources
 *  function.  It walks all resources, compares them to the screen
 *  number passed in as \a n and calls the appropriate DMX function to
 *  create the associated resource on the back-end server. */
static void
dmxBECreateResources(void *value, XID id, RESTYPE type, void *n)
{
    int scrnNum = (uintptr_t) n;
    ScreenPtr pScreen = screenInfo.screens[scrnNum];

    if ((type & TypeMask) == (RT_WINDOW & TypeMask)) {
        /* Window resources are created below in dmxBECreateWindowTree */
    }
    else if ((type & TypeMask) == (RT_PIXMAP & TypeMask)) {
        PixmapPtr pPix = value;

        if (pPix->drawable.pScreen->myNum == scrnNum) {
            dmxBECreatePixmap(pPix);
            dmxBERestorePixmap(pPix);
        }
    }
    else if ((type & TypeMask) == (RT_GC & TypeMask)) {
        GCPtr pGC = value;

        if (pGC->pScreen->myNum == scrnNum) {
            /* Create the GC on the back-end server */
            dmxBECreateGC(pScreen, pGC);
            /* Create any pixmaps associated with this GC */
            if (!pGC->tileIsPixel) {
                dmxBECreatePixmap(pGC->tile.pixmap);
                dmxBERestorePixmap(pGC->tile.pixmap);
            }
            if (pGC->stipple != pScreen->PixmapPerDepth[0]) {
                dmxBECreatePixmap(pGC->stipple);
                dmxBERestorePixmap(pGC->stipple);
            }
            if (pGC->font != defaultFont) {
                (void) dmxBELoadFont(pScreen, pGC->font);
            }
            /* Update the GC on the back-end server */
            dmxChangeGC(pGC, -1L);
        }
    }
    else if ((type & TypeMask) == (RT_FONT & TypeMask)) {
        (void) dmxBELoadFont(pScreen, (FontPtr) value);
    }
    else if ((type & TypeMask) == (RT_CURSOR & TypeMask)) {
        dmxBECreateCursor(pScreen, (CursorPtr) value);
    }
    else if ((type & TypeMask) == (RT_COLORMAP & TypeMask)) {
        ColormapPtr pCmap = value;

        if (pCmap->pScreen->myNum == scrnNum)
            (void) dmxBECreateColormap((ColormapPtr) value);
#if 0
        /* TODO: Recreate Picture and GlyphSet resources */
    }
    else if ((type & TypeMask) == (PictureType & TypeMask)) {
        /* Picture resources are created when windows are created */
    }
    else if ((type & TypeMask) == (GlyphSetType & TypeMask)) {
        dmxBEFreeGlyphSet(pScreen, (GlyphSetPtr) value);
#endif
    }
    else {
        /* Other resource types??? */
    }
}

/** Create window hierachy on back-end server.  The window tree is
 *  created in a special order (bottom most subwindow first) so that the
 *  #dmxCreateNonRootWindow() function does not need to recursively call
 *  itself to create each window's parents.  This is required so that we
 *  have the opportunity to create each window's border and background
 *  pixmaps (where appropriate) before the window is created. */
static void
dmxBECreateWindowTree(int idx)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[idx];
    WindowPtr pRoot = screenInfo.screens[idx]->root;
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pRoot);
    WindowPtr pWin;

    /* Create the pixmaps associated with the root window */
    if (!pRoot->borderIsPixel) {
        dmxBECreatePixmap(pRoot->border.pixmap);
        dmxBERestorePixmap(pRoot->border.pixmap);
    }
    if (pRoot->backgroundState == BackgroundPixmap) {
        dmxBECreatePixmap(pRoot->background.pixmap);
        dmxBERestorePixmap(pRoot->background.pixmap);
    }

    /* Create root window first */
    dmxScreen->rootWin = pWinPriv->window = dmxCreateRootWindow(pRoot);
    XMapWindow(dmxScreen->beDisplay, dmxScreen->rootWin);

    pWin = pRoot->lastChild;
    while (pWin) {
        pWinPriv = DMX_GET_WINDOW_PRIV(pWin);

        /* Create the pixmaps regardless of whether or not the
         * window is created or not due to lazy window creation.
         */
        if (!pWin->borderIsPixel) {
            dmxBECreatePixmap(pWin->border.pixmap);
            dmxBERestorePixmap(pWin->border.pixmap);
        }
        if (pWin->backgroundState == BackgroundPixmap) {
            dmxBECreatePixmap(pWin->background.pixmap);
            dmxBERestorePixmap(pWin->background.pixmap);
        }

        /* Reset the window attributes */
        dmxGetDefaultWindowAttributes(pWin, &pWinPriv->cmap, &pWinPriv->visual);

        /* Create the window */
        if (pWinPriv->mapped && !pWinPriv->offscreen)
            dmxCreateAndRealizeWindow(pWin, TRUE);

        /* Next, create the bottom-most child */
        if (pWin->lastChild) {
            pWin = pWin->lastChild;
            continue;
        }

        /* If the window has no children, move on to the next higher window */
        while (!pWin->prevSib && (pWin != pRoot))
            pWin = pWin->parent;

        if (pWin->prevSib) {
            pWin = pWin->prevSib;
            continue;
        }

        /* When we reach the root window, we are finished */
        if (pWin == pRoot)
            break;
    }
}

/* Refresh screen by generating exposure events for all windows */
static void
dmxForceExposures(int idx)
{
    ScreenPtr pScreen = screenInfo.screens[idx];
    WindowPtr pRoot = pScreen->root;
    Bool anyMarked = FALSE;
    WindowPtr pChild;

    for (pChild = pRoot->firstChild; pChild; pChild = pChild->nextSib)
        anyMarked |= pScreen->MarkOverlappedWindows(pChild, pChild,
                                                    (WindowPtr *) NULL);
    if (anyMarked) {
        /* If any windows have been marked, set the root window's
         * clipList to be broken since it will be recalculated in
         * ValidateTree()
         */
        RegionBreak(&pRoot->clipList);
        pScreen->ValidateTree(pRoot, NULL, VTBroken);
        pScreen->HandleExposures(pRoot);
        if (pScreen->PostValidateTree)
            pScreen->PostValidateTree(pRoot, NULL, VTBroken);
    }
}

/** Compare the new and old screens to see if they are compatible. */
static Bool
dmxCompareScreens(DMXScreenInfo * new, DMXScreenInfo * old)
{
    int i;

    if (new->beWidth != old->beWidth)
        return FALSE;
    if (new->beHeight != old->beHeight)
        return FALSE;
    if (new->beDepth != old->beDepth)
        return FALSE;
    if (new->beBPP != old->beBPP)
        return FALSE;

    if (new->beNumDepths != old->beNumDepths)
        return FALSE;
    for (i = 0; i < old->beNumDepths; i++)
        if (new->beDepths[i] != old->beDepths[i])
            return FALSE;

    if (new->beNumPixmapFormats != old->beNumPixmapFormats)
        return FALSE;
    for (i = 0; i < old->beNumPixmapFormats; i++) {
        if (new->bePixmapFormats[i].depth != old->bePixmapFormats[i].depth)
            return FALSE;
        if (new->bePixmapFormats[i].bits_per_pixel !=
            old->bePixmapFormats[i].bits_per_pixel)
            return FALSE;
        if (new->bePixmapFormats[i].scanline_pad !=
            old->bePixmapFormats[i].scanline_pad)
            return FALSE;
    }

    if (new->beNumVisuals != old->beNumVisuals)
        return FALSE;
    for (i = 0; i < old->beNumVisuals; i++) {
        if (new->beVisuals[i].visualid != old->beVisuals[i].visualid)
            return FALSE;
        if (new->beVisuals[i].screen != old->beVisuals[i].screen)
            return FALSE;
        if (new->beVisuals[i].depth != old->beVisuals[i].depth)
            return FALSE;
        if (new->beVisuals[i].class != old->beVisuals[i].class)
            return FALSE;
        if (new->beVisuals[i].red_mask != old->beVisuals[i].red_mask)
            return FALSE;
        if (new->beVisuals[i].green_mask != old->beVisuals[i].green_mask)
            return FALSE;
        if (new->beVisuals[i].blue_mask != old->beVisuals[i].blue_mask)
            return FALSE;
        if (new->beVisuals[i].colormap_size != old->beVisuals[i].colormap_size)
            return FALSE;
        if (new->beVisuals[i].bits_per_rgb != old->beVisuals[i].bits_per_rgb)
            return FALSE;
    }

    if (new->beDefVisualIndex != old->beDefVisualIndex)
        return FALSE;

    return TRUE;
}

/** Restore Render's picture */
static void
dmxBERestoreRenderPict(void *value, XID id, void *n)
{
    PicturePtr pPicture = value;        /* The picture */
    DrawablePtr pDraw = pPicture->pDrawable;    /* The picture's drawable */
    int scrnNum = (uintptr_t) n;

    if (pDraw->pScreen->myNum != scrnNum) {
        /* Picture not on the screen we are restoring */
        return;
    }

    if (pDraw->type == DRAWABLE_PIXMAP) {
        PixmapPtr pPixmap = (PixmapPtr) pDraw;

        /* Create and restore the pixmap drawable */
        dmxBECreatePixmap(pPixmap);
        dmxBERestorePixmap(pPixmap);
    }

    dmxBECreatePicture(pPicture);
}

/** Restore Render's glyphs */
static void
dmxBERestoreRenderGlyph(void *value, XID id, void *n)
{
    GlyphSetPtr glyphSet = value;
    int scrnNum = (uintptr_t) n;
    dmxGlyphPrivPtr glyphPriv = DMX_GET_GLYPH_PRIV(glyphSet);
    DMXScreenInfo *dmxScreen = &dmxScreens[scrnNum];
    GlyphRefPtr table;
    char *images;
    Glyph *gids;
    XGlyphInfo *glyphs;
    char *pos;
    int beret;
    int len_images = 0;
    int i;
    int ctr;

    if (glyphPriv->glyphSets[scrnNum]) {
        /* Only restore glyphs on the screen we are attaching */
        return;
    }

    /* First we must create the glyph set on the backend. */
    if ((beret = dmxBECreateGlyphSet(scrnNum, glyphSet)) != Success) {
        dmxLog(dmxWarning,
               "\tdmxBERestoreRenderGlyph failed to create glyphset!\n");
        return;
    }

    /* Now for the complex part, restore the glyph data */
    table = glyphSet->hash.table;

    /* We need to know how much memory to allocate for this part */
    for (i = 0; i < glyphSet->hash.hashSet->size; i++) {
        GlyphRefPtr gr = &table[i];
        GlyphPtr gl = gr->glyph;

        if (!gl || gl == DeletedGlyph)
            continue;
        len_images += gl->size - sizeof(gl->info);
    }

    /* Now allocate the memory we need */
    images = calloc(len_images, sizeof(char));
    gids = xallocarray(glyphSet->hash.tableEntries, sizeof(Glyph));
    glyphs = xallocarray(glyphSet->hash.tableEntries, sizeof(XGlyphInfo));

    pos = images;
    ctr = 0;

    /* Fill the allocated memory with the proper data */
    for (i = 0; i < glyphSet->hash.hashSet->size; i++) {
        GlyphRefPtr gr = &table[i];
        GlyphPtr gl = gr->glyph;

        if (!gl || gl == DeletedGlyph)
            continue;

        /* First lets put the data into gids */
        gids[ctr] = gr->signature;

        /* Next do the glyphs data structures */
        glyphs[ctr].width = gl->info.width;
        glyphs[ctr].height = gl->info.height;
        glyphs[ctr].x = gl->info.x;
        glyphs[ctr].y = gl->info.y;
        glyphs[ctr].xOff = gl->info.xOff;
        glyphs[ctr].yOff = gl->info.yOff;

        /* Copy the images from the DIX's data into the buffer */
        memcpy(pos, gl + 1, gl->size - sizeof(gl->info));
        pos += gl->size - sizeof(gl->info);
        ctr++;
    }

    /* Now restore the glyph data */
    XRenderAddGlyphs(dmxScreen->beDisplay, glyphPriv->glyphSets[scrnNum],
                     gids, glyphs, glyphSet->hash.tableEntries, images,
                     len_images);

    /* Clean up */
    free(images);
    free(gids);
    free(glyphs);
}

/** Reattach previously detached back-end screen. */
int
dmxAttachScreen(int idx, DMXScreenAttributesPtr attr)
{
    ScreenPtr pScreen;
    DMXScreenInfo *dmxScreen;
    CARD32 scrnNum = idx;
    DMXScreenInfo oldDMXScreen;
    int i;

    /* Return failure if dynamic addition/removal of screens is disabled */
    if (!dmxAddRemoveScreens) {
        dmxLog(dmxWarning,
               "Attempting to add a screen, but the AddRemoveScreen\n");
        dmxLog(dmxWarning,
               "extension has not been enabled.  To enable this extension\n");
        dmxLog(dmxWarning,
               "add the \"-addremovescreens\" option either to the command\n");
        dmxLog(dmxWarning, "line or in the configuration file.\n");
        return 1;
    }

    /* Cannot add a screen that does not exist */
    if (idx < 0 || idx >= dmxNumScreens)
        return 1;
    pScreen = screenInfo.screens[idx];
    dmxScreen = &dmxScreens[idx];

    /* Cannot attach to a screen that is already opened */
    if (dmxScreen->beDisplay) {
        dmxLog(dmxWarning,
               "Attempting to add screen #%d but a screen already exists\n",
               idx);
        return 1;
    }

    dmxLogOutput(dmxScreen, "Attaching screen #%d\n", idx);

    /* Save old info */
    oldDMXScreen = *dmxScreen;

    /* Copy the name to the new screen */
    dmxScreen->name = strdup(attr->displayName);

    /* Open display and get all of the screen info */
    if (!dmxOpenDisplay(dmxScreen)) {
        dmxLog(dmxWarning,
               "dmxOpenDisplay: Unable to open display %s\n", dmxScreen->name);

        /* Restore the old screen */
        *dmxScreen = oldDMXScreen;
        return 1;
    }

    dmxSetErrorHandler(dmxScreen);
    dmxCheckForWM(dmxScreen);
    dmxGetScreenAttribs(dmxScreen);

    if (!dmxGetVisualInfo(dmxScreen)) {
        dmxLog(dmxWarning, "dmxGetVisualInfo: No matching visuals found\n");
        XFree(dmxScreen->beVisuals);
        XCloseDisplay(dmxScreen->beDisplay);

        /* Restore the old screen */
        *dmxScreen = oldDMXScreen;
        return 1;
    }

    dmxGetColormaps(dmxScreen);
    dmxGetPixmapFormats(dmxScreen);

    /* Verify that the screen to be added has the same info as the
     * previously added screen. */
    if (!dmxCompareScreens(dmxScreen, &oldDMXScreen)) {
        dmxLog(dmxWarning,
               "New screen data (%s) does not match previously\n",
               dmxScreen->name);
        dmxLog(dmxWarning, "attached screen data (%s)\n", oldDMXScreen.name);
        dmxLog(dmxWarning,
               "All data must match in order to attach to screen #%d\n", idx);
        XFree(dmxScreen->beVisuals);
        XFree(dmxScreen->beDepths);
        XFree(dmxScreen->bePixmapFormats);
        XCloseDisplay(dmxScreen->beDisplay);

        /* Restore the old screen */
        *dmxScreen = oldDMXScreen;
        return 1;
    }

    /* Initialize the BE screen resources */
    dmxBEScreenInit(screenInfo.screens[idx]);

    /* TODO: Handle GLX visual initialization.  GLXProxy needs to be
     * updated to handle dynamic addition/removal of screens. */

    /* Create default stipple */
    dmxBECreatePixmap(pScreen->PixmapPerDepth[0]);
    dmxBERestorePixmap(pScreen->PixmapPerDepth[0]);

    /* Create the scratch GCs */
    dmxBECreateScratchGCs(idx);

    /* Create the default font */
    (void) dmxBELoadFont(pScreen, defaultFont);

    /* Create all resources that don't depend on windows */
    for (i = currentMaxClients; --i >= 0;)
        if (clients[i])
            FindAllClientResources(clients[i], dmxBECreateResources,
                                   (void *) (uintptr_t) idx);

    /* Create window hierarchy (top down) */
    dmxBECreateWindowTree(idx);

    /* Restore the picture state for RENDER */
    for (i = currentMaxClients; --i >= 0;)
        if (clients[i])
            FindClientResourcesByType(clients[i], PictureType,
                                      dmxBERestoreRenderPict,
                                      (void *) (uintptr_t) idx);

    /* Restore the glyph state for RENDER */
    for (i = currentMaxClients; --i >= 0;)
        if (clients[i])
            FindClientResourcesByType(clients[i], GlyphSetType,
                                      dmxBERestoreRenderGlyph,
                                      (void *) (uintptr_t) idx);

    /* Refresh screen by generating exposure events for all windows */
    dmxForceExposures(idx);

    dmxSync(&dmxScreens[idx], TRUE);

    /* We used these to compare the old and new screens.  They are no
     * longer needed since we have a newly attached screen, so we can
     * now free the old screen's resources. */
    XFree(oldDMXScreen.beVisuals);
    XFree(oldDMXScreen.beDepths);
    XFree(oldDMXScreen.bePixmapFormats);
    /* TODO: should oldDMXScreen.name be freed?? */

#ifdef PANORAMIX
    if (!noPanoramiXExtension)
        return dmxConfigureScreenWindows(1, &scrnNum, attr, NULL);
    else
#endif
        return 0;               /* Success */
}

/*
 * Resources that may have state on the BE server and need to be freed:
 *
 * RT_NONE
 * RT_WINDOW
 * RT_PIXMAP
 * RT_GC
 * RT_FONT
 * RT_CURSOR
 * RT_COLORMAP
 * RT_CMAPENTRY
 * RT_OTHERCLIENT
 * RT_PASSIVEGRAB
 * XRT_WINDOW
 * XRT_PIXMAP
 * XRT_GC
 * XRT_COLORMAP
 * XRT_PICTURE
 * PictureType
 * PictFormatType
 * GlyphSetType
 * ClientType
 * EventType
 * RT_INPUTCLIENT
 * XETrapType
 * RTCounter
 * RTAwait
 * RTAlarmClient
 * RT_XKBCLIENT
 * RTContext
 * TagResType
 * StalledResType
 * SecurityAuthorizationResType
 * RTEventClient
 * __glXContextRes
 * __glXClientRes
 * __glXPixmapRes
 * __glXWindowRes
 * __glXPbufferRes
 */

#ifdef PANORAMIX
/** Search the Xinerama XRT_PIXMAP resources for the pixmap that needs
 *  to have its image saved. */
static void
dmxBEFindPixmapImage(void *value, XID id, RESTYPE type, void *p)
{
    if ((type & TypeMask) == (XRT_PIXMAP & TypeMask)) {
        PixmapPtr pDst = (PixmapPtr) p;
        int idx = pDst->drawable.pScreen->myNum;
        PanoramiXRes *pXinPix = (PanoramiXRes *) value;
        PixmapPtr pPix;
        int i;

        dixLookupResourceByType((void **) &pPix, pXinPix->info[idx].id,
                                RT_PIXMAP, NullClient, DixUnknownAccess);
        if (pPix != pDst)
            return;             /* Not a match.... Next! */

        FOR_NSCREENS(i) {
            PixmapPtr pSrc;
            dmxPixPrivPtr pSrcPriv = NULL;

            if (i == idx)
                continue;       /* Self replication is bad */

            dixLookupResourceByType((void **) &pSrc, pXinPix->info[i].id,
                                    RT_PIXMAP, NullClient, DixUnknownAccess);
            pSrcPriv = DMX_GET_PIXMAP_PRIV(pSrc);
            if (pSrcPriv->pixmap) {
                FoundPixImage = True;
                return;
            }
        }
    }
}
#endif

/** Save the pixmap image only when there is not another screen with
 *  that pixmap from which the image can be read when the screen is
 *  reattached.  To do this, we first try to find a pixmap on another
 *  screen corresponding to the one we are trying to save.  If we find
 *  one, then we do not need to save the image data since during
 *  reattachment, the image data can be read from that other pixmap.
 *  However, if we do not find one, then we need to save the image data.
 *  The common case for these are for the default stipple and root
 *  tile. */
static void
dmxBESavePixmap(PixmapPtr pPixmap)
{
#ifdef PANORAMIX
    int i;

    /* If Xinerama is not active, there's nothing we can do (see comment
     * in #else below for more info). */
    if (noPanoramiXExtension)
        return;

    FoundPixImage = False;
    for (i = currentMaxClients; --i >= 0;)
        if (clients[i])
            FindAllClientResources(clients[i], dmxBEFindPixmapImage,
                                   (void *) pPixmap);

    /* Save the image only if there is no other screens that have a
     * pixmap that corresponds to the one we are trying to save. */
    if (!FoundPixImage) {
        dmxPixPrivPtr pPixPriv = DMX_GET_PIXMAP_PRIV(pPixmap);

        if (!pPixPriv->detachedImage) {
            ScreenPtr pScreen = pPixmap->drawable.pScreen;
            DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];

            pPixPriv->detachedImage = XGetImage(dmxScreen->beDisplay,
                                                pPixPriv->pixmap,
                                                0, 0,
                                                pPixmap->drawable.width,
                                                pPixmap->drawable.height,
                                                -1, ZPixmap);
            if (!pPixPriv->detachedImage)
                dmxLog(dmxWarning, "Cannot save pixmap image\n");
        }
    }
#else
    /* NOTE: The only time there is a pixmap on another screen that
     * corresponds to the one we are trying to save is when Xinerama is
     * active.  Otherwise, the pixmap image data is only stored on a
     * single screen, which means that once it is detached, that data is
     * lost.  We could save the data here, but then that would require
     * us to implement the ability for Xdmx to keep the pixmap up to
     * date while the screen is detached, which is beyond the scope of
     * the current project. */
    return;
#endif
}

/** Destroy resources on the back-end server.  This function is called
 *  from #dmxDetachScreen() via the dix layer's FindAllResources
 *  function.  It walks all resources, compares them to the screen
 *  number passed in as \a n and calls the appropriate DMX function to
 *  free the associated resource on the back-end server. */
static void
dmxBEDestroyResources(void *value, XID id, RESTYPE type, void *n)
{
    int scrnNum = (uintptr_t) n;
    ScreenPtr pScreen = screenInfo.screens[scrnNum];

    if ((type & TypeMask) == (RT_WINDOW & TypeMask)) {
        /* Window resources are destroyed below in dmxBEDestroyWindowTree */
    }
    else if ((type & TypeMask) == (RT_PIXMAP & TypeMask)) {
        PixmapPtr pPix = value;

        if (pPix->drawable.pScreen->myNum == scrnNum) {
            dmxBESavePixmap(pPix);
            dmxBEFreePixmap(pPix);
        }
    }
    else if ((type & TypeMask) == (RT_GC & TypeMask)) {
        GCPtr pGC = value;

        if (pGC->pScreen->myNum == scrnNum)
            dmxBEFreeGC(pGC);
    }
    else if ((type & TypeMask) == (RT_FONT & TypeMask)) {
        dmxBEFreeFont(pScreen, (FontPtr) value);
    }
    else if ((type & TypeMask) == (RT_CURSOR & TypeMask)) {
        dmxBEFreeCursor(pScreen, (CursorPtr) value);
    }
    else if ((type & TypeMask) == (RT_COLORMAP & TypeMask)) {
        ColormapPtr pCmap = value;

        if (pCmap->pScreen->myNum == scrnNum)
            dmxBEFreeColormap((ColormapPtr) value);
    }
    else if ((type & TypeMask) == (PictureType & TypeMask)) {
        PicturePtr pPict = value;

        if (pPict->pDrawable->pScreen->myNum == scrnNum) {
            /* Free the pixmaps on the backend if needed */
            if (pPict->pDrawable->type == DRAWABLE_PIXMAP) {
                PixmapPtr pPixmap = (PixmapPtr) (pPict->pDrawable);

                dmxBESavePixmap(pPixmap);
                dmxBEFreePixmap(pPixmap);
            }
            dmxBEFreePicture((PicturePtr) value);
        }
    }
    else if ((type & TypeMask) == (GlyphSetType & TypeMask)) {
        dmxBEFreeGlyphSet(pScreen, (GlyphSetPtr) value);
    }
    else {
        /* Other resource types??? */
    }
}

/** Destroy the scratch GCs that are created per depth. */
static void
dmxBEDestroyScratchGCs(int scrnNum)
{
    ScreenPtr pScreen = screenInfo.screens[scrnNum];
    GCPtr *ppGC = pScreen->GCperDepth;
    int i;

    for (i = 0; i <= pScreen->numDepths; i++)
        dmxBEFreeGC(ppGC[i]);
}

/** Destroy window hierachy on back-end server.  To ensure that all
 *  XDestroyWindow() calls succeed, they must be performed in a bottom
 *  up order so that windows are not destroyed before their children.
 *  XDestroyWindow(), which is called from #dmxBEDestroyWindow(), will
 *  destroy a window as well as all of it's children. */
static void
dmxBEDestroyWindowTree(int idx)
{
    WindowPtr pWin = screenInfo.screens[idx]->root;
    WindowPtr pChild = pWin;

    while (1) {
        if (pChild->firstChild) {
            pChild = pChild->firstChild;
            continue;
        }

        /* Destroy the window */
        dmxBEDestroyWindow(pChild);

        /* Make sure we destroy the window's border and background
         * pixmaps if they exist */
        if (!pChild->borderIsPixel) {
            dmxBESavePixmap(pChild->border.pixmap);
            dmxBEFreePixmap(pChild->border.pixmap);
        }
        if (pChild->backgroundState == BackgroundPixmap) {
            dmxBESavePixmap(pChild->background.pixmap);
            dmxBEFreePixmap(pChild->background.pixmap);
        }

        while (!pChild->nextSib && (pChild != pWin)) {
            pChild = pChild->parent;
            dmxBEDestroyWindow(pChild);
            if (!pChild->borderIsPixel) {
                dmxBESavePixmap(pChild->border.pixmap);
                dmxBEFreePixmap(pChild->border.pixmap);
            }
            if (pChild->backgroundState == BackgroundPixmap) {
                dmxBESavePixmap(pChild->background.pixmap);
                dmxBEFreePixmap(pChild->background.pixmap);
            }
        }

        if (pChild == pWin)
            break;

        pChild = pChild->nextSib;
    }
}

/** Detach back-end screen. */
int
dmxDetachScreen(int idx)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[idx];
    int i;

    /* Return failure if dynamic addition/removal of screens is disabled */
    if (!dmxAddRemoveScreens) {
        dmxLog(dmxWarning,
               "Attempting to remove a screen, but the AddRemoveScreen\n");
        dmxLog(dmxWarning,
               "extension has not been enabled.  To enable this extension\n");
        dmxLog(dmxWarning,
               "add the \"-addremovescreens\" option either to the command\n");
        dmxLog(dmxWarning, "line or in the configuration file.\n");
        return 1;
    }

    /* Cannot remove a screen that does not exist */
    if (idx < 0 || idx >= dmxNumScreens)
        return 1;

    /* Cannot detach from a screen that is not opened */
    if (!dmxScreen->beDisplay) {
        dmxLog(dmxWarning,
               "Attempting to remove screen #%d but it has not been opened\n",
               idx);
        return 1;
    }

    dmxLogOutput(dmxScreen, "Detaching screen #%d\n", idx);

    /* Detach input */
    dmxInputDetachAll(dmxScreen);

    /* Save all relevant state (TODO) */

    /* Free all non-window resources related to this screen */
    for (i = currentMaxClients; --i >= 0;)
        if (clients[i])
            FindAllClientResources(clients[i], dmxBEDestroyResources,
                                   (void *) (uintptr_t) idx);

    /* Free scratch GCs */
    dmxBEDestroyScratchGCs(idx);

    /* Free window resources related to this screen */
    dmxBEDestroyWindowTree(idx);

    /* Free default stipple */
    dmxBESavePixmap(screenInfo.screens[idx]->PixmapPerDepth[0]);
    dmxBEFreePixmap(screenInfo.screens[idx]->PixmapPerDepth[0]);

    /* Free the remaining screen resources and close the screen */
    dmxBECloseScreen(screenInfo.screens[idx]);

    /* Adjust the cursor boundaries (paints detached console window) */
    dmxAdjustCursorBoundaries();

    return 0;                   /* Success */
}
