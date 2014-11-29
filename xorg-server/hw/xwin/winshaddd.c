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
 * Local prototypes
 */

static Bool
 winAllocateFBShadowDD(ScreenPtr pScreen);

static void
 winShadowUpdateDD(ScreenPtr pScreen, shadowBufPtr pBuf);

static Bool
 winCloseScreenShadowDD(ScreenPtr pScreen);

static Bool
 winInitVisualsShadowDD(ScreenPtr pScreen);

static Bool
 winAdjustVideoModeShadowDD(ScreenPtr pScreen);

static Bool
 winBltExposedRegionsShadowDD(ScreenPtr pScreen);

static Bool
 winActivateAppShadowDD(ScreenPtr pScreen);

static Bool
 winRedrawScreenShadowDD(ScreenPtr pScreen);

static Bool
 winRealizeInstalledPaletteShadowDD(ScreenPtr pScreen);

static Bool
 winInstallColormapShadowDD(ColormapPtr pColormap);

static Bool
 winStoreColorsShadowDD(ColormapPtr pmap, int ndef, xColorItem * pdefs);

static Bool
 winCreateColormapShadowDD(ColormapPtr pColormap);

static Bool
 winDestroyColormapShadowDD(ColormapPtr pColormap);

static Bool
 winCreatePrimarySurfaceShadowDD(ScreenPtr pScreen);

static Bool
 winReleasePrimarySurfaceShadowDD(ScreenPtr pScreen);

/*
 * Create the primary surface and attach the clipper.
 * Used for both the initial surface creation and during
 * WM_DISPLAYCHANGE messages.
 */

static Bool
winCreatePrimarySurfaceShadowDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    HRESULT ddrval = DD_OK;
    DDSURFACEDESC ddsd;

    /* Describe the primary surface */
    ZeroMemory(&ddsd, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    /* Create the primary surface */
    ddrval = IDirectDraw2_CreateSurface(pScreenPriv->pdd2,
                                        &ddsd, &pScreenPriv->pddsPrimary, NULL);
    if (FAILED(ddrval)) {
        ErrorF("winCreatePrimarySurfaceShadowDD - Could not create primary "
               "surface: %08x\n", (unsigned int) ddrval);
        return FALSE;
    }

#if CYGDEBUG
    winDebug("winCreatePrimarySurfaceShadowDD - Created primary surface\n");
#endif

    /*
     * Attach a clipper to the primary surface that will clip our blits to our
     * display window.
     */
    ddrval = IDirectDrawSurface2_SetClipper(pScreenPriv->pddsPrimary,
                                            pScreenPriv->pddcPrimary);
    if (FAILED(ddrval)) {
        ErrorF("winCreatePrimarySurfaceShadowDD - Primary attach clipper "
               "failed: %08x\n", (unsigned int) ddrval);
        return FALSE;
    }

#if CYGDEBUG
    winDebug("winCreatePrimarySurfaceShadowDD - Attached clipper to "
             "primary surface\n");
#endif

    /* Everything was correct */
    return TRUE;
}

/*
 * Detach the clipper and release the primary surface.
 * Called from WM_DISPLAYCHANGE.
 */

static Bool
winReleasePrimarySurfaceShadowDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);

    ErrorF("winReleasePrimarySurfaceShadowDD - Hello\n");

    /* Release the primary surface and clipper, if they exist */
    if (pScreenPriv->pddsPrimary) {
        /*
         * Detach the clipper from the primary surface.
         * NOTE: We do this explicity for clarity.  The Clipper is not released.
         */
        IDirectDrawSurface2_SetClipper(pScreenPriv->pddsPrimary, NULL);

        ErrorF("winReleasePrimarySurfaceShadowDD - Detached clipper\n");

        /* Release the primary surface */
        IDirectDrawSurface2_Release(pScreenPriv->pddsPrimary);
        pScreenPriv->pddsPrimary = NULL;
    }

    ErrorF("winReleasePrimarySurfaceShadowDD - Released primary surface\n");

    return TRUE;
}

/*
 * Create a DirectDraw surface for the shadow framebuffer; also create
 * a primary surface object so we can blit to the display.
 *
 * Install a DirectDraw clipper on our primary surface object
 * that clips our blits to the unobscured client area of our display window.
 */

static Bool
winAllocateFBShadowDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    HRESULT ddrval = DD_OK;
    DDSURFACEDESC ddsd;
    DDSURFACEDESC *pddsdShadow = NULL;

#if CYGDEBUG
    winDebug("winAllocateFBShadowDD\n");
#endif

    /* Create a clipper */
    ddrval = (*g_fpDirectDrawCreateClipper) (0,
                                             &pScreenPriv->pddcPrimary, NULL);
    if (FAILED(ddrval)) {
        ErrorF("winAllocateFBShadowDD - Could not create clipper: %08x\n",
               (unsigned int) ddrval);
        return FALSE;
    }

#if CYGDEBUG
    winDebug("winAllocateFBShadowDD - Created a clipper\n");
