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
#include <stdio.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xfuncs.h>
#include <X11/Xaw/StripCharP.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

#define MS_PER_SEC 1000
#define NUM_VALUEDATA(w) (sizeof((w)->strip_chart.valuedata) / \
                          sizeof((w)->strip_chart.valuedata[0]))

/*
 * Class Methods
 */
static void XawStripChartInitialize(Widget, Widget, ArgList, Cardinal*);
static void XawStripChartDestroy(Widget);
static void XawStripChartRedisplay(Widget, XEvent*, Region);
static void XawStripChartResize(Widget);
static Boolean XawStripChartSetValues(Widget, Widget, Widget,
				      ArgList, Cardinal*);

/*
 * Prototypes
 */
static void CreateGC(StripChartWidget, unsigned int);
static void DestroyGC(StripChartWidget, unsigned int);
static void draw_it(XtPointer, XtIntervalId*);
static void MoveChart(StripChartWidget, Bool);
static int repaint_window(StripChartWidget, int, int);

/*
 * Initialization
 */
#define offset(field)	XtOffsetOf(StripChartRec, field)
static XtResource resources[] = {
  {
    XtNwidth,
    XtCWidth,
    XtRDimension,
    sizeof(Dimension),
    offset(core.width),
    XtRImmediate,
    (XtPointer)
    120
  },
  {
    XtNheight,
    XtCHeight,
    XtRDimension,
    sizeof(Dimension),
    offset(core.height),
    XtRImmediate,
    (XtPointer)120
  },
  {
    XtNupdate,
    XtCInterval,
    XtRInt,
    sizeof(int),
    offset(strip_chart.update),
    XtRImmediate,
    (XtPointer)10
  },
  {
    XtNminScale,
    XtCScale,
    XtRInt,
    sizeof(int),
    offset(strip_chart.min_scale),
    XtRImmediate,
    (XtPointer)1
  },
  {
    XtNforeground,
    XtCForeground,
    XtRPixel,
    sizeof(Pixel),
    offset(strip_chart.fgpixel),
    XtRString,
    (XtPointer)XtDefaultForeground
  },
  {
    XtNhighlight,
    XtCForeground,
    XtRPixel,
    sizeof(Pixel),
    offset(strip_chart.hipixel),
    XtRString,
    (XtPointer)XtDefaultForeground
  },
  {
    XtNgetValue,
    XtCCallback,
    XtRCallback,
    sizeof(XtPointer),
    offset(strip_chart.get_value),
    XtRImmediate,
    NULL
  },
  {
    XtNjumpScroll,
    XtCJumpScroll,
    XtRInt,
    sizeof(int),
    offset(strip_chart.jump_val),
    XtRImmediate,
    (XtPointer)DEFAULT_JUMP
  },
};
#undef offset

StripChartClassRec stripChartClassRec = {
  /* core */
  {
    (WidgetClass)&simpleClassRec,	/* superclass */
    "StripChart",			/* class_name */
    sizeof(StripChartRec),		/* widget_size */
    XawInitializeWidgetSet,		/* class_initialize */
    NULL,				/* class_part_initialize */
    False,				/* class_inited */
    XawStripChartInitialize,		/* initialize */
    NULL,				/* initialize_hook */
    XtInheritRealize,			/* realize */
    NULL,				/* actions */
    0,					/* num_actions */
    resources,				/* resources */
    XtNumber(resources),		/* num_resources */
    NULLQUARK,				/* xrm_class */
    True,				/* compress_motion */
    XtExposeCompressMultiple		/* compress_exposure */
    | XtExposeGraphicsExposeMerged,
    True,				/* compress_enterleave */
    False,				/* visible_interest */
    XawStripChartDestroy,		/* destroy */
    XawStripChartResize,		/* resize */
    XawStripChartRedisplay,		/* expose */
    XawStripChartSetValues,		/* set_values */
    NULL,				/* set_values_hook */
    NULL,				/* set_values_almost */
    NULL,				/* get_values_hook */
    NULL,				/* accept_focus */
    XtVersion,				/* version */
    NULL,				/* callback_private */
    NULL,				/* tm_table */
    XtInheritQueryGeometry,		/* query_geometry */
    XtInheritDisplayAccelerator,	/* display_accelerator */
    NULL,				/* extension */
  },
  /* simple */
  {
    XtInheritChangeSensitive,		/* change_sensitive */
#ifndef OLDXAW
    NULL
#endif
  },
  /* strip_chart_class */
  {
    NULL
  }
};

