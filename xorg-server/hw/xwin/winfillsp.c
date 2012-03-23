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

extern void ROP16(HDC hdc, int rop);

#define TRANSLATE_COLOR(color)						\
{									\
  if (pDrawable->depth == 15)						\
    color = ((color & 0x1F) << 19) | ((color & 0x03E0) << 6) |		\
      ((color & 0xF800) >> 8);						\
  else if (pDrawable->depth == 16)					\
    color = ((color & 0x1F) << 19) | ((color & 0x07E0) << 5) |		\
      ((color & 0xF800) >> 8);						\
  else if (pDrawable->depth == 24 || pDrawable->depth == 32)		\
    color = ((color & 0xFF) << 16) | (color & 0xFF00) |			\
      ((color & 0xFF0000) >> 16);					\
}

/* See Porting Layer Definition - p. 54 */
void
winFillSpansNativeGDI(DrawablePtr pDrawable,
                      GCPtr pGC,
                      int iSpans,
                      DDXPointPtr pPoints, int *piWidths, int fSorted)
{
    winGCPriv(pGC);
    HBITMAP hbmpOrig = NULL, hbmpOrigStipple = NULL;
    HBITMAP hPenOrig = NULL;
    HBITMAP hBitmap = NULL;
    PixmapPtr pPixmap = NULL;
    winPrivPixmapPtr pPixmapPriv = NULL;
    PixmapPtr pStipple = NULL;
    winPrivPixmapPtr pStipplePriv = NULL;
    PixmapPtr pTile = NULL;
    winPrivPixmapPtr pTilePriv = NULL;
    HDC hdcStipple = NULL, hdcTile = NULL;
    HPEN hPen = NULL;
    int iX;
    int fg, bg;
    RegionPtr pClip = pGC->pCompositeClip;
    BoxPtr pextent, pbox;
    int nbox;
    int extentX1, extentX2, extentY1, extentY2;
    int fullX1, fullX2, fullY1;
    HRGN hrgn = NULL, combined = NULL;

    nbox = RegionNumRects(pClip);
    pbox = RegionRects(pClip);

    if (!nbox)
        return;

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

    pextent = RegionExtents(pClip);
    extentX1 = pextent->x1;
    extentY1 = pextent->y1;
    extentX2 = pextent->x2;
    extentY2 = pextent->y2;

    /* Branch on the type of drawable we have */
    switch (pDrawable->type) {
    case DRAWABLE_PIXMAP:

        SelectClipRgn(pGCPriv->hdcMem, combined);
        DeleteObject(combined);
        combined = NULL;

        /* Get a pixmap pointer from the drawable pointer, and fetch privates  */
        pPixmap = (PixmapPtr) pDrawable;
        pPixmapPriv = winGetPixmapPriv(pPixmap);

        /* Select the drawable pixmap into memory hdc */
        hbmpOrig = SelectObject(pGCPriv->hdcMem, pPixmapPriv->hBitmap);
        if (hbmpOrig == NULL)
            FatalError("winFillSpans - DRAWABLE_PIXMAP - "
                       "SelectObject () failed on\n\tpPixmapPriv->hBitmap: "
                       "%08x\n", (unsigned int) pPixmapPriv->hBitmap);

        /* Branch on the fill type */
        switch (pGC->fillStyle) {
        case FillSolid:

            ROP16(pGCPriv->hdcMem, pGC->alu);

            if (pDrawable->depth == 1) {
                if (pGC->fgPixel == 0)
                    hPenOrig = SelectObject(pGCPriv->hdcMem,
                                            GetStockObject(BLACK_PEN));
                else
                    hPenOrig = SelectObject(pGCPriv->hdcMem,
                                            GetStockObject(WHITE_PEN));
            }
            else {
                fg = pGC->fgPixel;
                TRANSLATE_COLOR(fg);
                hPen = CreatePen(PS_SOLID, 0, fg);
                hPenOrig = SelectObject(pGCPriv->hdcMem, hPen);
            }

            while (iSpans--) {
                fullX1 = pPoints->x;
                fullY1 = pPoints->y;
                fullX2 = fullX1 + (int) *piWidths;
                pPoints++;
                piWidths++;

                if (fullY1 < extentY1 || extentY2 <= fullY1)
                    continue;

                if (fullX1 < extentX1)
                    fullX1 = extentX1;
                if (fullX2 > extentX2)
                    fullX2 = extentX2;

                if (fullX1 >= fullX2)
                    continue;

                MoveToEx(pGCPriv->hdcMem, fullX1, fullY1, NULL);
                LineTo(pGCPriv->hdcMem, fullX2, fullY1);
            }

            SetROP2(pGCPriv->hdcMem, R2_COPYPEN);

            /* Give back the Pen */
            SelectObject(pGCPriv->hdcMem, hPenOrig);

            if (pDrawable->depth != 1)
                DeleteObject(hPen);
            break;

        case FillOpaqueStippled:

            pStipple = pGC->stipple;
            pStipplePriv = winGetPixmapPriv(pStipple);

            /* Create a device-dependent bitmap for the stipple */
            hBitmap = CreateDIBitmap(pGCPriv->hdcMem,
                                     (BITMAPINFOHEADER *) pStipplePriv->pbmih,
                                     CBM_INIT,
                                     pStipplePriv->pbBits,
                                     (BITMAPINFO *) pStipplePriv->pbmih,
                                     DIB_RGB_COLORS);

            /* Create a memory DC to hold the stipple */
            hdcStipple = CreateCompatibleDC(pGCPriv->hdcMem);

            /* Select the stipple bitmap into the stipple DC */
            hbmpOrigStipple = SelectObject(hdcStipple, hBitmap);
            if (hbmpOrigStipple == NULL)
                FatalError("winFillSpans () - DRAWABLE_PIXMAP - FillStippled - "
                           "SelectObject () failed on hbmpOrigStipple\n");

            /* Make a temporary copy of the foreground and background colors */
            bg = pGC->bgPixel;
            fg = pGC->fgPixel;

            /* Translate the depth-dependent colors to Win32 COLORREFs */
            TRANSLATE_COLOR(fg);
            TRANSLATE_COLOR(bg);
            SetTextColor(pGCPriv->hdcMem, fg);
            SetBkColor(pGCPriv->hdcMem, bg);

            while (iSpans--) {
                int width = pStipple->drawable.width;

                fullX1 = pPoints->x;
                fullY1 = pPoints->y;
                fullX2 = fullX1 + (int) *piWidths;
                pPoints++;
                piWidths++;

                if (fullY1 < extentY1 || extentY2 <= fullY1)
                    continue;

                if (fullX1 < extentX1)
                    fullX1 = extentX1;
                if (fullX2 > extentX2)
                    fullX2 = extentX2;

                if (fullX1 >= fullX2)
                    continue;

                for (iX = fullX1; iX < fullX2; iX += width) {
                    int xoffset;

                    if ((iX + pStipple->drawable.width) > fullX2)
                        width = fullX2 - iX;
                    else
                        width = pStipple->drawable.width;

                    if (iX == fullX1)
                        xoffset =
                            (fullX1 -
                             (pDrawable->x +
                              (pGC->patOrg.x % pStipple->drawable.width) -
                              pStipple->drawable.width)) %
                            pStipple->drawable.width;
                    else
                        xoffset = 0;

                    if (xoffset + width > pStipple->drawable.width)
                        width = pStipple->drawable.width - xoffset;

                    BitBlt(pGCPriv->hdcMem,
                           iX, fullY1,
                           width, 1,
                           hdcStipple,
                           xoffset,
                           (fullY1 -
                            (pDrawable->y +
                             (pGC->patOrg.y % pStipple->drawable.height) -
                             pStipple->drawable.height)) %
                           pStipple->drawable.height, g_copyROP[pGC->alu]);
                }
            }

            /* Clear the stipple HDC */
            SelectObject(hdcStipple, hbmpOrigStipple);
            DeleteDC(hdcStipple);

            /* Delete the device dependent stipple bitmap */
            DeleteObject(hBitmap);

            break;
        case FillStippled:

            pStipple = pGC->stipple;
            pStipplePriv = winGetPixmapPriv(pStipple);

            /* Create a device-dependent bitmap for the stipple */
            hBitmap = CreateDIBitmap(pGCPriv->hdcMem,
                                     (BITMAPINFOHEADER *) pStipplePriv->pbmih,
                                     CBM_INIT,
                                     pStipplePriv->pbBits,
                                     (BITMAPINFO *) pStipplePriv->pbmih,
                                     DIB_RGB_COLORS);

            /* Create a memory DC to hold the stipple */
            hdcStipple = CreateCompatibleDC(pGCPriv->hdcMem);

            /* Select the stipple bitmap into the stipple DC */
            hbmpOrigStipple = SelectObject(hdcStipple, hBitmap);
            if (hbmpOrigStipple == NULL)
                FatalError("winFillSpans () - DRAWABLE_PIXMAP - FillStippled - "
                           "SelectObject () failed on hbmpOrigStipple\n");

            /* Make a temporary copy of the foreground and background colors */
            bg = pGC->bgPixel;
            fg = pGC->fgPixel;

            /* Translate the depth-dependent colors to Win32 COLORREFs */
            TRANSLATE_COLOR(fg);
            TRANSLATE_COLOR(bg);

            /* this is fudgy, we should only invert on the last one
             * We need to get the black/white pixels right in the
             * colormap. But yeah ! it's working.. 
             */
            if (pGC->bgPixel != -1 && pGC->fgPixel != -1) {
                SetTextColor(pGCPriv->hdcMem, fg);
                SetBkColor(pGCPriv->hdcMem, bg);
                BitBlt(hdcStipple,
                       0, 0,
                       pStipple->drawable.width, pStipple->drawable.height,
                       hdcStipple, 0, 0, 0x330008);
            }
            else if (pGC->bgPixel == -1) {
                SetTextColor(pGCPriv->hdcMem, fg);
                SetBkMode(pGCPriv->hdcMem, TRANSPARENT);
                BitBlt(hdcStipple,
                       0, 0,
                       pStipple->drawable.width, pStipple->drawable.height,
                       hdcStipple, 0, 0, 0x330008);
            }
            else if (pGC->fgPixel == -1) {
                SetTextColor(pGCPriv->hdcMem, bg);
                SetBkMode(pGCPriv->hdcMem, TRANSPARENT);
#if 0
                BitBlt(hdcStipple,
                       0, 0,
                       pStipple->drawable.width, pStipple->drawable.height,
                       hdcStipple, 0, 0, 0x330008);
#endif
            }

            while (iSpans--) {
                int width = pStipple->drawable.width;

                fullX1 = pPoints->x;
                fullY1 = pPoints->y;
                fullX2 = fullX1 + (int) *piWidths;
                pPoints++;
                piWidths++;

                if (fullY1 < extentY1 || extentY2 <= fullY1)
                    continue;

                if (fullX1 < extentX1)
                    fullX1 = extentX1;
                if (fullX2 > extentX2)
                    fullX2 = extentX2;

                if (fullX1 >= fullX2)
                    continue;

                for (iX = fullX1; iX < fullX2; iX += width) {
                    int xoffset;

                    if ((iX + pStipple->drawable.width) > fullX2)
                        width = fullX2 - iX;
                    else
                        width = pStipple->drawable.width;

                    if (iX == fullX1)
                        xoffset =
                            (fullX1 -
                             (pDrawable->x +
                              (pGC->patOrg.x % pStipple->drawable.width) -
                              pStipple->drawable.width)) %
                            pStipple->drawable.width;
                    else
                        xoffset = 0;

                    if (xoffset + width > pStipple->drawable.width)
                        width = pStipple->drawable.width - xoffset;

                    BitBlt(pGCPriv->hdcMem,
                           iX, fullY1,
                           width, 1,
                           hdcStipple,
                           xoffset,
                           (fullY1 -
                            (pDrawable->y +
                             (pGC->patOrg.y % pStipple->drawable.height) -
                             pStipple->drawable.height)) %
                           pStipple->drawable.height, g_copyROP[pGC->alu]);
                }
            }

            /* Clear the stipple HDC */
            SelectObject(hdcStipple, hbmpOrigStipple);
            DeleteDC(hdcStipple);

            /* Delete the device dependent stipple bitmap */
            DeleteObject(hBitmap);

            /* Restore the background mode */
            SetBkMode(pGCPriv->hdcMem, OPAQUE);
            break;

        case FillTiled:

            /* Get a pixmap pointer from the tile pointer, and fetch privates  */
            pTile = (PixmapPtr) pGC->tile.pixmap;
            pTilePriv = winGetPixmapPriv(pTile);

            /* Create a memory DC to hold the tile */
            hdcTile = CreateCompatibleDC(pGCPriv->hdcMem);

            /* Select the tile into a DC */
            hbmpOrig = SelectObject(hdcTile, pTilePriv->hBitmap);
            if (hbmpOrig == NULL)
                FatalError("winFillSpans - DRAWABLE_PIXMAP - FillTiled - "
                           "SelectObject () failed on pTilePriv->hBitmap\n");

            while (iSpans--) {
                int width = pTile->drawable.width;

                fullX1 = pPoints->x;
                fullY1 = pPoints->y;
                fullX2 = fullX1 + (int) *piWidths;
                pPoints++;
                piWidths++;

                if (fullY1 < extentY1 || extentY2 <= fullY1)
                    continue;

                if (fullX1 < extentX1)
                    fullX1 = extentX1;
                if (fullX2 > extentX2)
                    fullX2 = extentX2;

                if (fullX1 >= fullX2)
                    continue;

                for (iX = fullX1; iX < fullX2; iX += width) {
                    int xoffset;

                    if ((iX + pTile->drawable.width) > fullX2)
                        width = fullX2 - iX;
                    else
                        width = pTile->drawable.width;

                    if (iX == fullX1)
                        xoffset =
                            (fullX1 -
                             (pDrawable->x +
                              (pGC->patOrg.x % pTile->drawable.width) -
                              pTile->drawable.width)) % pTile->drawable.width;
                    else
                        xoffset = 0;

                    if (xoffset + width > pTile->drawable.width)
                        width = pTile->drawable.width - xoffset;

                    BitBlt(pGCPriv->hdcMem,
                           iX, fullY1,
                           width, 1,
                           hdcTile,
                           xoffset,
                           (fullY1 -
                            (pDrawable->y +
                             (pGC->patOrg.y % pTile->drawable.height) -
                             pTile->drawable.height)) % pTile->drawable.height,
                           g_copyROP[pGC->alu]);
                }
            }

            /* Push the tile pixmap out of the memory HDC */
            SelectObject(hdcTile, hbmpOrig);

            /* Delete the tile */
            DeleteDC(hdcTile);
            break;

        default:
            ErrorF("winFillSpans - DRAWABLE_PIXMAP - Unknown fillStyle\n");
            break;
        }

        /* Reset clip region */
        SelectClipRgn(pGCPriv->hdcMem, NULL);

        /* Push the drawable pixmap out of the GC HDC */
        SelectObject(pGCPriv->hdcMem, hbmpOrig);
        break;

    case DRAWABLE_WINDOW:

        SelectClipRgn(pGCPriv->hdc, combined);
        DeleteObject(combined);
        combined = NULL;

        /* Branch on fill style */
        switch (pGC->fillStyle) {
        case FillSolid:

            ROP16(pGCPriv->hdc, pGC->alu);

            if (pDrawable->depth == 1) {
                if (pGC->fgPixel == 0)
                    hPenOrig = SelectObject(pGCPriv->hdc,
                                            GetStockObject(BLACK_PEN));
                else
                    hPenOrig = SelectObject(pGCPriv->hdc,
                                            GetStockObject(WHITE_PEN));
            }
            else {
                fg = pGC->fgPixel;
                TRANSLATE_COLOR(fg);
                hPen = CreatePen(PS_SOLID, 0, fg);
                hPenOrig = SelectObject(pGCPriv->hdc, hPen);
            }

            while (iSpans--) {
                fullX1 = pPoints->x;
                fullY1 = pPoints->y;
                fullX2 = fullX1 + (int) *piWidths;
                pPoints++;
                piWidths++;

                if (fullY1 < extentY1 || extentY2 <= fullY1)
                    continue;

                if (fullX1 < extentX1)
                    fullX1 = extentX1;
                if (fullX2 > extentX2)
                    fullX2 = extentX2;

                if (fullX1 >= fullX2)
                    continue;

                MoveToEx(pGCPriv->hdc, fullX1, fullY1, NULL);
                LineTo(pGCPriv->hdc, fullX2, fullY1);
            }

            SetROP2(pGCPriv->hdc, R2_COPYPEN);

            /* Give back the Brush */
            SelectObject(pGCPriv->hdc, hPenOrig);

            if (pDrawable->depth != 1)
                DeleteObject(hPen);
            break;

        case FillOpaqueStippled:

            pStipple = pGC->stipple;
            pStipplePriv = winGetPixmapPriv(pStipple);

            /* Create a device-dependent bitmap for the stipple */
            hBitmap = CreateDIBitmap(pGCPriv->hdc,
                                     (BITMAPINFOHEADER *) pStipplePriv->pbmih,
                                     CBM_INIT,
                                     pStipplePriv->pbBits,
                                     (BITMAPINFO *) pStipplePriv->pbmih,
                                     DIB_RGB_COLORS);

            /* Create a memory DC to hold the stipple */
            hdcStipple = CreateCompatibleDC(pGCPriv->hdc);

            /* Select the stipple bitmap into the stipple DC */
            hbmpOrigStipple = SelectObject(hdcStipple, hBitmap);
            if (hbmpOrigStipple == NULL)
                FatalError("winFillSpans () - DRAWABLE_PIXMAP - FillStippled - "
                           "SelectObject () failed on hbmpOrigStipple\n");

            /* Make a temporary copy of the foreground and background colors */
            bg = pGC->bgPixel;
            fg = pGC->fgPixel;

            /* Translate the depth-dependent colors to Win32 COLORREFs */
            TRANSLATE_COLOR(fg);
            TRANSLATE_COLOR(bg);
            SetTextColor(pGCPriv->hdc, fg);
            SetBkColor(pGCPriv->hdc, bg);

            while (iSpans--) {
                int width = pStipple->drawable.width;

                fullX1 = pPoints->x;
                fullY1 = pPoints->y;
                fullX2 = fullX1 + (int) *piWidths;
                pPoints++;
                piWidths++;

                if (fullY1 < extentY1 || extentY2 <= fullY1)
                    continue;

                if (fullX1 < extentX1)
                    fullX1 = extentX1;
                if (fullX2 > extentX2)
                    fullX2 = extentX2;

                if (fullX1 >= fullX2)
                    continue;

                for (iX = fullX1; iX < fullX2; iX += width) {
                    int xoffset;

                    if ((iX + pStipple->drawable.width) > fullX2)
                        width = fullX2 - iX;
                    else
                        width = pStipple->drawable.width;

                    if (iX == fullX1)
                        xoffset =
                            (fullX1 -
                             (pDrawable->x +
                              (pGC->patOrg.x % pStipple->drawable.width) -
                              pStipple->drawable.width)) %
                            pStipple->drawable.width;
                    else
                        xoffset = 0;

                    if (xoffset + width > pStipple->drawable.width)
                        width = pStipple->drawable.width - xoffset;

                    BitBlt(pGCPriv->hdc,
                           iX, fullY1,
                           width, 1,
                           hdcStipple,
                           xoffset,
                           (fullY1 -
                            (pDrawable->y +
                             (pGC->patOrg.y % pStipple->drawable.height) -
                             pStipple->drawable.height)) %
                           pStipple->drawable.height, g_copyROP[pGC->alu]);
                }
            }

            /* Clear the stipple HDC */
            SelectObject(hdcStipple, hbmpOrigStipple);
            DeleteDC(hdcStipple);

            /* Delete the device dependent stipple bitmap */
            DeleteObject(hBitmap);

            break;

        case FillStippled:
            pStipple = pGC->stipple;
            pStipplePriv = winGetPixmapPriv(pStipple);

            /* Create a device-dependent bitmap for the stipple */
            hBitmap = CreateDIBitmap(pGCPriv->hdcMem,
                                     (BITMAPINFOHEADER *) pStipplePriv->pbmih,
                                     CBM_INIT,
                                     pStipplePriv->pbBits,
                                     (BITMAPINFO *) pStipplePriv->pbmih,
                                     DIB_RGB_COLORS);

            /* Create a memory DC to hold the stipple */
            hdcStipple = CreateCompatibleDC(pGCPriv->hdc);

            /* Select the stipple bitmap into the stipple DC */
            hbmpOrigStipple = SelectObject(hdcStipple, hBitmap);
            if (hbmpOrigStipple == NULL)
                FatalError("winFillSpans () - DRAWABLE_PIXMAP - FillStippled - "
                           "SelectObject () failed on hbmpOrigStipple\n");

            /* Make a temporary copy of the foreground and background colors */
            bg = pGC->bgPixel;
            fg = pGC->fgPixel;

            /* Translate the depth-dependent colors to Win32 COLORREFs */
            TRANSLATE_COLOR(fg);
            TRANSLATE_COLOR(bg);

            /* this is fudgy, we should only invert on the last one
             * We need to get the black/white pixels right in the
             * colormap. But yeah ! it's working.. 
             */
            if (pGC->bgPixel != -1 && pGC->fgPixel != -1) {
                SetTextColor(pGCPriv->hdc, fg);
                SetBkColor(pGCPriv->hdc, bg);
                BitBlt(hdcStipple,
                       0, 0,
                       pStipple->drawable.width, pStipple->drawable.height,
                       hdcStipple, 0, 0, 0x330008);
            }
            else if (pGC->bgPixel == -1) {
                SetTextColor(pGCPriv->hdc, fg);
                SetBkMode(pGCPriv->hdc, TRANSPARENT);
                BitBlt(hdcStipple,
                       0, 0,
                       pStipple->drawable.width, pStipple->drawable.height,
                       hdcStipple, 0, 0, 0x330008);
            }
            else if (pGC->fgPixel == -1) {
                SetTextColor(pGCPriv->hdc, bg);
                SetBkMode(pGCPriv->hdc, TRANSPARENT);
#if 0
                BitBlt(hdcStipple,
                       0, 0,
                       pStipple->drawable.width, pStipple->drawable.height,
                       hdcStipple, 0, 0, 0x330008);
#endif
            }

            while (iSpans--) {
                int width = pStipple->drawable.width;

                fullX1 = pPoints->x;
                fullY1 = pPoints->y;
                fullX2 = fullX1 + (int) *piWidths;
                pPoints++;
                piWidths++;

                if (fullY1 < extentY1 || extentY2 <= fullY1)
                    continue;

                if (fullX1 < extentX1)
                    fullX1 = extentX1;
                if (fullX2 > extentX2)
                    fullX2 = extentX2;

                if (fullX1 >= fullX2)
                    continue;

                for (iX = fullX1; iX < fullX2; iX += width) {
                    int xoffset;

                    if ((iX + pStipple->drawable.width) > fullX2)
                        width = fullX2 - iX;
                    else
                        width = pStipple->drawable.width;

                    if (iX == fullX1)
                        xoffset =
                            (fullX1 -
                             (pDrawable->x +
                              (pGC->patOrg.x % pStipple->drawable.width) -
                              pStipple->drawable.width)) %
                            pStipple->drawable.width;
                    else
                        xoffset = 0;

                    if (xoffset + width > pStipple->drawable.width)
                        width = pStipple->drawable.width - xoffset;

                    BitBlt(pGCPriv->hdc,
                           iX, fullY1,
                           width, 1,
                           hdcStipple,
                           xoffset,
                           (fullY1 -
                            (pDrawable->y +
                             (pGC->patOrg.y % pStipple->drawable.height) -
                             pStipple->drawable.height)) %
                           pStipple->drawable.height, g_copyROP[pGC->alu]);
                }
            }

            /* Clear the stipple HDC */
            SelectObject(hdcStipple, hbmpOrigStipple);
            DeleteDC(hdcStipple);

            /* Delete the device dependent stipple bitmap */
            DeleteObject(hBitmap);

            /* Restore the background mode */
            SetBkMode(pGCPriv->hdc, OPAQUE);
            break;

        case FillTiled:

            /* Get a pixmap pointer from the tile pointer, and fetch privates  */
            pTile = (PixmapPtr) pGC->tile.pixmap;
            pTilePriv = winGetPixmapPriv(pTile);

            /* Select the tile into a DC */
            hbmpOrig = SelectObject(pGCPriv->hdcMem, pTilePriv->hBitmap);
            if (hbmpOrig == NULL)
                FatalError("winFillSpans - DRAWABLE_WINDOW - FillTiled - "
                           "SelectObject () failed on pTilePriv->hBitmap\n");

            while (iSpans--) {
                int width = pTile->drawable.width;

                fullX1 = pPoints->x;
                fullY1 = pPoints->y;
                fullX2 = fullX1 + (int) *piWidths;
                pPoints++;
                piWidths++;

                if (fullY1 < extentY1 || extentY2 <= fullY1)
                    continue;

                if (fullX1 < extentX1)
                    fullX1 = extentX1;
                if (fullX2 > extentX2)
                    fullX2 = extentX2;

                if (fullX1 >= fullX2)
                    continue;

                for (iX = fullX1; iX < fullX2; iX += width) {
                    int xoffset;

                    if ((iX + pTile->drawable.width) > fullX2)
                        width = fullX2 - iX;
                    else
                        width = pTile->drawable.width;

                    if (iX == fullX1)
                        xoffset =
                            (fullX1 -
                             (pDrawable->x +
                              (pGC->patOrg.x % pTile->drawable.width) -
                              pTile->drawable.width)) % pTile->drawable.width;
                    else
                        xoffset = 0;

                    if (xoffset + width > pTile->drawable.width)
                        width = pTile->drawable.width - xoffset;

                    BitBlt(pGCPriv->hdc,
                           iX, fullY1,
                           width, 1,
                           pGCPriv->hdcMem,
                           xoffset,
                           (fullY1 -
                            (pDrawable->y +
                             (pGC->patOrg.y % pTile->drawable.height) -
                             pTile->drawable.height)) % pTile->drawable.height,
                           g_copyROP[pGC->alu]);
                }
            }

            /* Push the tile pixmap out of the memory HDC */
            SelectObject(pGCPriv->hdcMem, hbmpOrig);
            break;

        default:
            ErrorF("winFillSpans - DRAWABLE_WINDOW - Unknown fillStyle\n");
            break;
        }

        /* Reset clip region */
        SelectClipRgn(pGCPriv->hdc, NULL);
        break;

    default:
        ErrorF("winFillSpans - Unknown drawable type\n");
        break;
    }
}
