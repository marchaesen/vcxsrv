/***********************************************************

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xmu/Drawing.h>
#include <X11/Xaw/ScrollbarP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

#define NoButton	-1
#define PICKLENGTH(widget, x, y)					\
(((widget)->scrollbar.orientation == XtorientHorizontal) ? (x) : (y))

/*
 * Class Methods
 */
static void XawScrollbarClassInitialize(void);
static void XawScrollbarDestroy(Widget);
static void XawScrollbarInitialize(Widget, Widget, ArgList, Cardinal*_args);
static void XawScrollbarRealize(Widget, Mask*, XSetWindowAttributes*);
static void XawScrollbarRedisplay(Widget, XEvent*, Region);
static void XawScrollbarResize(Widget);
static Boolean XawScrollbarSetValues(Widget, Widget, Widget,
				     ArgList, Cardinal*);

/*
 * Prototypes
 */
static Boolean CompareEvents(XEvent*, XEvent*);
static void CreateGC(Widget);
static float FloatInRange(float, float, float);
static float FractionLoc(ScrollbarWidget, int, int);
static void ExtractPosition(XEvent*, Position*, Position*);
static int InRange(int, int, int);
static void FillArea(ScrollbarWidget, int, int, int);
static Bool LookAhead(Widget, XEvent*);
static void PaintThumb(ScrollbarWidget);
static Bool PeekNotifyEvent(Display*, XEvent*, char*);
static void SetDimensions(ScrollbarWidget);

/*
 * Actions
 */
static void EndScroll(Widget, XEvent*, String*, Cardinal*);
static void MoveThumb(Widget, XEvent*, String*, Cardinal*);
static void NotifyScroll(Widget, XEvent*, String*, Cardinal*);
static void NotifyThumb(Widget, XEvent*, String*, Cardinal*);
static void StartScroll(Widget, XEvent*, String*, Cardinal*);

/*
 * Initialization
 */
static char defaultTranslations[] =
"<Btn1Down>:"	"StartScroll(Forward)\n"
"<Btn2Down>:"	"StartScroll(Continuous) MoveThumb() NotifyThumb()\n"
"<Btn3Down>:"	"StartScroll(Backward)\n"
"<Btn4Down>:"	"StartScroll(Backward)\n"
"<Btn5Down>:"	"StartScroll(Forward)\n"
"<Btn2Motion>:"	"MoveThumb() NotifyThumb()\n"
"<BtnUp>:"	"NotifyScroll(Proportional) EndScroll()\n";

static float floatZero = 0.0;

#define Offset(field) XtOffsetOf(ScrollbarRec, field)

