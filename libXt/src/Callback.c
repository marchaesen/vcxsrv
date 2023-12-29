/***********************************************************
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

Copyright 1987, 1988 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

/*

Copyright 1987, 1988, 1998  The Open Group

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

static _Xconst _XtString XtNinvalidCallbackList = "invalidCallbackList";
static _Xconst _XtString XtNxtAddCallback = "xtAddCallback";
static _Xconst _XtString XtNxtRemoveCallback = "xtRemoveCallback";
static _Xconst _XtString XtNxtRemoveAllCallback = "xtRemoveAllCallback";
static _Xconst _XtString XtNxtCallCallback = "xtCallCallback";

/* However it doesn't contain a final NULL record */
#if __STDC_VERSION__ >= 199901L
#define ToList(p) ((p)->callbacks)
#else
#define ToList(p) ((XtCallbackList) ((p)+1))
#endif

static InternalCallbackList *
FetchInternalList(Widget widget,
                  _Xconst char *name)
{
    XrmQuark quark;
    int n;
    CallbackTable offsets;
    InternalCallbackList *retval = NULL;

    quark = StringToQuark(name);
    LOCK_PROCESS;
    offsets = (CallbackTable)
        widget->core.widget_class->core_class.callback_private;

    for (n = (int) (long) *(offsets++); --n >= 0; offsets++)
        if (quark == (*offsets)->xrm_name) {
            retval = (InternalCallbackList *)
                ((char *) widget - (*offsets)->xrm_offset - 1);
            break;
        }
    UNLOCK_PROCESS;
    return retval;
}

void
_XtAddCallback(InternalCallbackList *callbacks,
               XtCallbackProc callback,
               XtPointer closure)
{
    register InternalCallbackList icl;
    register XtCallbackList cl;
    register int count;

    icl = *callbacks;
    count = icl ? icl->count : 0;

    if (icl && icl->call_state) {
        icl->call_state |= _XtCBFreeAfterCalling;
        icl = (InternalCallbackList)
            __XtMalloc((Cardinal) (sizeof(InternalCallbackRec) +
                                   sizeof(XtCallbackRec) * (size_t) (count +
                                                                     1)));
        (void) memmove((char *) ToList(icl), (char *) ToList(*callbacks),
                       sizeof(XtCallbackRec) * (size_t) count);
    }
    else {
        icl = (InternalCallbackList)
            XtRealloc((char *) icl, (Cardinal) (sizeof(InternalCallbackRec) +
                                                sizeof(XtCallbackRec) *
                                                (size_t) (count + 1)));
    }
    *callbacks = icl;
    icl->count = (unsigned short) (count + 1);
    icl->is_padded = 0;
    icl->call_state = 0;
    cl = ToList(icl) + count;
    cl->callback = callback;
    cl->closure = closure;
}                               /* _XtAddCallback */

void
_XtAddCallbackOnce(register InternalCallbackList *callbacks,
                   XtCallbackProc callback,
                   XtPointer closure)
{
    register XtCallbackList cl = ToList(*callbacks);
    register int i;

    for (i = (*callbacks)->count; --i >= 0; cl++)
        if (cl->callback == callback && cl->closure == closure)
            return;

    _XtAddCallback(callbacks, callback, closure);
}                               /* _XtAddCallbackOnce */

void
XtAddCallback(Widget widget,
              _Xconst char *name,
              XtCallbackProc callback,
              XtPointer closure)
{
    InternalCallbackList *callbacks;
    XtAppContext app = XtWidgetToApplicationContext(widget);

    LOCK_APP(app);
    callbacks = FetchInternalList(widget, name);
    if (!callbacks) {
        XtAppWarningMsg(app,
                        XtNinvalidCallbackList, XtNxtAddCallback,
                        XtCXtToolkitError,
                        "Cannot find callback list in XtAddCallback", NULL,
                        NULL);
        UNLOCK_APP(app);
        return;
    }
    _XtAddCallback(callbacks, callback, closure);
    if (!_XtIsHookObject(widget)) {
        Widget hookobj = XtHooksOfDisplay(XtDisplayOfObject(widget));

        if (XtHasCallbacks(hookobj, XtNchangeHook) == XtCallbackHasSome) {
            XtChangeHookDataRec call_data;

            call_data.type = XtHaddCallback;
            call_data.widget = widget;
            call_data.event_data = (XtPointer) name;
            XtCallCallbackList(hookobj,
                               ((HookObject) hookobj)->hooks.
                               changehook_callbacks, (XtPointer) &call_data);
        }
    }
    UNLOCK_APP(app);
}                               /* XtAddCallback */