#endif

    /* Attach the clipper to our display window */
    ddrval = IDirectDrawClipper_SetHWnd(pScreenPriv->pddcPrimary,
                                        0, pScreenPriv->hwndScreen);
    if (FAILED(ddrval)) {
        ErrorF("winAllocateFBShadowDD - Clipper not attached to "
               "window: %08x\n", (unsigned int) ddrval);
        return FALSE;
    }

#if CYGDEBUG
    winDebug("winAllocateFBShadowDD - Attached clipper to window\n");
#endif

    /* Create a DirectDraw object, store the address at lpdd */
    ddrval = (*g_fpDirectDrawCreate) (NULL, &pScreenPriv->pdd, NULL);
    if (FAILED(ddrval)) {
        ErrorF("winAllocateFBShadowDD - Could not start DirectDraw: %08x\n",
               (unsigned int) ddrval);
        return FALSE;
    }

#if CYGDEBUG
    winDebug("winAllocateFBShadowDD () - Created and initialized DD\n");
#endif

    /* Get a DirectDraw2 interface pointer */
    ddrval = IDirectDraw_QueryInterface(pScreenPriv->pdd,
                                        &IID_IDirectDraw2,
                                        (LPVOID *) &pScreenPriv->pdd2);
    if (FAILED(ddrval)) {
        ErrorF("winAllocateFBShadowDD - Failed DD2 query: %08x\n",
               (unsigned int) ddrval);
        return FALSE;
    }

    /* Are we full screen? */
    if (pScreenInfo->fFullScreen) {
        DDSURFACEDESC ddsdCurrent;
        DWORD dwRefreshRateCurrent = 0;
        HDC hdc = NULL;

        /* Set the cooperative level to full screen */
        ddrval = IDirectDraw2_SetCooperativeLevel(pScreenPriv->pdd2,
                                                  pScreenPriv->hwndScreen,
                                                  DDSCL_EXCLUSIVE
                                                  | DDSCL_FULLSCREEN);
        if (FAILED(ddrval)) {
            ErrorF("winAllocateFBShadowDD - Could not set "
                   "cooperative level: %08x\n", (unsigned int) ddrval);
            return FALSE;
        }

        /*
         * We only need to get the current refresh rate for comparison
         * if a refresh rate has been passed on the command line.
         */
        if (pScreenInfo->dwRefreshRate != 0) {
            ZeroMemory(&ddsdCurrent, sizeof(ddsdCurrent));
            ddsdCurrent.dwSize = sizeof(ddsdCurrent);

            /* Get information about current display settings */
            ddrval = IDirectDraw2_GetDisplayMode(pScreenPriv->pdd2,
                                                 &ddsdCurrent);
            if (FAILED(ddrval)) {
                ErrorF("winAllocateFBShadowDD - Could not get current "
                       "refresh rate: %08x.  Continuing.\n",
                       (unsigned int) ddrval);
                dwRefreshRateCurrent = 0;
            }
            else {
                /* Grab the current refresh rate */
                dwRefreshRateCurrent = ddsdCurrent.u2.dwRefreshRate;
            }
        }

        /* Clean up the refresh rate */
        if (dwRefreshRateCurrent == pScreenInfo->dwRefreshRate) {
            /*
             * Refresh rate is non-specified or equal to current.
             */
            pScreenInfo->dwRefreshRate = 0;
        }

        /* Grab a device context for the screen */
        hdc = GetDC(NULL);
        if (hdc == NULL) {
            ErrorF("winAllocateFBShadowDD - GetDC () failed\n");
            return FALSE;
        }

        /* Only change the video mode when different than current mode */
        if (!pScreenInfo->fMultipleMonitors
            && (pScreenInfo->dwWidth != GetSystemMetrics(SM_CXSCREEN)
                || pScreenInfo->dwHeight != GetSystemMetrics(SM_CYSCREEN)
                || pScreenInfo->dwBPP != GetDeviceCaps(hdc, BITSPIXEL)
                || pScreenInfo->dwRefreshRate != 0)) {
            ErrorF("winAllocateFBShadowDD - Changing video mode\n");

            /* Change the video mode to the mode requested, and use the driver default refresh rate on failure */
            ddrval = IDirectDraw2_SetDisplayMode(pScreenPriv->pdd2,
                                                 pScreenInfo->dwWidth,
                                                 pScreenInfo->dwHeight,
                                                 pScreenInfo->dwBPP,
                                                 pScreenInfo->dwRefreshRate, 0);
            if (FAILED(ddrval)) {
                ErrorF("winAllocateFBShadowDD - Could not set "
                       "full screen display mode: %08x\n",
                       (unsigned int) ddrval);
                ErrorF
                    ("winAllocateFBShadowDD - Using default driver refresh rate\n");
                ddrval =
                    IDirectDraw2_SetDisplayMode(pScreenPriv->pdd2,
                                                pScreenInfo->dwWidth,
                                                pScreenInfo->dwHeight,
                                                pScreenInfo->dwBPP, 0, 0);
                if (FAILED(ddrval)) {
                    ErrorF
                        ("winAllocateFBShadowDD - Could not set default refresh rate "
                         "full screen display mode: %08x\n",
                         (unsigned int) ddrval);
                    return FALSE;
                }
            }
        }
        else {
            ErrorF("winAllocateFBShadowDD - Not changing video mode\n");
        }

        /* Release our DC */
        ReleaseDC(NULL, hdc);
        hdc = NULL;
    }
    else {
        /* Set the cooperative level for windowed mode */
        ddrval = IDirectDraw2_SetCooperativeLevel(pScreenPriv->pdd2,
                                                  pScreenPriv->hwndScreen,
                                                  DDSCL_NORMAL);
        if (FAILED(ddrval)) {
            ErrorF("winAllocateFBShadowDD - Could not set "
                   "cooperative level: %08x\n", (unsigned int) ddrval);
            return FALSE;
        }
    }

    /* Create the primary surface */
    if (!winCreatePrimarySurfaceShadowDD(pScreen)) {
        ErrorF("winAllocateFBShadowDD - winCreatePrimarySurfaceShadowDD "
               "failed\n");
        return FALSE;
    }

    /* Describe the shadow surface to be created */
    /* NOTE: Do not use a DDSCAPS_VIDEOMEMORY surface,
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
                                        &ddsd, &pScreenPriv->pddsShadow, NULL);
    if (FAILED(ddrval)) {
        ErrorF("winAllocateFBShadowDD - Could not create shadow "
               "surface: %08x\n", (unsigned int) ddrval);
        return FALSE;
    }

#if CYGDEBUG
    winDebug("winAllocateFBShadowDD - Created shadow\n");
#endif

    /* Allocate a DD surface description for our screen privates */
    pddsdShadow = pScreenPriv->pddsdShadow = malloc(sizeof(DDSURFACEDESC));
    if (pddsdShadow == NULL) {
        ErrorF("winAllocateFBShadowDD - Could not allocate surface "
               "description memory\n");
        return FALSE;
    }
    ZeroMemory(pddsdShadow, sizeof(*pddsdShadow));
    pddsdShadow->dwSize = sizeof(*pddsdShadow);

