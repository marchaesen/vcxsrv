/*
 *
Copyright 1989, 1998  The Open Group
Copyright 2003-2004 Roland Mainz <roland.mainz@nrubsig.org>

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
 *
 *
 *			    XawInitializeWidgetSet
 *
 * This routine forces a reference to vendor shell so that the one in this
 * widget is installed.  Any other cross-widget set initialization should be
 * done here as well.  All Athena widgets should include "XawInit.h" and
 * call this routine from their ClassInitialize procs (this routine may be
 * used as the class init proc).
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/Intrinsic.h>
#include <X11/Vendor.h>
#include <X11/Xaw/XawInit.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include "Private.h"

void
XawInitializeWidgetSet(void)
{
    static Boolean firsttime = True;

    if (firsttime) {
	firsttime = False;
#ifndef OLDXAW
	XawPixmapsInitialize();
	XawInitializeDefaultConverters();
#endif
	XtInitializeWidgetClass(vendorShellWidgetClass);
    }
}

/* XawOpenApplication() - mainly identical to XtOpenApplication() but
 * takes a |Display *| and |Screen *| as arguments, too... */
Widget XawOpenApplication(XtAppContext *app_context_return,
                          Display      *dpy,
                          Screen       *screen,
                          String        application_name,
                          String        application_class,
                          WidgetClass   widget_class,
                          int          *argc,
                          _XtString    *argv)
{
    Widget   toplevel;
    Cardinal n;
    Arg      args[2];

    XtToolkitInitialize();
    *app_context_return = XtCreateApplicationContext();
    if( *app_context_return == NULL )
        return NULL;

    XtDisplayInitialize(*app_context_return, dpy,
                        application_name, application_class,
                        NULL, 0,
                        argc, argv);

    n = 0;
    if (screen) {
        XtSetArg(args[n], XtNscreen, screen); n++;
    }
    toplevel = XtAppCreateShell(application_name,
                                application_class,
                                widget_class,
                                dpy,
                                args, n);

    return toplevel;
}