static void
AddCallbacks(Widget widget _X_UNUSED,
             InternalCallbackList *callbacks,
             XtCallbackList newcallbacks)
{
    register InternalCallbackList icl;
    register int i, j;
    register XtCallbackList cl;

    icl = *callbacks;
    i = icl ? icl->count : 0;
    for (j = 0, cl = newcallbacks; cl->callback; cl++, j++);
    if (icl && icl->call_state) {
        icl->call_state |= _XtCBFreeAfterCalling;
        icl = (InternalCallbackList)
            __XtMalloc((Cardinal)
                       (sizeof(InternalCallbackRec) +
                        sizeof(XtCallbackRec) * (size_t) (i + j)));
        (void) memmove((char *) ToList(*callbacks), (char *) ToList(icl),
                       sizeof(XtCallbackRec) * (size_t) i);
    }
    else {
        icl = (InternalCallbackList) XtRealloc((char *) icl,
                                               (Cardinal) (sizeof
                                                           (InternalCallbackRec)
                                                           +
                                                           sizeof(XtCallbackRec)
                                                           * (size_t) (i + j)));
    }
    *callbacks = icl;
    icl->count = (unsigned short) (i + j);
    icl->is_padded = 0;
    icl->call_state = 0;
    for (cl = ToList(icl) + i; --j >= 0;)
        *cl++ = *newcallbacks++;
}                               /* AddCallbacks */

void
XtAddCallbacks(Widget widget,
               _Xconst char *name,
               XtCallbackList xtcallbacks)
{
    InternalCallbackList *callbacks;
    Widget hookobj;
    XtAppContext app = XtWidgetToApplicationContext(widget);

    LOCK_APP(app);
    callbacks = FetchInternalList(widget, name);
    if (!callbacks) {
        XtAppWarningMsg(app,
                        XtNinvalidCallbackList, XtNxtAddCallback,
                        XtCXtToolkitError,
                        "Cannot find callback list in XtAddCallbacks", NULL,
                        NULL);
        UNLOCK_APP(app);
        return;
    }
    AddCallbacks(widget, callbacks, xtcallbacks);
    hookobj = XtHooksOfDisplay(XtDisplayOfObject(widget));
    if (XtHasCallbacks(hookobj, XtNchangeHook) == XtCallbackHasSome) {
        XtChangeHookDataRec call_data;

        call_data.type = XtHaddCallbacks;
        call_data.widget = widget;
        call_data.event_data = (XtPointer) name;
        XtCallCallbackList(hookobj,
                           ((HookObject) hookobj)->hooks.changehook_callbacks,
                           (XtPointer) &call_data);
    }
    UNLOCK_APP(app);
}                               /* XtAddCallbacks */

void
_XtRemoveCallback(InternalCallbackList *callbacks,
                  XtCallbackProc callback,
                  XtPointer closure)
{
    register InternalCallbackList icl;
    register int i, j;
    register XtCallbackList cl, ncl, ocl;

    icl = *callbacks;
    if (!icl)
        return;

    cl = ToList(icl);
    for (i = icl->count; --i >= 0; cl++) {
        if (cl->callback == callback && cl->closure == closure) {
            if (icl->call_state) {
                icl->call_state |= _XtCBFreeAfterCalling;
                if (icl->count == 1) {
                    *callbacks = NULL;
                }
                else {
                    j = icl->count - i - 1;
                    ocl = ToList(icl);
                    icl = (InternalCallbackList)
                        __XtMalloc((Cardinal) (sizeof(InternalCallbackRec) +
                                               sizeof(XtCallbackRec) *
                                               (size_t) (i + j)));
                    icl->count = (unsigned short) (i + j);
                    icl->is_padded = 0;
                    icl->call_state = 0;
                    ncl = ToList(icl);
                    while (--j >= 0)
                        *ncl++ = *ocl++;
                    while (--i >= 0)
                        *ncl++ = *++cl;
                    *callbacks = icl;
                }
            }
            else {
                if (--icl->count) {
                    ncl = cl + 1;
                    while (--i >= 0)
                        *cl++ = *ncl++;
                    icl = (InternalCallbackList)
                        XtRealloc((char *) icl,
                                  (Cardinal) (sizeof(InternalCallbackRec)
                                              +
                                              sizeof(XtCallbackRec) *
                                              icl->count));
                    icl->is_padded = 0;
                    *callbacks = icl;
                }
                else {
                    XtFree((char *) icl);
                    *callbacks = NULL;
                }
            }
            return;
        }
    }
}                               /* _XtRemoveCallback */

