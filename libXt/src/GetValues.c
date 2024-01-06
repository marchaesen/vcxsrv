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
#include "StringDefs.h"

static int
GetValues(char *const base,             /* Base address to fetch values from */
          XrmResourceList *res,         /* The current resource values.      */
          register Cardinal num_resources, /* number of items in resources   */
          ArgList args,                 /* The resource values requested     */
          Cardinal num_args)            /* number of items in arg list       */
{                
    register ArgList arg;
    register Cardinal i;
    register XrmName argName;
    register XrmResourceList *xrmres;
    int translation_arg_num = -1;
    static XrmQuark QCallback = NULLQUARK;
    static XrmQuark QTranslationTable = NULLQUARK;

    LOCK_PROCESS;
    if (QCallback == NULLQUARK) {
        QCallback = XrmPermStringToQuark(XtRCallback);
        QTranslationTable = XrmPermStringToQuark(XtRTranslationTable);
    }
    UNLOCK_PROCESS;

    /* Resource lists should be in compiled form already  */

    for (arg = args; num_args != 0; num_args--, arg++) {
        argName = StringToName(arg->name);
        for (xrmres = res, i = 0; i < num_resources; i++, xrmres++) {
            if (argName == (*xrmres)->xrm_name) {
                /* hack; do special cases here instead of a get_values_hook
                 * because get_values_hook looses info as to
                 * whether arg->value == NULL for ancient compatibility
                 * mode in _XtCopyToArg.  It helps performance, too...
                 */
                if ((*xrmres)->xrm_type == QCallback) {
                    XtCallbackList callback =
                        _XtGetCallbackList((InternalCallbackList *)
                                           (base - (*xrmres)->xrm_offset - 1));

                    _XtCopyToArg((char *) &callback, &arg->value,
                                 (*xrmres)->xrm_size);
                }
                else if ((*xrmres)->xrm_type == QTranslationTable)
                    translation_arg_num = (int) (arg - args);
                else {
                    _XtCopyToArg(base - (*xrmres)->xrm_offset - 1,
                                 &arg->value, (*xrmres)->xrm_size);
                }
                break;
            }
        }
    }
    return translation_arg_num;
}                               /* GetValues */

static void
CallGetValuesHook(WidgetClass widget_class,
                  Widget w,
                  ArgList args,
                  Cardinal num_args)
{
    WidgetClass superclass;
    XtArgsProc get_values_hook;

    LOCK_PROCESS;
    superclass = widget_class->core_class.superclass;
    UNLOCK_PROCESS;
    if (superclass != NULL)
        CallGetValuesHook(superclass, w, args, num_args);

    LOCK_PROCESS;
    get_values_hook = widget_class->core_class.get_values_hook;
    UNLOCK_PROCESS;
    if (get_values_hook != NULL)
        (*get_values_hook) (w, args, &num_args);
}

static void
CallConstraintGetValuesHook(WidgetClass widget_class,
                            Widget w,
                            ArgList args,
                            Cardinal num_args)
{
    ConstraintClassExtension ext;

    LOCK_PROCESS;
    if (widget_class->core_class.superclass->core_class.
        class_inited & ConstraintClassFlag) {
        CallConstraintGetValuesHook(widget_class->core_class.superclass, w,
                                    args, num_args);
    }

    for (ext = (ConstraintClassExtension) ((ConstraintWidgetClass) widget_class)
         ->constraint_class.extension;
         ext != NULL && ext->record_type != NULLQUARK;
         ext = (ConstraintClassExtension) ext->next_extension);

    if (ext != NULL) {
        if (ext->version == XtConstraintExtensionVersion
            && ext->record_size == sizeof(ConstraintClassExtensionRec)) {
            if (ext->get_values_hook != NULL)
                (*(ext->get_values_hook)) (w, args, &num_args);
        }
        else {
            String params[1];
            Cardinal num_params = 1;

            params[0] = widget_class->core_class.class_name;
            XtAppWarningMsg(XtWidgetToApplicationContext(w),
                            "invalidExtension", "xtCreateWidget",
                            XtCXtToolkitError,
                            "widget class %s has invalid ConstraintClassExtension record",
                            params, &num_params);
        }
    }
    UNLOCK_PROCESS;
}

void
XtGetValues(register Widget w,
            register ArgList args,
            register Cardinal num_args)
{
    WidgetClass wc;
    int targ;
    XtAppContext app = XtWidgetToApplicationContext(w);

    if (num_args == 0) {
        return;
    } else if (args == NULL) {
        XtAppErrorMsg(app,
                      "invalidArgCount", "xtGetValues", XtCXtToolkitError,
                      "Argument count > 0 on NULL argument list in XtGetValues",
                      NULL, NULL);
    }

    LOCK_APP(app);
    wc = XtClass(w);
    LOCK_PROCESS;
    /* Get widget values */
    targ = GetValues((char *) w, (XrmResourceList *) wc->core_class.resources,
                     wc->core_class.num_resources, args, num_args);
    UNLOCK_PROCESS;
    if (targ != -1 && XtIsWidget(w)) {
        XtTranslations translations = _XtGetTranslationValue(w);

        _XtCopyToArg((char *) &translations, &args[targ].value,
                     sizeof(XtTranslations));
    }

    /* Get constraint values if necessary */
    /* constraints may be NULL if constraint_size==0 */
    if (XtParent(w) != NULL && !XtIsShell(w) && XtIsConstraint(XtParent(w)) &&
        w->core.constraints) {
        ConstraintWidgetClass cwc
            = (ConstraintWidgetClass) XtClass(XtParent(w));
        LOCK_PROCESS;
        GetValues((char *) w->core.constraints,
                  (XrmResourceList *) (cwc->constraint_class.resources),
                  cwc->constraint_class.num_resources, args, num_args);
        UNLOCK_PROCESS;
    }
    /* Notify any class procedures that we have performed get_values */
    CallGetValuesHook(wc, w, args, num_args);

    /* Notify constraint get_values if necessary */
    if (XtParent(w) != NULL && !XtIsShell(w) && XtIsConstraint(XtParent(w)))
        CallConstraintGetValuesHook(XtClass(XtParent(w)), w, args, num_args);
    UNLOCK_APP(app);
}                               /* XtGetValues */

void
XtGetSubvalues(XtPointer base,          /* Base address to fetch values from */
               XtResourceList resources,/* The current resource values.      */
               Cardinal num_resources,  /* number of items in resources      */
               ArgList args,            /* The resource values requested */
               Cardinal num_args)       /* number of items in arg list       */
{
    XrmResourceList *xrmres;

    xrmres = _XtCreateIndirectionTable(resources, num_resources);
    GetValues((char *) base, xrmres, num_resources, args, num_args);
    XtFree((char *) xrmres);
}
