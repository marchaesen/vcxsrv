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

#define INTRINSIC_C

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "IntrinsicI.h"
#include "VarargsI.h"        /* for geoTattler */
#ifndef NO_IDENTIFY_WINDOWS
#include <X11/Xatom.h>
#endif
#ifndef VMS
#include <sys/stat.h>
#endif /* VMS */
#ifdef WIN32
#include <direct.h>            /* for _getdrives() */
#endif

#include <stdlib.h>

String XtCXtToolkitError = "XtToolkitError";

Boolean XtIsSubclass(
    Widget    widget,
    WidgetClass widgetClass)
{
    register WidgetClass w;
    Boolean retval = FALSE;
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    LOCK_PROCESS;
    for (w = widget->core.widget_class; w != NULL; w = w->core_class.superclass)
	if (w == widgetClass) {
	    retval = TRUE;
	    break;
	}
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
    return retval;
} /* XtIsSubclass */


Boolean _XtCheckSubclassFlag(
    Widget object,
    _XtXtEnum flag)
{
    Boolean retval;

    LOCK_PROCESS;
    if (object->core.widget_class->core_class.class_inited & flag)
	retval = TRUE;
    else
	retval = FALSE;
    UNLOCK_PROCESS;
    return retval;
} /*_XtVerifySubclass */


Boolean _XtIsSubclassOf(
    Widget object,
    WidgetClass widgetClass,
    WidgetClass superClass,
    _XtXtEnum flag)
{
    LOCK_PROCESS;
    if (!(object->core.widget_class->core_class.class_inited & flag)) {
	UNLOCK_PROCESS;
	return False;
    } else {
	register WidgetClass c = object->core.widget_class;
	while (c != superClass) {
	    if (c == widgetClass) {
		UNLOCK_PROCESS;
		return True;
	    }
	    c = c->core_class.superclass;
	}
	UNLOCK_PROCESS;
	return False;
    }
} /*_XtIsSubclassOf */


XtPointer XtGetClassExtension(
    WidgetClass	object_class,
    Cardinal	byte_offset,
    XrmQuark	type,
    long	version,
    Cardinal	record_size)
{
    ObjectClassExtension ext;
    LOCK_PROCESS;

    ext = *(ObjectClassExtension *)((char *)object_class + byte_offset);
    while (ext && (ext->record_type != type || ext->version < version
		   || ext->record_size < record_size)) {
	ext = (ObjectClassExtension) ext->next_extension;
    }

    UNLOCK_PROCESS;
    return (XtPointer) ext;
}


static void ComputeWindowAttributes(
    Widget		 widget,
    XtValueMask		 *value_mask,
    XSetWindowAttributes *values)
{
    XtExposeProc expose;

    *value_mask = CWEventMask | CWColormap;
    (*values).event_mask = XtBuildEventMask(widget);
    (*values).colormap = widget->core.colormap;
    if (widget->core.background_pixmap != XtUnspecifiedPixmap) {
	*value_mask |= CWBackPixmap;
	(*values).background_pixmap = widget->core.background_pixmap;
    } else {
	*value_mask |= CWBackPixel;
	(*values).background_pixel = widget->core.background_pixel;
    }
    if (widget->core.border_pixmap != XtUnspecifiedPixmap) {
	*value_mask |= CWBorderPixmap;
	(*values).border_pixmap = widget->core.border_pixmap;
    } else {
	*value_mask |= CWBorderPixel;
	(*values).border_pixel = widget->core.border_pixel;
    }
    LOCK_PROCESS;
    expose = widget->core.widget_class->core_class.expose;
    UNLOCK_PROCESS;
    if (expose == (XtExposeProc) NULL) {
	/* Try to avoid redisplay upon resize by making bit_gravity the same
	   as the default win_gravity */
	*value_mask |= CWBitGravity;
	(*values).bit_gravity = NorthWestGravity;
    }
} /* ComputeWindowAttributes */

static void CallChangeManaged(
    register Widget		widget)
{
    register Cardinal		i;
    XtWidgetProc		change_managed;
    register WidgetList		children;
    int    			managed_children = 0;

    register CompositePtr cpPtr;
    register CompositePartPtr clPtr;

    if (XtIsComposite (widget)) {
	cpPtr = (CompositePtr)&((CompositeWidget) widget)->composite;
        clPtr = (CompositePartPtr)&((CompositeWidgetClass)
                   widget->core.widget_class)->composite_class;
    } else return;

    children = cpPtr->children;
    LOCK_PROCESS;
    change_managed = clPtr->change_managed;
    UNLOCK_PROCESS;

    /* CallChangeManaged for all children */
    for (i = cpPtr->num_children; i != 0; --i) {
	CallChangeManaged (children[i-1]);
	if (XtIsManaged(children[i-1])) managed_children++;
    }

    if (change_managed != NULL && managed_children != 0) {
	CALLGEOTAT(_XtGeoTrace(widget,"Call \"%s\"[%d,%d]'s changemanaged\n",
		       XtName(widget),
		       widget->core.width, widget->core.height));
	(*change_managed) (widget);
    }
} /* CallChangeManaged */


static void MapChildren(
    CompositePart *cwp)
{
    Cardinal i;
    WidgetList children;
    register Widget child;

    children = cwp->children;
    for (i = 0; i <  cwp->num_children; i++) {
	child = children[i];
	if (XtIsWidget (child)){
	    if (child->core.managed && child->core.mapped_when_managed) {
		XtMapWidget (children[i]);
	    }
	}
    }
} /* MapChildren */


