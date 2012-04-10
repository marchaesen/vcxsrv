/***********************************************************
Copyright (c) 1993, Oracle and/or its affiliates. All rights reserved.

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
#include "Shell.h"
#include "StringDefs.h"

typedef struct _XtEventRecExt {
    int type;
    XtPointer select_data[1]; /* actual dimension is [mask] */
} XtEventRecExt;

#define EXT_TYPE(p) (((XtEventRecExt*) ((p)+1))->type)
#define EXT_SELECT_DATA(p,n) (((XtEventRecExt*) ((p)+1))->select_data[n])

#define NonMaskableMask ((EventMask)0x80000000L)

/*
 * These are definitions to make the code that handles exposure compresssion
 * easier to read.
 *
 * COMP_EXPOSE      - The compression exposure field of "widget"
 * COMP_EXPOSE_TYPE - The type of compression (lower 4 bits of COMP_EXPOSE.
 * GRAPHICS_EXPOSE  - TRUE if the widget wants graphics expose events
 *                    dispatched.
 * NO_EXPOSE        - TRUE if the widget wants No expose events dispatched.
 */

#define COMP_EXPOSE   (widget->core.widget_class->core_class.compress_exposure)
#define COMP_EXPOSE_TYPE (COMP_EXPOSE & 0x0f)
#define GRAPHICS_EXPOSE  ((XtExposeGraphicsExpose & COMP_EXPOSE) || \
			  (XtExposeGraphicsExposeMerged & COMP_EXPOSE))
#define NO_EXPOSE        (XtExposeNoExpose & COMP_EXPOSE)

EventMask XtBuildEventMask(
    Widget widget)
{
    XtEventTable ev;
    EventMask	mask = 0L;
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    for (ev = widget->core.event_table; ev != NULL; ev = ev->next)
	if (ev->select) {
	    if (!ev->has_type_specifier)
		mask |= ev->mask;
	    else {
		if (EXT_TYPE(ev) < LASTEvent) {
		    Cardinal i;
		    for (i = 0; i < ev->mask; i++)
			if (EXT_SELECT_DATA(ev, i))
			    mask |= *(EventMask*)EXT_SELECT_DATA(ev, i);
		}
	    }
	}
    LOCK_PROCESS;
    if (widget->core.widget_class->core_class.expose != NULL)
	mask |= ExposureMask;
    if (widget->core.widget_class->core_class.visible_interest)
	mask |= VisibilityChangeMask;
    UNLOCK_PROCESS;
    if (widget->core.tm.translations)
	mask |= widget->core.tm.translations->eventMask;

    mask =  mask & ~NonMaskableMask;
    UNLOCK_APP(app);
    return mask;
}

static void CallExtensionSelector(
    Widget widget,
    ExtSelectRec* rec,
    Boolean forceCall)
{
    XtEventRec* p;
    XtPointer* data;
    int* types;
    Cardinal i, count = 0;

    for (p = widget->core.event_table; p != NULL; p = p->next)
	if (p->has_type_specifier &&
	    EXT_TYPE(p) >= rec->min && EXT_TYPE(p) <= rec->max)
	    count += p->mask;

    if (count == 0 && !forceCall) return;

    data = (XtPointer *) ALLOCATE_LOCAL(count * sizeof (XtPointer));
    types = (int *) ALLOCATE_LOCAL(count * sizeof (int));
    count = 0;

    for (p = widget->core.event_table; p != NULL; p = p->next)
	if (p->has_type_specifier &&
	    EXT_TYPE(p) >= rec->min && EXT_TYPE(p) <= rec->max)
	    for (i =0; i < p->mask; i++) {
		types[count] = EXT_TYPE(p);
		data[count++] = EXT_SELECT_DATA(p, i);
	    }

    (*rec->proc)(widget, types, data, count, rec->client_data);
    DEALLOCATE_LOCAL((char*) types);
    DEALLOCATE_LOCAL((char*) data);
}

static void
RemoveEventHandler(
    Widget widget,
    XtPointer select_data,
    int type,
    Boolean has_type_specifier,
    Boolean other,
    XtEventHandler proc,
    XtPointer closure,
    Boolean raw)
{
    XtEventRec *p, **pp;
    EventMask eventMask, oldMask = XtBuildEventMask(widget);

    if (raw) raw = 1;
    pp = &widget->core.event_table;
    while ((p = *pp) &&
	   (p->proc != proc || p->closure != closure || p->select == raw ||
	    has_type_specifier != p->has_type_specifier ||
	    (has_type_specifier && EXT_TYPE(p) != type)))
	pp = &p->next;
    if (!p) return;

    /* un-register it */
    if (!has_type_specifier) {
	eventMask = *(EventMask*)select_data;
	eventMask &= ~NonMaskableMask;
	if (other)
	    eventMask |= NonMaskableMask;
	p->mask &= ~eventMask;
    } else {
	Cardinal i;
	/* p->mask specifies count of EXT_SELECT_DATA(p,i)
	 * search through the list of selection data, if not found
	 * dont remove this handler
	 */
	for (i = 0; i < p->mask && select_data != EXT_SELECT_DATA(p,i);) i++;
	if (i == p->mask) return;
	if (p->mask == 1) p->mask = 0;
	else {
	    p->mask--;
	    while (i < p->mask) {
		EXT_SELECT_DATA(p,i) = EXT_SELECT_DATA(p, i+1);
		i++;
	    }
	}
    }

    if (!p->mask) {        /* delete it entirely */
        *pp = p->next;
	XtFree((char *)p);
    }

    /* Reset select mask if realized and not raw. */
    if ( !raw && XtIsRealized(widget) && !widget->core.being_destroyed) {
	EventMask mask = XtBuildEventMask(widget);
	Display* dpy = XtDisplay (widget);

	if (oldMask != mask)
	    XSelectInput(dpy, XtWindow(widget), mask);

	if (has_type_specifier) {
	    XtPerDisplay pd = _XtGetPerDisplay(dpy);
	    int i;
	    for (i = 0; i < pd->ext_select_count; i++) {
		if (type >= pd->ext_select_list[i].min &&
		    type <= pd->ext_select_list[i].max) {
		    CallExtensionSelector(widget, pd->ext_select_list+i, TRUE);
		    break;
		}
		if (type < pd->ext_select_list[i].min) break;
	    }
	}
    }
}

/*	Function Name: AddEventHandler
 *	Description: An Internal routine that does the actual work of
 *                   adding the event handlers.
 *	Arguments: widget - widget to register an event handler for.
 *                 eventMask - events to mask for.
 *                 other - pass non maskable events to this proceedure.
 *                 proc - proceedure to register.
 *                 closure - data to pass to the event hander.
 *                 position - where to add this event handler.
 *                 force_new_position - If the element is already in the
 *                                      list, this will force it to the
 *                                      beginning or end depending on position.
 *                 raw - If FALSE call XSelectInput for events in mask.
 *	Returns: none
 */

