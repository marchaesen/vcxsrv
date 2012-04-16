/*
 * Copyright 2001-2004 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   David H. Dawes <dawes@xfree86.org>
 *   Kevin E. Martin <kem@redhat.com>
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * Interface for cursor support.  \see dmxcursor.c. */

#ifndef DMXCURSOR_H
#define DMXCURSOR_H

#include "mipointer.h"

/** Cursor private area. */
typedef struct _dmxCursorPriv {
    Cursor cursor;
} dmxCursorPrivRec, *dmxCursorPrivPtr;

/** Cursor functions for mi layer. \see dmxcursor.c \see dmxscrinit.c */
extern miPointerScreenFuncRec dmxPointerCursorFuncs;

/** Sprite functions for mi layer. \see dmxcursor.c \see dmxscrinit.c */
extern miPointerSpriteFuncRec dmxPointerSpriteFuncs;

extern void dmxReInitOrigins(void);
extern void dmxInitOrigins(void);
extern void dmxInitOverlap(void);
extern void dmxCursorNoMulti(void);
extern void dmxMoveCursor(DeviceIntPtr pDev, ScreenPtr pScreen, int x, int y);
extern void dmxCheckCursor(void);
extern int dmxOnScreen(int x, int y, DMXScreenInfo * dmxScreen);
extern void dmxHideCursor(DMXScreenInfo * dmxScreen);

extern void dmxBECreateCursor(ScreenPtr pScreen, CursorPtr pCursor);
extern Bool dmxBEFreeCursor(ScreenPtr pScreen, CursorPtr pCursor);

#define DMX_GET_CURSOR_PRIV(_pCursor, _pScreen) ((dmxCursorPrivPtr) \
    dixLookupScreenPrivate(&(_pCursor)->devPrivates, CursorScreenKey, _pScreen))

#define DMX_SET_CURSOR_PRIV(_pCursor, _pScreen, v) \
    dixSetScreenPrivate(&(_pCursor)->devPrivates, CursorScreenKey, _pScreen, v)

#endif                          /* DMXCURSOR_H */