static Boolean ShouldMapAllChildren(
    CompositePart *cwp)
{
    Cardinal i;
    WidgetList children;
    register Widget child;

    children = cwp->children;
    for (i = 0; i < cwp->num_children; i++) {
	child = children[i];
	if (XtIsWidget(child)) {
	    if (XtIsRealized(child) && (! (child->core.managed
					  && child->core.mapped_when_managed))){
		    return False;
	    }
	}
    }

    return True;
} /* ShouldMapAllChildren */


static void RealizeWidget(
    Widget			widget)
{
    XtValueMask			value_mask;
    XSetWindowAttributes	values;
    XtRealizeProc		realize;
    Window			window;
    Display*			display;
    String			class_name;
    Widget			hookobj;

    if (!XtIsWidget(widget) || XtIsRealized(widget)) return;
    display = XtDisplay(widget);
    _XtInstallTranslations(widget);

    ComputeWindowAttributes (widget, &value_mask, &values);
    LOCK_PROCESS;
    realize = widget->core.widget_class->core_class.realize;
    class_name = widget->core.widget_class->core_class.class_name;
    UNLOCK_PROCESS;
    if (realize == NULL)
	XtAppErrorMsg(XtWidgetToApplicationContext(widget),
		      "invalidProcedure","realizeProc",XtCXtToolkitError,
		      "No realize class procedure defined",
		      (String *)NULL, (Cardinal *)NULL);
    else {
	CALLGEOTAT(_XtGeoTrace(widget,"Call \"%s\"[%d,%d]'s realize proc\n",
		       XtName(widget),
		       widget->core.width, widget->core.height));
	(*realize) (widget, &value_mask, &values);
    }
    window = XtWindow(widget);
    hookobj = XtHooksOfDisplay(XtDisplayOfObject(widget));
    if (XtHasCallbacks(hookobj,XtNchangeHook) == XtCallbackHasSome) {
	XtChangeHookDataRec call_data;

	call_data.type = XtHrealizeWidget;
	call_data.widget = widget;
	XtCallCallbackList(hookobj,
		((HookObject)hookobj)->hooks.changehook_callbacks,
		(XtPointer)&call_data);
    }
#ifndef NO_IDENTIFY_WINDOWS
    if (_XtGetPerDisplay(display)->appContext->identify_windows) {
	int len_nm, len_cl;
	char *s;

	len_nm = widget->core.name ? strlen(widget->core.name) : 0;
	len_cl = strlen(class_name);
	s = __XtMalloc((unsigned) (len_nm + len_cl + 2));
	s[0] = '\0';
	if (len_nm)
	    strcpy(s, widget->core.name);
	strcpy(s + len_nm + 1, class_name);
	XChangeProperty(display, window,
			XInternAtom(display, "_MIT_OBJ_CLASS",
				    False),
			XA_STRING, 8, PropModeReplace, (unsigned char *) s,
			len_nm + len_cl + 2);
	XtFree(s);
    }
#endif
#ifdef notdef
    _XtRegisterAsyncHandlers(widget);
#endif
    /* (re)register any grabs extant in the translations */
    _XtRegisterGrabs(widget);
    /* reregister any grabs added with XtGrab{Button,Key} */
    _XtRegisterPassiveGrabs(widget);
    XtRegisterDrawable (display, window, widget);
    _XtExtensionSelect(widget);

    if (XtIsComposite (widget)) {
	Cardinal		i;
	CompositePart *cwp = &(((CompositeWidget)widget)->composite);
	WidgetList children = cwp->children;
	/* Realize all children */
	for (i = cwp->num_children; i != 0; --i) {
	    RealizeWidget (children[i-1]);
	}
	/* Map children that are managed and mapped_when_managed */

	if (cwp->num_children != 0) {
	    if (ShouldMapAllChildren(cwp)) {
		XMapSubwindows (display, window);
	    } else {
		MapChildren(cwp);
	    }
	}
    }

    /* If this is the application's popup shell, map it */
    if (widget->core.parent == NULL && widget->core.mapped_when_managed) {
	XtMapWidget (widget);
    }
} /* RealizeWidget */

void XtRealizeWidget (
    Widget		widget)
{
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    if (XtIsRealized (widget)) {
	UNLOCK_APP(app);
	return;
    }
    CallChangeManaged(widget);
    RealizeWidget(widget);
    UNLOCK_APP(app);
} /* XtRealizeWidget */


static void UnrealizeWidget(
    Widget		widget)
{
    CompositeWidget	cw;
    Cardinal		i;
    WidgetList		children;

    if (!XtIsWidget(widget) || !XtIsRealized(widget)) return;

    /* If this is the application's popup shell, unmap it? */
    /* no, the window is being destroyed */

    /* Recurse on children */
    if (XtIsComposite (widget)) {
	cw = (CompositeWidget) widget;
	children = cw->composite.children;
	/* Unrealize all children */
	for (i = cw->composite.num_children; i != 0; --i) {
	    UnrealizeWidget (children[i-1]);
	}
	/* Unmap children that are managed and mapped_when_managed? */
	/* No, it's ok to be managed and unrealized as long as your parent */
	/* is unrealized. XtUnrealize widget makes sure the "top" widget */
	/* is unmanaged, we can ignore all descendents */
    }

    if (XtHasCallbacks(widget, XtNunrealizeCallback) == XtCallbackHasSome)
	XtCallCallbacks(widget, XtNunrealizeCallback, NULL);

    /* Unregister window */
    XtUnregisterDrawable(XtDisplay(widget), XtWindow(widget));

    /* Remove Event Handlers */
    /* remove grabs. Happens automatically when window is destroyed. */

    /* Destroy X Window, done at outer level with one request */
    widget->core.window = None;

    /* Removing the event handler here saves having to keep track if
     * the translation table is changed while the widget is unrealized.
     */
    _XtRemoveTranslations(widget);
} /* UnrealizeWidget */