static void
AddEventHandler(
    Widget widget,
    XtPointer select_data,
    int type,
    Boolean         has_type_specifier,
    Boolean         other,
    XtEventHandler  proc,
    XtPointer	closure,
    XtListPosition  position,
    Boolean         force_new_position,
    Boolean         raw)
{
    register XtEventRec *p, **pp;
    EventMask oldMask = 0, eventMask = 0;

    if (!has_type_specifier) {
	eventMask = *(EventMask*)select_data & ~NonMaskableMask;
	if (other) eventMask |= NonMaskableMask;
	if (!eventMask) return;
    } else if (!type) return;

    if (XtIsRealized(widget) && !raw) oldMask = XtBuildEventMask(widget);

    if (raw) raw = 1;
    pp = &widget->core.event_table;
    while ((p = *pp) &&
	   (p->proc != proc || p->closure != closure || p->select == raw ||
	    has_type_specifier != p->has_type_specifier ||
	    (has_type_specifier && EXT_TYPE(p) != type)))
	pp = &p->next;

    if (!p) {		                /* New proc to add to list */
	if (has_type_specifier) {
	    p = (XtEventRec*) __XtMalloc(sizeof(XtEventRec) +
				       sizeof(XtEventRecExt));
	    EXT_TYPE(p) = type;
	    EXT_SELECT_DATA(p,0) = select_data;
	    p->mask = 1;
	    p->has_type_specifier = True;
	} else {
	    p = (XtEventRec*) __XtMalloc(sizeof(XtEventRec));
	    p->mask = eventMask;
	    p->has_type_specifier = False;
	}
	p->proc = proc;
	p->closure = closure;
	p->select = ! raw;

	if (position == XtListHead) {
	    p->next = widget->core.event_table;
	    widget->core.event_table = p;
	    pp = &widget->core.event_table;
	} else {
	    *pp = p;
	    p->next = NULL;
	}
    }
    else {
	if (force_new_position) {
	    *pp = p->next;

	    if (position == XtListHead) {
		p->next = widget->core.event_table;
		widget->core.event_table = p;
	    } else {
	       	/*
		 * Find the last element in the list.
		 */
		while (*pp)
		    pp = &(*pp)->next;
		*pp = p;
		p->next = NULL;
	    }
	}

	if (!has_type_specifier)
	    p->mask |= eventMask;
	else {
	    Cardinal i;
	    /* p->mask specifies count of EXT_SELECT_DATA(p,i) */
	    for (i = 0; i < p->mask && select_data != EXT_SELECT_DATA(p,i); )
		i++;
	    if (i == p->mask) {
		p = (XtEventRec*) XtRealloc((char*)p,
					    sizeof(XtEventRec) +
					    sizeof(XtEventRecExt) +
					    p->mask * sizeof(XtPointer));
		EXT_SELECT_DATA(p,i) = select_data;
		p->mask++;
		*pp = p;
	    }
	}
    }

    if (XtIsRealized(widget) && !raw) {
	EventMask mask = XtBuildEventMask(widget);
	Display* dpy = XtDisplay (widget);

	if (oldMask != mask)
	    XSelectInput(dpy, XtWindow(widget), mask);

	if (has_type_specifier) {
	    XtPerDisplay pd = _XtGetPerDisplay (dpy);
	    int i;
	    for (i = 0; i < pd->ext_select_count; i++) {
		if (type >= pd->ext_select_list[i].min &&
		    type <= pd->ext_select_list[i].max) {
		    CallExtensionSelector(widget, pd->ext_select_list+i, FALSE);
		    break;
		}
		if (type < pd->ext_select_list[i].min) break;
	    }
	}
    }
}

void XtRemoveEventHandler(
    Widget	    widget,
    EventMask       eventMask,
    _XtBoolean	    other,
    XtEventHandler  proc,
    XtPointer	    closure)
{
    WIDGET_TO_APPCON(widget);
    LOCK_APP(app);
    RemoveEventHandler(widget, (XtPointer) &eventMask, 0, FALSE,
		       other, proc, closure, FALSE);
    UNLOCK_APP(app);
}

void XtAddEventHandler(
    Widget	    widget,
    EventMask       eventMask,
    _XtBoolean      other,
    XtEventHandler  proc,
    XtPointer	    closure)
{
    WIDGET_TO_APPCON(widget);
    LOCK_APP(app);
    AddEventHandler(widget, (XtPointer) &eventMask, 0, FALSE, other,
		    proc, closure, XtListTail, FALSE, FALSE);
    UNLOCK_APP(app);
}

void XtInsertEventHandler(
    Widget	    widget,
    EventMask       eventMask,
    _XtBoolean      other,
    XtEventHandler  proc,
    XtPointer	    closure,
    XtListPosition  position)
{
    WIDGET_TO_APPCON(widget);
    LOCK_APP(app);
    AddEventHandler(widget, (XtPointer) &eventMask, 0, FALSE, other,
		    proc, closure, position, TRUE, FALSE);
    UNLOCK_APP(app);
}

void XtRemoveRawEventHandler(
    Widget	    widget,
    EventMask       eventMask,
    _XtBoolean	    other,
    XtEventHandler  proc,
    XtPointer	    closure)
{
    WIDGET_TO_APPCON(widget);
    LOCK_APP(app);
    RemoveEventHandler(widget, (XtPointer) &eventMask, 0, FALSE,
		       other, proc, closure, TRUE);
    UNLOCK_APP(app);
}

void XtInsertRawEventHandler(
    Widget	    widget,
    EventMask       eventMask,
    _XtBoolean	    other,
    XtEventHandler  proc,
    XtPointer	    closure,
    XtListPosition  position)
{
    WIDGET_TO_APPCON(widget);
    LOCK_APP(app);
    AddEventHandler(widget, (XtPointer) &eventMask, 0, FALSE, other,
		    proc, closure, position, TRUE, TRUE);
    UNLOCK_APP(app);
}

void XtAddRawEventHandler(
    Widget	    widget,
    EventMask       eventMask,
    _XtBoolean      other,
    XtEventHandler  proc,
    XtPointer	    closure)
{
    WIDGET_TO_APPCON(widget);
    LOCK_APP(app);
    AddEventHandler(widget, (XtPointer) &eventMask, 0, FALSE, other,
		    proc, closure, XtListTail, FALSE, TRUE);
    UNLOCK_APP(app);
}

void XtRemoveEventTypeHandler(
    Widget	    widget,
    int		    type,
    XtPointer	    select_data,
    XtEventHandler  proc,
    XtPointer	    closure)
{
    WIDGET_TO_APPCON(widget);
    LOCK_APP(app);
    RemoveEventHandler(widget, select_data, type, TRUE,
		       FALSE, proc, closure, FALSE);
    UNLOCK_APP(app);
}

