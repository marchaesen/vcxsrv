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
#include "window/util.h"
#include "window/wizard.h"
#include "resources/resources.h"
#include "config.h"
#include <prsht.h>
#include <commctrl.h>

#include <stdexcept>

#include <X11/Xlib.h>

#include <stdlib.h>

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
                        SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
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
                        SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
                    }
                    else
                        SetWindowLong(hwndDlg, DWL_MSGRESULT, IDD_CLIENTS);
                    return TRUE;
                case IDD_CLIENTS:
                    // Check for select client startup method
                    if (IsDlgButtonChecked(hwndDlg, IDC_CLIENT))
                    {
                        config.client = CConfig::StartProgram;
                        SetWindowLong(hwndDlg, DWL_MSGRESULT, IDD_PROGRAM);
                    } else if (IsDlgButtonChecked(hwndDlg, IDC_XDMCP))
                    {
                        config.client = CConfig::XDMCP;
                        SetWindowLong(hwndDlg, DWL_MSGRESULT, IDD_XDMCP);
                    } else if (IsDlgButtonChecked(hwndDlg, IDC_CLIENT_NONE))
                    {
                        config.client = CConfig::NoClient;
                        SetWindowLong(hwndDlg, DWL_MSGRESULT, IDD_EXTRA);
                    } else
                        SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
                    return TRUE;
                case IDD_PROGRAM:
                    // Check wether local or remote client should be started
                    if (IsDlgButtonChecked(hwndDlg, IDC_CLIENT_LOCAL))
                        config.local = true;
                    else if (IsDlgButtonChecked(hwndDlg, IDC_CLIENT_REMOTE))
                        config.local = false;
                    else
                    {
                        SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
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
                    }
                    // Check for valid input
                    if (!config.local && (config.host.empty() || config.localprogram.empty() || config.remoteprogram.empty()))
                        SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
                    else
                        SetWindowLong(hwndDlg, DWL_MSGRESULT, IDD_EXTRA);
                    return TRUE;
                case IDD_XDMCP:
                    // Check for broadcast
                    if (IsDlgButtonChecked(hwndDlg, IDC_XDMCP_BROADCAST))
                        config.broadcast = true;
                    else if (IsDlgButtonChecked(hwndDlg, IDC_XDMCP_QUERY))
                        config.broadcast = false;
                    else
                    {
                        SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
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
                        SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
                    else	
                        SetWindowLong(hwndDlg, DWL_MSGRESULT, IDD_EXTRA);
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
                    SetWindowLong(hwndDlg, DWL_MSGRESULT, IDD_FINISH);
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
                    SetWindowLong(hwndDlg, DWL_MSGRESULT, IDD_CLIENTS);
                    return TRUE;
                case IDD_FONTPATH:
                case IDD_EXTRA: // temporary. fontpath is disabled
                    switch (config.client)
                    {
                        case CConfig::NoClient:	
                            SetWindowLong(hwndDlg, DWL_MSGRESULT, IDD_CLIENTS);
                            return TRUE;
                        case CConfig::StartProgram:
                            SetWindowLong(hwndDlg, DWL_MSGRESULT, IDD_PROGRAM);
                            return TRUE;
                        case CConfig::XDMCP:
                            SetWindowLong(hwndDlg, DWL_MSGRESULT, IDD_XDMCP);
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
                            if (!config.localprogram.empty())
                                       SetDlgItemText(hwndDlg, IDC_CLIENT_PROGRAM, config.localprogram.c_str());
                            if (!config.remoteprogram.empty())
                                       SetDlgItemText(hwndDlg, IDC_CLIENT_REMOTEPROGRAM, config.remoteprogram.c_str());
                            SetDlgItemText(hwndDlg, IDC_CLIENT_USER, config.user.c_str());
                            SetDlgItemText(hwndDlg, IDC_CLIENT_HOST, config.host.c_str());
                            SetDlgItemText(hwndDlg, IDC_CLIENT_PASSWORD, config.remotepassword.c_str());
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
            std::string display_id = ":" + config.display;
            std::string display = "DISPLAY=127.0.0.1" + display_id + ".0";

            // Build Xsrv commandline
            buffer = "vcxsrv " + display_id + " ";
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
                buffer += "-noclipboardprimary ";
            if (config.wgl)
                buffer += "-wgl ";
            if (config.disableac)
                buffer += "-ac ";
            if (!config.extra_params.empty())
            {
                buffer += config.extra_params;
                buffer += " ";
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
                      // Need to use -console since certain commands will not execute when no console
                    _snprintf(cmdline,512,"plink -console -ssh -X%s %s %s", 
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
            HANDLE handles[2];
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
                        FALSE, 0, NULL, CurDir, &si, &pi )) 
                throw win32_error("CreateProcess failed");
            handles[hcount++] = pi.hProcess;

            if (!client.empty())
            {
                // Set DISPLAY variable
                _putenv(display.c_str());

                // Wait for server to startup
                dpy = WaitForServer(pi.hProcess);
                if (dpy == NULL)
                {
                    while (hcount--)
                        TerminateProcess(handles[hcount], (DWORD)-1);
                    throw std::runtime_error("Connection to server failed");
                }
                
#ifdef _DEBUG
                printf("%s\n", client.c_str());
#endif

                // Hide a console window 
                // FIXME: This may make it impossible to enter the password
                sic.dwFlags = STARTF_USESHOWWINDOW;
                sic.wShowWindow = SW_HIDE;
                
                // Start the child process. 

                // Create a console, otherwise some commands will not execute with plink
                #ifdef WITH_FLASH
                AllocConsole();
                #else
                HWINSTA h=GetProcessWindowStation();
                HWINSTA horig=h;
                if (h)
                {
                  h=CreateWindowStationW(NULL, 0, STANDARD_RIGHTS_READ, NULL);
                  if (h)
                    SetProcessWindowStation(h);
                  AllocConsole();
                  SetProcessWindowStation(horig);
                  CloseWindowStation(h);
                }
                #endif

                if( !CreateProcess( NULL, (CHAR*)client.c_str(), NULL, NULL,
                            FALSE, 0, NULL, CurDir, &sic, &pic )) 
                {
                    DWORD err = GetLastError();
                    while (hcount--)
                        TerminateProcess(handles[hcount], (DWORD)-1);
                    throw win32_error("CreateProcess failed", err);
                }
                handles[hcount++] = pic.hProcess;
                #ifdef WITHFLASH
                ShowWindow( GetConsoleWindow(), SW_HIDE );  // make it hidden
                #endif
            }

            // Wait until any child process exits.
            DWORD ret = WaitForMultipleObjects(hcount, handles, FALSE, INFINITE );

#ifdef _DEBUG
            printf("killing process!\n");
#endif
            // Check if Xsrv is still running, but only when we started a local program
            if (config.local)
            {
            DWORD exitcode;
            GetExitCodeProcess(pi.hProcess, &exitcode);
            unsigned counter = 0;
            while (exitcode == STILL_ACTIVE)
            {
                if (++counter > 10)
                    TerminateProcess(pi.hProcess, (DWORD)-1);
                else
                    // Shutdown Xsrv (the soft way!)
                    EnumThreadWindows(pi.dwThreadId, KillWindowsProc, 0);
                Sleep(500);
                GetExitCodeProcess(pi.hProcess, &exitcode);
            }
            // Kill the client
            TerminateProcess(pic.hProcess, (DWORD)-1);
            }

            // Close process and thread handles. 
            CloseHandle( pi.hProcess );
            CloseHandle( pi.hThread );
            CloseHandle( pic.hProcess );
            CloseHandle( pic.hThread );
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