void XtUnrealizeWidget (
    Widget		widget)
{
    Window window;
    Widget hookobj;
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    window = XtWindow(widget);
    if (! XtIsRealized (widget)) {
	UNLOCK_APP(app);
	return;
    }
    if (widget->core.managed && widget->core.parent != NULL)
	XtUnmanageChild(widget);
    UnrealizeWidget(widget);
    if (window != None)
	XDestroyWindow(XtDisplay(widget), window);
    hookobj = XtHooksOfDisplay(XtDisplayOfObject(widget));
    if (XtHasCallbacks(hookobj, XtNchangeHook) == XtCallbackHasSome) {
	XtChangeHookDataRec call_data;

	call_data.type = XtHunrealizeWidget;
	call_data.widget = widget;
	XtCallCallbackList(hookobj,
		((HookObject)hookobj)->hooks.changehook_callbacks,
		(XtPointer)&call_data);
    }
    UNLOCK_APP(app);
} /* XtUnrealizeWidget */


void XtCreateWindow(
    Widget		 widget,
    unsigned int	 window_class,
    Visual		 *visual,
    XtValueMask		 value_mask,
    XSetWindowAttributes *attributes)
{
    XtAppContext app = XtWidgetToApplicationContext(widget);

    LOCK_APP(app);
    if (widget->core.window == None) {
	if (widget->core.width == 0 || widget->core.height == 0) {
	    Cardinal count = 1;
	    XtAppErrorMsg(app,
		       "invalidDimension", "xtCreateWindow", XtCXtToolkitError,
		       "Widget %s has zero width and/or height",
		       &widget->core.name, &count);
	}
	widget->core.window =
	    XCreateWindow (
		XtDisplay (widget),
		(widget->core.parent ?
		    widget->core.parent->core.window :
		    widget->core.screen->root),
		(int)widget->core.x, (int)widget->core.y,
		(unsigned)widget->core.width, (unsigned)widget->core.height,
		(unsigned)widget->core.border_width, (int) widget->core.depth,
		window_class, visual, value_mask, attributes);
    }
    UNLOCK_APP(app);
} /* XtCreateWindow */


/* ---------------- XtNameToWidget ----------------- */

static Widget NameListToWidget(
    Widget root,
    XrmNameList     names,
    XrmBindingList  bindings,
    int in_depth, int *out_depth, int *found_depth);

typedef Widget (*NameMatchProc)(XrmNameList,
    XrmBindingList,
    WidgetList, Cardinal, int, int *, int *);

static Widget MatchExactChildren(
    XrmNameList     names,
    XrmBindingList  bindings,
    register WidgetList children,
    register Cardinal num,
    int in_depth, int *out_depth, int *found_depth)
{
    register Cardinal   i;
    register XrmName    name = *names;
    Widget w, result = NULL;
    int d, min = 10000;

    for (i = 0; i < num; i++) {
	if (name == children[i]->core.xrm_name) {
	    w = NameListToWidget(children[i], &names[1], &bindings[1],
		    in_depth+1, &d, found_depth);
	    if (w != NULL && d < min) {result = w; min = d;}
	}
    }
    *out_depth = min;
    return result;
}

static Widget MatchWildChildren(
    XrmNameList     names,
    XrmBindingList  bindings,
    register WidgetList children,
    register Cardinal num,
    int in_depth, int *out_depth, int *found_depth)
{
    register Cardinal   i;
    Widget w, result = NULL;
    int d, min = 10000;

    for (i = 0; i < num; i++) {
	w = NameListToWidget(children[i], names, bindings,
		in_depth+1, &d, found_depth);
	if (w != NULL && d < min) {result = w; min = d;}
    }
    *out_depth = min;
    return result;
}

static Widget SearchChildren(
    Widget root,
    XrmNameList     names,
    XrmBindingList  bindings,
    NameMatchProc matchproc,
    int in_depth, int *out_depth, int *found_depth)
{
    Widget w1 = NULL, w2;
    int d1, d2;

    if (XtIsComposite(root)) {
	w1 = (*matchproc)(names, bindings,
		((CompositeWidget) root)->composite.children,
		((CompositeWidget) root)->composite.num_children,
		in_depth, &d1, found_depth);
    } else d1 = 10000;
    w2 = (*matchproc)(names, bindings, root->core.popup_list,
	    root->core.num_popups, in_depth, &d2, found_depth);
    *out_depth = (d1 < d2 ? d1 : d2);
    return (d1 < d2 ? w1 : w2);
}

static Widget NameListToWidget(
    register Widget root,
    XrmNameList     names,
    XrmBindingList  bindings,
    int in_depth, int *out_depth, int *found_depth)
{
    Widget w1, w2;
    int d1, d2;

    if (in_depth >= *found_depth) {
	*out_depth = 10000;
	return NULL;
    }

    if (names[0] == NULLQUARK) {
	*out_depth = *found_depth = in_depth;
	return root;
    }

    if (! XtIsWidget(root)) {
	*out_depth = 10000;
	return NULL;
    }

    if (*bindings == XrmBindTightly) {
	return SearchChildren(root, names, bindings, MatchExactChildren,
		in_depth, out_depth, found_depth);

    } else {	/* XrmBindLoosely */
	w1 = SearchChildren(root, names, bindings, MatchExactChildren,
		in_depth, &d1, found_depth);
	w2 = SearchChildren(root, names, bindings, MatchWildChildren,
		in_depth, &d2, found_depth);
	*out_depth = (d1 < d2 ? d1 : d2);
	return (d1 < d2 ? w1 : w2);
    }
} /* NameListToWidget */

