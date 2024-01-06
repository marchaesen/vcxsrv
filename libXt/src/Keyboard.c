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

Copyright 1987, 1988, 1994, 1998  The Open Group

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
#include "EventI.h"

#define _GetWindowedAncestor(w) (XtIsWidget(w) ? w : _XtWindowedAncestor(w))

/* InActiveSubtree cache of the current focus source and its ancestors */
static Widget *pathTrace = NULL;
static int pathTraceDepth = 0;
static int pathTraceMax = 0;

/* FindKeyDestination cache of focus destination and ancestors up to source */
static Widget *pseudoTrace = NULL;
static int pseudoTraceDepth = 0;
static int pseudoTraceMax = 0;

void
_XtClearAncestorCache(Widget widget)
{
    /* the caller must lock the process lock */
    if (pathTraceDepth && pathTrace[0] == widget)
        pathTraceDepth = 0;
}

static XtServerGrabPtr
CheckServerGrabs(XEvent *event, Widget *trace, Cardinal traceDepth)
{
    Cardinal i;

    for (i = traceDepth; i > 0; i--) {
        XtServerGrabPtr grab;

        if ((grab = _XtCheckServerGrabsOnWidget(event, trace[i - 1], KEYBOARD)))
            return (grab);
    }
    return (XtServerGrabPtr) 0;
}

static Boolean
IsParent(Widget a, Widget b)
{
    for (b = XtParent(b); b; b = XtParent(b)) {
        if (b == a)
            return TRUE;
        if (XtIsShell(b))
            return FALSE;
    }
    return FALSE;
}

#define RelRtn(lca, type) {*relTypeRtn = type; return lca;}

static Widget
CommonAncestor(register Widget a, register Widget b, XtGeneology *relTypeRtn)
{
    if (a == b) {
        RelRtn(a, XtMySelf)
    }
    else if (IsParent(a, b)) {
        RelRtn(a, XtMyAncestor)
    }
    else if (IsParent(b, a)) {
        RelRtn(b, XtMyDescendant)
    }
    else
        for (b = XtParent(b); b && !XtIsShell(b); b = XtParent(b))
            if (IsParent(b, a)) {
                RelRtn(b, XtMyCousin)
            }
    RelRtn(NULL, XtUnrelated)
}

#undef RelRtn

static Widget
_FindFocusWidget(Widget widget,
                 Widget *trace,
                 int traceDepth,
                 Boolean activeCheck,
                 Boolean *isTarget)
{
    int src;
    Widget dst;
    XtPerWidgetInput pwi = NULL;

    /* For each ancestor, starting at the top, see if it's forwarded */

    /* first check the trace list till done or we go to branch */
    for (src = traceDepth - 1, dst = widget; src > 0;) {
        if ((pwi = _XtGetPerWidgetInput(trace[src], FALSE))) {
            if (pwi->focusKid) {
                dst = pwi->focusKid;
                for (src--; src > 0 && trace[src] != dst; src--) {
                }
            }
            else
                dst = trace[--src];
        }
        else
            dst = trace[--src];
    }

    if (isTarget) {
        if (pwi && pwi->focusKid == widget)
            *isTarget = TRUE;
        else
            *isTarget = FALSE;
    }

    if (!activeCheck)
        while (XtIsWidget(dst)
               && (pwi = _XtGetPerWidgetInput(dst, FALSE))
               && pwi->focusKid)
            dst = pwi->focusKid;

    return dst;
}

static Widget
FindFocusWidget(Widget widget, XtPerDisplayInput pdi)
{
    if (pdi->focusWidget)
        return pdi->focusWidget;
    else
        return _FindFocusWidget(widget, pdi->trace, pdi->traceDepth, FALSE,
                                NULL);
}

Widget
XtGetKeyboardFocusWidget(Widget widget)
{
    XtPerDisplayInput pdi;
    Widget retval;

    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    pdi = _XtGetPerDisplayInput(XtDisplay(widget));
    retval = FindFocusWidget(widget, pdi);
    UNLOCK_APP(app);
    return retval;
}

static Boolean
IsOutside(XKeyEvent *e, Widget w)
{
    Position left, right, top, bottom;

    /*
     * if the pointer is outside the shell or inside
     * the window try to see if it would receive the
     * focus
     */
    XtTranslateCoords(w, 0, 0, &left, &top);
    /* We need to take borders into consideration */
    left = (Position) (left - w->core.border_width);
    top = (Position) (top - w->core.border_width);
    right = (Position) (left + w->core.width + w->core.border_width);
    bottom = (Position) (top + w->core.height + w->core.border_width);

    if ((e->x_root < left) || (e->y_root < top) ||
        (e->x_root > right) || (e->y_root > bottom))
        return TRUE;
    else
        return FALSE;
}