void XtInsertEventTypeHandler(
    Widget	    widget,
    int		    type,
    XtPointer	    select_data,
    XtEventHandler  proc,
    XtPointer	    closure,
    XtListPosition  position)
{
    WIDGET_TO_APPCON(widget);
    LOCK_APP(app);
    AddEventHandler(widget, select_data, type, TRUE, FALSE,
		    proc, closure, position, TRUE, FALSE);
    UNLOCK_APP(app);
}

typedef struct _WWPair {
    struct _WWPair *next;
    Window window;
    Widget widget;
} *WWPair;

typedef struct _WWTable {
    unsigned int mask;		/* size of hash table - 1 */
    unsigned int rehash;	/* mask - 2 */
    unsigned int occupied;	/* number of occupied entries */
    unsigned int fakes;		/* number occupied by WWfake */
    Widget *entries;		/* the entries */
    WWPair pairs;		/* bogus entries */
} *WWTable;

static const WidgetRec WWfake;	/* placeholder for deletions */

#define WWHASH(tab,win) ((win) & tab->mask)
#define WWREHASHVAL(tab,win) ((((win) % tab->rehash) + 2) | 1)
#define WWREHASH(tab,idx,rehash) ((idx + rehash) & tab->mask)
#define WWTABLE(display) (_XtGetPerDisplay(display)->WWtable)

static void ExpandWWTable(WWTable);

void XtRegisterDrawable(
    Display* display,
    Drawable drawable,
    Widget widget)
{
    WWTable tab;
    int idx, rehash;
    Widget entry;
    Window window = (Window) drawable;
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    LOCK_PROCESS;
    tab = WWTABLE(display);
    if (window != XtWindow(widget)) {
	WWPair pair;
	pair = XtNew(struct _WWPair);
	pair->next = tab->pairs;
	pair->window = window;
	pair->widget = widget;
	tab->pairs = pair;
	UNLOCK_PROCESS;
	UNLOCK_APP(app);
	return;
    }
    if ((tab->occupied + (tab->occupied >> 2)) > tab->mask)
	ExpandWWTable(tab);

    idx = WWHASH(tab, window);
    if ((entry = tab->entries[idx]) && entry != &WWfake) {
	rehash = WWREHASHVAL(tab, window);
	do {
	    idx = WWREHASH(tab, idx, rehash);
	} while ((entry = tab->entries[idx]) && entry != &WWfake);
    }
    if (!entry)
	tab->occupied++;
    else if (entry == &WWfake)
	tab->fakes--;
    tab->entries[idx] = widget;
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
}

void XtUnregisterDrawable(
    Display* display,
    Drawable drawable)
{
    WWTable tab;
    int idx, rehash;
    Widget entry;
    Window window = (Window) drawable;
    Widget widget = XtWindowToWidget (display, window);
    DPY_TO_APPCON(display);

    if (widget == NULL) return;

    LOCK_APP(app);
    LOCK_PROCESS;
    tab = WWTABLE(display);
    if (window != XtWindow(widget)) {
	WWPair *prev, pair;

	prev = &tab->pairs;
	while ((pair = *prev) && pair->window != window)
	    prev = &pair->next;
	if (pair) {
	    *prev = pair->next;
	    XtFree((char *)pair);
	}
	UNLOCK_PROCESS;
	UNLOCK_APP(app);
	return;
    }
    idx = WWHASH(tab, window);
    if ((entry = tab->entries[idx])) {
	if (entry != widget) {
	    rehash = WWREHASHVAL(tab, window);
	    do {
		idx = WWREHASH(tab, idx, rehash);
		if (!(entry = tab->entries[idx])) {
		    UNLOCK_PROCESS;
		    UNLOCK_APP(app);
		    return;
		}
	    } while (entry != widget);
	}
	tab->entries[idx] = (Widget)&WWfake;
	tab->fakes++;
    }
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
}

static void ExpandWWTable(
    register WWTable tab)
{
    unsigned int oldmask;
    register Widget *oldentries, *entries;
    register Cardinal oldidx, newidx, rehash;
    register Widget entry;

    LOCK_PROCESS;
    oldmask = tab->mask;
    oldentries = tab->entries;
    tab->occupied -= tab->fakes;
    tab->fakes = 0;
    if ((tab->occupied + (tab->occupied >> 2)) > tab->mask) {
	tab->mask = (tab->mask << 1) + 1;
	tab->rehash = tab->mask - 2;
    }
    entries = tab->entries = (Widget *) __XtCalloc(tab->mask+1, sizeof(Widget));
    for (oldidx = 0; oldidx <= oldmask; oldidx++) {
	if ((entry = oldentries[oldidx]) && entry != &WWfake) {
	    newidx = WWHASH(tab, XtWindow(entry));
	    if (entries[newidx]) {
		rehash = WWREHASHVAL(tab, XtWindow(entry));
		do {
		    newidx = WWREHASH(tab, newidx, rehash);
		} while (entries[newidx]);
	    }
	    entries[newidx] = entry;
	}
    }
    XtFree((char *)oldentries);
    UNLOCK_PROCESS;
}

Widget XtWindowToWidget(
    register Display *display,
    register Window window)
{
    register WWTable tab;
    register int idx, rehash;
    register Widget entry;
    WWPair pair;
    DPY_TO_APPCON(display);

    if (!window) return NULL;

    LOCK_APP(app);
    LOCK_PROCESS;
    tab = WWTABLE(display);
    idx = WWHASH(tab, window);
    if ((entry = tab->entries[idx]) && XtWindow(entry) != window) {
	rehash = WWREHASHVAL(tab, window);
	do {
	    idx = WWREHASH(tab, idx, rehash);
	} while ((entry = tab->entries[idx]) && XtWindow(entry) != window);
    }
    if (entry) {
    	UNLOCK_PROCESS;
    	UNLOCK_APP(app);
	return entry;
    }
    for (pair = tab->pairs; pair; pair = pair->next) {
	if (pair->window == window) {
	    entry = pair->widget;
	    UNLOCK_PROCESS;
    	    UNLOCK_APP(app);
	    return entry;
	}
    }
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
    return NULL;
}

void _XtAllocWWTable(
    XtPerDisplay pd)
{
    register WWTable tab;

    tab = (WWTable) __XtMalloc(sizeof(struct _WWTable));
    tab->mask = 0x7f;
    tab->rehash = tab->mask - 2;
    tab->entries = (Widget *) __XtCalloc(tab->mask+1, sizeof(Widget));
    tab->occupied = 0;
    tab->fakes = 0;
    tab->pairs = NULL;
    pd->WWtable = tab;
}