static XtResource resources[] = {
  {
    XtNlength,
    XtCLength,
    XtRDimension,
    sizeof(Dimension),
    Offset(scrollbar.length),
    XtRImmediate,
    (XtPointer)1
  },
  {
    XtNthickness,
    XtCThickness,
    XtRDimension,
    sizeof(Dimension),
    Offset(scrollbar.thickness),
    XtRImmediate,
    (XtPointer)14
  },
  {
    XtNorientation,
    XtCOrientation,
    XtROrientation,
    sizeof(XtOrientation),
    Offset(scrollbar.orientation),
    XtRImmediate,
    (XtPointer)XtorientVertical
  },
  {
    XtNscrollProc,
    XtCCallback,
    XtRCallback,
    sizeof(XtPointer),
    Offset(scrollbar.scrollProc),
    XtRCallback,
    NULL
  },
  {
    XtNthumbProc,
    XtCCallback,
    XtRCallback,
    sizeof(XtPointer),
    Offset(scrollbar.thumbProc),
    XtRCallback,
    NULL
  },
  {
    XtNjumpProc,
    XtCCallback,
    XtRCallback,
    sizeof(XtPointer),
    Offset(scrollbar.jumpProc),
    XtRCallback,
    NULL
  },
  {
    XtNthumb,
    XtCThumb,
    XtRBitmap,
    sizeof(Pixmap),
    Offset(scrollbar.thumb),
    XtRImmediate,
    (XtPointer)XtUnspecifiedPixmap
  },
  {
    XtNforeground,
    XtCForeground,
    XtRPixel,
    sizeof(Pixel),
    Offset(scrollbar.foreground),
    XtRString,
    (XtPointer)XtDefaultForeground
  },
  {
    XtNshown,
    XtCShown,
    XtRFloat,
    sizeof(float),
    Offset(scrollbar.shown),
    XtRFloat,
    (XtPointer)&floatZero
  },
  {
    XtNtopOfThumb,
    XtCTopOfThumb,
    XtRFloat,
    sizeof(float),
    Offset(scrollbar.top),
    XtRFloat,
    (XtPointer)&floatZero
  },
  {
    XtNscrollVCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    Offset(scrollbar.verCursor),
    XtRString,
    (XtPointer)"sb_v_double_arrow"
  },
  {
    XtNscrollHCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    Offset(scrollbar.horCursor),
    XtRString,
    (XtPointer)"sb_h_double_arrow"
  },
  {
    XtNscrollUCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    Offset(scrollbar.upCursor),
    XtRString,
    (XtPointer)"sb_up_arrow"
  },
  {
    XtNscrollDCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    Offset(scrollbar.downCursor),
    XtRString,
    (XtPointer)"sb_down_arrow"
  },
  {
    XtNscrollLCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    Offset(scrollbar.leftCursor),
    XtRString,
    (XtPointer)"sb_left_arrow"
  },
  {
    XtNscrollRCursor,
    XtCCursor,
    XtRCursor,
    sizeof(Cursor),
    Offset(scrollbar.rightCursor),
    XtRString,
    (XtPointer)"sb_right_arrow"
  },
  {
    XtNminimumThumb,
    XtCMinimumThumb,
    XtRDimension,
    sizeof(Dimension),
    Offset(scrollbar.min_thumb),
    XtRImmediate,
    (XtPointer)7
  },
};
#undef Offset

static XtActionsRec actions[] = {
	{"StartScroll",		StartScroll},
	{"MoveThumb",		MoveThumb},
	{"NotifyThumb",		NotifyThumb},
	{"NotifyScroll",	NotifyScroll},
	{"EndScroll",		EndScroll},
};

