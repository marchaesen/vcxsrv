/*
 *Copyright (C) 2003-2004 Harold L Hunt II All Rights Reserved.
 *Copyright (C) Colin Harrison 2005-2008
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
 *Except as contained in this notice, the name of the copyright holder(s)
 *and author(s) shall not be used in advertising or otherwise to promote
 *the sale, use or other dealings in this Software without prior written
 *authorization from the copyright holder(s) and author(s).
 *
 * Authors:	Harold L Hunt II
 *              Colin Harrison
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif

/*
 * Including any server header might define the macro _XSERVER64 on 64 bit machines.
 * That macro must _NOT_ be defined for Xlib client code, otherwise bad things happen.
 * So let's undef that macro if necessary.
 */
#ifdef _XSERVER64
#undef _XSERVER64
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <limits.h>

#include <X11/Xatom.h>

#include "internal.h"
#include "winclipboard.h"

/*
 * Constants
 */

#define WIN_POLL_TIMEOUT	1

#ifndef WM_CLIPBOARDUPDATE
#define WM_CLIPBOARDUPDATE 0x031D
#endif

/*
 * Process X events up to specified timeout
 */

static int
winProcessXEventsTimeout(HWND hwnd, Window iWindow, Display * pDisplay,
                         ClipboardConversionData *data, ClipboardAtoms *atoms, int iTimeoutSec)
{
    int iConnNumber;
    struct timeval tv;
    int iReturn;
    DWORD dwStopTime = GetTickCount() + iTimeoutSec * 1000;

    winDebug("winProcessXEventsTimeout () - pumping X events for %d seconds\n",
             iTimeoutSec);

    /* Get our connection number */
    iConnNumber = ConnectionNumber(pDisplay);

    /* Loop for X events */
    while (1) {
        fd_set fdsRead;
        long remainingTime;

        /* Process X events */
        iReturn = winClipboardFlushXEvents(hwnd, iWindow, pDisplay, data, atoms);

        winDebug("winProcessXEventsTimeout () - winClipboardFlushXEvents returned %d\n", iReturn);

        if ((WIN_XEVENTS_NOTIFY_DATA == iReturn) || (WIN_XEVENTS_NOTIFY_TARGETS == iReturn) || (WIN_XEVENTS_FAILED == iReturn)) {
          /* Bail out */
          return iReturn;
        }

        /* We need to ensure that all pending requests are sent */
        XFlush(pDisplay);

        /* Setup the file descriptor set */
        FD_ZERO(&fdsRead);
        FD_SET(iConnNumber, &fdsRead);

        /* Adjust timeout */
        remainingTime = dwStopTime - GetTickCount();
        tv.tv_sec = remainingTime / 1000;
        tv.tv_usec = (remainingTime % 1000) * 1000;
        winDebug("winProcessXEventsTimeout () - %ld milliseconds left\n",
                 remainingTime);

        /* Break out if no time left */
        if (remainingTime <= 0)
            return WIN_XEVENTS_SUCCESS;

        /* Wait for an X event */
        iReturn = select(iConnNumber + 1,       /* Highest fds number */
                         &fdsRead,      /* Read mask */
                         NULL,  /* No write mask */
                         NULL,  /* No exception mask */
                         &tv);  /* Timeout */
        if (iReturn < 0) {
            ErrorF("winProcessXEventsTimeout - Call to select () failed: %d.  "
                   "Bailing.\n", iReturn);
            break;
        }

        if (!FD_ISSET(iConnNumber, &fdsRead)) {
            winDebug("winProcessXEventsTimeout - Spurious wake, select() returned %d\n", iReturn);
        }
    }

    return WIN_XEVENTS_SUCCESS;
}

/*
 * Process a given Windows message
 */

