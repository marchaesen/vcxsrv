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
#include "VarargsI.h"
#include "ShellP.h"
#include "CreateI.h"
#ifndef X_NO_RESOURCE_CONFIGURATION_MANAGEMENT
#include "ResConfigP.h"
#endif
#include <stdio.h>

static _Xconst _XtString XtNxtCreateWidget = "xtCreateWidget";
static _Xconst _XtString XtNxtCreatePopupShell = "xtCreatePopupShell";

static void
CallClassPartInit(WidgetClass ancestor, WidgetClass wc)
{
    if (ancestor->core_class.superclass != NULL) {
        CallClassPartInit(ancestor->core_class.superclass, wc);
    }
    if (ancestor->core_class.class_part_initialize != NULL) {
        (*(ancestor->core_class.class_part_initialize)) (wc);
    }
}

void
XtInitializeWidgetClass(WidgetClass wc)
{
    XtEnum inited;

    LOCK_PROCESS;
    if (wc->core_class.class_inited) {
        UNLOCK_PROCESS;
        return;
    }
    inited = 0x01;
    {
        WidgetClass pc;

#define LeaveIfClass(c, d) if (pc == c) { inited = d; break; }
        for (pc = wc; pc; pc = pc->core_class.superclass) {
            LeaveIfClass(rectObjClass, 0x01 | RectObjClassFlag);
            LeaveIfClass(coreWidgetClass, 0x01 |
                         RectObjClassFlag | WidgetClassFlag);
            LeaveIfClass(compositeWidgetClass, 0x01 |
                         RectObjClassFlag |
                         WidgetClassFlag | CompositeClassFlag);
            LeaveIfClass(constraintWidgetClass, 0x01 |
                         RectObjClassFlag |
                         WidgetClassFlag |
                         CompositeClassFlag | ConstraintClassFlag);
            LeaveIfClass(shellWidgetClass, 0x01 |
                         RectObjClassFlag |
                         WidgetClassFlag | CompositeClassFlag | ShellClassFlag);
            LeaveIfClass(wmShellWidgetClass, 0x01 |
                         RectObjClassFlag |
                         WidgetClassFlag |
                         CompositeClassFlag |
                         ShellClassFlag | WMShellClassFlag);
            LeaveIfClass(topLevelShellWidgetClass, 0x01 |
                         RectObjClassFlag |
                         WidgetClassFlag |
                         CompositeClassFlag |
                         ShellClassFlag | WMShellClassFlag | TopLevelClassFlag);
        }
#undef LeaveIfClass
    }
    if (wc->core_class.version != XtVersion &&
        wc->core_class.version != XtVersionDontCheck) {
        String param[3];
        _Xconst _XtString mismatch =
            "Widget class %s version mismatch (recompilation needed):\n  widget %d vs. intrinsics %d.";
        _Xconst _XtString recompile = "Widget class %s must be re-compiled.";
        Cardinal num_params;

        param[0] = wc->core_class.class_name;
        param[1] = (String) (XtIntPtr) wc->core_class.version;
        param[2] = (String) (XtIntPtr) XtVersion;

        if (wc->core_class.version == (11 * 1000 + 5) ||        /* MIT X11R5 */
            wc->core_class.version == (11 * 1000 + 4)) {        /* MIT X11R4 */
            if ((inited & WMShellClassFlag) &&
                (sizeof(Boolean) != sizeof(char) ||
                 sizeof(Atom) != sizeof(Widget) ||
                 sizeof(Atom) != sizeof(String))) {
                num_params = 3;
                XtWarningMsg("versionMismatch", "widget", XtCXtToolkitError,
                             mismatch, param, &num_params);
                num_params = 1;
                XtErrorMsg("R4orR5versionMismatch", "widget", XtCXtToolkitError,
                           recompile, param, &num_params);

            }
        }
        else if (wc->core_class.version == (11 * 1000 + 3)) {   /* MIT X11R3 */
            if (inited & ShellClassFlag) {
                num_params = 1;
                XtWarningMsg("r3versionMismatch", "widget", XtCXtToolkitError,
                             "Shell Widget class %s binary compiled for R3",
                             param, &num_params);
                XtErrorMsg("R3versionMismatch", "widget", XtCXtToolkitError,
                           recompile, param, &num_params);
            }
        }
        else {
            num_params = 3;
            XtWarningMsg("versionMismatch", "widget", XtCXtToolkitError,
                         mismatch, param, &num_params);
            if (wc->core_class.version == (2 * 1000 + 2)) {     /* MIT X11R2 */
                num_params = 1;
                XtErrorMsg("r2versionMismatch", "widget", XtCXtToolkitError,
                           recompile, param, &num_params);
            }
        }
    }

    if ((wc->core_class.superclass != NULL)
        && (!(wc->core_class.superclass->core_class.class_inited)))
        XtInitializeWidgetClass(wc->core_class.superclass);

    if (wc->core_class.class_initialize != NULL)
        (*(wc->core_class.class_initialize)) ();
    CallClassPartInit(wc, wc);
    wc->core_class.class_inited = inited;
    UNLOCK_PROCESS;
}

