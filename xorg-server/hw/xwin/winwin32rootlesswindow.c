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
 * Authors:	Kensuke Matsuzaki
 *		Earle F. Philhower, III
 *		Harold L Hunt II
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"
#include "winprefs.h"

#if 0
/*
 * winMWExtWMReorderWindows
 */

void
winMWExtWMReorderWindows(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    HWND hwnd = NULL;
    win32RootlessWindowPtr pRLWin = NULL;
    win32RootlessWindowPtr pRLWinSib = NULL;
    DWORD dwCurrentProcessID = GetCurrentProcessId();
    DWORD dwWindowProcessID = 0;
    XID vlist[2];

#if CYGMULTIWINDOW_DEBUG && FALSE
    winDebug("winMWExtWMReorderWindows\n");
#endif

    pScreenPriv->fRestacking = TRUE;

    if (pScreenPriv->fWindowOrderChanged) {
#if CYGMULTIWINDOW_DEBUG
        winDebug("winMWExtWMReorderWindows - Need to restack\n");
#endif
        hwnd = GetTopWindow(NULL);

        while (hwnd) {
            GetWindowThreadProcessId(hwnd, &dwWindowProcessID);

            if ((dwWindowProcessID == dwCurrentProcessID)
                && GetProp(hwnd, WIN_WINDOW_PROP)) {
                pRLWinSib = pRLWin;
                pRLWin =
                    (win32RootlessWindowPtr) GetProp(hwnd, WIN_WINDOW_PROP);

                if (pRLWinSib) {
                    vlist[0] = pRLWinSib->pFrame->win->drawable.id;
                    vlist[1] = Below;

                    ConfigureWindow(pRLWin->pFrame->win,
                                    CWSibling | CWStackMode, vlist,
                                    wClient(pRLWin->pFrame->win));
                }
                else {
                    /* 1st window - raise to the top */
                    vlist[0] = Above;

                    ConfigureWindow(pRLWin->pFrame->win, CWStackMode,
                                    vlist, wClient(pRLWin->pFrame->win));
                }
            }
            hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
        }
    }

    pScreenPriv->fRestacking = FALSE;
    pScreenPriv->fWindowOrderChanged = FALSE;
}
#endif

/*
 * winMWExtWMMoveXWindow
 */

void
winMWExtWMMoveXWindow(WindowPtr pWin, int x, int y)
{
    CARD32 *vlist = malloc(sizeof(CARD32) * 2);

    vlist[0] = x;
    vlist[1] = y;
    ConfigureWindow(pWin, CWX | CWY, vlist, wClient(pWin));
    free(vlist);
}

/*
 * winMWExtWMResizeXWindow
 */

void
winMWExtWMResizeXWindow(WindowPtr pWin, int w, int h)
{
    CARD32 *vlist = malloc(sizeof(CARD32) * 2);

    vlist[0] = w;
    vlist[1] = h;
    ConfigureWindow(pWin, CWWidth | CWHeight, vlist, wClient(pWin));
    free(vlist);
}

/*
 * winMWExtWMMoveResizeXWindow
 */

void
winMWExtWMMoveResizeXWindow(WindowPtr pWin, int x, int y, int w, int h)
{
    CARD32 *vlist = malloc(sizeof(long) * 4);

    vlist[0] = x;
    vlist[1] = y;
    vlist[2] = w;
    vlist[3] = h;

    ConfigureWindow(pWin, CWX | CWY | CWWidth | CWHeight, vlist, wClient(pWin));
    free(vlist);
}

/*


 * winMWExtWMDecorateWindow - Update window style. Called by EnumWindows.
 */

wBOOL CALLBACK
winMWExtWMDecorateWindow(HWND hwnd, LPARAM lParam)
{
    win32RootlessWindowPtr pRLWinPriv = NULL;
    ScreenPtr pScreen = NULL;
    winPrivScreenPtr pScreenPriv = NULL;
    winScreenInfo *pScreenInfo = NULL;

    /* Check if the Windows window property for our X window pointer is valid */
    if ((pRLWinPriv =
         (win32RootlessWindowPtr) GetProp(hwnd, WIN_WINDOW_PROP)) != NULL) {
        if (pRLWinPriv != NULL && pRLWinPriv->pFrame != NULL &&
            pRLWinPriv->pFrame->win != NULL)
            pScreen = pRLWinPriv->pFrame->win->drawable.pScreen;
        if (pScreen)
            pScreenPriv = winGetScreenPriv(pScreen);
        if (pScreenPriv)
            pScreenInfo = pScreenPriv->pScreenInfo;
        if (pRLWinPriv && pScreenInfo)
            winMWExtWMUpdateWindowDecoration(pRLWinPriv, pScreenInfo);
    }
    return TRUE;
}

/*
 * winMWExtWMUpdateWindowDecoration - Update window style.
 */

