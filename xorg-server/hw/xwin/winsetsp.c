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
winSetSpansNativeGDI(DrawablePtr pDrawable,
                     GCPtr pGC,
                     char *pSrcs,
                     DDXPointPtr pPoints,
                     int *piWidths, int iSpans, int fSorted)
{
    winGCPriv(pGC);
    PixmapPtr pPixmap = NULL;
    winPrivPixmapPtr pPixmapPriv = NULL;
    HBITMAP hbmpOrig = NULL;
    BITMAPINFO *pbmi;
    HRGN hrgn = NULL, combined = NULL;
    int nbox;
    BoxPtr pbox;

    nbox = RegionNumRects(pGC->pCompositeClip);
    pbox = RegionRects(pGC->pCompositeClip);

    if (!nbox)
        return;

    pbmi = malloc(sizeof(BITMAPINFO) + sizeof(RGBQUAD));

    combined = CreateRectRgn(pbox->x1, pbox->y1, pbox->x2, pbox->y2);
    nbox--;
    pbox++;
    while (nbox--) {
        hrgn = CreateRectRgn(pbox->x1, pbox->y1, pbox->x2, pbox->y2);
        CombineRgn(combined, combined, hrgn, RGN_OR);
        DeleteObject(hrgn);
        hrgn = NULL;
        pbox++;
    }

    /* Branch on the drawable type */
    switch (pDrawable->type) {
    case DRAWABLE_PIXMAP:

        SelectClipRgn(pGCPriv->hdcMem, combined);
        DeleteObject(combined);
        combined = NULL;

        pPixmap = (PixmapPtr) pDrawable;
        pPixmapPriv = winGetPixmapPriv(pPixmap);

        /* Select the drawable pixmap into a DC */
        hbmpOrig = SelectObject(pGCPriv->hdcMem, pPixmapPriv->hBitmap);
        if (hbmpOrig == NULL)
            FatalError("winSetSpans - DRAWABLE_PIXMAP - SelectObject () "
                       "failed on pPixmapPriv->hBitmap\n");

        while (iSpans--) {
            ZeroMemory(pbmi, sizeof(BITMAPINFO));
            pbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            pbmi->bmiHeader.biWidth = *piWidths;
            pbmi->bmiHeader.biHeight = 1;
            pbmi->bmiHeader.biPlanes = 1;
            pbmi->bmiHeader.biBitCount = pDrawable->depth;
            pbmi->bmiHeader.biCompression = BI_RGB;

            /* Setup color table for mono DIBs */
            if (pDrawable->depth == 1) {
                RGBQUAD *bmiColors = &(pbmi->bmiColors[0]);
                bmiColors[1].rgbBlue = 255;
                bmiColors[1].rgbGreen = 255;
                bmiColors[1].rgbRed = 255;
            }

            StretchDIBits(pGCPriv->hdcMem,
                          pPoints->x, pPoints->y,
                          *piWidths, 1,
                          0, 0,
                          *piWidths, 1,
                          pSrcs,
                          (BITMAPINFO *) pbmi,
                          DIB_RGB_COLORS, g_copyROP[pGC->alu]);

            pSrcs += PixmapBytePad(*piWidths, pDrawable->depth);
            pPoints++;
            piWidths++;
        }

        /* Reset the clip region */
        SelectClipRgn(pGCPriv->hdcMem, NULL);

        /* Push the drawable pixmap out of the GC HDC */
        SelectObject(pGCPriv->hdcMem, hbmpOrig);
        break;

    case DRAWABLE_WINDOW:

        SelectClipRgn(pGCPriv->hdc, combined);
        DeleteObject(combined);
        combined = NULL;

        while (iSpans--) {
            ZeroMemory(pbmi, sizeof(BITMAPINFO));
            pbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            pbmi->bmiHeader.biWidth = *piWidths;
            pbmi->bmiHeader.biHeight = 1;
            pbmi->bmiHeader.biPlanes = 1;
            pbmi->bmiHeader.biBitCount = pDrawable->depth;
            pbmi->bmiHeader.biCompression = BI_RGB;

            /* Setup color table for mono DIBs */
            if (pDrawable->depth == 1) {
                RGBQUAD *bmiColors = &(pbmi->bmiColors[0]);
                bmiColors[1].rgbBlue = 255;
                bmiColors[1].rgbGreen = 255;
                bmiColors[1].rgbRed = 255;
            }

            StretchDIBits(pGCPriv->hdc,
                          pPoints->x, pPoints->y,
                          *piWidths, 1,
                          0, 0,
                          *piWidths, 1,
                          pSrcs,
                          (BITMAPINFO *) pbmi,
                          DIB_RGB_COLORS, g_copyROP[pGC->alu]);

            pSrcs += PixmapBytePad(*piWidths, pDrawable->depth);
            pPoints++;
            piWidths++;
        }

        /* Reset the clip region */
        SelectClipRgn(pGCPriv->hdc, NULL);
        break;

    default:
        FatalError("\nwinSetSpansNativeGDI - Unknown drawable type\n\n");
        break;
    }

    free(pbmi);
}