Widget XtNameToWidget(
    Widget root,
    _Xconst char* name)
{
    XrmName *names;
    XrmBinding *bindings;
    int len, depth, found = 10000;
    Widget result;
    WIDGET_TO_APPCON(root);

    len = strlen(name);
    if (len == 0) return NULL;

    LOCK_APP(app);
    names = (XrmName *) ALLOCATE_LOCAL((unsigned) (len+1) * sizeof(XrmName));
    bindings = (XrmBinding *)
	ALLOCATE_LOCAL((unsigned) (len+1) * sizeof(XrmBinding));
    if (names == NULL || bindings == NULL) _XtAllocError(NULL);

    XrmStringToBindingQuarkList(name, bindings, names);
    if (names[0] == NULLQUARK) {
	DEALLOCATE_LOCAL((char *) bindings);
	DEALLOCATE_LOCAL((char *) names);
	UNLOCK_APP(app);
	return NULL;
    }

    result = NameListToWidget(root, names, bindings, 0, &depth, &found);

    DEALLOCATE_LOCAL((char *) bindings);
    DEALLOCATE_LOCAL((char *) names);
    UNLOCK_APP(app);
    return result;
} /* XtNameToWidget */

/* Define user versions of intrinsics macros */

#undef XtDisplayOfObject
Display *XtDisplayOfObject(
     Widget object)
{
    /* Attempts to LockApp() here will generate endless recursive loops */
    if (XtIsSubclass(object, hookObjectClass))
	return DisplayOfScreen(((HookObject)object)->hooks.screen);
    return XtDisplay(XtIsWidget(object) ? object : _XtWindowedAncestor(object));
}

#undef XtDisplay
Display *XtDisplay(
	Widget widget)
{
    /* Attempts to LockApp() here will generate endless recursive loops */
    return DisplayOfScreen(widget->core.screen);
}

#undef XtScreenOfObject
Screen *XtScreenOfObject(
     Widget object)
{
    /* Attempts to LockApp() here will generate endless recursive loops */
    if (XtIsSubclass(object, hookObjectClass))
	return ((HookObject)object)->hooks.screen;
    return XtScreen(XtIsWidget(object) ? object : _XtWindowedAncestor(object));
}

#undef XtScreen
Screen *XtScreen(
	Widget widget)
{
    /* Attempts to LockApp() here will generate endless recursive loops */
    return widget->core.screen;
}

#undef XtWindowOfObject
Window XtWindowOfObject(
     Widget object)
{
    return XtWindow(XtIsWidget(object) ? object : _XtWindowedAncestor(object));
}


#undef XtWindow
Window XtWindow(
	Widget widget)
{
    return widget->core.window;
}

#undef XtSuperclass
WidgetClass XtSuperclass(
    Widget widget)
{
    WidgetClass retval;

    LOCK_PROCESS;
    retval = XtClass(widget)->core_class.superclass;
    UNLOCK_PROCESS;
    return retval;
}

#undef XtClass
WidgetClass XtClass(
    Widget widget)
{
    WidgetClass retval;

    LOCK_PROCESS;
    retval = widget->core.widget_class;
    UNLOCK_PROCESS;
    return retval;
}

#undef XtIsManaged
Boolean XtIsManaged(
	Widget object)
{
    Boolean retval;
    WIDGET_TO_APPCON(object);

    LOCK_APP(app);
    if (XtIsRectObj(object))
	retval = object->core.managed;
    else
	retval = False;
    UNLOCK_APP(app);
    return retval;
}

#undef XtIsRealized
Boolean XtIsRealized (
    Widget   object)
{
    Boolean retval;
    WIDGET_TO_APPCON(object);

    LOCK_APP(app);
    retval = XtWindowOfObject(object) != None;
    UNLOCK_APP(app);
    return retval;
} /* XtIsRealized */

#undef XtIsSensitive
Boolean XtIsSensitive(
	Widget	object)
{
    Boolean retval;
    WIDGET_TO_APPCON(object);

    LOCK_APP(app);
    if (XtIsRectObj(object))
	retval = object->core.sensitive && object->core.ancestor_sensitive;
    else
	retval = False;
    UNLOCK_APP(app);
    return retval;
}

/*
 * Internal routine; must be called only after XtIsWidget returns false
 */
Widget _XtWindowedAncestor(
    register Widget object)
{
    Widget obj = object;
    for (object = XtParent(object); object && !XtIsWidget(object);)
	object = XtParent(object);

    if (object == NULL) {
	String params = XtName(obj);
	Cardinal num_params = 1;
	XtErrorMsg("noWidgetAncestor", "windowedAncestor", XtCXtToolkitError,
		   "Object \"%s\" does not have windowed ancestor",
		   &params, &num_params);
    }

    return object;
}

#undef XtParent
Widget XtParent(
    Widget widget)
{
    /* Attempts to LockApp() here will generate endless recursive loops */
    return widget->core.parent;
}

#undef XtName
String XtName(
     Widget object)
{
    /* Attempts to LockApp() here will generate endless recursive loops */
    return XrmQuarkToString(object->core.xrm_name);
}