void
XtRemoveCallback(Widget widget,
                 _Xconst char *name,
                 XtCallbackProc callback,
                 XtPointer closure)
{
    InternalCallbackList *callbacks;
    Widget hookobj;
    XtAppContext app = XtWidgetToApplicationContext(widget);

    LOCK_APP(app);
    callbacks = FetchInternalList(widget, name);
    if (!callbacks) {
        XtAppWarningMsg(app,
                        XtNinvalidCallbackList, XtNxtRemoveCallback,
                        XtCXtToolkitError,
                        "Cannot find callback list in XtRemoveCallback", NULL,
                        NULL);
        UNLOCK_APP(app);
        return;
    }
    _XtRemoveCallback(callbacks, callback, closure);
    hookobj = XtHooksOfDisplay(XtDisplayOfObject(widget));
    if (XtHasCallbacks(hookobj, XtNchangeHook) == XtCallbackHasSome) {
        XtChangeHookDataRec call_data;

        call_data.type = XtHremoveCallback;
        call_data.widget = widget;
        call_data.event_data = (XtPointer) name;
        XtCallCallbackList(hookobj,
                           ((HookObject) hookobj)->hooks.changehook_callbacks,
                           (XtPointer) &call_data);
    }
    UNLOCK_APP(app);
}                               /* XtRemoveCallback */

void
XtRemoveCallbacks(Widget widget,
                  _Xconst char *name,
                  XtCallbackList xtcallbacks)
{
    InternalCallbackList *callbacks;
    Widget hookobj;
    int i;
    InternalCallbackList icl;
    XtCallbackList cl, ccl, rcl;
    XtAppContext app = XtWidgetToApplicationContext(widget);

    LOCK_APP(app);
    callbacks = FetchInternalList(widget, name);
    if (!callbacks) {
        XtAppWarningMsg(app,
                        XtNinvalidCallbackList, XtNxtRemoveCallback,
                        XtCXtToolkitError,
                        "Cannot find callback list in XtRemoveCallbacks", NULL,
                        NULL);
        UNLOCK_APP(app);
        return;
    }

    icl = *callbacks;
    if (!icl) {
        UNLOCK_APP(app);
        return;
    }

    i = icl->count;
    cl = ToList(icl);
    if (icl->call_state) {
        icl->call_state |= _XtCBFreeAfterCalling;
        icl =
            (InternalCallbackList)
            __XtMalloc((Cardinal)
                       (sizeof(InternalCallbackRec) +
                        sizeof(XtCallbackRec) * (size_t) i));
        icl->count = (unsigned short) i;
        icl->call_state = 0;
    }
    ccl = ToList(icl);
    while (--i >= 0) {
        *ccl++ = *cl;
        for (rcl = xtcallbacks; rcl->callback; rcl++) {
            if (cl->callback == rcl->callback && cl->closure == rcl->closure) {
                ccl--;
                icl->count--;
                break;
            }
        }
        cl++;
    }
    if (icl->count) {
        icl = (InternalCallbackList)
            XtRealloc((char *) icl, (Cardinal) (sizeof(InternalCallbackRec) +
                                                sizeof(XtCallbackRec) *
                                                icl->count));
        icl->is_padded = 0;
        *callbacks = icl;
    }
    else {
        XtFree((char *) icl);
        *callbacks = NULL;
    }
    hookobj = XtHooksOfDisplay(XtDisplayOfObject(widget));
    if (XtHasCallbacks(hookobj, XtNchangeHook) == XtCallbackHasSome) {
        XtChangeHookDataRec call_data;

        call_data.type = XtHremoveCallbacks;
        call_data.widget = widget;
        call_data.event_data = (XtPointer) name;
        XtCallCallbackList(hookobj,
                           ((HookObject) hookobj)->hooks.changehook_callbacks,
                           (XtPointer) &call_data);
    }
    UNLOCK_APP(app);
}                               /* XtRemoveCallbacks */

