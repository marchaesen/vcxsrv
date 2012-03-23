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
 *              Earle F. Philhower III
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"
#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif
#include <shellapi.h>
#include "winprefs.h"

/*
 * References to external globals
 */

#ifdef XWIN_CLIPBOARD
extern Bool g_fClipboardStarted;
#endif
/*
 * Local function prototypes
 */

static wBOOL CALLBACK
winExitDlgProc(HWND hDialog, UINT message, WPARAM wParam, LPARAM lParam);

static wBOOL CALLBACK
winChangeDepthDlgProc(HWND hDialog, UINT message, WPARAM wParam, LPARAM lParam);

static wBOOL CALLBACK
winAboutDlgProc(HWND hDialog, UINT message, WPARAM wParam, LPARAM lParam);

static void
 winDrawURLWindow(LPARAM lParam);

static LRESULT CALLBACK
winURLWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void
 winOverrideURLButton(HWND hdlg, int id);

static void
 winUnoverrideURLButton(HWND hdlg, int id);

/*
 * Owner-draw a button as a URL
 */

static void
winDrawURLWindow(LPARAM lParam)
{
    DRAWITEMSTRUCT *draw;
    char str[256];
    RECT rect;
    HFONT font;
    COLORREF crText;

    draw = (DRAWITEMSTRUCT *) lParam;
    GetWindowText(draw->hwndItem, str, sizeof(str));
    str[255] = 0;
    GetClientRect(draw->hwndItem, &rect);

    /* Color the button depending upon its state */
    if (draw->itemState & ODS_SELECTED)
        crText = RGB(128 + 64, 0, 0);
    else if (draw->itemState & ODS_FOCUS)
        crText = RGB(0, 128 + 64, 0);
    else
        crText = RGB(0, 0, 128 + 64);
    SetTextColor(draw->hDC, crText);

    /* Create font 8 high, standard dialog font */
    font = CreateFont(-8, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE,
                      0, 0, 0, 0, 0, "MS Sans Serif");
    if (!font) {
        ErrorF("winDrawURLWindow: Unable to create URL font, bailing.\n");
        return;
    }
    /* Draw it */
    SetBkMode(draw->hDC, OPAQUE);
    SelectObject(draw->hDC, font);
    DrawText(draw->hDC, str, strlen(str), &rect, DT_LEFT | DT_VCENTER);
    /* Delete the created font, replace it with stock font */
    DeleteObject(SelectObject(draw->hDC, GetStockObject(ANSI_VAR_FONT)));
}

/*
 * WndProc for overridden buttons
 */

static LRESULT CALLBACK
winURLWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC origCB = NULL;
    HCURSOR cursor;

    /* If it's a SetCursor message, tell it to the hand */
    if (msg == WM_SETCURSOR) {
        cursor = LoadCursor(NULL, IDC_HAND);
        if (cursor)
            SetCursor(cursor);
        return TRUE;
    }
    origCB = (WNDPROC) GetWindowLongPtr(hwnd, GWLP_USERDATA);
    /* Otherwise fall through to original WndProc */
    if (origCB)
        return CallWindowProc(origCB, hwnd, msg, wParam, lParam);
    else
        return FALSE;
}

/*
 * Register and unregister the custom WndProc
 */

static void
winOverrideURLButton(HWND hwnd, int id)
{
    WNDPROC origCB;

    origCB = (WNDPROC) SetWindowLongPtr(GetDlgItem(hwnd, id),
                                        GWLP_WNDPROC, (LONG_PTR) winURLWndProc);
    SetWindowLongPtr(GetDlgItem(hwnd, id), GWLP_USERDATA, (LONG_PTR) origCB);
}

static void
winUnoverrideURLButton(HWND hwnd, int id)
{
    WNDPROC origCB;

    origCB = (WNDPROC) SetWindowLongPtr(GetDlgItem(hwnd, id), GWLP_USERDATA, 0);
    if (origCB)
        SetWindowLongPtr(GetDlgItem(hwnd, id), GWLP_WNDPROC, (LONG_PTR) origCB);
}

/*
 * Center a dialog window in the desktop window
 * and set small and large icons to X icons.
 */

