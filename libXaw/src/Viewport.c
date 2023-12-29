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
#include <X11/Xmu/Misc.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/ViewportP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

/*
 * Class Methods
 */
static Boolean Layout(FormWidget, unsigned int, unsigned int, Bool);
static void XawViewportChangeManaged(Widget);
static void XawViewportInitialize(Widget, Widget, ArgList, Cardinal*);
static void
XawViewportConstraintInitialize(Widget, Widget, ArgList, Cardinal*);
static XtGeometryResult XawViewportGeometryManager(Widget, XtWidgetGeometry*,
						   XtWidgetGeometry*);
static XtGeometryResult XawViewportQueryGeometry(Widget,
						 XtWidgetGeometry*,
						 XtWidgetGeometry*);
static void XawViewportRealize(Widget, XtValueMask*, XSetWindowAttributes*);
static void XawViewportResize(Widget);
static Boolean XawViewportSetValues(Widget, Widget, Widget,
				    ArgList, Cardinal*);

/*
 * Prototypes
 */
static void ComputeLayout(Widget, Bool, Bool);
static void ComputeWithForceBars(Widget, Bool, XtWidgetGeometry*,
				 int*, int*);
static Widget CreateScrollbar(ViewportWidget, Bool);
static XtGeometryResult GeometryRequestPlusScrollbar(ViewportWidget, Bool,
						     XtWidgetGeometry*,
						     XtWidgetGeometry*);
static Bool GetGeometry(Widget, unsigned int, unsigned int);
static void MoveChild(ViewportWidget, int, int);
static XtGeometryResult QueryGeometry(ViewportWidget, XtWidgetGeometry*,
				      XtWidgetGeometry*);
static void RedrawThumbs(ViewportWidget);
static void ScrollUpDownProc(Widget, XtPointer, XtPointer);
static void SendReport(ViewportWidget, unsigned int);
static void SetBar(Widget, int, unsigned int, unsigned int);
static XtGeometryResult TestSmaller(ViewportWidget, XtWidgetGeometry*,
				    XtWidgetGeometry*);
static void ThumbProc(Widget, XtPointer, XtPointer);

/*
 * Initialization
 */
#define offset(field) XtOffsetOf(ViewportRec, viewport.field)
static XtResource resources[] = {
  {
    XtNforceBars,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    offset(forcebars),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNallowHoriz,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    offset(allowhoriz),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNallowVert,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    offset(allowvert),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNuseBottom,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    offset(usebottom),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNuseRight,
    XtCBoolean,
    XtRBoolean,
    sizeof(Boolean),
    offset(useright),
    XtRImmediate,
    (XtPointer)False
  },
  {
    XtNreportCallback,
    XtCReportCallback,
    XtRCallback,
    sizeof(XtPointer),
    offset(report_callbacks),
    XtRImmediate,
    NULL
  },
};
#undef offset

#define Superclass	(&formClassRec)
ViewportClassRec viewportClassRec = {
  /* core */
  {
    (WidgetClass)Superclass,		/* superclass */
    "Viewport",				/* class_name */
    sizeof(ViewportRec),		/* widget_size */
    XawInitializeWidgetSet,		/* class_initialize */
    NULL,				/* class_part_init */
    False,				/* class_inited */
    XawViewportInitialize,		/* initialize */
    NULL,				/* initialize_hook */
    XawViewportRealize,			/* realize */
    NULL,				/* actions */
    0,					/* num_actions */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    True,				/* compress_motion */
    True,				/* compress_exposure */
    True,				/* compress_enterleave */
    False,				/* visible_interest */
    NULL,				/* destroy */
    XawViewportResize,			/* resize */
    XtInheritExpose,			/* expose */
    XawViewportSetValues,		/* set_values */
    NULL,				/* set_values_hook */
    XtInheritSetValuesAlmost,		/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    NULL,				/* tm_table */
    XawViewportQueryGeometry,		/* query_geometry */
    XtInheritDisplayAccelerator,	/* display_accelerator */
    NULL,				/* extension */
  },
  /* composite */
  {
    XawViewportGeometryManager,		/* geometry_manager */
    XawViewportChangeManaged,		/* change_managed */
    XtInheritInsertChild,		/* insert_child */
    XtInheritDeleteChild,		/* delete_child */
    NULL,				/* extension */
  },
  /* constraint */
  {
    NULL,				/* subresourses */
    0,					/* subresource_count */
    sizeof(ViewportConstraintsRec),	/* constraint_size */
    XawViewportConstraintInitialize,	/* initialize */
    NULL,				/* destroy */
    NULL,				/* set_values */
    NULL,				/* extension */
  },
  /* form */
  {
    Layout,				/* layout */
  },
  /* viewport */
  {
    NULL,				/* extension */
  },
};

