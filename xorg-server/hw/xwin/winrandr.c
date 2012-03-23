/*
 *Copyright (C) 2001-2004 Harold L Hunt II All Rights Reserved.
 *Copyright (C) 2009-2010 Jon TURNEY
 *
 *Permission is hereby granted, free of charge, to any person obtaining
 *a copy of this software and associated documentation files (the
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
 *NONINFRINGEMENT. IN NO EVENT SHALL HAROLD L HUNT II BE LIABLE FOR
 *ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *Except as contained in this notice, the name of the author(s)
 *shall not be used in advertising or otherwise to promote the sale, use
 *or other dealings in this Software without prior written authorization
 *from the author(s)
 *
 * Authors:	Harold L Hunt II
 *              Jon TURNEY
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"
#include "mivalidate.h"         // for union _Validate used by windowstr.h

#ifndef RANDR_12_INTERFACE
#error X server must have RandR 1.2 interface
#endif

/*
 * Answer queries about the RandR features supported.
 */

static Bool
winRandRGetInfo(ScreenPtr pScreen, Rotation * pRotations)
{
    winDebug("winRandRGetInfo ()\n");

    /* Don't support rotations */
    *pRotations = RR_Rotate_0;

    /*
       The screen doesn't have to be limited to the actual
       monitor size (we can have scrollbars :-), so what is
       the upper limit?
     */
    RRScreenSetSizeRange(pScreen, 0, 0, 4096, 4096);

    return TRUE;
}

/*

*/
void
winDoRandRScreenSetSize(ScreenPtr pScreen,
                        CARD16 width,
                        CARD16 height, CARD32 mmWidth, CARD32 mmHeight)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    WindowPtr pRoot = pScreen->root;

    // Prevent screen updates while we change things around
    SetRootClip(pScreen, FALSE);

    /* Update the screen size as requested */
    pScreenInfo->dwWidth = width;
    pScreenInfo->dwHeight = height;

    /* Reallocate the framebuffer used by the drawing engine */
    (*pScreenPriv->pwinFreeFB) (pScreen);
    if (!(*pScreenPriv->pwinAllocateFB) (pScreen)) {
        ErrorF("winDoRandRScreenSetSize - Could not reallocate framebuffer\n");
    }

    pScreen->width = width;
    pScreen->height = height;
    pScreen->mmWidth = mmWidth;
    pScreen->mmHeight = mmHeight;

    /* Update the screen pixmap to point to the new framebuffer */
    winUpdateFBPointer(pScreen, pScreenInfo->pfb);

    // pScreen->devPrivate == pScreen->GetScreenPixmap(screen) ?
    // resize the root window
    //pScreen->ResizeWindow(pRoot, 0, 0, width, height, NULL);
    // does this emit a ConfigureNotify??

    // Restore the ability to update screen, now with new dimensions
    SetRootClip(pScreen, TRUE);

    // and arrange for it to be repainted
    miPaintWindow(pRoot, &pRoot->borderClip, PW_BACKGROUND);

    /* Indicate that a screen size change took place */
    RRScreenSizeNotify(pScreen);
}

/*
 * Respond to resize request
 */
static
    Bool
winRandRScreenSetSize(ScreenPtr pScreen,
                      CARD16 width,
                      CARD16 height, CARD32 mmWidth, CARD32 mmHeight)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;

    winDebug("winRandRScreenSetSize ()\n");

    /*
       It doesn't currently make sense to allow resize in fullscreen mode
       (we'd actually have to list the supported resolutions)
     */
    if (pScreenInfo->fFullScreen) {
        ErrorF
            ("winRandRScreenSetSize - resize not supported in fullscreen mode\n");
        return FALSE;
    }

    /*
       Client resize requests aren't allowed in rootless modes, even if
       the X screen is monitor or virtual desktop size, we'd need to
       resize the native display size
     */
    if (FALSE
#ifdef XWIN_MULTIWINDOWEXTWM
        || pScreenInfo->fMWExtWM
#endif
        || pScreenInfo->fRootless
#ifdef XWIN_MULTIWINDOW
        || pScreenInfo->fMultiWindow
#endif
        ) {
        ErrorF
            ("winRandRScreenSetSize - resize not supported in rootless modes\n");
        return FALSE;
    }

    winDoRandRScreenSetSize(pScreen, width, height, mmWidth, mmHeight);

    /* Cause the native window for the screen to resize itself */
    {
        DWORD dwStyle, dwExStyle;
        RECT rcClient;

        rcClient.left = 0;
        rcClient.top = 0;
        rcClient.right = width;
        rcClient.bottom = height;

        ErrorF("winRandRScreenSetSize new client area w: %d h: %d\n", width,
               height);

        /* Get the Windows window style and extended style */
        dwExStyle = GetWindowLongPtr(pScreenPriv->hwndScreen, GWL_EXSTYLE);
        dwStyle = GetWindowLongPtr(pScreenPriv->hwndScreen, GWL_STYLE);

        /*
         * Calculate the window size needed for the given client area
         * adjusting for any decorations it will have
         */
        AdjustWindowRectEx(&rcClient, dwStyle, FALSE, dwExStyle);

        ErrorF("winRandRScreenSetSize new window area w: %ld h: %ld\n",
               rcClient.right - rcClient.left, rcClient.bottom - rcClient.top);

        SetWindowPos(pScreenPriv->hwndScreen, NULL,
                     0, 0, rcClient.right - rcClient.left,
                     rcClient.bottom - rcClient.top, SWP_NOZORDER | SWP_NOMOVE);
    }

    return TRUE;
}

/*
 * Initialize the RandR layer.
 */

Bool
winRandRInit(ScreenPtr pScreen)
{
    rrScrPrivPtr pRRScrPriv;

    winDebug("winRandRInit ()\n");

    if (!RRScreenInit(pScreen)) {
        ErrorF("winRandRInit () - RRScreenInit () failed\n");
        return FALSE;
    }

    /* Set some RandR function pointers */
    pRRScrPriv = rrGetScrPriv(pScreen);
    pRRScrPriv->rrGetInfo = winRandRGetInfo;
    pRRScrPriv->rrSetConfig = NULL;
    pRRScrPriv->rrScreenSetSize = winRandRScreenSetSize;
    pRRScrPriv->rrCrtcSet = NULL;
    pRRScrPriv->rrCrtcSetGamma = NULL;

    return TRUE;
}