#if CYGDEBUG
    winDebug("winAllocateFBShadowDD - Locking shadow\n");
#endif

    /* Lock the shadow surface */
    ddrval = IDirectDrawSurface2_Lock(pScreenPriv->pddsShadow,
                                      NULL, pddsdShadow, DDLOCK_WAIT, NULL);
    if (FAILED(ddrval) || pddsdShadow->lpSurface == NULL) {
        ErrorF("winAllocateFBShadowDD - Could not lock shadow "
               "surface: %08x\n", (unsigned int) ddrval);
        return FALSE;
    }

#if CYGDEBUG
    winDebug("winAllocateFBShadowDD - Locked shadow\n");
#endif

    /* We don't know how to deal with anything other than RGB */
    if (!(pddsdShadow->ddpfPixelFormat.dwFlags & DDPF_RGB)) {
        ErrorF("winAllocateFBShadowDD - Color format other than RGB\n");
        return FALSE;
    }

    /* Grab the pitch from the surface desc */
    pScreenInfo->dwStride = (pddsdShadow->u1.lPitch * 8)
        / pScreenInfo->dwBPP;

    /* Save the pointer to our surface memory */
    pScreenInfo->pfb = pddsdShadow->lpSurface;

    /* Grab the color depth and masks from the surface description */
    pScreenPriv->dwRedMask = pddsdShadow->ddpfPixelFormat.u2.dwRBitMask;
    pScreenPriv->dwGreenMask = pddsdShadow->ddpfPixelFormat.u3.dwGBitMask;
    pScreenPriv->dwBlueMask = pddsdShadow->ddpfPixelFormat.u4.dwBBitMask;

#if CYGDEBUG
    winDebug("winAllocateFBShadowDD - Returning\n");
#endif

    return TRUE;
}

static void
winFreeFBShadowDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;

    /* Free the shadow surface, if there is one */
    if (pScreenPriv->pddsShadow) {
        IDirectDrawSurface2_Unlock(pScreenPriv->pddsShadow, NULL);
        IDirectDrawSurface2_Release(pScreenPriv->pddsShadow);
        pScreenPriv->pddsShadow = NULL;
    }

    /* Detach the clipper from the primary surface and release the primary surface, if there is one */
    winReleasePrimarySurfaceShadowDD(pScreen);

    /* Release the clipper object */
    if (pScreenPriv->pddcPrimary) {
        IDirectDrawClipper_Release(pScreenPriv->pddcPrimary);
        pScreenPriv->pddcPrimary = NULL;
    }

    /* Free the DirectDraw2 object, if there is one */
    if (pScreenPriv->pdd2) {
        IDirectDraw2_RestoreDisplayMode(pScreenPriv->pdd2);
        IDirectDraw2_Release(pScreenPriv->pdd2);
        pScreenPriv->pdd2 = NULL;
    }

    /* Free the DirectDraw object, if there is one */
    if (pScreenPriv->pdd) {
        IDirectDraw_Release(pScreenPriv->pdd);
        pScreenPriv->pdd = NULL;
    }

    /* Invalidate the ScreenInfo's fb pointer */
    pScreenInfo->pfb = NULL;
}