WidgetClass viewportWidgetClass = (WidgetClass)&viewportClassRec;

/*
 * Implementation
 */
static Widget
CreateScrollbar(ViewportWidget w, Bool horizontal)
{
    static Arg barArgs[] = {
	{XtNorientation,	    0},
	{XtNlength,		    0},
	{XtNleft,		    0},
	{XtNright,		    0},
	{XtNtop,		    0},
	{XtNbottom,		    0},
	{XtNmappedWhenManaged,	    False},
    };
    Widget clip = w->viewport.clip;
    ViewportConstraints constraints =
	(ViewportConstraints)clip->core.constraints;
    Widget bar;

    XtSetArg(barArgs[0], XtNorientation,
	   horizontal ? XtorientHorizontal : XtorientVertical);
    XtSetArg(barArgs[1], XtNlength,
	   horizontal ? XtWidth(clip) : XtHeight(clip));
    XtSetArg(barArgs[2], XtNleft,
	   !horizontal && w->viewport.useright ? XtChainRight : XtChainLeft);
    XtSetArg(barArgs[3], XtNright,
	   !horizontal && !w->viewport.useright ? XtChainLeft : XtChainRight);
    XtSetArg(barArgs[4], XtNtop,
	   horizontal && w->viewport.usebottom ? XtChainBottom: XtChainTop);
    XtSetArg(barArgs[5], XtNbottom,
	   horizontal && !w->viewport.usebottom ? XtChainTop: XtChainBottom);

    bar = XtCreateWidget(horizontal ? "horizontal" : "vertical",
			 scrollbarWidgetClass, (Widget)w,
			 barArgs, XtNumber(barArgs));
    XtAddCallback(bar, XtNscrollProc, ScrollUpDownProc, (XtPointer)w);
    XtAddCallback(bar, XtNjumpProc, ThumbProc, (XtPointer)w);

    if (horizontal) {
	w->viewport.horiz_bar = bar;
	constraints->form.vert_base = bar;
    }
    else {
	w->viewport.vert_bar = bar;
	constraints->form.horiz_base = bar;
    }

    XtManageChild(bar);

    return (bar);
}

/*ARGSUSED*/
static void
XawViewportInitialize(Widget request _X_UNUSED, Widget cnew,
		      ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    ViewportWidget w = (ViewportWidget)cnew;
    static Arg clip_args[8];
    Cardinal arg_cnt;
    Widget h_bar, v_bar;
    Dimension clip_height, clip_width;

    w->form.default_spacing = 0; /* Reset the default spacing to 0 pixels */

    /*
     * Initialize all widget pointers to NULL
     */
    w->viewport.child = NULL;
    w->viewport.horiz_bar = w->viewport.vert_bar = NULL;

    /*
     * Create Clip Widget
     */
    arg_cnt = 0;
    XtSetArg(clip_args[arg_cnt], XtNbackgroundPixmap, None);	arg_cnt++;
    XtSetArg(clip_args[arg_cnt], XtNborderWidth, 0);		arg_cnt++;
    XtSetArg(clip_args[arg_cnt], XtNleft, XtChainLeft);		arg_cnt++;
    XtSetArg(clip_args[arg_cnt], XtNright, XtChainRight);	arg_cnt++;
    XtSetArg(clip_args[arg_cnt], XtNtop, XtChainTop);		arg_cnt++;
    XtSetArg(clip_args[arg_cnt], XtNbottom, XtChainBottom);	arg_cnt++;
    XtSetArg(clip_args[arg_cnt], XtNwidth, XtWidth(w));		arg_cnt++;
    XtSetArg(clip_args[arg_cnt], XtNheight, XtHeight(w));	arg_cnt++;

    w->viewport.clip = XtCreateManagedWidget("clip", widgetClass, cnew,
					     clip_args, arg_cnt);

    if (!w->viewport.forcebars)
	return;		 /* If we are not forcing the bars then we are done */

    if (w->viewport.allowhoriz)
	(void)CreateScrollbar(w, True);
    if (w->viewport.allowvert)
	(void)CreateScrollbar(w, False);

    h_bar = w->viewport.horiz_bar;
    v_bar = w->viewport.vert_bar;

    /*
     * Set the clip widget to the correct height
     */
    clip_width = XtWidth(w);
    clip_height = XtHeight(w);

    if (h_bar != NULL &&  XtWidth(w) > XtWidth(h_bar) + XtBorderWidth(h_bar))
	clip_width = (Dimension)(clip_width - (XtWidth(h_bar) + XtBorderWidth(h_bar)));

    if (v_bar != NULL && XtHeight(w) > XtHeight(v_bar) + XtBorderWidth(v_bar))
	clip_height = (Dimension)(clip_height - (XtHeight(v_bar) + XtBorderWidth(v_bar)));

    arg_cnt = 0;
    XtSetArg(clip_args[arg_cnt], XtNwidth, clip_width); arg_cnt++;
    XtSetArg(clip_args[arg_cnt], XtNheight, clip_height); arg_cnt++;
    XtSetValues(w->viewport.clip, clip_args, arg_cnt);
}

