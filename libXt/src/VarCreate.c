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

/*

Copyright 1885, 1986, 1987, 1988, 1989, 1994, 1998  The Open Group

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
#include "StringDefs.h"
#include "Shell.h"
#include "VarargsI.h"
#include "CreateI.h"

#if (defined(SUNSHLIB) || defined(AIXSHLIB)) && defined(SHAREDCODE)
#define XtToolkitInitialize _XtToolkitInitialize
#endif                          /* (SUNSHLIB || AIXSHLIB) && SHAREDCODE */

static Widget
_XtVaCreateWidget(String name,
                  WidgetClass widget_class,
                  Widget parent,
                  va_list var,
                  int count)
{
    register Widget widget;
    XtTypedArgList typed_args = NULL;
    Cardinal num_args;

    _XtVaToTypedArgList(var, count, &typed_args, &num_args);

    widget = _XtCreateWidget(name, widget_class, parent, (ArgList) NULL,
                             (Cardinal) 0, typed_args, num_args);

    XtFree((XtPointer) typed_args);

    return widget;
}

Widget
XtVaCreateWidget(_Xconst char *name,
                 WidgetClass widget_class,
                 Widget parent,
                 ...)
{
    va_list var;
    register Widget widget;
    int total_count, typed_count;

    WIDGET_TO_APPCON(parent);

    LOCK_APP(app);
    va_start(var, parent);
    _XtCountVaList(var, &total_count, &typed_count);
    va_end(var);

    va_start(var, parent);
    widget = _XtVaCreateWidget((String) name, widget_class, parent, var,
                               total_count);
    va_end(var);
    UNLOCK_APP(app);
    return widget;
}

Widget
XtVaCreateManagedWidget(_Xconst char *name,
                        WidgetClass widget_class,
                        Widget parent,
                        ...)
{
    va_list var;
    register Widget widget;
    int total_count, typed_count;

    WIDGET_TO_APPCON(parent);

    LOCK_APP(app);
    va_start(var, parent);
    _XtCountVaList(var, &total_count, &typed_count);
    va_end(var);

    va_start(var, parent);
    widget = _XtVaCreateWidget((String) name, widget_class, parent, var,
                               total_count);
    XtManageChild(widget);
    va_end(var);
    UNLOCK_APP(app);
    return widget;
}

Widget
XtVaAppCreateShell(_Xconst char *name,
                   _Xconst char *class,
                   WidgetClass widget_class,
                   Display *display,
                   ...)
{
    va_list var;
    register Widget widget;
    XtTypedArgList typed_args = NULL;
    Cardinal num_args;
    int total_count, typed_count;
    DPY_TO_APPCON(display);

    LOCK_APP(app);
    va_start(var, display);

    _XtCountVaList(var, &total_count, &typed_count);
    va_end(var);

    va_start(var, display);

    _XtVaToTypedArgList(var, total_count, &typed_args, &num_args);
    widget = _XtAppCreateShell((String) name, (String) class, widget_class,
                               display, (ArgList) NULL, (Cardinal) 0,
                               typed_args, num_args);

    XtFree((XtPointer) typed_args);

    va_end(var);
    UNLOCK_APP(app);
    return widget;
}

Widget
XtVaCreatePopupShell(_Xconst char *name,
                     WidgetClass widget_class,
                     Widget parent,
                     ...)
{
    va_list var;
    register Widget widget;
    XtTypedArgList typed_args = NULL;
    Cardinal num_args;
    int total_count, typed_count;

    WIDGET_TO_APPCON(parent);

    LOCK_APP(app);
    va_start(var, parent);
    _XtCountVaList(var, &total_count, &typed_count);
    va_end(var);

    va_start(var, parent);

    _XtVaToTypedArgList(var, total_count, &typed_args, &num_args);
    widget = _XtCreatePopupShell((String) name, widget_class, parent,
                                 (ArgList) NULL, (Cardinal) 0, typed_args,
                                 num_args);

    XtFree((XtPointer) typed_args);

    va_end(var);
    UNLOCK_APP(app);
    return widget;
}

void
XtVaSetValues(Widget widget, ...)
{
    va_list var;
    ArgList args = NULL;
    Cardinal num_args;
    int total_count, typed_count;

    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    va_start(var, widget);
    _XtCountVaList(var, &total_count, &typed_count);
    va_end(var);

    va_start(var, widget);

    _XtVaToArgList(widget, var, total_count, &args, &num_args);
    XtSetValues(widget, args, num_args);
    _XtFreeArgList(args, total_count, typed_count);

    UNLOCK_APP(app);
    va_end(var);
}