static void
CallInitialize(WidgetClass class,
               Widget req_widget,
               Widget new_widget,
               ArgList args,
               Cardinal num_args)
{
    WidgetClass superclass;
    XtInitProc initialize;
    XtArgsProc initialize_hook;

    LOCK_PROCESS;
    superclass = class->core_class.superclass;
    UNLOCK_PROCESS;
    if (superclass)
        CallInitialize(superclass, req_widget, new_widget, args, num_args);
    LOCK_PROCESS;
    initialize = class->core_class.initialize;
    UNLOCK_PROCESS;
    if (initialize)
        (*initialize) (req_widget, new_widget, args, &num_args);
    LOCK_PROCESS;
    initialize_hook = class->core_class.initialize_hook;
    UNLOCK_PROCESS;
    if (initialize_hook)
        (*initialize_hook) (new_widget, args, &num_args);
}

static void
CallConstraintInitialize(ConstraintWidgetClass class,
                         Widget req_widget,
                         Widget new_widget,
                         ArgList args,
                         Cardinal num_args)
{
    WidgetClass superclass;
    XtInitProc initialize;

    LOCK_PROCESS;
    superclass = class->core_class.superclass;
    UNLOCK_PROCESS;
    if (superclass != constraintWidgetClass)
        CallConstraintInitialize((ConstraintWidgetClass) superclass,
                                 req_widget, new_widget, args, num_args);
    LOCK_PROCESS;
    initialize = class->constraint_class.initialize;
    UNLOCK_PROCESS;
    if (initialize)
        (*initialize) (req_widget, new_widget, args, &num_args);
}

static Widget
xtWidgetAlloc(WidgetClass widget_class,
              ConstraintWidgetClass parent_constraint_class,
              Widget parent,
              _Xconst _XtString name,
              ArgList args,     /* must be NULL if typed_args is non-NULL */
              Cardinal num_args,
              XtTypedArgList typed_args,     /* must be NULL if args is non-NULL */
              Cardinal num_typed_args)
{
    Widget widget;
    Cardinal csize = 0;
    ObjectClassExtension ext;

    if (widget_class == NULL)
        return 0;

    LOCK_PROCESS;
    if (!(widget_class->core_class.class_inited))
        XtInitializeWidgetClass(widget_class);
    ext = (ObjectClassExtension)
        XtGetClassExtension(widget_class,
                            XtOffsetOf(ObjectClassRec, object_class.extension),
                            NULLQUARK, XtObjectExtensionVersion,
                            sizeof(ObjectClassExtensionRec));
    if (parent_constraint_class)
        csize = parent_constraint_class->constraint_class.constraint_size;
    if (ext && ext->allocate) {
        XtAllocateProc allocate;
        Cardinal extra = 0;
        Cardinal nargs = num_args;
        Cardinal ntyped = num_typed_args;

        allocate = ext->allocate;
        UNLOCK_PROCESS;
        (*allocate) (widget_class, &csize, &extra, args, &nargs,
                     typed_args, &ntyped, &widget, NULL);
    }
    else {
        Cardinal wsize = widget_class->core_class.widget_size;

        UNLOCK_PROCESS;
        if (csize) {
            if (sizeof(struct {
                       char a; double b;}) != (sizeof(struct {
                                                      char a;
                                                      unsigned long b;}) -
                                               sizeof(unsigned long) +
                                               sizeof(double))) {
                if (csize && !(csize & (sizeof(double) - 1)))
                    wsize = (Cardinal) ((wsize + sizeof(double) - 1)
                                        & ~(sizeof(double) - 1));
            }
        }
        widget = (Widget) __XtCalloc(1, (unsigned) (wsize + csize));
        widget->core.constraints =
            (csize ? (XtPointer) ((char *) widget + wsize) : NULL);
    }
    widget->core.self = widget;
    widget->core.parent = parent;
    widget->core.widget_class = widget_class;
    widget->core.xrm_name = StringToName((name != NULL) ? name : "");
    widget->core.being_destroyed =
        (parent != NULL ? parent->core.being_destroyed : FALSE);
    return widget;
}

