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
 * Authors:	Dakshinamurthy Karra
 *		Suhaib M Siddiqi
 *		Peter Busch
 *		Harold L Hunt II
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"

/*
 * Local function prototypes
 */

static Bool
 winAllocateFBPrimaryDD(ScreenPtr pScreen);

static Bool
 winCloseScreenPrimaryDD(ScreenPtr pScreen);

static Bool
 winInitVisualsPrimaryDD(ScreenPtr pScreen);

static Bool
 winAdjustVideoModePrimaryDD(ScreenPtr pScreen);

static Bool
 winActivateAppPrimaryDD(ScreenPtr pScreen);

static Bool
 winHotKeyAltTabPrimaryDD(ScreenPtr pScreen);

/*
 * Create a DirectDraw primary surface 
 */

static Bool
winAllocateFBPrimaryDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    HRESULT ddrval = DD_OK;
    DDSURFACEDESC ddsd;
    DDSURFACEDESC *pddsdPrimary = NULL;
    DDSURFACEDESC *pddsdOffscreen = NULL;
    RECT rcClient;

    ErrorF("winAllocateFBPrimaryDD\n");

    /* Get client area location in screen coords */
    GetClientRect(pScreenPriv->hwndScreen, &rcClient);
    MapWindowPoints(pScreenPriv->hwndScreen,
                    HWND_DESKTOP, (LPPOINT) &rcClient, 2);

    /* Create a DirectDraw object, store the address at lpdd */
    ddrval = (*g_fpDirectDrawCreate) (NULL, &pScreenPriv->pdd, NULL);
    if (ddrval != DD_OK)
        FatalError("winAllocateFBPrimaryDD - Could not start DirectDraw\n");

    /* Get a DirectDraw2 interface pointer */
    ddrval = IDirectDraw_QueryInterface(pScreenPriv->pdd,
                                        &IID_IDirectDraw2,
                                        (LPVOID *) &pScreenPriv->pdd2);
    if (FAILED(ddrval)) {
        ErrorF("winAllocateFBShadowDD - Failed DD2 query: %08x\n",
               (unsigned int) ddrval);
        return FALSE;
    }

    ErrorF("winAllocateFBPrimaryDD - Created and initialized DD\n");

    /* Are we windowed or fullscreen? */
    if (pScreenInfo->fFullScreen) {
        /* Full screen mode */
        ddrval = IDirectDraw2_SetCooperativeLevel(pScreenPriv->pdd2,
                                                  pScreenPriv->hwndScreen,
                                                  DDSCL_FULLSCREEN
                                                  | DDSCL_EXCLUSIVE);
        if (FAILED(ddrval))
            FatalError("winAllocateFBPrimaryDD - Could not set "
                       "cooperative level\n");

        /* Change the video mode to the mode requested */
        ddrval = IDirectDraw2_SetDisplayMode(pScreenPriv->pdd2,
                                             pScreenInfo->dwWidth,
                                             pScreenInfo->dwHeight,
                                             pScreenInfo->dwBPP,
                                             pScreenInfo->dwRefreshRate, 0);
        if (FAILED(ddrval))
            FatalError("winAllocateFBPrimaryDD - Could not set "
                       "full screen display mode\n");
    }
    else {
        /* Windowed mode */
        ddrval = IDirectDraw2_SetCooperativeLevel(pScreenPriv->pdd2,
                                                  pScreenPriv->hwndScreen,
                                                  DDSCL_NORMAL);
        if (FAILED(ddrval))
            FatalError("winAllocateFBPrimaryDD - Could not set "
                       "cooperative level\n");
    }

    /* Describe the primary surface */
    ZeroMemory(&ddsd, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    /* Create the primary surface */
    ddrval = IDirectDraw2_CreateSurface(pScreenPriv->pdd2,
                                        &ddsd, &pScreenPriv->pddsPrimary, NULL);
    if (FAILED(ddrval))
        FatalError("winAllocateFBPrimaryDD - Could not create primary "
                   "surface %08x\n", (unsigned int) ddrval);

    ErrorF("winAllocateFBPrimaryDD - Created primary\n");

    /* Allocate a DD surface description for our screen privates */
    pddsdPrimary = pScreenPriv->pddsdPrimary = malloc(sizeof(DDSURFACEDESC));
    if (pddsdPrimary == NULL)
        FatalError("winAllocateFBPrimaryDD - Could not allocate surface "
                   "description memory\n");
    ZeroMemory(pddsdPrimary, sizeof(*pddsdPrimary));
    pddsdPrimary->dwSize = sizeof(*pddsdPrimary);

    /* Describe the offscreen surface to be created */
    /*
     * NOTE: Do not use a DDSCAPS_VIDEOMEMORY surface,
     * as drawing, locking, and unlocking take forever
     * with video memory surfaces.  In addition,
     * video memory is a somewhat scarce resource,
     * so you shouldn't be allocating video memory when
     * you have the option of using system memory instead.
     */
    ZeroMemory(&ddsd, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    ddsd.dwHeight = pScreenInfo->dwHeight;
    ddsd.dwWidth = pScreenInfo->dwWidth;

    /* Create the shadow surface */
    ddrval = IDirectDraw2_CreateSurface(pScreenPriv->pdd2,
                                        &ddsd,
                                        &pScreenPriv->pddsOffscreen, NULL);
    if (ddrval != DD_OK)
        FatalError("winAllocateFBPrimaryDD - Could not create shadow "
                   "surface\n");

    ErrorF("winAllocateFBPrimaryDD - Created offscreen\n");

    /* Allocate a DD surface description for our screen privates */
    pddsdOffscreen = pScreenPriv->pddsdOffscreen
        = malloc(sizeof(DDSURFACEDESC));
    if (pddsdOffscreen == NULL)
        FatalError("winAllocateFBPrimaryDD - Could not allocate surface "
                   "description memory\n");
    ZeroMemory(pddsdOffscreen, sizeof(*pddsdOffscreen));
    pddsdOffscreen->dwSize = sizeof(*pddsdOffscreen);

    ErrorF("winAllocateFBPrimaryDD - Locking primary\n");

    /* Lock the primary surface */
    ddrval = IDirectDrawSurface2_Lock(pScreenPriv->pddsPrimary,
                                      pScreenInfo->
                                      fFullScreen ? NULL : &rcClient,
                                      pddsdPrimary, DDLOCK_WAIT, NULL);
    if (ddrval != DD_OK || pddsdPrimary->lpSurface == NULL)
        FatalError("winAllocateFBPrimaryDD - Could not lock "
                   "primary surface\n");

    ErrorF("winAllocateFBPrimaryDD - Locked primary\n");

    /* We don't know how to deal with anything other than RGB */
    if (!(pddsdPrimary->ddpfPixelFormat.dwFlags & DDPF_RGB))
        FatalError("winAllocateFBPrimaryDD - Color format other than RGB\n");

    /* Grab the pitch from the surface desc */
    pScreenInfo->dwStride = (pddsdPrimary->u1.lPitch * 8)
        / pScreenInfo->dwBPP;

    /* Save the pointer to our surface memory */
    pScreenInfo->pfb = pddsdPrimary->lpSurface;

    /* Grab the color depth and masks from the surface description */
    pScreenPriv->dwRedMask = pddsdPrimary->ddpfPixelFormat.u2.dwRBitMask;
    pScreenPriv->dwGreenMask = pddsdPrimary->ddpfPixelFormat.u3.dwGBitMask;
    pScreenPriv->dwBlueMask = pddsdPrimary->ddpfPixelFormat.u4.dwBBitMask;

    ErrorF("winAllocateFBPrimaryDD - Returning\n");

    return TRUE;
}

static void
winFreeFBPrimaryDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;

    /* Free the offscreen surface, if there is one */
    if (pScreenPriv->pddsOffscreen) {
        IDirectDrawSurface2_Unlock(pScreenPriv->pddsOffscreen, NULL);
        IDirectDrawSurface2_Release(pScreenPriv->pddsOffscreen);
        pScreenPriv->pddsOffscreen = NULL;
    }

    /* Release the primary surface, if there is one */
    if (pScreenPriv->pddsPrimary) {
        IDirectDrawSurface2_Unlock(pScreenPriv->pddsPrimary, NULL);
        IDirectDrawSurface2_Release(pScreenPriv->pddsPrimary);
        pScreenPriv->pddsPrimary = NULL;
    }

    /* Free the DirectDraw object, if there is one */
    if (pScreenPriv->pdd) {
        IDirectDraw2_RestoreDisplayMode(pScreenPriv->pdd);
        IDirectDraw2_Release(pScreenPriv->pdd);
        pScreenPriv->pdd = NULL;
    }

    /* Invalidate the ScreenInfo's fb pointer */
    pScreenInfo->pfb = NULL;
}

