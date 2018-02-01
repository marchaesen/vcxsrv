/*
 * Copyright (c) 2005 Alexander Gottwald
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE ABOVE LISTED COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name(s) of the above copyright
 * holders shall not be used in advertising or otherwise to promote the sale,
 * use or other dealings in this Software without prior written authorization.
 */
#define SAVEPOSIX _POSIX_
#undef _POSIX_
#include <io.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#define _POSIX_ SAVEPOSIX

#include "window/util.h"
#include "window/wizard.h"
#include "resources/resources.h"
#include "config.h"
#include <prsht.h>
#include <commctrl.h>

#include <stdexcept>

#include <X11/Xlib.h>

#include <sstream>

static bool ContainPrintableChars(const char *Buf, unsigned Nbr)
{
  for (int i=0; i<Nbr; i++)
  {
    if (Buf[i]>0x20)
      return true;
  }
  return false;
}

HANDLE g_hChildStdoutRd;
int g_hStdOut;

/*
 * Process messages for the prompt dialog.
 */

static INT_PTR CALLBACK DisplayPromptDlgProc (HWND hwndDialog, UINT message, WPARAM wParam, LPARAM lParam)
{
  static UINT PasswordChar;
  static char *buf_p;
  switch (message)
  {
    case WM_INITDIALOG:
    {
      HWND hwndDesk=GetForegroundWindow();
      RECT rc, rcDlg, rcDesk;

      PasswordChar=SendDlgItemMessage(hwndDialog, IDC_INPUT, EM_GETPASSWORDCHAR, 0, 0);

      GetWindowRect (hwndDesk, &rcDesk);
      GetWindowRect (hwndDialog, &rcDlg);
      CopyRect (&rc, &rcDesk);

      OffsetRect (&rcDlg, -rcDlg.left, -rcDlg.top);
      OffsetRect (&rc, -rc.left, -rc.top);
      OffsetRect (&rc, -rcDlg.right, -rcDlg.bottom);

      SetWindowPos (hwndDialog,
      HWND_TOPMOST,
      rcDesk.left + (rc.right / 2),
      rcDesk.top + (rc.bottom / 2),
      0, 0,
      SWP_NOSIZE | SWP_FRAMECHANGED);

      buf_p=(char*)lParam;
      SendDlgItemMessage(hwndDialog, IDC_PROMPT_DESC, WM_SETTEXT, 0, (LPARAM)buf_p);
      SendDlgItemMessage(hwndDialog, IDC_PROMPT_DESC, EM_LINESCROLL, 0, 1000);
      SendDlgItemMessage(hwndDialog, IDC_PROMPT_DESC, EM_SETSEL, -1, -1);
      SetTimer(hwndDialog,1,20,NULL);
      return TRUE;
    }
    break;

    case WM_TIMER:
    {
      DWORD nbrAvail=0;
      PeekNamedPipe(g_hChildStdoutRd, NULL, NULL, NULL, &nbrAvail, NULL);
      if (nbrAvail)
      {
        int curLen=strlen(buf_p);
	buf_p=(char*)realloc(buf_p, curLen+nbrAvail+1);
        size_t Nbr = _read(g_hStdOut, buf_p+curLen, nbrAvail);
        buf_p[curLen+nbrAvail]=0;
        SendDlgItemMessage(hwndDialog, IDC_PROMPT_DESC, WM_SETTEXT, 0, (LPARAM)buf_p);
        SendDlgItemMessage(hwndDialog, IDC_PROMPT_DESC, EM_LINESCROLL, 0, 1000);
        SendDlgItemMessage(hwndDialog, IDC_PROMPT_DESC, EM_SETSEL, -1, -1);
      }
      break;
    }

    case WM_COMMAND:
      switch (LOWORD (wParam))
      {
        case IDOK:
	  buf_p=(char*)realloc(buf_p, 128);
          SendDlgItemMessage(hwndDialog, IDC_INPUT, WM_GETTEXT, 128, (LPARAM)buf_p);
          EndDialog(hwndDialog, (LPARAM)buf_p);
          return TRUE;
        case IDCANCEL:
          EndDialog(hwndDialog, NULL);
          return TRUE;
        case IDC_PASSWORD:
        {
          HWND hDlg=GetDlgItem(hwndDialog, IDC_INPUT);
          if (HIWORD(wParam)==BN_CLICKED)
          {
            if (BST_CHECKED==SendDlgItemMessage(hwndDialog, IDC_PASSWORD, BM_GETCHECK, 0, 0))
              SendMessage(hDlg, EM_SETPASSWORDCHAR, 0, 0);
            else
              SendMessage(hDlg, EM_SETPASSWORDCHAR, (WPARAM)PasswordChar, 0);
          }
          InvalidateRect(hDlg, NULL, TRUE);
        }
	return TRUE;
	case IDC_PROMPT_DESC:
	if (HIWORD(wParam)==EN_SETFOCUS)
          SendDlgItemMessage(hwndDialog, IDC_PROMPT_DESC, EM_SETSEL, -1, -1);
	
        return TRUE;
      }
      break;

    case WM_CLOSE:
      EndDialog (hwndDialog, NULL);
      return TRUE;
  }

  return FALSE;
}