void _XtFreeWWTable(
    register XtPerDisplay pd)
{
    register WWPair pair, next;

    for (pair = pd->WWtable->pairs; pair; pair = next) {
	next = pair->next;
	XtFree((char *)pair);
    }
    XtFree((char *)pd->WWtable->entries);
    XtFree((char *)pd->WWtable);
}

#define EHMAXSIZE 25 /* do not make whopping big */

static Boolean CallEventHandlers(
    Widget     widget,
    XEvent    *event,
    EventMask  mask)
{
    register XtEventRec *p;
    XtEventHandler *proc;
    XtPointer *closure;
    XtEventHandler procs[EHMAXSIZE];
    XtPointer closures[EHMAXSIZE];
    Boolean cont_to_disp = True;
    int i, numprocs;

    /* Have to copy the procs into an array, because one of them might
     * call XtRemoveEventHandler, which would break our linked list. */

    numprocs = 0;
    for (p=widget->core.event_table; p; p = p->next) {
	if ((!p->has_type_specifier && (mask & p->mask)) ||
	    (p->has_type_specifier && event->type == EXT_TYPE(p)))
	    numprocs++;
    }
    if (numprocs > EHMAXSIZE) {
	proc = (XtEventHandler *)__XtMalloc(numprocs * (sizeof(XtEventHandler) +
						      sizeof(XtPointer)));
	closure = (XtPointer *)(proc + numprocs);
    } else {
	proc = procs;
	closure = closures;
    }
    numprocs = 0;
    for (p=widget->core.event_table; p; p = p->next) {
	if ((!p->has_type_specifier && (mask & p->mask)) ||
	    (p->has_type_specifier && event->type == EXT_TYPE(p))) {
	    proc[numprocs] = p->proc;
	    closure[numprocs] = p->closure;
	    numprocs++;
	}
    }
/* FUNCTIONS CALLED THROUGH POINTER proc:
		Selection.c:ReqCleanup,
		"Shell.c":EventHandler,
		PassivGrab.c:ActiveHandler,
		PassivGrab.c:RealizeHandler,
		Keyboard.c:QueryEventMask,
		_XtHandleFocus,
		Selection.c:HandleSelectionReplies,
		Selection.c:HandleGetIncrement,
		Selection.c:HandleIncremental,
		Selection.c:HandlePropertyGone,
		Selection.c:HandleSelectionEvents
		*/
    for (i = 0; i < numprocs && cont_to_disp; i++)
	(*(proc[i]))(widget, closure[i], event, &cont_to_disp);
    if (numprocs > EHMAXSIZE)
	XtFree((char *)proc);
    return cont_to_disp;
}

static void CompressExposures(XEvent *, Widget);

#define KnownButtons (Button1MotionMask|Button2MotionMask|Button3MotionMask|\
		      Button4MotionMask|Button5MotionMask)

/* keep this SMALL to avoid blowing stack cache! */
/* because some compilers allocate all local locals on procedure entry */
#define EHSIZE 4

Boolean XtDispatchEventToWidget(
    Widget widget,
    XEvent* event)
{
    register XtEventRec *p;
    Boolean was_dispatched = False;
    Boolean call_tm = False;
    Boolean cont_to_disp;
    EventMask mask;
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);

    mask = _XtConvertTypeToMask(event->type);
    if (event->type == MotionNotify)
	mask |= (event->xmotion.state & KnownButtons);

    LOCK_PROCESS;
    if ( (mask == ExposureMask) ||
	 ((event->type == NoExpose) && NO_EXPOSE) ||
	 ((event->type == GraphicsExpose) && GRAPHICS_EXPOSE) ) {

	if (widget->core.widget_class->core_class.expose != NULL ) {

	    /* We need to mask off the bits that could contain the information
	     * about whether or not we desire Graphics and NoExpose events.  */

	    if ( (COMP_EXPOSE_TYPE == XtExposeNoCompress) ||
		 (event->type == NoExpose) )

		(*widget->core.widget_class->core_class.expose)
		    (widget, event, (Region)NULL);
	    else {
		CompressExposures(event, widget);
	    }
	    was_dispatched = True;
	}
    }

    if ((mask == VisibilityChangeMask) &&
        XtClass(widget)->core_class.visible_interest) {
	    was_dispatched = True;
	    /* our visibility just changed... */
	    switch (((XVisibilityEvent *)event)->state) {
		case VisibilityUnobscured:
		    widget->core.visible = TRUE;
		    break;

		case VisibilityPartiallyObscured:
		    /* what do we want to say here? */
		    /* well... some of us is visible */
		    widget->core.visible = TRUE;
		    break;

		case VisibilityFullyObscured:
		    widget->core.visible = FALSE;
		    /* do we want to mark our children obscured? */
		    break;
	    }
	}
    UNLOCK_PROCESS;

    /* to maintain "copy" semantics we check TM now but call later */
    if (widget->core.tm.translations &&
	(mask & widget->core.tm.translations->eventMask))
	call_tm = True;

    cont_to_disp = True;
    p=widget->core.event_table;
    if (p) {
	if (p->next) {
	    XtEventHandler proc[EHSIZE];
	    XtPointer closure[EHSIZE];
	    int numprocs = 0;

	    /* Have to copy the procs into an array, because one of them might
	     * call XtRemoveEventHandler, which would break our linked list. */

	    for (; p; p = p->next) {
		if ((!p->has_type_specifier && (mask & p->mask)) ||
		    (p->has_type_specifier && event->type == EXT_TYPE(p))) {
		    if (numprocs >= EHSIZE)
			break;
		    proc[numprocs] = p->proc;
		    closure[numprocs] = p->closure;
		    numprocs++;
		}
	    }
	    if (numprocs) {
		if (p) {
		    cont_to_disp = CallEventHandlers(widget, event, mask);
		} else {
		    int i;
		    for (i = 0; i < numprocs && cont_to_disp; i++)
			(*(proc[i]))(widget, closure[i], event, &cont_to_disp);
/* FUNCTIONS CALLED THROUGH POINTER proc:
		Selection.c:ReqCleanup,
		"Shell.c":EventHandler,
		PassivGrab.c:ActiveHandler,
		PassivGrab.c:RealizeHandler,
		Keyboard.c:QueryEventMask,
		_XtHandleFocus,
		Selection.c:HandleSelectionReplies,
		Selection.c:HandleGetIncrement,
		Selection.c:HandleIncremental,
		Selection.c:HandlePropertyGone,
		Selection.c:HandleSelectionEvents
		*/
		}
		was_dispatched = True;
	    }
	} else if ((!p->has_type_specifier && (mask & p->mask)) ||
		   (p->has_type_specifier && event->type == EXT_TYPE(p))) {
	    (*p->proc)(widget, p->closure, event, &cont_to_disp);
	    was_dispatched = True;
	}
    }
    if (call_tm && cont_to_disp)
	_XtTranslateEvent(widget, event);
    UNLOCK_APP(app);
    return (was_dispatched|call_tm);
}