/*
 * Transfer the damaged regions of the shadow framebuffer to the display.
 */

static void
winShadowUpdateDD(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    RegionPtr damage = shadowDamage(pBuf);
    HRESULT ddrval = DD_OK;
    RECT rcDest, rcSrc;
    POINT ptOrigin;
    DWORD dwBox = RegionNumRects(damage);
    BoxPtr pBox = RegionRects(damage);
    HRGN hrgnCombined = NULL;

    /*
     * Return immediately if the app is not active
     * and we are fullscreen, or if we have a bad display depth
     */
    if ((!pScreenPriv->fActive && pScreenInfo->fFullScreen)
        || pScreenPriv->fBadDepth)
        return;

    /* Return immediately if we didn't get needed surfaces */
    if (!pScreenPriv->pddsPrimary || !pScreenPriv->pddsShadow)
        return;

    /* Get the origin of the window in the screen coords */
    ptOrigin.x = pScreenInfo->dwXOffset;
    ptOrigin.y = pScreenInfo->dwYOffset;
    MapWindowPoints(pScreenPriv->hwndScreen,
                    HWND_DESKTOP, (LPPOINT) &ptOrigin, 1);

    /* Unlock the shadow surface, so we can blit */
    ddrval = IDirectDrawSurface2_Unlock(pScreenPriv->pddsShadow, NULL);
    if (FAILED(ddrval)) {
        ErrorF("winShadowUpdateDD - Unlock failed\n");
        return;
    }

    /*
     * Handle small regions with multiple blits,
     * handle large regions by creating a clipping region and
     * doing a single blit constrained to that clipping region.
     */
    if (pScreenInfo->dwClipUpdatesNBoxes == 0
        || dwBox < pScreenInfo->dwClipUpdatesNBoxes) {
        /* Loop through all boxes in the damaged region */
        while (dwBox--) {
            /* Assign damage box to source rectangle */
            rcSrc.left = pBox->x1;
            rcSrc.top = pBox->y1;
            rcSrc.right = pBox->x2;
            rcSrc.bottom = pBox->y2;

            /* Calculate destination rectange */
            rcDest.left = ptOrigin.x + rcSrc.left;
            rcDest.top = ptOrigin.y + rcSrc.top;
            rcDest.right = ptOrigin.x + rcSrc.right;
            rcDest.bottom = ptOrigin.y + rcSrc.bottom;

            /* Blit the damaged areas */
            ddrval = IDirectDrawSurface2_Blt(pScreenPriv->pddsPrimary,
                                             &rcDest,
                                             pScreenPriv->pddsShadow,
                                             &rcSrc, DDBLT_WAIT, NULL);

            /* Get a pointer to the next box */
            ++pBox;
        }
    }
    else {
        BoxPtr pBoxExtents = RegionExtents(damage);

        /* Compute a GDI region from the damaged region */
        hrgnCombined =
            CreateRectRgn(pBoxExtents->x1, pBoxExtents->y1, pBoxExtents->x2,
                          pBoxExtents->y2);

        /* Install the GDI region as a clipping region */
        SelectClipRgn(pScreenPriv->hdcScreen, hrgnCombined);
        DeleteObject(hrgnCombined);
        hrgnCombined = NULL;

        /* Calculating a bounding box for the source is easy */
        rcSrc.left = pBoxExtents->x1;
        rcSrc.top = pBoxExtents->y1;
        rcSrc.right = pBoxExtents->x2;
        rcSrc.bottom = pBoxExtents->y2;

        /* Calculating a bounding box for the destination is trickier */
        rcDest.left = ptOrigin.x + rcSrc.left;
        rcDest.top = ptOrigin.y + rcSrc.top;
        rcDest.right = ptOrigin.x + rcSrc.right;
        rcDest.bottom = ptOrigin.y + rcSrc.bottom;

        /* Our Blt should be clipped to the invalidated region */
        ddrval = IDirectDrawSurface2_Blt(pScreenPriv->pddsPrimary,
                                         &rcDest,
                                         pScreenPriv->pddsShadow,
                                         &rcSrc, DDBLT_WAIT, NULL);

        /* Reset the clip region */
        SelectClipRgn(pScreenPriv->hdcScreen, NULL);
    }

    /* Relock the shadow surface */
    ddrval = IDirectDrawSurface2_Lock(pScreenPriv->pddsShadow,
                                      NULL,
                                      pScreenPriv->pddsdShadow,
                                      DDLOCK_WAIT, NULL);
    if (FAILED(ddrval)) {
        ErrorF("winShadowUpdateDD - Lock failed\n");
        return;
    }

    /* Has our memory pointer changed? */
    if (pScreenInfo->pfb != pScreenPriv->pddsdShadow->lpSurface) {
        ErrorF("winShadowUpdateDD - Memory location of the shadow "
               "surface has changed, trying to update the root window "
               "pixmap header to point to the new address.  If you get "
               "this message and " PROJECT_NAME " freezes or crashes "
               "after this message then send a problem report and your "
               "%s file to " BUILDERADDR "\n", g_pszLogFile);

        /* Location of shadow framebuffer has changed */
        winUpdateFBPointer(pScreen, pScreenPriv->pddsdShadow->lpSurface);
    }
}