void
winMWExtWMUpdateWindowDecoration(win32RootlessWindowPtr pRLWinPriv,
                                 winScreenInfoPtr pScreenInfo)
{
    Bool fDecorate = FALSE;
    DWORD dwExStyle = 0;
    WINDOWPLACEMENT wndPlace;
    UINT showCmd = 0;

    wndPlace.length = sizeof(WINDOWPLACEMENT);

    /* Get current window placement */
    GetWindowPlacement(pRLWinPriv->hWnd, &wndPlace);

    if (winIsInternalWMRunning(pScreenInfo)) {
        if (!pRLWinPriv->pFrame->win->overrideRedirect)
            fDecorate = TRUE;
    }
#if 0
    if (wndPlace.showCmd == SW_HIDE)
        return;                 //showCmd = SWP_HIDEWINDOW;
    else
        showCmd = SWP_SHOWWINDOW;
#else
    if (wndPlace.showCmd == SW_HIDE)
        return;

    if (IsWindowVisible(pRLWinPriv->hWnd))
        showCmd = SWP_SHOWWINDOW;
#endif

    showCmd |= SWP_NOMOVE | SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER;

    winDebug("winMWExtWMUpdateWindowDecoration %p %s\n",
             pRLWinPriv, fDecorate ? "Decorate" : "Bare");

    /* Get the extended window style information */
    dwExStyle = GetWindowLongPtr(pRLWinPriv->hWnd, GWL_EXSTYLE);

    if (fDecorate) {
        RECT rcNew;
        int iDx, iDy;
        winWMMessageRec wmMsg;

        winScreenPriv(pScreenInfo->pScreen);

        /* */
        if (!(dwExStyle & WS_EX_APPWINDOW)) {
            winDebug("\tBare=>Decorate\n");
            /* Setup a rectangle with the X window position and size */
            SetRect(&rcNew,
                    pRLWinPriv->pFrame->x,
                    pRLWinPriv->pFrame->y,
                    pRLWinPriv->pFrame->x + pRLWinPriv->pFrame->width,
                    pRLWinPriv->pFrame->y + pRLWinPriv->pFrame->height);

#ifdef CYGMULTIWINDOW_DEBUG
            winDebug("\tWindow extend {%d, %d, %d, %d}, {%d, %d}\n",
                     rcNew.left, rcNew.top, rcNew.right, rcNew.bottom,
                     rcNew.right - rcNew.left, rcNew.bottom - rcNew.top);
#endif
            /* */
            AdjustWindowRectEx(&rcNew,
                               WS_POPUP | WS_SIZEBOX | WS_OVERLAPPEDWINDOW,
                               FALSE, WS_EX_APPWINDOW);

#ifdef CYGMULTIWINDOW_DEBUG
            winDebug("\tAdjusted {%d, %d, %d, %d}, {%d, %d}\n",
                     rcNew.left, rcNew.top, rcNew.right, rcNew.bottom,
                     rcNew.right - rcNew.left, rcNew.bottom - rcNew.top);
#endif
            /* Calculate position deltas */
            iDx = pRLWinPriv->pFrame->x - rcNew.left;
            iDy = pRLWinPriv->pFrame->y - rcNew.top;

            /* Calculate new rectangle */
            rcNew.left += iDx;
            rcNew.right += iDx;
            rcNew.top += iDy;
            rcNew.bottom += iDy;

            /* Set the window extended style flags */
            SetWindowLongPtr(pRLWinPriv->hWnd, GWL_EXSTYLE, WS_EX_APPWINDOW);

            /* Set the window standard style flags */
            SetWindowLongPtr(pRLWinPriv->hWnd, GWL_STYLE,
                             WS_POPUP | WS_SIZEBOX | WS_OVERLAPPEDWINDOW);

#ifdef CYGMULTIWINDOW_DEBUG
            winDebug("\tWindowStyle: %08x %08x\n",
                     WS_POPUP | WS_SIZEBOX | WS_OVERLAPPEDWINDOW,
                     WS_EX_APPWINDOW);
#endif
            /* Position the Windows window */
#ifdef CYGMULTIWINDOW_DEBUG
            winDebug("\tMoved {%d, %d, %d, %d}, {%d, %d}\n",
                     rcNew.left, rcNew.top, rcNew.right, rcNew.bottom,
                     rcNew.right - rcNew.left, rcNew.bottom - rcNew.top);
#endif
            SetWindowPos(pRLWinPriv->hWnd, NULL,
                         rcNew.left, rcNew.top,
                         rcNew.right - rcNew.left, rcNew.bottom - rcNew.top,
                         showCmd);

            wmMsg.hwndWindow = pRLWinPriv->hWnd;
            wmMsg.iWindow = (Window) pRLWinPriv->pFrame->win->drawable.id;
            wmMsg.msg = WM_WM_NAME_EVENT;
            winSendMessageToWM(pScreenPriv->pWMInfo, &wmMsg);

            winMWExtWMReshapeFrame((RootlessFrameID) pRLWinPriv,
                                   wBoundingShape(pRLWinPriv->pFrame->win));
        }
    }
    else {
        RECT rcNew;

        /* */
        if (dwExStyle & WS_EX_APPWINDOW) {
            winDebug("\tDecorate=>Bare\n");
            /* Setup a rectangle with the X window position and size */
            SetRect(&rcNew,
                    pRLWinPriv->pFrame->x,
                    pRLWinPriv->pFrame->y,
                    pRLWinPriv->pFrame->x + pRLWinPriv->pFrame->width,
                    pRLWinPriv->pFrame->y + pRLWinPriv->pFrame->height);
#if 0
            /* */
            AdjustWindowRectEx(&rcNew,
                               WS_POPUP | WS_CLIPCHILDREN,
                               FALSE, WS_EX_TOOLWINDOW);

            /* Calculate position deltas */
            iDx = pRLWinPriv->pFrame->x - rcNew.left;
            iDy = pRLWinPriv->pFrame->y - rcNew.top;

            /* Calculate new rectangle */
            rcNew.left += iDx;
            rcNew.right += iDx;
            rcNew.top += iDy;
            rcNew.bottom += iDy;
#endif

            /* Hide window temporary to remove from taskbar. */
            ShowWindow(pRLWinPriv->hWnd, SW_HIDE);

            /* Set the window extended style flags */
            SetWindowLongPtr(pRLWinPriv->hWnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW);

            /* Set the window standard style flags */
            SetWindowLongPtr(pRLWinPriv->hWnd, GWL_STYLE,
                             WS_POPUP | WS_CLIPCHILDREN);

            /* Position the Windows window */
            SetWindowPos(pRLWinPriv->hWnd, NULL,
                         rcNew.left, rcNew.top,
                         rcNew.right - rcNew.left, rcNew.bottom - rcNew.top,
                         showCmd);

            winMWExtWMReshapeFrame((RootlessFrameID) pRLWinPriv,
                                   wBoundingShape(pRLWinPriv->pFrame->win));
        }
    }
}