/*
 * This structure is passed into the check exposure proc.
 */

typedef struct _CheckExposeInfo {
    int type1, type2;		/* Types of events to check for. */
    Boolean maximal;		/* Ignore non-exposure events? */
    Boolean non_matching;	/* Was there an event that did not
				   match either type? */
    Window window;		/* Window to match. */
} CheckExposeInfo;

#define GetCount(ev) (((XExposeEvent *)(ev))->count)

static void SendExposureEvent(XEvent *, Widget, XtPerDisplay);
static Bool CheckExposureEvent(Display *, XEvent *, char *);
static void AddExposureToRectangularRegion(XEvent *, Region);

/*	Function Name: CompressExposures
 *	Description: Handles all exposure compression
 *	Arguments: event - the xevent that is to be dispatched
 *                 widget - the widget that this event occured in.
 *	Returns: none.
 *
 *      NOTE: Event must be of type Expose or GraphicsExpose.
 */

static void
CompressExposures(
XEvent * event,
Widget widget)
{
    CheckExposeInfo info;
    int count;
    Display* dpy = XtDisplay (widget);
    XtPerDisplay pd = _XtGetPerDisplay(dpy);
    XtEnum comp_expose;
    XtEnum comp_expose_type;
    Boolean no_region;

    LOCK_PROCESS;
    comp_expose = COMP_EXPOSE;
    UNLOCK_PROCESS;
    comp_expose_type = comp_expose & 0x0f;
    no_region = ((comp_expose & XtExposeNoRegion) ? True : False);

    if (no_region)
	AddExposureToRectangularRegion(event, pd->region);
    else
	XtAddExposureToRegion(event, pd->region);

    if ( GetCount(event) != 0 )
 	return;

    if ((comp_expose_type == XtExposeCompressSeries) ||
	(XEventsQueued(dpy, QueuedAfterReading) == 0)) {
	SendExposureEvent(event, widget, pd);
	return;
    }

    if (comp_expose & XtExposeGraphicsExposeMerged) {
	info.type1 = Expose;
	info.type2 = GraphicsExpose;
    }
    else {
	info.type1 = event->type;
	info.type2 = 0;
    }
    info.maximal = (comp_expose_type == XtExposeCompressMaximal);
    info.non_matching = FALSE;
    info.window = XtWindow(widget);

/*
 * We have to be very careful here not to hose down the processor
 * when blocking until count gets to zero.
 *
 * First, check to see if there are any events in the queue for this
 * widget, and of the correct type.
 *
 * Once we cannot find any more events, check to see that count is zero.
 * If it is not then block until we get another exposure event.
 *
 * If we find no more events, and count on the last one we saw was zero we
 * we can be sure that all events have been processed.
 *
 * Unfortunately, we wind up having to look at the entire queue
 * event if we're not doing Maximal compression, due to the
 * semantics of XCheckIfEvent (we can't abort without re-ordering
 * the event queue as a side-effect).
 */

    count = 0;
    while (TRUE) {
	XEvent event_return;

	if (XCheckIfEvent(dpy, &event_return,
			  CheckExposureEvent, (char *) &info)) {

	    count = GetCount(&event_return);
 	    if (no_region)
		AddExposureToRectangularRegion(&event_return, pd->region);
 	    else
		XtAddExposureToRegion(&event_return, pd->region);
	}
	else if (count != 0) {
	    XIfEvent(dpy, &event_return,
		     CheckExposureEvent, (char *) &info);
	    count = GetCount(&event_return);
 	    if (no_region)
		AddExposureToRectangularRegion(&event_return, pd->region);
 	    else
		XtAddExposureToRegion(&event_return, pd->region);
	}
	else /* count == 0 && XCheckIfEvent Failed. */
	    break;
    }

    SendExposureEvent(event, widget, pd);
}

void XtAddExposureToRegion(
    XEvent   *event,
    Region   region)
{
    XRectangle rect;
    XExposeEvent *ev = (XExposeEvent *) event;
    /* These Expose and GraphicsExpose fields are at identical offsets */

    if (event->type == Expose || event->type == GraphicsExpose) {
	rect.x = ev->x;
	rect.y = ev->y;
	rect.width = ev->width;
	rect.height = ev->height;
	XUnionRectWithRegion(&rect, region, region);
    }
}

#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

static void AddExposureToRectangularRegion(
     XEvent *event, /* when called internally, type is always appropriate */
     Region  region)
{
    XRectangle rect;
    XExposeEvent *ev = (XExposeEvent *) event;
    /* These Expose and GraphicsExpose fields are at identical offsets */

    rect.x = ev->x;
    rect.y = ev->y;
    rect.width = ev->width;
    rect.height = ev->height;

    if (XEmptyRegion(region)) {
	XUnionRectWithRegion(&rect, region, region);
    } else {
	XRectangle merged, bbox;

	XClipBox(region, &bbox);
	merged.x = MIN(rect.x, bbox.x);
	merged.y = MIN(rect.y, bbox.y);
	merged.width = MAX(rect.x + rect.width,
			   bbox.x + bbox.width) - merged.x;
	merged.height = MAX(rect.y + rect.height,
			    bbox.y + bbox.height) - merged.y;
	XUnionRectWithRegion(&merged, region, region);
    }
}

static Region nullRegion;
/* READ-ONLY VARIABLES: nullRegion */

void _XtEventInitialize(void)
{
#ifndef __lock_lint
    nullRegion = XCreateRegion();
#endif
}

/*	Function Name: SendExposureEvent
 *	Description: Sets the x, y, width, and height of the event
 *                   to be the clip box of Expose Region.
 *	Arguments: event  - the X Event to mangle; Expose or GraphicsExpose.
 *                 widget - the widget that this event occured in.
 *                 pd     - the per display information for this widget.
 *	Returns: none.
 */

static void
SendExposureEvent(
XEvent * event,
Widget widget,
XtPerDisplay pd)
{
    XtExposeProc expose;
    XRectangle rect;
    XtEnum comp_expose;
    XExposeEvent *ev = (XExposeEvent *) event;

    XClipBox(pd->region, &rect);
    ev->x = rect.x;
    ev->y = rect.y;
    ev->width = rect.width;
    ev->height = rect.height;

    LOCK_PROCESS;
    comp_expose = COMP_EXPOSE;
    expose = widget->core.widget_class->core_class.expose;
    UNLOCK_PROCESS;
    if (comp_expose & XtExposeNoRegion)
	(*expose)(widget, event, NULL);
    else
	(*expose)(widget, event, pd->region);
    (void) XIntersectRegion(nullRegion, pd->region, pd->region);
}