static void
CompileCallbacks(Widget widget)
{
    CallbackTable offsets;
    int i;

    LOCK_PROCESS;
    offsets = (CallbackTable)
        widget->core.widget_class->core_class.callback_private;

    for (i = (int) (long) *(offsets++); --i >= 0; offsets++) {
        InternalCallbackList *cl = (InternalCallbackList *)
            ((char *) widget - (*offsets)->xrm_offset - 1);

        if (*cl)
            *cl = _XtCompileCallbackList((XtCallbackList) *cl);
    }
    UNLOCK_PROCESS;
}

static Widget
xtCreate(String name,
         String class,
         WidgetClass widget_class,
         Widget parent,
         Screen *default_screen,    /* undefined when creating a nonwidget */
         ArgList args,          /* must be NULL if typed_args is non-NULL */
         Cardinal num_args,
         XtTypedArgList typed_args,  /* must be NULL if args is non-NULL */
         Cardinal num_typed_args,
         ConstraintWidgetClass parent_constraint_class,
         /* NULL if not a subclass of Constraint or if child is popup shell */
         XtWidgetProc post_proc)
{
    /* need to use strictest alignment rules possible in next two decls. */
    double widget_cache[100];
    Widget req_widget;
    XtPointer req_constraints = NULL;
    Cardinal wsize;
    Widget widget;
    XtCacheRef *cache_refs;
    XtCreateHookDataRec call_data;

    widget = xtWidgetAlloc(widget_class, parent_constraint_class, parent,
                           name, args, num_args, typed_args, num_typed_args);

    if (XtIsRectObj(widget)) {
        widget->core.managed = FALSE;
    }
    if (XtIsWidget(widget)) {
        widget->core.name = XrmNameToString(widget->core.xrm_name);
        widget->core.screen = default_screen;
        widget->core.tm.translations = NULL;
        widget->core.window = (Window) 0;
        widget->core.visible = TRUE;
        widget->core.popup_list = NULL;
        widget->core.num_popups = 0;
    };
    LOCK_PROCESS;
    if (XtIsApplicationShell(widget)) {
        ApplicationShellWidget a = (ApplicationShellWidget) widget;

        if (class != NULL)
            a->application.xrm_class = StringToClass(class);
        else
            a->application.xrm_class = widget_class->core_class.xrm_class;
        a->application.class = XrmQuarkToString(a->application.xrm_class);
    }
    UNLOCK_PROCESS;

    /* fetch resources */
    cache_refs = _XtGetResources(widget, args, num_args,
                                 typed_args, &num_typed_args);

    /* Convert typed arg list to arg list */
    if (typed_args != NULL && num_typed_args > 0) {
        Cardinal i;

        args = (ArgList) ALLOCATE_LOCAL(sizeof(Arg) * num_typed_args);
        if (args == NULL)
            _XtAllocError(NULL);
        for (i = 0; i < num_typed_args; i++) {
            args[i].name = typed_args[i].name;
            args[i].value = typed_args[i].value;
        }
        num_args = num_typed_args;
    }

    CompileCallbacks(widget);

    if (cache_refs != NULL) {
        XtAddCallback(widget, XtNdestroyCallback,
                      XtCallbackReleaseCacheRefList, (XtPointer) cache_refs);
    }

    wsize = widget_class->core_class.widget_size;
    req_widget = (Widget) XtStackAlloc(wsize, widget_cache);
    (void) memcpy(req_widget, (char *) widget, (size_t) wsize);
    CallInitialize(XtClass(widget), req_widget, widget, args, num_args);
    if (parent_constraint_class != NULL) {
        double constraint_cache[20];
        Cardinal csize;

        csize = parent_constraint_class->constraint_class.constraint_size;
        if (csize) {
            req_constraints = XtStackAlloc(csize, constraint_cache);
            (void) memcpy(req_constraints, widget->core.constraints,
                           (size_t) csize);
            req_widget->core.constraints = req_constraints;
        }
        else
            req_widget->core.constraints = NULL;
        CallConstraintInitialize(parent_constraint_class, req_widget, widget,
                                 args, num_args);
        if (csize) {
            XtStackFree(req_constraints, constraint_cache);
        }
    }
    XtStackFree((XtPointer) req_widget, widget_cache);
    if (post_proc != (XtWidgetProc) NULL && (parent != NULL)) {
        Widget hookobj;

        (*post_proc) (widget);
        hookobj = XtHooksOfDisplay((default_screen != (Screen *) NULL) ?
                                   default_screen->display :
                                   XtDisplayOfObject(parent));
        if (XtHasCallbacks(hookobj, XtNcreateHook) == XtCallbackHasSome) {

            call_data.type = XtHcreate;
            call_data.widget = widget;
            call_data.args = args;
            call_data.num_args = num_args;
            XtCallCallbackList(hookobj,
                               ((HookObject) hookobj)->hooks.
                               createhook_callbacks, (XtPointer) &call_data);
        }
    }
    if (typed_args != NULL) {
        while (num_typed_args-- > 0) {

            /* In GetResources we may have dynamically alloc'd store to hold */
            /* a copy of a resource which was larger then sizeof(XtArgVal). */
            /* We must free this store now in order to prevent a memory leak */
            /* A typed arg that has a converted value in dynamic store has a */
            /* negated size field. */

            if (typed_args->type != NULL && typed_args->size < 0) {
                XtFree((char *) typed_args->value);
                typed_args->size = -(typed_args->size);
            }
            typed_args++;
        }
        DEALLOCATE_LOCAL((char *) args);
    }
    return (widget);
}