static Bool
winInitScreenPrimaryDD(ScreenPtr pScreen)
{
    return winAllocateFBPrimaryDD(pScreen);
}

/*
 * Call the wrapped CloseScreen function.
 * 
 * Free our resources and private structures.
 */

static Bool
winCloseScreenPrimaryDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    Bool fReturn;

    ErrorF("winCloseScreenPrimaryDD - Freeing screen resources\n");

    /* Flag that the screen is closed */
    pScreenPriv->fClosed = TRUE;
    pScreenPriv->fActive = FALSE;

    /* Call the wrapped CloseScreen procedure */
    WIN_UNWRAP(CloseScreen);
    if (pScreen->CloseScreen)
        fReturn = (*pScreen->CloseScreen) (pScreen);

    /* Delete the window property */
    RemoveProp(pScreenPriv->hwndScreen, WIN_SCR_PROP);

    winFreeFBPrimaryDD(pScreen);

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

    /* Kill our screeninfo's pointer to the screen */
    pScreenInfo->pScreen = NULL;

    /* Free the screen privates for this screen */
    free((void *) pScreenPriv);

    return fReturn;
}

/*
 * Tell mi what sort of visuals we need.
 * 
 * Generally we only need one visual, as our screen can only
 * handle one format at a time, I believe.  You may want
 * to verify that last sentence.
 */