static void
winInitDialog(HWND hwndDlg)
{
    HWND hwndDesk;
    RECT rc, rcDlg, rcDesk;
    HICON hIcon, hIconSmall;

    hwndDesk = GetParent(hwndDlg);
    if (!hwndDesk || IsIconic(hwndDesk))
        hwndDesk = GetDesktopWindow();

    /* Remove minimize and maximize buttons */
    SetWindowLongPtr(hwndDlg, GWL_STYLE, GetWindowLongPtr(hwndDlg, GWL_STYLE)
                     & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX));

    /* Set Window not to show in the task bar */
    SetWindowLongPtr(hwndDlg, GWL_EXSTYLE,
                     GetWindowLongPtr(hwndDlg, GWL_EXSTYLE) & ~WS_EX_APPWINDOW);

    /* Center dialog window in the screen. Not done for multi-monitor systems, where
     * it is likely to end up split across the screens. In that case, it appears
     * near the Tray icon.
     */
    if (GetSystemMetrics(SM_CMONITORS) > 1) {
        /* Still need to refresh the frame change. */
        SetWindowPos(hwndDlg, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
    }
    else {
        GetWindowRect(hwndDesk, &rcDesk);
        GetWindowRect(hwndDlg, &rcDlg);
        CopyRect(&rc, &rcDesk);

        OffsetRect(&rcDlg, -rcDlg.left, -rcDlg.top);
        OffsetRect(&rc, -rc.left, -rc.top);
        OffsetRect(&rc, -rcDlg.right, -rcDlg.bottom);

        SetWindowPos(hwndDlg,
                     HWND_TOPMOST,
                     rcDesk.left + (rc.right / 2),
                     rcDesk.top + (rc.bottom / 2),
                     0, 0, SWP_NOSIZE | SWP_FRAMECHANGED);
    }

#ifdef XWIN_MULTIWINDOW
    if (g_hIconX)
        hIcon = g_hIconX;
    else
#endif
        hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_XWIN));

#ifdef XWIN_MULTIWINDOW
    if (g_hSmallIconX)
        hIconSmall = g_hSmallIconX;
    else
#endif
        hIconSmall = LoadImage(g_hInstance,
                               MAKEINTRESOURCE(IDI_XWIN), IMAGE_ICON,
                               GetSystemMetrics(SM_CXSMICON),
                               GetSystemMetrics(SM_CYSMICON), LR_SHARED);

    PostMessage(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM) hIcon);
    PostMessage(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM) hIconSmall);
}

/*
 * Display the Exit dialog box
 */

void
winDisplayExitDialog(winPrivScreenPtr pScreenPriv)
{
    int i;
    int liveClients = 0;

    /* Count up running clients (clients[0] is serverClient) */
    for (i = 1; i < currentMaxClients; i++)
        if (clients[i] != NullClient)
            liveClients++;
#if defined(XWIN_MULTIWINDOW)
    /* Count down server internal clients */
    if (pScreenPriv->pScreenInfo->fMultiWindow)
        liveClients -= 2;       /* multiwindow window manager & XMsgProc  */
#endif
#if defined(XWIN_CLIPBOARD)
    if (g_fClipboardStarted)
        liveClients--;          /* clipboard manager */
#endif

    /* A user reported that this sometimes drops below zero. just eye-candy. */
    if (liveClients < 0)
        liveClients = 0;

    /* Don't show the exit confirmation dialog if SilentExit & no clients,
       or ForceExit, is enabled */
    if ((pref.fSilentExit && liveClients <= 0) || pref.fForceExit) {
        if (g_hDlgExit != NULL) {
            DestroyWindow(g_hDlgExit);
            g_hDlgExit = NULL;
        }
        PostMessage(pScreenPriv->hwndScreen, WM_GIVEUP, 0, 0);
        return;
    }

    pScreenPriv->iConnectedClients = liveClients;

    /* Check if dialog already exists */
    if (g_hDlgExit != NULL) {
        /* Dialog box already exists, display it */
        ShowWindow(g_hDlgExit, SW_SHOWDEFAULT);

        /* User has lost the dialog.  Show them where it is. */
        SetForegroundWindow(g_hDlgExit);

        return;
    }

    /* Create dialog box */
    g_hDlgExit = CreateDialogParam(g_hInstance,
                                   "EXIT_DIALOG",
                                   pScreenPriv->hwndScreen,
                                   winExitDlgProc, (int) pScreenPriv);

    /* Show the dialog box */
    ShowWindow(g_hDlgExit, SW_SHOW);

    /* Needed to get keyboard controls (tab, arrows, enter, esc) to work */
    SetForegroundWindow(g_hDlgExit);

    /* Set focus to the Cancel button */
    PostMessage(g_hDlgExit, WM_NEXTDLGCTL,
                (WPARAM) GetDlgItem(g_hDlgExit, IDCANCEL), TRUE);
}

#define CONNECTED_CLIENTS_FORMAT	"There %s currently %d client%s connected."

/*
 * Exit dialog window procedure
 */