WidgetClass stripChartWidgetClass = (WidgetClass)&stripChartClassRec;

/*
 * Implementation
 */
/*
 * Function:
 *	CreateGC
 *
 * Parameters:
 *	w     - strip chart widget
 *	which - GC's to create
 *
 * Description:
 *	Creates the GC's
 */
static void
CreateGC(StripChartWidget w, unsigned int which)
{
    XGCValues myXGCV;

    if (which & FOREGROUND) {
	myXGCV.foreground = w->strip_chart.fgpixel;
	w->strip_chart.fgGC = XtGetGC((Widget)w, GCForeground, &myXGCV);
    }

    if (which & HIGHLIGHT) {
	myXGCV.foreground = w->strip_chart.hipixel;
	w->strip_chart.hiGC = XtGetGC((Widget)w, GCForeground, &myXGCV);
    }
}

/*
 * Function:
 *	DestroyGC
 *
 * Arguments:
 *	w     - strip chart widget
 *	which - which GC's to destroy
 *
 * Description:
 *	Destroys the GC's
 */
static void
DestroyGC(StripChartWidget w, unsigned int which)
{
    if (which & FOREGROUND)
	XtReleaseGC((Widget)w, w->strip_chart.fgGC);

    if (which & HIGHLIGHT)
	XtReleaseGC((Widget)w, w->strip_chart.hiGC);
}

/*ARGSUSED*/
static void
XawStripChartInitialize(Widget greq _X_UNUSED, Widget gnew,
			ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    StripChartWidget w = (StripChartWidget)gnew;

    if (w->strip_chart.update > 0)
    w->strip_chart.interval_id =
    XtAppAddTimeOut(XtWidgetToApplicationContext(gnew),
		    (unsigned long)(w->strip_chart.update * MS_PER_SEC),
		    draw_it, (XtPointer)gnew);
    CreateGC(w, ALL_GCS);

    w->strip_chart.scale = w->strip_chart.min_scale;
    w->strip_chart.interval = 0;
    w->strip_chart.max_value = 0.0;
    w->strip_chart.points = NULL;
    XawStripChartResize(gnew);
}

static void
XawStripChartDestroy(Widget gw)
{
    StripChartWidget w = (StripChartWidget)gw;

    if (w->strip_chart.update > 0)
	XtRemoveTimeOut(w->strip_chart.interval_id);
    if (w->strip_chart.points)
	XtFree((char *)w->strip_chart.points);
    DestroyGC(w, ALL_GCS);
}

/*
 * NOTE: This function really needs to receive graphics exposure
 *       events, but since this is not easily supported until R4 I am
 *       going to hold off until then.
 */
/*ARGSUSED*/
static void
XawStripChartRedisplay(Widget w, XEvent *event, Region region _X_UNUSED)
{
    if (event->type == GraphicsExpose)
	(void)repaint_window((StripChartWidget)w, event->xgraphicsexpose.x,
			     event->xgraphicsexpose.width);
    else
	(void)repaint_window((StripChartWidget)w, event->xexpose.x,
			     event->xexpose.width);
}

