/*
 *Copyright (C) 1994-2000 The XFree86 Project, Inc. All Rights Reserved.
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
 *              Colin Harrison
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif

#include "win.h"
#include "dixevents.h"
#include "winmultiwindowclass.h"
#include "winmultiwindowicons.h"

/*
 * Prototypes for local functions
 */

void
 winCreateWindowsWindow(WindowPtr pWin);

static void
 winDestroyWindowsWindow(WindowPtr pWin);

static void
 winUpdateWindowsWindow(WindowPtr pWin);

static void
 winFindWindow(void *value, XID id, void *cdata);

static
    void
winInitMultiWindowClass(void)
{
    static wATOM atomXWinClass = 0;
    WNDCLASSEX wcx;

    if (atomXWinClass == 0) {
        HICON hIcon, hIconSmall;

        /* Load the default icons */
        winSelectIcons(&hIcon, &hIconSmall);

        /* Setup our window class */
        wcx.cbSize = sizeof(WNDCLASSEX);
        wcx.style = CS_HREDRAW | CS_VREDRAW | (g_fNativeGl ? CS_OWNDC : 0);
        wcx.lpfnWndProc = winTopLevelWindowProc;
        wcx.cbClsExtra = 0;
        wcx.cbWndExtra = WND_EXTRABYTES;
        wcx.hInstance = g_hInstance;
        wcx.hIcon = hIcon;
        wcx.hCursor = 0;
        wcx.hbrBackground = NULL;
        wcx.lpszMenuName = NULL;
        wcx.lpszClassName = WINDOW_CLASS_X;
        wcx.hIconSm = hIconSmall;

#if CYGMULTIWINDOW_DEBUG
        winDebug ("winCreateWindowsWindow - Creating class: %s\n", WINDOW_CLASS_X);
#endif

        atomXWinClass = RegisterClassEx(&wcx);
    }
}

/*
 * CreateWindow - See Porting Layer Definition - p. 37
 */

Bool
winCreateWindowMultiWindow(WindowPtr pWin)
{
    Bool fResult = TRUE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winWindowPriv(pWin);
    winScreenPriv(pScreen);

#if CYGMULTIWINDOW_DEBUG
    winDebug ("winCreateWindowMultiWindow - pWin: %p\n", pWin);
#endif

    WIN_UNWRAP(CreateWindow);
    fResult = (*pScreen->CreateWindow) (pWin);
    WIN_WRAP(CreateWindow, winCreateWindowMultiWindow);

    /* Initialize some privates values */
    pWinPriv->hRgn = NULL;
    pWinPriv->hWnd = NULL;
    pWinPriv->pScreenPriv = winGetScreenPriv(pWin->drawable.pScreen);
    pWinPriv->fXKilled = FALSE;
#ifdef XWIN_GLX_WINDOWS
    pWinPriv->fWglUsed = FALSE;
#endif

    return fResult;
}

/*
 * DestroyWindow - See Porting Layer Definition - p. 37
 */

Bool
winDestroyWindowMultiWindow(WindowPtr pWin)
{
    Bool fResult = TRUE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winWindowPriv(pWin);
    winScreenPriv(pScreen);

#if CYGMULTIWINDOW_DEBUG
    winDebug ("winDestroyWindowMultiWindow - pWin: %p\n", pWin);
#endif

    WIN_UNWRAP(DestroyWindow);
    fResult = (*pScreen->DestroyWindow) (pWin);
    WIN_WRAP(DestroyWindow, winDestroyWindowMultiWindow);

    /* Flag that the window has been destroyed */
    pWinPriv->fXKilled = TRUE;

    /* Kill the MS Windows window associated with this window */
    winDestroyWindowsWindow(pWin);

    return fResult;
}

/*
 * PositionWindow - See Porting Layer Definition - p. 37
 *
 * This function adjusts the position and size of Windows window
 * with respect to the underlying X window.  This is the inverse
 * of winAdjustXWindow, which adjusts X window to Windows window.
 */