static Bool
winInitVisualsPrimaryDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    DWORD dwRedBits, dwGreenBits, dwBlueBits;

    /* Count the number of ones in each color mask */
    dwRedBits = winCountBits(pScreenPriv->dwRedMask);
    dwGreenBits = winCountBits(pScreenPriv->dwGreenMask);
    dwBlueBits = winCountBits(pScreenPriv->dwBlueMask);

    /* Store the maximum number of ones in a color mask as the bitsPerRGB */
    if (dwRedBits > dwGreenBits && dwRedBits > dwBlueBits)
        pScreenPriv->dwBitsPerRGB = dwRedBits;
    else if (dwGreenBits > dwRedBits && dwGreenBits > dwBlueBits)
        pScreenPriv->dwBitsPerRGB = dwGreenBits;
    else
        pScreenPriv->dwBitsPerRGB = dwBlueBits;

    ErrorF("winInitVisualsPrimaryDD - Masks: %08x %08x %08x bpRGB: %d\n",
           (unsigned int) pScreenPriv->dwRedMask,
           (unsigned int) pScreenPriv->dwGreenMask,
           (unsigned int) pScreenPriv->dwBlueMask,
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
            ErrorF("winInitVisualsPrimaryDD - "
                   "miSetVisualTypesAndMasks failed\n");
            return FALSE;
        }
        break;

    case 8:
#if CYGDEBUG
        winDebug("winInitVisuals - Calling miSetVisualTypesAndMasks\n");
#endif                          /* CYGDEBUG */
        if (!miSetVisualTypesAndMasks(pScreenInfo->dwDepth,
                                      PseudoColorMask,
                                      pScreenPriv->dwBitsPerRGB,
                                      PseudoColor,
                                      pScreenPriv->dwRedMask,
                                      pScreenPriv->dwGreenMask,
                                      pScreenPriv->dwBlueMask)) {
            ErrorF("winInitVisualsPrimaryDD - "
                   "miSetVisualTypesAndMasks failed\n");
            return FALSE;
        }
#if CYGDEBUG
        winDebug("winInitVisualsPrimaryDD - Returned from "
                 "miSetVisualTypesAndMasks\n");
#endif                          /* CYGDEBUG */
        break;

    default:
        ErrorF("winInitVisualsPrimaryDD - Unknown screen depth\n");
        return FALSE;
    }

    ErrorF("winInitVisualsPrimaryDD - Returning\n");

    return TRUE;
}

static Bool
winAdjustVideoModePrimaryDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    HDC hdc = NULL;
    DWORD dwBPP;

    /* We're in serious trouble if we can't get a DC */
    hdc = GetDC(NULL);
    if (hdc == NULL) {
        ErrorF("winAdjustVideoModePrimaryDD - GetDC failed\n");
        return FALSE;
    }

    /* Query GDI for current display depth */
    dwBPP = GetDeviceCaps(hdc, BITSPIXEL);

    /* DirectDraw can only change the depth in fullscreen mode */
    if (!(pScreenInfo->fFullScreen && (pScreenInfo->dwBPP != WIN_DEFAULT_BPP))) {
        /* Otherwise, We'll use GDI's depth */
        pScreenInfo->dwBPP = dwBPP;
    }

    /* Release our DC */
    ReleaseDC(NULL, hdc);

    return TRUE;
}