static bool CheckOutput(HANDLE hChildStdoutRd, int hStdOut, int hStdIn)
{
  g_hChildStdoutRd = hChildStdoutRd;
  g_hStdOut = hStdOut;
  DWORD NbrAvail=0;
  PeekNamedPipe(hChildStdoutRd, NULL, NULL, NULL, &NbrAvail, NULL);
  if (NbrAvail)
  {
    char *buf_p=(char*)malloc(NbrAvail+1);
    size_t Nbr = _read(hStdOut, buf_p, NbrAvail);
    if (ContainPrintableChars(buf_p,Nbr))
    {
      buf_p[Nbr]=0;
      INT_PTR Ret = DialogBoxParam (GetModuleHandle(NULL), "IDD_PROMPT", NULL, DisplayPromptDlgProc, (LPARAM)buf_p);

      if (Ret)
      {
        char *Data=(char*)Ret;
        // Write it to the client
        _write(hStdIn, Data, strlen(Data));
        _write(hStdIn, "\x0a", 1);
        //_write(hStdIn, "\x0d\x0a", 2);
      }

      return true;
    }

  }
  return false;
}


/// @brief Send WM_ENDSESSION to all program windows.
/// This will shutdown the started xserver
BOOL CALLBACK KillWindowsProc(HWND hwnd, LPARAM lParam)
{
    SendMessage(hwnd, WM_ENDSESSION, 0, 0);
    return TRUE;
}

/// @brief Actual wizard implementation.
/// This is based on generic CWizard but handles the special dialogs
class CMyWizard : public CWizard
{
    public:
    private:
        CConfig config; /// Storage for config options.
    public:
        /// @brief Constructor.
        /// Set wizard pages.
        CMyWizard() : CWizard()
        {
            AddPage(IDD_DISPLAY, IDS_DISPLAY_TITLE, IDS_DISPLAY_SUBTITLE);
            AddPage(IDD_CLIENTS, IDS_CLIENTS_TITLE, IDS_CLIENTS_SUBTITLE);
            AddPage(IDD_PROGRAM, IDS_PROGRAM_TITLE, IDS_PROGRAM_SUBTITLE);
            AddPage(IDD_XDMCP, IDS_XDMCP_TITLE, IDS_XDMCP_SUBTITLE);
            //AddPage(IDD_FONTPATH, IDS_FONTPATH_TITLE, IDS_FONTPATH_SUBTITLE);
            AddPage(IDD_EXTRA, IDS_EXTRA_TITLE, IDS_EXTRA_SUBTITLE);
            AddPage(IDD_FINISH, IDS_FINISH_TITLE, IDS_FINISH_SUBTITLE);
        }

        virtual void LoadConfig(const char *filename)
        {
            try {
                config.Load(filename);
            } catch (std::runtime_error &e)
            {
                char Message[255];
                sprintf(Message,"Failure: %s\n", e.what());
                MessageBox(NULL,Message,"Exception",MB_OK);
            }
        }

