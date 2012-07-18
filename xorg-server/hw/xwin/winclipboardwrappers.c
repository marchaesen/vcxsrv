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
#include "win.h"
#include "dixstruct.h"
#include <X11/Xatom.h>

/*
 * Constants
 */

#define CLIP_NUM_SELECTIONS		2
#define CLIP_OWN_PRIMARY		0
#define CLIP_OWN_CLIPBOARD		1

/*
 * Local function prototypes
 */

int winProcEstablishConnection(ClientPtr /* client */ );
int winProcQueryTree(ClientPtr /* client */ );
int winProcSetSelectionOwner(ClientPtr /* client */ );

DISPATCH_PROC(winProcEstablishConnection);
DISPATCH_PROC(winProcSetSelectionOwner);

/*
 * References to external symbols
 */

extern int g_iNumScreens;
extern unsigned int g_uiAuthDataLen;
extern char *g_pAuthData;
extern Bool g_fXdmcpEnabled;
extern Bool g_fClipboardLaunched;
extern Bool g_fClipboardStarted;
extern Bool g_fClipboard;
extern Window g_iClipboardWindow;
extern Atom g_atomLastOwnedSelection;
extern HWND g_hwndClipboard;

extern winDispatchProcPtr winProcEstablishConnectionOrig;
extern winDispatchProcPtr winProcQueryTreeOrig;
extern winDispatchProcPtr winProcSetSelectionOwnerOrig;

/*
 * Wrapper for internal EstablishConnection function.
 * Initializes internal clients that must not be started until
 * an external client has connected.
 */

int
winProcEstablishConnection(ClientPtr client)
{
    int iReturn;
    static int s_iCallCount = 0;
    static unsigned long s_ulServerGeneration = 0;

    if (s_iCallCount == 0)
        winDebug("winProcEstablishConnection - Hello\n");

    /* Do nothing if clipboard is not enabled */
    if (!g_fClipboard) {
        ErrorF("winProcEstablishConnection - Clipboard is not enabled, "
               "returning.\n");

        /* Unwrap the original function, call it, and return */
        InitialVector[2] = winProcEstablishConnectionOrig;
        iReturn = (*winProcEstablishConnectionOrig) (client);
        winProcEstablishConnectionOrig = NULL;
        return iReturn;
    }

    /* Watch for server reset */
    if (s_ulServerGeneration != serverGeneration) {
        /* Save new generation number */
        s_ulServerGeneration = serverGeneration;

        /* Reset call count */
        s_iCallCount = 0;
    }

    /* Increment call count */
    ++s_iCallCount;

    /*
     * This procedure is only used for initialization.
     * We can unwrap the original procedure at this point
     * so that this function is no longer called until the
     * server resets and the function is wrapped again.
     */
    InitialVector[2] = winProcEstablishConnectionOrig;

    /*
     * Call original function and bail if it fails.
     * NOTE: We must do this first, since we need XdmcpOpenDisplay
     * to be called before we initialize our clipboard client.
     */
    iReturn = (*winProcEstablishConnectionOrig) (client);
    if (iReturn != 0) {
        ErrorF("winProcEstablishConnection - ProcEstablishConnection "
               "failed, bailing.\n");
        return iReturn;
    }

    /* Clear original function pointer */
    winProcEstablishConnectionOrig = NULL;

    /* If the clipboard client has already been started, abort */
    if (g_fClipboardLaunched) {
        ErrorF("winProcEstablishConnection - Clipboard client already "
               "launched, returning.\n");
        return iReturn;
    }

    /* Startup the clipboard client if clipboard mode is being used */
    if (g_fClipboard) {
        /*
         * NOTE: The clipboard client is started here for a reason:
         * 1) Assume you are using XDMCP (e.g. XWin -query %hostname%)
         * 2) If the clipboard client attaches during X Server startup,
         *    then it becomes the "magic client" that causes the X Server
         *    to reset if it exits.
         * 3) XDMCP calls KillAllClients when it starts up.
         * 4) The clipboard client is a client, so it is killed.
         * 5) The clipboard client is the "magic client", so the X Server
         *    resets itself.
         * 6) This repeats ad infinitum.
         * 7) We avoid this by waiting until at least one client (could
         *    be XDM, could be another client) connects, which makes it
         *    almost certain that the clipboard client will not connect
         *    until after XDM when using XDMCP.
         */

        /* Create the clipboard client thread */
        if (!winInitClipboard()) {
            ErrorF("winProcEstablishConnection - winClipboardInit "
                   "failed.\n");
            return iReturn;
        }

        ErrorF("winProcEstablishConnection - winInitClipboard returned.\n");
    }

    /* Flag that clipboard client has been launched */
    g_fClipboardLaunched = TRUE;

    return iReturn;
}