/*ARGSUSED*/
static void
draw_it(XtPointer client_data, XtIntervalId *id _X_UNUSED)
{
    StripChartWidget w = (StripChartWidget)client_data;
    double value;

    if (w->strip_chart.update > 0)
	w->strip_chart.interval_id =
	    XtAppAddTimeOut(XtWidgetToApplicationContext((Widget)w),
			    (unsigned long)(w->strip_chart.update * MS_PER_SEC), draw_it,
			    client_data);

    if ((w->strip_chart.interval >= XtWidth(w)) ||
        ((Cardinal)w->strip_chart.interval >= NUM_VALUEDATA(w)))
	MoveChart((StripChartWidget)w, True);

    /* Get the value, stash the point and draw corresponding line */
    if (w->strip_chart.get_value == NULL)
	return;

    XtCallCallbacks((Widget)w, XtNgetValue, (XtPointer)&value);

    /*
     * Keep w->strip_chart.max_value up to date, and if this data
     * point is off the graph, change the scale to make it fit
     */
    if (value > w->strip_chart.max_value) {
	w->strip_chart.max_value = value;
	if (XtIsRealized((Widget)w) &&
	    w->strip_chart.max_value > w->strip_chart.scale) {
	    XClearWindow(XtDisplay(w), XtWindow(w));
	    w->strip_chart.interval = repaint_window(w, 0, XtWidth(w));
	}
    }

    w->strip_chart.valuedata[w->strip_chart.interval] = value;
    if (XtIsRealized((Widget)w)) {
	int y = (int)(XtHeight(w) - XtHeight(w) * value
		     / w->strip_chart.scale);

	XFillRectangle(XtDisplay(w), XtWindow(w), w->strip_chart.fgGC,
		       w->strip_chart.interval, y,
		       1, (unsigned)(XtHeight(w) - y));

	/*
	 * Fill in the graph lines we just painted over
	 */
	if (w->strip_chart.points != NULL) {
	    w->strip_chart.points[0].x = (short)w->strip_chart.interval;
	    XDrawPoints(XtDisplay(w), XtWindow(w), w->strip_chart.hiGC,
			w->strip_chart.points, w->strip_chart.scale - 1,
			CoordModePrevious);
	}

	XFlush(XtDisplay(w));		    /* Flush output buffers */
    }
    w->strip_chart.interval++;		    /* Next point */
}

/* Blts data according to current size, then redraws the stripChart window
 * Next represents the number of valid points in data.  Returns the (possibly)
 * adjusted value of next.  If next is 0, this routine draws an empty window
 * (scale - 1 lines for graph).  If next is less than the current window width,
 * the returned value is identical to the initial value of next and data is
 * unchanged.  Otherwise keeps half a window's worth of data.  If data is
 * changed, then w->strip_chart.max_value is updated to reflect the
 * largest data point
 */
static int
repaint_window(StripChartWidget w, int left, int width)
{
    int i, j;
    int next = w->strip_chart.interval;
    int scale = w->strip_chart.scale;
    int scalewidth = 0;

    /* Compute the minimum scale required to graph the data, but don't go
       lower than min_scale */
    if (w->strip_chart.interval != 0 || scale <= w->strip_chart.max_value)
	scale = (int)(w->strip_chart.max_value + 1);
    if (scale < w->strip_chart.min_scale)
	scale = w->strip_chart.min_scale;

    if (scale != w->strip_chart.scale) {
	w->strip_chart.scale = scale;
	left = 0;
	width = next;
	scalewidth = XtWidth(w);

	XawStripChartResize((Widget)w);

	if (XtIsRealized((Widget)w))
	    XClearWindow(XtDisplay(w), XtWindow(w));
    }

    if (XtIsRealized((Widget)w)) {
	Display *dpy = XtDisplay(w);
	Window win = XtWindow(w);

	width += left - 1;
	if (!scalewidth)
	    scalewidth = width;

	if (next < ++width)
	    width = next;

	if ((Cardinal)width > NUM_VALUEDATA(w))
	    width = NUM_VALUEDATA(w);

	/* Draw data point lines */
	for (i = left; i < width; i++) {
	    int y = (int)(XtHeight(w) - (XtHeight(w) * w->strip_chart.valuedata[i])
				   / w->strip_chart.scale);

	    XFillRectangle(dpy, win, w->strip_chart.fgGC,
			   i, y, 1, (unsigned)(XtHeight(w) - y));
	}

	/* Draw graph reference lines */
	for (i = 1; i < w->strip_chart.scale; i++) {
	    j = i * ((int)XtHeight(w) / w->strip_chart.scale);
	    XDrawLine(dpy, win, w->strip_chart.hiGC, left, j, scalewidth, j);
	}
    }
    return (next);
}

/*
 * Function:
 *	MoveChart
 *
 * Parameters:
 *	w - chart widget
 *	blit - blit the bits?
 *
 * Description:
 *	Moves the chart over when it would run off the end.
 */