Boolean XtIsObject(
    Widget object)
{
    WidgetClass wc;
    String class_name;

    /* perform basic sanity checks */
    if (object->core.self != object || object->core.xrm_name == NULLQUARK)
	return False;

    LOCK_PROCESS;
    wc = object->core.widget_class;
    if (wc->core_class.class_name == NULL ||
	wc->core_class.xrm_class == NULLQUARK ||
	(class_name = XrmClassToString(wc->core_class.xrm_class)) == NULL ||
	strcmp(wc->core_class.class_name, class_name) != 0) {
	    UNLOCK_PROCESS;
	    return False;
	}
    UNLOCK_PROCESS;

    if (XtIsWidget(object)) {
	if (object->core.name == NULL ||
	    (class_name = XrmNameToString(object->core.xrm_name)) == NULL ||
	    strcmp(object->core.name, class_name) != 0)
	    return False;
    }
    return True;
}

#if defined(WIN32)
static int access_file (
    char* path,
    char* pathbuf,
    int len_pathbuf,
    char** pathret)
{
    if (access (path, F_OK) == 0) {
	if (strlen (path) < len_pathbuf)
	    *pathret = pathbuf;
	else
	    *pathret = XtMalloc (strlen (path));
	if (*pathret) {
	    strcpy (*pathret, path);
	    return 1;
	}
    }
    return 0;
}

static int AccessFile (
    char* path,
    char* pathbuf,
    int len_pathbuf,
    char** pathret)
{
    unsigned long drives;
    int i, len;
    char* drive;
    char buf[MAX_PATH];
    char* bufp;

    /* just try the "raw" name first and see if it works */
    if (access_file (path, pathbuf, len_pathbuf, pathret))
	return 1;

#if defined(WIN32) && defined(__MINGW32__)
    /* don't try others */
    return 0;
#endif

    /* try the places set in the environment */
    drive = getenv ("_XBASEDRIVE");
    if (!drive)
	drive = "C:";
    len = strlen (drive) + strlen (path);
    bufp = XtStackAlloc (len + 1, buf);
    strcpy (bufp, drive);
    strcat (bufp, path);
    if (access_file (bufp, pathbuf, len_pathbuf, pathret)) {
	XtStackFree (bufp, buf);
	return 1;
    }

    /* one last place to look */
    drive = getenv ("HOMEDRIVE");
    if (drive) {
	len = strlen (drive) + strlen (path);
	bufp = XtStackAlloc (len + 1, buf);
	strcpy (bufp, drive);
	strcat (bufp, path);
	if (access_file (bufp, pathbuf, len_pathbuf, pathret)) {
	    XtStackFree (bufp, buf);
	    return 1;
	}
    }

    /* does OS/2 (with or with gcc-emx) have getdrives()? */
    /* tried everywhere else, go fishing */
    drives = _getdrives ();
#define C_DRIVE ('C' - 'A')
#define Z_DRIVE ('Z' - 'A')
    for (i = C_DRIVE; i <= Z_DRIVE; i++) { /* don't check on A: or B: */
	if ((1 << i) & drives) {
	    len = 2 + strlen (path);
	    bufp = XtStackAlloc (len + 1, buf);
	    *bufp = 'A' + i;
	    *(bufp + 1) = ':';
	    *(bufp + 2) = '\0';
	    strcat (bufp, path);
	    if (access_file (bufp, pathbuf, len_pathbuf, pathret)) {
		XtStackFree (bufp, buf);
		return 1;
	    }
	}
    }
    return 0;
}
#endif

static Boolean TestFile(
    String path)
{
#ifndef VMS
    int ret = 0;
    struct stat status;
#if defined(WIN32)
    char buf[MAX_PATH];
    char* bufp;
    int len;
    UINT olderror = SetErrorMode (SEM_FAILCRITICALERRORS);

    if (AccessFile (path, buf, MAX_PATH, &bufp))
	path = bufp;

    (void) SetErrorMode (olderror);
#endif
    ret = (access(path, R_OK) == 0 &&		/* exists and is readable */
	    stat(path, &status) == 0 &&		/* get the status */
#ifndef X_NOT_POSIX
	    S_ISDIR(status.st_mode) == 0);	/* not a directory */
#else
	    (status.st_mode & S_IFMT) != S_IFDIR);	/* not a directory */
#endif /* X_NOT_POSIX else */
    return ret;
#else /* VMS */
    return TRUE;	/* Who knows what to do here? */
#endif /* VMS */
}

/* return of TRUE = resolved string fit, FALSE = didn't fit.  Not
   null-terminated and not collapsed if it didn't fit */