Bool
winPositionWindowMultiWindow(WindowPtr pWin, int x, int y)
{
    Bool fResult = TRUE;
    int iX, iY, iWidth, iHeight;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winWindowPriv(pWin);
    winScreenPriv(pScreen);

    HWND hWnd = pWinPriv->hWnd;
    RECT rcNew;
    RECT rcOld;

#ifdef WINDBG
    RECT rcClient;
    RECT *lpRc;
#endif
    DWORD dwExStyle;
    DWORD dwStyle;

    winDebug ("winPositionWindowMultiWindow - pWin: %p\n", pWin);

    WIN_UNWRAP(PositionWindow);
    fResult = (*pScreen->PositionWindow) (pWin, x, y);
    WIN_WRAP(PositionWindow, winPositionWindowMultiWindow);

    winDebug ("winPositionWindowMultiWindow: (x, y) = (%d, %d)\n", x, y);

    /* Bail out if the Windows window handle is bad */
    if (!hWnd) {
        winDebug ("\timmediately return since hWnd is NULL\n");
        if (pWin->redirectDraw != RedirectDrawNone)
        {
          winDebug("winPositionWindowMultiWindow: Calling compReallocPixmap to make sure the pixmap buffer is valid.\n");
          compReallocPixmap(pWin, x, y, pWin->drawable.width, pWin->drawable.height, pWin->borderWidth);
        }
        return fResult;
    }

    /* Get the Windows window style and extended style */
    dwExStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
    dwStyle = GetWindowLongPtr(hWnd, GWL_STYLE);

    /* Get the X and Y location of the X window */
    iX = pWin->drawable.x + GetSystemMetrics(SM_XVIRTUALSCREEN);
    iY = pWin->drawable.y + GetSystemMetrics(SM_YVIRTUALSCREEN);

    /* Get the height and width of the X window */
    iWidth = pWin->drawable.width;
    iHeight = pWin->drawable.height;

    /* Store the origin, height, and width in a rectangle structure */
    SetRect(&rcNew, iX, iY, iX + iWidth, iY + iHeight);

#if CYGMULTIWINDOW_DEBUG
    lpRc = &rcNew;
    winDebug("winPositionWindowMultiWindow - drawable (%d, %d)-(%d, %d)\n",
           (int)lpRc->left, (int)lpRc->top, (int)lpRc->right, (int)lpRc->bottom);
#endif

    /*
     * Calculate the required size of the Windows window rectangle,
     * given the size of the Windows window client area.
     */
    AdjustWindowRectEx(&rcNew, dwStyle, FALSE, dwExStyle);

    /* Get a rectangle describing the old Windows window */
    GetWindowRect(hWnd, &rcOld);

#if CYGMULTIWINDOW_DEBUG
    /* Get a rectangle describing the Windows window client area */
    GetClientRect(hWnd, &rcClient);

    lpRc = &rcNew;
    winDebug("winPositionWindowMultiWindow - rcNew (%d, %d)-(%d, %d)\n",
           (int)lpRc->left, (int)lpRc->top, (int)lpRc->right, (int)lpRc->bottom);

    lpRc = &rcOld;
    winDebug("winPositionWindowMultiWindow - rcOld (%d, %d)-(%d, %d)\n",
           (int)lpRc->left, (int)lpRc->top, (int)lpRc->right, (int)lpRc->bottom);

    lpRc = &rcClient;
    winDebug("rcClient (%d, %d)-(%d, %d)\n",
           (int)lpRc->left, (int)lpRc->top, (int)lpRc->right, (int)lpRc->bottom);
#endif

    /* Check if the old rectangle and new rectangle are the same */
    if (!EqualRect(&rcNew, &rcOld)) {
      winDebug ("winPositionWindowMultiWindow - Need to move\n");
        winDebug("\tMoveWindow to (%d, %d) - %dx%d\n", (int)rcNew.left, (int)rcNew.top,
               (int)(rcNew.right - rcNew.left), (int)(rcNew.bottom - rcNew.top));

        /* Change the position and dimensions of the Windows window */
      if (pWinPriv->fWglUsed)
      {
        int iWidth=rcNew.right - rcNew.left;
        int iHeight=rcNew.bottom - rcNew.top;
        ScreenToClient(GetParent(hWnd), (LPPOINT)&rcNew);
        MoveWindow (hWnd,
                    rcNew.left, rcNew.top,
                    iWidth, iHeight, TRUE);
      }
      else
        MoveWindow (hWnd,
                    rcNew.left, rcNew.top,
                    rcNew.right - rcNew.left, rcNew.bottom - rcNew.top, TRUE);
    }
    else {
      winDebug ("winPositionWindowMultiWindow - Not need to move\n");
    }

    return fResult;
}

/*
 * ChangeWindowAttributes - See Porting Layer Definition - p. 37
 */

Bool
winChangeWindowAttributesMultiWindow(WindowPtr pWin, unsigned long mask)
{
    Bool fResult = TRUE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

    winDebug ("winChangeWindowAttributesMultiWindow - pWin: %p\n", pWin);

    WIN_UNWRAP(ChangeWindowAttributes);
    fResult = (*pScreen->ChangeWindowAttributes) (pWin, mask);
    WIN_WRAP(ChangeWindowAttributes, winChangeWindowAttributesMultiWindow);

    /*
     * NOTE: We do not currently need to do anything here.
     */

    return fResult;
}

/*
 * UnmapWindow - See Porting Layer Definition - p. 37
 * Also referred to as UnrealizeWindow
 */

Bool
winUnmapWindowMultiWindow(WindowPtr pWin)
{
    Bool fResult = TRUE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winWindowPriv(pWin);
    winScreenPriv(pScreen);

#if CYGMULTIWINDOW_DEBUG
    winDebug ("winUnmapWindowMultiWindow - pWin: %p\n", pWin);
#endif

    WIN_UNWRAP(UnrealizeWindow);
    fResult = (*pScreen->UnrealizeWindow) (pWin);
    WIN_WRAP(UnrealizeWindow, winUnmapWindowMultiWindow);

    /* Flag that the window has been killed */
    pWinPriv->fXKilled = TRUE;

    /* Destroy the Windows window associated with this X window */
    winDestroyWindowsWindow(pWin);

    return fResult;
}

/*
 * MapWindow - See Porting Layer Definition - p. 37
 * Also referred to as RealizeWindow
 */

Bool
winMapWindowMultiWindow(WindowPtr pWin)
{
    Bool fResult = TRUE;
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winWindowPriv(pWin);
    winScreenPriv(pScreen);

#if CYGMULTIWINDOW_DEBUG
    winDebug ("winMapWindowMultiWindow - pWin: %p\n", pWin);
#endif

    WIN_UNWRAP(RealizeWindow);
    fResult = (*pScreen->RealizeWindow) (pWin);
    WIN_WRAP(RealizeWindow, winMapWindowMultiWindow);

    /* Flag that this window has not been destroyed */
    pWinPriv->fXKilled = FALSE;

    /* Refresh/redisplay the Windows window associated with this X window */
    winUpdateWindowsWindow(pWin);

    /* Update the Windows window's shape */
    winReshapeMultiWindow(pWin);
    winUpdateRgnMultiWindow(pWin);

    return fResult;
}

