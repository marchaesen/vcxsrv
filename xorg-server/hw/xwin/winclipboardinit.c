/*
 *Copyright (C) 2003-2004 Harold L Hunt II All Rights Reserved.
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
#include "dixstruct.h"
#include "winclipboard.h"

/*
 * Local typedefs
 */

typedef int (*winDispatchProcPtr) (ClientPtr);

int winProcSetSelectionOwner(ClientPtr /* client */ );

/*
 * References to external symbols
 */

extern pthread_t g_ptClipboardProc;
extern winDispatchProcPtr winProcSetSelectionOwnerOrig;
extern Bool g_fClipboard;
extern HWND g_hwndClipboard;

/*
 * Intialize the Clipboard module
 */

Bool
winInitClipboard(void)
{
    ErrorF("winInitClipboard ()\n");

    /* Wrap some internal server functions */
    if (ProcVector[X_SetSelectionOwner] != winProcSetSelectionOwner) {
        winProcSetSelectionOwnerOrig = ProcVector[X_SetSelectionOwner];
        ProcVector[X_SetSelectionOwner] = winProcSetSelectionOwner;
    }

    /* Spawn a thread for the Clipboard module */
    if (pthread_create(&g_ptClipboardProc, NULL, winClipboardProc, NULL)) {
        /* Bail if thread creation failed */
        ErrorF("winInitClipboard - pthread_create failed.\n");
        return FALSE;
    }

    return TRUE;
}

/*
 * Create the Windows window that we use to recieve Windows messages
 */

HWND
winClipboardCreateMessagingWindow(void)
{
    WNDCLASSEX wc;
    HWND hwnd;

    /* Setup our window class */
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = winClipboardWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = WIN_CLIPBOARD_WINDOW_CLASS;
    wc.hIconSm = 0;
    RegisterClassEx(&wc);

    /* Create the window */
    hwnd = CreateWindowExA(0,   /* Extended styles */
                           WIN_CLIPBOARD_WINDOW_CLASS,  /* Class name */
                           WIN_CLIPBOARD_WINDOW_TITLE,  /* Window name */
                           WS_OVERLAPPED,       /* Not visible anyway */
                           CW_USEDEFAULT,       /* Horizontal position */
                           CW_USEDEFAULT,       /* Vertical position */
                           CW_USEDEFAULT,       /* Right edge */
                           CW_USEDEFAULT,       /* Bottom edge */
                           (HWND) NULL, /* No parent or owner window */
                           (HMENU) NULL,        /* No menu */
                           GetModuleHandle(NULL),       /* Instance handle */
                           NULL);       /* Creation data */
    assert(hwnd != NULL);

    /* I'm not sure, but we may need to call this to start message processing */
    ShowWindow(hwnd, SW_HIDE);

    /* Similarly, we may need a call to this even though we don't paint */
    UpdateWindow(hwnd);

    return hwnd;
}

void
winFixClipboardChain(void)
{
    if (g_fClipboard && g_hwndClipboard) {
        PostMessage(g_hwndClipboard, WM_WM_REINIT, 0, 0);
    }
}
