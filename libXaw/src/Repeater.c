/*
 *
Copyright 1990, 1994, 1998  The Open Group

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
 * Author:  Jim Fulton, MIT X Consortium
 *
 * This widget is used for press-and-hold style buttons.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/RepeaterP.h>
#include <X11/Xaw/XawInit.h>

#define DO_CALLBACK(rw) \
XtCallCallbackList((Widget)rw, rw->command.callbacks, NULL)

#define ADD_TIMEOUT(rw, delay)					\
XtAppAddTimeOut(XtWidgetToApplicationContext((Widget)rw),	\
		delay, tic, (XtPointer)rw)

#define CLEAR_TIMEOUT(rw) \
if ((rw)->repeater.timer) {			\
    XtRemoveTimeOut((rw)->repeater.timer);	\
    (rw)->repeater.timer = 0;			\
}

/*
 * Class Methods
 */
static void XawRepeaterInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawRepeaterDestroy(Widget);
static Boolean XawRepeaterSetValues(Widget, Widget, Widget,
				    ArgList, Cardinal*);

/*
 * Prototypes
 */
static void tic(XtPointer, XtIntervalId*);

/*
 * Actions
 */
static void ActionStart(Widget, XEvent*, String*, Cardinal*);
static void ActionStop(Widget, XEvent*, String*, Cardinal*);

/*
 * Initialization
 */
static char defaultTranslations[] =
"<Enter>:"	"highlight()\n"
"<Leave>:"	"unhighlight()\n"
"<Btn1Down>:"	"set() start()\n"
"<Btn1Up>:"	"stop() unset()\n"
;

static XtActionsRec actions[] = {
  {"start",	ActionStart},
  {"stop",	ActionStop},
};

#define offset(field)	XtOffsetOf(RepeaterRec, repeater.field)
static XtResource resources[] = {
  {
    XtNdecay,
    XtCDecay,
    XtRInt,
    sizeof(int),
    offset(decay),
    XtRImmediate,
    (XtPointer)REP_DEF_DECAY
  },
  {
    XtNinitialDelay,
    XtCDelay,
    XtRInt,
    sizeof(int),
    offset(initial_delay),
    XtRImmediate,
    (XtPointer)REP_DEF_INITIAL_DELAY
  },
  {
    XtNminimumDelay,
    XtCMinimumDelay,
    XtRInt,
    sizeof(int),
    offset(minimum_delay),
    XtRImmediate,
    (XtPointer)REP_DEF_MINIMUM_DELAY
  },
  {
    XtNrepeatDelay,
    XtCDelay,
    XtRInt,
    sizeof(int),
    offset(repeat_delay),
    XtRImmediate,
    (XtPointer)REP_DEF_REPEAT_DELAY
  },
  {
    XtNflash,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    offset(flash),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNstartCallback,
    XtCStartCallback,
    XtRCallback,
    sizeof(XtPointer),
    offset(start_callbacks),
    XtRImmediate,
    NULL
  },
  {
    XtNstopCallback,
    XtCStopCallback,
    XtRCallback,
    sizeof(XtPointer),
    offset(stop_callbacks),
    XtRImmediate,
    NULL
  },
};
#undef offset

