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

/*
 * Command.c - Command button widget
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/extensions/shape.h>
#include <X11/Xmu/Converters.h>
#include <X11/Xmu/Drawing.h>
#include <X11/Xmu/Misc.h>
#include <X11/Xaw/CommandP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

#define DEFAULT_HIGHLIGHT_THICKNESS 2
#define DEFAULT_SHAPE_HIGHLIGHT 32767
#define STR_EQUAL(str1, str2)	(str1 == str2 || strcmp(str1, str2) == 0)

/*
 * Class Methods
 */
static void XawCommandClassInitialize(void);
static void XawCommandDestroy(Widget);
static void XawCommandInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawCommandRealize(Widget, Mask*, XSetWindowAttributes*);
static void XawCommandResize(Widget);
static void XawCommandRedisplay(Widget, XEvent*, Region);
static Boolean XawCommandSetValues(Widget, Widget, Widget, ArgList, Cardinal*);
static void XawCommandGetValuesHook(Widget, ArgList, Cardinal*);
static Bool ChangeSensitive(Widget);

/*
 * Prototypes
 */
static GC Get_GC(CommandWidget, Pixel, Pixel);
static void PaintCommandWidget(Widget, XEvent*, Region, Bool);
static Region HighlightRegion(CommandWidget);
static Bool ShapeButton(CommandWidget, Bool);
static void XawCommandToggle(Widget);

/*
 * Actions
 */
static void Highlight(Widget, XEvent*, String*, Cardinal*);
static void Notify(Widget, XEvent*, String*, Cardinal*);
static void Reset(Widget, XEvent*, String*, Cardinal*);
static void Set(Widget, XEvent*, String*, Cardinal*);
static void Unhighlight(Widget, XEvent*, String*, Cardinal*);
static void Unset(Widget, XEvent*, String*, Cardinal*);

/*
 * Initialization
 */
static char defaultTranslations[] =
"<Enter>:"	"highlight()\n"
"<Leave>:"	"reset()\n"
"<Btn1Down>:"	"set()\n"
"<Btn1Up>:"	"notify() unset()\n"
;

#define offset(field) XtOffsetOf(CommandRec, field)
static XtResource resources[] = {
  {
    XtNcallback,
    XtCCallback,
    XtRCallback,
    sizeof(XtPointer),
    offset(command.callbacks),
    XtRCallback,
    NULL
  },
  {
    XtNhighlightThickness,
    XtCThickness,
    XtRDimension,
    sizeof(Dimension),
    offset(command.highlight_thickness),
    XtRImmediate,
    (XtPointer)DEFAULT_SHAPE_HIGHLIGHT
  },
  {
    XtNshapeStyle,
    XtCShapeStyle,
    XtRShapeStyle,
    sizeof(int),
    offset(command.shape_style),
    XtRImmediate,
    (XtPointer)XawShapeRectangle
  },
  {
    XtNcornerRoundPercent,
    XtCCornerRoundPercent,
    XtRDimension,
    sizeof(Dimension),
    offset(command.corner_round),
    XtRImmediate,
    (XtPointer)25
  },
};
#undef offset

static XtActionsRec actionsList[] = {
  {"set",		Set},
  {"notify",		Notify},
  {"highlight",		Highlight},
  {"reset",		Reset},
  {"unset",		Unset},
  {"unhighlight",	Unhighlight}
};

#define SuperClass ((LabelWidgetClass)&labelClassRec)