static Boolean Resolve(
    register _Xconst char *source,	/* The source string */
    register int len,		/* The length in bytes of *source */
    Substitution sub,	/* Array of string values to substitute */
    Cardinal num,	/* Number of substitution entries */
    char *buf,		/* Where to put the resolved string; */
    char collapse)	/* Character to collapse */
{
    register int bytesLeft = PATH_MAX;
    register char* bp = buf;
#ifndef DONT_COLLAPSE
    Boolean atBeginning = TRUE;
    Boolean prevIsCollapse = FALSE;

#define PUT(ch) \
    { \
	if (--bytesLeft == 0) return FALSE; \
        if (prevIsCollapse) \
	    if ((*bp = ch) != collapse) { \
		prevIsCollapse = FALSE; \
		bp++; \
	    } \
	    else bytesLeft++; \
        else if ((*bp++ = ch) == collapse && !atBeginning) \
	    prevIsCollapse = TRUE; \
    }
#else /* DONT_COLLAPSE */

#define PUT(ch) \
    { \
	if (--bytesLeft == 0) return FALSE; \
	*bp++ = ch; \
    }
#endif /* DONT_COLLAPSE */
#define escape '%'

    while (len--) {
#ifndef DONT_COLLAPSE
	if (*source == collapse) {
	    PUT(*source);
	    source++;
	    continue;
	}
	else
#endif /* DONT_COLLAPSE */
	    if (*source != escape) {
		PUT(*source);
	}
	else {
	    source++;
	    if (len-- == 0) {
		PUT(escape);
		break;
	    }

	    if (*source == ':' || *source == escape)
		PUT(*source)
	    else {
		/* Match the character against the match array */
		register Cardinal j;

		for (j = 0; j < num && sub[j].match != *source; j++) {}

		/* Substitute the substitution string */

		if (j >= num) PUT(*source)
		else if (sub[j].substitution != NULL) {
		    char *sp = sub[j].substitution;
		    while (*sp) {
			PUT(*sp);
			sp++;
		    }
		}
	    }
	}
	source++;
#ifndef DONT_COLLAPSE
	atBeginning = FALSE;
#endif /* DONT_COLLAPSE */
    }
    PUT('\0');

    return TRUE;
#undef PUT
#undef escape
}


String XtFindFile(
    _Xconst char* path,
    Substitution substitutions,
    Cardinal num_substitutions,
    XtFilePredicate predicate)
{
    char *buf, *buf1, *buf2, *colon;
    int len;
    Boolean firstTime = TRUE;

    buf = buf1 = __XtMalloc((unsigned)PATH_MAX);
    buf2 = __XtMalloc((unsigned)PATH_MAX);

    if (predicate == NULL) predicate = TestFile;

    while (1) {
	colon = (String)path;
	/* skip leading colons */
	while (*colon) {
	    if (*colon != ':') break;
	    colon++;
	    path++;
	}
	/* now look for an un-escaped colon */
	for ( ; *colon ; colon++) {
	    if (*colon == '%' && *(path+1)) {
		colon++;	/* bump it an extra time to skip %. */
		continue;
	    }
	    if (*colon == ':')
		break;
	}
	len = colon - path;
	if (Resolve(path, len, substitutions, num_substitutions,
		    buf, '/')) {
	    if (firstTime || strcmp(buf1,buf2) != 0) {
#ifdef XNL_DEBUG
		printf("Testing file %s\n", buf);
#endif /* XNL_DEBUG */
		/* Check out the file */
		if ((*predicate) (buf)) {
		    /* We've found it, return it */
#ifdef XNL_DEBUG
		    printf("File found.\n");
#endif /* XNL_DEBUG */
		    if (buf == buf1) XtFree(buf2);
		    else XtFree(buf1);
		    return buf;
		}
		if (buf == buf1)
		    buf = buf2;
		else
		    buf = buf1;
		firstTime = FALSE;
	    }
	}

	/* Nope...any more paths? */

	if (*colon == '\0') break;
	path = colon+1;
    }

    /* No file found */

    XtFree(buf1);
    XtFree(buf2);
    return NULL;
}


/* The implementation of this routine is operating system dependent */
/* Should match the code in Xlib _XlcMapOSLocaleName */

static char *ExtractLocaleName(
    String	lang)
{

#if defined(hpux) || defined(CSRG_BASED) || defined(sun) || defined(SVR4) || defined(sgi) || defined(__osf__) || defined(AIXV3) || defined(ultrix) || defined(WIN32) || defined (linux)
# ifdef hpux
/*
 * We need to discriminated between HPUX 9 and HPUX 10. The equivalent
 * code in Xlib in SetLocale.c does include locale.h via X11/Xlocale.h.
 */
#  include <locale.h>
#  ifndef _LastCategory
   /* HPUX 9 and earlier */
#   define SKIPCOUNT 2
#   define STARTCHAR ':'
#   define ENDCHAR ';'
#  else
    /* HPUX 10 */
#   define ENDCHAR ' '
#  endif
# else
#  ifdef ultrix
#   define SKIPCOUNT 2
#   define STARTCHAR '\001'
#   define ENDCHAR '\001'
#  else
#   ifdef WIN32
#    define SKIPCOUNT 1
#    define STARTCHAR '='
#    define ENDCHAR ';'
#    define WHITEFILL
#   else
#    if defined(__osf__) || (defined(AIXV3) && !defined(AIXV4))
#     define STARTCHAR ' '
#     define ENDCHAR ' '
#    else
#     if defined(linux)
#      define STARTSTR "LC_CTYPE="
#      define ENDCHAR ';'
#     else
#      if !defined(sun) || defined(SVR4)
#       define STARTCHAR '/'
#       define ENDCHAR '/'
#      endif
#     endif
#    endif
#   endif
#  endif
# endif

    char           *start;
    char           *end;
    int             len;
# ifdef SKIPCOUNT
    int		    n;
# endif
    static char*    buf = NULL;

    start = lang;
# ifdef SKIPCOUNT
    for (n = SKIPCOUNT;
	 --n >= 0 && start && (start = strchr (start, STARTCHAR));
	 start++)
	;
    if (!start)
	start = lang;
# endif
# ifdef STARTCHAR
    if (start && (start = strchr (start, STARTCHAR)))
# elif  defined (STARTSTR)
    if (start && (start = strstr (start,STARTSTR)))
# endif
    {
# ifdef STARTCHAR
	start++;
# elif defined (STARTSTR)
	start += strlen(STARTSTR);
# endif

	if ((end = strchr (start, ENDCHAR))) {
	    len = end - start;
	    if (buf != NULL) XtFree (buf);
	    buf = XtMalloc (len + 1);
	    if (buf == NULL) return NULL;
	    strncpy(buf, start, len);
	    *(buf + len) = '\0';
# ifdef WHITEFILL
	    for (start = buf; start = strchr(start, ' '); )
		*start++ = '-';
# endif
	    return buf;
	} else  /* if no ENDCHAR is found we are at the end of the line */
	    return start;
    }
# ifdef WHITEFILL
    if (strchr(lang, ' ')) {
	if (buf != NULL) XtFree (buf);
	else buf = XtMalloc (strlen (lang) + 1);
	if (buf == NULL) return NULL;
	strcpy(buf, lang);
	for (start = buf; start = strchr(start, ' '); )
	    *start++ = '-';
	return buf;
    }
# endif
# undef STARTCHAR
# undef ENDCHAR
# undef WHITEFILL
#endif

    return lang;
}