#define Superclass (&simpleClassRec)
ScrollbarClassRec scrollbarClassRec = {
  /* core */
  {
    (WidgetClass)&simpleClassRec,	/* superclass */
    "Scrollbar",			/* class_name */
    sizeof(ScrollbarRec),		/* widget_size */
    XawScrollbarClassInitialize,	/* class_initialize */
    NULL,				/* class_part_init */
    False,				/* class_inited */
    XawScrollbarInitialize,		/* initialize */
    NULL,				/* initialize_hook */
    XawScrollbarRealize,		/* realize */
    actions,				/* actions */
    XtNumber(actions),			/* num_actions */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    True,				/* compress_motion */
    True,				/* compress_exposure */
    True,				/* compress_enterleave */
    False,				/* visible_interest */
    XawScrollbarDestroy,		/* destroy */
    XawScrollbarResize,			/* resize */
    XawScrollbarRedisplay,		/* expose */
    XawScrollbarSetValues,		/* set_values */
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
  /* scrollbar */
  {
    NULL,				/* extension */
  },
};

WidgetClass scrollbarWidgetClass = (WidgetClass)&scrollbarClassRec;

/*
 * Implementation
 */
static void
XawScrollbarClassInitialize(void)
{
    XawInitializeWidgetSet();
    XtAddConverter(XtRString, XtROrientation, XmuCvtStringToOrientation,
		   NULL, 0);
    XtSetTypeConverter(XtROrientation, XtRString, XmuCvtOrientationToString,
		       NULL, 0, XtCacheNone, NULL);
}

/*
 * Make sure the first number is within the range specified by the other
 * two numbers.
 */
static int
InRange(int num, int small, int big)
{
    return ((num < small) ? small : ((num > big) ? big : num));
}

/*
 * Same as above, but for floating numbers
 */
static float
FloatInRange(float num, float small, float big)
{
    return ((num < small) ? small : ((num > big) ? big : num));
}

/* Fill the area specified by top and bottom with the given pattern */
static float
FractionLoc(ScrollbarWidget w, int x, int y)
{
    float   result;

    result = PICKLENGTH(w, (float)x / (float)XtWidth(w), (float)y / (float)XtHeight(w));

    return (FloatInRange(result, 0.0, 1.0));
}

static void
FillArea(ScrollbarWidget w, int top, int bottom, int thumb)
{
    Dimension length;

    top = XawMax(1, top);
    if (w->scrollbar.orientation == XtorientHorizontal)
    bottom = XawMin(bottom, XtWidth(w) - 1);
    else
    bottom = XawMin(bottom, XtHeight(w) - 1);

    if (bottom <= top)
	return;

    length = (Dimension)(bottom - top);

    switch(thumb) {
	/* Fill the new Thumb location */
	case 1:
	    if (w->scrollbar.orientation == XtorientHorizontal)
		XFillRectangle(XtDisplay(w), XtWindow(w), w->scrollbar.gc,
			       top, 1, length, (unsigned)(XtHeight(w) - 2));
	    else
		XFillRectangle(XtDisplay(w), XtWindow(w), w->scrollbar.gc,
			       1, top, (unsigned)(XtWidth(w) - 2), length);
	    break;
	/* Clear the old Thumb location */
	case 0:
	    if (w->scrollbar.orientation == XtorientHorizontal)
		XClearArea(XtDisplay(w), XtWindow(w),
			   top, 1, length, (unsigned)(XtHeight(w) - 2), False);
	    else
		XClearArea(XtDisplay(w), XtWindow(w),
			   1, top, (unsigned)(XtWidth(w) - 2), length, False);
	    break;
    }
}


/* Paint the thumb in the area specified by w->top and
   w->shown.  The old area is erased.  The painting and
   erasing is done cleverly so that no flickering will occur. */
static void
PaintThumb(ScrollbarWidget w)
{
    Position oldtop, oldbot, newtop, newbot;

    oldtop = w->scrollbar.topLoc;
    oldbot = (Position)(oldtop + w->scrollbar.shownLength);
    newtop = (Position)(w->scrollbar.length * w->scrollbar.top);
    newbot = (Position)(newtop + (int)(w->scrollbar.length * w->scrollbar.shown));
    if (newbot < newtop + (int)w->scrollbar.min_thumb)
	newbot = (Position)(newtop + w->scrollbar.min_thumb);
    w->scrollbar.topLoc = newtop;
    w->scrollbar.shownLength = (Dimension)(newbot - newtop);

    if (XtIsRealized((Widget)w)) {
	if (newtop < oldtop)
	    FillArea(w, newtop, XawMin(newbot, oldtop), 1);
	if (newtop > oldtop)
	    FillArea(w, oldtop, XawMin(newtop, oldbot), 0);
	if (newbot < oldbot)
	    FillArea(w, XawMax(newbot, oldtop), oldbot, 0);
	if (newbot > oldbot)
	    FillArea(w, XawMax(newtop, oldbot), newbot, 1);
    }
}

static void
SetDimensions(ScrollbarWidget w)
{
    if (w->scrollbar.orientation == XtorientVertical) {
	w->scrollbar.length = XtHeight(w);
	w->scrollbar.thickness = XtWidth(w);
    }
    else {
	w->scrollbar.length = XtWidth(w);
	w->scrollbar.thickness = XtHeight(w);
    }
}

static void
XawScrollbarDestroy(Widget w)
{
    ScrollbarWidget sbw = (ScrollbarWidget)w;

    XtReleaseGC(w, sbw->scrollbar.gc);
}

static void
CreateGC(Widget w)
{
    ScrollbarWidget sbw = (ScrollbarWidget)w;
    XGCValues gcValues;
    XtGCMask mask;
    unsigned int depth = 1;

    if (sbw->scrollbar.thumb == XtUnspecifiedPixmap)
	sbw->scrollbar.thumb = XmuCreateStippledPixmap(XtScreen(w),
						       (Pixel)1, (Pixel)0,
						       depth);
    else if (sbw->scrollbar.thumb != None) {
	Window root;
	int x, y;
	unsigned int width, height, bw;

	XGetGeometry(XtDisplay(w), sbw->scrollbar.thumb, &root, &x, &y,
		     &width, &height, &bw, &depth);
    }

    gcValues.foreground = sbw->scrollbar.foreground;
    gcValues.background = sbw->core.background_pixel;
    mask = GCForeground | GCBackground;

    if (sbw->scrollbar.thumb != None) {
	if (depth == 1) {
	    gcValues.fill_style = FillOpaqueStippled;
	    gcValues.stipple = sbw->scrollbar.thumb;
	    mask |= GCFillStyle | GCStipple;
	}
	else {
	    gcValues.fill_style = FillTiled;
	    gcValues.tile = sbw->scrollbar.thumb;
	    mask |= GCFillStyle | GCTile;
	}
    }
    sbw->scrollbar.gc = XtGetGC(w, mask, &gcValues);
}

/* ARGSUSED */
static void
XawScrollbarInitialize(Widget request _X_UNUSED, Widget cnew,
		       ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    ScrollbarWidget w = (ScrollbarWidget)cnew;

    CreateGC(cnew);

    if (XtWidth(w) == 0)
	XtWidth(w) = w->scrollbar.orientation == XtorientVertical ?
			w->scrollbar.thickness : w->scrollbar.length;

    if (XtHeight(w) == 0)
	XtHeight(w) = w->scrollbar.orientation == XtorientHorizontal ?
			w->scrollbar.thickness : w->scrollbar.length;

    SetDimensions(w);
    w->scrollbar.direction = 0;
    w->scrollbar.topLoc = 0;
    w->scrollbar.shownLength = w->scrollbar.min_thumb;
}

static void
XawScrollbarRealize(Widget gw, Mask *valueMask,
		    XSetWindowAttributes *attributes)
{
    ScrollbarWidget w = (ScrollbarWidget)gw;

    w->scrollbar.inactiveCursor = w->scrollbar.orientation == XtorientVertical ?
		w->scrollbar.verCursor : w->scrollbar.horCursor;

    XtVaSetValues(gw, XtNcursor, w->scrollbar.inactiveCursor, NULL);

    /*
     * The Simple widget actually stuffs the value in the valuemask
     */
    (*scrollbarWidgetClass->core_class.superclass->core_class.realize)
	(gw, valueMask, attributes);
}

/*ARGSUSED*/
static Boolean
XawScrollbarSetValues(Widget current, Widget request _X_UNUSED, Widget desired,
		      ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    ScrollbarWidget w = (ScrollbarWidget)current;
    ScrollbarWidget dw = (ScrollbarWidget)desired;
    Boolean redraw = False;

    /*
     * If these values are outside the acceptable range ignore them...
     */
    if (dw->scrollbar.top < 0.0 || dw->scrollbar.top > 1.0)
	dw->scrollbar.top = w->scrollbar.top;

    if (dw->scrollbar.shown < 0.0 || dw->scrollbar.shown > 1.0)
	dw->scrollbar.shown = w->scrollbar.shown;

    if (XtIsRealized (desired)) {
	if (w->scrollbar.foreground != dw->scrollbar.foreground ||
	    w->core.background_pixel != dw->core.background_pixel ||
	    w->scrollbar.thumb != dw->scrollbar.thumb) {
	    XtReleaseGC((Widget)dw, w->scrollbar.gc);
	    CreateGC((Widget)dw);
	    redraw = True;
	}
	if (w->scrollbar.top != dw->scrollbar.top ||
	    w->scrollbar.shown != dw->scrollbar.shown)
	    redraw = True;
    }

    return (redraw);
}

static void
XawScrollbarResize(Widget gw)
{
    /* ForgetGravity has taken care of background, but thumb may
     * have to move as a result of the new size. */
    SetDimensions((ScrollbarWidget)gw);
    XawScrollbarRedisplay(gw, NULL, NULL);
}

/*ARGSUSED*/
static void
XawScrollbarRedisplay(Widget gw, XEvent *event, Region region)
{
    ScrollbarWidget w = (ScrollbarWidget)gw;
    int x, y;
    unsigned int width, height;

    if (Superclass->core_class.expose)
	(*Superclass->core_class.expose)(gw, event, region);

    if (w->scrollbar.orientation == XtorientHorizontal) {
	x = w->scrollbar.topLoc;
	y = 1;
	width = (unsigned)(w->scrollbar.shownLength);
	height = (unsigned)(XtHeight(w) - 2);
    }
    else {
	x = 1;
	y = w->scrollbar.topLoc;
	width = (unsigned)(XtWidth(w) - 2);
	height = (unsigned)(w->scrollbar.shownLength);
    }

    if (region == NULL ||
	XRectInRegion(region, x, y, width, height) != RectangleOut) {
	/* Forces entire thumb to be painted */
	w->scrollbar.topLoc = (Position)(-(w->scrollbar.length + 1));
	PaintThumb(w);
    }
}

/*ARGSUSED*/
static void
StartScroll(Widget gw, XEvent *event _X_UNUSED, String *params, Cardinal *num_params)
{
    ScrollbarWidget w = (ScrollbarWidget)gw;
    Cursor cursor;
    char direction;

    if (w->scrollbar.direction != 0)	/* if we're already scrolling */
	return;
    if (*num_params > 0)
	direction = *params[0];
    else
	direction = 'C';

    w->scrollbar.direction = direction;

    switch(direction) {
	case 'B':
	case 'b':
	    cursor = w->scrollbar.orientation == XtorientVertical ?
		w->scrollbar.downCursor : w->scrollbar.rightCursor;
	    break;
	case 'F':
	case 'f':
	    cursor = w->scrollbar.orientation == XtorientVertical ?
		w->scrollbar.upCursor : w->scrollbar.leftCursor;
	    break;
	case 'C':
	case 'c':
	     cursor = w->scrollbar.orientation == XtorientVertical ?
		w->scrollbar.rightCursor : w->scrollbar.upCursor;
	     break;
	default:
	     return;	/* invalid invocation */
    }

    XtVaSetValues(gw, XtNcursor, cursor, NULL);

    XFlush(XtDisplay(w));
}

static Boolean
CompareEvents(XEvent *oldEvent, XEvent *newEvent)
{
#define Check(field) if (newEvent->field != oldEvent->field) return (False)

    Check(xany.display);
    Check(xany.type);
    Check(xany.window);

    switch(newEvent->type) {
	case MotionNotify:
	    Check(xmotion.state);
	    break;
	case ButtonPress:
	case ButtonRelease:
	    Check(xbutton.state);
	    Check(xbutton.button);
	    break;
	case KeyPress:
	case KeyRelease:
	    Check(xkey.state);
	    Check(xkey.keycode);
	    break;
	case EnterNotify:
	case LeaveNotify:
	    Check(xcrossing.mode);
	    Check(xcrossing.detail);
	    Check(xcrossing.state);
	    break;
    }
#undef Check

    return (True);
}

struct EventData {
    XEvent *oldEvent;
    int count;
};

static Bool
PeekNotifyEvent(Display *dpy, XEvent *event, char *args)
{
    struct EventData *eventData = (struct EventData*)args;

    return (++eventData->count == QLength(dpy)	/* since PeekIf blocks */
	    || CompareEvents(event, eventData->oldEvent));
}

static Bool
LookAhead(Widget w, XEvent *event)
{
    XEvent newEvent;
    struct EventData eventData;

    if (QLength(XtDisplay(w)) == 0)
	return (False);

    eventData.count = 0;
    eventData.oldEvent = event;

    XPeekIfEvent(XtDisplay(w), &newEvent, PeekNotifyEvent, (char*)&eventData);

    if (CompareEvents(event, &newEvent))
	return (True);

    return (False);
}

static void
ExtractPosition(XEvent *event, Position *x, Position *y)
{
    switch(event->type) {
	case MotionNotify:
	    *x = (Position)event->xmotion.x;
	    *y = (Position)event->xmotion.y;
	    break;
	case ButtonPress:
	case ButtonRelease:
	    *x = (Position)event->xbutton.x;
	    *y = (Position)event->xbutton.y;
	    break;
	case KeyPress:
	case KeyRelease:
	    *x = (Position)event->xkey.x;
	    *y = (Position)event->xkey.y;
	    break;
	case EnterNotify:
	case LeaveNotify:
	    *x = (Position)event->xcrossing.x;
	    *y = (Position)event->xcrossing.y;
	    break;
	default:
	    *x = 0;
	    *y = 0;
	    break;
    }
}

static void
NotifyScroll(Widget gw, XEvent *event, String *params, Cardinal *num_params)
{
    ScrollbarWidget w = (ScrollbarWidget)gw;
    long call_data = 0;
    char style;
    Position x, y;

    if (w->scrollbar.direction == 0)	/* if no StartScroll */
	return;

    if (LookAhead(gw, event))
	return;

    if (*num_params > 0)
	style = *params[0];
    else
	style = 'P';

    switch(style) {
	case 'P':    /* Proportional */
	case 'p':
	    ExtractPosition(event, &x, &y);
	    call_data = InRange(PICKLENGTH(w, x, y), 0, (int)w->scrollbar.length);
	    break;
	case 'F':    /* FullLength */
	case 'f':
	    call_data = w->scrollbar.length;
	    break;
    }

    switch(w->scrollbar.direction) {
	case 'B':
	case 'b':
	    call_data = -call_data;
	    /*FALLTHROUGH*/
	case 'F':
	case 'f':
	    XtCallCallbacks(gw, XtNscrollProc, (XtPointer)call_data);
	    break;
	case 'C':
	case 'c':    /* NotifyThumb has already called the thumbProc(s) */
	    break;
    }
}

/*ARGSUSED*/
static void
EndScroll(Widget gw, XEvent *event _X_UNUSED, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    ScrollbarWidget w = (ScrollbarWidget)gw;

    XtVaSetValues(gw, XtNcursor, w->scrollbar.inactiveCursor, NULL);
    XFlush(XtDisplay(w));		/* make sure it get propagated */

    w->scrollbar.direction = 0;
}

/*ARGSUSED*/
static void
MoveThumb(Widget gw, XEvent *event, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    ScrollbarWidget w = (ScrollbarWidget)gw;
    Position x, y;

    if (w->scrollbar.direction == 0)	/* if no StartScroll */
	return;

    if (LookAhead(gw, event))
	return;

    if (!event->xmotion.same_screen)
	return;

    ExtractPosition(event, &x, &y);
    w->scrollbar.top = FractionLoc(w, x, y);
}

/*ARGSUSED*/
static void
NotifyThumb(Widget gw, XEvent *event, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    ScrollbarWidget w = (ScrollbarWidget)gw;
    union {
	XtPointer xtp;
	float xtf;
    } xtpf;

    if (w->scrollbar.direction == 0)	/* if no StartScroll */
	return;

    if (LookAhead(gw, event))
	return;

    /* thumbProc is not pretty, but is necessary for backwards
       compatibility on those architectures for which it work{s,ed};
       the intent is to pass a (truncated) float by value. */
    xtpf.xtf = w->scrollbar.top;
    XtCallCallbacks(gw, XtNthumbProc, xtpf.xtp);
    XtCallCallbacks(gw, XtNjumpProc, (XtPointer)&w->scrollbar.top);

    PaintThumb(w);
}

/*
 *  Public routines
 */
/* Set the scroll bar to the given location. */
void
XawScrollbarSetThumb(Widget gw,
#if NeedWidePrototypes
		     double top, double shown
#else
		     float top, float shown
#endif
		     )
{
    ScrollbarWidget w = (ScrollbarWidget)gw;

    if (w->scrollbar.direction == 'c')	/* if still thumbing */
	return;

    w->scrollbar.top = (float)((top > 1.0)
			       ? 1.0
			       : ((top >= 0.0)
			          ? top
			          : w->scrollbar.top));

    w->scrollbar.shown = (float)((shown > 1.0)
				  ? 1.0
				  : (shown >= 0.0
				     ? shown
				     : w->scrollbar.shown));
    PaintThumb(w);
}