/*ARGSUSED*/
static void
XawViewportConstraintInitialize(Widget request _X_UNUSED, Widget cnew,
				ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    ((ViewportConstraints)cnew->core.constraints)->viewport.reparented = False;
}

static void
XawViewportRealize(Widget widget, XtValueMask *value_mask,
		   XSetWindowAttributes *attributes)
{
    ViewportWidget w = (ViewportWidget)widget;
    Widget child = w->viewport.child;
    Widget clip = w->viewport.clip;

    *value_mask |= CWBitGravity;
    attributes->bit_gravity = NorthWestGravity;
    (*Superclass->core_class.realize)(widget, value_mask, attributes);

    (*w->core.widget_class->core_class.resize)(widget);	/* turn on bars */

    if (child != NULL) {
	XtMoveWidget(child, 0, 0);
	XtRealizeWidget(clip);
	XtRealizeWidget(child);
	XReparentWindow(XtDisplay(w), XtWindow(child), XtWindow(clip), 0, 0);
	XtMapWidget(child);
    }
}

/*ARGSUSED*/
static Boolean
XawViewportSetValues(Widget current, Widget request _X_UNUSED, Widget cnew,
		     ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    ViewportWidget w = (ViewportWidget)cnew;
    ViewportWidget cw = (ViewportWidget)current;

    if (w->viewport.forcebars != cw->viewport.forcebars
	|| w->viewport.allowvert != cw->viewport.allowvert
	|| w->viewport.allowhoriz != cw->viewport.allowhoriz
	|| w->viewport.useright != cw->viewport.useright
	|| w->viewport.usebottom != cw->viewport.usebottom)
	(*w->core.widget_class->core_class.resize)(cnew); /* Recompute layout */

    return (False);
}

static void
XawViewportChangeManaged(Widget widget)
{
    ViewportWidget w = (ViewportWidget)widget;
    int num_children = (int)w->composite.num_children;
    Widget child, *childP;
    int i;

    child = NULL;
    for (childP = w->composite.children,
	 i = 0; i < num_children;
	 childP++, i++) {
	if (XtIsManaged(*childP)
	    && *childP != w->viewport.clip
	    && *childP != w->viewport.horiz_bar
	    && *childP != w->viewport.vert_bar)	{
	    child = *childP;
	    break;
	}
    }

    if (child != w->viewport.child) {
	w->viewport.child = child;
	if (child != NULL) {
	    XtResizeWidget(child, XtWidth(child), XtHeight(child), 0);
	    if (XtIsRealized(widget)) {
		ViewportConstraints constraints =
		    (ViewportConstraints)child->core.constraints;
		if (!XtIsRealized(child)) {
		    Window window = XtWindow(w);

		    XtMoveWidget(child, 0, 0);
		    w->core.window = XtWindow(w->viewport.clip);
		    XtRealizeWidget(child);
		    w->core.window = window;
		    constraints->viewport.reparented = True;
		}
		else if (!constraints->viewport.reparented) {
		    XReparentWindow(XtDisplay(w), XtWindow(child),
				    XtWindow(w->viewport.clip), 0, 0);
		    constraints->viewport.reparented = True;
		    if (child->core.mapped_when_managed)
		    XtMapWidget(child);
		}
	    }
	    GetGeometry(widget, XtWidth(child), XtHeight(child));
	    (*((ViewportWidgetClass)w->core.widget_class)->form_class.layout)
	    ((FormWidget)w, XtWidth(w), XtHeight(w), True /* True? */);
	}
    }

#ifdef notdef
    (*Superclass->composite_class.change_managed)(widget);
#endif
}