/*	Function Name: CheckExposureEvent
 *	Description: Checks the event queue for an expose event
 *	Arguments: display - the display connection.
 *                 event - the event to check.
 *                 arg - a pointer to the exposure info structure.
 *	Returns: TRUE if we found an event of the correct type
 *               with the right window.
 *
 * NOTE: The only valid types (info.type1 and info.type2) are Expose
 *       and GraphicsExpose.
 */

/* ARGSUSED */
static Bool
CheckExposureEvent(
Display * disp,
XEvent * event,
char * arg)
{
    CheckExposeInfo * info = ((CheckExposeInfo *) arg);

    if ( (info->type1 == event->type) || (info->type2 == event->type)) {
	if (!info->maximal && info->non_matching) return FALSE;
	if (event->type == GraphicsExpose)
	    return(event->xgraphicsexpose.drawable == info->window);
	return(event->xexpose.window == info->window);
    }
    info->non_matching = TRUE;
    return(FALSE);
}

static EventMask const masks[] = {
	0,			    /* Error, should never see  */
	0,			    /* Reply, should never see  */
	KeyPressMask,		    /* KeyPress			*/
	KeyReleaseMask,		    /* KeyRelease		*/
	ButtonPressMask,	    /* ButtonPress		*/
	ButtonReleaseMask,	    /* ButtonRelease		*/
	PointerMotionMask	    /* MotionNotify		*/
		| ButtonMotionMask,
	EnterWindowMask,	    /* EnterNotify		*/
	LeaveWindowMask,	    /* LeaveNotify		*/
	FocusChangeMask,	    /* FocusIn			*/
	FocusChangeMask,	    /* FocusOut			*/
	KeymapStateMask,	    /* KeymapNotify		*/
	ExposureMask,		    /* Expose			*/
	NonMaskableMask,	    /* GraphicsExpose, in GC    */
	NonMaskableMask,	    /* NoExpose, in GC		*/
	VisibilityChangeMask,       /* VisibilityNotify		*/
	SubstructureNotifyMask,     /* CreateNotify		*/
	StructureNotifyMask	    /* DestroyNotify		*/
		| SubstructureNotifyMask,
	StructureNotifyMask	    /* UnmapNotify		*/
		| SubstructureNotifyMask,
	StructureNotifyMask	    /* MapNotify		*/
		| SubstructureNotifyMask,
	SubstructureRedirectMask,   /* MapRequest		*/
	StructureNotifyMask	    /* ReparentNotify		*/
		| SubstructureNotifyMask,
	StructureNotifyMask	    /* ConfigureNotify		*/
		| SubstructureNotifyMask,
	SubstructureRedirectMask,   /* ConfigureRequest		*/
	StructureNotifyMask	    /* GravityNotify		*/
		| SubstructureNotifyMask,
	ResizeRedirectMask,	    /* ResizeRequest		*/
	StructureNotifyMask	    /* CirculateNotify		*/
		| SubstructureNotifyMask,
	SubstructureRedirectMask,   /* CirculateRequest		*/
	PropertyChangeMask,	    /* PropertyNotify		*/
	NonMaskableMask,	    /* SelectionClear		*/
	NonMaskableMask,	    /* SelectionRequest		*/
	NonMaskableMask,	    /* SelectionNotify		*/
	ColormapChangeMask,	    /* ColormapNotify		*/
	NonMaskableMask,	    /* ClientMessage		*/
	NonMaskableMask		    /* MappingNotify		*/
};

EventMask _XtConvertTypeToMask (
    int		eventType)
{
    if ((Cardinal) eventType < XtNumber(masks))
	return masks[eventType];
    else
	return NoEventMask;
}

Boolean _XtOnGrabList(
    register Widget widget,
    XtGrabRec  *grabList)
{
    register XtGrabRec* gl;
    for (; widget != NULL; widget = (Widget)widget->core.parent) {
	for (gl = grabList; gl != NULL; gl = gl->next) {
	    if (gl->widget == widget) return TRUE;
	    if (gl->exclusive) break;
	}
    }
    return FALSE;
}

static Widget LookupSpringLoaded(
    XtGrabList	grabList)
{
    XtGrabList	gl;

    for (gl = grabList; gl != NULL; gl = gl->next) {
	if (gl->spring_loaded) {
	  if (XtIsSensitive(gl->widget))
	    return gl->widget;
	  else
	    return NULL;
	}
	if (gl->exclusive) break;
    }
    return NULL;
}

static Boolean DispatchEvent(
    XEvent* event,
    Widget widget)
{

    if (event->type == EnterNotify &&
	event->xcrossing.mode == NotifyNormal &&
	widget->core.widget_class->core_class.compress_enterleave) {
	if (XPending(event->xcrossing.display)) {
	    XEvent nextEvent;
	    XPeekEvent(event->xcrossing.display, &nextEvent);
	    if (nextEvent.type == LeaveNotify &&
		event->xcrossing.window == nextEvent.xcrossing.window &&
		nextEvent.xcrossing.mode == NotifyNormal &&
		((event->xcrossing.detail != NotifyInferior &&
		 nextEvent.xcrossing.detail != NotifyInferior) ||
		 (event->xcrossing.detail == NotifyInferior &&
		 nextEvent.xcrossing.detail == NotifyInferior))) {
		/* skip the enter/leave pair */
		XNextEvent(event->xcrossing.display, &nextEvent);
		return False;
	    }
	}
    }

    if (event->type == MotionNotify &&
	    widget->core.widget_class->core_class.compress_motion) {
	while (XPending(event->xmotion.display)) {
	    XEvent nextEvent;
	    XPeekEvent(event->xmotion.display, &nextEvent);
	    if (nextEvent.type == MotionNotify &&
		    event->xmotion.window == nextEvent.xmotion.window &&
		    event->xmotion.subwindow == nextEvent.xmotion.subwindow) {
		/* replace the current event with the next one */
		XNextEvent(event->xmotion.display, event);
	    } else break;
	}
    }

    return XtDispatchEventToWidget(widget, event);
}

typedef enum _GrabType {pass, ignore, remap} GrabType;

