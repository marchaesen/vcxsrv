/*

Copyright 1987, 1998  The Open Group
Copyright 2002 Red Hat Inc., Durham, North Carolina.

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

*/

/*
 * Authors:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 * This file was originally taken from xc/lib/Xaw/Template.h
 */

#ifndef _Canvas_h
#define _Canvas_h

#include <X11/Intrinsic.h>

#define XtNcanvasExposeCallback "canvasExposeCallback"
#define XtCcanvasExposeCallback "CanvasExposeCallback"
#define XtNcanvasResizeCallback "canvasResizeCallback"
#define XtCcanvasResizeCallback "CanvasResizeCallback"

typedef struct _CanvasClassRec *CanvasWidgetClass;
typedef struct _CanvasRec *CanvasWidget;
extern WidgetClass canvasWidgetClass;

typedef struct _CanvasExposeDataRec {
    Widget w;
    XEvent *event;
    Region region;
} CanvasExposeDataRec, *CanvasExposeDataPtr;

#endif                          /* _Canvas_h */