static void
SetBar(Widget w, int top, unsigned int length, unsigned int total)
{
    XawScrollbarSetThumb(w, (float)top / (float)total,
			 (float)length / (float)total);
}

static void
RedrawThumbs(ViewportWidget w)
{
    Widget child = w->viewport.child;
    Widget clip = w->viewport.clip;

    if (w->viewport.horiz_bar != NULL)
	SetBar(w->viewport.horiz_bar, -(int)XtX(child),
	       XtWidth(clip), XtWidth(child));

    if (w->viewport.vert_bar != NULL)
	SetBar(w->viewport.vert_bar, -(int)XtY(child),
	       XtHeight(clip), XtHeight(child));
}

static void
SendReport(ViewportWidget w, unsigned int changed)
{
    XawPannerReport rep;

    if (w->viewport.report_callbacks) {
	Widget child = w->viewport.child;
	Widget clip = w->viewport.clip;

	rep.changed = changed;
	rep.slider_x = (Position) -XtX(child);	/* child is canvas */
	rep.slider_y = (Position) -XtY(child);	/* clip is slider */
	rep.slider_width = XtWidth(clip);
	rep.slider_height = XtHeight(clip);
	rep.canvas_width = XtWidth(child);
	rep.canvas_height = XtHeight(child);
	XtCallCallbackList((Widget)w, w->viewport.report_callbacks,
			   (XtPointer)&rep);
    }
}

static void
MoveChild(ViewportWidget w, int x, int y)
{
    Widget child = w->viewport.child;
    Widget clip = w->viewport.clip;

    /* make sure we never move past right/bottom borders */
    if (-x + (int)XtWidth(clip) > XtWidth(child))
	x = -(int)(XtWidth(child) - XtWidth(clip));

    if (-y + (int)XtHeight(clip) > XtHeight(child))
	y = -(int)(XtHeight(child) - XtHeight(clip));

    /* make sure we never move past left/top borders */
    if (x >= 0)
	x = 0;
    if (y >= 0)
	y = 0;

    XtMoveWidget(child, (Position) x, (Position) y);
    SendReport(w, (XawPRSliderX | XawPRSliderY));

    RedrawThumbs(w);
}