static Widget
FindKeyDestination(Widget widget,
                   XKeyEvent *event,
                   XtServerGrabPtr prevGrab,
                   XtServerGrabType prevGrabType,
                   XtServerGrabPtr devGrab,
                   XtServerGrabType devGrabType,
                   XtPerDisplayInput pdi)
{

    Widget dspWidget;
    Widget focusWidget;

    LOCK_PROCESS;
    dspWidget =
        focusWidget =
        pdi->focusWidget = _GetWindowedAncestor(FindFocusWidget(widget, pdi));

    /*
     * If a grab is active from a previous activation then dispatch
     * based on owner_events ala protocol but with focus being
     * determined by XtSetKeyboardFocus.
     */
    if (IsAnyGrab(prevGrabType)) {
        if (prevGrab->ownerEvents)
            dspWidget = focusWidget;
        else
            dspWidget = prevGrab->widget;
    }
    else {
        /*
         * If the focus widget is the event widget or a descendant
         * then we can avoid the rest of this. Else ugh...
         */
        if (focusWidget != widget) {
            XtGeneology ewRelFw;        /* relationship of event widget to
                                           focusWidget */
            Widget lca;

            lca = CommonAncestor(widget, focusWidget, &ewRelFw);

            /*
             * if the event widget is an ancestor of focus due to the pointer
             * and/or the grab being in an ancestor and it's a passive grab
             * send to grab widget.
             * we are also dispatching to widget if ownerEvents and the event
             * is outside the client
             */
            if ((ewRelFw == XtMyAncestor) &&
                (devGrabType == XtPassiveServerGrab)) {
                if (IsOutside(event, widget) || event->type == KeyPress)
                    dspWidget = devGrab->widget;
            }
            else {
                /*
                 * if the grab widget is not an ancestor of the focus
                 * release the grab in order to avoid locking. There
                 * is a possible case  in that ownerEvents true will fall
                 * through and if synch is set and the event widget
                 * could turn it off we'll lock. check for it ? why not
                 */
                if ((ewRelFw != XtMyAncestor)
                    && (devGrabType == XtPassiveServerGrab)
                    && (!IsAnyGrab(prevGrabType))
                    ) {
                    XtUngrabKeyboard(devGrab->widget, event->time);
                }
                /*
                 * if there isn't a grab with then check
                 * for a logical grab that would have been activated
                 * if the server was using Xt focus instead of server
                 * focus
                 */
                if ((event->type != KeyPress) || (event->keycode == 0)  /* Xlib XIM composed input */
                    )
                    dspWidget = focusWidget;
                else {
                    XtServerGrabPtr grab;

                    if (!pseudoTraceDepth ||
                        !(focusWidget == pseudoTrace[0]) ||
                        !(lca == pseudoTrace[pseudoTraceDepth])) {
                        /*
                         * fill ancestor list from lca
                         * (non-inclusive)to focusWidget by
                         * passing in lca as breakWidget
                         */
                        _XtFillAncestorList(&pseudoTrace,
                                            &pseudoTraceMax,
                                            &pseudoTraceDepth,
                                            focusWidget, lca);
                        /* ignore lca */
                        pseudoTraceDepth--;
                    }
                    if ((grab = CheckServerGrabs((XEvent *) event,
                                                 pseudoTrace,
                                                 (Cardinal) pseudoTraceDepth)))
                    {
                        XtDevice device = &pdi->keyboard;

                        device->grabType = XtPseudoPassiveServerGrab;
                        pdi->activatingKey = (KeyCode) event->keycode;
                        device->grab = *grab;
                        dspWidget = grab->widget;
                    }
                }
            }
        }
    }
    UNLOCK_PROCESS;
    return dspWidget;
}