/*
 * ReparentWindow - See Porting Layer Definition - p. 42
 */

void
winReparentWindowMultiWindow(WindowPtr pWin, WindowPtr pPriorParent)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

    winDebug
        ("winReparentMultiWindow - pWin:%p XID:0x%x, reparent from pWin:%p XID:0x%x to pWin:%p XID:0x%x\n",
         pWin, (unsigned int)pWin->drawable.id,
         pPriorParent, (unsigned int)pPriorParent->drawable.id,
         pWin->parent, (unsigned int)pWin->parent->drawable.id);

    WIN_UNWRAP(ReparentWindow);
    if (pScreen->ReparentWindow)
        (*pScreen->ReparentWindow) (pWin, pPriorParent);
    WIN_WRAP(ReparentWindow, winReparentWindowMultiWindow);

    /* Update the Windows window associated with this X window */
    winUpdateWindowsWindow(pWin);
}

static int localConfigureWindow;
int
winConfigureWindow(WindowPtr pWin, Mask mask, XID *vlist, ClientPtr client)
{
  localConfigureWindow++;
  int ret=ConfigureWindow(pWin, mask, vlist, client);
  localConfigureWindow--;
  return ret;
}

static int isWindowOnTop(HWND hAbove, HWND hBeneath)
{
    HWND hNext=GetNextWindow(hAbove, GW_HWNDNEXT);
    while (hNext && hNext!=hBeneath)
       hNext=GetNextWindow(hNext, GW_HWNDNEXT);
    return hNext==hBeneath;
}

/*
 * RestackWindow - Shuffle the z-order of a window
 */

void
winRestackWindowMultiWindow(WindowPtr pWin, WindowPtr pOldNextSib)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

    winDebug ("winRestackMultiWindow - %p\n", pWin);

    WIN_UNWRAP(RestackWindow);
    if (pScreen->RestackWindow)
        (*pScreen->RestackWindow) (pWin, pOldNextSib);
    WIN_WRAP(RestackWindow, winRestackWindowMultiWindow);

    /*
     * Calling winReorderWindowsMultiWindow here means our window manager
     * (i.e. Windows Explorer) has initiative to determine Z order.
     */
    if (pWin->nextSib != pOldNextSib)
        winReorderWindowsMultiWindow();
}

/*
 * winCreateWindowsWindow - Create a Windows window associated with an X window
 */