/*
 * winIsInternalWMRunning (winScreenInfoPtr pScreenInfo)
 */
Bool
winIsInternalWMRunning(winScreenInfoPtr pScreenInfo)
{
    return pScreenInfo->fInternalWM && !pScreenInfo->fAnotherWMRunning;
}

/*
 * winMWExtWMRestackWindows
 */

void
winMWExtWMRestackWindows(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    WindowPtr pRoot = pScreen->root;
    WindowPtr pWin = NULL;
    WindowPtr pWinPrev = NULL;
    win32RootlessWindowPtr pRLWin = NULL;
    win32RootlessWindowPtr pRLWinPrev = NULL;
    int nWindow = 0;
    HDWP hWinPosInfo = NULL;

#if CYGMULTIWINDOW_DEBUG
    winDebug("winMWExtWMRestackWindows\n");
#endif

    pScreenPriv->fRestacking = TRUE;

    if (pRoot != NULL) {
        for (pWin = pRoot->firstChild; pWin; pWin = pWin->nextSib)
            nWindow++;

        hWinPosInfo = BeginDeferWindowPos(nWindow);

        for (pWin = pRoot->firstChild; pWin; pWin = pWin->nextSib) {
            if (pWin->realized) {
                UINT uFlags;

                pRLWin =
                    (win32RootlessWindowPtr) RootlessFrameForWindow(pWin,
                                                                    FALSE);
                if (pRLWin == NULL)
                    continue;

                if (pWinPrev)
                    pRLWinPrev =
                        (win32RootlessWindowPtr)
                        RootlessFrameForWindow(pWinPrev, FALSE);

                uFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW;
                if (pRLWinPrev != NULL)
                    uFlags |= SWP_NOACTIVATE;

#if CYGMULTIWINDOW_DEBUG
                winDebug
                    ("winMWExtWMRestackWindows - DeferWindowPos (%08x, %08x)\n",
                     pRLWin->hWnd, pRLWinPrev ? pRLWinPrev->hWnd : HWND_TOP);
#endif
                hWinPosInfo = DeferWindowPos(hWinPosInfo, pRLWin->hWnd,
                                             pRLWinPrev ? pRLWinPrev->
                                             hWnd : HWND_TOP, 0, 0, 0, 0,
                                             uFlags);
                if (hWinPosInfo == NULL) {
                    ErrorF
                        ("winMWExtWMRestackWindows - DeferWindowPos () failed: %d\n",
                         (int) GetLastError());
                    return;
                }
                pWinPrev = pWin;
            }
        }
        if (!EndDeferWindowPos(hWinPosInfo)) {
            ErrorF
                ("winMWExtWMRestackWindows - EndDeferWindowPos () failed: %d\n",
                 (int) GetLastError());
            return;
        }
    }

#if CYGMULTIWINDOW_DEBUG
    winDebug("winMWExtWMRestackWindows - done\n");
#endif
    pScreenPriv->fRestacking = FALSE;
}