static wBOOL CALLBACK
winExitDlgProc(HWND hDialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    static winPrivScreenPtr s_pScreenPriv = NULL;

    /* Branch on message type */
    switch (message) {
    case WM_INITDIALOG:
    {
        char *pszConnectedClients;

        /* Store pointers to private structures for future use */
        s_pScreenPriv = (winPrivScreenPtr) lParam;

        winInitDialog(hDialog);

        /* Format the connected clients string */
        if (asprintf(&pszConnectedClients, CONNECTED_CLIENTS_FORMAT,
                     (s_pScreenPriv->iConnectedClients == 1) ? "is" : "are",
                     s_pScreenPriv->iConnectedClients,
                     (s_pScreenPriv->iConnectedClients == 1) ? "" : "s") == -1)
            return TRUE;

        /* Set the number of connected clients */
        SetWindowText(GetDlgItem(hDialog, IDC_CLIENTS_CONNECTED),
                      pszConnectedClients);
        free(pszConnectedClients);
    }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            /* Send message to call the GiveUp function */
            PostMessage(s_pScreenPriv->hwndScreen, WM_GIVEUP, 0, 0);
            DestroyWindow(g_hDlgExit);
            g_hDlgExit = NULL;

            /* Fix to make sure keyboard focus isn't trapped */
            PostMessage(s_pScreenPriv->hwndScreen, WM_NULL, 0, 0);
            return TRUE;

        case IDCANCEL:
            DestroyWindow(g_hDlgExit);
            g_hDlgExit = NULL;

            /* Fix to make sure keyboard focus isn't trapped */
            PostMessage(s_pScreenPriv->hwndScreen, WM_NULL, 0, 0);
            return TRUE;
        }
        break;

    case WM_MOUSEMOVE:
    case WM_NCMOUSEMOVE:
        /* Show the cursor if it is hidden */
        if (g_fSoftwareCursor && !g_fCursor) {
            g_fCursor = TRUE;
            ShowCursor(TRUE);
        }
        return TRUE;

    case WM_CLOSE:
        DestroyWindow(g_hDlgExit);
        g_hDlgExit = NULL;

        /* Fix to make sure keyboard focus isn't trapped */
        PostMessage(s_pScreenPriv->hwndScreen, WM_NULL, 0, 0);
        return TRUE;
    }

    return FALSE;
}

/*
 * Display the Depth Change dialog box
 */

void
winDisplayDepthChangeDialog(winPrivScreenPtr pScreenPriv)
{
    /* Check if dialog already exists */
    if (g_hDlgDepthChange != NULL) {
        /* Dialog box already exists, display it */
        ShowWindow(g_hDlgDepthChange, SW_SHOWDEFAULT);

        /* User has lost the dialog.  Show them where it is. */
        SetForegroundWindow(g_hDlgDepthChange);

        return;
    }

    /*
     * Display a notification to the user that the visual 
     * will not be displayed until the Windows display depth 
     * is restored to the original value.
     */
    g_hDlgDepthChange = CreateDialogParam(g_hInstance,
                                          "DEPTH_CHANGE_BOX",
                                          pScreenPriv->hwndScreen,
                                          winChangeDepthDlgProc,
                                          (int) pScreenPriv);
    /* Show the dialog box */
    ShowWindow(g_hDlgDepthChange, SW_SHOW);

    ErrorF("winDisplayDepthChangeDialog - DialogBox returned: %d\n",
           (int) g_hDlgDepthChange);
    ErrorF("winDisplayDepthChangeDialog - GetLastError: %d\n",
           (int) GetLastError());

    /* Minimize the display window */
    ShowWindow(pScreenPriv->hwndScreen, SW_MINIMIZE);
}

/*
 * Process messages for the dialog that is displayed for
 * disruptive screen depth changes. 
 */

