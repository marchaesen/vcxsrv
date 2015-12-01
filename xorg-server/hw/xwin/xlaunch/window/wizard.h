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
#ifndef __WIZARD_H__
#define __WIZARD_H__

#include "dialog.h"
#include <vector>

#include <prsht.h>

class CWizard : public CBaseDialog
{
    private:
        std::vector<PROPSHEETPAGE> pages;
        void PrepareSheetHeader(PROPSHEETHEADER &psh, BOOL modal);
    protected:
        virtual HWND CreateWindowHandle();
        static INT_PTR CALLBACK WizardDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
        virtual INT_PTR DlgDispatch(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
        virtual INT_PTR PageDispatch(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam, PROPSHEETPAGE *psp);
        virtual unsigned PageIndex(PROPSHEETPAGE *psp);
	virtual uintptr_t PageID(unsigned index);
        virtual BOOL WizardNext(HWND hwndDlg, unsigned index) { return FALSE; }
        virtual BOOL WizardBack(HWND hwndDlg, unsigned index) { return FALSE; } 
        virtual BOOL WizardFinish(HWND hwndDlg, unsigned index) { return FALSE; }
        virtual BOOL WizardReset(HWND hwndDlg, unsigned index) { return FALSE; }
        virtual BOOL WizardActivate(HWND hwndDlg, unsigned index) { return FALSE; }
    public:
        CWizard();
        void AddPage(const PROPSHEETPAGE &page);
        void AddPage(const char *page, HINSTANCE instance = NULL);
        void AddPage(DWORD id, DWORD title, DWORD subtitle, HINSTANCE instance = NULL);
        virtual int ShowModal();
};
#endif
