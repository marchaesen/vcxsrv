/*
 * Copyright (c) 1995  X Consortium
 * Copyright 2004 Red Hat Inc., Durham, North Carolina.
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
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT, THE X CONSORTIUM,
 * AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the X Consortium
 * shall not be used in advertising or otherwise to promote the sale,
 * use or other dealings in this Software without prior written
 * authorization from the X Consortium.
 */

/*
 * Derived from hw/xnest/Xnest.h by Rickard E. (Rik) Faith <faith@redhat.com>
 */

/** \file
 * This file includes all client-side include files with proper wrapping.
 */

#ifndef _DMXCLIENT_H_
#define _DMXCLIENT_H_

#define GC XlibGC

#ifdef _XSERVER64
#define DMX64
#undef _XSERVER64
typedef unsigned long XID64;
typedef unsigned long Mask64;
typedef unsigned long Atom64;
typedef unsigned long VisualID64;
typedef unsigned long Time64;

#define XID           XID64
#define Mask          Mask64
#define Atom          Atom64
#define VisualID      VisualID64
#define Time          Time64
typedef XID Window64;
typedef XID Drawable64;
typedef XID Font64;
typedef XID Pixmap64;
typedef XID Cursor64;
typedef XID Colormap64;
typedef XID GContext64;
typedef XID KeySym64;

#define Window        Window64
#define Drawable      Drawable64
#define Font          Font64
#define Pixmap        Pixmap64
#define Cursor        Cursor64
#define Colormap      Colormap64
#define GContext      GContext64
#define KeySym        KeySym64
#endif

#include <X11/Xlib.h>
#include <X11/Xlibint.h>        /* For _XExtension */
#include <X11/X.h>              /* from glxserver.h */
#include <X11/Xmd.h>            /* from glxserver.h */
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>

#include <X11/extensions/shape.h>

#include <X11/extensions/Xrender.h>
#undef PictFormatType

#include <X11/extensions/XKB.h>
#include "xkbstr.h"

#include <X11/extensions/XI.h>

/* Always include these, since we query them even if we don't export XINPUT. */
#include <X11/extensions/XInput.h>      /* For XDevice */
#include <X11/extensions/Xext.h>

#undef GC

#ifdef DMX64
#define _XSERVER64
#undef XID
#undef Mask
#undef Atom
#undef VisualID
#undef Time
#undef Window
#undef Drawable
#undef Font
#undef Pixmap
#undef Cursor
#undef Colormap
#undef GContext
#undef KeySym
#endif

/* Some protocol gets included last, after undefines. */
#include <X11/XKBlib.h>
#include <X11/extensions/XKBproto.h>
#include "xkbstr.h"
#undef XPointer
#include <X11/extensions/XIproto.h>

#endif