void
winCreateWindowsWindow(WindowPtr pWin)
{
    int iX, iY;
    int iWidth;
    int iHeight;
    HWND hWnd;
    HWND hFore = NULL;

    winWindowPriv(pWin);
    WinXSizeHints hints;
    Window daddyId;
    DWORD dwStyle, dwExStyle;
    RECT rc;

    winInitMultiWindowClass();

    winDebug("winCreateWindowsTopLevelWindow - pWin:%p XID:0x%x \n", pWin,
             (unsigned int)pWin->drawable.id);

    iX = pWin->drawable.x + GetSystemMetrics(SM_XVIRTUALSCREEN);
    iY = pWin->drawable.y + GetSystemMetrics(SM_YVIRTUALSCREEN);

    iWidth = pWin->drawable.width;
    iHeight = pWin->drawable.height;

    /* If it's an InputOutput window, and so is going to end up being made visible,
     make sure the window actually ends up somewhere where it will be visible 
     Dont't do it by making just one of the two iX and iY CW_USEDEFAULT since
     this will create a window at place CW_USEDEFAULT which is 0x80000000 */
    if (pWin->drawable.class != InputOnly) {
      while (1) {
        if (iX < GetSystemMetrics (SM_XVIRTUALSCREEN)) {
          iX = GetSystemMetrics (SM_XVIRTUALSCREEN);
          ErrorF("Resetting iX to %d\n",iX);
        }
        else if  (iX > GetSystemMetrics (SM_CXVIRTUALSCREEN))
        {
          iX = GetSystemMetrics (SM_CXVIRTUALSCREEN)-iWidth;
          ErrorF("Resetting iX to %d\n",iX);
        }
        else
          break;
      }

      while (1) {
        if (iY < GetSystemMetrics (SM_YVIRTUALSCREEN)) {
          iY = GetSystemMetrics (SM_YVIRTUALSCREEN);
          ErrorF("Resetting iY to %d\n",iY);
        }
        else if (iY > GetSystemMetrics (SM_CYVIRTUALSCREEN)) {
          iY = GetSystemMetrics (SM_CYVIRTUALSCREEN)-iHeight;
          ErrorF("Resetting iY to %d\n",iY);
        }
        else
          break;
      }
    }

    winDebug("winCreateWindowsWindow - 1 - %dx%d @ %dx%d\n", iWidth, iHeight, iX,
             iY);

    if (winMultiWindowGetTransientFor(pWin, &daddyId)) {
        if (daddyId && !pWin->overrideRedirect) {
            WindowPtr pParent;
            int res = dixLookupWindow(&pParent, daddyId, serverClient, DixReadAccess);
            if (res == Success) {
                winPrivWinPtr pParentPriv = winGetWindowPriv(pParent);
                hFore = pParentPriv->hWnd;
            }
        }
    }
    else if (!pWin->overrideRedirect) {
        /* Default positions if none specified */
        if (!winMultiWindowGetWMNormalHints(pWin, &hints))
            hints.flags = 0;


        if ((hints.flags & USPosition) ||
            ((hints.flags & PPosition) &&
             ((pWin->drawable.x - pWin->borderWidth != 0) ||
              (pWin->drawable.y - pWin->borderWidth != 0)))) {
            /*
              Always respect user specified position, respect program
              specified position if it's not the origin
            */
        }
        else {
            /* Use default position */
            iX = CW_USEDEFAULT;
            iY = CW_USEDEFAULT;
        }
    }

    winDebug("winCreateWindowsWindow - 2 - %dx%d @ %dx%d\n", iWidth,
             iHeight, iX, iY);

    /* Make it WS_OVERLAPPED in create call since WS_POPUP doesn't support */
    /* CW_USEDEFAULT, change back to popup after creation */
    dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    dwExStyle = WS_EX_TOOLWINDOW;

    /*
       Calculate the window coordinates containing the requested client area,
       being careful to preseve CW_USEDEFAULT
     */
    rc.top = (iY != CW_USEDEFAULT) ? iY : 0;
    rc.left = (iX != CW_USEDEFAULT) ? iX : 0;
    rc.bottom = rc.top + iHeight;
    rc.right = rc.left + iWidth;
    AdjustWindowRectEx(&rc, dwStyle, FALSE, dwExStyle);
    if (iY != CW_USEDEFAULT)
        iY = rc.top;
    if (iX != CW_USEDEFAULT)
        iX = rc.left;
    iHeight = rc.bottom - rc.top;
    iWidth = rc.right - rc.left;

    winDebug("winCreateWindowsWindow - 3 - %dx%d @ %dx%d\n", iWidth, iHeight, iX,
             iY);

    /* Create the window */
    hWnd = CreateWindowExA(dwExStyle,   /* Extended styles */
                           WINDOW_CLASS_X,      /* Class name */
                           WINDOW_TITLE_X,      /* Window name */
                           dwStyle,     /* Styles */
                           iX,  /* Horizontal position */
                           iY,  /* Vertical position */
                           iWidth,      /* Right edge */
                           iHeight,     /* Bottom edge */
                           hFore,       /* Null or Parent window if transient */
                           (HMENU) NULL,        /* No menu */
                           g_hInstance,       /* Instance handle */
                           pWin);       /* ScreenPrivates */
    if (hWnd == NULL) {
        ErrorF("winCreateWindowsWindow - CreateWindowExA () failed: %d\n",
               (int) GetLastError());
    }
    pWinPriv->hWnd = hWnd;

    /* If we asked the native WM to place the window, synchronize the X window position.
       Do this before the next SetWindowPos because this one is generating a WM_STYLECHANGED
       message which is causing a window move, which is wrong if the Xwindow does not
       have the correct coordinates yet */
    if (iX == CW_USEDEFAULT) {
      winAdjustXWindow(pWin, hWnd);
    }
    /* Change style back to popup, already placed... */
    SetWindowLongPtr(hWnd, GWL_STYLE,
                     WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    SetWindowPos(hWnd, 0, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE |
                 SWP_NOACTIVATE);

    /* Make sure it gets the proper system menu for a WS_POPUP, too */
    GetSystemMenu(hWnd, TRUE);

    /* Cause any .XWinrc menus to be added in main WNDPROC */
    PostMessage(hWnd, WM_INIT_SYS_MENU, 0, 0);

    SetProp(hWnd, WIN_WID_PROP, (HANDLE) (INT_PTR) winGetWindowID(pWin));

    /* Flag that this Windows window handles its own activation */
    SetProp(hWnd, WIN_NEEDMANAGE_PROP, (HANDLE) 0);
}

Bool winInDestroyWindowsWindow = FALSE;

/*
 * winDestroyWindowsWindow - Destroy a Windows window associated
 * with an X window
 */
static void
winDestroyWindowsWindow(WindowPtr pWin)
{
    MSG msg;

    winWindowPriv(pWin);
    BOOL oldstate = winInDestroyWindowsWindow;
    HICON hIcon;
    HICON hIconSm;

    winDebug("winDestroyWindowsWindow - pWin:%p XID:0x%x \n", pWin,
             (unsigned int)pWin->drawable.id);

    /* Bail out if the Windows window handle is invalid */
    if (pWinPriv->hWnd == NULL)
        return;

    winInDestroyWindowsWindow = TRUE;

    /* Store the info we need to destroy after this window is gone */
    hIcon = (HICON) SendMessage(pWinPriv->hWnd, WM_GETICON, ICON_BIG, 0);
    hIconSm = (HICON) SendMessage(pWinPriv->hWnd, WM_GETICON, ICON_SMALL, 0);

    /* Destroy the Windows window */
    DestroyWindow(pWinPriv->hWnd);

    /* Null our handle to the Window so referencing it will cause an error */
    pWinPriv->hWnd = NULL;

    /* Destroy any icons we created for this window */
    winDestroyIcon(hIcon);
    winDestroyIcon(hIconSm);

#ifdef XWIN_GLX_WINDOWS
    /* No longer note WGL used on this window */
    pWinPriv->fWglUsed = FALSE;
#endif

    /* Process all messages on our queue 
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (g_hDlgDepthChange == 0 || !IsDialogMessage(g_hDlgDepthChange, &msg)) {
            DispatchMessage(&msg);
        }
    }*/

    winInDestroyWindowsWindow = oldstate;

    winDebug("winDestroyWindowsWindow - done\n");
}

/*
 * winUpdateWindowsWindow - Redisplay/redraw a Windows window
 * associated with an X window
 */

static void
winUpdateWindowsWindow(WindowPtr pWin)
{
    winWindowPriv(pWin);
    HWND hWnd = pWinPriv->hWnd;

#if CYGMULTIWINDOW_DEBUG
    winDebug("winUpdateWindowsWindow\n");
#endif

    /* Check if the Windows window's parents have been destroyed */
    if (pWin->parent != NULL && pWin->parent->parent == NULL && pWin->mapped) {
        /* Create the Windows window if it has been destroyed */
        if (hWnd == NULL) {
            winCreateWindowsWindow(pWin);
            assert(pWinPriv->hWnd != NULL);
        }

        /* Display the window without activating it */
        if (pWin->drawable.class != InputOnly)
            ShowWindow(pWinPriv->hWnd, SW_SHOWNOACTIVATE);

        /* Send first paint message */
        UpdateWindow(pWinPriv->hWnd); // IZI 2019
    }
    else if (hWnd != NULL) {
        if (pWinPriv->fWglUsed) {
            /* We do not need to destroy the window but to reparent it and move it to the
               correct place when it is an opengl window */
            int offsetx;
            int offsety;
            HWND hParentWnd;
            WindowPtr pParent=pWin->parent;

            while (pParent) {
                winWindowPriv(pParent);
                hParentWnd=pWinPriv->hWnd;
                if (hParentWnd)
                    break;
                pParent=pParent->parent;
            }

            if (pParent) {
                offsetx=pParent->drawable.x;
                offsety=pParent->drawable.y;
            }
            else {
                offsetx=0;
                offsety=0;
            }
            if (hParentWnd == NULL)
                winDestroyWindowsWindow (pWin);
            else {
                winDebug ("-winUpdateWindowsWindow: %x changing parent to %x and moving to %d,%d\n",pWinPriv->hWnd,hParentWnd,pWin->drawable.x-offsetx,pWin->drawable.y-offsety);
                SetParent(pWinPriv->hWnd,hParentWnd);
                SetWindowPos(pWinPriv->hWnd,NULL,pWin->drawable.x-offsetx,pWin->drawable.y-offsety,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_SHOWWINDOW);
            }
        }
        else {
            /* Destroy the Windows window if its parents are destroyed */
            /* First check if we need to release the DC when it is an opengl window */
            winDestroyWindowsWindow (pWin);
            assert (pWinPriv->hWnd == NULL);
        }
    }

#if CYGMULTIWINDOW_DEBUG
    winDebug ("-winUpdateWindowsWindow\n");
#endif
}

/*
 * winGetWindowID -
 */

XID
winGetWindowID(WindowPtr pWin)
{
    WindowIDPairRec wi = { pWin, 0 };
    ClientPtr c = wClient(pWin);

    /* */
    FindClientResourcesByType(c, RT_WINDOW, winFindWindow, &wi);

#if CYGMULTIWINDOW_DEBUG
    winDebug("winGetWindowID - Window ID: %u\n", (unsigned int)wi.id);
#endif

    return wi.id;
}

/*
 * winFindWindow -
 */

static void
winFindWindow(void *value, XID id, void *cdata)
{
    WindowIDPairPtr wi = (WindowIDPairPtr) cdata;

    if (value == wi->value) {
        wi->id = id;
    }
}

/*
 * winReorderWindowsMultiWindow -
 */

void
winReorderWindowsMultiWindow(void)
{
    HWND hwnd = NULL;
    WindowPtr pWin = NULL;
    WindowPtr pWinSib = NULL;
    XID vlist[2];
    static Bool fRestacking = FALSE;    /* Avoid recusive calls to this function */
    DWORD dwCurrentProcessID = GetCurrentProcessId();
    DWORD dwWindowProcessID = 0;

#if CYGMULTIWINDOW_DEBUG || CYGWINDOWING_DEBUG
    winTrace("winReorderWindowsMultiWindow\n");
#endif

    if (fRestacking) {
        /* It is a recusive call so immediately exit */
#if CYGWINDOWING_DEBUG
        ErrorF("winReorderWindowsMultiWindow - "
               "exit because fRestacking == TRUE\n");
#endif
        return;
    }
    fRestacking = TRUE;

    /* Loop through top level Window windows, descending in Z order */
    for (hwnd = GetTopWindow(NULL);
         hwnd; hwnd = GetNextWindow(hwnd, GW_HWNDNEXT)) {
        /* Don't take care of other Cygwin/X process's windows */
        GetWindowThreadProcessId(hwnd, &dwWindowProcessID);

        if (GetProp(hwnd, WIN_WINDOW_PROP)
            && (dwWindowProcessID == dwCurrentProcessID)
            && !IsIconic(hwnd)) {       /* ignore minimized windows */
            pWinSib = pWin;
            pWin = GetProp(hwnd, WIN_WINDOW_PROP);

            if (!pWinSib) {     /* 1st window - raise to the top */
                vlist[0] = Above;

                winConfigureWindow(pWin, CWStackMode, vlist, wClient(pWin));
            }
            else {              /* 2nd or deeper windows - just below the previous one */
                vlist[0] = winGetWindowID(pWinSib);
                vlist[1] = Below;

                winConfigureWindow(pWin, CWSibling | CWStackMode,
                                vlist, wClient(pWin));
            }
        }
    }

    fRestacking = FALSE;
}

/*
 * CopyWindow - See Porting Layer Definition - p. 39
 */
void
winCopyWindowMultiWindow(WindowPtr pWin, DDXPointRec oldpt, RegionPtr oldRegion)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

    winDebug("CopyWindowMultiWindow\n");

    WIN_UNWRAP(CopyWindow);
    (*pScreen->CopyWindow) (pWin, oldpt, oldRegion);
    WIN_WRAP(CopyWindow, winCopyWindowMultiWindow);
}