/*
 * We need to blit our offscreen fb to
 * the screen when we are activated, and we need to point
 * the fb code back to the primary surface memory.
 */

static Bool
winActivateAppPrimaryDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    RECT rcSrc, rcClient;
    HRESULT ddrval = DD_OK;

    /* Check for errors */
    if (pScreenPriv == NULL
        || pScreenPriv->pScreenInfo == NULL
        || pScreenPriv->pddsPrimary == NULL
        || pScreenPriv->pddsOffscreen == NULL)
        return FALSE;

    /* Check for do-nothing */
    if (!pScreenPriv->fActive)
        return TRUE;

    /* We are activating */
    ddrval = IDirectDrawSurface2_IsLost(pScreenPriv->pddsOffscreen);
    if (ddrval == DD_OK) {
        IDirectDrawSurface2_Unlock(pScreenPriv->pddsOffscreen, NULL);
        /*
         * We don't check for an error from Unlock, because it
         * doesn't matter if the Unlock failed.
         */
    }

    /* Restore both surfaces, just cause I like it that way */
    IDirectDrawSurface2_Restore(pScreenPriv->pddsOffscreen);
    IDirectDrawSurface2_Restore(pScreenPriv->pddsPrimary);

    /* Get client area in screen coords */
    GetClientRect(pScreenPriv->hwndScreen, &rcClient);
    MapWindowPoints(pScreenPriv->hwndScreen,
                    HWND_DESKTOP, (LPPOINT) &rcClient, 2);

    /* Setup a source rectangle */
    rcSrc.left = 0;
    rcSrc.top = 0;
    rcSrc.right = pScreenPriv->pScreenInfo->dwWidth;
    rcSrc.bottom = pScreenPriv->pScreenInfo->dwHeight;

    ddrval = IDirectDrawSurface2_Blt(pScreenPriv->pddsPrimary,
                                     &rcClient,
                                     pScreenPriv->pddsOffscreen,
                                     &rcSrc, DDBLT_WAIT, NULL);
    if (ddrval != DD_OK)
        FatalError("winActivateAppPrimaryDD () - Failed blitting offscreen "
                   "surface to primary surface %08x\n", (unsigned int) ddrval);

    /* Lock the primary surface */
    ddrval = IDirectDrawSurface2_Lock(pScreenPriv->pddsPrimary,
                                      &rcClient,
                                      pScreenPriv->pddsdPrimary,
                                      DDLOCK_WAIT, NULL);
    if (ddrval != DD_OK || pScreenPriv->pddsdPrimary->lpSurface == NULL)
        FatalError("winActivateAppPrimaryDD () - Could not lock "
                   "primary surface\n");

    /* Notify FB of the new memory pointer */
    winUpdateFBPointer(pScreen, pScreenPriv->pddsdPrimary->lpSurface);

    /*
     * Register the Alt-Tab combo as a hotkey so we can copy
     * the primary framebuffer before the display mode changes
     */
    RegisterHotKey(pScreenPriv->hwndScreen, 1, MOD_ALT, 9);

    return TRUE;
}

/*
 * Handle the Alt+Tab hotkey.
 *
 * We need to save the primary fb to an offscreen fb when
 * we get deactivated, and point the fb code at the offscreen
 * fb for the duration of the deactivation.
 */

