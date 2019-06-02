/*
 * Export window information for the Windows-OpenGL GLX implementation.
 *
 * Authors: Alexander Gottwald
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"
#include "winpriv.h"
#include "winwindow.h"

extern Bool			g_fXdmcpEnabled;

void
 winCreateWindowsWindow(WindowPtr pWin);

/**
 * Return size and handles of a window.
 * If pWin is NULL, then the information for the root window is requested.
 */
HWND
winGetWindowInfo(WindowPtr pWin)
{
    HWND hwnd = NULL;
    winDebug("%s:%d pWin %p XID 0x%x\n", __FUNCTION__, __LINE__, pWin, (unsigned int)pWin->drawable.id);

    /* a real window was requested */
    if (pWin != NULL) {
        /* Get the window and screen privates */
        ScreenPtr pScreen = pWin->drawable.pScreen;
        winPrivScreenPtr pWinScreen = winGetScreenPriv(pScreen);
        winScreenInfoPtr pScreenInfo = NULL;

        if (pWinScreen == NULL) {
            ErrorF("winGetWindowInfo: screen has no privates\n");
            return hwnd;
        }

        hwnd = pWinScreen->hwndScreen;

        pScreenInfo = pWinScreen->pScreenInfo;
        /* check for multiwindow mode */
        if (pScreenInfo->fMultiWindow) {
            winWindowPriv(pWin);

            if (pWinPriv == NULL) {
                ErrorF("winGetWindowInfo: window has no privates\n");
                return hwnd;
            }

            if (pWinPriv->hWnd == NULL) {
              if (pWin->parent && pWin->parent->parent)
              {
                int offsetx;
                int offsety;
                int ExtraClass=(pWin->realized)?WS_VISIBLE:0;
                HWND hWndParent;
                WindowPtr pParent=pWin->parent;
                while (pParent)
                {
                  winWindowPriv(pParent);
                  hWndParent=pWinPriv->hWnd;
                  if (hWndParent)
                    break;
                  pParent=pParent->parent;
                }
                if (!hWndParent)
                  hWndParent=hwnd;
                if (pParent)
                {
                  offsetx=pParent->drawable.x;
                  offsety=pParent->drawable.y;
                }
                else
                {
                  offsetx=0;
                  offsety=0;
                }
                pWinPriv->hWnd=CreateWindowExA(0,
                             WIN_GL_WINDOW_CLASS,
                             "",
                             WS_CHILD |WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_DISABLED | ExtraClass,
                             pWin->drawable.x-offsetx,
                             pWin->drawable.y-offsety,
                             pWin->drawable.width,
                             pWin->drawable.height,
                             hWndParent,
                             NULL,
                             g_hInstance,
                             pWin);
                winDebug("Window created %x %x %d %d %d %d\n",pWinPriv->hWnd,hWndParent,pWin->drawable.x-offsetx,pWin->drawable.y-offsety,pWin->drawable.width, pWin->drawable.height);
                pWinPriv->fWglUsed=TRUE;
              }
              else
              {
                winCreateWindowsWindow(pWin);
                winDebug("winGetWindowInfo: forcing window to exist...\n");
              }
            }
            if (pWinPriv->hWnd != NULL) {
                /* copy window handle */
                hwnd = pWinPriv->hWnd;
            }
        }
        else if (g_fXdmcpEnabled)
        {
            winWindowPriv(pWin);

            if (pWinPriv == NULL)
            {
                ErrorF("winGetWindowInfo: window has no privates\n");
                return hwnd;
            }
            if (pWinPriv->hWnd == NULL)
            {
              if (!((pWin->drawable.x==0) &&
                     (pWin->drawable.y==0) &&
                     (pWin->drawable.width==pScreen->width) &&
                     (pWin->drawable.height==pScreen->height)
                    )
                  )
              {
                int ExtraClass=(pWin->realized)?WS_VISIBLE:0;
                pWinPriv->hWnd=CreateWindowExA(0,
                             WIN_GL_WINDOW_CLASS,
                             "",
                             WS_CHILD |WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_DISABLED | ExtraClass,
                             pWin->drawable.x,
                             pWin->drawable.y,
                             pWin->drawable.width,
                             pWin->drawable.height,
                             pWinScreen->hwndScreen,
                             NULL,
                             g_hInstance,
                             pWin);
                pWinPriv->fWglUsed=TRUE;
                /* copy size and window handle */
                hwnd = pWinPriv->hWnd;
              }
              else
              {
                hwnd = pWinScreen->hwndScreen;
              }
            }
            else
            {
              hwnd = pWinPriv->hWnd;
            }
        }
    }
    else {
        ScreenPtr pScreen = g_ScreenInfo[0].pScreen;
        winPrivScreenPtr pWinScreen = winGetScreenPriv(pScreen);

        if (pWinScreen == NULL) {
            ErrorF("winGetWindowInfo: screen has no privates\n");
        }
        else
        {
            winDebug("winGetWindowInfo: returning root window\n");

            hwnd=pWinScreen->hwndScreen;
        }
    }

    return hwnd;
}

Bool
winCheckScreenAiglxIsSupported(ScreenPtr pScreen)
{
    winPrivScreenPtr pWinScreen = winGetScreenPriv(pScreen);
    winScreenInfoPtr pScreenInfo = pWinScreen->pScreenInfo;

    if (pScreenInfo->fMultiWindow)
        return TRUE;


  if (g_fXdmcpEnabled)
    return TRUE;
    return FALSE;
}

void
winSetScreenAiglxIsActive(ScreenPtr pScreen)
{
    winPrivScreenPtr pWinScreen = winGetScreenPriv(pScreen);
    pWinScreen->fNativeGlActive = TRUE;
}