static void
MoveChart(StripChartWidget w, Bool blit)
{
    double old_max;
    int left, i, j;
    int next = w->strip_chart.interval;

    if (!XtIsRealized((Widget)w))
	return;

    if (XtWidth(w) > NUM_VALUEDATA(w))
	j = (int) NUM_VALUEDATA(w);
    else
	j = (int) XtWidth(w);
    if (w->strip_chart.jump_val < 0)
	w->strip_chart.jump_val = DEFAULT_JUMP;
    if (w->strip_chart.jump_val == DEFAULT_JUMP)
	j = j >> 1;
    else {
	j -= w->strip_chart.jump_val;
	if (j < 0)
	    j = 0;
    }

    (void)memmove((char *)w->strip_chart.valuedata,
		  (char *)(w->strip_chart.valuedata + next - j),
		  (size_t)j * sizeof(double));
    next = w->strip_chart.interval = j;

    /*
     * Since we just lost some data, recompute the
     * w->strip_chart.max_value
     */
    old_max = w->strip_chart.max_value;
    w->strip_chart.max_value = 0.0;
    for (i = 0; i < next; i++) {
	if (w->strip_chart.valuedata[i] > w->strip_chart.max_value)
	    w->strip_chart.max_value = w->strip_chart.valuedata[i];
    }

    if (!blit)
	return;

    if (old_max != w->strip_chart.max_value) {
	XClearWindow(XtDisplay(w), XtWindow(w));
	repaint_window(w, 0, XtWidth(w));
	return;
    }

    XCopyArea(XtDisplay((Widget)w), XtWindow((Widget)w), XtWindow((Widget)w),
	      w->strip_chart.hiGC, (int)XtWidth(w) - j, 0, (unsigned)j, XtHeight(w), 0, 0);

    XClearArea(XtDisplay((Widget)w), XtWindow((Widget)w),
	       j, 0, (unsigned)(XtWidth(w) - j), XtHeight(w), False);

    /* Draw graph reference lines */
    left = j;
    for (i = 1; i < w->strip_chart.scale; i++) {
	j = i * (XtHeight(w) / w->strip_chart.scale);
	XDrawLine(XtDisplay((Widget)w), XtWindow((Widget)w),
		  w->strip_chart.hiGC, left, j, XtWidth(w), j);
    }
}

/*ARGSUSED*/
static Boolean
XawStripChartSetValues(Widget current, Widget request _X_UNUSED, Widget cnew,
		       ArgList args _X_UNUSED, Cardinal *num_args _X_UNUSED)
{
    StripChartWidget old = (StripChartWidget)current;
    StripChartWidget w = (StripChartWidget)cnew;
    Bool ret_val = False;
    unsigned int new_gc = NO_GCS;

    if (w->strip_chart.update != old->strip_chart.update) {
	if (old->strip_chart.update > 0)
	    XtRemoveTimeOut(old->strip_chart.interval_id);
	if (w->strip_chart.update > 0)
	    w->strip_chart.interval_id =
		XtAppAddTimeOut(XtWidgetToApplicationContext(cnew),
				(unsigned long)(w->strip_chart.update * MS_PER_SEC),
				draw_it, (XtPointer)w);
    }

    if (w->strip_chart.min_scale > w->strip_chart.max_value + 1)
	ret_val = True;

    if (w->strip_chart.fgpixel != old->strip_chart.fgpixel) {
	new_gc |= FOREGROUND;
	ret_val = True;
    }

    if (w->strip_chart.hipixel != old->strip_chart.hipixel) {
	new_gc |= HIGHLIGHT;
	ret_val = True;
    }

    DestroyGC(old, new_gc);
    CreateGC(w, new_gc);

    return (Boolean)(ret_val);
}

/*
 * Function:
 *	XawStripChartResize
 *
 * Parameters:
 *	w - StripChart widget
 *
 * Description:
 *	Sets up the polypoint that will be used to draw in the graph lines.
 */
static void
XawStripChartResize(Widget widget)
{
    StripChartWidget w = (StripChartWidget)widget;
    XPoint *points;
    Cardinal size;
    int i;

    if (w->strip_chart.scale <= 1) {
	XtFree((char *)w->strip_chart.points);
	w->strip_chart.points = NULL;
	return;
    }

    size = (Cardinal)(sizeof(XPoint) * (size_t)(w->strip_chart.scale - 1));

    points = (XPoint *)XtRealloc((XtPointer)w->strip_chart.points, size);
    w->strip_chart.points = points;

    /* Draw graph reference lines into clip mask */

    for (i = 1; i < w->strip_chart.scale; i++) {
	points[i - 1].x = 0;
	points[i - 1].y = (short)(XtHeight(w) / w->strip_chart.scale);
    }
}