LRESULT CALLBACK
winClipboardWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND s_hwndNextViewer;
    static Bool s_fCBCInitialized;
    static Display *pDisplay;
    static Window iWindow;
    static ClipboardAtoms *atoms;
    static Bool fRunning;

    /* Branch on message type */
    switch (message) {
    case WM_DESTROY:
    {
        winDebug("winClipboardWindowProc - WM_DESTROY\n");

        if (g_fHasModernClipboardApi)
            {
                /* Remove clipboard listener */
                g_fpRemoveClipboardFormatListener(hwnd);
            }
        else
            {
                /* Remove ourselves from the clipboard chain */
                ChangeClipboardChain(hwnd, s_hwndNextViewer);
            }

        s_hwndNextViewer = NULL;
    }
        return 0;

    case WM_WM_QUIT:
    {
        winDebug("winClipboardWindowProc - WM_WM_QUIT\n");
        fRunning = FALSE;
        PostQuitMessage(0);
    }
        return 0;

    case WM_CREATE:
    {
        ClipboardWindowCreationParams *cwcp = (ClipboardWindowCreationParams *)((CREATESTRUCT *)lParam)->lpCreateParams;

        winDebug("winClipboardWindowProc - WM_CREATE\n");

        pDisplay = cwcp->pClipboardDisplay;
        iWindow = cwcp->iClipboardWindow;
        atoms = cwcp->atoms;
        fRunning = TRUE;

        if (g_fHasModernClipboardApi)
            {
                g_fpAddClipboardFormatListener(hwnd);
            }
        else
            {
                HWND first, next;
                DWORD error_code = 0;

                first = GetClipboardViewer();   /* Get handle to first viewer in chain. */
                if (first == hwnd)
                    return 0;           /* Make sure it's not us! */
                /* Add ourselves to the clipboard viewer chain */
                next = SetClipboardViewer(hwnd);
                error_code = GetLastError();
                if (SUCCEEDED(error_code) && (next == first))   /* SetClipboardViewer must have succeeded, and the handle */
                    s_hwndNextViewer = next;    /* it returned must have been the first window in the chain */
                else
                    s_fCBCInitialized = FALSE;
            }
    }
        return 0;

    case WM_CHANGECBCHAIN:
    {
        winDebug("winClipboardWindowProc - WM_CHANGECBCHAIN: wParam(%p) "
                 "lParam(%p) s_hwndNextViewer(%p)\n",
                 (HWND)wParam, (HWND)lParam, s_hwndNextViewer);

        if ((HWND) wParam == s_hwndNextViewer) {
            s_hwndNextViewer = (HWND) lParam;
            if (s_hwndNextViewer == hwnd) {
                s_hwndNextViewer = NULL;
                ErrorF("winClipboardWindowProc - WM_CHANGECBCHAIN: "
                       "attempted to set next window to ourselves.");
            }
        }
        else if (s_hwndNextViewer)
            SendMessage(s_hwndNextViewer, message, wParam, lParam);

    }
        winDebug("winClipboardWindowProc - WM_CHANGECBCHAIN: Exit\n");
        return 0;

    case WM_WM_REINIT:
    {
        /* Ensure that we're in the clipboard chain.  Some apps,
         * WinXP's remote desktop for one, don't play nice with the
         * chain.  This message is called whenever we receive a
         * WM_ACTIVATEAPP message to ensure that we continue to
         * receive clipboard messages.
         *
         * It might be possible to detect if we're still in the chain
         * by calling SendMessage (GetClipboardViewer(),
         * WM_DRAWCLIPBOARD, 0, 0); and then seeing if we get the
         * WM_DRAWCLIPBOARD message.  That, however, might be more
         * expensive than just putting ourselves back into the chain.
         */

        HWND first, next;
        DWORD error_code = 0;

        winDebug("winClipboardWindowProc - WM_WM_REINIT: Enter\n");

        if (g_fHasModernClipboardApi)
            {
                return 0;
            }

        first = GetClipboardViewer();   /* Get handle to first viewer in chain. */
        if (first == hwnd)
            return 0;           /* Make sure it's not us! */
        winDebug("  WM_WM_REINIT: Replacing us(%p) with %p at head "
                 "of chain\n", hwnd, s_hwndNextViewer);
        s_fCBCInitialized = FALSE;
        ChangeClipboardChain(hwnd, s_hwndNextViewer);
        s_hwndNextViewer = NULL;
        s_fCBCInitialized = FALSE;
        winDebug("  WM_WM_REINIT: Putting us back at head of chain.\n");
        first = GetClipboardViewer();   /* Get handle to first viewer in chain. */
        if (first == hwnd)
            return 0;           /* Make sure it's not us! */
        next = SetClipboardViewer(hwnd);
        error_code = GetLastError();
        if (SUCCEEDED(error_code) && (next == first))   /* SetClipboardViewer must have succeeded, and the handle */
            s_hwndNextViewer = next;    /* it returned must have been the first window in the chain */
        else
            s_fCBCInitialized = FALSE;
    }
        winDebug("winClipboardWindowProc - WM_WM_REINIT: Exit\n");
        return 0;

    case WM_DRAWCLIPBOARD:
    case WM_CLIPBOARDUPDATE:
    {
        static Bool s_fProcessingDrawClipboard = FALSE;
        int iReturn;

        if (message == WM_DRAWCLIPBOARD)
            winDebug("winClipboardWindowProc - WM_DRAWCLIPBOARD: Enter\n");
        else
            winDebug("winClipboardWindowProc -  WM_CLIPBOARDUPDATE: Enter\n");

        if (!g_fHasModernClipboardApi)
            {
                /*
                 * We've occasionally seen a loop in the clipboard chain.
                 * Try and fix it on the first hint of recursion.
                 */
                if (!s_fProcessingDrawClipboard) {
                    s_fProcessingDrawClipboard = TRUE;
                }
                else {
                    /* Attempt to break the nesting by getting out of the chain, twice?, and then fix and bail */
                    s_fCBCInitialized = FALSE;
                    ChangeClipboardChain(hwnd, s_hwndNextViewer);
                    winFixClipboardChain();
                    ErrorF("winClipboardWindowProc - WM_DRAWCLIPBOARD - "
                           "Nested calls detected.  Re-initing.\n");
                    winDebug("winClipboardWindowProc - WM_DRAWCLIPBOARD: Exit\n");
                    s_fProcessingDrawClipboard = FALSE;
                    return 0;
                }

                /* Bail on first message */
                if (!s_fCBCInitialized) {
                    s_fCBCInitialized = TRUE;
                    s_fProcessingDrawClipboard = FALSE;
                    winDebug("winClipboardWindowProc - WM_DRAWCLIPBOARD: Exit\n");
                    return 0;
                }
            }

        /*
         * NOTE: We cannot bail out when NULL == GetClipboardOwner ()
         * because some applications deal with the clipboard in a manner
         * that causes the clipboard owner to be NULL when they are in
         * fact taking ownership.  One example of this is the Win32
         * native compile of emacs.
         */

        /* Bail when we still own the clipboard */
        if (hwnd == GetClipboardOwner()) {

            winDebug("winClipboardWindowProc - WM_DRAWCLIPBOARD - "
                     "We own the clipboard, returning.\n");
            winDebug("winClipboardWindowProc - WM_DRAWCLIPBOARD: Exit\n");
            s_fProcessingDrawClipboard = FALSE;
            if (s_hwndNextViewer)
                SendMessage(s_hwndNextViewer, message, wParam, lParam);
            return 0;
        }

        /* Bail when shutting down */
        if (!fRunning)
            return 0;

        /*
         * Do not take ownership of the X11 selections when something
         * other than CF_TEXT or CF_UNICODETEXT has been copied
         * into the Win32 clipboard.
         */
        if (!IsClipboardFormatAvailable(CF_TEXT)
            && !IsClipboardFormatAvailable(CF_UNICODETEXT)) {

            winDebug("winClipboardWindowProc - WM_DRAWCLIPBOARD - "
                     "Clipboard does not contain CF_TEXT nor "
                     "CF_UNICODETEXT.\n");

            /*
             * We need to make sure that the X Server has processed
             * previous XSetSelectionOwner messages.
             */
            XSync(pDisplay, FALSE);

            winDebug("winClipboardWindowProc - XSync done.\n");

            /* Release PRIMARY selection if owned */
            iReturn = XGetSelectionOwner(pDisplay, XA_PRIMARY);
            if (iReturn == iWindow) {
                winDebug("winClipboardWindowProc - WM_DRAWCLIPBOARD - "
                         "PRIMARY selection is owned by us.\n");
                XSetSelectionOwner(pDisplay, XA_PRIMARY, None, CurrentTime);
            }
            else if (BadWindow == iReturn || BadAtom == iReturn)
                ErrorF("winClipboardWindowProc - WM_DRAWCLIPBOARD - "
                       "XGetSelectionOwner failed for PRIMARY: %d\n",
                       iReturn);

            /* Release CLIPBOARD selection if owned */
            iReturn = XGetSelectionOwner(pDisplay, atoms->atomClipboard);
            if (iReturn == iWindow) {
                winDebug("winClipboardWindowProc - WM_DRAWCLIPBOARD - "
                         "CLIPBOARD selection is owned by us, releasing\n");
                XSetSelectionOwner(pDisplay, atoms->atomClipboard, None, CurrentTime);
            }
            else if (BadWindow == iReturn || BadAtom == iReturn)
                ErrorF("winClipboardWindowProc - WM_DRAWCLIPBOARD - "
                       "XGetSelectionOwner failed for CLIPBOARD: %d\n",
                       iReturn);

            winDebug("winClipboardWindowProc - WM_DRAWCLIPBOARD: Exit\n");
            s_fProcessingDrawClipboard = FALSE;
            if (s_hwndNextViewer)
                SendMessage(s_hwndNextViewer, message, wParam, lParam);
            return 0;
        }

        /* Reassert ownership of PRIMARY */
        iReturn = XSetSelectionOwner(pDisplay,
                                     XA_PRIMARY, iWindow, CurrentTime);
        if (iReturn == BadAtom || iReturn == BadWindow ||
            XGetSelectionOwner(pDisplay, XA_PRIMARY) != iWindow) {
            ErrorF("winClipboardWindowProc - WM_DRAWCLIPBOARD - "
                   "Could not reassert ownership of PRIMARY\n");
        }
        else {
            winDebug("winClipboardWindowProc - WM_DRAWCLIPBOARD - "
                     "Reasserted ownership of PRIMARY\n");
        }

        /* Reassert ownership of the CLIPBOARD */
        iReturn = XSetSelectionOwner(pDisplay,
                                     atoms->atomClipboard, iWindow, CurrentTime);

        if (iReturn == BadAtom || iReturn == BadWindow ||
            XGetSelectionOwner(pDisplay, atoms->atomClipboard) != iWindow) {
            ErrorF("winClipboardWindowProc - WM_DRAWCLIPBOARD - "
                    "Could not reassert ownership of CLIPBOARD\n");
        }
        else {
            winDebug("winClipboardWindowProc - WM_DRAWCLIPBOARD - "
                     "Reasserted ownership of CLIPBOARD\n");
        }

        /* Flush the pending SetSelectionOwner event now */
        XFlush(pDisplay);

        s_fProcessingDrawClipboard = FALSE;
    }
        winDebug("winClipboardWindowProc - WM_DRAWCLIPBOARD: Exit\n");
        /* Pass the message on the next window in the clipboard viewer chain */
        if (s_hwndNextViewer)
            SendMessage(s_hwndNextViewer, message, wParam, lParam);
        return 0;

    case WM_DESTROYCLIPBOARD:
        /*
         * NOTE: Intentionally do nothing.
         * Changes in the Win32 clipboard are handled by WM_DRAWCLIPBOARD
         * above.  We only process this message to conform to the specs
         * for delayed clipboard rendering in Win32.  You might think
         * that we need to release ownership of the X11 selections, but
         * we do not, because a WM_DRAWCLIPBOARD message will closely
         * follow this message and reassert ownership of the X11
         * selections, handling the issue for us.
         */
        winDebug("winClipboardWindowProc - WM_DESTROYCLIPBOARD - Ignored.\n");
        return 0;

    case WM_RENDERALLFORMATS:
        winDebug("winClipboardWindowProc - WM_RENDERALLFORMATS - Hello.\n");

        /*
          WM_RENDERALLFORMATS is sent as we are shutting down, to render the
          clipboard so it's contents remains available to other applications.

          Unfortunately, this can't work without major changes. The server is
          already waiting for us to stop, so we can't ask for the rendering of
          clipboard text now.
        */

        return 0;

    case WM_RENDERFORMAT:
    {
        int iReturn;
        Bool fConvertToUnicode;
        Bool pasted = FALSE;
        Atom selection;
        ClipboardConversionData data;
        int best_target = 0;

        winDebug("winClipboardWindowProc - WM_RENDERFORMAT %d - Hello.\n",
                 (int)wParam);

        /* Flag whether to convert to Unicode or not */
        fConvertToUnicode = (CF_UNICODETEXT == wParam);

        selection = winClipboardGetLastOwnedSelectionAtom(atoms);
        if (selection == None) {
            ErrorF("winClipboardWindowProc - no monitored selection is owned\n");
            goto fake_paste;
        }

        winDebug("winClipboardWindowProc - requesting targets for selection from owner\n");

        /* Request the selection's supported conversion targets */
        XConvertSelection(pDisplay,
                          selection,
                          atoms->atomTargets,
                          atoms->atomLocalProperty,
                          iWindow, CurrentTime);

        /* Process X events */
        data.fUseUnicode = fConvertToUnicode;
        iReturn = winProcessXEventsTimeout(hwnd,
                                           iWindow,
                                           pDisplay,
                                           &data,
                                           atoms,
                                           WIN_POLL_TIMEOUT);

        if (WIN_XEVENTS_NOTIFY_TARGETS != iReturn) {
            ErrorF
                ("winClipboardWindowProc - timed out waiting for WIN_XEVENTS_NOTIFY_TARGETS\n");
            goto fake_paste;
        }

        /* Choose the most preferred target */
        {
            struct target_priority
            {
                Atom target;
                unsigned int priority;
            };

            struct target_priority target_priority_table[] =
                {
                    { atoms->atomCompoundText, 0 },
#ifdef X_HAVE_UTF8_STRING
                    { atoms->atomUTF8String,   1 },
#endif
                    { XA_STRING,               2 },
                };

            int best_priority = INT_MAX;

            int i,j;
            for (i = 0 ; data.targetList[i] != 0; i++)
                {
                    for (j = 0; j < sizeof(target_priority_table)/sizeof(struct target_priority); j ++)
                        {
                            if ((data.targetList[i] == target_priority_table[j].target) &&
                                (target_priority_table[j].priority < best_priority))
                                {
                                    best_target = target_priority_table[j].target;
                                    best_priority = target_priority_table[j].priority;
                                }
                        }
                }
        }

        free(data.targetList);
        data.targetList = 0;

        winDebug("winClipboardWindowProc - best target is %d\n", best_target);

        /* No useful targets found */
        if (best_target == 0)
          goto fake_paste;

        winDebug("winClipboardWindowProc - requesting selection from owner\n");

        /* Request the selection contents */
        XConvertSelection(pDisplay,
                          selection,
                          best_target,
                          atoms->atomLocalProperty,
                          iWindow, CurrentTime);

        /* Process X events */
        iReturn = winProcessXEventsTimeout(hwnd,
                                           iWindow,
                                           pDisplay,
                                           &data,
                                           atoms,
                                           WIN_POLL_TIMEOUT);

        /*
         * winProcessXEventsTimeout had better have seen a notify event,
         * or else we are dealing with a buggy or old X11 app.
         */
        if (WIN_XEVENTS_NOTIFY_DATA != iReturn) {
            ErrorF
                ("winClipboardWindowProc - timed out waiting for WIN_XEVENTS_NOTIFY_DATA\n");
        }
        else {
            pasted = TRUE;
        }

         /*
          * If we couldn't get the data from the X clipboard, we
          * have to paste some fake data to the Win32 clipboard to
          * satisfy the requirement that we write something to it.
          */
    fake_paste:
        if (!pasted)
          {
            /* Paste no data, to satisfy required call to SetClipboardData */
            SetClipboardData(CF_UNICODETEXT, NULL);
            SetClipboardData(CF_TEXT, NULL);
          }

        winDebug("winClipboardWindowProc - WM_RENDERFORMAT - Returning.\n");
        return 0;
    }
    }

    /* Let Windows perform default processing for unhandled messages */
    return DefWindowProc(hwnd, message, wParam, lParam);
}

/*
 * Process any pending Windows messages
 */

Bool
winClipboardFlushWindowsMessageQueue(HWND hwnd)
{
    MSG msg;

    /* Flush the messaging window queue */
    /* NOTE: Do not pass the hwnd of our messaging window to PeekMessage,
     * as this will filter out many non-window-specific messages that
     * are sent to our thread, such as WM_QUIT.
     */
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        /* Dispatch the message if not WM_QUIT */
        if (msg.message == WM_QUIT)
            return FALSE;
        else
            DispatchMessage(&msg);
    }

    return TRUE;
}