static void
widgetPostProc(Widget w)
{
    XtWidgetProc insert_child;
    Widget parent = XtParent(w);
    String param = XtName(w);
    Cardinal num_params = 1;

    if (XtIsComposite(parent)) {
        LOCK_PROCESS;
        insert_child =
            ((CompositeWidgetClass) parent->core.widget_class)->composite_class.
            insert_child;
        UNLOCK_PROCESS;
    }
    else {
        return;
    }
    if (insert_child == NULL) {
        XtAppErrorMsg(XtWidgetToApplicationContext(parent),
                      "nullProc", "insertChild", XtCXtToolkitError,
                      "\"%s\" parent has NULL insert_child method",
                      &param, &num_params);
    }
    else {
        (*insert_child) (w);
    }
}

Widget
_XtCreateWidget(String name,
                WidgetClass widget_class,
                Widget parent,
                ArgList args,
                Cardinal num_args,
                XtTypedArgList typed_args,
                Cardinal num_typed_args)
{
    register Widget widget;
    ConstraintWidgetClass cwc;
    Screen *default_screen;
    XtEnum class_inited;
    String params[3];
    Cardinal num_params;

    params[0] = name;
    num_params = 1;

    if (parent == NULL) {
        XtErrorMsg("invalidParent", XtNxtCreateWidget, XtCXtToolkitError,
                   "XtCreateWidget \"%s\" requires non-NULL parent",
                   params, &num_params);
    }
    else if (widget_class == NULL) {
        XtAppErrorMsg(XtWidgetToApplicationContext(parent),
                      "invalidClass", XtNxtCreateWidget, XtCXtToolkitError,
                      "XtCreateWidget \"%s\" requires non-NULL widget class",
                      params, &num_params);
    }
    LOCK_PROCESS;
    if (!widget_class->core_class.class_inited)
        XtInitializeWidgetClass(widget_class);
    class_inited = widget_class->core_class.class_inited;
    UNLOCK_PROCESS;
    if ((class_inited & WidgetClassFlag) == 0) {
        /* not a widget */
        default_screen = NULL;
        if (XtIsComposite(parent)) {
            CompositeClassExtension ext;

            ext = (CompositeClassExtension)
                XtGetClassExtension(XtClass(parent),
                                    XtOffsetOf(CompositeClassRec,
                                               composite_class.extension),
                                    NULLQUARK, 1L, (Cardinal) 0);
            LOCK_PROCESS;
            if (ext &&
                (ext->version > XtCompositeExtensionVersion ||
                 ext->record_size > sizeof(CompositeClassExtensionRec))) {
                params[1] = XtClass(parent)->core_class.class_name;
                num_params = 2;
                XtAppWarningMsg(XtWidgetToApplicationContext(parent),
                                "invalidExtension", XtNxtCreateWidget,
                                XtCXtToolkitError,
                                "widget \"%s\" class %s has invalid CompositeClassExtension record",
                                params, &num_params);
            }
            if (!ext || !ext->accepts_objects) {
                params[1] = XtName(parent);
                num_params = 2;
                XtAppErrorMsg(XtWidgetToApplicationContext(parent),
                              "nonWidget", XtNxtCreateWidget, XtCXtToolkitError,
                              "attempt to add non-widget child \"%s\" to parent \"%s\" which supports only widgets",
                              params, &num_params);
            }
            UNLOCK_PROCESS;
        }
    }
    else {
        default_screen = parent->core.screen;
    }

    if (XtIsConstraint(parent)) {
        cwc = (ConstraintWidgetClass) parent->core.widget_class;
    }
    else {
        cwc = NULL;
    }
    widget = xtCreate(name, (char *) NULL, widget_class, parent,
                      default_screen, args, num_args,
                      typed_args, num_typed_args, cwc, widgetPostProc);
    return (widget);
}