CommandClassRec commandClassRec = {
  /* core */
  {
    (WidgetClass)SuperClass,		/* superclass		  */
    "Command",				/* class_name		  */
    sizeof(CommandRec),			/* size			  */
    XawCommandClassInitialize,		/* class_initialize	  */
    NULL,				/* class_part_initialize  */
    False,				/* class_inited		  */
    XawCommandInitialize,		/* initialize		  */
    NULL,				/* initialize_hook	  */
    XawCommandRealize,			/* realize		  */
    actionsList,			/* actions		  */
    XtNumber(actionsList),		/* num_actions		  */
    resources,				/* resources		  */
    XtNumber(resources),		/* num_resources	  */
    NULLQUARK,				/* xrm_class		  */
    False,				/* compress_motion	  */
    True,				/* compress_exposure	  */
    True,				/* compress_enterleave	  */
    False,				/* visible_interest	  */
    XawCommandDestroy,			/* destroy		  */
    XawCommandResize,			/* resize		  */
    XawCommandRedisplay,		/* expose		  */
    XawCommandSetValues,		/* set_values		  */
    NULL,				/* set_values_hook	  */
    XtInheritSetValuesAlmost,		/* set_values_almost	  */
    XawCommandGetValuesHook,		/* get_values_hook	  */
    NULL,				/* accept_focus		  */
    XtVersion,				/* version		  */
    NULL,				/* callback_private	  */
    defaultTranslations,		/* tm_table		  */
    XtInheritQueryGeometry,		/* query_geometry	  */
    XtInheritDisplayAccelerator,	/* display_accelerator	  */
    NULL,				/* extension */
  },
  /* simple */
  {
    ChangeSensitive,			/* change_sensitive */
  },
  /* label */
  {
    NULL,				/* not used */
  },
  /* command */
  {
    NULL,				/* not used */
  },
};

WidgetClass commandWidgetClass = (WidgetClass)&commandClassRec;

/*
 * Implementation
 */
static GC
Get_GC(CommandWidget cbw, Pixel fg, Pixel bg)
{
    XGCValues	values;

    values.foreground	= fg;
    values.background	= bg;
    values.font		= cbw->label.font->fid;
    values.cap_style	= CapProjecting;

    if (cbw->command.highlight_thickness > 1)
	values.line_width = cbw->command.highlight_thickness;
    else
	values.line_width = 0;

    if (cbw->simple.international == True)
	return (XtAllocateGC((Widget)cbw, 0,
			     GCForeground | GCBackground | GCLineWidth |
			     GCCapStyle, &values, GCFont, 0));
    else
	return (XtGetGC((Widget)cbw,
			GCForeground | GCBackground | GCFont | GCLineWidth |
			GCCapStyle, &values));
}

/*ARGSUSED*/
static void
XawCommandInitialize(Widget request _X_UNUSED, Widget cnew,
		     ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    CommandWidget cbw = (CommandWidget)cnew;
    int shape_event_base, shape_error_base;

    if (!cbw->label.font) XtError("Aborting: no font found\n");

    if (cbw->command.shape_style != XawShapeRectangle &&
	!XShapeQueryExtension(XtDisplay(cnew), &shape_event_base,
			      &shape_error_base))
	cbw->command.shape_style = XawShapeRectangle;

    if (cbw->command.highlight_thickness == DEFAULT_SHAPE_HIGHLIGHT) {
	if (cbw->command.shape_style != XawShapeRectangle)
	    cbw->command.highlight_thickness = 0;
	else
	    cbw->command.highlight_thickness = DEFAULT_HIGHLIGHT_THICKNESS;
    }

    cbw->command.normal_GC = Get_GC(cbw, cbw->label.foreground,
				    cbw->core.background_pixel);
    cbw->command.inverse_GC = Get_GC(cbw, cbw->core.background_pixel,
				     cbw->label.foreground);
    XtReleaseGC(cnew, cbw->label.normal_GC);
    cbw->label.normal_GC = cbw->command.normal_GC;

    cbw->command.set = False;
    cbw->command.highlighted = HighlightNone;
}