static wBOOL CALLBACK
winChangeDepthDlgProc(HWND hwndDialog, UINT message,
                      WPARAM wParam, LPARAM lParam)
{
    static winPrivScreenPtr s_pScreenPriv = NULL;
    static winScreenInfo *s_pScreenInfo = NULL;
    static ScreenPtr s_pScreen = NULL;

#if CYGDEBUG
    winDebug("winChangeDepthDlgProc\n");
#endif

    /* Branch on message type */
    switch (message) {
    case WM_INITDIALOG:
#if CYGDEBUG
        winDebug("winChangeDepthDlgProc - WM_INITDIALOG\n");
#endif

        /* Store pointers to private structures for future use */
        s_pScreenPriv = (winPrivScreenPtr) lParam;
        s_pScreenInfo = s_pScreenPriv->pScreenInfo;
        s_pScreen = s_pScreenInfo->pScreen;

#if CYGDEBUG
        winDebug("winChangeDepthDlgProc - WM_INITDIALOG - s_pScreenPriv: %08x, "
                 "s_pScreenInfo: %08x, s_pScreen: %08x\n",
                 s_pScreenPriv, s_pScreenInfo, s_pScreen);
#endif

#if CYGDEBUG
        winDebug("winChangeDepthDlgProc - WM_INITDIALOG - orig bpp: %d, "
                 "current bpp: %d\n",
                 s_pScreenInfo->dwBPP,
                 GetDeviceCaps(s_pScreenPriv->hdcScreen, BITSPIXEL));
#endif

        winInitDialog(hwndDialog);

        return TRUE;

    case WM_DISPLAYCHANGE:
#if CYGDEBUG
        winDebug("winChangeDepthDlgProc - WM_DISPLAYCHANGE - orig bpp: %d, "
                 "new bpp: %d\n",
                 s_pScreenInfo->dwBPP,
                 GetDeviceCaps(s_pScreenPriv->hdcScreen, BITSPIXEL));
#endif

        /* Dismiss the dialog if the display returns to the original depth */
        if (GetDeviceCaps(s_pScreenPriv->hdcScreen, BITSPIXEL) ==
            s_pScreenInfo->dwBPP) {
            ErrorF("winChangeDelthDlgProc - wParam == s_pScreenInfo->dwBPP\n");

            /* Depth has been restored, dismiss dialog */
            DestroyWindow(g_hDlgDepthChange);
            g_hDlgDepthChange = NULL;

            /* Flag that we have a valid screen depth */
            s_pScreenPriv->fBadDepth = FALSE;
        }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
        case IDCANCEL:
            ErrorF("winChangeDepthDlgProc - WM_COMMAND - IDOK or IDCANCEL\n");

            /* 
             * User dismissed the dialog, hide it until the
             * display mode is restored.
             */
            ShowWindow(g_hDlgDepthChange, SW_HIDE);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        ErrorF("winChangeDepthDlgProc - WM_CLOSE\n");

        DestroyWindow(g_hDlgAbout);
        g_hDlgAbout = NULL;

        /* Fix to make sure keyboard focus isn't trapped */
        PostMessage(s_pScreenPriv->hwndScreen, WM_NULL, 0, 0);
        return TRUE;
    }

    return FALSE;
}

/*
 * Display the About dialog box
 */

void
winDisplayAboutDialog(winPrivScreenPtr pScreenPriv)
{
    /* Check if dialog already exists */
    if (g_hDlgAbout != NULL) {
        /* Dialog box already exists, display it */
        ShowWindow(g_hDlgAbout, SW_SHOWDEFAULT);

        /* User has lost the dialog.  Show them where it is. */
        SetForegroundWindow(g_hDlgAbout);

        return;
    }

    /*
     * Display the about box
     */
    g_hDlgAbout = CreateDialogParam(g_hInstance,
                                    "ABOUT_BOX",
                                    pScreenPriv->hwndScreen,
                                    winAboutDlgProc, (int) pScreenPriv);

    /* Show the dialog box */
    ShowWindow(g_hDlgAbout, SW_SHOW);

    /* Needed to get keyboard controls (tab, arrows, enter, esc) to work */
    SetForegroundWindow(g_hDlgAbout);

    /* Set focus to the OK button */
    PostMessage(g_hDlgAbout, WM_NEXTDLGCTL,
                (WPARAM) GetDlgItem(g_hDlgAbout, IDOK), TRUE);
}

/*
 * Process messages for the about dialog.
 */