Widget
_XtProcessKeyboardEvent(XKeyEvent *event, Widget widget, XtPerDisplayInput pdi)
{
    XtDevice device = &pdi->keyboard;
    XtServerGrabPtr devGrab = &device->grab;
    XtServerGrabRec prevGrabRec;
    XtServerGrabType prevGrabType = device->grabType;
    Widget dspWidget = NULL;
    Boolean deactivateGrab = FALSE;

    prevGrabRec = *devGrab;

    switch (event->type) {
    case KeyPress:
    {
        XtServerGrabPtr newGrab;

        if (event->keycode != 0 &&      /* Xlib XIM composed input */
            !IsServerGrab(device->grabType) &&
            (newGrab = CheckServerGrabs((XEvent *) event,
                                        pdi->trace,
                                        (Cardinal) pdi->traceDepth))) {
            /*
             * honor pseudo-grab from prior event by X
             * unlocking keyboard. Not Xt Unlock !
             */
            if (IsPseudoGrab(prevGrabType))
                XUngrabKeyboard(XtDisplay(newGrab->widget), event->time);
            else {
                /* Activate the grab */
                device->grab = *newGrab;
                pdi->activatingKey = (KeyCode) event->keycode;
                device->grabType = XtPassiveServerGrab;
            }
        }
    }
        break;

    case KeyRelease:
    {
        if (IsEitherPassiveGrab(device->grabType) &&
            (event->keycode == pdi->activatingKey))
            deactivateGrab = TRUE;
    }
        break;
    }
    dspWidget = FindKeyDestination(widget, event,
                                   &prevGrabRec, prevGrabType,
                                   devGrab, device->grabType, pdi);
    if (deactivateGrab) {
        /* Deactivate the grab */
        device->grabType = XtNoServerGrab;
        pdi->activatingKey = 0;
    }
    return dspWidget;
}

static Widget
GetShell(Widget widget)
{
    Widget shell;

    for (shell = widget; shell && !XtIsShell(shell); shell = XtParent(shell)) {
    }
    return shell;
}

/*
 * Check that widget really has Xt focus due to it having received an
 * event
 */
typedef enum { NotActive = 0, IsActive, IsTarget } ActiveType;

static ActiveType
InActiveSubtree(Widget widget)
{
    Boolean isTarget;
    ActiveType retval;

    LOCK_PROCESS;
    if (!pathTraceDepth || widget != pathTrace[0]) {
        _XtFillAncestorList(&pathTrace,
                            &pathTraceMax, &pathTraceDepth, widget, NULL);
    }
    if (widget == _FindFocusWidget(widget,
                                   pathTrace, pathTraceDepth, TRUE, &isTarget))
        retval = (isTarget ? IsTarget : IsActive);
    else
        retval = NotActive;
    UNLOCK_PROCESS;
    return retval;
}

void
_XtHandleFocus(Widget widget,
               XtPointer client_data, /* child who wants focus */
               XEvent *event,
               Boolean *cont _X_UNUSED)
{
    XtPerDisplayInput pdi = _XtGetPerDisplayInput(XtDisplay(widget));
    XtPerWidgetInput pwi = (XtPerWidgetInput) client_data;
    XtGeneology oldFocalPoint = pwi->focalPoint;
    XtGeneology newFocalPoint = pwi->focalPoint;

    switch (event->type) {

    case KeyPress:
    case KeyRelease:
        /*
         * We're getting the keyevents used to guarantee propagating
         * child interest ala ForwardEvent in R3
         */
        return;

    case EnterNotify:
    case LeaveNotify:

        /*
         * If operating in a focus driven model, then enter and
         * leave events do not affect the keyboard focus.
         */
        if ((event->xcrossing.detail != NotifyInferior)
            && (event->xcrossing.focus)) {
            switch (oldFocalPoint) {
            case XtMyAncestor:
                if (event->type == LeaveNotify)
                    newFocalPoint = XtUnrelated;
                break;
            case XtUnrelated:
                if (event->type == EnterNotify)
                    newFocalPoint = XtMyAncestor;
                break;
            case XtMySelf:
                break;
            case XtMyDescendant:
                break;

            }
        }
        break;
    case FocusIn:
        switch (event->xfocus.detail) {
        case NotifyNonlinear:
        case NotifyAncestor:
        case NotifyInferior:
            newFocalPoint = XtMySelf;
            break;
        case NotifyNonlinearVirtual:
        case NotifyVirtual:
            newFocalPoint = XtMyDescendant;
            break;
        case NotifyPointer:
            newFocalPoint = XtMyAncestor;
            break;
        }
        break;
    case FocusOut:
        switch (event->xfocus.detail) {
        case NotifyPointer:
        case NotifyNonlinear:
        case NotifyAncestor:
        case NotifyNonlinearVirtual:
        case NotifyVirtual:
            newFocalPoint = XtUnrelated;
            break;
        case NotifyInferior:
            return;
        }
        break;
    }

    if (newFocalPoint != oldFocalPoint) {
        Boolean add;
        Widget descendant = pwi->focusKid;

        pwi->focalPoint = newFocalPoint;

        if ((oldFocalPoint == XtUnrelated) &&
            InActiveSubtree(widget) != NotActive) {
            pdi->focusWidget = NULL;    /* invalidate the cache */
            pwi->haveFocus = TRUE;
            add = TRUE;
        }
        else if (newFocalPoint == XtUnrelated) {
            pdi->focusWidget = NULL;    /* invalidate the cache */
            pwi->haveFocus = FALSE;
            add = FALSE;
        }
        else
            return;

        if (descendant) {
            if (add) {
                _XtSendFocusEvent(descendant, FocusIn);
            }
            else {
                _XtSendFocusEvent(descendant, FocusOut);
            }
        }
    }
}