static Region
HighlightRegion(CommandWidget cbw)
{
    static Region outerRegion = NULL, innerRegion, emptyRegion;
    XRectangle rect;

    if (cbw->command.highlight_thickness == 0 ||
        cbw->command.highlight_thickness > Min(XtWidth(cbw), XtHeight(cbw)) / 2)
	return (NULL);

    if (outerRegion == NULL) {
	/* save time by allocating scratch regions only once. */
	outerRegion = XCreateRegion();
	innerRegion = XCreateRegion();
	emptyRegion = XCreateRegion();
    }

    rect.x = rect.y = 0;
    rect.width = XtWidth(cbw);
    rect.height = XtHeight(cbw);
    XUnionRectWithRegion(&rect, emptyRegion, outerRegion);
    rect.x = rect.y = (short)cbw->command.highlight_thickness;
    rect.width = (rect.width - cbw->command.highlight_thickness * 2);
    rect.height = (rect.height - cbw->command.highlight_thickness * 2);
    XUnionRectWithRegion(&rect, emptyRegion, innerRegion);
    XSubtractRegion(outerRegion, innerRegion, outerRegion);

    return (outerRegion);
}

/***************************
*  Action Procedures
***************************/
static void
XawCommandToggle(Widget w)
{
    CommandWidget xaw = (CommandWidget)w;
    Arg args[2];
    Cardinal num_args;

    num_args = 0;
    XtSetArg(args[num_args], XtNbackground,
	     xaw->label.foreground);		++num_args;
    XtSetArg(args[num_args], XtNforeground,
	     xaw->core.background_pixel);	++num_args;
    XtSetValues(w, args, num_args);
}

/*ARGSUSED*/
static void
Set(Widget w, XEvent *event _X_UNUSED, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    CommandWidget cbw = (CommandWidget)w;

    if (cbw->command.set)
	return;

    XawCommandToggle(w);
    cbw->command.set= True;
}

/*ARGSUSED*/
static void
Unset(Widget w, XEvent *event _X_UNUSED, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    CommandWidget cbw = (CommandWidget)w;

    if (!cbw->command.set)
	return;

    cbw->command.set = False;
    XawCommandToggle(w);
}

/*ARGSUSED*/
static void
Reset(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    CommandWidget cbw = (CommandWidget)w;

    if (cbw->command.set) {
	cbw->command.highlighted = HighlightNone;
	Unset(w, event, params, num_params);
    }
    else
	Unhighlight(w, event, params, num_params);
}

/*ARGSUSED*/
static void
Highlight(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    CommandWidget cbw = (CommandWidget)w;

    if (*num_params == (Cardinal)0)
	cbw->command.highlighted = HighlightWhenUnset;
    else {
	if (*num_params != (Cardinal)1)
	    XtWarning("Too many parameters passed to highlight action table.");
	switch (params[0][0]) {
	    case 'A':
	    case 'a':
		cbw->command.highlighted = HighlightAlways;
		break;
	    default:
		cbw->command.highlighted = HighlightWhenUnset;
		break;
	}
    }

    if (XtIsRealized(w))
	PaintCommandWidget(w, event, HighlightRegion(cbw), True);
}

/*ARGSUSED*/
static void
Unhighlight(Widget w, XEvent *event, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    CommandWidget cbw = (CommandWidget)w;

    cbw->command.highlighted = HighlightNone;
    if (XtIsRealized(w))
	PaintCommandWidget(w, event, HighlightRegion(cbw), True);
}

/*ARGSUSED*/
static void
Notify(Widget w, XEvent *event _X_UNUSED, String *params _X_UNUSED, Cardinal *num_params _X_UNUSED)
{
    CommandWidget cbw = (CommandWidget)w;

    /* check to be sure state is still Set so that user can cancel
       the action (e.g. by moving outside the window, in the default
       bindings.
    */
    if (cbw->command.set)
	XtCallCallbackList(w, cbw->command.callbacks, (XtPointer) NULL);
}

static void
XawCommandRedisplay(Widget w, XEvent *event, Region region)
{
    PaintCommandWidget(w, event, region, False);
}

/*
 * Function:
 *	PaintCommandWidget
 * Parameters:
 *	w      - command widget
 *	region - region to paint (passed to the superclass)
 *                 change - did it change either set or highlight state?
 */
