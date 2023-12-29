/*

Copyright 1989, 1994, 1998  The Open Group

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

#if (defined(SUNSHLIB) || defined(AIXSHLIB)) && !defined(SHAREDCODE)
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "IntrinsicI.h"
#include "VarargsI.h"
#include "ShellP.h"
#include "VendorP.h"
#include "CreateI.h"

#if defined(AIXSHLIB)
WidgetClass vendorShellWidgetClass = (WidgetClass) &vendorShellClassRec;

static void
_XtVendorInitialize()
{
    transientShellWidgetClass->core_class.superclass =
        (WidgetClass) &vendorShellClassRec;
    topLevelShellWidgetClass->core_class.superclass =
        (WidgetClass) &vendorShellClassRec;
}

#define VENDORINIT _XtVendorInitialize();

#else

#define VENDORINIT              /* as nothing */

#endif

#ifdef SUNSHLIB
/*
 * _XtInherit needs to be statically linked since it is compared against as
 * well as called.
 */
void
_XtInherit()
{
    extern void __XtInherit();

    __XtInherit();
}
#endif

/*
 * The following routine will be called by every toolkit
 * application, forcing this file to be statically linked.
 *
 * Note: XtInitialize, XtAppInitialize, and XtOpenApplication
 *       call XtToolkitInitialize.
 */

void
XtToolkitInitialize()
{
    extern void _XtToolkitInitialize();

    VENDORINIT _XtToolkitInitialize();
}

Widget
XtInitialize(_Xconst char *name,
             _Xconst char *classname,
             XrmOptionDescRec *options,
             Cardinal num_options, int *argc, String *argv)
{
    extern Widget _XtInitialize();

    VENDORINIT
        return _XtInitialize(name, classname, options, num_options, argc, argv);
}

Widget
XtAppInitialize(XtAppContext *app_context_return,
                _Xconst char *application_class,
                XrmOptionDescRec *options,
                Cardinal num_options,
                int *argc_in_out,
                String *argv_in_out,
                String *fallback_resources,
                ArgList args_in, Cardinal num_args_in)
{
    extern Widget _XtAppInitialize();

    VENDORINIT
        return _XtAppInitialize(app_context_return, application_class, options,
                                num_options, argc_in_out, argv_in_out,
                                fallback_resources, args_in, num_args_in);
}

Widget
XtVaAppInitialize(XtAppContext *app_context_return,
                  _Xconst char *application_class,
                  XrmOptionDescList options,
                  Cardinal num_options,
                  int *argc_in_out,
                  String *argv_in_out, String *fallback_resources, ...)
{
    va_list var;
    extern Widget _XtVaAppInitialize();

    VENDORINIT va_start(var, fallback_resources);

    return _XtVaAppInitialize(app_context_return, application_class, options,
                              num_options, argc_in_out, argv_in_out,
                              fallback_resources, var);
}

Widget
XtOpenApplication(XtAppContext *app_context_return,
                  _Xconst char *application_class,
                  XrmOptionDescRec *options,
                  Cardinal num_options,
                  int *argc_in_out,
                  String *argv_in_out,
                  String *fallback_resources,
                  WidgetClass widget_class,
                  ArgList args_in, Cardinal num_args_in)
{
    extern Widget _XtOpenApplication();

    VENDORINIT
        return _XtOpenApplication(app_context_return, application_class,
                                  options, num_options, argc_in_out,
                                  argv_in_out, fallback_resources, widget_class,
                                  args_in, num_args_in);
}

Widget
XtVaOpenApplication(XtAppContext *app_context_return,
                    _Xconst char *application_class,
                    XrmOptionDescList options,
                    Cardinal num_options,
                    int *argc_in_out,
                    String *argv_in_out,
                    String *fallback_resources, WidgetClass widget_class, ...)
{
    va_list var;
    extern Widget _XtVaOpenApplication();

    VENDORINIT va_start(var, widget_class);

    return _XtVaOpenApplication(app_context_return, application_class, options,
                                num_options, argc_in_out, argv_in_out,
                                fallback_resources, widget_class, var);
}

#else

#ifndef lint
static int dummy;               /* avoid warning from ranlib */
#endif

#endif                          /* SUNSHLIB or AIXSHLIB */

#if defined(SUNSHLIB) && !defined(SHAREDCODE)

int _XtInheritTranslations = 0;

extern CompositeClassRec compositeClassRec;
WidgetClass compositeWidgetClass = (WidgetClass) &compositeClassRec;

extern ConstraintClassRec constraintClassRec;
WidgetClass constraintWidgetClass = (WidgetClass) &constraintClassRec;

extern WidgetClassRec widgetClassRec;
WidgetClass widgetClass = &widgetClassRec;
WidgetClass coreWidgetClass = &widgetClassRec;

extern ObjectClassRec objectClassRec;
WidgetClass objectClass = (WidgetClass) &objectClassRec;

extern RectObjClassRec rectObjClassRec;
WidgetClass rectObjClass = (WidgetClass) &rectObjClassRec;

extern ShellClassRec shellClassRec;
WidgetClass shellWidgetClass = (WidgetClass) &shellClassRec;

extern OverrideShellClassRec overrideShellClassRec;
WidgetClass overrideShellWidgetClass = (WidgetClass) &overrideShellClassRec;

extern WMShellClassRec wmShellClassRec;
WidgetClass wmShellWidgetClass = (WidgetClass) &wmShellClassRec;

extern TransientShellClassRec transientShellClassRec;
WidgetClass transientShellWidgetClass = (WidgetClass) &transientShellClassRec;

extern TopLevelShellClassRec topLevelShellClassRec;
WidgetClass topLevelShellWidgetClass = (WidgetClass) &topLevelShellClassRec;

extern ApplicationShellClassRec applicationShellClassRec;
WidgetClass applicationShellWidgetClass =
    (WidgetClass) &applicationShellClassRec;

extern SessionShellClassRec sessionShellClassRec;
WidgetClass sessionShellWidgetClass = (WidgetClass) &sessionShellClassRec;

extern HookObjClassRec hookObjClassRec;
WidgetClass hookObjectClass = (WidgetClass) &hookObjClassRec;

#endif                          /* SUNSHLIB */
