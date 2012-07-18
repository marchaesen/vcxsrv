/*
 *Copyright (C) 2001-2004 Harold L Hunt II All Rights Reserved.
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
 *NONINFRINGEMENT. IN NO EVENT SHALL HAROLD L HUNT II BE LIABLE FOR
 *ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *Except as contained in this notice, the name of Harold L Hunt II
 *shall not be used in advertising or otherwise to promote the sale, use
 *or other dealings in this Software without prior written authorization
 *from Harold L Hunt II.
 *
 * Authors:	Harold L Hunt II
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"
#include "winmsg.h"

/* See Porting Layer Definition - p. 6 */
void
winBlockHandler(ScreenPtr pScreen,
                pointer pTimeout, pointer pReadMask)
{
#if defined(XWIN_CLIPBOARD) || defined(XWIN_MULTIWINDOW)
    winScreenPriv(pScreen);
#endif
    MSG msg;

#ifndef HAS_DEVWINDOWS
    struct timeval **tvp = pTimeout;

    if (*tvp != NULL) {
        (*tvp)->tv_sec = 0;
        (*tvp)->tv_usec = 100;
    }
#endif

#if defined(XWIN_CLIPBOARD) || defined(XWIN_MULTIWINDOW)
    /* Signal threaded modules to begin */
    if (pScreenPriv != NULL && !pScreenPriv->fServerStarted) {
        int iReturn;

        ErrorF("winBlockHandler - pthread_mutex_unlock()\n");

        /* Flag that modules are to be started */
        pScreenPriv->fServerStarted = TRUE;

        /* Unlock the mutex for threaded modules */
        iReturn = pthread_mutex_unlock(&pScreenPriv->pmServerStarted);
        if (iReturn != 0) {
            ErrorF("winBlockHandler - pthread_mutex_unlock () failed: %d\n",
                   iReturn);
            goto winBlockHandler_ProcessMessages;
        }

        winDebug("winBlockHandler - pthread_mutex_unlock () returned\n");
    }

 winBlockHandler_ProcessMessages:
#endif

    /* Process all messages on our queue */
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if ((g_hDlgDepthChange == 0
             || !IsDialogMessage(g_hDlgDepthChange, &msg))
            && (g_hDlgExit == 0 || !IsDialogMessage(g_hDlgExit, &msg))
            && (g_hDlgAbout == 0 || !IsDialogMessage(g_hDlgAbout, &msg))) {
            DispatchMessage(&msg);
        }
    }
}
