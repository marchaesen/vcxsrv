
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

#ifndef WINCLIPBOARD_INTERNAL_H
#define WINCLIPBOARD_INTERNAL_H

/* X headers */
#include <X11/Xlib.h>

/* Windows headers */
#include <X11/Xwindows.h>

#define WIN_XEVENTS_SUCCESS			0
#define WIN_XEVENTS_FAILED			1
#define WIN_XEVENTS_NOTIFY_DATA			3
#define WIN_XEVENTS_NOTIFY_TARGETS		4

#define WM_WM_REINIT                           (WM_USER + 1)
#define WM_WM_QUIT                             (WM_USER + 2)

/*
 * References to external symbols
 */

extern void winDebug(const char *format, ...);
extern void ErrorF(const char *format, ...);

/*
 * winclipboardtextconv.c
 */

void
 winClipboardDOStoUNIX(char *pszData, int iLength);

void
 winClipboardUNIXtoDOS(char **ppszData, int iLength);

/*
 * winclipboardthread.c
 */


typedef struct
{
    Atom atomClipboard;
    Atom atomLocalProperty;
    Atom atomUTF8String;
    Atom atomCompoundText;
    Atom atomTargets;
} ClipboardAtoms;

/*
 * winclipboardwndproc.c
 */

Bool winClipboardFlushWindowsMessageQueue(HWND hwnd);

LRESULT CALLBACK
winClipboardWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

typedef struct
{
  Display *pClipboardDisplay;
  Window iClipboardWindow;
  ClipboardAtoms *atoms;
} ClipboardWindowCreationParams;

/*
 * winclipboardxevents.c
 */

typedef struct
{
  Bool fUseUnicode;
  Atom *targetList;
} ClipboardConversionData;

int
winClipboardFlushXEvents(HWND hwnd,
                         Window iWindow, Display * pDisplay, ClipboardConversionData *data, ClipboardAtoms *atom);


Atom
winClipboardGetLastOwnedSelectionAtom(ClipboardAtoms *atoms);

void
winClipboardInitMonitoredSelections(void);

#endif
