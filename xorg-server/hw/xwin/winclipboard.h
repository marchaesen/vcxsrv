#ifndef _WINCLIPBOARD_H_
#define _WINCLIPBOARD_H_
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

/* Standard library headers */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef __CYGWIN__
#include <sys/select.h>
#else
#include <X11/Xwinsock.h>
#endif
#include <fcntl.h>
#include <setjmp.h>
#include <pthread.h>

/* X headers */
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xlocale.h>

/* Windows headers */
#include <X11/Xwindows.h>

/* Clipboard module constants */
#define WIN_CLIPBOARD_WINDOW_CLASS		"xwinclip"
#define WIN_CLIPBOARD_WINDOW_TITLE		"xwinclip"
#ifdef HAS_DEVWINDOWS
#define WIN_MSG_QUEUE_FNAME			"/dev/windows"
#endif
#define WIN_CONNECT_RETRIES			40
#define WIN_CONNECT_DELAY			4
#define WIN_JMP_OKAY				0
#define WIN_JMP_ERROR_IO			2
#define WIN_LOCAL_PROPERTY			"CYGX_CUT_BUFFER"
#define WIN_XEVENTS_SUCCESS			0
#define WIN_XEVENTS_CONVERT			2
#define WIN_XEVENTS_NOTIFY			3
#define WIN_CLIPBOARD_RETRIES			40
#define WIN_CLIPBOARD_DELAY			1

#define WM_WM_REINIT                           (WM_USER + 1)

/*
 * References to external symbols
 */

extern char *display;
extern void winDebug(const char *format, ...);
extern void winErrorFVerb(int verb, const char *format, ...);

/*
 * winclipboardinit.c
 */

Bool
 winInitClipboard(void);

HWND winClipboardCreateMessagingWindow(void);

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

void *winClipboardProc(void *);

/*
 * winclipboardwndproc.c
 */

BOOL winClipboardFlushWindowsMessageQueue(HWND hwnd);

LRESULT CALLBACK
winClipboardWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

/*
 * winclipboardxevents.c
 */

int

winClipboardFlushXEvents(HWND hwnd,
                         int iWindow, Display * pDisplay, Bool fUnicodeSupport);
#endif