        /// @brief Handle the PSN_WIZNEXT message.
        /// @param hwndDlg Handle to active page dialog.
        /// @param index Index of current page.
        /// @return TRUE if the message was handled. FALSE otherwise.
        virtual BOOL WizardNext(HWND hwndDlg, unsigned index)
        {
#ifdef _DEBUG
            printf("%s %d\n", __FUNCTION__, index);
#endif
            switch (PageID(index))
            {
                case IDD_DISPLAY:
                    // Check for select window mode
                    if (IsDlgButtonChecked(hwndDlg, IDC_MULTIWINDOW))
                        config.window = CConfig::MultiWindow;
                    else if (IsDlgButtonChecked(hwndDlg, IDC_FULLSCREEN))
                        config.window = CConfig::Fullscreen;
                    else if (IsDlgButtonChecked(hwndDlg, IDC_WINDOWED))
                        config.window = CConfig::Windowed;
                    else if (IsDlgButtonChecked(hwndDlg, IDC_NODECORATION))
                        config.window = CConfig::Nodecoration;
                    else
                    {
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, -1);
                        return TRUE;
                    }
                    // Get selected display number
                    {
                        char buffer[512];
                        GetDlgItemText(hwndDlg, IDC_DISPLAY, buffer, 512);
                        buffer[511] = 0;
                        config.display = buffer;
                    }
                    // Check for valid input
                    if (config.display.empty())
                    {
                        MessageBox(hwndDlg,"Please fill in a display number.","Error",MB_OK);
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, -1);
                    }
                    else
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_CLIENTS);
                    return TRUE;
                case IDD_CLIENTS:
                    // Check for select client startup method
                    if (IsDlgButtonChecked(hwndDlg, IDC_CLIENT))
                    {
                        config.client = CConfig::StartProgram;
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_PROGRAM);
                    } else if (IsDlgButtonChecked(hwndDlg, IDC_XDMCP))
                    {
                        config.client = CConfig::XDMCP;
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_XDMCP);
                    } else if (IsDlgButtonChecked(hwndDlg, IDC_CLIENT_NONE))
                    {
                        config.client = CConfig::NoClient;
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_EXTRA);
                    } else
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, -1);
                    return TRUE;
                case IDD_PROGRAM:
                    // Check wether local or remote client should be started
                    if (IsDlgButtonChecked(hwndDlg, IDC_CLIENT_LOCAL))
                        config.local = true;
                    else if (IsDlgButtonChecked(hwndDlg, IDC_CLIENT_REMOTE))
                        config.local = false;
                    else
                    {
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, -1);
                        return TRUE;
                    }
                    // Read program, user and host name
                    {
                        char buffer[512];
                        GetDlgItemText(hwndDlg, IDC_CLIENT_USER, buffer, 512);
                        buffer[511] = 0;
                        config.user = buffer;
                        GetDlgItemText(hwndDlg, IDC_CLIENT_HOST, buffer, 512);
                        buffer[511] = 0;
                        config.host = buffer;
                        GetDlgItemText(hwndDlg, IDC_CLIENT_PROGRAM, buffer, 512);
                        buffer[511] = 0;
                        config.localprogram = buffer;
                        GetDlgItemText(hwndDlg, IDC_CLIENT_REMOTEPROGRAM, buffer, 512);
                        buffer[511] = 0;
                        config.remoteprogram = buffer;
                        GetDlgItemText(hwndDlg, IDC_CLIENT_PASSWORD, buffer, 512);
                        buffer[511] = 0;
                        config.remotepassword = buffer;
                        GetDlgItemText(hwndDlg, IDC_CLIENT_PRIVATEKEY, buffer, 512);
                        buffer[511] = 0;
                        config.privatekey = buffer;
                    }
                    // Check for valid input
                    if (!config.local && (config.host.empty() || config.localprogram.empty() || config.remoteprogram.empty()))
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, -1);
                    else
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_EXTRA);
                    return TRUE;
                case IDD_XDMCP:
                    // Check for broadcast
                    if (IsDlgButtonChecked(hwndDlg, IDC_XDMCP_BROADCAST))
                        config.broadcast = true;
                    else if (IsDlgButtonChecked(hwndDlg, IDC_XDMCP_QUERY))
                        config.broadcast = false;
                    else
                    {
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, -1);
                        return TRUE;
                    }
                    // Check for indirect mode
                    if (IsDlgButtonChecked(hwndDlg, IDC_XDMCP_INDIRECT))
                        config.indirect = true;
                    else
                        config.indirect = false;
                    // Read hostname
                    {
                        char buffer[512];
                        GetDlgItemText(hwndDlg, IDC_XDMCP_HOST, buffer, 512);
                        buffer[511] = 0;
                        config.xdmcp_host = buffer;
                    }
                    // Check for valid input
                    if (!config.broadcast && config.xdmcp_host.empty())
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, -1);
                    else
                        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_EXTRA);
                    if (IsDlgButtonChecked(hwndDlg, IDC_XDMCP_TERMINATE))
                        config.xdmcpterminate = true;
                    else
                        config.xdmcpterminate = false;
                    return TRUE;
                case IDD_EXTRA:
                    // check for clipboard
                    if (IsDlgButtonChecked(hwndDlg, IDC_CLIPBOARD))
                        config.clipboard = true;
                    else
                        config.clipboard = false;
                    // check for clipboard primary selection
                    if (IsDlgButtonChecked(hwndDlg, IDC_CLIPBOARDPRIMARY))
                        config.clipboardprimary = true;
                    else
                        config.clipboardprimary = false;
                    // check for wgl
                    if (IsDlgButtonChecked(hwndDlg, IDC_WGL))
                        config.wgl = true;
                    else
                        config.wgl = false;
                    // check for access control
                    if (IsDlgButtonChecked(hwndDlg, IDC_DISABLEAC))
                        config.disableac = true;
                    else
                        config.disableac = false;
                    // read parameters
                    {
                        char buffer[512];
                        GetDlgItemText(hwndDlg, IDC_EXTRA_PARAMS, buffer, 512);
                        buffer[511] = 0;
                        config.extra_params = buffer;
                    }
                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_FINISH);
                    return TRUE;
                default:
                    break;
            }
            return FALSE;
        }
        /// @brief Handle PSN_WIZFINISH message.
        /// @param hwndDlg Handle to active page dialog.
        /// @param index Index of current page.
        /// @return TRUE if the message was handled. FALSE otherwise.
        virtual BOOL WizardFinish(HWND hwndDlg, unsigned index)
        {
#ifdef _DEBUG
            printf("finish %d\n", index);
#endif
            return FALSE;
        }
        /// @brief Handle PSN_WIZBACK message.
        /// Basicly handles switching to proper page (skipping XDMCP or program page
        /// if required).
        /// @param hwndDlg Handle to active page dialog.
        /// @param index Index of current page.
        /// @return TRUE if the message was handled. FALSE otherwise.
        virtual BOOL WizardBack(HWND hwndDlg, unsigned index)
        {
            switch (PageID(index))
            {
                case IDD_PROGRAM:
                case IDD_XDMCP:
                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_CLIENTS);
                    return TRUE;
                case IDD_FONTPATH:
                case IDD_EXTRA: // temporary. fontpath is disabled
                    switch (config.client)
                    {
                        case CConfig::NoClient:
                            SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_CLIENTS);
                            return TRUE;
                        case CConfig::StartProgram:
                            SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_PROGRAM);
                            return TRUE;
                        case CConfig::XDMCP:
                            SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, IDD_XDMCP);
                            return TRUE;
                    }
                    break;
            }
            return FALSE;
        }
        /// @brief Handle PSN_SETACTIVE message.
        /// @param hwndDlg Handle to active page dialog.
        /// @param index Index of current page.
        /// @return TRUE if the message was handled. FALSE otherwise.
        virtual BOOL WizardActivate(HWND hwndDlg, unsigned index)
        {
#ifdef _DEBUG
            printf("%s %d\n", __FUNCTION__, index);
#endif
            switch (PageID(index))
            {
                case IDD_CLIENTS:
                    // Enable or disable XDMCP radiobutton and text
                    if (config.window != CConfig::MultiWindow)
                    {
                      EnableWindow(GetDlgItem(hwndDlg, IDC_XDMCP), true);
                      EnableWindow(GetDlgItem(hwndDlg, IDC_XDMCP_DESC), true);
                    }
                    else
                    {
                      if (IsDlgButtonChecked(hwndDlg, IDC_XDMCP))
                        CheckRadioButton(hwndDlg, IDC_CLIENT_NONE, IDC_CLIENT, IDC_CLIENT_NONE);
                      EnableWindow(GetDlgItem(hwndDlg, IDC_XDMCP), false);
                      EnableWindow(GetDlgItem(hwndDlg, IDC_XDMCP_DESC), false);
                    }
                    break;
            }
            return FALSE;
        }
    protected:
        /// @brief Enable or disable the control for remote clients.
        /// @param hwndDlg Handle to active page dialog.
        /// @param state State of control group.
        void EnableRemoteProgramGroup(HWND hwndDlg, BOOL state)
        {
            EnableWindow(GetDlgItem(hwndDlg, IDC_CLIENT_PASSWORD), state);
            EnableWindow(GetDlgItem(hwndDlg, IDC_CLIENT_HOST), state);
            EnableWindow(GetDlgItem(hwndDlg, IDC_CLIENT_USER), state);
            EnableWindow(GetDlgItem(hwndDlg, IDC_CLIENT_PASSWORD_DESC), state);
            EnableWindow(GetDlgItem(hwndDlg, IDC_CLIENT_HOST_DESC), state);
            EnableWindow(GetDlgItem(hwndDlg, IDC_CLIENT_USER_DESC), state);
            EnableWindow(GetDlgItem(hwndDlg, IDC_CLIENT_REMOTEPROGRAM), state);
            EnableWindow(GetDlgItem(hwndDlg, IDC_CLIENT_REMOTEPROGRAM_DESC), state);
            EnableWindow(GetDlgItem(hwndDlg, IDC_CLIENT_PRIVATEKEY), state);
            EnableWindow(GetDlgItem(hwndDlg, IDC_CLIENT_PRIVATEKEY_DESC), state);
            EnableWindow(GetDlgItem(hwndDlg, IDC_CLIENT_PROGRAM), !state);
            EnableWindow(GetDlgItem(hwndDlg, IDC_CLIENT_PROGRAM_DESC), !state);
        }
        /// @brief Enable or disable the control for XDMCP connection.
        /// @param hwndDlg Handle to active page dialog.
        /// @param state State of control group.
        void EnableXDMCPQueryGroup(HWND hwndDlg, BOOL state)
        {
            EnableWindow(GetDlgItem(hwndDlg, IDC_XDMCP_HOST), state);
            EnableWindow(GetDlgItem(hwndDlg, IDC_XDMCP_INDIRECT), state);
        }
        /// @brief Fill program box with default values.
        /// @param hwndDlg Handle to active page dialog.
        void FillProgramBox(HWND hwndDlg)
        {
            HWND cbwnd = GetDlgItem(hwndDlg, IDC_CLIENT_PROGRAM);
            if (cbwnd == NULL)
                return;
            SendMessage(cbwnd, CB_RESETCONTENT, 0, 0);
            SendMessage(cbwnd, CB_ADDSTRING, 0, (LPARAM) "xcalc");
            SendMessage(cbwnd, CB_ADDSTRING, 0, (LPARAM) "xclock");
            SendMessage(cbwnd, CB_ADDSTRING, 0, (LPARAM) "xwininfo");
            SendMessage(cbwnd, CB_SETCURSEL, 0, 0);
        }
        void ShowSaveDialog(HWND parent)
        {
            char szTitle[512];
            char szFilter[512];
            char szFileTitle[512];
            char szFile[MAX_PATH];
            HINSTANCE hInst = GetModuleHandle(NULL);

            LoadString(hInst, IDS_SAVE_TITLE, szTitle, sizeof(szTitle));
            LoadString(hInst, IDS_SAVE_FILETITLE, szFileTitle, sizeof(szFileTitle));
            LoadString(hInst, IDS_SAVE_FILTER, szFilter, sizeof(szFilter));
            for (unsigned i=0; szFilter[i]; i++)
                if (szFilter[i] == '%')
                    szFilter[i] = '\0';

            strcpy(szFile, "config.xlaunch");

            OPENFILENAME ofn;
            memset(&ofn, 0, sizeof(OPENFILENAME));
            ofn.lStructSize = sizeof(OPENFILENAME);
            ofn.hwndOwner = parent;
            ofn.lpstrFilter = szFilter;
            ofn.lpstrFile= szFile;
            ofn.nMaxFile = sizeof(szFile)/ sizeof(*szFile);
            ofn.lpstrFileTitle = szFileTitle;
            ofn.nMaxFileTitle = sizeof(szFileTitle);
            ofn.lpstrInitialDir = (LPSTR)NULL;
            ofn.Flags = OFN_SHOWHELP | OFN_OVERWRITEPROMPT;
            ofn.lpstrTitle = szTitle;

            if (GetSaveFileName(&ofn))
            {
                try {
                          config.Save(ofn.lpstrFile);
                } catch (std::runtime_error &e)
                {
                char Message[255];
                sprintf(Message,"Failure: %s\n", e.what());
                MessageBox(NULL,Message,"Exception",MB_OK);
                }
            }
        }
    public:

        /// @brief Handle messages fo the dialog pages.
        /// @param hwndDlg Handle of active dialog.
        /// @param uMsg Message code.
        /// @param wParam Message parameter.
        /// @param lParam Message parameter.
        /// @param psp Handle to sheet paramters.
        virtual INT_PTR PageDispatch(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam, PROPSHEETPAGE *psp)
        {
            HWND hwnd;
            switch (uMsg)
            {
                case WM_INITDIALOG:
                    switch (PageID(PageIndex(psp)))
                    {
                        case IDD_DISPLAY:
                            // Init display dialog. Enable correct check buttons
                            switch (config.window)
                            {
                                default:
                                case CConfig::MultiWindow:
                                    CheckRadioButton(hwndDlg, IDC_MULTIWINDOW, IDC_NODECORATION, IDC_MULTIWINDOW);
                                    break;
                                case  CConfig::Fullscreen:
                                    CheckRadioButton(hwndDlg, IDC_MULTIWINDOW, IDC_NODECORATION, IDC_FULLSCREEN);
                                    break;
                                case  CConfig::Windowed:
                                    CheckRadioButton(hwndDlg, IDC_MULTIWINDOW, IDC_NODECORATION, IDC_WINDOWED);
                                    break;
                                case  CConfig::Nodecoration:
                                    CheckRadioButton(hwndDlg, IDC_MULTIWINDOW, IDC_NODECORATION, IDC_NODECORATION);
                                    break;
                            }
                            // Set display number
                            SetDlgItemText(hwndDlg, IDC_DISPLAY, config.display.c_str());
                            break;
                        case IDD_CLIENTS:
                            // Init client dialog. Enable correct check buttons
                            switch (config.client)
                            {
                                default:
                                case CConfig::NoClient:
                                    CheckRadioButton(hwndDlg, IDC_CLIENT_NONE, IDC_CLIENT, IDC_CLIENT_NONE);
                                    break;
                                case CConfig::StartProgram:
                                    CheckRadioButton(hwndDlg, IDC_CLIENT_NONE, IDC_CLIENT, IDC_CLIENT);
                                    break;
                                case CConfig::XDMCP:
                                    CheckRadioButton(hwndDlg, IDC_CLIENT_NONE, IDC_CLIENT, IDC_XDMCP);
                                    break;
                            }
                            break;
                        case IDD_PROGRAM:
                            // Init program dialog. Check local and remote buttons
                            CheckRadioButton(hwndDlg, IDC_CLIENT_LOCAL, IDC_CLIENT_REMOTE, config.local?IDC_CLIENT_LOCAL:IDC_CLIENT_REMOTE);
                            EnableRemoteProgramGroup(hwndDlg, config.local?FALSE:TRUE);
                            // Fill combo boxes
                            FillProgramBox(hwndDlg);
                            // Set edit fields
                            SetDlgItemText(hwndDlg, IDC_CLIENT_PROGRAM, config.localprogram.c_str());
                            SetDlgItemText(hwndDlg, IDC_CLIENT_REMOTEPROGRAM, config.remoteprogram.c_str());
                            SetDlgItemText(hwndDlg, IDC_CLIENT_USER, config.user.c_str());
                            SetDlgItemText(hwndDlg, IDC_CLIENT_HOST, config.host.c_str());
                            SetDlgItemText(hwndDlg, IDC_CLIENT_PASSWORD, config.remotepassword.c_str());
                            SetDlgItemText(hwndDlg, IDC_CLIENT_PRIVATEKEY, config.privatekey.c_str());
                            break;
                        case IDD_XDMCP:
                            // Init XDMCP dialog. Check broadcast and indirect button
                            CheckRadioButton(hwndDlg, IDC_XDMCP_QUERY, IDC_XDMCP_BROADCAST, config.broadcast?IDC_XDMCP_BROADCAST:IDC_XDMCP_QUERY);
                            CheckDlgButton(hwndDlg, IDC_XDMCP_INDIRECT, config.indirect?BST_CHECKED:BST_UNCHECKED);
                            EnableXDMCPQueryGroup(hwndDlg, config.broadcast?FALSE:TRUE);
                            // Set hostname
                            SetDlgItemText(hwndDlg, IDC_XDMCP_HOST, config.xdmcp_host.c_str());
                            CheckDlgButton(hwndDlg, IDC_XDMCP_TERMINATE, config.xdmcpterminate?BST_CHECKED:BST_UNCHECKED);
                            break;
                        case IDD_EXTRA:
                            CheckDlgButton(hwndDlg, IDC_CLIPBOARD, config.clipboard?BST_CHECKED:BST_UNCHECKED);
                            CheckDlgButton(hwndDlg, IDC_CLIPBOARDPRIMARY, config.clipboardprimary?BST_CHECKED:BST_UNCHECKED);
                            CheckDlgButton(hwndDlg, IDC_WGL, config.wgl?BST_CHECKED:BST_UNCHECKED);
                            CheckDlgButton(hwndDlg, IDC_DISABLEAC, config.disableac?BST_CHECKED:BST_UNCHECKED);
                            SetDlgItemText(hwndDlg, IDC_EXTRA_PARAMS, config.extra_params.c_str());
                            break;

                    }
                case WM_COMMAND:
                    // Handle control messages
                    switch (LOWORD(wParam))
                    {
                        // Handle clicks on images. Check proper radiobutton
                        case IDC_MULTIWINDOW_IMG:
                        case IDC_FULLSCREEN_IMG:
                        case IDC_WINDOWED_IMG:
                        case IDC_NODECORATION_IMG:
                            CheckRadioButton(hwndDlg, IDC_MULTIWINDOW, IDC_NODECORATION, LOWORD(wParam)-4);
                            SetFocus(GetDlgItem(hwndDlg, LOWORD(wParam)-4));
                            break;
                        // Disable unavailable controls
                        case IDC_CLIENT_REMOTE:
                        case IDC_CLIENT_LOCAL:
                            EnableRemoteProgramGroup(hwndDlg, LOWORD(wParam) == IDC_CLIENT_REMOTE);
                            break;
                        case IDC_XDMCP_QUERY:
                        case IDC_XDMCP_BROADCAST:
                            EnableXDMCPQueryGroup(hwndDlg, LOWORD(wParam) == IDC_XDMCP_QUERY);
                            break;
                        case IDC_FINISH_SAVE:
                            ShowSaveDialog(hwndDlg);
                            break;
                    }
            }
            // pass messages to parent
            return CWizard::PageDispatch(hwndDlg, uMsg, wParam, lParam, psp);
        }

        /// @brief Try to connect to server.
        /// Repeat until successful, server died or maximum number of retries
        /// reached.
        Display *WaitForServer(HANDLE serverProcess)
        {
            int     ncycles  = 120;         /* # of cycles to wait */
            int     cycles;                 /* Wait cycle count */
            Display *xd;

            for (cycles = 0; cycles < ncycles; cycles++) {
                if ((xd = XOpenDisplay(NULL))) {
                    return xd;
                }
                else {
                    if (WaitForSingleObject(serverProcess, 1000) == WAIT_TIMEOUT)
                        continue;
                }
            }
            return NULL;
        }

        /// @brief Do the actual start of VCXsrv and clients
        void StartUp()
        {
            std::string buffer;
            std::string client;

            // Construct display strings
            int DisplayNbr=atoi(config.display.c_str());
            std::string display_id = ":" + config.display;
            std::string display = "DISPLAY=127.0.0.1" + display_id + ".0";

            // Build Xsrv commandline
            buffer = "vcxsrv ";
            if (DisplayNbr!=-1)
              buffer += display_id + " ";
            switch (config.window)
            {
                case CConfig::MultiWindow:
                    buffer += "-multiwindow ";
                    break;
                case CConfig::Fullscreen:
                    buffer += "-fullscreen ";
                    break;
                case CConfig::Nodecoration:
                    buffer += "-nodecoration ";
                    break;
                default:
                    break;
            }
            // Add XDMCP parameter
            if (config.client == CConfig::XDMCP)
            {
                if (config.broadcast)
                    buffer += "-broadcast ";
                else
                {
                    if (config.indirect)
                        buffer += "-indirect ";
                    else
                        buffer += "-query ";
                    buffer += config.xdmcp_host;
                    buffer += " ";
                }
                if (config.xdmcpterminate)
                    buffer += "-terminate ";
            }
            if (config.clipboard)
                buffer += "-clipboard ";
            if (!config.clipboardprimary)
                buffer += "-noprimary ";
            if (config.wgl)
                buffer += "-wgl ";
            else
                buffer += "-nowgl ";
            if (config.disableac)
                buffer += "-ac ";
            if (!config.extra_params.empty())
            {
                buffer += config.extra_params;
                buffer += " ";
            }

            int *pDisplayfd;
            if (DisplayNbr==-1)
            {
              // Pass the handle of some shared memory to vcxsrv to getting back the display nbr
              SECURITY_ATTRIBUTES sa;
              sa.nLength=sizeof(sa);
              sa.lpSecurityDescriptor=NULL;
              sa.bInheritHandle=TRUE;
              HANDLE hDisplayFdMem=CreateFileMapping(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(int), NULL);
              pDisplayfd=(int*)MapViewOfFile(hDisplayFdMem, FILE_MAP_READ|FILE_MAP_WRITE, 0, 0, 0);
              *pDisplayfd=-1;  // Not yet initialised
              buffer+="-displayfd ";
              std::stringstream ss;
              ss<<(uintptr_t)hDisplayFdMem;
              buffer+=ss.str();
            }
            // Construct client commandline
            if (config.client == CConfig::StartProgram)
            {
                if (!config.local)
                {
                    char cmdline[512];
                    std::string host = config.host;
                    std::string remotepassword;
                    if (!config.user.empty())
                        host = config.user + "@" + config.host;
                    if (!config.remotepassword.empty())
                      remotepassword=std::string(" -pw ")+config.remotepassword;
                    if (!config.privatekey.empty())
                      remotepassword+=std::string(" -i \"")+config.privatekey+"\"";
                    _snprintf(cmdline,512,"plink -ssh -C -X%s %s %s",
                                remotepassword.c_str(), host.c_str(),config.remoteprogram.c_str());
                    client += cmdline;
                }
                else
                {
                    client += config.localprogram.c_str();
                }
            }

            // Prepare program startup
            STARTUPINFO si, sic;
            PROCESS_INFORMATION pi, pic;
            DWORD hcount = 0;
            Display *dpy = NULL;

            ZeroMemory( &si, sizeof(si) );
            si.cb = sizeof(si);
            ZeroMemory( &pi, sizeof(pi) );
            ZeroMemory( &sic, sizeof(sic) );
            sic.cb = sizeof(sic);
            ZeroMemory( &pic, sizeof(pic) );

            // Start VCXsrv process.
#ifdef _DEBUG
            printf("%s\n", buffer.c_str());
#endif
            char CurDir[MAX_PATH];
            GetModuleFileName(NULL,CurDir,MAX_PATH);
            *strrchr(CurDir,'\\')=0;

            if( !CreateProcess( NULL, (CHAR*)buffer.c_str(), NULL, NULL,
                        TRUE, 0, NULL, CurDir, &si, &pi ))
                throw win32_error("CreateProcess failed");

            if (!client.empty())
            {
                if (DisplayNbr==-1)
                {
                  // Wait maximum 10 seconds
                  int Count=1000;
                  while (-1==*pDisplayfd)
                  {
                    if (Count-- < 0)
                      throw std::runtime_error("Connection to server failed");
                    Sleep(10);
                  }
                  std::stringstream ss;
                  ss<<*pDisplayfd;
                  display_id = ":" + ss.str();
                  display = "DISPLAY=127.0.0.1" + display_id + ".0";
                }
                // Set DISPLAY variable
                _putenv(display.c_str());

                // Wait for server to startup
                dpy = WaitForServer(pi.hProcess);
                if (dpy == NULL)
                {
                    TerminateProcess(pi.hProcess, (DWORD)-1);
                    throw std::runtime_error("Connection to server failed");
                }

#ifdef _DEBUG
                printf("%s\n", client.c_str());
#endif

                // Start the child process.

                #if 1
                // Create a console, otherwise some commands will not execute with plink
                HWINSTA h=GetProcessWindowStation();
                HWND hConsoleWnd=NULL;
                if (h)
                {
                  AllocConsole();
                  hConsoleWnd=GetConsoleWindow();
                  ShowWindow(hConsoleWnd, SW_HIDE );  // make it hidden, the disadvantage of this method is that the console window flashes
                  // but we must be able to show it when the client puts some output
                }

                HANDLE hChildStdinRd;
                HANDLE hChildStdinWr;
                HANDLE hChildStdoutRd;
                HANDLE hChildStdoutWr;
                SECURITY_ATTRIBUTES saAttr;
                BOOL fSuccess;

                saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
                saAttr.bInheritHandle = TRUE;
                saAttr.lpSecurityDescriptor = NULL;

                if (!CreatePipe(&hChildStdinRd, &hChildStdinWr, &saAttr, 0))
                  throw win32_error("CreatePipe failed", GetLastError());

                // Ensure the write handle to the pipe for STDIN is not inherited. 
                if ( ! SetHandleInformation(hChildStdinWr, HANDLE_FLAG_INHERIT, 0) )
                  throw win32_error("SetHandleInformation failed", GetLastError());

                if (!CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &saAttr, 0))
                  throw win32_error("CreatePipe failed", GetLastError());

                // Ensure the read handle to the pipe for STDOUT is not inherited. 
                if ( ! SetHandleInformation(hChildStdoutRd, HANDLE_FLAG_INHERIT, 0) )
                  throw win32_error("SetHandleInformation failed", GetLastError());

                sic.dwFlags = STARTF_USESTDHANDLES;
                sic.hStdInput = hChildStdinRd;
                sic.hStdOutput = hChildStdoutWr;
                sic.hStdError = hChildStdoutWr;

                if (!CreateProcess(NULL,(CHAR*)client.c_str(),NULL,NULL,TRUE,0,NULL, CurDir, &sic, &pic))
                {
                  DWORD err = GetLastError();
                  TerminateProcess(pi.hProcess, (DWORD)-1);
                  throw win32_error("CreateProcess failed", err);
                }
                CloseHandle(hChildStdinRd);
                CloseHandle(hChildStdoutWr);
                CloseHandle(pic.hThread);

                int hStdIn = _open_osfhandle((intptr_t)hChildStdinWr, _O_WRONLY|_O_BINARY);
                int hStdOut = _open_osfhandle((intptr_t)hChildStdoutRd, _O_RDONLY|_O_BINARY);
                HANDLE hConsoleInput=GetStdHandle(STD_INPUT_HANDLE);
                HANDLE hConsoleOutput=GetStdHandle(STD_OUTPUT_HANDLE);
                SetConsoleMode(hConsoleInput, 0);  // Needed to disable local echo, and return only upon carriage return of read function
                while (1)
                {
                  if (!WaitForSingleObject(pic.hProcess, 20 ))
                  {
                      // Child does not exist anymore, but it could be that there is still error output in the pipes
                      // So wait some time, that then check the output again
                    Sleep(500);
                    CheckOutput(hChildStdoutRd, hStdOut, hStdIn);
                    break;
                  }
                  if (!WaitForSingleObject(pi.hProcess, 0))
                  {
                    TerminateProcess(pic.hProcess, (DWORD)-1);
                    break;
                  }
                  CheckOutput(hChildStdoutRd, hStdOut, hStdIn);
                }
                #else
                // Hide a console window
                // FIXME: This may make it impossible to enter the password
                sic.dwFlags = STARTF_USESHOWWINDOW;
                sic.wShowWindow = SW_HIDE;

                if( !CreateProcess( NULL, (CHAR*)client.c_str(), NULL, NULL,
                            FALSE, 0, NULL, CurDir, &sic, &pic ))
                {
                  DWORD err = GetLastError();
                  while (hcount--)
                      TerminateProcess(handles[hcount], (DWORD)-1);
                  throw win32_error("CreateProcess failed", err);
                }
                CloseHandle( pic.hThread );
                #endif
            }

            // Wait until child process exits.
            DWORD ret = WaitForSingleObject(pic.hProcess, INFINITE );