static void
AddFocusHandler(Widget widget,
                Widget descendant,
                XtPerWidgetInput pwi,
                XtPerWidgetInput psi,
                XtPerDisplayInput pdi,
                EventMask oldEventMask)
{
    EventMask eventMask, targetEventMask;
    Widget target;

    /*
     * widget must now select for key events if the descendant is
     * interested in them.
     *
     * shell borders are not occluded by the child, they're occluded
     * by reparenting window managers. !!!
     */
    target = descendant ? _GetWindowedAncestor(descendant) : NULL;
    targetEventMask = XtBuildEventMask(target);
    eventMask = targetEventMask & (KeyPressMask | KeyReleaseMask);
    eventMask |= FocusChangeMask | EnterWindowMask | LeaveWindowMask;

    if (oldEventMask) {
        oldEventMask &= KeyPressMask | KeyReleaseMask;
        oldEventMask |= FocusChangeMask | EnterWindowMask | LeaveWindowMask;

        if (oldEventMask != eventMask)
            XtRemoveEventHandler(widget, (oldEventMask & ~eventMask),
                                 False, _XtHandleFocus, (XtPointer) pwi);
    }

    if (oldEventMask != eventMask)
        XtAddEventHandler(widget, eventMask, False,
                          _XtHandleFocus, (XtPointer) pwi);

    /* What follows is too much grief to go through if the
     * target doesn't actually care about focus change events,
     * so just invalidate the focus cache & refill it when
     * the next input event actually arrives.
     */

    if (!(targetEventMask & FocusChangeMask)) {
        pdi->focusWidget = NULL;
        return;
    }

    if (XtIsRealized(widget) && !pwi->haveFocus) {
        if (psi->haveFocus) {
            Window root, child;
            int root_x, root_y, win_x, win_y;
            int left, right, top, bottom;
            unsigned int modMask;
            ActiveType act;

            /*
             * If the shell has the focus but the source widget
             * doesn't, it may only be because the source widget
             * wasn't previously tracking focus or crossing events.
             * If the target wants focus events, we have to
             * now determine whether the source has the focus.
             */

            if ((act = InActiveSubtree(widget)) == IsTarget)
                pwi->haveFocus = TRUE;
            else if (act == IsActive) {
                /*
                 * An ancestor contains the focus, so if source
                 * contains the pointer, then source has the focus.
                 */

                if (XQueryPointer(XtDisplay(widget), XtWindow(widget),
                                  &root, &child,
                                  &root_x, &root_y, &win_x, &win_y, &modMask)) {
                    /* We need to take borders into consideration */
                    left = top = -((int) widget->core.border_width);
                    right =
                        (int) (widget->core.width +
                               (widget->core.border_width << 1));
                    bottom =
                        (int) (widget->core.height +
                               (widget->core.border_width << 1));

                    if (win_x >= left && win_x < right &&
                        win_y >= top && win_y < bottom)
                        pwi->haveFocus = TRUE;
                }
            }
        }
    }
    if (pwi->haveFocus) {
        pdi->focusWidget = NULL;        /* invalidate the cache */
        _XtSendFocusEvent(target, FocusIn);
    }
}

static void
QueryEventMask(Widget widget,           /* child who gets focus */
               XtPointer client_data,   /* ancestor giving it */
               XEvent *event _X_UNUSED,
               Boolean *cont _X_UNUSED)
{
    /* widget was once the target of an XtSetKeyboardFocus but
     * was unrealized at the time.   Make sure ancestor still wants
     * focus set here then install the handler now that we know the
     * complete event mask.
     */
    Widget ancestor = (Widget) client_data;
    XtPerWidgetInput pwi = _XtGetPerWidgetInput(ancestor, FALSE);

    if (pwi) {
        Widget target = pwi->queryEventDescendant;

        /* use of 'target' is non-standard hackery;
           allows focus to non-widget */
        if (pwi->focusKid == target) {
            AddFocusHandler(ancestor, target, pwi,
                            _XtGetPerWidgetInput(GetShell(ancestor), TRUE),
                            _XtGetPerDisplayInput(XtDisplay(ancestor)),
                            (EventMask) 0);
        }
        XtRemoveEventHandler(widget, XtAllEvents, True,
                             QueryEventMask, client_data);
        pwi->map_handler_added = FALSE;
    }
}