static void
ComputeLayout(Widget widget, Bool query, Bool destroy_scrollbars)
{
    ViewportWidget w = (ViewportWidget)widget;
    Widget child = w->viewport.child;
    Widget clip = w->viewport.clip;
    ViewportConstraints constraints =
	(ViewportConstraints)clip->core.constraints;
    Bool needshoriz, needsvert;
    int clip_width, clip_height;
    XtWidgetGeometry intended;

    if (child == NULL)
	return;

    clip_width = XtWidth(w);
    clip_height = XtHeight(w);
    intended.request_mode = CWBorderWidth;
    intended.border_width = 0;

    if (w->viewport.forcebars) {
	needsvert = w->viewport.allowvert;
	needshoriz = w->viewport.allowhoriz;
	ComputeWithForceBars(widget, query, &intended,
			     &clip_width, &clip_height);
    }
    else {
	Dimension prev_width, prev_height;
	XtGeometryMask prev_mode;
	XtWidgetGeometry preferred;

	needshoriz = needsvert = False;

	/*
	 * intended.{width,height} caches the eventual child dimensions,
	 * but we don't set the mode bits until after we decide that the
	 * child's preferences are not acceptable
	 */
	if (!w->viewport.allowhoriz)
	    intended.request_mode |= CWWidth;

	if (XtWidth(child) < clip_width)
	    intended.width = (Dimension)clip_width;
	else
	    intended.width = XtWidth(child);

	if (XtHeight(child) < clip_height)
	    intended.height = (Dimension)clip_height;
	else
	    intended.height = XtHeight(child);

	if (!w->viewport.allowvert)
	    intended.request_mode |= CWHeight;

	if (!query) {
	    preferred.width = XtWidth(child);
	    preferred.height = XtHeight(child);
	}
	do { /* while intended != prev  */
	    if (query) {
		(void)XtQueryGeometry(child, &intended, &preferred);
		if (!(preferred.request_mode & CWWidth))
		    preferred.width = intended.width;
		if (!(preferred.request_mode & CWHeight))
		    preferred.height = intended.height;
	    }
	    prev_width = intended.width;
	    prev_height = intended.height;
	    prev_mode = intended.request_mode;
	    /*
	     * note that having once decided to turn on either bar
	     * we'll not change our mind until we're next resized,
	     * thus avoiding potential oscillations
	     */
#define CheckHoriz() \
	    if (w->viewport.allowhoriz &&				\
		preferred.width > clip_width) {				\
		if (!needshoriz) {					\
		    Widget bar2;					\
									\
		    needshoriz = True;					\
		    if ((bar2 = w->viewport.horiz_bar) == NULL)		\
			bar2 = CreateScrollbar(w, True);		\
		    clip_height -= XtHeight(bar2) + XtBorderWidth(bar2);\
		    if (clip_height < 1)				\
			clip_height = 1;				\
		}							\
		intended.width = preferred.width;			\
	    }

	    CheckHoriz();
	    if (w->viewport.allowvert && preferred.height > clip_height) {
		if (!needsvert) {
		    Widget bar;
		    needsvert = True;
		    if ((bar = w->viewport.vert_bar) == NULL)
			bar = CreateScrollbar(w, False);
		    clip_width -= XtWidth(bar) + XtBorderWidth(bar);
		    if (clip_width < 1)
			clip_width = 1;
		    CheckHoriz();
		}
		intended.height = preferred.height;
	    }
	    if (!w->viewport.allowhoriz || preferred.width < clip_width) {
		intended.width = (Dimension)clip_width;
		intended.request_mode |= CWWidth;
	    }
	    if (!w->viewport.allowvert || preferred.height < clip_height) {
		intended.height = (Dimension)clip_height;
		intended.request_mode |= CWHeight;
	    }
	} while (intended.request_mode != prev_mode
		 || (intended.request_mode & CWWidth
		     && intended.width != prev_width)
		 || (intended.request_mode & CWHeight
		     && intended.height != prev_height));
    }

    if (XtIsRealized(clip))
	XRaiseWindow(XtDisplay(clip), XtWindow(clip));

    XtMoveWidget(clip,
		 (Position)(needsvert ? w->viewport.useright ? 0 :
					XtWidth(w->viewport.vert_bar)
			    + XtBorderWidth(w->viewport.vert_bar) : 0),
		 (Position)(needshoriz ? w->viewport.usebottom ? 0 :
					 XtHeight(w->viewport.horiz_bar)
			    + XtBorderWidth(w->viewport.horiz_bar) : 0));
    XtResizeWidget(clip, (Dimension)clip_width, (Dimension)clip_height, 0);

    if (w->viewport.horiz_bar != NULL) {
	Widget bar = w->viewport.horiz_bar;

	if (!needshoriz) {
	    constraints->form.vert_base = NULL;
	    if (destroy_scrollbars) {
		XtDestroyWidget(bar);
		w->viewport.horiz_bar = NULL;
	    }
	}
	else {
	    int bw = XtBorderWidth(bar);

	    XtResizeWidget(bar, (Dimension)clip_width, (Dimension)XtHeight(bar), (Dimension)bw);
	    XtMoveWidget(bar,
			 (Position)(needsvert && !w->viewport.useright
				    ? XtWidth(w->viewport.vert_bar) : -bw),
			 (Position)(w->viewport.usebottom
				    ? XtHeight(w) - XtHeight(bar) - bw : -bw));
	    XtSetMappedWhenManaged(bar, True);
	}
    }

    if (w->viewport.vert_bar != NULL) {
	Widget bar = w->viewport.vert_bar;

	if (!needsvert)	{
	    constraints->form.horiz_base = NULL;
	    if (destroy_scrollbars) {
		XtDestroyWidget(bar);
		w->viewport.vert_bar = NULL;
	    }
	}
	else {
	    int bw = bar->core.border_width;

	    XtResizeWidget(bar, (Dimension)XtWidth(bar), (Dimension)clip_height, (Dimension)bw);
	    XtMoveWidget(bar,
			(Position)(w->viewport.useright
				   ? XtWidth(w) - XtWidth(bar) - bw : -bw),
			(Position)(needshoriz && !w->viewport.usebottom
				   ? XtHeight(w->viewport.horiz_bar) : -bw));
	   XtSetMappedWhenManaged(bar, True);
	}
    }

    if (child != NULL) {
	XtResizeWidget(child, intended.width, intended.height, 0);
	MoveChild(w, needshoriz ? XtX(child) : 0,	needsvert ? XtY(child) : 0);
    }

    SendReport (w, XawPRAll);
}