/*
 * MoveWindow - See Porting Layer Definition - p. 42
 */
void
winMoveWindowMultiWindow(WindowPtr pWin, int x, int y,
                         WindowPtr pSib, VTKind kind)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

    winDebug("MoveWindowMultiWindow to (%d, %d)\n", x, y);

    WIN_UNWRAP(MoveWindow);
    (*pScreen->MoveWindow) (pWin, x, y, pSib, kind);
    WIN_WRAP(MoveWindow, winMoveWindowMultiWindow);
}

/*
 * ResizeWindow - See Porting Layer Definition - p. 42
 */
void
winResizeWindowMultiWindow(WindowPtr pWin, int x, int y, unsigned int w,
                           unsigned int h, WindowPtr pSib)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;

    winScreenPriv(pScreen);

    winDebug("ResizeWindowMultiWindow to (%d, %d) - %dx%d\n", x, y, w, h);

    WIN_UNWRAP(ResizeWindow);
    (*pScreen->ResizeWindow) (pWin, x, y, w, h, pSib);
    WIN_WRAP(ResizeWindow, winResizeWindowMultiWindow);
}

/*
 * winAdjustXWindow
 *
 * Move and resize X window with respect to corresponding Windows window.
 * This is called from WM_MOVE/WM_SIZE handlers when the user performs
 * any windowing operation (move, resize, minimize, maximize, restore).
 *
 * The functionality is the inverse of winPositionWindowMultiWindow, which
 * adjusts Windows window with respect to X window.
 */
