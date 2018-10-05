#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"

void XdmcpHostSelected(int HostIndex);

static HWND g_hDlgHosts;
static char HostToConnect[128];

static void SelectHost(int Index)
{
  int i;
  char HostName[128];
  int HostIndex=SendDlgItemMessage(g_hDlgHosts, IDC_HOSTLIST, LB_GETITEMDATA, (WPARAM)Index, 0);

  HostToConnect[0]=0;
  SendDlgItemMessage(g_hDlgHosts, IDC_HOSTLIST, LB_GETTEXT, (WPARAM)Index, (LPARAM)HostToConnect);
  g_pszQueryHost=HostToConnect;

  gethostname(HostName,128);

  XdmcpHostSelected(HostIndex);

  for (i = 0; i < g_iNumScreens; ++i)
    {
      /* Change the window title to reflect the host we are connecting to */
      if (g_ScreenInfo[i].pScreen)
      {
        char szTitle[256];

        winScreenPriv(g_ScreenInfo[i].pScreen);
        winScreenInfo	*pScreenInfo = pScreenPriv->pScreenInfo;
        snprintf (szTitle, sizeof (szTitle), WINDOW_TITLE_XDMCP, HostToConnect, HostName, display, (int) pScreenInfo->dwScreen);
        SetWindowText(pScreenPriv->hwndScreen,szTitle);
      }
    }

  DestroyWindow (g_hDlgHosts);
  g_hDlgHosts = NULL;
}

/*
 * Process messages for the about dialog.
 */

static INT_PTR CALLBACK DisplayXdmcpHostsDlgProc (HWND hwndDialog, UINT message, WPARAM wParam, LPARAM lParam)
{
  /* Branch on message type */
  switch (message)
  {
    case WM_INITDIALOG:
    {
      HWND hwndDesk=GetForegroundWindow();
      RECT rc, rcDlg, rcDesk;
      
      GetWindowRect (hwndDesk, &rcDesk);
      GetWindowRect (hwndDialog, &rcDlg);
      CopyRect (&rc, &rcDesk);

      OffsetRect (&rcDlg, -rcDlg.left, -rcDlg.top);
      OffsetRect (&rc, -rc.left, -rc.top);
      OffsetRect (&rc, -rcDlg.right, -rcDlg.bottom);

      SetWindowPos (hwndDialog, HWND_TOPMOST, rcDesk.left + (rc.right / 2), rcDesk.top + (rc.bottom / 2), 0, 0, SWP_NOSIZE | SWP_FRAMECHANGED);
      return TRUE;
    }
    break;

    case WM_COMMAND:
      switch (LOWORD (wParam))
      {
        case IDOK:
        {
          int Index;
          winDebug ("DisplayXdmcpHostsDlgProc - WM_COMMAND - IDOK or IDCANCEL\n");

            /* Retreive the host to connect to */
          Index=SendDlgItemMessage(g_hDlgHosts, IDC_HOSTLIST, LB_GETCURSEL, 0, 0);
          if (Index==LB_ERR)
          {
            MessageBox(hwndDialog,"Please select a host to connect to!","Error",MB_OK);
          }
          else
          {
            SelectHost(Index);
          }

          return TRUE;
        }
        break;
        case IDCANCEL:
          winDebug ("DisplayXdmcpHostsDlgProc - WM_COMMAND - IDCANCEL\n");

          DestroyWindow (g_hDlgHosts);
          ddxGiveUp(EXIT_ERR_ABORT);
          TerminateProcess(GetCurrentProcess(),1);  /* Exit the application */

          return TRUE;
        break;
        case IDC_HOSTLIST:
          if (HIWORD(wParam)==LBN_DBLCLK)
          {
            int Index=SendDlgItemMessage(g_hDlgHosts, IDC_HOSTLIST, LB_GETCURSEL, 0, 0);
            SelectHost(Index);
            return TRUE;
          }
        break;
      }
      break;

    case WM_CLOSE:
      ErrorF ("DisplayXdmcpHostsDlgProc - WM_CLOSE\n");

      DestroyWindow (g_hDlgHosts);
      g_hDlgHosts = NULL;
      ddxGiveUp(EXIT_ERR_ABORT);
      TerminateProcess(GetCurrentProcess(),1);  /* Exit the application */

      return TRUE;
  }

  return FALSE;
}

int XdmcpHostAddName(const char *HostName, int HostIndex)
{
  int Index=SendDlgItemMessage(g_hDlgHosts, IDC_HOSTLIST, LB_FINDSTRINGEXACT, -1, (LPARAM)HostName);
  if (LB_ERR==Index)
  {
    Index=SendDlgItemMessage(g_hDlgHosts, IDC_HOSTLIST, LB_ADDSTRING, 0, (LPARAM)HostName); 
    SendDlgItemMessage(g_hDlgHosts, IDC_HOSTLIST, LB_SETITEMDATA, (WPARAM)Index, (LPARAM)HostIndex); 
    return -1;
  }
  else
    return SendDlgItemMessage(g_hDlgHosts, IDC_HOSTLIST, LB_GETITEMDATA, (WPARAM)Index, 0);
}
/*
 * Display the host selection dialog box
 */

void DisplayXdmcpHostsDialog(void)
{
  /* Check if dialog already exists */
  if (g_hDlgHosts != NULL)
  {
    /* Dialog box already exists, display it */
    ShowWindow (g_hDlgHosts, SW_SHOW);

    /* User has lost the dialog.  Show them where it is. */
    SetForegroundWindow (g_hDlgHosts);

    return;
  }

  /* Show an arrow cursor as long as the selection dialog is shown */
  {
    int i;
    for (i = 0; i < screenInfo.numScreens; ++i)
    {
      winScreenPriv(screenInfo.screens[i]);
      pScreenPriv->cursor.handle=LoadCursor(NULL,IDC_ARROW);
    }
  }

  /*
   * Display the about box
   */
  g_hDlgHosts = CreateDialogParam (g_hInstance, "XDMCP_HOSTS", GetForegroundWindow(), DisplayXdmcpHostsDlgProc, 0);

  /* Show the dialog box */
  ShowWindow (g_hDlgHosts, SW_SHOW);

  /* Needed to get keyboard controls (tab, arrows, enter, esc) to work */
  SetForegroundWindow (g_hDlgHosts);

  /* Set focus to the OK button */
  PostMessage (g_hDlgAbout, WM_NEXTDLGCTL, (WPARAM)GetDlgItem (g_hDlgHosts, IDOK), TRUE);
}