static void FillInLangSubs(
    Substitution subs,
    XtPerDisplay pd)
{
    int len;
    char *string, *p1, *p2, *p3;
    char **rest;
    char *ch;

    if (pd->language == NULL ||
	(pd->language != NULL && pd->language[0] == '\0')) {
	subs[0].substitution = subs[1].substitution =
		subs[2].substitution = subs[3].substitution = NULL;
	return;
    }

    string = ExtractLocaleName(pd->language);

    if (string == NULL ||
	(string != NULL && string[0] == '\0')) {
	subs[0].substitution = subs[1].substitution =
		subs[2].substitution = subs[3].substitution = NULL;
	return;
    }

    len = strlen(string) + 1;
    subs[0].substitution = string;
    p1 = subs[1].substitution = __XtMalloc((Cardinal) 3*len);
    p2 = subs[2].substitution = subs[1].substitution + len;
    p3 = subs[3].substitution = subs[2].substitution + len;

    /* Everything up to the first "_" goes into p1.  From "_" to "." in
       p2.  The rest in p3.  If no delimiters, all goes into p1.  We
       assume p1, p2, and p3 are large enough. */

    *p1 = *p2 = *p3 = '\0';

    ch = strchr(string, '_');
    if (ch != NULL) {
	len = ch - string;
	(void) strncpy(p1, string, len);
	p1[len] = '\0';
	string = ch + 1;
	rest = &p2;
    } else rest = &p1;

    /* Rest points to where we put the first part */

    ch = strchr(string, '.');
    if (ch != NULL) {
	len = ch - string;
	strncpy(*rest, string, len);
	(*rest)[len] = '\0';
	(void) strcpy(p3, ch+1);
    } else (void) strcpy(*rest, string);
}

/*
 * default path used if environment variable XFILESEARCHPATH
 * is not defined.  Also substitued for %D.
 * The exact value should be documented in the implementation
 * notes for any Xt implementation.
 */
static const char *implementation_default_path(void)
{
#if defined(WIN32)
    static char xfilesearchpath[] = "";

    return xfilesearchpath;
#else
    return XFILESEARCHPATHDEFAULT;
#endif
}


static SubstitutionRec defaultSubs[] = {
    {'N', NULL},
    {'T', NULL},
    {'S', NULL},
    {'C', NULL},
    {'L', NULL},
    {'l', NULL},
    {'t', NULL},
    {'c', NULL}
};