int
winAdjustXWindow(WindowPtr pWin, HWND hwnd)
{
    RECT rcDraw;                /* Rect made from pWin->drawable to be adjusted */
    RECT rcWin;                 /* The source: WindowRect from hwnd */
    DrawablePtr pDraw;
    XID vlist[4];
    LONG dX, dY, dW, dH, x, y;
    DWORD dwStyle, dwExStyle;

#define WIDTH(rc) (rc.right - rc.left)
#define HEIGHT(rc) (rc.bottom - rc.top)

    if( !pWin->realized) //  IZI  Window is being destroyed?
        return 0;

    winDebug("winAdjustXWindow\n");

    if (IsIconic(hwnd)) {
      winDebug("\timmediately return because the window is iconized\n");
        /*
         * If the Windows window is minimized, its WindowRect has
         * meaningless values so we don't adjust X window to it.
         */
        vlist[0] = 0;
        vlist[1] = 0;
        return winConfigureWindow(pWin, CWX | CWY, vlist, wClient(pWin));
    }

    pDraw = &pWin->drawable;

    /* Calculate the window rect from the drawable */
    x = pDraw->x + GetSystemMetrics(SM_XVIRTUALSCREEN);
    y = pDraw->y + GetSystemMetrics(SM_YVIRTUALSCREEN);
    SetRect(&rcDraw, x, y, x + pDraw->width, y + pDraw->height);
    winDebug("\tDrawable extend {%d, %d, %d, %d}, {%d, %d}\n",
             (int)rcDraw.left, (int)rcDraw.top, (int)rcDraw.right, (int)rcDraw.bottom,
             (int)(rcDraw.right - rcDraw.left), (int)(rcDraw.bottom - rcDraw.top));
    dwExStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    dwStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
    winDebug("\tWindowStyle: %08x %08x\n", (unsigned int)dwStyle, (unsigned int)dwExStyle);
    AdjustWindowRectEx(&rcDraw, dwStyle, FALSE, dwExStyle);

    /* The source of adjust */
    GetWindowRect(hwnd, &rcWin);
    winDebug("\tWindow extend {%d, %d, %d, %d}, {%d, %d}\n",
             (int)rcWin.left, (int)rcWin.top, (int)rcWin.right, (int)rcWin.bottom,
             (int)(rcWin.right - rcWin.left), (int)(rcWin.bottom - rcWin.top));
    winDebug("\tDraw extend {%d, %d, %d, %d}, {%d, %d}\n",
             (int)rcDraw.left, (int)rcDraw.top, (int)rcDraw.right, (int)rcDraw.bottom,
             (int)(rcDraw.right - rcDraw.left), (int)(rcDraw.bottom - rcDraw.top));

    if (EqualRect(&rcDraw, &rcWin)) {
        /* Bail if no adjust is needed */
    winDebug("\treturn because already adjusted\n");
        return 0;
    }

    /* Calculate delta values */
    dX = rcWin.left - rcDraw.left;
    dY = rcWin.top - rcDraw.top;
    dW = WIDTH(rcWin) - WIDTH(rcDraw);
    dH = HEIGHT(rcWin) - HEIGHT(rcDraw);

    /*
     * Adjust.
     * We may only need to move (vlist[0] and [1]), or only resize
     * ([2] and [3]) but currently we set all the parameters and leave
     * the decision to winConfigureWindow.  The reason is code simplicity.
     */
    vlist[0] = pDraw->x + dX - wBorderWidth(pWin);
    vlist[1] = pDraw->y + dY - wBorderWidth(pWin);
    vlist[2] = pDraw->width + dW;
    vlist[3] = pDraw->height + dH;

    winDebug("\tConfigureWindow to (%u, %u) - %ux%u\n",
           (unsigned int)vlist[0], (unsigned int)vlist[1],
           (unsigned int)vlist[2], (unsigned int)vlist[3]);
    return winConfigureWindow(pWin, CWX | CWY | CWWidth | CWHeight,
                              vlist, wClient(pWin));

#undef WIDTH
#undef HEIGHT
}

