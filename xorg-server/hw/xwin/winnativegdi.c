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
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"

/*
 * Local function prototypes
 */

static Bool
 winAllocateFBNativeGDI(ScreenPtr pScreen);

static void
 winShadowUpdateNativeGDI(ScreenPtr pScreen, shadowBufPtr pBuf);

static Bool
 winCloseScreenNativeGDI(int nIndex, ScreenPtr pScreen);

static Bool
 winInitVisualsNativeGDI(ScreenPtr pScreen);

static Bool
 winAdjustVideoModeNativeGDI(ScreenPtr pScreen);

#if 0
static Bool
 winBltExposedRegionsNativeGDI(ScreenPtr pScreen);
#endif

static Bool
 winActivateAppNativeGDI(ScreenPtr pScreen);

static Bool
 winRedrawScreenNativeGDI(ScreenPtr pScreen);

static Bool
 winRealizeInstalledPaletteNativeGDI(ScreenPtr pScreen);

static Bool
 winInstallColormapNativeGDI(ColormapPtr pColormap);

static Bool
 winStoreColorsNativeGDI(ColormapPtr pmap, int ndef, xColorItem * pdefs);

static Bool
 winCreateColormapNativeGDI(ColormapPtr pColormap);

static Bool
 winDestroyColormapNativeGDI(ColormapPtr pColormap);

static Bool
winAllocateFBNativeGDI(ScreenPtr pScreen)
{
    FatalError("winAllocateFBNativeGDI\n");

    return TRUE;
}

static void
winFreeFBNativeGDI(ScreenPtr pScreen)
{
    FatalError("winFreeFBNativeGDI\n");
}

static Bool
winInitScreenNativeGDI(ScreenPtr pScreen)
{
    FatalError("winInitScreenNativeGDI\n");
}

/*
 * We wrap whatever CloseScreen procedure was specified by fb;
 * a pointer to said procedure is stored in our privates.
 */

static Bool
winCloseScreenNativeGDI(int nIndex, ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;

    ErrorF("winCloseScreenNativeGDI - Freeing screen resources\n");

    /* Flag that the screen is closed */
    pScreenPriv->fClosed = TRUE;
    pScreenPriv->fActive = FALSE;

    /* 
     * NOTE: mi doesn't use a CloseScreen procedure, so we do not
     * need to call a wrapped procedure here.
     */

    /* Delete the window property */
    RemoveProp(pScreenPriv->hwndScreen, WIN_SCR_PROP);

    ErrorF("winCloseScreenNativeGDI - Destroying window\n");

    /* Delete tray icon, if we have one */
    if (!pScreenInfo->fNoTrayIcon)
        winDeleteNotifyIcon(pScreenPriv);

    /* Free the exit confirmation dialog box, if it exists */
    if (g_hDlgExit != NULL) {
        DestroyWindow(g_hDlgExit);
        g_hDlgExit = NULL;
    }

    /* Kill our window */
    if (pScreenPriv->hwndScreen) {
        DestroyWindow(pScreenPriv->hwndScreen);
        pScreenPriv->hwndScreen = NULL;
    }

    /* Invalidate our screeninfo's pointer to the screen */
    pScreenInfo->pScreen = NULL;

    /* Free the screen privates for this screen */
    free(pScreenPriv);

    ErrorF("winCloseScreenNativeGDI - Returning\n");

    return TRUE;
}

static void
winShadowUpdateNativeGDI(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    FatalError("winShadowUpdateNativeGDI\n");
    return;
}

