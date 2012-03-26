/*
 * External interface for the server's AppleWM support
 */
/**************************************************************************

Copyright (c) 2002 Apple Computer, Inc. All Rights Reserved.
Copyright (c) 2003-2004 Torrey T. Lyons. All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

#ifndef _APPLEWMEXT_H_
#define _APPLEWMEXT_H_

#include "window.h"
#include <Xplugin.h>

#if XPLUGIN_VERSION < 4
typedef int xp_frame_attr;
typedef int xp_frame_class;
typedef int xp_frame_rect;
#endif

typedef int (*DisableUpdateProc) (void);
typedef int (*EnableUpdateProc) (void);
typedef int (*SetWindowLevelProc) (WindowPtr pWin, int level);
typedef int (*FrameGetRectProc) (xp_frame_rect type, xp_frame_class class,
                                 const BoxRec * outer, const BoxRec * inner,
                                 BoxRec * ret);
typedef int (*FrameHitTestProc) (xp_frame_class class, int x, int y,
                                 const BoxRec * outer, const BoxRec * inner,
                                 int *ret);
typedef int (*FrameDrawProc) (WindowPtr pWin, xp_frame_class class,
                              xp_frame_attr attr, const BoxRec * outer,
                              const BoxRec * inner, unsigned int title_len,
                              const unsigned char *title_bytes);
typedef int (*SendPSNProc) (uint32_t hi, uint32_t lo);
typedef int (*AttachTransientProc) (WindowPtr pWinChild, WindowPtr pWinParent);

/*
 * AppleWM implementation function list
 */
typedef struct _AppleWMProcs {
    DisableUpdateProc DisableUpdate;
    EnableUpdateProc EnableUpdate;
    SetWindowLevelProc SetWindowLevel;
    FrameGetRectProc FrameGetRect;
    FrameHitTestProc FrameHitTest;
    FrameDrawProc FrameDraw;
    SendPSNProc SendPSN;
    AttachTransientProc AttachTransient;
} AppleWMProcsRec, *AppleWMProcsPtr;

void AppleWMExtensionInit(AppleWMProcsPtr procsPtr);

void AppleWMSetScreenOrigin(WindowPtr pWin);

Bool AppleWMDoReorderWindow(WindowPtr pWin);

void AppleWMSendEvent(int /* type */ ,
                      unsigned int /* mask */ ,
                      int /* which */ ,
                      int       /* arg */
    );

unsigned int AppleWMSelectedEvents(void
    );

#endif                          /* _APPLEWMEXT_H_ */