/*
  Helper function for creating a DIB to back a pixmap
 */
static HBITMAP winCreateDIB(ScreenPtr pScreen, int width, int height, int bpp, void **ppvBits, BITMAPINFOHEADER **ppbmih)
{
    winScreenPriv(pScreen);
    BITMAPV4HEADER *pbmih = NULL;
    HBITMAP hBitmap = NULL;

    /* Allocate bitmap info header */
    pbmih = malloc(sizeof(BITMAPV4HEADER) + 256 * sizeof(RGBQUAD));
    if (pbmih == NULL) {
        ErrorF("winCreateDIB: malloc() failed\n");
        return NULL;
    }
    memset(pbmih, 0, sizeof(BITMAPV4HEADER) + 256 * sizeof(RGBQUAD));

    /* Describe bitmap to be created */
    pbmih->bV4Size = sizeof(BITMAPV4HEADER);
    pbmih->bV4Width = width;
    pbmih->bV4Height = -height;  /* top-down bitmap */
    pbmih->bV4Planes = 1;
    pbmih->bV4BitCount = bpp;
    if (bpp == 1) {
        RGBQUAD *bmiColors = (RGBQUAD *)((char *)pbmih + sizeof(BITMAPV4HEADER));
        pbmih->bV4V4Compression = BI_RGB;
        bmiColors[1].rgbBlue = 255;
        bmiColors[1].rgbGreen = 255;
        bmiColors[1].rgbRed = 255;
    }
    else if (bpp == 8) {
        pbmih->bV4V4Compression = BI_RGB;
        pbmih->bV4ClrUsed = 0;
    }
    else if (bpp == 16) {
        pbmih->bV4V4Compression = BI_RGB;
        pbmih->bV4ClrUsed = 0;
    }
    else if (bpp == 32) {
        pbmih->bV4V4Compression = BI_BITFIELDS;
        pbmih->bV4RedMask = pScreenPriv->dwRedMask;
        pbmih->bV4GreenMask = pScreenPriv->dwGreenMask;
        pbmih->bV4BlueMask = pScreenPriv->dwBlueMask;
        pbmih->bV4AlphaMask = 0;
    }
    else {
        ErrorF("winCreateDIB: %d bpp unhandled\n", bpp);
    }

    /* Create a DIB with a bit pointer */
    hBitmap = CreateDIBSection(NULL,
                               (BITMAPINFO *) pbmih,
                               DIB_RGB_COLORS, ppvBits, NULL, 0);
    if (hBitmap == NULL) {
        ErrorF("winCreateDIB: CreateDIBSection() failed\n");
        return NULL;
    }

    /* Store the address of the BMIH in the ppbmih parameter */
    *ppbmih = (BITMAPINFOHEADER *)pbmih;

    winDebug("winCreateDIB: HBITMAP %p pBMIH %p pBits %p\n", hBitmap, pbmih, *ppvBits);

    return hBitmap;
}


/*
 * CreatePixmap - See Porting Layer Definition
 */
PixmapPtr
winCreatePixmapMultiwindow(ScreenPtr pScreen, int width, int height, int depth,
                           unsigned usage_hint)
{
    winPrivPixmapPtr pPixmapPriv = NULL;
    PixmapPtr pPixmap = NULL;
    int bpp, paddedwidth;

    /* allocate Pixmap header and privates */
    pPixmap = AllocatePixmap(pScreen, 0);
    if (!pPixmap)
        return NullPixmap;

    bpp = BitsPerPixel(depth);
    /*
      DIBs have 4-byte aligned rows

      paddedwidth is the width in bytes, padded to align

      i.e. round up the number of bits used by a row so it is a multiple of 32,
      then convert to bytes
    */
    paddedwidth = (((bpp * width) + 31) & ~31)/8;

    /* setup Pixmap header */
    pPixmap->drawable.type = DRAWABLE_PIXMAP;
    pPixmap->drawable.class = 0;
    pPixmap->drawable.pScreen = pScreen;
    pPixmap->drawable.depth = depth;
    pPixmap->drawable.bitsPerPixel = bpp;
    pPixmap->drawable.id = 0;
    pPixmap->drawable.serialNumber = NEXT_SERIAL_NUMBER;
    pPixmap->drawable.x = 0;
    pPixmap->drawable.y = 0;
    pPixmap->drawable.width = width;
    pPixmap->drawable.height = height;
    pPixmap->devKind = paddedwidth;
    pPixmap->refcnt = 1;
    pPixmap->devPrivate.ptr = NULL; // later set to pbBits
    pPixmap->master_pixmap = NULL;
#ifdef COMPOSITE
    pPixmap->screen_x = 0;
    pPixmap->screen_y = 0;
#endif
    pPixmap->usage_hint = usage_hint;

    /* Check for zero width or height pixmaps */
    if (width == 0 || height == 0) {
        /* DIBs with a dimension of 0 aren't permitted, so don't try to allocate
           a DIB, just set fields and return */
        return pPixmap;
    }

    /* Initialize pixmap privates */
    pPixmapPriv = winGetPixmapPriv(pPixmap);
    pPixmapPriv->hBitmap = NULL;
    pPixmapPriv->pbBits = NULL;
    pPixmapPriv->pbmih = NULL;

    /* Create a DIB for the pixmap */
    pPixmapPriv->hBitmap = winCreateDIB(pScreen, width, height, bpp, &pPixmapPriv->pbBits, &pPixmapPriv->pbmih);
    pPixmapPriv->owned = TRUE;

    winDebug("winCreatePixmap: pPixmap %p HBITMAP %p pBMIH %p pBits %p\n", pPixmap, pPixmapPriv->hBitmap, pPixmapPriv->pbmih, pPixmapPriv->pbBits);
    /* XXX: so why do we need this in privates ??? */
    pPixmap->devPrivate.ptr = pPixmapPriv->pbBits;

    return pPixmap;
}