static void
FocusDestroyCallback(Widget widget _X_UNUSED,
                     XtPointer closure, /* Widget */
                     XtPointer call_data _X_UNUSED)
{
    XtSetKeyboardFocus((Widget) closure, NULL);
}

void
XtSetKeyboardFocus(Widget widget, Widget descendant)
{
    XtPerDisplayInput pdi;
    XtPerWidgetInput pwi;
    Widget oldDesc, oldTarget, target, hookobj;

    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    LOCK_PROCESS;
    pdi = _XtGetPerDisplayInput(XtDisplay(widget));
    pwi = _XtGetPerWidgetInput(widget, TRUE);
    oldDesc = pwi->focusKid;

    if (descendant == widget)
        descendant = (Widget) None;

    target = descendant ? _GetWindowedAncestor(descendant) : NULL;
    oldTarget = oldDesc ? _GetWindowedAncestor(oldDesc) : NULL;

    if (descendant != oldDesc) {

        /* update the forward path */
        pwi->focusKid = descendant;

        /* all the rest handles focus ins and focus outs and misc gunk */

        if (oldDesc) {
            /* invalidate FindKeyDestination's ancestor list */
            if (pseudoTraceDepth && oldTarget == pseudoTrace[0])
                pseudoTraceDepth = 0;

            XtRemoveCallback(oldDesc, XtNdestroyCallback,
                             FocusDestroyCallback, (XtPointer) widget);

            if (!oldTarget->core.being_destroyed) {
                if (pwi->map_handler_added) {
                    XtRemoveEventHandler(oldTarget, XtAllEvents, True,
                                         QueryEventMask, (XtPointer) widget);
                    pwi->map_handler_added = FALSE;
                }
                if (pwi->haveFocus) {
                    _XtSendFocusEvent(oldTarget, FocusOut);
                }
            }
            else if (pwi->map_handler_added) {
                pwi->map_handler_added = FALSE;
            }

            if (pwi->haveFocus)
                pdi->focusWidget = NULL;        /* invalidate cache */

            /*
             * If there was a forward path then remove the handler if
             * the path is being set to null and it isn't a shell.
             * shells always have a handler for tracking focus for the
             * hierarchy.
             *
             * Keep the pwi record on the assumption that the client
             * will continue to dynamically assign focus for this widget.
             */
            if (!XtIsShell(widget) && !descendant) {
                XtRemoveEventHandler(widget, XtAllEvents, True,
                                     _XtHandleFocus, (XtPointer) pwi);
                pwi->haveFocus = FALSE;
            }
        }

        if (descendant) {
            Widget shell = GetShell(widget);
            XtPerWidgetInput psi = _XtGetPerWidgetInput(shell, TRUE);

            XtAddCallback(descendant, XtNdestroyCallback,
                          FocusDestroyCallback, (XtPointer) widget);

            AddFocusHandler(widget, descendant, pwi, psi, pdi,
                            oldTarget ? XtBuildEventMask(oldTarget) : 0);

            if (widget != shell)
                XtAddEventHandler(shell,
                                  FocusChangeMask | EnterWindowMask |
                                  LeaveWindowMask, False, _XtHandleFocus,
                                  (XtPointer) psi);

            if (!XtIsRealized(target)) {
                XtAddEventHandler(target, (EventMask) StructureNotifyMask,
                                  False, QueryEventMask, (XtPointer) widget);
                pwi->map_handler_added = TRUE;
                pwi->queryEventDescendant = descendant;
            }
        }
    }
    hookobj = XtHooksOfDisplay(XtDisplay(widget));
    if (XtHasCallbacks(hookobj, XtNchangeHook) == XtCallbackHasSome) {
        XtChangeHookDataRec call_data;

        call_data.type = XtHsetKeyboardFocus;
        call_data.widget = widget;
        call_data.event_data = (XtPointer) descendant;
        XtCallCallbackList(hookobj,
                           ((HookObject) hookobj)->hooks.changehook_callbacks,
                           (XtPointer) &call_data);
    }
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
}