void
XtVaSetSubvalues(XtPointer base,
                 XtResourceList resources,
                 Cardinal num_resources,
                 ...)
{
    va_list var;
    ArgList args;
    Cardinal num_args;
    int total_count, typed_count;

    va_start(var, num_resources);
    _XtCountVaList(var, &total_count, &typed_count);
    va_end(var);

    if (typed_count != 0) {
        XtWarning("XtVaTypedArg is not valid in XtVaSetSubvalues()\n");
    }

    va_start(var, num_resources);
    _XtVaToArgList((Widget) NULL, var, total_count, &args, &num_args);

    XtSetSubvalues(base, resources, num_resources, args, num_args);

    XtFree((XtPointer) args);

    va_end(var);
}

Widget
_XtVaOpenApplication(XtAppContext *app_context_return,
                     _Xconst char *application_class,
                     XrmOptionDescList options,
                     Cardinal num_options,
                     int *argc_in_out,
                     _XtString *argv_in_out,
                     String *fallback_resources,
                     WidgetClass widget_class,
                     va_list var_args)
{
    XtAppContext app_con;
    Display *dpy;
    register int saved_argc = *argc_in_out;
    Widget root;
    String attr;
    int count = 0;
    XtTypedArgList typed_args;

    XtToolkitInitialize();      /* cannot be moved into _XtAppInit */

    dpy = _XtAppInit(&app_con, (String) application_class, options, num_options,
                     argc_in_out, &argv_in_out, fallback_resources);

    typed_args = (XtTypedArgList) __XtMalloc((unsigned) sizeof(XtTypedArg));
    attr = va_arg(var_args, String);
    for (; attr != NULL; attr = va_arg(var_args, String)) {
        if (strcmp(attr, XtVaTypedArg) == 0) {
            typed_args[count].name = va_arg(var_args, String);
            typed_args[count].type = va_arg(var_args, String);
            typed_args[count].value = va_arg(var_args, XtArgVal);
            typed_args[count].size = va_arg(var_args, int);
        }
        else {
            typed_args[count].name = attr;
            typed_args[count].type = NULL;
            typed_args[count].value = va_arg(var_args, XtArgVal);
            typed_args[count].size = 0;
        }
        count++;
        typed_args = XtReallocArray(typed_args, (Cardinal) count + 1,
                                    (Cardinal) sizeof(XtTypedArg));
    }
    typed_args[count].name = NULL;

    va_end(var_args);

    root =
        XtVaAppCreateShell(NULL, application_class,
                           widget_class, dpy,
                           XtNscreen, (XtArgVal) DefaultScreenOfDisplay(dpy),
                           XtNargc, (XtArgVal) saved_argc,
                           XtNargv, (XtArgVal) argv_in_out,
                           XtVaNestedList, (XtVarArgsList) typed_args, NULL);

    if (app_context_return != NULL)
        *app_context_return = app_con;

    XtFree((XtPointer) typed_args);
    XtFree((XtPointer) argv_in_out);
    return (root);
}

Widget
_XtVaAppInitialize(XtAppContext *app_context_return,
                   _Xconst char *application_class,
                   XrmOptionDescList options,
                   Cardinal num_options,
                   int *argc_in_out,
                   _XtString *argv_in_out,
                   String *fallback_resources,
                   va_list var_args)
{
    return _XtVaOpenApplication(app_context_return, application_class,
                                options, num_options,
                                argc_in_out, argv_in_out, fallback_resources,
                                applicationShellWidgetClass, var_args);
}

#if !((defined(SUNSHLIB) || defined(AIXSHLIB)) && defined(SHAREDCODE))

/*
 * If not used as a shared library, we still need a front end to
 * _XtVaOpenApplication and to _XtVaAppInitialize.
 */

Widget
XtVaOpenApplication(XtAppContext *app_context_return,
                    _Xconst char *application_class,
                    XrmOptionDescList options,
                    Cardinal num_options,
                    int *argc_in_out,
                    _XtString *argv_in_out,
                    String *fallback_resources,
                    WidgetClass widget_class,
                    ...)
{
    Widget code;
    va_list var;

    va_start(var, widget_class);
    code = _XtVaOpenApplication(app_context_return, (String) application_class,
                                options, num_options, argc_in_out, argv_in_out,
                                fallback_resources, widget_class, var);
    va_end(var);
    return code;
}

Widget
XtVaAppInitialize(XtAppContext *app_context_return,
                  _Xconst char *application_class,
                  XrmOptionDescList options,
                  Cardinal num_options,
                  int *argc_in_out,
                  _XtString *argv_in_out,
                  String *fallback_resources,
                  ...)
{
    Widget code;
    va_list var;

    va_start(var, fallback_resources);
    code = _XtVaOpenApplication(app_context_return, (String) application_class,
                                options, num_options, argc_in_out, argv_in_out,
                                fallback_resources,
                                applicationShellWidgetClass, var);
    va_end(var);
    return code;
}

#endif                          /* !((SUNSHLIB || AIXSHLIB) && SHAREDCODE) */