/*
 * Function:
 *	ComputeWithForceBars
 *
 * Parameters:
 *	widget	    - viewport widget
 *	query	    - whether or not to query the child
 *	intended    - cache of the child's height is stored here
 *		      (used and returned)
 *	clip_width  - size of clip window (used and returned)
 *	clip_height - ""
 *
 * Description:
 *	Computes the layout give forcebars is set.
 */
static void
ComputeWithForceBars(Widget widget, Bool query, XtWidgetGeometry *intended,
		     int *clip_width, int *clip_height)
{
    ViewportWidget w = (ViewportWidget)widget;
    Widget child = w->viewport.child;
    XtWidgetGeometry preferred;

    /*
     * If forcebars then needs = allows = has
     * Thus if needsvert is set it MUST have a scrollbar
     */
    if (w->viewport.allowvert) {
	if (w->viewport.vert_bar == NULL)
	    w->viewport.vert_bar = CreateScrollbar(w, False);

	*clip_width -= XtWidth(w->viewport.vert_bar) +
		       XtBorderWidth(w->viewport.vert_bar);
    }

    if (w->viewport.allowhoriz) {
	if (w->viewport.horiz_bar == NULL)
	    w->viewport.horiz_bar = CreateScrollbar(w, True);

	*clip_height -= XtHeight(w->viewport.horiz_bar) +
			XtBorderWidth(w->viewport.horiz_bar);
    }

    AssignMax(*clip_width, 1);
    AssignMax(*clip_height, 1);

    if (!w->viewport.allowvert) {
	intended->height = (Dimension)*clip_height;
	intended->request_mode = CWHeight;
    }
    if (!w->viewport.allowhoriz) {
	intended->width = (Dimension)*clip_width;
	intended->request_mode = CWWidth;
    }

    if (query) {
	if (w->viewport.allowvert || w->viewport.allowhoriz) {
	    XtQueryGeometry(child, intended, &preferred);

	    if (!(intended->request_mode & CWWidth)) {
		if (preferred.request_mode & CWWidth)
		    intended->width = preferred.width;
		else
		    intended->width = XtWidth(child);
	    }

	    if (!(intended->request_mode & CWHeight)) {
		if (preferred.request_mode & CWHeight)
		    intended->height = preferred.height;
		else
		    intended->height = XtHeight(child);
	    }
	}
    }
    else {
	if (w->viewport.allowvert)
	    intended->height = XtHeight(child);
	if (w->viewport.allowhoriz)
	    intended->width = XtWidth(child);
    }

    if (*clip_width > (int)intended->width)
	intended->width = (Dimension)*clip_width;
    if (*clip_height > (int)intended->height)
	intended->height = (Dimension)*clip_height;
}

static void
XawViewportResize(Widget widget)
{
    ComputeLayout(widget, True, True);
}

/*ARGSUSED*/
static Boolean
Layout(FormWidget w, unsigned int width _X_UNUSED, unsigned int height _X_UNUSED, Bool force _X_UNUSED)
{
    ComputeLayout((Widget)w, True, True);
    w->form.preferred_width = XtWidth(w);
    w->form.preferred_height = XtHeight(w);

    return (False);
}

static void
ScrollUpDownProc(Widget widget, XtPointer closure, XtPointer call_data)
{
    ViewportWidget w = (ViewportWidget)closure;
    Widget child = w->viewport.child;
    int pix = (int)(long)call_data;
    int x, y;

    if (child == NULL)
	return;

    x = XtX(child) - (widget == w->viewport.horiz_bar ? pix : 0);
    y = XtY(child) - (widget == w->viewport.vert_bar ? pix : 0);
    MoveChild(w, x, y);
}

/*ARGSUSED*/
static void
ThumbProc(Widget widget, XtPointer closure, XtPointer call_data)
{
    ViewportWidget w = (ViewportWidget)closure;
    Widget child = w->viewport.child;
    float percent = *(float *)call_data;
    int x, y;

    if (child == NULL)
	return;

    if (widget == w->viewport.horiz_bar)
	x = (int)(-percent * XtWidth(child));
    else
	x = XtX(child);

    if (widget == w->viewport.vert_bar)
	y = (int)(-percent * XtHeight(child));
    else
	y = XtY(child);

    MoveChild(w, x, y);
}

