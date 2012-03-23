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
 * 		Alan Hourihane <alanh@fairlite.demon.co.uk>
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"

/* See Porting Layer Definition - p. 55 */
void
winGetSpansNativeGDI(DrawablePtr pDrawable,
                     int nMax,
                     DDXPointPtr pPoints,
                     int *piWidths, int iSpans, char *pDsts)
{
    PixmapPtr pPixmap = NULL;
    winPrivPixmapPtr pPixmapPriv = NULL;
    int iSpan;
    DDXPointPtr pPoint = NULL;
    int *piWidth = NULL;
    char *pDst = pDsts;
    HBITMAP hbmpWindow, hbmpOrig, hbmpOrig1;
    BYTE *pbWindow = NULL;
    HDC hdcMem, hdcMem1;
    ScreenPtr pScreen = pDrawable->pScreen;

    winScreenPriv(pScreen);

    /* Branch on the drawable type */
    switch (pDrawable->type) {
    case DRAWABLE_PIXMAP:
#if 0
        ErrorF("winGetSpans - DRAWABLE_PIXMAP %08x\n", pDrawable);
#endif

        pPixmap = (PixmapPtr) pDrawable;
        pPixmapPriv = winGetPixmapPriv(pPixmap);

        /* Open a memory HDC */
        hdcMem1 = CreateCompatibleDC(NULL);
        hdcMem = CreateCompatibleDC(NULL);

        /* Select the drawable pixmap into a DC */
        hbmpOrig1 = SelectObject(hdcMem1, pPixmapPriv->hBitmap);

        if (hbmpOrig1 == NULL)
            FatalError("winGetSpans - DRAWABLE_PIXMAP - SelectObject () "
                       "failed on pPixmapPriv->hBitmap\n");

        /* Loop through spans */
        for (iSpan = 0; iSpan < iSpans; ++iSpan) {
            pPoint = pPoints + iSpan;
            piWidth = piWidths + iSpan;

            hbmpWindow = winCreateDIBNativeGDI(*piWidth, 1,
                                               pDrawable->depth,
                                               &pbWindow, NULL);

            hbmpOrig = SelectObject(hdcMem, hbmpWindow);

            /* Transfer the window bits to the window bitmap */
            BitBlt(hdcMem,
                   0, 0, *piWidth, 1, hdcMem1, pPoint->x, pPoint->y, SRCCOPY);

            memcpy(pDst,
                   (char *) pbWindow,
                   PixmapBytePad(*piWidth, pDrawable->depth));

            /* Pop the window bitmap out of the HDC and delete the bitmap */
            SelectObject(hdcMem, hbmpOrig);
            DeleteObject(hbmpWindow);

#if 0
            ErrorF("(%dx%dx%d) (%d,%d) w: %d\n",
                   pDrawable->width, pDrawable->height, pDrawable->depth,
                   pPoint->x, pPoint->y, *piWidth);
#endif

            /* Calculate offset of next bit destination */
            pDst += PixmapBytePad(*piWidth, pDrawable->depth);
        }

        /* Pop the pixmap's bitmap out of the HDC */
        SelectObject(hdcMem1, hbmpOrig1);

        /* Delete the HDCs */
        DeleteDC(hdcMem1);
        DeleteDC(hdcMem);
        break;

    case DRAWABLE_WINDOW:
#if 0
        ErrorF("winGetSpans - DRAWABLE_WINDOW\n");
#endif

        /* Open a memory HDC */
        hdcMem = CreateCompatibleDC(NULL);

        /* Loop through spans */
        for (iSpan = 0; iSpan < iSpans; ++iSpan) {
            pPoint = pPoints + iSpan;
            piWidth = piWidths + iSpan;

            hbmpWindow = winCreateDIBNativeGDI(*piWidth, 1,
                                               pDrawable->depth,
                                               &pbWindow, NULL);

            hbmpOrig = SelectObject(hdcMem, hbmpWindow);

            /* Transfer the window bits to the window bitmap */
            BitBlt(hdcMem,
                   0, 0,
                   *piWidth, 1,
                   pScreenPriv->hdcScreen, pPoint->x, pPoint->y, SRCCOPY);

            memcpy(pDst,
                   (char *) pbWindow,
                   PixmapBytePad(*piWidth, pDrawable->depth));

            /* Pop the window bitmap out of the HDC */
            SelectObject(hdcMem, hbmpOrig);

            DeleteObject(hbmpWindow);

#if 0
            ErrorF("(%dx%dx%d) (%d,%d) w: %d\n",
                   pDrawable->width, pDrawable->height, pDrawable->depth,
                   pPoint->x, pPoint->y, *piWidth);
#endif

            /* Calculate offset of next bit destination */
            pDst += PixmapBytePad(*piWidth, pDrawable->depth);
        }

        /* Delete the window bitmap */
        DeleteDC(hdcMem);
        break;

    default:
        FatalError("winGetSpans - Unknown drawable type\n");
        break;
    }
}
