#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif

#include "win.h"
#include "winglobals.h"
#include "winmsg.h"

HWND g_hMainThreadMsgWnd;

#define WINDOW_CLASS_THREAD_MSG "vcxsrv/x thread msg"

#define WM_CREATE_WINDOW  (WM_USER)
#define WM_DESTROY_WINDOW (WM_USER+1)

#define WINDOW_CLASS_MAINTHREAD_MSG "vcxsrv/x main thread msg"

struct windows_createparams
{
  DWORD dwExStyle;
  LPCSTR lpClassName;
  LPCSTR lpWindowName;
  DWORD dwStyle;
  int X;
  int Y;
  int nWidth;
  int nHeight;
  HWND hWndParent;
  HMENU hMenu;
  HINSTANCE hInstance;
  LPVOID lpParam;
};

static pthread_t ptCreateWindowsThread;

static LRESULT CALLBACK winMainThreadMsgWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
    case WM_ADJUSTXWINDOW:
      winAdjustXWindow((WindowPtr)wParam, (HWND)lParam);
      return 0;
    case WM_REORDERWINDOWS:
      winReorderWindowsMultiWindow();
      return 0;
    case WM_POSITIONWINDOW:
      {
        LONG x,y;
        WindowPtr pWin=(WindowPtr)wParam;
        DrawablePtr pDraw = &pWin->drawable;
        x =  pDraw->x - wBorderWidth(pWin);
        y = pDraw->y - wBorderWidth(pWin);
        winPositionWindowMultiWindow(pWin, x, y);
      }
      return 0;
    case WM_CONFIGUREWINDOW:
      {
        XID vlist[1] = { 0 };
        WindowPtr pWin=(WindowPtr)wParam;
        Mask mask=(Mask)lParam;
        ConfigureWindow(pWin, mask, vlist, serverClient);
      }
      return 0;
  }

  return DefWindowProc(hwnd, message, wParam, lParam);
}

static HWND winMainThreadCreateMsgWindow(void)
{
  HWND hwndMsg;

  // register window class
  WNDCLASSEX wcx;
  memset(&wcx, 0, sizeof(wcx));

  wcx.cbSize = sizeof(wcx);
  wcx.style = CS_HREDRAW | CS_VREDRAW;
  wcx.lpfnWndProc = winMainThreadMsgWindowProc;
  wcx.hInstance = g_hInstance;
  wcx.lpszClassName = WINDOW_CLASS_MAINTHREAD_MSG;
  RegisterClassEx(&wcx);

  // Create the msg window.
  hwndMsg = CreateWindowEx(0, // no extended styles
      WINDOW_CLASS_MAINTHREAD_MSG,        // class name
      "XWin Main Thread Msg Window", // window name
      WS_OVERLAPPEDWINDOW,       // overlapped window
      CW_USEDEFAULT,     // default horizontal position
      CW_USEDEFAULT,     // default vertical position
      CW_USEDEFAULT,     // default width
      CW_USEDEFAULT,     // default height
      (HWND) NULL,       // no parent or owner window
      (HMENU) NULL,      // class menu used
      g_hInstance,     // instance handle
      NULL);     // no window creation data

  if (!hwndMsg) {
    ErrorF("winMainThreadCreateMsgWindow - Create msg window failed\n");
    return NULL;
  }

  winDebug("winMainThreadCreateMsgWindow - Created msg window hwnd 0x%p\n", hwndMsg);

  return hwndMsg;
}

/*
 * winThreadMsgWindowProc - Window procedure for msg window
 */

static LRESULT CALLBACK
winThreadMsgWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
    case WM_CREATE_WINDOW:
      {
        struct windows_createparams *pParams=(struct windows_createparams *)lParam;
        return (LRESULT)CreateWindowExA(
            pParams->dwExStyle,
            pParams->lpClassName,
            pParams->lpWindowName,
            pParams->dwStyle,
            pParams->X,
            pParams->Y,
            pParams->nWidth,
            pParams->nHeight,
            pParams->hWndParent,
            pParams->hMenu,
            pParams->hInstance,
            pParams->lpParam
            );
      }
    case WM_DESTROY_WINDOW:
      return DestroyWindow((HWND)wParam);
  }

  return DefWindowProc(hwnd, message, wParam, lParam);
}