static Bool
winInitScreenShadowDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);

    /* Get a device context for the screen  */
    pScreenPriv->hdcScreen = GetDC(pScreenPriv->hwndScreen);

    return winAllocateFBShadowDD(pScreen);
}

/*
 * Call the wrapped CloseScreen function.
 *
 * Free our resources and private structures.
 */

static Bool
winCloseScreenShadowDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    Bool fReturn;

#if CYGDEBUG
    winDebug("winCloseScreenShadowDD - Freeing screen resources\n");
#endif

    /* Flag that the screen is closed */
    pScreenPriv->fClosed = TRUE;
    pScreenPriv->fActive = FALSE;

    /* Call the wrapped CloseScreen procedure */
    WIN_UNWRAP(CloseScreen);
    if (pScreen->CloseScreen)
        fReturn = (*pScreen->CloseScreen) (pScreen);

    winFreeFBShadowDD(pScreen);

    /* Free the screen DC */
    ReleaseDC(pScreenPriv->hwndScreen, pScreenPriv->hdcScreen);

    /* Delete the window property */
    RemoveProp(pScreenPriv->hwndScreen, WIN_SCR_PROP);

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

#if defined(XWIN_CLIPBOARD) || defined(XWIN_MULTIWINDOW)
    /* Destroy the thread startup mutex */
    pthread_mutex_destroy(&pScreenPriv->pmServerStarted);
#endif

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
winInitVisualsShadowDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    DWORD dwRedBits, dwGreenBits, dwBlueBits;

    /* Count the number of ones in each color mask */
    dwRedBits = winCountBits(pScreenPriv->dwRedMask);
    dwGreenBits = winCountBits(pScreenPriv->dwGreenMask);
    dwBlueBits = winCountBits(pScreenPriv->dwBlueMask);

    /* Store the maximum number of ones in a color mask as the bitsPerRGB */
    if (dwRedBits == 0 || dwGreenBits == 0 || dwBlueBits == 0)
        pScreenPriv->dwBitsPerRGB = 8;
    else if (dwRedBits > dwGreenBits && dwRedBits > dwBlueBits)
        pScreenPriv->dwBitsPerRGB = dwRedBits;
    else if (dwGreenBits > dwRedBits && dwGreenBits > dwBlueBits)
        pScreenPriv->dwBitsPerRGB = dwGreenBits;
    else
        pScreenPriv->dwBitsPerRGB = dwBlueBits;

    ErrorF("winInitVisualsShadowDD - Masks %08x %08x %08x BPRGB %d d %d "
           "bpp %d\n",
           (unsigned int) pScreenPriv->dwRedMask,
           (unsigned int) pScreenPriv->dwGreenMask,
           (unsigned int) pScreenPriv->dwBlueMask,
           (int) pScreenPriv->dwBitsPerRGB,
           (int) pScreenInfo->dwDepth, (int) pScreenInfo->dwBPP);

    /* Create a single visual according to the Windows screen depth */
    switch (pScreenInfo->dwDepth) {
    case 24:
    case 16:
    case 15:
        /* Create the real visual */
        if (!miSetVisualTypesAndMasks(pScreenInfo->dwDepth,
                                      TrueColorMask,
                                      pScreenPriv->dwBitsPerRGB,
                                      TrueColor,
                                      pScreenPriv->dwRedMask,
                                      pScreenPriv->dwGreenMask,
                                      pScreenPriv->dwBlueMask)) {
            ErrorF("winInitVisualsShadowDD - miSetVisualTypesAndMasks "
                   "failed for TrueColor\n");
            return FALSE;
        }

#ifdef XWIN_EMULATEPSEUDO
        if (!pScreenInfo->fEmulatePseudo)
            break;

        /* Setup a pseudocolor visual */
        if (!miSetVisualTypesAndMasks(8, PseudoColorMask, 8, -1, 0, 0, 0)) {
            ErrorF("winInitVisualsShadowDD - miSetVisualTypesAndMasks "
                   "failed for PseudoColor\n");
            return FALSE;
        }
#endif
        break;

    case 8:
        if (!miSetVisualTypesAndMasks(pScreenInfo->dwDepth,
                                      pScreenInfo->fFullScreen
                                      ? PseudoColorMask : StaticColorMask,
                                      pScreenPriv->dwBitsPerRGB,
                                      pScreenInfo->fFullScreen
                                      ? PseudoColor : StaticColor,
                                      pScreenPriv->dwRedMask,
                                      pScreenPriv->dwGreenMask,
                                      pScreenPriv->dwBlueMask)) {
            ErrorF("winInitVisualsShadowDD - miSetVisualTypesAndMasks "
                   "failed\n");
            return FALSE;
        }
        break;

    default:
        ErrorF("winInitVisualsShadowDD - Unknown screen depth\n");
        return FALSE;
    }

#if CYGDEBUG
    winDebug("winInitVisualsShadowDD - Returning\n");
#endif

    return TRUE;
}

/*
 * Adjust the user proposed video mode
 */