static Bool
winInitVisualsNativeGDI(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;

    /* Set the bitsPerRGB and bit masks */
    switch (pScreenInfo->dwDepth) {
    case 24:
        pScreenPriv->dwBitsPerRGB = 8;
        pScreenPriv->dwRedMask = 0x00FF0000;
        pScreenPriv->dwGreenMask = 0x0000FF00;
        pScreenPriv->dwBlueMask = 0x000000FF;
        break;

    case 16:
        pScreenPriv->dwBitsPerRGB = 6;
        pScreenPriv->dwRedMask = 0xF800;
        pScreenPriv->dwGreenMask = 0x07E0;
        pScreenPriv->dwBlueMask = 0x001F;
        break;

    case 15:
        pScreenPriv->dwBitsPerRGB = 5;
        pScreenPriv->dwRedMask = 0x7C00;
        pScreenPriv->dwGreenMask = 0x03E0;
        pScreenPriv->dwBlueMask = 0x001F;
        break;

    case 8:
        pScreenPriv->dwBitsPerRGB = 8;
        pScreenPriv->dwRedMask = 0;
        pScreenPriv->dwGreenMask = 0;
        pScreenPriv->dwBlueMask = 0;
        break;

    default:
        ErrorF("winInitVisualsNativeGDI - Unknown screen depth\n");
        return FALSE;
        break;
    }

    /* Tell the user how many bits per RGB we are using */
    ErrorF("winInitVisualsNativeGDI - Using dwBitsPerRGB: %d\n",
           (int) pScreenPriv->dwBitsPerRGB);

    /* Create a single visual according to the Windows screen depth */
    switch (pScreenInfo->dwDepth) {
    case 24:
    case 16:
    case 15:
        if (!miSetVisualTypesAndMasks(pScreenInfo->dwDepth,
                                      TrueColorMask,
                                      pScreenPriv->dwBitsPerRGB,
                                      TrueColor,
                                      pScreenPriv->dwRedMask,
                                      pScreenPriv->dwGreenMask,
                                      pScreenPriv->dwBlueMask)) {
            ErrorF("winInitVisuals - miSetVisualTypesAndMasks failed\n");
            return FALSE;
        }
        break;

    case 8:
        ErrorF("winInitVisuals - Calling miSetVisualTypesAndMasks\n");
        if (!miSetVisualTypesAndMasks(pScreenInfo->dwDepth,
                                      StaticColorMask,
                                      pScreenPriv->dwBitsPerRGB,
                                      StaticColor,
                                      pScreenPriv->dwRedMask,
                                      pScreenPriv->dwGreenMask,
                                      pScreenPriv->dwBlueMask)) {
            ErrorF("winInitVisuals - miSetVisualTypesAndMasks failed\n");
            return FALSE;
        }
        break;

    default:
        ErrorF("winInitVisualsNativeGDI - Unknown screen depth\n");
        return FALSE;
    }

#if 1
    ErrorF("winInitVisualsNativeGDI - Returning\n");
#endif

    return TRUE;
}

/* Adjust the video mode */
static Bool
winAdjustVideoModeNativeGDI(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    HDC hdc = NULL;
    DWORD dwBPP;

    hdc = GetDC(NULL);

    /* We're in serious trouble if we can't get a DC */
    if (hdc == NULL) {
        ErrorF("winAdjustVideoModeNativeGDI - GetDC () failed\n");
        return FALSE;
    }

    /* Query GDI for current display depth */
    dwBPP = GetDeviceCaps(hdc, BITSPIXEL);
    pScreenInfo->dwDepth = GetDeviceCaps(hdc, PLANES);

    switch (pScreenInfo->dwDepth) {
    case 24:
    case 16:
    case 15:
    case 8:
        break;
    default:
        if (dwBPP == 32)
            pScreenInfo->dwDepth = 24;
        else
            pScreenInfo->dwDepth = dwBPP;
        break;
    }

    /* GDI cannot change the screen depth, so we'll use GDI's depth */
    pScreenInfo->dwBPP = dwBPP;

    /* Release our DC */
    ReleaseDC(NULL, hdc);

    return TRUE;
}

static Bool
winActivateAppNativeGDI(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);

    /*
     * Are we active?
     * Are we fullscreen?
     */
    if (pScreenPriv != NULL
        && pScreenPriv->fActive
        && pScreenPriv->pScreenInfo && pScreenPriv->pScreenInfo->fFullScreen) {
        /*
         * Activating, attempt to bring our window 
         * to the top of the display
         */
        ShowWindow(pScreenPriv->hwndScreen, SW_RESTORE);
    }

    /*
     * Are we inactive?
     * Are we fullscreen?
     */
    if (pScreenPriv != NULL
        && !pScreenPriv->fActive
        && pScreenPriv->pScreenInfo && pScreenPriv->pScreenInfo->fFullScreen) {
        /*
         * Deactivating, stuff our window onto the
         * task bar.
         */
        ShowWindow(pScreenPriv->hwndScreen, SW_MINIMIZE);
    }

    return TRUE;
}

HBITMAP
winCreateDIBNativeGDI(int iWidth, int iHeight, int iDepth,
                      BYTE ** ppbBits, BITMAPINFO ** ppbmi)
{
    BITMAPINFOHEADER *pbmih = NULL;
    HBITMAP hBitmap = NULL;
    BITMAPINFO *pbmi = NULL;

    /* Don't create an invalid bitmap */
    if (iWidth == 0 || iHeight == 0 || iDepth == 0) {
        ErrorF("\nwinCreateDIBNativeGDI - Invalid specs w %d h %d d %d\n\n",
               iWidth, iHeight, iDepth);
        return NULL;
    }

    /* Allocate bitmap info header */
    pbmih = (BITMAPINFOHEADER *) malloc(sizeof(BITMAPINFOHEADER)
                                        + 256 * sizeof(RGBQUAD));
    if (pbmih == NULL) {
        ErrorF("winCreateDIBNativeGDI - malloc () failed\n");
        return FALSE;
    }
    ZeroMemory(pbmih, sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));

    /* Describe bitmap to be created */
    pbmih->biSize = sizeof(BITMAPINFOHEADER);
    pbmih->biWidth = iWidth;
    pbmih->biHeight = -iHeight;
    pbmih->biPlanes = 1;
    pbmih->biBitCount = iDepth;
    pbmih->biCompression = BI_RGB;
    pbmih->biSizeImage = 0;
    pbmih->biXPelsPerMeter = 0;
    pbmih->biYPelsPerMeter = 0;
    pbmih->biClrUsed = 0;
    pbmih->biClrImportant = 0;

    /* Setup color table for mono DIBs */
    if (iDepth == 1) {
        pbmi = (BITMAPINFO *) pbmih;
        pbmi->bmiColors[1].rgbBlue = 255;
        pbmi->bmiColors[1].rgbGreen = 255;
        pbmi->bmiColors[1].rgbRed = 255;
    }

    /* Create a DIB with a bit pointer */
    hBitmap = CreateDIBSection(NULL,
                               (BITMAPINFO *) pbmih,
                               DIB_RGB_COLORS, (void **) ppbBits, NULL, 0);
    if (hBitmap == NULL) {
        ErrorF("winCreateDIBNativeGDI - CreateDIBSection () failed\n");
        return NULL;
    }

    /* Free the bitmap info header memory */
    if (ppbmi != NULL) {
        /* Store the address of the BMIH in the ppbmih parameter */
        *ppbmi = (BITMAPINFO *) pbmih;
    }
    else {
        free(pbmih);
        pbmih = NULL;
    }

    return hBitmap;
}