static void
PaintCommandWidget(Widget w, XEvent *event, Region region, Bool change)
{
    CommandWidget cbw = (CommandWidget)w;
    Bool very_thick;
    GC rev_gc;

    very_thick = cbw->command.highlight_thickness
		 > Min(XtWidth(cbw), XtHeight(cbw)) / 2;

    if (cbw->command.highlight_thickness == 0) {
	(*SuperClass->core_class.expose) (w, event, region);
	return;
    }

    /*
     * If we are set then use the same colors as if we are not highlighted
     */

    if (cbw->command.highlighted != HighlightNone) {
	rev_gc = cbw->command.normal_GC;
    }
    else {
	rev_gc = cbw->command.inverse_GC;
    }

    if (!((!change && cbw->command.highlighted == HighlightNone)
	|| (cbw->command.highlighted == HighlightWhenUnset
	    && cbw->command.set))) {
	if (very_thick)
	    XFillRectangle(XtDisplay(w),XtWindow(w), rev_gc,
			   0, 0, XtWidth(cbw), XtHeight(cbw));
	else {
	    /* wide lines are centered on the path, so indent it */
	    if (cbw->core.background_pixmap != XtUnspecifiedPixmap &&
		rev_gc == cbw->command.inverse_GC) {
		XClearArea(XtDisplay(w), XtWindow(w),
			   0, 0, XtWidth(cbw), cbw->command.highlight_thickness,
			   False);
		XClearArea(XtDisplay(w), XtWindow(w),
			   0, cbw->command.highlight_thickness,
			   cbw->command.highlight_thickness,
			   (unsigned)(XtHeight(cbw) - (cbw->command.highlight_thickness<<1)),
			   False);
		XClearArea(XtDisplay(w), XtWindow(w),
			   XtWidth(cbw) - cbw->command.highlight_thickness,
			   cbw->command.highlight_thickness,
			   cbw->command.highlight_thickness,
			   (unsigned)(XtHeight(cbw) - (cbw->command.highlight_thickness<<1)),
			   False);
		XClearArea(XtDisplay(w), XtWindow(w),
			   0, XtHeight(cbw) - cbw->command.highlight_thickness,
			   XtWidth(cbw), cbw->command.highlight_thickness,
			   False);
	    }
	    else {
		int offset = cbw->command.highlight_thickness / 2;

		XDrawRectangle(XtDisplay(w),XtWindow(w), rev_gc, offset, offset,
			       (unsigned)(XtWidth(cbw) - cbw->command.highlight_thickness),
			       (unsigned)(XtHeight(cbw) - cbw->command.highlight_thickness));
	   }
	}
    }

    (*SuperClass->core_class.expose)(w, event, region);
}

static void
XawCommandDestroy(Widget w)
{
    CommandWidget cbw = (CommandWidget)w;

    /* Label will release cbw->command.normal_GC */
    XtReleaseGC(w, cbw->command.inverse_GC);
}

/*ARGSUSED*/
static Boolean
XawCommandSetValues(Widget current, Widget request _X_UNUSED, Widget cnew,
		    ArgList args, Cardinal *num_args)
{
    CommandWidget oldcbw = (CommandWidget)current;
    CommandWidget cbw = (CommandWidget)cnew;
    Boolean redisplay = False;

    if (oldcbw->core.sensitive != cbw->core.sensitive && !cbw->core.sensitive) {
	cbw->command.highlighted = HighlightNone;
	redisplay = True;
    }

    if (cbw->command.set) {
	unsigned int i;
	Pixel foreground, background;

	foreground = oldcbw->label.foreground;
	background = oldcbw->core.background_pixel;
	for (i = 0; i < *num_args; i++) {
	    if (STR_EQUAL(args[i].name, XtNforeground))
		background = cbw->label.foreground;
	    else if (STR_EQUAL(args[i].name, XtNbackground))
		foreground = cbw->core.background_pixel;
	}
	cbw->label.foreground = foreground;
	cbw->core.background_pixel = background;
    }

    if (oldcbw->label.foreground != cbw->label.foreground
	|| oldcbw->core.background_pixel != cbw->core.background_pixel
	|| oldcbw->command.highlight_thickness
	!= cbw->command.highlight_thickness
	|| oldcbw->label.font != cbw->label.font) {
	XtReleaseGC(cnew, cbw->command.inverse_GC);

	cbw->command.normal_GC = Get_GC(cbw, cbw->label.foreground,
					cbw->core.background_pixel);
	cbw->command.inverse_GC = Get_GC(cbw, cbw->core.background_pixel,
					 cbw->label.foreground);
	XtReleaseGC(cnew, cbw->label.normal_GC);
	cbw->label.normal_GC = cbw->command.normal_GC;

	redisplay = True;
    }

    if (XtIsRealized(cnew)
	&& oldcbw->command.shape_style != cbw->command.shape_style
	&& !ShapeButton(cbw, True))
	cbw->command.shape_style = oldcbw->command.shape_style;

    return (redisplay);
}