Widget
XtCreateWidget(_Xconst char *name,
               WidgetClass widget_class,
               Widget parent,
               ArgList args,
               Cardinal num_args)
{
    Widget retval;

    WIDGET_TO_APPCON(parent);

    LOCK_APP(app);
    retval =
        _XtCreateWidget((String) name, widget_class, parent, args, num_args,
                        (XtTypedArgList) NULL, (Cardinal) 0);
    UNLOCK_APP(app);
    return retval;
}

Widget
XtCreateManagedWidget(_Xconst char *name,
                      WidgetClass widget_class,
                      Widget parent,
                      ArgList args,
                      Cardinal num_args)
{
    register Widget widget;

    WIDGET_TO_APPCON(parent);

    LOCK_APP(app);
    XtCheckSubclass(parent, compositeWidgetClass, "in XtCreateManagedWidget");
    widget = _XtCreateWidget((String) name, widget_class, parent, args,
                             num_args, (XtTypedArgList) NULL, (Cardinal) 0);
    XtManageChild(widget);
    UNLOCK_APP(app);
    return widget;
}

static void
popupPostProc(Widget w)
{
    Widget parent = XtParent(w);

    parent->core.popup_list = XtReallocArray(parent->core.popup_list,
                                             (parent->core.num_popups + 1),
                                             (Cardinal) sizeof(Widget));
    parent->core.popup_list[parent->core.num_popups++] = w;
}

Widget
_XtCreatePopupShell(String name,
                    WidgetClass widget_class,
                    Widget parent,
                    ArgList args,
                    Cardinal num_args,
                    XtTypedArgList typed_args,
                    Cardinal num_typed_args)
{
    register Widget widget;
    Screen *default_screen;

    if (parent == NULL) {
        XtErrorMsg("invalidParent", XtNxtCreatePopupShell, XtCXtToolkitError,
                   "XtCreatePopupShell requires non-NULL parent", NULL, NULL);
    }
    else if (widget_class == NULL) {
        XtAppErrorMsg(XtWidgetToApplicationContext(parent),
                      "invalidClass", XtNxtCreatePopupShell, XtCXtToolkitError,
                      "XtCreatePopupShell requires non-NULL widget class",
                      NULL, NULL);
    }
    XtCheckSubclass(parent, coreWidgetClass, "in XtCreatePopupShell");
    default_screen = parent->core.screen;
    widget = xtCreate(name, (char *) NULL, widget_class, parent,
                      default_screen, args, num_args, typed_args,
                      num_typed_args, (ConstraintWidgetClass) NULL,
                      popupPostProc);

#ifndef X_NO_RESOURCE_CONFIGURATION_MANAGEMENT
    XtAddEventHandler(widget, (EventMask) PropertyChangeMask, FALSE,
                      (XtEventHandler) _XtResourceConfigurationEH, NULL);
#endif
    return (widget);
}

