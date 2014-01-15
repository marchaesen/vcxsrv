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
 * Authors:	drewry, september 1986
 *		Harold L Hunt II
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"

/*
 * Local prototypes
 */

#if 0
static void
 winXRotatePixmapNativeGDI(PixmapPtr pPix, int rw);

static void
 winYRotatePixmapNativeGDI(PixmapPtr pPix, int rh);

static void

winCopyRotatePixmapNativeGDI(PixmapPtr psrcPix, PixmapPtr *ppdstPix,
                             int xrot, int yrot);
#endif

/* See Porting Layer Definition - p. 34 */
/* See mfb/mfbpixmap.c - mfbCreatePixmap() */
PixmapPtr
winCreatePixmapNativeGDI(ScreenPtr pScreen,
                         int iWidth, int iHeight,
                         int iDepth, unsigned usage_hint)
{
    winPrivPixmapPtr pPixmapPriv = NULL;
    PixmapPtr pPixmap = NULL;

    /* Allocate pixmap memory */
    pPixmap = AllocatePixmap(pScreen, 0);
    if (!pPixmap) {
        ErrorF("winCreatePixmapNativeGDI () - Couldn't allocate a pixmap\n");
        return NullPixmap;
    }

#if CYGDEBUG
    winDebug("winCreatePixmap () - w %d h %d d %d uh %d bw %d\n",
             iWidth, iHeight, iDepth, usage_hint,
             PixmapBytePad(iWidth, iDepth));
#endif

    /* Setup pixmap values */
    pPixmap->drawable.type = DRAWABLE_PIXMAP;
    pPixmap->drawable.class = 0;
    pPixmap->drawable.pScreen = pScreen;
    pPixmap->drawable.depth = iDepth;
    pPixmap->drawable.bitsPerPixel = BitsPerPixel(iDepth);
    pPixmap->drawable.id = 0;
    pPixmap->drawable.serialNumber = NEXT_SERIAL_NUMBER;
    pPixmap->drawable.x = 0;
    pPixmap->drawable.y = 0;
    pPixmap->drawable.width = iWidth;
    pPixmap->drawable.height = iHeight;
    pPixmap->devKind = 0;
    pPixmap->refcnt = 1;
    pPixmap->devPrivate.ptr = NULL;
    pPixmap->usage_hint = usage_hint;

    /* Pixmap privates are allocated by AllocatePixmap */
    pPixmapPriv = winGetPixmapPriv(pPixmap);

    /* Initialize pixmap privates */
    pPixmapPriv->hBitmap = NULL;
    pPixmapPriv->hdcSelected = NULL;
    pPixmapPriv->pbBits = NULL;
    pPixmapPriv->dwScanlineBytes = PixmapBytePad(iWidth, iDepth);

    /* Check for zero width or height pixmaps */
    if (iWidth == 0 || iHeight == 0) {
        /* Don't allocate a real pixmap, just set fields and return */
        return pPixmap;
    }

    /* Create a DIB for the pixmap */
    pPixmapPriv->hBitmap = winCreateDIBNativeGDI(iWidth, iHeight, iDepth,
                                                 &pPixmapPriv->pbBits,
                                                 (BITMAPINFO **) &pPixmapPriv->
                                                 pbmih);

#if CYGDEBUG
    winDebug("winCreatePixmap () - Created a pixmap %08x, %dx%dx%d, for "
             "screen: %08x\n",
             pPixmapPriv->hBitmap, iWidth, iHeight, iDepth, pScreen);
#endif

    return pPixmap;
}

/* 
 * See Porting Layer Definition - p. 35
 *
 * See mfb/mfbpixmap.c - mfbDestroyPixmap()
 */

Bool
winDestroyPixmapNativeGDI(PixmapPtr pPixmap)
{
    winPrivPixmapPtr pPixmapPriv = NULL;

#if CYGDEBUG
    winDebug("winDestroyPixmapNativeGDI ()\n");
#endif

    /* Bail early if there is not a pixmap to destroy */
    if (pPixmap == NULL) {
        ErrorF("winDestroyPixmapNativeGDI () - No pixmap to destroy\n");
        return TRUE;
    }

    /* Get a handle to the pixmap privates */
    pPixmapPriv = winGetPixmapPriv(pPixmap);

#if CYGDEBUG
    winDebug("winDestroyPixmapNativeGDI - pPixmapPriv->hBitmap: %08x\n",
             pPixmapPriv->hBitmap);
#endif

    /* Decrement reference count, return if nonzero */
    --pPixmap->refcnt;
    if (pPixmap->refcnt != 0)
        return TRUE;

    /* Free GDI bitmap */
    if (pPixmapPriv->hBitmap)
        DeleteObject(pPixmapPriv->hBitmap);

    /* Free the bitmap info header memory */
    free(pPixmapPriv->pbmih);
    pPixmapPriv->pbmih = NULL;

    /* Free the pixmap memory */
    free(pPixmap);
    pPixmap = NULL;

    return TRUE;
}

/* 
 * Not used yet
 */

Bool
winModifyPixmapHeaderNativeGDI(PixmapPtr pPixmap,
                               int iWidth, int iHeight,
                               int iDepth,
                               int iBitsPerPixel, int devKind, void *pPixData)
{
    FatalError("winModifyPixmapHeaderNativeGDI ()\n");
    return TRUE;
}

#if 0
/* 
 * Not used yet.
 * See cfb/cfbpixmap.c
 */

static void
winXRotatePixmapNativeGDI(PixmapPtr pPix, int rw)
{
    ErrorF("winXRotatePixmap()\n");
    /* fill in this function, look at CFB */
}

/*
 * Not used yet.
 * See cfb/cfbpixmap.c
 */
static void
winYRotatePixmapNativeGDI(PixmapPtr pPix, int rh)
{
    ErrorF("winYRotatePixmap()\n");
    /* fill in this function, look at CFB */
}

/* 
 * Not used yet.
 * See cfb/cfbpixmap.c
 */

static void
winCopyRotatePixmapNativeGDI(PixmapPtr psrcPix, PixmapPtr *ppdstPix,
                             int xrot, int yrot)
{
    ErrorF("winCopyRotatePixmap()\n");
    /* fill in this function, look at CFB */
}
#endif