/*
 * Wrapper for internal SetSelectionOwner function.
 * Grabs ownership of Windows clipboard when X11 clipboard owner changes.
 */

int
winProcSetSelectionOwner(ClientPtr client)
{
    int i;
    DrawablePtr pDrawable;
    WindowPtr pWindow = None;
    Bool fOwnedToNotOwned = FALSE;
    static Window s_iOwners[CLIP_NUM_SELECTIONS] = { None };
    static unsigned long s_ulServerGeneration = 0;

    REQUEST(xSetSelectionOwnerReq);

    REQUEST_SIZE_MATCH(xSetSelectionOwnerReq);

    winDebug("winProcSetSelectionOwner - Hello.\n");

    /* Watch for server reset */
    if (s_ulServerGeneration != serverGeneration) {
        /* Save new generation number */
        s_ulServerGeneration = serverGeneration;

        /* Initialize static variables */
        for (i = 0; i < CLIP_NUM_SELECTIONS; ++i)
            s_iOwners[i] = None;
    }

    /* Abort if clipboard not completely initialized yet */
    if (!g_fClipboardStarted) {
        /* ErrorF ("winProcSetSelectionOwner - Clipboard not yet started, "
           "aborting.\n"); */
        goto winProcSetSelectionOwner_Done;
    }

    /* Grab window if we have one */
    if (None != stuff->window) {
        /* Grab the Window from the request */
        int rc =
            dixLookupWindow(&pWindow, stuff->window, client, DixReadAccess);
        if (rc != Success) {
            ErrorF("winProcSetSelectionOwner - Found BadWindow, aborting.\n");
            goto winProcSetSelectionOwner_Done;
        }
    }

    /* Now we either have a valid window or None */

    /* Save selection owners for monitored selections, ignore other selections */
    if (XA_PRIMARY == stuff->selection) {
        /* Look for owned -> not owned transition */
        if (None == stuff->window && None != s_iOwners[CLIP_OWN_PRIMARY]) {
            fOwnedToNotOwned = TRUE;

            winDebug("winProcSetSelectionOwner - PRIMARY - Going from "
                     "owned to not owned.\n");

            /* Adjust last owned selection */
            if (None != s_iOwners[CLIP_OWN_CLIPBOARD])
                g_atomLastOwnedSelection = MakeAtom("CLIPBOARD", 9, TRUE);
            else
                g_atomLastOwnedSelection = None;
        }

        /* Save new selection owner or None */
        s_iOwners[CLIP_OWN_PRIMARY] = stuff->window;

        winDebug("winProcSetSelectionOwner - PRIMARY - Now owned by: %d\n",
                 stuff->window);
    }
    else if (MakeAtom("CLIPBOARD", 9, TRUE) == stuff->selection) {
        /* Look for owned -> not owned transition */
        if (None == stuff->window && None != s_iOwners[CLIP_OWN_CLIPBOARD]) {
            fOwnedToNotOwned = TRUE;

            winDebug("winProcSetSelectionOwner - CLIPBOARD - Going from "
                     "owned to not owned.\n");

            /* Adjust last owned selection */
            if (None != s_iOwners[CLIP_OWN_PRIMARY])
                g_atomLastOwnedSelection = XA_PRIMARY;
            else
                g_atomLastOwnedSelection = None;
        }

        /* Save new selection owner or None */
        s_iOwners[CLIP_OWN_CLIPBOARD] = stuff->window;

        winDebug("winProcSetSelectionOwner - CLIPBOARD - Now owned by: %d\n",
                 stuff->window);

    }
    else
        goto winProcSetSelectionOwner_Done;

    /*
     * At this point, if one of the selections is still owned by the 
     * clipboard manager then it should be marked as unowned since
     * we will be taking ownership of the Win32 clipboard.
     */
    if (g_iClipboardWindow == s_iOwners[CLIP_OWN_PRIMARY])
        s_iOwners[CLIP_OWN_PRIMARY] = None;
    if (g_iClipboardWindow == s_iOwners[CLIP_OWN_CLIPBOARD])
        s_iOwners[CLIP_OWN_CLIPBOARD] = None;

    /*
     * Handle case when selection is being disowned,
     * WM_DRAWCLIPBOARD did not do the disowning,
     * both monitored selections are no longer owned,
     * an owned to not owned transition was detected,
     * and we currently own the Win32 clipboard.
     */
    if (stuff->window == None
        && s_iOwners[CLIP_OWN_PRIMARY] == None
        && s_iOwners[CLIP_OWN_CLIPBOARD] == None
        && fOwnedToNotOwned
        && g_hwndClipboard != NULL && g_hwndClipboard == GetClipboardOwner()) {
        winDebug("winProcSetSelectionOwner - We currently own the "
                 "clipboard and neither the PRIMARY nor the CLIPBOARD "
                 "selections are owned, releasing ownership of Win32 "
                 "clipboard.\n");

        /* Release ownership of the Windows clipboard */
        OpenClipboard(NULL);
        EmptyClipboard();
        CloseClipboard();

        goto winProcSetSelectionOwner_Done;
    }

    /* Abort if no window at this point */
    if (None == stuff->window) {
        winDebug("winProcSetSelectionOwner - No window, returning.\n");
        goto winProcSetSelectionOwner_Done;
    }

    /* Abort if invalid selection */
    if (!ValidAtom(stuff->selection)) {
        ErrorF("winProcSetSelectionOwner - Found BadAtom, aborting.\n");
        goto winProcSetSelectionOwner_Done;
    }

    /* Cast Window to Drawable */
    pDrawable = (DrawablePtr) pWindow;

    /* Abort if clipboard manager is owning the selection */
    if (pDrawable->id == g_iClipboardWindow) {
        winDebug("winProcSetSelectionOwner - We changed ownership, "
                 "aborting.\n");
        goto winProcSetSelectionOwner_Done;
    }

    /* Abort if root window is taking ownership */
    if (pDrawable->id == 0) {
        ErrorF("winProcSetSelectionOwner - Root window taking ownership, "
               "aborting\n");
        goto winProcSetSelectionOwner_Done;
    }

    /* Close clipboard if we have it open already */
    if (GetOpenClipboardWindow() == g_hwndClipboard) {
        CloseClipboard();
    }

    /* Access the Windows clipboard */
    if (!OpenClipboard(g_hwndClipboard)) {
        ErrorF("winProcSetSelectionOwner - OpenClipboard () failed: %08x\n",
               (int) GetLastError());
        goto winProcSetSelectionOwner_Done;
    }

    /* Take ownership of the Windows clipboard */
    if (!EmptyClipboard()) {
        ErrorF("winProcSetSelectionOwner - EmptyClipboard () failed: %08x\n",
               (int) GetLastError());
        goto winProcSetSelectionOwner_Done;
    }

    /* Advertise regular text and unicode */
    SetClipboardData(CF_UNICODETEXT, NULL);
    SetClipboardData(CF_TEXT, NULL);

    /* Save handle to last owned selection */
    g_atomLastOwnedSelection = stuff->selection;

    /* Release the clipboard */
    if (!CloseClipboard()) {
        ErrorF("winProcSetSelectionOwner - CloseClipboard () failed: "
               "%08x\n", (int) GetLastError());
        goto winProcSetSelectionOwner_Done;
    }

 winProcSetSelectionOwner_Done:
    return (*winProcSetSelectionOwnerOrig) (client);
}