static wBOOL CALLBACK
winAboutDlgProc(HWND hwndDialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    static winPrivScreenPtr s_pScreenPriv = NULL;
    static winScreenInfo *s_pScreenInfo = NULL;
    static ScreenPtr s_pScreen = NULL;

#if CYGDEBUG
    winDebug("winAboutDlgProc\n");
#endif

    /* Branch on message type */
    switch (message) {
    case WM_INITDIALOG:
#if CYGDEBUG
        winDebug("winAboutDlgProc - WM_INITDIALOG\n");
#endif

        /* Store pointers to private structures for future use */
        s_pScreenPriv = (winPrivScreenPtr) lParam;
        s_pScreenInfo = s_pScreenPriv->pScreenInfo;
        s_pScreen = s_pScreenInfo->pScreen;

        winInitDialog(hwndDialog);

        /* Override the URL buttons */
        winOverrideURLButton(hwndDialog, ID_ABOUT_CHANGELOG);
        winOverrideURLButton(hwndDialog, ID_ABOUT_WEBSITE);
        winOverrideURLButton(hwndDialog, ID_ABOUT_UG);
        winOverrideURLButton(hwndDialog, ID_ABOUT_FAQ);

        return TRUE;

    case WM_DRAWITEM:
        /* Draw the URL buttons as needed */
        winDrawURLWindow(lParam);
        return TRUE;

    case WM_MOUSEMOVE:
    case WM_NCMOUSEMOVE:
        /* Show the cursor if it is hidden */
        if (g_fSoftwareCursor && !g_fCursor) {
            g_fCursor = TRUE;
            ShowCursor(TRUE);
        }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
        case IDCANCEL:
            ErrorF("winAboutDlgProc - WM_COMMAND - IDOK or IDCANCEL\n");

            DestroyWindow(g_hDlgAbout);
            g_hDlgAbout = NULL;

            /* Fix to make sure keyboard focus isn't trapped */
            PostMessage(s_pScreenPriv->hwndScreen, WM_NULL, 0, 0);

            /* Restore window procedures for URL buttons */
            winUnoverrideURLButton(hwndDialog, ID_ABOUT_CHANGELOG);
            winUnoverrideURLButton(hwndDialog, ID_ABOUT_WEBSITE);
            winUnoverrideURLButton(hwndDialog, ID_ABOUT_UG);
            winUnoverrideURLButton(hwndDialog, ID_ABOUT_FAQ);

            return TRUE;

        case ID_ABOUT_CHANGELOG:
        {
            int iReturn;

#ifdef __CYGWIN__
            const char *pszCygPath = "/usr/X11R6/share/doc/"
                "xorg-x11-xwin/changelog.html";
            char pszWinPath[MAX_PATH + 1];

            /* Convert the POSIX path to a Win32 path */
            cygwin_conv_to_win32_path(pszCygPath, pszWinPath);
#else
            const char *pszWinPath = "http://x.cygwin.com/"
                "devel/server/changelog.html";
#endif

            iReturn = (int) ShellExecute(NULL,
                                         "open",
                                         pszWinPath, NULL, NULL, SW_MAXIMIZE);
            if (iReturn < 32) {
                ErrorF("winAboutDlgProc - WM_COMMAND - ID_ABOUT_CHANGELOG - "
                       "ShellExecute failed: %d\n", iReturn);
            }
        }
            return TRUE;

        case ID_ABOUT_WEBSITE:
        {
            const char *pszPath = __VENDORDWEBSUPPORT__;
            int iReturn;

            iReturn = (int) ShellExecute(NULL,
                                         "open",
                                         pszPath, NULL, NULL, SW_MAXIMIZE);
            if (iReturn < 32) {
                ErrorF("winAboutDlgProc - WM_COMMAND - ID_ABOUT_WEBSITE - "
                       "ShellExecute failed: %d\n", iReturn);
            }
        }
            return TRUE;

        case ID_ABOUT_UG:
        {
            const char *pszPath = "http://x.cygwin.com/docs/ug/";
            int iReturn;

            iReturn = (int) ShellExecute(NULL,
                                         "open",
                                         pszPath, NULL, NULL, SW_MAXIMIZE);
            if (iReturn < 32) {
                ErrorF("winAboutDlgProc - WM_COMMAND - ID_ABOUT_UG - "
                       "ShellExecute failed: %d\n", iReturn);
            }
        }
            return TRUE;

        case ID_ABOUT_FAQ:
        {
            const char *pszPath = "http://x.cygwin.com/docs/faq/";
            int iReturn;

            iReturn = (int) ShellExecute(NULL,
                                         "open",
                                         pszPath, NULL, NULL, SW_MAXIMIZE);
            if (iReturn < 32) {
                ErrorF("winAboutDlgProc - WM_COMMAND - ID_ABOUT_FAQ - "
                       "ShellExecute failed: %d\n", iReturn);
            }
        }
            return TRUE;
        }
        break;

    case WM_CLOSE:
        ErrorF("winAboutDlgProc - WM_CLOSE\n");

        DestroyWindow(g_hDlgAbout);
        g_hDlgAbout = NULL;

        /* Fix to make sure keyboard focus isn't trapped */
        PostMessage(s_pScreenPriv->hwndScreen, WM_NULL, 0, 0);

        /* Restore window procedures for URL buttons */
        winUnoverrideURLButton(hwndDialog, ID_ABOUT_CHANGELOG);
        winUnoverrideURLButton(hwndDialog, ID_ABOUT_WEBSITE);
        winUnoverrideURLButton(hwndDialog, ID_ABOUT_UG);
        winUnoverrideURLButton(hwndDialog, ID_ABOUT_FAQ);

        return TRUE;
    }

    return FALSE;
}
