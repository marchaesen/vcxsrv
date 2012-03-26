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
 * Provides pixmap support. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxsync.h"
#include "dmxpixmap.h"

#include "pixmapstr.h"
#include "servermd.h"
#include "privates.h"

/** Initialize a private area in \a pScreen for pixmap information. */
Bool
dmxInitPixmap(ScreenPtr pScreen)
{
    if (!dixRegisterPrivateKey
        (&dmxPixPrivateKeyRec, PRIVATE_PIXMAP, sizeof(dmxPixPrivRec)))
        return FALSE;

    return TRUE;
}

/** Create a pixmap on the back-end server. */
void
dmxBECreatePixmap(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxPixPrivPtr pPixPriv = DMX_GET_PIXMAP_PRIV(pPixmap);

    /* Make sure we haven't already created this pixmap.  This can
     * happen when the pixmap is used elsewhere (e.g., as a background
     * or border for a window) and the refcnt > 1.
     */
    if (pPixPriv->pixmap)
        return;

    if (pPixmap->drawable.width && pPixmap->drawable.height) {
        pPixPriv->pixmap = XCreatePixmap(dmxScreen->beDisplay,
                                         dmxScreen->scrnWin,
                                         pPixmap->drawable.width,
                                         pPixmap->drawable.height,
                                         pPixmap->drawable.depth);
        dmxSync(dmxScreen, FALSE);
    }
}

/** Create a pixmap for \a pScreen with the specified \a width, \a
 *  height, and \a depth. */
PixmapPtr
dmxCreatePixmap(ScreenPtr pScreen, int width, int height, int depth,
                unsigned usage_hint)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    PixmapPtr pPixmap;
    int bpp;
    dmxPixPrivPtr pPixPriv;

#if 0
    DMX_UNWRAP(CreatePixmap, dmxScreen, pScreen);
    if (pScreen->CreatePixmap)
        ret = pScreen->CreatePixmap(pPixmap);
#endif

    /* Create pixmap on back-end server */
    if (depth == 24)
        bpp = 32;
    else
        bpp = depth;

    pPixmap = AllocatePixmap(pScreen, 0);
    if (!pPixmap)
        return NullPixmap;

    pPixmap->drawable.type = DRAWABLE_PIXMAP;
    pPixmap->drawable.class = 0;
    pPixmap->drawable.pScreen = pScreen;
    pPixmap->drawable.depth = depth;
    pPixmap->drawable.bitsPerPixel = bpp;
    pPixmap->drawable.id = 0;
    pPixmap->drawable.serialNumber = NEXT_SERIAL_NUMBER;
    pPixmap->drawable.x = 0;
    pPixmap->drawable.y = 0;
    pPixmap->drawable.width = width;
    pPixmap->drawable.height = height;
    pPixmap->devKind = PixmapBytePad(width, bpp);
    pPixmap->refcnt = 1;
    pPixmap->usage_hint = usage_hint;

    pPixPriv = DMX_GET_PIXMAP_PRIV(pPixmap);
    pPixPriv->pixmap = (Pixmap) 0;
    pPixPriv->detachedImage = NULL;

    /* Create the pixmap on the back-end server */
    if (dmxScreen->beDisplay) {
        dmxBECreatePixmap(pPixmap);
    }

#if 0
    DMX_WRAP(CreatePixmap, dmxCreatePixmap, dmxScreen, pScreen);
#endif

    return pPixmap;
}

/** Destroy the pixmap on the back-end server. */
Bool
dmxBEFreePixmap(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxPixPrivPtr pPixPriv = DMX_GET_PIXMAP_PRIV(pPixmap);

    if (pPixPriv->pixmap) {
        XFreePixmap(dmxScreen->beDisplay, pPixPriv->pixmap);
        pPixPriv->pixmap = (Pixmap) 0;
        return TRUE;
    }

    return FALSE;
}

/** Destroy the pixmap pointed to by \a pPixmap. */
Bool
dmxDestroyPixmap(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    Bool ret = TRUE;

#if 0
    DMX_UNWRAP(DestroyPixmap, dmxScreen, pScreen);
#endif

    if (--pPixmap->refcnt)
        return TRUE;

    /* Destroy pixmap on back-end server */
    if (dmxScreen->beDisplay) {
        if (dmxBEFreePixmap(pPixmap)) {
            /* Also make sure that we destroy any detached image */
            dmxPixPrivPtr pPixPriv = DMX_GET_PIXMAP_PRIV(pPixmap);

            if (pPixPriv->detachedImage)
                XDestroyImage(pPixPriv->detachedImage);
            dmxSync(dmxScreen, FALSE);
        }
    }
    FreePixmap(pPixmap);

#if 0
    if (pScreen->DestroyPixmap)
        ret = pScreen->DestroyPixmap(pPixmap);
    DMX_WRAP(DestroyPixmap, dmxDestroyPixmap, dmxScreen, pScreen);
#endif

    return ret;
}

/** Create and return a region based on the pixmap pointed to by \a
 *  pPixmap. */
RegionPtr
dmxBitmapToRegion(PixmapPtr pPixmap)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pPixmap->drawable.pScreen->myNum];
    dmxPixPrivPtr pPixPriv = DMX_GET_PIXMAP_PRIV(pPixmap);
    XImage *ximage;
    RegionPtr pReg, pTmpReg;
    int x, y;
    unsigned long previousPixel, currentPixel;
    BoxRec Box;
    Bool overlap;

    if (!dmxScreen->beDisplay) {
        pReg = RegionCreate(NullBox, 1);
        return pReg;
    }

    ximage = XGetImage(dmxScreen->beDisplay, pPixPriv->pixmap, 0, 0,
                       pPixmap->drawable.width, pPixmap->drawable.height,
                       1, XYPixmap);

    pReg = RegionCreate(NullBox, 1);
    pTmpReg = RegionCreate(NullBox, 1);
    if (!pReg || !pTmpReg) {
        XDestroyImage(ximage);
        return NullRegion;
    }

    for (y = 0; y < pPixmap->drawable.height; y++) {
        Box.y1 = y;
        Box.y2 = y + 1;
        previousPixel = 0L;
        for (x = 0; x < pPixmap->drawable.width; x++) {
            currentPixel = XGetPixel(ximage, x, y);
            if (previousPixel != currentPixel) {
                if (previousPixel == 0L) {
                    /* left edge */
                    Box.x1 = x;
                }
                else if (currentPixel == 0L) {
                    /* right edge */
                    Box.x2 = x;
                    RegionReset(pTmpReg, &Box);
                    RegionAppend(pReg, pTmpReg);
                }
                previousPixel = currentPixel;
            }
        }
        if (previousPixel != 0L) {
            /* right edge because of the end of pixmap */
            Box.x2 = pPixmap->drawable.width;
            RegionReset(pTmpReg, &Box);
            RegionAppend(pReg, pTmpReg);
        }
    }

    RegionDestroy(pTmpReg);
    XDestroyImage(ximage);

    RegionValidate(pReg, &overlap);

    dmxSync(dmxScreen, FALSE);
    return pReg;
}
