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

Copyright 1985, 1986, 1987, 1988, 1989, 1998  The Open Group

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
#include "StringDefs.h"

static String XtNxtGetTypedArg = "xtGetTypedArg";

void
XtVaGetSubresources(Widget widget,
                    XtPointer base,
                    _Xconst char *name,
                    _Xconst char *class,
                    XtResourceList resources,
                    Cardinal num_resources,
                    ...)
{
    va_list var;
    XtTypedArgList args;
    Cardinal num_args;
    int total_count, typed_count;

    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    va_start(var, num_resources);
    _XtCountVaList(var, &total_count, &typed_count);
    va_end(var);

    va_start(var, num_resources);

    _XtVaToTypedArgList(var, total_count, &args, &num_args);

    _XtGetSubresources(widget, base, name, class, resources, num_resources,
                       NULL, 0, args, num_args);

    XtFree((XtPointer) args);

    va_end(var);
    UNLOCK_APP(app);
}

void
XtVaGetApplicationResources(Widget widget,
                            XtPointer base,
                            XtResourceList resources,
                            Cardinal num_resources,
                            ...)
{
    va_list var;
    XtTypedArgList args;
    Cardinal num_args;
    int total_count, typed_count;

    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    va_start(var, num_resources);
    _XtCountVaList(var, &total_count, &typed_count);
    va_end(var);

    va_start(var, num_resources);

    _XtVaToTypedArgList(var, total_count, &args, &num_args);

    _XtGetApplicationResources(widget, base, resources, num_resources,
                               NULL, 0, args, num_args);

    XtFree((XtPointer) args);

    va_end(var);
    UNLOCK_APP(app);
}

static void
GetTypedArg(Widget widget,
            XtTypedArgList typed_arg,
            XtResourceList resources,
            Cardinal num_resources)
{
    String from_type = NULL;
    Cardinal from_size = 0;
    XrmValue from_val, to_val;
    register Cardinal i;
    Arg arg;
    XtPointer value;

    /* note we presume that the XtResourceList to be un-compiled */

    for (i = 0; i < num_resources; i++) {
        if (StringToName(typed_arg->name) ==
            StringToName(resources[i].resource_name)) {
            from_type = resources[i].resource_type;
            from_size = resources[i].resource_size;
            break;
        }
    }

    if (i == num_resources) {
        XtAppWarningMsg(XtWidgetToApplicationContext(widget),
                        "unknownType", XtNxtGetTypedArg, XtCXtToolkitError,
                        "Unable to find type of resource for conversion",
                        NULL, NULL);
        return;
    }

    value = ALLOCATE_LOCAL(from_size);
    if (value == NULL)
        _XtAllocError(NULL);
    XtSetArg(arg, typed_arg->name, value);
    XtGetValues(widget, &arg, 1);

    from_val.size = from_size;
    from_val.addr = (XPointer) value;
    to_val.addr = (XPointer) typed_arg->value;
    to_val.size = (unsigned) typed_arg->size;

    if (!XtConvertAndStore(widget, from_type, &from_val,
                           typed_arg->type, &to_val)) {
        if (to_val.size > (unsigned) typed_arg->size) {
            String params[2];
            Cardinal num_params = 2;

            params[0] = typed_arg->type;
            params[1] = XtName(widget);
            XtAppWarningMsg(XtWidgetToApplicationContext(widget),
                            "insufficientSpace", XtNxtGetTypedArg,
                            XtCXtToolkitError,
                            "Insufficient space for converted type '%s' in widget '%s'",
                            params, &num_params);
        }
        else {
            String params[3];
            Cardinal num_params = 3;

            params[0] = from_type;
            params[1] = typed_arg->type;
            params[2] = XtName(widget);
            XtAppWarningMsg(XtWidgetToApplicationContext(widget),
                            "conversionFailed", XtNxtGetTypedArg,
                            XtCXtToolkitError,
                            "Type conversion (%s to %s) failed for widget '%s'",
                            params, &num_params);
        }
    }
    DEALLOCATE_LOCAL(value);
}

static int
GetNestedArg(Widget widget,
             XtTypedArgList avlist,
             ArgList args,
             XtResourceList resources,
             Cardinal num_resources)
{
    int count = 0;

    for (; avlist->name != NULL; avlist++) {
        if (avlist->type != NULL) {
            GetTypedArg(widget, avlist, resources, num_resources);
        }
        else if (strcmp(avlist->name, XtVaNestedList) == 0) {
            count += GetNestedArg(widget, (XtTypedArgList) avlist->value,
                                  args, resources, num_resources);
        }
        else {
            (args + count)->name = avlist->name;
            (args + count)->value = avlist->value;
            ++count;
        }
    }

    return (count);
}

void
XtVaGetValues(Widget widget, ...)
{
    va_list var;
    String attr;
    ArgList args;
    XtTypedArg typed_arg;
    XtResourceList resources = (XtResourceList) NULL;
    Cardinal num_resources;
    int count, total_count, typed_count;

    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    va_start(var, widget);

    _XtCountVaList(var, &total_count, &typed_count);

    if (total_count != typed_count) {
        size_t limit = (size_t) (total_count - typed_count);

        args = XtMallocArray((Cardinal) limit, (Cardinal) sizeof(Arg));
    }
    else
        args = NULL;            /* for lint; really unused */
    va_end(var);

    if (args != NULL) {
        va_start(var, widget);
        for (attr = va_arg(var, String), count = 0; attr != NULL;
             attr = va_arg(var, String)) {
            if (strcmp(attr, XtVaTypedArg) == 0) {
                typed_arg.name = va_arg(var, String);
                typed_arg.type = va_arg(var, String);
                typed_arg.value = va_arg(var, XtArgVal);
                typed_arg.size = va_arg(var, int);

                if (resources == NULL) {
                    XtGetResourceList(XtClass(widget), &resources,
                                      &num_resources);
                }

                GetTypedArg(widget, &typed_arg, resources, num_resources);
            }
            else if (strcmp(attr, XtVaNestedList) == 0) {
                if (resources == NULL) {
                    XtGetResourceList(XtClass(widget), &resources,
                                      &num_resources);
                }

                count += GetNestedArg(widget, va_arg(var, XtTypedArgList),
                                      (args + count), resources, num_resources);
            }
            else {
                args[count].name = attr;
                args[count].value = va_arg(var, XtArgVal);
                count++;
            }
        }
        va_end(var);
    }

    XtFree((XtPointer) resources);

    if (args != NULL) {
        XtGetValues(widget, args, (Cardinal) count);
        XtFree((XtPointer) args);
    }
    UNLOCK_APP(app);
}

void
XtVaGetSubvalues(XtPointer base,
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

    if (typed_count != 0) {
        XtWarning
            ("XtVaTypedArg is an invalid argument to XtVaGetSubvalues()\n");
    }
    va_end(var);

    va_start(var, num_resources);
    _XtVaToArgList((Widget) NULL, var, total_count, &args, &num_args);
    va_end(var);

    XtGetSubvalues(base, resources, num_resources, args, num_args);

    XtFree((XtPointer) args);
}