static Bool
winAdjustVideoModeShadowDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    HDC hdc = NULL;
    DWORD dwBPP;

    /* We're in serious trouble if we can't get a DC */
    hdc = GetDC(NULL);
    if (hdc == NULL) {
        ErrorF("winAdjustVideoModeShadowDD - GetDC () failed\n");
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
 * Blt exposed regions to the screen
 */

static Bool
winBltExposedRegionsShadowDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    RECT rcSrc, rcDest;
    POINT ptOrigin;
    HDC hdcUpdate = NULL;
    PAINTSTRUCT ps;
    HRESULT ddrval = DD_OK;
    Bool fReturn = TRUE;
    Bool fLocked = TRUE;
    int i;

    /* BeginPaint gives us an hdc that clips to the invalidated region */
    hdcUpdate = BeginPaint(pScreenPriv->hwndScreen, &ps);
    if (hdcUpdate == NULL) {
        ErrorF("winBltExposedRegionsShadowDD - BeginPaint () returned "
               "a NULL device context handle.  Aborting blit attempt.\n");
        return FALSE;
    }

    /* Unlock the shadow surface, so we can blit */
    ddrval = IDirectDrawSurface2_Unlock(pScreenPriv->pddsShadow, NULL);
    if (FAILED(ddrval)) {
        fReturn = FALSE;
        goto winBltExposedRegionsShadowDD_Exit;
    }
    else {
        /* Flag that we have unlocked the shadow surface */
        fLocked = FALSE;
    }

    /* Get the origin of the window in the screen coords */
    ptOrigin.x = pScreenInfo->dwXOffset;
    ptOrigin.y = pScreenInfo->dwYOffset;

    MapWindowPoints(pScreenPriv->hwndScreen,
                    HWND_DESKTOP, (LPPOINT) &ptOrigin, 1);
    rcDest.left = ptOrigin.x;
    rcDest.right = ptOrigin.x + pScreenInfo->dwWidth;
    rcDest.top = ptOrigin.y;
    rcDest.bottom = ptOrigin.y + pScreenInfo->dwHeight;

    /* Source can be enter shadow surface, as Blt should clip */
    rcSrc.left = 0;
    rcSrc.top = 0;
    rcSrc.right = pScreenInfo->dwWidth;
    rcSrc.bottom = pScreenInfo->dwHeight;

    /* Try to regain the primary surface and blit again if we've lost it */
    for (i = 0; i <= WIN_REGAIN_SURFACE_RETRIES; ++i) {
        /* Our Blt should be clipped to the invalidated region */
        ddrval = IDirectDrawSurface2_Blt(pScreenPriv->pddsPrimary,
                                         &rcDest,
                                         pScreenPriv->pddsShadow,
                                         &rcSrc, DDBLT_WAIT, NULL);
        if (ddrval == DDERR_SURFACELOST) {
            /* Surface was lost */
            ErrorF("winBltExposedRegionsShadowDD - IDirectDrawSurface2_Blt "
                   "reported that the primary surface was lost, "
                   "trying to restore, retry: %d\n", i + 1);

            /* Try to restore the surface, once */
            ddrval = IDirectDrawSurface2_Restore(pScreenPriv->pddsPrimary);
            ErrorF("winBltExposedRegionsShadowDD - "
                   "IDirectDrawSurface2_Restore returned: ");
            if (ddrval == DD_OK)
                ErrorF("DD_OK\n");
            else if (ddrval == DDERR_WRONGMODE)
                ErrorF("DDERR_WRONGMODE\n");
            else if (ddrval == DDERR_INCOMPATIBLEPRIMARY)
                ErrorF("DDERR_INCOMPATIBLEPRIMARY\n");
            else if (ddrval == DDERR_UNSUPPORTED)
                ErrorF("DDERR_UNSUPPORTED\n");
            else if (ddrval == DDERR_INVALIDPARAMS)
                ErrorF("DDERR_INVALIDPARAMS\n");
            else if (ddrval == DDERR_INVALIDOBJECT)
                ErrorF("DDERR_INVALIDOBJECT\n");
            else
                ErrorF("unknown error: %08x\n", (unsigned int) ddrval);

            /* Loop around to try the blit one more time */
            continue;
        }
        else if (FAILED(ddrval)) {
            fReturn = FALSE;
            ErrorF("winBltExposedRegionsShadowDD - IDirectDrawSurface2_Blt "
                   "failed, but surface not lost: %08x %d\n",
                   (unsigned int) ddrval, (int) ddrval);
            goto winBltExposedRegionsShadowDD_Exit;
        }
        else {
            /* Success, stop looping */
            break;
        }
    }

    /* Relock the shadow surface */
    ddrval = IDirectDrawSurface2_Lock(pScreenPriv->pddsShadow,
                                      NULL,
                                      pScreenPriv->pddsdShadow,
                                      DDLOCK_WAIT, NULL);
    if (FAILED(ddrval)) {
        fReturn = FALSE;
        ErrorF("winBltExposedRegionsShadowDD - IDirectDrawSurface2_Lock "
               "failed\n");
        goto winBltExposedRegionsShadowDD_Exit;
    }
    else {
        /* Indicate that we have relocked the shadow surface */
        fLocked = TRUE;
    }

    /* Has our memory pointer changed? */
    if (pScreenInfo->pfb != pScreenPriv->pddsdShadow->lpSurface)
        winUpdateFBPointer(pScreen, pScreenPriv->pddsdShadow->lpSurface);

 winBltExposedRegionsShadowDD_Exit:
    /* EndPaint frees the DC */
    if (hdcUpdate != NULL)
        EndPaint(pScreenPriv->hwndScreen, &ps);

    /*
     * Relock the surface if it is not locked.  We don't care if locking fails,
     * as it will cause the server to shutdown within a few more operations.
     */
    if (!fLocked) {
        IDirectDrawSurface2_Lock(pScreenPriv->pddsShadow,
                                 NULL,
                                 pScreenPriv->pddsdShadow, DDLOCK_WAIT, NULL);

        /* Has our memory pointer changed? */
        if (pScreenInfo->pfb != pScreenPriv->pddsdShadow->lpSurface)
            winUpdateFBPointer(pScreen, pScreenPriv->pddsdShadow->lpSurface);

        fLocked = TRUE;
    }
    return fReturn;
}