#ifdef _DEBUG
            printf("killing process!\n");
#endif
            // Check if VCXsrv is still running, but only when we started a local program or a client
            if (config.local || !client.empty())
            {
              DWORD exitcode;
              GetExitCodeProcess(pi.hProcess, &exitcode);
              unsigned counter = 0;
              while (exitcode == STILL_ACTIVE)
              {
                if (++counter > 10)
                    TerminateProcess(pi.hProcess, (DWORD)-1);
                else
                    // Shutdown VCXsrv (the soft way!)
                    EnumThreadWindows(pi.dwThreadId, KillWindowsProc, 0);
                Sleep(500);
                GetExitCodeProcess(pi.hProcess, &exitcode);
              }
            }

            // Close process and thread handles.
            CloseHandle( pi.hProcess );
            CloseHandle( pi.hThread );
            CloseHandle( pic.hProcess );
        }
};

int main(int argc, char **argv)
{
    try {
        InitCommonControls();
        CMyWizard dialog;

        bool skip_wizard = false;

        for (int i = 1; i < argc; i++)
        {
            if (argv[i] == NULL)
                continue;

            std::string arg(argv[i]);
            if (arg == "-load" && i + 1 < argc)
            {
                i++;
                dialog.LoadConfig(argv[i]);
                continue;
            }
            if (arg == "-run" && i + 1 < argc)
            {
                i++;
                dialog.LoadConfig(argv[i]);
                skip_wizard = true;
                continue;
            }
        }

        int ret = 0;
        if (skip_wizard || (ret =dialog.ShowModal()) != 0)
            dialog.StartUp();
#ifdef _DEBUG
        printf("return %d\n", ret);
#endif
        return 0;
    } catch (std::runtime_error &e)
    {
                char Message[255];
                sprintf(Message,"Failure: %s\n", e.what());
                MessageBox(NULL,Message,"Exception",MB_OK);
        return -1;
    }
}