static XtGeometryResult
TestSmaller(ViewportWidget w, XtWidgetGeometry *request,
	    XtWidgetGeometry *reply_return)
{
    if (request->width < XtWidth(w) || request->height < XtHeight(w))
	return (XtMakeGeometryRequest((Widget)w, request, reply_return));

    return (XtGeometryYes);
}

static XtGeometryResult
GeometryRequestPlusScrollbar(ViewportWidget w, Bool horizontal,
			     XtWidgetGeometry *request,
			     XtWidgetGeometry *reply_return)
{
    Widget sb;
    XtWidgetGeometry plusScrollbars;

    plusScrollbars = *request;
    if ((sb = w->viewport.horiz_bar) == NULL)
	sb = CreateScrollbar(w, horizontal);
    request->width = (Dimension)(request->width + XtWidth(sb));
    request->height = (Dimension)(request->height + XtHeight(sb));
    XtDestroyWidget(sb);
    return (XtMakeGeometryRequest((Widget)w, &plusScrollbars, reply_return));
}

#define WidthChange()	(request->width != XtWidth(w))
#define HeightChange()	(request->height != XtHeight(w))
static XtGeometryResult
QueryGeometry(ViewportWidget w, XtWidgetGeometry *request,
	      XtWidgetGeometry *reply_return)
{
    if (w->viewport.allowhoriz && w->viewport.allowvert)
	return (TestSmaller(w, request, reply_return));

    else if (w->viewport.allowhoriz && !w->viewport.allowvert) {
	if (WidthChange() && !HeightChange())
	    return (TestSmaller(w, request, reply_return));
	else if (!WidthChange() && HeightChange())
	    return (XtMakeGeometryRequest((Widget)w, request, reply_return));
	else if (WidthChange() && HeightChange())
	    return (GeometryRequestPlusScrollbar(w, True, request, reply_return));
	else /* !WidthChange() && !HeightChange() */
	    return (XtGeometryYes);
    }
    else if (!w->viewport.allowhoriz && w->viewport.allowvert) {
	if (!WidthChange() && HeightChange())
	    return (TestSmaller(w, request, reply_return));
	else if (WidthChange() && !HeightChange())
	    return (XtMakeGeometryRequest((Widget)w, request, reply_return));
	else if (WidthChange() && HeightChange())
	    return (GeometryRequestPlusScrollbar(w, False, request, reply_return));
	else /* !WidthChange() && !HeightChange() */
	    return (XtGeometryYes);
    }
    else /* (!w->viewport.allowhoriz && !w->viewport.allowvert) */
	return (XtMakeGeometryRequest((Widget)w, request, reply_return));
}
#undef WidthChange
#undef HeightChange

static XtGeometryResult
XawViewportGeometryManager(Widget child, XtWidgetGeometry *request,
			   XtWidgetGeometry *reply)
{
    ViewportWidget w = (ViewportWidget)child->core.parent;
    Bool rWidth = (request->request_mode & CWWidth) != 0;
    Bool rHeight = (request->request_mode & CWHeight) != 0;
    XtWidgetGeometry allowed;
    XtGeometryResult result;
    Bool reconfigured;
    Bool child_changed_size;
    unsigned int height_remaining;

    if (request->request_mode & XtCWQueryOnly)
	return (QueryGeometry(w, request, reply));

    if (child != w->viewport.child
	|| request->request_mode & (XtGeometryMask)(~(CWWidth | CWHeight | CWBorderWidth))
	|| ((request->request_mode & CWBorderWidth)
	    && request->border_width > 0))
	return (XtGeometryNo);

    allowed = *request;

    reconfigured = GetGeometry((Widget)w,
				rWidth ? request->width : XtWidth(w),
				rHeight ? request->height : XtHeight(w));

    child_changed_size = (rWidth && XtWidth(child) != request->width) ||
			 (rHeight && XtHeight(child) != request->height);

    height_remaining = XtHeight(w);
    if (rWidth && XtWidth(w) != request->width) {
	if (w->viewport.allowhoriz && request->width > XtWidth(w)) {
	    /* horizontal scrollbar will be needed so possibly reduce height */
	    Widget bar;

	    if ((bar = w->viewport.horiz_bar) == NULL)
		bar = CreateScrollbar(w, True);
	    height_remaining = (height_remaining - (unsigned)(XtHeight(bar) + XtBorderWidth(bar)));
	    reconfigured = True;
	}
	else
	    allowed.width = XtWidth(w);
    }
    if (rHeight && height_remaining != request->height) {
	if (w->viewport.allowvert && request->height > height_remaining) {
	    /* vertical scrollbar will be needed, so possibly reduce width */
	    if (!w->viewport.allowhoriz || request->width < XtWidth(w)) {
		Widget bar;

		if ((bar = w->viewport.vert_bar) == NULL)
		    bar = CreateScrollbar(w, False);
		if (!rWidth) {
		    allowed.width = XtWidth(w);
		    allowed.request_mode |= CWWidth;
		}
		if (allowed.width  >  XtWidth(bar) + XtBorderWidth(bar))
		    allowed.width = (Dimension)(allowed.width - (XtWidth(bar) + XtBorderWidth(bar)));
		else
		    allowed.width = 1;
		reconfigured = True;
	    }
	}
	else
	    allowed.height = (Dimension)height_remaining;
    }

    if (allowed.width != request->width || allowed.height != request->height) {
	*reply = allowed;
	result = XtGeometryAlmost;
    }
    else {
	if (rWidth)
	    XtWidth(child) = request->width;
	if (rHeight)
	    XtHeight(child) = request->height;
	result = XtGeometryYes;
    }

    if (reconfigured || child_changed_size)
	ComputeLayout((Widget)w, False, result == XtGeometryYes);

    return (result);
}

