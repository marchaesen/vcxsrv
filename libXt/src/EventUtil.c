/*

Copyright (c) 1993, Oracle and/or its affiliates.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/
/********************************************************

Copyright 1988 by Hewlett-Packard Company
Copyright 1987, 1988, 1989 by Digital Equipment Corporation, Maynard, Massachusetts

Permission to use, copy, modify, and distribute this software
and its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that copyright notice and this permission
notice appear in supporting documentation, and that the names of
Hewlett-Packard or Digital not be used in advertising or
publicity pertaining to distribution of the software without specific,
written prior permission.

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

********************************************************/

/*

Copyright 1987, 1988, 1989, 1998  The Open Group

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "IntrinsicI.h"
#include "PassivGraI.h"
#include "StringDefs.h"
#include "EventI.h"

static XContext perWidgetInputContext = 0;

void
_XtFreePerWidgetInput(Widget w, XtPerWidgetInput pwi)
{
    LOCK_PROCESS;
    XDeleteContext(XtDisplay(w), (Window) w, perWidgetInputContext);

    XtFree((char *) pwi);
    UNLOCK_PROCESS;
}

/*
 * This routine gets the passive list associated with the widget
 * from the context manager.
 */
XtPerWidgetInput
_XtGetPerWidgetInput(Widget widget, _XtBoolean create)
{
    XtPerWidgetInput pwi = NULL;
    Display *dpy = widget->core.screen->display;

    LOCK_PROCESS;
    if (!perWidgetInputContext)
        perWidgetInputContext = XUniqueContext();

    if (XFindContext(dpy,
                     (Window) widget,
                     perWidgetInputContext, (XPointer *) &pwi) && create) {
        pwi = (XtPerWidgetInput)
            __XtMalloc((unsigned) sizeof(XtPerWidgetInputRec));

        pwi->focusKid = NULL;
        pwi->queryEventDescendant = NULL;
        pwi->focalPoint = XtUnrelated;
        pwi->keyList = pwi->ptrList = NULL;

        pwi->haveFocus =
            pwi->map_handler_added =
            pwi->realize_handler_added = pwi->active_handler_added = FALSE;

        XtAddCallback(widget, XtNdestroyCallback,
                      _XtDestroyServerGrabs, (XtPointer) pwi);

        (void) XSaveContext(dpy,
                            (Window) widget,
                            perWidgetInputContext, (char *) pwi);
    }
    UNLOCK_PROCESS;
    return pwi;
}

void
_XtFillAncestorList(Widget **listPtr,
                    int *maxElemsPtr,
                    int *numElemsPtr,
                    Widget start,
                    Widget breakWidget)
{
#define CACHESIZE 16
    Cardinal i;
    Widget w;
    Widget *trace = *listPtr;

    /* First time in, allocate the ancestor list */
    if (trace == NULL) {
        trace = XtMallocArray(CACHESIZE, (Cardinal) sizeof(Widget));
        *maxElemsPtr = CACHESIZE;
    }
    /* First fill in the ancestor list */

    trace[0] = start;

    for (i = 1, w = XtParent(start);
         w != NULL && !XtIsShell(trace[i - 1]) && trace[i - 1] != breakWidget;
         w = XtParent(w), i++) {
        if (i == (Cardinal) *maxElemsPtr) {
            /* This should rarely happen, but if it does it'll probably
               happen again, so grow the ancestor list */
            *maxElemsPtr += CACHESIZE;
            trace = XtReallocArray(trace, (Cardinal) *maxElemsPtr,
                                   (Cardinal) sizeof(Widget));
        }
        trace[i] = w;
    }
    *listPtr = trace;
    *numElemsPtr = (int) i;
#undef CACHESIZE
}

Widget
_XtFindRemapWidget(XEvent *event,
                   Widget widget,
                   EventMask mask,
                   XtPerDisplayInput pdi)
{
    Widget dspWidget = widget;

    if (!pdi->traceDepth || !(widget == pdi->trace[0])) {
        _XtFillAncestorList(&pdi->trace, &pdi->traceMax,
                            &pdi->traceDepth, widget, NULL);
        pdi->focusWidget = NULL;        /* invalidate the focus
                                           cache */
    }
    if (mask & (KeyPressMask | KeyReleaseMask))
        dspWidget = _XtProcessKeyboardEvent((XKeyEvent *) event, widget, pdi);
    else if (mask & (ButtonPressMask | ButtonReleaseMask))
        dspWidget = _XtProcessPointerEvent((XButtonEvent *) event, widget, pdi);

    return dspWidget;
}

void
_XtUngrabBadGrabs(XEvent *event,
                  Widget widget,
                  EventMask mask,
                  XtPerDisplayInput pdi)
{
    XKeyEvent *ke = (XKeyEvent *) event;

    if (mask & (KeyPressMask | KeyReleaseMask)) {
        if (IsServerGrab(pdi->keyboard.grabType) &&
            !_XtOnGrabList(pdi->keyboard.grab.widget, pdi->grabList))
            XtUngrabKeyboard(widget, ke->time);

    }
    else {
        if (IsServerGrab(pdi->pointer.grabType) &&
            !_XtOnGrabList(pdi->pointer.grab.widget, pdi->grabList))
             XtUngrabPointer(widget, ke->time);
    }
}