/*
 * Do any engine-specific appliation-activation processing
 */

static Bool
winActivateAppShadowDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);

    /*
     * Do we have a surface?
     * Are we active?
     * Are we fullscreen?
     */
    if (pScreenPriv != NULL
        && pScreenPriv->pddsPrimary != NULL && pScreenPriv->fActive) {
        /* Primary surface was lost, restore it */
        IDirectDrawSurface2_Restore(pScreenPriv->pddsPrimary);
    }

    return TRUE;
}

/*
 * Reblit the shadow framebuffer to the screen.
 */

static Bool
winRedrawScreenShadowDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    HRESULT ddrval = DD_OK;
    RECT rcSrc, rcDest;
    POINT ptOrigin;

    /* Get the origin of the window in the screen coords */
    ptOrigin.x = pScreenInfo->dwXOffset;
    ptOrigin.y = pScreenInfo->dwYOffset;
    MapWindowPoints(pScreenPriv->hwndScreen,
                    HWND_DESKTOP, (LPPOINT) &ptOrigin, 1);
    rcDest.left = ptOrigin.x;
    rcDest.right = ptOrigin.x + pScreenInfo->dwWidth;
    rcDest.top = ptOrigin.y;
    rcDest.bottom = ptOrigin.y + pScreenInfo->dwHeight;

    /* Source can be entire shadow surface, as Blt should clip for us */
    rcSrc.left = 0;
    rcSrc.top = 0;
    rcSrc.right = pScreenInfo->dwWidth;
    rcSrc.bottom = pScreenInfo->dwHeight;

    /* Redraw the whole window, to take account for the new colors */
    ddrval = IDirectDrawSurface2_Blt(pScreenPriv->pddsPrimary,
                                     &rcDest,
                                     pScreenPriv->pddsShadow,
                                     &rcSrc, DDBLT_WAIT, NULL);
    if (FAILED(ddrval)) {
        ErrorF("winRedrawScreenShadowDD - IDirectDrawSurface_Blt () "
               "failed: %08x\n", (unsigned int) ddrval);
    }

    return TRUE;
}

/*
 * Realize the currently installed colormap
 */

static Bool
winRealizeInstalledPaletteShadowDD(ScreenPtr pScreen)
{
    return TRUE;
}

/*
 * Install the specified colormap
 */

static Bool
winInstallColormapShadowDD(ColormapPtr pColormap)
{
    ScreenPtr pScreen = pColormap->pScreen;

    winScreenPriv(pScreen);
    winCmapPriv(pColormap);
    HRESULT ddrval = DD_OK;

    /* Install the DirectDraw palette on the primary surface */
    ddrval = IDirectDrawSurface2_SetPalette(pScreenPriv->pddsPrimary,
                                            pCmapPriv->lpDDPalette);
    if (FAILED(ddrval)) {
        ErrorF("winInstallColormapShadowDD - Failed installing the "
               "DirectDraw palette.\n");
        return FALSE;
    }

    /* Save a pointer to the newly installed colormap */
    pScreenPriv->pcmapInstalled = pColormap;

    return TRUE;
}

/*
 * Store the specified colors in the specified colormap
 */

static Bool
winStoreColorsShadowDD(ColormapPtr pColormap, int ndef, xColorItem * pdefs)
{
    ScreenPtr pScreen = pColormap->pScreen;

    winScreenPriv(pScreen);
    winCmapPriv(pColormap);
    ColormapPtr curpmap = pScreenPriv->pcmapInstalled;
    HRESULT ddrval = DD_OK;

    /* Put the X colormap entries into the Windows logical palette */
    ddrval = IDirectDrawPalette_SetEntries(pCmapPriv->lpDDPalette,
                                           0,
                                           pdefs[0].pixel,
                                           ndef,
                                           pCmapPriv->peColors
                                           + pdefs[0].pixel);
    if (FAILED(ddrval)) {
        ErrorF("winStoreColorsShadowDD - SetEntries () failed\n");
        return FALSE;
    }

    /* Don't install the DirectDraw palette if the colormap is not installed */
    if (pColormap != curpmap) {
        return TRUE;
    }

    if (!winInstallColormapShadowDD(pColormap)) {
        ErrorF("winStoreColorsShadowDD - Failed installing colormap\n");
        return FALSE;
    }

    return TRUE;
}