#if !defined(AIXV3) || !defined(AIXSHLIB)
static /* AIX shared libraries are broken */
#endif
Boolean _XtDefaultDispatcher(
    XEvent  *event)
{
    register    Widget widget;
    GrabType    grabType;
    XtPerDisplayInput pdi;
    XtGrabList  grabList;
    Boolean	was_dispatched = False;
    DPY_TO_APPCON(event->xany.display);

    /* the default dispatcher discards all extension events */
    if (event->type >= LASTEvent)
	return False;

    LOCK_APP(app);

    switch (event->type) {
      case KeyPress:
      case KeyRelease:
      case ButtonPress:
      case ButtonRelease:	grabType = remap;	break;
      case MotionNotify:
      case EnterNotify:		grabType = ignore;	break;
      default:			grabType = pass;	break;
    }

    widget = XtWindowToWidget (event->xany.display, event->xany.window);
    pdi = _XtGetPerDisplayInput(event->xany.display);
    grabList = *_XtGetGrabList(pdi);

    if (widget == NULL) {
	if (grabType == remap
	    && (widget = LookupSpringLoaded(grabList)) != NULL) {
	    /* event occurred in a non-widget window, but we've promised also
	       to dispatch it to the nearest accessible spring_loaded widget */
	    was_dispatched = (XFilterEvent(event, XtWindow(widget))
			      || XtDispatchEventToWidget(widget, event));
	}
	else was_dispatched = XFilterEvent(event, None);
    }
    else if (grabType == pass) {
	if (event->type == LeaveNotify ||
	    event->type == FocusIn ||
	    event->type == FocusOut) {
		if (XtIsSensitive (widget))
		    was_dispatched = (XFilterEvent(event, XtWindow(widget)) ||
				      XtDispatchEventToWidget(widget, event));
	    } else was_dispatched = (XFilterEvent(event, XtWindow(widget)) ||
				     XtDispatchEventToWidget(widget, event));
    }
    else if (grabType == ignore) {
	if ((grabList == NULL || _XtOnGrabList(widget, grabList))
	    && XtIsSensitive(widget)) {
	    was_dispatched = (XFilterEvent(event, XtWindow(widget))
			      || DispatchEvent(event, widget));
	}
    }
    else if (grabType == remap) {
	EventMask	mask = _XtConvertTypeToMask(event->type);
	Widget		dspWidget;
	Boolean		was_filtered = False;

	dspWidget = _XtFindRemapWidget(event, widget, mask, pdi);

	if ((grabList == NULL ||_XtOnGrabList(dspWidget, grabList))
	    && XtIsSensitive(dspWidget)) {
	    if ((was_filtered = XFilterEvent(event, XtWindow(dspWidget)))) {
		/* If this event activated a device grab, release it. */
		_XtUngrabBadGrabs(event, widget, mask, pdi);
		was_dispatched = True;
	    } else
		was_dispatched = XtDispatchEventToWidget(dspWidget, event);
	}
	else _XtUngrabBadGrabs(event, widget, mask, pdi);

	if (!was_filtered) {
	    /* Also dispatch to nearest accessible spring_loaded. */
	    /* Fetch this afterward to reflect modal list changes */
	    grabList = *_XtGetGrabList(pdi);
	    widget = LookupSpringLoaded(grabList);
	    if (widget != NULL && widget != dspWidget) {
		was_dispatched = (XFilterEvent(event, XtWindow(widget))
				  || XtDispatchEventToWidget(widget, event)
				  || was_dispatched);
	    }
	}
    }
    UNLOCK_APP(app);
    return was_dispatched;
}

Boolean XtDispatchEvent (
    XEvent  *event)
{
    Boolean was_dispatched, safe;
    int dispatch_level;
    int starting_count;
    XtPerDisplay pd;
    Time	time = 0;
    XtEventDispatchProc dispatch = _XtDefaultDispatcher;
    XtAppContext app = XtDisplayToApplicationContext(event->xany.display);

    LOCK_APP(app);
    dispatch_level = ++app->dispatch_level;
    starting_count = app->destroy_count;

    switch (event->type) {
      case KeyPress:
      case KeyRelease:	   time = event->xkey.time;		break;
      case ButtonPress:
      case ButtonRelease:  time = event->xbutton.time;		break;
      case MotionNotify:   time = event->xmotion.time;		break;
      case EnterNotify:
      case LeaveNotify:	   time = event->xcrossing.time;	break;
      case PropertyNotify: time = event->xproperty.time;	break;
      case SelectionClear: time = event->xselectionclear.time;	break;

      case MappingNotify:  _XtRefreshMapping(event, True);	break;
    }
    pd = _XtGetPerDisplay(event->xany.display);
    if (time) pd->last_timestamp = time;
    pd->last_event = *event;

    if (pd->dispatcher_list) {
	dispatch = pd->dispatcher_list[event->type];
	if (dispatch == NULL) dispatch = _XtDefaultDispatcher;
    }
    was_dispatched = (*dispatch)(event);

    /*
     * To make recursive XtDispatchEvent work, we need to do phase 2 destroys
     * only on those widgets destroyed by this particular dispatch.
     *
     */

    if (app->destroy_count > starting_count)
	_XtDoPhase2Destroy(app, dispatch_level);

    app->dispatch_level = dispatch_level - 1;

    if ((safe = _XtSafeToDestroy(app))) {
	if (app->dpy_destroy_count != 0) _XtCloseDisplays(app);
	if (app->free_bindings) _XtDoFreeBindings(app);
    }
    UNLOCK_APP(app);
    LOCK_PROCESS;
    if (_XtAppDestroyCount != 0 && safe) _XtDestroyAppContexts();
    UNLOCK_PROCESS;
    return was_dispatched;
}

/* ARGSUSED */
static void GrabDestroyCallback(
    Widget  widget,
    XtPointer closure,
    XtPointer call_data)
{
    /* Remove widget from grab list if it destroyed */
    XtRemoveGrab(widget);
}

static XtGrabRec *NewGrabRec(
    Widget  widget,
    Boolean exclusive,
    Boolean spring_loaded)
{
    register XtGrabList    gl;

    gl		      = XtNew(XtGrabRec);
    gl->next	      = NULL;
    gl->widget        = widget;
    gl->exclusive     = exclusive;
    gl->spring_loaded = spring_loaded;

    return gl;
}

void XtAddGrab(
    Widget  widget,
    _XtBoolean exclusive,
    _XtBoolean spring_loaded)
{
    register    XtGrabList gl;
    XtGrabList	*grabListPtr;
    XtAppContext app = XtWidgetToApplicationContext(widget);

    LOCK_APP(app);
    LOCK_PROCESS;
    grabListPtr = _XtGetGrabList(_XtGetPerDisplayInput(XtDisplay(widget)));

    if (spring_loaded && !exclusive) {
	XtAppWarningMsg(app,
		"grabError", "xtAddGrab", XtCXtToolkitError,
		"XtAddGrab requires exclusive grab if spring_loaded is TRUE",
		(String *) NULL, (Cardinal *) NULL);
	exclusive = TRUE;
    }

    gl = NewGrabRec(widget, exclusive, spring_loaded);
    gl->next = *grabListPtr;
    *grabListPtr = gl;

    XtAddCallback (widget, XtNdestroyCallback,
	    GrabDestroyCallback, (XtPointer) NULL);
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
}