static HWND winCreateThreadMsgWindow(void)
{
  HWND hwndMsg;

  // register window class
  WNDCLASSEX wcx;
  memset(&wcx, 0, sizeof(wcx));

  wcx.cbSize = sizeof(wcx);
  wcx.style = CS_HREDRAW | CS_VREDRAW;
  wcx.lpfnWndProc = winThreadMsgWindowProc;
  wcx.hInstance = g_hInstance;
  wcx.lpszClassName = WINDOW_CLASS_THREAD_MSG;
  RegisterClassEx(&wcx);

  // Create the msg window.
  hwndMsg = CreateWindowEx(0, // no extended styles
      WINDOW_CLASS_THREAD_MSG,        // class name
      "XWin Thread Msg Window", // window name
      WS_OVERLAPPEDWINDOW,       // overlapped window
      CW_USEDEFAULT,     // default horizontal position
      CW_USEDEFAULT,     // default vertical position
      CW_USEDEFAULT,     // default width
      CW_USEDEFAULT,     // default height
      (HWND) NULL,       // no parent or owner window
      (HMENU) NULL,      // class menu used
      g_hInstance,     // instance handle
      NULL);     // no window creation data

  if (!hwndMsg) {
    ErrorF("winCreateThreadMsgWindow - Create msg window failed\n");
    return NULL;
  }

  winDebug("winCreateThreadMsgWindow - Created msg window hwnd 0x%p\n", hwndMsg);

  return hwndMsg;
}

static pthread_t ptCreateWindowsThread;

static HWND hWndMsg;

static DWORD dwCurrentThreadID;

static void *winCreateWindowThreadProc(void *arg)
{
  MSG msg;

  winDebug("winCreateWindowThreadProc - Hello\n");

  dwCurrentThreadID = GetCurrentThreadId();

  hWndMsg = winCreateThreadMsgWindow();

  /* Process one message from our queue */
  while (GetMessage(&msg, NULL, 0, 0))
  {
    if ((g_hDlgDepthChange == 0 || !IsDialogMessage(g_hDlgDepthChange, &msg))
        && (g_hDlgExit == 0 || !IsDialogMessage(g_hDlgExit, &msg))
        && (g_hDlgAbout == 0 || !IsDialogMessage(g_hDlgAbout, &msg)))
    {
      DispatchMessage(&msg);
    }
  }
  winDebug("winCreateWindowThreadProc - Exit\n");

  return NULL;
}

int myCreateWindowsThead()
{
  g_hMainThreadMsgWnd=winMainThreadCreateMsgWindow();

  /* Spawn a thread for the msg window  */
  if (pthread_create(&ptCreateWindowsThread, NULL, winCreateWindowThreadProc, NULL))
  {
    /* Bail if thread creation failed */
    ErrorF("winCreateMsgWindow - pthread_create failed.\n");
    exit(1);
  }
  while (!hWndMsg)
    Sleep(0);  // Wait until thread created

  return dwCurrentThreadID;
}

HWND myCreateWindowExA( DWORD dwExStyle,
                        LPCSTR lpClassName,
                        LPCSTR lpWindowName,
                        DWORD dwStyle,
                        int X,
                        int Y,
                        int nWidth,
                        int nHeight,
                        HWND hWndParent,
                        HMENU hMenu,
                        HINSTANCE hInstance,
                        LPVOID lpParam)
{
  struct windows_createparams params =
  {
    dwExStyle,
    lpClassName,
    lpWindowName,
    dwStyle,
    X,
    Y,
    nWidth,
    nHeight,
    hWndParent,
    hMenu,
    hInstance,
    lpParam
  };
  return (HWND)SendMessage(hWndMsg, WM_CREATE_WINDOW, (WPARAM)NULL, (LPARAM)&params);
}

BOOL myDestroyWindow(HWND hWnd)
{
  return (BOOL)SendMessage(hWndMsg, WM_DESTROY_WINDOW, (WPARAM)hWnd, (LPARAM)NULL);
}