void
_XtRemoveAllCallbacks(InternalCallbackList *callbacks)
{
    register InternalCallbackList icl = *callbacks;

    if (icl) {
        if (icl->call_state)
            icl->call_state |= _XtCBFreeAfterCalling;
        else
            XtFree((char *) icl);
        *callbacks = NULL;
    }
}                               /* _XtRemoveAllCallbacks */

void
XtRemoveAllCallbacks(Widget widget, _Xconst char *name)
{
    InternalCallbackList *callbacks;
    Widget hookobj;
    XtAppContext app = XtWidgetToApplicationContext(widget);

    LOCK_APP(app);
    callbacks = FetchInternalList(widget, name);
    if (!callbacks) {
        XtAppWarningMsg(app,
                        XtNinvalidCallbackList, XtNxtRemoveAllCallback,
                        XtCXtToolkitError,
                        "Cannot find callback list in XtRemoveAllCallbacks",
                        NULL, NULL);
        UNLOCK_APP(app);
        return;
    }
    _XtRemoveAllCallbacks(callbacks);
    hookobj = XtHooksOfDisplay(XtDisplayOfObject(widget));
    if (XtHasCallbacks(hookobj, XtNchangeHook) == XtCallbackHasSome) {
        XtChangeHookDataRec call_data;

        call_data.type = XtHremoveAllCallbacks;
        call_data.widget = widget;
        call_data.event_data = (XtPointer) name;
        XtCallCallbackList(hookobj,
                           ((HookObject) hookobj)->hooks.changehook_callbacks,
                           (XtPointer) &call_data);
    }
    UNLOCK_APP(app);
}                               /* XtRemoveAllCallbacks */

InternalCallbackList
_XtCompileCallbackList(XtCallbackList xtcallbacks)
{
    register int n;
    register XtCallbackList xtcl, cl;
    register InternalCallbackList callbacks;

    for (n = 0, xtcl = xtcallbacks; xtcl->callback; n++, xtcl++) {
    };
    if (n == 0)
        return (InternalCallbackList) NULL;

    callbacks =
        (InternalCallbackList)
        __XtMalloc((Cardinal)
                   (sizeof(InternalCallbackRec) +
                    sizeof(XtCallbackRec) * (size_t) n));
    callbacks->count = (unsigned short) n;
    callbacks->is_padded = 0;
    callbacks->call_state = 0;
    cl = ToList(callbacks);
    while (--n >= 0)
        *cl++ = *xtcallbacks++;
    return (callbacks);
}                               /* _XtCompileCallbackList */

XtCallbackList
_XtGetCallbackList(InternalCallbackList *callbacks)
{
    int i;
    InternalCallbackList icl;
    XtCallbackList cl;

    icl = *callbacks;
    if (!icl) {
        static XtCallbackRec emptyList[1] = { {NULL, NULL} };
        return (XtCallbackList) emptyList;
    }
    if (icl->is_padded)
        return ToList(icl);
    i = icl->count;
    if (icl->call_state) {
        XtCallbackList ocl;

        icl->call_state |= _XtCBFreeAfterCalling;
        ocl = ToList(icl);
        icl = (InternalCallbackList)
            __XtMalloc((Cardinal)
                       (sizeof(InternalCallbackRec) +
                        sizeof(XtCallbackRec) * (size_t) (i + 1)));
        icl->count = (unsigned short) i;
        icl->call_state = 0;
        cl = ToList(icl);
        while (--i >= 0)
            *cl++ = *ocl++;
    }
    else {
        icl = (InternalCallbackList)
            XtRealloc((char *) icl, (Cardinal) (sizeof(InternalCallbackRec)
                                                + sizeof(XtCallbackRec)
                                                * (size_t) (i + 1)));
        cl = ToList(icl) + i;
    }
    icl->is_padded = 1;
    cl->callback = (XtCallbackProc) NULL;
    cl->closure = NULL;
    *callbacks = icl;
    return ToList(icl);
}