#define Superclass	(&commandClassRec)
RepeaterClassRec repeaterClassRec = {
  /* core */
  {
    (WidgetClass)Superclass,		/* superclass */
    "Repeater",				/* class_name */
    sizeof(RepeaterRec),		/* widget_size */
    XawInitializeWidgetSet,		/* class_initialize */
    NULL,				/* class_part_initialize */
    False,				/* class_inited */
    XawRepeaterInitialize,		/* initialize */
    NULL,				/* initialize_hook */
    XtInheritRealize,			/* realize */
    actions,				/* actions */
    XtNumber(actions),			/* num_actions */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    True,				/* compress_motion */
    True,				/* compress_exposure */
    True,				/* compress_enterleave */
    False,				/* visible_interest */
    XawRepeaterDestroy,			/* destroy */
    XtInheritResize,			/* resize */
    XtInheritExpose,			/* expose */
    XawRepeaterSetValues,		/* set_values */
    NULL,				/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    defaultTranslations,		/* tm_table */
    XtInheritQueryGeometry,		/* query_geometry */
    XtInheritDisplayAccelerator,	/* display_accelerator */
    NULL,				/* extension */
  },
  /* simple */
  {
    XtInheritChangeSensitive,		/* change_sensitive */
  },
  /* label */
  {
    NULL,				/* extension */
  },
  /* command */
  {
    NULL,				/* extension */
  },
  /* repeater */
  {
    NULL,				/* extension */
  },
};

WidgetClass repeaterWidgetClass = (WidgetClass) &repeaterClassRec;


/*
 * Implementation
 */
/*ARGSUSED*/
static void
tic(XtPointer client_data, XtIntervalId *id _X_UNUSED)
{
    RepeaterWidget rw = (RepeaterWidget)client_data;

    rw->repeater.timer = 0;		/* timer is removed */
    if (rw->repeater.flash) {
	Widget w = (Widget)rw;

	XClearWindow(XtDisplay(w), XtWindow(w));
	XtCallActionProc(w, "reset", NULL, NULL, 0);
	XClearWindow(XtDisplay(w), XtWindow(w));
	XtCallActionProc(w, "set", NULL, NULL, 0);
    }
    DO_CALLBACK(rw);

    rw->repeater.timer = ADD_TIMEOUT(rw, (unsigned long)rw->repeater.next_delay);

    if (rw->repeater.decay) {
	rw->repeater.next_delay -= rw->repeater.decay;
	if (rw->repeater.next_delay < rw->repeater.minimum_delay)
	    rw->repeater.next_delay = rw->repeater.minimum_delay;
    }
}

/*ARGSUSED*/
static void
XawRepeaterInitialize(Widget greq _X_UNUSED, Widget gnew,
		      ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    RepeaterWidget cnew = (RepeaterWidget)gnew;

    if (cnew->repeater.minimum_delay < 0)
	cnew->repeater.minimum_delay = 0;
    cnew->repeater.timer = 0;
}

static void
XawRepeaterDestroy(Widget gw)
{
    CLEAR_TIMEOUT((RepeaterWidget)gw);
}

/*ARGSUSED*/
static Boolean
XawRepeaterSetValues(Widget gcur, Widget greq _X_UNUSED, Widget gnew,
		     ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    RepeaterWidget cur = (RepeaterWidget)gcur;
    RepeaterWidget cnew = (RepeaterWidget)gnew;

    if (cur->repeater.minimum_delay != cnew->repeater.minimum_delay) {
	if (cnew->repeater.next_delay < cnew->repeater.minimum_delay)
	    cnew->repeater.next_delay = cnew->repeater.minimum_delay;
    }

    return (False);
}

/*ARGSUSED*/
static void
ActionStart(Widget gw, XEvent *event _X_UNUSED, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    RepeaterWidget rw = (RepeaterWidget)gw;

    CLEAR_TIMEOUT(rw);
    if (rw->repeater.start_callbacks)
	XtCallCallbackList(gw, rw->repeater.start_callbacks, NULL);

    DO_CALLBACK(rw);
    rw->repeater.timer = ADD_TIMEOUT(rw, (unsigned long)rw->repeater.initial_delay);
    rw->repeater.next_delay = rw->repeater.repeat_delay;
}

/*ARGSUSED*/
static void
ActionStop(Widget gw, XEvent *event _X_UNUSED, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    RepeaterWidget rw = (RepeaterWidget)gw;

    CLEAR_TIMEOUT((RepeaterWidget)gw);
    if (rw->repeater.stop_callbacks)
	XtCallCallbackList(gw, rw->repeater.stop_callbacks, NULL);
}