Widget
XtCreatePopupShell(_Xconst char *name,
                   WidgetClass widget_class,
                   Widget parent,
                   ArgList args,
                   Cardinal num_args)
{
    Widget retval;

    WIDGET_TO_APPCON(parent);

    LOCK_APP(app);
    retval = _XtCreatePopupShell((String) name, widget_class, parent, args,
                                 num_args, (XtTypedArgList) NULL, (Cardinal) 0);
    UNLOCK_APP(app);
    return retval;
}

Widget
_XtAppCreateShell(String name,
                  String class,
                  WidgetClass widget_class,
                  Display *display,
                  ArgList args,
                  Cardinal num_args,
                  XtTypedArgList typed_args,
                  Cardinal num_typed_args)
{
    Widget shell;

    if (widget_class == NULL) {
        XtAppErrorMsg(XtDisplayToApplicationContext(display),
                      "invalidClass", "xtAppCreateShell", XtCXtToolkitError,
                      "XtAppCreateShell requires non-NULL widget class",
                      NULL, NULL);
    }
    if (name == NULL)
        name = XrmNameToString(_XtGetPerDisplay(display)->name);
    shell = xtCreate(name, class, widget_class, (Widget) NULL,
                     (Screen *) DefaultScreenOfDisplay(display),
                     args, num_args, typed_args, num_typed_args,
                     (ConstraintWidgetClass) NULL, _XtAddShellToHookObj);

#ifndef X_NO_RESOURCE_CONFIGURATION_MANAGEMENT
    XtAddEventHandler(shell, (EventMask) PropertyChangeMask, FALSE,
                      (XtEventHandler) _XtResourceConfigurationEH, NULL);
#endif

    return shell;
}

Widget
XtAppCreateShell(_Xconst char *name,
                 _Xconst char *class,
                 WidgetClass widget_class,
                 Display *display, ArgList args, Cardinal num_args)
{
    Widget retval;
    DPY_TO_APPCON(display);

    LOCK_APP(app);
    retval = _XtAppCreateShell((String) name, (String) class, widget_class,
                               display, args, num_args, (XtTypedArgList) NULL,
                               (Cardinal) 0);
    UNLOCK_APP(app);
    return retval;
}

Widget
XtCreateApplicationShell(_Xconst char *name _X_UNUSED,
                         WidgetClass widget_class,
                         ArgList args,
                         Cardinal num_args)
{
    Widget retval;
    Display *dpy;
    XrmClass class;
    XtAppContext app = _XtDefaultAppContext();

    LOCK_APP(app);
    dpy = app->list[0];
    class = _XtGetPerDisplay(dpy)->class;

    retval =
        _XtAppCreateShell((String) NULL, XrmQuarkToString((XrmQuark) class),
                          widget_class, dpy, args, num_args,
                          (XtTypedArgList) NULL, (Cardinal) 0);
    UNLOCK_APP(app);
    return retval;
}

Widget
_XtCreateHookObj(Screen *screen)
{
    Widget req_widget;
    double widget_cache[100];
    Cardinal wsize = 0;
    Widget hookobj = xtWidgetAlloc(hookObjectClass,
                                   (ConstraintWidgetClass) NULL,
                                   (Widget) NULL, "hooks",
                                   (ArgList) NULL, (Cardinal) 0,
                                   (XtTypedArgList) NULL, (Cardinal) 0);

    ((HookObject) hookobj)->hooks.screen = screen;
    (void) _XtGetResources(hookobj, (ArgList) NULL, 0,
                           (XtTypedArgList) NULL, &wsize);
    CompileCallbacks(hookobj);
    wsize = hookObjectClass->core_class.widget_size;
    req_widget = (Widget) XtStackAlloc(wsize, widget_cache);
    (void) memcpy(req_widget, (char *) hookobj, (size_t) wsize);
    CallInitialize(hookObjectClass, req_widget, hookobj,
                   (ArgList) NULL, (Cardinal) 0);
    XtStackFree((XtPointer) req_widget, widget_cache);
    return hookobj;
}