static Bool
GetGeometry(Widget w, unsigned int width, unsigned int height)
{
    XtWidgetGeometry geometry, return_geom;
    XtGeometryResult result;

    if (width == XtWidth(w) && height == XtHeight(w))
	return (False);

    geometry.request_mode = CWWidth | CWHeight;
    geometry.width = (Dimension)width;
    geometry.height = (Dimension)height;

    if (XtIsRealized(w)) {
	if (((ViewportWidget)w)->viewport.allowhoriz && width > XtWidth(w))
	    geometry.width = XtWidth(w);
	if (((ViewportWidget)w)->viewport.allowvert && height > XtHeight(w))
	    geometry.height = XtHeight(w);
    }
    else {
	/* This is the Realize call; we'll inherit a w&h iff none currently */
	if (XtWidth(w) != 0) {
	    if (XtHeight(w) != 0)
		return (False);
	    geometry.width = XtWidth(w);
	}
	if (XtHeight(w) != 0)
	    geometry.height = XtHeight(w);
    }

    result = XtMakeGeometryRequest(w, &geometry, &return_geom);
    if (result == XtGeometryAlmost)
	result = XtMakeGeometryRequest(w, &return_geom, NULL);

    return (result == XtGeometryYes);
}

static XtGeometryResult
XawViewportQueryGeometry(Widget w, XtWidgetGeometry *constraints,
			 XtWidgetGeometry *reply)
{
    if (((ViewportWidget)w)->viewport.child != NULL)
	return (XtQueryGeometry(((ViewportWidget)w)->viewport.child,
				constraints, reply));

    return (XtGeometryYes);
}

void
XawViewportSetLocation
(
 Widget gw,
#if NeedWidePrototypes
 double xoff, double yoff
#else
 float xoff, float yoff
#endif
 )
{
    ViewportWidget w = (ViewportWidget)gw;
    Widget child = w->viewport.child;
    int x, y;

    if (xoff > 1.0)			/* scroll to right */
	x = XtWidth(child);
    else if (xoff < 0.0)		/* if the offset is < 0.0 nothing */
	x = XtX(child);
    else
	x = (int)((float)XtWidth(child) * xoff);

    if (yoff > 1.0)
	y = XtHeight(child);
    else if (yoff < 0.0)
	y = XtY(child);
    else
	y = (int)((float)XtHeight(child) * yoff);

    MoveChild (w, -x, -y);
}

void
XawViewportSetCoordinates(Widget gw,
#if NeedWidePrototypes
	int x, int y
#else
	Position x, Position y
#endif
)
{
    ViewportWidget w = (ViewportWidget)gw;
    Widget child = w->viewport.child;

    if (x > XtWidth(child))
	x = (Position)XtWidth(child);
    else if (x < 0)
	x = (Position)XtX(child);

    if (y > XtHeight(child))
	y = (Position)XtHeight(child);
    else if (y < 0)
	y = (Position)XtY(child);

    MoveChild (w, -x, -y);
}