/*
 * Colormap initialization procedure
 */

static Bool
winCreateColormapShadowDD(ColormapPtr pColormap)
{
    HRESULT ddrval = DD_OK;
    ScreenPtr pScreen = pColormap->pScreen;

    winScreenPriv(pScreen);
    winCmapPriv(pColormap);

    /* Create a DirectDraw palette */
    ddrval = IDirectDraw2_CreatePalette(pScreenPriv->pdd,
                                        DDPCAPS_8BIT | DDPCAPS_ALLOW256,
                                        pCmapPriv->peColors,
                                        &pCmapPriv->lpDDPalette, NULL);
    if (FAILED(ddrval)) {
        ErrorF("winCreateColormapShadowDD - CreatePalette failed\n");
        return FALSE;
    }

    return TRUE;
}

/*
 * Colormap destruction procedure
 */

static Bool
winDestroyColormapShadowDD(ColormapPtr pColormap)
{
    winScreenPriv(pColormap->pScreen);
    winCmapPriv(pColormap);
    HRESULT ddrval = DD_OK;

    /*
     * Is colormap to be destroyed the default?
     *
     * Non-default colormaps should have had winUninstallColormap
     * called on them before we get here.  The default colormap
     * will not have had winUninstallColormap called on it.  Thus,
     * we need to handle the default colormap in a special way.
     */
    if (pColormap->flags & IsDefault) {
#if CYGDEBUG
        winDebug("winDestroyColormapShadowDD - Destroying default "
                 "colormap\n");
#endif

        /*
         * FIXME: Walk the list of all screens, popping the default
         * palette out of each screen device context.
         */

        /* Pop the palette out of the primary surface */
        ddrval = IDirectDrawSurface2_SetPalette(pScreenPriv->pddsPrimary, NULL);
        if (FAILED(ddrval)) {
            ErrorF("winDestroyColormapShadowDD - Failed freeing the "
                   "default colormap DirectDraw palette.\n");
            return FALSE;
        }

        /* Clear our private installed colormap pointer */
        pScreenPriv->pcmapInstalled = NULL;
    }

    /* Release the palette */
    IDirectDrawPalette_Release(pCmapPriv->lpDDPalette);

    /* Invalidate the colormap privates */
    pCmapPriv->lpDDPalette = NULL;

    return TRUE;
}

/*
 * Set engine specific functions
 */

Bool
winSetEngineFunctionsShadowDD(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;

    /* Set our pointers */
    pScreenPriv->pwinAllocateFB = winAllocateFBShadowDD;
    pScreenPriv->pwinFreeFB = winFreeFBShadowDD;
    pScreenPriv->pwinShadowUpdate = winShadowUpdateDD;
    pScreenPriv->pwinInitScreen = winInitScreenShadowDD;
    pScreenPriv->pwinCloseScreen = winCloseScreenShadowDD;
    pScreenPriv->pwinInitVisuals = winInitVisualsShadowDD;
    pScreenPriv->pwinAdjustVideoMode = winAdjustVideoModeShadowDD;
    if (pScreenInfo->fFullScreen)
        pScreenPriv->pwinCreateBoundingWindow =
            winCreateBoundingWindowFullScreen;
    else
        pScreenPriv->pwinCreateBoundingWindow = winCreateBoundingWindowWindowed;
    pScreenPriv->pwinFinishScreenInit = winFinishScreenInitFB;
    pScreenPriv->pwinBltExposedRegions = winBltExposedRegionsShadowDD;
    pScreenPriv->pwinActivateApp = winActivateAppShadowDD;
    pScreenPriv->pwinRedrawScreen = winRedrawScreenShadowDD;
    pScreenPriv->pwinRealizeInstalledPalette
        = winRealizeInstalledPaletteShadowDD;
    pScreenPriv->pwinInstallColormap = winInstallColormapShadowDD;
    pScreenPriv->pwinStoreColors = winStoreColorsShadowDD;
    pScreenPriv->pwinCreateColormap = winCreateColormapShadowDD;
    pScreenPriv->pwinDestroyColormap = winDestroyColormapShadowDD;
    pScreenPriv->pwinHotKeyAltTab =
        (winHotKeyAltTabProcPtr) (void (*)(void)) NoopDDA;
    pScreenPriv->pwinCreatePrimarySurface = winCreatePrimarySurfaceShadowDD;
    pScreenPriv->pwinReleasePrimarySurface = winReleasePrimarySurfaceShadowDD;
#ifdef XWIN_MULTIWINDOW
    pScreenPriv->pwinFinishCreateWindowsWindow =
        (winFinishCreateWindowsWindowProcPtr) (void (*)(void)) NoopDDA;
#endif

    return TRUE;
}