/*
 * DestroyPixmap - See Porting Layer Definition
 */
Bool
winDestroyPixmapMultiwindow(PixmapPtr pPixmap)
{
    winPrivPixmapPtr pPixmapPriv = NULL;

    /* Bail early if there is not a pixmap to destroy */
    if (pPixmap == NULL) {
        return TRUE;
    }

    /* Decrement reference count, return if nonzero */
    --pPixmap->refcnt;
    if (pPixmap->refcnt != 0)
        return TRUE;

    winDebug("winDestroyPixmap: pPixmap %p\n", pPixmap);

    /* Get a handle to the pixmap privates */
    pPixmapPriv = winGetPixmapPriv(pPixmap);

    /* Nothing to do if we don't own the DIB */
    if (!pPixmapPriv->owned)
        return TRUE;

    /* Free GDI bitmap */
    if (pPixmapPriv->hBitmap)
        DeleteObject(pPixmapPriv->hBitmap);

    /* Free the bitmap info header memory */
    free(pPixmapPriv->pbmih);
    pPixmapPriv->pbmih = NULL;

    /* Free the pixmap memory */
    free(pPixmap);
    pPixmap = NULL;

    return TRUE;
}

/*
 * ModifyPixmapHeader - See Porting Layer Definition
 */
Bool
winModifyPixmapHeaderMultiwindow(PixmapPtr pPixmap,
                                 int width,
                                 int height,
                                 int depth,
                                 int bitsPerPixel, int devKind, void *pPixData)
{
    int i;
    winPrivPixmapPtr pPixmapPriv = winGetPixmapPriv(pPixmap);
    Bool fResult;

    /* reinitialize everything */
    pPixmap->drawable.depth = depth;
    pPixmap->drawable.bitsPerPixel = bitsPerPixel;
    pPixmap->drawable.id = 0;
    pPixmap->drawable.x = 0;
    pPixmap->drawable.y = 0;
    pPixmap->drawable.width = width;
    pPixmap->drawable.height = height;
    pPixmap->devKind = devKind;
    pPixmap->refcnt = 1;
    pPixmap->devPrivate.ptr = pPixData;
    pPixmap->drawable.serialNumber = NEXT_SERIAL_NUMBER;

    /*
      This can be used for some out-of-order initialization on the screen
      pixmap, which is the only case we can properly support.
    */

    /* Look for which screen this pixmap corresponds to */
    for (i = 0; i < screenInfo.numScreens; i++) {
        ScreenPtr pScreen = screenInfo.screens[i];
        winScreenPriv(pScreen);
        winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;

        if (pScreenInfo->pfb == pPixData)
            {
                /* and initialize pixmap privates from screen privates */
                pPixmapPriv->hBitmap = pScreenPriv->hbmpShadow;
                pPixmapPriv->pbBits = pScreenInfo->pfb;
                pPixmapPriv->pbmih = pScreenPriv->pbmih;

                /* mark these not to get released by DestroyPixmap */
                pPixmapPriv->owned = FALSE;

                return TRUE;
            }
    }

    /* Otherwise, since creating a DIBSection from arbitrary memory is not
     * possible, fallback to normal.  If needed, we can create a DIBSection with
     * a copy of the bits later (see comment about a potential slow-path in
     * winBltExposedWindowRegionShadowGDI()). */
    pPixmapPriv->hBitmap = 0;
    pPixmapPriv->pbBits = 0;
    pPixmapPriv->pbmih = 0;
    pPixmapPriv->owned = FALSE;

    winDebug("winModifyPixmapHeaderMultiwindow: falling back\n");

    {
        ScreenPtr pScreen = pPixmap->drawable.pScreen;
        winScreenPriv(pScreen);
        WIN_UNWRAP(ModifyPixmapHeader);
        fResult = (*pScreen->ModifyPixmapHeader) (pPixmap, width, height, depth, bitsPerPixel, devKind, pPixData);
        WIN_WRAP(ModifyPixmapHeader, winModifyPixmapHeaderMultiwindow);
    }

    return fResult;
}