static void
XawCommandGetValuesHook(Widget w, ArgList args, Cardinal *num_args)
{
    CommandWidget cbw = (CommandWidget)w;
    unsigned int i;

    for (i = 0; i < *num_args; i++) {
	if (STR_EQUAL(args[i].name, XtNforeground))
	    *((String*)args[i].value) = cbw->command.set ?
		(String)cbw->core.background_pixel : (String)cbw->label.foreground;
	else if (STR_EQUAL(args[i].name, XtNbackground))
	    *((String*)args[i].value) = cbw->command.set ?
		(String)cbw->label.foreground : (String)cbw->core.background_pixel;
    }
}

static void
XawCommandClassInitialize(void)
{
    XawInitializeWidgetSet();
    XtSetTypeConverter(XtRString, XtRShapeStyle, XmuCvtStringToShapeStyle,
		       NULL, 0, XtCacheNone, NULL);
    XtSetTypeConverter(XtRShapeStyle, XtRString, XmuCvtShapeStyleToString,
		       NULL, 0, XtCacheNone, NULL);
}

static Bool
ShapeButton(CommandWidget cbw, Bool checkRectangular)
{
    Dimension corner_size = 0;

    if (cbw->command.shape_style == XawShapeRoundedRectangle) {
	corner_size = XtWidth(cbw) < XtHeight(cbw) ?
			XtWidth(cbw) : XtHeight(cbw);
	corner_size = (Dimension)((corner_size * cbw->command.corner_round) / 100);
    }

    if (checkRectangular || cbw->command.shape_style != XawShapeRectangle) {
	if (!XmuReshapeWidget((Widget)cbw, cbw->command.shape_style,
			      corner_size, corner_size)) {
	    cbw->command.shape_style = XawShapeRectangle;
	    return (False);
	}
    }

    return (True);
}

static void
XawCommandRealize(Widget w, Mask *valueMask, XSetWindowAttributes *attributes)
{
    (*commandWidgetClass->core_class.superclass->core_class.realize)
	(w, valueMask, attributes);

    ShapeButton((CommandWidget)w, False);
}

static void
XawCommandResize(Widget w)
{
    if (XtIsRealized(w))
	ShapeButton((CommandWidget)w, False);

    (*commandWidgetClass->core_class.superclass->core_class.resize)(w);
}

static Bool
ChangeSensitive(Widget w)
{
    CommandWidget cbw = (CommandWidget)w;

    if (XtIsRealized(w)) {
	if (XtIsSensitive(w)) {
	    if (w->core.border_pixmap != XtUnspecifiedPixmap)
		XSetWindowBorderPixmap(XtDisplay(w), XtWindow(w),
				       w->core.border_pixmap);
	    else
		XSetWindowBorder(XtDisplay(w), XtWindow(w),
				 w->core.border_pixel);
	}
	else {
	    if (cbw->simple.insensitive_border == None)
		cbw->simple.insensitive_border =
		    XmuCreateStippledPixmap(XtScreen(w),
					    w->core.border_pixel,
					    cbw->command.set ?
						cbw->label.foreground :
						w->core.background_pixel,
					    w->core.depth);
	    XSetWindowBorderPixmap(XtDisplay(w), XtWindow(w),
				   cbw->simple.insensitive_border);
	}
    }

    return (False);
}