static Bool
winHotKeyAltTabPrimaryDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    HRESULT ddrval = DD_OK;

    ErrorF("\nwinHotKeyAltTabPrimaryDD\n\n");

    /* Alt+Tab was pressed, we will lose focus very soon */
    pScreenPriv->fActive = FALSE;

    /* Check for error conditions */
    if (pScreenPriv->pddsPrimary == NULL || pScreenPriv->pddsOffscreen == NULL)
        return FALSE;

    /* Did we loose the primary surface? */
    ddrval = IDirectDrawSurface2_IsLost(pScreenPriv->pddsPrimary);
    if (ddrval == DD_OK) {
        ddrval = IDirectDrawSurface2_Unlock(pScreenPriv->pddsPrimary, NULL);
        if (FAILED(ddrval))
            FatalError("winHotKeyAltTabPrimaryDD - Failed unlocking primary "
                       "surface\n");
    }

    /* Blit the primary surface to the offscreen surface */
    ddrval = IDirectDrawSurface2_Blt(pScreenPriv->pddsOffscreen, NULL,  /* should be rcDest */
                                     pScreenPriv->pddsPrimary,
                                     NULL, DDBLT_WAIT, NULL);
    if (ddrval == DDERR_SURFACELOST) {
        IDirectDrawSurface2_Restore(pScreenPriv->pddsOffscreen);
        IDirectDrawSurface2_Restore(pScreenPriv->pddsPrimary);

        /* Blit the primary surface to the offscreen surface */
        ddrval = IDirectDrawSurface2_Blt(pScreenPriv->pddsOffscreen,
                                         NULL,
                                         pScreenPriv->pddsPrimary,
                                         NULL, DDBLT_WAIT, NULL);
        if (FAILED(ddrval))
            FatalError("winHotKeyAltTabPrimaryDD - Failed blitting primary "
                       "surface to offscreen surface: %08x\n",
                       (unsigned int) ddrval);
    }
    else {
        FatalError("winHotKeyAltTabPrimaryDD - Unknown error from "
                   "Blt: %08dx\n", (unsigned int) ddrval);
    }

    /* Lock the offscreen surface */
    ddrval = IDirectDrawSurface2_Lock(pScreenPriv->pddsOffscreen,
                                      NULL,
                                      pScreenPriv->pddsdOffscreen,
                                      DDLOCK_WAIT, NULL);
    if (ddrval != DD_OK || pScreenPriv->pddsdPrimary->lpSurface == NULL)
        FatalError("winHotKeyAltTabPrimaryDD - Could not lock "
                   "offscreen surface\n");

    /* Notify FB of the new memory pointer */
    winUpdateFBPointer(pScreen, pScreenPriv->pddsdOffscreen->lpSurface);

    /* Unregister our hotkey */
    UnregisterHotKey(pScreenPriv->hwndScreen, 1);

    return TRUE;
}

/* Set engine specific functions */
Bool
winSetEngineFunctionsPrimaryDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;

    /* Set our pointers */
    pScreenPriv->pwinAllocateFB = winAllocateFBPrimaryDD;
    pScreenPriv->pwinFreeFB = winFreeFBPrimaryDD;
    pScreenPriv->pwinShadowUpdate =
        (winShadowUpdateProcPtr) (void (*)(void)) NoopDDA;
    pScreenPriv->pwinInitScreen = winInitScreenPrimaryDD;
    pScreenPriv->pwinCloseScreen = winCloseScreenPrimaryDD;
    pScreenPriv->pwinInitVisuals = winInitVisualsPrimaryDD;
    pScreenPriv->pwinAdjustVideoMode = winAdjustVideoModePrimaryDD;
    if (pScreenInfo->fFullScreen)
        pScreenPriv->pwinCreateBoundingWindow =
            winCreateBoundingWindowFullScreen;
    else
        pScreenPriv->pwinCreateBoundingWindow = winCreateBoundingWindowWindowed;
    pScreenPriv->pwinFinishScreenInit = winFinishScreenInitFB;
    pScreenPriv->pwinBltExposedRegions =
        (winBltExposedRegionsProcPtr) (void (*)(void)) NoopDDA;
    pScreenPriv->pwinActivateApp = winActivateAppPrimaryDD;
    pScreenPriv->pwinRedrawScreen = NULL;
    pScreenPriv->pwinRealizeInstalledPalette = NULL;
    pScreenPriv->pwinInstallColormap = NULL;
    pScreenPriv->pwinStoreColors = NULL;
    pScreenPriv->pwinCreateColormap = NULL;
    pScreenPriv->pwinDestroyColormap = NULL;
    pScreenPriv->pwinHotKeyAltTab = winHotKeyAltTabPrimaryDD;
    pScreenPriv->pwinCreatePrimarySurface =
        (winCreatePrimarySurfaceProcPtr) (void (*)(void)) NoopDDA;
    pScreenPriv->pwinReleasePrimarySurface =
        (winReleasePrimarySurfaceProcPtr) (void (*)(void)) NoopDDA;
#ifdef XWIN_MULTIWINDOW
    pScreenPriv->pwinFinishCreateWindowsWindow =
        (winFinishCreateWindowsWindowProcPtr) (void (*)(void)) NoopDDA;
#endif

    return TRUE;
}