void
XtCallCallbacks(Widget widget,
                _Xconst char *name,
                XtPointer call_data)
{
    InternalCallbackList *callbacks;
    InternalCallbackList icl;
    XtCallbackList cl;
    int i;
    char ostate;
    XtAppContext app = XtWidgetToApplicationContext(widget);

    LOCK_APP(app);
    callbacks = FetchInternalList(widget, name);
    if (!callbacks) {
        XtAppWarningMsg(app,
                        XtNinvalidCallbackList, XtNxtCallCallback,
                        XtCXtToolkitError,
                        "Cannot find callback list in XtCallCallbacks", NULL,
                        NULL);
        UNLOCK_APP(app);
        return;
    }

    icl = *callbacks;
    if (!icl) {
        UNLOCK_APP(app);
        return;
    }
    cl = ToList(icl);
    if (icl->count == 1) {
        (*cl->callback) (widget, cl->closure, call_data);
        UNLOCK_APP(app);
        return;
    }
    ostate = icl->call_state;
    icl->call_state = _XtCBCalling;
    for (i = icl->count; --i >= 0; cl++)
        (*cl->callback) (widget, cl->closure, call_data);
    if (ostate)
        icl->call_state |= ostate;
    else if (icl->call_state & _XtCBFreeAfterCalling)
        XtFree((char *) icl);
    else
        icl->call_state = ostate;
    UNLOCK_APP(app);
}                               /* XtCallCallbacks */

XtCallbackStatus
XtHasCallbacks(Widget widget,
               _Xconst char *callback_name)
{
    InternalCallbackList *callbacks;
    XtCallbackStatus retval = XtCallbackHasSome;

    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    callbacks = FetchInternalList(widget, callback_name);
    if (!callbacks)
        retval = XtCallbackNoList;
    else if (!*callbacks)
        retval = XtCallbackHasNone;
    UNLOCK_APP(app);
    return retval;
}                               /* XtHasCallbacks */

void
XtCallCallbackList(Widget widget,
                   XtCallbackList callbacks,
                   XtPointer call_data)
{
    register InternalCallbackList icl;
    register XtCallbackList cl;
    register int i;
    char ostate;

    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    if (!callbacks) {
        UNLOCK_APP(app);
        return;
    }
    icl = (InternalCallbackList) callbacks;
    cl = ToList(icl);
    if (icl->count == 1) {
        (*cl->callback) (widget, cl->closure, call_data);
        UNLOCK_APP(app);
        return;
    }
    ostate = icl->call_state;
    icl->call_state = _XtCBCalling;
    for (i = icl->count; --i >= 0; cl++)
        (*cl->callback) (widget, cl->closure, call_data);
    if (ostate)
        icl->call_state |= ostate;
    else if (icl->call_state & _XtCBFreeAfterCalling)
        XtFree((char *) icl);
    else
        icl->call_state = 0;
    UNLOCK_APP(app);
}                               /* XtCallCallbackList */

void
_XtPeekCallback(Widget widget _X_UNUSED,
                XtCallbackList callbacks,
                XtCallbackProc *callback,
                XtPointer *closure)
{
    register InternalCallbackList icl = (InternalCallbackList) callbacks;
    register XtCallbackList cl;

    if (!callbacks) {
        *callback = (XtCallbackProc) NULL;
        return;
    }
    cl = ToList(icl);
    *callback = cl->callback;
    *closure = cl->closure;
    return;
}

void
_XtCallConditionalCallbackList(Widget widget,
                               XtCallbackList callbacks,
                               XtPointer call_data,
                               _XtConditionProc cond_proc)
{
    register InternalCallbackList icl;
    register XtCallbackList cl;
    register int i;
    char ostate;

    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    if (!callbacks) {
        UNLOCK_APP(app);
        return;
    }
    icl = (InternalCallbackList) callbacks;
    cl = ToList(icl);
    if (icl->count == 1) {
        (*cl->callback) (widget, cl->closure, call_data);
        (void) (*cond_proc) (call_data);
        UNLOCK_APP(app);
        return;
    }
    ostate = icl->call_state;
    icl->call_state = _XtCBCalling;
    for (i = icl->count; --i >= 0; cl++) {
        (*cl->callback) (widget, cl->closure, call_data);
        if (!(*cond_proc) (call_data))
            break;
    }
    if (ostate)
        icl->call_state |= ostate;
    else if (icl->call_state & _XtCBFreeAfterCalling)
        XtFree((char *) icl);
    else
        icl->call_state = 0;
    UNLOCK_APP(app);
}