#if 0
static Bool
winBltExposedRegionsNativeGDI(ScreenPtr pScreen)
{

    return TRUE;
}
#endif

static Bool
winRedrawScreenNativeGDI(ScreenPtr pScreen)
{
    FatalError("winRedrawScreenNativeGDI\n");
    return TRUE;
}

static Bool
winRealizeInstalledPaletteNativeGDI(ScreenPtr pScreen)
{
    FatalError("winRealizeInstalledPaletteNativeGDI\n");
    return TRUE;
}

static Bool
winInstallColormapNativeGDI(ColormapPtr pColormap)
{
    FatalError("winInstallColormapNativeGDI\n");
    return TRUE;
}

static Bool
winStoreColorsNativeGDI(ColormapPtr pmap, int ndef, xColorItem * pdefs)
{
    FatalError("winStoreColorsNativeGDI\n");
    return TRUE;
}

static Bool
winCreateColormapNativeGDI(ColormapPtr pColormap)
{
    FatalError("winCreateColormapNativeGDI\n");
    return TRUE;
}

static Bool
winDestroyColormapNativeGDI(ColormapPtr pColormap)
{
    FatalError("winDestroyColormapNativeGDI\n");
    return TRUE;
}

/* Set engine specific funtions */
Bool
winSetEngineFunctionsNativeGDI(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;

    /* Set our pointers */
    pScreenPriv->pwinAllocateFB = winAllocateFBNativeGDI;
    pScreenPriv->pwinFreeFB = winFreeFBNativeGDI;
    pScreenPriv->pwinShadowUpdate = winShadowUpdateNativeGDI;
    pScreenPriv->pwinInitScreen = winInitScreenNativeGDI;
    pScreenPriv->pwinCloseScreen = winCloseScreenNativeGDI;
    pScreenPriv->pwinInitVisuals = winInitVisualsNativeGDI;
    pScreenPriv->pwinAdjustVideoMode = winAdjustVideoModeNativeGDI;
    if (pScreenInfo->fFullScreen)
        pScreenPriv->pwinCreateBoundingWindow =
            winCreateBoundingWindowFullScreen;
    else
        pScreenPriv->pwinCreateBoundingWindow = winCreateBoundingWindowWindowed;
    pScreenPriv->pwinFinishScreenInit = winFinishScreenInitNativeGDI;
    /*
     * WARNING: Do not set the BltExposedRegions procedure pointer to anything
     * other than NULL until a working painting procedure is in place.
     * Else, winWindowProc will get stuck in an infinite loop because
     * Windows expects the BeginPaint and EndPaint functions to be called
     * before a WM_PAINT message can be removed from the queue.  We are
     * using NULL here as a signal for winWindowProc that it should
     * not signal that the WM_PAINT message has been processed.
     */
    pScreenPriv->pwinBltExposedRegions = NULL;
    pScreenPriv->pwinActivateApp = winActivateAppNativeGDI;
    pScreenPriv->pwinRedrawScreen = winRedrawScreenNativeGDI;
    pScreenPriv->pwinRealizeInstalledPalette =
        winRealizeInstalledPaletteNativeGDI;
    pScreenPriv->pwinInstallColormap = winInstallColormapNativeGDI;
    pScreenPriv->pwinStoreColors = winStoreColorsNativeGDI;
    pScreenPriv->pwinCreateColormap = winCreateColormapNativeGDI;
    pScreenPriv->pwinDestroyColormap = winDestroyColormapNativeGDI;
    pScreenPriv->pwinHotKeyAltTab =
        (winHotKeyAltTabProcPtr) (void (*)(void)) NoopDDA;

    return TRUE;
}