void XtRemoveGrab(
    Widget  widget)
{
    register XtGrabList gl;
    register Boolean done;
    XtGrabList	*grabListPtr;
    XtAppContext app = XtWidgetToApplicationContext(widget);

    LOCK_APP(app);
    LOCK_PROCESS;

    grabListPtr = _XtGetGrabList(_XtGetPerDisplayInput(XtDisplay(widget)));

    for (gl = *grabListPtr; gl != NULL; gl = gl->next) {
	if (gl->widget == widget) break;
    }

    if (gl == NULL) {
	    XtAppWarningMsg(app,
		       "grabError","xtRemoveGrab",XtCXtToolkitError,
		       "XtRemoveGrab asked to remove a widget not on the list",
		       (String *)NULL, (Cardinal *)NULL);
	    UNLOCK_PROCESS;
    	    UNLOCK_APP(app);
	    return;
	}

    do {
	gl = *grabListPtr;
	done = (gl->widget == widget);
	*grabListPtr = gl->next;
	XtRemoveCallback(gl->widget, XtNdestroyCallback,
		GrabDestroyCallback, (XtPointer)NULL);
	XtFree((char *)gl);
    } while (! done);
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
    return;
}

void XtMainLoop(void)
{
	XtAppMainLoop(_XtDefaultAppContext());
}

void XtAppMainLoop(
	XtAppContext app)
{
    LOCK_APP(app);
    do {
	XtAppProcessEvent(app, XtIMAll);
    } while(app->exit_flag == FALSE);
    UNLOCK_APP(app);
}

void _XtFreeEventTable(
    XtEventTable *event_table)
{
    register XtEventTable event;

    event = *event_table;
    while (event != NULL) {
	register XtEventTable next = event->next;
	XtFree((char *) event);
	event = next;
    }
}

Time XtLastTimestampProcessed(
    Display *dpy)
{
    Time time;
    DPY_TO_APPCON(dpy);

    LOCK_APP(app);
    LOCK_PROCESS;
    time =  _XtGetPerDisplay(dpy)->last_timestamp;
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
    return(time);
}

XEvent* XtLastEventProcessed(
    Display* dpy)
{
    XEvent* le = NULL;
    DPY_TO_APPCON(dpy);

    LOCK_APP(app);
    le = &_XtGetPerDisplay(dpy)->last_event;
    if (!le->xany.serial)
	le =  NULL;
    UNLOCK_APP(app);
    return le;
}

void _XtSendFocusEvent(
    Widget child,
    int type)
{
    child = XtIsWidget(child) ? child : _XtWindowedAncestor(child);
    if (XtIsSensitive(child) && !child->core.being_destroyed
	&& XtIsRealized(child)
	&& (XtBuildEventMask(child) & FocusChangeMask))
    {
	XFocusChangeEvent event;
	Display* dpy = XtDisplay (child);

	event.type = type;
	event.serial = LastKnownRequestProcessed(dpy);
	event.send_event = True;
	event.display = dpy;
	event.window = XtWindow(child);
	event.mode = NotifyNormal;
	event.detail = NotifyAncestor;
	if (XFilterEvent((XEvent*)&event, XtWindow(child)))
	    return;
	XtDispatchEventToWidget(child, (XEvent*)&event);
    }
}

static XtEventDispatchProc* NewDispatcherList(void)
{
    XtEventDispatchProc* l =
	(XtEventDispatchProc*) __XtCalloc((Cardinal) 128,
					(Cardinal)sizeof(XtEventDispatchProc));
    return l;
}

XtEventDispatchProc XtSetEventDispatcher(
    Display		*dpy,
    int			event_type,
    XtEventDispatchProc	proc)
{
    XtEventDispatchProc *list;
    XtEventDispatchProc old_proc;
    register XtPerDisplay pd;
    DPY_TO_APPCON(dpy);

    LOCK_APP(app);
    LOCK_PROCESS;
    pd = _XtGetPerDisplay(dpy);

    list = pd->dispatcher_list;
    if (!list) {
	if (proc) list = pd->dispatcher_list = NewDispatcherList();
	else return _XtDefaultDispatcher;
    }
    old_proc = list[event_type];
    list[event_type] = proc;
    if (old_proc == NULL) old_proc = _XtDefaultDispatcher;
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
    return old_proc;
}

void XtRegisterExtensionSelector(
    Display		*dpy,
    int			min_event_type,
    int			max_event_type,
    XtExtensionSelectProc	proc,
    XtPointer 		client_data)
{
    ExtSelectRec *e;
    XtPerDisplay pd;
    int i;
    DPY_TO_APPCON(dpy);

    if (dpy == NULL) XtErrorMsg("nullDisplay",
		"xtRegisterExtensionSelector", XtCXtToolkitError,
		"XtRegisterExtensionSelector requires a non-NULL display",
		(String *) NULL, (Cardinal *) NULL);

    LOCK_APP(app);
    LOCK_PROCESS;
    pd = _XtGetPerDisplay(dpy);

    for (i = 0; i < pd->ext_select_count; i++) {
	e = &pd->ext_select_list[i];
	if (e->min == min_event_type && e->max == max_event_type) {
	    e->proc = proc;
	    e->client_data = client_data;
	    return;
	}
	if ((min_event_type >= e->min && min_event_type <= e->max) ||
	    (max_event_type >= e->min && max_event_type <= e->max)) {
	    XtErrorMsg("rangeError", "xtRegisterExtensionSelector",
		       XtCXtToolkitError,
	"Attempt to register multiple selectors for one extension event type",
		       (String *) NULL, (Cardinal *) NULL);
	    UNLOCK_PROCESS;
	    UNLOCK_APP(app);
	    return;
	}
    }
    pd->ext_select_count++;
    pd->ext_select_list =
	    (ExtSelectRec *) XtRealloc((char *) pd->ext_select_list,
		       pd->ext_select_count * sizeof(ExtSelectRec));
    for (i = pd->ext_select_count - 1; i > 0; i--) {
	if (pd->ext_select_list[i-1].min > min_event_type) {
	    pd->ext_select_list[i] = pd->ext_select_list[i-1];
	} else break;
    }
    pd->ext_select_list[i].min = min_event_type;
    pd->ext_select_list[i].max = max_event_type;
    pd->ext_select_list[i].proc = proc;
    pd->ext_select_list[i].client_data = client_data;
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
}

void _XtExtensionSelect(
    Widget widget)
{
    int i;
    XtPerDisplay pd;
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    LOCK_PROCESS;

    pd = _XtGetPerDisplay(XtDisplay(widget));

    for (i = 0; i < pd->ext_select_count; i++) {
	CallExtensionSelector(widget, pd->ext_select_list+i, FALSE);
    }
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
}
