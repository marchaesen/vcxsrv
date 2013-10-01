/* $Xorg: xclock.c,v 1.4 2001/02/09 02:05:39 xorgcvs Exp $ */
/* $XdotOrg: $ */

/*
 * xclock --  Hacked from Tony Della Fera's much hacked clock program.
 *
 * "-strftime" option added by George Belotsky, Open Light Software Inc.
 */

/*
Copyright 1989, 1998  The Open Group

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
/* $XFree86: xclock.c,v 1.16 2002/10/21 13:33:08 alanh Exp $ */


#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <X11/Xatom.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include "Clock.h"
#include <X11/Xaw/Cardinals.h>
#include "clock.bit"
#include "clmask.bit"

#ifdef XKB
#include <X11/extensions/XKBbells.h>
#endif

#ifndef NO_I18N
#include <locale.h> /* for setlocale() */
Boolean no_locale = True; /* if True, use old behavior */
#endif

#ifdef HAVE_GETPID
# include <unistd.h>
#endif

/* Command line options table.  Only resources are entered here...there is a
   pass over the remaining options after XtParseCommand is let loose. */

static XrmOptionDescRec options[] = {
{"-chime",	"*clock.chime",		XrmoptionNoArg,		"TRUE"},
{"-hd",		"*clock.hands",		XrmoptionSepArg,	NULL},
{"-hands",	"*clock.hands",		XrmoptionSepArg,	NULL},
{"-hl",		"*clock.highlight",	XrmoptionSepArg,	NULL},
{"-highlight",	"*clock.highlight",	XrmoptionSepArg,	NULL},
{"-update",	"*clock.update",	XrmoptionSepArg,	NULL},
{"-padding",	"*clock.padding",	XrmoptionSepArg,	NULL},
{"-d",		"*clock.analog",	XrmoptionNoArg,		"FALSE"},
{"-digital",	"*clock.analog",	XrmoptionNoArg,		"FALSE"},
{"-analog",	"*clock.analog",	XrmoptionNoArg,		"TRUE"},
{"-twelve",	"*clock.twentyfour",	XrmoptionNoArg,		"FALSE"},
{"-twentyfour",	"*clock.twentyfour",	XrmoptionNoArg,		"TRUE"},
{"-brief",	"*clock.brief",		XrmoptionNoArg,		"TRUE"},
{"-utime",	"*clock.utime",		XrmoptionNoArg,		"TRUE"},
{"-strftime",	"*clock.strftime",	XrmoptionSepArg,	NULL},
#ifdef XRENDER
{"-face",	"*clock.face",		XrmoptionSepArg,	NULL},
{"-render",	"*clock.render",	XrmoptionNoArg,		"TRUE"},
{"-norender",	"*clock.render",	XrmoptionNoArg,		"FALSE"},
{"-sharp",	"*clock.sharp",		XrmoptionNoArg,		"TRUE"},
#endif
};

static void quit ( Widget w, XEvent *event, String *params,
		   Cardinal *num_params );

static XtActionsRec xclock_actions[] = {
    { "quit",	quit },
};

static Atom wm_delete_window;

/*
 * Report the syntax for calling xclock.
 */
static void _X_NORETURN
Syntax(const char *call)
{
    fprintf (stderr, "Usage: %s %s", call,
	    "[-analog] [-bw <pixels>] [-digital] [-brief]\n"
	    "       [-utime] [-strftime <fmt-str>]\n"
	    "       [-fg <color>] [-bg <color>] [-hd <color>]\n"
	    "       [-hl <color>] [-bd <color>]\n"
	    "       [-fn <font_name>] [-help] [-padding <pixels>]\n"
	    "       [-rv] [-update <seconds>] [-display displayname]\n"
#ifdef XRENDER
	    "       [-[no]render] [-face <face name>] [-sharp]\n"
#endif
	    "       [-geometry geom] [-twelve] [-twentyfour]\n\n");
    exit(1);
}

static void _X_NORETURN
die(Widget w, XtPointer client_data, XtPointer call_data)
{
    XCloseDisplay(XtDisplayOfObject(w));
    exit(0);
}

static void
quit(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    Arg arg;

    if (event->type == ClientMessage &&
	event->xclient.data.l[0] != wm_delete_window) {
#ifdef XKB
	XkbStdBell(XtDisplay(w), XtWindow(w), 0, XkbBI_MinorError);
#else
	XBell (XtDisplay(w), 0);
#endif
    } else {
	/* resign from the session */
	XtSetArg(arg, XtNjoinSession, False);
	XtSetValues(w, &arg, ONE);
	die(w, NULL, NULL);
    }
}

static void
save(Widget w, XtPointer client_data, XtPointer call_data)
{
    XtCheckpointToken token = (XtCheckpointToken) call_data;
    /* we have nothing to save */
    token->save_success = True;
}

int
main(int argc, char *argv[])
{
    Widget toplevel;
    Arg arg;
    Pixmap icon_pixmap = None;
    XtAppContext app_con;

#ifndef NO_I18N
    char *locale_name = setlocale(LC_ALL,"");
    XtSetLanguageProc ( NULL, NULL, NULL );

    if(!locale_name || 0 == strcmp(locale_name,"C")) {
	no_locale = True;
    }
    else {
	no_locale = False;
    }
#endif

    toplevel = XtOpenApplication(&app_con, "XClock",
				 options, XtNumber(options), &argc, argv, NULL,
				 sessionShellWidgetClass, NULL, ZERO);
    if (argc != 1) Syntax(argv[0]);
    XtAddCallback(toplevel, XtNdieCallback, die, NULL);
    XtAddCallback(toplevel, XtNsaveCallback, save, NULL);
    XtAppAddActions (app_con, xclock_actions, XtNumber(xclock_actions));

    /*
     * This is a hack so that wm_delete_window will do something useful
     * in this single-window application.
     */
    XtOverrideTranslations(toplevel,
		    XtParseTranslationTable ("<Message>WM_PROTOCOLS: quit()"));

    XtSetArg(arg, XtNiconPixmap, &icon_pixmap);
    XtGetValues(toplevel, &arg, ONE);

    if (icon_pixmap == None) {
	arg.value = (XtArgVal)XCreateBitmapFromData(XtDisplay(toplevel),
				       XtScreen(toplevel)->root,
				       (char *)clock_bits, clock_width, clock_height);
	XtSetValues (toplevel, &arg, ONE);
    }

    XtSetArg(arg, XtNiconMask, &icon_pixmap);
    XtGetValues(toplevel, &arg, ONE);
    if (icon_pixmap == None) {
	arg.value = (XtArgVal)XCreateBitmapFromData(XtDisplay(toplevel),
				       XtScreen(toplevel)->root,
				       (char *)clock_mask_bits, clock_mask_width,
				       clock_mask_height);
	XtSetValues (toplevel, &arg, ONE);
    }

    XtCreateManagedWidget ("clock", clockWidgetClass, toplevel, NULL, ZERO);
    XtRealizeWidget (toplevel);
    wm_delete_window = XInternAtom (XtDisplay(toplevel), "WM_DELETE_WINDOW",
				    False);
    (void) XSetWMProtocols (XtDisplay(toplevel), XtWindow(toplevel),
			    &wm_delete_window, 1);

#ifdef HAVE_GETPID
    {
	unsigned long pid = (unsigned long)getpid();
	XChangeProperty(XtDisplay(toplevel), XtWindow(toplevel),
			XInternAtom(XtDisplay(toplevel), "_NET_WM_PID", False),
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *) &pid, 1);
    }
#endif

    XtAppMainLoop (app_con);
    exit(0);
}