String XtResolvePathname(
    Display *dpy,
    _Xconst char* type,
    _Xconst char* filename,
    _Xconst char* suffix,
    _Xconst char* path,
    Substitution substitutions,
    Cardinal num_substitutions,
    XtFilePredicate predicate)
{
    XtPerDisplay pd;
    static const char *defaultPath = NULL;
    const char *impl_default = implementation_default_path();
    int idef_len = strlen(impl_default);
    char *massagedPath;
    int bytesAllocd, bytesLeft;
    char *ch, *result;
    Substitution merged_substitutions;
    XrmRepresentation db_type;
    XrmValue value;
    XrmName name_list[3];
    XrmClass class_list[3];
    Boolean pathMallocd = False;

    LOCK_PROCESS;
    pd = _XtGetPerDisplay(dpy);
    if (path == NULL) {
#ifndef VMS
	if (defaultPath == NULL) {
	    defaultPath = getenv("XFILESEARCHPATH");
	    if (defaultPath == NULL)
		defaultPath = impl_default;
	}
	path = defaultPath;
#endif /* VMS */
    }

    if (path == NULL)
	path = "";	/* NULL would kill us later */

    if (filename == NULL) {
	filename = XrmClassToString(pd->class);
    }

    bytesAllocd = bytesLeft = 1000;
    massagedPath = ALLOCATE_LOCAL(bytesAllocd);
    if (massagedPath == NULL) _XtAllocError(NULL);

    if (path[0] == ':') {
	strcpy(massagedPath, "%N%S");
	ch = &massagedPath[4];
	bytesLeft -= 4;
    } else ch = massagedPath;

    /* Insert %N%S between adjacent colons
     * and default path for %D.
     * Default path should not have any adjacent colons of its own.
     */

    while (*path != '\0') {
	if (bytesLeft < idef_len) {
	    int bytesUsed = bytesAllocd - bytesLeft;
	    char *new;
	    bytesAllocd +=1000;
	    new = __XtMalloc((Cardinal) bytesAllocd);
	    strncpy( new, massagedPath, bytesUsed );
	    ch = new + bytesUsed;
	    if (pathMallocd)
		XtFree(massagedPath);
	    else
		DEALLOCATE_LOCAL(massagedPath);
	    pathMallocd = True;
	    massagedPath = new;
	    bytesLeft = bytesAllocd - bytesUsed;
	}
	if (*path == '%' && *(path+1) == ':') {
	    *ch++ = '%';
	    *ch++ = ':';
	    path += 2;
	    bytesLeft -= 2;
	    continue;
	}
	if (*path == ':' && *(path+1) == ':') {
	    strcpy(ch, ":%N%S:");
	    ch += 6;
	    bytesLeft -= 6;
	    while (*path == ':') path++;
	    continue;
	}
	if (*path == '%' && *(path+1) == 'D') {
	    strcpy(ch, impl_default);
	    ch += idef_len;
	    bytesLeft -= idef_len;
	    path += 2;
	    continue;
	}
	*ch++ = *path++;
	bytesLeft--;
    }
    *ch = '\0';
#ifdef XNL_DEBUG
    printf("Massaged path: %s\n", massagedPath);
#endif /* XNL_DEBUG */

    if (num_substitutions == 0)
	merged_substitutions = defaultSubs;
    else {
	int i = XtNumber(defaultSubs);
	Substitution sub, def;
	merged_substitutions = sub = (Substitution)
	    ALLOCATE_LOCAL((unsigned)(num_substitutions+i)*sizeof(SubstitutionRec));
	if (sub == NULL) _XtAllocError(NULL);
	for (def = defaultSubs; i--; sub++, def++) sub->match = def->match;
	for (i = num_substitutions; i--; ) *sub++ = *substitutions++;
    }
    merged_substitutions[0].substitution = (String)filename;
    merged_substitutions[1].substitution = (String)type;
    merged_substitutions[2].substitution = (String)suffix;
    name_list[0] = pd->name;
    name_list[1] = XrmPermStringToQuark("customization");
    name_list[2] = NULLQUARK;
    class_list[0] = pd->class;
    class_list[1] = XrmPermStringToQuark("Customization");
    class_list[2] = NULLQUARK;
    if (XrmQGetResource(XrmGetDatabase(dpy), name_list, class_list,
			&db_type, &value) &&
	db_type == _XtQString)
	merged_substitutions[3].substitution = (char *)value.addr;
    else
	merged_substitutions[3].substitution = NULL;
    FillInLangSubs(&merged_substitutions[4], pd);

    result = XtFindFile(massagedPath, merged_substitutions,
			num_substitutions + XtNumber(defaultSubs),
			predicate);

    if (merged_substitutions[5].substitution != NULL)
	XtFree( (XtPointer)merged_substitutions[5].substitution );

    if (merged_substitutions != defaultSubs)
	DEALLOCATE_LOCAL(merged_substitutions);

    if (pathMallocd)
	XtFree(massagedPath);
    else
	DEALLOCATE_LOCAL(massagedPath);

    UNLOCK_PROCESS;
    return result;
}


Boolean XtCallAcceptFocus(
    Widget widget,
    Time *time)
{
    XtAcceptFocusProc ac;
    Boolean retval;
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    LOCK_PROCESS;
    ac = XtClass(widget)->core_class.accept_focus;
    UNLOCK_PROCESS;

    if (ac != NULL)
	retval = (*ac) (widget, time);
    else
	retval = FALSE;
    UNLOCK_APP(app);
    return retval;
}

#ifdef XT_GEO_TATTLER
/**************************************************************************
 GeoTattler:  This is used to debug Geometry management in Xt.

  It uses a pseudo resource XtNgeotattler.

  E.G. if those lines are found in the resource database:

    myapp*draw.XmScale.geoTattler: ON
    *XmScrollBar.geoTattler:ON
    *XmRowColumn.exit_button.geoTattler:ON

   then:

    all the XmScale children of the widget named draw,
    all the XmScrollBars,
    the widget named exit_button in any XmRowColumn

   will return True to the function IsTattled(), and will generate
   outlined trace to stdout.

*************************************************************************/

#define XtNgeoTattler "geoTattler"
#define XtCGeoTattler "GeoTattler"

typedef struct { Boolean   geo_tattler ;} GeoDataRec ;

static XtResource geo_resources[] = {
    { XtNgeoTattler, XtCGeoTattler, XtRBoolean, sizeof(Boolean),
      XtOffsetOf(GeoDataRec, geo_tattler),
      XtRImmediate, (XtPointer) False }
};

/************************************************************************
  This function uses XtGetSubresources to find out if a widget
  needs to be geo-spied by the caller. */
static Boolean IsTattled (Widget widget)
{
    GeoDataRec geo_data ;

    XtGetSubresources(widget, (XtPointer)&geo_data,
                      (String)NULL, (String)NULL,
		      geo_resources, XtNumber(geo_resources),
		      NULL, 0);

    return geo_data.geo_tattler;

}  /* IsTattled */

static int n_tab = 0 ;  /* not MT for now */

void
_XtGeoTab (int direction)  /* +1 or -1 */
{
    n_tab += direction ;
}


void
_XtGeoTrace (Widget widget, ...)
{
    va_list args;
    char *fmt;
    int i ;
    if (IsTattled(widget)) {
	va_start(args, widget);
	fmt = va_arg(args, char *);
	for (i=0; i<n_tab; i++) printf("     ");
	(void) vprintf(fmt, args);
	va_end(args);
    }
}

#endif /* XT_GEO_TATTLER */

